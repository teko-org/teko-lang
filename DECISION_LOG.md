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
that and the five other gaps measured here, `f32`→`f64` among them [closed by D78, below].

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


### D44 · `T?` over a value — the counted box (2026-09-08)
[`docs/specs/nullable.md`](docs/specs/nullable.md) § 2's box arm and § 15's Q1b, landed on
top of D43. `T?` is now taught over **every** type teko has: the reference arm is D43's
handle itself, and a VALUE — `i64`, `u64`, `i32`, `i8`, `i16`, `u8`, `u16`, `u32`, `bool`,
`char`, `f64`, `f32`, an `enum` at any underlying width, `TimeSpan` and `DateTime` — is a
handle to an **immutable counted box**. The refusal `teko: a nullable of a value type is
not taught yet` is gone with the crumb that owed it.

**1. One invariant, two storages, and no new rule of lifetime.** The handle stays a
pointer, `null` stays `0` and `HasValue` stays `handle != 0`; what a value handle points at
is an object with the very shape a `T[]` of heap already has ([`teko_heaparr.tk`](teko_heaparr.tk)):
vtable at `+0`, count at `+8`, the payload WIDTH at `+16` and the payload itself at `+24`.
`tk_is_counted` ([`teko_struct.tk`](teko_struct.tk)) answers 1 for the box arm — one clause,
beside the one D43 wrote — and that single answer is the whole reclaim: a `T?` field, a
`T?` element of an `i64?[]`, a `T?` argument (borrowed and parked), a `T?` return (owned),
a `T?` temporary and a `T?` captured by a closure are the counted positions
[`teko_rc.tk`](teko_rc.tk) and [`teko_deleg.tk`](teko_deleg.tk) already knew. **One release
serves every box there is** — a box holds no reference, because a counted `T` is already a
pointer and never reaches this arm — so it is `rt_free(p, 24 + ld64(p + 16))` and nothing
else.

**2. Value semantics come from immutability, not from a copy rule.** Every store into a
`T?` slot builds a FRESH box (`tk_nl_wrap`, [`teko_null.tk`](teko_null.tk)), so `b = a;
b = 7;` leaves `a` at its own value, and no site can reach a payload's address:
`.Value` is not a slot. The implicit `T` → `T?` is written at the nine slots D33 enumerated,
each one line beside the `tk_num_widen` call already there — an initializer, an assignment
and a `return` ([`teko_rc.tk`](teko_rc.tk)); an argument of a free/method call (same file),
of a virtual call ([`teko_expr.tk`](teko_expr.tk)) and of an interface call
([`teko_iface.tk`](teko_iface.tk)); an element of a `params T[]`
([`teko_params.tk`](teko_params.tk)); a field store ([`teko_typeof.tk`](teko_typeof.tk));
and an element of a `T[]` ([`teko_heaparr.tk`](teko_heaparr.tk)) — and the widening runs
FIRST, so `f64? x = 5;` boxes one point zero.

**3. The honest cost is measured, not argued.** `i64? x = 5;` ALLOCATES and `rt_live()`
counts it, where C#'s `int?` costs nothing. `tests/surface_nullable_value.tk` asserts the
number at every shape and asserts that `rt_peak()` does not move across ten thousand
iterations of a loop that churns a box: a 32-byte box comes straight back to the 32-byte
free list, which is Q0's probe 8 holding in a program.

**Three departures from the spec, each measured rather than argued.**

- **The box's runtime is GENERATED, not written into `lib/rt.tk`**, where § 2 put it. It is
  the same departure D43 made for `tk_nl_ck` and for the same reason: everything in
  `lib/rt.tk` is parsed into every program that includes it, so a function added there moves
  the `--dump-ast` of every fixture, and this crumb's own gate is that a fixture spelling no
  value `?` does not move at all. `tk_nl_vt`, `tk_nl_release`, `tk_nl_new` and `tk_nl_dflt`
  go through `tk_top_emit`, once per unit that needs each.
- **The payload WRITER is one function per payload type**, where § 2 promised no per-type
  code at all. It is three lines — `T? tk_nl_box_T(T v) { uptr p = tk_nl_new(W); stW(p + 24,
  v); return p; }` — and it is per type for the exact reason `tkarr_put_T` is per element
  type: `v` has to be DECLARED at that type, because a float travels in the machine's other
  register file and an `i64` parameter never receives it, and because an `enum` or a
  primitive with members reaching a numeric slot is refused by D34/D40's own clause. The
  RELEASE is still one function for every box, which is what § 2's property was about.
- **`GetValueOrDefault()` is a typed load through one address, not a ternary.** § 6 wrote it
  as `x == 0 ? <zero of T> : x.Value`; what runs is `tk_ld(T, tk_nl_dflt(x))`, where
  `tk_nl_dflt` answers the box's own payload address or the address of eight zero bytes.
  `default(T)` is all-zero bits for every value teko has — `0`, `0.0`, `false`, `'\0'`, the
  zero member of an `enum`, a `TimeSpan` of no ticks — so it evaluates the nullable exactly
  once, needs no branch in the tree and needs no per-type code either.

**What the value arm refuses, and where.** The scalar half of the compatibility check
(`tk_check_scalar_compat`, [`teko_typeof.tk`](teko_typeof.tk)) hands a boxed slot to
`tk_nl_check_value`, which judges the value against what the nullable ENCLOSES and reports
under the NULLABLE's own name — the slot the source actually wrote: `i64? n = 2.5;` is
`teko: a value of type f64 does not convert to i64?`, `Color? c = 5;` is
`teko: a value of type i64 does not convert to Color?`, and a class in an `i64?` slot is
refused by the same wording. `tk_row_fits` gains the guard that no row fits a box arm, and
`i64 j = n;` stays D43's own `teko: a value of type i64? does not convert to i64`. The
overload rounds need no line: an integer literal lands on `i64` and picks `pick(i64)` over
`pick(i64?)` (C#'s own preference), a value of the nullable row picks `pick(i64?)`, and
`null` picks it too by D43's clause.

**`mc limits` is unmoved, and the row ceiling was measured rather than raised.** `types` 11,
`alias` 19, `syntax` 15, `passes` 15/30, `intrin` 8/16, `syntax_type` 2/8 — the compiler
registers no new primitive, no new pass and no new intrinsic, and `mc limits . --config`
answers `ok`. In a COMPILED program a `T?` still costs one row per distinct `T`:
`tests/surface_nullable_value.tk`, the heaviest fixture this repository has for the
construct, uses **24 of `TK_MAXSTRUCT`'s 32** (measured by adding dummy classes until
`teko: too many type declarations` fires: 8 more are accepted, 9 are not), so the ceiling
stands where D43 left it.

**Proof:** the taught compiler builds on mc 0.15.23; 55/55 fixtures at their `expect-exit`
(53 before, plus `surface_nullable_value.tk` at 42 and `surface_nullable_value_panic.tk` at
70); `--dump-ast` of all **53 pre-existing fixtures byte-identical** to `dc6d0ce9` — this
crumb touches no accepted program that does not spell a value `?`; `FIXPOINT OK`;
`sh scripts/check-docs.sh` green; `mc limits` unmoved as above; refusal probes outside
`tests/` (`build/probe_nl_*`, not committed) enumerated in the pull request, each with its
message; `mc pkg hash .` in the pull request.

**The fix a verifier's review found: `i64? a = 5; a == 5` compiled and ran always false.**
`tk_ops_binary` ([`teko_ops.tk`](teko_ops.tk)) claims a binary only when at least one side
is a ROW of the type table (`tk_op_row(ta) >= 0`); when the operator is not declared by
either side, it falls through to the core's own raw arithmetic whenever the OTHER side is
of a core type — the clause that is right for a bare reference (`v + zero` really is one
pointer word plus zero) and wrong for a nullable, where the handle is never the value.
`i64? a`'s own type IS a row (`tk_nl_row`'s own `TK_KNULL` one), so `a == 5` reached that
same fallback: `5` is a core `i64`, its own row lookup answers `-1`, and the site fell
through unclaimed — the handle compared against five, byte for byte, always false. `Cell?
== Cell` and `i64? == i64?` never hit the fallback (both operands are rows), which is why
those two were already correctly refused and the gap went unnoticed until `T?` met a plain
core-typed operand. **The rule this fix enforces:** a nullable operand — reference or value,
found by `tk_is_nl` on either side's row — claims the binary UNCONDITIONALLY, ahead of that
fallback, and takes `==`/`!=` against `null` (`tk_is_null_lit`, either side) and nothing
else; every other operator, and `==`/`!=` against anything but `null`, is refused by the
existing `tk_op_none_msg` wording. The same claim reaches unary `- ! ~ +`, and a new one at
`N_IF` (the ternary's own node by the time this pass runs) refuses a nullable used bare as a
condition — `teko: i64? is not a condition`, named by the row (a `bool?` prints as `u8?`,
`bool` being an alias of `u8`); a `while`/`for`/`do` guard is `!(cond)` and meets the unary
refusal first, ``declares no operator `!` `` — since a boxed value's handle answers
`HasValue`, not the value inside it, and `false` is still a live box. `HasValue`'s own
lowering (`left != 0`) would have been caught by the new claim as well — `left` IS a
nullable operand — so it is MARKED (`tk_nl_hv_mark`, on the RECEIVER node rather than the
binary: `tk_nl_pend`'s own return value is copied into a deferred placeholder by
`node_assign`, which keeps a child's id and drops the parent's, so marking the child is what
survives that copy) instead of built any differently, which is what keeps `--dump-ast`
byte-identical on the fixtures that already spell `.HasValue`. **Proof of the fix:** 55/55
fixtures unchanged at their `expect-exit`; `--dump-ast` of the 53 pre-existing fixtures still
byte-identical to `dc6d0ce9`; `FIXPOINT OK`; `sh scripts/check-docs.sh` green; a probe matrix
outside `tests/` covering every operator (`+ - * / % < <= > >= & | ^ << >>` and unary
`- ! ~ +`) over `i64?`/`f64?`/`Cell?`, `T? == null`/`!= null` on both sides, two nullables
compared, `HasValue` as a bare condition (both a local and a PARAMETER, the two lowering
paths), and `bool?`/`Cell?` bare in `if`/the ternary/`while` — each refusing or compiling as
the rule states.

### D45 · `??` and `?.` — the two operators of `T?` (2026-09-08)
[`docs/specs/nullable.md`](docs/specs/nullable.md) § 5's operators and § 15's Q2, on top of
D43 and D44. `a ?? b` and `a?.m` / `a?.m(x)` are taught with C#'s own rules: the left side
is evaluated exactly once, the right side only when the handle is 0, and everything behind
a `?.` — the member, and the arguments of a call — runs only when the receiver has a value.

**1. Both are rewritten inside the ternary's own walk, and register no pass.** § 5 promised
"a pass that runs immediately ahead of `tk_ternary_pass`" and § 13 budgeted `passes` +1;
what runs is the same rewrite at the same instant, called from `tk_tern_scan`
([`teko_ternary.tk`](teko_ternary.tk)) the moment it reaches one of the two placeholders the
parser left. The walk is the thing worth having — it already hoists into the enclosing
statement list, fences a lone `if`/`return` branch into a block of its own, re-hoists into a
loop's body so a condition is read again every turn, and reduces the inside out so a nested
operator is lowered first — and reaching it from inside costs one `else if` and no pass at
all. The two lowerings live in [`teko_null.tk`](teko_null.tk); `tk_tern_zero` is the one
shape they share with the ternary, extracted from `tk_tern_lower` with the node it builds
unchanged.

**2. The rewrite is an `if`, not a `tk_ternary` placeholder.** § 5 wrote it as
`tk_ternary($t != 0, $t.Value, b)`. It cannot be: the ternary types its two arms against
EACH OTHER, and which arm `??` needs — `$t` itself for a right side of the same `T?` row,
`$t.Value` for a right side of type `T` — depends on the right side's own type, which is
known only after the right side has itself been reduced. So each lowering builds the `if`
directly, with the same three pieces `tk_tern_lower` builds it from (a hidden local for the
value, a hidden local for the result, and an assignment inside each branch), and the
laziness is the same laziness: what an arm hoisted goes into that arm's own list.

**3. The result type, spelled out.** `a ?? b` where `b` is the SAME `T?` row answers `T?`
and unwraps nothing; where `b` is a value that fits `T` — the enclosed type itself, a
derived class, an integer into a float — it answers `T`, through `.Value`, which for a boxed
value is the payload read and for a reference is the same pointer; `a ?? null` keeps the
row. Anything else is refused by the wording every mismatched value already gets, including
a nullable of another row on the right: there is no covariance between nullable rows here
either. `a?.m` answers `M?` — a value member boxed, a reference member as the nullable of
its own type, and a member already declared `U?` keeping its row, which is what makes
`a?.b?.c` chain — and `void` is refused, `teko: ?. needs a value`.

**4. The member behind a `?.` is resolved by the road every deferred `.` already takes.**
The lowering binds `T $v = $t.Value;` inside the taken branch and hands `$v`, the member's
name and the form the parser read to `tk_pend_add`/`tk_pend_do`
([`teko_typeof.tk`](teko_typeof.tk)) — the same pair that answers a `.` on a receiver only
the oracle can type. A field, a property, a virtual call, an interface call and a
primitive's own member all come back without a line of their own here, and the local is what
makes the virtual case work at all: that pass refuses a virtual call whose left side is not
a name or a field.

**5. A plain `.` on either operator's result is refused.** `.` and `?.` share precedence 12,
so `a?.b.c` would read as `(a?.b).c` where C# short-circuits the whole chain — and the
placeholder has no type until the rewrite, so the `.` would resolve by member name alone.
`teko: bind the ?? or ?. result to a variable before reading a member`; `a?.b?.c` is the
form. `??=` is refused by name (`teko: ??= is not taught`), since `??=` is no lexeme of its
own and `a = a ?? b;` is the form.

**6. The precedence divergence is real and is recorded rather than fixed.** `??` sits at 1,
tied with `||` and the ternary, because `mc`'s Pratt table starts there and `syntax_infix`
refuses anything outside 1..100; renumbering it is `mc`'s base grammar (D3). So
`a || b ?? c` reads as `(a || b) ?? c` where C# reads `a || (b ?? c)`, and what a program
that writes it actually gets is `teko: ?? needs a nullable on the left` — `||` answers a
truth value. It is a row of [`docs/reference/not-yet.md`](docs/reference/not-yet.md), and
right-associativity (`a ?? b ?? c` is `a ?? (b ?? c)`) is what the same floor buys.

**7. One pre-existing defect fixed on the way, because Q2 walks into it.** A ternary — and
now a `??` — whose type is an `enum` was refused with `teko: a value of type uptr does not
convert to Color`, for a `null` no source wrote: the hidden temporary is declared with a
placeholder value, and an enum row is a row of the struct table, so it got the compiler's
own `null` (D43's exemption covers the row check, not the enum clause, and an enum converts
from nothing but itself). `tk_tern_zero` now gives an enum slot the zero of its OWN type —
the very node `Color.Red` is, an `N_INT` retagged — so `Color j = c ? Color.Red :
Color.Blue;` and `Color j = k ?? Color.Blue;` both compile.

**8. `mc limits`, measured, and the spec's own row corrected.** § 13 wrote the cost as
`syntax` +2. The table that `syntax_infix` moves is **`infix`**, 22 → 24 of 64 reserved;
`syntax` is unmoved at 15, and so are `passes` 15/30, `intrin` 8/16, `syntax_type` 2/8,
`alias` 19 and `types` 11. No new intrinsic, no new pass, no change to `mc` (D2/D21).

**Proof.** 56/56 fixtures at their `expect-exit`, `tests/surface_nullable_ops.tk` (the new
one) at 42 with a counter proving both evaluation rules and `rt_live()` back to its floor
after every shape; `--dump-ast` of the 55 pre-existing fixtures byte-identical to
`726a0724`; `FIXPOINT OK`; `sh scripts/check-docs.sh` green, the reference page's own sample
built and run; and a probe matrix outside `tests/` covering `??` with a non-nullable left,
with incompatible operands and with a foreign nullable row, `?.` on a non-nullable, on a
`void` method, as a slot and with an index, `??=`, `a?[i]`, `a || b ?? c` and a plain `.` on
either result — each refusing with the message documented in
[`docs/reference/diagnostics.md`](docs/reference/diagnostics.md).

### D46 · Definite assignment — a local read before it is assigned is refused (2026-09-08)
[`docs/specs/nullable.md`](docs/specs/nullable.md) § 8 and § 15's Q3, the fourth of § 19's
proposals and the half D42 deferred. **A local declared without `?` and without an
initializer, read with no assignment to it anywhere earlier in source order, is refused
where it is read**: `teko: <name> is used before it is assigned`.

**1. It over-approximates "assigned", on purpose, and that is the whole design.** A name
counts as assigned from the moment an assignment to it appears earlier in the body, in
whatever block that assignment sits and whether or not that block runs — an `if` arm, an
`else`, a `loop` body, a `switch` case, a nested block. So the rule can never refuse a
correct program, which is the property that lets it land at all: D42's own example of
legitimate code is *declare first, build in a branch*, and a strong analysis would need a
dominator tree nobody has asked for. What it catches is exactly the class D42 named — a
`struct`, a class or a scalar local declared and then read with **nothing** ever written to
it, which until now reached the run as whatever the stack held. `struct P p; p.x = 4;` is
refused at the store, because that store READS `p` to reach the field.

**2. Four "counts as an assignment" cases, three of them free.** By the time the analysis
runs, an initializer, a later `x = e`, a `foreach` variable and a `for` initialiser are all
either an `N_VAR` carrying a value or an `N_ASSIGN`, and `f(out x)` / `f(ref x)` /
`use (&x)` are all the `N_ADDR` `tk_ref_addr` builds. The fourth — a by-value `use (x)` —
is an ordinary `N_IDENT` and needed a row: `tk_cap_val` ([teko_deleg.tk](teko_deleg.tk))
records the node it reads the local through, and the walk counts that one node as an
assignment instead of a read. § 8's table asks for it in as many words ("refusing a capture
is not this crumb's business"), and the alternative — matching the generated
`tk_cap_put(...)` call by NAME — is the kind of shape-matching that breaks the day the
writer changes.

**3. Three declarations are born assigned.** One with an initializer; a local array
(`i64 a[4]` — `nd_val` is the length and the name IS the storage, the same clause
`tk_ty_scope_var` already reads); and a `T?`, because `T? x;` is `T? x = null;` and the `?`
is the licence to hold nothing (§ 19.2, D43). A parameter, a field, a global and an element
of `new T[n]` are not judged at all: § 8's own "out of scope, and stated so".

**4. `a = a + 1;` on an unassigned `a` is refused**, C#'s own answer. The bit is set AFTER
the value is walked, not before, so an assignment does not vouch for its own right-hand
side. That is not an over-approximation lost: a read of a slot nothing ever wrote is never
correct.

**5. Zero new pass, and it rides teko_typeof.tk's walk of its own.** § 12 promised the
oracle's own visitor and Q0's probe 9 said the walk could see an assignment before the read
it judges. What landed is a walk **beside** it, in the same file and inside the same
registered `pass()`: a pre-order visitor cannot mark an assignment after its value, and it
cannot tell an `N_ASSIGN`'s target — which is not a read — from an `N_IDENT` that is. So
`tk_da_walk` is its own recursion, run from `tk_typeof_pass` after the deferred `.` has been
rewritten. Two consequences, both wanted: the tree is at its closest to the source (`p.x = 4`
is already the `st64(p + off, 4)` whose `p` is the read to judge), and none of the reads
`tk_rc_pass` INJECTS later — the `rc_dec(x)` of a block's releases — exists to be mistaken
for one the source wrote. `tk_typeof_pass`'s early return is now a guard around the pend
walk alone; the check runs over every unit, because a local read before it is assigned is
wrong in a program with no deferred `.` exactly as in one with a hundred.

**6. Two ceilings, both new.** `TK_MAXDA` 256 locals per function: on overflow the rule
**steps aside for that function** rather than judge a name against a table that could not
hold its declaration — a silent accept is safe where a silent refuse is not.
`TK_MAXCAPRD` 256 by-value captures over the unit, which is a diagnostic
(`teko: too many captures by value in one unit`) because a missing row there would produce
a false refusal.

**7. D42's second defect is closed at compile time.** Its ruling — "this is the developer's
error, documented, not the language's" — was explicitly conditional on the nullable design
arriving ("where the compiler learns that a name declared without `?` must be built before
it is read"). It has. [`docs/reference/types.md`](docs/reference/types.md) § `struct` now
says the read is refused, and `null` reaching a non-nullable local is one road narrower in
[`docs/reference/nullable.md`](docs/reference/nullable.md): what remains open is the branch
the rule deliberately does not judge, a row of
[`docs/reference/not-yet.md`](docs/reference/not-yet.md) with the other seven.

**8. Two findings this crumb did not act on.** A refusal raised on a node the
`<teko-loop-prelude>` `#rule` built (`x++`, `x--`, `x += k` as a STATEMENT) is reported at
the rule's own file and line rather than the source's — pre-existing for every diagnostic
on such a node (``teko: no operator `+` takes these operands`` on `t++` prints the same
file), and none of Q3's business. And `mc limits`' registration rows are untouched:
`passes` 15/30, `syntax` 15, `infix` 24, `syntax_type` 2/8, `intrin` 8/16, `alias` 19,
`types` 11 — only `nodes`, `funcs`, `globals`, `strings` and `symbols` move, which is the
code itself.

**Proof:** 57/57 fixtures at their `expect-exit`, `tests/surface_definite.tk` (the new one)
at 42 over nine helpers, every one of them a shape the analysis must ACCEPT — the false
refusal is the failure mode that fixture exists to catch; `--dump-ast` of the 56
pre-existing fixtures byte-identical to `7e7d4bca`, the crumb refusing and rewriting
nothing; `FIXPOINT OK`; `sh scripts/check-docs.sh` green, its 108 samples included; and a
probe set outside `tests/` — ten refusals (a `struct` written through, a scalar, a class
method call, an assignment that comes later, `a = a + 1`, a loop body, an inner
declaration that does not assign the outer name, a `T[]` indexed, `x++`, a method body) and
five acceptances (a `T?` with no initializer, one branch only, `out`/`ref`/`use`, the
`for`/`foreach` forms, and the four positions out of scope).

### D47 · `enum`: `ToString`, `Parse`, `TryParse`, `IsDefined` — a parallel mechanism, not `teko_prim.tk`'s (2026-09-08)
`docs/specs/enum.md` § 6, N2b — the second crumb of the `enum` sequence, over D39's own
`TK_KENUM` type and D40's primitive-member table. Four names, dispatched by the enum's own
struct-table row (`si`) rather than by `teko_prim.tk`'s `type_new`-keyed lowering table:
`TimeSpan`/`DateTime` have NO row there, an enum always does (D39), and registering one in
`tk_prim_type` would make `tk_ty_binary` (teko_typeof.tk) ask `tk_prim_op_ret` for every
enum binary — a table an enum's own bitwise/comparison operators (D39's early claim in
`teko_ops.tk`) never populate a row of, silently breaking `Color.Red | Color.Green`'s own
typing. So N2b is a self-contained, PARALLEL mechanism in `teko_enum.tk`: two globals per
enum and four ordinary calls into `lib/rt.tk`, none of it touching `teko_prim.tk`'s tables,
`tk_prim_is`, or a single line `TimeSpan`/`DateTime` read.

**The two globals are built LAZILY**, the first time `ToString`/`Parse`/`TryParse`/
`IsDefined` is actually spelled on that enum — never at the `enum` declaration itself.
`tk_enum_ensure(si, line, fl)` is the memoized builder: it walks `teko_const.tk`'s own qualified-
constant table for the rows this enum wrote (`mc_cls_at(i) == si`), in the SAME order they
were declared, and emits `str Name__names[n] = { ... }` / `i64 Name__vals[n] = { ... }` as
plain `N_GLOBAL` nodes with a compile-time-constant initializer list — no `N_BLOB`, no
relocation, no generated function body per enum, exactly the mechanism the spec's § 6
states. Declaration order is what makes an ALIASED value's `ToString` answer the FIRST name
declared with it, C#'s own rule, for free — a forward scan over the SAME array. Because the
call that first asks for text may stand in any body, the two globals go through
`tk_top_emit` (D27, category (c) — `teko_heaparr.tk`'s own per-element-type row is made
lazily the same way, for the same reason).

**An enum a program never asks text of pays nothing.** None of the 57 fixtures that existed
before this crumb spells `ToString`/`Parse`/`TryParse`/`IsDefined` on an enum, so **no
`Name__names`/`Name__vals` global appears in any of their `--dump-ast`** — the lazy-globals
design's own proof. `--dump-ast` of the 3 fixtures that do not `#include "../lib/rt.tk"`
(`hello`, `primitives_ptr`, `primitives_scalar`) is byte-identical to `33c7485c`. The other 54
DO include `lib/rt.tk`, and their dump moves — not from anything this crumb's own design
changed, but because `lib/rt.tk` is `#include`d text: the six new functions this crumb
appends (`tk_enum_find`, `tk_enum_name`, `tk_enum_isdefined`, `tk_enum_parse`, the four
`tk_enum_tryparseN` as one row, `tk_str_eq`, `tk_i64_to_dec`) land in EVERY fixture that pulls
`lib/rt.tk` in, called or not. Measured precisely: identical except for the appended new
`rt.tk` functions, which no pre-existing fixture calls — zero lines removed from `lib/rt.tk`
itself (`diff` against `33c7485c`'s own copy: 0 `<` lines, a pure append after its existing
end), and zero pre-existing function BODY moves anywhere in the 54 dumps. The spec's own
preference (§ "if the table is generated only when `ToString`/`Parse` is used, nothing
changes: prefer this") is what the design follows for the two GLOBALS; the new FUNCTIONS
`lib/rt.tk` itself carries are a one-time, one-crumb cost every `#include "rt.tk"` fixture
pays regardless, the same as every earlier `lib/rt.tk` addition in this log (D40's
`TimeSpan`/`DateTime` helpers, D39's own). Eagerly emitting the two per-enum globals for
every declared enum was considered and rejected on the globals' own measurement above.

**Two dispatch sites, mirroring `teko_prim.tk`'s own split for the identical reason.**
`Color.Red.ToString()` never resolves at PARSE time: `tk_struct_of_expr` (teko_struct.tk)
recognizes a local (`N_IDENT`) and a call (`N_CALL`) as a typed receiver, not a retyped
`N_INT` (a qualified enum constant, D39's own retagging), so it defers — exactly the "a
receiver the parser cannot type" road `teko_typeof.tk`'s pass already owns. A LOCAL
receiver (`c.ToString()`) resolves at parse time, through `tk_member_of` (teko_expr.tk); a
deferred one resolves through `tk_pend_emit`'s own PASS-time twin (teko_typeof.tk). Both
gained one guard, `if (tk_is_enum(si)) { r = tk_enum_instance/tk_enum_pend(...); if (r != 0)
return r; }`, ahead of the existing "unknown member of" refusal, mirroring the `tk_prim_is`
guard `tk_dot`/`tk_pend_do` already carry for a primitive receiver. A name other than
`ToString` answers 0, read as "not ours" — which is also what keeps the lazy build lazy: an
unrecognized member name never reaches `tk_enum_ensure`.

**The statics** (`Color.Parse`, `TryParse`, `IsDefined`) hook `tk_static_member`
(teko_access.tk) the same way, ahead of its own unconditional "has no member" for an enum:
`tk_enum_static(si, m, line, fl)` answers 0 for any other name, read the same way.

**`TryParse`'s `out` argument is read directly through `tk_ref_addr` (teko_ref.tk), not
through `parse_expr`'s own registered `out` handler.** The obvious shape — parse the second
argument as an ordinary expression and let `out c` tag itself — breaks on
`tk_ref_check_call` (teko_ref.tk), the pass that walks every `N_CALL` and refuses an
argument whose `ref`/`out` tag does not match the CALLEE's own declared parameter kind: the
callee here is a plain `lib/rt.tk` function taking `uptr`, not a `ref`/`out` parameter, so a
TAGGED address argument is refused where an UNTAGGED one is not. `tk_ref_addr` builds the
same address (a bare local's `&`, a field's `left + offset`, an array element's own
addressing) without calling `tk_rfarg_tag`, so the node answers `TK_RP_NONE` by construction
— matching the callee exactly — and hands back the pointee's type in the same call, which is
what the `out` argument's own type check (`tk_reject_compat`, reused verbatim) needs.
Measured with a probe (`build/probe/p1_out_mismatch.tk`, not committed): the naive shape
refused every `TryParse` call outright before this fix, `teko: argument 5 is not passed by
reference`; this design compiles and runs it.

**`TryParse` writes through FOUR width-specific functions**, not one. A single `st64`
through the `out` slot would overrun a narrower enum's own storage — `Level : u8` is one
byte wide, not eight — so `lib/rt.tk` carries `tk_enum_tryparse8/16/32/64`, picked at the
call site by the enum's own `type_width`, the same question `tk_stn` (teko_struct.tk)
answers for an ordinary indirect store. `tests/surface_enum_text.tk` exercises both the
default `i32` width and an explicit `: u8` one.

**`ToString`'s digits fallback, and the two runtime helpers `lib/rt.tk` did not have.**
`str` is `uptr` with a NUL terminator and no `string` object yet (N7), so a name/value
lookup needs a byte-wise `tk_str_eq` and a decimal formatter `tk_i64_to_dec` this file did
not carry — added here, mirroring `mc`'s own `examples/api/lib/rt.mc:itoa` (not vendored,
the same small idiom, and the same documented limit: `i64`'s minimum is not representable
via negation and is not handled — no enum value reaches it, since C# does not range-check an
explicit cast either, D39 § 5). `tk_enum_name`/`tk_enum_value` are the spec's own pinned
pair (§ 6); `tk_enum_isdefined`, `tk_enum_parse` and the four `tk_enum_tryparseN` are thin
callers over them, none a new intrinsic.

**§ 9's own rule — the include is part of the surface — restated without depending on
`teko_prim.tk`'s own `tk_prim_need_include`,** which asks a question keyed on `tk_prim_is`
an enum never answers yes to. `tk_enum_need_include` is the same three-part message
(`teko: X needs #include "rt.tk" before it is used`) `TimeSpan` already established,
independently written so the two mechanisms genuinely do not share code.

**What N2b does NOT teach, and why**: `CompareTo`/`Equals` (the ordinal comparison is
already free through `<`/`<=`/`>`/`>=`/`==`/`!=`, N2a, so these two are a thin wrapper best
built once as `IComparable`/`IEquatable` conformance rather than one-off for `enum`);
`GetNames()`/`GetValues()` (need a HEAP `T[]`/`str[]` filled at compile time from the two
globals this crumb already writes — the array machinery is proven, filling one from a fixed
global inside a generated body is not measured, so it is left rather than shipped unproven);
`[Flags]`-style `ToString` decomposition (no attribute grammar). All three are recorded in
[not-yet.md](docs/reference/not-yet.md) § Enums, not silently dropped.

`tests/surface_enum_text.tk` (`expect-exit: 50`): `ToString` on a member, on an aliased
value (the first name) and on two out-of-set values (positive and negative, over the
default `i32` underlying); `Parse` round-tripping every member of two enums, aliased value
included; `TryParse` on a good and a bad name, over BOTH an `i32`-underlying and a
`u8`-underlying enum, the `out` slot left untouched on failure; `IsDefined` on a defined
value, an undefined one, and a value shared by an alias — plus, over a NARROW signed (`:
i16`) and a default `i32` enum each carrying a NEGATIVE member (`Neg = 0 - 100`/`Neg = 0 -
5`), `ToString`, `Parse`, `TryParse` (including the sign-extended value written through
`out`) and `IsDefined`, the review fix below's own regression cover.
`tests/surface_enum_parse_panic.tk` (`expect-exit: 70`): `Color.Parse("Nope")`.

**Review fix (`teko-org/teko-lang#696`): a negative member's own value collided with `-1`,
the "not found" sentinel.** `tk_enum_value(names, vals, n, s)` returned the MEMBER'S VALUE on
a match, `-1` on none — so `Signed.Parse("Neg")` on `enum Signed : i16 { Neg = 0 - 100, ...
}` (already in `tests/surface_enum.tk`) panicked (exit 70) instead of answering `Signed.Neg`,
and `Signed.TryParse("Neg", out s)` answered 0 without writing, because `Neg`'s own value
(`-100`) reads exactly like "not found" to a `v < 0` test. Fixed at the root: `tk_enum_value`
is now `tk_enum_find(names, n, s)`, answering the member's INDEX (or `-1`) rather than its
value; `tk_enum_parse` and the four `tk_enum_tryparseN` read `vals[idx]` only once the index
itself has cleared the `< 0` check, so no member's own value is ever read as a sentinel.
`docs/reference/runtime.md`'s own `enum` text table corrected to match (it named
`tk_enum_value`, "or `-1`" — the phrasing that hid the bug in the first read). Proof: the
reproducer named in the review (`enum Small { A = 0 - 5, B = 0, C = 5 } ... Small.Parse("A")
!= Small.A`) went from exit 70 to the expected answer; `tests/surface_enum_text.tk` gained
the `Signed`/`SignedWide` negative-member coverage above (12 more `return N` checks, N
picking up from 30 through 45, the fixture's own `expect-exit` moved from 42 to 50 to keep
every code distinct from the success one).

**Copilot finding (`teko-org/teko-lang#696`): `tk_i64_to_dec` negated `v` and so overflowed
on `i64`'s minimum.** The formatter's own comment claimed no enum value reaches it; wrong —
an enum over `i64` reaches it through an explicit cast, which C# does not range-check either
(docs/specs/enum.md § 5), so `((Wide) (0 - 9223372036854775807 - 1)).ToString()` formatted
garbage. Fixed at the root, the textbook way: the digits are peeled on the NON-POSITIVE side
(a positive `v` is negated first, a negative one is kept), `'0' - v % 10` per digit since
mc's `%` truncates toward zero and so keeps a non-positive remainder for a non-positive
dividend. `tests/surface_enum_text.tk` gained `enum Wide : i64` and the three checks at the
ends of the range (`i64` minimum, `i64` maximum, zero — `return 46..48`); the minimum's check
answered 46 against the old formatter, 50 against the new one.

**Copilot's second pass (`teko-org/teko-lang#696`), two smaller ones.** `tk_enum_ensure`
refused "too many enums asked for text in one unit" at whatever `tk_line`/`tk_file` last
held, since its three callers set them only after the call; it now takes the use site
(`line`, `fl`) and refuses there — a no-op on accepted code, the 59 `--dump-ast` dumps
byte-identical before and after. `docs/reference/not-yet.md` named `GetNames`/`GetValues`'s
refusal as `teko: unknown static member of Color`; the enum path refuses earlier, as
`teko: Color has no member GetNames` — the row now says so.

Proof: `mc build . --config mc.macos.toml` clean; **59/59** fixtures at their `expect-exit`
(the 57 existing — Q3's `tests/surface_definite.tk` among them — plus the two this crumb
adds); `--dump-ast` of the 3 of the 57 existing fixtures that do not `#include
"../lib/rt.tk"` (`hello`, `primitives_ptr`, `primitives_scalar`) byte-identical to
`33c7485c`, compiled from scratch by both releases; the other 54 move — the appended new
`lib/rt.tk` functions landing in their own `#include`, not called by any of them, no
`Name__names`/`Name__vals` global among the moved lines, zero pre-existing function body
touched (`diff` of `lib/rt.tk` against `33c7485c`'s own copy: `0` `<` lines, a pure append);
`FIXPOINT OK` (`teko1.o == teko2.o` on the first turn, `--dump-asm` diff empty over 213151
lines, 59/59 fixtures under the self-hosted `teko1`); `sh scripts/check-docs.sh` green (562
links, 378 diagnostics, 110 samples — 73 run, 37 no-run, the two this crumb adds among
them); `mc limits . --config mc.macos.toml` verdict `ok`, `syntax` `15/30`, `passes`
`15/30`, `intrin` `8/16`, `alias` `19/38`, `types` `11/22` — every row identical, element
for element, to the same command run against `33c7485c` from scratch: N2b adds no syntax
word, no pass and no intrinsic, exactly the spec's own § 10 prediction ("nothing" for every
row `TimeSpan`'s own C1 did not already move). Five probes outside `tests/`
(`build/probe/*.tk`, not committed) proving the five refusals this entry names, each with
its exact message: a `TryParse` `out` argument of the wrong type (`teko: a value of type
i64 does not convert to Color`), a wrong argument count (`teko: wrong number of arguments
for Parse`), a missing include (`teko: Color needs #include "rt.tk" before it is used`),
`ToString` without `()` (`teko: the member is a method; call it with (): ToString`), and
`TryParse`'s second argument not `out` (`` teko: TryParse's second argument is `out <name>`
``). `mc pkg hash .` after the review fix and the two Copilot passes above:
`cc2d24556b11ae8a523b7ad91d4690a988149f8315ac08fb03d99e3321e30238`.

### D48 · `DateTimeKind` is an `enum`, and a primitive row's parameters are per position (2026-09-08)
`docs/specs/enum.md` § 8, N2c — the last crumb of the `enum` sequence, over D39's `TK_KENUM`
type, D47's text side and D41's primitive-member table. **The whole surface of
`tests/surface_datetime.tk` is byte-identical to what C2 landed**, which is the point of
the crumb: `leap.Kind != DateTimeKind.Unspecified` is spelled the same either way, and
everything that moved is under it.

**`DateTimeKind` is now an ordinary declaration in an ordinary library file:**

```teko
// no-run
public enum DateTimeKind : i32 { Unspecified = 0, Utc = 1, Local = 2 }
```

in `lib/time.tk`, beside the functions that use it. D41's `type_alias("DateTimeKind",
ty_i32)`, its `syntax_expr` and the three `tk_dtk_unspecified/utc/local()` functions are
DELETED — an alias and an enum of one name cannot coexist, `tk_newname` (teko_struct.tk)
refuses the second with `teko: the name is already a type`. This is the first `enum` teko
declares from inside an `#include`d file; it works because the include is textual and read
before any use, and because `tk_fwd_scan`'s pre-scan does not need to know the word.

**It is a pure tightening, and the tightening is the deliverable.** An alias over `i32`
converted from any integer; an enum converts from nothing but itself (D39 § 5). Measured on
this branch:

- `i64 n = d.Kind;` → `teko: a value of type DateTimeKind does not convert to i64`
- `DateTimeKind k = 7;` → `teko: a value of type i64 does not convert to DateTimeKind`
- `new DateTime(t, 7)` → `teko: a value of type i64 does not convert to DateTimeKind`
- `d.Kind + DateTimeKind.Utc` → ``teko: no operator `+` takes these operands``

All four compiled before. `(DateTimeKind) 7` is still accepted — an explicit cast into an
enum is C#'s own — and the run-time guard in `tk_dt_from_ticks_kind` (`k < 0 || k > 2`,
`teko: a date kind is out of range`, exit 70) STAYS, because that cast is exactly how a
program still reaches it. And the enum's whole text side comes along for free, with no row
of its own: `d.Kind.ToString()`, `DateTimeKind.Parse`/`TryParse`/`IsDefined`,
`case DateTimeKind.Utc:`, a ternary over two enum arms and a by-value capture all work
through the mechanisms D39/D47 already built.

**Three lines in `lib/time.tk` it was NOT** — the spec's § 8 estimate was wrong, because a
row of D41's table names its types at `teko_init()` time and this type exists only after
the `#include`. `teko_prim.tk` took three additions, all of them the ones D41 itself named
as coming:

1. **A column may name a type that does not exist yet.** `tk_prim_late(name)` returns an id
   BELOW -1 (`-2 - slot`, so -1 keeps its meaning of "no type here") and `tk_prim_ty(col)`
   resolves it at the SITE through `tk_struct_find_exact`/`sr_ty_at`. The three accessors
   (`ppos_ty_at`, `pmr_ret_at`) are the only readers, so no caller sees the encoding, and a
   name still undeclared answers -1 — which every reader already takes as "refuse nothing",
   and which is the very site `tk_prim_need_include` refuses for the missing include.
2. **A row's parameter list is a COUNT and a HEAD into a pool of positions**, one column per
   argument, where it was a count and ONE type. `new DateTime(ticks, kind)` is an `i64`
   beside a `DateTimeKind` and is the first row that needed it — the case D41 wrote down as
   "the day one is, this column becomes a list and nothing else moves", and nothing else
   moved: `tk_prim_membern` writes `np` positions of one type for every other row, and
   `tk_prim_member2` is the two-position registration. Ceiling `TK_MAXPRIMP` 128 (41 used),
   `TK_MAXPRIML` 4 (1 used), both with a capacity message.
3. **`tk_prim_ret` and `tk_prim_conv` grew one enum arm each.** The lowering symbol still
   answers the UNDERLYING integer (`i32 tk_dt_kind(i64)`, unchanged) and still takes one
   (`tk_dt_from_ticks_kind(i64, i64)`, unchanged, and `lib/time.tk` never names the enum),
   so the compiler writes `(DateTimeKind) tk_dt_kind((i64) d)` on the way out and `(i64) k`
   on the way in. Neither is an instruction and neither is a licence the surface has: the
   precedents are `tk_nl_payload` (teko_null.tk, an enum out of a nullable box) and
   `tk_cap_val` (teko_deleg.tk, an enum captured by value into a lambda).

Nothing else was needed. The comparison, the overload pick, the ternary, the capture, the
`switch` label and the deferred `.ToString()` are all keyed on the type table's own row and
worked with no line added — proved by the fixture, not assumed.

**The one thing the crumb LOST: the friendly include refusal.** `DateTimeKind.Utc` without
`#include "time.tk"` used to say `teko: DateTimeKind needs #include "time.tk" before it is
used`; it now says `teko: unknown member: Utc`, and `DateTimeKind k;` reaches the core's own
`expected ; after expression`. Keeping the hint was tried and is IMPOSSIBLE without touching
a shared parse function: any registration keyed on the word (`syntax_expr` is the only door,
and mc's `syntax_expr_find` scans backwards so the enum's own later registration would
correctly shadow it) calls `word_add`, which makes `DateTimeKind` a TOKEN rather than a
`T_IDENT` — and `enum DateTimeKind` in `lib/time.tk` then dies at `tk_newname` with
`lib/time.tk:204: teko: name of enum expected: DateTimeKind`, measured on a throwaway build.
Buying the message back would mean a clause in `tk_newname` (every class, struct, interface,
trait and enum declaration passes through it) plus reserving the word program-wide for
programs that never include the file. `DateTime` and `TimeSpan` keep their own include
refusal, because they are compiler registrations; a library type is told apart by the
library, which is exactly what `DateTimeKind` now is. Written down in
[diagnostics.md](docs/reference/diagnostics.md).

**Fixtures: 61.** `tests/surface_datetime.tk` is UNCHANGED (byte-identical, still 42) —
that is the proof the surface did not move — and two are new:
`tests/surface_datetime_kind.tk` (42), which is `DateTimeKind k = d.Kind;`, the three
members as `switch` labels through a parameter and a local, both explicit casts,
`new DateTime(t, k)` from a variable, from another date's own `.Kind`, from a parameter,
from a call's return, from a field and from an array element, `.ToString()` on a member/a
property/a value outside the set, `Parse`/`TryParse` with `out`/`IsDefined`, a ternary over
two enum arms and a by-value capture; and `tests/surface_datetime_kind_panic.tk` (70), the
run-time kind guard `(DateTimeKind) 7` still reaches. The refusals above had no harness at
the time (D33) and were in `diagnostics.md` behind a `// no-run` fence — landed, D52.

**`--dump-ast`, and the two diffs that are NOT the accepted code moving.** 53 of the 59
existing fixtures are byte-identical to `37417b63`. The six that are not are exactly the
six that `#include "../lib/time.tk"`, and their diffs are two facts and nothing else:

- **three functions deleted** (`tk_dtk_unspecified/utc/local`, 15 lines of dump), in all
  six — the library file lost them;
- **one interface id shifted by one**, in `surface_nullable_ops.tk` (5 → 6) and
  `surface_nullable_value.tk` (19 → 20). An interface's id in its own itab is its ROW INDEX
  in teko_struct.tk's type table (`tk_itab_emit`, teko_iface.tk), the enum takes a row where
  the `type_alias` took none, and every type declared after it in the same unit moves up by
  one. The itab entry and the lookup are the same number, so it stays consistent — proved by
  those two fixtures passing.

`surface_datetime.tk`'s own dump changes as § 8 predicted and in no other way:
`DateTimeKind.Utc` is now `INT val=1 type=DateTimeKind` (a folded constant) where it was
`CALL name=tk_dtk_utc`, `.Kind` gains a `CAST type=DateTimeKind` over the call, and the
constructor's kind argument gains the `CAST type=i64` back down.

**The Copilot finding on #697, and the root it had.** The tightening above was
BYPASSABLE, and the review caught it: `new DateTime(1, k)` on an `i64 k` parameter compiled
and reached the run-time kind guard (`teko: a date kind is out of range`, exit 70) where § 5
refuses it at compile time. The cause is one line of `tk_prim_args`, which runs with
`atpass = 0` for `new`: the parser's oracle (`tk_pty_of`) answers -1 for a parameter,
`tk_check_scalar_compat` reads -1 as "refuse nothing" and returns, and `tk_prim_conv` then
wrapped the argument in the enum column's own `(i64)` cast — so the value crossed under the
`i64` the lowering symbol declares and `tk_rc_call_args` (teko_rc.tk), the late check that
catches every other unresolved argument against that declaration, had nothing left to see.
It is the CAST that laundered it, which is why the mirror position never leaked:
`new DateTime(k, DateTimeKind.Utc)` with `k` of enum type is `teko: a value of type
DateTimeKind does not convert to i64` today, with no line of this fix, and so are the field
and the call-return spellings of the kind argument, which `tk_pty_of` does resolve.

**The fix is the deferral, and only the check is deferred.** An argument the oracle cannot
type, landing on a column whose conversion is a cast — a primitive one and an enum one, the
only two — is remembered with the type its POSITION asks for (`tk_prim_arg_defer`, 128
entries, its own capacity message) and judged in the operator pass, on the node itself,
where teko_ops.tk's walk already carries this file's cast check: after the oracle, under the
scope the argument was written in, and with no pass of its own (`passes` 15/30 unmoved).
The CONVERSION is not deferred and needs no deferral — on both cast arms `tk_prim_conv`
never reads the argument's type at all, so the node written at parse time is the node it
would write knowing it, and a check that passes proves the type was the column's own. That
is why **all 61 existing `--dump-ast` dumps are byte-identical** to `cd606290`.
And an argument NOTHING types, even at pass time, is refused rather than guessed at:
`teko: the type of this argument is not known here` — `tk_prim_binary`'s own rule one
position over, and the one shape that reaches it is a local array's element, which lowers
to `ld64(a + i * 8)` at parse time and loses its element type there (`new DateTime(t,
a[0])`, `d.CompareTo(a[0])`, both of which used to compile and read raw bytes). The
accepted twins — the same four shapes with a `DateTimeKind` value — are in
`tests/surface_datetime_kind.tk` (codes 43-47), the refused ones in `diagnostics.md`.

**Proof**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config
mc.macos.toml` clean; **61/61** fixtures at their `expect-exit`; `--dump-ast` as above;
`sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (61/61 under the
self-hosted `teko1`); `sh scripts/check-docs.sh` green (564 links, 381 diagnostics, 113 samples). `mc limits` verdict `ok` throughout — on the compiler's own floor (the
`tests/hello.tk` leg) `alias` **19 → 18** (the deleted `type_alias`) with `syntax` 15,
`types` 11, `passes` 15/30 and `intrin` 8/16 all unmoved; on a program that includes
`lib/time.tk` (the `tests/surface_datetime.tk` leg) `types` **13 → 14** and `syntax`
**15 → 16** (the enum's own `type_new` and its `syntax_expr`/`syntax_stmt` pair, the exact
cost § 10 publishes for any `enum` a program declares) with `alias` 21 → 21, since the
`type_new` puts back the alias the deleted registration freed. The spec's § 10 prediction
of "`alias` 14 stays 14" is right for the program and one too high for the compiler;
`syntax` is the arena's own `T_SYNTAX` high-water mark over the four registries that share
it, which is why removing one `syntax_expr` from the floor did not move it. `mc pkg hash .`:
`d14f7c7519288990ee9d849b0de8b529c6cbe5c568fef8f880bcc7cb6a52d0dd`.

**Copilot finding, third pass — three on the deferred check itself, and the one root under
two of them.** The deferral above was right about WHEN to check and wrong about WHAT the
oracle may be asked. All three were reproduced before they were fixed, on `f6a565f1`:

1. **A global was refused for a type it plainly declares.** `DateTimeKind g =
   DateTimeKind.Utc;` at the top of a file and `new DateTime(t, g)` inside a function read
   `teko: the type of this argument is not known here` — a regression against the alias,
   which took any integer there. `tk_ty_of` (teko_typeof.tk) answered -1 for every global
   but G1's `T[]` of heap, and its own header called that the truth. It is not: a global is
   in scope in EVERY body of the unit, so the oracle now answers a global by its
   declaration (`tk_ty_global`, teko_array.tk), off the sweep that already collected the
   arrays — one more row per global SLOT, a global that holds one value, told from
   `i64 a[4]` by `nd_val != 0`, the rule `tk_on_stmt` already reads for a local.
   `TK_MAXGSLOT` 2048, **521** in the compiler's own unit (`mc_teko.tk`, mc's core
   included), with its own capacity message.
2. **An overloaded callee laundered the argument, and refused a legitimate one.** At the
   check's own point a call to a top-level name still carries the name the source wrote,
   and both oracles answer `decl_ret(decl_find(name))` for an N_CALL — the FIRST
   declaration of a name that may carry several signatures, the one `tk_over_pass` replaces
   two passes later with the symbol the ARGUMENTS pick. Measured, both ways: with
   `DateTimeKind pick(i64)` ahead of `i64 pick(i64, i64)`, `new DateTime(1, pick(1, 2))`
   saw an enum, deferred nothing and let the `i64` cross under the column's cast to the
   run-time kind guard (exit 70); with the two in the other order, the same call was
   REFUSED for a conversion the chosen overload never asks for, and so was `new
   DateTime(pick(1, 2), DateTimeKind.Utc)` on the ticks column.

   **The fix is that a call's type is not decided at the check's point at all.**
   `tk_prim_arg` (teko_prim.tk) passes -1 to `tk_check_scalar_compat` for any N_CALL, so on
   a CAST column the argument is deferred and judged by `tk_prim_arg_rest` once the symbol
   is written, and on every OTHER column it crosses under its own type and
   `tk_rc_call_args` (teko_rc.tk, the last pass of all) judges it against the very
   declaration the row names — the division of labour D48 already had, with the guess
   removed from both sides. The CONVERSION still reads the oracle's answer unchanged, which
   is what keeps the float column widening `TimeSpan.FromHours(n)` and keeps every dump
   still.

   Two placements were weighed and one was taken. Resolving the nested overload EARLY would
   mean running `tk_ov_collect`/`tk_ov_scan` before their pass, which moves the overload
   refusals ahead of every other one; deferring and re-checking needs no new pass and no new
   order, so `tk_prim_arg_rest` moved from the end of `tk_ops_pass` to the end of
   `tk_over_pass` — the same one call, three passes later, and `tk_prim_arg_pend` leaves a
   call to it instead of judging it in the walk. The walk's scope is what a NAME needs; a
   call's type is its callee's declared return type, which no scope enters into, so nothing
   was lost by moving it. `passes` stays **15/30**: no pass was added.
3. **The fixture's own comment claimed coverage it did not have.** Of the four shapes
   `tests/surface_datetime_kind.tk` said reached the deferred check, only the parameter did
   — `utc_kind()` was declared above `main`, `st.kind` was a local's field and `ks[0]` a
   local enum array's element, all three typed by the parser. Measured with a throwaway
   build that accumulates the deferred lines: `DEFER: 41`, one entry. The fixture now
   FORCES the shapes instead of claiming them — `utc_kind` is declared at the bottom of the
   file, the field is read off a `Stamp` PARAMETER (`at_field`), the global and the two
   overloaded callees are new — and the same instrumentation over it prints six entries,
   one per intended line (`DEFER: 58 62 68 192 212 214`: parameter, field of a parameter,
   global, forward call, and the two overloaded calls). What does NOT defer is written down
   as what it is: a local enum array's element carries its element type into the load, and
   it is the `i64` twin that loses it and is refused. **Fixtures stay 61**; the file gains
   codes 48-53 and `tests/surface_datetime_kind.tk` is the ONLY dump that moves.

**Proof of the third pass**, mc **0.15.23**, macos/aarch64: `mc build . --config
mc.macos.toml` clean; **61/61** fixtures at their `expect-exit`; `--dump-ast` of all 61
against `f6a565f1` — **60 byte-identical**, the touched fixture the only diff;
`sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (`teko1.o == teko2.o`
on the first turn, `--dump-asm` of teko2 vs teko3 empty over 214044 lines, 61/61 fixtures
under `teko1`); `sh scripts/check-docs.sh` green (564 links, 382 diagnostics, 114 samples).
`mc limits` verdict `ok` on both legs with every table unmoved — floor (`tests/hello.tk`)
`passes` 15/30, `syntax` 15, `alias` 18, `types` 11, `intrin` 8/16, and the
`tests/surface_datetime.tk` leg `syntax` 16, `alias` 21, `types` 14 — the only figure that
moves is the arena's own high-water, `467792 → 1114960` bytes on the floor and
`3211120 → 3858288` on the datetime leg (the two tables plus this crumb's own source,
against a 33554432-byte reservation). `mc pkg hash .`:
`f3c53a71be989bcf05fe302a040150a3cbf1289b2a1dce4b08ae7b55c482537d`.

**Copilot finding, fourth pass — the CONVERSION was still made on the guess, and the
oracle's new answer made one global honest and one dangerous.** Both were reproduced on
`108391e1` before they were fixed.

1. **A float column laundered what a cast column no longer could.** The third pass took the
   guess out of the CHECK and left it in the conversion, and on a float column the
   conversion IS the decision: `tk_num_widen` widens an integer and leaves everything else
   alone. With an `i64 pick(i64)` declared ahead of a `DateTimeKind pick(i64, i64)`,
   `TimeSpan.FromHours(pick(1, 2))` read the FIRST declaration, wrote `(f64)
   pick__i64__i64(1, 2)` over a call that returns an enum, and `tk_rc_call_args`
   (teko_rc.tk) saw exactly the `f64` the column asks for and had nothing to say — the same
   laundering, one column over. The mirror pair, an `f64 pick(i64)` ahead of an `i64
   pick(i64, i64)`, wrote NO cast over an integer return and fed the raw eight bytes to a
   float parameter.

   **The fix is that `cty` is what everything reads.** `tk_prim_arg` (teko_prim.tk) already
   built the -1 an N_CALL deserves for the check; it now hands that same -1 to
   `tk_prim_conv`, so no node is written on a guess: both cast arms never read the type at
   all, the float arm writes nothing, and every other column's conversion is the identity
   whatever the type is. `tk_prim_defers` adds the float column to the deferral table, and
   `tk_prim_arg_widen` writes the cast once `tk_over_pass` has picked the symbol. The wrap
   is IN PLACE (`tk_nd_moved`, the node-rewrite pattern teko_rc.tk's `tk_rc_assign` already
   uses) because the argument was spliced into the lowered call's own list two passes ago
   and nothing at that point holds the link it is; `nd_next` is the one field left behind,
   since the sibling link belongs to the SLOT and not to the value. Only a CALL defers on a
   float column, and the reason is WHOSE type a later pass still moves: a call's is its
   callee's, which the overload pick may replace, while every other argument crosses under
   the type it already has and `tk_rc_call_args` (teko_rc.tk, the last pass of all) both
   judges it and writes the widening against the declaration the row names --
   `TimeSpan.FromHours(x + 1)` on an `i64 x` parameter is three hours with no entry in this
   table, measured. (The fifth pass below replaced the two resolvers this sentence first
   named with one walk; the invariant is unchanged and its reason is this one.) The rule
   itself moved to `tk_num_widens` (teko_typeof.tk) so the two readers cannot drift. **All 61 dumps stayed
   byte-identical** across this fix alone: no fixture had a call in a float column, and the
   cast still lands where the literal already put it. `tests/surface_datetime_kind.tk`
   gains codes 54-57 over both declaration orders and a float-returning overload; the
   refusal (`teko: a value of type DateTimeKind does not convert to f64`) is in
   diagnostics.md.

2. **`tk_ty_global` has a side effect, and it is right twice and wrong once.** With the
   oracle answering a global by its declaration, a global receiver stops falling through to
   the by-name member search. Measured against `33c7485c` (main), which refuses all three:

   - a PRIMITIVE global (`TimeSpan g; g.Days`, `g.TotalHours`, `g.Negate()`,
     `g.CompareTo(t)`, `g.Equals(d)`, and a write followed by a read) is **right**, and
     comes for free — not one line was added for it. `docs/reference/not-yet.md`'s row is
     deleted, and so are the sentences in `timespan.md` and `datetime.md` that cited it;
     `tests/surface_timespan.tk` reads `g_span` as a receiver (codes 70-76);
   - a global declared `T?` over a REFERENCE is **right** too: its handle IS the pointer,
     so `.HasValue`, `.Value`, `??` and `?.` read as a local's do and the object lives as
     long as any other global's (`rt_live()` measured). `tests/surface_nullable_ref.tk`
     gains `globalcheck()`, codes 110-119;
   - a global declared `T?` over a VALUE is **wrong**, and the branch turned a silent hole
     into a segfault. The box that type promises is built by the rc pass out of the SCOPE a
     local lives in — `tk_rc_var` and `tk_rc_assign` (teko_rc.tk) both return at once for a
     name no scope holds — so `i64? n; n = 5;` stored the bare 5, and `n.Value`, which on
     main answers `teko: unknown member: Value`, became `ld64(tk_nl_ck(5) + 24)`: exit 139.

   The DECLARATION is what is wrong, so the declaration is what is refused: `teko: a global
   does not hold a nullable box`, from `tk_gs_check` (teko_array.tk) in the same sweep that
   collects the slots, which closes `n = 5;` and not only the read. not-yet.md keeps its
   row under the new message and `nullable.md` says which half of `T?` a global takes.

Two nits with it: `docs/specs/datetime.md` § 11's inventory and `docs/specs/enum.md` § 12's
N2c gate line now name `tests/surface_datetime_kind_panic.tk` (70) beside the 42 one.

**Copilot finding, fifth pass -- the check had TWO points and needed one, and a node that
moves takes its type with it.** Both were reproduced on `d8d00572` before they were fixed.

1. **A legal argument was refused, because the point that judged it was dead for its
   shape.** The third pass split the judgement in two -- `tk_prim_arg_pend` inside
   teko_ops.tk's walk for what a scope answers, `tk_prim_arg_rest` at the end of
   `tk_over_pass` for what the overload pick answers -- and the first half is UNREACHABLE
   for an argument that is an `N_BINARY` or an `N_UNARY`: `tk_ops_visit` returns from both
   of those arms before the hook at the bottom of the function. So the node fell to the
   sweep, which runs with NO scope entered, and `new DateTime(1, k | DateTimeKind.Utc)` on
   a `DateTimeKind k` parameter -- legal, since a bitwise operator over an enum answers the
   enum (§ 4 of the spec) -- read `teko: the type of this argument is not known here`.
   The same for `k & DateTimeKind.Local` and for `~~k`.

   **The fix is that there is one point again, and it is a walk of its own**:
   `tk_prim_arg_judge` (teko_prim.tk) drives `tk_ty_pass_walk` at the end of
   `tk_over_pass`, after that pass's own walk has written every picked symbol. That is the
   only place where BOTH halves of what types an argument are true at once -- the scope a
   parameter is read under is live, and every overload under the argument is already the
   signature its own arguments chose, at whatever depth. Moving the hook ahead of the two
   returns in `tk_ops_visit` was the smaller diff and the wrong one: it judges the node
   BEFORE the pick under it, which is exactly the laundering the third pass removed.
   Hooking `tk_ov_visit` itself does not work either, because `tk_ty_walk_list` visits a
   node before its children and a nested call is picked after its parent is visited.
   `tk_prim_arg_rest` is DELETED: nothing is left for a sweep. That a body walk reaches
   every deferred argument was measured rather than argued -- a throwaway build that
   errors on any entry the walk did not mark fired on none of the 61 fixtures nor on the
   compiler's own source under `scripts/bootstrap.sh` -- and the reason is what a global
   initializer may be: mc requires it to be CONSTANT, so a row's call never stands there
   (`DateTime g = new DateTime(1, gk);` is `global initializer must be constant`) and the
   constant arguments that do (`1 + 2`, `(i64) 3`, a `const` name) are typed by the parser
   and never defer. `passes` stays **15/30**, and a unit that deferred nothing walks
   nothing (the table is this module's own memory, and `tk_prim_arg_judge` returns on an
   empty one).

   The refusals do not move, and one of them gets the better wording for free: `new
   DateTime(1, x | 1)` on an `i64 x` parameter was `the type of this argument is not known
   here` and is now `teko: a value of type i64 does not convert to DateTimeKind`, the type
   it really has. `tests/surface_datetime_kind.tk` gains codes 58-61.

2. **A node rewritten in place kept a type that was no longer its own.**
   `tk_prim_arg_widen` wraps the argument by moving what the node WAS into a fresh node
   (`tk_nd_moved`) and making the original the `N_CAST`; a row of teko_struct.tk's
   expression-type table (`tk_xt_put`) is keyed on the NODE, so a registration the argument
   already had stayed on the wrapper. `tk_ty_of` reads that table BEFORE the node's own
   kind, so the `(f64)` then answered `i64` -- the type of what it wraps -- and
   `tk_rc_call_args` (teko_rc.tk), the last pass of all, widened it a second time.
   Measured: `TimeSpan.FromHours(h.Get())` with `h` a PARAMETER (so the `.` is a
   placeholder the oracle's pass rebuilds AND registers a type for) dumped `CAST type=f64`
   over `CAST type=f64` over the call. A local receiver does not reach it -- the parser
   types it, so the argument never defers -- which is why no fixture had caught it.
   **The fix is in the move, not in the caller**: `tk_xt_move` (teko_struct.tk) repoints
   every row of the node onto the fresh one, because the registration describes the VALUE
   and the value is what moved. `tests/surface_datetime_kind.tk` gains code 62 and its
   dump carries one cast.

**Copilot finding, sixth pass -- the deferred judgement trusted a type the operator pass
had guessed.** Reproduced on `38826cb4` before it was fixed.

The fifth pass gave the deferred argument ONE point of judgement, at the end of
`tk_over_pass`, and that point asks `tk_ty_of` for the argument's type. For an N_BINARY
`tk_ty_of` is `tk_ty_binary` (teko_typeof.tk), which answers from its operands -- and the
operands were TYPED by a pass that ran three earlier. `tk_ops_pass` reads a call as
`decl_ret(decl_find(name))`, the FIRST declaration of a name that may carry several, so
with an `i64 pick(i64, i64)` declared AHEAD of a `DateTimeKind pick(i64)` the walk saw two
integers in `new DateTime(1, pick(1) + 1)`, left the node to the core as raw arithmetic,
and `tk_over_pass` then rewrote the call to the enum overload. The deferred check asked
`tk_ty_binary`, got `DateTimeKind` -- the LEFT operand's type, the core's own rule -- and
accepted the argument for exactly the type the column asks for. `k + 1` on an enum
parameter is ``teko: no operator `+` takes these operands`` (§ 4); `pick(1) + 1` compiled
and reached the run-time kind guard, and so did `pick(1) - 1`, `pick(1) * 2` and
`-pick(1)` (exit 70). It is the third pass's laundering one level up: the guess was taken
out of the check and out of the conversion, and stayed in the SUBTREE the check reads.

**The fix is that the claim is re-asked, not that the type is re-read.** `tk_prim_arg_do`
(teko_prim.tk) calls `tk_ops_rejudge` (teko_ops.tk) on the argument before it reads its
type, and that function is `tk_ops_binary`'s own three claims -- nullable, enum, primitive
-- in `tk_ops_binary`'s own order, over the types the pick left behind, with the operands
re-judged first for the same reason `tk_ops_operand` resolves them first. The enum and the
nullable claim only CHECK, so re-asking them is a no-op whenever the pick changed nothing,
which is why the 60 other dumps do not move; the primitive claim LOWERS, and it lowers here
exactly as it would have lowered there -- in place, with `tk_rc_pass` still to come --
because a primitive operand the guess hid is the one shape whose raw arithmetic is wrong
rather than merely untyped. Measured both ways on `38826cb4`: `t.CompareTo(spick(1) +
spick(2))` over two `TimeSpan.MaxValue` returns added eight bytes to eight bytes and
answered a wrapped number where the row panics (`teko: a time span overflowed`, exit 70),
and `TimeSpan.FromTicks(2).CompareTo(dpick(3) - epick(1))` over two `DateTime` returns
carried the two `Kind` bits into the `TimeSpan` (`1 << 62` ticks) where `tk_dt_sub` masks
them off. The USER-operator arm is not re-asked: a node it owns is already an N_CALL by
then (`tk_ops_emit` replaced it) and a node it left to the core is the pointer arithmetic
teko_ops.tk's own header allows.

**What it does NOT fix, and that is written down rather than worked around.** The guess is
still what `tk_ops_pass` judges everywhere OUTSIDE a deferred primitive argument, in both
directions: `DateTimeKind k = pick(1) + 1;` compiles, and a LEGAL `pick(1) |
DateTimeKind.Utc` -- a bitwise operator over an enum answers the enum -- is REFUSED with
``teko: no operator `|` takes these operands`` when the enum overload is not the first
declaration, inside a primitive argument as well as outside one, because that refusal is
raised three passes before the re-judgement exists. Closing it means resolving the
overloads ahead of the operator pass, which moves every overload refusal ahead of every
other one and is not this crumb's to do; the row is in
[not-yet.md](docs/reference/not-yet.md) under both spellings. The accumulated table over
the eight shapes the review named, in both declaration orders, is the fixture's own codes
63-64 for what is accepted and diagnostics.md for what is refused.

**Fixtures stay 61.** `tests/surface_datetime_kind.tk` gains codes 63 (an overloaded call
under a legal bitwise operator, the enum overload first) and 64 (the `DateTime - DateTime`
above, whose two overloads are declared `i64` first, and which answers 64 on the build
before this fix). Two nits with it: `docs/internals/primitives.md` still described the
first design of the deferred check -- two cast columns, judged on `tk_ops_pass`'s walk --
where it is three columns judged by `tk_prim_arg_judge`'s own walk at the end of
`tk_over_pass`; and `docs/reference/nullable.md`'s "Only locals" now says which half of the
sentence belongs to the definite-assignment walk (which never judges a global) and which to
the DECLARATION check `tk_gs_check` makes over a global declared `T?` over a value.

**Copilot finding, seventh pass -- the ROOT under all six, and what it made redundant.**
Every pass above fixed one consumer of one wrong answer: `tk_ty_of` typed a call to an
overloaded name by `decl_ret(decl_find(name))`, the FIRST declaration of a name that may
carry several. The seventh pass fixes the ANSWER instead, in D49 below,
and the machinery this entry built around the guess shrinks with it: `tk_ops_rejudge`,
the sixth pass's second judgement, is DELETED (57 lines), because the operator pass now
sees the picked type on its first visit. What STAYS is the parse-time half, measured: a
`new DateTime(...)` argument is lowered while the unit is still being read, where
`tk_pty_of` (teko_struct.tk) cannot ask an overload table that does not exist yet -- so
`tk_prim_arg`'s `cty = -1` for a call, the deferral table and `tk_prim_arg_widen`/
`tk_xt_move` are all still what they were. Removing that one line makes
`tests/surface_datetime_kind.tk` answer 1 instead of 42. The limit this entry's sixth pass
wrote into not-yet.md -- `DateTimeKind k = pick(1) + 1;` compiling outside an argument, and
a legal `pick(1) | DateTimeKind.Utc` refused -- is CLOSED, both directions, and the row is
replaced by what is left of it there.

**Copilot finding, eighth pass -- one latent arity slip, five stale sentences.**
`tk_prim_member`, the one-argument registration helper, tested `p0 >= 0` to tell "no
argument" from "one", but a late type name (`tk_prim_late`, D48) is an id BELOW -1, so a
one-argument row over a late type would have been stored with arity zero. No row does that
yet (the only late column sits in the two-argument constructor, registered by position), so
the 62 dumps are byte-identical before and after; the test is `p0 != -1` now, the sentinel
itself. The five sentences (`teko_prim.tk`'s two rationales, `docs/internals/primitives.md`'s
row schema and walk rationale, `docs/specs/enum.md`'s gate count) still described the
first-declaration guess D49 removed, or the one-type row N2c widened, or 61 fixtures; they
say what the code does now.

**Copilot finding, ninth pass -- the table was rebuilt and judged once, and the compiler's
own names were nobody's.** D49 pulled `tk_ov_prepare` up to pass 6 and rebuilt its rows on
every call, but a flag (`tk_ov_scanned`) kept the JUDGEMENT at the first call: every
declaration the unit gains afterwards -- the `tkarr_new_T`/`tkarr_release_T`/`tkarr_put_T`
group `tk_params_pass` (12) emits, which is exactly the growth D49 measured at 84 rows to 94
-- was never scanned, so it was never marked and never mangled. A program declaring one of
those names by hand kept its own plain symbol beside the generated one and the site picked
whichever table answered first. Measured on `665c7442`, `i64 tkarr_put_i64(i64 a)` beside an
`i64 total(params i64[] xs)` and a `total(1, 2, 3)`: `teko: a value of type i64[] does not
convert to i64` at the CALL, a legal program refused for the wrong reason -- the generated
helper had been type-checked against the program's own function.

**The root is that nothing told a generated declaration from a written one**, and rescanning
alone does not fix it: with the scan repaired the same program reads `teko: no overload of
tkarr_put_i64 matches these arguments`, a better message for a program that is still legal.
Reserving the `tkarr_`/`tk_nl_` SPELLINGS was tried and is wrong twice over: those helpers
are emitted during the PARSE (`tk_ha_ensure_gen`, teko_heaparr.tk; `tk_nl_ensure_ck`,
teko_null.tk), so they are already in the pass-6 table and a prefix rule refuses 14 of the
fixtures outright; and the compiler's own sources declare `tk_nl_box_fn` and `tk_nl_boxname`,
so the bootstrap would refuse itself.

**So the mark is on the NODE, at the one door every generator uses.** `tk_top_emit`
(teko_struct.tk) is where a generated top-level declaration is added -- K6's own audit
already names it as the point every mid-declaration generator goes through -- and it now
records the pair (node, name) in a table of its own (`TK_MAXEMIT` 512, capacity message,
134 used by `tests/surface_lambda.tk`, the busiest fixture, and 0 by `tests/hello.tk`).
`tk_ov_collect` then refuses a top-level declaration the compiler did NOT emit whose name
one it DID emit carries: `teko: the name is the compiler's own: tkarr_put_i64`, and the same
for `tk_nl_ck`, `tk_nl_box_i64`, `tk_nl_new`, `tk_nl_dflt`, a `tk_nl_vt`/`Name__vt` global
or an enum's `Name__names`. It is the node and never the spelling, so the generated
declaration itself passes, and a name that merely LOOKS like one is a program's own:
`tkarray` and `tk_nl_boxer` beside a `params` list and an `i64?` compile and run (exit 49).
Globals are checked too, though they are collected by nobody: `tk_nl_vt` is a generated
GLOBAL and a program declaring one would clash with it exactly as a function does.

**The memoization was audited, and no generator keys on `decl_find`** -- the worst case
would be a generator deciding "already emitted" because the PROGRAM declared the name, and
the program's function then silently standing in for the helper. Every one keys on a table
of its own: `ha_gen_at`/`ha_put_at` (teko_heaparr.tk), `tk_nl_ck_gen`/`tk_nl_box_gen`/the
`nl_box` payload list (teko_null.tk), `tk_enum_slot` (teko_enum.tk), `dgi_row_at`/`dgi_fn_at`
(teko_deleg.tk), `tk_ix_emitted` (teko_struct.tk). The three `decl_find` memos that exist
are include checks and name resolution (`tk_prim_need_include_of`, `tk_enum_need_include`,
`tk_deleg_resolve_fn`), not generators. Proved rather than argued: on `665c7442`,
`i64 tk_nl_box_i64(i64 v)` declared ABOVE the first `i64?` reached
mc's own `function declared twice` -- the generator had emitted its own anyway -- and it is
`teko: the name is the compiler's own: tk_nl_box_i64` now.

**Copilot finding, tenth pass -- the record of the compiler's own names had one door and
eight ways around it.** The ninth pass put the pair (node, name) of every generated
top-level declaration in a table of `tk_top_emit`'s own (teko_struct.tk) and taught
`tk_ov_collect` to refuse a program's declaration carrying one of those names. The door was
real and incomplete: `grep -n 'top_add(' teko_*.tk` answered fourteen sites, and eight of
them were generators that never went through it -- a class method's mangled body and a
constructor's (`tk_member_body`, `tk_member_ctor`, teko_class.tk), both property accessor
forms (`tk_prop_auto_body`, `tk_prop_arrow_body`, teko_prop.tk), a static field's global and
a struct's implicit allocator (`tk_static_field`, `tk_struct`, teko_struct.tk), a service's
slot and memoized getter (`tk_di_getter_sym`, teko_di.tk) and every declaration a generic
instance replays (`tk_gen_replay`, teko_generic.tk). Their symbols are as much the
compiler's as `tkarr_put_i64` is: the program never spells `point_area`, the mangling does.

**The fix is one door with two spellings, and the second is the first's own body.**
`tk_top_emit_as(i64 n, uptr owner)` records the pair, calls `top_add` and then sets
`p_decl_name()` to `owner`; `tk_top_emit(i64 n)` is `tk_top_emit_as(n, p_decl_name())`, the
mid-declaration form D27 asks for, unchanged to the byte for its eleven existing callers. A
generator that stands at TOP LEVEL -- inside a `class`/`struct` body the core is reading --
passes `0`, which is exactly what `top_add` leaves behind on its own, so those seven sites
gained the row and nothing else; the two in `tk_di_getter_sym` run from pass 4, where the
name is already 0, and take the plain door. `tk_gen_replay` keeps its own save of
`p_decl_name()` with the rest of its scratch and passes 0 too, because `parse_top` writes
the name itself once per declaration the instance produces -- and it answers 0 for a `class`,
whose members add themselves, which is why the function records nothing for a null node.
**That is the whole reason the 62 dumps do not move**: not one migrated site changed what
`p_decl_name()` is after it returns.

**Two sites are NOT recorded, and the reason is in the code.** teko_ns.tk's `tk_ns_top`
(a namespaced type's function at top level) and the `namespace A { ... }` body's own loop
add declarations the PROGRAM wrote; `tk_ns_rename_decl` mangles them two passes later, so
the node still carries the SHORT name here. Recording it would reserve `area` against the
very program that wrote `namespace geo { i64 area() }`, and it would say "the compiler's
own" about a body the compiler never wrote. The collision that family really has -- a
top-level `i64 geo__area()` beside that namespace -- is a program colliding with its own
mangled name, and it reaches the core's `function declared twice` before and after this
pass, unchanged.

**Measured, on `448c6154` and on this commit**, each family a program declaring the
generated symbol by hand:

| family | the name | on `448c6154` | now |
|---|---|---|---|
| struct implicit allocator | `stamp_new` | `teko: a value of type i64 does not convert to Stamp` (the wrong cause, at the `new`) | `teko: the name is the compiler's own: stamp_new` |
| static field | `stamp_made` | `global name declared twice` (the core) | `teko: the name is the compiler's own: stamp_made` |
| class method | `point_area` | the core's `function declared twice`, `call to unknown function` or teko's `no overload of point_area matches these arguments`, by how the program uses it | `teko: the name is the compiler's own: point_area` |
| class constructor | `point_ctor__i64` | the core's `function declared twice`, `call to unknown function` or teko's `no overload of point_ctor__i64 matches these arguments`, by how the program uses it | `teko: the name is the compiler's own: point_ctor__i64` |
| auto property accessor | `square_get_Side` | the core's `function declared twice`, `call to unknown function` or teko's `no overload of square_get_Side matches these arguments`, by how the program uses it | `teko: the name is the compiler's own: square_get_Side` |
| arrow property accessor | `square_get_Area` | the core's `function declared twice`, `call to unknown function` or teko's `no overload of square_get_Area matches these arguments`, by how the program uses it | `teko: the name is the compiler's own: square_get_Area` |
| service getter | `svc_di_get` | `function declared twice` | `teko: the name is the compiler's own: svc_di_get` |
| generic instance method | `box__circle__2_cap` | `... instantiated from ...: function declared twice`, or `no overload of box__circle__2_cap matches these arguments`, by how the program uses it | `teko: the name is the compiler's own: box__circle__2_cap` |
| namespace mangling | `geo__area` | `function declared twice` | `function declared twice` (deliberately unchanged, above) |

The first row is the one that was a genuine hole rather than a worse message (the verifier's
own reproduction reached it the other way round, `a value of type Stamp does not convert to
i64` at a caller that never wrote `new` -- the same silent misresolution): the core never
saw two declarations, because a struct's allocator and the program's function differ in
RETURN type, and the program's `Stamp stamp_new()` simply stood in for the generated one at
the `new` site. The others gained the cause in place of the symptom. A name that merely
LOOKS generated is still a program's own: `point_areas` and `pointarea` beside a
`class Point { i64 area() }` compile and run.

**`TK_MAXEMIT` stays 512, with the worst case measured rather than argued.** A throwaway
build that reports `tk_nemit` at the last pass, run over all 62 fixtures on both commits:
the busiest is `tests/surface_lambda.tk` at **134 -> 142**, then `tests/surface_di.tk`
67 -> 103, `tests/surface_nullable_ref.tk` 48 -> 59, `tests/order_types.tk` 45 -> 55,
`tests/surface_namespace.tk` 44 -> 52; `tests/hello.tk` is 0 on both. The compiler's own
source is 0 as well -- `mc_teko.tk` is written in the mc subset teko does not generate for,
and the same throwaway build reports `EMIT: 0` for it -- so the bootstrap has no worst case
of its own. 142 of 512 is 28% of the reservation, and the capacity message
(`teko: too many generated declarations in one unit`) is the one the ninth pass wrote.

**Proof of the tenth pass**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build .
--config mc.macos.toml` clean; **62/62** fixtures at their `expect-exit`; `--dump-ast` of all
62 against `448c6154` -- **62 byte-identical**, which is what says the seven top-level sites
kept `p_decl_name()` exactly where a bare `top_add` left it; `sh scripts/bootstrap.sh --os
macos --arch aarch64` -> `FIXPOINT OK` (62/62 under the self-hosted `teko1`);
`sh scripts/check-docs.sh` green (**568** links, 385 diagnostics, 117 samples). `mc limits`
verdict `ok` on both legs with every table unmoved from the ninth pass -- floor
(`tests/hello.tk`) `passes` **15/30**, `syntax` 15, `alias` 18, `types` 11, `intrin` 8/16,
heap 1114992, and the `tests/surface_datetime.tk` leg `syntax` 16, `alias` 21, `types` 14,
heap 3875504 (against a 33554432-byte reservation). `mc pkg hash .`:
`0b8e9993f94b8faf2fc4da8688c40aa06346bc5e106e3d3973bab0c841a15a96`.

**Proof of the ninth pass**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build .
--config mc.macos.toml` clean; **62/62** fixtures at their `expect-exit`; `--dump-ast` of all
62 against `665c7442` -- **62 byte-identical**, which is what says the rescan and the mark
change no accepted program; `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (62/62 under the self-hosted `teko1`); `sh scripts/check-docs.sh` green (567
links, **385** diagnostics, 117 samples). `mc limits` verdict `ok` on both legs with every
table unmoved -- floor (`tests/hello.tk`) `passes` **15/30**, `syntax` 15, `alias` 18,
`types` 11, `intrin` 8/16, heap 1114992, and the `tests/surface_datetime.tk` leg `syntax`
16, `alias` 21, `types` 14, heap 3875504 (against a 33554432-byte reservation).
`mc pkg hash .`: `5516de82a24b18f001eca4f4e8eed83843450129e328ba2c48f9efe08229f428`.

**Proof of the sixth pass**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build .
--config mc.macos.toml` clean; **61/61** fixtures at their `expect-exit`; `--dump-ast` of
all 61 against `38826cb4` -- **60 byte-identical**, and the one that moves is the fixture
edited here, a pure ADDITION once the compiler's own `$gN` temporaries are normalized (81
lines added, none removed); `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (`teko1.o == teko2.o` on the first turn, `--dump-asm` of teko2 vs teko3 empty
over 214672 lines, 61/61 fixtures under `teko1`); `sh scripts/check-docs.sh` green (567
links, 383 diagnostics, 117 samples). `mc limits` verdict `ok` on both legs with every
table unmoved from the fifth pass -- floor (`tests/hello.tk`) `passes` 15/30, `syntax` 15,
`alias` 18, `types` 11, `intrin` 8/16, heap 1114848, and the `tests/surface_datetime.tk`
leg `syntax` 16, `alias` 21, `types` 14, heap 3858176 (against a 33554432-byte
reservation). `mc pkg hash .`:
`1c76727914a271be8d92f6bb0516a2f27d4efb8ffc82f46d465ea6ea1a1578af`.

**Proof of the fifth pass**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build .
--config mc.macos.toml` clean; **61/61** fixtures at their `expect-exit`; `--dump-ast` of
all 61 against `d8d00572` -- **60 byte-identical**, and the one that moves is the fixture
edited here, a pure ADDITION once the compiler's own `$gN` temporaries are normalized (218
lines added, none removed); `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (61/61 under the self-hosted `teko1`); `sh scripts/check-docs.sh` green (565
links, 383 diagnostics, 116 samples). `mc limits` verdict `ok` on both legs with every
table unmoved from the fourth pass -- floor (`tests/hello.tk`) `passes` 15/30, `syntax` 15,
`alias` 18, `types` 11, `intrin` 8/16, heap 1114960, and the `tests/surface_datetime.tk`
leg `syntax` 16, `alias` 21, `types` 14, heap 3858176 (against a 33554432-byte
reservation). `mc pkg hash .`:
`9b657a1299e88d46d919e47b3e8ed2da8c4641e3f80492364d1dc6bf56b7bd5e`.

**Proof of the fourth pass**, mc **0.15.23**, macos/aarch64: `mc build . --config
mc.macos.toml` clean; **61/61** fixtures at their `expect-exit`; `--dump-ast` of all 61
against `108391e1` — **58 byte-identical**, and the three that move are the three fixtures
edited here, every one of whose diffs is pure ADDITION once the compiler's own `$gN`
temporaries are normalized (no existing line moved);
`sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (`teko1.o == teko2.o`
on the first turn, `--dump-asm` of teko2 vs teko3 empty over 214416 lines, 61/61 fixtures
under `teko1`); `sh scripts/check-docs.sh` green (564 links, **383** diagnostics, 115
samples). `mc limits` verdict `ok` on both legs with every table unmoved from the third
pass — floor (`tests/hello.tk`) `passes` 15/30, `syntax` 15, `alias` 18, `types` 11,
`intrin` 8/16, heap 1114848, and the `tests/surface_datetime.tk` leg `syntax` 16, `alias`
21, `types` 14, heap 3858288 (against a 33554432-byte reservation). `mc pkg hash .`:
`1f36848ed1779e8821b0f454e1b3ce19225ea8cd5b2bf6ddd8113247cc1a4c23`.

### D49 · The oracle asks the overload table: a call is typed by the pick, never by the first declaration (2026-09-13)
The seventh review pass on [#697](https://github.com/teko-org/teko-lang/pull/697), and the
ROOT of the family D48's third, fourth, fifth and sixth passes each patched one consumer of.
`tk_ty_of` (teko_typeof.tk) answered an N_CALL with `decl_ret(decl_find(name))` — the FIRST
declaration of a name that may carry several signatures — and the pass that replaces a site
with the symbol its ARGUMENTS pick is `tk_over_pass`, number **14** of the fifteen. Every
consumer of the oracle runs before it: the operator pass (11), the ternary and `??`/`?.` (9),
a primitive row's deferred argument, an initializer. Each of the four earlier passes taught
one of them to distrust the answer; this one makes the answer true.

**The design is a flag, not a new pass and not a new table** (option iii of the three the
recon weighed). `tk_ov_resolve` becomes `i64 tk_ov_pick(i64 n, i64 commit)`:

- at `commit == 1` it is the pass, unchanged to the byte — the rounds, then
  `tk_pm_expand_call` or `tk_fill_defaults`, then `set_nd_name`;
- at `commit == 0` it is a QUESTION. It runs the same five rounds over the same argument
  types (`tk_ov_arg_ty` takes the flag too, so a nested overloaded call is resolved the same
  way) and returns `decl_ret` of the chosen declaration **before every rewrite**. Not one
  node moves while it is asked, which is what keeps the `params` expansion and the default
  fill happening exactly once, at the pass, over the tree the source wrote.
- each of the four `err_at` sites becomes `-1` under `commit == 0`: too many arguments, an
  argument of unknown type, no overload matching, more than one matching. **-1 is never a
  guess** — it is the "not known" this oracle already answers for anything it cannot see,
  and no consumer refuses on it (`tk_check_scalar_compat`, `tk_ops_binary`, `tk_enum_*`,
  `tk_prim_binary` all state that rule in their own headers). A question reports nothing,
  because the pass reaches the same site later and reports it once.

`tk_ov_prepare(root)` is what fills the table, called at the TOP of `tk_typeof_pass` (pass
6, ahead of every consumer) and again at the top of `tk_over_pass`. It REBUILDS the
declaration rows on each call and scans them again (D48's ninth pass corrects this entry — it
scanned ONCE as first written): the unit grows between the two, since `tk_params_pass` (12)
emits the allocator, release and element store of a `T[]` row a call site is the first to
build — measured, `tests/surface_params.tk` carries 84 top-level declarations at pass 6 and
94 at pass 14 — so a table collected once would have made the pick depend on whether
anything asked earlier, and a table SCANNED once judged only the rows pass 6 could see.

**The consumers this closes**, every one measured on `9784ad80` before and after, with the
picked overload declared SECOND so the guess and the pick disagree:

| consumer | written | on `9784ad80` | now |
|---|---|---|---|
| `tk_ops_promote` (11) | `TimeSpan.FromHours(gpick(1) + 1)`, `f64 gpick(i64)` | 1h: the `1` was not promoted | 2.5h |
| `tk_ops_binary` (11) | `i64 r = 2 + rpick(1)`, `i64 rpick(i64)` | ``no operator `+` takes these operands`` | 42 |
| `tk_ops_binary`, enum arm | `bpick(1) \| DateTimeKind.Unspecified` | ``no operator `\|` takes these operands`` (a row of not-yet.md) | the enum, both declaration orders |
| `tk_ops_binary`, enum arm | `DateTimeKind k = pick(1) + 1;` | COMPILED as raw arithmetic | ``no operator `+` takes these operands`` |
| `tk_ops_unary` | `new DateTime(1, +pick(1))` | the `+` erased over an enum, then a bogus "overloaded call outside a function body" | ``no operator `+` takes these operands`` |
| `tk_ternary_pass` (9) | `c ? tpick2(2, 3) : 9` | `the two arms of ?: have different types` | 5 |
| `tk_nl_co_lower` (9) | `npick(1) ?? d`, `i64? npick(i64)` | ``?? needs a nullable on the left`` | 8 |
| `tk_prim_arg_judge` (14) | `new DateTime(7, bpick(1) \| ...)` | refused as above | the enum |
| an initializer | `DateTimeKind k = bpick(1);` | already right (`tk_check_scalar_compat` reads the same oracle) | unchanged |

**What it deletes.** `tk_ops_rejudge` (teko_ops.tk), the sixth pass's second judgement of an
operator under a deferred argument: 57 lines, gone, because pass 11 now sees the picked type
on its first visit — it refuses `pick(1) + 1` where the pick is an enum and lowers
`dpick(3) - epick(1)` to the row's own call where the pick is a primitive, at the pass where
every other operator is judged. The two fixture codes that measured it, 63 and 64, pass with
it gone. **What it does NOT delete, measured rather than argued**: the parse-time deferral.
A `new DateTime(...)` argument is lowered while the unit is still being read, where
`tk_pty_of` (teko_struct.tk) cannot ask a table that does not exist yet, so `tk_prim_arg`'s
`cty = -1` for a call, `tk_prim_arg_defer`, `tk_prim_arg_widen` and `tk_xt_move` all stay —
removing that one line makes `tests/surface_datetime_kind.tk` answer 1 instead of 42.

**The risks, and which way each was taken.** `tk_ov_scan`'s three declaration refusals — an
overloaded `main`, an overloaded `extern`, two overloads differing only by `ref`/`out` — now
fire at pass 6 instead of pass 14, with the same message at the same line; a program whose
only error is one of those sees it earlier than another error it also has. Keeping the scan
at 14 only was possible (a second flag) and was not taken: the marks it writes are exactly
what `tk_ov_find` reads, so the oracle would have had to duplicate the judgement — and
scanning at 6 ONLY, which is what this entry first shipped, left every row the later passes
add unjudged, which is what D48's ninth pass fixes. Re-entrancy is
bounded by the tree: a question descends into its own arguments and never back up. Cost is a
re-resolution per question rather than a cache — the compiler's own source declares no
overload at all (mc's core is what it is built from), so `scripts/bootstrap.sh` measures the
zero case and `FIXPOINT OK` holds.

**The limits that STAY**, both now in not-yet.md under what they really are:

- **parse time**. `tk_pty_of` types a node while the unit is incomplete, and a `switch`
  subject's temporary is written there: `switch (pick(1))` types `$t` by the first
  declaration of `pick`, and a `case` label of the picked overload's own type is then
  ``teko: no operator `==` takes these operands``. Bind the call to a local first.
- **a USER operator read by a pass that runs ahead of pass 11**: `c ? v + 1 : 9` on a class
  that declares `operator+` is `teko: the two arms of ?: have different types`, and the same
  expression on the right of `??` or behind `?.` gets that position's own message. Nothing to
  do with overloads — the ternary, `??` and `?.` are rewritten at pass 9 and `tk_ops_emit`
  has not written the call yet.

**Fixtures: 62.** `tests/surface_overload_ops.tk` (exit 42, 16 codes) is the accepted half of
the table above, in both declaration orders; the refused half is in
[diagnostics.md](docs/reference/diagnostics.md) behind `// no-run`, and the file is REFUSED
by the compiler built from `9784ad80`, at its first ternary.

**Proof**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config mc.macos.toml`
clean; **62/62** fixtures at their `expect-exit`; `--dump-ast` of the 61 that existed before
against `9784ad80` — **61 byte-identical**, the root fix and the deletion each measured on
their own; `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (62/62 under
the self-hosted `teko1`); `sh scripts/check-docs.sh` green (567 links, 383 diagnostics, 117
samples). `mc limits` verdict `ok` on both legs with every table unmoved from the sixth pass
— floor (`tests/hello.tk`) `passes` **15/30**, `syntax` 15, `alias` 18, `types` 11, `intrin`
8/16, heap 1114880, and the `tests/surface_datetime.tk` leg `syntax` 16, `alias` 21, `types`
14, heap 3875392 (against a 33554432-byte reservation). `mc pkg hash .` over the source tree
of this entry's code commits, after the eighth pass: `1a4edc3dc140c8270a2c8fa29940b8aebf1dab673298b8026453e1a336560e62`.

### D50 · Every field store is one gate, and one judgement (G-e, 2026-09-13)
*This entry is the whole of D50 as it stands. It was written in eleven passes over
[#698](https://github.com/teko-org/teko-lang/pull/698) — the crumb and ten Copilot reviews
— and every earlier wording of it, including the `tk_member_fn` push described below, the
two-oracle door the third pass removed, the silent `-1` the fourth one turned into a
refusal, the registration the fourth one gave every call alike and every intermediate proof
block, is **superseded in this entry**, which is the only current one.*

**The defect.** A field store had SIX code paths and only one of them ran the value through
`tk_field_store_val` (teko_typeof.tk): D33's widening, its narrowing refusal, D43's `null`
only in a `T?` field, D34's reference/number mismatch, Q1b's box wrap. The one that did was
`p.f = e` on a receiver the parser can type (`tk_field_use`, teko_expr.tk). The others built
the store CALL over the source's own value node:

- `this.f = e` and the implicit `f = e` inside a method or a constructor (`tk_this_assign`,
  teko_this.tk);
- `T.f = e` on a `static` field, on BOTH its roads — the direct site (`tk_static_use`) and
  its forward-referenced twin, resolved once the type it names is finally read
  (`tk_fwd_resolve_static_one`), both teko_access.tk;
- `p.items[i] = e` (`tk_array_index`, teko_struct.tk), which already called the gate;
- `xs[i] = e` on a `T[]` of heap (`tk_ha_store`, teko_heaparr.tk), which called the ROW
  check alone — no scalar verdict, no deferral, and so judged BEFORE the overload pick;
- and `h.f = e` on a receiver the parser cannot type at all, rebuilt by the pass
  (`tk_pend_field`, teko_typeof.tk), which judged and widened ON ITS OWN
  (`tk_check_compat`, `tk_num_widen`) — every rule of the store but Q1b's box.

`public H(i64 k) { this.rate = k; }` with `f64 rate` stored the PARAMETER's raw eight bytes,
read back as a double near 2.5e-323 instead of 5.0, and a `static f64` receiving an integer
did the same; neither refused a class value landing in an `i64` field; and
`void set(H h, i64 k) { h.count = k; }` with an `i64? count` stored those eight bytes as a
BOX HANDLE — a segfault, exit 139, measured.

**The fix, one sentence: every site builds its value through `tk_field_store_val`, and that
gate defers whatever no oracle can type.** No new function for the splice —
`teko_struct.tk`'s own forward declaration of it (already there for `tk_array_index`) is
what every later file calls — and no new pass: `mc limits`' `passes` row does not move.

**The door decides ONLY what the parse-time oracle types with certainty; everything else is
DEFERRED, never written raw and never refused there.** That is the root under every thread
the review found, and the one rule the whole gate now reads by. `tk_fs_vty` asks `tk_pty_of`
(teko_struct.tk) and nothing else — a literal, a cast, a node the type table already tags
(`tk_xt_ty`; a tag is not a guess) and a local whose declaration the parser has read — so
`tk_fs_defer` records the value node, the field's type and the site, and the judgement
happens once, later. The value's type is the one thing a store site may not know: a
PARAMETER is in no parse-time scope at all (`tk_slv`'s own rule, D33's "not known here"), an
implicit `f = e` is rewritten from a PASS with its right-hand side still a bare name
(`tk_this_assign` runs BEFORE `tk_this_ident` turns `rate` into the typed load), a
`T.f = e` on a type declared BELOW is resolved in `tk_fwd_pass`, one pass ahead of the scope
that would answer, and a FREE function's `H.total = k` was never inside a member body at all.

**The pass-time oracle is NOT a second answer at the door.** An earlier wording of this entry
had `tk_ty_of` as `tk_fs_vty`'s fallback and claimed it was "silent everywhere it is not
needed", on the grounds that a parse-time site has no function scope. The claim was false:
`tk_ty_of` reads the GLOBAL table for a bare name (`tk_ty_global`, teko_array.tk), and a
global is in scope in every body — so a global a parameter or a FIELD *shadows* answered for
the shadowing name, at a site with no scope at all and at the two sites that run from a pass
BEFORE the walk that resolves the name. Measured (Copilot on #698, pass 3): with a global
`f64 step` beside a class that declares `i64 step`, the implicit `rate = step;` was typed
`f64`, converted nothing and was not deferred — the store did not land at all, the field
kept what the constructor had left in it — and the mirror (`i64 cnt; cnt = step;`) was
REFUSED for a narrowing nobody wrote. One oracle at the door, one judgement later.

**The ROW check is behind the deferral too, not in front of it.** `tk_check_field_store`
reads the same parse-time oracle for a value `tk_struct_of_expr` cannot see, so an
overloaded call answered with the FIRST declaration of its name and
`Cell rc; this.rc = rick(1, 2);` was refused — `teko: a value of type i64 does not convert
to Cell?` — for a store that is a `Cell` (Copilot on #698, pass 3). `tk_field_store_val`
defers on `vty < 0` BEFORE either check runs; `tk_fs_do` is the first point with the final
type, and `tk_check_compat` there is the scalar verdicts and the row one in one call.

**Two kinds are "not known" even where the parse-time oracle answers, because a LATER PASS
decides them** (`tk_fs_vty`, the gate's own question):

- an **N_CALL to an overloaded name**. `tk_pty_of` answers `decl_ret` of the FIRST
  declaration — D49 leaves that as the parse-time limit it is — while the site is rewritten
  to the symbol its ARGUMENTS pick at pass 14. Measured: `f64 rate; this.rate = pick(1, 2);`
  with `f64 pick(i64)` declared ahead of `i64 pick(i64, i64)` wrote the picked overload's
  raw integer into the float field, and the mirror pair hid a narrowing the pick does ask
  about (`i64 n; this.n = pick(1, 2);` on an `f64` pick compiled). It is D48's own rule for a
  primitive row's argument (`tk_prim_arg`'s `cty = -1` for a call), one slot over.
- an **N_BINARY or N_UNARY over a class that declares the operator**, which is not a call
  until `tk_ops_pass` (11) writes one. `i64 sum; this.sum = g + 1;` with
  `operator+(GrandBase, i64) -> i64` was refused — `teko: a value of type GrandBase does not
  convert to i64` — for a store that is an integer.

**ONE point of judgement: the end of `tk_over_pass`** (pass 14), beside D48's
`tk_prim_arg_judge` and by the same reasoning. `tk_field_store_judge` walks with
`tk_ty_pass_walk`, so the scope a parameter is read under is alive; by then the tree is
resolved (pass 6), every user operator is the call it stands for (11) and every overload pick
is committed (just above, in the same pass). `tk_fs_do` asks `tk_ty_of`, judges with
`tk_check_compat` and writes the conversion IN PLACE (`tk_nd_moved`, teko_prim.tk), because
the value is a link of the store call's own argument list and nothing there knows which one.
The end of `tk_typeof_pass`, where the judgement first ran, has only the first of those four
halves and a half: it was what left the pick and the operator wrong.

**A value the PASS cannot type either is REFUSED**, `teko: the type of this value is not
known here` — the invariant the whole gate rests on: a store is judged, or it is refused; it
is never written raw. The third pass left that `-1` returning in silence, reading D226's
"not known is never a verdict" as a licence to emit the store unchecked, and silence is
exactly what an unanswered value needs protection from: what reaches the machine then is its
own eight bytes in a slot of another type. D226's rule holds where it was written — an
ORACLE answers -1 rather than guessing — and this is the one point that turns the unanswered
question into a refusal, the very sentence D48 gives a primitive row's deferred argument, one
slot over.

**The type of an INDIRECT call is the compiler's own to record, and a widening cast never
lands on one.** A virtual call and an interface call are `callp`, which names no callee: the
core types the node `TY_I64` by itself, so `tk_callp_ret` (teko_array.tk) writes a cast only
for a float and for a narrow integer, and `tk_emit_call`/`tk_iface_call`/`tk_iface_prop_use`
(teko_expr.tk) registered the return in `tk_xt` only when it was a ROW. A wide scalar return
had no type for anyone to read: `f64 rate; this.rate = b.M();` on an `i64 M()` reached
`tk_fs_do` with `tk_ty_of` answering -1 (Copilot on #698, pass 4). Two halves, both of them
the same defect:

- the INDIRECT sites — `tk_emit_call`'s vtable branch, `tk_iface_call`, `tk_iface_prop_use` —
  register the DECLARED return type, the row when the type has one and the type id in every
  case, which is the registration the pass-time road already makes for the very same call
  (`tk_pend_do`, teko_typeof.tk) and the one `tk_field_use` makes for a field load. A `void`
  return is registered too, as `TY_VOID` — the eleventh pass below is what made it so, and
  what the twelfth turned into a refusal on both roads: the wording this paragraph carried
  before, "a `void` return registers nothing", was the fourth pass's own and stopped being
  true the moment `void` became an answer the store gate has to give a verdict on
  (Copilot on #698, pass 12). A DIRECT call registers its ROW
  alone, exactly as it always did: it names its callee, `tk_ty_of` reads the declared return
  off that symbol, and a scalar row there answers nothing the oracle did not already have
  while spending one of `TK_MAXXT`'s rows per call in the unit — 256 rows then, **4096** since
  the sixth pass below raised the ceiling. The fourth pass registered every call alike, and a
  body with enough of them began refusing `teko: too many expressions whose type is known`
  where it used to compile: measured by bisection on one body of direct calls, **253** calls
  was the ceiling it left, against **2000** before the crumb and 2000 again after this pass
  (Copilot on #698, pass 5). The worst fixture is unmoved by that narrowing —
  `surface_nullable_ops` needs **239** rows of the 256 that were the ceiling then (**4096**
  today), where the crumb found 238 — since what it registers is boxes and indirect returns,
  not direct calls;
- a cast written DIRECTLY over a `callp` is how mc's core is TOLD what an indirect call
  returns (`tk_callp_ret`'s own contract, mc src/gen_resolve.mc), so the widening cast
  `tk_num_widen` writes was read as that declaration instead of as a conversion — the core
  took the call for one that returns `f64`, moved `d0` into the field and converted nothing.
  Measured at the surface, mc 0.15.23: `f64 x = (f64)(b.M());` on an `i64 M()` read garbage,
  `(f64)((i64)(b.M()))` read 5.0. `tk_num_widen` (teko_typeof.tk) puts the integer's own
  declaring cast in between, and the widening converts THAT node. It is not an mc defect: a
  cast on a `callp` is mc's own way of declaring the return type, and teko is what must not
  spend it on a conversion.

The same "a node this module built carries its type" closes the element LOAD: `tk_ha_load`
(teko_heaparr.tk) registered a row and nothing else, so `xs[0] += 100;` — the compound the
element store is built from — reached the judge as a binary over a load no oracle could type.
It registers the element type like a field load does.

**A registration follows the VALUE into the node the TREE holds.** An index on a GLOBAL array
is a bare `N_INDEX` while the body is parsed — neither array table is filled yet — and a pass
replaces it in place afterwards: `tk_hg_rewrite_index` (teko_array.tk) builds the load with
`tk_ha_load` and copies it into the tree's own node with `node_assign`, so the tag the
builder had just written stayed on a node nobody reads. `f64 rate; this.rate = g[0];` on a
global `i64[] g` reached the judge with no type at all and was REFUSED, `teko: the type of
this value is not known here` — a false refusal, for a value the compiler itself had just
typed (Copilot on #698, pass 5). The registration is MOVED (`tk_xt_move`, teko_struct.tk),
which is also one row fewer than the row re-add it replaces; it is the same move `tk_fs_do`
and `tk_pend_do` already make for a node they rewrite. The FIXED-array rewrite beside it
(`tk_array_maybe_rewrite_index`) tagged nothing at all and was refused the same way, so it
registers the element type itself: `tk_arr_load` cannot do it for both, being shared with a
`ref` parameter's read and a lambda capture's, which are elements of nothing. Measured on
`0f96fdf2`/`21ca62ae` and after: `this.rate = g[0]` with an `i64[] g` global refused, then
**5.0**; `i64 n; this.n = gf[0]` on an `f64[]` global refused as "not known", then refused as
the NARROWING it is; `Cell[] gc; this.c = gc[0]` and a fixed `i64 gx[2]` the same. Before the
crumb all four compiled and wrote the raw eight bytes.

**The loads the PARSER itself resolves carry their type too.** A FIXED array's element is
lowered where it is read — `a[0]` on a local (`tk_arr_index_of`, teko_array.tk) and
`this.items[0]` on an inline array field (`tk_array_index`, teko_struct.tk) — so no pass ever
revisits either one and the tag has to be written at the site. Neither wrote one for a SCALAR
element: `tk_arr_index_of` tagged nothing at all and `tk_array_index` registered a ROW alone,
the same half-registration the indirect call had. Measured on `6a30d158`: with `f64 rate`,
`i64 a[2]; this.rate = a[0];` and `this.rate = this.items[0];` on an `i64 items[2]` were both
REFUSED, `teko: the type of this value is not known here`; after, both read back **6.0** and
**4.0**, and the mirrors (`i64 n; this.n = fa[0]` on an `f64 fa[2]`, and the inline twin)
refuse the NARROWING they are instead of the type they have. Each writes
`tk_xt_put(r, tk_struct_by_ty(ety), ety, ...)` — the row when the element has one and the
type id in every case — which is exactly `tk_ha_load`'s own registration one slot over, and
what `tk_arr_load` may not do for either: a `ref` parameter's read and a lambda capture's go
through it too, and they are elements of nothing.

**The type table is METADATA FOR THE UNIT, and its ceiling has to fit a real program.**
`TK_MAXXT` (teko_struct.tk) was **256** rows and `TK_MAXFS` (teko_typeof.tk) **512**, figures
inherited from a table that only a struct load wrote to. This crumb made the marking UNIFORM
— every load of a field, of an array element, of a `T[]` and of a fixed array, every box and
every non-`void` indirect return spends one row, and a row is never released — so 257 valid
scalar reads in one unit met `teko: too many expressions whose type is known` where the
program was correct: measured, a body of **300** field stores from a global `i64[]` was
refused at the 260th on `6a30d158`. Both become **4096**: 5 columns × 4096 × 8 B = 160 KB
each, 320 KB in all, against a 33554432-byte reservation whose floor leg (`tests/hello.tk`)
moves from **467824** to **1114992** bytes used, `mc limits` verdict `ok` with every other
table unmoved. Measured with a throwaway counter printed at the last pass, the busiest
fixture spends **239** of `TK_MAXXT` (`surface_nullable_ops`) and **34** of `TK_MAXFS`
(`surface_field_store`), so the new ceiling is sixteen times the worst case either table has
ever seen; the boundary is proved from both sides — 300 reads compile, 4200 are refused with
the table's own message, the fixed-array road on `TK_MAXXT` and the global one on `TK_MAXFS`.

**One rule, not a policy per shape.** A narrower marking was weighed and REJECTED: an
indirect return of `f64` or of a sub-word integer already carries `tk_callp_ret`'s declaring
cast, and `tk_ty_of` could read the type off that cast instead of off a row. It saves rows
only for calls whose return is one of those two shapes, it is a second answer for a question
that has one, and it makes "is this value marked?" depend on the value's own type — the
condition every defect in this entry came from. The uniform rule is the cheap one at 4096
rows: what the parser knows and no pass recovers is MARKED, everywhere, and the capacity is
what pays for it.

**A direct call with ONE declaration is still deferred, on purpose.** `tk_fs_vty` answers -1
for every `N_CALL`, and `tk_pty_of` could type the single-declaration case off `decl_ret`
with no ambiguity at all. It would be wrong for the same reason the overloaded case is: the
declaration count is a fact about the file READ SO FAR, and a second declaration of the same
name BELOW the store turns the answer the door already gave into the wrong one — D49's own
limit, and the reason a call is judged after the pick is committed (pass 14) rather than
where it is written. The deferral costs one row of `TK_MAXFS`, which is what the paragraph
above just paid for.

**An ELEMENT of a `T[]` of heap goes through the one door.** `tk_ha_store` called
`tk_check_field_store` directly, which is the ROW half judged with the PARSE-TIME oracle and
nothing else: `Cell[] xs; xs[0] = rick(1, 2);` was refused — `teko: a value of type i64 does
not convert to Cell` — by the FIRST declaration of an overloaded name, while `i64[] a;
a[0] = 1.5;` compiled and wrote the double's raw bits and `f64[] f; f[0] = 1;` wrote the
integer's, local and global alike (Copilot on #698, pass 4, and the scout's own measurement).
It calls `tk_field_store_val` with the element type, so the deferral, the two scalar
verdicts, the row one, Q1a's `null` clause and Q1b's box are the field's, exactly.

**The site a deferred RECEIVER reports is its own.** `tk_pend_field` (teko_typeof.tk)
resolves a value that is itself a deferred member first (`tk_pend_do`), and that call leaves
`tk_line`/`tk_file` on the row IT resolved — so `h.n = k.d;` written over two lines refused
the store at the inner access's line. The table's own row (`pd_line_at`/`pd_file_at`) is
what the gate is given.

**The reference predicate is the full one.** `tk_check_field_store` (teko_struct.tk), with
the value's type in hand, refused an integer and a float only — so an `enum`, a primitive
with members, a raw `uptr` and an unrelated class each crossed into a field of reference type
(measured: `b.c = raw;` and `b.c = k;` on a `Color k` both compiled). It asks
`tk_check_compat`, the judge an assignment, an initializer, an argument and a `return` are
given, Q1b's box exemption inside it.

**`tk_member_fn` pushes NOTHING onto `tk_slv`.** An intermediate version of this crumb had it
push every parameter's type there for the length of the body; the deferral covers every site,
the push covered only the body being parsed, and `tk_slv_add`'s own rule (D33) is that a
parameter answers -1 there — one table for two questions is what that rule was written
against. Deletion over addition: there is no such push, and the 62 pre-existing dumps are
byte-identical either way.

**A FIXED array's element goes through the one door as well.** The fourth pass sent the
element of a `T[]` of heap through `tk_field_store_val`; the element of a fixed array —
`a[i] = e` on a local, `g[i] = e` on a global, and the compound `a[i] += e` behind both —
was still built as the raw `stW(addr, v)` `tk_arr_store` emits, on all three of its roads
(`tk_arr_index_of`, `tk_arr_compound` and `tk_array_resolve_write`, teko_array.tk).
Measured on `5b78af5c` (the seventh head, this paragraph's own "before"): `f64 gf[2];
gf[0] = 1;` wrote the integer's own eight bytes and read back 5e-324 instead of 1.0, local
and global alike, `i64 gi[2]; gi[0] = 1.5;` wrote the double's and was not refused, a class
value landed in an `i64` element (`gi2[0] = c;` compiled) and so did a `null` in an `f64`
one; after, the two widen to 1.0, the two narrowings are refused
(`teko: a value of type f64 does not convert to i64`), and the reference and the `null`
meet `tk_check_compat`'s own wording. The door is a function of teko_array.tk's own
(`tk_arr_elem_store`) rather than `tk_arr_store` itself, for the very reason `tk_arr_load`
tags no type: a `ref` parameter's write (teko_ref.tk) and a lambda capture's
(teko_deleg.tk) go through `tk_arr_store` too, and they are elements of nothing. The
COMPOUND needed the other half of the same rule — its own load was built by `tk_arr_load`,
which tags nothing, so `gf[0] += 1;` summed a float no oracle could type and reached the
judge as an untypable binary; it registers the element type there exactly as `tk_ha_load`
does, and reads 2.0. Q1b's box has no fixed-array shape to fire on: an array of `i64?`, of
a class or of any other row is refused at its declaration, `teko: an array of objects is
not taught yet; use a field array or wait for T[]`, so a fixed element is always a scalar
and the door's row half is answered before it is asked.

**A subtree the parser PARKED is not in the tree a pass walks.** `tk_array_walk_reads`
(teko_array.tk) rewrites every `N_INDEX` on a global array it finds IN THE TREE, and three
subtrees are not there when it runs: the value and the index a deferred global write holds
(`gd_val`/`gd_idx`, spliced back only by `tk_array_resolve_write`, after the walk) and the
value a deferred member access holds (`pd_arg`, teko_typeof.tk, rebuilt at pass 6). An
index inside one of them survived raw and was refused by the check that finds an index
nothing lowered, ``teko: `[` needs an array: src`` (`tk_pm_check_index`, teko_params.tk).
Measured on `5b78af5c`: `dst[0] = src[0];` and `dst[src[0]] = 9;` between two global
`i64[]`s, the same pair between two fixed globals, and `void set(H h) { h.rate = gl[0]; }`
— four ordinary programs refused for a read the compiler knows how to build. The two
parked by a WRITE are walked in `tk_array_pass` itself, before the writes resolve
(`tk_gd_walk_reads`), and the one parked by an ACCESS is walked where it is taken out of
the table (`tk_pend_field`), each of them nested-safe: the walk is the same recursive one,
so `h.rate = gl[0] + gx[0];` rewrites both. It is not only a refusal that is fixed: the
load reaches the field-store door TYPED, which is what the judge then reads.

**EVERY subtree a deferred access parked goes through the same walk, at the one point
every road out of the table passes.** The eighth pass walked ONE of them — the value of a
deferred store (`pd_arg`, from inside `tk_pend_field`) — and the table parks three:
`pd_recv` (the receiver), `pd_na` (the receiver's own INDEX for the two `p.items[i]`
forms, and a call's argument COUNT for every other form, an integer that is no node at
all) and `pd_arg` (a store's value AND a call's argument list). `tk_pend_do`'s own
receiver line rewrote the top node of `pd_recv` alone (`tk_array_maybe_rewrite_index`),
which is not a walk: a NESTED index under it was left raw. Measured on `a9e0b0d7`, three
ordinary programs refused for a read the compiler knows how to build, ``teko: `[` needs an
array: gl``: `void set(H h) { h.items[gl[1]] = 1; }` (the receiver's index, Copilot on
#698, pass 9), `h.rate = 1 + h.mul(gl[1]);` — a member call NESTED in the value, whose own
arguments the inner row parks and which is resolved through this very function — and
`gc[gl[1] - 1].v`, the index inside the receiver's index. `tk_pend_park_reads`
(teko_typeof.tk) walks all three in `tk_pend_do`, before the receiver's type is asked for,
and `pd_na` is walked only in the two forms where it is a node; the per-emitter call in
`tk_pend_field` is deleted, one walk taking its place. A throwaway counter over every
rewrite of a global array's index, split by whether `tk_array_pass` had already finished,
says how much of the work only this walk reaches: `surface_field_store` rewrites **17**
indexes, **6** of them from a parked subtree — the two of `h.rate = gl[0] + gx[0];` and the
four of helper 23 — and `surface_array_global` rewrites 10 with 1 parked, the receiver of
`cs[i].area()` the eighth pass's own line already had.

**The rewrite of a global array's reads may not match by NAME ALONE.** `g[i]` on a global
array is a bare `N_INDEX` while the body is parsed and a pass replaces it afterwards
(`tk_array_maybe_rewrite_index`), keyed by the base's name against the two global tables —
and a LOCAL of another type that shadows that name was rewritten just the same, while the
base identifier under the rewrite still resolved lexically to the local. Measured on
`a9e0b0d7`: with a global `i64[] src` and a local `i64 src = 3;`, `dst[0] = src[0];`
compiled and ran off the local's own value, exit **139** — an invalid program that neither
compiled correctly nor was refused; the same through a parked subtree
(`h.rate = src[0];`), and the same on the WRITE side, which never asks the question at all
(`tk_arr_defer_write` parks any `N_IDENT[...] =` for the pass to claim). The root is
LEXICAL and is known at PARSE TIME and nowhere later, so the refusal is written there:
`tk_bracket` (teko_params.tk) already answers every array shape a local can have —
`tk_ax_find`, `tk_arr_find`, `tk_hp_find`, `tk_struct_of_expr` — so a base that is still a
name the parser has seen DECLARED as a local of this scope (`tk_slv_find`, the parser's own
scoped lookup) indexes nothing, and it is refused with the wording `tk_pm_check_index`
gives an index no road lowered, ``teko: `[` needs an array: src``. Measured after: the
shadowing program is refused at its own line, the same program WITHOUT the shadow reads the
global as it always did (exit 77 over both kinds of global array), and a shadow that ends at
a `}` leaves the global readable again below it (exit 70). A PARAMETER that shadows a global
array is the same defect and is NOT closed here: a parameter is in no parse-time scope at
all (`tk_slv_add`'s own rule, D33, and the reason this entry rejects pushing one there),
and the only table that could answer — K3's `tk_hp` — is not reset for a free function with
no parameters at all, so widening it would refuse a correct program. It is
[not-yet.md](docs/reference/not-yet.md)'s own row, measured (exit 139), and a crumb of its
own.

**The element store REPORTS the store it built.** `tk_arr_elem_store` (teko_array.tk), the
door the eighth pass gave a fixed array's element, returned `tk_arr_store`'s raw node — no
`tk_os_mark` — while the five sibling doors all mark, and a store the reclaim pass never
heard of keeps the raw `stW` where `rt_store_own` belongs (teko_rc.tk). The global road
needed the other half: `tk_array_resolve_write` copies the built node into the tree's own
placeholder (`node_assign`), so the mark has to be re-made under the identity the reclaim
walks — the re-mark `tk_hg_resolve_write` beside it already makes for a `T[]` of heap, asked
here as `tk_os_has(r)` so the two halves cannot drift apart. No fixed element is counted
TODAY and the refusal that makes it so is three files away: `Cell cs[2];`, `i64? xs[2];` and
`P ps[2];` are each refused at the DECLARATION, ``teko: an array of objects is not taught
yet; use a field array or wait for T[]`` (measured, all three, on this head), and an `enum`
element is the exempt one `tk_is_counted` answers "no" for — so the mark fires on no program
that compiles today and the 62 dumps are byte-identical with and without it. What it buys is
that "every element store is reported" is a property of the DOOR rather than a coincidence
of that refusal.

**The site a store reports is its own, at every one of the six.** `tk_field_store_val`
builds nodes, and both halves of what it can build move the compiler's current position:
`tk_num_widen` and `tk_nl_wrap` (teko_typeof.tk, teko_null.tk) set `tk_line`/`tk_file` to
the VALUE's own node before writing the conversion. `tk_field_use` (teko_expr.tk) has
always restored the store's own site after the call; four of the other five did not, so
the store node they built afterwards carried the value's line. Measured with a throwaway
`err_at` on the store node, a store written over two lines: the implicit `r =`/`3;`
reported line 7 and now reports 6 (`tk_this_assign`, teko_this.tk), the static
`S.total =`/`3;` reported 8 and now reports 7 (`tk_static_use`), the same store on the
FORWARD road reported 5 and now reports 4 (`tk_fwd_resolve_static_one`, both
teko_access.tk), and the deferred receiver's `h.r =`/`3;` reported 6 and now reports 5
(`tk_pend_field`, teko_typeof.tk, whose row already gave the REFUSAL its own site in the
third pass -- the store node it builds is the other half of that same rule).
`tk_ha_store` (teko_heaparr.tk) and the fixed element's own door restore it for the same
reason.

**The row check asks `null` nothing.** `tk_check_field_store` (teko_struct.tk) kept a copy
of Q1a's rule — `null` lands only in a slot declared `T?` — under a comment that called
itself "the ELEMENT store's own road to the rule", on the grounds that the element store
reached no other check. Since the fourth pass it enters by the same door as every other
site, and that door's scalar half (`tk_check_scalar_compat`) gives the very same verdict
over the very same node one line earlier: the copy was dead code under a comment that
contradicted the shared gate. It is deleted, and what is left is the rule the row question
really has — `null` is silent on it, a reference fits any row — spelled exactly as
`tk_check_compat` spells it. No verdict moves: the 63 fixtures and the `null` probes are
unchanged either way.

**A registration follows the value into the node the tree holds, at the FORWARD static's
own road too.** `tk_hg_rewrite_index` was one of two functions that build a node and copy it
into the tree with `node_assign`; the other is `tk_fwd_resolve_static_one` (teko_access.tk),
which resolves every `T.x` on a type declared BELOW its use — the LOAD, the static call and
the static property alike — and tagged the node it BUILT. `f64 rate; this.rate = H2.total;`
with `H2` declared below its reader reached the judge with no type at all and was REFUSED,
`teko: the type of this value is not known here`, a false refusal over a value this very
function had just typed (Copilot on #698, pass 11). The registration is MOVED under the
tree's identity (`tk_xt_move`), the twin of the `tk_os_has(r)` re-mark one line above it and
the same move `tk_fs_do` and `tk_pend_do` already make. Measured: the store above was
refused and reads back **5.0**; its mirror (`i64 n; this.n = H2.frate;` on a `static f64`)
was refused as "not known" and is refused as the NARROWING it is.

**A FIELD of the class being parsed shadows a global array exactly as a local does.** The
ninth pass closed the LOCAL half of "the rewrite of a global array's reads may not match by
NAME ALONE"; the same defect lives one scope out. With a global `i64[] src` beside an
`i64 src` FIELD, `src[0]` in a method was rewritten into a load of the GLOBAL by
`tk_array_pass`, while the base identifier under that load was rewritten by `tk_this_ident`
into the FIELD's own load — the array pass runs first (teko.tk's own order) — so the field's
eight bytes were read as the array's handle: exit **139**, an invalid program that neither
compiled correctly nor was refused (Copilot on #698, pass 11). The binding is LEXICAL and
known at PARSE time, so the answer is written where the local's already is, in `tk_bracket`
(teko_params.tk): inside a member body (`tk_body_class`, teko_this.tk), a bare name the class
declares is that FIELD — C#'s own rule, and `tk_this_field`'s. A field that IS a `T[]` of
heap answers there through `tk_ha_index`, exactly as a `T[]` PARAMETER does four lines above,
so it no longer reads the global by accident and the implicit `xs[0]` has a road even when no
global of that name exists at all, where it used to be refused; everything else — a scalar,
an object, a delegate, and an INLINE array field, which is reached through `this.` and
nowhere else — takes the refusal the local half gives, ``teko: `[` needs an array: src``.
Measured after: the shadowing program is refused at its own line on the READ, on the WRITE,
over both kinds of global array and through a parked subtree; the `T[]` field reads the field
with the global beside it (**7**, as it did) and without it (**7**, where it was refused);
the same program with no field reads the global (**99**), unmoved. A field declared BELOW the
method that reads it is NOT closed here and is [not-yet.md](docs/reference/not-yet.md)'s own
row, measured (exit 139): the parser has not read that field yet, which is the one fact this
door rests on, and the tables that could answer later are three walks and a parked-row column
away — the same shape, and the same ruling, the parameter shadow already has.

**A `void` call is no value for a field to take.** An INDIRECT call is a `callp`, which names
no callee, so mc's core types the node `TY_I64` by itself; the three sites that record the
declared return (`tk_emit_call`'s vtable branch, `tk_iface_call`, `tk_iface_prop_use`,
teko_expr.tk) skipped `void` on the grounds that it has "nothing to be asked about" — the
fourth pass's own wording, above — and that left the core's `TY_I64` standing as the answer
the oracle gives. `i64 n; this.n = b.M();` on a VIRTUAL `void M()` compiled and stored
whatever the call had left in the return register (measured: exit **64**), and the INTERFACE
twin the same. The DIRECT road never had that half — the core refuses `value of type void`
where it can see one, the very message a local initializer gets — but the judge did not
refuse it either: `TY_VOID` names no row of the type table, is not a float and is not one of
`tk_is_int_ty`'s words, so `tk_check_compat` was silent for a field of NUMBER type and only a
field of REFERENCE type got a verdict. The declared return is registered, `void` included —
it spends the row a wide scalar return already spends and names no row, exactly as `i64` does
not — and `tk_fs_do` turns that answer into the refusal, in the wording every mismatched
value already gets: `teko: a value of type void does not convert to i64`. No new `teko:`
string. Measured over the six roads of the gate: the virtual and the interface store compiled
(exit 64 and exit 0, garbage) and are refused; the direct call through `this.`, through a
deferred receiver, through a `static` field, into an `i64?` box and into an element of a
`T[]` now carry the same sentence, where four of them used to fall to mc's own wording. What
this does NOT close is the same `void` reaching another slot: `i64 x = b.M();` on a virtual
`void M()` still compiles (measured, exit 32) — a local initializer is judged nowhere near
this gate, and the registration this pass adds is what a crumb of its own would read.

**Coverage.** `tests/surface_field_store.tk` (`expect-exit: 42`), twenty-six helpers: the
constructor's explicit `this.rate = k` and the implicit `rate = k`; a method's
`this.rate = this.rate + 1`; a `static f64` written and read back; a `Cell?` field boxing
`null` and a live reference; an `enum` field; `this.rate = k + 1` (an N_BINARY); `rate =
step` (implicit on both sides); a forward static (`H2` declared BELOW its writer) written
from a member's own parameter; a FREE function's parameter into a static field and into an
instance one; an `i64?` field boxed from a parameter; a receiver that is a PARAMETER, with
both the `f64` widening and the `i64?` box; `this.rate = pick(1, 2)` with the picked overload
declared SECOND, in BOTH orders; `this.sum = g + 1` over a user operator; the two SHADOWED
roads in both directions — a global `f64 k` over every `i64 k` parameter and a global
`f64 step` over the `i64 step` field, each written into an `f64` field and into an `i64` one;
`this.rc = rick(1, 2)` on a `Cell?` field whose pick returns a ROW while the first
declaration returns `i64`; a VIRTUAL `i64 M()` and an INTERFACE `i64 M()` into an `f64`
field, on both roads — a receiver the parser types and a receiver only the pass does — with
the float return beside them, which never had the bug; an ELEMENT of a `T[]` of heap in its
four shapes (the pick that returns a row, the compound over a load nobody typed, the
widening an `f64[]` writes, the box an `i64?[]` wraps) with `rt_live()` back to its floor
inside the helper; an element of a GLOBAL array in a field store, both kinds — the `T[]` of
heap and the fixed one — into an `f64` field and into a `Cell?` one; an element of a LOCAL
fixed array and an element of an INLINE array field, the two loads the parser resolves on the
spot, each into an `f64` field; and `rt_live()` back to
its floor at the end, that floor being the four objects the three global `T[]`s ROOT — the
arrays `gl`, `gc` and `gdst` themselves and the one `Cell` that `gc` holds, `gx` and `gf`
being FIXED arrays the counter never sees (a global
array is never released, `surface_array_global.tk`'s own header; the earlier "three objects,
two global arrays" here was the count of a head that had one `T[]` fewer — Copilot on #698,
pass 12); the element of a FIXED array as the store TARGET,
global and local, the widening (`f64 gf[2]; gf[0] = 1;`), the compound over its own load
(`gf[0] += 1;`) and an element whose type already matched, which the door has to leave
exactly as it found it; a global element as the VALUE and as the INDEX of another global's
write, on both kinds of global array; the same parked subtree on the deferred
RECEIVER's road (`h.rate = gl[0] + gx[0];` through a parameter); the forward static as the VALUE of a store
(`this.rate = W3.tally;` on a class declared ABOVE it, whose `i64` widens into the `f64`
field); the implicit index on a `T[]` FIELD (`xs[0] = 8;`, `xs[0] += 1;` and
`this.rate = xs[0];`) with this file's own global arrays standing beside it, the object and
its array released inside the helper so the floor below is unmoved; and the three OTHER
subtrees that same access parks (helper 23) — the receiver's own index
(`h.items[gl[1]] = 7;`), the arguments of a member call NESTED in the value
(`h.rate = 1 + h.mul(gl[1]);`) and an index nested inside the receiver's index
(`gc[gl[1] - 1].v`), none of which allocates, so the floor below is unmoved. The floor `rt_live()`
returns to is **four** objects with those helpers in: three global arrays and the one
`Cell` one of them holds. The file exits **71** on
`c7df7787` (the first head of the PR); on `5ecae153` it is REFUSED at the operator store,
exits **139** with that helper neutralised (the box segfault) and **131** with the box
neutralised too (the pick); on `bb05ed0e` (the third head) it is REFUSED at the row pick,
and exits **81** — the field-to-field store the global shadows — with the pick helper and
the two mirror stores neutralised. On `0f96fdf2` (the fourth head, this entry's own
"before") it is REFUSED at `cs[0] = rick(1, 2)`, the element store judged by the first
declaration; with that one neutralised it exits **171**, the virtual `i64 M()` whose raw
bits the `f64` field kept; with the virtual helper neutralised too it exits **181**,
`f64[] fs; fs[0] = 1;` writing the integer's. On `21ca62ae` (the fifth head, and this
paragraph's own "before") it is REFUSED at `this.rate = gl[0]`, the global element no
registration reached. On `6a30d158` (the sixth head, and this paragraph's own "before") it is
REFUSED at `this.rate = a[0]`, the local fixed array's element no registration reached. On
`5b78af5c` (the seventh head, and the eighth pass's own "before") it is REFUSED where the
deferred receiver reads a global element, ``teko: `[` needs an array: gl`` — the first of
the three parked subtrees it reaches. On `a9e0b0d7` (the eighth head, and the ninth pass's
own "before") it is REFUSED at `h.items[gl[1]] = 7;`, the deferred receiver's own INDEX;
with that helper neutralised, at the member call NESTED in the value; with that one
neutralised too, at the index nested inside the receiver's index — the three subtrees of
helper 23, in the order the file writes them. On `a0b9388c` (the tenth head, and this pass's
own "before") it is REFUSED at `xs[0] = 8;`, the implicit index on a `T[]` FIELD
(`teko: not a known array: xs`, helper 25); with that helper neutralised, at
`this.rate = W3.tally;`, the forward static's LOAD no registration reached (helper 24) —
after which it exits **42**, every other verdict of the file unmoved. That every verdict the earlier passes settled is
unmoved is what the 62 byte-identical dumps say, not this file's own exit.

Compound assignment on a field (`this.rate += 2;`) is not taught: the `+=` sugar takes a bare
NAME on its left, ``the rule expected a name on the left``, a pre-existing and unrelated gap,
so helper 3 writes the same shape by hand.

The refusals — narrowing through `this.`, `null` in a non-`T?` field, an `enum` field taking
a bare integer, a reference/number mismatch through a `static` field and through a parameter
receiver, the narrowing a PICK asks for, the name that SHADOWS a global array (a local's and
a field's alike) and the `void` call a field cannot take — are `// no-run` samples in
[types.md](docs/reference/types.md) and [diagnostics.md](docs/reference/diagnostics.md),
reusing the wording every mismatched value already gets. Two `teko: ...` strings are new in
the whole crumb: the deferral table's own ceiling, `teko: too many field stores of unknown
type` (4096), and the judgement's own refusal, `teko: the type of this value is not known
here`, both documented with the judgement itself; the element store's refusals reuse the
wording every mismatched value already gets.

**Proof**, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config mc.macos.toml`
clean; **63/63** fixtures at their `expect-exit`; `--dump-ast` of the **62** fixtures that
predate the crumb, against `da33ebd4` — **62 byte-identical**, re-proved on the ELEVENTH
head against a compiler built from `da33ebd4` itself, since every site this crumb touches
only changes a program that was previously silently WRONG or wrongly refused;
`sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (63/63 under the
self-hosted `teko1`); `sh scripts/check-docs.sh` green (567 links, 387 diagnostics, 122
samples on the eleventh head — 121 on the ninth, the `void` store being the sample this pass
added and the shadowed index the one the ninth did; the seventh pass's own wording said 570
links, over what the script counts on that head and on this one alike); `mc limits . --config mc.macos.toml` verdict `ok`, every table of elements unmoved
— `passes` 15/30, `syntax` 15, `infix` 24, `alias` 18, `types` 11, `intrin` 8/16 — no new
pass, no new intrinsic (D2, D21); the floor leg's `heap` is the one figure that moves,
**467824 → 1114992** bytes used against a 33554432-byte reservation (measured in a clean
`build/`; the floor leg's heap figure follows the state of `build/` and is not a gate — an
independent run over a used `build/` reads 721776 on both sides), which is the two tables
raised to 4096. `mc pkg hash .` over the source tree of this entry's code commits, after the eleventh pass:
`d054f989225ce29724ca6bfa3989a5b23deb1c55956f7d9544e8ff5d21b501f1`.

**What the element's own type bought for free.** With every array element load carrying
its type (`tk_ha_load`, `tk_arr_index_of`, `tk_array_index`, the fixed-global rewrite), the
oracle now types an array element as an OPERAND too: `xs[0] + xs[1]` over a `TimeSpan[]`
(and over a fixed `TimeSpan fx[2]`) lowers through the row's own `tk_ts_add`, overflow check
included, where it used to fall to the core's raw `+` — measured on this head, exit 42, five
`tk_ts_add` calls in the dump of a probe mixing heap and fixed elements with a local. The
`not-yet.md` row that documented the gap ("an array element as an operand") is removed.

A throwaway instrumentation after the judgement — every row of the deferral table asserted
`done` — proved that no deferred store survives it, over the 63 fixtures and the whole
bootstrap; re-run on every later pass, where a row the judge cannot type is a refusal rather
than a silent store, it also proves that no store is ACCEPTED with `-1`: every fixture
compiles and none of them trips the refusal. The `TK_MAXXT` and `TK_MAXFS` figures quoted
above are a throwaway counter printed at the LAST pass, `tk_rc_pass`, on both of its exits —
the two tables are per unit and never reset, so their final value is the unit's cost — run
over all 63 fixtures, and re-run on the eleventh head: `TK_MAXXT`'s worst is **239**, the
same fixture and the same figure the fifth pass measured (a `void` indirect return spends a
row, and the busiest fixture writes none), and `TK_MAXFS`' worst is **34**, up from 28 with
the two helpers this pass added to `surface_field_store`; the 253-call ceiling of the fifth pass, and the 2000 beside it, are
bisections, the constant lowered and every fixture recompiled until one trips. Probe matrices
outside `tests/` (not committed) measured each defect above before its fix and its absence
after, and the two capacity boundaries from both sides: 300 scalar reads compile, 4200 are
refused with the tripped table's own message.

**The twelfth pass: `void` at the DOOR, and the inline array field's own sentence.** Two
things the eleventh pass left half-done, both measured on its own head `fd05b9b8`:

- **`TY_VOID` as a value is a refusal on EVERY road, not only the deferred one.** The
  eleventh pass registered the declared return of the three indirect sites, `void` included,
  so that `tk_fs_do` could refuse it — but a receiver the PARSER types registers that answer
  at PARSE time, and `tk_field_store_val` reads a registered type as one it KNOWS and never
  defers. `tk_check_scalar_compat` has no clause for `TY_VOID` (it names no row, is not a
  float and is not one of `tk_is_int_ty`'s words), so the door took it: `i64 n; v.n = b.M();`
  on a VIRTUAL `void M()` through a LOCAL receiver compiled and exited **32**, the INTERFACE
  twin the same, and `f64[] xs; xs[0] = b.M();` — the element store, the same door — took it
  too, exit **0** with the return register's leftovers in the element. `tk_fs_void_check`
  (teko_typeof.tk) is one function called from BOTH doors, the parse-time store and the
  judge, and the judge's own inline clause is now that call: all four roads refuse with the
  same sentence, `teko: a value of type void does not convert to i64`, and a delegate FIELD
  call, which used to answer the judge's *the type of this value is not known here* from the
  wrong line, now says it too. No new `teko:` string.
- **An INLINE array field keeps the sentence written for it.** The eleventh pass's guard for
  a bare name the class declares (`tk_bracket`, teko_params.tk) refused everything that is
  not a `T[]` with its generic ``teko: `[` needs an array``, and an inline array field is
  exactly such a name: `items[0]` inside a method got that wording where `da33ebd4` gives
  ``teko: an array field is reached through `this.`: items``
  (docs/reference/diagnostics.md), the one the pass used to give when the bare name still
  reached `tk_this_ident`. The specific road (`tk_this_reject_array`) is taken first now, and
  the generic one is left for what really indexes nothing.

**And the claim an element load carries its own type is read by a fixture now.** The two
reference pages stated it with no executable regression behind them.
`surface_timespan.tk` reads `xs[i] + t`, `xs[i] + xs[j]`, `t + xs[i]`, `-` and `>`, on a
FIXED array and on a `T[]` of heap, with a VARIABLE index; `surface_timespan_overflow.tk`
makes the panic at its end an element operand, which is where the overflow CHECK is proven
(the raw `+` would wrap in silence), with two edge operations on elements above it that must
not fire; `surface_datetime.tk` reads `ds[i] + t` with the `Kind` riding through,
`ds[i] - ds[j]`, the comparison, and an element of a `DateTimeKind` array as the second
ARGUMENT of the raw constructor (`new DateTime(1, ks[i])`). All three are REFUSED on
`da33ebd4` — `teko: unknown member: Ticks`, `Month` — which is what makes them guards.

Two figures of the entry above were corrected in place by this pass: "a `void` return
registers nothing" (the fourth pass's own sentence, untrue since the eleventh) and "the
three objects the two global arrays ROOT" (the count of a head with one `T[]` fewer; the
fixture roots **four** objects through **three** global `T[]`s, `gl`/`gc`/`gdst` plus the
`Cell` that `gc` holds, `gx` and `gf` being fixed arrays the counter never sees).

**Proof**, mc **0.15.23**, macos/aarch64: `mc build . --config mc.macos.toml` clean;
**63/63** fixtures at their `expect-exit`; `--dump-ast` of all **63** fixtures under the
compiler of `fd05b9b8` and under this one, over the SAME sources — **63 byte-identical**,
which is the whole of the code change being a refusal and nothing else; the three fixtures
this pass edits, old source against new under this compiler, differ by addition in two
(`surface_timespan.tk`, `surface_datetime.tk`: 0 lines removed, sorted diff) and by one
rewritten line in the third (`surface_timespan_overflow.tk`: the overflow operand is now an
element, `heap[ei] + TimeSpan.FromTicks(1)`, its comment with it — 2 lines out, the
verifier's own count); `sh scripts/bootstrap.sh --os macos --arch aarch64` →
`FIXPOINT OK` (63/63 under the self-hosted `teko1`); `sh scripts/check-docs.sh` green (567
links, 387 diagnostics, 122 samples — unmoved, the `void` paragraph gaining a sentence and
no sample); `mc limits . --config mc.macos.toml` verdict `ok` with every table unmoved —
`passes` 15/30, `syntax` 15, `infix` 24, `alias` 18, `types` 11, `intrin` 8/16, `heap`
1114992 of 33554432 — no new pass and no new intrinsic (D2, D21). The throwaway
instrumentation was re-run on this head: every row of the deferral table asserted `done`
after the judgement over all 63 fixtures (0 survivors), and the two high-waters re-proved by
bisection from both sides — `TK_MAXFS` **34** (`surface_field_store` trips a mark of 33) and
`TK_MAXXT` **239** (`surface_nullable_ops` trips a mark of 238), both unmoved by this pass.
`mc pkg hash .` over the source tree of this pass's code and fixture commits:
`0c49cc1ddb4d6e6c91f44c17320fcf5bc2ba7518b70f3f8cbed11cfe9eecaafe`.

**The thirteenth pass: a bare name stands for THREE members, and the guard answered one.**
The eleventh pass shut the FIELD half of "the rewrite of a global array's reads may not match
by NAME ALONE"; `tk_this_ident` (teko_this.tk) resolves a bare name to a field, then to a
member `const` (`tk_this_const`), then to a PROPERTY (`tk_this_prop_read`), and the guard in
`tk_bracket` (teko_params.tk) asked `tk_field_find` alone — so the last two names still took
the global road. Measured on `b48d465c`, with a global `i64[] src` beside the member: a
`const i64 src = 5;` read the CONSTANT as the array's handle, exit **139**, the eleventh
pass's own shape; a scalar PROPERTY `src` read the GETTER's result as the handle and exited
**70**, `teko: index into a null array`, the run-time trap catching by luck what nothing had
refused. Both are now refused where the parser still knows the binding, with the sentence the
local and the field already get, ``teko: `[` needs an array: src``, and in `tk_this_ident`'s
own order — const before property — so the name resolves at the door to exactly what it
resolves to in the pass. A `const` is always a scalar value (`tk_mconst_use`, teko_const.tk),
so it only ever indexes nothing; a property CAN be declared `T[]`, and that one is READ
rather than refused: `tk_ha_index` over a base the pass turns into the getter call, the
implicit form of `h.xs[0]`, which already compiled. On `b48d465c` the implicit `xs[0]` on a
`T[]` property was refused ``teko: `[` needs an array`` with no name at all — it fell past
every road to `tk_pm_check_index` — and reads **7** now. An INLINE array is no property's
type (only a field is laid out that way), so `tk_this_reject_array` gains no second site. A
member declared BELOW the method that reads it is still not closed, and the
[not-yet.md](docs/reference/not-yet.md) row the eleventh pass wrote for a field now names the
`const` and the property beside it: the parse-time guard rests on the member being READ
already, and the pass that could answer later (`tk_array_pass`, teko_array.tk) walks the unit
root with no class in hand.

Two documentation claims were corrected in place by this pass, both measured against the
compiler rather than reasoned about:

- `TK_MAXXT`'s ceiling is quoted **twice** in the fourth/fifth pass paragraph above as
  **256**, the figure of the head those passes measured; the sixth pass raised it to
  **4096**. Both sentences now say which is history and which is today.
- **`null` in a raw `uptr`/`ptr` slot is accepted**, and the field-store rule in
  [types.md](docs/reference/types.md) stated "`null` only in a `T?` field" with no exception.
  `tk_check_scalar_compat` (teko_typeof.tk) has said so since Q1a — `uptr` is the very type
  `null` carries, `0` is an ordinary value of it, and `ti < 0` for a raw pointer leaves the
  `T?` clause unreached. Measured: `uptr a = null;`, `ptr b = null;` and the field forms all
  compile and read `0`. The rule page ([nullable.md](docs/reference/nullable.md)) carried the
  same absolute sentence and gains the same exception.

**Proof**, mc **0.15.23**, macos/aarch64: `mc build . --config mc.macos.toml` clean;
**63/63** fixtures at their `expect-exit`; `--dump-ast` of all **63** fixtures under the
compiler of `b48d465c` and under this one, over the same sources — **62 byte-identical**, the
one that moves being `surface_field_store.tk`, the fixture this pass edits; against the base
`da33ebd4`, **59** of the **62** fixtures that exist on both are byte-identical and the three
that move are the three this PR edits (`surface_datetime.tk`, `surface_timespan.tk`,
`surface_timespan_overflow.tk`), `surface_field_store.tk` being new;
`sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (63/63 under the
self-hosted `teko1`); `sh scripts/check-docs.sh` green (**567** links, **387** diagnostics,
**122** samples — the no-run block on ``teko: `[` needs an array`` growing two classes and no
new sample); `mc limits . --config mc.macos.toml` verdict `ok`, every table unmoved —
`passes` 15/30, `syntax` 15, `alias` 18, `types` 11, `intrin` 8/16, `heap` 1114992 of
33554432 — no new pass and no new intrinsic (D2, D21). The refusals and the reads above were
measured one program at a time outside `tests/`, before the fix and after; the `T[]` property
is a committed regression instead, in `surface_field_store.tk`'s `mbindexcheck` (26): class
`X` gains a `T[]` property named `gl`, the name of a global `T[]` of the same file, written
and read through the implicit `gl[0]` and asserted against `x.gl[0]` and against the global's
own untouched `gl[0]` — and `rccheck`'s floor is unmoved at **4**, the new array dying with
its object. `mc pkg hash .` over the source tree of this pass's code and fixture commit:
`87e627f70123afa56da2f314ba445066e265890594029cc694fecfbbb28f9595`.

### D51 · A global falls back through every oracle that reads the pass-time SCOPE alone, `ref`/`out`'s own dedicated pointee check included (2026-09-13)

D48 gave `tk_ty_of`'s own bare `N_IDENT` case a global fallback (`tk_ty_global`,
teko_array.tk): a global is in scope in every body, so "not known here" was never true of
one. Five OTHER sites read the SAME pass-time scope, `tk_ty_scope_find` (teko_typeof.tk), for
a name shaped differently and never fell back once the scope answered -1: `tk_ty_of`'s own
`N_ADDR` case (a `ref`/`out` argument whose pointee was not known at parse time,
teko_typeof.tk), the overload matcher's own `tk_ov_arg_ty` (teko_over.tk), and three of
`teko_deleg.tk`'s own dispatch points -- `tk_deleg_expr_ty`, `tk_deleg_assign`, and
`tk_deleg_visit`'s `N_CALL` branch. Measured with a probe pair each: a `ref f64` parameter
given an `i64` global compiled and passed the address through unchecked (`r2`), where the
same mismatch on a LOCAL already refused, `teko: a value of type i64 does not convert to
f64` (`r3`); an overload chosen by a `ref` global's own pointee refused a legal call,
`teko: the type of argument 1 of pick is not known here` (`o2`); a delegate-typed GLOBAL,
called (`g(1)`), fell through every one of `teko_deleg.tk`'s own dispatch points to a bare
`N_CALL` of a name nothing declares, `unknown name` from the core -- never a `teko: ...`
refusal, D20's own rule (`d1`). The fix is a fallback to `tk_ty_global` after
`tk_ty_scope_find` at all five, through one new one-line helper, `tk_ty_scope_or_global`
(teko_typeof.tk) -- the same order every other bare-name lookup in the oracle already reads
a name in.

**A sixth site, found by re-measuring rather than by the citation that named the first
five.** Applying only that fallback left `r2` compiling AND THEN SEGFAULTING (on the base
itself, with no fallback at all, the same program compiles and silently corrupts the global
through the bit-reinterpreting write) -- worse than
the silent pass it started as. The refusal `r3` gets does not come from `tk_ty_of` at all: a
`ref`/`out` call argument's pointee is checked by a SEPARATE, dedicated identity rule over a
SEPARATE table, `tk_ref_check_pointee`/`tk_ref_arg_pointee` (teko_ref.tk), which runs ahead
of the general per-argument compat/widen pass (`tk_rc_call_args`, teko_rc.tk -- `tk_ref_pass`
is registered before `tk_rc_pass` in `teko.tk`). It already carried its own comment naming
the gap: "a pointee the pass cannot name (a global, an ambiguous local) is silently
skipped." Once `tk_ty_of`'s new fallback let a global's real type reach `tk_rc_call_args`
first, that pass's own implicit-widen rule (D33: an integer converts to a float slot) took
it at face value and wrapped the `ref`-tagged ADDRESS argument in the float cast meant for a
VALUE -- `scvtf` on a pointer, the segfault. `tk_ref_arg_pointee` gets the identical
one-line fallback, and because the dedicated pointee check now runs to completion before
`tk_rc_call_args` ever sees the argument, it refuses first, with the identical wording a
local gets: both read `tk_reject_compat` over the same two type names. It reads the SHARED
helper, `tk_ty_scope_or_global` (teko_typeof.tk), like every other site here: teko_ref.tk is
included AHEAD of teko_typeof.tk in `teko.tk`, so the name is not defined yet where it is
used -- which is what the forward declaration at the head of teko_ref.tk is for, the same
device that file already uses for `tk_default_decl_count`. (The first round of this crumb
did write a private fallback of its own there, on the reasoning that the include order
forbade the helper; the verifier's rewrite below replaced the whole function and took the
forward declaration instead. Copilot, second pass, read the paragraph against the code.)

**`tk_pm_arg_ty` (teko_params.tk) widened too**, from `tk_ty_global_ha` (a global `T[]` of
heap only) to the general `tk_ty_global`, deleting the now fully-subsumed
`tk_ty_global_ha` (teko_array.tk) -- nothing else called it. Measured, not argued: a
`params i64[]` call with an `f64` global in its tail already refused, at the same line, with
the same wording, `teko: a value of type f64 does not convert to i64`, because
`tk_rc_call_args` catches the generated `tkarr_put_i64` call's own argument downstream,
through `tk_ty_of`'s pre-existing (D48) fallback, before this pass's own element check ever
ran. The widen changes which pass answers first, not what is answered -- a global `T[]` of
heap was never the only global `params` could see; it was the only one anything downstream
had not already caught. (Item 3 of the Copilot pass below goes further: the table this site
read BEFORE reaching any global was the wrong one too.)

**Nine other `tk_ty_scope_find` call sites are untouched.** `teko_ns.tk`'s own guard in the
mangling pass (twice), `teko_this.tk`'s five "is this a local or a parameter?" guards, the
guard `teko_over.tk`'s own member-access fallback takes before the field/`this` search, and
`teko_deleg.tk`'s own `tk_lam_check_name`: each asks "is this a local?" only to fall through
to a DIFFERENT table on a `no` -- a field, `this`, a capture, a function name, a type -- and
a global answering there would be the wrong table's own name to answer instead of the right
one's. `tk_lam_check_name` is the sharpest of them: a global is exactly what it defers to
`tk_lg_add`, the one point a global's own row is knowable.

**Copilot finding, pass 1: the wrap guard was NOT one of them, the new fallback erased an
answer, and the `params` oracle was the wrong table to begin with.** Three sites, each
measured on its own probe before and after, `mc` 0.15.23:

1. `tk_deleg_coerce`'s own bare-name-to-thunk wrap (teko_deleg.tk) was filed above as a
   guard that must NOT see a global. It must: the table it falls through to on a `no` is
   "the name of a FUNCTION to wrap", and a delegate-typed GLOBAL on the right of a slot is
   not one. `Op h; h = g_op;`, `takeOp(g_op)`, `return g_op;` and `Op m = g_op;` all wrapped
   a function nothing declares -- `teko: unknown function: g_op` on a program with no error
   in it, on the base (`da33ebd4`) exactly as on this crumb's first round. It reads
   `tk_ty_scope_or_global` now, like every dispatch point around it. A free function's own
   name is in NO slot table, global or local, so it still answers -1 and is still wrapped
   (probe: assignment, argument, `return` and initializer over a plain function, all
   unchanged); a global of some OTHER type on a delegate slot now gets the delegate's own
   mismatch, `teko: Op takes a function, another Op, or null`, where it used to get the
   misleading `unknown function`.
2. `tk_ref_arg_pointee`'s new fallback was UNCONDITIONAL, and that erased a deliberate
   answer. `tk_ref_local_ty` returns -1 for two different things: "the body declares no such
   local", and "two sibling blocks declare it under two types" (`tk_ref_lamb`) -- the second
   is not "no local", it is "not one type", and reading the global there types a slot the
   site never meant. A global `i64 g` beside `if (c) { f64 g = 2.0; bumpf(ref g); }` refused
   the legal call, `teko: a value of type i64 does not convert to f64`: a REGRESSION this
   crumb's own first round introduced (the base compiles and runs it). The two answers are
   kept apart now, and only "no such local" falls through to the global.
3. `tk_pm_arg_ty` (teko_params.tk) read `tk_slv_find_unit`, a UNIT-WIDE most-recent-wins
   table: a local of the same name in ANOTHER function answered for this one. `i64 g = 3;`
   with an `f64 g` declared last in some unrelated body refused `sum(1, g, 2)` over
   `params i64[]` with `teko: a value of type f64 does not convert to i64` -- a legal program,
   refused on the base too, so pre-existing rather than this crumb's, and the wrong oracle
   either way. The declaration being walked is known right there, so it is recorded
   (`tk_pm_cur_fn`, set by `tk_pm_params_of`) and the name is read under the function that
   owns it.

2 and 3 are one rule, so they are one piece of code: `tk_local_ty_in(fn, name)` (the body of
the former `tk_ref_local_ty`, taking its function as an argument) and
`tk_local_or_global_in(fn, name)` (that, then `tk_ty_global`, and -1 rather than a global on
an ambiguity), both teko_ref.tk, which teko_params.tk reads as well -- teko_ref.tk is
included ahead of it. `tk_ref_local_ty` is gone, its one caller rewritten.
`tk_slv_find_unit` (teko_struct.tk) keeps its declaration with no caller left in the tree;
deleting it belongs to the next crumb that opens that module. The comment over
`tk_ref_check_pointee` is corrected too: a global IS named now, and the only pointee left
unnamed is the ambiguous local.

**Verifier finding: that "ambiguity" was the bug, and two oracles were answering one name.**
The reviewer's own reproducer -- `bumpf(ref g)` written OUTSIDE the two sibling blocks that
shadow an `i64` global `g`, where nothing is ambiguous at all -- compiled on the head of
this crumb and exited 139, `EXC_BAD_ACCESS` at `ldr d17, [x10]`. `--dump-ast` prints the
whole story in two lines: `CAST type=f64` over `ADDR type=uptr name=g`, D33's own `scvtf`
run on an ADDRESS. Item 2 above kept the "ambiguous local" answer apart from "no such
local", and that is exactly what broke it: `tk_ref_scan_local` decided ambiguity by scanning
the WHOLE body, blind to the position of the site, so it answered "ambiguous, refuse
nothing" for every `ref g` in the function -- including one written where no block declaring
`g` is open -- while `tk_rc_call_args` (teko_rc.tk), reading the LEXICAL scope through
`tk_ty_of`, answered the global's `i64` and widened the address on it. Two oracles for one
name, one of them position-blind.

The fix is that there is only ONE oracle, and no such thing as an ambiguous name. A name at
a point is the innermost declaration visible THERE, or, when no block open there declares
it, the global -- which is what `tk_ty_scope_or_global` (teko_typeof.tk) already answers,
and what `tk_ty_of`, `tk_ov_arg_ty` and teko_deleg.tk's dispatch points already read.
`tk_ref_pass` runs AFTER `tk_typeof_pass` (teko.tk), so the oracle's own scope stack is
available to it: `tk_ref_fn` opens it with the function's parameters and `tk_ref_walk` keeps
it live exactly as `tk_ty_walk_list` does -- a mark at every block, cut back at its `}`, a
local in scope only after the statement that declares it has been walked -- and
`tk_ref_arg_pointee` is three lines that ask it. teko_params.tk, the other caller, does the
same in `tk_pm_walk`/`tk_pm_walk_unit` and asks the same helper. DELETED with the scan:
`tk_ref_scan_local`, `tk_ref_lty`, `tk_ref_lamb`, `tk_local_ty_in`, `tk_local_or_global_in`
(teko_ref.tk), `tk_ref_param_ty` (teko_ref.tk -- `tk_ty_scope_params` records a `ref`/`out`
parameter under its own pointee, so the private parameter lookup had nothing left to add)
and `tk_pm_cur_fn` (teko_params.tk). ADDED: `tk_ty_scope_mark`/`tk_ty_scope_cut`
(teko_typeof.tk), two one-line accessors, because teko_ref.tk is included ahead of that file
and cannot name `tk_nscope` itself. Item 2's own probe still passes and now for the right
reason: `ref g` INSIDE the block that declares `f64 g` is that block's local and is
accepted; the same `ref g` after the block has closed is the global and is judged against
it.

**And an invariant of its own, so no second oracle can ever cast an address again.**
`tk_rc_call_args` widens an N_ADDR never -- an address is not a numeric value, whatever
`tk_ty_of` says its pointee is. A `ref`/`out` argument is judged there by the one rule that
owns pointees, identity (`tk_ref_check_pointee`), which refuses in the same words a value
gets; that is also the site that judges an OVERLOADED name's pointee, which teko_ref.tk's
own call check (declared exactly once) leaves to the matcher. With the lexical oracle in
place, every user-written mismatch is refused before this guard is reached -- measured:
a mismatched pointee on an overloaded name is refused by the matcher itself,
`teko: no overload of f matches these arguments` -- so the guard is proved by `--dump-ast`
instead: the accepted `bumpi(ref g)` carries a bare `ADDR type=uptr name=g` with no CAST
over it, where the head of this crumb printed `CAST type=f64` over that same node.

Four probes, each run on four builds (`da33ebd4` the base, `98e18245` the first round,
`3ba13535` the head the verifier reproved, and this fix), mc **0.15.23**, macos/aarch64.
`g` is an `i64` global except where the row says otherwise, and the two sibling blocks
declare `i64 g` and `f64 g`:

| probe | `da33ebd4` | `98e18245` | `3ba13535` | now |
|---|---|---|---|---|
| `bumpf(ref g)` after both blocks | compiles, `g` silently corrupted (exit 2) | refuses | compiles, **exit 139** | refuses, `teko: a value of type i64 does not convert to f64` |
| `bumpf(ref g)` inside the `f64` block | runs, 42 | refuses (the first round's regression) | runs, 42 | runs, 42 |
| `bumpf(ref g)` inside the `i64` block, `f64` global | refuses | refuses | refuses | refuses |
| `bumpi(ref g)` after both blocks, both declaring `f64 g` | refuses, `teko: a value of type f64 does not convert to i64` | refuses | refuses | runs, 42 |

The last row is the scan's own verdict read out loud: a legal call, refused on the base and
on both rounds of this crumb because a block that is CLOSED at the site still answered for
the name. It is the line `tests/surface_globals.tk` gains.

Proof of the finding's fix, mc **0.15.23**, macos/aarch64: the reproducer refuses,
`teko: a value of type i64 does not convert to f64`, at the line that writes it; **63/63**
fixtures at their `expect-exit`, with `tests/surface_globals.tk` section 6 extended by the
outside-the-blocks call (`bumpi(ref g_sib)`, accepted, and `main` checks the global really
was bumped, 7 -> 8 -> 9); the `--dump-ast` of the **62** fixtures that existed at
`da33ebd4`, byte-identical to that base; `sh scripts/bootstrap.sh --os macos --arch
aarch64` -> `FIXPOINT OK` (63/63 under `teko1`); `sh scripts/check-docs.sh` green
(568 links, 385 diagnostics, 119 samples); `mc limits . --config mc.macos.toml` verdict `ok`
on both legs, the `tests/hello.tk` leg's structural counts byte-identical to the head of
this crumb and its heap 1114880 estimated against 1114992 used of a 33554432 ceiling, the
compiler leg's used heap 92311424 of a 218234880 reserve (the deletions give back what the
scan cost: `nodes` used 154066 -> 153967, `ins` 212163 -> 211960, `funcs` 3133 -> 3131).
`mc pkg hash .` at this fix's own code commit:
`050f35870ffdd4349e216d19da851a21ae66af4fa312328a72ad3d2028263e3e`.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config mc.macos.toml`
clean; **63/63** fixtures at their `expect-exit` (`tests/surface_globals.tk`, exit 42, six
sections: `ref`/`out` over a scalar global, an overload picked by a `ref` global, a
delegate-typed global called as a plain function, as a lambda, from inside another function
and read on the RIGHT of an assignment, an argument, a `return` and an initializer, `params`
over a global scalar element the last declaration of that name in the unit masks from
another body, a regression check over an enum global and `T?` over a reference global (D48's
own two sites, untouched by this crumb), and a `ref f64` over the `f64` one of two sibling
blocks that shadow an `i64` global -- the pass-head build refuses that last one, the base
refuses the delegate one); `--dump-ast` of the 62 fixtures that existed before,
byte-identical to `da33ebd4`;
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (63/63 under the
self-hosted `teko1`; the compiler's own sources declare no `ref`/`out`/delegate/`params`
over a global, so the ladder proves the fix is inert on itself, not that it is exercised by
it); `sh scripts/check-docs.sh` green (568 links, 385 diagnostics, 119 samples -- two new
`// no-run` samples: the `ref f64` global mismatch in
[diagnostics.md](docs/reference/diagnostics.md), whose paragraph also states the one pointee
still read as unnamed, and a global of a non-delegate type on a delegate slot in
[delegates.md](docs/reference/delegates.md)); `mc limits . --config mc.macos.toml`
verdict `ok` on both legs, measured back-to-back against `da33ebd4` under the identical
command: the compiler-build leg's `nodes`/`funcs`/`strings`/`ins`/`symbols`/`heap` move by
this crumb's own small source growth (used heap 91606832 -> 92324032 of a
203096064/218234880 reserve), the `tests/hello.tk` leg's structural counts (`tokens` through
`intrin`) are BYTE IDENTICAL and only its own `heap` moves (476016 -> 1114992 of a 33554432
ceiling, 3.3%) -- D35's own precedent: the estimator is `mc`'s own and moves on its own
account, and every table stays comfortably `ok`. `mc pkg hash .`:
`9de962f08ba2783e376da38789e9f4f2cc87d9001a250ec7f8e9e753164a36eb`.

**Copilot finding, pass 2: an address guard wider than its own reason, and a `params` oracle
that still had a second table in front of it.** Both measured on `da33ebd4` and on the head
of this crumb (`37f90714`) before either was touched, mc **0.15.23**:

1. The N_ADDR guard above skipped the compatibility check for EVERY address, where only a
   `ref`/`out` parameter has a different rule to take. The premise it was filed under does
   not survive measurement: `take(&x)` at a class, an `i64?`, an `enum` and a `TimeSpan`
   parameter compiles on the BASE exactly as it does on the head, and the ADDRESS is never
   cast on either -- `tk_ty_of` types an UNTAGGED address with nothing at all. Its pointee
   branch is entered only by a node `ref`/`out` TAGGED (`tk_rfarg_kind`), and a bare `&x`
   falls past every case of the oracle to -1, which refuses nothing, which is also why
   `tk_num_widen` had nothing to widen there (`tk_num_widens(f64, -1)` is 0, and `uptr` is
   outside `tk_is_int_ty` by name in any case). So the guard removed no refusal that
   existed -- and it was still the wrong shape, because it answered the ARGUMENT's question
   where the rule belongs to the PARAMETER. It is split: `ref`/`out` takes the identity rule
   over pointees, anything else takes the ordinary `tk_check_compat` every value takes, and
   the widen is out of reach of both. The day the oracle learns to type a bare `&x`, one
   pass answers it instead of nothing answering at all.
2. `tk_pm_arg_ty` (teko_params.tk) still asked a table BEFORE the lexical scope, the
   function-wide `pmp_name`/`pmp_ty` filled by `tk_pm_params_of` and read by
   `tk_pm_param_ty`. A table with no position in it cannot be shadowed: inside `f(f64 x)`,
   a block's own `i64 x = 2` still answered `f64`, so `sumi(1, x)` over a `params i64[]`
   was refused, `teko: a value of type f64 does not convert to i64` -- a legal program,
   refused on the base and on the head alike. The verifier's own rewrite had already put
   every parameter of the declaration being walked into the scope the walk keeps live
   (`tk_ty_scope_params`, teko_typeof.tk, in `tk_pm_walk_unit`), so the table was a second
   oracle for a name the first one already knew. DELETED: `TK_MAXPMP`, `pmp_name`/`pmp_ty`/
   `tk_npmp` and their four accessors, `tk_pm_params_of` and `tk_pm_param_ty` -- and with
   them the refusal ``teko: too many parameters in one declaration``, that table's own
   16-row ceiling, struck from
   [diagnostics.md](docs/reference/diagnostics.md): the scope answers -1 past its end
   (`tk_ty_scope_add`) rather than refusing, "not known here", which refuses nothing.
   Deletion over addition: 43 lines out of teko_params.tk, 13 of comment back in, and the
   one call site that read them is a line shorter.
3. The sixth-site paragraph above described code that the verifier's own rewrite had already
   replaced: `tk_ref_arg_pointee` reads the SHARED `tk_ty_scope_or_global`, through the
   forward declaration at the head of teko_ref.tk, not a private fallback of its own. The
   paragraph says so now.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: **63/63** fixtures at their
`expect-exit`, with `tests/surface_globals.tk` (exit 42) gaining section 4's `shadowparam`
(a local shadowing a PARAMETER at a `params` call, accepted with the LOCAL's type) and
`plainparam` (no shadow: the parameter itself answers), and a new section 7, `addrcheck` --
a bare `&v` at a plain `uptr` parameter, which now goes THROUGH the ordinary compatibility
rule instead of past it; the `--dump-ast` of the **62** fixtures that existed at `da33ebd4`,
byte-identical to that base, the guard's own proof that no address gained a `CAST` and no
accepted program moved; `sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`
(63/63 under `teko1`); `sh scripts/check-docs.sh` green (568 links, **384** diagnostics --
the one struck above -- 119 samples); `mc limits . --config mc.macos.toml` verdict `ok` on
both legs, the `tests/hello.tk` leg's structural counts and heap (1114992) byte-identical to
the head of this crumb, the compiler leg giving back what the deleted table cost (`nodes`
used 153967 -> 153815, `ins` 211960 -> 211752, `funcs` 3131 -> 3125, heap 92311424 ->
92276992 of a 218169344 reserve). `mc pkg hash .` at this pass's own code commit:
`c037572671bf559c85096676e3908fa8cd153b0df15dc81023e43b1cd313fa18`.

**Copilot finding, pass 3: the validator has ONE caller that runs where no oracle
answers, and it was guessing "function" for every name.** The reviewer read the wrap guard
above against `teko_heaparr.tk` and found the site the first pass's measurement could not
reach from a slot: `ops[i] = e` on an `Op[]` is coerced by `tk_deleg_coerce` from
`tk_ha_index`, at PARSE time, where `tk_ty_scope_find` is empty (the scope walk has not
run) and `tk_ty_global` is empty as well (`tk_hg_collect` fills it in `tk_array_pass`, the
fifth pass). So the fallback the first pass gave the guard cannot help there, and every
NAME written at an element store read -1 and was wrapped as a function. Measured on
`da33ebd4`, on the head of this crumb (`2664c87c`) and on this fix, mc **0.15.23**,
macos/aarch64 -- `Op` is `delegate i64 Op(i64 a)`, `g_op` a global of it, `lo` a local, `p`
a parameter, `g_n` an `i64` global:

| probe | `da33ebd4` | `2664c87c` | now |
|---|---|---|---|
| `ops[0] = g_op;` then `ops[0](1)` | `teko: unknown function: g_op` | same | runs, 42 |
| `ops[0] = lo;` | `teko: unknown function: lo` | same | runs, 42 |
| `ops[0] = p;` (a parameter) | `teko: unknown function: p` | same | runs, 42 |
| `ops[0] = addOne;` (declared above) | runs, 42 | runs, 42 | runs, 42 |
| `ops[0] = laterFn;` (declared below) | `teko: unknown function: laterFn` | same | runs, 42 |
| `Op h = ops[0];` (read back) | runs, 42 | runs, 42 | runs, 42 |
| `g_ops[0] = addOne;` on a GLOBAL `Op[]` | `unknown name` (the core) | same | runs, 42 |
| `ops[0] = g_n;` | `teko: unknown function: g_n` | same | `teko: Op takes a function, another Op, or null` |
| `Op h; h = g_n;` (pass 1's own site) | `teko: unknown function: g_n` | `teko: Op takes a function, another Op, or null` | unchanged |

Three things the table says. The guess was never about GLOBALS: a local and a parameter
died exactly as the global did, so a parse-time resolver for `tk_ty_global` -- the cheaper
alternative -- would have fixed one row of five and left the rest. A function declared
BELOW the store died too, because `decl_find` at parse time only knows what has been read
so far. And a GLOBAL `Op[]` was not coerced at all: `tk_array_resolve_write`
(teko_array.tk) builds its store inside `tk_array_pass` and called `tk_ha_store` with the
raw value, so the bare name reached the core, `unknown name` -- never a `teko: ...`
refusal, D20's own rule again.

**The fix is D48's, applied to the one caller that needed it.** The coercion moves from
`tk_ha_index` to `tk_ha_store` (teko_heaparr.tk), the one point BOTH element stores pass
through, and a value the store's own oracles cannot settle is recorded
(`tk_deleg_store_defer`, teko_deleg.tk) and coerced during `tk_deleg_walk` -- the same
device `tk_prim_arg_defer` (D48) is for an argument a parse-time column cannot type, and
the same reasoning the verifier finding above used to pick its walk: `tk_deleg_walk` is the
one place that carries both halves of the question at once, the LEXICAL scope at the site
(`tk_ty_scope_var`, a mark per block) and every global row (`tk_array_pass` runs before
`tk_deleg_pass`, teko.tk). The judgement reads the value out of the store's own argument
list rather than remembering it, so a pass that rewrote the node in between is judged as it
stands, and a wrap is spliced into the list link -- no node ever changes identity. A store
the walk never reaches is judged all the same at the end of the pass, with the scope
closed: silence is the one answer this table may not give.

**Only what nothing can name waits.** `tk_deleg_store_late` defers a bare identifier the
pass-time scope and the global rows do not answer for AND that is either a SLOT the
parser's own scoped table of locals holds (`tk_pty_of`, teko_struct.tk -- a local shadowing
a function of that name is the local, not the function) or a name no declaration read so
far gives a body to. Everything else is settled where it is written: `null`, a
`new Op(...)`, a lambda, a call, an element read, and a function declared above the store.
That is what keeps the 62 dumps identical -- `tk_deleg_wrap` emits four declarations
through `tk_top_emit`, so deferring a wrap that already worked would move them in the tree
of a program that did not change. One line moves in teko_array.tk too
(`tk_deleg_late_move`, forward-declared there as `tk_ha_store` already is): the global
write COPIES the store it built into the node the tree holds (`node_assign`) and drops the
one it built, so the deferral re-points at the node the walk will actually reach -- the
same re-mark `tk_os_add` makes one line above it. ADDED: one refusal, ``teko: too many
element stores of unknown type`` (64 in one unit), in
[diagnostics.md](docs/reference/diagnostics.md).

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: the nine probes above, each run on the
three builds; **63/63** fixtures at their `expect-exit`, with `tests/surface_globals.tk`
(exit 42) gaining section 3b, `delegarrcheck` -- an `Op[]` taking a global, a local, a
function declared above and one declared below, the element read back into a name, a
parameter stored from another body (`storeParam`), and a GLOBAL `Op[]` taking a function
and a local; the `--dump-ast` of the **62** fixtures that existed at `da33ebd4`,
byte-identical to that base; `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (63/63 under `teko1`); `sh scripts/check-docs.sh` green (568 links, **385**
diagnostics -- the one added above -- 119 samples); `mc limits . --config mc.macos.toml`
verdict `ok` on both legs, measured back-to-back against `2664c87c` under the identical
command: the `tests/hello.tk` leg's structural counts (`tokens` through `intrin`) BYTE
IDENTICAL, the compiler leg moving by this pass's own source growth (`nodes` used 153815 ->
154187, `ins` 211752 -> 212286, `funcs` 3125 -> 3136, used heap 91629824 -> 92501344 of a
218562560 reserve). `mc pkg hash .` at this pass's own code commit:
`ea80f178f29b78b3456e57eb85ccd5532751f0892aa271f2628502ce76efb18d`.

**Copilot finding, pass 4: the store the third pass taught to wait was judged where nothing
can be judged.** The reviewer read the third pass against `tk_array_resolve_write` and found
the other half of it: `tk_hg_resolve_write` (teko_array.tk) assembles a GLOBAL `T[]`'s own
element store INSIDE `tk_array_pass`, and the third pass handed that store the same
validator the parse-time one takes. A pass is not a site: no lexical scope stands around it,
so the only rows in reach were the globals, and the escape rule the parse site runs
(`tk_lam_escapes` in `tk_ha_index`, teko_heaparr.tk) did not run there at all. Four probes,
each measured on `da33ebd4`, on the head of this crumb (`853f829e`) and on this fix, mc
**0.15.23**, macos/aarch64 -- `Op` is `delegate i64 Op(i64 a)` and `g_ops` a global `Op[]`:

| probe | `da33ebd4` | `853f829e` | now |
|---|---|---|---|
| `g_ops[0] = new Op((i64 y) use (&x) => x + y)` | runs, 42 | runs, 42 | refuses, `teko: a lambda that captures by reference cannot leave its scope` |
| the same on a LOCAL `Op[]` | refuses, same words | refuses | refuses |
| `g_ops[0] = f` with `f` a lambda tainted by `use (&acc)` | runs, 42 | runs, 42 | refuses, the same words |
| the same on a LOCAL `Op[]` | refuses, same words | refuses | refuses |
| `dst[0] = g` with a local `i64 g` shadowing a delegate global `Op g` | `unknown name` (the core, at the global's own assignment) | compiles, **exit 139** | refuses, `teko: Op takes a function, another Op, or null` |
| `dst[0] = f` with a local `Op f` beside a free function `f` | runs, 42 (the LOCAL) | runs, **141** (the FUNCTION) | runs, 42 (the LOCAL) |
| `g_ops[0] = chooser(0)`, `chooser` a LOCAL delegate returning `Op` | runs, 42 | refuses, `teko: Op takes a function, another Op, or null` | runs, 42 |

The base is right on two of those rows by not judging at all -- it coerced nothing, so the
raw name reached the code generator and the innermost declaration answered, which is also
why its own two remaining rows are `unknown name` from the core. The third pass judged, and
judged in the one place where a name means whatever the globals say it means.

**One rule, and it is the same one the crumb has been applying all along: judge where names
have types.** A store built in a PASS judges NOTHING where it is built -- not the coercion,
not the type of the value, not the escape -- and waits whole for `tk_deleg_walk`, which
stands at the site with the lexical scope live (`tk_ty_scope_var`, a mark per block), every
global row collected (`tk_array_pass` runs first, teko.tk) and the function that owns the
store known (`tk_cur_fn_name`, which is exactly the owner `tk_lam_escapes` reads a taint
under). ADDED: `tk_deleg_defer_all` and its one-line setter (teko_deleg.tk), forward-declared
in teko_array.tk beside `tk_deleg_late_move` and set around the two lines of
`tk_hg_resolve_write` that build the store; one column in the deferral table, `dl_esc`, so
the escape is judged at the walk only for the store that could not judge it where it was
written -- a LOCAL store's escape was already taken at its parse site, and taking it again
at the walk would read a taint that site could not see yet, the position-blindness the
verifier finding above already struck once. `tk_deleg_late_do` takes the escape first, in
the same words and from the same `tk_lam_escapes` the parse site calls, then the coercion it
already took.

**Deletion, not addition, for the pass's own leftovers.** `tk_slv_find_unit`
(teko_struct.tk) is gone: the second pass left it with no caller in the tree and the comment
in teko_params.tk that names it says so now. The comment over `tk_ref_arg_pointee`
(teko_ref.tk) attributed a "unit-wide most-recent-wins table" to `tk_slv_find`, which is the
PARSER's own stack of locals still in scope -- the unit-wide one was `tk_slv_find_unit`, the
function just deleted. It names the table for what it is: a stack that records a declaration
and never a PARAMETER, so `lvl_c(ref x)` inside `lvl_b(ref i64 x)` answers -1, or the type
of some unrelated local named `x` open around the call.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: the seven probes above on the three
builds; **63/63** fixtures at their `expect-exit`, with `tests/surface_globals.tk` (exit 42)
gaining section 3c, `delegarrglobalcheck` -- a GLOBAL `Op[]` taking a lambda with no
capture, a call through a LOCAL delegate, and a local of delegate type shadowing a free
function of that name, the element read back and called in each case; the head of this crumb
refuses that section at its second store. The two refusals are one `// no-run` sample in
[diagnostics.md](docs/reference/diagnostics.md), whose escape entry now names an ELEMENT of
a `T[]`, local or global, beside the field and the static field it already named. The
`--dump-ast` of the **62** fixtures that existed at `da33ebd4`, byte-identical to that base;
instrumented, `tk_deleg_late_rest` refusing any store that reaches it unjudged, the whole
suite and every probe pass -- no pending store survives the walk (and the instrument is not
vacuous: with `tk_deleg_late_pend` disabled it fires on the fixture at once);
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (63/63 under `teko1`);
`sh scripts/check-docs.sh` green (568 links, 385 diagnostics -- none added, the pass speaks
in refusals that already existed -- **120** samples, the one added above);
`mc limits . --config mc.macos.toml` verdict `ok` on both legs, measured back-to-back
against `853f829e` from the same clean state: the `tests/hello.tk` leg IDENTICAL down to its
`heap` (used 467824 of a 33554432 ceiling), the compiler leg moving by this pass's own
source growth (`nodes` used 154187 -> 154215, `ins` 212286 -> 212326, `funcs` 3136 -> 3137,
`globals` 922 -> 924, used heap 91854176 -> 91921088 of a 217448448 reserve). `mc pkg hash .`
at this pass's own code commit:
`766306c9be8a85b6788252ae2058b230cb2fe72726ae9ba665f4483afb1aca10`.

**Copilot finding, fifth pass: the store that waits differed a NAME and nothing else, the
global slot never asked the escape rule, and the queue was eight times shallower than the
writes that fill it.** Three findings, each measured on `da33ebd4` (the base), on the head
of this crumb (`945b94b1`) and on this fix, mc **0.15.23**, macos/aarch64 -- `Op` is
`delegate i64 Op(i64 a)`, `Chooser` a `delegate Op Chooser(i64 k)`, `g_op` a global `Op`
and `local` a local holding a lambda tainted by `use (&acc)`:

| probe | `da33ebd4` | `945b94b1` | now |
|---|---|---|---|
| `ops[0] = chooser(1)` on a LOCAL `Op[]` | refuses, `teko: Op takes a function, another Op, or null` | same | runs, 42 |
| the same store on a GLOBAL `Op[]` (fourth pass) | `unknown name` (the core) | runs, 42 | runs, 42 |
| `g_op = local;` | compiles, `g_op` outlives `acc` | same | refuses, `teko: a lambda that captures by reference cannot leave its scope` |
| `g_op = new Op((i64 x) use (&acc) => acc + x);` | compiles, the same dangling capture | same | refuses, the same words |
| `Op local2 = local;` / `local2 = local;` (a LOCAL target) | runs, 42 | runs, 42 | runs, 42 |
| 65 valid deferred stores in one unit (20 global, 45 local) | `teko: unknown function: lo` (the third pass's own gap) | refuses, `teko: too many element stores of unknown type` | runs, 42 |

1. **The store differed a bare `N_IDENT` and judged everything else on the spot.**
   `tk_deleg_store_late` (teko_deleg.tk) asked its question only of a NAME, so every other
   shape went straight to `tk_deleg_coerce` at PARSE time, where `tk_ty_scope_find` is empty
   and `tk_ty_global` is not filled yet -- and a value those two cannot type is refused
   there in the delegate's own mismatch words. `ops[0] = chooser(1)`, with `chooser` a local
   delegate answering `Op`, is exactly that value, and
   [arrays.md](docs/reference/arrays.md) promises it "in a local array and in a global one"
   alike: the global half has waited for the walk since the fourth pass, the local half
   refused a legal program. It is ONE rule at both halves now, the global store's own: what
   the site's oracles TYPE (`tk_deleg_expr_ty`, the same oracle the validator asks one line
   later) is settled where it stands, and what they do not type waits for `tk_deleg_walk`,
   which stands at the site with the lexical scope live and every global row collected.
   Deferring only what the site types WRONG is what keeps the accepted programs where they
   are: a wrap emits four declarations through `tk_top_emit`, and moving a wrap that already
   worked would move them (the 62 dumps). A ternary is the one value the validator takes
   APART, one branch at a time, so it stays at the site whenever the site is judging at all.
2. **A GLOBAL of delegate type took a capture that dies before it does.** `tk_deleg_assign`
   read the target through `tk_ty_scope_or_global` (pass 1) but never asked D221 decision
   21's first escape, so `g_op = local;` and `g_op = new Op(... use (&acc) ...)` parked the
   ADDRESS of a dead local in a slot that survives the call -- accepted on the base and on
   the head alike. A field, a static field, an element of a `T[]` and a `return` already
   carry that verdict; the plain global slot was the one write of a delegate with none. It
   asks `tk_lam_escapes` when, and only when, the target is NOT in the lexical scope
   (`tk_ty_scope_find` < 0, the same helper the walk keeps live): a LOCAL target keeps the
   taint PROPAGATION it always had (`tk_lam_taint_stmt`) and no refusal, because the scope
   that owns the capture is still the one holding it.
3. **The queue was 64 where the writes that fill it are capped at 512.** `TK_MAXDGLATE`
   (teko_deleg.tk) is the table of element stores waiting for the walk, and `TK_MAXGDEF`
   (teko_array.tk, 512) is the ceiling on the array writes of a unit that wait for
   `tk_array_pass` -- every one of which may need a row in the first. 65 perfectly ordinary
   stores were refused ``teko: too many element stores of unknown type``. The table is as
   deep as the one that feeds it now, and `null` -- the one value that means the same thing
   at every site, judged identically by the pass and by the parse site -- takes no row at
   all. The ceiling and its refusal are stated in
   [diagnostics.md](docs/reference/diagnostics.md).

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: the six probes above on the three
builds; **63/63** fixtures at their `expect-exit`, with `tests/surface_globals.tk` (exit 42)
gaining section 3d, `delegarrlocalcheck` -- a LOCAL `Op[]` taking a call through a local
delegate and a lambda typed at the site, and a by-reference capture handed to another LOCAL
through an initializer and through an assignment; the head of this crumb refuses that
section at its first store. The two new refusals are one `// no-run` sample in
[diagnostics.md](docs/reference/diagnostics.md), whose escape entry now names a GLOBAL of
delegate type beside the field, the static field and the element it already named, and says
that another LOCAL is not one. The `--dump-ast` of the **62** fixtures that existed at
`da33ebd4`, byte-identical to that base; instrumented, `tk_deleg_late_rest` refusing any
store that reaches it unjudged, the whole suite and every probe pass -- no deferred store
survives the walk (and the instrument is not vacuous: with `tk_deleg_late_pend` disabled it
fires on the first probe at once); `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (63/63 under `teko1`); `sh scripts/check-docs.sh` green (568 links, 385
diagnostics -- none added, the ceiling's own row is corrected from 64 to 512 -- **121**
samples, the one added above); `mc limits . --config mc.macos.toml` verdict `ok` on both
legs, measured back-to-back against `945b94b1` from the same clean state: the
`tests/hello.tk` leg BYTE IDENTICAL down to its `heap` (used 464272 of a 33554432 ceiling),
the compiler leg moving by this pass's own source growth and its one deeper table (`nodes`
used 154215 -> 154279, `ins` 212326 -> 212446, `funcs` 3137 and `globals` 924 unchanged,
used heap 85187904 -> 85532512 of a 149487616 reserve). `mc pkg hash .` at this pass's own
code commit:
`709748245e85b6ca08389ff24e75bbbdf4e13883d59411c484361f08e8d015b1`.

**Copilot finding, sixth pass: the store guessed "function" for a name a PARAMETER
shadows, kept a ternary at a site that cannot judge its branches, and the escape rule read
the top of the node and never its arms.** Three findings, each measured on `da33ebd4` (the
base), on the head of this crumb (`a3a70f66`) and on this fix, mc **0.15.23**,
macos/aarch64 -- `Op` is `delegate i64 Op(i64 a)`, `Chooser` a `delegate Op Chooser(i64 k)`,
`held` a local holding a lambda tainted by `use (&acc)` and `other` a plain `Op`:

| probe | `da33ebd4` | `a3a70f66` | now |
|---|---|---|---|
| `ops[0] = p` inside `storeParam(Op p)`, under a free `i64 p(i64)` | runs, the FUNCTION's answer | same | runs, the PARAMETER's answer |
| `ops[0] = c ? chooser(1) : chooser(0)` on a LOCAL `Op[]` | refuses, `teko: Op takes a function, another Op, or null` | same | runs, 42 |
| `ops[0] = c ? addLater : addOne`, `addLater` declared BELOW | refuses, `teko: unknown function: addLater` | same | runs, 42 |
| the same ternary on a GLOBAL `Op[]` | runs, 42 (nothing judged it) | runs, 42 | runs, 42 |
| `g_ops[0] = flag ? held : other` | compiles, the capture outlives `acc` | same | refuses, `teko: a lambda that captures by reference cannot leave its scope` |
| `g_op = flag ? held : other` | compiles, the same dangling capture | same | refuses, the same words |
| `return flag ? held : other` from a function answering `Op` | compiles, the same | same | refuses, the same words |
| `g_op = flag ? new Op(... use (&acc) ...) : other` | compiles, the same | same | refuses, the same words |
| `Op h = flag ? held : other` (a LOCAL target) | runs, 42 | runs, 42 | runs, 42 |

1. **`decl_find` is not the question a store may ask about a name.** `tk_deleg_store_late`
   (teko_deleg.tk) settled a bare `N_IDENT` whenever a declaration read SO FAR gave that
   name a body, and wrapped it as that function. A PARAMETER shadows the function for the
   whole body, and no table the parser keeps records one: `tk_pty_of` (teko_struct.tk) is a
   stack of DECLARATIONS, the pass-time scope and the global rows are both empty at parse
   time, and `decl_find(p_decl_name())` cannot help either -- the enclosing N_FUNC joins the
   unit only after its own body has been read (`parse_function`, mc's `src/parse.mc`), so
   its parameter list is unreachable from inside it. `storeParam(Op p) { ops[0] = p; }`
   written under a free `i64 p(i64)` stored the wrap and called the FUNCTION: not a refusal,
   a wrong answer, on the base exactly as on the head. The name waits for the walk, where a
   parameter stands in `tk_ty_scope_var` exactly as a local does. Two lines of guess
   deleted, and with them the last table in this file that answered about a name from
   somewhere other than the site.
2. **A ternary is late exactly when a BRANCH is.** The same guard sent every ternary
   straight to `tk_deleg_coerce`, which takes it APART -- one coercion per branch, at the
   parse site, against the two oracles that answer nothing there. A branch calling a LOCAL
   delegate was refused in the delegate's own mismatch words and a branch naming a function
   declared BELOW the store died `teko: unknown function: ...`, both legal programs, and
   both the very shapes the fifth pass had just taught the bare value to wait for. The guard
   recurses now, so a ternary whose branches the site can type is settled where it stands
   (the dumps) and one whose branches it cannot waits whole.
3. **The escape rule owns every shape the validator accepts, and it accepted one it never
   read.** `tk_lam_escapes` (teko_deleg.tk) answers over a leaf: a call to an allocator
   whose lambda captured by reference, or a name tainted with one. A ternary is an `N_CALL`
   named `tk_ternary`, so it asked `tk_lamref_has("tk_ternary")` -- no allocator has that
   name -- and answered 0 without looking at either arm. EVERY site that reads this rule
   took the capture unrefused: the global of delegate type (`tk_deleg_assign`), the element
   of a `T[]` local or global (`tk_ha_index`, `tk_deleg_late_do`), the field and the static
   field (teko_expr.tk, teko_access.tk) and the `return` (`tk_deleg_return`). One recursion
   in the one rule fixes all six, which is what makes it the root cause rather than the
   site the reviewer named; `tk_lam_taint_stmt`'s own propagation follows it for free, so
   `Op q = c ? held : other;` taints `q` as `Op q = held;` always did. A LOCAL target is
   still accepted: the scope that owns the capture is the one holding it.

**The suppressed comment is a LIMIT, not a defect, and it is written down instead of
patched.** `void sink(Op p) { g_op = p; }` called `sink(held)` retains the capture in a
global after the caller returns, and nothing refuses it. The escape is an intra-function
taint (D42): the caller's site is an ordinary argument pass, which is how a delegate is
used at all -- `forEach(xs, new Op((i64 x) use (&sum) => ...))` has the identical shape, and
a rule that refused the argument would refuse `register(Op cb)` with it. Carrying the
verdict into the callee needs a qualifier on the parameter's own TYPE, a design and not a
patch, so it is a row of [not-yet.md](docs/reference/not-yet.md): a by-reference capture
handed to a parameter that a callee stores in an outliving slot is not caught, the taint
does not cross a call.

**One dump moves, and it is this pass's own cost, named and measured.**
`tests/surface_array_heap.tk` writes `ops[0] = add; ops[1] = mul;` into an `Op[]`, the one
shape in the 62 fixtures of `da33ebd4` that the parse site used to settle and now defers
(finding 1: nothing at that site can know whether `add` is the function or a parameter).
The unit has the same **2439** lines and the same multiset of them, and `diff` is a single
pair of hunks: one contiguous **88**-line block, the eight declarations of the two wraps
(`Op__thunk_add`, `Op__vt_add`, `Op__release_add`, `Op__new_add`, and `mul`'s four), moves
from the middle of the unit to its end -- which is where every wrap the store defers has
been emitted since the third pass (`ops[3] = addLater`, a function declared below, already
took that path). The fixture is at its `expect-exit` on both sides. The other **61**
fixtures that existed at `da33ebd4` are byte-identical to that base. The alternative --
asking, at the parse site, whether the name is a parameter of the enclosing declaration --
has no table to ask: teko's own parameter readers (`tk_default_param`, teko_default.tk, and
`tk_params`, teko_class.tk) know each name as they read it, but the one table they fill
(`tk_hp_*`, teko_struct.tk) keeps `T[]` parameters alone and is reset by the next parameter
list a LAMBDA in the body opens. Judging where names have types is this crumb's whole rule;
the moved block is what it costs here.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: the nine probes above on the three
builds, plus the twenty probes of the third, fourth and fifth passes re-run on this build --
every one at the verdict its own table records; **63/63** fixtures at their `expect-exit`,
with `tests/surface_globals.tk` (exit 42) gaining section 3e, `delegternarycheck` (the
ternary at a LOCAL and at a GLOBAL element store, an arm naming a function declared below,
and the by-reference capture another LOCAL may still hold) and `storeParam` gaining the free
function `p` its own parameter shadows -- the head of this crumb refuses that section at its
first store, the base refuses the fixture earlier still. The `--dump-ast` of **61** of the
62 fixtures that existed at `da33ebd4` byte-identical to that base, the 62nd relocated as
described above; instrumented, `tk_deleg_late_rest` refusing any store that reaches it
unjudged, the whole suite and all twenty-nine probes pass -- no deferred store survives the
walk (and the instrument is not vacuous: with `tk_deleg_late_pend` disabled it fires on
`tests/surface_globals.tk:183` at once); `sh scripts/bootstrap.sh --os macos --arch aarch64`
-> `FIXPOINT OK` (63/63 under `teko1`); `sh scripts/check-docs.sh` green (568 links, 385
diagnostics -- none added, the escape's own entry now names the ternary -- **122** samples,
one added); `mc limits . --config mc.macos.toml` measured back-to-back against `a3a70f66`
from the same clean state, verdict identical on both legs (the compiler leg `ok`, the
`tests/hello.tk` leg `grew` on the head exactly as here, its heap estimate 16318 against
464272 used of a 33554432 ceiling): that second leg is BYTE IDENTICAL down to its `heap`,
and the compiler leg moves by this pass's own source growth (`nodes` used 154279 -> 154307,
`ins` 212446 -> 212503, `funcs` 3137 and `globals` 924 unchanged, used heap 85532512 ->
85578928 of a 149553152 reserve). `mc pkg hash .` at this pass's own code commit:
`eab5530bc2fadc4462fa2b1146a9411a34a85492a053d1d142805fcad0660d0c`.

**Copilot finding, seventh pass: a CALL was settled by the FIRST declaration of its name,
and the store that waits was judged twice.** Three findings, each measured on `da33ebd4`
(the base), on the head of this crumb (`82fa6aa9`) and on this fix, mc **0.15.23**,
macos/aarch64 -- `Op` is `delegate i64 Op(i64 a)`, `Chooser` a `delegate Op Chooser(i64 k)`,
`Boxed` a class and `make` an overloaded name:

| probe | `da33ebd4` | `82fa6aa9` | now |
|---|---|---|---|
| `ops[0] = chooser(1)`, a LOCAL `Chooser` shadowing a `Boxed chooser(i64)` | refuses, `teko: Op takes a function, another Op, or null` | refuses, `teko: a value of type Boxed does not convert to Op` | runs, 42 |
| `ops[0] = make(1, 2)`, first row `Op`, the pick `i64` | compiles, **exit 139** | compiles, **exit 139** | refuses, `teko: Op takes a function, another Op, or null` |
| `ops[0] = make(1, 2)`, first row `Boxed`, the pick `Op` | refuses | refuses, the generic words | runs, 42 |
| `ops[0] = new Other();` | refuses, `teko: Op takes a function, another Op, or null` | refuses, `teko: a value of type Other does not convert to Op` | refuses, the delegate's own words |
| `ops[0] = o;`, `o` a LOCAL of class `Other` | `teko: unknown function: o` | refuses, the generic words | refuses, the delegate's own words |
| 128 late LOCAL element stores in one unit | `teko: unknown function: lo` | runs, 42 | runs, 42 |
| 129 of them | `teko: too many stores into a slot of class type` | the same | the same |

1. **`decl_find` is not the question a store may ask about a CALL either.** The sixth pass
   struck that table for a bare NAME and left it standing for everything else:
   `tk_deleg_store_late` (teko_deleg.tk) settled a value whenever `tk_deleg_expr_ty` typed
   it as the element, and on an `N_CALL` that oracle reads the pass-time scope (empty at
   parse) and then falls through to `tk_ty_of`, which asks `decl_find` for the FIRST
   declaration of the called name -- no scope, no prepared overloads. Its two errors point
   opposite ways and neither is recoverable at the site: a LOCAL delegate `chooser`
   shadowing a free `Boxed chooser(i64)` was typed by the FUNCTION's return and a legal
   store was refused, and an overload whose first row answers `Op` while the row the call
   picks answers `i64` was SETTLED, the `i64` written into the `Op[]` unchecked, and the
   first call through the element segfaulted (exit 139, on the base exactly as on the head).
   The rule is the one D50 already states over `tk_fs_vty` and D48 over an argument: a call
   whose return only the pass resolves is not an early check's to guess at. Every `N_CALL`
   waits for `tk_deleg_walk`, which stands at the site with the lexical scope live and the
   overloads already picked. A ternary IS an `N_CALL` (of `tk_ternary`), so the sixth pass's
   branch-by-branch recursion goes with it -- the same verdict in one rule less, because a
   bare name in a branch was already late at this site. A FUNCTION declared above the store
   is still wrapped where the name is unambiguously a function, which at parse time means
   nowhere: that is the sixth pass's own answer, unchanged, and it is why no accepted
   program's tree moves here -- a wrap is what moves a tree, and nothing that waits is ever
   wrapped.
2. **A store that waits was judged twice, and the generic verdict spoke first.**
   `tk_ha_store` (teko_heaparr.tk) ran `tk_check_field_store` (teko_struct.tk) over the
   value whether it was deferred or not, and that check reads the SAME parse-time tables the
   guard above had just refused to trust: `ops[0] = new Other()` and `ops[0] = o` on an
   `Op[]` were refused `teko: a value of type Other does not convert to Op`, the generic
   conversion words, where the delegate's own validator answers
   `teko: Op takes a function, another Op, or null`; and finding 1's shadowed `chooser` was
   refused by it over the FUNCTION's return type even after the coercion had waited. One
   judgement per store: what waits is `tk_deleg_late_do`'s, whole, and `null` and the shapes
   the site does type keep the check -- an element store is the only road to Q1a's null rule
   (`cs[i] = null` on a `Cell[]`). The refusal is not lost, it is the right one: both shapes
   above are refused in the delegate's own words now.
3. **The queue's ceiling was derived from a table a LOCAL store never touches, and the
   derivation is what was wrong -- not the number.** The fifth pass raised `TK_MAXDGLATE`
   from 64 to 512 by reading `TK_MAXGDEF` (512, teko_array.tk), the writes into a
   possibly-global array; a local store is never one of those, so the reviewer is right that
   the ceiling has to be stated on its own terms. Measured before resizing anything: a
   delegate is a COUNTED type, so `tk_os_mark` records every element store of one, local or
   global, in `TK_MAXOS` (128, teko_struct.tk), a table never reset within a unit -- 128 late
   local stores compile and run, and 513 are refused at the 129th,
   `teko: too many stores into a slot of class type`, on the base as well (pre-existing, and
   an adjacent finding, not this crumb's). The queue cannot be filled past 128 by any
   program, so 512 stays where it is -- four times the deepest reach -- with its real feeder
   named in the code and in
   [diagnostics.md](docs/reference/diagnostics.md), where the row now says the ceiling is its
   own and which refusal a program really meets first. Growing it to 4096 would have added
   192KB of globals for rows nothing can add.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: the seven probes above on the three
builds, plus the **29** probes of passes 1 to 6 and the verifier finding re-run on this
build -- every one at the verdict its own table records, and the head of this crumb and this
fix agree on all 29; **63/63** fixtures at their `expect-exit`, with
`tests/surface_globals.tk` (exit 42) gaining section 3f, `delegcallcheck` -- a call through
a LOCAL `Chooser` that a free `Boxed chooser(i64)` shadows, and a call to the overload of
`make` whose first row is `Boxed` and whose picked row is `Op` -- the head of this crumb
refuses that section at its first store. The mirror image (a first row of `Op` over a pick
that is not one) cannot be a fixture, so it is the `// no-run` sample added to
[diagnostics.md](docs/reference/diagnostics.md). The `--dump-ast` of the 62 fixtures that
existed at `da33ebd4`: **62 of 62 byte-identical to the head of this crumb**, and against
the base, **61** byte-identical with `tests/surface_array_heap.tk` exactly as the sixth pass
declared it (2439 lines, the same multiset, one 88-line block of eight wrap declarations
moved to the end) -- deferring a call moves no tree, because only a bare name is ever
wrapped; instrumented, `tk_deleg_late_rest` refusing any store that reaches it unjudged, the
whole suite and all 36 probes pass -- no deferred store survives the walk (and the instrument
is not vacuous: with `tk_deleg_late_pend` disabled it fires on `tests/surface_globals.tk:193`
and on the probes at once); `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (63/63 under `teko1`); `sh scripts/check-docs.sh` green (568 links, 385
diagnostics -- none added, the ceiling's own row restated -- **123** samples, one added);
`mc limits . --config mc.macos.toml` verdict identical to the head on both legs (the
compiler leg `ok`, the `tests/hello.tk` leg `grew` there as here), measured back-to-back from
the same clean state: that second leg is BYTE IDENTICAL down to its `heap` (estimate 16318
against 464272 used of a 33554432 ceiling), and the compiler leg gives back what the deleted
recursion cost (`nodes` used 154307 -> 154284, `ins` 212503 -> 212458, `funcs` 3137,
`globals` 924, `strings` 2096 and `symbols` 6157 unchanged, used heap 85578928 -> 85637664 of
a 149684224 reserve). `mc pkg hash .` at this pass's own code commit:
`8f5a5276b4a1544adf52fc42f9e63c19aa9e0db51ae1b9efd5788421cef8155f`.

**Copilot finding, eighth pass: the store's own `null` check answered `yes` for a plain
`0`, and the walk's own lookup cost a table scan per NODE it visited.** Two findings,
measured on `da33ebd4` (the base), on the head of this crumb (`fa596667`) and on this fix,
mc **0.15.23**, macos/aarch64 -- `Op` is `delegate i64 Op(i64 a)`:

| probe | `da33ebd4` | `fa596667` | now |
|---|---|---|---|
| `ops[0] = 0;` on a LOCAL `Op[]`, then `ops[0](1)` | `call to unknown function` | `call to unknown function` | refuses, `teko: Op takes a function, another Op, or null` |
| `ops[0] = null;` on a LOCAL `Op[]` (unaffected) | refuses, `teko: null needs a slot declared Op?` | the same | the same |
| `g_ops[0] = 0;` on a GLOBAL `Op[]`, no call | `call to unknown function` | `call to unknown function` | refuses, the delegate's own words |
| `g_op = addOne; g_op = 0;` on a plain GLOBAL `Op` | `unknown name` | `call to unknown function` | refuses, the delegate's own words |

1. **`nd_kind(v) == N_INT && nd_val(v) == 0` is not `null`.** `tk_deleg_store_late` and
   `tk_deleg_coerce` (teko_deleg.tk) both asked that predicate for "is this value `null`",
   and an ordinary `i64` literal `0` answers it exactly as `teko_type.tk`'s own `tk_null` does
   -- the two differ only in `nd_type`, `TY_I64` against the `TY_UPTR` only `tk_null`
   carries, which neither check read. `ops[0] = 0;` slipped past `tk_deleg_store_late`'s
   null branch as if it were `null` (so the store was never deferred to the walk) and then
   past `tk_deleg_coerce`'s twin in `tk_check_field_store` (`ety` a delegate is never
   nullable, so a plain scalar there types nothing and the check falls through in silence,
   `tk_check_field_store`'s own `ei < 0` branch) -- a raw `0` reached codegen where a
   two-word delegate object belongs, and every probe above died at a MC CORE error with no
   `teko:` prefix at all (D20), not the silent numeric store the citation named but a
   diagnostic this project does not own. `tk_is_null_lit` (teko_struct.tk), which checks the
   `TY_UPTR` type, is what both sites ask now -- the SAME helper `tk_check_field_store`
   itself already reads for the identical question at every other slot a store passes
   through, so the fix is a call already in scope, not a new one. `null` is unaffected
   everywhere it was already accepted (`ops[0] = null` still needs the element declared
   `Op?`, `Op? h = null;` still passes, both probed).
2. **`tk_deleg_late_pend`, the check `tk_deleg_walk` runs at EVERY node it visits while a
   store still waits, scanned the WHOLE table -- up to `TK_MAXDGLATE` (512) entries -- per
   node, not per store.** This compiler's own source visits on the order of 150K nodes
   there (`mc limits`' own `nodes` row, below), so the table lookup this crumb's earlier
   passes added is, worst case, a comparison for every (node, waiting store) pair at once --
   most of them a store some earlier or later FUNCTION owns and the node being visited can
   never be. The fix keys each entry by the node id it waits on: `dl_bucket`/`dl_chain`
   (teko_deleg.tk), a fixed 1024-bucket table chained through the existing 512-row store
   (`tk_dg_bucket_insert`), turns "does any waiting store name this node" into the one or
   two comparisons its own chain holds -- node ids are a dense, always-positive sequence
   (`node_new`, mc's own `ast.mc`), so a plain `n % TK_DGBUCKETS` spreads them evenly
   whatever the source looks like. `tk_deleg_late_move` (the one caller that re-points an
   entry at a NEW node id, a global array's own store) inserts into the entry's new bucket
   rather than unlinking the old one -- the stale slot never matches again, because
   `dl_node` itself changed, and is left as one harmless dead link rather than taught how to
   remove itself from a chain shared with live entries; `tk_deleg_late_rest`, the one full
   sweep of the table (once, at the end of the pass, never per node), is untouched.
   Measured: `sh scripts/bootstrap.sh` end to end, three back-to-back runs each, `fa596667`
   (39.446s, 39.519s) against this fix (38.701s) -- inside the run-to-run noise of a build
   that also links, diffs 215K lines of assembly and runs 63 fixtures, so the wall clock does
   not prove the complexity claim by itself; `mc limits` does, on the compiler leg (the one
   the bucket table changes, `tests/hello.tk`'s own leg is a 38-node program the old scan
   never had to cross): `nodes` 154284 -> 154332 (+48, the bucket table and its one helper),
   `funcs` 3137 -> 3138 (+1, `tk_dg_bucket_insert`), `globals` 924 -> 926 (+2, `dl_bucket`
   and `dl_chain`), both legs still `ok` against their own ceiling.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: the four probes above on the three
builds, plus the 36 probes of passes 1 to 7 re-run on this build -- every one at the verdict
its own table (or narrative) records; **63/63** fixtures at their `expect-exit`, with
`tests/surface_globals.tk` (exit 42) gaining a comment (section 3g) that names the fixed
predicate and points at the `// no-run` case this refusal cannot be a fixture of
([diagnostics.md](docs/reference/diagnostics.md)) -- no new code path in the fixture itself,
because every store it already makes is a real function. The claim this entry made about the
validator's null ARM is corrected by the ninth pass below: `storeParam` and `h = ops[1]`
carry a function and a delegate value, never `null`, and nothing executable reaches that arm
at all. The
`--dump-ast` of all **63** fixtures: byte-identical to `fa596667` at every one -- neither
finding moves an accepted program's tree, the first because no fixture ever wrote a literal
`0` where a delegate is expected, the second because the bucket table answers the exact same
membership question the linear scan did, only faster. `sh scripts/bootstrap.sh --os macos
--arch aarch64` -> `FIXPOINT OK` (63/63 under `teko1`); `sh scripts/check-docs.sh` green (569
links, 385 diagnostics -- one no-run sample added and documented in the same paragraph, 124
samples); `mc limits --config mc.macos.toml` verdict `ok` on both legs, deltas in finding 2
above. `mc pkg hash .` at this pass's own code commit:
`5fa74991ce956317f690c012c26789d1b0fab809cd724956e8aca950ddf83d95`.

**Copilot finding, ninth pass: an entry has ONE chain link, and the move gave it two
buckets.** `dl_chain` (teko_deleg.tk) is one link per ENTRY, not one per (entry, bucket)
pair, so an entry belongs to exactly one bucket at a time. `tk_deleg_late_move` -- the one
caller that re-points a waiting store at a new node id, a global `T[]`'s own store copied
into its placeholder by `tk_hg_resolve_write` (teko_array.tk) -- called
`tk_dg_bucket_insert` WITHOUT taking the entry out of the bucket it was already in, and the
eighth pass above called the leftover slot "a single harmless dead link". It is not one.
The insert overwrites `dl_chain[i]` with the new bucket's head, so the OLD chain is cut at
`i`: everything behind `i` there stops being reachable, and when the two buckets coincide
the entry ends up pointing at itself. Both modes measured on `ba0dc27f` (the head this fix
sits on), mc **0.15.23**, macos/aarch64, with a build instrumented to print every move, every
entry `tk_deleg_late_rest` still finds undone, and a 4096-step guard in
`tk_deleg_late_pend`:

| probe | `ba0dc27f` | with `tk_dg_bucket_remove` |
|---|---|---|
| `tests/surface_globals.tk`, the SHIPPED fixture | `PROBE dgrest leftover i=13 node=1908` -- one of its six global stores never judged at the walk | no leftover, no cycle; the six are judged where the scope is live |
| the same, exit | 42 | 42 |
| `p9_cycle`, one global `Op[]` store padded with 1002 filler globals so the built store node (2395) and the placeholder it is copied into (1371) differ by exactly `TK_DGBUCKETS` -- bucket 347 for both | spins: killed at **30s** of CPU (`alarm 30`), nothing compiled | 0.06s, exit 42 |
| the same padded to 1001 (`p9_ctrl`, buckets 346/347) | 0.06s, exit 42 | unchanged |

The first row is the refutation of "harmless": the cut is real, on a file this repository
already ships. Entry 13 is `g_ops2[0] = new Op((i64 a) => a * 3)`, moved into bucket 884;
entry 14 (`g_ops2[1] = chooser(1)`, node 2932, the same bucket) was deferred after it and
then moved out to bucket 893, and that insert overwrote the link entry 14 held to entry 13.
Bucket 884 then answered "entry 14" alone, the walk never found entry 13 at its own node,
and `tk_deleg_late_rest` judged it at the END of the pass, with the scope closed -- the
exact place D51's fourth pass moved this judgement OUT of. The fixture's exit did not move
because the value that fell through is a lambda, whose verdict is the same in a closed
scope; the store two rows below it (`g_ops2[2] = shadowed`, a LOCAL shadowing a free
function) judged there would have wrapped the FUNCTION and answered 110 instead of 11. The
sweep is a backstop, not a second judgement point, and nothing may be pushed into it by an
index that lost its own chain.

**The fix is the unlink the insert always needed**: `tk_dg_bucket_remove(i, from)` walks the
OLD bucket's chain, drops `i` out of it (head or interior, `dl_bucket` or the predecessor's
own `dl_chain`), and only then does `tk_deleg_late_move` write the new node id and insert.
Cost is the length of the one chain the entry sits in -- the same order the lookup it
protects already pays, and paid once per moved store (at most one per global element store)
rather than per node visited. The alternative the recon weighed, dropping the index and
keeping a node-sorted vector, buys nothing here: the table is built in defer order, a move
would have to re-sort it, and the lookup this replaces is already one or two comparisons.

**No new fixture, and the reason is measured**: both modes need two node ids congruent mod
`TK_DGBUCKETS`, and the ids are absolute counts of every node the compiler's own passes
have built by then -- `p9_cycle` needs exactly 1002 filler globals on THIS build and a
different number on the next one. A fixture written on that arithmetic would stop exercising
anything the first time an unrelated pass adds a node, while claiming it still does. The
invariant lives in the code instead (`tk_dg_bucket_remove`, and the header above it), and
`tests/surface_globals.tk` is the program where the first mode was actually caught.

**Two corrections to the eighth pass's own entry**, both Copilot's:

- the module is `teko_type.tk`, not `tk_type.tk` -- the name is written correctly now here
  and in [diagnostics.md](docs/reference/diagnostics.md).
- the claim that `storeParam` and `h = ops[1]` exercise the store validator's `null` arm is
  **false**: both carry a function and a delegate value, never `null`. Measured with a print
  in `tk_deleg_store_late`'s `tk_is_null_lit` branch over all **63** fixtures -- not one
  reaches it, and none can. The arm sits behind `dsi = tk_deleg_row(ety)` (teko_heaparr.tk)
  and the validator's own `si < 0` guard, so only a NON-nullable `Op[]` element ever gets
  there, and a `null` written into one is refused one line later by `tk_deleg_coerce`
  (`teko: null needs a slot declared Op?`). The shape where the store is ACCEPTED, an
  `Op?[]` element, answers `tk_deleg_row` with -1 and never reaches this validator at all
  (probed: `Op?[] ops = new Op?[1]; ops[0] = null;` compiles and runs, and prints nothing).
  So the `// no-run` sample in [diagnostics.md](docs/reference/diagnostics.md) is the only
  cover that arm has, and the only one it can have while `null` in a delegate element is
  refused.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config mc.macos.toml`
clean; **63/63** fixtures at their `expect-exit`; `--dump-ast` of all **63** byte-identical
to `ba0dc27f` (the fix changes which pass reaches a waiting store, never the tree any
accepted program ends with); the probe corpora of passes 1 to 7 re-run whole -- **70** files
from those passes' own worktrees, plus the eighth pass's four and this pass's two, **76** in
all, each compiled and run on `ba0dc27f` and on this fix: every verdict identical except
`p9_cycle`, which is the finding (no binary at all against exit 42). `sh
scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (63/63 under `teko1`,
44.3s); `sh scripts/check-docs.sh` green (569 links, 385 diagnostics, 124 samples); `mc
limits --config mc.macos.toml` verdict `ok` on both legs -- compiler leg `nodes` 154332 ->
154421 (+89, `tk_dg_bucket_remove` and its header), `funcs` 3138 -> 3139 (+1), `globals` 926
unchanged, floor leg (`tests/hello.tk`) `passes` 15/30, `syntax` 15, `alias` 18, `types` 11,
`intrin` 8/16, heap 1114992, and the `tests/surface_datetime.tk` leg `syntax` 16, `alias`
21, `types` 14, heap 3875392 (against a 33554432-byte reservation).

**Copilot finding, tenth pass: one name, two oracles -- the check read the block and the
rewrite read the signature.** `tk_ref_pass` (teko_ref.tk) does two things to a `ref x` at a
call: it CHECKS the pointee against the parameter it is handed to (`tk_ref_arg_pointee`,
which D51's verifier finding moved onto the lexical scope this walk keeps) and, when `x` is
itself a `ref`/`out` PARAMETER of the function being walked, it REWRITES `&x` into `x` --
the repass, because the parameter's value already IS the caller's slot address. The rewrite
asked `tk_ref_param_named`, a scan of the signature's parameter list with no position in it
at all, so a local shadowing the parameter was invisible to it:

```teko
void bumpf(ref f64 v) { v = v + 1.0; }
i64 f(ref i64 x) {
    if (x > 0) {
        f64 x = 2.0;
        bumpf(ref x);        // checked as `ref f64` -- and rewritten as the OUTER `ref i64`
    }
}
```

The check accepts it, correctly: `x` there is the block's own `f64`. The rewrite then hands
`bumpf` the address the CALLER passed in, so `v = v + 1.0` writes a float over the caller's
`i64` variable and the local stays at 2.0 -- silent memory corruption from a program with no
error in it. Measured on `5b14e695`, mc **0.15.23**, macos/aarch64:

| probe | `5b14e695` | with this fix |
|---|---|---|
| `p10_shadow`: the shape above, caller's `i64 a = 7` read back after the call | exit **20** -- `a` holds 3.0's bit pattern, and the local was never bumped | exit 0 -- local 3.0, `a` still 7 |
| `p10_shadow_closed`: the same, then `bumpi(ref x)`, `x = x + 10` and `x != 18` AFTER the block | exit 20 (same corruption) | exit 0 -- the repass, the write and the read through the parameter all stand |
| `tests/surface_globals.tk` with section 6b added | exit **131** (`refshadow` returns 1: the local was not bumped) | exit 42 |

**The fix is the binding, asked once.** `tk_ref_fn` already opens the oracle's scope with
the parameters and keeps it block by block; `tk_ref_nparams` records the mark right after
`tk_ty_scope_params`, and `tk_ref_param_named` now answers the parameter only while
`tk_ty_scope_index(name)` -- the innermost entry binding that name, a new two-line accessor
beside `tk_ty_scope_find`, which is rewritten to call it -- is still below that mark. Above
it a local declared inside the body owns the name, and the name is not a repass: the `&x`
the parser built stands, which is exactly the local's address. The same guard covers the
other two rewrites this walk makes on a bare name (`x = e` into `stW(x, e)`, and a read into
`ldW(x)`), which had the identical blindness and no reported case only because a shadow of
a `ref` parameter is rare. One question, one answer, the same one the check reads.

**Copilot finding, tenth pass (suppressed): the store validator returned every `null`
unjudged, and the check that used to catch it is skipped for a waiting store.**
`tk_deleg_coerce` (teko_deleg.tk) accepted `null` unconditionally, and a ternary is coerced
branch by branch, so `ops[0] = flag ? null : null` on an `Op[]` came back untouched, the
lowering saw two arms of the same `uptr` type, and the element held a null -- `teko: call
through a null delegate`, exit 70, at the first call through it. Q1a's rule (`null` lands
only in a slot declared `T?`) reached an element store through `tk_check_field_store`
(teko_struct.tk), and the seventh pass stopped running that check over a value the store
site cannot type: a waiting store is `tk_deleg_late_do`'s alone, and that one calls this
validator and nothing else. Instrumented builds of `5b14e695`, mc **0.15.23**:

| store | trace | verdict |
|---|---|---|
| `ops[0] = flag == 1 ? null : null` | `PROBE store late=1` / `PROBE late_do i=0` / `PROBE coerce ternary` / `PROBE coerce null accepted` x2 -- no `PROBE field_store ran` | compiles, **exit 70** |
| `ops[0] = null` (no ternary) | `PROBE store late=0` / `PROBE coerce null accepted` / `PROBE field_store ran` | refused, `teko: null needs a slot declared Op?` |

The second row is also a correction to the ninth pass's own entry above, which said a
`null` written into a non-nullable element "is refused one line later by `tk_deleg_coerce`":
it is not, and never was -- the refusal came from `tk_check_field_store` beside it, which is
precisely why the shape that skips that check had none.

**The fix is Q1a's rule, asked by the validator itself**, in the same words the other two
sites give it (`teko: null needs a slot declared Op?`): `si` is a DELEGATE row and a `T?` is
a row of its own (`TK_KNULL`, which `tk_deleg_row` answers -1 for), so every `null` that
reaches this validator is one written into a slot not declared to hold it. A compiler-written
null (`tk_nl_own_null_is`, teko_null.tk) keeps the standing D41 gives it. Verdicts, same
build pair:

| probe | `5b14e695` | with this fix |
|---|---|---|
| `p10_tern_null`: `ops[0] = flag == 1 ? null : null` on an `Op[]` | compiles, **exit 70** | refuses, `teko: null needs a slot declared Op?` |
| `p10_tern_mixed`: `ops[0] = flag == 1 ? addOne : null` | refuses one pass later, `teko: the two arms of ?: have different types` | refuses at the branch, `teko: null needs a slot declared Op?` |
| `p10_bare_null`: `ops[0] = null` | refuses, `teko: null needs a slot declared Op?` | unchanged (the validator answers first now, in the same words) |
| `p10_null_sites`: `Op h = null;`, `takeOp(null)`, `return null;` from an `Op` | refuses, `teko: null needs a slot declared Op?` | unchanged |
| `p10_nl_array`: `Op?[] ops = new Op?[1]; ops[0] = null; ops[0] = c ? null : null;` | runs, 42 | runs, 42 -- the element type answers `tk_deleg_row` with -1 and never reaches the validator |

The last row is the accepted side of the rule, and the array [arrays.md](docs/reference/arrays.md)
now names: an `Op[]` element is declared `Op`, an `Op?[]` element is the slot a `null`
belongs in. The refused shapes cannot be a fixture (a refusal has no exit code), so they are
`// no-run` samples in [diagnostics.md](docs/reference/diagnostics.md), checked by
`scripts/check-docs.sh` like every other one.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config mc.macos.toml`
clean; **63/63** fixtures at their `expect-exit`, `tests/surface_globals.tk` (exit 42)
gaining section 6b, `refshadowcheck` -- the first executable cover for a local shadowing a
`ref` parameter, and 131 on `5b14e695`; the seven probes above, each run on both builds;
the **76** probes of passes 1 to 9 re-run whole on both builds -- every verdict identical,
line for line. `--dump-ast` of **62** of the 63 fixtures byte-identical to `5b14e695`, the
63rd being `surface_globals.tk`, whose dump grows by exactly the 70 lines of the two new
functions and loses none: inside the block `bumpf(ADDR name=x)` (the local's own address),
after it `bumpi(IDENT name=x)`, `st64(x, ld64(x) + 10)` and `ld64(x)` -- the repass, the
write and the read through the parameter, unmoved. `sh scripts/bootstrap.sh --os macos
--arch aarch64` -> `FIXPOINT OK` (63/63 under `teko1`, 41.2s); `sh scripts/check-docs.sh`
green (573 links, 385 diagnostics, 125 samples -- one no-run sample added); `mc limits
--config mc.macos.toml` verdict `ok` on both legs -- compiler leg `nodes` 154421 -> 154484
(+63, `tk_ty_scope_index`, the guard and their headers), `funcs` 3139 -> 3140 (+1,
`tk_ty_scope_index`), `globals` 926 -> 927 (+1, `tk_ref_nparams`), floor leg
(`tests/hello.tk`) unchanged at `nodes` 38, `funcs` 2, heap 1114992.
`mc pkg hash .` at this pass's own code commit:
`184b554c9ca066b0bf0d3997626e08387f10d40daeb5943a400341acd88b51a2`.

**Copilot finding, eleventh pass: one store took two rows, one name had two
ceilings, and one nested call had no oracle at all.** Three defects, each
measured on `dd489bbe` (the head this fix sits on) and on this fix, mc
**0.15.23**, macos/aarch64. The fourth reported finding is not this crumb's and
is answered below.

1. **A GLOBAL element store spent TWO rows of `TK_MAXOS`** (128,
   teko_struct.tk). `tk_ha_store` (teko_heaparr.tk) marks the store it builds
   (`tk_os_mark`), and `tk_hg_resolve_write` (teko_array.tk) copies that store
   into the placeholder node the tree already holds (`node_assign`) and then
   marked the placeholder as well -- so the first row stayed on the node it had
   just dropped, an orphan the reclaim pass can never reach, and every counted
   store into a global array cost two rows instead of one. The ceiling for a
   global array was therefore 64, not 128: the 65th store was refused
   `teko: too many stores into a slot of class type` on a program with no error
   in it. The fix is the re-point the site already makes for the OTHER table it
   feeds: `tk_os_move(from, to)` (teko_struct.tk), beside `tk_os_mark`, exactly
   as `tk_deleg_late_move` sits beside `tk_deleg_store_defer` -- one line below
   it, on the same two node ids.
2. **`tk_ty_scope_add` (teko_typeof.tk) stopped writing in silence.** Its table
   held 256 names of one function while the parser accepts `TK_MAXSLV` (8192,
   teko_struct.tk) locals, and past that mark it simply returned -- so the
   declaration the source wrote last was missing from the one oracle this crumb
   has spent ten passes making single. Every reader then answered about the
   WRONG declaration: with 255 locals ahead of it, an `f64 x` shadowing a
   `ref i64 x` parameter was invisible, `bumpf(ref x)` inside its block was
   refused `teko: a value of type i64 does not convert to f64` on a legal
   program, and `tk_ref_param_named` (teko_ref.tk, the tenth pass's own
   binding) read the parameter for the same name -- one step from the repass
   that hands the callee the caller's `i64` slot to write a float through.
   Two things fix it, and both are the same rule: the ceiling is the PARSER's
   own (`#define TK_MAXSCOPE TK_MAXSLV`), so a body the parser accepted always
   fits, and the overflow is a REFUSAL (`teko: too many locals in one
   function`), because a table that cannot record a name may say so and may
   not answer -1 about a declaration that is right there. With one ceiling the
   refusal is reachable only through the temporaries the compiler declares
   itself (teko_null.tk, teko_ternary.tk): a body with 8191 locals is refused
   by the parser first, `teko: too many locals in one unit` (measured).
3. **`tk_ov_arg_ty` (teko_over.tk) had no answer for a call through a delegate
   slot.** Which overload a call picks depends on typing its arguments, and
   this pick is asked DURING `tk_deleg_walk` -- `tk_deleg_coerce` ->
   `tk_deleg_expr_ty` -> `tk_ty_of` -> the matcher -- which stands on the store
   BEFORE it descends into the value, so a nested `chooser(1)` has not been
   rewritten yet. The matcher asked `tk_ty_of`, which asks `decl_find` about a
   name no declaration owns: -1, no row matched, and the value came back
   untyped. `ops[0] = make(chooser(1))` (`make` overloaded, `chooser` a local
   delegate) was refused `teko: Op takes a function, another Op, or null` on a
   legal program, in EITHER order of the two rows -- and so was
   `Op h = make(chooser(1))`, a plain local initializer, which is why the fix
   is not in `tk_deleg_late_do`: recursing into the arguments of a waiting
   store would have left every other caller of the validator exactly as
   broken. The matcher asks `tk_deleg_expr_ty` for a call no overload table
   names, which is the same peek the validator one frame up already takes; once
   the walk HAS rewritten the call, its name is the built one, no slot answers
   for it, and the answer is the `tk_ty_of` it always was.

| probe | `dd489bbe` | with this fix |
|---|---|---|
| `p11_os_g64`: 64 stores into a global `Op[]` | 42 | 42 |
| `p11_os_g65`: 65 of them | refused, `teko: too many stores into a slot of class type` | 42 |
| `p11_os_g128`: 128 of them | refused, same words | 42 |
| `p11_os_g129`: 129 of them | refused | refused -- the ceiling, where the table says it is |
| `p11_os_l128` / `p11_os_l129`: the same into a LOCAL `Op[]` | 42 / refused | unchanged |
| `p11_scope254`: 254 locals, then `bumpf(ref x)` under an `f64 x` shadowing a `ref i64 x` | 42 | 42 |
| `p11_scope255` / `p11_scope300`: 255 and 300 locals | refused, `teko: a value of type i64 does not convert to f64` | 42 |
| `p11_scope8191` / `p11_scope8300` | refused, `teko: too many locals in one unit` | unchanged -- the parser's own ceiling answers first |
| `p11_ovcall_a` / `p11_ovcall_b`: `ops[0] = make(chooser(1))`, the two rows in either order | refused, `teko: Op takes a function, another Op, or null` | 42 |
| `p11_ovcall_c3`: `Op h = make(chooser(1))`, a local initializer | refused, same words | 42 |
| `p11_ovcall_c1` (`make` declared once), `c2` (a literal argument), `c4` (the pick at the overload pass, after the walk) | 42 | 42 |

**The fourth finding is #698's, and the merge order is the answer.**
`i64?[] xs; i64 g = 5; xs[0] = g;` wrote the global's raw 5 into a slot that
holds a box, because `tk_ha_store` asked `tk_pty_of` (teko_struct.tk, a
parse-time table of declarations) for the value's type and got -1, so
`tk_nl_wrap` handed the value back unwrapped. Measured on three builds: it
segfaults (exit **139**) on `dd489bbe`, on a GLOBAL `i64?[]` and on a LOCAL one
alike -- so the report's "global" is not the boundary, the oracle is -- and it
runs, 42, on the head of **#698** (`a0b9388c`), whose `tk_ha_store` is one call
to `tk_field_store_val` (teko_typeof.tk): the deferral, the row verdict and
Q1b's box, all through the one door D50 gave every field store. Fixing it here
would mean writing a second gate into the body #698 deletes. It is left to
#698, and **#698 merges first**; this crumb touches `tk_hg_resolve_write` and
`tk_ov_arg_ty`, neither of which #698 edits.

**And what the fifth finding corrected, all of it prose**: the header over
`tk_deleg_store_late`'s own caller (teko_deleg.tk) still described the sixth
pass's rule -- "a name a `new Op(...)`/lambda/call already types ... decided
where they were written" -- where the seventh pass made every CALL wait, a
`new Op(...)`, a lambda and a ternary among them; `tk_ref_param_named`'s header
still named "a scope table filled past `TK_MAXSCOPE`" as a way its lookup
answers -1, which finding 2 above removes; `paniced` -> `panicked` in
teko_deleg.tk and in [diagnostics.md](docs/reference/diagnostics.md); and
`tests/surface_globals.tk`'s own section 3f header, where "a pick that does not
settled the store unchecked" said neither of the two things it meant.

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config
mc.macos.toml` clean; **63/63** fixtures at their `expect-exit`,
`tests/surface_globals.tk` (exit 42) gaining sections 3h (`delegovcallcheck`:
the overloaded call over a nested delegate call at a local element store, at a
local initializer and at a global element store) and 3i (`osceilcheck`: 65
stores into a global `Op[]`, and the sum they call back) -- the head this fix
sits on refuses BOTH, `teko: too many stores into a slot of class type` at 3i
and `teko: Op takes a function, another Op, or null` at 3h, measured one
section at a time; the **83** probes of passes 1 to 10, re-run whole on
`dd489bbe` and on this fix -- every verdict identical, line for line -- plus
this pass's own 25; `--dump-ast` of all **63** fixtures byte-identical to
`dd489bbe` except `surface_globals.tk`, whose own dump grows by its two new
sections and whose only other movement is the gensym counter renumbering
($g40 -> ...) that any added code causes, and **61** of the 62 fixtures that
existed at `da33ebd4` byte-identical to that base, with
`tests/surface_array_heap.tk` exactly as the sixth pass declared it (the four
declarations of a wrap, emitted at a different point of the unit, none added
and none lost). `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (63/63 under `teko1`, 40.2s); `sh scripts/check-docs.sh` green
(573 links, **386** diagnostics -- the capacity refusal above is the new one --
125 samples); `mc limits . --config mc.macos.toml` verdict `ok` on both legs:
compiler leg `nodes` 154484 -> 154523 (+39), `funcs` 3140 -> 3141 (+1,
`tk_os_move`), `globals` 927 unchanged, `ins` 212689 -> 212748, used heap
92492880 -> 93221888 of a 220004352 reserve; the `tests/hello.tk` leg's
structural counts byte-identical (`nodes` 38, `funcs` 2, `ins` 35, `passes`
15/30, `syntax` 15, `alias` 18, `types` 11, `intrin` 8/16) with its own heap
467824 -> 1114992 of a 33554432 ceiling (3.3%) -- a STEP in mc's own estimator
and not the table's size: `TK_MAXSCOPE` at 1024 and at 8192 both measure
1114992, which is D35's own precedent read back. `mc pkg hash .` at this pass's
own code commit:
`71ec8182830b155bb163a17ff0c52631011f15f168c776e22622d5a06e219602`.

**D50 ∧ D51, rebased onto `13b3c38a`.** This entry was written against
`da33ebd4`; D50 ([#698](https://github.com/teko-org/teko-lang/pull/698)) landed
first, as the fourth finding above said it would, and the two meet at exactly
one body, `tk_ha_store` (teko_heaparr.tk). D50 made every element store ONE
call to the door `tk_field_store_val` (teko_typeof.tk) -- the deferral, the two
scalar verdicts, the row one and Q1b's box; the seventh pass of this entry made
a store the site cannot settle the DELEGATE validator's alone, because the
generic row check types a name and a call from the same parse-time tables the
`late` guard just refused to trust. Both rules stand, in this order: the
coercion runs first for a value the site CAN name, and the door is asked only
when the store does not wait --
`if (dsi >= 0 && !late) v = tk_deleg_coerce(...); if (!late) v =
tk_field_store_val(...);`. A waiting store keeps its single judgement at
`tk_deleg_late_do` (`tk_deleg_pass`, 7), which runs BEFORE the door's own judge
(`tk_field_store_judge`, at the end of `tk_over_pass`, 14), so the wrap the
walk splices in is what the tree holds either way and no deferred row is left
pointing at a node a later pass replaced. `tk_hg_resolve_write` (teko_array.tk)
keeps this entry's `tk_os_move`/`tk_deleg_late_move` over D50's `tk_os_add`
re-mark -- the rows the built store owns are MOVED onto the node the tree
keeps, one rule for both tables -- and a global element store of any type that
is NOT a delegate still goes through D50's door whole, since
`tk_deleg_store_late` answers 0 for `si < 0`. That is what makes the fourth
finding run: `i64?[] xs; xs[0] = g;` with `g` a global reads **42** on the
rebased head, on a GLOBAL `i64?[]` and on a LOCAL one alike (it segfaults, 139,
on `dd489bbe`) -- #698's door and this entry's write, cooperating.

Proof on the new base, mc **0.15.23** (`MC_VERSION`), macos/aarch64. The
eleven key probes of the two families, each at its verdict: (D51) the global
`i64?[]`/local `i64?[]` store above, an `f64 x` shadowing a `ref i64 x`
parameter, a global `Op` into a global `Op[]`, `ops[0] = chooser(1)` through a
local delegate, `ops[0] = make(chooser(1))` and `Op h = make(chooser(1))` over
two overloads -- 42 each -- and a by-reference lambda into a global `Op[]`
refused `teko: a lambda that captures by reference cannot leave its scope`;
(D50) `this.rate = k` on a widened `f64` field, `h.count = k` into an `i64?`
field through a receiver the parser cannot type, `xs[0] = rick(1, 2)` picking
the second declaration of an overloaded name into a `Cell[]`,
`h.items[gl[0]] = 9` reading a global element inside a store, and
`dst[0] = src[0]` under a local shadowing a global `T[]` -- 42 each. **64/64**
fixtures at their `expect-exit` (the 63 of `13b3c38a` plus
`tests/surface_globals.tk`); `--dump-ast` of those 63 against `13b3c38a`: **62
byte-identical**, and `tests/surface_array_heap.tk` exactly as the sixth pass
declared it -- 2439 lines on both builds, the same multiset line for line, the
four declarations of a wrap emitted at a different point of the unit.
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (64/64
under `teko1`, 39.5s); `sh scripts/check-docs.sh` green (572 links, 388
diagnostics, 130 samples); `mc limits . --config mc.macos.toml` verdict `ok` on
both legs, the `tests/hello.tk` leg's structural counts unmoved from D50's own
measurement (`passes` 15/30, `syntax` 15, `alias` 18, `types` 11, `intrin`
8/16, heap 1114992 of 33554432) and the `tests/surface_datetime.tk` leg at
`syntax` 16, `alias` 22, `types` 15, heap 3998976 -- two rows this entry's own
code does NOT move. The sentence here used to attribute the `+1` on `alias`
and the `+1` on `types` to "this entry", against "D50's 21 and 14"; both halves
were wrong, and three builds say so (the twelfth pass below re-measured them
back to back, mc 0.15.23, macos/aarch64): `da33ebd4`, the base BEFORE D50,
reads **21**/**14**; `13b3c38a`, D50 alone, already reads **22**/**15**; this
crumb's head reads 22/15 too. **D50 is the single cause of both rows**, and the
21/14 the ninth pass recorded is the figure of a head that predates it.
`mc pkg hash .` over the rebased tree:
`433973c4026dd0f10a917dff5e8d53e24624ac09ddd545c3fa80ac7037870ba1`.

**Copilot finding, twelfth pass: a delegate call read the TAG of a `ref`
argument and never the type behind it, a `?` on a slot hid the escape rule
from two sites, and the pass-time scope was one table short of the signature
it holds.** Four findings, each measured on `cdcd8a6d` (the head this fix sits
on) and on this fix, mc **0.15.23**, macos/aarch64 -- `Mut` is
`delegate void Mut(ref f64 x)`, `Op` is `delegate i64 Op(i64 a)`, `g` an `i64`
and `acc` a local a lambda captures by reference:

| probe | `cdcd8a6d` | now |
|---|---|---|
| `Mut m = bumpf; i64 g; m(ref g);` on a LOCAL slot | compiles, the `i64` comes back holding a double, **exit 9** | refuses, `teko: a value of type i64 does not convert to f64` |
| the same through a GLOBAL `Mut` over a global `i64` | compiles, **exit 9** | refuses, the same words |
| `bumpf(ref g)` written DIRECTLY (the wording it is measured against) | refuses, the same words | unchanged |
| `m(ref v)` / `mi(ref n)` with the pointee the signature declares | runs | runs |
| `Op? g; g = new Op((i64 x) use (&acc) => acc + x);` | compiles, the capture outlives `acc` | refuses, `teko: a lambda that captures by reference cannot leave its scope` |
| the same into a plain `Op` global | refuses, the same words | unchanged |
| `Op? h;` LOCAL taking that lambda | runs | runs |
| `xs[0] = new Op(... use (&acc) ...)` on a LOCAL `Op?[]` | compiles | refuses, the same words |
| the same on a GLOBAL `Op?[]` | compiles | refuses, the same words |
| the same pair on an `Op[]`/global `Op[]` | refuses | unchanged |
| `Op?[]` taking `new Op((i64 x) => x + 1)` and `null`, local and global | runs | runs |
| `Op[] xs; xs[0] = null;` | refuses, `teko: null needs a slot declared Op?` | unchanged |
| `TK_MAXSLV` locals + 1 parameter in one function | refuses, `teko: too many locals in one function` | the pass is silent; mc's own `frame too large` answers |

1. **A call through a DELEGATE compared the argument's `ref`/`out` TAG and
   nothing else.** `tk_deleg_check_arg_kinds` (teko_deleg.tk) read `dg_pk_at`
   -- is this argument tagged the way the signature says? -- and never
   `dg_pty_at`, the type the callee writes THROUGH that address. Nothing else
   was going to: the `callp` a delegate call lowers to is built in
   `tk_deleg_pass` (7), AFTER `tk_ref_pass` (6) whose own call check would
   have asked, and it names no callee, so `tk_rc_call_args` (teko_rc.tk) has
   no declaration to read either. That check is the only door the argument
   passes, and it was open: `Mut m = bumpf; i64 g = 7; m(ref g);` ran
   `v = v + 1.0` through an integer's address and the integer came back
   holding 8.0's bit pattern -- a local slot and a global one alike, and the
   same program written as the DIRECT `bumpf(ref g)` is refused. The rule is
   the one that already owns pointees, IDENTITY
   (`tk_ref_check_pointee`, teko_ref.tk, D51's verifier finding): it is split
   over the pointee TYPE rather than the parameter NODE
   (`tk_ref_check_pointee_ty`, two lines and one caller rewritten), because a
   delegate signature keeps its pointees in a flat column and has no N_PARAM
   to hand over. The oracle underneath is unchanged and shared
   (`tk_ref_arg_pointee`: the lexical scope live at the site, then the
   globals), so a delegate LOCAL, PARAMETER or GLOBAL called by name is judged
   whole -- `tk_deleg_pass` opens that scope with the signature and keeps it
   live, exactly as `tk_ref_pass` does. **What is NOT closed, measured and
   stated rather than patched:** the two roads whose `callp` is built at PARSE
   time, a delegate FIELD (`tk_field_use`, teko_expr.tk) and an `Op[]` ELEMENT
   (`tk_ha_deleg_call`, teko_heaparr.tk), stand where the pass-time scope is
   empty and `tk_ty_global` is not filled yet (`tk_hg_collect` runs in
   `tk_array_pass`, 5), so the pointee answers -1 there and is skipped in
   silence -- the same answer `tk_ref_check_pointee` gives any pointee no
   oracle names, and the one sentence its header has carried since the
   verifier finding. Reaching them needs the store's own device, a row that
   waits for a walk, and that is a crumb, not a line.
2. **`tk_deleg_row` answers -1 for `Op?`, and two sites read that -1 as "this
   slot holds no delegate".** `Op?` is a `TK_KNULL` row of its own
   (teko_struct.tk), and the guard the escape rule sits behind asked exactly
   that question: `tk_deleg_assign` (teko_deleg.tk) returned before reaching
   D221 decision 21's first escape, so `Op? g; g = new Op((i64 x) use (&acc)
   => acc + x);` and `g = local` parked the ADDRESS of a dead local in a
   global -- the very write the `Op` beside it is refused for since the fifth
   pass. `tk_deleg_row_under` peels the `?` and is read at the escape question
   and nowhere else: a `?` says the slot may hold NOTHING, never that it lives
   LESS LONG, which is the only thing that rule asks. The coercion keeps
   reading `tk_deleg_row`, which is what leaves `Op? g = null;` the nullable
   row's own business and `Op[] xs; xs[0] = null;` refused.
3. **The element store had the same -1, and its global road had nowhere to ask
   the question at all.** `tk_ha_index` (teko_heaparr.tk) guards the parse-site
   escape with the same call, so an `Op?[]` element took the capture too; and a
   GLOBAL array's store is assembled inside `tk_array_pass`
   (`tk_hg_resolve_write`, teko_array.tk), where the rule cannot run -- the
   fourth pass's own finding, whose deferral is reached only when
   `tk_deleg_store_late` answers 1, which it cannot for a row it reads as -1.
   The parse site reads `tk_deleg_row_under`, and the global store defers the
   ESCAPE ALONE, with `dl_si` = -1: `tk_deleg_late_do` takes the escape from
   the one walk that knows which function owns the store and then returns,
   because the `null` and the type of that element were already judged by
   D50's own door where the store was built (`tk_field_store_val`) and
   `tk_deleg_coerce` would refuse the very `null` an `Op?[]` element is
   declared to hold. One rule per question, at the site that can answer it.
4. **`TK_MAXSCOPE` was `TK_MAXSLV`, and the scope holds the PARAMETERS too.**
   The eleventh pass tied the pass-time scope's ceiling to the parser's own so
   that "a body the parser accepted always fits"; `tk_ty_scope_params`
   (teko_typeof.tk) pushes the whole signature into that same table before the
   body is walked, and no parse-time table counts a parameter (`tk_slv_add`'s
   own rule, D33). At `TK_MAXSLV` locals exactly the invariant broke: 8192
   locals is accepted by the parser -- 8193 is its own
   `teko: too many locals in one unit` -- and one parameter beside them made
   the pass refuse with a ceiling of its own,
   `teko: too many locals in one function`. It is `TK_MAXSLV + MAXPARAMS`
   now, mc's own ABI ceiling on a signature added to the parser's own on
   locals, 192 bytes of globals for the two columns. **What this does not
   buy, measured:** mc refuses a function with **510** locals outright,
   `frame too large` (509 + 1 parameter is the last that compiles), so no
   program that BUILDS can reach either ceiling -- what the fix removes is a
   refusal in teko's own voice for a body teko's own parser accepted, and the
   boundary probe now reaches a high-water of **8193** scope entries with the
   pass silent. The refusal stays reachable, and only for what it was written
   for: the temporaries the compiler declares itself (teko_null.tk,
   teko_ternary.tk), which no source count bounds. A throwaway counter on
   `tk_ty_scope_add`, printed at the last pass over all 64 fixtures, says the
   busiest body spends **72** of the 8204 rows (`surface_nullable_ops`).

Proof, mc **0.15.23** (`MC_VERSION`), macos/aarch64: `mc build . --config
mc.macos.toml` clean; **64/64** fixtures at their `expect-exit`, with
`tests/surface_globals.tk` (exit 42) gaining section 8, `delegrefcheck` -- the
pointee the signature declares, `ref f64` and `ref i64`, through a delegate
LOCAL and a delegate GLOBAL -- and section 9, `nulldelegcheck` -- an `Op?`
global and an `Op?[]` element, global and local, taking a lambda with NO
capture and the `null` a `?` is declared for, read back through
`== null`/`!= null`. Both sections are the ACCEPTED side on purpose: every
refusal of this pass is a refusal, which has no exit code, and the four of
them are `// no-run` samples in
[diagnostics.md](docs/reference/diagnostics.md) instead. **No new `teko:`
string**: the pointee mismatch is the wording a direct call already gives and
the escape is the sentence five slots already carry. The **141** probes of
this crumb -- the **83** of passes 1 to 10, the **25** of the eleventh, the
**11** of the two families D50 ∧ D51 measures and this pass's own **22** --
run whole on `cdcd8a6d` and on this fix: **135** verdicts identical line for
line and **6** that move, which are the four findings above and nothing else.
`--dump-ast` of all **64** fixtures under the compiler of `cdcd8a6d` and under
this one, over the SAME sources -- **64 byte-identical**, the whole code change
being refusals and one capacity; against `13b3c38a`, **62** of the 63 fixtures
that exist on both are byte-identical and `tests/surface_array_heap.tk` is
exactly as the sixth pass declared it (2439 lines on both, the same multiset
line for line, one contiguous 88-line block of wrap declarations emitted at a
different point of the unit), `tests/surface_globals.tk` being this crumb's
own. Instrumented, `tk_deleg_late_rest` refusing any store that reaches it
unjudged: the 64 fixtures and all 141 probes pass with **0** survivors, and the
instrument is not vacuous -- with `tk_deleg_late_pend` disabled it fires on
`tests/surface_globals.tk:228` at once. `sh scripts/bootstrap.sh --os macos
--arch aarch64` -> `FIXPOINT OK` (64/64 under the self-hosted `teko1`, 58.5s);
`sh scripts/check-docs.sh` green (**573** links, **388** diagnostics -- none
added -- **132** samples, the two `// no-run` blocks this pass writes: the
delegate `ref` mismatch beside the direct call's own, and the `T?` slot's
escape beside the plain global's). Two rows join
[not-yet.md](docs/reference/not-yet.md), both measured here: `??` over a `T?`
of delegate type is refused `teko: Op takes a function, another Op, or null`,
and a bare FUNCTION name stored into a `T?` delegate slot is `unknown name`
from the core at an initializer and `teko: the type of this value is not known
here` at an element store -- the wrap reads the delegate row, which a `T?`
answers -1 for, and `new Op(addOne)` is the form that works today.
`mc limits . --config mc.macos.toml` verdict `ok` on both legs, measured back
to back against `cdcd8a6d` from the same clean `build/`: the `tests/hello.tk`
leg is BYTE IDENTICAL down to its `heap` (467824 of a 33554432 ceiling;
`passes` 15/30, `syntax` 15, `alias` 18, `types` 11, `intrin` 8/16) and the
compiler leg moves by this pass's own source growth (`nodes` used 155312 ->
155398, `funcs` 3157 -> 3159 -- `tk_ref_check_pointee_ty` and
`tk_deleg_row_under` -- `globals` 933 unchanged, `ins` 213918 -> 214057,
`symbols` 6189 -> 6191, used heap 93817520 -> 93957712). The
`tests/surface_datetime.tk` leg is identical on both heads as well (`syntax`
16, `alias` 22, `types` 15, heap 3026144), which is the measurement the
correction to the D50 ∧ D51 paragraph above rests on: `da33ebd4` reads 21/14,
`13b3c38a` reads 22/15, and this crumb changes neither. `mc pkg hash .` at
this pass's own code commit:
`93fab590c5fd29dc2ceaabab56ba296e39d41044b3b6e38bd6ee36861d45ed6e`.
### D52 · A refusal has a harness (2026-09-14)
D33 closed the integer-to-float narrowing refusal and, in the same entry, named the gap:
`tests/` held only programs that compile and run, judged by `// expect-exit`, and a
refusal — a `teko:` message and the line it is raised at — had no oracle of its own. Every
crumb since has paid the same tax: a `docs/reference/diagnostics.md` or `docs/specs/*.md`
entry fenced `// no-run`, a claim in prose ("measured on `b48d465c`") that nothing after
that commit re-checks, and a growing set of throwaway probes outside `tests/` that a scout
has to re-measure by hand every time. This crumb closes it, in the harness's own words
rather than a new one.

**One corridor, one script.** `scripts/fixtures.sh <compiler> <config-base> [exe-suffix]`
is what four call sites used to repeat with small, driftable variations —
CONTRIBUTING.md's own recipe, `scripts/bootstrap.sh`'s criterion 3, and the CI leg's
"primitives, types and surface fixtures" step, itself a THIRD glob
(`tests/primitives_*.tk tests/types_*.tk tests/surface_*.tk`) that silently skipped
`tests/order_*.tk` — the two `order_*` fixtures ran under `scripts/bootstrap.sh`'s
`tests/*.tk` loop (the `fixpoint` job) but never in the five-leg `ngen (<os>/<arch>)` job
itself, the only one the `main` ruleset's aggregator actually requires. The script now
reads every fixture directly under `tests/` (no glob, no exclusion) and both fixture kinds
through the same loop shape derive/build/judge already had.

**The second kind.** `tests/refuse/*.tk` carries a two-line header —
`// expect-refuse: teko: <the exact message>` and `// expect-refuse-line: <the exact
line>` — and the script demands the build FAIL (a build that succeeds where a refusal was
named is itself the failure) with that exact `:<line>: teko: <message>` substring in the
compiler's own stderr, CR-stripped for the Windows legs and matched with `grep -F` so a
message carrying `?`, `[` or `.` is read literally rather than as a pattern. Message and
line are BOTH checked, deliberately: measuring a handful of constructs beforehand showed
the two can move independently — the same wording at a line the source did not raise it on
is as wrong a proof as the right line with stale words, and a diagnostic completed by an
appended name (`` "teko: method of `" `` + `I` + `" not implemented"` ) only reads right as
the ONE literal line the compiler actually writes, not the two halves `diagnostics.md`
quotes apart. A `.tk` under either directory missing its header(s) fails the run outright
instead of being silently skipped — a harness that can be starved of its own oracle by a
typo is not a harness.

**Fifteen fixtures, one per permanent law**, deliberately NOT the two provisional
restrictions `docs/reference/not-yet.md` still lists as "for now"
(`i64? == 5`, `items[0]` with no `this`) — a harness fixture is a regression lock, and
locking a restriction the language intends to lift is exactly the debt this crumb should
not add. The fifteen, each measured against `build/teko` at `13b3c38a` before being
written down: an `f64` narrowed into `i64` by assignment (`narrow_assign.tk`), `null` into
a plain reference slot (`null_nonnullable.tk`), a non-null reference into a numeric slot
(`ref_into_numeric.tk`, D34), a plain integer into an `enum` slot (`enum_from_int.tk`),
`+` over two `enum` members (`enum_plus.tk`), a global `T?` over a value type
(`nullable_global_value.tk`, D44), a field declared `void` (`field_void.tk`), a `struct`
local read through before `new` (`struct_no_new.tk`, D46 — the same rule a plain scalar
local gets), a `void` call's result stored into a typed field (`field_store_void.tk`,
D50), narrowing through a static field (`static_field_narrow.tk`), through `this`
(`this_field_narrow.tk`) and through a heap array element (`array_elem_narrow.tk`) — three
fixtures over the one gate D50 unified, each proving a different call site still reaches
it — narrowing through `return` (`return_narrow.tk`), instantiating an `abstract class`
(`abstract_new.tk`) and a class declaring an interface it does not fully implement
(`interface_missing.tk`).

**Proof:** `scripts/fixtures.sh ./build/teko mc.macos.toml` → **63 passed, 15 refused as
expected, 0 failed**; `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK`,
the same corridor under the self-hosted `teko1`; `sh scripts/check-docs.sh` green (570
links, 387 diagnostics, 122 samples); `mc limits . --config mc.macos.toml` verdict `ok`,
every table unmoved (`passes` 15/30, `types` 11, `intrin` 8/16, `alias` 18); `--dump-ast`
of all 63 `tests/*.tk` fixtures byte-identical between `13b3c38a` and this head — no
`teko*.tk` module touched, so the proof is trivial by construction and confirmed instead
of assumed; `mc pkg hash .` unchanged
(`87e627f70123afa56da2f314ba445066e265890594029cc694fecfbbb28f9595`) — `mc.toml`'s `files`
lists no script, test or doc. A mutation drill on the harness itself: the message, then the
line, then the whole diagnostic of one refuse fixture were each corrupted in turn, and
`scripts/fixtures.sh` failed the corridor each time with the exact stderr it read printed
alongside — the third mutation (swapping the `teko:` refusal for an unrelated core error,
`unknown name`) is the harness proving it does not accept ANY build failure as a pass, only
one whose stderr carries the named diagnostic.

**The second direction, one step later.** `scripts/check-docs.sh` step 4 proves every
`"teko: …"` literal in the sources is on the diagnostics page; step 4b now proves every
`// expect-refuse:` message under `tests/refuse/` is composed from a documented literal — the
longest string literal of the compiler (10+ characters) found inside the message has to be
on the page, so a fixture whose message drifted from the sources, or a refusal the page never
names, fails the `docs` gate. The literals are read from `teko*.tk` as whole string tokens,
never by a length-bounded pattern: a pattern such as `"[^"]{10,}"` skips a short literal and
then pairs its closing quote with the next literal's opening one, and the composed message
`"teko: " + name + " is used before it is assigned"` was exactly the case that exposed it.
Drill: replacing one fixture's message tail with an undocumented word fails the check;
restored, `docs ok: 569 links, 387 diagnostics, 15 refusals, 122 samples`.

---

**The fragment check, one step later still.** The site's own `mcsite --check` refused two
pull requests in a row on something `scripts/check-docs.sh` had passed: a relative link
leaving `docs/` (#700) and a `#slug` whose heading had been renamed (#703,
`docs/reference/diagnostics.md:303` pointing at `#primitives-with-members-timespan-datetime`
after the heading gained `DateOnly`). Step 1b now applies the generator's own rule to every
`#slug` on a link, in-page or cross-page: a heading's id is its text lowercased, every run of
characters outside `[a-z0-9_]` one dash, no dash at either end, and a repeated id numbered
`-2`, `-3`, ... in page order (`u_slug` and `md_unique_id` in mc's `site/gen`). Drills: the
#703 rename fails naming the page and the slug; a cross-page fragment with one letter added
fails the same way; restored, `docs ok: 575 links, 26 fragments, 388 diagnostics, 21
refusals, 133 samples`.

### D53 · A global SCALAR slot takes the same assignment corridor as a local (G-b, 2026-09-14)
D48 gave a global a TYPE (`tk_ty_global`, teko_array.tk, over the slot table `tk_hg_collect`
records) and D51 gave every by-name oracle a fallback into it. What neither gave it is the
CHECK: the corridor a value crosses on its way into a slot -- `tk_check_compat` /
`tk_num_widen` / `tk_nl_wrap` (teko_typeof.tk, teko_null.tk) -- ran on a local, a field, an
argument, an array element and a `return`, and on a global it ran nowhere. Eight shapes
measured on the base (`84c63025`), every one of them compiling, every one of them refused
or converted on the identical local:

- `f64 gw = 1;` -- `gw` held the raw integer bits (`GLOBAL type=f64 name=gw` /
  `INT val=1 type=i64`), so the guard `gw > 0.9 && gw < 1.1` failed. A local `f64 x = 1;`
  is one point zero (D33).
- `f64 gw; ... gw = 5;` from inside a body -- the same raw store.
- `i64 gn = 1.5;` and `gn = 1.5;` -- accepted; the local refuses,
  `teko: a value of type f64 does not convert to i64`.
- `Cell gc; ... gc = null;` -- accepted; the local refuses,
  `teko: null needs a slot declared Cell?` (D43).
- `i64 gn; ... gn = someCell;` -- accepted; the local refuses (D34).
- `Color gk = 1;` and `gk = 1;` -- an enum out of a bare integer, accepted.
- `Cell gc; Box b; gc = b;` -- two unrelated rows, accepted.
- `i8 g8 = 1.5;` -- accepted.

**ROOT: two doors, one of them shut and the other never cut.** `tk_rc_assign` (teko_rc.tk)
opened with `i64 li = tk_rc_index(nd_name(n)); if (li < 0) return;` -- a global is in no
pass-time scope, so it answered -1 and the function returned ABOVE the three shared rules
rather than below them. The early return becomes a fallback: the slot's type is
`sc_ty_at(li)` when the name is a local or a parameter and `tk_ty_global(nd_name(n))`
otherwise, and the `li < 0` return moves down to just above the rc lowering, which is
untouched and still needs a scope slot of its own (the address it stores through, the
borrowed-parameter rule). The ORDER is lexical, and it is the order D51 already settled:
`tk_rc_index` walks the scope stack and holds the parameters below its floor, so the global
is reached only when no local and no parameter of that name is in scope --
`i64 paramcheck(i64 gw)` beside an `f64 gw` at file scope stores an integer 5, and a field
`n` written implicitly inside a method is a field store that never reaches this function at
all (proved in both declaration orders, the class above the global and below it -- D50's
pass 3 found real shadow hazards on the field-store gate, so it is a fixture, not an
argument). `tk_nl_wrap` is a no-op here by construction: a global declared `T?` over a
VALUE is already refused at pass 5 by `tk_gs_check` (teko_array.tk, D44).

A global's own INITIALIZER had no door at all. The two sweeps that walk an `N_GLOBAL`
(`tk_garr_collect`, `tk_hg_collect`) only record rows, and nothing else in fifteen passes
looked at one. `tk_rc_global` is **one clause in `tk_rc_pass`'s own top-level loop**, beside
the `N_FUNC` one -- no new pass, `tk_compat_needed()` already answers 1 for every unit, and
`passes` stays 15/30. It skips the two shapes that are not a slot with a value: `nd_val != 0`
is an array (and the shape every GENERATED global takes -- an enum's element table writes
`nd_val = cnt`, teko_enum.tk, and `tk_glb`'s vtable globals carry no `nd_a`), `nd_a == 0` is
a declaration with nothing to judge. The value needs no deferral machinery: a global
initializer is constant by mc's own rule, so `tk_pty_of` (teko_struct.tk) reads its type off
the folded node.

**The widen at an initializer is a LITERAL REWRITE, not a cast.** `tk_num_widen` writes an
`N_CAST`, and mc's core refuses one at file scope -- `f64 g = (f64) 1;` dies with `global
initializer must be constant`, measured on mc 0.15.23, which is also why no `N_CALL`, no
`N_IDENT` and no `N_CAST` can ever be a global initializer in the first place. So
`tk_rc_glb_widen` converts the literal where the literal reader itself does:
`fl_dec2bits(neg, m, 0, mant, ebits, 0)` (`lib/float.mc`, in scope through teko_float.tk's
`#include <float>`), 53/11 for `f64` and 24/8 for `f32`, the same widths `fl_lit` passes for
`1.0` and `1.0f`. `-1` is folded by the core and a `const` name is rebuilt into an `N_INT` by
teko_const.tk before this pass runs, so `f64 a = 1;`, `f64 b = -1;` and `f64 c = K;` are ONE
case and not three -- all three read their float value in the fixture, as does `f32 d = 1;`
at its own width. Inside a body the same conversion is an ordinary statement and takes
`tk_num_widen`'s `N_CAST`: `gw = 5;` on an `f64` global reads 5.0.

**Nothing else moved, and the measurement says why.** Across all 64 pre-existing fixtures
there are 285 global initializers, every one an `N_INT` or an `N_STR`; of the ones in a
scalar slot, the pairs are `i64 <- i64`, `f64 <- f64`, `Color <- Color`,
`DateTimeKind <- DateTimeKind` and `i8`/`i16` from a negative integer -- not one float slot
takes a non-float literal, and an `N_STR` is typed -1 by every oracle, so `tk_num_widens`
never answers 1 for it. The `--dump-ast` of all 64 is byte-identical to the base's.

**OPEN, on the same early return, and NOT this crumb's:** a counted global is not stored
through `rt_store`. `Cell gc; void fill(){ Cell c = new Cell(42); gc = c; }
void churn(){ Cell a = new Cell(7); Cell b = new Cell(9); } i64 main(){ fill(); churn();
churn(); return gc.v; }` exits **7** and wants 42 -- the global takes the pointer with no
count of its own and is left pointing at reclaimed memory. Fixing it means letting the rc
lowering itself run for a global, which moves the dumps of `surface_globals.tk:643/647` and
`surface_nullable_ref.tk:331/342` and needs a ruling on whether that lands before the
release; it is its own crumb. Recorded for the user in
`docs/reference/not-yet.md` § "Types and declarations".
CLOSED by D55 below: the crumb was cut, the leave is `tty < 0` and the not-yet row is gone.

**Proof** (mc 0.15.23, macos/aarch64, base `84c63025`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **65 passed, 21 refused as
expected, 0 failed** (was 64/15: `tests/surface_globals_slot.tk` and six under
`tests/refuse/`, each one measured compiling on the base before it was written down, and the
surface fixture exiting **11** there -- the first `f64` global read -- against 42 here). The
verifier caught the measurement short on three of the six: a refuse fixture that declares a
`class` and carries no `#include "../../lib/rt.tk"` never reaches the pass-time refusal on a
build where it does not fire -- the core answers `call to unknown function` at line 1 for
the runtime the class needs -- so "accepted on the base" was not actually observed. Every
refuse fixture that declares a class (eleven, the eight pre-existing ones included) now
carries the include, its `// expect-refuse-line` moved by one, and the base measurement was
redone: the six new ones compile there, the fifteen old ones refuse at their new lines
(`64 passed, 15 refused as expected, 6 failed` with the head's `tests/refuse/` over the base
build);
`./build/teko --dump-ast` byte-identical against the base build for every one of the 64
pre-existing `tests/*.tk`; `sh scripts/bootstrap.sh --os macos --arch aarch64` →
`FIXPOINT OK`, the judge running over the compiler's own global slots with the new check in
force; `sh scripts/check-docs.sh` green (`docs ok: 575 links, 388 diagnostics, 21 refusals,
133 samples`); `mc limits . --config mc.macos.toml` verdict `ok` with `passes` 15/30,
`types` 11, `intrin` 8/16, `alias` 18, `syntax` 15 -- every table unmoved -- and
`./build/teko limits tests/hello.tk` byte-identical to the base compiler's own output on the
same file; `mc pkg hash .`
`b119323edb59839324ee65fbbb29f0da741a099ee2e3f3c76942282aa414e0db`
(base `93fab590c5fd29dc2ceaabab56ba296e39d41044b3b6e38bd6ee36861d45ed6e`: `teko_rc.tk` is a
listed file, so the hash moves by design).

### D54 · `DateOnly`, and the width a primitive may have (2026-09-14)
*This log ended at D52 when N4a started: D53 was already taken by a crumb in flight (it has
landed since, and this branch was rebased onto it), so the number was assigned rather than
taken in order.*

`docs/specs/datetime-extras.md`'s N4 is split: **N4a is `DateOnly` and landed here**; N4b
(`TimeOnly`) and N5 (`DateTimeOffset`) stand as designed. What it is at the surface is
[datetime.md § `DateOnly`](docs/reference/datetime.md#dateonly) — the day number since
`0001-01-01`, C#'s own `int` representation, one `type_new("DateOnly", 4, 4, TK_SINT)`,
sixteen member rows, six operator rows and the `tk_do_*` half of `lib/time.tk`, which
CALLS `DateTime`'s calendar rather than copying it: `new DateOnly(2024, 2, 30)` panics
inside the very `tk_dt_days_from_ymd` the date constructor uses, and `AddMonths` clamps
through the very `tk_dt_add_months` that makes `2024-01-31` plus one month `2024-02-29`.

**Four bytes is the whole of what this crumb cost the mechanism.** Every primitive with
members was eight bytes until now, which made the compiler's own two casts
(`tk_prim_raw`'s `(i64) d`, `tk_prim_ret`'s `(DateOnly) r`) no instruction at all. Over
four they are a sign extension in and a narrowing store out — the pair an `enum : i32`
(`DateTimeKind`) already takes through every slot a value has (D38) — and no line of any
machine changed. What DID have to change is the record that tells such a cast from one a
source wrote: it is a list of node indices, and a node replaced **in place**
(`node_assign`) keeps the placeholder's index, so the record has to be handed over.
`tk_pend_do` (teko_typeof.tk) and `tk_ops_replace` (teko_ops.tk) did that already because
a lowering's own result travelled through them; with every indirect load of a narrow
primitive now being a cast too (`tk_ld`, `tk_arr_load`, `tk_callp_ret`), **six more doors
were dropping it** and refusing a cast the compiler had just written itself:
`tk_node_replace` (teko_this.tk — every implicit-`this` rewrite, so `d.DayNumber` on a
field inside a method), `tk_ref_replace` (teko_ref.tk — a `ref`/`out` pointee read),
`tk_array_maybe_rewrite_index` and `tk_hg_rewrite_index` (teko_array.tk — the fixed-global
and `T[]`-global element reads), `tk_deleg_call` (teko_deleg.tk — a delegate whose return
type is narrow declares it with a cast) and `tk_fwd_resolve_static_one` (teko_access.tk — a
static field read before its own type is declared). This is the crumb's own gap, found by
its own fixture, fixed here and not deferred — but patched door by door, which is what the
verifier's first pass found a seventh copy of.

**Verifier finding, first pass: the seventh door, and why counting doors was the wrong
fix.** A by-reference capture read is an in-place replacement too, and it had no carry:

```teko
#include "../lib/time.tk"
delegate DateOnly DOp();
i64 main() {
    DateOnly refd = new DateOnly(2024, 2, 29);
    DOp byref = () use (&refd) => refd;
    DateOnly viaref = byref();
    if (viaref.DayNumber != 738944) return 108;
    return 42;
}
```

— refused with ``teko: a DateOnly does not cast; `.DayNumber` reads it and
`new DateOnly(...)` builds it`` at the lambda's line, over a program that writes no cast at
all. ROOT: `tk_lam_replace` (teko_deleg.tk) rewrote the captured identifier into the
`tk_arr_load` its prologue derefs through — a compiler-own cast over four bytes — with
`node_assign` plus `set_nd_next` and nothing else, so the mark stayed on the node the tree
had just stopped pointing at and the cast walk read it as one a source wrote. Only the READ
side was affected: the write side (`use (&d)` then `d = …`) rewrites with `set_nd_kind` /
`set_nd_a` and never copies a node, so it compiled before and compiles now.

The fix is not an eighth carry. `node_assign` now appears **once in the whole compiler**, in
`tk_node_replace` (teko_struct.tk, beside `tk_nd` and the other node doors), which keeps
`nd_next` and hands the cast record over; the twenty-two hand-written replacements — the
seven above, `tk_ops_replace` and `tk_pend_do` which already carried it, the field store's
own wrap, the ternary's three, the reclaim's three, the deferred `new`, the DI resolution,
the `const` fold, the `??` lowering and the two deferred global stores — all call it,
and the three one-off wrappers that had grown around the same three lines
(`tk_ref_replace`, `tk_ops_replace`, `tk_lam_replace`) are gone. A door that forgets the
record cannot exist any more, because there is no second place to write one. Five of the
six forward declarations of `tk_prim_own_cast_moved` went with them.

**The other four-byte value, measured.** `use (&k)` on a `DateTimeKind` local — an
`enum : i32`, the same width — compiles and reads back correctly **on `e0129ba6` and here
alike**: nothing in the language casts an enum, so an enum load never reaches
`tk_prim_cast_check`, which only fires when one side of an `N_CAST` is a primitive carrying
a member table. The enum road is a fixture now (`surface_dateonly.tk`, cases 81–83) exactly
because it does NOT share the defect and must not start sharing it.

**The cast refusal gained a column.** It was generated from the type name with `.Ticks`
written into it, which over a `DateOnly` would have named a member the type does not have.
The reader is now a per-primitive column of `tk_prim_type` (`` `.Ticks` ``,
`` `.DayNumber` ``) and the builder stays the generated `new X(...)`, which `DateOnly` has:
``teko: a DateOnly does not cast; `.DayNumber` reads it and `new DateOnly(...)` builds it``.
That amends the spec's § 2 line, which had named `DateOnly.FromDayNumber(n)` as the
builder; both build one, and the shorter diff keeps one generated sentence.

**Three amendments to the spec as written**, each marked on that page: `DateOnly`
registers **no arithmetic operator at all** (C# declares none — `d + t` and `d - d` are
``teko: no operator `+` takes these operands`` and `d1.DayNumber - d2.DayNumber` is the
form); its six comparisons and `CompareTo`/`Equals` lower to `tk_ts_eq` … `tk_ts_ge` and
`tk_ts_cmp` rather than to a `tk_do_*` family, because a day number is an ordinary
non-negative `i64` and six wrappers that forward and nothing else are code not written;
and `ToString`/`Parse`/`TryParse` and `.ToDateTime(TimeOnly)` are **out of N4a** — no
`teko_prim.tk` primitive has a `str` member yet (`DateTime` has none either) and the
fourth needs a type N4b registers. All four are rows in
[not-yet.md](docs/reference/not-yet.md).

`TK_MAXPRIMM` 96 → 160 and `TK_MAXPRIMO` 32 → 48, raised here rather than in N4b, which
would have overflowed both. The tables stand at **3 primitives, 82 member rows, 28
operator rows, 51 parameter positions** and one late type name.

**Proof** (macOS/aarch64, mc 0.15.23, rebased onto `e0129ba6`):
- `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **67 passed, 26 refused as
  expected, 0 failed** (65/21 on the base): `tests/surface_dateonly.tk` at 42 — which walks
  a `DateOnly` through a local, a parameter, a return, a field, a global, a fixed-array
  element, a `T[]` element, a `ref`/`out` pointee, a closure's by-value capture and a
  closure's by-REFERENCE capture read and written, with the largest day number there is,
  because that set is what proves the four-byte width —
  `tests/surface_dateonly_panic.tk` at 70, and `tests/refuse/dateonly_{from_int,to_i64,
  to_datetime,cast,plus_datetime}.tk`.
- `--dump-ast` of all **65** pre-existing `tests/*.tk`, `e0129ba6`'s compiler run over THIS
  tree against this head's own: **byte-identical, 65 of 65**. The same 65 against this
  head's compiler as it stood BEFORE the one-helper collapse: **byte-identical too** — the
  refactor accepts the same code and builds the same tree, and the only dump that moves in
  the whole set is `surface_dateonly.tk`'s, whose source gained the by-reference section.
  Ten pre-existing fixtures include `lib/time.tk` (`surface_datetime`,
  `surface_datetime_kind`, the two `*_panic`, `surface_timespan`,
  `surface_timespan_overflow`, `surface_nullable_value`, `surface_nullable_ops`,
  `surface_overload_ops`, `surface_globals_slot`); dumped with ONE compiler over the two
  TREES, each differs by **+137 lines, −0** — the `tk_do_*` functions the library gained
  and nothing else.
- `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK`.
- `mc limits`, verdict `ok` on both legs, peak columns: the `tests/hello.tk` floor
  `passes` 15, `intrin` 8 and `syntax` 15 **unmoved**, `types` 11 → **12** and `alias`
  18 → **19** (each `type_new` takes an alias row too, as `TimeSpan` and `DateTime` each
  did); the `tests/surface_datetime.tk` leg `passes` 15 and `intrin` 8 **unmoved**,
  `types` 15 → **16**, `syntax` 16 → **17**, `alias` 22 → **23**.
- `sh scripts/check-docs.sh` → `docs ok: 585 links, 388 diagnostics, 26 refusals, 137
  samples` (137 blocks: 77 run, 60 no-run) — no new `teko: …` literal, the cast one
  changed shape and is documented.
- `mc pkg hash .` → `9380d2531e08563be1013612736d542f78d7105c6d45744b4214f6e6ccabf460`.

**Left open, found here and not touched.** Two, both pre-existing and neither `DateOnly`'s:
a `.` on the RESULT OF A CALL whose type is a primitive is not resolved —
`f().DayNumber` on a delegate, and `f().Day` on a `DateTime` measured the same way, reach
`teko: unknown member: DayNumber`, and a local in between is the spelling that works; and
`class TimeSpan { }` compiles silently, because a primitive's type word is not protected
against a declaration that shadows it.
### D55 · A store into a COUNTED global slot goes through `rt_store` (G-c, 2026-09-14)
D53 left one half of `tk_rc_assign` (teko_rc.tk) still shut: the CHECK reached a global,
the RC LOWERING did not. `Cell gc; void fill(){ Cell c = new Cell(42); gc = c; }
void churn(){ Cell a = new Cell(7); Cell b = new Cell(9); } i64 main(){ fill(); churn();
churn(); return gc.v; }` exited **7** against 42 on the base (`e0129ba6`): `fill`'s own
`rc_dec(c)` at the `}` freed the object the global still pointed at, and the two `churn`
calls wrote over the block the free list had handed back. A delegate global
(`Op g_op = addOne;`) and a `T[]` one (`g = new i64[n];`) were unowned the same way.

**ROOT: `if (li < 0) return;`, one line above the lowering.** `tk_rc_index` answers -1 for
a name no pass-time scope holds, which is every global, so the function left before
`tk_is_counted(tty)` was ever asked. The lowering itself needs nothing a global cannot
give: `tk_addr(name)` builds `&g`, an address mc's core takes at any scope (it is what
`ref g_mf` on a global already passes, `tests/surface_globals.tk`), and `rt_store` /
`rt_store_own` are chosen by `tk_rc_own(nd_a(n))` exactly as on a local. So the leave
becomes `tty < 0` -- the name is neither a local nor a global, the only case with no slot
to judge -- and the BORROWED-parameter guard, a rule about parameters and nothing else,
asks for a scope slot first: `li >= 0 && li < tk_rc_floor`, since a global's `li` is -1,
which is below the floor and is not a parameter. Two lines of surface code; no new pass,
no new intrinsic, `passes` 15/30 unmoved.

**The owned/borrowed split needed no line of its own.** `new C(..)`, `new T[n]` and a
call result are `TK_OWNED` (teko_expr.tk, teko_heaparr.tk, `tk_rc_call_owned`), so they
take `rt_store_own` and move their single reference in -- no double-own. Everything else
-- a local, a parameter, a field, a ternary arm -- is borrowed and takes `rt_store`, which
increments before it releases, so `g = g;` cannot free the object between the two steps
(lib/rt.tk) and the source stays alive. `null` is the handle `0`, which `rc_inc`/`rc_dec`
already treat as a no-op, so `gq = null;` on a `Cell?` global is a plain `rt_store` that
releases the old value and stores nothing.

**The RULING is one half of the slot rule, not a change to the other half.** A global is
still a ROOT: the value it holds at the end of the run is never released (§50 decision 16,
`teko_array.tk`, `tests/surface_array_global.tk`). What it gains is the rule every other
slot already had -- it releases its OLD value when it is overwritten
(`docs/reference/memory.md`), whose two rows are amended here; the `not-yet.md` row D53
wrote for the defect is deleted, and D53's own OPEN paragraph carries a pointer to this
entry. No refusal is added or moved, so `docs/reference/diagnostics.md` is untouched.

**BLAST RADIUS, measured, not assumed.** Of the 86 `--dump-ast` outputs of the PRE-EXISTING
fixtures (the base's 65 `tests/*.tk` and 21 `tests/refuse/*.tk`; the new
`tests/surface_globals_rc.tk`, whose every section stores into a counted global, has no base
dump to compare against and is left out of this count), **81 are byte-identical** to the
base compiler's. The five that move hold **27 stores into a counted global** between them,
and the diff is exactly those 27 statements, each `ASSIGN name=<g>` becoming
`EXPRSTMT · CALL rt_store[_own] · ADDR <g>` with its value subtree unchanged: with the
27 head lines accounted for, the residual of the diff in both directions is EMPTY.
**24** are `rt_store_own` -- every `new` and every call result -- and **3** are `rt_store`,
the three `= null` stores (`surface_globals_slot.tk`, `surface_globals.tk`,
`surface_nullable_ref.tk`). By fixture: `surface_globals.tk` 13 (`g_op` 4, `g_cell` 2,
`g_ops`, `g_ops2`, `g_many`, `g_mut`, `g_muti`, `g_maybe`, `g_maybes`),
`surface_array_global.tk` 5, `surface_globals_slot.tk` 4, `surface_field_store.tk` 3,
`surface_nullable_ref.tk` 2. The generated `N_ASSIGN` sites that must NOT start firing --
`teko_di.tk`'s `TY_UPTR` local, teko_loop.tk's four, teko_ternary.tk's temporary -- are
proved untouched by the same enumeration: they carry no counted global name, and the 81
identical dumps cover every fixture that builds one. The live-count oracles
(`surface_field_store.tk`'s `rt_live() != 4`, `surface_array_global.tk`,
`surface_nullable_ref.tk`) all still hold, unedited: no raw alias survived an overwrite,
so no double-free was introduced.

A STATIC field was already counted and is unmoved: it lowers to an 8-byte buffer global
and its store goes through the field-store path, which emitted
`rt_store(holder_s, c)` before this crumb and emits it after (measured on the same
reproducer written with `public static Cell s;` -- exit 42 on both builds).

**FIXTURE** `tests/surface_globals_rc.tk` (`// expect-exit: 42`, 9 helpers, `rt_live()`
and a `~Cell()` counter as the oracle of every claim): the reproducer; the overwrite that
leaves ONE object alive and runs ONE destructor; `= null` back to the floor; `Cell?`
beside `Cell`, read through `.HasValue`/`.Value`; the four sources of the value -- a
parameter, a field, a call result and a ternary arm, each with its own `rt_live()`
assertion and each borrowed source proved alive; a store written inside a LAMBDA body,
read back after two rounds of allocation (`tk_rc_pass` walks every top-level `N_FUNC`, and
a lambda is lifted into one); a delegate global assigned and re-assigned, the first
delegate object released by the second store; a `T[]` global re-assigned, the old array
and the element it owned both gone; and a global read after a loop of a thousand
allocations. It exits **11** on the base build -- the reproducer's own helper -- against
42 here.

**Proof** (mc 0.15.23, macos/aarch64, base `e0129ba6`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **66 passed, 21 refused as
expected, 0 failed** (was 65/21); the dump enumeration above;
`sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` -- the compiler's own
sources are mc, which has no class, no interface, no delegate and no `T[]` of heap, so
`tk_is_counted` answers 0 for every global they declare and stage 2 rebuilds itself
byte-for-byte; `sh scripts/check-docs.sh` green (`docs ok: 575 links, 388 diagnostics,
21 refusals, 133 samples`); `mc limits . --config mc.macos.toml` verdict `ok` on both legs,
every table unmoved except the size of the added surface code itself
(`nodes` used 155529 → 155533, `ins` 214246 → 214257; the `heap` column is not a proof
figure -- the verifier measured +405344 on the compiler leg and +393216 on the `tests/hello.tk`
leg where the first pass had written +12128 and "byte-identical": `mc limits`'s heap `used`
reflects the state of `build/` at the time of the run, so only the verdict and the table
rows are compared), with `passes` 15/30, `types` 11, `intrin` 8/16,
`alias` 18 and `syntax` 15 exactly where D53 left them, and
`./build/teko limits tests/hello.tk` byte-identical to the base compiler's own output;
`mc pkg hash .` `698450587e6e7addee68b51ce72bc4a34d0427b84fe6375cb3ded46de40271cd`
(base `b119323edb59839324ee65fbbb29f0da741a099ee2e3f3c76942282aa414e0db`: `teko_rc.tk` is a
listed file, so the hash moves by design).

### D56 · A STRUCT global is a class global (G-d, 2026-09-14)
D5 already settled a struct value as a POINTER, eight bytes, so a struct global takes the
door every other reference-typed global takes, three doors already cut: D48 gives it a ROW
in the slot table, D53 runs its assignment through the same compat/widen corridor a local
takes, and `tk_is_counted` (teko_struct.tk, D5) answers 0 for a struct row -- no vtable, no
count -- so a struct global's own store never reaches `rt_store` at all, whether D55 (PR
#704, in verification at this crumb's base and landed since) has landed or not: a struct is excluded
from that corridor by TYPE, not by being a global. **Measured: G-d is zero compiler changes**
-- one fixture, `tests/surface_globals_struct.tk`, ten shapes each proven green on the base
(`e0129ba6`) before the file was written: built in a body (`new` at a struct global's own
DECLARATION is refused by mc's own core, `global initializer must be constant` -- a `new` is
a call, never a constant, so no `teko:` refusal of this crumb's own is possible or needed
here); a field store and read; a whole-struct store aliasing a local (`gp = p; p.x = 42;`
reads 42 through `gp`) -- the RECORDED semantics: **C# copies a struct on assignment; teko
does not**, because a struct global (like a struct local) holds no sixteen-byte value to
copy, only the pointer D5 already gave it, so `gp = p` is the exact alias a class global
already licensed; a local built from the global (`Point q = gp;`) is the same alias; the
global passed by value, by `ref` and by `out`; a method through the implicit `this`; a nested
struct field two levels deep (`gl.a.x`); a struct global whose OWN field is counted (`struct
S { Cell c; } S gs;`) survives two unrelated `Cell` allocations still reading its own value
-- the field store is gated by the FIELD's type (`tk_os_mark`, teko_struct.tk), never by the
struct's, so the reclaim serves it with no line of its own either; a `static` field of struct
type; `Point? gp2;` at file scope (a struct is a REFERENCE, Q1a, so `T?` over it is a global
like any other class nullable -- no box); a parameter shadowing the global's name. No refuse
fixture: `tests/refuse/global_row_mismatch.tk` is a class-to-class mismatch (`Cell` into a
`Box` slot), and it fixes the wording of the ROW case; the struct-to-struct form was measured
to refuse with the identical message (`teko: a value of type Box does not convert to Point`,
global and local alike) because `tk_check_compat` reads the row and judges a struct row the
way it judges a class row -- so a struct-specific refuse fixture would prove nothing the
class one does not, and none is added.

**Docs.** `docs/reference/types.md` § struct: one paragraph beside the struct-vs-class
boundary, naming the alias rule explicitly and the initializer refusal (build it in a body).
`docs/reference/not-yet.md`: a row beside the definite-assignment table's existing "not
judged at all" line, naming the segfault a reader hits reading a struct/class global's field
before ever building it (a global slot is zero at load, so the read is a null-pointer deref,
exit **139**/SIGSEGV) and pointing it at D42's ruling -- unguarded, the developer's error,
not judged, the same ruling a local's own unguarded read already answers to. No
`docs/reference/globals.md` exists to carry a row of its own (checked `docs/reference/
README.md`'s own table; globals are folded into `arrays.md`, `types.md` and `memory.md`).

**OPEN, found measuring this crumb, and NOT this crumb's:** the parse-time argument check at
a VIRTUAL or an INTERFACE call skips every GLOBAL argument, silently, not merely unconverted.
`tk_pty_of`'s `N_IDENT` arm (teko_struct.tk) answers only `tk_slv_find`, the parser's own
stack of LOCALS -- a global holds no row there -- so `tk_vcall_args_check` (teko_expr.tk) and
`tk_ifargs_check` (teko_iface.tk) judge nothing when a virtual/interface call argument is a
bare global name. Measured: `class Point { public i64 x; } class Box { public i64 w; } class
B { public virtual i64 take(Point p) { return p.x; } } Box gb; i64 main() { B b = new B(); gb
= new Box(); gb.w = 7; return b.take(gb); }` compiles and exits **7**, `Box.w`'s bits read as
`Point.x`; `class B { public virtual i64 take(i64 n) { return n; } } f64 gf = 1.5; i64
main() { B b = new B(); return b.take(gf); }` compiles and exits **16**, the float bits read
as an integer. The identical call with a LOCAL in place of the global is refused (measured:
`teko: a value of type f64 does not convert to i64`). Not this crumb's: the fix is teaching
`tk_pty_of` the global table, a change reached from every virtual and interface call site in
the unit, not from a struct-only measurement. Recorded in `docs/reference/not-yet.md` §
Numeric conversions, beside the row for the same check's other gap (a bare parameter name).

**Proof** (mc 0.15.23, macos/aarch64, base `e0129ba6`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **66 passed, 21 refused as
expected, 0 failed** (was 65/21: one fixture added, `tests/surface_globals_struct.tk`,
exiting 42 on the first measurement -- no probe, no retry); no hook module touched by this
crumb, so `./build/teko --dump-ast` is byte-identical to the base's for every pre-existing
fixture BY CONSTRUCTION -- the strongest proof a G crumb has offered, since there is no diff
to read at all; `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK`; `sh
scripts/check-docs.sh` → `docs ok: 575 links, 388 diagnostics, 21 refusals, 133 samples`,
unmoved; `mc limits . --config mc.macos.toml` verdict `ok`, `passes` 15/30, `types` 11,
`intrin` 8/16, `alias` 18, `syntax` 15 -- every table exactly where D53 left them; `mc pkg
hash .` `b119323edb59839324ee65fbbb29f0da741a099ee2e3f3c76942282aa414e0db`, UNMOVED against
the base (no listed file changed -- `tests/` and `docs/` are not in `mc.toml`'s own list).

### D57 · The arguments of an INDIRECT call are judged like a direct call's (2026-09-14)
An ordinary call names its callee, so the last pass of all looks the symbol up and judges
every argument against the declaration (`tk_rc_call_args`, teko_rc.tk). A VIRTUAL call, an
INTERFACE call and the UNQUALIFIED form of either inside a method name none: all three are
a `callp`, and the only point that knows which method they reach is the site that BUILDS
them -- `tk_vcall_args_check` (teko_expr.tk), `tk_ifargs_check` (teko_iface.tk) and
`tk_this_emit` (teko_this.tk), which built the same `callp` and called **no check at all**.

Those sites ask the oracle they have, the parse-time `tk_pty_of` (teko_struct.tk), whose
N_IDENT arm is `tk_slv_find` -- the parser's own stack of locals -- and which has no N_ADDR
arm whatever. `tk_check_compat` and `tk_num_widen` both read its -1 as "do nothing", so the
argument crossed **unjudged and unconverted**. Eight shapes measured on the base
(`af2fffad`), every one of them compiling, every one of them refused or converted on the
identical local through the qualified spelling:

- `Box gb; b.take(gb);` on a `take(Point)` -- exit **7**, the callee reading `Box.w` as
  `Point.x`. The interface twin the same.
- `f64 gf; b.takei(gf);` on an `i64` parameter -- accepted, the double's bit pattern in an
  integer slot. The interface twin the same.
- `i64 gi; b.bump(ref gi);` on a `ref f64` parameter -- exit 42 and `gi` left holding the
  bits of 2.0. The interface twin the same, and the LOCAL `ref li` too, where the DIRECT
  call is refused `teko: a value of type i64 does not convert to f64`.
- `takei(lf)` written unqualified inside a method, with an `f64` LOCAL -- accepted. This is
  the road on which even a local crossed unread: it is rewritten from a pass, where the
  parser's stack is long closed, so `tk_pty_of` answers -1 for every name.
- `takef(2)` written unqualified, on an `f64` parameter -- exit **1** against 25. The
  CONVERSION went missing with the check, and that half is silently wrong in a program with
  no global in it at all.

**ROOT: one oracle, asked at the only point that holds the callee, and too early.** The
answer is not a second oracle at the site -- the pass-time tables (`tk_ty_global`,
teko_array.tk, filled at pass 5; `tk_ty_scope_or_global`, teko_typeof.tk) do not exist while
the parser is running, and reading `tk_ty_global` alone would be wrong twice over: a global
declared BELOW the body works today, and a LOCAL shadowing a global of another type must
resolve to the local. So the judgement is DEFERRED, which is D48's own discipline for a
primitive row's argument and D50's for a field store, one slot over: the argument is PARKED
(`tk_vca_defer`) and judged at the end of `tk_over_pass` (`tk_vcall_arg_judge`,
teko_typeof.tk), **no pass of its own** -- `passes` stays 15/30 -- at the one point that has
the lexical scope live at the site (`tk_ty_pass_walk`), every deferred `.` resolved, every
user operator lowered and every overload pick committed. It matches by node IDENTITY,
because the argument was spliced into the `callp`'s own list and nothing at the door knows
which link it is.

**Two rules at the judge, neither of them new.** By value it is `tk_ty_of` then
`tk_check_compat` -- the two scalar verdicts and the row one in one call -- followed by the
conversion C# §10.2.3 owes it; `ref`/`out` is `tk_ref_check_pointee_ty` (teko_ref.tk), the
IDENTITY rule `tk_rc_call_args` and the delegate call already apply, with a pointee no scope
and no global table names silently skipped there as here. The conversion is D50's own tail,
factored out of `tk_fs_do` as `tk_fs_convert` and called from both: the node keeps its
identity and BECOMES the cast or the box, because the slot it crosses into holds no handle
on it. No behaviour of the field store moved -- the 94 pre-existing dumps prove it.

**Only a bare NAME is parked by value, and that is a rule, not a shortcut.** *(Superseded
in part by D59's second pass: the rule below refused nothing, but it let every COMPOSITE
expression -- a binary, a ternary, a negation -- cross unjudged and unwidened on all three
roads, which the skip this entry documented is exactly the record of. Every by-value
argument is parked now, and the false-refusal risk this paragraph names is answered at the
judge instead: a parked row the pass still cannot type is refused only when it is a bare
name.)* What a later
pass adds to a name is the scope, the global table and the load an unqualified field name
becomes; a local array's element, an indirect `callp` and an address the source built by hand
are no better known at pass 14 than at the door, so parking them would turn today's silence
into a refusal of code with nothing wrong with it. Measured unmoved on both builds: an
argument that is a field of `this`, a local array's element, an `out` argument, a global
declared below, a local shadowing a global, and `p.same(v)` on a bare parameter
(`tests/primitives_float.tk`, the one pre-existing fixture that parks anything -- 1 row,
judged `i64` against `i64`, no node moved). A top-level `const` never reaches the table at
all: teko_const.tk rebuilds the name into its `N_INT` before any of the three doors reads it,
so `b.take(K)` into a `take(Point)` was already refused on the base and `b.takei(K)` runs
here -- probed in both directions.

**`tk_this_emit` calls `tk_vcall_args_check` on its `slot >= 0` branch**, and on that branch
only: the direct branch names its callee and the last pass of all looks it up. One body, not
a third copy of the loop.

**The ceiling is D50's, for D50's reason.** `TK_MAXVCA` 4096 over six columns is 192 KB of
globals, against a measured peak of **26** (`tests/surface_globals_calls.tk`) and 1
everywhere else in the tree; `mc limits`' `globals` row moves 935 -> 942, six arrays and a
counter, against 2530 reserved. Over it,
`teko: too many deferred call arguments`, the one new literal, documented on the
diagnostics page's Capacity table.

**FIXTURES** (8). Seven refusals, each measured COMPILING on the base before it was written
down: `tests/refuse/vcall_global_arg.tk`, `vcall_global_arg_scalar.tk`,
`ifcall_global_arg.tk`, `ifcall_global_arg_scalar.tk`, `vcall_ref_pointee.tk`,
`ifcall_ref_pointee.tk` and `thiscall_virtual_arg.tk` -- the last being new ground, an `f64`
LOCAL through the unqualified road. And the positive `tests/surface_globals_calls.tk` (42):
globals above and below `main` on all three roads and through the written-out `this.m(x)`,
the widening of an `i64` global onto an `f64` parameter, a `ref` global with the right
pointee on both roads, a local shadowing a global of another type, a `const`, and a
`DateOnly`/`TimeSpan` global -- four bytes and eight (D54) -- crossing the `callp`. It exits
**14** on the base, section 1's fourth check, and 42 here.

**Proof** (mc 0.15.23, macos/aarch64, written against base `af2fffad` and re-measured on
`d0fbe9a4`, the merge of D56 and #706 -- neither touches a compiler source, `git diff
af2fffad d0fbe9a4` over `*.tk`/`lib/`/`*.mc`/`mc.toml`/`teko.toml` being one added fixture):
`mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` -> **70 passed, 33 refused as expected, 0 failed** (69/26 on the base, and
running the head's `tests/` under the BASE compiler reads `69 passed, 26 refused as
expected, 8 failed` -- exactly this crumb's eight); `--dump-ast` of all **95** pre-existing
fixtures (69 `tests/*.tk` + 26 `tests/refuse/*.tk`), the base compiler's output against this
head's: **byte-identical, 95 of 95** -- the change only parks, refuses and converts, and no
pre-existing fixture hands an indirect call an argument that needs a conversion;
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (the compiler's own
sources are mc: no class, no interface, so nothing is ever parked there and stage 2 rebuilds
itself byte-for-byte); `sh scripts/check-docs.sh` green (`docs ok: 587 links, 32 fragments,
389 diagnostics, 33 refusals, 138 samples`); `mc limits . --config mc.macos.toml` verdict `ok` on
both legs -- the `tests/hello.tk` leg has `passes` 15/30, `types` 12, `intrin` 8/16, `alias`
19 and `syntax` 15 **unmoved**, and `./build/teko limits tests/hello.tk` is byte-identical to
the base compiler's own output; the compiler leg moves only by the size of the added surface
code and its tables, `nodes` 155615 -> 155975, `globals` 935 -> 942, `ins` 214500 -> 215007,
`funcs` 3162 -> 3173, `lowered` 3144 -> 3155; `mc pkg hash .`
`9b9e6483727e8e0736bfd4dc37db5ef87141bbf76a6f33a729b405b1afd8a8e7` (base
`0d0b6fa61e30ea12c7cb8ae1bd60b4db9827f53a4a5c5d67d8a74c038ae62795`, the same on `af2fffad`
and on `d0fbe9a4`: five listed `.tk` modules moved, so the hash moves by design).

**TWO rows of `not-yet.md` are CLOSED by this entry**, not merely amended. The first, older
one: "an integer argument at a virtual or an interface call, written as a bare parameter
name (`a.by(n)` inside `g(i64 n)`) -- not converted". Probed both ways: `s.by(n)` on a
`virtual f64 by(f64)` and `k.take(n)` on an interface `f64 take(f64)`, with an `i64`
parameter `n`, read the raw bits on the base and read 8.0 here. The second is the row D56
([#705](https://github.com/teko-org/teko-lang/pull/705)) wrote for this very gap while
measuring a struct global -- "a virtual or an interface call argument written as a bare
global name ... not judged AT ALL" -- whose own reproducer is
`tests/refuse/vcall_global_arg.tk` here. This branch was rebased onto D56 and #706 at the
end; the merge kept D56 ahead of this entry and removed both rows, since the conversion and
the judgement they name are what the fixtures now lock. The `switch`-on-an-enum-parameter
row that cited the first as a sibling limitation is amended to say why it is NOT one: a
`switch` subject's hidden local is DECLARED at parse time, so there is no later point at
which its type could still be chosen.

**Left open, found here and NOT touched** -- a call through a DELEGATE slot does not judge a
by-value argument. `tk_deleg_check_arg_kinds` (teko_deleg.tk) checks the `ref`/`out` KIND and,
since D51, the `ref` POINTEE, and nothing else: the by-value column `dg_pty_at` is never
compared. `delegate i64 Op(i64 a); i64 twice(i64 a) { return a + a; } f64 gf = 1.5;
i64 main() { Op f = twice; f64 lf = 1.5; i64 a = f(lf); i64 b = f(gf); return 64; }` compiles
on this head exactly as on the base, local and global alike, where the direct `twice(lf)` is
refused. It is its own crumb -- the door is a different one, in a different module, and the
rule it needs is the one this entry just wrote down. Recorded as a row of
`docs/reference/not-yet.md` § "Numeric conversions".

**Copilot findings, second pass -- the door judged a tag it never read, and trusted an
oracle that answers about the WRONG declaration.** Three findings on the compiler and one
on the prose, each measured first on `d0fbe9a4` (the base) and on this crumb's head
(`14502bef`), where every one of them reads the SAME: none is a regression of this branch,
each is a gap the judge it added does not close. mc 0.15.23, macos/aarch64; `Box` a class
with `virtual i64 takei(i64)`, `virtual i64 takef(f64)` and `virtual i64 bump(ref f64)`,
`Sink` the interface declaring the same three, and `pick` an overloaded name:

| probe | `d0fbe9a4` = `14502bef` | now |
|---|---|---|
| `b.takef(pick(2))`, `f64 pick(f64)` declared ahead of `i64 pick(i64)` | compiles, **exit 40** (the raw integer 3 read as a double) | runs, **30** -- the direct twin's own answer |
| `s.takef(pick(2))`, the itab road | compiles, **exit 40** | runs, **30** |
| `b.takei(pick(2))`, the same pair | **refuses** a legal program, `teko: a value of type f64 does not convert to i64` | runs, 13 |
| `b.takef(pick(2.5))`, `i64 pick(i64)` declared first | runs, 30 -- the cast the door wrote was `f64` over an `f64` and mc lowered it to nothing | runs, 30, with no cast written at all |
| `b.takei(ref x)` on a by-value `i64` | compiles, **exit 209** (the low byte of a stack address) | refuses, `teko: argument 2 is not passed by reference` |
| `s.takei(ref x)`, the itab road | compiles, **exit 209** | the same refusal |
| `takei(ref x)` unqualified, inside a method | compiles, **exit 177** | the same refusal |
| `b.bump(x)` on a `ref f64`, no tag at the site | compiles, **SIGSEGV** (the callee's `d = 2.0` written through a value) | refuses, ``teko: argument 2 needs `ref` at the call site`` |
| `s.bump(x)` / unqualified `bump(x)` | compiles, **SIGSEGV** | the same refusal |
| the direct twins, `takef(pick(2))`, `takei(ref x)`, `bump(x)` | 30, and the two refusals in exactly those words | unmoved |

1. **An OVERLOADED call's return type is not the first declaration's.** `tk_pty_of`
   (teko_struct.tk) types an `N_CALL` as `decl_ret(decl_find(name))`, and `decl_find`
   (mc/src/parse.mc) walks `unit_head` in declaration order and answers with the FIRST --
   while which overload a site reaches is settled by `tk_ov_pick` at pass 14. Both doors
   read that answer as final, and it is wrong in both directions at once: against a matching
   parameter it writes no conversion where the picked overload needs one (the raw integer
   into an `f64` parameter, exit 40), and against a mismatched one it REFUSES a program
   whose picked overload fits. It is D49's own finding, one door over: `tk_prim_defers`
   (teko_prim.tk) defers a call on a float column for exactly this reason. So the park test
   grows one shape -- `tk_vca_ov_call` (teko_typeof.tk) -- and the whole decision moves into
   `tk_vca_park`, the one helper both loops now call: it hands back the parse-time answer
   when that answer is trustworthy and -1, the "check nothing, convert nothing" both readers
   already understand, when the argument was parked instead. The count is taken from
   `decl_find`'s own node forward along `nd_next`, because teko_over.tk's `tk_ov_find` is a
   table built at pass 6 and this door runs while the parser is still inside the body. Only
   a call to an overloaded name is added: a call to a name declared once is answered
   correctly today, and a call to a name declared BELOW is left exactly as it was.
2. **and 3. The `ref`/`out` TAG was never compared with the parameter's kind.** Both loops
   compared TYPES only, and the tag is not a type: `tk_ref_check_call` (teko_ref.tk) is
   keyed on a call's SYMBOL and a `callp` has none, so nothing on the three indirect roads
   ever asked the question. An address crossed into a by-value slot and a value crossed into
   a `ref` one, the second being the one that writes through what it was handed -- SIGSEGV
   on every road, where the direct call refuses at the same line. The rule is factored out
   of `tk_ref_check_call` as `tk_ref_check_kind` and asked FIRST at all three doors, before
   the compat and the park: nothing that follows means anything when the tag is wrong. The
   words are the direct call's, and so is the numbering -- the receiver counts as argument
   1, exactly as it does in the mangled `Owner_method` a non-virtual method call lowers to,
   so `b.m(ref x)` says "argument 2" whichever road carries it. `tk_ref_check_call` keeps
   its own behaviour to the letter: the 98 base dumps and its own fixtures prove the
   factoring is a no-op.
4. **The diagnostics page's own sentence.** "a `ref`/`out` pointee no scope and no table of
   globals holds a row for stays silently skipped" is unreadable, and it was also the page's
   only statement of what this door lets through in silence. Rewritten as two named cases --
   a `ref`/`out` pointee no scope and no table of globals NAMES (the address was built by
   the source and its target has no declared type to compare), and a by-value argument that
   is neither a bare name nor a call to an overloaded one (no later pass knows more about it
   than the site did) -- with the tag rule and the overload rule stated beside them, and
   the two `"teko: argument "` completions amended to say that all three indirect roads give
   them too.

**FIXTURES** (7, for 39 refusals and 71 runs in the tree): `tests/vcall_overloaded_arg.tk`
(39) drives both directions of finding 1 on the vtable road, the itab road and the
unqualified one -- refused on the base at its fourth check -- and six refusals name one
direction on one road each: `tests/refuse/vcall_ref_extra.tk`, `vcall_ref_missing.tk`,
`ifcall_ref_extra.tk`, `ifcall_ref_missing.tk`, `thiscall_ref_extra.tk` and
`thiscall_ref_missing.tk`. The unqualified road gets its own pair because its DOOR is
another one (`tk_this_emit`, teko_this.tk, which runs from a pass where the parser's stack
of locals is closed), even though the loop it reaches is shared.

**Proof of the second pass** (mc 0.15.23, macos/aarch64): `sh scripts/fixtures.sh
./build/teko mc.macos.toml` -> **71 passed, 39 refused as expected, 0 failed**;
`--dump-ast` against the BASE compiler (`d0fbe9a4`) over every fixture that tree holds,
**98 of 98 byte-identical**, and against this crumb's own head (`14502bef`) over all of
its, **106 of 106** -- the dump is taken after the passes, checked by the same comparison
reading `CAST type=f64` where the fix now writes one; `sh scripts/bootstrap.sh --os macos
--arch aarch64` -> `FIXPOINT OK`; `sh scripts/check-docs.sh` green (`docs ok: 587 links, 32
fragments, 389 diagnostics, 39 refusals, 138 samples`); `mc limits . --config
mc.macos.toml` verdict `ok` on both legs, the `tests/hello.tk` leg's table rows
unmoved against the base's and `14502bef`'s (`passes` 15/30, `types` 12, `intrin` 8/16,
`alias` 19, `syntax` 15; the `heap` column is not a proof figure, D55's lesson -- the
verifier measured it moving while every counted row stood) and the compiler leg moving by the added surface alone --
`nodes` 155975 -> 156097, `ins` 215007 -> 215225, `funcs` 3173 -> 3176, `lowered` 3155 ->
3158, `globals` **942 unmoved** (no table is added, the park test only widens);
`mc pkg hash .` `ef81724394452c08172a4a47bfcf2cfd2c463bc6861bcf1c40496a4122831b51`.
### D58 · `TimeOnly`, the fourth primitive that costs nothing new (2026-09-14)
*D57 was already taken by a crumb in flight (`fix/vcall-args-judge`, PR #707, D57), the
same reason D54 gives for its own number: assigned rather than taken in order.*

`docs/specs/datetime-extras.md`'s N4b lands: `TimeOnly`, ticks since midnight,
`0 .. 863999999999` (`TICKS_PER_DAY - 1`), C#'s own representation, eight bytes —
`type_new("TimeOnly", 8, 8, TK_SINT)`, twenty member rows of its own, seven operator rows,
and one MORE row on `DateOnly`'s own table, `.ToDateTime(TimeOnly)`, the member N4a left out
because the argument's type did not exist yet. Registered in `tk_time_init`
BETWEEN `tk_dt_operators()` and the `DateOnly` block, so both new columns —
`TimeOnly.FromDateTime`'s own (naming `DateTime`) and `DateOnly.ToDateTime`'s own (naming
`TimeOnly`) — read a LIVE id at registration time and neither table needs `tk_prim_late`.

**Eight bytes, and half its rows call no new function at all.** Unlike `DateOnly` (D54),
`TimeOnly` costs the mechanism nothing: no width, no new `TK_PM*` kind, no new door in the
own-cast list, no pass.

- `.Hour` `.Minute` `.Second` `.Millisecond` point straight at `DateTime`'s own `tk_dt_hour`
  / `tk_dt_minute` / `tk_dt_second` / `tk_dt_ms`: masking `DATETIME_TICK_MASK` off a value
  that never carried a `Kind` above it is a no-op (a `TimeOnly` needs under 40 bits, the mask
  clears only the top two), so four functions that would repeat the same division and modulo
  are code not written.
- The six comparisons, `.CompareTo` and `.Equals` point at `TimeSpan`'s own `tk_ts_eq` …
  `tk_ts_ge` / `tk_ts_cmp`, the exact reuse `DateOnly` made under D54: a tick count of a time
  of day is an ordinary non-negative `i64` too.
- `.Ticks` and `.ToTimeSpan()` are symbol-less identity rows (`tk_prim_emit`'s `sym == 0`
  path, teko_prim.tk § "the emission"): the same eight bytes read back under the other type,
  a cast and no call. `.Ticks` repeats `TimeSpan.Ticks`'s own precedent; `.ToTimeSpan()` is
  the first time the trick is used on a METHOD (`TK_PMFUN`) rather than a property
  (`TK_PMPROP`) — and needed no change, because nothing in `tk_prim_emit` special-cases the
  row's `kind` except `TK_PMCTOR`, so the identity path was already general enough for a
  zero-argument instance method with a receiver.

**What IS new: the wrapping arithmetic, and one panic.** `.Add(TimeSpan)`, `.AddHours(f64)`
and `.AddMinutes(f64)` never panic — C#'s own rule — wrapping at both ends of the day
(`tk_to_add`, `lib/time.tk`: the `TimeSpan` operand is reduced mod `TICKS_PER_DAY` FIRST so
the sum cannot overflow the `i64` itself, since a `TimeSpan` may carry ticks far outside one
day). `.IsBetween(a, b)` is C#'s own rule too: `a` inclusive, `b` exclusive, and it WRAPS
when `a` is after `b` (`22:00` to `02:00` covers midnight). The one operator, `t1 - t2`, is
NEVER negative — the elapsed time from `t2` to `t1`, wrapping FORWARD across midnight when
`t1` is earlier (`tk_to_sub`); `t + t` has no row and is refused by absence, and so does
`t + TimeSpan`/`t - TimeSpan` — C# has no such operators either, `.Add` is the form. The one
new panic, `teko: a time of day is out of range`, fires on `new TimeOnly(ticks)` and
`TimeOnly.FromTimeSpan(ts)` alone: no existing wording in `lib/time.tk` reads honestly for
an interval that starts at zero, unlike every other range that file already guards. The
three calendar-free constructors (`new TimeOnly(h, mi)`, `(h, mi, s)`, `(h, mi, s, ms)`)
reuse `tk_dt_time_ticks` and so reuse its four existing panics
(`an hour`/`a minute`/`a second`/`a millisecond is out of range`) — the same function
`new DateTime(y, m, d, h, mi, s)` panics on.

**Seven amendments to the spec, recorded on that page and not repeated here:** the panic it
named, `` `that time of day does not exist` ``, does not exist — the landed wordings are the
five above; a `DateOnly` drift N4a itself left in that page (`` `that date does not exist` ``
where D54 actually landed `` `a date does not exist` ``) is fixed now, since the page is open
again; the fixtures landed under `tests/surface_timeonly*.tk`, not the `primitives_*.tk`
names the page had used; `ToString`/`Parse`/`TryParse` are out of N4b, the same reason N4a's
own amendment gives; the `(h, mi)` two-argument constructor, missing from the page's own
table, is added to it; `TimeOnly.ToString()`'s own row is removed from that table for the
same reason; and § 6's `types +3`/`syntax +3` are the PAGE's total across all three crumbs
(N4a, N4b, N5), not one crumb's — N4b's own share is `+1`/`+1`, one `type_new` and one type
word.

**Docs.** `docs/reference/datetime.md` gains `## TimeOnly` (after `## DateOnly`) and its
"Under the hood" closing note is updated for the fourth primitive and `DateOnly`'s own
seventeenth row. `docs/reference/types.md` gets the matching short section.
`docs/reference/diagnostics.md`: the "Primitives with members" heading gains a name, a
`TimeOnly` example beside the generated cast refusal, a `### TimeOnly (N4b)` subsection
mirroring `DateOnly`'s, and the panic-list paragraph corrected — `.ToDateTime(t)` is no
longer "not yet". `docs/reference/runtime.md`: the `tk_to_*` signature table, the
`DateOnly.ToDateTime` row, `TIMEONLY_MAX_TICKS`, and the panic table's fifteenth row.
`docs/reference/not-yet.md`: `TimeOnly` and `.ToDateTime(t)` come out of the not-taught
rows, and a `TimeOnly`-shaped `ToString`/`Parse` row goes in beside `DateOnly`'s.
`docs/internals/primitives.md`: the ceilings recomputed and a new "What the fourth primitive
cost: nothing" section. `docs/specs/README.md` row 9b marked landed.

**Proof** (macOS/aarch64, mc 0.15.23, base `d0fbe9a4`):
- `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **71 passed, 31 refused as
  expected, 0 failed** (69/26 on the base): `tests/surface_timeonly.tk` at 42 (58 checks: all
  four constructors, both ends of the range, both other builders agreeing, `.Add` wrapping
  both ways, `.AddHours`/`.AddMinutes` wrapping, `.ToTimeSpan()`, `.IsBetween` ordinary and
  wrapping with both inclusive/exclusive edges, the six comparisons plus `.CompareTo`/
  `.Equals`, `t - t` ordinary and forward-wrapping, `DateOnly.ToDateTime(TimeOnly)`
  round-tripped through both `FromDateTime`s and `.TimeOfDay`, the spec's own § 2 sample
  lifted into a real `expect-exit` block, and the value through a local, a field, a global,
  both shapes of array element and a closure capture — short, since eight bytes needs no
  width proof) — `tests/surface_timeonly_panic.tk` at 70 (`new TimeOnly(864000000000)`, one
  tick past the range, after a page of checks that must not fire: both calendar-free ends of
  a day, the raw-ticks constructor and `FromTimeSpan` at the very top of the range, and
  wrapping arithmetic that goes nowhere near it) — and `tests/refuse/timeonly_{from_int,
  to_i64,to_datetime,cast,plus_timeonly}.tk`.
- `--dump-ast`, `d0fbe9a4`'s own compiler run over both trees (one compiler, two trees —
  D54's own form): the **57** pre-existing `tests/*.tk` that do not include `lib/time.tk`
  are byte-identical, 57 of 57; the **12** that do (`surface_dateonly`,
  `surface_dateonly_panic`, `surface_datetime`, `surface_datetime_kind`,
  `surface_datetime_kind_panic`, `surface_datetime_panic`, `surface_globals_slot`,
  `surface_nullable_ops`, `surface_nullable_value`, `surface_overload_ops`,
  `surface_timespan`, `surface_timespan_overflow`) each differ by exactly **+140 lines, −0**,
  one insertion point at the end of the dump — the `tk_to_*`/`tk_do_to_datetime` functions
  `lib/time.tk` gained and nothing else; `d0fbe9a4`'s OLD compiler dumping `lib/time.tk`'s
  new tail at all is itself a finding worth naming: none of the new functions names
  `TimeOnly`, so an unmodified base compiler parses and dumps them with no error.
- `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK`.
- `mc limits`, verdict `ok` on both legs: the `tests/hello.tk` floor `passes` 15/30,
  `intrin` 8/16 and `syntax` 15/30 **unmoved**, `types` 12 → **13** and `alias` 19 → **20**
  (the fourth `type_new`'s own alias row); the `tests/surface_dateonly.tk` leg — the SAME
  source both before and after, only the compiler under it changed — `passes` and `intrin`
  unmoved, `types` 18 → **19**, `syntax` 19 → **20**, `alias` 25 → **26**.
- `sh scripts/check-docs.sh` → `docs ok: 593 links, 38 fragments, 388 diagnostics, 31
  refusals, 140 samples` — one stale `#fragment` link fixed (the "Primitives with members"
  heading's own slug shifted with the new name), one new `teko: …` literal
  (`a time of day is out of range`), documented.
- `mc pkg hash .` → `246d6defa97f3a76094ddf1469e8512c0a815d4d45269f3bb3565fc219c757d3`.
### D59 · A delegate call's arguments are judged and widened like a direct call's (2026-09-14)
D57 taught the vtable road, the itab road and the unqualified form of either. The DELEGATE
road was the fourth `callp` in the tree and it kept the whole gap: its own door
(`tk_deleg_check_arg_kinds`, teko_deleg.tk) read the `ref`/`out` TAG and, since D57, the
`ref` POINTEE -- and nothing about a value. Measured on `f660795d`, for
`delegate i64 Op(i64)` and `delegate f64 F(f64)`:

| road | narrowing `f64` -> `i64` parameter | widening `i64` -> `f64` parameter |
|---|---|---|
| direct call (the control) | refused `teko: a value of type f64 does not convert to i64` | widened, exit 25 |
| delegate LOCAL, `f(lf)` | accepted, exit 64 | not widened, the raw integer crossed |
| delegate GLOBAL, `gop(gf)` | accepted, exit 96 | -- |
| delegate PARAMETER, `apply(Op f) { f(lf) }` | accepted, exit 64 | not widened |
| delegate FIELD, `h.cb(lf)` | accepted, exit 128 | not widened |
| `Op[]` ELEMENT, `ops[0](lf)` | accepted, exit 0 (the double's bits truncated to a byte) | not widened |

The missing widening is not an exotic shape: `F f = half; f(5);` passed the LITERAL 5 as an
integer where `half(5)` reads 25, and `f64? x` on the parameter made it worse -- the raw
integer was handed to a callee that dereferences it as a box, and the program SIGSEGV'd
(exit 139, measured). The `ref` gap D57 left open was the two roads that build their `callp`
while the file is still being parsed: `class H { public Mut cb; }` with
`delegate void Mut(ref f64 x); i64 g = 0; h.cb(ref g);` compiled and let the callee write
1.0's bit pattern through the `i64`'s own slot, where the bare-name road refuses it.

**ROOT: one choke point, and it was asking one question out of three.** All three roads go
through `tk_deleg_build` (teko_deleg.tk), the single builder of a delegate `callp` -- the
bare name rewritten at pass 8 (`tk_deleg_call`), a FIELD through `tk_field_use`
(teko_expr.tk) and an `Op[]` ELEMENT through `tk_ha_deleg_call` (teko_heaparr.tk) -- and it
calls `tk_deleg_check_arg_kinds` for every one of them. That function's whole judgement was
`if (pk != TK_RP_NONE) tk_ref_check_pointee_ty(dg_pty_at(si, i), a, line, fl);` beside an
inlined copy of the kind check. The signature was in hand all along: `dg_pty_at(si, i)` is
the parameter's own type, per position.

**The fix is D57's own two-line shape, reused, with nothing new invented.** Per argument
position: the `ref`/`out` TAG first, through `tk_ref_check_kind` (teko_ref.tk) -- the direct
call's own words, which the door had been repeating verbatim rather than calling; then the
type, through `tk_vca_park` (teko_typeof.tk). A by-value argument is offered the parse-time
oracle's answer (`tk_pty_of`, teko_struct.tk) and a `ref`/`out` one is offered none at all,
because no oracle at this door types what an address points at. What the park keeps is
judged at the end of `tk_over_pass` (pass 14, `tk_vcall_arg_judge`) by the two rules it
already carries -- `tk_ty_of` + `tk_check_compat` + `tk_fs_convert` by value,
`tk_ref_check_pointee_ty` for a pointee -- and what it hands back is judged inline, with the
conversion written IN PLACE by `tk_fs_convert`: this walk is over `nd_next` and holds no
handle to relink with, so the node keeps its identity and BECOMES the cast, rather than
`tk_vcall_args_check`'s splice. **No pass of its own** and **no table of its own**: `passes`
stays 15/30 and `globals` 942.

**Which arguments park, and why that is the same rule and not a wider one.** `tk_pty_of`
answers a literal, a cast and an xt-tagged node on EVERY road, and a bare name only while
the parser's own stack is live -- so on the two parse-time roads a local is typed inline and
on the pass-8 road every identifier parks, which is exactly D57's own finding for the
unqualified road. A call to an OVERLOADED name parks on all three (`tk_vca_ov_call`:
`tk_pty_of` would answer the FIRST declaration's return, not the pick's), and
`f(pick(2))` with `f64 pick(f64)` declared ahead of `i64 pick(i64)` is the fixture row for
it. A `ref`/`out` argument parks always, which is what carries the pointee rule onto the
FIELD and ELEMENT roads for the first time. Nothing else changed hands: the whole battery of
legal shapes was measured identical on both builds -- a class reference, a struct, an
interface value, an `str`, a `bool`, an `enum`, a global of each, a local array's element, a
direct call, an arithmetic expression, a delegate LOCAL passed as an argument, a lambda
wrapped in `new Op(...)`, an unqualified field inside a method, and `T?` parameters boxed
from a literal and from a name.

**One shape's diagnostic moved, and it was already refused.** A bare FUNCTION name as an
argument at a delegate call whose parameter is itself a delegate type (`r(twice)` on
`delegate i64 Runner(Op f)`) came out of the core as `unknown name` and now comes out as
`teko: the type of this argument is not known here` at the same line -- the wrap that turns
a function name into a delegate object is written at a call that NAMES its callee, and an
indirect call names none. `r(new Op(twice))` is typed where it stands and runs. That row
replaces, in `docs/reference/not-yet.md`, the by-value row D57 added and this entry closes.

**Fixtures.** Nine refusals, `tests/refuse/deleg_*`: `deleg_arg_narrow.tk`,
`deleg_arg_narrow_global.tk` and `deleg_arg_narrow_param.tk` (the three bare-name slots),
`deleg_field_arg_narrow.tk` and `deleg_elem_arg_narrow.tk` (the two parse-time roads),
`deleg_field_ref_pointee.tk` (the pointee on the FIELD road, which had none) and
`deleg_arg_ref_pointee.tk` (the same on the bare-name road, which D59 moved from pass 8 onto
the shared park -- the refusal and its line stand where D57 left them), plus
`deleg_arg_ref_missing.tk` and `deleg_arg_ref_extra.tk` for the two kind diagnostics now
given by `tk_ref_check_kind` rather than by a copy of it. One positive:
`tests/surface_delegate.tk` grows `argcheck()`, eleven rows over the LOCAL, PARAMETER, FIELD
and ELEMENT roads -- an integer literal and an integer name widened, a global integer, a
right-typed global, a call to an overloaded name, and a `ref` global with the RIGHT pointee
through a field slot. Nine of those eleven are WRONG on the base (measured as a bitmask,
`1015`); the two that stand are the two that ask for no conversion at all. `expect-exit: 42`
unmoved.

**Proof** (mc 0.15.23, macos/aarch64, base `f660795d`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **71 passed, 54 refused as
expected, 0 failed** (was 71/45); `--dump-ast` byte-identical against the base compiler for
**70 of the 71** pre-existing `tests/*.tk`, the one that moves being the fixture this crumb
grew, whose diff is exactly nine added `CAST type=f64` lines and nothing else;
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`;
`sh scripts/check-docs.sh` -> `docs ok: 587 links, 32 fragments, 389 diagnostics, 54
refusals, 138 samples` (no new literal: every message this door gives was already
documented); `mc limits . --config mc.macos.toml` verdict `ok` on both legs, every counted
row of the `tests/hello.tk` leg exactly where the base left it (`passes` 15/30, `types` 12,
`intrin` 8/16, `alias` 19, `syntax` 15) and the compiler leg moving DOWN by the inlined
error construction this crumb deleted -- `nodes` 156214 -> 156205, `ins` 215408 -> 215350,
`funcs` 3178, `lowered` 3160, `symbols` 6240, `strings` 2120 and `globals` **942** all
unmoved; `mc pkg hash .`
`64615adc8c576867df94b79f1193d40dbe96f3ea3cf0a197921ffe870d9be6be` (base
`14b54b7d0a7175813638eb7fb8bbeacf8c9f4be1fa9b912ce29d6b2cdcfbc73f`: `teko_deleg.tk` is a
listed file, so the hash moves by design).

**Copilot finding, second pass: the park read two shapes, and every other one crossed
raw.** `delegate f64 F(f64); F f = half; f(1 + 2);` compiled and the integer 3 never
reached the parameter at all -- `tk_pty_of` answers -1 for a BINARY, `tk_vca_park` parked
only an `N_IDENT` and a call to an overloaded name, so the door took its `at < 0` path,
converted nothing, and the callee read the FP register instead of the integer one. The
direct `half(1 + 2)` widens. The same gate stood on D57's three roads, where D57's own
second pass had DOCUMENTED it as a silent skip -- "a by-value argument that is neither a
bare name nor an overloaded call" -- and a silent wrong result is worse than a refusal.
Measured on `9b6c4180`, one shape at a time, each on six roads (direct, delegate, vtable,
itab, unqualified, `this.`), the argument's value 3 onto an `f64` parameter:

| argument shape | direct | the five indirect roads |
|---|---|---|
| `1 + 2` | widened | **all five wrong** |
| `x + 2` | widened | **all five wrong** |
| `x > 0 ? 3 : 4` | widened | **all five wrong** |
| `0 - (x - 4)` | widened | **all five wrong** |
| `arr[0]`, `h.n` | widened | right (the xt table tags an element load and a field load) |
| `(i64) y` | widened | right (`tk_pty_of` has an `N_CAST` arm) |

...and the narrowing mirror, `f(lf * 2.0)` into a `delegate i64 Op(i64)` and
`b.takei(lf * 2.0)` into a `virtual i64 takei(i64)`: both compiled, exits 64 and 16, where
the direct call is refused.

**ROOT: the restriction was at the wrong end.** D57's reason for parking only a name was a
FALSE REFUSAL -- a shape the pass cannot type either would turn today's silence into a
refusal of code with nothing wrong with it -- and that is a question for the JUDGE, which
knows the answer, not for the door, which does not. So `tk_vca_defer` now parks EVERY
by-value argument the parse-time oracle could not type, and `tk_vca_do` decides on what
`tk_ty_of` gives back: a type means `tk_check_compat` + `tk_fs_convert`, exactly as before;
-1 is refused for the shapes D57 parked (a bare NAME, where the scope, the global table and
the field load are precisely what this walk has, so a name still unanswered is a name
nothing declares -- D57's invariant, unmoved) and skipped SILENTLY for the rest, which is
what the door itself did before. The three-way distinction is the column that used to be a
`ref`/`out` flag (`TK_VCA_VAL`/`TK_VCA_REF`/`TK_VCA_NAME`): no new array, `globals`
**943** unmoved.

**FIXTURES** (2 refusals, 2 positives grown). `tests/refuse/deleg_arg_expr_narrow.tk` and
`vcall_arg_expr_narrow.tk`, both measured COMPILING on `9b6c4180`. `tests/surface_delegate.tk`
`argcheck()` grows seven rows -- a binary over two literals, a binary over a name, a ternary
and a negation on the LOCAL road, then the FIELD, ELEMENT and PARAMETER roads -- and exits
**152** on that head (row 12, the first of them). `tests/surface_globals_calls.tk` gains
section 9, `exprcheck()`, the same four shapes on the vtable, itab, unqualified and `this.`
roads, each road first passing 2.0 so that a row which crossed RAW reads that 2.0 back
instead of its own 3.0: **102** on that head, 42 here.

**Proof** (mc 0.15.23, macos/aarch64, base `9b6c4180`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 61 refused as
expected, 0 failed** (73/59 on the base); `--dump-ast` against the base compiler for all 73
pre-existing `tests/*.tk` -- **byte-identical on 71**, and the two this crumb grew move by
exactly 7 and 12 inserted `CAST type=f64` lines and nothing else (both dumps compare equal
once those lines and the indentation they add are removed), every one of them on a call that
computed the wrong bits before; `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK`; `sh scripts/check-docs.sh` -> `docs ok: 598 links, 41 fragments, 389
diagnostics, 61 refusals, 141 samples`; `mc limits . --config mc.macos.toml` verdict `ok` on
both legs, the `tests/hello.tk` leg byte-identical to the base's (`passes` 15/30, `types` 13,
`intrin` 8/16, `alias` 20, `syntax` 15) and the compiler leg moving only by the added surface
code -- `nodes` 156549 -> 156565, `ins` 215960 -> 215968, with `globals` 943, `funcs` 3182,
`lowered` 3164, `strings` 2138 and `symbols` 6263 all unmoved; `mc pkg hash .`
`15c2fdc3d62c5110cd8b5589f28beb3a3c837cc49ecc7a8c0c0b46f8688d1e6e` (base
`f2a456cbe8da2786a861d66ae62c2587a90c95812c80b4c3e5b09a8545ba88f8`: `teko_typeof.tk`, `teko_expr.tk` and `teko_deleg.tk` are
listed files, so the hash moves by design).
### D60 · A type word already taught refuses a second `class`/`struct`/`interface`/`delegate` under the same name (2026-09-14)
`class TimeSpan { public i64 v; }` (a teko primitive, `teko_time.tk`), `class f64 { ... }`
(mc's own core word, its bundled `<float>` module) and `class usize { ... }` (one of the
seven `type_alias` words `teko_type.tk` declares) all compiled SILENTLY, with the last
registration winning program-wide: `class f64 { public i64 v; } f64 x = 1.5;` refused with
`teko: a value of type f64 does not convert to f64` (both sides of `=` now the class's own
row), and `class TimeSpan {} ... t.v` refused with `teko: unknown member of TimeSpan: v`
(the primitive's own row, not the class's). `tk_newname` (`teko_struct.tk`) is the one door
every one of those four constructs takes for its name, and its duplicate guard
(`alias_find(p_id()) >= 0`, `teko_struct.tk:1463-1464`, the existing `teko: the name is
already a type` wording D20 already names) is short-circuited by the `tk_fwd_pending`
branch just above it: the byte-level forward scan (`tk_fwd_reg_type`, `teko_fwd.tk:353`,
D50's own pre-pass so a use above its declaration still resolves) calls `type_new(qname, 8,
8, TK_INT)` BLIND, guarded only by its own table (`tk_fwd_find`, its only guard), so a
`class TimeSpan` lands a SECOND row under the same word in mc's type table before the real
declaration is ever read -- `type_new` appending unconditionally is mc's own documented
contract (`src/hooks.mc:637-639`, read-only, D2), not a defect to report. `enum` has no such
pre-scan (D216: no `type_new` at all) and was the one construct already refused correctly.

**The fix**, entirely inside `tk_newname`'s own `tk_fwd_pending` branch, on the
declaration's own line (the pre-scan carries no line number to refuse from,
`tk_fwd_try_decl`): `tk_type_word_shadowed(qname)` answers 1 when `qname` is a type word
registered MORE THAN ONCE in mc's type table -- `type_count()`/`type_name(t)` counted, since
the pre-scan's own blind `type_new` is what put the second row there -- which catches every
`type_new`-based collision (`TimeSpan`, `DateTime`, `DateOnly`, `ref`, `out`, `params`, `i8`,
`i16`, and mc's own `f64`/`f32`/`f64raw`/`i32`) with zero new state. The seven
`type_alias`-based words (`bool`, `char`, `byte`, `isize`, `usize`, `ptr`, `str`,
`teko_type.tk:74-80`) leave no row in mc's type table for that count to see -- `type_alias`
appends to the alias table alone -- and mc gives no by-name alias lookup that survives the
scan (`alias_find` takes a token id and answers only the LAST registration, which after the
scan's own blind `type_new` is always the scan's own row: measured empirically, not a
documented gap to report). Teko owns those seven words, so `tk_type_word_shadowed` checks a
short table beside their declaration instead -- the same corridor, one more list. A
namespaced type is unaffected either way: `namespace geo { class TimeSpan {} }` qualifies to
`geo__TimeSpan` (`tk_ns_qualified_name`), a name neither list nor count ever matches, so the
word stays free per namespace exactly as D-unnumbered (§31 N1, `teko_struct.tk`'s own
comment) already promised. A genuine duplicate (`class Cell {}` twice, no namespace) never
reaches this branch at all -- its second occurrence's token is already a reserved word by
the time it is read, so it falls straight to the pre-existing `alias_find` guard, unchanged.

**Fixtures.** Six refusals, `tests/refuse/type_word_{class,struct,iface,deleg,core,alias}.tk`
-- one per construct (`class`/`struct`/`interface`/`delegate`) plus the mc-core and the
`type_alias` collision, each `// expect-refuse: teko: the name is already a type: <name>` at
the declaration's own line. One positive: `tests/surface_namespace.tk` grows a `geo.TimeSpan`
class (measured first: the un-fixed compiler already accepted it, silently, the same gap) and
a `main()` read of its own field, proving the namespaced word still resolves to the class, not
the primitive, `expect-exit: 42` unmoved (an early `return 9` guards the new check without
touching the existing sum).

**Docs.** `docs/reference/diagnostics.md`'s `"teko: the name is already a type"` entry widened
to name the three sources (a teko primitive, mc's own core word, a `type_alias` word) and the
namespace exception; no `docs/reference/not-yet.md` row named this gap, so none is removed.

**Proof** (mc 0.15.23, macos/aarch64, base `d0fbe9a4`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **69 passed, 32 refused as
expected, 0 failed** (was 69/26: six fixtures added, one grown); `--dump-ast` byte-identical
against the base compiler for every one of the 68 other pre-existing `tests/*.tk` fixtures (no
hook module change alters an ACCEPTING parse -- the new code path is reached only when the
refusal fires); `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK`; `sh
scripts/check-docs.sh` → `docs ok: 585 links, 30 fragments, 388 diagnostics, 32 refusals, 137
samples`; `mc limits . --config mc.macos.toml` verdict `ok` on both legs, `passes` 15/30,
`types` 12, `intrin` 8/16, `alias` 19, `syntax` 15 -- every table exactly where the base left
them, only the size-of-surface-code rows moved (`nodes` 176853 → 177006, `ins` 214500 →
214683 on the compiler leg; the `heap` column is not a proof figure -- it reflects the state
of `build/` at the time of the run, D55's lesson); `./build/teko limits
tests/hello.tk` byte-identical to the base compiler's own output; `mc pkg hash .`
`aec7809911795c16d3438606780ab75064b06a4d9b085b27148b66851b4f7dab` (base
`0d0b6fa61e30ea12c7cb8ae1bd60b4db9827f53a4a5c5d67d8a74c038ae62795`: `teko_struct.tk` is a
listed file, so the hash moves by design).

### D61 · A call is typed by the pick and by the slot, at the `.` door too (2026-09-14)
*(The number D61 was reserved when this crumb was cut; the entry is appended here, after
D60, which is where it belongs chronologically.)*

Two defects met on ONE door -- the `.` whose RECEIVER is a CALL, over a primitive with
members (`DateOnly`, `DateTime`, `TimeSpan`, `TimeOnly`) or an enum. Measured on
`d028a7a0`:

| written | before | after |
|---|---|---|
| `DOp f = mk; f().DayNumber` (`delegate DateOnly DOp()`) | refused `teko: unknown member: DayNumber` | 738944 |
| the same through a parameter, a global slot, a lambda bound to a slot | refused, identically | reads the member |
| `DateTime pick(i64)` ahead of `DateOnly pick(i64, i64)`, `pick(1, 2).Day` | **exit 1** -- the DateOnly value lowered through the DateTime row | 29 |
| the mirror order, `TimeSpan pick(i64)` first, `pick(1, 2).Day` | refused `teko: unknown member of TimeSpan: Day` on a legal program | 29 |

A LOCAL in between (`DateOnly d = f(); d.DayNumber`) always worked, and so did a delegate
FIELD call (`h.cb().Day`) and every call the parser itself writes -- direct, static,
method, virtual, interface, constructor.

**ROOT A: the pass-time oracle had no arm for a call through a slot.** `tk_typeof_pass`
(pass 6, teko.tk) resolves the deferred `.`; `tk_deleg_pass` (pass 8) is what rewrites
`f()` into a `callp`. Until it runs, the call's name is the SLOT's, no declaration owns
it, and `tk_ty_of`'s N_CALL arm (teko_typeof.tk) asked `tk_ov_find`/`decl_find` only --
both -1 -- so the member was resolved by its own name and refused. The lookup already
existed one file over, `tk_deleg_expr_ty` (teko_deleg.tk), written for `tk_ov_arg_ty` for
exactly this reason. Reordering the passes is not the fix and is rejected: the delegate
pass reads types the typeof pass writes (teko.tk's own note on the order).

**FIX A:** the two lines move INTO `tk_ty_of`'s N_CALL branch, ahead of the overload
table, with `tk_deleg_row`/`dg_ret_at` forward-declared as `tk_nl_pend` already is. Every
consumer of the oracle -- the operator pass, the ternary, `??`, a primitive row's
argument, the deferred `.` -- gets the one answer. `tk_deleg_expr_ty` then read
`return tk_ty_of(e)` and is DELETED, its three call sites (`tk_deleg_coerce`,
`tk_deleg_store_late`, `tk_ov_arg_ty`) asking the oracle directly.

**ROOT B: `tk_dot` still typed a call receiver by the FIRST declaration.** D49 took
`decl_ret(decl_find(name))` out of the pass-time oracle and left it standing at the parse
door: `tk_dot` (teko_expr.tk) asked `tk_pty_of` (teko_struct.tk), whose N_CALL arm is
exactly that guess, and handed the answer to `tk_prim_dot`. On an overloaded callee the
guess is wrong in both directions -- a silently wrong RESULT in one order, a refusal of a
legal program in the other.

**FIX B:** a call receiver carrying no `tk_xt_ty` tag of its own is DEFERRED
(`tk_defer_member`) instead of typed here, so `tk_prim_pend` -- `tk_prim_dot`'s twin, at
`atpass` -- answers with the pass-time oracle, which is `tk_ov_pick` for an overloaded
name and, since fix A, the delegate table for a call through a slot. A call the node DOES
type (`new DateOnly(...)`, a static or an instance row the parser already emitted, all
`tk_xt_put`) keeps the parse-time road unchanged.

**The choice inside fix B, measured rather than assumed.** `atpass` also flips the oracle
a row's ARGUMENT is checked with (`tk_prim_ty_of`, teko_prim.tk), so the deferral could
have moved an accepted tree; the narrow alternative was to defer only receivers whose
callee name carries more than one declaration. The BROAD form was measured first and kept:
`--dump-ast` is byte-identical against the base compiler for all 73 pre-existing
`tests/*.tk`, `tomorrow(leap).Month` (surface_datetime.tk) among them, and for a probe of
the shape at risk, `tomorrow(leap).AddTicks(k).Day` with `k` a parameter -- identical tree
and identical exit on both compilers. A narrowing nothing measures is complexity, so it
was not written.

**Fixtures.** `tests/surface_prim_call_member.tk` (`expect-exit: 42`, 33 checks): the
delegate LOCAL reading `.DayNumber`, `.Year`, `.Month` and the chain `.AddDays(1).Day`;
the delegate PARAMETER; the GLOBAL slot; a lambda bound to a slot; `TimeSpan.Ticks`,
`DateTime.Day`/`.Hour`/`.Date.Day` and `DateTimeKind.ToString()` through a slot; both
overload orders; and the seven receiver shapes that already worked, anchored because the
fix moves every call receiver onto the deferred road. `tests/refuse/deleg_call_member.tk`:
`f().Nope` is now refused `teko: unknown member of DateOnly: Nope` -- the TYPE named,
where the same line used to be refused by the member's name alone.

**Docs.** `docs/reference/delegates.md` says a call through a slot carries the delegate's
return type wherever it is read; `docs/reference/diagnostics.md` records that a CALL
receiver is among the receivers `teko: unknown member of <type>: <name>` now names;
`docs/reference/not-yet.md` gains the row no page owned -- two METHOD overloads of one
arity are refused `teko: ambiguous overload; two signatures take this many arguments`,
the method road picking by argument COUNT alone (the ruling in
`tests/surface_overload_method.tk`) -- and its `switch`-subject row now lists the `.` on a
call result among the sites a PASS types.

**Proof** (mc 0.15.23, macos/aarch64, base `d028a7a0`): `mc build . --config
mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **74 passed,
62 refused as expected, 0 failed** (was 73/61); `--dump-ast` byte-identical against the
base compiler for every one of the 73 pre-existing `tests/*.tk`; `sh scripts/bootstrap.sh
--os macos --arch aarch64` → `FIXPOINT OK`; `sh scripts/check-docs.sh` → `docs ok: 598
links, 41 fragments, 389 diagnostics, 62 refusals, 141 samples`; `./build/teko limits
tests/hello.tk` byte-identical to the base compiler's own output -- `passes` 15, `syntax`
15, `alias` 20, `types` 13, `intrin` 8, no registration of any kind added -- and `mc
limits . --config mc.macos.toml` `ok` on both legs with only the size-of-surface-code rows
moving (`nodes` 179091 → 179246, `ins` 215968 → 215980, and `funcs`/`symbols` DOWN by two,
the deleted helper); `mc pkg hash .`
`c71e2f944b688166f76bad309b374fa2ab32add347cec1d0c92666258451c8af` (base
`15c2fdc3d62c5110cd8b5589f28beb3a3c837cc49ecc7a8c0c0b46f8688d1e6e`: three hook modules and
two fixtures are listed files, so the hash moves by design).

**Verifier finding, first pass (the ceiling the broad form reaches).** Every untagged call
receiver now waits in the `pd_*` table, which the deferred `.` road shared at `TK_MAXPEND`
128 — a function holding 129 reads such as `s = s + mkday().Day;`, a single non-overloaded
callee, compiled on the base at any count tried (1000) and refused on this branch's first
head with `teko: too many member accesses on a value of unknown type`. The table is now
4096 (nine arrays of that many words), the same size `TK_MAXXT`, `TK_MAXFS` and the argument
park use; measured: 600 such reads in one function compile and run (exit 248 = 600·29 mod
256), `mc limits` verdict `ok` with `globals` 943/2544 unmoved and `grow` 0 on every row.
The ceiling is a fact of the shared table, not a fixture: a refuse fixture that pins it
would break the day it grows.

**Copilot findings on #718 (two, both at the root).**

*1. The deferral guard stood one lookup too late.* Fix B's check
(`nd_kind(left) == N_CALL && tk_xt_ty(left) < 0`) was written BELOW
`i64 si = tk_struct_of_expr(left)` in `tk_dot` (teko_expr.tk), and that lookup carries an
N_CALL arm of its own -- `tk_struct_by_ty(decl_ret(decl_find(name)))`, the same
first-declaration guess, for a receiver whose type is a ROW. So a call whose FIRST
declaration returns a class, a struct or an enum never reached the deferral at all: it went
through `tk_member_of` with the wrong row. Measured on this branch's own head
(`3546684c`), one probe per shape:

| written | before | after |
|---|---|---|
| `Cell cpick(i64)` ahead of `Box cpick(i64, i64)`, `cpick(1, 2).w` (both declare `w`, at 8 and at 0) | **exit 0** -- the Box read at Cell's offset | 7 |
| the same pair, `cpick(1, 2).v` (only Box declares `v`) | refused `teko: unknown member of Cell: v` on a legal program | 9 |
| the mirror order, `cmirror(1, 2).pad` (only Cell declares `pad`) | refused `teko: unknown member of Box: pad` | 100 |
| the struct pair, `spick(1, 2).w` (`SPad`/`SBare`) | **exit 0** | 7 |
| two enums, `epick(1, 2).ToString()` / the mirror order | the other enum's name table | `Wild` / `Green` |

The fix is the move: the untagged-call check now stands ahead of BOTH oracles, since both
answer a call the same wrong way. The BROAD form is kept a second time, and measured the
same way: `--dump-ast` is byte-identical against `59652293`'s compiler for all **73**
pre-existing `tests/*.tk` with that commit's own sources. One dump DOES move, on a shape no
fixture had before -- a program that reads `.ToString()` off a call returning an ENUM emits
`Hue__names`/`Hue__vals` at the END of the unit instead of ahead of `main`, because
`tk_enum_ensure` (teko_enum.tk) now runs from the pass rather than from the parse: the two
globals are byte-identical in content, only their position in the dump moved, and the
values they answer are the fixture's own rows 46-49. Everything else in that probe --
`mk().w`, `mk().in.v`, `mk().bump()`, `mks().b`, a 200-iteration loop over `mk().w` -- dumps
byte-identical on both compilers. No narrowing to overloaded callees was written: nothing
measures a reason for it.

*2. The diagnostics table still quoted the old ceiling.* `docs/reference/diagnostics.md`
read `128 waiting for the pass` for `teko: too many member accesses on a value of unknown
type` after `TK_MAXPEND` became 4096; the row now names the new ceiling, what spends a slot
in it (a receiver the parser cannot type, and since D61 any untagged call receiver) and
what the 128 used to cover. `TK_MAXPEND` and that sentence are quoted nowhere else in
`docs/` -- grepped over `docs/internals/` and `docs/reference/` -- so the one row is the
whole fix.

**Fixtures.** `tests/surface_prim_call_member.tk` keeps its name (a rename would only churn
what D61 and `docs/reference/diagnostics.md` already cite; its header names the widened
reach) and grows to **52 checks**: rows 34-41 and 43-45 are the class and struct pairs in both
orders (42 is the fixture's own success code and is no row), rows 46-49 the two enums in
both orders, rows 50-53 the same three returns with NO
overload anywhere -- the shapes the moved check carries from the parse road onto the
deferred one. `tests/refuse/call_member_unknown.tk` is new: `cpick(1, 2).pad` is refused
`teko: unknown member of Box: pad`, where before the move the same line COMPILED and exited
7, reading Box's own `w` through `Cell.pad`'s offset.

**Proof** (mc 0.15.23, macos/aarch64, pre-fix head `3546684c` merged with `origin/main`
at `111559fb`): `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **74 passed, 64
refused as expected, 0 failed** (was 74/63); `sh scripts/bootstrap.sh --os macos --arch
aarch64` → `FIXPOINT OK`; `sh scripts/check-docs.sh` → `docs ok: 611 links, 41 fragments,
389 diagnostics, 64 refusals, 143 samples`; `mc limits . --config mc.macos.toml` verdict
`ok`, `grow` 0 on every row, and every counted row identical to the pre-fix head's --
`nodes` used 156713, `ins` 216188, `funcs` 3184, `lowered` 3166, `globals` 944, `symbols`
6266, `strings` 2138, `passes` 15, `syntax` 15, `alias` 20, `types` 13, `intrin` 8, `rules`
6, `on_stmt` 4 -- only the `nodes` ESTIMATE moving (179815 → 179872, the comment this crumb
writes); the linux/x86_64 config gives the same compiler-leg table and the same `ok`
verdict; `./build/teko limits tests/hello.tk` identical to the pre-fix compiler's own output
on every table. `mc pkg hash .`
`aa4a07da80f7336a739d3aa37cdc40e8a06da9bc7e38ec24c396bffbaf9c9731` (pre-fix head
`23c318e282b1019fd7eb7aa294e2aac069aa67a90e4fcb2c9579e1e5a2e6c9a4`: `teko_expr.tk` is a
listed file, so the hash moves by design).

### D62 · A bare FUNCTION name lands in a FIELD of delegate type, judged where names have types (2026-09-14)
`Op cb = twice;` on a local wraps the function in the thunk every delegate value shares
(D221 §41), and `h.cb = twice;` on the field beside it answered `teko: the type of this
value is not known here`. So did `this.cb = twice;` and the implicit `cb = twice;` of a
constructor, and so did a STATIC field (`H.scb = twice;`); a function whose signature is
NOT the delegate's gave the same generic sentence, where the local slot names the cause
(`teko: wrong does not match the delegate Op(i64)`, `tk_deleg_check_sig`). The workaround
was a local of delegate type held for one line (`Op cb = twice; h.cb = cb;`, what
`tests/surface_delegate.tk` did) or `new Op(twice)` spelled in full. The ELEMENT of an
`Op[]` already had the answer (D51) and the field road had none of it: the chain
`tk_field_use` (teko_expr.tk) -> `tk_field_store_val` (teko_typeof.tk) -> `tk_fs_vty` asks
`tk_pty_of` for an N_IDENT, a free function's name is in no slot table, -1, `tk_fs_defer`
parks the value, and `tk_fs_do` at the end of `tk_over_pass` asks `tk_ty_of`, which answers
-1 for the same reason and refuses. No delegate validator stood anywhere on that road.

**The seat: the DOOR, not its callers.** Two shapes were open. (a) The element road's
own template (`tk_ha_store`, teko_heaparr.tk) copied at each site that builds a field
store; (b) the ONE door all of them pass through, `tk_field_store_val`. (b) is what
landed, for the reason D54 gave the last time this question came up and the reason
CLAUDE.md's root-cause law gives: a guard in the shared function is a smaller diff than a
guard in every caller, and it leaves no sibling caller behind -- the constructor and the
static field are fixed by the same three lines as `h.cb = twice`, and were never
separately written.

That is not a figure of speech, so the door's callers are named exhaustively rather than
counted (Copilot on #713, which found the earlier wording -- "the four field-store sites"
in `diagnostics.md`, "the five sites" in the source -- short of the truth). Every caller
of `tk_field_store_val`, measured by grep on the final tree: `tk_field_use` (teko_expr.tk,
`p.f = e` and `this.f = e`), `tk_this_assign` (teko_this.tk, the implicit `f = e` of a
method or constructor), `tk_static_use` and `tk_fwd_resolve_static_one` (teko_access.tk,
`H.scb = e` written after and before the type's own declaration), `tk_array_index`
(teko_struct.tk, an element of an ARRAY FIELD), `tk_pend_field` (teko_typeof.tk, a store
through a receiver the parser cannot type yet), `tk_arr_elem_store` (teko_array.tk, a
FIXED array's element) and `tk_ha_store` (teko_heaparr.tk, a `T[]`'s element). The first
six carry a delegate and all six were measured refusing this store on `441be45a` and
accepting it here; the last two cannot -- a fixed array of delegate element type is
refused at its own declaration (``teko: an array of this type is not taught yet``), and a
bare name at a `T[]` element is parked by the element road's own validator and never
reaches the door (D51). `tests/surface_delegate.tk`'s `dfcheck2` covers the three that had
no fixture: the deferred receiver, the array-field element and the forward-referenced
static store.

**What the door may do there is DETECT, never judge.** `tk_deleg_coerce` reads
`tk_ty_scope_or_global`, and at a parse-time door no scope answers for any name at all
(the hazard `tk_fs_vty`'s own header documents in full: a global a parameter or a field
shadows answered for the shadowing name, measured on #698). So the door asks two questions
it can answer with no scope -- is the field's type a delegate row (`tk_deleg_row`), and is
the value a bare `N_IDENT` -- and hands the store to the delegate's own late table
(`tk_deleg_field_late`, teko_deleg.tk) instead of `tk_fs_defer`. `tk_deleg_pass` judges it
with the lexical scope of the site live, which is the element road's own answer, and it
runs BEFORE `tk_ops_pass`/`tk_over_pass`/`tk_rc_pass`, so the four declarations a wrap
emits are walked by every pass that has to see them -- a delegate with a counted return
(`rcheck`, `tests/surface_delegate.tk`) is unaffected. `decl_find` is deliberately NOT
asked as part of the detection: it answers "a function of that name has been read so far",
which is not the question (a PARAMETER shadows that function and nothing the parser keeps
records one, D51's sixth pass) and it would miss a namespaced function the walk resolves
by prefix (`tk_deleg_resolve_fn`).

**One column, no new table.** What waits is the VALUE node, not a store call: the door
hands the value back and its callers build the store around it afterwards, so there is
no store node to key on yet. The existing late table takes it with one more column
(`dl_val`, 1 = "`dl_node` IS the value"), and `tk_deleg_val_do` rewrites the node IN PLACE
(`tk_node_replace` + `tk_xt_move`) -- the same answer `tk_fs_convert` gives a field store's
own widening, and for the same reason: the slot holding the value keeps no handle this
function could re-point. The value node is visited by `tk_deleg_walk` like any other (it
is the second argument of the store it ends up in), so the bucket that keys the table by
node id needed nothing new, and no other entry's behaviour moves (`dl_val` is written 0 by
`tk_deleg_store_defer` for every store-keyed row).

**The namespace order of a bare name, fixed at the shared resolver.** Copilot on #713
read the field road's own tail and found `tk_deleg_resolve_fn` (teko_deleg.tk) asking
`decl_find(fname)` BEFORE `tk_ns_call_try_prefixes` -- the opposite of what
`tk_ns_rewrite_call` (teko_ns.tk) does for a CALL, where the site's own namespace and its
prefixes outward come first and a flat declaration of the exact name only after. So inside
`namespace geo`, with a `col` declared both flat and in `geo`, `col(2, 3)` called
`geo__col` while `Op f = col;` wrapped the flat one: the same name, two different
functions, silently, with no diagnostic anywhere.

**It is PRE-EXISTING, and it is not the field road's.** Measured on the base `441be45a`,
with the same source exercising six roads and an exit code carrying one bit per road: the
local initializer, the assignment, the call argument, the `return` and the `Op[]` element
ALL wrapped the flat `col` there (mask 63 of 63; the field road could not compile at all on
that compiler, which is what this entry's first half fixes). On the PR head before the fix
the seventh road joined them (mask 127 of 127), and after it six of the seven answer the
namespaced `col` (mask 4). The resolver is shared by every road that wraps a bare name, so
one line moved fixes all of them:

    i64 d = tk_ns_call_try_prefixes(fname, tk_deleg_cur_ns);
    if (d < 0 && decl_find(fname) >= 0) return fname;

-- `tk_ns_rewrite_call`'s own order, transcribed. The steps that precede the namespace one
there (a local, a parameter, a member of the walked function's type) are already answered
before this function is reached: `tk_deleg_coerce`'s guard, `tk_ty_scope_or_global`, is
what refuses to wrap a name any slot declares.

The road still out is the seventh, `new Op(col)`, and it is a different seat: that wrap is
built while the file is still being READ (`tk_new_deleg` -> `tk_deleg_wrap` ->
`tk_deleg_thunk`), and `tk_ns_scan_decls` mangles declarations in a later pass, so no
namespaced name exists to resolve at the moment the thunk is emitted -- `new Op(only_geo)`
inside `geo`, with no flat twin, dies `call to unknown function` at lowering on `441be45a`
and here alike. Deferring the thunk to `tk_deleg_pass` is a redesign of the explicit form,
not a patch to a resolver; the row is in `docs/reference/not-yet.md` with the workaround
(the contextual form, which IS built in the pass and does resolve). `tests/surface_delegate.tk`'s
`geo.ns_roads` locks the six that work, including the fallback to a flat `add` no `geo__add`
competes with.

**Scope.** A value the door DOES type keeps the verdict it has today, byte for byte: a
local of delegate type, a `new Op(...)`, a call, a literal, and every field that is not of
delegate type. The one wording that moves is a name of another type written into a delegate
field -- `i64 g_n; h.cb = g_n;` now reads `teko: Op takes a function, another Op, or null`,
the delegate's own sentence, where the general conversion words answered before; both are
refusals, and the new one is the element road's, already in
`docs/reference/diagnostics.md`. An `Op?` field is a `TK_KNULL` row of its own, which
`tk_deleg_row` answers -1 for, so it declines the detour and keeps the refusal
`docs/reference/not-yet.md` already records for a bare name into a `T?` delegate slot.

**Out of scope, measured and recorded in `docs/reference/not-yet.md`** (both identical on
the base compiler, neither introduced here): a LAMBDA written straight into a field
(`h.cb = (i64 x) => x * 2;`) dies `expected ) in cast` in the core's parser -- the field
store reads its right-hand side with `parse_expr` alone and has no counterpart to
`tk_deleg_init_expr`'s lookahead, which is a parse-time hole in the field road, not a
judging one; and calling a delegate through a STATIC field by its qualified name
(`H.cb(21)`) dies `teko: unknown member: cb`, because `tk_static_use` (teko_access.tk) has
no delegate-call branch where `tk_field_use` has one. `Op f = H.cb; f(21);` is taught and
is what the fixture does.

**Fixtures.** `tests/surface_delegate.tk`: `fcheck`'s workaround is gone -- `h.cb = add;`
direct, which IS the proof (the base compiler refuses the file at that line) -- and a new
`dfcheck` covers the constructor (`this.cb = add;`), the implicit form (`made = mul;`), the
STATIC field (`D62H.scb = add;`, read back through a local and called) and the name that is
a PARAMETER shadowing a free function `p` of the same name (`setcb(h, mul)`, whose call
afterwards proves the parameter is what landed). `dfcheck2` covers the three callers of the
door that had no fixture at all: a receiver typed at pass time (`void set_late(LateH h)`,
with `LateH` declared BELOW it), an element of an ARRAY FIELD (`h.cbs[0] = mul;`) and the
forward-referenced static store (`LateH.scb = mul;` written above the class). `geo.ns_roads`
locks the namespace order over six roads at once, with `col` declared both flat and inside
`geo` and a flat-only `add` for the fallback. `dfcheck` and `dfcheck2` run near the end of
`main` and assert `rt_live()` at 1 then 2: a static field outlives every scope, so each
helper's own delegate is still live, and that count is exactly what must remain.
`expect-exit: 42` unmoved. One refusal, `tests/refuse/deleg_field_name_sig.tk`: a
wrong-signature function into the field, the delegate's own sentence at the store's own
line.

**Proof** (mc 0.15.23, macos/aarch64, base `441be45a`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 51 refused as
expected, 0 failed** (was 73/50, one refusal added and one fixture grown); `--dump-ast`
byte-identical against the base compiler for all 73 pre-existing `tests/*.tk` with their
`441be45a` sources -- the 72 the tree still carries verbatim, plus `surface_delegate.tk`'s
own base source compiled by both binaries; `sh scripts/bootstrap.sh --os macos --arch
aarch64` -> `FIXPOINT OK`; `sh scripts/check-docs.sh` -> `docs ok: 611 links, 41 fragments,
389 diagnostics, 51 refusals, 143 samples`; `mc limits` verdict `ok` with `grow` 0 on every
row of both legs (macos/aarch64 and linux/x86_64), every counted table exactly where the
base left it -- `passes` 15, `types` 13, `intrin` 8, `alias` 20, `syntax` 15, `rules` 6,
`on_stmt` 4 -- and only the size-of-surface-code rows moved (`nodes` 156558 -> 156697,
`funcs` 3182 -> 3185, `lowered` 3164 -> 3167, `globals` 943 -> 944 for `dl_val`, `symbols`
6263 -> 6267, `ins` 216018 -> 216226, identical on both legs; `strings` unmoved at 2138);
`mc pkg hash .`
`fda769ca698d63e4ccddfbc3a5d2820edfb5b8c3bac24be8b20adc9e55a006dc` (base
`571bf6db10a035eded3f0b36a36abda17fe2d6d82adedb6db6f4041c5523ab75`: `teko_deleg.tk` and
`teko_typeof.tk` are listed files, so the hash moves by design).

### D63 · `new Op(f)` and `Op f = ...` are one thunk, and the `ref` pass does not judge the compiler's own forwarding (2026-09-14)
`delegate void Mut(ref f64 x); void bumpf(ref f64 x) { x = x + 1.0; }` was accepted through
`Mut m = bumpf;` and refused through `Mut m = new Mut(bumpf);`, at the `new` line, with
``teko: argument 1 needs `ref` at the call site`` — the same for `out`. A FALSE refusal, not
wrong codegen: the machine code both roads produce was already correct, and the contextual
road was running a `tests/surface_globals.tk` row (`delegrefcheck`) the whole time.

**The root cause.** Both roads build the SAME wrapper — `tk_deleg_thunk` memoizes one per
(delegate row, function), so there is exactly one `Mut__thunk_bumpf` whichever spelling asks
for it — and that wrapper (`tk_deleg_thunk_fn`, `teko_deleg.tk`) forwarded every parameter as
a bare `tk_id("a0")`, typed by `dg_pslot_at` (`TY_UPTR` for a `ref`/`out` one, K2w) and never
reading `dg_pk_at`, the KIND column beside it. `bumpf(a0)` is an address passed with nothing
saying it is one, and `tk_ref_check_kind` (`teko_ref.tk`) refuses exactly that. Only the
`new` road refused because `new` builds its thunk during the PARSE, so `tk_ref_pass`
(`teko.tk`, pass 12) walks it; the contextual road builds the identical thunk inside
`tk_deleg_pass`, which runs AFTER that walk, and nothing looked at it. One road checked, one
road not — and the check being asked of a body no program wrote is itself the defect, which
is where the second pass below lands.

**Two gates the forwarded argument has to satisfy, if it is to be judged at all.** (1) `tk_ref_check_kind` reads the
argument's TAG (`tk_rfarg_kind`), so the argument must be tagged with the delegate's own
kind. (2) `tk_ref_check_pointee_ty` reads `tk_ref_arg_pointee`, which for an `N_IDENT`
PREFERS the lexical scope — the right rule for a `ref x` the source wrote, where the tag is
the parser's guess (`tk_slv_find`) and the scope is the truth (higiene 3, item A) — and the
scope holds `a0` under the `uptr` it is declared at, there being no `tk_rp_*` row for it:
`uptr` against `f64` would be refused in the other words.

**The four seats, measured** (the first three on the first pass, the fourth on the second).

| seat | what it costs | verdict |
|---|---|---|
| (i) tag the argument and wrap the name in `(uptr)`, so it is not an `N_IDENT` and the pointee oracle falls to the tag | 12 lines, `teko_deleg.tk` alone — and one row of `TK_MAXRFARG` per forwarded `ref`/`out` parameter, out of the budget the SOURCE's own arguments draw on | taken on the first pass, **rejected** on the second |
| (ii) `tk_rp_add` on the thunk's own parameter, so the SCOPE answers the pointee | `tk_ref_walk` then rewrites `a0` into `ld64(a0)` and breaks the forward — needs a guard there — and an `out` thunk trips `tk_ref_check_out_assigned` unless it is also marked seen: two files, three new branches | rejected |
| (iii) `tk_addr("a0")` — `&a0` | the thunk's OWN slot, not the caller's; the repass road that rewrites `&x` into `x` (`tk_ref_walk`'s `N_ADDR` branch) never fires here, since `a0` carries no `ref` row | rejected, wrong code |
| (iv) do not judge the thunk at all: `tk_ref_pass` skips a function the delegate table itself names as a forwarder | one column on the memo table `tk_deleg_thunk` already keeps, one predicate, one `&&` in the pass loop — and the forwarded argument goes back to the bare `tk_id(pn)` it always was | **taken**, second pass |

Seat (i) was `tk_deleg_thunk_arg`: a by-value parameter stayed the bare name, and a `ref`/`out`
one became `tk_cast(TY_UPTR, tk_id(pn))` tagged `tk_rfarg_tag(a, dg_pk_at, dg_pty_at)`. The
tag is the DELEGATE's, which is the callee's by then — `tk_deleg_check_sig` has already proved
kind and type identical parameter by parameter before any thunk exists, which is also why the
mismatch (`void byval(f64 x)` on a `Mut`) is refused, at the `new` line, by name, under every
seat in the table.

**Copilot finding, second pass: the tag is spent out of the program's own budget.** The
`rf_*` table (`teko_ref.tk`, `TK_MAXRFARG` 512) is unit-wide and holds one row per `ref`/`out`
ARGUMENT. Seat (i) put the compiler's own forwarding in it: one row per forwarded parameter,
`TK_MAXDGI` (64) wrapper pairs × the delegate's arity, against the same 512 rows every
argument the SOURCE writes draws on. Measured, one generated unit, 30 delegate types of 8
`ref i64` parameters each (one target function per type, so 30 wrappers = 240 forwarded
parameters) plus N `src(ref x)` calls, the same text compiled by the base compiler
(`441be45a`) and by seat (i) (`cd844c2a`):

| N source `ref` arguments | base `441be45a` | seat (i) `cd844c2a` | seat (iv) `0ef92adc` |
|---|---|---|---|
| 272 | accepted | accepted | accepted |
| 273 | accepted | **segfault (139)** | accepted |
| 500 | accepted | segfault (139) | accepted |
| 512 | accepted | segfault (139) | accepted |
| 513 | segfault (139) | segfault (139) | refused (the table's real ceiling) |

The overflow is a SEGMENTATION FAULT everywhere the guard below is still the swapped
`err_at`, which is every column but the last: exhausting `TK_MAXRFARG` crashed the compiler,
it did not refuse. The word "refused" belongs to seat (iv)'s tree alone, where the same
overflow finally speaks — ``teko: too many `ref`/`out` arguments in one unit``, with the file
and the line (re-measured on the third pass, mc 0.15.23, macos/aarch64; the probe is
regenerated per N and no row is quoted from an earlier run).

Seat (i) cost the unit 240 of its 512 rows — code no one wrote — and a legitimate 273rd
source `ref` argument then crashed the compiler where the base compiler takes 512. Seat (iv)
puts the whole budget back: 512 on both roads, `new` and contextual alike, which is more than
the base manages, since the base cannot compile the `new` road at all (that is this
decision's own bug).

**Why seat (iv) is the root and not a hole punched in a check.** A thunk is not a body the
program wrote and then had exempted; it is a body the COMPILER wrote, argument by argument,
out of `dg_pk_at`/`dg_pty_at`, over a target `tk_deleg_check_sig` has already proved identical
to the delegate parameter by parameter. Correct BY CONSTRUCTION, so there is nothing in it for
`tk_ref_pass` to verify — and nothing for it to rewrite either: the thunk's own parameters
carry no `tk_rp_*` row, so both rewriting branches of `tk_ref_walk` were already inert there,
and `tk_ref_fn_has_refout` is already 0, so the `out` prologue and the never-assigned check
were already skipped. The walk's ONLY effect inside a thunk was `tk_ref_check_call` asking a
question about a call the compiler itself had just written, and answering ``argument 1 needs
`ref` at the call site``. D63's rule, restated: **the `ref` pass does not judge the compiler's
own forwarding.**

The mark is an IDENTITY, never a name pattern. `tk_deleg_thunk`'s memo table already holds one
row per (delegate row, function) pair; the forwarder's `N_FUNC` node becomes that row's fourth
column (`dgi_thunk`), and `tk_deleg_is_thunk(f)` is the one question `tk_ref_pass` asks. D48's
`tk_emit_owns`/`tk_emit_is` registry could not carry this: it records every GENERATED
declaration, and a lambda body is generated too while holding the code the program wrote —
that body keeps every check the same code written inline would get. Reserving the spelling
`Op__thunk_add` is teko_over.tk's job and a different question.

**A second defect the probe surfaced, fixed here.** `tk_rfarg_tag`'s own overflow guard read
`err_at(tk_line, tk_file, …)` — the two arguments the wrong way round, where `tk_rp_add`
fifty lines above spells `err_at(tk_file, tk_line, …)`. A unit that exhausted `TK_MAXRFARG`
therefore read an integer line number as a file POINTER and died of a segmentation fault
instead of refusing: measured on the base compiler too (`441be45a`, 513 source `ref`
arguments, exit 139), so it is older than this decision and reachable from pure source. It now
refuses, with the file and the line: ``teko: too many `ref`/`out` arguments in one unit``.
No new message, no new fixture — a 519-line generated fixture would pin the ceiling's exact
value rather than the guard, and would have to be rewritten the day the table grows.

**Fixtures.** `tests/surface_globals.tk`'s `delegrefcheck` grows four rows: `new Mut(bumpf)`
and `new MutI(bumpi)` (the two pointees the contextual rows already prove, now on the `new`
road) and a `Setter`/`seti` pair — `delegate void Setter(out i64 x)`, the first `out` delegate
any fixture has — on BOTH roads, `return 5` through `return 8`, `expect-exit: 42` unmoved. One
refusal, `tests/refuse/deleg_new_kind.tk`: `new Mut(byval)` over `void byval(f64 x)` stays
refused by `tk_deleg_check_sig`, `teko: byval does not match the delegate Mut(ref f64)` at the
`new` line — untouched by the second pass, which changes nothing about what a target has to
match.

**Docs.** `docs/reference/delegates.md` states that the contextual form and `new Op(...)` are
the same memoized thunk, that a `ref`/`out` parameter travels through it as the caller's own
address, and that the kind is matched once on the TARGET before any wrapper exists, with a
runnable sample of `ref` and `out` over `new`. `docs/reference/diagnostics.md`'s delegate-shaped
bullet carries `" does not match the delegate "` as a literal on one line (the check-docs
refusal gate matches the compiler's own string, and the phrase was split across a line wrap),
naming the kind as part of what has to match. No new `teko: …` refusal was added by either
pass.

**Proof, second pass** (mc 0.15.23, macos/aarch64, base `441be45a`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **73 passed, 51 refused as
expected, 0 failed** (was 73/50); `--dump-ast` byte-identical against the base compiler for
**all 73** pre-existing `tests/*.tk` — `tests/surface_globals.tk` included, the two
`CAST type=uptr` hunks seat (i) put above the forwarded `IDENT name=a0` of `Mut__thunk_bumpf`
and `MutI__thunk_bumpi` being gone, and there is no `CAST` left anywhere in that dump; `sh
scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (81.9s); `sh
scripts/check-docs.sh` → 598 links, 41 fragments, 389 diagnostics, 51 refusals, 142 samples,
all green; `mc build . --config mc.macos.toml --limits` verdict `ok`, every COUNTED table
exactly where the base left it — `passes` 15/30, `syntax` 15/30, `alias` 20/40, `types` 13/26,
`intrin` 8/16 — and only the size-of-surface-code rows moved (`funcs`/`lowered` 3816 → 3819,
`globals` 1272 → 1273, `symbols` 7756 → 7760, `nodes` 178817 → 179140, `ins` 216018 → 216116
on the compiler leg); `./build/teko limits tests/hello.tk` and `./build/teko limits
tests/surface_delegate.tk` both byte-identical to the base compiler's; `mc pkg hash .`
`b3b40c5aa0985d1205838560f11107624da2f78471934ea5afec518185e5dbad` (base
`571bf6db10a035eded3f0b36a36abda17fe2d6d82adedb6db6f4041c5523ab75`: `teko_deleg.tk` and
`teko_ref.tk` are listed files, so the hash moves by design).

**Third pass, verifier finding: the RECLAIM's argument check judged the same forwarding.**
The `ref` pass was not the only pass asking a question about the thunk's body. `tk_rc_call_args`
(`teko_rc.tk`, D226's compat crumb) checks every `N_CALL`'s arguments against the callee's
declared parameter types, and it routes a `ref`/`out` parameter to the rule that owns pointees
(`tk_ref_check_pointee`) only when the ARGUMENT node is an `N_ADDR` — the shape `f(ref x)` has
at the surface. A thunk forwards the caller's address as the bare name of a slot declared
`uptr` (`tk_id(pn)` over `dg_pslot_at`, K2w), which is not an `N_ADDR`, so the argument fell to
the ordinary `tk_check_compat(pt, at)` with `pt` = the POINTEE and `at` = `uptr`. Permissive
for a scalar pointee — `uptr` against `f64` or `i64` has no row to fit and passes — and a hard
row-fit refusal for a class or a struct one:

```teko
class Box { public i64 v; public Box(i64 x) { v = x; } }
delegate void Fill(ref Box b);
void fill(ref Box b) { b.v = 3; }

i64 main() {
    Box b = new Box(1);
    Fill f = fill;                                // ...and `new Fill(fill)` alike
    f(ref b);
    return b.v;
}
```

``teko: a value of type uptr does not convert to Box``, at the line of the delegate's own
declaration, on BOTH roads — measured on the base compiler (`441be45a`) and on the second
pass (`7aae5261`), for `ref Box`, `ref DateOnly` (a four-byte primitive), a `ref` struct and
`out Cell`, eight programs in all. Not a regression against `main`: the base refuses the same
eight, six of them in these very words and the two `new` ones with the ``argument 1 needs
`ref` at the call site`` this decision's first pass removed. It IS a regression inside the
branch — seat (i) (`cd844c2a`) compiled all eight, by accident: the `(uptr)` cast it wrapped
the forwarded name in made the node something `tk_ty_of` answered with the TAG's pointee, so
the compat question came out right for the wrong reason. The budget probe killed that seat,
and the accident with it.

**The fix is the rule already in force, applied to the second pass that breaks it.**
`tk_rc_call_args` returns at once when the function it is walking is a thunk
(`tk_deleg_is_thunk(tk_rc_cur_fn)` — the pass already keeps the enclosing `N_FUNC` in
`tk_rc_cur_fn` for K2's own `ref`/`out` exception, and it is the same identity `tk_ref_pass`
skips under). One line, one file. There is nothing there to judge — `tk_deleg_check_sig`
proves the target identical to the delegate parameter by parameter, type AND kind, before any
thunk exists — and nothing to rewrite: with `pt` equal to `at` at every by-value position, the
widen (`tk_num_widen`) and the box (`tk_nl_wrap`) the walk also performs are no-ops inside a
thunk, which is why no dump of a thunk body moves. The reclaim's own work is untouched: this
is one call's argument check, not the pass, and a `ref` argument carries no count in the first
place (the park, the own and the releases all live in `tk_rc_walk`/`tk_rc_store`, which still
walk the thunk exactly as before — `Mut__thunk_bumpf`'s dump is byte-identical). D63's rule,
restated once more and now pass-agnostic: **no pass judges the compiler's own forwarding.**

**Fixtures, third pass.** `tests/surface_delegate.tk` grows `pcheck`, the seventh helper:
`delegate void Fill(ref Box b)` over a class, `delegate void Move(ref Pt p)` over a struct and
`delegate void Mk(out Cell c)` over a counted class, each on BOTH roads, with the target both
WRITING through the reference (`b.v = 3`) and REBINDING the caller's slot (`b = new Box(7)`,
the prior reference released), `expect-exit: 42` unmoved. `rt_live()` is 1 rather than 0 after
it: the one `Pt` a struct allocates carries no count and is live for the run, `lib/rt.tk`'s own
declared debt — and `cell_dtors` reaches 8, the two `out` rebinds plus the slot itself, which
is what says the thunk road neither leaks nor double-frees. `tests/surface_dateonly.tk` carries
the four-byte pointee (`delegate void SetDay(ref DateOnly d)`, `nextday` through both roads,
`return 84` through `return 87`), where `lib/time.tk` is already in scope. One refusal,
`tests/refuse/deleg_new_ref_pointee.tk`: `new Fill(fillc)` over `void fillc(ref Cell c)` is
refused by `tk_deleg_check_sig` for the POINTEE, `teko: fillc does not match the delegate
Fill(ref Box)`. No new `teko: …` message on this pass either — both fixtures' before-states
are existing refusals.

**Proof, third pass** (mc 0.15.23, macos/aarch64, base `441be45a`): `mc build . --config
mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **73 passed, 52
refused as expected, 0 failed** (was 73/51); the eight programs above compile and run to 0 on
both roads and refuse on `441be45a` and on `7aae5261` alike; `--dump-ast` byte-identical
against the base compiler for **all 73** pre-existing `tests/*.tk` with their original
sources, `tests/surface_globals.tk` and its two thunks included — the skip removes no node,
because the check it skips wrote none; `sh scripts/bootstrap.sh --os macos --arch aarch64` →
`FIXPOINT OK` (133.3s); `sh scripts/check-docs.sh` → 598 links, 41 fragments, 389 diagnostics,
52 refusals, 142 samples, all green; `mc build . --config mc.macos.toml --limits` verdict
`ok`, every COUNTED table where the second pass left it — `passes` 15/30, `syntax` 15/30,
`alias` 20/40, `types` 13/26, `intrin` 8/16 — with `funcs`/`lowered` 3819, `globals` 1273 and
`symbols` 7760 unmoved and only `nodes` 179140 → 179259 and `ins` 216116 → 216125 on the
compiler leg; `./build/teko limits tests/hello.tk` byte-identical to the base compiler's; `mc
pkg hash .` `c9f8692f8bfdba896b329d634fa10357cff0f936e1195111549082d66799501b` (`teko_rc.tk`
is a listed file, so the hash moves by design).

### D64 · The pin rises to 0.16.1 (2026-09-14)
`MC_VERSION` moves from `0.15.23` to `0.16.1` — twenty-five `minicompiler/mc` commits, of
which one changes the SHAPE of what a build produces here and two more close a blocker this
same entry once recorded open. **M52: the standard library left the binary.** Up to 0.15.23
`<sys>`, `<prelude>`, `<io>`, `<float>` and the two float machines were a blob inside `mc`;
since 0.16.0 they are 44 files (41 at 0.16.0) on disk under `lib/mc/v<version>/`, which `mc` resolves next to
its own binary (by realpath, so a symlinked `mc` still finds it) and one directory up. `mc
build` **stages that tree beside a `[compiler]` product**, so `build/lib/mc/v0.16.1/` now
travels with `build/teko`, and the child resolves its own roots — teko passes **no
`--libs-dir` anywhere**, and none was added. The rest of the range is invisible from here:
the x86-64/Win64 register allocator and peephole (M49 D2), the arm64 peephole,
loop-invariant hoisting, the reproducible bench cell (M50), `mc --exe` for Windows PE,
version constraints for `[deps]`/`[tools]`, `[package].mc`, the registry index snapshot,
`$` as a claimable surface token, `i128`/`u128` on x86-64 (SysV and Win64), and M53's own
release canary machinery, which is the mechanism that unblocked this entry below.

**`mc.toml` gains `[package].mc = "0.16.1"`, a bare MINIMUM (not a cap).** R2, the
registry's own validator, moved off its 0.15.20 pin once 0.16.0 shipped and now reads
version constraints, so the manifest can finally say which mc a build of this package
needs — mc `docs/reference/packages.md § The minimum mc version` is mc's own prescription: a
bare/`>=` value is the normal form, a `^`/`~`/`=` CAPS the compiler and is written only
against a future break already known, which this package has none of. `teko.toml` stays
bare — it is a build config, not a manifest, and carries no `[package]` table to begin
with. There is still no `[deps] stdlib` to declare — `docs/specs/packages.md` predicted the
library would become a *package* reached through the lock; what shipped moved it to a
*directory*, leaving the include names, the closure rule and `[deps]` untouched.

**One change beyond the version string, and it is not cosmetic.**
`.github/actions/package-teko` used to stage `teko` and `lib/rt.tk` into the release
archive and nothing else. `lib/rt.tk` includes `<sys>`; under 0.16.x a `teko` binary with no
`lib/mc/` beside it answers `lib/rt.tk:40: unknown bundled include: sys` and cannot compile
a single program — measured, by copying `build/teko` out of `build/` and running it, and
measured again on a full staged archive, which compiles `#include "lib/rt.tk"` to
`exit 42` with the tree and refuses with that line the moment `lib/mc/` is removed. (`mc`
itself, whose driver resolves the same names, says `not in this compiler and mc 0.16.1's
library tree was not found`; the taught compiler is built on `<mc/core_min>` and keeps the
core's shorter wording.) The action now copies the tree `mc build` staged beside the
binary into the archive's own `lib/mc/`, and fails loudly if it is not there; the archive's
`INSTALL.txt` and the release notes gained the `cp -R lib /usr/local/` (and the Windows
`xcopy`) that the install needs. `docs/guide/00-getting-started.md` gained the same line for
`mc` itself, `docs/reference/build.md` states where the tree lives, and both were verified
by installing `mc` into a `bin/` + `lib/` pair and into a bare directory: the first
compiles `#include <sys>`, the second refuses with that exact message.
`.github/actions/setup-mc` needed nothing — it already untars the WHOLE asset into
`.mc-toolchain/mc-<version>-<os>-<asset>/` and addresses the binary inside it, so
`lib/mc/v0.16.1/` lands beside it by construction.
`.github/actions/windows-sysroot`'s `kernel32.def` needed nothing either: mc's own
`scripts/sysroot-windows.sh` at `v0.16.1` still lists the same nineteen names, compared
verbatim.

**The windows/x86_64 blocker this entry recorded open is CLOSED, by `mc` 0.16.1, not by
anything here.** mc 0.16.0 added a direct PE executable backend for windows/x86_64 (M42
step 2), and `drv_teach` took that host's exe slot unconditionally, ignoring a declared
`[linker]` and writing `build/teko.exe` with mc's own PE writer — an image that the loader
refused the moment the program imported anything from `kernel32`, so `build/teko.exe`
exited 127 on every invocation and `mc build` reported it as a silent `return 1`, with no
diagnostic. That was reported to `minicompiler/mc` with the two-line pure-`mc` reproducer
this entry's earlier draft carried, and never worked around here (D2). `minicompiler/mc`
PR #84 (commit `718bd82`) makes a declared `[linker]` win over the host's direct exe
backend for the taught compiler too — the same precedence `drv_entry` already gave the
entry — so `mc build` on windows/x86_64 links `teko.exe` with the leg's own `lld-link`
exactly as it did at 0.15.23, and the direct PE writer never enters the road at all; a
companion finding, PR #83 (commit `166c9f1`), separately showed the loader's refusal on the
DIRECT road was never a layout defect but an unresolvable `kernel32` import (`write`, which
`kernel32.dll` does not export), orthogonal to teko once fix 1 takes the linked road.
Teko's own release canary (`.github/workflows/mc-canary.yml`, M53's mechanism) judged
0.16.1 `ok` on every leg — `https://raw.githubusercontent.com/teko-org/teko-lang/canary/0.16.1.json`,
run `https://github.com/teko-org/teko-lang/actions/runs/34877983682` — windows/x86_64 and
`teko_std` included, which is the measurement this entry stands on rather than a second
reproducer written here: the fix is on mc's side and mc's own release process is what
proves it, per D2.

Proof, macos/aarch64, with the pinned `~/.local/mc/mc-0.16.1-macos-arm64/mc`: `mc build .
--config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` →
**75 passed, 83 refused as expected, 0 failed**; `sh scripts/bootstrap.sh --os macos --arch
aarch64` → `FIXPOINT OK` (`teko2.o == teko3.o`, fixed point on the FIRST turn —
`teko1.o == teko2.o` — and the `--dump-asm` diff empty over 230438 lines);
`sh scripts/check-docs.sh` → `docs ok: 615 links, 45 fragments, 390 diagnostics, 83
refusals, 144 samples` (re-measured on `abf3137b`, after `origin/main` `afdfae88` — D65 to
D70 — was merged in). `mc pkg hash .` is
`373c8f04a413bae0f5c86fab5a19f2dedfffc76c5fc0028691f162a2660bcdc2` (base, `origin/main`
`afdfae88` under 0.15.23,
`a8122b64a3235e39820a1cc1aa3fc1446e3b890fc684791fdbc12868b7343e50`: the tree hash now moves
with this pin, because `[package].mc` is new bytes in `mc.toml`, which the previous 0.16.0
draft of this entry did not carry).

`mc limits . --config mc.macos.toml` keeps verdict `ok` on every row of both tables, and no
table appeared, disappeared or changed its ceiling. The rows that MOVED, `build/teko.mc`
first (estimate / used, 0.15.23 → 0.16.1): `includes` 78/78 → 79/79, `nodes`
181369/157320 → 188941/164854, `defines` 1249/1201 → 1334/1284, `funcs` 3833/3193 →
3980/3277, `lowered` 3833/3175 → 3980/3258, `globals` 1277/945 → 1326/946, `strings`
2667/2138 → 2727/2174, `ins` 217118 → 226179, `symbols` 7777/6276 → 8033/6397, `undef`
18 → 0, `backends` 8 → 9. On `tests/hello.tk` exactly one row moves: `backends` 8 → 9
(the `heap` column is not a proof figure — it reflects the state of `build/` at the time
of the run, D55's lesson). Every row's motion is the same handful of causes read twice —
D69's own capacity raise (already on `main`, unrelated to this pin), the library now
SOURCE the build reads (one more include) instead of a blob, and 0.16.x registers one more
backend (`mc --exe` for Windows PE) — and none of them is near its reserve.

`--dump-ast` of all **158** fixtures (75 under `tests/`, 83 under `tests/refuse/`, the
refusals compared on their stderr since they produce no tree), taken with the compiler
built by 0.15.23 (`origin/main` `afdfae88`) and with the compiler built by 0.16.1 over the
same unmodified tree, is **byte-identical on every one**. The taught compiler's own output
does not depend on the host `mc`; what moved underneath it is codegen, one more capacity
ceiling (D69) and library location, not grammar.

### D65 · A delegate FIELD is callable on every road it can be read (2026-09-14)
D59 closed the argument gap on the four `callp` roads that HAD a call. This entry is about
the roads that had none. `class H { public Op cb; }` with `delegate i64 Op(i64 a, i64 b)`
answered `h.cb(2, 3)` only where the PARSER itself could type the receiver. Measured on
`d028a7a0`:

| the site | before |
|---|---|
| `h.cb(21)` on a LOCAL, `this.cb(x)`, `hs[0].cb(21)`, `mk().cb(21)`, `Op f = h.cb; f(21)` | ran |
| `i64 f(H h) { return h.cb(21); }` — a PARAMETER receiver | `teko: the member is a field, not a method: cb` |
| `H gh; ... gh.cb(21)` — a GLOBAL receiver | the same |
| `i64 f(A a) { return a.b.cb(21); }` — a field of a field under a parameter | the same |
| `cb(x)` UNQUALIFIED inside a method of `H` | `call to unknown function`, from the core |
| `H.cb(21)` on a `static Op cb` | `teko: unknown member: cb` |
| `LateH.scb(1)` on a `static Op scb` of a type declared BELOW the call | the same |

The fourth row broke the refusal law on its own: a diagnostic with no `teko:` prefix, from
mc's own resolver, for a construct teko taught.

**ROOT: one rule, written at one door out of five.** `tk_field_use` (teko_expr.tk) reads
`tk_deleg_row(fty)` and, on a `(`, hands the field's load to `tk_field_deleg_call` ->
`tk_deleg_build`, the single builder of a delegate `callp`. The four other doors a field
is reached through never asked the question:

  - `tk_pend_field` (teko_typeof.tk) is `tk_field_use`'s PASS-TIME twin — the emitter for
    every receiver the parser cannot type, which is a parameter, a global, a `TK_PFWD`
    type and a field of any of them (teko_expr.tk's `tk_dot` defers all four). Its first
    statement was `if (form == TK_PCALL) err_at2(..., "teko: the member is a field, not a
    method", m);`. It now asks `tk_deleg_row` first, exactly as its parse-time twin does,
    and reports the return through `pty`/`ppure`/`pown` rather than through `tk_xt_put` —
    the node that ends up in the TREE is `tk_pend_do`'s placeholder, not what was built
    for it, which is the split `tk_deleg_build`'s own contract leaves to its caller.
  - `tk_this_call` (teko_this.tk) rewrites `area(2)` into `this.area(2)` when the class
    declares a METHOD of that name, and leaves the node alone otherwise — so a free
    function of that name still answers, which is the rule that stays. A FIELD of delegate
    type was never one of its cases, so the bare name reached no rewrite at all.
    `tk_this_deleg_call` is the new one: the field of `this` under that name
    (`tk_this_field`, which lets a local or a parameter answer first, C#'s own rule), its
    row, and `tk_deleg_build` over `tk_ld(fty, tk_this_field_addr(fi))` — the very load
    `this.cb` builds, and the static field's own global when the field is `static`.
  - `tk_static_member` (teko_access.tk) decided on `(` BEFORE it ever looked at the field
    table: `if (p_id() == K_LPAR) return tk_static_call(si, m, line, fl);`, whose
    `tk_method_pick` answered -1 and gave `unknown member`. It now asks
    `tk_static_deleg_field` first — a field of that name, `static`, not an array, of a type
    that names a delegate row — and takes `tk_field_deleg_call` with the field's own global
    as the address. The lookup is deliberately NOT `tk_static_field_of`: that one refuses an
    INSTANCE member in words of its own, and `H.n(1)` on an instance field is a mistake
    whose report belongs where it already was (measured unmoved).
  - `tk_ns_rewrite_call` (teko_ns.tk), the fifth door, found by the second verifier: inside
    a NAMESPACE a bare call is rewritten to the namespace's own free function by pass 2,
    BEFORE `tk_this_call`/`tk_this_deleg_call` ever read the node, and the member guard it
    stopped on was `tk_method_named_find(cls, name)` alone — so `namespace geo { i64 cb(i64);
    class H { public Op1 cb; i64 go(i64 x) { return cb(x); } } }` compiled to a direct call
    to `geo__cb` (`--dump-ast`: `FUNC geo__h_go` -> `CALL geo__cb`) and answered 105 where
    the same program outside a namespace answers 6. It now also stops on a FIELD whose type
    names a delegate row (`tk_field_find` + `tk_deleg_row`), which is exactly what
    `tk_this_deleg_call` then builds — and only on a delegate one, and only for an `N_CALL`:
    a bare `n(1)` on an `i64` field keeps the fork below, and `&cb` keeps naming the free
    function it names outside a namespace. Its twin `tk_ns_rewrite_ident` already asked
    `tk_field_find`.
    The same door carried a second defect of its own: `tk_ns_call_cls` was filled with
    `tk_method_of_fn(...)`, a METHOD row number, and read by both rewrites as a STRUCT row
    number — so the member guard answered for whatever type happened to sit at that index
    and held only where the two numbers coincided (one class, its methods declared first).
    With three classes ahead of it, even the METHOD half failed: a namespaced free `tag`
    won over the class's own `tag`, 105 on `d028a7a0`. `tk_ns_cls_of_fn` reads `mt_cls_at`
    off the row, the same pair `teko_this.tk` reads for its own `tk_pass_class`.

**What does NOT move, and it is the larger half.** A method named `cb` still answers ahead
of the field on both the bare-name road and the `Type.` road, as it does in C#. A
non-delegate field keeps every wording it had: `h.n(1)` through a parameter receiver is
still `teko: the member is a field, not a method: n`, `H.n(1)` on a `static i64` is still
`teko: unknown member: n`, and an instance field reached through its type is still the
instance-member refusal. An ARRAY field is excluded at both new doors (`fd_nel_at > 0`),
so the two `p.items[i]` forms are untouched. And `n(1)` on an `i64` field by its bare name
is STILL `call to unknown function` — a surface fork left alone, recorded in
`docs/reference/not-yet.md`: a bare name becomes a call only where it can become one, and
widening that into a teko refusal would have to decide what `n(1)` means for every
non-callable member, which is a design and not this crumb.

**D59's judge rides along, unasked.** All three new roads go through `tk_deleg_build`, so
`tk_deleg_check_arg_kinds` fires on them from the first day: the count, the `ref`/`out`
kind, the pointee, the by-value type and C# §10.2.3's widening. The deferred road parks
what it cannot type at PASS 6 (`tk_typeof_pass`) and the park is judged at the end of
`tk_over_pass`, pass 14 — later, so nothing is read before it is written. `tests/refuse/
deleg_pend_arg_narrow.tk` is the row for it. **No pass of its own and no table of its own**:
`passes` stays 15/30 and `globals` 943.

**Fixtures** (3 refusals, 1 positive grown). `tests/refuse/deleg_pend_not_deleg.tk` and
`deleg_static_not_deleg.tk` hold the two wordings that must NOT move;
`deleg_pend_arg_narrow.tk` is an `f64` local through a deferred `Op(i64)` field call.
`tests/surface_delegate.tk` grows `pfcheck` — a parameter receiver, a field of a field under
one, and the bare name inside a method, with `rt_live()` back to its floor, the delegate
being borrowed by the call and not kept — and `sfcheck`, the two slots that OUTLIVE the
helper: a `static Op` reached as `PH.scb(20, 1)` and by its bare name from a static method,
and a GLOBAL receiver. `main` asserts `rt_live() == 3` after that one rather than the floor,
which is what a static field and a global holding an object holding a delegate come to.
`expect-exit: 42` unmoved; the grown fixture is refused by the base compiler at `PH.scb`.

**Proof** (mc 0.15.23, macos/aarch64, base `d028a7a0`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 64 refused as
expected, 0 failed** (73/61 on the base); `--dump-ast` against the base compiler for **all
73** pre-existing `tests/*.tk` with their ORIGINAL sources — **byte-identical on all 73**,
every new branch being reached only where the program did not compile before;
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`;
`sh scripts/check-docs.sh` -> `docs ok: 600 links, 43 fragments, 389 diagnostics, 64
refusals, 141 samples` (no new literal: every message these three doors give was already
documented); `mc limits . --config mc.macos.toml` verdict `ok` on both legs, the
`tests/hello.tk` leg unmoved on every counted row (`passes` 15/30, `types` 13, `intrin`
8/16, `alias` 20, `syntax` 15, `rules` 4, `on_stmt` 4, `globals` 0) and the compiler leg
moving only by the added surface code — `nodes` 156565 -> 156854, `ins` 215968 -> 216382,
`funcs` 3182 -> 3185, `lowered` 3164 -> 3167, `symbols` 6263 -> 6266, with `globals` **943**
and `strings` **2138** both unmoved; `mc pkg hash .` `e6e74b374d956ea95810ba0654ed2789426dbde6d5ba6dc8236f889e8aa3dcfe` (base
`15c2fdc3d62c5110cd8b5589f28beb3a3c837cc49ecc7a8c0c0b46f8688d1e6e`: `teko_typeof.tk`,
`teko_this.tk` and `teko_access.tk` are listed files, so the hash moves by design).

**The precedence is written out, because this crumb moved it.** A verifier measured a
shape the entry above did not name: a FREE function and a delegate FIELD sharing a name,
the bare name called inside a method.

```teko
delegate i64 Op1(i64 a);
i64 add1(i64 a) { return a + 1; }
i64 cb(i64 a) { return a + 100; }         // a FREE function named `cb`
class H {
    public Op1 cb;                        // ...and a delegate FIELD named `cb`
    public i64 go(i64 x) { return cb(x); }
}
i64 main() { H h = new H; h.cb = new Op1(add1); return h.go(5); }
```

`d028a7a0` exits **105** — the free function — and this crumb exits **6**, the field, with
no diagnostic either way. The twin with a METHOD instead of a field (a free `i64 m(i64)`
returning `a + 100`, a method `m` returning `a + 1`, `m(x)` inside a sibling method) exits
**6 on the base as well**: `tk_this_call` (teko_this.tk:414) asks
`tk_method_named_find(tk_pass_class, m)` FIRST and rewrites the site into `this.m(...)`,
so a member has shadowed a free function of its name since long before D65.

**The rule, and it is C#'s.** A bare name inside a method resolves to the class's own
member — method, then field — before any free function of that name. C# has no free
functions; its nearest forms, a static method of the enclosing (or a static) class and a
top-level local function, are both shadowed by a same-named member of the class whose body
reads the name, silently. teko already did that for the method half, so the head's
behaviour is the consistent one and the base's was the outlier: the field half simply had
no door (the name reached no rewrite and fell through to the core's resolver, which knows
only the free function). A program that read a free function through a same-named delegate
field changed meaning with this crumb, **deliberately**. From outside the class nothing
moves — `cb(5)` in `main` is still 105 — and a local or a parameter of that name still
answers before the member, which is `tk_this_field`'s own rule.

**The honest alternative was measured and rejected.** A REFUSAL at the collision —
`teko: cb is both a field of H and a function; write this.cb(...) or H's own name` — reads
well, but it refuses what C# accepts, and it would have to refuse the METHOD twin too, or
teko would refuse the field collision while silently shadowing the method one. Widening it
to both halves breaks working programs for a shape C# has never complained about. Member
wins, no refusal.

`tests/surface_delegate.tk` grows `shadowcheck` for it: a free `cb` and a free `tag`, a
class `Shad` with a delegate field `cb` and a method `tag`, `cb(5)` and `tag(5)` inside its
methods both answering **6** (the members), `sh.cb(5)` = 6 through the receiver, and
`main` reading both free functions at **105**. `docs/reference/delegates.md` (the
precedence sentence) and `docs/reference/classes.md` ("The receiver is not written") carry
the rule.

**Proof, second pass** (mc 0.15.23, macos/aarch64, base `d028a7a0`, only `tests/` and
`docs/` moved — no module of the compiler changed in this pass):
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 64 refused as
expected, 0 failed**, `tests/surface_delegate.tk` still `expect-exit: 42` with
`shadowcheck` inside it and still refused by the base compiler (at `PH.scb`, line 429);
`--dump-ast` against the base compiler over all **73** pre-existing `tests/*.tk` with their
ORIGINAL sources — **72 byte-identical**, `tests/surface_delegate.tk` a pure INSERTION (the
base's dump plus `shadowcheck`'s own nodes, nothing else moved), unchanged from the first
pass;
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`;
`sh scripts/check-docs.sh` -> `docs ok: 601 links, 44 fragments, 389 diagnostics, 64
refusals, 141 samples` (the link and the fragment are `classes.md` -> `delegates.md#calling`;
no new diagnostic, because the ruling is that nothing is refused); `mc limits . --config
mc.macos.toml` verdict `ok` on both legs with every counted row unmoved from the first pass
— compiler leg `nodes` 156854, `ins` 216382, `funcs` 3185, `lowered` 3167, `symbols` 6266,
`globals` 943, `strings` 2138, and the `tests/hello.tk` leg `passes` 15/30, `types` 13,
`intrin` 8/16, `alias` 20, `syntax` 15, `rules` 4, `on_stmt` 4, `globals` 0; `mc pkg hash .`
`e6e74b374d956ea95810ba0654ed2789426dbde6d5ba6dc8236f889e8aa3dcfe`, unmoved — a fixture,
the log and two reference pages are not listed files.

**The fifth door, measured** (mc 0.15.23, macos/aarch64, base `d028a7a0`). Every shape is
written inside `namespace geo` except the fourth, where the namespace holds only the free
function and the class is top level under a `using geo;`; `delegate i64 Op1(i64 a);` and
`i64 add1(i64 a)` are top level throughout, and the exit code is what `main` returns:

| the shape | base | head |
|---|---|---|
| a delegate FIELD `cb` vs the namespace's free `cb`, bare `cb(x)` in a method | 105 | **6** |
| a METHOD `tag` vs the namespace's free `tag`, bare `tag(x)` in a sibling method | 6 | 6 |
| the namespace's free `lone` with NO member of that name, `lone(x)` in a method | 105 | 105 |
| a `using`-imported free `cb` vs a delegate field of a top-level class | 105 | **6** |
| `cb(5)` in a namespace FUNCTION, outside the class | 105 | 105 |
| the METHOD twin with three classes declared ahead of it (the index defect) | 105 | **6** |

Row 2 held on the base only by the coincidence row 6 breaks: `tk_ns_call_cls` carried a
method row number where a struct row number was read.

**Proof, third pass** (mc 0.15.23, macos/aarch64, base `d028a7a0`):
`mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` -> **73 passed, 64 refused as expected, 0 failed**, `tests/surface_delegate.tk`
still `expect-exit: 42` with `shadowcheck` grown by the namespaced pair (`shd.NShad`) and
exiting **214** on the pre-fix compiler — row 210 + 4, the field shape;
`--dump-ast` against the base compiler over all **73** pre-existing `tests/*.tk` with their
ORIGINAL sources — **byte-identical on all 73**, and byte-identical on all 73 of the branch's
own sources as they stood before this pass grew the fixture, compiler `cc43ad53` against the
fixed one: this door fires only for a class that declares a delegate field named like a free
function reachable from its namespace, and no fixture had one before this pass;
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`;
`sh scripts/check-docs.sh` -> `docs ok: 601 links, 44 fragments, 389 diagnostics, 64
refusals, 141 samples` (no new literal: the ruling is still that nothing is refused);
`mc limits . --config mc.macos.toml` verdict `ok` on both legs, the `tests/hello.tk` leg
unmoved (`passes` 15/30, `types` 13, `intrin` 8/16, `alias` 20, `syntax` 15, `rules` 4,
`on_stmt` 4, `globals` 0) and the compiler leg moving only by the added surface code —
`nodes` 156854 -> 156917, `ins` 216382 -> 216486, `funcs` 3185 -> 3187, `lowered` 3167 ->
3169, `symbols` 6266 -> 6268, with `globals` **943** and `strings` **2138** both unmoved;
`mc pkg hash .`
`a0ef8936de1773c578ee8c9800524caef98a9837499b16f6e50b74f40af0e221` (`teko_ns.tk` is a
listed file, so the hash moves by design).

**A silent-wrong repaired as a side effect: the CONST road.** The third-pass verifier owes
this entry a paragraph it did not have. `tk_ns_cls_of_fn`'s index fix is read by BOTH
rewrites of teko_ns.tk, and the second one is `tk_ns_rewrite_ident`, whose member guard is
`tk_field_find || tk_mconst_find || tk_method_named_find` on that same class index. So a
namespaced `const X` standing beside a MEMBER `const X`, the bare `X` read inside a method
of that class, answered the NAMESPACE const wherever the two row numbers did not coincide.
Measured on `409bc8c0` with three classes declaring methods ahead of the one under test,
`namespace nsc { const i64 X = 7; ... class NC { public const i64 X = 1; i64 go() { return
X; } } }`: the base exits **7**, the head exits **1**. No diagnostic either way — a
silent-wrong, and the same C# rule D65 wrote out for the field: the class's own member
answers before anything the namespace declares. It costs no fixture of its own: the row is
`nsh.cgo()` in `tests/surface_delegate.tk`'s `shadowcheck`, a `const i64 CX = 105` in
`namespace shd` against `NShad`'s own `public const i64 CX = 6`.

**Copilot on #716, pass 4 — the forward road and the array guard.** `LateH.scb(1)` with
`LateH` declared below the call goes through `tk_fwd_resolve_static_one` (teko_access.tk),
whose `TK_STCALL` branch picked methods only and refused `teko: unknown member: scb`. The
branch now asks `tk_static_deleg_field` first and builds the delegate `callp` with
`tk_deleg_build` over the arguments the site recorded (`st_arg_at`/`st_na_at`) — no re-parse,
the same builder the known-type road takes; measured with a LOCAL argument (`go(k)` calling
`LateH.scb(k, 2)`), the judged `callp` runs. And `tk_ns_deleg_field` (teko_ns.tk) now excludes
an ARRAY field — `fd_ty_at` carries the element type, so `public Op cb[2]` looked like a
callable delegate field and stopped the namespace rewrite of a bare `cb(5)` that names the
namespace's free function; every other delegate-call door already excluded arrays.
`tests/surface_delegate_fwd.tk` carries both.

### D66 · A contextual lambda is read on every slot of delegate type teko's own parser reaches (2026-09-14)
`(i64 x) => x * 2` with no `new Op(...)` around it needs a READER that looks ahead before
mc's core does: `parse_primary` sees `(`, asks `type_of_token` for a cast and otherwise
parses a group, and neither branch has a fallback -- `expected ) in cast` is what a lambda
gets there. teko's own lookahead for it (`tk_paren_lambda_follows`, non-consuming, counting
bytes from `p_cp()` to the matching `)` and testing for `=>`/`use`) was reached from exactly
TWO places: a delegate variable declaration (`tk_deleg_var_stmt`) and a METHOD-call argument
(`tk_args_typed`). Every other right-hand side of delegate type died in the core, so the
surface said "a lambda goes wherever a delegate value goes" and the compiler said otherwise
at six sites.

**Measured on `d028a7a0`** (mc 0.15.23, macos/aarch64), seven roads, all seven
`expected ) in cast`:

| road | written | before | after |
|---|---|---|---|
| instance field | `h.cb = (i64 x) => x * 2;` | `expected ) in cast` | **accepted** |
| constructor | `this.cb = (i64 x) => x * 2;` | `expected ) in cast` | **accepted** |
| static field | `H.cb = (i64 x) => x * 2;` | `expected ) in cast` | **accepted** |
| `T[]` element | `fs[0] = (i64 x) => x * 2;` | `expected ) in cast` | **accepted** |
| `return` | `return (i64 x) => x * 2;` | `expected ) in cast` | unchanged, and why below |
| free-function argument | `apply((i64 x) => x * 2, 21)` | `expected ) in cast` | unchanged |
| bare-name assignment | `g = (i64 x) => x * 2;` (a global, or a field written without `this.`) | `expected ) in cast` | unchanged |

**ONE helper, three call sites, four roads.** `tk_deleg_rhs(ty, line, fl)` (teko_deleg.tk)
answers "the value of a slot of type `ty`": `tk_deleg_row_under(ty)` -- the delegate row with
the `?` peeled off -- hands it to `tk_deleg_init_expr`, the reader the declaration road
already had, and every other type is `parse_expr(0)`, byte for byte what each site read
before. `tk_field_use` (teko_expr.tk), `tk_static_use` (teko_access.tk, a forward
declaration away, the module being included four files earlier) and `tk_ha_index`
(teko_heaparr.tk) call it in place of their own `parse_expr(0)`. The CONSTRUCTOR road costs
nothing: `this` is an ordinary receiver by the time `.` is read, so `this.cb = ...` reaches
`tk_field_use` like any other `h.cb = ...`. The `?` is peeled for the same reason the
element road's own escape check already peeled it (D51, twelfth pass): an `Op?` slot holds
a lambda exactly as an `Op` slot does, and the declaration road has read `Op? f = (i64 x)
=> ...;` since Q1a.

**Three roads stay closed, and the reason is not teko's to fix.** They are the three parse
positions mc's core owns outright, with no registration reaching them (`src/parse.mc`, read
only, D2): `syntax_stmt` refuses a core keyword, so no handler may stand at `return`;
`parse_call` reads a free call's arguments itself, so the six word registrations never see
one (a METHOD call is teko's own `tk_call_method_args`, which is why that road works);
and a bare-name assignment falls to `parse_expr` at the bottom of `parse_stmt_core`, below
`syntax_stmt_find`. The one door that WOULD reach all three is `syntax_expr("(")` -- the
head of `parse_primary` tests `syntax_expr_find` before the `K_LPAR` branch -- which would
claim every parenthesised expression and every cast in every program and make teko
re-implement both. That is a fork, not a patch (D21). `new Op((i64 x) => ...)` is what those
three positions take, and `docs/reference/not-yet.md` carries them with this reason.

**The escape rule did not move.** Each of the three sites already called `tk_lam_escapes`
over the value it parsed, so `h.cb = (i64 x) use (&acc) => ...;` is refused `teko: a lambda
that captures by reference cannot leave its scope` at the store's own line on every road
just opened -- the same sentence the seven existing escape sites carry, no site missing it,
none added.

**FIXTURES** (1 grown, 5 refusals). `tests/surface_lambda.tk` grows item 22,
`slot_roads_check`: the field road in all three grafias (explicit parameters, short, and a
capture by value), the static field, a constructor's `this.cb`, and two `Op[]` elements --
plus three rows that write a CAST (`(i64) 3.5`) and a GROUP (`(k + 1) * 2`, `(fs[1])`)
through the very same three readers, so the lookahead is measured not to have claimed a `(`
that opens neither. It is the LAST helper `main` calls and the only one whose `rt_live()`
floor is 1: `St.cb` is a static, and the closure it holds outlives every scope. Its own
`method_check` drops the `new Op(...)` the field road used to force. The five refusals --
`tests/refuse/deleg_{field,static,this,elem,nullfield}_lam_escape.tk` -- carry the escape
sentence at the store line of each opened road; the `Op?` one is what pins
`tk_deleg_row_under`, since with the `?` unpeeled that store would fall to `parse_expr(0)`
and die in the core instead.

**Docs.** `docs/reference/delegates.md` replaces the sentence that promised a lambda
"wherever a value of its delegate type is expected" with the table of roads that take one
and the three that still need `new Op(...)`. `docs/reference/not-yet.md` widens its
`return (i64 x) => e;` row into the three closed positions with the parser reason.
`docs/reference/diagnostics.md` is unmoved: no refusal is new here.

**Proof** (mc 0.15.23, macos/aarch64, base `d028a7a0`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 66 refused as
expected, 0 failed** (73/61 on the base: five refusals added, one positive grown);
`--dump-ast` **byte-identical on all 73** pre-existing `tests/*.tk` with their ORIGINAL
sources, base compiler against this one, before the fixture was grown -- a lookahead that
fires only where the core parser refused today cannot move an accepted program, and the
cast and group rows measure the other half; `sh scripts/bootstrap.sh --os macos --arch
aarch64` -> `FIXPOINT OK`; `sh scripts/check-docs.sh` -> `docs ok: 598 links, 41 fragments,
389 diagnostics, 66 refusals, 141 samples`; `mc limits . --config mc.macos.toml` verdict
`ok` on both legs, every counted table exactly where the base left it (`passes` 15/30,
`syntax` 15/30, `types` 13, `intrin` 8/16, `alias` 20/40, `on_stmt` 4, `syntax_type` 2,
`globals` 943) and only the size-of-surface-code rows moving with the one function added
(`nodes` 156565 -> 156598, `ins` 215968 -> 216017, `funcs` 3182 -> 3183, `lowered` 3164 ->
3165, `symbols` 6263 -> 6264); `mc pkg hash .`
`aaca0aa1374363ee36b04626da0a0fac8c0f1eefd947d29c8097fc650dbc00c1`, with
`origin/main` merged in at `111559fb` (base
`0f04c593b5aad2ad3bbf45d97ba6427d56a05fe3620bf3f1ca73d4fb7256d6eb`: `teko_deleg.tk`,
`teko_expr.tk`, `teko_access.tk` and `teko_heaparr.tk` are listed files, so the hash moves
by design).

**Copilot on #719, four roads measured again on `95e157cb`.** Each of the four was
reproduced first, and each answer is the same question asked once: does a READER of teko's
own hold the slot's TYPE at the moment the value is parsed?

| road | measured on `95e157cb` | became |
|---|---|---|
| `H.cb = (i64 x) => e;` with `class H` declared BELOW | `expected ) in cast` | a documented limit |
| `h.take_q((i64 x) => e, 42)` on a `void take_q(Op? f, i64 k)` | `expected ) in cast` | **opened** |
| `Op[] g; g[0] = (i64 x) => e;` (a GLOBAL array) | `expected ) in cast` | a documented limit |
| `h.pick((i64 x) => e, 21)` on an OVERLOADED `pick` | `expected ) in cast` | **opened** |

**Two opened, one reader.** Both are the same site: `tk_call_method_args` (teko_expr.tk)
asked the callee's SINGLE declaration for the parameter's type, through `tk_deleg_row` and
a `tk_method_name_count(si, m) != 1` guard, so an `Op?` parameter answered -1 and an
overloaded name never asked at all. It asks per POSITION now --
`tk_method_param_deleg(si, m, n)`: the level of the chain that declares the name (the one
`tk_method_pick` picks from, a class's own declarations hiding the base's overloads), every
signature there long enough to reach position `n`, the row taken with the `?` peeled off
(`tk_deleg_row_under`), and -1 the moment two candidates disagree or one is not a delegate
there. `tk_args_typed` folded into that loop, which is why `funcs` does not move. Three
roads came with it, measured: the `Op?` parameter (the coercion still sees the nullable
type and boxes for it -- READING one back inside the callee is untaught, `call to unknown
function`, and the fixture only proves the parse), an overloaded name at either arity, and
a method INHERITED from a base, whose name the derived class declares zero times and which
the old `!= 1` guard sent to the untyped `tk_args`. An overload whose candidates DISAGREE
at that position (`mix(Op)` beside `mix(i64)`) still needs `new Op(...)`: which signature
applies is decided by the argument COUNT, which the parser does not have yet.

**Two documented, and the reason is the same one.** The value is parsed BEFORE the slot's
type exists, so there is no row to read a lambda against and no later pass can re-read
what the parser already refused. A static field of a type declared below the write goes
through `tk_fwd_defer_static` (teko_access.tk): the forward pre-scan reserves the type's
WORD -- `type_new` plus the syntax registrations -- and never a row of the type table,
because materializing rows at scan time would reorder every interface's vtable slot
(teko_fwd.tk's own header), so the field has no declared type at that point;
`tk_fwd_resolve_static_one` learns it one pass too late. A global `T[]` element goes
through `tk_arr_defer_write` (teko_array.tk), deferred because a global array may be
declared BELOW its own write -- measured, and accepted today -- so the element type is only
collected by `tk_hg_collect` in pass 5. Reading the lambda against a row materialized on
the spot would mean replaying a whole declaration from inside an expression
(`tk_fwd_materialize` is a source push written for the `:` list) and moving row indices the
`--dump-ast` gate pins; opening only the declared-above half would make the guarantee
depend on declaration order, which is the same limit in a fuzzier shape. So the claim is
narrowed instead: `docs/reference/delegates.md` qualifies the static-field row to a type
declared ABOVE and the element row to a LOCAL or field `T[]`, and
`docs/reference/not-yet.md` carries both -- plus the disagreeing overload -- with the
reason. Its stale D62 row ("a LAMBDA written straight into a FIELD ... `expected ) in
cast`") is deleted: D66 opened that road and the row was measured false; the row for
calling an `Op?` says "local or PARAMETER" now, the new road's own untaught half.

**Proof of this pass** (mc 0.15.23, macos/aarch64, head `95e157cb`): `mc build . --config
mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed,
67 refused as expected, 0 failed** (`tests/surface_lambda.tk` grows item 23,
`arg_roads_check`: the `Op?` parameter, an overloaded name at both arities, a method of the
base, and a GROUP at a delegate argument with a CAST at the position beside it, so the
per-position reader is measured not to have claimed a `(` that opens neither; no refusal
added, no diagnostic new); `--dump-ast` **byte-identical on all 73** pre-existing
`tests/*.tk` with their ORIGINAL sources (4715223 bytes of dump), `59652293`'s compiler
against this one; `sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`;
`sh scripts/check-docs.sh` -> `docs ok: 612 links, 41 fragments, 389 diagnostics, 67
refusals, 143 samples`; `mc limits . --config mc.macos.toml` verdict `ok` on both legs,
every counted table exactly where `95e157cb` left it (`passes` 15/30, `syntax` 15/30,
`types` 13, `intrin` 8/16, `alias` 20/40, `on_stmt` 4, `syntax_type` 2, `globals` 944,
`funcs` 3186, `lowered` 3168, `symbols` 6268) and only the size-of-surface-code rows moving
(`nodes` 156737 -> 156807, `ins` 216225 -> 216334); `mc pkg hash .`
`cc220d3c617a15396d75a872947ca825f19bc6c0ec0f71c70cccf5a86bb2969e`, against
`014f902503d1510e450ba4422be5b5a5d76bd58c73b678e98ed1b5e843c6106d` on `95e157cb`:
`teko_expr.tk` is a listed file, so the hash moves by design.

### D67 · The EXPLICIT thunk `new Op(f)` resolves `f` on the same late/contextual road the bare-name wrap already takes (2026-09-14)
`new Op(f)` picked its target at PARSE time (`tk_new_deleg` -> `tk_deleg_wrap` -> `tk_deleg_thunk`
-> `decl_find(f)`), which answers about the FIRST declaration of a name alone and only about one
already read so far -- three measured wrong answers where the contextual road (`Op g = f;`,
resolved in `tk_deleg_pass`, the walk) already gets it right:

| the target | before (base `afdfae88`) | after |
|---|---|---|
| **overloaded**: `f64 pick(f64 a)` declared first, `i64 pick(i64 a)` second, `delegate i64 Op(i64 a)` | `teko: pick does not match the delegate Op(i64)` -- the FIRST declaration's own mismatch, even though the second matches exactly | resolves to `pick(i64)`, `f(5)` answers 6 |
| **declared BELOW** the `new` | `teko: unknown function: later` | resolves, `f(2, 3)` answers the target's own return |
| **namespaced**, reached from INSIDE its own namespace, no flat twin | `call to unknown function`, from the CORE at lowering -- no `teko:` prefix at all, D62's own "the road still out is the seventh, `new Op(col)`" | resolves to the namespaced target, `f(2, 3)` answers its own return |
| **namespaced**, reached through a `using` alone, no flat twin | *runs, by accident*: the thunk is emitted un-namespaced, so `tk_ns_pass`'s own call-rewrite (which ALSO revisits every generated body) falls through the SAME file's `using`s with `ns` empty -- which is the identical accident that fails the row above the moment the same target is reached from inside its own namespace, since there the thunk's `ns` is EMPTY where the SITE's is not | resolves deliberately, from the recorded SITE namespace, not from an accident of where the generated code happens to land |

**ROOT: the thunk was built while the file was still being read, and namespaces are not
mangled -- nor is the whole unit's own declaration set complete -- until later.** The
contextual road already answers all three questions correctly, in one shared resolver
(`tk_deleg_resolve_fn`, D62) run from `tk_deleg_pass`, the walk that stands once the entire
unit is parsed. `new Op(f)` never called that resolver at all; it called `decl_find` straight
from the parser, which is the wrong tool for every one of the three defects above by
construction, not by an oversight in one branch.

**The fix: `new Op(f)` builds a PLACEHOLDER, not a thunk.** `tk_new_deleg`'s bare-name branch
now calls `tk_deleg_new_placeholder`: an ordinary `N_CALL` naming `f` itself, with NO argument
(the same 0-arg shape the resolved allocator call wears, so `tk_ref_check_call`'s own argument
loop -- which would otherwise ask a question about a callee this node does not yet truly name --
is a no-op over an empty list regardless of what `f` turns out to mean), tagged with the
delegate row right away (`tk_xt_put`, the SAME row and type the resolved allocator call would
carry). Every consumer that asks the node's TYPE between here and the walk that patches it --
`tk_ty_of`, `tk_pty_of`, a nested delegate call's own argument judge, `tk_deleg_coerce`'s own
"already a value of this type" fork -- answers correctly from the tag alone, so nothing between
parse time and the walk (`tk_ns_pass`'s own bare-call rewrite included, which reaches every
`N_CALL` a namespaced function's body holds and is free to rename the placeholder's `nd_name`
however it likes) has anything to trip over: this function never reads that name back, only the
one it captured at parse time into a table of its own.

The site -- the delegate row, the raw name, the NAMESPACE live right now (`tk_ns_current()`,
frozen at parse time, never read back off the mutable pass-time `tk_deleg_cur_ns`), the line, the
file -- is recorded in the SAME late table an element store's own deferred value already waits in
(`dl_*`, `teko_deleg.tk`, `dl_val` 2 here, never 0 or 1), reusing its bucket-by-node-id lookup
(`tk_dg_bucket_insert`/`tk_deleg_late_pend`) rather than a second one: D51's eighth pass already
measured that a per-node linear scan of this table is a real cost at compiler scale, and a
SECOND per-node scan for a brand new "is this node a pending thunk" question would repeat the
exact mistake it fixed. `tk_deleg_new_do` (the `dl_val` 2 branch of `tk_deleg_late_do`) is the
patch: it resolves the recorded name through `tk_deleg_resolve_fn` -- unmoved, the identical
namespace/flat/using order D62 already gave the contextual road, now taking `ns` as a PARAMETER
rather than the pass's own global, since this road's backstop sweep (`tk_deleg_late_rest`, never
actually reached in practice -- every `new Op(f)` site measured sits inside a function body, and
a teko global's own initializer is core-checked to be a CONSTANT, so it cannot hold one) runs
after that global is reset to 0 for the whole unit -- and then calls `tk_deleg_wrap`, exactly the
allocator-call construction the parse-time road always built, splicing it into the placeholder's
own identity (`tk_node_replace` + `tk_xt_move`, the same move `tk_deleg_val_do` makes for D62's
own field-store wrap, one line up in the same file).

**The overload pick, factored out of the single-declaration check rather than duplicated.**
`tk_deleg_check_sig`'s own arity/return/parameter-type/`ref`-out-kind comparison is now
`tk_deleg_sig_matches(si, d)`, a predicate the existing function calls unchanged (its own wording
and line untouched, `tests/refuse/deleg_new_kind.tk` and `deleg_new_ref_pointee.tk` byte for byte
the same) and `tk_deleg_pick_target` (new) calls too, walking `tk_deleg_root` for every DEFINITION
(never a `N_PROTO`, excluded the way `tk_ov_dup_def`, teko_over.tk, already excludes one from its
own "occupies the name" check -- `decl_valid` answers 1 for a prototype exactly as it does for a
body, and a plain forward-declared function would otherwise count as two candidates sharing a
name) named `f`, refusing `teko: <f> has no overload matching <Op(...)>` when none matches and
`teko: ambiguous overload for <Op(...)>: <f>` when more than one does (C# §10.2.3's method group
conversion, exact match, no widening -- teko has none for a delegate target either). `tk_deleg_
thunk` takes this road only when `tk_deleg_target_count(f) > 1`; a name declared exactly once
keeps the single-declaration path -- and its wording -- untouched, which is what makes the
"declared BELOW" defect close for free: `decl_find`/the root walk both see the whole unit once
this runs at pass time, whichever order the source wrote it in.

**The second refusal is unreachable, and the log says so rather than fabricating a fixture for
it.** Two declarations that both match a delegate's signature EXACTLY -- arity, return, every
parameter's type and kind, all equal -- are two declarations of the identical signature INCLUDING
return type, which is `function declared twice` at the CORE's own hand, checked incrementally as
each is parsed, well before this resolver or any pass ever runs (measured: a program with `i64
dup(i64 a)` declared twice, wrapped in `new Op(dup)`, refuses `function declared twice` at line 5
on the base and on this branch alike, never reaching `tk_deleg_pick_target` at all). Every other
angle this crumb tried to construct ambiguity from -- two overloads differing only by `ref`/`out`
(`tk_ov_refout_clash` refuses that pairing outright, in `tk_over_pass`, before it could ever
reach a delegate target -- but that pass runs AFTER this one, so this is not what saves it; the
KIND is still part of the exact match, and two candidates cannot both carry the delegate's own
kind at a position where they disagree), two namespace levels each supplying a candidate
(`tk_ns_call_try_prefixes` stops at the CLOSEST level with any declaration at all, never
considering a farther one), a `using` supplying a second namespace's overload (`tk_ns_call_try_
usings` already refuses THAT collision itself, in its own words, upstream of this resolver) --
was either refused earlier by an EXISTING gate or provably narrows to at most one exact match by
construction. `tk_deleg_pick_target`'s ambiguous branch is kept anyway, mirroring the C# rule
this crumb is built against and the project's own convention of a defensive branch documented as
such (D63's own "seat" table) rather than trusted to be dead code by a proof no verifier re-runs
by hand. No fixture reaches it, and none should be invented that does not measure a REAL defect.

**The capacity table gains no new define.** The new pending-thunk row shares `TK_MAXDGLATE` (512)
and its bucket with an element store's own deferred value (`dl_val` 0/1) rather than a table of
its own: both are "a node in the tree that needs a walk-time judgement, keyed by its own
identity, resolved once" in exactly the same shape, and giving the explicit thunk road a SEPARATE
512-row budget would let one construct starve the other's ceiling for no reason either one needs
its own. The overflow wording is its own, `teko: too many delegate targets awaiting resolution`,
so a program that exhausts the shared table on either half says which one it was doing.

**Dumps: identical for 154 of the 158 pre-existing fixtures; 4 differ by the SAME shape, and are
the same program.** `tests/surface_dateonly.tk`, `tests/surface_delegate.tk`,
`tests/surface_globals.tk` and `tests/surface_lambda.tk` (the four `tests/*.tk` fixtures whose
base source writes `new Op(f)`/`new Mut(bumpf)`/`new SetDay(nextday)`/etc over a BARE name;
`tests/surface_globals_rc.tk` also matches the same grep and is UNCHANGED, because its own use is
the LAMBDA form, `new Op((x) => ...)`, untouched by this crumb) each print the identical SET of
lines -- same line count, a sorted diff of zero, every declaration, every name, every type
unmoved -- in a DIFFERENT ORDER: the four generated declarations (the thunk, its vtable, its
release, its allocator) move from immediately after the declaration that wrote the `new`, to
wherever `tk_top_emit` lands when `tk_deleg_pass`'s own walk resolves the placeholder, later in
the unit. `mc`'s own resolver does not care about declaration order (a forward reference is
ordinary, `decl_find` walks the whole list), and the reclaim/codegen passes read the SAME four
declarations either way, which is what the sorted-diff-zero measurement is standing in for: not
"the same in spirit" but the same bytes, relocated. No OTHER pre-existing fixture's dump moves by
one byte.

**Fixtures.** `tests/surface_delegate.tk` grows four cases: `ovcheck` (the overloaded target),
`belowcheck` (declared below its own `new`), `ns_roads`'s own eighth road (`new Op(col)` from
INSIDE `geo`, the seventh D62 left open) and `usingcheck` (a namespaced-only target, `geo.gadd`,
reached through a file-level `using geo;` added for it), `expect-exit: 42` unmoved, `rt_live()`
unmoved by all three new helpers (purely local, no static/global storage). One refusal,
`tests/refuse/deleg_new_overload_none.tk`: neither arity of an overloaded `pick` matches the
delegate, `teko: pick has no overload matching Op(i64, i64)` at the `new` line.

**Docs.** `docs/reference/delegates.md`'s "Which function a bare name names" section states that
`new Op(f)` takes the identical order the contextual road always has, with the overload and the
namespace-through-`new` samples run inline; the stale paragraph claiming `new Op(name)` was the
one form the order did not reach is replaced by a runnable one that closes it.
`docs/reference/not-yet.md` drops its own row for the namespace gap this crumb closes.
`docs/reference/diagnostics.md` gains the two new wordings under "Delegates, lambdas and
captures" and a new row in the Capacity table for the shared `TK_MAXDGLATE` budget.

**Proof** (mc 0.15.23, macos/aarch64, base `afdfae88`): `mc build . --config mc.macos.toml`
clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **75 passed, 84 refused as
expected, 0 failed** (75/83 on the base: one refusal added, `surface_delegate.tk` grown in
place); `--dump-ast` of every pre-existing `tests/*.tk` (75) and `tests/refuse/*.tk` (83), each
compiled with its ORIGINAL base source by both binaries -- **154 byte-identical, 4 reordered-only
(sorted-diff zero, see above)**; `sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT
OK` (93.7s); `sh scripts/check-docs.sh` -> `docs ok: 615 links, 45 fragments, 392 diagnostics, 84
refusals, 146 samples`; `mc limits . --config mc.macos.toml` verdict **ok**, `grow` 0 on every
row of the macos/aarch64 leg (the base's OWN verdict at this commit is `grew` on four rows --
`passes`, `alias`, `types` grew, `syntax` tight -- a pre-existing property of the unmodified base
at this exact source size, unrelated to and not fixed by this crumb; this crumb's own larger
surface code is what happens to push those same four rows past their estimate threshold, closing
them to `ok` as a side effect, not a goal reported as if it were one) -- the compiler leg's
counted rows move only by the added surface code (`nodes` 158062 -> 166006, `funcs` 3199 -> 3292,
`lowered` 3181 -> 3273, `globals` 945 -> 948, `strings` 2139 -> 2178, `symbols` 6283 -> 6418,
`ins` 218423 -> 228120, all on the macos/aarch64 leg only -- a linux/x86_64 cross-build measured
locally cannot LINK on this host, so that leg is CI's to confirm); `mc pkg hash .`
`b8409a481cace40a589be06ecb92bd294c545e6be74c6471c335390eb3c75397` (base
`a8122b64a3235e39820a1cc1aa3fc1446e3b890fc684791fdbc12868b7343e50`: `teko_deleg.tk` is a listed
file, so the hash moves by design).

### D68 · A member name after `.` -- or in its own declaration -- may be a word teko has TAUGHT (2026-09-14)

C# lets a member share its name with a contextual keyword (`value`, `where`...); teko's own
words are reserved harder than that -- `word_is_taught` (mc core, `src/hooks.mc`) marks every
`syntax`/`syntax_stmt`/`syntax_expr`/`type_alias`/`type_new` word PROGRAM-WIDE, including one a
`class`/`struct`/`enum`/`interface`/`trait`/`delegate` declaration teaches on the spot -- and
every seat that read a member name called `p_ident()`, which refuses anything that is not a
plain `T_IDENT`. `public i64 ref;`, `i64 params() {}`, `Type.out`, `a.isize`, `E.ptr`, all
refused with mc core's own bare `name expected`, no `teko:` prefix, on a program with nothing
wrong beyond spelling a field the way C#'s `ref`/`params` would.

**Measured** (base `0ae03bcb`, `class H { public i64 value; }` / `i64 now() {}` first): `value`,
`now`, `count`, `get` are NOT taught words in this tree (no `type_alias`/`type_new`/`syntax*`
registers them) -- they compiled before this crumb and compile after it, unchanged. The real
refusal needs a word teko DID register: `ref`/`out`/`params` (`type_new`), `isize`/`usize`/
`byte`/`str` (`type_alias`). `public i64 ref;` (a field), `i64 params() {}` (a method), `Late.str`
(a static on a type declared BELOW the read, `tk_fwd_defer_static`) -- all `name expected` on
`0ae03bcb`, all compile and run after this crumb.

**The fix: one helper, twelve seats.** `tk_member_name()` (`teko_struct.tk`, beside `tk_newname`,
the type-name reader D60 already guards) reads `p_name()` -- the token's own lexeme, whichever id
it carries -- when `word_is_taught(p_id())` answers 1, `p_ident()` otherwise; every other seat
(`tk_newname` itself for a TYPE, a LOCAL, a PARAMETER, a FUNCTION) is untouched and keeps its
D60 refusal -- there is no shadow risk after a `.` or inside a member's own declaration, so the
widening stops exactly at the member seat.

| seat | file:line (before) | what it reads |
|---|---|---|
| `p.m` | `teko_expr.tk:604` (`tk_dot`) | an instance member on a typed receiver |
| `Type.m` | `teko_access.tk:344` (`tk_static_member`) | a static member, mconst, enum member |
| `Type.m` (declared below) | `teko_access.tk:427` (`tk_fwd_defer_static`) | a static on a `TK_PFWD` row |
| `PrimType.m` | `teko_prim.tk:834` (`tk_prim_static`) | a primitive's own static table |
| `base.m` | `teko_this.tk:148` (`tk_base_call`) | the base class's own method, direct |
| `p?.m` | `teko_null.tk:624` (`tk_qd_infix`) | Q2's `?.` placeholder |
| `ref p.m` | `teko_ref.tk:312` (`tk_ref_addr`) | a field address argument |
| `foreach (... in x.m)` | `teko_loop.tk:443` (`tk_fe_source`) | the loop source's own field (guard line 442 widened too: `p_id() != T_IDENT && !word_is_taught(p_id())`) |
| field / method / property | `teko_ops.tk:213` (`tk_op_name`) | every non-`operator` member name `tk_member` (teko_class.tk) reads |
| enum member | `teko_enum.tk:319` | the member's own declared name |
| interface member | `teko_iface.tk:607` (`tk_iface_member`) | a method or property signature |
| const member | `teko_const.tk:173` (`tk_member_const`) | `public const T m = e;` |

That is the "8 seats" the scout counted after `.` (`tk_dot` through `tk_fe_source`) plus the four
DECLARATION seats the task also named (`tk_op_name` covers field, method AND property in one
line, since `tk_member` reads all three through it).

**What stays refused, unwidened (D60).** A TYPE name (`class ref {}`): `teko: the name is
already a type: ref`, mc core's own `err_name`, unchanged -- `tk_newname` was not touched. A
LOCAL (`i64 ref = 5;`): `name reserved by a syntax/type_alias registration: ref`, mc core's own
message, no `teko:` prefix -- also unchanged, and per the crumb's own instructions this is not a
`teko:`-authored refusal, so no `tests/refuse/*.tk` fixture is added for it (there was nothing to
add: it already compiled to that same core wording before this crumb, and still does). Bare
(implicit `this.`) access is a THIRD, unrelated case this crumb does not touch: `return ref;`
inside a method still reads `ref`/`out` as the argument-position prefix `type_new("ref")`/
`type_new("out")` registers at EXPRESSION position -- the ambiguity is inherent to those two
specific words wearing a prefix meaning teko itself gave them, not a member-name gap, and it
predates this crumb (`this.ref` reads the field exactly as it always did).

**Fixture.** `tests/surface_member_word.tk` -- one class (`Box`), one enum (`Kind`), one type
declared below the code that reaches its static (`Late`), eight checks (field, method, static,
property, `this.`, `?.`, enum member, forward static), each keyed to a genuinely taught word
(`ref`, `params`, `out`, `isize`, `byte`, `usize`, `ptr`, `str`) -- `// expect-exit: 42`.
`docs/reference/types.md`'s own "reserved program-wide" sample grows a `class Box` proving the
same `ref`/`params` pair, run by `scripts/check-docs.sh`. No `docs/reference/not-yet.md` row
named this gap (searched: no row mentions a member sharing a taught word), so none is removed.

**Proof** (mc 0.17.0, macos/aarch64, base `0ae03bcb`): `mc build . --config mc.macos.toml` clean;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **76 passed, 84 refused as expected, 0
failed** (75/84 on the base: one fixture added, none removed, none reworded); `--dump-ast` of
every pre-existing `tests/*.tk` and `tests/refuse/*.tk` (159, the new fixture excluded since the
base refuses it), each run against ITS OWN source by both binaries -- **159/159 byte-identical**
(stdout+stderr, so a refusal's own wording is part of the comparison, not just an accepted
program's tree); `sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (46.5s);
`sh scripts/check-docs.sh` -> `docs ok: 614 links, 45 fragments, 392 diagnostics, 84 refusals, 146
samples`; `mc limits . --config mc.macos.toml` verdict **ok** on every row but `heap` (never
cited, per this crumb's own gate).

**Copilot on #724, pass 1.** `word_is_taught` also answers 1 for punctuation with a registered
infix handler (`+`, and member access's own `.`/`[`/`?`), so the helper accepted `i64 +;` as a
member name. `tk_member_word_ok` (teko_struct.tk) now restricts the widening to a WORD — an
identifier, or a taught token whose lexeme starts with a letter or `_` — and `tk_loop`'s
`foreach (... in x.m)` guard reads the same predicate; `i64 +;` is `name expected` again, from
the core. The fixture grew the seats with a parser of their own: an interface member (`when`),
a member const read through the type (`scope`), `ref b.match` into a `ref` parameter, `foreach`
over an inline array field (`switch`), and `base.type()` from an override — every one keyed to a
`syntax` word the base compiler refuses. Measured while probing: `loop` is mc's OWN keyword
(refused everywhere, rightly), and a member named `namespace` dies `mc: empty lexeme` in the
core — recorded in `not-yet.md`, not worked around.

### D69 · Five capacity ceilings measured too low for a real program: `TK_MAXFWD` 32 -> 256, `TK_MAXOS` 128 -> 4096, `TK_MAXSTRUCT` 32 -> 256, `TK_MAXEMIT` 512 -> 4096, `TK_MAXMETHOD` 128 -> 1024 (2026-09-14)

(D67/D68 are reserved by in-flight scouts, not yet in this log at write time; this entry
takes the next free number, D69.)

Two verifiers hit two separate ceilings this week, both sized in the crumb's early days and
never re-measured against a program with more than a handful of types or stores.

**`TK_MAXFWD` (teko_fwd.tk), the lexical pre-scan's own table** (`fw_name`/`fw_ty`/
`fw_kind`/`fw_vis`/`fw_proj`/`fw_span`/`fw_spanlen`/`fw_multi`/`fw_mat`, plus `fl_name` --
ten `uptr`/`i64` arrays, 8 B a column): a `class`/`struct`/`interface`/`delegate` keyword the
byte-level pre-scan finds ANYWHERE in the reachable source (the whole file, not just a
forward reference) takes one row, for the whole build, never released. The 32nd was fine;
the 33rd refused `teko: too many forward-declared types` at `?:0:` (no file/line: the scan
runs before any real parsing). Raised to 256.

**`TK_MAXOS` (teko_struct.tk), one array, `os_node[TK_MAXOS]`** (one `i64` column): a store
this module emits into a slot of counted type (a field, an inline array element, an
auto-property's backing) takes one row, keyed to the STORE SITE in the source (a store
inside a loop body is one row no matter how many times the loop runs). D51's eleventh pass
already fixed the double-counting that had silently halved this ceiling for a global array
(128 declared, 64 delivered); this entry raises the declared number itself, matching
`TK_MAXXT`/`TK_MAXFS`, both already 4096 for the same reason -- one row per site, a unit-wide
table that never shrinks. Raised to 4096.

**Cost, measured, not assumed.** `mc limits . --config mc.macos.toml`, both legs, mc
**0.15.23**, macos/aarch64, base `59652293`:

| leg | `globals` before | `globals` after | `heap` used before | `heap` used after | verdict |
|---|---|---|---|---|---|
| compiler (`build/teko`) | 944 / 2546 reserved | 944 / 2546 reserved (unmoved) | 95084208 | 95115648 | `ok`, `grow` 0 |
| `tests/hello.tk` (compiled BY `build/teko`) | 0 / 32 reserved | 0 / 32 reserved (unmoved) | 468016 | 492592 | `ok`, `grow` 0 |

`globals` counts DECLARATIONS (one per `i64 x[N];`, whatever `N` is), so raising a
`#define`'s numeric literal moves it not at all -- the ten arrays above and `os_node` are
still ten and one top-level globals, respectively, before and after. The real cost is the
STATIC storage the first two ceilings' arrays reserve in `build/teko`'s own BSS, which is what
`heap`'s `used` column picks up: +30720 B on the compiler's own leg (10 x 8 B x 224 for
`TK_MAXFWD`'s growth, 1 x 8 B x 3968 for `TK_MAXOS`'s, plus a few bytes of comment text the
pre-scan counts), +24576 B on the `hello.tk` leg (the SAME arrays, now compiled into the
teko compiler that leg measures, `os_node`'s own share of it). Both legs stay `ok` with
`grow` 0 by a wide margin (`heap` reserved is 223739904 / 33554432), so 256 and 4096 are the
numbers taken, not a smaller compromise.

**Proof, throwaway probes in scratch (not fixtures -- a refuse fixture pinning a ceiling
breaks the day it grows, D63's lesson), each run on the base build (`59652293`, ceilings 32
and 128) and on this crumb's build (256 and 4096), mc 0.15.23, macos/aarch64:**

| probe | shape | base (`59652293`) | this crumb |
|---|---|---|---|
| 33 distinct classes (`class C0 { public i64 v; }` x33, `#include "lib/rt.tk"`) | one keyword occurrence per class, scanned once each | refused `teko: too many forward-declared types` at `?:0:` (scan time, no line) | refused `teko: too many type declarations` at line 34 (a SEPARATE ceiling, `TK_MAXSTRUCT`, see below) -- proves `TK_MAXFWD`'s OWN ceiling cleared: the SAME 33 classes no longer trip the scan |
| 257 distinct classes | same shape, 257 of them | refused `teko: too many forward-declared types` at `?:0:` | refused `teko: too many forward-declared types` at `?:0:`, unchanged wording -- the new ceiling (256) is honored and reported the same way |
| 129 stores into elements of a LOCAL `Probe[]` (one class, one instance stored 129 times by literal index -- `TK_MAXOS` counts SITES, not runtime iterations, so a loop proves nothing here) | 129 store statements | refused `teko: too many stores into a slot of class type` at the 129th | compiles, runs 42 |
| 4096 of the same | 4096 store statements | (not run; already known refused past 128) | compiles, runs 42 |
| 4097 of the same | 4097 store statements | (not run) | refused `teko: too many stores into a slot of class type` at the 4097th |
| 512 / 513 deferred delegate-element stores (`ops[i] = pick();`, `pick()` a CALL, so `tk_deleg_store_late` defers every one to `TK_MAXDGLATE`, teko_deleg.tk, a SEPARATE 512-row table -- each also takes a row of `TK_MAXOS` first, since a delegate is a counted type) | 512 / 513 stores | (base already refuses at the 65th on a GLOBAL array, D51 -- this probe uses a LOCAL one, unaffected) | 512 compiles, runs 42; 513 refuses `teko: too many element stores of unknown type` (`TK_MAXDGLATE`'s own wording, not `TK_MAXOS`'s -- `TK_MAXOS` no longer fills first now that it is 4096) |

The last row is why `docs/reference/diagnostics.md`'s note under `"teko: too many element
stores of unknown type"` needed a wording change, not just a number: it used to say a
program with 513 such stores never reaches this table's own ceiling because `TK_MAXOS`
(128) fills first, at the 129th; with `TK_MAXOS` at 4096 that is no longer true, and the
513th now answers with THIS table's own message.

**Adjacent finding, not fixed here: `TK_MAXSTRUCT` (teko_struct.tk, still 32, "structs,
classes and interfaces declared in one source") is coupled to `TK_MAXFWD` in lockstep for
any plain, non-generic program, and raising `TK_MAXFWD` alone does not let one declare more
than 32 distinct types.** `tk_type_row_new`, the ONE function that appends a row to the
shared type table `TK_MAXSTRUCT` bounds, is called both by a real declaration
(`tk_type_add`, every `class`/`struct`/`interface`/`delegate` alike -- `teko_deleg.tk:285`
calls it too, so the comment naming only three of the four kinds is stale) and by
`tk_fwd_row`/`tk_fwd_row_by_ty` materializing a forward-scanned placeholder. A `partial`
class's SECOND and later parts do NOT take a second row of EITHER table (`tk_fwd_try_decl`:
`if (seen >= 0) { set_fw_multi_at(fi, 1); return p; }`, no second `tk_fwd_add`; the real
parser adopts the existing row too), so there is no way, in the language as taught today,
for `tk_nfwd` to exceed `tk_nstruct` for a program with no generics: every scanned keyword
becomes exactly one real declaration, and the pre-scan (a single pass over the WHOLE
reachable source, run before any parsing) always finishes counting all of them before the
parser reaches declaration 1. Measured: the 33-classes probe above, run on THIS crumb's
build, no longer trips `TK_MAXFWD` (which now allows up to 256) but trips `TK_MAXSTRUCT`
(still 32) one parse-time step later, at the exact same count -- the wording changes, the
practical ceiling does not. `docs/specs/nullable.md` already names this ceiling and its
cost ("a raise costs BSS in `globals`... measure before raising it"), so this is a known,
tracked tension, not a new one; whether it needs raising too is a separate crumb's call
(possibly D67 or D68, reserved and not yet in this log at write time) -- out of scope here,
since the task named these two ceilings only.

**Gate**, mc **0.15.23** (`MC_VERSION`), macos/aarch64, base `59652293`: `mc build . --config
mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 62
refused as expected, 0 failed**, identical count to the base; `--dump-ast` byte-identical
against the base build for all 73 `tests/*.tk` and all 62 `tests/refuse/*.tk` -- neither
ceiling changes any accepted or refused program already in the tree; `sh scripts/
bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK` (73/73, 196.7s); `sh scripts/
check-docs.sh` -> `docs ok: 611 links, 41 fragments, 389 diagnostics, 62 refusals, 143
samples`; `mc limits` verdict `ok` with `grow` 0 on both legs, `globals` unmoved on both (see
table above); `mc pkg hash .`
`f7ef6013d75a654878e132765963b6bd288ca718575a5b10481d623a36298f30` (base
`5c0738020efa4533cdf6442d5fa6e7cc593c6c8776d9974e30450f0e9c299cce`: `teko_fwd.tk`,
`teko_struct.tk` and `docs/reference/diagnostics.md` are listed files, so the hash moves by
design).

**Follow-up (same day): `TK_MAXSTRUCT` (teko_struct.tk, 32) raised to 256, closing the gap
this entry already named as an adjacent finding.** The lockstep argument above holds for any
plain, non-generic, non-partial source: `tk_type_row_new` is the ONE function that appends a
row to the shared type table, called both by a real declaration and by a forward placeholder's
adoption, so `tk_nstruct` never exceeds `tk_nfwd` for such a program. Raising `TK_MAXFWD`
alone therefore bought nothing past the 32nd type: a 33rd class cleared the (now 256-deep)
scan and refused one parse-time step later, at the identical count, under `TK_MAXSTRUCT`'s own
message. Measured, `mc` **0.15.23**, macos/aarch64, this crumb's own build (`teko_struct.tk`
edited, everything else as `85379111` left it) against base `59652293`:

| probe | shape | at `TK_MAXSTRUCT` 32 (`59652293`/`85379111`, unmoved by D69's own FWD/OS raise) | at `TK_MAXSTRUCT` 256 (this follow-up) |
|---|---|---|---|
| 33 classes, one field each, `#include "lib/rt.tk"` | one keyword per class | refused `teko: too many type declarations` at line 34 | compiles, runs 42 |
| 64 classes, 2 fields + 1 method each | 64 classes, 128 fields, 64 methods, all under `TK_MAXFIELD`/`TK_MAXMETHOD` | (not reached: the 33rd already refused) | compiles, runs 42 |
| 33 interfaces, 2 methods each | one keyword per interface -- interfaces share this same table (`teko_iface.tk`'s declaration road also calls `tk_type_add`) | (not reached, same refusal as the classes probe, at the 33rd) | compiles, runs 42 |
| 40 interfaces, 2 methods each | 80 interface methods, well under `TK_MAXIFMETH` (128) | (not reached) | compiles, runs 42 |
| 40 delegates | one keyword per delegate -- `teko_deleg.tk:285` calls `tk_type_row_new` too, so a delegate spends a row of this table exactly like a class does | (not reached) | compiles, runs 42 |
| 257 classes, one field each (`TK_MAXSTRUCT`+1) | -- | refused `teko: too many forward-declared types` at `?:0:` (scan time) |

The last row is the coupling working as designed, not a gap: `TK_MAXSTRUCT` was raised to
**match** `TK_MAXFWD` (both 256), and the pre-scan (`teko_fwd.tk`) always runs to completion
BEFORE any real parsing, over the WHOLE reachable source. For any plain program, `tk_nstruct`
can never exceed `tk_nfwd`, so once the two ceilings are equal, the fwd-scan ceiling is
always the one a 257th type hits FIRST -- `TK_MAXSTRUCT`'s own message becomes unreachable
from plain (non-generic, non-partial) source, exactly as it was when both ceilings stood at
32 together, before D69's own FWD raise briefly exposed the mismatch. Setting `TK_MAXSTRUCT`
any higher than `TK_MAXFWD` would only waste BSS (the fwd scan caps every plain program at
`TK_MAXFWD` regardless); setting it any lower reproduces the exact bug this follow-up closes.
256 is therefore not a compromise -- it is the only value that lets a program actually use the
room D69's `TK_MAXFWD` raise already bought it.

**Cost, measured directly from the source (not `mc limits`' `heap` column, which the task
asked not to cite here).** Every array this module -- and the two others that also key a row
by counted-type identity -- sizes by `TK_MAXSTRUCT`:

| file | arrays | shape | columns | bytes/row |
|---|---|---|---|---|
| `teko_struct.tk` | `sr_name`, `sr_ty`, `sr_size`, `sr_base`, `sr_form`, `sr_nv`, `sr_m0`, `sr_mn`, `sr_ni`, `sr_vis`, `sr_proj`, `sr_abst`, `sr_part`, `sr_off`, `sr_hline`, `sr_hfile`, `ha_ety`, `nl_of` | 18 x `[TK_MAXSTRUCT]` | 1 | 144 B |
| `teko_deleg.tk` | `dg_ret`, `dg_np` | 2 x `[TK_MAXSTRUCT]` | 1 | 16 B |
| `teko_deleg.tk` | `dg_pty`, `dg_pk` | 2 x `[TK_MAXSTRUCT * TK_DGMAX]` (`TK_DGMAX` = `MAXPARAMS` - 2 = 10) | 10 | 160 B |
| `teko_heaparr.tk` | `ha_gen`, `ha_put` | 2 x `[TK_MAXSTRUCT]` | 1 | 16 B |

336 B per `TK_MAXSTRUCT` row (22 one-column `i64`/`uptr` arrays at 8 B, plus two ten-column
arrays at 80 B each), all `i64`/`uptr`, 8 B on both macos/aarch64 and linux/x86_64. Total BSS
this table alone reserves: 32 x 336 B = 10752 B before, 256 x 336 B = 86016 B after -- a
+75264 B (73.5 KB) delta, far under the "a few MB" budget the task set, and `TK_MAXFIELD`
(already 256, unmoved by this follow-up) shows the codebase already accepts that order of
magnitude for a sibling table of the same shape.

**Sibling ceilings a 33+-type program hits next, probed the same way (mc 0.15.23,
macos/aarch64, this crumb's build):**

| ceiling | value | probed shape | fires at |
|---|---|---|---|
| `TK_MAXEMIT` (`"teko: too many generated declarations in one unit"`) | 512 (unmoved) | N plain classes, ONE field, no methods, no interface -- a real class still emits ~4 top-level declarations (constructor, release, alloc, and one more) even with no virtual/interface surface | the 129th class (128 compile and run 42; 129 refuses at line 129) -- THIS is the practical ceiling for "how many plain classes fit in one source", well before `TK_MAXSTRUCT`'s own 256 |
| `TK_MAXMETHOD` (`"teko: too many methods"`) | 128 (unmoved) | 64 classes, 2 fields + 2 own methods each, each implementing one shared interface (1 method) -- 3 methods/class, 192 class methods total | the 43rd class (line 44 of the combined-shape probe, ~128th method total) |
| `TK_MAXFIELD` (`"teko: too many fields"`) | 256 (unmoved, already raised past 32 in an earlier crumb) | not the bottleneck at 64 classes x 2-3 fields (128-192 fields, under 256) | not reached in any probe above |
| `TK_MAXIFMETH` (`"teko: too many interface methods"`) | 128 (unmoved) | 40 interfaces x 2 methods = 80 | not reached (well under) |
| `TK_MAXIMPL` (`(class, interface)` pairs, `"teko: too many implemented interfaces"`) | 64 (unmoved) | 64 classes x 1 interface each = 64 pairs | not reached in the probes above (`TK_MAXMETHOD` fires first, at class 43, before the 64th pair is registered) |

No `TK_MAXIFACE` or `TK_MAXVT` constant exists in this codebase: an interface's own count is
`TK_MAXSTRUCT` (the shared type table, D69's own finding above), its methods are
`TK_MAXIFMETH`, and a virtual slot is `TK_MAXVSLOT` (128, unmoved, not probed here -- no
probe above declared a `virtual` method). `TK_MAXEMIT` was the most pressing of these at write
time: a program of plain classes alone -- no fields beyond one, no methods, no interfaces --
already needed a raise past 128 classes to grow further, a considerably lower ceiling than
`TK_MAXSTRUCT`'s new 256. It was probed and recorded here but out of scope for THIS pass (the
task named `TK_MAXSTRUCT` only); the second follow-up below closes it, together with the
`TK_MAXMETHOD` row in the table above.

**Gate**, mc **0.15.23** (`MC_VERSION`), macos/aarch64, base `59652293`: `sh scripts/
fixtures.sh ./build/teko mc.macos.toml` -> **73 passed, 62 refused as expected, 0 failed**,
identical count to D69's own gate; `--dump-ast` of every one of the 73 `tests/*.tk` and 62
`tests/refuse/*.tk` (135 files) byte-identical against a `build/teko` built from base
`59652293` in a separate worktree -- `TK_MAXSTRUCT`'s raise changes no accepted or refused
program already in the tree; `sh scripts/bootstrap.sh --os macos --arch aarch64` ->
`FIXPOINT OK` (73/73, 162.8s, `teko1.o == teko2.o` on the first turn, `--dump-asm` of
`teko2`/`teko3` empty diff over 219047 lines); `sh scripts/check-docs.sh` -> `docs ok: 611
links, 41 fragments, 389 diagnostics, 62 refusals, 143 samples`, unmoved; `mc build . --config
mc.macos.toml --limits` verdict `ok` with `grow` 0 on both legs, `globals` unmoved on both
(compiler leg 944/2546 reserved, `hello.tk`-by-`build/teko` leg 0/32 reserved -- identical to
D69's own table, confirming the `TK_MAXSTRUCT` arrays cost `globals` nothing: a `#define`'s
numeric literal moves no DECLARATION count, only the reserved bytes behind each one); `mc pkg
hash .` `ac4870f1846bf46d66f7a76a80b7d7904a6f46a94325d55ff5d2f2e7107f8304` (base, `85379111`:
`f7ef6013d75a654878e132765963b6bd288ca718575a5b10481d623a36298f30` -- `teko_struct.tk` is a
listed file, so the hash moves by design; `DECISION_LOG.md` and the two docs edits are not
listed files and move the hash not at all).

**Second follow-up (merged forward past D61/D62/D63/D66/D71): `TK_MAXMETHOD` (teko_class.tk,
128) raised to 1024 and `TK_MAXEMIT` (teko_struct.tk, 512) raised to 4096, closing the two
sibling ceilings the table above named and left out of scope.** Every array either constant
sizes is a plain `[TK_MAXMETHOD]` or `[TK_MAXEMIT]` literal, so raising the `#define` grows
every one of them without a second edit: `grep -n TK_MAXMETHOD teko*.tk lib/*.tk` finds 13
one-column arrays, all in `teko_class.tk` (`mt_name` through `mt_abst`), no other module;
`grep -n TK_MAXEMIT teko*.tk lib/*.tk` finds 2, both in `teko_struct.tk` (`tk_emit_name`,
`tk_emit_node`), no other module. Every loop that walks either table bounds itself by the
RUNNING count (`i >= tk_nmethod`, `i >= tk_nemit`), never by a hardcoded literal, so no
secondary table can fall out of step with either cap: `grep -rn tk_nmethod teko*.tk lib/*.tk`
and `grep -rn tk_nemit teko*.tk lib/*.tk` show every read site is one of those two guards, a
constructor-time count snapshot (`tk_own_methods = tk_nmethod`, a trait's own copy,
unaffected), or a `!= 0` used-at-all test.

**Cost, from the source, never from `mc limits`' `heap` column.** Both tables are all-`i64`/
`uptr` columns, 8 B each, on both macos/aarch64 and linux/x86_64:

| table | file | arrays | columns | bytes/row | before (rows) | after (rows) | delta (rows) | delta (bytes) |
|---|---|---|---|---|---|---|---|---|
| `TK_MAXMETHOD` | `teko_class.tk` | `mt_name`, `mt_cls`, `mt_sig`, `mt_fn`, `mt_np`, `mt_nreq`, `mt_d0`, `mt_ret`, `mt_slot`, `mt_vis`, `mt_static`, `mt_prop`, `mt_abst` (13) | 1 | 104 B | 128 | 1024 | 896 | +93184 B |
| `TK_MAXEMIT` | `teko_struct.tk` | `tk_emit_name`, `tk_emit_node` (2) | 1 | 16 B | 512 | 4096 | 3584 | +57344 B |

Combined: **+150528 B (147 KB)**, far under the "a few MB" this codebase already spent on
`TK_MAXSTRUCT`'s own raise a paragraph up, and on `TK_MAXFIELD` before it.

**Proof, throwaway probes in scratch (not fixtures, same reasoning as D69's own first
paragraph), each run on the base compiler (`origin/main` `409bc8c0`, caps 128/512) and on
this crumb's build (1024/4096), mc 0.15.23, macos/aarch64:**

| probe | shape | base (`409bc8c0`) | this crumb |
|---|---|---|---|
| 129 methods, one class, no fields, no interface (`class C { public i64 m0() {...} ... m128() {...} }`) | one row per method, the SAME table whether the class is one or many | refused `teko: too many methods` at the 129th declaration (line 132) | compiles, runs 42 |
| 1025 methods, same shape | 1025 rows | (not run; already known refused past 128) | refused `teko: too many methods` at the 1025th declaration (line 1028) — the new ceiling (1024) is honored, exact boundary: methods 1..1024 all land, the 1025th trips |
| 129 lambda literals stored into one instance field (`delegate i64 Op(i64 x); class H { public Op cb; } … h.cb = (i64 x) => x + N;`, D66's field road, 129 times) — each lambda literal is 4 fresh `tk_top_emit` calls (the function, its vtable, its release, its allocator; `H`'s own declaration spends 4 more, measured directly against the boundary below) | 4 emits/lambda + 4 for `H` itself | refused `teko: too many generated declarations in one unit` at the 129th store (line 136): 4 (H) + 127×4 = 512 exactly, the 128th store (0-indexed 127) is the one that would spend row 513 | compiles, runs 42 |
| 1025 of the same | 4 emits/lambda + 4 for `H` | (not run) | refused `teko: too many generated declarations in one unit` at the 1025th store (line 1032): 4 (H) + 1023×4 = 4096 exactly, the 1024th store (0-indexed 1023) is the one that would spend row 4097 — the new ceiling (4096) is honored at the identical arithmetic boundary |

The two `129` rows share the shape of D69's own "Sibling ceilings" probes above (methods
count is class-count-agnostic). The emit probe stores LAMBDA LITERALS, not `new Op(fn)`
thunks: a literal is never memoized, so each written occurrence is its own fresh
`tk_top_emit_as` quartet, where a named function forwarded through D63's thunk table would
have undercounted (one thunk serves every caller). The store also has to be a FIELD, not a
bare local reassigned by name — `Op f; f = (i64 x) => ...;` repeated is one of D66's own
CLOSED roads (bare-name assignment still needs `new Op(...)`) — so the probe declares one
field (`H.cb`) and stores a fresh lambda into it repeatedly, exactly the road D66 opened.

**Gate**, mc **0.15.23** (`MC_VERSION`), macos/aarch64, base `origin/main` `409bc8c0` (this
crumb also carries D61/D62/D63/D66/D71, merged forward before this pass): `mc build .
--config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` -> **74
passed, 71 refused as expected, 0 failed**, identical count to the merged base; `--dump-ast`
byte-identical against a `build/teko` built from base `409bc8c0` in a separate worktree for
**all 145** `tests/*.tk` (74) and `tests/refuse/*.tk` (71) — 4937127 bytes of dump, neither
ceiling moves any accepted or refused program already in the tree; `sh scripts/bootstrap.sh
--os macos --arch aarch64` -> `FIXPOINT OK` (74/74, 90.1s, `teko1.o == teko2.o` on the first
turn, `--dump-asm` of `teko2`/`teko3` empty diff over 219327 lines); `sh scripts/
check-docs.sh` -> `docs ok: 614 links, 42 fragments, 389 diagnostics, 71 refusals, 144
samples`; `mc build . --config mc.macos.toml --limits` verdict `ok` with `grow` 0 on both
legs, `globals` unmoved and identical to the base build on both (compiler leg 945/2548
reserved, `hello.tk`-by-`build/teko` leg 0/32 reserved) — the two `#define`s move no
DECLARATION count, only the reserved bytes behind each one, confirmed directly against a
`409bc8c0` build rather than assumed; `mc pkg hash .`
`d614aa95490bf97ab93959b31e4a9761ce02d5301acc41418e9668b3387e7460` (base `409bc8c0`:
`6252f75a4f9f261365d651c6b318d6a1985cf4330c9605c8fcb4d1aee95dd1a6` — `teko_class.tk`,
`teko_struct.tk` and `docs/reference/diagnostics.md` are listed files, so the hash moves by
design).

### D70 · A lambda inside a method reads the CLASS's own member, never a same-named global, silently (2026-09-14)
`i64 n = 100; class Holder { public i64 n; public i64 direct() { return n; } public void
arm() { this.cb = new Op((i64 x) => x + n); } }` -- `direct()` read the field (1,
`tk_this_ident`); `arm()`'s own lambda read the GLOBAL instead (100), for the identical bare
name, neither refused. `tk_lam_check_name` (teko_deleg.tk) asked scope, capture,
`decl_find` (functions only) and `tk_struct_find` (types only) -- never the enclosing
class's own members -- so a global that only happened to share a field's name silently won
the door `tk_this_field` already guards for the method itself.

D11 stands: a lambda captures nothing implicitly, `this` included, and giving it one is a
design of its own -- a closure holding a counted `this` opens a reference cycle the reclaim
does not break on its own, an open fork rather than a patch here (recorded in
`docs/reference/not-yet.md`). So the fix is the refusal a field with no colliding global
already gave: `tk_lam_member` (new, teko_deleg.tk), asked right before `decl_find` for
every bare `N_IDENT`, checks `tk_field_find`/`tk_mconst_find`/`tk_prop_find`/
`tk_method_named_find` against `tk_body_class` -- the PARSE-time class the lambda's own
`tk_lambda_finish` still runs inside (the generated function is only promoted to a
top-level `N_FUNC` afterwards, past every later pass's own reach). An INSTANCE member reads
`teko: X is not captured; add it to use (...)`, the same sentence a plain field already
answered with; a STATIC one takes no receiver and resolves right there, the same road
`tk_this_field_addr`/`tk_mconst_use`/`tk_prop_static_use` already open for the method
itself -- measured: it did not resolve before this fix either (a static field/method named
bare inside a lambda answered the SAME "not captured", or a raw core error for a call --
never a working read), so nothing regresses by making it work now.

The CALL twin of the same door: `go(x)` inside a lambda, `go` a method of the class.
`tk_lam_walk` only ever looked at `N_IDENT`; a call's own name was never checked, so it fell
straight to the core's own resolver (`gen_resolve.mc`), which knows no function literally
named `go` (a method's real symbol is always mangled) and refused `call to unknown
function` -- the core's wording, never teko's -- and a FREE function of the identical name
would have silently answered in `go`'s place, the same silent-shadow bug for a call instead
of a read (measured). `tk_lam_check_call` (new) asks `tk_method_named_find` the same way,
before `tk_lam_walk` even reaches the call's arguments; a STATIC method resolves (the
mangled symbol takes the call node's own name, `tk_fill_defaults` filling what the site
left out); an INSTANCE one is refused by a literal of its own, `teko: a method is not
reachable from a lambda`, completed by the name -- `use (...)` captures a VALUE, never a
method, so the field's own sentence would read false here.

**A third door, found once D65 landed on top of this crumb: a delegate FIELD, called
bare.** `class H { public Op cb; } i64 cb(i64 a) { return a + 100; }` -- `direct()`'s own
`cb(x)` reads the FIELD (D65, `tk_this_deleg_call`), and a lambda's own bare `cb(x)` read
the FREE function instead, silently -- the identical silent-shadow this crumb already
closed for a plain field and a method, on the one road D65 could not reach: its own
`tk_this_deleg_call` runs at PASS time (`tk_this_fix`, pass 6), by which point a lambda's
generated body is no longer inside any method at all (`tk_lambda_finish` promotes it to a
top-level `N_FUNC` at PARSE time, well before pass 6 ever starts), and `tk_field_deleg_call`
(teko_expr.tk), the one builder every other delegate-call door funnels through, is itself
PARSE-time and reads its arguments off the TOKEN STREAM (`tk_args`) -- a lambda's own
`N_CALL` already carries its arguments PARSED, as `nd_a(n)`, so neither existing builder
fits as written. `tk_lam_deleg_call` (new, teko_deleg.tk) is the third: asked by
`tk_lam_check_call` when the name is no method, it reads `tk_field_find` against
`tk_body_class` and, on a delegate row, calls `tk_deleg_build` directly over the field's own
load and the call node's own `nd_a(n)` -- the same builder `tk_this_deleg_call` and
`tk_static_deleg_call` each wrap for their own PASS/PARSE shape, so D59's argument judge
(arity, `ref`/`out` kind, the pointee, C# widening) rides along here exactly as it does on
every other delegate-call road. A STATIC field needs no receiver and resolves the same way
`tk_static_deleg_call` (teko_access.tk) resolves the type's own qualified spelling; an
INSTANCE one reads the crumb's own sentence, `is not captured`, an array field is excluded
(`fd_nel_at`, D65's own guard on every other door), and `tk_check_member` rides along on the
STATIC road exactly as `tk_static_deleg_call` already asks it.

**A guard added at the same door, not measured on the base but a genuine gap in this
crumb's own first draft:** `tk_lam_check_call` asked the method table and (now) the field
table before ever asking whether the CALLED name was a local, a parameter or a capture of
the lambda's own scope -- unlike `tk_lam_check_name`'s `N_IDENT` road, which already asks
`tk_ty_scope_find`/`tk_lc_find` first, `tk_lam_check_call` asked neither, so `use (cb) =>
cb(2)` where `cb` is a captured delegate PARAMETER, inside a class that also happens to
declare a member of that name, would have been hijacked into the member's own call instead
of the capture's. `tk_lam_check_call` now asks `tk_ty_scope_find`/`tk_lc_find` first, the
same two calls `tk_lam_check_name` already opens with, before either member table -- a
local answers before a member, C#'s own rule, unchanged for the read half and now honoured
for the call half too.

**The WRITE twin, closed on the same PR before merge (2026-09-15).** The first pass above
recorded, and left open, the write half of the identical bug: `(i64 x) => { n = x; }` inside
`arm()`, `n` a field shadowed by a same-named global, silently wrote the GLOBAL too --
`tk_lam_walk`'s `N_ASSIGN` branch checked a by-reference capture and nothing else; a plain
field/global write target was never asked anything at all. Measured directly against
`origin/main` `bcee28f2` (pre-D70 entirely, so the READ half was broken too): a probe with
`this.cb = new Op((i64 x) => { n = x; return n; });` returned the encoding for "wrote the
global, field untouched" on both the base and this branch's own first pass, instance AND
static alike.

`tk_lam_member_store` (new, `teko_deleg.tk`) is the write door: asked by `tk_lam_walk`'s
`N_ASSIGN` case for every target that is not a real local, a parameter or a by-value capture
(`tk_ty_scope_find`, the identical local-answers-first order the read side already opens
with) and not a by-reference capture (the existing branch, unchanged), it reads the same
four tables `tk_lam_member` reads for a bare NAME and builds the STORE `tk_this_assign`
builds for the method itself: a STATIC field stores through `tk_field_store_val`, the
identical coercion gate `tk_static_use`/`tk_fwd_resolve_static_one` take; a member CONST is
refused `teko: a constant is not assigned or called`, factored into `tk_const_assign_refuse`
(`teko_access.tk`) so the qualified `Type.C = e` door and this one ask the same string, never
a second copy of it; a STATIC property with a `set` calls it with no receiver, the exact call
`tk_prop_static_use` builds; every INSTANCE road -- field, property, method name -- reads
`tk_lam_not_captured`, unchanged from the read side (D11 stands: `this` is not implicitly
captured, so nothing a lambda can WRITE through an instance member exists). `n += x;`/`n++;`
reach the identical door as `n = x;` does: `<teko-loop-prelude>`'s own `#rule` lowers both to
`n = n + x;` at PARSE time, ahead of this walk, so only the assignment TARGET is this
function's own business -- the RHS is an ordinary expression `tk_lam_walk` already walks
first, and the READ of `n` inside it resolves through `tk_lam_member` like any other.

**A second, narrower gap found while wiring the write door, not measured on the base
independently but blocking it outright:** `tk_lam_member`'s own field and STATIC-property
branches replaced a bare NAME with a load (`tk_ld`/`tk_call`) and never tagged it
(`tk_xt_put`) the way `tk_this_ident`/`tk_this_prop_read` (`teko_this.tk`) already tag their
own identical loads. A plain READ never needed the tag -- nothing downstream asked
`tk_fs_vty` of it -- but the WRITE door does, the moment the read of `n` sits inside `n`'s
own store (`sn += y;` lowers to a STORE whose VALUE reads the same field): `tk_fs_vty`
answers -1 for an untagged `N_CALL`/`N_BINARY` and defers the judgement to a later pass that
can never settle it for a raw `ld64`/`st64` call (no declaration owns that name, so
`decl_find`/`tk_ty_of`'s own `N_CALL` arms answer -1 for it at ANY pass), and the store died
`teko: the type of this value is not known here` on a program that types clearly at the
point the load is built. Measured directly: `H.sn += y; H.sn++;` inside a lambda refused with
that sentence before the tag was added, and resolved (`H.sn` 0 -> 3 -> 4) once it was. Both
branches now carry the identical `tk_xt_put` their `teko_this.tk` siblings already carry --
an addition to a SEPARATE side table, not to the node itself, so `--dump-ast` (which prints
node fields, not this table) is unmoved by it.

**Fixtures.** `tests/refuse/lambda_field_name.tk`: an instance FIELD read bare, the exact
repro at the top of this entry. `tests/refuse/lambda_deleg_field_call.tk`: an instance
delegate FIELD called bare, the third door's own repro, a FREE function of the identical
name answering on the base. `tests/refuse/lambda_prop_name.tk`: an instance PROPERTY read
bare. `tests/refuse/lambda_method_call.tk`: an instance METHOD called bare. `tests/refuse/
lambda_method_name.tk`: an instance METHOD named bare, with no call around it -- the
`N_IDENT` twin of the call one, `tk_lam_member`'s own method branch, which no earlier
fixture exercised. `tests/refuse/lambda_field_name_write.tk`: the WRITE twin of the first
fixture, `n = x;` on an instance field inside a lambda's own block body. `tests/
surface_lambda.tk`'s `member_check`: a lambda naming only a global with no colliding member
(`armGlobal`, unchanged), one naming a STATIC field and a STATIC method (`armStatic`), and
one naming a member CONST and a STATIC property (`armConstProp`) -- every one of the three
set beside an IDENTICALLY SPELLED global, proving the member wins. `member_deleg_check`,
kept apart because its own STATIC delegate field outlives the function's scope exactly as
`slot_roads_check`'s own `St.cb` does: a STATIC method's own lambda calling a STATIC
delegate field bare (`sarmDeleg`), also beside an identically spelled global.
`member_write_check` (new): `D70Member.armWrite` sets its own STATIC field `wfield` bare
(`=`), then bumps it bare (`+=`/`++`), each from inside a lambda the STATIC method builds;
the same-named global `wfield` stays exactly where it started, proving the member won on
both statement shapes and the global was never touched at all.

**Proof, the write door landed** (mc 0.15.23, macos/aarch64, base `origin/main` `bcee28f2`,
merged forward past D61/D62/D63/D65/D66/D69, this branch's own read-and-call door already on
top of it): `mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` -> **75 passed, 80 refused as expected, 0 failed** (74 refused on `bcee28f2`,
six refusals added across the whole PR, one of them this crumb's own
`lambda_field_name_write.tk`; 75 passed unmoved, `surface_lambda.tk` grown in place again);
`--dump-ast` byte-identical against a `build/teko` built from `bcee28f2` in a separate
worktree, for all **149** pre-existing `tests/*.tk` (75, `tests/parts/*.tk` excluded -- those
are `#include`-only, not fixtures) and `tests/refuse/*.tk` (74), each compiled with its
ORIGINAL `bcee28f2` source by both binaries -- the `tk_xt_put` addition above touches a side
table `--dump-ast` never prints, so it moves nothing here either; `sh scripts/bootstrap.sh
--os macos --arch aarch64` -> `FIXPOINT OK`; `sh scripts/check-docs.sh` -> `docs ok: 616
links, 45 fragments, 390 diagnostics, 80 refusals, 144 samples` (the diagnostics count is
UNMOVED: the write door's own two new sentences -- `a constant is not assigned or called`
and the `not captured`/`the property has no set` reuse -- are extensions of entries already
on the page, not new ones); `mc limits . --config mc.macos.toml` verdict `ok` with `grow` 0
on every row of both legs and `globals` **945** exactly where the base left them -- only the
size-of-surface-code rows moved (`nodes` 157320 -> 158004, `funcs` 3193 -> 3199, `lowered`
3175 -> 3181, `strings` 2138 -> 2139, `symbols` 6276 -> 6283, `ins` 217118 -> 218369); `mc pkg
hash .` `d980fe76b4325dd54284332968829d60be3374aaeda50019032f399e43c97e14` (base
`2773f2b5af7d1e2edc5120df5b1536c6cededd6365515a7c82f5858c17acb332`: `teko_deleg.tk` and
`teko_access.tk` are listed files (`mc.toml`), so the hash moves by design; `docs/` and
`tests/` are not listed and do not affect it).

### D71 · The mc canary: a pre-release is promoted by a file this repository writes (2026-09-15)
mc's M53 (`docs/specs/M53.md` § 6, its D14-D17) freezes the surface for 1.0.0 and asks one
thing of the language it compiles: **that a release be proved by teko before it is a release
at all.** The constraint the direction set is "no credential" in either direction — mc cannot
dispatch a workflow here (that needs a PAT) and this repository does not get mc's `release`
webhook (a foreign repository never does), so both halves PULL and the only thing that crosses
the boundary is a URL each side reads anonymously. This is teko's half.

**The protocol.** mc publishes every tag as a GitHub **pre-release**, assets and checksums
complete and identical to a release's — the flag is the one property of a published release a
later job can flip. `.github/workflows/mc-canary.yml` polls
`repos/minicompiler/mc/releases` every 15 minutes (mc's own number: its `promote` budgets a
quarter of an hour for this schedule to notice inside a 90-minute poll), takes the newest
pre-release with no verdict yet, runs the recipe against that toolchain, and commits with its
own `GITHUB_TOKEN`, at the ROOT of this repository's `canary` branch:

```
https://raw.githubusercontent.com/teko-org/teko-lang/canary/<version>.json
{"version":"0.17.0","status":"ok","run":"<the actions run>","utc":"2026-09-15T12:34:56Z"}
```

**Those four fields are the contract**, and nothing else is in the file: no commit sha (a
verdict is about an *mc version*, not a teko commit — that is also why it is a file and not a
commit status, whose subject would have to be a teko commit and the mapping invented), no
date beside `utc`, and `version` BARE, without its leading `v`. mc's `promote` polls that URL
with `curl` and no token, every 60 s for 90 minutes: `ok` runs `gh release edit --prerelease=false`,
`fail` leaves the pre-release standing, and a file that never appears is **neither** — a
timeout, advisory until mc 1.0.0 so a teko outage cannot hold an mc patch. `publish-to-registry`
is `needs: promote` on mc's side, so a pre-release never becomes the registry's newest row.

**The recipe is called, not copied.** `ngen.yml` gains ONE input, `mc_version`, which its
three `setup-mc` uses take instead of `MC_VERSION`; empty — a push, a pull request,
`release.yml` — is the pin, so nothing about the twelve jobs changes for anyone else. The
canary calls it with the candidate. What a candidate mc must pass is therefore exactly what a
merge into `main` must pass, by construction: five native legs, five fixpoint legs, `docs` and
the aggregator. The one part of the recipe `ngen.yml` does not hold is the standard library,
which mc counts in, so a `std` job runs `teko-org/teko-std`'s own linux/x86_64 leg against the
same candidate — the vendored `deps/teko` its `mc.lock` pins (never `mc pkg sync`: a canary
must not depend on the registry being up, and `mc build` rehashes the vendored tree against the
lock either way), then `scripts/fixtures.sh` over its fixtures. Nothing there writes to the
registry.

**`ok` needs every needed job green.** The `verdict` job is `if: always()` — a canary that goes
quiet on a red leg times mc out instead of answering it — and `cancelled` and `skipped` count
as `fail`, because neither is evidence that the candidate compiles teko and `promote` must not
read silence as consent. The branch **accumulates**, one file per mc version, and is never
force-pushed the way `site` is (D36): mc polls one file for up to 90 minutes and a file must
not vanish under a reader. One `concurrency` group for the whole workflow, uncancellable, is
what keeps two runs from racing the same push, and a `workflow_dispatch` naming a `version`
explicitly rewrites a verdict already on the branch — that is how a re-run corrects itself.

**What this asks of the repository, once:** `refs/heads/canary` in the `All Green` ruleset's
exclude list, beside `refs/heads/site`, which is there for the same reason — the ruleset covers
`~ALL` and requires a pull request, and `GITHUB_TOKEN` has no bypass. Without it the `verdict`
job's push is refused and every candidate times out into mc's advisory promotion, which is
exactly the pipeline mc has today.

**Proof.** `actionlint` clean beyond the `SC2016` information `ngen.yml`'s own summary step
already reports (a `printf` format in single quotes, which is what a format string is);
`sh scripts/check-docs.sh` → `docs ok: 599 links, 42 fragments, 389 diagnostics, 50 refusals,
141 samples`. No `.tk`, `.mc` or `teko.toml` byte moves in this crumb, so `--dump-ast` is
unchanged by inspection: nothing the parser reads was touched. **What is NOT yet proved, and
why:** the round trip. `workflow_dispatch` refuses a workflow that is not on the default branch
(`HTTP 404: workflow mc-canary.yml not found on the default branch`), so the proof dispatch —
`gh workflow run mc-canary.yml -f version=0.16.0`, a real release whose `ok` is a true statement
— is the first step after this lands; and `refs/heads/canary` is not yet on the `All Green`
ruleset's exclude list (the ruleset PUT is an owner action the session could not take), so
until it is, `verdict`'s push is refused and every candidate falls through to mc's advisory
timeout. Both are recorded here so the entry does not claim a run that has not happened; the
verifier of this crumb caught the first draft of this paragraph claiming exactly that.

**Amendment (2026-09-15).** Both open items above closed the same day: the mc session, an
admin of this repository, added `refs/heads/canary` to the `All Green` exclude list, and the
dispatch for 0.16.0 (run 34863283302) wrote `0.16.0.json` = `fail` (windows/x86_64, the direct
PE backend, the truthful verdict). The 0.16.1 and 0.17.0 candidates were judged `ok` in full
(runs 34877983682 and 34889251142). What did NOT work is the `schedule` trigger: zero cron runs
in the six hours after the workflow landed on `main`, so every candidate so far was judged by a
manual `workflow_dispatch`; mc's `promote` reads the file the same way either road writes it.

### D72 · The pin rises to mc 0.17.0, the release candidate that freezes mc's surface (2026-09-15)
mc 0.17.0 is the RC whose publication starts the surface freeze (mc `hooks.md` § 8: from here to
1.0.0 a rename or a removal of the hook surface ships only with an alias or a major). It was
judged by this repository's own canary before it was promoted (D71; run 34889251142, all fifteen
jobs green, `teko_std` included), and the whole local recipe is green on it, on `main`
`6bffa633` before a single file moved: `mc build . --config mc.macos.toml` clean;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **75 passed, 83 refused as expected,
0 failed**; `sh scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK` (`--dump-asm`
diff empty over 230438 lines); `sh scripts/check-docs.sh` → `docs ok: 615 links, 45 fragments,
390 diagnostics, 83 refusals, 144 samples`; `mc limits` verdict `ok`, no ceiling moved.

The move is the one D64 prescribed for a pin: `MC_VERSION` → `0.17.0`; `mc.toml`'s
`[package].mc` follows the pin (the registry's own validator runs 0.17.0 since mc-registry #33,
so the minimum this manifest states is the compiler that validates it) — **superseded by the
second amendment below: the minimum is the oldest mc of the frozen surface, 0.17.0, and does
not follow the pin**; the four places that
quote the pinned number as the CURRENT one (`CONTRIBUTING.md`, `docs/guide/00-getting-started.md`,
`docs/reference/build.md`'s staged path and message text, `.github/workflows/site.yml`'s comment)
follow; every mention of 0.16.0/0.16.1 that narrates a past measurement stays. Nothing in the
hook modules moves: `--dump-ast` of all **158** fixtures (75 under `tests/`, 83 under
`tests/refuse/`) is byte-identical between the compiler built by 0.16.1 over `main` and the
compiler built by 0.17.0 over this tree. `mc pkg hash .` on this tree is
`ef8f2df1e26e615cb0452ce7a436ffafbcf42710c4f3002e690919aca71dd287` (the manifest moved; the
modules did not).

Two facts recorded for the next raise. The registry refused teko 0.12.4 while its sandbox ran
0.16.0 (`teko 0.12.4 needs mc >= 0.16.1 (this is mc 0.16.0): upgrade the compiler`, job 64):
`[package].mc` doing its job, and the reason the minimum and the validator's compiler have to
move together. And `mc tool install` exists since mc 0.15.21 (M48 C3): `tekoc` as an
installable tool (`[project] kind = "exe"`, `[package].bin`, `[[permission]]`) is a crumb of its
own, the last row of `docs/specs/roadmap-1.0.md` § What teko owes.

**Amendment (2026-09-15) — the pin rises to 0.17.2.** mc 0.17.1 (the four `mc tool`
defects, `tekoc-tool.md` § 10) and 0.17.2 (`mc pkg sync` with a vendored `deps/x` at another
version: `deps/x is B, [deps] wants A: update the checkout or remove deps/x`, found by the std
lockstep) are patches under the same frozen surface. The canary judged 0.17.1 `ok` (run
34921240874); 0.17.2 was promoted by mc's timeout while this repository's poller was not
watching, and its verdict was written afterwards by a manual dispatch. The whole local recipe is
green on 0.17.2 over `a1cf0b52` (94 passed, 119 refused, 0 failed; `FIXPOINT OK`; docs ok) and
`--dump-ast` is byte-identical between the compiler built by 0.17.0 and by 0.17.2 on every
fixture. The move is D64's: `MC_VERSION` to 0.17.2 and the four current-pin quotes with it;
`[package].mc` moved to 0.17.2 too on that day and was put back to 0.17.0 by the second
amendment (the minimum does not follow the pin); nothing in the modules moves.

**Second amendment (2026-09-15) — the minimum is not the pin.** Two releases were refused by
the registry's validator for the same reason: its sandbox ran the mc one patch behind the
pin (`teko 0.12.4 needs mc >= 0.16.1 (this is mc 0.16.0)`, job 64; `teko 0.15.0 needs mc >=
0.17.2 (this is mc 0.17.0)`, job 80). A minimum that follows the pin turns every mc patch into
a registry outage until the sandbox catches up, for nothing: the hooks build on every 0.17.x,
because the surface is frozen since 0.17.0. The rule is now: `[package].mc` names the OLDEST
mc of the frozen surface the hooks build on — `0.17.0` — and `MC_VERSION`, the pin CI tests,
moves freely above it. The two move together again only when a hook the modules use is born
in a later mc, which the freeze rules out before 1.0.0. Measured before the change: the same
tree builds and passes the whole recipe on 0.17.0 and on 0.17.2 with byte-identical
`--dump-ast` (D72's first amendment); `mc pkg hash .` moves (the manifest's bytes are part of
it) and nothing else does.

**Third amendment (2026-09-15) — the pin rises to 0.17.5.** mc 0.17.3 and 0.17.4 changed the
arity of `cmp_cond`, a public symbol `teko_typeof.tk` calls (`cmp_cond(op)` became
`cmp_cond(op, unsigned)` with contract v6's unsigned compares); the canary refused both
(`canary/0.17.3.json`, `canary/0.17.4.json`: `wrong number of arguments`), mc kept them
pre-releases, restored `cmp_cond(op)` in 0.17.5 (mc #95) and made its own `check-freeze`
record arity from then on. The canary judged 0.17.5 `ok` (run 34961800863, verdict written
six minutes after the publish) and mc promoted the release on that verdict. The whole local
recipe is green on 0.17.5 over `8d95c1ff` (102 passed, 130 refused, 0 failed; `FIXPOINT OK`;
docs ok), and `--dump-ast` is byte-identical between the compiler built by 0.17.2 and by
0.17.5 on all 232 fixtures. The move is D64's again: `MC_VERSION` to 0.17.5 and the
current-pin quotes with it (`site.yml`, `CONTRIBUTING.md`, `build.md`, `00-getting-started.md`;
`docs/specs/tekoc-tool.md` keeps its measurements against 0.17.0 and says so — the pin of the
day it was measured on, not a current-pin quote);
`[package].mc` stays 0.17.0 (the second amendment); nothing in the modules moves. Contract v6
compares `u64`/`uptr` unsigned from 0.17.3 on — the pages that recorded the signed compare
(`small-ints.md`, `lib/limbs.tk`) now say which side of the fix each pin is on, and the 32-bit
limbs stay, because the minimum still admits 0.17.0.

**Fourth amendment (2026-09-15) — the pin rises to 1.0.0.** mc 1.0.0 is the release that
promises the frozen surface for good: the 423 entries of mc's `tests/golden/surface.txt`, any
later removal or change of one being a MAJOR (mc's `docs/reference/hooks.md` § 8). Same
tarballs, same layout, same canary contract. The canary judged it `ok` (run 34965435282,
verdict written seven minutes after the publish, dispatched by this repository's poller). The
whole local recipe is green on 1.0.0 over `12bc95f9` (112 passed, 133 refused, 0 failed;
`FIXPOINT OK`; docs ok), and `--dump-ast` is byte-identical between the compiler built by
0.17.5 and by 1.0.0 on all 245 fixtures. The move is D64's: `MC_VERSION` to 1.0.0 and the
current-pin quotes with it (`site.yml`, `CONTRIBUTING.md`, `build.md`, `00-getting-started.md`);
`[package].mc` stays 0.17.0 (the second amendment: the oldest mc of the frozen surface, which
1.0.0 now guarantees); nothing in the modules moves. What 1.0.0 changes for this repository is
the owner's: CLAUDE.md's "v1.0.0 ships only together with mc 1.0.0" is now satisfiable, and
that tag is the owner's to cut, not this log's.

### D74 · A primitive may be a machine type; the closed list stays closed (C3, 2026-09-14)
> A type teko registers with `type_new` carries whatever its representation needs to **move**: a
> derived machine table per instruction set, deriving from the table in effect and delegating
> everything else through a pristine copy. That is not an intrinsic and not a fork of `mc`'s core
> — `git diff src/` for it is empty, which is the criterion
> [`mc`'s own guide](https://github.com/minicompiler/mc/blob/main/docs/guide/96-a-new-primitive.md)
> sets — and the "zero new intrinsics" law does not veto it. What the law still forbids is
> unchanged and is the whole of it: **no operation of the language surface may be an intrinsic.**
> Addition, rounding, formatting, parsing and every conversion have surface code in `lib/`, and
> `mc limits`' `intrin` row does not move. A primitive that cannot be moved without a **new
> instruction encoding** is still allowed — the encoding is the module's, beside the table — and
> a primitive that cannot be expressed without changing `mc`'s core is a fork and halts, as D2 says.

That is `docs/specs/decimal.md` § 14, proposed when the page was written and entering the log
here, with C3 built on it. `docs/specs/surface.md` § "What is 'magic'" carries the amendment;
D21's own sentence is untouched, and the measurement below is what makes the claim checkable.

**D73 is reserved** by `docs/specs/tekoc-tool.md`'s draft, merged on `main` in #725; this entry
is D74 so the two do not collide.

#### The five rulings this crumb was dispatched with, and what each became

1. **teko writes its OWN `teko_wide.tk`, keyed by an id SET.** Modelled on `mc`'s bundled
   `lib/i128.mc` but never on `t == ty_decimal`: `tk_wide_add(ty, ldsym, stsym)` and
   `tk_wide_is(ty)` are the shape of `iw_is` widened from two ids to a set of four
   (`TK_MAXWIDE`, `teko: too many wide types`). N3's `Guid`, N5's `DateTimeOffset` and N6's
   `i128` register their ids into it and get the whole machine with **no machine work at all**.
   `<i128>` is deliberately NOT included: it reserves the words `i128`/`u128` program-wide,
   which is N6's surface ahead of N6's crumb.

   The file split that fell out: the machine is `teko_wide.tk` and is generic; `decimal` is
   `teko_decimal.tk` and is its first client. One file would have been ~560 lines and would have
   mixed "how sixteen bytes move" with "what a decimal is", and the second module is what makes
   the next three crumbs one line each.

2. **Movement is BY ADDRESS**, as § 1 mandates, and the justification was re-measured rather
   than inherited: `mc`'s own `lib/i128.mc` proves a register pair IS writable from outside
   `src/`, on all three tables, so the choice is not forced. It is made anyway, because a
   register pair is three ABI implementations (SysV's `rax:rdx`, Win64's hidden reference,
   AAPCS64's even-numbered pair) and an address is **one rule every machine already
   implements**. Every handler either delegates in full or rewrites one depth into a pointer and
   then delegates in full, so the argument counting, the stack area, the shadow space and the
   float/integer split all stay where `mc` and `<float>` wrote them. teko writes no ABI, and the
   five-leg matrix is what proves it (`ngen` and `fixpoint`, all five, green on the first push).

3. **The `mc limits` gate.** `mc limits . --config mc.macos.toml`, tolerance 1.0, the high-water
   column, base `9ded80ec` → this crumb: `intrin` **8 → 8 (unmoved, the law's own row)**,
   `passes` **15 → 15 (unmoved)**, `syntax` **15 → 15 (unmoved** — `syntax_lit` is not counted
   there, and no `syntax_expr("decimal")`/`syntax_stmt("decimal")` is registered: those are what
   a RECEIVER form needs and C5 is where they are spent**)**, `types` **13 → 14 (+1, the
   `decimal` row)**, `alias` **20 → 21 (+1**, a side effect of `type_new` reserving the word,
   not a second registration**)**. Verdict `ok` on both sides; **no new `grew` row**. The three
   derived tables add **no row at all** — a derived table shadows the name it registers and
   consumes no `machines` slot. `docs/specs/decimal.md` § 10 carries the same table; the `heap`
   column is not read (D-entries do not quote it).

4. **The field and the array-element roads.** The ruling allowed them to stay explicit
   (`tk_dec_ld`/`tk_dec_st`) unless they fell out of the machine handlers for free, and asked
   for a measurement. **They did not fall out, and leaving them would have been a silent wrong
   answer.** Measured on this branch before the fix: `decimal arr[3]; arr[1] = 1.5m;` compiled
   and emitted `str x10, [x9]` — teko lowers **every** indirect access to a raw `ldW`/`stW`
   picked by WIDTH (`tk_ldn`/`tk_stn`, `teko_struct.tk`, the one table a field, an array
   element, a `ref`/`out` pointee, a closure capture and a property all go through), and sixteen
   is a width no machine has, so eight bytes of the literal's ADDRESS were written where sixteen
   bytes of its value belonged.

   The root cause is that one table, so that is where it is fixed: a wide type carries its own
   two lowering symbols in the set and `tk_ldn`/`tk_stn` answer with them. `MTASK_LOAD` and
   `MTASK_STORE` are registered as well, as the backstop for any road that reaches the machine
   directly. Measured after: a fixed global array, a fixed local array, a `decimal[]` of heap, a
   `class` field and a `struct` field all move sixteen bytes and leave their neighbour fields
   intact. `&a[i]` is refused for every element type in teko and always was, which is why the
   explicit helpers stay in `lib/decimal.tk` and are what the fixture uses over a raw `ptr`.

5. **This entry is D74**, after D72, with D73 reserved (above).

#### What is built

`teko_wide.tk` — the wide id set, one sixteen-byte frame slot per wide depth, and three tables
derived over `arm64`, `x86_64` and `x86_64-win` (Windows on aarch64 uses the `arm64` table, so
three cover all five legs), copied from `machine_tab(...)` **after** `tk_float_init()` has run so
a float operation still reaches `<float>`'s handler through the pristine copy. Seventeen slots:
`PROLOGUE`, `PARAM`, `PARAM_REG`, `LOAD`, `STORE`, `LOCAL_LOAD`, `LOCAL_STORE`, `GLOBAL_LOAD`,
`GLOBAL_STORE`, `CALL`, `CALLP`, `RET`, and the five guards `BIN`/`CMP`/`UN`/`CAST`/`CONST`.
**No `MTASK_ENCODE`, no `INS_SIZE`, no `DUMP`, no `RELOC_KIND`**: this module emits no opcode the
base machine did not already encode, and `--dump-asm` needs no new mnemonic.

The per-instruction-set part is **four answers**, written by each prologue: the pristine table in
force, the frame base register, the transfer and address scratch registers, and how one word is
loaded and stored. Every handler is written once over them. The address of a frame slot is
`MTASK_LOCAL_ADDR`'s and the address of a global is `MTASK_SYM_ADDR`'s, both delegated, so no
`adrp`, no `lea [rip + disp32]` and no relocation shape appears in this module.

The return buffer is `tk_dec_retbuf`, a global of the **program**, declared in `lib/decimal.tk`.
It could not be a module-private global of the compiler (a machine emits code for the program,
not for itself) and it could not be a frame slot of the returning function (gone at the
epilogue; reading it after the return is a red-zone read this compiler is not entitled to on
AArch64 or on Win64). The machine finds it with `global_find` + `glb_sym`, and a program that
returns a `decimal` without the include is told so by name. One buffer is safe under recursion
and under nesting because the call site copies out **immediately** after the branch, before any
other instruction the handler emits — a rule of one handler, in one place, with a recursive
fixture and a two-wide-returns-in-one-expression fixture as its oracles.

`teko_decimal.tk` — `type_new("decimal", 16, 16, TK_WIDE)`, `tk_wide_add`, and the literal
`<digits>[.<digits>][e[+|-]<digits>](m|M)` through `syntax_lit`, becoming a module-private
global with an `N_BLOB` initializer and an `N_IDENT` naming it (the `$tk_dec_N` gensym carries a
`$` the lexer never forms into an identifier). The digits go into four 32-bit limbs most
significant first; a carry out of the top limb is remembered, so `1e40m` is refused rather than
wrapped. Two refusals of its own: `teko: decimal literal out of range` and
`teko: a decimal carries at most 28 decimal places`.

**The registration order is load-bearing, in both directions**, and `teko_init()` says so at
both call sites: `tk_dec_init()` runs **before** `tk_float_init()`, because `syntax_lit` handlers
run in registration order and the first non-zero node wins — registered the other way round,
`<float>`'s `fl_lit` reads `1.5` and stops before the `m`, and `1.5m` silently becomes an `f64`
followed by a stray identifier. `tk_wide_init()` runs **after** it, because a derived table has
to copy the table `<float>` left in place and not the one underneath it.
`tests/primitives_decimal_order.tk` puts `0.5m` and `0.5` in one program and is the oracle for
the first; the five-leg matrix is the oracle for the second. One consequence worth recording:
`decimal` now takes the type id `f64` used to have, and every id after it shifts by one. Nothing
in this repository keys on an id's numeric value, and all 169 fixtures agree.

#### What refuses, and why almost nothing new was written

`decimal` is registered with `tk_prim_type` — the primitive-member mechanism `TimeSpan` and
`DateTime` already ride — with an **empty** member table. A primitive with no rows converts to
nothing but itself, claims no operator and answers every member by name, so one registration
buys the whole of § 4 with wordings this compiler already had: `` no operator `+` takes these
operands `` for the arithmetic and the six comparisons, `a value of type decimal does not
convert to i64` and its mirror, `unknown member of decimal` and its static twin,
`const requires a constant expression` for `const decimal`, and
`a case label must be a constant expression` for a `decimal` `case`.

**Two refusals are new.** `teko: a decimal does not cast yet` — `tk_prim_cast_check` names the
type's reader and its constructor, and `decimal` has neither yet, so a primitive registered with
a null reader gets the short form instead of a message pointing at members that do not exist.
`` teko: an `extern` takes no decimal `` — refused at the DECLARATION (`tk_ov_extern_wide`,
`teko_over.tk`, on the top-level walk that already visits every `N_EXTERN`), because the
sixteen-byte convention is teko's own and no C ABI shares it: without this the call would
compile, link and hand libc an address where sixteen bytes were expected.

**Five more are guards on the machine** and are unreachable from the surface —
`arithmetic`/`a comparison`/`a unary operator`/`a cast`/`a constant`
`is not defined on a sixteen-byte value yet`, plus
`teko: a sixteen-byte value in an allocatable register` for `MTASK_PARAM_REG` under `--opt=1`.
They exist so that a hole in the reasoning above is a message rather than eight bytes moved
where sixteen were meant. All of them are in `docs/reference/diagnostics.md`.

#### The gate, as run

`mc build . --config mc.macos.toml` clean on mc 0.17.0;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **78 passed, 91 refused as expected,
0 failed** (76 + 2 and 84 + 7);
`sh scripts/bootstrap.sh --os macos --arch aarch64` → **FIXPOINT OK** (`--dump-asm` diff empty
over 234592 lines); `sh scripts/check-docs.sh` → `docs ok`;
`mc limits` per ruling 3. `--dump-ast` of all **169** fixtures against a compiler built from
`origin/main` (`9ded80ec`): **160 byte-identical, 9 differing, and the 9 are this crumb's own
new fixtures** — which the base compiler cannot compile at all, since it has no `decimal`.

### D75 · `Guid`, the second wide type: what C1 and C3 owed a sixteen-byte value (N3, 2026-09-14)
> A `TK_WIDE` type is carried **by the wide set and by nothing named after `decimal`**. N3 is the
> proof: `Guid` registers one id, one load symbol, one store symbol and one return buffer, and the
> three derived machine tables of `teko_wide.tk` move its sixteen bytes with **no machine work at
> all**. Where that turned out not to be true — C1's member lowering, the return buffer's name and
> the `ref`/`out` pointee road — the fix is in the SHARED code and serves every wide type at once,
> never in a branch that reads the type's name.

`Guid` is `docs/specs/guid.md` whole but § 5, which is N9. It is the second `TK_WIDE` type and the
first that is not `decimal`, which is exactly why the order puts it here: the machine module is
proved general while it is still small (D74, `22e8d5d1`).

#### The seven rulings this crumb was dispatched with, and what each became

1. **Representation: RFC 4122 text order, big-endian, byte 0 = `time_low`.** `type_new("Guid", 16,
   16, TK_WIDE)`, § 1's table exactly, so `ToString` is a straight walk from byte 0 to byte 15 and
   `Parse` its inverse. It is **not** C#'s in-memory order, whose first eight bytes are
   byte-swapped on a little-endian host. The difference is invisible to `ToString`, `Parse`, `==`
   and `!=`, and visible in the **ordering**: teko compares the sixteen bytes **unsigned, left to
   right**, C# compares its first field as a signed `int` and the next two as signed `short`s.
   That is a **recorded divergence** (§ 1, § 6, `docs/reference/types.md`), not a gap: matching
   C# would mean matching the byte swap too, and then `ToString` stops being a walk.
   `tests/primitives_guid_bytes.tk` pins the layout with an oracle and not a comment, and
   `tests/primitives_guid.tk` pins the ordering with values that only pass under it.

2. **The return buffer is per TYPE — a fourth column of the wide set.** `tw_retbuf_name()`
   answered `"tk_dec_retbuf"` for the whole program (`teko_wide.tk`, C3), so a function returning
   a `Guid` was told to `include "decimal.tk"`. It is `tk_wide_add(ty, ldsym, stsym, retbuf)` now,
   read by `tw_ret` through `tw_retbuf_of(ty)`, and the include named in the refusal comes from
   the primitive table (`tk_prim_inc_of`, `teko_prim.tk`): `lib/guid.tk` declares
   `Guid tk_guid_retbuf;` beside its own `tk_guid_ld`/`tk_guid_st`, and a program that never
   mentions `decimal` is never pointed at `decimal.tk`. The alternative — one shared buffer — is
   one file every wide-returning program has to include whatever it uses.

3. **`ref`/`out` of a wide value is inside N3**, because `Guid.TryParse(s, out g)` needs it and
   because it is C3's own hole. Measured on `22e8d5d1`: `void bump(ref decimal d) { d = 7m; }`
   was refused ``teko: a value of type decimal does not convert to uptr`` at
   `lib/rt.tk:385` — a line no source wrote.

   The root cause is one node. A `ref`/`out` parameter's name means the POINTEE everywhere the
   scope records it (`tk_ty_scope_params`), and the deref pass then writes a load and a store
   over it in which the very same name means the **address** — and nothing said so. Harmless
   while that load was `ld64`, which `decl_find` does not know and `tk_rc_call_args` skips;
   not harmless for a wide pointee, whose indirect access is a declared function of the program
   (`tk_dec_ld(ptr p)`, `tk_ldn`/`tk_stn`, D74). One tag where the node is built
   (`tk_ref_slot_addr`, `teko_ref.tk`) fixes it for every pointee at once, and the statement's
   own line and file are put in force there too, which is where the misattributed line was lost.
   `tests/primitives_decimal_out.tk` proves it for `decimal` as well, at exit 42.

4. **`tk_prim_cast_check` gains a BUILDER clause beside the reader.** Each is a COLUMN of
   `tk_prim_type` and each carries its VERB, because the pair is not the same sentence for every
   type: `TimeSpan` is read by `.Ticks` and built by `new TimeSpan(...)`, while a `Guid` is not
   constructed at all. The refusal reads
   `` teko: a Guid does not cast; `.ToString()` writes it and `Guid.Parse(s)` reads it ``
   (§ 2), and every wording that existed is **byte-identical** — `decimal` keeps its `yet` form,
   which is what a primitive registered with neither clause gets.

5. **The include flip is the owner's.** `#include "guid.tk"` stays a build-time step the surface
   names, refused per type through the primitive table
   (`teko: Guid needs #include guid.tk before it is used`,
   `teko: include "guid.tk" before returning a sixteen-byte value`). Whether `guid.tk`,
   `time.tk`, `decimal.tk` and `string.tk` fold into `lib/rt.tk` is one decision for all four
   (§ 11), and it is not taken here.

6. **`Guid.NewGuid` is N9**, a `TK_PMSOON` row: `teko: Guid.NewGuid is not taught yet`. A
   version-4 `Guid` is sixteen bytes of **cryptographic** randomness and there is no fallback —
   a value built from a counter, a clock or an address would compile and two processes would
   collide. `tests/refuse/guid_newguid.tk` is its oracle.

7. **The members are § 4's list**: `Guid.Empty`, `Guid.Parse(str)`, `Guid.TryParse(str, out Guid)`,
   `.ToString()`, `.ToString(str fmt)` (`"D"`/`"N"`, either case, anything else a run-time panic),
   `.CompareTo(Guid)`, `.Equals(Guid)`, `.IsEmpty`. `Parse` takes the 36-character hyphenated form
   and the 32-character bare one, either case, and panics on everything else
   (`teko: the string is not a Guid`, exit 70). `"B"`, `"P"` and `"X"` are three more spellings of
   the same sixteen bytes and are not taught (§ 6) — so `Parse` accepts two formats and not C#'s
   five, which is this crumb's one narrowing of C# and is written into
   `docs/reference/not-yet.md`.

#### What C1 owed a WIDE receiver, measured

`docs/specs/guid.md` § 7 said `teko_prim.tk` grows "nothing". **That was wrong**, and the page is
corrected. The lowering crossed three things through a cast that no sixteen-byte type can wear —
each of them `tw_cast`'s own `die` — and all three are fixed in the shared code:

- `tk_prim_emit` crossed the receiver as `tk_prim_raw(recv)`, i.e. `(i64) recv`. A WIDE receiver
  crosses as **itself**: `g.ToString()` is `tk_guid_tostring(g)`, and the lowering symbol declares
  the wide type, so the sixteen bytes travel by address exactly as `tw_args` sends any argument.
- `tk_prim_ret` wrapped a wide-returning call in `tk_cast`. A wide result crosses **uncast**: the
  call's own declared return type is already what every oracle reads.
- `tk_prim_conv` cast a wide argument. Same rule, same reason.

A fourth change is a split and not a behaviour: `tk_prim_static_m` takes the member name already
read, so a module that owns a primitive may hand-parse ONE static and leave every other one to the
table. `Guid.TryParse(s, out g)` is that static — `out <name>` is no column the `pmr_*` rows have —
and it is parsed exactly as `Color.TryParse` is (`teko_enum.tk`), reading the operand through
`tk_ref_addr` rather than through the tagging `out` handler.

#### What is built

`teko_guid.tk` (new, 37th module): one `type_new`, one `tk_wide_add`, one `tk_prim_type`, one
`syntax_expr`/`syntax_stmt` pair, nine member rows, six operator rows, the `NewGuid` `TK_PMSOON`
row and the hand-parsed `TryParse`. `lib/guid.tk` (new): the copy helpers, the return buffer,
`Empty`/`IsEmpty`, the hex scanner and formatter, `Parse`/`TryParse`/`ToString`, and
`tk_guid_cmp` with its six spellings. Nothing else: `teko_wide.tk` grew one column, `teko_ref.tk`
one tag, `teko_prim.tk` the four above, `teko.tk` an `#include` and one `_init()` call,
`lib/rt.tk`, `core_teko.mc` and `user.mc` nothing at all.

#### The gate, as run

`mc build . --config mc.macos.toml` clean on mc 0.17.0;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **84 passed, 102 refused as expected,
0 failed** (79 + 5 and 91 + 11);
`sh scripts/bootstrap.sh --os macos --arch aarch64` → **FIXPOINT OK**;
`sh scripts/check-docs.sh` → `docs ok: 654 links, 53 fragments, 403 diagnostics, 102 refusals,
148 samples`. `--dump-ast` of all **170** fixtures that exist on `22e8d5d1`, against a compiler
built from that commit: **170 byte-identical, 0 differing** — the four `teko_prim.tk` hunks move
nothing, because no program that compiled before has a wide primitive member. The five `Guid`
fixtures and `primitives_decimal_out.tk` also compile on all four non-host targets
(`linux/x86_64`, `linux/aarch64`, `windows/x86_64`, `windows/aarch64`), which is the SysV, Win64
and AAPCS64 tables reached; the five-leg matrix is the oracle that runs them.

`mc limits . --config mc.macos.toml`, tolerance 1.0, base `22e8d5d1` → this crumb: `intrin`
**8 → 8 (unmoved, the law's own row)**, `passes` **15 → 15 (unmoved)**, `syntax` **15 → 16 (+1**,
the table is keyed by NAME, so `syntax_expr("Guid")` and `syntax_stmt("Guid")` are one row
between them**)**, `types` **14 → 15 (+1)**, `alias` **21 → 22 (+1**, and
`docs/specs/guid.md` § 8's "alias unmoved" was wrong: `type_new` reserves the word in the very
table `type_alias` uses, so the two move together and always have — the page is corrected**)**.
Verdict `ok` on both sides; **no new `grew` row**, and the three derived tables still add none.

**The verifier of D75, and the `out` of `TryParse`.** The hand-written parser handed
`tk_guid_tryparse` a raw address and checked the pointee's type only when parse time could
answer it: a GLOBAL (`pty` -1) failed OPEN and sixteen bytes went into an eight-byte `i64`
global, and an `out`/`ref` PARAMETER as the target would have been `&name`, the callee's own
slot. Closed at the root: the argument is TAGGED (`tk_rfarg_tag(oe, TK_RP_OUT, pty)`) and
`tk_guid_tryparse` declares `out Guid o`, so teko_ref.tk's own pass judges it exactly as any
`out` argument — the global's declared type, the repass of a parameter as the caller's slot.
Measured: local, global and parameter targets write the value (exit 42);
`out victim` with `i64 victim;` refuses `teko: a value of type i64 does not convert to Guid`
(`tests/refuse/guid_tryparse_out_global.tk`).

### D76 · `DateTimeOffset`, the third wide type: the instant and its offset in sixteen bytes (N5, 2026-09-14)
> A UTC instant, `+0`, and its offset in signed minutes, `+8`, is the third `TK_WIDE` type
> and the first whose own members mix TWO other primitives (`DateTime`, `TimeSpan`) rather
> than reading raw bytes alone. It rides `teko_wide.tk` (D74) and the mechanism D75
> generalised — the reader/builder cast clauses, the per-type return buffer, the wide
> `ref`/`out` road — with no change to any of the three, which is the proof the machine
> generalises past its second client and not only its first. Where the shared code still had
> a hole — a stale include-quoting convention, a `new T()` that would crash a wide type's own
> codegen guard — the fix is in the SHARED code, for every wide type at once, never in a
> branch that reads this type's name.

`DateTimeOffset` is `docs/specs/datetime-extras.md`'s own N5, the crumb it named "still
open" — depended on `docs/specs/datetime.md`'s C2 (`DateTime`/`TimeSpan`, D40/D41) and
`docs/specs/decimal.md`'s C3 (`teko_wide.tk`, D74), both live by the time this crumb ran,
and it registers LAST in `tk_time_init()` so its own columns can name `DateTime`'s and
`TimeSpan`'s live ids without a `tk_prim_late` indirection.

#### The seven rulings this crumb was dispatched with, and what each became

1. **One two-argument constructor only**, `new DateTimeOffset(DateTime, TimeSpan)`. C#'s
   `(i64 ticks, TimeSpan)` overload is NOT taught: `tk_prim_pick` (`teko_prim.tk`) chooses a
   `"new"` row by ARGUMENT COUNT alone, and the one row this table carries is of arity 2, so
   a second row of the same arity was never going to be told apart from it. Measured, and
   pinned rather than special-cased: `new DateTimeOffset(621355968000000000,
   TimeSpan.Zero)` reaches the row's own first-position check and is refused
   `teko: a value of type i64 does not convert to DateTime` — an ORDINARY argument mismatch,
   not a row of its own (`tests/refuse/dto_ticks_ctor.tk`). `new DateTimeOffset(new
   DateTime(t), ts)` is the written form, recorded in `not-yet.md`.

2. **Text stays in N5.** `ToString()`/`ToString("o")`/`ToString("s")` and
   `Parse`/`TryParse` of BOTH forms — the "o" 33-character round-trip form with its offset
   suffix, and the "s" 19-character sortable one, which `Parse` reads as UTC, offset zero,
   since teko has no time-zone database to read a bare wall-clock string against. Every
   format outside those two panics by name at run time, exit 70
   (`teko: the DateTimeOffset format is not taught`, `teko: the string is not a
   DateTimeOffset`), the same two-panic shape `Guid`'s own text already has. The amendment is
   recorded on `docs/specs/datetime-extras.md` itself: its own § 2/§ 4 wording —
   `` `.DayNumber` reads it `` and "no primitive here has a `str` member yet" — describes
   `DateOnly`, not a blanket claim over every type this page designs, and was easy to misread
   as one; narrowed where it appears.

3. **`new T()` on a WIDE type is refused by name, one guard in the shared code, for
   `decimal`, `Guid` and `DateTimeOffset` at once.** `tk_prim_new`'s empty-argument branch
   wrote `tk_cast(ty, tk_int(0))` unconditionally — harmless for eight bytes (`TimeSpan.Zero`
   is exactly that cast) and a codegen `die` for sixteen: `tw_cast` (`teko_wide.tk`) has no
   zero BIT PATTERN a wide cast could write, since a wide value moves by address and never
   through a register. `decimal` and `Guid` never reached it before this crumb — neither
   registers a `"new"` row of ANY arity, so `tk_prim_new`'s earlier `ri < 0` check refused
   `new decimal()`/`new Guid()` first, with the generic "this primitive has no constructor" —
   but `DateTimeOffset` DOES register one, of arity 2, so its own zero-argument call reached
   the crash. The guard moved AHEAD of that check and reads `tk_wide_is(ty)` first: every
   wide type answers with its own named zero instead — `` `new decimal()` is not taught;
   write `0m` ``, `` `new Guid()` is not taught; write `Guid.Empty` ``, `` `new
   DateTimeOffset()` is not taught; write `DateTimeOffset.MinValue` `` — read from a FOURTH
   column of `tk_prim_type` (`pt_zero`), one wording template, one guard, three spellings.
   `decimal`'s and `Guid`'s own wordings CHANGE (a strictly better answer, naming the type's
   own zero instead of a generic "no constructor"), untested by any fixture until now since
   neither had one; `docs/reference/diagnostics.md`'s own catalogue records the change and
   the reason.

4. **The include column is bare everywhere; one place quotes it.** `teko_time.tk` baked its
   own quotes into `tk_time_include()` (`return "\"time.tk\"";`) while `teko_guid.tk` passed
   the name bare (`"guid.tk"`), so `tk_prim_need_include_of` (`teko_prim.tk`) printed a
   correctly-quoted message for one and an unquoted one for the other, and `tw_ret`
   (`teko_wide.tk`) — which adds its OWN quotes around whatever `tk_prim_inc_of` answers —
   would have printed `include ""time.tk""` for a wide type registered under the baked-quote
   column, which `DateTimeOffset` is the first one of. Fixed at the root, once: the column
   (`pt_inc`, every `tk_prim_type` call) carries the bare name everywhere now, and a new
   `tk_prim_inc_q` is the ONE reader both `tk_prim_need_include_of` and `tw_ret` ask for the
   quoted form. `tests/refuse/guid_no_include.tk`'s own wording moves from `teko: Guid needs
   #include guid.tk before it is used` to `` teko: Guid needs #include "guid.tk" before it is
   used `` — measured, and the only wording this repository ships that the fix moves at all,
   since no fixture on `dcbd88c5` yet returned a `DateTimeOffset` without its own include.

5. **`TK_MAXPRIMO` rises from 48 to 64.** Measured before this crumb: 41 operator rows in
   use against a cap of 48 (`tk_prim_op` calls, summed over `teko_time.tk`/`teko_guid.tk`);
   N5's own nine rows (six comparisons, `o + ts`, `o - ts`, `o - o`) would have crossed it.
   `docs/reference/diagnostics.md`'s own capacity table had drifted to a stale `32` for this
   row and a stale `96` for `TK_MAXPRIMM`'s (160, unmoved: measured usage after this crumb is
   142 member rows, comfortably under) since before D74 — both corrected here, not only the
   row this crumb needed raised.

6. **The code lives in `teko_time.tk` and `lib/time.tk`; no new module.** A `DateTimeOffset`
   section registered LAST in `tk_time_init()`, so `tk_ty_datetime`/`tk_ty_timespan` are live
   ids where its constructor and operator columns name them; `lib/time.tk` carries
   `tk_dto_retbuf`/`tk_dto_ld`/`tk_dto_st` beside `tk_dec_retbuf`/`tk_guid_retbuf`'s own
   shape, and every reader, the offset validator, the Unix conversions, `ToOffset`, the
   arithmetic (reusing `tk_dt_add`/`tk_dt_sub_ts`/`tk_dt_sub` directly, an instant IS a
   `DateTime`), the comparisons (reusing `tk_dt_eq` … `tk_dt_cmp` directly, the same reuse
   `DateOnly`/`TimeOnly` already made of `tk_ts_*`) and the text. `mc.toml`'s
   `[package].files` needed no new entry — `sh scripts/check-docs.sh`'s own manifest check is
   the proof.

7. **`DateTimeOffset.Now`/`UtcNow` are C6-class externs**, a `TK_PMSOON` row apiece, refused
   `teko: DateTimeOffset.Now is not taught yet` — the same wall clock `DateTime.Now` is
   already blocked on (the owner's ruling of 2026-09-08). Nothing new is asked of the `mc`
   channel: no member on this page needs `mc` to change.

#### What C1/C3/D75 owed a value that reads TWO other primitives, measured

Nothing. `tk_prim_conv`/`tk_prim_ret`/`tk_prim_emit` already answer "does this column name a
wide type, or a primitive, or neither" by the COLUMN's own type and not by the ROW's, so a
constructor whose two positions are `DateTime` (a primitive) and `TimeSpan` (a primitive) and
whose OWN type is wide crosses each argument correctly with no line changed: `tk_prim_member2`
(D41, written for `new DateTime(ticks, kind)`) already took two DIFFERING column types, and
D75's wide-receiver/wide-argument split already took a wide TYPE regardless of which column
carried it. The two mechanisms had never been exercised TOGETHER on one row before this crumb,
and the measurement is that they compose with no seam: `tests/primitives_dto.tk`'s own
constructor call is the oracle.

#### What is built

`teko_prim.tk` — `TK_MAXPRIMO` 48 → 64; a fourth `pt_zero` column on `tk_prim_type` (six
call sites updated: four unused-`0` for `TimeSpan`/`DateTime`/`TimeOnly`/`DateOnly`, `"0m"`
for `decimal`, `"Guid.Empty"` for `Guid`); the wide `new T()` guard in `tk_prim_new`, moved
ahead of the "no constructor" check; `tk_prim_inc_q`, the one quoting point
`tk_prim_need_include_of` now reads. `teko_wide.tk` — `tw_ret`'s own refusal reads the include
through `tk_prim_inc_q` instead of quoting it by hand. `teko_time.tk` — the `DateTimeOffset`
section: `tk_dto_statics`/`_ctors`/`_members`/`_operators`, the hand-parsed
`tk_dto_tryparse_expr` (`Guid.TryParse`'s own shape, D75), `tk_dto_expr`, and the
registration at the end of `tk_time_init()`; `tk_time_include()` unbaked. `teko_decimal.tk`,
`teko_guid.tk` — one argument each, the type's own zero spelling. `lib/time.tk` — the
`DateTimeOffset` section: the return buffer, the two copy helpers, the two extractors, the
offset validator, the statics, the constructor, the Unix conversions, the readers reusing
`tk_dt_*` directly, `ToOffset`, the arithmetic and the comparisons reusing `tk_dt_*` directly,
the `"o"`/`"s"` formatters and their inverse scanner, `Parse`/`TryParse`. Nothing else:
`teko.tk`, `core_teko.mc`, `user.mc`, `mc.toml` unchanged; no new file.

#### The gate, as run

`mc build . --config mc.macos.toml` clean on mc 0.17.0;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **86 passed, 111 refused as expected,
0 failed** (84 + 2 and 103 + 8); `sh scripts/bootstrap.sh --os macos --arch aarch64` →
**FIXPOINT OK**; `sh scripts/check-docs.sh` → `docs ok: 667 links, 60 fragments,
404 diagnostics, 111 refusals, 149 samples, manifest listed`.

`--dump-ast` of all **187** fixtures that exist on `dcbd88c5`, each compiler in its own
project directory (so a fixture's `#include` reads that compiler's OWN `lib/`): **170
byte-identical, 17 differing**. Sixteen of the seventeen are every fixture that
`#include`s `time.tk` — `lib/time.tk` gained roughly forty functions at its OWN tail, which
is EVERY line of every one of those sixteen diffs (`diff` shows zero lines removed on all
sixteen, measured one by one): no existing declaration moved, the new ones are appended
after everything a program could have referenced before this crumb landed. The seventeenth
is `tests/refuse/guid_no_include.tk`, ruling 4's own wording change, already accounted for.
Cross-compiled (compile only; this host's linker cannot link a foreign-arch object) on the
three non-host machine tables `tests/primitives_dto.tk` reaches — `linux/x86_64`,
`linux/aarch64`, `windows/x86_64` — with no error, which is the SysV, AAPCS64 and Win64
tables `teko_wide.tk`'s own derived machines answer for a `DateTimeOffset` argument, a
`DateTimeOffset` return and a `DateTime`/`TimeSpan` argument beside it in one call; the CI
five-leg matrix is what RUNS all five, this being the local measurement ahead of the push.

`mc limits . --config mc.macos.toml`, tolerance 1.0, base `dcbd88c5` → this crumb: `intrin`
**8 → 8 (unmoved, the law's own row)**, `passes` **15 → 15 (unmoved)**, `syntax`
**16 → 17 (+1**, one row, keyed by NAME — `syntax_expr("DateTimeOffset")` and
`syntax_stmt("DateTimeOffset")` are the one row between them**)**, `types` **15 → 16 (+1**,
the `DateTimeOffset` row**)**, `alias` **22 → 23 (+1**, `type_new` reserving the word, the
same lockstep D75 measured for `Guid`**)**. Verdict `ok` on both sides; **no new `grew`
row**. `TK_MAXPRIMM`/`TK_MAXPRIMO` are this project's own tables and outside `mc limits`'
own set: measured directly, 142/160 and 50/64 after this crumb (the second ceiling raised
by ruling 5, the first left at its existing 160 since 142 does not reach it).

### D77 · The `decimal` arithmetic: 96-bit limbs in surface code, C#'s scales and rounding, and an integer promoted rather than a row registered (C4, 2026-09-15)

> Eleven operators, six of them comparisons, and four conversions — every one of them a
> CALL into `lib/decimal.tk`, which is ordinary teko over 32-bit limbs held in `u64` locals
> and needs no 128-bit instruction on any of the five legs. `mc limits`' `intrin` row does
> not move, `passes` does not move, `syntax` does not move and no name is added to the
> language: what C4 registers is twelve rows of a table `TimeSpan` already had and four
> rows of one this crumb adds beside it. An integer beside a `decimal` is CONVERTED before
> the operator pass looks for a row, which is C# §12.4.5's own binary numeric promotion and
> the reason twelve rows cover all eleven operators plus `+=`, `-=`, `++` and `--`.

C4 is `docs/specs/decimal.md`'s own keystone crumb (§ 4–§ 9, § 12), the one C5 (round and
text), N6 (`i128`/`u128`) and C7 (the native wide instructions) all wait on. It depends on
C3 (D74), which carries the sixteen-byte value, its literal and its movement; nothing here
touched `teko_wide.tk`, which is the proof the machine generalises to a type that COMPUTES
and not only one that moves.

#### The seven rulings this crumb was dispatched with, and what each became

1. **The integer promotion lives in `tk_ops_promote`, and there are no mixed rows.**
   `teko_ops.tk`'s promotion already converted an integer beside a float (D33); a wide arm
   above it converts an integer beside a `decimal` the same way, and both go through the
   same `tk_num_widen` (`teko_typeof.tk`) that every one of D33's nine slots already calls.
   The FUNCTION was extended, not copied: its decimal arm writes a CALL to
   `tk_dec_from_i64` where the float arm writes an `N_CAST`, because `MTASK_CAST` over
   sixteen bytes has no meaning (`tw_cast`, `teko_wide.tk`). The wide arm runs AHEAD of
   `tk_ops_promotes`' own operator list, because `%` is deliberately not on it — this
   backend has no float remainder — and a `decimal` remainder is an ordinary call.
   `tk_check_scalar_compat`'s primitive clause gained the one exception C# §10.2.3 names,
   asked as `tk_num_widens` so that a primitive registering no conversion row (`Guid`,
   `DateTimeOffset`, `TimeSpan`) is refused exactly as before.

2. **`%` is C#'s**: the sign of the DIVIDEND, the scale `max(s1, s2)`, the divisor's sign
   ignored. `-7.5m % 2m` is `-1.5m` and `7.5m % -2m` is `1.5m`, both fixtures.

3. **The alignment of `+`/`-` cannot overflow, and the result can.** Both operands are
   raised to the larger scale IN THE SCRATCH — eight 32-bit limbs, 256 bits — where a
   96-bit mantissa times 10^28 (~2^189) still fits, so an addition never fails for want of
   room to align. It is the RESULT that is reduced, by dividing by ten until it fits both
   28 places and 96 bits, which is what `.NET`'s own `DecCalc` does before it gives up.

4. **Unary `+` is the operand itself, with no row.** `tk_prim_unary_plus` (`teko_prim.tk`)
   asks the table a question it already answers — does this primitive ADD TO ITSELF? — and
   a primitive that does is a number whose unary `+` is its own operand. A row would lower
   it to a call that copies sixteen bytes to hand them straight back. The question is the
   right one and not a `decimal` branch: C# declares `operator +(decimal)` and
   `operator +(TimeSpan)` and declares none on `DateTime`, `Guid` or `DateTimeOffset`, and
   those are exactly the types with no `T + T` row. `+t` on a `TimeSpan` therefore started
   working too, and its row left `not-yet.md`.

5. **A `decimal` global takes NO initializer — the ruling's own "measure and pick", and the
   measurement moved it.** The dispatch expected the leading `-` folded into the literal's
   own blob to make `decimal g = -3.25m;` a constant. Measured on `85901f5b`: `decimal g =
   3.25m;` already dies without any minus, because the literal is the `N_IDENT` of its own
   blob global and mc's `parse_global` (src/parse.mc) demands an `N_INT` — at PARSE time,
   before any teko hook could see it. Making it work needs a taught TOP-LEVEL declaration
   (`syntax` in `parse_top`), which would move `mc limits`' `syntax` row and rewrite a
   chunk of the core's own global grammar for one spelling. So the sign fold was **not
   written** (25 lines saved) and mc's rule stands, alongside `const decimal` and
   `case 1m:` which have exactly the same cause. What C4 does add is the refusal for the
   one spelling that gets PAST mc's rule: `decimal g = 5;` — `5` IS an `N_INT` — would have
   been rewritten by `tk_rc_glb_widen` (`teko_rc.tk`) into the `f64` bits the float slot
   beside it takes. It is refused by name instead,
   `teko: a global decimal takes no initializer`, with `tests/refuse/decimal_global_init.tk`
   as its oracle. A `decimal` at file scope is a slot and an assignment.

6. **`decimal?`, `??` and lambda capture stay where `not-yet.md` put them.** Re-measured on
   this branch: `m ?? 0m` still reaches `teko_wide.tk`'s own machine guard
   (`mc: teko: a cast is not defined on a sixteen-byte value yet`, no `file:line`), which is
   the row `not-yet.md` already carries — the `??` lowering writes a cast of its own and the
   conversion table this crumb adds does not reach it. A surface refusal with a line is a
   small crumb of its own and is not C4's.

7. **The cast-to-call lowering is a TABLE, and everything outside it keeps C3's refusal.**
   `tk_prim_cast_op(to, from, sym)` with four rows (`tk_dec_from_i64`, `tk_dec_from_f64`,
   `tk_dec_to_i64`, `tk_dec_to_f64`), read by `tk_prim_cast_check` before it refuses. The
   two directions are not the same rewrite: `(decimal) x` NAMES a wide type, so no cast may
   survive and the node becomes the call; `(i32) d` keeps the cast the source wrote over the
   call's `i64` result, where it is an ordinary narrowing, and the cast becomes the
   compiler's own (`tk_prim_own_cast`). A target no row names — `(str) d`, and every cast
   over a `Guid` or a `DateTimeOffset` — still earns
   `teko: a decimal does not cast yet` / the reader-and-builder wording, unchanged.

#### Four more rulings, from the review of the crumb (rulings 8–11)

Copilot's five threads and the verifier's F1–F5 landed on the branch before the merge. Two
of the five were **silently wrong answers**, which is the one class of defect this project
refuses to ship, and both are fixed at the root rather than at the site that reported them.

8. **A `u64` SOURCE converts through a row of its own.** `decimal d = x;` and `(decimal) x`
   on a `u64` at or above 2^63 answered a NEGATIVE decimal — `0xFFFFFFFFFFFFFFFF` read as
   `-1m` — on the explicit road, on the implicit one and on the mixed `d + u` alike. The
   cause is one table and one sign test: `tk_prim_cast_find` matches EVERY integer against
   the `TY_I64` row (`tk_prim_slot_fits`, which is what lets `u8`…`i32` ride the same row)
   and `tk_dec_from_i64` asks `n < 0`. The fix is `tk_dec_from_u64`, which fills the limbs
   from the two 32-bit halves and asks no sign question, plus its row registered BEFORE the
   `i64` one: a column naming `TY_U64` takes nothing but `u64`, no slot rule widens into it,
   so the other seven integer types stay exactly where they were and `u64` — the only one
   whose magnitude an `i64` cannot hold — is the only one that moves. Both roads read that
   one table (`tk_prim_cast_lower`, `tk_num_widen`, and `tk_ops_promote` through the
   second), so neither grows a branch of its own. Measured before: sign 1 on all three
   spellings. After: `9223372036854775808m`, `18446744073709551615m`,
   `1m + x == 9223372036854775809m`, six new rows in
   `tests/primitives_decimal_convert.tk`. This is the shape every future wide type takes
   for the same question — the row is keyed on the source type, not on a branch inside the
   lowering.

9. **The quotient carries the smallest scale that preserves it** — C# § 12.9.3, and the one
   operator of the five that decides a scale of its own. Every quotient used to come back
   at scale 28: `5m / 2m` was `2.5m` with 28 places where C# gives 1, `1m / 8m` 28 where C#
   gives 3. Equality cannot see a scale, so the fixture passed while half the division
   results disagreed with C#. `tk_dec_div` is long division ONE decimal place per round
   now, stopping at a remainder of zero (exact there, and every further place would be a
   trailing zero), at the 28th place, or at a quotient that no longer fits 96 bits — the
   ceiling `tk_dv_shl96` used to express as `dividend < divisor * 2^96`, read off the
   quotient itself, so that helper left with its last caller. The trailing zeros the natural
   scale `sa - sb` carries are then stripped for the same rule, which is what makes
   `4.0m / 2m` equal `2m` at scale 0. **Nothing else in this type strips a zero**:
   `1.0m == 1.00m` stays true, `1.0m * 1.0m` is still `1.00m`, and `+`/`-`/`*` keep the
   scales § 5 gives them. `1m / 3m` still spends all 28 places and `1m / 3m * 3m` is still
   `0.9999999999999999999999999999m`. The fixture asserts the SCALE of every quotient now,
   read off `&d` per § 1's layout, and not only the value —
   `docs/specs/decimal.md` § 5 and § 8 carry the rule, which they did not state at all.

10. **The IMPLICIT conversion names the include, exactly as the explicit one does.**
    `decimal d = 1;` with no `#include "decimal.tk"` reached the core's own `call to unknown
    function` — no `teko:`, no file named — while `(decimal) 1` one line over already
    answered `teko: decimal needs #include "decimal.tk" before it is used`. The check goes
    into `tk_num_widen`'s wide arm and nowhere else: that one function IS the implicit road
    in all nine of D33's slots and is what `tk_ops_promote` calls for a mixed operand, so
    `d + 1` — which used to answer the unrelated `teko: the type of the right side of `+` is
    not known here` — is covered by the same question.
    `tests/refuse/decimal_implicit_no_include.tk` is the oracle.

11. **A fixture's failure path may not return its success code.**
    `tests/primitives_decimal_math.tk:111` answered `return 42` on a failed check, which is
    the fixture's own `expect-exit`: the assertion could not fail. The audit over every
    decimal fixture found one more of the same shape, older than this crumb
    (`primitives_decimal_value.tk:160`), fixed here because it is an assertion THIS branch's
    gate runs over code this branch changes. Both were proved live by mutation — the
    expected value moved by one, the fixtures exit 55 and 47 instead of 42 — which is the
    check `// expect-exit` alone cannot make.

The two documentation findings are corrected in place:
`docs/internals/primitives.md`'s ceilings table said `TK_MAXPRIMO | 48`, a number two crumbs
stale (D76 raised it to 64, this one to 80), and carried no row for `TK_MAXPRIMX` at all —
both fixed, with the live counts (62/80 and 5/8); every other `TK_MAXPRIM*` number in `docs/`
was re-read and the only other mention is `docs/specs/datetime-extras.md`'s narrative of
D76's own raise, which is history and correct. `docs/reference/not-yet.md`'s lambda-capture
row spelled the capture `use (d)` with no context: measured on this branch, the spelling the
row is about is `F f = () use (d) => d;` — the capture list FOLLOWS the parameter list — and
it does still answer `mc: teko: a cast is not defined on a sixteen-byte value yet`, so the
row stands and now shows the spelling it is true of. `use (d) () => d`, the order a reader
guesses, is a different refusal (`call by name only`) that says nothing about the wide value,
and the row says that too.

**What this moved in the files listed below.** `lib/decimal.tk` is still **append-only
against `85901f5b`** — `tk_dec_div`'s body is rewritten and `tk_dv_shl96` deleted with its
last caller, but both live inside C4's own new block, which is why the dumps below still show
zero lines removed. `teko_typeof.tk` gains the include check and one forward declaration;
`teko_decimal.tk` one conversion row; `tests/primitives_decimal_value.tk` one exit code, the
only base fixture whose SOURCE this pass touched. The file is 695 lines now, not the 661 the
section below recorded.

The gate after the review, mc 0.17.0, macos/aarch64:
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **93 passed, 114 refused as expected,
0 failed**; `sh scripts/bootstrap.sh --os macos --arch aarch64` → **FIXPOINT OK**;
`sh scripts/check-docs.sh` → `docs ok: 674 links, 63 fragments, 406 diagnostics, 114
refusals, 149 samples, manifest listed`; `mc limits` verdict `ok` on both legs with `intrin`
**8**, `passes` **15**, `syntax` **17**, `types` **16**, `alias` **23** and every other
capped table exactly where the first pass left them, `TK_MAXPRIMO` 62/80 and `TK_MAXPRIMX`
**5**/8. `--dump-ast` over **all 201** `.tk` files under `tests/` on `85901f5b`, each
compiler in its own project directory with `--include=lib` (without it every fixture with an
`#include` dumps the same one-line open failure and the comparison proves nothing — the trap
this pass fell into once): **195 byte-identical, 6 differing**, the same six the first pass
had, none added by the review. Four are `decimal.tk`'s includers with **zero lines removed**
(`_indirect` +1700, `_out` +1700, `_order` +2829, `_value` +1701 and the one line its own
exit code moved), and two are the refuse fixtures C4 turned into run cases. It supersedes
the counts the section below recorded for the first pass.

#### What the library turned out to be

Eight 32-bit limbs in `u64` locals, 256 bits, which is what the widest intermediate needs:
an operand of `+` aligned by 10^28 (~2^189), the 192-bit product of `*`, and the 96-bit
divisor shifted by 96 that bounds the quotient of `/`. The vectors are LOCAL arrays, never
globals, so `tk_dec_add(tk_dec_mul(a, b), c)` cannot find another call's scratch — measured,
`u64 v[8]` in a frame is its own address and crosses as a `uptr` with no accessor.

Division is long division ONE BIT at a time over the whole 256 — no quotient-digit estimate
to get wrong, 256 rounds of a shift, a compare and at most a subtract. It is the price this
crumb pays for an algorithm read once and trusted, and C7 is still the speed crumb the
specification names.

Every result goes through one function, `tk_dec_pack(r, scale, sign)`: it divides by ten
until the value fits both 28 places and 96 bits and rounds **half away from zero** on the
last digit — the OPERATORS' rounding, C#'s own, and not `decimal.Round`'s half to even,
which is C5. Only the LAST remainder is read, and that is exact for this rule: a five rounds
up as well, so there is no tie to break. The carry of that rounding can itself leave 96 bits
(999…9 + 1), which is why the reduction is a loop and not a step. `1m / 3m * 3m` is
`0.9999999999999999999999999999m` and not `1m`, which is the fixture the whole design turns
on.

#### The one `mc` behaviour that shaped the code

**`mc` compares every integer signed, `u64` included.** `gen_binary` (mc src/gen_walk.mc)
emits `MTASK_CMP` with a condition and no signedness, where `bin_op` DOES take
`type_signed(...)` for divide and shift — so `u64 m = 2; u64 lim = one << 63; m >= lim` is
TRUE. Measured at the surface on mc 0.17.0 and reproducible in four lines of pure `mc`. It
is not worked around and nothing in `mc` is touched (D2): the library simply holds no
comparison over a 64-bit value that may set bit 63 — every limb is below 2^32, and the one
range check that needs the top bit (`tk_dec_to_i64`) reads it with a SHIFT, which is
unsigned where a comparison is not. Reported as an adjacent finding, not acted on here.

#### What is built

`lib/decimal.tk` — 49 lines to 661, append-only below C3's three declarations: the scratch
vector (`tk_dv_*`: zero, copy, compare, add, subtract, increment, multiply-small,
divide-small, shift, the school product and the bitwise long division), the sixteen bytes
read and written, `tk_dec_raise`/`tk_dec_pack`, `tk_dec_add`/`_sub`/`_mul`/`_div`/`_rem`/
`_neg`, `tk_dec_cmp` and the six comparisons, and `tk_dec_from_i64`/`_to_i64`/`_to_f64`/
`_from_f64`. It includes `rt.tk` itself, for `panic` and for `tk_f64_bits`.
`teko_prim.tk` — `TK_MAXPRIMO` 64 → 80 (50 rows to 62); the conversion table
(`TK_MAXPRIMX 8`, `tk_prim_cast_op`, `tk_prim_cast_find`, `tk_prim_cast_arg`,
`tk_prim_cast_lower`) read by `tk_prim_cast_check`; `tk_prim_unary_plus`; and the UNARY
road routed through `tk_prim_conv` like the binary one, which is the latent defect this
crumb had to fix first — `tk_prim_raw` is `(i64) x` and dies in `tw_cast` for a wide
operand, and `-d` is the first wide unary this compiler has.
`teko_decimal.tk` — `tk_dec_ops()`, the twelve operator rows and the four conversion rows,
registered AFTER `tk_float_init()` because two of them name `ty_f64`.
`teko_typeof.tk` — `tk_num_wide_widens`, the wide arm of `tk_num_widens`/`tk_num_widen`,
and the one exception in `tk_check_scalar_compat`'s primitive clause.
`teko_ops.tk` — `tk_ops_promote_wide`, ahead of the float clause.
`teko_rc.tk` — the wide refusal in `tk_rc_glb_widen`.
`teko.tk` — one call. Nothing else: `core_teko.mc`, `user.mc`, `teko_wide.tk`, `mc.toml`
unchanged; no new module, no new `lib/` file.

#### The gate, as run

`mc build . --config mc.macos.toml` clean on mc 0.17.0;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **93 passed, 113 refused as expected,
0 failed** (87 + 6 and 111 − 2 + 4); `sh scripts/bootstrap.sh --os macos --arch aarch64` →
**FIXPOINT OK**; `sh scripts/check-docs.sh` → `docs ok: 671 links, 62 fragments,
406 diagnostics, 113 refusals, 149 samples, manifest listed`.

`--dump-ast` of all **198** fixtures that exist on `85901f5b`, each compiler in its own
project directory (so a fixture's `#include` reads that compiler's OWN `lib/`): **192
byte-identical, 6 differing**. Four of the six are every fixture that `#include`s
`decimal.tk` — `primitives_decimal_value.tk`, `_out.tk`, `_indirect.tk` and `_order.tk` —
and `lib/decimal.tk` grew at its own tail: `diff` shows **zero lines removed** on all four,
measured one by one, so no existing declaration moved. `_order.tk` adds more than the other
three (2803 lines against 1674) for one reason: it includes `decimal.tk` ALONE, and
`decimal.tk` now includes `rt.tk`, which the other three already had. The remaining two are
`tests/refuse/decimal_add.tk` and `tests/refuse/decimal_cast.tk`, which C4 turns into run
cases — their source changed, which is the whole point of the crumb.

`mc limits . --config mc.macos.toml`, tolerance 1.0, base `85901f5b` → this crumb:
`intrin` **8 → 8 (unmoved, the law's own row)**, `passes` **15 → 15 (unmoved)**, `syntax`
**17 → 17 (unmoved — C4 adds no name to the language)**, `types` **16 → 16 (unmoved)**,
`alias` **23 → 23 (unmoved)**. The compiler leg grows where a compiler that gained code
grows and nowhere else: `nodes` 169522 → 170166, `funcs` 3367 → 3378, `globals` 979 → 983,
`strings` 2276 → 2295, `ins` 233594 → 234610, `symbols` 6622 → 6656, `defines` 1286 → 1287.
Verdict `ok` on both legs; **no new `grew` row**. This project's own tables are outside
`mc limits`' set and were measured directly: `TK_MAXPRIMO` **62/80** (the ceiling raised
here from 64), `TK_MAXPRIMX` **4/8** (new), `TK_MAXPRIMM` **141/160** (unmoved by this crumb,
which registers no member row at all; D76 recorded 142 for the same table, one more than a
call count of `tk_prim_member*` outside `teko_prim.tk` finds today).

### D78 · `f32` widens to `f64` implicitly, `f64` never narrows without a cast — the raw bit move is gone (2026-09-15)

> `docs/reference/not-yet.md`, since D33, carried one row this crumb closes:
> `f64 d = s;` with an `f32` `s` was neither converted nor refused. It compiled, and it was
> SILENT-WRONG: the core moves the four bytes of the `f32` into the low half of the eight-byte
> slot and leaves the top half whatever the frame held, so `f32 b = 2.5f; f64 e = b;` did not
> read `2.5`. `b == 2.5` and `b + 1.0` were worse still — `res_binary` (mc/src/gen_resolve.mc)
> types a mixed binary from the LEFT operand, so a comparison with `b` on the left ran as an
> `f32` compare against the raw low half of the `f64` literal's bits, never promoting either
> side. Three call sites, one door each, close it: `tk_num_widens` (`teko_typeof.tk`), which
> every widen in the language already answers through; `tk_ops_promote`
> (`teko_ops.tk`), the mixed-operand rewrite a binary runs before it is typed; and
> `tk_rc_glb_widen` (`teko_rc.tk`), the literal-rewrite a GLOBAL initializer takes because mc's
> core refuses a cast there.

**Ruling 1 — `f32` widens to `f64` implicitly (C# §10.2.3), in the same nine slots an
integer already widens in.** `tk_num_widens` (`teko_typeof.tk`) answers 1 for a pair of
`TK_FLOAT` types when the source is the narrower one (`type_width(ety) < type_width(tty)`);
`tk_num_widen`, the one function every one of D33's nine slots already calls through, falls
into its existing `tk_cast(tty, e)` line unchanged — an ordinary `N_CAST` the machine's own
`fa_cast`/`fx_cast` (`<float>`, `lib/machine_arm64_float.mc`/`lib/machine_x86_64_float.mc`)
already lowers both ways, ordinary code this crumb writes none of.

**Ruling 2 — `f64` narrows to `f32` in NO slot without a cast (C# CS0664: `float f = 2.5;`
is an error, "use an F suffix").** One clause beside the existing float-into-non-float
rejection in `tk_check_scalar_compat` (`teko_typeof.tk`): both operands `TK_FLOAT` and the
TARGET the narrower one refuses with the wording every mismatched value already gets, `teko:
a value of type f64 does not convert to f32`. A float LITERAL without an `f` suffix is an
ordinary `f64` node (`fl_lit`, `lib/float.mc`) and is refused into an `f32` slot the same way
a variable is — `2.5f` or `(f32) 2.5` is what says the narrowing is meant.

**Ruling 3 — a global's own initializer is a LITERAL REWRITE, and D33's existing one had a
trap for a float source.** `tk_rc_glb_widen` reads `nd_val(e)` as a DECIMAL value when the
source is an integer (`fl_dec2bits`'s own contract), but a float literal's `nd_val` is
already a BIT PATTERN `fl_lit` wrote — feeding `1.0f`'s `0x3f800000` to `fl_dec2bits` as the
integer `1065353216` would have rewritten `f64 G = 1.0f;` into garbage. The guard is
`type_kind(nd_type(e)) == TK_FLOAT`, asked before the integer path, and the rewrite for that
arm is BIT SURGERY over the two IEEE-754 layouts rather than a re-encode: sign carried
unchanged, the biased exponent rebiased `127 → 1023`, the 23-bit mantissa left-shifted into
the 52-bit one, a SUBNORMAL `f32` renormalized first (the same shift-until-the-implicit-bit
a `clz` would give — every subnormal `f32` fits as a NORMAL `f64`, so this is the one branch
with no direct rebias), and `inf`/`NaN` and the signed zero each keeping their own exponent
shape. `tk_f32_bits_to_f64` (`teko_rc.tk`) is nineteen lines of `u64` shifts and masks, no
call into `<float>` at all — the taught compiler's OWN sources build with the stock core
(`core_teko.mc`'s four `#include <mc/core_*>`, no `<float>`), so nothing in this file may use
an actual `f64` local; every existing bit-pattern rewrite in this same function is the same
constraint, unstated until now.

**The bit table this crumb's own fixture reads back through `&x`/`ld8`
(`tests/primitives_f32.tk`), least-significant byte first:**

| value | width | bytes |
|---|---|---|
| `2.5f` | `f32` | `00 00 20 40` |
| `2.5` widened from the `f32` above | `f64` | `00 00 00 00 00 00 04 40` |
| `1.0f` widened at a global initializer (`f64 G = 1.0f;`) | `f64` | `00 00 00 00 00 00 f0 3f` |
| `1` into an `f32` slot (D33's own rule, unmoved) | `f32` | `00 00 80 3f` |
| `(f32) 2.5`, the explicit road, unmoved | `f32` | `00 00 20 40` |

Before this crumb, the second row was **four of those eight bytes followed by whatever the
frame held** — not a fixed wrong value, which is why no fixture recorded one: the bug was
"reads garbage", not "reads a specific wrong number".

**`tests/primitives_decimal_order.tk` carried the double error this crumb's own scouting
found.** Line 32 read `f32 f = 0.25;` — an `f64` literal narrowed into an `f32` slot, which
this crumb now refuses — and line 34 compared `f != 0.25`, which the SAME silent-wrong this
crumb closes made pass: `f` held the low four bytes of `0.25`'s eight (zero, since `0.25`'s
low word is `0x00000000`), and `f != 0.25` read the left side raw (`res_binary`'s own LEFT
rule) as an `f32` `0.0`'s bits against nothing narrowed at all on the right — two wrongs
that happened to agree. The fixture now writes `0.25f`, an `f32` literal the target's own
width, and passes by the RIGHT arithmetic: `tk_ops_promote`'s new symmetric pair
(below) inserts an `f64` cast over `f` for the comparison, the one `--dump-ast` line this
crumb's whole diff allows outside the file's own edited literal (measured against the base
compiler on `f335f9d0`, below).

**`tk_ops_promote` itself had to stop being asymmetric, or ruling 1 opened the same bug at
the BINARY sites `tk_num_widens` did not reach before.** Its old shape only ever converted
`nd_b` to `nd_a`'s type when `nd_a` was the float, which is correct for an int-beside-a-float
(the int is always the one that moves) but wrong the moment BOTH operands can be a float of
a different width: `b == 2.5` with `b` an `f32` on the LEFT asked `tk_num_widen(f32, f64,
nd_b)`, which converts NOTHING (`f64` does not widen to `f32`), leaving the comparison typed
`f32` against a `f64` literal's raw bits — the exact bug this crumb exists to close, now
reachable through the fix itself. The rewrite is the same symmetric pair
`tk_ops_promote_wide` already uses one function up: `tk_num_widens(ta, tb)` then
`tk_num_widens(tb, ta)`, whichever ONE answers converts the OTHER side, so the narrower float
moves regardless of which side of the operator it sits on, and the site is typed from the
survivor — the wider type — however `res_binary` reads the left operand once it is
resolved.

#### What is built

`teko_typeof.tk` — the float clause in `tk_num_widens` (widens) and one clause in
`tk_check_scalar_compat` (refuses the other way).
`teko_ops.tk` — `tk_ops_promote`'s float arm rewritten as the same symmetric pair
`tk_ops_promote_wide` already carries, so a mixed `f32`/`f64` binary converts whichever side
is narrower rather than only ever the right one.
`teko_rc.tk` — the `TK_FLOAT` guard in `tk_rc_glb_widen` and `tk_f32_bits_to_f64`, the bit
surgery beside it.
`tests/primitives_f32.tk` (new, `// expect-exit: 42`) — the bit table above, an argument
into an `f64` parameter, a value returned `f32` and widened at the caller, a field store, an
element store, a global assignment, and the explicit cast, unmoved.
`tests/refuse/f32_from_f64.tk`, `f32_from_f64_arg.tk`, `f32_from_f64_global.tk` (new) —
an initializer, an argument and a global initializer, each refused with `teko: a value of
type f64 does not convert to f32` at its own line.
`tests/primitives_decimal_order.tk` — one literal, `0.25` to `0.25f`, the double error
above.
`docs/reference/not-yet.md`, `docs/reference/types.md`, `docs/reference/diagnostics.md` —
the gap's own row deleted, the implicit/explicit rule written out beside the integer one it
mirrors, and the file-scope refusal table's new row.
Nothing else: no new intrinsic, no new node kind, no new type registered, `core_teko.mc`/
`user.mc`/`mc.toml` untouched.

#### The gate, as run

`mc build . --config mc.macos.toml` clean on mc 0.17.0; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` → **94 passed, 117 refused as expected, 0 failed** (93 + 1 and 114 + 3);
`sh scripts/bootstrap.sh --os macos --arch aarch64` → **FIXPOINT OK**; `sh
scripts/check-docs.sh` → `docs ok: 672 links, 64 fragments, 406 diagnostics, 117 refusals,
150 samples, manifest listed`.

`--dump-ast` of every fixture that exists on `f335f9d0` (the base this branch forked from),
each compiler in its own project directory: **92 of 93 positive fixtures byte-identical, one
differing** (`primitives_decimal_order.tk`, both allowed changes — the edited literal and
the one `fcvt` the corrected arithmetic now inserts, both justified above) — **and all 114
`tests/refuse/` fixtures' stderr byte-identical**, so no refusal moved a word or a line that
this crumb did not mean to touch.

`mc build . --config mc.macos.toml --compiler-only --limits`, stock mc 0.17.0, base
`f335f9d0` → this crumb: `intrin` **8 → 8 (unmoved)**, `types` **16 → 16 (unmoved)**,
`syntax` **17 → 17 (unmoved — this crumb adds no name to the language)**, `alias` **23 →
23 (unmoved)**, `passes` **15 → 15 (unmoved)**. Verdict `ok` on both legs; no new `grew`
row. The compiler build itself grows where code was added and nowhere else: `nodes` 170191
→ 170359, `funcs` 3378 → 3379, `ins` 234647 → 234873, `symbols` 6657 → 6658; `globals`
983 → 983 and `defines` 1287 → 1287 are unmoved (`tk_f32_bits_to_f64` is a function, not a
new global).

**Copilot findings on #731 — the width rule reached every SLOT `tk_num_widen`/
`tk_check_scalar_compat` cover directly, but two slots answer through a DIFFERENT gate
first and neither one asked the question.** Both reproduced on `97eb5933` before they were
fixed, both refused after.

1. **A `T?` VALUE payload is a slot too (Q1b), and `tk_nl_check_value` never asks
   `tk_check_scalar_compat` at all — it asks `tk_nl_fits` (`teko_null.tk`), a parallel rule
   written before this crumb existed, and that rule's float clause was `type_kind(pty) ==
   TK_FLOAT` returning 1 for ANY float `ety`, width unasked.** `f32? x = 2.5;` compiled,
   and `tk_nl_wrap` handed `tk_num_widen(f32, f64, e)` a pair `tk_num_widens` correctly
   refuses to convert (narrowing), so the node crossed unconverted into the box — the exact
   raw-bit bug this whole crumb exists to kill, alive one layer down. The fix reuses the
   same oracle rather than re-deriving the rule a second time: `tk_nl_fits`'s float branch
   now asks `tk_num_widens(pty, ety)` for a float/float pair (the equal-width case already
   returned at the function's first line, so what is left to ask is exactly the widen/
   narrow question), an integer source still fits either width unconditionally, matching
   `tk_check_scalar_compat`'s own shape one line up. Reported under the nullable's own
   name, the slot the source actually wrote — `teko: a value of type f64 does not convert
   to f32?` — since `tk_nl_check_value` already names `ti` and not the payload.
   `tests/refuse/f32_from_f64_nullable.tk` (new); `tests/primitives_f32.tk` gains an
   `f64? y = b;` row (bits read back through `.Value`, codes 31-33) proving the WIDENING
   direction still boxes the converted value and not the narrower one's raw bits, and an
   unmoved `f32? z = 2.5f;` row (codes 34-35, equal width, no conversion).

2. **A `params f64[]` element is a slot too, and `tk_pm_check_elem` asks
   `tk_pm_elem_fits` (`teko_params.tk`) before `tk_pm_pack` ever reaches its own
   `tk_num_widen` call — and the typed-value arm of `tk_pm_elem_fits` rejected ANY float
   mismatch outright** (`type_kind(aty) == TK_FLOAT || type_kind(ety) == TK_FLOAT) return
   0`), a clause written for an untyped integer literal's tie-break that a typed float
   value fell through into as well. `total(1.0, b)` on an `f32 b` against a declared
   `params f64[]` read `teko: a value of type f32 does not convert to f64` — backwards from
   D78's own ruling 1, which widens exactly that pair everywhere else. The fix is one
   clause ahead of the literal-only arms, asked at every stage and not `loose` only (a
   typed float carries no literal tie-break to protect): `type_kind(aty) ==
   TK_FLOAT && type_kind(ety) == TK_FLOAT` asks `tk_num_widens(ety, aty)`, the identical
   oracle. `tk_pm_pack`'s own `tk_num_widen(ety, tty, t)` call was already unconditional
   and already general — nothing in it named integers specifically — so once the gate
   stopped refusing the pair early, the cast it inserts was already the right one; no
   second change was needed there. `tests/refuse/f32_params_from_f64.tk` (new, `params
   f32[]` given the untyped `2.5`, still narrowing and still refused); `tests/
   primitives_f32.tk` gains a `total(params f64[] xs)` helper and two rows, `total(1.0,
   b)` (code 29) and `total(b, b)` (code 30), both elements `f32` widening into the
   `f64[]` the pack builds.

Neither fix touches `tk_num_widen`, `tk_num_widens` or `tk_check_scalar_compat` themselves
— both gates now ask the SAME oracle those already answer through, rather than repeating
its rule a third and a fourth time with their own drift.

**The gate, re-run with both fixes in:** `mc build . --config mc.macos.toml` clean; `sh
scripts/fixtures.sh ./build/teko mc.macos.toml` → **94 passed, 119 refused as expected, 0
failed** (94 + 0 and 117 + 2, the two new refuse fixtures); `sh scripts/bootstrap.sh --os
macos --arch aarch64` → **FIXPOINT OK**; `sh scripts/check-docs.sh` → `docs ok: 673 links,
64 fragments, 406 diagnostics, 119 refusals, 150 samples, manifest listed`.

`--dump-ast` of every fixture that exists on `f335f9d0` (`--include=lib --include=tests`,
single-file mode, base and this crumb each its own worktree): **92 of 93 byte-identical,
the same one fixture as the crumb's own first pass** (`primitives_decimal_order.tk`,
unrelated — the literal edit and the `fcvt` D78's own arithmetic fix inserts, already
justified above). Neither Copilot fix moved a SECOND fixture's dump: no fixture on `main`
before this PR exercised a `T?` value payload or a `params f64[]` element with a REAL `f32`
source (both gaps were reachable only through code this PR itself adds), so there is
nothing already accepted for either fix to silently move.

`mc build . --config mc.macos.toml --compiler-only --limits`, `rm -rf build` first on both
legs, `97eb5933` (this PR before the two fixes) → after: `intrin` 0 → 0, `types` 1 → 1,
`syntax` 0 → 0, `alias` 1 → 1, `passes` 0 → 0 — every budget row this repository's own laws
(D21, zero new intrinsics) are measured against **unmoved**. `nodes` 170359 → 170388 (+29),
`funcs` 3379 → 3379 (unmoved — both fixes extend an existing function's body, neither adds
one), `ins` 234873 → 234921 (+48), `symbols` 6658 → 6658 (unmoved). `heap` (the one row `mc
limits` reports in bytes, never elements) moved from 100645536 to 100747472, +101936 bytes,
+0.10% — stated here as what it is, the proportional cost of the ~30 nodes and ~50
instructions two small clauses added, and not offered as proof of anything: the proof this
fix is correct is the two reproducers above turning from a silent pass into the width
refusal, the two new fixtures, and the unmoved `--dump-ast` of the 92 fixtures that predate
it.

---

### D79 · `decimal` rounds, is written and is read back — C5, and the wide argument column the mechanism owed (2026-09-15)

> C4 (D77) left `decimal` computing and converting with an EMPTY member table: `Round`,
> `ToString`, `Parse` and the statics were all `teko: unknown static member of decimal`,
> and the two `syntax` rows a receiver costs were unspent on purpose (D74). C5 spends them
> and registers nineteen rows of `docs/specs/decimal.md` § 7 — the whole API, every one of
> them an ordinary call into `lib/decimal.tk`, no intrinsic, no pass, no machine. The
> crumb's own surprise was on the mechanism's side and is ruling 8 below: `decimal.Round(d,
> 2)` is the first row in this compiler whose ARGUMENT is sixteen bytes, and the shared
> widening wrote a cast where a call was needed.

**Ruling 1 — `decimal.ToDouble(d)` and `decimal.FromDouble(f64)` are not registered, and
§ 7 is amended to say so.** They are `(f64) d` and `(decimal) x`, two of the four
conversion rows C4 already landed, under C#'s other spelling. A member row would be a
second door to one road, and a second door is a second thing to keep right. They earn
`teko: unknown static member of decimal` with the reason written down in
`docs/reference/not-yet.md`.

**Ruling 2 — `TK_MAXPRIMM` is 192.** The nineteen rows take the member table from 141 to
160, which is exactly the cap D76 set, so the next primitive would have hit a ceiling for
no reason but arithmetic. Measured with a counter printed at the end of `teko_init()`, not
by counting registration lines: `nprimm=160`, `nprimp=101` of 128, `nprimo=62` of 80,
`nprimt=7` of 8, `nprimx=5` of 8. Raising a `#define` costs the array it sizes and nothing
else.

**Ruling 3 — `Round` is half to EVEN; `Truncate`, `Floor` and `Ceiling` are C#'s, at scale
zero; the scale never grows.** This type now carries two rounding rules and they are both
C#'s: the OPERATORS round half away from zero (`tk_dec_pack`, D77) and `decimal.Round`
rounds half to even, `MidpointRounding.ToEven`, which is the owner's own naming of it.
`Round(1.5m, 3)` answers `1.5m` at scale 1 — a scale never GROWS — and `Round(2.675m, 2)`
answers `2.68m`, because `2.675` is exact in base ten, so the tie is real and the kept `7`
is odd. The parity of what is kept is read off limb zero: ten is even, so a binary value
and its last decimal digit share a parity, which is one test instead of a division.
`Floor(-1.5m)` is `-2m`, `Ceiling(-1.5m)` is `-1m`, `Truncate(-1.5m)` is `-1m`, and all
three answer at scale 0 (`Floor(1.50m)` is `1m`), which is what C# answers. One function
writes all three (`tk_dec_intpart`), told apart by the SIGN a discarded fraction grows the
magnitude for; `Truncate` passes a sign no value has.

**Ruling 4 — a zero carries no sign in the text, `ToString(n)` is C#'s `"F<n>"`, and a
places count outside `0..28` is ONE panic.** `-0m` writes `"0"`, which is consistent with
`-0m == 0m` being true and with `.Sign` answering 0 for it; `0.00m` writes `"0.00"`,
because the scale is part of the value and a trailing zero is not noise (`1.50m` and `1.5m`
are two values). `ToString(places)` rounds by ruling 3 and then pads with zeros in the
TEXT, never in the mantissa — `decimal.MaxValue.ToString(2)` is 29 digits and two zeros, a
number no 96-bit mantissa holds. The dispatch proposed a second message for its range
(`the decimal format is out of range`); there is one cause here — a places count outside
the range a scale has — so there is one message,
`teko: the decimal places are out of range`, reached by `ToString` through the rounding it
does first. One cause, one message, one fixture (D20's own economy).

**Ruling 5 — `Parse` is exact or refused, and its grammar has no culture in it.**
`[+|-] digits [ . digits ] [ (e|E) [+|-] digits ]`, with `.5` and `5.` accepted as C#
accepts them, and NOTHING else: no surrounding whitespace, no thousands separator, no
currency sign — § 9 already excludes formatting from this type and this is that exclusion
read on the way in. Two failures stay two causes, which is why `tk_dec_scan` answers three
values: `teko: the string is not a decimal` for text that is none, and
`teko: decimal overflow` for a number that IS one and does not fit — a mantissa past 96
bits, or a scale past 28 places once the exponent moved it. C# would round the second case;
teko refuses it, because this type is exact or loud and rounding a number the writer wrote
out in full is the silently-wrong answer this project does not ship. `ToString` never
produces such a string, so the round trip is closed. `TryParse` reads both as false,
answers `0`/`1` and never panics — the hand-parsed `out` argument is
`Guid.TryParse`'s own shape (D75).

**Ruling 6 — `CompareTo` and `.Sign` read the VALUE.** `CompareTo` is `tk_dec_cmp`, already
scale-insensitive, and `Equals` is `tk_dec_eq`, which is what `==` asks: no new function for
either. `.Scale` is the raw scale byte (`tk_dec_scale`), and `.Sign` is a NEW function
(`tk_dec_signum`) precisely because `tk_dec_sign` is the sign BIT and answers 1 for `-0m`,
where C#'s `.Sign` answers 0.

**Ruling 7 — `lib/math.tk` lands, `public`, and teaches nothing.** § 7 names it inside C5,
so it lands with C5: `Math.Round` in both arities plus `Truncate`, `Floor`, `Ceiling` and
`Abs` over a `decimal`, forwarding to the statics. It is the one library file that needs no
mechanism at all — a static method on a declared class is a construct this compiler already
runs — so nothing in it is registered and `mc limits` does not move for it. It is `public`
because an `internal` class answers only to code of its own project (`tk_check_type_use_from`,
`teko_access.tk`), and a program that includes a library is not one; measured, since the
first draft was `internal` and `--dump-ast` refused the fixture with
`teko: Math is internal to another project` while `mc build` — one project — accepted it.
C#'s other forty `Math` overloads are a library crumb of their own and are named in
`not-yet.md`.

**Ruling 8 — a WIDE column converts its argument with a CALL, and `tk_prim_arg_widen` wrote
a cast.** This is the defect the crumb found, fixed at the root rather than avoided.
`tk_prim_arg_widen` (`teko_prim.tk`) is where an argument whose type the parser could not
settle gets its widening once the pass knows it; it built an `N_CAST` to the column's type,
which is right for a float column and meaningless for a sixteen-byte one — `MTASK_CAST` over
a wide value has no lowering and `tw_cast` (`teko_wide.tk`) `die`s on it. Nothing had ever
exercised it: `Guid`'s statics take a `str` and `decimal` had no member rows at all, so
`decimal.Round(d, 2)` is the first wide ARGUMENT in this compiler. Measured on the branch,
before the rows were written: every integer argument the parser cannot type — a local, a
parameter, a `u8`, an array element, a call — died in codegen with `teko: a cast is not
defined on a sixteen-byte value yet`, while a LITERAL passed, because the parser types it
and `tk_rc_call_args` (`teko_rc.tk`) writes the conversion against the declaration instead.
The fix is `tk_num_widen` (`teko_typeof.tk`), which already answers both shapes since D77,
with `tk_node_replace` putting its node in place of the argument's — the identity and the
sibling link a node already spliced into a call's list must keep. One rule, one place, and
the float arm is untouched: all 92 pre-existing fixtures dump byte-identically.

**Ruling 9 — `(str) d` now names the pair that does the work.** With `pt_read`/`pt_build`
filled, the cast refusal is
``teko: a decimal does not cast; `.ToString()` writes it and `decimal.Parse(s)` reads it``
instead of C3's shorter *a decimal does not cast yet* — the same `Guid`-shaped wording D75
introduced, and `tests/refuse/decimal_str_cast.tk` moves with it in the same commit.

**What it cost.** `mc limits . --config mc.macos.toml` on `a1cf0b52` and on the crumb,
verdict `ok` on both sides: `syntax` **17 → 18** — ONE row and not two, because the table is
keyed by NAME and `syntax_expr("decimal")`/`syntax_stmt("decimal")` share it — with
`intrin` **8**, `passes` **15**, `types` **16** and `alias` **23** all unmoved, which is
D21's own row among them. The compiler unit grows with the library it carries: `nodes`
198544 → 199142, `funcs` 4127 → 4132, `globals` 1375 → 1377, `strings` 2880 → 2914.

**What proves it.** `tests/primitives_decimal_round.tk` (53 checks, exit 42),
`_text.tk` (57, 42), `_parse_bad.tk` (70) and `_places_bad.tk` (70), plus the reworded
`tests/refuse/decimal_str_cast.tk`: 98 passed, 119 refused as expected, 0 failed.
`FIXPOINT OK`. `--dump-ast` of all 217 fixtures under the base compiler and under this one:
212 byte-identical, and the five that move are the four new fixtures (which the base
compiler refuses by name) and the refusal whose wording ruling 9 changed.

**One adjacent finding, reported and not fixed here.** `.` sinks THROUGH a prefix `- ! ~`
(`teko_prefix.tk`, delivery 5) so that `-a.x` means `-(a.x)` as C# does — and the node shape
cannot tell a bare prefix from a PARENTHESIZED one, so `(-d).ToString()` also reads as
`-(d.ToString())`. It predates this crumb and is not `decimal`'s: measured on `a1cf0b52`,
`(-t).Hours` on a `TimeSpan` reads as `-(t.Hours)` the same way. On a `decimal` the
consequence is louder, because the `-` then lands on a `str` and the read segfaults, so
every fixture here binds a negative receiver to a local first and says why.

### D80 (D79 is C5's, on its own branch) · a postfix after a parenthesized `- ! ~` binds to the GROUP, not past it (2026-09-15)

> `.`/`[`/`?.` already sink through a BARE `- ! ~` chain (delivery 5, `teko_prefix.tk`):
> `-a.x` reads `-(a.x)`, matching C#. The sink fired on the AST alone -- a receiver shaped
> `N_UNARY(op, x)` -- and the AST carries no mark for a SOURCE `(...)`, so it fired the
> same way when the source had written the parentheses on purpose: `(-t).Duration().Hours`
> answered -3 for a 3-hour `TimeSpan`, read as `-(t.Duration().Hours)`; `(-arr)[0]` compiled
> to `-(arr[0])`, a value, where C# refuses the array negation outright; `(-oc)?.M()` failed,
> but late and on the WRONG type -- C5's own verifier found the same shape on `decimal`,
> where `(-d).ToString()` negates a `str` and segfaults (`docs/reference/not-yet.md`, the
> row this entry removes). Silent-wrong on `.`/`[`, late-and-wrong on `?.`.

**The signal is lexical, because the AST has none.** mc's core returns `(expr)` as `expr`
itself -- no `N_PAREN`, no flag (`src/parse.mc:911-913`) -- so `tk_unary_parened`
(`teko_prefix.tk`) reads raw SOURCE BYTES backward from `p_start()`, the position of
whatever `cur` is when a `syntax_infix` handler runs: mc's own `parse_expr` (M21) already
consumed the operator before calling the handler, so there is no token left to ask "was
this parenthesized" -- the byte scan is the only road open. It skips whitespace back past
the operator itself (`oplen` bytes: 1 for `.` and `[`, 2 for `?.`) and the space before it,
checks for a `)`, matches it back to its `(` (parens counted; ceiling 1 below), and peels
any further redundant `(` right after that one -- `((-t))`'s outer pair wraps `(-t)`
itself, so the peel is not optional -- until it finds `- ! ~` or gives up. The three call
sites (`tk_dot` teko_expr.tk, `tk_bracket` teko_params.tk, `tk_qd_infix` teko_null.tk) gate
their existing sink on `!tk_unary_parened(oplen)`; the sink's own shape (`tk_unary_base`,
`tk_unary_rewrap`) is untouched.

**Ceiling 1 (documented, not fixed): a string literal or a comment between the group and
the operator is not read as one.** The scan is bytes, not tokens -- a `(`/`)` inside either
is counted as a real paren. Nothing in this repository's fixtures or library files puts one
there, and a lexically-aware scan would need the lexer's own token stream, which a
`syntax_infix` handler receives only going FORWARD (`p_skip_balanced`'s own road, mc core);
going backward past an already-consumed token, there is none.

**Ceiling 2: the scan gives up after `TK_UNARY_PAREN_MAX` (4096) bytes without closing a
paren at depth 0**, answering "not parenthesized" -- today's sink -- rather than reading
arbitrarily far back with no floor. Far more than any real parenthesized unary receiver
spans; a floor this wide costs nothing measurable and keeps the scan inside the buffer
`p_src_end()` already bounds going forward.

**Neither refuse fixture needed a new refusal.** `(-arr)[0]` and `(-oc)?.M()` both
already end at `teko: <row> declares no operator \`-\`` (`tk_op_none_msg`,
`teko_ops.tk`) -- the SAME pre-existing rule that already refuses `i64[] narr = -arr;` and
`!oc`/`-oc` on any nullable class today. The fix only changes which node that rule is asked
about: before, `(-arr)[0]` sank to `-(arr[0])`, which COMPILED and answered the silently
wrong value (measured on `758ad9ce`: exit 251, `-(arr[0])` = -5); `(-oc)?.M()` sank to
`-(oc?.M())`, refused too, but naming `i64?` -- the boxed RETURN of `M()`, never the `Cell?`
a reader actually negated. After the fix, both refuse pointing at the receiver the source
wrote, `i64[]` and `Cell?`, at the line the source wrote it on.

**The readings table**, `t = TimeSpan.FromHours(-3)` throughout, measured on this branch:

| source | before (`758ad9ce`) | after |
|---|---|---|
| `(-t).Duration().Hours` | -3 (wrong) | 3 |
| `-t.Duration().Hours` | -3 | -3 (unchanged: no group to bind to) |
| `-(t).Duration().Hours` | -3 | -3 (unchanged: the `(` wraps only `t`, C#'s own reading) |
| `(-(t)).Duration().Hours` | 3 | 3 (unchanged: outer group wraps `-(t)` whole already) |
| `((-t)).Duration().Hours` | 3 | 3 (unchanged: one more redundant layer, peeled either way) |
| `(-arr)[0]` | compiles, -5 (wrong) | `teko: i64[] declares no operator \`-\`` |
| `(-oc)?.M()` | `teko: i64? declares no operator \`-\`` (wrong receiver named) | `teko: Cell? declares no operator \`-\`` (the real one) |
| `(-d).ToString()` (`decimal`) | measured: exit 139 (SIGSEGV) -- `d.ToString()` returns a `str`, an 8-byte pointer, and `tk_dec_neg` reads it as a 16-byte `decimal` | `"-1.5"` |
| `(-1m).CompareTo(1m)` (`decimal`) | 0 (`-(1m.CompareTo(1m))` = `-0`) | -1 |

Five of the nine rows move: the `TimeSpan` reading that used to answer the wrong VALUE
silently, the array and the nullable that used to compile wrong or refuse the wrong
receiver, and the two `decimal` rows (one of them a segfault). The other four stand exactly
where they stood: `-t...`/`-(t)...` had no group to bind to in the first place (the sink is
C#'s own reading there, unchanged), and the two redundantly-nested forms already collapsed
to the same AST either way. No existing fixture wrote the parenthesized form, so nothing
already accepted moves.

**The gate.** `mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh
./build/teko mc.macos.toml` -> **99 passed, 121 refused as expected, 0 failed** (98 + 1 new
accept fixture, 119 + 2 new refuse fixtures); `sh scripts/bootstrap.sh --os macos --arch
aarch64` -> **FIXPOINT OK**; `sh scripts/check-docs.sh` -> `docs ok: 683 links, 67
fragments, 406 diagnostics, 121 refusals, 151 samples, manifest listed`.

`--dump-ast` of every fixture that predates this crumb (`758ad9ce`, `--include=lib
--include=tests[/refuse]`, single-file mode, base and this crumb each its own worktree):
**98 of 98 `tests/*.tk` and 119 of 119 `tests/refuse/*.tk` byte-identical** -- every
pre-existing program, accepted or refused, parses to the exact same tree either compiler
builds. None of them writes the parenthesized form the probe reads, so the probe never
fires on any of them; this is the no-op proof.

`mc build . --config mc.macos.toml --compiler-only --limits`, `rm -rf build` first on both
legs, `758ad9ce` (before) vs this branch (after): `intrin` 0 -> 0, `types` 1 -> 1, `syntax`
0 -> 0, `alias` 1 -> 1, `passes` 0 -> 0 -- every budget row D21 (zero new intrinsics) is
measured against, unmoved. `nodes` 170902 -> 171117 (+215), `funcs` 3384 -> 3386 (+2, the
two functions this crumb adds -- `tk_unary_ws`, `tk_unary_parened`), `ins` 235739 -> 236053
(+314), `symbols` 6695 -> 6697 (+2). `heap` (the one row `mc limits` reports in bytes, never
elements) moved from 101088544 to 101245504, +156960 bytes, +0.16% -- stated as what it is,
not offered as proof of anything: the proof is the readings table above, the three new
fixtures, and the 217 pre-existing fixtures' `--dump-ast` unmoved.

---

### D81 · `i128` and `u128` are teko's own two registrations, `<i128>` is never included, and the limb vector is lifted rather than copied (N6a, 2026-09-15)

> Two `type_new` calls, one literal handler, twenty-two operator rows, eight conversion
> rows and about 340 lines of surface teko in `lib/wide.tk` -- and `mc`'s bundled `<i128>`
> is not included, now or ever. Measured: `<float>` and `<i128>` cannot coexist in one
> compiler, the module adds four intrinsics D21 forbids outright, and its machine handlers
> break 34 of this repository's fixtures. teko registers the two types itself, over the one
> `teko_wide.tk` machine C3 built, with **nothing new in that file**. `mc limits`' `intrin`
> row is 8 before and 8 after.

N6a is `docs/specs/small-ints.md` § 6's first half. It depends on C3 (D74) for the
sixteen-byte machine and on C4 (D77) for the limb technique -- and it duplicates neither,
which is the point of its first ruling. N6b (`% << >> & | ^ ~`, `ToString`/`Parse`/
`TryParse`, the members, `↔ decimal` and `↔ f64`) is the NEXT crumb and is left out on
purpose; every one of its spellings is refused by name today and is a row of
`docs/reference/not-yet.md`.

#### The ten rulings this crumb was dispatched with, and what each became

1. **`<i128>` is closed, and this entry is what closes D74's "N6 re-decides".** D74 left
   `#include <i128>` out because it reserves the words `i128`/`u128` ahead of N6's own
   crumb, and said N6 would re-decide. It is decided: **never**. Three independent reasons,
   each sufficient on its own. The opcode ranges COLLIDE on x86_64 -- `<float>`'s `FX_BASE`
   and `<i128>`'s `XW_BASE` are both 100, so a plain `f64 a + a` dies in `--dump-asm` with
   `no dump for a wide opcode` whichever module is initialised first -- and on arm64 the
   measured init order `i128_init` then `machine_arm64_float_init` executes an illegal
   instruction on `i128 y = x * 3i` (mc 0.17.2's `fa_mine` is bounded by `FI_MAXOP` 142,
   so the arm64 half is a measurement, not a range claim; mc fixed both in its #93 by
   delimiting every band). It adds
   **four intrinsics**, which D21 forbids without qualification. And its `iw_*` machine
   handlers claim the sixteen-byte depth `teko_wide.tk` already owns, which breaks 34
   fixtures. teko's own registration is `teko_i128.tk`, beside `teko_decimal.tk`;
   `teko_wide.tk` gained one raised `#define` and not one line of logic, which is the proof
   D74's machine generalises to a fourth and a fifth client.

2. **The limb vector is LIFTED, not copied.** C4's `tk_dv_*` block -- eight 32-bit limbs and
   the sixteen operations over them, including the one long division -- moved out of
   `lib/decimal.tk` into `lib/limbs.tk`, which `decimal.tk` includes at the very line the
   block used to start on and `wide.tk` includes too. A second `tk_dv_divmod` was not
   written. The proof it is a no-op is stronger than the ruling asked for: `--dump-ast
   --include=lib --include=tests` over **all 220 files under `tests/`** is **byte-identical**
   to `a1d9ca46`, declaration order included -- the include sits exactly where the block sat,
   so nothing even reorders and there is no sorted diff to enumerate. It is its own commit
   (`846d88d2`), gated on its own.

3. **The limbs stay 32 bits wide, and now for TWO reasons.** C4's was headroom. N6a's is
   mc's own defect: **mc compares every integer SIGNED, `u64` included** (measured on mc
   0.17.2, reported to `minicompiler/mc`, fix expected in 0.17.3), so `a < b` on the two
   `u64` halves of a 128-bit value answers backwards the moment bit 63 is set. A limb below
   2^32 has no such bit. Every compare in `lib/wide.tk` is a limb compare or an explicit
   `(limb3 >> 31) & 1` sign test, so the whole file is right on both sides of the defect and
   stays right when it is fixed. `lib/limbs.tk`'s header says so in the file, not only here.

4. **The literal is `<digits>i` / `<digits>u`, decimal digits only, registered before
   `tk_float_init()`.** `mc`'s own `<i128>` spelling, both suffixes case-insensitive; C# has
   no `Int128` literal at all. The mechanism is `tk_dec_lit`'s: a module-private global with
   an `N_BLOB` initializer, gensym'd `$tk_w128_<n>` with a `$` the lexer never forms into an
   identifier, and the node returned is the `N_IDENT` naming it. It carries the MAGNITUDE
   only -- a leading `-` is the unary operator applied to the value -- so the ceiling is
   2^127-1 for `i` and 2^128-1 for `u`, checked on the four limbs at compile time with the
   carry remembered rather than dropped: `teko: an i128 literal is out of range` /
   `teko: a u128 literal is out of range`. `i128.MinValue` is written
   `-170141183460469231731687303715884105727i - 1i` until N6b registers the constant, which
   is the same hole C# has and fills with `Int128.MinValue`.

5. **Eleven operator rows per type, no mixed row, and the promotion through D77's own
   door.** `tk_ops_promote`'s wide arm needed no change at all: it asks `tk_num_widens`,
   which asks the primitive's own conversion table, so registering the four integer-source
   rows made `x + 1`, `1 + x`, `x += 1`, `x++` and `x--` work with no line in `teko_ops.tk`.
   `i128 op u128` gets no row and is refused -- ``teko: no operator `+` takes these
   operands`` -- because `tk_is_int_ty` answers 0 for every wide id (D38's rule), so no
   promotion opens a door between them; C# refuses the same expression without a cast.
   Unary `+` has no row either: `tk_prim_unary_plus` reads the `T + T` row and hands the
   operand back.

6. **The conversion rows are keyed on the source's SIGNEDNESS, and there are four and not
   two.** `tk_i128_from_u64` before `tk_i128_from_i64`, and the same pair for `u128`: a
   column naming `TY_I64` takes every integer teko has, `u64` included, and a `u64` at or
   above 2^63 through a sign-extending door is a negative 128-bit value in silence. That is
   D77's ruling 8 exactly, applied at registration time rather than found by a verifier. The
   two wide-to-wide rows move no bit. The `(u64) x`, `(i32) x` and every other narrow target
   ride the single `(TY_I64, i128)` row: the call answers an `i64` and the cast the source
   wrote is the ordinary narrowing over it -- measured, `(i32) x` on a 6 answers 6 and
   `(i64) 18446744073709551616i` answers 0, which is C#'s unchecked narrowing.

   **The one place this is wider than C#.** `u128 x = -1;` is accepted here and answers
   2^128-1, where C# makes every signed source an explicit cast to `UInt128`. The table has
   no implicit/explicit column and teko's own `u64 x = -1;` has always been accepted for the
   same reason -- there is no `checked` word to tell a wrapped conversion from a wrong one.
   Recorded in `not-yet.md` rather than fixed with a column a single type would use.

7. **`x / 0i` panics `teko: division by zero`, exit 70 -- and the wording joins no family,
   because there is none.** The ruling asked for a measurement of what `i64 / 0` does in
   teko today. It does **nothing**: there is no guard at all, the machine's own `sdiv` runs,
   and aarch64 answers 0. So `decimal division by zero` is the only neighbour and it carries
   a type word this one does not need. The divisor is tested BEFORE the long division,
   because `tk_dv_divmod` over a zero divisor answers every bit set rather than failing, and
   a wrong quotient is worse than an abort.

8. **The caps, and one wording fix that fell out.** `TK_MAXWIDE` 4 -> 8 (the two
   registrations filled the old table exactly), `TK_MAXPRIMT` 8 -> 16 (6 -> 8, same),
   `TK_MAXPRIMO` 80 -> 128 (62 -> 84), `TK_MAXPRIMX` 8 -> 16 (5 -> 13, which the ruling did
   not name and the build found at once). And `tk_prim_cast_check`'s refusal opened
   `"teko: a "` unconditionally, which reads *a i128*. `tk_prim_article` answers *an* before
   a vowel SOUND and not a vowel letter -- *an i128*, *a u128*, "you-128", the rule that
   gives *a union* -- and every primitive registered before this crumb begins with a
   consonant, so `decimal`, `Guid`, `DateTime`, `TimeSpan` and `DateTimeOffset` read exactly
   as they always did.

9. **Three run fixtures and nine refusal fixtures.** `tests/primitives_i128.tk` (42, 40
   assertions) walks the value through a local, a parameter, a return, a recursive return, a
   class field, a struct field, a fixed array element, a heap array element, a global, a
   `ref` pointee, an `out` pointee, and a `decimal` in the same program -- three wide types,
   three return buffers, one machine. `tests/primitives_i128_math.tk` (42, 68 assertions) is
   the arithmetic, every value chosen so a 64-bit implementation gives a different answer.
   `tests/primitives_i128_divzero.tk` (70) puts the zero behind a call the folder cannot see
   through. The nine under `tests/refuse/` are the literal range in both types, the mixed
   signedness, the two N6b casts, `const`, a `case` label, an `extern` and a global
   initializer. Both run fixtures were **proved live by mutation** -- a value moved by one
   answers the assertion's own code and not 42 -- and no failure path returns 42 (D77's
   ruling 11, checked on both).

10. **This entry is D81**, after D80.

#### The gate, mc 0.17.2, macos/aarch64

`mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` -> **102 passed, 130 refused as expected, 0 failed** (99 + 3, 121 + 9);
`sh scripts/bootstrap.sh --os macos --arch aarch64` -> **FIXPOINT OK**;
`sh scripts/check-docs.sh` -> `docs ok: 694 links, 71 fragments, 409 diagnostics, 130
refusals, 152 samples, manifest listed`.

`mc limits . --config mc.macos.toml`, `rm -rf build` first on both legs, base `a1d9ca46`
against this branch: `intrin` **8 -> 8 (unmoved, the law's own row)**, `passes` **15 -> 15
(unmoved)**, `syntax` **18 -> 20 (+2**, the two type words, each counted once by name
however many `syntax_expr`/`syntax_stmt` pairs it takes; `syntax_lit` is not counted
there**)**, `types` **16 -> 18 (+2**, the two `type_new` rows**)**, `alias` **23 -> 25 (+2**,
the side effect of `type_new` reserving each word, exactly as D74 recorded for
`decimal`**)**. Verdict `grew` on both legs with the SAME four rows `grew` before this crumb
(`passes`, `syntax`, `alias`, `types`) and **no new one**.

`--dump-ast --include=lib --include=tests` over all 220 `.tk` files under `tests/` (99
accept + 121 refuse, as they stand on `a1d9ca46`), base compiler built in its own worktree:
**220 of 220 byte-identical**, after the limb lift and after the compiler change alike. The
`decimal` fixtures included: the lift reorders nothing, so the sorted-diff enumeration the
ruling asked for is empty in the strongest sense -- there is no diff at all.

What is left open: N6b, whole, and the `u128` implicit-conversion width of ruling 6.

### D82 · integer division by zero, and `MinValue / -1`, panic on every leg -- the machine's answer is no longer the language's (2026-09-15)

Measured on `main` (`8d95c1ff`), aarch64 and x86_64 alike: a plain int `/`/`%` (`i64`,
`u64`, `i32`, `u32`, `i16`, `u16`, `i8`, `u8`, every core width teko has) reached the machine's own
`sdiv`/`idiv` untouched (`tk_ops_binary`, teko_ops.tk, `if (sa < 0 && sb < 0) return;`).
`sdiv` (aarch64) answers `0` for `x / 0` and `x` for `x % 0`, in silence; `idiv` (x86_64)
raises `#DE`, SIGFPE, exit 136 on Linux; `MinValue / -1` overflows the quotient register on
x86_64 while aarch64 answers `MinValue` back, also in silence. Three different answers to
the same source depending on which CI leg ran it, and none of them is C#'s: D3 says the
surface follows C#, which throws `DivideByZeroException` for the first and, on **both** its
own ISAs, `OverflowException` for the second (`int.MinValue / -1` throws under the CLR on
x86 and ARM alike, the runtime itself absorbing the ISA's own difference where teko had not
yet). `decimal` (`lib/decimal.tk`) and `i128`/`u128` (`lib/wide.tk`, D81) already panicked
`division by zero`; only the eight core widths were still the machine's own word.

1. **`mc` is target-defined here, and says nothing for the channel — so this is taught, not
   reported.** `docs/core-language.md:86-108`, `docs/reference/language.md:273-279` and
   `machine.md:444-448` all describe `/`/`%` by the ISA's own instruction, with no promise
   about a zero divisor or an overflowing quotient; the mc project's own notices file has
   nothing open on it either. D2's rule is a defect gets a reproducer and a report; a
   deliberately unspecified corner the surface language wants to promise something over is
   the ordinary work of a taught delta (D3), the same reasoning `decimal`'s and `i128`'s own
   guards already used.

2. **The door is `teko_ternary.tk`'s own pass, extended, not a new one.** `tk_ternary_pass`
   used to skip its whole walk when a unit wrote none of `?`, `??`, `?.` (`tk_ntern == 0 &&
   tk_nl_nops == 0`) — a division needs no hook of its own to be COUNTED (`/`/`%` are core
   grammar, no `syntax_infix` fires for either), so there is no cheap signal to test instead
   of walking, and the bail is gone: the pass now visits every function unconditionally, the
   same way `teko_rc.tk`'s own walk always has. `tk_tern_scan`'s existing generic recursion
   (already visiting every child of every node when nothing else claims it) is the SAME walk
   a division's own operands ride — a guard is built AFTER the recursion into `nd_a`/`nd_b`,
   so a ternary or a nested division inside either operand is already lowered into a plain
   reference by the time the guard reads its type; asking earlier would answer -1 for an
   unresolved `tk_ternary(...)` placeholder and silently skip the guard. `mc limits`: `passes`
   and `on_stmt` are BYTE FOR BYTE unmoved (measured against `8d95c1ff`, below) — the walk
   this crumb needed already existed.

3. **A LITERAL divisor is never guarded, zero or nonzero; a literal zero is answered where
   it is written (amended by the PR's review, finding F3).** `x / 2` is left exactly as the
   core wrote it — `nd_kind(nd_b(n)) == N_INT` returns before anything is built — which is
   what keeps `--dump-ast` unmoved for the common case (measured: enumeration below). The
   first draft of this ruling guarded a literal ZERO instead ("an unconditional panic once
   built, correct if unusual"), and that was wrong twice over, both measured on the branch's
   own first head:
   - `return 12 / 0;`, BOTH sides constant, used to be mc's own compile-time refusal
     (`division by zero`, no `teko:` prefix, `fold_binary`/`const_bin`, `mc/src/parse.mc`)
     and the guard HID it: `fold()` runs after every `pass()`, so once a guard was built
     around that node the folder never saw it and the program compiled, panicking 70 at run
     time. Returning early on any literal hands the case straight back to mc — measured:
     `12 / 0` and `12 % 0` refuse exactly as they do on `8d95c1ff`.
   - `a / 0`, a dividend the folder cannot see through, is folded by mc NOT AT ALL (measured
     on `8d95c1ff`: exit 0 on aarch64, `idiv`'s own SIGFPE on x86_64) — the very split this
     entry exists to close, so leaving it unguarded and unanswered was not an option either.
     C# refuses that line outright (CS0020, *Division by constant zero*, whatever the
     dividend is) and D3 follows C#: it is the refusal `teko: division by zero`, at the line
     the division is written on, with no run-time guard built
     (`tests/refuse/divzero_literal.tk`). The wording is the run-time panic's own — the same
     cause, answered earlier because the source already says it.

4. **The divisor, and (signed, non-literal) the dividend, are each read at most twice, so
   each is hoisted into a temporary exactly when it is not already safe to read twice.** A
   bare name is read again as it stands; the temp declared for anything else consumes the
   original node as its own initializer (so it runs exactly once) and the division's own
   operand slot is rewritten to a fresh reference to the same temp — the node just consumed
   cannot sit in two places in the tree. The overflow check (`b == -1 && a == MinValue`)
   reads the dividend only for a SIGNED type (`type_signed`, mc's own predicate) with a
   non-literal divisor; an unsigned width builds no overflow check and never reads the
   dividend a second time at all. `MinValue` is built per WIDTH (`type_width`, not a fixed
   `i64`), computed as `0 - (1 << (w*8-1))` in the host's own two's complement arithmetic —
   the identity `-MinValue == MinValue` (D81's own fixed point) makes the SAME formula answer
   right at every width from 1 to 8 bytes without a per-width branch, so `i32.MinValue /
   -1` panics through the identical code path `i64.MinValue / -1` does
   (`tests/surface_divguard.tk` exercises the narrow width at a divisor OTHER than `-1`,
   proving the guard does not false-positive there; a second exit-70 fixture for a second
   width would prove nothing the formula's own uniformity does not already).

5. **`panic` is program code, not something every unit carries — the guard checks that the
   `panic` it is about to call is the RUNTIME's own, and refuses when it is not.** The first
   draft asked `decl_find("panic") >= 0` and nothing else, which proves only that a
   declaration of the name exists: measured on the branch's own first head, a program with
   no `#include "rt.tk"` declaring `i64 panic(str) { return 0; }` compiled into a guard that
   called that no-op, so `a / 0` ran on and the program exited 99 with no abort (the PR's
   review). The declaration node `decl_find` answers with now has to have been READ FROM
   `lib/rt.tk` (`nd_file` of that node, suffix-matched by `tk_div_ends`) — measured: a
   fixture writing `#include "../lib/rt.tk"` and `lib/decimal.tk` writing `#include "rt.tk"`
   both announce the declaration under exactly `lib/rt.tk`, since `lex_file` normalises the
   path. Anything else takes the same refusal the missing include takes
   (`tests/refuse/divzero_own_panic.tk`). The other half is mc's own and is left there,
   measured: a program that declares its own `panic` AND includes the runtime is `function
   declared twice`, ahead of this pass.
   Measured: none of the compiler's own ~11 real divisions (`teko_*.tk`/`core_teko.mc`/
   `user.mc`, every one of them by a `#define`d constant or the literal `10` of a digit
   extraction) has a non-literal divisor, so the guard never actually fires while teko
   compiles its own source and `panic` never needs to be declared there — `tests/hello.tk`-
   style code with no `#include` at all keeps compiling. A PROGRAM that divides by something
   the folder cannot see through and never wrote `#include "rt.tk"` (directly or through
   `decimal.tk`/`time.tk`/...) is refused `teko: an integer division needs #include rt.tk`
   instead of failing later with "call to unknown function: panic" — the third road the
   crumb's own task asked to choose between, decided in the program's favour: an assumption
   that `<sys>`'s raw `write`/`exit` are always available is not sound either (a program with
   no `#include` at all, `tests/hello.tk`'s own shape, has neither).

6. **mc's own bundled core needed a SEPARATE exclusion, found only by running the fixed
   point on every OS, not by reasoning about it first.** The fixed point
   (`scripts/bootstrap.sh`) failed three times, on three different shapes, before this
   ruling closed:
   - `src/arena.mc`'s own `buf_pad` (`<mc/core_min>`, reached from `mc_teko.tk`) divides by a
     PARAMETER (`buf_len(b) % align`) and is announced under a bare canonical name
     (`mc/arena`, no directory, no extension — hooks.md's own "the canonical bundled name...
     for a bundled one"). `tk_origin_of_file` (teko_access.tk, the SAME oracle `internal`
     reads) answers this WRONG: with the bare (no-directory) entry and config
     `scripts/bootstrap.sh` derives from the repository root, its own fallback ("the project
     is the whole [current] directory") calls `mc/arena` project code too, since it does not
     start with `.`. Fixed by filtering on the SAME `.tk`-suffix oracle `source_claim`
     already uses (`tk_fwd_is_source_name`, teko_fwd.tk), `.mc` joining it for
     `core_teko.mc`/`user.mc` — every bundled canonical name ends in neither.
   - mc's own LIBRARY TREE, resolved onto disk rather than carried in the blob
     (`lib/machine_arm64_float.mc`, reached the same include), announces a REAL path with the
     SAME extension this project's own files carry, so the suffix filter alone answered 1
     for it too — needing a second line, not a replacement for the first.
   - That second line was first written as `tk_origin_of_file`'s own absolute-path check (an
     absolute path is never this project's own, that function's own first line) — right on
     macOS and Linux CI, and WRONG on the Windows leg: a Windows runner's own resolved path
     starts with a drive letter (`D:/a/teko-lang/...`), not `/`, so the check never fires
     there and the leg failed exactly the same way, on exactly the file the macOS leg had
     already cleared. The fixed, portable answer does not try a third spelling of "absolute
     path": mc's own docs name the layout directly (`docs/reference/bundle.md`, "its
     standard library in a tree... `lib/mc/v<version>/`"), stable across every OS this
     project targets, so `tk_div_project_file` (teko_ternary.tk) checks the substring
     `lib/mc/v` — present in the Linux/macOS path (`/…/build/lib/mc/v0.17.2/lib/…`) and the
     Windows one alike (`D:/a/…/build/lib/mc/v0.17.2/lib/…`), since `mc` itself normalises
     the separator to `/` on every host (measured on all three). The final oracle is: a
     path naming mc's own resolved library tree is excluded outright; otherwise a name
     ending `.tk` or `.mc` is this project's own. D2 draws the line at mc's own
     bundled/resolved core either way — a defect there (if `buf_pad` genuinely divides by
     zero somewhere) is minicompiler/mc's to report, never teko's to silently reinterpret.
     This project has no `[deps]` yet; a locked package resolved to some OTHER root would
     need a line of its own, not needed today.

7. **`lib/wide.tk`'s `i128` amends D81's ruling 3: `MinValue / -1i` panics too, and `%` was
   never a row to amend (D81's own eleven-op table has no `%` for either wide type — N6b's).**
   Measured before writing the fixture: NOTHING in this repository ever asserted the wrap
   `tests/primitives_i128_math.tk`'s own header claimed for it (`-7i / 2i == -3i` is the only
   division row there; "the WRAP at 2^127" section tests `+ - *`, never `/`) — so no row
   moved, only the missing assertion was added
   (`tests/primitives_i128_divovf.tk`). `tk_w_sdiv_ovf` reads the two ORIGINAL 128-bit
   operands directly, ahead of the magnitude split every divide takes: `MinValue` is bit 127
   set and nothing else, `-1` is every bit set, both by the same 32-bit limb layout
   `tk_w_isneg` already reads. `u128` gets no such check: an unsigned divide never overflows
   its own width.

8. **Eight fixtures that run, three that are refused (amended by the PR's review, F4).**
   `tests/surface_divzero.tk` (70), `tests/surface_remzero.tk` (70),
   `tests/surface_divovf.tk` (70, `i64.MinValue / m` with `m` a parameter),
   `tests/surface_divguard.tk` (42: every width with a non-literal divisor, truncation
   toward zero, `MinValue / 1`, a narrow width's own `MinValue` divided by something other
   than `-1`, a divisor with a side effect evaluated exactly once, both operands with side
   effects evaluated left to right and once each on an unsigned width, `u64.MaxValue / 2`, a
   literal divisor path left untouched, and — ruling 9 — the `&&`/`||`/`while`/nested rows
   whose division is never reached, side-effecting lazy divisor included),
   `tests/surface_divlazy.tk` (70: the same lazy division reached through a TRUE left side),
   `tests/surface_divlazy_value.tk` (42: the `&&`/`||` temporary is a truth value, ruling 9),
   `tests/surface_divpanic_overload.tk` (42: a `panic(i64)` of the program's own ahead of the
   include, ruling 10),
   `tests/primitives_i128_divovf.tk` (70). Refused: `tests/refuse/divzero_no_rt.tk` (the
   missing include), `tests/refuse/divzero_own_panic.tk` (a `panic` of the program's own,
   ruling 5) and `tests/refuse/divzero_literal.tk` (`a / 0`, ruling 3). The first draft
   wrote no refusal fixture at all, on the argument that every fixture needing `panic`
   covers the include by construction — it does not: an oracle no fixture pins is an oracle
   nothing measures, and the review found two real defects behind exactly that gap (D52).

9. **A division in the LAZY operand of `&&`/`||` is guarded inside the branch that reaches
   it, by lowering the operator into the branch form `?:` already takes (amended by the PR's
   review, finding F1).** `tk_div_guard` appends its check to the list the enclosing
   STATEMENT is preceded by, which is right for a division that always runs and wrong for
   one written behind a short-circuit: measured on the branch's own first head,
   `if (n != 0 && a / n > 1)` with `n == 0` exited 70 where `8d95c1ff` answers 42, and `||`
   and a `while` condition alike. `?:`, `??` and `?.` never had the defect for one reason —
   each is lowered into a REAL BRANCH before the walk reaches its arms, so every hoist an
   arm needs lands in that branch's own list. So `&&`/`||` takes the same road, ahead of the
   guard walk (`tk_div_lazy_lower`, teko_ternary.tk):

       x && y  ->  i64 t = 0; if (x) { <y's own hoists>; t = y != 0; } else { t = 0; }
       x || y  ->  i64 t = 0; if (x) { t = 1; } else { <y's own hoists>; t = y != 0; }

   The temporary holds the operator's OWN value, a normalized truth value, never the right
   operand itself: `n != 0 && a / n` is 1, not the quotient (the first draft assigned `y`
   raw and answered 3 for `7 / 2`; `tests/surface_divlazy_value.tk` pins it).

   Amended by the PR's fourth review, after the merge (the follow-up PR): a guarded
   division's DIVIDEND is hoisted before its divisor is walked, so a divisor that carries
   preludes of its own — a nested guarded division, a ternary — evaluates after it:
   `left() / (x / y)` runs `left()` first, as C# does; `tests/surface_divnested.tk` pins the
   order. `MinValue % -1` panics like `MinValue / -1` and stays that way: .NET throws
   `OverflowException` for `int.MinValue % -1` on x64 and arm64 alike (the C# specification
   ties the remainder's overflow to the quotient's), and `tests/surface_removf.tk` pins it.
   The `tk_div_lazy_ty` ceiling stated above is narrower than "same behaviour": an `&&`
   whose right side is a division typed only after this walk (both operands behind `?:`
   placeholders, resolving to `decimal`) takes the branch form, and the `y != 0` it writes
   is then a decimal comparison where the plain operator would have been refused as an
   `&&` over a primitive — a program that was refused is accepted with the comparison's own
   answer. No fixture has that shape; the oracle that closes it is a type for a placeholder
   before its lowering, which this walk does not have.

   The LEFT operand always runs and keeps its own guard at the statement. The temporary is
   `TY_I64` outright — the truth value every comparison, `!`, `&&` and `||` already carries
   (`tk_bool_lit`, teko_type.tk) — so `tk_tern_lower` is not reused as it stands: it types
   the two arms and would refuse a `bool`-returning right side against a plain `false`.
   ONLY an `&&`/`||` whose right operand carries a division that needs a guard is rewritten
   (`tk_div_lazy_needs`, the guard's own predicate read one step early): measured, the
   `--dump-ast` of all 237 fixtures as they stand on the branch's first head is byte for
   byte identical under this head. The one deliberate imprecision: an operand still holding
   a `?:`/`??`/`?.` placeholder has no type yet (`tk_ty_of` answers -1) and counts as
   possibly-integer, so an `&&` whose right side divides two DECIMALS reached through a
   ternary would take the branch form too — same behaviour, a moved dump, and no fixture in
   the corpus has that shape.

10. **F5, left as it is with its ceiling stated: `tk_div_project_file` excludes mc's own
    resolved library tree by the hard-coded substring `lib/mc/v`** (ruling 6). A project of
    its own that happened to keep sources in a directory literally named `lib/mc/v…` would
    be excluded from the guard along with mc's, and a locked `[deps]` package resolved to
    some third root is not covered either (this repository has no `[deps]` yet).
    The runtime's own `panic` (ruling 5) is recognised the same way, by the suffix
    `lib/rt.tk` of the file its declaration was read from, over EVERY declaration of the
    name in the unit (`tk_div_rt_panic`, not `decl_find`'s first hit: an unrelated overload
    such as `panic(i64)` declared ahead of the include leaves the runtime's `panic(str)`
    declared and selectable, `tests/surface_divpanic_overload.tk`). That suffix is a
    ceiling too: an include root of the project's own that holds a `vendor/lib/rt.tk` with
    a no-op `panic` passes it, and the guard then calls that no-op. A project that ships a
    counterfeit runtime under the runtime's own path gets what it wrote; the oracle that
    would tell the two apart is the same one named below.
    `tk_origin_of_file` was measured and is NOT the oracle to replace it with — ruling 6
    records both ways it answers wrong for this question (the bare bundled name, and the
    Windows drive-letter path). A real oracle would be mc's own: a hook naming the resolved
    library root, which is minicompiler/mc's to offer and not this repository's to invent.

11. **This entry is D82**, after D81.

#### The gate, mc 0.17.2, macos/aarch64

`mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` -> **112 passed, 133 refused as expected, 0 failed** (102 + 10, 121 + 12 --
the eight that run and the three that are refused of ruling 8 plus the two of the fourth
review's amendment, over the 102 + 121 the base
carries); `sh scripts/bootstrap.sh --os macos --arch aarch64` -> **FIXPOINT OK** (stage 1
through 3 each compile `mc_teko.tk` clean, `teko2.o == teko3.o`, `--dump-asm` diff empty,
teko1 compiles all 112+133 fixtures at its own oracle); `sh scripts/check-docs.sh` -> `docs
ok: 698 links, 74 fragments, 411 diagnostics, 133 refusals, 152 samples, manifest listed`.

`mc limits . --config mc.macos.toml`, `rm -rf build` first on both legs, base `8d95c1ff`
against this branch: `passes` **15 -> 15 (unmoved)**, `on_stmt` **4 -> 4 (unmoved)**,
`intrin` **8 -> 8 (unmoved)**, `syntax` **20 -> 20 (unmoved)**, `alias` **25 -> 25
(unmoved)**, `types` **18 -> 18 (unmoved)** — every row this crumb could have grown stayed
exactly where D81 left it; no `type_new`, no `syntax`/`syntax_infix`, no new `pass()` or
`on_stmt()` registration anywhere in this crumb.

`--dump-ast --include=lib --include=tests` over all 232 `.tk` files under `tests/` as they
stand on `8d95c1ff`, base compiler built in its own worktree: **195 of 232
byte-identical, 37 differ**. (An earlier pass of this measurement read 3 differing, from a
`tk_origin_of_file` draft of ruling 6 that also excluded `lib/decimal.tk`/`lib/limbs.tk`/
`lib/time.tk` by their own bare `#include`d name — the false-negative twin of the
`mc/arena` false positive it was written to fix, corrected once the portable oracle
landed.) Every one of the 37 is fully explained by two sources and nothing else:

- **Ruling 7's own `tk_w_sdiv_ovf`** — every fixture that `#include`s `wide.tk` gains the
  new function and its one call site in `tk_w_sdiv`.
- **Three EXISTING library helpers this crumb's guard now also reaches**, because they are
  this project's own code (`lib/limbs.tk`, `lib/time.tk`) dividing by a PARAMETER, exactly
  the shape ruling 2-4 guards anywhere else: `tk_dv_divs` (`lib/limbs.tk`, shared by
  `decimal.tk` and `wide.tk`, `cur / d` and `cur % d`, unsigned — one zero-check each, no
  prior guard at all) and `tk_ts_mul`/`tk_ts_div` (`lib/time.tk`'s `TimeSpan` arithmetic,
  `r / n` and `a / n`, signed — a zero-check AND an overflow-check each). The last two
  already special-cased `n == 0` and `n == -1` by hand before ever reaching the division
  (`tk_ts_div`'s own header: "`-1` is taken out of the way BEFORE the division that checks
  the product") — this crumb's guard is provably DEAD CODE there, redundant with logic
  already proven correct, never fires, and does not change one exit code. `tk_dv_divs` had
  no such hand guard and is the one place this crumb adds a genuinely NEW safety net inside
  the standard library rather than only at a program's own division.

Every fixture that transitively reaches one of those three functions — every `decimal`,
`i128`/`u128`, `DateTime`/`DateOnly`/`TimeOnly`/`TimeSpan`/`DateTimeOffset` fixture in the
corpus, 37 of them — shows the SAME small, fully-accounted diff (2, 4 or 6 new `panic`
call sites depending on which of the three functions it reaches, 3 for the `i128`/`u128`
trio which also carries ruling 7's own addition). **Not one of the 37 changed a single exit
code** (`sh scripts/fixtures.sh`, above: 108 passed, 0 failed) and **not one of the other
195 fixtures' own AST moved** — every plain division written directly IN a fixture's own
source, rather than reached through one of those three library functions, divides by a
literal. This crumb changes accepted RUN-TIME behaviour on purpose (a program dividing a
non-literal divisor by zero now panics instead of reading the machine's own answer) without
moving a single byte of AST where the case that changed is not exercised, and with no exit
code moved anywhere it is.

The three rulings the PR's review added (3 amended, 5 amended, 9) were measured the same
way against the branch's OWN first head (`47fdb2f0`, its worktree and its compiler): the
`--dump-ast` of all **237 of 237** fixtures as they stand there is **byte for byte
identical** under this head. The `&&`/`||` branch form and the literal-divisor exit fire
only on source this head adds — nothing already in the corpus carried a division in a lazy
operand, and nothing in it divides by a literal zero.

What is left open: nothing new. The `#include rt.tk` road (ruling 5) is a design choice this
entry records rather than a gap; a program that needs it and does not have it is refused by
name, not left to guess.

---

### D83 · the seven remaining operators of `i128`/`u128` -- a shift count is the SAME wide type as the value, and `%`'s sign follows the dividend alone (N6b-1, 2026-09-15)

> `%`, `<<`, `>>`, `&`, `|`, `^`, `~` land on both wide types, over a bit-level shift and a
> limb-wise bitwise core added to `lib/wide.tk`, and fourteen new operator rows -- seven per
> type, registered from a SECOND function (`tk_i128_rows_bits`) because `mc` caps a call at
> twelve parameters and eighteen names plus the type does not fit the one `tk_i128_rows`
> already had. Zero mc changes, zero new intrinsics: every operator is ordinary teko over
> `lib/limbs.tk`'s long division and `lib/wide.tk`'s own eight-limb scratch. `mc limits`'
> `intrin` row is 8 before and 8 after; `TK_MAXPRIMO`'s fill moves from 84 to 98 of the 128
> the cap already had room for (D81 raised it there for exactly this).

N6b-1 is the first crumb of N6b, `docs/specs/small-ints.md` § 6's second half. It depends on
N6a (D81) for the eight-limb scratch and touches nothing else N6a built. The places where
`i128` and `u128` differ grow from two to four: `/` and the four orderings (D81), and now `%`
(the remainder's sign, ruling 2) and `>>` (arithmetic against logical, ruling 3); `<<`, `&`,
`|`, `^` and `~` are the same bits for both and join neither list. What is still N6b's own --
`ToString`/`Parse`/`TryParse`, § 7's members and statics, the `decimal`/`f64` conversions --
is left out on purpose and stays a row of `docs/reference/not-yet.md`.

#### The rulings this crumb was dispatched with, and what each became

1. **The shift count is the SAME wide type as the value, never `i64` -- because it has no
   choice.** `x << 2` reaches the operator table already promoted: `tk_ops_promote` runs its
   WIDE arm first for every operator (`teko_ops.tk`), so the bare `2` on the right becomes an
   `i128` before `tk_prim_op_find` ever looks at a row, and a `(t, TY_I64)` row would simply
   never be reached (`tk_prim_slot_fits`, `teko_prim.tk`). The row is `(t, t)`, same as every
   other binary one, and the WRAPPER (`lib/wide.tk`) is where the count is read back down to
   a machine shift amount: its own low 7 bits, `tk_i128_lo(cnt) & 127` (or `tk_u128_lo` for
   the unsigned side). This is .NET's own rule and not an invention -- `Int128`/`UInt128`'s
   shift operators mask their amount to 7 bits the same way, and a negative count falls out
   under the mask exactly as a count past 127 does: measured, `1i << negcnt` with `negcnt`
   every bit set answers `i128.MinValue` (`& 127` on all-ones is 127, and `1i << 127i` is the
   sign bit alone), and `1i << 129i` answers `2i` (`129 & 127` is 1).

2. **`>>` is arithmetic for `i128` and logical for `u128`, one core serving both.** The core
   below the wrapper (`tk_w_shr`) shifts the WHOLE eight-limb scratch right, filling from the
   top with whatever limbs 4..7 already hold -- zero, since `tk_w_vec` zeroes them on the way
   in, which is `u128`'s own answer with no extra step. `i128`'s own `tk_w_sar` fills limbs
   4..7 with the sign (`tk_w_isneg`, the same bit `tk_w_neg` already reads) BEFORE calling the
   same `tk_w_shr`, so the identical bit-by-bit move that pulls zero in for `u128` pulls one
   bits in here instead: one core, one fill, no second shift routine. `<<` needs no such
   split -- both types wrap modulo 2^128 the same way `+`/`-`/`*` already do -- and shares one
   `tk_w_shl` for both. Both cores shift the WHOLE scratch, not just the four live limbs, so a
   left shift's overflow past bit 127 lands in limbs 4..7 where `tk_w_mask` (every wrapper's
   own last line, as every other operator's already is) drops it -- the same wrap `docs/specs/
   small-ints.md` § 6 already fixed.

3. **`%`'s sign follows the DIVIDEND alone, never `sign(a) != sign(b)` as `/`'s quotient
   does.** `lib/limbs.tk`'s `tk_dv_divmod(q, a, b)` already leaves the remainder in `a` once
   it returns (N6a's own scout finding, unchanged since C4): a plain `tk_w_umod` is `tk_w_udiv`
   called for its side effect on `a`, the quotient `q` built and dropped. The signed core
   (`tk_w_smod`) takes the two operands' magnitudes exactly as `tk_w_sdiv` does, but restores
   the sign onto the REMAINDER using the DIVIDEND's own sign bit (`sa`) and never the
   divisor's -- `-7i % 2i == -1i`, `7i % -2i == 1i`, C#'s own rule for `%` and the one place
   this operator's sign differs from `/`'s. Truncating division needs `a == (a/b)*b + a%b`,
   which only the dividend's own sign on the remainder satisfies.

4. **`i128.MinValue % -1i` panics too, reusing `tk_w_sdiv_ovf` UNCHANGED -- D82's ruling 7
   said `%` was never a row to amend before this crumb added one, and this is that
   amendment.** The quotient `%` would need to compute the remainder from is the one value
   the width cannot hold, so `tk_w_smod` reads the ORIGINAL operands through `tk_w_sdiv_ovf`
   before either sign is touched, exactly where `tk_w_sdiv` already does -- `tests/
   primitives_i128_removf.tk` is the assertion D82's own entry noted `%` had never had.
   `u128` gets no such check: an unsigned remainder never overflows its own width, the same
   asymmetry `/` already has.

5. **`mc` caps a call at twelve parameters, so `tk_i128_rows` could not simply grow.**
   Eleven names plus `t` already filled it exactly; eighteen names plus `t` for the twenty-
   five-row table this crumb was first drafted as (measured: `teko_i128.tk:246: at most 12
   parameters`, `mc`'s own limit, not this repository's). The eleven-row function is
   untouched and a second one, `tk_i128_rows_bits`, carries the seven new names over eight
   parameters -- two calls per type instead of one, same shape, same `tk_prim_op` table
   underneath.

6. **The message drift `i128.MaxValue` had is fixed in passing, not left for a future
   crumb to trip over again.** Measured: `i128.MaxValue` (a static member N6a's own header
   already documented as unregistered) answers `teko: unknown static member of i128: MaxValue`
   (`tk_prim_no_static`, `teko_prim.tk`), not the `teko: unknown member of i128` three pages
   claimed (`teko_i128.tk`'s own header comment, `docs/reference/types.md`, `docs/reference/
   not-yet.md`) -- an instance-member and a static-member refusal are two different functions
   in this compiler (`tk_prim_no_member`/`tk_prim_no_static`) and always were; nothing here
   changes which one a static member reaches, only the three descriptions of it.
   `docs/internals/primitives.md`'s ceilings table is a second, older drift the same pass
   found: it still read `TK_MAXPRIMT` 8, `TK_MAXPRIMO` 80 and `TK_MAXPRIMX` 8 with "holds 5
   of 8", D81's own numbers from before it raised every one of the three caps to 16, 128 and
   16 -- `teko_prim.tk`'s own running `#define` comments already carried the true count and
   this page had simply fallen behind them. Fixed to the code's own numbers, with this
   crumb's own delta -- `TK_MAXPRIMO` 84 to 98 -- appended in the same running style.

**Proof of this crumb** (mc 0.17.5, macos/aarch64, head `dce502fb`, merged with `origin/main`
at `12bc95f9` -- D82's fourth review, `tests/surface_removf.tk` and `tests/surface_divnested
.tk` -- before this gate ran): `mc build . --config mc.macos.toml` clean; `sh scripts/
fixtures.sh ./build/teko mc.macos.toml` -> **115 passed, 133 refused as expected, 0 failed**
(112 + 3 new: `primitives_i128_bits.tk` at 42, `primitives_i128_remzero.tk` and
`primitives_i128_removf.tk` at 70; zero new refuse fixtures -- nothing this crumb adds is
refused by name); `sh scripts/bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`;
`sh scripts/check-docs.sh` -> `docs ok: 696 links, 74 fragments, 411 diagnostics, 133
refusals, 152 samples, manifest listed`; `mc limits` on both legs (the `tests/hello.tk` leg's verdict is `grew`, exit 3, identically
on base and head -- the budget tables diff clean), every row
against `12bc95f9` unmoved except the size-of-surface-code ones (`nodes`, `funcs`, `lowered`,
`ins`, `symbols`, `globals`) -- on the `hello.tk` leg `intrin` **8**, `passes` **15**,
`syntax` **20**, `alias` **25**, `types` **18**, `on_stmt` **4**, identical to the base;
`TK_MAXPRIMO` **98**/128 (84 before), every other primitive cap unmoved by this crumb.
`--dump-ast --include=lib --include=tests` (single-file mode, base `12bc95f9` and this
branch each its own worktree): **112 of 112 pre-existing `tests/*.tk` and 133 of 133
`tests/refuse/*.tk` byte-identical** -- no pre-existing program's tree moved a node, which
this crumb's own shape predicts: no row it added can fire on source that predates it, since
every one of the seven operators was a compile-time refusal on every such program before this
crumb and a lowered call after it, and nothing that used to refuse now silently accepts (the
133 refusals are the same 133, unmoved).

What is left open: the rest of N6b (`ToString`/`Parse`/`TryParse`, the members and statics,
the `decimal`/`f64` conversions) is the next crumb, unblocked and unchanged by this one.

### D84 · the `f64` and `decimal` conversions of `i128`/`u128`, both directions -- `(f64) x`
rounds ONCE over the whole magnitude, and a `double` source SATURATES (N6b-2, 2026-09-15)

> Eight new rows of the cast-to-call table (`teko_i128.tk`), `TK_MAXPRIMX` 16 -> 32: `(f64)
> v`/`(i128) x`/`(u128) x` (`lib/wide.tk`) and `(decimal) v`/`(i128) d`/`(u128) d`
> (`lib/decimal.tk`'s two, `lib/wide.tk`'s two) -- neither wide file includes the other
> (D81's own ruling), each reads the other type's sixteen bytes raw. Zero mc changes, zero
> new intrinsics: `mc limits`' `intrin` row is 8 before and 8 after.

N6b-2 is `docs/specs/small-ints.md` § 8's remaining two rows. It depends on N6a (D81) for
the eight-limb scratch and on C5 (D79) for `decimal`'s own text/round crumb having landed
first (the `decimal` side reuses `tk_dv_over96`/`tk_dec_build`, both C4's). It leaves
`ToString`/`Parse`/`TryParse` and § 7's members and statics as N6b-3, the crumb after this
one.

#### The rulings this crumb was dispatched with, and what each became

1. **`(i128)/(u128) x` on a `double` truncates toward zero and SATURATES, never wraps.**
   NaN -> 0; `d >= 2^127` -> `i128.MaxValue`; `d <= -2^127` -> `i128.MinValue`; for `u128`:
   `d < 0` -> 0, `d >= 2^128` -> `u128.MaxValue`. .NET's own `Int128`/`UInt128` explicit
   conversion from `double` takes exactly this shape, and it is teko's OWN rule and not the
   primitive widths' unchecked default -- there is no `checked` word here to have chosen a
   wrap with, the way there is none to have chosen a saturating cast with either; the market
   (C#, since C# has the type teko is borrowing the name from) settles the fork.
   `tk_i128_from_f64`/`tk_u128_from_f64` (`lib/wide.tk`) read the double's own IEEE754 BITS
   through `tk_f64_bits`/`tk_w_trunc_mag`, never a `(u64) x` cast near 2^64 -- `lib/limbs.tk`'s
   own header is the reason (`mc` compares every integer SIGNED, `u64` included) and this
   crumb extends the same caution to a narrowing float-to-int instruction it did not measure
   as safe at that boundary.
2. **`(f64) x` rounds ONCE, to nearest even, over the WHOLE 128-bit magnitude.**
   `tk_dec_to_f64`'s own three-limb accumulation (`r = r * 2^32 + limb`, twice) is exact for
   96 bits and would round at every step for 128, which is not one rounding. `tk_w_mag_to_f64`
   instead finds the highest set bit, keeps the top 53 as the candidate mantissa, folds the
   next bit and everything below it into a round bit and one sticky bit, and assembles the
   IEEE754 bits directly (`tk_f64_from_bits`) -- the textbook software float-from-bignum
   routine, and the one that answers Python's `float(int)` bit for bit: measured against
   3200-odd random probes across every bit length from 0 to 127, signed and unsigned, both
   directions, before this crumb's own fixture was cut down to the handful it keeps.
   `-2^127` is exactly representable (a power of two needs no rounding at all), which is
   `primitives_i128_convert.tk`'s own assertion for it.
3. **`(decimal) x` panics `teko: decimal overflow` at or past `2^96`, and is otherwise
   exact, scale 0.** `decimal.MaxValue` is `2^96 - 1`; `tk_dv_over96` (already `lib/limbs.tk`'s
   own, C4's) is the one check both new `lib/decimal.tk` functions read after loading the
   magnitude raw. C#'s own `OverflowException` on the same cast is the family this joins,
   the same word `decimal`'s own arithmetic already panics with (D77) and not a new one.
4. **`(i128) d` truncates toward zero and never overflows; `(u128) d` panics on a negative
   `d` whose TRUNCATED magnitude is still nonzero.** A `decimal`'s own mantissa never holds
   more than 96 bits, so the signed direction (`tk_i128_from_dec`) has no bound to cross --
   `decimal.MaxValue < 2^127 - 1` by a wide margin. The unsigned one reads the sign bit and
   the truncated magnitude separately: `-0.5m` truncates to a magnitude of 0 and answers `0u`
   (C#'s own answer, and consistent with every other truncating cast in this project), `-1m`
   truncates to a nonzero magnitude and panics -- C# throws `OverflowException` reading ANY
   out-of-range decimal into an unsigned integer type, and a negative one always is.
5. **No implicit conversion in either direction, measured and pinned.** `f64 x = v;` and
   `decimal d = v;` on an `i128`/`u128` `v` both stay refused,
   `teko: a value of type i128 does not convert to f64`/`decimal` -- `tk_num_wide_widens`
   (teko_typeof.tk) still answers 0 for a wide source (D38), which no new cast-table row
   touches, and `tk_prim_cast_lower` is reached only from an explicit `N_CAST` a plain
   assignment never builds. `tests/refuse/i128_implicit_f64.tk` and `_implicit_decimal.tk`
   are the two fixtures this ruling asked for.
6. **The ceiling.** `1i128 << 100i128` round-trips through `(f64)` and back exactly (a power
   of two, `primitives_i128_convert.tk`'s own assertion); `2^53 + 1` does not -- the same
   fixture converts it, gets back `2^53` (round to even) and asserts the two values differ,
   which is the "the round trip is lossy above `2^53`" row the ruling asked for.

#### What changed, file by file

`teko_prim.tk`: `TK_MAXPRIMX` 16 -> 32 (comment updated in the running style the other two
caps already carry). `teko_i128.tk`: eight new `tk_prim_cast_op` rows appended after N6a's
eight (D77's integer-before-float ordering does not actually bind here -- every new column
names an EXACT primitive, `ty_f64` and `tk_ty_decimal` are as `tk_prim_is` as `i128`/`u128`
are, so `tk_prim_slot_fits`'s `pt == t` line is the only one that ever matches and
registration order carries no meaning; it is kept in one shape anyway, for a reader).
`lib/wide.tk`: `tk_i128_to_f64`/`tk_u128_to_f64`, `tk_i128_from_f64`/`tk_u128_from_f64`,
`tk_i128_from_dec`/`tk_u128_from_dec`, and the shared helpers underneath them
(`tk_w_mag_to_f64`, `tk_w_trunc_mag`, `tk_w_bitlen64`/`128`, `tk_w_bit_at`,
`tk_w_sticky_below`, `tk_w_two_pow`, `tk_w_neg2`, `tk_w_bit63`/`tk_w_bit52`) -- about 220
lines, none of them a machine handler. `lib/decimal.tk`: `tk_dec_from_i128`/
`tk_dec_from_u128`, each reading its argument's sixteen bytes raw and reusing `tk_dv_over96`/
`tk_dec_build` (both C4's, already in file via `limbs.tk`).

**Two refuse fixtures deleted, both compile now:** `tests/refuse/i128_cast_float.tk` and
`i128_cast_decimal.tk` -- the exact two programs D81's own header named as the doors this
crumb would open. **Four fixtures added:** `tests/primitives_i128_convert.tk` (42, the run
fixture every ruling above cites), `tests/primitives_i128_decovf.tk` and
`_udec_neg.tk` (70 each, the two panics), and three under `tests/refuse/`
(`i128_cast_str.tk`, the shorter wording D74 wrote, now naming only `(str) x`;
`i128_implicit_f64.tk`, `i128_implicit_decimal.tk`, ruling 5's own two).

#### An adjacent finding this crumb's own `--dump-ast` pass surfaced, reported and not fixed

**D83's own gate claimed `--dump-ast` "byte-identical" over every pre-existing fixture, and
that claim does not hold under re-measurement.** `--dump-ast --include=lib --include=tests`
dumps every `FUNC` a source's own include chain parses, whether or not the program at hand
calls it -- there is no dead-code trim at the dump. D83 added its fourteen new operators'
worth of functions to `lib/wide.tk`, which `tests/primitives_i128.tk` (an N6a fixture,
already landed and already a "base" fixture at D83's own gate) already included; diffing
`12bc95f9` against `2656b0fe` with the SAME `--dump-ast --include=lib --include=tests`
invocation this entry's own gate uses shows 636 lines of pure ADDITION (new `FUNC`s
appended at the point `tk_i128_rows_bits`'s own new symbols were written, zero deletions)
in that exact file -- not the byte-identical dump D83's own PR body reported. This crumb's
own gate below measures the same thing honestly: a fixture that does not transitively
include `wide.tk`/`decimal.tk` is untouched, and one that does shows the identical
ADDITIVE-ONLY shape (new library `FUNC`s appended, zero deletions, zero reordering) --
which is the actual, provable claim a crumb that ADDS surface code to an already-included
library can make, and the one this entry makes instead of repeating D83's overclaim.

#### A second adjacent finding, newly reachable and not this crumb's to fix

**`a & b == c` on three `i128`s parses as `a & (b == c)`, C's own table and not C#'s.**
`mc`'s core grammar gives `&`/`|`/`^` LOWER precedence than `==` (D3's own "reuse the core
grammar" ruling), which was already true for every other integer type and became reachable
for `i128`/`u128` the moment D83 landed `&`. Measured with `--dump-ast`: `a & b == c`
(unparenthesized) and `a & (b == c)` produce byte-identical trees, and `(a & b) == c` a
different one; `b == c` answers `i64` 0/1, which `tk_ops_promote` widens to the wide type
before `&` is looked up, so the expression TYPE CHECKS rather than refuses -- the program
that meant `(a & b) == c` gets a silently different answer instead of a parse error.
Recorded as a `docs/reference/not-yet.md` row (this crumb's own deliverable named it); the
grammar itself is `mc`'s core and D2 forbids working around it here.

**Proof of this crumb** (mc 1.0.0, macos/aarch64, head TBD, merged with `origin/main` at
`2656b0fe`): `mc build . --config mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko
mc.macos.toml` -> **118 passed, 134 refused as expected, 0 failed** (115 + 3 new; 133 - 2 + 3
= 134, the two N6a refuse fixtures this crumb deletes and the three it adds); `sh scripts/
bootstrap.sh --os macos --arch aarch64` -> `FIXPOINT OK`; `sh scripts/check-docs.sh` -> `docs
ok: 696 links, 74 fragments, 411 diagnostics, 134 refusals, 152 samples, manifest listed`
(411 diagnostics unmoved: every new refusal this crumb's fixtures measure is an existing
generic wording, `"...does not convert to..."`/`"decimal overflow"`, not a new literal
string). `mc limits` (`rm -rf build` first, both legs): on the `tests/hello.tk` leg,
`intrin` **8**, `passes` **15**, `syntax` **20**, `alias` **25**, `types` **18**, `on_stmt`
**4**, every one identical to `2656b0fe`'s own (rows compared, not verdicts -- the `hello.tk`
leg reads `grew` on both, unrelated to this crumb, D72's own tolerance knob). `TK_MAXPRIMX`
**21**/32 (13/16 before), every other primitive cap unmoved.

`--dump-ast --include=lib --include=tests` (single-file mode, base `2656b0fe` in a fresh
clone under the scratchpad, this branch its own worktree), over the 246 fixtures that exist
on both sides (two removed, accounted for separately): **224 of 246 byte-identical**, the
other **22** -- every fixture that transitively includes `lib/wide.tk` and/or
`lib/decimal.tk` -- show a PURELY ADDITIVE diff (new library `FUNC`s appended where this
crumb's own new functions land in the source file, zero deletions, zero lines moved,
confirmed by grepping every diff for a `<` line and finding none). No pre-existing program's
own AST changed a single node.

What is left open: N6b-3 (`ToString`/`Parse`/`TryParse`, § 7's members and statics) is the
next and last crumb of N6b, unblocked and unchanged by this one. The two adjacent findings
above are reported, not fixed, in keeping with the crumb's own boundary.
