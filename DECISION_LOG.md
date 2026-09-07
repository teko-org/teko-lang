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
`[compiler] modules = ["<teko/core_teko.mc>", "<teko/teko.tk>", "user.mc"]` with `core = "<mc/core_min>"` and `out = "build/teko"`, so its `mc` builds teko locally,
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

### D27 · A generated declaration keeps the name of the one being parsed (2026-09-07)
`top_add` clears `p_decl_name()` (mc `docs/reference/hooks.md` § Asking about the parse),
and teko generates declarations from EXPRESSIONS: the thunk of `new Op(fn)`, the vtable,
release and allocator of `new T[n]`, `tk_ix` at an array-field index, a whole generic
instance re-parsed at `Box<i64> b = ...`. Every one of them fires while a declaration of
the program's own is still being read, whose rest still asks whose it is —
`tk_taint_owner` (teko_deleg.tk) keys the escape of a by-reference capture by it,
`tk_default_param` (teko_default.tk) starts a parameter row on it. So a generator adds
through `tk_top_emit` (teko_struct.tk), which restores the name, and a generator that
re-parses whole declarations saves and restores it around the replay. A bare `top_add`
from inside a body is the defect: it left the compiler with a null owner, which the
`str_eq` of `tk_taint_find` read through — an explicit thunk written before a
by-reference lambda crashed the compiler instead of compiling (K6). A generator is
invisible to the parse it interrupts.

The rule, in one line: **every top-level declaration teko emits from inside a body goes
through `tk_top_emit`.** K6's audit reads every `top_add` call site in the port as one of
three. **(a) Top level only**, inside a `class`/`struct`/`namespace` body read by
`parse_top` — member bodies and constructors (`teko_class.tk`), accessors
(`teko_prop.tk`), the static field and the struct constructor (`teko_struct.tk`), the
namespace body and its function (`teko_ns.tk`): no declaration of the program's own is
open. **(b) After the parse, from a `pass()`** — the memoized DI getter and its slot
(`teko_di.tk`): `p_decl_name()` is 0 already and nobody reads it any more. The `params`
pass emits from here too, and goes through `tk_top_emit` all the same, because what it asks
for is what `new T[n]` asks for mid-parse, from one shared `tk_ha_ensure_put`. **(c) Possibly mid-declaration, fired by an expression** — the
delegate thunk and the lambda's own four (`teko_deleg.tk`), the three of `new T[n]`
(`teko_heaparr.tk`), `tk_ix` (`teko_struct.tk`), and everything `tk_class_close` emits:
the vtable, the release, `tk_vt_init`, the interface table, the method table and each
allocator (`teko_class.tk`), because a `partial class` closes at its first USE and that
`new` may stand in any body. Every (c) site adds through `tk_top_emit`; the one exception
is `teko_generic.tk`'s replay, which saves and restores the name with the rest of its
scratch because `parse_top` writes it itself, once per declaration the instance produces.

The same invariant covers a body a handler reads ITSELF, with no generated declaration in
sight: an arrow accessor (`get => e;` / `set => s;`, `teko_prop.tk`) is not read by
`parse_function`, so its statements belonged to nobody and `tk_taint_owner` keyed them
under a null owner — the same crash. It now calls `p_set_decl_name`, which mc's hook
reference requires of exactly a handler that owns a declaration and reads it itself.

### D28 · The site is mc's generator over `docs/`, published to teko-lang.org (2026-09-07)
`docs/` is the source of the website and `site/` holds only what turns it into one: the
configuration, the templates, the stylesheet, the brand assets the pages reference and the
two Python checkers. **The generator is not vendored.** `mcsite` is `minicompiler/mc`'s
own, written in mc, and `.github/workflows/site.yml` checks that repository out at the tag
`MC_VERSION` pins — the same release `setup-mc` resolves — builds `mcsite` there and runs it
over `site/site.toml`; nothing of that checkout is read after the build, because `mcsite`
resolves the templates, the static files and `tools/*.py` against the directory holding
`site.toml`. So the generator cannot drift from the compiler the fixtures were proved on:
raising the pin moves both at once, and the templates carry the attribution the MIT licence
of the copied files asks for.

`--check` is the gate — every internal link, then `checkhtml.py` (structure, accessibility,
the three Content-Security-Policy rules) and `contrast.py` (WCAG ratios read out of the
stylesheet) — and it runs on a pull request touching `docs/**` or `site/**` as well, without
deploying. `sh scripts/check-docs.sh` covers `site/**/*.md` for the same reason. Deployment
is `actions/deploy-pages` on a push to `main` alone, with `pages: write` and `id-token:
write` granted to that job and no other; the artifact carries `public/CNAME`, so a
deployment cannot drop the custom domain. The canonical host is **`teko-lang.org`**, served
from GitHub Pages, and `[site] base_url` is `/` because the site is at the root of its own
domain. The registry link in the header is the index route that is live today
(`minicompiler.dev/packages`); it becomes the per-toolchain listing when mc's R3 lands
(D26).

