# Teko — Fonte canônica (`.tkp` / `.tks` / `.tkt`)

> **Correção doutrinária (alinhada a TEKO_HISTORY §B.37).** Este snapshot congelado
> referia, na prosa/comentários, um "retorno Unit" para "não retorna valor". Conforme
> §B.37, `Unit` **deixa de existir** — "não retorna valor" é `void`, um **marcador de
> retorno** (`-> void`), nunca um tipo/valor/membro/binding. As menções a "Unit return"
> abaixo foram trocadas por "void return"; só essas formas mudaram.

Toda a fonte Teko do compilador, num documento só — na ordem de build (M.4). É o
fonte do compilador **auto-hospedado** (o destino); o C23 é o andaime do bootstrap,
que você converte a partir daqui. O build C ignora `.tkp`/`.tks`/`.tkt`.

## Correção desta revisão — tipos nativos são INJETADOS, não código-fonte

Revisão anterior definia `byte`/`str`/`error` como `type X = Y` no `core.tks` —
**a falha.** Por **B.19**, os nomes de tipo primitivos (`u8`…`u64`, `bool`, …), os
derivados nativos (`byte`, `str`, `char`) e `error` são **TIPOS PREDEFINIDOS,
INJETADOS** pelo compilador (como a stdlib `teko::`), **nunca código-fonte**. Suas
representações são injetadas no **codegen**; no bootstrap C o `core.h` as fornece
concretas (`tk_byte`, `tk_str`, `tk_error`, + os stamps `TK_RESULT`/`TK_LIST`).
Lexados como `Ident`, resolvidos pelo checker.

**Em aberto:** B.19 os torna **shadowáveis** (um dev pode nomear um local `i32`; o
checker resolve por shadowing). A alternativa é **reservá-los** (erro como
identificador) — pendente da tua decisão. Não afeta o parser (que usa os tipos
como tipos, nunca como identificadores).

---

## `teko.tkp` — manifesto do projeto (B.33, TOML)

```toml
# teko.tkp — the Teko project manifest (B.33). The Teko-source parallel of the
# C bootstrap's CMakeLists.txt: it describes the eventual self-hosted compiler.
# Its format is TOML. The C build ignores it.
#
# Declares the project's CANONICAL ROOT NAME, the artifact, and the source root.
# `teko` is the reserved root — no user project may claim it; the language's own
# project is its sole bearer, and every symbol is addressed from it
# (teko::lexer::Token, teko::text::str_from_utf8, ...).

name = "teko"
root = "src"           # the source root — INVISIBLE in addressing
                       # (teko::lexer::Token, never teko::src::lexer::Token)

[artifact]
kind = "binary"        # the compiler is an executable

[aliases]
# `use`-aliases and dependency aliases live HERE (B.33), never in a .tks.
# (none yet — the seed has no external dependencies.)
```

---

## `src/core.tks` — o prelúdio (intrínsecos INJETADOS, sem `type` fonte)

O prelúdio **não tem declarações de tipo** — os built-ins são injetados pelo
compilador, não escritos em Teko:

```teko
// src/core.tks — the prelude is INJECTED, not source (B.19).
//
// There are NO `type byte = u8` / `type str = []byte` / `type error = …`
// declarations. The primitive types (u8..u64, i8..i64, bool, …), the native
// derived types (byte, str, char), and error are PREDEFINED TYPES, INJECTED by
// the compiler (like the teko:: stdlib) — never user source. Their concrete
// representations are injected at CODEGEN; the C bootstrap's core.h provides them
// directly. They are lexed as `Ident` and resolved by the checker.
//
//   byte        — one octet               (rep: u8)
//   str         — UTF-8 text              (rep: a {ptr,len} slice over bytes;
//                                          str_from_utf8 is the only door — B.36)
//   error       — error-as-value (B.1)    (rep: { message: str }; `T | error` is
//                                          native variant syntax — B.14; the C's
//                                          TK_RESULT exists only because C lacks
//                                          variants). to_string is provided by the
//                                          injected error, not written here.
//   teko::list  — the growable list, a compiler BUILTIN over the one true
//                 intrinsic (the raw allocation primitive — the OS/FFI bottom).
//
// RESERVED (revalidation overturns B.19's shadowable clause — M.1 exclusion-by-
// construction is decisive, with the `teko`-root precedent: injected AND reserved).
// A reserved type name used as a plain identifier is an ERROR. The escape that
// would let one be an identifier (C#-style `@name`) is DEFERRED TO EVOLUTION (the
// seed never needs it); its exact syntax is pending. They stay INJECTED — injection
// (the definition) and reservation (the name) are orthogonal.
```

**C23 — `src/core.h`** (a injeção feita concreta no bootstrap; `core.h` provê `tk_error` + os stamps `TK_RESULT`/`TK_LIST`. `tk_byte`/`tk_str` ficam em `text.h` — escolha de DAG: `core.h` independente, via `const char*` no error):

```c
// src/core.h — the prelude made concrete in C. Bottom of the module DAG (B.8).
#ifndef TK_CORE_H
#define TK_CORE_H

#include <stdbool.h>
#include <stdlib.h>    // realloc, free, abort, size_t — for TK_LIST

// error — the built-in error-as-value (B.1). In the bootstrap the message is a
// static ASCII literal (a const char*), keeping core.h independent of teko::text
// (DAG one-directional); the Teko-level message is a `str`.
typedef struct {
    const char *message;
} tk_error;

static inline tk_error tk_error_make(const char *message) {
    return (tk_error){ .message = message };
}

// TK_RESULT(T, Name) — the C form of `T | error` (no generics in the seed — M.5).
// Read `.as.value` only when `ok`, `.as.error` only when `!ok` (the match).
#define TK_RESULT(T, Name)                     \
    typedef struct {                           \
        bool ok;                               \
        union { T value; tk_error error; } as; \
    } Name

// TK_LIST(T, Name) — teko::list realized concretely. `push` CONSUMES and RETURNS
// (xs = Name##_push(xs, item)); allocation failure PANICS (abort — M.5).
#define TK_LIST(T, Name)                                            \
    typedef struct { T *ptr; size_t len; size_t cap; } Name;        \
    static inline Name Name##_empty(void) {                         \
        return (Name){ .ptr = NULL, .len = 0, .cap = 0 };           \
    }                                                               \
    static inline Name Name##_push(Name xs, T item) {               \
        if (xs.len == xs.cap) {                                     \
            size_t ncap = (xs.cap == 0) ? 8 : (xs.cap * 2);         \
            T *np = realloc(xs.ptr, ncap * sizeof(T));              \
            if (np == NULL) { abort(); }                            \
            xs.ptr = np;                                            \
            xs.cap = ncap;                                          \
        }                                                           \
        xs.ptr[xs.len] = item;                                      \
        xs.len = xs.len + 1;                                        \
        return xs;                                                  \
    }                                                               \
    static inline void Name##_free(Name xs) { free(xs.ptr); }

#endif // TK_CORE_H
```

---

## `src/text/text.tks` — validação UTF-8 (`teko::text`)

```teko
// src/text/text.tks   (namespace 'teko::text'). The base types byte/str live in
// the PRELUDE (core.tks); this module is the OPERATIONS on them — the UTF-8
// validation. Depends on the prelude in ONE direction (no cycle). str_from_utf8
// is the ONLY door from raw bytes to a str (B.36).

// The valid range of the FIRST continuation byte, plus how many follow. (A struct
// because Teko has no tuples — it packages the three results of classifying a lead.)
type Lead = struct {
    cont: u64             // continuation bytes that follow the lead byte
    lo:   byte            // first continuation byte: lower bound
    hi:   byte            // first continuation byte: upper bound
}

// Well-formed UTF-8 per RFC 3629 / Unicode: reject overlong encodings, UTF-16
// surrogates (U+D800..U+DFFF), and codepoints > U+10FFFF. The FIRST continuation
// byte carries the tightest constraint; the rest are plain 0x80..0xBF. One
// deterministic walk — no heuristic, no guessing (M.3).
fn valid_utf8(s: []byte) -> bool {
    mut i = 0
    loop {
        if i >= s.len { break }
        let b = s[i]
        if b <= 0x7F { i++; continue }              // ASCII — a single byte

        let lead = match b {
            0xC2..=0xDF => Lead { cont = 1; lo = 0x80; hi = 0xBF }
            0xE0        => Lead { cont = 2; lo = 0xA0; hi = 0xBF }   // no overlong
            0xE1..=0xEC => Lead { cont = 2; lo = 0x80; hi = 0xBF }
            0xED        => Lead { cont = 2; lo = 0x80; hi = 0x9F }   // no surrogate
            0xEE..=0xEF => Lead { cont = 2; lo = 0x80; hi = 0xBF }
            0xF0        => Lead { cont = 3; lo = 0x90; hi = 0xBF }   // no overlong
            0xF1..=0xF3 => Lead { cont = 3; lo = 0x80; hi = 0xBF }
            0xF4        => Lead { cont = 3; lo = 0x80; hi = 0x8F }   // <= U+10FFFF
            _           => return false             // 0x80..0xC1, 0xF5..0xFF: invalid
        }

        if s.len - i <= lead.cont { return false }  // truncated: not enough bytes
        if s[i + 1] < lead.lo || s[i + 1] > lead.hi { return false }

        mut k = 2
        loop {
            if k > lead.cont { break }
            let cb = s[i + k]
            if cb < 0x80 || cb > 0xBF { return false }
            k++
        }
        i = i + lead.cont + 1
    }
    true
}

// str_from_utf8 — the validated constructor (UTF-8 is FORCED). The ONLY door from
// raw bytes to a str (B.36 — a str MEANS valid UTF-8, so it IS). Zero-copy on
// success (the str views the same bytes, re-typed). Teko never guesses an
// encoding (M.3) — it validates the one codepage it declares (UTF-8).
fn str_from_utf8(b: []byte) -> str | error {
    if !valid_utf8(b) { return error { message = "invalid UTF-8" } }
    str(b)                // []byte -> str: the validated newtype wrap [form TBD]
}
```

**C23 — `src/text/text.h`:**

```c
// src/text/text.h — teko::text: bootstrap text types + operations (A.16 / B.36).
#ifndef TK_TEXT_H
#define TK_TEXT_H

#include "../core.h"   // tk_error, TK_RESULT (DAG: text → core)
#include <stddef.h>    // size_t
#include <stdint.h>    // uint8_t

// byte — one octet. Teko's `byte` is a newtype over u8 (distinct identity, same
// rep); C typedefs are transparent, so that identity lives in the Teko checker.
typedef uint8_t tk_byte;

// str — a VIEW into validated UTF-8 bytes (zero-copy, B.35). Validity is set once
// at construction and trusted downstream, never re-checked (B.36).
typedef struct {
    const tk_byte *ptr;   // the bytes (valid UTF-8)
    size_t         len;   // length in BYTES (not codepoints)
} tk_str;

// `str | error` — the result of validating raw bytes as UTF-8.
TK_RESULT(tk_str, tk_str_result);

// str_from_utf8 — the validated constructor (the ONLY door from bytes to a str;
// UTF-8 FORCED — B.36). Zero-copy on success (views the same bytes).
tk_str_result tk_str_from_utf8(const tk_byte *bytes, size_t len);

// slice [start, end) — a zero-copy VIEW sharing s's bytes (no re-validation; the
// caller cuts on codepoint boundaries). The reference's `slice(source, a, b)`.
static inline tk_str tk_str_slice(tk_str s, size_t start, size_t end) {
    return (tk_str){ .ptr = s.ptr + start, .len = end - start };
}

#endif // TK_TEXT_H
```

**C23 — `src/text/text.c`:**

```c
// src/text/text.c — UTF-8 validation (the bootstrap's only bytes → str door).
#include "text.h"

// Well-formed UTF-8 per RFC 3629: reject overlong, UTF-16 surrogates
// (U+D800..U+DFFF), and codepoints > U+10FFFF. The first continuation byte
// carries the tightest constraint; the rest are plain 0x80..0xBF. One walk (M.3).
static bool valid_utf8(const tk_byte *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        tk_byte b = s[i];
        if (b <= 0x7F) { i += 1; continue; }        // ASCII — a single byte

        size_t  cont;                                // continuation bytes that follow
        tk_byte lo, hi;                              // valid range for the FIRST of them
        if      (b >= 0xC2 && b <= 0xDF) { cont = 1; lo = 0x80; hi = 0xBF; }
        else if (b == 0xE0)              { cont = 2; lo = 0xA0; hi = 0xBF; } // no overlong
        else if (b >= 0xE1 && b <= 0xEC) { cont = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xED)              { cont = 2; lo = 0x80; hi = 0x9F; } // no surrogate
        else if (b >= 0xEE && b <= 0xEF) { cont = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF0)              { cont = 3; lo = 0x90; hi = 0xBF; } // no overlong
        else if (b >= 0xF1 && b <= 0xF3) { cont = 3; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF4)              { cont = 3; lo = 0x80; hi = 0x8F; } // <= U+10FFFF
        else return false;                           // 0x80..0xC1, 0xF5..0xFF: invalid lead

        if (len - i <= cont) return false;           // truncated: not enough bytes
        if (s[i + 1] < lo || s[i + 1] > hi) return false;            // first continuation
        for (size_t k = 2; k <= cont; k += 1) {                      // the rest, plain
            if (s[i + k] < 0x80 || s[i + k] > 0xBF) return false;
        }
        i += cont + 1;
    }
    return true;
}

tk_str_result tk_str_from_utf8(const tk_byte *bytes, size_t len) {
    if (!valid_utf8(bytes, len)) {
        return (tk_str_result){ .ok = false, .as.error = tk_error_make("invalid UTF-8") };
    }
    return (tk_str_result){ .ok = true, .as.value = (tk_str){ bytes, len } };
}
```

---

## `src/lexer/token.tks` — os tipos do token (`teko::lexer`)

O conjunto **completo** de tokens (A.1 → A.15). A lógica de varredura está em
`lexer.tks` (mesmo namespace, mesma pasta — se enxergam sem `use`).

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — the token TYPES.

type TokenKind = enum {
    // --- literals & identifiers ---
    Number          // 123, 1_000
    Ident           // snake_case names
    Str             // "…"   — its text is a str (B.36)
    Byte            // b'x'  — one octet (B.36)
    Underscore      // _     — the wildcard

    // --- keywords ---
    Fn
    Type
    Struct
    Enum
    Let
    Mut
    Const
    If
    Else
    Loop
    Break
    Continue
    Return
    Match
    When
    As
    Use
    Exp

    // --- arithmetic & bitwise operators ---
    Plus            // +
    Minus           // -
    Star            // *
    Slash           // /
    Percent         // %
    Amp             // &
    Pipe            // |
    Caret           // ^
    Tilde           // ~
    Shl             // <<
    Shr             // >>

    // --- assignment (compound — B.4: statement-only) ---
    Assign          // =
    PlusEq          // +=
    MinusEq         // -=
    StarEq          // *=
    SlashEq         // /=
    PercentEq       // %=
    AmpEq           // &=
    PipeEq          // |=
    CaretEq         // ^=
    ShlEq           // <<=
    ShrEq           // >>=

    // --- comparison ---
    EqEq            // ==
    Ne              // !=
    Lt              // <
    Le              // <=
    Gt              // >
    Ge              // >=

    // --- logical ---
    AndAnd          // &&
    OrOr            // ||
    Bang            // !

    // --- delimiters & punctuation ---
    LParen          // (
    RParen          // )
    LBrace          // {
    RBrace          // }
    LBracket        // [
    RBracket        // ]
    Comma           // ,
    Colon           // :
    ColonColon      // ::
    Arrow           // ->
    FatArrow        // =>
    DotDotEq        // ..=
    Newline         // a significant line break (B.26 — statement separator)
    Doc             // /** … */ doc comment (attaches to a type/fn declaration — optional)

    // --- word-operator (B.23) — APPENDED LAST on purpose: `kind_byte` serializes
    //     operator kinds by their enum ordinal (E7 makes that explicit), so a new
    //     variant must not shift the existing operators. `to` is never a stored op
    //     (the cast is its own `Cast` node — F2), so its ordinal is never serialized.
    To              // `to` — the cast operator (x to T); preserves→ok / loses→error (E7)
    Dot             // `.` — postfix field/method access (P2/F2); appended last, same ordinal reason
    Semicolon       // `;` — statement/field separator (B.17); appended last, same ordinal reason — never a stored op
    Variant         // `variant` — variant type-body keyword (B.14); appended last, same ordinal reason — never a stored op (P5c)
}

type Token = struct {
    kind: TokenKind
    text: str
}
```

---

## `src/lexer/lexer.tks` — o scanner completo (`teko::lexer`)

Escaneia o `str` já validado **byte a byte** (B.36 — a sintaxe de Teko é ASCII, então
classes de caractere usam literais `b'x'`). Estado ref-less (B.7): o cursor mora no
chamador, helpers são puros. *Maximal munch* (B.23) resolve operadores multi-byte
(`==`, `->`, `<<=`, `..=`, …). Leitores que podem falhar retornam `Scan | error`.

> A referência divide a varredura em `scan.tks` + `lexer.tks`; aqui está unificado
> (como no `lexer.c` do bootstrap). Os helpers de escape/byte-literal
> (`escape_byte`, `byte_value`, `one_byte`, `str_of_bytes`) a referência deixou como
> *promessa* (A.17) — sintetizados abaixo e marcados; a forma de construir um `str`
> de bytes frescos depende do construtor do newtype (a nota `str(b)`).

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — the complete scanner.

// A read result: the token produced + where scanning stopped. (A struct because
// Teko has no tuples; it packages result + new state.)
type Scan = struct {
    token: Token
    next:  u64
}

// A decoded escape / a byte-literal value: the byte + the pos after it.
type EscByte = struct {
    value: byte
    next:  u64
}
type ByteVal = struct {
    value: byte
    next:  u64
}

// --- byte access ---

// the byte at p, or 0 if out of bounds (a safe peek for look-ahead — maximal munch).
fn at(source: str, p: u64) -> byte {
    if p >= source.len { return 0 }
    source[p]
}

// --- predicates (pure, over a single byte; ASCII syntax → byte literals) ---

fn is_digit(c: byte) -> bool {
    c >= b'0' && c <= b'9'
}

fn is_alpha(c: byte) -> bool {
    (c >= b'a' && c <= b'z') || (c >= b'A' && c <= b'Z')
}

fn is_ident_continue(c: byte) -> bool {
    is_alpha(c) || is_digit(c) || c == b'_'
}

// --- whitespace & comments (pure: take pos, return the new pos) ---

// skip spaces/tabs/CR — NOT newlines (a newline is a significant token, B.26).
fn skip_spaces(source: str, pos: u64) -> u64 {
    mut p = pos
    loop {
        if p >= source.len { break }
        let c = source[p]
        if c != b' ' && c != b'\t' && c != b'\r' { break }
        p++
    }
    p
}

// skip a `//` line comment: advance to the newline (or end), leaving it for the loop.
fn skip_line(source: str, pos: u64) -> u64 {
    mut p = pos
    loop {
        if p >= source.len { break }
        if source[p] == b'\n' { break }
        p++
    }
    p
}

// skip a block comment `/* … */` (not nested); returns the pos after the `*/`.
fn skip_block_comment(source: str, pos: u64) -> u64 | error {
    mut p = pos + 2                       // past `/*`
    loop {
        if p + 1 >= source.len { return error { message = "unterminated block comment" } }
        if source[p] == b'*' && source[p + 1] == b'/' { return p + 2 }
        p++
    }
}

// read a DOC comment `/** … */` → a Doc token spanning the whole comment.
fn read_doc_comment(source: str, pos: u64) -> Scan | error {
    mut p = pos + 3                       // past `/**`
    loop {
        if p + 1 >= source.len { return error { message = "unterminated doc comment" } }
        if source[p] == b'*' && source[p + 1] == b'/' {
            let end = p + 2
            return Scan { token = Token { kind = TokenKind::Doc; text = slice(source, pos, end) }; next = end }
        }
        p++
    }
}

// --- a token spanning n bytes from pos (the common Scan builder) ---

fn sym(source: str, pos: u64, n: u64, kind: TokenKind) -> Scan {
    Scan { token = Token { kind = kind; text = slice(source, pos, pos + n) }; next = pos + n }
}

// --- numbers & identifiers ---

// digits, allowing `_` as a separator BETWEEN digits (B.28): `1_000`.
fn read_number(source: str, pos: u64) -> Scan {
    mut p = pos
    loop {
        if p >= source.len { break }
        let c = source[p]
        if is_digit(c) { p++; continue }
        if c == b'_' {
            if p + 1 < source.len && is_digit(at(source, p + 1)) { p++; continue }
        }
        break
    }
    Scan { token = Token { kind = TokenKind::Number; text = slice(source, pos, p) }; next = p }
}

fn read_ident(source: str, pos: u64) -> Scan {
    mut p = pos
    loop {
        if p >= source.len { break }
        if !is_ident_continue(source[p]) { break }
        p++
    }
    Scan { token = Token { kind = TokenKind::Ident; text = slice(source, pos, p) }; next = p }
}

// a run of `_` then a letter/digit is ONE identifier (`_foo`, `__bar`); a `_` not so
// followed is the wildcard token (emitted one at a time — the parser rejects a run).
fn read_underscore(source: str, pos: u64) -> Scan {
    mut p = pos
    loop {
        if p >= source.len { break }
        if source[p] != b'_' { break }
        p++
    }
    if p < source.len {
        if is_alpha(source[p]) || is_digit(source[p]) {
            return read_ident(source, pos)   // reads from the first `_`
        }
    }
    sym(source, pos, 1, TokenKind::Underscore)
}

// --- keywords (an ident whose text matches the table — B.19) ---

fn keyword_kind(text: str) -> TokenKind {
    if text == "fn"       { return TokenKind::Fn }
    if text == "type"     { return TokenKind::Type }
    if text == "struct"   { return TokenKind::Struct }
    if text == "enum"     { return TokenKind::Enum }
    if text == "variant"  { return TokenKind::Variant }
    if text == "let"      { return TokenKind::Let }
    if text == "mut"      { return TokenKind::Mut }
    if text == "const"    { return TokenKind::Const }
    if text == "if"       { return TokenKind::If }
    if text == "else"     { return TokenKind::Else }
    if text == "loop"     { return TokenKind::Loop }
    if text == "break"    { return TokenKind::Break }
    if text == "continue" { return TokenKind::Continue }
    if text == "return"   { return TokenKind::Return }
    if text == "match"    { return TokenKind::Match }
    if text == "when"     { return TokenKind::When }
    if text == "as"       { return TokenKind::As }
    if text == "to"       { return TokenKind::To }   // the cast operator (x to T) — F1/E7
    if text == "use"      { return TokenKind::Use }
    if text == "exp"      { return TokenKind::Exp }
    TokenKind::Ident
}

fn keyword_or_ident(source: str, pos: u64) -> Scan {
    let s = read_ident(source, pos)
    let k = keyword_kind(s.token.text)
    Scan { token = Token { kind = k; text = s.token.text }; next = s.next }
}

// --- escapes & fresh-byte → str (reference-deferred; see the note above) ---

// decode `\` + one byte → the byte; the set is the structural minimum (it can grow).
fn escape_byte(source: str, pos: u64) -> EscByte | error {
    let after = pos + 1
    if after >= source.len { return error { message = "unterminated escape" } }
    let v = match source[after] {
        b'n'  => 0x0A
        b't'  => 0x09
        b'r'  => 0x0D
        b'\\' => 0x5C
        b'"'  => 0x22
        b'\'' => 0x27
        b'0'  => 0x00
        _     => return error { message = "unknown escape" }
    }
    EscByte { value = v; next = after + 1 }
}

// one byte of a byte literal: a raw byte, or an escape.
fn byte_value(source: str, pos: u64) -> ByteVal | error {
    if source[pos] == b'\\' {
        let e = match escape_byte(source, pos) {
            EscByte as eb => eb
            error as err  => return err
        }
        return ByteVal { value = e.value; next = e.next }
    }
    ByteVal { value = source[pos]; next = pos + 1 }
}

// build a str from FRESH bytes (a decoded literal), not a view into the source.
// Escapes are ASCII and literal bytes come from the validated source, so validity
// holds; the str-from-bytes primitive is the same as text.tks's `str(b)` [TBD].
fn one_byte(b: byte) -> str {
    mut xs = teko::list::empty()
    xs = teko::list::push(xs, b)
    str(xs)
}
fn str_of_bytes(xs: []byte) -> str {
    str(xs)
}

// --- string & byte literals ---

// `"` at pos. Collect bytes until the closing `"`, decoding escapes.
fn read_str(source: str, pos: u64) -> Scan | error {
    mut p = pos + 1                          // past the opening quote
    mut bytes = teko::list::empty()
    loop {
        if p >= source.len { return error { message = "unterminated string literal" } }
        let c = source[p]
        if c == b'"' {
            return Scan { token = Token { kind = TokenKind::Str; text = str_of_bytes(bytes) }; next = p + 1 }
        }
        if c == b'\\' {
            let e = match escape_byte(source, p) {
                EscByte as eb => eb
                error as err  => return err
            }
            bytes = teko::list::push(bytes, e.value)
            p = e.next
            continue
        }
        bytes = teko::list::push(bytes, c)   // a literal byte (ASCII or UTF-8 cont.)
        p++
    }
}

// `b'…'`: pos points at `b`, `'` at pos+1. One byte (raw or escaped) then a closing `'`.
fn read_byte_lit(source: str, pos: u64) -> Scan | error {
    let inner = pos + 2                       // past `b'`
    if inner >= source.len { return error { message = "unterminated byte literal" } }
    let v = match byte_value(source, inner) {
        ByteVal as bv => bv
        error as err  => return err
    }
    if at(source, v.next) != b'\'' { return error { message = "expected closing ' in byte literal" } }
    Scan { token = Token { kind = TokenKind::Byte; text = one_byte(v.value) }; next = v.next + 1 }
}

// --- symbols: operators, delimiters, punctuation (maximal munch — B.23) ---

