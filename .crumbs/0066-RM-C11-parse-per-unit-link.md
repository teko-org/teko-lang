---
seq: 0066
crumb-id: RM-C11
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C10]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:280-288"          # C11 — parse-per-unit → incomplete AST → LINK (internal FFI)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:365-435"          # §6bis the new pipeline + the internal-FFI ruling
  - "docs/design/reducao-memoria-arrays-0.3.1.md:403-409"          # visibility → linkage table (exp/pub/private)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:411-426"          # the internal FFI is RICH (Teko types + ABI), not ABI-only
---

# 0066 · RM-C11 — parse-per-unit → incomplete AST → LINK (the rich internal-FFI table)

> Restructure the frontend to parse each namespace into an INCOMPLETE AST (exp+pub decls + pending refs, no
> bodies) and add the global LINK barrier that builds the rich internal-FFI table feeding each unit's checker.

## Goal

The compiler is a monolith with mutual recursion across namespaces; today whole-program parse+check holds every
body resident — the current memory peak. Eixo C reshapes this into a STAGED pipeline whose peak is the MAX of
one stage, not the SUM (`reducao…` §6bis). C11 lays the first two stages: (1) **parse-per-unit → incomplete
AST** — each namespace parses to a compact form carrying ONLY the linkage declarations (types, fn signatures,
`const` values) of every `exp` AND `pub` decl, plus a list of pending cross-unit references; bodies are NOT
retained (dropped after extracting decls+pendings, re-parsed when the unit reaches the checker); and (2) the
**LINK barrier** — join the incomplete ASTs, build the **internal-FFI table** (the rich link table: Teko
signature + linkage symbol + ABI per `exp`+`pub` decl), resolve the pending refs, and FEED each unit's checker.
The table is TRANSITORY — it lives the link and vanishes after (never embarked in the `.tkh`). The critical
ruling: the internal FFI is **RICH, not ABI-only** (`reducao…` 411-426) — because the checker runs AFTER the
link, the table must carry the Teko-level signature (to type cross-namespace calls) alongside the symbol+ABI
(to emit + link). Additive — it coexists with whole-program; `teko.c` stays byte-identical. It drives no
teaching reseed; a `fixpoint-rebuild` swap. It depends on C10 (determinized gensym) and blocks C12.

Not blocked by any open dependency; this is executable design.

## Where

- `src/build/project.tks:352` — `frontend_parse(include_tests, quiet, tty): ParsedFront | error` — restructure
  to parse per namespace into the incomplete-AST form (decls + pendings), NOT the whole `parser::Program` of
  bodies.
- `src/build/project.tks:272` — `type ParsedFront` — extend/replace its payload with the per-unit incomplete
  ASTs + the pending-reference lists.
- `src/build/project.tks:387` — `frontend_check(pf, quiet, tty, explicit): Frontend | error` — takes the
  linked table (the internal FFI) as the cross-namespace symbol source; the per-unit body re-parse happens here
  (C12 fuses check+lower+emit; C11 only provides the linked table).
- `src/emit/tkb_frame.tks` (the `.tkb` decl projection, `reducao…` 286-288) — REUSE its exported-decl
  projection, EXTENDED to include `pub` (not only `exp`), as the incomplete-AST/internal-FFI carrier.
- NEW module skeleton: `src/build/link.tks` — the LINK barrier + the `InternalFfi` table type. New decls below.
- Existing types touched: `parser::Program`/`parser::Item` (the parse output), `checker::TProgram` (the check
  input).

## How

1. **Parse per unit into an incomplete AST** (`reducao…` §6bis step 1). Each namespace parses to: the linkage
   decls (types, fn signatures, `const` values) of every `exp` AND `pub` decl, plus a list of pending
   cross-unit reference names. Bodies (statement trees) are NOT resident — dropped after extraction,
   re-parsable by re-parsing the unit when it reaches the checker.
2. **The LINK barrier — build the RICH internal FFI** (`reducao…` §6bis step 2, 411-426). Join the incomplete
   ASTs; build the internal-FFI table keyed by `exp`+`pub` symbol, each entry carrying BOTH the Teko signature
   (param/return types, for the checker) AND the linkage symbol + ABI (for codegen + link); resolve the pending
   refs against it. Draft the W15 surface the implementer copies verbatim:

