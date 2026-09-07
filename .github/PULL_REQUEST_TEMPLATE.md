## What

<!-- One paragraph: what this PR changes and why. -->

## Checklist (project invariants — see CONTRIBUTING.md)

- [ ] **Base branch is `main`**
- [ ] All work lives in the repository root — the teko-over-mc port (`docs/history/design/port-teko-mc.md`)
- [ ] Toolchain pinned: built with the `mc` release named by `MC_VERSION` (never `latest`)
- [ ] `mc build . --config <host config>` builds and the fixture loop is green (exit 42/70)
- [ ] Fixpoint: `sh scripts/bootstrap.sh` prints `FIXPOINT OK` (`teko2.o` == `teko3.o` byte for byte, empty `--dump-asm` diff, the 45 fixtures pass under teko1)
- [ ] `ngen (mc) CI` green on every leg

## Design rulings

<!-- If this PR implements or depends on a design decision, link it (DECISION_LOG.md entry or
     a docs/specs/*.md or docs/history/design/*.md section). Write "none" otherwise. -->
