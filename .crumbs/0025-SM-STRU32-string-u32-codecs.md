---
seq: 0025
crumb-id: SM-STRU32
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [RM-C2]
sources:
  - "docs/design/mudancas-superficie-0.3.1.md:1723-1752"                 # Doc-2 string-u32 rulings (A)-(D)
  - "docs/design/mudancas-superficie-0.3.1.md:1753-1757"                 # ratified wave order (string-u32 in §16)
  - "docs/design/native-slice-str-rep-separation-0.3.1.md:19-71"         # rep decision (str fat, cap-in-storage)
---

# 0025 · SM-STRU32 — string as `[]u32` representation + codecs (utf-8↔u32 encode/decode) surface

> String as `[]u32` representation + codecs (utf-8↔u32 encode/decode) surface — the additive text surface the
> string-subsystem migration is later built over.

## Goal

Ship the ADDITIVE surface for the new string model (Doc-2 ruling `mudancas-superficie-0.3.1.md:1723-1752`):
a string is internally a **fixed-width `[]u32`** array of codepoints (O(1) indexing, no per-char header),
with the fat carrying `{qtd-chars, qtd-bytes, array-u32, encoding}` and `char` becoming a **pure `u32`
codepoint**; and the **text codecs** that trim to/from UTF-8 (and the other encodings) at the metal
boundary — `utf8→u32` decode and `u32→utf8` encode over `teko::text`. This crumb delivers the CODEC SURFACE
and the `[]u32`/`char=u32` acceptance ONLY — additive and inert; it does NOT flip the compiler's own `str`
storage (the full subsystem migration is the ratified §16/Doc-1-etapa-1 wave, migrated ONCE there to avoid
double work, `:1731-1733`). `char` already casts to `u32` as its codepoint (`typer.tks:2406-2410`) and
`teko::text` already validates UTF-8 (`text.tks:9,88`); this extends that to the u32 codec pair. No `src/`
site adopts the new representation yet, so a `[dry]` build is byte-identical. Its seed folds into SM-R1.

## Where

- `src/text/text.tks:9,88` — `valid_utf8` / `str_from_utf8` (the existing UTF-8 door) — ADD the u32 codec
  pair `decode_utf8_u32` (bytes → `[]u32` codepoints) and `encode_u32_utf8` (`[]u32` → `[]byte`), the
  metal-boundary trim.
- `src/text/text.tks` — ADD `char_to_u32`/`u32_to_char` surface (the `char ↔ u32` codepoint bridge,
  formalizing the cast at `typer.tks:2406`) and `str_to_u32`/`u32_to_str` (a `str` ↔ its `[]u32` codepoint
  view — the additive accessor the future storage flip reuses).
- `src/checker/typer.tks:2406-2410` — the `Char` cast arm (`char → u32/u64/i64` codepoint) — UNCHANGED
  (already correct); the codecs build on it.
- The `str` fat rep (`native-slice-str-rep-separation-0.3.1.md:19-71`) — `str` STAYS a 2-word `{ptr,len}`
  fat in this crumb; the `{qtd-chars, qtd-bytes, array-u32, encoding}` widening is the deferred §16/Doc-1
  migration, NOT here.

NEW: no new module; the codec pair + the `char↔u32` / `str↔[]u32` bridges land in the existing
`src/text/text.tks` over the RM-C2 count→`[total]byte=[]`→copy idiom.

## How

1. **Add the u32 codec pair** (over the RM-C2 no-push idiom — count the codepoints/bytes, `of_len`, fill by
   index; no growth):

```teko
/**
 * decode_utf8_u32 — decode well-formed UTF-8 bytes into a FIXED `[]u32` of Unicode codepoints (the new
 * string's internal array form). Two passes over the RM-C2 idiom: count the codepoints, `of_len<u32>(n)`,
 * then fill by index — no growth, no per-char header. Rejects the same ill-formed input `valid_utf8` does
 * (overlong forms, UTF-16 surrogates, codepoints past U+10FFFF), returning `error`.
 *
 * @param b  the UTF-8 bytes to decode
 * @return   the codepoints as a fixed `[]u32`, or `error` on invalid UTF-8
 * @throws   when `b` is not well-formed UTF-8 (RFC 3629)
 * @since 0.3.1
 */
exp fn decode_utf8_u32(b: []byte): []u32 | error

/**
 * encode_u32_utf8 — encode a `[]u32` of Unicode codepoints to UTF-8 bytes (the metal-boundary trim). Two
 * passes: sum the UTF-8 byte length of each codepoint, `of_len<byte>(total)`, then write each codepoint's
 * 1–4 bytes by index — no growth. Rejects a codepoint that is a UTF-16 surrogate or past U+10FFFF with
 * `error` (it is not an encodable scalar value).
 *
 * @param cps  the codepoints to encode
 * @return     the UTF-8 bytes, or `error` on a non-scalar codepoint
 * @throws     when a codepoint is a surrogate or exceeds U+10FFFF
 * @since 0.3.1
 */
exp fn encode_u32_utf8(cps: []u32): []byte | error
```

