# NOTES — `teko --version` / `--help` implementation (feat/cli-version-flag)

Investigation of how the version/description is CURRENTLY embedded in the binary, and
the design chosen for `--version`/`-v` and `--help`/`-h`.

## Where the entry point actually lives

There is NO `src/main.tks` / `src/main.c`. The executable ENTRY POINT twins are at the
**project root**:

- `main.c`  — the C SEED of `teko`'s `int main(int argc, char **argv)`. Built by CMake:
  `add_executable(teko main.c src/driver.c src/runtime/teko_rt.c ...)`.
- `main.tks` — the canonical Teko original (the `teko::` virtual-main). Built when `teko`
  compiles its own corpus (`teko build .`).

These are the SUPREME-RULE twins for this change. `src/driver.c` / `src/driver.tks` are the
driver LIBRARY (compile/run/test project entry points), not the CLI dispatch.

Both `main.c` and `main.tks` already have the "no args → usage, exit 2" path:
- `main.c`: `if (argc < 2) { usage(); return 2; }` (line ~113); `usage()` at line ~49.
- `main.tks`: `if args.len < 2 { eprintln(...); teko::exit(2) }`.

## How the version is CURRENTLY embedded (C7.1k)

The version is embedded per-BUILT-BINARY, sourced from the manifest of the project being
built (`teko.tkp` for teko itself):

1. `tk_emit_meta(name, version, suffix, description)` — `src/codegen/codegen.c:165` and its
   twin `src/codegen/codegen.tks:187`. Emits an `@(#)`-marked `static const char
   tk_build_meta[]` = `"@(#)<name> <version>[-<suffix>] | <description>"` appended to the
   generated C (what(1)/strings-readable). Called from `driver.c::tk_backend` (line ~530)
   and `project.tks::run_cc`-adjacent (project.tks line ~487).
2. macOS Info.plist section — `driver.c::write_macos_plist` (line ~230) + `project.tks`
   `plist_xml` (line ~413): a `__TEXT,__info_plist` Mach-O section with
   CFBundleShortVersionString / CFBundleVersion = `m.version`.

Both draw from the **already-parsed manifest struct** (`tk_manifest.version` / `.suffix` /
`.description`), which is the teko.tkp source of truth. But:
- `tk_build_meta` is a LOCAL static in the *generated* C — not readable as a runtime string
  from `main.c` / `main.tks` without a fragile self-parse of the binary.
- The plist is macOS-only and not a portable runtime string either.

So EXPOSING the existing embedded string directly to `--version` is not clean. Instead the
task's sanctioned fallback applies: a **build-time constant fed from teko.tkp**, wired into
BOTH build paths so both engines embed byte-identically.

## Chosen design — `tk_rt_version()` host surface

The runtime `teko_rt.c` is linked by BOTH the bootstrap `teko` (via CMake) AND every binary
`teko build` produces (via `run_cc`, which compiles `teko_rt.c`). That is the one seam both
build paths share, so it is where the version constant lives.

- **`src/runtime/teko_rt.{h,c}`**: add `tk_str tk_rt_version(void)` returning a compile-time
  string `TEKO_VERSION_STRING`, with an in-file fallback `#ifndef TEKO_VERSION_STRING`
  default of `"0.0.0.0-dev"` (honest placeholder when no manifest-derived define is passed).
- **Bootstrap build (CMake)**: at configure time, parse `version` + `suffix` from `teko.tkp`
  (raw — NO gen substitution), and
  `target_compile_definitions(teko PRIVATE TEKO_VERSION_STRING="0.0.1.0-bootstrap")`.
- **Self-hosted build (`driver.c::run_cc` + `project.tks::run_cc`)**: build a
  `-DTEKO_VERSION_STRING="<m.version>[-<m.suffix>]"` cc flag from the ALREADY-PARSED manifest
  and pass it when compiling `teko_rt.c`. This is generic: a user building their project gets
  THEIR manifest's version; teko building itself gets `0.0.1.0-bootstrap`. Single source of
  truth = the project's own `.tkp`. No runtime file read.
- **`src/env/env.tks`**: declare `pub extern fn version() -> str = "tk_rt_version" from "teko_rt"`
  so `main.tks` can call `teko::env::version()`; it lowers to the raw `tk_rt_version()` C call.
- **`src/codegen/codegen.{c,tks}`**: add `version -> tk_rt_version` to the plain builtin map
  (mirrors the `os -> tk_rt_os` precedent, line ~2056), so the call lowers to `tk_rt_version()`
  in the generated main, regardless of extern-resolution order.
- **`src/checker/scope.{c,tks}`**: add a `version` builtin_fn `() -> str` (mirrors `os`), so
  the call typechecks even via the builtin fallback path.

`main.c` calls `tk_rt_version()` directly (already includes `driver.h`; add the teko_rt.h
decl visibility via driver.h's include chain or a direct prototype).

### Why byte-identity holds
The version string lives in `teko_rt.c` behind a `-D`, NOT in the generated `.c`. The
generated C from `main.tks` contains only a `tk_rt_version()` CALL (same token in every gen),
so gen-1 == gen-2 == gen-3 generated C stays byte-identical. Both gens' `teko_rt.c` is
compiled with the same `-DTEKO_VERSION_STRING="0.0.1.0-bootstrap"` (CMake for gen-1, run_cc
for gen-2+), so the linked binaries also carry the identical string.

## The flags

`--version` / `-v`  → print `teko <version>` (one line), exit 0.
`--help` / `-h`     → print the usage banner + the version line, exit 0.
Both SHORT-CIRCUIT before the `argc < 2 → usage, exit 2` path (which is unchanged).
Version string = `TEKO_VERSION_STRING` (raw manifest `version` + `-suffix`), e.g.
`0.0.1.0-bootstrap`.

## Constraints honored
- RAW manifest version embedded (`0.0.1.0-bootstrap`); NO "4th field = gen" substitution
  (that is `scripts/derive_version.sh` release-wiring, untouched).
- No `release.yml` / `tag-on-version-bump.yml` / `derive_version.sh` changes.
- Surface = `main.c` + `main.tks` + `teko_rt.{c,h}` + `env.tks` + `codegen.{c,tks}` +
  `scope.{c,tks}` + `CMakeLists.txt`.
