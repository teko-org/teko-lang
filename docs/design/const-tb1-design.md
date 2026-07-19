# T-B1 — widen the reloc model for data→data relocations (#594 Tier-B)

Status: READY-TO-IMPLEMENT (architect, 2026-07-18). Track: Tier-B pointer-bearing
aggregate → rodata (`docs/design/const-module-level-plan.md` §8 crumbs T-B1–T-B6,
§5.1 verdict, DECISION_LOG D2). Lane: the const-wave working tree (S4–S6 already
swept). **This crumb changes ZERO emitted bytes** — it is pure type widening plus a
consumer-side honest-stop scaffold that never fires today.

> Scope of T-B1 (verbatim from §8): "widen the reloc model: add a patch-site SECTION
> tag to `RelocX86` / arm64/riscv `Reloc` (today `.text`-only,
> `encode_x86_64.tks:1310`); the LIR carries a data-section relocation entry for a
> rodata-internal pointer field." T-B1 delivers the TYPES + the plumbing seam;
> emission is T-B2 (ELF `.rela.rodata`), T-B3 (Mach-O/COFF rodata relocs), T-B4
> (wasm intra-data offsets), T-B5 (VM). No producer emits a Tier-B const yet
> (`serialize_const` still honest-stops pointer-bearing aggregates — T-B6 flips it).

---

## 1. Current state (proven with lines)

### 1.1 The three reloc structs are `.text`-only

| ISA | struct | file:line | fields | kind enum |
|---|---|---|---|---|
| x86-64 | `RelocX86` | `encode_x86_64.tks:1844–1864` | `offset: u32` (`:1849`), `sym: str` (`:1854`), `kind: RelocKindX86` (`:1858`), `addend: i64` (`:1863`) | `RelocKindX86 = enum { Plt32; Pc32; Abs64 }` `minst_x86.tks:47` |
| arm64 | `Reloc` | `encode_arm64.tks:2066–2081` | `offset: u32` (`:2071`), `sym: str` (`:2075`), `kind: MRelocKind` (`:2080`) | `MRelocKind = enum { PageHi; PageLo; Call }` `minst.tks:42` — **NO absolute-pointer kind** |
| riscv64 | `RelocRiscv` | `encode_riscv.tks:1798–1821` | `offset: u32` (`:1804`), `sym: str` (`:1810`), `kind: RelocKindRiscv` (`:1815`), `addend: i64` (`:1820`) | `RelocKindRiscv = enum { Call; PcRelHi20; PcRelLo12; Branch; Jal; Abs64 }` `minst_riscv.tks:47` |

The `offset` field's doc is explicit that it is **`.text`-base-relative**
(`encode_x86_64.tks:1846–1848` "relative to this function's `.text` base";
`encode_arm64.tks:2068–2070`; `encode_riscv.tks:1800–1803`). There is no field that
says *which section the patched field lives in* — the answer is hard-wired to
`.text`. The module-level doc on each `EncodedModule*.relocs` repeats it: "every
relocation, `.text`-section-relative (module-rebased)" (`encode_x86_64.tks:1921`,
`encode_arm64.tks:2450`, `encode_riscv.tks:2286`).

### 1.2 How a rodata reference works today (text→rodata only)

- A rodata pointer is materialized **in code** at the use site: `encode_lea_rip_x86`
  (`encode_x86_64.tks:1309`) emits `lea dst,[rip+disp32]` with a zeroed disp32 and a
  reloc whose patch site is in `.text`; arm64 uses the ADRP+ADD pair
  (`isel_arm64.tks:1559`), riscv the AUIPC+ADDI pair (`encode_riscv.tks:2080`).
- The rodata blob itself is **flat bytes with no internal pointer**: `encode_rodata`
  (`encode_arm64.tks:2638`, called `encode_x86_64.tks:2425`,
  `encode_riscv.tks:2491`) lays each `LRodata` entry at a running offset and emits
  **one defined, file-local, section-2 `Symbol` per entry** (`encode_arm64.tks:2646`)
  — bytes + symbols, **never a relocation**. `ModuleRodata = { bytes; syms }`
  (`encode_arm64.tks:2600–2607`).
