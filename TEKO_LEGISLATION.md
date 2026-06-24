# Teko — The Legislation (current norms in force)

> **This document holds the rules, conduct, and doctrine *currently in force* — the distilled "what is
> true now," separated from the historical *how we got here* (which lives in `TEKO_HISTORY.md`).** It is
> the second source of Teko law: it **grows**, and may be **amended, extended, or revised — but only
> under rigorous audit**, holding three premises absolutely:
>
> 1. **It may never wound or subvert the Constitution.** As Teko itself does, legislation *complements
>    and reinforces* the Laws **without modifying them**. (The Constitution — `TEKO_CONSTITUTION.md` —
>    is the supreme, immutable source; it is altered only by *illumination*, never by legislation.)
> 2. **Every rule cites the Law(s) that govern it** (the documentation convention), by reference *up*
>    to the Constitution. A rule with no governing Law named is **incomplete**.
> 3. **No gaps — silence does not authorize.** What is in neither the Constitution nor this legislation
>    is **not permitted by omission**; the un-legislated is submitted to the tribunal (audience +
>    assay), never presumed.
>
> **Reference direction (bottom-up):** legislation points *up* to the Constitution (each rule → its
> governing Law); history points up to legislation. The Constitution points nowhere — it is the essence.
>
> **Status — distillation complete for all settled decisions.** Every B.1–B.31 decision is distilled
> into the norms below, each citing its governing Law. The canonical lexer code (Part A) and a small set
> of **genuinely open design items** (the `inject` binding, metal representation — evolution-distant)
> still point to `TEKO_HISTORY.md`; a pointer to history is a placeholder for not-yet-written or
> not-yet-decided material, never a gap — the norm exists (in history) or the design is openly pending.
> Further change to this document (revising a norm, distilling a future decision) is legislation-work,
> done under the three premises above and rigorous audit.

---

## How to read this document

- **A distilled rule** states the norm in force, plainly, and cites its governing Law(s) (e.g. **M.3**)
  by reference up to the Constitution. It is *what holds now* — no was→is→why (that is history).
- **A pointer** (`→ HISTORY §B.x`) marks a norm whose distilled text is not yet written here; consult
  `TEKO_HISTORY.md §B.x` for the current decision and its rationale until it is distilled.
- **The Redefinitions Index** (below) tracks norms that were fixed early and later revised — only the
  legislation redefines; the Constitution, being immutable, has nothing to redefine.

---

## ⚠️ Redefinitions Index — norms that were revised

This index lists rules whose *current* form supersedes an earlier one. When this index and any entry
disagree, the index's "current state" wins. *(Mirrors the history's redefinitions index; kept here
because redefinition is a legislative act.)*

