# Runtime de-C port — per-symbol CENSUS + bucket classification (camada-2, Stage 1)

> Companion to `migracao-runtime-c-para-teko-0.3.1.md` (the ordered roadmap). This file is the
> MEASURED inventory that roadmap asks for: every `tk_*` / `teko__assert__*` symbol the linux
> native path still links from C, classified into the three owner-named buckets. Produced on
> branch `runtime/de-c-port`. Nothing here bumps or opens a PR.

## The C surface still linked into every native binary

`link_object_elf_direct` (`src/build/project.tks:2097`) links, in addition to the own-backend
object, exactly two compiled-by-clang seeds plus one header:

| C unit | lines | role |
|--------|------:|------|
| `src/runtime/teko_rt.c` | 4930 | the execution runtime — 328 extracted `tk_*` definitions |
| `src/runtime/teko_rt.h` | 1570 | the runtime's public prototypes + `TK_RT_LIST` macro + inline shims |
| `src/assert/assert.c`   | 256  | the assert seed — `teko__assert__*` (WEAK) + `static` helpers |
| `src/assert/assert.h`   | 71   | assert-seed prototypes |
| `src/win32_compat.h`    | —    | Windows-only POSIX shims (chdir/mkdir/getcwd/setenv/dirent/spawnvp) |

The compile+link is wired through `build_cc_argv` (`:1103`, transitional `cc`-as-linker),
`compile_c_object_argv` (`:2062`, the `cc -c` of each seed) and `link_object_elf_direct`
(`:2097`, the direct `ld.lld` line). De-C means all three stop referencing these C units.

## Method

The census was produced by extracting each top-level function body from `teko_rt.c`, stripping
string literals and comments, and scanning the residue for libc / syscall / platform symbols by
word boundary. A function with NO such reference is bucket (a); one that reaches only
libc/syscall data primitives is (b); one that reaches signals, backtrace/execinfo, `setjmp`/
`longjmp`, `abort`, `clock_gettime`/`QueryPerformanceCounter`, or Win32 SEH is (c). Boundary
notes below correct the few cases where a body DELEGATES its libc use to a called helper (the
allocation substrate is the important one).

## Counts

| bucket | count | meaning |
|--------|------:|---------|
| (a) PURE-COMPUTE | 183 | no libc/syscall in the body — portable to Teko directly |
| (b) LIBC-DEPENDENT | 126 | snprintf/float-format, alloc, memcpy/memcmp, fwrite, file/proc syscalls |
| (c) PLATFORM-GLUE | 19 | signals, backtrace, setjmp/longjmp, abort, monotonic clock, Win32 SEH |
| total (teko_rt.c) | 328 | |

`assert.c` adds 24 `teko__assert__*` (all WEAK) + 4 `static` helpers; its only libc is `snprintf`
(24×), `memcmp` (4×), `free` (1×), `exit` (2×) — a pure bucket-(b) unit modulo the exit path.

## Bucket (b) — primitive frequency (the port's real work surface)

Sorted by how many bucket-(b) functions reach each primitive:

```
29 memcpy   22 malloc   17 snprintf  13 free     9 memcmp    8 fwrite
 8 strlen    7 fopen     7 fclose     7 stdout    6 fread     6 fputs
 6 getenv    6 fprintf   5 fflush     5 close     4 open      4 fputc
 3 realloc   3 memset    3 stdin      3 execvp    3 fork      3 waitpid
 2 calloc    2 write     2 sysconf    2 chdir     2 dup2      2 fcntl
 1 each: strtod strtol strchr strstr strcmp getcwd getpid kill pipe
        poll opendir readdir closedir mkdir remove rename setenv errno read
```

### (b) sub-layers, easiest first

1. **memory ops** — `memcpy` (29), `memcmp` (9), `memset` (3): pure byte loops in Teko, no
   syscall. The single largest lever and the safest. `tk_str_eq`/`tk_str_cmp`/`tk_str_concat`/
   `tk_str_of_bytes`/`tk_slice_eq_bytes`/`tk_char_eq`/`tk_intern_find`/`tk_sort_names` etc.
