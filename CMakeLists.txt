# CMakeLists.txt — Teko bootstrap (the C23 mirror of the spec).
#
# This tree was MATERIALIZED from the canonical spec-as-source markdown
# (TEKO_SOURCE.md + TEKO_CHECKER.md): every fenced code block was moved into its
# own file under src/. Two coexisting layers live here:
#   * .tks / .tkt — the canonical Teko source (lexer→parser→checker→emit), the
#     real language; the Teko toolchain (the downloaded release seed) compiles these.
#   * .h / .c     — the C23 bootstrap mirror of the same algorithms.
#
# NOTE (M.4 — do not build on the incomplete): the C mirror is a frozen front-end
# COMPILE-REGRESSION GUARD, not a runnable program. Since #524 (VM retirement) the
# vestigial C-bootstrap `teko` executable (main.c + src/driver.c, which #include'd
# vm/vm.h) is retired: the compiler seed is the downloaded release (CI never invokes
# cmake). What remains is the `teko_bootstrap` STATIC library — archiving it guards the
# C front-end mirror against compile regressions.

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

# teko::assert — the C seed of assert.tks (the injected testing assertions), archived
# into the front-end guard library alongside the rest of the C mirror.
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

# The bootstrap mirror as one static library — the frozen front-end COMPILE-REGRESSION
# GUARD. The whole C front-end + checker + emit + codegen compiles (F0 done); the default
# build archives it. It is not linked into an executable: since #524 the compiler seed is
# the downloaded release, so the C-bootstrap `teko` exe (and the VM's vm.c) are retired.
add_library(teko_bootstrap STATIC
    ${TEKO_TEXT_SOURCES}
    ${TEKO_ASSERT_SOURCES}
    ${TEKO_BUILD_SOURCES}
    ${TEKO_LEXER_SOURCES}
    ${TEKO_PARSER_SOURCES}
    ${TEKO_CHECKER_SOURCES}
    ${TEKO_EMIT_SOURCES}
    ${TEKO_CODEGEN_SOURCES})
