---
seq: 0092
crumb-id: SM-S5
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [SM-G8, SM-S4]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1209-1212"   # Phase S — S5 (delete `unsafe`, last)
  - "docs/design/plano-secao6-aposentar-unsafe.md:96-102"                # PASSO 3 blast radius (contagion + modifier)
  - "docs/design/plano-secao6-aposentar-unsafe.md:181-202"               # crumbs C6/C7 (delete contagion + modifier + serialization)
  - "docs/design/plano-mestre-0.3.1-implementacao.md:252-257"            # M3 — clean removal, NO tombstone
  - "src/parser/parse_decl.tks:153"                                      # is_unsafe_modifier_at (current)
---

# 0092 · SM-S5 — DELETE the `unsafe` keyword + `is_unsafe` + contagion (last; nothing left to contain)

> DELETE the `unsafe` keyword, the `is_unsafe` AST field, and the whole contagion machinery — the LAST expurgo:
> after SM-G8 retired manual memory and SM-S4 moved FFI to opaque `ptr`, nothing remains to contain. Clean, NO
> tombstone. E1 expurgo reseed (second half).

## Goal

The final surface expurgo. By now SM-G8 retired manual memory (`mem::free`/`#must_free`/`Arena`/`RawBuf`/
`Owned<T>`) so no type is `is_unsafe = true`, and SM-S4 moved `src/` FFI to the opaque `ptr` boundary — the
entire `unsafe` contagion is DEAD CODE. This crumb DELETES it: the `unsafe` contextual modifier (parse + the
`is_unsafe` AST field + its `.tkb` serialization), and the contagion machinery
(`reject_unsafe_*`/`method_is_effective_unsafe` in `collect.tks`; `is_unsafe_type`/`unsafe_carrying*` in
`resolve.tks`; the local gate + `with_fn_unsafe` calls + `#arena_size`×`unsafe` check in `typer.tks`; the
`fn_unsafe` field + `with_fn_unsafe` in `scope.tks`). This is a byte-MOVER (parser + serialization + a
`TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION` bump). Removal is CLEAN — an `unsafe fn`/`unsafe type` program gets the
SAME generic unexpected-token error a never-existent modifier would get; **NO tombstone / "`unsafe` was retired"
diagnostic** (the M3 no-tombstone law supersedes the older secao6 §6 recommended reject-text — see Judgment
calls). `extern fn` (§G) is untouched, gaining only the "wrap a foreign `ptr` with `__wrap<T>()`" honest-stop.
Driven by the E1 expurgo reseed `{SM-S4, SM-S5}`.

## Where

Line numbers below cite the secao6 blast-radius doc (`plano-secao6-aposentar-unsafe.md` §1, base `5f24e443`);
the current tree has SHIFTED (measured: `is_unsafe_modifier_at` is now `parse_decl.tks:153`, called at `:1369`,
`:1438`). The implementer re-anchors by symbol name, not line.

- `src/parser/parse_decl.tks` — delete `is_unsafe_modifier_at` (`:153`) / `consume_unsafe_modifier` and their
  calls (`:1369`, `:1438`, and the historical `:392-394,883-885,1285,1388`).
- `src/parser/ast.tks` — remove the `is_unsafe` field from `parser::Function` and `parser::TypeDecl`
  (historically `:442,593`); sweep the ~31 `is_unsafe =` struct literals.
- `src/checker/collect.tks` — delete `reject_unsafe_signature_contagion`/`reject_unsafe_field_contagion`/
  `reject_unsafe_alias`/`method_is_effective_unsafe` + their call sites.
- `src/checker/resolve.tks` — delete `is_unsafe_type`/`unsafe_carrying*`/`func_type_unsafe_carrying`/
  `generic_args_unsafe_carrying`.
- `src/checker/typer.tks` — delete the local-unsafe gate, the `with_fn_unsafe` calls, and the
  `#arena_size`×`unsafe` check.
- `src/checker/scope.tks` — remove the `fn_unsafe` `Env` field + `with_fn_unsafe`, and every struct copy that
  propagated it.
- `src/emit/tkb_write.tks` / `tkb_read.tks` — remove the trailing `is_unsafe` I/O (the benign trailing case).
- `src/emit/tkb_frame.tks` — BUMP `TKB_EXPR_VERSION` + `TKB_PROGRAM_VERSION` (the wire changed).
- `src/lexer/lexer.tks` — update the doc-comment noting `unsafe` is no longer a contextual word.

## How

1. **Confirm nothing is `is_unsafe` (build-first).** After SM-G8 + SM-S4, no type/fn is `unsafe`-marked and no
   `src/` site uses raw pointers on the surface. The self-compile enumerating any surviving `unsafe`/`is_unsafe`
   use is the signal a prior migration was incomplete — fix it BEFORE this reseed.
2. **Delete the contagion machinery** (dead code): the `collect.tks` rejecters, the `resolve.tks` carriers, the
   `typer.tks` gate/`with_fn_unsafe`/`#arena_size`×`unsafe` check, the `scope.tks` `fn_unsafe` field. PRESERVE
   `is_unique_at` and the affine lattice (they serve the scope/cross-thread model, NOT unsafe).
3. **Delete the `unsafe` modifier + AST field** (`parse_decl.tks` + `ast.tks`); sweep the ~31 `is_unsafe =`
   literals. The checker REJECTS a struct literal with a missing field at dry build, so a missed literal is
   caught by the type system BEFORE the fixpoint — not a silent rewrite.
