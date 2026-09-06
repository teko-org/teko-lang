---
seq: 0142
crumb-id: RM-C9b
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [RM-C9a]
sources:
  - "docs/design/arena-terminal-zero-libc-0.3.1.md:52-90"
  - "src/runtime/arena.tks:1-3"
  - "src/runtime/teko_rt.c:2019-2032"
---

# 0142 · RM-C9b — transcribe the arena CONTROL word to Teko

> Replace the last mutable process word the arena reaches into C for — `tk_g_arena_control` — with a
> Teko `#thread_local global var`, deleting two of the three arena externs.

## Goal

`arena.tks:1-3` binds `tk_arena_control_get`/`tk_arena_control_set` (C `_Thread_local uint64_t
tk_g_arena_control`, `teko_rt.c:2019`). It is the P2 seam: every task-local arena slot hangs off the
CONTROL block reached through this ONE word. With the `global var` surface landed (0141), the word
moves into Teko as `#thread_local global var ar_control_word: u64 = 0`, and `ar_control()` reads/writes
it directly. Byte-FAITHFUL: `_Thread_local` on the C leg preserves the exact per-task discipline (D1
spawn isolation, arena spec `:300`). Removes 2 of the 3 arena externs; the array/arena path is now one
extern (`tk_arena_paranoid`, 0143) from zero.

## Where

- `src/runtime/arena.tks:1-3` — the two `extern fn tk_arena_control_get/set … from "teko_rt"` — REMOVE.
- `src/runtime/arena.tks` — NEW `#thread_local global var ar_control_word: u64 = 0` (module scope).
- `src/runtime/arena.tks:291-302` — `ar_control()` — read `ar_control_word` in place of
  `tk_arena_control_get()`; write it in place of `tk_arena_control_set(control)`.
- `src/runtime/teko_rt.c:2019-2032` — `tk_g_arena_control` + `tk_arena_control_get/set` — become dead
  (no caller after the flip); REMOVED in 0146 (C7 two-legs), NOT here.

## How

1. Land the module word (thread-local, faithful to the C `_Thread_local`):

```teko
#thread_local global var ar_control_word: u64 = 0
```

2. Rewrite `ar_control()` to use it (no other line of `arena.tks` changes — the CONTROL block layout,
   META-POOL, and every `ar_ctrl_get/set` are unchanged; only the anchor word moves):

```teko
fn ar_control(): u64 {
    if ar_control_word != 0 { return ar_control_word }
    var slab = ar_meta_slab_new(AR_META_SLAB)
    var control = slab + AR_SLAB_HEADER
    ar_ctrl_set(control, CTRL_MAGIC, CTRL_MAGIC_VALUE)
    ar_ctrl_set(control, CTRL_META_SLAB, slab)
    ar_ctrl_set(control, CTRL_META_CURSOR, ar_align_up(control + CTRL_BYTES))
    ar_ctrl_set(control, CTRL_META_END, slab + AR_META_SLAB)
    ar_control_word = control
    control
}
```

3. Delete the two externs at `arena.tks:1-3`.
4. **Reseed ITERATIVELY** (message pass over the integrated tree BEFORE the single reseed) until
   `gen2==gen3` byte-identical. The self-build drives the word at scale (every alloc reaches
   `ar_control`), so a divergence surfaces as a fixpoint break, not a missed fixture.

## Rulings & laws

- **Teko-only + expurgo:** the anchor moves C→Teko; `tk_g_arena_control` is REMOVED from C in 0146, not
  patched (D90 — nada em teko_rt.c pro expurgo).
- **§16 se existe em C, existe em Teko + faithfulness:** `_Thread_local` on the C leg keeps semantics
  byte-identical; a plain global would regress spawn isolation (D1). The `#thread_local` attribute is
  load-bearing.
- **Fork protocol:** rests on 0141 = FORK-ABERTO-1 option B (law-first, D119/D125).
- **W15:** `ar_control` and the word are private → no doc.
- **Removals = clean expurgo, NO tombstone.**
- **Safety:** NEVER `teko test .`; subshell `ulimit -v 4718592`; reseed ONLY at this [RITUAL];
  `gen2==gen3`; E4-class harvest; leave gen2/gen3 in the worktree scratchpad for the drain.

## Fixtures

none — the fixpoint self-build exercises this. Every region enter/leave/alloc during a self-build
drives `ar_control_word`; a divergence is a `gen2!=gen3` break.

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed. "Green" = the CONTROL word runs from the
Teko `#thread_local global var`, `arena.tks` binds only `tk_arena_paranoid` from C, and `gen2==gen3`
byte-identical after harvest. Measure dry-build `peak`: expect NÃO-CRESCER (D68 expurgo floor).
Reseed-class: `expurgo`.

## Deps

`RM-C9a` (the `global var` surface).

## Done when

`arena.tks` holds the CONTROL anchor in a Teko `#thread_local global var`, the two
`tk_arena_control_*` externs are gone, and `gen2==gen3` with the dry-build peak not risen.
