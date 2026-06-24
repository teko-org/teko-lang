// src/build/manifest.h — teko::build: the `.tkp` manifest reader, C23 mirror of
// manifest.tks. The manifest is read BEFORE any Teko is parsed (a standalone simple
// TOML-subset parser — LEGISLATION §208–214).
#ifndef TK_BUILD_MANIFEST_H
#define TK_BUILD_MANIFEST_H

#include "../core.h"        // TK_LIST, TK_RESULT, tk_error
#include "../text/text.h"   // tk_str

// Artifact — the C mirror of tkp_rule.tks `enum { Executable; Library }`. (src/build
// has no other .c yet, so the enum is realized here, at its first C use.) The manifest
// encodes Executable as `[artifact] kind = "binary"`.
typedef enum {
    TK_ARTIFACT_EXECUTABLE,
    TK_ARTIFACT_LIBRARY,
} tk_artifact;

// a list of strings — teko::list realized over tk_str (deps / aliases as simple string
// lists in the seed — M.5; richer dependency records are evolution).
TK_LIST(tk_str, tk_strs);

// the parsed manifest. `name` / `source` are VIEWS into the source bytes (zero-copy,
// like every tk_str); the deps/aliases lists own their backing array (free with
// tk_strs_free). `artifact` defaults to Library unless `[artifact] kind = "binary"`.
typedef struct {
    tk_str      name;       // canonical root
    tk_artifact artifact;   // Executable | Library
    tk_str      source;     // the invisible source root
    tk_strs     deps;       // [dependencies] keys
    tk_strs     aliases;    // [aliases] keys
} tk_manifest;

// `Manifest | error` — the result of parse_manifest.
TK_RESULT(tk_manifest, tk_manifest_result);

// tk_parse_manifest — read a `.tkp` source into a tk_manifest, or fail honestly (M.3,
// no silent coercion). Recognized TOML subset: top-level `key = "value"`, `[table]`
// headers, and arrays `key = ["a", "b"]`. NOT full TOML (no datetimes, no nested inline
// tables — M.5). Malformed input or a missing required field (name, source) → tk_error.
// Allocation failure aborts (M.5), mirroring the codebase.
tk_manifest_result tk_parse_manifest(tk_str src);

#endif // TK_BUILD_MANIFEST_H
