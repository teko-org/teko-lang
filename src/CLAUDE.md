# CLAUDE.md — `src/` (compiler internals)

See the root `CLAUDE.md` for commands, CI, discipline, and decisions. This is a map.

## Pipeline
`lexer` → `parser_*` (modular: statements, FFI, visibility, concurrent, DI, generics) →
type/semantic (`semantic_*`) → **IL bytecode** (`codegen_li.c/.h`, `BytecodeBuffer` +
`ConstantStringPool`) → backend.

## Backend (`src/codegen/`)
- `codegen_metal.c` — the polymorphic router: `teko_metal_emit_program(ctx, bytecode, size)`
  dispatches each opcode by `(os, arch)` to one of the **16 emitters**. It also runs the
  IL pre-passes (constant folding / DCE / CSE) and reads 4-byte args for
  `ICONST/SCONST/JMP/JMP_IF_FALSE/FUNC_BEGIN`.
- Emitters: `apple/` (Mach-O), `linux/` (ELF), `windows/` (PE-COFF), `bsd_unix/` (ELF),
  `bare_metal/emit_wasm.c` (WASM). Shared cores: `emit_x86_64_sysv_common.c`,
  `emit_arm64_gas_common.c`, `linux/emit_linux_riscv_common.c` (kept byte-identical via goldens).
- Linker/object writers: `tld_elf*.c`, `tld_macho.c`, `tld_pe.c`, `tld_wasm.c`, `tld_symbols.c`.
- `OpCode` enum lives in `codegen_li.h`; free slots include `0x09`, `0x0A–0x0F`, `0x15–0x1F`.

## Hard rules (also in root CLAUDE.md)
- The WASM emitter is an **accumulator machine** (`$w0/$w1/$cp`); keep every op stack-neutral.
- MSVC: no computed-goto / `auto` / `nullptr`; portable packing (`teko_il.h`); guard `<unistd.h>`.
- **Zero-init AST nodes** (`calloc`) — the Windows crash was an uninitialized-pointer free in
  `parser_ffi.c`. Don't free fields a parse path may have left unset.
- FFI (`parser_ffi.c`) is parsed but **not yet lowered** to IL — that's the Browser FFI phase.
