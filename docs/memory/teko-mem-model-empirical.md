---
section: memory-implementation
created: 2026-07-13
source: DECISION_LOG.md (D29 round-2, D31b empirical findings), .claude/skills/dispatch/SKILL.md
---

# Teko Memory Model Empirical Findings (2026-07-13)

From D29/D31 VM+native empirical: **1.5GB was the VM interpreter (in-process functional env), not the arena.** The compiler's arena is ~366 MB (codegen + front-end), leak-to-root batch-safe with right-sizing (free-list + first-rung tuning + free-old-on-grow).

**Free-of-aliased is ASan-invisible:** only `TEKO_MEM_PARANOID=1` catches double-free or use-after-free of aliased pointers (the spine doesn't track them); vanilla ASan is silent (no instrumentation overhead). Native gate ASan + UBSan audit the production path's UB; PARANOID mode is dev-time defense.

**Cyclic graphs compile natively today:** DLL/cycle via class fields (ref-to-T field in class T) works; only `Ref`-as-field rejected (the inline case). Adopting for cycles is opt-in (`adopt { }`), not mandatory.

**Arena = default right-but-insufficient:** default arena is correct-by-construction (S0 seam, leak-to-root S2 discipline), but single region for whole process doesn't isolate lifetimes. **Spine is the bet** — inferred points-to/uniqueness (one-function + one-hop bounded) enables stored borrows + manual `mem::free`; spine audit pending, sound parity unverified.

**Hierarchy:** invisible arena (default) → `#must_free` + `defer`/`adopt` (safe lexical/explicit bulk-drop) → `unsafe #must_free Arena` (dev region with manual `mem::free`) → `RawBuf`/`Owned<T>` (malloc/free raw).
