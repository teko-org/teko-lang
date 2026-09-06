---
seq: 0171
crumb-id: PV-C1
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [PRE-C2]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:5"        # §5.3 provenance
  - "DECISION_LOG.md:1161-1164"                                  # D133 provenance = FORMA 3
  - "src/checker/collect.tks:1354"                              # check_no_duplicate_types
  - "src/checker/check_modules.tks:227"                         # global_type_collision_at
---

# 0171 · PV-C1 — provenance tag (Base vs User) on units/decls + reserved-name redefinition gate

> The D133 enforcement: each unit/decl carries an origin — `Base` (prelude-injected) vs `User` (project
> source) — derived from `inject_runtime_prelude`. The reserved base types are defined ONCE by `Base`;
> a `User` decl reusing a reserved name collides → error. This is asset-orthogonal to the VFS
> `<project-name>::` key. Additive check (no emitted-byte change for valid corpora) → fixpoint.

## Goal

Wire provenance (annex §5.3): `inject_runtime_prelude` tags injected units `Base`, discovered project
units `User`. The reserved-name set = {`str`,`char`,`ptr`,`uptr`,`isize`,`usize`,`u8`,`[]byte`-name,…}.
Gate `check_no_duplicate_types`/`global_type_collision_at`: a `User` `TypeDecl` whose name ∈ reserved AND
a `Base` def exists → error `user program cannot redefine reserved type "<name>"`. The `Base` origin
defines them legitimately once (dogfood: the compiler's own `src/` is `User` now — it consumes the base
via injection, so it too cannot redefine, proving the gate).

## Where

- `src/build/project.tks` `inject_runtime_prelude` — stamp `Provenance = Base|User` on each `SourceFile`/
  unit (Base for the injected prelude, User for discovered).
- The AST/`TypeDecl` + `TypeTable` entry — carry the provenance tag through collect.
- `src/checker/collect.tks:1354` `check_no_duplicate_types` + `src/checker/check_modules.tks:227`
  `global_type_collision_at` — the reserved-name gate (User-vs-Base).

## How

1. `Provenance` enum `{ Base; User }`; add to `SourceFile`/unit; `inject_runtime_prelude` sets Base on the
   injected prelude units, User elsewhere.
2. Thread provenance into `TypeTable` entries in collect.
3. Reserved-name set as a checker const list.
4. In the collision check: if a `User` type name ∈ reserved and a `Base` def of that name exists → emit
   the reserved-redefinition error; the `Base`-defines-once is legitimate (not a self-collision).
5. Since the compiler's own `src/` is `User` and consumes the base defs via injection, it must NOT
   redefine `str`/`char`/etc. — verify the `src/` is already free of such redefs (BT-C1 moved them to
   Base); any residual is a real cleanup this crumb surfaces.

## Rulings & laws

- **Teko-only.**
- **D133 FORMA 3 (provenance):** the marker is origin (Base-vs-User from injection), NOT project name, NOT
  a build flag; supersedes the provisional `name=="teko"` marker.
- **Orthogonal to `<project-name>::`:** that is asset-namespacing (D135); provenance is base-vs-user.
- **NO detecting-the-nonexistent:** the gate only fires on a real reserved-name User redef.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; build gen2, `gen2==gen3` (valid corpora
  unchanged). Ratchet: ADDITIVE check → peak NOT grown.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `provenance_user_redef_str` | a User `type str = …` when Base defines `str` | `EXPECT_COMPILE_FAIL` |
| `provenance_user_own_type_ok` | a User `type mystr = []byte { … }` (non-reserved) | `0` |

## Gate

`[fixpoint]` — build gen2 + the reject/accept oracles + `gen2==gen3` (valid corpora byte-identical). "Green"
= User reserved-name redef errors, User non-reserved newtypes pass, the compiler's own `src/` (User)
compiles against the injected Base defs. Reseed-class: `fixpoint-rebuild`.

## Deps

`PRE-C2`

## Done when

Units/decls carry Base-vs-User provenance, a User reserved-name redefinition errors, non-reserved user
newtypes pass, and `gen2==gen3`.
