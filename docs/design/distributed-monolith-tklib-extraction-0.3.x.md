# Distributed-monolith stdlib extraction (`./tklib`) — keystone study (0.3.x)

> **Status:** DESIGN-ONLY / plan-shaping. No product code, no build, no reseed. This study answers
> the owner's 2026-08-20 proposal (extract the ENTIRE stdlib into `./tklib`, leave the monolith
> compiler-only, auto-link the stdlib always) and lands a GO/NO-GO + sequencing recommendation for
> the coordinator to bring to the owner BEFORE any execution. Code-grounded against `src/` on
> `arch-tklib-extraction` (off `fix/retirement`).

---

## 0. VERDICT (read this first)

**GO — but SPLIT by D54 (teach-now / use-later), NOT as a 0.3.1 memory pivot.**

- **Fixpoint-survival verdict: SURVIVES.** The `gen2 == gen3` invariant holds across the move,
  provided four determinism conditions (all either already true or cheaply pinnable): (1) `.tkl` is
  byte-deterministic — **CONFIRMED** (fixed DOS date `0x5A21` + STORE method, no mtime); (2) the
  `.tkb` serialize/deserialize round-trip is lossless+deterministic — must be PINNED by a new
  carve-fixpoint gate; (3) the extracted package keeps `name = "teko"` so every fully-qualified name
  stays byte-identical — **this is the load-bearing decision**; (4) the dep-merge order is stable.
  The invariant becomes a DOUBLE fixpoint (the stdlib `.tkl` must reproduce AND the compiler must
  reproduce), but both reduce to the determinism the current gate already assumes.

- **Memory verdict: extraction is NOT a path to ≤1.5 GB on the C route.** On the only route
  available today (C), the linked stdlib `.tkb` is merged at the typed-tree level and **RE-EMITTED
  into the single `teko.c` monolith** — so the codegen output-buffer elephant (COL-F0,
  `tk_slice_push_r` in the `cb` emit buffer = ~93% of peak) is **untouched**. Extraction only removes
  the stdlib's front-end parse+check AST (a minority of peak), and even that is partly offset by
  loading+deserializing the `.tkb`. **COL-F0 / RM-C remains THE mandatory lever.** Extraction's real
  codegen-memory payoff only materializes on the native / separate-object-per-namespace route, which
  is **D52-gated** (native is last).

- **Sequencing:** the owner's explicit prerequisite ("learn NOW to link `.tkb` + pass the fixpoint
  with the stdlib as a linked package, on the C route, BEFORE the move") maps exactly onto **D54**.
  **TEACH NOW (0.3.1, leaf, zero reseed perturbation):** harden the consumption path + add the
  dormant auto-link built-in + a **carve-fixpoint CI gate** that proves `gen2 == gen3` through a
  linked stdlib package **without physically moving any file and without changing the default
  self-build**. **MOVE LATER (0.3.2, gated on the triple D52 marco + a settled reseed):** relocate to
  `./tklib`, flip auto-link on for the compiler's own build, reseed. The full codegen-memory benefit
  is itself deferred to the native era (D52).

**Why NOT a 0.3.1 pivot:** extraction buys ≈0 memory on the C route, so it cannot ride the memory
campaign's justification; and physically moving + flipping auto-link perturbs the fixpoint/reseed,
which must stay stable while the memory campaign lands its own iterative reseeds
(`ENSINAR→SEED→SWEEP→SEED`). Two reseed-perturbing programmes in flight at once violates
"LIMPEZA PRIMEIRO, reseed só no fim". The teach-now half is safe because the default build still
discovers all of `src/` — the new capability is dormant until 0.3.2 flips it.

---

## 1. The fixpoint / bootstrap mechanics (THE risk)

### 1.1 What self-host is today (code-grounded)

Every generation is one command — `teko . -o out --no-verify --release` — building the WHOLE
monolith. Source discovery is `discover(name, source)` (`src/build/discover.tks:50`): it walks the
`source` root and derives each file's namespace from its directory, rooted at `name`. The compiler's
own manifest (`teko.tkp`) sets `name = "teko"`, `source = "src"`, and `source` is **INVISIBLE in
addressing** — so `src/list/list.tks` → namespace `teko::list`, `src/checker/*` → `teko::checker`.
Compiler and stdlib live in ONE typed tree, checked and monomorphized together, lowered to ONE
`teko.c`.

