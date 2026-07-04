## What

<!-- One paragraph: what this PR changes and why. -->

## Checklist (project invariants — see CONTRIBUTING.md)

- [ ] **Base branch is `main`** (the active development line)
- [ ] **SUPREME RULE**: every `.c`/`.h` change is mirrored in its `.tks` twin (and vice versa)
- [ ] Verified with the C bootstrap: `./build/teko build . -o bin` (test gate green)
- [ ] Verified self-hosted: `./bin/teko build . -o /tmp/gen2` (test gate green)
- [ ] Generated C byte-identical between engines (after gensym normalization); gen-2 == gen-3
- [ ] New behavior covered by a regression example (`examples/regressions/…`) and/or `.tkt` tests
- [ ] VM == native on the affected behavior

## Design rulings

<!-- If this PR implements or depends on a design decision, link the law/ruling
     (TEKO_CONSTITUTION.md / TEKO_LEGISLATION.md / master-plan item). Write "none" otherwise. -->
