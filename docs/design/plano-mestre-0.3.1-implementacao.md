# teko-lang 0.3.1 — MASTER ORDERED IMPLEMENTATION PLAN (the whole program)

> **Purpose.** This is the ORDERED PROGRAM for the entire 0.3.1 wave — the skeleton the owner
> approves BEFORE we fan out one permanent full doc per crumb. It is the milestone spine + the
> complete merged crumb inventory (one row per crumb, every source doc) + the cross-doc dependency
> graph + the reseed budget + the terminal-native phase. It is NOT the per-crumb detail docs (those
> come next); it IS the order and the dependency graph, complete and correct.
>
> **DESIGN ONLY.** No product `.tks`, no build, no reseed, no `teko test`. This file is the deliverable.
>
> **Non-negotiable final milestone.** A **100% NATIVE binary with NO C dependency** — no `cc`/`gcc`/
> `clang` — emitting a linkable `.o` that the OS linker (`ld`/lld / the platform linker) links into
> the final binary. See Phase M4.
>
> **Structural law (owner ruling 2026-08-19).** The plan is phased **program-wide**, NOT as the
> concatenation of per-subsystem queues. All surface teaching of the whole 0.3.1 is merged into the
> **smallest number of teaching-reseeds**, dependency-ordered, with the FIRST teaching phase made the
> LARGEST possible (its full dependency closure). The general form is: **consolidated teaching
> cluster(s) → pure-Teko library/migrations over the taught surface (0 teaching reseed) →
> consolidated expurgo cluster(s) → terminal native `.o`.** A per-subsystem "FASE 0" (e.g. the
> collections queue's) is ABSORBED into the program-wide teaching cluster.
>
> **Sources merged** (crumb sections read): `lang-evolution-0.3.1-memory-and-surface.md` (umbrella;
> P/A/G/R/S), `mudancas-superficie-0.3.1.md` (Doc-2, the law; §9.D, §11 order, §16, §17, per-type
> memory model 1614-1669), `reducao-memoria-arrays-0.3.1.md` (C1–C17), `io-streaming-0.3.1.md`
> (crumbs 1–9), `colecoes-memoria-fila-implementacao-0.3.1.md` (FASE 0 / Q1–Q22 / FASE 2) +
> `colecoes-e-memoria-modelo-unificado-0.3.1.md` + `colecoes-remodelagem-backing-fixo-0.3.1.md` +
> `table-collection-sql-linq-0.3.1.md`, `plano-s16-*.md` (foundation, syscall-intrinsic, arena-mmap,
> monolith-cc-emit, arch-cross-emit, sync-cross-plataforma, expurgo-libc-completo),
> `plano-s17-pragmas-crumbs.md`, `plano-9d-migracao-variant.md`, `migracao-runtime-c-para-teko-0.3.1.md`,
> the native legs `backend-a1..a4`, `backend-b1-x8664`, `backend-b3-windows-coff`, `aarch64-elf-0.3.1.md`,
> `teko-target-crosslink-0.3.1.md`, and the arena tuning trio `arena-especificacao-unica-0.3.1.md` +
> `arena-em-teko.md` + `arena-escopada-stream-expurgo-0.3.1.md` (Doc-1 = TUNING, last).

---

## 1. The milestone spine (the ordered program, with what-gates-what)

The sequencing law, made explicit and reconciled across the source docs:

- **Doc-2 (surface, ENTIRE) resolves the dependencies of Doc-1 (arena TUNING, LAST).** Doc-2 §16
  delivers the *correct* arena (arena-per-object + escape-holder + wrap-refcount + deep-copy-at-boundary
  + F2 singletons); Doc-1 only *improves* the already-correct arena (pre-sizing, physical DPS elision,
  arena elision, literal dedup, push-mitigation). No circularity (Doc-2:1632-1663).
- **§17 → §16** (`#os`/`#arch`/`#if` select the per-target FFI; owner "17 antes de 16", Doc-2:1215/1252).
- **§16 syscall intrinsic → io-streaming + arena-mmap** (every syscall call-site needs the intrinsic).
- **fixed-arrays (`of_len`+index-assign) → collections** (every collection base is built on the fixed
  backing).
- **The feeders** (memory-reduction RM + io-streaming) and **collections** ride the taught surface.
- **The terminal → the 100% native `.o` (no C toolchain).**

The program is six milestones:

| # | Milestone | What it is | Gate that closes it |
|---|---|---|---|
| **M0** | **PIN** | `type_match`+`frame_sweep_inst`+`push_inst_block` root-map pin (DPS go/no-go) | objdump pin, no ritual |
| **M1** | **TEACHING CLUSTER (the ONE forward teaching reseed)** | the maximal dependency-closed set of ALL 0.3.1 surface teaching, taught together, one reseed | native ladder + `gen2==gen3` (DPS) + C-route self-reproduce → **the single teaching reseed R1** |
| **M2** | **PURE-TEKO LIBRARY + MIGRATIONS** | the whole library + every source sweep/conversion over the taught surface; teaches NOTHING; only fixpoint-rebuilds where the core consumes a swap | per-item `[dry]`/`[fixpoint]` |
| **M3** | **CONSOLIDATED EXPURGO** | clean removal of every de-taught construct (lexer+parser+checker, NO tombstone), minimal expurgo reseeds | per-cluster `[RITUAL]` expurgo reseed |
| **M4** | **TERMINAL NATIVE `.o` (endgame)** | per-unit native object emission; fixpoint migrated to native-object-reproducible; **remove the C route (`teko.c` + `cc`) entirely** | 4 native CI legs green + native-object reproduces |
| **M5** | **ARENA TUNING (Doc-1)** | optimization of the already-correct arena; the physical realization of DPS elision + pre-sizing + literal dedup | native fixpoint (perf, not capability) |

**Why M1 can be so large — the historical baseline TC-0 (already banked in the seed on
`origin/fix/retirement`).** The enablers that would otherwise force *earlier* teaching reseeds are
ALREADY seeded, which is exactly what lets the first *forward* teaching reseed absorb everything else:

| banked teaching (TC-0) | seed/commit | source |
|---|---|---|
| §9 operator dispatch + §9.4 counterpart | delivered | Doc-2:1222 |
| §9.2b shape/constraint solver | `cf0c70b5` | Doc-2:1224 |
| §13 item-14 fat-header | `6f4d78ba` | Doc-2:1225 |
| §7 DI Part A (`service`/`svc`/`ServiceLifetime`) | `0a246dfe` | Doc-2:1226 |
| §14 comptime engine (B2–B5) | `a0586372` &c. | Doc-2:1227 |
| §15 `global` | `21bbe7ae` | Doc-2:1228 |
| **§17 pragmas** (`#os`/`#arch`/`#if`, prune-before-check) | reseed `20d7cb9b` | Doc-2:1607 |
| **§16 syscall intrinsic** (`syscall0..6`,`ptr_word`,`ref_word`) | land `fb0ec8c7`, reseed `1a03a68e` | expurgo:187 |
| **`extern type = struct`** (C1) | `c7ac134b` | expurgo:189 |
| 9-ops keystone; generic-stack | `b778b7d1`,`b300ee6a` | Doc-2:1332-1354 |

Because **§17 + §16-syscall + extern-struct are already banked**, the owner's classic forcing chain
(`§17 gate §16 gate io/arena; fixed-arrays gate collections`) no longer forces a *forward* split —
the enablers are in the seed, so the remaining surface teaching collapses into **one** maximal cluster
(M1). This is the headline of the consolidation (§3).

---

## 2. The complete merged crumb inventory (one ordered stream)

Legend — **gate**: `[dry]` = compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change);
`[fixpoint]` = build gen2 + scoped regression + `gen2==gen3` byte-identity (core-consumes swap,
teaches nothing); `[RITUAL]` = full gate + a genuine reseed. **reseed-class**: `teaching` /
`expurgo` / `fixpoint-rebuild` / `none`. IDs namespaced by source (`SM`=umbrella surface/memory,
`RM`=reducao-memoria, `IO`=io-streaming, `COL`=collections, `S16/S17`=§16/§17, `9D`=inline-union,
`RT`=runtime-C→Teko, `NAT`=native backend, `D1`=Doc-1 arena tuning).

