// src/build/manifest.c   (namespace 'teko::build')
//
// A MINIMAL TOML subset parser — enough for `.tkp`, not full TOML (no datetimes, no
// nested inline tables — M.5). Read BEFORE any Teko is parsed (LEGISLATION §208–214).
// One line at a time; every malformed line is an honest error (M.3 — no coercion).
#include "manifest.h"

// --- line cursor (zero-copy views into the source bytes) ---------------------

// the byte at p, or 0 past the end (a safe peek, like the lexer's `at`).
static tk_byte at(tk_str s, size_t p) {
    if (p >= s.len) return 0;
    return s.ptr[p];
}

static bool is_space(tk_byte c) { return c == ' ' || c == '\t' || c == '\r'; }

// advance past spaces/tabs/CR (not newlines — a line is the unit of work).
static size_t skip_spaces(tk_str s, size_t p) {
    while (p < s.len && is_space(s.ptr[p])) p++;
    return p;
}

// --- string value: a `"…"` quoted scalar -------------------------------------
//
// Reads the bytes between the quotes as a zero-copy view. Escapes are NOT
// interpreted in the seed (a `.tkp` value is a plain name/path — M.5); a backslash
// is a literal byte. `*end` lands just past the closing quote on success.
static bool read_quoted(tk_str s, size_t p, tk_str *out, size_t *end) {
    if (at(s, p) != '"') return false;
    size_t start = p + 1;
    size_t q = start;
    while (q < s.len && s.ptr[q] != '"') q++;
    if (q >= s.len) return false;                 // unterminated
    *out = tk_str_slice(s, start, q);
    *end = q + 1;
    return true;
}

// --- table headers & keys ----------------------------------------------------

// a key byte: letters, digits, `_`, `-`, `.` (TOML bare-key set, plus `.` for the
// rare dotted dep name — kept permissive; M.5).
static bool is_key_byte(tk_byte c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// read a bare key at p → view + the pos after it (0-length if none).
static tk_str read_key(tk_str s, size_t p, size_t *end) {
    size_t start = p;
    while (p < s.len && is_key_byte(s.ptr[p])) p++;
    *end = p;
    return tk_str_slice(s, start, p);
}

static bool key_is(tk_str k, const char *lit) {
    size_t i = 0;
    for (; i < k.len; i++) {
        if (lit[i] == '\0') return false;
        if (k.ptr[i] != (tk_byte)lit[i]) return false;
    }
    return lit[i] == '\0';
}

// --- which table are we in? --------------------------------------------------
typedef enum { SEC_ROOT, SEC_ARTIFACT, SEC_DEPS, SEC_ALIASES, SEC_COVERAGE, SEC_OTHER } section;

static tk_manifest_result fail(const char *msg) {
    return (tk_manifest_result){ .ok = false, .as.error = tk_error_make(msg) };
}

// an UNQUOTED non-negative integer value (TOML `key = 80`): the digit run at p (0 if none).
static uint64_t read_int(tk_str s, size_t p) {
    uint64_t n = 0;
    while (p < s.len && s.ptr[p] >= '0' && s.ptr[p] <= '9') { n = n * 10 + (uint64_t)(s.ptr[p] - '0'); p++; }
    return n;
}

tk_manifest_result tk_parse_manifest(tk_str src) {
    tk_manifest m = {
        .name     = (tk_str){ NULL, 0 },
        .artifact = TK_ARTIFACT_LIBRARY,   // default until `[artifact] kind = "binary"`
        .source   = (tk_str){ NULL, 0 },
        .deps     = tk_strs_empty(),
        .aliases  = tk_strs_empty(),
        .version  = (tk_str){ NULL, 0 },
        .suffix   = (tk_str){ NULL, 0 },
        .cov_functions = 80,  // D4 floors — default 80 when [coverage] / its keys are absent
        .cov_lines    = 80,
        .cov_branches = 80,
    };
    bool have_name = false, have_source = false;
    section sec = SEC_ROOT;

    size_t p = 0;
    while (p < src.len) {
        // slice the current line [p, eol); leave p past the newline for the next turn.
        size_t eol = p;
        while (eol < src.len && src.ptr[eol] != '\n') eol++;
        tk_str line = tk_str_slice(src, p, eol);
        p = (eol < src.len) ? eol + 1 : eol;

        size_t i = skip_spaces(line, 0);
        tk_byte c = at(line, i);

        if (c == 0 || c == '#') continue;          // blank or comment line

        // a table header `[name]`
        if (c == '[') {
            size_t ke;
            tk_str name = read_key(line, i + 1, &ke);
            size_t j = skip_spaces(line, ke);
            if (at(line, j) != ']') { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("malformed table header (expected ']')"); }
            if      (key_is(name, "artifact"))     sec = SEC_ARTIFACT;
            else if (key_is(name, "dependencies")) sec = SEC_DEPS;
            else if (key_is(name, "aliases"))      sec = SEC_ALIASES;
            else if (key_is(name, "coverage"))     sec = SEC_COVERAGE;
            else                                   sec = SEC_OTHER;
            continue;
        }

        // a `key = value` pair
        size_t ke;
        tk_str key = read_key(line, i, &ke);
        if (key.len == 0) { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("malformed line (expected a key or '[table]')"); }
        size_t eq = skip_spaces(line, ke);
        if (at(line, eq) != '=') { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("malformed key/value (expected '=')"); }
        size_t v = skip_spaces(line, eq + 1);

        switch (sec) {
        case SEC_ROOT: {
            // name / source / version / suffix are top-level quoted strings.
            tk_str val; size_t ve;
            if (!read_quoted(line, v, &val, &ve)) { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("expected a quoted string value"); }
            if      (key_is(key, "name"))    { m.name = val;   have_name = true; }
            else if (key_is(key, "source"))  { m.source = val; have_source = true; }
            else if (key_is(key, "version")) { m.version = val; }
            else if (key_is(key, "suffix"))  { m.suffix = val; }
            // unknown top-level keys are ignored (forward-compatible — M.5).
            break;
        }
        case SEC_COVERAGE: {
            // [coverage] functions / lines / branches — UNQUOTED integer floors (default 80 each).
            if      (key_is(key, "functions")) m.cov_functions = read_int(line, v);
            else if (key_is(key, "lines"))     m.cov_lines     = read_int(line, v);
            else if (key_is(key, "branches"))  m.cov_branches  = read_int(line, v);
            break;
        }
        case SEC_ARTIFACT: {
            // `kind = "binary"` → Executable; anything else (incl. "library") → Library.
            if (key_is(key, "kind")) {
                tk_str val; size_t ve;
                if (!read_quoted(line, v, &val, &ve)) { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("expected a quoted string value"); }
                m.artifact = key_is(val, "binary") ? TK_ARTIFACT_EXECUTABLE : TK_ARTIFACT_LIBRARY;
            }
            break;
        }
        case SEC_DEPS:    m.deps    = tk_strs_push(m.deps, key);    break;
        case SEC_ALIASES: m.aliases = tk_strs_push(m.aliases, key); break;
        case SEC_OTHER:   break;   // an unrecognized table — keys ignored (M.5)
        }
    }

    if (!have_name)   { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("manifest missing required field 'name'"); }
    if (!have_source) { tk_strs_free(m.deps); tk_strs_free(m.aliases); return fail("manifest missing required field 'source'"); }

    return (tk_manifest_result){ .ok = true, .as.value = m };
}
