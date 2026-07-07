// src/runtime/teko_rt.h   (namespace 'teko::runtime')
// libteko_rt: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#ifndef TEKO_RT_H
#define TEKO_RT_H

#include <stdint.h>   // uint8_t, int64_t
#include <stddef.h>   // size_t
#include <stdbool.h>  // bool
#include <stdlib.h>   // realloc, free — needed by TK_RT_LIST


// ─── C7.14: concrete list types (TK_RT_LIST) ────────────────────────────────
// TK_RT_LIST(T, Name) — the runtime analogue of core.h's TK_LIST. Generates a
// typed growable array struct and three static-inline helpers with ZERO overhead
// at the call site (all inlined). Growth uses realloc/free directly (stdlib.h)
// because this header is self-contained and must not include core.h (which
// re-declares tk_alloc as static-inline, conflicting with the extern below).
// OOM panics via abort() — same M.1 fail-loud contract as tk_alloc itself.
// `push` CONSUMES and RETURNS (xs = Name##_push(xs, item)); the caller holds the
// one live copy. `free` releases the backing buffer without touching elements.
#define TK_RT_LIST(T, Name)                                                   \
    typedef struct { T *ptr; size_t len; size_t cap; } Name;                  \
    static inline Name Name##_empty(void) {                                   \
        return (Name){ .ptr = NULL, .len = 0, .cap = 0 };                    \
    }                                                                         \
    static inline Name Name##_push(Name xs, T item) {                        \
        if (xs.len == xs.cap) {                                               \
            size_t ncap = (xs.cap == 0) ? 8 : (xs.cap * 2);                  \
            void *p = realloc(xs.ptr, ncap * sizeof(T));                      \
            if (p == NULL) { abort(); }                                       \
            xs.ptr = (T *)p; xs.cap = ncap;                                   \
        }                                                                     \
        xs.ptr[xs.len] = item;                                                \
        xs.len = xs.len + 1;                                                  \
        return xs;                                                            \
    }                                                                         \
    __attribute__((unused)) static inline void Name##_free(Name xs) { free(xs.ptr); }   /* (#151) many instantiations never free (arena-era callers) — not a warning */

// byte — one octet (mirrors src/text/text.h's tk_byte; same rep).
typedef uint8_t tk_byte;

// str — a VIEW into UTF-8 bytes; len is in BYTES, NOT NUL-terminated.
// Identical shape to src/text/text.h's tk_str.
typedef struct {
    const tk_byte *ptr;   // the bytes
    size_t         len;   // length in BYTES
} tk_str;

// char — a UTF-8 codepoint (1–4 bytes). DISTINCT type (its own checker tag), but its runtime
// layout is the SAME byte-slice shape the codegen lifts a `[]byte` to (`{uint8_t*,uint64_t}` ==
// tk_slice_byte). A `c'…'` literal lowers to a static byte array + this view. Decode to the scalar
// codepoint via tk_char_to_u32 (the explicit `char to u32`/u64/i64 cast).
typedef struct {
    uint8_t  *ptr;        // the codepoint's UTF-8 bytes
    uint64_t  len;        // length in BYTES (1–4)
} tk_char;

// tk_slice_byte — the runtime layout of a `[]byte` slice. Same {ptr,len} shape as tk_char
// (both are byte arrays). Pre-declared here so tk_bytes_of_str can use it in teko_rt.h
// without depending on the generated program's per-file typedef.
typedef struct {
    tk_byte  *ptr;
    uint64_t  len;
} tk_slice_byte;

// closure / function VALUE (W10a). The uniform runtime representation of a value of a function
// type `(A, B) -> R`: a code pointer plus a captured-environment pointer. For a NAMED fn used as a
// value (W10a) `env` is NULL and `fn` is the C function's address; calls cast `fn` back to the
// statically-known C signature `R (*)(A, B)` and invoke it. The `env` field is reserved for W10b
// capturing closures (so the struct shape never changes when captures land).
typedef struct {
    void *fn;     // the code pointer (a C function address)
    void *env;    // captured environment (NULL for a named fn / non-capturing closure)
} tk_closure;

// error — the Teko built-in error-as-value (E2-NATIVE). Mirrors core.h's tk_error so that
// generated programs carry full diagnostic adornments in native just as the VM does.
// A message-only `error { message = "…" }` literal leaves file/line/col/expected/actual
// at their zero/NULL defaults (C1.3 additive — all existing sites are unchanged).
typedef struct {
    tk_str     message;    // the error message (required)
    tk_str     file;       // source file (empty = unknown)
    uint32_t   line;       // 1-based line (0 = unknown)
    uint32_t   col;        // 1-based column (0 = unknown)
    tk_str     expected;   // rendered expected type (empty = n/a)
    tk_str     actual;     // rendered actual type   (empty = n/a)
} tk_error;

// Constructors — message-only (minimal) and the two diagnostic adornment helpers that
// mirror core.h's tk_error_loc / tk_error_types (E2).
static inline tk_error tk_error_make(tk_str message) {
    tk_error e; e.message = message; e.file = (tk_str){0}; e.line = 0; e.col = 0;
    e.expected = (tk_str){0}; e.actual = (tk_str){0}; return e;
}
static inline tk_error tk_error_loc(tk_error e, uint32_t line, uint32_t col) {
    e.line = line; e.col = col; return e;
}
static inline tk_error tk_error_types(tk_error e, tk_str expected, tk_str actual) {
    e.expected = expected; e.actual = actual; return e;
}

// C7.14: concrete list type instantiations (must come AFTER tk_byte/tk_str are defined).
// Generated programs and the compiler itself use these typed lists. The Teko-surface
// equivalents are `[]str`, `[]byte`, `[]i64` with `teko::list::push` / `teko::list::empty`
// (see teko_rt.tks § C7.14 comment block).
TK_RT_LIST(tk_byte,   tk_byte_list)    // []byte  — byte builder, str of bytes, etc.
TK_RT_LIST(tk_str,    tk_str_list)     // []str   — string accumulator lists (argv, paths, …)
TK_RT_LIST(int64_t,   tk_i64_list)     // []i64   — integer accumulator lists

// tk_alloc — the allocation seam (S0→S1). Hands back a fresh, uniquely-addressable block of
// ≥ n usable bytes (n→1 when 0 so the result is unique), aligned to the arena's alignment
// (TK_ARENA_ALIGN in teko_rt.c: max of max_align_t's and __int128's, so __int128-carrying
// Expr/TExpr nodes are correctly aligned even where max_align_t's alignment is only 8);
// tk_panic on OOM (M.1, never NULL). Generated code allocates through this: slice
// copy-append AND the auto-boxed recursive-value-type back-edges (tk_alloc(sizeof *p)).
// (S1) The body now bump-allocates from the process ROOT region (tk_region_root) instead
// of calling malloc directly — the swap is mechanical, the contract is unchanged, and the
// root is never dropped so the leak profile is identical to malloc-everywhere (M.5).
// LINKAGE NOTE: this is the EXTERN runtime tk_alloc, distinct from core.h's static-inline
// tk_alloc (internal linkage, used by the compiler/VM TUs over libc). The two never merge;
// do NOT give core.h's tk_alloc external linkage nor #include this header into a vm.c-linked
// TU — that would route VM allocations to the arena while their tk_free0 stays libc (corruption).
void *tk_alloc(size_t n);

