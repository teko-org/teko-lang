# Package Manager — format, consumption model, resolver, and the PK0–PK3 build-out

**Status:** DESIGN (doc-only). Consolidates the decisions the owner CLOSED on 2026-07-11
across issues **#218** (PK0–PK3), **#219** (PK4–PK7), **#180** (`.tkl` + pre-linker), and
**#506** (bindgen / consumption model). This document is the plan of record: it does **not**
implement product code — it fixes the format + the consumption model, specifies the resolver
algorithm, gives the PK0–PK3 crumb sequence with the type/function shapes an implementer copies
verbatim, and draws the line around what is deferred (PK4–PK7 + registry server + playground).

> Reading order for the reviewer: §1 recaps the closed decisions (do not re-open), §2 is the
> format + consumption model, §3 is the resolver, §4 is security / threat-model, §5 is the new
> `tool` artifact kind, §6 is the PK0–PK3 crumb sequence (the executable plan), §7 is the
> deferred track, §8 is risks + the short list of items the owner still needs to confirm.

Every design choice below is framed against the Constitution laws
(**M.0** metal · **M.1** safety/fail-loud · **M.2** explicitness/determinism · **M.3** honesty ·
**M.4** build-order · **M.5** austerity). Byte-identity is treated as a facet of **M.2**, and
"cost only for what is actually used" as a facet of **M.5** — the owner's framing in the closed
decisions.

---

## 1. The closed decisions (recap — DO NOT re-open)

| # | Decision | Source |
|---|----------|--------|
| **Container** | `.tkl` = ZIP(`<name>.tkh` + `<name>.tkb` + `<name>.tsym`); named `<name>-<version>[-<suffix>].tkl`. ZIP-STORE codec already DONE (C7.12). | #180 |
| **`.tkb`** | **Typed AST** (`checker::TCall`/`TExpr` post-checker; generics & open-types INTACT). NOT machine code, does NOT link. The vehicle of generic bodies. | #506, drain-254-L4L5 |
| **`.tkh`** | **Header + MANIFEST**: the public API (`exp` signatures + `exp` types + docs) **plus the package's own dependency declarations + version + name** (the `.tkp` metadata). One file carries both the API and the transitive-graph edges. | #218, #180 |
| **Consumption** | Rust-crate model (not C-lib). The consumer **monomorphizes the `.tkb` against real use** (M.5, per actually-used instantiation) → native → **app cache**. Cache keyed by (AST-hash, instantiation-set, target); stable use = HIT. "Almost static." | #506, #180 (Opção 1 ratified) |
| **Resolver (PK2)** | **.NET/NuGet model:** SINGLE-ALIGNED-VERSION, FLAT graph (reads the deps' `.tkh`; NO sub-sub-packages), each package **once**, zero diamond duplication; conflict = **BUILD ERROR** (no binding-redirect). Global cache holds many versions; each build aligns to one. | #218 |
| **Versioning (PK1)** | SemVer with OPERATORS `>=` `>` `<=` `<` `=` + ranges (`>1.0 <2.0`) + `^`/`~` sugar. | #218 |
| **Lockfile (PK3)** | `teko.lock` (aligned versions + integrity hashes); committed in the app. | #218 |
| **Deps (PK0)** | `[dependencies]` in `.tkp`: `name = ">=1.2 <2.0"` (registry) / `{ path = … }` / `{ git = … }`. | #218 |
| **Cache (PK5)** | Global content-addressed store `~/.teko/packages/` (read-only) + per-project `teko.lock` + per-target build cache `<target>/.teko-cache/`; verbs `teko clean` / `--no-cache` / `--cache-dir`. | #180, #218 |
| **Signing (PK7)** | Integrity (hash/checksum in lock + index) **now**; authenticity (crypto publisher signature) **later** — EXCEPT for `tool` run-time exec (see §4/§5). | #218, #219 |
| **Naming** | `@publisher/name` (registry scope) → maps to the package's canonical root, addressed `teko::<domain>::<unit>`. | #218 |
| **`ref` = the package boundary** | Cross-package Teko↔Teko mutation flows through `ref` (safe Teko↔Teko FFI); `ptr`/`&` unsafe = raw cross-*language* FFI. The boundary is an instance of the `ref` model. | owner 2026-07-11 |

The **only** item the owner left open is *server/registry: later vs parallel-now* — and the
owner's direction for THIS work is explicit: **"servidor depois, primeiro versão + definições"**
→ the registry server, the site `teko-lang.cloud`, and the wasm playground (#509) are §7
(deferred). Everything else above is settled and this doc merely consolidates it.

---

## 2. The format + the consumption model

### 2.1 The `.tkl` container

`.tkl` = a deterministic **ZIP-STORE** archive (fixed timestamps, CRC-32 per entry — the codec
that already ships as C7.12 in `teko::compress`) bundling three members:

```
<name>-<version>[-<suffix>].tkl   (ZIP-STORE, deterministic)
  ├─ <name>.tkh    header + MANIFEST — the type-check surface AND the dep graph edges
  ├─ <name>.tkb    the whole-program TYPED AST (generics as stampable templates)
  └─ <name>.tsym   symbol table (present only when applicable)
```

The container is the **distributable dependency unit**. It carries ONLY the package's own typed
tree — **never** its dependencies' AST (deps are REFERENCED, not inlined — this is what keeps the
flat graph flat and avoids diamond duplication; M.5).

### 2.2 `.tkb` — typed AST, not machine code

The `.tkb` is the checker's **typed tree** (`checker::TProgram` = `TItem`/`TFunction`/`TStatement`/
`TExpr`, with `TCall` and open type-params preserved), serialized by the program-level codec
completed in C7.16 (`src/emit/tkb_write.tks` / `tkb_read.tks` / `tkb_frame.tks`). It is **IL, not
`.o`** — it does not link. Its purpose is to be the **vehicle of generic bodies**: a `Stack<T>`
travels as a stampable template so the *consumer* can instantiate it against the consumer's own
types.

