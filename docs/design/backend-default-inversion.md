# Backend default inversion — `Backend::Native` becomes the default; C becomes an explicit pin

**Status:** DESIGN + SITE AUDIT (doc-only). Implements the **owner ruling 2026-07-14**.
Grounded in the flag world `fix/issue-390-d1-backend-flag` (the D1/#390 `--backend={c,native}`
flag, the "#569" work) — every `file:line` below is against that branch's tree.

> **Deliverable of record.** This doc is the no-gaps audit of every site that must pin
> `--backend=c` after the default flips to native, plus the source crumb sequence, the retire
> ladder, and the verification plan. It writes NO product code.

---

## 0. The ruling and its provenance (counter-argue §4 — record with attribution)

**Owner ruling 2026-07-14.** The default code-generation backend inverts to **`Backend::Native`**.
C becomes **opt-in** via **`--backend=c`** (both spellings `--backend=c` and `--backend c`), and
that opt-out MUST be pinned on every site that compiles the compiler itself or that the seed
chain / fixpoint depends on — self-host, release, fixpoint, and C-reference sites — **until the
own self-host fixpoint is green** (own-backend-architecture §2.4 criterion 2). The user-facing
default (`teko build <proj>`, `teko run`) dogfoods native; the compiler's OWN bootstrap stays on
C via explicit pins. The pin-list is migration debt that shrinks to zero as native self-hosts.

**Supersession (dated, not deleted).** The prior "default `Backend::C` until retirement" was an
**integrator-pinned recommendation** "collected at N8/#225" in `own-backend-architecture.md`
§2.4/§3.7 — **not** an owner ruling. This ruling supersedes that recommendation. The old text is
marked `SUPERSEDED 2026-07-14`, not removed.

**Consistency with D37.** `DECISION_LOG.md` D37 (owner, 2026-07-12/13, "VM Retirement — native
becomes the sole engine") already ruled native the sole *execution* engine. This inversion makes
native the default *codegen backend* too — a continuation of D37, not a reversal.

**§2.4 criterion 2 is HONORED, not violated.** "Own backend self-hosts (gen1==gen2==gen3
byte-identical through the own path)" remains an unmet bar. The inversion does not pretend it is
met: it makes the bar EXPLICIT as the `--backend=c` pin-list on the self-host / release / fixpoint
sites. When criterion 2 goes green those pins drop (the retire ladder, §4). Until then the
fixpoint stays proven on C, exactly as today — only the DEFAULT moves, never the trusted chain.

---

## 1. Why the inversion is safe iff the pin-list is complete

The native backend does **not** self-host the whole compiler yet:

- It links via the system `cc`/cross-`gcc` as linker (M-linker phase E, #392–394, not landed;
  own-backend-architecture §3.6, D-5).
- The own self-host fixpoint (gen1==gen2==gen3 through the own path) is UNMET (§2.4 crit. 2).
- `scripts/diff_c_own.sh` proves own==C only on the small `examples/regressions/` **corpus**, not
  on the whole compiler; the corpus is a subset of the language the compiler itself uses.

Therefore: **flipping the default is safe only if every invocation that compiles the compiler
itself, or that the seed chain / fixpoint depends on, pins `--backend=c`.** A single missed
self-host / release site = a broken or miscompiled seed. That is a **release-breaking defect**,
not a test flake. The rest of this doc is the complete pin-list.

---

## 2. The exhaustive C-pin site audit

Classification: **(a)** MUST pin `--backend=c` now (self-host / release / fixpoint / C-reference);
**(b)** becomes native-default or explicit `--backend=native` (user-facing / own differential leg);
**(c)** already C-by-construction or unaffected (no change).

### 2.1 `.github/workflows/release.yml` — CRITICAL (miss ⇒ every release breaks)

The release builds THREE self-host generations and gates on `gen2/teko.c == gen3/out/teko.c`.
Native emits **no `teko.c`** — a missed pin makes the `cmp` fail (no file) and starves
`cross_compile_linux.sh` of its only input.