fn read_symbol(source: str, pos: u64) -> Scan | error {
    let c  = source[pos]
    let c1 = at(source, pos + 1)
    let c2 = at(source, pos + 2)

    // 3-byte (longest first)
    if c == b'<' && c1 == b'<' && c2 == b'=' { return sym(source, pos, 3, TokenKind::ShlEq) }
    if c == b'>' && c1 == b'>' && c2 == b'=' { return sym(source, pos, 3, TokenKind::ShrEq) }
    if c == b'.' && c1 == b'.' && c2 == b'=' { return sym(source, pos, 3, TokenKind::DotDotEq) }

    // 2-byte
    if c == b'=' && c1 == b'=' { return sym(source, pos, 2, TokenKind::EqEq) }
    if c == b'=' && c1 == b'>' { return sym(source, pos, 2, TokenKind::FatArrow) }
    if c == b'!' && c1 == b'=' { return sym(source, pos, 2, TokenKind::Ne) }
    if c == b'<' && c1 == b'=' { return sym(source, pos, 2, TokenKind::Le) }
    if c == b'>' && c1 == b'=' { return sym(source, pos, 2, TokenKind::Ge) }
    if c == b'<' && c1 == b'<' { return sym(source, pos, 2, TokenKind::Shl) }
    if c == b'>' && c1 == b'>' { return sym(source, pos, 2, TokenKind::Shr) }
    if c == b'-' && c1 == b'>' { return sym(source, pos, 2, TokenKind::Arrow) }
    if c == b':' && c1 == b':' { return sym(source, pos, 2, TokenKind::ColonColon) }
    if c == b'&' && c1 == b'&' { return sym(source, pos, 2, TokenKind::AndAnd) }
    if c == b'|' && c1 == b'|' { return sym(source, pos, 2, TokenKind::OrOr) }
    if c == b'+' && c1 == b'=' { return sym(source, pos, 2, TokenKind::PlusEq) }
    if c == b'-' && c1 == b'=' { return sym(source, pos, 2, TokenKind::MinusEq) }
    if c == b'*' && c1 == b'=' { return sym(source, pos, 2, TokenKind::StarEq) }
    if c == b'/' && c1 == b'=' { return sym(source, pos, 2, TokenKind::SlashEq) }
    if c == b'%' && c1 == b'=' { return sym(source, pos, 2, TokenKind::PercentEq) }
    if c == b'&' && c1 == b'=' { return sym(source, pos, 2, TokenKind::AmpEq) }
    if c == b'|' && c1 == b'=' { return sym(source, pos, 2, TokenKind::PipeEq) }
    if c == b'^' && c1 == b'=' { return sym(source, pos, 2, TokenKind::CaretEq) }

    // `..` exists ONLY as `..=` (handled above); a lone `..` is an error
    if c == b'.' && c1 == b'.' { return error { message = "expected '=' to close range '..='" } }

    // 1-byte
    let one = match c {
        b'+' => TokenKind::Plus
        b'-' => TokenKind::Minus
        b'*' => TokenKind::Star
        b'/' => TokenKind::Slash
        b'%' => TokenKind::Percent
        b'&' => TokenKind::Amp
        b'|' => TokenKind::Pipe
        b'^' => TokenKind::Caret
        b'~' => TokenKind::Tilde
        b'!' => TokenKind::Bang
        b'<' => TokenKind::Lt
        b'>' => TokenKind::Gt
        b'=' => TokenKind::Assign
        b':' => TokenKind::Colon
        b';' => TokenKind::Semicolon
        b',' => TokenKind::Comma
        b'.' => TokenKind::Dot
        b'(' => TokenKind::LParen
        b')' => TokenKind::RParen
        b'{' => TokenKind::LBrace
        b'}' => TokenKind::RBrace
        b'[' => TokenKind::LBracket
        b']' => TokenKind::RBracket
        _    => return error { message = "unexpected character" }
    }
    sym(source, pos, 1, one)
}

// --- one token at pos (already past spaces/comments/newline), or fail ---

fn next_token(source: str, pos: u64) -> Scan | error {
    let c = source[pos]
    // a byte literal `b'…'` — `b` would otherwise begin an identifier
    if c == b'b' && at(source, pos + 1) == b'\'' { return read_byte_lit(source, pos) }
    if is_digit(c) { return read_number(source, pos) }
    if is_alpha(c) { return keyword_or_ident(source, pos) }
    if c == b'_'   { return read_underscore(source, pos) }
    if c == b'"'   { return read_str(source, pos) }
    read_symbol(source, pos)
}

// --- the main loop ---

fn tokenize(source: str) -> []Token | error {
    mut pos: u64 = 0
    mut tokens = teko::list::empty()

    loop {
        pos = skip_spaces(source, pos)
        if pos >= source.len { break }

        let c = source[pos]

        // `//` line comment → skip to the newline
        if c == b'/' && at(source, pos + 1) == b'/' {
            pos = skip_line(source, pos)
            continue
        }
        // `/* … */` block & `/** … */` doc comments
        if c == b'/' && at(source, pos + 1) == b'*' {
            // a DOC comment `/** … */` (but NOT the empty `/**/`) becomes a Doc
            // TOKEN (the parser attaches it to the next type/fn declaration); a plain
            // block comment is skipped.
            if at(source, pos + 2) == b'*' && at(source, pos + 3) != b'/' {
                let scan = match read_doc_comment(source, pos) { Scan as sc => sc; error as e => return e }
                tokens = teko::list::push(tokens, scan.token)
                pos = scan.next
                continue
            }
            pos = match skip_block_comment(source, pos) { u64 as p => p; error as e => return e }
            continue
        }
        // a significant newline (B.26) is a token
        if c == b'\n' {
            tokens = teko::list::push(tokens, sym(source, pos, 1, TokenKind::Newline).token)
            pos = pos + 1
            continue
        }

        let scan = match next_token(source, pos) {
            Scan as sc => sc
            error as e => return e
        }
        tokens = teko::list::push(tokens, scan.token)
        pos    = scan.next
    }

    tokens
}
```

---

## `main.tks` — o ponto de entrada (uma `main` virtual)

`main.tks` é o **arquivo de entrada do executável**. Seu conteúdo, **depois do cabeçalho de
`use`s**, É **o corpo de uma `main` implícita** — uma *main virtual*. Ele **não pode
declarar tipos nem funções**: só o cabeçalho `use` (a única declaração permitida) e
**statements** (que formam o corpo). É **obrigatório** quando o artefato do `.tkp` é
`executable` e **proibido** quando é `library`. A modelagem disto (`MainFile` = use + corpo,
distinta de um módulo = use + declarações; com a leitura do `main.tks` embrulhando tudo numa
main virtual) é a refatoração **R-main** — ver roadmap.

```teko
// main.tks — the executable entry point. After the `use` header, the rest IS the body of
// an implicit `main` (a "virtual main"): NO type/function declarations, only `use` +
// statements. Mandatory when the `.tkp` artifact is `executable`; forbidden for a `library`.
//
//   use teko::lexer        // the header — `use` is the only declaration allowed here
//   use teko::parser
//
//   <statements…>          // everything else = the virtual main's body
//
// The pipeline (read → lex → parse → check → codegen) is not wired yet (M.4 — nothing is
// built on the incomplete), so for now the virtual main's body is empty.
```

---

## `src/text/text_test.tkt` — testes de `teko::text`

```teko
// src/text/text_test.tkt — tests for teko::text. NO `main`: a .tkt has no entry
// point. Tests are PUBLIC functions marked `#test`; the compiler collects and runs
// them (a test runner — an ALPHA feature). In the bootstrap that runner does not
// exist, so the C mirror (text_test.c) uses a `main` harness instead (C may have a
// main). (`assert` and failure semantics are not yet pinned — alpha.)

// helper (not a test): is str_from_utf8(bytes) ok?
fn is_valid(bytes: []byte) -> bool {
    match str_from_utf8(bytes) {
        str   => true
        error => false
    }
}

#test
fn accepts_valid_utf8() {
    assert is_valid([0x68, 0x65, 0x6C, 0x6C, 0x6F])    // "hello"
    assert is_valid([0x63, 0x61, 0x66, 0xC3, 0xA9])    // "café" (é = C3 A9)
    assert is_valid([0xE2, 0x82, 0xAC])                // € U+20AC
    assert is_valid([0xF0, 0x9F, 0xA6, 0xB4])          // U+1F9B4
    assert is_valid([])                                // empty
}

#test
fn rejects_malformed_utf8() {
    assert !is_valid([0x80])                           // stray continuation
    assert !is_valid([0xC0, 0xAF])                     // overlong slash
    assert !is_valid([0xED, 0xA0, 0x80])               // surrogate U+D800
    assert !is_valid([0xE2, 0x82])                     // truncated 3-byte
    assert !is_valid([0xC1, 0x80])                     // lead C1 (overlong)
    assert !is_valid([0xF4, 0x90, 0x80, 0x80])         // > U+10FFFF
    assert !is_valid([0xE2, 0x28, 0xAC])               // 0x28 not a continuation
}
```

---

## `src/lexer/lexer_test.tkt` — testes de `teko::lexer`

```teko
// src/lexer/lexer_test.tkt — tests for teko::lexer. NO `main`: tests are PUBLIC
// `#test` functions the compiler collects and runs (a runner — ALPHA). The C
// mirror uses a `main` harness (the runner does not exist in the bootstrap).

// helpers (not tests):
// the kinds of the tokens of `source` (already validated), or error if it fails.
fn kinds_of(source: str) -> []TokenKind | error {
    let toks = match tokenize(source) {
        ts: []Token => ts
        error       => return error { message = "lex failed" }
    }
    mut ks = teko::list::empty()
    mut i  = 0
    loop {
        if i >= toks.len { break }
        ks = teko::list::push(ks, toks[i].kind)
        i++
    }
    ks
}

// do the kinds of `source` equal `want`, in order?
fn kinds_eq(source: str, want: []TokenKind) -> bool {
    let got = match kinds_of(source) {
        ks: []TokenKind => ks
        error           => return false
    }
    if got.len != want.len { return false }
    mut i = 0
    loop {
        if i >= got.len { break }
        if got[i] != want[i] { return false }
        i++
    }
    true
}

// does lexing `source` fail with an error?
fn is_error(source: str) -> bool {
    match tokenize(source) {
        []Token => false
        error   => true
    }
}

#test
fn lexes_kinds_in_order() {
    assert kinds_eq("1 + 2 * 3", [TokenKind::Number, TokenKind::Plus,
                                  TokenKind::Number, TokenKind::Star, TokenKind::Number])
    assert kinds_eq("( a )",     [TokenKind::LParen, TokenKind::Ident, TokenKind::RParen])
    assert kinds_eq("1_000",     [TokenKind::Number])      // one number (separator)
    assert kinds_eq("__bar",     [TokenKind::Ident])       // one ident (leading _)
    assert kinds_eq("_",         [TokenKind::Underscore])
    assert kinds_eq("",          [])                       // empty
}

#test
fn rejects_unexpected_byte() {
    assert is_error("1 @ 2")                               // unexpected byte
}

#test
fn preserves_token_spans() {
    // 1_000+foo -> Number("1_000") Plus("+") Ident("foo")
    let toks = match tokenize("1_000+foo") {
        ts: []Token => ts
        error       => return
    }
    assert toks.len == 3
    assert toks[0].text == "1_000"
    assert toks[1].text == "+"
    assert toks[2].text == "foo"
}

// --- F1: the `to` cast keyword ---

#test
fn lexes_to_cast_keyword() {
    // happy: `x to u32` lexes as Ident · To · Ident.
    assert kinds_eq("x to u32", [TokenKind::Ident, TokenKind::To, TokenKind::Ident])
}

#test
fn to_is_reserved_not_ident() {
    // barrier: `to` is a keyword — the lexer yields `To`, never `Ident`, so it
    // cannot be a name (the parser then rejects `To` in name position — P-phase).
    assert kinds_eq("to", [TokenKind::To])
    assert !kinds_eq("to", [TokenKind::Ident])
}

#test
fn lexes_dot_access() {
    // `.` is its own token (P2/F2): a.b → Ident · Dot · Ident; `..` is still a range error.
    assert kinds_eq("a.b", [TokenKind::Ident, TokenKind::Dot, TokenKind::Ident])
    assert kinds_eq("a; b", [TokenKind::Ident, TokenKind::Semicolon, TokenKind::Ident])   // `;` separator (P3b-field)
    assert kinds_eq("variant", [TokenKind::Variant])   // `variant` keyword (P5c)
    assert kinds_eq(".",   [TokenKind::Dot])
    assert is_error("..")
}
```

### C23 — `src/lexer/token.h` + `lexer.c` (delta F1/P2 — `to`, `.`)

> O lexer canônico acima é a fonte `.tks`; o port C do lexer é o bootstrap on-disk
> (hoje **A.1**, 9 tokens). Delta a aplicar nele — convenção `tk_token_kind` /
> `TK_TOKEN_*`, com `TK_TOKEN_TO`/`TK_TOKEN_DOT` **acrescentados por último** (mesmo
> motivo do enum Teko: `kind_byte` serializa operador pelo ordinal — E7; nenhum dos
> dois é op gravado, logo seu ordinal não importa). Os testes canônicos são os `.tkt`.

```c
// token.h — tk_token_kind enum: append last (do not shift the operator ordinals).
TK_TOKEN_TO    // `to` — the cast operator (x to T); preserves→ok / loses→error (E7)
TK_TOKEN_DOT   // `.`  — postfix field/method access (P2/F2)

// lexer.c — tk_keyword_kind(text): add the `to` row, beside `as`.
if (tk_str_eq(text, "to")) return TK_TOKEN_TO;

// lexer.c — tk_read_symbol: a lone `.` (after the `..=`/`..` checks) → TK_TOKEN_DOT.
case '.': return tk_sym(src, pos, 1, TK_TOKEN_DOT);
```

---

## O parser (`src/parser/*.tks`) — Parte 1: AST + plumbing

O parser espelha o lexer, em **descida recursiva** (a estrutura de chamada É a
precedência — B.23). Esta parte traz a **AST inteira**, os **tipos-resultado**
(`Parsed` & família) e os **helpers de cursor/operador**. As funções de parse vêm
nas Partes 2–4. (Resolvido aqui: a anotação do `Binding` é `type_ann: TypeExpr` —
a referência tinha um `type_name: []u8` legado de antes de A.8.)

### `src/parser/type.tks` — a AST de tipos (recursiva, B.8)

```teko
// A path: one or more identifiers joined by `::` (lexer::Token, or just u64).
type Segment   = struct { name: str }
type Path      = struct { segments: []Segment }       // at least one

type NamedType = struct { path: Path }                // u64 | lexer::Token
type SliceType = struct { element: TypeExpr }         // []T (recursive — B.8)
type UnionType = struct { members: []TypeExpr }       // A | B | … (two or more)

type TypeExpr  = NamedType | SliceType | UnionType    // exactly one (variant — B.14)
```

### `src/parser/ast.tks` — a AST de expressões, statements e itens

```teko
// --- expressions ---
type Number  = struct { value: i64 }                  // a literal number leaf
type Var     = struct { name: str }                   // a variable reference (reading)
type StrLit  = struct { text: str }                   // "…"  (B.36)
type ByteLit = struct { value: byte }                 // b'x' (B.36)
type Binary  = struct { op: lexer::TokenKind; left: Expr; right: Expr }   // any binary op
type Unary   = struct { op: lexer::TokenKind; operand: Expr }             // - ~ ! (prefix)
type CmpTerm = struct { op: lexer::TokenKind; operand: Expr }             // one cmp continuation
type Compare = struct { first: Expr; rest: []CmpTerm }  // a<b<c chain, kept as written (M.3)
type Call    = struct { callee: Path; args: []Expr }    // f(x), lexer::foo(x)
type IfExpr  = struct {                                  // if/else is an EXPRESSION (B.20)
    cond:     Expr
    then_blk: []Statement
    has_else: bool
    else_blk: []Statement
}
type MatchExpr = struct { subject: Expr; arms: []Arm }  // match is an EXPRESSION (B.15)

// --- postfix access & cast (F2) — recursive over Expr (compiler-managed indirection) ---
type FieldAccess = struct { receiver: Expr; field: str }                 // x.field — read a struct field (the checker's own `s.token`/`b.left` shape; B.29)
type MethodCall  = struct { receiver: Expr; method: str; args: []Expr }  // recv.method(a, …) — B.29 instance call via `.`; method *checking* is deferred (M.4)
type Cast        = struct { expr: Expr; target: TypeExpr }               // x to T — explicit conversion; preserves→ok / loses→compile-error (E7; M.0+M.1)
type PathExpr    = struct { path: Path }                                 // a qualified path as a VALUE — Enum::Member (e.g. PrimKind::U32), module statics (P2; the F2/Part-1 AST had only single-name Var)

type Expr = Number | Var | Call | IfExpr | MatchExpr | StrLit | ByteLit | Binary | Unary | Compare | FieldAccess | MethodCall | Cast | PathExpr

// --- statements ---
type BindKind = enum { Let; Mut; Const }
type SimpleName = struct { name: str }
type DestructurePattern = struct { names: []str }       // let { x; y } = … (B.13 partial)
type BindTarget = SimpleName | DestructurePattern

type Binding = struct {                                 // let/mut/const TARGET [: T] = value
    kind:     BindKind
    target:   BindTarget
    has_type: bool
    type_ann: TypeExpr                                  // the parsed annotation (A.8)
    value:    Expr
}
type Assign  = struct { name: str; op: lexer::TokenKind; value: Expr }  // x = / += / … (B.4)
type Return  = struct { has_value: bool; value: Expr }   // `return [expr]` — bare `return` (no value) has has_value=false, value a placeholder (B.20; P4a)
type LoopStmt = struct { body: []Statement }            // the only primitive loop (M.5)
type BreakStmt    = struct { }
type ContinueStmt = struct { }
type ExprStmt = struct { expr: Expr }                   // a bare expression on its own line

type Statement = Binding | Assign | Return | LoopStmt | BreakStmt | ContinueStmt | ExprStmt

// --- top-level items ---
type Param    = struct { name: str; type_ann: TypeExpr }   // immutable (B.21)
type Function = struct {
    name:        str
    params:      []Param
    has_return:  bool                                   // `-> ret` present? (absent = void return; P5a)
    return_type: TypeExpr                               // the return type (valid iff has_return)
    body:        []Statement
    is_exp:      bool                                   // marked `exp` → exported (.tkh)
    has_doc:     bool                                   // a `/** … */` doc precedes it?
    doc:         str                                    // the doc span (valid iff has_doc)
}
type Field       = struct { name: str; type_ann: TypeExpr }
type StructBody  = struct { fields: []Field }
type EnumBody    = struct { members: []str }            // member names, in order
type VariantBody = struct { type_expr: TypeExpr }       // a union (A.8)
type TypeBody    = StructBody | EnumBody | VariantBody
type TypeDecl    = struct {                             // nominal (B.13)
    name:    str
    body:    TypeBody
    is_exp:  bool                                       // marked `exp` → exported (.tkh)
    has_doc: bool
    doc:     str                                        // the doc span (valid iff has_doc)
}
type UseDecl     = struct { path: Path; has_alias: bool; alias: str }   // use a::b [as c]

// --- R-main: a `.tks` file is a MAIN (executable entry) or a MODULE. The old `Item`
//     (which mixed declarations and loose statements) is RETIRED. ---
type Decl     = Function | TypeDecl                              // a top-level declaration (a module's content)
type MainFile = struct { uses: []UseDecl; body: []Statement }    // main.tks: the `use` header + the VIRTUAL MAIN's body (statements only — no type/fn decls)
type Module   = struct { uses: []UseDecl; decls: []Decl }        // a module: the `use` header + declarations (no loose statements)
type File     = MainFile | Module                                // a `.tks` is exactly one; the file's role decides (main.tks ⇔ an executable `.tkp`)
```

#### C23 — `src/parser/ast.h` (delta F2 — `Cast` / `FieldAccess` / `MethodCall`)

> A AST canônica acima é `.tks`; o port C (`parser/ast.h`, consumido pelo checker via
> `tk_expr`/`TK_EXPR_*`) recebe três nós. Campos recursivos são **ponteiros**
> (indireção gerida). A **ordem das tags é livre** — o `Expr` do parser **não** é
> serializado (só o `TExpr` *tipado* é, com tag-bytes explícitos), ao contrário do
> `TokenKind` da F1.

```c
// parser/ast.h — three new Expr nodes (F2). Recursive fields are pointers.
typedef struct { tk_expr *receiver; tk_str field; }                                tk_field_access; // x.field
typedef struct { tk_expr *receiver; tk_str method; tk_expr *args; size_t n_args; } tk_method_call;  // recv.method(…)
typedef struct { tk_expr *expr; tk_type_expr target; }                             tk_cast;          // x to T

// tk_expr.tag enum — add: TK_EXPR_FIELD_ACCESS, TK_EXPR_METHOD_CALL, TK_EXPR_CAST
// tk_expr.as union — add: tk_field_access field_access; tk_method_call method_call; tk_cast cast;
```

### `src/parser/pattern.tks` — a AST de padrões (do `match`)

```teko
type LiteralPattern  = struct { value: Expr }           // a scalar literal
type RangePattern    = struct { lo: Expr; hi: Expr }    // lo ..= hi (inclusive)
type AltPattern      = struct { options: []Pattern }    // a | b | … (value axis)
type BindPattern     = struct { type_name: Path; has_binding: bool; binding: str }   // `Foo as x` (bind) or bare `Foo` (match the case, no bind — has_binding=false); the checker's `Byte =>` shape (P3b)
type FieldPattern    = struct { type_name: Path; fields: []str }  // Type { f; g } (variant axis)
type WildcardPattern = struct { }                       // _

type Pattern = LiteralPattern | RangePattern | AltPattern | BindPattern | FieldPattern | WildcardPattern

type Arm = struct {
    pattern:  Pattern
    has_when: bool          // a `when` guard present? (does NOT count for exhaustiveness)
    guard:    Expr          // the guard condition (when has_when)
    body:     Expr          // the arm's value (match is an expression)
}
```

### `src/parser/result.tks` — a família de resultados (payload + cursor `next`)

```teko
// Every parser returns the parsed thing + where parsing resumed. Generics
// (evolution) collapse this into one `Parsed<T>`; the seed declares each concretely
// (the named M.5 cost). A single node uses `node`; a list uses a descriptive field.
type Parsed        = struct { node: Expr;              next: u64 }   // an expression
type ParsedStmt    = struct { node: Statement;         next: u64 }   // a statement
type ParsedType    = struct { node: TypeExpr;          next: u64 }   // a type expression
type ParsedDecl    = struct { node: Decl;              next: u64 }   // a top-level declaration (a module's; R-main)
type ParsedBlock   = struct { statements: []Statement; next: u64 }   // a `{ … }` block
type ParsedBody    = struct { node: TypeBody;          next: u64 }   // a struct/enum/variant body
type ParsedPattern = struct { node: Pattern;           next: u64 }   // a match pattern
type ParsedArm     = struct { node: Arm;               next: u64 }   // a match arm
type ParsedArms    = struct { arms: []Arm;             next: u64 }   // a `{ arm; … }` arm list (P4e)
type ParsedArgs    = struct { args: []Expr;            next: u64 }   // call arguments
type ParsedParams  = struct { params: []Param;         next: u64 }   // function parameters
type ParsedTarget  = struct { node: BindTarget;        next: u64 }   // a binding target
type ParsedNames   = struct { names: []str;            next: u64 }   // a `{ … }` field-name list
type ParsedPath    = struct { node: Path;              next: u64 }   // an expression path a::b::c
type Guard         = struct { has_when: bool; guard: Expr; next: u64 }     // an optional `when`
type Annotation    = struct { has_type: bool; type_ann: TypeExpr; next: u64 }  // an optional `: T`
```

### `src/parser/cursor.tks` — helpers puros sobre (tokens, pos)

```teko
// Is there a token at `pos`? Position/index are always u64.
fn has_token(tokens: []lexer::Token, pos: u64) -> bool {
    pos < tokens.len
}

// The kind at `pos` (caller guarantees has_token first).
fn kind_at(tokens: []lexer::Token, pos: u64) -> lexer::TokenKind {
    tokens[pos].kind
}

// has_token + kind compare, folded flat (guard over nest).
fn is_kind_at(tokens: []lexer::Token, pos: u64, k: lexer::TokenKind) -> bool {
    if !has_token(tokens, pos) { return false }
    kind_at(tokens, pos) == k
}

// Demand a specific kind at `pos`; return the position AFTER it, or an error (B.1).
// The shared helper that flattens "is there a token? is it right? else error."
fn expect(tokens: []lexer::Token, pos: u64, kind: lexer::TokenKind, msg: str) -> u64 | error {
    if !has_token(tokens, pos) { return error { message = msg } }
    if kind_at(tokens, pos) != kind { return error { message = msg } }
    pos + 1
}

// Skip a run of Newline terminators (blank lines / empty statements — B.17).
fn skip_terminators(tokens: []lexer::Token, pos: u64) -> u64 {
    mut p = pos
    loop {
        if !has_token(tokens, p) { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline { break }
        p++
    }
    p
}

// is there a separator at `pos` — `;` or a newline? (B.17 — inside `{}`)
fn is_sep(tokens: []lexer::Token, pos: u64) -> bool {
    is_kind_at(tokens, pos, lexer::TokenKind::Semicolon) || is_kind_at(tokens, pos, lexer::TokenKind::Newline)
}

// skip a run of separators (`;` and newlines). Shared by field lists, arms (P3d), blocks (P4).
fn skip_seps(tokens: []lexer::Token, pos: u64) -> u64 {
    mut p = pos
    loop {
        if !is_sep(tokens, p) { break }
        p++
    }
    p
}
```

### `src/parser/optokens.tks` — um predicado por nível de precedência (B.23)

```teko
// level 2 — unary prefix
fn is_unary(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Minus || k == lexer::TokenKind::Tilde || k == lexer::TokenKind::Bang
}
// level 3 — shift
fn is_shift(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Shl || k == lexer::TokenKind::Shr
}
// level 4 — multiplicative `* / %` AND bitwise `&` (Julia model; AND≈*)
fn is_multiplicative(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Star || k == lexer::TokenKind::Slash ||
    k == lexer::TokenKind::Percent || k == lexer::TokenKind::Amp
}
// level 5 — additive `+ -` AND bitwise `| ^` (OR/XOR≈+)
fn is_additive(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Plus || k == lexer::TokenKind::Minus ||
    k == lexer::TokenKind::Pipe || k == lexer::TokenKind::Caret
}
// level 6 — comparison
fn is_comparison(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Lt || k == lexer::TokenKind::Gt ||
    k == lexer::TokenKind::Le || k == lexer::TokenKind::Ge ||
    k == lexer::TokenKind::EqEq || k == lexer::TokenKind::Ne
}
// level 7 — logical AND
fn is_andand(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    tokens[pos].kind == lexer::TokenKind::AndAnd
}
// level 8 — logical OR
fn is_oror(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    tokens[pos].kind == lexer::TokenKind::OrOr
}
// statement level — assignment (plain `=` or any compound; B.4)
fn is_assign_op(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Assign    ||
    k == lexer::TokenKind::PlusEq    || k == lexer::TokenKind::MinusEq   ||
    k == lexer::TokenKind::StarEq    || k == lexer::TokenKind::SlashEq   ||
    k == lexer::TokenKind::PercentEq ||
    k == lexer::TokenKind::AmpEq     || k == lexer::TokenKind::PipeEq    ||
    k == lexer::TokenKind::CaretEq   ||
    k == lexer::TokenKind::ShlEq     || k == lexer::TokenKind::ShrEq
}
```