### M0 — PIN (go/no-go, no ritual)

| id | goal | area/files | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| SM-P1 | pin `type_match`+`frame_sweep_inst`+`push_inst_block` to the return/tail-merge vs self-append facet (DPS bet) | root-map + objdump | umbrella §10 P1 | [dry] | none | — |

### M1 — TEACHING CLUSTER (the ONE forward teaching reseed; all rows land, ONE reseed at the end)

All rows are **additive / feature-gated-inert / self-lowering**, authored in OLD spelling+types so
today's seed parses them; none uses another row's not-yet-seeded surface. The reseed **SM-R1** at the
bottom captures a single seed that DPS-lowers AND parses/knows every added construct.

*Memory byte-movers (must be in the seed; land before SM-R1):*

| id | goal | area/files | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| SM-A1 | instrument return-box volume | assessment D0 | umbrella A1 | [dry] | none | SM-P1 |
| SM-A2 | DPS ABI + `lower_return_into_dest` + `alloc_call_dest` (dest-passed only; `ret_dest=null`=today) | lir/backend | umbrella A2 | [RITUAL]* | (folds R1) | SM-A1 |
| SM-A3 | retire `own_returned_value` on DPS path; keep `frame_escape_guard` net | checker/lir | umbrella A3 | [RITUAL]* | (folds R1) | SM-A2 |
| SM-A4 | arena elision (`scope_touches_arena` guard) | lir | umbrella A4 | [RITUAL]* | (folds R1) | SM-A1 |
| SM-A5 | `push_inst_block` self-append point-fix (AL3 `grow_inplace` boundary; NOT DPS) | lir | umbrella A5 | [RITUAL]* | (folds R1) | SM-P1 |

*Additive surface grammar (old spelling; inert-until-adopted):*

| id | goal | area/files | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| SM-G1 | additive `:` return operator (accept `Arrow` OR `Colon`) | parser | umbrella G1 | [dry] | (folds R1) | — |
| SM-G2 | merge `Let`/`Mut`→`var` (accept `var`/`let`/`mut`); re-base CF3 on flow-single-assign | parser/checker | umbrella G2 | [dry] | (folds R1) | — |
| SM-G3 | `static` kw + synthetic `self` receiver + `base` rename (accept both loose+synthetic) | parser/checker | umbrella G3 | [dry] | (folds R1) | — |
| SM-G4 | remove `-> ref T` return arm + gate cluster (rides G1) | parser/checker | umbrella G4 | [dry] | (folds R1) | SM-G1 |
| SM-G5 | Marshall opaque `ptr`/`uptr` + `__wrap`/`__unwrap` + tag runtime (inert) | checker/codegen/rt | umbrella G5 | [dry] | (folds R1) | — |
| SM-G6 | DI `service`/`svc` escape-taint + string-key (Part A banked; finish taint) | di/checker | umbrella G6 | [dry] | (folds R1) | — |
| SM-G7 | reclassify `teko::mem`+region primitives as SAFE intrinsics (`__wrap` supplies check) | scope/checker | umbrella G7 | [dry] | (folds R1) | SM-G5 |
| SM-G8 | retire manual memory (`mem::free`/`#must_free`/`Arena`/`RawBuf`/`Owned<T>`); migrate call-sites | mem/checker | umbrella G8 | [dry] | (folds R1) | SM-G7 |
| SM-G9 | add `size`/`usize` to `PrimKind` + prim predicates + prim→machine-type table (inert) | type/lir | umbrella G9 | [dry] | (folds R1) | — |
| SM-G10 | method overloading (relax same-name reject to param-signature distinctness) | checker/AL4a | umbrella G10 | [dry] | (folds R1) | — |
| SM-G11 | operator overloading (dunder map + derived `!=`/`>`/`<=`/`>=`) | checker/lir | umbrella G11 | [dry] | (folds R1) | — |
| SM-G12 | generic-constraint acceptance: a constraint accepts ANY type (named, `[]A`, `error`, and disjunctions like `[]A \| A \| error`) + the special markers (`class`/`struct`/`service`/`notnull`) + interfaces + service lifetimes (`transient`/`scoped`/`singleton`); rejects ONLY traits. Closes the gap the delivered §9.2b solver (`cf0c70b5`) left — `<T: []A \| A \| error>` currently fails. **Site:** the parser's "constraint term" grammar accepts a NAME, not a full type — broaden it to a full type. Observed CI 965, error verbatim: *"expected a constraint term (a form word `class`/`service`/`struct`, an interface/type name, or `notnull`) in a type-parameter constraint"* (that message is broadened by this crumb). | parser/checker | Doc-2:423-441 §9.2b + owner 2026-08-19 (CI 965) | [dry] | (folds R1) | — |
| 9D-T1 | inline-union: accept `A | B` structural union in field position (additive; `variant` still lives) | parser/checker | 9D §5.1(1) | [dry] | (folds R1) | — |

