# Teko — Decision Log

The decisions in force, and only those. This file restarts at **D1** on 2026-09-07: teko
is now a language taught to [`mc`](https://github.com/minicompiler/mc), the standalone
compiler that came before it is retired, and a log that grew for months around that
compiler could not be read end to end. Nothing here is a translation of the old entries —
each one was rewritten from what actually governs the work today.

Sources: owner rulings 2026-09-04 through 2026-09-07, and the mc project's answers on
packaging and the registry in the same window. A later entry supersedes an earlier one on
the same point; when a decision changes, the entry is rewritten and dated, not appended
to. The retired compiler's record, including its own D1-D231, is kept verbatim in the
private repository `teko-org/teko-history` — a `D<number>` cited in a source comment
refers to that record, not to this file.

---

### D1 · Teko is a language taught to `mc` (2026-09-07)
Teko has no compiler of its own. It is a set of hook modules — `teko.tk` and 30
`teko_*.tk` files (31 module files in all) plus the runtime `lib/rt.tk`, driven by
`core_teko.mc` and `user.mc` — that teach `mc` the constructs teko adds on top of its
base grammar. `mc` enters by release, pinned in
`MC_VERSION`, never as a submodule and never from a working clone; a version other than
the pin is not expected to build this tree. Raising the pin is its own change, taken only
after the whole local recipe is green on the new release.

### D2 · Zero changes to mc's core (2026-09-07)
Nothing in `minicompiler/mc`'s own sources changes for teko's sake. Everything teko adds
lives in this repository. When `mc` genuinely cannot express something, the answer is a
minimal reproducer written in pure `mc` and a report to that project — never a workaround
here, and never a patch there that this repository depends on before it is released.

### D3 · The surface follows C#, and only the delta is taught (2026-09-07)
Where C# has a form, teko takes it; where C# has none, the market decides; teko's own
older spelling is not inherited. The base grammar — expressions, statements, types,
functions, `loop`, `break N` — comes from `mc` and is reused as it is, so what this
repository teaches is the delta: classes, structs, interfaces, traits, generics,
delegates, properties, namespaces, dependency injection and reference counting. A fork
this log does not settle is decided by C#, recorded here, and the work continues; a halt
is for what neither C#, nor the market, nor `mc` answers.

### D4 · The surface is statically typed — no `Variant` (2026-09-07)
There is no dynamic union, no tagged run-time value, no "any" type. Every receiver,
argument and operand has a type the compiler knows, or it is a refusal at compile time.
Where another language would reach for a variant, teko uses an `interface` (dispatch
through the itable), a `class` (vtable) or generics with constants. A construct that
"wants" a variant is a fork, not something to emulate.

### D5 · Types: `class`, `struct`, `interface`, `trait` (2026-09-07)
`class` and `struct` share one layout machine and differ in what they carry: a class has a
vtable, a struct has none. `interface` is conformance plus an itable, and may carry a
default body and a static signature the implementing type must supply. `trait` works as
PHP's does — compile-time flattening of fields and methods into the using class, with the
class's own member winning over the trait's and the trait's over the base's. A trait is
not a type: no variable, no `new`, no conformance list.

### D6 · Members follow C#: `public`, `private`, `protected`, `static`, `internal` (2026-09-07)
C#'s defaults hold — a top-level type with no modifier is `internal`, a member with no
modifier is `private`. `internal` is measured against the **project's own code**: the
files the project's manifest brings in, not the namespace and not the file. Because
`internal` is teachable, there are no nested classes or structs; that was the excluding
pair, and `internal` won.

### D7 · Methods have no explicit receiver (2026-09-07)
A method does not declare `self`. The compiler injects the receiver; inside the body a
name resolves to a member of `this` when no local or parameter shadows it; `this` is
contextual for the explicit form and `base.m()` calls the base implementation
non-virtually. It holds for classes, structs, traits, interface signatures, constructors
(`Name(...) { }`) and destructors (`~Name()`). A static operator has no receiver.

### D8 · Operators are static members, resolved by overload (2026-09-07)
An operator is a static member of the type with its operands as explicit parameters
(`Vec operator+(Vec a, Vec b)`, and the reversed `Vec operator+(i64 k, Vec v)`), resolved
over the types of both operands among the operators declared by either. Pairs are
mandatory (`==`/`!=`, `<`/`>`, `<=`/`>=`). There is no vtable dispatch for operators.

### D9 · Control flow: both `switch` spellings, the ternary, `break N` (2026-09-07)
The `switch` statement and the `switch` expression both exist, as in C#. `break N` counts
the `switch` as one level, so `break` leaves the `switch` and `break 2` also leaves the
enclosing loop. `match` does not exist — the switch expression covers it — and `when`
survives as a `case` guard, sugar over `if`. The ternary `c ? a : b` is part of the
surface with C#'s precedence, and the switch expression is sugar over a chain of them.

### D10 · Abstract and partial (2026-09-07)
`abstract` is C#'s: an abstract class is not instantiable, an abstract member has no body,
occupies a vtable slot and obliges `override` in the first concrete derived class, and an
abstract member requires an abstract class. **Partial classes yes, partial methods no**: a
class may be declared in more than one place and is closed in the pass; `partial` on a
method is a clear error.

### D11 · Closures capture explicitly, PHP-style (2026-09-07)
A lambda or a local function declares what it captures with `use (a, b)` — by value by
default, by reference with `use (&a)`, which is `mc`'s own address-of-a-local. Nothing is
captured implicitly. The generated object carries exactly the fields the `use` list names,
and the whole thing is sugar over `mc`'s function pointer and indirect call.

### D12 · Reclaim: reference counting decided in the pass (2026-09-07)
Ownership and reference counting have one owner, the pass, never the parser: block depth
at parse time is not loop depth, and ownership depends on a static type a deferred member
access does not yet have. A store marks the site; the pass decides whether it is an owning
store; a destructor runs when the count reaches zero, before the fields are released.

### D13 · Dependency injection resolved at compile time (2026-09-07)
A class marks its lifetime by implementing `IServiceSingleton`, `IServiceScoped` or
`IServiceTransient`; registration happens while compiling and injection is resolved then —
no run-time container, no reflection. Injection is through the constructor, as in C#, and
`scope { }` opens a scope. Teko differs from C# on one point: **a Singleton may receive a
Scoped**, because the Singleton has a scope of its own; a Transient inherits the scope of
whichever of the two it is created in.

### D14 · Self-hosting is one unit, proved by the fixed point (2026-09-07)
The compiler compiles itself as a single unit, `mc_teko.tk`. The proof is the ladder
teko0 (stock `mc`) → teko1 → teko2 → teko3: the objects `teko2.o` and `teko3.o` must be
byte-identical, the two `--dump-asm` dumps must not differ, and teko1 must compile and run
every fixture with the right exit code. It runs on the five native pairs — linux/x86_64,
linux/aarch64, macos/aarch64, windows/x86_64, windows/aarch64 — each on a runner of that
operating system and architecture, so nothing is cross-compiled and left unproven.

### D15 · Distribution: the library `teko`, the tool `tekoc` later (2026-09-07)
The package is the repository: `mc.toml` at the root carries `[package]` and **no
`[project]`**, which is what makes the registry classify the name as a library; the build
config is `teko.toml`, read with `mc build . --config teko.toml`, because `mc pkg hash`
reads `mc.toml` and takes no `--config`. The compiler as a tool is a separate name,
`tekoc` (`kind = "exe"`), published later — the kind is fixed to a name at its first
publication, so the two cannot share one. `teko_std` is versioned in lockstep with `teko`;
every other library evolves on its own version line.

### D16 · How a consumer uses teko (2026-09-07)
A consumer pins `[deps] teko = "x.y.z"` and names the taught compiler in its own build:
`[compiler] modules = ["<teko/teko.tk>", "user.mc"]`, so its `mc` builds teko locally,
nailed by the tree hash. Once the registry's toolchain support lands (mc's R2/R3, agreed
2026-09-07), it validates a teko package by building the taught compiler from
`[package].modules` and the package's `[deps]` **first**, in a sandbox with no network, and
compiling the `check` units with it (D26); until then the validator compiles `check` with
the stock `mc`, which is why `teko`'s own unit, `mc_teko.tk`, is written in core syntax. Installing the compiler as a tool
(`mc tool install`) is the road that opens once `mc` ships it; until then the
`[compiler] modules` road is the one that runs, and it stays valid afterwards.

