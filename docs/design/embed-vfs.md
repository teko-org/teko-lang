# `#embed` + read-only embedded VFS — design-ahead

**STATUS: design-ahead, NOT scheduled — "supérfluo por hora" (owner 2026-07-20); unblocks on demand.**

Architect: design-ahead, 2026-07-20. No product code touched by this document. Orthogonal
to the in-flight F1 / AL / comptime waves — this design references their *declared* shapes
only and gates on none of them beyond the one honest dependency called out in §7.

**Convention (counter-argue protocol):**
- **(owner 2026-07-20)** — a ratified ruling from the owner, captured verbatim in intent.
- **(integrator-pinned, veto-open)** — an architect inference filling a gap the ruling left
  open. It is the recommended resolution; the owner may veto it when this unblocks. Every
  open question in §8 is one of these, surfaced explicitly rather than silently baked in.

---

## 0. What this is, in one paragraph

`#embed("pattern")` is a **top-level directive** (decoupled from any declaration) that bakes
the matched in-project files into the binary's read-only data at compile time and registers
them in a single global **read-only VFS**. At runtime the program reads its own embedded
files with zero filesystem dependency. The first consumer is the brand mascot
(`docs/brand/mascot.ansi`, 12 029 bytes, already in-tree): print it at the start of a command
when the output is a real TTY (reusing `teko::build::output_is_tty`). The VFS is the
foundation; the mascot is use #1.

---

## 1. Constitutional grounding

The feature earns its place by three of the six Laws; the same Laws are why it is *deferred*,
not *rejected*.

### 1.1 M.0 — the metal: a self-contained binary

`TEKO_CONSTITUTION.md` M.0 (line 179+) makes the compiled artifact the ruler. `#embed`
serves M.0 directly: the file's bytes live **in rodata**, so the shipped binary carries its
assets and has **no runtime dependency on an external file**. The compile-time read
(`teko::io::read_file*`, run by a compiler phase — §4) leaves **no** read syscall in the
emitted program; the datum is materialized once, at build, into the binary image. This is the
same discipline the compiler already uses to inline its own string literals into rodata
(`lower.tks:3896` `str_to_bytes` → `LRodata`).

### 1.2 M.1 — safety by exclusion-by-construction, and fail-loud

M.1 (line 247+) runs a spectrum from *detect-and-fail* to **make-inexpressible** (its maximal
grade, lines 257–266). This design sits at the strongest grade **by construction** wherever
it can, and at fail-loud where it cannot:

- **Path resolution is downward-only, in-project, exclusion-by-construction (owner 2026-07-20,
  ruling 2).** `..` (ascend), an absolute path (`/…`, `X:\…`), and any sibling/ancestor escape
  are **rejected at parse/resolve time** — the external path is *inexpressible*, not merely
  discouraged. The build is reproducible because a path outside the project **cannot be
  written**, exactly the "make the invalid inexpressible" technique the Constitution names
  (lines 264–266). No flag, no warning, no opt-out — the strongest M.1 grade.
- **Path conflict → PANIC (owner 2026-07-20, ruling 4).** Two directives resolving the same
  logical path is a **compile error**, not a last-writer-wins merge. This is M.1's
  "fail early and explicit; never corrupt in silence" (line 247): a silent overwrite would be
  a silent loss.
- **Compression level out of range → error (owner 2026-07-20, ruling 5).** Validated in
  comptime against the codec's declared range (const-range check); out of range is a located
  compile error, never a clamp (a clamp is a silent loss — M.1).

### 1.3 M.5 — austerity: convenience justified, and deferred

M.5 (line 370+) is the most *cedible* Law: it governs what *not* to add above the metal. An
embedded VFS is a **convenience** (triage: not a metal operator → M.5 jurisdiction, lines
399–405). It is justified — a self-contained binary is a real ergonomic and M.0 win — but a
convenience yields to the metal and to the in-flight essential waves. Hence the owner's
"supérfluo por hora": M.5 is why this ships **after** the F1/AL/comptime spine, on demand,
not now. The design is captured today so the *deliberation* M.5 forces is already done when
the slot opens.

---

## 1b. Prior art (owner-supplied references, 2026-07-20)

The design is **not novel** — it is the established `embed`-a-file pattern, hardened by Teko's
Laws. Positioning it against the three reference implementations sharpens what is borrowed and
what is deliberately different.

