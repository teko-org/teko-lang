# TEKO_TARGET semantics + per-target deps in the manifest (cross-link)

- Status: **DECISIONS CLOSED** (2026-07-24) — all decisions ratified: R1-R5 (§0) + the §4.2
  arch-key spec-fill. Doc-only; no product code in this change.
- Branch: `design/teko-target` (local, no push).
- Chain: engages `.31` after the current vagões; `origin/main` is the reference.
- Owner rulings registered: 2026-07-24 (verbatim in §0; shared-lib ruling in §5.0; §4.2 spec-fill
  RATIFIED by the owner 2026-07-24 — see §4.2).
- Author role: architect. Deliverable is this plan; implementers copy the Javadoc snippets verbatim.

---

## 0. Owner ruling (verbatim, dated 2026-07-24)

> "TEKO_TARGET deve ser do sistema e arquitetura atuais quando não definido; quando definido,
> deve falhar em arquiteturas e sistemas não suportados; se diferente do sistema atual, deve
> emitir normalmente, contudo as libs estáticas devem estar apontadas no tkp para a arquitetura e
> sistema desejados e disponíveis localmente; já o que é shared/dynamic, não precisa ter a lib
> localmente (o linker conseguirá resolver), só não vai executar."

Decomposed into ratifiable clauses (law-first):

- **R1 (host default).** `TEKO_TARGET` unset ⇒ target = current OS AND current arch.
- **R2 (honest fail).** `TEKO_TARGET` set ⇒ FAIL on unsupported OS/arch, with a message that lists
  the supported set (single source of truth — D39).
- **R3 (cross emit).** target ≠ host ⇒ emit normally (object + link driver selection only).
- **R4 (static locality).** cross static libs MUST be pointed at in the `.tkp` for the requested
  os-arch AND present locally; a requested target whose static lib is not pointed-at/present is a
  COMPILE error (M.3), never an opaque `ld` failure.
- **R5 (shared locality — REVISED by owner ruling 2026-07-24).** The default is to FAIL when a
  shared lib is not found at link. The dev either supplies the lib, or passes `--allow-undef` (OUR
  Teko-linker flag) to enable an explicit "blind" link. Never a silent blind-link — opt-in, M.3.
  The cross binary still will not RUN on the host. Literal phrase registered in §5.0.

---

## 1. The reproduced bug and where the wrong default is born

Reproduced today: `--backend=native` with no `TEKO_TARGET` on linux x86_64 emitted a **Mach-O
arm64** object; `cc`/`ld` then died with an opaque cross-format error. Setting
`TEKO_TARGET=x86_64-linux` made the whole vertical work (exit 42).

Inventory of the defect surface (single file: `src/build/project.tks`):

- `native_target()` — `src/build/project.tks:1108-1109`. Unset `TEKO_TARGET` falls back to
  `NativeTarget::Arm64Macho` unconditionally. **This is the bug's origin.**
- `target_from_name()` — `src/build/project.tks:1077-1095`. An unrecognized/unsupported value
  ALSO falls back to `NativeTarget::Arm64Macho` (line 1095) — a silent mis-lower, violating R2.
- `emit_native()` dispatch — `src/build/project.tks:1179-1187`. Correct once `native_target()` is
  correct; it only selects the per-ISA tail (isel/regalloc/encode/objfile). No change of substance.
- `default_cc_for_target()` — `src/build/project.tks:602-610`. Already target-aware; consumes the
  same `NativeTarget`, so it rides on the fix for free.

**Where the "supported targets list" lives today.** There is no single table. The list is
implicit in two coupled places: the `NativeTarget` enum (`src/build/project.tks:1059`) and the
string arms of `target_from_name()` (`:1077-1095`). D39 wants ONE source. See §3.

**Host-detection primitives available today.**

- `teko::os()` — builtin, backed by `tk_rt_os()` (`src/runtime/teko_rt.c:1754`), returns
  `"macos"|"linux"|"windows"|"unknown"`. Wired at `src/checker/scope.tks:450` (signature) and
  `src/codegen/codegen.tks:3036` (C symbol `tk_rt_os`). Already used by `target_os()`
  (`src/build/project.tks:105-111`) for `#os(...)` conditional compilation.
- **There is NO `teko::arch()` / `tk_rt_arch()`.** This is the missing primitive for a precise
  host default (arm64-linux vs x86_64-linux, arm64-macos vs x86_64-macos). It is the ONE maintained-C
  addition this design needs (`teko_rt.{c,h}` is the sanctioned exception to the C freeze).

---

## 2. Host detection (Teko-pure, D39 single source)

### 2.1 Two-phase host default (adiantar o que der)

The motivating bug is arch-mismatch on a linux x86_64 host, but the URGENT fix does NOT require the
new arch primitive. Split:

- **Phase 1 (crumb C1, ships now, seed-safe).** Derive the host default from `teko::os()` ALONE,
  choosing each OS's dominant arch: `linux → X8664Linux`, `macos → Arm64Macho`,
  `windows → X8664Windows`. This uses only the builtin already in the bootstrap seed, needs no
  bump, and **fixes the reproduced bug immediately** (linux x86_64 no longer emits arm64 Mach-O).
  On a mac host the default stays `Arm64Macho` — FIXPOINT and the arm64 differential are byte-preserved.
