# The guide

Task-oriented, read in order: install the toolchain, get a program running, then one page
per part of the surface. Every fenced example is a **whole program**, compiled and run by
[`../../scripts/check-docs.sh`](../../scripts/check-docs.sh) with its `// expect-exit: N`
as the assertion, and derived from the fixtures in
[`tests/`](https://github.com/teko-org/teko-lang/tree/main/tests) — so
nothing here describes a construct the taught compiler cannot compile.

| page | covers |
|---|---|
| [00-getting-started.md](00-getting-started.md) | what teko is, the pinned `mc`, the taught compiler, a first program, the fixtures, the fixed point |
| [10-values-and-types.md](10-values-and-types.md) | scalars, the seven aliases, floats, `str`, `ptr`, `null` and the `T?` slot, fixed arrays and `T[]` |
| [20-classes.md](20-classes.md) | classes and structs, members, inheritance, `virtual`/`abstract`, interfaces, traits, properties, operators |
| [30-generics.md](30-generics.md) | type and `const` parameters, instantiation, inline array fields |
| [40-delegates-and-lambdas.md](40-delegates-and-lambdas.md) | delegate types, lambdas, explicit captures, the null panic |
| [50-control-flow.md](50-control-flow.md) | `loop`/`while`/`for`/`foreach`, `break N`, both `switch` spellings, the ternary |
| [60-parameters.md](60-parameters.md) | defaults, overloads, `ref`/`out`, `params` |
| [70-namespaces-and-imports.md](70-namespaces-and-imports.md) | `namespace`, `using`, `import`, several files, `partial` |
| [80-dependency-injection.md](80-dependency-injection.md) | the three lifetime markers, `inject`, `scope { }` |
| [90-memory.md](90-memory.md) | the arena, reference counting, roots, the guards that exit 70 |
| [95-packages.md](95-packages.md) | the manifest today, pinning `teko` and `teko_std` once published |
| [99-what-is-not-there-yet.md](99-what-is-not-there-yet.md) | the rule of the cut, and where the whole refusal list lives |

The exhaustive side, construct by construct, is [the reference](../reference/README.md);
what is designed and not built is [`../specs/`](../specs/README.md).
