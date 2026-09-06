# Overnight digest — 2026-08-14 (architect design-ahead + concrete fixes)

> **For the owner, at a glance.** Six design-only artefacts landed overnight, each on its own `design/*`
> branch off `origin/fix/retirement` (NOT pushed; the integrator folds). NONE touched product code, ran a
> build, a reseed, or `teko test`. This index summarises each in a few lines, then collects **every point
> that needs your deliberation** in one place (§B) so you can decide part-by-part without opening six docs.
>
> **How to read this:** §A = the six artefacts (what/status/anchors). **§B = your decisions** (the only
> section that needs you). §C = resolved context (no decision needed). §D = pipeline state.

---

## A. The six artefacts

### A1. Concurrent collections — Option B (shared-memory + lock/interlocked)
- **Branch / SHA / doc:** `design/collections-optionB` · `cf40fe24` ·
  `docs/design/plano-collections-genericas-e-concorrentes-0.3.1.md` (§3 rewritten; §1–§2 generic untouched).
- **Decided:** per your ruling, the concurrent family is **Option B** — shared-memory collections behind
  lock/interlocked (lock-free CAS), NOT the chan-actor plan (superseded). Reconciled with §10 as the
  **`Arc<Mutex<T>>`** pattern: isolation stays the DEFAULT, a concurrent collection is the explicit,
  marked, opt-in escape hatch (a sanctioned §10 exception, not a violation). Adds a NEW `teko::threads::sync`
  coordination family (`WaitGroup`/`Barrier`/`Semaphore`/`Latch`/`Atomic<T>`) as futex-backed handles-by-id.
- **Status:** design-only, ready for fold. FASE-2, blocked on §10 F1 (per-task thread) + new C primitives.
- **Key anchors:** F2 program region ALREADY seeded (`tk_region_program`, `teko_rt.c:2305`); the atomic/
  spinlock idiom already present (`teko_rt.c:1570`/`:1588`); runtime primitives to add
  `tk_mutex_lock`/`tk_atomic_cas/add/load/store`. Speed = striped locks (ConcurrentDictionary) + lock-free
  (Treiber/Michael-Scott); memory = one shared object beats the actor's N chan buffers + per-op copies.

### A2. §9.D R1 — the three inline-union capabilities (5 crumbs)
- **Branch / SHA / doc:** `design/9d-capabilities-crumbs` · `6b7edb06` ·
  `docs/design/plano-9d-capabilities-crumbs-r1.md`.
- **Decided:** the ordered crumb sequence for Reseed 1 (the additive compiler capabilities only; the
  ~2000-site sweep stays out of scope, §14-gated). A1 (parser grouped type `(A|B)`/`[](A|B)`), B1
  (resolved-side variant mangle arm), B2 (syntactic slice-of-union via resolution), C1 (parser
  `GroupBindPattern`), C2 (checker group-bind typing + exhaustiveness). All inert on today's corpus → ONE
  additive reseed.
- **Status:** design-only, implementer-ready. R1 unblocked today; the sweep waits on §14 `@Type()`.
- **Key anchors / finding:** capability (b)'s "resolve-then-mangle" is the EXACT idiom already at
  `emit_type_expr:2652-2663`, so byte-agreement is a corollary of the single canonicalizer
  `cg_union_normalize_null` (`codegen.tks:2003`), not a promise. Parser patch at `parse_type.tks:148`.

### A3. Serialization tags — S1–S7 (the §14-consumer half)
- **Branch / SHA / doc:** `design/serial-crumbs` · `aa515a70` · `docs/design/serial-tags-crumbs-r1.md`.
- **Decided:** the crumb sequence for the serialization-OWNED half, contracting against §14's declared
  producer shapes. S1 (lexer backtick `Tag`), S2 (`Field.tag`/`StructBody.type_tag` AST + `.tkb` codec +
  reseed), S3 (per-format tag grammar), S4 (`@fields` `.tag` extension), S5 (tagged synthesis branches),
  S6 (schema-first: type-level tag + `.tkp [schema]` + URI-not-prefix), S7 (`encode`/`decode` skeletons).
- **Status:** design-only. S1/S2/S3-grammar/S6a/S7-skeletons buildable NOW behind the S2 reseed; the
  comptime-consuming halves resume when §14 B2/B3/B5/B6 land.