2. **Add the `char ↔ u32` / `str ↔ []u32` bridges.** `char_to_u32`/`u32_to_char` formalize the codepoint
   cast (`typer.tks:2406`); `str_to_u32(s)` returns the codepoint view of a `str` (via `decode_utf8_u32`
   over its bytes) and `u32_to_str(cps)` builds a `str` from codepoints (via `encode_u32_utf8` +
   `str_from_utf8`). These are the additive accessors the deferred storage flip reuses unchanged.
3. **`str` stays 2-word here (rep decision).** Do NOT widen `str`'s fat — `str` remains `{ptr,len}` (16
   bytes; `native-slice-str-rep-separation-0.3.1.md:21`). The `{qtd-chars, qtd-bytes, array-u32, encoding}`
   fat and the storage flip are the ratified §16/Doc-1-etapa-1 wave (migrate ONCE there); this crumb only
   ships the codec + view SURFACE so that wave has its building block seeded.
4. **Codecs are text, not data-format.** These are CHARACTER codecs (UTF-8↔u32 over the codepoint array),
   DISTINCT from the already-delivered data-serialization encoders (base64/json/cbor — `:1749`). Placement:
   `teko::text`.
5. **Stay inert.** No `src/` site adopts `[]u32` string storage yet; the codecs are new `exp` surface with
   no compiler-core caller → `[dry]` build byte-identical.

## Rulings & laws

- **Teko-only:** `src/text/text.tks` `.tks` over the RM-C2 idiom; no `teko_rt.c` (the codec is pure Teko).
- **W15 full Javadoc** on every new `exp` codec/bridge; flatten/extract; no inline `//`.
- **Doc-2 string-u32 (owner 2026-08-16, `:1723-1752`):** string = fixed `[]u32` + `{qtd-chars, qtd-bytes,
  array-u32, encoding}` fat; `char` = pure `u32` codepoint; multi-encoding text codecs on the stdlib. This
  crumb ships the codec SURFACE; the storage flip is the deferred §16/Doc-1 wave (migrate once).
- **`str` stays 2-word (rep decision `native-slice-str-rep…:21`):** no str-widening in this crumb.
- **NO PUSHES (CLAUDE.md):** the codecs are two-pass count→`of_len`→fill-by-index (RM-C2), no growth.
- **Additive/inert:** removes nothing; no corpus adopter → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

`src/text` already round-trips UTF-8 bytes in the self-build, but the u32 codepoint codecs + the reject
branches are NEW and un-exercised by the fixpoint — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `utf8_u32_roundtrip` | `encode_u32_utf8(decode_utf8_u32(b)) == b` for ASCII + multi-byte (BMP + astral) text | 0 |
| `u32_codepoint_index` | `str_to_u32(s)` gives O(1)-indexable codepoints; `s[i]` codepoint matches the u32 view | 0 |
| `decode_invalid_utf8_reject` | an overlong form / a surrogate / a byte past U+10FFFF returns `error` (matched by the caller) | EXPECT_COMPILE_FAIL |
| `encode_surrogate_reject` | encoding a surrogate or a codepoint > U+10FFFF returns `error` | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the fixtures + fixpoint (byte-identical; codecs are new surface with no core caller).
"Green" = the u32 codec pair round-trips valid text and rejects ill-formed input, the `char↔u32` /
`str↔[]u32` bridges resolve, `str` stays 2-word, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`RM-C2` (the count→`[total]byte=[]`→copy no-push idiom the two-pass codecs materialize over).

## Done when

`decode_utf8_u32`/`encode_u32_utf8` + the `char↔u32` / `str↔[]u32` bridges exist in `teko::text` (pure Teko,
two-pass no-push), round-trip valid text and reject ill-formed input, `str` stays 2-word (storage flip
deferred to the §16/Doc-1 wave), the fixtures pass, and a `[dry]` build is byte-identical.
