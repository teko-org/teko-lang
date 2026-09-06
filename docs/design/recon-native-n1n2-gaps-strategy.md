# Strategic RECON — the native N1/N2 lowering gaps in the test corpus: WIP-known, not regressions

> **Status:** RECON for the owner to direct (read-only). This file is the SOLE edit. NO build, NO test,
> NO reseed. Isolated worktree off `origin/fix/retirement` (HEAD `d9613aea`), branch
> `design/native-n1n2-gap-recon`. NOT a fix — a decision brief.
>
> **The question the owner asked:** the `mem-paranoid` ran CLEAN through every heavy pass (checker /
> monomorph / consteval — no arena growth, NO leak) and fails on a **series of native-backend lowering
> stops** (`len` [fixed] → `s is not a fat-pointer local … [in cl_shadow_nested]` → probably more). **Are
> these gaps WIP-known ("native legs") or regressions/bugs?**

---

## 1. Answer: WIP-KNOWN, by design — not regressions, not bugs

These are the **deliberate, documented incompleteness of the N1/N2 native-lowering subset** — the "native
legs" roadmap — now EXPOSED because the test lane finally runs the native route. Five independent proofs:

1. **The native subset is a mapped, byte-identical boundary.** `docs/memory/mapa-native-6-pernas-0.3.1.0.md`
   measured **26 distinct LOWERING honest-stops**, byte-identical across all 6 platforms, all in the shared
   `src/lir/lower.tks`, for "unsupported language constructs." The stops are shared infrastructure, not
   platform bugs.
2. **The test lane historically DID NOT run the native backend at all.** Same doc, Probe D: `teko test .`
   emitted C and ran `cc` UNCONDITIONALLY (`run_native_gate`, `project.tks`), ignoring `TEKO_BACKEND` — a
   documented "false green." The owner's recent gate ("fail the lane if any C is present after test") is
   what forces the native route now, which is EXACTLY why these known N1/N2 stops surface one-by-one today.
   The gaps were always there; the C-route false-green masked them.
3. **The completion is an owner-sequenced, numbered roadmap.** `docs/memory/0.3.1.0-linux-native-first-stop.md`
   describes the native self-build as a platform-sequenced plan with numbered **degraus** (steps 1-6: the
   `byte` machine class, variant cases, `&&`/`||`, aggregate push, fat `match`, the variant wrapper) — an
   explicitly INCREMENTAL subset build-out, shared lowering benefiting all platforms.
4. **The gaps are an enumerated, weighted BACKLOG.** `docs/memory/bulk-native-verdicts-0.3.1.md` is a
   MEASURED verdict of all 203 bulk fixtures — **KNOWN-STOP 84 · PASS 76 · KNOWN-WRONG 21 · BLOCKED 20** —
   with an explicit "**.32 work list, one wagon per row**" ranking the gaps by weight (`str has no single
   PrimKind` ×17, `integer operator not yet lowered` ×8, …). A planned backlog, not surprises.
5. **`cl_shadow_nested`'s stop is a documented, TESTED honest-stop** (§2). The `len` fix (just landed) was
   the same class.

**Conclusion:** the red native-test-lane is EXPECTED — the N1/N2 subset is incomplete by design. It is not
a regression, not a leak, and not an internal-assert failure. The mem verdict (no arena growth) already
stands on the heavy passes; the native-lowering stops are an ORTHOGONAL completeness signal.

---

## 2. `cl_shadow_nested` specifically — an honest-stop in the fat-pointer family (tested)