- The writers fold the rodata target offset **into the `.text` patch site's addend**:
  `elf_build_relas` re-targets the `.rodata` `STT_SECTION` symbol and adds the
  datum's offset to the addend (`objfile_elf.tks:490–499`, `elf_rodata_hit` `:432`);
  COFF `coff_apply_rodata_addends` folds into the `.text` site (§5.1);
  Mach-O `emit_reloc_table` (`objfile_macho.tks:600`). The neutral request type
  `ElfRelocReq` (`objfile_elf.tks:878–897`) carries only `offset` (text-relative),
  `sym`, `rtype`, `addend`. The ELF section set has `.rela.text` but **no
  `.rela.rodata`** (§5.1 verdict; `elf_section_names()` / `objfile_elf.tks:455`
  region).

### 1.3 How the LIR represents rodata today

- `LRodata = struct { symbol: str; bytes: []byte }` (`lir.tks:146`) — flat bytes, one
  final symbol, **no way to say "byte O of these bytes is a pointer to symbol S"**.
- `LGlobalAddr { symbol }` (`lir.tks:259–263`) references a rodata/global symbol from
  code; `LFieldAddr` (`:245`) + `LLoad` (`:250`) read a field off a base pointer.
- The aggregate-const materializer (S/crumb-8, already in the tree):
  `serialize_const` (`lower.tks:5137`) → `intern_aggregate_const_decl` (`:5159`) →
  `add_rodata(m, LRodata { symbol = const_rodata_symbol(cd.name); bytes })`
  (`:5162`). **`serialize_const` HONEST-STOPS any pointer/slice-bearing value**
  (`lower.tks:5142`: "pointer/slice-bearing value is Tier-B (T-B), not crumb 6").
  That honest-stop is the current Tier-B boundary on the PRODUCER side.

### 1.4 What is missing to express "a patch site INSIDE rodata pointing to rodata"

1. **No section discriminator on a reloc** — `offset` is unconditionally
   `.text`-relative; a rodata-internal patch site cannot even be named.
2. **No LIR carrier** — `LRodata` has no per-blob list of "at offset O put a pointer
   to rodata symbol S".
3. **`encode_rodata` produces only symbols, never relocs** — the consumer end has no
   path from an internal-pointer field to a `Reloc`.
4. **arm64 `MRelocKind` has no absolute-pointer kind** (x86/riscv already have
   `Abs64` with numeric mappings — `elf_reloc_type` `objfile_elf.tks:392`,
   `riscv_reloc_type` `encode_riscv.tks:746`, `coff_reloc_type`
   `objfile_coff.tks:341`; arm64 `reloc_type_value` `objfile_macho.tks:200` maps only
   PageHi/PageLo/Call). **Deferred to T-B3** (the writer that needs the number).
5. **No writer path for a rodata patch site** (`.rela.rodata` / `.rdata` reloc /
   Mach-O local rodata reloc / wasm intra-data offset) — T-B2/T-B3/T-B4.

T-B1 delivers #1, #2, and the #3 seam (as a honest-stop). #4/#5 are the later
writer/VM crumbs.

### 1.5 Codec audit (risk #5, answered)

`serialize_program` (`emit/tkb_frame.tks:424`) serializes **`checker::TProgram`** — the
typed AST. `Reloc*`, `EncodedModule*`, and `LModule`/`LRodata` are produced strictly
DOWNSTREAM (lower → encode → objfile) and consumed in-process to emit bytes; **none of
them cross `serialize_program`/`deserialize_program`** (grep for `LModule`/`LRodata`
serialization in `src/emit/*.tks` is empty). Therefore widening `RelocX86`/`Reloc`/
`RelocRiscv` **cannot** touch any `.tkb`. Widening `LRodata` cannot touch the codec
either (`LModule` is never serialized). `.tkc` references (`build/project.tks`,
`codegen/codegen.tks`) are the C-backend object cache, not a `Reloc*` serialization.
**Conclusion: the widening is codec-inert.**

---

## 2. Exact signatures to add (full-Javadoc, copy verbatim)

### 2.1 `RelocSect` — the patch-site section tag (`src/backend/minst.tks`)

Placed in `minst.tks` (namespace `teko::backend`, home of `MRelocKind` at `:42`), so
all three encoders — which share the `teko::backend` namespace and reference
`MRelocKind` unqualified — see it without a new `use`.