*Fixed-arrays + memory-model + string-u32 (absorbed collections FASE 0 + RM/io/arena new intrinsics):*

| id | goal | area/files | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| RM-C2 | `mem::copy(dst,at,src)` index-join primitive + the "count→`[total]byte=[]`→copy" idiom | mem | RM C2 | [dry] | (folds R1) | — |
| COL-F0a | `of_len` + `place`/`read`/`write`+`bucket` fixed-backing intrinsics | typer/codegen | COL FASE0 | [dry] | (folds R1) | RM-C2 |
| COL-F0b | class-holder escape (region-drop-via-escape, conservative GATE-1 default) | checker/escape | COL FASE0 | [dry] | (folds R1) | COL-F0a |
| COL-F0c | wrapped retain/release (refcount-wrap: root-arena `addr→count` dict) | rt/codegen | COL FASE0 / Doc-2:1619 | [dry] | (folds R1) | COL-F0a |
| COL-F0d | weak-ref hook + `deep_copy` + chunk-node capability + CAS helper | rt/codegen/sync | COL FASE0 | [dry] | (folds R1) | COL-F0a |
| SM-STRU32 | string as `[]u32` representation + codecs (utf-8↔u32 encode/decode) surface | str/codegen | Doc-2 (string-u32) | [dry] | (folds R1) | RM-C2 |
| IO-2 | `teko::mem::byte_ptr` builtin + lower (`lower_addr_of_place`) + codegen | scope/lir/codegen | io crumb 2 | [dry] | (folds R1) | SM-P1 |
| S16-MM-wp | `word_ptr` intrinsic (inverse of `ptr_word`) + C-leg load/store gap fix | scope/lir/codegen | arena-mmap §2.1/§2.2 | [dry] | (folds R1) | — |
| S16-MM-const | mmap syscall numbers/flags in `teko::sys` (`#arch`-guarded; §17 banked) | sys.tks | arena-mmap §2.3 | [dry] | (folds R1) | — |
| S16-SYNC-const | cross-platform sync/arena/thread ABI consts (`#os`-guarded) in `teko::sys` | sys.tks | sync crumb1 | [dry] | (folds R1) | — |

*The hinge:*

| id | goal | area/files | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| **SM-R1** | **THE ONE forward teaching reseed** — capture a seed that DPS-lowers + parses/knows ALL of M1 | `reseed-bootstrap.yml` | umbrella R1 | **[RITUAL]** | **teaching (1, the only forward one)** | ALL M1 rows |

\* A2–A5 individually carry a native-ladder ritual in dev; their *seed* is captured once at SM-R1
(the reseed is the harvest, not per-crumb). This is the umbrella's "P1 → Phase A → Phase G → R1".

### M2 — PURE-TEKO LIBRARY + MIGRATIONS (over the taught surface; NO teaching reseed)

*Source sweeps (byte-preserving; adopt the newly-seeded surface):*

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| SM-S1 | sweep `src/`+`.tkt` to `:` returns | all src | umbrella S1 | [fixpoint] | fixpoint-rebuild | SM-R1 |
| SM-S2 | sweep to `var`; drop `let`/`mut` acceptance | all src | umbrella S2 | [fixpoint] | fixpoint-rebuild | SM-R1 |
| SM-S3 | sweep methods to `self`/`base`/`static`; remove loose-receiver parse | ~89 sites+synth | umbrella S3 | [fixpoint] | fixpoint-rebuild | SM-R1 |
| SM-S6 | reball memory/collection positions `u64`→`usize`/`size` (source pos stays `u32`) | mem/collections | umbrella S6 | [fixpoint] | fixpoint-rebuild | SM-R1,SM-G9 |
| SM-S7 | (optional) adopt overloading in `src/` (empty if `src/` adopts nothing) | src | umbrella S7 | [fixpoint] | none/fixpoint | SM-R1 |

*Memory-reduction conversions (Eixo A — kill push; the ≤1.5 GB target lands here):*

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| RM-C1 | measure/shadow the `cb`/`append_fo` emit buffer (baseline; no src change) | obs | RM C1 | [dry] | none | — |
| RM-C3 | convert codegen emit buffer to spread-literal `b"…"`+`..str` index-materialize (the 93%) | codegen | RM C3 | [fixpoint] | fixpoint-rebuild | RM-C2,SM-R1 |
| RM-C4 | convert checker+build push (MAP/PARSE/FILTER/BUFFER; redesign `Env` ownership) | checker/build | RM C4 | [fixpoint] | fixpoint-rebuild | RM-C3 |
| RM-C5 | convert lir+backend+parser+codegen residual (LEnv/LowerCtx parallel arrays) | lir/backend/parser | RM C5 | [fixpoint] | fixpoint-rebuild | RM-C4 |
| RM-C6 | arena-per-scope at boundaries (`region_enter/leave/drop_subtree` + `arena_push/pop/commit`) | codegen/rt | RM C6 | [fixpoint] | fixpoint-rebuild | RM-C5 |
| RM-C7 | array literal via arena (`emit_array_lit` non-spread → `region_alloc`, kill raw `malloc`) | codegen | RM C7 | [fixpoint] | fixpoint-rebuild | RM-C6 |
| RM-C8 | targeted free + purge-on-reassign (`region_free`; `assign_frees_old`; `CgArenaSym::RegionFree`) | codegen | RM C8 | [fixpoint] | fixpoint-rebuild | RM-C7 |

