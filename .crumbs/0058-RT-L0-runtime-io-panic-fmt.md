---
seq: 0058
crumb-id: RT-L0
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-IO]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:37-50"      # §1.1 what teko_rt.tks already does in Teko (L0)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:110-118"    # §2.1 the layer ladder (L0 pure over []byte)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:172-178"    # §2.4 leaves first: L0/L2 do not wait on L3+
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:317"        # §5 F3 — L0/L2 close (str/fmt/UTF-8)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:134-147"    # §2.2 how the native links its own Teko runtime
---

# 0058 · RT-L0 — runtime C→Teko L0: io/panic/fmt/str-query/guards (pure over `[]byte`)

> Close the L0 layer: io / panic / int→str / formatting / str-query / F3 guards run in pure Teko over `[]byte`,
> touching NO OS state — the first and cheapest runtime layer to leave C.

## Goal

L0 is **computation pure over `[]byte`** — no OS state, only the allocation seam (L1) beneath it. `teko_rt.tks`
already realises ~90% of it (`migracao…` §1.1): `u64_to_str`/`i64_to_str`/`str_concat`/`str_of_bytes`/`concat`,
the whole `fmt_*` family (`pad_left`/`group_thousands`/`to_radix`/`fmt_d`/`fmt_x_*`/`fmt_b`/`fmt_dyn_*`), the
str-query family (`str_eq`/`str_hash` FNV-1a/`str_compare`/`str_slice*`/`str_len`/`str_ends_with`/`str_contains`),
and the F3 guards (`div`/`mod` checked, `to_u8` narrowing). What keeps L0 from being CLOSED is not more
translation — it is (a) closing **B2**, the circularity where the io bare-names (`print`/`println`/`write`/
`eprint`/`eprintln`/`ewrite`) still bind the C `tk_*` symbol, and (b) the **float bottom** (`ftoa`/`%.17g`),
which S16-IO (FASE2) realises pure-Teko. This crumb rewires the io bare-names onto the migrated
`SYS_write`/panic bottom and confirms the fmt/str-query/guard bodies stand alone — no `teko_rt.c` symbol on the
L0 path. Byte-preserving for existing programs: the emitted `teko.c` for any program using io/fmt/str is
guarded byte-identical by the fixpoint (residence of existing cases is unchanged; `migracao…` R8). It does NOT
reseed teaching — a `fixpoint-rebuild` swap the core consumes.

**BLOCKED (design-ahead, honest).** Behind the **native fixpoint closing** (`migracao…` banner). This is the
design; the implementer resumes in minutes when the fixpoint closes. L0 is a leaf: it can close while the
Win32 process block (L4) is still C (`migracao…` §2.4 — "adiantar o que der"). It stands on L1 (alloc) for
string construction — but L1's seam already exists as `teko_rt` today; the L1 MIGRATION (`0059` RT-L1) is a
sibling, not a hard prerequisite of the L0 rewire.

## Where

- `src/runtime/teko_rt.tks:62-101` — the io bare-names `print`/`println`/`write`/`ewrite`/`eprint`/`eprintln`
  (today `extern fn … from "teko_rt"`, binding the C `tk_write`) — rewire onto the migrated `SYS_write`
  (S16-IO) so the body is Teko, not a C extern (closes B2).
- `src/runtime/teko_rt.tks:695-746` — `panic`/`panic_div0`/`panic_oob`/`panic_cast`/`panic_overflow`/
  `panic_oob_at` — confirm they stand on the migrated `SYS_write`+`SYS_exit_group` (S16-IO), not `tk_panic_str`.
- `src/runtime/teko_rt.tks:115-184,206-487,496-594,754-772` — the int→str, fmt_*, str-query, and F3-guard
  bodies (already Teko) — confirm none binds a `teko_rt.c` symbol; retarget stragglers.
- `src/runtime/teko_rt.tks:107-599` (float family `ftoa`/`fmt_f`/`fmt_e`/`fmt_g`/`f64_g17`) — repoint the
  float bottom onto S16-IO's pure-Teko `ftoa`/`%.17g` (was the DEFERRED `teko::float::parse`/`fmt::f64_g17`).
- `src/runtime/teko_rt.c` (the 11 live symbols `tk_print`/`tk_println`/`tk_write`/`tk_eprint`/`tk_eprintln`/
  `tk_ewrite`/`tk_panic_str`/`tk_exit`/`tk_str_concat`/`tk_i64_to_str`/`tk_u64_to_str`, `migracao…` §2.2) —
  their L0 half goes DEAD on the native path; NOT deleted here (deletion is `0095`/`0096`, M3).
- NO new module; NO new decl surface — the L0 bodies pre-exist in `teko_rt.tks`.

## How