### Precedência do `to` e dos pós-fixos (B.23 — decisão da F2, descida em P2)

A escada de descida recursiva ganha **dois níveis** para os nós da F2 (predicados
`is_*` e as funções de parse correspondentes vêm na **P2**):

- **Pós-fixos `.` e chamada — o nível mais alto (primário pós-fixo).** `x.campo`
  (`FieldAccess`), `x.metodo(args)` (`MethodCall`) e `f(args)` (`Call`) ligam **mais
  forte** que tudo: aplicam-se em laço logo após um primário (`a.b.c`, `f(x).g`).
- **Cast `to` — logo **abaixo** do unário, **acima** de toda a escada binária.**
  `parse_shift` (nível 3) passa a chamar `parse_cast`, que chama `parse_unary`
  (nível 2). Consequência (M.3 — fica como escrito):
  - `-x to u32`  ⇒ `(-x) to u32`   *(unário liga mais forte que `to`)*
  - `x to u32 * 2` ⇒ `(x to u32) * 2` *(`to` liga mais forte que `*`)*
  - `x to u32 << 2` ⇒ `(x to u32) << 2` *(idem sobre shift; `u32 << 2` nem tiparia)*

Ordem final (forte → fraco): **primário/pós-fixo → unário → `to` → shift → mult →
add → comparação → `&&` → `||`**. O `to` é o único nível novo na cadeia binária; os
pós-fixos estendem o nível primário já existente.

---
## O parser — Parte 2: as funções de parse (P1: tipos)

A descida recursiva começa pelo topo da gramática de **tipos** (B.8 recursiva, B.14
variante). `parse_type` é o nível da **união** `|` — o operador de tipo de menor
precedência; abaixo dele, `parse_primary` decide entre **slice** `[]T` e **caminho
nomeado** `a::b::c`. Um único primário **não** vira `UnionType` (sem nó supérfluo —
M.5); o elemento de um slice é um primário, então `[]A | B` é `([]A) | B` (o slice liga
mais forte que `|`). As funções vivem em `teko::parser` (acesso a `lexer::TokenKind::X`
sem `use` é cross-namespace; os helpers de cursor são do mesmo namespace, bare).

### `src/parser/parse_path.tks` — caminhos `a::b::c` (compartilhado: tipos e chamadas)

```teko
// src/parser/parse_path.tks   (namespace 'teko::parser')
// A path: one or more Idents joined by `::` (B.14). At least one segment; fails (B.1)
// if there is no leading Ident. Shared by NamedType and by call callees.
fn parse_path(tokens: []lexer::Token, pos: u64) -> ParsedPath | error {
    if !is_kind_at(tokens, pos, lexer::TokenKind::Ident) {
        return error { message = "expected a name" }
    }
    mut segs = teko::list::empty()
    segs = teko::list::push(segs, Segment { name = tokens[pos].text })
    mut p = pos + 1
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::ColonColon) { break }
        // consume `::`, then demand a name after it
        if !is_kind_at(tokens, p + 1, lexer::TokenKind::Ident) {
            return error { message = "expected a name after '::'" }
        }
        segs = teko::list::push(segs, Segment { name = tokens[p + 1].text })
        p = p + 2
    }
    ParsedPath { node = Path { segments = segs }, next = p }
}
```

### `src/parser/parse_type.tks` — `parse_type` (união → primário → slice/nomeado)

```teko
// src/parser/parse_type.tks   (namespace 'teko::parser')

// A primary type: a slice `[]T` or a named path. (Members of a `|` union and a
// slice's element are primaries — the union level is `parse_type` below.)
fn parse_primary(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    if is_kind_at(tokens, pos, lexer::TokenKind::LBracket) {
        return parse_slice(tokens, pos)
    }
    parse_named(tokens, pos)
}

// A named type is a path: `u64`, `lexer::Token` (nominal — B.13).
fn parse_named(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    let pp = match parse_path(tokens, pos) { ParsedPath as x => x; error as e => return e }
    ParsedType { node = NamedType { path = pp.node }, next = pp.next }
}

// `[]T` — `pos` is at `[` (parse_primary checked). The element is a PRIMARY, so
// `[]A | B` is `([]A) | B`; nested slices recurse (`[][]T`). Recursive — B.8.
fn parse_slice(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    if !is_kind_at(tokens, pos + 1, lexer::TokenKind::RBracket) {
        return error { message = "expected ']' to close '[' in a slice type '[]T'" }
    }
    let elem = match parse_primary(tokens, pos + 2) { ParsedType as x => x; error as e => return e }
    ParsedType { node = SliceType { element = elem.node }, next = elem.next }
}

// THE TYPE ENTRY — the union level (`|`, the lowest-precedence type operator, B.14):
// one or more primaries. One → that primary (no needless UnionType — M.5); two or
// more → a UnionType.
fn parse_type(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    let first = match parse_primary(tokens, pos) { ParsedType as x => x; error as e => return e }
    mut members = teko::list::empty()
    members = teko::list::push(members, first.node)
    mut p = first.next
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Pipe) { break }
        let m = match parse_primary(tokens, p + 1) { ParsedType as x => x; error as e => return e }
        members = teko::list::push(members, m.node)
        p = m.next
    }
    if members.len == 1 { return ParsedType { node = first.node, next = p } }
    ParsedType { node = UnionType { members = members }, next = p }
}
```

#### C23 — `src/parser/parse_type.c` (mirror)

> Espelho fiel do `.tks`. Os structs da AST do parser (`tk_type_expr`, `tk_path`), os
> resultados (`tk_parsed_*`) e o cursor (`tk_is_kind_at`) são a infra do **port C do
> parser** (token-kinds via `tk_token_kind`/`TK_TOKEN_*` do lexer; lista crescível e
> `tk_box_type` = a indireção gerida feita concreta). Mostrados compactos.

```c
// --- parser AST (parser/ast.h, type part) ---
typedef struct { tk_str name; } tk_segment;
typedef struct { tk_segment *segments; size_t n; } tk_path;
typedef struct tk_type_expr tk_type_expr;
struct tk_type_expr {
    enum { TK_TYPE_EXPR_NAMED, TK_TYPE_EXPR_SLICE, TK_TYPE_EXPR_UNION } tag;
    union {
        tk_path named;                                      // NamedType
        struct { tk_type_expr *element; } slice;            // SliceType ([]T)
        struct { tk_type_expr *members; size_t n; } onion;  // UnionType (A|B|…)  ('union' is reserved)
    } as;
};

// --- results (parser/result.h) ---
typedef struct { tk_path      node; size_t next; } tk_parsed_path;
typedef struct { tk_type_expr node; size_t next; } tk_parsed_type;
TK_RESULT(tk_parsed_path, tk_parsed_path_result);   // tk_parsed_path | error
TK_RESULT(tk_parsed_type, tk_parsed_type_result);   // tk_parsed_type | error

// forward decls (mutual recursion: primary ↔ slice; type → primary)
static tk_parsed_type_result parse_primary(const tk_token *t, size_t n, size_t pos);

static tk_parsed_path_result parse_path(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT))
        return (tk_parsed_path_result){ .ok = false, .as.error = tk_error_make("expected a name") };
    tk_segment *segs = NULL; size_t ns = 0;
    tk_segs_push(&segs, &ns, (tk_segment){ .name = t[pos].text });
    size_t p = pos + 1;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_COLONCOLON)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT))
            return (tk_parsed_path_result){ .ok = false, .as.error = tk_error_make("expected a name after '::'") };
        tk_segs_push(&segs, &ns, (tk_segment){ .name = t[p + 1].text });
        p += 2;
    }
    return (tk_parsed_path_result){ .ok = true,
        .as.value = { .node = { .segments = segs, .n = ns }, .next = p } };
}

static tk_parsed_type_result parse_named(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos);
    if (!pp.ok) return (tk_parsed_type_result){ .ok = false, .as.error = pp.as.error };
    tk_type_expr ty = { .tag = TK_TYPE_EXPR_NAMED, .as.named = pp.as.value.node };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = pp.as.value.next } };
}

static tk_parsed_type_result parse_slice(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_RBRACKET))
        return (tk_parsed_type_result){ .ok = false,
            .as.error = tk_error_make("expected ']' to close '[' in a slice type '[]T'") };
    tk_parsed_type_result e = parse_primary(t, n, pos + 2);
    if (!e.ok) return e;
    tk_type_expr *elem = tk_box_type(e.as.value.node);   // compiler-managed indirection
    tk_type_expr ty = { .tag = TK_TYPE_EXPR_SLICE, .as.slice = { .element = elem } };
    return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = ty, .next = e.as.value.next } };
}

static tk_parsed_type_result parse_primary(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACKET)) return parse_slice(t, n, pos);
    return parse_named(t, n, pos);
}

tk_parsed_type_result tk_parse_type(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_type_result first = parse_primary(t, n, pos);
    if (!first.ok) return first;
    tk_type_expr *members = NULL; size_t nm = 0;
    tk_types_push(&members, &nm, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_type_result m = parse_primary(t, n, p + 1);
        if (!m.ok) return m;
        tk_types_push(&members, &nm, m.as.value.node);
        p = m.as.value.next;
    }
    if (nm == 1) return (tk_parsed_type_result){ .ok = true,
        .as.value = { .node = members[0], .next = p } };
    tk_type_expr ty = { .tag = TK_TYPE_EXPR_UNION, .as.onion = { .members = members, .n = nm } };
    return (tk_parsed_type_result){ .ok = true, .as.value = { .node = ty, .next = p } };
}
```

### `src/parser/parser_test.tkt` — testes do parser (P1: tipos)

```teko
// src/parser/parser_test.tkt — tests for teko::parser. NO main: PUBLIC `#test`
// functions the compiler collects (a runner — ALPHA). C mirror: a `main` harness.

// helper: lex `source`, then parse a type from position 0 — the TypeExpr, or error.
fn type_of(source: str) -> TypeExpr | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pt = match parse_type(toks, 0) { ParsedType as x => x; error as e => return e }
    pt.node
}

// does parsing a type from `source` fail (the barrier)?
fn type_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_type(toks, 0) { ParsedType => false; error => true }
}

// is `t` a single-segment NamedType named `name`?
fn named_is(t: TypeExpr, name: str) -> bool {
    match t {
        NamedType as n => n.path.segments.len == 1 && n.path.segments[0].name == name
        _              => false
    }
}

// the arity of a union type (0 if `t` is not a UnionType).
fn union_arity(t: TypeExpr) -> u64 {
    match t {
        UnionType as u => u.members.len
        _              => 0
    }
}

#test
fn parses_named_and_path() {
    // happy: a bare name, and a multi-segment path.
    let t = match type_of("u64") { TypeExpr as x => x; error => return }
    assert named_is(t, "u64")
    let p = match type_of("lexer::Token") { TypeExpr as x => x; error => return }
    match p {
        NamedType as n => {
            assert n.path.segments.len == 2
            assert n.path.segments[0].name == "lexer"
            assert n.path.segments[1].name == "Token"
        }
        _ => assert false
    }
}

#test
fn parses_slice_and_nesting() {
    // happy: []u64 and [][]u64 (B.8 recursion).
    let t = match type_of("[]u64") { TypeExpr as x => x; error => return }
    match t {
        SliceType as s => assert named_is(s.element, "u64")
        _              => assert false
    }
    let t2 = match type_of("[][]u64") { TypeExpr as x => x; error => return }
    match t2 {
        SliceType as s => match s.element {
            SliceType as s2 => assert named_is(s2.element, "u64")
            _               => assert false
        }
        _ => assert false
    }
}

#test
fn parses_union_and_single() {
    // happy: A | B (arity 2), A | B | C (arity 3); a single primary is NOT wrapped (M.5).
    let u2 = match type_of("A | B") { TypeExpr as x => x; error => return }
    assert union_arity(u2) == 2
    let u3 = match type_of("A | B | C") { TypeExpr as x => x; error => return }
    assert union_arity(u3) == 3
    let one = match type_of("A") { TypeExpr as x => x; error => return }
    assert union_arity(one) == 0
    assert named_is(one, "A")
    // precedence: []A | B is ([]A) | B — a 2-member union whose first member is a slice.
    let mix = match type_of("[]A | B") { TypeExpr as x => x; error => return }
    match mix {
        UnionType as u => {
            assert u.members.len == 2
            match u.members[0] { SliceType as s => assert named_is(s.element, "A"); _ => assert false }
        }
        _ => assert false
    }
}

#test
fn rejects_malformed_types() {
    // barrier: each must fail to parse (B.1).
    assert type_errors("[]")          // slice with no element
    assert type_errors("A |")         // union with no right-hand side
    assert type_errors("::")          // path with no leading name
    assert type_errors("lexer::")     // `::` with no name after
    assert type_errors("")            // no type at all
}
```

> **C mirror dos testes (harness `main`, alpha).** Os `.tkt` acima são canônicos; o
> harness C roda os mesmos casos — feliz: `tk_parse_type` aceita `u64`, `lexer::Token`,
> `[]u64`, `[][]u64`, `A | B` (aridade 2), `[]A | B` (união-de-2, 1º membro slice);
> barreira: rejeita `[]`, `A |`, `::`, `lexer::`, `""`. (Asserts diretos sobre `.tag`,
> `.as.onion.n` e `.as.named.segments`.)

---
## O parser — Parte 3: expressões (P2)

A escada de descida recursiva **é** a precedência (B.23). Por ser grande e mutuamente
recursiva (o átomo chama `parse_expr` de volta nos parênteses e nos argumentos), ela é
construída em **sub-etapas P2a–P2e**, e cada uma **re-enraíza** `parse_expr` no seu novo
topo — assim cada sub-etapa fica testável de imediato, sem stubs:

- **P2a** — átomos, chamadas, literais — `parse_expr := parse_atom`
- **P2b** — pós-fixos `.` (`FieldAccess`/`MethodCall`) — `:= parse_postfix`
- **P2c** — unário + cast `to` — `:= parse_cast`
- **P2d** — escada binária + comparação — `:= parse_comparison`
- **P2e** — lógicos `&& ||` + entrada final — `:= parse_or`

Sem genéricos/ponteiro-de-função (diferidos), cada nível é uma função concreta (o custo
M.5 já assumido na família `Parsed`). `if`/`match` como expressões ficam **diferidos**
até P3 (`parse_pattern`) + P4 (`parse_block`) — não se constrói sobre o que não existe
(M.4); `parse_atom` não os reconhece (caem em "expected an expression").

### P2a — átomos, chamadas e literais

A **folha** da escada: um literal (`Number`/`StrLit`/`ByteLit`), um nome/caminho/chamada,
ou `( expr )`. Um `Ident` inicia um caminho; depois dele, `(` → `Call`, um só segmento →
`Var`, senão → **`PathExpr`** (o caminho-valor qualificado, `PrimKind::U32` — nó novo,
gap que a AST F2/Parte-1 deixou). `parse_expr` aponta provisoriamente para `parse_atom`.

#### `src/parser/parse_lit.tks` — decodificação de literais (texto do token → valor)

```teko
// src/parser/parse_lit.tks   (namespace 'teko::parser')

// a Number token's text (decimal digits with `_` separators) → i64. (Other bases and
// overflow are not in the seed's literal path.) Dogfooding `to`: byte→i64 widens (ok).
fn lit_int(text: str) -> i64 {
    mut acc = 0
    mut i = 0
    loop {
        if i >= text.len { break }
        let c = text[i]
        if c != b'_' {
            acc = acc * 10 + (c to i64) - (b'0' to i64)
        }
        i++
    }
    acc
}

// a Byte token's text is the already-decoded octet (the lexer resolved escapes/quotes).
fn lit_byte(text: str) -> byte {
    text[0]
}
```

#### `src/parser/parse_expr.tks` — átomos (P2a)

```teko
// src/parser/parse_expr.tks   (namespace 'teko::parser')

// `( e, e, … )` — `pos` is at `(`. Empty `()` allowed; comma-separated; no trailing
// comma (strict — M.2). Returns the args and the position after `)`. Used by Call (here)
// and by MethodCall (P2b).
fn parse_call_args(tokens: []lexer::Token, pos: u64) -> ParsedArgs | error {
    mut p = pos + 1                                  // consume `(`
    mut args = teko::list::empty()
    if is_kind_at(tokens, p, lexer::TokenKind::RParen) {
        return ParsedArgs { args = args, next = p + 1 }
    }
    loop {
        let a = match parse_expr(tokens, p) { Parsed as x => x; error as e => return e }
        args = teko::list::push(args, a.node)
        p = a.next
        if !is_kind_at(tokens, p, lexer::TokenKind::Comma) { break }
        p = p + 1                                    // consume `,`
    }
    if !is_kind_at(tokens, p, lexer::TokenKind::RParen) {
        return error { message = "expected ')' to close the argument list" }
    }
    ParsedArgs { args = args, next = p + 1 }
}

// the LEAF: a literal, a name/path/call, `( expr )`, or an `if`/`match` expression (B.20/B.15).
fn parse_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if !has_token(tokens, pos) { return error { message = "expected an expression" } }
    let k = kind_at(tokens, pos)
    if k == lexer::TokenKind::Number {
        return Parsed { node = Number { value = lit_int(tokens[pos].text) }, next = pos + 1 }
    }
    if k == lexer::TokenKind::Str {
        return Parsed { node = StrLit { text = tokens[pos].text }, next = pos + 1 }
    }
    if k == lexer::TokenKind::Byte {
        return Parsed { node = ByteLit { value = lit_byte(tokens[pos].text) }, next = pos + 1 }
    }
    if k == lexer::TokenKind::LParen {
        let inner = match parse_expr(tokens, pos + 1) { Parsed as x => x; error as e => return e }
        if !is_kind_at(tokens, inner.next, lexer::TokenKind::RParen) {
            return error { message = "expected ')' to close a parenthesized expression" }
        }
        return Parsed { node = inner.node, next = inner.next + 1 }    // grouping is transparent
    }
    if k == lexer::TokenKind::If {
        return parse_if(tokens, pos)
    }
    if k == lexer::TokenKind::Match {
        return parse_match(tokens, pos)
    }
    if k == lexer::TokenKind::Ident {
        let pp = match parse_path(tokens, pos) { ParsedPath as x => x; error as e => return e }
        if is_kind_at(tokens, pp.next, lexer::TokenKind::LParen) {
            let ca = match parse_call_args(tokens, pp.next) { ParsedArgs as x => x; error as e => return e }
            return Parsed { node = Call { callee = pp.node, args = ca.args }, next = ca.next }
        }
        if pp.node.segments.len == 1 {
            return Parsed { node = Var { name = pp.node.segments[0].name }, next = pp.next }
        }
        return Parsed { node = PathExpr { path = pp.node }, next = pp.next }
    }
    error { message = "expected an expression" }
}

// THE EXPRESSION ENTRY — descends the whole ladder (P2 complete: or → and → comparison
// → additive → multiplicative → shift → cast → unary → postfix → atom).
fn parse_expr(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_or(tokens, pos)
}
```

#### C23 — `src/parser/parse_expr.c` (mirror, P2a)

> Espelho fiel. Nó novo: `tk_path_expr { tk_path path; }` + tag `TK_EXPR_PATH` (ordem das
> tags do `Expr` é livre — não serializada). `parse_path` vem do `parse_type.c` (P1).

```c
// parser/ast.h — one new Expr node: tk_path_expr; tag TK_EXPR_PATH.
typedef struct { tk_path path; } tk_path_expr;   // PrimKind::U32, module statics

// parser/result.h
typedef struct { tk_expr node; size_t next; }                 tk_parsed;
typedef struct { tk_expr *args; size_t n_args; size_t next; } tk_parsed_args;
TK_RESULT(tk_parsed,      tk_parsed_result);
TK_RESULT(tk_parsed_args, tk_parsed_args_result);

// forward decls (atom ↔ expr via paren/args)
tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos);

static tk_parsed_args_result parse_call_args(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;
    tk_expr *args = NULL; size_t na = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = 0, .next = p + 1 } };
    for (;;) {
        tk_parsed_result a = tk_parse_expr(t, n, p);
        if (!a.ok) return (tk_parsed_args_result){ .ok = false, .as.error = a.as.error };
        tk_exprs_push(&args, &na, a.as.value.node); p = a.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) break;
        p += 1;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_args_result){ .ok = false, .as.error = tk_error_make("expected ')' to close the argument list") };
    return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = na, .next = p + 1 } };
}

static tk_parsed_result parse_atom(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected an expression") };
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_NUMBER) { tk_expr e = { .tag = TK_EXPR_NUMBER, .as.number = { .value = tk_lit_int(t[pos].text) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } }; }
    if (k == TK_TOKEN_STR)    { tk_expr e = { .tag = TK_EXPR_STR,  .as.str  = { .text  = t[pos].text } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } }; }
    if (k == TK_TOKEN_BYTE)   { tk_expr e = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } }; }
    if (k == TK_TOKEN_LPAREN) {
        tk_parsed_result in = tk_parse_expr(t, n, pos + 1);
        if (!in.ok) return in;
        if (!tk_is_kind_at(t, n, in.as.value.next, TK_TOKEN_RPAREN))
            return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected ')' to close a parenthesized expression") };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = in.as.value.node, .next = in.as.value.next + 1 } };
    }
    if (k == TK_TOKEN_IF)    return parse_if(t, n, pos);
    if (k == TK_TOKEN_MATCH) return parse_match(t, n, pos);
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) return (tk_parsed_result){ .ok = false, .as.error = pp.as.error };
        size_t after = pp.as.value.next;
        if (tk_is_kind_at(t, n, after, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, after);
            if (!ca.ok) return (tk_parsed_result){ .ok = false, .as.error = ca.as.error };
            tk_expr e = { .tag = TK_EXPR_CALL, .as.call = { .callee = pp.as.value.node, .args = ca.as.value.args, .n_args = ca.as.value.n_args } };
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ca.as.value.next } };
        }
        if (pp.as.value.node.n == 1) {
            tk_expr e = { .tag = TK_EXPR_VAR, .as.var = { .name = pp.as.value.node.segments[0].name } };
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = after } };
        }
        tk_expr e = { .tag = TK_EXPR_PATH, .as.path = { .path = pp.as.value.node } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = after } };
    }
    return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected an expression") };
}

tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos) {
    return parse_or(t, n, pos);   // P2 complete: the whole ladder
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P2a)

