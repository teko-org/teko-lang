# Arena/`tk_task` terminal — the ZERO-LIBC ("sem-C") layer (RM-C9)

Status: DESIGN (architect). Read-and-design ONLY. Base: `fix/retirement` tip `6b21978f`.
Scope: the LAST residual-C of the arena/`tk_task` path. The arena MECHANISM is already Teko
(`src/runtime/arena.tks`, over raw `mmap`/`munmap`); this doc designs only the gap that still
reaches into C, plus the per-namespace AST drain that makes the peak fall.

---

## §0 — VERDICT: what is already sem-C vs the gap (verified against `src/`, not presumed)

**ALREADY sem-C (LANDED, verified):** the ENTIRE arena mechanism in `src/runtime/arena.tks`
(1078 lines): bump allocation, chunk mmap/munmap, region new/drop/subtree, DI register/lookup,
checkpoint push/pop/commit, free-bins + large-scan, META-POOL header free-list, wrap-refcount
table, capture/panic delivery, `ret_dest`, `environ`/`fd_stage`/`intern`/`names` control slots —
all over `teko::sys::syscall*` + `teko::mem::load_u64`/`store_u64` + `word_ptr`/`ptr_word`. The
L0/L1/L2 switch-over is DONE: `src/codegen/codegen.tks:109-142` emits `TK_ARENA_*` macros that route
to `teko_teko__runtime__*` (the Teko arena fns), so **the Teko arena IS the live compiler runtime**.
The C `tk_region_alloc`/`tk_region_new`/`tk_arena_push`/… are dead-in-emit (a lone doc-comment mention
at `codegen.tks:4981`).

**THE GAP (residual C the arena/`tk_task` path still binds):**

1. `tk_arena_control_get` / `tk_arena_control_set` — the ONE mutable per-flow-of-control word
   (`_Thread_local uint64_t tk_g_arena_control`, `teko_rt.c:2019-2032`). This is the P2 seam
   `arena.tks:1-3` binds; every task-local slot (regs, root, free-bins, marks, intern, names,
   capture, wrap-table, cur-stack) hangs off the CONTROL block reached through this ONE word.
2. `tk_arena_paranoid` — cached `getenv("TEKO_MEM_PARANOID")` (libc `getenv`), `teko_rt.c:2038-2045`;
   bound at `arena.tks:5`, read on the free path (`ar_free_block`).
3. The u64-handle twin externs `project.tks:1639-1649` — `tk_region_root_u`/`_new_u`/`_drop_u`/
   `_enter_u`/`_leave`/`_current_u`, the native fused-emit path's per-unit arena calls, still
   `from "teko_rt"` (`fold_lfunc_scoped_x86` and siblings at `:1658/1911/1930/2043`).
4. The `tk_task` seam (`_Thread_local tk_g_current_task`, `struct tk_task`, `tk_task_begin/end/
   reset/current`, `teko_rt.c:1048/1088/1093/2236/2247/2250`). NOTE — this is NO LONGER on the
   arena's own state path: all arena state moved into the mmap'd CONTROL block. `tk_task_current`
   now backs OTHER, non-arena legs (cov/test/chan/fd — the `#define tk_g_*` block `:1101-1154`).
   `tk_task_begin/end` are called ONLY by user-program spawn (`tk_thread_start`), never by the
   single-threaded self-build (`teko_rt.c:2270-2271`). Its FULL removal folds into the cov/test/
   chan/fd + concurrency legs, NOT RM-C9. RM-C9 removes only the ARENA-path C.