| file:line | invocation | class | why |
|---|---|---|---|
| `release.yml:138` | `teko . -o gen1 --no-verify` | **(a)** | seed→gen1 self-host |
| `release.yml:139` | `./gen1/teko . -o gen2 --no-verify` | **(a)** | gen1→gen2 self-host |
| `release.yml:140` | `./gen2/teko . -o out --no-verify` | **(a)** | gen2→gen3; **must emit `out/teko.c`** |
| `release.yml:141` | `cmp gen2/teko.c out/teko.c` | (a-consequence) | fixpoint gate reads `teko.c` — only exists on the C path |
| `release.yml:211` | `teko . -o gen1 --no-verify` (macOS/Win) | **(a)** | seed→gen1 self-host |
| `release.yml:212` | `./gen1/$BIN . -o gen2 --no-verify` | **(a)** | gen1→gen2 self-host |
| `release.yml:213` | `./gen2/$BIN . -o out --no-verify` | **(a)** | gen2→gen3; emits `teko.c` |
| `release.yml:214` | `cmp gen2/teko.c out/teko.c` | (a-consequence) | fixpoint gate |

### 2.2 `.github/workflows/native.yml`

| file:line | invocation | class | why |
|---|---|---|---|
| `native.yml:133` | `teko . -o bin` (build-test, primary) | **(a)** | seed→gen1 self-host (the release-binary codegen tail runs native by default) |
| `native.yml:135` | `teko . -o bin --no-verify` (build-test, secondary) | **(a)** | seed→gen1 self-host |
| `native.yml:182` | `teko . -o bin --no-verify` (riscv gen1) | **(a)** | seed→gen1 self-host (gen1 carries the riscv own backend, but the BUILD of gen1 is a self-host) |
| `native.yml:196` | `./scripts/diff_c_own.sh` (riscv) | (script) | see §2.4 — legs pinned inside the script |
| `native.yml:269` | `teko . -o bin` (gen1-checks) | **(a)** | seed→gen1 self-host |
| `native.yml:273` | `./bin/teko test .` | **(c)** | `test_project`→`run_native_gate`→`tk_emit_c_test`+`run_cc`: **C-by-construction**, ignores `m.backend` (verified `project.tks:1342`) |
| `native.yml:277` | `cli_flags_test.sh ./bin/teko` | **(c)** | `--version`/`--help`/no-args only; no build |
| `native.yml:281` | `fmt_cli_test.sh ./bin/teko` | **(c)** | fmt path; no backend codegen |
| `native.yml:285` | `region_drop_subtree_test.sh` | **(c)** | C runtime unit test |
| `native.yml:294` | `native_regressions.sh` (TEKO=./bin/teko) | (script) | see §2.4 |
| `native.yml:308` | `diff_c_own.sh` | (script) | see §2.4 |
| `native.yml:353-359` | `validate_wasm_own.sh` | (script) | see §2.4 |
| `native.yml:396` | `teko . -o out --no-verify` (release-cross-smoke, emit `teko.c`) | **(a)** | **must emit `teko.c`** for `cross_compile_linux.sh` |
| `native.yml:414` | `./gd-linux-x86_64-glibc/teko . -o /tmp/xrun --no-verify` | **(a)** | the cross artifact self-compiles (self-host miscompile guard) |

### 2.3 `.github/workflows/sanitizers.yml`

The whole ASan/TSan value is sanitizing the **generated C + the `cc` link seam** through a `cc`
wrapper. Native emits no C and bypasses `cc` entirely, so an unpinned build under these lanes
sanitizes **nothing**. All builds here pin C.

| file:line | invocation | class | why |
|---|---|---|---|
| `sanitizers.yml:117` | `teko . -o bin $VERIFY_FLAG` (ASan gen1) | **(a)** | self-host + must route through the sanitized `cc` wrapper |
| `sanitizers.yml:146` | `./bin/teko . -o gen2-asan $VERIFY_FLAG` | **(a)** | gen1→gen2 self-host, sanitize the emitted C |
| `sanitizers.yml:166` | `./bin/teko build "$proj" -o …` (regressions under ASan) | **(a)** | intent is to sanitize the C-emit path (`tk_emit_c`/`tk_emit_tsym` UAF history); native emits no C |
| `sanitizers.yml:228` | `teko . -o bin $VERIFY_FLAG` (TSan gen1) | **(a)** | self-host + sanitized `cc` |
| `sanitizers.yml:247` | `./bin/teko build "$proj" -o "$out"` (TSan smoke) | **(a)** | comment fixes the path as "front-end → C → host cc → execute"; pin C to keep that coverage + byte-identical exit 5 |
| `sanitizers.yml:306` | `teko . -o bin --no-verify` (windows-selfhost gen1) | **(a)** | self-host |
| `sanitizers.yml:312` | `diff_c_own.sh` (windows) | (script) | see §2.4 |

