# #443 — RPO block numbering for the A3 register allocator (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave (umbrella
`remodel/backend-build`). Issue **#443**. Fix branch `fix/issue-443-rpo-numbering`, base the
backend wave umbrella. **0.3.1 backend-completion — must land EARLY, before A4**, because it
unblocks multi-block `if`/`match` register allocation. Grounded in the merged A3 code
(`src/backend/regalloc.tks`, PR #398), the A1 LIR lowering (`src/lir/lower.tks`, `lir.tks`), the
A2 isel (`src/backend/isel_arm64.tks`), and the machine-IR interp (`src/backend/minst_interp.tks`).
Supersedes the KNOWN OVER-APPROXIMATION warning in `docs/design/backend-a3-regalloc.md` §6.2.

> This is a PLAN. It does not write product code. It specifies the crumb sequence, the exact
> new/changed function shapes (in full-Javadoc Teko, copy-paste ready), the regression fixtures,
> the ritual points, and the risks/law tensions, so the implementer resumes in minutes.

---

## 1. The defect (verified by the #384 adversarial review, 2026-07-09)

`has_back_edge` (`regalloc.tks:398-416`) decides "is this a loop?" by the proxy

```
terminator target id  <=  emitting block id      →  back-edge (loop) → honest-stop
```

That is only correct when block ids are **topological** (id order == execution order). A1 lowering
does **not** guarantee this. `alloc_block` (`lir.tks:396`) assigns ids by allocation order
(`id = f.blocks.len`, dense + sequential), and `lower_if_value` (`lower.tks:1959`) allocates the
**merge block before the arm bodies**:

```
let thenb   = alloc_block(ctx.func)      // id N
let elseb   = alloc_block(thenb.func)    // id N+1
let mergeblk = alloc_block(elseb.func)   // id N+2   ← merge id is HIGHER than the arms
```

But when an arm body itself contains an `if`/`match`, the **nested** construct allocates ids
`N+3, N+4, N+5, …` (and isel appends split edge blocks past `max_block_id`,
`isel_arm64.tks:1799-1814`, `selctx_new_block:211`), so a nested arm's tail block gets a **higher**
id than the **outer** merge it targets. `close_arm_to` (`lower.tks:1877`) then emits an `LJump`
that is **forward in the CFG but backward in id**.

Two consequences (both grounded in `regalloc.tks`):

1. **`has_back_edge` FALSE-POSITIVES on acyclic nested control flow.** The forward `LJump`
   (target id ≤ source id) trips the proxy, so `compute_intervals` (`regalloc.tks:653`)
   honest-stops a **valid** `if`/`match`-nested program. Today this is fail-safe (an error, not a
   miscompile) and unreachable (regalloc is not yet wired into a compile path — single-block
   fixtures are what ship), but it blocks A4 the moment multi-block `if`/`match` flows through.

2. **The strict linear numbering itself is INVERTED for such blocks.** `number_insts`
   (`regalloc.tks:31`) flattens `f.blocks` **in list order**, which is **ascending id order**
   (LIR blocks pushed `0..maxLir` by `alloc_block`, then edge blocks appended `maxLir+1..` by
   `selctx_new_block`). For a nested arm, a value **defined** in a high-id block and **used** in a
   low-id merge is numbered def-point > use-point. `virt_interval` (`regalloc.tks:553`) then takes
   `start = earliest def`, `end = latest touch`, so the interval collapses to `[def, def]` and the
   **use falls outside the live range** → the scan can reuse that register for an overlapping value
   → **silent miscompile**. This is deeper than the honest-stop: even bypassing `has_back_edge`,
   basic linear-scan liveness would be wrong.

### 1.1 Worked example (the inversion, concretely)

`let x = if c1 { if c2 { 10 } else { 20 } } else { 30 }` lowers (ids in allocation order) to a CFG
whose **execution** order is entry → arms → nested-merge → **outer-merge (lowest id) LAST**. The
outer-merge param `x` is written by edge moves in the arm/edge blocks (high ids) and read in the
outer-merge block (low id). In **id order** the read is numbered *before* the writes → inverted. In
**RPO** (entry first, merge last) the writes precede the read → monotonic. RPO is the fix for both
the false-positive *and* the inversion in one stroke.

---

## 2. Root cause, in one line

**Block ids are allocation order; liveness numbering and back-edge detection assume topological
order.** The allocator needs a topological (acyclic) linearization of the CFG. The canonical one is
**reverse post-order (RPO)**.

---

## 3. Chosen approach — RPO renumbering as an allocator-internal analysis pass

**Recommended: compute a reverse-post-order block sequence once per function in `regalloc_func`,
and drive both the linear numbering and the back-edge test off RPO *position* (not id).** Do **not**
mutate block ids and do **not** physically reorder `MFunc.blocks`. RPO is a pure numbering/analysis
coordinate; the machine function (ids, block list, branch targets) is untouched.

Why RPO position solves everything:

- **Real back-edge test, allocation-order-independent.** For a **reducible** CFG (the only kind
  Teko's structured lowering produces — `if`/`match`/`loop`, `break`/`continue` target enclosing
  loops; no `goto`), an edge is a **back-edge iff it is *retreating* in RPO**: `pos(target) <=
  pos(source)`. A DAG (acyclic) has **no** retreating edges, so nested `if`/`match` no longer
  false-positives; a real `loop` latch→header edge **is** retreating, so it still honest-stops
  (the A3-loop deferral stays intact).
- **Monotonic liveness numbering.** RPO is a topological order of the acyclic CFG, so numbering the
  instruction stream in RPO order makes every def precede all its uses (§1.1 inversion gone).

### 3.1 Rejected alternative — DFS/dominance back-edge test + separate numbering

You could detect back-edges with a DFS-tree ancestor test (mark edges to a grey/ancestor node) and
*separately* fix the numbering. But the numbering fix **also** requires a topological order — which
**is** RPO. So a separate back-edge analysis is strictly *more* code than reusing RPO positions,
which give exact back-edge detection **for free** on reducible CFGs. **Recommend RPO;** the DFS
ancestor test is redundant given RPO is already computed for the numbering.

Trade-off recorded: RPO's "retreating = back-edge" equivalence holds **only for reducible CFGs**.
Teko cannot express an irreducible CFG today (structured control flow only). If a future construct
could, the retreating-edge test would **conservatively over-report** back-edges → an extra
honest-stop, **never a miscompile** (M.3 honest, fail-safe). This assumption is stated so a future
irreducible-CFG feature knows to revisit it.

### 3.2 Where the fix lives — a pre-pass in `regalloc.tks`, not isel/lower

| Candidate site | Verdict | Reason |
|---|---|---|
| **Pre-pass in `regalloc_func` (`regalloc.tks`)** | **CHOSEN** | Localizes the fix to the consumer. No id change, no block-list mutation, no isel/lower/interp perturbation. Smallest blast radius; ripple contained to `regalloc.tks` + `regalloc_test.tkt` (verified: those three fns have no other src callers). |
| Fix block emission order in isel `select_all_blocks` | Rejected | isel emits in LIR order and appends edge blocks as discovered; reordering mid-selection is complex and perturbs A2 golden dumps for zero allocator benefit. |
| Make `alloc_block`/`lower_if_value` allocate ids topologically | Rejected | Deep A1 change: every LIR/golden dump shifts, wide blast radius across the whole backend, for a problem the allocator can solve locally. |
| `number_insts` computes RPO internally (only) | Rejected as *sole* change | `has_back_edge` **also** needs positions; computing RPO in two places duplicates the DFS. Thread one shared `order`. |

---

## 4. The subtle invariant — targets are IDS; we renumber, we do not reorder

`MBranch`/`MBranchCond`/`MCbz` `.target` fields hold **block IDS** (`minst.tks`), and the interp
follows the CFG **by id** (`find_mblock(f, cur)`, `minst_interp.tks:1093`, seeded from
`f.blocks[0].id`, `:1122`). Because the RPO pass:

- **does not change any `MBlock.id`**, every branch `.target` stays valid untouched;
- **does not reorder `MFunc.blocks`**, `rewrite_func` (`regalloc.tks:1576`) rebuilds blocks in the
  same list order with the same ids — output structure is identical to today except for register
  substitution, so **golden dumps and the interp are provably unaffected**;
- uses RPO **only** to assign program *points* for liveness and to compute *positions* for the
  back-edge test.

**Interp-correctness argument:** the interp is order-independent (id-driven) with the single
constraint `entry = f.blocks[0]`. The pass touches neither ids nor `blocks[0]`, so
`minst_interp(regalloc(sel)) == minst_interp(sel)` continues to hold by construction. (Should a
future variant choose to *physically* reorder into RPO for A4 code layout — a legitimate future
optimization — the **only** invariant to preserve is entry-at-index-0, which RPO guarantees since
RPO always starts at the entry. That variant is out of scope for #443.)

---

## 5. New and changed function shapes (full-Javadoc, copy-paste ready)

All new code lives in `src/backend/regalloc.tks`. Comments are full Javadoc doc-comments per the
W15 law; no inline `//`. The repo convention folds the error case into `@return … | error` (no
`@throws`) — matched below.

### 5.1 New — the RPO analysis (add near `number_insts`, `@since #443`)

```teko
/**
 * u32_in — whether `list` contains `x`.
 *
 * @param []u32 list  the list to search
 * @param u32 x  the value to find
 * @return bool  true iff `x` is present
 */
fn u32_in(list: []u32, x: u32) -> bool {
    mut i: u64 = 0
    loop {
        if i >= list.len { break }
        if list[i] == x { return true }
        i++
    }
    false
}

/**
 * reverse_u32 — `list` reversed (post-order becomes reverse-post-order).
 *
 * @param []u32 list  the list to reverse
 * @return []u32  the reversed list
 */
fn reverse_u32(list: []u32) -> []u32 {
    mut out: []u32 = teko::list::empty()
    mut i: u64 = list.len
    loop {
        if i == 0 { break }
        i = i - 1
        out = teko::list::push(out, list[i])
    }
    out
}

/**
 * block_insts_of — the instruction list of `f`'s block with id `id`, or the
 * empty list if no such block (a defensive default; every real target names a
 * present block).
 *
 * @param MFunc f  the function to search
 * @param u32 id  the block id
 * @return []MInst  the block's instructions, or empty
 */
fn block_insts_of(f: MFunc, id: u32) -> []MInst {
    mut i: u64 = 0
    loop {
        if i >= f.blocks.len { break }
        if f.blocks[i].id == id { return f.blocks[i].insts }
        i++
    }
    teko::list::empty()
}

/**
 * block_successors — the CFG successors of `f`'s block `id`: every distinct
 * terminator target in the block (a block ends in up to two terminators — a
 * conditional `MBranchCond`/`MCbz` plus a trailing unconditional `MBranch`).
 * Collected in instruction order for a deterministic RPO (M.2).
 *
 * @param MFunc f  the function
 * @param u32 id  the block whose successors to enumerate
 * @return []u32  the distinct successor block ids, in terminator order
 */
fn block_successors(f: MFunc, id: u32) -> []u32 {
    let insts = block_insts_of(f, id)
    mut out: []u32 = teko::list::empty()
    mut i: u64 = 0
    loop {
        if i >= insts.len { break }
        let t = terminator_target(insts[i])
        if t.has { out = add_unique_u32(out, t.target) }
        i++
    }
    out
}

/**
 * RpoState — the fold state of the post-order DFS: the ids already visited and
 * the post-order accumulator (a block is appended after all its successors).
 *
 * @since #443
 */
type RpoState = struct {
    /**
     * visited — the block ids the DFS has already entered.
     */
    visited: []u32
    /**
     * post — the post-order sequence so far (reversed to RPO by the driver).
     */
    post: []u32
}

/**
 * rpo_visit — the post-order DFS step: enter `id` (once), recurse into every
 * successor in order, then append `id` to the post-order. Threads the visited
 * set + accumulator functionally so no block is entered twice (the recursion
 * is bounded by CFG nesting depth, small for structured control flow).
 *
 * @param MFunc f  the function whose CFG is walked
 * @param u32 id  the block id to visit
 * @param RpoState st  the visited set + post-order so far
 * @return RpoState  the updated visited set + post-order
 */
fn rpo_visit(f: MFunc, id: u32, st: RpoState) -> RpoState {
    if u32_in(st.visited, id) { return st }
    let succs = block_successors(f, id)
    mut cur = RpoState { visited = teko::list::push(st.visited, id); post = st.post }
    mut i: u64 = 0
    loop {
        if i >= succs.len { break }
        cur = rpo_visit(f, succs[i], cur)
        i++
    }
    RpoState { visited = cur.visited; post = teko::list::push(cur.post, id) }
}

/**
 * append_unvisited — `order` extended with any of `f`'s block ids not already
 * present, in block-list order. Keeps the numbering TOTAL: an unreachable
 * block (never reached from entry) still receives program points so no vreg is
 * left un-numbered. Reachable functions are unchanged.
 *
 * @param MFunc f  the function
 * @param []u32 order  the reachable-block order so far
 * @return []u32  the order extended with any unreachable block ids
 */
fn append_unvisited(f: MFunc, order: []u32) -> []u32 {
    mut out = order
    mut i: u64 = 0
    loop {
        if i >= f.blocks.len { break }
        let bid = f.blocks[i].id
        if !u32_in(out, bid) { out = teko::list::push(out, bid) }
        i++
    }
    out
}

/**
 * rpo_block_order — the reverse-post-order block-id sequence of `f`, entry
 * first: a topological linearization of the acyclic CFG (a real `loop`
 * back-edge is the one retreating edge, detected by position, §3). The
 * numbering + back-edge coordinate the allocator indexes by, replacing the
 * allocation-order id proxy (#443). Unreachable blocks are appended last so
 * the order covers every block.
 *
 * @param MFunc f  the selected function
 * @return []u32  the RPO block-id order (entry first)
 */
fn rpo_block_order(f: MFunc) -> []u32 {
    if f.blocks.len == 0 { return teko::list::empty() }
    let entry = f.blocks[0].id
    let st = rpo_visit(f, entry, RpoState { visited = teko::list::empty(); post = teko::list::empty() })
    append_unvisited(f, reverse_u32(st.post))
}

/**
 * block_pos — the position of block id `id` in the RPO `order` (its liveness
 * rank), or `order.len` if absent (a defensive default — an absent target
 * looks like the last position, never a spurious back-edge).
 *
 * @param []u32 order  the RPO block-id order
 * @param u32 id  the block id to rank
 * @return u32  the 0-based RPO position, or `order.len`
 */
fn block_pos(order: []u32, id: u32) -> u32 {
    mut i: u64 = 0
    loop {
        if i >= order.len { break }
        if order[i] == id { return i to u32 }
        i++
    }
    order.len to u32
}
```

### 5.2 Changed — `number_insts` flattens in RPO order

```teko
/**
 * number_insts — flatten `f`'s blocks into one linearly-numbered instruction
 * stream in RPO block order (§3.1). RPO is a topological order of the acyclic
 * CFG, so every def precedes all its uses — the inversion the allocation-order
 * flattening produced for a nested `if`/`match` merge is gone (#443). Every
 * later pass (interval computation, the scan, the rewrite) indexes liveness by
 * these points.
 *
 * @param MFunc f  the selected function
 * @param []u32 order  the RPO block-id order (from `rpo_block_order`)
 * @return []NumberedInst  the flattened, RPO-numbered stream
 */
fn number_insts(f: MFunc, order: []u32) -> []NumberedInst {
    mut out: []NumberedInst = teko::list::empty()
    mut p: u32 = 0
    mut bi: u64 = 0
    loop {
        if bi >= order.len { break }
        let bid = order[bi]
        let insts = block_insts_of(f, bid)
        mut ii: u64 = 0
        loop {
            if ii >= insts.len { break }
            out = teko::list::push(out, NumberedInst { point = p; block_id = bid; inst = insts[ii] })
            p = p + 1
            ii++
        }
        bi++
    }
    out
}
```

### 5.3 Changed — `has_back_edge` becomes RPO-position-based

```teko
/**
 * has_back_edge — whether any terminator is a RETREATING edge in RPO
 * (`pos(target) <= pos(source)`): the exact loop test on a reducible CFG (§3).
 * An acyclic nested `if`/`match` has no retreating edge (all forward in RPO),
 * so it no longer false-positives; a real `loop` latch→header edge retreats
 * and still honest-stops (A3-loop deferral, §6.2). Replaces the
 * allocation-order id proxy (#443).
 *
 * @param []NumberedInst stream  the numbered stream
 * @param []u32 order  the RPO block-id order
 * @return bool  true iff a retreating (back-)edge is present
 */
fn has_back_edge(stream: []NumberedInst, order: []u32) -> bool {
    mut i: u64 = 0
    loop {
        if i >= stream.len { break }
        let ni = stream[i]
        let t = terminator_target(ni.inst)
        if t.has {
            if block_pos(order, t.target) <= block_pos(order, ni.block_id) { return true }
        }
        i++
    }
    false
}
```

### 5.4 Changed — `compute_intervals` threads `order`

```teko
/**
 * compute_intervals — the §3.2 liveness pass: one walk of the RPO-numbered
 * stream driven by the §1.1 def/use table, producing the virtual intervals to
 * color and the fixed pin intervals to block. Honest-stops on a real loop
 * back-edge (§6.2, the A3-loop follow-up), detected by RPO position (#443).
 *
 * @param []NumberedInst stream  the RPO-numbered instruction stream
 * @param []u32 order  the RPO block-id order (for the back-edge test)
 * @return IntervalSet | error  the computed intervals, or the back-edge stop
 */
fn compute_intervals(stream: []NumberedInst, order: []u32) -> IntervalSet | error {
    if has_back_edge(stream, order) {
        return error { message = "regalloc: a loop back-edge needs live-range holes over the strict linear numbering — deferred to the A3-loop follow-up (§6.2)" }
    }
    // … body unchanged from regalloc.tks:657-679 …
}
```

### 5.5 Changed — `regalloc_func` computes the order once and threads it

```teko
/**
 * regalloc_func — allocate one function end-to-end: RPO-order, number, compute
 * intervals, scan, rewrite. Returns a NAMED honest-stop error on a real loop
 * back-edge (§6.2, via `compute_intervals`) or on an FPR-value spill (A3 lowers
 * GPR spills only). The RPO order is computed once and threaded so the
 * numbering and the back-edge test share one topological linearization (#443).
 *
 * @param AbiDescriptor abi  the register file
 * @param MFunc f  the selected function (virtual MRegs + ABI pins)
 * @return MFunc | error  the fully-colored function, or a named honest-stop
 */
pub fn regalloc_func(abi: AbiDescriptor, f: MFunc) -> MFunc | error {
    let order = rpo_block_order(f)
    let numbered = number_insts(f, order)
    let ivals = match compute_intervals(numbered, order) {
        IntervalSet as s => s
        error as e => return e
    }
    let sr = linear_scan(abi, ivals)
    if detect_fpr_spill(sr, f) {
        return error { message = "regalloc: FPR-value spill not yet supported (A3 lowers GPR spills only; own-backend FPR-spill deferral)" }
    }
    rewrite_func(abi, f, sr)
}
```

**Ripple (contained to `regalloc.tks` + `regalloc_test.tkt`):** the three re-signatured fns
(`number_insts`, `has_back_edge`, `compute_intervals`) have **no other src callers**. But
`regalloc_test.tkt` calls the module-private `number_insts` (5 sites) and `compute_intervals`
(4 sites) directly — those call sites MUST update in the same crumb (§7). The private-fn access is
legitimate (a `.tkt` sees its module's internals).

---

## 6. The crumb sequence (ordered, each independently gate-able)

Every crumb owes the full ritual — the **both-engine gate** (native `teko . -o bin` AND VM
`teko test .`) · paranoid · fixpoint — and **100% coverage on its new code**. No machine bytes.

**CRITICAL SEQUENCING LAW (do not violate):** the numbering fix (Crumb 2) MUST land **before or
with** the back-edge relaxation (Crumb 3). Relaxing `has_back_edge` first — while numbering is
still allocation-order — would REMOVE the honest-stop that is currently the only thing guarding the
inverted-liveness miscompile (§1.2), turning a fail-safe stop into a live miscompile. Numbering
first is always fail-safe: a correct RPO numbering under a still-conservative id-based back-edge
over-stops (never miscompiles). Never the reverse.

### Crumb 1 — the RPO analysis helpers (additive, inert)
- **Add** to `regalloc.tks`: `u32_in`, `reverse_u32`, `block_insts_of`, `block_successors`,
  `RpoState`, `rpo_visit`, `append_unvisited`, `rpo_block_order`, `block_pos` (§5.1). No existing
  fn changes; behavior is unchanged (nothing calls them yet).
- **Tests** (`regalloc_test.tkt`): `rpo_block_order` on hand-built `MFunc`s —
  (a) straight-line single block → `[0]`; (b) diamond with an id-inverted merge (entry id 0, merge
  id 1, arms id 2/3, arms jump to merge 1) → RPO ranks entry first, merge last, `pos(1) > pos(2)`
  and `pos(1) > pos(3)`; (c) a 2-block loop (B0→B1→B0) → RPO `[0,1]`, `pos(0) < pos(1)` (so the
  B1→B0 edge is retreating). Assert positions via `block_pos`.
- **Gate.** Pure addition, all existing tests untouched.

### Crumb 2 — RPO numbering (numbering fix, back-edge STILL conservative)
- **Change** `number_insts(f)` → `number_insts(f, order)` (§5.2). `regalloc_func` computes
  `let order = rpo_block_order(f)` and passes it (`compute_intervals` unchanged this crumb — still
  the id-based `has_back_edge(stream)`, which keeps over-stopping nested `if`/`match`, fail-safe).
- **Update** the 5 existing `number_insts(...)` call sites in `regalloc_test.tkt`
  (lines 221, 237, 252, 261, 918). For the single-block fixtures RPO order == `[0]` == id order, so
  **expected values are unchanged** — only the call adds `rpo_block_order(f)`. Recommended helper to
  cut churn: `fn rgt_number(f: MFunc) -> []NumberedInst { number_insts(f, rpo_block_order(f)) }`.
- **New white-box test** (the inverted-liveness case, at the numbering layer): build the diamond
  fixture where a vreg is **defined in a high-id arm block and used in the low-id merge block**
  (§10.3); assert its def-point < its use-point in `number_insts(f, rpo_block_order(f))` (monotonic
  — would FAIL under the old id-order flattening).
- **Gate.** Existing single-block behavior identical; numbering now RPO-correct; back-edge still
  conservative (nested still stops via old proxy — safe).

### Crumb 3 — RPO-position back-edge (the unblock) + fixtures + doc-sync
- **Change** `has_back_edge(stream)` → `has_back_edge(stream, order)` (position-based, §5.3);
  `compute_intervals(stream)` → `compute_intervals(stream, order)` (§5.4); `regalloc_func` passes
  `order` to `compute_intervals` (§5.5). Now a nested `if`/`match` **allocates**; a real `loop`
  **still honest-stops**.
- **Update** the 4 existing `compute_intervals(...)` call sites in `regalloc_test.tkt`
  (lines 237, 252, 261, 918) to thread `order` (helper: `fn rgt_intervals(f: MFunc) ->
  IntervalSet | error { let o = rpo_block_order(f); compute_intervals(number_insts(f, o), o) }`).
  Single-block expected values unchanged.
- **New fixtures** (§10): the nested-`if` interp-equivalence + allocate-success (§10.1); the loop
  still-honest-stops guard (§10.2, the existing `rgt_regalloc_func_back_edge_honest_stops` at
  `regalloc_test.tkt:827` is a genuine B0↔B1 loop and MUST stay green — extend or keep it).
- **Doc-sync:** update `docs/design/backend-a3-regalloc.md` §6.2 — replace the "KNOWN
  OVER-APPROXIMATION" warning with "RESOLVED by #443 (RPO position back-edge test); see
  `backend-443-rpo-numbering.md`". The A3-loop deferral text stays (real loops still stop).
- **Gate + ritual + 100% coverage on the new code.**

```
Crumb 1 (RPO helpers, inert) ─▶ Crumb 2 (RPO numbering) ─▶ Crumb 3 (RPO back-edge + unblock)
                                        [#443 closes at Crumb 3]
```

Crumbs 2 and 3 may be co-committed if the implementer prefers a single lane, provided the ordering
law holds (numbering correct before back-edge relaxed). Kept separate here for smallest safe steps.

---

## 7. Exactly which existing test call sites change

`regalloc_test.tkt` (private-fn access is legitimate):

| Line | Current | After |
|---|---|---|
| 221 | `number_insts(f)` | `number_insts(f, rpo_block_order(f))` |
| 237 | `compute_intervals(number_insts(rgt_func(insts)))` | thread `order` (§6 helper) |
| 252 | `compute_intervals(number_insts(rgt_func(insts)))` | thread `order` |
| 261 | `compute_intervals(number_insts(rgt_func(insts)))` | thread `order` |
| 918 | `compute_intervals(number_insts(f))` | thread `order` |

All five are **single-block** fixtures (`rgt_func` builds one block id 0), so RPO == id order and
**no expected assertion value changes** — the edits are mechanical signature threading.

---

## 8. Regression fixtures (built to match isel's actual output)

Model the harness on the existing interp-equivalence tests (`regalloc_test.tkt:675-692`) and the
existing back-edge test (`:827-837`). `minst_interp` follows the CFG by id from `blocks[0]`, so a
hand-built `MFunc` with an id-inverted merge exercises exactly the isel shape.

### 8.1 Nested-`if` interp-equivalence + allocate-success (the primary #443 regression)
Build an **acyclic** `MFunc` whose block-list/id order is NOT topological — a merge block with a
**lower id than the arm-tail blocks that jump to it**, and a **vreg defined in an arm and used in
the merge** (so it also exercises the inversion). Concretely (ids in parens):

- **B0** (0, entry): `movz v0=1`; `cbz(zero=true) v0 -> B3`; `branch B2` — a real conditional so
  the CFG-follow is exercised (v0=1 nonzero ⇒ falls through to `branch B2`, the "then" path).
- **B1** (1, MERGE, lowest id, executes LAST): param `p=v10`; `mov x0=v10`; `ret`.
- **B2** (2, then arm): `movz v1=42`; `mov v10=v1` (edge move into the merge param); `branch B1`.
- **B3** (3, else arm): `movz v2=99`; `mov v10=v2`; `branch B1`.

`v10` is **defined in B2/B3 (ids 2/3) and used in B1 (id 1)** — the id-inversion. Block list in id
order `[B0,B1,B2,B3]`. Assert:
1. `minst_interp(module, "main") == 42` (pre) — the interp already runs this (id-driven).
2. `regalloc_func(aapcs64(), f)` returns **`MFunc` (NOT error)** — this is the sharp guard: it
   **fails before the fix** (`has_back_edge` false-positives on B2→B1 / B3→B1), **passes after**.
3. `all_physical(colored) == true` and `all_physical(f) == false`.
4. `minst_interp(regalloc_module(...), "main") == 42` (post == pre) — semantics preserved.

> Do NOT wrap the `regalloc_func` result in the `error => return` early-out the other harnesses use
> — that would silently pass before the fix. Assert `is_err == false` explicitly.

### 8.2 The inversion discriminator (optional, sharpens 8.1)
Add a second value that stays live **across** the arms into the merge, so a mis-numbered allocation
would color it identically to `v10` and corrupt the result: e.g. B0 also `movz v20=7`, and B1
computes `add v11 = v10 + v20; mov x0 = v11` → expected 49. Under inverted numbering `v10`'s range
collapses and the scan can alias it with `v20` → wrong sum; under RPO both are live → 49. The
interp-equivalence (post == 49) is the black-box catch.

### 8.3 The loop still honest-stops (the deferral guard — already present)
`regalloc_test.tkt:827 rgt_regalloc_func_back_edge_honest_stops` builds B0→B1→B0 (a genuine
2-block loop; B1→B0 is retreating in RPO `[0,1]`). It MUST STAY GREEN after the fix (assert
`regalloc_func` returns `error`). Keep it verbatim; optionally add a self-loop (B0 `branch B0`) and
a 3-block loop for coverage of the retreating-edge branch.

### 8.4 White-box numbering-monotonic (the §1.2 inversion at the numbering layer)
On the 8.1 fixture, assert in `number_insts(f, rpo_block_order(f))` that the point of the `mov
v10=v1` def (in B2) is **less than** the point of the `mov x0=v10` use (in B1). This is the crisp
proof that RPO fixed the numbering even independently of the back-edge test.

---

## 9. Ritual points

- **Both-engine gate at every crumb:** native `teko . -o bin` **and** VM `teko test .`. The
  diff-VM==native lane runs the VM; the recursion in `rpo_visit` and the `mut cur = RpoState{…}` /
  `cur = rpo_visit(…)` reassignment must run on both.
- **VM gotcha watch:**
  - No `x = match { … return }` reassignment (VM can't do control flow inside a match used as an
    assignment RHS) — none is introduced; `regalloc_func` keeps the existing `let … = match { …
    error as e => return e }` **binding** shape (`regalloc.tks:1634`, the pattern isel documents at
    `isel_arm64.tks:1831-1835`).
  - Literal u32 anchoring: keep the `to u32` casts on literals (`i to u32`, `order.len to u32`) as
    the surrounding code does; a bare integer literal can mis-infer width in the VM.
  - `mut i: u64 = list.len` then `i = i - 1` in `reverse_u32` guards the `i == 0` break BEFORE
    decrement (no u64 underflow).
- **100% coverage on new code** (`teko . -o bin --coverage`, native path, never `test --coverage`):
  every new fn (`rpo_visit` both branches — already-visited early-return and the recursion;
  `block_pos` found + fallback; `append_unvisited` present + absent; `has_back_edge` retreating +
  forward) needs a covering fixture. The fixtures in §8 cover the reachable branches; the defensive
  fallbacks (`block_insts_of` empty, `block_pos` `order.len`) need a tiny direct unit test or a
  justified-unreachable note in the PR.
- **Fixpoint + paranoid** at the crumb that closes (#443 at Crumb 3), per the standing ritual.

---

## 10. Risks + law tensions (resolved law-first)

| Risk / tension | Resolution |
|---|---|
| **Relaxing the back-edge test before numbering is fixed → live miscompile.** | The §6 SEQUENCING LAW: numbering (Crumb 2) before back-edge relaxation (Crumb 3). Numbering-first is always fail-safe. |
| **RPO retreating≡back-edge holds only for reducible CFGs.** | Teko's structured lowering is reducible (no `goto`; `break`/`continue` target enclosing loops). Failure mode of the assumption is a spurious honest-stop, never a miscompile (M.3). Documented for any future irreducible-CFG feature. |
| **`number_insts`/`compute_intervals` signature change breaks existing tests.** | Ripple is fully mapped (§7); all five sites are single-block, expected values unchanged, mechanical edit + optional helper. |
| **Recursion depth in `rpo_visit`.** | Bounded by CFG nesting depth (structured control flow, shallow). Functional threading, one entry per block (`visited` guard). If ever a concern, an explicit-stack iterative post-order is a drop-in later; not needed now (M.1, simplest correct first). |
| **Unreachable blocks left un-numbered.** | `append_unvisited` makes the order total (every block numbered), so no vreg is dropped. |
| **Physically reordering `MFunc.blocks` (the tempting A4-layout variant).** | Out of scope for #443. We renumber, not reorder (§4) — smallest blast radius, interp/golden dumps untouched. If A4 wants RPO code layout, that is a separate, later change whose only invariant is entry-at-index-0 (RPO already guarantees). |

**No genuine unresolved tension → no HALT.** Every decision is resolved law-first with the RPO
pass; the deferrals (A3-loop for real loops, A3-splitting, A4-frame) are unchanged. The one thing
for the integrator to confirm (not a design tension): the fix branch base — the active backend wave
umbrella (`remodel/backend-build`), and whether #443 rides as a sub-PR or a mini-umbrella if it
grows.

## 11. A4-6 follow-up — the UNREACHABLE-block false-positive #443 did not cover (#385)

#443 removed the acyclic false-positive for the blocks the RPO DFS reaches. It did **not** cover a
second, distinct acyclic false-positive because no #443 fixture exercised it: a **dead** block whose
forward edge retreats only because `append_unvisited` numbers it last.

**The shape.** `lower_match_value` (`src/lir/lower.tks`) always emits a defensive-fallback block past
the last arm (a `const 0; jump merge`, mirroring the codegen's defensive-return spirit). When the
final arm is **irrefutable** — a `_` wildcard, the common exhaustive tail — its pattern test lowers to
an UNCONDITIONAL `jump` straight into that arm's body (`close_test_to`, `test.always`), so the
fallback block that arm allocated as its own "next test" target has **no predecessor**. It is
unreachable dead code.

**Why it tripped.** `rpo_block_order` = DFS-from-entry (reachable, in RPO) **then**
`append_unvisited` (every remaining block, block-list order, so the numbering stays total and no dead
vreg is dropped). The dead fallback lands LAST. Its own forward `jump` to the merge block — which the
DFS numbered earlier — thus satisfies `pos(merge) ≤ pos(fallback)`, the exact retreating-edge test.
`has_back_edge` reported a back-edge on a CFG that is a pure acyclic DAG. Forensic trace for
`match 5 { 1 => 40, 2 => 41, _ => 42 }` (8 machine blocks): RPO order `[0,3,5,6,4,2,1,7]`, block 7 the
dead fallback (unreachable), `pos(7)=7`, its successor the merge block 1 at `pos(1)=6` →
`6 ≤ 7` → false back-edge → the A4-5 `own_match_exit` fixture KNOWN_STOPped at `regalloc_func`.

**Root cause class.** Scenario (a) of the A4-6 brief — a block reachable only via a "missed edge"
lands at the end of the order and its forward branch to the merge looks like a retreat — in its purest
form: the block is reachable via **no** edge at all (genuinely dead), so no edge is "missing" from
`block_successors`; the CFG is correctly modeled and correctly acyclic. The defect is entirely that the
back-edge test counted an edge leaving a dead block.

**Fix.** A retreating edge is a real loop back-edge only if it is part of a cycle **reachable from
entry**; a dead block cannot form such a loop. `reachable_blocks(f)` returns the DFS-visited set (the
reachable blocks, before `append_unvisited`), and `has_back_edge` counts a retreat only when its
SOURCE block is in that set. `compute_intervals` and `regalloc_func` thread the reachable set beside
the order (one extra argument, mechanical at both call sites). The dead block is still numbered (its
vregs still get program points — allocation of dead code stays correct and harmless), it just no
longer contributes a spurious back-edge. A **reachable** `loop` latch→header edge is unaffected and
still honest-stops (A3-loop deferral). Contained to `src/backend/regalloc.tks`; no isel/lower change —
the dead block is a legitimate, intentional part of the match lowering, not a bug to remove.
