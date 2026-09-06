# §10 Concurrency — the FIRST batch (a working `spawn`) at implementation depth

**Status:** DESIGN-AHEAD (architect). No product `.tks`/`.c` changed by this doc.
**Ground truth:** HEAD `be73b604` (`git rev-parse HEAD`).
**Builds on:** `docs/design/plano-s10-concorrencia-crumbs.md` (the surviving summary — crumb spine + D1/D2).
**Spec:** `docs/design/mudancas-superficie-0.3.1.md` §10 (~L669-1052, sealed). `spawn f(args)` =
fire-and-forget thread; args BY COPY; sub-root arena; no return, no join. `spawn` is a CONTEXTUAL
keyword (Ruling B) — zero corpus collisions (verified below).

**Scope of THIS doc:** the four crumbs `C0a + S1 + S2 + S3` only. Channels (C0b/C0c/C1-C5), `await`
(A1-A4), `cancel`, `journal` are LATER batches and are out of scope here.

**Corpus-leaf invariant (holds for all four crumbs):** `grep -rn '\bspawn\b' src/` finds 14 hits, ALL
in comments/strings about process-spawning (`teko::process`, `win32_compat.h`, build stage text) — ZERO
are an identifier named `spawn`, and none is at statement head. The compiler NEVER writes `spawn` in
its own corpus (axis-2 parallel codegen uses a SEPARATE internal `fork_join`, §10.2 L757). Therefore
every reseed below stays mechanical/byte-identical on the corpus: the new grammar/checker/codegen paths
are added but never EXERCISED by `src/`, so the 2-pass fixpoint converges on the same emitted `teko.c`.

---

## Crumb C0a — `tk_thread_spawn` (maintained-C runtime) — the FIRST implementable crumb

**Files:** `src/runtime/teko_rt.{c,h}` (maintained-C, exempt from the Teko-only law); one additive link
flag in `src/build/project.tks`. **Precedent:** `tk_test_run` (`teko_rt.c:2753`) owns its `setjmp`
bracket so emitted code never sees it — `tk_thread_spawn` owns the `tk_task_begin`/`tk_task_end`
bracket for exactly the same reason (D1).

### C0a.1 — signatures (add to `teko_rt.h`, near the task decls at `teko_rt.h:213-216`)

```c
/* tk_thread_spawn — fire-and-forget: start a DETACHED OS thread that runs `entry(ctx_blob)` on its
 * OWN sub-root arena. The trampoline `entry` (emitted by codegen, one per spawn-site) is the only
 * caller of the target fn; `ctx_blob` is a single self-contained malloc'd block holding a deep COPY
 * of every argument (the spawning thread packed it before this call). Ownership of `ctx_blob` passes
 * to the spawned thread, which frees it after `entry` returns. Never blocks the caller; returns as
 * soon as the thread is created. Panics (never returns an error code) if the OS refuses the thread —
 * a spawn that cannot start is a program-fatal condition, matching tk_task_begin's OOM panic.
 *
 * §16 NOTE: pthread_create is the TRANSITIONAL mechanism. clone(2)/CreateThread selection is an
 * internal runtime detail deferred to §16 (which rewrites all of teko_rt.c to Teko/FFI). Nothing
 * outside this file may assume pthreads. */
void tk_thread_spawn(void (*entry)(void *), void *ctx_blob);

/* tk_thread_join_spawn — the JOINABLE twin (await-batch, D2-option-a). Same contract as
 * tk_thread_spawn but returns an opaque handle the caller can later hand to tk_thread_join to block
 * until the thread's `entry` has returned. UNUSED by the spawn surface (spawn is detached); declared
 * now so the await lowering (A4) resumes in minutes when D2 is ruled. Marked await-batch — do NOT
 * wire a Teko surface to it in this batch. */
uint64_t tk_thread_join_spawn(void (*entry)(void *), void *ctx_blob);   /* AWAIT-BATCH — not used by spawn */
void     tk_thread_join(uint64_t handle);                               /* AWAIT-BATCH — not used by spawn */

/* tk_thread_spawn_selftest — the C-owned probe body the `thread_spawn_paranoid` fixture reaches
 * through ONE ordinary extern call (the tk_test_capture_probe pattern, teko_rt.c:2798 — a Teko test
 * cannot pass a `void(*)(void*)` because `cabi fn` is not a token the lexer mints). Spawns `n`
 * threads, each doing tk_task_begin/alloc/tk_task_end, JOINS them all (deterministic — detached
 * threads give the test no finish edge), then returns tk_names_live_count(). Under TEKO_MEM_PARANOID=1
 * the caller asserts the return equals the pre-spawn baseline. */
uint64_t tk_thread_spawn_selftest(int64_t n);
```

### C0a.2 — the implementation (add to `teko_rt.c`, sibling of tk_task_begin/end at :2496-2522)

