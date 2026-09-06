# Packages, the Manifest, and Distribution

## `teko.tkp` — the project manifest

Every Teko project is described by a `teko.tkp` file, written in **TOML** — chosen because it
is typed (no YAML-style coercion surprises), commentable, minimal, and read before any Teko
source is parsed at all (a standalone parser, no chicken-and-egg with the language it
describes).

```toml
name = "hello"
source = "src"            # the source root — invisible in addressing
version = "0.1.0.0"
suffix = "beta"
description = "An example Teko program."

[artifact]
kind = "binary"            # binary | static | shared | package

[platforms]
targets = ["linux", "macos", "windows"]

[coverage]
functions = 80
lines = 80
branches = 80

[dependencies]
teko-json = "^1.2"

[aliases]
json = "teko-json"

[extern.libs.linux]
ssl = []
```

- **`name`** is the project's canonical root namespace — every symbol addresses from it
  (`hello::main`, never a path through `source`).
- **`[artifact] kind`** is one of four: **`binary`** (a native executable, requires
  `main.tks`), **`static`**/**`shared`** (a native library for C/FFI consumers), or
  **`package`** (a `.tkl` — the distributable Teko-to-Teko dependency unit; forbids `main.tks`).
- **`[coverage]`** sets the function/line/branch floors `teko build`'s test gate enforces
  (default 80% each when the table is absent).
- **`[dependencies]`** declares other Teko packages this project consumes, with SemVer
  constraints; **`[aliases]`** binds a shorter local name to a dependency or an internal path.
- **`[extern.libs.<os>]`** maps a logical FFI library handle used in `extern fn … from "name"`
  declarations to the concrete link spec for each platform — keeping source portable while
  platform linkage detail stays in the manifest (see the developer reference's
  `ffi-and-runtime.md` for the `extern` syntax itself).

## The package format — `.tkl`

A **Teko Library File** (`.tkl`) is a ZIP container distributing a dependency:

| Inside the `.tkl` | Role |
|---|---|
| `<name>.tkh` | The `exp` interface — what a consumer type-checks against |
| `<name>.tkb` | The serialized, already-typed AST (Teko's intermediate language — *not* machine code) |
| `<name>.tsym` | Debug symbols, when present |

A package carries **only its own** typed tree — never its dependencies' — so the dependency
graph never duplicates code across levels; a consumer resolves and loads each dependency's own
`.tkl` independently. This is the same consumer-driven model Rust crates use: monomorphization
of a generic dependency happens at the **consumer's** build, specialized to how that consumer
actually uses it, not baked once into the published package.

## The build pipeline, dependency-aware

1. Every dependency (native library or `.tkl` package) loads first.
2. The current project type-checks **with** those dependencies already in scope — the whole
   program (application plus dependencies) is checked as one unit, never piecemeal.
3. Only after that validates does the artifact — a `binary`/`static`/`shared` native output, or
   a `.tkl` package for the `package` artifact kind — get emitted.

Teko-to-Teko dependencies are statically pre-linked at the **typed-tree** level (the `.tkb`
merge) — this is the deliberate alternative to dynamic linking for same-language dependencies;
FFI is reserved for genuinely foreign, non-Teko code at the unsafe boundary.

## Versioning and the resolver

Dependency versions use **SemVer** with the usual operator set (`^`, `~`, exact, ranges). The
resolver is **flat and single-aligned**: every package in the resolved graph gets exactly one
version, chosen to satisfy every constraint simultaneously — no per-branch duplicate versions
of the same package coexisting in one build, which keeps the typed-tree pre-link simple and
avoids the "diamond dependency" version-skew class of bug outright. A resolved graph is
recorded in **`teko.lock`**, checked into the project for reproducible builds.

## Security model — default-strict, layered by cost

Package consumption is secure by default, at a cost proportional to what a given consumer
actually needs to trust:

- **Layer 0 — verification-reproducible.** A package's `.tkb` can be independently
  regenerated from its published source and compared, given the same compiler version.
- **Layer 1 — integrity (mandatory).** Every published artifact carries a SHA-256 digest; a
  digest mismatch is a hard, unconditional failure — there is no "trust it anyway" path.
- **Layer 2 — transparency.** An append-only Merkle transparency log plus registry
  counter-signing makes it possible to verify offline that a given package version was really
  published, and was not quietly substituted for a different one after the fact.
- **Layer 3 — authenticity (keyless).** Publisher identity is established through a
  Sigstore/Fulcio-style keyless signing flow backed by OIDC — no long-lived private signing key
  for a maintainer to lose or leak.

Consuming a Teko dependency is **always static** (the `.tkb` typed-tree pre-link, above);
producing an artifact **for the outside, non-Teko world** may be binary, static, or shared, and
follows the ordinary FFI trust boundary rules described in the developer reference. A `tool`
artifact kind exists for executable dev-tools a project consumes at build time (declared under
`[tools]`) — arbitrary code that runs at *build* time is named as a distinct, explicitly-opted-
into risk category from an ordinary compile-time dependency.

## Publishing

`teko build --kind=package` (or `[artifact] kind = "package"` in the manifest) produces a
`.tkl` named `<name>-<version>[-<suffix>].tkl`. Publishing pushes that artifact, its SHA-256
digest, and its transparency-log entry to a registry — the same three-layer model above is
what a consumer's `teko build` verifies before ever loading the package's typed tree.