- **Phase 2 (crumbs C2+C3, after a seed bump).** Add `teko::arch()` and make the default precise
  (arm64-linux, x86_64-macos, etc.). Phase 1's os-only heuristic is the honest interim; Phase 2
  supersedes it.

### 2.2 The arch primitive (maintained-C, seed-gated)

`teko_rt.{c,h}` is the sanctioned C exception. Add a leaf mirroring `tk_rt_os`:

```c
/* tk_rt_arch — the host CPU architecture as a canonical lowercase token
 * ("x86_64" | "arm64" | "unknown"), selected from the compiler's target
 * predefines. Mirrors tk_rt_os's already-working plain-str shape (no lift). Note "arm64"
 * (not "aarch64") and "x86_64" (not "amd64") are the canonical spellings so the token
 * concatenates directly with tk_rt_os() into a NativeTarget key. [backs teko::arch] */
tk_str tk_rt_arch(void);
```

Selection: `__aarch64__ → "arm64"`, `__x86_64__ → "x86_64"`, else `"unknown"`. (Implementer maps
the predefines; this doc states the contract only.)

Front-end wiring mirrors `os` EXACTLY at the two sites that make it usable by the compiler
(the compiler is compiled by the frozen C backend, so C emission is what matters):

- `src/checker/scope.tks` — beside line 450, add the `arch` signature (`(): str`).
- `src/codegen/codegen.tks` — beside line 3036, add `else if last == "arch" { builtin =
  "tk_rt_arch"; has_builtin = true }`.
- Native N1 lowering is OPTIONAL: a corpus program calling `teko::arch()` under `--backend=native`
  honest-stops in `lower_native_call` (`src/lir/lower.tks:1085`) until wired — acceptable, the
  compiler itself does not run through N1.

**Seed sequencing (bootstrap law).** `native_target()` lives in `project.tks`, compiled BY the
seed. The seed must recognize `teko::arch()` before `project.tks` may CALL it. Therefore:
C2 lands `tk_rt_arch` + the two wirings (used by NO one yet) → seed bump → C3 makes
`host_target()` call `teko::arch()`. Do not collapse C2 and C3 across the bump.

### 2.3 The single supported-targets table (D39)

One source, replacing the enum-plus-scattered-strings duality. The `NativeTarget` enum stays (it is
the dispatch discriminant), but the OS×arch → variant mapping and the canonical/alias names become a
single `host_target()` composition plus one `supported_targets()` list used BOTH for lookup and for
the honest-error message.

```teko
/**
 * host_target — the NativeTarget for the build host, composed from teko::os() and teko::arch()
 * as the "<arch>-<os>" key (e.g. "x86_64-linux", "arm64-macos"), resolved through the single
 * supported-targets table (supported_targets). When the host os-arch is not a supported own-backend
 * target (e.g. arm64-linux before its lane lands, or an "unknown" arch), this is the honest stop
 * point for the DEFAULT build — the caller (native_target) surfaces target_error listing the set,
 * so an unbuildable host never silently mis-lowers to arm64/Mach-O.
 *
 * @return the host's own-backend target, or an error naming the unsupported host os-arch (D39/R2)
 * @throws when "<arch>-<os>" is absent from supported_targets
 * @since 0.3.1
 */
fn host_target(): NativeTarget | error { /* crumb C3 */ }

/**
 * supported_targets — the SINGLE source of truth (D39) for own-backend targets: the ordered list
 * of (canonical os-arch name, NativeTarget) pairs. Every other site — target_from_name lookup,
 * host_target composition, the R2 honest-error message, the M.3 tkp validation, and the regressor
 * target-matrix — reads THIS list; no string literals for targets live anywhere else.
 *
 * @return the canonical (name, variant) pairs, in display order for the honest-error message
 * @since 0.3.1
 */
fn supported_targets(): []TargetRow { /* crumb C3 */ }

/**
 * TargetRow — one row of the supported-targets table: the canonical os-arch name, its NativeTarget
 * dispatch variant, and its object format (used by the M.3 static-lib validator and by
 * check_object_wellformed to pick elf/macho/coff).
 *
 * @field name    the canonical "<arch>-<os>" key (e.g. "x86_64-linux")
 * @field variant the dispatch discriminant for emit_native
 * @field objfmt  the object format token ("elf" | "macho" | "coff")
 * @since 0.3.1
 */
type TargetRow = struct { name: str; variant: NativeTarget; objfmt: str }
```

`target_from_name()` (R2) becomes a table lookup with aliases folded in, returning an error (not the
arm64 fallback) on miss:

```teko
/**
 * target_from_name — map a raw TEKO_TARGET value (canonical name or a recognized alias, e.g.
 * "x86_64-elf" → x86_64-linux) to its NativeTarget via supported_targets.
 * REPLACES the pre-0.3.1 silent Arm64Macho fallback: an unsupported/typo value is now a hard error
 * (R2) whose message lists the supported set from supported_targets — no silent mis-lower.
 *
 * @param name the raw TEKO_TARGET value (or manifest [extern] target, canonical or alias)
 * @return the selected own-backend target, or an error listing supported_targets on an unknown name
 * @throws when name is neither a canonical supported name nor a known alias (R2)
 * @since 0.3.1
 */
fn target_from_name(name: str): NativeTarget | error { /* touches src/build/project.tks:1077 */ }

/**
 * native_target — the effective own-backend target: host_target() when TEKO_TARGET is unset (R1),
 * else target_from_name(value) (R2). REPLACES the unconditional Arm64Macho fallback at
 * project.tks:1109. The default build on a supported host resolves to that host's target
 * (mac → Arm64Macho, byte-identical FIXPOINT preserved); an unset target on an unsupported host, or
 * a set-but-unsupported value, surfaces the honest error rather than emitting a wrong-format object.
 *
 * @return the resolved target, or the R1/R2 honest error
 * @throws when the host os-arch (unset) or the requested value (set) is unsupported
 * @since 0.3.1
 */
fn native_target(): NativeTarget | error { /* touches src/build/project.tks:1108 */ }
```

Note the return-type change `NativeTarget → NativeTarget | error` ripples to `emit_native()`
(`:1179`), `default_cc()` (`:629`) and `default_cc_for_target` callers — each threads the error up to
the build's single honest-stop, not a `panic`. That ripple is part of C3's blast radius (flatten
with early-return guards per the Javadoc law).

---

## 3. Cross-emit: what changes in the pipeline (R3)

**Confirmed: nothing beyond objfile/ABI selection.** `lir::lower` produces a target-independent
`LModule`; `emit_native()` (`:1179`) dispatches on the `NativeTarget` to the per-ISA tail
(isel → regalloc → encode → objfile). Cross vs host differ ONLY in which tail runs and which link
driver (`default_cc_for_target`) is chosen. There is no host-arch assumption anywhere upstream of the
dispatch. So R3 is already structurally satisfied once `native_target()` is correct.

**What cross CANNOT do: run.** The build's own==C differential and any run-after-build must be SKIPPED
when target ≠ host — the host cannot execute a cross object/binary. This must be HONEST, not a
fabricated pass. Design:

```teko
/**
 * cross_note — whether the resolved target differs from the build host, and the human note the
 * build report carries when it does: the binary was EMITTED for <target> but NOT executed on host
 * <host> (R3/R5). Empty note when target == host. The build's run/differential step consults
 * is_cross to SKIP execution honestly (an emitted-not-run status), never to invent a green.
 *
 * @param target the resolved own-backend target
 * @return a record { is_cross: bool; note: str } for the build report
 * @since 0.3.1
 */
fn cross_note(target: NativeTarget): CrossNote { /* crumb C5 */ }
```

Reporting surface: a field on the build report, printed as an explicit line, e.g.
`cross: emitted x86_64-windows on host x86_64-linux — not executed`. This reuses the exact
honest-skip discipline the regressor already uses for absent run-wrappers
(`resolve_run_wrapper`, `docs/design/regressor-principal-0.3.1.md` §2g). A cross native build is,
for the run step: emitted, verified
well-formed (`check_object_wellformed`, §2f of that doc), NOT run.

---

## 4. TKP surface — the definition ALREADY EXISTS (ratify, do not redesign)

Per the owner ruling 2026-07-24, the A/B/C option menu is **CANCELLED**: the manifest surface for
per-target link deps is already legislated. This section CITES the existing spec and RATIFIES it; the
ONE genuine gap for cross-link (the arch dimension) is flagged separately in §4.2 as a spec-fill, not
a redesign.

### 4.1 The ratified surface (citations)

- **Static AND shared linkage vocabulary — TEKO_LEGISLATION.md:423** (verbatim):
  > "The `.tkp [extern.libs]` table maps that handle to a concrete link spec … Array-element
  > vocabulary: `[]` → `-l<key>` (a leading `lib` stripped); a bare name → `-l<name>`;
  > `static:<name>`/`shared:<name>` → forced static/dynamic linkage; a path (has `/` or a
  > `.a`/`.lib`/`.so`/`.dylib`/`.dll`/`.o` extension) → linked as a direct file (the extension
  > decides static vs shared); a `-flag` element → passed verbatim (the M.5 escape). `[extern]
  > prefer = "static"|"shared"` sets the default linkage; `[extern.search]` adds `-L` paths (per-OS;
  > a non-existent path is dropped softly, an explicitly-named missing lib file is a hard error); a
  > shared lib auto-emits an rpath for its resolved directory. … A missing symbol at link time
  > produces a fail-loud diagnostic naming the declaration and hinting `from "<lib>"`."

  Governing Law there: M.4 + M.2 + M.5. Re-ratified in **TEKO_MASTER_PLAN.md:463** (C7.0, RATIFIED
  2026-06-27). So `static:`/`shared:`, `prefer`, `[extern.search]`, the missing-file-is-hard-error
  default, and the fail-loud missing-symbol diagnostic are ALREADY law — this design consumes them.

