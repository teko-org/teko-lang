---
seq: 0096
crumb-id: S16-SWEEP
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [RM-C9]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:335-340"              # C-death context
  - "docs/design/plano-mestre-0.3.1-implementacao.md:266"              # M3 S16-SWEEP row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:408-410"          # §16 removes the runtime C dep
  - "src/codegen/codegen.tks:8765"                                     # FASE9 #include emission
  - "src/runtime/teko_rt.c"                                            # the C files to delete
---

# 0096 · S16-SWEEP — FASE9 stop emitting `#include "teko_rt.h"/assert.h/win32_compat`; DELETE the C files

> The E4 (part 2) §16 TERMINAL sweep: with RM-C9 done (runtime fully in Teko), stop codegen emitting the
> `#include "teko_rt.h"` / `#include "assert.h"` / `win32_compat` lines (FASE9), DELETE the four C files
> (`teko_rt.{c,h}`, `assert.{c,h}` + the `win32_compat.h` header), and fix the CI/build scripts that
> compiled them. The runtime half of "no C" closes here.

## Goal

By this seq every runtime leg (RT-L*) and the arena control slot (RM-C9) live in Teko, so the emitted C
translation unit no longer needs ANY C runtime include. This crumb makes that literal: codegen's FASE9
prologue (`codegen.tks:8765-8766`) STOPS emitting `#include "teko_rt.h"` and `#include "assert.h"` (and
the `win32_compat` include), the four maintained-C files are DELETED, and the CI/build scripts that
referenced/compiled them are fixed. It is a **byte-mover** expurgo (the emitted `teko.c` changes — the
include lines vanish — so it reseeds; E4 part 2). This removes the runtime C DEPENDENCY; the emitted
`teko.c` itself remains as the transitional COMPILE crutch until M4's RM-C16 removes the C route entirely.
No tombstone — the deleted files simply cease to exist and nothing includes them.

## Where

- `src/codegen/codegen.tks:8765-8766` — the `cb(b, "#include \"teko_rt.h\"\n")` and
  `cb(b, "#include \"assert.h\"\n\n")` emission (+ the `win32_compat` include if emitted nearby) — REMOVE
  the emission so the generated TU carries no C runtime include.
- `src/runtime/teko_rt.c`, `src/runtime/teko_rt.h`, `src/assert/assert.c`, `src/assert/assert.h` — the
  four C files — DELETE. `src/win32_compat.h` (the fifth header the includes named) — DELETE with them
  (its consumers are gone once the includes stop).
- CI / build scripts — `scripts/build_gen1_from_c.sh` and the CI legs that `cc`-compiled the emitted TU
  against `teko_rt.c`/`assert.c` — fix to compile the TU WITHOUT the deleted C files (the runtime is now
  self-contained Teko lowered into the TU).

## How

1. **Stop the FASE9 include emission** (`codegen.tks:8765`): remove the two `cb(...)` include lines (and
   the `win32_compat` one). The emitted TU's prologue now names no C runtime header. This is what moves
   the bytes — the reseed carries the shorter prologue.
2. **DELETE the four C files** (+ `win32_compat.h`). Per the maintained-C exception being SPENT: with the
   runtime in Teko and no include emitted, these files have no consumer. This is the moment the
   maintained-C exception ends for the runtime.
3. **Fix CI/build scripts** so the generated `teko.c` compiles standalone (no `-c teko_rt.c`, no
   `assert.c` link). The `ci_cc_wrap.sh` clang shim path stays (it wraps `cc`→clang for the emitted TU),
   only the runtime C compilation is removed.
4. **NO tombstone:** nothing references the deleted files; there is no "runtime moved to Teko" comment or
   diagnostic — the includes are simply gone.
5. **Reseed ITERATIVELY** (the emitted-byte change makes this a real reseed) until `gen2==gen3`
   byte-identical, AFTER the unified message pass over the integrated tree.
6. **Triage the CI (owner):** a `fixpoint (native)` failure is EXPECTED (native stops at its degree); a
   `produce this leg` failure (C or native) is a REAL failure — read the step head=100.

## Rulings & laws

- **Teko-only:** codegen `.tks`; the C files are DELETED (the maintained-C runtime exception is now
  spent — the runtime lives in Teko).
- **W15 full Javadoc** on any survivor touched; the deleted files carry no doc.
- **Removals = clean expurgo, NO tombstone:** the includes and files vanish; no diagnostic references
  them (`plano-mestre:253-257`).
- **§16 removes the RUNTIME C dependency** (`plano-mestre:408-410`); the emitted `teko.c` stays as the
  COMPILE crutch until RM-C16 (M4). Do NOT remove the C ROUTE here — only the C runtime files.
- **Safety:** NEVER `teko test .`; `ulimit -v 6815744` per build; commit each green step; reseed ONLY at
  this [RITUAL]; E4-part-2 harvest at `gen2==gen3`; the unified message pass precedes the reseed.

## Fixtures

none — the fixpoint self-build exercises this. The emitted TU compiling and running WITHOUT the C runtime
files IS the proof; a residual dependency surfaces as a build/link failure in the produce-leg or a
`gen2!=gen3` fixpoint break. No isolated `.tkr` reaches the codegen-prologue/CI-script surface.

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed (E4, part 2, LAST of M3). "Green" = codegen
emits no C runtime include, the four C files (+ `win32_compat.h`) are deleted, the CI/build scripts
compile the TU standalone, the C `produce` legs stay green, and `gen2==gen3` byte-identical after the E4
harvest. Reseed-class: `expurgo`.

## Deps

`RM-C9` (runtime + arena control fully in Teko) + all M2 §16 rows — verbatim from 000-INDEX. E4's second
member; M3 closes here.

## Done when

No `#include "teko_rt.h"/"assert.h"/win32_compat` is emitted, the four C files (+ `win32_compat.h`) are
deleted, the build scripts compile the generated TU standalone, the C produce legs stay green, and the
E4 reseed lands `gen2==gen3` byte-identical — the runtime half of "no C" is closed.
