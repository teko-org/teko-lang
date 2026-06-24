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

set(TEKO_LEXER_SOURCES
    src/lexer/lexer.c)

set(TEKO_CHECKER_SOURCES
    src/checker/type.c
    src/checker/scope.c
    src/checker/resolve.c
    src/checker/collect.c
    src/checker/match.c
    src/checker/typer.c
    src/checker/revalidate.c)

set(TEKO_EMIT_SOURCES
    src/emit/tkb_buf.c
    src/emit/tkb_frame.c
    src/emit/tkb_write.c
    src/emit/tkb_read.c
    src/emit/tkh.c
    src/emit/header.c)

# The bootstrap mirror as one (currently incomplete) static library. Excluded from
# ALL so a plain `cmake --build` does not attempt the not-yet-linkable mirror.
add_library(teko_bootstrap STATIC EXCLUDE_FROM_ALL
    ${TEKO_TEXT_SOURCES}
    ${TEKO_LEXER_SOURCES}
    ${TEKO_CHECKER_SOURCES}
    ${TEKO_EMIT_SOURCES})

# --- F-1: build smoke / driver skeleton --------------------------------------
# A standalone, ALWAYS-built entry that proves the toolchain + CMake configure/compile/
# link/run end-to-end, independent of the (still-incomplete) mirror library above. F1
# grows this into the real driver (read->lex->parse->check->emit-C) once F0 makes the
# mirror compile and it can be linked in.
add_executable(tekoc src/main.c)