```teko
// appended to parser_test.tkt — P2a expression-atom tests. #test collected.

// helper: lex `source`, parse an expression from 0 — the Expr, or error.
fn expr_of(source: str) -> Expr | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pe = match parse_expr(toks, 0) { Parsed as x => x; error as e => return e }
    pe.node
}

// does parsing an expression from `source` fail (the barrier)?
fn expr_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_expr(toks, 0) { Parsed => false; error => true }
}

#test
fn parses_literals_and_names() {
    match expr_of("42")  { Number as nn => assert nn.value == 42; _ => assert false }
    match expr_of("x")   { Var as v     => assert v.name == "x";  _ => assert false }
    match expr_of("b'A'"){ ByteLit as bl => assert bl.value == b'A'; _ => assert false }
    match expr_of("\"hi\"") { StrLit as s => assert s.text == "hi"; _ => assert false }
    // a qualified path as a value → PathExpr (PrimKind::U32 shape).
    match expr_of("PrimKind::U32") {
        PathExpr as pe => {
            assert pe.path.segments.len == 2
            assert pe.path.segments[0].name == "PrimKind"
            assert pe.path.segments[1].name == "U32"
        }
        _ => assert false
    }
}

#test
fn parses_calls_and_paren() {
    // f(x) → Call(1 arg); f() → Call(0); ns::f(x) → Call with a 2-segment callee.
    match expr_of("f(x)")     { Call as c => { assert c.callee.segments.len == 1; assert c.args.len == 1 } _ => assert false }
    match expr_of("f()")      { Call as c => assert c.args.len == 0; _ => assert false }
    match expr_of("ns::f(x)") { Call as c => assert c.callee.segments.len == 2; _ => assert false }
    // a parenthesized atom is transparent: (42) is just 42.
    match expr_of("(42)") { Number as nn => assert nn.value == 42; _ => assert false }
}

#test
fn rejects_malformed_atoms() {
    // barrier: each must fail (B.1). (Binary/operator cases arrive in P2c–P2e.)
    assert expr_errors("(1")     // unclosed paren
    assert expr_errors("f(")     // unclosed call, no arg
    assert expr_errors("f(x")    // unclosed call after an arg
    assert expr_errors("f(x,")   // trailing comma, no next arg
    assert expr_errors("+")      // not the start of an atom
    assert expr_errors("")       // empty
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `42`, `x`,
> `b'A'`, `"hi"`, `PrimKind::U32` (PathExpr), `f(x)`/`f()`/`ns::f(x)` (Call), `(42)`;
> barreira — `(1`, `f(`, `f(x`, `f(x,`, `+`, `""`. (Asserts sobre `.tag`, `.as.number.value`,
> `.as.call.n_args`, `.as.path.path.n`.)


### P2b — pós-fixos `.` (`FieldAccess` / `MethodCall`)

Logo acima da folha: uma cadeia de `.campo` e `.metodo(args)` aplicada após um átomo —
o nível mais forte depois do primário. `a.b`, `a.b().c`, `f(x).g`. Distinção pela AST:
`.nome` seguido de `(` é `MethodCall`; senão é `FieldAccess`. Esquerda-associativo (a
cadeia cresce o nó externo). Re-enraíza `parse_expr := parse_postfix`. (A chamada de um
caminho — `f(x)` — já é montada no `parse_atom`; o `(args)` aqui pertence ao método.)

#### `src/parser/parse_expr.tks` — pós-fixos (P2b)

```teko
// postfix: a chain of `.field` and `.method(args)` after an atom (tightest after the
// leaf). Left-assoc. `a.b`, `a.b().c`, `f(x).g`.
fn parse_postfix(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let prim = match parse_atom(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = prim.node
    mut p = prim.next
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Dot) { break }
        if !is_kind_at(tokens, p + 1, lexer::TokenKind::Ident) {
            return error { message = "expected a field or method name after '.'" }
        }
        let name = tokens[p + 1].text
        if is_kind_at(tokens, p + 2, lexer::TokenKind::LParen) {
            let ca = match parse_call_args(tokens, p + 2) { ParsedArgs as x => x; error as e => return e }
            node = MethodCall { receiver = node, method = name, args = ca.args }
            p = ca.next
        } else {
            node = FieldAccess { receiver = node, field = name }
            p = p + 2
        }
    }
    Parsed { node = node, next = p }
}
```

#### C23 — `src/parser/parse_expr.c` (mirror, P2b)

```c
// forward decl so tk_parse_expr (re-rooted to it) can call it.
static tk_parsed_result parse_postfix(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result parse_postfix(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result prim = parse_atom(t, n, pos);
    if (!prim.ok) return prim;
    tk_expr node = prim.as.value.node; size_t p = prim.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_DOT)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT))
            return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected a field or method name after '.'") };
        tk_str name = t[p + 1].text;
        if (tk_is_kind_at(t, n, p + 2, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, p + 2);
            if (!ca.ok) return (tk_parsed_result){ .ok = false, .as.error = ca.as.error };
            tk_expr m = { .tag = TK_EXPR_METHOD_CALL, .as.method_call = { .receiver = tk_box_expr(node), .method = name, .args = ca.as.value.args, .n_args = ca.as.value.n_args } };
            node = m; p = ca.as.value.next;
        } else {
            tk_expr f = { .tag = TK_EXPR_FIELD_ACCESS, .as.field_access = { .receiver = tk_box_expr(node), .field = name } };
            node = f; p = p + 2;
        }
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P2b)

```teko
// appended to parser_test.tkt — P2b postfix tests.

#test
fn parses_field_and_method() {
    // a.b → FieldAccess; a.b() → MethodCall(0 args); a.b(x) → MethodCall(1 arg).
    match expr_of("a.b") { FieldAccess as fa => assert fa.field == "b"; _ => assert false }
    match expr_of("a.b()")  { MethodCall as mc => { assert mc.method == "b"; assert mc.args.len == 0 } _ => assert false }
    match expr_of("a.b(x)") { MethodCall as mc => { assert mc.method == "b"; assert mc.args.len == 1 } _ => assert false }
}

#test
fn postfix_chains_left() {
    // a.b.c → FieldAccess(FieldAccess(a,b),c) — left-assoc.
    match expr_of("a.b.c") {
        FieldAccess as outer => {
            assert outer.field == "c"
            match outer.receiver { FieldAccess as inner => assert inner.field == "b"; _ => assert false }
        }
        _ => assert false
    }
    // f(x).g → FieldAccess(Call(f,[x]), g) — postfix applies after a call atom.
    match expr_of("f(x).g") {
        FieldAccess as fa => {
            assert fa.field == "g"
            match fa.receiver { Call as c => assert c.args.len == 1; _ => assert false }
        }
        _ => assert false
    }
}

#test
fn rejects_malformed_postfix() {
    assert expr_errors("a.")     // no name after `.`
    assert expr_errors("a.5")    // a number is not a field/method name
    assert expr_errors("a.b(")   // unclosed method-call args
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `a.b`
> (FieldAccess), `a.b()`/`a.b(x)` (MethodCall), `a.b.c` (cadeia à esquerda), `f(x).g`
> (pós-fixo sobre Call); barreira — `a.`, `a.5`, `a.b(`.


### P2c — unário `- ~ !` + cast `to`

Dois níveis acima dos pós-fixos. **Unário** (`- ~ !`): prefixo, direita-associativo
(`!!a` é `!(!a)`). **Cast** (`x to T`, F2): logo abaixo do unário e **acima de toda a
escada binária**, esquerda-associativo (`x to A to B` é `(x to A) to B`). O alvo do `to`
é um **tipo-primário** (`parse_primary`, P1) — `x to u32`, `x to []u8`, `x to lexer::Token`
— então `x to A | B` é `(x to A) | B` (o `|` é bitwise-or de valor; ver nota da P2). A
relação-chave: unário liga mais forte que `to`, logo `-x to u32` é `(-x) to u32`.
Re-enraíza `parse_expr := parse_cast`.

#### `src/parser/parse_expr.tks` — unário + cast (P2c)

```teko
// prefix unary `- ~ !` (right-assoc: `!!a` is `!(!a)`). Below `to`.
fn parse_unary(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if is_unary(tokens, pos) {
        let op = kind_at(tokens, pos)
        let operand = match parse_unary(tokens, pos + 1) { Parsed as x => x; error as e => return e }
        return Parsed { node = Unary { op = op, operand = operand.node }, next = operand.next }
    }
    parse_postfix(tokens, pos)
}

// cast `x to T` (F2) — below unary, above all binary. Left-assoc. The target is a
// type-PRIMARY (parse_primary, P1): `x to u32`, `x to []u8`, `x to lexer::Token`.
fn parse_cast(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_unary(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = first.node
    mut p = first.next
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::To) { break }
        let ty = match parse_primary(tokens, p + 1) { ParsedType as x => x; error as e => return e }
        node = Cast { expr = node, target = ty.node }
        p = ty.next
    }
    Parsed { node = node, next = p }
}
```

#### C23 — `src/parser/parse_expr.c` (mirror, P2c)

> `parse_cast` chama o **tipo-primário** do P1 — exposto como `tk_parse_type_primary`
> (no doc P1 ele aparece `static parse_primary`; o port C o expõe para uso cross-file).

```c
// forward decls so the re-rooted tk_parse_expr can reach parse_cast.
static tk_parsed_result parse_unary(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_cast(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result parse_unary(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_unary(t, n, pos)) {
        tk_token_kind op = t[pos].kind;
        tk_parsed_result o = parse_unary(t, n, pos + 1);
        if (!o.ok) return o;
        tk_expr e = { .tag = TK_EXPR_UNARY, .as.unary = { .op = op, .operand = tk_box_expr(o.as.value.node) } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = o.as.value.next } };
    }
    return parse_postfix(t, n, pos);
}

static tk_parsed_result parse_cast(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_unary(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_TO)) {
        tk_parsed_type_result ty = tk_parse_type_primary(t, n, p + 1);   // type-primary (P1)
        if (!ty.ok) return (tk_parsed_result){ .ok = false, .as.error = ty.as.error };
        tk_expr c = { .tag = TK_EXPR_CAST, .as.cast = { .expr = tk_box_expr(node), .target = ty.as.value.node } };
        node = c; p = ty.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P2c)

```teko
// appended to parser_test.tkt — P2c unary + cast tests.

#test
fn parses_unary() {
    match expr_of("-x") { Unary as u => assert u.op == lexer::TokenKind::Minus; _ => assert false }
    match expr_of("!a") { Unary as u => assert u.op == lexer::TokenKind::Bang;  _ => assert false }
    match expr_of("~b") { Unary as u => assert u.op == lexer::TokenKind::Tilde; _ => assert false }
    // right-assoc: !!a is !(!a).
    match expr_of("!!a") {
        Unary as outer => match outer.operand { Unary as inner => assert inner.op == lexer::TokenKind::Bang; _ => assert false }
        _ => assert false
    }
}

#test
fn parses_cast() {
    match expr_of("x to u32") { Cast as c => assert true; _ => assert false }
    // target may be a slice type.
    match expr_of("x to []u8") {
        Cast as c => match c.target { SliceType as s => assert true; _ => assert false }
        _ => assert false
    }
    // left-assoc: x to A to B is (x to A) to B — the outer Cast's expr is a Cast.
    match expr_of("x to A to B") {
        Cast as outer => match outer.expr { Cast as inner => assert true; _ => assert false }
        _ => assert false
    }
    // unary binds tighter than `to`: -x to u32 is (-x) to u32 — a Cast of a Unary.
    match expr_of("-x to u32") {
        Cast as c => match c.expr { Unary as u => assert u.op == lexer::TokenKind::Minus; _ => assert false }
        _ => assert false
    }
}

#test
fn rejects_malformed_unary_cast() {
    assert expr_errors("-")        // unary with no operand
    assert expr_errors("x to")     // cast with no target type
    assert expr_errors("to u32")   // `to` cannot start an expression
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `-x`/`!a`/`~b`,
> `!!a` (direita), `x to u32`, `x to []u8` (alvo slice), `x to A to B` (esquerda),
> `-x to u32` (Cast de Unary); barreira — `-`, `x to`, `to u32`.


### P2d — escada binária + comparação

Quatro níveis, cada binário com a **mesma forma** (analisa o mais-forte, repete sobre
seus operadores, monta `Binary` à esquerda). Pelos números do `optokens` (B.23), do mais
forte ao mais fraco: **shift** (3, `<< >>`) → **multiplicativo** (4, `* / %` e `&`) →
**aditivo** (5, `+ -` e `| ^`) → **comparação** (6). Logo `shift` liga mais forte que
`*`, e `&` mais forte que `|` (modelo Julia: `&`≈`*`, `|`/`^`≈`+`). A **comparação**
guarda a cadeia `a < b < c` como escrita (M.3 — sem reescrever para `a<b && b<c`): um
primeiro + uma lista de termos `(op, operando)`; sem termo → o primeiro nu. Re-enraíza
`parse_expr := parse_comparison`.

#### `src/parser/parse_expr.tks` — escada binária + comparação (P2d)

```teko
// level 3 — shift `<< >>` (left-assoc; the tightest binary, just above cast).
fn parse_shift(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_cast(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = first.node
    mut p = first.next
    loop {
        if !is_shift(tokens, p) { break }
        let op = kind_at(tokens, p)
        let rhs = match parse_cast(tokens, p + 1) { Parsed as x => x; error as e => return e }
        node = Binary { op = op, left = node, right = rhs.node }
        p = rhs.next
    }
    Parsed { node = node, next = p }
}

// level 4 — multiplicative `* / %` and bitwise `&` (left-assoc).
fn parse_multiplicative(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_shift(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = first.node
    mut p = first.next
    loop {
        if !is_multiplicative(tokens, p) { break }
        let op = kind_at(tokens, p)
        let rhs = match parse_shift(tokens, p + 1) { Parsed as x => x; error as e => return e }
        node = Binary { op = op, left = node, right = rhs.node }
        p = rhs.next
    }
    Parsed { node = node, next = p }
}

// level 5 — additive `+ -` and bitwise `| ^` (left-assoc).
fn parse_additive(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_multiplicative(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = first.node
    mut p = first.next
    loop {
        if !is_additive(tokens, p) { break }
        let op = kind_at(tokens, p)
        let rhs = match parse_multiplicative(tokens, p + 1) { Parsed as x => x; error as e => return e }
        node = Binary { op = op, left = node, right = rhs.node }
        p = rhs.next
    }
    Parsed { node = node, next = p }
}

// level 6 — comparison. A CHAIN `a < b < c` is kept as written (M.3): a first + a list
// of (op, operand) terms; no term → the bare first.
fn parse_comparison(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_additive(tokens, pos) { Parsed as x => x; error as e => return e }
    mut terms = teko::list::empty()
    mut p = first.next
    loop {
        if !is_comparison(tokens, p) { break }
        let op = kind_at(tokens, p)
        let rhs = match parse_additive(tokens, p + 1) { Parsed as x => x; error as e => return e }
        terms = teko::list::push(terms, CmpTerm { op = op, operand = rhs.node })
        p = rhs.next
    }
    if terms.len == 0 { return Parsed { node = first.node, next = p } }
    Parsed { node = Compare { first = first.node, rest = terms }, next = p }
}
```

#### C23 — `src/parser/parse_expr.c` (mirror, P2d)

> `parse_additive` é o exemplar; `parse_shift` (→`parse_cast`, `tk_is_shift`) e
> `parse_multiplicative` (→`parse_shift`, `tk_is_multiplicative`) têm forma **idêntica**
> (só mudam o predicado e a função mais-forte chamada). `parse_comparison` difere (monta
> `Compare`). Forward-decls para a re-raiz alcançar `parse_comparison`.

```c
static tk_parsed_result parse_shift(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_multiplicative(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_additive(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_comparison(const tk_token *t, size_t n, size_t pos);

// EXEMPLAR binary level — shift/multiplicative are identical but for predicate + callee.
static tk_parsed_result parse_additive(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_multiplicative(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_additive(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result r = parse_multiplicative(t, n, p + 1);
        if (!r.ok) return r;
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(r.as.value.node) } };
        node = b; p = r.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}
// parse_shift:          first=parse_cast;           while tk_is_shift;          callee parse_cast.
// parse_multiplicative: first=parse_shift;          while tk_is_multiplicative; callee parse_shift.

static tk_parsed_result parse_comparison(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_additive(t, n, pos);
    if (!first.ok) return first;
    tk_cmp_term *terms = NULL; size_t nt = 0; size_t p = first.as.value.next;
    while (tk_is_comparison(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result r = parse_additive(t, n, p + 1);
        if (!r.ok) return r;
        tk_terms_push(&terms, &nt, (tk_cmp_term){ .op = op, .operand = tk_box_expr(r.as.value.node) });
        p = r.as.value.next;
    }
    if (nt == 0) return (tk_parsed_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    tk_expr e = { .tag = TK_EXPR_COMPARE, .as.compare = { .first = tk_box_expr(first.as.value.node), .rest = terms, .n_rest = nt } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P2d)

```teko
// appended to parser_test.tkt — P2d binary + comparison tests.

// is `e` a Binary with op `op`?
fn binop_is(e: Expr, op: lexer::TokenKind) -> bool {
    match e { Binary as b => b.op == op; _ => false }
}

#test
fn binary_precedence() {
    // `1 + 2 * 3` is `1 + (2 * 3)`: a `+` whose right is a `*`.
    match expr_of("1 + 2 * 3") {
        Binary as b => { assert b.op == lexer::TokenKind::Plus; assert binop_is(b.right, lexer::TokenKind::Star) }
        _ => assert false
    }
    // `1 * 2 + 3` is `(1 * 2) + 3`: a `+` whose left is a `*`.
    match expr_of("1 * 2 + 3") {
        Binary as b => { assert b.op == lexer::TokenKind::Plus; assert binop_is(b.left, lexer::TokenKind::Star) }
        _ => assert false
    }
    // bitwise: `&` (mult level) binds tighter than `|` (add level): `a | b & c` is `a | (b & c)`.
    match expr_of("a | b & c") {
        Binary as b => { assert b.op == lexer::TokenKind::Pipe; assert binop_is(b.right, lexer::TokenKind::Amp) }
        _ => assert false
    }
}

#test
fn comparison_and_chains() {
    // `a < b` → Compare with 1 term.
    match expr_of("a < b") {
        Compare as cmp => { assert cmp.rest.len == 1; assert cmp.rest[0].op == lexer::TokenKind::Lt }
        _ => assert false
    }
    // `a < b < c` is kept as a chain (M.3): Compare with 2 terms.
    match expr_of("a < b < c") { Compare as cmp => assert cmp.rest.len == 2; _ => assert false }
    // additive binds tighter than comparison: `a + b < c` → Compare whose first is a `+`.
    match expr_of("a + b < c") {
        Compare as cmp => match cmp.first { Binary as b => assert b.op == lexer::TokenKind::Plus; _ => assert false }
        _ => assert false
    }
    // a lone additive is NOT wrapped in Compare (M.5 — no needless node).
    match expr_of("a + b") { Binary as b => assert b.op == lexer::TokenKind::Plus; _ => assert false }
}

#test
fn rejects_malformed_binary() {
    assert expr_errors("1 +")    // additive, no rhs
    assert expr_errors("1 *")    // multiplicative, no rhs
    assert expr_errors("1 <")    // comparison, no rhs
    assert expr_errors("<< 2")   // shift, no left operand
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `1 + 2 * 3` /
> `1 * 2 + 3` (mult sobre add), `a | b & c` (`&` sobre `|`), `a < b` / `a < b < c`
> (cadeia), `a + b < c` (add sobre comparação), `a + b` (não vira Compare); barreira —
> `1 +`, `1 *`, `1 <`, `<< 2`.


### P2e — lógicos `&&` `||` + entrada final (fecha o P2)

Os dois níveis mais fracos: **`&&`** (7) e **`||`** (8, o mais fraco). Mesma forma dos
demais binários, mas o `op` é fixo (não há outro operador no nível). `&&` liga mais forte
que `||` (`a && b || c` é `(a && b) || c`). Com `parse_or` no topo, `parse_expr` passa a
descer a **escada inteira** — o P2 está **completo**.

#### `src/parser/parse_expr.tks` — lógicos (P2e)

```teko
// level 7 — logical AND `&&` (left-assoc).
fn parse_and(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_comparison(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = first.node
    mut p = first.next
    loop {
        if !is_andand(tokens, p) { break }
        let rhs = match parse_comparison(tokens, p + 1) { Parsed as x => x; error as e => return e }
        node = Binary { op = lexer::TokenKind::AndAnd, left = node, right = rhs.node }
        p = rhs.next
    }
    Parsed { node = node, next = p }
}

// level 8 — logical OR `||` (left-assoc, the loosest; the ladder's top).
fn parse_or(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let first = match parse_and(tokens, pos) { Parsed as x => x; error as e => return e }
    mut node = first.node
    mut p = first.next
    loop {
        if !is_oror(tokens, p) { break }
        let rhs = match parse_and(tokens, p + 1) { Parsed as x => x; error as e => return e }
        node = Binary { op = lexer::TokenKind::OrOr, left = node, right = rhs.node }
        p = rhs.next
    }
    Parsed { node = node, next = p }
}
```

#### C23 — `src/parser/parse_expr.c` (mirror, P2e)

> Forma idêntica aos níveis binários, com `op` **fixo** (`TK_TOKEN_ANDAND`/`TK_TOKEN_OROR`).
> `parse_or` é o topo que a `tk_parse_expr` (re-enraizada) chama.

```c
static tk_parsed_result parse_and(const tk_token *t, size_t n, size_t pos);
static tk_parsed_result parse_or(const tk_token *t, size_t n, size_t pos);

static tk_parsed_result parse_and(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_comparison(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_andand(t, n, p)) {
        tk_parsed_result r = parse_comparison(t, n, p + 1);
        if (!r.ok) return r;
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_ANDAND, .left = tk_box_expr(node), .right = tk_box_expr(r.as.value.node) } };
        node = b; p = r.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_or(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result first = parse_and(t, n, pos);
    if (!first.ok) return first;
    tk_expr node = first.as.value.node; size_t p = first.as.value.next;
    while (tk_is_oror(t, n, p)) {
        tk_parsed_result r = parse_and(t, n, p + 1);
        if (!r.ok) return r;
        tk_expr b = { .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_OROR, .left = tk_box_expr(node), .right = tk_box_expr(r.as.value.node) } };
        node = b; p = r.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P2e + escada inteira)

```teko
// appended to parser_test.tkt — P2e logical tests + a full-ladder end-to-end check.

#test
fn parses_logical() {
    match expr_of("a && b") { Binary as b => assert b.op == lexer::TokenKind::AndAnd; _ => assert false }
    match expr_of("a || b") { Binary as b => assert b.op == lexer::TokenKind::OrOr;  _ => assert false }
    // `&&` binds tighter than `||`: `a && b || c` is `(a && b) || c`.
    match expr_of("a && b || c") {
        Binary as b => { assert b.op == lexer::TokenKind::OrOr; assert binop_is(b.left, lexer::TokenKind::AndAnd) }
        _ => assert false
    }
    // `a || b && c` is `a || (b && c)`: an `||` whose right is `&&`.
    match expr_of("a || b && c") {
        Binary as b => { assert b.op == lexer::TokenKind::OrOr; assert binop_is(b.right, lexer::TokenKind::AndAnd) }
        _ => assert false
    }
}

#test
fn full_ladder_end_to_end() {
    // `a + b < c && d` is `((a + b) < c) && d`: an `&&` whose left is a Compare whose
    // first is a `+`. Exercises additive → comparison → and in one shot.
    match expr_of("a + b < c && d") {
        Binary as top => {
            assert top.op == lexer::TokenKind::AndAnd
            match top.left {
                Compare as cmp => match cmp.first { Binary as p => assert p.op == lexer::TokenKind::Plus; _ => assert false }
                _ => assert false
            }
        }
        _ => assert false
    }
    // `x to u32 + 1` is `(x to u32) + 1`: cast binds tighter than additive.
    match expr_of("x to u32 + 1") {
        Binary as b => { assert b.op == lexer::TokenKind::Plus; match b.left { Cast as c => assert true; _ => assert false } }
        _ => assert false
    }
}

#test
fn rejects_malformed_logical() {
    assert expr_errors("a &&")   // no rhs
    assert expr_errors("a ||")   // no rhs
    assert expr_errors("|| b")   // no left operand
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `a && b`,
> `a || b`, `a && b || c` (`&&` sobre `||`), `a || b && c`; ponta-a-ponta
> `a + b < c && d` (add→comparação→and) e `x to u32 + 1` (cast sobre add); barreira —
> `a &&`, `a ||`, `|| b`.

---

> **P2 — `parse_expr` FECHADO.** A escada de precedência inteira (P2a átomos · P2b
> pós-fixos · P2c unário+cast · P2d binários+comparação · P2e lógicos) está parseável e
> testada (feliz + barreira em cada sub-etapa). Adicionado o nó `PathExpr` (gap da AST).
> **Diferido:** `if`/`match` como expressões — entram quando P3 (`parse_pattern`) e P4
> (`parse_block`) existirem (M.4), ligados ao `parse_atom`.


---
## O parser — Parte 4: padrões do `match` (P3)

O `match` tem dois eixos (B.15): o **eixo valor** (literal, range `..=`, alt `|`) e o
**eixo variante** (bind `Foo as x`, field `Foo { a; b }`), mais o curinga `_`. Construído
em sub-etapas: `parse_pattern_primary` é a base — **P3a** curinga+literal, **P3b** ganha
bind+field; `parse_pattern` (a entrada) ganha range+alt em **P3c**; `parse_arm` (`pattern
[when guard] => body`) vem em **P3d**. Como no P2, cada sub-etapa re-enraíza a entrada no
seu topo atual, ficando testável de imediato. Usa os literais do P2a (`lit_int`/`lit_byte`).

### P3a — curinga `_` + literal

A base mínima: `_` (Wildcard) ou um literal escalar (`Number`/`Str`/`Byte`), cujo
`LiteralPattern.value` é o nó-literal correspondente. `parse_pattern` aponta
provisoriamente para `parse_pattern_primary`.

#### `src/parser/parse_pattern.tks` — base: curinga + literal (P3a)

```teko
// src/parser/parse_pattern.tks   (namespace 'teko::parser')

// the BASE pattern: wildcard or a scalar literal. (Bind/field added in P3b; range/alt
// wrap this in parse_pattern at P3c.)
fn parse_pattern_primary(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    if !has_token(tokens, pos) { return error { message = "expected a pattern" } }
    let k = kind_at(tokens, pos)
    if k == lexer::TokenKind::Underscore {
        return ParsedPattern { node = WildcardPattern { }, next = pos + 1 }
    }
    if k == lexer::TokenKind::Number {
        return ParsedPattern { node = LiteralPattern { value = Number { value = lit_int(tokens[pos].text) } }, next = pos + 1 }
    }
    if k == lexer::TokenKind::Str {
        return ParsedPattern { node = LiteralPattern { value = StrLit { text = tokens[pos].text } }, next = pos + 1 }
    }
    if k == lexer::TokenKind::Byte {
        return ParsedPattern { node = LiteralPattern { value = ByteLit { value = lit_byte(tokens[pos].text) } }, next = pos + 1 }
    }
    if k == lexer::TokenKind::Ident {
        // `Foo as x` (bind), `Foo { f; g }` (field destructure), or bare `Foo` (match the
        // case, no binding). [P3b / P3b-field]
        let pp = match parse_path(tokens, pos) { ParsedPath as x => x; error as e => return e }
        if is_kind_at(tokens, pp.next, lexer::TokenKind::As) {
            if !is_kind_at(tokens, pp.next + 1, lexer::TokenKind::Ident) {
                return error { message = "expected a name after `as` in a pattern" }
            }
            return ParsedPattern { node = BindPattern { type_name = pp.node, has_binding = true, binding = tokens[pp.next + 1].text }, next = pp.next + 2 }
        }
        if is_kind_at(tokens, pp.next, lexer::TokenKind::LBrace) {
            let fns = match parse_field_names(tokens, pp.next) { ParsedNames as x => x; error as e => return e }
            return ParsedPattern { node = FieldPattern { type_name = pp.node, fields = fns.names }, next = fns.next }
        }
        return ParsedPattern { node = BindPattern { type_name = pp.node, has_binding = false, binding = "" }, next = pp.next }
    }
    error { message = "expected a pattern" }
}

// a primary pattern, optionally a range `lo ..= hi` (inclusive; both bounds must be
// literals — their Exprs are extracted). [P3c]
fn parse_pattern_range(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    let lo_pat = match parse_pattern_primary(tokens, pos) { ParsedPattern as x => x; error as e => return e }
    if !is_kind_at(tokens, lo_pat.next, lexer::TokenKind::DotDotEq) {
        return lo_pat
    }
    let lo = match lo_pat.node { LiteralPattern as lp => lp.value; _ => return error { message = "a range bound must be a literal" } }
    let hi_pat = match parse_pattern_primary(tokens, lo_pat.next + 1) { ParsedPattern as x => x; error as e => return e }
    let hi = match hi_pat.node { LiteralPattern as lp => lp.value; _ => return error { message = "a range bound must be a literal" } }
    ParsedPattern { node = RangePattern { lo = lo, hi = hi }, next = hi_pat.next }
}

// THE PATTERN ENTRY (P3c): the alt level — one or more range-or-primaries separated by
// `|`. One → that pattern (no needless AltPattern — M.5); two or more → an AltPattern.
fn parse_pattern(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    let first = match parse_pattern_range(tokens, pos) { ParsedPattern as x => x; error as e => return e }
    mut options = teko::list::empty()
    options = teko::list::push(options, first.node)
    mut p = first.next
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Pipe) { break }
        let opt = match parse_pattern_range(tokens, p + 1) { ParsedPattern as x => x; error as e => return e }
        options = teko::list::push(options, opt.node)
        p = opt.next
    }
    if options.len == 1 { return ParsedPattern { node = first.node, next = p } }
    ParsedPattern { node = AltPattern { options = options }, next = p }
}
```

#### C23 — `src/parser/parse_pattern.c` (mirror, P3a)

> O `tk_pattern` é tagged-union; aqui as variantes da P3a (P3b/P3c acrescentam as demais).
> A ordem das tags é livre — o `Pattern` do parser **não** é serializado.

```c
// parser/ast.h — pattern nodes (P3a subset).
typedef struct { tk_expr value; } tk_literal_pattern;   // a scalar literal
typedef struct { } tk_wildcard_pattern;                 // _
typedef struct { tk_path type_name; bool has_binding; tk_str binding; } tk_bind_pattern; // Foo / Foo as x  (P3b)
typedef struct { tk_path type_name; tk_str *fields; size_t n_fields; } tk_field_pattern; // Foo { f; g }  (P3b-field)
typedef struct tk_pattern tk_pattern;
typedef struct { tk_expr lo; tk_expr hi; } tk_range_pattern;             // lo ..= hi  (P3c)
typedef struct { tk_pattern *options; size_t n_options; } tk_alt_pattern; // a | b | …  (P3c)
struct tk_pattern {
    enum { TK_PAT_WILDCARD, TK_PAT_LITERAL, TK_PAT_BIND, TK_PAT_FIELD, TK_PAT_RANGE, TK_PAT_ALT } tag;
    union { tk_literal_pattern literal; tk_bind_pattern bind; tk_field_pattern field; tk_range_pattern range; tk_alt_pattern alt; } as;
};
typedef struct { tk_pattern node; size_t next; } tk_parsed_pattern;
TK_RESULT(tk_parsed_pattern, tk_parsed_pattern_result);

tk_parsed_pattern_result tk_parse_pattern(const tk_token *t, size_t n, size_t pos);

static tk_parsed_pattern_result parse_pattern_primary(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a pattern") };
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_UNDERSCORE) {
        tk_pattern p = { .tag = TK_PAT_WILDCARD };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_NUMBER) {
        tk_expr v = { .tag = TK_EXPR_NUMBER, .as.number = { .value = tk_lit_int(t[pos].text) } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_STR)  { tk_expr v = { .tag = TK_EXPR_STR,  .as.str  = { .text  = t[pos].text } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } }; }
    if (k == TK_TOKEN_BYTE) { tk_expr v = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } }; }
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) return (tk_parsed_pattern_result){ .ok = false, .as.error = pp.as.error };
        size_t after = pp.as.value.next;
        if (tk_is_kind_at(t, n, after, TK_TOKEN_AS)) {
            if (!tk_is_kind_at(t, n, after + 1, TK_TOKEN_IDENT))
                return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a name after `as` in a pattern") };
            tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = true, .binding = t[after + 1].text } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after + 2 } };
        }
        if (tk_is_kind_at(t, n, after, TK_TOKEN_LBRACE)) {
            tk_parsed_names_result fns = parse_field_names(t, n, after);
            if (!fns.ok) return (tk_parsed_pattern_result){ .ok = false, .as.error = fns.as.error };
            tk_pattern fp = { .tag = TK_PAT_FIELD, .as.field = { .type_name = pp.as.value.node, .fields = fns.as.value.names, .n_fields = fns.as.value.n_names } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = fp, .next = fns.as.value.next } };
        }
        tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = false, .binding = (tk_str){0} } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after } };
    }
    return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("expected a pattern") };
}