- **Per-OS override — TEKO_LEGISLATION.md:426** (verbatim excerpt):
  > "a per-OS override in the `.tkp` (`[extern.symbols] host_write.windows = "_write"`,
  > `[extern.libs.libc] windows = ["ucrt"]`) keeps ONE portable Teko declaration."

  Implemented today as `[extern.libs.<os>]`, parsed at `src/build/manifest.tks:367-368` (the section
  name after `extern.libs.` becomes `sec_os`, matched at link time by `target_os`). The
  `static:`/`shared:` prefixes already parse at `src/build/manifest.tks:207-208` (mode currently
  stripped/deferred).

- **The os-arch key vocabulary — tkr-regression-format.md §6** (ratified): `on "<os-arch>"` routes
  into `[expect.<os-arch>]`/`[golden.<os-arch>]`; the target set (x86_64/arm64 linux, etc.)
  is the same vocabulary as `[platforms] targets` (TEKO_LEGISLATION.md:302) and `HOST_TARGET`
  (`src/build/manifest.tks:275`). This is the SINGLE os-arch string space (D39) the fill in §4.2
  reuses.

The design therefore does NOT invent a tkp surface. It: (a) STOPS stripping the `static:`/`shared:`
mode at `manifest.tks:207-208` (the ruling needs the mode preserved — §5), and (b) applies the
ratified `[extern.libs.<os>]` + `static:`/`shared:` surface to the cross-link case.

### 4.2 Arch dimension for `[extern.libs]` — spec-fill RATIFIED (owner, 2026-07-24)

> **RATIFIED (owner, 2026-07-24):** "Gap de spec-fill no tkp aceito" — the `[extern.libs.<key>]`
> section key now accepts the full os-arch form (e.g. `x86_64-linux = [...]`) in addition to the bare
> OS, exactly as proposed here. This is a refinement of the already-ratified per-OS section
> (LEGISLATION:426) onto the already-ratified os-arch vocabulary (tkr-regression-format.md §6) — not
> a redesign, not a menu of options.

