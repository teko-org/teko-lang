---
seq: 0147
crumb-id: F6-SWEEP
milestone: M16
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [F6-WIN-P2, F6-PID]
sources:
  - "docs/design/f6-process-zerolibc-windows.md:80-90"
  - "DECISION_LOG.md:1140-1170"   # D125 pre-sweep: delete cadaver C, no caller
  - "DECISION_LOG.md:1002-1031"   # D104/D107 F6 landed
---

# 0147 · F6-SWEEP — pre-sweep the now-dead process C (D125)

> Delete the F6 C cadavers that 0144-0146 orphaned: the `tk_rt_*` process/pipe/pid bodies and the `tk_win32_*` spawn helpers, with `tk_rt_close_fd` EXCLUDED (journal still calls it).

## Goal

D125 incremental pre-sweep (mini-F9): once 0144-0146 land and reseed, the Windows process
externs and pid externs have no `.tks` caller, so their C bodies are cadavers. Delete them
from `teko_rt.c`/`win32_compat.h` and remove the now-unused `#include`s they alone pulled.
This is **cadaver deletion, not migration-by-C-edit** — D125 explicitly authorizes it
(D90 governs migration, which already happened in Teko). Proves deadness: the tree links
and the CI legs stay green without the removed code. Removing C bodies does not change the
emitted `teko.c` (separate file), but the build must relink → this carries a full ritual.

## Where

`src/runtime/teko_rt.c` — delete (verify zero live caller first, grep `.tks` tree):
- `tk_rt_spawn_redirected` (`:3929`) + POSIX helpers it alone uses
  (`tk_rt_next_nul_token`/`tk_rt_token_u64`/`tk_rt_read_nul_vector`/`tk_rt_token_fd`/
  `tk_rt_open_redirect`/`tk_rt_child_lift`/`tk_rt_child_drop`/`tk_rt_child_bind_all`/
  `tk_rt_parent_release` — confirm none is shared with a still-live entry).
- `tk_rt_wait_one` (`:3982`), `tk_rt_pipe` (`:4013`) + `tk_rt_pack_pipe`/
  `tk_rt_pipe_read_fd`/`tk_rt_pipe_write_fd`, `tk_rt_win_peek_ready` (`:4059`),
  `tk_rt_fd_wait_readable` (`:4070`), `tk_rt_fd_fill` (`:4109`), `tk_rt_fd_take_byte`
  (`:4126`).
- `tk_rt_pid` (`:5026`), `tk_rt_pid_alive` (`:5034`).
- the per-task `rt_fd_stage[]`/`rt_fd_staged`/`rt_fd_taken` members (`:1078`) IF no other
  entry reads them.

`src/win32_compat.h` — delete `tk_win32_spawn_redirected` (`:298`), `tk_win32_wait_one`
(`:328`), `tk_win32_redirect_handle` (`:223`), `tk_win32_redirect_handle_of_fd` (`:241`),
`tk_win32_child_handle` (`:254`), `tk_win32_inheritable_std` (`:211`),
`tk_win32_build_envblock` (`:264`), `tk_win32_join_cmdline` (`:186`) — each iff no live
caller remains (`tk_win32_spawnvp` for `tk_rt_run`/`run_quiet` may still use
`tk_win32_join_cmdline`? — VERIFY: the landed Teko `run` arm replaced `tk_rt_run`, so if
`tk_rt_run`/`tk_rt_run_quiet` are ALSO dead, sweep them and their helpers too).

**EXCLUDED from this sweep:** `tk_rt_close_fd` (`:4045`) — `journal.tks:418 close_fd_rt`
still calls it. Its death belongs to journal's F4/F8 ficha, NOT F6.

## How

1. **Grep-prove deadness first** (the D125 double-value): for each symbol, `Grep` the
   `.tks` tree for its `from "teko_rt"` extern AND the reseeded `bootstrap/teko.c` for a
   call — both must be empty (except `tk_rt_close_fd`). List anything still referenced =
   do NOT delete it, report it.
2. Delete the confirmed cadavers + any helper used ONLY by a deleted body.
3. Remove `#include` lines that only those bodies needed (`<sys/wait.h>` waitpid,
   `<poll.h>` poll, the `_pipe`/`_get_osfhandle` `<io.h>` comment refs — trim only the
   includes with zero remaining user; conservative: keep any include still referenced).
4. Also sweep the `tk_rt_run`/`tk_rt_run_quiet` bodies + `tk_win32_spawnvp` IF the grep
   shows the landed Teko `run` arm (D107) left them uncalled — same proof.
5. Rebuild the full native ladder (all six CI legs) to prove link + runtime green with the
   cadavers gone.

## Rulings & laws

- **D125:** pre-sweep of caller-less C is authorized (most-recent-wins over the frozen-C
  reading); it is F9-incremental, not migration.
- **D90:** untouched for MIGRATION (already done in Teko); this is deletion of dead bodies.
- **D124 gate feed:** removing these cadavers makes the final F9 certification grep see only
  the real live set.
- **`tk_rt_close_fd` exclusion is load-bearing:** deleting it while journal calls it breaks
  the link — the deadness proof (step 1) catches this; it stays.
- **Fork protocol:** no fork — pure dead-code removal behind a grep proof.

## Fixtures

`none — the six-leg CI ladder is the deadness oracle` (if it links and runs green without
the removed code, the code was dead — D125's second value). No `.tkr` authored.

## Gate

`[RITUAL]`: full native ladder, all legs green (Linux/mac/Windows × C/native as they close),
fixpoint `gen2==gen3` unchanged from 0145/0146's reseed (C-body deletion does not alter the
emitted `teko.c`; the ritual proves LINK + runtime). No new reseed content — the reseed
already happened in 0144-0146; this ritual re-validates the link surface.

## Deps

F6-WIN-P2, F6-PID

## Done when

The F6 process/pipe/pid C cadavers are gone from `teko_rt.c`/`win32_compat.h`
(`tk_rt_close_fd` excepted), the includes are trimmed, every CI leg links and runs green,
and a grep of the reseeded `teko.c` shows zero live reference to any removed symbol.
