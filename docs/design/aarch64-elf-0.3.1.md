# AArch64-ELF — "deveria existir" (measured design + crumb plan)

**ONE LINE:** **LANE, not wagon** — ~20 edited lines + 7 new declarations, **zero new codegen** (the
ELF writer is already ISA-agnostic and takes `e_machine` + numeric reloc types as parameters), and
**78 of the 80 skips unlock from it** — measured as a differential (80 skips on a simulated arm64
host, 2 on the real x86-64 host, and those same 2 were toolchain-provisioning skips in both — one
of which, the wasm engine, ceased to exist when wasm left the tree, 2026-07-30).

**Status:** DESIGN (doc-only). Branch `cargo/0.3.1-aarch64-elf-desenho`, base
`origin/remodel/0.3.1.0-linux-native-2` (`fab2a759`; `adccc12f` — `teko::arch()` — already drained).
Measured with `.gen1b` built by the mandated ladder
(`scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1` → `TEKO_BACKEND=c .gen1/teko . -o .gen1b
--no-verify --release`; `teko 0.3.0.31-beta`). Every number below is a measurement, and each one
names how it was taken.

---

## 0. Arbitration: the estimate of "a whole AArch64-ELF codegen backend" is wrong

The other agent's conclusion (that unlocking the arm64 leg needs an entire AArch64-ELF codegen
backend) does not survive contact with the source. The requesting measurement is right on every row
it claimed, and right about the shape of the work — **translate relocations we already emit** — with
**one** correction that matters and **two** obstacles it did not see.

| claim under test | verdict | evidence |
|---|---|---|
| isel + encoder AArch64 already exist | **CONFIRMED** | `src/backend/isel_arm64.tks` (1791 ln), `src/backend/encode_arm64.tks` (2529 ln); a full arm64 object is emitted **on this x86-64 host** — see §5.1 |
| ABI AAPCS64 exists | **CONFIRMED** | `src/backend/abi_aapcs64.tks` (269 ln), reached via `teko::backend::AAPCS64` |
| AArch64 relocations exist, but in Mach-O | **CONFIRMED** | `src/backend/objfile_macho.tks:193` `reloc_type_value` |
| `objfile_elf.tks` only knows `EM_X86_64` / `R_X86_64_*` | **CONFIRMED but UNDERSTATED IN OUR FAVOUR** | the writer is **already ISA-agnostic** — §1 |
| `NativeTarget` has no `Arm64Linux` | **CONFIRMED** | `src/build/project.tks:1576` |
| "the work is translating relocations, not inventing codegen" | **CONFIRMED, with one correctness fix** | §2, §3 |

**The one correction (the central risk, and it is a live defect today):** `MRelocKind` is **not** a
sufficient key for the reloc type. `select_func_addr` tags an `ADRP`+`ADD` pair with
`MRelocKind::Call`, and `reloc_type_value(Call)` is unconditionally `ARM64_RELOC_BRANCH26`. A naive
`MRelocKind → R_AARCH64_*` map inherits that. See §2.3 — reproduced, disassembled, and
cross-checked against a real toolchain.

**Two obstacles behind the first**, neither of them codegen: `scripts/check_elf.sh` hardcodes
x86-64 twice (§4.4) and the arm64 test leg has no `no_skips_gate.sh` step at all (§4.5).

Three small factual corrections to the requesting brief are in §8.

---

## 1. The finding that collapses the estimate: the ELF writer is already ISA-agnostic

`src/backend/objfile_elf.tks` is not an x86 writer with x86 constants sprinkled through it. It is a
**neutral ELF64 `ET_REL` writer** with a per-ISA adapter in front, and it says so:

- `pub type ElfObject = struct { e_machine: u32; e_flags: u32; text; rodata; symbols: []Symbol;
  relocs: []ElfRelocReq; rodata_relocs: []ElfRelocReq }` (`:990`)
- `pub fn emit_elf_object(obj: ElfObject): []byte` (`:1052`) — its own doc-comment: *"No ISA
  knowledge remains past this boundary — the per-ISA `emit_elf` adapter supplies the machine, flags,
  and numeric reloc types, so E1 (the ELF static linker) reuses this verbatim."*
- `pub type ElfRelocReq = struct { offset; sym; rtype: u32; addend: i64 }` — **`rtype` is already a
  NUMBER**, not an x86 enum.
- `fn emit_elf_header(buf, lay, e_machine: u32, e_flags: u32, nsects)` (`:758`) — the machine is a
  **parameter**.
- `pub fn emit_elf(enc: EncodedModuleX86): []byte` (`:1136`) is a **13-line adapter**: it fills
  `e_machine = EM_X86_64`, `e_flags = 0`, and maps kinds to numbers.

So the AArch64 ELF object writer is a **sibling adapter**, and `emit_elf_object` /
`emit_elf_header` / `elf_build_symbols` / `elf_build_relas` / `compute_elf_layout` /
`emit_elf_shdrs` are **untouched**.

Two further pieces already exist that the brief did not count in its own favour:

- `src/build/linker.tks` already carries `LinkArch::Arm64` × `LinkFormat::Elf` →
  `gnu_emulation_for` returns **`"aarch64linux"`** (`:95`), with `linker_candidates` and
  `linker_targets_arch` behind it, and a unit test pinning it
  (`src/build/linker_test.tkt:26`).
- `emit_static_archive` (GNU `ar`, `src/backend/objfile_ar.tks:415`) takes `obj_bytes` + `symbols`
  and is container-only — format-agnostic, so it takes an arm64 ELF object unchanged.