static tk_parsed_pattern_result parse_pattern_range(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result lo = parse_pattern_primary(t, n, pos);
    if (!lo.ok) return lo;
    if (!tk_is_kind_at(t, n, lo.as.value.next, TK_TOKEN_DOTDOTEQ)) return lo;
    if (lo.as.value.node.tag != TK_PAT_LITERAL)
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("a range bound must be a literal") };
    tk_expr lo_e = lo.as.value.node.as.literal.value;
    tk_parsed_pattern_result hi = parse_pattern_primary(t, n, lo.as.value.next + 1);
    if (!hi.ok) return hi;
    if (hi.as.value.node.tag != TK_PAT_LITERAL)
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_error_make("a range bound must be a literal") };
    tk_pattern p = { .tag = TK_PAT_RANGE, .as.range = { .lo = lo_e, .hi = hi.as.value.node.as.literal.value } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = hi.as.value.next } };
}

tk_parsed_pattern_result tk_parse_pattern(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result first = parse_pattern_range(t, n, pos);
    if (!first.ok) return first;
    tk_pattern *opts = NULL; size_t no = 0;
    tk_pats_push(&opts, &no, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_pattern_result o = parse_pattern_range(t, n, p + 1);
        if (!o.ok) return o;
        tk_pats_push(&opts, &no, o.as.value.node);
        p = o.as.value.next;
    }
    if (no == 1) return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    tk_pattern alt = { .tag = TK_PAT_ALT, .as.alt = { .options = opts, .n_options = no } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = alt, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P3a)

```teko
// appended to parser_test.tkt — P3a pattern tests.

// helper: lex `source`, parse a pattern from 0 — the Pattern, or error.
fn pattern_of(source: str) -> Pattern | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pp = match parse_pattern(toks, 0) { ParsedPattern as x => x; error as e => return e }
    pp.node
}

// does parsing a pattern from `source` fail (the barrier)?
fn pattern_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_pattern(toks, 0) { ParsedPattern => false; error => true }
}

#test
fn parses_wildcard_and_literals() {
    match pattern_of("_") { WildcardPattern => assert true; _ => assert false }
    match pattern_of("5") {
        LiteralPattern as lp => match lp.value { Number as nn => assert nn.value == 5; _ => assert false }
        _ => assert false
    }
    match pattern_of("\"s\"") {
        LiteralPattern as lp => match lp.value { StrLit as s => assert s.text == "s"; _ => assert false }
        _ => assert false
    }
    match pattern_of("b'A'") {
        LiteralPattern as lp => match lp.value { ByteLit as bl => assert bl.value == b'A'; _ => assert false }
        _ => assert false
    }
}

#test
fn rejects_non_patterns() {
    assert pattern_errors("+")    // not a pattern start
    assert pattern_errors("")     // empty
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `_` (Wildcard),
> `5`/`"s"`/`b'A'` (LiteralPattern com o nó-literal certo); barreira — `+`, `""`.


### P3b — bind `Foo as x` + caso nu `Foo` (eixo variante)

Um `Ident` inicia um caminho-tipo; depois dele, `as nome` → `BindPattern` que **liga** o
valor (`has_binding = true`); sem `as`, é o **caso nu** `Foo` — casa a case sem ligar
(`has_binding = false`), a forma que o próprio checker usa (`Byte =>`, `null =>`). O
`parse_pattern_primary` (acima, na P3a) ganhou o ramo `Ident`. **Refinamento da AST:**
`BindPattern` ganhou `has_binding` (a forma F2/Parte-1 supunha ligação sempre) — gap
real, pois o caso nu não tinha representação.

> **Field `Foo { a; b }` fica para a próxima sub-etapa** (P3b-field): ela carrega o
> **token `;`**, que o lexer ainda **não** reconhece (`read_symbol` cai em "unexpected
> character" no `;`) — uma lacuna que afeta também os arms (P3d) e blocos (P4). Bind e
> caso nu não precisam de `;`, então entram limpos aqui.

#### `src/parser/parser_test.tkt` — testes do parser (P3b)

```teko
// appended to parser_test.tkt — P3b bind + bare-case tests.

#test
fn parses_bind_and_bare() {
    // `Foo as x` → BindPattern with binding.
    match pattern_of("Foo as x") {
        BindPattern as bp => {
            assert bp.has_binding
            assert bp.binding == "x"
            assert bp.type_name.segments.len == 1
            assert bp.type_name.segments[0].name == "Foo"
        }
        _ => assert false
    }
    // bare `Foo` → BindPattern, no binding (matches the case; the checker's `Byte =>` shape).
    match pattern_of("Foo") { BindPattern as bp => assert !bp.has_binding; _ => assert false }
    // a qualified case: `lexer::Token as t`.
    match pattern_of("lexer::Token as t") {
        BindPattern as bp => { assert bp.has_binding; assert bp.type_name.segments.len == 2 }
        _ => assert false
    }
}

#test
fn rejects_malformed_bind() {
    assert pattern_errors("Foo as")    // `as` with no name
    assert pattern_errors("Foo as 5")  // a number is not a binding name
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `Foo as x`
> (BindPattern, `has_binding`), bare `Foo` (`!has_binding`), `lexer::Token as t`
> (caminho de 2 segmentos); barreira — `Foo as`, `Foo as 5`. (Asserts sobre `.tag`,
> `.as.bind.has_binding`, `.as.bind.binding`.)


### P3b-field — field `Foo { a; b }` + token `;`

O field destructura uma case-struct nomeando campos: `Foo { a; b }` → `FieldPattern`. O
`parse_pattern_primary` (na P3a) ganhou o ramo `{` (acima). Isto **exigiu o token `;`**,
que o lexer não tinha (`read_symbol` caía em "unexpected character" no `;`) — lacuna que
deixava o próprio checker inlexável. Adicionados: `Semicolon` (enum + `read_symbol`,
**no fim** do enum pelo motivo do ordinal), os helpers `is_sep`/`skip_seps` no `cursor`
(`;` ou newline, B.17 — reusados por arms/blocos), e `parse_field_names`. Separador entre
campos é `;`/newline; lista vazia e separador final permitidos.

#### `src/parser/parse_pattern.tks` — `parse_field_names` (P3b-field)

```teko
// `{ f; g }` — a field-name list; `;` or newline separates; empty `{ }` allowed; a
// trailing separator is allowed. `pos` is at `{`. (Shared shape with blocks/arms — B.17.)
fn parse_field_names(tokens: []lexer::Token, pos: u64) -> ParsedNames | error {
    mut p = skip_seps(tokens, pos + 1)        // consume `{`, skip leading separators
    mut names = teko::list::empty()
    if is_kind_at(tokens, p, lexer::TokenKind::RBrace) {
        return ParsedNames { names = names, next = p + 1 }
    }
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Ident) {
            return error { message = "expected a field name in `Type { … }`" }
        }
        names = teko::list::push(names, tokens[p].text)
        p = p + 1
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';', a newline, or '}' after a field name" }
        }
        p = skip_seps(tokens, p)
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }   // trailing separator
    }
    ParsedNames { names = names, next = p + 1 }                        // consume `}`
}
```

#### C23 — `src/parser/parse_pattern.c` (mirror, P3b-field)

```c
// parser/result.h
typedef struct { tk_str *names; size_t n_names; size_t next; } tk_parsed_names;
TK_RESULT(tk_parsed_names, tk_parsed_names_result);

// forward decl so parse_pattern_primary (above) can call it.
static tk_parsed_names_result parse_field_names(const tk_token *t, size_t n, size_t pos);

static tk_parsed_names_result parse_field_names(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_str *names = NULL; size_t nn = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE))
        return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = 0, .next = p + 1 } };
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_error_make("expected a field name in `Type { … }`") };
        tk_strs_push(&names, &nn, t[p].text); p += 1;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a field name") };
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
    }
    return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = nn, .next = p + 1 } };
}
```

> Lexer (`token.h`/`lexer.c`): `TK_TOKEN_SEMICOLON` acrescentado **por último** (motivo do
> ordinal) + `if (c == ';') return TK_TOKEN_SEMICOLON;`. Cursor (`cursor.c`): `tk_is_sep`
> (`;`/newline) + `tk_skip_seps`.

#### `src/parser/parser_test.tkt` — testes do parser (P3b-field)

```teko
// appended to parser_test.tkt — P3b-field field-pattern tests.

#test
fn parses_field_pattern() {
    // Foo { a; b } → FieldPattern with fields [a, b].
    match pattern_of("Foo { a; b }") {
        FieldPattern as fp => {
            assert fp.type_name.segments[0].name == "Foo"
            assert fp.fields.len == 2
            assert fp.fields[0] == "a"
            assert fp.fields[1] == "b"
        }
        _ => assert false
    }
    // an empty field list is allowed: Foo { }.
    match pattern_of("Foo { }") { FieldPattern as fp => assert fp.fields.len == 0; _ => assert false }
    // a single field.
    match pattern_of("Foo { a }") { FieldPattern as fp => assert fp.fields.len == 1; _ => assert false }
}

#test
fn rejects_malformed_field() {
    assert pattern_errors("Foo { a b }")   // two names with no separator
    assert pattern_errors("Foo { a;")      // unclosed
    assert pattern_errors("Foo { 5 }")     // a number is not a field name
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `Foo { a; b }`
> (2 campos), `Foo { }` (vazio), `Foo { a }` (1); barreira — `Foo { a b }`, `Foo { a;`,
> `Foo { 5 }`. Mais o teste de lexer `a; b` → `Ident · Semicolon · Ident`.


### P3c — range `lo ..= hi` + alt `a | b` (eixo valor)

A entrada `parse_pattern` passa a ser o nível **alt** (`|`): uma lista de
`range-ou-primário` separada por `|` — um só → o próprio padrão (sem `AltPattern`
supérfluo — M.5), dois ou mais → `AltPattern`. Abaixo, `parse_pattern_range` envolve o
primário com `..=` (inclusivo); os **limites têm que ser literais**, e seus `Expr` são
extraídos do `LiteralPattern` (`RangePattern.lo`/`hi` são `Expr`). Assim `1 ..= 5 | 10
..= 15` é um alt de dois ranges. Com isto o **eixo valor fecha** (literal · range · alt).

#### `src/parser/parser_test.tkt` — testes do parser (P3c)

```teko
// appended to parser_test.tkt — P3c range + alt tests.

#test
fn parses_range_and_alt() {
    // 1 ..= 5 → RangePattern with literal bounds.
    match pattern_of("1 ..= 5") {
        RangePattern as rp => {
            match rp.lo { Number as nn => assert nn.value == 1; _ => assert false }
            match rp.hi { Number as nn => assert nn.value == 5; _ => assert false }
        }
        _ => assert false
    }
    // 1 | 2 | 3 → AltPattern with 3 options.
    match pattern_of("1 | 2 | 3") { AltPattern as ap => assert ap.options.len == 3; _ => assert false }
    // a single pattern is NOT wrapped in Alt (M.5).
    match pattern_of("5") { LiteralPattern => assert true; _ => assert false }
    // alt of ranges: 1 ..= 5 | 10 ..= 15 → Alt of 2, each a Range.
    match pattern_of("1 ..= 5 | 10 ..= 15") {
        AltPattern as ap => {
            assert ap.options.len == 2
            match ap.options[0] { RangePattern => assert true; _ => assert false }
        }
        _ => assert false
    }
}

#test
fn rejects_malformed_range_alt() {
    assert pattern_errors("1 ..=")       // range with no hi
    assert pattern_errors("_ ..= 5")     // a range bound must be a literal (wildcard isn't)
    assert pattern_errors("1 |")         // alt with no right option
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `1 ..= 5`
> (RangePattern, limites literais), `1 | 2 | 3` (Alt de 3), `5` (não vira Alt),
> `1 ..= 5 | 10 ..= 15` (Alt de 2 ranges); barreira — `1 ..=`, `_ ..= 5`, `1 |`.
> (Asserts sobre `.tag`, `.as.range.lo`, `.as.alt.n_options`.)


### P3d — arms (`pattern [when guard] => body`) — fecha o P3

Um arm de `match`: um padrão, um `when guard` **opcional**, `=>`, e um corpo (que é uma
**expressão** — `match` é expressão, B.15). O `parse_guard` encapsula o opcional (ausente
→ `has_when=false`, com um `guard` placeholder `Number 0`, nunca lido — o flag o gateia,
como `doc=""` gateia `has_doc`). A guarda `when` **não** conta para exaustividade (M.1).
A **lista** de arms (`{ arm; arm }`) pertence ao `match`-expressão e entra no P4e (junto
com `parse_block`); aqui fica o arm unitário, já testável (usa `parse_pattern` + `parse_expr`).

#### `src/parser/parse_arm.tks` — guarda opcional + arm (P3d)

```teko
// src/parser/parse_arm.tks   (namespace 'teko::parser')

// an optional `when guard` before `=>`. Absent → has_when=false (guard is a placeholder
// Number 0, never read — gated by has_when). `pos` is right after the pattern.
fn parse_guard(tokens: []lexer::Token, pos: u64) -> Guard | error {
    if !is_kind_at(tokens, pos, lexer::TokenKind::When) {
        return Guard { has_when = false, guard = Number { value = 0 }, next = pos }
    }
    let g = match parse_expr(tokens, pos + 1) { Parsed as x => x; error as e => return e }
    Guard { has_when = true, guard = g.node, next = g.next }
}

// a match arm: `pattern [when guard] => body`. The body is an expression (B.15); the
// `when` guard does NOT count for exhaustiveness (M.1).
fn parse_arm(tokens: []lexer::Token, pos: u64) -> ParsedArm | error {
    let pat = match parse_pattern(tokens, pos) { ParsedPattern as x => x; error as e => return e }
    let g = match parse_guard(tokens, pat.next) { Guard as x => x; error as e => return e }
    if !is_kind_at(tokens, g.next, lexer::TokenKind::FatArrow) {
        return error { message = "expected '=>' after a match pattern" }
    }
    let body = match parse_expr(tokens, g.next + 1) { Parsed as x => x; error as e => return e }
    ParsedArm { node = Arm { pattern = pat.node, has_when = g.has_when, guard = g.guard, body = body.node }, next = body.next }
}
```

#### C23 — `src/parser/parse_arm.c` (mirror, P3d)

```c
// parser/ast.h — the arm; parser/result.h — the guard + parsed-arm results.
typedef struct { tk_pattern pattern; bool has_when; tk_expr guard; tk_expr body; } tk_arm;
typedef struct { bool has_when; tk_expr guard; size_t next; } tk_guard;
typedef struct { tk_arm node; size_t next; } tk_parsed_arm;
TK_RESULT(tk_guard,      tk_guard_result);
TK_RESULT(tk_parsed_arm, tk_parsed_arm_result);

static tk_guard_result parse_guard(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_WHEN)) {
        tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };   // placeholder, gated by has_when
        return (tk_guard_result){ .ok = true, .as.value = { .has_when = false, .guard = zero, .next = pos } };
    }
    tk_parsed_result g = tk_parse_expr(t, n, pos + 1);
    if (!g.ok) return (tk_guard_result){ .ok = false, .as.error = g.as.error };
    return (tk_guard_result){ .ok = true, .as.value = { .has_when = true, .guard = g.as.value.node, .next = g.as.value.next } };
}

tk_parsed_arm_result tk_parse_arm(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result pat = tk_parse_pattern(t, n, pos);
    if (!pat.ok) return (tk_parsed_arm_result){ .ok = false, .as.error = pat.as.error };
    tk_guard_result g = parse_guard(t, n, pat.as.value.next);
    if (!g.ok) return (tk_parsed_arm_result){ .ok = false, .as.error = g.as.error };
    if (!tk_is_kind_at(t, n, g.as.value.next, TK_TOKEN_FATARROW))
        return (tk_parsed_arm_result){ .ok = false, .as.error = tk_error_make("expected '=>' after a match pattern") };
    tk_parsed_result body = tk_parse_expr(t, n, g.as.value.next + 1);
    if (!body.ok) return (tk_parsed_arm_result){ .ok = false, .as.error = body.as.error };
    tk_arm arm = { .pattern = pat.as.value.node, .has_when = g.as.value.has_when, .guard = g.as.value.guard, .body = body.as.value.node };
    return (tk_parsed_arm_result){ .ok = true, .as.value = { .node = arm, .next = body.as.value.next } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P3d)

```teko
// appended to parser_test.tkt — P3d arm tests.

// helper: lex `source`, parse an arm from 0 — the Arm, or error.
fn arm_of(source: str) -> Arm | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pa = match parse_arm(toks, 0) { ParsedArm as x => x; error as e => return e }
    pa.node
}

fn arm_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_arm(toks, 0) { ParsedArm => false; error => true }
}

#test
fn parses_arms() {
    // `_ => 5`: wildcard, no guard, body 5.
    match arm_of("_ => 5") {
        Arm as a => {
            assert !a.has_when
            match a.pattern { WildcardPattern => assert true; _ => assert false }
            match a.body { Number as nn => assert nn.value == 5; _ => assert false }
        }
        _ => assert false
    }
    // `Foo as x => x`: bind pattern, body the bound var.
    match arm_of("Foo as x => x") {
        Arm as a => match a.pattern { BindPattern as bp => assert bp.binding == "x"; _ => assert false }
        _ => assert false
    }
    // `1 when c => 2`: a guard.
    match arm_of("1 when c => 2") {
        Arm as a => {
            assert a.has_when
            match a.guard { Var as v => assert v.name == "c"; _ => assert false }
        }
        _ => assert false
    }
    // `1 ..= 5 => 0`: a range-pattern arm.
    match arm_of("1 ..= 5 => 0") {
        Arm as a => match a.pattern { RangePattern => assert true; _ => assert false }
        _ => assert false
    }
}

#test
fn rejects_malformed_arms() {
    assert arm_errors("_ 5")          // no `=>`
    assert arm_errors("_ =>")         // no body
    assert arm_errors("_ when => 1")  // `when` with no guard expression
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `_ => 5`
> (sem guarda), `Foo as x => x`, `1 when c => 2` (guarda), `1 ..= 5 => 0` (range);
> barreira — `_ 5`, `_ =>`, `_ when => 1`.

---

> **P3 — padrões FECHADO.** As 6 variantes (P3a wildcard+literal · P3b bind/nu ·
> P3b-field field · P3c range+alt) + os arms (P3d, com guarda `when` opcional) parseiam,
> com par feliz+barreira. Refinamentos: nó `has_binding` (caso nu), token `;` e
> `is_sep`/`skip_seps` no cursor. A **lista** de arms `{ … }` e o `match`-expressão entram
> no **P4e** (junto com `parse_block`).


---
## O parser — Parte 5: statements e blocos (P4)

Statements **não** são uma escada de precedência — são um **dispatch** sobre o primeiro
token. Construído por sub-etapas, cada uma adicionando um tipo: **P4a** bloco + `ExprStmt`
+ `Return`; **P4b** bindings `let`/`mut`/`const`; **P4c** destructure + `Assign`; **P4d**
`loop`/`break`/`continue`; **P4e** liga `IfExpr`/`MatchExpr` ao `parse_atom` (usa o bloco
daqui + os arms do P3) — removendo o diferimento do P2. `parse_block` existe desde já (só
itera `parse_statement`), com o separador `;`/newline (B.17, reusa `is_sep`/`skip_seps`).

### P4a — bloco + `ExprStmt` + `Return`

`parse_statement`: `return [expr]` (valor opcional — `return` nu termina num separador ou
`}`) ou, por omissão, uma **expressão nua** (`ExprStmt`). `parse_block`: sequência de
statements em `{ … }`, vazio e separador final permitidos. **Refinamento da AST:** `Return`
ganhou `has_value` — o `return` nu (`error => return`, usado em funções void) não tinha
representação.

#### `src/parser/parse_stmt.tks` — dispatch + bloco (P4a)

```teko
// src/parser/parse_stmt.tks   (namespace 'teko::parser')

// dispatch a statement. P4a: `return [expr]` and a bare-expression statement. P4b–P4d add
// bindings, assignment, loop/break/continue.
fn parse_statement(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    if !has_token(tokens, pos) { return error { message = "expected a statement" } }
    if is_kind_at(tokens, pos, lexer::TokenKind::Return) {
        // `return` with an optional value: bare `return` ends at a separator or `}`.
        let after = pos + 1
        if !has_token(tokens, after) || is_sep(tokens, after) || is_kind_at(tokens, after, lexer::TokenKind::RBrace) {
            return ParsedStmt { node = Return { has_value = false, value = Number { value = 0 } }, next = after }
        }
        let v = match parse_expr(tokens, after) { Parsed as x => x; error as e => return e }
        return ParsedStmt { node = Return { has_value = true, value = v.node }, next = v.next }
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Loop) {
        if !is_kind_at(tokens, pos + 1, lexer::TokenKind::LBrace) {
            return error { message = "expected '{' after `loop`" }
        }
        let blk = match parse_block(tokens, pos + 1) { ParsedBlock as x => x; error as e => return e }
        return ParsedStmt { node = LoopStmt { body = blk.statements }, next = blk.next }
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Break) {
        return ParsedStmt { node = BreakStmt { }, next = pos + 1 }
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Continue) {
        return ParsedStmt { node = ContinueStmt { }, next = pos + 1 }
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Let) || is_kind_at(tokens, pos, lexer::TokenKind::Mut) || is_kind_at(tokens, pos, lexer::TokenKind::Const) {
        return parse_binding(tokens, pos)
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Ident) && is_assign_op(tokens, pos + 1) {
        return parse_assign(tokens, pos)
    }
    let e = match parse_expr(tokens, pos) { Parsed as x => x; error as e => return e }
    ParsedStmt { node = ExprStmt { expr = e.node }, next = e.next }
}

// a `{ … }` block: a sequence of statements, `;`/newline separated; empty `{ }` allowed;
// trailing separator allowed. `pos` is at `{`. (Same separator shape as field lists — B.17.)
fn parse_block(tokens: []lexer::Token, pos: u64) -> ParsedBlock | error {
    mut p = skip_seps(tokens, pos + 1)   // consume `{`, skip leading separators
    mut stmts = teko::list::empty()
    loop {
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }
        let s = match parse_statement(tokens, p) { ParsedStmt as x => x; error as e => return e }
        stmts = teko::list::push(stmts, s.node)
        p = s.next
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';', a newline, or '}' after a statement" }
        }
        p = skip_seps(tokens, p)
    }
    ParsedBlock { statements = stmts, next = p + 1 }   // consume `}`
}
```

#### C23 — `src/parser/parse_stmt.c` (mirror, P4a)

```c
// parser/ast.h — statement nodes (P4a subset). parser/result.h — the results.
typedef struct { bool has_value; tk_expr value; } tk_return;   // return [expr]
typedef struct { tk_expr expr; } tk_expr_stmt;                 // a bare expression
typedef struct tk_statement tk_statement;
typedef struct { tk_statement *body; size_t n; } tk_loop_stmt; // loop { … }  (P4d)
struct tk_statement {
    enum { TK_STMT_RETURN, TK_STMT_EXPR, TK_STMT_BINDING, TK_STMT_ASSIGN, TK_STMT_LOOP, TK_STMT_BREAK, TK_STMT_CONTINUE } tag;
    union { tk_return ret; tk_expr_stmt expr_stmt; tk_binding binding; tk_assign assign; tk_loop_stmt loop_stmt; } as;
};
typedef struct { tk_statement node; size_t next; } tk_parsed_stmt;
typedef struct { tk_statement *statements; size_t n; size_t next; } tk_parsed_block;
TK_RESULT(tk_parsed_stmt,  tk_parsed_stmt_result);
TK_RESULT(tk_parsed_block, tk_parsed_block_result);

tk_parsed_stmt_result tk_parse_statement(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected a statement") };
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_RETURN)) {
        size_t after = pos + 1;
        if (!tk_has_token(t, n, after) || tk_is_sep(t, n, after) || tk_is_kind_at(t, n, after, TK_TOKEN_RBRACE)) {
            tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };
            tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = false, .value = zero } };
            return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = after } };
        }
        tk_parsed_result v = tk_parse_expr(t, n, after);
        if (!v.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error };
        tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = true, .value = v.as.value.node } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LOOP)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE))
            return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected '{' after `loop`") };
        tk_parsed_block_result blk = tk_parse_block(t, n, pos + 1);
        if (!blk.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = blk.as.error };
        tk_statement s = { .tag = TK_STMT_LOOP, .as.loop_stmt = { .body = blk.as.value.statements, .n = blk.as.value.n } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = blk.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_BREAK)) {
        tk_statement s = { .tag = TK_STMT_BREAK };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = pos + 1 } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_CONTINUE)) {
        tk_statement s = { .tag = TK_STMT_CONTINUE };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = pos + 1 } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LET) || tk_is_kind_at(t, n, pos, TK_TOKEN_MUT) || tk_is_kind_at(t, n, pos, TK_TOKEN_CONST))
        return parse_binding(t, n, pos);
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT) && tk_is_assign_op(t, n, pos + 1))
        return parse_assign(t, n, pos);
    tk_parsed_result e = tk_parse_expr(t, n, pos);
    if (!e.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = e.as.error };
    tk_statement s = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = e.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = e.as.value.next } };
}

tk_parsed_block_result tk_parse_block(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);
    tk_statement *stmts = NULL; size_t ns = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) return (tk_parsed_block_result){ .ok = false, .as.error = s.as.error };
        tk_stmts_push(&stmts, &ns, s.as.value.node); p = s.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_block_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a statement") };
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_block_result){ .ok = true, .as.value = { .statements = stmts, .n = ns, .next = p + 1 } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P4a)

```teko
// appended to parser_test.tkt — P4a block + statement tests.

// helper: lex `source`, parse a block from 0 — the []Statement, or error.
fn block_of(source: str) -> []Statement | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pb = match parse_block(toks, 0) { ParsedBlock as x => x; error as e => return e }
    pb.statements
}

fn block_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_block(toks, 0) { ParsedBlock => false; error => true }
}

#test
fn parses_block_stmts() {
    // empty block.
    match block_of("{ }") { ss: []Statement => assert ss.len == 0; error => assert false }
    // a bare-expression statement.
    let one = match block_of("{ x }") { ss: []Statement => ss; error => return }
    assert one.len == 1
    match one[0] {
        ExprStmt as es => match es.expr { Var as v => assert v.name == "x"; _ => assert false }
        _ => assert false
    }
    // return with a value.
    let r = match block_of("{ return 5 }") { ss: []Statement => ss; error => return }
    match r[0] {
        Return as ret => { assert ret.has_value; match ret.value { Number as nn => assert nn.value == 5; _ => assert false } }
        _ => assert false
    }
    // bare return (no value).
    let br = match block_of("{ return }") { ss: []Statement => ss; error => return }
    match br[0] { Return as ret => assert !ret.has_value; _ => assert false }
    // two statements separated by `;`.
    match block_of("{ a; b }") { ss: []Statement => assert ss.len == 2; error => assert false }
}

#test
fn rejects_malformed_block() {
    assert block_errors("{ a b }")    // two statements with no separator
    assert block_errors("{ a")        // unclosed block
    assert block_errors("{ + }")      // not a valid statement
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `{ }` (vazio),
> `{ x }` (ExprStmt), `{ return 5 }` (Return com valor), `{ return }` (`!has_value`),
> `{ a; b }` (2 statements); barreira — `{ a b }`, `{ a`, `{ + }`. (Asserts sobre `.tag`,
> `.as.ret.has_value`, `.n`.)


### P4b — bindings `let` / `mut` / `const` (+ anotação `: T`)

`let|mut|const NOME [: T] = valor` (B.13, B.21). O `parse_statement` ganhou o ramo de
dispatch (acima). O alvo aqui é um **nome simples** (`SimpleName`); o destructure
(`{ x; y }`) entra no P4c. A anotação `: T` opcional é encapsulada em `parse_annotation`
(ausente → `has_type=false`, `type_ann` um placeholder `no_type()`, nunca lido — gateado
pelo flag). Sem mudança de AST: `BindKind`, `SimpleName`, `BindTarget` e `Annotation` já
existem.

#### `src/parser/parse_stmt.tks` — anotação + binding (P4b)

```teko
// a placeholder TypeExpr for an absent annotation (never read — gated by has_type).
fn no_type() -> TypeExpr {
    NamedType { path = Path { segments = teko::list::empty() } }
}

// an optional `: T` annotation. Absent → has_type=false (type_ann a placeholder). `pos`
// is where the `:` would be.
fn parse_annotation(tokens: []lexer::Token, pos: u64) -> Annotation | error {
    if !is_kind_at(tokens, pos, lexer::TokenKind::Colon) {
        return Annotation { has_type = false, type_ann = no_type(), next = pos }
    }
    let ty = match parse_type(tokens, pos + 1) { ParsedType as x => x; error as e => return e }
    Annotation { has_type = true, type_ann = ty.node, next = ty.next }
}

// `let|mut|const TARGET [: T] = value` (B.13, B.21). TARGET is a name or a destructure
// `{ x; y }` (parse_bind_target).
fn parse_binding(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let kw = kind_at(tokens, pos)
    mut kind = BindKind::Let
    if kw == lexer::TokenKind::Mut   { kind = BindKind::Mut }
    if kw == lexer::TokenKind::Const { kind = BindKind::Const }
    let tgt = match parse_bind_target(tokens, pos + 1) { ParsedTarget as x => x; error as e => return e }
    let ann = match parse_annotation(tokens, tgt.next) { Annotation as x => x; error as e => return e }
    if !is_kind_at(tokens, ann.next, lexer::TokenKind::Assign) {
        return error { message = "expected '=' in a binding" }
    }
    let v = match parse_expr(tokens, ann.next + 1) { Parsed as x => x; error as e => return e }
    ParsedStmt { node = Binding { kind = kind, target = tgt.node, has_type = ann.has_type, type_ann = ann.type_ann, value = v.node }, next = v.next }
}
```

#### C23 — `src/parser/parse_stmt.c` (mirror, P4b)

> Os structs `tk_bind_target`/`tk_binding`/`tk_annotation` entram com a AST de statements
> (antes da union, que já referencia `tk_binding`). Forward-decl de `parse_binding` para o
> `parse_statement` (acima) alcançá-lo.

```c
// parser/ast.h — binding target + binding; parser/result.h — annotation result.
typedef struct { tk_str name; } tk_simple_name;
typedef struct { tk_str *names; size_t n_names; } tk_destructure_target;   // let { x; y } = …  (P4c)
typedef struct {
    enum { TK_TARGET_NAME, TK_TARGET_DESTRUCTURE } tag;
    union { tk_simple_name name; tk_destructure_target destructure; } as;
} tk_bind_target;
typedef enum { TK_BIND_LET, TK_BIND_MUT, TK_BIND_CONST } tk_bind_kind;
typedef struct { tk_bind_kind kind; tk_bind_target target; bool has_type; tk_type_expr type_ann; tk_expr value; } tk_binding;
typedef struct { bool has_type; tk_type_expr type_ann; size_t next; } tk_annotation;
TK_RESULT(tk_annotation, tk_annotation_result);

static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos);  // forward (parse_statement calls it)

static tk_type_expr no_type(void) {
    return (tk_type_expr){ .tag = TK_TYPE_EXPR_NAMED, .as.named = { .segments = NULL, .n = 0 } };
}

static tk_annotation_result parse_annotation(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_COLON))
        return (tk_annotation_result){ .ok = true, .as.value = { .has_type = false, .type_ann = no_type(), .next = pos } };
    tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
    if (!ty.ok) return (tk_annotation_result){ .ok = false, .as.error = ty.as.error };
    return (tk_annotation_result){ .ok = true, .as.value = { .has_type = true, .type_ann = ty.as.value.node, .next = ty.as.value.next } };
}

static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos) {
    tk_bind_kind kind = TK_BIND_LET;
    if (t[pos].kind == TK_TOKEN_MUT)   kind = TK_BIND_MUT;
    if (t[pos].kind == TK_TOKEN_CONST) kind = TK_BIND_CONST;
    tk_parsed_target_result tgt = parse_bind_target(t, n, pos + 1);
    if (!tgt.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = tgt.as.error };
    tk_annotation_result ann = parse_annotation(t, n, tgt.as.value.next);
    if (!ann.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = ann.as.error };
    if (!tk_is_kind_at(t, n, ann.as.value.next, TK_TOKEN_ASSIGN))
        return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_error_make("expected '=' in a binding") };
    tk_parsed_result v = tk_parse_expr(t, n, ann.as.value.next + 1);
    if (!v.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error };
    tk_statement s = { .tag = TK_STMT_BINDING, .as.binding = { .kind = kind, .target = tgt.as.value.node, .has_type = ann.as.value.has_type, .type_ann = ann.as.value.type_ann, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P4b)

```teko
// appended to parser_test.tkt — P4b binding tests.

// helper: parse a single statement from `source` — the Statement, or error.
fn stmt_of(source: str) -> Statement | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let ps = match parse_statement(toks, 0) { ParsedStmt as x => x; error as e => return e }
    ps.node
}

fn stmt_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_statement(toks, 0) { ParsedStmt => false; error => true }
}

#test
fn parses_bindings() {
    // let x = 1
    match stmt_of("let x = 1") {
        Binding as b => {
            assert b.kind == BindKind::Let
            assert !b.has_type
            match b.target { SimpleName as sn => assert sn.name == "x"; _ => assert false }
            match b.value { Number as nn => assert nn.value == 1; _ => assert false }
        }
        _ => assert false
    }
    // mut y: u8 = 2  (with annotation)
    match stmt_of("mut y: u8 = 2") {
        Binding as b => {
            assert b.kind == BindKind::Mut
            assert b.has_type
            match b.type_ann { NamedType as nt => assert nt.path.segments[0].name == "u8"; _ => assert false }
        }
        _ => assert false
    }
    // const Z = 3
    match stmt_of("const Z = 3") { Binding as b => assert b.kind == BindKind::Const; _ => assert false }
}

#test
fn rejects_malformed_bindings() {
    assert stmt_errors("let = 1")      // no name
    assert stmt_errors("let x 1")      // no `=`
    assert stmt_errors("let x =")      // no value
    assert stmt_errors("let x: = 1")   // annotation with no type
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `let x = 1`
> (Let, sem tipo), `mut y: u8 = 2` (Mut, com anotação), `const Z = 3` (Const); barreira —
> `let = 1`, `let x 1`, `let x =`, `let x: = 1`. (Asserts sobre `.tag`, `.as.binding.kind`,
> `.as.binding.has_type`.)


### P4c — destructure `let { x; y } = …` + `Assign` `x op= v`

Duas adições. O **destructure**: `parse_binding` agora chama `parse_bind_target` (acima
refatorado), que devolve `SimpleName` ou `DestructurePattern { names }` — reusando
`parse_field_names` para o `{ … }`. O **Assign** (`x = v`, `x += v`, …; B.4 — só
statement): `parse_statement` ganhou o ramo `Ident` seguido de operador de atribuição
(`is_assign_op`, já existente) → `parse_assign`. Sem mudança de AST: `DestructurePattern`,
`ParsedTarget` e `Assign` já existem. Atribuição a campo/índice (`x.f = …`) **não** está na
AST (só nome simples — seed); `x.f` parseia como expressão e o `=` sobra (erro no bloco).

#### `src/parser/parse_stmt.tks` — alvo + atribuição (P4c)

```teko
// a binding target: a simple name or a destructure `{ x; y }` (B.13). `pos` after the kw.
// Reuses parse_field_names for the `{ … }` name list.
fn parse_bind_target(tokens: []lexer::Token, pos: u64) -> ParsedTarget | error {
    if is_kind_at(tokens, pos, lexer::TokenKind::LBrace) {
        let names = match parse_field_names(tokens, pos) { ParsedNames as x => x; error as e => return e }
        return ParsedTarget { node = DestructurePattern { names = names.names }, next = names.next }
    }
    if !is_kind_at(tokens, pos, lexer::TokenKind::Ident) {
        return error { message = "expected a name or `{ … }` after `let`/`mut`/`const`" }
    }
    ParsedTarget { node = SimpleName { name = tokens[pos].text }, next = pos + 1 }
}

// `name op= value` (B.4 — assignment is statement-only). Simple-name target; the op
// (= += -= …) is captured. (is_assign_op already checked at dispatch.)
fn parse_assign(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let name = tokens[pos].text
    let op = kind_at(tokens, pos + 1)
    let v = match parse_expr(tokens, pos + 2) { Parsed as x => x; error as e => return e }
    ParsedStmt { node = Assign { name = name, op = op, value = v.node }, next = v.next }
}
```

#### C23 — `src/parser/parse_stmt.c` (mirror, P4c)

> `tk_assign` entra com a AST de statements (antes da union, que já o referencia); o
> destructure entrou em `tk_bind_target` (acima). Forward-decls de `parse_bind_target` e
> `parse_assign` para `parse_binding`/`parse_statement` (acima) alcançá-los.

```c
// parser/ast.h — assignment; parser/result.h — parsed-target result.
typedef struct { tk_str name; tk_token_kind op; tk_expr value; } tk_assign;   // name op= value
typedef struct { tk_bind_target node; size_t next; } tk_parsed_target;
TK_RESULT(tk_parsed_target, tk_parsed_target_result);

static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos);  // forward
static tk_parsed_stmt_result   parse_assign(const tk_token *t, size_t n, size_t pos);        // forward

static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACE)) {
        tk_parsed_names_result names = parse_field_names(t, n, pos);
        if (!names.ok) return (tk_parsed_target_result){ .ok = false, .as.error = names.as.error };
        tk_bind_target tgt = { .tag = TK_TARGET_DESTRUCTURE, .as.destructure = { .names = names.as.value.names, .n_names = names.as.value.n_names } };
        return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = names.as.value.next } };
    }
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT))
        return (tk_parsed_target_result){ .ok = false, .as.error = tk_error_make("expected a name or `{ … }` after `let`/`mut`/`const`") };
    tk_bind_target tgt = { .tag = TK_TARGET_NAME, .as.name = { .name = t[pos].text } };
    return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = pos + 1 } };
}

static tk_parsed_stmt_result parse_assign(const tk_token *t, size_t n, size_t pos) {
    tk_str name = t[pos].text;
    tk_token_kind op = t[pos + 1].kind;
    tk_parsed_result v = tk_parse_expr(t, n, pos + 2);
    if (!v.ok) return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error };
    tk_statement s = { .tag = TK_STMT_ASSIGN, .as.assign = { .name = name, .op = op, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P4c)

```teko
// appended to parser_test.tkt — P4c destructure + assignment tests.

#test
fn parses_destructure_and_assign() {
    // let { x; y } = p → Binding with a destructure target of 2 names.
    match stmt_of("let { x; y } = p") {
        Binding as b => match b.target {
            DestructurePattern as dp => { assert dp.names.len == 2; assert dp.names[0] == "x" }
            _ => assert false
        }
        _ => assert false
    }
    // x = 5 → Assign with op `=`.
    match stmt_of("x = 5") {
        Assign as a => { assert a.name == "x"; assert a.op == lexer::TokenKind::Assign }
        _ => assert false
    }
    // x += 1 → Assign with op `+=`.
    match stmt_of("x += 1") { Assign as a => assert a.op == lexer::TokenKind::PlusEq; _ => assert false }
    // a bare expression is still an ExprStmt, not an Assign.
    match stmt_of("x") { ExprStmt => assert true; _ => assert false }
}

#test
fn rejects_malformed_assign() {
    assert stmt_errors("x =")          // assignment with no value
    assert stmt_errors("let { x = p")  // unclosed destructure
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz —
> `let { x; y } = p` (destructure de 2), `x = 5` (Assign `=`), `x += 1` (`+=`), `x`
> (ExprStmt, não Assign); barreira — `x =`, `let { x = p`. (Asserts sobre `.tag`,
> `.as.binding.target.tag`, `.as.assign.op`.)


### P4d — `loop` + `break` + `continue`

`loop { … }` é o **único** laço primitivo (M.5) — `parse_statement` ganhou o ramo (acima)
que reusa `parse_block` para o corpo. `break` e `continue` são statements-palavra (nós de
zero campo). Sem funções novas nem mudança de AST (`LoopStmt`/`BreakStmt`/`ContinueStmt` já
existem). A regra "`break`/`continue` só dentro de loop" é do **checker** (não do parser).

```teko
// (added to parse_statement, P4d)
//   loop { body } → LoopStmt { body = <block> }   (reuses parse_block)
//   break         → BreakStmt { }
//   continue      → ContinueStmt { }
```

#### `src/parser/parser_test.tkt` — testes do parser (P4d)

```teko
// appended to parser_test.tkt — P4d loop/break/continue tests.

#test
fn parses_loop_break_continue() {
    // loop { x } → LoopStmt with a 1-statement body.
    match stmt_of("loop { x }") { LoopStmt as l => assert l.body.len == 1; _ => assert false }
    // an empty loop body.
    match stmt_of("loop { }") { LoopStmt as l => assert l.body.len == 0; _ => assert false }
    // break / continue.
    match stmt_of("break")    { BreakStmt => assert true;    _ => assert false }
    match stmt_of("continue") { ContinueStmt => assert true; _ => assert false }
    // loop { break } → a LoopStmt whose body is a single BreakStmt.
    match stmt_of("loop { break }") {
        LoopStmt as l => match l.body[0] { BreakStmt => assert true; _ => assert false }
        _ => assert false
    }
}

#test
fn rejects_malformed_loop() {
    assert stmt_errors("loop x")     // no `{` after loop
    assert stmt_errors("loop {")     // unclosed loop body
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `loop { x }`
> (corpo de 1), `loop { }` (vazio), `break`, `continue`, `loop { break }` (loop com break
> dentro); barreira — `loop x`, `loop {`. (Asserts sobre `.tag`, `.as.loop_stmt.n`.)


### P4e — `if`/`match` como expressões — fecha o P4 (remove o diferimento do P2)

`parse_atom` (na P2a) agora reconhece `if` e `match` (acima). **`parse_if`**: `if cond
{ then } [else { else } | else if …]` — `cond` via `parse_expr`, blocos via `parse_block`;
o `else if` vira o else-bloco com um único statement (o if aninhado). **`parse_match`**:
`match subject { arms }` — usa **`parse_arms`** (a lista de arms diferida do P3d), que
reusa `parse_arm` com o separador `;`/newline e exige ≥1 arm (M.1). Com isto **expressão,
statement e padrão fecham o ciclo mutuamente recursivo** — o parser está completo de ponta.

#### `src/parser/parse_if.tks` — if/else (P4e)

```teko
// src/parser/parse_if.tks   (namespace 'teko::parser')

// `if cond { then } [else { else } | else if …]` — if/else is an EXPRESSION (B.20).
// `pos` is at `if`. `else if` is the else-block holding the nested if as one statement.
fn parse_if(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let cond = match parse_expr(tokens, pos + 1) { Parsed as x => x; error as e => return e }
    if !is_kind_at(tokens, cond.next, lexer::TokenKind::LBrace) {
        return error { message = "expected '{' after the `if` condition" }
    }
    let then_blk = match parse_block(tokens, cond.next) { ParsedBlock as x => x; error as e => return e }
    let p = then_blk.next
    if !is_kind_at(tokens, p, lexer::TokenKind::Else) {
        return Parsed { node = IfExpr { cond = cond.node, then_blk = then_blk.statements, has_else = false, else_blk = teko::list::empty() }, next = p }
    }
    // `else if …` — the else branch is another if-expression, wrapped as one statement.
    if is_kind_at(tokens, p + 1, lexer::TokenKind::If) {
        let elif = match parse_if(tokens, p + 1) { Parsed as x => x; error as e => return e }
        let eb = teko::list::push(teko::list::empty(), ExprStmt { expr = elif.node })
        return Parsed { node = IfExpr { cond = cond.node, then_blk = then_blk.statements, has_else = true, else_blk = eb }, next = elif.next }
    }
    // `else { … }`
    if !is_kind_at(tokens, p + 1, lexer::TokenKind::LBrace) {
        return error { message = "expected '{' or `if` after `else`" }
    }
    let else_blk = match parse_block(tokens, p + 1) { ParsedBlock as x => x; error as e => return e }
    Parsed { node = IfExpr { cond = cond.node, then_blk = then_blk.statements, has_else = true, else_blk = else_blk.statements }, next = else_blk.next }
}
```

#### `src/parser/parse_match.tks` — lista de arms + match (P4e)

```teko
// src/parser/parse_match.tks   (namespace 'teko::parser')

// the arm list of a `match`: `{ arm; arm; … }` — `;`/newline separates; ≥1 arm (a match
// needs a branch — M.1). `pos` is at `{`.
fn parse_arms(tokens: []lexer::Token, pos: u64) -> ParsedArms | error {
    mut p = skip_seps(tokens, pos + 1)   // consume `{`, skip leading separators
    mut arms = teko::list::empty()
    loop {
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }
        let a = match parse_arm(tokens, p) { ParsedArm as x => x; error as e => return e }
        arms = teko::list::push(arms, a.node)
        p = a.next
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';', a newline, or '}' after a match arm" }
        }
        p = skip_seps(tokens, p)
    }
    if arms.len == 0 {
        return error { message = "a `match` needs at least one arm" }
    }
    ParsedArms { arms = arms, next = p + 1 }
}

// `match subject { arms }` — match is an EXPRESSION (B.15). `pos` is at `match`.
fn parse_match(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let subj = match parse_expr(tokens, pos + 1) { Parsed as x => x; error as e => return e }
    if !is_kind_at(tokens, subj.next, lexer::TokenKind::LBrace) {
        return error { message = "expected '{' after the `match` subject" }
    }
    let arms = match parse_arms(tokens, subj.next) { ParsedArms as x => x; error as e => return e }
    Parsed { node = MatchExpr { subject = subj.node, arms = arms.arms }, next = arms.next }
}
```

#### C23 — `src/parser/parse_if.c` + `parse_match.c` (mirror, P4e)

> `tk_if_expr`/`tk_match_expr` são os nós da AST de expressões (já com tags `TK_EXPR_IF`/
> `TK_EXPR_MATCH`). Forward-decls de `parse_if`/`parse_match` para o `parse_atom` (acima).

```c
// parser/ast.h — if/match expr nodes; parser/result.h — the arm-list result.
typedef struct { tk_expr *cond; tk_statement *then_blk; size_t n_then; bool has_else; tk_statement *else_blk; size_t n_else; } tk_if_expr;
typedef struct { tk_expr *subject; tk_arm *arms; size_t n_arms; } tk_match_expr;
typedef struct { tk_arm *arms; size_t n_arms; size_t next; } tk_parsed_arms;
TK_RESULT(tk_parsed_arms, tk_parsed_arms_result);

static tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos);     // forward
static tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos);  // forward

static tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result cond = tk_parse_expr(t, n, pos + 1);
    if (!cond.ok) return cond;
    if (!tk_is_kind_at(t, n, cond.as.value.next, TK_TOKEN_LBRACE))
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' after the `if` condition") };
    tk_parsed_block_result tb = tk_parse_block(t, n, cond.as.value.next);
    if (!tb.ok) return (tk_parsed_result){ .ok = false, .as.error = tb.as.error };
    size_t p = tb.as.value.next;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ELSE)) {
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node), .then_blk = tb.as.value.statements, .n_then = tb.as.value.n, .has_else = false, .else_blk = NULL, .n_else = 0 } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
    }
    if (tk_is_kind_at(t, n, p + 1, TK_TOKEN_IF)) {
        tk_parsed_result elif = parse_if(t, n, p + 1);
        if (!elif.ok) return elif;
        tk_statement *eb = NULL; size_t neb = 0;
        tk_statement es = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = elif.as.value.node } };
        tk_stmts_push(&eb, &neb, es);
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node), .then_blk = tb.as.value.statements, .n_then = tb.as.value.n, .has_else = true, .else_blk = eb, .n_else = neb } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = elif.as.value.next } };
    }
    if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_LBRACE))
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' or `if` after `else`") };
    tk_parsed_block_result ebk = tk_parse_block(t, n, p + 1);
    if (!ebk.ok) return (tk_parsed_result){ .ok = false, .as.error = ebk.as.error };
    tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node), .then_blk = tb.as.value.statements, .n_then = tb.as.value.n, .has_else = true, .else_blk = ebk.as.value.statements, .n_else = ebk.as.value.n } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ebk.as.value.next } };
}