```c
#include <pthread.h>   /* §16-TRANSITIONAL: pthread_create/join. Retired when teko_rt.c goes to FFI. */

/* tk_thread_trampoline_arg — what pthread's void* start-routine slot carries: the codegen trampoline
 * `entry` and its ctx_blob, so ONE C start-routine can serve every spawn-site. Itself malloc'd by
 * the spawning thread, freed FIRST thing on the new thread. */
typedef struct { void (*entry)(void *); void *ctx_blob; } tk_thread_call;

/* tk_thread_start — the pthread start-routine. The arena bracket lives HERE, invisible to emitted
 * code: tk_task_begin installs a FRESH sub-root arena for this thread (the _Thread_local
 * tk_g_current_task, teko_rt.c:1386, is per-thread — the new thread starts on tk_g_main_task's zero
 * value and task_begin gives it its own), `entry(blob)` runs the deep-copied args + the target,
 * tk_task_end frees the whole arena. The blob is freed by `entry` itself (see the S3 trampoline);
 * `tk_thread_call` is freed here. */
static void *tk_thread_start(void *raw) {
    tk_thread_call call = *(tk_thread_call *)raw;
    free(raw);
    tk_task *saved = tk_task_begin();   /* this thread's OWN sub-root arena (D1) */
    call.entry(call.ctx_blob);          /* trampoline: unpack blob -> target(args) -> free(blob) */
    tk_task_end(saved);                 /* the arena dies with the thread */
    return NULL;
}

void tk_thread_spawn(void (*entry)(void *), void *ctx_blob) {
    tk_thread_call *call = malloc(sizeof *call);
    if (call == NULL) tk_panic("out of memory");
    call->entry = entry;
    call->ctx_blob = ctx_blob;
    pthread_t th;
    if (pthread_create(&th, NULL, tk_thread_start, call) != 0) tk_panic("spawn: cannot create thread");
    pthread_detach(th);                 /* fire-and-forget: no join, no return, no handle (§10) */
}
```

**Arena-ownership rule (the whole point of C0a):** the spawned thread's `_Thread_local
tk_g_current_task` is INDEPENDENT of the spawning thread's (teko_rt.c:1383-1393 — one `%fs`-relative
load per thread). `tk_task_begin` on the new thread starts from an EMPTY discipline (no root, no marks
— teko_rt.c:2492-2502) so its `tk_arena_pop`/`tk_task_end` can NEVER reach another task's allocations.
Args do not cross arenas because they were deep-copied into `ctx_blob` (libc heap, task-independent) by
the spawning thread BEFORE hand-off; the target reads only the blob, never the spawning arena. There is
therefore no cross-arena aliasing to protect against — the copy IS the isolation.

### C0a.3 — the joinable twin (await-batch — compile it now, do not wire it)

```c
typedef struct { pthread_t th; } *tk_thread_handle;   /* opaque; heap-boxed so a u64 can name it */

uint64_t tk_thread_join_spawn(void (*entry)(void *), void *ctx_blob) {
    tk_thread_call *call = malloc(sizeof *call);
    if (call == NULL) tk_panic("out of memory");
    call->entry = entry; call->ctx_blob = ctx_blob;
    tk_thread_handle h = malloc(sizeof *h);
    if (h == NULL) tk_panic("out of memory");
    if (pthread_create(&h->th, NULL, tk_thread_start, call) != 0) tk_panic("spawn: cannot create thread");
    return (uint64_t)(uintptr_t)h;   /* NOT a neg-cast: uintptr_t is unsigned — the i64->u64 trap does not apply here */
}
void tk_thread_join(uint64_t handle) {
    tk_thread_handle h = (tk_thread_handle)(uintptr_t)handle;
    pthread_join(h->th, NULL);
    free(h);
}
```

### C0a.4 — fixture `thread_spawn_paranoid` (how to drive it BEFORE the `spawn` surface exists)

The surface does not exist yet, so the fixture reaches the primitive the SAME way `tk_test_capture_probe`
(teko_rt.c:2798) reaches `tk_test_run`: a C-owned selftest body invoked through ONE plain-scalar extern.

```c
/* tk_thread_spawn_paranoid_body — one thread's work: a fresh task, a handful of allocations, a clean
 * task end. Under TEKO_MEM_PARANOID=1 the freed blocks are poisoned + never parked (teko_rt.c:4750),
 * so a stray cross-task pointer would corrupt and the live-count would drift. */
static void tk_thread_spawn_paranoid_body(void *unused) { (void)unused;
    for (int i = 0; i < 32; i++) { volatile char *p = tk_alloc(64 + (size_t)i); p[0] = (char)i; }
}

