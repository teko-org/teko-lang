# teko-lang 0.3.1 — ORDERED CRUMB MANIFEST (000-INDEX)

> **What this is.** The single index that fixes the TOTAL execution order and the exact filename of
> every per-crumb doc for the 0.3.1 implementation wave. Source of truth:
> `docs/design/plano-mestre-0.3.1-implementacao.md` (the master plan — milestone spine M0→M5, the
> merged crumb inventory §2, and the §5 at-a-glance / §3 reseed budget). Every crumb in the master
> plan appears here as exactly ONE row, in a total order that respects (1) the milestone spine
> (M0 < M1 < M2 < M3 < M4 < M5) and (2) every intra/cross dependency (a crumb follows all its deps).
>
> **Total crumbs: 112.**
>
> **Seq ranges per milestone:**
> - **M0 — PIN:** `0001` (1 crumb)
> - **M1 — TEACHING CLUSTER:** `0002`–`0030` (29 crumbs; converges at SM-R1 = `0030`)
> - **M2 — PURE-TEKO LIBRARY + MIGRATIONS:** `0031`–`0090` (60 crumbs)
> - **M3 — CONSOLIDATED EXPURGO:** `0091`–`0096` (6 crumbs)
> - **M4 — TERMINAL NATIVE `.o`:** `0097`–`0107` (11 crumbs)
> - **M5 — ARENA TUNING (Doc-1):** `0108`–`0112` (5 crumbs)
>
> **Reseed budget (from §3.3, unchanged by this index).** The forward teaching+expurgo budget is
> **5 reseeds**: **1 teaching** (`0030` SM-R1 — the whole M1 surface, the only forward teaching
> reseed) **+ ≤4 expurgo** (E1 `{SM-S4, SM-S5}` · E2 `{COL-F2}` · E3 `{9D-EXP}` · E4 `{RM-C9,
> S16-SWEEP}`). Everything else is byte-identity `fixpoint-rebuild` (core-consumes a swap; teaches
> nothing) or `none` (`[dry]` leaf). The 10 banked TC-0 teaching reseeds (§2) are historical, already
> in the seed on `origin/fix/retirement`, and NOT part of the forward budget.
>
> **Per-crumb docs are self-contained.** Each per-crumb doc (written next, filename in the table below)
> stands alone: it carries its own goal, the WHERE (files/subsystem it touches), the HOW (approach +
> Teko type/fn shapes in full-Javadoc), the rulings/laws it obeys, the regression fixtures
> (inputs → expected native exit codes), the gate it must pass (`[dry]`/`[fixpoint]`/`[RITUAL]`), and
> its deps as crumb-ids. This index only fixes ORDER and NAMES; it does not restate crumb detail.

## Ordering method + tie-break

The master plan §2 already lists crumbs in a valid topological order within and across milestones;
this manifest preserves that listing order, which was verified against every declared dependency edge
(all edges point strictly backward in this sequence). Where the master plan leaves two crumbs
unordered relative to each other (e.g. the mutually-independent M1 additive-grammar crumbs, the M2
`[dry]` collection bases, the design-ahead NAT legs), the **stable tie-break** applied is:
**(1) milestone, then (2) subsystem group as grouped in master-plan §2** (memory byte-movers → additive
grammar → fixed-arrays/memory-model; then in M2: source-sweeps → memory-reduction Eixo A →
io-streaming → §16/runtime → RM Eixo C → collections), **then (3) crumb-id ascending**. `filename`
sort therefore equals execution order.

**Ordering flags:** none. No dependency cycle and no unorderable pair was found. `GATE-2` (dep of
`0089` COL-QQuery) and `S17 (banked)` (dep of `0104` NAT-XL) are already-resolved owner gates / banked
teaching, not crumbs, so they impose no ordering constraint here. The RESEED-SERIAL dispatch rule
(§3.4 — one reseed/fixpoint at a time) is an execution-time constraint the coordinator enforces; this
index gives the serial spine it walks.

## The manifest (execution order)

