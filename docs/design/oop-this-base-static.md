# OOP syntax modernization — `this` / `base` / `static`

Architect design (2026-07-06). Owner-requested (2026-07-06) on developer feedback that the
current method syntax (loose untyped first-arg receiver + `class Base(binding)`) is hard to read.

**Verdict up front:** this is a **front-end-only rename** that lowers to the EXACT SAME
TAST/codegen/VM the current model already produces. The single design fork needing the owner's
ruling is **hard-cut vs dual-syntax transition** (see §6, HALT). Everything else resolves law-first.

---

## 0. Ground truth (verified at file:line)

- **Receiver = the untyped first param** (index 0, `has_type=false`). Parsed by
  `parse_params` with `allow_receiver=true` for a struct/class/interface method
  (`src/parser/parse_decl.tks:33-39`). Its NAME is author-chosen; today the corpus convention is
  `self` (`src/io/stream.tks`, `src/checker/synth.tks:199`).
- **Static method = a method whose first param is TYPED, or which has no params.** The sole
  detector, used EVERYWHERE, is `params.len == 0 || params[0].has_type`
  (`src/checker/di.tks:119-124` `is_static_method`; `src/checker/typer.tks:745`
  `is_instance`; `src/checker/collect.tks:721-727` `method_sig_matches` rstart/istart).
- **Base binding = `class Base (binding)`** parsed at `src/parser/parse_decl.tks:399-408` into
  `ClassBody.has_base_binding` / `base_binding_name` (`src/parser/ast.tks:235-244`). It is realized
  as a **synthetic `let <binding>: <Base> = <self upcast>` prepended to the method body**
  (`src/checker/typer.tks:3110-3128`). So the base binding is ALREADY a pure front-end desugar into
  a normal local — `base` is nothing but a fixed name for that local.
- **Instance dot-call `recv.method(a)` desugars to `Type::method(recv, a)`** — the receiver rides
  `args[0]` with an EMPTY arg-name (`src/checker/typer.tks:812-819`, `cnames` pushes `""`). So the
  "receiver is never named at the call site" rule (memory `teko-default-args-named-call`) is
  structural, independent of what we call the receiver inside the body.
- **Codegen/VM never match the receiver by name.** Codegen emits `params[0]` positionally through
  `cb_ident` (`src/codegen/codegen.tks:5950`), rewriting the placeholder `type_ann` to
  `Named{struct_name}` in the typer (`typer.tks:3057-3075`) BEFORE codegen sees it. The VM binds
  `params[0]` positionally (`src/vm/vm.tks:1229+`). **The receiver's spelling is invisible to both
  back-ends** → renaming it is guaranteed fixpoint-neutral.
- **Lexer:** `this` / `base` / `static` / `self` are NOT keywords today
  (`src/lexer/lexer.tks:266-274`; the file explicitly notes `self`/`base` are deliberately plain
  identifiers). `class`/`abstract`/`virtual`/`override`/`intern`/`interface` ARE keywords.
- **Corpus size (production `src/`):** 0 classes, **4 real interfaces** (`io/stream.tks:61-64`),
  0 traits. ~**89** `self`-receiver sites across all `.tks`+`.tkt`. The OOP mass lives in the
  **`.tkt` test files** (`parser_test`, `checker_test`, `vm_test`, `codegen_test`, `generics_test`)
  and in `synth.tks` (which SYNTHESIZES `fn eq/hash/compare/clone(self, …)`).
- **`base` is a live local variable name in production** (`src/driver.tks:177-189`,
  `src/checker/resolve.tks:947,1298,1708`, `src/compress/zlib.tks:31`). **A reserved `base` keyword
  would break these** — decisive for the contextual-vs-reserved choice (§4).
- **`this` collides with no identifier. `static` collides with no identifier** (only appears as a
  C-keyword string literal in `lir/lower.tks:257` and `codegen.tks:1105` — harmless).

---

## 1. Current-vs-proposed grammar (before / after)

### Instance method (uses receiver + a base call)

CURRENT:
```
type Animal = virtual class {
    name: str
    pub fn greet(self) -> str { self.name }
}
type Dog = class Animal(parent) {
    pub override fn greet(self) -> str { parent.greet() ~ "!" }
}
```

