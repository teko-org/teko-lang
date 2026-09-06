# Native variant/match root map — the self-host fixpoint blockers (0.3.1)

**Base:** `fix/retirement` @ `584b4f08` (func_type arm-join fix landed).
**Scope:** READ + design only. No product-code changes. Cost-bounded: reasoned from the
pinned backtraces + static reading of `src/lir/lower.tks`, `src/lir/frame_escape.tks`,
`src/lir/lir.tks`, `src/checker/typer.tks`, the C oracle `src/codegen/codegen.tks:6771`,
and the five `docs/memory/*native*` notes. No native ladder builds were run.

**Central question posed:** do the three remaining pinned blockers
(`checker::type_match` SIGSEGV, `lir::frame_sweep_inst` SIGSEGV, `lir::push_inst_block` OOM)
share the SAME arm-tag/variant-model root as bug #4 (`arm_join_retag_into`), or are they
distinct?

**Verdict up front:** No. Bug #4's *proximate* root — a match-arm variant construction
storing `tag = arm POSITION` instead of `tag = declared member index` — is CLOSED and
byte-verified, and it does **not** recur in the three remaining blockers. The three split
into **two** distinct proximate families, neither of which is the arm-tag bug:

- **type_match + frame_sweep_inst** — aggregate value (`TExpr` / `FrameSet`) conveyed
  BY-ADDRESS across a match/if MERGE or returned past its frame slot. Same *by-address
  aggregate conveyance* family as fixes #1 (region-lifetime self-append) and #2 (free-old
  UAF); NOT the tag-numbering facet of #4.
- **push_inst_block** — a corrupt slice header (garbage `len`) reaching `teko::list::push`,
  i.e. the *self-append / slice-header* corruption family of #1/#2. The "int-result match"
  input is only the *trigger volume* (it drives `lower_match_value` → `alloc_block` /
  `append_inst` self-append hard); the match tag model is not implicated.