// ── Arena allocation (S1 — TEKO_EVOLUTION_DESIGN §5.2: arena primitive + root region) ──
// A bump-allocator REGION: a chunk-list of aligned-malloc'd blocks, sub-allocated by a bump
// offset. No per-object metadata, no free-list (M.0 metal/no-GC). region_alloc results are
// TK_ARENA_ALIGN-aligned (an over-aligned chunk payload, NOT merely malloc's guarantee — see
// teko_rt.c), so every type that was malloc-stored — including __int128-carrying nodes — stays
// correctly aligned. OOM panics (M.1, never NULL). region_drop bulk-frees the whole span in
// one pass (the S2 keystone). The process ROOT region (tk_region_root) is never dropped in
// S1 → its memory lives for the whole process = today's malloc-everywhere leak (M.5
// leak-tolerant), so routing tk_alloc through it is behavior-preserving (the only divergence
// is the OOM boundary, which shifts by the per-chunk header — still fail-loud, M.1).
// S1 reroutes ONLY this runtime seam; core.h's compiler seam stays on libc until S2's
// realloc-aware list migration (the arena has no per-block size header, so a drop-in realloc
// needs old-size threaded through TK_LIST — an S2-scope change).
#define TK_REGION_DEFAULT_CHUNK (64u * 1024u)   // default chunk payload (bytes)
typedef struct tk_region tk_region;             // opaque — full struct lives in teko_rt.c
// (S2 — arena PARENT-PTR TREE + per-arena type→instance registry) tk_region_new now takes the
// enclosing region as its PARENT (NULL for a parentless/root region). Codegen threads the
// currently-active region variable (the function frame, or the innermost block region) at every
// call site, so the tree mirrors the LEXICAL nesting that used to be implicit in C scoping only.
// This is the prerequisite for a future DI `#scoped` lifetime: tk_region_lookup walks UP the
// parent chain to find an already-materialized ancestor instance before tk_region_register makes
// a new one in the CURRENT arena (never an ancestor's — registration is always local).
tk_region *tk_region_new(tk_region *parent);    // a fresh empty region (default chunk size), child of `parent` (NULL = no parent)
void      *tk_region_alloc(tk_region *r, size_t n);  // bump-allocate n (n→1), aligned; OOM→panic
void       tk_region_drop(tk_region *r);        // bulk-free every chunk + the region (NULL-tolerant; idempotent on a re-walk — head is cleared before free; callers MUST null their handle after, as the freed region must not be reused)
void       tk_region_drop_subtree(tk_region *root);  // (#337) the `adopt` bulk-drop: drop `root` AND every live region whose ->parent chain reaches it, in one sweep (cycles among objects irrelevant); NULL-tolerant; callers MUST null their handle after
tk_region *tk_region_root(void);                // the process root region (lazy; never dropped in S1; parent = NULL — the tree root)
// (#109 test-gate memory) checkpoint/rewind the ROOT region's bump position, bulk-freeing everything
// it allocated in between. Balanced push/pop; used by the test-gate runner to bound per-test memory.
void       tk_arena_push(void);                 // save the root region's current bump position
void       tk_arena_pop(void);                  // free every root-region chunk allocated since the matching push
// tk_region_register — bind `type_id` → `instance` in `r`'s OWN table (never an ancestor's; a
// second registration of the same type_id in the same region OVERWRITES — the compiler is
// expected to enforce true duplicate-registration errors at a higher DI layer; this is just the
// storage primitive).
void       tk_region_register(tk_region *r, uint64_t type_id, void *instance);
// tk_region_lookup — find `type_id`'s instance in `r`, else its parent, else its parent's parent,
// … until found or the chain ends (NULL). The #scoped walk-up primitive.
void      *tk_region_lookup(tk_region *r, uint64_t type_id);
// (W9.3b) tk_regions_free_all — free EVERY still-live region (the root + every live scoped frame/block
// region) and empty the registry. Called at the termination choke points (tk_panic*, tk_exit, and an
// atexit hook lazily registered in tk_region_root) so that an abnormal exit/panic does not leak the
// stack-local scoped regions that a diverging path skips dropping. Idempotent: after it runs the
// registry is empty, so a second call (e.g. atexit after an explicit panic/exit call) is a no-op.
void       tk_regions_free_all(void);

// tk_print — write exactly s.len bytes from s.ptr to stdout; no newline, no NUL.
void tk_print(tk_str s);
// tk_println — tk_print(s) then a single '\n' (0x0A).
void tk_println(tk_str s);
// Host output FFI bottoms (scope.c write/ewrite/eprint/eprintln) — s.len bytes, NUL-tolerant.
// write → stdout; ewrite/eprint → stderr; eprintln → stderr + '\n'.
void tk_write(tk_str s);
void tk_ewrite(tk_str s);
void tk_eprint(tk_str s);
void tk_eprintln(tk_str s);
// teko::float::parse(str) -> f64 (strtod over a NUL-terminated copy; non-numeric → 0.0).
double tk_float_parse(tk_str s);

// --- string interpolation `$"…{expr}…"` builders (self-host parity) ---
// These are EXTERN (linked from teko_rt.c), NOT static inline — both the generated C and
// the VM call them, and the VM forward-declares them (like tk_print) rather than including
// this header. Leak-tolerant (M.5 — the results are short-lived process-lifetime buffers).
//
// tk_str_concat — a fresh str holding a's bytes then b's bytes; the result OWNS the buffer.
tk_str tk_str_concat(tk_str a, tk_str b);
// (C7.1a) FFI marshalling: the raw byte pointer of a str (teko::mem::as_ptr — borrows, ptr+len
// use), a fresh NUL-terminated C copy of a str (teko::mem::as_cstr), and a copy of a
// NUL-terminated foreign C string back into a fresh str (teko::mem::str_from_cstr).
void *tk_as_ptr(tk_str s);
void *tk_cstr_dup(tk_str s);
tk_str tk_str_from_cstr(const void *p);
// (tk_bytes_from_ptr is declared after tk_ffi_bytes, below — its return type is defined there.)
// tk_i64_to_str / tk_u64_to_str — the integer's DECIMAL text in a fresh str. The interp
// lowering widens every signed int hole to i64 and every unsigned hole to u64 (every Teko
// integer prim except u128/i128 fits; the checker scopes holes to what the corpus needs).
tk_str tk_i64_to_str(int64_t v);
tk_str tk_u64_to_str(uint64_t v);

// --- Phase 3 str/byte stdlib (the four recognized-but-not-yet-lowered builtins) ---
// Same contract as tk_str_concat: a fresh malloc'd buffer the result OWNS, tk_panic on OOM
// (M.1), leak-tolerant (M.5 — short-lived).
//
// tk_str_of_bytes — a fresh str COPYING the bytes of a []byte slice. A []byte slice has the
// SAME {ptr,len} shape as tk_str, so codegen passes the slice value directly; this copies it
// into a fresh owned buffer (the `str_of_bytes` builtin; the `str` builtin aliases it).
tk_str tk_str_of_bytes(tk_str bytes);
// tk_one_byte — a fresh 1-byte str holding c.
tk_str tk_one_byte(tk_byte c);
// tk_bytes_of_str — zero-copy view of a str's bytes as a []byte slice. Returns a tk_slice_byte
// pointing into the same memory. The caller must not outlive the originating str allocation.
tk_slice_byte tk_bytes_of_str(tk_str s);
// tk_char_to_u32 — decode a `char` (its 1–4 UTF-8 bytes) to the scalar codepoint value. The bytes
// are valid UTF-8 by construction (the lexer validated the literal), so this is a pure decode.
uint32_t tk_char_to_u32(tk_char c);
// tk_str_len_chars — count UTF-8 codepoints in s. Walks the byte sequence using lead-byte widths;
// no allocation, no copy. (The `len_chars` builtin lowers to this.)
uint64_t tk_str_len_chars(tk_str s);
// tk_str_chars — split s into a heap-allocated array of tk_char (one per UTF-8 codepoint).
// Each tk_char.ptr borrows INTO s.ptr (no copy of the codepoint bytes); the outer array is
// malloc'd. Returns {ptr, len} where len == tk_str_len_chars(s). OOM panics (M.1).
// (The `chars` builtin lowers to this; the generated C holds the result as tk_slice_char.)
typedef struct { tk_char *ptr; uint64_t len; } tk_slice_char;
tk_slice_char tk_str_chars(tk_str s);
// tk_str_concat3 REMOVED (2026-07-01) — superseded by `concat(params pieces: []str)`, bridged
// at the call site (codegen.c/.tks) by folding N pieces via tk_str_concat; no runtime symbol needed.
// tk_ftoa — x rendered as %.17g float text (exact binary64 round-trip) in a fresh str.
tk_str tk_ftoa(double x);

