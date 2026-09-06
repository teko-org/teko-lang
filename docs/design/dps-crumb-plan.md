# DPS keystone chain — ordered, serialized crumb plan (BOTH emissions: C-route `codegen.tks` + native `lower.tks`)

> **Owner rulings 2026-08-20 (govern this plan):**
> 1. *"Escrever em teko não exime o agente de emitir em C. Tem que sim ensinar em teko, mas o codegen C
>    tem que saber emitir."* — the C codegen MUST EXPLICITLY emit arena-aware destination-passing, not
>    lean on the host C compiler's generic `sret` (which gives a caller slot but knows nothing of Teko's
>    ARENA — which region, the escape / single-writer rules that give the UAF/OOB safety).
> 2. *"Uma coisa não tem a ver com a outra, o lower tem que ser feito SIM. O que é deferido para depois
>    é RODAR o build, não a escritura de código."* — DPS must be WRITTEN in BOTH directions NOW: the C
>    emission (`codegen.tks`) AND the native emission (`src/lir/lower.tks`), per the SAME owner ABI. What
>    is deferred is RUNNING/validating the native backend at runtime — NOT writing `lower.tks`.
>
> **Consequence (the shape of every crumb below).** Each DPS crumb makes TWO coordinated edits under the
> owner's single ratified ABI (`ast-computed-arena-assessment-0.3.1.md` §4):
> - **C-route emission** in `codegen.tks` — WRITTEN now AND runtime-validated now (the C-route self-build
>   + `.tkr` fixtures exercise the emitted C directly).
> - **Native LIR emission** in `lower.tks` — WRITTEN now, self-hosts now (it is compiler source: it
>   compiles via the C route and reseeds BYTE-IDENTICAL like all source), but its native RUNTIME
>   validation is deferred to the milestone (running `TEKO_BACKEND=native` is deferred; writing the code
>   is not).
>
> Both edits ride the SAME per-crumb reseed: each changes `teko.c` (both files are compiler source), and
> the C-route fixpoint `gen2.c == gen3.c` proves BOTH transliterate deterministically.
>
> **Status:** architect deliverable (design + ordering only; NO product `.tks`, NO build, NO reseed).
> **Branch:** `fix/retirement` @ HEAD `3acd43c5`. **Owner:** schivei. **Milestone:** M1 (memory
> byte-movers), required-before `SM-R1`. **NO PR** (`fix/retirement` carries PR #110; commit/cherry-pick only).

---

## 0. The design (owner's, ratified) and the two emissions it maps to

The DPS ABI is settled in `docs/design/ast-computed-arena-assessment-0.3.1.md` §4 and restated as the
memory-model foundation in `docs/design/lang-evolution-0.3.1-memory-and-surface.md` §1. This plan does
not author an alternative — it applies the owner's ONE ABI to BOTH emissions.

**The keystone, in the owner's words** (assessment:238-241): *"the callee receives a reference to the
caller's arena (a destination), and `return` writes the value THERE and exits — no callee-local frame
slot to copy out of. This is destination-passing style (DPS) for aggregate returns."*

**The UAF guard, ruled by the owner** (assessment:434-443): the destination MUST be *"a destination the
caller allocated in the caller's OWN current region … NOT a fresh child (a child dropped at scope exit
could strand the returned value)."* DPS *"does NOT drop anything under the callee — it redirects where
the callee writes."*

**Governing safety principle** (assessment:462): *"A segurança não está no tipo da variável, está na
capacidade das arenas."* The destination is **single-writer-BY-CONSTRUCTION from the call/return control
flow** (assessment:502-508), never a `let`/`mut` fact.

**The owner's phased sequence** (assessment §6, D0…D6). This task orders the **memory-byte-mover
subset** — the "keystone chain" — as BOTH-emission crumbs:

| this task | owner phase | what it is (both emissions) |
|---|---|---|
| (prereq, DONE) | D1 | pin `type_match`/`frame_sweep_inst` = `SM-P1` — informs the NATIVE tail-merge routing (now written) |
| **SM-A1** | D0 | instrument the aggregate-return conveyance volume (baseline) — C + native probes |
| **SM-A2** | D2 | DPS ABI + arena-aware construct-in-caller-region (the keystone) — C emission + native LIR emission |
| **SM-A3** | D3 | retire the post-hoc return copy on the DPS path — C escape/by-value copy + native `own_returned_value` box |
| **SM-A4** | Idea 2 | arena elision (`scope_touches_arena`) — C region openers + native region openers |

**`SM-P1` verdict is banked** (`docs/memory/0.3.1-native-p1-pin-type-match-frame-sweep.md`, `4f775e8d`):
`type_match` = RETURN/TAIL-MERGE facet → **native DPS closes it by construction (GO)**; `frame_sweep_inst`
= PAYLOAD-BIND facet → **native DPS does NOT close it (NO-GO)** (self-append family, `SM-A5`). Because
the native side is now WRITTEN, this pin is a live input to SM-A2's native tail-merge routing (route each
tail `match`/`if` arm into the shared `ret_dest`), NOT a native-only afterthought.

Owner phases D4 (remove `-> ref T` = `SM-G4`), D5/D6 (CF3 + `let`/`mut` merge = `SM-G2`) are OUT OF
SCOPE — separate crumbs (§6 seam). The owner is explicit (assessment:518-527) that "DPS does not FORCE
`let`/`mut` removal" — the SM-A crumbs must NOT touch `BindKind`.

---

## 1. The two backends' arena models DPS builds on (VERIFIED, HEAD `3acd43c5`)

### 1.1 C route (`src/codegen/codegen.tks`) — arena is already first-class

Per-function setup, `emit_function_cov` (`codegen.tks:7381`):
- **`_tkrr = tk_region_current()`** (`:7393`, `cg_rr_init_expr` `:7276`) — the **CALLER's current region**,
  captured at callee entry. This IS the "caller's arena" the owner ABI names; the DPS dest lives here.
- **`_tkfr = tk_region_new(_tkrr)`** (`:7395`) + **`tk_region_enter(_tkfr)`** (`:7396`) — a child frame
  region, DROPPED on return (`cg_frame_exit` `:7355` / `cg_return_frame_exit` `:7362` / `cg_drop_all_regions`
  `:6290`). A value built in `_tkfr` and returned is stranded — the UAF DPS avoids by writing into the
  caller-passed dest in `_tkrr`, never `_tkfr`.
- **Primitives** (`CgArenaSym` `:251`, `cg_arena_sym` `:255`): `tk_region_alloc` (`:284`), `tk_region_new`
  (`:285`), `tk_region_current` (`:289`), `tk_region_root` (`:288`). Dest reserved with
  `tk_region_alloc(<caller region>, sizeof(T))`.
- **Existing escape machinery** DPS refines/retires: `checker::fn_escaping_vars(f)` (`:7387`) +
  `fn_body_has_frame_local` (`:7251`) + `binding_conveys_escape` (`:7286`) route an escaping aggregate
  return to `_tkrr` today, or return it by value (a C struct copy). DPS replaces both with an EXPLICIT
  caller-passed destination.
- **Edit sites:** callee return `emit_return` (`:6708`, by-value `return <expr>;` at `:6731-6733`) →
  `emit_as_r`/`emit_as_r_in` (`:5018`/`:5022`); callee signature `emit_function_sig` (`:7196`, params
  `:7207-7224`); caller `emit_call`/`emit_call_inner` (`:3286`/`:3297`).

### 1.2 Native LIR (`src/lir/lower.tks`) — the owner's original ABI target

