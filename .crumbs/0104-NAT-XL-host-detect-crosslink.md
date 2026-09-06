---
seq: 0104
crumb-id: NAT-XL
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/teko-target-crosslink-0.3.1.md:117-192"               # supported_targets/host_target/TargetRow
  - "docs/design/teko-target-crosslink-0.3.1.md:398-472"               # crumb sequence C1..C6 (C3 folds here)
  - "docs/design/plano-mestre-0.3.1-implementacao.md:289"              # M4 NAT-XL row
  - "src/build/project.tks:1299"                                       # native_target
  - "src/build/manifest.tks:31"                                        # Manifest extern.libs os-arch key
---

# 0104 · NAT-XL — host detection + supported-targets table + cross-emit note

> Host detection (`teko::os()`/`teko::arch()`) + the single supported-targets table
> (`supported_targets`/`TargetRow`/`host_target`) + the cross-emit honest note — the ratified TKP
> cross-link surface. Folds the C3 table left deferred to the arch-seed bump.

## Goal

The target-resolution leg (TKP ratified 2026-07-24, decisions CLOSED): make `native_target()`
(`project.tks:1299`) resolve from a SINGLE supported-targets table keyed by `<arch>-<os>` composed from
`teko::os()` + `teko::arch()`, so an unset `TEKO_TARGET` picks the precise host target (R1), a set value
FAILS honestly on an unsupported os-arch listing the supported set (R2), a cross target emits normally
(R3) with an honest emit-not-run note (the host cannot execute a cross object), cross static libs are
validated at COMPILE time (R4/M.3), and cross shared libs FAIL by default with `--allow-undef` as the sole
opt-in (R5). Per the crosslink doc, C1/C2/C4/C5/C6 already landed; this crumb FOLDS the deferred **C3**
(`supported_targets`/`TargetRow`/`host_target`) now that the `teko::arch()` seed is in place — replacing
C1's os-only heuristic with the precise os-arch table. **Byte-preserving** (mac-host default stays
`Arm64Macho` so the arm64 differential + fixpoint are byte-identical; the table only makes the interim
os-only default PRECISE); reseed-class `none` (a `[dry]` leaf). The manifest dep is `S17 (banked)` — an
already-resolved banked gate, not a build blocker (per 000-INDEX:46).

## Where

- `src/build/project.tks:1299` `native_target` — resolve via `host_target()` (unset) / `target_from_name()`
  (set), threading `NativeTarget | error` to the build's single honest-stop.
- `src/build/project.tks` — the new `host_target()`/`supported_targets()`/`TargetRow`, folding the
  standalone `target_name`/`target_objfmt` columns C4/C5 landed into `TargetRow`.
- `src/build/manifest.tks:31` `Manifest` + the `os_lib_*` parallel columns — the `[extern.libs.<os-arch>]`
  section-key match (already parsed; C4 pinned it) + `validate_static_libs_for_target` (R4).
- `src/checker/scope.tks` / `src/codegen/codegen.tks` — `teko::arch()` wiring (C2, already landed +
  seeded); confirm `host_target()` may CALL it (the seed recognises it).

## How

1. **The single table** (`supported_targets`/`TargetRow`): one ordered list of `(canonical os-arch name,
   NativeTarget variant, objfmt)` rows — the SOLE source for the `target_from_name` lookup, the
   `host_target` composition, the R2 honest-error message, the M.3 validation, and the regressor matrix. No
   target string literal lives anywhere else (D39).

```teko
/**
 * host_target — the NativeTarget for the build host, composed as "<arch>-<os>" from teko::os() + teko::arch()
 * and resolved through supported_targets. An unsupported host os-arch (e.g. an "unknown" arch) is the
 * honest stop for the DEFAULT build — native_target surfaces the R2 error listing the set, so an
 * unbuildable host never silently mis-lowers.
 *
 * @return the host's own-backend target, or an error naming the unsupported host os-arch (D39/R2)
 * @throws when "<arch>-<os>" is absent from supported_targets
 * @since 0.3.1
 */
fn host_target(): NativeTarget | error

/**
 * TargetRow — one supported-targets row: the canonical "<arch>-<os>" name, its NativeTarget dispatch
 * variant, and its object format ("elf" | "macho" | "coff") — read by target_from_name, host_target, the
 * M.3 static-lib validator, and check_object_wellformed. The single source of truth (D39).
 *
 * @since 0.3.1
 */
type TargetRow = struct { name: str; variant: NativeTarget; objfmt: str }
```

