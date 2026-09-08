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

### D26 · Teko packages live in mc's registry, marked `language = "teko"` (2026-09-07)
There is one integrated index, not a teko one beside an mc one: `pkg.minicompiler.dev` is
its canonical host, `pkg.teko-lang.org` an alias host serving the same bytes, and a
read-only `/mcp` endpoint answers over the same rows. A teko package **is** an mc package
plus `[package].language = "teko"`, a key `mc` ignores and the registry will classify on
once its support lands (mc's R3): for a package carrying it the registry will build the
taught compiler from `[package].modules` and the package's `[deps]` first (mc's R2), then
compile the `check` units with it. Neither is live yet; the key is written now so the first
publication does not change the manifest again.

**Correction (2026-09-07, same day):** the key mc's R3 reads is `[package].language`, not
`toolchain` — `toolchain` is a word this log used for the concept before the mc side had
settled the key's own spelling, and it is never read by the registry or by `mc` itself.
R3 also DERIVES the language from `[deps] teko` when the key is absent, so a library that
only declares that dependency (`teko_std` and every other library over it) is classified
`teko` without carrying the key at all; `[package].language` is the explicit override a
package needs only when its own name does not say so on its own (the compiler package
`teko` itself, which declares no `[deps] teko`). `mc.toml` and
[`docs/specs/packages.md`](docs/specs/packages.md) carry the corrected key.

Three shapes —
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
(`minicompiler.dev/packages`); it becomes the per-language listing when mc's R3 lands
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
`held(i64)` declared was `held(i64)`. It lands on a `struct` as readily as on a `class`,
which C# does not allow: a struct value in teko IS a pointer to the allocation
([`docs/reference/types.md`](docs/reference/types.md) § `struct`), so `hold(null)` with
`hold(Vec)` and `hold(Cell)` both declared is ambiguous by design and not by oversight.

The two gaps this entry left standing — an integer literal that does not convert to a
float parameter, and a binary mixing the two taking the type of its LEFT operand — are
closed by **D33**.


### D33 · An integer converts to a float slot; nothing narrows back (2026-09-07)
C# §10.2.3's implicit numeric conversion, in every slot teko has one. An integer — a
literal, a local, a parameter, a field, of any integer KIND the core or a module
registered, `i32` included — written where an `f64` or an `f32` is declared becomes that
float. `tk_is_int_ty` (teko_typeof.tk) answers by ID, not by `type_kind`: a class, an
interface and `ref`/`out`/`params` all register `TK_INT` too (teko_struct.tk's,
teko_access.tk's and teko_ref.tk's own `type_new`), and every core type answers `TK_INT`
from `type_kind` regardless — `TY_VOID` and `TY_UPTR` included, neither a number — so the
kind alone cannot tell an integer from a reference. `i32` is the one integer id outside the
core's five raw ones (`TY_U8`..`TY_I64`): `core_types_init()` registers it before any
module's `user_init()` runs (`hooks.md` § "the id a registration returns"), so it always
answers exactly `TY_MAX`, deterministically, before a class or an interface of the
program's own claims the ids after it. The core converts
none of them: `walk_narrow` (mc/src/gen_walk.mc) answers 0 for `TK_FLOAT`, so the eight
bytes of the integer reached the slot and were read as a mantissa — `f64 y = 1` was zero,
`p.w = 4` was zero, `g(3)` against `f64 g(f64)` was zero, and `1 + 2.5` was neither three
point five nor a refusal. The one surface form that lowers to `scvtf` is the cast, so the
conversion IS a cast, written by the compiler where the source did not have to: `tk_cast`
([`teko_array.tk`](teko_array.tk)) through the one helper `tk_num_widen`
([`teko_typeof.tk`](teko_typeof.tk)). Zero new intrinsics, zero new passes, nothing
changed in `mc` (D2, D21).

**The nine slots**, each in the module that owns it: a variable initializer, an assignment
and a `return` ([`teko_rc.tk`](teko_rc.tk)); an argument of a free or method call (same
file), of a virtual call ([`teko_expr.tk`](teko_expr.tk)) and of an interface call
([`teko_iface.tk`](teko_iface.tk)); an element of a `params f64[]`
([`teko_params.tk`](teko_params.tk)); a field store ([`teko_typeof.tk`](teko_typeof.tk),
shared by the two parse-time sites that build one); and a binary mixing the two
([`teko_ops.tk`](teko_ops.tk)). The helper hands the node BACK and the caller splices it,
because an argument is a link of a sibling list and an operand is a child — the shape
`tk_deleg_coerce` ([`teko_deleg.tk`](teko_deleg.tk)) already had.

**The binary converts the integer operand whichever side it stands on.** `res_binary`
(mc/src/gen_resolve.mc) recomputes the expression's type from the LEFT operand once it is
resolved, so converting the left is what makes the site a float one and converting the
right is what keeps a site that already was one from mixing a register in: `1 + 2.5` and
`2.5 + 1` are both three point five, which supersedes D32's "the type of the LEFT
operand". Only the operators C# promotes for are asked — `+ - * /` and the six
comparisons. A shift and the bitwise trio take no float in C# at all, and `%` has no float
instruction on this backend (`<float>` has add, sub, mul, div and compare, and `2.5 % 7`
is already `mc: no float remainder`), so promoting either would only move the failure.

**Nothing narrows back**, and the refusal is the wording every mismatched value already
gets, `teko: a value of type f64 does not convert to i64`. `null` joins it: it is a
reference (D32) and a number is not, so a numeric slot refuses it —
`teko: a value of type uptr does not convert to i64` — where before `solo(null)` against a
single `solo(i64)` compiled and passed a pointer-width zero. Both verdicts live in
`tk_check_scalar_compat`, the half of the compatibility check that needs no row of the type
table, which is what lets the FIELD store share them: its own check reads its value through
`tk_struct_of_expr`, which answers about objects only, and the two scalar cases had never
been asked there at all.

**`params f64[]` takes an integer element** at the LOOSE stage of the expanded round, C#
§12.6.4 over §10.2.3: a list is applicable in expanded form when every element of the tail
converts. Asking it at `loose` only is what keeps D30's tie-break — the strict stage runs
first, so `f(1)` is still `params i64[]`, while `f(1, 1.5)` fails every strict candidate
and lands on `params f64[]` with the `1` converted. The ORDINARY overload rounds are not
widened: `f(3)` with `f(f64)` and `f(uptr)` declared still reads
`teko: no overload of f matches these arguments`, since a better-conversion rule (§12.6.4.4)
is a decision of its own; a name declared once always converts.
[`docs/reference/not-yet.md`](docs/reference/not-yet.md) § *Numeric conversions* carries
that and the five other gaps measured here, `f32`→`f64` among them.

**A NEGATIVE `i32` widened to a float is wrong on `aarch64`, and it is `mc`'s own defect,
not taught here.** `tests/primitives_float.tk` was rewritten again to add `i32` at the four
of the nine slots it can reach as a source (`i32` never converts an ARRAY element or a
`params f64[]` one, since neither of those declares an `i32` element in this crumb) —
caught mid-review, before `i32` reached `tk_is_int_ty` at all, a POSITIVE `i32` widens
correctly on every target, and a NEGATIVE one does too on `x86_64`
(`lib/machine_x86_64_float.mc`'s `fx_cast` special-cases only `TY_U64`/`TY_UPTR` and signs
everything else), but not on `aarch64`: `lib/machine_arm64_float.mc`'s `fa_cast` chooses
`ucvtf` over `scvtf` for any integer source that is not the exact id `TY_I64`, so a sign-
extended `i32` register reads as a huge positive double. Reproduced on `bea5ccce`, BEFORE
this fix, through an explicit `(f64) d` cast on a negative `i32` local — `i32` never reached
an IMPLICIT float slot before `tk_is_int_ty` learned its id, but the explicit cast already
took the same broken lowering, so the defect is `mc`'s own and older than this crumb (D2): a
minimal pure-`mc` reproducer is reported upstream, not patched here, and the fixture proves
only what is true on every target — a positive `i32` and, in an integer-only slot, a
negative one.

**The parse-time table of locals becomes a scope.** `tk_slv`
([`teko_struct.tk`](teko_struct.tk)) grew ever-forward over the whole unit, so a name
declared in one function answered for a name spelled the same in the next: an `f64 s` local
typed the `i64 s` PARAMETER of another function, and a virtual call passing it was refused
for a conversion nobody wrote — measured on `b60a4635`, before this change, so the defect is
older than it. The two questions are separated instead of merged: the rows stay append-only
for the one PASS that reads them with every scope already closed
(`tk_pm_arg_ty`, [`teko_params.tk`](teko_params.tk)), and a stack of indices beside them,
cut at the same `}` `tk_local` is, is what the parser asks. A parameter is in no scope table
at parse time, so the honest answer for it is -1, "not known here", which every caller
already treats as "refuse nothing".

**A refusal still has no harness.** `tests/` holds programs that compile and run, judged by
`// expect-exit`, and there is no `// expect-refuse:` form; the two refusals above are
documented with a `// no-run` fence in
[`docs/reference/diagnostics.md`](docs/reference/diagnostics.md) and
[`docs/reference/types.md`](docs/reference/types.md), and the harness is a crumb of its own.

