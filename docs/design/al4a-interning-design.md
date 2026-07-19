# AL4a — Mangled-name cost removal: design reconciliation (proof-first, rev 2)

Status: design-ready. Author: architect. Date: 2026-07-19.
Scope: reconcile AL4a against codegen reality after two owner ratifications + the
"compute-once-as-literal, no overhead" principle refinement.
Rule: this doc is a PLAN. No product code written. Every count/claim below was re-audited by
the architect against real signatures/structures (file:line proven, not estimated).

Owner ratifications folded in:
1. Cache KEY = pure `u64` structural hash; `Map<str>+hex` REJECTED. (Now moot — see below.)
2. Emitted C name MAY be opaque/numeric. (Turns out: NOT NEEDED — see §4.)
3. Principle: "if I know the value I'm converting, treat it as a PRE-CONVERTED LITERAL at
   compile time, no overhead — assign once, every site just READS O(1); no hash, no lookup,
   no recompute."

---

## 0. The reframe (what the audit forces)

The AL4a issue was written as "memoize the descriptive string with a hash+lookup cache." The
audit shows that framing is BOTH too expensive (108-fn threading) AND misaligned with the
owner's principle (a hash+lookup is itself the overhead he wants gone). The real finding:

**The 99 MB is not a missing cache — it is throwaway-string materialization.** The producers
build a `str` on the heap only to (a) append it to an output buffer, or (b) compare it to
another string. The fix is not to CACHE the throwaway string; it is to STOP BUILDING IT:
write the characters straight to the output buffer, and compare types structurally. This
needs ZERO new threading for the dominant case, keeps names readable, and is exactly the
owner's "no overhead, treat as literal write" principle.

Proof of the anti-pattern, side by side:
- `mangle_type_name` (codegen.tks:364) — a **Named** type's C name: `cb(buf,"tk_t_")` +
  `cb_tysym(buf,name)`. Appends into the existing `[]byte` buffer. **Zero string allocation.**
  Named types ALREADY satisfy the owner's principle (name carried on the node, O(1) append).
- `cg_variant_typename` (buf form, codegen.tks:1210) — an **inline variant**'s C name:
  `cb(buf, cg_variant_typename_str(v))`. It calls the STR form, which allocates a fresh `str`
  via a concat chain, THEN appends it and throws it away. **One+ heap string per call.**
  This single line, reached ~5.6M times, is the bulk of the 99 MB.