The legislation's per-target `.tkp` override was spelled per-**OS** only (`.windows`, `.libc`).
Cross-link to a specific os-arch (the ruling's "arquitetura, não apenas SO") needs the section key to
carry the ARCH too. The os-arch vocabulary is ALREADY ratified (tkr §6) — this fill wires it into the
`extern.libs` section key: the `[extern.libs.<key>]` section key is now either a bare OS (existing
meaning: any arch of that OS) OR a full os-arch from the ratified vocabulary:

```toml
[extern.libs]                       # all targets (LAW :423 — unchanged)
sqlite3 = ["shared:sqlite3"]

[extern.libs.linux]                 # any linux arch (LAW :426 — unchanged, back-compat)
m = ["shared:m"]

[extern.libs.x86_64-linux]          # SPEC-FILL: os-arch key (ratified vocabulary, tkr §6)
foo = ["static:vendor/x86_64-linux/libfoo.a"]

[extern.libs.arm64-macos]
foo = ["static:vendor/arm64-macos/libfoo.a"]
```

Parser delta is minimal: `sec_os` after `extern.libs.` already captures `x86_64-linux` verbatim;
link-time resolution gains an exact os-arch match ALONGSIDE the existing bare-OS match. No new TOML
shape, no new vocabulary, no competing surface. This fill is the entire net-new manifest work; it is
flagged as a spec-fill for owner ratification, separate from the ratified §4.1 surface.

### 4.3 M.3 validation (compile-time, not ld-time) — R4

```teko
/**
 * validate_static_libs_for_target — M.3: for the resolved target, every static: lib declared for
 * that os-arch ([extern.libs.<os-arch>] §4.2, or a bare-OS section matching the target's OS) must
 * resolve to a file present LOCALLY; a requested target whose static lib is absent/unpointed is a
 * COMPILE error naming the lib, the target, and the missing path (R4) — surfaced HERE, never
 * deferred to an opaque ld failure. This is the cross-link specialization of the ALREADY-RATIFIED
 * "an explicitly-named missing lib file is a hard error" rule (LEGISLATION:423). Shared: libs follow
 * the §5 fail-or-`--allow-undef` rule, not this one. No-op when the target has no static: deps.
 *
 * @param m      the parsed manifest ([extern.libs.<os-arch>] + bare-OS sections)
 * @param target the resolved own-backend target
 * @return ok, or the honest compile error naming the first missing (lib, target, path)
 * @throws when a static: lib for target is not pointed-at or not present locally (R4/M.3)
 * @since 0.3.1
 */
fn validate_static_libs_for_target(m: Manifest, target: NativeTarget): error? { /* crumb C4 */ }
```

Manifest struct delta (`src/build/manifest.tks:31` type `Manifest`, and the builder `:311+`): the
existing `os_lib_os`/`os_lib_flag`/`os_lib_name` parallel arrays generalize from an OS key to an
os-arch key (the OS-only sections store their bare OS, matched as "any arch of that OS"; the new §4.2
sections store the full os-arch). A `os_lib_mode: []str` parallel column records `static`/`shared`
(today stripped at `:207-208` — the ruling requires it PRESERVED) so the linker step and M.3 can
distinguish the R4 static-locality rule from the §5 shared fail-or-`--allow-undef` rule.

---

## 5. Shared/dynamic without a local lib — default FAIL, `--allow-undef` opt-in (R5)

### 5.0 Owner ruling registered (verbatim, 2026-07-24)

> "SHARED SEM LIB LOCAL: o default é FALHAR se a lib não for encontrada no link; o dev ou fornece a
> lib, ou passa `--allow-undef` (flag NOSSA, do linker Teko — no mundo transitório o teko traduz
> para o equivalente do system ld: --allow-shlib-undefined/-undefined dynamic_lookup/etc.; no E1 é
> nativa) para habilitar o link 'às cegas' explícito. Nunca blind-link silencioso — opt-in, M.3."

This SUPERSEDES the earlier draft that blind-linked shared cross by default. It is CONSISTENT with
the already-ratified LEGISLATION:423 ("an explicitly-named missing lib file is a hard error" + a
fail-loud missing-symbol diagnostic): the DEFAULT is to fail loudly; `--allow-undef` is the sole,
explicit escape.

### 5.1 Semantics

- **Default (no flag).** A `shared:<name>` (or `.so`/`.dylib`/`.dll`) whose target library is not
  found at link FAILS with a fail-loud diagnostic naming the declaration and `from "<lib>"` (the
  LEGISLATION:423 shape). Never a silent blind link.
- **`--allow-undef` (opt-in, OUR flag).** An explicit Teko-linker flag that enables the "blind" link:
  the shared reference is emitted with its symbols left undefined-at-link, resolved at load time on
  the target. Opt-in only, M.3 (the dev states the intent; the tool never assumes it).

### 5.2 Transitional translation (system `cc`/`ld`), by object format

`--allow-undef` translates to the platform equivalent under the transitional system linker; in E1 it
becomes a native own-linker capability (no host flag):

- **ELF (linux).** `--allow-undef` → `-Wl,--allow-shlib-undefined`. `shared:<name>` → `-l<name>`
  (`DT_NEEDED`); soname is a runtime concern, not the link.
- **Mach-O (macos).** `--allow-undef` → `-Wl,-undefined,dynamic_lookup`. `shared:<name>` →
  `-l<name>`.
- **PE/COFF (windows).** HONEST GAP even WITH `--allow-undef`. COFF cannot reference a DLL without an
  **import library** (`.lib`/`.a` import stub) or a generated import table; system
  `link.exe`/`lld-link` will not invent one. So on `x86_64-windows` cross, `shared:` requires the
  import lib pointed-at in the manifest regardless of `--allow-undef`; otherwise it is a NAMED
  honest-stop with a clear message until E1. Do NOT fabricate an import table in the transitional
  path.

### 5.3 E1 future (post-transitional)

When the own M-linker writes `DT_NEEDED` (ELF) / `LC_LOAD_DYLIB` (Mach-O) / the PE import table
directly, `--allow-undef` becomes native and the COFF gap closes (the own linker synthesizes the
import table from the declared soname/import name). This doc scopes only the transitional mechanics;
the E1 handoff is a named forward reference.

### 5.4 How `.tkp` declares shareds

Using the ratified surface (§4.1) plus the §4.2 arch-key fill: `shared:<name>` inside `[extern.libs]`
(all targets, LAW :423) or `[extern.libs.<os-arch>]` (per target, §4.2). The NAME is the link name;
per-target sonames scope naturally by the per-os-arch section. A single global `shared:<name>`
suffices when the link name is stable across targets (the common case); COFF's import-lib name goes
in the `[extern.libs.x86_64-windows]` section.

---

## 6. Crumb sequence

Each crumb is independently gate-able; ritual (full gate) points marked.

- **C1 (S, URGENT — own vagão, ships immediately, NO seed bump). DONE (2026-07-24, feat/0.3.1-target-host-default).**
  Replaced the `native_target()` unset-fallback with an os-only host default via `teko::os()`
  (`linux→X8664Linux`, `macos→Arm64Macho`, `windows→X8664Windows`, `host_target_for_os`/
  `host_default_target`, `src/build/project.tks`). Fixed the reproduced bug (verified: linux-host
  default now emits ELF x86_64, `scripts/target_host_default_test.sh`). Widened this vagão's scope
  by owner instruction to ALSO land `target_from_name`'s R2 honest error (unsupported `TEKO_TARGET`
  now names the value + lists the supported set, `unsupported_target_error`, threaded through
  `native_target`/`emit_native`/`emit_static_lib`/`resolve_cc` as `NativeTarget | error`) and the
  separate owner ruling removing the `--backend` CLI flag (`TEKO_BACKEND` is now the sole
  code-generation-backend seam; `has_backend_flag` rejects a stray `--backend` honestly, pointing at
  `TEKO_BACKEND`). `supported_targets()`/`TargetRow`/`host_target()` (the precise os×arch table) stay
  C3 scope, gated on the `teko::arch()` seed bump (C2) as planned. **RITUAL: full gate green
  (build/test/fixpoint/diff_c_own/W15) + the new fixture proving linux-host default emits ELF x86_64
  (§7, T1).** FIXPOINT on mac unchanged (verified).