static tk_parsed_arms_result parse_arms(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);
    tk_arm *arms = NULL; size_t na = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        tk_parsed_arm_result a = tk_parse_arm(t, n, p);
        if (!a.ok) return (tk_parsed_arms_result){ .ok = false, .as.error = a.as.error };
        tk_arms_push(&arms, &na, a.as.value.node); p = a.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_arms_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a match arm") };
        p = tk_skip_seps(t, n, p);
    }
    if (na == 0) return (tk_parsed_arms_result){ .ok = false, .as.error = tk_error_make("a `match` needs at least one arm") };
    return (tk_parsed_arms_result){ .ok = true, .as.value = { .arms = arms, .n_arms = na, .next = p + 1 } };
}

static tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result subj = tk_parse_expr(t, n, pos + 1);
    if (!subj.ok) return subj;
    if (!tk_is_kind_at(t, n, subj.as.value.next, TK_TOKEN_LBRACE))
        return (tk_parsed_result){ .ok = false, .as.error = tk_error_make("expected '{' after the `match` subject") };
    tk_parsed_arms_result arms = parse_arms(t, n, subj.as.value.next);
    if (!arms.ok) return (tk_parsed_result){ .ok = false, .as.error = arms.as.error };
    tk_expr e = { .tag = TK_EXPR_MATCH, .as.match_expr = { .subject = tk_box_expr(subj.as.value.node), .arms = arms.as.value.arms, .n_arms = arms.as.value.n_arms } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = arms.as.value.next } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P4e)