// --- Format spec helpers ($"{x:F2}" / $"{x:[fmt]}") ---
// All return fresh malloc'd str (tk_panic on OOM).  spec codes:
//   F{n}  fixed-point n decimal places (default 6)
//   D{n}  zero-padded integer to n digits
//   X/x   hex uppercase/lowercase
//   E{n}  scientific notation
//   N{n}  thousands-separator float;  N (no digit) = integer with thousands-sep
//   G{n}  shorter of F/E (snprintf %g)
//   B     binary
//   P{n}  percentage (val*100, n decimal places)
tk_str tk_fmt_f(double val, int prec);
tk_str tk_fmt_d(int64_t val, int width);
tk_str tk_fmt_x_upper(uint64_t val);
tk_str tk_fmt_x_lower(uint64_t val);
tk_str tk_fmt_e(double val, int prec);
tk_str tk_fmt_n_f(double val, int prec);
tk_str tk_fmt_n_i(int64_t val);
tk_str tk_fmt_g(double val, int prec);
tk_str tk_fmt_b(uint64_t val);
tk_str tk_fmt_p(double val, int prec);
// Dynamic dispatchers: parse spec at runtime (first char + optional digits).
tk_str tk_fmt_dyn_f64(double val, tk_str spec);
tk_str tk_fmt_dyn_i64(int64_t val, tk_str spec);
tk_str tk_fmt_dyn_u64(uint64_t val, tk_str spec);

// --- Phase 3 str query/slice builtins (the checker types these via tk_builtin_fn) ---
// The query helpers (eq/ends_with/contains/len) do NO allocation; the slice helpers return a
// FRESH owned buffer (same ownership as tk_str_concat), tk_panic on OOM/out-of-range (M.1).
//
// tk_str_eq — true iff a and b have the same length and the same bytes (memcmp; embedded NUL
// tolerated). No allocation.
bool tk_str_eq(tk_str a, tk_str b);
// (TR3) tk_str_hash — FNV-1a over the str's bytes (offset basis 14695981039346656037, prime
// 1099511628211, u64 wraparound); an empty str hashes to the offset basis. No allocation.
uint64_t tk_str_hash(tk_str s);
// (TR3) tk_str_cmp — lexicographic byte compare (unsigned): -1 (a < b) / 0 (equal) / 1 (a > b);
// the shorter str is the lesser when one is a prefix of the other. No allocation.
int64_t tk_str_cmp(tk_str a, tk_str b);
// tk_str_slice — the substring bytes [start, end) as a fresh owned str. An out-of-range slice
// (start > end, or end > s.len) PANICS (M.1, parity with the VM's bounds check). The empty
// slice (start == end) is a valid empty str (1-byte buffer so ptr is never NULL+stale len).
tk_str tk_str_slice(tk_str s, uint64_t start, uint64_t end);
// tk_str_slice_to — tk_str_slice(s, 0, end).
tk_str tk_str_slice_to(tk_str s, uint64_t end);
// tk_str_slice_from — tk_str_slice(s, start, s.len).
tk_str tk_str_slice_from(tk_str s, uint64_t start);
// tk_str_len — s.len (the byte length). No allocation.
uint64_t tk_str_len(tk_str s);
// tk_str_ends_with — true iff s ends with suffix (suffix.len <= s.len and the tail bytes
// match). No allocation.
bool tk_str_ends_with(tk_str s, tk_str suffix);
// tk_str_contains — true iff needle occurs in s (naive byte search; an empty needle → true).
// No allocation.
bool tk_str_contains(tk_str s, tk_str needle);
// tk_f64_g17 — x rendered as %.17g into a fresh owned str (the host float renderer; the same
// renderer as tk_ftoa, exposed under the `f64_g17` name the checker/codegen reference).
tk_str tk_f64_g17(double x);

// --- ROUND 0: UTF-8 codepoint operations (char_at / str_slice_chars / is_alpha / is_digit /
//     is_space / to_lower / to_upper). Mirrored in scope.c/.tks, codegen.c/.tks, vm.c/.tks. ---
//
// tk_char_at — the UTF-8 codepoint at 0-based codepoint index i in s. Panics if out of range.
// Returns a tk_char view INTO s.ptr (no copy); the caller must ensure s outlives the result.
tk_char tk_char_at(tk_str s, int64_t i);
// tk_str_slice_chars — the substring from codepoint index `from` (inclusive) to `to` (exclusive),
// returned as a FRESH owned str (copied). Panics if from > to or to > len_chars(s). (M.1)
tk_str tk_str_slice_chars(tk_str s, int64_t from, int64_t to);
// tk_is_alpha — true if the codepoint is a Unicode letter. For single-byte ASCII uses isalpha(3);
// for multibyte codepoints (lead byte ≥ 0x80) returns true (simplified ROUND 0 rule).
bool tk_is_alpha(tk_char c);
// tk_is_digit — true iff the codepoint is an ASCII decimal digit '0'–'9'.
bool tk_is_digit(tk_char c);
// tk_is_space — true iff the codepoint is ASCII whitespace (' ', '\t', '\n', '\r', '\f', '\v').
bool tk_is_space(tk_char c);
// tk_to_lower — ASCII lowercase: 'A'–'Z' → 'a'–'z'; non-ASCII chars are returned unchanged.
// Returns a tk_char that either borrows the source bytes (non-ASCII) or a static 1-byte store
// (ASCII — the result is valid for the program lifetime).
tk_char tk_to_lower(tk_char c);
// tk_to_upper — ASCII uppercase: 'a'–'z' → 'A'–'Z'; non-ASCII chars are returned unchanged.
tk_char tk_to_upper(tk_char c);

// =========================================================================
// Host-FFI + arithmetic bottoms (Phase 7 / scope.c builtin_fn surface).
//
// THE LIFTING SEAM. The host I/O builtins return Teko `T | error` / `error?`
// sums and `[]str`. Those are GENERATED struct types (tk_u_str_error,
// tk_opt_error, tk_slice_str, …) defined in the emitted C, AFTER this header is
// #included — so the runtime CANNOT name them. Instead each primitive returns a
// FIXED-ABI result struct over header-knowable types only (tk_str + scalars +
// tk_str*), and codegen lifts that into the program's result type (the
// emit_host_ffi statement-expression). `error` lowers to its message tk_str.
// =========================================================================

// str | error  (read_file, var): ok → value; !ok → err (the message).
typedef struct { bool ok; tk_str value; tk_str err; } tk_ffi_sres;
// error?  (write_file, chdir): ok → success (no error); !ok → err present.
typedef struct { bool ok; tk_str err; } tk_ffi_ures;
// []str | error  (list_dir): ok → {ptr,len} entries; !ok → err.
typedef struct { bool ok; tk_str *ptr; uint64_t len; tk_str err; } tk_ffi_slres;
// u64 | error  (last_index_of): ok → value; !ok → not found.
typedef struct { bool ok; uint64_t value; } tk_ffi_u64res;
// []byte  (bytes_from_ptr): a {ptr,len} the codegen lifts to the generated tk_slice_byte (C7.1a).
typedef struct { tk_byte *ptr; uint64_t len; } tk_ffi_bytes;
// (C7.1a) copy n octets from a foreign pointer into a fresh []byte (teko::mem::bytes_from_ptr).
tk_ffi_bytes tk_bytes_from_ptr(const void *p, uint64_t n);