- **C2 (M, seed-gated). DONE (2026-07-25, feat/0.3.1-target-crosslink).** `tk_rt_arch()` added to
  `src/runtime/teko_rt.c` (beside `tk_rt_os`) + declared in `teko_rt.h`; `teko::arch()` wired in the
  checker (`scope.tks::builtin_fn`, beside the `os` arm) and the codegen builtin map
  (`codegen.tks`, `else if last == "arch" { builtin = "tk_rt_arch" }`). Called by NO ONE in the
  corpus — deliberately, so the seed ladder stays two rungs (this vagão + the .32 seed that carries
  it). Verified end-to-end on a scratch project built by gen1 (`teko::arch()` → `"x86_64"`).
  **Then seed bump.**
- **C3 (M, after the C2 bump). NOT in the C2/C4/C5/C6 vagão — deferred to .32 by integrator ruling
  (2026-07-25).** Collapsing C2 and C3 into one vagão is forbidden by the bootstrap law (§8), and
  landing C3 alone in .31 would add a THIRD rung to the .31 seed ladder for no gain: C4/C5/C6 do not
  depend on it. They ride C1's os-only host default, which is already precise on linux/x86_64,
  windows/x86_64 and macos/arm64 (the overwhelming majority); the ONE imprecision it leaves — a
  same-OS different-arch host (arm64-linux, x86_64-macos) reading an explicit same-OS cross request as
  NOT cross — is documented at `cross_note` and is the conservative direction (the host runs it and
  reports the real exit, rather than fabricating a skip). In the .32 the .31 seed already carries
  `teko::arch()`, so C3 becomes a plain refactor. C3 still owns
  `supported_targets()`/`TargetRow`/`host_target()`; the C4/C5/C6 vagão landed only the two
  reverse-direction columns it could not do without — `target_name` (variant → canonical os-arch) and
  `target_objfmt` — as standalone matches for C3 to fold into `TargetRow`.
- **C4 (M-L). DONE (2026-07-25, feat/0.3.1-target-crosslink).** Ratifies §4.1 and fills §4.2: the
  `[extern.libs.<os-arch>]` section key was ALREADY captured verbatim by the parser (now pinned by a
  test), so the delta is at LINK time — `os_lib_key_matches` over `LinkTargetKeys`
  (`build_os`/`emit_os`/`os_arch`), the ONE applicability rule now shared by the link line
  (`link_target_keys`) and the M.3 validators (`target_link_keys`). **Second defect found and fixed
  in-crumb:** a bare-OS section was matched ONLY against `target_os`, which answers the HOST OS when
  no `[extern] target` triple is set — so cross-linking to `x86_64-windows` on linux silently dropped
  `[extern.libs.windows]`, contradicting §4.2's own "any arch of that OS". The rule now also matches
  the OS being linked FOR; additive by construction (every key that matched before still matches), and
  `target_os`/`#os()` pruning are deliberately untouched (moving those changes conditional-compilation
  semantics for cross builds — a separate decision). The `static:`/`shared:` mode is no longer
  stripped: `mf_extern_spec` returns `ExternLibSpec { flag; mode }` into the new
  `link_mode`/`os_lib_mode` columns. **Bug found and fixed in-crumb:** `mf_extern_flag` tested for a
  path BEFORE stripping the mode prefix, so the ratified `static:vendor/x86_64-linux/libfoo.a`
  spelling leaked the literal `static:` prefix onto the `cc` line.
  `validate_static_libs_for_target` implements R4/M.3.
- **C5 (M). DONE (2026-07-25, feat/0.3.1-target-crosslink).** `CrossNote`/`cross_note` +
  `cross_note_for_name` (the name-keyed door) + `resolved_cross_note`. `teko run` and the native test
  gate print the emitted-not-run line and skip the child process when cross; the regression runner
  consults the SAME `cross_note_for_name` (`tkr_row_run_verdict`) instead of keeping a second idea of
  "cross", and `host_cc_cannot_link_cross_reason` closes the pre-build hole the vagão-17 probe left
  (a cross target with no dedicated cross driver reached the host `ld` and failed on the object
  format, indistinguishably from a compiler regression).
- **C6 (S). DONE (2026-07-25, feat/0.3.1-target-crosslink).** `--allow-undef` (`ALLOW_UNDEF_FLAG`,
  `allow_undef_of`/`allow_undef_selected`, skipped by `project_arg_of`, listed in `teko --help`) +
  `allow_undef_cc_flag` per objfmt + `omit_flag_for_blind_link` (a named-but-absent `shared:` path is
  withdrawn from the link line under the opt-in — otherwise `ld` fails on the missing file and the
  opt-in is a no-op) + the DEFAULT fail-loud (`validate_shared_libs_for_target`) + the COFF
  import-lib named honest-stop. **§5.2's ELF row is incomplete and was corrected in-crumb:**
  `--allow-shlib-undefined` alone governs undefined symbols INSIDE a shared dependency, so an
  EXECUTABLE with an undefined symbol still fails `ld` (verified with `cc` on linux/x86_64). The ELF
  translation is therefore the PAIR `-Wl,--allow-shlib-undefined,--unresolved-symbols=ignore-all`;
  emitting only the ratified flag would have shipped a no-op opt-in, which M.1/M.3 forbid more
  strongly than a flag list binds. Mach-O's `-undefined dynamic_lookup` needed no correction.

