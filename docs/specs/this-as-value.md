# `this` as a value — `return this;`

**All four crumbs are BUILT**: 1 and 3 by D102 (`this` is a value of the enclosing class
or interface, and `this = e;` is refused), 2 and 4 by D103 (the fluent fixture with its
exact `rt_live()` assertions, and the `struct`'s own message). What follows is the design
as it was measured before the build; § 4.3's table is the architect's measurement, and
§ 7 marks what landed.

Everything below was measured on `mc` 1.0.1, macos/aarch64, against `origin/main`
`85abd466`, in a detached worktree. Where a claim of [D87](../../DECISION_LOG.md) is
restated here it was re-measured; § 1 says where D87 was right and where it was
incomplete.

---

## 1. Ground truth, measured

### 1.1 What `return this;` does today

```sh
sed -e 's/^os   = .*/os   = "macos"/' -e 's/^arch = .*/arch = "aarch64"/' teko.toml >mc.macos.toml
mc build . --config mc.macos.toml
./build/teko --include=lib --include=p p/a.tk -o p/a.o
```

```teko
// no-run
#include "rt.tk"
class C {
    public i64 v;
    public C() { v = 0; }
    public C Set(i64 x) { v = x; return this; }
}
i64 main() { C c = new C(); c.Set(1); return 42; }
```

> `p/a.tk:5: teko: a value of type uptr does not convert to C`

**Refused**, not accepted-and-wrong. Every surface position refuses with the same sentence,
at the line of the construct that consumes the value:

| position | probe | verdict today |
|---|---|---|
| method body, `return this;` | `public C Set(i64 x) { v = x; return this; }` | `teko: a value of type uptr does not convert to C` |
| constructor, `c2 = this;` | `public C() { v = 0; c2 = this; }` | same |
| property accessor `get` | `public C Self { get { return this; } }` | same |
| `set` half of the same property | `set { v = 1; }` with the `get` above | same (the `get` refuses first) |
| operator body / a class that *declares* an `operator+` | `public static C operator+(C a, C b) { ... } public C M() { return this; }` | same |
| argument, `f(this)` | `public i64 M() { return g(this); }` | same |
| initializer, `C d = this;` | `public i64 M() { C d = this; return d.v; }` | same |
| return typed as an INTERFACE the class implements | `public I Me() { return this; }` on `class C : I` | `teko: a value of type uptr does not convert to I` |
| interface DEFAULT body, `I Me() { return this; }` | `interface I { i64 V(); I Me() { return this; } }` | `teko: a value of type uptr does not convert to I` |
| ternary arm, `b ? this : o` | `public C M(C o, bool b) { return b ? this : o; }` | `teko: the two arms of ?: have different types` |
| lambda body, `() => this` | `F f = () => this;` | `teko: this is not captured; add it to use (...)` |
| `use (this)` on that lambda | `() use (this) => this` | `teko: expected a captured name` |
| `struct` method, `return this;` | `struct P { public i64 x; public P Self() { return this; } }` | `teko: a value of type uptr does not convert to P` |
| static member | `public static i64 S() { C d = this; ... }` | ``teko: `this` is not there in a static member`` |
| file scope | `i64 main() { uptr q = this; ... }` | ``teko: `this` is only valid inside the body of a type`` |

**Three positions are accepted today, and two of the three are wrong.** These are *not* in
D87, and they are the part of the ground truth the log had missed:

| accepted today | what it compiles to | verdict |
|---|---|---|
| `public uptr M() { return this; }` — a method DECLARED `uptr` | returns the receiver address as a raw `uptr` | accepted; an escape hatch nobody should write, and it stays legal |
| `this == o` in a method of `C`, `o` of type `C` | a raw pointer compare | **wrong**: `C a; C b; a == b;` is refused ``teko: C declares no operator `==` `` everywhere else. `this` typing `uptr` is the only reason it slips past `tk_ops_binary` |
| `this = o;` as a statement | `ASSIGN name=this` over the receiver parameter, with **no** reference counting at all (`tk_rc_assign` sees `TY_UPTR`, which is not counted, and leaves) | **wrong**: C# refuses `this = e;` on a class outright |

Proof of the last one, on the base binary:

```sh
./build/teko --dump-ast --include=lib --include=p p/s2.tk | sed -n '1139,1144p'
```

```
FUNC name=c_M
  PARAM type=uptr name=this
  PARAM type=C name=o
  BLOCK
    ASSIGN name=this
      IDENT type=i64 name=o
```

### 1.2 Where the type is decided, and where each refusal is raised

| what | `file:line` at `85abd466` |
|---|---|
| the receiver parameter is `uptr` | `teko_class.tk:590` — `if (recv) head = param_new(TY_UPTR, tk_this_name());` (and `:993`, `:1302`, `teko_prop.tk:161`, `teko_deleg.tk:256`, `:1876` for the compiler's own generated bodies) |
| the parse-time word `this` | `teko.tk:435` `syntax_expr("this", &tk_this);` → `tk_this()` at `teko_this.tk:112`, whose whole result is `tk_this_recv()` at `teko_this.tk:120` — a plain `N_IDENT` named `this` |
| the receiver's CLASS, already recorded at parse time | `teko_class.tk:820` `tk_local_add(tk_this_name(), ci);` and `teko_prop.tk:259` — read by `tk_local_find` (`teko_struct.tk:1040`) through `tk_struct_of_expr` (`teko_struct.tk:1207`). **This table is a parse-time stack, cut at every `}`, and is empty by pass time.** |
| the PASS-time oracle | `tk_ty_of` at `teko_typeof.tk:352`. It reads `tk_xt_ty(n)` (`teko_struct.tk:1070`) at `teko_typeof.tk:360` **before** falling through to the scope lookup for an `N_IDENT` at `:362` |
| the pass-time scope that answers `uptr` | `tk_ty_scope_params` at `teko_typeof.tk:264`, which walks the declared parameter list |
| the conversion refusal | built at `teko_struct.tk:1279` (`tk_reject_compat`), decided by `tk_check_compat` at `teko_typeof.tk:700` |
| reached, for a `return`, from | `teko_rc.tk:252` inside `tk_rc_return` (`teko_rc.tk:248`) |
| …for an initializer | `teko_rc.tk:168` (`tk_rc_var`) |
| …for an assignment | `teko_rc.tk:218` |
| …for an argument | `teko_rc.tk:512`, `:518`, `teko_expr.tk:384`, `teko_iface.tk:296`, `teko_deleg.tk:488`, `teko_params.tk:570` |
| the ternary's own refusal | `teko_ternary.tk`, reached from the arm-compare; its scope walk is `teko_ternary.tk:705` |
| the two `this` refusals | `teko_this.tk:116` (``teko: `this` is only valid inside the body of a type ``) and `teko_this.tk:117` (``teko: `this` is not there in a static member ``) |
| the compiler's own field addressing | `tk_this_addr` at `teko_this.tk:207` — `tk_bin(K_ADD, tk_this_recv(), tk_int(fd_off_at(fi)))`; the same shape at `teko_prop.tk:232`, `teko_class.tk:1271`, `teko_deleg.tk:1870` |
| the operator judge that D87's draft collided with | `tk_ops_binary` at `teko_ops.tk:834`, reading `tk_ty_of(a)`/`tk_ty_of(b)` at `:840`–`:841`; the refusal at `teko_ops.tk:607` |
| pass order | `teko.tk:496` `pass(&tk_typeof_pass)` → `teko.tk:535` `pass(&tk_ops_pass)` → `teko.tk:568` `pass(&tk_rc_pass)`. The addressing is BUILT by the sixth pass and the operator judge runs after it, which is exactly why a globally retyped `this` reaches it |

### 1.3 D87, verified against the tree

| D87's claim | verdict |
|---|---|
| `this` types `uptr`, from `param_new(TY_UPTR, ...)` in `teko_class.tk` | **true**, `teko_class.tk:590` |
| retyping in `tk_ty_scope_params` makes the compiler's `this + OFF` reach user `operator+` | **true and load-bearing**: `tk_ops_binary` (`teko_ops.tk:840`) asks `tk_ty_of` on the operand, and `tk_this_addr` (`teko_this.tk:207`) builds the `K_ADD` from a bare `tk_this_recv()`. Any change that answers "the class" for *that* node repeats the failure |
| five of `tk_ty_scope_params`'s seven callers are unbracketed by `tk_this_enter_fn` | **true today**: the callers are `teko_ternary.tk:705`, `teko_params.tk:723`, `teko_deleg.tk:2065`, `:2283`, `teko_ns.tk:1056`, `teko_rc.tk:689`, `teko_ref.tk:688`, `teko_typeof.tk:953`; only `teko_typeof.tk:951` pairs an enter. **But the line numbers in D87 have all drifted** (`:685`→`:723`, `:2043`→`:2065`, `:2261`→`:2283`, `:1050`→`:1056`) |
| "the redesign owed: the class for SURFACE consumers only, established in every scope walk" | **the premise is right, the mechanism named is wrong and more expensive than needed.** Nothing has to be established in any scope walk. The distinction "surface `this`" vs "the compiler's own `this`" is already available for free as a NODE IDENTITY — see § 3 |

---

## 2. What C# promises, and what teko reaches

| C# | teko, after this design | exit where teko cannot reach |
|---|---|---|
| fluent chaining, `obj.Set(1).Set(2)` | **reached**, measured running | — |
| `return this;` from a method typed as the class | **reached** | — |
| `return this;` typed as a BASE class (`class C : B`, `public B Up() { return this; }`) | **reached** — `tk_row_fits` (`teko_struct.tk:1244`) already answers derives-from | — |
| `return this;` typed as an interface the class implements | **reached** — the same `tk_row_fits`, through `tk_impl_has` | — |
| `return this;` from an INTERFACE DEFAULT body, static type = the interface | **reached**: `tk_this_enter_fn` (`teko_this.tk:181`) already sets `tk_pass_class` from `tk_iface_of_def(fn)`, and the parse-time `tk_body_class` is the interface row | — |
| covariant returns (`override D Me()` over `virtual B Me()`) | **reached**, measured running (exit 42) — teko's override matching is by name and arity, so a narrower return type is accepted. That is *more* permissive than C# 8 and matches C# 9 | — |
| `return this;` in a `struct` | **refused.** Teko's `struct` is not a value type on assignment yet: `P b = a; b.x = 9;` already changes `a.x` on `85abd466` (measured, exit 1). `return this;` would alias for the same pre-existing reason. A copy-on-return here would be the only place in the language that copies, which is a lie | ``teko: `this` is not a value in a struct`` + a [not-yet.md](../reference/not-yet.md) row |
| `this` implicitly captured by a lambda | **refused, D11 stands.** A closure holding a counted `this` opens a reference cycle `teko_rc.tk` does not break | `teko: this is not captured; add it to use (...)` stays — see the risk in § 6 |
| `this = e;` on a class | **refused** (it silently compiles today) | ``teko: `this` is read-only`` |
| `obj.Set(1).Set(2)` on a GENERIC class | **blocked on an unrelated defect**: `class Box<T> { public Box<T> Id(Box<T> o) { return o; } }` already fails `Box__i64 instantiated from …: expected < after a generic type name` with no `this` anywhere. Not this design's to fix | the pre-existing generic message |

---

## 3. The reference count, stated plainly

**Returning `this` needs no new reference-counting machinery at all.** It lands exactly on
the road a method returning a PARAMETER of class type already takes, and that road was
measured working.

- **`this` is a parameter.** `tk_rc_fn` (`teko_rc.tk:683`) pushes the signature and sets
  `tk_rc_floor` above it, so every parameter sits **below the floor**: borrowed, never
  released by the callee (`tk_rc_releases`, `teko_rc.tk:142`, starts at the floor).
- **`tk_rc_own(this)` is 0.** `tk_rc_own` (`teko_rc.tk:116`) answers `TK_BORROWED` for a
  registration that says so, and 0 for any `N_IDENT`. Either way `this` is borrowed.
- **The receiver's count therefore rises, automatically.** `tk_rc_return`
  (`teko_rc.tk:248`) computes `needinc = !tk_rc_own(e)` at `teko_rc.tk:261` when the
  declared return is counted, and emits `rc_inc($t)` before the frame's releases. The
  caller receives a reference of its own.
- **The caller releases it.** A kept result is an owning local, released by
  `tk_rc_releases` at the end of its block. A **discarded** result —
  `obj.Set(1);` as a statement — is dropped by `tk_rc_exprstmt` (`teko_rc.tk:316`), which
  wraps an owned expression in `rt_drop`.

### The analogous existing case, measured

```teko
// no-run
#include "rt.tk"
class C {
    public i64 v;
    public C Fld;
    public C() { v = 0; }
    public C Same(C o) { return o; }    // returns a PARAMETER of class type
    public C GetFld()  { return Fld; }  // returns a FIELD
}
i64 main() {
    C a = new C();
    a.Fld = new C();
    i64 l1 = rt_live();
    a.Same(a);                          // DISCARDED
    a.GetFld();                         // DISCARDED
    if (rt_live() != l1) return 10;
    C b = a.Same(a);                    // kept
    C f = a.GetFld();
    if (rt_live() != l1) return 11;
    return 42;
}
```

Exit **42** on `85abd466`. No leak, no double release, on both roads, kept and discarded.
The same program written with `return this;` — the fixture of crumb 1 — exits 42 with the
design in place (measured; see § 4.3).

**The one leak that remains is the user's own cycle**, not this design's: `public C() { c2
= this; }` makes an object point at itself, and a plain reference count never collects a
cycle. [memory.md](../reference/memory.md)'s "What is not reclaimed" table carries the
rule and names this constructor form; it is a declared debt, counted by `rt_live()`, not a
defect of this design.

---

## 4. The design

### 4.1 Three candidates

| # | design | cost | what it breaks |
|---|---|---|---|
| A | retype the receiver parameter at `teko_class.tk:590` (`param_new(TY_UPTR)` → the class's own id) | one line | **everything.** The generated release/vtable bodies (`teko_class.tk:993`, `:1302`, `teko_prop.tk:161`, `teko_deleg.tk:256`, `:1876`) all pass a raw `uptr`; every `--dump-ast` moves; `rt_free(this, n)` and `ld64(this)` change signature shape. Rejected |
| B | retype `this` in the pass scope, `tk_ty_scope_params` (`teko_typeof.tk:264`) | one line, plus bracketing seven scope-walk callers with `tk_this_enter_fn` | **D87's measured failure.** `tk_this_addr`'s `this + OFF` becomes a class-typed `+`, reaches `tk_ops_binary` (`teko_ops.tk:840`), and a class that declares an `operator+` — `lib/string.tk` is one — either refuses a line that writes no `+` or compiles a wrong `ld8(0)`. Rejected, again |
| C | **register the node the PARSER built.** `tk_this()` (`teko_this.tk:112`) is the only place a `this` a *user wrote* becomes a node. Register that node in the existing expression-type table with the enclosing type's row | **one line** in `tk_this()`, no new table, no new state, no new pass | nothing measured — see § 4.3 |

**C is the pick.** The distinction D87 asked for — "the class for surface consumers, `uptr`
for the compiler's own addressing" — is not a property of the scope, it is a property of
the **node**. `tk_this_addr`, `teko_prop.tk:232`, `teko_class.tk:1271` and
`teko_deleg.tk:1870` each build their *own* fresh `tk_this_recv()`/`tk_id("this")`; the one
node that came from the source is `tk_this()`'s. Marking that node needs no scope walk, no
bracketing, and no second oracle.

`tk_ty_of` already consults the table first (`teko_typeof.tk:360`, ahead of the `N_IDENT`
scope lookup at `:362`), so the answer reaches every surface consumer — `return`, an
initializer, an argument, `?:`, `??`, a `==` — through the one function they all already
ask. `tk_struct_of_expr` (`teko_struct.tk:1207`) reads the same table first, so the
parse-time door agrees with the pass-time one for free.

### 4.2 The one line

```
// teko_this.tk, in tk_this(), replacing `return tk_this_recv();`
i64 r = tk_this_recv();
if (p_id() != K_DOT && (tk_is_class(tk_body_class) || tk_is_iface(tk_body_class)))
    tk_xt_add(r, tk_body_class, 1, TK_BORROWED);
return r;
```

Four things, each earning itself:

- `tk_xt_add` (`teko_struct.tk:1111`) fills `xt_ty` from `sr_ty_at(row)` — the class's or
  the interface's own registered type id.
- `pure = 1`: re-evaluating `this` is free. (`tk_pure`, `teko_struct.tk:1080`, already
  answers 1 for any `N_IDENT`, so this only keeps the row honest.)
- `TK_BORROWED`: the receiver is a borrowed parameter. This is what makes `tk_rc_own`
  answer 0 and `tk_rc_return` emit the `rc_inc` of § 3.
- `p_id() != K_DOT`: a `this` that is about to be a `.` receiver is left exactly as it is,
  so the whole of the existing member road is untouched and the table cost is one row per
  *bare* `this`, against `TK_MAXXT` = 4096 (`teko_struct.tk:55`). **Measured: dropping this
  guard also leaves all 140 fixtures byte-identical under `--dump-ast` and closes the fixed
  point, so it is prudence rather than necessity** — keep it.
- the kind test excludes `struct`, whose `this` keeps the existing refusal until crumb 4
  gives it a better message.

### 4.3 Measured, with the one line in place

| measurement | command | result |
|---|---|---|
| build | `mc build . --config mc.macos.toml` | clean |
| fixtures | `sh scripts/fixtures.sh ./build/teko mc.macos.toml` | **140 passed, 183 refused as expected, 0 failed** — identical to the base, which measures 140/183/0 |
| fixed point | `sh scripts/bootstrap.sh --os macos --arch aarch64` | **FIXPOINT OK** |
| docs | `sh scripts/check-docs.sh` | `docs ok` |
| `--dump-ast`, all 140 fixtures, base vs head, both binaries run IN PLACE next to their own `build/lib/mc/v1.0.1/` | `./build/teko --dump-ast --include=lib --include=tests tests/*.tk`, `cmp` per file | **140 identical, 0 different.** The change is a strict no-op on every program accepted today |
| `mc limits . --config mc.macos.toml`, both legs | base vs head | **`passes` 0/8, `syntax` 0/16, `alias` 1/16, `types` 1/8, `on_stmt` 0/8, `intrin` 0/8 unmoved on the compiler leg; `passes` 15/30, `syntax` 20/40, `alias` 25/50, `types` 18/36, `on_stmt` 4/8, `intrin` 8/16 unmoved on the `tests/hello.tk` leg.** Zero new intrinsics, zero new passes. Only size rows move: `nodes` 212905 → 212912, `ins` 246193 → 246214 |
| fluent chaining, runtime | the § 5 fixture | exit **42** |
| the eleven refused positions of § 1.1 | re-run | **all eleven compile**, including the interface default body and the class that declares its own `operator+` — D87's exact failure, structurally absent |

---

## 5. The surface

### Legal

```teko
// no-run
#include "rt.tk"
interface I { i64 V(); I Me() { return this; } }        // a default body: static type is I
class B { public i64 v; public B() { v = 1; } public virtual B Me() { return this; } }
class C : B, I {
    public C() { v = 0; }
    public i64 V() { return v; }
    public C Set(i64 x) { v = x; return this; }         // fluent
    public B  Up()      { return this; }                // typed as the BASE
    public I  AsI()     { return this; }                // typed as the INTERFACE
    public C  Self { get { return this; } }             // a property accessor
    public override C Me() { return this; }             // covariant override
    public static C operator+(C a, C b) { return a; }   // declared, and never reached by `this + OFF`
}
i64 main() {
    C c = new C();
    c.Set(1).Set(2).Set(3);                             // chained, result DISCARDED
    C d = c.Set(7);                                     // kept
    I j = c;
    I i = j.Me();
    return 42;
}
```

### Illegal, and the refusal each earns

```teko
// no-run
#include "rt.tk"
struct P { public i64 x; public P Self() { return this; } }
//                                        teko: `this` is not a value in a struct
//   BUILT (D103), raised at the `this` itself in `tk_this()` -- the `else` of
//   the kind test crumb 1 added. It used to answer the generic
//   `teko: a value of type uptr does not convert to P`, at the construct that
//   CONSUMED the value and naming the receiver parameter's declared type. The
//   field roads are untouched: `this.x`, a bare `x` and `this.x = v` inside a
//   struct method all still work (measured before and after).
class C {
    public i64 v;
    public C() { v = 0; }
    public void Reset(C o) { this = o; }
//                           teko: `this` is read-only
    public static C Make() { return this; }
//                                 teko: `this` is not there in a static member  (unchanged)
}
class U { public U() {} }
class D { public D() {} public U Bad() { return this; } }
//                                              teko: a value of type D does not convert to U
i64 stray() { return this; }
//                   teko: `this` is only valid inside the body of a type        (unchanged)
```

Note the fourth message: the same sentence as today, with `D` where it used to say `uptr`.
The improvement is free — `tk_reject_compat` (`teko_struct.tk:1279`) names whatever
`tk_ty_of` gave it.

### One acceptance becomes a refusal

`this == o` on two `C` references compiles today into a raw pointer compare, and after this
design it is refused ``teko: C declares no operator `==` ``. **That is an alignment, not a
regression**: `C a; C b; if (a == b)` is refused by that exact sentence on `85abd466` —
`this` typing `uptr` was the one hole in the rule. Nothing in the tree depends on it (140
passed / 183 refused / 0 failed, unchanged). **`this == o` gets its own refuse fixture in
crumb 1** (`tests/refuse/this_eq_no_op.tk`) and a diagnostics note. `this == null` is a
different form and is **unaffected**: it takes the nullable road, measured compiling before
and after, and has no fixture of its own here.

---

## 6. Hooks, by module

| module | pass | what it grows |
|---|---|---|
| `teko_this.tk` | parse (`tk_this`, `teko_this.tk:112`, registered at `teko.tk:435`) | the four-line registration of § 4.2 and the header paragraph that says why the node and not the scope. Crumb 3 adds the `this = e;` refusal; crumb 4 adds the `struct` one. **`tk_addr_of_member` (`teko_this.tk:516`, D97) is untouched** — `tk_ty_of` tests `tk_rfarg_kind` at `teko_typeof.tk:354`, *before* the table, so `&this` / `ref this` never read the new row |
| `teko_struct.tk` | — | nothing. `tk_xt_add`/`tk_xt_put` (`:1111`/`:1101`), `tk_xt_ty` (`:1070`), `tk_row_fits` (`:1244`) and `tk_reject_compat` (`:1279`) are used as they are |
| `teko_typeof.tk` | 6 (`teko.tk:496`) | nothing. `tk_ty_of` (`:352`) already reads the table ahead of the scope; `tk_check_compat` (`:700`) already asks `tk_row_fits`. `tk_ty_scope_params` (`:264`) is **not** touched — that is the whole point |
| `teko_rc.tk` | 14 (`teko.tk:568`) | nothing. `tk_rc_return`'s `needinc` (`:261`) and `tk_rc_exprstmt` (`:316`) already do the work of § 3 |
| `teko_ops.tk` | 11 (`teko.tk:535`) | nothing, and that is the load-bearing claim: the compiler's own `this + OFF` nodes are unregistered and keep answering `uptr` at `teko_ops.tk:840` |
| `teko_class.tk`, `teko_prop.tk`, `teko_iface.tk`, `teko_deleg.tk` | — | nothing. `teko_class.tk:590` stays `TY_UPTR`; `teko_class.tk:820` / `teko_prop.tk:259` already record the row this design reads |
| `lib/rt.tk` | — | **nothing.** `rt_own`, `rc_inc`, `rt_drop`, `rt_store` already carry every case |
| `core_teko.mc` / `user.mc` | — | **nothing registered.** No new `pass()`, no new `syntax_expr`, no new `on_stmt`, no new intrinsic — confirmed by the six unmoved `mc limits` rows in § 4.3 |

**No collision with what landed today.** D96's index-base marking (`teko_array.tk`,
`teko_typeof.tk`) and D100/D101's jagged work (`teko_heaparr.tk`) both reach `this` only as
`this.rows` / `this.items[i]` — a `.` receiver, which the `K_DOT` guard leaves unregistered.
D97's address-of judge is short-circuited ahead of the table, as above. All three were
re-measured green in the 140/183/0 and the fixed point of § 4.3.

---

## 7. The crumbs

| # | crumb | size | depends on | gate | docs owed |
|---|---|---|---|---|---|
| **1** | **BUILT (D102). `this` is a value: the node registration.** The four lines of § 4.2 (with the `K_DOT` and kind guards), the header paragraph, and the fixtures. Every position in § 1.1's first table starts compiling | **S** — one module, one function, three fixtures | — | `surface_this_value.tk` (42), `refuse/this_bad_type.tk`, `refuse/this_eq_no_op.tk`; **`--dump-ast` identical over all 140 base fixtures**; fixed point closes; `mc limits` six rows unmoved on both legs | `docs/reference/types.md` § `class` gains the paragraph; `docs/reference/not-yet.md` **loses** the `return this;` row and gains the `struct` one; `docs/reference/diagnostics.md` gains nothing new (every message already exists) and gains the note on `this == o` |
| **2** | **BUILT (D103). Fluent chaining, proved end to end, and the reclaim.** No compiler change: the fixture that runs a three-link chain with a discarded result, a kept one, an interface default body, a base-typed return and a covariant override, all under `rt_live()` | **S** — one fixture, no module | crumb 1 | `surface_this_fluent.tk` (42) — the § 5 legal program with `rt_live()` assertions at every step | `docs/guide/` gains the chaining recipe; `docs/reference/memory.md` gains the sentence on the self-referencing constructor (a cycle a count never collects) |
| **3** | **BUILT (D102). `this = e;` is refused.** A pre-existing silent wrong answer (§ 1.1). One guard, in `tk_this()`'s own file, at the point the parser sees `this` followed by `=` | **S** — one module, one refuse fixture | none — **runs in parallel with any unrelated crumb**, and with crumbs 1/2 if the implementer keeps the two edits apart in `tk_this()` | `refuse/this_assign.tk`, message ``teko: `this` is read-only``; `--dump-ast` identical over all 140 (nothing accepted today changes except the refused form) | `docs/reference/diagnostics.md` gains the message; `docs/reference/not-yet.md` needs no row |
| **4** | **BUILT (D103). `this` in a `struct` gets its own message.** Replace the inherited `teko: a value of type uptr does not convert to P` with ``teko: `this` is not a value in a struct``, raised where the kind guard of crumb 1 declines | **S** — one module, one refuse fixture | crumb 1 (the kind guard is the hook) | `refuse/this_struct_value.tk`; `--dump-ast` identical | `docs/reference/not-yet.md` gains the `struct` row; `docs/reference/diagnostics.md` gains the message |

**Order and parallelism.** 1 → 2 and 1 → 4 are hard dependencies. **Crumb 3 is independent
of all three** and of everything else in flight; it is the one that can be handed to an
implementer while another works elsewhere. Crumbs 1, 2 and 4 are one road and must run in
sequence, because 2's fixture and 4's message are both written against 1's registration.

Every gate above is the local recipe, unchanged:

```sh
mc build . --config mc.macos.toml
sh scripts/fixtures.sh ./build/teko mc.macos.toml
sh scripts/bootstrap.sh --os macos --arch aarch64
sh scripts/check-docs.sh
```

## 8. The fixtures

**Built by D102** (crumbs 1 and 3):

| fixture | asserts |
|---|---|
| `tests/surface_this_value.tk` | `// expect-exit: 42` — `return this;` from a method, a constructor field store, a property `get` and `set`, an argument `f(this)`, an initializer `C d = this;`, a ternary arm, a base-typed return, an interface-typed return, an interface default body, a parenthesized receiver `(this).field`, and a class that also declares `operator+` and reads a `uptr` field — D87's exact pairing. A three-link chain discarded and a chain kept are bracketed by `rt_live()` on a NON-cyclic class, so the floor is exact on both sides |
| `tests/refuse/this_bad_type.tk` | `// expect-refuse: teko: a value of type D does not convert to U` — the class name, not `uptr` |
| `tests/refuse/this_eq_no_op.tk` | ``// expect-refuse: teko: C declares no operator `==` `` — the alignment of § 5 |
| `tests/refuse/this_assign.tk` | `` // expect-refuse: teko: `this` is read-only `` |
| `tests/refuse/this_compound_assign.tk` | the same sentence on `this++`. Not planned by this page: `this += e` and `this++` were measured compiling as silently as `this = e`, so D102's guard covers the whole write set |

**Built by D103** (crumbs 2 and 4):

| fixture | asserts | crumb |
|---|---|---|
| `tests/surface_this_fluent.tk` | `// expect-exit: 42` — a three-frame chain DISCARDED, one KEPT, one PASSED ON as an argument to a callee that allocates while holding it, one built a frame DOWN over a receiver that is not the callee's own `this`, one RETURNED from an interface default body, and a covariant `override Cell Me()` reached through a BASE-typed slot. Every `rt_live()` read is EXACT, over cycle-free classes, and both assertions were proved to bite by mutating the compiler: killing `tk_rc_exprstmt`'s sweep gives exit 29, killing `tk_rc_return`'s `needinc` gives exit 70 (`teko: reference count below zero`) | 2 |
| `tests/refuse/this_struct_value.tk` | `` // expect-refuse: teko: `this` is not a value in a struct `` | 4 |

One boundary the fixture cannot cross, and it is not this design's: a link whose method is
`virtual` needs a name or a field on the left (`teko: a virtual call needs a name or a
field on the left`, `teko_expr.tk`), so `b.Me().Me()` is refused with or without `this`.
The covariant override is reached through a name and the chain continues over non-virtual
links.

## 9. Risks and law tensions

| # | risk | recommended resolution |
|---|---|---|
| R1 | **`this == o` flips from accepted to refused.** A behaviour change on a form that compiles today | **Take it.** It aligns `this` with every other class reference (`a == b` is already refused by the same sentence), and nothing in tree depends on it. Land it inside crumb 1 with its own refuse fixture and a diagnostics note, never silently |
| R2 | **`TK_MAXXT` is 4096** (`teko_struct.tk:55`) and every bare `this` now takes a row. A program with thousands of them refuses `teko: too many expressions whose type is known` | The `K_DOT` guard keeps the cost to bare-`this`-as-value, which no program written today contains. Measured with the guard *off* the fixed point still closes. If a real program ever hits it, the ceiling is raised the way `TK_MAXFS`'s was, not the design changed |
| R3 | **`tk_xt_at` is a linear scan from the top** (`teko_struct.tk:1049`). More rows is more scan | Measured: `ins` 246193 → 246214, and the fixed point runs in the same band. No action |
| R4 | **The lambda tells a lie.** `() => this` says `teko: this is not captured; add it to use (...)`, and `use (this)` then says `teko: expected a captured name`. The message names a form that does not exist | **Out of this design's scope, and it is D11's fork, not this one's.** [not-yet.md](../reference/not-yet.md) already carries the row and the reason (a closure holding a counted `this` opens a cycle the reclaim does not break). The honest fix is a message that does not name `use (...)` for `this` — propose it as a follow-up crumb, not a condition of 1.0 |
| R5 | **The self-referencing constructor leaks** (`public C() { c2 = this; }`) | A cycle, not a defect. [memory.md](../reference/memory.md)'s "What is not reclaimed" table states the rule and names this constructor form |
| R6 | **Generic fluent chaining is blocked.** `class Box<T> { public Box<T> Set(T x) { … return this; } }` refuses `expected < after a generic type name` at instantiation, **with or without `this`** (measured on a `return o;` control) | Not this design's. Report it as its own defect with the pure control program; the `not-yet.md` row belongs to generics |
| R7 | **`struct` is not a value type on assignment** (`P b = a; b.x = 9;` changes `a.x` today, measured) | Refuse `this` as a value in a `struct` (crumb 4) rather than inventing a copy-on-return that exists nowhere else in the language. The struct-copy design is its own page |
| R8 | **Law tension: "C# decides the form."** C# allows `this` in a struct (as a copy) and captures `this` implicitly in a lambda. This design refuses both | Both exits are refusals with a stated reason, never a wrong answer, which is what the law asks for. Both carry a `not-yet.md` row naming the design owed |

## 10. What I would refuse to build for 1.0

- **`this` in a `struct`.** Making it right means making `struct` a value type — a copy at
  every assignment, every argument, every return. That is a language-wide design with its
  own page, and half of it shipped inside `return this;` would be worse than the refusal.
  → ``teko: `this` is not a value in a struct``, plus a `not-yet.md` row: *`return this;`,
  `f(this)` and `S x = this;` inside a `struct` body — `this` is a value only in a class or
  an interface; a `struct` has no copy at assignment yet, so returning `this` would alias
  rather than copy.*
- **Implicit `this` capture in a lambda.** D11 stands and the reference cycle is real. The
  `not-yet.md` row exists; only its *message* is worth a follow-up (R4).
- **A lifted or reference `==` for `this`.** `this == o` is a class reference like any
  other; it gets the rule every class reference gets, and nothing special.
- **Any re-typing of the receiver parameter.** Candidates A and B in § 4.1 both cost more
  and buy less than the one line. If a future construct seems to need one, that is the
  moment to re-open this page — not before.