// str_from_utf8(bytes) — the validated bytes -> str constructor (ROUND 0 / B.36). ok → a fresh
// str COPYING the bytes; !ok → err "invalid UTF-8". Reuses tk_ffi_sres (same {ok,value,err}
// shape as read_file/getenv). Takes ptr+len (the []byte ABI), mirroring write_file_bytes's arg.
tk_ffi_sres tk_rt_str_from_utf8(const tk_byte *ptr, uint64_t len);

// teko::io::read_file(path) — slurp the whole file as UTF-8 bytes (owned copy).
tk_ffi_sres tk_rt_read_file(tk_str path);
// (DT3) teko::io::read_line() — read one LINE from stdin (the trailing '\n'/"\r\n" stripped),
// an owned copy — empty when stdin has no more input (check tk_rt_stdin_eof() to tell that
// apart from a genuine blank line). A DIRECT `str` return (no {ok,value,err} lift) so this
// brand-new primitive stays lowerable by every codegen generation, including the released
// bootstrap seed's frozen codegen.c, which can only special-case ALREADY-KNOWN {ok,value,err}
// shapes by name (mirrors tk_rt_os/tk_rt_version's already-working plain-str shape).
tk_str tk_rt_read_line(void);
// (DT3) teko::io::stdin_eof() — did the LAST read_line() hit real EOF (stdin fully exhausted)?
bool tk_rt_stdin_eof(void);
// (#229) teko::io::read_stdin() — slurp all of stdin until EOF (owned copy), for `teko fmt -`.
// A bare tk_str (no error union — see tk_rt_read_stdin's definition for why); panics on a
// genuine read failure.
tk_str tk_rt_read_stdin(void);
// teko::env::var(name) — the environment value, or error when unset.
tk_ffi_sres tk_rt_getenv(tk_str name);
// teko::io::write_file(path, content) — (over)write the file; error on failure.
tk_ffi_ures tk_rt_write_file(tk_str path, tk_str content);
// teko::io::write_file_bytes(path, data) — write a raw []byte slice to the file; error on failure.
// Takes the byte list as a ptr+len pair (the C7.14 tk_byte_list ABI). Shares the same
// write-path as tk_rt_write_file; accepts binary data (not UTF-8-restricted).
tk_ffi_ures tk_rt_write_file_bytes(tk_str path, const tk_byte *ptr, uint64_t len);
// teko::env::chdir(path) — change the process working directory; error on failure.
tk_ffi_ures tk_rt_chdir(tk_str path);
// teko::fs::mkdir(path) — create a directory (mode 0755); SUCCESS if it already exists.
tk_ffi_ures tk_rt_mkdir(tk_str path);
// (issue #79) teko::fs::remove_file(path) — delete a file (libc remove); SUCCESS if already absent.
tk_ffi_ures tk_rt_remove_file(tk_str path);
// teko::env::cwd() — the current working directory as an owned absolute path, or error.
tk_ffi_sres tk_rt_getcwd(void);
// teko::env::set_var(name, value) — set an environment variable; error on failure.
tk_ffi_ures tk_rt_setenv(tk_str name, tk_str value);
// teko::fs::list_dir(path) — the directory entries (excluding "." / ".."), or error.
tk_ffi_slres tk_rt_list_dir(tk_str path);
// teko::str::last_index_of(hay, needle) — byte index of the LAST occurrence, or not-found.
tk_ffi_u64res tk_rt_last_index_of(tk_str hay, tk_str needle);
// teko::process::run(argv) — fork/exec argv[0] with argv, wait, return its exit status
// (127 when argv is empty / exec fails). Takes the slice as ptr+len (no generated type).
int32_t tk_rt_run(const tk_str *argv, uint64_t n);
// teko::process::run_quiet(argv) — same contract as tk_rt_run, but the child's stdout/stderr
// are redirected to the null device (issue #73: the cc flag-family probe uses this so a
// deliberately-rejected compiler flag doesn't print to the user's build output).
int32_t tk_rt_run_quiet(const tk_str *argv, uint64_t n);
// teko::env::args() — the captured process argv as owned tk_str's; *n receives the count.
// tk_set_args must run first (the generated `main` calls it before the virtual-main body).
void    tk_set_args(int argc, char **argv);
tk_str *tk_rt_args(uint64_t *n);
// (C7.1f) the host OS name: "macos"/"linux"/"windows"/"unknown" (teko::os; per-OS resolution + `#os`).
tk_str tk_rt_os(void);
// (CLI --version) the build's version string — the RAW project-manifest `version` + `-<suffix>`
// (e.g. "0.0.1.0-bootstrap"). Compiled from the TEKO_VERSION_STRING define injected by both build
// paths (CMake for the bootstrap, run_cc for self-host), never a runtime file read. (teko::env::version)
tk_str tk_rt_version(void);
// (#148) the process peak RSS in bytes (0 = unavailable) — teko::mem::peak_rss.
uint64_t tk_peak_rss(void);

// (#194 C6) teko::crypto::rand::secure_bytes(n) — n cryptographically-secure random bytes
// from the host CSPRNG (getrandom(2)/getentropy(3) on POSIX, rand_s (ucrt) on Windows).
// Returns a fresh owned buffer of EXACTLY n bytes (n == 0 -> a valid empty slice, ptr may be
// NULL); tk_panic on a genuine host entropy failure (M.1 — silently returning weak/short
// output is a security defect, never a soft error here).
tk_slice_byte tk_rt_secure_bytes(uint64_t n);

// ---- Date/Time placeholder types (ROUND 0) ----
// Five value types: DateTime (signed ns since Unix epoch), TimeSpan (signed ns duration),
// Time (ns since midnight), Date (days since 1970-01-01 = 0), DateTimeOffset (DateTime + offset).
// DateTime.ticks is i128 (signed) so DateTime - DateTime always fits in TimeSpan (i128).
typedef struct { __int128  ticks;                         } tk_datetime;
typedef struct { __int128  ticks;                         } tk_timespan;
typedef struct { uint64_t  ticks;                         } tk_time;
typedef struct { int32_t   days;                          } tk_date;
typedef struct { __int128  ticks; int16_t offset_minutes; } tk_datetimeoffset;

tk_datetime       tk_rt_datetime_now(void);
tk_datetimeoffset tk_rt_datetime_local_now(void);
tk_date           tk_rt_date_today(void);
tk_time           tk_rt_time_now_utc(void);
tk_timespan       tk_rt_timespan_from_ns(int64_t ns);
tk_date           tk_rt_date_from_days(int32_t days);

__int128 tk_rt_datetime_to_unix_ns(tk_datetime dt);
int32_t  tk_rt_date_year(tk_date d);
int32_t  tk_rt_date_month(tk_date d);
int32_t  tk_rt_date_day_of_month(tk_date d);
int32_t  tk_rt_time_hour(tk_time t);
int32_t  tk_rt_time_minute(tk_time t);
int32_t  tk_rt_time_second(tk_time t);
int16_t  tk_rt_dto_offset_minutes(tk_datetimeoffset dto);

// --- arithmetic (ROUND 0) ---
// DateTime +/- TimeSpan = DateTime; DateTime - DateTime = TimeSpan (always fits, both i128).
// TimeSpan +/- TimeSpan = TimeSpan. Date +/- (days: i32) = Date. Comparisons via raw ticks/days
// (exposed already through the accessors above) — no dedicated compare fn needed (Teko can
// compare the extracted i128/i32 with native `<`/`==`).
tk_datetime tk_rt_datetime_add(tk_datetime dt, tk_timespan span);
tk_datetime tk_rt_datetime_sub(tk_datetime dt, tk_timespan span);
tk_timespan tk_rt_datetime_diff(tk_datetime a, tk_datetime b);      // a - b
tk_timespan tk_rt_timespan_add(tk_timespan a, tk_timespan b);
tk_timespan tk_rt_timespan_sub(tk_timespan a, tk_timespan b);
__int128    tk_rt_timespan_to_ns(tk_timespan span);
tk_date     tk_rt_date_add_days(tk_date d, int32_t days);
int32_t     tk_rt_date_diff_days(tk_date a, tk_date b);             // a - b, in days

