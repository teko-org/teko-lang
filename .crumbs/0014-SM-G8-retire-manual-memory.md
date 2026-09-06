---
seq: 0014
crumb-id: SM-G8
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: ["SM-G7"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:405-416"  # §6.5.2 E/F removable
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:452-457"  # §6.5.4 steps 2-3
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1183-1185"# §10 Phase G — G8
---

# 0014 · SM-G8 — retire manual memory (`mem::free`/`#must_free`/`Arena`/`RawBuf`/`Owned<T>`)

> Retire manual memory (`mem::free`/`#must_free`/`Arena`/`RawBuf`/`Owned<T>`); migrate call-sites.

## Goal

The arena model makes manual memory OBSOLETE with no safety residual. `#must_free`'s entire job is
barring a region LEAK — but the arena model STRUCTURALLY CANNOT leak a region (every region drops at its
scope, at a DI `scoped` boundary, or at a phase boundary). `mem::free` on a `[]T`/class instance is
manual heap free — obsoleted by arena drop. `Arena` (the non-lexical manual region) reshapes into a
SCOPE (lexical region or DI `scoped` lifetime). `RawBuf` (raw ptr+len) becomes a safe arena-backed
`[]byte`; `Owned<T>`'s move-only ownership is now the DPS/ownership model (SM-A2, §1.1/§8). This crumb
DELETES the `must_free` dataflow, the `mem::free` checker path, and the two `src/mem/unsafe` files, and
MIGRATES the few call sites to lexical/DI-scoped regions. `unsafe` STILL PARSES (its deletion is SM-S5).
Mechanical + contained (two files + few call sites); depends on SM-G7 (safe intrinsics first). Its seed
folds into SM-R1.

## Where

- `src/checker/typer.tks:3437-3600` — the `must_free_consumed_on_all_paths` dataflow — DELETE.
- `src/checker/typer.tks:941-969` — the `mem::free` checker path (`:955` `mem::free` on `[]T`/class) — DELETE.
- `src/parser/ast.tks:595` — `must_free` field — DELETE.
- `src/mem/unsafe/arena.tks` — DELETE (the `Arena` `#must_free unsafe type`; the known aliased-UAF gap,
  `arena.tks:9-15`, disappears with its subject).
- `src/mem/unsafe/rawbuf.tks` — DELETE (`RawBuf`→`[]byte`, `Owned<T>`→DPS ownership).
- The manual-region call sites — migrate to a lexical region / DI `scoped` lifetime.
- `unsafe` keyword — UNTOUCHED here (parses as no-op; deleted in SM-S5, `0092`).

## How

1. **Delete the `must_free` dataflow** (`typer.tks:3437-3600`) and the `ast.tks:595` `must_free` field.
   Its subject (a region leak) cannot occur under the arena model — the detection is DEAD CODE to REMOVE
   (CLAUDE.md "NÃO DETECTAR/BARRAR O QUE NÃO EXISTE"), not to keep. The `arena_manual_leak` rejection
   test RETIRES (the arena cannot leak, so the rejection has no subject).
2. **Delete the `mem::free` path** (`typer.tks:941-969`): manual heap free is obsoleted by arena drop.
3. **Delete `src/mem/unsafe/arena.tks` + `rawbuf.tks`.** Reshape `Arena` uses into a SCOPE (lexical region
   or DI `scoped`); `RawBuf`→arena-backed `[]byte`; `Owned<T>`→DPS move-on-return ownership.
4. **Migrate the call sites.** The compiler's own unsafe surface is tiny (two files + the `teko::mem`
   builtin declarations); rewrite each manual-region site to a lexical/DI-scoped region. Mechanical and
   contained. A truly-dynamic data-dependent free point (neither lexical nor a DI scope — rare) reshapes
   to "make it a scope."
5. **Keep `unsafe` parsing.** Do NOT delete the keyword here — SM-S5 (`0092`) deletes it last, after the
   sweep removes every `unsafe`/`#must_free`/raw-type occurrence.
6. **Confirm byte-neutrality.** Steps change bytes only for the two `src/mem/unsafe` files and their few
   call sites — mechanical, fixpoint-gated. After the migration a `[dry]` build over the reshaped sites is
   byte-stable; the deletions are of already-unused surface elsewhere.

No new `fn`/`type` — this crumb REMOVES manual-memory surface and migrates call sites.

## Rulings & laws

- **Teko-only:** checker/parser/mem `.tks`; NO `teko_rt.c` — the manual-memory machinery is dead code to
  REMOVE, its runtime residue routes through the arena (CLAUDE.md "NADA em teko_rt.c PRO EXPURGO").
- **Removals = clean expurgo, NO tombstone:** `#must_free`/`mem::free`/`Arena` become simply
  UNRECOGNISED; `arena_manual_leak` retires (no subject). No "was removed" diagnostic.
- **NÃO DETECTAR O QUE NÃO EXISTE:** the `must_free` dataflow is dead detection under the arena model —
  remove the root, not the message.
- **W15:** touched declarations keep Javadoc.
- **Depends on SM-G7** (region primitives reclassified as safe intrinsics — the arena-scoped replacement
  must be a safe intrinsic before manual memory retires).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The reshaped `src/` scopes are exercised by the fixpoint, but the "region drops at scope, no `#must_free`
needed" behavior and the retired leak-reject need an isolated oracle:

| fixture | asserts | expected |
|---|---|---|
| `must_free_removed` | `#must_free` / `mem::free` no longer parse; region drops at scope | 0 |

(`arena_manual_ok` reshaped to a scope is folded into this fixture; `arena_manual_leak` is RETIRED — its
subject no longer exists.)

## Gate

`[dry]` — compile + `must_free_removed` + fixpoint (byte-identical over the reshaped sites). "Green" =
the `must_free` dataflow + `mem::free` path + `Arena`/`RawBuf`/`Owned<T>` are gone, call sites reshaped to
scopes, region drops at scope, `unsafe` still parses (no-op), build byte-stable. Reseed-class: `(folds R1)`.

## Deps

`SM-G7` (safe region intrinsics must exist as the replacement before manual memory retires).

## Done when

The `must_free` dataflow, `mem::free` path, and `src/mem/unsafe/{arena,rawbuf}.tks` are deleted, the few
manual-region call sites are migrated to lexical/DI-scoped regions, `#must_free`/`mem::free` no longer
parse (no tombstone), `arena_manual_leak` is retired, `must_free_removed` is exit `0`, and `unsafe` still
parses as a no-op.