*io-streaming library (leaves + compiler migrations):*

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| IO-1 | syscall constants (`SYS_READ/…`,`O_*`,`SEEK_*`) in `sys.tks` | sys | io crumb1 | [dry] | none | — |
| IO-3 | `file_stream.tks` (`os_*` wrappers + `FileStream` + open/read/write/seek/close) | io | io crumb3 | [dry] | none | IO-1,IO-2 |
| IO-4 | `stat`/`FileInfo`/`file_size` | fs | io crumb4 | [dry] | none | IO-1 |
| IO-5 | helpers `write_stream`/`append_stream`/`read_stream` (open+defer-close+1024 loop) | io | io crumb5 | [dry] | none | IO-3 |
| IO-6 | rewrite TOTAL forms (`read_file`/`write_file`/…) over stream (kill `teko_rt` FFI edge) | io/io.tks | io crumb6 | [fixpoint] | fixpoint-rebuild | IO-5 |
| IO-7 | migrate compiler reads to `read_stream` (`assemble`/`fmt`/`project`/`regression`) | src | io crumb7 | [fixpoint] | fixpoint-rebuild | IO-6 |
| IO-8 | migrate compiler writes to `write_stream`/`append_stream` (`project`/`fmt`/`regr_group`/`init`) | src | io crumb8 | [fixpoint] | fixpoint-rebuild | IO-7 |