// D3 — TEST-COVERAGE SINK. A host side-channel (like print's buffer / args), so the VM can
// record which production functions executed during a `teko test` run WITHOUT a Teko
// module-mutable (M.0). The VM marks a function's id (its source line) on entry; the runner
// reads the distinct count afterward to compute function-level coverage.
void     tk_cov_reset(void);        // clear the executed-id set (call before a test run)
void     tk_cov_mark(uint64_t id);  // record an executed id (deduped)
uint64_t tk_cov_distinct(void);     // how many distinct ids were marked
bool     tk_cov_is_marked(uint64_t id);   // was this exact id marked?

// D3-branch — branch coverage (only recorded when ON; off by default). enter/leave maintain the
// current-fn stack so a branch id can pack (fn, line, col, outcome); the report queries hits.
void     tk_cov_branches_on(bool on);     // enable/disable branch recording
void     tk_cov_branch_reset(void);       // clear branch marks + fn stack
void     tk_cov_enter(uint64_t fn);       // push the entered fn's items-index
void     tk_cov_leave(void);              // pop it
void     tk_cov_branch(uint32_t line, uint32_t col, uint64_t outcome);   // mark a taken branch outcome (current fn)
bool     tk_cov_branch_hit(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome);  // report query

// D3-line — LINE coverage (marked on every evaluated expression; a hash set, only when ON).
void     tk_cov_lines_on(bool on);
void     tk_cov_line_reset(void);
void     tk_cov_line(uint32_t line);                       // mark a line as executed (current fn)
bool     tk_cov_line_hit(uint64_t fn, uint32_t line);      // report query

// #265 (Track A) — EXPLICIT-fn line/branch marks for the native test gate. The VM keeps a live
// enter/leave fn-stack (eval_call), so tk_cov_line/tk_cov_branch read tk_fn_stack[sp-1]. The native
// test binary has NO enter/leave inside production bodies, so codegen passes the owning fn's
// prog.items index EXPLICITLY, bypassing the stack — every interior mark keys on the fn the static
// floor walk (line_coverage/branch_coverage) queries. Same tk_line_id/tk_branch_id packing.
void     tk_cov_line_at(uint64_t fn, uint32_t line);                             // mark a line for fn (explicit)
void     tk_cov_branch_at(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome);  // mark a branch for fn (explicit)

// #265 — cross-process coverage merge for the NATIVE test gate. The child test binary dumps its three
// sinks to a `.tkcov` file at exit; the compiler (parent) merges them, then runs the unchanged static
// walk + floors. The coverage id is the shared prog.items index, so the packed ids are portable.
void     tk_cov_dump(const char *path);    // write the three sinks to a `.tkcov` file (child, cov-on)
bool     tk_cov_merge(tk_str path);        // union a `.tkcov` into this process's sinks (parent)

// tk_slice_push — the AMORTIZED lowering of `teko::list::push` (a `[]T` grow-by-one). The
// language keeps value semantics (fixed slices, copy-to-grow — collections #2); this is purely a
// codegen optimization so a LINEAR `b = push(b, x)` chain (the codegen output buffer is 1.4MB+
// built this way) is O(1) amortized instead of O(n) copy-every-push → O(n²). A small cache of
// recent "live tails" lets an in-place append happen when `ptr` is the current end of a buffer
// this function grew (matched by ptr + length witness + element size, with spare capacity); ANY
// other slice — a stale/shorter version, a different or untracked buffer, or a full one — COPIES
// (geometric growth), so aliased/branched buffers never observe each other's appends (value-safe).
// `elem` points at one element of `esz` bytes; `*out_len` receives the new length; returns the
// (possibly same) data pointer.
void *tk_slice_push(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len);
// (S2 Level-1) region-aware variant — the grown buffer is allocated in `region` (a function frame
// region `_tkfr`) instead of the process root, so a NON-escaping slice's whole buffer history is
// bulk-freed when the frame drops. `tk_slice_push` is the root-region wrapper over this. Codegen
// emits this only for a slice binding the escape analysis proves frame-local.
void *tk_slice_push_r(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len, tk_region *region);
// (#148 S2 Level-2) free-old-on-grow variant — for a self-append whose chain the checker PROVED
// linear: on a copy-grow the old buffer is PARKED on the free-list for reuse (realloc parity).
void *tk_slice_push_fo(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len);
// (#148 R2) bulk byte-append with free-old-on-grow BY DECREE (the linear cb emitter chain) — one
// memcpy per fragment; the old buffer parks for reuse the moment a grow replaces it.
void *tk_append_bytes_fo(const void *ptr, uint64_t len, const void *src, uint64_t n, uint64_t *out_len);
// (mem::free) tk_free_block — park an explicitly freed root-arena block for same-size REUSE
// (the `teko::mem::free` []T-arm lowering: `tk_free_block(s.ptr, s.len * sizeof(elem))`).
void tk_free_block(void *p, uint64_t bytes);

// --- arithmetic FFI over the i128 carrier (sign-aware) + float bit-patterns ---
// div/rem: truncated division/remainder; sgn selects signed vs unsigned interpretation.
__int128 tk_div(__int128 a, __int128 b, bool sgn);
__int128 tk_rem(__int128 a, __int128 b, bool sgn);
// fdiv: float division (the VM's f64 `/`); int_to_float: i128 (sgn-aware) → f64.
double   tk_fdiv(double a, double b);
double   tk_int_to_float(__int128 v, bool sgn);
// f64 ↔ raw IEEE-754 bit pattern (the .tkb float (de)serialization edge).
uint64_t tk_f64_bits(double x);
double   tk_f64_from_bits(uint64_t bits);

// tk_panic — fail loud (M.1): "teko: panic: <msg>\n" to stderr, then non-zero exit.
_Noreturn void tk_panic(const char *msg);
// the Teko-level globals `panic(str)` / `exit(<int>)` (legislator's ruling — no `never` type).
// tk_panic_str takes a tk_str (ptr+len, tolerates embedded NUL); tk_exit ends with a status code.
_Noreturn void tk_panic_str(tk_str msg);
_Noreturn void tk_exit(int32_t code);
_Noreturn void tk_panic_div0(void);       // "division by zero"
_Noreturn void tk_panic_oob(void);        // "index out of bounds"
_Noreturn void tk_panic_cast(void);       // "impossible conversion" (the `x to T` guard — B.36 / M.1)
_Noreturn void tk_panic_overflow(void);   // "integer overflow"
// (C1.7) positioned OOB panic — prefix "line:col: " then the canonical OOB panic. codegen passes
// the offending index node's position (C1-POS) so a NATIVE index-out-of-bounds locates like the VM
// (vm_panic_oob_at).
_Noreturn void tk_panic_oob_at(uint32_t line, uint32_t col);
// (C1.7-CAST) positioned cast panic — same shape as tk_panic_oob_at. codegen wraps every
// tk_to_* call in a statement-expression that sets these globals first; tk_panic_cast reads them.
// When line==0 the plain "impossible conversion" message is emitted (position unknown).
extern uint32_t _tk_cast_loc_line;
extern uint32_t _tk_cast_loc_col;

// teko::assert (the injected testing assertions) lives in its own C seed —
// src/assert/assert.{c,h} (canonical: src/assert/assert.tks). Generated programs that
// call them get the symbols because driver.c::run_cc compiles src/assert/assert.c
// alongside this runtime; the compiler lib links the same source via CMake.

