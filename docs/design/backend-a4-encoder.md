# A4 / N2d — arm64 encoder + Mach-O object + link via system `ld` (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave. Issue **#385**. Branch
`fix/issue-385-encoder` (the A4 mini-umbrella; base = the umbrella carrying A1+A2+A3+#443). Base for
the design: `docs/design/own-backend-architecture.md` §3.5 (object emission) + §5 (D-2 runtime link,
D-5 system-linker sequencing), `docs/design/backend-a3-regalloc.md` §3.5/§6.1 (the frame deferral A4
owns), grounded in the merged code (`src/backend/minst.tks`, `regalloc.tks`, `abi_aapcs64.tks`,
`minst_interp.tks`, `src/lir/lir.tks`, `src/build/project.tks`).

> This is a PLAN. It designs **the KEYSTONE**: the first point where the own backend produces a REAL
> native executable to diff against the C backend — "the birth of the C-vs-own differential". A4
> consumes the fully-physical, RPO-numbered `MModule` `regalloc_module` produces (multi-block acyclic
> `if`/`match` CFGs now allocate correctly after #443), turns it into arm64 machine bytes, wraps them
> in a Mach-O object, links that against the C-built `teko_rt` via `cc`-as-linker, and runs it. A4
> owns the frame prologue/epilogue + `MFrameAddr` resolution A3 explicitly deferred (§6.1, "A4-frame").
> It emits **no** cross-target code (ELF/PE/x86/riscv/wasm are Phases B/C), does **not** add the
> `--backend` flag (D1/#390 owns it — A4 uses a temporary env seam), and does **not** build the own
> linker (Phase E/#226 — A4 links via `cc`). The hard tail (FP encoding, big-frame/big-imm forms,
> data globals/GOT relocs, the own-linker `__info_plist`) is scoped forward as **named** honest-stops
> for 0.3.1 completion, each behind a specific slice.

---

## 0. TL;DR — the recommended shape

- **Sub-slice order (each independently gate-able, smallest-safe-step):**
  `A4-1` encoder-core (position-independent integer/control/memory encodings) →
  `A4-2` frame finalization (prologue/epilogue + `MFrameAddr`/`MRet` expansion, the A3 "A4-frame"
  deferral) → `A4-3` branch layout + intra-function fixups + relocation records →
  `A4-4` Mach-O object writer → `A4-5` link via `cc`/`ld` + `emit_native` wiring + **the C-vs-own
  differential** (the keystone; **#385 CLOSES here**).
- **Encoder model:** each function encodes to a `[]u32` word buffer (branch fixups patch whole words —
  arm64 is fixed-width, so no relaxation/iteration), plus a `[]Reloc` for symbol references
  (`MRelocKind` maps 1:1 to the three ARM64 external reloc types). Module assembly flattens the words
  to `[]byte` little-endian (`(w & 0xFF) to byte` idiom) and rebases per-function reloc offsets.
- **Frame:** a pure `compute_frame_layout(abi, f) -> FrameLayout` side-table (frame size, the
  callee-saved save list **re-derived by scanning the physical `MFunc`** — `MFunc` does not carry
  `used_callee_saved`, §3.1 — and a `slot → SP-offset` map). The encoder consumes it to prepend the
  prologue and to expand `MFrameAddr → ADD dst, sp, #off` and `MRet → epilogue + ret`, **without
  mutating the block stream** — so the A3 interp oracle stays valid on the pre-encode `MFunc`.
- **Differential:** a new `scripts/diff_c_own.sh` leg over an **`exit(n)`-terminated** integer/control
  corpus: `C-native exit == own-native exit (== interp)`. Gated on **macOS-arm64** (needs the host
  Mach-O `ld`); honest-skip elsewhere with a named reason.
- **No genuine HALT.** Two decisions are resolved law-first and recorded (§7): re-derive the
  callee-saved set (M.1) and use a temporary `TEKO_BACKEND=native` env seam (M.4, D1 supersedes). One
  adjacent finding is **REPORTED up**, not resolved here (§8: the virtual-`main` trailing-value exit
  code vs C-`main`'s unconditional `return 0`).

---

## 1. The assessed starting point (what A4 consumes)

`regalloc_module(aapcs64(), select_module(lower_program(prog)))` yields an `MModule`
(`minst.tks:847`) whose every `MFunc` (`minst.tks:823`) is:

- **fully physical** — every `MReg` operand has `is_phys=true` (`all_physical`, `regalloc.tks:1597`);
  register ids are ISA numbers (x0..x30, SP=31 GPR; v0..v31 FPR — `minst.tks:22`);
- **RPO-numbered, acyclic, multi-block** — after #443 the blocks in `MFunc.blocks` are in a topological
  order that makes `if`/`match` merges allocate correctly; a `loop` back-edge still honest-stops in
  regalloc (`A3-loop`, `backend-a3-regalloc.md` §6.2), so A4 never sees a cyclic CFG;
- **frame-carrying but SP-agnostic** — `MFunc.frame: []lir::LAlloca` (`minst.tks:837`) is the ordered
  slot table (one entry per `LAlloca` + one per spill); `MFrameAddr{dst,slot}` (`minst.tks:594`) is the
  abstract "address of slot" A3 deferred to A4 for SP/FP-offset resolution;
- **symbol-final** — `MCall.sym` / `MAdrp.sym` / `MAddLo.sym` are already the final linker names
  (`mangle_fn_symbol`, `lower.tks:262`; runtime `tk_*`); A4 never re-mangles, only prefixes `_` for
  Mach-O.

The entry function is the synthesized virtual-`main`: `new_func("main", 0, [], I32)`
(`lower.tks:4054`) — symbol `main`, nullary, returns i32. On Mach-O it becomes the defined symbol
`_main`, exactly the entry the C crt0 calls. The interp oracle runs it as `minst_interp(m, "main",
args)` (`minst_interp.tks:1117`); only `tk_exit` executes (exit code read from x0, `interp_call`
`:901`), every other call is golden-dump-only.

**The three tables ride through unchanged** into the object: `MModule.rodata` (`[]LRodata` — interned
string bytes, `lir.tks:146`), `MModule.globals` (`[]LGlobal` — none produced by the N2 subset yet,
`lir.tks:151`), `MModule.layouts` (struct layouts, informational to A4).

### 1.1 The 29-case `MInst` is the encoder's whole input alphabet

`MInst` (`minst.tks:787`) is the closed 29-case variant. A4's encoder is a total per-case match over
it, mirroring `print_minst`'s dispatch (`minst.tks:1868`) and `map_minst_regs`'s (`regalloc.tks:1249`)
— the same "no case may be missed" discipline (a missed case = a wrong or absent instruction, caught
by golden bytes). The reference for what each case *means* is the golden dumper `print_minst`: A4 is
"the same table, emitting bytes instead of text".

---

## 2. The encoder model (`src/backend/encode_arm64.tks`)

### 2.1 Byte/word buffers — the established idiom

The compiler already builds binary buffers as `[]byte` via `teko::list::push(buf, (x & 0xFF) to
byte)` with little-endian splitting (`src/compress/compress.tks:112-123`, `zip_u32`). A4 reuses it
verbatim. Instruction words are `u32`; the object's `__text` is their little-endian flattening.

```teko
/**
 * emit_u32_le — append `w` to `buf` as four little-endian bytes (the arm64
 * instruction-word / Mach-O little-endian-field emit primitive), the shared
 * `[]byte` idiom (`src/compress/compress.tks:117`). All bit-slicing stays in
 * u32 arithmetic; only the final `& 0xFF` narrows to `byte` (the VM byte-width
 * anchoring gotcha — never widen a `byte` mid-expression).
 *
 * @param []byte buf  the buffer to extend
 * @param u32 w  the 32-bit word to append
 * @return []byte  `buf` followed by w's four little-endian bytes
 */
pub fn emit_u32_le(buf: []byte, w: u32) -> []byte {
    mut b = teko::list::push(buf, (w & 0xFF) to byte)
    b = teko::list::push(b, ((w >> 8) & 0xFF) to byte)
    b = teko::list::push(b, ((w >> 16) & 0xFF) to byte)
    teko::list::push(b, ((w >> 24) & 0xFF) to byte)
}
```

Per **function**, the encoder builds a `[]u32` word list (not `[]byte`) so a forward branch's
displacement can be patched into a whole word after every block's word-offset is known, with no
byte-window surgery. The module writer flattens each function's words to bytes.

### 2.2 The relocation record + encoded-function/-module types

`MRelocKind` (`minst.tks:42`, `{PageHi; PageLo; Call}`) maps **1:1** onto the three ARM64 external
relocation types A4 needs — this is why isel already tags every symbol reference with its kind:

| `MRelocKind` | source `MInst` | arm64 instr | Mach-O reloc type | value |
|---|---|---|---|---|
| `PageHi` | `MAdrp` | ADRP | `ARM64_RELOC_PAGE21` | 3 |
| `PageLo` | `MAddLo` | ADD (lo12) | `ARM64_RELOC_PAGEOFF12` | 4 |
| `Call` | `MCall` (BL) | BL | `ARM64_RELOC_BRANCH26` | 2 |

```teko
/**
 * Reloc — one symbol relocation the object writer turns into a Mach-O
 * `relocation_info` entry: the byte offset within `__text` (function-relative
 * during encoding, module-rebased at assembly) where the fixup applies, the
 * target symbol's already-final name, and which `MRelocKind` field it patches.
 * A2 pre-tags every symbol reference with its kind (`minst.tks:42`), so A4 owes
 * no analysis — one `Reloc` per `MAdrp`/`MAddLo`/`MCall`.
 *
 * @since #385 A4-3
 */
pub type Reloc = struct {
    /**
     * offset — the byte offset of the relocated instruction word.
     */
    offset: u32
    /**
     * sym — the target symbol's final name (unprefixed; the writer adds `_`).
     */
    sym: str
    /**
     * kind — which ARM64 relocation this instruction owes.
     */
    kind: MRelocKind
}

/**
 * EncodedFunc — one function lowered to machine bytes: its symbol, its `__text`
 * words (prologue ++ every block's encoded instructions ++ epilogues), and its
 * function-relative relocations (offsets rebased to the module `__text` at
 * assembly).
 *
 * @since #385 A4-3
 */
pub type EncodedFunc = struct {
    /**
     * symbol — the function's final symbol (unprefixed).
     */
    symbol: str
    /**
     * words — the encoded instruction stream, in layout order (u32 words,
     * flattened little-endian by the module writer).
     */
    words: []u32
    /**
     * relocs — the symbol relocations, offsets relative to this function's
     * `__text` base.
     */
    relocs: []Reloc
}

/**
 * EncodedModule — the whole program lowered to section images + a flat symbol
 * table: the concatenated `__text` bytes, the `__const` (rodata) bytes, the
 * defined+undefined symbol list, and the module-rebased relocations. The Mach-O
 * writer (A4-4) turns this into an object file; no arm64 knowledge remains past
 * this boundary (the writer is ISA-agnostic — Phase B reuses its shape).
 *
 * @since #385 A4-4
 */
pub type EncodedModule = struct {
    /**
     * text — the `__TEXT,__text` section image (all functions concatenated).
     */
    text: []byte
    /**
     * rodata — the `__TEXT,__const` section image (interned string bytes).
     */
    rodata: []byte
    /**
     * symbols — the symbol table: one defined entry per function (and per
     * rodata entry), plus one undefined entry per externally-referenced symbol.
     */
    symbols: []Symbol
    /**
     * relocs — every relocation, `text`-section-relative, in emission order.
     */
    relocs: []Reloc
}

/**
 * Symbol — one symbol-table entry: its name, whether it is defined in this
 * object, which section it lives in (when defined), and its offset within that
 * section. Undefined symbols (the `tk_*` runtime + any not-yet-emitted callee)
 * carry `defined=false`; the relocations reference them by name, the writer by
 * table index.
 *
 * @since #385 A4-4
 */
pub type Symbol = struct {
    /**
     * name — the symbol's final name (unprefixed; the writer adds the `_`).
     */
    name: str
    /**
     * defined — true iff this object defines the symbol (a `__text`/`__const`
     * datum), false for an external reference.
     */
    defined: bool
    /**
     * sect — the 1-based Mach-O section index the definition lives in
     * (meaningful only when `defined`); the writer fixes the ordering.
     */
    sect: u8
    /**
     * offset — the byte offset of the definition within its section
     * (meaningful only when `defined`).
     */
    offset: u32
}
```

### 2.3 Register + immediate encoding helpers

```teko
/**
 * enc_reg — a physical `MReg`'s 5-bit ISA field value: its `id` directly
 * (x0..x30/SP=31 for GPR, v0..v31 for FPR). SP vs XZR (both id 31) is decided
 * by the INSTRUCTION, not the register — CMP hard-wires XZR, `MFrameAddr`'s ADD
 * hard-wires SP — so no per-register disambiguation is needed here.
 *
 * @param MReg r  a physical register (is_phys must hold; A4 runs post-regalloc)
 * @return u32  its 5-bit ISA field value
 */
fn enc_reg(r: MReg) -> u32 { r.id }

/**
 * sf_bit — the size flag for a `wide` operand: 1 (the 64-bit X form) shifted to
 * bit 31, or 0 (the 32-bit W form). The single most-repeated encoding subfield.
 *
 * @param bool wide  true for the 64-bit form
 * @return u32  `1 << 31` when wide, else 0
 */
fn sf_bit(wide: bool) -> u32 { if wide { 0x80000000 } else { 0 } }
```

The i64 immediate on `MAluImm`/`MCmpImm` is masked to the 12-bit unsigned field; a **negative** imm
selects the SUB/ADD-flipped form (an ADD `#-k` encodes as SUB `#k`), and an imm outside `[0, 4095]`
is a **named honest-stop** (`A4-bigimm`, §6) — isel folds only small field offsets and peephole
constants into `MAluImm`, so this is reached rarely and closed later with a MOVZ+op sequence.

### 2.4 The per-case encoding table (the bit-exact ground truth)

The load-bearing cases the A4 differential corpus (integer/control/memory) exercises. Every base word
below is the operand-zeroed template; `enc_reg`/immediate fields OR into it. **The implementer MUST
re-derive each against `llvm-mc -arch=arm64 --show-encoding` (or `clang -c` + `llvm-objdump -d`) — the
table is the spec, the assembler is the oracle.** Two worked examples pin the convention:
`movz x0,#42` = `0xD2800000 | (42<<5)` = `0xD2800540` → bytes `40 05 80 D2`; `ret` = `0xD65F03C0` →
bytes `C0 03 5F D6`.

| `MInst` | arm64 | base (64-bit / 32-bit) | field placement | slice |
|---|---|---|---|---|
| `MMovZ` | MOVZ | `0xD2800000` / `0x52800000` | `\| (hw<<21) \| (imm16<<5) \| Rd`, hw=shift/16 | A4-1 |
| `MMovK` | MOVK | `0xF2800000` / `0x72800000` | same | A4-1 |
| `MMovN` | MOVN | `0x92800000` / `0x12800000` | same | A4-1 |
| `MMov` | ORR (reg, Rn=XZR) | `0xAA0003E0` / `0x2A0003E0` | `\| (Rm<<16) \| Rd` | A4-1 |
| `MAlu Add` | ADD (shifted reg) | `0x8B000000` / `0x0B000000` | `\| (Rm<<16) \| (Rn<<5) \| Rd` | A4-1 |
| `MAlu Sub` | SUB | `0xCB000000` / `0x4B000000` | same | A4-1 |
| `MAlu And/Orr/Eor` | AND/ORR/EOR | `0x8A/0xAA/0xCA 000000` | same | A4-1 |
| `MAlu Mul` | MADD (Ra=XZR) | `0x9B007C00` / `0x1B007C00` | `\| (Rm<<16) \| (Rn<<5) \| Rd` | A4-1 |
| `MAlu Sdiv/Udiv` | SDIV/UDIV | `0x9AC00C00 / 0x9AC00800` | `\| (Rm<<16) \| (Rn<<5) \| Rd` | A4-1 |
| `MAlu Lsl/Lsr/Asr` | LSLV/LSRV/ASRV | `0x9AC02000/2400/2800` | same | A4-1 |
| `MAluImm Add/Sub` | ADD/SUB (imm) | `0x91/0xD1 000000` (+32-bit) | `\| (imm12<<10) \| (Rn<<5) \| Rd` | A4-1 |
| `MMSub` | MSUB (`dst=c-a*b`) | `0x9B008000` / `0x1B008000` | `\| (b<<16) \| (c<<10) \| (a<<5) \| dst` | A4-1 |
| `MNeg` | SUB (Rn=XZR) | `0xCB0003E0` / `0x4B0003E0` | `\| (Rm<<16) \| Rd` | A4-1 |
| `MMvn` | ORN (Rn=XZR) | `0xAA2003E0` / `0x2A2003E0` | `\| (Rm<<16) \| Rd` | A4-1 |
| `MCmp` | SUBS (reg, Rd=XZR) | `0xEB00001F` / `0x6B00001F` | `\| (Rm<<16) \| (Rn<<5)` | A4-1 |
| `MCmpImm` | SUBS (imm, Rd=XZR) | `0xF100001F` / `0x7100001F` | `\| (imm12<<10) \| (Rn<<5)` | A4-1 |
| `MCSet` | CSINC (Rn=Rm=XZR) | `0x9A9F07E0` / `0x1A9F07E0` | `\| (invert(cond)<<12) \| Rd` | A4-1 |
| `MExt Sxtb..Uxth` | SBFM/UBFM | (per-op immr/imms) | `\| (Rn<<5) \| Rd` | A4-1 |
| `MLoad`/`MStore` | LDR/STR (unsigned off) | `0xF9400000 / 0xF9000000` (+B/H/W/sign) | `\| ((off/size)<<10) \| (Rn<<5) \| Rt` | A4-1 |
| `MFrameAddr` | ADD (imm, Rn=SP) | `0x91000000` | `\| (slotoff<<10) \| (31<<5) \| Rd` (via FrameLayout) | A4-2 |
| `MBranch` | B | `0x14000000` | `\| (imm26 & 0x03FFFFFF)`, imm26=(tgt−here)/4 | A4-3 |
| `MBranchCond` | B.cond | `0x54000000` | `\| ((imm19 & 0x7FFFF)<<5) \| cond` | A4-3 |
| `MCbz` | CBZ/CBNZ | `0xB4/0xB5000000` / `0x34/0x35000000` | `\| ((imm19&0x7FFFF)<<5) \| Rt` | A4-3 |
| `MCall` | BL (+ reloc) | `0x94000000` (placeholder) | + `Reloc{Call}` | A4-3 |
| `MCallIndirect` | BLR | `0xD63F0000` | `\| (Rn<<5)` | A4-3 |
| `MAdrp` | ADRP (+ reloc) | `0x90000000` (placeholder) | `\| Rd` + `Reloc{PageHi}` | A4-3 |
| `MAddLo` | ADD lo12 (+ reloc) | `0x91000000` (placeholder) | `\| (Rn<<5) \| Rd` + `Reloc{PageLo}` | A4-3 |
| `MRet` | (epilogue) + RET | `0xD65F03C0` | epilogue words prepended (A4-2) | A4-1/2 |
| `MFAlu`/`MFNeg`/`MFCmp`/`MFMov`/`MCvt` | FADD… / SCVTF… | — | **named honest-stop `A4-fp`** | 0.3.1 |

The FP family (`MFAlu`/`MFNeg`/`MFCmp`/`MFMov`/`MCvt`) is a **single named honest-stop** in A4-1:
`encode_inst_word` returns a `"A4-fp: float-op encoding deferred to 0.3.1"` error for those cases. A
float fixture then honest-stops IDENTICALLY on the own-native side and is compared at the stop, never
at a fabricated value (the A2/A3 §4 precedent). The encodings are regular and are listed in the
appendix so the fast-follow is mechanical; scoping the KEYSTONE to the integer/control/memory subset
is what keeps A4 shippable and the differential's first light small.

### 2.5 `encode_inst_word` / `encode_func` / `encode_module`

```teko
/**
 * EncWord — one instruction's encoding outcome: its base word plus an optional
 * pending fixup. A position-independent instruction resolves to just `word`
 * (`has_branch`/`has_reloc` false). A branch carries `has_branch=true` +
 * `target` (the machine-block id, patched once block offsets are known, §3.3).
 * A symbol reference carries `has_reloc=true` + `reloc_sym`/`reloc_kind`.
 *
 * @since #385 A4-1
 */
pub type EncWord = struct {
    /**
     * word — the base 32-bit encoding (operand fields filled, branch/reloc
     * fields left zero for later patching).
     */
    word: u32
    /**
     * has_branch — true iff `target` is an intra-function block displacement to
     * patch after layout (§3.3).
     */
    has_branch: bool
    /**
     * target — the destination machine-block id (meaningful when `has_branch`).
     */
    target: u32
    /**
     * branch_form — which displacement field to patch: 0=B(imm26),
     * 1=B.cond/CBZ(imm19). Sizes the sign-extended range check.
     */
    branch_form: u32
    /**
     * has_reloc — true iff this word owes a symbol `Reloc` (BL/ADRP/ADDLo).
     */
    has_reloc: bool
    /**
     * reloc_sym — the relocation's target symbol (meaningful when `has_reloc`).
     */
    reloc_sym: str
    /**
     * reloc_kind — the relocation kind (meaningful when `has_reloc`).
     */
    reloc_kind: MRelocKind
}

/**
 * encode_inst_word — encode one fully-physical `MInst` to its `EncWord` (§2.4).
 * A total per-case match over the 29-case variant (`minst.tks:787`), mirroring
 * `print_minst`'s dispatch. `MFrameAddr` is resolved through the supplied
 * `FrameLayout` (§3.2); `MRet` returns the bare RET word (the epilogue is
 * prepended by `encode_func`, §3.2). The FP family returns a NAMED honest-stop
 * (`A4-fp`, 0.3.1); a big immediate / big frame offset / unscaled load returns
 * its own named stop (`A4-bigimm`/`A4-bigframe`/`A4-ldur`).
 *
 * @param FrameLayout layout  the function's frame layout (for MFrameAddr)
 * @param MInst inst  the instruction to encode (all operands physical)
 * @return EncWord | error  the encoding, or a named honest-stop
 */
fn encode_inst_word(layout: FrameLayout, inst: MInst) -> EncWord | error { … }

/**
 * encode_func — lower one fully-colored `MFunc` to an `EncodedFunc`: compute
 * its frame layout (§3.2), emit the prologue, encode every block in
 * `f.blocks` order (RPO after #443) recording each block's word offset,
 * expand `MFrameAddr`/`MRet`, resolve intra-function branch displacements from
 * the recorded block offsets (§3.3), and collect symbol relocations. Returns a
 * named honest-stop if any instruction does.
 *
 * @param AbiDescriptor abi  the register file (for the callee-saved save-set)
 * @param MFunc f  the fully-physical function
 * @return EncodedFunc | error  the encoded function, or a named honest-stop
 */
pub fn encode_func(abi: AbiDescriptor, f: MFunc) -> EncodedFunc | error { … }

/**
 * encode_module — encode every `MFunc` of `m`, concatenate their `__text`
 * words into the module image, rebase each function's reloc offsets by its
 * `__text` base, intern the rodata bytes into `__const`, and build the symbol
 * table (one defined entry per function + rodata entry; one undefined entry
 * per externally-referenced symbol not defined in-module). The globals table is
 * carried but empty in the N2 subset (`A4-globals` honest-stop, §6).
 *
 * @param AbiDescriptor abi  the register file
 * @param MModule m  the fully-colored module
 * @return EncodedModule | error  the section images + symbols + relocs
 */
pub fn encode_module(abi: AbiDescriptor, m: MModule) -> EncodedModule | error { … }
```

---

## 3. Frame finalization — the A3 "A4-frame" deferral (A4-2)

A3 finalized the *slot table* (`MFunc.frame`) and recorded nothing else; the prologue/epilogue and the
`MFrameAddr` SP/FP-offset resolution are A4's (`backend-a3-regalloc.md` §6.1, §3.5). A4 does them as a
**pure side-table** the encoder consumes — the block stream is NOT rewritten, so the A3 interp oracle
still runs the pre-encode `MFunc` unchanged (the interp models slots directly via `frame_slot_addr`,
`minst_interp.tks:843`, and has no SP — mutating `MFrameAddr` into SP arithmetic would break it).

### 3.1 The callee-saved save-set is RE-DERIVED (not carried)

`MFunc` carries only `{symbol, blocks, frame}` (`minst.tks:823`) — **`ScanResult.used_callee_saved`
is internal to regalloc and dropped** by `regalloc_func`. A4 therefore re-derives the save-set by
scanning the fully-physical function: every physical register that appears as a def or use (via
`inst_regs`, which is `pub`, `regalloc.tks:206`) and satisfies `is_callee_saved(abi, r)`
(`abi_aapcs64.tks:214`), plus FP(x29)+LR(x30) **always** (uniform prologue → correct backtraces via
the `.tsym`; 16 bytes, an honest, negligible M.4 cost). This is resolved **law-first (M.1/M.5): the
emitted instructions ARE the source of truth**; recomputing from them avoids a redundant carried field
that could desync, and keeps A3's output type frozen (§7, D-A).

### 3.2 The layout + prologue/epilogue

```teko
/**
 * FrameLayout — the resolved stack frame of one function: its total 16-aligned
 * byte size, the ordered callee-saved registers to save/restore (re-derived by
 * scanning, §3.1), and the SP-relative byte offset of EVERY frame slot. Purely
 * a side-table the encoder reads; the block stream is never rewritten (so the
 * A3 interp oracle stays valid).
 *
 * @since #385 A4-2
 */
pub type FrameLayout = struct {
    /**
     * size — the total frame size in bytes, rounded up to 16 (AAPCS64 SP
     * 16-alignment); 0 for a frameless function.
     */
    size: u32
    /**
     * saved_gpr — the callee-saved GPR ids to STP/LDP (plus FP/LR handled
     * separately), in ascending order for a deterministic prologue (M.2).
     */
    saved_gpr: []u32
    /**
     * saved_fpr — the callee-saved FPR ids to save/restore, ascending.
     */
    saved_fpr: []u32
    /**
     * slot_offsets — the SP-relative byte offset of each `MFunc.frame` entry,
     * in frame order. Each slot honors its OWN `LAlloca` alignment (NOT a
     * uniform `slot * spill_slot_bytes` stride — the compiler emits
     * mixed-size/mixed-align allocas for structs/arrays/pointer-pairs,
     * `lower.tks:1110/1331/3144/3274`), so `MFrameAddr{slot}` resolves to `ADD
     * dst, sp, #slot_offsets[slot]`.
     */
    slot_offsets: []u32
}

/**
 * compute_frame_layout — build a function's `FrameLayout`: re-derive the
 * callee-saved save-set (§3.1), lay out the frame slots bottom-up honoring each
 * `LAlloca`'s alignment (`slot k` at `align_up(end of slot k-1, align_k)`), and
 * size the frame: 0 when FRAMELESS (no slots, no callee-saved, and no LR to
 * preserve — the `exit(n)` leaf), else slot region + callee-saved area (8 bytes
 * each) + 16 for FP/LR, rounded to 16. A pure side-table pass — it never
 * mutates the block stream, and returns a `FrameLayout` unconditionally; an
 * offset that outruns its encodable field is a NAMED honest-stop
 * (`A4-bigframe`, §6) raised at EMISSION time (`encode_frame_addr` /
 * `emit_prologue`), not here.
 *
 * @param AbiDescriptor abi  the register file (callee-saved partition + slot size)
 * @param MFunc f  the fully-physical function
 * @return FrameLayout  the resolved frame
 */
pub fn compute_frame_layout(abi: AbiDescriptor, f: MFunc) -> FrameLayout { … }

/**
 * slot_offset — the SP-relative byte offset of frame slot `slot` under
 * `layout` (a direct lookup into the alignment-honoring `slot_offsets` map),
 * the concrete address `MFrameAddr{slot}` resolves to (an ADD from SP, §2.4).
 *
 * @param FrameLayout layout  the resolved frame
 * @param u32 slot  the frame-slot index
 * @return u32  the SP-relative byte offset
 */
pub fn slot_offset(layout: FrameLayout, slot: u32) -> u32 { … }
```

**Frame decision.** A function is FRAMELESS (`size=0`, no prologue/epilogue) iff it has no frame
slots, uses no callee-saved registers, AND does not need LR preserved — where LR preservation is
needed only when the body BOTH makes a call (clobbering LR) AND has a reachable `MRet` (`needs_lr_save`
= `has_call && has_ret`). A leaf, or the `exit(n)`-terminated virtual-`main` (a call but no `MRet`), is
thus frameless; a call-and-return function with no slots/callee-saved still gets the minimal 16-byte
FP/LR frame (the doc's N=16 case).

**Prologue** (size `N`): `sub sp, sp, #N` ; `stp x29, x30, [sp, #N-16]` ; `add x29, sp, #N-16` (set FP)
; one `stp` per saved-callee-saved pair (`0xA9000000 | (imm7<<15) | (Rt2<<10) | (31<<5) | Rt`, imm7 =
off/8), the odd trailing register of a class via a single `str`. **Epilogue** (LIFO): the `ldp`s/`ldr`s
; `ldp x29, x30, [sp, #N-16]` ; `add sp, sp, #N`. STP/LDP are encoder-internal byte sequences, NOT
`MInst`s (there is no store-pair `MInst`). A4-2 emits these via `emit_prologue`/`emit_epilogue`; the
`RET` stays the `MRet` encoding (`encode_ret`), which the A4-3 function-encode layer emits immediately
after the epilogue words. `MFrameAddr{dst,slot}` → `ADD dst, sp, #slot_offset(layout,slot)`. The
single `A4-bigframe` guard is the FP/LR pair offset (`N-16`) fitting the signed 7-bit STP field: when
it does, every smaller pair/slot offset fits too.

> **First-light note.** The keystone fixture `exit(42)` produces a **frameless** `main` (no spills, no
> allocas, a non-returning `bl _tk_exit` — LR is clobbered but never restored), so its `FrameLayout` is
> `size=0` and the prologue/epilogue collapse to nothing. Thus A4-2 is fully validated by golden bytes
> (a spill/return fixture with a real frame) BEFORE the A4-5 differential needs it, and the very first
> end-to-end binary needs only A4-1 + A4-3(BL) + A4-4 + A4-5.

### 3.3 Branch layout + fixups (A4-3)

Arm64 is fixed-width — every instruction (incl. the multi-word prologue/epilogue/`MFrameAddr`
expansions) is a whole number of 4-byte words — so block byte-offsets are known exactly after one
encode pass; **no branch relaxation / iteration** is needed. `encode_func`:

1. **Emit pass** — prepend the prologue words; walk `f.blocks` in order, recording each block's
   starting word-index in a `block_word_offset[block_id]` map; encode each instruction via
   `encode_inst_word`, pushing `EncWord`s (branches carry `has_branch`+`target`; symbol refs push a
   `Reloc` at the current word-offset); expand `MRet` to epilogue+ret, `MFrameAddr` to the resolved ADD.
2. **Patch pass** — for each `EncWord` with `has_branch`, compute `disp = (block_word_offset[target] −
   here_word_index) * 4`, range-check it against the field width (imm26 = ±128 MB, imm19 = ±1 MB — our
   functions are tiny, so an overflow is an internal-invariant honest-stop, not expected), and OR the
   sign-masked displacement into the word.

Symbol relocations (BL/ADRP/ADDLo) need no patching — the placeholder word stays and the `Reloc`
(offset = word-index*4, rebased at module assembly) drives the linker. This makes `encode_func`'s
output deterministic and byte-testable with no external tool.

---

## 4. The Mach-O object writer (`src/backend/objfile_macho.tks`, A4-4)

A minimal `MH_OBJECT` (relocatable) Mach-O for arm64, enough for `ld`/`cc` to link. It is
**ISA-agnostic** (consumes only `EncodedModule`), so Phase B/E reuses its shape. Structure
(all little-endian, `emit_u32_le` + a `emit_u64_le`/`emit_bytes`/`emit_cstr` family):

- **`mach_header_64`** — magic `0xFEEDFACF`, cputype `CPU_TYPE_ARM64 = 0x0100000C`, cpusubtype
  `0x00000000`, filetype `MH_OBJECT = 1`, `ncmds`/`sizeofcmds`, flags `0`, reserved.
- **`LC_SEGMENT_64`** (one segment, empty name) with the sections the corpus needs:
  `__text` (segname `__TEXT`, flags `S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS`) and
  `__const` (segname `__TEXT`, for rodata). Each `section_64` carries `addr`/`size`/`offset`/`align`/
  `reloff`/`nreloc`/`flags`. `__data` (globals) is **deferred** (`A4-globals` honest-stop — the N2
  subset lowers no `LGlobal`, `lir.tks:151`).
- **`LC_BUILD_VERSION`** — platform `PLATFORM_MACOS = 1` + minos/sdk (modern `ld` wants it).
- **`LC_SYMTAB`** — the `nlist_64` array + string table: each defined symbol carries `N_SECT`, its
  section index, and its ABSOLUTE value (`section.addr + section-relative offset` — NOT the bare
  offset; a `__const` symbol's `section.addr` is `text_size`, so a bare offset understates it and
  `ld` hard-rejects the object). Functions (`_main`, each mangled function) additionally carry
  `N_EXT` (globally visible, so `crt0`/other objects can call them); rodata symbols do NOT (file-local,
  mirroring `clang`'s own convention for string constants — avoids a duplicate-symbol clash when two
  objects each define the same-named constant at Phase E link). Undefined symbols (`_tk_exit`, any
  not-yet-emitted callee) carry `N_UNDF | N_EXT`, value 0.
- **`LC_DYSYMTAB`** — the ilocalsym/iextdefsym/iundefsym ranges (`ld` requires it even for a static
  object); the writer orders the symbol table {locals, defined-ext, undefined} so the three ranges are
  contiguous.
- **Section reloc tables** — per section, a `relocation_info[]` built from `EncodedModule.relocs`:
  each is `{r_address (section offset), r_symbolnum (symbol table index), r_pcrel, r_length=2,
  r_extern=1, r_type}` with `r_type` ∈ {`ARM64_RELOC_BRANCH26`, `ARM64_RELOC_PAGE21`,
  `ARM64_RELOC_PAGEOFF12`} (§2.2). `r_pcrel=1` for BRANCH26/PAGE21, `0` for PAGEOFF12.

```teko
/**
 * emit_macho — assemble a Mach-O `MH_OBJECT` for arm64 from an `EncodedModule`:
 * the header + `LC_SEGMENT_64` (`__text`/`__const`) + `LC_BUILD_VERSION` +
 * `LC_SYMTAB` + `LC_DYSYMTAB`, the section images, the per-section relocation
 * tables, and the symbol+string tables. Enough for `cc`/`ld` to link into a
 * runnable executable against the C-built `teko_rt` (D-2 option 1 bootstrap).
 * The `__info_plist` section is added at LINK time (`-Wl,-sectcreate`,
 * `project.tks:434`), not embedded here — object-embedded plist is deferred to
 * the own-linker (E2). Data globals + GOT relocs are the `A4-globals` stop (§6).
 *
 * @param EncodedModule enc  the section images + symbols + relocations
 * @return []byte  the Mach-O object file bytes
 */
pub fn emit_macho(enc: EncodedModule) -> []byte { … }
```

Testability: golden byte assertions on the fixed header prefix (magic/cputype/filetype) and a tiny
one-function module's layout, plus a **host-tool well-formedness check** in the gate — `llvm-objdump
--macho -d <obj>` disassembles back to the expected mnemonics and `otool -hlv <obj>` / `ld -r` accepts
it (macOS-arm64 lane only, honest-skip elsewhere). This is the slice where an external tool
cross-checks the bytes; the encoder slices (A4-1..A4-3) need no tool at all.

---

## 5. Link + `emit_native` + the C-vs-own DIFFERENTIAL (A4-5, the keystone)

### 5.1 `emit_native` (the own-backend driver)

```teko
/**
 * emit_native — the own-AOT-backend path: lower the checked program to LIR,
 * select arm64 MInst, allocate registers, encode to arm64 bytes, wrap in a
 * Mach-O object, and link it against the C-built `teko_rt` via `cc`-as-linker
 * (D-2 option 1 bootstrap, `own-backend-architecture.md` §5.2). Any pass's
 * named honest-stop propagates out unchanged, so an unsupported fixture stops
 * IDENTICALLY on the own side and the differential compares at the stop, never
 * at a fabricated value (the A2/A3 §4 precedent). Selected behind the temporary
 * `TEKO_BACKEND=native` seam until D1/#390 lands the `--backend` flag (§7, D-B).
 *
 * @param dir the project directory, for diagnostics
 * @param stem the output artifact stem
 * @param out_dir the resolved output directory
 * @param prog the checked, typed program
 * @param m the resolved manifest
 * @return the process exit status (0 on success)
 */
fn emit_native(dir: str, stem: str, out_dir: str, prog: checker::TProgram, m: Manifest) -> i32 {
    let lmod = match lir::lower_program(prog) { lir::LModule as x => x; error as e => return fail(dir, e.message) }
    let sel  = match isel::select_module(lmod) { minst::MModule as x => x; error as e => return fail(dir, e.message) }
    let col  = match regalloc::regalloc_module(abi::aapcs64(), sel) { minst::MModule as x => x; error as e => return fail(dir, e.message) }
    let enc  = match encode::encode_module(abi::aapcs64(), col) { encode::EncodedModule as x => x; error as e => return fail(dir, e.message) }
    let obj  = objfile::emit_macho(enc)
    … write `<out>/<stem>.o` ; link_object(objpath, binpath, m, prog) ; write `<binp>.tsym` …
}
```

### 5.2 The link step (reuse `run_cc`'s machinery)

`run_cc` (`project.tks:397`) already assembles the exact link line A4 needs — it just passes a `.c`
where A4 passes a `.o`. Extract a `link_object(objfile, binary, m, prog) -> i32` that reuses `run_cc`'s
compiler resolution, the `teko_rt.c` + `assert.c` sources, `-lm`, the reachable `[extern.libs]` flags,
and the `__info_plist` `-Wl,-sectcreate` (so `__info_plist` parity is preserved for free in the
`cc`-linker era, `own-backend-architecture.md` §2.1). The only change is `cfile → objfile`; `cc` links
the object + compiles/links the C runtime, resolving `_tk_exit`, `_tk_set_args`, `_tk_region_*`. The
own object provides `_main`. (`tk_set_args` arg-forwarding for `teko::env::args()` is a named stop —
the keystone corpus does not read argv; see §6 `A4-args`.)

### 5.3 The differential gate

A new leg — `scripts/diff_c_own.sh` (or an own arm inside `diff_vm_native.sh`) — over an
**`exit(n)`-terminated** integer/control corpus (the subset the interp already runs, §1). Per fixture:

1. **C-native** (trusted): `teko <fixture> -o <cbin>` (default path) → run → capture exit + stdout.
2. **own-native**: `TEKO_BACKEND=native teko <fixture> -o <ownbin>` → run → capture exit + stdout.
3. **assert** `C-native == own-native` on exit code AND stdout; optionally a third `== interp` leg via
   a tiny `minst_interp`/`interp_lmodule` harness for a pre-machine cross-check.

**Gated on macOS-arm64** (the highest-RAM lane, the native-test-gate ruling; needs the host Mach-O
`ld`); on any other host the leg **honest-skips** with a named reason (`"A4 own-backend differential
needs host arm64 Mach-O ld; skipped on <platform>"`). Stays cheap: a handful of tiny fixtures, the C
side already built by the existing gate.

---

## 6. Honest-stops — which slice each lives behind (0.3.1 completion vs later clusters)

| stop | slice it lives behind | what it defers | closed by |
|---|---|---|---|
| `A4-fp` | A4-1 (`encode_inst_word`) | FP-op + FP-conversion encoding (`MFAlu`/`MFNeg`/`MFCmp`/`MFMov`/`MCvt`) | 0.3.1 (encodings in appendix) |
| `A4-bigimm` | A4-1 | ALU/CMP immediate outside `[0,4095]` (MOVZ+op sequence) | 0.3.1 |
| `A4-ldur` | A4-1 | unscaled / >12-bit load-store offset (LDUR/STUR) — spills use offset 0 | 0.3.1 |
| `A4-bigframe` | A4-2 | frame slot offset beyond the ADD-imm 12-bit field | 0.3.1 |
| `A4-globals` | A4-4 | `__data` section + GOT data relocs — N2 lowers no `LGlobal` | when lowering emits globals |
| `A4-args` | A4-5 | `tk_set_args` argv forwarding into `main` | 0.3.1 |
| i128 register-pair ops | — (inherited) | isel never emits them (`isel §6.3`) — rides A2 | A2's i128 route |
| FP-value spill | — (inherited) | `detect_fpr_spill` errors in A3 before A4 | A3's `A3-fpr-spill` |
| `loop` back-edge | — (inherited) | regalloc honest-stops (`A3-loop`) before A4 | A3's `A3-loop` |
| ELF/PE/COFF, x86/riscv/wasm | later clusters | other targets | Phases B (#386–#388), C (#389) |
| `--backend={c,native}` flag | later cluster | the real manifest flag (A4 uses the env seam) | D1/#390 |
| own linker (drop `cc`) | later cluster | Mach-O static linker (+ object `__info_plist`) | Phase E/#226 |

Each stop is a NAMED error the pipeline surfaces; a fixture reaching one stops identically on the own
side and is compared at the stop (never at a fabricated value — M.3, the A2/A3 §4 precedent).

---

## 7. Deferrals + recorded decisions (law-first — no HALT)

**D-A · Re-derive the callee-saved save-set (not widen `MFunc`).** `MFunc` does not carry
`used_callee_saved`; A4 re-derives it by scanning the physical function (§3.1). **Law-first (M.1/M.5):**
the emitted instructions ARE the source of truth — recomputing from them is one obvious way, avoids a
redundant carried field that could desync, and freezes A3's output type. (Alternative: widen `MFunc`
+ thread the set out of `regalloc_func` — more coupling, no benefit, since A4 already walks every
instruction.) Recorded, not open.

**D-B · Temporary `TEKO_BACKEND=native` env seam (not the `--backend` flag).** D1/#390 owns the
manifest `--backend={c,native}` flag; A4 needs the native path *reachable* to birth the differential
before D1 lands. **Law-first (M.4/M.1):** a tiny additive env read in `emit_binary`/`backend` (default
unchanged → FIXPOINT-preserving; §3.7 of the architecture doc's `emit_binary` shape) is a visible,
honest, removable test seam; D1 SUPERSEDES it with the real flag. Not a default change, not a
second way to ship — a scaffold. Recorded, not open.

**D-C · `cc`-as-linker + C-built `teko_rt` (D-2 option 1 bootstrap).** Already resolved in the
architecture recon (§5.2 D-2, §5 D-5): A4–B3 link the C-built `teko_rt.o` via `cc` so the backend is
validated fast; convergence to lowering `teko_rt.tks` through the own backend + dropping `cc` is
E1/E2. **Toolchain independence is only fully achieved at E1/E2, not at #385** — recorded so no one
claims independence prematurely (M.3). Not an A4 decision to reopen.

**D-D · `#443` RPO numbering is a hard PREREQUISITE.** A4-3's multi-block layout assumes the
RPO-ordered, back-edge-correct regalloc #443 landed; on a base lacking it, nested `if`/`match`
functions honest-stop in regalloc (the old `target ≤ block id` over-approximation, §6.2 of the A3
doc) and skip identically. A4 must branch off a base carrying #443. Sequencing note, not a tension.

**No genuine unresolved tension → no HALT.** Every deferral above is a scope boundary resolved
law-first with a named follow-up. The keystone bar (§5.3) is fully provable on the acyclic, non-i128,
integer, `exit(n)`-terminated subset A2/A3 already run.

---

## 8. The main-tail divergence — GROUNDED in A4-5 (integrator fold #21); end-to-end fixture PENDING

**Finding: there is NO SEMANTIC divergence — both backends already honor the #423 trailing-exit ruling
(proven by source-level analysis; an end-to-end own-native fixture is a pending fast-follow, see the
caveat below).** The A4
design flagged a suspected divergence: that the C backend's `main` "unconditionally `return 0`"
(`codegen.tks:8449`) while the LIR virtual-`main` returns the trailing loose EXPRESSION's value
(`lower_virtual_main`, `lower.tks:4053`). Grounding BOTH sides in A4-5 shows that premise was a MISREAD
of line 8449:

- **C backend.** `emit_program_main` (`codegen.tks:8412`) emits the LAST loose statement through
  `emit_block_tail` → `emit_exprstmt_tail`, whose `in_main` branch emits `return (int)(<tail value>);`
  (`codegen.tks:5499-5502`). The `return 0;` at line 8449 is a FALL-THROUGH DEFAULT — dead code when a
  value-carrying tail already returned. So the C backend returns the trailing expression's value as the
  exit code (verified empirically: `6 * 7` → C-native exit 42, VM exit 42).
- **LIR own backend.** `#423` (commit `87f81b6`, "virtual-main trailing exit") touched ONLY the LIR side
  (`lower.tks`/`lir.tks`) — it made `close_virtual_main` MIRROR `lower_fn_body`'s #421 trailing-expression
  auto-return, bringing LIR INTO PARITY with the already-existing C behavior. The C backend was not
  touched by #423 because it was already correct.

**Resolution (law-consistent, no deferral).** The ratified semantic (#421/#423) is: a trailing loose
expression IS the process exit code. Both backends implement it; they are ALIGNED. The differential
corpus is scoped to `exit(n)`-terminated fixtures not to sidestep a divergence (there is none) but
because that is (a) exactly the subset the LIR interp oracle validates and (b) the own-backend's
frameless-`main` first light (`bl _tk_exit`, no `MRet`). This exit(n)-corpus convention is codified in
`scripts/diff_c_own.sh`'s header.

**Caveat — the parity above is proven by source-level analysis + the A4-2 frame goldens, NOT yet by an
end-to-end own-native fixture.** A trailing-VALUE `main` is FRAMED (it falls through to `MRet` rather
than diverging through `bl _tk_exit`), so it rides the own path's `MRet` + prologue/epilogue machinery
(A4-2) — machinery the frame goldens exercise directly (`encode_arm64_test.tkt`'s spill/return fixture),
but which the A4-5 exit(n) corpus, by construction, never reaches (every corpus fixture diverges before
any `MRet`). No fixture in `examples/regressions/own_*` yet exercises a framed `main` through
`diff_c_own.sh` end to end. Today the corpus cannot add one: the only two ways to make a non-trivial
FRAMED `main` (a spill, or a `match`/loop needing a real return path) either need `A4-args` (a real
argv-carrying entry) or hit the same inherited `A3-loop`/`A4-bigframe` stops `own_match_exit` already
documents. **This is a scope note, not a re-opened tension** — the fast-follow is mechanical (add a
framed-`main` fixture once A3-loop or an equivalent unblocks one), not a new design question.

---

## 9. Regression fixtures + the gate

### 9.1 Golden byte-vector unit tests (`src/backend/encode_arm64_test.tkt`) — engine-agnostic, no machine

- **Per-`MInst` encodings** (A4-1): `encode_inst_word(movz.x %p0,#42) → 0xD2800540` (bytes `40 05 80
  D2`); `add.x %p0,%p1,%p2 → 0x8B020020`; `mov.w %p0,%p8 → 0x2A0803E0`; `sub.x %p0,%p1,#7`;
  `cmp.x %p1,%p2`; `cset.x %p0, eq`; `msub.x`; `ret → 0xD65F03C0`; each FP case → the `A4-fp` stop.
- **Frame** (A4-2): a spill fixture (via a tiny 2-GPR test descriptor forcing a spill) → the prologue
  `sub sp,sp,#N; stp x29,x30,...`, the `MFrameAddr → add %pd, sp, #off`, the epilogue+`ret`;
  `compute_frame_layout` offsets asserted directly; a frameless `main` → `size=0`, empty prologue.
- **Branch/reloc** (A4-3): a two-block `if` → the `B.cond` displacement patched to `(tgt−here)`; an
  `MAdrp`+`MAddLo` → placeholder words + two `Reloc{PageHi/PageLo}`; an `MCall tk_exit` → placeholder
  `0x94000000` + one `Reloc{Call,"tk_exit"}`.
- **Mach-O** (A4-4, `objfile_macho_test.tkt`): header magic/cputype/filetype bytes; a one-function
  module's symtab has `_main` defined + `_tk_exit` undefined; the reloc table has one `BRANCH26`.

### 9.2 End-to-end differential fixtures (`examples/regressions/`, driven by `diff_c_own.sh`)

| fixture | program (shape) | expected exit | VM | C-native | own-native |
|---|---|---|---|---|---|
| `own_exit_zero` | `exit(0)` | 0 | ✓ (existing) | ✓ (existing) | **new** |
| `own_exit_code` | `exit(42)` | 42 | ✓ | ✓ | **new** |
| `own_arith_exit` | `exit(6 * 7)` | 42 | ✓ | ✓ | **new** |
| `own_sub_exit` | `exit(100 - 58)` | 42 | ✓ | ✓ | **new** |
| `own_if_exit` | `if 5 > 3 { exit(1) } else { exit(2) }` | 1 | ✓ | ✓ | **new** |
| `own_match_exit` | `match k { 0 => exit(7); _ => exit(9) }` | (per k) | ✓ | ✓ | **new** |

All exit-code (no stdout) at first; a `print`-then-`exit` fixture is added once `MCall` to the string
runtime is exercised (rides the same BL/reloc path). The VM + C-native legs are already covered by
`diff_vm_native.sh`; A4-5 adds the own-native leg and asserts `own == C (== interp)`.

### 9.3 Ritual + coverage posture

- **Every A4-N** owes the full ritual: **both-engine gate** (native `teko . -o bin` AND `teko test .`
  VM), **paranoid**, **fixpoint**, and **100% coverage on its new code** (definition-of-done). The
  encoder is highly branchy (per-`MInst`, per-width, per-cond) — cover every encoded case + every
  honest-stop arm via golden tests; a genuinely unreachable arm is justified in the PR.
- **VM-gotcha watch** (the encoder is dense byte-work): (a) build buffers via `teko::list::push(buf,
  (w & 0xFF) to byte)` — never widen a `byte` mid-expression (byte-width anchoring); (b) do all
  bit-slicing in `u32`/`u64`, narrow to `byte` only at the last step; (c) no `x = match {…return}` —
  use `let x = match {…}` then act (the isel/regalloc VM gotcha); (d) `u32` shifts for imm26/imm19
  displacement math (watch sign masking on negative displacements).
- **Fixpoint is trivially preserved** through A4-1..A4-4 (new files, no reachable call from the default
  path) and through A4-5 (the native path is behind the `TEKO_BACKEND=native` seam; default stays C).

### 9.4 Ritual points (where the FULL gate must pass)

- After **A4-2** (frame) and **A4-3** (layout/reloc): full both-engine gate + golden goldens green
  (these change the encoder's output shape and are the riskiest bit-work).
- The **KEYSTONE full ritual at A4-5**: the whole gate — both engines + fixpoint + the **new C-vs-own
  leg green on macOS-arm64** — must pass. **#385 CLOSES here.**

---

## 10. The sub-slice decomposition (ordered, each gate-able)

```
A3 (done) + #443 (done) ─▶ A4-1 ─▶ A4-2 ─▶ A4-3 ─▶ A4-4 ─▶ A4-5   [#385 closes]
                            encoder   frame    branch/   Mach-O    link + emit_native
                            core     finalize  reloc     object    + C-vs-own differential
```

- **A4-1 · encoder core** — `src/backend/encode_arm64.tks`: `emit_u32_le`, `enc_reg`/`sf_bit`,
  `EncWord`, `encode_inst_word` for the position-independent integer/control/memory cases (§2.4); the
  `A4-fp`/`A4-bigimm`/`A4-ldur` honest-stops. **Proven by:** per-`MInst` golden byte vectors. No e2e.
- **A4-2 · frame finalization** — `FrameLayout`, `compute_frame_layout` (re-derived save-set, §3.1),
  `slot_offset`, prologue/epilogue emission + `MFrameAddr`/`MRet` expansion inside `encode_func`; the
  `A4-bigframe` stop. **Proven by:** prologue/epilogue + `MFrameAddr` golden bytes + offset-table
  asserts (tiny-descriptor spill fixture). No e2e.
- **A4-3 · branch layout + fixups + relocs** — `Reloc`, block-offset recording, branch-displacement
  patching (§3.3), `EncodedFunc`, symbol `Reloc` collection. **Proven by:** two-block `if`
  displacement + ADRP/ADDLo/BL reloc-record golden asserts. No object.
- **A4-4 · Mach-O object writer** — `Symbol`, `EncodedModule`, `encode_module` (concat + rebase +
  symbol table) added to `src/backend/encode_arm64.tks`; `src/backend/objfile_macho.tks`: `emit_macho`
  (header + segment/sections + build-version + symtab + dysymtab + reloc tables); the `A4-globals`
  stop. **Proven by:** header/symtab/reloc golden bytes + `otool`/`nm` well-formedness on macOS-arm64.
- **A4-5 · link + differential (KEYSTONE)** — `emit_native` in `src/build/project.tks`, `link_object`
  (extracted from `run_cc`), the `TEKO_BACKEND=native` seam, `scripts/diff_c_own.sh`, the §9.2
  fixtures; the `A4-args` stop. **Proven by:** `own-native == C-native (== interp)` exit codes over the
  corpus on macOS-arm64 (honest-skip elsewhere). **#385 CLOSES.**

**Files:** new `src/backend/encode_arm64.tks`, `src/backend/objfile_macho.tks`,
`src/backend/encode_arm64_test.tkt`, `src/backend/objfile_macho_test.tkt`, `scripts/diff_c_own.sh`,
`examples/regressions/own_*`; touched `src/build/project.tks` (`emit_native` + the env seam + extract
`link_object` from `run_cc`). **Reuses unchanged:** `lir::lower_program`, `isel::select_module`,
`regalloc::regalloc_module`, `abi::aapcs64`/`is_callee_saved`, `regalloc::inst_regs` (pub), the whole
`minst` type surface.

---

## Appendix · FP encodings (for the `A4-fp` fast-follow)

Regular double(`dbl`)/single forms; ftype bit = `1<<22` for double, `0` for single. Base words
(double): FADD `0x1E602800`, FSUB `0x1E603800`, FMUL `0x1E600800`, FDIV `0x1E601800`
(`| (Rm<<16) | (Rn<<5) | Rd`); FNEG `0x1E614000`, FMOV(reg) `0x1E604000` (`| (Rn<<5) | Rd`); FCMP
`0x1E602000` (`| (Rn<<5) | (Rm<<16)`, Rd=0); SCVTF `0x9E620000`, UCVTF `0x9E630000`, FCVTZS
`0x9E780000`, FCVTZU `0x9E790000`, FCVT(d→s/s→d) `0x1E624000`/`0x1E22C000`. Single forms clear bit 22
(and the `sf` for int-side of conversions varies by width). The implementer re-derives each against
`llvm-mc --show-encoding` before shipping the fast-follow.