uint64_t tk_thread_spawn_selftest(int64_t n) {
    uint64_t baseline = tk_names_live_count();
    if (n < 0) n = 0;
    /* JOIN each thread — a detached spawn gives the test no finish edge, so the paranoid selftest uses
     * the joinable twin to be deterministic (this is exactly why the twin is designed now). */
    for (int64_t i = 0; i < n; i++) {
        uint64_t h = tk_thread_join_spawn(tk_thread_spawn_paranoid_body, NULL);
        tk_thread_join(h);
    }
    return tk_names_live_count() - baseline;   /* MUST be 0: every task_begin/alloc/task_end balanced */
}
```

**The probe (a new example `examples/probes/thread_spawn/`), Teko side — the extern shape names.tks
already uses (names.tks:84, `extern fn live_count_raw(): u64 = "tk_names_live_count" from "teko_rt"`):**

```
// examples/probes/thread_spawn/src/main.tks   (own native backend, Linux)
extern fn thread_spawn_selftest(n: i64): u64 = "tk_thread_spawn_selftest" from "teko_rt"

fn main() {
    // 100 threads, each a full task_begin/alloc/task_end cycle. Delta MUST be 0.
    var drift = thread_spawn_selftest(100)
    if drift != 0 { teko::process::exit(1) }
    teko::process::exit(42)   // the probe's success sentinel (matches chan_dgram's "-> 42" convention)
}
```

Driven with `TEKO_MEM_PARANOID=1 ./examples/probes/thread_spawn/bin/thread_spawn` → exit **42**;
any live-count drift → exit **1**. `n=100` exercises the paranoid poison across 100 independent arenas.
The extern takes only a scalar (`i64`→`u64`), so it compiles today — no `cabi fn` needed.

### C0a.5 — the link flag (the ONE corpus touch C0a needs)

`pthread_create`/`pthread_join`/`pthread_detach` resolve from libc on glibc ≥ 2.34 (the target,
Linux 6.18/Fedora) with no extra flag, but for portability add `-pthread` to BOTH the compile and link
argv builders in `src/build/project.tks` (the `teko_rt.c`-object compile at project.tks:2126 and the
final link at project.tks:1170 — the same argv the `-lm` note at project.tks:1118 describes). This is
ADDITIVE and mechanical. Reseed impact is reasoned in the ritual section (it does NOT break fixpoint
byte-identity — the flag changes the C-compiler invocation, never a single byte of emitted `teko.c`).

---

## Crumb S1 — parser: `Spawn` AST node + contextual `spawn` recognition

**File:** `src/parser/parse_stmt.tks` (+ the node in `src/parser/ast.tks`). **Model:** `is_adopt_head`
(parse_stmt.tks:183) / `parse_adopt` (:200) — the exact position-based contextual-keyword shape.
`spawn` is NOT lexer-reserved (no `TokenKind::Spawn`); it is recognized ONLY as `Ident("spawn")` at
statement head immediately followed by a call.

### S1.1 — the AST node (add to `src/parser/ast.tks`, beside `AdoptStmt` at :427)

```
/**
 * Spawn — a `spawn f(args)` statement (§10): fire-and-forget concurrent execution of a CALL. The
 * target is stored as a full `Expr` (whose `.kind` the checker requires to be a `Call` or
 * `MethodCall`), so the existing call typing/codegen is reused verbatim rather than duplicated.
 * `spawn` is a CONTEXTUAL statement head (not a hard keyword) recognized by ident text + a following
 * call at statement position; an identifier named `spawn` anywhere else stays legal. INERT in the
 * corpus (no `spawn` in `src/`), so its whole grammar/checker/codegen path is dead code for the
 * compiler's own fixpoint.
 *
 * @field call  the call expression to run on a fresh detached thread (kind must be Call/MethodCall).
 * @since 0.3.1 §10 (concurrency batch 1 — a working spawn)
 */
pub type Spawn = struct { call: Expr }
```

Add `parser::Spawn` to the `@Statement()` union macro (ast.tks:541):

```
macro Statement() { lowering { parser::Binding | parser::Assign | parser::Return | parser::LoopStmt | parser::BreakStmt | parser::ContinueStmt | parser::ExprStmt | parser::DeferStmt | parser::AdoptStmt | parser::BlockStmt | parser::MultiBind | parser::LoweringFrag | parser::Spawn } }
```

### S1.2 — recognition + parse (add to `src/parser/parse_stmt.tks`)

```
/**
 * is_spawn_head — is the token at `pos` a CONTEXTUAL `spawn` statement head? True ONLY for the ident
 * text "spawn" immediately followed by another ident that begins a call — i.e. `spawn NAME(` or
 * `spawn NAME.method(` / `spawn ns::NAME(`. Any other use of a symbol named `spawn` (an assignment
 * target `spawn = …`, a bare expression `spawn`, a field/method receiver `spawn.x`, a call
 * `spawn(...)` where spawn is itself the callee) is NOT a head and falls through to ordinary parsing,
 * preserving the contextual-keyword invariant (Ruling B). The `spawn(` case is deliberately EXCLUDED
 * (an LParen right after `spawn` means `spawn` is the callee, not the keyword).
 *
 * @param []lexer::Token tokens  the token stream
 * @param u64 pos  the candidate statement-head index
 * @return bool  true iff `pos` opens a `spawn <call>` statement
 * @since 0.3.1 §10 (concurrency batch 1)
 */