**Stale reference to flag:** the task brief names a `mem-paranoid` job (`./bin/teko . -o gen2-mp`).
No such job exists on this branch — `sanitizers.yml` has exactly `asan-default`, `tsan`,
`windows-selfhost`. If a `mem-paranoid` lane is re-introduced, its self-host build is class **(a)**.

### 2.4 Differential scripts — the C leg INVERTS, the own leg keeps native

These scripts build each corpus fixture twice and diff. **The C leg forces C today by
`unset TEKO_BACKEND` — which after the flip resolves to native and silently breaks the trusted
side of the differential.** Every C leg must switch from `unset TEKO_BACKEND` to an explicit
`--backend=c` pin. Every on-the-fly `./bin/teko` self-host inside these scripts is class (a).

**`scripts/diff_c_own.sh`**

| file:line | invocation | class | change |
|---|---|---|---|
| `diff_c_own.sh:178` | `"$builder_abs" build "$script_dir" -o "$script_dir/bin"` (self-host fallback) | **(a)** | add `--backend=c` |
| `diff_c_own.sh:296` | `( unset TEKO_BACKEND; env $TEKO_CC_ENV "$teko_abs" build "$proj" -o "$cout" )` — the **C-native leg** | **(a)** | replace `unset TEKO_BACKEND` with `--backend=c`; the trusted reference side MUST be C |
| `diff_c_own.sh:306` | `env TEKO_BACKEND=native $OWN_TARGET_ENV … build "$proj" -o "$oout"` — the **own-native leg** | **(b)** | migrate `TEKO_BACKEND=native` → `--backend=native`; keep `$OWN_TARGET_ENV` (`TEKO_TARGET`); keep the `"(own backend)"` marker assertion (`:324`) |

**`scripts/validate_wasm_own.sh`**

| file:line | invocation | class | change |
|---|---|---|---|
| `validate_wasm_own.sh:105` | self-host fallback build | **(a)** | add `--backend=c` |
| `validate_wasm_own.sh:306` | `( unset TEKO_BACKEND; "$teko_abs" build "$proj" -o "$cout" )` — C-native leg | **(a)** | replace `unset TEKO_BACKEND` with `--backend=c` |
| `validate_wasm_own.sh:346` | `env TEKO_BACKEND=native TEKO_TARGET="$target" … build …` — own-wasm leg | **(b)** | migrate to `--backend=native`; keep `TEKO_TARGET` |