The chain (`scripts/build_with_seed_fallback.sh` + `scripts/fixpoint_gate.sh`, owner ruling
2026-07-28):

```
release/seed --C--> gen0 --C--> gen1 (emits teko.c)      [build_with_seed_fallback.sh: the doubling]
             --backend--> gen2 --backend--> gen3          [fixpoint_gate.sh]   ASSERT gen2 == gen3
```

`gen0` is the seed's build of the tip; `gen0_to_gen1` **doubles** it (gen0 rebuilds the identical
source) so gen1's emitted `teko.c` is THIS tree's own algorithm rendering itself — the harvest
candidate. `fixpoint_gate.sh` then builds `gen2 = gen1(source)` and `gen3 = gen2(source)` at the
**identical output path** (the path-trap guard) and asserts byte-identity. gen1 is deliberately never
compared (C route vs the pinned backend are different generators). The verdict is fixed at
`gen2 == gen3` in every state of the world.

### 1.2 What changes under extraction, and the exact invariant

Define two operations:

- **L(g)** = the stdlib `.tkl` package that compiler `g` produces from `./tklib`
  (`teko build ./tklib`, `Artifact::Package`).
- **C(g, tkl)** = the compiler binary that `g` produces from `./src` (compiler-only) while
  auto-linking `tkl`.

A generation is now the composite `g ↦ C(g, L(g))`. The fixpoint gate's operands become:

```
gen2 = C(gen1, L(gen1))
gen3 = C(gen2, L(gen2))
ASSERT gen2 == gen3
```

`gen2 == gen3` holds iff the composite is a fixed point at gen1→gen2→gen3, which decomposes into:

1. **L is deterministic and reproducing:** `L(gen1) == L(gen2)`. Because gen1 is already the real
   algorithm (gen0-doubled) and gen2 = C(gen1, L(gen1)) descends from it, gen1 and gen2 agree as
   functions on `./tklib`, so their `.tkl` bytes match — **iff `.tkl` emission is byte-deterministic**.
2. **C is deterministic given (compiler, tkl):** `C(gen1, X) == C(gen2, X)` for the fixed
   `X = L(gen1) = L(gen2)`. Same convergence argument as today's single-tree fixpoint.

So the invariant SURVIVES; it is the SAME proof lifted onto two artifacts instead of one. The new
obligation is purely determinism, and it lands on four seams:

| # | Determinism obligation | Status in `src/` today |
|---|------------------------|------------------------|
| 1 | `.tkl` ZIP has no timestamp / stable member order | **CONFIRMED SAFE** — `write_zip_entries` uses `zip_fixed_dos_date()` = `0x5A21` (`compress.tks:87,204,232`), STORE method, members in call order (`.tkh`, `.tkb`, `.tsym`, `project.tks:1191-1193`). |
| 2 | `.tkb` = `serialize_program`/`deserialize_program` round-trips losslessly and stably | Used already by `load_dep_program` (`project.tks:159`); **must be PINNED** by the carve-fixpoint gate (a byte-diff of two independent `.tkl` builds). |
| 3 | Fully-qualified names identical after the move | **Depends on the `name="teko"` decision (§3).** With it, byte-identical. |
| 4 | Dep-merge order stable (`seed_from_dep` → `load_deps_program` append order) | Deterministic today: `m.deps` is manifest order, items appended in `dep_prog.items` order (`project.tks:174-179`). |

### 1.3 Bootstrap ordering — which compiler builds the stdlib `.tkb`

No circularity. The stdlib package build (`teko build ./tklib`) type-checks `./tklib` as a
**self-contained** monolith package: `list` uses `str`, `str` uses `runtime`, etc., all WITHIN
`./tklib` — nothing external. The *checker/parser* doing the type-check is compiled INTO the `teko`
binary in hand; it needs no external stdlib to run. So each generation is:

```
Step A:  gen_k  build ./tklib   ->  L(gen_k)  (the .tkl; backend-INDEPENDENT — see §1.4)
Step B:  gen_k  build ./src  (auto-link L(gen_k))  ->  gen_{k+1}
```

`gen0` (from the seed) produces `L(gen0)` and then `gen1`; `fixpoint_gate.sh` then runs Steps A+B
under gen1 to get gen2 and under gen2 to get gen3. **The scripts change shape:** `build_gen` in
`fixpoint_gate.sh` and `build_project` in `build_with_seed_fallback.sh` must each gain the Step-A
stdlib-package pre-build before the Step-B compiler build (see the crumbs in §6).

