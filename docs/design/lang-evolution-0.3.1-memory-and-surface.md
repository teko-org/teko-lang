# teko-lang 0.3.1 — the unified language evolution: one memory model, six surface changes

**Base:** `fix/retirement` @ `afcbc035` (or later). **Scope:** READ + DESIGN only. No product code.
This is the MASTER spec that consolidates the already-assessed memory model (DPS / arena elision /
arena floor, the `let`/`mut` analysis) with six decided surface changes, PLUS three folded-in owner
addenda (the `unsafe`/raw-pointer retirement §6.5, the machine-word integers `size`/`usize` + position
reballing §7b), and gives the implementer team one crumb-ordered migration + reseed plan. No commit
trailers (ruling 2026-07-15).

**Reads this spec builds on (do not re-litigate — consolidate):**
- `docs/design/ast-computed-arena-assessment-0.3.1.md` — DPS keystone, arena elision, arena floor,
  the `let`/`mut` removal safety verification (§4.8), the arena-safety principle.
- `docs/design/native-variant-match-root-map-0.3.1.md` — the conveyance-boundary deep root DPS
  partly closes (type_match / frame_sweep_inst / push_inst_block).
- `docs/design/arena-por-escopo-0.3.1.md` + `docs/design/o-profiler-como-afinador-de-arenas-0.3.1.md`
  — the retention model and the region-drop safety predicate.
- `docs/design/oop-this-base-static.md` (superseded on the receiver SPELLING: owner ruled `self`, not
  `this`), `docs/design/marshall-spec.md` (superseded on the pointer SHAPE: owner ruled opaque
  `ptr`/`uptr`, not generic `ptr<T>`), `src/checker/di.tks` (the annotation-era DI the keyword model
  replaces).

---

## 0. The governing safety principle (owner) — the spine of the whole wave

> Memory safety lives in **ARENA capacity + lifetime** (UAF, overflow) **+ flow / F1 exclusivity**
> (aliasing) — **NOT in any variable keyword.** (`ast-computed-arena-assessment-0.3.1.md` §4.8,
> verified against `borrow.tks`, `escape.tks`, `typer.tks`.)

Every one of the six surface changes is a corollary of this principle, and the memory model is its
mechanism. The wave is coherent because ONE discipline — *the caller's arena owns the value; the
callee writes into it; exclusivity is control-flow* — appears five times:

| the one discipline | surface expression |
|---|---|
| caller-arena destination-passing (DPS) | the native return model (§2) |
| " applied to the receiver | `self` = the caller's instance mutated in place (§7) |
| " frees the keyword from safety | `let`/`mut` → `var` (§3); `-> ref T` removal (§4) |
| arena-lifetime + type-tag replaces `unsafe` | Marshall opaque `ptr.__wrap<T>` (§8) |
| arena lifetime IS the DI lifetime | `service singleton/scoped/transient` (§9) |

The remaining two surface changes (`->` → `:` §5; `self`/`base`/`static` §7) are pure legibility
that ride the same migration. This is why the wave is **one grammar migration + one source sweep +
one reseed**, not many independent efforts.

**The ONLY wave items NOT derived from the §0 arena thesis are the two OVERLOADING features (§7c —
method overloading + operator overloading).** They are pure ergonomics; they do not flow from the
memory discipline, they COMPOSE with it (an overloaded op returning an aggregate uses DPS; distinct
overloads mangle like existing signature-distinguished symbols). Every OTHER item is a corollary of the
arena principle above. The machine-word integers + reballing (§7b) and the `unsafe`/raw-pointer
retirement (§6.5) ARE arena corollaries (they make the type say what the metal does, and they retire a
containment the arena makes unnecessary).

---

## 1. The memory model, consolidated (the foundation — already assessed)

Nothing here is new design; it is the ratified conclusion of the assessment doc, restated as the
foundation the surface changes assume. Build order inside the memory model: **DPS (gated) → elision
(parallel) → floor (folded into the profiler)**.

### 1.1 Idea 3 — DPS / caller-arena virtual return (THE KEYSTONE)

A callee allocates its return DIRECTLY into the caller's arena via a synthetic destination
(`alloc_call_dest`); `return` becomes virtual — write-into-dest + exit, no copy-out. Closes the
RETURN and TAIL-MERGE-INTO-RETURN conveyance boundaries (≈ 2 of the 3 remaining native fixpoint
blockers — `type_match`, `frame_sweep_inst`). Gated on cheap-pinning those two to the return facet
(root-map C2/C3). Retires the retrofitted `own_returned_value` box (`lower.tks:11715`) and satisfies
`frame_escape_guard` (`frame_escape.tks:56`) by construction.

**Type/fn shapes** (copy verbatim; full-Javadoc): `fn_returns_aggregate`, `with_ret_dest`,
`lower_return_into_dest`, `alloc_call_dest` — specified verbatim in
`ast-computed-arena-assessment-0.3.1.md` §4.2; do not re-author, copy from there. Existing fns
touched: `lower_return`/`lower_return_fat` (`lower.tks:7245`/`:7278`), `lower_call` (`:1740`),
`lower_block_value`/`lower_match` tail (`:10798`), retire `own_returned_value` (`:11715`) on the DPS
path.

### 1.2 Idea 2 — arena elision

A `scope_touches_arena(body): bool` predicate elides the region for alloc-free leaf scopes, guarding
`open_native_region`/`open_frame_region` (`lower.tks:1619`/`:1392`) exactly as the existing
`bracket_depth > 0` skip does (`lower.tks:1637`). Saves the 64 KiB region floor per elided leaf.
Independent, cheap, composes with DPS. Conservative: doubt → do not elide (route to enclosing region,
leak-safe, never UAF).

### 1.3 Idea 1 — arena floor

Fold the static per-scope lower bound into the profiler's existing `#arena_size` presize path
(`codegen.tks:9832`) as the `Confidence::Thin` seed. Lowest priority; not a standalone analysis. AL3
`grow_inplace` owns the slice-cap 1.8 GB, not this.

### 1.4 The safety verification carried forward

The `let`/`mut` removal (§3) rests entirely on `ast-computed-arena-assessment-0.3.1.md` §4.8: in this
compiler `BindKind` is an INTENT gate (`is_mut` gates only `&`/`ref`/`free` ergonomics,
`typer.tks:949/1769/3276/4212`), never a safety invariant; borrow safety is F1 exclusivity in
`borrow.tks`, which never reads `BindKind`. The DPS destination's write-once is
single-writer-BY-CONSTRUCTION from the call/return control flow, not from `let`. This spec does not
re-prove it; it consumes it.

---

## 2. Surface change 1 — `let`/`mut` → `var`

**Decision:** everything mutable; ONE keyword `var` for all locals (type optional, inference stays).
`const` retained. Params stay immutable (B.21) — "everything mutable" is **LOCALS ONLY**.

### 2.1 Grammar (additive transition)

- **Lexer** (`lexer.tks:339-341`): add `if text == "var" { return TokenKind::Var }` (new
  `TokenKind::Var` in `token.tks`). **KEEP** `let`/`mut`/`const` classified — during migration all
  four are accepted; `let`/`mut` become soft-deprecated no-op spellings of `var` (a lint, not a parse
  error), so the current seed's own source keeps parsing (bootstrap-additive). `const` unchanged.
- **Parser** (`parse_stmt.tks:55/194/227/256-258`, `loop_head.tks:84/98/407`): the keyword→`BindKind`
  map sends `Var`, `Let`, and `Mut` ALL to the merged local kind; `Const` stays separate. The
  `ref`→Mut desugar (`parse_stmt.tks:311`) targets the merged kind.

### 2.2 Checker / type-system

- **`BindKind`** (`ast.tks:259`): `enum { Let; Mut; Const }` → collapse `Let`+`Mut` into ONE local
  kind (name it `Var`; keep `Const`). Every `is_mut` read for a local becomes always-true.
- The three ergonomic gates (`typer.tks:949` free, `:1769`/`:3276` `&`, `:4212` `ref` source) become
  always-pass for locals; delete the now-dead "declare it `mut`" messages.
- **Params stay B.21-immutable** (`scope.tks:186/249`, `match.tks:216/253`) — a SEPARATE axis, not
  `let`. Do NOT make params mutable on this crumb.
- **CF3 const-fold re-base** (`comptime_fold.tks:2918`, `lp_is_const_binding`): today
  `k == Let || k == Const`. With `Let` gone, re-base on **FLOW-single-assignment** — a local written
  exactly once is effectively immutable and IS derivable — so the folds (and emitted bytes) survive.
  Byte-preserving; this is the one byte-mover in the change and its fixpoint gate is the proof.

### 2.3 Lowering / arena

None. Both `let` and `mut` already lower to the SAME writable slot (codegen distinguishes only
`Const`: `codegen.tks:8583` rodata, `:1447` frame-route; the `Mut` fat-rebind branch
`lower.tks:6704` re-targets the merged kind). No storage difference → a program's emitted bytes are
unchanged by the merge.

### 2.4 Byte-preservation

**Byte-preserving**, gated on the CF3 re-base holding the folds (`cf3_fold_survives_let_merge`
fixture). `gen2==gen3` unaffected; the compiler's own source never reassigns a `let`, so removing the
rejection cannot change how `src/` lowers.

---

## 3. Surface change 2 — remove `-> ref T` (return-by-reference)

**Decision:** zero production uses (verified: one probe + two rejection tests only —
`ast-computed-arena-assessment-0.3.1.md` §4.5). `ref` survives ONLY on parameters (the F1 borrow
direction). DPS subsumes every genuine `-> ref T` case (the identity pass-down returns caller-owned
storage; under DPS the value already lands in the caller's arena).

### 3.1 Grammar

Delete the `Reference` return-type arm from `parse_function`'s return parse (see §5 — the return
operator itself is changing to `:`, so this arm disappears in the same edit). `ref` in PARAMETER
position (`parse_params`) is untouched.

### 3.2 Checker

Delete the gate cluster (§4.5 of the assessment): `check_ref_return_passdown`,
`check_ref_return_passdown_stmt`, `check_ref_return_passdown_inexpr`, `ref_passdown_error`,
`ref_value_is_passdown` (~5 fns), the two invocation sites (`typer.tks:5992`, `:6378`), and
`collect_ref_param_names`'s return-gate use. One fewer return KIND in the type system.

### 3.3 Byte-preservation

**Byte-preserving** — vestigial feature removal; the form ceasing to parse IS the new rejection
fixture (`ref_return_form_rejected`, `EXPECT_COMPILE_FAIL`). The one probe is retargeted; the two
rejection tests retire.

---

## 4. Surface change 3 — `->` return operator → `:`

**Decision:** unify the type-annotation operator to `:` EVERYWHERE (var, param, field, return):
`fn f(params): T { }`. This DELETES the `Arrow` token (`->`). Match arms keep `=>` (FatArrow),
untouched.

### 4.1 Grammar (additive transition)

- **Lexer** (`token.tks:85`): `Arrow // ->`. During migration the lexer keeps emitting `Arrow` for
  `->` AND the parser learns to accept `:` in return position; then `->` is dropped from the lexer and
  `Arrow` deleted from `token.tks`. `=>` (`FatArrow`, `:86`) is never touched.
- **Parser** (`parse_function`, `parse_decl.tks:359`): today
  `if is_kind_at(tokens, p, lexer::TokenKind::Arrow)`. Additive form: accept `Arrow` OR `Colon` after
  the `)`; `:` after `)` is unambiguous (a block `{` cannot start there, and there is no other `:`
  position after a param list). After the sweep, accept ONLY `Colon` and delete the `Arrow` branch.
- Var/param/field annotations already use `:` — no change; this is purely making RETURN match them.

### 4.2 Byte-preservation

**Byte-preserving after the source sweep** — `:` and `->` lex to distinct tokens but parse to the
IDENTICAL return-type AST node, so the same program emits the same bytes. The seed must accept both
BEFORE the sweep (§12), which the additive crumb guarantees.

---

## 5. Surface change 4 — `self` / `base` keywords + `static` modifier

**Decision:** instance-by-default (members may use `self`/`base`); `static` marks class-level members
(no `self`). `base` = parent. REMOVE the receiver-param special-case
(`is_receiver`/`allow_receiver`/`allow_untyped_first` in `parse_decl.tks`) — the 1st param becomes a
normal typed param. `self` becomes a reserved-in-method keyword (today just a convention).

**Convergence (owner):** `self` = the caller's instance mutated in place = the same DPS/caller-arena
discipline. `self` is implicitly a MUTABLE receiver: the caller passes a pointer into its own arena;
the method writes through it; exclusivity is the single-writer call/return control flow (§0). This is
DPS applied to the receiver channel — §7 details the convergence.

### 5.1 Grammar (additive transition)

Follows `oop-this-base-static.md` §2 verbatim EXCEPT the receiver is spelled **`self`** (owner ruled
`self`, not `this`):

- **Lexer** (`lexer.tks:315-364`): add `static` as a **RESERVED** keyword (`TokenKind::Static`; no
  identifier collision). Keep `self` and `base` **CONTEXTUAL** (plain `Ident` from the lexer; meaning
  only inside a method body). **Do NOT reserve `base`** — it is a LIVE production local name
  (`driver.tks:177`, `resolve.tks:947/1298/1708`, `zlib.tks:31`); reserving it breaks production and
  is unrelated churn (M.5 austerity). See §5.5 (risk R2) — this softens the owner's "reserved"
  request to "reserved-in-method-body", resolved law-first, no HALT.
