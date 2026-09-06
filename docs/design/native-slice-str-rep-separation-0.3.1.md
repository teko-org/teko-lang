# #112 — Native slice/str fat-rep separation (`str` 2-word, `[]T` 3-word) — crumb plan (0.3.1)

Status: **DESIGN (architect, 2026-08-06).** Base `fix/retirement`. Read-only + design-doc only; no
product code here. This turns issue #112 (the native-rep remainder the F3 plan defers to — see the
fail-loud at `src/lir/lower.tks:11114` and the annotation at `src/lir/lir.tks:293`) into an ordered,
gate-able crumb sequence with a quantified blast radius and a precise fixpoint seam.

Reconciled with `docs/design/f3-array-cap-len-plan-0.3.1.md` (F3 = the C-path rep that already
landed) and `docs/memory/raiz-comum-dos-degraus-0.3.1.0.md` (the R0/R2 width-authority unification
this plan pays off) and `docs/memory/achatamento-de-n2-plano-0.3.1.md` (AL-Wave; #112 unblocks AL3
native).

**#112 is the keystone**: one native-rep change that simultaneously (1) fixes the native fixpoint
crash (gen2==gen3), (2) unblocks F3-native's `grow_inplace` (removes the fail-loud), and (3) is the
precondition for De-C (removing all C emission — which cannot start until the native fixpoint holds).

---

## 0. The rep decision (the answer, law-first)

**`str` stays a 2-word `{ptr,len}` fat (16 bytes). `[]T` becomes a 3-word `{ptr,len,cap}` fat
(24 bytes), `cap` at offset 16.** No str-widening (that is a different, larger ABI change; `str` has
no `cap`). The two predicates that already discriminate at the TYPE level — `is_str_type` (exactly
`str`, `lower.tks:2406`) and `is_fat_type` (`str` OR `[]T`, `lower.tks:10838`) — become the width
selector; the gap #112 closes is the native LOWERING that today collapses both to one uniform
`fat_slot_bytes()=16`.

**The load-bearing sub-decision: `cap` lives in STORAGE, not in the SSA value form.** `LoweredFat =
{ ctx, ptr, len }` (`lower.tks:10848`) — the two-VReg value currency a fat expression threads
through — STAYS two VRegs, for both `str` AND `[]T`. `cap` is written into and read out of the
memory SLOT; it is CONSUMED by exactly one operation, `tk_slice_grow_inplace`, which takes the
slot's ADDRESS (via F1's `ref` → `lower_addr_of_place`), so it never needs a third threaded VReg.

Why cap-in-storage-not-in-value (this is what keeps the blast contained):
- `LoweredFat` is threaded through the whole backend (603 `slice`/`.len` refs in `lower.tks`). A
  third VReg would touch every one. Keeping it two-VReg confines the change to the STORAGE width
  authorities + the one grow primitive + the slice-element stride.
- It matches the sole consumer. Only `grow_inplace` reads `cap`, and it reads it from memory
  through `ref`. Reads (`.len`, `x[i]`, spread) need only `ptr`/`len`.
- It is fixpoint-cheap: the value-threading LIR is byte-identical; only slice STORAGE layout widens.

**`cap` is carried ONLY in GROWABLE locations** — this is a per-LOCATION-KIND rule, not purely
per-type:
- CARRIES cap (24-byte, `cap@16`): a `mut x: []T` frame slot; a `[]T` slice ELEMENT inside a slice
  buffer (`[][]T`, so stride is consistent and `grow(ref outer[i], v)` is representable); a `[]T`
  FIELD of a struct; a `[]T` CONST array member.
- DOES NOT carry cap (stays cap-less, 2-word image): a slice stored as a VARIANT PAYLOAD (the
  wrapper is a FIXED 24-byte `{tag@0, ptr@8, len@16}` — `variant_wrapper_bytes()=24`,
  `lower.tks:9622` — with NO room for a cap word, and a variant payload is never `grow(ref …)`-able
  in place); a fat function PARAMETER (passed as a register pair). These are value copies; reads
  need only `ptr`/`len`.

