// src/runtime/teko_rt_wasm_shim.c   (namespace 'teko::runtime', wasm32-wasi target only)
//
// The own AOT backend's wasm32-wasi link tail (#389 C1-5) links this TINY,
// wasm-specific shim instead of the full teko_rt.c. teko_rt.c pulls in POSIX
// process/signal/backtrace/dynamic-loading surface (fork/execvp/waitpid,
// dlfcn.h, execinfo.h, sys/resource.h, signal.h) and uses `_Float16`, none of
// which wasm32-wasi supports (`_Float16` is rejected outright by the wasm32
// LLVM backend; wasi-libc has no dlfcn/execinfo/fork/waitpid/getrusage at
// all) — a genuine runtime-portability gap, out of THIS slice's scope (a
// full wasm32-wasi port of teko_rt.c is a named follow-on, not expanded here,
// M.3). This shim implements JUST the runtime symbol the exit(n)/arithmetic/
// control-flow corpus's own-generated code calls (`tk_exit`), delegating
// directly to its WASI syscall — no libc, no wasi-libc crt startfiles, so it
// compiles and links standalone via `wasm-ld` (docs/design/backend-c1-wasm.md
// §11 D-W1's "self-contained module + thin wasi-shim" MVP-scoping choice).

// The raw WASI preview1 import: `proc_exit(rval: i32)`, imported straight
// from "wasi_snapshot_preview1" (no wasi-libc wrapper, no <wasi/api.h>).
__attribute__((import_module("wasi_snapshot_preview1"), import_name("proc_exit")))
void __teko_wasi_proc_exit(int code);

// tk_exit — the wasm32-wasi realization of teko_rt.c's tk_exit(int): every
// own-generated program's `exit(n)` call lowers to this symbol
// (src/lir/lower.tks::call_symbol). Diverges via the WASI process-exit
// syscall; never returns.
void tk_exit(int code) {
    __teko_wasi_proc_exit(code);
    __builtin_unreachable();
}
