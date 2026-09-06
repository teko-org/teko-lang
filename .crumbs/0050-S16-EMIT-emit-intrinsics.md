---
seq: 0050
crumb-id: S16-EMIT
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/plano-s16-expurgo-libc-completo.md:79-96"    # §1.1 emitted-C core
  - "docs/design/plano-s16-expurgo-libc-completo.md:205-213"  # §2 the NEW items (floor/round/ceil, memcpy, typedefs)
  - "docs/design/plano-s16-expurgo-libc-completo.md:224-259"  # §3 FASE1 + why-this-order
  - "docs/design/plano-s16-expurgo-libc-completo.md:366-393"  # §7 fixtures S4/S5/S6
  - "docs/design/plano-s16-expurgo-libc-completo.md:396-409"  # §8 ritual after F1
---

# 0050 · S16-EMIT — FASE1 emit intrinsics: floor/round/ceil + memcpy + own typedefs

> Make the codegen EMIT its own `floor`/`round`/`ceil` + `memcpy` intrinsics and its own fixed-width
> `typedef`s, so the emitted `teko.c` stops `#include`-ing `<math.h>`/`<string.h>`/`<stdint.h>`/
> `<stdbool.h>` — the cheapest §16 gate to prove (self-host, no OS touched).

## Goal

FASE1 of the §16 libc expurgo kills the C headers of the EMITTED image WITHOUT touching the OS — pure
codegen, self-host only. The emitted `teko.c` today `#include`s `<stdint.h>`/`<stdbool.h>` (types),
`<math.h>` (`floor`, 5× real fp), `<string.h>` (`memcpy`, 2× guarded), `<stdlib.h>` (`malloc`/`abort`).
This crumb removes the first four header dependencies by emitting: (a) fixed-width `typedef`s
(`typedef unsigned char uint8_t;` …) instead of `<stdint.h>`/`<stdbool.h>` (the Go/musl pattern, the
only route that makes `cc bootstrap/teko.c` compile without `-include`); (b) `floor`/`round`/`ceil` as
codegen intrinsics (soft/`__builtin_*` WITHOUT the header) mirroring the landed `f64_bits` bit
-reinterpret pattern; (c) `memcpy` as an inline loop / `__builtin_memcpy` intrinsic. Byte-mover for the
emitted `teko.c`? YES — the preamble changes (headers → typedefs; math/mem calls → intrinsics) → a real
emit delta → `fixpoint-rebuild` reseed. This is the self-host gate: the first ritual after SM-R1.

## Where

- `src/codegen/codegen.tks:8760-8764` — the emitted `#include <stdint.h>`/`<stdbool.h>`/`<stdlib.h>`/
  `<math.h>`/`<string.h>` block — replace the type/math/string includes with emitted typedefs +
  intrinsic helpers. (`<stdlib.h>` for `malloc`/`abort` is FASE2/FASE3 territory — not this crumb.)
- `src/codegen/codegen.tks` preamble slot — NEW: emit the fixed-width `typedef` block and the
  `floor`/`round`/`ceil`/`memcpy` `static inline` helpers, USE-GATED (emit only what the program uses),
  next to the `f64_bits`/syscall-helper emit precedent.
- `src/codegen/codegen.tks` `emit_call` dispatch ladder — the `floor`/`round`/`ceil`/`memcpy` builtin
  arms route to the emitted intrinsic symbols (no header).

## How

1. **Fixed-width typedefs instead of `<stdint.h>`/`<stdbool.h>`** (`§1.1` note (a), the recommended
   law-first route):

```teko
/**
 * cg_emit_fixed_width_typedefs — append the compiler's OWN fixed-width integer typedefs
 * (`typedef unsigned char uint8_t; typedef long long int64_t;` …) to the emitted C preamble, replacing
 * `#include <stdint.h>`/`<stdbool.h>`. The Go/musl pattern: zero header, the only route that lets `cc
 * bootstrap/teko.c` compile without `-include` of a C header (`plano-s16-expurgo §1.1`). The native leg
 * needs nothing (it already materializes `i64`/`byte`/`bool` as machine types).
 * @param buf  the preamble byte buffer
 * @return     `buf` with the typedef block appended
 * @since §16
 */
fn cg_emit_fixed_width_typedefs(buf: Cb): Cb
```

