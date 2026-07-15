# E1 / L1 — the own ELF static linker (issue #392): crumb sequence

**Status:** PLAN (doc-only). Lane **#406** `fix/issue-392-elf-linker` (E1). Design-ahead
KEYSTONE/L: turns issue **#392** into an ordered, independently gate-able crumb sequence so
implementers drain sub-PRs into the lane. This doc builds NO product code.

**Base for the design:** `docs/design/own-backend-architecture.md` §3.5–3.7, §4 Phase E, §5 D-2/D-5;
the **#392 owner comments** (2026-07-10 musl-archive ruling; 2026-07-11 entry-point ruling) relayed
by the integrator; the **#182** toolchain-bundle infra; and the live code paths. E1 is the recon's
**#226 M-linker**, re-homed as **#392**.

> **Revision note (2026-07-15).** This supersedes the first draft. The integrator relayed #392's 4
> comments (the API was blocked to me). They reshape E1 to **musl-static via bundled archives** (not
> a freestanding syscall stub), add the **`teko link` CLI + foreign `.o`/`.a` inputs**, the
> **link-optimization flags**, **linker-owned entry-point**, and **honest reloc/format validation**.
> One premise in the brief is **corrected with evidence** (§10): there is **no `read_file_bytes` seed
> wall** — `read_file` already reads binary. See §10 and §13 for the two forks I return to the owner.

---

## 1. Scope, reconciled to the owner comments + the code

### 1.1 What E1 is

E1 is the **static** ELF linker and the **`teko link`** driver command. It:
1. accepts **N pre-compiled inputs** — object files (`.o`) and static archives (`.a`), **own AND
   foreign** (clang/gcc/zig), when their **format + arch** match the target;
2. **resolves symbols** across all inputs (with lazy archive-member pull), garbage-collects dead
   sections, applies relocations, lays out load segments, **sets the entry point itself**, and writes
   a **runnable static ELF executable** (`Elf64_Ehdr` + `Elf64_Phdr`);
3. carries **link-optimization** passes (`--prune-dead`, `--strip`, rodata-merge, ICF) in the
   format-agnostic core, each reporting a **binary-size delta**;
4. **replaces `cc`-as-linker** (`src/build/project.tks:737` `link_object`) on the ELF targets.

The own backend already emits well-formed ELF `ET_REL` objects and the `linux-x86_64`/`riscv64`
own==C differential is green through `cc`-as-linker (`emit_native_x86` `project.tks:1113`,
`emit_native_riscv` `:1171`; `scripts/diff_c_own.sh`). **E1 REPLACES the linker; it does not touch the
object emitter.** The ISA-agnostic emitter boundary (`objfile_elf.tks:840` `emit_elf_object`, whose
doc already says *"E1 (the ELF static linker) reuses this unchanged"*) is the own-object input door.

### 1.2 The Linux static/dynamic matrix (owner ruling 2026-07-10)