PROPOSED:
```
type Animal = virtual class {
    name: str
    pub fn greet() -> str { this.name }
}
type Dog = class Animal {
    pub override fn greet() -> str { base.greet() ~ "!" }
}
```

- No loose first parameter; `this` is an implicit, always-in-scope Ref to the receiver.
- No `(binding)` after the base; `base` is the implicit Ref to the upcast-self.

### Static method (a factory)

CURRENT (static = a TYPED or zero first param; `make` below is static because it has no receiver):
```
type Point = class {
    x: i64; y: i64
    pub fn make(x: i64, y: i64) -> Point { Point { x = x; y = y } }   // static (no untyped 1st param)
    pub fn dist(self) -> i64 { self.x + self.y }                       // instance (untyped `self`)
}
```

PROPOSED (static = the `static` keyword; instance = its absence):
```
type Point = class {
    x: i64; y: i64
    pub static fn make(x: i64, y: i64) -> Point { Point { x = x; y = y } }
    pub fn dist() -> i64 { this.x + this.y }
}
```

### Interface signature

CURRENT: `pub type Reader = interface { fn read(self, into: Buf) -> u64 | error }`
PROPOSED: `pub type Reader = interface { fn read(into: Buf) -> u64 | error }`

An interface method is instance-by-default (a contract on `this`); a static signature would be
`static fn …` (interfaces do not carry statics today — remains rejected, unchanged).

---

## 2. Full-stack impact map (file:line)

### 2.1 Lexer — `src/lexer/lexer.tks:266-274`, `src/lexer/token.tks`
- Add `static` as a **reserved keyword** → `TokenKind::Static` (new member in `token.tks` near
  `Intern`). Safe: no identifier named `static` exists.
- Add `this` and `base` as **CONTEXTUAL keywords**, NOT reserved (because `base` is a live local
  name in production — §0). Realize them as plain `Ident` from the lexer; the checker gives them
  meaning ONLY inside a method body (see 2.3). This is exactly the `params`/`from` contextual
  pattern the lexer already documents.

### 2.2 Parser — `src/parser/parse_decl.tks`
- **`static`** (`parse_function`, near the modifier block at `:190-197`): accept an optional
  `static` token. Add `is_static: bool` to `parser::Function` (`ast.tks`). Enforce **statics are
  TYPE-members only** (reject `static` on a top-level `fn`: `allow_receiver == false` ⇒ error).
- **Receiver injection (the keystone):** in `parse_function`, when `allow_receiver == true` and the
  method is NOT `static`, **synthesize an untyped first `Param { name = "this"; has_type = false }`**
  and do NOT consume one from the source `(...)`. When `static`, synthesize nothing. This keeps the
  ENTIRE downstream invariant `params[0].has_type == false ⇔ instance` intact with zero checker
  churn — `is_static_method`, `is_instance`, `method_sig_matches` all keep working verbatim.
  → The loose-first-arg parse at `parse_params:33-39` (`is_receiver`) is REMOVED from the new
    grammar (kept only if dual-syntax, §6).
- **Base binding removal** (`parse_class_body:399-408`): stop parsing `(binding)`. Keep
  `ClassBody.has_base_binding`/`base_binding_name` in the AST but **always set
  `has_base_binding = true` / `base_binding_name = "base"`** whenever `has_base` — i.e. the base
  local is now unconditionally named `base`. (Zero churn in `typer.tks:3199-3239`, which already
  threads these fields.)

### 2.3 Checker
- **`this` resolution** — comes for free: the synthetic `Param{name="this"}` is `define`d into the
  method env at `typer.tks:3087` exactly as `self` is today. `this.field` / `this.method()` resolve
  as ordinary field/method access on a local named `this`. **No new resolve code.**
- **`base` resolution** — comes for free: `typer.tks:3117-3128` already prepends
  `let base: <Base> = <this upcast>`; we just fix the binding name to `"base"`. **No new resolve
  code.** Guard: `base` outside a subclass method (no `has_base`) must error clearly
  ("`base` is only available in a method of a class that has a base class") — a new diagnostic in
  the resolver's unknown-name path, scoped so it never fires for the production `base` locals
  (those are outside class-method bodies, so the contextual meaning never applies).