```teko
/**
 * RelocSect — which section a relocation's PATCH SITE lives in (#594 T-B1). Until
 * now every relocation patched a field in `.text` (a text→rodata/text→text
 * reference), so the section was implicit; a pointer-bearing rodata const needs a
 * relocation whose patched field is INSIDE the read-only data section (a data→data
 * pointer), so the section must become explicit. `Text` reproduces the historical
 * behavior byte-for-byte (`offset` is `.text`-base-relative); `Rodata` means the
 * `offset` is `.rodata`/`__const`-base-relative and the writer must patch the data
 * section (emitted in T-B2..T-B5, never in T-B1).
 *
 * @since #594 T-B1
 */
pub type RelocSect = enum { Text; Rodata }
```

### 2.2 `sect` field on each reloc struct (default `Text` everywhere)

Add ONE field, doc'd, to each of the three structs. Every existing literal sets
`sect = RelocSect::Text` (Teko struct literals list every field, so the compiler
flags each site — the mechanical driver). The rebasing helpers PRESERVE `r.sect`.

`RelocX86` (`encode_x86_64.tks:1844`) gains, after `addend`:

```teko
    /**
     * sect — which section this relocation's patched field lives in (#594 T-B1):
     * `Text` (the historical default — `offset` is `.text`-base-relative), or
     * `Rodata` (a data→data pointer whose `offset` is `.rodata`-base-relative,
     * emitted only from T-B2 on). Every reloc the encoder builds today is `Text`.
     */
    sect: RelocSect
```

Same field + doc on arm64 `Reloc` (`encode_arm64.tks:2066`, after `kind`) and riscv
`RelocRiscv` (`encode_riscv.tks:1798`, after `addend`), adjusting `.text`→`__text`
wording for arm64.

Literal edits (add `sect = RelocSect::Text`, or `sect = r.sect` in a rebaser):

- x86: `encode_x86_64.tks:2131` (build), `:2327` (`rebase_relocs_x86` → `sect = r.sect`).
- arm64: `encode_arm64.tks:2340` (build), `:2504` (`rebase_relocs` → `sect = r.sect`).
- riscv: `encode_riscv.tks:2199` (build), `:2322` (`rebase_relocs_riscv` → `sect = r.sect`).

> `MRelocKind`/`RelocKindX86`/`RelocKindRiscv` are **NOT touched in T-B1**. The
> absolute-pointer kind a data reloc needs already exists for x86 (`Abs64`) and riscv
> (`Abs64`); arm64 gains it in **T-B3** (with its `reloc_type_value` number). T-B1
> never constructs a `Rodata` reloc, so no kind is needed yet.

### 2.3 `LDataReloc` + widened `LRodata` (`src/lir/lir.tks`)

```teko
/**
 * LDataReloc — one rodata-INTERNAL pointer field (#594 T-B1): the byte `offset`,
 * within the owning `LRodata` blob, of a pointer-width slot that must be relocated
 * to hold the address of the rodata datum named `target`. This is the LIR carrier
 * for the data→data relocation the current toolchain cannot yet emit (§5.1); a
 * Tier-B aggregate const (e.g. an ABI descriptor with `[]u32` slice fields) will
 * produce one per slice/pointer field in T-B6. No lowering produces one in T-B1, so
 * every `LRodata.relocs` is empty and the backend behavior is byte-identical.
 *
 * @since #594 T-B1
 */
pub type LDataReloc = struct { offset: u32; target: str }

/**
 * data_reloc — build an `LDataReloc` for a pointer slot at byte `offset` inside a
 * rodata blob that targets rodata symbol `target` (#594 T-B1).
 *
 * @param offset  the pointer slot's byte offset within its `LRodata` blob
 * @param target  the final rodata symbol name the slot must point to
 * @return        the data-relocation entry
 * @since #594 T-B1
 */
pub fn data_reloc(offset: u32, target: str) -> LDataReloc {
    LDataReloc { offset = offset; target = target }
}
```

`LRodata` (`lir.tks:146`) widens with a `relocs` list (default empty preserves the
flat-bytes behavior):

```teko
/** LRodata — one read-only data entry: its already-final symbol, the raw bytes (a
    string literal's UTF-8, an interpolation piece, or a Tier-A aggregate const's
    serialized image), and its internal pointer relocations (#594 T-B1 — EMPTY for
    every entry the compiler produces today; a Tier-B aggregate const populates it
    from T-B6, and T-B2..T-B5 teach the writers/VM to emit/resolve them). Referenced
    by `LGlobalAddr` (A1-4, #382). */
pub type LRodata = struct { symbol: str; bytes: []byte; relocs: []LDataReloc }
```