| seq | filename | crumb-id | milestone | goal (one line) | gate | reseed-class | deps |
|---|---|---|---|---|---|---|---|
| 0001 | 0001-SM-P1-pin-rootmap-dps.md | SM-P1 | M0 | pin `type_match`+`frame_sweep_inst`+`push_inst_block` to the return/tail-merge vs self-append facet (DPS bet) | [dry] | none | — |
| 0002 | 0002-SM-A1-instrument-return-box.md | SM-A1 | M1 | instrument return-box volume | [dry] | none | SM-P1 |
| 0003 | 0003-SM-A2-dps-abi-lower-return.md | SM-A2 | M1 | DPS ABI + `lower_return_into_dest` + `alloc_call_dest` (dest-passed only; `ret_dest=null`=today) | [RITUAL]* | (folds R1) | SM-A1 |
| 0004 | 0004-SM-A3-retire-own-returned.md | SM-A3 | M1 | retire `own_returned_value` on DPS path; keep `frame_escape_guard` net | [RITUAL]* | (folds R1) | SM-A2 |
| 0005 | 0005-SM-A4-arena-elision-guard.md | SM-A4 | M1 | arena elision (`scope_touches_arena` guard) | [RITUAL]* | (folds R1) | SM-A1 |
| 0006 | 0006-SM-A5-push-inst-block-fix.md | SM-A5 | M1 | `push_inst_block` self-append point-fix (AL3 `grow_inplace` boundary; NOT DPS) | [RITUAL]* | (folds R1) | SM-P1 |
| 0007 | 0007-SM-G1-colon-return-operator.md | SM-G1 | M1 | additive `:` return operator (accept `Arrow` OR `Colon`) | [dry] | (folds R1) | — |
| 0008 | 0008-SM-G2-var-let-mut-merge.md | SM-G2 | M1 | merge `Let`/`Mut`→`var` (accept `var`/`let`/`mut`); re-base CF3 on flow-single-assign | [dry] | (folds R1) | — |
| 0009 | 0009-SM-G3-static-self-base.md | SM-G3 | M1 | `static` kw + synthetic `self` receiver + `base` rename (accept both loose+synthetic) | [dry] | (folds R1) | — |
| 0010 | 0010-SM-G4-remove-ref-return-arm.md | SM-G4 | M1 | remove `-> ref T` return arm + gate cluster (rides G1) | [dry] | (folds R1) | SM-G1 |
| 0011 | 0011-SM-G5-marshall-opaque-ptr.md | SM-G5 | M1 | Marshall opaque `ptr`/`uptr` + `__wrap`/`__unwrap` + tag runtime (inert) | [dry] | (folds R1) | — |
| 0012 | 0012-SM-G6-di-service-taint.md | SM-G6 | M1 | DI `service`/`svc` escape-taint + string-key (Part A banked; finish taint) | [dry] | (folds R1) | — |
| 0013 | 0013-SM-G7-mem-region-safe-intrinsics.md | SM-G7 | M1 | reclassify `teko::mem`+region primitives as SAFE intrinsics (`__wrap` supplies check) | [dry] | (folds R1) | SM-G5 |
| 0014 | 0014-SM-G8-retire-manual-memory.md | SM-G8 | M1 | retire manual memory (`mem::free`/`#must_free`/`Arena`/`RawBuf`/`Owned<T>`); migrate call-sites | [dry] | (folds R1) | SM-G7 |
| 0015 | 0015-SM-G9-size-usize-primkind.md | SM-G9 | M1 | add `size`/`usize` to `PrimKind` + prim predicates + prim→machine-type table (inert) | [dry] | (folds R1) | — |
| 0016 | 0016-SM-G10-method-overloading.md | SM-G10 | M1 | method overloading (relax same-name reject to param-signature distinctness) | [dry] | (folds R1) | — |
| 0017 | 0017-SM-G11-operator-overloading.md | SM-G11 | M1 | operator overloading (dunder map + derived `!=`/`>`/`<=`/`>=`) | [dry] | (folds R1) | — |
| 0018 | 0018-SM-G12-constraint-acceptance.md | SM-G12 | M1 | generic-constraint acceptance: a constraint accepts ANY type (named, `[]A`, `error`, disjunctions like `[]A \| A \| error`) + markers (`class`/`struct`/`service`/`notnull`) + interfaces + service lifetimes; rejects ONLY traits (closes the §9.2b gap; broadens the constraint-term grammar to a full type) | [dry] | (folds R1) | — |
| 0019 | 0019-9D-T1-inline-union-accept.md | 9D-T1 | M1 | inline-union: accept `A \| B` structural union in field position (additive; `variant` still lives) | [dry] | (folds R1) | — |
| 0020 | 0020-RM-C2-mem-copy-index-join.md | RM-C2 | M1 | `mem::copy(dst,at,src)` index-join primitive + the "count→`[total]byte=[]`→copy" idiom | [dry] | (folds R1) | — |
| 0021 | 0021-COL-F0a-of-len-fixed-backing.md | COL-F0a | M1 | `of_len` + `place`/`read`/`write`+`bucket` fixed-backing intrinsics | [dry] | (folds R1) | RM-C2 |
| 0022 | 0022-COL-F0b-class-holder-escape.md | COL-F0b | M1 | class-holder escape (region-drop-via-escape, conservative GATE-1 default) | [dry] | (folds R1) | COL-F0a |
| 0023 | 0023-COL-F0c-wrapped-retain-release.md | COL-F0c | M1 | wrapped retain/release (refcount-wrap: root-arena `addr→count` dict) | [dry] | (folds R1) | COL-F0a |
| 0024 | 0024-COL-F0d-weak-deepcopy-cas.md | COL-F0d | M1 | weak-ref hook + `deep_copy` + chunk-node capability + CAS helper | [dry] | (folds R1) | COL-F0a |
| 0025 | 0025-SM-STRU32-string-u32-codecs.md | SM-STRU32 | M1 | string as `[]u32` representation + codecs (utf-8↔u32 encode/decode) surface | [dry] | (folds R1) | RM-C2 |
| 0026 | 0026-IO-2-byte-ptr-builtin.md | IO-2 | M1 | `teko::mem::byte_ptr` builtin + lower (`lower_addr_of_place`) + codegen | [dry] | (folds R1) | SM-P1 |
| 0027 | 0027-S16-MM-wp-word-ptr-intrinsic.md | S16-MM-wp | M1 | `word_ptr` intrinsic (inverse of `ptr_word`) + C-leg load/store gap fix | [dry] | (folds R1) | — |
| 0028 | 0028-S16-MM-const-mmap-syscall-consts.md | S16-MM-const | M1 | mmap syscall numbers/flags in `teko::sys` (`#arch`-guarded; §17 banked) | [dry] | (folds R1) | — |
| 0029 | 0029-S16-SYNC-const-sync-abi-consts.md | S16-SYNC-const | M1 | cross-platform sync/arena/thread ABI consts (`#os`-guarded) in `teko::sys` | [dry] | (folds R1) | — |
| 0030 | 0030-SM-R1-teaching-reseed.md | SM-R1 | M1 | THE ONE forward teaching reseed — capture a seed that DPS-lowers + parses/knows ALL of M1 | [RITUAL] | teaching (1, the only forward one) | ALL M1 rows |
| 0031 | 0031-SM-S1-sweep-colon-returns.md | SM-S1 | M2 | sweep `src/`+`.tkt` to `:` returns | [fixpoint] | fixpoint-rebuild | SM-R1 |
| 0032 | 0032-SM-S2-sweep-var.md | SM-S2 | M2 | sweep to `var`; drop `let`/`mut` acceptance | [fixpoint] | fixpoint-rebuild | SM-R1 |
| 0033 | 0033-SM-S3-sweep-self-base-static.md | SM-S3 | M2 | sweep methods to `self`/`base`/`static`; remove loose-receiver parse | [fixpoint] | fixpoint-rebuild | SM-R1 |
| 0034 | 0034-SM-S6-reball-usize-size.md | SM-S6 | M2 | reball memory/collection positions `u64`→`usize`/`size` (source pos stays `u32`) | [fixpoint] | fixpoint-rebuild | SM-R1,SM-G9 |
| 0035 | 0035-SM-S7-adopt-overloading.md | SM-S7 | M2 | (optional) adopt overloading in `src/` (empty if `src/` adopts nothing) | [fixpoint] | none/fixpoint | SM-R1 |
| 0036 | 0036-RM-C1-measure-emit-buffer.md | RM-C1 | M2 | measure/shadow the `cb`/`append_fo` emit buffer (baseline; no src change) | [dry] | none | — |
| 0037 | 0037-RM-C3-codegen-spread-literal.md | RM-C3 | M2 | convert codegen emit buffer to spread-literal `b"…"`+`..str` index-materialize (the 93%) | [fixpoint] | fixpoint-rebuild | RM-C2,SM-R1 |
| 0038 | 0038-RM-C4-checker-build-push.md | RM-C4 | M2 | convert checker+build push (MAP/PARSE/FILTER/BUFFER; redesign `Env` ownership) | [fixpoint] | fixpoint-rebuild | RM-C3 |
| 0039 | 0039-RM-C5-lir-backend-residual.md | RM-C5 | M2 | convert lir+backend+parser+codegen residual (LEnv/LowerCtx parallel arrays) | [fixpoint] | fixpoint-rebuild | RM-C4 |
| 0040 | 0040-RM-C6-arena-per-scope.md | RM-C6 | M2 | arena-per-scope at boundaries (`region_enter/leave/drop_subtree` + `arena_push/pop/commit`) | [fixpoint] | fixpoint-rebuild | RM-C5 |
| 0041 | 0041-RM-C7-array-lit-arena.md | RM-C7 | M2 | array literal via arena (`emit_array_lit` non-spread → `region_alloc`, kill raw `malloc`) | [fixpoint] | fixpoint-rebuild | RM-C6 |
| 0042 | 0042-RM-C8-targeted-free-purge.md | RM-C8 | M2 | targeted free + purge-on-reassign (`region_free`; `assign_frees_old`; `CgArenaSym::RegionFree`) | [fixpoint] | fixpoint-rebuild | RM-C7 |
| 0043 | 0043-IO-1-syscall-constants.md | IO-1 | M2 | syscall constants (`SYS_READ/…`,`O_*`,`SEEK_*`) in `sys.tks` | [dry] | none | — |
| 0044 | 0044-IO-3-file-stream.md | IO-3 | M2 | `file_stream.tks` (`os_*` wrappers + `FileStream` + open/read/write/seek/close) | [dry] | none | IO-1,IO-2 |
| 0045 | 0045-IO-4-stat-fileinfo.md | IO-4 | M2 | `stat`/`FileInfo`/`file_size` | [dry] | none | IO-1 |
| 0046 | 0046-IO-5-stream-helpers.md | IO-5 | M2 | helpers `write_stream`/`append_stream`/`read_stream` (open+defer-close+1024 loop) | [dry] | none | IO-3 |
| 0047 | 0047-IO-6-total-forms-over-stream.md | IO-6 | M2 | rewrite TOTAL forms (`read_file`/`write_file`/…) over stream (kill `teko_rt` FFI edge) | [fixpoint] | fixpoint-rebuild | IO-5 |
| 0048 | 0048-IO-7-migrate-compiler-reads.md | IO-7 | M2 | migrate compiler reads to `read_stream` (`assemble`/`fmt`/`project`/`regression`) | [fixpoint] | fixpoint-rebuild | IO-6 |
| 0049 | 0049-IO-8-migrate-compiler-writes.md | IO-8 | M2 | migrate compiler writes to `write_stream`/`append_stream` (`project`/`fmt`/`regr_group`/`init`) | [fixpoint] | fixpoint-rebuild | IO-7 |
| 0050 | 0050-S16-EMIT-emit-intrinsics.md | S16-EMIT | M2 | FASE1 emit intrinsics: floor/round/ceil + memcpy + own typedefs (kill `<math.h>/<string.h>/<stdint.h>` of emitted C) | [fixpoint] | fixpoint-rebuild | SM-R1 |
| 0051 | 0051-S16-IO-sys-exit-write-ftoa.md | S16-IO | M2 | FASE2 `SYS_exit_group`+`SYS_write` + float-bottom ftoa/`%.17g` pure-Teko | [fixpoint] | fixpoint-rebuild | S16-EMIT |
| 0052 | 0052-S16-MM-pool-arena-mmap-meta-pool.md | S16-MM-pool | M2 | arena-mmap meta-pool (mmap-specific memory-correctness keystone) | [dry] | none | S16-MM-wp,S16-MM-const |
| 0053 | 0053-S16-MM-L1-arena-switchover-l1.md | S16-MM-L1 | M2 | arena switch-over L1: Teko-over-mmap arena behind unchanged call-sites (shim indirection) | [fixpoint] | fixpoint-rebuild | S16-MM-pool |
| 0054 | 0054-S16-MM-L2-arena-switchover-l2.md | S16-MM-L2 | M2 | arena switch-over L2: compiler's own runtime IS the Teko-over-mmap arena (load-bearing) | [fixpoint] | fixpoint-rebuild | S16-MM-L1 |
| 0055 | 0055-S16-FS-fs-env-time-random.md | S16-FS | M2 | FASE4 fs+env+time+random (`open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/`getenv`/`getrandom`/`getrusage`) | [fixpoint] | fixpoint-rebuild | S16-MM-L2 |
| 0056 | 0056-S16-SYNC-sync-ffi-split.md | S16-SYNC | M2 | sync FFI+split (futex/ulock/WaitOnAddress) unblocking cross-platform seed | [fixpoint] | fixpoint-rebuild | S16-SYNC-const |
| 0057 | 0057-S16-PANIC-assert-to-teko.md | S16-PANIC | M2 | FASE5 `assert.c`→Teko (compare+panic, L0-shaped) | [fixpoint] | fixpoint-rebuild | S16-IO |
| 0058 | 0058-RT-L0-runtime-io-panic-fmt.md | RT-L0 | M2 | runtime C→Teko L0: io/panic/fmt/str-query/guards (pure over `[]byte`) | [fixpoint] | fixpoint-rebuild | S16-IO |
| 0059 | 0059-RT-L1-runtime-alloc-arena.md | RT-L1 | M2 | L1: alloc+arena/regions+slice/box (the two compiler seams; mmap via extern) | [fixpoint] | fixpoint-rebuild | S16-MM-L2 |
| 0060 | 0060-RT-L2-runtime-utf8-str.md | RT-L2 | M2 | L2: UTF-8 char + rotatable str-construction (pure over L1) | [fixpoint] | fixpoint-rebuild | RT-L1 |
| 0061 | 0061-RT-L3-runtime-ffi-fs-env.md | RT-L3 | M2 | L3: FFI host fs/env/time/date (POSIX/Win32 leaf syscalls) | [fixpoint] | fixpoint-rebuild | S16-FS |
| 0062 | 0062-RT-L4-runtime-process-pipes.md | RT-L4 | M2 | L4: process/pipes/redirect (fork/exec/CreateProcess; struct-by-value FFI) — GATE §16-FASE6 | [fixpoint] | fixpoint-rebuild | RT-L3 |
| 0063 | 0063-RT-L5-runtime-interning-task.md | RT-L5 | M2 | L5: interning/task/names/coverage (process state over L1; cross-thread names) | [fixpoint] | fixpoint-rebuild | RT-L1,S16-SYNC |
| 0064 | 0064-RT-L6-runtime-test-harness.md | RT-L6 | M2 | L6: test harness + assert + backtrace (setjmp/longjmp irreducible-partial + signal) — owner gate | [fixpoint] | fixpoint-rebuild | RT-L5,S16-PANIC |
| 0065 | 0065-RM-C10-determinize-gensym.md | RM-C10 | M2 | determinize gensym (drop `buf.len`-derived temp names → global counter) — pre-req of Eixo C | [fixpoint] | fixpoint-rebuild | RM-C5 |
| 0066 | 0066-RM-C11-parse-per-unit-link.md | RM-C11 | M2 | parse-per-unit → incomplete AST → LINK (the rich internal-FFI table feeding each unit's checker) | [fixpoint] | fixpoint-rebuild | RM-C10 |
| 0067 | 0067-RM-C12-fused-check-lower-emit.md | RM-C12 | M2 | fused check+lower+emit per unit (abstract unit-output; drop unit region) — `teko.c` byte-identical | [fixpoint] | fixpoint-rebuild | RM-C11,RM-C6 |
| 0068 | 0068-RM-C13-tkb-per-unit-dump.md | RM-C13 | M2 | typed `.tkb` per-unit disk dump (serialize/deserialize one namespace; deterministic frame) | [fixpoint] | fixpoint-rebuild | RM-C12 |
| 0069 | 0069-RM-C14-incremental-build.md | RM-C14 | M2 | incremental build (per-unit typed cache; OFF on self-build/fixpoint) | [dry] | none | RM-C12,RM-C13 |
| 0070 | 0070-COL-Q1-chunkchain-base.md | COL-Q1 | M2 | ChunkChain base (growable + TS substrate) | [dry] | none | COL-F0a |
| 0071 | 0071-COL-Q3-ring-base.md | COL-Q3 | M2 | Ring base | [dry] | none | COL-F0a |
| 0072 | 0072-COL-Q4-hash-base.md | COL-Q4 | M2 | Hash base | [dry] | none | COL-Q1 |
| 0073 | 0073-COL-Q5-ordered-base.md | COL-Q5 | M2 | Ordered base (+Node) | [dry] | none | COL-F0a |
| 0074 | 0074-COL-Q6-heap-base.md | COL-Q6 | M2 | Heap base | [dry] | none | COL-Q1 |
| 0075 | 0075-COL-Q7-bitset-base.md | COL-Q7 | M2 | BitSet base | [dry] | none | COL-F0a |
| 0076 | 0076-COL-Q8-weak-wrappers.md | COL-Q8 | M2 | Weak wrappers (`Weak<T>`) | [dry] | none | COL-F0d |
| 0077 | 0077-COL-Q9-list-chunkchain.md | COL-Q9 | M2 | convert `List<T>` → chunk-chain + TS + three-category (build side) | [fixpoint] | fixpoint-rebuild | COL-Q1 |
| 0078 | 0078-COL-Q10-map-dict-hashset.md | COL-Q10 | M2 | convert `Map`/`Dictionary`/`HashSet` → Hash (Map is core-consumed by `teko::env`) | [fixpoint] | fixpoint-rebuild | COL-Q4,COL-Q9 |
| 0079 | 0079-COL-Q11-sorted-set-dict.md | COL-Q11 | M2 | convert `SortedSet`/`SortedDictionary` → Ordered | [dry]/[fixpoint] | none/fixpoint | COL-Q5,COL-Q10 |
| 0080 | 0080-COL-Q12-priorityqueue-heap.md | COL-Q12 | M2 | convert `PriorityQueue` → Heap | [dry]/[fixpoint] | none/fixpoint | COL-Q6,COL-Q11 |
| 0081 | 0081-COL-Q13-stack.md | COL-Q13 | M2 | `Stack<T>` (wraps ChunkChain) | [dry] | none | COL-Q1 |
| 0082 | 0082-COL-Q14-queue-deque.md | COL-Q14 | M2 | `Queue<T>`/`Deque<T>` (wrap Ring) | [dry] | none | COL-Q3 |
| 0083 | 0083-COL-Q15-linkedlist.md | COL-Q15 | M2 | `LinkedList<T>` (doubly-linked; shares Node w/ Q5) | [dry] | none | COL-Q5 |
| 0084 | 0084-COL-Q16-counter-multiset.md | COL-Q16 | M2 | `Counter<T>`/`MultiSet<T>` (wraps Dictionary) | [dry] | none | COL-Q10 |
| 0085 | 0085-COL-Q17-multimap.md | COL-Q17 | M2 | `MultiMap<K,V>` (wraps Dictionary<K,List<V>>) | [dry] | none | COL-Q10,COL-Q9 |
| 0086 | 0086-COL-Q18-weakmap-weakset.md | COL-Q18 | M2 | `WeakMap`/`WeakSet` (Hash<Weak,·> + get-time prune) | [dry] | none | COL-Q4,COL-Q8 |
| 0087 | 0087-COL-Q19-ringbuffer-blocking.md | COL-Q19 | M2 | `RingBuffer`/`BlockingCollection` (bounded Ring + condvar) | [dry] | none | COL-Q3,S16-SYNC |
| 0088 | 0088-COL-Q20-table-core.md | COL-Q20 | M2 | `Table<…>` core (chunk-chain rows + Map/SortedSet indices + atomic multi-index txn; ≤16 cols) | [dry] | none | COL-Q1,COL-Q4,COL-Q5 |
| 0089 | 0089-COL-QQuery-table-query-linq.md | COL-QQuery | M2 | `Table` query surface (LINQ-typed) — GATE-2 resolved LINQ (owner 2026-08-19) | [dry] | none | COL-Q20,GATE-2 |
| 0090 | 0090-COL-QFile-filetable.md | COL-QFile | M2 | `FileTable<Row>` (io-streaming backed; rebuild-indices-on-load) | [dry] | none | COL-Q20,IO-8 |
| 0091 | 0091-SM-S4-drop-arrow-lexer.md | SM-S4 | M3 | drop `->`/`Arrow` from lexer+`token.tks`; migrate `src/` FFI to opaque `ptr` | [RITUAL] | expurgo | SM-S1 (all `:` swept) |
| 0092 | 0092-SM-S5-delete-unsafe-kw.md | SM-S5 | M3 | DELETE `unsafe` kw + `is_unsafe` + contagion (last; nothing left to contain) | [RITUAL] | expurgo | SM-G8,SM-S4 |
| 0093 | 0093-COL-F2-remove-push-legacy.md | COL-F2 | M3 | remove `push`/single-array/`empty`/`with_cap`/`grow_inplace` + dead `arr_*`/`sorted_insert`/`heap_*`; clean lexer+parser+checker | [RITUAL] | expurgo | ALL COL-Q*, RM-C3..C8 |
| 0094 | 0094-9D-EXP-remove-variant-form.md | 9D-EXP | M3 | remove `type X = variant` form (folhas→raízes, `Type` last) + `Variant` carrier | [RITUAL] | expurgo | 9D-T1, all inline-union sites migrated |
| 0095 | 0095-RM-C9-terminal-c-root-removal.md | RM-C9 | M3 | TERMINAL C-root removal + transcribe arena control slot (`.bss`/`MAP_FIXED`) to Teko | [RITUAL] | expurgo | ALL RT-L*, S16-* |
| 0096 | 0096-S16-SWEEP-delete-c-files.md | S16-SWEEP | M3 | FASE9 stop emitting `#include "teko_rt.h"/assert.h/win32_compat"`; DELETE the 4 C files; fix CI/build scripts | [RITUAL] | expurgo | RM-C9, all M2 §16 rows |
| 0097 | 0097-NAT-A1-lir-lowering-frontier.md | NAT-A1 | M4 | close LIR lowering coverage frontier (new `LOp` cases, `LModule` growth, fat ptrs, control-flow) | [fixpoint] | fixpoint-rebuild | RM-C5 |
| 0098 | 0098-NAT-A2-arm64-isel.md | NAT-A2 | M4 | arm64 isel (`minst.tks` + `isel_arm64.tks`) | [dry] | none | NAT-A1 |
| 0099 | 0099-NAT-A3-regalloc-aapcs64.md | NAT-A3 | M4 | linear-scan regalloc + AAPCS64 descriptor (`regalloc.tks`,`abi_aapcs64.tks`) | [dry] | none | NAT-A2 |
| 0100 | 0100-NAT-A4-arm64-encoder-macho.md | NAT-A4 | M4 | arm64 encoder + Mach-O object + link via system `ld` (`encode_arm64.tks`) | [dry] | none | NAT-A3 |
| 0101 | 0101-NAT-B1-x86-64-sysv-elf.md | NAT-B1 | M4 | x86-64 SysV ABI + ELF object (`abi_sysv64`,`minst_x86`,`isel_x86_64`; ELF writer reused) | [dry] | none | NAT-A3 |
| 0102 | 0102-NAT-AARCH-aarch64-elf-reloc.md | NAT-AARCH | M4 | aarch64-ELF relocation map ((MRelocKind,width,addend) key) + writer decls | [dry] | none | NAT-A4,NAT-B1 |
| 0103 | 0103-NAT-B3-windows-coff.md | NAT-B3 | M4 | Windows x86_64 (Win64 ABI + PE/COFF; `shadow_space` descriptor field; COFF writer) | [dry] | none | NAT-B1 |
| 0104 | 0104-NAT-XL-host-detect-crosslink.md | NAT-XL | M4 | host detection (`teko::os()`/`arch()`) + supported-targets table + cross-emit note (TKP ratified) | [dry] | none | S17 (banked) |
| 0105 | 0105-RM-C15-terminal-native-o.md | RM-C15 | M4 | terminal native: per-unit `.o` (lower→isel→regalloc→encode→`emit_elf`/`macho`/`coff`; unit region dropped = the dump); `exp`+`pub`=GLOBAL, private=local | [fixpoint] | fixpoint-rebuild | RM-C12, NAT-A4/B1/B3/AARCH/XL |
| 0106 | 0106-RM-C16-native-object-fixpoint.md | RM-C16 | M4 | migrate fixpoint `gen2.c==gen3.c` → native-object-reproducible; when 4 native CI legs green + object reproduces, REMOVE the C route (`teko.c` + `cc`); reseed becomes native object | [RITUAL] | fixpoint-rebuild (native seed) | RM-C15, S16-SWEEP |
| 0107 | 0107-RM-C17-emit-tkh-package.md | RM-C17 | M4 | emit + package the compiler's own `.tkh` (aggregated `exp` surface); ship binary+`.tkh` (internal-FFI `exp`+`pub` stays transient) | [fixpoint] | fixpoint-rebuild | RM-C16 |
| 0108 | 0108-D1-T1-arena-pre-sizing.md | D1-T1 | M5 | arena pre-sizing / static floor (start at Σ expected slots; union → largest slot wins) | [fixpoint] | fixpoint-rebuild | RM-C16 |
| 0109 | 0109-D1-T2-physical-dps-elision.md | D1-T2 | M5 | physical DPS elision (backend returns direct into caller arena; realize the copy elision) | [fixpoint] | fixpoint-rebuild | SM-A2,RM-C16 |
| 0110 | 0110-D1-T3-arena-elision-proper.md | D1-T3 | M5 | arena elision proper (no region where nothing allocates; forwarders / const returns) | [fixpoint] | fixpoint-rebuild | SM-A4,RM-C16 |
| 0111 | 0111-D1-T4-literal-dedup.md | D1-T4 | M5 | static literal/folded-constant dedup (emit once + reference → smaller binary) | [fixpoint] | fixpoint-rebuild | RM-C16 |
| 0112 | 0112-D1-T5-push-copy-mitigation.md | D1-T5 | M5 | push/copy-grow mitigation via known-size arrays + `with_cap` pre-capacity (levers already exist) | [fixpoint] | fixpoint-rebuild | COL-F2 |
| 0113 | 0113-NAT-N1-union-nullable-native-lowering.md | NAT-N1 | M2 | union / nullable native-lowering family (largest honest-stop cluster); PULLED FORWARD from M4 (D106) — gate = write-only native emit + C `gen2.c==gen3.c` | [fixpoint] | fixpoint-rebuild | NAT-A1 |
| 0114 | 0114-NAT-N2-fatptr-dispatch-match-operator-residual.md | NAT-N2 | M2 | fat-pointer dispatch/closure + match-pattern + operator residual + KNOWN-WRONG; PULLED FORWARD from M4 (D106) | [fixpoint] | fixpoint-rebuild | NAT-N1 |
| 0115 | 0115-S10-SURF-concurrency-surface-teaching.md | S10-SURF | M1 | §10 concurrency SURFACE teaching (spawn / chan / await→Intent accept) | [dry] | (folds R1) | — |
| 0116 | 0116-S10-RT-threads-channels-intent-runtime.md | S10-RT | M2 | §10 runtime: threads + channel transports + Intent + region-per-thread | [fixpoint] | fixpoint-rebuild | S10-SURF, S16-SYNC |
| 0117 | 0117-D1-DI-arena-lifetime-binding.md | D1-DI | M5 | DI Part B: bind service lifetimes to arena regions (`svc_scope_expr` seam) | [fixpoint] | fixpoint-rebuild | S10-RT, RM-C16 |
| 0118 | 0118-STD-ENC-encoding-catalog-tail.md | STD-ENC | M2 | encoding/serialization catalog tail (csv·toml·ini·yaml·protobuf·asn1·fixed) | [dry] | none | — |
| 0119 | 0119-STD-FFI-ffi-dependent-stdlib.md | STD-FFI | M2 | FFI-dependent stdlib (crypto::rand·openssl·gpg·net·db·odbc·rpc) | [dry] | none | RT-L3, S16-SWEEP |
| 0120 | 0120-SM-V1-visibility-enforcement-activation.md | SM-V1 | M2 | activate `exp`/`pub`/private visibility enforcement (Doc-2 §11) | [fixpoint] | fixpoint-rebuild | RM-C17 |
| 0121 | 0121-ERR-FACTORY-error-factory-builtins.md | ERR-FACTORY | M2 | `error::new(msg)` and `error::join(a,b)` factory builtins | [fixpoint] | fixpoint-rebuild | SM-G4 |
| 0122 | 0122-S16-SYSCALL-PORTABLE-host-portability.md | S16-SYSCALL-PORTABLE | M2 | gate syscall C helpers to `__linux__` + portable stub fallback | [fixpoint] | fixpoint-rebuild | RM-C17 |
| 0123 | 0123-COL-F0c2-wrap-table-redesign.md | COL-F0c2 | M1 | wrap-refcount table root redesign (O(1) shared hash + CAS + chunk-chain) | [dry] | (folds R1) | COL-F0c, COL-F0d |
| 0124 | 0124-RT-L4-ENV-envp-capture-zerolibc.md | RT-L4-ENV | M2 | Teko-managed `environ` overlay + `var`/`set_var`/`cwd`/`chdir` (zero C-runtime) | [RITUAL] | fixpoint-rebuild | S16-FS, RT-ENTRY |
| 0125 | 0125-RT-ENTRY-start-abi-per-os.md | RT-ENTRY | M2 | process entry per-OS: argc/argv/envp from the sanctioned OS ABI (linker-resolved) | [RITUAL] | fixpoint-rebuild | S16-FS |
| 0126 | 0126-NAT-B0-graceful-stop-capture.md | NAT-B0 | M2 | capture/exit/cancel as GRACEFUL-STOP (panic-flag + return, defers/drops per frame; ELIMINATES setjmp/longjmp, unblocks macOS) — supersedes D105 C-leg | [fixpoint] | fixpoint-rebuild | RT-L6 |
| 0127 | 0127-NAT-B3-lowering-crash-invariant-sweep.md | NAT-B3 | M2 | class-(b) crash sweep: aggregate/fat sret return per-ABI (x86@933 `[]byte`, arm64@90 struct, win64@1145 slice) + false-`(internal)` variant-member fix + frontier `(internal)` sweep | [fixpoint] | fixpoint-rebuild | NAT-A1 |
| 0128 | 0128-NAT-B4-campaign-emission-native-mirror.md | NAT-B4 | M2 | native mirror of the campaign's own C emissions (clone aarch64 · `_start`/`stack_ptr` per-OS · spawn-mmap glue) | [fixpoint] | fixpoint-rebuild | RT-ENTRY |

## Footnotes

- **`[RITUAL]*` on SM-A2..A5 (`0003`–`0006`).** Per master-plan §2 M1 note: A2–A5 each carry a
  native-ladder ritual in dev, but their *seed* is captured once at `0030` SM-R1 (the reseed is the
  harvest, not per-crumb). Their reseed-class is therefore "(folds R1)" — they do not each mint a
  reseed; SM-R1 is the single teaching reseed for the whole M1 cluster.
- **`(folds R1)`** in the reseed-class column marks every M1 row whose surface is banked by the single
  `0030` SM-R1 teaching reseed. No M1 row mints its own reseed.
- **`[dry]/[fixpoint]` and `none/fixpoint`** (COL-Q11 `0079`, COL-Q12 `0080`, SM-S7 `0035`) are carried
  verbatim from the master plan: the gate/reseed-class resolves to the heavier arm only if `src/`
  actually core-consumes the swap; otherwise it is a `[dry]` leaf with no reseed.
- **Verbatim carry.** crumb-id, milestone, goal, gate, reseed-class and deps are copied from master-plan
  §2. `\|` inside a goal/deps cell is a Markdown-escaped literal `|` (e.g. the union `[]A | A | error`).