- **`static`** — `is_static_method` (`di.tks:121-124`) becomes `m.is_static` (authoritative flag)
  while KEEPING the structural fallback for dual-syntax windows. The instance/static parity check
  (`collect.tks:721-727`) is unchanged because the synthetic receiver preserves `params[0]`.
- **`this`/`base` are RESERVED as receiver/binding names** — reject a user param or `let` literally
  named `this`/`base` inside a method (they now have a fixed meaning). New small diagnostic.
- **default-args + named-call** (`teko-default-args-named-call`): UNCHANGED. The receiver still
  rides `args[0]` with an empty name (`typer.tks:816-819`); `this` never appears at a call site.
- **interface conformance** (`method_sig_matches`): UNCHANGED (receiver-presence via synthetic
  param preserved).
- **#254 generic methods / #294 constraint dispatch (JUST landed):** both read the CURRENT receiver
  model — `receiver_canonical_name` (`collect.tks:74`) and `method_func_type` skip `params[0]`, and
  `type_method_call` builds `cargs[0] = receiver`. Because we PRESERVE `params[0]` as a synthetic
  untyped receiver, **#254/#294 need zero change**. This is the crucial reason to inject a synthetic
  receiver rather than teach the whole checker about a receiver-less shape.

### 2.4 Codegen — `src/codegen/codegen.tks`
- **Zero semantic change.** `params[0]` is still the untyped receiver with `type_ann` rewritten to
  `Named{struct_name}` by the typer (`typer.tks:3057-3075`) before codegen. The emitted C param
  name changes from `self`/`parent` to `this`/`base` — **but the C output is byte-for-byte
  identical to what you'd get by renaming the source identifier**, which is exactly the
  fixpoint-neutrality we need.
- **One belt-and-braces edit:** add `"this"` to `cg_is_c_keyword` (`codegen.tks:1103-1112`). `this`
  is a C++ keyword, not a C one, so C compiles fine today — but adding it (→ `this_`) future-proofs
  a C++ back-end and costs nothing (it only changes the emitted param name, uniformly at def+use via
  `cb_ident`, so still fixpoint-neutral).

### 2.5 VM — `src/vm/vm.tks`
- **Zero change.** The VM binds `params[0]` positionally and the base binding is a normal
  `let` statement in the TAST it interprets. `vm.tks:2842` even documents the base-binding cast as a
  same-storage upcast — untouched.

**Net:** parser + lexer + a handful of checker diagnostics change. **Codegen and VM produce the
same bytes.** This is the property that lets the fixpoint survive a corpus-wide rewrite.

---

## 3. `this`/`base`/`static` — reserved or contextual? (decided law-first)

| word     | decision              | why |
|----------|-----------------------|-----|
| `static` | **RESERVED keyword**  | no identifier collision; a modifier position (like `virtual`), M.2 wants it unmistakable. |
| `this`   | **CONTEXTUAL**        | no collision today, but keeping it contextual matches `base` and avoids reserving a common word project-wide (M.5 austerity — reserve only what must be). Meaning applies only inside a method body. |
| `base`   | **CONTEXTUAL (mandatory)** | `base` is a LIVE local name in `driver.tks`/`resolve.tks`/`zlib.tks`. Reserving it would break production code and force churn unrelated to OOP. Contextual = zero collateral. |

Contextual realization: lexer emits `Ident`; the checker treats `this`/`base` specially **only when
resolving an identifier inside a class/struct-method body** (the env already has `this` defined and,
for a subclass, `base`). Outside that scope they are ordinary names — the production `base` locals
are untouched.

---

## 4. Law / tension notes (evolution phase; log for owner review)

- **M.2 Explicit / M.3 Honest — PRO.** `this`/`base`/`static` make the receiver and staticness
  *visible and named-for-what-they-are*, replacing an implicit positional convention (an untyped
  loose arg) that devs misread. This is a legibility WIN under M.2/M.3.
- **M.5 Austerity.** Adds one reserved word (`static`) + two contextual ones. Justified: it removes
  a *steeper* implicit rule (the untyped-first-arg convention) that cost every reader deliberation.
  Net austerity is neutral-to-positive; keep `this`/`base` contextual to reserve minimally.