`LRodata` literal edits (add `relocs = teko::list::empty()`):

- `lower.tks:3888` (`intern_rodata`), `lower.tks:5162` (`intern_aggregate_const_decl`).
- test helpers: `lir_interp_test.tkt:432`, any `lower_test.tkt` / `stackify_test.tkt`
  `LRodata { … }` builder (the compiler enumerates them).

> `serialize_const` (`lower.tks:5137`) and its Tier-B honest-stop (`:5142`) are
> **UNCHANGED** in T-B1. It remains the producer-side gate: because it still
> honest-stops pointer-bearing aggregates, no `LRodata` ever carries a non-empty
> `relocs`. T-B6 replaces that honest-stop with a serializer that emits bytes +
> `data_reloc` entries.

### 2.4 `encode_rodata` — the consumer seam (honest-stop, all three encoders)

`encode_rodata` gains a scan of each entry's `relocs`; if any is non-empty it
honest-stops (a rodata-internal pointer needs the T-B2..T-B5 writer/VM path). Its
return type becomes `ModuleRodata | error` (arm64 `encode_arm64.tks:2638`) and the
x86/riscv analogs; the three `encode_module*` callers (`encode_x86_64.tks:2425`,
`encode_arm64.tks:2735`, `encode_riscv.tks:2491`) match the new arm. Because every
`relocs` is empty today, the honest-stop is unreachable and the output is
byte-identical.

```teko
/**
 * honest_data_reloc — the named honest-stop for a rodata-INTERNAL pointer relocation
 * (#594 T-B1): a Tier-B aggregate const whose rodata image contains a pointer needs a
 * data→data relocation the object writers/VM cannot emit until T-B2..T-B5 (§5.1). No
 * const produces one yet (`serialize_const` honest-stops pointer-bearing aggregates
 * upstream), so this never fires in T-B1 — it is the placed seam T-B2 replaces with
 * real `.rela.rodata` emission.
 *
 * @return  the located Tier-B honest-stop error
 * @since #594 T-B1
 */
fn honest_data_reloc() -> error {
    error { message = "rodata-internal pointer relocation: emission lands in T-B2..T-B5 (#594 T-B)" }
}

/**
 * rodata_has_internal_relocs — whether any entry of `rodata` carries a non-empty
 * internal-pointer relocation list (#594 T-B1). Always false today; the guard that
 * makes `encode_rodata` honest-stop the moment a Tier-B const starts producing them.
 *
 * @param rodata  the module's interned rodata table
 * @return        true iff some entry has an `LDataReloc`
 * @since #594 T-B1
 */
fn rodata_has_internal_relocs(rodata: []lir::LRodata) -> bool {
    mut i: u64 = 0
    loop {
        if i >= rodata.len { break }
        if rodata[i].relocs.len > (0 to u64) { return true }
        i++
    }
    false
}
```

`encode_rodata` body gains, before the `ModuleRodata { … }` return:

```teko
    if rodata_has_internal_relocs(rodata) { return honest_data_reloc() }
```

and its signature/callsite:

```teko
fn encode_rodata(rodata: []lir::LRodata) -> ModuleRodata | error
```

```teko
    let rod = match encode_rodata(m.rodata) { ModuleRodata as x => x; error as e => return e }
```

(the x86/riscv variants return `ModuleRodataX86`/`ModuleRodataRiscv` — match those
names as they exist).

### 2.5 Writer defensive honest-stop (scaffold, never fires)

So T-B2/T-B3 have a single obvious edit point AND so a future stray `Rodata` reloc
can never silently be written as a `.text` reloc, add a defensive guard to each
`*_reloc_reqs` / reloc-table builder — it references the new `sect` field, compiles
today, and cannot fire (every reloc in `EncodedModule.relocs` is `Text`, because
`encode_functions` produces text relocs and `encode_rodata` honest-stops before it
could add a `Rodata` one):

