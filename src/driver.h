// src/driver.h — the Teko bootstrap DRIVER (F1/F2: path-to-first-binary).
//
// Teko is a MONOLITH that compiles PROJECTS, not isolated `.tks` files (REBOOT_PLAN
// §2.6). The driver wires the project pipeline end-to-end: read `.tkp` → discover →
// assemble (read → lex → parse → reconcile → MERGE) → check (whole, M.1) → then either
// the native backend (build) or the VM (run).
#ifndef TK_DRIVER_H
#define TK_DRIVER_H

#include "core.h"            // tk_error, TK_RESULT
#include "text/text.h"       // tk_str, tk_str_result
#include "parser/ast.h"      // tk_main_file, tk_module, tk_program

// B1a — the IO boundary. Reads the whole file at `path` into a freshly heap-allocated
// `tk_str` (UTF-8 validated on the way in). This is the bootstrap's ONE host-IO
// function (the unsafe host boundary — M.1, contained here). On failure returns a
// result with `.ok == false` and a clear error message.
tk_str_result tk_read_file(const char *path);

// B1c — R-main reconciliation. Flatten the parsed file model into a flat `tk_item`
// list (`tk_program`) the checker consumes.
//   * MainFile: each `use` → TK_ITEM_USE; each virtual-main statement → TK_ITEM_STATEMENT.
//   * Module:   each `use` → TK_ITEM_USE; each decl → TK_ITEM_FUNCTION / TK_ITEM_TYPE_DECL.
tk_program tk_main_file_to_program(tk_main_file mf);
tk_program tk_module_to_program(tk_module m);

// A3 — the PROJECT BUILD entry. Teko is a MONOLITH that compiles PROJECTS, not isolated
// `.tks` files (REBOOT_PLAN §2.6). Given a project directory `dir`, read `<dir>/.tkp`
// (tk_parse_manifest), discover the source tree (tk_discover), assemble all files into
// one merged tk_program (tk_assemble), type-check it whole (tk_type_program — M.1), then
// run the native BACKEND (tk_backend) over the merged program to produce a binary named
// from the manifest. For a single-namespace project (root main.tks ± one namespace) the
// lowering works; if codegen hits an unsupported multi-namespace mangling case it fails
// with the honest existing message (no silent mis-emit). Output binary stem = manifest
// `name`. Returns 0 on a clean build. main() dispatches here for `teko build <dir>`.
// `out_dir` is the build output directory (default "bin", or the CLI `-o <dir>` argument).
int tk_compile_project(const char *dir, const char *out_dir);

// Eixo D — the PROJECT RUN entry (debug profile). Mirrors tk_compile_project's front
// (manifest → discover → assemble → check) but ends in the VM: INTERPRET the checked
// merged tree (tk_vm_run) instead of codegen → cc. The process exit code is the
// virtual-main's (early `return n` → n, default 0); a panic (÷0 / impossible cast /
// failed assert) goes to stderr with a non-zero exit. main() dispatches here for
// `teko run <dir>`.
int tk_run_project(const char *dir);

// D2 — the PROJECT TEST entry (`teko test <dir>`): assemble WITH `.tkt` tests, then run every
// `#test` function on the VM (fail-fast). Mirrors project.tks test_project.
int tk_test_project(const char *dir);

#endif // TK_DRIVER_H