- **Key anchors:** `Field` `ast.tks:643`; four field-build sites `parse_decl.tks:917/990/1245/1275`; `.tkb`
  `[]Field` writer `tkb_write.tks:367-382`, `StructBody` `:421`. §14 owns `@fields`/`synthesize_serializers`
  + the untagged path; serialization owns the tag layer + tagged branches + schema-first.

### A4. Fix — `len` builtin CALL not lowered (N2)  ← patch, to apply
- **Branch / SHA / doc:** `design/len-lowering-fix` · `18a1d589` ·
  `docs/design/fix-len-builtin-call-lowering.md`.
- **Decided:** pinpoint of the mem-paranoid codegen failure. The failing `len` is the builtin CALL
  `teko::str::len(<expr>)` (`checker_test.tkt:619`/`:627`) — NOT the `.len` field form. `len` is a checker
  builtin (`scope.tks:1095`) with empty `call_ns`; `lower_call` has no `len` arm and `native_builtin_symbol`
  no twin, so it hits `unresolved_builtin_stop` (`lower.tks:5351`). Recommended fix (a): intercept the call
  and lower it as the fat-length read (reusing `lower_fat_expr`, which already covers a fat-returning call).
- **Status:** patch written (~18 lines), an implementer is applying it. Last codegen blocker for #110.

### A5. Fix — `__dso_handle` undefined on x86_64-glibc link  ← patch, to apply
- **Branch / SHA / doc:** `design/dso-handle-fix` · `de283f14` ·
  `docs/design/fix-dso-handle-glibc-x86-link.md`.
