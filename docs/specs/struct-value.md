# `struct` as a value type

**Mostly built.** Nothing on this page ran at `e3e6aa13`, where it was written. **V1 and V2
landed with D105**; **V3, V4 and V5 landed with D106**, so all six landings of § 3.1 copy: a
struct cannot contain itself, and `S b = <e>;`, `b = <e>;`, `x.f = <e>;`, `a[i] = <e>;`, a
by-value argument at every call road and `S? b = <e>;` each give the destination a fresh
block. **V6 landed with D108**: the `foreach` element is still bound to the array's own row
— a copy there would send the write nowhere — so a write through it is refused the way C#
refuses it, and a struct captured by value names the struct and the `&` road instead of a
generic conversion. **V7 is still designed, not built** — nothing reclaims a copy. Every
"today" below is the measurement at
`e3e6aa13` unless a row says otherwise. The reference describes what runs; this page
describes what a struct is *supposed* to be and the ordered crumbs that get it there.

Two rows of [`../reference/not-yet.md`](../reference/not-yet.md) point at each other and
are one missing piece: a `struct` parameter is not copied, and a `struct` has no copy at
assignment. Both are **silently wrong results** on a page whose third line promises none.
This page closes both, and says plainly which neighbouring shapes stay refused instead of
half-working.

---

## 1. What a `struct` is today

Measured, not assumed. Every claim below carries the line that decides it.

| question | answer | where it is decided |
|---|---|---|
| what does the type id say | `type_new(name, 8, 8, TK_INT)` — width **8**, align 8, integer class | `teko_access.tk:728` (`tk_type_word`), and the forward placeholder's identical call at `teko_fwd.tk:355` |
| so what does `type_width` say | **8**, for every struct, whatever its fields | same |
| what does a slot of struct type hold | a **pointer** to the allocation | `teko_struct.tk:18-24` (the module header states it), `docs/reference/types.md:420-422` |
| what does the ABI pass | one integer register — a pointer is an `i64` the core's own operators already fit, "so no derived machine and no intrinsic is needed" | `teko_struct.tk:23-24` |
| where does the storage come from | `new Name` / `new Name()` only, lowered to a generated `name_new()` whose whole body is `uptr p = rt_alloc(NAME_SIZE); return p;` | `teko_struct.tk:1604-1612` (`tk_ctor`), emitted at `teko_struct.tk:1925` |
| and a bare `Name p;` | **no storage at all** — the slot is uninitialised, and reading it is refused | `teko: p is used before it is assigned` (D46); measured, probe `p08` |
| is `NAME_SIZE` always a multiple of 8 | **yes** — `struct A { u8 a; }` publishes `A_SIZE == 8` | probe `p32`, exit 42 |
| is a struct reference-counted | **no** — `tk_is_counted` answers 0 for a struct row | `teko_struct.tk:812-831`; `docs/reference/memory.md:176` |
| is a **field** of a struct reference-counted | **yes**, by the field's own type, through the same `tk_os_mark` road a class field takes | `teko_struct.tk` `tk_os_mark`; measured, probes `p30` and `p31` give the **identical** answer |
| can a struct declare a constructor | **no** — `teko: a constructor needs a class` | probe `p42` |
| can a struct implement an interface | **no** — the `:` is not even parsed (`expected { in the struct body`) | probe `p36` |

**The consequence for the design.** A struct value is a pointer; a slot of struct type is
eight bytes; the object it names is an `rt_alloc` block that is never reclaimed. "Copy"
therefore cannot mean *the backend already does it* — the backend moves eight bytes and is
right to. It has to be **a fresh allocation plus a memberwise copy**, written at the site
where C# would copy.

This page does **not** propose changing the representation. D5 gave a struct value a
pointer; D56 built a struct global on top of that ruling and every road in the tree —
fields, globals, `T?`, `T[]`, `ref`/`out`, the reclaim's `tk_is_counted` exclusion — reads
it. What D56 also recorded, in one sentence, is that **"C# copies a struct on assignment;
teko does not"**. That sentence is the thing this page supersedes: the *representation*
stays, the *semantics* become C#'s.

---

## 2. The ground truth, measured widely

Every row ran at `e3e6aa13` on a fresh worktree build (`mc build . --config mc.macos.toml`,
mc 1.0.1, macos/aarch64). The command that produced them is in § 10. `C#` is what the
same program answers in C#.

### 2.1 Copy and alias

| # | shape | teko | C# | verdict |
|---|---|---|---|---|
| p01 | `void f(S s) { s.n = 99; } … a.n = 1; f(a); return a.n;` | **99** | 1 | **silently wrong** |
| p02 | `S b = a; b.n = 9; return a.n;` | **9** | 1 | **silently wrong** |
| p03 | `S b = t.s;` — from a struct field | **9** | 1 | **silently wrong** |
| p04 | `S b = a[0];` — from a `S[]` element | **9** | 1 | **silently wrong** |
| p05 | `S a = mk();` — from a call return | **9** | 1 | **silently wrong** |
| p47 | `S b = c.get();` — a method returning a **field** | **9** | 1 | **silently wrong** |
| p10 | `t.s = a; a.n = 9; return t.s.n;` — a struct field of a struct | **9** | 1 | **silently wrong** |
| p22 | `c.s = a; a.n = 9; return c.s.n;` — a struct field of a **class** | **9** | 1 | **silently wrong** |
| p11 | `arr[0] = a; a.n = 9; return arr[0].n;` — a `S[]` element store | **9** | 1 | **silently wrong** |
| p18 | `g = …; S b = g;` — a struct **global** | **9** | 1 | **silently wrong** (D56 recorded this one on purpose) |
| p14 | `i64 f(params S[] xs) { xs[0].n = 99; } f(a);` | **99** | 1 | **silently wrong** |
| p40 | `foreach (S s in a) { s.n = 9; }` | array mutated, **108** | **refused**, CS1654 | **silently wrong**, and C# refuses the write outright |
| p43 | `i64 f(S s) { s.n = s.n + 1; return s.n; }` | `f` **2**, caller **2** | `f` 2, caller 1 | **silently wrong** |
| p46 | `S? b = a; a.n = 9; return b.Value.n;` | **9** | 1 | **silently wrong** |