2. **integer formatting** — `tk_i64_to_str`/`tk_u64_to_str` reach only `malloc`+`memcpy`; the
   digit loop is already pure (the `*_len` twins are bucket-(a)). Port once alloc exists.
3. **allocation substrate (THE BOTTOM)** — `tk_chunk_alloc` (`teko_rt.c:1261`) is the sole page
   source: `posix_memalign` (POSIX) / `_aligned_malloc` (Win32). `tk_region_alloc` bump-allocates
   from chunks; `tk_alloc` bumps from the current region; `tk_arena_push/pop/commit`,
   `tk_region_new*`, `tk_regions_free_all`, `tk_one_byte`, `tk_str_chars`, `tk_bytes_from_ptr`,
   `tk_cstr*`, `tk_intern_*` all rest on it. **`posix_memalign` was MISSED by the scanner** (not
   in the `malloc` word set) so `tk_chunk_alloc` and several arena fns land in (a)/(b)
   inconsistently — they are all bucket (b), gated on the allocator wall below.
4. **float format / dtoa** — `tk_ftoa`/`tk_f64_g17`/`tk_fmt_{d,e,f,g,p,x,n}` reach `snprintf`/
   `printf` for the actual `%g`/`%f`/`%e` conversion. Needs a Teko dtoa (Grisu/Ryū) or a `snprintf`
   syscall bridge. The `*_len` sizing twins are already bucket-(a).
5. **buffered stdio** — `tk_print`/`tk_println`/`tk_flush_out`/`tk_chan_*`/`tk_test_*` reach
   `fwrite`/`fputs`/`fflush`/`stdout`. Bottom is `write(2)`.
6. **file + process syscalls** — `tk_rt_read_file`/`tk_rt_write_file`/`tk_rt_run`/`tk_rt_pipe`/
   `tk_rt_spawn_*` reach `open`/`read`/`close`/`fork`/`execvp`/`waitpid`/`pipe`/`dup2`/`fcntl`/
   `chdir`/`mkdir`/`getcwd`/`opendir`/`readdir`. The heaviest syscall surface; last of (b).

## Bucket (c) — platform-glue (19, last)

```
tk_backtrace                 backtrace, backtrace_symbols
tk_canary_check_head         abort, backtrace
tk_canary_check_region       abort, backtrace
tk_cov_enter                 abort
tk_cov_mark                  abort
tk_covb_add                  abort
tk_line_rehash               abort
tk_panic_str                 abort            (the fail-loud bottom)
tk_redzone_verify            abort, backtrace
tk_rt_crash_handler          raise, signal
tk_rt_install_crash_handler  signal
tk_rt_install_stop_handlers  signal
tk_rt_stop_handler           raise, signal
tk_rt_monotonic_ns           clock_gettime / QueryPerformanceCounter
tk_wall_now_ns               clock_gettime
tk_test_capture_leave        longjmp
tk_test_run                  setjmp
tk_slice_push_fo             backtrace        (grow-fail diagnostic)
tk_slice_push_r              backtrace
```

`win32_compat.h` is entirely bucket (c) (Windows lane): `_chdir`/`_mkdir`/`_getcwd`/`setenv`
shim/`dirent` shim/`tk_win32_spawnvp`, plus the SEH `__try`/`__except` group
(`tk_win_seh_*`). It dies with the rest of the runtime port on the Windows leg.

## Bucket (a) — pure-compute (183, port first)

The full list (scanner output; the allocation-substrate entries flagged in (b).3 belong to (b)):