### 1.4 One simplifier: `L(g)` is backend-independent

`Artifact::Package` emission (`project.tks:1173-1203`) writes `.tkh` (`emit::emit_program`), `.tkb`
(`emit::serialize_program`), `.tsym` (`codegen::tk_emit_tsym`) — **no native/C code generator runs**.
So `L(g)` depends only on `g`'s front+middle-end, NOT on `TEKO_BACKEND`. This means the per-leg
backend pin (`native` on Linux, `c` on macOS/Windows) affects only Step B, and the stdlib package is
identical across legs. It also means the carve-fixpoint de-risk (§6) runs on the C route TODAY
without waiting for native.

### 1.5 Where it could break (and the mitigation)

- **`.tkb` non-determinism** (any map/set iteration order, pointer identity, or interning that leaks
  into serialization). Mitigation: the carve-fixpoint gate diffs `L(gen1)` vs `L(gen2)` byte-for-byte
  BEFORE trusting the composite; a mismatch localizes to the serializer, not the whole compiler.
- **A name collision** between the two `name="teko"` trees (compiler defining `teko::list::*`, or
  stdlib defining `teko::checker::*`). Today they are disjoint subdirs → disjoint namespaces; the
  partition scout (§3, D55) must PRESERVE that disjointness — no symbol may live in both trees.
- **Reseed churn during the memory campaign.** Flipping the self-build to auto-link is a
  reseed-perturbing change; running it concurrently with the memory reseeds risks a fixpoint that
  fails for two independent reasons at once. Mitigation: the sequencing (§6) — teach-now stays
  dormant, move-later waits for the memory marco.

---

## 2. C-route feasibility NOW + hardening

**Feasible NOW.** Package consumption already exists at the typed-tree level and is C-route-usable
without any native own-linker:

- `load_dep_program` (`project.tks:104-163`) reads `packages/<dep>-*.tkl`, unzips, finds `<dep>.tkb`,
  `emit::deserialize_program` → `checker::TProgram`.
- `load_deps_program` (`:165-183`) concatenates all deps' items into one `TProgram`.
- `frontend_check` (`:393`) calls it, then `checked_program_of` (`:218-224`) passes `dep_prog` to
  `checker::type_program_with_deps_pre_mono`, which `seed_from_dep`s the dep types/env BEFORE the
  pre-walk (`typer.tks:5570-5579`) — i.e. **merged pre-monomorphization**, so generic stdlib
  templates cross the boundary and are stamped against compiler-side instantiation sites. This is the
  correct semantic contract (Model A of the toolchain doc).

**What must be HARDENED before it can carry the compiler's own stdlib** (toolchain doc findings +
this study's):

1. **No version check / ignores the `.tkh` public gate** (`load_dep_program` picks the FIRST
   prefix-matching `.tkl`, `project.tks:110-121`). For an auto-linked stdlib pinned to the compiler's
   own version this must select by exact version and honor `.tkh` visibility (only `exp` surface
   consumable).
2. **`[dependencies]` sub-keys silently dropped** — `deps = mf2_table(root,"dependencies").keys`
   (`manifest.tks:360`). Version/`path=` constraints are lost. Promote to a real
   `{name, version, path}` record.
3. **`[aliases]` is ALSO keys-only** — `aliases = mf2_table(root,"aliases").keys`
   (`manifest.tks:361`): the alias TARGET (value) is dropped. If the tklib:: consumer alias (§3) is
   ever offered, this needs the same promotion.
4. **LOCAL `path=` resolution absent** — only `packages/<dep>-*.tkl` is searched. The distributed
   monolith installs `tklib` alongside the compiler, so resolution must also find the co-installed
   stdlib `.tkl` (a built-in path, §4), not only a user `packages/` dir.
5. **Memory anti-pattern on the hot path** — `load_dep_program` materializes the `.tkl` bytes with a
   `teko::list::push` loop (`project.tks:130-136`), a copy-grow that the NO-PUSHES law bans. Since
   `read_file` already returns the content and `str`↔`[]byte` share the `{ptr,len}` rep, this must
   become a reinterpret (`tk_bytes_of_str_len`), not a byte-by-byte push. Left as-is, extraction
   would ADD a push-elephant to the critical build path.