| System | Mechanism | Granularity | Compression | FS abstraction | Path safety |
|---|---|---|---|---|---|
| **C (`#embed`, C23)** | preprocessor directive expanding to a comma-list of byte literals (an array initializer) | single file, injected as a literal in comptime | none | none (raw bytes) | tooling / include-path |
| **C#** | embedded resources — a **named** table in the assembly (name→bytes in metadata), read via `Assembly.GetManifestResourceStream(name)` | per-resource, name lookup at runtime | none (opt-in via tooling) | flat named table | build config |
| **Go (`//go:embed`)** | directive populating a named **variable** — a single `[]byte`/`string`, OR an `embed.FS`: a read-only FS **type** with `Open`/`ReadFile`/`ReadDir` + glob patterns | single file **or** an FS tree | none | `embed.FS` type | rule of the tooling (rejects absolute / `..` / outside-module) |
| **Teko `#embed`** | top-level directive registering matched in-project files into ONE global read-only VFS; a dedicated `FileSystem` struct const | whole VFS, `read`/`exists`/`list` | **built-in, transparent** (`TYPE`/`LEVEL`) | dedicated `FileSystem` struct | **exclusion-by-construction (M.1)** |

**Teko ≈ Go `embed.FS`** — the dedicated read-only file-system *type* is exactly the shape we
converged on (owner ruling 3), against the flatter C literal-array and C# named-table models.
It is the closest prior art. On top of Go, Teko adds **two deliberate refinements**, each Law-
grounded, plus **one deliberate structural difference**:

**Refinement 1 — built-in transparent compression.** Go does **not** compress; a `//go:embed`
asset sits in the binary at full size. Teko's optional `TYPE`/`LEVEL` (§2.2, owner ruling 5)
compresses at build and decompresses in the accessor, invisibly — a real ergonomic + binary-
size win reusing `src/compress/` (§3.4), which Go leaves to the caller.

**Refinement 2 — path safety as make-inexpressible, not a tooling rule.** Go *also* forbids
absolute paths, `..`, and outside-module references — but by a **rule of the go tool** (a
detect-and-reject check). Teko forbids them by **construction**: the escaping path is
*inexpressible* (§1.2, §2.5), the **strongest grade of M.1** (Constitution lines 257–266). Same
prohibition, harder guarantee — the reproducibility of the build follows from the grammar/
resolver, not from a linter that could be bypassed or drift.

**Deliberate structural difference — one global VFS vs. Go's per-variable binding
(owner 2026-07-20, ruling 6).** Go binds each `//go:embed` directive to a **named variable**
(`//go:embed x.txt` populates *this* var); distinct embeds are distinct values. Teko instead
accumulates **every** `#embed`, in any file, into **one global VFS** (owner ruling 3), and a
path collision between two directives is a **PANIC** (owner ruling 4, §2.5).

- *Trade-off (global-single):* one obvious lookup surface (`FILES.read(path)`), no per-embed
  naming ceremony, and a whole-program uniqueness invariant enforced fail-loud (M.1) — at the
  cost of a program-wide namespace where two libraries could, in principle, claim the same path
  (caught at compile time by the panic, never silently).
- *Trade-off (Go per-variable):* natural per-module isolation and no cross-file collision — at
  the cost of scattering assets across many typed variables with no single catalogue.

The owner chose global-single: it matches "one self-contained binary, one asset catalogue"
(M.0) and turns the only downside (collision) into a loud compile error (M.1) rather than a
silent shadow. Recorded as owner ruling 6 (2026-07-20).

**Net:** the design is the Go `embed.FS` pattern, **hardened by Teko's Laws** — compression
where Go has none, inexpressibility where Go has a tooling rule, and a single fail-loud global
catalogue where Go has per-variable bindings.

---

## 2. Surface (Teko)

### 2.1 The directive — two forms

A top-level directive, on its own line, **decoupled from declarations** (owner 2026-07-20,
ruling 1) — unlike `#test`/`#inject`/`#os`, which annotate a following `fn`/`type`. It has an
**effect**: it registers the matched files into the one global VFS. It follows the payload
grammar of `#os("…")` (`parse_decl.tks:779`) and `#inject(…)` (`parse_decl.tks:816`).

```teko
#embed("docs/brand/mascot.ansi")
#embed("assets/table.json", Deflate, 6)
```

Grammar (tokens): `Hash Ident("embed") LParen Str [ Comma Ident Comma Int ] RParen`.