```
tk_arena_commit tk_arena_pop tk_arena_push tk_as_ptr tk_assert_scenario_prefix
tk_assert_scenario_prefix_len tk_branch_id tk_bytes_of_str tk_bytes_of_str_len tk_canary_forget
tk_canary_hash tk_canary_record tk_char_at tk_char_to_u32 tk_chunk_try tk_cov_branch
tk_cov_branch_at tk_cov_branch_hit tk_cov_branch_reset tk_cov_branches_on tk_cov_distinct
tk_cov_is_marked tk_cov_leave tk_cov_line tk_cov_line_at tk_cov_line_hit tk_cov_line_reset
tk_cov_lines_on tk_cov_reset tk_exit tk_exit_status tk_f64_g17_len tk_ffi_sres_into_out
tk_ffi_ures_into_out tk_fmt_b tk_fmt_b_len tk_fmt_d_len tk_fmt_dyn_f64 tk_fmt_dyn_f64_len
tk_fmt_dyn_i64 tk_fmt_dyn_i64_len tk_fmt_dyn_u64 tk_fmt_dyn_u64_len tk_fmt_e_len tk_fmt_f_len
tk_fmt_g_len tk_fmt_n_f_len tk_fmt_n_i_len tk_fmt_p_len tk_fmt_x_lower_len tk_fmt_x_upper_len
tk_free_purge tk_free_take tk_ftoa_len tk_i64_to_str_len tk_intern_get tk_is_alpha tk_is_digit
tk_is_space tk_jdn_to_ymd tk_journal_note tk_line_id tk_line_insert_packed tk_line_insert_raw
tk_names_capacity tk_names_cell_status tk_names_forget tk_names_generation_of tk_names_live_count
tk_names_lookup tk_names_slot_at tk_names_slot_of tk_names_status tk_names_take tk_obs_add
tk_obs_mstr_note tk_one_byte_len tk_panic tk_panic_div0 tk_peak_rss tk_push_slot
tk_region_ancestor_reaches tk_region_current tk_region_current_u tk_region_drop_u tk_region_enter
tk_region_enter_u tk_region_gen_next tk_region_leave tk_region_lookup tk_region_new tk_region_new_u
tk_region_program tk_region_root tk_region_root_u tk_rt_arch_len tk_rt_chdir_ok
tk_rt_date_day_of_month tk_rt_date_from_days tk_rt_date_month tk_rt_date_year tk_rt_fd_take_byte
tk_rt_float_parse_bits tk_rt_getcwd_ok tk_rt_getenv_ok tk_rt_last_index_of_ok tk_rt_list_dir_ok
tk_rt_mkdir_ok tk_rt_next_nul_token tk_rt_os_len tk_rt_pack_pipe tk_rt_pipe_read_fd
tk_rt_pipe_write_fd tk_rt_read_file_ok tk_rt_read_line_len tk_rt_read_nul_vector tk_rt_read_stdin_len
tk_rt_remove_file_ok tk_rt_secure_bytes tk_rt_setenv_ok tk_rt_stdin_eof tk_rt_str_from_utf8_ok
tk_rt_token_fd tk_rt_token_u64 tk_rt_version_len tk_rt_wait_status_code tk_rt_wall_days
tk_rt_wall_ns_of_day tk_rt_wall_offset_minutes tk_rt_win_peek_ready tk_rt_write_file_bytes_ok
tk_rt_write_file_ok tk_scope_byte_ok tk_set_args tk_slice_char_eq tk_slice_f32_eq tk_slice_f64_eq
tk_slice_push tk_slice_str_eq tk_slice_with_cap tk_slice_with_cap_r tk_spin_lock tk_spin_unlock
tk_str_cmp tk_str_concat_len tk_str_hash tk_str_len tk_str_len_chars tk_str_of_bytes_len
tk_str_slice tk_str_slice_chars_len tk_str_slice_from tk_str_slice_from_len tk_str_slice_len
tk_str_slice_to tk_str_slice_to_len tk_task_current tk_task_reset tk_task_reset_transient
tk_termination_hook_once tk_test_any_failed tk_test_capture_last_code tk_test_capture_probe
tk_test_capturing tk_test_end tk_test_outcome_at tk_test_probe_body tk_test_probe_returns
tk_test_report tk_test_scope_len tk_test_shard_take tk_to_lower tk_to_upper tk_u64_to_str_len
tk_win_seh_is_fatal
```