### D17 · Versions are `vX.Y.Z`; the first is v0.4.0 (2026-09-07)
Three parts, `mc`'s own format. The next release of the line taught to `mc` is **v0.4.0**;
**v1.0.0 ships only together with `mc` 1.0.0**, which is also when the port is considered
closed. Publication happens only from a stable version.

### D18 · This repository is English-only (2026-09-07)
Every file that lands here is written in English: documentation, code comments, commit
messages, PR bodies, workflow comments. Portuguese belongs in chat with the owner — who
reads Portuguese only — or in the private history repository. `sh scripts/check-docs.sh`
enforces it mechanically over the tracked sources, with the brand assets exempt because
they are metadata, not prose.

### D19 · The documentation gate proves the docs (2026-09-07)
`sh scripts/check-docs.sh`, the `docs` job in CI, checks five things: every relative link
resolves (link extraction skips fenced code, so a code sample is never read as a broken
link); no path of the retired compiler appears outside `docs/history/`; no Portuguese
appears in the public tree (D18); every `teko: …` diagnostic the modules emit appears in
`docs/reference/diagnostics.md`; and every fenced ` ```teko ` block either carries
`// expect-exit: N` — in which case it is compiled by the taught compiler and RUN, with the
exit code compared — or `// no-run`. A doc that cannot pass the gate does not merge.