| Topic | Current norm (in force) | Governing Law | History |
|---|---|---|---|
| Error propagation | Error is always handled with `match`; **no `?` propagation operator** in the seed. | **M.4** (don't build sugar on the transitional `Valor\|Error` shape) | → HISTORY §B.1, §B.16 |
| Nominal typing | `type X = Y` is a **distinct** type (no aliases); identity is the name. | **M.3** (the type *is* its identity, not a transparent proxy) | → HISTORY §B.13 |
| String prefixes (seed) | Seed = `"` delimiter + `$"..."` one-level interpolation (literal brace `{{`). `$`-count trick **removed**; raw `@"..."` and multi-line `"""..."""` → evolution. | **M.5** (austerity; aesthetic ≠ weight) | → HISTORY §B.5 |
| `bigint` | A **library** type (evolution), not native — the sign-check comparison removed the ceiling that briefly justified native. | **M.5** + **M.0** | → HISTORY §B.30 |
| Three-way comparison | `compare → Ordering`, an **`enum`** `{ Less=-1; Equal=0; Greater=1 }` (i8-backed). | **M.0 ≺ M.3 ≺ M.5** | → HISTORY §B.31 |
| Closures | **Magic closures banned** (M.3 — they lie); **stateless function passing is SEED** (`fn(args)->ret` type, bare-name passing, no `&`, top-level only); stateful forms `use`/`inject` are evolution; DI lifetimes are `#` directives. | **M.3** (primary) | → HISTORY §B.10 |
| `static` | **Banned** (disguised global state). Replacement: DI (`inject`) + singleton — evolution. `static` distinct from `inject`. | **M.1** + **M.5** | → HISTORY §B.25, §B.10 |
| Numeric conversions (`to`) | **Any defined numeric→numeric conversion is allowed** (incl. byte↔int — byte AS u8, B.36), incl. narrowing/sign (`i32 to i8`, `i32 to u32`); loss is **caught, never silent**. **Validating whether a conversion is possible lives at RUNTIME** — an impossible conversion (the value doesn't fit) **PANICS** (debug AND release; parity with ÷0/OOB). **Constants are the exception**: a constant out of range is a **compile error** (fail early, static). `bool`↔num and non-numeric = undefined → compile error. **Supersedes** the early "forbid every lossy conversion at compile time" *and* the interim "defined-release truncation" (↺ refined: conversions panic, they do not wrap). | **M.1** (forbids *silent* loss; the panic-guard cures it — fail loudly) **+ M.0** (keep the metal conversion attempt) **+ §II** (parity with ÷0/OOB) | → HISTORY conversions block |
| `AltPattern` axis (`\|` in `match`) | An `Alt` option may be a **value pattern** (literal/range) **or a bare variant case** (`BindPattern`, `has_binding=false`) — so `RED \| GREEN` against a variant subject is legal and **counts toward variant-axis exhaustiveness** (C7b expands it). **Bindings inside an `Alt` option are forbidden** (`Foo as x \| Bar`, or a `FieldPattern` inside `\|`) → error. **Supersedes** A.14's "Alt = value axis only" annotation (the canonical axis-exclusive parse). | **M.0** (alternation is generous; `\|` already means "one of these" in the type grammar) **+ M.2** (legible at the call site) **+ M.5** (one unified parse path) **+ M.1** (exhaustiveness stays sound: Alt expands, bindings excluded by construction) | → HISTORY §A.14 (↺ Alt axis) |

---

## The norms in force (distilled + pointers)

> Each entry is either a **distilled rule** (norm + governing Law) or a **pointer** to history awaiting
> distillation. Anchors like **M.3** refer up to `TEKO_CONSTITUTION.md`.

### Error model
- **Error is a value, not an effect** — a recoverable failure is the `Error` case of a variant
  (`Valor | Error`), returned normally and handled with `match`; propagated by returning it. No
  `raise`/`throw`/`on error`, no exceptions, no stack unwinding. **Governing Law: M.1** (failure visible
  in the return type, no hidden control flow) **+ M.3** (the variant honestly carries either a value or
  a reason). The both-set/neither-set invalid states are impossible by construction (a variant is
  exactly one case) — *exclusion-by-construction*, the strong mode of **M.1**.
  *(→ HISTORY §B.1, §B.2 for was→is→why.)*
- **No `?` error-propagation in the seed** — error is *always* `match`; `?` is reserved strictly for
  nullability (`T?`, `?.`, `??`). **Governing Law: M.4** (do not build dedicated sugar over the
  transitional `Valor | Error` shape; `match` is the general mechanism that survives the shape's
  evolution). *(→ HISTORY §B.16.)*
- **Guard over nest — flatten the fallible chain (code-style norm).** When a `match` on `Valor | Error`
  exists only to extract the value and continue, write it as a **guard line** that extracts-or-returns,
  then keep the following code at the **top level** — do **not** nest the rest inside the `Parsed`/`Valor`
  arm. The approved form (exactly B.16's): `let p = match call() { Error as e => return e; Valor as v => v }`,
  then use `p` at the same level. Chaining N fallible calls this way stays **flat** (N guard lines),
  instead of the "Hadouken" staircase (N levels of nesting with a triangle of closing braces). The
  early-return in the error arm + the final value as the function's last expression is the honest shape
  (visible, no magic — the lexer A.1 already uses a diverging arm). **Governing Law:** the *legibility-
  local* facet of **M.2** (the useful work stays where the reader is, not buried levels deep) **+ M.3**
  (the guard is an explicit `match` with a visible `return`, not hidden propagation). *(→ HISTORY §B.16
  for the approved form; applied throughout Part A.)*
- **Extract repeated structure into named helpers.** Recurring fallible patterns (e.g. "expect a token
  of kind K or error," "parse one operand or propagate") become **named functions**, improving legibility
  and maintenance. Generic helpers may take the varying parts as **passed functions** (stateless function
  passing — seed, B.10 form 1). One cohesive helper, not a copied block (the aggregation norm). **Governing
  Law: M.5** (a name earns its weight by use; no duplicated blocks) **+** the legibility-local facet of
  **M.2**.

### Types
- **Nominal typing** — `type X = Y` creates a **distinct** type whose identity is its name; there are no
  transparent aliases. A newtype inherits its base's operations (same-type operates freely; crossing to
  the base or a sibling needs an explicit cast). **Governing Law: M.3** (the type *is* its identity, it
  does not *represent* the base — IS, not REPRESENTS). DDD is cheap at the root as a result.
  *(→ HISTORY §B.13.)*
- **`byte`, `u8`, and `char` are *distinct* real types; a *fixed* `char` is rejected** — refuse the false
  distinction *and* honor the real ones (superseding the early "char = byte = u8"). **`u8`** is a number
  (arithmetic); **`byte`** is an octet (a unit of *data* — IO, the lexer's scan, `.tkb` binary), a newtype
  over `u8`; **`bool`** is distinct (two values, boolean algebra, not arithmetic). A *fixed* `char = [4]byte`
  is **rejected** — it would *lie* about a **variable** (1–4-byte) UTF-8 codepoint; the honest codepoint
  type is the **variable `char = []byte`** (keeps its name; alpha-native). **Governing Law: M.3** (a fixed
  char lies; the distinctions byte/number/codepoint are real) **+ B.12/B.13.** *(→ HISTORY §B.12, §B.36.)*
- **Variant = a union of declared types** — each case is a real, separately-declared type (struct/enum/
  primitive) unioned with `|`; no inline cases, no special constructor; the inner type is reachable only
  through `match` (the proof C's `union` lacks). **Governing Law: M.3** (honest about what it carries) **+
  M.1** (exclusion-by-construction: exactly one case). *(→ HISTORY §B.14.)*

### Text & encoding
- **Bootstrap text is `byte` + `str`; UTF-8 is the (validated) codepage.** **`byte = u8`** (newtype) is one
  octet; **`str = []byte`** is text *in UTF-8* (Teko's codepage — ASCII-compatible, universal), conceptually
  a sequence of codepoints (iterated), stored as compact UTF-8 octets, zero-copy from `read_file`. Bootstrap
  literals are **`str` (`"…"`)** and **`byte` (`b'+'`)**; the lexer is **byte-level** (matches ASCII syntax
  with byte literals, collects string bytes). `str` **always validates** — UTF-8 is **forced**: a `str`
  *means* valid UTF-8, so it *is* (the invariant), guaranteed from the bootstrap via the always-on
  `str_from_utf8(bytes) -> str | Error`. **Governing Law: M.3** (UTF-8 named; `str` cannot lie about being
  valid) **+ M.1** (valid by construction) **+ B.12/B.13.** *(→ HISTORY §B.36.)*
- **A *fixed* `char` is rejected; the *variable* `char = []byte` is alpha-native.** A UTF-8 codepoint is
  variable (1–4 bytes), so a fixed `char = [4]byte` would lie; the honest **`char = []byte`** (the variable
  bytes of one codepoint, a zero-copy view into a `str`) keeps the name (variable removes the lie). It is
  **alpha-native** — codepoint work (iteration, classification, the numeric codepoint via an explicit
  decode) that the byte-level bootstrap does not use but a presentable UTF-8 language must have (the
  **bitwise** precedent: not-bootstrap ≠ evolution). **Governing Law: M.3** (no lying fixed width) **+ M.0**
  (alpha must resolve codepoints natively) **+ M.4** (`char` builds on the UTF-8 foundation). *(→ HISTORY
  §B.36.)*
- **Bytes have no inherent encoding — Teko never detects; it validates (always) or transcodes-from-declared.**
  The same octets are different text in different codepages, so heuristic detection is *guessing* (forbidden).
  Two honest paths: **validate** UTF-8 (the **always-on, bootstrap** `str_from_utf8` — the honest substitute
  for detection: guarantees the invariant, not the file's intent) and **transcode** from/to a **declared**
  codepage (`from_<cp>` / `to_<cp> -> []byte | Error`, the caller declares; never guesses — **evolution**).
  **Governing Law: M.3** (never guess an encoding) **+ M.1** (validated / `from_`/`to_` fail explicitly) **+
  M.5** (foreign-codepage transcoding is evolution). *(→ HISTORY §B.36.)*

### Pattern matching
- **`match` — one construct, two axes, forced exhaustiveness** — a single `match subject { pattern =>
  body }`; whether a branch matches a *value* (scalar: literals/ranges/`2 | 3`) or a *type* (a variant
  case) depends on the subject (the compiler knows which). Value-axis rarely binds (you hold the value;
  capture with `as`); variant-axis binds intrinsically — `Number as n` (whole) or `Binary { left; right }`
  (nominal partial selection by field name; omitted fields ignored, no `..`; separator `;`/newline per
  B.26). **Exhaustiveness is forced** (closed union, known at compile time); `_` is an optional valve
  (needed only when coverage is otherwise incomplete); a `when`-guarded branch covers *conditionally* and
  does **not** count toward exhaustiveness (guard-coverage is undecidable) — close such a type with an
  unconditional branch or `_`. **Governing Law: M.1** (exhaustiveness = safety; the `when` subtlety
  refuses to guess) **+ M.5** (one construct serves both axes) **+ M.3** (binding *reveals* the variant's
  hidden content honestly). *(→ HISTORY §B.15.)*

### Methods & static (the bare `self`)
- **Functions in a struct are permitted but discouraged** — the encouraged path is a free function
  (data ≠ behavior); a function-in-struct should look mildly *strange* (a conscious exception for
  `to_string`/`parse`). **Governing Law: M.5** (dissuade-without-prohibiting; the discomfort is a design
  signal). *(→ HISTORY §B.29.)*
- **Instance method = a function whose first arg is a bare, untyped `self`; static = no such `self`** —
  the receiver is a bare name with **no type** (name-free; the marker is the *position*). The untyped
  receiver **closes the `ref` door by construction**: with no type, there is no slot for a `*`, so the
  receiver is always a *copy* (value semantics, no mutation in the seed) — the pointer-into-arena problem
  *cannot be expressed*. Instance → called with `.` (`p.to_string()` ≡ `Ponto::to_string(p)`); static →
  called with `::` (`Ponto::parse(...)`). "Static without `static`" — a type-associated function with no
  global state (the banned `static` is global *mutable state*; Teko separates that from
  type-association). **Governing Law: M.1** (the bare `self` is *exclusion-by-construction* — removes the
  syntax that would open `ref`) **+ M.2** (the bare `self` is an explicit marker, not Zig's inference) **+
  M.3** (`.` vs `::` honestly mark instance vs type). *(→ HISTORY §B.29.)*
- **No overload, no override (in the seed)** — a function's identity is its **unique name in scope**, not
  its signature; two same-named functions collide (error) even with different signatures. `override`
  returns with OOP (it is constitutive of subtype polymorphism — evolution); `overload` **never** returns
  (redundant with generics + default args). **Governing Law: M.2** (no signature-resolution guessing) **+
  M.3** (nominal identity) **+** the *legibility-local* facet of **M.2** (one name = one function). *(→
  HISTORY §B.29.)*

### Modules & files
- **The stdlib is injected, not imported; `teko::` is the reserved native root** — `teko::` (e.g.
  `teko::print`, `teko::Error`) is non-shadowable, not aliasable, never a dependency; writing native code
  is gated by repo governance, not cryptography. **The canonical root *is* the project's name** (declared
  in `.tkp`): the language's own project is canonically **`teko`**, so its modules are `teko::lexer`,
  `teko::parser`, …, and the injected stdlib emanates from the same root (compiler + stdlib are one `teko`
  project). No *user* project may claim `teko`. **Governing Law: M.4** (the kernel model). *(→ HISTORY §B.9.)*
- **Absolute addressing — no relative or indirect access** — a symbol's canonical name is its **full
  absolute path from the canonical root** (`teko::lexer::Token`). Within the **same** namespace (directory),
  references are **bare**; crossing to **any other** namespace — even within your own project — uses the
  **absolute path** (not the old last-segment qualification). The source root (`src/`) is *invisible*
  (`teko::lexer`, never `teko::src::lexer`). Nested namespaces are sub-directories joined by `::`; a name
  colliding with a sub-namespace segment is a **compile error**. `.tkt` tests live **beside** the code they
  test (same namespace; no `/test` dir). **Governing Law: M.3** (every path tells the truth about where a
  symbol lives) **+ M.5** (one addressing scheme, not two). *(→ HISTORY §B.9, §B.32.)*
- **`use` is alias-only; re-export is ABOLISHED** — `use` does not change visibility (`pub`/`exp`) or
  declare a dependency (`.tkp`); it **binds a shorter name** to an absolute path. `use teko::lexer` binds
  the **last segment** `lexer` (implicit alias) → `lexer::Token`; `use … as x` binds `x`. Three scopes:
  file-local (`use`), global (`.tkp` `[aliases]`), imported (dependency rename). A name resolves to
  **exactly one thing**: if a `use` (the implicit last segment *or* an explicit `as` alias) would clash
  with a function, type, any item, another alias, or a sub-namespace segment, it is a **compile error with
  no precedence** — Teko never silently picks a winner; resolve with a distinct `as` or the full absolute
  path (always unambiguous). The absolute path always remains valid. **No** wildcard `use …::*` (invisible
  names), **no** relative imports, and **re-export does not and will never exist** (`pub use`/`export … from
  …` is the indirect access this model forbids — it *lies* about where a symbol lives; facades use a real
  one-line **wrapper** at the public path, API stability is `exp`-surface discipline). **Governing Law: M.3**
  (no lying paths) **+ M.5** (one scheme) **+** the *legibility-local* facet of **M.2** (the reader always
  knows a name's origin) **+ M.1** (ambiguity is an error, not a guess — *exclusion-by-construction*). *(→
  HISTORY §B.32.)*
- **Visibility — `pub` vs `exp` (private = absence of a keyword)** — `pub` = **public within the project**
  (visible across the project's namespaces, but **not** in the binary header — its signature is not exposed
  externally); `exp` = **exported in the binary header** (the library's public ABI), hence **public by
  definition**. `pub` is "open inside the house," `exp` is "open to the street." **Governing Law: M.2** (the
  boundary is explicit) **+ M.3** (`pub`/`exp` honestly mark the two reaches). *(→ HISTORY §B.9.)*
- **The `.tkp` manifest is TOML** — declares `name` (canonical root), `artifact` (executable/library),
  `source` (invisible root), `[dependencies]` (+ import aliases), `[aliases]` (global). TOML because it is
  read *before* any Teko is parsed (standalone simple parser): explicit and *typed* (no YAML coercion lies),
  commentable (a manifest explains its choices), minimal, unambiguous, and **not bespoke** (no duplicate
  parser). YAML rejected (lies — the "Norway problem"), JSON weak (no comments), INI weak (no nesting).
  **Governing Law: M.3** (typed, no coercion) **+ M.2** (commentable) **+ M.5** (minimal, reuse not bespoke)
  **+ M.1/M.0** (unambiguous, simple grammar). *(→ HISTORY §B.33.)*
- **A Teko package = `.tkh` (interface) + `.tkb` (typed-tree payload); deps are STATICALLY pre-linked, not
  FFI.** When a `.tkp` declares `artifact = library`, the compiler emits a **package**: the `.tkh` (the `exp`
  interface a consumer type-checks against) and the `.tkb` (the full serialized **typed tree** — Teko IL).
  The **`.tkb` is binary but is NOT a native object (`.o`)** — it is Teko's own typed-tree serialization. When
  compiling the dev's code against dependencies, the compiler **loads the dependency packages into memory and
  acts as a PRE-LINKER, statically merging** their typed trees with the dev's into one program before codegen.
  This is **static linking of Teko objects at the typed-tree level** — the deliberate alternative to dynamic
  linking / **FFI** (FFI is reserved for *foreign*, non-Teko code, at the unsafe IO boundary). **Governing Law:
  M.1** (static merge = the whole program is checked together, no dynamic-boundary surprises) **+ M.3** (Teko↔Teko
  is honest static inclusion, not a pretend-foreign call) **+ M.4** (the consuming compile is built on already-
  checked packages). *(Forward-looking: the pre-linker + package loader land with the pipeline driver; today only
  the emitters — `.tkb` codec and `.tkh` `build_header`/`emit_tkh` — exist.)*
- **Recursive types allowed; cyclic namespace dependencies forbidden** — mutually recursive *types* are
  fine (compiler-managed indirection, no exposed pointer — the AST needs it); a namespace import **cycle
  is a compile error** (modules form a DAG, one-directional, matching the compiler's own pipeline flow).
  **Governing Law: M.4** (one-directional layering; data may cycle, modules may not — a cycle is a design
  smell). *(→ HISTORY §B.8.)*


### Memory & references
- **Pointers `ptr`/`uptr` are opaque, transport-only** — word-size numeric types for extern marshalling
  only, **never dereferenced** in Teko code; no `*` syntax, no typed `const_ptr`/`mut_ptr`. FFI is unsafe
  by nature (accepted and discouraged, not papered over with a fake guarantee the C side can break). If a
  `const`/`mut` pointer distinction were ever needed, that is the signal to implement `ref`, not to grow
  pointers. **Governing Law: M.1** (the metal is contained at the FFI boundary) **+ M.3** (no pretend
  guarantee). *(→ HISTORY §B.6.)*
- **State flows without `ref`: caller-local + return** — mutable state lives in the **caller** (`mut
  pos`); pure helpers take `(source, pos)` and **return** the new state (in a struct — no tuples); the
  caller reassigns. The fix is *returning* state, not changing what is passed. **Governing Law: M.1** (no
  aliasing, no mutation-at-a-distance) **+ M.2** (state advances visibly). *(→ HISTORY §B.7.)*
- **Generics are deferred (evolution)** — generics themselves are tractable; the **constraint system** is
  the cost (C++/Haskell pay in illegibility). A `variant` (closed sum) does **not** substitute for
  generics (open parametric) — closed-vs-open is the line. Deferred until the self-hosted compiler reveals
  where duplication actually hurts. **Governing Law: M.5** (the constraint system's weight isn't justified
  in the seed) **+ M.4** (design with real data, not ahead of it). *(→ HISTORY §B.11.)*

### Runtime & IO

- **The runtime stance — a "metal" *ethos*, AOT-on-host as the LTS, bare-metal as aspiration.** "Metal"
  is an **ethos** (no GC, no hidden runtime, no managed heap; you control memory via arenas — B.7; visible
  costs; a thin, explicit host boundary), **not** a deployment target — fully compatible with a host OS.
  **Bare-metal** (no OS, drivers, bare hardware) is the **aspiration**, years away. Three materialization
  stages (build-order): **(1) `.tkb`** — IL/bytecode, interpreted, the first materialization and bootstrap
  step; **(2) AOT-native on a host OS** — the **LTS**, the ethos fully realized, **what ships**; **(3)
  bare-metal** — the aspiration, isolated behind the IO boundary. Teko ships stage 2 and **never claims**
  stage 3 for what ships; "metal" reads as the ethos, never as bare-metal. **Governing Law: M.3** (the
  aspiration↔reality gap is named, not hidden) **+ M.4** (the stages are layers; build-order applied to the
  runtime) **+ M.5** (the minimal runtime that works — no bare-metal drivers when AOT-on-host serves). *(→
  CONSTITUTION M.0; HISTORY §B.34.)*
  - **First-binary realization = TRANSPILE-TO-C.** Stage 2 (AOT-native) is first realized by **lowering the
    typed tree to C** and letting the host `cc` produce the binary — reusing the toolchain (**M.5**), not a
    bespoke native codegen. **Both execution modes are planned:** transpile-to-C/AOT (first) and the **stage-1
    `.tkb` VM/interpreter** — the VM is a **future mode** (not dropped), it just does not gate the first binary
    (it needs the statement/program `.tkb` codec, today expression-only). The path is TEKO_ROADMAP_BINARY.md.
    *(legislator's choice — → HISTORY first-binary backend.)*
- **IO is slurp (whole-file `[]byte`), not streams — for the seed.** `read_file(path) -> []byte | Error`
  (open, read all, close), `write_file(path, []byte) -> () | Error`, `write_err([]byte)` (stderr).
  **Streams are deferred** (they enter when large inputs justify the weight). `read_file` returns **raw
  `[]byte`** (octets), not `[]u8` — a file *is* bytes; interpreting them as text (UTF-8 → `char` → `str`)
  is a separate step (`byte` is distinct from `u8` — B.12). **Governing Law: M.5** (small files don't
  justify stream weight) **+ M.3** (one open/read-all/close, no hidden buffering; data vs interpretation
  kept distinct) **+ M.1** (`-> []byte | Error`, explicit failure). *(→ HISTORY §B.35.)*
- **The IO boundary (`teko::io`) is the thin, named, swappable host edge.** The stdlib module doing
  file/console IO is the isolated boundary between Teko and the host; its **interface** (`read_file`,
  `write_file`) is **stable across the stages**, while its **implementation** descends the stack
  (interpreter syscalls → AOT syscalls → bare-metal drivers). The aspiration is reached by swapping the
  implementation, not redesigning. **Governing Law: M.4** (a clean isolated layer) **+ M.3** (named, not
  magic) **+ M.0** (thin over syscalls, no fat IO runtime). *(→ HISTORY §B.35.)*

### Behavior + state
  stateful is passable; forms with captured state are inexpressible in the seed). It needs **no `ref`**:
  a function pointer is a *code* address, outside the data-`ref` ban's jurisdiction. **Governing Law:
  M.0** (a code pointer + indirect call is metal) **+ M.3** (no hidden state — a code address does not
  lie) **+ M.1** (no captured state → none of the closure dangers) **+ M.5** (trivial weight, high use).
  *(→ HISTORY §B.10 — re-analyzed to seed when the operator layer A.5 needed it.)*
- **Magic closures are banned (permanently)** — implicit scope capture is a `(code, state)` struct
  disguised as a function; it *lies* about what it carries. **Governing Law: M.3** (primary); the
  concurrency hazard (M.1) and cost (M.5) are *symptoms* of the hidden state, not the root.
- **Stateful behavior-passing returns through two honest forms (both EVOLUTION)** — the capability
  (behavior + *state*) returns by state-origin, each form declaring its provenance:
  - **`use`** — state from the caller's *local* scope, declared explicitly (PHP-style; only what you
    list). Copy form safe under value semantics; reference form (`use (&x)`) waits for `ref` + arenas.
  - **`inject`** — a dependency injected from outside; an **auxiliary footnote** (after `-> return`,
    before the body) that *contextualizes* the signature without being part of it (the caller never
    supplies it — the compiler does). **Governing Law: M.3** (each origin named; honest about
    provenance) **+ M.2** (explicit, not magic). *(→ HISTORY §B.10 for the full reasoning, the
    `inject`-vs-`with`-vs-`injects` naming, and the title/sentence/footnote metaphor.)*
- **DI lifetimes are `#` directives (EVOLUTION)** — `#singleton` / `#scoped` / `#transient` bind a
  dependency to an **arena** (root / scope / local), so resolution is planned at compile time and the
  object materializes in its arena at runtime — no runtime container, no reflection, no overhead.
  **Governing Law: M.0** (compiles to direct arena allocation) **+ M.1** (a reference outliving its arena
  is caught at compile time). They are *directives* (the "title" — compile-time allocation strategy),
  distinct from `inject` (the "footnote" — runtime contract). *(→ HISTORY §B.10.)*
- **`inject` requires a traceable binding (EVOLUTION)** — an explicit binding declares which concrete
  type fulfills an interface and with which lifetime; DI by compiler guessing is forbidden. DI depends on
  **interfaces** (injection by contract), which precede full OOP. **Governing Law: M.3** (traceable, not
  guessed) **+ M.4** (interfaces → DI → OOP, build order). *(→ HISTORY §B.10.)*

### Operators & precedence
- **Operators map to silicon; quasi-metal goes to a library** — an operation is an operator only if it
  is (nearly) one instruction (arithmetic, bitwise, shift, comparison). abs/modulo/sqrt/pow/trig/log and
  π/e are `teko::math` functions, not operators. **Governing Law: M.0**. *(→ HISTORY §B.22, §B.24.)*
- **Maximal munch — longest-match-always, no context heuristic** — the lexer always takes the longest
  token that fits (`<<` not `<` `<`; `//` always a comment; `..=` one token); it classifies, it does
  not interpret. **Governing Law: M.2** (deterministic, predictable) **+ M.5** (refuses context-heuristic
  complexity), resting on **M.4** (the lexer classifies, doesn't interpret — a context heuristic would
  invade the parser's role). *(→ HISTORY §B.23.)*
- **Precedence follows Julia 100%; bitwise above comparison** — where school mathematics has an order
  (`()`, `* /`, `+ -`, left-to-right) Teko obeys it; bitwise/shift map to analogous arithmetic levels
  (AND≈`*`, OR/XOR≈`+`, shift just below `*`) and sit **above** comparison, fixing C's error
  (`a & b == c` is `(a & b) == c`); comparison chains (`a < b < c`). **Governing Law: M.3** (primary —
  the grouping must not *deceive*: C's precedence inverts intent, the expression lies about what it
  groups; honest precedence makes syntax match apparent meaning) **+** the *legibility-local* facet of
  **M.2** (honest precedence is also predictable) **+ M.0** (the levels reflect the metal-arithmetic
  analogy; bitwise stay metal operators, not functions). *(→ HISTORY §B.23.)*
- **`+` never concatenates** — string building is `string::concat` / interpolation `$"..."`, never `+`.
  **Governing Law: M.3** (the operator must not lie — concatenation is not commutative addition) **+
  M.0** (concatenation is alloc+copy, not an instruction) **+** the *legibility-local* facet of **M.2**
  (an un-overloaded `+` reveals its operands are numeric). *(→ HISTORY §B.27.)*

### Safety & determinism
- **∞/NaN never exist as values; ÷0 panics (runtime) / is a compile error (literal)** — the metal stores
  IEEE ∞/NaN, but Teko intercepts at the origin; recoverable division is `teko::math::div → T | Error`.
  **Governing Law: M.1** (no silent poison; "the NaN coffin stays shut"). *(→ HISTORY §B.24.)*
- **Costly safety checks are debug/test-only; release is metal-pure (defined behavior)** — overflow
  panics in debug, wraps (defined) in release; the build profile decides. Division's ÷0 check is the
  exception that stays in release (poison/crash is worse). **Governing Law: M.0** (release performance) **+
  M.1** (debug catches bugs). *(→ HISTORY §B.22, §B.24.)*

### Syntax & structure
- **Delimiters — absolute rule** — `;`/newline separates inside `{}`; comma separates inside `()`/`[]`;
  no exceptions (destructuring and `match` bindings use `;`/newline to match struct form). `use`/`inject`
  lists are argument lists → comma. **Governing Law: M.2** (one explicit rule) **+ M.5** (no special
  cases). *(→ HISTORY §B.26.)*
- **Significant newline; `;` is an inline separator** — statements end at newline; `;` only to put
  several on one line. **Governing Law: M.2** **+ M.5**. *(→ HISTORY §B.17.)*
- **`++`/`--` are statement-only** — they exist (increment/decrement is a machine instruction) but are
  forbidden inside any expression (`arr[i++]` is a compile error); one form only (`p++`, no `++p`);
  `+= 1` covers non-unit steps. **Governing Law: M.0** (they exist — `inc`/`dec` are metal) **+ M.3**
  (statement-only: inside an expression `++` would lie, being value-and-mutation at once) **+** the
  *legibility-local* facet of **M.2** (a mid-expression `i++` hides that `i` changes) **+ M.5** (one
  form — statement-only makes pre/post identical, so a second form pays no weight). *(→ HISTORY §B.4.)*
- **`mut` only on local variables** — function parameters and struct fields are never `mut`; mutability
  is a local-scope, arena-controlled property. **Governing Law: M.1** (arena control) **+ M.2** (explicit).
  *(→ HISTORY §B.21.)*
- **`if` is an expression; no `unless`** — `if/else` returns a value; there is no `unless` (a negated-`if`
  is redundant — write `if !cond`). **Governing Law: M.5** (no redundant construct) **+ M.2**.
  *(→ HISTORY §B.20.)*
- **Comments — line `//` and nesting block `/* */` only; discarded before the termination decision** —
  repeated opening chars are insignificant (`///` = `//`); doc-comments are a future tooling convention,
  not a lexer token; comments are trivia (never reach the AST), skipped like non-newline whitespace while
  significant newlines are preserved. **Governing Law: M.5** (two forms, no doc-token bloat) **+ M.4**
  (the lexer classifies and discards trivia; nesting fixes C's non-nesting defect). *(→ HISTORY §B.18.)*
- **Keywords — a fixed table; primitive type names are predefined types, not keywords** — the lexer
  looks a word up in the keyword table (else `Ident`); `i32`/`u8`/`bool`/… are injected predefined types
  (lexed as `Ident`, resolved by the checker), not reserved words; `as`/`in` are word-operators; logical
  ops are symbols (`&&`/`||`/`!`), no `and`/`or`/`not`. Seed keywords: `let mut const fn return type
  struct variant enum` · `match when loop break continue defer` · `in as` · `pub exp` · `use` · `true
  false null` (plus `!in` compound). **Governing Law: M.5** (minimal keyword set — predefined types
  aren't reserved) **+ M.2** (explicit table lookup, no heuristic). *(→ HISTORY §B.19.)*
- **Literals — bases `0x`/`0b`/`0o` (lowercase prefix, case-insensitive digits, suffix combines);
  digit separator `_` only between digits, any base; string escapes exactly `\n \t \r \\ \" \0 \u{HEX}`
  (`\u` → UTF-8 bytes); `''` is the zero byte (`char = u8`); floats require `0.5`/`5.0` (reject `.5`/`5.`
  to protect the `.` access operator).** **Governing Law: M.0** (hex/bin serve the bitwise operators;
  `''`=0 and `\u`→bytes follow `char = u8`) **+ M.2** (`.5` rejected — it would be ambiguous with field
  access). The invalidity of `__` (double wildcard) is *exclusion-by-construction* (M.1): three rules
  for other reasons make it inexpressible, with no dedicated anti-`__` rule. *(→ HISTORY §B.28.)*

### Arrays
- **`[]T` (Go-style); never nullable; forced initialization; out-of-range panics; `.len` is `u64`** —
  jagged (`[][]`) and real multidimensional (`[,]`); capped at 16 levels. **Governing Law: M.1**
  (out-of-range panics; size `u64` makes negative size unrepresentable — exclusion-by-construction) **+
  M.0** (`.len` an intrinsic field, no generics). *(→ HISTORY §B.25.)*

---

## Bootstrap & process (how Teko is built — distinct from what Teko is)

> The norms above govern **the language** (what Teko *is*). This section governs **the bootstrap
> process** (how Teko is *built*, in which language, by whom). It is process-legislation — still under
> the three premises (it never wounds the Constitution; it cites the Laws; it fills a vacuum that
> silence would otherwise leave). Its governing Law is largely **M.4** (build order): the bootstrap *is*
> the pipeline's construction order, in C until self-host.

- **Two languages, two roles — both are written.** The **seed compiler is written in pure C23** (the
  *tool* that translates Teko → binary); the **examples/tests are written in Teko** (the *target
  language*). Both must exist: C23 must *compile* the Teko seed, so until self-host the C23 implementation
  and the Teko target are co-developed. The supreme specification both serve is the **Legislation** (this
  document): the examples *demonstrate* the spec, the C23 compiler *implements* it. **Governing Law: M.4**
  (the tool precedes the self-hosted language in build order).
- **Agents are bilingual — each writes C23 or Teko on demand.** There is one kind of agent; the task
  decides the language, by this criterion: **implementing the compiler → C23** (lexer, parser, checker,
  codegen, linker); **demonstrating or testing the language → Teko** (examples `E.x`, `.tkt` tests,
  target code). An agent must know both and apply the criterion per task. **Governing Law: M.2** (the
  criterion is explicit — no guessing which language a task wants).
- **The discipline-overhead rule — the seed's C23 is written *as if Teko governed it*.** C (any version)
  **violates most of Teko's Laws by nature**: implicit conversion (against M.2/M.3), undefined behavior
  and `NULL` and loose pointers and undefined overflow (against M.1), the treacherous `a & b == c`
  precedence (the very bug Teko fixes). This is **not a contradiction** — it is the nature of bootstrap
  (you need an existing language to create the one that does not yet exist; chicken-and-egg, resolved by
  C). But it imposes a **discipline overhead**: the seed's C23 is code under *extra* discipline — the
  agent **manually honors Teko's intentions that C does not enforce**: initialize everything (no garbage),
  use no UB, check overflow where it matters, write explicit parentheses where C's precedence would
  betray intent, keep error handling explicit, avoid implicit conversions. C is the *tool*, but Teko's
  *intentions* guide how the tool is used. **Governing Law: M.1 + M.2 + M.3 applied to the act of
  building** (the laws govern the constructor's hand even when the construction language does not enforce
  them) **+ M.4** (this holds during bootstrap; at self-host the language enforces its own laws).
- **The temporal arc (M.4).** **Bootstrap** (the compiler compiles itself) and **0.0.1-alpha** are
  predominantly **C23** (building the compiler). **0.0.1-LTS is self-hosted** — rewritten in Teko on a
  clean AST — after which Teko compiles itself and the C23 seed is retired. The C23 is necessary *until*
  self-host; the discipline-overhead rule applies for exactly that window (once Teko compiles Teko, the
  language enforces what the agent used to honor by hand). *(→ HISTORY: milestones bootstrap/alpha/LTS.)*

### Source organization (both `.c`/`.h` and `.tks`/`.tkp`/`.tkt` obey the norms)

> Source organization is a **manifestation of the Laws**, not an arbitrary choice of whoever types. The
> Teko side is normed by **B.9** (namespace = directory; files in a directory aggregated; visibility =
> physical scope; no headers; `.tkt` beside `.tks`; source root invisible). The **C seed compiler**
> cannot abolish headers/include-order/preprocessor (C forces them) — so, by the discipline-overhead
> rule, it is **organized to *mirror* the Teko structure**, honoring the norms' intentions in the
> language that does not enforce them.

- **The C tree mirrors the Teko namespace tree (same division, same names).** The compiler's folder
  structure reflects the language it compiles: a Teko namespace `lexer/` ↔ a C folder `src/lexer/`
  (`lexer.c`, `lexer.h`); names correspond (`lexer.tks` ↔ `lexer.c`/`lexer.h`), so navigating the C
  compiler *is* navigating the Teko structure. **Governing Law: M.4** (the physical structure *is* the
  build order — each folder is a pipeline stage).
- **The `.h` manifests the visibility boundary (`pub`/`exp`), not mere technical declaration.** What Teko
  would expose via `pub`/`exp`, the C declares in the `.h` (the module's public interface); what Teko
  would keep private (directory scope) is `static` in the `.c` (file scope, the C analogue of Teko's
  directory-private). The header is the boundary made concrete — the same concept as Teko's visibility,
  in the tool that requires a header. The agent decides what goes in the `.h` by asking *"what would be
  `pub`/`exp` in Teko?"*. **Governing Law: M.2** (the boundary is explicit) **+ M.3** (the `.h` honestly
  *is* the public surface, not incidental).
- **Aggregation by cohesion (SRP), not one-construct-per-file.** As in Teko (files in a directory
  aggregated by cohesion), the C groups by responsibility — `lexer.c` contains the *whole* lexer, not
  `token.c` + `keyword.c` + `scanner.c` fragmented without reason. **One `.h` per stage** (subdivide only
  if cohesion demands it — the same aggregation rule). The "soup of global registries" anti-pattern is
  forbidden. **Governing Law: M.5** (no arbitrary fragmentation).
- **Preprocessor disciplined to its minimum.** Includes at the top, ordered; header guards; `#define`
  **only** for the guard, **never** for metaprogramming (Teko has no macros; the seed's C does not abuse
  them). No logic hidden in the preprocessor. **Governing Law: M.2 + M.3** (no preprocessor magic — what
  you read is what compiles).
- **Tests beside the code (mirroring `.tkt`).** Compiler tests live beside the code they test
  (`lexer_test.c` in `src/lexer/`), mirroring `.tkt`-beside-`.tks` — so a test sees the module's `static`
  internals without breaking the boundary. C has no extension-driven build profile, so **the build
  (Makefile/CMake) excludes tests from the compiler binary and includes them only in the test target —
  by hand** (discipline overhead: the build does manually what Teko does by extension). **Governing Law:
  M.4** (tests beside, as in B.9) **+** the discipline-overhead rule.
- **The native stdlib folder mirrors `teko::`.** The C implementation of the injected stdlib (`src/teko/`)
  mirrors the `teko::` namespaces (`strings/`, `math/`, …) — full coherence with the language's own root.
- **The canonical seed-compiler tree (follows the pipeline — M.4):**
  ```
  teko/
  ├── src/                 source root (mirrors Teko's invisible src/)
  │   ├── lexer/   lexer.c   lexer.h     (+ lexer_test.c)
  │   ├── parser/  parser.c  parser.h    (+ parser_test.c)
  │   ├── ast/     ast.c     ast.h       (the tree's node types)
  │   ├── checker/ checker.c checker.h   (semantic/type analysis)
  │   ├── codegen/ codegen.c codegen.h   (the emitters → target)
  │   ├── linker/  linker.c  linker.h    (the `tld`)
  │   └── teko/    …                     (native stdlib, mirrors teko::)
  ├── main.c               compiler entry point (orchestrates the pipeline)
  └── (Makefile/CMake)     build (outside the language norms; honors the above by hand)
  ```
  Each folder is a pipeline stage; the structure *is* the build order (M.4). The same tree, by mirroring,
  describes both the C compiler and the eventual self-hosted Teko compiler.

### Build rule — a feature may be a cross-cutting *dimension*, not a single layer

> Some features are not one layer but a **dimension threaded through several
> layers**, each strand depending on a different structure. **Assignment** is the
> worked example: binding a name to a value happens in many contexts —
> **statement bindings** (`let`/`mut`/`const`, simple `x = v`) depend on
> identifiers + expression; **compound** (`x += v`) depends on the **operators**
> (B.22); **arguments** (parameter ← caller) depend on **`fn`**; **destructuring**
> (`let {x; y} = p`) depends on **`struct`**; **match-bindings** (`Number as n`)
> depend on **`match`**; **aliases** (`use x as y`) depend on **`use`/modules**.
> Each form is **born with the structure that hosts it, never ahead of it** (M.4).
> So such a dimension is **lit one structure at a time** — it cannot be "closed"
> in a single layer, and attempting to do so builds strands on absent structures.
> **The recorded order for assignment:** statement bindings (done) → operators
> (done) → variable references (done) → compound (done) → `fn` (arguments — done)
> → `struct` (destructuring — done) → `match` (match-bindings — done) → `use`
> (aliases — done). **The assignment dimension is COMPLETE** — all seven strands
> woven, each born with the structure that hosts it (M.4). The insight held: assignment
> is a dimension threaded through the language, lit one structure at a time, never a
> box to be closed in isolation.
> **Governing Law: M.4** (never build a strand on an incomplete structure).

---

## Part A — Canonical code (the lexer, the law manifest in code)

> The canonical lexer is the **pure manifestation** of the Laws in code — it *is* legislation (a norm
> made concrete). It was audited against the Laws (e.g. positions/widths corrected to `u64`; underscore
> separator; newline/`;` not comma). Specific tokens may be **argued and revised** when an ad-hoc
> decision is found to conflict with a Law — such revision is legislation-work under the three premises.
>
> The full canonical lexer code currently lives in **`TEKO_HISTORY.md` Part A** (A.1). The **parser**
> (A.2) is now written there too — an **expression parser** (the natural pair of the A.1 lexer): it
> consumes A.1's token stream and builds an expression AST, applying precedence (B.23) by **recursive
> descent** (the precedence is visible in the function structure — the legibility-local facet of M.2),
> with the AST as a recursive `variant` (B.14/B.8), error-as-value (B.1), and the ref-less state flow of
> A.1 (B.7). A.2 is the canonical structure the C23 `src/parser/parser.c`+`.h` will **mirror** (the
> source-organization norm). **A.3** extends both stages for the first **declaration** — the `let` layer
> (lexer gains keywords/`=`/significant-newline; parser gains a statement level: `Program` =
> `[]Statement`, `Statement = Let | ExprStmt`), reusing the A.2 expression parser unchanged for the value
> (M.4 — build on the complete); each layer extends **lexer-first, then parser**, the smallest complete
> step. **A.4** adds the statement bindings `const`/`mut`, the optional type annotation (`: T`), and
> simple reassignment (`x = v`, a distinct AST case from *binding* — M.3 create-vs-reassign); one parser
> serves the three bindings. It does **not** close "the assignment family" — assignment is a cross-cutting
> dimension (compound awaits operators; arguments await `fn`; destructuring awaits `struct`;
> match-bindings await `match`; aliases await `use`), built one strand at a time (see the Bootstrap &
> process build rule). Order: **operators → compound → fn**. **A.5** builds the complete operator layer —
> the full inventory (B.22: arithmetic `+ - * / %`, bitwise `& | ^ ~`, shift `<< >>`, comparison, logical)
> and the nine-level precedence hierarchy (B.23) as recursive-descent functions (precedence visible in
> the call structure — M.2); comparison **chains** into a `Compare` node preserving the source (M.3,
> semantics deferred to codegen), unary is prefix right-associative, and the five ordinary binary levels
> collapse into one helper via **stateless function passing** (B.10 form 1 — re-analyzed to *seed* when
> A.5 needed it: a passed module-level function is a code address, no `ref`, no capture). This **unblocks
> compound assignment**, which returns next, complete. The surrounding rules are distilled and the
> exhumations settled; the canonical code can be carried here when useful. → HISTORY Part A (A.1 lexer,
> A.2 parser, A.3 `let`, A.4 statement bindings, A.5 operators, A.6 variable references, A.7 compound
> assignment). **A.6** adds the missing expression atom — a **variable reference** (an `Ident` used as a
> value; absent since A.2, so `let z = y` had failed), completing expressions before compound rests on
> them. **A.7** brings **compound assignment** reborn complete (`+= -= *= /= %= &= |= ^= <<= >>=`, all
> metal/ALU) over the finished operators and variables; the logical compounds `&&= ||=` stay evolution
> (compounds of non-metal control flow). The op is **preserved** in the `Assign` node (not desugared in
> the parser — consistent with comparison chains, honest to source, and future-proof for complex targets:
> codegen does the desugar and single-evaluation). **A.8** adds **type expressions** — the sub-grammar
> for writing a type (`parse_type`: simple names, slices `[]T`, unions `A | B`, qualified names
> `lexer::T` via paths), used throughout the examples' signatures but previously unparseable (A.4's
> annotation read a single `Ident`); it completes A.4's annotation and is the prerequisite for `fn`.
> **A.9** is **`fn`** — function declaration (`fn name(params) -> ret { body }`: comma-separated immutable
> typed params, required return type via `parse_type`, body a statement block parsed to `}`) and **calls**
> (`f(args)`, completing `parse_atom` — a path with `(` is a call, without is a variable); `Program`
> becomes `[]Item` (`Function | Statement`). Parameter binding is the assignment dimension's fourth strand
> (B.21 — immutable). Order held: operators → variables → compound → types → `fn` → type declarations →
> destructuring. **A.12–A.14** add control flow: **A.12** the `if`-expression (returns the chosen
> branch — B.20; enters at the atom level) and `return` (early exit); **A.13** `loop` (the sole
> primitive loop — M.5) with `break`/`continue` (`if cond { break }` — B.20); **A.14** `match` (pattern
> discrimination — B.15; an expression, two axes, `when` guards, `_` valve), the sixth strand of the
> assignment dimension. Match's field pattern (`Type { f; g }`) reuses A.11's destructuring machine, and
> exhaustiveness is the checker's verdict (the parser only structures the arms). **A.15** adds **`use`** —
> aliases (`use teko::lexer` / `use … as x`), the **seventh and final** assignment strand, reusing the
> path machine (A.8/A.9); the parser records the path + explicit alias, while the implicit last-segment
> alias, the iron rule (B.32), absolute-path resolution, and file-local scoping are the checker's. **The
> assignment dimension is complete** (all seven strands). **A.16** adds the **literal layer** — the
> bootstrap text types (`byte = u8` octet, `str = []byte` **always-validated** UTF-8 — B.36), making the
> lexer **byte-level** (`[]byte`, byte literals `b'+'`) and adding `str` (`"…"`) and `byte` (`b'+'`) literal
> tokens + atoms; the variable `char = []byte` (codepoint) is **alpha-native** and foreign-codepage
> transcoding is evolution. **A.17** consolidates the **parser plumbing** — declaring the eleven result
> types the earlier examples referenced (the `Parsed*` family + `Guard`, all `{ payload; next: u64 }`) and
> writing the deferred readers (`parse_expr_path`; `parse_field_names`, the extracted `{}`-name reader that
> closes the A.11/A.14 divergence; `parse_alt_pattern`; `parse_stmt_item`) — making the seam whole for the
> port (M.4). → HISTORY Part A (A.1–A.17).

---

## Open design (not yet decided — distinct from distillation)

The norms above are distilled. What remains is **genuinely undecided design**, not pending distillation —
evolution-distant items awaiting their layer (per M.4):
- **The `inject` binding's placement** — where the traceable binding is declared (a project bindings
  file / an entry-point block / a per-namespace registry). Operates over interfaces, carries the
  `#lifetime` directive. **Deliberately left open until DI is actually built** — the placement only
  becomes clear once the layers it rests on exist (interfaces, the parser that reads them, the expanded
  arena model the lifetimes need). Deciding it now would be building on the incomplete (**M.4**); the
  design will resolve itself when DI is constructed, not before. This is deferral by discipline, not an
  omission.
- **Metal representation of `use`/`inject`** — appears like ordinary arguments, but the parser must
  decide the *how* before the metal is fixed (M.4: don't settle the metal on an unwritten parser).

*All B.1–B.31 decisions are now distilled into the norms above, each citing its governing Law. The
History retains every was→is→why. When the open-design items are decided (under the three premises), they
join the norms here.*