4. **Remove the serialization + BUMP the version.** Drop the trailing `is_unsafe` read/write; bump
   `TKB_EXPR_VERSION` + `TKB_PROGRAM_VERSION` (the R2 standard procedure — a trailing field does not shift an
   ordinal, but changing the wire requires a version bump; the reader already rejects stale artefacts aloud).
   The reseed builds from SOURCE, so the fixpoint is indifferent to the `.tkb` bump.
5. **Clean expurgo — NO tombstone.** Do NOT add any "`unsafe` was retired" rule, deprecation shim, or migration
   hint. `unsafe fn`/`unsafe type` simply fails to parse as the generic unexpected-token/unknown-modifier error.
6. **Reseed at the [RITUAL].** Build gen2 native (the corpus has no `unsafe`), FIXPOINT `gen2==gen3` over the new
   wire, reseed — the new seed no longer understands `unsafe`.

## Rulings & laws

- **Teko-only:** parser/checker/emit/lexer `.tks`; the C twins are FROZEN and `src/runtime/teko_rt.{c,h}` is the
  maintained-C exception — §6 requires ZERO edit to the runtime (its arena free is `tk_region_drop`/
  `tk_regions_free_all`, host-edge, never `mem::free`).
- **W15 full Javadoc** on any touched declaration; flatten; no inline `//`.
- **Removals = CLEAN expurgo, NO tombstone (owner ruling 3 / master-plan §M3:252-257):** no lexer token, no
  parser production, no checker rule, NO special diagnostic — the same generic error a never-existent modifier
  gets.
- **Ordering law (owner, secao6 §0):** DELETE `unsafe` LAST — after SM-G7/G8 reclassified/retired manual memory
  and SM-S4 moved FFI to opaque `ptr`; nothing remains to contain.
- **`.tkb` version bump (R2 standard procedure):** trailing removal + `TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION`
  bump; NOT a fork; reseed builds from source → fixpoint indifferent.
- **Preserve `is_unique_at` / the affine lattice** — they serve the scope/cross-thread model, not unsafe; do
  NOT delete them with the contagion.
- **Consolidated expurgo E1 `{SM-S4, SM-S5}` (master-plan §M3 note):** SM-S5 is the second half, sharing the
  migration-complete precondition with SM-S4.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build gen2 in a subshell with `ulimit -v 6815744` (a
  blown guard is a root-cause fix, never a raised ceiling); reseed ONLY at this [RITUAL]; FIXPOINT `gen2==gen3`
  byte-identical over the bumped wire; sweep `.tkt`/`.tkr` after the AST/serialization change; commit each green
  step.

## Fixtures

The self-build no longer contains `unsafe` (swept by the G-phase + SM-S4 preconditions), so it cannot assert the
reject — write the non-self-build reject guards (the no-tombstone guard is the point):

| fixture | asserts | expected |
|---|---|---|
| `unsafe_fn_rejected` | `unsafe fn f() { }` no longer parses — GENERIC unexpected-token error, NO "`unsafe` was retired" tombstone | `EXPECT_COMPILE_FAIL` |
| `unsafe_type_rejected` | `unsafe type M = struct { }` no longer parses — same generic error, no tombstone | `EXPECT_COMPILE_FAIL` |
| `unsafe_keyword_removed` | a program using the `unsafe` modifier fails as an unknown/unexpected token; a program without it still compiles | `EXPECT_COMPILE_FAIL` |

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed. Build gen2 native (the `unsafe`-free corpus), the
reject fixtures green, FIXPOINT `gen2==gen3` byte-identical over the bumped `.tkb` wire, reseed (the new seed no
longer understands `unsafe`). "Green" = `unsafe fn`/`unsafe type` no longer parse (generic error, no tombstone),
the contagion machinery is gone, `is_unique_at`/the lattice survive, gen2==gen3. **Reseed-class: expurgo.**

## Deps

`SM-G8` (manual memory retired — no type is `is_unsafe`), `SM-S4` (`src/` FFI moved to opaque `ptr` — no raw
pointer surface remains to contain).

## Done when

The `unsafe` modifier, the `is_unsafe` AST field + its serialization, and the entire contagion machinery are
deleted cleanly (NO tombstone), `is_unique_at`/the affine lattice are preserved, the `.tkb` version is bumped,
the reject fixtures pass, the fixpoint `gen2==gen3` holds, and the E1 expurgo reseed is captured — `unsafe` is
gone from the language.

## Judgment calls (flagged for the owner)

- **Law tension — RESOLVED law-first (no HALT): the secao6 §6 recommended REJECT diagnostic text is SUPERSEDED
  by the M3 no-tombstone law.** `plano-secao6-aposentar-unsafe.md:299-302` (decision 4, 2026-08-11) recommends
  specific reject strings, e.g. *"unsafe was retired in 0.3.1: memory safety is the arena's job …"*. The later
  consolidated-expurgo law (owner ruling 3, `plano-mestre-…:252-257`, 2026-08-19) mandates CLEAN removal with
  **NO "was removed" diagnostic** — a removed construct is simply unrecognised, the same generic error a
  never-existent name gets. Passes-all-Laws wins: the no-tombstone law is the binding one, so this crumb ships
  the GENERIC error and DROPS the older recommended reject-text. Reported, not converted to an issue.
- **The `assemble.tks:102` surface `mem::free` caller** (secao6 C2, an adjacent finding) belongs to the SM-G8
  manual-memory retirement, not SM-S5 — assumed already severed by SM-G8. If it survives to here, it is a signal
  SM-G8 was incomplete; fix before this reseed (build-first), do not open a new issue.
