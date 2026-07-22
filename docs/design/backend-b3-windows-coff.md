# B3 / N5 — Windows x86_64 (Win64 ABI + PE/COFF) (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave. Issue **#388**. Mini-umbrella
`fix/issue-388-windows-coff` (**#402**; base = the umbrella `remodel/backend-build` carrying A1→A4 +
#443 + B1 + B2, seed `teko 0.3.0.10-beta`). Base for the design: `docs/design/backend-b1-x8664.md`
(the x86-64 ISA pipeline this REUSES WHOLE) + `docs/design/backend-b2-riscv64.md` (the
reuse-vs-parallel writer discipline this INVERTS) + `docs/design/own-backend-architecture.md` §2.1
(target #4, x86-64 Win64 PE/COFF) + §3.4/§3.5, grounded in the merged code
(`src/backend/{minst_x86,isel_x86_64,regalloc_x86,encode_x86_64,abi_sysv64,abi_aapcs64,objfile_elf,
objfile_macho}.tks`, `src/build/project.tks`, `scripts/{diff_c_own,check_elf,check_macho}.sh`,
`.github/workflows/{native,sanitizers}.yml`).

> This is a PLAN. It brings the **FIRST re-targeting of an ISA to a second OS/object-format**: Windows
> x86_64 (Win64 ABI + PE/COFF), REUSING the ENTIRE x86-64 ISA pipeline from B1 (isel/regalloc/encoder
> are byte-for-byte the SAME instruction set — Windows x64 is Linux x86-64). The deltas are exactly
> TWO: (1) the **Win64 ABI** (`abi_win64.tks` — different arg regs RCX/RDX/R8/R9, 32-byte shadow space,
> RDI/RSI callee-saved, callee-saved XMM6-15), and (2) the **PE/COFF object writer** (`objfile_coff.tks`
> — a genuinely new, third object format). It does NOT retire the C backend, does NOT add the
> `--backend`/`--target` flags (D1/#390 owns them — B3 extends the temporary env seam with
> `TEKO_TARGET=x86_64-windows`), and does NOT build the own linker (Phase E2/#226 — B3 links via
> `clang`/`lld-link`-as-linker on the Windows runner). The hard tail (SSE float, data globals, argv
> forwarding, the callee-saved-XMM save path) is scoped forward as **named** honest-stops for 0.3.1,
> mirroring A4/B1/B2's discipline exactly.

---

## 0. TL;DR — the recommended shape

- **THE HEADLINE: B3 reuses the WHOLE x86-64 ISA pipeline UNCHANGED.** Windows x64 and Linux x86-64 are
  the SAME instruction set, so `isel_x86_64.tks`, `regalloc_x86.tks`, and the entire *instruction*
  encoder in `encode_x86_64.tks` (`encode_inst_x86`, REX/SIB/ModRM, rel32 branches, `RelocX86`,
  `EncodedModuleX86`) are REUSED VERBATIM — **zero changes**. The argument-register difference
  (RCX/RDX/R8/R9 vs RDI/RSI/…) and the result register (RAX, same as SysV) fall out FREE from
  `arg_reg(abi, …)` reading the new descriptor. The two deltas are surgically small: a new ABI
  descriptor and a new object writer. (§1.)
- **Decision-1 (the flagged tension) — COFF is a PARALLEL writer (`objfile_coff.tks`), NOT a
  generalization of the neutral ELF core; resolved law-first, NO HALT.** B2 folded x86/riscv ELF into
  `emit_elf_object` BECAUSE ELF↔ELF share the container byte-for-byte modulo three scalars
  (`e_machine`/`e_flags`/reloc-type). **COFF shares almost nothing structurally with ELF** — an 18-byte
  `IMAGE_SYMBOL` with the 8-byte inline-name-or-strtab-offset union (ELF has a 4-byte `st_name`
  offset), **no explicit reloc addend** (the addend is in-place in `.text`, like Mach-O — ELF has an
  explicit `r_addend`), **per-section relocation pointers in the section header** (ELF uses a separate
  `.rela.text` section), and a **4-byte-size-prefixed string table** (ELF's is a bare NUL-run). Folding
  COFF into `emit_elf_object` would blast the neutral core (which B1 AND B2 both ride) with format
  branches — the opposite of smallest-blast (M.1) and one-way (M.5). The **precedent is `emit_macho`**:
  a parallel writer, never folded into ELF. COFF is the THIRD parallel writer, and — being structurally
  closest to Mach-O (18B symbols, in-place addend, per-section relocs) — it mirrors `objfile_macho.tks`'s
  shape while REUSING only the genuinely-neutral byte helpers + the `Symbol` type. (§2.1, §5.)
- **Decision-2 (the flagged tension) — the Win64 frame delta lives in `compute_frame_layout_x86`, read
  from a new `AbiDescriptor.shadow_space` field; the instruction encoder is NEVER forked; resolved
  law-first, NO HALT.** Two Win64 frame facts differ from SysV: (a) the **callee-saved SET** (RDI/RSI
  callee-saved, XMM6-15 callee-saved) — captured ENTIRELY by the `win64()` descriptor partition and
  consumed FREE by the existing `collect_callee_saved_x86`/`is_callee_saved`; and (b) the **32-byte
  shadow space** a caller must reserve for any callee — a frame-reserve convention NOT expressible in
  the register-file lists. The smallest-blast home for (b) is an additive `shadow_space: u32` field on
  `AbiDescriptor` (0 for AAPCS64/SysV/LP64D — byte-identical output, FIXPOINT-guarded; 32 for Win64),
  read by `compute_frame_layout_x86` (which already receives `abi`) with a `func_makes_call_x86` scan —
  **zero function-signature changes anywhere, and not one instruction-encoding function touched**. The
  callee-saved-XMM *save path* (`movaps` spill of XMM6-15) is unimplemented and NAMED as the
  `B3-xmm-callee-saved` honest-stop (the integer corpus allocates no XMM, so `saved_fpr` stays empty).
  (§2.2, §4.)
- **Sub-slice order (each independently gate-able, smallest-safe-step) — FOUR slices (tighter than
  B1/B2's eight, because the ISA pipeline is 100% reused):**
  `B3-1` `abi_win64` + regalloc-core-neutrality proof (the RDI/RSI-callee-saved flip, the 4-arg window,
  the R10/R11 scratch) → `B3-2` the Win64 frame delta (`shadow_space` field + `func_makes_call_x86` +
  the `compute_frame_layout_x86` shadow reservation + the `B3-xmm-callee-saved` stop; B1's x86 frame
  goldens stay byte-identical since SysV `shadow_space=0`) → `B3-3` `objfile_coff` writer (the big new
  piece: `IMAGE_FILE_HEADER` + section headers + the 18-byte symbol table + string table + relocations;
  assembler-verified via `clang -target x86_64-windows-msvc -c` + `llvm-readobj --sections/--symbols/--relocations`) → `B3-4`
  target dispatch (`NativeTarget::X8664Windows`, `TEKO_TARGET=x86_64-windows`) + `emit_native_win` +
  the Windows C-vs-own differential lane + `check_coff.sh` (**#388 CLOSES here**).
- **Differential placement — WINDOWS RUNS PE NATIVELY (no qemu), macOS byte-tests.** The executing
  `C-native == own-native` differential runs on the **windows-x86_64** runner (which executes PE
  directly — the key advantage over B2's qemu-riscv case), wired onto the existing `windows-selfhost`
  lane (`sanitizers.yml`, already self-builds gen-1 on Windows and is already BLOCKING in the sanitizer
  gate). Locally on macOS-arm64: COFF goldens (byte-vectors, assembler-cross-checked with
  `clang -target x86_64-windows-msvc -c` → a real COFF `.obj`) run machine-free; `llvm-readobj --sections/--symbols/--relocations`
  round-trips the emitted object cross-format; execution honest-skips (macOS cannot run PE). Cross-link
  proof via `lld-link` / `clang -target x86_64-windows`.
- **No genuine HALT.** Both flagged tensions resolve law-first with clear winners (§8 D-B/D-C). The
  residual DRY tension (a parallel writer duplicates the section-layout WALK) is the SAME per-format
  mirroring `emit_macho`/`emit_elf_object` already embody and is REPORTED up (§8 R-1), not a blocker.

---

## 1. The assessed starting point — what B3 REUSES vs what it ADDS

B3 is the first proof the own backend is *OS/format*-independent, not just *ISA*-independent (B1/B2
proved the latter). The grounding audit:

### 1.1 REUSED VERBATIM — the WHOLE x86-64 ISA pipeline (no copy, no change; same `teko::backend` ns)

| Symbol(s) | Home | Why unchanged for Win64 |
|---|---|---|
| the entire `MInstX86` variant + `MAluOpX86`/`RelocKindX86` + `MFuncX86`/`MBlockX86`/`MModuleX86` | `minst_x86.tks` | same ISA — Windows x64 IS x86-64 |
| `select_module_x86` (the whole isel: two-address copy, fixed-reg div, `lea rip`, arg/result lowering) | `isel_x86_64.tks` | arg regs come from `arg_reg(abi,…)`; result pin RAX (Win64 == SysV); **zero isel change** |
| `inst_regs_x86`/`map_minst_x86`/`rewrite_inst_x86`/`regalloc_func_x86`/`regalloc_module_x86` | `regalloc_x86.tks` | thread `abi` — feeding `win64()` instead of `sysv64()` is the ONLY difference |
| the whole instruction encoder — `encode_inst_x86`, REX/SIB/ModRM, `push_byte_x86`/`push_imm32_x86`, rel32 branch layout + patch, `RelocX86`, `EncInstX86`, `EncodedFuncX86`, `EncodedModuleX86` | `encode_x86_64.tks` | identical machine code; **not one encoding byte differs** |
| `AbiDescriptor` + `arg_reg`/`allocatable_pool`/`is_caller_saved`/`is_callee_saved`/`spill_scratch` | `abi_aapcs64.tks:14,170,187,199,214,231` | pure register-file descriptor; B3 clones the PARTITION + adds `shadow_space` (§2.2) |
| `linear_scan`/`candidate_pool`/`assign_lookup` + `LiveInterval`/`IntervalSet`/`ScanResult`/`SubstEntry` | `regalloc.tks` | register-abstract; the HARD algorithm, single-sourced |
| `Symbol { name; defined; local; sect; offset }` | `encode_arm64.tks:1787` | the neutral object-symbol record; COFF binds it its own way (§5.3) |
| `emit_u32_le` (pub), `emit_u64_le`/`emit_str_bytes`/`emit_u8_field`/`pad_to_mult`/`align_up` | `encode_arm64.tks`/`objfile_macho.tks` (same-ns) | byte-table primitives — COFF reuses them like ELF/Mach-O do |
| `push_range` | `abi_aapcs64.tks:106` (same-ns) | contiguous register-id run assembly for `win64()` |
| `finish_native_object` + `build_cc_argv`/`resolve_cc`/`link_object` | `project.tks:879,473,440,559` | the object-format-agnostic write-`.o`+cc-link+`.tsym`+marker tail — REUSED (§6.1) |

> **Namespace fact (load-bearing, carried from B1/B2).** `pub` = cross-NAMESPACE export; a bare `fn` =
> namespace-private (visible to every file in `teko::backend`). B3's new files live in the SAME
> `teko::backend` namespace, so they reuse all of the above with **no `use` and no re-export** — and,
> per the silent-bare-name-collision gotcha (no diagnostic), they MUST NOT re-declare any of these
> names. Every parallel COFF/Win64 type/fn carries a `Coff`/`_coff`/`_win64` suffix (§1.3).

### 1.2 what B3 ADDS (the two deltas — nothing forked, only new files + one guarded field)

| new symbol(s) | home | why new |
|---|---|---|
| `win64()` + `win64_gpr_arg_seq`/`win64_gpr_allocatable`/`win64_gpr_caller_saved`/… | `abi_win64.tks` (new) | the Win64 register-file partition (§3) |
| `shadow_space: u32` field on `AbiDescriptor` (+ 3 constructors set 0) | `abi_aapcs64.tks` (1 field) | the 32-byte shadow-space reserve (§2.2) — additive, fixpoint-guarded |
| `func_makes_call_x86` + the shadow-space reservation in `compute_frame_layout_x86` | `encode_x86_64.tks` (frame only) | the Win64 caller-reserves-32 rule (§4) — the encoder ISA untouched |
| `emit_coff` + `CoffHeader`/`CoffSection`/`CoffSymbol`/`CoffReloc`/`CoffLayout` + `coff_reloc_type`/`build_coff_strtab` | `objfile_coff.tks` (new) | the PE/COFF container (§5) |
| `NativeTarget::X8664Windows` + `emit_native_win` | `project.tks` (additive arm) | target dispatch (§6) |
| `check_coff.sh` + the `diff_c_own.sh` Windows lane | `scripts/` | the Windows differential (§6.2) |

### 1.3 The naming convention (silent-collision-safe)

Every parallel COFF declaration lives in `teko::backend` with an explicit suffix so it CANNOT collide
with the frozen ELF/Mach-O names. The set: `CoffHeader`/`CoffSection`/`CoffSymbol`/`CoffReloc`/
`CoffLayout`/`CoffStrtab` ; `RelocKindX86` is REUSED (the same `Plt32`/`Pc32`/`Abs64` the x86 encoder
already tags — COFF re-maps them numerically, §5.4) ; `coff_reloc_type`/`build_coff_strtab`/
`coff_symbol_index`/`emit_coff_header`/`emit_coff_section`/`emit_coff_symtab`/`emit_coff_relocs`/
`compute_coff_layout` ; `emit_coff` (pub) ; `win64` (pub) + the `win64_*` pool helpers. The neutral
reuse (`Symbol`, `emit_u32_le`, `emit_u64_le`, `emit_str_bytes`, `align_up`, `pad_to_mult`, the whole
`encode_x86_64`/`regalloc_x86`/`isel_x86_64` surface) stays bare/`pub`.

---

## 2. The two decisions (law-first — NO HALT)

The task flags two potential HALTs. Both have clear winners; recorded (§8), not opened.

### 2.1 COFF writer — PARALLEL, not a generalization (D-B)

**Chosen: a parallel `objfile_coff.tks` writer, mirroring `emit_macho`'s shape.**

- **COFF diverges from ELF structurally, not scalarly.** The B2 ELF generalization was clean because
  x86-ELF and riscv-ELF are the SAME container modulo `e_machine`/`e_flags`/reloc-type. COFF differs in
  EVERY record: `IMAGE_SYMBOL` is 18 bytes with an 8-byte inline-or-offset name UNION (ELF: 24 bytes,
  4-byte `st_name` offset); `IMAGE_RELOCATION` is 10 bytes with **no addend field** (the addend rides
  in-place in `.text`, like Mach-O — ELF `.rela` carries an explicit 8-byte `r_addend`); relocations
  are pointed at from the **section header** (ELF uses a distinct `.rela.text` SECTION with its own
  header); the string table is **4-byte-size-prefixed** (ELF's is a bare NUL-run). Threading COFF
  through `emit_elf_object(ElfObject)` would require format branches at the header, section, symbol,
  reloc AND strtab level — destroying the neutrality that B1 AND B2 both depend on and risking two
  shipped differentials. **Blast radius of the parallel writer: new files only.**
- **The precedent is decisive.** `objfile_macho.tks` is ITS OWN parallel writer (`emit_macho`), never
  folded into ELF — because Mach-O also structurally diverges. COFF is the third parallel writer, and
  it is structurally CLOSEST to Mach-O (18B symbols, in-place addend, per-section reloc pointers), so
  it mirrors `objfile_macho.tks`'s `compute_*_layout`-then-ordered-emit shape.
- **M.5 preserved.** The parallel writer is the ONE way to write COFF, exactly as `emit_macho` is the
  one way to write Mach-O and `emit_elf_object` the one way to write ELF. The genuinely-shared assets —
  `emit_u32_le`/`emit_u64_le`/`emit_str_bytes`/`align_up`/`pad_to_mult` and the neutral `Symbol` — stay
  single-sourced and are reused.

Rejected: **extending `emit_elf_object` to a tri-format core** (blasts the neutral ELF writer B1/B2
ride, format branches everywhere, violates M.1/M.5) and **generalizing a Mach-O↔COFF core** (every
record's byte layout still differs — `IMAGE_SYMBOL` 18B vs `nlist_64` 16B, `IMAGE_RELOCATION` 10B vs
`relocation_info` 8B, `IMAGE_SECTION_HEADER` 40B vs `section_64` 80B — so it would be as branch-heavy
as folding into ELF). The residual DRY (a duplicated layout WALK) is REPORTED (§8 R-1).

### 2.2 The Win64 frame delta — a `shadow_space` descriptor field, the encoder never forked (D-C)

**Chosen: an additive `shadow_space: u32` on `AbiDescriptor`, read by `compute_frame_layout_x86`.**

Two Win64 frame facts differ from SysV:

1. **The callee-saved SET** — RDI/RSI are callee-saved on Win64 (caller-saved on SysV!); XMM6-15 are
   callee-saved. This is captured ENTIRELY by the `win64()` descriptor partition (§3) and consumed
   FREE by `collect_callee_saved_x86` (which scans + calls `is_callee_saved(abi, …)`,
   `encode_x86_64.tks:950`). The GPR save/restore path (`emit_push_reg_x86`/`emit_pop_reg_x86`) is
   register-generic, so RDI/RSI get pushed/popped with no code change. **Free from the descriptor.**
2. **The 32-byte shadow space** — a Win64 CALLER must reserve 32 bytes of stack for every callee to
   spill its 4 register args. This is a frame-reserve CONVENTION, not a register-file fact, so it has no
   home in the arg/allocatable/saved lists. The smallest-blast home is an additive `shadow_space: u32`
   field on `AbiDescriptor`: `compute_frame_layout_x86` already receives `abi`, so it reads
   `abi.shadow_space` with **zero signature changes**. AAPCS64/SysV/LP64D set it to 0 →
   byte-identical frame output → FIXPOINT holds (B1's x86 frame goldens are the guardrail). Win64 sets
   32; when the function makes a call (a `func_makes_call_x86` scan), the 32 bytes are reserved at the
   BOTTOM of the frame (the outgoing-arg region) and folded into the 16-byte alignment (32 is
   16-aligned, so it never perturbs the residue).

**The encoder ISA is NEVER forked.** The ONLY touch to `encode_x86_64.tks` is inside
`compute_frame_layout_x86` (a frame SIDE-TABLE function, not an instruction encoder) + a new
`func_makes_call_x86` scan. `encode_inst_x86` and every REX/SIB/ModRM path are untouched. That is the
precise answer to "where the Win64 frame delta lives given the shared x86 encoder": in the frame
side-table, keyed off a guarded descriptor field, leaving instruction selection/encoding identical.

**The callee-saved-XMM *save path* is a NAMED honest-stop.** `emit_saved_pushes_x86`
(`encode_x86_64.tks:1124`) pushes only GPRs (SysV had no callee-saved FPRs). Under Win64, an allocated
XMM6-15 live across a call would land in `saved_fpr` and need a `movaps [rsp+off], xmm` save/restore —
UNIMPLEMENTED. The integer corpus allocates NO XMM (FP is the `B3-fp` stop), so `saved_fpr` is empty
in practice; a non-empty `saved_fpr` raises the `B3-xmm-callee-saved` honest-stop (§7) rather than
mis-emitting. It rides 0.3.1 with `B3-fp`.

Rejected: **a `frame_conv` param threaded through `compute_frame_layout_x86`/`encode_func_x86`/
`encode_module_x86`** (changes THREE frozen B1 signatures + `emit_native_x86`'s call — bigger blast)
and **a `compute_frame_layout_win64` wrapper** (still needs `encode_module_x86` to dispatch on `abi`,
so it does not actually avoid touching frozen code, and it duplicates the framed-detection logic). The
descriptor-field approach has ZERO signature changes and is fixpoint-guarded. Recorded (§8 D-C).

---

## 3. `abi_win64.tks` — the Win64 register file (B3-1)

The whole cost of the ABI is cloning `AbiDescriptor`'s partition (`abi_sysv64.tks` is the SHAPE
template) with the Win64 differences. **Win64 x86-64:** integer args `RCX,RDX,R8,R9` (only 4 — SysV
has 6); integer result `RAX` (same as SysV — D-A carries: result RAX ≠ arg0 RCX); FP args `XMM0..XMM3`
(only 4 — SysV has 8), **positionally slot-shared** with the int regs (the Nth argument takes the Nth
int reg OR the Nth XMM, never both — an FP-only concern, `B3-argslot` §7); FP result `XMM0`;
callee-saved GPR `RBX,RBP,RDI,RSI,R12..R15` (**RDI/RSI callee-saved — the Win64 flip**); callee-saved
XMM `XMM6..XMM15`; everything else caller-saved. Register-id convention: the SAME x86
instruction-encoding order B1 fixed (`RAX=0,RCX=1,RDX=2,RBX=3,RSP=4,RBP=5,RSI=6,RDI=7,R8..R15=8..15`;
`XMM0=0..XMM15=15`), so the reused encoder's ModRM/REG field is the id directly.

```teko
/**
 * win64 — the concrete Microsoft x64 (Windows x86-64 PE/COFF) register-file
 * descriptor: RCX/RDX/R8/R9 integer arguments (only FOUR — the integer result is
 * RAX, pinned separately at the return site, D-A, exactly as SysV), XMM0..XMM3
 * float arguments (only four, positionally slot-shared with the int regs — an
 * FP-only concern deferred to B3-argslot), RBX/RBP/RDI/RSI/R12..R15 callee-saved
 * (RDI/RSI are callee-saved on Windows but CALLER-saved on SysV — the headline
 * partition flip), XMM6..XMM15 callee-saved, everything else caller-saved. The
 * allocatable pools list caller-saved first (scratch-cheap short values before a
 * callee-save prologue push), excluding RSP/RBP (the frame registers) and the
 * reserved spill scratch R10/R11 (GPR) / XMM4/XMM5 (FPR — caller-saved, unlike
 * SysV's callee-saved XMM14/XMM15, which Win64 must NOT use as scratch). Register
 * ids follow the x86 instruction-encoding order (identical to sysv64), so the
 * REUSED encoder's ModRM/REG field is the id directly. `shadow_space` is 32 (the
 * Win64 caller-reserved 32-byte spill area — §4); every other ABI leaves it 0.
 * Clones `sysv64`'s SHAPE with the Win64 partition + shadow space — the whole
 * cost of a new ABI on an already-supported ISA (Phase B, §2.2 of the arch doc).
 *
 * @return AbiDescriptor  the Win64 register file
 */
pub fn win64() -> AbiDescriptor {
    AbiDescriptor {
        gpr_arg = win64_gpr_arg_seq()
        fpr_arg = push_range(teko::list::empty(), 0, 3)
        gpr_allocatable = win64_gpr_allocatable()
        fpr_allocatable = win64_fpr_allocatable()
        gpr_caller_saved = win64_gpr_caller_saved()
        fpr_caller_saved = push_range(teko::list::empty(), 0, 5)
        gpr_spill_scratch = push_range(teko::list::empty(), 10, 11)
        fpr_spill_scratch = push_range(teko::list::empty(), 4, 5)
        spill_slot_bytes = 8
        spill_slot_align = 8
        shadow_space = 32 to u32
    }
}
```

The pool helpers mirror `abi_sysv64.tks`'s `sysv_gpr_arg_seq`/`sysv_gpr_allocatable`/
`sysv_gpr_caller_saved` shape exactly (each fully Javadoc'd):
- `win64_gpr_arg_seq()` → `[1(RCX),2(RDX),8(R8),9(R9)]` (four args; result RAX pinned by isel, D-A).
- `win64_gpr_allocatable()` → caller-saved first `RAX(0),RCX(1),RDX(2),R8(8),R9(9)` (every caller-saved
  GPR except the R10/R11 scratch) then callee-saved `RBX(3),RSI(6),RDI(7),R12..R15(12..15)` (every
  callee-saved GPR except the RBP(5)/RSP(4) frame registers). **RSI/RDI now sit in the callee-saved
  tail** — the visible Win64 flip vs `sysv_gpr_allocatable` (where they were caller-saved).
- `win64_gpr_caller_saved()` → `RAX(0),RCX(1),RDX(2),R8(8),R9(9),R10(10),R11(11)` — the full
  call-clobber set (including R10/R11 scratch). **RDI/RSI are ABSENT** (callee-saved on Win64), unlike
  `sysv_gpr_caller_saved` which includes them — so a value live across a call MAY occupy RDI/RSI.
- `win64_fpr_allocatable()` → caller-saved first `XMM0..XMM3(0..3)` then callee-saved `XMM6..XMM15(6..15)`
  (excluding the XMM4/XMM5 spill scratch).

### 3.1 The `shadow_space` field addition to `AbiDescriptor` (behavior-preserving)

`AbiDescriptor` (`abi_aapcs64.tks:14`) gains one field; the three existing constructors gain one line:

```teko
    /**
     * shadow_space — the caller-reserved stack area, in bytes, every callee owns
     * to spill its register arguments (Win64: 32; AAPCS64/SysV/LP64D: 0 — they
     * have no shadow space). `compute_frame_layout_x86` adds this to the frame of
     * a function that makes a call (`func_makes_call_x86`), reserving the
     * outgoing-argument region at the bottom of the frame; a 0 leaves every other
     * ABI's frame output byte-identical (FIXPOINT). The one frame-convention fact
     * the register-id lists cannot express (§2.2, D-C).
     */
    shadow_space: u32
```

> **Fixpoint guardrail.** `aapcs64()`/`sysv64()`/`riscv64_lp64d()` each add `shadow_space = 0 to u32`.
> Because `compute_frame_layout_x86` only reserves shadow space when `abi.shadow_space > 0`, the arm64,
> riscv64 and x86-64-Linux frame images are BYTE-IDENTICAL to today — B1's x86 frame goldens + the
> default-build fixpoint are the guardrail. The stable-seed lane parses the new field fine (a struct
> field + field access is core syntax in every seed — no staged-syntax hazard).

### 3.2 Regalloc-core-neutrality proof (the B3-1 gate)

B3-1 ships `abi_win64.tks` + the `shadow_space` field + a unit test that drives the SHARED `linear_scan`
over a hand-built `IntervalSet` under `win64()` and asserts the Win64-specific facts:
- (a) the allocatable order is caller-saved-first (`RAX(0)` before `RBX(3)`);
- (b) a call-crossing interval avoids the caller-saved set (RAX/RCX/RDX/R8/R9/R10/R11) — and, the Win64
  twist, MAY land on **RDI(7)/RSI(6)** (callee-saved on Win64), which the SAME test under `sysv64()`
  would push OFF (they are caller-saved there);
- (c) `is_callee_saved(win64(), preg(7/*RDI*/,GPR))` is **true** and `is_callee_saved(win64(),
  preg(6/*RSI*/,GPR))` is **true** (the flip) while `is_callee_saved(win64(), preg(1/*RCX*/,GPR))` is
  false;
- (d) `arg_reg(win64(), GPR, 0)` = RCX(1), `arg_reg(win64(), GPR, 3)` = R9(9), and `arg_reg(win64(),
  GPR, 4).ok == false` (the FOUR-register window — the fifth int arg is the `B3-args` stop);
- (e) `spill_scratch(win64(), GPR, 0/1)` = R10/R11.

This proves the shared core carries no SysV assumption BEFORE any Win64 frame or object code exists —
the smallest possible first step (mirrors B1-1/B2-1). No new instruction, no encoder change.

---

## 4. The Win64 frame delta — shadow space + the callee-saved set (B3-2)

The frame is the ONLY part of `encode_x86_64.tks` B3 touches, and only its side-table functions
(never an instruction encoder).

### 4.1 `func_makes_call_x86` — the shadow-space trigger

```teko
/**
 * func_makes_call_x86 — whether the physical `MFuncX86` contains any call
 * (`MCallX86`/`MCallIndX86`), so `compute_frame_layout_x86` knows to reserve the
 * Win64 shadow space (a caller-only obligation — a leaf function reserves none).
 * A total scan over every block's instructions, mirroring the shape of
 * `collect_callee_saved_x86`'s block walk. On SysV (`shadow_space == 0`) the
 * result is unused, so the scan runs only on the Win64 path.
 *
 * @param MFuncX86 f  the fully-physical function
 * @return bool  true iff the function issues at least one call
 */
fn func_makes_call_x86(f: MFuncX86) -> bool { … }
```

### 4.2 `compute_frame_layout_x86` — the shadow-space reservation (the one guarded change)

`compute_frame_layout_x86` (`encode_x86_64.tks:1021`) gains a shadow-space contribution, gated on
`abi.shadow_space > 0 && func_makes_call_x86(f)`:

- the shadow bytes are ADDED to `slots_size` before `frame_sub_size_x86` sizes the `sub rsp` amount, so
  the outgoing-argument region is reserved at the bottom of the frame ([rsp+0 .. rsp+32]);
- every real slot offset shifts UP by the shadow bytes (slots live ABOVE the shadow region), folded
  into the existing `shift_slot_offsets_x86` delta (`n_saved*8 + shadow`);
- 32 is 16-aligned, so the SysV alignment invariant (`(rsp+8)` 16-aligned at every callee entry) is
  preserved unchanged.

The doc-comment gains a Win64 clause; the SysV code path (`abi.shadow_space == 0`) is byte-identical.

> **The exit-corpus consequence (a REAL Win64 vs SysV difference).** B1's SysV `main` for `exit(n)` is
> **frameless** (a non-returning `call tk_exit`, no slots, no callee-saved → `size=0`, empty prologue).
> The Win64 `main` MAKES a call → needs 32 bytes shadow, so with the shadow folded into `slots_size`,
> `frame_sub_size_x86(32, 0) == align_up(32,16) == 32` and the function is **framed**, riding B1's
> existing framed prologue VERBATIM: `push rbp` (realigns the 8-mod-16 entry RSP to 0-mod-16),
> `mov rbp, rsp`, `sub rsp, 0x20` (=32, the shadow region at `[rsp+0..32]`), then `mov ecx, n` (the exit
> code — RCX is Win64 arg0 from `arg_reg(win64(),GPR,0)`, NOT RDI), then `call tk_exit`. RSP is
> 0-mod-16 with 32 bytes of shadow below at the `call`. `tk_exit` never returns, so no epilogue is
> reached — but the prologue IS emitted, exercising the framed path even for the exit corpus. This is
> the shadow space manifesting through the REUSED `emit_prologue_x86`/`emit_sub_rsp_x86`, only `size`
> differs (32 vs SysV's 0). For the corpus, `n_saved == 0`, so the shadow region trivially sits at the
> frame bottom; the general shadow-below-callee-saved-pushes ordering (when `n_saved > 0`) is a named
> frame-layout refinement — the corpus never has both callee-saved registers AND a call.

### 4.3 Frame goldens (the B3-2 gate)

- a Win64 `main` that calls `tk_exit(42)` → prologue `push rbp` [`55`], `mov rbp,rsp` [`48 89 E5`],
  `sub rsp, 0x20` (=32) [`48 83 EC 20`], arg `mov ecx, 42` [`B9 2A 00 00 00`], `call tk_exit`
  [`E8 00 00 00 00` + one `RelocX86{Plt32,"tk_exit"}`]; `compute_frame_layout_x86(win64(), f).size == 32`
  asserted directly (32 = the shadow space, folded into `slots_size` and 16-aligned);
- a Win64 LEAF function (no call) → NO shadow space, frameless when it also has no slots/callee-saved
  (proves `func_makes_call_x86` gates correctly);
- a Win64 function with an `MFrameAddrX86` slot → the slot offset is shifted above the 32-byte shadow
  region;
- the guardrail: EVERY existing B1 x86 frame golden (SysV, `shadow_space=0`) re-runs UNCHANGED and
  byte-identical (proof the field addition is behavior-preserving);
- the `B3-xmm-callee-saved` stop: a constructed function with a callee-saved XMM in `saved_fpr` raises
  the named error (never mis-emits).

---

## 5. `objfile_coff.tks` — the PE/COFF relocatable writer (B3-3)

The big new piece. Mirrors `objfile_macho.tks`'s shape (a `compute_coff_layout` pass then ordered
emit), consuming the ISA-agnostic `EncodedModuleX86` (the writer holds no x86 knowledge — E2/#226's
COFF linker reuses it). All fields little-endian. Reuses the neutral byte helpers
(`emit_u32_le`/`emit_u64_le`/`emit_str_bytes`/`emit_u8_field`/`pad_to_mult`/`align_up`).

### 5.1 The COFF object layout (bit-exact — the ground-truth tables)

An `IMAGE_FILE_MACHINE_AMD64` relocatable object. File order:
`[IMAGE_FILE_HEADER][.text data][.rdata data][.text relocations][symbol table][string table]`.
(No optional header — that is a PE image concern, not a relocatable object.) The section headers
follow the file header; each section header points at its raw data + its relocation array via file
offsets computed in `compute_coff_layout`.

**IMAGE_FILE_HEADER (20 bytes):**

| field | bytes | value |
|---|---|---|
| `Machine` | 2 | `0x8664` (IMAGE_FILE_MACHINE_AMD64) |
| `NumberOfSections` | 2 | 1 (`.text` only) or 2 (`+ .rdata` when rodata present) |
| `TimeDateStamp` | 4 | `0` (deterministic — FIXPOINT; NOT the wall clock) |
| `PointerToSymbolTable` | 4 | file offset of the symbol table (computed) |
| `NumberOfSymbols` | 4 | symbol-record count (incl. any aux records — §5.3) |
| `SizeOfOptionalHeader` | 2 | `0` (relocatable object) |
| `Characteristics` | 2 | `0` (`IMAGE_FILE_LARGE_ADDRESS_AWARE`=0x0020 optional; 0 is accepted) |

**IMAGE_SECTION_HEADER (40 bytes each):**

| field | bytes | `.text` | `.rdata` |
|---|---|---|---|
| `Name` | 8 | `".text\0\0\0"` | `".rdata\0\0"` (≤ 8 bytes → inline; a longer name uses `"/off"` into the strtab) |
| `VirtualSize` | 4 | `0` | `0` (0 in a relocatable object) |
| `VirtualAddress` | 4 | `0` | `0` |
| `SizeOfRawData` | 4 | `text.len` | `rodata.len` |
| `PointerToRawData` | 4 | file offset of `.text` bytes | file offset of `.rdata` bytes |
| `PointerToRelocations` | 4 | file offset of the `.text` reloc array (0 when none) | `0` |
| `PointerToLinenumbers` | 4 | `0` | `0` |
| `NumberOfRelocations` | 2 | reloc count (§5.4) | `0` |
| `NumberOfLinenumbers` | 2 | `0` | `0` |
| `Characteristics` | 4 | `0x60500020` = CNT_CODE(0x20)\|MEM_EXECUTE(0x20000000)\|MEM_READ(0x40000000)\|ALIGN_16BYTES(0x00500000) | `0x40400040` = CNT_INITIALIZED_DATA(0x40)\|MEM_READ(0x40000000)\|ALIGN_8BYTES(0x00400000) |

> **`>0xFFFF` relocations trap (named, N/A for the corpus).** COFF's `NumberOfRelocations` is 16-bit;
> a section with > 0xFFFF relocations sets the `IMAGE_SCN_LNK_NRELOC_OVFL` flag + stores the real count
> in the first reloc's `VirtualAddress`. The corpus has one reloc per function, so this is a named
> `B3-relocovfl` internal-invariant stop (never reached — mirrors A4/B1's far-branch stops).

**IMAGE_SYMBOL (18 bytes each — NOT 4-aligned, the table is packed):**

| field | bytes | meaning |
|---|---|---|
| `Name` | 8 | name ≤ 8 → the raw bytes NUL-padded to 8; name > 8 → `[0,0,0,0]` (the `Zeroes` sentinel) then a 4-byte little-endian `Offset` into the string table (§5.2) |
| `Value` | 4 | defined: the symbol's section-relative byte offset (`Symbol.offset`); undefined: 0 |
| `SectionNumber` | 2 | 1-based (1=`.text`, 2=`.rdata`); `0` = IMAGE_SYM_UNDEFINED (external undefined) |
| `Type` | 2 | `0x20` (DTYPE_FUNCTION<<4) for a `.text` function; `0` for a data/section symbol |
| `StorageClass` | 1 | `2` IMAGE_SYM_CLASS_EXTERNAL (global functions + undefined externs); `3` IMAGE_SYM_CLASS_STATIC (the `.rdata` section symbol / file-local) |
| `NumberOfAuxSymbols` | 1 | `0` for the MVP (a section-definition aux record is the `B3-sectaux` detail, §5.3) |

**IMAGE_RELOCATION (10 bytes each):**

| field | bytes | meaning |
|---|---|---|
| `VirtualAddress` | 4 | the `.text`-section-relative byte offset of the patched field (module-rebased, the A4-3/B1-6 rebase — ports directly from `RelocX86.offset`) |
| `SymbolTableIndex` | 4 | the 0-based index of the target symbol in the COFF symbol table (§5.5 — the A4-4/B1-7-class correctness point) |
| `Type` | 2 | `IMAGE_REL_AMD64_*` (§5.4) |

### 5.2 The COFF string table (`build_coff_strtab`) — the 4-byte-size-prefixed, inline-≤8 form

COFF's string table is **unique**: a 4-byte little-endian total-size field (INCLUDING the 4 size
bytes, so the minimum value is 4), followed by NUL-terminated names — and it holds ONLY names longer
than 8 bytes (names ≤ 8 are stored inline in the symbol's `Name` field). On Windows **x64** the C ABI
uses **bare** symbol names (no `_` prefix — unlike Win32 x86 and unlike Mach-O). So B3 needs its own
`build_coff_strtab` (NOT Mach-O's `build_strtab`, which prepends `_`; NOT ELF's `build_elf_strtab`,
which is a bare NUL-run with no size prefix nor inline optimization):

```teko
/**
 * CoffStrtab — the assembled COFF string table plus the per-symbol name
 * resolution: the string-table image (a 4-byte little-endian total-size field —
 * INCLUDING itself, minimum 4 — then each >8-byte name NUL-terminated), and, per
 * symbol in emission order, either the ≤8-byte inline name bytes or the strtab
 * offset a >8-byte name resolves to. Names ≤ 8 bytes never enter `bytes`.
 *
 * @since #388 B3-3
 */
type CoffStrtab = struct {
    /** bytes — the string-table image (4-byte size prefix + NUL-terminated long names). */
    bytes: []byte
    /** inline_name — per symbol: the ≤8 raw name bytes when the name fits inline, else empty. */
    inline_name: [][]byte
    /** long_offset — per symbol: the strtab offset for a >8-byte name, else 0. */
    long_offset: []u32
}

/**
 * build_coff_strtab — assemble the COFF string table from the symbol list: every
 * name ≤ 8 bytes is recorded for INLINE emission in the symbol's 8-byte `Name`
 * field (NUL-padded, bare — no `_` prefix on Windows x64); every longer name is
 * appended to `bytes` (NUL-terminated) and its offset recorded so the symbol emits
 * the `[0,0,0,0]`+offset union. `bytes` opens with the 4-byte total-size field
 * (patched last, once every long name is placed). The COFF analog of Mach-O's
 * `build_strtab` (which prepends `_`) and ELF's `build_elf_strtab` (a bare
 * NUL-run) — neither reusable, since COFF's size-prefix + inline-≤8 layout is
 * structurally distinct (§5.1, D-B).
 *
 * @param []Symbol symbols  the symbol table, in emission order
 * @return CoffStrtab  the string-table bytes + per-symbol inline/offset resolution
 */
fn build_coff_strtab(symbols: []Symbol) -> CoffStrtab { … }
```

### 5.3 The symbol table — local(static)-then-external, aux-free MVP

The symbol emission order mirrors the ELF/Mach-O convention (file-local statics, then defined globals,
then undefined externs) — **for determinism**, though COFF (unlike ELF's `sh_info`) imposes no ordering
requirement; correctness rides purely on `SymbolTableIndex` (§5.5). The set for the corpus:
- a **`.rdata` section symbol** (only when rodata is present): `Name=".rdata"` (inline), `Value=0`,
  `SectionNumber=2`, `Type=0`, `StorageClass=3` (STATIC) — rodata relocations target THIS symbol +
  an in-place addend (§5.4), the COFF idiom that dodges per-string symbols (the A4-4/B1-7 "local
  rodata symbol" finding, COFF form). `NumberOfAuxSymbols=0` (a full toolchain emits a 1-aux
  section-definition record here; lld/lld-link accept a 0-aux static section symbol — the aux record
  is the named `B3-sectaux` detail if a linker rejects the bare form);
- **defined functions**: `Name` inline-or-offset, `Value=section-relative offset` (NOT rebased —
  COFF `Value` in a relocatable is section-relative, `SectionNumber` names the section, like ELF's
  `st_value` and UNLIKE Mach-O's absolute `n_value`; the A4-4 Mach-O rebase is INAPPLICABLE, and
  stating so is the requirement so no one ports it by reflex), `SectionNumber=1`, `Type=0x20`,
  `StorageClass=2` (EXTERNAL);
- **undefined externs** (`tk_exit`, later `tk_set_args`/`tk_region_*`): `Value=0`, `SectionNumber=0`
  (UNDEFINED), `Type=0`, `StorageClass=2`.

`count_local`/`count_defined` (`objfile_macho.tks:141,160`, same-ns) port directly for the ordering.

### 5.4 The relocations the corpus needs (`coff_reloc_type`) — REUSE `RelocKindX86`

The x86 encoder already tags each fixup with `RelocKindX86` (`Plt32`/`Pc32`/`Abs64`); COFF re-maps them
numerically (the ONLY COFF knowledge the writer injects), exactly as the ELF adapter does with
`elf_reloc_type`. **The `RelocKindX86` enum is REUSED unchanged** — no new reloc enum:

| `RelocKindX86` | source | COFF type | value | addend handling |
|---|---|---|---|---|
| `Plt32` | `MCallX86` (`call rel32`) | `IMAGE_REL_AMD64_REL32` | `0x0004` | **the −4 is IMPLICIT** in REL32 (defined relative to the next instruction); the in-place rel32 field stays 0 → linker computes `S − (P+4)` |
| `Pc32` | `MLeaRipX86` (`lea [rip+d]`) | `IMAGE_REL_AMD64_REL32` | `0x0004` | −4 implicit; a rodata target's byte offset is written **in-place** into the disp32 field (`B3-rodata-addend`, §7 — the exit corpus never hits it) |
| `Abs64` | data global (deferred `B3-globals`) | `IMAGE_REL_AMD64_ADDR64` | `0x0001` | absolute; addend in-place |

```teko
/**
 * coff_reloc_type — the numeric `IMAGE_REL_AMD64_*` type for the reused
 * `RelocKindX86` the x86 encoder already tags: `Plt32`/`Pc32` → REL32(0x0004)
 * (a PC-relative-to-next-instruction fixup — the −4 SysV/ELF addend is IMPLICIT
 * in the COFF REL32 definition, so it is NOT written to a reloc field the way ELF
 * writes `r_addend`), `Abs64` → ADDR64(0x0001). The ONLY COFF-specific knowledge
 * the writer injects (the adapter mirror of `elf_reloc_type`).
 *
 * @param RelocKindX86 kind  the relocation kind the encoder tagged
 * @return u32  the `IMAGE_RELOCATION` `Type` field value
 */
fn coff_reloc_type(kind: RelocKindX86) -> u32 {
    match kind {
        Plt32 => 0x0004 to u32
        Pc32  => 0x0004 to u32
        Abs64 => 0x0001 to u32
    }
}
```

> **The addend inversion (bake in — the B1→B3 gotcha).** ELF carries `RelocX86.addend = −4` in an
> explicit `r_addend` field. COFF has **no addend field**: for `IMAGE_REL_AMD64_REL32` the −4 is baked
> into the type's definition (relative to `P+4`), so the writer **DROPS the −4** (the rel32 in-place
> field stays 0, as the encoder already emits). A non-(−4) addend — a rodata `Pc32` folding a `.rdata`
> byte offset — must be written IN-PLACE into the disp32 field of the `.text` bytes (the Mach-O idiom),
> the `B3-rodata-addend` detail. The exit corpus's ONLY reloc is the `call tk_exit` REL32 (addend −4,
> implicit), so no in-place addend write is exercised e2e — unit-goldened only, exactly like B1's
> `MLeaRipX86`.

### 5.5 The writer entry + the baked findings

```teko
/**
 * emit_coff — assemble an `IMAGE_FILE_MACHINE_AMD64` relocatable PE/COFF object
 * from an `EncodedModuleX86`: the 20-byte `IMAGE_FILE_HEADER` + the
 * `IMAGE_SECTION_HEADER`s (`.text`, and `.rdata` when rodata is present) + the
 * section images + the `.text` `IMAGE_RELOCATION` array + the 18-byte
 * `IMAGE_SYMBOL` table (static-then-external, the `.rdata` STATIC section symbol,
 * bare Windows-x64 names) + the 4-byte-size-prefixed string table (only >8-byte
 * names). Enough for `clang -target x86_64-windows` / `lld-link` to link into a
 * runnable `.exe` against the C-built `teko_rt` (D-2 option 1 bootstrap). The
 * writer is ISA-agnostic (consumes only `EncodedModuleX86`'s byte images +
 * symbols + relocations), so E2/#226's COFF linker reuses its shape. Data globals
 * + the callee-saved-XMM save path are the `B3-globals`/`B3-xmm-callee-saved`
 * stops, raised earlier in the pipeline.
 *
 * @param EncodedModuleX86 enc  the section images + symbols + relocations
 * @return []byte  the PE/COFF object file bytes
 */
pub fn emit_coff(enc: EncodedModuleX86) -> []byte { … }
```

Baked A4-4/B1-7-class findings (as COFF requirements):
- **Symbol value model.** `IMAGE_SYMBOL.Value` in a relocatable is **section-relative** + `SectionNumber`
  (like ELF `st_value`, NOT Mach-O's absolute `n_value` rebase) — do NOT port the Mach-O
  `section_addr + offset`. Stating the inversion IS the requirement.
- **Reloc `VirtualAddress` rebase.** `.text`-relative + module-rebased per function (the A4-3/B1-6
  rebase, `RelocX86.offset` already carries it).
- **`SymbolTableIndex` correctness (the A4-4/B1-7 class).** The 0-based index MUST count any aux
  records that precede a symbol; the MVP uses 0 aux records, so index == emission position and
  `coff_symbol_index(symbols, name)` (the `sym_index` analog) resolves it. If `B3-sectaux` later adds a
  section-definition aux record, the indices MUST shift to account for it — a golden pins this.
- **Local rodata via the `.rdata` STATIC section symbol** + in-place addend (the COFF form of
  "rodata symbols are file-local"), so two objects never clash at the E2 link.

---

## 6. Target dispatch + the Windows differential (B3-4, the KEYSTONE)

### 6.1 `emit_native` gains a fourth target arm (additive, FIXPOINT-preserving)

`NativeTarget` (`project.tks:699`) gains an `X8664Windows` case; `target_from_name` (`:713`) maps
`"x86_64-windows"` (and the `"x86_64-win"`/`"x86_64-pe"` deprecated aliases) → `X8664Windows`;
`emit_native`'s `match` (`:777`) gains the arm dispatching to a new `emit_native_win` tail (mirroring
`emit_native_x86:827`):

```teko
/**
 * emit_native_win — the Windows x86-64 / PE-COFF own-backend tail (#388 B3): select
 * x86-64 `MInstX86` from the lowered module (the REUSED B1 isel), allocate registers
 * under the Win64 register file (`win64` — RCX/RDX/R8/R9 args, RDI/RSI callee-saved,
 * 32-byte shadow space), encode to the SAME x86 machine bytes (the REUSED B1 encoder —
 * only the frame reserves shadow space, §4), wrap them in a PE/COFF object
 * (`emit_coff`), and hand the bytes to `finish_native_object` (write `.o` +
 * clang/lld-link + `.tsym` + the `"(own backend)"` marker). Reached only via
 * `TEKO_TARGET=x86_64-windows`; the emitted object is a Win64 COFF, so the executing
 * C-vs-own differential runs on the windows-x86_64 runner (which runs PE NATIVELY —
 * no emulator, unlike B2's qemu), while a macOS host still writes the object for the
 * byte / `llvm-readobj --sections/--symbols/--relocations` cross-check even though it cannot LINK or RUN a PE.
 * The isel/regalloc/encoder are the B1 x86 code UNCHANGED — only `win64()` + `emit_coff`
 * differ (§1.1).
 *
 * @param dir   the project directory (for diagnostics)
 * @param od    the slash-stripped output directory (already created)
 * @param stem  the output artifact name (the manifest `name`)
 * @param lmod  the target-independent lowered module
 * @param prog  the checked program (extern-reachability, `.tsym`)
 * @param m     the resolved manifest (link knobs)
 * @return      0 on a successful build+link, else the failing status
 */
fn emit_native_win(dir: str, od: str, stem: str, lmod: teko::lir::LModule, prog: checker::TProgram, m: Manifest) -> i32 {
    let sel = match teko::backend::select_module_x86(lmod) { teko::backend::MModuleX86 as x => x; error as e => return fail(dir, e.message) }
    let col = match teko::backend::regalloc_module_x86(teko::backend::win64(), sel) { teko::backend::MModuleX86 as x => x; error as e => return fail(dir, e.message) }
    let enc = match teko::backend::encode_module_x86(teko::backend::win64(), col) { teko::backend::EncodedModuleX86 as x => x; error as e => return fail(dir, e.message) }
    finish_native_object(dir, od, stem, teko::backend::emit_coff(enc), prog, m)
}
```

The `Arm64Macho`/`X8664Linux`/`Riscv64Linux` arms are UNCHANGED (verbatim A4/B1/B2 bodies), so the
default build stays byte-identical (FIXPOINT) and all three existing differentials are untouched.
`finish_native_object` is REUSED verbatim — it writes `<stem>.o`, links via `resolve_cc`'s compiler
(on the Windows runner that is host `clang`, which drives `lld-link`), and prints the exclusive
`"(own backend)"` marker. The `.o` extension is kept (clang/lld-link consume a COFF object regardless
of extension); the Windows executable naming (`.exe`) is whatever the EXISTING C-backend Windows link
path already produces via `build_cc_argv` — B3 inherits it, no new logic (the C backend already ships
native Windows binaries in `release.yml`).

> **Cross-host honesty.** On the macOS-arm64 dev host, selecting `x86_64-windows` PRODUCES a valid COFF
> object but the host toolchain cannot LINK a PE nor RUN it — so the link honest-skips. `emit_native_win`
> still writes the `.o` before the link attempt, so the object is always available for the
> `llvm-readobj --sections/--symbols/--relocations` byte cross-check (§6.2). Only the windows-x86_64 runner EXECUTES the lane.

### 6.2 The differential — WINDOWS runs PE natively, macOS byte-tests

Extend `scripts/diff_c_own.sh` with a **windows-x86_64 lane**, detected by a Git-Bash/MSYS `uname`
(`MINGW*`/`MSYS*`/`CYGWIN*` with `x86_64`): set `FLAVOR="windows-x86_64 PE/COFF"`,
`OWN_TARGET_ENV="TEKO_TARGET=x86_64-windows"`, `OBJ_CHECK="$script_dir/scripts/check_coff.sh"`,
`RUN_WRAP=""` (Windows RUNS the PE natively — no emulator). Per fixture: build the C-native `.exe`
(default C backend), build the own-native `.exe` (`TEKO_BACKEND=native TEKO_TARGET=x86_64-windows`,
linked by host clang/lld-link), run BOTH natively, assert identical exit code + stdout, plus
`check_coff.sh` well-formedness on the own object. The SAME F1 guards apply (the own build MUST print
`"(own backend)"`; the object MUST exist). It reuses the corpus loop + KNOWN-STOP logic verbatim
(`own_print_exit` stays a KNOWN-STOP — the shared `println` LIR-builtin gap surfaces as an lld-link
undefined-symbol error, the same target-independent gap B1/B2 inherit).

`scripts/check_coff.sh` (new, the COFF sibling of `check_elf.sh`/`check_macho.sh`): `llvm-readobj
--file-headers --sections --symbols --relocations` (the format-generic flags — `--coff-sections`/
`--coff-symbols`/`--coff-relocations` do NOT exist in llvm-readobj ≥ 18; the generic flags are stable
across versions and already cross-format on a COFF object) asserts `IMAGE_FILE_MACHINE_AMD64` + a
`.text` section + a defined text symbol + a `REL32` reloc; `llvm-objdump -d` disassembles `.text`; a
`lld-link`/`clang -target x86_64-windows` dry link (or `-c` consumability) confirms the object is
linker-acceptable. `llvm-readobj`/`llvm-objdump` are cross-format, so the script runs on macOS too
(execution honest-skips; the byte-goldens are the machine-free proof).

**CI wiring.** The `windows-selfhost` lane (`sanitizers.yml:239`) already provisions the seed +
self-builds gen-1 on Windows and is already BLOCKING in the sanitizer `gate` job (`:267`). B3-4 adds
one step to it: after `Build gen1`, run `diff_c_own.sh` (host-detected Windows lane). A red Windows
differential then gates the merge (main-integrity law). No new runner, no qemu — the least-new-infra
placement, and the FIRST executing own==C differential that runs the artifact NATIVELY on its target OS.

**Validation matrix (which runs where):**

| check | windows-x86_64 (CI) | macOS-arm64 (dev + CI) |
|---|---|---|
| encoder golden byte-vectors (already B1's — reused, shared ISA) | — (B1 owns) | ✓ unit gate |
| `emit_coff` header/section/symtab/reloc golden bytes (`clang -target x86_64-windows-msvc -c` oracle) | ✓ | ✓ (clang cross-targets; machine-free) |
| Win64 frame goldens (`sub rsp,32` shadow) + B1 SysV frame goldens byte-identical | ✓ | ✓ |
| `.obj` well-formedness (`llvm-readobj --sections/--symbols/--relocations` / `llvm-objdump` / `lld-link`) | ✓ | ✓ (llvm tools cross-format) |
| **executing `C-native == own-native` differential** | ✓ (runs the PE natively) | honest-skip (cannot run PE) |
| arm64 Mach-O + x86-64 ELF + riscv64 ELF differentials | unchanged | unchanged |

### 6.3 The interp oracle — NO parallel `minst_x86_interp` (inherited from B1's R-2)

B3 adds no instruction semantics (it reuses `MInstX86` whole), so the oracle question is B1's exactly:
the target-independent LIR interp + the assembler-cross-checked goldens + the EXECUTING Windows
differential (a runner that runs the artifact natively) cover the oracle role. No parallel interp is
built (inherited `B1-interp`, REPORTED not blocking).

---

## 7. Honest-stops — which slice each lives behind (0.3.1 completion vs later)

| stop | slice | what it defers | closed by |
|---|---|---|---|
| `B3-fp` | B3-1/B3-2 | SSE float ops/conversions + the Win64 XMM0-3 float-arg path | 0.3.1 (mirrors `A4-fp`/`B1-fp`; reuses B1's SSE encodings) |
| `B3-xmm-callee-saved` | B3-2 | the `movaps` save/restore of callee-saved XMM6-15 (`emit_saved_pushes_x86` is GPR-only) — reached only when FP allocates an XMM live across a call | 0.3.1 (rides `B3-fp`) |
| `B3-argslot` | B3-1 | the Win64 positional register-slot rule (Nth arg = Nth int reg OR Nth XMM) + variadic float-in-both-int-and-xmm — FP-only | 0.3.1 (rides `B3-fp`) |
| `B3-args` | B3-1/B3-4 | stack args past the 4-int/4-fp window + `tk_set_args` argv forwarding | 0.3.1 (mirrors `B1-args`; NARROWER window than SysV's 6) |
| `B3-globals` | B3-3 | `.data`/`.bss` + `IMAGE_REL_AMD64_ADDR64` data relocs — N2 lowers no `LGlobal` | when lowering emits globals |
| `B3-rodata-addend` | B3-3 | the in-place disp32 addend for a rodata `Pc32` (the corpus's only reloc is `call` REL32, addend −4 implicit) | unit-goldened; hit when a rodata `lea` lands e2e |
| `B3-sectaux` | B3-3 | the section-definition AUX symbol record for `.rdata` (the 0-aux static section symbol is accepted by lld-link; add the aux only if a linker rejects it) | if a target linker requires it |
| `B3-relocovfl` | B3-3 | the `IMAGE_SCN_LNK_NRELOC_OVFL` > 0xFFFF-relocs overflow (internal invariant — one reloc per function) | (mirrors A4/B1 far-branch stops) |
| FPR spill | — inherited | `detect_fpr_spill` analog errors before encode (A3's `A3-fpr-spill`) | A3's follow-up |
| `loop` back-edge | — inherited | regalloc honest-stops (`A3-loop`) before encode | A3's `A3-loop` |
| i128 register-pair ops | — inherited | isel never emits them (rides A2's i128 route) | A2's i128 route |
| windows-arm64, wasm | later clusters | other targets | B3-follow (arm64 COFF), C1 (#389) |
| `--backend`/`--target` flags | later cluster | the real manifest flags (B3 uses env seams) | D1/#390 |
| own PE/COFF linker (drop clang/lld-link) | later cluster | the COFF static linker | Phase E2/#226 |

Each stop is a NAMED error the pipeline surfaces; a fixture reaching one stops IDENTICALLY on the own
side and is compared at the stop, never at a fabricated value (M.3, the A4/B1/B2 precedent).

> **windows-arm64 (`#388`'s second half) is a NAMED follow, not part of this MVP.** The arch doc scopes
> B3 as x86_64 + arm64 Windows. This plan delivers **x86_64 Windows** (the reuse-heavy half) and NAMES
> **arm64 Windows** as the follow-on: it needs `abi_win64_arm` (AAPCS-on-Windows differs from AAPCS64:
> different callee-saved set, no red zone) + reuse of the A4 arm64 encoder + `emit_coff` with
> `Machine=0xAA64` (IMAGE_FILE_MACHINE_ARM64). The `windows-arm64` CI lane is DISABLED (owner ruling
> 2026-07-06, #304), so its differential cannot run in CI today — the arm64-Windows half waits on that
> lane's re-enable and is REPORTED up (§8 R-3), keeping #388's x86_64 half a complete, gate-able
> deliverable (the issue-is-100% law: the x86_64 half is 100% of what CAN be gated now; the arm64 half
> is honestly blocked on infra, surfaced for the integrator to sequence).

---

## 8. Deferrals + recorded decisions (law-first — NO HALT) + reported findings

**D-A · Win64 result register (RAX) = SysV result register (RAX), ≠ arg0 (RCX).** Win64's integer
result is RAX, identical to SysV — so B1's D-A (RAX pinned at the return site, distinct from arg0)
carries UNCHANGED; the isel result-pin path is reused verbatim. arg0 = RCX (from `arg_reg(win64(),…)`),
not RDI — free from the descriptor. Recorded.

**D-B · COFF is a PARALLEL writer, not a generalization of `emit_elf_object`.** §2.1/§5. The flagged
tension. Resolved law-first (M.1 smallest-blast + M.5 one-way): COFF diverges from ELF at EVERY record
(18B symbols with the name union, no addend field, per-section reloc pointers, size-prefixed strtab),
so folding it into the neutral ELF core would blast the writer B1 AND B2 ride. The `emit_macho`
precedent is decisive; `objfile_coff.tks` is the third parallel writer, reusing only the neutral byte
helpers + `Symbol`. A tri-format `emit_elf_object` and a Mach-O↔COFF core are both REJECTED (branch-heavy,
every record byte-differs). **This is a flagged potential-HALT with a clear winner, so it does NOT HALT.**
Recorded.

**D-C · The Win64 frame delta lives in `compute_frame_layout_x86` via an additive `AbiDescriptor.
shadow_space` field; the instruction encoder is NEVER forked.** §2.2/§4. The flagged tension. Resolved
law-first (M.1 smallest-blast + fixpoint-guarded): the callee-saved SET is free from the descriptor
partition; the 32-byte shadow space is a frame-reserve convention that goes on the descriptor as a
field read by the frame side-table (zero signature changes; `shadow_space=0` keeps every other ABI
byte-identical). A `frame_conv` param (changes 3 frozen B1 signatures) and a `compute_frame_layout_win64`
wrapper (still needs `encode_module_x86` dispatch + duplicates framed-detection) are REJECTED. The
callee-saved-XMM save path is the NAMED `B3-xmm-callee-saved` stop. **A flagged potential-HALT with a
clear winner, so it does NOT HALT.** Recorded.

**D-D · `RelocKindX86` REUSED (no new COFF reloc enum); `coff_reloc_type` re-maps it.** §5.4. The x86
encoder already tags `Plt32`/`Pc32`/`Abs64`; COFF maps them to REL32/ADDR64 in an adapter, exactly as
`elf_reloc_type` does. The −4 addend is DROPPED (implicit in COFF REL32). Recorded.

**D-E · `TEKO_TARGET=x86_64-windows` env seam (not the `--target` flag).** Mirrors B1's D-D / B2's D-E /
A4's D-B. Additive `NativeTarget::X8664Windows` + `target_from_name` entry; default → arm64
(fixpoint-preserving); D1/#390 supersedes. Recorded.

**D-F · clang/lld-link-as-linker on the Windows runner (D-2 option 1 bootstrap, inherited).** B3 links
the COFF `.o` via host `clang -target x86_64-windows` (which drives `lld-link`) against the C-built
`teko_rt`; toolchain independence on Windows arrives at E2/#226 (the PE/COFF static linker), NOT at
#388 — recorded so no one claims independence prematurely (M.3, the A4/B1/B2 precedent).

**Reported findings (adjacent — NOT turned into issues here; folded/sequenced by the integrator):**

- **R-1 · The parallel COFF writer duplicates the section-layout WALK.** `emit_coff` mirrors
  `emit_macho`'s `compute_*_layout`-then-ordered-emit shape (as `emit_elf_object` does). This is the
  SAME per-format mirroring the three writers already embody; the genuinely-shared byte helpers +
  `Symbol` stay single-sourced. A future extraction — a neutral "object skeleton" describing
  sections/symbols/relocs that each format renders — is the same DRY-sweep candidate B1's R-1 / B2's
  R-1 raise. REPORTED for the integrator to sequence (a 0.3.1 DRY-sweep companion), not a B3 blocker.
- **R-2 · No parallel `minst_x86_interp`.** §6.3 — inherited from B1's R-2; the LIR interp + goldens +
  the executing Windows differential cover the oracle role. REPORTED.
- **R-3 · windows-arm64 (the second half of #388) is infra-blocked.** §7 — the `windows-arm64` CI lane
  is DISABLED (owner #304), so its differential cannot gate in CI. The x86_64 half is delivered 100%;
  the arm64 half (reuse the A4 arm64 encoder + `abi_win64_arm` + `emit_coff` with `Machine=0xAA64`)
  waits on that lane's re-enable. REPORTED for the integrator to sequence — surfaced, never silently
  dropped (issue-is-100%: the plan delivers all of #388 that is gate-able today + names the honest
  infra block on the rest).
- **R-4 · `own_print_exit` (LIR builtin-call surface).** The KNOWN-STOP (`diff_c_own.sh:190`, `println`
  vs `tk_println`) is a shared LIR-lowering gap — it KNOWN-STOPs identically on the Windows lane (the
  lld-link rejects the undefined `println`). Already a reported finding; B3 inherits it unchanged.

---

## 9. Regression fixtures + the gate

### 9.1 Golden byte-vector unit tests — assembler-oracled

- **COFF writer** (B3-3, `objfile_coff_test.tkt`): `IMAGE_FILE_HEADER` `Machine=0x8664` +
  `NumberOfSections`; the `.text` `IMAGE_SECTION_HEADER` `Characteristics=0x60500020`; a one-function
  module's symbol table — `main` defined (`SectionNumber=1`, section-relative `Value`, `StorageClass=2`,
  `Type=0x20`) and `tk_exit` undefined (`SectionNumber=0`); the `.text` `IMAGE_RELOCATION`
  (`Type=0x0004` REL32, correct `SymbolTableIndex`); a >8-byte name (`tk_region_alloc`) → the
  `[0,0,0,0]`+offset union + a string-table entry (the size-prefix + inline-≤8 mechanism); the
  `.rdata` STATIC section symbol when rodata present. **Each vector re-derived against
  `clang -target x86_64-windows-msvc -c` + `llvm-readobj --sections/--symbols/--relocations`** (the table is spec, the assembler
  is oracle — the A4/B1/B2 discipline).
- **Win64 frame** (B3-2, `encode_x86_64_test.tkt` extension): a `main` calling `tk_exit(42)` under
  `win64()` → `compute_frame_layout_x86(win64(), f).size == 32`, framed prologue `55` (`push rbp`) +
  `48 89 E5` (`mov rbp,rsp`) + `48 83 EC 20` (`sub rsp,32`), arg `mov ecx,42` (`B9 2A 00 00 00`),
  `call` reloc; a Win64 leaf → frameless (`func_makes_call_x86` gates); a slot → shifted above the
  32-byte shadow region; the `B3-xmm-callee-saved` stop; **every B1 SysV frame golden re-runs
  byte-identical** (the field-addition guardrail).
- **Instruction encodings** — NONE new (the x86 ISA is B1's, reused whole; B1's `encode_x86_64_test.tkt`
  is the guardrail, unchanged).

### 9.2 End-to-end differential fixtures — the SAME corpus A4/B1/B2 use (`examples/regressions/own_*`)

B3 adds NO new fixtures; it adds the Windows own-native column to the existing corpus
(`diff_c_own.sh:153`):

| fixture | program | expected exit | VM | C-native | own-arm64 | own-x86 | own-riscv | **own-win (new)** |
|---|---|---|---|---|---|---|---|---|
| `own_exit_zero` | `exit(0)` | 0 | ✓ | ✓ | ✓ | ✓ | ✓ | **win-run** |
| `own_exit_code` | `exit(42)` | 42 | ✓ | ✓ | ✓ | ✓ | ✓ | **win-run** (`mov ecx,42` + shadow `sub rsp,32` + `call`) |
| `own_arith_exit` | `exit(6 * 7)` | 42 | ✓ | ✓ | ✓ | ✓ | ✓ | **win-run** (two-address `imul` path) |
| `own_sub_exit` | `exit(100 - 58)` | 42 | ✓ | ✓ | ✓ | ✓ | ✓ | **win-run** |
| `own_if_exit` | `if 5 > 3 { exit(1) } else { exit(2) }` | 1 | ✓ | ✓ | ✓ | ✓ | ✓ | **win-run** (`jcc rel32` path) |
| `own_match_exit` | `match k { 0 => exit(7); _ => exit(9) }` | (per k) | ✓ | ✓ | ✓ | ✓ | ✓ | **win-run** |
| `own_print_exit` | `println` then `exit` | — | ✓ | ✓ | KSTOP | KSTOP | KSTOP | **KSTOP** (R-4, inherited) |

Every ISA path (two-address ALU, `imul`, `jcc rel32`, the `call` reloc) is EXACTLY B1's — the Windows
lane re-validates it end-to-end through a DIFFERENT ABI (Win64 arg regs + shadow space) and a DIFFERENT
object format (COFF), so it hardens the shared pipeline against ABI/format coupling.

### 9.3 Ritual posture — RIGHT-SIZED (dono ruling 2026-07-10; CI is the gate)

- **CI is the gate.** The FULL gate (both engines + FIXPOINT + paranoid + the Windows differential) runs
  in CI at the ritual points. Local per-slice verification is PROPORTIONATE — the
  default-path-unchanged proof + the slice's own goldens/tests + (for the frame slice) B1's x86 frame
  goldens re-green byte-identical + (for the writer slice) the `emit_coff` goldens + a `lld-link`
  consumability check. **NO local 2-gen-fixpoint or paranoid for the mechanical slices** — CI does it.
- **FIXPOINT is structural, not re-proven locally per slice.** B3 is purely ADDITIVE files + one
  guarded `AbiDescriptor` field (0 for every existing ABI) + one `NativeTarget` arm whose default
  (arm64) path is verbatim, so the default output is byte-unchanged by construction; CI's fixpoint leg
  confirms it once.
- **100% coverage on new code (definition-of-done).** The COFF writer is branchy (per-section,
  inline-vs-strtab name, per-reloc-kind, static-vs-external symbol) — cover every branch + every
  honest-stop arm via goldens; a genuinely unreachable arm is justified in the PR.
- **VM-gotcha watch** (dense byte-work, the A4/B1/B2 list carries over): (a) build the COFF records in
  `u32`/`u64`, narrow to `byte` only at the last LE-emit step; (b) the 18-byte `IMAGE_SYMBOL` is
  UNALIGNED — do not `pad_to_mult` the symbol table (COFF packs it); (c) no `x = match {…return}` —
  use `let x = match {…}` then act; (d) a NEGATIVE `to u32`/`to u8` **panics** — the strtab 4-byte size
  field + any offset math stays in `u32` (never a negative cast); (e) `flags` is a reserved keyword —
  name the section-characteristics local `sect_chars`/`characteristics` (not `flags`); (f)
  struct-literals span lines with a leading-field-per-line layout (the `win64()`/`AbiDescriptor` shape).
- **Bootstrap-seed lane.** B3 uses ONLY existing syntax (new `.tks` files + one additive struct field +
  additive dispatch), so the stable-seed lane parses all of `src/` — no staged-syntax hazard (the wave
  stable-seed gotcha does not bite).

### 9.4 Ritual points (where the FULL CI gate must pass)

- After **B3-1** (abi + core-neutrality proof): full CI gate + the neutrality unit test green (the
  RDI/RSI-callee-saved flip + the 4-arg window) — the cheapest proof the shared core carries no SysV
  assumption.
- After **B3-2** (Win64 frame): full CI gate + the Win64 frame goldens (`sub rsp,32` shadow) green +
  **every B1 SysV frame golden byte-identical** (the field-addition guardrail).
- After **B3-3** (COFF writer): full CI gate + the `emit_coff` header/section/symtab/reloc goldens
  green + `llvm-readobj --sections/--symbols/--relocations` / `lld-link` consumability on the emitted object (any host,
  machine-free).
- The **KEYSTONE full CI ritual at B3-4**: the whole gate — both engines + fixpoint + the **windows-x86_64
  C-vs-own leg green** (executing the PE NATIVELY on the runner) + the macOS byte-test lane green + all
  three prior differentials (arm64 Mach-O, x86-64 ELF, riscv64 ELF) unchanged. **#388 x86_64 CLOSES
  here** (arm64-Windows is the named infra-blocked follow, R-3).

---

## 10. Integration + merge strategy (dono ruling 2026-07-10)

From B2 on, sub-sub-PRs **MERGE** into the mini-umbrella (not squash — preserve the B3-1…B3-4 slice
history), and the mini-umbrella **SQUASHES** into the umbrella:

- Each `B3-N` is a sub-sub-PR on `fix/issue-388-windows-coff-N` (worktree-isolated) with base the **B3
  mini-umbrella branch** (`fix/issue-388-windows-coff`, tracked by **#402**). When green it **merges**
  (merge-commit, history preserved) into the mini-umbrella.
- The mini-umbrella **#402** accumulates B3-1…B3-4; when the KEYSTONE CI ritual is green it **squashes**
  into the umbrella `remodel/backend-build` (one squashed commit per mini-umbrella, the umbrella BUILD
  field bumped per merge, the seed re-downloaded after AllGreen + Build-Release green before the next
  step).
- The integrator (not the architect) drives the squashes; the verifier runs the gate; agents draft,
  never merge.

---

## 11. The sub-slice decomposition (ordered, each gate-able)

```
B2 (done, #387) ─▶ B3-1 ─────▶ B3-2 ──────▶ B3-3 ────────▶ B3-4         [#388 x86_64 closes]
                   abi_win64    Win64 frame   objfile_coff   target-dispatch
                   + core-      (shadow_space  (COFF writer:  emit_native_win
                   neutrality   field + call   header/sects/  + windows diff
                   proof (RDI/  scan + frame   symtab/strtab/ (native PE run)
                   RSI flip,    reserve; B1    reloc; asm-    + check_coff.sh
                   4-arg win)   goldens hold)  verified)
```

- **B3-1 · `abi_win64.tks` + the `shadow_space` field + core-neutrality proof** — `win64()` descriptor
  + the `win64_*` pool helpers + the additive `AbiDescriptor.shadow_space` field (3 constructors set 0);
  a unit test driving the SHARED `linear_scan` under `win64()` asserting the RDI/RSI-callee-saved flip,
  the 4-arg window, the R10/R11 scratch (§3.2). **Proven by:** the neutrality test + descriptor asserts
  + B1's x86 frame goldens byte-identical (the `shadow_space=0` guardrail). No Win64 frame or object
  code yet.
- **B3-2 · Win64 frame delta** — `func_makes_call_x86` + the `compute_frame_layout_x86` shadow-space
  reservation (gated on `abi.shadow_space>0 && makes_call`) + the slot-offset shift + the
  `B3-xmm-callee-saved` stop (§4). **Proven by:** the `sub rsp,32` shadow frame goldens + the leaf-vs-caller
  gating + every B1 SysV frame golden byte-identical. The instruction encoder is UNTOUCHED.
- **B3-3 · `objfile_coff.tks`** — `emit_coff`: `IMAGE_FILE_HEADER` + section headers + the 18-byte
  symbol table (the name union) + `build_coff_strtab` (size-prefix + inline-≤8) + `IMAGE_RELOCATION`
  (`coff_reloc_type` re-mapping the reused `RelocKindX86`), baking the §5.5 findings; the
  `B3-globals`/`B3-rodata-addend`/`B3-sectaux`/`B3-relocovfl` stops. **Proven by:** header/section/symtab/
  strtab/reloc goldens (assembler-cross-checked) + `llvm-readobj --sections/--symbols/--relocations` / `lld-link` well-formedness
  (machine-free, any host). No e2e.
- **B3-4 · target dispatch + Windows differential (KEYSTONE)** — `NativeTarget::X8664Windows` +
  `target_from_name` + `emit_native_win` (§6.1), `scripts/check_coff.sh`, the `diff_c_own.sh`
  windows-x86_64 lane (§6.2), the `windows-selfhost` CI step. **Proven by:** `own-native == C-native`
  exit codes over the corpus on the windows-x86_64 runner (executing the PE natively) + the macOS
  byte-test lane. **#388 x86_64 CLOSES.**

**Files:** new `src/backend/abi_win64.tks`, `objfile_coff.tks`, and their `*_test.tkt`; new
`scripts/check_coff.sh`; touched `src/backend/abi_aapcs64.tks` (the additive `shadow_space` field —
arm64/riscv/sysv set 0, byte-preserving), `src/backend/encode_x86_64.tks` (`func_makes_call_x86` + the
`compute_frame_layout_x86` shadow reservation — FRAME side-table only, the ISA encoder untouched),
`src/build/project.tks` (`NativeTarget::X8664Windows` + `target_from_name` + `emit_native_win`),
`scripts/diff_c_own.sh` (the windows lane), `.github/workflows/sanitizers.yml` (the `windows-selfhost`
differential step). **Reuses unchanged (the headline):** the WHOLE x86-64 ISA pipeline —
`select_module_x86` (`isel_x86_64.tks`), `regalloc_module_x86`/`inst_regs_x86`/… (`regalloc_x86.tks`),
`encode_module_x86`/`encode_inst_x86`/REX/SIB/ModRM/rel32 + `RelocX86`/`EncodedModuleX86`
(`encode_x86_64.tks`, the instruction encoder), the neutral `MInstX86` surface, the whole
`AbiDescriptor` accessor set + the register-abstract scan core, `Symbol`, the byte-buffer helpers
(`emit_u32_le`/`emit_u64_le`/`emit_str_bytes`/`emit_u8_field`/`pad_to_mult`/`align_up`), and
`finish_native_object`/`build_cc_argv`/`link_object`.

---

## Appendix · The Win64 register-slot argument rule + SSE FP (for the `B3-fp`/`B3-argslot` fast-follow)

Win64's FP arguments are **positionally slot-shared** with the integer arguments: the Nth argument
occupies the Nth register SLOT — an integer arg N takes the Nth int reg (RCX/RDX/R8/R9), a float arg N
takes the Nth XMM (XMM0/1/2/3) — so a mixed `(int, double, int)` signature uses RCX, XMM1, R8 (the
slot INDEX drives the register, NOT a per-class running counter, UNLIKE SysV where int and float args
draw from independent sequences). A **variadic** call passes each float in BOTH the slot's XMM AND the
slot's int reg (the callee may read either). Both are FP-only and ride `B3-argslot` (the integer
corpus never exercises them). The SSE instruction encodings are B1's `A4-fp`/`B1-fp` appendix VERBATIM
(same ISA — ADDSD `F2 0F 58 /r`, CVTSI2SD `F2 REX.W 0F 2A /r`, MOVSD `F2 0F 10 /r`, XORPS-mask FNEG,
…); the ONLY Win64 additions for `B3-fp` are (a) the XMM0-3 arg lowering under the slot rule and
(b) the `movaps [rsp+off], xmm6..15` callee-saved-XMM save/restore in the prologue/epilogue
(`B3-xmm-callee-saved`), re-derived against `clang -target x86_64-windows-msvc -c` before shipping.