- Form A — `#embed("pattern")`: embed matched files **uncompressed** (`EmbedCompress::None`).
- Form B — `#embed("pattern", TYPE, LEVEL)`: `TYPE` is a closed enum atom
  (`None | Deflate | Gzip`); `LEVEL` is an integer literal validated in comptime against the
  codec's range.

Every `#embed`, in any file, accumulates into the **same** VFS (owner 2026-07-20, ruling 3).

### 2.2 The compression enum (closed)

**(owner 2026-07-20, ruling 5.)** A closed enum, reusing `src/compress/`. `zstd`/`lz4` are a
later extension (a new variant + a codec — additive, no surface change to existing directives).

```teko
/**
 * EmbedCompress — the closed set of compression codecs an `#embed` directive may name
 * (owner ruling 2026-07-20). `None` stores the file's bytes verbatim; `Deflate` runs
 * `teko::compress::deflate` (raw DEFLATE); `Gzip` runs `teko::compress::gzip_compress`
 * (gzip container). The codec is applied AT BUILD TIME and the bytes are stored compressed
 * in rodata; the VFS accessor returns them DECOMPRESSED, so compression is invisible to the
 * reader. `zstd`/`lz4` are a deliberately deferred future variant (additive).
 *
 * @since #embed (design-ahead)
 */
pub type EmbedCompress = enum { None; Deflate; Gzip }
```

### 2.3 The VFS type + accessor API

**(owner 2026-07-20, ruling 3.)** The VFS is **not** a `map<K,V>` — it is a **dedicated
`FileSystem` struct**, `const`, generated in comptime, that lives at the root and is **empty
when no `#embed` exists anywhere**. This decouples it from the generic stack (§7): no
`Map<K: Hashable & Eq, V>` is needed.

```teko
/**
 * FileSystem — the process-global, read-only, compile-time-materialized virtual file system
 * (owner ruling 2026-07-20). Every `#embed` directive across the whole program accumulates
 * into the single `const` instance (§8 Q2 names it). It is EMPTY when no `#embed` appears —
 * a zero-entry directory over empty byte heaps, costing nothing.
 *
 * Representation (integrator-pinned, veto-open — §7.2): FLATTENED so it materializes as
 * Tier-A rodata (self-contained `[]byte`/scalar, no internal pointer) and therefore does NOT
 * gate on #594 Tier-B. The three heaps + count are separate module-level Tier-A consts the
 * comptime pass emits; this struct is a thin cursor over them:
 *   - `names`  — every embedded path, UTF-8, concatenated into one `[]byte`.
 *   - `blobs`  — every file's (possibly compressed) content, concatenated into one `[]byte`.
 *   - `table`  — a fixed-width record per entry, packed into one `[]byte`: (name_off, name_len,
 *                blob_off, blob_len, orig_len, comp_tag) — all fixed POD, so the table itself
 *                is Tier-A.
 *   - `count`  — the number of entries (a scalar const).
 *
 * @since #embed (design-ahead)
 */