- **Decided:** the direct-ld ELF link (`project.tks:2163`) pushes glibc's `Scrt1.o`/`crti.o`/`crtn.o` but
  NOT `crtbeginS.o`, which defines `__dso_handle` (pulled by `atexit(tk_regions_free_all)`, `teko_rt.c:2212`,
  via glibc's `libc_nonshared.a`). x86-glibc + binutils 2.42 hard-fails; arm64 links clean. Recommended fix
  (A): define `__dso_handle` weak+hidden in `teko_rt.c` guarded `#if defined(__GLIBC__)` — satisfies the
  direct link, the cc-driver route's strong def wins, inert on arm64/macOS/Windows/musl.
- **Status:** patch written, bundled to an implementer. Pre-existing (not our regression), not environmental.

### A6. FASE-1b — generic collections G1–G6 + `IHash`
- **Branch / SHA / doc:** `design/collections-generics-crumbs` · `263b9dac` ·
  `docs/design/collections-generics-fase1b-crumbs.md`.
- **Decided:** the crumb sequence for the capability-constrained generic collections. G1 (`IHash` method
  interface + `StrKey`), G2 (`arr_*` combinators + IOrd helpers), G3 (`Dictionary<K: IEq & IHash, V>`), G4
  (`HashSet`), G5 (`SortedSet`/`PriorityQueue`/`SortedDictionary` over IOrd), G6 (additive
  `Map`→`Dictionary<StrKey,V>` bridge; `Map` STAYS — `teko::env` depends).
- **Status:** design-only, implementer-ready. GATE: enters only a seed that already contains 9-ops (seed
  discipline) → sequence after the 9-ops reseed.
- **Key anchors:** `IHash` dispatches via the #254 method-over-`T` path (DONE); `Dictionary` generalises
  `map_find_index` (`map.tks:143`); `str_hash` (`teko_rt.tks:529`). Contracts against 9-ops IEq/IOrd
  (`plano-9ops-…:341-357`).

---

## B. Needs your deliberation

Grouped by artefact. Each: one-line context + my recommendation. None is a HALT — all have a law-first
default; these are the points where your ruling changes the shape.

**B1. Serialization (A3)**
- **Extent-3 (`T.fields`) ratification** — `@fields<T>()` needs comptime field reflection, which §14's
  plan lists as "owner-pending" (guarded to comptime descriptors only, never a runtime field-value read).
  *Recommend: ratify now* — the serialization keystone requires it and the seal already permits it guarded.
  (This is §14's confirmation; serialization inherits it.)
- **`.tkp [schema]` remote-fetch policy** — schema-first can reference external schemas; how to resolve a
  namespace URI to a file. *Recommend: local-first (manifest map → vendored convention), remote fetch ONLY
  opt-in AND content-pinned, default no network* (hermetic build / byte-identical fixpoint). Confirm the
  opt-in+pin default before the fetch path is built; the local-first path needs no confirmation.
- **Method-form `user.encode()`** — the free-function `json::encode(u)` ships first (rides monomorph, no
  #254); a method form needs #254. *Recommend: defer the method sugar to a later wave.* Not a blocker.

**B2. Generic collections (A6)**
- **Host module for `IEq`/`IOrd`/`IHash`** — `src/cmp/` does NOT exist (its `cmp.tks` was dropped in
  `07588731`, "outran the seed"); `src/core.tks` exists. The three must cohabit (one `K: IEq & IHash`
  constraint resolves both). *Recommend: 9-ops crumb 6 pins the module (re-created `src/cmp/` post-reseed,
  or `src/core.tks`); `IHash` co-locates there.* A coordination decision owned by 9-ops crumb 6.
- **`StrKey` wrapper vs direct-`str` key capability** — a `struct { key: str } IEq & IHash` works today; a
  primitive `str` carrying the capability directly depends on 9-ops T-1 authorising capability on a
  prim-backed type. *Recommend: ship the `StrKey` wrapper (safe/proven); direct-`str` is an additive
  follow-on if 9-ops T-1 authorises it.*

**B3. `len` builtin fix (A4)**
- **(a) backend arm vs (b) rewrite the test line** — (a) lowers the `len` CALL as a fat-length read
  (closes the capability gap program-wide); (b) edits the two test lines to the `.len` field form (leaves
  the CALL unlowered for any other caller). *Recommend (a)* — it is your default and the correct closure;
  (b) is a narrower workaround. Veto is yours.

---

## C. Resolved context (no decision needed — for orientation)

- **Memory / #110** — mem-paranoid ran CLEAN through every heavy pass (checker/monomorph/consteval ×2, no
  arena growth): **NO leak.** The earlier concern was a 9-ops peak-RSS inflation, since corrected. The only
  remaining #110 blockers are the two codegen/link fixes (A4 `len`, A5 `__dso_handle`), both patched and en
  route to implementers.
- **9-ops self-hosts** — the interface-operator capability landed via a staging step (`src/cmp/cmp.tks`
  dropped in `07588731` because its operators outran the then-seed); IEq/IOrd arrive on the next reseed.
- **§14 A0/A1 landed inert** — the macro/comptime lexer + AST nodes + parse are in (`5f0d701c`,
  "feat(parser): macro/comptime AST nodes + parse (§14 A1)"), byte-identical (no corpus uses `@` yet).

---

## D. Pipeline / reseed state

| track | state |
|---|---|
| **§14 macro/comptime** | IN PROGRESS — A0/A1 landed inert; B2/B3/B5/B6 (comptime engine + `@fields` + synthesis) are the producer gate for serialization + the §9.D sweep. |
| **9-ops (IEq/IOrd)** | LANDING — next reseed; unblocks FASE-1b generics (A6) and generic `sort<T: IOrd>`. |
| **§9.D R1 (A2)** | DESIGN-READY — 5 crumbs, one additive reseed; sweep waits on §14 `@Type()`. |
| **Concurrent collections (A1)** | DESIGN-READY (Option B) — FASE-2, blocked on §10 F1 + new C primitives. |
| **Serialization (A3)** | DESIGN-READY — S1/S2/S3/S6a/S7 buildable now behind the S2 reseed; rest gated on §14. |
| **Generic collections (A6)** | DESIGN-READY — gated on the 9-ops reseed. |
| **Fixes len (A4) + dso (A5)** | PATCHED — implementers applying; the two remaining #110 codegen/link blockers. |

---

*All six branches are off `origin/fix/retirement`, design-only, NOT pushed. SHAs: `cf40fe24` (A1),
`6b7edb06` (A2), `aa515a70` (A3), `18a1d589` (A4), `de283f14` (A5), `263b9dac` (A6). Read any in full with
`git show <sha>:<doc>`. This digest is itself design-only on `design/overnight-digest`.*