Proof: 45/45 fixtures at their `expect-exit`, `FIXPOINT OK`, `mc limits` verdict ok with
`passes` still 15/30 and `intrin` still 8/16, and `--dump-ast` byte-identical on 44 of the
45 — the one that moves is `tests/primitives_float.tk`, rewritten here to cover the nine
slots with values that only come out right if the conversion happened.

### D34 · A non-null reference does not convert to a numeric slot either (2026-09-07)
D33 closed the integer-to-float direction and the two scalar verdicts (`null`, a float
narrowing) `tk_check_scalar_compat` ([`teko_typeof.tk`](teko_typeof.tk)) already judged
without a row of the type table. A THIRD value needed the same treatment and did not have
it: `Foo f = new Foo(); f64 x = f;` — a class reference reaching a float or an integer
slot — compiled and passed the pointer through as the slot's own bit pattern, silently,
in every one of the nine slots D33 already covers (an initializer, an assignment, a
`return`, an argument of a free/method/virtual/interface call, an element of a `params
f64[]`, and a field store). `null` already refused there (D32/D33) and a float already
refused there; a struct, a class, an interface, a delegate and a `T[]` of heap — every
value whose type is a row of the type table, `tk_struct_by_ty(ty) >= 0` — did not, because
`tk_check_compat` ([`teko_typeof.tk`](teko_typeof.tk)) asks the row question only when the
TARGET is a row (`ti = tk_struct_by_ty(tty); if (ti < 0) return;`), which is exactly a
numeric slot. C# has no such conversion in either direction (§10.2, §6.2), so the fix
mirrors D33's own scalar half: `tk_check_scalar_compat` now asks, once the float/`null`
cases are past, whether the VALUE's type (`ety`) is a row while the slot (`tty`) is
numeric, and refuses with the wording every mismatched value already gets —
`teko: a value of type Foo does not convert to f64`. The check is on the value, never the
slot, which is what keeps a `ref`/`out`/`params` slot out of it: those are `type_new`
ids outside `tk_is_int_ty`'s range (teko_ref.tk's, teko_access.tk's own registration,
D33's own reasoning), so the slot side of the new check never matches one.

**A pre-existing defect surfaced once the new refusal could see it.**
`tk_ty_of` (teko_typeof.tk), the pass-time type oracle `tk_rc_call_args`
([`teko_rc.tk`](teko_rc.tk)) asks for a call argument's type, does not know about a
`ref`/`out` argument's tag (`tk_rfarg_kind`/`tk_rfarg_pointee`, teko_ref.tk) — it reads
the argument's ADDRESS expression by shape instead, the same way any other node is read.
`ref p.x`'s address is `p + offset`, an `N_BINARY`, and the core's own rule for a binary
(kept by `tk_ty_binary`) is the LEFT operand's type — `p` itself, `Point` — so
`bump(ref p.x)` against `bump(ref i64 x)` was asked, from this crumb's new check onward,
whether a `Point` converts to the parameter's own pointee `i64`, and refused a program
`tests/surface_refout.tk` already proved correct. `tk_pty_of`
([`teko_struct.tk`](teko_struct.tk)), the PARSE-TIME twin `tk_vcall_args_check`
([`teko_expr.tk`](teko_expr.tk)) asks the same question through, never had this problem —
it does not recognize `N_BINARY`/`N_ADDR` at all and answers -1, "not known", which every
caller already treats as "refuse nothing" — so only the pass-time free-call site broke.
The fix is `tk_ty_of`'s own, at the top: a `ref`/`out`-tagged node answers its POINTEE
(`tk_rfarg_pointee`), falling back to the live scope by name for a bare local whose
pointee was not yet known at parse time — the exact lookup `tk_ov_arg_ty`
([`teko_over.tk`](teko_over.tk)) already makes for the overload matcher, over the same
side table. No accepted program's `--dump-ast` moves: a `ref`/`out` argument's `at` was
either already right (a bare local, unresolved at parse time, both answered -1 before and
answer its scope type now, and `tk_check_compat(pt, pt, ...)` refuses nothing) or wrong in
a way nothing downstream ever read (`tk_num_widen` never converts a class into anything),
so the correction is invisible to every one of the 45 fixtures and visible only to the
refusal this crumb adds.

