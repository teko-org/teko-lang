# 🗺️ Engineering Plan: Leko AOT Bare-Metal Compiler (Phase 5)

This document specifies the definitive architecture of the **Ahead-of-Time (AOT)** backend for the **Leko** language. The goal is to completely eliminate the dependency on a Virtual Machine (VM) in production (*Release*) mode, transpiling the Abstract Syntax Tree (AST) and Intermediate Language (IL) directly into native (*bare-metal*) machine code specific to each combination of **Operating System and Processor Architecture**.

---

## 1. Native Target Matrix (Target Triples) and ABIs

To support the universality of the language without depending on heavy third-party ecosystems, the pure C backend segments call management and register conventions (*Calling Conventions*) as shown in the table below:

| Operating System (OS) | Executable Family | Calling Convention / ABI | System Exit (Syscall) |
| :--- | :--- | :--- | :--- |
| **macOS (Darwin)** | Mach-O (`.s` -> `.o`) | ARM64 Standard Apple ABI | `svc #0x80` / `sys_exit` (1) |
| **Linux (64-bit)** | ELF (`.s` -> `.o`) | System V AMD64 ABI | `syscall` / `sys_exit` (60) |
| **Linux (32-bit)** | ELF (`.s` -> `.o`) | System V i386 ABI | `int $0x80` / `sys_exit` (1) |
| **Windows (64-bit)** | PE/COFF (`.s` -> `.obj`) | Microsoft x64 Calling Convention | `ExitProcess` via `KERNEL32` |
| **FreeBSD (Unix)** | ELF (`.s` -> `.o`) | System V AMD64 (FreeBSD syscall) | `int $0x80` / `sys_exit` (1) |
| **Bare-Metal (WASM)** | WebAssembly Web Text | Monadic S-Expression Expressions | `unreachable` instruction |
| **Bare-Metal (AVR)** | Flat Binary (Arduino) | Atmel AVR 8-bit ABI | Infinite loop `rjmp .` |

---

## 2. Physical Requirements per Leko ISA Opcode

Each architecture file segregated in the subfolders (`apple/`, `linux/`, `windows/`, `bsd_unix/`, `bare_metal/`) must implement a contiguous `switch` translating the abstract IL opcodes into real hardware register instructions:

### A) Text Loading and String Allocation (`OP_SCONST`)
Because dynamic strings or literals do not fit directly inside a single CPU register, the compiler adopts relative memory pointer strategies:
*   **macOS (Darwin ARM64):** Injects the page operator `adrp x0, .L_str_idx@PAGE` combined with the offset load `add x0, x0, .L_str_idx@PAGEOFF`. Stores the bytes in the `.section __TEXT,__cstring,cstring_literals` section via the `.asciz` directive.
*   **Linux / FreeBSD (x86_64):** Uses instruction-pointer-relative (*RIP-relative*) addressing via `leaq .L_str_idx(%rip), %rax`. Stores the bytes in the `.section .rodata` section via the `.asciz` directive.
*   **Windows (x86_64):** Adopts Intel syntax with `lea rax, [rip + .L_str_idx]`. Stores the bytes in the `.section .rdata,"dr"` section using the byte-definition directive `db "text", 0`.
*   **WebAssembly (WASM):** Maps all literals contiguously in the virtual module's data segment: `(data (i32.const offset) "text\00")`.
*   **AVR (8-bit Microcontrollers):** Injects the `.section .progmem.data` modifier to save the text in the chip's Flash memory, preventing static strings from consuming the scarce hardware RAM (SRAM). The load splits the 16-bit pointer across two registers: `ldi r24, lo8(.L_str_idx)` and `ldi r25, hi8(.L_str_idx)`.

### B) Arithmetic Protections in Silicon (`OP_ADD`, `OP_DIV`)
*   **RISC Processors (ARM64, RISC-V, MIPS):** Execute three-operand arithmetic (`add w0, w0, w1`). `OP_DIV` requires a prior hardware barrier via branches if the divisor is zero (`cbz` on ARM, `beqz` on RISC-V) to prevent a processor crash from a floating-point exception.
*   **CISC Processors (x86_64, x86_32):** Execute two-operand arithmetic accumulating into the first register (`addl %ebx, %eax`). `OP_DIV` requires sign-extending the accumulator before the physical instruction (`cltd` in 64-bit, `cdq` in 32-bit), and the safety branch against division by zero uses `cmpl $0, %ebx` combined with the conditional jump `je`.

### C) The Native Physical Arena Allocator (`OP_ARENA_PUSH`, `OP_ARENA_POP`)
To support zero-cost automatic memory management (*Region-Based Memory Management*) without the overhead of a Garbage Collector or dynamic Heap checks in production:
*   The compiler reserves an immutable callee-saved register to act as the **Active Arena Cursor** (`x19` on ARM64, `%r12` on x86_64, `x9` on RISC-V, `r28/r29` on AVR).
*   **`OP_ARENA_PUSH` (`using` Block Start):** Allocates a fixed contiguous memory frame (e.g. 1024 bytes) by directly decrementing the CPU's physical stack pointer (`sp` / `%rsp`) and locking the Arena register to that address.
*   **`OP_ARENA_POP` (Scope Discard):** Releases all the region's memory in constant time **\(O(1)\)**, rolling back the stack pointer in batch via a single arithmetic addition instruction (`add sp, sp, #1024` or `addq $1024, %rsp`). Gigabytes of local data are wiped in a single clock cycle.

---

## 3. Central Emitter Orchestration Flow (`codegen_metal.c`)

To coordinate multi-engine generation without producing type conflicts or logical warnings in IDEs, the main routing file uses sentinel markers (`0xFE` to initialize the target OS's physical Prologue and `0xFF` to emit the closing Epilogue), scanning the IL contiguously:

```c
void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    if (!ctx || !bytecode || size == 0) return;

    // 1. Dispatch the start signal (Injects .global directives and OS stack alignments)
    teko_metal_route_instruction(ctx, 0xFE, 0);

    // 2. Linear scan and strict triage of Opcodes and Little Endian arguments
    uint32_t i = 0;
    while (i < size) {
        OpCode op = (OpCode)bytecode[i++];
        int32_t arg = 0;
        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) {
            arg = (bytecode[i+0] << 0) | (bytecode[i+1] << 8) | (bytecode[i+2] << 16) | (bytecode[i+3] << 24);
            i += 4;
        }
        teko_metal_route_instruction(ctx, op, arg);
    }

    // 3. Dispatch the termination signal (Restores registers and injects the 'ret' or 'syscall' instruction)
    teko_metal_route_instruction(ctx, 0xFF, 0);
}
```

---

## 4. Next Dynamic Coding Steps

With the physical folder tree built via terminal (`mkdir -p`) and the hardware planning documented, the submodules will be coded in the following stable sequence of isolated files:

1.  `src/codegen/apple/emit_darwin_arm64.c`
2.  `src/codegen/bsd_unix/emit_freebsd_x86_64.c`
3.  `src/codegen/linux/emit_linux_x86_64.c`
4.  `src/codegen/windows/emit_win_x86_64.c`
5.  `src/codegen/bare_metal/emit_wasm.c`
6.  `src/codegen/bare_metal/emit_avr.c`

Each increment will be backed by strict Unity assertions within the `tests/codegen/` folder to guarantee 100% low-level stability.