The native backend already holds the retrofitted, leaky version of DPS the owner ABI formalizes
(assessment:99-116). Verified anchors (HEAD `3acd43c5`; **the pre-existing `.crumbs/0002`–`0005` cite a
STALE 11700-line `lower.tks` — the real file is 6577 lines**; §7 D-1):
- **`type LowerCtx`** — `lower.tks:435-450` (add the `ret_dest` field, owner's `with_ret_dest`).
- **`fn lower_return`** — `lower.tks:2822` (boxes at `:2833` via `own_returned_value`).
- **`fn lower_return_fat`** — `lower.tks:2840` (fat/aggregate return; stores len to `ret_len_slot`).
- **`fn lower_fn_body`** — `lower.tks:5852`; **implicit-tail box at `:5861`** — a SECOND box site the
  crumbs/assessment miss; DPS routing + retirement MUST cover it.
- **`fn own_returned_value`** — `lower.tks:4460` (the retrofit box; `box_aggregate_value_at` `:4438`,
  `returned_aggregate_box_bytes` `:4452`).
- **`fn lower_call`** — `lower.tks:1240` (caller dispatch; reserve + pass the hidden `ret_dest`).
- **`fn region_current_vreg`** — `lower.tks:647` (the caller-region handle `alloc_call_dest` uses).
- **`fn open_native_region`** — `lower.tks:653` (region opener; `bracket_depth>0` skip at `:654`/`:663`
  is the precedent shape for the A4 elision arm). **`fn open_frame_region`** — `lower.tks:578`.
- **Tail merges:** `fn lower_block_value` `:3396`; `fn lower_match`/`lower_match_value`/`lower_match_arm_value`
  `:4221`/`:4168`/`:4148` — each tail arm lowers into the shared `ret_dest` (the `type_match` facet
  SM-P1 confirmed native DPS closes).
- **`fn frame_escape_guard`** — `frame_escape.tks:9` (inversion net; KEPT); `frame_sweep_inst` `:94`.
- Helpers: `is_fat_type` `:4255`, `is_register_value_type` (widely used).

### 1.3 Validation split
- **C emission:** WRITTEN + RUNTIME-VALIDATED now — `.tkr` fixtures (`Given env = ["TEKO_BACKEND=c"]`,
  `Then it exits 0`) compile through `codegen.tks` (clang) and assert the compiled binary's exit code.
- **Native emission:** WRITTEN + SELF-HOSTS now (compiles via the C route; reseeds byte-identical), but
  its RUNTIME validation (running `TEKO_BACKEND=native` binaries) is DEFERRED to the milestone. The
  per-crumb C-route fixpoint `gen2.c == gen3.c` proves the native LIR source transliterates
  deterministically; running it natively is the deferred step.

---

## 2. The owner's ABI, applied to BOTH emissions (the shapes the implementer adds)

**Native LIR shapes — copy VERBATIM from `ast-computed-arena-assessment-0.3.1.md:262-323`** (do not
re-author): `fn_returns_aggregate(f: checker::TFunction, ctx: LowerCtx): bool`,
`with_ret_dest(ctx: LowerCtx, dest: u32 | null): LowerCtx`,
`lower_return_into_dest(ctx: LowerCtx, r: checker::TReturn): LowerStmtOut | error`,
`alloc_call_dest(ctx: LowerCtx, callee: checker::TFunction): Lowered`. Their full Javadoc bodies are in
the assessment; `alloc_call_dest` MUST reserve in `region_current_vreg` (the caller's CURRENT region),
NEVER a fresh child (owner UAF guard).

**C-route counterparts — the SAME ABI, C emission** (full Javadoc; `CgProg`/`Cb`/`RegionFrame` are the
codegen types):

```teko
/**
 * cg_fn_returns_aggregate — does `f`'s C return type need a caller-passed arena destination under DPS?
 * True exactly when the return is emitted as a C struct/slice/variant (the SAME classification the
 * layout tables use), never a register scalar/enum/pointer. The C-route twin of the owner's
 * `fn_returns_aggregate` (assessment §4.2).
 *
 * @param prog  the codegen program (its struct/variant/layout tables)
 * @param f     the function whose return convention is being decided
 * @return      true iff `f` must receive a hidden `_ret_dest` destination for its return value
 * @since 0.3.1
 */
fn cg_fn_returns_aggregate(prog: CgProg, f: checker::TFunction): bool

/**
 * cg_alloc_call_dest — the caller half on the C route: emit the reservation of a destination for an
 * aggregate-returning call in the CALLER's CURRENT arena region (`tk_region_current()` at the call
 * site — owned by the caller, outlives the call), yielding the C expression for the hidden `_ret_dest`
 * argument. NEVER a fresh child region (assessment:434-443). The C-route twin of `alloc_call_dest`.
 *
 * @param buf     the C output buffer
 * @param prog    the codegen program
 * @param callee  the aggregate-returning function being called (for its return layout/size)
 * @param regions the caller's live region stack (its current region is the destination's home)
 * @return        the buffer with the destination reserved and the `_ret_dest` C expression, or error
 * @since 0.3.1
 */
fn cg_alloc_call_dest(buf: Cb, prog: CgProg, callee: checker::TFunction, regions: []RegionFrame): CgDest | error

/**
 * emit_return_into_dest — the C-route VIRTUAL return: emit the construction of the return value DIRECTLY
 * into the caller-passed `_ret_dest` (in the caller's arena region) and a bare `return`, replacing the
 * by-value `return <expr>;` + the post-hoc escape copy. A tail `if`/`match` feeding the return emits
 * each arm's construction into the SAME `_ret_dest`. Entered only when `_ret_dest != NULL`; when NULL,
 * `emit_return` keeps today's by-value path (byte-identical). The C-route twin of `lower_return_into_dest`.
 *
 * @param buf   the C output buffer
 * @param prog  the codegen program
 * @param r     the typed return statement
 * @param dest  the caller-passed destination (`_ret_dest`) in scope, non-null on the DPS path
 * @return      the buffer with the construct-in-dest emission, or a NAMED codegen honest-stop
 * @throws      when the return shape cannot be placed into the destination (out-of-subset)
 * @since 0.3.1
 */
fn emit_return_into_dest(buf: Cb, prog: CgProg, r: checker::TReturn, dest: CgDest, /* … ctx … */): Cb | error
```

Elision predicate (owner's Idea 2, both emissions):

```teko
/**
 * scope_touches_arena / cg_scope_touches_arena — true iff a scope's body performs ANY arena allocation
 * (array literal, aggregate materialization, `region_alloc`, a DPS dest construction, or a call whose
 * callee may allocate into the current region). When FALSE for a leaf scope, the region is elided — no
 * region opener, saving the 64 KiB floor. CONSERVATIVE: any doubt returns TRUE (keep the region; route
 * to the enclosing region — leak-safe, never UAF; assessment §3.3).
 *
 * @param body  the scope body under analysis (native: LBlock; C route: []@TStatement())
 * @return      true iff the scope allocates into the arena (⇒ keep the region); false ⇒ safe to elide
 * @since 0.3.1
 */
fn scope_touches_arena(body: LBlock): bool                       // native, lower.tks
fn cg_scope_touches_arena(prog: CgProg, body: []@TStatement()): bool   // C route, codegen.tks
```

**The hidden `_ret_dest` / `ret_dest` shape (owner ABI, assessment:250-251):** aggregate-returning
functions gain a hidden trailing destination — a pointer into the caller's current arena region.
`ret_dest == null` (`_ret_dest == NULL`) selects today's path, byte-identical, so DPS is opt-in per call
site on BOTH backends. The exact C spelling and the native `LType` of the dest are implementation choices
WITHIN the owner's ABI; the load-bearing contract is: caller-current-region destination,
constructed-in-place, `null` = today.

---

## 3. Standing constraints baked into every crumb

1. **Both emissions WRITTEN per crumb (owner 2026-08-20).** C side (`codegen.tks`) + native side
   (`lower.tks`), one ABI. C side is runtime-validated now; native side self-hosts now, runtime-validated
   at the milestone.
2. **SERIALIZED + PER-CRUMB HAND-HARVEST RESEED** (CLAUDE.md:243-245, owner). Each crumb changes emitted
   `teko.c` (both files are source) → each gets its OWN reseed. Land ONE AT A TIME: crumb N → reseed →
   commit/push `fix/retirement` → THEN crumb N+1. No parallel reseed-bearing work.
3. **RESEED = gen0 from the seed, like CI, NEVER `fetch_teko.sh`** (CLAUDE.md:195-201, 238-242): `CC=clang
   scripts/build_gen1_from_c.sh` → gen0→gen1→gen2→gen3; assert `gen2.c == gen3.c` (`scripts/fixpoint_gate.sh`);
   reseed `bootstrap/teko.c := gen2`. `fetch_teko.sh` 403s in agent sandboxes.
4. **MEMORY GUARD `ulimit -v 4194304` (4 GiB) = INVIOLABLE** (CLAUDE.md:210-216). All builds in a subshell
   under this cap. DPS should REDUCE the C-route peak (deletes the by-value struct-return copy + post-hoc
   escape box on the DPS path). A blow-up is a signal to FIX inflation, NEVER to raise the ceiling.
   (`.crumbs/0002`–`0005` say 6.5 GiB — STALE.)
5. **NEVER `teko test .`**; scoped `.tkr` only; full gate = the C-route fixpoint.
6. **Teko-only + full Javadoc.** All edits in `codegen.tks` / `lower.tks` (`.tks`); C twins frozen (runtime
   primitives already exist — no new C, assessment:657-659). Every new/edited declaration carries a
   `/** … */` Javadoc block; NO inline `//`.

---

## 4. The ordered crumb sequence (both emissions per crumb; serialized; one reseed at a time)

### SM-A1 — instrument the aggregate-return conveyance volume (owner's D0)

- **C emission (`codegen.tks`).** Count aggregate returns, their byte-size, and escape-routed vs
  by-value copy, at `emit_return` (`:6708`)/`emit_as_r` (`:5018`), reading `fn_escaping_vars` (`:7387`)/
  `fn_body_has_frame_local` (`:7251`)/`binding_conveys_escape` (`:7286`); report near `cg_emit_arena_presize`
  (`:7397`). Behind an inert env-gated flag → byte-identical.
- **Native emission (`lower.tks`).** Count + size `own_returned_value` boxes at the two box sites
  `lower_return:2833` and `lower_fn_body:5861`, using `returned_aggregate_box_bytes` (`:4452`); same
  inert gate.
- **Signatures added.** Fresh inert accumulators (e.g. `cg_obs_return_conv_record` / `obs_return_box_record`);
  NO ABI change, NO emission-decision change.
- **Dependency edge.** `SM-P1` (DONE). Nothing depends on A1 for correctness; A2 reads its baseline.
- **Fixtures.** `none` — the self-build returns aggregates throughout; the probe is verified by its report.
- **Gate / reseed.** `[dry]` — compile + trivial fixpoint; normal build byte-identical. **Reseed-class:
  `none`** if byte-inert; else fold its reseed into SM-A2. **Ritual: NONE.**

### SM-A2 — DPS ABI + arena-aware construct-in-caller-region (owner's D2 — THE KEYSTONE)

- **C emission (`codegen.tks`).**
  - `emit_function_sig` (`:7196`): when `cg_fn_returns_aggregate(prog, f)`, emit a hidden trailing
    `_ret_dest` param after `f.params` (`:7207-7224`).
  - `emit_function_cov` (`:7381`): thread `_ret_dest` into the body ctx; its home is the caller region
    already captured as `_tkrr = tk_region_current()` (`:7393`).
  - `emit_call`/`emit_call_inner` (`:3286`/`:3297`): caller reserves the dest via `cg_alloc_call_dest`
    in its current region (`tk_region_alloc(region_current(), …)`, `CgArenaSym` `:284-289`) and threads
    it as `_ret_dest`; else NULL.
  - `emit_return` (`:6708`) → when `_ret_dest != NULL`, delegate to `emit_return_into_dest`; else keep
    the by-value `return <expr>;` path (`:6731-6733`). `emit_as_r`/`emit_as_r_in` (`:5018`/`:5022`): on
    the DPS path, materialize INTO `_ret_dest`; tail `if`/`match` arms into the SAME `_ret_dest`.
- **Native emission (`lower.tks`).**
  - `type LowerCtx` (`:435-450`): add `ret_dest: u32 | null` (owner's `with_ret_dest`); thread it through
    the `ctx_with*` copy constructors like `region_stack`/`bracket_depth`.
  - `lower_call` (`:1240`): when `fn_returns_aggregate(callee)`, `alloc_call_dest` in
    `region_current_vreg` (`:647`) → hidden `ret_dest`; else `null`.
  - `lower_return` (`:2822`) and **`lower_fn_body` (`:5852`, implicit-tail box `:5861`)**: when
    `ret_dest != null`, delegate to `lower_return_into_dest`; else keep the `own_returned_value` box
    (`:2833`/`:5861`) unchanged. `lower_return_fat` (`:2840`): fat path writes ptr/len into the dest on
    the DPS branch.
  - Tail merges `lower_block_value` (`:3396`), `lower_match_value`/`lower_match_arm_value`
    (`:4168`/`:4148`): each tail arm lowers into the shared `ret_dest` — the `type_match` RETURN/TAIL-MERGE
    facet SM-P1 confirmed native DPS closes.
  - `own_returned_value` (`:4460`): bypassed on the DPS path (retired in SM-A3).
- **Signatures added.** Native: `fn_returns_aggregate`, `with_ret_dest`, `lower_return_into_dest`,
  `alloc_call_dest` (verbatim §2). C: `cg_fn_returns_aggregate`, `cg_alloc_call_dest`,
  `emit_return_into_dest`, `CgDest`. Both dests in the caller's CURRENT region, never a fresh child.
- **`ret_dest == null` = today, byte-identical on both backends** — DPS opt-in per call site.
- **Dependency edge.** Depends on **SM-A1** (baseline) + banked **SM-P1** (GO on `type_match`, informing
  the native tail-merge routing).
- **Fixtures** (C route; `Given env = ["TEKO_BACKEND=c"]`, `Then it exits 0` unless noted — these
  runtime-validate the C emission; the native emission is self-hosted, runtime-validated at the milestone):

  | fixture | asserts | expected |
  |---|---|---|
  | `dps_c_aggregate_return_value_correct` | struct built in a tail `if`, returned via `_ret_dest`, reads back correct at the caller | exit 0 |
  | `dps_c_variant_match_return` | variant returned through a tail `match` (the `type_match` shape) carries the correct value | exit 0 |
  | `dps_c_nested_call_dest` | an aggregate returned by `f` and consumed as `g`'s argument is correct (dest threads through nested calls) | exit 0 |
  | `dps_c_escaping_return_no_uaf` | a returned aggregate outlives the callee's `_tkfr` drop (born in the caller region, not `_tkfr`) | exit 0 |
  | `dps_c_escaping_return_inversion` | INVERSION: force the dest into a fresh CHILD region — the value must be stranded/corrupt | non-zero / detected |
  | `dps_c_ret_dest_null_byte_identical` | a register-value (non-aggregate) return path is unchanged | exit 0 + fixpoint byte-identity |

- **Gate / reseed.** **RITUAL POINT.** C-route: gen0 from `bootstrap/teko.c`, gen0→gen1→gen2→gen3 under
  `ulimit -v 4194304`, assert **`gen2.c == gen3.c`** (proves BOTH the C and native LIR source
  transliterate deterministically); the six C fixtures pass; the SM-A1 baseline drops. On green:
  **hand-harvest reseed `bootstrap/teko.c := gen2`**, commit, push. THEN SM-A3.

### SM-A3 — retire the post-hoc return copy on the DPS path (owner's D3)

- **C emission (`codegen.tks`).** On the `_ret_dest != NULL` branch, REMOVE the by-value materialization
  + the escape-routed `_tkrr` copy (redundant — the value is born in the dest). Anchors: `emit_return`
  (`:6708`)/`emit_as_r` (`:5018`); keep `fn_body_has_frame_local` (`:7251`)/`binding_conveys_escape`
  (`:7286`)/the `_tkrr` conveyance (`:7393`) as the net for residual/non-DPS returns.
- **Native emission (`lower.tks`).** On the `ret_dest != null` branch of `lower_return` (`:2833`) AND
  `lower_fn_body` (`:5861`), remove the `own_returned_value` box construction. Do NOT delete the symbol
  (`:4460`) — the legacy `ret_dest == null` path still uses it. KEEP `frame_escape_guard`
  (`frame_escape.tks:9`) as the inversion net (DPS satisfies it by construction).
- **Signatures added.** NONE — removes dead emissions; no new `fn`/`type` (clean removal, no tombstone,
  CLAUDE.md "NÃO EMITIR O QUE NÃO FLUI").
- **Dependency edge.** Depends on **SM-A2** (DPS landed ⇒ old conveyance dead).
- **Fixtures.** `none new` — exercised by the self-build + gated by the fixpoint; re-run SM-A2's set.
  Optional: `dps_c_no_double_convey` (DPS path writes the dest ONCE, no residual copy) — exit 0 +
  emitted-C inspection.
- **Gate / reseed.** **RITUAL POINT.** C-route `gen2.c == gen3.c` under 4 GiB + SM-A2's fixtures green +
  the old conveyance no longer emits on the DPS path (C) + `frame_escape_guard` clean (native, static
  analysis over the reseeded module) + the SM-A1 baseline at/near zero. On green: **hand-harvest reseed**,
  commit, push. THEN SM-A4.

### SM-A4 — arena elision (owner's Idea 2)

- **C emission (`codegen.tks`).** `cg_scope_touches_arena(prog, body)` gates the region openers: the
  `want_frame`/`_tkfr` decision in `emit_function_cov` (`:7388-7398`) and the per-block `want_block`/`_tkbr`
  opener in `emit_loop_while` (`:6757-6762`, `region_new`/`RegionDrop` `:6761`/`:6767`). Leaf + alloc-free
  ⇒ skip.
- **Native emission (`lower.tks`).** `scope_touches_arena(body)` gates `open_native_region` (`:653`) and
  `open_frame_region` (`:578`), as the elision arm BESIDE the existing `bracket_depth > 0` skip
  (`:654`/`:663`).
- **Signatures added.** `cg_scope_touches_arena` (C) + `scope_touches_arena` (native), §2. Default TRUE on
  any uncertainty (conservative; doubt → keep the region, leak-safe, never UAF).
- **Dependency edge.** Depends on **SM-A1** (region-floor baseline). NOT dependent on A2/A3 (may be
  AUTHORED in parallel), BUT its reseed-bearing build SERIALIZES after A3's reseed. Sequenced LAST.
- **Fixtures.**

  | fixture | asserts | expected |
  |---|---|---|
  | `arena_c_elided_leaf_scope` | an alloc-free `if`/match arm runs correctly AND the emitted C has no `tk_region_new` for that scope | exit 0 + emitted-C inspection |
  | `arena_c_elision_conservative` | a leaf scope with an opaque call is NOT elided (region kept) — leak-safe | exit 0 |

- **Gate / reseed.** **RITUAL POINT.** C-route `gen2.c == gen3.c` under 4 GiB + fixtures pass + region-floor
  volume drops. On green: **hand-harvest reseed**, commit, push. Closes the DPS keystone chain; the
  surface wave (SM-G*, COL-*) and SM-R1 proceed on top.

---

## 5. Serialization / reseed timeline (dispatch order, explicit)

```
SM-P1 (DONE — pin GO on type_match, informs SM-A2's native tail-merge routing)
  │
SM-A1 [dry]     → both probes, no reseed (inert)                    → commit/push fix/retirement
  │
SM-A2 [RITUAL]  → C+native emission · C-route gen2.c==gen3.c        → HAND-HARVEST RESEED → commit/push
  │                + 6 C-route fixtures (bootstrap/teko.c := gen2)        ↑ next crumb waits for this
SM-A3 [RITUAL]  → C+native retirement · C-route fixpoint + guard    → HAND-HARVEST RESEED → commit/push
  │
SM-A4 [RITUAL]  → C+native elision · C-route fixpoint + fixtures    → HAND-HARVEST RESEED → commit/push
  │
(DPS keystone chain closed → SM-G* / COL-* / … → SM-R1)
```

Each RITUAL crumb makes BOTH edits, then ONE reseed. The C-route fixpoint proves both files self-host
byte-identical; the C fixtures runtime-validate the C emission; the native emission's runtime validation
is deferred to the milestone. **One reseed at a time** ("um reseed de cada vez", owner) — SM-A4's
predicates may be AUTHORED in parallel, but its reseed-bearing build does not run concurrently.

---

## 6. Out of scope (separate crumbs)

DEFERRED (milestone-gated): **running/validating** the native backend at runtime (`TEKO_BACKEND=native`),
including the native fixpoint frontier the SM-P1 re-execution noted (`serialize_const`, `syscall6`,
`ar_mmap`) and the master plan's `D1-T2` physical DPS elision. The native DPS CODE is WRITTEN here (§4);
only its native RUN is deferred.

Owner sequence D4/D5/D6 (separate crumbs): remove `-> ref T` (`SM-G4`), re-base CF3 + merge `let`/`mut`
(`SM-G2`). The SM-A crumbs must NOT touch `BindKind` (assessment:518-527, "DPS does not FORCE `let`/`mut`
removal").

---

## 7. Divergences found (current code vs the docs / crumbs) — flagged, resolved

- **D-0 (SCOPE — the load-bearing correction, twice).** (i) The crumbs `.crumbs/0002`–`0005`, assessment
  §4, and umbrella §1 author DPS ONLY against native `lower.tks`; VERIFIED `codegen.tks` has **0**
  references to LIR/`own_returned_value` and the C self-build emits **0** `tk_slice_elem_box` calls —
  yet **owner 2026-08-20 ruling 1** requires the C codegen to emit arena-aware DPS ITSELF (not `sret`).
  (ii) **Owner ruling 2** requires the native `lower.tks` side WRITTEN now too (deferred = running it, not
  writing it). **Resolution:** every crumb (§4) makes BOTH edits under the ONE owner ABI; the C side is
  runtime-validated now, the native side self-hosts now / runtime-validated at the milestone. The owner's
  ABI is unchanged.
- **D-1 (native anchors ALL stale).** Every native file:line in the crumbs/assessment/umbrella is off by
  ~4000–7000 lines (they cite an 11700-line `lower.tks`; real file is 6577). **Resolution:** §1.2 table
  (verified). Additionally `own_returned_value` is called at BOTH `lower_return:2833` AND
  `lower_fn_body:5861` (implicit tail) — the crumbs name only the former; SM-A2/A3 cover both.
- **D-2 (SM-A1 channel fictional).** No `tk_obs`/`#arena_size`/`Confidence` symbol exists. **Resolution:**
  SM-A1 defines its own inert-gated probes on both backends, faithful to owner D0 — observability detail,
  not an ABI decision, no HALT.
- **D-3 (memory guard 6.5→4 GiB).** Crumbs say `ulimit -v 6815744`; standing law is `4194304`
  (CLAUDE.md:210). **Resolution:** 4 GiB. DPS should reduce the C-route peak; a blow-up is a fix signal.
- **D-4 (reseed model "folds R1" → per-crumb).** Crumbs mark A2/A3/A4 "(folds R1)"; standing law
  (CLAUDE.md:243-245, owner) is per-crumb hand-harvest, serialized. **Resolution:** per-crumb (§4/§5).
- **D-5 (ritual = C-route fixpoint; validates the C emission at runtime + both files' self-host).** The
  crumbs' "native ladder gen2==gen3" is deferred (native RUN is milestone-gated). For this spine the
  ritual is `gen2.c == gen3.c` + the `.tkr` C-route fixtures. **Resolution:** §4 fixtures are C-route
  (`TEKO_BACKEND=c`), exit-code + emitted-C inspection; the native emission rides the same reseed as
  self-hosted source.

**No unfillable design gap, no HALT.** The owner's DPS ABI, safety proof, and fixtures are fully
specified in assessment §4 and applied to BOTH emissions verbatim. The only under-specified items are
implementation choices WITHIN the ABI (SM-A1's probe mechanism; the exact C spelling / native `LType` of
the `ret_dest`), resolvable law-first, not owner decisions.
