# teko-lang 0.3.1 — EXECUTION-ORDER CONTROL (2026-08-19)

> **Purpose:** Single ledger fixing TRUE execution order for ALL 122 crumbs (0001–0122), including
> the 8 gap-crumbs (0113–0120) and 2 recovered new crumbs (0121–0122). Source of truth for order,
> milestone spine, gate dependencies, and current STATUS (landed / verify-only / needs-impl / on-branch
> / gated / deferred).
>
> **Compiled:** 2026-08-19 (owner decisions D53 consolidated; await-model deliberation on-file).
> **Total crumbs:** 122 (112 from master-plan + 8 gap + 2 recovered).
>
> **STATUS = implementation status in `src/` (not doc existence).** `planned/unverified` rows are
> confirmed by the crumb's scout-pass at execution (two-pass law); updated as crumbs land.
>
> **Status vocabulary:**
> - `landed` — work is complete and integrated into HEAD
> - `verify-only` — mechanism exists but needs confirmation (not implementation)
> - `needs-impl` — work is open and on queue
> - `in-progress` — actively being implemented
> - `partial` — partial implementation present; missing pieces identified
> - `on-branch:<name>` — undrained work waiting on a recovery branch
> - `planned:verify-at-exec` — planned, unverified; will be confirmed at execution (scout-pass)
> - `gated:D52` — blocked by owner gate (memory stabilization)
> - `deferred:post-native-fixpoint-green` — deferred until native reaches fixpoint + green tests

