# `B1-args` — the stack-arg slot, and the `.s` escape hatch (owner, 2026-07-28)

## Why only Windows fails

    isel x86-64: B1-args — an integer call argument past the ABI's argument-register
    window needs the stack-arg slot (0.3.1)

Not instability, and NOT the harvested seed — the failure predates it (`aa5e380`, `f74ce40`; the
seed landed at `0ad247c`). It is an ABI window difference: **Win64 has four integer argument
registers (RCX/RDX/R8/R9), System V has six (RDI/RSI/RDX/RCX/R8/R9)**. The same call fits in
registers on Linux and macOS and overflows on Windows.

Owner ruling: *"Neste caso, tem que corrigir."*

## The fix — three connected places, not one

`src/backend/isel_x86_64.tks` holds TWO stops, and both must land together: a caller that pushes a
fifth argument is useless if the callee cannot read it.

1. **`pin_args_x86` (caller).** Args past `arg_reg(abi, GPR, n)` are stored to the outgoing area
   with `store_x86` into `MMem { base = RSP; offset = … }`. The Nth overflow argument sits at
   `shadow + (n - window) * 8` on Win64.
2. **`select_param_x86` (callee).** The mirror: a param past the window is LOADED from the caller's
   frame rather than moved out of a register.
3. **`compute_frame_layout_x86`.** It already reserves the shadow region; it must also reserve
   `max(outgoing overflow args)` across every call in the function, and keep the 16-byte alignment
   the ABI requires at the call site.

The machinery exists — `store_x86`, `load_x86`, `MMem`, `MFrameAddrX86`, and a frame layout that
already knows about shadow space. This is not new ground; it is careful ground.

**Why it was not done in the .31 wagon:** a wrong stack-arg lowering does not fail loudly, it
generates SILENTLY WRONG CODE, and only on Windows. That is the exact failure class this train has
been fighting all day (owner: *"se o erro é em runtime, ele pode não morar onde estamos olhando"*).
It needs a session with room to verify, not the tail of one.

## The `.s` idea — RAISED AND DROPPED, 2026-07-28

Recorded so nobody picks it up later mistaking it for live `.32` curriculum. The owner proposed
teaching the compiler to consume per-target `.s` files the way Go selects them by filename suffix
(`foo_amd64.s`, `foo_linux_amd64.s`), then dropped it the same day: *"Entendi, deixa pra lá o .s
então."*

It would not have helped here in any case, and that is worth keeping even though the idea is gone:
`B1-args` is not a routine anyone wants to hand-write — it is the lowering of an ORDINARY Teko call
that carries more arguments than the register window. No hand-written assembly teaches the
instruction selector to emit that call.

## The Windows lane pins it meanwhile

`.github/workflows/pr.yml`'s Windows self-test step holds a KNOWN-STOP envelope (owner ruling
2026-07-28: *"Ok KNOWN-STOP para os testes de WINDOWS"*). It lives in the LANE and not in
`own_native.tkr` because that fixture passes on every System V leg — a fixture cannot hold two
truths, but a lane can hold the one true of its own ABI. The verdict is delegated to
`scripts/known_stop_gate.sh`, which demands the suite fail, that the failure name the EXACT pinned
diagnostic ("an integer call argument past the ABI's argument-register window needs the stack-arg
slot") — not merely the shared `B1-args` family name, which `select_param_x86`'s parameter stop and
`pin_args_x86`'s variadic-call stop also carry (`scripts/known_stop_gate_test.sh` proves the
distinction by inversion) — that exactly one regression row failed, and that no unit test did. When
this fix lands, that first condition goes red and the envelope comes out.