---

## 2. The relocation map — measured against a real toolchain, not from memory

### 2.1 The numbers, from the host's own `/usr/include/elf.h` (authoritative)

| symbol | value | meaning |
|---|---|---|
| `EM_AARCH64` | **183** | `elf.h:317` |
| `R_AARCH64_ABS64` | **257** | direct 64-bit (`elf.h:2911`) |
| `R_AARCH64_ADR_PREL_PG_HI21` | **275** | page-rel ADRP imm, bits 32:12 (`elf.h:2929`) |
| `R_AARCH64_ADD_ABS_LO12_NC` | **277** | direct ADD imm, bits 11:0 (`elf.h:2931`) |
| `R_AARCH64_JUMP26` | **282** | PC-rel `B` imm, bits 27:2 (`elf.h:2935`) |
| `R_AARCH64_CALL26` | **283** | PC-rel `BL` imm, bits 27:2 (`elf.h:2936`) |
| `R_AARCH64_LDST64_ABS_LO12_NC` | **286** | LD/ST imm, bits 11:3 (`elf.h:2939`) |

> Recalled-from-memory values for `JUMP26`/`CALL26`/`ADD_ABS_LO12_NC` were **wrong by one** before
> this was measured. Do not take these from memory; they are pinned here against `elf.h` and against
> §2.2's real object.

### 2.2 Independent cross-check: a real `clang --target=aarch64-linux-gnu` object

A reference C file with exactly Teko's four shapes (string in `.rodata`, direct call, function
address stored into a slot, tail call) compiled with `clang 18` produced (`readelf -r`):

```
Machine: AArch64      Flags: 0x0
00000c  ...0113 R_AARCH64_ADR_PREL_PG_HI21   .rodata + 0     <- adrp, string
000010  ...0115 R_AARCH64_ADD_ABS_LO12_NC    .rodata + 0     <- add lo12, string
000018  ...011b R_AARCH64_CALL26             tk_println + 0  <- bl
00001c  ...0113 R_AARCH64_ADR_PREL_PG_HI21   slot + 0
000020  ...0113 R_AARCH64_ADR_PREL_PG_HI21   twice + 0       <- adrp, FUNCTION ADDRESS
000024  ...0115 R_AARCH64_ADD_ABS_LO12_NC    twice + 0       <- add lo12, FUNCTION ADDRESS
00002c  ...011e R_AARCH64_LDST64_ABS_LO12_NC slot + 0
000034  ...011a R_AARCH64_JUMP26             tk_exit + 0
```

`0x113=275`, `0x115=277`, `0x11a=282`, `0x11b=283`, `0x11e=286` — every one agrees with §2.1.

Three things this proves, that presumption would have got wrong:

1. **`e_flags = 0x0`** for AArch64 `ET_REL`. Measured, not assumed.
2. A `.rodata` reference is `ADR_PREL_PG_HI21` + `ADD_ABS_LO12_NC` **against the `.rodata` SECTION
   symbol with the offset on the addend** — which is *precisely* the idiom the existing writer
   already implements for x86 (`ElfRodataHit`, `elf_rodata_secsym_index`, `objfile_elf.tks:387..`).
   Nothing new is needed for rodata.
3. **A function address is `ADR_PREL_PG_HI21` + `ADD_ABS_LO12_NC`, NOT `CALL26`.** This is the
   authoritative confirmation of §2.3.

### 2.3 THE CENTRAL RISK, reproduced: `MRelocKind` alone is the wrong key

`src/backend/isel_arm64.tks:1502`:

```
fn select_func_addr(ctx0: SelCtx, inst: lir::LInst, fa: lir::LFuncAddr): SelCtx {
    select_addr_pair(ctx0, inst, fa.symbol, MRelocKind::Call, MRelocKind::Call)
}
```

`select_addr_pair` emits `MAdrp{kind}` then `MAddLo{kind}`; `encode_arm64.tks:717/729` pass `i.kind`
straight through; `objfile_macho.tks:196` maps `Call → 2` (`ARM64_RELOC_BRANCH26`) unconditionally.
So an `ADRP` and an `ADD` are tagged as a **branch** relocation.

**Reproduced on this host** (`TEKO_TARGET=arm64-macos`, a program taking a function value):

```
$ llvm-readobj --relocations bin/fa.o
    0x28 1 2 1 ARM64_RELOC_BRANCH26 0 _.Lclofn0
    0x2C 1 2 1 ARM64_RELOC_BRANCH26 0 _.Lclofn0

$ llvm-objdump -d --section=__text bin/fa.o
      28: 90000001   adrp x1, 0x0
      2c: 91000021   add  x1, x1, #0x0
```

Two consecutive words — an `adrp` and an `add` — both carrying `BRANCH26`. A linker honouring that
writes a 26-bit displacement into bits 25:0 of the `ADRP` word, corrupting the page immediate **and
the `Rd` field**. This is **exactly the failure class the brief named**: it links and computes
garbage at runtime, silently.

This is a **pre-existing defect on the shipped `arm64-macos` path**, not something AArch64-ELF
introduces. It is reachable from the corpus: `examples/regressions/own_native/src/corpus.tks:1635`
(`rd_tick_fn` returns a lambda) lowers through `LFuncAddr`.

**Resolution — law-first, no HALT.** Constitution M.3 (never a fabricated artifact) forbids shipping
an adapter that knowingly emits a relocation the linker will mis-apply, and §2.2's real toolchain
settles what the correct relocation *is*. The fix is one line, at the source, and it fixes **both**
formats:

```
/**
 * select_func_addr — the `LFuncAddr` selection: the SAME `ADRP + ADD` page pair
 * a global address uses (`PageHi`/`PageLo`). A function's address is an
 * ADDRESSING form, not a branch: the relocation describes the instruction whose
 * immediate is patched, never the section the target lives in. Measured against
 * a real toolchain (`docs/design/aarch64-elf-0.3.1.md` §2.2): `clang
 * --target=aarch64-linux-gnu` emits `R_AARCH64_ADR_PREL_PG_HI21` +
 * `R_AARCH64_ADD_ABS_LO12_NC` for a function address, and reserves `CALL26` for
 * a `BL`. Tagging the pair `Call` (as this did until now) made the Mach-O writer
 * stamp `ARM64_RELOC_BRANCH26` onto an `ADRP` and an `ADD` — a relocation that
 * LINKS and then corrupts the page immediate and the `Rd` field at runtime.
 *
 * @param ctx0  the context to extend
 * @param inst  the enclosing instruction (its result VReg)
 * @param fa    the function-address op
 * @return      the advanced context
 */
fn select_func_addr(ctx0: SelCtx, inst: lir::LInst, fa: lir::LFuncAddr): SelCtx {
    select_addr_pair(ctx0, inst, fa.symbol, MRelocKind::PageHi, MRelocKind::PageLo)
}
```

**Blast radius, measured:** no Mach-O byte golden pins the bad shape. `objfile_macho_test.tkt:171`
already builds its `adrp`/`add-lo` pair with `PageHi`/`PageLo`. Only three test sites assert the
`Call`-on-`ADRP` spelling and need their expectation moved:
`src/backend/isel_arm64_test.tkt:350` (`"adrp.call %0, @teko__demo__f"` → `"adrp.page_hi ..."`),
`src/backend/minst_test.tkt:106-107` (printer), and nothing in `encode_arm64_test.tkt`
(`:226` pins a default kind on a non-reloc word and is unaffected).

After the fix, `MRelocKind` **is** a total, correct key, and `select_func_addr` becomes
byte-identical to `select_global_addr` — cyclomatic complexity down, not up.

### 2.4 The map to implement

| `MRelocKind` | patch site | Mach-O today | **AArch64 ELF** | appears in a real compile? |
|---|---|---|---|---|
| `PageHi` | `ADRP` imm 32:12 | `ARM64_RELOC_PAGE21`(3) | **`R_AARCH64_ADR_PREL_PG_HI21` = 275** | **yes** (measured, §5.1) |
| `PageLo` | `ADD` imm 11:0 | `ARM64_RELOC_PAGEOFF12`(4) | **`R_AARCH64_ADD_ABS_LO12_NC` = 277** | **yes** (measured) |
| `Call` | `BL` imm 27:2 | `ARM64_RELOC_BRANCH26`(2) | **`R_AARCH64_CALL26` = 283** | **yes** (measured) |
| `Abs64` | 8-byte data slot in `.rodata` | `ARM64_RELOC_UNSIGNED`(0), len 3 | **`R_AARCH64_ABS64` = 257** | **no** — live code (`rodata_relocs_arm64`, `encode_arm64.tks:2473`) but empty in every compile today |

**Not needed, and this is a measurement, not an assumption.** No `R_AARCH64_LDST*_ABS_LO12_NC` is
required. `encode_arm64.tks:32` documents that only `BL`/`ADRP`/`ADDLo` carry relocations, and
`grep` confirms `MLoad`/`MStore` are built by `load_inst`/`store_inst`, which take no symbol. **This
is the one invariant that will silently rot**: the day a peephole folds an `add lo12` into an
`ldr [x, #lo12]`, `ADD_ABS_LO12_NC` on a load scales the immediate wrong (bits 11:0 vs 11:3) and
produces exactly the link-and-fail-at-runtime bug. Crumb 2's fixture pins it (§6).

---

## 3. What AArch64 ELF requires that x86-64 does not — measured

| dimension | requirement | already satisfied? |
|---|---|---|
| `e_machine` | 183 | **no** — the one new constant |
| `e_flags` | **0x0** (measured, §2.2) | **yes** — the adapter passes `0` exactly like x86 |
| instruction alignment | 4 bytes | **yes** — `.text` `sh_addralign` is already **16** (`objfile_elf.tks:858`) and every section offset is `align_up(..., 8)` (`:697-707`) |
| section set | `.text`/`.rodata`/`.symtab`/`.strtab`/`.shstrtab`/`.rela.text` | **yes** — identical; nothing arch-specific |
| symbol `st_value` | bare section-relative | **yes** — already the ELF convention (`emit_elf_object` doc) |
| `.eh_frame` / unwind | **not required** for a non-unwinding `ET_REL`; the reference object built clean with `-fno-unwind-tables` and `ld.lld -r` accepted it | **yes** — nothing to add |
| `.note.GNU-stack` | absent from the object → a `ld` *warning*, on **both** arches | **arch-independent**; the x86-64 lane already ships without it. Report-up, not this lane's scope |
| symbol name prefix | ELF uses bare names (Mach-O prefixes `_`) | **yes** — `EncodedModule.symbols` carry unprefixed names (`objfile_macho.tks:174` adds the `_`); the ELF path uses them bare |
| AArch64 `$x`/`$d` mapping symbols | aaelf64 §4.5.4 defines them; **optional** for linking — the reference object links and `ld.lld -r`s without any | **not required.** Noted as a disassembly-fidelity nicety, explicitly out of scope |