**Minimal "teach + prove" prerequisite** (the owner's ask, C route): items 1, 4, 5 + the auto-link
built-in (§4) + the carve-fixpoint gate (§6). Items 2, 3 are needed for GENERAL packages but a
built-in auto-linked stdlib can bypass the manifest `[dependencies]` table entirely (§4), so they are
not on the critical path for the stdlib move — they belong to the general package-manager track.

---

## 3. Namespace / alias strategy — the riskiest seam for the fixpoint

**Recommendation (law-first — byte-identity under main-integrity + the fixpoint wins): the extracted
package KEEPS `name = "teko"`. The stdlib is addressed `teko::list`, `teko::io`, … exactly as today.
NO rename to `tklib::`. NO source change to the compiler's ~thousands of `teko::…` references.**

Why this is the only safe choice for the fixpoint:

- Namespaces are directory-derived and rooted at the manifest `name`, with `source` INVISIBLE
  (`discover.tks`, `teko.tkp:10-12`). A package built from `./tklib` with `name = "teko"`,
  `source = "tklib"` yields **byte-identical** fully-qualified names to `./src` with `name = "teko"`,
  `source = "src"` — because both roots are `teko` and both source dirs are invisible. `tklib/list/…`
  → `teko::list`, same as `src/list/…` → `teko::list` today.
- The compiler's own code is unchanged: `teko::str::concat`, `teko::list::push`, `teko::io::println`
  resolve against the merged dep exactly as they resolve against the in-tree definitions today. Zero
  edits to the corpus = zero risk of a byte-diff sneaking in via a rename sweep. This is decisive: a
  rename would touch every compiler file and each edit is a chance to perturb the emitted C.
- The `teko` root reservation ("no user project may claim it; the language's own project is its sole
  bearer", `teko.tkp:6-8`) is HONORED, not violated: the stdlib package IS the language's own
  project (the distributed monolith), so it legitimately bears `teko`. This needs a one-line ruling
  (the reservation now spans TWO co-distributed projects, both owned by the language), consistent with
  D55 (the stdlib is the monolith's exposed public surface).

**On the owner's "tklib:: aliases":** offer `tklib::` at most as an OPTIONAL consumer-side alias
(external users may `use tklib::list as list`), resolved through the `[aliases]` manifest surface
(once promoted, §2 item 3). The alias is a convenience on the CONSUMER, never a rename of the
canonical symbols — the canonical, serialized-in-`.tkb` names stay `teko::…`. This resolves
"renaming (aliases + namespaces)" law-first with no fixpoint exposure.

**Partition caveat (D55):** the EXACT set of modules that move is a future scout — D55 rules the
monolith keeps only what the compiler itself consumes. Note two non-obvious residents: `teko::str`
(e.g. `concat`) and the print/hash primitives live in `src/runtime/teko_rt.tks` (the maintained-C
seam's Teko twin, `teko_rt.tks:23,67,232`), addressed under `teko::str`/`teko::runtime`. The runtime
twin STAYS with the compiler (it is the `teko_rt.{c,h}` exception), so the partition boundary is not a
clean "everything the user calls leaves" — runtime-backed primitives the compiler depends on remain.
The scout must draw the boundary by ACTUAL compiler use (the self-build is the oracle: if `src/`
references it, it stays or is linked), not by module name.

---

## 4. Auto-link — the always-on implicit stdlib dependency

The compiler links the stdlib WITHOUT a `.tkp` `[dependencies]` entry (the "distributed monolith":
packages shipped together by the installer). Design as a **built-in implicit dependency** injected in
the build front-end, parallel to the existing runtime-prelude injection:

- There is already precedent — `inject_runtime_prelude` (`project.tks:323`) + `rt_inject_namespaces`
  (`:278`) implicitly inject `teko::runtime`/`teko::sys` source for `Artifact::Binary`. The stdlib
  auto-link is the same shape one layer up: an implicit dep resolved and merged with no manifest
  declaration.
- **Seam:** `frontend_check` (`project.tks:387`) currently calls
  `load_deps_program(pf.manifest)` (manifest-declared deps only). Add an
  `implicit_stdlib_dep_program()` that (a) locates the co-installed stdlib `.tkl` via a built-in
  search (env `TK_STDLIB_TKL` override → sibling-of-argv[0] install layout → in-repo
  `tklib/*.tkl`, mirroring `rt_dir()`'s argv[0]-relative resolution `:417-426`), (b) loads it via the
  hardened `load_dep_program`, and (c) merges its items ahead of the manifest deps into the single
  `dep_prog` handed to `checked_program_of`.
- **Suppression:** the stdlib package's OWN build must NOT auto-link itself (infinite/self dep) — gate
  auto-link off when `m.artifact == Package && m.name == "teko"`, or by an explicit
  `#![no_std]`-equivalent build flag. `Artifact::Package` already opts out of the runtime prelude
  (`artifact_wants_runtime_prelude`, `:314-321`); reuse that discipline.
- **Version pinning:** the built-in resolver selects the stdlib `.tkl` whose version equals the
  compiler's own `version` (`teko.tkp:13`) — this is exactly the hardening item §2.1 (exact-version
  selection), and it is what keeps the distributed monolith internally consistent.

Type/function shapes the implementer will ADD (design-ahead, full-Javadoc, C-route, native-agnostic):

```
/**
 * Locate the co-distributed stdlib package `.tkl` for THIS compiler, without a manifest entry.
 *
 * Resolution order (first hit wins): the `TK_STDLIB_TKL` env override; the installed layout beside
 * `argv[0]` (`<prefix>/lib/teko/teko-<version>.tkl`); the in-repo `tklib/teko-<version>.tkl`. The
 * selected file's version MUST equal `compiler_version` — a mismatch is a hard error, never a
 * silent newest-wins pick.
 *
 * @param compiler_version the compiler's own `version` string, used for exact-version selection
 * @return the absolute path to the stdlib `.tkl`, or an error naming every location searched
 */
fn resolve_stdlib_tkl(compiler_version: str): str | error

/**
 * The implicit, always-linked stdlib typed program for a compiler build, merged AHEAD of any
 * manifest `[dependencies]`. Empty (no-op) when the artifact is the stdlib package itself, so the
 * distributed monolith never depends on itself.
 *
 * @param m the resolved project manifest (its artifact/name decide self-suppression)
 * @return the deserialized stdlib `TProgram`, or an empty program when auto-link is suppressed,
 *         or an error when the pinned `.tkl` cannot be resolved or read
 */
fn implicit_stdlib_dep_program(m: Manifest): checker::TProgram | error
```

Existing fns touched: `frontend_check` (`project.tks:387`, merge the implicit program into
`dep_prog`), `load_dep_program` (`:104`, harden per §2), `load_deps_program` (`:165`, accept a
pre-resolved path/program), and the two bootstrap scripts (§1.3).

---

## 5. Memory payoff — QUANTIFIED vs the elephant

**Code volume (measured on this branch):** total `src/*.tks` ≈ **93,149** lines. Core compiler
(`checker/lir/backend/parser/codegen/lexer/names/emit/build`) ≈ **64,680**. The stdlib subtree
candidates (`list str map io fs collections crypto numeric encoding sort cmp math iter fmt text
compress time process threads sys regex`) ≈ **24,376+** (some, e.g. `str`, additionally sit in
`runtime`). So the stdlib is roughly **~28–30%** of source VOLUME.

**But volume reduction ≠ peak reduction, and the split matters:**

- The profiler (`tk_obs`) attributes **~93% of peak** to `tk_slice_push_r` in the codegen `cb`
  emit-buffer (COL-F0). That elephant lives in **CODEGEN**, not the front-end.
- **On the C route (the only route now), extraction does NOT touch the elephant.** The linked stdlib
  `.tkb` is merged at the typed-tree level (`seed_from_dep`) and the compiler binary must still
  CONTAIN the machine code for every stdlib function it calls (`list::push`, `str::concat`, …) — so
  codegen **re-emits all used stdlib code into the single `teko.c`**. The `cb` buffer grows exactly as
  much as today. Extraction removes only the stdlib's **parse + check** AST (front-end), a minority of
  peak, and even that is partly OFFSET by loading + deserializing the `.tkb` (its bytes + the
  materialized `TProgram` are resident during the build).
- Net C-route peak change from extraction alone: **small (single-digit % of peak, plausibly near
  zero or slightly negative** once the `.tkb` load cost and the current push-loop reader §2.5 are
  counted). **Extraction alone does NOT reach ≤1.5 GB.**

**Where extraction WOULD help codegen memory:** only when the stdlib is a SEPARATE compilation unit
that is LINKED (not re-emitted) — i.e. the `.tkb`→native-`.o` path / the per-namespace
object-per-unit endgame (CLAUDE.md "cada namespace emite um OBJETO"). Then the compiler's self-build
codegen emits only compiler code and links a prebuilt stdlib object, and the `cb` buffer shrinks by
the stdlib's share. That route is **native, hence D52-gated** (native is the LAST etapa). So the
codegen-memory dividend of extraction is deferred to the native era regardless.

**Conclusion (Q5):** COL-F0 and extraction are **complementary, not substitutes — and COL-F0 is the
one that hits the marco.** COL-F0 (no-push, kill the `cb` copy-grow) is necessary and near-sufficient
for ≤1.5 GB; extraction contributes ~0 on the C route and its codegen dividend waits for native.
**Extraction does NOT change the COL-F0 / RM-C plan** and must not be scheduled AS a memory measure.
Its justification is architectural (D55 monolith boundary, the package toolchain proof, the kernel/DB
side-cars), not the memory marco.

---

## 6. GO/NO-GO + sequencing + prerequisite crumbs

**GO, split teach-now / move-later (D54). NOT a 0.3.1 memory pivot; the MOVE is a 0.3.2
restructuring gated on the memory marco.**

Risk-adjusted rationale: the fixpoint survives (§1) and the C-route machinery exists (§2), so the
capability is low-risk to TEACH now. But the MOVE buys ≈0 memory on the C route (§5) and perturbs the
reseed, so it must not compete with the memory campaign; it waits for the triple D52 marco (dry build
≤1.5 GB + `gen2==gen3` + green tests) and a settled reseed. This is precisely D54: teach the surface
now (dormant), defer the heavy use.

### 6.1 TEACH-NOW crumbs (0.3.1, C route, leaf — NO physical move, NO default-build change, NO reseed)

Ordered, each independently gate-able:

1. **PKG-H1 — harden `load_dep_program`.** Exact-version selection + honor the `.tkh` public gate +
   replace the `str→[]byte` push loop with a `str`↔`[]byte` reinterpret. Gate: existing
   `manifest.tkr` / package regressors stay green; add a fixture (below).
2. **PKG-H2 — built-in stdlib resolver + auto-link plumbing (DORMANT).** Add `resolve_stdlib_tkl` +
   `implicit_stdlib_dep_program` (§4) and wire into `frontend_check`, but keep it a **no-op for the
   compiler's own build** (the compiler still discovers all of `src/`; the implicit dep resolves to
   an empty program unless a stdlib `.tkl` is present AND a build flag opts in). This lands the
   surface without changing what the self-build compiles.
3. **PKG-C1 — carve-fixpoint CI gate (the de-risk that satisfies the owner's prerequisite).** A new
   script `scripts/carve_fixpoint.sh` (sibling of `fixpoint_gate.sh`) that, WITHOUT moving any file:
   (a) builds a stdlib `.tkl` from a carved view of the existing `src/` stdlib subdirs
   (`teko build` over a temp project whose `source` points at the stdlib subset, `name="teko"`);
   (b) builds the compiler from the COMPLEMENTARY `src/` subset auto-linking that `.tkl`;
   (c) runs the `gen2==gen3` assertion through that linked-package build; (d) byte-diffs
   `L(gen1)` vs `L(gen2)` to pin `.tkb`/`.tkl` determinism (§1.5). Runs on the C route today. This is
   the empirical proof that "the compiler can link `.tkb` + pass the fixpoint with the stdlib as a
   linked package" — landed BEFORE the move, exactly as the owner required. Kept OFF the default gate
   (opt-in lane) until green, then promoted.

Ritual points (full gate must pass): after PKG-H1 (touches the shared `project.tks` build path →
reseed-relevant only if the emitted C shifts; if leaf, no reseed); after PKG-C1 goes green on the C
route (the go-signal for 0.3.2).

### 6.2 MOVE-LATER crumbs (0.3.2, gated on D52 marco + settled reseed)

4. **TKLIB-M1 — partition scout (D55).** Determine EXACTLY which modules the compiler consumes vs
   which are pure user surface; the self-build is the oracle. Produces the move manifest and the
   disjointness proof (§1.5 collision guard).
5. **TKLIB-M2 — physical move + `tklib/teko.tkp` (`name="teko"`, `source="tklib"`,
   `[artifact] kind="package"`).** No source edits to the moved files (namespaces preserved, §3).
6. **TKLIB-M3 — flip auto-link ON for the compiler build** (PKG-H2 stops being dormant) + update the
   two bootstrap scripts to the Step-A/Step-B shape (§1.3).
7. **TKLIB-M4 — reseed** through the new self-build (harvest gen1's `teko.c`), fixpoint `gen2==gen3`,
   green tests. Single reseed at the end (LIMPEZA-PRIMEIRO discipline).

Ritual point: TKLIB-M3+M4 is a hard reseed ritual — the full gate + fixpoint + a fresh
`bootstrap/teko.c` harvest.

### 6.3 What 0.3.1 keeps if the owner DEFERS even the teach-now half

The package-consumption path stays as-is (used by real user packages), the memory campaign proceeds
untouched, and this doc is the resume-in-minutes design. Nothing regresses; the only cost is that the
empirical fixpoint-through-package proof (PKG-C1) is not yet in hand when 0.3.2 opens.

### 6.4 Regression fixtures (inputs → expected native exit codes)

Per the testing law (no tests for what the self-build exercises), these are ORACLES for paths the
fixpoint does NOT reach — the ERROR/diagnostic and determinism paths:

- **`pkg_version_mismatch.tkr`** — a project auto-linking a stdlib `.tkl` whose version ≠ the
  compiler's → **compile error** (nonzero exit), asserting the "exact-version, no newest-wins"
  diagnostic. (PKG-H1; the happy path is exercised by the carve-fixpoint self-build, so no positive
  test.)
- **`pkg_tkh_gate.tkr`** — a consumer referencing a `pub` (non-`exp`) stdlib symbol across the
  package boundary → **compile error** (the `.tkh` gate rejects it). Guards §2.1.
- **`tkl_determinism` (script assertion in `carve_fixpoint.sh`, not a `.tkr`)** — two independent
  `L(g)` builds are byte-identical; a diff is a nonzero exit naming the first differing offset. Guards
  §1.5.
- **No positive functional fixtures** — the carved self-build (PKG-C1) exercises the entire happy
  path (the compiler compiling itself through the linked stdlib), so per CLAUDE.md the fixpoint IS the
  proof; adding functional `.tkr`s here would be tautological and is forbidden.

---

## 7. Risks + law tensions (all resolved law-first — NO HALT)

- **Fixpoint (§1):** SURVIVES; the new obligations are determinism seams already satisfied (`.tkl`) or
  cheaply pinned (`.tkb`). No tension.
- **Namespace byte-identity (§3):** resolved by keeping `name="teko"` — the `teko` root reservation
  is honored (the stdlib IS the language's own project, D55). One-line ruling, no source sweep, no
  tension.
- **Memory (§5):** extraction is not a memory measure; COL-F0 stays primary. Prevents a
  mis-scheduling that would have paused the memory campaign for ~0 gain. No tension — a sequencing
  correction.
- **D52 (native last):** the codegen-memory dividend of extraction rides the native/separate-object
  route, which is already D52-gated. The C-route teach-now is native-agnostic (§1.4). Consistent, no
  tension.
- **Reseed discipline:** the split keeps exactly one reseed-perturbing programme in flight at a time
  (memory campaign now; tklib move after). Honors "LIMPEZA PRIMEIRO, reseed só no fim". No tension.
- **Owner's explicit "learn NOW" prerequisite:** satisfied by PKG-C1 (carve-fixpoint) on the C route,
  before any file moves — a faithful reading of the ask under D54. No tension.

**No genuine unresolved tension → no HALT.** The single owner-facing DECISION this study surfaces
(not a tension, a ratification the coordinator should carry): confirm the `name="teko"` /
no-rename / tklib-as-consumer-alias-only strategy (§3) and the teach-now-in-0.3.1 /
move-in-0.3.2-after-the-memory-marco sequencing (§0, §6).

---

## 8. Adjacent findings (REPORTED up — not issues this study opens)

- `load_dep_program` reads the `.tkl` via a `teko::list::push` byte loop (`project.tks:130-136`) — a
  NO-PUSHES-law violation on the build hot path, independent of extraction; worth fixing regardless
  (folds into PKG-H1).
- `[dependencies]` and `[aliases]` are both keys-only (`manifest.tks:360-361`) — sub-key/value drop
  affects the general package-manager track (versions, alias targets, local `path=`), already flagged
  by the toolchain doc for `[dependencies]`; the `[aliases]` value-drop is an additional instance.
- `freestanding` still detours to `invoke_cc` (`project.tks:921`, per the toolchain doc) — the last
  C-driver reach on the native path; unrelated to extraction but on the same no-C endgame.