That is **fourteen** distinct shapes, not two. Any design that fixes only the two rows
`not-yet.md` names leaves twelve.

### 2.2 What is already right, or already refused

| # | shape | teko | C# | verdict |
|---|---|---|---|---|
| p06 | `void f(ref S s) { s.n = 99; }` | **99** | 99 | **correct** — the escape hatch the design must keep |
| p07 | `void f(out S s) { s = new S; s.n = 99; }` | **99** | 99 | **correct** |
| p41 | `s.bump()` mutating through the implicit receiver | **2** | 2 | **correct** — a struct method mutates the variable in C# too |
| p45 | `use (&a)` — capture by reference | **9** (sees the write) | n/a (C# has no explicit capture) | **correct by construction** |
| p25b | `use (a)` where `a` is a **class** | **9** | 9 | **correct** |
| p08 | `S a;` then `a.n = 7` | refused, `teko: a is used before it is assigned` | refused | **correct** (D46) |
| p13 | `a == b` on two structs | refused, ``teko: S declares no operator `==` `` | **memberwise equality** | **honest refusal**, not C#'s form — stays refused (§ 7) |
| p50 | `&s.n` | refused, `teko: the address of a field is not taught yet; pass it as `ref` or `out`: n` | n/a | **correct** (D104, verified on this head) |
| p36 | `struct S : I` | refused (`expected { in the struct body`) — no struct boxing exists | allowed | **honest**, and it removes a whole copy site from the design |
| p42 | `new S(5)` | refused, `teko: a constructor needs a class` | allowed | **honest**, and it removes another |
| — | `return this;` inside a struct | refused, ``teko: `this` is not a value in a struct`` | allowed | **D103, not reopened** |

### 2.3 What is refused, but with the wrong words

| # | shape | teko says | should say |
|---|---|---|---|
| p17b / p25a | `use (a)` capturing a **struct** by value | `teko: a value of type S does not convert to i64` | a message that names the struct and the `&` road (§ 7) |

That message is a D103-shaped defect — a generic conversion refusal standing in for a
reason — and it is the fourth one found in the struct family in three days.

### 2.4 What compiles today and must not, once a copy exists

| # | shape | teko | C# | why it matters |
|---|---|---|---|---|
| p34 | `struct S { public S next; }` | **compiles**, `next` is an 8-byte pointer | **refused**, CS0523 | a memberwise **deep** copy of this recurses for ever |
| p35 | `struct T { public S a[2]; }` — an inline array of struct | **compiles** | refused (C# has no inline arrays outside `fixed`) | the copy has to reach the elements, or refuse |
| p19 | `S[] a = new S[2]; return a[0].n;` | **SIGSEGV, exit 139** | 0 | `new S[n]` fills the rows with **null pointers**, not with n zeroed structs; not this page's crumb, but it is the same root and § 8 names it |
| p21 | `S a = new S; if (a == null)` | **compiles**, answers false | refused (a non-nullable value type is never `null`) | the reference shape is visible in the surface; § 8 |

---

## 3. The design: three candidates

| | A — copy at the landing | B — the slot holds the object | C — refuse what aliases |
|---|---|---|---|
| representation | unchanged: pointer, 8 bytes, `TK_INT` | `type_new(name, SIZE, 8, TK_OPAQUE)` — the slot **is** the struct | unchanged |
| what it costs | one `rt_copy` in `lib/rt.tk`; one generated `name_copy` beside the existing `name_new`; one pass of ~200 lines | field access, `a[i]`, assignment, the by-value ABI and a move of arbitrary width, **per width**, all of which mc's core does not have | a refusal at six sites, and the removal of the struct parameter, `l.a = p`, and `S b = a` from the surface |
| what it breaks | nothing accepted stays accepted with a *different* answer — that is the point; `--dump-ast` moves legitimately | everything: `T?`, `T[]`, globals, `ref`/`out`, `tk_is_counted`, D5, D53, D56, D104 | `tests/types_struct.tk`'s own `l.a = p`, `docs/reference/types.md`'s worked example, and `tests/surface_globals_struct.tk` |
| zero core changes | **yes** | **no** | yes |
| zero new intrinsics | **yes** | **no** | yes |
| fits `mc limits` | yes — `passes` 15 → 16 of 30, everything else unmoved | n/a | yes |
| is it C#'s form | **yes** | yes | no |

**B is a fork, and it halts here.** mc's own guide is explicit: *"`type_new` gives
**primitives**. Aggregates, members, `a[i]` on your type, typed pointers, generics and
user-defined conversions are not here — a module that wants structure lowers to `uptr` the
way `examples/lang` does"* (`mini_compiler/docs/guide/96-a-new-primitive.md:161-165`). An
inline struct slot needs every one of those, plus a `MTASK_LOAD`/`MTASK_STORE` machine per
distinct struct width, plus a by-value ABI for an arbitrary size; mc defers even the
narrower `MTASK_DEPTH_SPAN` with its price written down (`96-a-new-primitive.md:180-184`).
That is a contract change in `mc/src/`, which this repository does not make. **B is not
planned around and not attempted.**

**C is not smaller, it is poorer.** It refuses `S b = a;`, `b = a;`, `x.f = a;`,
`a[i] = s;`, a by-value struct parameter and `S a = mk();` — after which a struct is a
field bag you can only reach through `ref`, and two landed fixtures plus the reference's own
worked example have to be rewritten. A smaller honest surface beats a large one that half
works, but C is not smaller than A by any measure that matters: A's whole machinery is one
runtime function, one generated function and one pass.

### 3.1 The chosen design, in one sentence

> **A struct value is copied at every point where it lands in a storage location, by a
> generated `name_copy` that allocates a fresh block, copies the bytes, keeps the count of
> every counted field and recurses into every struct-typed field.**

Copying at the **landing** and nowhere else is what makes this cheap. `return s;` needs no
copy, because whatever the caller does with the result is itself a landing that copies.
`f(mk())` needs no copy of the return value, because the by-value parameter is a landing.
`mk().x` needs no copy, because a field read is not a landing. The landing set is closed
and small:

| | landing | node the pass rewrites |
|---|---|---|
| L1 | `S b = <e>;` | `N_VAR` of struct type with an initializer |
| L2 | `b = <e>;` — local, parameter or global | `N_ASSIGN` into a slot of struct type |
| L3 | `x.f = <e>;` | the marked field store, where the **field**'s type is a struct |
| L4 | `a[i] = <e>;` on a `S[]`, `params S[]` included | the element store, where the **element** type is a struct |
| L5 | a by-value argument at a struct-typed parameter | the argument at index `i` where `decl_param_type(d, i)` is a struct row **and** `tk_rp_kind` says the parameter is not `ref`/`out` |
| L6 | `S? b = <e>;` | the nullable conversion, where the enclosed type is a struct |

**As built (D106, V3–V5), three deviations from the table above.** L3 and L4 are ONE arm,
not two: every one of the nine sites that builds a slot store passes `tk_os_mark`
(teko_struct.tk), which already holds the slot's declared type, so the value node is
recorded there and the row resolved at pass 16 — where a FORWARD type is a real row and
`tk_params_pass` has already built its `params` element stores. L5 is TWO roads, not one:
`decl_find` answers for a call that names its callee, and the three INDIRECT roads — the
vtable, the itab and the delegate — name none, so their argument is recorded at the one door
all three pass (`tk_vca_park`, teko_typeof.tk). And L6 is NO arm at all: Q1a makes a nullable
over a reference the reference itself, so peeling the slot's row with `tk_row_through_nl` in
the one place that derives it gives the `?` spelling of all five other landings for free.

**The one exception, at every landing:** when `<e>` is *provably fresh* — an `N_CALL` to
the generated `name_new` symbol, i.e. the `new S` / `new S()` the parser just lowered — the
copy is skipped. Nothing else is provably fresh: a method may `return f;` on a field
(p47), so an ordinary call's result is copied.

### 3.2 What the pass reads, and why it is last

`tk_copy_pass` registers immediately **before** `pass(&tk_rc_pass)` in `teko.tk` — behind
the oracle (`tk_typeof_pass`, so every deferred `.` has a type), behind `tk_ref_pass` (so a
`ref x` argument is already an address and is not of struct type any more), behind
`tk_deleg_pass` and `tk_ops_pass` (so a call is a call), behind `tk_params_pass` (so a
`params` list is already the `S[]` whose element stores L4 catches) and behind
`tk_over_pass` (so `decl_find` on a call's name answers **one** declaration). The reclaim
runs last, unchanged: a struct is not counted, so `S b = name_copy(a);` is invisible to it.

### 3.3 The generated copy function

Emitted by `tk_struct()` at the closing `}`, next to the existing `tk_ctor` call, with the
same naming rule (`tk_copy_name` mirrors `tk_ctor_name`: `point_copy` beside `point_new`,
the compiler's, D48).

```
// what the compiler emits for:  struct Point { i64 x; Cell c; Inner in; }
Point point_copy(Point s) {
    uptr d = rt_alloc(POINT_SIZE);
    rt_copy(d, s, POINT_SIZE);                  // every byte, padding and inline arrays included
    rc_inc(ld64(s + POINT_C));                  // one line per COUNTED field (tk_is_counted)
    st64(d + POINT_IN, inner_copy(ld64(s + POINT_IN)));   // one line per STRUCT field
    return d;
}
```

Whole-block first, fix-ups after, is what makes it short: the bulk copy carries padding, a
`u8`/`u32` field's exact width and an inline array field (`i64 a[4]`, probe `p33`) with no
per-shape code at all, and only the two field kinds that need more get a line. A static
field has `fd_sym_at(fi) != 0` and is no part of the object (`tests/types_struct.tk` proves
`POINT_SIZE` is the same with and without it), so it is skipped.

`rt_copy` is the first of the **two** new runtime functions — `rt_retain_array(base, n)`,
the mirror of the `rt_release_array` a counted inline array field already had, is the other,
and the "as built" note below says why. `rt_copy` itself is five lines, legal because
`NAME_SIZE` is always a multiple of 8 (probe `p32`):

```
// lib/rt.tk
void rt_copy(uptr d, uptr s, i64 n) {
    i64 i = 0;
    loop { if (i >= n) break; st64(d + i, ld64(s + i)); i = i + 8; }
}
```

`rc_inc(0)` is already a no-op (`lib/rt.tk:132-135`), so a null field needs no guard.

**As built (D105, V2), four deviations from the sketch above.** The parameter is `uptr s`,
not `Name s` — the spelling a method's implicit receiver already takes (`teko_this.tk`), so
the field arithmetic in the body is plain pointer arithmetic and a struct value lands on it
the way it lands on `this`. The body opens with `if (s == 0) return 0;`: a struct field is a
pointer `rt_alloc` zeroed, so a struct whose struct-typed field was never built reaches the
recursion with 0, and `rt_copy` would read address 0 — the null is copied AS null instead.
And `rt_copy` is not the only new runtime function: an inline array FIELD of counted element
type (`struct W { Cell c[2]; }`, which compiles today) needs one `rc_inc` per element, so
`rt_retain_array(base, n)` joins it as the mirror of the `rt_release_array` `tk_release_fields`
already calls. An inline array field of STRUCT element type is unrolled, one copy per element. And the
arms select the field's row through `tk_row_through_nl`, not through `tk_struct_by_ty`: Q1a
makes a nullable over a REFERENCE the reference itself, so an `S? f` field holds exactly
what an `S f` field holds and has to recurse the same way — the `s == 0` guard answers the
null handle. The cycle check of § 4.4 peels the same way, so
`struct Node { public Node? next; }` is refused with the direct shape.

---

## 4. The surface

### 4.1 A parameter is a copy

```teko
// no-run
struct S { public i64 n; }

void f(S s) { s.n = 99; }               // legal: `s` is the callee's own struct

i64 main() {
    S a = new S;  a.n = 1;
    f(a);
    return a.n;                          // 1 -- C#'s answer, today's is 99
}
```

### 4.2 An assignment is a copy

```teko
// no-run
S a = new S;  a.n = 1;
S b = a;                                 // b is a fresh block
b.n = 9;
return a.n;                              // 1

t.s = a;   a.n = 9;   return t.s.n;      // 1 -- the field store copies too
arr[0] = a; a.n = 9;  return arr[0].n;   // 1 -- the element store copies too
```

### 4.3 `ref` and `out` are how you keep the alias

```teko
// no-run
void bump(ref S s) { s.n = s.n + 1; }    // legal, and unchanged by this design

i64 main() { S a = new S; a.n = 1; bump(ref a); return a.n; }   // 2
```

### 4.4 A struct cannot contain itself

```teko
// no-run
struct Node { public Node next; }                  // ILLEGAL
// teko: a struct cannot contain itself

struct A { public B b; }    struct B { public A a; }   // ILLEGAL, at B's `}`
// teko: a struct cannot contain itself
```

C# refuses the same shape (CS0523). Use a `class` for a linked structure — a class value is
a reference and this design does not touch it.

### 4.5 A `foreach` variable of struct type is read-only

```teko
// no-run
foreach (S s in a) { i64 k = s.n; }      // legal: reading is fine
foreach (S s in a) { s.n = 9; }          // ILLEGAL
// teko: a foreach variable of struct type is read-only
```

C# refuses it too (CS1654). The alternative — binding `s` to a copy — would make the write
compile and go nowhere, which is exactly the class of bug this page exists to remove.

**As built (D108), three notes.** The refusal is **not** raised where the loop is built: the
element declaration is the compiler's own, and the question is asked where a **write through
the name** is judged — `tk_os_mark` (teko_struct.tk, the one door every field and inline
element store passes), `tk_defer_member` (teko_typeof.tk, the same store on a receiver only
a pass can type, which is how a forward-declared struct reaches it) and `tk_prop_use`
(teko_prop.tk, a property's `set`). `tk_foreach` publishes the element name for the body it
parses and drops it at the end, so the rule holds exactly where the name is in scope. That
makes the refused set C#'s CS1654 set and wider than the one line above: `s.n = e`,
`s.inner.n = e` through a nested struct, `s.a[i] = e` on an inline array field and
`s.P = e` on a property — all four **compiled and mutated the array** before this crumb,
measured. And it **breaks programs that compile today**: `foreach (S s in a) { s.n = 9; }`
built and answered 99, and now it does not build. Two shapes are left standing on purpose:
`bump(ref s)` is C#'s own CS1657, a different rule about handing the variable out rather
than about modifying its members, and `s.bump()` is a call C# accepts outright (in C# it
mutates the loop variable's copy; here it still reaches the row). Both are in
[`../reference/not-yet.md`](../reference/not-yet.md).

### 4.6 A struct is captured by reference, not by value

```teko
// no-run
D d = new D(() use (&a) => a.n);         // legal
D d = new D(() use (a) => a.n);          // ILLEGAL
// teko: a struct is captured with `&`; there is no capture by value of a struct
```

Before D108 this was refused with `teko: a value of type S does not convert to i64`, which
named neither the struct nor the road.

**As built (D108).** The question is asked at `tk_lambda_use` (teko_deleg.tk), where the
capture list is read and the captured local's own type is already in hand for the counted
check beside it — so the refusal lands on the `use (...)` the user wrote instead of on the
closure slot's store further in. The row is peeled through `T?` (`tk_row_through_nl`) for
the same reason every other struct road peels it: Q1a makes a nullable over a reference the
reference itself. Nothing about `use (&a)` changed.

### 4.7 Two structs are not compared with `==`

```teko
// no-run
if (a == b) { }                          // ILLEGAL, unchanged
// teko: S declares no operator `==`
```

C# gives a struct memberwise equality for free. Teko does not, and this design does not add
it: the existing refusal already names the road (declare the operator, D8), and a generated
memberwise `==` is a second design with its own float/nested/counted questions. § 7 writes
the `not-yet.md` row.

---

## 5. The hooks, by module

| module | pass | what it grows |
|---|---|---|
| **`teko_struct.tk`** | parse time, `tk_struct()` at the closing `}` (`:1892-1926`) | `tk_copy_name` (mirrors `tk_ctor_name`, `:486`); `tk_copy_fn(name, si, ty, size)` building the body of § 3.3 from the field table (`fd_off`/`fd_ty`/`fd_sym`, `:300-306`); one `tk_top_emit_as` beside the existing one at `:1925`. The cycle check of § 4.4, run at the same close over the field graph. Reads `tk_is_counted` (`:816`) and `tk_is_struct` (`:405`) — both already public |
| **`teko_copy.tk`** (new, module 42) | `pass(&tk_copy_pass)`, registered in `teko.tk` immediately before `pass(&tk_rc_pass)` | **as built (D105, D106):** three readers and one door. `tk_copy_row(ty)` peels the slot's row through `T?` (L6); `tk_copy_fresh(e, si)` answers for `name_new` and for a `name_copy` this pass already wrote; `tk_copy_at(e, si)` is the single door, rewriting IN PLACE with `tk_nd_moved` + `tk_node_replace` rather than returning a node to splice. `tk_copy_var` walks `tk_nslv` (L1), `tk_copy_assign` / `tk_copy_args` walk the tree under `tk_ty_pass_walk` (L2 and the DIRECT road of L5), and the `cp_*` table walks what the slot sites recorded (L3, L4 and the INDIRECT roads of L5). Reads `decl_find` (mc core), `tk_decl_param_ty` / `tk_decl_param_node` / `tk_rp_kind` (`teko_ref.tk`) and `tk_ty_scope_or_global` (`teko_typeof.tk`) |
| **`teko_null.tk`** | — | **nothing, as built.** L6 needed no site of its own: peeling the slot's row with `tk_row_through_nl` where it is derived gives the `?` spelling of all five other landings at once (D106) |
| **`teko_struct.tk`** | `tk_os_mark` | one line and a two-column table: the value node of a landing a SLOT built, recorded where the slot's declared type is at hand and rewritten at pass 16 (D106, L3 and L4) |
| **`teko_typeof.tk`** | `tk_vca_park` | one line: the same record for the argument of an INDIRECT call, the one door the vtable, the itab and the delegate roads all pass (D106, the indirect half of L5) |
| **`teko_loop.tk`** | parse time, `foreach` | the read-only refusal of § 4.5 |
| **`teko_deleg.tk`** | parse time, `use (...)` | the by-value capture refusal of § 4.6, replacing the generic conversion message |
| **`lib/rt.tk`** | runtime | `rt_copy(d, s, n)` — five lines, § 3.3 |
| **`core_teko.mc` / `user.mc`** | — | **nothing.** No new subcommand, no new hook registration; `teko_init()` in `teko.tk` gains one `pass()` and `teko.tk` one `#include` |

**`mc limits` after the whole sequence:** `passes` **15 → 16** of 32, which is where D105
left it and where D106 leaves it — V3, V4 and V5 register no pass of their own. `syntax`
20/40, `types` 18/36, `alias` 25/50, `on_stmt` 4/8 all unmoved, because the design registers
no word and no type. `heap` rises with the source (471 936 of 33 554 432 at D106, from
469 184); every crumb re-runs `teko limits` as part of its gate.

---

## 6. The crumb sequence

Ordered, each landable on its own, each with its oracle.

| # | crumb | size | modules it touches | gate | docs it owes | can it run beside an unrelated crumb? |
|---|---|---|---|---|---|---|
| **V1** *(landed, D105)* | **A struct cannot contain itself.** The direct case and the mutual case, checked at each struct's closing `}` over the field graph | **S** | `teko_struct.tk` | `tests/refuse/struct_cycle_self.tk` + `tests/refuse/struct_cycle_mutual.tk`, both `expect-refuse: teko: a struct cannot contain itself` with the exact line; the whole fixture suite green; `teko limits` unmoved | `not-yet.md` row; `types.md` § struct one sentence | **no** — `teko_struct.tk` is the busiest file in the tree and D104 touched it two days ago. Must land alone, and **before V2** |
| **V2** *(landed, D105)* | **`rt_copy`, `name_copy`, and the first landing (L1).** `S b = <e>;` copies; `new S` is the one exception | **M** | `lib/rt.tk`, `teko_struct.tk`, **new** `teko_copy.tk`, `teko.tk` (one `#include`, one `pass`) | `tests/struct_copy_local.tk` **exit 42** — p02/p03/p04/p05/p47 inverted in one program, plus `rt_live()` proving the copy is a *new* block; `teko limits` shows `passes` 16/30, verdict ok; the fixed point closes; the whole suite green | `types.md` § struct (replace the alias paragraph), `memory.md` "What is not reclaimed" (a copy is one more uncounted allocation), `not-yet.md` (delete the assignment row), `DECISION_LOG.md` (supersede D56's one sentence) | **no** — it edits `teko.tk`'s pass list and `teko_struct.tk` |
| **V3** *(landed, D106)* | **The store landings (L2, L3, L4).** assignment, field store, `S[]` element store, `params S[]` element | **M** | `teko_copy.tk` only | `tests/struct_copy_store.tk` **exit 42** — p10/p11/p18/p22/p14 inverted; the suite green | `types.md`, `arrays.md` one row each; `not-yet.md` (delete the `params` note if one exists) | **yes** — one file, and nothing else in the tree reads it. Depends on V2 |
| **V4** *(landed, D106)* | **The call landing (L5).** A by-value struct parameter is copied at the call; `ref`/`out` is not | **M** | `teko_copy.tk` (+ *reads* `teko_ref.tk`, no edit) | `tests/struct_copy_param.tk` **exit 42** — p01/p43 inverted, a method parameter, a delegate parameter, a `params` element, and a `ref`/`out` parameter proving it still aliases; the suite green | `types.md` § struct, `not-yet.md` (delete the parameter row) | **yes**, same file as V3 so **not beside V3**. Depends on V2 |
| **V5** *(landed, D106)* | **The counted field, proved; and `S?` (L6).** The copy keeps the count of a class-typed field; `S? b = a;` copies | **S** | `teko_copy.tk`, `teko_null.tk` | `tests/struct_copy_counted.tk` **exit 42** with exact `rt_live()` assertions (copy a struct holding a `Cell`, drop one copy's scope, the `Cell` survives; drop both, it dies); `tests/struct_copy_nullable.tk` **exit 42** — p46 inverted | `memory.md` (a struct copy is a new owner of every counted field), `nullable.md` | **yes**. Depends on V2 |
| **V6** *(landed, D108)* | **The two refusals the copy cannot serve.** A `foreach` variable of struct type is read-only; a struct is captured with `&` | **S** | **as built:** `teko_struct.tk` (the table and the check, beside `tk_os_mark`), `teko_loop.tk`, `teko_typeof.tk`, `teko_prop.tk`, `teko_deleg.tk` | `tests/refuse/struct_foreach_readonly.tk` and `tests/refuse/struct_capture_byvalue.tk`, exact message + exact line; `tests/struct_copy_local.tk`'s own `foreachcheck` rewritten to the read | `not-yet.md` two rows; `delegates.md`, `control-flow.md` one paragraph each | **yes**, and it is the only crumb here that is independent of V2 as well — it can land first, in parallel with V1 |
| **V7** | **The reclaim of copies** (*optional for 1.0*). A copy the pass itself created is owned by its scope: `rt_free(p, SIZE)` at the `}`, at every jump that leaves it, and not at all when it is returned | **L** | `teko_rc.tk`, `teko_copy.tk` | `tests/struct_copy_reclaim.tk` **exit 42**: `rt_live()` back to its entry floor after a scope, and `rt_peak()` **not growing** across a 100 000-iteration loop that copies a struct; the suite green | `memory.md` (move the struct-copy row out of "What is not reclaimed") | **no** — `teko_rc.tk` is the reclaim's single source. Depends on V2..V5 |

**Honest answer on parallelism.** V1, V2 and V7 each edit a file the rest of the tree
reads (`teko_struct.tk`, `teko.tk`, `teko_rc.tk`) and **must land alone**, exactly as D104's
eight-module crumb had to. V3, V4 and V5 touch `teko_copy.tk` and almost nothing else, so
each can run beside an unrelated crumb — but **not beside each other**, because they edit
the same walk. V6 touches two leaf modules and is free.

**Minimum for 1.0:** V1, V2, V3, V4, V6. V5 is one short crumb and should be in. V7 is the
one that may slip, and § 8 prices the slip.

---

## 7. What I would refuse to build for 1.0

Three, each with its message and its `not-yet.md` row.

### 7.1 A generated memberwise `==`

C# gives every struct a default `Equals`/`==` over its fields. Teko keeps the refusal it has.

> ``teko: S declares no operator `==` ``

**`not-yet.md` row:**

| written | message |
|---|---|
| `a == b` on two values of `struct` type, with no `operator==` declared | ``teko: S declares no operator `==` `` — C# gives a struct memberwise equality for free; teko does not. The road is D8's: declare the pair (`==`/`!=`) as static members of the struct. A generated memberwise comparison is a design of its own — it has to decide float `NaN`, a counted field's identity-versus-value, and a nested struct's recursion — and none of that is owed by the copy ([struct-value.md](../specs/struct-value.md) § 7.1) |

### 7.2 `new S[n]` filling the rows with structs

`new S[2]` today hands out two **null** rows, and `a[0].n` faults (probe `p19`, exit 139).
C# hands out `n` zeroed structs. Building that means the array allocator calling `s_new()`
per row, which is `n` allocations nobody reclaims — a different design with a different
cost. For 1.0 the shape gets a **refusal at the read**, not a fill.

> `teko: a row of a struct array is not built; assign it before you read it`

**`not-yet.md` row:**

| written | message |
|---|---|
| reading a field of an unassigned row of a `struct` array — `S[] a = new S[2]; a[0].n` | `teko: a row of a struct array is not built; assign it before you read it` — C# fills `new S[n]` with `n` zeroed structs; teko fills it with `n` null handles, because a struct value is a pointer and the fill would be `n` allocations nobody reclaims. Assign the row first (`a[0] = new S;`). Before this refusal existed the read was a **SIGSEGV, exit 139** ([struct-value.md](../specs/struct-value.md) § 7.2) |

*(This one is adjacent, not owed by the copy. If it is cut, say so — but then the row above
must say `not judged`, and `not-yet.md`'s third line stops being true for it.)*

### 7.3 A struct captured by value in a lambda

The closure slot's layout is `teko_deleg.tk`'s own and copying into it is a seventh landing
in a module none of the crumbs above touch. The refusal names the road that already works
(probe `p45`: `use (&a)` is correct today).

> ``teko: a struct is captured with `&`; there is no capture by value of a struct``

**`not-yet.md` row:**

| written | message |
|---|---|
| `use (s)` capturing a value of `struct` type by value | ``teko: a struct is captured with `&`; there is no capture by value of a struct`` — before this message, the generic ``teko: a value of type S does not convert to i64``, which named neither the struct nor the road. `use (&s)` captures the same struct by reference and is what a lambda over a struct should write ([struct-value.md](../specs/struct-value.md) § 7.3) |

---

## 8. The rc question

**Measured first.** A store into a struct's class-typed field already runs the counted-store
road: probes `p30` (`struct S { Cell c; }`) and `p31` (`class H { Cell c; }`) give the
**identical** answer — the overwritten `Cell` dies in both. D56 recorded the reason: the
field store is gated by the **field's** type (`tk_os_mark`), never by the struct's. The
struct's own block is the thing that is not counted (`tk_is_counted`,
`teko_struct.tk:812`; `memory.md:176`).

**So what does the copy owe.** A copy is a **new owner** of every counted field it copies.
`rt_copy` moves the pointer bits without touching the count, so the generated `name_copy`
must `rc_inc` each counted field — the `rc_inc(ld64(s + POINT_C))` line of § 3.3, one per
field where `tk_is_counted(fd_ty_at(fi))` answers 1. `tk_is_counted` already answers for a
class, an interface, a delegate, a `T[]` and a `T?` over any of them
(`teko_struct.tk:812-831`), so the predicate needs no new arm.

Without that line the program is a **use-after-free**: two struct blocks name one `Cell`
with a count of 1, the first to be released frees it, the second reads freed memory. That
is worse than today's aliasing, and it is why V5's oracle is exact `rt_live()` numbers and
not an exit code alone.

**What the copy does *not* owe.** The copy's own block is a struct block, and a struct block
is not counted — so the copy adds nothing to any count and nothing to any release path.
That is also why it is not freed, which is the next section's problem.

---

## 9. Risks and law tensions

| | risk | recommended resolution |
|---|---|---|
| R1 | **The arena.** `lib/rt.tk:42` is a fixed **4 MiB** heap. A copy is an allocation nobody reclaims, so a loop that passes a struct by value exhausts it: a 16-byte struct copied 262 144 times is `arena exhausted`. Today the same loop allocates nothing | **Land V7.** Until it lands, `memory.md`'s "What is not reclaimed" gains the struct **copy** beside the struct allocation, in the same declared-debt table, and V2's own fixture asserts `rt_peak()` so the number is visible rather than discovered in the field. V7 is tractable: a copy the pass created escapes its scope only through `return` — every other exit (a store, an argument, a capture) is itself a landing that copies again, and `ref` hands out an address that dies with the call |
| R2 | **D56 says the opposite.** D56 (2026-09-14) recorded *"C# copies a struct on assignment; teko does not"* as the deliberate semantics of a struct global, and `docs/reference/types.md:484-486` says the same in the reference's own voice. `not-yet.md`'s row (written at `f81e0217`, newer) calls it "not judged" and owes a design | **The newest ruling wins, and C# decides the form.** V2's decision entry supersedes D56 **on the semantics only** and cites it: the *representation* D5 gave — a pointer, eight bytes — is untouched, and every road built on it (D53, D56, Q1a, D104) keeps working. `types.md`'s paragraph is rewritten by V2, not deleted |
| R3 | **A `ref` argument must not be copied.** If the pass mistakes `f(ref a)` for a by-value landing, `ref` silently stops working — a new silently-wrong result in exchange for the one removed | Three guards as built (D106), not the two this row predicted, and the partition is **not** the one it predicted either. Measured by mutation: removing `tk_rp_kind` alone from the direct road fails the gate (`struct_copy_param` exits 132, the `ref` parameter forwarded on); removing `tk_rfarg_kind` alone from the indirect door fails it (exit 172, the delegate/vtable/itab `ref`); removing the N_ADDR shape check alone **does not** fail it, because `tk_rp_kind` already carries every measured shape, and removing the pair fails at the plainest `ref` of all (exit 122). The N_ADDR check is kept as the one guard that does not depend on `decl_find` answering with the call's own declaration, and the gate cannot prove it |
| R4 | **`--dump-ast` moves.** The law is that the dump is identical when a change does not change accepted code. V2 changes what accepted code *answers* | Say it in the decision entry rather than working around it. V1 and V6 are refusals and **must** leave the dump of accepted code byte-identical — that is their own gate. V2/V3/V4/V5 change it on purpose, and each names in its PR body exactly which fixtures' dumps moved and why |
| R5 | **`TK_MAXXT`** (4 096 expressions whose type is known, `teko_struct.tk:56`) gains rows from every generated copy body — a few per field | Measured today's worst fixture at 239 of 4 096. A unit with all 256 fields (`TK_MAXFIELD`) adds roughly 512. No change recommended; V2's gate re-runs `teko limits` and the number is watched |
| R6 | **`name_copy` is a reserved symbol.** A user function called `point_copy` now collides with the compiler's, exactly as `point_new` already does (D48) | Same rule, same message road as `point_new`. V2 adds a refuse fixture only if `point_new`'s own collision has one; if it does not, neither does this, and the row goes in `not-yet.md` beside D48's |
| R7 | **The cycle check runs at the closing `}`, not at the field.** A mutual cycle is only visible once the second struct closes, so the refusal is reported at `B`'s `}` and not at `A`'s field | That is where C# reports it too (CS0523 names the member, at the member). V1's `expect-refuse-line` pins whichever line it actually lands on; the fixture is the contract |
| R8 | **`foreach` read-only is a refusal on code that compiles today** (probe `p40` runs and answers 108) | It is a *silently wrong* program today — the write reaches the array, which C# forbids precisely because the reader expects a copy. V6 turns a wrong answer into a refusal, which `not-yet.md`'s third line requires. It is a breaking change and belongs in its own decision entry |

---

## 10. How every measurement was made

A detached worktree at `e3e6aa13`, built with the pinned mc:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
export PATH=$HOME/.local/mc/mc-1.0.1-macos-arm64:$PATH
git worktree add ../tk-structval e3e6aa13 --detach
cd ../tk-structval
sed -e 's/^os   = .*/os   = "macos"/' -e 's/^arch = .*/arch = "aarch64"/' \
    teko.toml >mc.macos.toml
mc build . --config mc.macos.toml
```

Each probe is one `.tk` under `probe/` **at the package root** (a config under `build/`
dies with `mc: cannot open: build/tests/...`, because every relative path in a config
resolves against the config's own directory), built and run with:

```sh
sed -e "s|^entry = .*|entry = \"$src\"|" -e "s|^out   = .*|out   = \"build/probe\"|" \
    mc.macos.toml > mc.probe.toml
./build/teko build . --config mc.probe.toml --entry-only   # the LINKING form
./build/probe; echo "EXIT=$?"
```

`./build/teko file.tk -o x` emits an **object**, not an executable; only
`build . --config <cfg> --entry-only` links.

`--dump-ast` takes **no** `--config` — verified on this head:

```
$ ./build/teko --dump-ast probe/p02.tk --config mc.macos.toml
mc: unknown option: --config
$ ./build/teko --dump-ast probe/p02.tk
mc: cannot open: probe/rt.tk
$ ./build/teko --dump-ast --include=lib --include=tests probe/p02.tk | wc -l
1159
```

so a fixture spelling `#include "rt.tk"` dumps **nothing** without `--include`, and an
empty-versus-empty comparison proves nothing. Every crumb that claims a dump is unchanged
must count **non-empty** dumps on both sides.

The limits reading quoted in § 5 is `mc limits . --config mc.macos.toml` on this head:
`passes 15/30`, `syntax 20/40`, `types 18/36`, `alias 25/50`, `on_stmt 4/8`,
`heap 469184/33554432`, `tolerance 1.00, verdict ok`.

---

## 11. Citations checked on this head

Every citation the brief carried, re-verified at `e3e6aa13`:

| claim | status |
|---|---|
| `this` is not a value inside a struct (D103) | **holds** — `DECISION_LOG.md:11628`, and the `not-yet.md` row quotes the message |
| a fixed array of structs stays refused (D99) | **holds for the declaration** (`Cell cs[2];` → `teko: an array of objects is not taught yet; use a field array or wait for T[]`, `DECISION_LOG.md:11248-11250`) — but `struct T { public S a[2]; }`, an inline array **field** of struct type, **compiles today** (probe `p35`). The two are different doors and § 2.4 records it |
| a struct allocation is not counted, declared debt | **holds** — `docs/reference/memory.md:176` |
| `&s.n` is refused with every other field address (D104) | **holds** — verified by probe `p50` |
| D102/D103 live in `teko_this.tk`/`tk_this()`; D104 spans eight modules | **holds** — `git show --stat e3e6aa13` names `teko_ref.tk`, `teko_struct.tk`, `teko_typeof.tk`, `teko_deleg.tk`, `teko_class.tk`, `teko_default.tk`, `teko_prop.tk`, `teko_generic.tk` |
| "the 31 modules at the root" | **drifted** — there are **41** (`teko.tk` + 40 `teko_*.tk`), which is what `CLAUDE.md` says. `teko_copy.tk` makes 42 |
| `docs/reference/types.md` describes the alias as designed | **found, and it is the tension R2 names** — `types.md:484-486`, resting on D5/D56, says the opposite of `not-yet.md`'s newer row |
