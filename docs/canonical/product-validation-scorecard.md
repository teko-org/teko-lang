# Product Validation Scorecard — `docs/canonical/` vs. Reality

**Reviewer role:** Product (Sonnet). Read-only pass over the canonical "as-if-done" doc set
(`docs/canonical/dev/`, `docs/canonical/product/`, `docs/canonical/README.draft.md`), cross-checked
against `src/`, `TEKO_MASTER_PLAN.md`, `TEKO_ROADMAP_*.md`, `TEKO_LEGISLATION.md`, `docs/design/*.md`,
`docs/memory/*.md`, and the real `README.md`, on branch `cargo/0.3.1.0-canonical-docs`
(HEAD `60f4ecbe`) as of **2026-08-03**.

**Verdict up front:** the canonical set is well-written and mostly *individually* accurate to some
ratified design — but it is calibrated to "the design docs already commit to this," not to "this is
close to landing." Several chapters describe capabilities that are 0% built (own linker excepted,
which the doc itself is honest about), one chapter (concurrency) risks describing a model that may
already be **superseded** by a later owner ruling, and one code sample in the flagship README draft
would **not compile** against the real `bin/teko` today. None of this is disqualifying — the doc says
up front it is the destination, not the trailhead — but the distances are uneven enough that a few
chapters read as more finished than the project is, in ways a reader (or the owner scanning for a
go/no-go) would not guess without cross-referencing `src/` themselves.

---

## 1. Scorecard by area

Legend: 🟢 pronto (matches reality, modulo normal beta churn) · 🟡 parcial (real, substantial, but a
material gap between doc and code) · 🟠 desenho (a real design exists; ~0 product code) · 🔴 não
existe / fantasia (no design consensus, or the doc's claim conflicts with what's ratified).