**The cap seed rule (soundness, preserves value semantics).** Any store of a slice VALUE into a
cap-carrying slot writes `cap = len` by default: the stored value's backing is only KNOWN to hold
`len` elements, so `len == cap` forces `grow_inplace` to realloc on the next append (never
overwriting a possibly-shared backing buffer). Only backing-OWNING constructors (`emit_array_lit`,
`tk_slice_with_cap`, and the const materializer whose literal is immutable so `cap == count`) seed
`cap > len`. This is the SAME aliasing law F3 §8 legislates (`is_unique_at` — in-place only via
exclusive `ref`; value-form copies preserve): #112 must not violate it, and the `cap = len` default
is how the native store obeys it by construction.

**Consequence: `fat_slot_bytes()` becomes a single per-type width oracle** `fat_value_bytes(t)`
(the name the raiz-comum R0 sketch already reserves, `raiz-comum-dos-degraus-0.3.1.0.md:540-546`):
`is_str_type(t) → 16`, slice → 24. Every one of today's THREE independent "16" authorities routes
through it (they agree at 16 today only by arithmetic coincidence — the raiz-comum finding).

**No law tension ⇒ no HALT.** `str`-stays-2-word is the owner's inviolable constraint; the
cap-in-storage decision is pure engineering, resolved by the minimize-blast + preserve-value-
semantics laws. The one honesty caveat (the crash mechanism) is handled by making the diagnosis a
pre-registered, falsifiable gate (crumb N112.0), not an assertion.

---

## 1. Blast radius — every site that assumes uniform `fat_slot_bytes()=16`

Three "16" authorities exist today, one predicate discriminates already, and the value form is
untouched. Classification: **(a)** must become slice-aware (24 for `[]T`, 16 for `str`); **(b)** must
stay 16 (`str`-only, or a fixed wrapper with no cap room); **(c)** already type-discriminated —
only the width needs plumbing through the unified oracle.

| # | Site | File:line | Class | What changes |
|---|---|---|---|---|
| A1 | `fat_slot_bytes()` — width authority #1 | `lower.tks:12186` | (a) | becomes `fat_value_bytes(t)`; 16 for str, 24 for slice |
| A2 | `field_layout_size` — width authority #2 (`Ptr*2`) | `lower.tks:15930` | (a) | slice field → 24 via the oracle; str field stays 16 |
| A3 | `error_field_size_of` — width authority #3 (`Ptr*2`) | `lower.tks:12925` | (b) | `error` fields are ALWAYS `str` → stays 16; route through oracle for uniformity, value unchanged |
| A4 | `elem_byte_stride` fat branch | `lower.tks:13571` | (a) | `[]str` element 16, `[][]T` element 24 — the aliasing-critical stride |
| A5 | `elem_byte_align` fat branch | `lower.tks:13585` | (c) | ptr-align (8) unchanged either width |
| A6 | `alloc_fat_slot` / mutable-local frame slot | `lower.tks:12198` | (a) | slice `mut` local slot 24; str `mut` local slot 16 |
| A7 | `store_fat_slot` / `load_fat_slot` | `lower.tks:12215` / `12231` | (a) | generalized to `store_fat_image`/`load_fat_image`; slice store writes cap=len@16, load ignores cap |
| A8 | `guarded_index_addr` stride for fat elem (index read) | `lower.tks:13607` | (a) | uses `fat_value_bytes(elem)` — 24 for slice-of-slice indexing |
| A9 | `lower_fat_element_push` item slot + `esz` | `lower.tks:11237,11240` | (a) | the fat-element push temp slot + runtime `sizeof` → 24 for a slice element |
| A10 | `const_fat_array_image` stride + `FatMemberImage` slot image | `lower_const.tks:401`, `lir.tks:302` | (a) | const `[][]T` member slot 24 (8 zero ptr + 8 count + 8 cap=count); const `[]str` member stays 16 |
| A11 | const-size guards ("a fat slot needs `fat_slot_bytes()`") | `lower_const.tks:911,939,975,1001,1023` | (a) | the "needs 16" literal becomes "needs `fat_value_bytes(field_type)`" |
| A12 | `store_fat_variant_payload_pair` + `variant_payload_len_offset` | `lower.tks:9796`, `9642` | (b) | variant wrapper is fixed 24-byte `{tag,ptr,len}`; a slice payload stays cap-less — UNCHANGED |
| A13 | `bind_fat_case_payload`, `lower_fat_field` (hand-rolled fat image r/w) | (raiz-comum R2 list) | (c) | folded onto `store_fat_image`/`load_fat_image` in N112.2; str/slice by type |
| A14 | `LoweredFat` value form + its ~603 threading refs | `lower.tks:10848` | (b) | UNCHANGED — two VRegs (ptr,len) for both; the decision that keeps the blast small |
| A15 | The SEAM: `teko_rt.{c,h}` slice helpers + `tk_slice_elem_box` | `teko_rt.c:4545`, `.h:1293` | (a)/(c) | `esz` widths corrected by A2/A4; `tk_slice_grow_inplace` is the new C symbol (N112.4, F3.3-owned) |