**The inverse direction — a number reaching a reference slot — was checked, not
widened.** `Foo f = 5;`, `f = 5;` and `g(5)` against `Foo g(Foo)` already refuse, through
`tk_check_compat`'s own row check (`tk_row_fits(ti, ei)` with `ei = tk_struct_by_ty(ety)
< 0` for a scalar value, which the identity/derives/implements rule always fails). `Foo f
= null;` still accepts (D32: a reference fits any row). A raw `uptr` value into a
reference slot (`Foo f = raw;`, `raw` an `uptr` local, not the `null` literal) already
refuses too — `teko: a value of type uptr does not convert to Foo` — an asymmetry with the
numeric-slot side (a raw `uptr` there is left alone, D33), pre-existing and out of this
crumb's scope. One genuine gap stayed open and is not fixed here, out of scope by the
crumb's own instruction: `h.f = 5;` on a field of row type, at the PARSE-TIME field store
(`tk_check_field_store`, teko_struct.tk) — it reads the value through
`tk_struct_of_expr`, which answers about an object expression only, so a scalar value
answers "not known" and the store proceeds, writing the integer's bits where a pointer is
expected. Recorded in [`docs/reference/not-yet.md`](docs/reference/not-yet.md) § Numeric
conversions, for a crumb that fixes it deliberately.

Proof: 45/45 fixtures at their `expect-exit` (`tests/surface_refout.tk`'s `bump(ref p.x)`
included), `--dump-ast` byte-identical to `2dc1c22a` on all 45, `FIXPOINT OK`,
`sh scripts/check-docs.sh` green, `mc limits` unchanged row for row against the same
baseline (the pre-existing `passes`/`intrin` warnings do not move). Fourteen probes
outside `tests/` (`build/probe_*.tk`, not committed) prove the new refusal at every
slot — an initializer, an assignment, an argument, a `return`, a field store, a
`params f64[]` element, a `struct` value and a `delegate` value — and the remaining six
prove the inverse direction's existing behaviour (an integer refused at an initializer,
an assignment and an argument; `null` accepted; a raw `uptr` refused) plus the one gap
left open (an integer into a field, silently accepted).

### D35 · The pin rises to 0.15.22 (2026-09-07)
`MC_VERSION` moves from `0.15.18` to `0.15.22` (`minicompiler/mc` PRs #51-#57). The reason
is the registry, not this repository: the registry's validator runs an `mc` newer than
whatever tag it is checking, and `v0.4.0` was cut pinned at `0.15.13`, whose `lex_set_libs`
still took one argument — the validator's own `mc` already carries the two-argument form
(PR #50, landed in `0.15.18`, D29), so every validation of `v0.4.0` died `wrong number of
arguments` before it ever reached this repository's own gate. `v0.4.1` is the fix: the same
cut, re-tagged from `main` once `main` itself is pinned on an `mc` whose hook API matches
what the validator runs. 0.15.22 also carries the `[replace]` fix (`mc` PR #57,
`5747958`): `deps_apply` (`src/deps.mc`) applied `dep_resolve` before `dep_replace`, so a
package named in `[deps]` and pinned in `mc.lock` but never fetched, and pointed at a local
path by `[replace]`, died `is not fetched` before the replacement was ever consulted —
`[replace]` was unusable on its own. This repository's `mc.toml` carries no `[replace]`
table today, so the fix changes nothing here; it is cited because it is the other
user-visible change in the range and because a package under `teko_std` may lean on it.

**Zero code changes.** `docs/reference/hooks.md` at `v0.15.22` differs from `v0.15.18` by
two additions only — `host_self_path()` and `host_getcwd()`, both new hooks for `mc tool`
(PR #55) and `mc upgrade` (PR #53), neither called from this repository — and the
`lex_set_libs` row, unchanged since the D29 pin (`core_teko.mc`'s `lex_set_libs(&libs_open,
0)` call site is already the two-argument form). The rest of the range (`M49` steps A/D1 —
the `-O` flag and the arm64 register allocator; `M44` step 5 — `mc upgrade`; the bootstrap
decoupling of PR #54) is codegen and driver internals behind hooks this repository does not
call. No hook this repository calls changed signature or behaviour.

Baseline (mc 0.15.22, `MC_VERSION` still reading `0.15.18` during the proof): 45/45
fixtures at their `expect-exit`, `FIXPOINT OK` (`teko1.o == teko2.o` on the first turn,
`--dump-asm` diff empty), `sh scripts/check-docs.sh` green (292 links, 345 diagnostics, 68
samples), `mc limits . --config mc.macos.toml` verdict `ok` on every table for both
`build/teko.mc` and `tests/hello.tk`, unchanged in shape against the 0.15.18 baseline (the
raw estimates move a few percent — the estimator is `mc`'s own, not this repository's — but
every table stays `ok` at `tolerance = 1.0`). `mc pkg hash .` is byte-identical between the
two releases (`3e6feff8e981912423654dbc12ca6d4948efa89d21ea02128b7636f0683791b1` — the
source tree did not move). `--dump-ast` of all 45 fixtures, compiled by the taught compiler
built once by each release, is byte-identical between `0.15.18` and `0.15.22` on every one
— the codegen and driver changes in the range touch nothing this repository's grammar
parses into. Only then was `MC_VERSION` written and the literal `0.15.18` mentions
(`CONTRIBUTING.md`, `docs/guide/00-getting-started.md`, `.github/workflows/site.yml`)
raised to `0.15.22`.

### D36 · The site is served from the VPS, not from GitHub Pages (2026-09-08)
D28 put teko-lang.org on GitHub Pages because that is where `minicompiler/mc`'s own
`site.yml` deploys. The domain itself, though, is not served that way on mc's side: the
`A` record of `minicompiler.dev` points at the VPS, proxied by Cloudflare, and Traefik on
that server routes it — and GitHub never issued the certificate for teko-lang.org, twelve
hours of `authorization_created` with `pages/health` reporting every check green. The owner
asked why Pages at all when the server is there; there was no reason left.

So the site is served the way the registry is. `site.yml` still builds `mcsite` from the
pinned mc tag and renders `docs/`, and on a push to `main` it commits the rendered tree as
an orphan commit on the **`site` branch**, force-pushed with the workflow's own token. On
the VPS, `/srv/teko-site/pull.sh` (cron, every five minutes) checks that branch out into the
directory an `nginx` container serves, and `/etc/dokploy/traefik/dynamic/teko-site.yml` —
hand-written like `mc-panel.yml` — routes `teko-lang.org` and `www.teko-lang.org` to it,
behind the `mc-cf-only` allow-list so the origin answers Cloudflare alone. **No deploy
credential exists anywhere**: the repository is public and the server only reads it. In
Cloudflare the apex `A` record moves from GitHub's four addresses to the VPS, proxied, and
`www` becomes a `CNAME` to the apex; the three redirect domains do not change. The `CNAME`
file the Pages artifact carried is gone with the artifact.

### D37 · The pin rises to 0.15.23 (2026-09-08)
`MC_VERSION` moves from `0.15.22` to `0.15.23` (`minicompiler/mc` PRs #58-#59). The reason
is a defect this repository itself reported: on `aarch64`, `lib/machine_arm64_float.mc`'s
`fa_cast` decided signedness with `t == TY_I64`, true only of the full-width signed
integer, so every narrower signed source (`i32`, and any `i16`/`i8` a module registers,
`mc`'s `TK_SINT` kinds) took the UNSIGNED conversion instructions — `ucvtf` widening,
`fcvtzu` narrowing. `i32 d = -5; f64 x = (f64) d;` read the sign-extended `-5` register as
a huge positive integer instead of a negative one, correct on `x86_64` (whose `fx_cast`
already treated everything but `TY_U64`/`TY_UPTR` as signed) and correct on `aarch64` too
for a non-negative `i32`, which is why the earlier D33 fixture (`primitives_float.tk`)
never caught it — it probes `2147483647`, the positive boundary, never a negative one. PR
#59 tests `type_signed()` (`t == TY_I64 || type_kind(t) == TK_SINT`, already exported from
`src/ast.mc`) at both cast directions instead: `SCVTF`/`FCVTZS` for a signed source or
target, `UCVTF`/`FCVTZU` otherwise. PR #58, in the same range, touches `Makefile` and
`site/` only (a glibc `mcsite` render and `make bundle`'s in-place rewrite) — nothing under
`src/`, `stage0/`, `lib/` or `tools/` outside `machine_arm64_float.mc` itself.

**Zero code changes here beyond the fixture.** `docs/reference/hooks.md` does not appear in
the `v0.15.22...v0.15.23` diff at all (`gh api repos/minicompiler/mc/compare/...`), and
neither does `scripts/sysroot-windows.sh` — the kernel32 export list
`.github/actions/windows-sysroot/action.yml` carries stays the same nineteen names; no sync
was needed. This repository calls no `<float>` hook directly (D33's own
finding stands: the bug lives entirely inside `mc`'s bundled float machine, reached only
through the surface `(f64)`/`(i64)` casts teko already taught), so no hook signature or
behaviour this repository depends on moved.

`tests/primitives_float.tk` gains the negative-`i32` half D33 left out on purpose
(`docs/reference/not-yet.md` § *Numeric conversions*, now removed — the gap it named is
closed): a negative `i32` widened to `f64` at the same three slots the positive boundary
already covers — a variable initializer, a field store, a free call's argument — plus a
narrowing `(i64)` cast back, which exercises `fcvtzs` the same way. `expect-exit: 42`
unchanged; the fixture now returns 22-25 for the four new checks instead of falling
through past 21.

Proof: a standalone probe (`i32 d = -5; f64 fd = d; if (fd < 0.0) return 42; return 1;`)
built by the taught compiler exits `1` under mc 0.15.22 (the bug, reproduced) and `42`
under mc 0.15.23 (fixed) — same probe source, same teko frontend, only the `mc` release
under it changed. 45/45 fixtures at their `expect-exit` (`primitives_float` included, now
at its widened `expect-exit: 42` through the new checks), `FIXPOINT OK` (`teko2.o ==
teko3.o` on the first turn, `--dump-asm` diff empty), `sh scripts/check-docs.sh` green (397
links, 345 diagnostics, 83 samples), `mc limits . --config mc.macos.toml` verdict `ok` on
every table for both `build/teko.mc` and `tests/hello.tk`, unchanged against the 0.15.22
baseline. `mc pkg hash .` unchanged (`3e6feff8e981912423654dbc12ca6d4948efa89d21ea02128b7636f0683791b1` —
the hash reads no path this pin's changes touch). `--dump-ast` of all 45 fixtures, taken on
the tree BEFORE the fixture edit (so the comparison is the pin alone), is byte-identical
between `0.15.22` and `0.15.23` on every one — confirming the codegen fix changes no AST
this repository's grammar produces, only the instructions the backend emits underneath it.
Only then was `MC_VERSION` written and the literal `0.15.22` mentions (`CONTRIBUTING.md`,
`docs/guide/00-getting-started.md`, `.github/workflows/site.yml`) raised to `0.15.23`; the
two `0.15.22` mentions left alone are historical (D35's own entry above, and
`windows-sysroot/action.yml`'s comment on which release added which kernel32 export, both
about the PAST release and not the current pin).


### D38 · `i8`/`i16`, and every indirect load teko builds for itself (2026-09-08)
`docs/specs/small-ints.md`'s N0, the first crumb of the numeric-types sequence
([`docs/specs/README.md`](docs/specs/README.md)). Two lines in `teko_type.tk` —
`type_new("i8", 1, 1, TK_SINT)` and `type_new("i16", 2, 2, TK_SINT)`, kept in their own
globals (`tk_ty_i8`/`tk_ty_i16`) the way every registration in this port is — and
`tk_is_int_ty` (teko_typeof.tk) rewritten from `t == TY_MAX` (a number written down, and
`i32`'s own id read by coincidence) to the positive list the spec's § 2 states: the core's
five raw ids, `i32` by its own exported name (`ty_i32`, `src/hooks.mc`), and this crumb's
two. The rule that goes with it, in the predicate's own comment: **a `type_new` id joins
`tk_is_int_ty` only when the module that registered it says so, by name, there** —
`type_kind(t) == TK_SINT` is not a shortcut for the same answer, because `TimeSpan`,
`DateTime` and every future `enum` will register that kind too, and none of them is a
number.

**The spec's own claim — "two lines, and one predicate, nothing else" — held for every
DECLARED local, parameter, global and array element (the core's own `walk_narrow`,
`type_signed` and `fold_taught` do all of that by kind, M45), and did not hold for what
teko generates for ITSELF.** Every indirect load this project builds — a struct/class
field, a `ref`/`out` pointee, a static field, a property's auto-generated getter, a
delegate's by-value capture, an inline array field, a pending (parse-deferred) field
access — reads through `tk_ldn`'s raw `ld8`/`ld16` (teko_struct.tk), the core's own FIXED
intrinsics, always zero-extending. Correct for the three unsigned narrow types this
project already had (`u8`/`u16`/`u32`); silently wrong for a signed one: `box.v = -9;` read
back `247`. Caught by this crumb's own fixture, not by inspection — `docs/specs/small-ints.md`
never named the gap because its own worked example never round-tripped a value through a
STRUCT FIELD, only through locals, a parameter, a return and a local array (all core
grammar, all already correct).

`teko_array.tk`'s own `tk_arr_load` already carried the fix, for `i32`, its only signed
narrow element before this crumb (a comment there says so: "the raw load is always
zero-extending ... so a signed narrower-than-word element ... is cast to its own type
afterward"). `i32` never reached a struct field, a `ref`/`out` pointee or any of the other
six shapes above with a NEGATIVE value in any existing fixture, so the same gap already
existed for `i32` and stayed invisible until `i8`/`i16` — and this crumb's fixture, which
does exercise every one of those seven shapes — made it observable. The fix is `tk_ld`
(teko_struct.tk), `tk_arr_load`'s own two-line check factored out once: wrap the raw
zero-extended load in `tk_cast` (the one surface form that sign-extends by kind,
`MTASK_CAST`) when the type is `TK_SINT` and narrower than the word. The ten raw
`tk_call(tk_ldn(ty), addr)` sites this port had — teko_struct.tk's own array-field index,
teko_access.tk's `ref`/`out` and its static-field chain, teko_expr.tk's field load and its
delegate-field call, teko_deleg.tk's closure prologue, teko_prop.tk's auto-accessor body,
teko_heaparr.tk's delegate-element call, teko_this.tk's own field read, and
teko_typeof.tk's pending-field resolution — now go through it; `teko_array.tk`'s own
`tk_arr_load` (already correct) and `teko_loop.tk`'s single `TY_UPTR` site (never narrow)
are untouched. Zero new intrinsics, zero changes to `mc` (D2/D21): the fix is the same cast
`(i8) x` already lowers to, written by the compiler where ten sites did not write it.

**No accepted program's `--dump-ast` moves.** The check only fires for `type_kind(ty) ==
TK_SINT && type_width(ty) < 8`, and no existing fixture reads a struct/class field, a
`ref`/`out` pointee, a static field, a property, a delegate capture or an inline array
field of `i32` type with a value the sign bit of which matters for the assertion — every
`i32` use in the existing 45 is either a local, a parameter, a `return`, a widening source,
or a field/argument the fixture reads back through a comparison the bug's own zero-extension
does not change the OUTCOME of (`primitives_float.tk`'s `b.w` is an `f64` field, not an
`i32` one — the value flows through it, the field itself is never narrow-signed). The ten
sites now route through `tk_ld` produce the identical two-node shape `tk_arr_load` already
had for `i32`; for every OTHER type (`TK_INT`, `TK_FLOAT`, a row of the type table) the
function returns the raw call unchanged, byte-for-byte the same node the old code built.

`tests/primitives_small_ints.tk` (`expect-exit: 42`) is the fixture: negative `i8`/`i16`
locals with a signed `>>`, `/`, `%` and comparison; a local array (the core-grammar
baseline); every one of the seven indirect-load shapes above, each with a value whose sign
bit the assertion depends on; the nine-slot `f64` conversion (an initializer, an
assignment, a free call's argument, an explicit narrowing cast back); and the wrap
arithmetic `docs/specs/small-ints.md` § 4 states — `(i8) 100 + (i8) 100` is `200` in
flight and `-56` at the next store, `(i16) 32767 + (i16) 1` wraps to `-32768` the same
way, because teko does not teach C#'s integer promotion (a decision `u8`/`u16`/`u32`
already made, D3 forbidding a change to `mc`'s own `+`).

`sbyte`, `short`, `ushort`, `int`, `uint`, `long`, `ulong` stay ordinary identifiers
([not-yet.md](docs/reference/not-yet.md)): the spec's own § 1 leaves the seven-word C#
alias family an open fork this crumb does not take.

Proof: `mc build . --config mc.macos.toml` clean; 46/46 fixtures at their `expect-exit`
(the 45 existing plus `primitives_small_ints`); `--dump-ast` of the 45 existing fixtures
byte-identical to `29a7825f`; `FIXPOINT OK` (`teko2.o == teko3.o` on the first turn,
`--dump-asm` diff empty, 46/46 under the self-hosted `teko1`); `sh scripts/check-docs.sh`
green (399 links, 345 diagnostics, 85 samples — no new diagnostic string, so
`docs/reference/diagnostics.md` needed no change); `mc limits . --config mc.macos.toml`
verdict `ok` on both `build/teko.mc` and `tests/hello.tk`, `types` `7/14` to `9/14` and
`alias` `14/28` to `16/28` — the spec's own § 10 table predicted `alias` would stay put;
measured, a `type_new` registration moves it the same as `types`, by the count of new
words, not just the estimator's `types` row — `passes` `15/30` and `intrin` `8/16`
unmoved, exactly as the spec's table also said. `mc pkg hash .` reported in the PR.

### D39 · `enum`: the type, the members, the operators, the `switch` (2026-09-08)
`docs/specs/enum.md`'s N2a, the first crumb of the `enum` sequence
([`docs/specs/README.md`](docs/specs/README.md)): a new module, `teko_enum.tk`, and small
additions to five existing ones, over the exact mechanism the spec names — a class is
`type_new` plus `syntax_expr`/`syntax_stmt`, both at parse time, and a `const` member is a
folded row in `teko_const.tk`'s own qualified-constant table; an `enum` is those two,
pointed at each other, with one new kind (`TK_KENUM`, teko_struct.tk) so every existing
check that answers "is this a reference" (`tk_is_counted`, `tk_op_row`'s callers) keeps
answering "no" for it by construction.

**The declaration** (`teko_enum.tk`) reads the eight underlying types through the core's
own `p_type()`, filtered by `tk_is_int_ty` (teko_typeof.tk, D38) — the exact positive list
that predicate already carries, so no separate word table duplicates it — and registers
`type_new(name, width, align, kind)` at the underlying type's OWN width/align/kind (`i32`,
`(4,4,TK_SINT)`, by default, C#'s own), not the pointer-width `TK_INT` every class/struct
gets. Members take the previous value plus one, fold through the core's own `fold()` for an
explicit one, and are written into `teko_const.tk`'s existing member-const table
(`tk_mconst_add`), which is what makes a duplicate member NAME refuse for free
(`tk_mconst_find`'s own scan) and is what the spec calls "zero lines in teko_const.tk".

**`Color.Red` in expression position** (teko_access.tk's `tk_static_member`, the same
`Type.member` road a class const reaches) is the qualified-const lookup UNCHANGED, plus one
line: the folded `N_INT` node is retagged `set_nd_type(n, sr_ty_at(si))` when the row is an
enum — the one difference between an enum member and a plain `i64` constant. A member not
found this way is refused `teko: <Name> has no member <Member>` before falling through to
the field/property/method dispatch a class const would take next, since an enum declares
none of those.

**The two scalar-compat clauses** (teko_typeof.tk's `tk_check_scalar_compat`, D33/D34's own
function): the existing D34 clause ("a class/struct/enum value reaching a numeric slot
refuses") already covers enum→int/f64 for free, because an enum IS a struct-table row; the
NEW clause is the reverse — a target that IS an enum row accepts nothing but its own type,
so a bare integer (the literal `0` included, since it carries no different node shape from
any other `i64` value), a different enum, and `null` all refuse into an enum slot. Both
explicit conversions (`(i64) c`, `(Color) n`) go through `tk_cast`/`MTASK_CAST` untouched, a
plain machine cast neither function is ever asked about.

**The operators** (teko_ops.tk) gain one EARLY branch in `tk_ops_binary`/`tk_ops_unary`,
ahead of the existing class-operator resolution: when either operand's row is `TK_KENUM`
(checked before the generic `tk_op_declared`/"declares no operator" path, which would
otherwise misfire — an enum declares no `operator` method at all, so the unmodified path
would refuse every legal comparison too), the six comparisons and `& | ^` on the SAME enum,
plus unary `~`, are left to the core untouched (the width/kind registration already makes
the core's own instruction correct); everything else is refused `teko: no operator \`X\`
takes these operands`, the exact class-operator message, reused rather than duplicated. An
operand the oracle could not type at all (an array element, `teko_array.tk`'s own `N_INDEX`
gap) is left alone rather than refused, the same "refuse only what is SURE wrong" rule
`tk_check_scalar_compat` already states for `ety < 0`.