```teko
/**
 * InternalFfi — the transitory internal-FFI table the LINK barrier builds: per `exp`+`pub` declaration, the
 * RICH link entry the STAGED pipeline needs. Because the checker runs AFTER the link, each entry carries the
 * Teko signature (to type a cross-namespace call) AND the linkage symbol + ABI (to emit + resolve it) — an
 * ABI-only table would leave the checker unable to type a `pub` cross-namespace call (`reducao…` 411-426). It
 * lives the link and is DISCARDED after streaming; it is NEVER embarked in the `.tkh` (only `exp` is —
 * orthogonal; `reducao…` R8).
 *
 * @since 0.3.1
 */
type InternalFfi = struct {
    /** the link entries, keyed by qualified `exp`+`pub` symbol. */
    entries: []FfiEntry
}

/**
 * FfiEntry — one rich link entry: the Teko-level signature plus the linkage symbol and ABI for a single
 * `exp` or `pub` declaration.
 *
 * @since 0.3.1
 */
type FfiEntry = struct {
    /** the qualified symbol name (`namespace::name`). */
    symbol: str
    /** the Teko signature (param/return types) — what the CHECKER types the call against. */
    sig: @Type()
    /** the visibility deciding `.o` linkage (exp/pub = global, private never enters the table). */
    vis: Visibility
}

/**
 * link_units — the global LINK barrier: collect the incomplete ASTs of all units, build the rich internal-FFI
 * table, and resolve every pending cross-unit reference. Runs ONCE before any body is checked (the monolith's
 * mutual recursion requires the complete export set first, `reducao…` 428-435). Returns the table that feeds
 * each unit's checker in the streaming stage (C12).
 *
 * @param units  the per-unit incomplete ASTs (exp+pub decls + pending refs)
 * @return       the resolved internal-FFI table, or a link error (an unresolved cross-unit reference)
 * @throws       when a pending reference resolves to no exported/pub declaration
 * @since 0.3.1
 */
fn link_units(units: []IncompleteUnit): InternalFfi | error
```

3. **Map visibility → linkage** (`reducao…` 403-409): `exp` = global + in `.tkh` + in the internal FFI; `pub` =
   global + NOT in `.tkh` + in the internal FFI; `private` = static/local + not in the table at all. This
   mapping is registered here and reused by the native terminal (`.o` symbol table = the internal FFI on the
   linkage plane, C15).
4. **Reuse the `.tkb` decl projection** (`reducao…` 286-288), extended to include `pub`, as the incomplete-AST
   carrier — do NOT invent a new frame unless the minimal `exp`+`pub`+pendings projection needs one.
5. **Additive — coexists with whole-program.** C11 does NOT yet stream check+lower+emit (that is C12); it adds
   the parse-per-unit + LINK path while the whole-program path still produces `teko.c`. `teko.c` stays
   byte-identical (`reducao…` 288, Fixpoint).
6. **The `.tkh` stays orthogonal** — emitted separately, only `exp`, embarked in the package; the internal FFI
   (`exp`+`pub`) NEVER leaks into it (`reducao…` R8).

Reused (do NOT redeclare): `parser::Program`/`Item`, `checker::TProgram`, the `.tkb` decl projection
(`emit/tkb_frame.tks`), the `Visibility` enum (`exp`/`pub`/private).

## Rulings & laws

- **Teko-only:** the restructure lands in `src/build/{project,link}.tks` + `src/emit/tkb_frame.tks` (`.tks`);
  no C twin.
- **W15 full Javadoc** on `InternalFfi`/`FfiEntry`/`link_units` + every member; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface (the whole-program path persists
  through C11); the whole-program retire, if any, is C12's guarded byte-identical swap.
- **Owner ruling — the internal FFI is RICH, not ABI-only** (`reducao…` 411-426): carry Teko types + ABI; an
  ABI-only table is a bug (the checker cannot type a `pub` cross-namespace call).
- **Visibility → linkage (`reducao…` 403-409, `tast.tks` M.4):** `exp`→`.tkh`+global; `pub`→global, NOT
  `.tkh`; private→local. The internal FFI is `exp`+`pub`, transitory, never in `.tkh`.
- **Determinism (`reducao…` R6):** the LINK must produce a deterministic table (sorted keys) — same input →
  same table → same `teko.c` (rests on C10's determinized gensym, the dep).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the `ParsedFront`/`InternalFfi` signature changes.

## Fixtures

none — the fixpoint self-build exercises this. (The compiler IS the multi-namespace monolith the parse-per-unit
+ LINK path processes; whole-program self-build with `teko.c` byte-identical + `gen2==gen3` is the regression.
A cross-unit `pub` reference that the RICH table must type is exercised by the compiler's own source.)

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-identity, additive (the whole-program path still emits). "Green" =
the frontend parses per namespace into incomplete ASTs, the LINK builds the rich internal-FFI table (Teko types
+ ABI) and resolves every cross-unit reference, the checker types `pub` cross-namespace calls against it, and
the emitted `teko.c` is byte-identical. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing;
no reseed harvested).

## Deps

`RM-C10` (`0065` — determinized gensym; the pre-condition without which per-unit ordering would shift temp
names and diverge `teko.c`).

## Done when

the frontend parses per namespace into incomplete ASTs (exp+pub decls + pendings, no bodies), the LINK barrier
builds the rich internal-FFI table and resolves cross-unit references feeding each unit's checker, the
whole-program path still emits `teko.c` byte-identical, C12 is unblocked, and a `[fixpoint]` build is
`gen2==gen3` byte-identical.