- `x86_reloc_reqs` (`objfile_elf.tks:988`): in the loop, `if r.sect ==
  backend::RelocSect::Rodata { … }` → return an ELF `.rela.rodata` honest-stop
  (T-B2's seam). Because the function returns `[]ElfRelocReq` (no error arm), make
  the guard bail via the caller: simplest is to keep T-B1's guard at the
  `encode_rodata` boundary (§2.4) ONLY and add the writer guard in T-B2 when
  `x86_reloc_reqs` gains its `| error`. **Recommended: T-B1 places the guard ONLY at
  `encode_rodata`; the writers stay untouched** (they only ever receive `Text`
  relocs, proven by the `encode_rodata` gate). This keeps T-B1 to the encoder layer
  and leaves the `.rela.rodata` writer edit wholly inside T-B2.

> Decision (law-first, smallest safe step): T-B1's honest-stop lives at
> `encode_rodata` (the point where a data reloc would first be constructed), NOT in
> the writers. Rationale: it is the single narrowest choke point, needs no writer
> signature change, and leaves T-B2/T-B3 as self-contained writer crumbs. The writer
> `sect` handling is introduced by the crumb that actually emits it.

---

## 3. Compatibility proof (zero bytes)

1. **`RelocSect::Text` is set at every construction site** (§2.2), and the rebasers
   copy `r.sect`. No reloc is ever `Rodata` in T-B1 (no producer). The writers read
   `offset`/`sym`/`kind`/`addend` exactly as before; the new field is inert.
2. **`LRodata.relocs` is `empty` at every construction site** (§2.3). `encode_rodata`
   lays bytes + section-2 symbols identically; `rodata_has_internal_relocs` returns
   false, so `honest_data_reloc` is unreachable and `ModuleRodata` is unchanged.
3. **`serialize_const`'s Tier-B honest-stop is untouched** — the producer gate stays
   closed, guaranteeing #2.
4. **Codec-inert** (§1.5): `Reloc*`/`LRodata` never cross `serialize_program`, so no
   `.tkb` byte moves; `.tkc` object cache is unrelated.
5. **Guards:** the existing byte-exact tests are the proof — `encode_x86_64_test.tkt`,
   `encode_arm64_test.tkt`, `encode_riscv_test.tkt` (instruction/module goldens),
   `objfile_elf_test.tkt`, `objfile_elf_riscv_test.tkt`, `objfile_macho_test.tkt`,
   `objfile_coff_test.tkt` (object-file goldens), `lower_test.tkt`,
   `lir_interp_test.tkt`, `tkb_test.tkt` — must ALL stay byte-for-byte green, and
   **fixpoint gen2==gen3** is the final proof that the self-hosting compiler emits
   identical bytes.

---

## 4. Sequence of edits (file-by-file, with a regression point per edit)

Each edit is independently gate-able; run the listed `.tkt` after each.

| # | File | Edit | Regression gate (`.tkt`) |
|---|---|---|---|
| E1 | `src/lir/lir.tks` | `LDataReloc` type + `data_reloc` helper (§2.3); widen `LRodata` with `relocs` | `lower_test.tkt`, `lir_interp_test.tkt` compile + pass; `tkb_test.tkt` (proves codec still round-trips TProgram, unaffected) |
| E2 | `src/lir/lower.tks` | add `relocs = teko::list::empty()` to `LRodata` literals `:3888`, `:5162`; leave `serialize_const:5142` honest-stop unchanged | `lower_test.tkt` (esp. the aggregate-const rodata test near `:877`), `lir_interp_test.tkt` |
| E3 | `src/backend/minst.tks` | `RelocSect` enum (§2.1) | `minst_test.tkt` compiles |
| E4 | `src/backend/encode_x86_64.tks` | `sect` on `RelocX86` (`:1844`); `sect = RelocSect::Text` at `:2131`; `sect = r.sect` at `:2327`; `encode_rodata` guard + `\| error` + callsite `:2425` | `encode_x86_64_test.tkt`, `objfile_elf_test.tkt`, `objfile_coff_test.tkt` (all goldens byte-identical) |
| E5 | `src/backend/encode_arm64.tks` | `sect` on `Reloc` (`:2066`); `sect = RelocSect::Text` at `:2340`; `sect = r.sect` at `:2504`; `encode_rodata` guard + `\| error` + callsite `:2735` | `encode_arm64_test.tkt`, `objfile_macho_test.tkt` |
| E6 | `src/backend/encode_riscv.tks` | `sect` on `RelocRiscv` (`:1798`); `sect = RelocSect::Text` at `:2199`; `sect = r.sect` at `:2322`; `encode_rodata` guard + `\| error` + callsite `:2491` | `encode_riscv_test.tkt`, `objfile_elf_riscv_test.tkt` |
| E7 | test helpers (`lir_interp_test.tkt:432`, `lower_test.tkt`, `stackify_test.tkt`, `objfile_coff_test.tkt:117` `co_noreloc_module` if it builds `RelocX86`/`LRodata`) | add the new default fields to test-side literals | the touched `.tkt` themselves |

> The writers (`objfile_elf.tks`, `objfile_elf_riscv.tks`, `objfile_macho.tks`,
> `objfile_coff.tks`) are **NOT edited in T-B1** (§2.5 decision). They are T-B2/T-B3.

---

## 5. Regression fixtures to ADD (inputs → expected, VM and native)

All three are unit `.tkt` tests calling the backend directly, so they run identically
under the VM and the native harness (same `error`/exit outcome).

1. **The gate fires (new behavior):** hand-build an `LModule` with one `LRodata {
   symbol = "k"; bytes = <8 zero bytes>; relocs = [data_reloc(0, "other")] }` and call
   `encode_module_x86` / `encode_module` (arm64) / `encode_module_riscv`. **Expected:
   the `error` arm** (`honest_data_reloc`, message contains "T-B"). Add to
   `encode_x86_64_test.tkt`, `encode_arm64_test.tkt`, `encode_riscv_test.tkt`. This is
   the ONE test that proves the new field is wired end-to-end to the honest-stop.
2. **Default is inert (byte-identity):** build the same module with `relocs =
   teko::list::empty()` and assert `encode_module*` returns `ModuleRodata`/
   `EncodedModule*` (the `ok` arm) whose `rodata` bytes equal the pre-T-B1 golden for
   that blob — i.e. an empty `relocs` list encodes byte-for-byte as before.
3. **Reloc default tag:** in each `encode_*_test.tkt`, after encoding a function that
   references a rodata symbol (an existing golden case), assert every produced
   `Reloc*.sect == RelocSect::Text`. Guards against a missed `sect = …` at a literal.
4. **LIR default:** in `lower_test.tkt`, assert a freshly interned `LRodata` (via a
   string literal lowering) has `relocs.len == 0` — guards `intern_rodata`.

No existing fixture changes its expected exit code; the whole existing golden corpus
is the byte-identity guard.

---

## 6. Ritual points

- **Per-edit:** the file's own `.tkt` gate (table §4) — each edit is gate-able alone.
- **RITUAL POINT — end of T-B1:** the FULL gate (every backend golden byte-identical
  + `lower`/`lir_interp`/`tkb` suites + **fixpoint gen2==gen3** + both engines). Zero
  bytes changed, so a green full gate IS the compatibility proof. **No seed bump** —
  T-B1 adds no capability the corpus uses (the 🔑 SEED BUMP #3 is after T-B5, §8).

---

## 7. Risks + law tensions (with resolution)

1. **A missed `sect = RelocSect::Text` at a literal → uninitialized/garbage tag →
   a `Rodata`-tagged reloc leaks to a writer → wrong bytes.** *Resolution:* Teko
   struct literals require every field, so the compiler enumerates all six reloc
   sites and every `LRodata` site; fixture §5.3 asserts `sect == Text` on real output.
   Cannot silently regress.
2. **`encode_rodata` signature change (`ModuleRodata` → `| error`) ripples to 3
   callers + any direct test caller.** *Resolution:* the change is mechanical (a
   `match`); the three `encode_module*` sites are listed (§2.4); goldens catch any
   behavioral drift. Small, contained.
3. **Serialized-struct leak to `.tkb`/`.tkc` (the explicit audit ask).** *Resolution
   (proven §1.5):* `Reloc*` and `LRodata`/`LModule` never reach
   `serialize_program` (which serializes `checker::TProgram` only). The widening is
   codec-inert; `tkb_test.tkt` stays green as the guard.
4. **arm64 lacks an absolute-pointer `MRelocKind`.** *Resolution:* NOT a T-B1 problem
   — T-B1 never constructs a `Rodata` reloc (it honest-stops first), so no kind is
   needed. arm64 `MRelocKind::Abs64` + its `reloc_type_value` number is added in
   **T-B3** (the Mach-O writer that emits it). Recorded here so T-B3 does not
   rediscover it.
5. **Redundant gate (both `serialize_const` and `encode_rodata` honest-stop).**
   *Soft tension, not a HALT.* Keeping both is intentional defense-in-depth across the
   producer/consumer boundary: T-B6 opens the producer gate, T-B2 opens the consumer
   gate; until both are open the tier is closed at either end. Passes all laws.

No genuine unresolved tension → no HALT.