fn is_spawn_head(tokens: []lexer::Token, pos: u64): bool {
    if !is_kind_at(tokens, pos, lexer::TokenKind::Ident) { return false }
    if tokens[pos].text != "spawn" { return false }
    is_kind_at(tokens, pos + 1, lexer::TokenKind::Ident)   // `spawn NAME…` — a call target follows; `spawn(`/`spawn.`/`spawn =` are NOT heads
}

/**
 * parse_spawn — parse a contextual `spawn <call>` at `pos` (the `spawn` ident; is_spawn_head true).
 * Parses ONE full expression after the keyword and requires its kind to be a `Call` or `MethodCall`;
 * anything else (a bare var, a binary expr, a literal) is a parse error — `spawn` runs a CALL, never
 * an arbitrary expression. No block, no arguments of its own.
 *
 * @param []lexer::Token tokens  the token stream
 * @param u64 pos  the index of the `spawn` ident
 * @return Parsed<@Statement()> | error  the parsed Spawn and the index past the call, or a parse error
 * @throws  the target is not a call (`spawn x`, `spawn 1 + 2`), or a parse error inside the call
 * @since 0.3.1 §10 (concurrency batch 1)
 */
fn parse_spawn(tokens: []lexer::Token, pos: u64): Parsed<@Statement()> | error {
    var e = match parse_expr(tokens, pos + 1) { Parsed<Expr> as x => x; error as err => return err }
    var ok = match e.node.kind { Call as _c => true; MethodCall as _m => true; _ => false }
    if !ok { return err_at(tokens, pos + 1, "`spawn` must be followed by a call, e.g. `spawn worker(n, msg)`") }
    Parsed<@Statement()> { node = Spawn { call = e.node }; next = e.next }
}
```

### S1.3 — dispatch wiring (in `parse_statement`, parse_stmt.tks:59)

Insert BEFORE the `is_adopt_head` test (parse_stmt.tks:108) — order among contextual heads is
irrelevant (each keys on distinct ident text), but placing it with the other contextual heads keeps
the block cohesive:

```
    if is_spawn_head(tokens, pos) {
        return parse_spawn(tokens, pos)
    }
```

**Collision confirmation:** because `is_spawn_head` requires `Ident("spawn")` FOLLOWED BY an `Ident`,
and the corpus never writes `spawn <ident>` at statement head (0 hits), the branch is never taken while
compiling `src/`. `spawn(...)`, `x.spawn`, `spawn = …`, `let spawn = …` all remain ordinary — the
contextual-keyword invariant holds.

---

## Crumb S2 — checker: validate the spawn target (the ref-guard)

**File:** `src/checker/typer.tks` (dispatch at :7365) + the typed node in `src/checker/tast.tks`
(:198/:213/:226). Rules, in order: (1) the target is a call (already guaranteed by S1, re-asserted for
the typed form); (2) **no `ref` param crosses the spawn boundary** (arena UAF — a `ref` is a
`Ref<T>`/`Reference` naming memory in the SPAWNING arena, which the spawned thread would read after
the spawning scope drops); (3) each arg is copyable by value into the blob (the S3 deep-copy must be
synthesizable for its type). No captured out-of-arena references arise because a `spawn` has no
closure body — only a call with explicit args, each independently guarded.

### S2.1 — the typed node (add to `src/checker/tast.tks`, beside `TAdoptStmt` at :213)

```
/**
 * TSpawn — the typed `spawn <call>` (§10). Carries the typed call so codegen reuses emit_call and the
 * deep-copy sees each arg's resolved `@Type()`. The checker has already proven: the call resolves, no
 * parameter is a `Reference` (ref-guard), and every argument type is blob-copyable.
 *
 * @field call  the typed call expression (its `.kind` is a `TCall`; `.type` is the call's return
 *              type, discarded — spawn is fire-and-forget with no return).
 * @since 0.3.1 §10 (concurrency batch 1)
 */