**`scripts/native_regressions.sh`** (issue #64 CWD/rt_dir regression)

| file:line | invocation | class | change |
|---|---|---|---|
| `native_regressions.sh:126` | `"$bootstrap_abs" build "$script_dir" -o "$script_dir/bin"` (self-host fallback) | **(a)** | add `--backend=c` |
| `native_regressions.sh:55` | `( cd "$CWD_REGRESSION_FIXTURE" && … build . -o "$out" )` | **(a)-conservative** | fixture defaults to `examples/regressions/char_ops` (`:26`); this check validates CWD/`TK_RT_DIR` probing, orthogonal to codegen, and runs on BOTH the previous-release seed and gen1. Pin `--backend=c` to keep it deterministic + byte-identical. **FLAG:** if `char_ops` is confirmed in the native-covered set, this could stay (b); pinning C is the safe default. |

**`scripts/install_share_runtime_test.sh`** (standalone; not wired into a CI workflow today)

| file:line | invocation | class | change |
|---|---|---|---|
| `install_share_runtime_test.sh:55` | `"$TEKO_BINARY" . -o "$GEN_DIR/emit"` (emits `teko.c`, asserted at `:57`) | **(a)** | must emit `teko.c`; pin `--backend=c` |
| `install_share_runtime_test.sh:126` | installed binary builds `exit_success_path`, validating the `share/teko` **C-runtime staging** (`teko_rt.c`) | **(a)** | the test IS the C-runtime link path; pin `--backend=c` |

### 2.5 Class (c) — already C-by-construction / unaffected (no change)

| file:line / file | why no change |
|---|---|
| `project.tks:1342` `run_native_gate` (`teko test`, and the D4 build gate) | emits `tk_emit_c_test` + `run_cc`; never reads `m.backend`. `teko test` and the gate leg of `teko build` stay C-by-construction. **Verify sustained** as native matures — if the gate ever gains a backend switch, revisit. |
| `scripts/ci_provision_teko.sh`, `scripts/fetch_teko.sh` | download released binaries only; invoke no build. The seed they place is used by workflow steps that pin at their own call sites. |
| `scripts/cross_compile_linux.sh` | consumes `teko.c` via `zig cc`; builds no compiler. Unchanged — but **depends** on the upstream C-emit pins (`release.yml:140`, `native.yml:396`). |
| `scripts/package_release.sh` | packages the built binary + the bootstrap `teko.c`; no compiler build. |
| `scripts/ci_cc_wrap.sh` | writes a `cc` wrapper; invokes no `teko`. |
| `scripts/cli_flags_test.sh`, `scripts/fmt_cli_test.sh`, `scripts/region_drop_subtree_test.sh` | CLI/fmt/runtime-unit smokes; no backend codegen of the compiler. |

### 2.6 Pin-count summary

- **Class (a) — MUST pin `--backend=c` now:** `release.yml` ×6 build steps (+2 `cmp` consequences),
  `native.yml` ×6, `sanitizers.yml` ×6, `diff_c_own.sh` ×2 (self-host + C leg), `validate_wasm_own.sh`
  ×2, `native_regressions.sh` ×2, `install_share_runtime_test.sh` ×2. **= 26 build sites.**
- **Class (b) — own leg → `--backend=native` (+ keep `TEKO_TARGET`):** `diff_c_own.sh:306`,
  `validate_wasm_own.sh:346`. **= 2.**
- **Class (c) — no change:** the test gate + the download/package/cc-wrap/CLI-smoke scripts.

---

## 3. Source-change crumb sequence (design only — for the implementer)

Each crumb is independently gate-able. Snippets are in full-Javadoc style — copy verbatim.

### Crumb S1 — `backend_of` inverts (flag wins; no-flag → Native; env seam retired)

`src/build/project.tks:739-753`. Replace the body's tail and delete the env fallback. Retire the
`TEKO_BACKEND` seam (M.5, one selector); `--target`/`TEKO_TARGET` is orthogonal and stays.

```teko
/**
 * backend_of — resolve the code-generation backend for a build from the CLI
 * `--backend={c,native}` flag. Since the 2026-07-14 owner ruling the DEFAULT is
 * `Backend::Native` (Teko's own AOT backend + M-linker — the dogfooded user path).
 * `--backend=c` (or the two-token `--backend c`) OPTS OUT to the trusted C backend,
 * which the compiler's own self-host / release / fixpoint / C-reference sites pin
 * explicitly until the own self-host fixpoint is green (own-backend-architecture
 * §2.4 criterion 2). The flag is the SOLE selector — the temporary `TEKO_BACKEND`
 * env seam (D-B, #385) is retired (M.5, one way to select a backend).
 *
 * @param args   the full CLI argument vector
 * @param start  the index to begin scanning from (2 for a subcommand, 1 for a bare project)
 * @return       `Backend::C` when `--backend=c` / `--backend c` is present, else `Backend::Native`
 */
fn backend_of(args: []str, start: u64) -> Backend {
    mut i = start
    loop {
        if i >= args.len { break }
        let a = args[i]
        if a == "--backend=c" { return Backend::C }
        if a == "--backend=native" { return Backend::Native }
        if a == "--backend" && i + 1 < args.len {
            if args[i + 1] == "c" { return Backend::C }
            if args[i + 1] == "native" { return Backend::Native }
        }
        i = i + 1
    }
    Backend::Native
}
```

Then **delete** `native_backend_selected()` (`project.tks:709-711`) and its doc-comment, and every
`TEKO_BACKEND` read/mention in the surrounding doc-comments (`:615`, `:702-710`, `:822`, `:884`,
`:1424`). `emit_native`'s `TEKO_TARGET` target-selection seam (`:721-727`) stays UNTOUCHED.

> **Alternative if the env seam is retained** (NOT recommended — violates M.5): keep
> `native_backend_selected()` but the fallback line becomes unreachable-for-C, since the no-flag
> default is Native regardless. Retiring it is the clean, one-way answer.

### Crumb S2 — the manifest default flips

`src/build/manifest.tks:379`: `backend = Backend::C` → `backend = Backend::Native`. Update the
`Backend` doc-comment (`:9-15`) — the default is now `Native`, `C` is the opt-in trusted path.
Update the field doc (`:54-56`) — `backend_of` now defaults to Native, `--backend=c` opts out.

> The **authoritative** default is `backend_of` (S1) via `with_backend`; the manifest constructor
> value at `:379` is the pre-stamp seed and is overwritten on every live CLI path. Flip it too so
> Native is the single story at every layer and no unstamped path silently falls back to C.

### Crumb S3 — the `compile_project` convenience + help banner

- `src/build/project.tks:1246` `compile_project(...) { compile_project_g(..., Backend::C) }` →
  `Backend::Native`. **FLAG:** this convenience has no live caller (main.tks calls
  `compile_project_g` with `backend_of` directly); flip for consistency or note it dead.
- `main.tks:63` help banner: `--backend={c,native}   code-generation backend (default: c; …)` →
  `default: native; c = the trusted C backend (self-host/release pin)`.

### Crumb S4 — the tests (default is now Native; `--backend=c` opts out; precedence still holds)

`src/build/project_test.tkt`:

- `backend_of_flag_wins` (asserts `--backend=native`→Native, `--backend=c`→C) — **KEEP unchanged**;
  the flag parse is unchanged.
- `backend_of_defaults_to_c` (`:256-267`) — **RENAME to `backend_of_defaults_to_native`** and invert:
  a flag-absent call resolves to `Backend::Native`. Drop the `set_var("TEKO_BACKEND","")` guard
  (seam retired).
- `backend_of_flag_overrides_env` (`:237-252`, sets `TEKO_BACKEND=native`) — the env-precedence
  premise dies with the seam. **REWRITE to `backend_of_flag_pins_c`**: with no env, `--backend=c`
  and `--backend c` → `Backend::C`, and a flag-absent call → `Backend::Native`. This preserves the
  precedence proof (explicit flag beats default) without the retired env seam.
- `project_arg_of_skips_backend_flag` — **KEEP**; `project_arg_of` (`:1546-1560`) already skips both
  `--backend <v>` and `--backend=<v>` and needs no change.

`src/build/manifest_test.tkt` (exact sites):

- `:115`/`:123` — the "defaults its backend to `Backend::C`" test: flip the doc-comment and the
  assertion `m.backend == Backend::C` → `== Backend::Native`.
- `:129`-`:142` — the `with_backend` immutability test currently changes to `Backend::Native` and
  asserts the original stays `Backend::C` (`:142`). Once the default IS Native this is trivial
  (Native→Native). **Rewrite to change to `Backend::C`**: `with_backend(m, Backend::C)` → the copy
  is `Backend::C` (`:138`), the original `m` stays `Backend::Native` (`:142`). This keeps the
  immutability proof meaningful against the new default.

### Crumb S5 — the CI/script pins (§2 audit)

Apply §2.1–§2.4: add `--backend=c` to all 26 class-(a) sites; migrate the 2 class-(b) own legs from
`TEKO_BACKEND=native` to `--backend=native` (keeping `TEKO_TARGET`). This crumb is the one that
KEEPS the release/self-host byte-identical; land it in the SAME PR as S1–S2 so no window exists
where the default flips but the pins are absent.

**Sequencing law:** S5 must not lag S1/S2 into a merge. A merged S1/S2 without S5 = a
release-breaking window. Gate the whole set as one ritual (§5).

---

## 4. The retire ladder — each `--backend=c` pin is tracked debt

The pins are debt that shrinks to zero as native self-hosts. Order of removal:

- [ ] **R0 (already covered):** the own==C differential CORPUS (`diff_c_own.sh` own leg,
  `validate_wasm_own.sh` own leg) already proves own==C on the corpus — these are class (b), not
  debt; they exercise the native path today.
- [ ] **R1 — sanitizer corpus builds** (`sanitizers.yml:166`, `:247`): may drop the C pin once the
  native path is itself sanitizer-instrumentable (own backend emits machine code, so this likely
  stays C until an own-backend UB story exists — LOW priority, keep pinned).
- [ ] **R2 — the CWD/rt_dir regression** (`native_regressions.sh:55`): drop the C pin once `char_ops`
  (and the #64 rt_dir probe) is confirmed native-buildable across all release hosts.
- [ ] **R3 — the self-host builds** (`release.yml` gen chain, `native.yml` gen1 builds,
  `sanitizers.yml` gen1 builds, `install_share_runtime_test.sh:55`): drop **only** when the own
  self-host fixpoint is green — gen1==gen2==gen3 byte-identical through the own path across all 8
  targets (§2.4 crit. 2), gated on **D2/#391** (3-way gate) **+ Phase E** (the M-linker removes the
  last `cc`-as-linker fallback). This is the bulk of the debt and the last to drop.
- [ ] **R4 — the release cross-compile emit** (`release.yml:140`, `native.yml:396`,
  `cross_compile_linux.sh` consumers): the `teko.c` emit path retires only when the own backend
  cross-emits all six Linux targets natively AND the M-linker links them — i.e. `cross_compile_linux.sh`
  itself is replaced by an own-backend cross path. Gated on Phase B (all ISAs) + Phase E.
- [ ] **R5 — retire `tk_emit_c` entirely** (own-backend-architecture §2.4): all pins gone, C backend
  deleted. This is the terminal state; only then does `--backend=c` itself disappear.

**Invariant:** a pin may be removed only when its site's own==native equivalence is PROVEN for that
exact build (fixpoint for self-host sites; cross-run for release sites), never on a date.

---

## 5. Ritual / verification plan (for the implementer)

The inversion is proven safe when BOTH hold, in one gate:

1. **Default-native works (user-facing).** A flag-absent `teko build <fixture>` and `teko run
   <fixture>` over a native-covered corpus fixture builds + runs via the own backend (the
   `"(own backend)"` marker present) and exits correctly. This is new positive coverage — add a
   fixture assertion that a flagless build reports the own-backend marker (mirrors
   `diff_c_own.sh`'s marker check).
2. **Every pinned self-host/release stays BYTE-IDENTICAL to today.** With S5 applied, the
   `release.yml` fixpoint (`cmp gen2/teko.c out/teko.c`) still passes and the emitted `teko.c` is
   identical to the pre-inversion `teko.c` for the same commit (the pins force the exact same C
   path). The `diff_c_own` / `validate_wasm_own` corpus stays green (C leg = pinned C reference;
   own leg = native). The riscv qemu leg and windows-selfhost leg stay green.

**Explicit statement for the reviewer:** a missing self-host / release / fixpoint pin is a
**release-breaking defect**, not a warnable nit. The self-host sites (`release.yml` gen chain,
`native.yml`/`sanitizers.yml` gen1 builds) and the `teko.c`-emit sites (`release.yml:140`,
`native.yml:396`, `install_share_runtime_test.sh:55`) are the ones where a miss silently produces
a non-`teko.c` output and fails the `cmp` — or worse, ships a native-miscompiled seed. The PR gate
MUST re-run `release.yml` in dry-run (or the equivalent gen1→gen2→gen3 + `cmp` locally) to prove the
fixpoint survives the flip.

**Ritual points (full gate must pass):**
- After S1–S4: `teko test .` on the compiler (unit tests for `backend_of` + manifest default).
- After S5, before merge: the FULL native.yml + sanitizers.yml gate on the PR, plus a release.yml
  **dry-run** dispatch proving gen2==gen3 and the six cross artifacts still build. This is the
  keystone ritual point for this issue.

---

## 6. Risks + law tensions (resolved law-first)

- **R-α — env-seam retirement blast radius.** Retiring `TEKO_BACKEND` touches the differential
  scripts (their own legs move to `--backend=native`). *Resolution (M.5, one-way-to-do-it):* retire
  it; the flag is the single selector. The migration is mechanical and covered by S5. If the
  integrator prefers minimal blast radius, `backend_of`'s fallback can flip to `Native` while
  leaving `native_backend_selected()` in place as dead code — but that leaves two spellings of one
  choice (M.5 violation), so retirement is recommended. **Not a HALT.**
- **R-β — `native.yml:133` runs the gate AND the release-binary codegen.** `teko . -o bin` first
  runs the C test gate (unaffected) then codegens the release binary via `backend()` = native by
  default. The codegen tail is a self-host and MUST pin C. Confirmed class (a). No tension.
- **R-γ — `char_ops` under native** (`native_regressions.sh:55`): unknown whether the native backend
  covers `char_ops` on every host. *Resolution:* pin C (conservative) now; drop at R2 once verified.
  **FLAG for the implementer to confirm.**
- **R-δ — `install_share_runtime_test.sh` not in a CI workflow.** It is a standalone script; if
  `install.sh`'s test path or a future lane runs it, the pins matter. Pin C regardless (it is a
  C-runtime-staging test by definition). **FLAG: confirm whether any lane invokes it.**
- **R-ε — task brief cites `mem-paranoid` + `diff_c_own.sh`/`validate_wasm_own.sh`/`backend_of` that
  do not all exist on `main`.** They exist on the flag branch `fix/issue-390-d1-backend-flag` (this
  audit's base), NOT on the `remodel/backend-build` local tip or `main`. **The inversion must land
  on the flag branch (or its merge), where the `--backend` flag actually exists.** No `mem-paranoid`
  job exists even here (stale). **FLAG: confirm the target branch before implementing.**

No genuine unresolved tension remains — this design does not HALT.