Two known codec gaps (the #165 "G1/G2") are the pre-requisite for real generic packages and are
already ratified as separate work — recorded here so the resolver design does not silently
depend on them:

- **G1** — `write_tfunction` must serialize `type_params` + `type_constraints` (needs a new codec
  for `parser::ConstraintExpr`); today a generic template crosses the `.tkb` as if concrete.
- **G2** — struct/class **method bodies** must be serialized in `write_typebody`.

The ratified implementation sequence for the generic-across-`.tkb` machinery is
**#162 → #254 → G2/#165 → #180** (see #180 and `docs/design/memory-unsafe-backend-remodel.md`).
The package-manager resolver (this doc, PK0–PK3) is **orthogonal** to G1/G2: it resolves and
pins packages regardless of whether their surface is generic; a monomorphic package works end to
end today, a generic one lights up when G1/G2 close.

### 2.3 `.tkh` — header + manifest

The `.tkh` today (`src/emit/tkh.tks`) carries only `types: []TyExport` + `fns: []FnSig` — the
public API. The owner CLOSED (2026-07-11) that the published `.tkh` must ALSO carry the `.tkp`
metadata (**name + version + the package's own dependency declarations**). This is what lets the
resolver walk the transitive graph **flat, straight from the deps' `.tkh`**, with no separate
manifest fetch. So the published `.tkh` becomes a **"header + manifest"**:

```
Header (extended)
  ├─ manifest: PkgManifest        NEW — { name, version, deps: []DepDecl }
  ├─ types:    []TyExport         (unchanged — the exp type surface)
  └─ fns:      []FnSig            (unchanged — the exp fn surface)
```

This is a codec extension (crumb PK-codec, §6) and a `.tkh` format-version bump (the reader
currently gates `ver != 1`; the manifest-carrying `.tkh` is version 2).

### 2.4 The consumption model (Rust-crate, consumer-driven monomorphization)

The consumer is the "native FFI" that **finalizes compilation**. The model, closed by the owner:

1. **Base (default):** the consumer reads each dep's `.tkb`, monomorphizes it against the
   consumer's REAL use-sites (M.5 — only instantiations actually used are stamped) → native →
   the **app build cache**. The project stays thin (it references a signed AST); the machine code
   lives in the cache. This is incremental compilation *crossing the package boundary*.
2. **Cache key** = (`.tkb` AST-hash, the set of instantiations demanded by the consumer, target
   triple). Stable use = HIT (no recompile); a changed use recompiles only the NEW instance
   ("pre-compile whenever the utilization changes" = M.5 literal).
3. **Where instances land:** in the CONSUMER's artifact/cache — **Opção 1, ratified** (#180). The
   dep's own artifact carries only its NON-generic code and is immutable/read-only; the package
   directory is never mutated by a build. All build output goes to `<target>/.teko-cache/`
   (`deps/`, `mono/`, `obj/`, `meta/`) per the #180 layout. This is the SAME model as Rust crate
   metadata + downstream monomorphization / C++ templates-in-header.

**The monomorphic/generic split (M.5, from #506):**

- A **monomorphic** surface is pure link-only: the symbol is already concrete; the consumer needs
  no body — the `.tkh`/tsym declares it and the pre-linker links the dep's archive.
- A **generic** surface is NOT link-only: `Stack<T>` instantiated with the consumer's types must
  be stamped in the consumer → it needs the BODY, which travels in the `.tkb`. This is exactly
  why the `.tkl` carries a `.tkb`.

### 2.5 Distribution spectrum (all sound by M.2)

The `.tkp` references a dep identically in all three; only WHERE the artifact comes from changes:

1. **Base** — the consumer compiles from the AST to its cache (uniform generics, thin,
   reproducible; cold-start compiles).
2. **Optimization** — a server pre-compiles `(use, target) → artifact`; the consumer DOWNLOADS on
   a hit and compiles on a miss. This is a distributed build cache, **byte-identity-gated**.
3. **Edge** — prebuilt link-only, for a package with NO generics.

### 2.6 Byte-identity (M.2) is the master wall of the whole ecosystem — highlight

Byte-identity is not a correctness nicety here; it is the **structural precondition** for the
entire distributed-cache story and it is a **security property** (see §4):

> For a server to pre-compile an artifact and for a consumer to USE that artifact instead of
> compiling locally, the two builds must produce **byte-IDENTICAL** output — otherwise the build
> is non-reproducible and the cache is unsound. This is exactly Bazel's remote cache invariant
> (sound only because hermetic + reproducible). **M.2 is the 4th door of the order-independence
> dragon and the door that JUSTIFIES fighting it: without byte-identity there is no trustworthy
> distributed cache.**

The three "don't-recompile-from-source" optimizations — incremental, link-only-deps, and
generic-from-`.tkb` — MUST all produce byte-identical output to a from-source build (M.2). Same
dragon, three doors; this is a law from the design of the package manager, not a surprise at the
first failing fixpoint. The regression bar is the existing one: temp-normalized gen1↔gen2 diff = 0
and gen2 == gen3 byte-identical (FIXPOINT).

### 2.7 `ref` = the package boundary (safe Teko↔Teko FFI)

Cross-package consumption — one Teko package handing mutable state to another — flows through
**`ref`**, the safe Teko↔Teko mutation channel. `ptr`/`&` (unsafe) remain the RAW cross-*language*
FFI. The package boundary is therefore an **instance of the `ref` model**, not a new mechanism:

- A dep's public mutable API returns/accepts `Ref<T>` across the boundary. Because the consumer
  MONOMORPHIZES the dep's `.tkb` in its own context (§2.4), the `Ref<T>` is stamped with the
  consumer's concrete `T`, arena-backed, no GC — exactly the in-process `Ref` lowering, now
  spanning a package seam. The boundary adds no runtime metadata and no ABI negotiation: it is the
  same arena/spine model, monomorphized once per canonical instance (which the single-aligned flat
  resolver guarantees — §3).
- `ptr<T>`/`&` cross the boundary only for FOREIGN (non-Teko) code, and carry the `unsafe` colour
  by type exactly as they do intra-package. A Teko↔Teko dep edge never needs them.

This is why the FLAT single-aligned resolver matters beyond dedup: ONE canonical instance of each
generic type across the whole graph means a `Ref<Foo<i64>>` handed from dep A to dep B is the
**same** stamped type — no re-instantiation, no coercion at the seam (M.2).

---

## 3. The resolver — flat, single-aligned (PK1 + PK2)

### 3.1 Versioning (PK1): SemVer + operators

A version is `major.minor.patch.build` + an optional prerelease `suffix` (the 4th field is the
BUILD number per the project's release-versioning rule; `suffix` = `alpha`/`beta`/… or `""` for a
final release). Constraints are conjunctions of operator comparators, with `^`/`~` as sugar that
DESUGARS to a pair of bounds:

- `^1.2.3` → `>=1.2.3 <2.0.0` (caret: compatible-within-major; for `0.x`, `^0.2.3` → `>=0.2.3 <0.3.0`).
- `~1.2.3` → `>=1.2.3 <1.3.0` (tilde: compatible-within-minor).
- `>1.0 <2.0` → two bounds (`Gt 1.0.0`, `Lt 2.0.0`).
- `=1.2.3` → exactly `1.2.3`.

Type/function shapes (implementer copies verbatim; new module `src/build/version.tks`, namespace
`teko::build`):

```teko
/**
 * A parsed SemVer version: `major.minor.patch.build` plus an optional prerelease suffix.
 * The 4th field (`build`) is the project's BUILD number (release-versioning rule); it is 0 when
 * the version string omits it. `suffix` is the prerelease stage ("alpha"/"beta"/...) and is ""
 * for a final release. A final release sorts ABOVE any prerelease of the same major.minor.patch.
 *
 * @since 0.3.x
 */
pub type Version = struct {
    major: u64
    minor: u64
    patch: u64
    build: u64
    suffix: str
}

/**
 * A comparison operator for a single version bound.
 *
 * @since 0.3.x
 */
pub type CmpOp = enum { Ge; Gt; Le; Lt; Eq }

/**
 * One comparator of a requirement: an operator applied to a version.
 *
 * @since 0.3.x
 */
pub type VersionBound = struct { op: CmpOp; version: Version }

/**
 * A version requirement = the CONJUNCTION of its bounds. `>1.0 <2.0` is two bounds; `^1.2.3`
 * desugars to two bounds; `=1.2.3` is one. An empty bound list accepts every version.
 *
 * @since 0.3.x
 */
pub type VersionReq = struct { bounds: []VersionBound }

/**
 * Parse a version string into a Version.
 *
 * @param s the dotted version, optionally suffixed (e.g. "1.2.3", "0.2.0.2-beta")
 * @return the parsed Version, or an error when `s` is malformed (non-numeric field, empty)
 * @since 0.3.x
 */
pub fn parse_version(s: str) -> Version | error

/**
 * Total order over versions: field-by-field (major, minor, patch, build), then a final release
 * ABOVE any prerelease of the same numeric version, then suffix lexicographic.
 *
 * @param a the left version
 * @param b the right version
 * @return -1 when a < b, 0 when equal, 1 when a > b
 * @since 0.3.x
 */
pub fn compare_versions(a: Version, b: Version) -> i64

/**
 * Parse a requirement string, desugaring `^`/`~` and splitting a space-separated range into
 * bounds.
 *
 * @param s the requirement (e.g. ">=1.2 <2.0", "^1.2.3", "=1.0.0")
 * @return the VersionReq, or an error when a comparator or version is malformed
 * @since 0.3.x
 */
pub fn parse_version_req(s: str) -> VersionReq | error

/**
 * Does a version satisfy EVERY bound of a requirement?
 *
 * @param v the candidate version
 * @param req the requirement (conjunction of bounds)
 * @return true iff `v` satisfies all bounds of `req`
 * @since 0.3.x
 */
pub fn version_satisfies(v: Version, req: VersionReq) -> bool
```

### 3.2 Dependency declarations (PK0): the `[dependencies]` grammar

`[dependencies]` grows from bare keys (today `deps: []str` in `Manifest`) to structured records.
Three source forms:

```toml
[dependencies]
"@acme/json"   = ">=1.2 <2.0"                       # registry (version requirement)
localutil       = { path = "../localutil" }          # local directory
netclient       = { git = "https://…", tag = "v1.0" } # git (tag / branch / rev)
```

Type shapes (extend `src/build/manifest.tks`, namespace `teko::build`):

```teko
/**
 * Where a dependency's package comes from. Exactly one variant case per `[dependencies]` value
 * form: a registry version-requirement string, a local `path`, or a `git` URL + ref.
 *
 * @since 0.3.x
 */
pub type RegistrySource = struct {}

/**
 * A local-directory dependency source (`name = { path = "…" }`). Full-trust (dev-controlled),
 * never fetched, never integrity-pinned against a registry hash.
 *
 * @since 0.3.x
 */
pub type PathSource = struct { path: str }

/**
 * A git dependency source (`name = { git = "…", tag|branch|rev = "…" }`). `git_ref` is the
 * pinned tag/branch/rev ("" = the remote's default branch).
 *
 * @since 0.3.x
 */
pub type GitSource = struct { url: str; git_ref: str }

/**
 * The tagged union of dependency sources (M.2 explicit — the source form is never inferred).
 *
 * @since 0.3.x
 */
pub type DepSource = RegistrySource | PathSource | GitSource

/**
 * One parsed `[dependencies]` entry: the dependency name (a `@publisher/unit` registry key or a
 * bare local name), where it comes from, and — for a registry source — the accepted version
 * requirement. `constraint` is an empty (accept-all) VersionReq for path/git sources.
 *
 * @since 0.3.x
 */
pub type DepSpec = struct {
    name: str
    source: DepSource
    constraint: VersionReq
}
```

`Manifest` gains `dep_specs: []DepSpec` (the existing `deps: []str` stays as the derived name
list for backward-compat call-sites; the parser fills both). Parsing an inline table
(`{ path = … }` / `{ git = … }`) is a small extension to the existing minimal-TOML reader in
`manifest.tks` (a new `mf_read_inline_table` helper alongside `mf_read_quoted`/`mf_read_array`).

### 3.3 The resolver (PK2): flat single-aligned algorithm

**Inputs:** the root `Manifest` (its `dep_specs`) + the read-only package store (a lookup from
`(name, version)` to a `.tkl` path). **Output:** the flat single-aligned `Resolution`.

```teko
/**
 * One node of the FLAT resolved graph: a package at its single ALIGNED version, its source, the
 * `.tkl` it resolved to, and the integrity hash covering that whole `.tkl` (PK7 integrity-now).
 *
 * @since 0.3.x
 */
pub type ResolvedDep = struct {
    name: str
    version: Version
    source: DepSource
    tkl_path: str
    integrity: str
}

/**
 * The whole resolution: exactly ONE ResolvedDep per package name (single-aligned; no diamond
 * duplication), plus the topological build order (leaves first, Kahn) the pre-linker consumes.
 *
 * @since 0.3.x
 */
pub type Resolution = struct {
    deps: []ResolvedDep
    order: []str
}

/**
 * Read the manifest block (name + version + the package's own dependency declarations) out of a
 * dependency's `.tkh` header. This is the edge-source the flat resolver walks — no separate
 * manifest fetch.
 *
 * @param h the dep's parsed `.tkh` Header (version 2, manifest-carrying)
 * @return the package's manifest block (name, version, declared deps)
 * @since 0.3.x
 */
pub fn header_manifest(h: Header) -> PkgManifest

/**
 * Resolve the whole dependency graph FLAT and single-aligned.
 *
 * Walks the transitive graph by reading each dep's `.tkh` manifest block (§2.3); collects, per
 * distinct package NAME, EVERY version requirement imposed anywhere in the graph; intersects them
 * and picks the highest available version satisfying the intersection (single-aligned). A package
 * appears exactly once. An empty intersection is a BUILD ERROR that names the conflicting
 * requirements and their imposers. A dependency cycle is a BUILD ERROR (M.1 — no hang).
 *
 * @param root the consuming project's manifest
 * @param store the read-only package store (name+version -> .tkl path + available versions)
 * @return the flat single-aligned Resolution, or a build error (conflict, cycle, or missing package)
 * @since 0.3.x
 */
pub fn resolve_deps(root: Manifest, store: PackageStore) -> Resolution | error

/**
 * The alignment check for ONE package name: find the single version satisfying ALL requirements
 * imposed on it across the whole graph.
 *
 * @param name the package name being aligned (for the error message)
 * @param reqs every VersionReq imposed on `name` (root + every dep that references it)
 * @param available the versions of `name` present in the store
 * @return the highest available version satisfying every req, or a build error listing the
 *         unsatisfiable requirements
 * @since 0.3.x
 */
pub fn align_versions(name: str, reqs: []VersionReq, available: []Version) -> Version | error
```

**Algorithm (deterministic, M.2):**

1. **Seed** the worklist from the root's `dep_specs`. Maintain a map `name → []VersionReq`
   (all requirements seen so far) and a set of visited names.
2. **Walk (flat):** pop a name; if visited, skip. Read its `.tkh` manifest block; for each of ITS
   declared deps, record the requirement under that dep's name and push the dep name. Because the
   graph is FLAT, transitive deps are merged into the SAME single namespace — there is no nesting,
   no per-parent copy (this is the anti-`node_modules` rule).
3. **Align:** for each distinct name, call `align_versions` with the accumulated requirements and
   the store's available versions → the single canonical version. Empty intersection → BUILD ERROR
   (M.1, explicit, names the imposers).
4. **Cycle check + topo-order:** build the name→name edge set from the manifest blocks; Kahn's
   algorithm yields the leaves-first `order` (and detects a cycle → honest build error).
5. **Emit** `Resolution`: one `ResolvedDep` per name (with its `tkl_path` + `integrity` hash from
   the store), plus `order`.

**Why flat + single-aligned (the law argument):** one instance per name ⇒ ONE canonical instance
of each generic type in the cache ⇒ clean monomorphization + byte-identity (M.2). Node-style
nesting would place N copies of the same type in the graph, breaking the single canonical instance
and the `ref`-boundary sameness (§2.7). The conflict-as-build-error (no binding-redirect) is M.1
fail-loud: the dev loosens or bumps a constraint rather than the resolver silently picking.

### 3.4 The lockfile (PK3): `teko.lock`

`teko.lock` PINS the aligned resolution — exact versions + integrity hashes — and is committed in
the app for reproducibility. A build with a lockfile present verifies the resolution still matches
(and re-uses the pinned versions); a mismatch (a `.tkl` whose hash differs from the pinned
integrity) is an honest error (tamper / drift detected — M.3).

```teko
/**
 * One pinned lockfile entry: the exact aligned version, a normalized source descriptor, and the
 * integrity hash covering the whole `.tkl`.
 *
 * @since 0.3.x
 */
pub type LockEntry = struct {
    name: str
    version: str
    source: str
    integrity: str
}

/**
 * The parsed `teko.lock`: one pinned entry per resolved package, in deterministic (name-sorted)
 * order so the file is byte-stable across machines (M.2).
 *
 * @since 0.3.x
 */
pub type Lockfile = struct { entries: []LockEntry }

/**
 * Serialize a Resolution to `teko.lock` text (deterministic, name-sorted, fixed formatting).
 *
 * @param res the flat single-aligned resolution
 * @return the `teko.lock` file contents
 * @since 0.3.x
 */
pub fn write_lockfile(res: Resolution) -> str

/**
 * Parse `teko.lock` text back into a Lockfile.
 *
 * @param src the `teko.lock` contents
 * @return the parsed Lockfile, or an error on a malformed entry
 * @since 0.3.x
 */
pub fn parse_lockfile(src: str) -> Lockfile | error

/**
 * Does a fresh Resolution still match the pinned Lockfile (same names, versions, integrity)?
 * A mismatch means the store drifted or a `.tkl` was tampered with — the caller fails honestly.
 *
 * @param lock the committed lockfile
 * @param res the freshly-computed resolution
 * @return true iff every resolved dep matches its pinned entry exactly
 * @since 0.3.x
 */
pub fn lockfile_matches(lock: Lockfile, res: Resolution) -> bool
```

### 3.5 Cache + monomorphization-by-use (PK5 — wires into the ratified #180 build)

The resolution feeds the build path already designed and RATIFIED in #180 — this doc does not
re-spec it, it names the seam:

- The **read-only store** (`~/.teko/packages/`, or `packages/` in the cwd today) is only READ.
- The per-target **build cache** `<target>/.teko-cache/{deps,mono,obj,meta}` holds all derived
  output: `deps/<name>-<version>.a` (the dep's non-generic archive), `mono/<name>-<version>/*.{c,o}`
  (the consumer-stamped instances — Opção 1), `obj/` (the consumer's own `.o`), `meta/*.stamp`
  (RESERVED for future incremental — honest-stop now, always regenerate).
- Build order = the resolver's `order` (leaves first); the pre-linker links
  `obj/ + mono/**/*.o + deps/*.a`, gated by reachability (#181/#220).
- CLI surface, already ratified: **`--no-cache`** (force-regenerate the build cache; does NOT
  re-fetch the store), **`teko clean`** (`rm -rf <target>/.teko-cache/`; never touches the binary
  or the store), and **`--cache-dir`** (relocate the build cache root).

The cache key is (`.tkb` AST-hash, instantiation-set, target). The lockfile's integrity hash keys
the STORE artifact (a function of the dep alone → deterministic checksum); the derived build cache
is reconstructible and does NOT enter the lockfile.

---

## 4. Security / threat-model

The package manager's security posture is a first-class deliverable, not an afterthought. Teko has
two **structural** wins here that npm and Cargo lack — assert them strongly — and a short list of
concerns the owner has decided how to handle.

### 4.1 Structural wins

- **NO arbitrary execution at build time.** A package is an AST (`.tkb`); building it is *pure
  compilation* (const-fold over a type-table; no `build.rs`, no `postinstall`, no arbitrary
  comptime). The #1 supply-chain vector of npm (`postinstall`) and Cargo (`build.rs`) **does not
  exist** in Teko. `add`-ing a dependency is safe — it only compiles. This is a direct consequence
  of the metaprogramming-out-of-LTS ruling (no general comptime) + the AST-package model, and it
  is worth stating as a headline property (M.3 honesty — Teko does not pretend the code it builds;
  it also does not RUN it).

- **Byte-identity (M.2) = a VERIFIABLE, trustless distributed cache.** Because a from-source build
  and a server-precompiled artifact must be byte-identical (§2.6), a consumer can RECOMPILE and
  check that the server's artifact matches (by hash). A compromised server serving a malicious
  artifact is therefore **detectable** — byte-identity is a *security* property, not merely a
  correctness one. This is the direct answer to "M.2 is a straitjacket": M.2 is precisely what
  makes the distributed cache trustless (the Bazel remote-cache argument).

### 4.2 What is signed (integrity now, authenticity later)

- The **integrity hash** covers the ENTIRE `.tkl` (tkh + tkb + tsym), computed at
  publish/fetch time.
- The **lockfile PINS** that hash (`LockEntry.integrity`); a build verifies every `.tkl` against
  its pinned hash (tamper detection — §3.4).
- The **registry index** carries the hash (the index entry for `(name, version)` includes the
  `.tkl` integrity), so a fetch can be checked before the artifact is trusted.
- **Authenticity** (a crypto publisher SIGNATURE over the hash — Ed25519/OpenPGP, PK7) is a LATER
  layer for registry PACKAGES — EXCEPT for `tool` run-time execution (§4.3, §5).

### 4.3 Concerns (owner's decisions)

- **Tools re-introduce arbitrary execution at RUN time (§5).** A `tool` is an executable a dev
  installs and RUNS — that is arbitrary code execution, unlike a linked library package. This is
  the ONE place authenticity must **not** be deferred: a `tool` installed from a REMOTE registry
  must be authenticity-checked (signature), not merely integrity-hashed. Law-first recommendation
  (M.1/M.3): until PK7 signing lands, installing a `tool` from a remote registry is an
  **honest-stop** (`teko` refuses with a clear "remote tool install requires signature
  verification (PK7)"), while a `tool` from a local `path=`/`git=` source — dev-controlled, same
  trust as building your own source — works now. See §8 for the confirm-this flag.
- **Typosquatting.** `@publisher/name` scoping helps (a name is owned within a publisher scope).
  The registry (deferred, §7) needs anti-squat policy + publisher identity verification. Recorded
  as a registry-layer requirement, not a compiler one.
- **Runtime capability / sandbox.** v1 = full-trust-once-installed (like Cargo/npm). FUTURE =
  capability-based execution / an opt-in **WASI-sandbox** for running tools in isolation (connects
  to the wasm backend targets N6a/N6b). Recorded as a future direction, out of PK0–PK3 scope.

---

## 5. Artifact kinds by ROLE — Teko-dep consumption vs cross-language emission

The five `Artifact` kinds split into **two distinct roles**, and they must not be confused (the
owner's clarification, 2026-07-11). The confusion to avoid is thinking a Teko package can be
"shared-linked" — it cannot.

### 5.1 Consuming a Teko dependency = ALWAYS static

A Teko package consumed by another Teko project is **always statically integrated**:

- **`package`** (`.tkl` AST) → monomorphized-by-use (§2.4) → compiled INTO the app → **static
  link**.
- **`tool`** (`.tkl` AST) → built-and-installed as a native exe (§5.3) — not linked at all.

**There is NO "shared Teko dependency."** Dynamic-linking a Teko package makes no sense, for three
law-grounded reasons:

1. **Open generics do not pre-compile into a `.so`.** `Stack<T>` has no code until the consumer's
   use-sites supply `T` — there is nothing to place in a shared object ahead of time.
2. **There is no stable ABI.** The package's realized surface depends on the CONSUMER's
   instantiations, so no fixed dynamic-symbol table exists (M.3 — a `.so` claiming to be the
   package's ABI would be pretending).
3. **It contradicts single-version + byte-identity.** The model is "recompile the exact aligned
   version" (§3) — the OPPOSITE of dynamic-swap. Dynamic linking exists precisely to swap an
   implementation at load time without recompiling; that breaks the single canonical instance
   (§2.7) and the byte-identity wall (§2.6).

So Teko↔Teko consumption is `package` (static-link) or `tool` (install) — never `shared`.

### 5.2 Emitting FOR the outside world (cross-language) = binary / static / shared

`binary`, `static` (`.a`), and `shared` (`.so`/`.dll`) are **cross-language EMISSION** kinds — the
PRODUCER side of #440 (emit a lib + C-ABI header for a C/C++/Rust/Go/… consumer). Here the surface
is a MONOMORPHIC C-ABI (generics are monomorphized at the boundary, per #440/#441/WIT), and here
`shared` DOES make sense: a C program dynamically links Teko's `.so`. This is Teko being CONSUMED
by another language — it is NOT a mode of consuming a Teko package.

| Kind | Role | Distributed as | Consumed by | Generics |
|------|------|----------------|-------------|----------|
| `binary` | executable | native exe | the OS | monomorphic (whole-program) |
| `package` | Teko dependency | `.tkl` (AST) | a Teko project (STATIC link, mono-by-use) | templates travel in `.tkb` |
| `tool` | executable dev-tool | `.tkl` (AST) | installed as native exe on PATH | monomorphic (whole-program) |
| `static` | cross-language lib | `.a` + C-ABI `.h` (#440) | C/C++/Rust/Go/… (static link) | monomorphic at the C-ABI boundary |
| `shared` | cross-language lib | `.so`/`.dll` + C-ABI `.h` (#440) | C/C++/Rust/Go/… (DYNAMIC link) | monomorphic at the C-ABI boundary |

**The rule to remember:** `shared`/`static` are cross-language emission kinds (#440), never a
Teko-package consumption mode. Consuming a Teko dependency is ALWAYS static (`package`/`tool`).

### 5.3 The `tool` kind (executable dev-tool)

A **`tool`** is a package whose role is an EXECUTABLE dev-tool / CLI (model = `cargo install` /
`dotnet tool` / `go install`), distributed as AST like any package but **built to a native
executable at install time** and placed on the bin/PATH — it is **not linked into the consuming
app**.

#### 5.3.1 Producer side — a 5th `Artifact` kind

Add `Tool` to the existing `Artifact` enum (`src/build/tkp_rule.tks`), declared exactly like the
others in `.tkp` (M.5 — reuse the enum + the existing parse path; no new mechanism):

```toml
[artifact]
kind = "tool"     # binary | static | shared | package | tool
```

Rules (law-first, consistent with the existing R-main rules):

- A `tool` **REQUIRES a `main.tks`** (it is executable — like `binary`), in contrast to
  `static`/`shared`/`package` which FORBID one.
- A `tool` is DISTRIBUTED as a `.tkl` (the AST container — same as `package`), so it can be
  published, resolved, pinned, and integrity-checked by the SAME PK0–PK3 machinery. What differs
  is its ROLE at consumption: compile-to-native-exe-and-install, not link.

#### 5.3.2 Consumer side — the `[tools]` section

Consuming a tool is declared in a NEW `[tools]` section of the `.tkp`, PARALLEL to
`[dependencies]` (M.2 explicit — a tool install is never confused with a linked dependency),
using the SAME version-requirement + `path`/`git` grammar:

```toml
[tools]
"@acme/lint"  = ">=2.0 <3.0"          # install this tool (native exe) for the project
localgen       = { path = "../gen" }   # a dev-controlled local tool
```

The resolver resolves `[tools]` entries with the SAME `resolve_deps` machinery (they are part of
the flat graph for *fetch/pin* purposes), but the build step for a `tool` is **compile-to-native-
exe + install to the bin/PATH**, not link-into-app. Because a tool runs arbitrary code, its
authenticity matters: a REMOTE-registry tool install is gated behind PK7 signing (§4.3
honest-stop); `path=`/`git=` tools are full-trust.

#### 5.3.3 Security note (restated)

A `tool` runs arbitrary code when executed → authenticity is NOT deferred for tools (§4.3). The
future WASI-sandbox option (§4.3) is the isolation path for running an untrusted tool. This is the
ONE seam where the "integrity-now, authenticity-later" default is overridden.

---

## 6. The PK0–PK3 crumb sequence (the executable plan)

Each crumb is the smallest independently gate-able step, with its key signatures (above, §3), its
regression fixtures (inputs → exit code, VM and native), and its ritual point. Fixtures for pure
logic run on the VM as `.tkt`; end-to-end build fixtures run NATIVE (per the native-test-gate
ruling — `#test` is compiled native, never VM). New modules land as compilable skeletons with
full doc-comments + honest-stops so the plan advances even before every dep closes (design-ahead).

### Crumb C1 — `version.tks`: the version model (PK1-a)

New module `src/build/version.tks` (namespace `teko::build`): `Version`, `CmpOp`,
`parse_version`, `compare_versions`. Pure functions, no compiler wiring yet.

- **Fixtures (VM `.tkt`):**
  - `V1a` `parse_version("1.2.3")` → `{1,2,3,0,""}`; `parse_version("0.2.0.2-beta")` →
    `{0,2,0,2,"beta"}`; exit 0.
  - `V1b` `parse_version("1.x")` / `""` → error path taken; exit 0 (the test asserts the error).
  - `V1c` `compare_versions(1.2.3, 1.2.4)` = -1; `compare_versions(1.0.0, 1.0.0-beta)` = 1
    (final > prerelease); `compare_versions(0.2.0.1, 0.2.0.2)` = -1 (build field); exit 0.
- **Ritual:** none required (pure additive logic, no compiler behavior change); a VM + native
  build of the new module must pass. Fixpoint is neutral (compiler does not yet use it).

### Crumb C2 — `version.tks`: requirements (PK1-b)

Add `VersionBound`, `VersionReq`, `parse_version_req` (with `^`/`~` desugar + ranges),
`version_satisfies` to `version.tks`.

- **Fixtures (VM `.tkt`):**
  - `V2a` `version_satisfies(1.5.0, ">=1.2 <2.0")` = true; `version_satisfies(2.0.0, ">=1.2 <2.0")`
    = false; exit 0.
  - `V2b` `^1.2.3` accepts 1.9.0, rejects 2.0.0; `^0.2.3` accepts 0.2.9, rejects 0.3.0; exit 0.
  - `V2c` `~1.2.3` accepts 1.2.9, rejects 1.3.0; `=1.2.3` accepts only 1.2.3; exit 0.
  - `V2d` `parse_version_req(">= bad")` → error; exit 0 (asserts error).
- **Ritual:** none required (pure). VM + native build passes.

### Crumb C3 — `manifest.tks`: `[dependencies]` with sources + constraints (PK0)

Extend `manifest.tks`: `RegistrySource`/`PathSource`/`GitSource`/`DepSource`/`DepSpec`; a
`mf_read_inline_table` helper; `Manifest.dep_specs: []DepSpec` (keep `deps: []str` derived).
Parse `[tools]` into a parallel `tool_specs: []DepSpec`.

- **Fixtures (VM `.tkt` for parse + native for round-trip through the driver):**
  - `D1` `[dependencies]` with `"@acme/json" = ">=1.2 <2.0"` → `DepSpec` registry, 2 bounds; exit 0.
  - `D2` `bar = { path = "../bar" }` → `PathSource`; exit 0.
  - `D3` `baz = { git = "https://…", tag = "v1.0" }` → `GitSource{git_ref="v1.0"}`; exit 0.
  - `D4` `[tools]` entry parses into `tool_specs`, distinct from `dep_specs`; exit 0.
  - `D5` malformed inline table (`{ path = }`) → honest error; exit non-zero.
- **Ritual:** FULL GATE — `manifest.tks` is production code the compiler reads; both engines +
  byte-identity + fixpoint gen2==gen3 (the compiler self-builds through the manifest parser).

### Crumb C4 — `.tkh` manifest block (PK-codec)

Extend `src/emit/tkh.tks`: add `PkgManifest`/`DepDecl` and a `manifest` field on `Header`; write /
read / collect it; bump the `.tkh` format version to 2 (the reader accepts 2); `header_manifest`
accessor. This is the codec change that lets the resolver read dep edges from the `.tkh`.

- **Fixtures:**
  - `H1` (VM `.tkt`) round-trip: build a `Header` with a manifest block (name/version/2 deps) →
    `emit_tkh` → `read_tkh` → identical; exit 0.
  - `H2` (VM `.tkt`) byte-fidelity: emit → load → re-emit BYTE-IDENTICAL (the C7.16 acceptance
    bar, now including the manifest block); exit 0.
  - `H3` (native) an emitted package's `.tkh` (version 2) is read back by the resolver's
    `header_manifest` and yields the declared deps; exit 0.
  - `H4` a version-1 `.tkh` (no manifest block) → honest "unsupported/legacy .tkh" OR forward-read
    with an empty manifest (decide at implementation; the honest-stop is acceptable); exit as designed.
- **Ritual:** FULL GATE — touches the serializer; both engines + byte-identity + **fixpoint**
  (the codec is exactly where byte-identity regressions hide).

### Crumb C5 — the flat single-aligned resolver (PK2)

New module `src/build/resolve_deps.tks` (namespace `teko::build`): `ResolvedDep`, `Resolution`,
`PackageStore` (an in-memory lookup abstraction so the resolver is testable WITHOUT fetch —
fetch is PK4/deferred), `resolve_deps`, `align_versions`. Design-ahead: the `PackageStore` is a
declared interface fed by an in-memory fixture today and by the real store when PK4 closes.

- **Fixtures (VM `.tkt` over synthetic in-memory headers + store):**
  - `R1` diamond A→C(`>=1.0`), B→C(`>=1.1`); store C = {1.0, 1.1, 1.2} → aligns C to **1.2** (highest
    satisfying both), ONE `ResolvedDep` for C (no duplication); exit 0.
  - `R2` conflict A→C(`<1.5`), B→C(`>=1.5`) → BUILD ERROR naming both imposers; exit non-zero.
  - `R3` cycle A→B→A → honest build error (no hang); exit non-zero.
  - `R4` `order` is leaves-first (Kahn): for A→B→C, `order` = [C, B, A]; exit 0.
  - `R5` single-aligned canonicity: two deps both requiring `Foo<i64>` from C resolve to the SAME
    C instance → one canonical stamped type (ties to #180 F1/F5 byte-identity); exit 0.
- **Ritual:** FULL GATE after the resolver is wired into the build (compiler behavior change);
  both engines + byte-identity + fixpoint. (The resolver logic alone, tested over in-memory
  stores, gates VM+native without the fixpoint dependency until it is wired.)

### Crumb C6 — `teko.lock` (PK3)

New module `src/build/lockfile.tks` (namespace `teko::build`): `LockEntry`, `Lockfile`,
`write_lockfile`, `parse_lockfile`, `lockfile_matches`; integrity hash over the whole `.tkl`
(reuse `teko::crypto::hash` / the existing FNV or a SHA-256 when `teko::crypto` is available —
honest-stop to a content hash the store already computes if crypto is not yet wired).

- **Fixtures:**
  - `L1` (VM `.tkt`) resolve → `write_lockfile` → `parse_lockfile` → `lockfile_matches` = true
    (round-trip, deterministic name-sorted output); exit 0.
  - `L2` (native) a lock pins version+hash; a store `.tkl` whose hash differs from the pin →
    integrity error (tamper detected); exit non-zero.
  - `L3` (native) lockfile present + matches → build re-uses pinned versions, output byte-identical
    to the no-lock build (reproducibility); exit 0.
- **Ritual:** FULL GATE (production build path); both engines + byte-identity + fixpoint.

### Crumb C7 — wire resolution into the build cache + CLI (PK5, into #180)

Wire `Resolution.order` into the topo-order build + the `<target>/.teko-cache/{deps,mono,obj,meta}`
layout ratified in #180; store read-only; add the `--no-cache` flag and the `teko clean` verb
(both already ratified). This crumb DEPENDS on #180's `.a` emission + pre-linker; where #180 is
not yet merged it is design-ahead against #180's declared seam.

- **Fixtures (native; reuse/extend #180 F1–F9):**
  - `C7a` build twice with unchanged use → 2nd build HITs the cache; binary byte-identical; exit 0.
  - `C7b` `teko build --no-cache` → cache regenerated from scratch; binary byte-identical to the
    plain build (F8); exit 0.
  - `C7c` `teko clean` → `<target>/.teko-cache/` removed; `<stem>` + `<stem>.c` + the store remain;
    idempotent re-run exits 0 (F7).
  - `C7d` the read-only store is not mutated during a build (F9); exit 0.
- **Ritual:** FULL GATE (the whole native build path) — both engines + byte-identity + **fixpoint
  gen2==gen3** is the wall for the "don't-recompile-from-source" invariant (§2.6). Register that
  the self-host does NOT exercise cross-package generics (the compiler is single-package) — the
  resolver/lockfile fixtures are the only coverage there; the ritual proves no regression but does
  not prove multi-package generics.

### Crumb C8 — `tool` artifact kind + honest-stops (PK0/security scaffolding)

Add `Artifact::Tool` (`tkp_rule.tks`) + the `[tools]` consumption path (compile-to-native-exe +
install), with honest-stops: remote-registry tool install → "requires signature verification
(PK7)"; the actual FETCH + PATH install → honest-stop until PK4/PK7. Local `path=`/`git=` tools
build to a native exe now.

- **Fixtures:**
  - `T1` (native) `[artifact] kind = "tool"` with `main.tks` → emits a `.tkl` (role=tool); exit 0.
  - `T2` (native) `kind = "tool"` WITHOUT `main.tks` → honest error (R-main for tools); exit non-zero.
  - `T3` (native) a `path=` tool builds to a native exe; exit 0.
  - `T4` (native) a REMOTE-registry `[tools]` entry → honest-stop "requires signature verification
    (PK7)"; exit non-zero.
- **Ritual:** FULL GATE (production build dispatch touched); both engines + byte-identity + fixpoint.

### Ritual summary

The **full gate** (both engines · paranoid · diff_vm_native · parity · fixpoint gen2==gen3) is
mandatory at crumbs **C3, C4, C5, C6, C7, C8** — every crumb that touches production compiler code
or the serializer. C1/C2 are pure additive logic (VM+native build must pass; no fixpoint
dependency). The serializer crumb (C4) and the cache-wire crumb (C7) are the two where
byte-identity regressions are most likely — treat their fixpoint as the primary bar.

---

## 7. Track posterior — OUT of scope for now (owner: "servidor depois, primeiro versão + definições")

The following are explicitly DEFERRED. They are named here so the PK0–PK3 seams (the
`PackageStore` interface, the integrity hash, the `[tools]` honest-stops) are shaped to accept them
later without redesign.

- **PK4 — fetch + content-addressed cache (network half).** HTTPS fetch, `.tkl` download +
  unzip into the read-only store `~/.teko/packages/`, content-addressing. (#219; depends on
  `teko::net` #201/#194/#197.) The `PackageStore` interface (C5) is the seam it fills.
- **PK5 — CLI verbs beyond `clean`/`--no-cache`.** `add`/`update`/`publish`/`vendor`/`tree`.
- **PK6 — registry (index / protocol).** Recommended shape = a static HTTPS index (no live
  server needed for resolution); the index carries the per-`(name,version)` integrity hash.
- **PK7 — signing (crypto authenticity).** Ed25519/OpenPGP publisher signatures over the integrity
  hash. **Exception:** authenticity for `tool` run-time execution is NOT deferred (§4.3/§5) — it
  is the honest-stop gate on remote tool installs from day one.
- **Workspaces** (multi-package repos) — a later CLI/resolver extension.
- **Registry server + site `teko-lang.cloud` + package server.** The owner's direction: server
  LATER; first the version + the definitions (this doc). The VPS (`root@187.77.42.87`,
  runner `vps-x64`, dokploy) is the eventual home.
- **Wasm playground (#509).** Rides the wasm backend targets (N6a WASI / N6b browser); out of the
  package-manager scope now.
- **Bindgen (#506).** The multi-language FFI binding generator (`--emit-bindings`) and the `.tks`
  link-only consumption path are a SIBLING track (pós-LTS for the non-`.tks` targets); the `.tkh`
  format this doc fixes is its stable input.

---

## 8. Risks, law tensions, and items for the owner to confirm

### Risks

- **R1 — self-host does not cover cross-package generics.** The compiler is a single package, so
  the ritual/fixpoint never exercises the multi-package monomorphization path. Mitigation: the C5
  resolver fixtures + the #180 F1–F6 multi-package fixtures are the ONLY coverage; the ritual
  proves no-regression but gives a false-green on multi-package generics. Registered (mirrors the
  #254 generic-methods-gap lesson: design-settled ≠ compiler-proven).
- **R2 — G1/G2 codec gaps gate the generic story, not the resolver.** PK0–PK3 (resolve/pin/lock)
  work for monomorphic packages today; generic packages light up only when G1/G2 (#165) close.
  This is a sequencing fact, not a blocker for PK0–PK3. Registered so no one blocks the resolver on
  the codec.
- **R3 — `.tkh` version bump.** Extending `.tkh` to carry the manifest block bumps its format
  version (reader currently gates `ver != 1`). Not a tension; the C4 fixture H4 pins the
  legacy-read behavior. Recorded.

### Law tensions — all resolved law-first (no HALT)

- **Flat single-aligned vs multi-version (npm-style):** resolved by the owner already (single-
  aligned, conflict=build-error) and reinforced by M.2 (one canonical instance → byte-identity) +
  M.1 (fail-loud on conflict). No tension remains.
- **Opção 1 vs Opção 2 (where mono instances land):** resolved (Opção 1, consumer cache) by the
  package-immutability rule (#180) — Opção 2 would write into the read-only package. Closed.
- **Tool authenticity vs deferred signing:** resolved law-first (M.1/M.3) — remote-registry tool
  install is an honest-stop until PK7; local `path=`/`git=` tools are full-trust. This constrains
  the tool rollout, so it is listed below for explicit confirmation.

### Items the owner should confirm (found while consolidating — none block PK0–PK3 design)

1. **`[tools]` as a distinct section vs a role on `[dependencies]`.** The syntax was left to the
   architect; this doc chooses a PARALLEL `[tools]` section (M.2 explicit — install ≠ link). Please
   confirm the section name (`[tools]`) or veto in favor of a role flag on `[dependencies]`.
2. **Remote `tool` install honest-stop until PK7.** This doc gates remote-registry tool installs
   behind signature verification (§4.3/§5.2) — a `tool` runs arbitrary code, so integrity-only is
   insufficient. Local `path=`/`git=` tools work now. Confirm this is the desired v1 posture (it
   means the FIRST usable remote tools wait for PK7).
3. **`@publisher/name` ↔ canonical-root binding.** The registry scope `@publisher/unit` maps to the
   package's canonical root (its `.tkp name`, addressed `teko::<domain>::<unit>`). The exact rule
   binding the registry scope to the declared root (must `@acme/json`'s `.tkp` declare a matching
   canonical root? is `@publisher` recorded in the `.tkh` manifest block?) is a registry-layer
   detail. Minor; can be settled when PK6 (registry) opens, but flagged so it is not forgotten.
4. **Integrity hash algorithm now.** This doc uses "the content hash covering the whole `.tkl`".
   If `teko::crypto::hash` (SHA-256) is not yet wired when C6 lands, the honest fallback is the
   store's existing content hash (FNV frame). Confirm SHA-256-when-available is acceptable, or pin
   the algorithm now.

None of these four blocks the PK0–PK3 design or the crumb sequence; items 1 and 2 are the two the
owner most likely wants to weigh in on before C8 (the `tool` crumb).

---

## Appendix — file map (where each crumb lands)

| Crumb | File(s) | Namespace |
|-------|---------|-----------|
| C1/C2 | `src/build/version.tks` (new) + `src/build/version_test.tkt` | `teko::build` |
| C3 | `src/build/manifest.tks` (extend) + `manifest_test.tkt` | `teko::build` |
| C4 | `src/emit/tkh.tks` (extend) + `src/emit/tkb_*` reuse + `tkb_test.tkt` | `teko::emit` |
| C5 | `src/build/resolve_deps.tks` (new) + `resolve_deps_test.tkt` | `teko::build` |
| C6 | `src/build/lockfile.tks` (new) + `lockfile_test.tkt` | `teko::build` |
| C7 | `src/build/project.tks` (extend — wires into #180) | `teko::build` |
| C8 | `src/build/tkp_rule.tks` (extend `Artifact`) + `src/build/project.tks` (tool dispatch) | `teko::build` |
