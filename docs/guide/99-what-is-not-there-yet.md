# What is not there yet

The rule of the cut is **no silently wrong result**: a construct v0.4.0 does not teach is
refused where it is written, with a `teko: <short cause>` naming it, so a program either
means what it says or does not compile. The whole list — every refusal and the message it
answers — is [not-yet.md](../reference/not-yet.md), and the messages themselves are
catalogued in [diagnostics.md](../reference/diagnostics.md). Nothing on that list is a
promise about a later version: what is designed and not built lives in
[`../specs/`](../specs/README.md), never mixed into the pages describing what runs.

The headline absences, so you meet them here rather than in a diagnostic: no type inside a
type, no type alias and no variant; no generic `delegate` and no `Func<>`/`Action<>`, no
multicast and no `op.Invoke(x)`; no `T[][]` and no fixed array of objects; no nested
`namespace`, no `using X = A.B;` and no `using static`; no `match` and no standalone
`when`; no `params` on a method or a constructor; no partial method; and, in
dependency injection, no generic key, no keyed registration and no factory.
