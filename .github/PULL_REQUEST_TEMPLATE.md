## What

<!-- One paragraph: what this PR changes and why. -->

## Checklist (project invariants — see CONTRIBUTING.md)

- [ ] **Base branch is `main`** (or active remodel umbrella if during a wave)
- [ ] **Native engine only** (VM retired #524): all code in Teko (`.tks`), no C bootstrap
- [ ] **Seed from released binary**: `./scripts/fetch_teko.sh && ./.teko/teko . -o bin` (test gate green)
- [ ] Fixpoint: gen-1 rebuilds itself byte-identical (gen-2 == gen-1 after re-compile)
- [ ] **Coverage of new code:** at least 100% coverage on the delta
- [ ] New behavior covered by a regression example (`examples/regressions/…`) and/or `.tkt` tests

## Design rulings

<!-- If this PR implements or depends on a design decision, link the law/ruling
     (TEKO_CONSTITUTION.md / TEKO_LEGISLATION.md / master-plan item). Write "none" otherwise. -->