- **Parser** (`parse_function`, `parse_decl.tks:291`): accept optional `static`; add `is_static: bool`
  to `parser::Function`. The KEYSTONE (`oop-this-base-static.md` §2.2): when `allow_receiver && !static`,
  synthesize a first `Param { name = "self"; has_type = false }` and consume NO receiver from source;
  when `static`, synthesize nothing. This preserves the ENTIRE downstream invariant
  `params[0].has_type == false ⇔ instance` — `is_static_method` (`di.tks:147`), `is_instance`
  (`typer.tks:745`), `method_sig_matches` (`collect.tks:721`) keep working verbatim.
- **Additive window:** accept BOTH the old untyped-first-param receiver AND the new synthetic-`self`
  form; a later crumb removes the loose-receiver parse (`parse_params` `allow_untyped_first`,
  `parse_decl.tks:34`). Base binding `class Base(binding)` → `class Base`; unconditionally set
  `base_binding_name = "base"` when `has_base` (`oop-this-base-static.md` §2.2).

### 5.2 Checker

Comes for free (`oop-this-base-static.md` §2.3): the synthetic `Param{name="self"}` is `define`d into
the method env exactly as today's convention `self` is; `base` reuses the existing synthetic
`let base: <Base> = <self upcast>` prepend (`typer.tks:3110-3128`). New small diagnostics only:
reject a user param/local literally named `self`/`base` in a method; `base` where `!has_base` → clear
error. `is_static_method` reads `m.is_static` (structural fallback kept during the additive window).
#254/#294 generic-method/constraint dispatch need ZERO change (the synthetic `params[0]` is preserved).

### 5.3 Lowering / arena

**Zero semantic change; codegen byte-neutral** (`oop-this-base-static.md` §2.4): `params[0]` is still
the untyped receiver with `type_ann` rewritten to `Named{struct_name}` before codegen; the receiver's
spelling is invisible to the backend. Renaming `self`(convention)→`self`(keyword) and `parent`→`base`
is fixpoint-neutral. Belt-and-braces: add `"self"` to `cg_is_c_keyword` is unnecessary (C has no
`self` keyword), skip it.

### 5.4 Byte-preservation

**Byte-preserving** — front-end rename; codegen emits the same bytes (the property that lets a
corpus-wide method rewrite survive the fixpoint). The mechanical sweep must keep `gen2==gen3`
byte-identical; `synth.tks` (which EMITS methods, `:199/314/365/418/475`) is updated to emit
`name="self"` + `is_static` directly.

---

## 6. Surface change 5 — Marshall: opaque pointers

**Decision:** replace generic `ptr<T>` with a single OPAQUE non-generic `ptr`/`uptr`. Methods:
- `[u]ptr.__wrap<T>(): T | error | null` — **FALLIBLE**, checked cast, safe via arena-lifetime +
  type-tag, **NO `unsafe`**.
- `[u]ptr.__unwrap<T>()` — **INFALLIBLE**, pure pointer exposure.

This REMOVES the generic pointer type family (`Ptr { inner: Type? }`, `checker/type.tks`) and its
monomorphization, and REDUCES `unsafe`. Note this INVERTS and RENAMES the fallibility of the earlier
`marshall-spec.md` (`wrap`=null-panic, `unwrap`=infallible): the new `__wrap` is the *checked cast IN*
(returns `T | error | null`, no panic), the new `__unwrap` is the *raw exposure OUT*.

### 6.1 The tag + arena safety invariant (design this precisely — the load-bearing part)

An opaque `ptr` is a raw address into a **teko arena** paired with a **type-tag** derived at the
allocation site. Two representations, decided law-first:

**RECOMMENDED — the tag lives in the arena's allocation header, not in the pointer word.** Every
arena allocation already routes through `tk_region_alloc` (`teko_rt.c:2034`); a routable object
allocation records a `u64 type_tag` (the `di_type_id`-style FNV-1a of the type's canonical name,
`di.tks:373` — reuse that exact derivation, one hash function project-wide) in a per-object header
slot. `ptr`/`uptr` stay a bare machine word (C-repr `void *` / `uintptr_t`); the tag is fetched from
the header, so `ptr` remains truly opaque and word-sized (no fat pointer, no ABI change to raw C
interop). This is the same shape the native DI lifetime cache already uses
(`tk_region_register`/`tk_region_lookup` keyed by `di_type_id`, `di.tks:368`).

**The invariant `__wrap<T>` checks, in order (all dynamic, none `unsafe`):**
1. **Non-null** — address 0 → `null` (not an error; the `T | error | null` union carries it honestly).
2. **Arena-liveness** — the address falls inside a LIVE region of the region tree
   (`tk_region_lookup` walk). A freed-but-mapped address whose region is dropped → `error`
   ("pointer's arena is no longer live"). This is exactly what makes `__wrap` safe WITHOUT `unsafe`:
   the arena lifetime model, not a dev assertion, provides UAF-freedom.
3. **Tag match** — the header's `type_tag == di_type_id(T)` → return the `T` value; mismatch →
   `error` ("pointer tags type X, wrapped as T").

Because all three are dynamic and the failure modes are surfaced as `null`/`error` (never UB, never a
panic that a program cannot catch), `__wrap<T>` needs no `unsafe` context. `__unwrap<T>` performs a
pure reinterpret (the address as a `T`-typed exposure) with NO check — it is INFALLIBLE at the
crossing and, like the old `unwrap`, dangerous only if the resulting value outlives the arena; but
because the value it yields is arena-resident and the type system tracks that residence, the danger is
the ordinary escape story (A1), not an `unsafe` trapdoor.

**Honest boundary:** step 2 (arena-liveness) is sound for teko-arena-allocated pointers only. A `ptr`
minted from a foreign C address (FFI) is NOT in any teko region, so `tk_region_lookup` returns "not
found" → `__wrap` returns `error`. That is the correct, honest answer: teko cannot vouch for a foreign
address, so it refuses to wrap it into a checked `T`. Raw FFI work uses `__unwrap`/raw operators, which
make no safety claim.

### 6.1a Marshall #3 — a service becomes an opaque pointer ONLY inside the trusted backend

