# Teko — Programmer's Reference

This is the canonical, target-state reference for writing Teko programs: what the language is,
how to get started, the language itself, the CLI and editor tooling, and package management.

## Map of this tree

| Document | Covers |
|---|---|
| [`what-is-teko.md`](what-is-teko.md) | Teko in one page: the pitch, the design pillars, who it's for |
| [`getting-started.md`](getting-started.md) | Installing the toolchain, your first project, the core commands |
| [`language-guide.md`](language-guide.md) | The language: types, errors-as-values, optionals, memory, classes/interfaces/generics, pattern matching, concurrency, testing |
| [`cli-and-tooling.md`](cli-and-tooling.md) | Every `teko` subcommand, editor support, the debugger |
| [`packages.md`](packages.md) | The manifest, dependencies, publishing, the security model |
| [`examples.md`](examples.md) | Worked, runnable examples |

## If you already know a systems language

Teko borrows deliberately, and names what it borrows from rather than blending unattributed
conventions: **Rust** on the surface (syntax, types, ergonomics), **Zig** on control flow and
explicitness, **C#** on extensibility patterns, **Go** on certain runtime behaviors. If
something looks unfamiliar, it is very likely a considered choice against one of those four,
not an oversight — the language guide names the reference it follows wherever the choice is
non-obvious.
