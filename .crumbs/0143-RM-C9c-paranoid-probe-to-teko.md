---
seq: 0143
crumb-id: RM-C9c
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [RM-C9b]
sources:
  - "docs/design/arena-terminal-zero-libc-0.3.1.md:52-90"
  - "src/runtime/arena.tks:5,710-728"
  - "src/runtime/teko_rt.c:2038-2045"
---

# 0143 · RM-C9c — transcribe the `TEKO_MEM_PARANOID` probe to Teko

> Replace the last arena extern — `tk_arena_paranoid` — with a one-time, allocation-free raw envp
> scan, taking the array/arena path to ZERO `from "teko_rt"` on the free side.

## Goal

`arena.tks:5` binds `tk_arena_paranoid` (C `getenv("TEKO_MEM_PARANOID")` cached, `teko_rt.c:2038`),
read on the free path (`ar_free_block`, `arena.tks:719-728`) to decide poison-vs-park. envp is already
captured in Teko (`CTRL_ENVIRON` slot, `arena.tks:786-792`; `teko::env::env_snapshot`). This crumb
replaces the extern with a lazy raw byte-scan of the captured envp for `TEKO_MEM_PARANOID`, cached in a
`global var` / CTRL slot. The scan MUST stay allocation-free (§1.2 arena-core dialect forbids a `str`
on the arena path) — so it is a raw `load`/compare loop over the envp `char**`, NEVER `teko::env::var`
(which returns `str | error` and allocates). Removes the 3rd and final arena extern.

## Where

- `src/runtime/arena.tks:5` — `extern fn tk_arena_paranoid … from "teko_rt"` — REMOVE.
- `src/runtime/arena.tks` — NEW `#thread_local global var ar_paranoid_cache: i64 = -1` (or a spare CTRL
  slot; `-1` = unprobed) + NEW private `ar_paranoid(): u64` that lazily probes and caches.
- `src/runtime/arena.tks:722` — `ar_free_block` — call the new `ar_paranoid()` in place of
  `tk_arena_paranoid()`.
- `src/runtime/teko_rt.c:2038-2045` — `tk_arena_paranoid` — dead after the flip; REMOVED in 0146.

## How

1. Cache word (unprobed sentinel `-1`):

```teko
#thread_local global var ar_paranoid_cache: i64 = 0 - 1
```

2. Lazy, allocation-free probe over the captured envp. `environ_slot()` (`arena.tks:786`) holds the
   envp base (a `char **`, NUL-terminated by a 0 entry); each entry is a C `NAME=VALUE\0` string.
   Scan for the key `TEKO_MEM_PARANOID` by raw byte compare; a present, non-empty value → 1, else 0.
   No `str` is built — only `ar_load` of pointers/bytes and integer compares:

```teko
fn ar_env_match(entry: u64): bool { ... }   // raw NAME= compare against the 17-byte key, no alloc
fn ar_paranoid(): u64 {
    if ar_paranoid_cache >= 0 { return ar_paranoid_cache to u64 }
    var envp = environ_slot()
    var result: u64 = 0
    if envp != 0 {
        var i: u64 = 0
        loop {
            var entry = ar_load(envp + i * WORD)
            if entry == 0 { break }
            if ar_env_match(entry) { result = 1; break }
            i++
        }
    }
    ar_paranoid_cache = result to i64
    result
}
```

3. Repoint `ar_free_block` to `ar_paranoid()`; delete the extern at `arena.tks:5`.
4. Reseed iteratively to `gen2==gen3`.

Note: if `environ_slot()` is 0 at first free (envp not yet captured), the probe returns 0 (paranoid
off) — the exact behavior `getenv` returning NULL gave. The `MEM_PARANOID` ladder gate re-runs with
the env set, exercising the `==1` arm.

## Rulings & laws

- **Teko-only + expurgo:** `tk_arena_paranoid` removed from C in 0146, not patched (D90).
- **§16 sem atalhos:** a REAL Teko probe (raw envp scan), never a degraded stub; allocation-free per
  the arena-core dialect (`plano-s16-arena-mmap.md:83-100`).
- **NO PUSHES:** the scan builds nothing; a fixed key compared byte-by-byte.
- **W15:** `ar_paranoid`/`ar_env_match` are private → no doc.
- **Safety:** NEVER `teko test .`; subshell `ulimit -v 4718592`; reseed ONLY at this [RITUAL];
  `gen2==gen3`; leave gen2/gen3 in scratchpad.

## Fixtures

none — the fixpoint self-build (paranoid off) exercises the `==0` arm; the ladder's `MEM_PARANOID`
gen2 run (env set) exercises the `==1` arm. Both are self-build paths, not fixtures.

## Gate

`[RITUAL]` — full native ladder (incl. `MEM_PARANOID` exit 0) + expurgo reseed. "Green" = the paranoid
oracle runs in Teko, `arena.tks` has ZERO `from "teko_rt"`, and `gen2==gen3` byte-identical. Measure
dry-build `peak`: NÃO-CRESCER (D68). Reseed-class: `expurgo`.

## Deps

`RM-C9b`.

## Done when

`arena.tks` binds no `from "teko_rt"`, the paranoid probe runs as an allocation-free Teko envp scan,
`MEM_PARANOID` exit 0 holds, and `gen2==gen3`.