2. **`target_from_name` as a table lookup** (R2): map a raw `TEKO_TARGET` (canonical or alias) via
   `supported_targets`, returning an error listing the set on a miss — REPLACING any silent fallback.
3. **Cross honest-note** (R3, C5 already landed `cross_note`): confirm `native_target`'s result feeds
   `cross_note` so a cross build prints `cross: emitted <target> on host <host> — not executed` and SKIPS
   the run honestly (never a fabricated green).
4. **R4/R5 validation** (C4/C6 landed): `validate_static_libs_for_target` (compile-time static-locality)
   + the shared default-fail / `--allow-undef` opt-in; confirm both read `TargetRow.objfmt` for the
   elf/macho/coff split.
5. **Preserve mac fixpoint**: `host_target()` maps `macos→arm64` by construction, so the mac-host default
   stays `Arm64Macho` — the arm64 differential + `gen2==gen3` stay byte-identical.

## Rulings & laws

- **Teko-only:** `src/build/*.tks` + the already-seeded `teko::arch()` wiring; `tk_rt_arch` is the
  sanctioned maintained-C leaf (C2, already landed).
- **W15 full Javadoc** on `host_target`/`supported_targets`/`TargetRow`/`target_from_name`; flatten the
  `NativeTarget | error` ripple with early-return guards; no `//`.
- **Decisions CLOSED (owner 2026-07-24):** R1-R5 + the §4.2 arch-key spec-fill — ratified, not a redesign,
  not a menu.
- **Honest fail over fabricated capability (M.3):** an unsupported target is a compile error listing the
  set; a cross build is emitted-not-run, never a fake pass.
- **Mac fixpoint byte-identical:** `host_target` maps macos→arm64 by construction.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change on a supported host; sweep `.tkt` after the `native_target` signature
  ripple.

## Fixtures

The self-build resolves only the HOST target; the cross + reject paths are never self-exercised, so
isolated oracles are required (these landed with C1/C4/C5/C6 per the crosslink doc §7.1 — cite + keep):

| fixture | asserts | expected |
|---|---|---|
| `own_default_host_object_wellformed` | unset `TEKO_TARGET` on a linux host emits ELF x86_64 (not Mach-O) | `0` |
| `target_unsupported_lists_the_set` | `TEKO_TARGET=bogus-arch` fails; diagnostic LISTS `supported_targets` (R2) | `EXPECT_COMPILE_FAIL` |
| `cross_coff_emits_and_skips_run` | `TEKO_TARGET=x86_64-windows` on linux emits COFF + honest RUN-skip (R3) | `0` (emit ok, run skipped) |
| `cross_static_lib_must_be_present` | a cross `static:` lib absent for the target is a COMPILE error naming (lib, target, path) (R4) | `EXPECT_COMPILE_FAIL` |
| `cross_shared_default_fails_allow_undef_opts_in` | a cross `shared:` lib absent fails by default; `--allow-undef` opts in (R5) | `EXPECT_COMPILE_FAIL` / `0` |

## Gate

`[dry]` — compile + the fixtures + trivial fixpoint (the host default is byte-identical on mac/linux).
"Green" = `native_target` resolves through the single `supported_targets` table, the host default is
precise (mac stays `Arm64Macho`, fixpoint byte-identical), an unsupported target errors listing the set, a
cross target emits + honest-skips the run, and R4/R5 validation holds. Reseed-class: `none`.

## Deps

`S17 (banked)` — verbatim from 000-INDEX (an already-resolved banked gate, not a build blocker per
000-INDEX:46). The `teko::arch()` seed (C2) is already in place.

## Done when

`native_target` resolves from the single supported-targets table (precise host default, mac fixpoint
byte-identical), an unsupported target errors listing the set, a cross target emits + honest-skips the run,
cross static/shared libs follow R4/R5, the fixtures pass, and the `[dry]` build is byte-identical.
