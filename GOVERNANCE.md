# Governance

## Roles

Teko currently has a single maintainer and BDFL (benevolent dictator for life): **Elton Schivei Costa** ([@schivei](https://github.com/schivei)). The maintainer has final say on all design and code decisions.

## How design decisions are made — law first

Teko is unusual in that its design process is codified:

- **[TEKO_CONSTITUTION.md](TEKO_CONSTITUTION.md)** — the laws (M.0–M.5) every design ruling must satisfy.
- **[TEKO_LEGISLATION.md](TEKO_LEGISLATION.md)** — the record of ratified rulings.
- **[TEKO_MASTER_PLAN.md](TEKO_MASTER_PLAN.md)** — the ordered execution sequence for all open work.

Design tensions are resolved by testing each option against the laws (the option that passes all laws wins), not by preference or vote. Contentious cases go to a *tribunal*: a written analysis of every option against every law, recorded in the legislation. Proposals are welcome from anyone — file a design issue using the feature-request template, which asks for the law-first analysis up front.

## What is stable

Nothing yet. Until `1.0`, any syntax or semantics can change; ratified rulings in the legislation are the closest thing to stability guarantees (changing one requires a new tribunal, not a casual PR).
