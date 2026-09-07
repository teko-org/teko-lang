# History

Frozen record. Nothing here is rewritten to match the current port; it is kept verbatim
so no measurement, ruling or lesson is lost when the surface that produced it retires.

- [`handoff-2026-09.md`](handoff-2026-09.md) — the former `HANDOFF.md`, the full
  operational guide for the port as of September 2026, copied whole. Superseded page by
  page as [`../internals/`](../internals/README.md) fills in (D4 of the docs plan).
- [`decision-log-legacy.md`](decision-log-legacy.md) — `DECISION_LOG.md` entries D1
  through D210, the decisions of the retired standalone Teko compiler (the C-bootstrap
  engine). `DECISION_LOG.md` at the repository root keeps D211 onward, the port.
- [`design/`](design/) — the ten design documents that predate this tree: the plans that
  led to the port, the rebase, and this documentation itself, plus two drafts
  (`draft-CLAUDE.md`, `draft-CONTRIBUTING.md`) that seeded the root `CLAUDE.md` and
  `CONTRIBUTING.md`.

Why the cut lands here and not in a rewrite: the guide and the reference describe the
compiler as it is, checked mechanically against real fixtures
([`../../scripts/check-docs.sh`](../../scripts/check-docs.sh)); a page that also has to
stay a faithful history of every past decision could not be checked that way, and the
mix is what made the old `HANDOFF.md` and `DECISION_LOG.md` grow past the point anyone
could read them end to end.
