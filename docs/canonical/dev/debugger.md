# `tdb` — the Teko Debugger

Teko ships its own source-level debugger, `tdb`, rather than depending on `gdb`/`lldb` as the
only way to step through a Teko program. It exists because the own native backend does not go
through a C intermediate that a foreign debugger could read symbols from "for free" — the
debugging story has to be built on the same footing as the codegen it debugs.

## The experience

```sh
teko build --debug=lines .     # -g is rejected with a message pointing here: Teko has no
                                # C-shaped "-g" knob; --debug=lines is the explicit Teko form
tdb exec ./bin/myprogram
```

```
#0  teko::demo::add   at src/main.tks:42
#1  teko::demo::main  at src/main.tks:52
(tdb) break src/main.tks:42
(tdb) continue
(tdb) print x
(tdb) locals
(tdb) backtrace
```

`tdb` reports Teko-qualified names and exact `file:line` positions natively — a backtrace names
`teko::demo::add`, not a mangled C symbol a human has to demangle by hand.

## Why line-level debug info, not a C-shaped `-g`

`--release` builds carry no debug info by default (matching every other systems toolchain's
default); `--debug=lines` is the explicit, named way to ask for it, and it is deliberately
**not** spelled `-g` — Teko's manifest has no place to hide a build flag by convention, and a
familiar-looking `-g` would imply parity with a C toolchain's debug info that the own backend
does not (yet) fully match. Naming the flag differently is honest about that gap rather than
papering over it with a familiar spelling.

## What the compiler produces

Two complementary artifacts feed the debugger:

- **`.tsym`** (*Teko Symbols*) — a compact, purpose-built map: `<compiled-symbol>` →
  `<teko-qualified-name>` → `file:line`. This is the minimum needed for readable stack traces
  and is what production crash backtraces already rely on, independent of any richer debug
  format.
- **DWARF** (`.debug_abbrev`/`.debug_info`/`.debug_line`, emitted by `src/backend/dwarf.tks`) —
  a real, standards-shaped line-and-frame table. DWARF is kept as **interop**, not because the
  design needs its own format reinvented: it is what lets `tdb` reuse a standard unwinder/line
  lookup instead of building one from nothing, and it is what makes a Teko binary at least
  partially legible to `gdb`/`lldb` directly, without `tdb` at all, as a fallback.

`tdb` itself does not need to out-guess a foreign debugger's heuristics for locating Teko
frames — because Teko controls both the compiler and the debugger, the position information
travels through the pipeline as **ground truth** (carried on every LIR instruction as it is
lowered) rather than being re-derived by inference at debug time the way a debugger for an
opaque black-box binary has to.

## Phased delivery, each phase independently useful

The debugger is built as a sequence of phases, each one a complete, usable capability on its
own rather than a single monolithic delivery:

1. **The harness and oracle** — a test asset proving debug info stays correct as the compiler
   changes, so the debugger's own foundation cannot silently rot.
2. **Compiler lineage (`.tsym` v2)** — production stack traces with an exact line, everywhere.
3. **The control floor** — `tdb exec` runs a program under `tdb`'s control at all.
4. **Breakpoints** — stopping reliably at an address and resuming.
5. **Position** — the full terminal debugger: stepping, frame navigation, source display.
6. **Variables and legibility** — `print`, `locals`, `args`, `whatis`.
7. **The editor** — debugging a Teko program from inside an editor (DAP-shaped, wired to the
   same LSP-adjacent tooling described in `lsp-and-tooling.md`).
8. **The ports** — `tdb` on macOS and on Windows, on top of the native backend's own port work.

## Verification

`tdb` is proven against **hand-written oracle fixtures** — programs whose expected stack trace,
breakpoint behavior, and variable values are known in advance and checked exactly — at three
levels: unit-level assertions on the emitted debug data, a scripted end-to-end session driving
`tdb` itself, and (where useful) cross-checking against a standard debugger's independent
DWARF-reading path as a second reader that doesn't share any code with `tdb`.