- **Where it lives:** `src/checker/closures_test.tkt:101` — a `#test` fn (issue #107) exercising NESTED
  CLOSURE shadowing: an outer closure param `write` captured by an inner lambda that introduces its own
  `write`, plus a fat (`str`) value flowing through the capture.
- **The message is an honest-stop, not a crash/assert.** It is raised at `src/lir/lower.tks:12344` inside
  `lower_var_fat`, whose doc-comment DECLARES it: `@throws when the name is neither a fat local nor an
  interned str/slice aggregate const`. The `(internal)` suffix reads like an assert but is a deliberate
  named subset boundary — a fat (`str`/`[]T`) local that is NOT bound as a fat slot / fat `let` / rodata
  const has no native representation yet. Here that shape arises from a fat value captured THROUGH a nested
  closure, which the native env does not yet bind as a fat slot.
- **It is EXPECTED behaviour, pinned by a regression test.** `src/lir/lower_test.tkt:2238`
  (`lwt_fat_local_reassign_honest_stops_on_later_fat_use`) asserts this exact stop fires
  (`str_contains(out, "\`s\` is not a fat-pointer local")`), and `:2255` documents the degrau-4 fix that
  closed the fat-PARAMETER version of it. The subset is being completed shape-by-shape; the closure-capture
  fat local is simply a shape not yet reached.
- **Family:** fat-pointer/slice — the SAME family as the `len` builtin-call stop just fixed.

---

## 3. The series — how many, what families, why one-by-one

**How many:** dozens of distinct honest-stops across the corpus. The map counted 26 distinct in the
example projects; `bulk` alone counts 84 KNOWN-STOP + 21 KNOWN-WRONG + 20 BLOCKED. The `.tkt` self-tests
(checker/lir/parser) exercise the compiler's OWN richer constructs, so the corpus is a superset — but they
CLUSTER into a small number of families.

**The families (by weight — the fix-unit is the family, not the occurrence):**
| family | representative stop | rough weight | note |
|---|---|---|---|
| **union / nullable type lowering** (LARGEST) | `T \| null` / `str \| error` "has no single PrimKind" in cast/match/field/comparison | ~25+ of the mapped occurrences; `str`-PrimKind ×17 in bulk | core language surface — one family-fix clears the most fixtures |
| **fat-pointer / slice** | `len` (fixed); `s is not a fat-pointer local` (`cl_shadow_nested`); fat-pointer `match`-as-value; interface-dispatch-fat | medium | the `len`/`cl_shadow_nested` items |
| **integer / operator** | `integer operator not yet lowered` | ×8 (bulk) | small, mechanical |
| **aggregate slice-push layout** | `push whose element is aggregate … no registered layout` (`push_minst_block`, `emit_u32_le`) | few | the self-compile stop |

**Why they reveal one-by-one:** the native gate compiles each corpus item and STOPS at the FIRST
unlowerable construct, reporting `[in ns::fn]` and aborting the run; fixing that item exposes the next
first-stop. Each `.tkt`/fixture is a separate item, so the earliest failing one masks everything behind it
— the classic peel-the-onion pattern the roadmap docs already describe ("first stop", "one wagon per
row"). Because they cluster into ~4 families, a single family-level fix (e.g. union/nullable lowering)
clears many items at once, not one.

---

## 4. Recommendation to the owner — decouple the two signals

The mem-paranoid gate is currently CONFLATING two independent questions:
- **(Q1) Does memory grow?** — the actual memory question. **Answered: NO** (checker/monomorph/consteval
  ran clean, no arena growth).
- **(Q2) Does the native backend lower the WHOLE corpus?** — a codegen-completeness question. **Answered:
  NO, by known design** (the N1/N2 subset is incomplete).

These are orthogonal. My recommendation is **BOTH, decoupled:**

- **(b, for the GATE — do now) Re-baseline mem-paranoid to Q1 only.** The memory verdict should STAND on
  the passes that complete (which showed no growth) and treat native-lowering honest-stops as a SEPARATE,
  expected WIP metric — not a memory failure. Concretely: the mem gate measures arena growth through the
  heavy passes; a native-lowering honest-stop (`N1: … (N2)`) is recorded as "known native-subset gap", not
  as a leak/failure. This unblocks the clean mem verdict TODAY instead of holding it hostage to N1/N2
  completeness.
- **(a, for the ROADMAP — schedule) Complete the N1/N2 subset by FAMILY, biggest first.** This is bounded,
  already-planned work (the `bulk` ".32 work list" + the `first-stop` degraus sequence it). Effort is a
  handful of `lower.tks` arms per family (the `len` fix was ~18 lines; `cl_shadow_nested` is one fat-local
  shape). **Prioritise the union/nullable family** — it is the largest and is core language surface, so it
  clears the most corpus items per crumb. Each family-fix is gate-able alone and byte-identical (additive
  to the subset, zero fixpoint risk). Days-to-weeks, not a rewrite.

**Bottom line for the owner:** the red native-test-lane is NOT a regression and NOT a memory problem — it
is the known, mapped incompleteness of the native lowering subset, newly visible because the strict test
gate removed the C-route false-green. The memory verdict (no leak) is already earned on the heavy passes;
decouple it from the native-completeness signal, and drive the N1/N2 completion as the planned per-family
track, union/nullable first.

---

## 5. Anchors (verified on `origin/fix/retirement`, HEAD `d9613aea`)

| what | file:line |
|---|---|
| native subset map — 26 byte-identical lowering stops; Probe D false-green | `docs/memory/mapa-native-6-pernas-0.3.1.0.md` (Probe D §179-238) |
| owner-sequenced degrau roadmap (Linux-first, shared lowering) | `docs/memory/0.3.1.0-linux-native-first-stop.md` |
| enumerated weighted backlog (KNOWN-STOP 84 / PASS 76 / …) + ".32 work list" | `docs/memory/bulk-native-verdicts-0.3.1.md` |
| memory/perf flattening plan (the N2/achatamento track) | `docs/memory/achatamento-de-n2-plano-0.3.1.md` |
| `cl_shadow_nested` (#107 nested-closure shadowing test) | `src/checker/closures_test.tkt:101` |
| the honest-stop itself (`lower_var_fat`, declared `@throws`) | `src/lir/lower.tks:12337-12344` |
| the regression test asserting the stop fires | `src/lir/lower_test.tkt:2238-2245`; the degrau-4 fat-param fix `:2255-2265` |

*Grounding: all file:line real on `origin/fix/retirement`. No build/test/reseed run. Answer to the central
question: the gaps are WIP-known native-subset incompleteness (documented, tested, enumerated), not
regressions — decouple the clean memory verdict from the native-completeness signal.*