```teko
// appended to parser_test.tkt — P4e if/match-expression tests (reuses expr_of).

#test
fn parses_if_and_match() {
    // if c { a } → IfExpr, no else.
    match expr_of("if c { a }") {
        IfExpr as ie => { assert !ie.has_else; assert ie.then_blk.len == 1 }
        _ => assert false
    }
    // if c { a } else { b } → IfExpr with else.
    match expr_of("if c { a } else { b }") {
        IfExpr as ie => { assert ie.has_else; assert ie.else_blk.len == 1 }
        _ => assert false
    }
    // else-if chains: the else block holds a single (nested) IfExpr statement.
    match expr_of("if a { 1 } else if b { 2 } else { 3 }") {
        IfExpr as ie => {
            assert ie.has_else
            match ie.else_blk[0] {
                ExprStmt as es => match es.expr { IfExpr => assert true; _ => assert false }
                _ => assert false
            }
        }
        _ => assert false
    }
    // match subj { _ => 0 } → MatchExpr with 1 arm.
    match expr_of("match x { _ => 0 }") { MatchExpr as me => assert me.arms.len == 1; _ => assert false }
    // a match with two arms.
    match expr_of("match x { 1 => a; _ => b }") { MatchExpr as me => assert me.arms.len == 2; _ => assert false }
}

#test
fn rejects_malformed_if_match() {
    assert expr_errors("if c a")            // no `{` after condition
    assert expr_errors("if c { a } else")   // `else` with no block/if
    assert expr_errors("match x a")         // no `{` after subject
    assert expr_errors("match x { }")       // a match needs at least one arm
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `if c { a }`
> (sem else), `if c { a } else { b }`, cadeia `else if`, `match x { _ => 0 }`,
> `match x { 1 => a; _ => b }`; barreira — `if c a`, `if c { a } else`, `match x a`,
> `match x { }`.

---

> **P4 — statements/blocos FECHADO, e o parser de ponta está completo.** P4a bloco +
> ExprStmt + Return · P4b bindings · P4c destructure + Assign · P4d loop/break/continue ·
> P4e if/match-expressões. **O diferimento do P2 foi removido** — `if`/`match` parseiam.
> O ciclo expressão↔statement↔padrão fecha. Refinamento: `Return.has_value` (return nu);
> resultados novos `ParsedArms`. Falta **P5** (itens top-level) e **P6** (integração).


---
## Refatoração R-main — `main.tks` é uma `main` virtual

Correção da modelagem: um `.tks` é **ou** um **arquivo-main** (`main.tks`, a entrada do
executável) **ou** um **módulo** (biblioteca/importado). O `main.tks`, **após o cabeçalho
de `use`s**, É o corpo de uma `main` implícita — só `use` + statements, **sem** declarar
tipos ou funções. Um módulo é o oposto: `use` + declarações, **sem** statements soltos.
`main.tks` é **obrigatório** p/ `.tkp` `executable` e **proibido** p/ `library`. Subdividida
em **R-main-a** (esta — a AST), **R-main-b** (`parse_main_file`), **R-main-c**
(`parse_module`), **R-main-d** (regra do `.tkp`).

### R-main-a — a modelagem (AST de arquivo)

O antigo `Item = Function | TypeDecl | UseDecl | Statement` (que **misturava** declaração e
statement solto) e `Program = { items: []Item }` foram **aposentados**, e o resultado
`ParsedItem` virou `ParsedDecl` (acima, em `ast.tks`/`result.tks`). No lugar: `Decl`
(Function|TypeDecl), `MainFile` (uses + corpo), `Module` (uses + decls) e `File`
(MainFile|Module). É só estrutura — os parsers (`parse_main_file`/`parse_module`) e a regra
do `.tkp` vêm em R-main-b/c/d.

#### C23 — `src/parser/ast.h` (delta R-main-a — o modelo de arquivo)

> Os `tk_function`/`tk_type_decl`/`tk_use_decl` são os structs de declaração (vêm com o
> P5). Os antigos `tk_program`/`tk_item` (hoje assumidos pelo checker) são **aposentados**.

```c
// parser/ast.h — R-main: the file model (retires tk_program/tk_item).
typedef struct {
    enum { TK_DECL_FUNCTION, TK_DECL_TYPE } tag;
    union { tk_function function; tk_type_decl type_decl; } as;
} tk_decl;                                                                                  // a module's declaration
typedef struct { tk_use_decl *uses; size_t n_uses; tk_statement *body; size_t n_body; } tk_main_file;  // use header + virtual main body
typedef struct { tk_use_decl *uses; size_t n_uses; tk_decl *decls; size_t n_decls; } tk_module;        // use header + declarations
typedef struct {
    enum { TK_FILE_MAIN, TK_FILE_MODULE } tag;
    union { tk_main_file main_file; tk_module module; } as;
} tk_file;
typedef struct { tk_decl node; size_t next; } tk_parsed_decl;                               // was tk_parsed_item
```

> **Ripple para o checker (Fase C) — levantado, não tocado aqui.** O checker usa hoje
> `tk_program`/`tk_item` (`tk_check_program`, `tk_check_item`, `tk_collect`,
> `collect_types`). Com o modelo novo: checar um **`MainFile`** = checar o corpo de
> statements numa *main virtual* (sem coletar tipos próprios — `main.tks` não os declara);
> checar um **`Module`** = coletar `decls` e checar. Migrações: `tk_check_program` →
> `tk_check_main_file` / `tk_check_module`; `tk_check_item` → `tk_check_decl`;
> `collect_types(tk_item *)` → `collect_types(tk_decl *)`. R-main-a é só a AST do parser;
> a migração do checker fica para a **Fase C** (consta no roadmap).


### R-main-b — `parse_main_file` (cabeçalho `use` + corpo de statements)

Lê o `main.tks`: o **cabeçalho `use`** (zero ou mais `use a::b [as c]`) e depois o **corpo
de statements** (a main virtual), até o fim. **Rejeita** qualquer declaração `type`/`fn`
(`is_decl_start`) — `main.tks` só tem `use` + statements. O `parse_use` e o
`parse_use_header` são **compartilhados** (R-main-c/`parse_module` reusam; absorve o antigo
P5d). Resultados novos: `ParsedUse`/`ParsedUses`/`ParsedMainFile`.

#### `src/parser/parse_file.tks` — `use` + main file (R-main-b)

```teko
// src/parser/parse_file.tks   (namespace 'teko::parser')

// extends the Parsed* family (result.tks).
type ParsedUse      = struct { node: UseDecl;       next: u64 }   // one `use`
type ParsedUses     = struct { uses: []UseDecl;     next: u64 }   // a `use` header
type ParsedMainFile = struct { node: MainFile;      next: u64 }   // a parsed main.tks

// does the token at `pos` start a top-level declaration (`fn` or `type`)? (Shared with
// parse_module — R-main-c.)
fn is_decl_start(tokens: []lexer::Token, pos: u64) -> bool {
    is_kind_at(tokens, pos, lexer::TokenKind::Fn) || is_kind_at(tokens, pos, lexer::TokenKind::Type)
}

// `use a::b::c [as alias]` (B.33). `pos` is at `use`.
fn parse_use(tokens: []lexer::Token, pos: u64) -> ParsedUse | error {
    let pp = match parse_path(tokens, pos + 1) { ParsedPath as x => x; error as e => return e }
    mut has_alias = false
    mut alias = ""
    mut p = pp.next
    if is_kind_at(tokens, p, lexer::TokenKind::As) {
        if !is_kind_at(tokens, p + 1, lexer::TokenKind::Ident) {
            return error { message = "expected a name after `as` in a `use`" }
        }
        has_alias = true
        alias = tokens[p + 1].text
        p = p + 2
    }
    ParsedUse { node = UseDecl { path = pp.node, has_alias = has_alias, alias = alias }, next = p }
}

// the `use` header: zero or more `use` decls at the top, separator-terminated. Returns the
// uses and the position where the body/decls begin. (Shared with parse_module.)
fn parse_use_header(tokens: []lexer::Token, pos: u64) -> ParsedUses | error {
    mut p = skip_seps(tokens, pos)
    mut uses = teko::list::empty()
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Use) { break }
        let u = match parse_use(tokens, p) { ParsedUse as x => x; error as e => return e }
        uses = teko::list::push(uses, u.node)
        p = u.next
        if !has_token(tokens, p) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';' or a newline after a `use`" }
        }
        p = skip_seps(tokens, p)
    }
    ParsedUses { uses = uses, next = p }
}

// main.tks: the `use` header, then the VIRTUAL MAIN's body (statements). Rejects any
// type/function declaration. `pos` is 0 (file start).
fn parse_main_file(tokens: []lexer::Token, pos: u64) -> ParsedMainFile | error {
    let hdr = match parse_use_header(tokens, pos) { ParsedUses as x => x; error as e => return e }
    mut p = skip_seps(tokens, hdr.next)
    mut body = teko::list::empty()
    loop {
        if !has_token(tokens, p) { break }
        if is_decl_start(tokens, p) {
            return error { message = "main.tks is a virtual main: it may not declare types or functions (only `use` + statements)" }
        }
        let s = match parse_statement(tokens, p) { ParsedStmt as x => x; error as e => return e }
        body = teko::list::push(body, s.node)
        p = s.next
        if !has_token(tokens, p) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';' or a newline after a statement" }
        }
        p = skip_seps(tokens, p)
    }
    ParsedMainFile { node = MainFile { uses = hdr.uses, body = body }, next = p }
}
```

#### C23 — `src/parser/parse_file.c` (mirror, R-main-b)

```c
// parser/result.h — new results.
typedef struct { tk_use_decl node; size_t next; }                 tk_parsed_use;
typedef struct { tk_use_decl *uses; size_t n_uses; size_t next; } tk_parsed_uses;
typedef struct { tk_main_file node; size_t next; }                tk_parsed_main_file;
TK_RESULT(tk_parsed_use,       tk_parsed_use_result);
TK_RESULT(tk_parsed_uses,      tk_parsed_uses_result);
TK_RESULT(tk_parsed_main_file, tk_parsed_main_file_result);

static bool is_decl_start(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_FN) || tk_is_kind_at(t, n, pos, TK_TOKEN_TYPE);
}

static tk_parsed_use_result parse_use(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos + 1);
    if (!pp.ok) return (tk_parsed_use_result){ .ok = false, .as.error = pp.as.error };
    bool has_alias = false; tk_str alias = (tk_str){0}; size_t p = pp.as.value.next;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_AS)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT))
            return (tk_parsed_use_result){ .ok = false, .as.error = tk_error_make("expected a name after `as` in a `use`") };
        has_alias = true; alias = t[p + 1].text; p += 2;
    }
    tk_use_decl u = { .path = pp.as.value.node, .has_alias = has_alias, .alias = alias };
    return (tk_parsed_use_result){ .ok = true, .as.value = { .node = u, .next = p } };
}

static tk_parsed_uses_result parse_use_header(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos);
    tk_use_decl *uses = NULL; size_t nu = 0;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_USE)) {
        tk_parsed_use_result u = parse_use(t, n, p);
        if (!u.ok) return (tk_parsed_uses_result){ .ok = false, .as.error = u.as.error };
        tk_uses_push(&uses, &nu, u.as.value.node); p = u.as.value.next;
        if (!tk_has_token(t, n, p)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_uses_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a `use`") };
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_uses_result){ .ok = true, .as.value = { .uses = uses, .n_uses = nu, .next = p } };
}

