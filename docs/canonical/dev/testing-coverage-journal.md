# Testing, Coverage, and the Run Journal

Testing is not bolted onto Teko's build — it is a build **gate**. `teko build` runs the
project's tests before codegen ever starts; a failing test, or coverage below the manifest's
declared floor, bars the release outright.

## Writing tests

A test is a Teko function annotated `#test`, in a file ending `.tkt`, living **beside** the
code it tests (`foo.tks` + `foo_test.tkt`, same namespace — there is no separate `test/`
directory). `teko test .` discovers and runs every `#test` function in the project. Assertions
report both sides on failure (`eq_i64 — expected 42, got 41`), not just pass/fail, because a
test suite whose failures don't say what diverged is not much better than one that doesn't run.

New language features carry a **regression example** under `examples/regressions/<name>/` — a
`.tkp` project plus source whose exit code is the proof of the behavior, verified against
native execution — in addition to unit tests.

## The coverage gate

The manifest's `[coverage]` table sets function/line/branch floors (default 80% each when
absent); `teko build` enforces them as part of the same gate that runs the tests. Coverage can
be exported as Cobertura XML (`teko test . --coverage`) for CI/reporting integration. New or
changed code covers all its branches and lines; an arm that is genuinely unreachable is listed
with a stated reason rather than silently excluded from the count.

`--no-verify` is the one deliberate escape hatch, and it is total: it routes straight to
codegen with no gate of any kind, never partially. It exists because the self-hosting bootstrap
ladder (`self-host-fixpoint.md`) has to rebuild every rung of its own history without re-proving
what a completed, already-gated generation already established — running the full gate on every
rung of a multi-step bootstrap would re-verify the same thing dozens of times for no benefit.
It is not a partial-skip flag, and no other flag silently reduces gate scope — a gate that can
be halfway skipped without saying so is treated as a worse defect than no gate at all.

## Test execution model: isolate-per-test, root-per-isolate

Each test runs in its own **isolate** (see `memory-model.md`): a genuinely separate OS thread
with its own arena root, "as if it were a separate program." This is a deliberate, load-bearing
choice over a suspension-style (async/await) model, made in this order:

1. **isolate == isolated thread** (1:1 with the OS thread today; an M:N backing under the same
   surface is a possible future implementation, never a visible-behavior change).
2. **A fresh arena root at birth**, not a mark inside a shared root — each test tree-roots its
   own region (`tk_region_new(NULL)`) and drops it whole on completion
   (`tk_region_drop`), rather than pushing/popping a shared process-global arena around each
   test. This is what makes arena reuse between tests structurally impossible rather than
   merely policed — a defect class (double-rewind, one test's teardown unwinding into another
   test's live allocations) is made unreachable instead of caught after the fact.
3. **Synchronization/sharing is layered on top, later, and only where genuinely needed** —
   channels (`chan<T>`), not a shared arena domain, for anything that must cross isolate
   boundaries.

Test **lanes** (not one isolate per test) are what actually run in parallel:
`hardware_parallelism()` lanes, each running many tests in strict sequence — so expensive
per-lane setup happens once per lane, not once per test, without needing to group isolates
under a shared domain to get that amortization.

## The run journal

The run journal is the accounting layer under the parallel test harness: it gives every test
its own **channel** for stdout/stderr capture, with a **verdict-first line** —
`test <label> ... ok` — that is atomic and never displaced by anything the test body itself
prints. This is what makes a parallel run's output legible: a failing test's diagnostic output
is attributed to the test that produced it, never interleaved with another lane's output or
mistaken for a different test's result because the pass/fail line happened to land somewhere
else in the stream.

On top of per-test capture, the journal maintains:

- **Per-run identity** — each test run is distinguishable, so a scratch-space collision
  between two runs (or two lanes within a run) is a provable, structural impossibility rather
  than an occasionally-observed flake.
- **Coverage union across shards** — parallel lanes each accumulate coverage independently;
  the journal merges them into one report identical to what a serial run would have produced.
- **`--replay`** — a completed run's journal can be replayed for inspection without re-running
  the underlying tests, useful for triaging a CI failure after the fact without needing to
  reproduce timing-sensitive conditions live.
- **A single unified tally** surviving both an aborted unit-test phase and a fully-completed
  regression phase, so "how many tests ran, how many passed, why did each skip" is one
  question with one answer regardless of which phase stopped early.

## The regression suite (`.tkr`)

Beyond `#test` unit tests, the project manifest lists an explicit, ordered set of `.tkr`
regressor files (`[tests] regression = [...]`) — no directory discovery, no glob: a listed
path that doesn't resolve is a manifest error, never a silently-skipped entry. Each `.tkr`
scenario states `Given`/`When`/`Then` steps; a scenario whose contract *is* its exit code
(cross-compiled objects, self-host fixpoint checks, and a handful of declarative,
already-established platform facts) may validly have only a `Then exit` — but a scenario
lacking any `Then` at all asserts nothing and is a defect in the suite, not a passing test.

A `skipped` step on a path that is supposed to run unconditionally is treated as a failure,
not a quiet pass — a gate that can be skipped without anyone noticing protects less than no
gate, because it *looks* like protection.