| Lane | Linkage | Runtime provider | Owner assignment |
|------|---------|------------------|------------------|
| **musl** | **STATIC** | bundled `crt1.o`/`crti.o`/`crtn.o` + `libc.a` (per arch, #182) | **E1** |
| **glibc** | **DYNAMIC** | `ld-linux.so` + `libc.so.6` | **E1-core + E3 (#394)** |

**musl-static is E1**: E1 links the **REAL, libc-dependent** `teko_rt` (`teko_rt.c:13-41` — `malloc`,
`stdio`, `unistd`, …) against the **pre-bundled per-arch musl archives**, reusing the **#182** bundle
infra. **Correct link order + the start-symbol set are an explicit E1 deliverable** (§11). The
**glibc lane is dynamic and deferred to E3** — glibc has no sane full-static link, so it needs the
dynamic section / `PT_INTERP` / GOT-PLT machinery that #394 owns. **The differential runs the musl
lane first.** The freestanding-syscall-stub design of the prior draft is **dropped**.

### 1.3 The E-cluster target-matrix map (owner ruling — the durable reference)

The owner's **definitive emit+own-link target matrix** (recorded verbatim). Each row is a
(format, arch) the own backend both **emits an object for** and **links itself**; the lane column
is the linker cluster that owns it. Wasm is NOT here — it self-links (the emitted module *is* the
artifact, `backend-wasm.md` §1.2), so it owes no linker step.

| # | Format | Arch | Linkage | Lane | Shared core |
|---|--------|------|---------|------|-------------|
| 1 | Linux musl ELF | x86_64 | **STATIC** | **E1** | `link_elf.tks` + `linker.tks` |
| 2 | Linux musl ELF | arm64 | **STATIC** | **E1** (after the arm64→ELF precursor, L1-0) | `link_elf.tks` + `linker.tks` |
| 3 | Linux musl ELF | riscv64 | **STATIC** | **E1** | `link_elf.tks` + `linker.tks` |
| 4 | Linux glibc ELF | x86_64 | DYNAMIC | E3 (#394) | `linker.tks` + `link_elf.tks` + dyn |
| 5 | Linux glibc ELF | arm64 | DYNAMIC | E3 (#394) | `linker.tks` + `link_elf.tks` + dyn |
| 6 | Linux glibc ELF | riscv64 | DYNAMIC | E3 (#394) | `linker.tks` + `link_elf.tks` + dyn |
| 7 | Windows PE/COFF | x86_64 | (per E2) | E2 (#393) | `linker.tks` + `link_coff.tks` |
| 8 | Windows PE/COFF | arm64 | (per E2) | E2 (#393) | `linker.tks` + `link_coff.tks` |
| 9 | macOS Mach-O | arm64 | (per E2) | E2 (#393) | `linker.tks` + `link_macho.tks` + `archive.tks` |

**E1 = rows 1–3 (the Linux musl-static ELF lane, all three arches).** The **shared
`src/link/linker.tks` core** (model + symbol resolution + `archive.tks` `ar` pull + `--prune-dead`/
`--strip`/merge/ICF + layout + entry-point) is reused **unchanged** by every row; each lane adds only
its format `.tks` back (ELF reloc/writer here, Mach-O/COFF for E2, the dynamic section for E3) and its
per-format entry-point (ELF `e_entry`, Mach-O `LC_MAIN`, PE `AddressOfEntryPoint` — §7). The
glibc-dynamic ELF rows (4–6) reuse this ELF back plus E3's dynamic-linking additions — same three
arches, different linkage. *(The owner labels this the "10-target matrix"; the enumerated rows sum to
9 own-link targets — a macOS-x86_64 or a 10th row may be intended; confirm the count, it does not
affect E1's rows 1–3.)*

### 1.4 The three musl arches — what is reachable at E1

The owner folds **arm64 into E1** (rows 1–3). The implemented `NativeTarget` (`project.tks:939`) has
**only** `X8664Linux` + `Riscv64Linux`; there is **no `Arm64Linux`**, and arm64 emits **Mach-O only**
(`emit_native_arm64` → `emit_macho`, `:1091`). So:

- **x86_64-musl-static** — E1 now (`emit_native_x86`); the **keystone** arch (L1-11).
- **riscv64-musl-static** — E1 now (`emit_native_riscv`); L1-12.
- **arm64-musl-static** — E1, **gated on a REQUIRED early backend precursor crumb (L1-0)**: add
  `NativeTarget::Arm64Linux` + an arm64→`emit_elf_object` adapter (the existing arm64 isel/regalloc/
  encoder already produce AAPCS64 machine bytes; only the *object wrapper* is Mach-O — L1-0 swaps in
  the ELF wrapper + the `MRelocKind`→`R_AARCH64_*` mapping). This resolves the prior Fork-A: arm64 is
  **in E1 as a required crumb**, not deferred (L1-0 → L1-13).

---

## 2. The input contracts

### 2.1 Own-object model (frozen in-tree, in-memory door)

- **`ElfObject`** (`objfile_elf.tks:795`) — `{ e_machine: u32, e_flags: u32, text: []byte,
  rodata: []byte, symbols: []Symbol, relocs: []ElfRelocReq }`.
- **`Symbol`** (`encode_arm64.tks:1787`) — `{ name: str, defined: bool, sect: u8, offset: u32,
  local: bool }`.
- **`ElfRelocReq`** (`objfile_elf.tks:762`) — `{ offset: u32, sym: str, rtype: u32, addend: i64 }`;
  `rtype` is ALREADY the numeric `R_<arch>_*` code.

### 2.2 Foreign-input model (`.o`/`.a` off disk — the CLI door)

Foreign inputs are read as **bytes** and parsed. **The bytes seam already exists**: `read_file`
returns raw binary as a `str` and the tree already reads a binary ZIP `.tkl` this way, converting
`str → []byte` via `s[i]` (`project.tks:167-180`). So the foreign door is pure Teko over the seed —
**no new intrinsic** (§10). Two parsers:
- **ELF `.o` parser** — `Elf64_Ehdr`/`Elf64_Shdr`/`.symtab`/`.rela.*` → `LinkObject`.
- **`ar` archive parser** — the `!<arch>\n` header, member table, and the `/` symbol-index member →
  `{ members: []LinkObject, index: []ArchiveSym }` for **lazy member pull**.

### 2.3 Relocation types E1 must apply (own + foreign)

| ISA | `rtype` | name | static meaning |
|-----|--------:|------|----------------|
| x86-64 | 1 | `R_X86_64_64` | `S + A` (abs64) |
| x86-64 | 2 | `R_X86_64_PC32` | `S + A − P` (pcrel32) |
| x86-64 | 4 | `R_X86_64_PLT32` | `S + A − P` (static ⇒ ≡ PC32, no PLT) |
| x86-64 | 10/11 | `R_X86_64_32`/`32S` | `S + A` (abs32; libc.a code uses these) |
| x86-64 | 9 | `R_X86_64_GOTPCREL` | GOT-relative — **honest-stop** unless a static GOT slot is synthesized (§8) |
| riscv64 | 2 | `R_RISCV_64` | `S + A` |
| riscv64 | 16/17 | `R_RISCV_BRANCH`/`JAL` | `S + A − P` |
| riscv64 | 19 | `R_RISCV_CALL_PLT` | `S + A − P` over `auipc+jalr` (static ⇒ no PLT) |
| riscv64 | 23/24 | `R_RISCV_PCREL_HI20`/`LO12_I` | HI20/LO12 pair (LO12 takes P from its HI20) |
| arm64 | 283/275/277/257 | `R_AARCH64_CALL26`/`ADR_PREL_PG_HI21`/`ADD_ABS_LO12_NC`/`ABS64` | applied at L1-13 (object emission unblocked by L1-0) |

**Any relocation type not in the applier's table is a NAMED honest-stop** (owner requirement #2 — M.3;
never a silent mislink). The corpus + musl `crt*.o`/`libc.a` fix the subset E1 must cover first;
E1 grows the table as new inputs demand.

---

## 3. Architecture (`src/link/`)

Three new modules, plus a CLI arm and a manifest section:

- **`src/link/linker.tks`** — the **format-independent core**: the neutral `LinkObject` model,
  symbol resolution + lazy archive pull, the reachability / dead-section GC (`--prune-dead`), the
  abstract section/segment/entry-point layout, and the link-opt passes (`--strip`, rodata-merge,
  ICF). **E2 (#393) and E3 (#394) reuse this whole module** (§12).
- **`src/link/link_elf.tks`** — the **ELF back**: the ELF `.o` parser, the per-`rtype` relocation
  applier (x86_64 + riscv64), and the static `ET_EXEC` writer (`Ehdr`/`Phdr`/segments, `e_entry`).
- **`src/link/archive.tks`** — the **`ar` static-archive** reader (format-neutral; ELF and Mach-O
  both use `ar`, so E2 reuses it).

Two front doors build a `LinkObject`: **(a) in-memory** from the own encoder's `ElfObject` (the fast
build path, no re-parse), **(b) from disk bytes** via `read_file` → parser (foreign `.o`/`.a`, and
the bundled musl objects). The **musl-static build** feeds the linker `[own.o (in-memory), crt1.o,
crti.o, libc.a, crtn.o]` in link order (§11).

### 3.1 Static image model (deterministic, non-PIE `ET_EXEC`)

Non-PIE `ET_EXEC` at a fixed load base (x86-64 `0x400000`, riscv64 `0x10000`), `PT_LOAD` segments
`[headers+.text]` R+X / `[.rodata]` R / `[.data+.bss]` RW, `p_align` 0x1000, `e_entry` = the
resolved **`_start`** vaddr (from `crt1.o`). Fixed base + `ET_EXEC` ⇒ no load-time relocations, no
PIE ⇒ **byte-deterministic** output (M.2). `.init_array`/`.fini_array` are concatenated in input
order and their bounds symbols (`__init_array_start/_end`) synthesized (§11). PIE/`ET_DYN` is E3.

---

## 4. Crumb sequence — [NOW-implementable]

Every crumb is an independent sub-PR with one gating fixture, **pure Teko over the seed** (byte-list
build via `teko::list::push`, binary read via `read_file`+`s[i]` per `project.tks:167-180`, write via
`write_file_bytes`). **No new seed intrinsic** (§10). Order: **L1-0** (arm64→ELF backend precursor)
runs on a **parallel track** (it is object *emission*, independent of the linker core); **L1-1→L1-9**
is the linker-core + x86-64 critical path; **L1-10/L1-11** wire the x86_64 keystone differential;
**L1-12** forks riscv64; **L1-13** folds in arm64 (needs L1-0 + L1-1..L1-9). Each of the three musl
arches (L1-11 x86_64 / L1-12 riscv64 / L1-13 arm64) is independently gate-able via `diff_c_own.sh`.

### L1-0 — arm64→ELF backend precursor (parallel track; unblocks the arm64 musl arch)
- **Goal:** make the arm64 backend emit an ELF `ET_REL` object so E1's arm64 musl arch (row 2) is
  reachable. The arm64 isel/regalloc/encoder already produce AAPCS64 machine bytes (`emit_native_arm64`
  `project.tks:1087`); **only the object wrapper is Mach-O**. L1-0 adds `NativeTarget::Arm64Linux`
  (`project.tks:939`) + `native_target()` mapping for `TEKO_TARGET=arm64-linux`, an
  `emit_native_arm64_elf` tail (reuse `select_module`/`regalloc_module(aapcs64())`/`encode_module`,
  swap `emit_macho` → a new `emit_elf_arm64`), and the `emit_elf_arm64(enc) -> []byte` adapter that
  lowers each `MRelocKind` (`encode_arm64.tks:1474` — `PageHi`/`PageLo`/`Call`; the Mach-O map at
  `objfile_macho.tks:194`) to its ELF code and hands a neutral `ElfObject` (`e_machine`=183=EM_AARCH64)
  to the shared `emit_elf_object` (`objfile_elf.tks:840`). This is object *emission* — NOT linker
  work — so it drains in parallel with L1-1..L1-9.
- **The `MRelocKind` → `R_AARCH64_*` map** the adapter injects: `Call`→`R_AARCH64_CALL26`(283),
  `PageHi`→`R_AARCH64_ADR_PREL_PG_HI21`(275), `PageLo`→`R_AARCH64_ADD_ABS_LO12_NC`(277), plus
  `Abs64`→`R_AARCH64_ABS64`(257) for data refs — the exact ELF counterpart of the Mach-O
  `ARM64_RELOC_*` mapping `objfile_macho.tks:194` already ships.
- **Files:** new `src/backend/objfile_elf_arm64.tks` (`emit_elf_arm64`, mirroring
  `objfile_elf_riscv.tks`), `src/build/project.tks` (`NativeTarget::Arm64Linux`, `native_target`,
  `emit_native_arm64_elf`); tests `objfile_elf_arm64_test.tkt`.
- **Fixture (gates):** `objfile_elf_arm64_test.tkt` golden (the arm64 ELF `.o` bytes for a known
  `enc`, reloc types asserted) + `scripts/check_elf.sh` (cross-format `readelf`/`llvm-readobj`) on the
  emitted arm64 `.o` — the same byte cross-check `emit_native_x86`/`_riscv` already pass.

### L1-1 — format-agnostic link model + in-memory own adapter
- **Goal:** the neutral model the whole linker walks + the `ElfObject → LinkObject` in-memory adapter.
- **Files:** new `src/link/linker.tks`, new `src/link/link_elf.tks` (`elf_to_link_object`); test
  `src/link/linker_test.tkt`.
- **Type signatures:**
```teko
/**
 * LinkSecKind — the section classes E1 lays out: Text (R+X), Rodata (R), Data (RW), Bss (RW,
 * no file bytes), InitArray/FiniArray (RW, order-preserving), Other (a named section carried
 * verbatim for a foreign object — placed by flags, never interpreted).
 *
 * @since #392 L1-1
 */
pub type LinkSecKind = enum { Text; Rodata; Data; Bss; InitArray; FiniArray; Other }

/**
 * LinkSection — one input section image: its kind, its ELF name (foreign sections keep their
 * name for --prune-dead + merge keying), the SHF flags (alloc/exec/write/merge), the bytes
 * (empty for Bss), and the in-memory size.
 *
 * @since #392 L1-1
 */
pub type LinkSection = struct {
    /** kind — the section class driving segment placement. */
    kind: LinkSecKind
    /** name — the ELF section name (e.g. ".text", ".rodata.str1.1"). */
    name: str
    /** flags — the SHF_* bitset (SHF_ALLOC/EXECINSTR/WRITE/MERGE/STRINGS). */
    flags: u64
    /** bytes — the section image (empty for Bss). */
    bytes: []byte
    /** size — the in-memory size (== bytes.len except Bss). */
    size: u64
}

/**
 * LinkSym — a symbol in the neutral table: name, whether THIS object defines it, its binding
 * (Local/Global/Weak — weak loses to a strong global, never a duplicate error), the defining
 * section index within its object, the in-section offset, and whether it is exported (a
 * --prune-dead root beyond the entry point).
 *
 * @since #392 L1-1
 */
pub type LinkSym = struct {
    /** name — the final (unprefixed) symbol name. */
    name: str
    /** defined — true iff the owning object defines it. */
    defined: bool
    /** binding — Local | Global | Weak. */
    binding: SymBinding
    /** sec — the defining section's index within its object (meaningful when defined). */
    sec: u32
    /** offset — the definition's byte offset within that section. */
    offset: u32
    /** exported — true iff a --prune-dead reachability root (entry or an exported global). */
    exported: bool
}

/**
 * SymBinding — ELF symbol binding: Local (private), Global (strong), Weak (yields to a strong
 * definition; an unresolved weak resolves to 0, never an error).
 *
 * @since #392 L1-1
 */
pub type SymBinding = enum { Local; Global; Weak }

/**
 * LinkReloc — one relocation: the patched field's offset within its object's section `sec`,
 * the target symbol name, the numeric R_<arch>_* type, and the addend.
 *
 * @since #392 L1-1
 */
pub type LinkReloc = struct {
    /** sec — the index of the section whose bytes hold the patched field. */
    sec: u32
    /** offset — the field offset within that section. */
    offset: u32
    /** sym — the target symbol name (resolved at L1-3). */
    sym: str
    /** rtype — the numeric R_<arch>_* code (arch-dispatched at L1-6/L1-12). */
    rtype: u32
    /** addend — the r_addend. */
    addend: i64
}

/**
 * LinkObject — one relocatable input: its machine id, its sections, its symbols, and its
 * relocations. Built by elf_to_link_object (in-memory own object), parse_elf_object (a disk
 * `.o`, L1-5), or an archive member (L1-4).
 *
 * @since #392 L1-1
 */
pub type LinkObject = struct {
    /** e_machine — 62 (x86-64) or 243 (riscv); the whole link must agree. */
    e_machine: u32
    /** e_flags — 0 (x86-64) or 0x0004 (RV64 LP64D). */
    e_flags: u32
    /** origin — the input's provenance (a path or "<own>"), for diagnostics. */
    origin: str
    /** sections — the object's section images. */
    sections: []LinkSection
    /** symbols — the object's symbol table. */
    symbols: []LinkSym
    /** relocs — the object's relocations. */
    relocs: []LinkReloc
}

/**
 * elf_to_link_object — ingest an own-emitted ElfObject into the neutral model (in-memory, no
 * disk round-trip): the ElfObject value taken before emit_elf_object serializes it.
 *
 * @param obj  the own ISA-agnostic ELF object (objfile_elf.tks:795)
 * @return     the neutral LinkObject
 */
pub fn elf_to_link_object(obj: teko::backend::ElfObject) -> LinkObject { /* L1-1 */ }
```
- **Fixture (gates):** `linker_test.tkt` — a two-symbol `ElfObject` (defined `.text` fn + undefined
  ref) → `elf_to_link_object` → every field maps. Pure model, both engines.

### L1-2 — ELF `.o` parser (bytes → `LinkObject`)
- **Goal:** parse a relocatable ELF64 object from a `[]byte` (own re-parse AND foreign): `Ehdr`
  (assert `ET_REL`, machine matches), `Shdr` table, `.symtab`+`.strtab`, `.rela.*`. Unknown/
  unsupported constructs (compressed sections, `ET_REL` machine mismatch) → **named honest-stop**.
- **Files:** `src/link/link_elf.tks`; test `src/link/link_elf_test.tkt`.
- **Type signature:**
```teko
/**
 * parse_elf_object — parse a relocatable ELF64 object from raw bytes into the neutral model.
 * Validates e_ident/e_type==ET_REL/e_machine; lifts every SHF_ALLOC section, the symbol table
 * (binding + section + value), and each `.rela` table. A malformed header, a non-ET_REL type,
 * a machine mismatch, or a compressed/unsupported section is a NAMED error (owner req #2, M.3).
 * Testable today over in-memory bytes (e.g. emit_elf(enc) output) — no disk read required to
 * exercise it.
 *
 * @param bytes   the object file image
 * @param origin  the source path (diagnostics)
 * @return        the LinkObject, or a named parse error
 */
pub fn parse_elf_object(bytes: []byte, origin: str) -> LinkObject | error { /* L1-2 */ }
```
- **Fixture (gates):** `link_elf_test.tkt` — round-trip: `emit_elf(enc)` bytes → `parse_elf_object`
  → assert the recovered sections/symbols/relocs equal the source `ElfObject`; plus a non-`ET_REL`
  header → the named honest-stop.

### L1-3 — symbol resolution across N objects + honest binding rules
- **Goal:** merge all input symbol tables into one global table; bind undefined refs to defining
  objects; **weak/strong** precedence (a strong global beats a weak; two strong globals = duplicate
  error; an unresolved weak → 0); a still-unresolved strong ref = unresolved error.
- **Files:** `src/link/linker.tks`; test `linker_test.tkt`.
- **Type signatures:**
```teko
/**
 * GlobalSym — a resolved global entry: name, the defining object index, its section index there,
 * its in-section offset, and its binding (a Weak entry may still be overridden by a later Strong).
 *
 * @since #392 L1-3
 */
pub type GlobalSym = struct {
    /** name — the resolved symbol name. */
    name: str
    /** obj — the defining object's index in the link list. */
    obj: u64
    /** sec — its section index within that object. */
    sec: u32
    /** offset — its in-section offset. */
    offset: u32
    /** binding — Global or Weak (Local never enters the global table). */
    binding: SymBinding
}

/**
 * resolve_symbols — build the global table across `objs`. Strong-vs-strong duplicate = error;
 * Strong overrides Weak silently; Local stays private. Undefined-everywhere strong names are NOT
 * an error here (an archive member may still define them — L1-4); they are re-checked after the
 * archive pull.
 *
 * @param objs  the explicit (non-archive) objects, in link order
 * @return      the global table, or a duplicate-symbol error
 */
pub fn resolve_symbols(objs: []LinkObject) -> []GlobalSym | error { /* L1-3 */ }
```
- **Fixture (gates):** `linker_test.tkt` — clean resolve; strong-over-weak override; strong-strong
  duplicate error; unresolved-strong error surfaced (post-archive).

### L1-4 — `ar` archive reader + lazy member pull
- **Goal:** parse a `.a` (`!<arch>\n`, member headers, the `/` symbol-index member) and pull ONLY
  members that define a currently-undefined symbol, iterating to a fixpoint (a pulled member may
  create new undefined refs). This is the classic static-archive semantics that makes `libc.a`
  contribute only what the program uses.
- **Files:** new `src/link/archive.tks`, `src/link/linker.tks` (the pull loop); test
  `src/link/archive_test.tkt`.
- **Type signatures:**
```teko
/**
 * Archive — a parsed `ar` static archive: its members (each an unparsed object image + name) and
 * its symbol index (symbol name → member offset), driving lazy pull. Format-neutral (ELF and
 * Mach-O both use `ar`), so E2 reuses it.
 *
 * @since #392 L1-4
 */
pub type Archive = struct {
    /** members — the raw member images, in archive order. */
    members: []ArchiveMember
    /** index — the `/` symbol table: symbol name → defining member. */
    index: []ArchiveSym
}

/**
 * ArchiveMember — one archive member: its name and its raw object bytes (parsed on demand only
 * when the member is pulled — the whole point of lazy selection).
 *
 * @since #392 L1-4
 */
pub type ArchiveMember = struct {
    /** name — the member file name. */
    name: str
    /** bytes — the member's raw object image. */
    bytes: []byte
}

/**
 * ArchiveSym — one symbol-index row: the exported symbol name and the member index that defines it.
 *
 * @since #392 L1-4
 */
pub type ArchiveSym = struct {
    /** name — an exported symbol name. */
    name: str
    /** member — the index into Archive.members that defines it. */
    member: u64
}

/**
 * parse_archive — parse a `.a` image into members + symbol index. A malformed magic/header is a
 * named error.
 *
 * @param bytes   the archive image
 * @param origin  the source path (diagnostics)
 * @return        the Archive, or a named parse error
 */
pub fn parse_archive(bytes: []byte, origin: str) -> Archive | error { /* L1-4 */ }

/**
 * pull_archive_members — the lazy fixpoint: while an undefined strong symbol is defined by an
 * un-pulled member of some `arch`, parse and add that member (via parse_elf_object), updating the
 * undefined set. Terminates when no undefined symbol names an un-pulled member. Foreign arch/format
 * mismatch on a pulled member is a named honest-stop.
 *
 * @param objs      the explicit objects (seed the undefined set)
 * @param archives  the archives, in link order
 * @return          the explicit objects plus every pulled member, or a named error
 */
pub fn pull_archive_members(objs: []LinkObject, archives: []Archive) -> []LinkObject | error { /* L1-4 */ }
```
- **Fixture (gates):** `archive_test.tkt` — a synthetic 2-member archive built in-test: pulling with
  an undefined `foo` pulls only `foo`'s member (not the unused one), and a transitive `foo→bar`
  reference pulls `bar`'s member; asserts the pulled set + size.

### L1-5 — reachability + `--prune-dead` (link-time dead-SECTION elimination)
- **Goal:** the mark-sweep over the section reference graph seeded at the **entry + exported roots**;
  drop unreferenced `SHF_ALLOC` sections from the image. **This is dead-SECTION elimination, NOT
  runtime GC** (owner explicitly renamed it away from `--gc-sections`; runtime GC stays banned). Off
  by default; `--prune-dead` enables it. Reports a byte-size delta.
- **Files:** `src/link/linker.tks`; test `linker_test.tkt`.
- **Type signatures:**
```teko
/**
 * SizeReport — a link-opt pass's before/after byte accounting, surfaced to the driver so the
 * differential gate's bytes column (§9) can assert the size effect.
 *
 * @since #392 L1-5
 */
pub type SizeReport = struct {
    /** pass — the pass name ("prune-dead" | "strip" | "merge-rodata" | "icf"). */
    pass: str
    /** before — total allocated bytes before the pass. */
    before: u64
    /** after — total allocated bytes after the pass. */
    after: u64
}

/**
 * prune_dead_sections — mark-sweep live sections from the entry symbol + every exported root,
 * following relocations section-to-section; sweep the rest. NOT runtime GC — a pure static
 * reachability over the SECTION graph (conceptually parallel to reachable_libs, project.tks:246,
 * but over sections). Returns the pruned objects + a SizeReport.
 *
 * @param objs   the linked objects (post archive pull)
 * @param gtab   the resolved global table
 * @param entry  the entry root symbol name
 * @return       the pruned objects + the size report
 */
pub fn prune_dead_sections(objs: []LinkObject, gtab: []GlobalSym, entry: str) -> PruneResult { /* L1-5 */ }
```
- **Fixture (gates):** `linker_test.tkt` — an object with a referenced `.text.used` and an
  unreferenced `.text.dead`: `--prune-dead` drops `.text.dead`, keeps an exported root even if
  unreferenced, and the `SizeReport.after < before` by the dead section's size.

### L1-6 — x86-64 relocation application (+ honest reloc validation)
- **Goal:** patch every kept x86-64 relocation using the final vaddr table: `R_X86_64_64` (`S+A`),
  `PC32`/`PLT32` (`S+A−P`), `32`/`32S` (`S+A`). Unsupported types (`GOTPCREL`, TLS) → **named
  honest-stop**. Out-of-range field → error, not truncation.
- **Files:** `src/link/link_elf.tks`; test `link_elf_test.tkt`.
- **Type signature:**
```teko
/**
 * apply_relocs_x86 — apply x86-64 relocations into the laid-out section images, dispatching on
 * the numeric rtype (1/2/4/10/11). P is the field's absolute vaddr; S is the resolved symbol
 * vaddr. Any rtype outside the supported set is a NAMED honest-stop (owner req #2); an
 * out-of-range pcrel32/abs32 is a link error (M.3).
 *
 * @param img   the laid-out image (patched in place-of-address)
 * @param objs  the linked objects (source of relocs)
 * @return      the patched image, or a named link error
 */
pub fn apply_relocs_x86(img: ImageLayout, objs: []LinkObject) -> ImageLayout | error { /* L1-6 */ }
```
- **Fixture (gates):** `link_elf_test.tkt` — `PLT32`/`PC32`/`64`/`32S` patch-byte goldens + a
  `GOTPCREL` input → the named honest-stop.

### L1-7 — section/segment layout + linker-owned entry point (owner ruling 2026-07-11)
- **Goal:** merge kept sections by class, place segments at the arch base, resolve every symbol to an
  absolute vaddr, synthesize `.init_array`/`.fini_array` bounds symbols, and **set the entry point
  itself** — ELF `e_entry` = the resolved `_start` vaddr (the job `cc` did before; now the core's).
- **Files:** `src/link/linker.tks` (abstract layout + `resolve_entry`), `src/link/link_elf.tks`
  (base/alignment); test `link_elf_test.tkt`.
- **Type signatures:**
```teko
/**
 * ImageLayout — the laid-out static image: the ordered load segments, the merged section images,
 * the .bss size, the resolved entry vaddr, and the per-symbol absolute vaddr table.
 *
 * @since #392 L1-7
 */
pub type ImageLayout = struct {
    /** segments — the PT_LOAD segments in file order. */
    segments: []LinkSegment
    /** images — the merged, final-ordered section images (Text/Rodata/Data). */
    images: []LinkSection
    /** bss_size — the aggregate .bss size. */
    bss_size: u64
    /** entry — the resolved entry virtual address (Ehdr e_entry). */
    entry: u64
    /** sym_vaddr — resolved (name → absolute vaddr) per live symbol. */
    sym_vaddr: []SymAddr
}

/**
 * resolve_entry — the linker-owned entry point (owner ruling 2026-07-11): the vaddr of the entry
 * symbol (ELF "_start", from crt1.o). E2/E4 supply the per-format analogue (Mach-O LC_MAIN, PE
 * AddressOfEntryPoint, wasm `_start` export) over the same resolved table — so the entry decision
 * lives in the linker, never in `cc`.
 *
 * @param sym_vaddr  the resolved symbol→vaddr table
 * @param entry_name the entry symbol ("_start" for ELF)
 * @return           the entry vaddr, or an "entry symbol undefined" error
 */
pub fn resolve_entry(sym_vaddr: []SymAddr, entry_name: str) -> u64 | error { /* L1-7 */ }
```
- **Fixture (gates):** `link_elf_test.tkt` — golden layout (RX base + page-aligned RO/RW), `entry`
  == the `_start` `SymAddr`, `__init_array_start/_end` bracket the concatenated `.init_array`.

### L1-8 — static ELF `ET_EXEC` writer
- **Goal:** serialize `ImageLayout` → a runnable static `ET_EXEC` (`Ehdr` with `e_entry`, 3× `PT_LOAD`
  `Phdr`, padded segment images). Reuses the LE byte emitters in `objfile_elf.tks:11`.
- **Files:** `src/link/link_elf.tks`; test `link_elf_test.tkt` + the L1-11 integration.
- **Type signature:**
```teko
/**
 * write_static_elf — the top-level x86-64 ELF static linker: from the resolved ImageLayout, emit
 * the Elf64_Ehdr (e_type=ET_EXEC, e_machine/e_flags, e_entry), the PT_LOAD Phdr array, and the
 * segment images. No PT_INTERP/dynamic/PLT/GOT (that is E3). Byte-deterministic.
 *
 * @param img  the resolved, relocated image
 * @return     the runnable static ELF bytes
 */
pub fn write_static_elf(img: ImageLayout) -> []byte { /* L1-8 */ }
```
- **Fixture (gates):** `link_elf_test.tkt` — magic/`e_type`==2/`e_phnum`==3/`e_entry`-in-RX byte
  asserts (end-to-end run deferred to L1-11 where the musl runtime is present).

### L1-9 — link-opt passes: `--strip`, rodata-merge, ICF
- **Goal:** the remaining format-agnostic optimizations, each returning a `SizeReport`: `--strip`
  (drop `.symtab`/`.strtab`/non-alloc sections), **rodata-merge** (SHF_MERGE|STRINGS dedup of
  identical strings/consts), **ICF** (identical-code-folding — optional/LATE, behind `--icf`, off by
  default).
- **Files:** `src/link/linker.tks`; test `linker_test.tkt`.
- **Type signatures:**
```teko
/**
 * strip_symbols — drop the non-allocated symbol/string sections (and any debug section) from the
 * output when --strip is set; the allocated image is unchanged. Returns a SizeReport (file-size
 * delta, since strip touches only non-alloc bytes).
 *
 * @param img  the laid-out image
 * @return     the stripped image + size report
 */
pub fn strip_symbols(img: ImageLayout) -> StripResult { /* L1-9 */ }

/**
 * merge_rodata — dedup byte-identical entries across SHF_MERGE|STRINGS sections, rewriting the
 * relocations that pointed at a removed duplicate to its survivor. Returns a SizeReport.
 *
 * @param objs  the linked objects (rodata + relocs rewritten)
 * @return      the merged objects + size report
 */
pub fn merge_rodata(objs: []LinkObject) -> MergeResult { /* L1-9 */ }

/**
 * fold_identical_code — OPTIONAL/LATE (--icf, default off): fold byte-identical, relocation-
 * equivalent Text sections to one copy, redirecting symbols. Conservative (no fold across
 * differing relocation targets). Returns a SizeReport.
 *
 * @param objs  the linked objects
 * @return      the folded objects + size report
 */
pub fn fold_identical_code(objs: []LinkObject) -> IcfResult { /* L1-9 */ }
```
- **Fixture (gates):** `linker_test.tkt` — two identical rodata strings → one copy + relocs
  repointed + `SizeReport` delta; `--strip` drops the symtab; `--icf` folds two identical leaf
  functions.

### L1-10 — `teko link` CLI + `[link]` manifest + honest validation (two input-resolution modes)
- **Goal:** the driver subcommand, with the owner's **two input-resolution modes + hard invariant**.
  On parse, **validate format + arch + version** of every input; a mismatch (an arm64 object into an
  x86-64 link, a Mach-O into an ELF link, a stale object version) is a **named honest error** BEFORE
  any patching (owner req #2).

  **Mode 1 — `.tkp`-driven (project mode).** When a `.tkp` is provided or in context, the linker
  **READS the manifest to discover the object set**: the project's own emitted object(s) **plus** the
  `[link]` section inputs (extra `.o`/`.a`, libs, link flags, archive search paths). *The manifest is
  the source of truth for the object/archive set in this mode.*
  ```
  teko link [--project <dir>]           # or: run inside a dir carrying <name>.tkp
            [-o <out>] [--target <triple>] [--prune-dead] [--strip] [--merge] [--icf]
  ```

  **Mode 2 — explicit-objects.** The caller passes the complete set on the command line:
  ```
  teko link <a.o> <b.o> <lib.a> ...  -o <out>
            [--target <triple>] [--prune-dead] [--strip] [--merge] [--icf]
  ```

  **Invariant (owner, hard):** *if no `.tkp` is provided, ALL objects must be passed explicitly.* The
  linker does **ZERO discovery** in Mode 2 — the caller supplies the COMPLETE set (own objects +
  runtime/musl archives + any foreign `.o`). **No `.tkp` = no implicit inputs, ever.** Concretely: an
  empty positional list **and** no `.tkp` is a named error (`teko link: no inputs and no .tkp`), never
  a silent link of nothing; `[link]`/bundle discovery fires ONLY in Mode 1.
- **Where each mode lands in the sequence:** the **own-build path** (`finish_native_object`, L1-11) is
  **Mode 1** — it already holds the resolved `Manifest` and the in-memory own `ElfObject`, so it takes
  the own object **in-memory** (no re-parse) and discovers the musl bundle + `[link]` inputs from the
  manifest, reading those archives via `read_file`. The **`teko link` foreign path is Mode 2** — every
  input, including the own `.o`, arrives on the command line and is read from disk via `read_file`
  (this is the path that exercises the foreign-`.o`/`.a` parsers L1-2/L1-4 end-to-end; §10 confirms it
  needs **no** new seed intrinsic — the brief's `read_file_bytes` wall does not exist).
- **Files:** the driver dispatch (new `link` arm beside `build`/`run`/`test`/`fmt`, recognized at
  `src/build/help.tks:21`), `src/build/manifest.tks` (`[link]` section, parse at `:251`, `Manifest`
  at `:31`), a new `link_cmd` entry alongside `compile_project` (`project.tks:1259`); tests
  `manifest_test.tkt`, `help_test.tkt`, a `link_cmd` test.
- **Type signatures:**
```teko
/**
 * LinkInputs — the resolved input set + options, produced by resolve_link_inputs from EITHER
 * mode, so the format dispatch below is mode-agnostic. In Mode 1 `objects`/`archives` come from
 * the manifest + bundle; in Mode 2 they are exactly the command-line positionals.
 *
 * @since #392 L1-10
 */
pub type LinkInputs = struct {
    /** own — the in-memory own object when the build path supplies it (Mode 1), else absent. */
    own: []teko::backend::ElfObject
    /** objects — the on-disk .o inputs to read+parse (foreign, or the own .o in Mode 2). */
    objects: []str
    /** archives — the .a inputs (musl bundle + [link] libs in Mode 1; positionals in Mode 2). */
    archives: []str
    /** out — the -o output path. */
    out: str
    /** target — the resolved target triple ("" = host). */
    target: str
    /** opts — the requested link-opt flags (prune-dead/strip/merge/icf). */
    opts: LinkOpts
}

/**
 * resolve_link_inputs — apply the owner's two-mode rule. If a `.tkp` is present (Mode 1), READ it:
 * the own object(s) + the [link] section inputs + the bundled musl archives (§10) are the set. If
 * NO `.tkp` (Mode 2), the set is EXACTLY the command-line positionals — zero discovery; an empty
 * positional list is the "no inputs and no .tkp" named error. The invariant lives HERE so both the
 * own-build path and `teko link` share one honest resolver.
 *
 * @param argv  the `teko link` argument vector
 * @param m     the resolved manifest when a `.tkp` is in context, else absent (Mode 2)
 * @return      the resolved input set, or a named error (no inputs / bad flag)
 */
pub fn resolve_link_inputs(argv: []str, m: []Manifest) -> LinkInputs | error { /* L1-10 */ }

/**
 * link_cmd — the `teko link` entry: resolve_link_inputs (two modes above), read each on-disk input
 * via read_file (the binary seam, project.tks:167-180) into bytes + parse, dispatch to the matching
 * format linker by the first input's format+arch (validating every subsequent input agrees — a
 * mismatch is a named error), run the requested opt passes, and write `-o <out>`. The one command
 * the own toolchain exposes to link arbitrary own+foreign objects/archives.
 *
 * @param argv  the `teko link` argument vector
 * @return      the process exit status (0 on a successful link)
 */
fn link_cmd(argv: []str) -> i32 { /* L1-10 */ }
```
- **Fixture (gates):** `manifest_test.tkt` + a `link_cmd` unit — (a) Mode 2 explicit positionals link;
  (b) Mode 1 `[link]` discovery pulls the manifest inputs; (c) **no `.tkp` + no positionals → the
  named "no inputs and no .tkp" error** (the invariant's trip-wire); (d) a format/arch-mismatch input
  → the named honest error (exit non-zero, message names the offender).

### L1-11 — wire the musl-static lane + the differential bytes column, behind the seam
- **Goal:** route `finish_native_object`'s link step to the own linker for ELF targets under
  `TEKO_LINK=own`: link `[own.o (in-memory), <bundle>/crt1.o, crti.o, libc.a, crtn.o]` in order (§11),
  reading the bundled musl objects via `read_file`. Default (seam unset) calls `link_object`
  (`project.tks:737`) byte-for-byte as today — **FIXPOINT preserved**. Extend `diff_c_own.sh` with a
  **bytes column** asserting the own-linked size against a per-fixture budget.
- **Files:** `src/build/project.tks` (`finish_native_object` `:1198` seam branch + a `link_object_own`
  that calls `teko::link::link_cmd`/`write_static_elf`), `scripts/diff_c_own.sh` (bytes column);
  `project_test.tkt` (seam-unset fixpoint guard).
- **Fixture (gates):** `diff_c_own.sh` linux-x86_64 under `TEKO_LINK=own` — own-linked musl-static
  ELF exit+stdout == C-native over the corpus, the `(own backend)` marker guard holds, and the bytes
  column is within budget; `project_test.tkt` asserts the seam-unset path still calls the cc linker.

### L1-12 — riscv64 relocation application + riscv musl archives + qemu differential
- **Goal:** the riscv fork of L1-6/L1-11: `apply_relocs_riscv` (`R_RISCV_64`, `BRANCH`/`JAL`,
  `CALL_PLT` → static `auipc+jalr`, `PCREL_HI20`/`LO12_I` pairing), the riscv `write_static_elf_riscv`
  (base `0x10000`, `e_flags`=0x0004), and the bundled riscv musl `crt*.o`/`libc.a`.
- **Files:** `src/link/link_elf.tks`, `src/build/project.tks` (riscv bundle paths); tests
  `link_elf_test.tkt`, `diff_c_own.sh` riscv leg.
- **Fixture (gates):** the `riscv64-qemu` leg of `diff_c_own.sh` under `TEKO_LINK=own
  TEKO_DIFF_TARGET=riscv64-linux` — own-linked riscv musl-static ELF runs under `qemu-riscv64-static`,
  exit+stdout == C-riscv, bytes within budget.

### L1-13 — arm64 relocation application + arm64 musl archives + differential (needs L1-0)
- **Goal:** the arm64 fork of L1-6/L1-11 for the arm64 musl arch (row 2), unblocked by the L1-0
  ELF-object precursor: `apply_relocs_arm64` (`R_AARCH64_CALL26` `S+A−P` into the BRANCH26 field;
  `ADR_PREL_PG_HI21` page-hi and `ADD_ABS_LO12_NC` page-lo, paged against `P`; `ABS64` `S+A`), the
  arm64 `write_static_elf_arm64` (`e_machine`=183, base `0x400000`), and the bundled arm64 musl
  `crt*.o`/`libc.a`.
- **Files:** `src/link/link_elf.tks` (`apply_relocs_arm64`, `write_static_elf_arm64`),
  `src/build/project.tks` (arm64 bundle paths + the `Arm64Linux` link seam); tests
  `link_elf_test.tkt`, `diff_c_own.sh` arm64 leg.
- **Fixture (gates):** an arm64 leg of `diff_c_own.sh` under `TEKO_LINK=own TEKO_TARGET=arm64-linux`
  — own-linked arm64 musl-static ELF exit+stdout == C-native (natively on an arm64 runner, or under
  `qemu-aarch64-static` on the x86_64 CI host), bytes within budget. Depends on **L1-0 + L1-1..L1-9**.

---

## 5. Ritual / verification points

| Crumb | Proving leg | Ritual point |
|-------|-------------|--------------|
| L1-0 (parallel) | `objfile_elf_arm64_test.tkt` golden + `check_elf.sh` on the arm64 `.o` | unit + cross-format |
| L1-1..L1-4 | `linker_test.tkt` / `link_elf_test.tkt` / `archive_test.tkt` units | unit |
| L1-5 | `--prune-dead` unit + SizeReport delta | unit |
| L1-6 | x86 reloc patch-byte goldens + honest-stop | unit |
| L1-7 | golden layout + linker-owned `e_entry` + init_array bounds | unit |
| L1-8 | ELF `ET_EXEC` well-formedness bytes | unit |
| L1-9 | strip/merge/ICF SizeReport units | unit |
| L1-10 | `teko link` two-mode resolve + no-`.tkp`-no-inputs invariant + format/arch-mismatch honest error | unit |
| **L1-11** | `diff_c_own.sh` `TEKO_LINK=own` **musl-static own==C x86_64 + bytes column** + fixpoint guard | **RITUAL: musl-static differential (keystone arch)** |
| **L1-12** | `diff_c_own.sh` riscv qemu leg, `TEKO_LINK=own` | **RITUAL: riscv64 qemu differential** |
| **L1-13** | `diff_c_own.sh` arm64 leg, `TEKO_LINK=own TEKO_TARGET=arm64-linux` (native or qemu-aarch64) | **RITUAL: arm64 differential (needs L1-0)** |

**Switch point + flag safety:** the own linker is reached at `finish_native_object`
(`project.tks:1198`); L1-11 adds the `TEKO_LINK=own` branch. Seam-unset ⇒ `cc`-as-linker unchanged ⇒
**the default C path's FIXPOINT never regresses** (the `project_test.tkt` seam-unset guard is the
trip-wire).

**Self-host fixpoint — confirmed E1 bar is the CORPUS, not the compiler (C-3).** With musl-static,
E1 *could* eventually link the full-libc compiler — but that stresses the whole `libc.a` reloc/TLS
surface at once. **E1's gate bar is the corpus differential (musl-static own==C)**; the **compiler
binary stays on `cc`-link** until the musl lane is proven across K releases and the reloc table is
complete. Own-linking the compiler + its byte-identical fixpoint is a **post-E1** milestone (it, plus
the glibc-dynamic lane, is E3-adjacent). Confirmed per the brief.

---

## 6. `teko link` CLI + `[link]` manifest (summary of L1-10)

`teko link <inputs.o|.a ...> -o <out> [--target <triple>] [--prune-dead] [--strip] [--merge]
[--icf]`. The `[link]` `.tkp` section carries default extra inputs, default opt flags, and archive
search paths (parsed beside `[extern]` at `manifest.tks:353`). Every input is format+arch+version
validated on read; the first input fixes the link's format/arch and any disagreeing input is a named
error. This is the surface that lets the own toolchain link **own + foreign** objects/archives — the
independence deliverable.

---

## 7. Link-optimization flags (summary of L1-5 + L1-9)

All in the format-agnostic core, tested per-format, each returning a `SizeReport`:
`--prune-dead` (dead-SECTION elimination — NOT runtime GC), `--strip` (drop symtab/strings),
`--merge` (SHF_MERGE rodata/const dedup), `--icf` (identical-code-folding, optional/late, default
off). The differential gate gains a **bytes column** (L1-11) so a size regression is caught like an
exit/stdout regression.

---

## 8. Honest validation (owner requirement #2, threaded through)

Every parse/apply boundary honest-stops with a NAMED error rather than mislinking: a non-`ET_REL`
object (L1-2), an archive with a bad magic (L1-4), a strong-strong duplicate (L1-3), an unresolved
strong symbol (L1-3/L1-4), an **unsupported relocation type** (L1-6/L1-12 — the `GOTPCREL`/TLS/COMDAT
frontier), an out-of-range field, and a **format/arch/version mismatch** on any input (L1-10). These
stops ARE the honest scope boundary: E1 covers the reloc/section subset the corpus + musl
`crt*.o`/`libc.a` actually use, and grows the table crumb by crumb — never silently.

---

## 9. Bootstrap-wall check — CORRECTED verdict: **NO new seed intrinsic; NO staged wall**

**The brief's premise is corrected with evidence.** The brief states E1 needs a
`read_file_bytes -> []byte | error` intrinsic (a staged-seed wall) for the foreign-input path. **It
does not.** `teko::io::read_file` (`io.tks:18`) already returns **raw binary bytes as a `str`**, and
the tree **already reads a binary ZIP `.tkl` archive this way**, converting `str → []byte` by `s[i]`
indexing — `project.tks:167-180` (comment: *"the file is a ZIP archive — binary, not UTF-8, but
read_file returns the raw bytes as a str; we treat each byte via s[i]"*). teko `str` is length-based
(NUL-safe), so it holds arbitrary bytes. **E1 reads foreign `.o`/`.a` and the bundled musl objects
through the existing `read_file` seam** — proven, in the seed, used for exactly this shape today.

Therefore **every E1 crumb, including the musl-static end-to-end differential (L1-11/L1-12), is
NOW-implementable** — pure Teko over `read_file` + `teko::list::push` + `write_file_bytes`
(`io.tks:46`). **No new builtin/intrinsic. No staged wall. Nothing blocks on a prior release.**

A dedicated `read_file_bytes` would be a **performance refinement only** (the per-byte `s[i]→[]byte`
copy is O(n) with per-byte list growth — costly for a multi-MB `libc.a`, an M.4 concern, not a
correctness one). If profiling shows the copy dominates, add `read_file_bytes` as an **optimization**
later; it is **not** an E1 prerequisite. **This correction is Fork-B I return to the owner (§13).**

---

## 10. The musl bundle: link order + start symbols (E1 deliverable, owner ruling 2026-07-10)

Reuses the **#182** toolchain-bundle infra (the per-arch runtime + assert already resolve via
`rt_dir`/`assert_dir`, `project.tks:387`, `:1318`; the bundle adds `crt1.o`/`crti.o`/`crtn.o` +
`libc.a` per arch beside them). The static link order E1 emits:

```
crt1.o  crti.o  <own program>.o  --start-group libc.a --end-group  crtn.o
```

- **`crt1.o`** defines **`_start`** (⇒ `e_entry`, L1-7) and calls `__libc_start_main(main, argc,
  argv)` — the own object still defines **`main`** exactly as today (frameless `bl _tk_exit` corpus
  shape), so nothing in the backend changes.
- **`crti.o`/`crtn.o`** frame `_init`/`_fini`; E1 concatenates `.init_array`/`.fini_array` in order
  and synthesizes `__init_array_start/_end` (L1-7).
- **`libc.a`** contributes only pulled members (`__libc_start_main`, `malloc`, `write`, … whatever
  `teko_rt`'s corpus path references) via lazy selection (L1-4). musl's `libc.a` is self-contained
  and fully static (the reason the owner picked musl over glibc for the static lane).

The **start-symbol set** E1 must satisfy for the corpus: `_start`, `__libc_start_main`, `main`,
`_init`/`_fini`, `__init_array_start/_end`, plus the `teko_rt` externs (`tk_*`). Any symbol still
undefined after the archive pull is the named unresolved error (L1-3).

---

## 11. The shared-core contract E2/E3 reuse

`src/link/linker.tks` + `src/link/archive.tks` are format-neutral by construction (opaque `u32`
`rtype`; section *kinds* + names, not ELF indices; `ar` is shared by ELF and Mach-O). **E2 (#393,
Mach-O/COFF)** adds only a format→`LinkObject` adapter, a per-format reloc applier, and a
header/load-command writer (+ the per-format entry point: Mach-O `LC_MAIN`, PE `AddressOfEntryPoint`)
— reusing resolution + archive pull + `--prune-dead`/`--strip`/merge/ICF + layout whole. **E3 (#394)**
adds the dynamic path (`PT_INTERP`/`PT_DYNAMIC`/GOT-PLT, `ET_DYN` PIE) for the **glibc-dynamic** Linux
lane and shared libraries; the core `LinkObject`/resolution/GC stay unchanged. Same "shared core +
thin per-target adapter" split as Phase B (`own-backend-architecture.md:171-181`).

---

## 12. Dependencies + what E1 unblocks

- **Depends on:** #386 (B1 x86-64 ELF object — **LANDED**: `emit_native_x86` `project.tks:1113`,
  `objfile_elf.tks`, commit `ffd2a64`) and #387 (B2 riscv64 ELF object — **LANDED**:
  `emit_native_riscv` `:1171`, `objfile_elf_riscv.tks`, the `riscv64-qemu` leg
  `.github/workflows/native.yml:145`). Both are in-tree and green. The **arm64 musl arch (row 2)**
  additionally depends on the in-lane precursor **L1-0** (arm64→ELF object emission; arm64 emits
  Mach-O only today). Plus the **#182 bundle** infra for the per-arch musl archives (§10).
- **E1 is C-RETIRE condition-3 progress for the ELF targets** (`own-backend-architecture.md` §2.4
  `:99`): after E1, x86_64-musl, riscv64-musl and arm64-musl own-native binaries need **no external
  linker** (and no external toolchain — the archives are bundled, not toolchain-invoked).
- **Unblocks:** E2 (#393 Mach-O/COFF: rows 7–9) and E3 (#394 dynamic + the glibc-dynamic ELF lane:
  rows 4–6) on the shared core (§11).

---

## 13. Forks returned to the owner (integrator to relay — I do not re-issue)

- **Fork-A — RESOLVED by the owner's definitive matrix.** arm64-musl is **in E1** (row 2). The
  arm64→ELF backend precursor is now a **required E1 crumb, L1-0** (not a deferred fork): add
  `NativeTarget::Arm64Linux` + `emit_elf_arm64` over the shared `emit_elf_object`, reusing the
  existing arm64 isel/regalloc/encoder. L1-0 runs on a **parallel track** and unblocks L1-13. Recorded
  here only to note the prior fork is closed.
- **Fork-B — the `read_file_bytes` wall does not exist (a correction the integrator should relay to
  the owner).** The last brief asked me to *keep* `read_file_bytes` as a staged-seed prerequisite. I
  cannot record it as a wall against the evidence: `read_file` (`io.tks:18`) **already returns raw
  binary bytes as a `str`**, and the tree **already reads a binary ZIP `.tkl` this way**, converting
  `str → []byte` by `s[i]` — `project.tks:167-180` (its comment says exactly this). So **E1 reads
  foreign `.o`/`.a` (Mode 2) and the musl bundle (Mode 1) through the existing seam; there is NO
  staged wall and nothing blocks on a prior release.** A dedicated `read_file_bytes` would be a
  **performance-only** refinement (the per-byte copy is O(n) with per-byte list growth — costly for a
  multi-MB `libc.a`, an M.4 concern, not correctness). **Recommendation:** E1 proceeds on the existing
  `read_file` seam now; add `read_file_bytes` later only if profiling the archive read demands it.
  **Owner ruling wanted only to confirm this (recommended), or to require the perf-refinement extern
  up front** — either way E1 is not blocked. *(This is the honest relay: I am flagging the
  disagreement with the brief rather than silently recording a non-existent wall — M.3.)*
- **C-3 — E1 fixpoint bar = the corpus differential** (musl-static own==C), the compiler stays on
  `cc`-link until the musl lane is proven + the reloc table complete; compiler own-link + its
  byte-identical fixpoint is post-E1 / E3-adjacent. **Recorded as confirmed per the brief** (§5).

None of these change the crumb sequence for the three musl arches — Fork-A is folded in as L1-0/L1-13,
and Fork-B, if anything, **removes** a blocker the brief assumed.