A link to a repository **directory** is the one shape the generator does not map — it
resolves a `.md` of the site to its page, any other repository FILE to `edit_url`, and a
section directory to that section's index, leaving everything else as written and reporting
it. So a page that means the fixture directory names it by URL, not as `../../tests/`.

### D29 · The pin rises to 0.15.18 (2026-09-07)
`MC_VERSION` moves from `0.15.13` to `0.15.18` (`minicompiler/mc` PRs #44-#50): a comparison
benchmark and its docs, `M48` C1/C2 (package `[[permission]]`/`[tools]` and six sandbox
primitives), an HTTP soak workflow, and `M44` step 4 (`mc install`, the slim compiler
flavour). None of it touches the base grammar this repository teaches over.

One break, found by the baseline build against the new release before the pin moved:
`lex_set_libs` gained a second parameter, `hintfn` (`src/lex.mc`, landed in PR #50 — `mc
install`'s slim-compiler diagnostic asks the hint only when the compiler carries no
bundle at all). `core_teko.mc`'s call site — built with `<mc/core_bundle>`, so `bopen_fn`
is never 0 — now passes `0`; the hint is dead code for a compiler that always carries a
bundle. `docs/reference/hooks.md` at the `v0.15.18` tag still shows the old one-argument
signature — a doc lag on `mc`'s side, reported upstream, not patched here (D2).

Baseline (mc 0.15.18, `MC_VERSION` still reading `0.15.13`): 45/45 fixtures, `FIXPOINT OK`
(`teko1.o == teko2.o` on the first turn, `--dump-asm` diff empty), `mc limits` verdict ok,
`scripts/check-docs.sh` 63 samples / 277 links / 349 diagnostics. Only then was
`MC_VERSION` written and the literal `0.15.13` mentions (`CONTRIBUTING.md`,
`docs/guide/00-getting-started.md`, `.github/workflows/site.yml`) raised to `0.15.18`.
### D30 · `params T[]` is the only `params`; the word list is gone (2026-09-07)
`params` is a **modifier** read before the type, as `ref` and `out` are, and the type after
it is a genuine `T[]` — C#'s own form (D3). It goes on the last parameter of a **free
function**, one only, never with `ref`/`out`, never with a default, never on an `extern`;
in the body it is an ordinary array, and at the call site the compiler builds the array out
of the arguments, with a single argument that is already a `T[]` passing straight through
without a copy. The array is counted, so the statement that built it releases it and every
counted element with it.

The list of machine **words** it replaces — the declaration instantiated once per argument
count, the twelve-argument ceiling, the two refusals of a float, and the three runtime
helpers of `lib/rt.tk` that backed it — is **removed in the same change**, so no program has
two spellings to choose between and no dead body is left behind. Two consequences follow:
`&f` on such a function is legal, and there is no ceiling on the arguments at one site,
because the tail goes to memory rather than to the call convention.

`params` on a **method, a constructor, an interface signature or a `delegate`** is refused,
``teko: `params` is taught on a free function only``: the virtual path is five call-shaping
sites and a vtable slot keyed by signature, and refusing it trades a silent hole for a
message.

A list is **one signature among the name's**, and C#'s §12.6.4.5 decides a site by ROUND
ORDER alone: the exact-arity rounds and the default-completing one all run before any list
is asked to swallow a tail, so a candidate applicable in its **normal form** wins — `f(1)`
is `f(i64)`, and `f(1)` is `f(i64 a, i64 b = 5)`, before `f(params i64[])` is ever asked.
`f(1, 2)` and `f()` are the list; `f(xs)` with an `i64[]` in hand is the list's own normal
form, without a copy. Two lists of one name are told apart by their **element type**, and
between two that both take a site the one with **more declared parameters** wins. What is
left is two refusals the resolution already had: a tail no element type takes is
`teko: no overload of f matches these arguments`, and a genuine tie — the same declared
parameter count, both element types taking the arguments — is
`teko: more than one overload of f matches these arguments`. A call of an overloaded name
carries at most **64** arguments, because resolution types them as a set; a name declared
once has no ceiling. The design, the steps and what each of them measured are
[`docs/specs/params-typed.md`](docs/specs/params-typed.md).

### D31 · Ownership is a column of its own; purity is never asked to stand in (2026-09-07)
`tk_xt` ([`teko_struct.tk`](teko_struct.tk)) answers two questions about a node teko built,
and they are different questions. `xt_pure` is **"may this be evaluated twice?"** — read by
`tk_pure`, and by the virtual-call shaping alone, before `tk_clone` copies a receiver into
the vtable load. `xt_own` is **"does the value carry a reference of its own?"** — read by
`tk_rc_own` ([`teko_rc.tk`](teko_rc.tk)), and through it by the three lowerings of the
reclaim: a borrowed value is not parked, is incremented into an owning slot, and is
incremented on the way out of a counted `return`.

They agree for most nodes — a load is pure and borrowed, a call that hands out a reference
is neither — and that coincidence is what let one column carry both. It cannot: **a node may
be borrowed and effectful at once.** Every `tkarr_put_T` link of a `params` chain
([`teko_params.tk`](teko_params.tk)) is one — it stores an element, takes a reference for a
counted one, and hands the array back — and it was registered `xt_pure = 1` to say the chain
carries a single reference. That said "duplicate me freely" to whoever would.

The rule: **never mark a node pure to say it is borrowed.** Ownership is `TK_OWNED` or
`TK_BORROWED` in `xt_own`; `tk_xt_put`/`tk_xt_add` take both answers, so a registration
states both and the site that knows says so once. The audit of all forty registrations in
the port is
[`docs/internals/nodes-and-xt.md`](docs/internals/nodes-and-xt.md) § *Purity and ownership
are two questions*: the loads are pure and borrowed, the calls are neither, the `params` link
is borrowed and not pure, and six sites that said `pure = 0` about a NAME to mean "owned" now
say what they mean.

No accepted program changes by it. The `params` pass runs behind the oracle, the last pass
that consults `tk_pure`, so nothing had duplicated a link — the correction stops the next
pass to be moved from inheriting a licence nobody meant to give, and that is worth a column
rather than a comment.

### D32 · A float literal has a type of its own; it does not land on an integer (2026-09-07)
`1.5` is an `f64` — `<float>`'s `fl_lit` gives it the bit pattern of the value and that type,
in an `N_INT` node, because the core has no float node of its own. Overload resolution
([`teko_over.tk`](teko_over.tk), `tk_ov_args_fit`) read the node's KIND alone and treated
every `N_INT` as the untyped integer literal, so `f(1.5)` matched `f(i64)` in the **exact**
round, before `f(f64)` was ever asked: the eight bytes of the pattern reached a parameter
that reads them as an integer, silently and with the wrong value. The rule is C#'s — an
integer widens to a double, nothing narrows back without being written down — and it now
holds in every round: a float literal fits a parameter of its own float type and no other,
the same rule an element of a `params` list was already judged by (`tk_pm_elem_fits`).

A site left with nothing says what did not convert rather than that nothing matched. C#
answers such a call by naming the argument of a candidate (§12.6.4.5), so a float argument
no signature of the name takes **at that position, at that arity** is reported
`teko: a value of type f64 does not convert to i64` — the wording every other mismatched
value already gets — and a site whose ARITY is what failed keeps
`teko: no overload of f matches these arguments`.

The same rule holds where no overload is searched. `tk_check_compat`
([`teko_typeof.tk`](teko_typeof.tk)) judges a float by its KIND before anything else, so a
name declared ONCE refuses `g(1.5)` against `i64 g(i64)` exactly as an overloaded one does,
and so do an assignment, an initializer and a `return`. That check ran only in a unit
declaring some teko type; the gate ([`teko_rc.tk`](teko_rc.tk), `tk_compat_needed`) is
widened to every unit, since whether a float belongs in an integer slot cannot depend on
whether the file happens to hold a class. Nothing else follows the widening: with no row in
the type table, `tk_is_counted` answers 0 and every rewrite of the reclaim stays gated off —
the `--dump-ast` of all 45 fixtures is byte-identical to the one before the change.

`null` is the other N_INT that is not the untyped integer literal — `TY_UPTR`, value 0 —
and it is a reference, C#'s rule: `tk_ov_args_fit` lets it land on a parameter that is a
row of the type table or a raw `uptr`, and on no integer, in every round; before, the
exact round read it as an integer literal and `held(null)` with both `held(Cell)` and
`held(i64)` declared was `held(i64)`.

Two gaps are left standing, both older than this and neither a float in an integer slot: an
integer literal does NOT convert to a float parameter (the rounds refuse it, and a call of a
name declared once accepts it and passes the integer's own bits, unconverted), and a binary
mixing the two takes the type of its LEFT operand, which is the core's own rule — `2 * 1.5`
is an `i64` site and `1.5 * 2.0` an `f64` one.