- **No-`ref`-keyword rule (memory `teko-ref-and-ptr-generic-ideas`, `teko-closures-design`):**
  `this`/`base` are Ref-BY-LOWERING (the same pointer-passing the current receiver uses —
  `teko-oop-w10b-design` "no `Ref<T>` receiver"). They introduce **no `ref` keyword and no `Ref<T>`
  in a receiver position** — fully consistent. Receiver-by-Ref via `this` is exactly the settled
  no-GC / arena / regions memory model (`teko-no-gc-vm-role`): `this` is an arena-backed
  pointer-lowered receiver, not a new reference type.
- **Closures capturing `this`** (`teko-closures-design`): a lambda inside a method already captures
  the local `self`; renaming it `this` changes nothing (capture is by the local's name, resolved in
  the method env). No new capture rule.
- **`static` vs the `ClassName::factory()` / `Type<Args>::fn` static-call parse (#254 L5):**
  orthogonal. `static` is a DECLARATION-site modifier; `::` is the CALL-site path. `#254`'s
  `parse_generic_static_call` (`parse_expr.tks:62`) is untouched — it resolves a static by
  path+arity, and staticness is still `params[0].has_type` (now also `is_static`). No conflict.
- **`teko-no-match-on-bool`, W15/full-Javadoc:** all NEW code (the parser edits, the codemod tool)
  ships in full-Javadoc, early-return style. The mechanical corpus rewrite is a rename, so it
  preserves existing doc-comments verbatim.

No genuine Law tension. The only open fork is process (migration shape), not law → §6 HALT.

---

## 5. Ordered crumb plan (for a teko-implementer)

Each crumb is independently gate-able (`./build/teko . -o bin` + `.tkt` gate, then
`./bin/teko . -o gen2`, then temp-normalized diff + gen1==gen2).

**C1 — lexer + AST fields (no behavior).**
- `token.tks`: add `Static` member (Javadoc). `lexer.tks:270`: `if text == "static" { return TokenKind::Static }`.
- `ast.tks`: add `is_static: bool` to `Function` (Javadoc it). Default `false` at every existing
  `Function{…}` construction site (grep `Function {` — mostly `synth.tks`, `collect.tks`,
  parser). Fixpoint: pure field addition, codegen ignores it → gen1==gen2 holds.
- Ritual: full gate. Regression: `parser_test.tkt` — `static fn` parses, `is_static==true`.

**C2 — parser: `static` + synthetic `this` receiver + `base` binding rename.**
- `parse_function`: parse optional `Static`; set `is_static`. Reject `static` when
  `allow_receiver == false` (top-level fn): "`static` is only valid on a struct/class method".
- When `allow_receiver && !is_static`: inject synthetic `Param{name="this"; has_type=false}` as
  `params[0]`; do NOT read a receiver from source. When `static`: no synthetic receiver.
- `parse_class_body`: drop `(binding)` parsing; when `has_base`, set `base_binding_name = "base"`,
  `has_base_binding = true`.
- **Dual-syntax toggle (pending §6 ruling):** if the owner picks a transition, ALSO keep the old
  loose-receiver path AND old `(binding)` path behind acceptance (emit both into the same AST); a
  later crumb removes them. If hard-cut, remove them now.
- Ritual: full gate. Regression fixtures below.

**C3 — checker diagnostics (small, additive).**
- Reject a user param/`let` named `this` or `base` inside a method body ("`this`/`base` are
  reserved receiver/base names in a method").
- `base` used where `!has_base` → clear error.
- `is_static_method` reads `m.is_static` (keep the structural fallback if dual-syntax).
- Ritual: full gate. Regression: `checker_test.tkt` cases below.

**C4 — the mechanical corpus rewrite (codemod).**
- Ship a `teko fmt`-style codemod (or a one-shot script) that rewrites every method site in
  `src/**/*.tks` and `src/**/*.tkt`:
  - drop the untyped first param, rewrite its every in-body use to `this` (rename `self`→`this`);
  - `static fn` where a method had no untyped first param;
  - `class Base(binding) {` → `class Base {`, rename in-body `binding.` → `base.`.
- `synth.tks` is CODE that emits methods — update it to emit `name="this"` + `is_static` directly
  (its synthesized `self` sites at `:199,314,365,418,475`).
- Because codegen output is unchanged, the rewrite MUST keep gen1==gen2 byte-identical. Validate
  after: temp-normalized diff=0 + gen1==gen2 (memory `selfhost-byte-identity-broken`).
- Ritual: full gate + **explicit fixpoint check** (the load-bearing gate for this issue).

**C5 — remove dual-syntax (only if §6 = transition).**
- Delete the old loose-receiver + `(binding)` parse paths and the structural static fallback.
- Ritual: full gate + fixpoint.

### Regression fixtures (inputs → expected, VM and native identical)
- `parser_test.tkt`:
  - `type C = class { pub fn f() -> i64 { 0 } }` → parses; `methods[0].params[0].name=="this"`,
    `has_type==false`, `is_static==false`.
  - `type C = class { pub static fn make() -> C { … } }` → `is_static==true`, 0 params.
  - `type D = class B { override fn g() -> i64 { base.h() } }` → `has_base`, base local == `base`.
  - `static fn topLevel() {}` at module scope → **error** (statics are type-members only).
  - `type C = class { fn f(this: i64) {} }` → **error** (`this` reserved in a method).
- `checker_test.tkt`:
  - instance method body uses `this.field` → type-checks; a sealed class + `base` → error.
  - interface `type R = interface { fn read(into: Buf) -> u64 | error }` conformance still holds.
  - a static factory called `C::make()` and an instance `x.f()` both resolve (parity unchanged).
- `vm_test.tkt` + `codegen_test.tkt`: port the existing VmCounter/VmShape/VmDogD3 fixtures to the
  new syntax; assert IDENTICAL exit codes/outputs VM and native (the whole point: behavior frozen).

### Ritual points (full gate MUST pass)
- End of C1, C2, C3, C4 (and C5 if used). C4's ritual additionally REQUIRES the byte-identity
  fixpoint (gen1==gen2, temp-normalized diff=0) — this is where a codegen regression would surface.

### Size estimate — **L** (front-end-only, tiny production corpus).
Not XL: production `src/` has 0 classes / 4 interfaces / 0 traits, and codegen/VM are untouched. The
weight is (a) the mechanical `.tkt` rewrite (~89 receiver sites, concentrated in 5 test files +
`synth.tks`), and (b) getting the fixpoint green after a corpus-wide rename. The parser/checker
delta is small and additive.

---

## 6. HALT — one owner decision (migration shape)

The design is settled; the codegen/VM are provably untouched. The ONE fork that is process-policy,
not law, and that changes the PR shape and the risk profile, is:

**FORK: hard-cut vs dual-syntax transition.**

- **(A) Hard-cut** — one atomic PR: change the grammar AND rewrite every method in the same commit;
  the old syntax stops parsing immediately.
  - PRO: no dead grammar, no ambiguity, simplest checker (single static-detector), M.5-clean.
  - CON: one big atomic PR; must coordinate with #163 (collections, IN FLIGHT with current
    syntax) — either #163 lands first, or #163 is written in the new syntax from the start.
- **(B) Dual-syntax transition** — accept BOTH syntaxes for a window; migrate the corpus; then
  remove the old grammar (crumb C5).
  - PRO: decouples from #163 and the fase-3 flood; each migration lands incrementally green.
  - CON: temporary grammar ambiguity (an untyped first param vs an implicit `this`), a two-mode
    checker, and a lingering "which is canonical" question that cuts against M.2/M.3.

**My recommendation: (A) hard-cut, sequenced BEFORE the fase-3 collections flood.** Rationale:
(1) the migration is genuinely small (front-end rename, ~89 sites, tiny production corpus, codegen
byte-neutral) so the atomic-PR cost is low; (2) a dual grammar keeps the very implicitness the owner
wants gone alive in the parser, which is an M.2/M.3 smell; (3) #163 is still OPEN with no
`src/collections` tree yet — do the syntax cut FIRST, then write #163 and the whole fase-3 stdlib in
the new syntax natively, so nothing gets written twice. Concretely: land C1-C4 as one PR
(dual-syntax skipped), gate + fixpoint green, then #163 proceeds on the new grammar.

If the owner prefers to protect in-flight #163 work, choose (B) and I will insert crumb C5 to retire
the old grammar once the corpus and #163 are both migrated.

**This is the only item requiring your ruling.** Everything else is ratified law-first above.
