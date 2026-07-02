# TEKO — ROADMAP: package ecosystem (registry + resolver + `teko::pkg`)

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors` (off `chore/reboot`)
>
> The ecosystem keystone. Every sibling roadmap that says "first-party PACKAGE" (web framework, GraphQL,
> OAuth server, OpenAPI, ORM, Prometheus/OTLP exporters, FFI DB drivers) presumes a way to **publish,
> resolve, fetch, and depend on** packages. This roadmap defines it. Builds directly on the existing
> `.tkl` packaging ([[teko-packaging-tkl]]) and the `.tkp` manifest.
>
> Same agent-distributable contract. Governed by the Laws; the on-demand-dependency principle (a binary
> pays only for what it imports) is the through-line, shared with DB's C7.20 and the web/cloud package
> splits.

---

## 0. What already exists to build on

- **`.tkl`** = a ZIP of `.tkh` (public header) + `.tkb` (own typed tree, deps referenced not inlined) +
  `.tsym`, named `<name>-<version>[-suffix].tkl` ([[teko-packaging-tkl]]).
- **`.tkp`** manifest (TOML): `name`, `version` (4-part `a.b.c.d` + optional `suffix`), `[artifact]`,
  `[platforms]`, `[aliases]` (reserved for `use`-aliases + dependency aliases), `[extern.libs.*]`.
- The pre-linker concept (load deps' `.tkh`+`.tkb`, merge typed trees before codegen) is already on the
  INDEPENDENCE roadmap.

Missing: dependency **declaration**, version **resolution**, a **lockfile**, a **registry** (index +
fetch), a **cache**, **integrity/signing**, and the `teko` CLI verbs. That is this roadmap.

---

## 1. Units

### ▪ PK0 — dependency declaration in `.tkp`
**Deps:** manifest parser. **Files:** `src/build/manifest.tks` (+ tkp_rule). Extend `[aliases]`/a new
`[dependencies]` table: `alias = { name = "…", version = "^1.2", registry = "…"?, git = "…"? , path =
"…"? }`. A dep is addressed in code by its **alias root** (`use httpx` → `httpx::client`), keeping the
`teko` reserved root inviolate. Sources: registry (default), git, local path (for workspaces). **Verify:**
`.tkt` manifest parse of each dep form.

### ▪ PK1 — version model + SemVer
**Deps:** PK0, `teko::math` (compare). **Files:** `src/pkg/semver.tks`. Teko versions are 4-part
`major.minor.patch.build` + `suffix` (pre-release). Define ordering, constraint grammar (`^`, `~`, ranges,
exact), and pre-release precedence. **Verify:** `.tkt` ordering + constraint-match vectors.

### ▪ PK2 — the resolver
**Deps:** PK1. **Files:** `src/pkg/resolve.tks`. Resolve the dependency graph to a single version per
package. **Decision to ratify:** **MVS** (Go-style minimal-version selection — simple, reproducible,
no SAT) vs a full SAT/PubGrub solver. *(rec: MVS first — deterministic, easy to explain, matches the
"reproducible builds" ethos; PubGrub later if diamond conflicts demand it.)* Detect + report conflicts
with a clear diagnostic (which two requirements clash). **Verify:** `.tkt` — resolution of a synthetic
graph incl. a conflict case.

### ▪ PK3 — the lockfile
**Deps:** PK2. **Files:** `src/pkg/lock.tks`. `teko.lock` (TOML): the exact resolved version + **content
checksum** (SHA-256 via `teko::crypto::hash`) + source of every transitive dep. Committed to the repo →
byte-reproducible builds. `teko build` uses the lock if present + consistent, else re-resolves and
rewrites it. **Verify:** `.tkt` lock write/read + staleness detection.

### ▪ PK4 — fetch + local cache
**Deps:** PK3, net (N5 https), `teko::compress` (unzip `.tkl`), crypto (checksum verify). **Files:**
`src/pkg/fetch.tks`. Download a `.tkl` from a registry/git, **verify the checksum**, extract into a
content-addressed local cache (`~/.teko/cache`), share across projects. Offline mode (cache-only).
**Verify:** native fetch from a loopback registry stub → cache → checksum match.

### ▪ PK5 — the registry (index + protocol)
**Deps:** PK4. **Files:** `src/pkg/registry.tks` + a spec doc. **Decision to ratify:** a **static index**
(Go-modules-style: a plain HTTPS file tree `/{name}/@v/list`, `.info`, `.tkl`, `.tksum` — no server code,
CDN-friendly, trivially mirrorable) vs a **dynamic service** (search/publish API, accounts). *(rec: static
index FIRST — zero server to operate, immutable+cacheable, matches the reproducibility ethos; a search
service is an additive layer.)* Define the on-disk/on-wire layout + integrity file. **Verify:** `.tkt` for
the index-format reader; native against a static fixture tree.

### ▪ PK6 — integrity + signing (T2)
**Deps:** PK4, crypto (C1 hash, C7 pk / C7-PGP). **Files:** `src/pkg/verify.tks`. Mandatory SHA-256
checksums (already in the lock); **optional publisher signatures** (Ed25519 / OpenPGP) + a trust policy.
A `teko.lock`-pinned checksum is the baseline; signatures add authenticity. **Verify:** `.tkt` verify
good/tampered/wrong-signer.

### ▪ PK7 — CLI verbs + workspaces
**Deps:** PK2–PK5. **Files:** driver + `src/pkg/*`. `teko add <pkg>[@ver]`, `teko remove`, `teko update`,
`teko fetch`/`teko vendor` (materialize deps locally), `teko publish` (build `.tkl` + push to a registry
target), `teko tree` (dep graph). **Workspaces:** a multi-package repo with `path` deps + one lock.
**Verify:** native `add`→resolve→lock→build against the loopback registry stub.

### ▪ PK8 — on-demand dependency wiring (ties to codegen)
**Deps:** PK2, the pre-linker. A resolved dep is **only compiled/linked into the binary if its symbols are
reachable** from the user's program (same reachability the DB C7.20 keystone defines for FFI libs, applied
to Teko packages + their transitive `[extern.libs.*]`). Importing a package that's never called adds no
code and requires none of its native libs. **Verify:** an example depending on a multi-driver package but
using one → only that path (and its lib) in the binary.

## 2. Dependency graph + tiers

```
.tkp/.tkl (exist) ── PK0 deps ── PK1 semver ── PK2 resolver ── PK3 lock ── PK4 fetch+cache ── PK5 registry
                                                   │                          │                  │
                                                   └── PK8 on-demand link      └── PK6 signing    └── PK7 CLI verbs
```

**Tiers.** **T1 (make packages usable at all):** PK0, PK1, PK2 (MVS), PK3 lock, PK4 fetch+cache, PK5
static-index registry, PK7 core verbs (`add`/`build`/`fetch`). **T2:** PK6 signing, PK8 on-demand link,
`publish`, workspaces. **T3:** search service, a PubGrub solver if needed, mirroring/proxy.

## 3. Open decisions (ratify with the sibling roadmaps in PR #80)

1. **Resolver:** MVS (rec) vs PubGrub/SAT.
2. **Registry:** static HTTPS index (rec) vs dynamic service — the static index needs no server and is
   CDN/mirror-friendly.
3. **Dependency addressing:** by alias root in `[dependencies]` (rec, keeps `teko` root reserved) — confirm
   the manifest schema.
4. **Signing:** checksums mandatory + signatures optional (rec) vs signatures required.
5. **A public default registry?** — operating one is a project/infra decision beyond the tooling; the
   format works with any static host / self-hosted mirror regardless.
6. **Version scheme:** confirm the 4-part `a.b.c.d`+suffix ordering (matches the existing `.tkp`) maps onto
   the SemVer constraint grammar cleanly.