// =========================================================================
// F3 runtime guards (M.1 — fail loud, never silent corruption / metal hazard):
// the backend emits calls to these in place of raw C `/`, `%`, `+`, `-`, `*`
// (integer only), and narrowing casts. All are SINGLE-EVALUATION (each argument
// is computed once by the caller and passed by value), so codegen never
// duplicates an operand subtree.
//
// COVERAGE RULE (M.3): conversion/division possibility is validated at RUNTIME;
// impossibility -> PANIC. Constants out of range are the checker's job (compile
// error) and are out of scope here.
//
// C7.15 — OVERFLOW GUARDS for +, -, * on INTEGER types:
// When TEKO_OVERFLOW_DEBUG is defined the helpers use __builtin_*_overflow
// (GCC/Clang) and call tk_panic_overflow() if overflow is detected.
// Without the flag they reduce to the plain C operation — zero overhead,
// identical to the old bare-operator path. Float +,-,* are NOT guarded
// (float overflow is not a Teko panic; only int arithmetic is guarded here).
// =========================================================================

// --- checked integer division / modulo: panic on a zero divisor (no UB / SIGFPE) ---
// One helper per signed/unsigned width (now incl. 128); codegen selects by the binary
// node's prim. (__int128 has no printf specifier but arithmetic on it is fine.)
static inline uint8_t  tk_div_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a / b); }
static inline uint16_t tk_div_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a / b); }
static inline uint32_t tk_div_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline uint64_t tk_div_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline unsigned __int128 tk_div_u128(unsigned __int128 a, unsigned __int128 b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int8_t   tk_div_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a / b); }
static inline int16_t  tk_div_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a / b); }
static inline int32_t  tk_div_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int64_t  tk_div_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline __int128 tk_div_i128(__int128 a, __int128 b){ if (b == 0) tk_panic_div0(); return a / b; }

static inline uint8_t  tk_mod_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a % b); }
static inline uint16_t tk_mod_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a % b); }
static inline uint32_t tk_mod_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline uint64_t tk_mod_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline unsigned __int128 tk_mod_u128(unsigned __int128 a, unsigned __int128 b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int8_t   tk_mod_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a % b); }
static inline int16_t  tk_mod_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a % b); }
static inline int32_t  tk_mod_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int64_t  tk_mod_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline __int128 tk_mod_i128(__int128 a, __int128 b){ if (b == 0) tk_panic_div0(); return a % b; }

// --- #49: width-masked shift <<, >> — defined result for an OUT-OF-RANGE count -------------
// Plain C `<<`/`>>` is UB when the shift count is >= the operand's bit-width (or negative); a
// Teko program that computes a large/negative-looking `u32`/`i64`/… count (e.g. from user input
// or arithmetic) must not hit native UB. Ruling: mask the count by (bit-width - 1) — C#/Java
// semantics — so `(1 as i32) << 40` == `1 << (40 & 31)` == `1 << 8` == 256, matching the VM
// (vm.c's eval_binary / vm.tks's apply_int_op) bit-for-bit. In-range counts are unaffected (the
// mask is a no-op when count < width). One helper per signed/unsigned width (u8..u128, i8..i128);
// codegen selects by the binary node's result prim (same tag as tk_div_*/tk_add_*). Signed `>>`
// keeps its existing sign-preserving (arithmetic) behavior — only the COUNT is fixed here.
static inline uint8_t  tk_shl_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a << (b & 7));   }
static inline uint16_t tk_shl_u16(uint16_t a, uint16_t b){ return (uint16_t)(a << (b & 15));  }
static inline uint32_t tk_shl_u32(uint32_t a, uint32_t b){ return a << (b & 31);              }
static inline uint64_t tk_shl_u64(uint64_t a, uint64_t b){ return a << (b & 63);              }
static inline unsigned __int128 tk_shl_u128(unsigned __int128 a, unsigned __int128 b){ return a << (b & 127); }
static inline int8_t   tk_shl_i8 (int8_t   a, int8_t   b){ return (int8_t )((uint8_t )a << ((uint8_t )b & 7));   }
static inline int16_t  tk_shl_i16(int16_t  a, int16_t  b){ return (int16_t)((uint16_t)a << ((uint16_t)b & 15));  }
static inline int32_t  tk_shl_i32(int32_t  a, int32_t  b){ return (int32_t)((uint32_t)a << ((uint32_t)b & 31));  }
static inline int64_t  tk_shl_i64(int64_t  a, int64_t  b){ return (int64_t)((uint64_t)a << ((uint64_t)b & 63));  }
static inline __int128 tk_shl_i128(__int128 a, __int128 b){ return (__int128)((unsigned __int128)a << ((unsigned __int128)b & 127)); }

static inline uint8_t  tk_shr_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a >> (b & 7));   }
static inline uint16_t tk_shr_u16(uint16_t a, uint16_t b){ return (uint16_t)(a >> (b & 15));  }
static inline uint32_t tk_shr_u32(uint32_t a, uint32_t b){ return a >> (b & 31);              }
static inline uint64_t tk_shr_u64(uint64_t a, uint64_t b){ return a >> (b & 63);              }
static inline unsigned __int128 tk_shr_u128(unsigned __int128 a, unsigned __int128 b){ return a >> (b & 127); }
static inline int8_t   tk_shr_i8 (int8_t   a, int8_t   b){ return (int8_t )(a >> (b & 7));   }
static inline int16_t  tk_shr_i16(int16_t  a, int16_t  b){ return (int16_t)(a >> (b & 15));  }
static inline int32_t  tk_shr_i32(int32_t  a, int32_t  b){ return a >> (b & 31);              }
static inline int64_t  tk_shr_i64(int64_t  a, int64_t  b){ return a >> (b & 63);              }
static inline __int128 tk_shr_i128(__int128 a, __int128 b){ return a >> (b & 127); }