2. **`floor`/`round`/`ceil` intrinsics** (`§2` item 4): emit each as a `static inline` helper (soft
   implementation or `__builtin_floor`/`__builtin_round`/`__builtin_ceil` — which need NO `<math.h>`),
   USE-GATED like the syscall helpers, dispatched by the builtin last-segment in `emit_call`. This
   mirrors the `f64_bits` "intrínseco de codegen" precedent (a compiler-lowered reinterpret, never a
   header call).
3. **`memcpy` intrinsic** (`§2` item 5): emit an inline byte-copy loop or `__builtin_memcpy` helper
   (no `<string.h>`), USE-GATED. Only the `spawn_sites`-guarded emit needs it, so most images emit
   nothing.
4. **Use-gate everything.** Emit ONLY the intrinsics/typedefs the program actually references (a
   one-shot pre-scan like `cg_scan_syscall_arities`). The typedefs are always needed (every image uses
   `uint8_t`&cia); the math/mem helpers are emitted only when a `floor`/`round`/`ceil`/`memcpy` call is
   present. This keeps the reseed a clean fixpoint where possible.
5. **Prove header removal.** After the flip, grep the emitted `teko.c`: it must NOT contain
   `#include <math.h>`/`<string.h>`/`<stdint.h>`/`<stdbool.h>`, and must contain `typedef … uint8_t`.
   (`<stdlib.h>` stays until FASE2/FASE3.)
6. **Fixpoint.** The preamble changes → `gen1 ≠ gen0`; converge `gen2 == gen3`. `floor`/`round`/`ceil`
   MUST be numerically identical to the libc versions (the fp results feed emitted programs) — a
   divergence is a wrong intrinsic, HALT and fix.

## Rulings & laws

- **Teko-only:** codegen emit logic in `.tks`; the emitted intrinsics are compiler-emitted C, not a
  maintained C twin.
- **W15 full Javadoc** on `cg_emit_fixed_width_typedefs` and the intrinsic emitters; flatten; no `//`.
- **Owner "no shortcuts" (`plano-s16-expurgo §5` OVERARCHING LAW):** if it exists in C, it exists in
  Teko/emitted-intrinsic — `floor`/`round`/`ceil`/`memcpy` get REAL intrinsics, not no-ops.
- **`f64_bits` precedent:** a codegen intrinsic (compiler-lowered) is the sanctioned form for a math
  primitive; it need not live in `teko::mem`.
- **Self-host gate ordering (`§3`):** FASE1 first because it kills the emitted-C headers without
  touching the OS — the cheapest gate to prove; it precedes the arena (FASE3) and I/O (FASE2).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each header
  removal as its own green step; reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep
  `.tkt`/`.tkr` after the preamble change.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `math_floor_intrinsic` | `floor(3.9) == 3.0 && floor(0.0-2.1) == (0.0-3.0)` via the emitted intrinsic | 0 |
| `memcpy_intrinsic` | copy 16 bytes A→B via the intrinsic, compare equal | 0 |
| `no_stdint_header` | the emitted `teko.c` does NOT contain `#include <stdint.h>`; DOES contain `typedef … uint8_t` | 0 (compile + grep) |

These are the §7 provers of FASE1 (`S4`/`S5`/`S6`): the fp/mem intrinsic behavior and the header-removal
proof are NOT self-build-exercised as ASSERTIONS (the self-build proves the compiler still compiles, not
that `floor` is numerically right or that the header is gone) — isolated oracles under
`examples/regressions/` required.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + the three fixtures + `gen2==gen3` byte-identity. This is a RITUAL point (§8
after-F1): full 3-gen fixpoint + reseed + the emitted `teko.c` provably loses `<math.h>`/`<string.h>`/
`<stdint.h>`/`<stdbool.h>`. "Green" = the header grep passes, the intrinsics are numerically correct, and
the ladder converges. Reseed-class: `fixpoint-rebuild`.

## Deps

`SM-R1`.

## Done when

The emitted `teko.c` emits its own fixed-width typedefs and `floor`/`round`/`ceil`/`memcpy` intrinsics
(no `<math.h>`/`<string.h>`/`<stdint.h>`/`<stdbool.h>`), the three fixtures pass, and `gen2==gen3` holds.