| Area | Doc says | Reality (evidence) | Status | Gap to close |
|---|---|---|---|---|
| **Pipeline (lex→parse→check→LIR)** | Full pipeline, TAST-first, backend-agnostic | Matches almost exactly: `src/lexer`, `src/parser`, `src/checker` (~45 files), `src/lir` all real, tested, wired | 🟢 | Cosmetic only |
| **Native backend (own AOT)** | The production path; C is a "differential comparative" kept alongside | Real, large (13.6k+ lines: `isel_*`, `encode_*`, `objfile_elf/macho/coff`, `regalloc*`, `dwarf.tks`) — but **self-hosting the compiler through the native backend does not yet reach the fixpoint on any of the 6 platform legs** as of the last measured run (`docs/memory/mapa-native-6-pernas-0.3.1.0.md`, 2026-07-29: all 6 legs FAIL at LOWERING). Most recent Linux-only progress (`docs/memory/handoff-0.3.1.0.md` §17, 2026-07-31) is down to one named blocker ("degrau 32", builtin `one_byte` not lowered) — real progress, still not closed. **`teko test .` always compiles via the C route regardless of `TEKO_BACKEND=native`** — a measured blind spot, so the native path isn't even exercised by the test gate today. | 🟡 (extensive code, not yet functionally complete on any target) | Reach `gen2==gen3` via native on ≥1 platform; fix the test-gate blind spot; then the other 5 legs |
| **Own linker (from scratch)** | Deliberately later-staged; system linker (`ld`/`ld64`/`lld-link`) is the production path today | `src/build/linker.tks` (363 lines) does exactly what the doc says: picks + validates the native system linker per target, hard-rejects MinGW. No from-scratch linker exists — **and the doc says so itself.** | 🟢 (accurate — the one chapter that under-claims rather than over-claims) | None — doc and reality agree |
| **Memory model — arena (default)** | Invisible, compiler-managed regions, tree-shaped | Real: `tk_region_new/drop`, `src/mem/unsafe/arena.tks` | 🟢 | None |
| **Memory model — `adopt { }`** | Bulk-drop region for cycles | Real: parsed (`parse_stmt.tks`), lowered (`lir/lower.tks`), used in regression grouping | 🟢 | None |
| **Memory model — `unsafe` modifier + `RawBuf`** | Type/fn modifier; `RawBuf`/`Owned<T>` at the bottom | `unsafe` keyword real in parser; `src/mem/unsafe/rawbuf.tks` + tests exist. `Owned<T>` name not found as such — check exact naming before publishing | 🟡 | Confirm `Owned<T>` vs. what's actually named in code |
| **Memory model — the spine** | "What the spine buys, concretely: sound stored borrows … and a sound manual `mem::free`" (stated as already true) | `src/checker/spine.tks` (1569 lines) is real and sophisticated — but its own header says **"PR-1 SCOPE: the PURE query… Nothing consumes it for a decision yet (that is PR-2/PR-3)."** The gates that would actually *enforce* sound stored borrows / gate `mem::free` are not landed. No `mem::free` public function found in `src/mem/`. | 🟡 (fact-computation done; the safety **guarantee** the doc claims is not yet enforced) | Land PR-2/PR-3 gates; land `mem::free` itself |
| **Isolate concurrency (`teko::isolate`, spawn/join/fork_join)** | Presented as an existing surface ("The surface is `teko::isolate` — `Isolate`, `spawn`, `join`, `fork_join`, `hardware_parallelism()`"), with only cross-isolate *sharing* vocabulary flagged as reserved-not-built | **Zero occurrences of `isolate`/`spawn`/`fork_join`/`hardware_parallelism` anywhere in `src/`.** The only "isolate" work in the tree is an unrelated, narrower thing: an in-progress *test-harness* task-isolation design (`docs/design/paralelizacao-0.3.1-eixo1-isolamento-por-task.md`, dated 2026-08-02, itself "DESENHO — no product code written this pass"), aimed at making `#test` runs process/task-isolated — not a user-facing language concurrency primitive. **More importantly:** the most recently *ratified* concurrency design for ordinary Teko programs (owner, 2026-06-30, "the concurrency CAPSTONE", `TEKO_MASTER_PLAN.md` Phase 10 S8/ASYNC) is a **different model** — `Intent<T>` + `async`/`await` + `teko::threading::run`/`teko::sync` (Mutex/RwLock/Semaphore/Atomic/Once/Barrier) — and explicitly states **"NO scope"** and **"NO spawn."** | 🔴 (not built, and possibly describing a superseded design) | **Needs an owner ruling before this chapter can be trusted at all** — see §3 |
| **Errors as values — `T\|error`, `match`, `?.`/`??`** | Core mechanism | Real, DONE (per `TEKO_MASTER_PLAN.md` Phase 1/"NULL PROPAGATION — DONE + RATIFIED") | 🟢 | None |
| **Error *factory* (`error::new`, `error::new_pos`, `error::join`)** | "The idiomatic way to produce one is the factory... The `error { message = ... }` struct-literal form still compiles (not removed)" | **Zero occurrences of `error::new`/`error::new_pos`/`error::join` anywhere in the real codebase.** Every one of the ~900 corpus files that constructs an error uses the raw struct literal `error { message = ... }` — the "still compiles, not removed" framing has it backwards: the factory doesn't exist yet, the struct literal is the *only* form. The current real `README.md` correctly uses `error { message = "boom" }`; the canonical `README.draft.md` swaps in `error::new("boom")`. | 🔴 | Land the factory functions, or drop the factory framing until they exist |
| **Testing gate (`teko test`/`teko build` pre-codegen gate)** | Tests run before codegen; coverage floors bar the release | Real and matches: `D2`/`D3`/`D4` DONE per master plan; `.tkr` regression suite real (`teko.tkp` `[tests] regression = [...]`, matches doc's "no glob" claim exactly) | 🟢 | None |
| **Coverage + Cobertura export** | `--coverage` → Cobertura XML | Real: `src/coverage/coverage.tks` builds Cobertura `<line>` elements | 🟢 | None |
| **The run journal** | Per-test channel, verdict-first line, coverage union across shards, `--replay` | `src/journal/journal.tks` (711+ lines) is real and detailed, matches most of this closely | 🟢 | Confirm `--replay` CLI flag is wired (not verified directly) |
| **Test execution model — "isolate == isolated OS thread, fresh arena root per test"** | Stated as the current model | **Not what's running today.** `src/test/test.tks`'s own header: parallelism today is **process-sharding**, round-robin by ordinal (`REGR_JOBS_DEFAULT = 4`), with scratch-path isolation as the fix for a *measured* cross-test overwrite bug — not OS-thread-per-test with a private arena root (that's the still-DESENHO task-isolation proposal above). | 🟡 (real parallelism exists; the *mechanism* doc describes is the next redesign, not what runs today) | Land the `tk_task` per-test isolation design, or restate this section as target-not-current |
| **LSP server (`teko lsp`)** | In-process JSON-RPC server: diagnostics, formatting, hover, go-to-def, completion, semantic tokens | Real and substantial: `src/lsp/{server,jsonrpc,diagnostics,nav,symbols}.tks` (2276 lines), wired as a real subcommand (`src/build/help.tks`), confirmed by the owner directly (`docs/memory/lsp-cliente-e-tooling-diferido-0.3.1.md`, 2026-08-03): "o **servidor** LSP está pronto." **Semantic tokens are the one overclaim inside this chapter**: `initialize_result` does not yet advertise `semanticTokensProvider`. | 🟢 (server) / 🟡 (semantic tokens specifically) | Wire semantic-token capability advertisement |
| **Editor clients (VS Code / Vim / Neovim / Emacs)** | "VS Code — a full extension: TextMate grammar... a `LanguageClient` wired to `teko lsp`, and build/run/test task integration." Same for Vim/Emacs. | **False today.** `tooling/vscode/package.json` is confirmed **grammar-only**: no `main`, no `activationEvents`, no `vscode-languageclient` dependency — owner's own words (2026-08-03): "`tooling/vscode/package.json` hoje é grammar-only." Same for Vim/Emacs/Nano (grammar generators only, no `LanguageClient` wiring found anywhere in `tooling/`). **A known, unfixed security bug** sits in this exact code path: `tooling/vscode` uses `cp.exec` with a string-interpolated command → command injection, flagged by the owner's own scout the same day. | 🔴 | Build the actual `LanguageClient` wiring (needs a non-Teko/TypeScript-fluent agent per the owner's own note) + fix the injection bug first |
| **`teko fmt`** | Zero-option canonical formatter, `--check` CI gate, idempotent | Real, DONE (`DT0`, `src/fmt/fmt.tks`), proven `fmt(fmt(x))==fmt(x)` over the whole corpus | 🟢 | Corpus-wide reformat + CI `--check` gate itself still open per the roadmap, but the tool works |
| **`teko doc`** | Doc-comment → HTML/Markdown/JSON generator | **No such subcommand exists.** No `src/doc/` or equivalent found. `TEKO_ROADMAP_DEVTOOLS.md`: "DT1 doc … remain ⬜" | 🔴 | Entirely unbuilt |
| **`teko lint`** | Style/best-practice linter, `--fix` | **No such subcommand exists.** `TEKO_ROADMAP_DEVTOOLS.md`: "DT2 lint … remain ⬜" | 🔴 | Entirely unbuilt |
| **`teko repl`** | Interactive REPL, "restated on top of the native backend's -O0 debug-compile-and-run path" | **No `src/repl/` exists at all.** The REPL was **explicitly, ratified-ly retired** (micro-decision **M1**, "RATIFIED = retire"), on the stated grounds that a native AOT compiler has no line-by-line eval "without a JIT or a compile-per-line loop (both out of scope for #524)." `TEKO_ROADMAP_DEVTOOLS.md` (pre-dates the retirement) still lists `DT3 repl` with a now stale/orphaned dependency. **The canonical doc's "-O0 compile-and-run" resolution is the architect's own proposed fix for a gap the ratified retirement explicitly declared out of scope — it is not an owner ruling on record.** | 🔴 (retired feature; proposed revival is unratified) | **Owner decision needed**: does Teko get a REPL at all, and on what mechanism? See §3 |
| **`tdb` debugger** | 8-phase description (harness/oracle → `.tsym` v2 → control floor → breakpoints → stepping → variables → editor/DAP → ports), reads as delivered | **Zero lines of `tdb` implementation exist.** The design doc it's drawn from says so explicitly: *"Nada disto se implementa nesta versão nem na seguinte"* ("none of this is implemented in this version nor the next" — `docs/design/tdb-proposta-0.3.1.md`). What **is** real: `.tsym` symbol-map emission (Phase 1 E3, DONE) and DWARF line/frame emission (`src/backend/dwarf.tks`) — genuinely the *substrate* the debugger would need, but not the debugger. | 🟠 (substrate real; the tool is 100% design) | Everything past the two emission pieces already shipped |
| **FFI / `extern` / `teko_rt`** | `extern fn ... = "sym" from "lib"`, marshalling restrictions, `extern type`, no variadics, per-OS manifest resolution | Well-supported by `src/build/manifest.tks` (`[extern.libs]` resolution, per-OS/per-arch keys, link-mode tracking) and the parser/checker surface | 🟢 | Exhaustive rule-by-rule audit not done in this pass, but structurally solid |
| **Package manifest (`teko.tkp`), local deps, `.tkl` format** | TOML manifest, `.tkl` ZIP of `.tkh`+`.tkb`(+`.tsym`), consumer-driven monomorphization | Real: `src/build/manifest.tks`, `.tkl` load/emit in `src/build/project.tks` (search `packages/<dep>-*.tkl`, ZIP parse, C7.10 pre-linker merge) | 🟢 | None for the local-only flow |
| **Package manager — SemVer resolver, lockfile, registry, fetch** | Flat/single-aligned resolver, `teko.lock`, `teko add/remove/update`, static-index or dynamic registry | **`src/pkg/` does not exist at all** — no `semver.tks`, `resolve.tks`, `lock.tks`, `registry.tks`, `verify.tks` anywhere in the tree. `TEKO_ROADMAP_PACKAGES.md`'s own tiering confirms: only `PK0` (manifest declaration, already covered above) is done; `PK1`(SemVer)–`PK7`(publishing) are all open roadmap items. | 🔴 | The entire resolver/lock/registry/CLI stack — this is the single largest all-design, zero-code area in the whole canonical set |
| **Package security model (SHA-256 mandatory, Merkle transparency log, Sigstore/Fulcio keyless signing)** | Presented as the shipped, layered (0–3) security model | **100% design** (`docs/design/package-manager.md`); no implementing code found anywhere | 🔴 | Everything — not started |
| **Stdlib — `teko::io` (Reader/Writer/Seeker/Closer)** | Interface family + combinators | Real: `src/io/stream.tks`, tested, matches `IO0` DONE status | 🟢 | Combinators still typed against closure twins rather than the interfaces directly (noted as a known, intentional interim in the master plan) |
| **Stdlib — `teko::iter`** | Adapters + `in` operator | Real: `src/iter/{iter,byte_iter,int_iter,str_iter,int_terminals}.tks` | 🟢 | None found in this pass |
| **Stdlib — `teko::math`** | Constants, classification, checked arithmetic | Real: `src/math/math.tks`, DONE per master plan (`M0`), with one documented deviation (`min`/`max`/`clamp` per-type, not generic, pending constrained-generics) | 🟢 | Minor (generic operator constraint, S6, not yet landed) |
| **Stdlib — collections (`Map`/`Set`), net/crypto/web/db/cloud-native** | Not the focus of the canonical set's language-guide but implied "batteries" via `packages.md`/roadmap cross-refs | Mixed: `teko::encoding::json` real and tested; `src/collections`/`src/crypto`/`src/compress` directories exist but most of the *L*ibrary tracks (NET_CRYPTO sockets, PACKAGES, DB, WEB, CLOUD_NATIVE) remain largely roadmap-stage per `TEKO_MASTER_PLAN.md`'s own accounting | 🟡 | Wide, tracked in the library-track roadmaps already — not this doc's main gap |
| **Self-hosting fixpoint (`gen2==gen3`)** | Presented as an already-achieved, standing property | True for the **C-emission bridge path** at various past checkpoints (per master-plan history); **not yet true for the native-backend path** on any platform as of the last measurement (see native-backend row above) — and the doc's own `self-host-fixpoint.md` is careful to scope the assertion to "native," so this is the native-backend gap surfacing here again, not a new one | 🟡 | Same as native-backend row |

---

## 2. Doc-set self-consistency issues (independent of the code gap)

1. **The "Open / needs a ruling" mechanism is promised but never used.** `docs/canonical/README.md`
   states: *"Where the target is genuinely open... this canon says so explicitly... see each file's
   'Open / needs a ruling' note where present."* **Not one file in `dev/` or `product/` contains such
   a note** — despite at least three live candidates that arguably warrant one (the `this`/`base`/
   `static` OOP-syntax proposal, the REPL's native-only mechanism, and the isolate-vs-`Intent<T>`
   concurrency tension, all detailed in §3 below). This is a promise the doc set doesn't keep, and
   it's an easy, low-cost fix (either add the notes, or drop the sentence).

2. **The concurrency chapter (`memory-model.md`'s "Isolate concurrency" section) may describe a
   superseded design.** See the isolate row above and §3.1. This is the one spot where the canonical
   doc isn't just "ahead of the code" — it risks being **ahead of the wrong design**, since a later,
   explicitly-ratified owner decision (`Intent<T>`/async-await, 2026-06-30) governs ordinary-program
   concurrency and explicitly rejects the vocabulary ("no spawn," "no scope") the canonical doc builds
   its whole concurrency story on.

3. **`README.draft.md`'s "fully self-hosting… byte-identical fixpoint across native generations"
   claim (banner + status line) overstates what's proven.** The fixpoint is proven for the C-bridge
   generations at various past checkpoints; it is **not yet proven for the native-backend generations**
   the sentence's own wording ("native generations") specifically claims.

4. **`README.draft.md`'s worked code sample uses `error::new("boom")`.** Per the error-factory row
   above, this does not exist in the language today — a reader who clones the repo, builds `bin/teko`,
   and pastes the README's own sample would hit a compile error on the very first example. The current
   real `README.md` gets this right (`error { message = "boom" }`).

---

## 3. Owner decisions surfaced (the architect's list, plus one this pass adds)

### 3.1 Concurrency: `isolate` vs. `Intent<T>` — **not flagged by the architect, found in this pass**
Two non-identical things both trade on the word "isolate" in the tree today:
- an unbuilt, narrowly-scoped **test-harness** task-isolation design
  (`docs/design/paralelizacao-0.3.1-eixo1-isolamento-por-task.md`), and
- the canonical doc's **user-facing language primitive** (`teko::isolate`, `spawn`/`join`/`fork_join`).

Separately, the most recently ratified **user-facing** concurrency design (owner, 2026-06-30, "the
concurrency CAPSTONE," `TEKO_MASTER_PLAN.md` Phase 10 S8/ASYNC) commits to `Intent<T>` + `async`/
`await` + `teko::threading`/`teko::sync`, explicitly declaring **"NO scope"** and **"NO spawn."**
Recommend the owner confirm, in one sentence: is `teko::isolate` (spawn/join/fork_join) still the
governing concurrency surface, or has `Intent<T>` superseded it? Whichever answer, the canonical
`memory-model.md` concurrency section needs to be rewritten to match — right now it is the chapter
most likely to actively mislead a contributor who takes it as settled.

### 3.2 `teko repl`: native-only mechanism — **flagged by the architect**
The REPL is ratified-retired (M1). No native replacement mechanism is ratified — the
canonical doc's "-O0 debug-compile-and-run" framing is a proposed resolution to a gap the retirement
ruling explicitly scoped *out*. Needs an owner call: build a native REPL (accepting the
compile-per-line cost the retirement doc flagged as out of scope), or drop `teko repl` from the
target-state story entirely.

### 3.3 `this`/`base`/`static` OOP syntax — **flagged by the architect**
`docs/design/oop-this-base-static.md` is a complete, law-clean, ready-to-implement proposal
(front-end-only rename, codegen byte-identical) with exactly one open fork: hard-cut vs. dual-syntax
migration. Current code and the canonical docs both still reflect the **old** syntax (bare untyped
`self`, `class Base(binding)`) — internally consistent with each other, just silent about the pending
decision. Needs the owner's one-line call on migration shape (the design doc recommends hard-cut,
sequenced before the fase-3 collections work).

### 3.4 `TEKO_LEGISLATION.md` — native-only, confirmed in this pass; fixed in this sweep
`TEKO_LEGISLATION.md` (lines ~397–423) is native-only: native is the sole execution engine per
`TEKO_MASTER_PLAN.md`'s 2026-07-13 ruling. This is a legislative document — of higher authority
than any roadmap note — and has had the distillation pass to remove any stale alternate-engine
language.

---

## 4. Product-lens read: does the as-if-done story cohere?

**The pitch itself is coherent and defensible.** "Safe systems language without a GC or a borrow
checker, errors as values, a small deliberate surface, tests as a build gate" is a real, understandable
position relative to Rust/Zig/Go, and the parts of it that are actually built (arena memory, optionals,
`match`, generics-via-monomorphization, the test-gate-before-codegen model, the LSP *server*) hang
together and demonstrate the pitch is buildable, not just marketing. This is not a project whose
as-if-done story is fantasy at the architecture level.

**Where a user would get confused if they took the canonical docs at face value today:**
- Clone the repo expecting `teko build` to emit native objects with no `cc` involved — it still
  emits C under the hood for the default path; the native backend can't yet even rebuild the compiler
  on any platform.
- Install the VS Code extension expecting hover/diagnostics/completion — get syntax coloring only,
  with a live command-injection bug sitting in the same package.
- Reach for `teko doc`, `teko lint`, `teko repl`, or `tdb` — all four are either fully missing or a
  retired/never-built dead end.
- Try to add a dependency from a registry, or trust a published package's signature — none of that
  machinery exists; only same-directory `packages/*.tkl` loading works.
- Copy the README's own first code sample (`error::new("boom")`) — it won't compile.

None of this is "the doc describes something the design can't sustain" (i.e., nothing here is
architecturally fantastical) — it's "the doc describes the finish line as if the runner had already
crossed it," in areas where the runner is still mid-race (native backend, packages, editor tooling,
repl/tdb) or, in one case (concurrency), may be running toward a moved finish line.

**Is `README.draft.md` an honest, vendable target, or does it promise too much?** As a *target*
document staged for review (with its own "not yet live" banner and links back to
`TEKO_MASTER_PLAN.md`), it's a reasonable thing to have written and to keep un-merged. As a **preview
of what would replace the real README**, it currently promises too much for a first-time visitor:
the native-backend framing, the `tdb` bullet, the editor-tooling bullet, and the `error::new` sample
all need to be either qualified or removed before this file could honestly become the real README.

---

## 5. Concrete recommendations for the REAL `README.md` (not applied — recommendation only)

**Keep as-is (already honest, don't regress on merge):**
- The current caveat "C is the current lowering target; an own AOT backend is the 0.3 direction, not
  yet shipped" — this is *more* accurate than the draft's framing and should survive any merge.
- The current `error { message = "boom" }` code sample — it compiles today; don't swap it for the
  factory form until the factory exists.
- The current Quick Start / prerequisites section (matches reality: C23 toolchain used to link
  generated programs, no claim of a linker-free path).

**Adopt from the draft, once true (do not merge until then):**
- The "all-native output, no host `cc`" framing — hold until the native backend reaches the
  `gen2==gen3` fixpoint on at least one platform, and until `teko test` stops silently using the C
  route regardless of `TEKO_BACKEND`.
- The LSP/editor-tooling bullet — hold until at least one editor has a real `LanguageClient` wired to
  `teko lsp` (today: syntax highlighting only, plus an open command-injection bug to fix first).
- The `tdb` bullet — hold until `tdb` has *any* implementation; today it's a design doc.
- The `error::new(...)` sample — hold until the factory functions exist; keep the struct-literal
  form in the meantime.

**New, worth adding to the real README regardless of the draft:**
- A one-line pointer to `docs/canonical/README.md` (already present in the draft, absent from the
  real README) — cheap and orients a reader who wants the target-state story without over-promising,
  *provided* the "Open / needs a ruling" gap above gets closed first so that pointer doesn't inherit
  the same overstatement risk.

---

## 6. Summary table (quick scan)

| Area | State | Distance to as-if-done |
|---|---|---|
| Compiler pipeline (lex/parse/check/LIR) | pronto | — |
| Native AOT backend | parcial | reach native `gen2==gen3` on ≥1 platform; fix test-gate blind spot |
| Own linker (from scratch) | desenho (doc agrees) | not gating; correctly framed |
| Memory model — arena/adopt | pronto | — |
| Memory model — spine enforcement / `mem::free` | parcial | land PR-2/PR-3 gates + `mem::free` |
| Isolate concurrency (user-facing) | não existe / possibly superseded | owner ruling needed (§3.1) |
| Errors as values (core) | pronto | — |
| Error factory (`error::new`/`join`) | não existe | build it, or stop describing it as already-idiomatic |
| Test gate + coverage + `.tkr` | pronto | — |
| Test parallelism = "isolate per test" | parcial (real, different mechanism) | land `tk_task` design or restate doc |
| LSP server | pronto | semantic-tokens capability flag |
| LSP editor clients | não existe | build `LanguageClient` wiring; fix injection bug first |
| `teko fmt` | pronto | corpus-wide reformat + CI gate |
| `teko doc` / `teko lint` | não existe | fully unbuilt |
| `teko repl` | retirado, unratified revival | owner ruling needed (§3.2) |
| `tdb` debugger | desenho (substrate real) | everything past `.tsym`+DWARF |
| FFI / `extern` / `teko_rt` | pronto | — |
| Local package deps / `.tkl` | pronto | — |
| Package resolver/lock/registry/security | não existe | the single largest all-design area |
| Stdlib (io/iter/math/json) | pronto–parcial | generic-constrained math ops (S6) |
| Self-host fixpoint (native) | parcial | same as native backend |