Blocked/forward: E1 (own-linker DT_NEEDED/import table) closes the COFF-shared-cross gap — NOT in
scope here; named forward reference only.

---

## 7. Fixtures / regressors (target × artifact)

The regressor's target-matrix machinery already designed in
`docs/design/regressor-principal-0.3.1.md` COVERS this — cite and populate, do not invent a new
harness:

- **F5 backends (env/target)** (that doc §3, table row F5): `Given target = "<os-arch>"` sets
  `TEKO_TARGET` and selects the run wrapper (§2g), honest-skipping the RUN when the wrapper/host
  cannot execute. Cross native emit-not-run lands here.
- **`Given target` + honest os-arch skip** (§2g, `resolve_run_wrapper`): the exact mechanism C5's
  cross emitted-not-run reuses.
- **`Then object well-formed`** (§2f, `check_object_wellformed`, elf/macho/coff by target): the
  post-build check that a cross object is the RIGHT format — this is the direct regressor for the
  motivating bug (a linux-host default MUST produce ELF, asserted by `check_elf`, not Mach-O).

New scenarios to add (inputs → expected exit, LIR oracle and native where applicable):

| # | scenario | target/env | expect |
|---|---|---|---|
| T1 | default build, no TEKO_TARGET, linux host | unset | build ok; object well-formed = ELF x86_64 (C1 regressor) |
| T2 | default build, no TEKO_TARGET, macos host | unset | build ok; object well-formed = Mach-O arm64 (FIXPOINT) |
| T3 | TEKO_TARGET=x86_64-linux, run | set, native host | exit 42 (the reproduced-good path) |
| T4 | TEKO_TARGET=bogus-arch | set | COMPILE FAIL; diagnostic LISTS supported_targets (R2) |
| T5 | TEKO_TARGET=x86_64-windows on linux host | set, cross | emit ok + object well-formed = COFF; run SKIPPED honest (R3) |
| T6 | cross static: lib for target not present | set, cross | COMPILE FAIL naming (lib, target, path) (R4/M.3) |
| T7a | cross shared: lib absent, NO --allow-undef (ELF) | set, cross | COMPILE/LINK FAIL, fail-loud naming decl + `from` (R5 default) |
| T7b | cross shared: lib absent, WITH --allow-undef (ELF) | set, cross, --allow-undef | emit + link ok (blind); run SKIPPED honest (R5 opt-in) |
| T8 | cross shared: on x86_64-windows, no import lib (even with --allow-undef) | set, cross | honest error / named stop (R5 COFF gap) |

LIR oracle vs native: T1-T3 assert absolute exit on both engines where the snippet runs; T4/T6/T8 are
compile-fail (engine-independent, diagnostic-pinned per the F1 inversion); T5/T7 assert emit + object
well-formed with an honest RUN-skip label.

### 7.1 Where each fixture actually landed (C2/C4/C5/C6 vagão, 2026-07-25)