**`switch` needed one line, not zero.** The spec's own claim — "zero lines in
teko_switch.tk, a qualified constant already resolves as a case label" — holds for the
CASE LABELS (`Color.Red` folds to an `N_INT` regardless of its retagged type, and
`tk_switch_label_val` only reads `nd_val`) but NOT for the switch's own hidden local: `i64
$t = x;` was hardcoded to `i64` regardless of the subject's real type, harmless while the
only switchable type was a plain integer, and wrong the moment `$t`'s declared type
disagrees with an enum-typed case label under `teko_ops.tk`'s new guard above (`$t == Color.
Red` reads as "an i64 compared against a Color", the SAME shape the guard correctly refuses
when a program writes it by hand). The fix is `tk_switch_stmt`'s own one line: `$t`'s
declared type is the parse-time oracle's best guess at the subject's (`tk_pty_of`,
teko_struct.tk), falling back to `i64` exactly where the old hardcoded value was — which is
every existing fixture's own switch, none of which switches on a DECLARED LOCAL of a
non-`i64` type (`--dump-ast` of the 46 fixtures already in the tree proves this: byte-
identical). The parser's own oracle does not see a PARAMETER's type (`tk_slv`/`tk_local`
track a declared local's `N_VAR` only, teko_struct.tk's own header says so for the
identical reason K4's `use (a, &b)` cannot see one either), so `switch` directly on an enum
PARAMETER is not yet taught — assign it to a local first. Recorded in
[not-yet.md](docs/reference/not-yet.md), not worked around.

**A fixed array of an enum needed one exemption, in `teko_array.tk`.** The existing refusal
("an array of objects is not taught yet") reads `tk_struct_by_ty(ety) >= 0`, true for ANY
struct-table row, enum included, though the reason the module states for the refusal — "the
element would be an object slot with no local name for `teko_rc.tk`'s pass to walk" — does
not hold for an enum, which owns no object and is never counted (`tk_is_counted` answers
"no" for a `TK_KENUM` row by construction, the same clause this crumb's own header leans
on). `tk_arr_on_stmt`/`tk_garr_collect` gain one `&& !tk_is_enum(esi)` each; a struct or a
class array element is still refused exactly as before. This module is not in the spec's
own § 9 table for N2a; the fixture's own "enum in array" requirement is what surfaced the
gap, and the fix is narrow enough (a value type with no counting obligation, moved by width
alone, the same as any core integer array already is) to belong with this crumb rather than
wait for a later one.

**Two pre-existing parser ambiguities, neither new to `enum` and neither this crumb's to
fix (D2):** `(i64) p.w` (a cast binds tighter than `.`, already in not-yet.md before this
crumb) and, newly documented here, a unary prefix (`!`, `-`, `~`) directly in front of a
parenthesized expression that itself opens with a qualified constant (`!(Color.Red <
Color.Green)`, reproduced identically with a plain class const, `!(Shape.MAX < 10)`): the
core's cast-detection right after a unary prefix does not backtrack past a type word
followed by `.`, and answers `expected ) in cast`. The fixture and the doc samples work
around both by binding the qualified constant to a local first.

`tests/surface_enum.tk` (`expect-exit: 42`): implicit and explicit member values, the
default underlying type and two explicit ones (`: u8`, and a SIGNED `: i16` round-tripping a
negative value through a class field's own indirect load, N0/D38's `tk_ld`), the six
comparisons, `| & ^` plus unary `~` (a double negation), both explicit casts, an enum
global/field/array element/parameter/return, a duplicate value used as an alias, and a
`switch` with a fallthrough pair of case labels and a `default`. `docs/reference/enums.md`
is folded into [types.md](docs/reference/types.md) (a `## enum` section, a compiled sample
of its own) rather than a page of its own, since the type is now part of "what runs today"
and not a spec; [diagnostics.md](docs/reference/diagnostics.md) carries the five new
`teko:` strings plus the two existing ones this crumb reuses verbatim (`no operator`, `a
value of type`); [not-yet.md](docs/reference/not-yet.md) carries N2b/N2c, `[Flags]`, the
bare member name in a `case`, and the two parser limitations above.