pub type FileSystem = struct {
    names: []byte
    blobs: []byte
    table: []byte
    count: u64

    /**
     * read — the DECOMPRESSED bytes of the embedded file at `path`, or `null` when no such
     * path is embedded. The stored bytes are inflated per the entry's `comp_tag`
     * (`teko::compress::inflate` for Deflate, `gzip_decompress` for Gzip, verbatim for None),
     * so the caller never sees compression.
     *
     * DECOMPRESSION TIMING (owner 2026-07-20, ruling 7): inflation is LAZY — it happens on
     * THIS `read` call, for THIS file only. Nothing decompresses at program load; an embedded
     * file that is never read is never inflated (it stays compressed in rodata, paying zero
     * runtime cost). A `None` entry returns its rodata bytes directly (no inflate; may be a
     * slice into rodata with no allocation).
     * NO HIDDEN CACHE (integrator-pinned, veto-open): each `read` re-inflates — the `FileSystem`
     * is `const` rodata and MUST stay pure (M.3: it is data, not an object with hidden mutable
     * state); a caller that reads the same file repeatedly holds the result itself. A memoizing
     * cache would need mutable state outside the const and is deliberately NOT part of the type.
     *
     * @param path  the project-relative logical path, exactly as it resolved at build time
     * @return      the file's decompressed bytes, or null when absent
     * @throws      never at the type level — a corrupt embedded stream is a build-time
     *              impossibility (the compiler produced it), so decode cannot fail here;
     *              see §8 Q1 on whether the return widens to `[]byte | error` defensively
     */
    fn read(self, path: str) -> []byte? { /* design-ahead: reads self.table/blobs, inflates */ }

    /**
     * exists — whether a file at `path` was embedded. O(count) over the packed table.
     *
     * @param path  the project-relative logical path
     * @return      true iff an entry with that name is present
     */
    fn exists(self, path: str) -> bool { /* design-ahead */ }

    /**
     * list — every embedded path, in directive/emit order.
     *
     * @return  the logical paths of all embedded files (empty when nothing is embedded)
     */
    fn list(self) -> []str { /* design-ahead */ }
}
```

Convenience accessor (integrator-pinned, veto-open — §8 Q1): `read_str(self, path: str) -> str?`
returning the decompressed bytes decoded as UTF-8, for text assets like the mascot, so the
common case skips a manual bytes→str decode.

### 2.4 The mascot — use #1 (reusing `is_tty`)

The mascot is cosmetic: print it only on a real terminal, silently skip it otherwise. It
reuses `teko::build::output_is_tty(no_tty)` (`progress.tks:33`) and the `--no-tty` override
(`no_tty_of`, `project.tks:1859`) already wired through the build/CLI.

```teko
/**
 * print_mascot — write the brand mascot to stdout at a command's start, but ONLY when the
 * output is an interactive terminal (reusing `teko::build::output_is_tty`, the same TTY
 * detection the build's `\r`-progress uses; `--no-tty` forces it off). The mascot is purely
 * cosmetic, so an absent embed or a non-TTY destination is a SILENT no-op — never an error.
 *
 * @param no_tty  the `--no-tty` CLI override (forces the plain, non-interactive path)
 * @return        nothing; best-effort cosmetic output
 * @since #embed (design-ahead)
 */
fn print_mascot(no_tty: bool) -> void {
    if !teko::build::output_is_tty(no_tty) { return }
    match teko::embed::FILES.read("docs/brand/mascot.ansi") {
        []byte as bytes => teko::io::write(teko::text::from_utf8_lossy(bytes))
        null => { }
    }
}
```

(`teko::text::from_utf8_lossy` names the bytes→str decode; §8 Q1 notes confirming/adding a
public decoder — `io::stream.tks:78` `Buf::from_bytes` and `lower.tks:3898` `str_to_bytes`
prove the inverse exists.)

### 2.5 Cases that FAIL (message shape)

All are compile-time; each `path`/level is a located token, so the error carries a
line:col (`err_at`). Wording is illustrative, not ratified.

| Input | Outcome | Message shape |
|---|---|---|
| `#embed("../secret.txt")` | error (M.1 inexpressible) | `#embed path escapes the project root — only in-project files (downward, no '..', no absolute path) may be embedded: "../secret.txt"` |
| `#embed("/etc/passwd")` | error | `#embed path must be project-relative — an absolute path is not embeddable: "/etc/passwd"` |
| `#embed("C:\\x")` | error | (same absolute-path error, Windows form) |
| same path from two directives | **PANIC / compile error** (M.1 fail-loud) | `#embed path conflict — "assets/x.json" is embedded by two directives; each embedded path must be unique` |
| `#embed("f.bin", Deflate, 99)` | error (const-range) | `#embed compression level 99 is out of range for Deflate (expected 0..9)` |
| `#embed("missing.txt")` | error | `#embed cannot read "missing.txt": no such file in the project` |
| `#embed("assets/*.zzz")` (glob, no match) | error (integrator-pinned §8 Q3) | `#embed pattern "assets/*.zzz" matched no files` |
| `#embed("f", Bogus, 1)` | error (closed enum) | `#embed compression type must be one of None, Deflate, Gzip` |

---

## 3. Type signatures / function shapes to add (and what they touch)

All snippets already in full-Javadoc house style; the implementer copies them verbatim.

### 3.1 Parser — a new AST node + `parse_embed` (touches `src/parser/`)

`#embed` is **not** an attribute in `parse_decl_attributes` (that machine requires a following
`fn`/`type`/`flags`/`const`, `parse_decl.tks:760–804`). Because it is decoupled, it is its own
top-level item. Two placements, both viable:

- **(integrator-pinned, veto-open) a new `Decl` variant `EmbedDecl`**, recognized in
  `parse_decl` before the fn/type/flags/const dispatch (`parse_decl.tks:1096+`) when the
  attribute name is `"embed"`. This reuses the existing `decls` stream (`Module.decls`) and
  every downstream enumeration (collect/checker) already walks it — the smallest plumbing
  change. Alternative: a dedicated `Module.embeds: []EmbedDirective` list; cleaner semantically
  but forces a new field through every `Module {…}` literal. **Recommend the Decl variant.**

```teko
/**
 * EmbedDecl — a top-level `#embed("pattern" [, TYPE, LEVEL])` directive (owner ruling
 * 2026-07-20). Unlike `#test`/`#os`/`#inject`, it annotates NO declaration; it is a
 * standalone effectful item that registers matched in-project files into the global VFS at
 * compile time. Parsed by `parse_embed`, following the `#os("…")` payload pattern.
 *
 * @param pattern          the quoted path/glob, exactly as written (validated at resolve time)
 * @param has_compression  form B was used (TYPE + LEVEL present)
 * @param comp             the codec (None when form A)
 * @param level            the compression level (ignored when comp is None)
 * @param line             the directive's source line (for located errors)
 * @param col              the directive's source column
 * @since #embed (design-ahead)
 */
pub type EmbedDecl = struct {
    pattern: str
    has_compression: bool
    comp: EmbedCompress
    level: i64
    line: u32
    col: u32
}

/**
 * parse_embed — parse a top-level `#embed(...)` directive at `pos` (the `#`), following the
 * `#os("…")` payload shape (`parse_decl.tks:779`). Accepts form A `("pattern")` and form B
 * `("pattern", TYPE, LEVEL)`; TYPE is a bare enum atom (None/Deflate/Gzip), LEVEL an Int
 * literal. Range/path validity are NOT checked here (they are resolve-time concerns) — this
 * stays purely syntactic, mirroring how `parse_const_decl` defers const-expr validation.
 *
 * @param tokens  the token stream
 * @param pos     the index of the `#` beginning the directive
 * @return        the parsed EmbedDecl and the index just past the closing `)`
 * @throws        a located error on a missing `(`, a non-string pattern, a malformed
 *                `, TYPE, LEVEL` tail, or an unknown compression type
 * @since #embed (design-ahead)
 */
fn parse_embed(tokens: []lexer::Token, pos: u64) -> Parsed<Decl> | error { /* design-ahead */ }
```

Wiring: `parse_decl_attributes` (or `parse_decl`) recognizes `Hash` + `Ident("embed")` and
delegates to `parse_embed` instead of the attribute loop; `parse_module`'s loop
(`parse_decl.tks:1183`) pushes the `EmbedDecl` like any other decl.

### 3.2 Resolution — path guard + glob (genuinely new; touches a new comptime pass)

```teko
/**
 * resolve_embed_path — turn an `#embed` pattern into the set of concrete, project-relative
 * logical paths it embeds, REJECTING by construction any path that escapes the project root
 * (owner ruling 2026-07-20, ruling 2: downward-only, in-project). Rejects a leading `/` or
 * drive-absolute prefix, any `..` segment, and any resolved path that is not a strict
 * descendant of `root`. Exact-path matching first (a pattern with no glob metacharacter is a
 * single path); glob is the deferred extension (§8 Q3).
 *
 * @param root     the absolute project root (the directory holding the .tkp)
 * @param pattern  the `#embed` pattern, verbatim from source
 * @return         the resolved in-project paths (>=1), or a located escape/no-match error
 * @throws         on absolute path, a `..` segment, an out-of-root resolution, or no match
 * @since #embed (design-ahead)
 */