| seq | filename | crumb-id | milestone | deps | gate | status |
|---|---|---|---|---|---|---|
| 0001 | 0001-SM-P1-pin-rootmap-dps.md | SM-P1 | M0 | — | [dry] | done |
| 0002 | 0002-SM-A1-instrument-return-box.md | SM-A1 | M1 | SM-P1 | [dry] | needs-impl |
| 0003 | 0003-SM-A2-dps-abi-lower-return.md | SM-A2 | M1 | SM-A1 | [RITUAL] | needs-impl |
| 0004 | 0004-SM-A3-retire-own-returned.md | SM-A3 | M1 | SM-A2 | [RITUAL] | needs-impl |
| 0005 | 0005-SM-A4-arena-elision-guard.md | SM-A4 | M1 | SM-A1 | [RITUAL] | needs-impl |
| 0006 | 0006-SM-A5-push-inst-block-fix.md | SM-A5 | M1 | SM-P1 | [RITUAL] | gated:D52 |
| 0007 | 0007-SM-G1-colon-return-operator.md | SM-G1 | M1 | — | [dry] | landed/verify-only |
| 0008 | 0008-SM-G2-var-let-mut-merge.md | SM-G2 | M1 | — | [dry] | needs-impl |
| 0009 | 0009-SM-G3-static-self-base.md | SM-G3 | M1 | — | [dry] | verify-only |
| 0010 | 0010-SM-G4-remove-ref-return-arm.md | SM-G4 | M1 | SM-G1 | [dry] | verify-only |
| 0011 | 0011-SM-G5-marshall-opaque-ptr.md | SM-G5 | M1 | — | [dry] | gated:owner |
| 0012 | 0012-SM-G6-di-service-taint.md | SM-G6 | M1 | — | [dry] | gated:owner |
| 0013 | 0013-SM-G7-mem-region-safe-intrinsics.md | SM-G7 | M1 | SM-G5 | [dry] | needs-impl |
| 0014 | 0014-SM-G8-retire-manual-memory.md | SM-G8 | M1 | SM-G7 | [dry] | needs-impl |
| 0015 | 0015-SM-G9-size-usize-primkind.md | SM-G9 | M1 | — | [dry] | needs-impl |
| 0016 | 0016-SM-G10-method-overloading.md | SM-G10 | M1 | — | [dry] | needs-impl |
| 0017 | 0017-SM-G11-operator-overloading.md | SM-G11 | M1 | — | [dry] | needs-impl |
| 0018 | 0018-SM-G12-constraint-acceptance.md | SM-G12 | M1 | — | [dry] | landed |
| 0019 | 0019-9D-T1-inline-union-accept.md | 9D-T1 | M1 | — | [dry] | verify-only |
| 0020 | 0020-RM-C2-mem-copy-index-join.md | RM-C2 | M1 | — | [dry] | landed |
| 0021 | 0021-COL-F0a-of-len-fixed-backing.md | COL-F0a | M1 | RM-C2 | [dry] | landed |
| 0022 | 0022-COL-F0b-class-holder-escape.md | COL-F0b | M1 | COL-F0a | [dry] | landed |
| 0023 | 0023-COL-F0c-wrapped-retain-release.md | COL-F0c | M1 | COL-F0a | [dry] | landed |
| 0024 | 0024-COL-F0d-weak-deepcopy-cas.md | COL-F0d | M1 | COL-F0a | [dry] | in-progress |
| 0025 | 0025-SM-STRU32-string-u32-codecs.md | SM-STRU32 | M1 | RM-C2 | [dry] | landed |
| 0026 | 0026-IO-2-byte-ptr-builtin.md | IO-2 | M1 | SM-P1 | [dry] | verify-only |
| 0027 | 0027-S16-MM-wp-word-ptr-intrinsic.md | S16-MM-wp | M1 | — | [dry] | verify-only |
| 0028 | 0028-S16-MM-const-mmap-syscall-consts.md | S16-MM-const | M1 | — | [dry] | verify-only |
| 0029 | 0029-S16-SYNC-const-sync-abi-consts.md | S16-SYNC-const | M1 | — | [dry] | verify-only |
| 0030 | 0030-SM-R1-teaching-reseed.md | SM-R1 | M1 | ALL M1 rows | [RITUAL] | needs-impl |
| 0031 | 0031-SM-S1-sweep-colon-returns.md | SM-S1 | M2 | SM-R1 | [fixpoint] | verify-only |
| 0032 | 0032-SM-S2-sweep-var.md | SM-S2 | M2 | SM-R1 | [fixpoint] | verify-only |
| 0033 | 0033-SM-S3-sweep-self-base-static.md | SM-S3 | M2 | SM-R1 | [fixpoint] | verify-only |
| 0034 | 0034-SM-S6-reball-usize-size.md | SM-S6 | M2 | SM-R1,SM-G9 | [fixpoint] | planned:verify-at-exec |
| 0035 | 0035-SM-S7-adopt-overloading.md | SM-S7 | M2 | SM-R1 | [fixpoint] | planned:verify-at-exec |
| 0036 | 0036-RM-C1-measure-emit-buffer.md | RM-C1 | M2 | — | [dry] | planned:verify-at-exec |
| 0037 | 0037-RM-C3-codegen-spread-literal.md | RM-C3 | M2 | RM-C2,SM-R1 | [fixpoint] | planned:verify-at-exec |
| 0038 | 0038-RM-C4-checker-build-push.md | RM-C4 | M2 | RM-C3 | [fixpoint] | planned:verify-at-exec |
| 0039 | 0039-RM-C5-lir-backend-residual.md | RM-C5 | M2 | RM-C4 | [fixpoint] | planned:verify-at-exec |
| 0040 | 0040-RM-C6-arena-per-scope.md | RM-C6 | M2 | RM-C5 | [fixpoint] | planned:verify-at-exec |
| 0041 | 0041-RM-C7-array-lit-arena.md | RM-C7 | M2 | RM-C6 | [fixpoint] | planned:verify-at-exec |
| 0042 | 0042-RM-C8-targeted-free-purge.md | RM-C8 | M2 | RM-C7 | [fixpoint] | planned:verify-at-exec |
| 0043 | 0043-IO-1-syscall-constants.md | IO-1 | M2 | — | [dry] | planned:verify-at-exec |
| 0044 | 0044-IO-3-file-stream.md | IO-3 | M2 | IO-1,IO-2 | [dry] | planned:verify-at-exec |
| 0045 | 0045-IO-4-stat-fileinfo.md | IO-4 | M2 | IO-1 | [dry] | planned:verify-at-exec |
| 0046 | 0046-IO-5-stream-helpers.md | IO-5 | M2 | IO-3 | [dry] | planned:verify-at-exec |
| 0047 | 0047-IO-6-total-forms-over-stream.md | IO-6 | M2 | IO-5 | [fixpoint] | planned:verify-at-exec |
| 0048 | 0048-IO-7-migrate-compiler-reads.md | IO-7 | M2 | IO-6 | [fixpoint] | planned:verify-at-exec |
| 0049 | 0049-IO-8-migrate-compiler-writes.md | IO-8 | M2 | IO-7 | [fixpoint] | planned:verify-at-exec |
| 0050 | 0050-S16-EMIT-emit-intrinsics.md | S16-EMIT | M2 | SM-R1 | [fixpoint] | planned:verify-at-exec |
| 0051 | 0051-S16-IO-sys-exit-write-ftoa.md | S16-IO | M2 | S16-EMIT | [fixpoint] | planned:verify-at-exec |
| 0052 | 0052-S16-MM-pool-arena-mmap-meta-pool.md | S16-MM-pool | M2 | S16-MM-wp,S16-MM-const | [dry] | planned:verify-at-exec |
| 0053 | 0053-S16-MM-L1-arena-switchover-l1.md | S16-MM-L1 | M2 | S16-MM-pool | [fixpoint] | planned:verify-at-exec |
| 0054 | 0054-S16-MM-L2-arena-switchover-l2.md | S16-MM-L2 | M2 | S16-MM-L1 | [fixpoint] | planned:verify-at-exec |
| 0055 | 0055-S16-FS-fs-env-time-random.md | S16-FS | M2 | S16-MM-L2 | [fixpoint] | planned:verify-at-exec |
| 0056 | 0056-S16-SYNC-sync-ffi-split.md | S16-SYNC | M2 | S16-SYNC-const | [fixpoint] | planned:verify-at-exec |
| 0057 | 0057-S16-PANIC-assert-to-teko.md | S16-PANIC | M2 | S16-IO | [fixpoint] | planned:verify-at-exec |
| 0058 | 0058-RT-L0-runtime-io-panic-fmt.md | RT-L0 | M2 | S16-IO | [fixpoint] | planned:verify-at-exec |
| 0059 | 0059-RT-L1-runtime-alloc-arena.md | RT-L1 | M2 | S16-MM-L2 | [fixpoint] | planned:verify-at-exec |
| 0060 | 0060-RT-L2-runtime-utf8-str.md | RT-L2 | M2 | RT-L1 | [fixpoint] | planned:verify-at-exec |
| 0061 | 0061-RT-L3-runtime-ffi-fs-env.md | RT-L3 | M2 | S16-FS | [fixpoint] | planned:verify-at-exec |
| 0062 | 0062-RT-L4-runtime-process-pipes.md | RT-L4 | M2 | RT-L3 | [fixpoint] | planned:verify-at-exec |
| 0063 | 0063-RT-L5-runtime-interning-task.md | RT-L5 | M2 | RT-L1,S16-SYNC | [fixpoint] | planned:verify-at-exec |
| 0064 | 0064-RT-L6-runtime-test-harness.md | RT-L6 | M2 | RT-L5,S16-PANIC | [fixpoint] | planned:verify-at-exec |
| 0065 | 0065-RM-C10-determinize-gensym.md | RM-C10 | M2 | RM-C5 | [fixpoint] | planned:verify-at-exec |
| 0066 | 0066-RM-C11-parse-per-unit-link.md | RM-C11 | M2 | RM-C10 | [fixpoint] | planned:verify-at-exec |
| 0067 | 0067-RM-C12-fused-check-lower-emit.md | RM-C12 | M2 | RM-C11,RM-C6 | [fixpoint] | planned:verify-at-exec |
| 0068 | 0068-RM-C13-tkb-per-unit-dump.md | RM-C13 | M2 | RM-C12 | [fixpoint] | planned:verify-at-exec |
| 0069 | 0069-RM-C14-incremental-build.md | RM-C14 | M2 | RM-C12,RM-C13 | [dry] | planned:verify-at-exec |
| 0070 | 0070-COL-Q1-chunkchain-base.md | COL-Q1 | M2 | COL-F0a | [dry] | planned:verify-at-exec |
| 0071 | 0071-COL-Q3-ring-base.md | COL-Q3 | M2 | COL-F0a | [dry] | planned:verify-at-exec |
| 0072 | 0072-COL-Q4-hash-base.md | COL-Q4 | M2 | COL-Q1 | [dry] | planned:verify-at-exec |
| 0073 | 0073-COL-Q5-ordered-base.md | COL-Q5 | M2 | COL-F0a | [dry] | planned:verify-at-exec |
| 0074 | 0074-COL-Q6-heap-base.md | COL-Q6 | M2 | COL-Q1 | [dry] | planned:verify-at-exec |
| 0075 | 0075-COL-Q7-bitset-base.md | COL-Q7 | M2 | COL-F0a | [dry] | planned:verify-at-exec |
| 0076 | 0076-COL-Q8-weak-wrappers.md | COL-Q8 | M2 | COL-F0d | [dry] | planned:verify-at-exec |
| 0077 | 0077-COL-Q9-list-chunkchain.md | COL-Q9 | M2 | COL-Q1 | [fixpoint] | planned:verify-at-exec |
| 0078 | 0078-COL-Q10-map-dict-hashset.md | COL-Q10 | M2 | COL-Q4,COL-Q9 | [fixpoint] | planned:verify-at-exec |
| 0079 | 0079-COL-Q11-sorted-set-dict.md | COL-Q11 | M2 | COL-Q5,COL-Q10 | [dry]/[fixpoint] | planned:verify-at-exec |
| 0080 | 0080-COL-Q12-priorityqueue-heap.md | COL-Q12 | M2 | COL-Q6,COL-Q11 | [dry]/[fixpoint] | planned:verify-at-exec |
| 0081 | 0081-COL-Q13-stack.md | COL-Q13 | M2 | COL-Q1 | [dry] | planned:verify-at-exec |
| 0082 | 0082-COL-Q14-queue-deque.md | COL-Q14 | M2 | COL-Q3 | [dry] | planned:verify-at-exec |
| 0083 | 0083-COL-Q15-linkedlist.md | COL-Q15 | M2 | COL-Q5 | [dry] | planned:verify-at-exec |
| 0084 | 0084-COL-Q16-counter-multiset.md | COL-Q16 | M2 | COL-Q10 | [dry] | planned:verify-at-exec |
| 0085 | 0085-COL-Q17-multimap.md | COL-Q17 | M2 | COL-Q10,COL-Q9 | [dry] | planned:verify-at-exec |
| 0086 | 0086-COL-Q18-weakmap-weakset.md | COL-Q18 | M2 | COL-Q4,COL-Q8 | [dry] | planned:verify-at-exec |
| 0087 | 0087-COL-Q19-ringbuffer-blocking.md | COL-Q19 | M2 | COL-Q3,S16-SYNC | [dry] | planned:verify-at-exec |
| 0088 | 0088-COL-Q20-table-core.md | COL-Q20 | M2 | COL-Q1,COL-Q4,COL-Q5 | [dry] | planned:verify-at-exec |
| 0089 | 0089-COL-QQuery-table-query-linq.md | COL-QQuery | M2 | COL-Q20,GATE-2 | [dry] | planned:verify-at-exec |
| 0090 | 0090-COL-QFile-filetable.md | COL-QFile | M2 | COL-Q20,IO-8 | [dry] | planned:verify-at-exec |
| 0091 | 0091-SM-S4-drop-arrow-lexer.md | SM-S4 | M3 | SM-S1 (all `:` swept) | [RITUAL] | verify-only |
| 0092 | 0092-SM-S5-delete-unsafe-kw.md | SM-S5 | M3 | SM-G8,SM-S4 | [RITUAL] | planned:verify-at-exec |
| 0093 | 0093-COL-F2-remove-push-legacy.md | COL-F2 | M3 | ALL COL-Q*, RM-C3..C8 | [RITUAL] | planned:verify-at-exec |
| 0094 | 0094-9D-EXP-remove-variant-form.md | 9D-EXP | M3 | 9D-T1, all inline-union sites migrated | [RITUAL] | planned:verify-at-exec |
| 0095 | 0095-RM-C9-terminal-c-root-removal.md | RM-C9 | M3 | ALL RT-L*, S16-* | [RITUAL] | planned:verify-at-exec |
| 0096 | 0096-S16-SWEEP-delete-c-files.md | S16-SWEEP | M3 | RM-C9, all M2 §16 rows | [RITUAL] | planned:verify-at-exec |
| 0097 | 0097-NAT-A1-lir-lowering-frontier.md | NAT-A1 | M4 | RM-C5 | [fixpoint] | planned:verify-at-exec |
| 0098 | 0098-NAT-A2-arm64-isel.md | NAT-A2 | M4 | NAT-A1 | [dry] | planned:verify-at-exec |
| 0099 | 0099-NAT-A3-regalloc-aapcs64.md | NAT-A3 | M4 | NAT-A2 | [dry] | planned:verify-at-exec |
| 0100 | 0100-NAT-A4-arm64-encoder-macho.md | NAT-A4 | M4 | NAT-A3 | [dry] | planned:verify-at-exec |
| 0101 | 0101-NAT-B1-x86-64-sysv-elf.md | NAT-B1 | M4 | NAT-A3 | [dry] | planned:verify-at-exec |
| 0102 | 0102-NAT-AARCH-aarch64-elf-reloc.md | NAT-AARCH | M4 | NAT-A4,NAT-B1 | [dry] | planned:verify-at-exec |
| 0103 | 0103-NAT-B3-windows-coff.md | NAT-B3 | M4 | NAT-B1 | [dry] | planned:verify-at-exec |
| 0104 | 0104-NAT-XL-host-detect-crosslink.md | NAT-XL | M4 | S17 (banked) | [dry] | planned:verify-at-exec |
| 0105 | 0105-RM-C15-terminal-native-o.md | RM-C15 | M4 | RM-C12, NAT-A4/B1/B3/AARCH/XL | [fixpoint] | planned:verify-at-exec |
| 0106 | 0106-RM-C16-native-object-fixpoint.md | RM-C16 | M4 | RM-C15, S16-SWEEP | [RITUAL] | planned:verify-at-exec |
| 0107 | 0107-RM-C17-emit-tkh-package.md | RM-C17 | M4 | RM-C16 | [fixpoint] | planned:verify-at-exec |
| 0108 | 0108-D1-T1-arena-pre-sizing.md | D1-T1 | M5 | RM-C16 | [fixpoint] | needs-impl |
| 0109 | 0109-D1-T2-physical-dps-elision.md | D1-T2 | M5 | SM-A2,RM-C16 | [fixpoint] | needs-impl |
| 0110 | 0110-D1-T3-arena-elision-proper.md | D1-T3 | M5 | SM-A4,RM-C16 | [fixpoint] | needs-impl |
| 0111 | 0111-D1-T4-literal-dedup.md | D1-T4 | M5 | RM-C16 | [fixpoint] | needs-impl |
| 0112 | 0112-D1-T5-push-copy-mitigation.md | D1-T5 | M5 | COL-F2 | [fixpoint] | needs-impl |
| 0113 | 0113-NAT-N1-union-nullable-native-lowering.md | NAT-N1 | M2 | NAT-A1 | [fixpoint] (write-only native + C fixpoint) | re-seq:tail-§16 R#1 (D106) |
| 0114 | 0114-NAT-N2-fatptr-dispatch-match-operator-residual.md | NAT-N2 | M2 | NAT-N1 | [fixpoint] (write-only native + C fixpoint) | re-seq:tail-§16 R#2 (D106) |
| 0115 | 0115-S10-SURF-concurrency-surface-teaching.md | S10-SURF | M2 | — | [dry] | verify-and-wire |
| 0116 | 0116-S10-RT-threads-channels-intent-runtime.md | S10-RT | M2 | S10-SURF | [fixpoint] | verify-and-wire |
| 0117 | 0117-D1-DI-arena-lifetime-binding.md | D1-DI | M5 | S10-RT, RM-C16 | [fixpoint] | needs-impl |
| 0118 | 0118-STD-ENC-encoding-catalog-tail.md | STD-ENC | M2 | — | [verify] | verify-only |
| 0119 | 0119-STD-FFI-ffi-dependent-stdlib.md | STD-FFI | M2 | — | [fixpoint] | needs-impl |
| 0120 | 0120-SM-V1-visibility-enforcement-activation.md | SM-V1 | M2 | RM-C17 | [fixpoint] | verify-only |
| 0121 | 0121-ERR-FACTORY-error-factory-builtins.md | ERR-FACTORY | M2 | SM-G4 | [fixpoint] | on-branch:origin/cargo/0.3.1.0-error-factory |
| 0122 | 0122-S16-SYSCALL-PORTABLE-host-portability.md | S16-SYSCALL-PORTABLE | M2 | RM-C17 | [fixpoint] | on-branch:origin/feat/s16-syscall-portable |
| 0123 | 0123-COL-F0c2-wrap-table-redesign.md | COL-F0c2 | M1 | COL-F0c,COL-F0d | [dry] | gated:FORK-ABERTO-1 |
| 0124 | 0124-RT-L4-ENV-envp-capture-zerolibc.md | RT-L4-ENV | M2 | S16-FS, RT-ENTRY | [RITUAL] | needs-impl |
| 0125 | 0125-RT-ENTRY-start-abi-per-os.md | RT-ENTRY | M2 | S16-FS | [RITUAL] | needs-impl |
| 0126 | 0126-NAT-B0-graceful-stop-capture.md | NAT-B0 | M2 | RT-L6 | [fixpoint] (write-only native + C fixpoint) | needs-impl:tail-§16 R#2 |
| 0127 | 0127-NAT-B3-lowering-crash-invariant-sweep.md | NAT-B3 | M2 | NAT-A1 | [fixpoint] (write-only native + C fixpoint) | needs-impl:tail-§16 R#1/R#3 |
| 0128 | 0128-NAT-B4-campaign-emission-native-mirror.md | NAT-B4 | M2 | RT-ENTRY | [fixpoint] (write-only native + C fixpoint) | needs-impl:tail-§16 R#1/R#4 |
| 0139 | 0139-DI-FIX1-svc-fixtures-cert.md | DI-FIX1 | M5 | — | [fixpoint] | needs-impl:cert-only (Part A + CR-A/C/D landed; fixtures absent) |
| 0140 | 0140-S10-CC5-svc-channel-by-key-di.md | S10-CC5 | M5 | S10-CC3, S10-CC4, DI-FIX1 | [fixpoint] | needs-impl:removes svc<> honest-stops in 0136/0137/0138 (D120) |