Proof: `mc build . --config mc.macos.toml` clean; 47/47 fixtures at their `expect-exit` (the
46 existing plus `surface_enum`); `--dump-ast` of the 46 existing fixtures byte-identical to
`214704d6`, compared against a from-scratch build of that commit; probes outside `tests/`
confirming `i64 n = c;`, `Color c = 0;`, `f64 x = c;`, a cross-enum comparison, `c + Color.
Green`, an unknown member, an empty enum, a duplicate member and a bad underlying type each
refuse with the exact message this entry and diagnostics.md carry; `FIXPOINT OK` (`teko2.o
== teko3.o` on the first turn, `--dump-asm` diff empty, 47/47 under the self-hosted
`teko1`); `sh scripts/check-docs.sh` green (402 links, 349 diagnostics, 88 samples); `mc
limits . --config mc.macos.toml` verdict `ok`, `syntax` `14/28` to `15/30` (one word, the
static ceiling growing with the source), `types`/`passes`/`intrin`/`alias` unmoved
(`9`/`15`/`8`/`16`, measured against a from-scratch build of `214704d6`) — exactly as the
spec's own § 10 table predicted, `types` included: nothing is registered by teko itself, so
a program that declares no enum pays nothing (`tests/surface_enum.tk`'s own five type
declarations move that program's `types` row from `9` to `14`, one per declaration, the
same cost a `class` already has). `mc pkg hash .`:
`81c97fa09efe9ca4f128cc95bdeb6108e60508f163ecd229f06587cae55eb78e`.

**A member is a typed value in overload resolution.** `Color.Red` is an `N_INT` retagged
with the enum's type, and `tk_ov_args_fit` ([`teko_over.tk`](teko_over.tk)) read every
non-float `N_INT` as the untyped integer literal — so `f(Color.Red)` against `f(Color)` and
`f(i64)` landed on `f(i64)` and was then refused, and against `f(Color)` and `f(Other)`,
or `f(Color)` and `f(Color, i64)`, matched nothing. The rule, the third exclusion beside the
float literal (its kind) and `null` (`tk_is_null_lit`, D32): an `N_INT` whose `nd_type` is a
row of the type table is a VALUE of that type, judged by the type alone, in every round.
`tk_pm_elem_fits` ([`teko_params.tk`](teko_params.tk)) applies the same rule to an element
of a `params` list, so `params Color[]` takes members and `params i64[]` refuses one. And
`null`, which those two functions let land on any row of the type table (D32), lands on no
enum: an enum is a value whatever row it occupies, so `f(null)` against `f(Color)` finds no
candidate (`teko: no overload of f matches these arguments`) and `total(null)` against
`params Color[]` is refused where it stands (`teko: a value of type uptr does not convert
to Color`), neither accepted and caught later. A
`switch` on an enum PARAMETER stays refused (``teko: no operator `==` takes these
operands``): a parameter has no type at parse time, and giving it one is a crumb of its own
(not-yet.md).

### D40 · `TimeSpan`, and the mechanism that gives a primitive members (2026-09-08)
`docs/specs/datetime.md`'s **P0** (the probes) and **C1**, the fifth crumb of the
numeric-types sequence ([`docs/specs/README.md`](docs/specs/README.md)). Two new modules,
`teko_prim.tk` (the mechanism, 0 registrations of its own) and `teko_time.tk` (the type and
its rows), one new library file `lib/time.tk`, and small additions to five existing modules.
`TimeSpan` is C#'s: a signed 64-bit count of 100-nanosecond ticks, registered
`type_new("TimeSpan", 8, 8, TK_SINT)` and kept OUT of `tk_is_int_ty` (D38's rule: a
`type_new` id joins that predicate only when the module that registered it says so, by
name, there), so it is a type of its own that no integer converts into and that converts
into no number.

**The mechanism is a LOWERING TABLE, not a vtable.** A `type_new` type has no row in
teko_struct.tk's type table, hence no field, no method and no constructor. `teko_prim.tk`
gives it members from data instead: a member row is `(type id, name, kind, parameter type,
symbol, result type)` over five kinds (a static value, a static call, an instance property,
an instance method, the value constructor) and an operator row is `(token, arity, left,
right, symbol, result, swap)`. Every symbol names an ordinary teko function of `lib/time.tk`
written over the RAW ticks — `i64` in, `i64` out — and **the compiler writes the two
casts**: `(i64) t` on the way in, `(TimeSpan) r` on the way out, neither an instruction
(both slots are eight bytes; `walk_narrow`, mc/src/gen_walk.mc, answers 0 for a width-8
`TK_SINT`). That is what keeps `lib/time.tk` from recursing into the operator it implements,
and what keeps every existing check honest: the argument `tk_rc_call_args` (teko_rc.tk) sees
IS an `i64`, and the value the oracle sees IS a `TimeSpan`. Zero new intrinsics, zero new
passes, nothing changed in `mc` (D2, D21).

**Four sites read the table, and all four already existed**: the type word in
expression/statement position (`syntax_expr`/`syntax_stmt` on the word, the same pair a
class name gets, teko_access.tk's `tk_type_word`); `new Name(args)` (teko_expr.tk's
`tk_new`); `.` on a receiver the PARSER can type (teko_expr.tk's `tk_dot`, through
`tk_pty_of`); and `.` on a receiver only the PASS can type (teko_typeof.tk's `tk_pend_do`,
at the exact line `tk_reject_scalar_member` answered before). The operator side is
teko_ops.tk's own pass, claiming ahead of the class-operator path exactly as the enum guard
does (D39).

**The P0 probes, measured before a line of `TimeSpan` was written** (recorded in full in
[`docs/internals/primitives.md`](docs/internals/primitives.md)): the word is a type in
every position with no further code, and `--dump-ast` prints it BY NAME, which is why
registering it moves no existing dump; `syntax_expr` reaches the handler ahead of the core's
"a type word is no expression" (parse_primary consults the table first) and `syntax_stmt`
wins over the core's own declaration path (parse_stmt_core), so the statement handler calls
`parse_var` itself with the type word UNREAD; the conversion clause leaves all 47 fixtures
byte-identical; and — the load-bearing one — **with no operator claim the core compiles and
RUNS raw arithmetic over two values of the new type**: `mk(10) * mk(4)` answered 40. The
spec's § 13 resolution is therefore measured and not precautionary: the pass claims every
binary and unary with a primitive operand and refuses the rows that do not exist.

**Five deltas from the spec's own text**, each a decision this entry takes:

1. **`.Ticks` and `new TimeSpan(t)` are rows with NO symbol**, which the emitter reads as
   the identity cast — the spec § 2 asks for exactly that lowering, and a symbol-less row is
   how the table says it without a special case per member.
2. **The `From*` family is .NET 7's, not .NET Framework's**: the value is scaled at full
   double precision and truncated toward zero, so `FromSeconds(0.0000001)` is one tick.
   The older rounding to the nearest millisecond is the behaviour C# itself dropped, and the
   spec named neither. The domain check is `!(t >= MIN && t <= MAX)`, which is also the NaN
   check: `>=` and `<=` are both false for an unordered compare on BOTH backends, while
   `t != t` answers differently on x86_64 (`comisd`+`setne` reads ZF) than on aarch64.
3. **C#'s five `TicksPer*` statics are taught** (`TimeSpan.TicksPerDay` and its four
   siblings). The spec's § 6 table does not list them and C# has them; D3 decides it.
   `new TimeSpan()` with no argument is C#'s parameterless value constructor and answers
   the zero span, for the same reason.
4. **`tk_ty_binary` (teko_typeof.tk) had to ask the operator table.** The core types a
   binary from its LEFT operand and the oracle copies that rule — but a reversed row makes
   it false: `3 * hour` is a `TimeSpan`, and `(3 * hour).Ticks` is resolved by the oracle
   BEFORE teko_ops.tk has rewritten the node into the call whose declared return type would
   say so. The question is asked only when an operand IS a primitive, so every other binary
   keeps the rule it had.
5. **Text and the hand-written cast refusal are NOT in C1.** `ToString`/`Parse` need the
   text machinery the `enum` page's N2b also waits for; `(i64) t` and `(TimeSpan) n` written
   by hand stay ACCEPTED as the identity they are (the value is exactly what `.Ticks` and
   `new TimeSpan(t)` give), because refusing them means telling a compiler-written cast from
   a source-written one, and that is C2's to build — with `DateTime`, whose `Kind` bits
   above the ticks make the hand-written cast actually wrong. Both are in
   [not-yet.md](docs/reference/not-yet.md).

**Two gaps this crumb's own refusal surfaced, both fixed here rather than deferred.**
A `ref`/`out` parameter's READ is rewritten into `tk_arr_load(pointee, name)` (teko_ref.tk),
a raw `ld64` the oracle cannot type — harmless while every consumer read "not known" as
"refuse nothing", and not harmless for a primitive operand, where "not known" is exactly
what the operator pass must refuse: `t = t + a` inside `f(ref TimeSpan t)` was
``teko: the type of the left side of `+` is not known here``. The load now registers the
pointee's own type (`tk_xt_put`, pure and borrowed, the two answers a field load already
gives), for every pointee alike. And the ternary's hidden local (teko_ternary.tk) is
initialized with a placeholder `0` typed `i64`, which a primitive slot refuses by this
crumb's own clause; the placeholder is now that same zero under the slot's type, the
identity cast again. Neither changes any existing fixture's `--dump-ast`.

**The conversion clause** is `tk_check_scalar_compat`'s (teko_typeof.tk), one `if` beside
D39's enum clause and shared with it in intent: a primitive converts to nothing but itself,
in either direction, in the nine slots D33 enumerated — an integer, a float, `null`, a
class, a struct, a `T[]` or a second primitive into a `TimeSpan` slot, and a `TimeSpan` into
an `i64`, an `f64` or a slot of row type. Every one of them reuses `tk_reject_compat`'s own
wording; the crumb adds five NEW `teko:` strings in all
(`a member of X is read-only`, `this primitive has no constructor`, and the three capacity
messages), plus one composed refusal the spec asks for: **the include is part of the
surface**, so a program that names `TimeSpan` without `#include "time.tk"` is told which
file it forgot (`teko: TimeSpan needs #include "time.tk" before it is used`) instead of
reaching `mc: call to unknown function` two phases later.

**`docs/specs/decimal.md` § 14's proposed decision is NOT taken here.** It is about a
primitive that needs a derived MACHINE table to move sixteen bytes; `TimeSpan` needs no
machine at all, so C1 does not exercise it and it stays unnumbered for C3, which does.