**THE RECLAIM (peak DROP):** per D118-ctx (`DECISION_LOG.md:1171`) the migrations float the
self-compile peak up because moving runtime C into Teko makes `teko` compile it, and the typed AST
stays all-resident "sem drenar por-unidade até a arena RM-C9". The native emit path ALREADY drains
per-unit (`project.tks:1658/1911/1930/2043`, via the C u64-twins of gap #3); the C-leg driver does
NOT. Teaching the C-leg driver to drain each namespace's AST after emitting it is the crumb where the
peak FALLS (D68 strict drop).

---

## §1 — The one irreducible seam and how to hold it sem-C

Everything reduces to holding ONE mutable word (the CONTROL-block address) in Teko without the C
`_Thread_local uint64_t`. Teko has NO mutable module/process word surface — the parser REJECTS
`global` on a variable (`parse_decl.tks:1388`); the `global` keyword today only marks a namespace-
level fn/type/const (cross-namespace visibility) and lowers to RODATA (`lir_print.tks:281`), which is
read-only. This is the same missing surface FORK-ABERTO-1 (`DECISION_LOG.md:662-666`, still "pending
owner A vs B") named for the wrap-table.

**LAW-FIRST RESOLUTION (most-recent wins).** FORK-ABERTO-1 offered (A) a companion C seam word, (B)
teach a Teko `global` mutable-word surface, (C) MAP_FIXED (rejected, fragile). For the TERMINAL,
option A is UNLAWFUL: D119 ("converter TUDO, nada vivo em C") and D125 ("sem novo teko_rt, remover
morto"), both 2026-08-26, SUPERSEDE the 2026-08-20 deferral in `plano-s16-arena-mmap.md:110-135`
(which kept the C seam "not on the critical path"). RM-C9 IS that critical path. Only B remains; §16
"se existe em C, existe em Teko" makes B mandatory (a `_Thread_local` word exists in C → its faithful
Teko form is a thread-local `.bss` word). RECOMMEND the owner formally close FORK-ABERTO-1 as **B**;
this also unblocks crumb 0123's process-global anchor and the F2-program-region gap
(`DECISION_LOG.md:664`).

**The two storage classes.** A faithful transcription needs the SAME storage class the C uses:
- `tk_arena_control` is `_Thread_local` → the Teko form is **thread-local** (per flow of control),
  preserving user-program spawn isolation (D1, arena spec `:300`). RM-C9 needs THIS.
- the wrap-table cross-thread + F2 need a **process-global** (shared) word. That is FASE-2 (cross-
  thread `wrapped` is D52-gated concurrency, D61) — deferred, folds the wrap-table anchor there.

So RM-C9 teaches the thread-local form NOW (crumb 0141), reusing the existing `#`-attribute
machinery (`#os`/`#arch`/`#test`): `#thread_local global var arena_control: u64 = 0`. The C leg
emits `_Thread_local <cty> <sym> = <init>;` — a C storage qualifier, NOT libc, NOT `from "teko_rt"`,
links freestanding — so semantics are byte-faithful to today. The process-global form (plain
`global var`, C leg `static <cty>`) is taught in the SAME surface but its heavy USE is FASE-2. Native
TLS (`.tbss`/`%fs`) is write-only in `lower.tks`, a D52 honest-stop today.

## §1.1 — ENSINO now, USE later (owner)

Teach the FULL `global var` surface in crumb 0141 (lexer already tokenizes `global`; parser/checker/
codegen ACCEPT it) even though native lowering is a D52 write-only honest-stop. Do NOT defer the
teaching. The USE is only the two arena words (0142/0143); the process-global heavy USE (wrap-table
cross-thread) is FASE-2.

---

## §2 — Crumb ladder (0141–0146), each independently gate-able

- **0141 · RM-C9a — `global var` mutable-word SURFACE (teaching, RESEED).** Flip the parser
  rejection to accept `global var name: T = <const>` at namespace level + `#thread_local` attribute.
  AST node, checker (const-init only, namespace-level only), codegen C-leg (`static`/`_Thread_local
  static`), `lower.tks` native `.bss`/`.tbss` write-only (D52 honest-stop). KEYSTONE.
- **0142 · RM-C9b — transcribe the CONTROL word (RESEED, faithful).** Replace `tk_arena_control_get/
  set` externs (`arena.tks:1-3`) with `#thread_local global var ar_control_word: u64 = 0` + inline
  get/set. Removes 2 externs. Byte-faithful to `_Thread_local`.
- **0143 · RM-C9c — transcribe `tk_arena_paranoid` (RESEED).** One-time lazy raw envp scan (over the
  captured `CTRL_ENVIRON` pointer, no `str` alloc — the arena-core dialect forbids it) for
  `TEKO_MEM_PARANOID`, cached in a `global var`/CTRL slot. Removes the 3rd extern.
- **0144 · RM-C9d — repoint the u64-handle twins (RESEED).** Add u64-twin wrappers to `arena.tks`
  (`region_root_u`/`region_new_u`/`region_drop_u`/`region_enter_u`/`region_current_u`) over the ptr
  fns via `word_ptr`/`ptr_word`; repoint `project.tks:1639-1649` off `from "teko_rt"`. This ZEROES
  `from "teko_rt"` on the arena path (0095 goal).
- **0145 · RM-C9e — per-namespace AST drain on the C-leg driver (RECLAIM, [RITUAL], peak DROP).**
  Wrap each namespace's check→emit in a checkpoint/region dropped after that unit emits, using landed
  `arena_push`/`arena_pop` (or `region_new`/`region_drop`). THE measurement crumb — expect strict
  peak fall (D68).
- **0146 · RM-C9f — dead C-root removal (C7 two-legs, expurgo RESEED).** After the flips + fixpoint,
  the self-compile enumerates dead `tk_arena_control_*`, `tk_arena_paranoid`, `tk_g_arena_control`,
  and the L2-orphaned `tk_region_*`/`tk_arena_push/pop/commit`/`tk_region_*_u` bodies → remove. KEEP
  `tk_task_current`/`struct tk_task`/`tk_panic` (other legs). NO tombstone.

## §3 — Measurement points (expect QUEDA)

- 0141–0144 are peak-NEUTRAL (faithful transcription): the D68 floor for expurgo work is
  NÃO-CRESCER (`DECISION_LOG.md:710`). Any of them CROSSING up is a regression — root-cause, don't
  land. Report `teko: memory: peak <N> MB` of the dry build at each reseed.
- **0145 is the peak-DROP** (RITUAL): the per-namespace AST drain recovers the migration float-up
  (D118-ctx). Measure dry-build `peak` before/after; expect strict fall. This is where the "também
  reclama memória" of RM-C9 is realized.
- 0146 does NOT move the peak (the removed C was never in the peak — it was clang-compiled, D118-ctx);
  it only shrinks `teko_rt.c` / the emitted `teko.c` symbol table.

## §4 — Risks + law tensions

- **FORK-ABERTO-1 (mutable-word surface, A vs B) is formally still pending owner.** Resolved
  law-first toward B by D119/D125 (more-recent). SHORT-flagged for the owner to close, because B (a)
  reverses the `plano-s16-arena-mmap.md:110-135` deferral, (b) is a lexer→backend surface addition,
  (c) settles the wrap-table anchor + F2 gap. NOT a hard HALT — the law path is unambiguous.
- **Thread-local faithfulness.** Emitting `_Thread_local` on the C leg preserves D1 spawn isolation
  exactly; a plain `static` would REGRESS user-program multi-thread arenas. The `#thread_local`
  attribute makes the class explicit; process-global is the separate (FASE-2) class.
- **Paranoid on the arena-core dialect.** The probe must stay allocation-free (§1.2 of
  `plano-s16-arena-mmap.md`): a raw envp byte-scan, cached, NEVER `teko::env::var` (which returns
  `str | error` and allocates). Probe once at first `ar_control` touch.
- **u64-twins are native-path (D52).** They compile into the self-image regardless (declared in
  `project.tks`), so repointing them is required to zero `from "teko_rt"` even though the native leg
  does not RUN yet.
- **Fixpoint non-convergence.** The control-word flip changes how the compiler holds its own runtime
  root; the 3-gen fixpoint (gen2==gen3) is the detector. HALT + fix, never reseed a non-converged
  arena (arena-em-teko §6 risk).

No genuine UNRESOLVED tension beyond the FORK-ABERTO-1 close (law-first B). The design is fully
drafted and buildable in the ladder order; nothing is BLOCKED except the owner's formal ratification
of B, which the terminal law already selects.
