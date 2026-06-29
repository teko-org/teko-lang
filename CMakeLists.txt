# CMakeLists.txt — Teko bootstrap (the C23 mirror of the spec).
#
# This tree was MATERIALIZED from the canonical spec-as-source markdown
# (TEKO_SOURCE.md + TEKO_CHECKER.md): every fenced code block was moved into its
# own file under src/. Two coexisting layers live here:
#   * .tks / .tkt — the canonical Teko source (lexer→parser→checker→emit), the
#     real language; a future Teko toolchain compiles these.
#   * .h / .c     — the C23 bootstrap mirror of the same algorithms.
#
# NOTE (M.4 — do not build on the incomplete): the C mirror is NOT yet a complete,
# linkable program — the pipeline (read→lex→parse→check→codegen) is not wired, there
# is no main, and the parser C mirror is partial (headers). (The lexer C mirror is
# now materialized — token.h/lexer.h/lexer.c — and compiles clean.) This file
# establishes the PROJECT STRUCTURE; it is
# intentionally not expected to compile/link as-is. Targets are listed so the tree is
# navigable and ready to be wired incrementally.

cmake_minimum_required(VERSION 3.20)
project(teko_bootstrap LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# All modules see the source root as the include base (headers are referenced as
# "type.h", "scope.h", … within a module, and "../parser/ast.h" across modules).
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- C mirror translation units, grouped by module ---------------------------
# (Header-only modules — core, parser (ast.h/result.h) — contribute include paths
#  only; they have no .c yet.)

set(TEKO_TEXT_SOURCES
    src/text/text.c)

# teko::assert — the C seed of assert.tks (the injected testing assertions). Shared by the
# VM (linked via the lib) and generated programs (driver.c hands it to the host cc).
set(TEKO_ASSERT_SOURCES
    src/assert/assert.c)

# teko::build — the `.tkp` manifest reader (crumb A1). The FIRST .c under src/build.
set(TEKO_BUILD_SOURCES
    src/build/tkp_rule.c
    src/build/manifest.c
    src/build/discover.c
    src/build/assemble.c)

set(TEKO_LEXER_SOURCES
    src/lexer/lexer.c)

set(TEKO_PARSER_SOURCES
    src/parser/ast.c
    src/parser/cursor.c
    src/parser/optokens.c
    src/parser/parse_lit.c
    src/parser/parse_path.c
    src/parser/parse_type.c
    src/parser/parse_pattern.c
    src/parser/parse_expr.c
    src/parser/parse_if.c
    src/parser/parse_match.c
    src/parser/parse_arm.c
    src/parser/parse_stmt.c
    src/parser/parse_decl.c
    src/parser/parse_file.c)

set(TEKO_CHECKER_SOURCES
    src/checker/type.c
    src/checker/scope.c
    src/checker/resolve.c
    src/checker/collect.c
    src/checker/match.c
    src/checker/expr.c
    src/checker/typer.c
    src/checker/check_modules.c
    src/checker/initanalysis.c
    src/checker/monomorph.c
    src/checker/escape.c
    src/checker/revalidate.c)

set(TEKO_EMIT_SOURCES
    src/emit/tkb_buf.c
    src/emit/tkb_frame.c
    src/emit/tkb_write.c
    src/emit/tkb_read.c
    src/emit/tkh.c
    src/emit/header.c
    src/compress/compress.c)   # C7.12: ZIP-STORE .tkl writer (teko::compress)

# --- F2: the C backend (codegen) — lowers the checked typed tree to C source ---
set(TEKO_CODEGEN_SOURCES
    src/codegen/codegen.c)

# --- Eixo D: the VM / interpreter — the test/debug execution engine (Stage-1) ---
# Tree-walks the checked typed tree directly (no .tkb, no codegen); mirrors codegen
# semantics. `teko run` executes a program on it; D2 will run tests on it.
set(TEKO_VM_SOURCES
    src/vm/vm.c)

# The bootstrap mirror as one static library. The whole C front-end + checker + emit
# compiles (F0 done); the default build now archives it, guarding the mirror against
# compile regressions. It is not yet linked into an executable — the driver (F1) will
# link `teko` against it once the pipeline is wired.
add_library(teko_bootstrap STATIC
    ${TEKO_TEXT_SOURCES}
    ${TEKO_ASSERT_SOURCES}
    ${TEKO_BUILD_SOURCES}
    ${TEKO_LEXER_SOURCES}
    ${TEKO_PARSER_SOURCES}
    ${TEKO_CHECKER_SOURCES}
    ${TEKO_EMIT_SOURCES}
    ${TEKO_CODEGEN_SOURCES}
    ${TEKO_VM_SOURCES})

# --- F1: the driver — path-to-first-binary -----------------------------------
# `teko` is now the REAL driver: read -> lex -> parse -> reconcile -> check, wired in
# src/driver.c and linked against the bootstrap mirror library. (Codegen/emit is F2;
# success here means the front-end pipeline ran and the program type-checked.)
# The VM (`teko run`) calls the execution-runtime symbols (tk_print/tk_panic_*/
# teko__assert__*), so the compiler binary itself links teko_rt.c — the same source the
# driver also hands to the host cc for generated programs. It is kept an EXE source (not in
# the lib) so it is never double-linked into the generated programs the driver builds.
add_executable(teko main.c src/driver.c src/runtime/teko_rt.c src/win32_int128_builtins.c)
target_link_libraries(teko PRIVATE teko_bootstrap)
# vm.c calls floor() via the f64 arithmetic path; Linux requires explicit -lm linkage.
if(UNIX AND NOT APPLE)
    target_link_libraries(teko PRIVATE m)
endif()
# src/win32_int128_builtins.c provides __divti3/__udivti3/__modti3/__umodti3/
# __floattidf/__floatuntidf/__fixdfti/__fixunsdfti — the compiler-rt builtins
# needed for __int128 arithmetic on Windows. Clang on MSVC ABI does not
# auto-link these; the file is all-#ifdef _WIN32 so it is a no-op on POSIX.
# The VM interpreter uses deep C recursion when running the Teko parser/lexer during
# `teko test` (each Teko function call adds ~4 C frames; parsing 66 files × recursive
# descent easily exceeds the default 8MB main-thread stack). Set to 64MB to give enough room.
if(APPLE)
    target_link_options(teko PRIVATE "-Wl,-stack_size,0x4000000")
elseif(WIN32)
    # lld-link /STACK:reserve — 64 MB, matching Linux/macOS.  Without this the
    # 1 MB Windows default overflows mid-test and the process exits with code 1.
    target_link_options(teko PRIVATE "-Wl,/STACK:67108864")
elseif(UNIX)
    target_link_options(teko PRIVATE "-Wl,-z,stacksize=67108864")
endif()

# F3: the generated programs link the minimal execution runtime (libteko_rt).
# teko needs the runtime's location to pass -I and the runtime source to the host cc.
target_compile_definitions(teko PRIVATE
    TK_RT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
    TK_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# --- SELF-HOST: the native build of teko's OWN corpus — THE end-to-end validation ----------
# `teko build .` runs the full pipeline (read → lex → parse → check → native codegen → cc link),
# emitting ./bin/teko. This is the gating proof: if this fails, the native backend is not coherent,
# so nothing downstream is guaranteed. Run it with:  cmake --build build --target selfhost
# (equivalently, by hand:  ./build/teko build .  from the project root → ./bin/teko).
add_custom_target(selfhost
    # The self-build runs the D4 test gate like any other build (`--no-test` is no longer honored —
    # a release must run its tests). The compiler's own `.tkt` corpus is VM-runnable and passes in
    # both vm.c (build/teko) and vm.tks (bin/teko). (See TEKO_MASTER_PLAN.md Phase 7 D4.)
    COMMAND $<TARGET_FILE:teko> build ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS teko
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "self-host: native build of teko's own corpus -> ./bin/teko (THE validation gate)"
    USES_TERMINAL
    VERBATIM)