Quantified: **3** width authorities (A1–A3) collapse to **1** oracle; **~10** `fat_slot_bytes()`
call-sites in `lower.tks` + **4** in `lower_const.tks` (the `Grep` census: 68 total refs incl. tests
and doc-comments, of which the LIVE authorities are the ~14 above); **2** hand-rolled fat-image
sites (A13) folded; **1** value form (A14) deliberately untouched; **1** maintained C seam (A15).
The `str`-only surface (A3, `error`; A12, variant payload) is class (b) and stays 16 by
construction. **There is NO `.cap` native read site today** (`Grep` for `.cap`/`cap_of` in
`src/lir` → none): the `.cap` surface accessor is F3.4's teaching, not #112's — #112 only lays the
cap word down.

**What breaks if a slot is 24 but a site still assumes 16 (the aliasing failure modes):**
- A4 not widened but store writes 24 → each `[][]T` element overwrites the next element's low 8
  bytes → corrupt ptr/cap → wild pointer → SIGSEGV in `tk_slice_elem_box` or the index load.
- A2 not widened but a `[]T` struct field stored 24 → overruns into the next field.
- A6 not widened but `grow_inplace` writes cap@16 → stack-frame corruption.
This is why the widening is ONE ATOMIC SEAM (N112.3): every cap-carrying authority flips together,
or none. Half-widening is strictly worse than either endpoint.

---

## 2. Ordered crumb sequence (each gate-able alone, bootstrap-safe)