The earlier `marshall-spec.md` left open (#3) whether a service value can cross the ptr boundary. Under
this wave it resolves cleanly: **the ONLY place a service legitimately becomes an opaque `ptr` (an
internal `__wrap`/`__unwrap`) is inside the trusted arena/DI backend — never in user code.**

- **User code CANNOT** `__wrap`/`__unwrap` a service, and needs no special Marshall rule to be stopped:
  `__wrap`/`__unwrap` take a `ptr`/`uptr` OPERAND, and the service-escape rule (§7.4) already forbids
  passing a service as an argument — so a service value can never reach a Marshall op in user code. The
  block is a consequence of the DI escape rule, not a second mechanism.
- **The trusted backend CAN**, because it is EXEMPT (§7.4 exemption boundary): the per-arena scoped
  registry and the root singleton slots hold service instances BY POINTER, and the `svc`-substituted
  resolution code stores/loads/passes those pointers. That store-as-opaque-pointer IS an internal
  wrap; the load-back-as-`T` IS an internal unwrap. Both are compiler-synthesized (below the flow
  pass) or `teko_rt` primitives, so they are outside the checked user corpus by construction.
- **No user-reachable surface.** There is no `service -> ptr` or `ptr -> service` operation a user can
  spell; the conversion exists only as backend machinery. Safety holds because every service pointer
  the backend wraps is arena-bounded (§7.4): the internal wrap/unwrap never produces a `ptr` that
  outlives its arena, so it never needs the `unsafe`/tag-check discipline user `__wrap` carries — the
  arena that owns the instance is exactly the registry that holds the pointer.

This is the seam that closes cleanly for services: user code is blocked by the escape rule; the backend
is exempt but arena-bounded; and there is no third path.

### 6.2 Checker / type-system

- Remove `Ptr { inner: Type? }` and `Uptr {}` generic handling; replace with two atomic types
  `Ptr` (opaque) and `Uptr`. Delete the `unsafe_carrying_at` `Ptr`-recurses-into-inner arm
  (`resolve.tks:971`) — an opaque `ptr` carries no pointee type to recurse into.
- `__wrap<T>` / `__unwrap<T>` are METHODS on `ptr`/`uptr` (the `[u]ptr.__wrap<T>()` receiver form),
  resolved by the method machinery; `T` is a required explicit type argument (nothing in an opaque word
  infers it). `__wrap` result type is `T | error | null`; `__unwrap` result type is `T`.
- The monomorphization surface shrinks: no `Ptr<T>` instantiations to specialize.

### 6.3 Lowering / arena

- Allocation sites that produce a routable object gain the header tag write (one `u64` store at
  `tk_region_alloc` time; the runtime helper is an additive `teko_rt` twin —
  `tk_region_alloc_tagged(r, n, tag)` — within the runtime exception, byte-identical when the tag path
  is unused).
- `__wrap<T>` lowers to: null-test → `tk_region_lookup(addr)` → header-tag compare → branch to
  value/null/error. `__unwrap<T>` lowers to a bare reinterpret (zero instructions beyond the type
  change), the same "Ref and Ptr are the same C type" zero-cost fact the old spec relied on.

### 6.4 Byte-preservation

**Byte-preserving until adopted.** Removing the generic `Ptr<T>` family changes bytes only for source
that USES generic pointers; the compiler's own `src/` uses raw pointers only through `teko::mem`
FFI primitives (`marshall-spec.md` §4), which migrate to the opaque `ptr` — a mechanical, contained
rewrite. The tag-header runtime path is inert (byte-identical) until a `__wrap` exists to read it. Net:
this change does NOT drive the reseed by itself; it rides the source sweep.

### 6.5 The `unsafe` / raw-pointer RETIREMENT — assessment, verdict, and migration

**Owner proposal:** since safe marshalling over opaque pointers exists (`__wrap<T>(): T|error|null` +
`__unwrap<T>()`), REMOVE `unsafe` and raw pointers entirely — memory-safety by construction (arena) +
a safe FFI/marshalling boundary (Marshall). This section ASSESSES the capability gap rigorously before
speccing removal, because a false "unsafe fully removable" is worse than an honest named residual.

#### 6.5.1 What requires `unsafe` / raw pointers TODAY (the inventory, file:line, and WHY)

| # | machinery | where | why it needs unsafe today |
|---|---|---|---|
| A | **`is_unsafe` containment-by-type** — `unsafe type`/`unsafe fn`, contagion through composition | `ast.tks:434` (`TypeDecl.is_unsafe`), `:585` (`Function.is_unsafe`); `resolve.tks` `unsafe_carrying_at:971` | the gate that stops safe code NAMING a raw-carrying type (U2). Its whole job is to CONTAIN the raw pointer TYPES of B in signatures/fields |
| B | **raw pointer type family** — `Ptr{inner}` (`ptr<T>`), `Uptr` (`uptr`); raw operators `*p`/`&x`/`p+n`/`p[n]`/`p->` | `type.tks`; marshall-spec §5.5 | the C address itself; arithmetic is a machine capability the checker cannot bound |
| C | **`teko::mem` FFI/marshalling builtins** — `as_ptr`/`as_cstr`/`str_from_c`/`bytes_from_ptr`/`store_u64`/`load`/`buf_ptr`/`region_buf` | `scope.tks:1012-1046`, `:533-551` | "UNSAFE BY CONTRACT" — the checker "cannot prove `addr` names live, aligned, in-bounds memory" (`scope.tks:549-551`). Produce/consume raw `ptr<byte>` |
| D | **arena/region primitives** — `region_alloc`/`region_new`/`region_drop`(subtree) | `scope.tks`, `teko_rt` (`tk_region_*`) | the metal-bottom allocator; today reached through the same raw-`uptr`/`ptr` surface |
| E | **manual memory** — `mem::free`, `#must_free`, `Arena` (`#must_free unsafe type`, single `uptr`) | `typer.tks:941-969`, the whole `must_free_consumed_on_all_paths` dataflow `:3437-3600`; `mem/unsafe/arena.tks` | non-lexical dev-controlled bulk free; `#must_free` is the consume-or-fail dataflow that BARS THE REGION LEAK; `unsafe` CONTAINS the residual dangling-ptr-after-free (the known aliased-UAF gap, `arena.tks:9-15`) |
| F | **stdlib unsafe types** — `RawBuf` (`ptr<byte>`+len), `Owned<T>` (move-only) | `mem/unsafe/rawbuf.tks` | `RawBuf` holds a raw address+len; `Owned<T>` is the move-only handle crossing OUT of unsafe |
| G | **FFI `extern fn`** — foreign symbol declarators | `teko_rt.tks:25-60`, ~55 files | the trust assertion that a C symbol has a declared type; params restricted to primitive/`byte`/`ptr`/`uptr`/`extern type` (`typer.tks::extern_type_ok`) |

**A load-bearing ground truth:** `extern fn` (G) is NOT `unsafe`-gated today — the io/panic declarations
in `teko_rt.tks` declare `extern fn` with no `unsafe` keyword; the unsafe-ness lives entirely in the
raw pointer TYPES a signature may name (A gating B), never in an `unsafe` BLOCK. Teko has no
statement-level `unsafe { }` — `unsafe` is a declaration modifier only. This is decisive for the
verdict: **the trust an FFI call needs already has a non-`unsafe` home (the `extern` declarator).**

#### 6.5.2 Does Marshall (opaque `ptr` + `__wrap`/`__unwrap`) + arena cover each class SAFELY?

- **A (containment) — REMOVABLE.** Its sole purpose is to contain B. If B's raw generic family and
  arithmetic are gone (below) and the opaque `ptr` is made safe-by-`__wrap` + arena-backed, there is
  nothing left to contain. The gate deletes with its subject.
- **B (raw type family + operators) — REMOVED BY §6 ALREADY, and this is the crux the owner's
  intuition rests on.** The opaque `ptr` is *opaque* — no element type, no `sizeof` — so it is
  STRUCTURALLY INCAPABLE of arithmetic (`p+n`/`p[n]` need an element width the opaque ptr does not
  carry). The §6 opaque-pointer decision therefore ALREADY kills pointer arithmetic and the raw
  operators, independent of this addendum. Safe indexing is `[]byte`/slices; the raw sigils have no
  surviving safe-world use. **COVERED.**
- **C (FFI marshalling builtins) — COVERED, reclassified as SAFE, via arena + `__wrap`.** Each is
  "unsafe by contract" only because the CHECKER cannot statically prove liveness — which is exactly
  what `__wrap<T>()`'s DYNAMIC check (null + arena-liveness + tag, §6.1) now provides. `buf_ptr`/
  `as_cstr` bump-allocate INTO an arena region, so their result is arena-backed and arena-bounded (safe
  on the Teko side by §0); `bytes_from_ptr` reads back a bounded `[]byte` copy. Re-expose them as SAFE
  intrinsics that yield/consume the opaque `ptr`, with `__wrap` as the checked re-entry. **COVERED.**
- **D (arena/region primitives) — COVERED, as TRUSTED INTRINSICS (the exempt backend, §7.4).** These
  ARE the arena machinery that implements the safety; they are the same trusted-backend zone the DI
  resolver lives in — compiler/runtime builtins, not user surface. Expose them as safe intrinsics (a
  region alloc into a live region is safe by construction); no user `unsafe`. **COVERED.**
- **E (manual memory) — REMOVABLE; the arena model makes it obsolete, no safety residual.**
  `#must_free`'s entire job is barring a region LEAK — but the arena model STRUCTURALLY CANNOT leak a
  region (every region drops at its scope, or at a DI `scoped` boundary, or at a phase boundary,
  `arena-por-escopo` §2). `mem::free` on a `[]T`/class instance (`typer.tks:955`) is manual heap free —
  obsoleted by arena drop. The `Arena` non-lexical manual region reshapes into a SCOPE (a lexical
  region, or a DI `scoped` lifetime that drops at its arena boundary). The only convenience lost is a
  truly dynamic, data-dependent free point that is neither lexical nor a DI scope — rare, and its safe
  answer is "make it a scope." **COVERED (no safety residual); one exotic convenience reshaped.**
- **F (RawBuf / Owned<T>) — REMOVABLE.** `RawBuf` (raw ptr+len) becomes a safe arena-backed `[]byte`
  (the fat slice IS ptr+len, arena-bounded). `Owned<T>`'s move-only ownership is now the DPS/ownership
  model (§1.1, §8) — move-on-return is the language's ownership mechanism, not an unsafe wrapper.
  **COVERED.**
- **G (`extern fn` FFI) — the ONE GENUINE RESIDUAL, and it is NOT the `unsafe` keyword.** Teko cannot
  verify foreign C code; that trust is irreducible. But it already lives in the `extern fn` declarator
  (the signature IS the contract), which is distinct, greppable, and non-`unsafe` TODAY. Two directions,
  both covered without an `unsafe` keyword:
  - **Teko → C (into C, including write-through out-params):** allocate arena-backed storage
    (a `[]byte`/a `buf_ptr` region), `__unwrap` its backing to an opaque `ptr`, pass to C via the
    `extern fn`; C writes THROUGH it into storage Teko owns and sized; Teko reads back by length /
    `__wrap`. Memory-safe on the Teko side (arena-backed, live, sized); the trust is only "C respects
    the length it was handed" — an `extern`-signature contract, not an `unsafe` block.
  - **C → Teko (a pointer returned from C):** `__wrap<T>()` is the sole sanctioned re-entry — its
    dynamic null + arena-liveness check refuses a foreign address not in any teko region (returns
    `error`), so a C pointer can only enter the checked world through a fallible, UB-free gate.

#### 6.5.3 VERDICT — FULL removal of `unsafe`, ONE named residual (FFI trust)

**The `unsafe` keyword and its entire machinery (A, B, E, F) are FULLY REMOVABLE.** Every class either
deletes with its subject (A), is already killed by the opaque-ptr decision (B), is obsoleted by the
arena model (E, F), or is reclassified as a safe intrinsic (C, D). **The one genuine residual is FFI
TRUST (G) — irreducible because Teko cannot verify foreign code — and it is NOT `unsafe`:** it takes
the minimal safe form of **(1) the `extern fn` declarator as the trust contract** (already distinct and
non-`unsafe`) **+ (2) Marshall's fallible `__wrap<T>()` as the ONLY sanctioned re-entry of an opaque/
foreign pointer into the checked world.** No user-facing `unsafe` keyword survives; `__unwrap` makes no
unsafe operation on the Teko side (it exposes an arena-valid address whose downstream C use is governed
by the extern contract). This is the honest architect's verdict: the language ends with **no `unsafe`
surface — memory-safe by arena, safe FFI by Marshall + extern-signature trust** — which composes
directly with the de-C endgame.

#### 6.5.4 The removal spec (bootstrap-additive)

Delete, in dependency order, each an independently gate-able crumb; all are byte-preserving-until-used
except the `src/mem/unsafe` migration (mechanical, contained):

1. **Reclassify C + D as safe intrinsics** — drop the `unsafe`-by-contract status of the `teko::mem`
   builtins (`scope.tks:1012-1046`) and the region primitives; `__wrap` supplies the dynamic check the
   checker cannot. Fixture: a `buf_ptr`→C→read-back round-trip in a NON-`unsafe` fn (was rejected).
2. **Retire E (`mem::free`, `#must_free`, `Arena`)** — delete the `must_free` dataflow
   (`typer.tks:3437-3600`), the `mem::free` checker path (`:941-969`), `ast.tks:595` `must_free`, and
   `src/mem/unsafe/arena.tks`. Migrate manual-region call sites to a lexical region / DI `scoped`
   lifetime. Fixture: `arena_manual_ok` reshaped to a scope; `arena_manual_leak` (the leak-reject) is
   RETIRED — the arena model cannot leak, so the rejection has no subject.
3. **Retire F** — delete `src/mem/unsafe/rawbuf.tks` (`RawBuf`→`[]byte`, `Owned<T>`→DPS ownership).
4. **Retire B's operators** — already gone with §6's opaque `ptr` (no arithmetic surface to delete
   beyond confirming the checker rejects `p+n`/`p[n]` on an opaque `ptr` with a clear "opaque pointer
   has no arithmetic" diagnostic).
5. **Delete A (the `unsafe` keyword + `is_unsafe` + contagion)** — remove `unsafe` from the lexer's
   contextual-keyword handling, `ast.tks:434/585` `is_unsafe`, `parse_decl.tks` unsafe-modifier parse
   (`:196-223`), `resolve.tks:971` `unsafe_carrying_at`, and the `collect.tks`/`typer.tks` containment
   checks. This is the last crumb — nothing remains to contain. Fixture: a former-`unsafe fn` naming
   the opaque `ptr` now compiles as a plain `pub fn`; `unsafe` as a modifier no longer parses
   (`EXPECT_COMPILE_FAIL`, `unsafe_keyword_removed`).
6. **Keep G (`extern fn`) untouched** — it is the residual's safe home. Add one honest-stop diagnostic:
   a foreign pointer entering the checked world without `__wrap` is rejected ("wrap a foreign `ptr`
   with `__wrap<T>()` before use").

**Bootstrap-additivity:** steps 1-4 land while `unsafe` still PARSES (a no-op modifier during the
window, like the `mut`→`var` soft-deprecation §2), so the current seed keeps building; step 5 drops the
keyword only AFTER the source sweep removes every `unsafe`/`#must_free`/raw-type occurrence from `src/`.
The compiler's own unsafe surface is tiny (two files in `src/mem/unsafe`, plus the `teko::mem` builtin
declarations), so the migration is contained.

**Byte-preservation / reseed:** steps 1-3 change bytes only for the two `src/mem/unsafe` files and their
(few) call sites — mechanical, fixpoint-gated, NOT a new byte-mover for the wider self-emit. Steps 4-5
are pure deletions of already-unused surface (byte-preserving after the sweep). **This retirement does
NOT need its own reseed** — it rides the one wave reseed (§9) and the source sweep (Phase S).

---

## 7. Surface change 6 — DI: dependency injection (arena-lifetime-native)

**Decision:** new keywords `service`, `sealed`, `singleton`, `scoped`, `transient`. Services are
sealed, non-inheritable, specialize an interface:

```teko
type MyService = service singleton AnInterface { static ctor(): self { ... } }
```

`static ctor(): self` = a MANDATORY, no-arg, reserved-name static constructor. `svc<T>(): T` = the
global resolver — a COMPILE-TIME INTRINSIC (no runtime symbol, §7.3), not a generic function. This
REPLACES the annotation-era DI (`#singleton`/`#inject`, `src/checker/di.tks`)
with a first-class keyword surface, reusing the existing registry + conflict + materializer machinery.

### 7.1 Grammar

- **Lexer** (`lexer.tks:315`): add RESERVED keywords `service`, `singleton`, `scoped`, `transient`,
  `sealed` (new `TokenKind`s). Verify no production identifier collisions before reserving (grep gate;
  `sealed`/`service` are near-certainly free; `scoped`/`transient`/`singleton` appear only as DI attr
  strings today). `ctor` stays a CONTEXTUAL reserved name (a method name, meaningful only as the
  static constructor of a `service`) to avoid reserving a plausibly-used identifier.
- **Parser** (`parse_type_decl`, `parse_decl.tks:798`; `parse_class_body`, `:559`): a new type body
  form `service <lifetime> <Interface> { ... }` where `<lifetime> ∈ {singleton, scoped, transient}`
  and `<Interface>` is a single interface reference (a service specializes exactly one interface — it
  is sealed and non-inheritable). `sealed` is implied by `service` (a service is always sealed); allow
  explicit `sealed service` as a redundant-but-accepted spelling, or reserve `sealed` for a future
  non-service sealed class — RECOMMEND: `service` implies sealed, `sealed` is parsed and required-absent
  for now with an honest stop reserving it (keeps the keyword claimed without over-committing).
- Map onto the existing AST: reuse `ClassBody` + `DiKind` (`ast.tks:387`, already
  `{ None; Singleton; Scoped; Transient }`) and set a new `ClassBody.is_service = true`. The `service`
  keyword sets `di_kind` from the lifetime keyword directly (replacing the `#singleton` attribute
  parse, `parse_stmt.tks:202`, which is retired for TYPE decls but the binding-level `#singleton`
  residence attribute is a separate concern — see R5).
- `static ctor(): self`: a static method named `ctor` with return type `self` (the service type). The
  parser accepts `self` as a return type spelling meaning "the enclosing type" inside a `service`
  body.

### 7.2 The compile-time registration table (part A — how it is built)

The existing `build_di_registry` / `DiRegistry` / `DiProvider` (`di.tks:16-122`) is the table; the
keyword surface feeds it instead of the annotation surface, and the table entry is widened to carry
everything a substituted call site needs.

**Collection.** The compiler walks all `service` declarations (`register_item_providers`, `di.tks:98`,
now reading `is_service` + the lifetime keyword instead of the `#lifetime` attribute) and builds a map
from **(provided-interface canonical name, optional string key)** to **(concrete service type,
lifetime, ctor symbol, service-id)**. `choose_factory` (`di.tks:134`) is REPLACED by "the static named
`ctor`" — mandatory, no-arg; a `service` without `static ctor(): self` is a hard error.

**Table entry shape** (widen `DiProvider`, `di.tks:16`):

```teko
/**
 * One row of the compile-time service table: a single (interface, key) slot a concrete service
 * fills. A service registers ONE row per interface it transitively satisfies (§interface_ancestry),
 * so a service specializing `B: interface A` produces both a `B` row and an `A` row — which is
 * exactly what turns two services under sibling children of `A` into a `svc<A>()` conflict. The
 * `service_id` is the stable runtime identity (`di_type_id` of the concrete type) the substituted
 * call site emits as a constant to key the root singleton slot and the per-arena scoped registry.
 *
 * @field iface        canonical name of a satisfied interface (direct OR transitive ancestor)
 * @field key          the optional string key ("" = unkeyed) that partitions the table
 * @field kind         the lifetime: Singleton | Scoped | Transient
 * @field impl_name    canonical name of the concrete `service` type
 * @field ctor_symbol  the emitted symbol of the service's `static ctor` (real code)
 * @field service_id   di_type_id(impl_name) — root-slot / scoped-registry key, emitted at call sites
 * @since 0.3.1
 */
pub type DiProvider = struct { iface: str; key: str; kind: parser::DiKind; impl_name: str; ctor_symbol: str; service_id: u64 }
```

**Transitive interface satisfaction** — the source of the `svc<A>()` conflict. Register each provider
under its specialized interface AND every interface that one transitively extends
(`register_over_implements`, `di.tks:116`, today registers only the DIRECT `implements` list):

```teko
/**
 * interface_ancestry — the full set of interface canonical names a service provider satisfies: the
 * interface it specializes plus every interface that one transitively extends. Registering a provider
 * under ALL of these is what lets `svc<Ancestor>()` resolve — and what makes two providers of sibling
 * children of one ancestor a COMPILE conflict on that shared ancestor (owner's A/B/C/D/E example:
 * D→B→A and E→C→A both emit an `A` row, so `svc<A>()` matches two rows and errors, while
 * `svc<B>/<C>/<D>/<E>` each match exactly one).
 *
 * @param iface  the canonical interface name a service specializes
 * @param table  the collected type table (for the `extends` edges)
 * @return       the canonical names of `iface` and all its transitive interface ancestors
 * @since 0.3.1
 */
fn interface_ancestry(iface: str, table: TypeTable): []str
```

**Conflict detection — the exact rule.** `T` (+ key) resolves **iff EXACTLY ONE** table row matches
`(iface == canonical(T), key == given)`. **Zero** matching rows ⇒ "no service provides `T`" compile
error; **≥ 2** ⇒ "ambiguous: N services satisfy `T`" compile error (the existing
`register_over_implements` duplicate check, `di.tks:117`, now firing per `(iface, key)` pair). The
check runs at TABLE-BUILD time for same-key duplicates, and at each `svc<T>(...)` call site for the
match count (so a program that never resolves an ambiguous `A` still gets the diagnostic if two
providers register the `A` row — a duplicate REGISTRATION under one key is the error, independent of
use).

**String-key partitioning.** The match key is the PAIR `(iface, key)`. Two providers of one interface
with DIFFERENT keys fill DISTINCT rows — no conflict (the whole point). Same `(iface, key)` (including
both unkeyed, `key == ""`) ⇒ conflict. The transitive ancestor conflict applies PER KEY: `svc<A>()` is
ambiguous only among unkeyed rows reaching `A`; `svc<A>(key "x")` only among `key "x"` rows. A keyed
and an unkeyed provider of one interface never conflict. See §7.5 for the registration/resolution
syntax.

**Representation / emission — FIRMED (a): the table is FULLY compile-time-resolved.** There is NO
emitted rodata table indexed by `T` at runtime, and NO generic dispatch by type. Each monomorphized
`svc<T>(...)` call site knows `T` statically, so the compiler resolves it against the table AT COMPILE
TIME (this is where the conflict error fires) down to a single concrete service + lifetime + `service_id`
+ `ctor_symbol`, and REPLACES the call site inline with the per-lifetime code (§7.3). Only the LIFETIME
MECHANISM is deferred to runtime — and even that is inline code touching two small runtime supports
(the root singleton slots, the per-arena scoped registry), never a resolver dispatch. `service_id` is
the only table datum that survives to runtime, emitted as a constant at each substituted site.

### 7.3 `svc<T>()` is a COMPILE-TIME INTRINSIC — the substitution + runtime support (part B)

**FIRMED (owner): `svc<T>()` / `svc<T>("key")` is a compile-time intrinsic, not a runtime function.**
There is NO generic `svc` symbol in the binary and it has NO ABI — it is a builtin/comptime-expanded
form, like a macro the checker+lowering owns. The compiler REPLACES each `svc<...>` call site inline,
at compile time, with the concrete resolution code. `ctor` IS real emitted code (the service's
constructor, symbol `ctor_symbol`); its INVOCATION is wired by the substituted `svc` site, never
called through a generic resolver. This keeps DI zero-reflection, zero-generic-dispatch, pure AOT.

Part B is therefore NOT one dispatch function — it is (1) the INLINE code each `svc` site expands to,
per lifetime, and (2) the runtime SUPPORT that inline code reads/writes. Both are specified below.

**The runtime support (two data structures; reuse existing `teko_rt`, justify any addition):**

1. **Per-arena scoped registry** — REUSE the existing native `tk_region_register(region, id, ptr)` /
   `tk_region_lookup(region, id): ptr` already on the region (`di.tks:368` names them as the DI
   lifetime cache; `di_cache` is the legacy-engine twin). It is a small per-region map `service_id ->
   instance ptr` on the `tk_region` header. No NEW structure is required for the storage; the only
   possible addition is a `tk_region_parent(region): region` accessor for the ancestry walk — add it
   ONLY if not already exposed (a one-line read accessor, within the runtime exception, behavior-
   identical). The walk itself is EMITTED INLINE (consistent with intrinsic expansion), so no
   `tk_region_lookup_up` runtime helper is needed.
2. **Root singleton slots + once-guard** — a singleton's instance lives keyed by `service_id` in the
   ROOT region's registry (`tk_region_register(tk_region_root(), id, ptr)`), and the once-guard IS the
   lookup: `tk_region_lookup(root, id)` non-null ⇒ already built. Idempotent get-or-init; no separate
   boolean flag needed, and no global mutable slot outside the arena model.

**What each `svc<T>()` site expands to, inline, per lifetime** (`ctor_symbol` = the service's emitted
`static ctor`; `cur` = `region_current_vreg(ctx)`, `lower.tks:1594`; `root` = `tk_region_root_u`):

- **`singleton` — root slot, once:**
  ```
  let inst = tk_region_lookup(root, <service_id>)          // once-guard
  if inst == null {
      inst = <ctor_symbol>(root)                           // ctor allocates INTO root
      tk_region_register(root, <service_id>, inst)
  }
  // value of the svc site = inst
  ```
- **`transient` — current arena, always new:**
  ```
  <ctor_symbol>(cur)                                       // ctor allocates INTO the current arena
  // value = the fresh instance; no lookup, no register
  ```
- **`scoped` — ancestry walk, find-or-create:**
  ```
  let r = cur                                              // walk from the CURRENT (caller's) arena up
  loop {
      let hit = tk_region_lookup(r, <service_id>)
      if hit != null { break with hit }
      let p = tk_region_parent(r)
      if p == null { break with none }                    // reached root, no hit
      r = p
  }
  // on hit: value = hit
  // on miss: inst = <ctor_symbol>(cur); tk_region_register(cur, <service_id>, inst); value = inst
  ```

**The ctor-invocation + arena-placement contract.** The service's `static ctor(): self` is emitted as
an ordinary function taking a hidden destination region (the DPS discipline of §1.1 applied to the
ctor's return): `<ctor_symbol>(dest_region)` constructs the instance INTO `dest_region`. The
substituted `svc` site passes root (singleton), current (transient), or current-on-miss (scoped) as
`dest_region`. If the ctor body itself calls `svc<Dep>()`, THAT is a separately-substituted site whose
`cur` at run time is the arena active when the ctor executes — i.e. the arena of the `svc` site that
triggered this ctor. This is the "resolved relative to the CALL SITE" property: a scoped dep consumed
inside any ctor resolves against the ancestry of whatever scope invoked the outer `svc`.

**Cross-lifetime consumption, made safe by the escape rule.** A `singleton`/`transient` may CONSUME a
`scoped` service: its ctor calls `svc<ScopedDep>()`, which resolves relative to the call site's arena
(above). The hazard would be a long-lived singleton RETAINING a pointer to a narrower-lived scoped
instance — a dangling reference when that scope drops. **The escape rule (§7.4) forecloses it: the ctor
cannot STORE the scoped value in a field (that is a service-escape compile error), it can only USE it
during construction.** So the scoped instance is consumed within its own arena's lifetime and never
outlives it. This is the arena lifetime+capacity principle (§0) doing the work: the scoped instance
lives in the call-site arena, is used within that arena's lifetime, and the escape rule guarantees no
pointer to it survives the arena's drop.

Reuse verbatim: `di_type_id` (`di.tks:373`) for `service_id`; `tk_region_register`/`tk_region_lookup`
for both registries; `tk_region_root_u`/`region_current_vreg` for placement; the `di_lower_use`
materializer-call shape (`di.tks:338`) as the template for the emitted `<ctor_symbol>(dest)` call. The
ONLY candidate `teko_rt` addition is `tk_region_parent` (if not already present) — one accessor,
justified by the scoped ancestry walk, behavior-identical.

### 7.4 The escape rule (owner, STRICT) — the service-taint / flow rule

> A service VALUE can NEVER be stored in a field, passed as a param, or returned. ONLY `svc<T>()` and
> `static ctor()` may PRODUCE a service value; NO other function. Claim-by-use is forced.

This auto-closes the Marshall escape (a service can't be a param, so it can't reach `__wrap`/`__unwrap`)
and it is why services need no lifetime annotation at USE sites — the arena placement is fixed at the
producer and the value is forbidden from outliving its claim.

**Checker taint/flow rule (new — `service_taint.tks`):**

```teko
/**
 * A value is SERVICE-TAINTED iff its origin is a `svc<T>()` call or a `service` type's `static ctor`
 * return. A tainted value may be USED (its methods called, `self`-mutated in place) but may NEVER
 * ESCAPE its producing scope: it may not be assigned to a field, passed as an argument (even
 * generalized to an interface-typed param), returned, or bound into an aggregate that escapes.
 * Taint propagates through direct aliasing (a `var s = svc<T>()` binding is tainted) but a service
 * value cannot be laundered — there is no function that accepts and returns it, because accepting it
 * as a param is itself the forbidden escape.
 *
 * @param e      the typed expression under flow analysis
 * @param env    the checker environment (for binding origins)
 * @return       true iff `e` carries service taint
 * @since 0.3.1
 */
fn is_service_tainted(e: TExpr, env: Env): bool

/**
 * check_no_service_escape — reject every escape of a service-tainted value: a field store
 * (`a.serv = service_var`), an argument (`fun(service_var)`), a return, or an aggregate literal
 * element. Runs in the flow pass after typing, so the interface-generalized cases (`a.serv:
 * AnInterface = svc<Impl>()`, a param typed `AnInterface`) are caught by taint origin, not by the
 * static type — an interface-typed slot receiving a tainted value is still an escape.
 *
 * @param stmt   the typed statement
 * @param env    the checker environment
 * @return       null when no service value escapes, else the located escape diagnostic
 * @throws       on a field store / param pass / return / aggregate-bind of a service-tainted value
 * @since 0.3.1
 */
fn check_no_service_escape(stmt: TStatement, env: Env): null | error
```

The rule keys on TAINT ORIGIN, not on the declared type, precisely so interface generalization cannot
bypass it: `a.serv = service_var` and `fun(service_var)` are rejected even when `a.serv`/the param are
interface-typed. The three forbidden sinks — field store, argument, return — plus aggregate-literal
element cover every escape channel; because the only PRODUCERS are `svc<T>()` and `ctor`, and neither
can be reached with a service as input, taint has no laundering path.

**The EXEMPTION BOUNDARY (owner refinement) — the escape rule binds USER code only.** The
compiler-generated arena/DI BACKEND is a SPECIAL CASE, EXEMPT, because it is the machinery that
IMPLEMENTS the safety and MUST hold service instances by pointer: the per-arena scoped registry STORES
instance pointers (that IS how a child scope reuses a parent's scoped instance, §7.3), the root
singleton slots STORE the once-inited pointer, and the substituted resolution code PASSES those
pointers around internally. Precisely what is exempt:

- **(a) the `svc`-substituted resolution code** the compiler emits inline at each call site (§7.3), and
- **(b) the runtime arena-registry / singleton-slot operations** — `tk_region_register`/
  `tk_region_lookup` (and the `tk_region_parent` walk), whether realized as the justified `teko_rt`
  primitives or compiler-emitted LIR.

**How the compiler distinguishes trusted-backend from user code:** the taint/escape check
(`check_no_service_escape`) runs over USER-AUTHORED bodies only. The `svc`-substituted code and the
ctor's hidden-destination plumbing are SYNTHESIZED by lowering AFTER the checker's flow pass — they
never pass through `check_no_service_escape` at all, so no exemption FLAG is needed for them (the check
simply does not see synthesized nodes). For the runtime registry primitives, they are `teko_rt`
functions (or LIR builtins), not `.tks` user functions, so they are outside the checked corpus by
construction. Where a synthesized store must coexist with the user body in the SAME pass (it does not,
in this design — substitution is a lowering transform below the flow pass), the fallback marker is a
`trusted: bool` on the emitted node that `check_no_service_escape` short-circuits on; the RECOMMENDED
design needs no such marker because substitution is strictly post-flow.

**The exemption does NOT reintroduce UAF — the arena model still bounds every backend-held pointer.**
Each pointer the backend stores is bounded by the arena it lives in: a `singleton` instance is in the
ROOT region (program lifetime — never dangles), a `scoped` instance is in the region whose registry
holds it (the pointer and the instance share that region's lifetime and drop together), a `transient`
instance is in the current region and is NEVER stored (nothing to dangle). So the backend holding
service pointers is safe by the §0 principle — arena lifetime+capacity bounds them — and the escape
rule on USER code is what guarantees no USER pointer ever aliases a service past its arena. The two
together (backend exempt-but-arena-bounded, user forbidden-from-retaining) close the class.

### 7.5 String-key registration (owner asked — DESIGN THIS)

**Proposal (one line):** register an optional key at the `service` decl —
`type Fast = service singleton(key "fast") Cache { static ctor(): self { ... } }` — and resolve it with
`svc<Cache>(key "fast")`; keyed providers occupy DISTINCT table slots, so multiple providers of one
interface coexist, while the UNKEYED conflict rule (§7.2) is unchanged.

**Syntax + resolution + conflict interaction, precisely:**
- **Registration syntax:** `service <lifetime>(key <string-literal>) <Interface>`. The parenthesized
  `key "..."` rides after the lifetime keyword (the AST already carries `has_di_key`/key fields inert,
  `di.tks:9`; `ClassBody`/`TypeDecl` have `has_di_key`, `di_key_rejected` at `di.tks:258` currently
  REJECTS it — flip that rejection into acceptance). A service with no `key` is the unkeyed provider.
- **Resolution:** `svc<T>(key "fast")` looks up the `(iface_canonical, key)` slot;
  `svc<T>()` looks up the `(iface_canonical, "")` unkeyed slot. The registry key becomes the pair
  `(iface, key)` (extend `DiProvider` with a `key: str` field, `di.tks:16`; `provider_registered`
  / `resolve_provider` match on the pair).
- **Conflict interaction:** two providers of one interface with DIFFERENT keys are NOT a conflict
  (distinct slots) — this is the whole point. Two providers with the SAME key (including both unkeyed)
  ARE a conflict (the existing `register_over_implements` check, now on the pair). The TRANSITIVE
  ancestor conflict (§7.2) applies PER KEY: `svc<A>()` unkeyed is ambiguous only if two unkeyed
  providers reach A; `svc<A>(key "x")` is ambiguous only if two `key "x"` providers reach A. A keyed
  and an unkeyed provider of the same interface never conflict.
- **Escape rule unchanged:** a keyed service value is service-tainted identically; the key affects
  RESOLUTION, not escape.

This is a strict superset of core DI: with no keys, the table and every diagnostic are byte-identical
to §7.2. Keys turn the single-slot-per-interface rule into single-slot-per-(interface, key).

### 7.6 Byte-preservation

**Byte-preserving until used.** `program_uses_di` (`di.tks:36`) already gates the entire pass — a
program with no `service`/`svc` is byte-identical. The compiler's own `src/` uses no DI, so the whole
change is inert for the self-emit until adopted. Does NOT drive the reseed.

---

## 7b. Type-system change — machine-word integers `size` / `usize` + the position REBALLING

**Decision (owner):** add `size` (signed machine word) and `usize` (unsigned machine word),
pointer-sized and TARGET-DEPENDENT (32-bit vs 64-bit), as the natural integer types for the
arena/slice/pointer machinery; then REBALL every memory/collection POSITION or MEASURE — currently
hard-coded `u64` — to `usize` (and `size` where signed). *"posições que agora possuem tamanho baseado
no que o metal oferece e não fixo em u64."*

### 7b.1 The two new types + grammar

- **Lexer** (`lexer.tks:315`): `size`/`usize` are NEW primitive type names. Follow the existing prim
  convention — they are resolved as `PrimKind` cases (§7b.2), not reserved keywords, exactly as `u64`
  is a type-name identifier today, so no keyword collision and no grammar change beyond the type table.
- **Checker** (`type.tks:11` `PrimKind = enum { … U8; U16; U32; U64; … }`): add `Size` and `Usize`
  members. `prim_is_int` (`:34`) → true for both; `prim_is_signed` (`:50`) → true for `Size`, false
  for `Usize`; `prim_width` (`:63`) returns the TARGET word width (64 on the fixpoint targets, 32 on a
  32-bit target) — a target-parameterized width, the one place target-dependence enters the type.

### 7b.2 `usize`/`size` vs `uptr` — related size, DISTINCT kinds

`uptr` (`type.tks:104`, the Marshall opaque-pointer word) is an ADDRESS; `usize` is a machine-word
INTEGER (a size/count/index). They share the machine-word WIDTH but are distinct KINDS and must stay
separate in the type system:

- **`uptr`/`ptr`** = an address (Marshall). `__unwrap<T>()` yields `ptr`/`uptr`; `__wrap` consumes one.
- **`usize`/`size`** = an integer measure/position. `.len`/`.cap`/indices/offsets are `usize`.
- **Legal conversions (explicit `to` only, never implicit):** `usize <-> u64`/`u32` is ordinary
  integer transport (`to`-cast, width-checked like any int, §cast-width-hygiene). `uptr <-> usize` is
  the ptr↔word bridge — it is a Marshall op (`to_uptr`/`from_uptr` territory), NOT a plain `to`-cast,
  precisely because crossing the address/integer line is a boundary, not a value conversion. So a
  `usize` index never silently becomes an address, and an `__unwrap`ped `uptr` never silently becomes a
  length. Keep the `type.tks:177` `Uptr` same-kind-only equality; add `Size`/`Usize` as ordinary
  int-family prims that `to`-convert with the other ints.

### 7b.3 The reballing scope — resolve the position ambiguity EXPLICITLY

Two kinds of "position" exist; they reball DIFFERENTLY:

- **MEMORY / machine positions → REBALL to `usize`/`size`.** Slice `.len` and `.cap`, array/collection
  indices, byte offsets, arena offsets, `tk_region_alloc` sizes, loop counters that index memory. These
  ARE what the metal sizes; they become `usize` (or `size` where a signed delta is needed, e.g. a
  pointer difference or a signed offset). This is the owner's target.
- **SOURCE positions → STAY FIXED (`u32`).** Line/col (`ast.tks` `line: u32`/`col: u32`, `TExpr`,
  every decl), byte-in-file spans, token offsets. These are NOT machine addresses or memory measures —
  they are diagnostic coordinates whose range is a source-file property, not a metal property, and
  making them target-dependent would be meaningless (a 32-bit build does not have shorter source
  files). **Decision: source positions do NOT reball; they remain `u32`.** This matches the owner's
  reading ("memory/machine positions primarily") and keeps diagnostics target-independent.

The discriminator for the mechanical sweep: a `u64` that MEASURES or INDEXES memory/a collection →
`usize`; a `u32` that LOCATES a point in source text → unchanged; a `u64` that is a domain value
(a hash, a numeric literal's value, a timestamp) → unchanged (it is not a position).

### 7b.4 Convergence with the slice / native rep (the tie the owner named)

The slice fat header's `len` (and `cap`) becoming `usize` ties DIRECTLY to the native slice
representation — the paused #112 rep work (`native-slice-str-rep-separation-0.3.1.md`). The whole
arena/DPS/slice machinery now speaks ONE machine-word type: `alloc_call_dest`'s sizes, the DPS
destination offsets, `tk_region_alloc`'s length, `scope_touches_arena`'s counts, `tk_slice_grow_inplace`
(AL3), and the slice header `{ptr, len, cap}` all become `usize`. This is a UNIFYING move: the memory
model's measures were already all word-sized on 64-bit; reballing makes the TYPE say what the metal
already does, which is exactly what lets the #112 rep work resume against a single position type rather
than a hard-coded `u64`.

### 7b.5 Byte-preservation on the fixpoint targets — the load-bearing argument

**On the 64-bit fixpoint targets (x86_64, arm64), `usize == u64` and `size == i64`, bit-for-bit.**
`prim_width(Usize) == 64` there, and the lowering emits the SAME machine type (`i64`/`u64`) it emits
for `u64` today. So `u64 → usize` on a position CHANGES the checker's type but lowers to the IDENTICAL
emitted bytes → **`gen2==gen3` holds**, and the reballing is byte-preserving on every fixpoint target.
Confirm precisely at the lowering: `Usize`/`Size` map to the same LIR/C integer type as `U64`/`I64`
(`lower.tks`/`codegen.tks` prim→machine-type table gains `Usize => i64`, `Size => i64` on a 64-bit
target). 32-bit (where `usize == u32`) is NOT a fixpoint target, so it never enters the `gen2==gen3`
comparison; its correctness is a separate cross-compile property, not a fixpoint gate.

Because it is byte-preserving on 64-bit, the reballing rides the SOURCE SWEEP (Phase S) and needs NO
separate reseed — it is a mechanical mass rewrite (every `.len: u64`, every index, every `to u64` on a
position → `usize`), validated by the fixpoint byte-identity like every other Phase-S sweep.

### 7b.6 Migration crumbs (Phase G additive + Phase S sweep)

- **G-crumb (additive):** add `Size`/`Usize` to `PrimKind` + the prim predicates + the lowering
  prim→machine-type table (`Usize => i64`, `Size => i64` on 64-bit), and make `usize`/`size` resolvable
  type names. Inert until used — `src/` still says `u64`, so byte-identical. Lands in Phase G (see §10,
  crumb G9).
- **S-crumb (sweep):** mechanically rewrite memory/collection positions in `src/` + `.tkt` from `u64`
  to `usize` (and `size` for signed deltas): slice `.len`/`.cap`, indices, offsets, arena sizes, the
  slice header, the DPS/AL3 machinery. Source positions (`line`/`col` `u32`) untouched. Byte-preserving
  on 64-bit → fixpoint-gated. Lands in Phase S (see §10, crumb S6).

---

## 8. The `self` ↔ DPS convergence (owner point 4, made precise)

`self` is a mutable receiver passed as a caller-arena pointer; a method mutating `self.field` writes
THROUGH that pointer into the caller's arena — structurally identical to a DPS destination write. The
two share one lowering discipline and one exclusivity proof:

- **Placement:** the receiver rides `params[0]` as a pointer into the caller's current region
  (`region_current_vreg`), exactly as `alloc_call_dest` reserves the return destination. Nothing new
  in codegen — the receiver already lowers by-address (`oop-this-base-static.md` §2.4).
- **Exclusivity:** during a method call the caller holds ONE receiver, passes it to ONE method, does
  not alias it until the call returns — the same single-writer-by-construction the DPS destination
  enjoys (§0). "Everything mutable" (§2) is safe for `self` for the same reason it is safe for locals:
  aliasing safety is control-flow F1 exclusivity, not a keyword.
- **Consequence:** removing `-> ref T` (§3) is consistent — the escape direction is DPS/`self`
  (callee writes caller storage), the borrow direction is `ref` params (F1). Two orthogonal
  mechanisms, no straddling.

No separate lowering work: `self`-as-mutable-receiver is DPS's receiver instance, already covered by
the DPS crumbs (§1.1) plus the front-end rename (§5).

---

## 7c. Overloading — methods + operators (ERGONOMICS, NOT arena corollaries)

**These two are the ONLY wave items NOT derived from the §0 arena thesis** (noted there): they are pure
ergonomics that COMPOSE with the discipline rather than flow from it. The composition points, stated up
front: an overloaded function/operator that returns an AGGREGATE is an ordinary aggregate-returning
function, so DPS (§1.1) applies — its result is born in the caller's arena; and distinct overloads mangle
to distinct symbols via the EXISTING signature-mangling, exactly like today's monomorph instances. Both
are feature-gated-inert (a program with no same-name defs and no dunder methods is byte-identical), so
they ride the one wave: Phase G adds the capability, Phase S sweeps only if `src/` chooses to adopt.

### 7c.1 Method (function) overloading — same name, different signatures

**Grounding.** Today the checker REJECTS same-name definitions: functions via `revalidate.tks:6`
("no duplicate definition"), and interface conformance already distinguishes the two cases at
`resolve.tks:878` ("Same name + SAME signature is fine (equivalent, kept once); same name + DIFFERENT
signature is [an error]"). Overloading RELAXES the function reject: same name is allowed IFF the
parameter signatures differ. A single-def name resolves exactly as today (fully additive for
non-overloaded code).

**Grammar.** None. Overloading is purely a checker relaxation — no new tokens, no new syntax. Two
`fn f(...)` decls with the same name and different param signatures both parse today; only the checker
currently rejects the second.

**Checker — the relaxation + resolution.**
- **Definition rule:** an environment may bind N functions to one name provided their PARAMETER
  signatures are pairwise distinct (by the same signature identity AL4a already computes for mangling,
  §mangling below). Two defs with identical parameter signatures remain a redeclaration error
  (`revalidate.tks:6` fires as today) — **the return type is NOT part of signature identity for the
  distinctness check** (below).
- **Resolution axis = PARAMETER signatures ONLY, never the return type.** A call site provides argument
  types, not an expected return; disambiguating on return type would make resolution depend on the
  call's context, which teko does not do (M.3 honesty — the reader resolves the overload from the
  arguments alone). Enforce: the return type is ignored in overload selection, and two overloads that
  differ ONLY in return type are a compile error (they are indistinguishable at every call site).
- **Resolution rule (candidate selection):** at a call `f(args)`, collect the overload set for `f`;
  filter to candidates whose arity + parameter types ACCEPT `args`; then:
  1. **Exact match first** — a candidate whose parameter types EQUAL the argument types wins outright.
  2. **Else the existing coercion/widening** — the same implicit widenings the checker already applies
     to a single call (numeric-literal context typing, `literal-context-typing.md`; `null`→union). A
     candidate reachable only by widening ranks below an exact match.
  3. **Tie-break / AMBIGUITY** — if ≥2 candidates are equally good (both exact, or both reachable only
     by the same-rank widening), it is a COMPILE ERROR ("ambiguous call to `f`: N overloads match
     `(argtypes)` equally"), never a silent pick. This is the "no magic values" spirit: the compiler
     refuses to guess. Reuse the existing `ambiguous` diagnostic idiom (`resolve.tks:697/893`).
- **This EXTENDS the call path, it does not invent a resolver from scratch.** Today a callee name
  resolves to ONE `Func` type; overloading makes that lookup return a candidate SET and adds the
  selection above. The candidate-cursor machinery `TtCands`/`tt_cands` (`resolve.tks:147-218`) already
  iterates same-key candidates for type lookups and interface dispatch — the overload set reuses that
  shape (a linear candidate scan keeping the best match, ambiguity on a tie). The achatamento plan's
  CK3 "overload resolution" step is where this lands; this is its content.

```teko
/**
 * select_overload — choose the one function an overloaded call resolves to, by PARAMETER signatures
 * only (never the return type). Filters the candidate set to those accepting `args`, prefers an exact
 * parameter-type match over one reached by the checker's existing implicit widenings, and REJECTS a
 * tie (>=2 equally-good candidates) as an ambiguous-call compile error rather than picking silently.
 *
 * @param cands  the functions bound to the called name (>=1; a single-def name trivially returns it)
 * @param args   the argument types at the call site
 * @return       the selected function, or an ambiguity/no-match error
 * @throws       when no candidate accepts `args`, or >=2 accept it equally well
 * @since 0.3.1
 */
fn select_overload(cands: []checker::TFunction, args: []Type): checker::TFunction | error
```

**Mangling — distinct overloads get distinct symbols FOR FREE.** Emitted function symbols already
encode the signature: the AL4a signature-interning/mangling that distinguishes monomorph instances
(`al4a-interning-design.md`; `mangle_type_name` and the function-symbol mangler, `codegen.tks:517-532`)
produces a signature-distinguished symbol, so two overloads of `f` mangle to two different symbols with
no new mangling work. Confirm: the function symbol must include the parameter-signature component (it
does for generics; overloads reuse the same component). Byte-preservation depends on this — a
non-overloaded `f`'s symbol must be UNCHANGED (do not add a signature suffix to single-def names, or
every existing symbol moves; only overloaded names need the disambiguating suffix, exactly as today).

**Interactions.**
- **Generics / monomorphization:** an overload set MAY contain a generic member. Resolution runs on the
  DECLARED signatures; a generic candidate matches by unifying its type params against `args` and ranks
  as a match (below exact, at widening rank — a concrete exact overload beats a generic one, the C++/
  Rust intuition). After selection, the chosen generic member monomorphizes as today. No new monomorph
  surface — selection just happens before instantiation.
- **Methods vs free functions:** overloading applies to BOTH. For struct/class METHODS, the synthetic
  `self` receiver (§5) is `params[0]`; two methods `f(self, a: i64)` and `f(self, a: str)` overload on
  the NON-receiver params (the receiver type is fixed by the method's owning type, so it never
  participates in disambiguation within one type). Method resolution already dispatches by receiver
  type then name; overloading adds the param-signature selection after the name match.
- **DI conflict rule (orthogonal — NON-interaction):** the §7.2 DI conflict is about INTERFACE
  PROVIDERS (two services claiming one interface), resolved by `(interface, key)` in the static table.
  Function overloading is about SAME-NAME FUNCTIONS resolved by parameter signatures. Different
  namespaces (the DI table vs the fn environment), different keys (interface vs param-sig), no shared
  mechanism. State explicitly: they do not interact.

**Byte-preservation.** Additive — a program with no same-name defs resolves and mangles identically.
**Confirm before the sweep:** grep `src/` for accidental same-name defs that overloading would now
ADMIT and thereby change meaning (today they are a redeclaration error, so none can exist in compiling
source — the reject guarantees `src/` has zero same-name collisions, so enabling overloading cannot
change how existing `src/` resolves; the check is a belt-and-braces confirmation, not a risk).

### 7c.2 Operator overloading — behavior for user types (NOT casting)

**Owner:** customize operator BEHAVIOR for user types; explicitly NO C#-style `implicit`/`explicit`
CONVERSION operators — conversion stays `to` (explicit), a SEPARATE axis from operator behavior. So
this adds no coercion path; `a + b` on user types calls a user method, it never converts `a` to `b`'s
type.

**The desugar target = a DUNDER METHOD convention, consistent with `__wrap`/`__unwrap` (§6) and the DI
`__di_materialize`/`ctor` convention (§7).** When an operand's type defines the dunder, the operator
desugars to the method call; primitives keep the builtin path untouched.

**The operator → dunder map + signature shape** (each dunder is an INSTANCE method with a `self`
receiver, fitting §5; an aggregate return uses DPS, §1.1):

| operator | dunder | signature shape |
|---|---|---|
| `a + b` | `__add` | `fn __add(self, rhs: T): R` |
| `a - b` | `__sub` | `fn __sub(self, rhs: T): R` |
| `a * b` | `__mul` | `fn __mul(self, rhs: T): R` |
| `a / b` | `__div` | `fn __div(self, rhs: T): R` |
| `a % b` | `__mod` | `fn __mod(self, rhs: T): R` |
| `a == b` | `__eq` | `fn __eq(self, rhs: T): bool` |
| `a < b` | `__lt` | `fn __lt(self, rhs: T): bool` |
| `-a` | `__neg` | `fn __neg(self): R` (unary) |
| `~a` | `__not` | `fn __not(self): R` (unary bitwise NOT; distinct from binary `~` concat, typer.tks:280) |
| `a[i]` | `__index` | `fn __index(self, i: I): R` |
| `a & b` / `a \| b` / `a ^ b` | `__band`/`__bor`/`__bxor` | bitwise (opt-in; see below) |

**Comparison derivation (RECOMMENDED — automatic).** A user type defines `__eq` + `__lt` ONLY; the
compiler DERIVES `!=` (¬`__eq`), `>` (rhs `__lt` self), `<=` (¬(rhs `__lt` self)), `>=` (¬`__lt`). One
source of truth, fewer methods — the owner's no-redeclaration ethos. Derivation is AUTOMATIC from
`__eq`/`__lt`; a type defining neither has no comparison operators (the builtin path stays for
primitives). A type MAY define `__eq` without `__lt` (equality only, no ordering); `<`/`>`/`<=`/`>=`
then remain undefined for it (a clear "type T defines no ordering" error), while `==`/`!=` work.

**Which operators are overloadable, and which are NOT (decided + justified):**
- **Overloadable:** arithmetic (`+ - * / %`), comparison (`== <` → derived `!= > <= >=`), unary
  `-`/`~`, index `[]`, and bitwise (`& | ^`) as an OPT-IN (they are genuine numeric-like behavior on a
  user bignum/bitset; low risk).
- **NOT overloadable, and why:** assignment `=` (rebinding is the language's storage model, not a
  behavior — overloading it invites the C++ copy-assignment morass); member/path `.`/`::` (resolution,
  not computation); logical short-circuit `&&`/`||` (overloading them would force EAGER evaluation of
  both operands, DESTROYING short-circuit semantics — a correctness trap, M.3); the concat `~` binary
  form stays the builtin string concat (`typer.tks:280`) — a user type wanting concatenation defines a
  named method, not an overload of the string operator. Range/`in`/`to` are cast/membership operators,
  a separate axis (owner: conversion stays `to`).

**Checker.** Operators are builtin-only in the typer today (`check_binary`/`check_unary`/`check_compare`
regimes, `typer.tks:278-479`). The desugar adds ONE branch at the front of each operator's typing: if a
user-typed operand's type defines the matching dunder (a method lookup on the type), REWRITE the
operator node to a method call (`a.__add(b)`) and type THAT; else fall through to the existing builtin
path (byte-identical for primitives). Because it is a rewrite to an ordinary method call, overload
resolution (§7c.1), generics, DPS, and mangling all apply with zero extra machinery. Guard: both
operands primitive → never look up a dunder (the builtin path is unconditional for prim/prim), so the
hot arithmetic path is untouched.

**Lowering / arena.** None new. `a.__add(b)` is an ordinary method call; if `R` is an aggregate it
returns via DPS (§1.1) into the caller's arena — the composition the owner named. Comparison dunders
return `bool` (scalar, no DPS).

**Byte-preservation.** Builtin ops on primitives are unchanged (the prim/prim guard). The user-op
desugar is INERT until a user type defines a dunder — `src/` is byte-identical until it adopts one. The
comparison-derivation is compile-time desugar (no runtime cost, no new symbols beyond the `__eq`/`__lt`
methods the type already defines).

### 7c.3 Migration crumbs

- **G10 — method overloading:** relax the same-name reject to param-signature distinctness
  (`revalidate.tks:6`); add `select_overload` candidate selection at the call path (extending the CK3
  step); confirm the function-symbol mangler emits the signature component for overloaded names only.
  Inert until `src/` defines an overload set. Size L. Ritual: full gate (byte-identical).
- **G11 — operator overloading:** add the dunder-lookup branch to each operator's typing
  (`typer.tks:278-479`), the operator→dunder map, and automatic `__eq`/`__lt` comparison derivation.
  Inert until a user type defines a dunder. Size L. Ritual: full gate (prim path byte-identical).
- **S7 (optional) — adopt in `src/`:** IF the compiler's own source chooses to use an overload set or
  an operator dunder (e.g. a `bigint`/`dec` `__add`), that adoption is a Phase-S sweep, byte-moving
  only where adopted, fixpoint-gated. If `src/` adopts nothing, S7 is empty and the feature ships
  purely as a user-facing capability.

---

## 9. Byte-preservation, the fixpoint, and the ONE reseed

### 9.1 Classification

| change | byte-preserving? | why |
|---|---|---|
| `let`/`mut` → `var` (§2) | **YES** | same slot; CF3 re-based on flow-single-assignment holds folds |
| remove `-> ref T` (§3) | **YES** | vestigial; no `src/` uses it |
| `->` → `:` (§4) | **YES** (post-sweep) | distinct tokens, identical AST/bytes |
| `self`/`base`/`static` (§5) | **YES** | front-end rename; codegen byte-neutral |
| Marshall opaque `ptr` (§6) | **YES until adopted** | tag path inert; `src/` FFI migration mechanical |
| `unsafe`/raw-ptr retirement (§6.5) | **YES** | deletes unused surface; obsoleted by arena; post-sweep |
| DI `service`/`svc` (§7) | **YES until used** | `program_uses_di`-gated; `src/` uses no DI |
| `size`/`usize` + reballing (§7b) | **YES on 64-bit targets** | `usize == u64` bit-for-bit; same lowered bytes |
| method overloading (§7c.1) | **YES until used** | additive; single-def names resolve+mangle identically |
| operator overloading (§7c.2) | **YES until used** | prim/prim path guarded; dunder desugar inert until a type defines one |
| **DPS / caller-arena return (§1.1)** | **MOVES BYTES** (deterministic) | native return ABI change; `gen1≠gen2`, `gen2==gen3` HOLDS |
| arena elision (§1.2) | MOVES BYTES (deterministic) | removes region new/enter/leave/drop; `gen2==gen3` holds |

**The only byte-mover that drives the seed is DPS (+ elision).** Every surface change is either pure
syntax (same bytes) or feature-gated-inert (same bytes until a program opts in). The self-emit fixpoint
invariant `gen2==gen3` is preserved by all of them because DPS/elision are DETERMINISTIC lowering
changes (the assessment doc §4.6: `gen1≠gen2` expected, `gen2==gen3` inviolable; the fixpoint is
self-consistency, NOT native==C).

### 9.2 Why exactly ONE reseed, and where it falls

The reseed (`.github/workflows/reseed-bootstrap.yml`) is needed for TWO reasons that fold into ONE
harvest:

1. **DPS is a self-lowering change** (the exact class the workflow header cites, like `74622f77`): the
   native return ABI does not reach the built gen1 until the seed carries a gen1 emitted WITH DPS. Until
   reseed, gen0-from-stale-seed lowers gen1 the old way and the native self-emit chain has no DPS.
2. **The additive grammar must be in the seed BEFORE the source sweep.** The seed (previous released
   binary's emitted C) only knows the OLD grammar. To sweep `src/` to `:`/`var`/`self`, the seed must
   already ACCEPT them. So the additive-acceptance crumbs (§§2.1, 4.1, 5.1 — written in OLD spelling so
   the current seed parses them) land FIRST, then the reseed captures a seed that both DPS-lowers and
   parses-new, and only THEN does the byte-preserving source sweep run.

The reseed's green criterion is the **C-route fixpoint** (the harvested C reproduces itself down the C
route); DPS is byte-preserving for the C route, so the reseed passes. The deeper **native gen2==gen3**
is proven by the drained PR's own matrix, not by the harvest — which is correct, because the reseed
EXISTS to unblock that native fixpoint. Drain is a **cherry-pick, no PR/merge** (branch protection has
no bypass); the lane's own 100%-green promotion is the gate.

**The wave = one grammar migration (additive) + one source sweep + one reseed.** No change needs a
second seed swap.

---

## 10. Crumb ordering — the master sequence

Each crumb is independently gate-able. Ritual = full native ladder (genB→gen2→gen3) + fixpoint where
marked. Sizes: S/M/L. The sequence is dependency-correct; the reseed is the hinge.

**Phase P — PIN (go/no-go, no ritual):**
- **P1 — pin `type_match` + `frame_sweep_inst` to the return/tail-merge facet** (root-map C2/C3),
  minimal-repro + objdump. **Size S. This is the go/no-go for the whole DPS-as-fixpoint-fix bet
  (§11).** If they pin to a payload-bind offset instead, DPS still lands for memory+correctness but the
  fixpoint grind is decoupled — re-plan §11.

**Phase A — MEMORY MODEL (byte-mover; lands before the reseed):**
- **A1 — instrument the return-box volume** (assessment D0). Size S. No ritual.
- **A2 — DPS ABI + `lower_return_into_dest` + `alloc_call_dest`**, entered only when a destination is
  passed (`ret_dest=null` = today's path, byte-identical); tail merges target `ret_dest`. **Size L.
  Ritual: full ladder + `gen2==gen3`.**
- **A3 — retire `own_returned_value` on the DPS path**; `frame_escape_guard` stays as the inversion
  net. Size M. Ritual: fixpoint + guard clean + P1's two blockers green.
- **A4 — arena elision** (`scope_touches_arena` guard, §1.2). Size M. Independent of A2/A3; can land in
  parallel. Ritual: full ladder.
- **A5 — push_inst_block point-fix** (self-append / AL3 `grow_inplace` boundary — NOT DPS, §11). Size
  M. Ritual: full ladder. **Land BEFORE the reseed so all 3 native blockers are addressed pre-seed.**

**Phase G — ADDITIVE GRAMMAR (byte-preserving; written in OLD spelling so the current seed parses it):**
- **G1 — additive `:` return operator** (accept `Arrow` OR `Colon`, §4.1). Size S. Ritual: full gate.
- **G2 — merge `Let`/`Mut` into `var`; accept `var`/`let`/`mut`**, retain `Const`, re-base CF3 on
  flow-single-assignment (§2). Size M. Ritual: fixpoint + `cf3_fold_survives_let_merge`.
- **G3 — `static` keyword + synthetic `self` receiver + `base` rename** (accept BOTH old loose-receiver
  and new synthetic-`self`, §5.1). Size L. Ritual: full gate.
- **G4 — remove `-> ref T`** return arm + gate cluster (§3); rides G1's return-parse edit. Size S.
  Ritual: fixpoint + `ref_return_form_rejected`.
- **G5 — Marshall opaque `ptr`/`uptr` + `__wrap`/`__unwrap` + tag runtime** (§6), inert until used.
  Size L. Ritual: full gate (tag path inert = byte-identical).
- **G6 — DI `service`/`svc` keyword surface + escape taint + string-key** (§7), inert until used;
  reuses `di.tks`. Size L. Ritual: full gate (`program_uses_di`-gated = byte-identical).
- **G7 — reclassify the `teko::mem` + region primitives as SAFE intrinsics** (§6.5.4 step 1); `__wrap`
  supplies the dynamic check. `unsafe` still parses (no-op window). Size M. Ritual: full gate.
- **G8 — retire manual memory (`mem::free`/`#must_free`/`Arena`) + `RawBuf`/`Owned<T>`** (§6.5.4 steps
  2-3); migrate the few call sites to lexical/DI-scoped regions. Size M. Ritual: fixpoint (mechanical,
  contained to `src/mem/unsafe`).
- **G9 — add `size`/`usize` to `PrimKind` + prim predicates + the lowering prim→machine-type table**
  (`Usize`/`Size` => `i64` on 64-bit, §7b.6). Inert until used (`src/` still says `u64` = byte-
  identical). Size M. Ritual: full gate (byte-identical).
- **G10 — method overloading** (§7c.3): relax the same-name reject to param-signature distinctness;
  add `select_overload` at the call path; overloaded-only symbol suffix. Inert until an overload set
  exists. Size L. Ritual: full gate (byte-identical).
- **G11 — operator overloading** (§7c.3): dunder-lookup branch per operator + the op→dunder map +
  automatic `__eq`/`__lt` comparison derivation. Inert until a type defines a dunder. Size L. Ritual:
  full gate (prim path byte-identical).

**Phase R — THE RESEED (the hinge):**
- **R1 — one reseed** via `reseed-bootstrap.yml` (dispatch by ref on the lane), cherry-pick drain, no
  PR. Captures a seed that DPS-lowers AND parses the new grammar. Gate: C-route self-reproduce +
  provenance; the drained lane proves native `gen2==gen3`. Size S (operational).

**Phase S — SOURCE SWEEP + DROP-OLD (byte-preserving; post-reseed, no further seed):**
- **S1 — sweep `src/` + `.tkt` to `:` returns.** Size M (mechanical). Ritual: fixpoint (byte-identical).
- **S2 — sweep to `var`; drop `let`/`mut` acceptance** (soft-deprecation → removed). Size M. Ritual:
  fixpoint.
- **S3 — sweep methods to `self`/`base`/`static`; remove the loose-receiver parse + `allow_untyped_first`.**
  Size L (the ~89 receiver sites + `synth.tks`). Ritual: fixpoint (the load-bearing byte-identity gate).
- **S4 — drop `->`/`Arrow`** from lexer+`token.tks`; migrate `src/` FFI to opaque `ptr`. Size M.
  Ritual: fixpoint.
- **S5 — DELETE the `unsafe` keyword + `is_unsafe` + contagion** (§6.5.4 steps 4-5), after the sweep
  removes every `unsafe`/`#must_free`/raw-type occurrence from `src/`. Last crumb — nothing remains to
  contain. `extern fn` (G) untouched, gains the "wrap a foreign `ptr` with `__wrap<T>()`" honest-stop.
  Size M. Ritual: fixpoint + `unsafe_keyword_removed` (EXPECT_COMPILE_FAIL).
- **S6 — REBALL memory/collection positions `u64` → `usize`/`size`** (§7b.3/§7b.6): slice `.len`/`.cap`,
  indices, offsets, arena sizes, the slice header, DPS/AL3 machinery. Source positions (`line`/`col`
  `u32`) untouched. Byte-preserving on 64-bit (`usize == u64`). Size L (mechanical mass rewrite).
  Ritual: fixpoint byte-identity (the proof that `usize` lowers identically to `u64` on the targets).
- **S7 (optional) — adopt overloading in `src/`** (§7c.3): only if the compiler's own source chooses an
  overload set or an operator dunder; byte-moving only where adopted, fixpoint-gated. Empty if `src/`
  adopts nothing (the features ship as user-facing capability regardless). Size S-M. Ritual: fixpoint.

**Independence map:** G1–G11 are mutually independent additive crumbs (each gate-able alone) and
independent of Phase A EXCEPT that A must land before R (DPS in the seed); G7/G8 (safe-intrinsic
reclassify + manual-memory retire), G9 (`size`/`usize` add), and G10/G11 (method + operator
overloading) all ride the same additive window, inert until adopted. S1–S7 each depend only on R and on
their matching G crumb; S5 (delete `unsafe`) must follow the sweeps that remove every `unsafe`/raw
occurrence, S6 (reball) is independent of the other sweeps but shares their fixpoint-byte-identity gate,
and S7 (adopt overloading) is optional and empty unless `src/` uses the feature. A4 (elision) and A5
(push_inst_block) are independent of the DPS core and of each other.

---

## 11. DPS ↔ native-fixpoint sequencing (the recommendation)

State on `fix/retirement`: 5 of ~8 bugs fixed; 3 remain — `type_match`, `frame_sweep_inst` (both the
by-address aggregate-through-merge/return family) and `push_inst_block` (self-append slice-header).

**Recommendation: FOLD DPS into the fixpoint grind for the two return-facet blockers; keep
push_inst_block a separate point-fix.**

- **P1 pins first.** If `type_match`/`frame_sweep_inst` pin to the return / tail-merge facet (the
  shape both actually have — a `TExpr`/`FrameSet` returned through a tail match/if merge), then
  **build DPS (A2/A3) INSTEAD of point-fixing them** — DPS closes both by construction (the value is
  born in the caller's arena; each tail arm lowers into the shared `ret_dest`), and the same change
  buys the memory win. This is the two-birds path.
- **push_inst_block is NOT DPS** — it is the self-append boundary (AL3 `grow_inplace` /
  materialize-at-append). Fix it separately (A5), BEFORE the reseed, so the seed does not lock in a
  still-crashing native chain.
- **If P1 disconfirms** (payload-bind offset, not return facet): DPS still lands for memory +
  return-correctness, but it does NOT close those two — then the fixpoint track reverts to the
  root-map's cheap-pin-and-point-fix grind (C5..Cn) and is sequenced INDEPENDENTLY of the surface
  wave. The surface wave (G/S) does not depend on the fixpoint being green — it depends only on the
  C-route reseed. So a disconfirming pin decouples the tracks; it does not block the surface changes.

**Sequencing verdict:** run P1 first; if confirmed, the fixpoint track and the DPS memory track are the
SAME work (A2/A3 double as the fix for 2 of 3), with A5 mopping up the third — all before R. The
surface grammar (G) can proceed in parallel with A on independent files, converging at R.

---

## 12. Regression fixtures (inputs → expected native exit codes)

Memory-model fixtures (from the assessment doc, carried in): `dps_aggregate_return_value_correct` (0),
`dps_variant_match_return` (0), `dps_frameset_if_return` (0), `dps_no_frame_escape` (0),
`dps_caller_dest_not_dropped` (0 / inversion fails), `reassign_former_let_now_compiles` (0),
`dps_dest_single_writer` (inversion fails), `cf3_fold_survives_let_merge` (0),
`arena_elided_leaf_scope` (0).

Surface fixtures (new):

| fixture | asserts | expected |
|---|---|---|
| `var_all_locals_mutable` | `var a = 0; a = 1` compiles; `const` still rejects reassign | 0 |
| `mut_accepted_as_var_softdep` | `mut a = 0` still parses during the additive window | 0 |
| `ref_return_form_rejected` | `fn f(): ref T` no longer parses | EXPECT_COMPILE_FAIL |
| `colon_return_operator` | `fn f(): T { }` parses; `->` also parses (additive window) | 0 |
| `arrow_token_removed` | `fn f(): T` no longer parses (post-S4) | EXPECT_COMPILE_FAIL |
| `self_keyword_receiver` | `fn f(): i64 { self.x }` types; no loose receiver param | 0 |
| `base_call_in_override` | `base.g()` in a subclass method resolves | 0 |
| `base_still_a_local_name` | production `base` local (non-method) still compiles (contextual) | 0 |
| `static_modifier_factory` | `static fn make(): C { }` has 0 params, `is_static` | 0 |
| `self_param_rejected` | a user param named `self` in a method is rejected | EXPECT_COMPILE_FAIL |
| `marshall_wrap_tag_ok` | `p.__wrap<T>()` on a live tagged arena ptr returns the value | 0 |
| `marshall_wrap_tag_mismatch` | `p.__wrap<Other>()` returns `error` (dynamic tag check) | 0 (error branch) |
| `marshall_wrap_dead_arena` | `__wrap` of a dropped-region address returns `error` | 0 (error branch) |
| `marshall_wrap_null` | `__wrap` of address 0 returns `null` | 0 (null branch) |
| `marshall_unwrap_infallible` | `p.__unwrap<T>()` exposes the value, no check | 0 |
| `svc_singleton_once` | two `svc<S>()` of a singleton return the same instance | 0 |
| `svc_transient_always_new` | two `svc<S>()` of a transient return distinct instances | 0 |
| `svc_scoped_ancestry_reuse` | a singleton consuming a scoped dep gets the call-site scoped instance | 0 |
| `svc_conflict_ancestor` | owner's A/B/C/D/E: `svc<A>()` is a compile conflict | EXPECT_COMPILE_FAIL |
| `svc_string_key_disambiguates` | two keyed providers of one iface; `svc<I>(key "x")` resolves | 0 |
| `service_escape_field_rejected` | `a.b = svc<S>()` (field store) rejected | EXPECT_COMPILE_FAIL |
| `service_escape_param_rejected` | `fun(svc<S>())` (argument) rejected, even interface-typed param | EXPECT_COMPILE_FAIL |
| `service_escape_return_rejected` | returning a service value rejected | EXPECT_COMPILE_FAIL |
| `service_ctor_mandatory` | a `service` without `static ctor(): self` is rejected | EXPECT_COMPILE_FAIL |
| `bufptr_ffi_in_safe_fn` | a `buf_ptr`→C→read-back round-trip in a NON-`unsafe` fn compiles (§6.5.4) | 0 |
| `former_unsafe_fn_now_safe` | a former-`unsafe fn` naming opaque `ptr` compiles as plain `pub fn` | 0 |
| `unsafe_keyword_removed` | `unsafe fn`/`unsafe type` no longer parses (post-S5) | EXPECT_COMPILE_FAIL |
| `must_free_removed` | `#must_free` / `mem::free` no longer parse; region drops at scope | 0 |
| `opaque_ptr_no_arithmetic` | `p + 1` / `p[0]` on an opaque `ptr` rejected ("no arithmetic") | EXPECT_COMPILE_FAIL |
| `foreign_ptr_needs_wrap` | a C-returned `ptr` used without `__wrap<T>()` is rejected | EXPECT_COMPILE_FAIL |
| `usize_len_index` | `xs.len: usize`, `xs[i: usize]` type-check; `usize` lowers = `u64` on 64-bit | 0 |
| `usize_uptr_not_implicit` | a `usize` used where a `uptr` is expected (and vice-versa) is rejected | EXPECT_COMPILE_FAIL |
| `source_pos_stays_u32` | `line`/`col` remain `u32` (source positions do not reball) | 0 |
| `reball_bytes_identical` | a position rewritten `u64`→`usize` emits byte-identical native (64-bit) | 0 |
| `overload_resolves_by_params` | `f(i64)` and `f(str)` both defined; each call picks the right one | 0 |
| `overload_ambiguous_rejected` | two equally-good candidates for a call → compile error | EXPECT_COMPILE_FAIL |
| `overload_return_type_only_rejected` | two `f()` differing only in return type → compile error | EXPECT_COMPILE_FAIL |
| `single_def_symbol_unchanged` | a non-overloaded `f`'s emitted symbol is unchanged (byte-identity) | 0 |
| `op_overload_add_aggregate` | `a + b` on a user type calls `__add`; aggregate result via DPS | 0 |
| `op_overload_cmp_derived` | defining `__eq`+`__lt` makes `!=`/`>`/`<=`/`>=` work (derived) | 0 |
| `op_no_overload_shortcircuit` | `&&`/`||` cannot be overloaded (short-circuit preserved) | EXPECT_COMPILE_FAIL |
| `op_prim_path_unchanged` | `i64 + i64` never looks up a dunder (builtin path byte-identical) | 0 |

Each fixture is a standalone project under `examples/regressions/<name>/`; REJECT fixtures carry
`EXPECT_COMPILE_FAIL`; `svc`/service/marshall accept-fixtures are native exit-code oracles.

---

## 13. Ritual points (full gate must pass)

- After **A2, A3, A4, A5** (each changes the native emit) — full ladder + `gen2==gen3`.
- After **G2, G4** (byte-movers via CF3 / return-arm removal) — fixpoint.
- After **R1** — the drained lane's full native matrix (`gen2==gen3` with DPS).
- After **S1, S2, S3, S4** (source sweeps) — fixpoint byte-identity is the load-bearing gate.
- G1/G3/G5/G6 are additive/inert — full gate, but fixpoint is trivially held (bytes unchanged).

---

## 14. Risks + open questions (ranked)

**R1 (biggest) — the reseed is a one-shot, unbypassable hinge coupled to a DPS bet that is unproven
until P1.** Branch protection admits no bypass, so R1 lands a seed that must ALREADY DPS-lower and
parse-new; if the native `gen2==gen3` is not reached after R1 (P1 mis-pinned, or push_inst_block A5 not
truly closed), the seed is swapped and the native fixpoint is still red with no cheap un-reseed.
**Resolution:** P1 is the HARD go/no-go BEFORE committing A2; A5 (push_inst_block) MUST land and prove
green pre-R1; the reseed's own C-route criterion + the drained lane's native matrix are the two gates
that must both be green before the cherry-pick drains. Do not couple R1 to any surface crumb whose
byte-move is unproven. No HALT — this is engineering sequencing, resolved by ordering.

**R2 — `base` as a reserved keyword breaks production.** The owner said `base` = parent keyword;
`base` is a LIVE production local (`driver.tks:177`, `resolve.tks:947/1298/1708`, `zlib.tks:31`).
**Resolution (law-first, M.5 austerity):** keep `base` CONTEXTUAL (meaning only inside a subclass
method body); production `base` locals are untouched. `self` per the owner is reserved-in-method;
verify no production non-receiver `self` identifier exists before reserving (grep gate in G3). This
softens "reserved" to "reserved-in-method-body" — recommended, not a HALT.

**R3 — the service escape rule vs generic containers / higher-order use.** The strict rule forbids a
service as a param, so a service cannot be passed to ANY helper — every method call on it must be
inline at the claim site, and it cannot be closed over. **Resolution:** this is the owner's explicit
"claim-by-use is forced" intent; document it loudly (a service is a call-local capability, not a
value). Open question for the owner: does `self`-calling ANOTHER method on the same service
(intra-service `self.helper()`) count as a param pass? **Recommendation:** NO — `self` is the receiver,
not an argument, and the receiver channel is the sanctioned use; only source-level ARGUMENTS are the
escape. Confirm.

**R4 — the DPS two-birds claim (type_match/frame_sweep_inst) is unconfirmed** until P1 (carried from
both source docs). **Resolution:** P1 gates it; §11 gives the disconfirm fallback (tracks decouple, the
surface wave proceeds on the C-route reseed regardless).

**R5 — the binding-level `#singleton` residence attribute** (`parse_stmt.tks:202`,
`modelo-de-memoria` §2a) is a DIFFERENT feature from the DI `service singleton` lifetime — same word,
different axis (a local's ROOT residence vs a service's lifetime). **Resolution:** keep them distinct;
`service singleton` is a TYPE lifetime, `#singleton` on a binding is a residence hint. Do not conflate
in the parser (the `service` keyword path is separate from the `#`-attribute path). Flag for the owner
that the shared word is a legibility smell; recommend renaming the binding attribute later (out of this
wave, reported not actioned).

**R6 — Marshall tag header adds a per-object `u64`** to every routable allocation. **Resolution:** gate
it — only allocations whose type can be `__wrap`ped need the tag; a static "is this type ever wrapped?"
walk (like `scope_touches_arena`) keeps the header off the hot path for the common untagged case. Open:
measure the overhead before making it unconditional. Byte-identical when the tag path is unused.

**R7 — corpus-wide sweeps (S1–S4) risking a fixpoint regression.** Four mechanical rewrites, each must
hold `gen2==gen3`. **Resolution:** each is its own crumb with a fixpoint ritual; a byte move on a
"mechanical" sweep is a logic bug in disguise — STOP and re-examine (the `arena-por-escopo` R5
discipline).

**No genuine unresolved law tension forces a HALT.** Every fork (base contextual, self-intra-call,
service word overload) is resolved law-first above with a recommendation; the owner's only true
decision gate is P1's engineering go/no-go, which is not a law conflict.

---

## 15. Executive summary (for relay to the owner)

**Deliverable:** `docs/design/lang-evolution-0.3.1-memory-and-surface.md` (this file). It ties the
already-assessed caller-arena memory model (DPS keystone + arena elision + arena floor) to all six
decided surface changes, and gives one crumb-ordered migration + reseed plan.

**The wave = one additive grammar migration + one source sweep + one reseed.** The crumb order is:
**P1 pin → Phase A (DPS/elision/push_inst_block, the byte-movers) → Phase G (additive grammar +
Marshall + DI, written in OLD spelling so the current seed parses them) → R1 the single reseed
(cherry-pick drain, no PR) → Phase S (byte-preserving source sweep to `:`/`var`/`self` + drop-old).**

**Byte-preserving vs reseed-driving:** all six surface changes are byte-preserving — pure syntax
(`->`→`:`, `let`/`mut`→`var`, `self`/`base`/`static`) emit identical bytes, and Marshall-opaque-`ptr`
+ DI-`service` are feature-gated inert until a program uses them. **The ONLY byte-mover that needs the
reseed is DPS** (native return ABI; `gen1≠gen2`, `gen2==gen3` holds). The reseed is needed for two
reasons that fold into one harvest: (a) DPS is a self-lowering change that only reaches gen1 via a new
seed, and (b) the seed must accept the new grammar before the source sweep.

**DPS ↔ native-fixpoint sequencing:** FOLD DPS into the fixpoint grind — build DPS INSTEAD of
point-fixing `type_match` + `frame_sweep_inst` (it closes both by construction on the return/tail-merge
facet), keep `push_inst_block` a separate self-append point-fix, and gate the whole bet on P1 pinning
those two to the return facet FIRST. If P1 disconfirms, DPS still lands for memory + correctness and
the fixpoint track decouples — the surface wave proceeds on the C-route reseed regardless.

**DI is pure AOT — `svc<T>()` is a compile-time intrinsic, not a runtime function.** It has no ABI/
symbol; the compiler builds a static table mapping `(interface [+ transitive ancestors], optional key)
→ (concrete service, lifetime, ctor symbol, service_id)`, resolves each monomorphized `svc<T>(...)`
site against it AT COMPILE TIME (where the conflict error fires — `T` resolves iff exactly one row
matches; ≥2 ⇒ error; the transitive ancestor rows are what make the owner's `svc<A>()` example a
conflict), and REPLACES the site inline with per-lifetime code: `singleton` = root-slot once-guard
get-or-init; `transient` = ctor into the current arena; `scoped` = arena-ancestry walk + find-or-create
in the per-arena registry. The only runtime support is two arena structures (the per-arena scoped
registry and the root singleton slots, both REUSING `tk_region_register`/`tk_region_lookup`; the sole
candidate `teko_rt` addition is a `tk_region_parent` accessor for the walk). `ctor` is the only real
emitted service code; its invocation is wired by the substituted site (DPS discipline: the ctor takes a
hidden destination region). Zero reflection, zero generic dispatch.

**The escape rule + its exemption:** a service value can never be stored in a field / passed as a param
/ returned in USER code (a taint-origin flow rule keyed on `svc`/`ctor` production, so interface
generalization cannot bypass it) — this is what forces claim-by-use and auto-blocks `__wrap`/`__unwrap`
on a service (a service can't be a Marshall operand). The compiler-generated arena/DI BACKEND is EXEMPT
(it MUST hold service instances by pointer — the scoped registry and singleton slots ARE pointer
stores), and the exemption is free because the substituted resolution code and the runtime registry
primitives are synthesized BELOW the flow pass / live in `teko_rt`, so `check_no_service_escape` never
sees them (no marker needed). The exemption does NOT reintroduce UAF: every backend-held pointer is
arena-bounded (singleton=root=program lifetime, scoped=its registry's region, transient=current and
never stored). **Marshall #3 resolves as backend-only:** the ONLY place a service becomes an opaque
pointer is inside that trusted, arena-bounded backend — never a user-reachable surface.

**String-key DI (one line):** register `service singleton(key "fast") Cache { ... }` and resolve
`svc<Cache>(key "fast")`; keyed providers occupy distinct `(interface, key)` table slots, so multiple
providers of one interface coexist while the unkeyed single-slot conflict rule (and its transitive
ancestor conflict) is unchanged per key.

**`unsafe` / raw-pointer retirement — VERDICT: FULL removal, one named residual (FFI trust).** Assessed
the whole machinery (grepped: `is_unsafe` `ast.tks:434/585`, `#must_free` + its consume-or-fail dataflow
`typer.tks:3437-3600`, `mem::free` `:941-969`, the `teko::mem` raw builtins `scope.tks:1012-1046`, the
region primitives, `src/mem/unsafe/{rawbuf,arena}.tks`, and `extern fn`). Every class is removable: the
`is_unsafe` containment deletes with its subject; pointer arithmetic is ALREADY killed by the opaque-ptr
decision (an opaque `ptr` structurally cannot do `p+n`/`p[n]`); the `teko::mem` + region builtins
reclassify as SAFE intrinsics because `__wrap`'s dynamic null+arena-liveness+tag check supplies exactly
the liveness the checker cannot prove statically; and manual memory (`mem::free`/`#must_free`/`Arena`)
is obsoleted because the arena model STRUCTURALLY CANNOT leak a region (it drops at scope / DI-scoped /
phase boundary), so `#must_free`'s leak-guard has no subject. **The ONE irreducible residual is FFI
trust — Teko cannot verify foreign C — but it is NOT the `unsafe` keyword:** it already lives in the
`extern fn` declarator (distinct, greppable, non-`unsafe` today), and Marshall's fallible `__wrap<T>()`
is the sole UB-free re-entry of a foreign/opaque pointer into the checked world. So the language ends
with **no `unsafe` surface — memory-safe by arena, safe FFI by Marshall + extern-signature trust**,
composing directly with the de-C endgame. The removal rides the wave reseed + source sweep (crumbs
G7/G8 while `unsafe` still parses as a no-op, then S5 deletes the keyword after the sweep); it needs no
reseed of its own. A false "fully removable" would have been worse than an honest residual — the residual
is named (FFI trust) and already has its non-`unsafe` home.

**Machine-word integers + position reballing (items 7-8):** add `size` (signed) / `usize` (unsigned)
pointer-sized, target-dependent machine-word types (new `PrimKind` cases, `type.tks:11`; `prim_width`
returns the target word), then REBALL every memory/collection position — slice `.len`/`.cap`, indices,
byte/arena offsets, the slice header, the DPS/AL3 machinery — from hard-coded `u64` to `usize`/`size`.
**Source positions (`line`/`col` `u32`) do NOT reball** — they locate points in source text, not the
metal, so they stay target-independent (explicit decision, matches the owner's "memory/machine
positions primarily"). **`usize` (an integer measure) stays a DISTINCT KIND from `uptr` (an address):**
`__unwrap` yields `ptr`/`uptr`, lengths/indices are `usize`; `usize↔u64` is a plain `to`-cast, but
`uptr↔usize` is a Marshall boundary op, never implicit. **Byte-preserving on the fixpoint targets:** on
x86_64/arm64 `usize == u64` and `size == i64` bit-for-bit (both lower to the same `i64`), so the
`u64→usize` rewrite changes the checker type but emits IDENTICAL bytes → `gen2==gen3` holds; the
reballing therefore rides the source sweep (Phase S, crumb S6) with no separate reseed (32-bit, where
`usize==u32`, is not a fixpoint target). This ties the whole arena/DPS/slice machinery to ONE
machine-word position type, unblocking the paused #112 native slice-rep work against `usize` rather than
a hard-coded `u64`. Add-the-type is crumb G9 (additive, inert until used); the mass rewrite is S6.

**Overloading — method + operator (items 9-10, the ONLY non-arena items — ergonomics that COMPOSE with
the discipline, they do not derive from it).** METHOD overloading relaxes the same-name reject
(`revalidate.tks:6`) to allow same name iff PARAMETER signatures differ; resolution is by parameters
ONLY, never the return type (two overloads differing only in return type is a compile error); exact
match beats the existing implicit widenings, and a tie (≥2 equally-good) is an ambiguity compile error
(no silent pick). It EXTENDS the call path's candidate selection (the achatamento CK3 step, reusing the
`TtCands` candidate-cursor shape) and gets distinct symbols for free from the AL4a signature mangling —
with single-def names' symbols UNCHANGED (byte-identity). OPERATOR overloading desugars `a OP b` to a
dunder method (`__add`/`__sub`/…/`__eq`/`__lt`/`__neg`/`__index`), consistent with `__wrap`/`__unwrap`,
an instance method `fn __add(self, rhs): R`; `__eq`+`__lt` are the only comparison methods a type
defines and `!=`/`>`/`<=`/`>=` are DERIVED automatically. Overloadable: arithmetic, comparison, unary
`-`/`~`, `[]`, opt-in bitwise; NOT `=`/`.`/`::` or short-circuit `&&`/`||` (overloading them would kill
short-circuit — a correctness trap). Both are feature-gated-inert (byte-identical until `src/` adopts a
dunder or an overload set; the prim/prim operator path is guarded and untouched), so they ride the wave:
G10/G11 add the capability, S7 (optional) sweeps only if `src/` chooses to use it. NO C#-style
conversion operators — conversion stays `to`, a separate axis. An overloaded op returning an aggregate
uses DPS (result born in the caller's arena) — the one composition point with §0. The DI conflict rule
is orthogonal (interface providers vs fn signatures — no interaction).

**Single biggest risk:** the reseed is an unbypassable one-shot hinge coupled to a DPS bet that is
unproven until the P1 pin — if the native `gen2==gen3` is not truly reached before R1 (P1 mis-pins, or
`push_inst_block` is not independently closed), you swap the seed and the native fixpoint stays red with
no cheap recovery. Mitigation: P1 is the hard go/no-go before A2, `push_inst_block` (A5) must be green
pre-reseed, and both the reseed's C-route criterion and the drained lane's native matrix must be green
before the cherry-pick drains.
