---
seq: 0163
crumb-id: EMB-C1
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: [EMB-C0]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:1"
  - "docs/design/embed-vfs.md:301-357"                          # parser design
  - "src/parser/parse_decl.tks:779"                             # #os payload grammar (pattern to mirror)
  - "src/parser/parse_decl.tks:816"                             # #inject payload
---

# 0163 · EMB-C1 — parser: `EmbedDecl` node + `parse_embed`, wired into `parse_decl`/`parse_module`

> `#embed` is a TOP-LEVEL directive decoupled from declarations (owner ruling 1), unlike `#os`/`#inject`
> which annotate a following decl. Add a `Decl` variant `EmbedDecl` recognized before the fn/type/flags/
> const dispatch. Feature-gated-inert (the decl is parsed but no pass consumes it until EMB-C2+) → no
> emitted-byte change.

## Goal

Parse `#embed("pattern")` (form A) and `#embed("pattern", TYPE, LEVEL)` (form B, TYPE ∈
None/Deflate/Gzip, LEVEL an Int) into an `EmbedDecl` carrying the pattern, the codec, the level, and the
directive's source line/col (for located errors) AND the **source-file path** (needed for file-relative
anchoring — annex §2). Range/path validity are NOT checked here (resolve-time). The decl flows through
`Module.decls` like any other; no downstream pass consumes it yet (inert).

## Where

- `src/parser/` (the `Decl` variant declaration) — add `EmbedDecl` (annex-derived; carries `pattern`,
  `has_compression`, `comp: EmbedCompress`, `level: i64`, `src_path: str`, `line: u32`, `col: u32`).
- `src/parser/parse_decl.tks:~1096` (before the fn/type/flags/const dispatch) — recognize `Hash` +
  `Ident("embed")` → delegate to `parse_embed`.
- `src/parser/parse_decl.tks:779` (`#os`) — mirror the payload-string grammar.
- `parse_module` loop (`parse_decl.tks:~1183`) — push the `EmbedDecl` like any decl.

## How

```teko
/**
 * parse_embed — parse a top-level `#embed(...)` at `pos` (the `#`), mirroring the `#os("…")` payload
 * shape. Accepts form A `("pattern")` and form B `("pattern", TYPE, LEVEL)` (TYPE a bare None/Deflate/
 * Gzip atom, LEVEL an Int). Purely syntactic — path/range validity are resolve-time concerns. Stamps the
 * containing source-file path for later file-relative anchoring.
 *
 * @param tokens    the token stream
 * @param pos       the index of the `#` beginning the directive
 * @param src_path  the path of the .tks file being parsed (for file-relative anchoring)
 * @return          the parsed EmbedDecl and the index past the closing `)`
 * @throws          a located error on a missing `(`, a non-string pattern, a malformed `, TYPE, LEVEL`
 *                  tail, or an unknown compression type
 * @since 0.3.1
 */
fn parse_embed(tokens: []lexer::Token, pos: u64, src_path: str): Parsed<Decl> | error
```

1. Add the `EmbedDecl` variant to the `Decl` union + every exhaustive `match` over `Decl` gets an inert
   arm (collect/checker/codegen just skip it until EMB-C2).
2. In `parse_decl`, on `Hash`+`Ident("embed")`, call `parse_embed` with the current source path instead
   of entering the attribute loop.
3. `parse_embed` reads the `(`, the string pattern; if a `,` follows, reads TYPE (one of the three atoms
   → `EmbedCompress`) and `,` LEVEL (Int); the `)`.

## Rulings & laws

- **Teko-only.**
- **Owner ruling 1 (decoupled top-level directive):** NOT an attribute in `parse_decl_attributes`.
- **NO tombstone / no detecting-the-nonexistent:** parse errors name the real cause (`expected ')'`, etc.).
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; inert decl → full gate byte-identical.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `embed_parse_bad_type` | `#embed("f", Bogus, 1)` | `EXPECT_COMPILE_FAIL` |
| `embed_parse_missing_level` | `#embed("f", Deflate)` (no level) | `EXPECT_COMPILE_FAIL` |

(Rejection paths the self-build never drives — the carve-out. The positive parse is exercised once the
prelude uses `#embed` in EMB-C6/PRE-C1.)

## Gate

`[dry]` — compiles; a parsed-but-unconsumed `EmbedDecl` leaves emitted bytes unchanged (`gen2==gen3`
no-op). "Green" = both forms parse, malformed tails reject with located errors, self-build byte-identical.
Reseed-class: `none`.

## Deps

`EMB-C0`

## Done when

Both `#embed` forms parse to an `EmbedDecl` (carrying src-path + line/col), malformed tails reject, and
the self-build is byte-identical (decl inert).