pub type TSpawn = struct { call: TExpr }
```

Add `checker::TSpawn` to the `@TStatement()` union (tast.tks:226):

```
macro TStatement() { lowering { checker::TBinding | checker::TAssign | checker::TReturn | checker::TLoopStmt | checker::TBreakStmt | checker::TContinueStmt | checker::TExprStmt | checker::TDeferStmt | checker::TAdoptStmt | checker::TBlockStmt | checker::TSpawn } }
```

### S2.2 — the typing + ref-guard (add to `src/checker/typer.tks`, beside `type_adopt` at :7425)

The ref-guard **mechanism**: a `ref T` PARAMETER resolves to `checker::Reference { inner = T }`
(type.tks:154 — a `ref` binder is the only holder of a `Reference`; parse_type wraps `ref T` into
`Ref<T>` which resolves to `Reference`). The callee's whole signature is `lookup_call`'s result
(scope.tks:432), a `Func` whose `params: []@Type()` (type.tks:120). So the guard is: resolve the
callee to its `Func`, scan `params`, reject if ANY member matches `checker::Reference`. Note: `Param`
(ast.tks:588) has NO `is_ref` field — `ref`-ness lives ENTIRELY in the resolved param TYPE, which is
why the guard reads `Func.params` (resolved) rather than the parser `Param` list.

```
/**
 * type_spawn — type-check a `spawn <call>` (§10). Types the call via the ordinary expr path (so
 * arity/named-args/defaults are checked exactly as a bare call), then applies the two spawn-boundary
 * rules: NO `ref` parameter (a `Reference` names spawning-arena memory the detached thread would read
 * after the scope drops — arena UAF), and every argument type must be blob-copyable (S3's deep-copy
 * must exist for it). The call's return type is discarded — spawn never returns.
 *
 * @param parser::Spawn s  the parsed spawn statement (its `.call.kind` is a Call/MethodCall — S1)
 * @param Env env  the enclosing type environment (unchanged on return — spawn binds nothing)
 * @param TypeTable table  the program type table
 * @return TypedStmt | error  the typed TSpawn, or a spawn-boundary violation / propagated call error
 * @since 0.3.1 §10 (concurrency batch 1)
 */
fn type_spawn(s: parser::Spawn, env: Env, table: TypeTable): TypedStmt | error {
    var te = match type_expr(s.call, env, table) { TExpr as x => x; error as e => return e }
    var callee = match tspawn_callee_path(te) { parser::Path as p => p; error as e => return e }
    var fty = match lookup_call(env, callee) { @Type() as t => t; error as e => return e }
    match fty {
        Func as f => {
            match tspawn_reject_ref_params(f, callee) { error as e => return e; _ => { } }
            match tspawn_reject_uncopyable_args(f, callee) { error as e => return e; _ => { } }
        }
        _ => return error { message = "internal: spawn target did not resolve to a function" }
    }
    TypedStmt { node = TSpawn { call = te }; env = env }
}

/**
 * tspawn_reject_ref_params — the REF-GUARD. Scans the resolved callee signature and rejects the FIRST
 * parameter typed `Reference` (a `ref T`): a `ref` may not cross the spawn boundary because it names
 * memory in the spawning thread's arena, which the detached thread would dereference after the
 * spawning scope has dropped it (use-after-free). The spec forbids `ref` crossing spawn/await.
 *
 * @param Func f  the resolved callee function type (params already resolved to `@Type()`)
 * @param parser::Path callee  the call target path (for the message)
 * @return bool | error  true when no param is a reference, else the rejection error
 * @since 0.3.1 §10 (concurrency batch 1)
 */
fn tspawn_reject_ref_params(f: Func, callee: parser::Path): bool | error {
    var i: u64 = 0
    loop {
        if i >= f.params.len { break }
        match f.params[i] {
            Reference => return error { message = teko::str::concat(path_last_name(callee), ": a `ref` parameter may not cross a `spawn` boundary — the reference names memory in the spawning thread's arena, which is freed when that scope exits (§10). Pass the value by copy instead.") }
            _ => { }
        }
        i = i + 1
    }
    true
}
```

`tspawn_reject_uncopyable_args` applies the SAME per-type predicate S3's deep-copy is defined over
(scalars, `str`, `[]T` of copyable `T`, flat structs of copyable fields). It honest-stops the v1
boundary — a CLASS instance, a `Ref`, a closure value — with:
`"<name>: a `spawn` argument of type <T> cannot yet be copied across the thread boundary (batch 1
supports scalars, str, slices and flat structs; class/closure/ref args are a later batch)"`. This keeps
S3 tractable and shippable while never silently mis-copying a graph. `tspawn_callee_path` extracts the
`parser::Path` from `te.kind` (TCall → `.callee`; a MethodCall lowers to its resolved TCall — reuse the
existing method-resolution result). `path_last_name`/`tspawn_callee_path` are tiny local helpers.

### S2.3 — dispatch wiring (in `type_statement`, typer.tks:7365)

Add the arm alongside `parser::AdoptStmt` (typer.tks:7391):

```
        parser::Spawn as sp => type_spawn(sp, env, table)
```

**Every other `@Statement()`/`@TStatement()` walker** (the borrow/escape/consteval/comptime/lambda-
collect passes that pattern-match the unions — borrow.tks:677+, escape.tks:327+, collect.tks:747+,
comptime_*.tks, consteval.tks, typer.tks:120/2738) gains a `parser::Spawn`/`checker::TSpawn` arm that
recurses into the single call expression (mirroring the `TExprStmt` arm — a spawn is, for every
analysis, one call run for effect). These are mechanical one-line additions; the exhaustiveness of the
inline unions forces the compiler to point at each site, so none is missed.

---

## Crumb S3 — codegen: ctx-blob + `cabi` trampoline + `tk_thread_spawn` call (the hardest crumb)