**Net: one new constant.** No alignment change, no new section, no unwind data, no header flags.

---

## 4. The honest count of sites to change

### 4.1 `src/backend/objfile_elf.tks` — 5 new declarations, **0 edits**

```
/**
 * EM_AARCH64 — the `Elf64_Ehdr.e_machine` value identifying an AArch64 (arm64)
 * object. The AArch64 LP64 ABI defines no processor-specific `e_flags`, so the
 * adapter passes `0` exactly as the x86-64 one does — measured against a
 * `clang --target=aarch64-linux-gnu` reference object, whose header reads
 * `Machine: AArch64  Flags: 0x0` (`docs/design/aarch64-elf-0.3.1.md` §2.2).
 *
 * @since 0.3.1
 */
const EM_AARCH64: u32 = 183 to u32

/**
 * elf_reloc_type_arm64 — the `R_AARCH64_*` relocation type for an `MRelocKind`,
 * the AArch64 sibling of `elf_reloc_type` and the ONLY AArch64 knowledge this
 * adapter injects into the ISA-agnostic writer.
 *
 * Every value is pinned against `/usr/include/elf.h` AND against a real
 * `clang --target=aarch64-linux-gnu` object (§2.1/§2.2), because a wrong number
 * here produces an object that LINKS and then computes garbage — the failure
 * class this whole design is arranged around:
 *
 *   `PageHi` → `R_AARCH64_ADR_PREL_PG_HI21`(275)  the ADRP page immediate, 32:12
 *   `PageLo` → `R_AARCH64_ADD_ABS_LO12_NC`(277)   the ADD low-12 immediate, 11:0
 *   `Call`   → `R_AARCH64_CALL26`(283)            the BL displacement, 27:2
 *   `Abs64`  → `R_AARCH64_ABS64`(257)             an 8-byte pointer slot in .rodata
 *
 * `PageLo` is `ADD_ABS_LO12_NC` and NEVER an `LDST*_ABS_LO12_NC`, because isel
 * only ever attaches a relocation to `MAdrp`/`MAddLo`/`MCall` — `load_inst` and
 * `store_inst` take no symbol (`encode_arm64.tks:32`). A future peephole that
 * folded an `add lo12` into an `ldr [x, #lo12]` would need its OWN kind: the
 * load form scales the immediate by the access size (bits 11:3 for a 64-bit
 * load), so reusing `ADD_ABS_LO12_NC` there would mis-scale silently.
 * `elf_arm64_reloc_kinds_are_add_form_only` pins that invariant.
 *
 * @param MRelocKind kind  the relocation kind isel pre-tagged
 * @return u32  the `Elf64_Rela` `r_info` type field
 * @since 0.3.1
 */
fn elf_reloc_type_arm64(kind: MRelocKind): u32 {
    match kind {
        PageHi => 275 to u32
        PageLo => 277 to u32
        Call => 283 to u32
        Abs64 => 257 to u32
    }
}
```

plus `arm64_reloc_reqs(enc: EncodedModule): []ElfRelocReq`,
`arm64_rodata_reloc_reqs(enc: EncodedModule): []ElfRelocReq` (the `RelocSect::Text` /
`RelocSect::Rodata` partition, structurally identical to `x86_reloc_reqs` /
`x86_rodata_reloc_reqs` at `:1094`/`:1115`), and:

```
/**
 * emit_elf_arm64 — the AArch64 ELF adapter, the exact sibling of `emit_elf`:
 * map an arm64 `EncodedModule` to a neutral `ElfObject` (`e_machine` =
 * `EM_AARCH64`, `e_flags` = 0, every `Reloc` lowered to an `ElfRelocReq` via
 * `elf_reloc_type_arm64`) and hand it to the shared, ISA-agnostic
 * `emit_elf_object`. The writer is untouched — its own contract already says no
 * ISA knowledge lives past that boundary — so the x86-64 goldens stay
 * byte-for-byte green and `arm64-macos` is not on this path at all.
 *
 * @param EncodedModule enc  the section images + symbols + relocations
 * @return []byte  the ELF64 `ET_REL` object file bytes
 * @since 0.3.1
 */
pub fn emit_elf_arm64(enc: EncodedModule): []byte {
    emit_elf_object(ElfObject {
        e_machine = EM_AARCH64
        e_flags = 0 to u32
        text = enc.text
        rodata = enc.rodata
        symbols = enc.symbols
        relocs = arm64_reloc_reqs(enc)
        rodata_relocs = arm64_rodata_reloc_reqs(enc)
    })
}
```

No `use` is needed: all of `src/backend/` is one namespace (`objfile_macho.tks` names `MRelocKind` /
`EncodedModule` / `Symbol` with no import).

### 4.2 `NativeTarget` and its dispatch — `src/build/project.tks`, 12 edits + 2 new fns

`NativeTarget` is **private** (`type`, not `pub type`, `:1576`), so the blast radius is one file plus
one `target_name` call in `src/build/tkr.tks:1246`.

| # | site | line | change |
|---|---|---|---|
| 1 | `NativeTarget` enum | 1576 | `+ Arm64Linux` |
| 2 | `default_cc_for_target` | 743 | `Arm64Linux => "cc"` |
| 3 | `target_from_name` | 1618 | `"arm64-linux"` (canonical, ruling #390 os-based) + `"aarch64-linux"` alias (what `cc -dumpmachine` says) |
| 4 | `unsupported_target_error` | 1583 | add the new names to the supported-set message (D39 single source) |
| 5 | **`host_target_for_os`** | **1679** | **`if os == "linux" && arch == "arm64" { return NativeTarget::Arm64Linux }`** — this ONE line is what unlocks 78 of the 80 skips (§5.2) |
| 6 | `target_name` | 1766 | `Arm64Linux => "arm64-linux"` |
| 7 | `target_objfmt` | 1800 | `Arm64Linux => OBJFMT_ELF` |
| 8 | `target_os_name` | 1835 | `Arm64Linux => "linux"` |
| 9 | `target_arch_name` | 1861 | `Arm64Linux => "arm64"` |
| 10 | `emit_native` dispatch | 2070 | `Arm64Linux => emit_native_arm64_linux(...)` |
| 11 | `emit_static_lib` dispatch | 2245 | `Arm64Linux => emit_static_lib_arm64_linux(...)` — **mandatory, not optional**: without it an arm64 `.a` request has no arm to land on |
| 12 | new fn | — | `emit_native_arm64_linux` |
| 13 | new fn | — | `emit_static_lib_arm64_linux` |

`host_target_for_os_guess` (`:1651`, OS-only) needs **no** change: `host_default_target_guess`
(`:1713`) prefers the precise resolver and only falls back on failure, so once `host_target_for_os`
answers for `linux`+`arm64`, `cross_note` correctly reads `arm64-linux` as **not cross** on an arm64
host. Measured by reading the fallback, and it is what keeps a second skip family from opening.

The two new tails are 5 lines each and differ from `emit_native_arm64` / `emit_static_lib_x86` in
exactly one call:

```
/**
 * emit_native_arm64_linux — the arm64/Linux-ELF own-backend tail: the SAME
 * pipeline as `emit_native_arm64` (platform-ABI entry wrap → arm64 isel →
 * AAPCS64 regalloc → arm64 encode) with the object format swapped from Mach-O to
 * ELF (`emit_elf_arm64`). One call differs; nothing else may, or the two arm64
 * paths drift and the Mach-O differential stops meaning anything.
 *
 * @param dir   the project directory (for diagnostics)
 * @param od    the slash-stripped output directory (already created)
 * @param stem  the output artifact name (the manifest `name`)
 * @param lmod  the target-independent lowered module
 * @param prog  the checked program (extern-reachability, `.tsym`)
 * @param m     the resolved manifest (link knobs)
 * @return      0 on a successful build+link, else the failing status
 */
fn emit_native_arm64_linux(dir: str, od: str, stem: str, lmod: teko::lir::LModule, prog: checker::TProgram, m: Manifest): i32 {
    let entry = match teko::lir::wrap_native_entry(lmod) { teko::lir::LModule as x => x; error as e => return fail(dir, e.message) }
    let sel = match teko::backend::select_module(entry) { teko::backend::MModule as x => x; error as e => return fail(dir, e.message) }
    let col = match teko::backend::regalloc_module(teko::backend::AAPCS64, sel) { teko::backend::MModule as x => x; error as e => return fail(dir, e.message) }
    let enc = match teko::backend::encode_module(teko::backend::AAPCS64, col) { teko::backend::EncodedModule as x => x; error as e => return fail(dir, e.message) }
    finish_native_object(dir, od, stem, teko::backend::emit_elf_arm64(enc), prog, m)
}
```

### 4.3 Linker invocation — **0 changes**, measured

`link_object` → `build_cc_argv`. The only OS-conditional flag is
`append_macos_plist_flag` (`:1306`), which returns `argv` unchanged unless the **emit OS** is
`"macos"`; `target_os_name(Arm64Linux)` is `"linux"`, so it is a no-op. `default_cc_for_target` is
`"cc"`, and on the arm64 runner the host `cc` is an aarch64 `cc`. No `-arch` / `-target` / `-m`
flag is emitted anywhere on this path. (Corollary, measured on this host: the `-sectcreate` failure
in §5.1 is the *Mach-O* path on a Linux host, and is not on the arm64-Linux path.)

### 4.4 `scripts/check_elf.sh` — 2 edits + 1 new argument (**obstacle #2**)

```
line 20:  if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then ... exit 0
line 41:  echo "$hdr" | grep -qi "X86-64"  || fail "not an EM_X86-64 object"
```

On an arm64 runner (`uname -m` = `aarch64`) line 20 prints `check_elf: skipped` and **exits 0**, and
`check_object_wellformed` (`src/build/regression.tks:948`) treats exit 0 as `regr_pass()`. So
`Then object well-formed` would **pass vacuously** on the arm64 leg — a green over an unexecuted
check, which the owner's own no-skips ruling names as the worst shape. Fix: accept `aarch64` in the
host gate, and take the expected os-arch as `$2` so the machine assertion is **parametric instead of
constant** (which also closes the same hole in the x86 direction). `check_object_wellformed` passes
`target` as that second argument — a 1-line change there.

`wellformed_script_for` (`regression.tks:926`) needs **no** change: `"arm64-linux"` contains neither
`windows` nor `macos`, so it already falls through to `check_elf.sh`.

### 4.5 The rest

- **`no_skips_gate.sh` is not wired into the arm64 leg at all.** It runs in exactly one `pr.yml`
  job — `regressor-full` (`:1450`), on `ubuntu-latest`/x86_64. `test-linux-arm64-glibc`
  (`:866`, `runs-on: ubuntu-24.04-arm`) just runs `teko test .`. That is why the leg is green over
  80 skips: nothing there is looking. **Sequencing consequence:** wiring the gate into the arm64
  leg *before* `Arm64Linux` lands turns the leg red. It must land **after**, in the same lane
  (crumb 5).
- **Unit-test sites to extend** (found by *running* them, §5.2): `src/build/project_test.tkt` —
  `host_target_for_os_is_precise_for_every_supported_pair` (`:117`),
  `host_target_for_os_rejects_every_unsupported_pair` (`:135`, must **drop**
  `("linux","arm64")` and keep the other four), `host_default_target_matches_the_real_host`
  (`:170`), plus the `target_from_name` / `target_name` / `objfmt` / `os` / `arch` row sets.
- **`src/build/tkr.tks:1246`** — reads `target_name(t)`; no change (`HOST_TARGET` fallback path).
- **`src/build/linker.tks`** — no change; already has `(Arm64, Elf)`.

**Totals: ~20 edited lines, 7 new declarations, 5 test sites, 1 shell script, 1 CI step. Zero new
codegen. Zero lines touched in `emit_elf_object` or any encoder.**

---

## 5. The measurements

### 5.1 arm64 emission works on this x86-64 host today

```
$ TEKO_TARGET=arm64-macos TEKO_BACKEND=native .gen1b/teko . -o bin --no-verify
  ... lexer ✓ parser ✓ checker ✓ monomorph ✓ consteval ✓
  /usr/bin/ld: Error: unable to disambiguate: -sectcreate
  teko: .: cc failed to link the own-backend object
$ ls bin/ -> aarch.o (758 bytes)
$ llvm-readobj --relocations bin/aarch.o
  Format: Mach-O arm64   Arch: aarch64
    0x1C ARM64_RELOC_PAGE21      _.Lstr0
    0x20 ARM64_RELOC_PAGEOFF12   _.Lstr0
    0x30 ARM64_RELOC_BRANCH26    _tk_println
    0x38 ARM64_RELOC_BRANCH26    _tk_exit
    0x70 ARM64_RELOC_BRANCH26    _tk_set_args
    0x74 ARM64_RELOC_BRANCH26    ___teko_native_vmain
```

The entire arm64 pipeline runs to a finished object; **only the link fails**, and only because the
host `ld` is not Apple's. The relocation census of a real compile is exactly **3 kinds**:
`PageHi`, `PageLo`, `Call`. `Abs64` never appears.

### 5.2 The 80 skips: exactly reproduced, and 78 of them have one cause

An arm64-Linux host is faithfully simulated by making `("linux","x86_64")` resolve to no
own-backend target — the skip predicates are a pure function of that resolver plus
`cc -dumpmachine`. Applied to a scratchpad copy, rebuilt with `.gen1b`, then `teko test .` (no
`REGRESSION_REQUIRE_TOOLS`, which is exactly what the arm64 leg runs):

```
teko: regressions 10 run, 80 skipped, 0 failed        exit 0   <- green over 80 skips
```

**80 — an exact match for the reported "~80 linhas saltadas", which validates the simulation.**
Cause distribution over all 80:

| count | reason |
|---|---|
| **78** | `this row needs the own backend's host-default target, but this host has none: unsupported TEKO_TARGET …` |
| 1 | `wasmtime not found on PATH for target wasm32-wasi` |
| 1 | `x86_64-w64-mingw32-gcc (the cross-linker for target x86_64-windows) not found on PATH` |

Per-file: `own_native.tkr` **60**, `regressor.tkr` **16**, and 1 each in
`native_union_known_stop`, `const_struct_ctor`, `crossmodule`, `builtin_name_not_hijacked`.

**The control, which makes 78 a differential rather than an inference.** The same corpus, same
`.gen1b`, same command, on the **unmodified** tree and the real x86-64 host:

```
teko: regressions 10 run, 2 skipped, 0 failed (30 builds, 479.0s)
  1  wasmtime not found on PATH for target wasm32-wasi
  1  x86_64-w64-mingw32-gcc (the cross-linker for target x86_64-windows) not found on PATH
```

**2 skips on x86-64, 80 on arm64, and the 2 are byte-identical in both runs.** The
arch-attributable delta is therefore **exactly 78**, measured from both sides rather than inferred
from one.

The 78 all come from one predicate chain:
`host_cc_cannot_link_host_default_reason` → `own_backend_target_of_row("")` →
`host_default_target()` → `host_target_for_os("linux","arm64")` → **error** →
`host_default_unsupported_skip_reason`. **The one line in §4.2 row 5 removes the error, and all 78
unlock.** The `own_native.tkr` 60 unlock as a block because it is a project `.tkr` — one build for
the whole file.

The other 2 are **not an aarch64 problem**. They are the same two capability rows the x86_64
`regressor-full` job resolves by *provisioning* plus `REGRESSION_REQUIRE_TOOLS=1`. Reaching zero
skips on the arm64 leg needs the same provisioning **there**. One of the two rows — the wasm one —
disappeared outright when wasm left the tree (2026-07-30), so only the cross-linker row remains.

### 5.3 Second obstacle, and there is no third

After the 78 unlock, the remaining exposure was searched for deliberately, since that is where the
previous estimate failed. Two things were found and both are in §4:

1. `check_elf.sh` skips-and-passes on aarch64 (§4.4) — honesty, not green.
2. `no_skips_gate.sh` is absent from the arm64 leg (§4.5) — sequencing.

Explicitly checked and **clear**: no hardcoded `x86_64-linux` target anywhere in the corpus (the
full `Given target` tally is 3× `x86_64-solaris`, 1× `x86_64-windows`, 1×
`host-os-arch` — the `host-os-arch` token, added by the 2026-07-27 ruling, already removed that
family); `arch_token_canonical` already folds `aarch64`→`arm64` (`regression.tks:711`) so
`host_cc_arch()` on an arm64 runner agrees with `target_arch_name(Arm64Linux)`;
`cross_cc_for_target("arm64-linux")`
returns `""`; `wellformed_script_for` already routes to ELF.

---

## 6. Crumb sequence — each step independently gate-able

Every crumb compiles and is green on its own. No crumb uses a language feature absent from the
`0.3.0.31-beta` seed (plain enums, matches, functions only).

**Crumb 1 — the reloc-kind correctness fix, alone.** `select_func_addr` → `PageHi`/`PageLo`
(§2.3). Update `isel_arm64_test.tkt:350` and `minst_test.tkt:106-107`. Add
`arm64_func_addr_pair_is_a_page_pair_not_a_branch`: build an `LFuncAddr` module, encode, assert the
two `Reloc`s are `PageHi`/`PageLo`; and a Mach-O golden asserting `r_type` 3 then 4 (never 2) on the
`ADRP`/`ADD` words. **Ships a fix to the shipped `arm64-macos` path, before anything depends on it.**
No ELF, no new target.

**Crumb 2 — `emit_elf_arm64`, pure emitter, no target.** The 5 declarations of §4.1. Fixtures:
a golden `ElfObject` byte test (`e_machine` 183, `e_flags` 0, `.rela.text` rows 275/277/283/257);
`elf_arm64_reloc_kinds_are_add_form_only` — assert `elf_reloc_type_arm64(PageLo) == 277` **and**
that no `MRelocKind` maps to any `LDST*_ABS_LO12_NC` value (278/284/285/286/299), so the §2.4
invariant fails loudly the day a load carries a relocation; and an x86 no-regression assertion that
`emit_elf` bytes are unchanged. Unreachable from any target yet — pure addition.

**Crumb 3 — `NativeTarget::Arm64Linux` + the 12 dispatch arms + the 2 tails** (§4.2), **including**
`host_target_for_os`. Extend the `project_test.tkt` sites of §4.5. **This is the crumb that
unlocks the 78** and the first that changes the arm64 leg's behaviour.

**Crumb 4 — `check_elf.sh` parametric** (§4.4) + the 1-line `check_object_wellformed` change. Add
`scripts/check_elf.sh`'s own self-check to the `CI gate` alongside `no_c_in_tests_gate_test.sh`.

**Crumb 5 — CI, last.** Add to `test-linux-arm64-glibc`: `tee teko-test.log`,
`REGRESSION_REQUIRE_TOOLS=1`, `gcc-mingw-w64-x86-64`, and
`sh scripts/no_skips_gate.sh teko-test.log`. **Strictly after crumb 3**, or the leg goes red on the
78 (§4.5).

**Minimum subset that makes `linux-arm64-glibc` emit, link and RUN:** crumbs **1 + 2 + 3**. Crumb 3
alone is not enough (no ELF emitter); crumbs 2+3 without 1 would emit an `ADRP`+`ADD` pair as
`CALL26` and ship the §2.3 defect into ELF. Crumbs 4 and 5 are honesty and enforcement, not
capability.

### Regression fixtures (inputs → expected exit codes)

| fixture | where | interp | native |
|---|---|---|---|
| `arm64_func_addr_pair_is_a_page_pair_not_a_branch` | `src/backend/isel_arm64_test.tkt` | 0 | n/a (unit) |
| `macho_func_addr_relocs_are_page21_pageoff12` | `src/backend/objfile_macho_test.tkt` | 0 | n/a (unit) |
| `elf_arm64_object_header_and_relas_are_golden` | `src/backend/objfile_elf_test.tkt` | 0 | n/a (unit) |
| `elf_arm64_reloc_kinds_are_add_form_only` | `src/backend/objfile_elf_test.tkt` | 0 | n/a (unit) |
| `elf_x86_bytes_are_unchanged_by_the_arm64_adapter` | `src/backend/objfile_elf_test.tkt` | 0 | n/a (unit) |
| `host_target_for_os_*` (3 sites extended) | `src/build/project_test.tkt` | 0 | n/a (unit) |
| `arm64_linux_target_row_set` (name/objfmt/os/arch) | `src/build/project_test.tkt` | 0 | n/a (unit) |
| `own_cross_arm64_linux_emits_elf` — `Given target = "arm64-linux"`, `Given source = "cases/cross_exit42.tks"`, `When built`, `Then object well-formed` | `examples/regressions/own_native/own_native.tkr` | — | exit **0** on x86_64 (object is the claim, mirroring `own_cross_x86_64_windows_emits_coff`); on the arm64 leg it is the host row and runs |
| the existing 78 rows | unchanged | — | exit as already declared (mostly **42**) — they *unlock*, they are not rewritten |

### Ritual points (the full gate must pass)

- **After crumb 1** — full gate + `test-macos-arm64` + `ar-validation-macos-arm64`: crumb 1 is the
  only crumb that changes bytes on a **shipped** path.
- **After crumb 3** — full gate + the whole arm64 pair (`test-linux-arm64-glibc`,
  `test-linux-arm64-musl`) + `cross-arch-determinism` (its `PINNED_TARGET="x86_64-linux"` keeps the
  four-asset comparison meaningful, but the four assets now *contain* a new target and must still
  agree byte-for-byte) + `regressor-full`.
- **After crumb 5** — full gate; the arm64 leg must report **0 skipped**.
- Fixpoint: `linux-arm64-glibc` is `fixpoint_backend=c` and `linux-arm64-musl` is **`native`**
  (`scripts/ci_producer_matrix.sh`). The musl arm64 leg therefore self-hosts natively — after crumb
  3 it is the first leg to compile the compiler with `emit_elf_arm64`. Treat it as the real ritual.

---

## 7. Risks and law tensions

| risk | severity | resolution |
|---|---|---|
| **Wrong reloc number → links, fails at runtime** | the one that matters | Every value pinned twice: `/usr/include/elf.h` **and** a real `clang --target=aarch64-linux-gnu` object (§2.1/§2.2). Not from memory — three recalled values were wrong. |
| **`PageLo` on a future `ldr` → mis-scaled immediate** | high, latent | `elf_arm64_reloc_kinds_are_add_form_only` (crumb 2) + the `elf_reloc_type_arm64` doc-comment. The invariant is asserted, not assumed. |
| **Crumb 1 changes shipped Mach-O bytes** | medium | No existing byte golden covers the bad shape (measured, §2.3): only 3 printer/spelling assertions move. Ritual on `test-macos-arm64` immediately after. |
| **Crumb 5 before crumb 3 turns the leg red** | medium | Sequencing is explicit; crumb 5 is last. |
| **`Abs64` is untested by any real compile** | low | Live code (`rodata_relocs_arm64`), empty today. Mapped for correctness and covered by a hand-built unit module — exactly the discipline the x86 side already applies to its `Abs64`. |
| **`emit_static_lib`'s dispatch has no arm64 arm** | low but dishonest (M.3) | Crumb 3 adds the arm; it is listed as mandatory, not optional. |

**Law tensions — resolved law-first, no HALT.**

1. *Is fixing `select_func_addr` in scope, when the issue says "AArch64-ELF"?* **Yes.** "Issues are
   100%" plus M.3 (never a fabricated artifact): an adapter that knowingly emits a relocation the
   linker mis-applies does not constitute AArch64-ELF *existing*. And the alternative — an honest
   stop in the adapter — is **not implementable**: the `Reloc` record carries no instruction-shape
   field, so the adapter structurally cannot see that a `Call` sits on an `ADRP`. Fixing the source
   is the only law-compatible option, and it is one line. Isolated as crumb 1 so the owner can
   detach it if he prefers.
2. *Canonical name — `arm64-linux` or `aarch64-linux`?* **`arm64-linux`** canonical: ruling #390
   (targets named by OS, format never) and `target_arch_name`'s existing vocabulary
   (`Arm64Macho → "arm64"`) both say `arm64`. `aarch64-linux` is accepted as a spelling alias, since
   `cc -dumpmachine` says `aarch64` and `arch_token_canonical` already folds the two.
3. *Does `check_elf.sh` deserve a known-stop for the aarch64 gap?* **No, and none is minted here.**
   Per the owner's ruling that known-stops are negotiated between owner and requester, this is only
   flagged. It closes with a `uname -m` clause and an argument, not a compiler change — pinning it
   would enshrine a CI defect in the shape reserved for compiler defects, the exact argument
   `pr.yml:48-52` already makes about the mingw line.

---

## 8. Corrections to the requesting brief, and adjacent findings (reported, not turned into issues)

**Corrections.**

1. `test / linux-arm64-glibc` runs on **`runs-on: ubuntu-24.04-arm` — real arm64 hardware**, not
   qemu (`pr.yml:870`). The `qemu-aarch64-static` provisioning is at **`pr.yml:1721-1724`**, in the
   separate `cross-arch-determinism` job, not `1026-1055`.
2. The brief's premise that the leg is green over ~80 skips is **exactly right** (measured: 80,
   exit 0), but the reason nothing catches it is that **`no_skips_gate.sh` never runs on that leg**
   — it is wired only into `regressor-full` (`pr.yml:1450`, x86_64).
