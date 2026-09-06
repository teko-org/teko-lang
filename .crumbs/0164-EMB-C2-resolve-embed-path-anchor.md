---
seq: 0164
crumb-id: EMB-C2
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: [EMB-C1]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:2"        # §2 anchoring + §2.1 error table
  - "DECISION_LOG.md:1151"                                       # D135 (/=project-root supersedes ruling 2)
  - "src/build/discover.tks:2"                                   # SourceFile{path,namespace}
  - "src/build/project.tks:399-403"                              # manifest name/source (project root)
---

# 0164 · EMB-C2 — `resolve_embed_path`: nu=file-relative / `/`=project-root anchor, escape reject, conflict PANIC, key normalize

> The path resolver, honoring the owner's TWO anchor forms: a bare pattern is FILE-relative (the dir of
> the .tks carrying the directive); a leading `/` is PROJECT-root (where the .tkp is). Both normalize to a
> `<project-name>::/path-relative-to-root` key (the `<project-name>::` compiler-injected). `..`-escape and
> Windows drive-absolute reject (M.1). A duplicate key across directives is a PANIC. Pure fn, no I/O →
> inert (no pass calls it until EMB-C3).

## Goal

Turn an `EmbedDecl` (pattern + src-path) into its concrete in-project key, per annex §2: resolve the
anchor (bare → `dirname(src_path)`; `/…` → project root), join, normalize to root-relative, reject any
result that escapes the root via `..` and any `X:\` drive-absolute, and prefix `<project-name>::/`
(from the manifest). Accumulate keys into an ordered set; a duplicate is the conflict PANIC (M.1
fail-loud). Exact-path only (glob is the deferred EMB-C7). Pure — the compile-time file READ is EMB-C3.

## Where

```teko
/**
 * resolve_embed_path — resolve an `#embed` pattern to its normalized VFS key. A bare pattern anchors at
 * the directive's source-file directory (file-relative); a leading `/` anchors at the project root. The
 * result is normalized relative to `root` and prefixed `<proj>::/`. Rejects, by construction, any path
 * escaping `root` via `..` and any Windows drive-absolute prefix.
 *
 * @param root      the absolute project root (the dir holding the .tkp)
 * @param proj      the project name (the compiler-injected `<project-name>::` asset namespace)
 * @param src_dir   the directory of the .tks carrying the directive (bare-pattern anchor)
 * @param pattern   the `#embed` pattern verbatim
 * @return          the normalized `<proj>::/rel` key, or a located escape error
 * @throws          on a `..` escape past root, or a drive-absolute pattern
 * @since 0.3.1
 */
fn resolve_embed_path(root: str, proj: str, src_dir: str, pattern: str): str | error
```

- NEW helper file or fold into the embed pass module (`src/embed/` or `src/checker/`).
- Uses `SourceFile.path` (`discover.tks:2`) for `src_dir`; `m.name`/`m.source` (`project.tks`) for
  `proj`/`root`.

## How

1. If `pattern` starts with `/` → anchor = `root`, rel-input = `pattern[1..]`. Else anchor =
   `dirname(src_path)`, rel-input = `pattern`.
2. Reject a `X:\`/`X:/` drive-absolute prefix → `#embed path must not be drive-absolute: "<pattern>"`.
3. Join anchor + rel-input; normalize `.`/`..` segments. If normalization rises ABOVE `root` → escape
   error `#embed path escapes the project root: "<pattern>"`.
4. Compute rel = the normalized path relative to `root`; key = `<proj>::/` + rel.
5. Conflict: the embed pass keeps an ordered key set; a duplicate key → PANIC
   `#embed path conflict: "<key>" embedded by two directives`.

Error wording = compiler style (annex §2.1). `#embed("/etc/passwd")` re-anchors to `<root>/etc/passwd`
(in-project) — NOT an absolute-path error (supersedes ruling 2).

## Rulings & laws

- **Teko-only.**
- **Owner D135 + in-flight correction:** nu=file-relative, `/`=project-root; `<project-name>::` compiler-
  injected; `..`-escape/drive reject; `/`=project-root supersedes ruling 2.
- **M.1 make-inexpressible:** escape rejected at resolve time.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; pure fn inert → full gate byte-identical.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `embed_escape_parent` | `resolve_embed_path(root, proj, src, "../x")` escapes root | `EXPECT_COMPILE_FAIL` |
| `embed_escape_drive` | `"C:\\x"` drive-absolute | `EXPECT_COMPILE_FAIL` |
| `embed_conflict` | two directives → same key | `EXPECT_COMPILE_FAIL` (message "conflict") |
| `embed_slash_reanchor` | `"/docs/x"` re-anchors in-project (no error when present) | `0` |

## Gate

`[dry]` — compiles; the resolver is pure and unconsumed → full gate byte-identical. "Green" = both
anchors normalize to `<proj>::/rel`, escapes/conflicts reject, `/`-re-anchor is in-project. Reseed-class:
`none`.

## Deps

`EMB-C1`

## Done when

`resolve_embed_path` normalizes both anchor forms to the `<proj>::/` key, rejects escapes/drive/conflict,
and the self-build is byte-identical (unconsumed).