// --- C7.15 overflow-guarded integer +, -, *: panic when TEKO_OVERFLOW_DEBUG is set ---
// One helper per signed/unsigned width (u8..u128, i8..i128). Float +,-,* are NOT here —
// float overflow is not a Teko error. Bool is not an arithmetic target.
// __builtin_add/sub/mul_overflow work on any integer type in GCC/Clang; the generic form
// `__builtin_add_overflow(a, b, &r)` selects the right overflow semantics for the type.
#ifdef TEKO_OVERFLOW_DEBUG
static inline uint8_t  tk_add_u8 (uint8_t  a, uint8_t  b){ uint8_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint16_t tk_add_u16(uint16_t a, uint16_t b){ uint16_t r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint32_t tk_add_u32(uint32_t a, uint32_t b){ uint32_t r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint64_t tk_add_u64(uint64_t a, uint64_t b){ uint64_t r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int8_t   tk_add_i8 (int8_t   a, int8_t   b){ int8_t   r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int16_t  tk_add_i16(int16_t  a, int16_t  b){ int16_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int32_t  tk_add_i32(int32_t  a, int32_t  b){ int32_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int64_t  tk_add_i64(int64_t  a, int64_t  b){ int64_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
// i128/u128: no standard builtin for 128-bit; use manual range-check.
static inline unsigned __int128 tk_add_u128(unsigned __int128 a, unsigned __int128 b){ unsigned __int128 r = a + b; if (r < a) tk_panic_overflow(); return r; }
static inline __int128 tk_add_i128(__int128 a, __int128 b){ __int128 r = a + b; if (((a ^ r) & (b ^ r)) < 0) tk_panic_overflow(); return r; }

static inline uint8_t  tk_sub_u8 (uint8_t  a, uint8_t  b){ uint8_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint16_t tk_sub_u16(uint16_t a, uint16_t b){ uint16_t r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint32_t tk_sub_u32(uint32_t a, uint32_t b){ uint32_t r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint64_t tk_sub_u64(uint64_t a, uint64_t b){ uint64_t r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int8_t   tk_sub_i8 (int8_t   a, int8_t   b){ int8_t   r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int16_t  tk_sub_i16(int16_t  a, int16_t  b){ int16_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int32_t  tk_sub_i32(int32_t  a, int32_t  b){ int32_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int64_t  tk_sub_i64(int64_t  a, int64_t  b){ int64_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline unsigned __int128 tk_sub_u128(unsigned __int128 a, unsigned __int128 b){ if (a < b) tk_panic_overflow(); return a - b; }
static inline __int128 tk_sub_i128(__int128 a, __int128 b){ __int128 r = a - b; if (((a ^ b) & (a ^ r)) < 0) tk_panic_overflow(); return r; }

static inline uint8_t  tk_mul_u8 (uint8_t  a, uint8_t  b){ uint8_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint16_t tk_mul_u16(uint16_t a, uint16_t b){ uint16_t r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint32_t tk_mul_u32(uint32_t a, uint32_t b){ uint32_t r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint64_t tk_mul_u64(uint64_t a, uint64_t b){ uint64_t r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int8_t   tk_mul_i8 (int8_t   a, int8_t   b){ int8_t   r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int16_t  tk_mul_i16(int16_t  a, int16_t  b){ int16_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int32_t  tk_mul_i32(int32_t  a, int32_t  b){ int32_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int64_t  tk_mul_i64(int64_t  a, int64_t  b){ int64_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
// u128 mul overflow: detect via division (no wider type available).
static inline unsigned __int128 tk_mul_u128(unsigned __int128 a, unsigned __int128 b){ if (a != 0 && b > ((unsigned __int128)-1)/a) tk_panic_overflow(); return a * b; }
// i128 mul overflow: unsigned-magnitude check with sign reconstruction.
static inline __int128 tk_mul_i128(__int128 a, __int128 b){
    unsigned __int128 ua = (a >= 0) ? (unsigned __int128)a : (unsigned __int128)(-a);
    unsigned __int128 ub = (b >= 0) ? (unsigned __int128)b : (unsigned __int128)(-b);
    // Signed max magnitude is 2^127-1 for same-sign and 2^127 for mixed-sign (INT128_MIN).
    // Use 2^127 as the cap: if ua*ub > 2^127 it always overflows (mixed yields INT128_MIN at equality).
    unsigned __int128 cap = (unsigned __int128)1 << 127;
    if (ua != 0 && ub > cap / ua) tk_panic_overflow();
    // Special case: ua==2^127 and ub==1 is valid only when result == INT128_MIN.
    if (ua == cap && ub == 1 && (a > 0 || b > 0)) tk_panic_overflow();
    return a * b;
}
#else  // !TEKO_OVERFLOW_DEBUG — plain C, zero overhead, identical to old bare-operator path
static inline uint8_t  tk_add_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a + b); }
static inline uint16_t tk_add_u16(uint16_t a, uint16_t b){ return (uint16_t)(a + b); }
static inline uint32_t tk_add_u32(uint32_t a, uint32_t b){ return a + b; }
static inline uint64_t tk_add_u64(uint64_t a, uint64_t b){ return a + b; }
static inline unsigned __int128 tk_add_u128(unsigned __int128 a, unsigned __int128 b){ return a + b; }
static inline int8_t   tk_add_i8 (int8_t   a, int8_t   b){ return (int8_t )(a + b); }
static inline int16_t  tk_add_i16(int16_t  a, int16_t  b){ return (int16_t)(a + b); }
static inline int32_t  tk_add_i32(int32_t  a, int32_t  b){ return a + b; }
static inline int64_t  tk_add_i64(int64_t  a, int64_t  b){ return a + b; }
static inline __int128 tk_add_i128(__int128 a, __int128 b){ return a + b; }

static inline uint8_t  tk_sub_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a - b); }
static inline uint16_t tk_sub_u16(uint16_t a, uint16_t b){ return (uint16_t)(a - b); }
static inline uint32_t tk_sub_u32(uint32_t a, uint32_t b){ return a - b; }
static inline uint64_t tk_sub_u64(uint64_t a, uint64_t b){ return a - b; }
static inline unsigned __int128 tk_sub_u128(unsigned __int128 a, unsigned __int128 b){ return a - b; }
static inline int8_t   tk_sub_i8 (int8_t   a, int8_t   b){ return (int8_t )(a - b); }
static inline int16_t  tk_sub_i16(int16_t  a, int16_t  b){ return (int16_t)(a - b); }
static inline int32_t  tk_sub_i32(int32_t  a, int32_t  b){ return a - b; }
static inline int64_t  tk_sub_i64(int64_t  a, int64_t  b){ return a - b; }
static inline __int128 tk_sub_i128(__int128 a, __int128 b){ return a - b; }

static inline uint8_t  tk_mul_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a * b); }
static inline uint16_t tk_mul_u16(uint16_t a, uint16_t b){ return (uint16_t)((unsigned)a * (unsigned)b); }
static inline uint32_t tk_mul_u32(uint32_t a, uint32_t b){ return a * b; }
static inline uint64_t tk_mul_u64(uint64_t a, uint64_t b){ return a * b; }
static inline unsigned __int128 tk_mul_u128(unsigned __int128 a, unsigned __int128 b){ return a * b; }
static inline int8_t   tk_mul_i8 (int8_t   a, int8_t   b){ return (int8_t )(a * b); }
static inline int16_t  tk_mul_i16(int16_t  a, int16_t  b){ return (int16_t)(a * b); }
static inline int32_t  tk_mul_i32(int32_t  a, int32_t  b){ return a * b; }
static inline int64_t  tk_mul_i64(int64_t  a, int64_t  b){ return a * b; }
static inline __int128 tk_mul_i128(__int128 a, __int128 b){ return a * b; }
#endif // TEKO_OVERFLOW_DEBUG

// --- checked FLOAT division: ruling (§5) — float ÷0 PANICS (parity with int, M.1) ---
// `%` on floats is invalid (the backend rejects it); only `/` rides these. Single-eval.
static inline double     tk_div_f64(double     a, double     b){ if (b == 0.0)     tk_panic_div0(); return a / b; }
static inline float      tk_div_f32(float      a, float      b){ if (b == 0.0f)    tk_panic_div0(); return a / b; }
static inline _Float16   tk_div_f16(_Float16   a, _Float16   b){ if (b == (_Float16)0) tk_panic_div0(); return a / b; }

// --- checked narrowing integer conversion: panic if the value can't fit the target ---
// The source value is widened to a 128-bit carrier WITHOUT loss (every Teko integer
// prim, incl. u128/i128, fits a 128-bit carrier): __int128 for a SIGNED source ("_s"),
// unsigned __int128 for an UNSIGNED source ("_u"). The range check then decides fit.
// A signed source -> unsigned target rides "_s" (the negative check catches it); an
// unsigned source -> signed target rides "_u" (the upper-bound check catches it).
// 128-bit-wide bounds are built from shifts so no literal exceeds what C can spell.
static inline uint8_t  tk_to_u8_s (__int128 v){ if (v < 0 || v > 0xFF)       tk_panic_cast(); return (uint8_t )v; }
static inline uint16_t tk_to_u16_s(__int128 v){ if (v < 0 || v > 0xFFFF)     tk_panic_cast(); return (uint16_t)v; }
static inline uint32_t tk_to_u32_s(__int128 v){ if (v < 0 || v > 0xFFFFFFFF) tk_panic_cast(); return (uint32_t)v; }
static inline uint64_t tk_to_u64_s(__int128 v){ if (v < 0 || v > (__int128)0xFFFFFFFFFFFFFFFFULL) tk_panic_cast(); return (uint64_t)v; }
static inline unsigned __int128 tk_to_u128_s(__int128 v){ if (v < 0) tk_panic_cast(); return (unsigned __int128)v; }
static inline uint8_t  tk_to_u8_u (unsigned __int128 v){ if (v > 0xFF)                tk_panic_cast(); return (uint8_t )v; }
static inline uint16_t tk_to_u16_u(unsigned __int128 v){ if (v > 0xFFFF)             tk_panic_cast(); return (uint16_t)v; }
static inline uint32_t tk_to_u32_u(unsigned __int128 v){ if (v > 0xFFFFFFFF)         tk_panic_cast(); return (uint32_t)v; }
static inline uint64_t tk_to_u64_u(unsigned __int128 v){ if (v > (unsigned __int128)0xFFFFFFFFFFFFFFFFULL) tk_panic_cast(); return (uint64_t)v; }

static inline int8_t   tk_to_i8_s (__int128 v){ if (v < -128          || v > 127)        tk_panic_cast(); return (int8_t )v; }
static inline int16_t  tk_to_i16_s(__int128 v){ if (v < -32768        || v > 32767)      tk_panic_cast(); return (int16_t)v; }
static inline int32_t  tk_to_i32_s(__int128 v){ if (v < -2147483648LL || v > 2147483647LL) tk_panic_cast(); return (int32_t)v; }
static inline int64_t  tk_to_i64_s(__int128 v){ if (v < -(__int128)0x8000000000000000ULL || v > (__int128)0x7FFFFFFFFFFFFFFFLL) tk_panic_cast(); return (int64_t)v; }
static inline int8_t   tk_to_i8_u (unsigned __int128 v){ if (v > 127)                tk_panic_cast(); return (int8_t )v; }
static inline int16_t  tk_to_i16_u(unsigned __int128 v){ if (v > 32767)             tk_panic_cast(); return (int16_t)v; }
static inline int32_t  tk_to_i32_u(unsigned __int128 v){ if (v > 2147483647LL)      tk_panic_cast(); return (int32_t)v; }
static inline int64_t  tk_to_i64_u(unsigned __int128 v){ if (v > (unsigned __int128)0x7FFFFFFFFFFFFFFFULL) tk_panic_cast(); return (int64_t)v; }
// i128 max == 2^127 - 1; built from shifts since no C literal can spell a 128-bit value.
static inline __int128 tk_to_i128_u(unsigned __int128 v){ if (v > (((unsigned __int128)0x7FFFFFFFFFFFFFFFULL << 64) | (unsigned __int128)0xFFFFFFFFFFFFFFFFULL)) tk_panic_cast(); return (__int128)v; }

// --- checked float -> int conversion: ruling (§5) — `to` truncates toward zero;
// NaN/inf or a value outside the target's range -> PANIC (parity with the int guard).
// Truncation toward zero is C's float->int conversion semantics; we range-check the
// SOURCE value in the float domain BEFORE converting. NaN fails every comparison, so a
// negated `(lo <= x && x <= hi)` traps NaN AND ±inf AND out-of-range in one test.
// Every f16/f32 value is exactly representable as a double, so the f32/f16 entry points
// widen losslessly and route through the f64 checker (single-eval entry typing preserved).
//
// Bound choice: for an 8/16/32-bit target both bounds are exact doubles, so the inclusive
// `[min, max]` compare is exact. For 64/128-bit targets the max integer is NOT an exact
// double, so the unsigned/positive ceiling is the EXCLUSIVE 2^W (resp. 2^(W-1)) via `<`,
// which is the exact double right above the representable range — any in-range value is
// strictly below it. The signed lower bound -2^(W-1) IS an exact double, so it stays `>=`.
static inline uint8_t  tk_to_u8_from_f64 (double x){ if(!(x>=0.0 && x<=255.0))             tk_panic_cast(); return (uint8_t )x; }
static inline uint8_t  tk_to_u8_from_f32 (float  x){ return tk_to_u8_from_f64((double)x); }
static inline uint8_t  tk_to_u8_from_f16 (_Float16 x){ return tk_to_u8_from_f64((double)x); }
static inline uint16_t tk_to_u16_from_f64(double x){ if(!(x>=0.0 && x<=65535.0))           tk_panic_cast(); return (uint16_t)x; }
static inline uint16_t tk_to_u16_from_f32(float  x){ return tk_to_u16_from_f64((double)x); }
static inline uint16_t tk_to_u16_from_f16(_Float16 x){ return tk_to_u16_from_f64((double)x); }
static inline uint32_t tk_to_u32_from_f64(double x){ if(!(x>=0.0 && x<=4294967295.0))      tk_panic_cast(); return (uint32_t)x; }
static inline uint32_t tk_to_u32_from_f32(float  x){ return tk_to_u32_from_f64((double)x); }
static inline uint32_t tk_to_u32_from_f16(_Float16 x){ return tk_to_u32_from_f64((double)x); }
static inline uint64_t tk_to_u64_from_f64(double x){ if(!(x>=0.0 && x<18446744073709551616.0)) tk_panic_cast(); return (uint64_t)x; }
static inline uint64_t tk_to_u64_from_f32(float  x){ return tk_to_u64_from_f64((double)x); }
static inline uint64_t tk_to_u64_from_f16(_Float16 x){ return tk_to_u64_from_f64((double)x); }
static inline unsigned __int128 tk_to_u128_from_f64(double x){ if(!(x>=0.0 && x<340282366920938463463374607431768211456.0)) tk_panic_cast(); return (unsigned __int128)x; }
static inline unsigned __int128 tk_to_u128_from_f32(float  x){ return tk_to_u128_from_f64((double)x); }
static inline unsigned __int128 tk_to_u128_from_f16(_Float16 x){ return tk_to_u128_from_f64((double)x); }

// Signed targets: valid x in [-2^(W-1), 2^(W-1) - 1]. For 8/16/32 the bounds are exact
// doubles; for 64/128 the upper bound is bounded strictly below 2^(W-1).
static inline int8_t   tk_to_i8_from_f64 (double x){ if(!(x>=-128.0 && x<=127.0))          tk_panic_cast(); return (int8_t )x; }
static inline int8_t   tk_to_i8_from_f32 (float  x){ return tk_to_i8_from_f64((double)x); }
static inline int8_t   tk_to_i8_from_f16 (_Float16 x){ return tk_to_i8_from_f64((double)x); }
static inline int16_t  tk_to_i16_from_f64(double x){ if(!(x>=-32768.0 && x<=32767.0))      tk_panic_cast(); return (int16_t)x; }
static inline int16_t  tk_to_i16_from_f32(float  x){ return tk_to_i16_from_f64((double)x); }
static inline int16_t  tk_to_i16_from_f16(_Float16 x){ return tk_to_i16_from_f64((double)x); }
static inline int32_t  tk_to_i32_from_f64(double x){ if(!(x>=-2147483648.0 && x<=2147483647.0)) tk_panic_cast(); return (int32_t)x; }
static inline int32_t  tk_to_i32_from_f32(float  x){ return tk_to_i32_from_f64((double)x); }
static inline int32_t  tk_to_i32_from_f16(_Float16 x){ return tk_to_i32_from_f64((double)x); }
static inline int64_t  tk_to_i64_from_f64(double x){ if(!(x>=-9223372036854775808.0 && x<9223372036854775808.0)) tk_panic_cast(); return (int64_t)x; }
static inline int64_t  tk_to_i64_from_f32(float  x){ return tk_to_i64_from_f64((double)x); }
static inline int64_t  tk_to_i64_from_f16(_Float16 x){ return tk_to_i64_from_f64((double)x); }
static inline __int128 tk_to_i128_from_f64(double x){ if(!(x>=-170141183460469231731687303715884105728.0 && x<170141183460469231731687303715884105728.0)) tk_panic_cast(); return (__int128)x; }
static inline __int128 tk_to_i128_from_f32(float  x){ return tk_to_i128_from_f64((double)x); }
static inline __int128 tk_to_i128_from_f16(_Float16 x){ return tk_to_i128_from_f64((double)x); }

#endif // TEKO_RT_H