3. `objfile_elf.tks` "only knows `EM_X86_64` / `R_X86_64_*`" understates the position **in the
   brief's favour**: the writer is already ISA-agnostic and parameterised on `e_machine` +
   numeric `rtype`. Only the 13-line *adapter* is x86-specific.

**Adjacent findings, reported up.**

- **`arm64-macos` mis-relocates every function address** (§2.3) — a shipped-path defect,
  reproduced and disassembled. Independent of AArch64-ELF; crumb 1 fixes it.
- **`check_object_wellformed` treats a leaf script's honest-skip (exit 0) as a PASS**
  (`regression.tks:948` + `check_elf.sh:20`). A green over an unexecuted check, on any host the
  leaf script declines. Structural, affects all three formats.
- **How to prove AArch64-ELF without arm64 hardware — measured on this host.** Provable by
  emission and bytes: object emission end-to-end (§5.1), `readelf -h/-r/-S` (cross-capable), `nm`
  (cross-capable), `llvm-objdump -d` (cross-capable), `llvm-readobj -r`, and **`ld.lld -r` accepts
  an aarch64 object (exit 0)** — so even linker-consumability is testable here. Needs real hardware
  or qemu: **execution only**. Two host tools are *not* cross-capable and must not be relied on for
  a cross check: GNU `ld -r` (`ld: ref.o: Relocations in generic ELF (EM: 183)`) and GNU
  `objdump -d` (parses the header, disassembles nothing). Since the arm64 test leg has native
  aarch64 binutils, CI needs **no** cross tooling — only §4.4's `uname -m` clause.
