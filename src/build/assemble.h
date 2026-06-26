// src/build/assemble.h — teko::build: multi-file project ASSEMBLY, C23 mirror of
// assemble.tks. The THIRD step of project-based compilation (after the manifest read
// A1 and discovery A2): read + parse every discovered file and MERGE all top-level
// items into ONE tk_program (M.1 — the whole program is seen before it is checked;
// REBOOT_PLAN §2.6 "compila o projeto inteiro de uma vez").
#ifndef TK_BUILD_ASSEMBLE_H
#define TK_BUILD_ASSEMBLE_H

#include "../core.h"          // tk_error, TK_RESULT
#include "../parser/ast.h"    // tk_program, tk_item
#include "discover.h"         // tk_source_files (the A2 output)

// `Program | error` — the result of assemble (the merged, untyped program).
TK_RESULT(tk_program, tk_program_result);

// tk_assemble — read + parse every discovered file, reconcile each to items, and
// MERGE them into one flat tk_program. The parse entry per file:
//   * a file whose path basename is `main.tks` → tk_parse_main_file (the entry point)
//   * every other file                          → tk_parse_module
// Every merged item is TAGGED with its SourceFile namespace (tk_item.namespace) so
// later codegen can mangle cross-namespace names; typing ignores the tag (the merged
// program is one flat name-space for resolution — see the A3 HALT note in assemble.c).
// On any read/parse failure → tk_error (the first failure, path-prefixed). Allocation
// failure aborts (M.5), mirroring the tree.
tk_program_result tk_assemble(tk_source_files files);

// tk_assemble_sel — assembly with a test selector: include_tests=true INCLUDES `.tkt` files
// (their `#test` functions enter the program for the VM runner). (Mirrors assemble.tks.)
tk_program_result tk_assemble_sel(tk_source_files files, bool include_tests);

#endif // TK_BUILD_ASSEMBLE_H