fn resolve_embed_path(root: str, pattern: str) -> []str | error { /* design-ahead */ }
```

- **Compile-time file read (genuinely new).** `teko::io::read_file` exists but returns `str`
  (`io.tks:18`); binary assets need bytes. There is `write_file_bytes` (`io.tks:46`) but **no**
  `read_file_bytes`. Add one maintained-C seam — this is inside the frozen-C **exception**
  (`teko_rt.{c,h}`), so it is allowed:
  `pub extern fn read_file_bytes(path: str) -> []byte | error = "tk_rt_read_file_bytes" from "teko_rt"`.
  It is called only by the compiler's embed pass, never by the emitted program (M.0, §1.1).
- **Glob matching (genuinely new).** Recommend (integrator-pinned, veto-open): ship **exact
  path** first; add glob (`*` within one path segment, then `**`) as a fast-follow. Grounds in
  M.5 — the mascot needs only an exact path.
- **Conflict panic.** The pass accumulates resolved paths into an ordered set; a duplicate is
  the PANIC of §2.5 (M.1).

### 3.3 Const materialization — Tier-A flatten (touches the #594 comptime→rodata machine)

The embed pass, after resolving + reading + compressing, emits **four Tier-A module-level
consts** (blob heap `[]byte`, name heap `[]byte`, packed table `[]byte`, count scalar) via the
#594 Tier-A aggregate→rodata path (`const-module-level-plan.md` §6.4, Crumb 6). Tier-A is
"self-contained rodata blob, NO internal pointer" (`const-tb1-design.md` §1.3; plan §5.1):
a `[]byte` is exactly Tier-A. The `FileSystem` const is a thin struct over those consts. See
§7.2 for why this flatten is chosen over a pointer-bearing struct (which would be Tier-B, and
Tier-B is blocked).

### 3.4 Compress — exact signatures to reuse (touches nothing; call-only)

Verified in `src/compress/`:

| Codec | compress (build) | decompress (runtime accessor) |
|---|---|---|
| Deflate | `deflate(data: []byte) -> []byte` (`deflate.tks:100`) | `inflate(data: []byte) -> []byte \| error` (`inflate.tks:711`) |
| Gzip | `gzip_compress(data: []byte) -> []byte` (`gzip.tks:57`) | `gzip_decompress(data: []byte) -> []byte \| error` (`gzip.tks:98`) |
| None | — (verbatim) | — (verbatim) |

Build-side compression runs in the compiler; the emitted `read` calls the matching
decompressor. `deflate`/`gzip_compress` return `[]byte` (no error arm); the decompressors
carry `| error` (a corrupt stream) — but the compiler produced the bytes, so at the accessor
they cannot fail (§8 Q1 on defensive widening).

### 3.5 Mascot wiring (touches `src/build/` command entry)

`print_mascot(no_tty)` (§2.4) is called at each command's start, after `no_tty_of(args)` is
computed (the value already flows into `compile_project_g`, `project.tks:1772`). One call site
per user-facing command entry; no signature churn.

---

## 4. Where the compile-time read plugs in

There is no compile-time file-read phase today; the compiler reads *source* via
`teko::io::read_file` in the driver. The `#embed` pass is a **new post-parse, pre-lowering
resolution phase** (integrator-pinned, veto-open) that walks `Module.decls` (or the `embeds`
list) for `EmbedDecl`s, runs `resolve_embed_path`, reads each file via the new
`read_file_bytes` seam, compresses, checks conflicts, and hands the flattened heaps to the
const-materialization (§3.3). It sits beside `collect`/`resolve` in `src/checker/` (which
already enumerate decls — grep confirmed `os_guard`/`has_inject` are read there). It runs
**once per build**, in the compiler — never in the emitted program.

---

## 5. Crumb sequence (honest S/M/L) + ritual

Ordered, each independently gate-able. C0 is DESIGN-AHEAD scaffolding that compiles **today**
and is inert; C1–C7 land when the owner unblocks.

| # | Size | Crumb | Regression gate |
|---|---|---|---|
| **C0** | S | **Scaffold that compiles today**: new `src/embed/embed.tks` (namespace `teko::embed`) with `EmbedCompress`, `FileSystem` (empty-default methods returning `null`/`false`/`empty`), and a `const FILES: FileSystem = <empty>`. Full Javadoc, honest-stops where behavior is future. Inert: nothing calls it yet. | new `embed_test.tkt` compiles + trivial empties; full gate byte-identical (adds an unused module) |
| **C1** | M | Parser: `EmbedDecl` node + `parse_embed`; wire into `parse_decl`/`parse_module`. | `parse_decl_test.tkt` parse goldens for both forms + each malformed tail |
| **C2** | M | `resolve_embed_path`: exact-path, downward-only, escape rejection, conflict detection (pure fn — no I/O). | `embed_test.tkt`: escape/absolute/`..`/conflict → error; in-project → ok |
| **C3** | M | Compile-time read seam `tk_rt_read_file_bytes` in **maintained** `teko_rt.{c,h}` + `io::read_file_bytes`; embed pass reads bytes; per-directive compress (call `deflate`/`gzip_compress`); level-range validation. | `io` seam test; embed pass unit: bytes read + round-trip through inflate |
| **C4** | L | Const materialization: emit the four Tier-A consts into rodata via #594 Tier-A; build the `FileSystem` const over them. **Changes emitted bytes** (the binary now carries the VFS). | re-golden; **fixpoint gen1==gen2**; VM==native |
| **C5** | S/M | Accessor: `read`/`exists`/`list` (+ `read_str`) decode the packed table and inflate per `comp_tag`. | `embed_test.tkt`: embed→read round-trip for None/Deflate/Gzip |
| **C6** | S | Mascot: `print_mascot` gated on `output_is_tty`; call at command entry. **The corpus now USES `#embed`** → requires C1–C5 in the released SEED first (see §5.1). | build/CLI test: TTY prints, `--no-tty`/non-TTY silent |
| **C7** | M | (deferred extension) Glob matching (`*`, then `**`); zstd/lz4 variants. | glob match fixtures; new-codec round-trip |