## Native lowering coverage re-sequencing (2026-08-26, D106 — `native-lowering-cobertura-zero-libc-0.3.1.md`)

- **Métrica corrigida.** Aderência native = `gen1` emite gen2 native COMPLETO por-arch (`item N/TOTAL`),
  write-only. O gate `[fixpoint] gen2==gen3` **native** dos crumbs 0113/0114 era INALCANÇÁVEL (gen2 native
  nem existe); trocado para **write-only native emit + C `gen2.c==gen3.c`** do reseed de fase. O `gen2==gen3`
  native só vira gate pós-F9 (`0106`/RM-C16).
- **Aderência REAL (CI #1135, `4f488589`):** x86_64-glibc morre em `item 933/9378` `emit_elf_shdr`
  (`objfile_elf.tks:398`/`:425`, HANG/OOM); arm64-musl CRASH em `item 90/9378` `debug_info_locals`
  (`dwarf.tks:508`, stack-smash sret AAPCS64); windows para em `item 1145/9367` `asm_module_items`
  (`assemble.tks:28`, sret Win64); macOS nem compila (`__builtin_longjmp` não suportado). Aderência <12%.
- **Pull-forward (D106):** 0113/0114 saem de `gated:D52`/M4 e rodam nos reseeds do tail-§16 (R#1/F7a e
  R#2/F8) — write-only, NÃO esperam o marco de memória nem RM-C15.
- **Graceful-stop (dono 2026-08-26):** capture/exit/cancel viram controle de fluxo por flag+return
  (0126), SUPERSEDE o setjmp/longjmp de D105/0064 — elimina o mac-blocker.
- **Crumbs novos:** 0126 (graceful-stop capture) · 0127 (classe-b crash/sret + invariante-variante sweep) ·
  0128 (espelho native das emissões da campanha: clone-aarch64, `_start`/`stack_ptr`, spawn-mmap).

## Gap Crumbs Status Summary (0113–0120)

**Appended 2026-08-19 per owner decisions (D53):**
- **0113 NAT-N1 / 0114 NAT-N2** — native lowering (union/nullable family + residual operators, 21 KNOWN-WRONG) — **RE-SEQUENCED (2026-08-26, D106):** no longer GATED(D52); pulled forward to tail-§16 R#1/R#2 (write-only native + C fixpoint), superseding the memory-milestone gate. See the "Native lowering coverage re-sequencing" section above.
- **0115 S10-SURF / 0116 S10-RT** — §10 concurrency surface + runtime (spawn/chan/journal/threads/ref-guard/sync) — ~70-85% landed (verify-and-wire mode). Await suspension model: **OPTION (c) stackful coroutines** (per `plano-s10-await-opcao-c-crumbs.md`, supersedes D2 "thread-per-await").
- **0117 D1-DI** — DI Part B (arena-lifetime service binding) — **active, not deferred** (s7-di-removal branch archived).
- **0118 STD-ENC** — encoding catalog (15 modules) — **verify-only** (all present and functional; no fresh impl).
- **0119 STD-FFI** — stdlib FFI missing pieces (only crypto exists; rand/openssl/net/db/odbc/rpc) — real work.
- **0120 SM-V1** — visibility enforcement activation — gate + triage (mechanism already at `src/checker/check_modules.tks:83-107`).

## New Crumbs (0121–0122)

**Recovered from undrained branches (2026-08-19 owner "precisa"):**
- **0121 ERR-FACTORY** — `error::new(msg)` / `error::join(a,b)` factory builtins. M2, recovery from `origin/cargo/0.3.1.0-error-factory`.
- **0122 S16-SYSCALL-PORTABLE** — gate raw-syscall C helpers to `__linux__` + portable `-ENOSYS` stubs. M2, recovery from `origin/feat/s16-syscall-portable`. C-route host portability, not native backend.

## IO/Panic cluster → Teko (pre-F9, arquiteto 2026-08-26)

Sequência ordenada, cada `[fixpoint]` / `fixpoint-rebuild`, reseed pelo implementer ao fim. Design em
`docs/design/io-panic-cluster-expurgo-0.3.1.md`. Nenhum bloqueado; independentes entre si.

- `0132-IO-R1` — residuais: `flush` no-op Teko, `exit_status` inline `& 0xFF`, remover dead-routing FS.
- `0133-IO-R2` — stdin em Teko sobre syscall (`read_line`/`stdin_eof`/`read_stdin`/`read_stdin_n`); frio, valida pós-F9.
- `0134-IO-R3` — família panic/trap emitida → `teko::runtime::panic_*` (reusa sink `panic`); quente, mede pico flat.

## Deferred (post-native-fixpoint-green)

**Not active crumbs; recorded for reference:**
- `teko doc` (`origin/cargo/0.3.1.0-devtools-doc`) — deferred until native fixpoint green
- `teko lint` (`origin/cargo/0.3.1.0-devtools-lint`) — deferred until native fixpoint green

## Counts by Status

| status | count |
|---|---|
| done | 1 (0001) |
| landed | 7 (0007, 0018, 0020, 0021–0023, 0025) |
| verify-only | 13 (0009–0010, 0019, 0026–0029, 0031–0033, 0091, 0118, 0120) |
| verify-and-wire | 2 (0115, 0116) |
| partial | 0 |
| in-progress | 1 (0024) |
| on-branch | 2 (0121, 0122) |
| needs-impl | 20 (0002–0005, 0008, 0030, 0108–0112, 0117, 0119, 0124, 0125) |
| planned:verify-at-exec | 73 (0034–0090, 0092–0107) |
| gated:D52 | 1 (0006) |
| re-seq:tail-§16 (D106, write-only native) | 5 (0113, 0114, 0126, 0127, 0128) |
| gated:owner | 2 (0011, 0012) |
| gated:FORK-ABERTO-1 | 1 (0123) |
| deferred | 0 (doc/lint tracked separately) |
| **TOTAL** | **128** |

---

## Wave 0.3.1-M5 — #embed/VFS + prelúdio-VFS + tipos-base + provenance + intrínsecos + sweep de região (arquiteto 2026-08-27)

Fiado em `docs/design/embed-vfs-sweep-integration-0.3.1.md` (superfícies exatas + resolução Tier-A +
ancoragem de path + reseed/ratchet). Rulings D130-D135. Ordem por dependência + economia de reseed.
**Fases A/B = ADITIVAS (piso NÃO-CRESCER); C/D = REDUÇÃO (ratchet BAIXAR estrito, D68).**

| seq | crumb | gate | 1-linha |
|---|---|---|---|
| 0162 | EMB-C0 | [dry] | scaffold `teko::embed` (EmbedCompress/ReadableFS/FileSystem-classe/RoFile/files()), inert |
| 0163 | EMB-C1 | [dry] | parser `EmbedDecl` + `parse_embed` (carrega src-path pra âncora file-relative) |
| 0164 | EMB-C2 | [dry] | `resolve_embed_path`: nu=file-relative / `/`=project-root, escape/drive/conflito, chave `<proj>::/` |
| 0165 | EMB-C3 | [fixpoint] | read-seam `read_file_bytes` (maintained-C) + compress build-time + level-range |
| 0166 | EMB-C4 | [RITUAL] | const-mat Tier-A (4 heaps) + `files()` vista-fina + `deflate`/`inflate` pub→exp — reseed |
| 0167 | EMB-C5 | [RITUAL] | `FileStream` mem-cursor (`of_slice`) + `get`/`exists`/`list`→RoFile + conforma ReadableFS — 🔑 SEED-BUMP |
| 0168 | PRE-C1 | [fixpoint] | embarca o prelúdio-pequeno (~13 runtime/sys/abi/assert) no VFS (Deflate) |
| 0169 | BT-C1 | [RITUAL] | consolida tipos-base reservados no prelúdio, injeta em TODO artefato incl. Package — reseed |
| 0170 | PRE-C2 | [RITUAL] | `inject_runtime_prelude` lê do VFS (memória), remove o disk-walk — binário self-contained — reseed |
| 0171 | PV-C1 | [fixpoint] | provenance Base-vs-User + gate redefinição de nome-reservado |
| 0172 | INTR-C1 | [dry] | censo 3-naturezas + primitiva raw-syscall (chão) + confirma wrap/unwrap landado |
| 0173 | INTR-C2 | [fixpoint] | expurga natureza-1 (`tk_*` dep-C) → superfície Teko (ratchet↓) |
| 0174 | INTR-C3 | [RITUAL] | natureza-2 (C-inline magia) → primitivas de superfície; só resta o raw `syscall` (ratchet↓) |
| 0155 | MEM-S1 | scaffold | fixtures `mem_*` no scratchpad (authoring ANTES de W1; pass pós-flip) |
| 0156 | MEM-W1 | [RITUAL] | elisão `slots==0` (repassa a região-param do pai) |
| 0157 | MEM-W2 | [RITUAL] | presize por `region_slots`; REMOVE `#arena_size`/`#arena_depth` |
| 0158 | MEM-W3 | [RITUAL] | scope-residence + seletor N-níveis + array-fixo de filhas (PARANOID) |
| 0159 | MEM-W4 | [RITUAL] | move-on-return via região-param; aposenta `ret_dest` ambiente (PARANOID) |
| 0160 | MEM-W5 | [RITUAL] | objeto dono-da-arena no fat pointer |
| 0161 | MEM-W6 | [RITUAL] | root nasce no `_start`→`main`; remove ambiente; reball (uso puro de wrap/unwrap) — RESEED-FINAL |

**Pontos de reseed:** 0166, 0167(+seed-bump), 0169, 0170, 0174, 0161(RESEED-FINAL). **Fork genuíno
remanescente:** nenhum (Tier-A resolvido §3; única escolha veto-open = nome `FILES` const-computado vs
accessor `files()`, cosmética). Drifts achados: §7 do anexo.

## Wave 0.3.1-M5 — fold-and-prune da AST por passo (D153, arquiteto 2026-08-28) — `fold-and-prune-por-passo-0.3.1.md`

Vem DEPOIS do sweep de superfície (W1-W6) + migração arena-tipo (0184). É o maior lever pós-superfície
pra aproximar do desejo `<1 GB` (D153; a régua é chegar o mais perto possível). Dois levers: jardinagem
por-fase (project + drop de região via `Region.child_sized`/`drop_subtree`) + drena-por-unidade (drop do
corpo pós-emit). Zero C (D148); reusa a arena-tipo.

| seq | crumb-id | gate | o que faz |
|---|---|---|---|
| 0185 | FP-0 | [dry] | instrumenta retenção por-fase (árvore vs scaffold + fatia `line/col`) — baseline |
| 0186 | FP-1 | [RITUAL] | `.tsym` opção A: `nid` na AST + tabela lateral; codegen resolve no emit → `teko.c` byte-idêntico |
| 0187 | FP-2 | [RITUAL] | `Region.enter/leave` + `project_program`/`intern_str_into` como transformação-IDENTIDADE (rede R2, NÃO dropa) |
| 0188 | FP-3 | [RITUAL] | `garden_phase` + drop pós-consteval (TypeTable + região do checker) — MAIOR queda |
| 0189 | FP-4 | [RITUAL] | drop na fronteira mono (parse/checker-scaffolding antes de mono) |
| 0190 | FP-5 | [fixpoint] | poda de campo confirmada (`doc`/`type_constraints`) — medir zero-leitor antes |
| 0191 | FP-6 | [RITUAL] | drena-por-unidade: codegen por-namespace + drop do CORPO pós-emit (byte-preservante) |
| 0192 | FP-7 | [RITUAL] | (opcional) `.tsym` opção B: emite `nid`, resolve em runtime — encolhe `teko.c`/binário |

**Pontos de reseed:** 0186, 0187, 0188, 0189, 0190(se podar), 0191, (0192). **Fork genuíno:** um só — o
bracket usa `Region.enter/leave` AMBIENTE (não threada região-param nas fases inteiras); recomendação
law-first = passa (é o mesmo mecanismo do `open/close_native_region`, o veto D130 mira o thread-local de
RUNTIME). HALT só se o dono exigir região-param nas fases (escala Eixo-C).