| # | Crumb | Rep state | Behavior | Gate |
|---|---|---|---|---|
| **N112.0** | **Diagnostic — pin the crash.** Reproduce the native self-host SIGSEGV in `tk_slice_elem_box` under `assemble_sel`; capture the fault: which element/stride, the computed layout width vs. the width the store wrote, whether a nested `[][]T`/variant-payload slice is mis-strided. Document the exact mechanism. NO rep change. | inert | — (repro FAILS) | a pre-registered root-cause note + a minimal native probe that reproduces (native exit != 0) — the falsifiable target N112.3 must flip to exit 0 |
| **N112.1** | **R0 — unify the width oracle.** Introduce `fat_value_bytes(t: checker::Type): u32`, returning 16 for str AND (still) 16 for slice — INERT. Route A1/A2/A3/A4/A8/A10/A11 through it. All widths still 16; behavior byte-identical. | inert (all 16) | preserves | C-path fixpoint gen2==gen3 byte-identical + `teko test .`; native builds no-worse than baseline |
| **N112.2** | **R2 — unify the fat image writer/reader.** Generalize `store_fat_slot`/`load_fat_slot` → `store_fat_image(addr, fo, carries_cap, line, col)` / `load_fat_image(addr, line, col)`; fold A7/A13 (variant-payload store stays cap-less, `carries_cap=false`). Still 2-word everywhere. | inert (all 16) | preserves | C-path fixpoint gen2==gen3 + `teko test .`; native no-worse |
| **N112.3** | **ACTIVATE — flip slice width to 24 at the seam.** `fat_value_bytes` returns 24 for slice (str stays 16). Atomically widens A2/A4/A6/A8/A9/A10/A11 for `[]T`; `store_fat_image(carries_cap=true)` writes `cap=len@16`; const slice member image becomes 24 (`cap=count`). Variant payload (A12) and value form (A14) unchanged. **This crumb fixes the crash.** | **2→3 word (slice)** | changes native rep (once) | **RITUAL: native fixpoint gen2==gen3 byte-identical MUST now hold — re-run N112.0's probe → native exit 0** + `teko test .` + peak RSS < 2.5 GiB |
| **N112.4** | **grow_inplace native wire.** Remove the fail-loud at `lower.tks:11114`; lower `teko::list::grow_inplace` onto the now-3-word rep, calling `tk_slice_grow_inplace(hdr, elem, esz, region)` (F3.3's runtime symbol). If that symbol is not yet landed (F3.3 pending), DESIGN-AHEAD: replace the rep-gap fail-loud with a NAMED honest-stop that references the CLOSED rep (`the native rep is now 3-word; blocked only on tk_slice_grow_inplace (F3.3)`), and land the lowering the moment the symbol exists. | 3-word | preserves target | native fixpoint gen2==gen3 + native `grow_inplace` probe (cap!=len slice, in-place hit) |
| **N112.5** | **Regression fixtures + ritual close** (§4). | 3-word | — | **RITUAL: native fixpoint gen2==gen3 byte-identical + `teko test .` + peak RSS** |

**Non-negotiable order:** N112.0 (pin) → N112.1 (oracle) → N112.2 (image r/w) → N112.3 (flip, the
seam) → N112.4 (grow wire) → N112.5 (ritual). N112.1/N112.2 are INERT-REP (introduce the plumbing,
don't use the third word). N112.3 ACTIVATES it (the single atomic seam). N112.4 REWIRES the runtime
consumer. Each runs its gate before the next.

**Bootstrap-safety.** N112.1–N112.2 are byte-preserving refactors; the previous released `teko`
seed builds gen1 unchanged. N112.3 changes the native rep — the seed still builds gen1 (which emits
the OLD 2-word native rep), gen1 builds gen2 (NEW 3-word), gen2==gen3 is the fixpoint (§3). No `src/`
adoption of `[N]T`/`.cap` surface in this load (that is F3.4, a later load).

---

## 3. The fixpoint seam — where the 2→3-word transition is absorbed

Exactly analogous to F3's C-path absorption (`f3-array-cap-len-plan-0.3.1.md` §4), but on the NATIVE
backend, and the native fixpoint is `gen2==gen3`, NOT `gen1==gen2`:

- **gen1** = the #112-aware compiler compiled by the OLD SEED. The seed emits the OLD native rep
  (2-word slices) for the compiler's own body. Transition artifact.
- **gen2** = the source compiled by gen1 — now the native backend EMITS the 3-word slice rep.
- **gen3** = the source compiled by gen2 — also 3-word. **gen2==gen3 (native, byte-identical) is
  the fixpoint.**

The 2→3-word transition is absorbed ONCE, at the gen1→gen2 boundary. Any gen2↔gen3 native diff is a
regression. The seam is N112.3: it is the ONE crumb where the native rep moves, and every
cap-carrying width authority flips inside it atomically (§1's "half-widening is worse"). The gate at
N112.3 is the FULL native ritual — this is the crumb that must make the native self-host, which
crashes today, reach a byte-stable fixpoint.

**Interim gate (N112.0–N112.2, native fixpoint not yet holding):** C-path fixpoint gen2==gen3 +
`teko test .` (the same discipline F3 §4 and CK1' §11 adopt while the native fixpoint is open). At
N112.3 the native fixpoint becomes the primary gate; from there C-path and native must BOTH hold.

**De-C dependency:** the owner's De-C stage (removing all C emission) requires the native fixpoint.
#112 closing N112.3 is precisely the unblock: gen2==gen3 native holding is the green light De-C
waits on.

---

## 4. Regression fixtures (input → expected native exit code)

Repo pattern: `src/<mod>/<mod>_test.tkt` (Teko asserts) + native e2e probes for exit code. Add:

| Fixture | Where | Input | Expected (native) |
|---|---|---|---|
| crash-repro (the N112.0 target) | native e2e probe | the `[]parser::Item` push in `assemble_sel` reduced to a minimal slice-of-aggregate-with-variant-field push | **FAILS before N112.3 (exit != 0 / SIGSEGV); PASSES after (exit 0)** — the falsifiable gate |
| slice-slot-24 | `src/lir/lower_test.tkt` | lower a `mut x: []i64` local; inspect the frame-slot alloca size | slot = 24 bytes; cap word present at offset 16; exit 0 |
| str-still-16 | `src/lir/lower_test.tkt` | lower a `mut s: str` local; inspect the slot | slot = 16 bytes; NO cap word — the str-stays-2-word proof; exit 0 |
| slice-of-slice-stride | `src/lir/lower_test.tkt` | index a `[][]i64` element | element stride 24 (`fat_value_bytes(slice)`); `[]str` element stride still 16; exit 0 |
| slice-field-24 | `src/lir/lower_test.tkt` | a struct `{ xs: []i64; s: str }`; check `field_layout_size` | `xs` field 24, `s` field 16, no overrun; exit 0 |
| variant-slice-payload-caples | `src/lir/lower_test.tkt` | a variant case carrying a `[]i64`; construct + read back | wrapper 24 bytes `{tag,ptr,len}`, cap-less; value reads correct; exit 0 |
| cap-seed-eq-len | native e2e probe | store a slice value into a slot, then `grow(ref x, v)` | grow sees len==cap → reallocs → no shared-backing overwrite; `x[len_old]==v`; exit 0 |
| grow-inplace-native | `src/list/list_test.tkt` + e2e | `mut x=[] (cap seeded > len via with_cap); grow(ref x, a); grow(ref x, b)` | in-place hits (len<cap), no realloc; `x.len==2`, `x[0]==a`,`x[1]==b`; exit 0 (N112.4) |
| const-slice-member-24 | `src/lir/lower_const.tks` test | a `const T: [][]i64 = [[1],[2]]` | member slot 24, `cap==count`; `.len`/`[i]` agree; exit 0 |
| native-fixpoint | ritual | full corpus, native gen2 vs gen3 | **gen2==gen3 native byte-identical** |

---

## 5. Ritual points (full gate)

- **N112.3** (the rep flip) and **N112.5** (the close) are the RITUAL points: the full gate must
  pass — native fixpoint gen2==gen3 byte-identical + `teko test .` + peak RSS < 2.5 GiB.
- N112.0 gates on its documented root-cause + reproducing probe. N112.1/N112.2 gate on C-path
  fixpoint + `teko test .` (native fixpoint not yet available). N112.4 gates on native fixpoint + the
  grow-inplace probe. None advances without its gate green.

---

## 6. Interaction with F3 and AL3

- **Removes the fail-loud at `lower.tks:11114`.** That error explicitly names #112 as the owner of
  "the 3-word `{ptr,len,cap}` header the primitive mutates is the native-rep remainder." N112.3 lays
  the header down; N112.4 lowers `grow_inplace` onto it.
- **Unblocks F3-native.** F3 landed the 3-word rep on the C path (`cg_slice_typename`,
  `codegen.tks:1784`, emits `{ptr,len,cap}`); `str`/`tk_char`/`tk_slice_byte` stay 2-word in
  `teko_rt.h` (the deliberate C-path asymmetry #112 mirrors natively). #112 is F3's native twin: it
  brings the native backend to rep parity WITHOUT moving emitted C bytes (native and C are separate
  backends; the native fixpoint is self-consistency, not C-byte-match).
- **Sequence F3-native → AL3-native.** Once N112.4 lands, `tk_slice_grow_inplace` is callable
  natively; F3.3's `grow(ref x, v)` bridge (`list.tks:17`, today a value-thread write-through) can
  be rewired to the in-place primitive on BOTH paths; AL3 then migrates the ~1383 push-sites to
  `grow(ref x)`, each collecting the O(1) in-place append natively — the ~1.8 GB O(n²) copy-grow
  cure that today runs only on the C path.
- **`tk_slice_grow_inplace` ownership.** The runtime primitive is F3.3's deliverable (its declared
  shape: `void tk_slice_grow_inplace(void *hdr, const void *elem, uint64_t esz, tk_region *region)`,
  `f3-array-cap-len-plan-0.3.1.md` §10). #112 provides the native REP that makes it lowerable and
  the native lowering arm (N112.4). If F3.3 is not yet landed when #112 reaches N112.4, N112.4 is
  design-ahead against that signature (§2).

---

## 7. Risks + resolutions

- **[BIGGEST] The crash mechanism is not statically obvious — the visible aggregate has no direct
  slice field.** `parser::Item = { content: ItemKind; namespace: str; file: str }`
  (`ast.tks:661`) — a variant + two `str`, no direct `[]T`. So "slice-field under-sizing" is NOT the
  self-evident cause; the fault more likely runs through a nested `[][]T` in the parser AST (stride
  16-vs-24 disagreement once the C-path/runtime F3 partial is in the tree, yielding a wild pointer
  `tk_slice_elem_box` dereferences) OR a variant payload whose slice member is mis-strided.
  **Resolution:** N112.0 is a mandatory DIAGNOSTIC crumb that pins the exact mechanism BEFORE the
  rep change, and N112.3's gate is the pre-registered "N112.0 probe now exits 0." If the diagnosis
  reveals an INDEPENDENT bug (not a fat-width/stride mismatch), that is REPORTED up — not folded
  into #112 — and #112's crash-fix claim is re-scoped honestly. The rep separation is the fix
  HYPOTHESIS; the number is the gate, not the intention.
- **A 24-byte slot aliasing a 16-byte assumption (§1's failure modes).** Any un-widened cap-carrying
  authority corrupts neighbors. **Resolution:** N112.3 flips ALL cap-carrying authorities through the
  single `fat_value_bytes` oracle atomically (that is the whole point of the R0 unification in
  N112.1); half-widening cannot occur because there is one width source.
- **The by-address aggregate path (`tk_slice_elem_box`).** It memcpy's the OUTER aggregate's
  whole-type width (shallow, `lower.tks:8438`). Widening a slice FIELD (A2) enlarges that width for
  any slice-containing aggregate; the `esz` must track it. **Resolution:** A2 (`field_layout_size`)
  and the struct layout sum drive `esz` from the same oracle — the box copy width follows
  automatically. Fixture `slice-field-24` + the crash-repro probe verify it.
- **cap-in-object + value semantics (F3 §8 aliasing hole).** A copied slice shares its backing; an
  in-place grow of the original must not let a stale copy read/write with a stale cap. **Resolution:**
  the cap-seed rule (§0) — every value store seeds `cap=len`, so a copy always forces realloc on its
  next in-place attempt; in-place spare-capacity use exists ONLY through F1's exclusive `ref`
  (`is_unique_at`). #112 does not introduce a new law here; it obeys F3 §8.
- **Const materialization.** A const `[][]T` member must be 24 with `cap=count` (immutable literal,
  cap==len). **Resolution:** A10/A11 widen the const member image + the const-size guards through the
  same oracle; `const_image_absorb`'s pad-to-`fat_slot_align` (`lower_const.tks:483`) already
  handles a non-uniform member width by construction (its doc-comment anticipated exactly this).
- **Places str and slice share a code path today.** `is_fat_type` selects one lowering path for
  both (A14 value form; A7 store/load; A4 stride). **Resolution:** the value form (A14) STAYS shared
  and 2-VReg — no split. Only the WIDTH authorities (A1–A11) gain the `is_str_type` discriminator
  they already have available; the shared code path is preserved, one width branch added.
- **The maintained-C exception (`teko_rt.{c,h}`).** #112 touches it only for `tk_slice_grow_inplace`
  (F3.3-owned, a genuine runtime primitive — the LIR has no realloc op, the same reasoning
  `tk_slice_elem_box` already uses) and, if N112.0 proves it, a minimal `esz`/stride correction in
  `tk_slice_elem_box`. Every C line is justified by "the native LIR cannot express reallocation/
  memcpy." `tk_str`/`tk_char`/`tk_slice_byte` in `teko_rt.h` STAY 2-word (str-no-widen; the two
  pre-declared runtime byte-slice views are the C-path's own cap-less exception, mirrored natively).

**No genuine law tension ⇒ no HALT.** The only design decisions (cap-in-storage; cap carried only in
growable locations; cap=len seed) are engineering, resolved by minimize-blast + preserve-value-
semantics + str-no-widen. The single honesty risk (crash mechanism) is converted into a falsifiable
gate, not an unresolved question.

---

## 8. Files touched (implementer roadmap)

- `src/lir/lower.tks` — `fat_value_bytes` oracle (A1, replaces `fat_slot_bytes`); `field_layout_size`
  (A2); `error_field_size_of` routed but str-only (A3); `elem_byte_stride`/`elem_byte_align` (A4/A5);
  `alloc_fat_slot`/`store_fat_slot`→`store_fat_image`/`load_fat_slot`→`load_fat_image` (A6/A7);
  index stride (A8); `lower_fat_element_push` slot+esz (A9); variant-payload store folded cap-less
  (A12/A13); remove the fail-loud at 11114, lower `grow_inplace` (N112.4).
- `src/lir/lower_const.tks` — const fat-array member image 24 for slice, `cap=count` (A10); const-size
  guards through the oracle (A11).
- `src/lir/lir.tks:302` — `FatMemberImage` doc/shape note: slice member image is 24, not 16.
- `src/runtime/teko_rt.{c,h}` — [maintained-C exception] `tk_slice_grow_inplace` (N112.4, F3.3-owned);
  a minimal `tk_slice_elem_box` esz/stride correction ONLY if N112.0 proves it needed. `tk_str`/
  `tk_char`/`tk_slice_byte` UNCHANGED (2-word).
- `src/list/list.tks:17` — the `grow` bridge is rewired to `grow_inplace` by F3.3 (coordinate; #112
  provides the native lowering the rewire relies on).
- Fixtures §4 in `src/lir/lower_test.tkt`, `src/list/list_test.tkt`, `src/lir/lower_const.tks` test +
  native e2e probes.

---

## 9. Report — executive summary for owner ratification

- **Rep decision:** `str` stays 2-word `{ptr,len}` (16); `[]T` becomes 3-word `{ptr,len,cap}` (24,
  cap@16). `cap` lives in STORAGE only — the SSA value form `LoweredFat` stays two VRegs (ptr,len)
  for both; cap is carried ONLY in growable locations (mut local slot, slice buffer element, struct
  field, const member), NOT in variant payloads (fixed 24-byte wrapper) or fat params. Cap seed
  rule: value store seeds `cap=len` (forces realloc — value-semantics-safe); only backing-owning
  constructors seed cap>len. The three "16" width authorities collapse to ONE per-type oracle
  `fat_value_bytes(t)` (paying the raiz-comum R0/R2 debt).
- **Crumbs (6, ordered):** N112.0 pin-the-crash (diagnostic) → N112.1 unify oracle (inert) → N112.2
  unify fat-image r/w (inert) → **N112.3 flip slice to 24 (the atomic seam — fixes the crash)** →
  N112.4 grow_inplace native wire (removes the 11114 fail-loud) → N112.5 fixtures + ritual.
- **Fixpoint seam:** native gen2==gen3 byte-identical; the 2→3-word transition absorbed ONCE at
  gen1→gen2, entirely inside N112.3. Interim gate C-path fixpoint until N112.3; native fixpoint is
  the primary gate from N112.3 on (and the exact unblock De-C waits for).
- **Biggest risk:** the crash mechanism is not statically obvious — the crashing aggregate
  (`parser::Item`) has NO direct slice field, so "slice-field under-sizing" is unproven; the fault
  likely runs through a nested `[][]T` stride or a variant-payload slice mismatch. Mitigated by
  making N112.0 a mandatory diagnostic whose reproducing probe is N112.3's pre-registered
  pass/fail gate — the rep change's crash-fix is falsifiable, not asserted; an independent bug found
  there is reported up, not absorbed.