The corpus rule changed under this design: the .31 regressor is `regressor.tkr` (the 234 fixture
directories were folded into 10 `.tkr` files, owner target ≤10 — **no new directory under
`examples/regressions/`**), so a new case is a `Scenario`/`Scenario Outline` in `regressor.tkr` or it
is a `#test`. The split below is forced by ONE mechanical fact: a `.tkr` scenario's project manifest
is SYNTHESIZED (`snippet_manifest_text`) and the grammar has no `Given manifest` noun, so a case whose
input IS a `[extern.libs…]` table cannot be a `.tkr` scenario at all. Those cases are `#test`s over
the validators — which the format doc itself prefers ("the reframe routes MOST compile-fail
diagnostics to CHECKER `#test`s (stronger than a diagnostic-less regressor)", §5.1).

| # | landed as | where |
|---|---|---|
| T1 | `scripts/target_host_default_test.sh` (C1, unchanged) | the one harness that leaves `TEKO_TARGET` unset |
| T2 | same script, macOS arm (host-conditional arm) | + FIXPOINT itself |
| T3 | `regressor.tkr` F5 `own_explicit_host_os_arch_runs` | runs for real on x86_64-linux; honest-skips elsewhere (`host_cc_cannot_link_cross_reason`) |
| T4 | `regressor.tkr` **new Feature F8** (3 compile-fail scenarios: the phrase, the offending value, a canonical name in the list) | diagnostic-pinned |
| T5 | `regressor.tkr` F5 `own_cross_x86_64_windows_emits_coff_and_skips_the_run` | emit + `Then object well-formed` (COFF) + honest RUN-skip |
| T6 | `#test` `pt_static_lib_cross_must_be_pointed_at_and_present` + `pt_static_validation_honours_the_section_key` (`project_test.tkt`) | needs a `[extern.libs]` manifest |
| T7a | `#test` `pt_shared_lib_default_fails_and_allow_undef_opts_in` (strict arm) | idem |
| T7b | same `#test` (opt-in arm) + `pt_blind_link_withdrawal_is_opt_in_only` | idem |
| T8 | `#test` `pt_shared_cross_coff_needs_an_import_lib` | idem |

Also pinned by `#test`: the §4.2 os-arch section key and the preserved mode
(`extern_libs_section_key_accepts_a_full_os_arch`, `global_extern_libs_carry_their_mode_column`,
`mf_extern_spec_*`, `manifest_test.tkt`), the link-time key match (`pt_os_lib_key_matches_both_shapes`),
the target/objfmt tables (`pt_target_name_and_objfmt_are_one_source`), the cross classification
(`pt_cross_note_is_empty_for_the_host_target`), the flag surface + translation
(`pt_allow_undef_flag_surface`) and the runner's new link-capability probe
(`host_cc_cannot_link_cross_reason_names_the_unlinkable_cross`, `regression_test.tkt`). The os-arch
KEY SELECTION was additionally proven end-to-end by hand on gen1 (`[extern.libs.x86_64-linux] nope`
puts `-lnope` on the link line — `cannot find -lnope`; `[extern.libs.arm64-macos] nope` does not —
`undefined reference`; `[extern.libs.linux] nope` still does).

**Reported gap (no product code missing):** the `.tkr` grammar cannot express a scenario whose input
is a manifest section. Adding a `Given manifest = """…"""` noun would let T6/T7/T8 be end-to-end
regressor scenarios instead of validator `#test`s. That is a REGRESSOR-FORMAT change (grammar + lower
+ `Tkr` field + the format doc), out of this design's scope — flagged for the integrator.

---

## 8. Risks + law tensions

- **Seed sequencing (bootstrap law).** `teko::arch()` cannot be USED by the compiler until the seed
  recognizes it. Mitigated by the C1(os-only)/C2(land builtin)/bump/C3(use it) split. Risk if
  collapsed: `project.tks` fails to compile under the old seed. Resolution: keep C2 and C3 across the
  bump; C1 is deliberately arch-free so the URGENT fix needs no bump.
- **FIXPOINT on mac.** C1/C3 must keep the mac-host default at `Arm64Macho` so the arm64 differential
  and gen1==gen2 stay byte-identical. The os-only heuristic and `host_target()` both map macos→arm64
  by construction. Verified by fixture T2.
- **Return-type ripple (`NativeTarget | error`).** C3 widens `native_target`/`target_from_name`;
  every caller must thread the error to the build's single honest-stop, not `panic`. Blast radius is
  `emit_native`/`default_cc`/`default_cc_for_target` callers — enumerated, bounded, flatten with
  guards.
- **COFF shared cross (R5 mechanical limit).** The transitional system linker CANNOT reference a DLL
  without an import lib, even with `--allow-undef`. Genuine mechanical limit, NOT a law tension:
  resolved by requiring the import lib pointed-at on `x86_64-windows` and treating an absent one as a
  named honest-stop until E1 (own import-table writer). Consistent with the ruling (opt-in blind link
  is still bounded by what the transitional linker can physically do). No owner HALT needed —
  ratifiable under "honest-stop over fabricated capability".
- **`static:`/`shared:` mode preservation.** The ruling requires the linkage MODE (today stripped at
  `manifest.tks:207-208`) to survive so the default-fail-vs-`--allow-undef` and R4-vs-R5 splits can
  be made. C4 must preserve it (`os_lib_mode`). Low risk; bounded to the manifest builder.

No genuine unresolved law tension remains. Nothing HALTs. All decisions are CLOSED (owner,
2026-07-24): R1-R5 (§0) plus the §4.2 arch-key spec-fill (RATIFIED) — the latter a refinement of the
already-ratified per-OS section onto the already-ratified os-arch vocabulary, not a redesign and not
a menu of options.

---

## 9. Files touched (map for implementers)

- `src/build/project.tks` — `native_target` (:1108), `target_from_name` (:1077), the new
  `host_target`/`supported_targets`/`TargetRow`, `cross_note`/`CrossNote`, the link-arg builder
  (shared flags), and the `NativeTarget | error` ripple through `emit_native` (:1179)/`default_cc`
  (:629).
- `src/runtime/teko_rt.c` / `.h` — `tk_rt_arch` (maintained-C exception), mirroring `tk_rt_os`
  (:1754 / :419).
- `src/checker/scope.tks` — `arch` builtin signature (beside :450).
- `src/codegen/codegen.tks` — `arch → tk_rt_arch` (beside :3036).
- `src/build/manifest.tks` — `Manifest` type (:31), the builder (:311+), `parse_extern_libs_entry`
  (:237) for os-arch sections + `os_lib_mode`, `validate_static_libs_for_target`.
- `regressor.tkr` (per `docs/design/regressor-principal-0.3.1.md` §3, F5/§2f/§2g) — T1-T8.