tk_parsed_main_file_result tk_parse_main_file(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) return (tk_parsed_main_file_result){ .ok = false, .as.error = hdr.as.error };
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_statement *body = NULL; size_t nb = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) break;
        if (is_decl_start(t, n, p))
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_error_make("main.tks is a virtual main: it may not declare types or functions (only `use` + statements)") };
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) return (tk_parsed_main_file_result){ .ok = false, .as.error = s.as.error };
        tk_stmts_push(&body, &nb, s.as.value.node); p = s.as.value.next;
        if (!tk_has_token(t, n, p)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a statement") };
        p = tk_skip_seps(t, n, p);
    }
    tk_main_file mf = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .body = body, .n_body = nb };
    return (tk_parsed_main_file_result){ .ok = true, .as.value = { .node = mf, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (R-main-b)

```teko
// appended to parser_test.tkt — R-main-b main-file tests.

fn main_file_of(source: str) -> MainFile | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pf = match parse_main_file(toks, 0) { ParsedMainFile as x => x; error as e => return e }
    pf.node
}

fn main_file_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_main_file(toks, 0) { ParsedMainFile => false; error => true }
}

#test
fn parses_main_file() {
    // a use header + a statement body.
    match main_file_of("use teko::lexer; x") {
        MainFile as mf => {
            assert mf.uses.len == 1
            assert mf.uses[0].path.segments[0].name == "teko"
            assert mf.body.len == 1
        }
        _ => assert false
    }
    // a use with an alias, then a body statement.
    match main_file_of("use a::b as c; x = 1") {
        MainFile as mf => { assert mf.uses[0].has_alias; assert mf.uses[0].alias == "c" }
        _ => assert false
    }
    // no use header, just a body of two statements.
    match main_file_of("let x = 1; x") {
        MainFile as mf => { assert mf.uses.len == 0; assert mf.body.len == 2 }
        _ => assert false
    }
    // an empty file → an empty virtual main.
    match main_file_of("") { MainFile as mf => { assert mf.uses.len == 0; assert mf.body.len == 0 } _ => assert false }
}

#test
fn rejects_decls_in_main() {
    assert main_file_errors("fn f() { }")            // a function declaration is forbidden
    assert main_file_errors("type T = struct { }")   // a type declaration is forbidden
    assert main_file_errors("x; use a::b")           // a `use` after a statement is misplaced
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz —
> `use teko::lexer; x` (1 use + 1 stmt), `use a::b as c; x = 1` (alias), `let x = 1; x`
> (sem header, 2 stmts), `""` (main vazia); barreira — `fn f() { }`, `type T = struct { }`,
> `x; use a::b`. (Asserts sobre `.n_uses`, `.n_body`, `.uses[0].has_alias`.)


---
## O parser — declarações top-level (P5)

Os **parsers de declaração** que um **módulo** contém (e que `parse_module`/R-main-c
consome): **P5a** `Function`, **P5b** `TypeDecl` struct, **P5c** enum/variant, **P5d**
`UseDecl` (já feito em R-main-b). Cada declaração devolve um `ParsedDecl` (`Decl = Function
| TypeDecl`). O antigo `parse_program` (P5e) foi substituído pelos dois pontos de entrada
de R-main.

### P5a — `parse_params` + `Function`

`[doc] [exp] fn nome(params) -> ret { corpo }` (B.21, B.29). Doc (`/** */`, token `Doc`) e
`exp` são opcionais e precedem `fn`. Os params são `nome: T` separados por **vírgula**
(B.17 — vírgula em `()`), imutáveis (B.21). O **`-> ret` é opcional** — ausente = retorno
void (como nas `#test fn …() {}`); por isso `Function` ganhou `has_return` (com `return_type`
placeholder via `no_type()`). O corpo é um bloco (P4a).

#### `src/parser/parse_decl.tks` — params + função (P5a)

```teko
// src/parser/parse_decl.tks   (namespace 'teko::parser')

// `( name: T, name: T, … )` — comma-separated params; empty `()` allowed; params are
// immutable (B.21). `pos` is at `(`.
fn parse_params(tokens: []lexer::Token, pos: u64) -> ParsedParams | error {
    mut p = pos + 1                                  // consume `(`
    mut params = teko::list::empty()
    if is_kind_at(tokens, p, lexer::TokenKind::RParen) {
        return ParsedParams { params = params, next = p + 1 }
    }
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Ident) {
            return error { message = "expected a parameter name" }
        }
        let name = tokens[p].text
        if !is_kind_at(tokens, p + 1, lexer::TokenKind::Colon) {
            return error { message = "expected ':' after a parameter name" }
        }
        let ty = match parse_type(tokens, p + 2) { ParsedType as x => x; error as e => return e }
        params = teko::list::push(params, Param { name = name, type_ann = ty.node })
        p = ty.next
        if !is_kind_at(tokens, p, lexer::TokenKind::Comma) { break }
        p = p + 1                                    // consume `,`
    }
    if !is_kind_at(tokens, p, lexer::TokenKind::RParen) {
        return error { message = "expected ')' to close the parameter list" }
    }
    ParsedParams { params = params, next = p + 1 }
}

// `[doc] [exp] fn name(params) -> ret { body }` (B.21, B.29). Doc + `exp` optional and
// precede `fn`; `-> ret` optional (absent = void). `pos` is at the doc, `exp`, or `fn`.
fn parse_function(tokens: []lexer::Token, pos: u64) -> ParsedDecl | error {
    mut p = pos
    mut has_doc = false
    mut doc = ""
    if is_kind_at(tokens, p, lexer::TokenKind::Doc) {
        has_doc = true
        doc = tokens[p].text
        p = p + 1
    }
    mut is_exp = false
    if is_kind_at(tokens, p, lexer::TokenKind::Exp) {
        is_exp = true
        p = p + 1
    }
    if !is_kind_at(tokens, p, lexer::TokenKind::Fn) {
        return error { message = "expected `fn`" }
    }
    p = p + 1
    if !is_kind_at(tokens, p, lexer::TokenKind::Ident) {
        return error { message = "expected a function name" }
    }
    let name = tokens[p].text
    p = p + 1
    if !is_kind_at(tokens, p, lexer::TokenKind::LParen) {
        return error { message = "expected '(' for the parameter list" }
    }
    let params = match parse_params(tokens, p) { ParsedParams as x => x; error as e => return e }
    p = params.next
    mut has_return = false
    mut return_type = no_type()
    if is_kind_at(tokens, p, lexer::TokenKind::Arrow) {
        let ret = match parse_type(tokens, p + 1) { ParsedType as x => x; error as e => return e }
        has_return = true
        return_type = ret.node
        p = ret.next
    }
    if !is_kind_at(tokens, p, lexer::TokenKind::LBrace) {
        return error { message = "expected '{' for the function body" }
    }
    let blk = match parse_block(tokens, p) { ParsedBlock as x => x; error as e => return e }
    ParsedDecl { node = Function { name = name, params = params.params, has_return = has_return, return_type = return_type, body = blk.statements, is_exp = is_exp, has_doc = has_doc, doc = doc }, next = blk.next }
}
```

#### C23 — `src/parser/parse_decl.c` (mirror, P5a)

```c
// parser/ast.h — params + function; parser/result.h — params + decl results.
typedef struct { tk_str name; tk_type_expr type_ann; } tk_param;   // immutable (B.21)
typedef struct {
    tk_str name; tk_param *params; size_t n_params;
    bool has_return; tk_type_expr return_type;
    tk_statement *body; size_t n_body;
    bool is_exp; bool has_doc; tk_str doc;
} tk_function;
typedef struct { tk_param *params; size_t n_params; size_t next; } tk_parsed_params;
TK_RESULT(tk_parsed_params, tk_parsed_params_result);
TK_RESULT(tk_parsed_decl,   tk_parsed_decl_result);   // tk_parsed_decl from R-main-a

static tk_parsed_params_result parse_params(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;
    tk_param *params = NULL; size_t np = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = 0, .next = p + 1 } };
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected a parameter name") };
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON))
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected ':' after a parameter name") };
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) return (tk_parsed_params_result){ .ok = false, .as.error = ty.as.error };
        tk_params_push(&params, &np, (tk_param){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) break;
        p += 1;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN))
        return (tk_parsed_params_result){ .ok = false, .as.error = tk_error_make("expected ')' to close the parameter list") };
    return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = np, .next = p + 1 } };
}

tk_parsed_decl_result tk_parse_function(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    bool is_exp = false;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { is_exp = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_FN))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected `fn`") };
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a function name") };
    tk_str name = t[p].text; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LPAREN))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '(' for the parameter list") };
    tk_parsed_params_result ps = parse_params(t, n, p);
    if (!ps.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = ps.as.error };
    p = ps.as.value.next;
    bool has_return = false; tk_type_expr ret = no_type();
    if (tk_is_kind_at(t, n, p, TK_TOKEN_ARROW)) {
        tk_parsed_type_result r = tk_parse_type(t, n, p + 1);
        if (!r.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = r.as.error };
        has_return = true; ret = r.as.value.node; p = r.as.value.next;
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LBRACE))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '{' for the function body") };
    tk_parsed_block_result blk = tk_parse_block(t, n, p);
    if (!blk.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = blk.as.error };
    tk_function f = { .name = name, .params = ps.as.value.params, .n_params = ps.as.value.n_params, .has_return = has_return, .return_type = ret, .body = blk.as.value.statements, .n_body = blk.as.value.n, .is_exp = is_exp, .has_doc = has_doc, .doc = doc };
    tk_decl d = { .tag = TK_DECL_FUNCTION, .as.function = f };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = blk.as.value.next } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P5a)

```teko
// appended to parser_test.tkt — P5a function tests.

// helper: parse a function decl from `source` — the Function, or error.
fn func_of(source: str) -> Function | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pd = match parse_function(toks, 0) { ParsedDecl as x => x; error as e => return e }
    match pd.node { Function as f => f; _ => error { message = "not a function" } }
}

fn func_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_function(toks, 0) { ParsedDecl => false; error => true }
}

#test
fn parses_functions() {
    // fn f() -> u8 { return 1 }
    match func_of("fn f() -> u8 { return 1 }") {
        Function as f => {
            assert f.name == "f"
            assert f.params.len == 0
            assert f.has_return
            assert !f.is_exp
            assert f.body.len == 1
        }
        _ => assert false
    }
    // exp fn g(x: u8, y: str) { }  — params, exported, void return (no `-> ret`).
    match func_of("exp fn g(x: u8, y: str) { }") {
        Function as f => {
            assert f.is_exp
            assert !f.has_return
            assert f.params.len == 2
            assert f.params[0].name == "x"
        }
        _ => assert false
    }
    // a doc comment precedes the function.
    match func_of("/** hi */ fn h() { }") { Function as f => assert f.has_doc; _ => assert false }
}

#test
fn rejects_malformed_functions() {
    assert func_errors("fn { }")           // no name
    assert func_errors("fn f { }")         // no params
    assert func_errors("fn f() -> { }")    // `->` with no return type
    assert func_errors("fn f() -> u8")     // no body
    assert func_errors("fn f(x) { }")      // param with no type
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz —
> `fn f() -> u8 { return 1 }` (com retorno), `exp fn g(x: u8, y: str) { }` (exp, 2 params,
> void), `/** hi */ fn h() { }` (doc); barreira — `fn { }`, `fn f { }`, `fn f() -> { }`,
> `fn f() -> u8`, `fn f(x) { }`. (Asserts sobre `.as.function.name`, `.n_params`,
> `.has_return`, `.is_exp`, `.has_doc`.)


### P5b — `TypeDecl` (struct)

`[doc] [exp] type Name = struct { campos }` (B.13, nominal). Doc + `exp` precedem `type`.
O corpo aqui é `struct { nome: T; … }` — campos separados por **`;`/newline** (B.17, dentro
de `{}`; difere dos params, que usam vírgula). `parse_type_body` despacha o corpo: P5b só
`struct`; `enum`/`variant` vêm no P5c (que também adiciona o keyword `variant`, ausente do
lexer). Resultado novo: `ParsedFields`.

#### `src/parser/parse_decl.tks` — campos + corpo + decl de tipo (P5b)

```teko
// `{ name: T; name: T; … }` — field list; `;`/newline separates; empty `{ }` allowed;
// trailing separator allowed. `pos` is at `{`. (Inside `{}` → `;`/newline, B.17.)
fn parse_fields(tokens: []lexer::Token, pos: u64) -> ParsedFields | error {
    mut p = skip_seps(tokens, pos + 1)   // consume `{`, skip leading separators
    mut fields = teko::list::empty()
    if is_kind_at(tokens, p, lexer::TokenKind::RBrace) {
        return ParsedFields { fields = fields, next = p + 1 }
    }
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Ident) {
            return error { message = "expected a field name" }
        }
        let name = tokens[p].text
        if !is_kind_at(tokens, p + 1, lexer::TokenKind::Colon) {
            return error { message = "expected ':' after a field name" }
        }
        let ty = match parse_type(tokens, p + 2) { ParsedType as x => x; error as e => return e }
        fields = teko::list::push(fields, Field { name = name, type_ann = ty.node })
        p = ty.next
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';', a newline, or '}' after a field" }
        }
        p = skip_seps(tokens, p)
        if is_kind_at(tokens, p, lexer::TokenKind::RBrace) { break }   // trailing separator
    }
    ParsedFields { fields = fields, next = p + 1 }
}

// the body of a type declaration. P5b: `struct { fields }`. (enum/variant → P5c.) `pos` is
// at `struct`/`enum`/`variant`.
fn parse_type_body(tokens: []lexer::Token, pos: u64) -> ParsedBody | error {
    if is_kind_at(tokens, pos, lexer::TokenKind::Struct) {
        if !is_kind_at(tokens, pos + 1, lexer::TokenKind::LBrace) {
            return error { message = "expected '{' after `struct`" }
        }
        let fs = match parse_fields(tokens, pos + 1) { ParsedFields as x => x; error as e => return e }
        return ParsedBody { node = StructBody { fields = fs.fields }, next = fs.next }
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Enum) {
        if !is_kind_at(tokens, pos + 1, lexer::TokenKind::LBrace) {
            return error { message = "expected '{' after `enum`" }
        }
        let ms = match parse_field_names(tokens, pos + 1) { ParsedNames as x => x; error as e => return e }
        return ParsedBody { node = EnumBody { members = ms.names }, next = ms.next }
    }
    if is_kind_at(tokens, pos, lexer::TokenKind::Variant) {
        let ty = match parse_type(tokens, pos + 1) { ParsedType as x => x; error as e => return e }
        return ParsedBody { node = VariantBody { type_expr = ty.node }, next = ty.next }
    }
    error { message = "expected `struct`, `enum`, or `variant`" }
}

// `[doc] [exp] type Name = <body>` (B.13, nominal). Doc + `exp` precede `type`. `pos` is at
// the doc, `exp`, or `type`.
fn parse_type_decl(tokens: []lexer::Token, pos: u64) -> ParsedDecl | error {
    mut p = pos
    mut has_doc = false
    mut doc = ""
    if is_kind_at(tokens, p, lexer::TokenKind::Doc) {
        has_doc = true
        doc = tokens[p].text
        p = p + 1
    }
    mut is_exp = false
    if is_kind_at(tokens, p, lexer::TokenKind::Exp) {
        is_exp = true
        p = p + 1
    }
    if !is_kind_at(tokens, p, lexer::TokenKind::Type) {
        return error { message = "expected `type`" }
    }
    p = p + 1
    if !is_kind_at(tokens, p, lexer::TokenKind::Ident) {
        return error { message = "expected a type name" }
    }
    let name = tokens[p].text
    p = p + 1
    if !is_kind_at(tokens, p, lexer::TokenKind::Assign) {
        return error { message = "expected '=' in a type declaration" }
    }
    let body = match parse_type_body(tokens, p + 1) { ParsedBody as x => x; error as e => return e }
    ParsedDecl { node = TypeDecl { name = name, body = body.node, is_exp = is_exp, has_doc = has_doc, doc = doc }, next = body.next }
}
```

#### C23 — `src/parser/parse_decl.c` (mirror, P5b)

```c
// parser/ast.h — field + struct body + type body + type decl.
typedef struct { tk_str name; tk_type_expr type_ann; } tk_field;
typedef struct { tk_field *fields; size_t n_fields; } tk_struct_body;
typedef struct { tk_str *members; size_t n_members; } tk_enum_body;     // P5c
typedef struct { tk_type_expr type_expr; } tk_variant_body;             // P5c
typedef struct {
    enum { TK_BODY_STRUCT, TK_BODY_ENUM, TK_BODY_VARIANT } tag;
    union { tk_struct_body struct_body; tk_enum_body enum_body; tk_variant_body variant_body; } as;
} tk_type_body;
typedef struct { tk_str name; tk_type_body body; bool is_exp; bool has_doc; tk_str doc; } tk_type_decl;
typedef struct { tk_field *fields; size_t n_fields; size_t next; } tk_parsed_fields;
typedef struct { tk_type_body node; size_t next; } tk_parsed_body;
TK_RESULT(tk_parsed_fields, tk_parsed_fields_result);
TK_RESULT(tk_parsed_body,   tk_parsed_body_result);

static tk_parsed_fields_result parse_fields(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);
    tk_field *fields = NULL; size_t nf = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE))
        return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = 0, .next = p + 1 } };
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected a field name") };
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON))
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected ':' after a field name") };
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) return (tk_parsed_fields_result){ .ok = false, .as.error = ty.as.error };
        tk_fields_push(&fields, &nf, (tk_field){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_error_make("expected ';', a newline, or '}' after a field") };
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) break;
    }
    return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = nf, .next = p + 1 } };
}

static tk_parsed_body_result parse_type_body(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_STRUCT)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE))
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected '{' after `struct`") };
        tk_parsed_fields_result fs = parse_fields(t, n, pos + 1);
        if (!fs.ok) return (tk_parsed_body_result){ .ok = false, .as.error = fs.as.error };
        tk_type_body b = { .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = fs.as.value.fields, .n_fields = fs.as.value.n_fields } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = fs.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_ENUM)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE))
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected '{' after `enum`") };
        tk_parsed_names_result ms = parse_field_names(t, n, pos + 1);
        if (!ms.ok) return (tk_parsed_body_result){ .ok = false, .as.error = ms.as.error };
        tk_type_body b = { .tag = TK_BODY_ENUM, .as.enum_body = { .members = ms.as.value.names, .n_members = ms.as.value.n_names } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ms.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_VARIANT)) {
        tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
        if (!ty.ok) return (tk_parsed_body_result){ .ok = false, .as.error = ty.as.error };
        tk_type_body b = { .tag = TK_BODY_VARIANT, .as.variant_body = { .type_expr = ty.as.value.node } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ty.as.value.next } };
    }
    return (tk_parsed_body_result){ .ok = false, .as.error = tk_error_make("expected `struct`, `enum`, or `variant`") };
}

tk_parsed_decl_result tk_parse_type_decl(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    bool is_exp = false;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { is_exp = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_TYPE))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected `type`") };
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a type name") };
    tk_str name = t[p].text; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ASSIGN))
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected '=' in a type declaration") };
    tk_parsed_body_result body = parse_type_body(t, n, p + 1);
    if (!body.ok) return (tk_parsed_decl_result){ .ok = false, .as.error = body.as.error };
    tk_type_decl td = { .name = name, .body = body.as.value.node, .is_exp = is_exp, .has_doc = has_doc, .doc = doc };
    tk_decl d = { .tag = TK_DECL_TYPE, .as.type_decl = td };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = body.as.value.next } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (P5b)

```teko
// appended to parser_test.tkt — P5b struct type-decl tests.

fn type_decl_of(source: str) -> TypeDecl | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pd = match parse_type_decl(toks, 0) { ParsedDecl as x => x; error as e => return e }
    match pd.node { TypeDecl as td => td; _ => error { message = "not a type decl" } }
}

fn type_decl_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_type_decl(toks, 0) { ParsedDecl => false; error => true }
}

#test
fn parses_struct_decl() {
    // type Point = struct { x: i32; y: i32 }
    match type_decl_of("type Point = struct { x: i32; y: i32 }") {
        TypeDecl as td => {
            assert td.name == "Point"
            assert !td.is_exp
            match td.body {
                StructBody as sb => { assert sb.fields.len == 2; assert sb.fields[0].name == "x" }
                _ => assert false
            }
        }
        _ => assert false
    }
    // /** d */ exp type Empty = struct { }  — exp, doc, empty struct.
    match type_decl_of("/** d */ exp type Empty = struct { }") {
        TypeDecl as td => {
            assert td.is_exp
            assert td.has_doc
            match td.body { StructBody as sb => assert sb.fields.len == 0; _ => assert false }
        }
        _ => assert false
    }
}

#test
fn rejects_malformed_struct() {
    assert type_decl_errors("type = struct { }")                  // no name
    assert type_decl_errors("type T struct { }")                  // no `=`
    assert type_decl_errors("type T = struct x: i32 }")           // no `{`
    assert type_decl_errors("type T = struct { x }")              // field with no type
    assert type_decl_errors("type T = struct { x: i32 y: i32 }")  // two fields, no separator
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz —
> `type Point = struct { x: i32; y: i32 }` (2 campos), `/** d */ exp type Empty = struct { }`
> (exp, doc, vazio); barreira — `type = struct { }`, `type T struct { }`,
> `type T = struct x: i32 }`, `type T = struct { x }`, `type T = struct { x: i32 y: i32 }`.
> (Asserts sobre `.as.type_decl.name`, `.body.tag`, `.as.struct_body.n_fields`.)


### P5c — `enum { membros }` + `variant <tipo>` (+ token `variant`)

`parse_type_body` (na P5b) ganhou os ramos `enum` e `variant` (acima). O **enum** é
`enum { A; B; C }` — nomes de membro separados por `;`/newline, **reusando
`parse_field_names`** (a mesma lista de nomes do field-pattern/destructure). O **variant**
é `variant <typeexpr>` — um tipo (união) após o keyword, via `parse_type` (P1): `variant
A | B` → `VariantBody { type_expr = UnionType{A,B} }`. **Adicionado o token `variant`** (enum
+ `read_symbol` não precisavam; `variant` faltava no lexer — lacuna como a do `;`), no fim
do enum (motivo do ordinal) + `keyword_kind` + teste de lexer. Com isto os três corpos de
tipo parseiam.

#### `src/parser/parser_test.tkt` — testes do parser (P5c)

```teko
// appended to parser_test.tkt — P5c enum/variant tests.

#test
fn parses_enum_and_variant() {
    // type Color = enum { Red; Green; Blue }
    match type_decl_of("type Color = enum { Red; Green; Blue }") {
        TypeDecl as td => match td.body {
            EnumBody as eb => { assert eb.members.len == 3; assert eb.members[0] == "Red" }
            _ => assert false
        }
        _ => assert false
    }
    // type Shape = variant Circle | Square → VariantBody with a 2-member union.
    match type_decl_of("type Shape = variant Circle | Square") {
        TypeDecl as td => match td.body {
            VariantBody as vb => match vb.type_expr {
                UnionType as u => assert u.members.len == 2
                _ => assert false
            }
            _ => assert false
        }
        _ => assert false
    }
    // a single-type variant: `variant A` (degenerate but parseable; the checker may require 2+).
    match type_decl_of("type S = variant A") {
        TypeDecl as td => match td.body { VariantBody => assert true; _ => assert false }
        _ => assert false
    }
}

#test
fn rejects_malformed_enum_variant() {
    assert type_decl_errors("type C = enum Red }")    // no `{` after `enum`
    assert type_decl_errors("type C = enum { 5 }")    // a number is not a member name
    assert type_decl_errors("type S = variant")       // variant with no type
    assert type_decl_errors("type T = thing { }")     // not struct/enum/variant
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz —
> `type Color = enum { Red; Green; Blue }` (3 membros), `type Shape = variant Circle | Square`
> (VariantBody, união de 2), `type S = variant A` (variant de 1); barreira —
> `type C = enum Red }`, `type C = enum { 5 }`, `type S = variant`, `type T = thing { }`.
> Mais o teste de lexer `variant` → `Variant`. (Asserts sobre `.body.tag`,
> `.as.enum_body.n_members`, `.as.variant_body.type_expr.tag`.)


### P5d — `UseDecl` (`use a::b [as c]`) — testes standalone

A função **`parse_use` já existe** (escrita no R-main-b, compartilhada com o cabeçalho de
`use`s e com `parse_module`/R-main-c). P5d fecha-a com seus **testes standalone** (o
R-main-b a exercitava só indiretamente, via `parse_main_file`). Com isto os parsers de
declaração — **Function** (P5a), **struct/enum/variant** (P5b/c) e **UseDecl** (P5d) —
estão completos e testados; o **R-main-c** (`parse_module`) já tem tudo que consome.

#### `src/parser/parser_test.tkt` — testes do parser (P5d)

```teko
// appended to parser_test.tkt — P5d standalone `use` tests.

fn use_of(source: str) -> UseDecl | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pu = match parse_use(toks, 0) { ParsedUse as x => x; error as e => return e }
    pu.node
}

fn use_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_use(toks, 0) { ParsedUse => false; error => true }
}

#test
fn parses_use_decls() {
    // use a::b::c  — a 3-segment path, no alias.
    match use_of("use a::b::c") {
        UseDecl as u => {
            assert !u.has_alias
            assert u.path.segments.len == 3
            assert u.path.segments[2].name == "c"
        }
        _ => assert false
    }
    // use a::b as c  — with an alias.
    match use_of("use a::b as c") {
        UseDecl as u => { assert u.has_alias; assert u.alias == "c"; assert u.path.segments.len == 2 }
        _ => assert false
    }
    // a single-segment use.
    match use_of("use teko") { UseDecl as u => assert u.path.segments.len == 1; _ => assert false }
}

#test
fn rejects_malformed_use() {
    assert use_errors("use")          // no path
    assert use_errors("use a as")     // `as` with no alias name
    assert use_errors("use ::")       // `::` with no leading name
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — `use a::b::c`
> (3 segmentos, sem alias), `use a::b as c` (alias), `use teko` (1 segmento); barreira —
> `use`, `use a as`, `use ::`. (Asserts sobre `.has_alias`, `.alias`, `.path.n`.)


### R-main-c — `parse_module` (cabeçalho `use` + laço de declarações)

Lê um **módulo**: o cabeçalho `use` (reusa `parse_use_header`/R-main-b) e depois um laço de
**declarações** (`Function`/`TypeDecl`, P5a–c), até o fim. **Rejeita statement solto** — o
`parse_decl` despacha pelo keyword (espreitando além de `doc`/`exp` para achar `fn`/`type`)
e erra em qualquer outra coisa. Resultado novo: `ParsedModule`. Com isto os **dois pontos de
entrada de arquivo** (`parse_main_file`/R-main-b, `parse_module`) existem — o antigo
`parse_program` está oficialmente substituído.

#### `src/parser/parse_file.tks` — dispatch de declaração + módulo (R-main-c)

```teko
// extends the Parsed* family.
type ParsedModule = struct { node: Module; next: u64 }   // a parsed module file

// dispatch a top-level declaration: a function or a type decl (each may be preceded by a
// doc comment and/or `exp`). Peeks past the doc/exp prefix to choose. `pos` is at the
// doc, `exp`, `fn`, or `type`.
fn parse_decl(tokens: []lexer::Token, pos: u64) -> ParsedDecl | error {
    mut k = pos
    if is_kind_at(tokens, k, lexer::TokenKind::Doc) { k = k + 1 }
    if is_kind_at(tokens, k, lexer::TokenKind::Exp) { k = k + 1 }
    if is_kind_at(tokens, k, lexer::TokenKind::Fn) {
        return parse_function(tokens, pos)
    }
    if is_kind_at(tokens, k, lexer::TokenKind::Type) {
        return parse_type_decl(tokens, pos)
    }
    error { message = "expected a declaration (`fn`/`type`, optionally `exp`/doc); loose statements belong in main.tks" }
}

// a module file: the `use` header, then a loop of declarations. Rejects loose statements.
// `pos` is 0 (file start).
fn parse_module(tokens: []lexer::Token, pos: u64) -> ParsedModule | error {
    let hdr = match parse_use_header(tokens, pos) { ParsedUses as x => x; error as e => return e }
    mut p = skip_seps(tokens, hdr.next)
    mut decls = teko::list::empty()
    loop {
        if !has_token(tokens, p) { break }
        let d = match parse_decl(tokens, p) { ParsedDecl as x => x; error as e => return e }
        decls = teko::list::push(decls, d.node)
        p = d.next
        if !has_token(tokens, p) { break }
        if !is_sep(tokens, p) {
            return error { message = "expected ';' or a newline after a declaration" }
        }
        p = skip_seps(tokens, p)
    }
    ParsedModule { node = Module { uses = hdr.uses, decls = decls }, next = p }
}
```

#### C23 — `src/parser/parse_file.c` (mirror, R-main-c)

```c
// parser/result.h — module result.
typedef struct { tk_module node; size_t next; } tk_parsed_module;
TK_RESULT(tk_parsed_module, tk_parsed_module_result);

static tk_parsed_decl_result parse_decl(const tk_token *t, size_t n, size_t pos) {
    size_t k = pos;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_DOC)) k += 1;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_EXP)) k += 1;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_FN))   return tk_parse_function(t, n, pos);
    if (tk_is_kind_at(t, n, k, TK_TOKEN_TYPE)) return tk_parse_type_decl(t, n, pos);
    return (tk_parsed_decl_result){ .ok = false, .as.error = tk_error_make("expected a declaration (`fn`/`type`, optionally `exp`/doc); loose statements belong in main.tks") };
}

tk_parsed_module_result tk_parse_module(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) return (tk_parsed_module_result){ .ok = false, .as.error = hdr.as.error };
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_decl *decls = NULL; size_t nd = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) break;
        tk_parsed_decl_result d = parse_decl(t, n, p);
        if (!d.ok) return (tk_parsed_module_result){ .ok = false, .as.error = d.as.error };
        tk_decls_push(&decls, &nd, d.as.value.node); p = d.as.value.next;
        if (!tk_has_token(t, n, p)) break;
        if (!tk_is_sep(t, n, p))
            return (tk_parsed_module_result){ .ok = false, .as.error = tk_error_make("expected ';' or a newline after a declaration") };
        p = tk_skip_seps(t, n, p);
    }
    tk_module m = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .decls = decls, .n_decls = nd };
    return (tk_parsed_module_result){ .ok = true, .as.value = { .node = m, .next = p } };
}
```

#### `src/parser/parser_test.tkt` — testes do parser (R-main-c)

```teko
// appended to parser_test.tkt — R-main-c module tests.

fn module_of(source: str) -> Module | error {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return error { message = "lex failed" } }
    let pm = match parse_module(toks, 0) { ParsedModule as x => x; error as e => return e }
    pm.node
}

fn module_errors(source: str) -> bool {
    let toks = match tokenize(source) { ts: []lexer::Token => ts; error => return true }
    match parse_module(toks, 0) { ParsedModule => false; error => true }
}

#test
fn parses_module() {
    // a use header + two declarations (a type and a function).
    match module_of("use teko::lexer; type T = struct { x: u8 }; fn f() { }") {
        Module as m => { assert m.uses.len == 1; assert m.decls.len == 2 }
        _ => assert false
    }
    // exp + doc on a declaration.
    match module_of("/** d */ exp fn g() -> u8 { return 1 }") {
        Module as m => {
            assert m.uses.len == 0
            assert m.decls.len == 1
            match m.decls[0] { Function as fn0 => { assert fn0.is_exp; assert fn0.has_doc } _ => assert false }
        }
        _ => assert false
    }
    // an empty module.
    match module_of("") { Module as m => { assert m.uses.len == 0; assert m.decls.len == 0 } _ => assert false }
}

#test
fn rejects_statements_in_module() {
    assert module_errors("let x = 1")   // a loose binding is a statement, not a declaration
    assert module_errors("x = 5")        // an assignment is a statement
    assert module_errors("f()")          // a bare expression is a statement
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz —
> `use teko::lexer; type T = struct { x: u8 }; fn f() { }` (1 use + 2 decls),
> `/** d */ exp fn g() -> u8 { return 1 }` (exp+doc), `""` (módulo vazio); barreira —
> `let x = 1`, `x = 5`, `f()` (statements num módulo). (Asserts sobre `.n_uses`,
> `.n_decls`, `.decls[0].as.function.is_exp`.)


---
## O parser — Parte 7: integração (P6)

O capstone do parser: **string → tokens → `MainFile`/`Module`** em código realista,
exercitando lexer + toda a escada (P1–P5 + R-main) junto. Sem código novo — só os testes
de integração (reusam `module_of`/`main_file_of` de R-main-b/c). Feliz: um módulo e um
main reais parseiam com a estrutura certa; barreira: malformados falham (localizado).

#### `src/parser/parser_test.tkt` — testes de integração (P6)

```teko
// appended to parser_test.tkt — P6 end-to-end integration.

#test
fn integration_module() {
    // a realistic module: use + a struct + an enum + a function with a conditional return.
    let src = "use teko::lexer; type Tok = struct { kind: u8; text: str }; type Kind = enum { A; B }; fn classify(t: Tok) -> Kind { if t.kind == 0 { return Kind::A }; Kind::B }"
    match module_of(src) {
        Module as m => {
            assert m.uses.len == 1
            assert m.decls.len == 3
            // the third declaration is the function `classify`.
            match m.decls[2] {
                Function as fn0 => {
                    assert fn0.name == "classify"
                    assert fn0.params.len == 1
                    assert fn0.params[0].name == "t"
                    assert fn0.has_return
                    assert fn0.body.len == 2          // the `if` statement, then `Kind::B`
                }
                _ => assert false
            }
        }
        error => assert false
    }
}

#test
fn integration_main() {
    // a realistic main.tks: use + a mutable binding, a loop with break + assignment, and a match.
    let src = "use teko::io; let mut n = 0; loop { if n == 3 { break }; n = n + 1 }; let r = match n { 3 => 1; _ => 0 }"
    match main_file_of(src) {
        MainFile as mf => {
            assert mf.uses.len == 1
            assert mf.body.len == 3              // the two `let`s and the `loop`
            // the second body statement is the loop.
            match mf.body[1] { LoopStmt as l => assert l.body.len == 2; _ => assert false }
            // the third binds `r` to a match expression.
            match mf.body[2] {
                Binding as b => match b.value { MatchExpr as me => assert me.arms.len == 2; _ => assert false }
                _ => assert false
            }
        }
        error => assert false
    }
}

#test
fn integration_barriers() {
    assert module_errors("fn f() { } let x = 1")    // a loose statement in a module
    assert main_file_errors("type T = struct { }")  // a declaration in a main file
    assert module_errors("fn f() { x + }")          // a malformed expression in a body
    assert main_file_errors("let x = ; x")          // a malformed binding in the main body
}
```

> **C mirror dos testes (harness `main`, alpha).** Mesmos casos: feliz — o módulo
> (use + struct + enum + função com `if`/return/FieldAccess/Compare/PathExpr; 3 decls) e o
> main (use + let/mut + loop+break+Assign + match-expr; 3 statements); barreira —
> statement num módulo, decl num main, expressão malformada num corpo, binding malformado.

---

> **P6 — integração FECHADA, e o parser está completo de ponta a ponta.** Lexer → parser
> compõem em código realista (módulo e main), com par feliz+barreira. Atingido o marco do
> roadmap: **"parser sem rachaduras"** — lexer→parser→`MainFile`/`Module` roda em fonte real
> e falha localizado no malformado. Pendente fora do parser: **R-main-d** (regra do `.tkp`,
> com o build) e a **Fase C** (refinamentos + migração do checker para o modelo de arquivo).

### R-main-d — a regra do `.tkp` (`main.tks` obrigatório p/ executável, proibido p/ biblioteca)

A **regra de projeto**: um `.tkp` executável **exige** `main.tks`; uma biblioteca o
**proíbe**. É checagem de **estrutura de projeto** — não do parser (por-arquivo) nem do
checker de tipos (que opera sobre um arquivo já parseado); pertence ao **build/driver**. A
**regra** entra aqui; a **fiação** (ler `[artifact] kind` do manifesto `.tkp` — `"binary"`
= executável — e enumerar os arquivos p/ saber se há `main.tks`) é do driver, **diferida**
(M.4 — pipeline não plugado). O artefato é modelado como `Artifact` (`Executable` ⇔ o
`kind = "binary"` do `.tkp`; `Library`).

#### `src/build/tkp_rule.tks` — a regra (R-main-d)

```teko
// src/build/tkp_rule.tks   (namespace 'teko::build')

// the `.tkp` artifact kind. (The manifest encodes `Executable` as `[artifact] kind = "binary"`.)
type Artifact = enum { Executable; Library }

// the `.tkp` main-file rule (R-main-d): an executable REQUIRES a main.tks; a library FORBIDS
// it. Returns the artifact (pass-through) on success, an error otherwise. The build driver
// supplies `artifact` (from the manifest) and `has_main` (from the file set) — that wiring is
// deferred (M.4).
fn check_main_file_rule(artifact: Artifact, has_main: bool) -> Artifact | error {
    if artifact == Artifact::Executable && !has_main {
        return error { message = "an executable project requires a main.tks" }
    }
    if artifact == Artifact::Library && has_main {
        return error { message = "a library project may not have a main.tks" }
    }
    artifact
}
```

#### C23 — `src/build/tkp_rule.c` (mirror, R-main-d)

```c
// src/build/tkp_rule.{h,c}
typedef enum { TK_ARTIFACT_EXECUTABLE, TK_ARTIFACT_LIBRARY } tk_artifact;
TK_RESULT(tk_artifact, tk_artifact_result);   // tk_artifact | error

tk_artifact_result tk_check_main_file_rule(tk_artifact artifact, bool has_main) {
    if (artifact == TK_ARTIFACT_EXECUTABLE && !has_main)
        return (tk_artifact_result){ .ok = false, .as.error = tk_error_make("an executable project requires a main.tks") };
    if (artifact == TK_ARTIFACT_LIBRARY && has_main)
        return (tk_artifact_result){ .ok = false, .as.error = tk_error_make("a library project may not have a main.tks") };
    return (tk_artifact_result){ .ok = true, .as.value = artifact };
}
```

#### `src/build/tkp_rule_test.tkt` — testes da regra (R-main-d)

```teko
// src/build/tkp_rule_test.tkt — tests for teko::build. #test collected.

// does the rule hold for (artifact, has_main)?
fn rule_ok(artifact: Artifact, has_main: bool) -> bool {
    match check_main_file_rule(artifact, has_main) { Artifact => true; error => false }
}

#test
fn enforces_main_file_rule() {
    // the four cases of the rule.
    assert rule_ok(Artifact::Executable, true)    // executable + main → ok
    assert !rule_ok(Artifact::Executable, false)  // executable + no main → error
    assert rule_ok(Artifact::Library, false)      // library + no main → ok
    assert !rule_ok(Artifact::Library, true)      // library + main → error
}
```

> **C mirror dos testes (harness `main`, alpha).** As quatro combinações:
> `(EXECUTABLE, true)` ok, `(EXECUTABLE, false)` erro, `(LIBRARY, false)` ok,
> `(LIBRARY, true)` erro — via `tk_check_main_file_rule(...).ok`.

> **R-main FECHADO.** `main.tks` é uma `main` virtual: R-main-a (modelo `MainFile`/`Module`/
> `Decl`) · R-main-b (`parse_main_file`) · R-main-c (`parse_module`) · R-main-d (a regra do
> `.tkp`). Diferido com o **build/driver** (M.4): a fiação que lê o manifesto + enumera os
> arquivos e chama `check_main_file_rule`. A migração do **checker** ao novo modelo de
> arquivo segue na **Fase C**.

---
## Decisões e diferimentos

Fechados (antes marcados abertos):

- **`to_string` em `error`** e **a construção de `str`** — resolvidos pela correção
  de injeção: `to_string` é provido pelo `error` injetado, e `str` nasce **só** pela
  porta injetada `str_from_utf8` (o `str(b)` é o re-tag privilegiado interno à porta,
  não sintaxe de construção do usuário).
- **`main.tks` — uma `main` virtual (corrige a nota anterior, errada, de `fn main()`).**
  Não há `fn main` escrito. O conteúdo do `main.tks`, **após o cabeçalho de `use`s**, **É**
  o corpo de uma `main` implícita; o arquivo **não pode** declarar tipos nem funções — só
  `use` (a única declaração permitida) + statements. **Obrigatório** quando o `.tkp` é
  `executable`, **proibido** quando é `library`. A modelagem (`MainFile` = use + corpo, vs
  módulo = use + decls) é a refatoração **R-main** no roadmap. **Lei: M.2** (a forma diz o
  que é — um arquivo-corpo, não um arquivo-com-`main`) **+ M.5** (sem cerimônia de
  `fn main(){}`). O *pipeline* (read→lex→parse→check→codegen) segue não plugado — **M.4**.
- **Harness `.tkt`** — decidido: sem `main`; testes são funções `#test` que o
  compilador coleta (runner é do **alpha**); o espelho C usa `main`. `#start`/`#end`
  (setup/teardown) ficam registrados para o alpha.

Diferido para **evolução**:

- **Escape `@nome`** (usar um nome reservado como identificador) — sintaxe exata fica
  para lá; o seed não precisa.
- A semântica de `assert`/falha do `.tkt` fecha com o runner, no **alpha**.