Proof: `mc build . --config mc.macos.toml` clean; **49/49** fixtures at their
`expect-exit` (the 47 existing plus `tests/surface_timespan.tk` at 42 and
`tests/surface_timespan_overflow.tk` at 70); `--dump-ast` of the 47 existing fixtures
byte-identical to `03189d92`; `FIXPOINT OK` (`teko2.o == teko3.o` on the first turn,
`--dump-asm` diff empty over 203801 lines, 49/49 under the self-hosted `teko1`);
`sh scripts/check-docs.sh` green; `mc limits . --config mc.macos.toml` verdict `ok`,
`types` `9` → `10` and `alias` `16` → `17` (a `type_new` moves both, D38's own finding),
`syntax` `15`, `passes` `15/30` and `intrin` `8/16` **unmoved** — the spec's § 10 table
predicted exactly that for the two that must not move. Thirty-two refusal probes and eleven
run-time probes outside `tests/` (`build/refuse/*`, `build/panic/*`, not committed) prove
every message and every panic this entry names, each with its exact text and exit code.

### D41 · `DateTime`, the calendar, and the cast a source may not write (2026-09-08)
`docs/specs/datetime.md`'s **C2**, the sixth crumb of the numeric-types sequence
([`docs/specs/README.md`](docs/specs/README.md)) and the second primitive on D40's lowering
table. No new module and no new pass: `teko_time.tk` grows the registrations,
`teko_prim.tk` the three mechanism additions below, `lib/time.tk` the calendar, and
`teko_array.tk`/`teko_ops.tk`/`teko_typeof.tk` one line each.

**`DateTime` is C#'s, bits included.** `type_new("DateTime", 8, 8, TK_SINT)`, the ticks
since `0001-01-01 00:00:00` in bits 0..61 and the `Kind` in bits 62..63 — C#'s own
`dateData` packing, which the maximum tick value (`3155378975999999999`, 62 bits) leaves
room for. It is kept OUT of `tk_is_int_ty` like `TimeSpan`, so it converts to nothing and
nothing converts to it. **The raw value is not the tick count**, and that one fact decides
the rest of this entry: a `Local` date is a NEGATIVE `i64`, so `.Ticks` is a call (where
`TimeSpan.Ticks` is the identity cast), every comparison is a call that masks the two bits
off, and the hand-written cast C1 left accepted had to be refused here. `DateTimeKind` is
`type_alias("DateTimeKind", ty_i32)` plus a three-value handler, the spec's own § 1 shape;
now that `enum` is taught (D39), turning it into one is N2c and a pure tightening.

**The calendar is the proleptic Gregorian one, computed as C# computes it**, in
`lib/time.tk` and in nothing else: the 400/100/4-year walk of `GetDatePart` with C#'s two
`== 4` corrections, and the cumulative month table written as a FUNCTION of ifs rather than
as the `const i64` array the spec's § 6 names — teko has no `const` array (`const i64 M[] =
{...}` is `expected = after const`), and a mutable global table in a library is state a
program can clobber. Every field is checked where a date is built, and a date that does not
exist panics (`teko: a date does not exist`, exit 70): `new DateTime(2023, 2, 29)` is the
fixture.

**Six decisions the spec did not take, each C#'s own answer where C# has one:**

1. **`AddDays`/`AddHours`/`AddMinutes`/`AddSeconds`/`AddMilliseconds` take an `f64` and
   round to the nearest MILLISECOND**, which is C#'s `DateTime.Add(double, int)` to this
   day — deliberately unlike `TimeSpan.From*`, which .NET 7 changed to full precision and
   D40 followed. Two neighbouring functions with two roundings is C#'s own shape, not an
   inconsistency this port invented. `AddTicks`, `AddMonths` and `AddYears` take whole
   counts, and `AddMonths` clamps the day to the length of the target month
   (`2024-01-31 + 1 month` is `2024-02-29`), with C#'s own ±120000 and ±10000 bounds.
2. **`DateTime.UnixEpoch` carries `Kind` `Utc`**, as C# declares it; `.Ticks` still answers
   `621355968000000000` because it masks. The arithmetic KEEPS the `Kind` it started from
   and the comparisons IGNORE it, which is C#'s `Compare`.
3. **`.Kind` answers `ty_i32`**, the id the alias resolves to, and the three constants are
   ordinary calls (`tk_dtk_utc()` and its two siblings) rather than folded literals — D40's
   own rule that every symbol in the surface has a body.
4. **`DateTime.Now`, `UtcNow` and `Today` are refused BY NAME** through a new row kind,
   `TK_PMSOON`: `teko: DateTime.Now is not taught yet`. The wall clock is `mc`'s to give
   (§ 8/§ 14) and the ask stands; a row is what keeps the site from reading as an unknown
   member.
5. **`SpecifyKind` and `Subtract` are NOT taught** ([not-yet.md](docs/reference/not-yet.md)).
   Both need a row whose parameters differ from each other in TYPE — a `DateTime` beside a
   `DateTimeKind`, two overloads of one arity — and the row carries one parameter type and a
   count (below). `SpecifyKind(d, k)` is `new DateTime(d.Ticks, k)` and `Subtract` is `-`.
6. **The fixtures are two**, `tests/surface_datetime.tk` (42, 106 assertions) and
   `tests/surface_datetime_panic.tk` (70), named for the `surface_*` family the repository
   uses, rather than § 11's five older names.

**Three additions to the mechanism, all in `teko_prim.tk`:**

- **A member row's parameter list is a COUNT and ONE type.** `new DateTime(y, m, d)` is
  three integers, `TimeSpan.FromHours(x)` one float; no member this table carries mixes
  parameter types, and the day one does, that column becomes a list and nothing else moves.
  **Rows of one name and different counts are the overload set of that name** —
  `new DateTime(...)` is five rows and the site picks by how many arguments it wrote.
- **`TK_PMSOON`**, decision 4 above.
- **The list that tells a compiler-written cast from a hand-written one.** `(i64) dt` and
  `(DateTime) n` are refused in the surface (§ 3) — the first answers the raw bits, `Kind`
  included, and the second is an integer that skipped every range check — and the compiler
  writes those very two nodes in every lowering. A MARK on the node was not available:
  `--dump-ast` prints every field a node has, so marking would move the dump of code that
  did not change. So `tk_cast` (teko_array.tk) records the casts it builds TOWARD a
  primitive, `tk_prim_raw` the ones it builds AWAY from one, and `tk_prim_cast_check`
  refuses every other cast touching a primitive. Two subtleties are load-bearing: a node
  replaced in place (`node_assign`) keeps the PLACEHOLDER's index, so the deferred `.`
  (teko_typeof.tk) and the operator rewrite (teko_ops.tk) hand the record over
  (`tk_prim_own_cast_moved`); and the check rides the operator pass's walk, which covers
  function bodies only, so a second small walk covers a global's own initializer. The
  message is composed from the type name — ``teko: a DateTime does not cast; `.Ticks` reads
  it and `new DateTime(...)` builds it`` — because the refusal belongs to every primitive
  and not to dates alone; the spec's § 3 wording was `DateTime`-specific. The ceiling is
  `TK_MAXPRIMC` 4096 casts per unit, the mechanism's fourth capacity message.

**What C1 listed as accepted and is now refused:** `(i64) t` and `(TimeSpan) n` written by
hand. That row of [not-yet.md](docs/reference/not-yet.md) is gone, which was C1's own plan
(D40 § 5) and the reason it waited for the type whose bits make the cast wrong.

Proof: `mc build . --config mc.macos.toml` clean on mc **0.15.23** (`MC_VERSION`);
**51/51** fixtures at their `expect-exit` (the 49 existing plus the two new ones);
`--dump-ast` of the 47 fixtures that do not include `lib/time.tk` byte-identical to
`c13d7d84`, and the two that DO include it different only by INSERTION — the new library
functions, zero deleted and zero changed lines, which is the file growing and not the
accepted code moving; `FIXPOINT OK` (`teko1.o == teko2.o` on the first turn, `--dump-asm`
diff empty over 205653 lines, 51/51 under the self-hosted `teko1`);
`sh scripts/check-docs.sh` green (466 links, 356 diagnostics, 95 samples) — a first run
reported `docs/reference/types.md` line 88 failing, which was the unpinned `mc` on the PATH
(0.15.18, the `fa_cast` D37 fixed) building the sample; with the pinned 0.15.23 the sample
passes on `c13d7d84` and on this branch alike; `mc limits . --config
mc.macos.toml` verdict `ok`, `types` `10` → `11` and `alias` `17` → `19` (the `type_new`
plus `DateTimeKind`'s `type_alias`), `syntax` `15`, `passes` `15/30` and `intrin` `8/16`
**unmoved**. Thirty-two refusal probes outside `tests/` (`build/refuse/*`, not committed)
prove every message this entry names with its exact text and exit code.

### D42 · Two pre-existing defects: capturing an 8-byte primitive, and a struct with no `new` (2026-09-08)
Two verifier findings, fixed in one crumb because both are the SAME class of bug —
compiler-generated code reading or writing a value at a width the surface's own
compatibility check was never told about.

**Defect 1 — `use (a)` of a `TimeSpan`/`DateTime`/`enum` refused the value it had just
read.** A lambda's by-value capture writes into the closure through `tk_cap_put(p, off,
i64 v)` (`lib/rt.tk`), the ONE writer every non-float, non-counted capture shares
(`tk_cap_writer`, teko_deleg.tk) — correct for `i64`/`i8`/`u8` (already `tk_is_int_ty`,
D38), and wrong for the three `TK_SINT` ids that are NOT ordinary integers by that same
rule (D38's own list: `TimeSpan`/`DateTime`, and every `enum`). The captured NAME crossed
into the argument list untouched, `tk_ty_of` answered its own row type, and
`tk_check_scalar_compat` (D34/D40's own clause) refused it on sight — `teko: a value of
type TimeSpan does not convert to i64` — reported at the CAPTURED LOCAL's own declaration
line rather than the `use (...)` site, because `tk_call3` (teko_struct.tk) stamps a
synthetic node with `tk_line`/`tk_file` as they stand at that instant, and
`tk_deleg_var_stmt` (teko_deleg.tk) only restores them to the DECLARATION's own line AFTER
parsing the whole initializer — the misleading line was a symptom, not the defect.

The read side (`tk_lambda_prologue`'s own `tk_ld`) was never the problem: an eight-byte
`TK_SINT` load returns the raw `ld64` call unwrapped (D38's own rule, `type_width(ty) < 8`
being false for these three), and that raw call's `TY_I64` tag is read by nothing —
`tk_ty_of` types an `N_CALL` by `decl_find`, which does not know a fixed core intrinsic
like `ld64` at all, so it answers `-1` and every compat check downstream reads that as
"not known" and steps aside. Only the WRITE side, where the compiler hands the CAPTURED
NAME (typed by the ordinary scope table, not a raw call) to `tk_cap_put`'s `i64` parameter,
ever reached a check that had an opinion.

The fix is `tk_cap_val` (teko_deleg.tk): the value a by-value capture hands to
`tk_cap_writer`'s own slot, computed once and shared by both writers (`tk_cap_put`,
`tk_cap_own`'s float/counted siblings are untouched). A primitive-with-members goes through
`tk_prim_raw` (teko_prim.tk) — the SAME compiler-authored raw view a row's own argument
already takes (D40 § 2/D41 § 3), so the cast is remembered on the "compiler wrote this"
list and the later refusal (`tk_prim_cast_check`) never mistakes it for a hand-written one.
An `enum` takes a plain `tk_cast(TY_I64, v)` — `docs/specs/enum.md` § 5 states both explicit
directions of an enum cast are "a machine cast and nothing more", so no hand-written-cast
refusal exists to sidestep. `tk_prim_is`/`tk_prim_raw` (teko_prim.tk, included AFTER
teko_deleg.tk) are forward-declared at the call site, the same way `tk_lambda_build` already
is at the top of the same file. Zero new intrinsics, zero changes to `mc` (D2/D21): the fix
is the same cast the surface already writes elsewhere, built where one site did not write
it. By-reference captures (`use (&a)`) were never affected — they hand `tk_addr(...)`, an
address, to `tk_cap_put`'s OWN `ref` writer, not through `tk_cap_val` at all.

**Defect 2 — `struct P p;` with no initializer crashed the run.** A struct value IS a
pointer to an allocation ([types.md](docs/reference/types.md) § struct), produced by `new`
alone, and a struct is NOT reference-counted, so a bare local of struct type never receives
the zero-to-`null` a counted local gets (`tk_rc_var`, K2b): `p` reaches the run holding
whatever the stack held, and `p.x = 4` writes through it — a `SIGBUS`/`SIGSEGV`, never a
`teko:` line. A bare CLASS local is the same one level down (a deterministic null
dereference, no guard on any field access), and a global struct with no initializer the
same one level up (BSS zeroes it to `null`, unguarded).

**The owner's ruling: this is the developer's error, documented, not the language's.** A
struct is not a primitive with a default value; a declaration without `new` is not
initialized, and the reference page says so in as many words. No compile-time refusal
(declaring first and building in a branch is legitimate code, and deciding that a name is
definitely assigned is the nullable design's job, not a statement hook's), and no runtime
guard (it would sit on the one lowering every field access shares and move `--dump-ast` on
nearly every fixture). What makes the case sound in the end is nullability by declaration:
no type is nullable unless written `T?`, and `T?` is sugar for `Nullable<T>` over ANY type,
reference or value — a spec of its own, ahead of `string`, where the compiler learns that a
name declared without `?` must be built before it is read.

The enum arm of `tk_cap_val` first asked the KIND (`TK_SINT` outside `tk_is_int_ty`), which
covers `enum Color` (`i32` underneath) and misses `enum Level : u8` (`TK_INT`); the verifier
caught it, and the arm now asks the row (`tk_is_enum`), which is what the slot check that
refused the value asks. `tests/surface_enum.tk` captures `Level` and `Signed` as well.

**Proof:** the taught compiler builds on mc 0.15.23; 51/51 fixtures at their `expect-exit`;
`--dump-ast` of the 48 fixtures this crumb does not touch byte-identical to `1d195e1d`, the
three it extends differing by insertion (and the gensym renumbering an insertion causes);
`FIXPOINT OK`; `sh scripts/check-docs.sh` green (466 links, 356 diagnostics, 95 samples);
`mc limits` unmoved (`types` 11, `alias` 19, `syntax` 15, `passes` 15/30, `intrin` 8/16);
`mc pkg hash .` in the pull request.


### D43 · `T?` over a reference, and `null` only in a slot declared `T?` (2026-09-08)
[`docs/specs/nullable.md`](docs/specs/nullable.md) § 19's four proposals, as far as Q0 and
Q1a carry them. Three of the four land here; the fourth (definite assignment) is Q3's and
is untouched.

**1. `T?` is one row of the type table over the type it encloses, and the value is the
handle itself.** `TK_KNULL` is a sixth row kind beside `TK_KSTRUCT`/`TK_KCLASS`/
`TK_KIFACE`/`TK_KDELEG`/`TK_KARRAY`/`TK_KENUM` ([`teko_struct.tk`](teko_struct.tk)), made
lazily by `tk_nl_row` the first time a source spells the suffix — the exact shape
`tk_ha_row` has for `T[]`, down to the registered name being a lexeme the lexer can never
form. For a REFERENCE `T` — a class, an interface, a delegate, a `T[]` of heap, a `struct` —
the handle IS `T`'s own pointer and `null` is `0`, so `Cell?` and `Cell` have the same
representation and the reclaim serves both with no line of its own: `tk_is_counted` answers
for a nullable row whatever it answers for the row it encloses, and `rc_dec(0)`/`rt_own(0)`
were already no-ops over the null handle. `tk_row_fits` gains the implicit `T` → `T?` (and
`D` → `B?`, `C` → `I?`, wherever `D` → `B` already fits) and refuses both `T?` → `T` and one
nullable row into another; `tk_ty_mangle_name` maps the row to `opt_T`, the rule `arr_T`
already lived by. The VALUE arm — the counted box for `i64?`, `f64?`, an `enum?`,
`TimeSpan?` — is Q1b's and is refused by name here:
`teko: a nullable of a value type is not taught yet`.

**2. `null` lands only in a slot declared `T?`, and a comparison against `null` stays legal
anywhere a reference-shaped value does.** The first half supersedes D32's "a reference lands
on any row" for the STORE direction, in the two places that judged it: `tk_ov_args_fit`
([`teko_over.tk`](teko_over.tk)) now lets `null` land on a raw `uptr` or on a nullable
parameter and on nothing else, so `held(null)` against `held(Cell)` and `held(Cell?)` picks
the nullable one instead of being ambiguous; `tk_pm_elem_fits`
([`teko_params.tk`](teko_params.tk)) does the same for an element of a `params T[]`. The
refusal itself is one clause of `tk_check_scalar_compat`
([`teko_typeof.tk`](teko_typeof.tk)) — `teko: null needs a slot declared Cell?` — which
every store slot of the language already reaches, measured slot by slot in
[`docs/internals/nullable-probes.md`](docs/internals/nullable-probes.md) probe 1b. The one
store that does not reach it is an ELEMENT of a `T[]`: `tk_ha_store`
([`teko_heaparr.tk`](teko_heaparr.tk)) judges its value through `tk_check_field_store`
([`teko_struct.tk`](teko_struct.tk)), which returned at once for a `null`, so the rule is
applied in that function's own `null` arm as well. The second half needs no code and is what
keeps § 9's three roads honest: a zeroed field, an element of `new T[n]` and a `struct`
declared without `new` all still deliver a `null` to a non-nullable slot, so refusing
`c == null` would delete the defensive code that catches them and make the run-time guards
unreachable from the surface.

**3. `Nullable<T>` is the compiler's construct, not an instance of the generic mechanism.**
It reads at the type position through a second `syntax_type` handler
([`teko_null.tk`](teko_null.tk)) registered behind the array's, and the two cooperate rather
than race: `take_type` offers a position to the chain ONCE, so `Cell?[]` is read here and
handed to `tk_ha_type`, and `Cell[]?` is read there and handed to `tk_nl_row`. Both were
measured as failures before they were written (probes 1 and 2). `HasValue` is `x != 0`;
`x.Value` is `tk_nl_ck(x)`, the guard, then the same pointer;
`GetValueOrDefault()` is refused on a reference nullable because C#'s answer for it is
`null`, which is the one value a `T` slot may not take.

**Four departures from the spec, each measured rather than argued.**

- **`tk_nl_ck` is GENERATED, not written into `lib/rt.tk`.** Everything in `lib/rt.tk` is
  parsed into every program that includes it, so a function added there moves the
  `--dump-ast` of every fixture — and this crumb's own gate is that a fixture spelling no
  `?` does not move at all. It is emitted once per unit that spells `.Value`, through
  `tk_top_emit`, the discipline `tkarr_release_T` already uses.
- **The refusal for a member of the enclosed type reads `teko: a Cell? is read through
  .Value`, not the spec's `… .Value or ?.`.** `?.` is Q2's and does not exist yet; a refusal
  naming a form the language does not have is a refusal that lies. Q2 widens the wording
  when it lands the operator.
- **`ref T?` and `out T?` are TAUGHT**, where § 11 listed them as not-yet: they were
  measured working at both positions, with an object and with `null`, and with the reclaim
  ending at its floor, so refusing them would be refusing something that works.
  `tests/surface_nullable_ref.tk` carries the oracle.
- **A `null` the COMPILER wrote is not judged by rule 1.** `tk_tern_lower`
  ([`teko_ternary.tk`](teko_ternary.tk)) declares its temporary of reference type with a
  `null` placeholder both branches overwrite before anything reads it. It is registered as
  the compiler's own (`tk_nl_own_null`, [`teko_null.tk`](teko_null.tk)) and skipped by the
  check — the exact standing D41 gives `tk_prim_raw`'s cast against `tk_prim_cast_check`,
  and what keeps `tests/surface_ternary.tk` and `tests/surface_switch.tk` byte-identical.

**Two positions `T?` does not reach, both found by Q0 and neither worked around.** A
nullable as a GENERIC ARGUMENT is refused where it stands
(`teko: a nullable is not a generic argument yet`, one line in `tk_gen_read_targ`): a type
argument travels as the SPELLING of the type, substituted into the template and mangled
into the instance's declaration name, and `Cell?` is not a spelling the lexer can form. A
SHORT type name inside a `namespace`, at a parameter or a return, never reaches the
`syntax_type` chain at all — `tk_ns_param_ty` answers the type without calling `p_type()` —
and that is **pre-existing and shared with `T[]`**: measured on the stock `0a37e491`,
`i64 use(Cell[] cs)` and `Cell[] make()` inside a namespace already answered `name expected`
there, with no `?` anywhere. Both are rows of
[`docs/reference/not-yet.md`](docs/reference/not-yet.md), not patches here, and neither is a
defect on `mc`'s side: the position is teko's own reader.

**The migration is a breaking change, and it is nine fixtures.** `Cell c = null;` compiled
before this entry and does not after. Two shapes, exactly as § 10 planned them: declare the
slot `T?` where the name is only ever compared against `null` (`order_types.tk`,
`surface_delegate.tk`'s `ncheck`, `surface_overload_free.tk`'s `held`), and close a block
around the value's life where the name is read afterwards (`surface_delegate.tk`'s `rcheck`,
`surface_lambda.tk`, `surface_params.tk`, `surface_refout.tk` three times,
`surface_array_heap.tk`, `surface_foreach.tk`) — the reclaim already releases at the `}` and
on the way out of a `return`, so `x = null;` followed by an `rt_live()` assertion becomes a
`}` followed by the same assertion. `surface_panic_null.tk` is the tenth shape and the
interesting one: its `Op f = null; return f(1, 2);` becomes a null arriving through a ZEROED
FIELD, which keeps the exit-70 oracle and documents § 9's first road at the same time. Six
fences of the guide and the reference migrate the same two ways.

**Proof:** the taught compiler builds on mc 0.15.23; 53/53 fixtures at their `expect-exit`
(51 before, plus `surface_nullable_ref.tk` at 42 and `surface_nullable_panic.tk` at 70);
`--dump-ast` of the 42 fixtures this crumb does not touch **byte-identical** to `0a37e491`,
and the nine migrated ones differing only by the type word or the block insertion — that is
the proof the new clauses fire only where a `?` is written; `FIXPOINT OK`;
`sh scripts/check-docs.sh` green; `mc limits` with `types` 11, `alias` 19, `syntax` 15,
`passes` 15/30 and `intrin` 8/16 all **unmoved**, and the single row that moves being
`syntax_type` 1/8 → 2/8, which is what a second registration is; twenty-nine refusal probes
outside `tests/` (`build/probe_*`, not committed) enumerated in the pull request, each with
its message and its exit; `mc pkg hash .` in the pull request.