- `cg_type_mangle_eq` (codegen.tks:4030): `cg_opt_key(a) == cg_opt_key(c)` — builds TWO
  throwaway strings just to compare them (same anti-pattern `qualify_eq` already killed in the
  checker's 63M-concat hot scan, resolve.tks:20).

---

## 1. Proven reachability facts (the owner's a/b/c, answered)

**(b) id-field on the `Type`/`Variant` node — IMPOSSIBLE.** `checker::Type` is a `variant`
(a VALUE tagged union, type.tks:93), copied by value and frequently SYNTHESIZED FRESH in
codegen (e.g. `checker::Variant { members = ... }` at cg_variant_key:6989). A node has no
stable identity; a mutable `id`/`mangled` field would not survive copies and would be absent
on fresh nodes. Achieving node-carried ids would require interning the entire Type
representation into reference cells in the CHECKER — a cross-phase refactor far larger than
anything in this wave. Ruled out.

**(a) index-in-registry, read O(1) at emit sites — NOT REACHABLE without threading.**
`CgTypeSet` (codegen.tks:6955) is a VALUE `struct` threaded ONLY through the
collection/ordering phase: every function that carries `set` is a `cg_collect_*` /
`cg_*_set_add` / `cg_type_ready` / `cg_emit_types_ordered` (all grepped). The HOT value-emit
callers of the producers carry `prog`/`buf` but NOT `set` — proven by signature:
`emit_type(buf, prog, t)` (:944), `emit_as_r(buf, prog, ...)` (:4070),
`cg_var_mkey(prog, v, i)` (:3579), `cg_wrap_open(buf, v, mkey)` (:3667),
`cg_emit_tag_prefix(buf, variantT)` (:4299), `cg_emit_variant_ctype(buf, v)` (:1216).
So an index living in the registry is invisible at the 6.65M reference sites; making it
visible = threading the registry to ~108 functions (the transitive caller set I counted in
rev 1: 92 to reach the giant, 108-112 for all producers, out of 268).

**No reference-semantics accumulator exists in codegen.** `CgTypeSet`, `CgEmitted` (:7317),
`DeferCtx` (:5315), `CovCtx` (:65), `RegionFrame` (:5282) are ALL value `struct`s (grepped:
zero `= class` in codegen.tks). `prog` is an immutable checker VALUE. So there is nowhere to
hang a zero-threading mutable memo — confirming a hash+lookup cache would cost the full
108-fn threading. **This is precisely why (c) is the WORST option and the owner rightly
deprioritized it.**

**Where the 99 MB actually splits (call-graph proven):** `cg_variant_typename_str` is called
ONLY from `cg_variant_typename` (buf, :1211). That buf form is called from 8 sites; 7 pass a
REAL output buffer (emit_type:984, cg_emit_variant_ctype:1219, cg_emit_inline_variant_typedef:
3671, cg_emit_types_ordered:7278/7299, :7591/7593) and only ONE — `cg_variant_key` (:6989) —
passes `empty()` to harvest a `str` dedup KEY. So of the 6.65M:
- **~5.6M are buffer-appends** that never needed a `str` at all → fixed by direct-write, ZERO
  threading (§2, crumb 1).
- **~1.03M are dedup KEYS** (`cg_variant_key`), and they live in the collection phase where
  the registry IS reachable → fixed by caching the key in the registry, registry-local
  threading only (§2, crumb 3).

Same shape for `cg_opt_key` (2.85M): part is emit-site comparison via `cg_type_mangle_eq`
(:4126, :4245) → structural compare, no string (crumb 2); part is collection-phase dedup keys
(`cg_opt_set_add`/`cg_slice_add`/`cg_type_ready`) → registry-cached (crumb 3).

---

## 2. Decision — the cheapest collision-safe scheme, ranked by the owner's principle

**Do NOT build a per-build hash cache and do NOT thread 108 functions.** Remove the string
materialization at its three sources, in this order:

### Crumb AL4a-1 — direct-write the structural type name (kills ~84% of the 99 MB). Size S.
Reimplement the BUFFER forms `cg_variant_typename` (:1210), `cg_member_key` (:1191), and
`cg_opt_mangle` (buf form) to append characters straight into the `[]byte` output — exactly
as `mangle_type_name` does for Named — recursively, WITHOUT ever constructing an intermediate
`str`. The STR forms (`cg_variant_typename_str`, `cg_member_key_str`, `cg_opt_mangle_str`)
remain, used only where a genuine `str` is still needed (the dedup keys, crumb 3).
- Threading: **NONE.** Same signatures; only the bodies change (str-build+append → append).
- Collision-safety: **trivially preserved** — byte-identical output, so identical C names.
- Principle fit: **exact** — "write the literal to the output, no throwaway, no overhead."
- Owner's opaque-name ratification: **NOT SPENT.** Names stay readable (`tk_u_i32_error`) for
  free; the win is not allocating, not shortening. Keep the readable C. (Opaque numeric ids
  remain an OPTIONAL future lever only if name LENGTH ever matters — not needed for perf.)

### Crumb AL4a-2 — structural mangle-equality without strings (kills cg_opt_key's emit share). Size S.
Replace `cg_type_mangle_eq(a,c) = cg_opt_key(a) == cg_opt_key(c)` (:4030) with a byte-free
predicate `cg_mangle_eq(a, c) -> bool` that decides "do these two types mangle to the same C
name" by folding their structure directly (the twin of `qualify_eq`). NOTE — this is NOT
`type_eq`: two DISTINCT types can mangle to the SAME C name (the `name_last_segment` bare-key
case, #109, is why `cg_type_mangle_eq` exists), so the predicate must mirror the MANGLE, not
nominal identity. Removes the two throwaway strings at the emit-narrowing sites (:4126,:4245).
- Threading: **NONE.** Local rewrite of one predicate.
- Collision-safety: preserved (decides identical-mangle exactly as the string compare did).

### Crumb AL4a-3 — cache the dedup KEY in the registry (the owner's "assign once, read"). Size M.
For the residual `str` needs — the dedup scans in the collection phase — store each registered
type's mangled key IN `CgTypeSet` at INSERTION time, so scans READ it instead of recomputing.
Add parallel key fields:
```teko
/**
 * B-cg2 — the codegen registry of distinct anonymous types needing a typedef, each stored
 * ALONGSIDE its already-computed mangled key so a dedup scan READS the key (O(1)) rather than
 * recomputing it (the owner's "assign the converted value once, then only reference it"). The
 * keys are collision-safe by construction: the SAME mangled string already used for dedup, so
 * two types that mangle equal still collapse to one typedef (mangle-eq, not type-eq).
 */
type CgTypeSet = struct {
    inners: []checker::Type;   opt_keys: []str
    slices: []checker::Type;   slice_keys: []str
    variants: []checker::Type; variant_keys: []str
}
```
`cg_opt_set_add` / `cg_slice_add` / `cg_variant_add` compute the key ONCE (via the STR form)
when pushing a new type and push it to the parallel `*_keys`; the dedup scans and
`cg_*_emitted_key` / `cg_type_ready` compare against the stored key. This is option (a) — a
per-build registry with a pre-assigned identifier read O(1) — applied EXACTLY where it is
naturally reachable (the collection phase already threads `set`).
- Threading: **registry-local only** — the ~8 collection functions that ALREADY carry `set`
  (`cg_opt_set_add`:6963, `cg_slice_add`:6975, `cg_variant_add`:6995, `cg_collect_type_opts`:
  7008 and siblings, `cg_opt_emitted_key`:7370, `cg_uvar_emitted_key`:7383, `cg_type_ready`:
  7395, `cg_emit_types_ordered`:7511). **NOT the 108 value-emit functions.**
- Collision-safety: preserved — the stored key is the identical string used today.

### AL4a-4 — `checker::qualify` — SEPARATE, DEFERRED. Size S (own crumb, own wave).
Different phase (~24 call sites across typer/collect/resolve/borrow/di), and its hot scan was
already fixed by `qualify_eq` (resolve.tks:20). Residual `qualify` calls mostly build
`Named{name=...}` that is CONSUMED, so there is little throwaway to remove. Lowest ROI (17 MB,
smallest of the four). Report up as a follow-up; do not bundle here.

---

## 3. Honest size verdict — L collapses to S + S + M

| crumb | what | threading | size |
|-------|------|-----------|------|
| AL4a-1 | direct-write structural name (buf forms) | **none** | **S** |
| AL4a-2 | string-free mangle-equality predicate | **none** | **S** |
| AL4a-3 | cache dedup key in `CgTypeSet` | registry-local (~8 fns already threading `set`) | **M** |
| AL4a-4 | `checker::qualify` (deferred, separate) | checker-local | S |

The rev-1 conclusion ("L, 108 functions, thread a `CgCtx`") is **withdrawn**. It was an
artifact of the "cache the descriptive string" framing. Once the goal is "stop materializing
throwaway strings," the dominant win (crumb 1) needs no threading at all, and the residual
needs only registry-local threading. **No path requires the 108-fn cascade.** The owner's
principle is not just achievable — it is the CHEAPER design.

---

## 4. Owner's ratifications — net effect

- **u64 key / `Map<str>+hex` rejected:** moot. No hash cache is built at all; there is nothing
  to key. (The rejection was correct — hash+lookup was the overhead to eliminate.)
- **Opaque numeric names allowed:** NOT NEEDED for the perf goal and NOT SPENT by this plan.
  The win is not-allocating, which is orthogonal to name readability. Recommend keeping the
  readable descriptive names (free) unless a future issue specifically wants shorter C
  identifiers. Told to the owner so the ratification budget is preserved.

---

## 5. Reachability question, answered plainly (no path is hidden)

- Is there a per-build distinct-type registry? **Yes — `CgTypeSet`** — but it is reachable
  ONLY in the collection phase, not at the 6.65M value-emit reference sites (proven §1).
- Does that block us? **No** — because the value-emit sites don't NEED the registry: they
  append the name to a buffer, which is done directly (crumb 1) with no key/id/lookup. The
  registry is needed only for DEDUP, which happens where the registry already lives (crumb 3).
- Node-carried id (the ideal O(1)-read-no-lookup)? **Impossible** under the value-`variant`
  Type representation (proven §1) — the only case that has it, Named, already uses it.

**No genuine unresolved tension remains.** Rev 1 flagged a value-variant-vs-compute-once
tension; the reframe dissolves it — the dominant case never needed compute-once, only
stop-recomputing-a-throwaway. Nothing here HALTs; nothing deviates from a ratified principle.

---

## 6. Regression fixtures + ritual

- **AL4a-1 (RITUAL — fixpoint):** self-build must emit BYTE-IDENTICAL C (`gen==gen`), because
  direct-write produces the same characters as build-then-append. Corpus of many repeated
  inline variants / optionals / slices, golden `.c` compare, VM and native, exit 0.
  Dark-matter probe: `cg_variant_typename_str` heap-string allocs drop from ~6.65M to ~1.03M
  (only the dedup-key harvest remains, addressed by crumb 3).
- **AL4a-2:** unit fixtures asserting `cg_mangle_eq(a,c)` agrees with the OLD
  `cg_opt_key(a)==cg_opt_key(c)` across a type matrix INCLUDING two distinct types that mangle
  equal (the #109 bare-key case) — must return true for those. VM and native, exit 0.
  Probe: `cg_opt_key` allocs at the emit-narrowing sites → 0.
- **AL4a-3 (RITUAL — fixpoint):** byte-identical C; dedup behavior unchanged (no duplicate or
  missing typedefs). Probe: `cg_opt_key`/`cg_variant_key` dedup-scan allocs → O(distinct
  types) instead of O(scan steps). VM and native, exit 0.
- If any fixpoint fails, the direct-write / predicate diverged from the string form — HALT and
  reconcile the fold; do not force-land.

---

## Relevant paths
- `/home/user/teko-lang/src/codegen/codegen.tks`
  - `mangle_type_name` :364 — the zero-alloc Named path to MIRROR.
  - `cg_variant_typename` (buf) :1210, `cg_member_key` :1191, `cg_opt_mangle` (buf) — crumb 1.
  - `cg_variant_typename_str` :1197, `cg_member_key_str` :1179, `cg_opt_mangle_str` :1119 — kept for dedup keys.
  - `cg_type_mangle_eq` :4030 (callers :4126,:4245) — crumb 2.
  - `CgTypeSet` :6955; `cg_opt_key` :6959, `cg_variant_key` :6987; set-adds :6963/6975/6995;
    `cg_*_emitted_key` :7370/7383; `cg_type_ready` :7395; `cg_emit_types_ordered` :7511 — crumb 3.
  - value-emit callers proving non-reachability of the registry: :944, :4070, :3579, :3667, :4299, :1216.
- `/home/user/teko-lang/src/checker/type.tks:93` — `Type` is a value `variant` (no node id); `type_eq` :110.
- `/home/user/teko-lang/src/checker/resolve.tks:20` — `qualify_eq`, the byte-free precedent crumb 2 mirrors; `qualify` :14 (deferred AL4a-4).
- `/home/user/teko-lang/docs/design/al1-proof-report.md:43-46` — RA-measured volumes.
- `/home/user/teko-lang/docs/design/al-wave-crumbs.md:112-142` — the original ratified AL4a spec (this doc supersedes its mechanism).
