## What

<!-- One paragraph: what this PR changes and why. -->

## Checklist (project invariants — see CONTRIBUTING.md)

- [ ] **Base branch is `main`**
- [ ] All work lives in the repository root — the teko-over-mc port (`docs/design/port-teko-mc.md`)
- [ ] Toolchain pinned: built with the `mc` release named by `MC_VERSION` (never `latest`)
- [ ] `mc build . --config <host config>` builds and the fixture loop is green (exit 42/70)
- [ ] Fixpoint: `sh scripts/bootstrap.sh` prints `FIXPOINT OK` (teko1 == teko2, byte-identical)
- [ ] `ngen (mc) CI` green on every leg

## Design rulings

<!-- If this PR implements or depends on a design decision, link it (DECISION_LOG.md entry or
     docs/design/*.md section). Write "none" otherwise. -->