**File:** `src/codegen/codegen.tks` (C backend — the shipping path for spawn). The native/LIR backend
(`src/lir/lower.tks`, `lower_stmt` at :6297) HONEST-STOPS `TSpawn` via its existing `_ => error {…}`
fall-through (message: `"native backend: spawn not yet lowered (C backend only in batch 1)"`) — sound
because the corpus never spawns, so the compiler's own native build is unaffected; a user program that
needs `spawn` builds on the C backend.

### S3.1 — the four synthesized pieces, per spawn-site

For `spawn worker(n, msg)` (namespace `app`, `worker(n: i64, msg: str)`), codegen synthesizes, keyed by
a fresh id `<k>` (`fresh_named`, the emit_adopt pattern at codegen.tks:10354):

**(1) the ctx-blob struct** — one contiguous, self-contained heap block; ONE `free` reclaims it:

```c
typedef struct {                 /* tk_spawn_ctx_<k> — the packed args for THIS spawn-site */
    int64_t a0;                  /* scalar arg: stored inline */
    tk_str  a1;                  /* fat arg: (ptr,len). `ptr` is FIXED UP to point INSIDE this block */
} tk_spawn_ctx_<k>;
```

Fat payloads (the bytes `a1.ptr`/an `[]T`'s elements) are packed into a flexible tail of the SAME
malloc, and each fat field's `ptr` is set to `((char*)blob) + offset`. Result: the blob owns its entire
transitive closure; nothing points into the spawning arena; `free(blob)` frees it all.

**(2) the trampoline** — matches `tk_thread_spawn`'s `void(*)(void*)`, emitted as a top-level static C
function (the lifted-lambda pattern, codegen.tks — top-level fns named via `cb_fn_name`):

```c
/* tk_spawn_tramp_<k> — the `entry` tk_thread_spawn calls on the new thread. Runs INSIDE the
 * tk_task_begin/tk_task_end bracket that tk_thread_start owns (C0a), so the target executes on this
 * thread's own arena. Unpacks the blob, calls the target, and — LAST — frees the blob. The blob is
 * freed AFTER the target returns (its fat args may be aliased by arena values the target built); the
 * arena itself is freed by tk_task_end in tk_thread_start, which runs after this returns. */
static void tk_spawn_tramp_<k>(void *raw) {
    tk_spawn_ctx_<k> *c = (tk_spawn_ctx_<k> *)raw;
    teko_app__worker(c->a0, c->a1);   /* the mangled target — cb_fn_name(ns="app", name="worker") */
    free(raw);
}
```

**(3) the deep-copy at the spawn site** (emitted inline where the `spawn` statement sits, on the
SPAWNING thread, while the spawning arena is live):

```c
{
    tk_spawn_ctx_<k> *c = tk_spawn_pack_<k>(n, msg);   /* malloc + memcpy the transitive bytes */
    tk_thread_spawn(tk_spawn_tramp_<k>, c);            /* (4) hand off; ownership passes to the thread */
}
```

where the synthesized packer copies the bytes out of the spawning arena into the heap block:

```c
/* tk_spawn_pack_<k> — deep-copy the args into ONE self-contained malloc. Scalars store inline; a fat
 * `tk_str`/slice copies its bytes into the block's tail and re-points `ptr` there. No arena pointer
 * survives. */
static tk_spawn_ctx_<k> *tk_spawn_pack_<k>(int64_t a0, tk_str a1) {
    size_t head = sizeof(tk_spawn_ctx_<k>);
    size_t need = head + a1.len;                 /* + every other fat payload's bytes */
    char *blk = malloc(need); if (!blk) tk_panic("out of memory");
    tk_spawn_ctx_<k> *c = (tk_spawn_ctx_<k> *)blk;
    c->a0 = a0;                                  /* scalar: bitwise */
    char *tail = blk + head;
    memcpy(tail, a1.ptr, a1.len);                /* fat: bytes copied out of the spawning arena */
    c->a1.ptr = tail; c->a1.len = a1.len;        /* re-point INTO the block */
    return c;
}
```

**(4) the `tk_thread_spawn` call** — shown in piece (3): `tk_thread_spawn(tk_spawn_tramp_<k>, c);`.

### S3.2 — dispatch wiring (in `emit_stmt_dispatch`, codegen.tks:10233)

Add the arm beside `checker::TAdoptStmt` (codegen.tks:10280). It emits ONLY the inline `{ pack; spawn; }`
block; the struct/trampoline/packer (pieces 1-3) are appended to the program's top-level lifted section
(the same `built.lifted` channel closures use, codegen.tks:6290) so they precede `main`:

```
        checker::TSpawn as sp => emit_spawn(b0, sp, prog, escaping, regions, fn_body, dctx, indent)
```

`emit_spawn` reads `sp.call.kind` as a `TCall` (its args carry resolved `@Type()`s via `.callee_type`
/`TExpr.type`), allocates `<k>` via `fresh_named("_tksp", …)`, and emits the four pieces. The per-arg
copy strategy is selected by the arg's `@Type()`: scalar → inline store; `str`/`[]T` → the tail-pack
memcpy above; flat struct → field-wise recursion of the same two cases. The deep-copy predicate is the
SAME one S2 gates on, so codegen never meets a type the checker did not clear.

### S3.3 — who owns/frees the blob, and why by-copy is safe

- **Allocated:** by the SPAWNING thread, via `malloc` (libc heap — NOT the arena). Heap, so neither the
  spawning arena's drop nor either thread's `tk_task_end` touches it.
- **Freed:** by the TRAMPOLINE (`free(raw)`), AFTER the target returns — one `free`, because the block
  is contiguous (all fat payloads live in its tail). The trampoline runs inside `tk_thread_start`, so
  the free happens before `tk_task_end` frees the new thread's arena — wait: order is
  `entry(blob){ target(); free(blob); }` then `tk_task_end`. The blob free precedes task_end; that is
  safe because the target has already RETURNED (fire-and-forget, no value escapes), so no live arena
  value is still being read out of the blob at free time. (If a later batch ever lets a value escape a
  spawned task, this order flips to free-after-task_end — noted for the await batch, irrelevant here.)
- **Why by-copy avoids reading the spawning arena after it drops:** every byte the target can reach was
  copied into the heap block by `tk_spawn_pack_<k>` BEFORE `tk_thread_spawn` returned — the packer
  re-points every fat `ptr` INTO the block. The spawning thread may drop its scope/arena the instant
  after `tk_thread_spawn` returns; the detached thread never dereferences anything but the heap block
  and its own fresh arena. There is no shared mutable state and no cross-arena pointer, so the classic
  spawn race (thread reads args from a caller frame that already returned) cannot occur.

### S3.4 — fixture `spawn_smoke` (regression, C backend)

A new `examples/regressions/spawn_smoke/` (the `own_native`/regression layout):

```
fn worker(tag: i64, msg: str) {
    // writes to a per-thread file whose name embeds `tag` — no shared-buffer race; the parent
    // verifies file contents after a bounded join-by-polling (spawn has no join, so the test
    // sleeps/polls for N files to appear, then reads them).
    ...
}
fn main() {
    var i = 0
    loop { if i >= 8 { break }; spawn worker(i, "hello"); i = i + 1 }
    // poll until 8 files exist, verify each contains its own tag + "hello", else exit(1)
    teko::process::exit(0)
}
```

Expected native exit codes: **0** on all 8 workers observed with correct per-tag content; **1** on a
missing/corrupt file (a cross-arena copy bug would corrupt `msg` or `tag`). A companion REJECT fixture
`spawn_reject/` asserts the S2 gates compile-fail:
- `spawn worker_ref(r)` where `worker_ref(ref x: i64)` → the ref-guard message (exit code of the
  compiler = failure; the fixture is a compile-must-fail case).
- `spawn 1 + 2` and `spawn x` (non-call) → the S1 parse error.
- `spawn make_node()` returning/taking a class arg → the S2 uncopyable-arg honest-stop.

---

## Ritual per crumb — reseed sequencing

| Crumb | Kind | Reseed | Corpus byte-identical? |
|-------|------|--------|------------------------|
| **C0a** | maintained-C (`teko_rt.{c,h}`) + 1 additive flag in `project.tks` | **runtime rebuild only; NO fixpoint reseed by itself** | YES — see reasoning below |
| **S1** | compiler-touching (parser) | **fixpoint + reseed** | YES — new grammar never exercised by `src/` |
| **S2** | compiler-touching (checker) | **fixpoint + reseed** | YES — new checker path never exercised by `src/` |
| **S3** | compiler-touching (codegen) | **fixpoint + reseed** | YES — new emit path never exercised by `src/` |

**The C0a "unused C function" question, reasoned out:** teko_rt.c is COMPILED and LINKED into every
artifact, but its source text is NOT part of what codegen EMITS. The emitted `teko.c` (the compiler's
self-image, and every user program's output) is produced by the `.tks` corpus running through codegen;
`tk_thread_spawn` appears in emitted `teko.c` ONLY when an emit path writes a call to it — and no
`.tks` does until S3 lands AND a program spawns. Therefore **adding `tk_thread_spawn` to teko_rt.c does
NOT change a single byte of any emitted `teko.c`.** C0a needs only a RUNTIME REBUILD (recompile
teko_rt.c into the release seed's object so the fixture can link) — not a codegen fixpoint reseed. The
2-pass fixpoint on the corpus stays byte-identical because the corpus emits the same `teko.c` it always
did; the new object is linked beneath it, invisibly.

**The `-pthread` flag, reasoned out:** it lives in `project.tks` (corpus), so it recompiles the
compiler — but it is DATA the compiler passes to the C-compiler at build time, never an input to
codegen. Compiling the corpus with the new `project.tks` still emits the identical `teko.c`; only the
subsequent `cc` invocation gains `-pthread`. So the fixpoint remains byte-identical on the emitted
`teko.c`; the reseed is mechanical. (On glibc ≥ 2.34 the flag is a no-op for symbol resolution, so even
the linked binary is behaviorally unchanged until a spawn is emitted.)

**Recommended landing order (each independently gate-able):**
1. **C0a** alone — teko_rt.{c,h} + `-pthread` + the `thread_spawn` probe. Rebuild runtime, run
   `TEKO_MEM_PARANOID=1` probe → 42. No fixpoint reseed. **This is the first implementable crumb.**
2. **S1** — parser + AST node + all `@Statement()` walker arms. Ritual: full gate + reseed.
3. **S2** — checker typing + ref-guard + `@TStatement()` walker arms. Ritual: full gate + reseed.
4. **S3** — codegen emit_spawn + native honest-stop. Ritual: full gate + reseed. Only AFTER S3 does an
   emitted `teko.c` ever contain a `tk_thread_spawn` call (and only for a user program that spawns — the
   corpus still never does, so the compiler's OWN self-image stays free of trampolines/threads).

**Ritual points (full gate must pass):** after S1, after S2, after S3 (each is a compiler-touching
crumb → fixpoint byte-identity + full regression). C0a's gate is the `thread_spawn_paranoid` probe under
`TEKO_MEM_PARANOID=1` plus a clean runtime rebuild; it does not require the compiler fixpoint. NEVER run
`teko test .` (OOM) — gate via the sharded/regression runners as the build tool already does.

---

## Risks + law tensions (recommended resolutions)

- **R1 — the `-pthread`/link dependency (Teko-only law tension).** Adding a link flag touches corpus
  (`project.tks`). RESOLUTION: it is maintained-runtime plumbing riding the C0a maintained-C change
  (the runtime IS exempt); the flag is additive, mechanical, fixpoint-neutral (reasoned above). No
  tension survives. Fallback if a target ever lacks pthreads: the D1 ruling already defers the
  thread-mechanism to §16, so clone(2)/CreateThread can replace pthread INSIDE tk_thread_spawn with
  zero surface/reseed impact.
- **R2 — deep-copy of arbitrary value graphs.** A full transitive clone of every possible arg type is a
  large transform. RESOLUTION: batch 1 defines the copy over scalars/`str`/`[]T`/flat structs and S2
  HONEST-STOPS the rest (class/closure/ref/nested-pointer graphs) with a message naming the later
  batch. This keeps S3 shippable and never silently mis-copies. Not a law tension — an explicit,
  gated scope line, reported here (not spun into a new issue).
- **R3 — the native/LIR backend.** `lower_stmt` honest-stops `TSpawn`. RESOLUTION: acceptable for
  batch 1 (C backend is spawn's shipping path; corpus never spawns so the compiler's own native build
  is unaffected). A native lowering is a follow-on, not a regression.
- **R4 — detached-thread nondeterminism in the fixture.** A fire-and-forget spawn gives a test no
  finish edge. RESOLUTION: the paranoid selftest uses the JOINABLE twin (designed in C0a.3) to be
  deterministic; the `spawn_smoke` regression polls for observable side effects (per-tag files) with a
  bounded wait. No law tension.

**No unresolved tension — nothing HALTs in this batch.** (D2/`await` is the only genuine owner-fork and
is OUT of scope here; it blocks A4, not any of C0a/S1/S2/S3.)

---

## Final anchors (file:line on HEAD `be73b604`)

- Runtime: `teko_rt.c:2496` tk_task_begin, `:2507` tk_task_end, `:1386` `_Thread_local` current-task,
  `:1391` tk_task_current, `:1346` `struct tk_task`, `:2753` tk_test_run (setjmp-bracket precedent),
  `:2798` tk_test_capture_probe (extern-probe precedent), `:2457` tk_names_live_count, `:4750`
  TEKO_MEM_PARANOID. Header: `teko_rt.h:213-216` task decls, `:466` tk_test_run, `:492` probe selectors.
- Parser: `parse_stmt.tks:59` parse_statement, `:183` is_adopt_head, `:200` parse_adopt (contextual
  model); `ast.tks:199` Call, `:409` ExprStmt, `:427` AdoptStmt, `:541` `@Statement()` union, `:588`
  Param (no `is_ref` — ref lives in the type).
- Checker: `typer.tks:7365` type_statement, `:7425` type_adopt; `tast.tks:67` TCall, `:120` Func,
  `:198` TExprStmt, `:213` TAdoptStmt, `:226` `@TStatement()`; `scope.tks:432` lookup_call;
  `type.tks:154` Reference (the `ref T` resolved form the guard scans).
- Codegen/LIR: `codegen.tks:10233` emit_stmt_dispatch, `:10280`/`:10351` emit_adopt (region-bracket
  model), `:4867` emit_call, `:574` cb_fn_name (user-symbol mangling), `:6290` lifted-fn channel;
  `lower.tks:6297` lower_stmt (native honest-stop site).
- Build: `project.tks:1170` link argv, `:1118` `-lm` note, `:2126` teko_rt.c object compile (`-pthread`
  sites).
</content>
</invoke>