### 5.1 Seed sequencing (bootstrap law)

The corpus may not USE a feature its seed lacks. `#embed` is understood only after C1–C5. So
**C1–C5 must land and ship in a released seed BEFORE C6** (the mascot, which puts `#embed` in
the compiler's own corpus). Sequence: land C1–C5 → 🔑 SEED BUMP → then C6 may add
`#embed("docs/brand/mascot.ansi")` to the corpus.

### 5.2 Ritual points

- **Per crumb:** the crumb's own `.tkt` gate.
- **RITUAL POINT — C4 (bytes change):** the binary first carries the rodata VFS. Full gate:
  re-golden every backend/object goldens, **fixpoint gen1==gen2 byte-identical**, `diff_vm_native.sh`
  (VM==native — same read/exit outcome), `TEKO_MEM_PARANOID=1`, Javadoc/`//`-audit. C0–C3 are
  byte-inert (no emitted-const change) and prove out on their unit gates + a green full gate.
- **RITUAL POINT — after C5, before the C5→seed bump:** full gate again; the accessor is now
  exercised by `embed_test.tkt` round-trips under both engines.

---

## 6. Regression fixtures to add (inputs → expected exit, VM and native)

Unit `.tkt` (run identically under VM and native — same `error`/exit outcome):

1. **Parse, form A/B** (`parse_decl_test.tkt`): `#embed("a/b.txt")` and
   `#embed("a/b.txt", Deflate, 6)` parse to the expected `EmbedDecl`; each malformed tail
   (missing `)`, non-string pattern, `Deflate` without a level, unknown type) → the located
   error.
2. **Escape rejection** (`embed_test.tkt`): `resolve_embed_path(root, "../x")`,
   `"/abs/x"`, `"a/../../x"`, `"C:\\x"` → **error** (each message names the escape kind);
   `"assets/x.json"` in-project → **ok**, one resolved path.
3. **Conflict panic**: two directives → the same resolved path → **compile error** (message
   contains "conflict").
4. **Level range**: `#embed("f", Deflate, 99)` → **error** ("out of range"); `…, Deflate, 6)`
   → ok.
5. **Round-trip, all codecs** (`embed_test.tkt`): embed a known blob under None/Deflate/Gzip,
   then `FILES.read(path)` == the original bytes (proves build-compress + accessor-decompress
   are inverse, and that `inflate`/`gzip_decompress` are wired).
6. **Empty VFS** (C0 guard): with no `#embed`, `FILES.read(x) == null`, `FILES.exists(x) == false`,
   `FILES.list().len == 0` — the empty default costs nothing and never errors.
7. **Mascot gating** (build test): `output_is_tty(false)` path writes the mascot bytes;
   `output_is_tty(true)` (i.e. `--no-tty`) and non-TTY write nothing; absent embed → silent.
8. **Byte-identity of the empty case** (C0/C4): a program with no `#embed` emits the same
   rodata as before (the empty VFS adds no entry) — guards that the machinery is pay-per-use.

No existing fixture changes its expected exit code until C4; from C4 the backend goldens
re-baseline once (the new rodata), then stay byte-identical.

---

## 7. Dependencies

### 7.1 DECOUPLED from the generic stack (`generic-stack-completion.md`)

The VFS is a **dedicated `FileSystem` struct**, not a `map<K,V>` (owner ruling 3), specifically
so it does **not** need `Map<K: Hashable & Eq, V>`. `generic-stack-completion.md` is a
**separate, independent wave** — reference it as adjacent, **not** a gate here. `#embed` runs
with zero generic instances (it keeps the `any_generic` no-op invariant the mono pass relies
on). No dependency.

### 7.2 The ONE honest dependency: const-aggregate → rodata (#594)

`#embed` materializes its data through the #594 const→rodata machine. The tiering matters:

- A `FileSystem` struct **with slice/pointer fields materialized as a single const aggregate**
  is **Tier-B** (pointer-bearing — `const-tb1-design.md` §1.3), and Tier-B is **currently
  honest-stopped** at the producer (`serialize_const`, `lower.tks:5142`) until crumbs
  T-B2..T-B6 land. If the VFS were emitted that way, `#embed` would **gate on #594 Tier-B**.
- **Recommended (integrator-pinned, veto-open): the FLATTENED Tier-A representation** (§2.3,
  §3.3). Each heap is a plain `[]byte` and the count a scalar — all **Tier-A** ("self-contained
  rodata blob, no internal pointer"; plan §5.1, §6.4 confirms `[]byte`/`str`/flat-POD aggregates
  are Tier-A with **zero backend change**). This makes `#embed` **independent of Tier-B**: it
  can ship on the Tier-A machine already designed (Crumb 6), needing only C4's emission of the
  four consts. This is the decoupling that lets the feature land the moment the owner unblocks,
  without waiting for T-B2..T-B6.

**Owner note:** ruling 3 said "Tier-A/B", acknowledging Tier-B. The architect's refinement is
that a Tier-A flatten avoids the Tier-B block entirely, so `#embed` need not wait on #594's
Tier-B wave. If the owner prefers the literal pointer-bearing struct for API cleanliness, that
is a real choice — it defers `#embed` behind T-B6. Both are captured; the flatten is
recommended (ships sooner, same public API).

### 7.3 Orthogonal to F1 / AL / comptime

References their declared shapes only; gates on none. The compile-time read reuses the
existing driver I/O discipline; the mascot reuses `output_is_tty` already in the tree.

---

## 8. Open questions (owner only — each with a recommendation)

1. **The exact accessor API.** Recommend `read(path) -> []byte?`, `exists(path) -> bool`,
   `list() -> []str`, **plus** `read_str(path) -> str?` for text assets (the mascot). Should
   `read` widen to `[]byte | error` defensively (a corrupt embedded stream) even though the
   compiler produced the bytes? Recommend **no** — keep `[]byte?`; a build-produced stream
   cannot be corrupt, and `?` keeps the common path clean (M.5). *(integrator-pinned,
   veto-open.)*
2. **The VFS const's namespace/name.** Recommend `teko::embed::FILES` (a `FileSystem` const
   `FILES` in namespace `teko::embed`). Alternatives: `teko::vfs::ROOT`, a bare root `EMBED`.
   *(integrator-pinned, veto-open.)*