1. **Close B2 — the io circularity.** The six io bare-names lose their `from "teko_rt"` extern and gain a Teko
   body that calls the migrated `SYS_write` (S16-IO) with the `str`→(ptr,len) seam of `migracao…` §2.2.3
   (`str` decomposed to `s.ptr to u64` + `s.len`; general `extern` forbids fat `str` except the 7 io/panic
   names — that exception now retires as the bodies become Teko).
2. **Confirm the panic path is Teko** over migrated `SYS_write`(stderr)+`SYS_exit_group` (S16-IO); no
   `tk_panic_str` C symbol.
3. **Repoint the float bottom** onto S16-IO's pure-Teko `ftoa`/`%.17g`; the `fmt_f`/`fmt_e`/`fmt_g`/`f64_g17`
   bodies (already Teko) now stand on a Teko bottom, closing the last DEFERRED L0 gap (`migracao…` §1.1).
4. **Audit fmt/str-query/guards for residual C.** Each `fmt_*`/`str_*`/`div`/`mod`/`to_u8` body already exists
   in Teko; confirm no `teko_rt.c` extern remains on the L0 path (nm-check the object).
5. **The link is the normal program link** (`migracao…` §2.2): the L0 bodies become `exp fn` Teko compiled INTO
   the program's own object; there is no separate runtime object to link. `build_cc_argv:923-924` pushing
   `teko_rt.c` no longer supplies these symbols on the native path.
6. **Fixpoint byte-identity is the detector.** `gen2==gen3` byte-identical proves existing io/fmt/str callers
   did not shift; a shift means a residence changed unintentionally — stop and re-examine (`migracao…` R8).

No new W15 signature: the io/fmt/str/guard surface is already declared and documented in `teko_rt.tks`. The
reused surfaces are `SYS_write`/`SYS_exit_group`/`ftoa`/`f64_g17` (migrated by S16-IO) and the `teko::list`
boxing seam (L1). Any edited body keeps its doc-comment; the six rewired io names keep their existing Javadoc
with `@throws` where the write can fail.

## Rulings & laws

- **Teko-only:** L0 bodies land in `src/runtime/teko_rt.tks` (the maintained-C exception is the BRIDGE the
  campaign retires, `migracao…` §1.4/R1); the C `teko_rt.c` L0 half goes DEAD, deleted only at M3.
- **W15 full Javadoc** on every touched declaration; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** this crumb removes no surface (io/fmt/str names persist); it kills
  the C bodies' role. Physical deletion of `teko_rt.{c,h}` is `0095` RM-C9 / `0096` S16-SWEEP (M3), clean and
  tombstone-free.
- **Owner decree (`expurgo…` §3, cited `migracao…` §1.1):** the runtime "goes to Teko — it is design, not
  translation"; L0 is the ~90%-done leaf, closed by wiring not translating.
- **Residence law (`modelo…` §0/§9, `migracao…` §4.1):** str-producing L0 fns (`str_concat`/`fmt_*`/
  `u64_to_str`) route by the current region (`_r`), NEVER root — a runtime str is MOVED to the caller on
  return, never leaked. This crumb preserves that residence exactly (byte-identity is the proof).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after any signature change (none expected).

## Fixtures

L0 is core-consumed by essentially every program (io/fmt/str), so the happy path is fixpoint-covered; the
isolated oracles cover the B2 rewire and the float bottom the self-build may not stress at the boundary:

| fixture | asserts | expected |
|---|---|---|
| `l0_io_no_c_symbol` | `println("hi")` runs and the object references NO `tk_write`/`tk_print` C symbol (nm-check) | `0` |
| `l0_float_g17_pure` | `$"{3.14159265358979}"` formats to the exact `%.17g` digits via the migrated Teko ftoa | `0` |
| `l0_guard_div0` | a checked `div(1, 0)` panics with the L0 message via migrated panic (probe catches exit) | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity. "Green" = the io bare-names bind
NO C symbol (B2 closed), the float bottom is pure Teko, the fmt/str-query/guard bodies stand alone, and the
emitted `teko.c` is byte-identical to before the swap. **Reseed-class:** `fixpoint-rebuild` (core-consumes;
teaches nothing; no reseed harvested).

## Deps

`S16-IO` (`0051` — FASE2 `SYS_exit_group`+`SYS_write`+float-bottom ftoa/`%.17g`: the write/exit/format bottom
L0's io/panic/float stand on).

## Done when

The io/panic/fmt/str-query/guard L0 family runs pure-Teko over the migrated `SYS_write`/`SYS_exit_group`/ftoa
with NO `teko_rt.c` symbol on the L0 path (nm-verified), B2 is closed, the fixtures exit `0`, and a
`[fixpoint]` build is `gen2==gen3` byte-identical.