Note: several `*_ok` / `*_len` / date / `region_*` entries are pure BECAUSE the syscall or
allocation is done by a sibling non-`_ok` function and this one only classifies/measures the
result — they port trivially but only become USEFUL once their sibling's (b) primitive is ported.

## The Stage-2 WALL (measured — where the port stops without new compiler support)

The bucket split is NOT the wall. Bucket (a) is portable Teko today. The wall is the LINK
mechanism, because a ported function must present the SAME symbol the codegen and the runtime's
own callers reference:

1. **Bare C-ABI symbol export on the OWN-native backend is MISSING.** The native mangler
   (`src/lir/lower.tks:885 mangle_fn_symbol`) ALWAYS emits `teko_<ns>__<name>` (or, under `flat`,
   `<ns>__<name>` — `emit_static_lib`/KP16). There is no path to emit a bare `tk_str_eq` with the
   C ABI. The reverse-FFI `abi="c"` export (`cg_c_export_symbol`, `src/codegen/codegen.tks:12085`)
   exists ONLY on the C backend and emits C wrapper TEXT, not an ELF symbol. So a Teko-compiled
   object cannot today provide the `tk_*` symbols `teko_rt.c` provides. Closing this needs one of:
   - a native `@export("tk_str_eq")` / `abi="c"` attribute honoured by `mangle_fn_symbol` +
     `emit_elf`, OR
   - rerouting every codegen call site (`native_builtin_symbol`/`call_symbol`/`builtin_io_symbol`
     in `lower.tks`, and the `tk_*` extern bottoms) to the mangled Teko symbols
     (`teko_runtime__*`), then linking a Teko runtime object.
2. **No build step compiles a Teko runtime object into a program's link.** `link_object_elf_direct`
   only knows the two C seeds. De-C requires compiling `src/runtime/*.tks` (+ `assert.tks`) through
   the own pipeline into a native `.o` and adding it to the link — for EVERY program, not just the
   compiler's self-build.
3. **The allocator bottom needs a raw syscall.** `tk_chunk_alloc` rests on `posix_memalign`/
   `_aligned_malloc`; the pure-Teko replacement bottoms at `mmap(2)` (anonymous, page-aligned).
   Teko has no `mmap` intrinsic/extern surface today. This is the one genuine SYSCALL-support gap
   in bucket (b): everything allocating (i.e. every string/list op) is blocked on it until an
   `mmap`/`munmap` bottom exists.
4. **dtoa** — float formatting bottoms at `snprintf %g`. Either a Teko dtoa or a `snprintf` bridge;
   independent of (3).
5. **stdio/write** bottom at `write(2)`; file/proc at `open/read/close/fork/execvp/waitpid/pipe/
   dup2/fcntl` — a syscall surface Teko must expose (or a thin `tk_sys_*` object) before bucket
   (b).5/(b).6 and bucket (c) can move.

## The clean FIRST increment (assert seed, self-declared programs)

`assert.c`'s `teko__assert__*` are all `TK_ASSERT_WEAK`; its `tk_assert_*` helpers are `static`.
The compiler's OWN corpus declares `teko::assert` (`src/assert/assert.tks`), so
`program_declares_assert_seed(prog)` is true for the self-build and `assert.c` is compiled WITHOUT
`TK_ASSERT_SEED_STRONG` — its symbols are weak and fully overridden by the Teko-compiled
`teko_assert__*`. For such a program `assert.o` contributes nothing to the binary. Therefore the
smallest fixpoint-preserving de-C step is: **when `program_declares_assert_seed(prog)`, skip
compiling and linking `assert.c` entirely** (`compile_c_object_argv` + the `asobj` push in
`link_object_elf_direct`, and the transitional `build_cc_argv:1162`). Observable behaviour is
identical (the weak symbols were already shadowed). This removes the C assert seed from the
compiler's own native binary with no new compiler feature — and is the template for how a
Teko-compiled runtime object, once (1)/(2) above exist, replaces the strong seed for arbitrary
programs.