3. **Glob now vs. later.** Recommend **exact path now, glob (C7) later** — the mascot needs
   only an exact path, and M.5 favors the smallest surface that delivers use #1. When glob
   lands, recommend **no-match is an error** (fail-loud, M.1), not a silent empty. *(integrator-pinned,
   veto-open.)*

No genuine unresolved law tension remains — the owner's five rulings + the Tier-A flatten
resolve every M.0/M.1/M.5 question. **No HALT.** The only true decision left for the owner is
§7.2 (Tier-A flatten vs. literal Tier-B struct), and that is a scheduling/aesthetic choice with
a clear recommendation, not a law conflict.

---

## 9. What remains BLOCKED / deferred (explicit)

- The **whole feature** is deferred by owner decree (M.5, "supérfluo por hora", 2026-07-20).
  Nothing here is scheduled.
- **C0** (the scaffold) is the only part that can land today without a schedule — it compiles,
  is inert, and gives C1+ a home. It is written but **not** applied by this document (no product
  code touched).
- If the owner chooses the literal pointer-bearing struct over the Tier-A flatten (§7.2), C4
  additionally **blocks on #594 T-B2..T-B6**. With the recommended flatten, C4 needs only the
  already-designed #594 **Tier-A** Crumb 6.
- The **new maintained-C seam** `tk_rt_read_file_bytes` (C3) is inside the frozen-C exception
  (`teko_rt.{c,h}`) and is the one non-`.tks` addition; it is compile-time-only and leaves no
  trace in emitted programs.