*§16 runtime migrations (math/emit intrinsics + arena-over-mmap + fs/env/time + sync + runtime C→Teko):*

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| S16-EMIT | FASE1 emit intrinsics: floor/round/ceil + memcpy + own typedefs (kill `<math.h>/<string.h>/<stdint.h>` of emitted C) | codegen | expurgo FASE1 | [fixpoint] | fixpoint-rebuild | SM-R1 |
| S16-IO | FASE2 `SYS_exit_group`+`SYS_write` + float-bottom ftoa/`%.17g` pure-Teko | codegen/rt | expurgo FASE2 | [fixpoint] | fixpoint-rebuild | S16-EMIT |
| S16-MM-pool | arena-mmap meta-pool (mmap-specific memory-correctness keystone) | rt/arena | arena-mmap §3 | [dry] | none | S16-MM-wp,S16-MM-const |
| S16-MM-L1 | arena switch-over L1: Teko-over-mmap arena behind unchanged call-sites (shim indirection) | rt/arena | arena-mmap §5 L1 | [fixpoint] | fixpoint-rebuild | S16-MM-pool |
| S16-MM-L2 | arena switch-over L2: compiler's own runtime IS the Teko-over-mmap arena (load-bearing) | rt/arena | arena-mmap §5 L2 | [fixpoint] | fixpoint-rebuild | S16-MM-L1 |
| S16-FS | FASE4 fs+env+time+random (`open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/`getenv`/`getrandom`/`getrusage`) | fs/rt | expurgo FASE4 | [fixpoint] | fixpoint-rebuild | S16-MM-L2 |
| S16-SYNC | sync FFI+split (futex/ulock/WaitOnAddress) unblocking cross-platform seed | sync/tkp | sync crumb2+ | [fixpoint] | fixpoint-rebuild | S16-SYNC-const |
| S16-PANIC | FASE5 `assert.c`→Teko (compare+panic, L0-shaped) | assert | expurgo FASE5 | [fixpoint] | fixpoint-rebuild | S16-IO |
| RT-L0 | runtime C→Teko L0: io/panic/fmt/str-query/guards (pure over `[]byte`) | rt | runtime-migr L0 | [fixpoint] | fixpoint-rebuild | S16-IO |
| RT-L1 | L1: alloc+arena/regions+slice/box (the two compiler seams; mmap via extern) | rt | runtime-migr L1 | [fixpoint] | fixpoint-rebuild | S16-MM-L2 |
| RT-L2 | L2: UTF-8 char + rotatable str-construction (pure over L1) | rt/str | runtime-migr L2 | [fixpoint] | fixpoint-rebuild | RT-L1 |
| RT-L3 | L3: FFI host fs/env/time/date (POSIX/Win32 leaf syscalls) | rt/fs | runtime-migr L3 | [fixpoint] | fixpoint-rebuild | S16-FS |
| RT-L4 | L4: process/pipes/redirect (fork/exec/CreateProcess; struct-by-value FFI) — **GATE §16-FASE6** | rt | runtime-migr L4 / expurgo FASE6 | [fixpoint] | fixpoint-rebuild | RT-L3 |
| RT-L5 | L5: interning/task/names/coverage (process state over L1; cross-thread names) | rt | runtime-migr L5 | [fixpoint] | fixpoint-rebuild | RT-L1,S16-SYNC |
| RT-L6 | L6: test harness + assert + backtrace (setjmp/longjmp irreducible-partial + signal) — **owner gate** | rt/harness | runtime-migr L6 / expurgo FASE8 | [fixpoint] | fixpoint-rebuild | RT-L5,S16-PANIC |

*RM Eixo C — staged pipeline (per-unit + internal-FFI link; enables the terminal native):*

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| RM-C10 | determinize gensym (drop `buf.len`-derived temp names → global counter) — **pre-req of Eixo C** | codegen | RM C10 | [fixpoint] | fixpoint-rebuild | RM-C5 |
| RM-C11 | parse-per-unit → incomplete AST → LINK (the rich internal-FFI table feeding each unit's checker) | frontend | RM C11 | [fixpoint] | fixpoint-rebuild | RM-C10 |
| RM-C12 | fused check+lower+emit per unit (abstract unit-output; drop unit region) — **`teko.c` byte-identical** | backend | RM C12 | [fixpoint] | fixpoint-rebuild | RM-C11,RM-C6 |
| RM-C13 | typed `.tkb` per-unit disk dump (serialize/deserialize one namespace; deterministic frame) | emit | RM C13 | [fixpoint] | fixpoint-rebuild | RM-C12 |
| RM-C14 | incremental build (per-unit typed cache; OFF on self-build/fixpoint) | build | RM C14 | [dry] | none | RM-C12,RM-C13 |

*Collections library (pure `.tks` over M1's taught surface; core-consumes ⇒ fixpoint-rebuild):*

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| COL-Q1 | ChunkChain base (growable + TS substrate) | collections | COL Q1 | [dry] | none | COL-F0a |
| COL-Q3 | Ring base | collections | COL Q3 | [dry] | none | COL-F0a |
| COL-Q4 | Hash base | collections | COL Q4 | [dry] | none | COL-Q1 |
| COL-Q5 | Ordered base (+Node) | collections | COL Q5 | [dry] | none | COL-F0a |
| COL-Q6 | Heap base | collections | COL Q6 | [dry] | none | COL-Q1 |
| COL-Q7 | BitSet base | collections | COL Q7 | [dry] | none | COL-F0a |
| COL-Q8 | Weak wrappers (`Weak<T>`) | collections | COL Q8 | [dry] | none | COL-F0d |
| COL-Q9 | convert `List<T>` → chunk-chain + TS + three-category (build side) | list/collections | COL Q9 | [fixpoint] | fixpoint-rebuild | COL-Q1 |
| COL-Q10 | convert `Map`/`Dictionary`/`HashSet` → Hash (Map is core-consumed by `teko::env`) | collections | COL Q10 | [fixpoint] | fixpoint-rebuild | COL-Q4,COL-Q9 |
| COL-Q11 | convert `SortedSet`/`SortedDictionary` → Ordered | collections | COL Q11 | [dry]/[fixpoint] | none/fixpoint | COL-Q5,COL-Q10 |
| COL-Q12 | convert `PriorityQueue` → Heap | collections | COL Q12 | [dry]/[fixpoint] | none/fixpoint | COL-Q6,COL-Q11 |
| COL-Q13 | `Stack<T>` (wraps ChunkChain) | collections | COL Q13 | [dry] | none | COL-Q1 |
| COL-Q14 | `Queue<T>`/`Deque<T>` (wrap Ring) | collections | COL Q14 | [dry] | none | COL-Q3 |
| COL-Q15 | `LinkedList<T>` (doubly-linked; shares Node w/ Q5) | collections | COL Q15 | [dry] | none | COL-Q5 |
| COL-Q16 | `Counter<T>`/`MultiSet<T>` (wraps Dictionary) | collections | COL Q16 | [dry] | none | COL-Q10 |
| COL-Q17 | `MultiMap<K,V>` (wraps Dictionary<K,List<V>>) | collections | COL Q17 | [dry] | none | COL-Q10,COL-Q9 |
| COL-Q18 | `WeakMap`/`WeakSet` (Hash<Weak,·> + get-time prune) | collections | COL Q18 | [dry] | none | COL-Q4,COL-Q8 |
| COL-Q19 | `RingBuffer`/`BlockingCollection` (bounded Ring + condvar) | collections | COL Q19 | [dry] | none | COL-Q3,S16-SYNC |
| COL-Q20 | `Table<…>` core (chunk-chain rows + Map/SortedSet indices + atomic multi-index txn; ≤16 cols) | collections | COL Q20 / table doc | [dry] | none | COL-Q1,COL-Q4,COL-Q5 |
| COL-QQuery | `Table` query surface (LINQ-typed) — **BLOCKED on GATE-2 (owner ruled LINQ 2026-08-19)** | collections | COL Q-Query | [dry] | none | COL-Q20,GATE-2 |
| COL-QFile | `FileTable<Row>` (io-streaming backed; rebuild-indices-on-load) | collections | COL Q-File | [dry] | none | COL-Q20,IO-8 |

### M3 — CONSOLIDATED EXPURGO (clean removal; NO tombstone; minimal reseeds)

Each cluster removes de-taught surface only AFTER every caller is migrated (build-first). A removed
construct becomes simply **unrecognised** — no lexer token, no parser production, no checker rule, no
"was removed" diagnostic; an old-form program gets the SAME generic unknown-symbol/unexpected-token
error a never-existent name would get (COL FASE2 discipline; owner ruling 3).

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| SM-S4 | drop `->`/`Arrow` from lexer+`token.tks`; migrate `src/` FFI to opaque `ptr` | lexer/token | umbrella S4 | [RITUAL] | expurgo | SM-S1 (all `:` swept) |
| SM-S5 | DELETE `unsafe` kw + `is_unsafe` + contagion (last; nothing left to contain) | lexer/parser/checker | umbrella S5 | [RITUAL] | expurgo | SM-G8,SM-S4 |
| COL-F2 | remove `push`/single-array/`empty`/`with_cap`/`grow_inplace` + dead `arr_*`/`sorted_insert`/`heap_*`; clean lexer+parser+checker | list/typer/collections | COL FASE2 | [RITUAL] | expurgo | ALL COL-Q*, RM-C3..C8 |
| 9D-EXP | remove `type X = variant` form (folhas→raízes, `Type` last) + `Variant` carrier | parser/checker | 9D §5.1(2-6) | [RITUAL] | expurgo | 9D-T1, all inline-union sites migrated |
| RM-C9 | TERMINAL C-root removal + transcribe arena control slot (`.bss`/`MAP_FIXED`) to Teko | rt/codegen | RM C9 | [RITUAL] | expurgo | ALL RT-L*, S16-* |
| S16-SWEEP | FASE9 stop emitting `#include "teko_rt.h"/assert.h/win32_compat"`; DELETE the 4 C files; fix CI/build scripts | codegen/rt/CI | expurgo FASE9 | [RITUAL] | expurgo | RM-C9, all M2 §16 rows |

> **Consolidation note.** M3 is authored as **≤4 dispatched expurgo reseeds**, batching removals that
> share a "migration-complete" precondition: (E1) surface `{SM-S4, SM-S5}`; (E2) collections
> `{COL-F2}`; (E3) inline-union `{9D-EXP}`; (E4) the C-death `{RM-C9, S16-SWEEP}`. E1–E3 are
> independent and may be ordered by whichever migration finishes first; E4 is LAST (it is the §16
> TERMINAL gate and the C-root death, and it precedes the native endgame's C-route removal in M4).

### M4 — TERMINAL NATIVE `.o` (the endgame; 100% native, NO `cc`/`gcc`/`clang`)

The native legs (design-ahead; each independently gate-able against the DECLARED shape of its
predecessor) feed RM-C15/C16. Built in dependency order; the C route stays alive as the crutch until
C16 proves the native object reproduces.

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| NAT-A1 | close LIR lowering coverage frontier (new `LOp` cases, `LModule` growth, fat ptrs, control-flow) | lir | backend-a1 | [fixpoint] | fixpoint-rebuild | RM-C5 |
| NAT-A2 | arm64 isel (`minst.tks` + `isel_arm64.tks`) | backend | backend-a2 | [dry] | none | NAT-A1 |
| NAT-A3 | linear-scan regalloc + AAPCS64 descriptor (`regalloc.tks`,`abi_aapcs64.tks`) | backend | backend-a3 | [dry] | none | NAT-A2 |
| NAT-A4 | arm64 encoder + Mach-O object + link via system `ld` (`encode_arm64.tks`) | backend | backend-a4 | [dry] | none | NAT-A3 |
| NAT-B1 | x86-64 SysV ABI + ELF object (`abi_sysv64`,`minst_x86`,`isel_x86_64`; ELF writer reused) | backend | backend-b1 | [dry] | none | NAT-A3 |
| NAT-AARCH | aarch64-ELF relocation map ((MRelocKind,width,addend) key) + writer decls | backend/objfile_elf | aarch64-elf | [dry] | none | NAT-A4,NAT-B1 |
| NAT-B3 | Windows x86_64 (Win64 ABI + PE/COFF; `shadow_space` descriptor field; COFF writer) | backend | backend-b3 | [dry] | none | NAT-B1 |
| NAT-XL | host detection (`teko::os()`/`arch()`) + supported-targets table + cross-emit note (TKP ratified) | build/backend | teko-target-crosslink | [dry] | none | S17 (banked) |
| RM-C15 | terminal native: per-unit `.o` (lower→isel→regalloc→encode→`emit_elf`/`macho`/`coff`; unit region dropped = the dump). **Symbol visibility → object symbol table: `exp`+`pub`=GLOBAL (reaches `ld`), private=`static`/local (never enters the table).** The internal-FFI link (RM-C11) resolves cross-unit symbols; the OS `ld`/`objfile_ar` joins the final binary. C12's abstract unit-output = `.o` here. | backend | RM C15 | [fixpoint] | fixpoint-rebuild | RM-C12, NAT-A4/B1/B3/AARCH/XL |
| RM-C16 | migrate fixpoint from `gen2.c==gen3.c` to **native-object-reproducible** (no timestamp, stable symbol/section order, no abs path, canonical relocations; audit `objfile_*`/`objfile_ar`). When the 4 native CI legs are green + the object reproduces, **REMOVE the C route (`teko.c` + `cc`)**; the 2 C CI legs become native. Reseed of bootstrap becomes the native object/binary. | backend/CI/bootstrap | RM C16 | [RITUAL] | fixpoint-rebuild (native seed) | RM-C15, S16-SWEEP |
| RM-C17 | emit + package the compiler's own `.tkh` (aggregated `exp` surface); ship binary+`.tkh` (internal-FFI `exp`+`pub` stays transient, NOT in `.tkh`) | emit/project | RM C17 | [fixpoint] | fixpoint-rebuild | RM-C16 |

### M5 — ARENA TUNING (Doc-1; optimization of the already-correct arena; LAST)

Doc-1 adds NO capability — it tunes the arena Doc-2/§16 already produced (Doc-2:1632-1663). Each is a
perf/binary-size fixpoint, not a capability gate.

| id | goal | area | src | gate | reseed | deps |
|---|---|---|---|---|---|---|
| D1-T1 | arena pre-sizing / static floor (start at Σ expected slots; union → largest slot wins) | arena | arena-espec §3 | [fixpoint] | fixpoint-rebuild | RM-C16 |
| D1-T2 | physical DPS elision (backend returns direct into caller arena; realize the copy elision) | backend | arena-espec §5 | [fixpoint] | fixpoint-rebuild | SM-A2,RM-C16 |
| D1-T3 | arena elision proper (no region where nothing allocates; forwarders / const returns) | lir | arena-espec §4 | [fixpoint] | fixpoint-rebuild | SM-A4,RM-C16 |
| D1-T4 | static literal/folded-constant dedup (emit once + reference → smaller binary) | codegen | Doc-2:1660 | [fixpoint] | fixpoint-rebuild | RM-C16 |
| D1-T5 | push/copy-grow mitigation via known-size arrays + `with_cap` pre-capacity (levers already exist) | codegen/collections | Doc-2:1664 | [fixpoint] | fixpoint-rebuild | COL-F2 |

---

## 3. Reseed budget + parallelism map (program-wide consolidation)

### 3.1 The maximal dependency-closed FIRST teaching reseed (owner ask 2a)

**Everything in M1 fits ONE teaching reseed (SM-R1).** The set:
`{SM-A2..A5 (DPS/elision/push_inst_block), SM-G1..G11 (all additive grammar + Marshall + DI-taint +
size/usize + overloading), 9D-T1 (inline-union accept), RM-C2 + COL-F0a..d (fixed-arrays + full
memory model: place/read/write+bucket, escape-holder, retain/release, weak-ref, deep_copy,
chunk-node, CAS), SM-STRU32 (string-u32 codecs), IO-2 (byte_ptr), S16-MM-wp/const +
S16-SYNC-const (word_ptr + mmap/sync consts)}`. It is dependency-closed because every row is
additive/inert/self-lowering and authored in OLD spelling+types, so none consumes another's
not-yet-seeded surface, and the enabling pragma/syscall/extern-struct teaching is already banked
(TC-0). DPS folds into the same harvest (umbrella R1: the seed both DPS-lowers and parses-new).

### 3.2 What must WAIT — and why (owner ask 2b)

**Nothing waits for a SECOND *teaching* reseed.** The classic forcing edges are all satisfied by the
banked TC-0:

| forcing edge | normally forces a later teaching reseed | why NOT here |
|---|---|---|
| §17 pragmas → §16 FFI | §16 waits on `#os`/`#arch` in the seed | §17 already seeded (`20d7cb9b`) |
| §16 syscall → io-streaming / arena-mmap | those wait on the intrinsic in the seed | syscall intrinsic already seeded (`1a03a68e`) |
| `extern type=struct` → leaf FFI structs | leaves wait on the declarator | extern-struct already seeded (`c7ac134b`) |
| fixed-arrays → collections | Q1–Q22 wait on `of_len`/`place` in the seed | taught in M1 (COL-F0a), reseeded at SM-R1, library built in M2 |

What waits is not teaching but **migration** (M2, no teaching reseed — only fixpoint-rebuilds where
the core consumes a swap) and **removal** (M3 expurgo reseeds, which *de-teach*, and by build-first
law cannot precede migration). So the program-wide teaching budget is the theoretical floor: **1
forward teaching reseed.**

### 3.3 The program-wide reseed budget (recalculated under consolidation)

| class | count | which |
|---|---|---|
| **teaching (forward)** | **1** | SM-R1 (the whole M1 cluster) |
| **teaching (banked, historical)** | (10, done) | TC-0 (§2) — not in the forward budget |
| **expurgo** | **≤4** | E1 `{SM-S4,SM-S5}` · E2 `{COL-F2}` · E3 `{9D-EXP}` · E4 `{RM-C9,S16-SWEEP}` |
| **fixpoint-rebuild (core-consumes; teaches nothing)** | ~30 | the M2 `[fixpoint]` swaps (RM-C3..C8, IO-6..8, S16-*, RT-L*, RM-C10..C13, COL-Q9/Q10, SM-S1/S2/S3/S6) + M4 NAT/C15/C16/C17 + M5 |
| **native seed migration** | 1 (folded) | RM-C16 flips the reseed *criterion* from C-route to native-object; not a new teaching |

**Total teaching+expurgo reseeds (the ones that change what surface the seed understands): 5** (1
teaching + ≤4 expurgo). Everything else is byte-identity fixpoint-rebuild, which teaches/de-teaches
nothing. This is the minimum the build-first + no-tombstone laws permit.

### 3.4 Parallelism map (RESEED-SERIAL vs PARALLELIZABLE)

- **RESEED-SERIAL spine (dispatch strictly one at a time):** M0 SM-P1 → the whole M1 cluster
  converging at **SM-R1** → then every `[fixpoint]` swap in M2/M4/M5 (each rebuilds the compiler on
  the previous, byte-identity gate) → the M3 expurgo reseeds → RM-C16. A reseed/fixpoint item is never
  dispatched concurrently with another reseed/fixpoint item ("um reseed de cada vez", owner).
- **PARALLELIZABLE (design-only or independent leaves, authored off the serial spine):** within M1,
  the additive crumbs SM-G1..G11 / 9D-T1 / COL-F0* / IO-2 / S16-MM-* are mutually independent authoring
  tasks (they only converge at SM-R1). In M2 the `[dry]` collection bases/compositions (COL-Q1/Q3/Q5/
  Q7/Q13..Q20, COL-QFile) and the io/fs leaves (IO-1/3/4/5, S16-MM-pool) may be authored in parallel;
  the coordinator SERIALISES their *dispatch/build* and may interleave a `[dry]` item between two
  rebuilds only when its deps are green. The native legs NAT-A2/A3/A4/B1/B3/AARCH/XL are design-ahead
  in parallel against the declared predecessor shapes.
- **Owner rules baked in:** (a) **teach-once-per-phase** — M1 teaches the whole program's surface in
  one reseed to minimize seeds; (b) **no redundant fixtures** for what the fixpoint already exercises
  (write `.tkr` only for paths the self-build never touches — io error branches, collection
  concurrency/leak boundaries, `EXPECT_COMPILE_FAIL` rejects, the `expurgo_unknown_not_tombstone`
  guard); (c) **removals = clean expurgo** of lexer/parser/checker with **NO tombstone diagnostic**;
  (d) the **6.5 GiB build guard (`ulimit -v`) is INVIOLABLE** at every gate — a blown guard is a
  root-cause fix, never a raised ceiling.

---

## 4. The terminal native milestone, detailed (M4 as its own phase)

**Goal:** a 100% native binary with NO C toolchain — no `cc`/`gcc`/`clang` — that emits a linkable
`.o` per unit, which the OS linker (`ld`/lld / the platform linker) links into the final binary.

**The per-unit pipeline (RM-C15, the endgame of RM Eixo C):** for each namespace, in deterministic
order — `lower_program`/`lower_item` (NAT-A1 closes the LIR frontier) → **isel** (`select_module*`:
NAT-A2 arm64, NAT-B1 x86-64, NAT-B3 Win64) → **regalloc** (`regalloc_module`, linear-scan, NAT-A3 +
the ABI descriptors AAPCS64/SysV64/Win64) → **encode** (`encode_module`, NAT-A4/B1/B3 bit-exact) →
**emit** `emit_elf`/`emit_macho`/`emit_coff` → one `.o` on disk; the unit's arena region is dropped
(the `.o` IS the dump — the memory-ceiling win of Eixo C realized on the native route). The aarch64
relocation map (NAT-AARCH) is keyed by `(MRelocKind, width, addend)`, not `MRelocKind` alone (the
reproduced link-and-fail bug).

**Symbol-table / visibility mapping (RM-C15):** `exp`+`pub` → **GLOBAL** symbol (visible to `ld` for
cross-unit link); **private** → `static`/**local** (never enters the symbol table). The object's
symbol table IS the internal-FFI (RM-C11) on the linkage plane — `pub` reaches `ld` without leaking
into the `.tkh`. The "own linker" (the internal-FFI of C11) resolves symbols between units; the OS
`ld` (or `objfile_ar`) joins the final binary. The runtime is linked the normal way: `ld` resolves
libc/kernel32 symbols as undefined externals (runtime-migr §2.2, proven `afdb1fd8`).

**Fixpoint migration + C-route removal (RM-C16):** move the fixpoint criterion from `gen2.c==gen3.c`
to **native-object-reproducible** — deterministic `.o` (no timestamp, stable symbol/section ordering,
no absolute paths, canonical relocations; audit `objfile_*`/`objfile_ar`). When the 4 native CI legs
are green AND the object reproduces, **remove the C route entirely** (`teko.c` + `cc`); the 2 C CI
legs are retargeted to native; the bootstrap reseed becomes the native object/binary. RM-C17 then
ships binary + compiler `.tkh`.

**Ordering within M4:** the native legs (NAT-*) are design-ahead and land against the DECLARED shape
of their predecessor (A1 LIR → A2 isel → A3 regalloc → A4/B1/B3 encoders → AARCH reloc → XL host/
cross). RM-C15 requires RM-C12 (fused per-unit emit) green on the C route first ("saída abstrata de
C12 = `.o` aqui", RM C15). RM-C16 requires **S16-SWEEP / RM-C9** (the C-death of §16) already done —
the native endgame removes the *compile* dependency on C; §16 removes the *runtime* dependency. Both
must close for "no C" to be literally true.

---

## 5. Open owner-gates (flagged, not blocking where a conservative default exists)

- **GATE-1 — class lifetime in a long-lived collection.** A `class` element removed early: region-drop
  via escape (conservative, ships now) vs promote-to-wrapped (eager free). **Resolution (baked):** M1
  COL-F0b teaches the conservative **region-drop-via-escape** holder semantics now (leak-safe, never
  UAF); promote-to-wrapped is an additive `[dry]` follow-up per collection when GATE-1 closes. Touches
  COL-F0b, COL-Q9/Q10/Q11/Q12/Q15 — none blocked. (COL §8, record.)
- **GATE-2 — Table query surface. RESOLVED: LINQ-typed (owner 2026-08-19, "vamos de LINQ, melhor").**
  Blocks ONLY COL-QQuery; COL-Q20 (Table core) and COL-QFile proceed. SQL-string is an optional later
  thin front-end desugaring to the LINQ chain (law-aligned; a string front-end needs the dynamic
  `Value` model the type system exists to avoid). No HALT.
- **P1 engineering go/no-go (SM-P1).** If `type_match`+`frame_sweep_inst` pin to the return/tail-merge
  facet, DPS (SM-A2/A3) closes both by construction (two-birds) and the fixpoint+memory tracks are the
  SAME work. If P1 disconfirms (payload-bind offset), DPS still lands for memory+correctness and the
  fixpoint track decouples — the surface wave proceeds on the C-route reseed regardless. This is
  engineering sequencing, not a law conflict; no HALT (umbrella §11/§14 R1).
- **§16 owner-gates (deferred, conservative default = keep C until closed):** **RT-L4 / §16-FASE6**
  (process/exec/pipes) is BLOCKED on struct-by-value FFI + a Win32 import-lib linker (Fase E);
  **RT-L6 / §16-FASE8** (setjmp/longjmp, signal, backtrace, dladdr) is the named irreducible-partial
  core awaiting the owner's ruling. Both keep their C twin alive (the `teko_rt`/assert maintained-C
  exception) until their gate closes; they do NOT block the memory target (Eixos A/B) nor the native
  compile endgame (M4), which removes the C *compile* dependency independently of these runtime tails.

---

## 6. Cross-doc ordering conflicts found + proposed resolution (flagged, not silently picked)

1. **§16 placement vs Doc-1.** Doc-2:1231-1232 lists §16/§17 as "ordem: ÚLTIMA"; Doc-2:1246-1254
   corrects this — §16 is PRE-Doc-1 (§17→§16→§11, all Doc-2), and Doc-1 is TUNING-only after. **Resolution
   adopted:** the corrected reading (§16 in M2/M3, Doc-1 in M5). The stale "última" rows are superseded
   by the 2026-08-15 owner correction in the same doc.
2. **Collections FASE 0 vs the program-wide teaching cluster.** The collections queue defines its own
   `[RITUAL]` "FASE 0" teaching reseed. **Resolution (owner ruling 2026-08-19):** ABSORB it into M1
   (COL-F0a..d) — it is NOT a separate reseed; the whole program's surface teaches once at SM-R1.
3. **io-streaming crumb 2 (`byte_ptr`) blocked on arena P1.** io §10 flags it design-ahead. **Resolution:**
   IO-2 folds into M1 alongside SM-P1/SM-A* (the addr-of-index lowering it needs is the same
   `lower_addr_of_place` P1 pins); it reseeds with SM-R1, not separately.
4. **§9.D reseed ladder (per-ADT).** 9D §5.1 defines a folhas→raízes de-teach/reseed ladder. **Resolution:**
   split it — the ADDITIVE accept (`A|B` in fields, 9D-T1) folds into M1/SM-R1; the REMOVAL of `type X =
   variant` + the carrier folds into the M3 expurgo cluster E3 (9D-EXP), after all inline-union sites
   are migrated in M2. This honors both build-first and the one-teaching-reseed consolidation.

No genuine unresolved law tension forces a HALT: every fork is resolved law-first (M.1/M.5 austerity,
build-first, no-tombstone) with a conservative default that lets the rest proceed. The only true owner
decision gates are P1 (engineering) and the two §16 tails (RT-L4/L6), each with a keep-C default.