There IS a plausible single *deep* root shared by all four prior fixes and at least two of
these three — "aggregates are the address of a per-instruction frame slot, and are not
reliably copied-out at conveyance boundaries" — but closing it is a structural change, is
NOT byte-preserving for the C route, and is NOT proven from static reading. A false
"systemic root" is worse than an honest grind, so the recommendation is: **cheap-PIN each of
the three (minimal repro + objdump, the exact method that pinned #4), THEN decide** — details
in §4.

---

## 1. The native variant/match-tag MODEL

### 1.1 Runtime image (uniform, correct, shared with the C route)
Every variant value is the uniform 24-byte wrapper `variant_wrapper_bytes()`:

```
offset 0 : i32 tag            (the DECLARED member index)
offset 8 : ptr  payload word  (scalar value, or boxed-aggregate pointer, or fat .ptr)
offset 16: i64  len           (fat members only: the .len half)
```

- **tag = the DECLARED member index**, everywhere. This is the model invariant.
- Payload offset `variant_payload_offset()` = 8, len offset `variant_payload_len_offset()`
  = 16.

### 1.2 CONSTRUCT side — uses declared index (correct)
`lower_variant_construct` / `lower_value_into_variant` / `store_variant_payload` /
`lower_null_union_construct` all tag with the member's *declared* index
(`direct_variant_member_index`, `variant_member_index_of`, `variant_null_member_index`,
`NullUnionShape.payload_index/null_index`). The doc-comments at lower.tks:3429/3454/3607/
3631/9277/9280 are explicit: "the member's declared tag index."

### 1.3 READ / DISPATCH side — uses declared index (correct)
`lower_variant_tag_eq_test` (lower.tks:9340) loads `tag@0` and compares against a *declared*
index resolved by name/kind through `find_variant_member` / `variant_case_of_bind_pattern`
(lower.tks:10460, 10407, 10584). `bind_case_payload` (lower.tks:9371) reads `payload@8`
(scalar or boxed) / `len@16` (fat) by the member's declared type shape. No position
arithmetic anywhere on the read side.

### 1.4 The ONE historic divergence (bug #4) and where it lived
The checker types an `if`/`match` USED AS A VALUE at the **union of its arm types IN ARM
ORDER** (`type_match`/`type_if` → `type_join`, arm-order). So a body
`match a { 0 => B{}; _ => A{} }` whose declared slot is `variant A | B` is typed `B | A` —
same member SET, DIFFERENT tag numbering. Lowered plainly, each arm built its wrapper with
ITS OWN inferred-union index (arm position); the consumer, reading through the DECLARED
slot's numbering, then misread the tag. `ref_target_type`'s `_ => Reference{…}` arm
(Reference→Type, declared tag 0) sat after four `error` arms and came out tagged `1=error`,
so `func_type` reported a spurious signature error with a garbage payload.

The fix (`arm_join_retag_into`, lower.tks:6881, mirroring the C oracle `emit_as` at
codegen.tks:6771): when the value is an `if`/`match` whose inferred variant type is not the
target's tag layout but every member reaches the target, retag the NODE type to the declared
`bound` and re-`lower_expr`, so `lower_block_value(..., target_ty = e.type)` (lower.tks:10798)
coerces each arm's tail DIRECTLY into the declared tag space via `lower_value_into_type`.

### 1.5 The tag==position vs tag==declared-index audit (the surface #4 touched)
The *only* site that ever assumed `tag == arm position` was the un-retagged arm tail's
construction into the inferred arm-order union. After #4 there is **no remaining
tag==position site on the scalar placement path**: `arm_join_retag_into` is invoked from
`lower_value_into_type` (lower.tks:6840), which every SCALAR placement routes through
(binding, assign, return, tail-expr-stmt, list-push, struct-field, arg-into-param —
enumerated in the lower.tks:6807 doc). Construct and read both use declared index. The tag
model is internally consistent post-#4.

**One adjacent gap noted (report-up, not part of this map's blockers):** the FAT path
`lower_fat_expr_into_type` (lower.tks:6654) and `lower_match_arm_fat` (lower.tks:12371,
calls `lower_block_value_fat` with NO `target_ty`) do NOT invoke `arm_join_retag_into`. This
is harmless for the tag model (fat values `str`/`[]T` carry no variant tag; the one fat
coercion that matters, `[]U`→`[]T` element widen, is handled separately at lower.tks:6658).
It would only bite a `[]variant` element reorder inside a fat match — not on the self-host
critical path. Flagged for the owner, not expanded into an issue here.

---

## 2. Root classification of the 3 remaining pinned blockers

### 2.1 `checker::type_match` — SIGSEGV at checker 3/7078, on a variant `match`
**Path (typer.tks:3971).** `type_match` returns the aggregate `TExpr` and, on the way,
conveys aggregates through control-flow joins: the arm-type fold
(`first = type_join(first, t, table)`, 3991), the anchor merge
`match match_join_anchor(typed) { Type as anchor => adopt_match_arm_literals(typed, anchor);
null => typed }` (3982, a `[]TArm` slice produced through a match merge), and the tail
`TExpr { kind = TMatchExpr { subject = s; arms = arms }; … }` (3998). The crash is
data-gated on the SUBJECT being a variant — i.e. it fires only on the variant-specific
sub-paths (`type_match_arm` bind-pattern typing, `exhaustive`, `resolve_foreach_placeholders`,
`adopt_match_arm_literals`), which item 3 is the first item to exercise.

**Classification: SAME broad subsystem as #4 (variant/match), but a DIFFERENT facet —
aggregate-through-merge/return, NOT arm-tag.** Nothing in `type_match` constructs a variant
whose declared index differs from its arm position (the `TExprKind`/`TMatchExpr` construction
is a DIRECT member, correctly tagged). What it *does* is return and merge aggregates
(`TExpr`, `[]TArm`) — the `TExpr`/slice is the address of a per-instruction frame slot, and
if that slot is reused/reclaimed across the merge or past the return, the consumer reads
corruption. This is the #1 (region-lifetime) / #2 (free-old) family, resurfacing at a
match-merge boundary rather than a self-append. **Not** closable by the #4 tag fix.
*Caveat:* the exact faulting offset (`type_match +0xe85`) was not disassembled under this
cost budget; this classification is from the code shape + the "variant-subject-gated" trigger,
and should be confirmed by the §4 pin before a fix is written.

### 2.2 `lir::frame_sweep_inst` — SIGSEGV, no-match project
**Path (frame_escape.tks:209).** `match inst.op { LAlloca => return frame_mark(set, …);
LFieldAddr as fa => return if flag_at(set.marked, fa.base) { frame_mark(set, inst.result) }
else { set }; LJump as j => …; LBranch as br => …; _ => { } }` then falls through to `set`.
The return type is the aggregate `FrameSet { marked: []bool; grew: bool }`
(frame_escape.tks:13). The `LFieldAddr` arm RETURNS AN IF-VALUE MERGE of `FrameSet`; every
arm returns a `FrameSet`; `frame_mark` (frame_escape.tks:272) builds
`FrameSet { marked = fs; grew = true }` where `fs = set.marked; fs[v] = true` is an
in-place index-assign into a `[]bool`. A NO-MATCH input still exercises this — every lowered
function has `LAlloca`/`LJump`/`LBranch`/`LFieldAddr` — so the crash is in
`frame_sweep_inst`'s OWN lowered body, not in data produced by a source-level match.

**Classification: SAME family as type_match — aggregate (`FrameSet`, and its `[]bool`)
conveyed by-address through an if-value MERGE and returned/mutated-in-place. NOT arm-tag,
NOT the #4 root.** Two candidate proximate causes, both in the by-address-aggregate family:
(a) the `FrameSet` returned through the `return if …` merge is a reused frame slot; or
(b) the `[]bool` `fs` (a slice header) mutated-in-place then wrapped and returned aliases a
slot the next call overwrites. Either is the #1/#2 slice/region family. Note this is a
READ+merge shape (`inst.op` tag-dispatch with boxed-struct payload binds `fa`/`j`/`br`), so
if instead the fault is in the payload BIND of a boxed multi-member variant, it is still a
READ-side sibling of the variant model, still not the arm-tag construction bug. Pin to
disambiguate (a) vs (b) vs payload-bind (§4).

### 2.3 `lir::push_inst_block` — spurious M.1 OOM, int-result match project
**Path (lir.tks:530).** `LBlock { id = b.id; params = b.params;
insts = teko::list::push(b.insts, inst) }`. An M.1 "out of memory (str concat)" / exit-134
here is the SAME signature as the func_type bug PRE-fix: a garbage LENGTH driving a huge
allocation. `push_inst_block` itself contains no match and no variant; it receives a `b:
LBlock` whose `b.insts` fat header arrives with a corrupt `len`. It is called in the
self-append hot loop `append_inst` (lir.tks:552) — `blocks[i] = push_inst_block(blocks[i],
inst)`, an in-place index-assign onto a slice that aliases `f.blocks`' buffer — whose own
doc-comment (lir.tks:534-551) already narrates a wild-write/aliasing hazard fixed one level
below at isel. The "int-result match" input is the trigger *volume*: `lower_match_value`
(lower.tks:10859) builds merge blocks via `alloc_block` + `append_inst`, hammering this
self-append path.

**Classification: DISTINCT from the match tag model entirely — the self-append /
slice-header-corruption family (#1 region-lifetime `5026006a`, #2 free-old UAF `74622f77`).**
The word "match" in the trigger is a red herring: the corruption is in the LBlock/LFunc
self-append machinery, not in how a source match is tagged. Closable by the same technique as
#1/#2 (materialize/copy at the self-append boundary, or park-safety of the aliased buffer).

### 2.4 Summary table

| blocker | subsystem | proximate family | same root as #4? |
|---|---|---|---|
| `type_match` SIGSEGV | variant/match | aggregate-through-merge/return (by-address) | No — #1/#2 family |
| `frame_sweep_inst` SIGSEGV | variant/match (read+merge) | aggregate-through-if-merge / in-place `[]bool` | No — #1/#2 family |
| `push_inst_block` OOM | LIR self-append | slice-header corruption / self-append | No — #1/#2 family |

---

## 3. Is there a SYSTEMIC fix?

**No single byte-preserving point-fix closes all three, and none of the three is closed by
re-touching the #4 arm-tag code.** The three do NOT share the arm-tag/tag-numbering root.

**There is, however, a candidate DEEP root** that would unify at least type_match +
frame_sweep_inst with ALL FOUR prior fixes:

> **The native backend represents every aggregate value (variant wrapper, struct, slice
> header) as the ADDRESS of a per-INSTRUCTION frame slot, and does not reliably COPY-OUT a
> fresh, correctly-lived copy when that aggregate crosses a conveyance boundary — a match/if
> merge block-arg, a slice self-append, a return past the slot's region, or a push in a
> loop.**

Direct evidence this is a real structural weakness, not a guess: the `bulk` native verdict
note already records the honest-stop *"a push whose element is an AGGREGATE needs slice
elements held BY VALUE — this backend's aggregate value is the address of a per-INSTRUCTION
frame slot, so a push in a loop would store the same address every iteration"* (3 fixtures).
Fixes #1 (self-append region), #2 (free-old UAF), and the #4 re-lower are each a *point patch
of one such boundary*. type_match (merge+return) and frame_sweep_inst (if-merge + in-place
`[]bool`) are two more boundaries of the same weakness. push_inst_block is the self-append
boundary #1 already patched, at a new site.

**But:** (a) a general copy-out-at-conveyance discipline is a structural change, explicitly
NOT byte-preserving for the C route (the C route relies on C's own value semantics and would
not change), so it cannot be validated by the byte-identical fixpoint the point-fixes enjoy;
(b) it is UNPROVEN from static reading — the three faults were not disassembled under this
budget. **Committing to it now would be exactly the "false systemic root" the task warns is
worse than an honest grind.** So it is recorded as a HYPOTHESIS to confirm, not a plan to
execute blind.

---

## 4. Recommended plan

**Grind — but a SMALL, ORDERED, CHEAP-PINNED grind, using the exact method that cracked #4
(minimal repro under `genB` native + `objdump`/core backtrace), NOT a full ladder per fix.**

### 4.1 Crumb sequence (each independently gate-able)

1. **C1 — Pin `push_inst_block` (cheapest, most isolated).** Minimal int-result-match
   project already exists in the last agent's scratch. Rebuild only that one tiny program with
   `TEKO_BACKEND=native` under `TEKO_NATIVE_PUSH_REDZONE` (the wild-write tripwire named in
   lir.tks:548) + `setarch -R`. Confirm the corrupt `b.insts.len` origin: the `append_inst`
   in-place index-assign vs an upstream `alloc_block`/merge writer. Gate: redzone fires at a
   named write. Expected family: #1/#2 self-append. Ritual: NONE yet (repro only).

2. **C2 — Pin `frame_sweep_inst`.** Minimal no-match project. Core-dump backtrace +
   `objdump -d` around `frame_sweep_inst`. Decide between: (a) `FrameSet` returned through the
   `return if …` merge is a reused slot; (b) the in-place `[]bool` `fs` aliases; (c) boxed
   multi-member payload bind (`fa`/`j`/`br`) reads a wrong offset. Gate: the faulting load
   is attributed to one of (a)/(b)/(c).

3. **C3 — Pin `type_match`.** Minimal variant-returning-match project. Same objdump method,
   focused on the `match_join_anchor`/`adopt_match_arm_literals` merge (typer.tks:3982) and
   the `type_join` fold, to confirm aggregate-through-merge vs a distinct checker path.

4. **C4 — Cross-check the deep root.** With C1-C3 pinned, test the hypothesis of §3: are ≥2
   of the faulting boundaries the SAME copy-out omission? If yes → design one
   materialize-at-conveyance helper and re-classify C1-C3 as its instances. If no → each gets
   its own #1-style point-fix.

5. **C5..Cn — Fix, one crumb per pinned blocker,** in order push_inst_block → frame_sweep_inst
   → type_match (cheapest/most-isolated first; each is the by-address family, so each fix is a
   materialize/park-safety patch localized to one boundary, C-route untouched, byte-preserving
   for the C fixpoint). Ritual point: after EACH fix, the full native self-host ladder
   (genB→gen2→gen3) is the gate — but run it ONCE per fix, not per crumb, and only after the
   cheap repro is green.

6. **Cn+1 — RE-PROBE for the "further bugs behind."** The func_type note warns "further
   pre-existing native codegen bugs sit behind" these three. After C5..Cn clear, re-run the
   ladder; expect 0-3 more of the same by-address family to surface (each cheap-pinnable by
   the same method). Do NOT assume gen2==gen3 is reached until a full ladder is green.

### 4.2 Honest size of the remaining work
- **Distinct fixes to clear the CURRENTLY-PINNED set: 3** (one per blocker), all in the
  by-address-aggregate family, all C-route-byte-preserving.
- **Realistic total to gen2==gen3: ~4-6 distinct point-fixes**, allowing for the 1-3
  "behind" bugs the note flags. This is a GRIND, not a one-shot — but a cheap grind: each
  fixed by minimal-repro + objdump (~the #4 effort), NOT a fresh 2h ladder per attempt.
- **If C4 confirms the deep root:** the count could collapse — one copy-out-at-conveyance
  change could subsume type_match + frame_sweep_inst (+ retro-simplify #1). That is the only
  path to a genuinely *systemic* close, and it is gated on C1-C3 proving the shared boundary.
  Do not bank on it until pinned.

---

## 5. What the native backend structurally CANNOT-yet-do (is "cannot lower an arbitrary
program" hyperbole?)

**Not hyperbole — it is literally measured.** Two independent memory notes quantify it:

- `docs/memory/mapa-native-6-pernas-0.3.1.0.md`: **26 distinct LOWERING honest-stops**
  (N2), byte-identical across all six targets — e.g. `u64 | null has no single PrimKind`
  (18×), null-union-wrapper-placement (4×), fat-pointer interface-dispatch result.
- `docs/memory/bulk-native-verdicts-0.3.1.md`: over 203 fixtures, **84 KNOWN-STOP + 21
  KNOWN-WRONG + 20 BLOCKED**; the weighted gap list includes `str` comparison-chain (17×),
  integer-operator (8×), reference deref-assignment (6×), fat-pointer interface-dispatch (5×),
  `null` match pattern (3×), and the structural **"aggregate held BY VALUE in a push"** note
  (3×) that is the same weakness as §3.

**Crucial scoping for the fixpoint decision, though:** these N2 honest-stops do **NOT** block
gen2==gen3. For `genB`(native) to have BUILT `gen2` at all, it must have lowered the ENTIRE
compiler source with zero N2 stops — the compiler's own source is written in the
native-lowerable subset. The three pinned blockers are RUNTIME crashes/OOM of correctly-
*reached* code (KNOWN-WRONG), not honest-stops. So:

- **The fixpoint critical path = the by-address-aggregate crashes only** (the 3 pinned + any
  "behind"). The §4 grind reaches gen2==gen3.
- **The 26+ N2 honest-stops are a separate, larger frontier** ("compile arbitrary programs",
  the `.32` work list), NOT gated by the fixpoint and NOT to be conflated with it. The
  ONE overlap is the "aggregate BY VALUE in a push" structural item, which is the same deep
  root as §3 — fixing that root would advance BOTH frontiers, which is the one argument for
  attempting the systemic route once C1-C4 pin it.

---

## Executive summary (for relay)

**Model:** every native variant value is a uniform 24-byte `{i32 tag@0; ptr@8; i64 len@16}`
wrapper whose tag is the DECLARED member index on both the construct and the read/dispatch
sides; bug #4's only defect — an un-retagged match/if arm tail constructing into the checker's
arm-ORDER inferred union, so `tag = arm position` — is closed by `arm_join_retag_into` and no
tag==position site remains on the scalar placement path.

**Root classification:** the three remaining blockers do NOT share #4's arm-tag root.
`type_match` and `frame_sweep_inst` are the *by-address aggregate conveyance* family (an
aggregate — `TExpr`/`[]TArm` / `FrameSet`/`[]bool` — carried through a match/if merge or
returned/mutated-in-place past its frame slot), i.e. siblings of fixes #1/#2, not #4;
`push_inst_block`'s OOM is a corrupt slice-header in the LBlock self-append loop, also the
#1/#2 family, with "int-result match" being only the trigger volume.

**Systemic fix?** No single byte-preserving point-fix closes all three, and re-touching #4
closes none. There is a plausible DEEP root (aggregates are frame-slot addresses never copied
out at conveyance boundaries) that could unify ≥2 of them with the prior fixes, but it is
non-byte-preserving and UNPROVEN — committing to it now is the "false systemic root" trap.

**Remaining work:** a CHEAP grind — pin each of the 3 by minimal-repro + objdump (the method
that cracked #4, not a 2h ladder each), fix cheapest-first (push_inst_block → frame_sweep_inst
→ type_match), then re-probe for the 1-3 "behind" bugs the func_type note flags. Realistic
total to gen2==gen3: **~4-6 distinct point-fixes**, all C-route-byte-preserving — unless a
cross-check confirms the deep root, which is the only path that could collapse the count.
The "cannot lower an arbitrary program" claim is REAL (26 measured N2 honest-stops) but does
NOT gate the fixpoint (the compiler's own source stays in-subset); it is a separate, larger
frontier that shares exactly one item (aggregate-by-value push) with the fixpoint's deep root.