### D20 · The refusals are a documented surface (2026-09-07)
The rule of the cut is **no silently wrong result**: a construct that is not taught is
refused where it is written, with a `teko: <short cause>` naming it, and the whole list
lives in `docs/reference/not-yet.md`. What is designed and not built lives in
`docs/specs/`; the reference describes only what runs. Nothing on the refusal list is a
promise about a later version.

### D21 · No new intrinsics (2026-09-07)
Every function has surface code. Nothing is recognised by name in the backend and
synthesised inline, and `mc limits` is the budget a construct fits into. A feature that
seems to require special handling in the backend is a fork: it is recorded here and asked,
not implemented quietly.

### D22 · The gates a pull request has to pass (2026-09-07)
`main` requires the five `ngen` legs, the five `fixpoint` legs, the `docs` job and the
aggregator, plus the branch-policy gate and CodeQL. **The aggregator keeps the literal
name `mc build ngen && run`**: a required check is matched by name, so renaming the job
without changing the ruleset in the same step would leave `main` waiting forever for a
check nobody reports. Merges are squash-only; a merge commit is blocked.

### D23 · How the work is dispatched (2026-09-07)
One agent at a time, each on its own branch and its own worktree, never the main checkout;
branch prefixes `ngen/**`, `feat/**`, `fix/**`, `docs/**`, `verify/**`; commit and push per
commit; `git config user.*` is never run. Each task runs scout → implementer →
independent verifier, and a PR merges only with CI green and **every Copilot review
finding resolved**. A dispatch that started wrong is killed and re-dispatched clean, never
patched in flight.

### D24 · Infrastructure follows the mc project's runbook (2026-09-07)
The VPS, Cloudflare and the package registry that serve this project are the same ones the
`mc` project operates, and its runbook is the source of truth for them. DNS records and
secrets are the owner's to change; this repository never holds a credential and never
provisions infrastructure of its own.

### D25 · The history moved out, and this log restarted (2026-09-07)
The frozen record — the retired standalone compiler, its decision log D1-D210, the
September 2026 handoff and the design documents of the port, all in Portuguese — moved
verbatim to the private repository `teko-org/teko-history`, together with the Portuguese
`CLAUDE.md` and `DECISION_LOG.md` as they stood. `docs/history/` keeps a pointer so no
link breaks. `CLAUDE.md` was rewritten from scratch for the mc era, and this log restarted
at D1 with the decisions in force. Nothing was translated: a decision worth keeping was
worth restating.

### D26 · Teko packages live in mc's registry, marked `toolchain = "teko"` (2026-09-07)
There is one integrated index, not a teko one beside an mc one: `pkg.minicompiler.dev` is
its canonical host, `pkg.teko-lang.org` an alias host serving the same bytes, and a
read-only `/mcp` endpoint answers over the same rows. A teko package **is** an mc package
plus `[package].toolchain = "teko"`, a key `mc` ignores and the registry will classify on
once its support lands (mc's R3): for a package carrying it the registry will build the
taught compiler from `[package].modules` and the package's `[deps]` first (mc's R2), then
compile the `check` units with it. Neither is live yet; the key is written now so the first
publication does not change the manifest again. Three shapes —
`teko`, the compiler; `teko_std`, the library, versioned in lockstep with it; and every
other library on its own line over `[deps] teko_std`. The closure rule is `mc`'s: a
package reaches its own files, the libraries the binary ships and its declared `[deps]`,
nothing else. On mc 0.16.0 `<float>` and the two float machines move into the `stdlib`
package and teko declares `[deps] stdlib`. The whole agreement is
[`docs/specs/packages.md`](docs/specs/packages.md).
