# `string`

**Designed, not built.** Nothing on this page compiles today; every sample carries
`// no-run` for that reason. What runs is [the type reference](../reference/types.md), and
this page is kept apart from it on purpose ([the specs index](README.md)).

teko has `str` today: a `uptr` with a NUL at the end
([types.md](../reference/types.md) § Scalars), no length, no value equality, no
concatenation, and `tk_str_len` walking to the terminator every time anybody asks how long
it is. C# has `string`: an immutable reference with a `Length`, indexing by `char`,
comparison **by value**, concatenation with `+`, interpolation with `$"…"`, and a large
method surface.

This is the largest item in the plan and the only one that touches something already
shipped, so it is the one that has to be split into crumbs that each land on their own. It
is also the one that must not break a single line of what exists, and § 2 is the rule that
makes that true.

---

## 1. What a `string` is

**An ordinary counted teko class**, declared in `lib/string.tk`. Not a `type_new`
primitive, and the reason is decisive: a primitive has no row in `teko_struct.tk`'s type
table, so `tk_is_counted` answers `0` for it and **the reclaim never fires** — every string
a loop built would live for the run. A class is a reference with a vtable at word 0 and a
count at word 1, which is exactly what C# says a string is, and it brings methods,
properties, operators, a constructor and a destructor with it at no cost in mechanism.

```
class string {
    private i64  nbytes;      // STRING_NBYTES = 16   the UTF-8 length, no NUL
    private i64  nchars;      // STRING_NCHARS = 24   the code-point count
    private uptr data;        // STRING_DATA   = 32   nbytes bytes, then a NUL
    private i64  owned;       // STRING_OWNED  = 40   1 when the destructor frees `data`
}                             // STRING_SIZE   = 48
```

Four decisions are packed into those four fields.

- **The bytes are UTF-8 and they live in a second block**, not in a tail of the object. A
  teko class has a fixed `NAME_SIZE`; a variable-length tail would need an allocator teko's
  `new` does not have. Two allocations per string is the price, `rt_alloc`'s size classes
  absorb the 48-byte half, and the destructor gives both back.
- **`nchars` is stored, not walked.** `.Length` is C#'s "number of characters" and a
  program reads it in a loop condition; computing it by walking would make every loop
  quadratic. It costs eight bytes and one pass at construction.
- **`nbytes == nchars` means the string is ASCII**, which makes `s[i]` an `ld8` rather than
  a walk. It is the common case and it costs one comparison to detect.
- **`data` is NUL-terminated** even though `nbytes` says where it ends. That single byte is
  what makes § 3's `string` → `str` conversion a field load instead of a copy, and it is
  what lets a `string` reach `puts`, `write` and every other `extern` unchanged.

The methods are **not virtual**. C#'s `string` is sealed, teko has no reason to differ, and
it matters here for a reason § 4 needs: an interned literal's word 0 is never read.

## 2. The rule: `str` stays, `string` arrives beside it

Changing the type of `"..."` would break every fixture, every `extern`, `panic(str)`,
`puts`, and `lib/rt.tk` itself. It is not done. The rule is four lines and it is the whole
of the coexistence:

1. **A `"..."` literal is a `str`**, exactly as today: an `N_STR` the core types `TY_UPTR`.
   Nothing that compiles now compiles differently, and `--dump-ast` is byte-identical for
   every program that names no `string`.
2. **A literal written where a `string` is expected becomes an interned `string`**, at
   compile time, with no allocation and no run-time work (§ 4). `string s = "hi";` reads
   like C# and costs nothing.
3. **A `string` converts to `str` implicitly**, in every slot, because it is a field load
   of an already-NUL-terminated pointer. `panic(s)`, `puts(s)` and `extern i64 write(i64,
   str, i64)` all take a `string` with nothing written down.
4. **A `str` converts to a `string` only explicitly**, `new string(p)`, which measures with
   `tk_str_len` and copies. C# spells the same thing `new string(char*)`.

Read together: **`str` is the C boundary, `string` is the language's string.** That is the
same pair C# itself has under `unsafe`, and it is the sentence
[types.md](../reference/types.md) gains when this lands.

The future `teko_std` package's `strings.tk` (`docs/specs/packages.md`) is unaffected: its
`str` functions are the low-level layer and stay exactly as they are, and `string`
overloads grow beside them. Nothing is renamed and nothing is removed.

## 3. Conversions

| from | to | how | why |
|---|---|---|---|
| a `"..."` **literal** | `string` | **implicit**, rewritten to the interned object (§ 4) | rule 2 |
| `string` | `str`, `ptr`, `uptr` | **implicit**, one `ld64` of the `data` field | rule 3 |
| `str`, `ptr`, `uptr` (not a literal) | `string` | **explicit**: `new string(p)`, which copies | rule 4 |
| `string` | `string` | identity | |
| `null` | `string` | **implicit** — a `string` is a reference and C# strings are nullable | D32 |
| `string` | any number | refused: `teko: a value of type string does not convert to i64` | D34, already |
| any number | `string` | refused: `teko: a value of type i64 does not convert to string` | D34, already |
| `char` | `string` | `new string(c)` and `new string(c, i64 count)` | C#'s two constructors |
| `i64`, `f64`, `decimal`, `DateTime`, `Guid`, an `enum` | `string` | `.ToString()`, never implicit | |

**Rule 3 also converts a `string` into a plain `uptr` slot**, and there is nothing to be
done about that: `str`, `ptr` and `uptr` are one type, which
[types.md](../reference/types.md) § Limits already publishes ("an overload cannot tell them
apart"). It is written down rather than discovered.

**The borrowed `str` dies with the object.** `str p = s.ToString();` inside a scope whose
`s` is released leaves `p` dangling — the same hazard `tk_str_slice` already has, and the
same one C# has with a `fixed` pointer. It goes in [memory.md](../reference/memory.md) with
the rest of the reclaim's rules, in the crumb that ships it.

Both implicit directions are **`tk_num_widen`'s siblings**: a helper that wraps a node and
hands it back for the caller to splice, called from the same nine slots D33 enumerated — a
variable initializer, an assignment, a `return`, an argument of a free, method, virtual or
interface call, an element of a `params T[]`, and a field store. **Zero new passes**, and
the shape is one the modules already know.

## 4. The literal, interned

A literal in a `string` slot becomes an `N_IDENT` naming a module-private global, gensym'd
with a `$` the lexer never forms into an identifier — the shape
`docs/specs/decimal.md` § 3 uses, and the shape `mc`'s own `<i128>` uses for the same
reason. The global is an ordinary initializer list:

```
uptr $s7[6] = { 0, 1099511627776, 2, 2, "hi", 0 };
//              vt  pinned count   nb nc  bytes  owned
```

Three things about it are load-bearing, and all three were measured on the tree rather than
assumed:

- **A string literal inside a global initializer list is ordinary `mc`.** It works, and it
  is what puts the bytes in `__data` with the relocation the object writer already knows how
  to emit.
- **`&global` inside a global initializer is `initializer must be constant`.** So word 0,
  the vtable, is **zero** — and it is never read, because the count never reaches zero
  (below) and the methods are not virtual. An interned literal is a `string` no
  `string_vt_init()` ever has to have run for, which matters: that function is emitted to
  be called by the constructor, so a program using only literals never calls it at all.
- **The count is pinned at 2^40.** `rc_inc`/`rc_dec` are balanced by the reclaim, so a
  literal's count moves up and down around the pin and cannot reach `0`; an unbalanced
  `rc_dec` would have to fire a trillion times to get there. `rt_live()` does not move for a
  literal, because `rt_alloc` was never called, and a fixture asserts exactly that.

Interning is by **value**, per compilation unit: two occurrences of `"hi"` in one unit name
one global, which is C#'s own literal interning and costs a lookup at parse time.

## 5. The surface

**Legal.**

```teko
// no-run
#include "string.tk"

string greet(string who) {
    return "hello, " + who;
}

i64 main() {
    string a = "hi";                             // interned, no allocation
    string b = "hi";
    if (a != b) return 1;                        // by VALUE, not by reference
    if (a.Length != 2) return 2;
    if (a[0] != 'h') return 3;

    string c = a + " there";
    if (c.Length != 8) return 4;
    if (c.Substring(0, 2) != a) return 5;
    if (c.IndexOf("there") != 3) return 6;
    if (c.StartsWith("hi") == false) return 7;

    puts(c);                                     // a `string` in a `str` slot
    if (tk_str_len(c) != 8) return 8;            // the same, into rt.tk

    string d = new string("raw");                // an explicit copy of a `str`
    if (d != "raw") return 9;

    string e = null;                             // a string is a reference
    if (e != null) return 10;

    if (greet("world").Length != 12) return 11;
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
#include "string.tk"

i64 main() {
    string s = "hi";
    i64 n = s;                    // teko: a value of type string does not convert to i64
    string t = 5;                 // teko: a value of type i64 does not convert to string
    string u = "n=" + 5;          // teko: no operator `+` takes these operands
    str p = "raw";
    string v = p;                 // teko: a value of type uptr does not convert to string
    s[0] = 'H';                   // teko: a string is immutable
    string w = $"n={n}";          // teko: string interpolation is not taught yet
    return 0;
}
```

| written | message |
|---|---|
| a `string` in a numeric slot | `teko: a value of type string does not convert to i64` (D34, no new code) |
| a number in a `string` slot | `teko: a value of type i64 does not convert to string` (D34, no new code) |
| a non-literal `str` in a `string` slot | `teko: a value of type uptr does not convert to string` — `new string(p)` is the copy |
| `string + i64` and every other mixed `+` | ``teko: no operator `+` takes these operands`` (§ 8) |
| `s[i] = c` | `teko: a string is immutable` |
| `$"…"` | `teko: string interpolation is not taught yet` — **and the lexer gets there first today**, § 9 |
| `s.Lenght` | `teko: string has no member Lenght` — the wording a class member already gets |
| `string` used without the include | ``teko: `string` needs #include "string.tk"`` |

## 6. `Length`, and what an index means

C#'s `string` is UTF-16 and `Length` counts UTF-16 units; teko's `char` is a scalar code
point (`u32`, `docs/reference/types.md` § Scalars) and the bytes are UTF-8. The two cannot
both be C#'s, so the surface keeps C#'s **meaning** and states its own units:

| member | is | cost |
|---|---|---|
| `.Length` | the number of **code points** | `O(1)`, stored |
| `.Utf8Length` | the number of **bytes** | `O(1)`, stored |
| `s[i]` | the `i`-th **code point**, as a `char` | `O(1)` when the string is ASCII (`nbytes == nchars`), `O(i)` otherwise |

`.Utf8Length` is not a C# member and is here because it is the number every `extern` wants
and it would otherwise be a walk. `s[i]` on an index outside `0 .. Length - 1` panics —
`teko: string index out of range`, exit 70 — which is the guard every `T[]` index already
carries ([arrays.md](../reference/arrays.md)).

`s[i]` is the one construct on this page that needs a lowering **outside** the class
mechanism: `[` is `teko_array.tk`'s `tk_bracket`, and it refuses a receiver that is not an
array today (``teko: `[` needs an array``). It gains one row: a receiver typed `string`
lowers to `tk_string_at(s, i)`. That is a targeted addition and **not** a general indexer —
`this[i]` on a user class stays untaught, and stays a `not-yet.md` row.

## 7. The API

Split across two crumbs; § 12 says which is which.

| static | instance — N7 | instance — N8 |
|---|---|---|
| `string.Empty` | `.Length`, `.Utf8Length` | `.Substring(i64)`, `.Substring(i64, i64)` |
| `string.Concat(string, string)` | `.ToString()` — identity | `.IndexOf(string)`, `.IndexOf(char)`, `.LastIndexOf(string)` |
| `string.IsNullOrEmpty(string)` | `.Equals(string)`, `.CompareTo(string)` | `.Contains(string)`, `.StartsWith(string)`, `.EndsWith(string)` |
| `string.Join(string, string[])` | `.GetHashCode()` | `.Trim()`, `.TrimStart()`, `.TrimEnd()` |
| | | `.ToUpper()`, `.ToLower()` — **ASCII only**, § 10 |
| | | `.Replace(string, string)`, `.Split(char)` → `string[]` |
| | | `.PadLeft(i64)`, `.PadRight(i64)` |

Every one allocates a **new** string or returns an existing one; nothing mutates. `.Split`
returns a `T[]` of counted elements, which is a shape `docs/reference/arrays.md` already
describes and the reclaim already releases.

## 8. Operators

| written | how | result |
|---|---|---|
| `a + b`, both `string` | `public static string operator+(string a, string b)` on the class | a new `string`, owned |
| `a == b`, `a != b` | `public static i64 operator==(string a, string b)` and its mandatory pair (D8) | `i64` 0/1, **by value** |
| `a == null`, `null == a` | the same operator; `null` lands on a row parameter (D32) and the body answers on the pointer | `i64` 0/1 |
| `a < b` and the other three | not declared, so not taught: `.CompareTo` is the form | — |
| `string + i64`, `string + f64`, `string + Foo` | refused | — |

**Every row above is a mechanism that already runs.** An operator is a `public static`
member resolved over both operands (D8, [classes.md](../reference/classes.md) § Operators),
`tk_ops_pass` lowers it, and `tk_rc_pass` sees a call returning a counted row and parks the
temporary. There is no new operator machinery on this page at all.

`"n=" + 5` is legal C# and is **refused** here. It needs a `ToString` on every type, which
needs a root type, which needs `object`, which needs boxing — § 11. `"n=" +
tk_num(n)` is the road today and `$"n={n}"` is the road when § 9 unblocks.

## 9. `$"..."` needs `mc` 0.15.25, where a module claims `$`

C#'s interpolation is `$"a{x}b"`. In `mc` up to 0.15.24 the lexer could not hand it over:
`$` opens a **hole** token — `$1`, `$name`, `$$name`, the machinery `#rule` substitution
uses — and `$` followed by anything that is not a digit or a letter was `invalid hole`,
raised inside `lex_next` and therefore before any hook could see the token. Measured on the
tree at the 0.15.23 pin: `puts($"hi")` is `invalid hole` at the `$`.

**`mc` 0.15.25 made it pure surface.** When `$` is not a hole (not followed by a name, a
digit or `$`), the lexer falls through to the punctuation matcher, so a module that
registered `syntax_expr("$", …)` owns the token and reads the string literal after it with
`p_cp()`/`p_take_lit`; the three `#rule` forms are untouched, nothing in `mc`'s core changed.
N10 therefore waits for one thing only: teko's pin reaching 0.15.25 (a `MC_VERSION` crumb
proved by the whole recipe, D29/D35/D37). Until then `$"…"` is what the pinned lexer says it
is, and this page's own refusal (`teko: string interpolation is not taught yet`) only
becomes reachable afterwards.

**What it lowers to, so the crumb is ready the day it lands.** `$"a{x}b{y}"` becomes a
chain of `string.Concat` over the literal pieces and one formatter per hole, chosen **by
the hole's static type** — which is what keeps it inside D4:

| the hole's type | the formatter |
|---|---|
| `string` | itself |
| `str` | `new string(p)` |
| `char` | `new string(c)` |
| `i64` and every integer | `tk_str_from_i64` |
| `f64`, `f32` | `fmt_f64` (`<float_rt>`'s own) |
| `decimal`, `DateTime`, `TimeSpan`, `Guid`, an `enum` | that type's own `ToString` |
| anything else | `teko: no interpolation of a value of type Foo` |

No boxing, no run-time tag, no `object`. The alignment and format specifiers C# writes as
`{x,10:F2}` are not taught, and are a `not-yet.md` row.

## 10. What stays out

| left out | why |
|---|---|
| `$"…"` | § 9, on a pin at `mc` ≥ 0.15.25 |
| `"n=" + 5` | § 8: it needs a universal `ToString`, which needs `object` |
| `string.Format`, `{0}` placeholders | `params string[]` makes it writable as a library function once `string` exists; it is not a language construct |
| Unicode-aware `ToUpper`/`ToLower`, culture, collation, normalisation | a table-driven library, not a primitive. The two methods here are ASCII-only and say so in their own documentation |
| `char` as UTF-16, surrogate pairs | teko's `char` is a code point (`docs/reference/types.md`), and that predates this page |
| `StringBuilder` | a class over a growable buffer; a library once `string` exists |
| verbatim strings (`@"…"`), raw literals (`"""…"""`) | the lexer is `mc`'s and frozen; neither is asked for |
| `this[i]` on a user class | § 6: the index lowering is `string`-shaped on purpose; a general indexer is its own decision |
| `s[i] = c` | C# strings are immutable and so are teko's |
| interning **across** compilation units | teko compiles one unit (D14); the question does not arise |

## 11. Where `object` and `Nullable<T>` come in

Neither is designed here, and both are named so the order is honest.

**`object`** is what `"n=" + 5`, `string.Format` and a heterogeneous collection all
actually want, and it is **the one item in this plan that runs into a law**. C#'s `object`
is the root of everything, including the value types, and getting an `i64` into one means
**boxing**: a heap cell carrying a type tag, read back with a checked unbox. A type tag read
at run time is what D4 rules out ("no dynamic union, no tagged run-time value, no 'any'
type"). Two roads, and the choice is the owner's, not C#'s:

- **a reference root only** — `object` is a base every class implicitly derives from, giving
  `ToString`, `Equals` and `GetHashCode` through the vtable, with **no boxing of primitives
  at all** and every conversion still static. It fits D4 as written. It costs a vtable slot
  and an implicit base on every class, so it is L, and it changes the layout constants every
  fixture asserts;
- **full C# `object`**, which is boxing, which is an amendment to D4.

Recommendation: the first, and a `not-yet.md` row saying so. It lands after `string`,
because `ToString` returning a `string` is the whole reason to want it.

**`Nullable<T>` / `T?`** is designed, and on its own page: [nullable.md](nullable.md).
The paragraph that stood here — a generic `struct Nullable<T>` for a value `T`, landing
after `object`, with the reference half left open — is superseded by the owner's ruling of
2026-09-08: one mechanism, `T?` over ANY type, a construct of the compiler rather than an
instance of the generic one, and it lands **before** N7 so that `string? s = null;` is the
spelling from this page's first day.

## 12. The hooks, by module

| module | what it grows |
|---|---|
| **`teko_string.tk`** (new) | the literal interning (§ 4), the two implicit conversions of § 3, the include check, and the `$"…"` refusal |
| `teko_typeof.tk` | `tk_str_intern` and `tk_str_borrow` beside `tk_num_widen`, and the two calls in the slots it owns |
| `teko_rc.tk` | the same two helpers called at the six slots it owns (initializer, assignment, `return`, and the three call-argument kinds) |
| `teko_iface.tk`, `teko_expr.tk`, `teko_params.tk` | one call each, at the slot each already owns for `tk_num_widen` |
| `teko_array.tk` | one row in `tk_bracket`: a `string` receiver lowers to `tk_string_at` (§ 6) |
| `teko_ops.tk` | **nothing**: `operator+` and `operator==` on a class are D8's road |
| `teko_class.tk`, `teko_prop.tk` | **nothing**: `string` is an ordinary class with ordinary methods and properties |
| `lib/string.tk` (new) | the class, its methods, `tk_string_at`, `tk_str_from_i64`: about 600 lines of ordinary teko, `#include "rt.tk"` |
| `lib/rt.tk` | nothing; `tk_str_len` and `tk_str_slice` stay exactly as they are (§ 2) |
| `teko.tk` | `#include` and one `_init()` call |
| `core_teko.mc`, `user.mc` | nothing |

`lib/string.tk` includes `lib/rt.tk` and **not** the other way round: a program that wants
an array should not carry six hundred lines of string code. That leaves
`#include "string.tk"` as a build-time step C# does not have — the same trade
`docs/specs/datetime.md` § 13 leaves open, and it should be decided once for `time.tk`,
`decimal.tk`, `guid.tk` and `string.tk` together rather than four times.

## 13. What it costs in `mc limits`

| row | before | after | why |
|---|---|---|---|
| `types` | — | **+1 per program**, not per compiler | `string` is a declared class, and teko registers one id per declared class today |
| `syntax` | — | **+1** | the `$` claim, when § 9 unblocks; zero before that |
| `alias`, `passes`, `intrin` | — | unmoved | the conversions ride the nine slots that exist, the operators ride D8 |
| `nodes`, `funcs`, `globals`, `heap` | — | up | ~600 lines of library, and one global per distinct literal in a `string` slot |

`string` is the only page in this plan that adds **no** `type_new` to the compiler itself.
That is the strongest argument for the class over the primitive, after the reclaim.

## 14. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/surface_string_value.tk` | a literal in every one of the nine slots; `==` and `!=` by value against a second literal with the same bytes and a different one; `.Length` and `.Utf8Length` on ASCII and on a multi-byte string; `string.Empty`; `null` in and out | `42` |
| `tests/surface_string_intern.tk` | `rt_live()` and `rt_used()` do **not** move across a hundred uses of the same literal; two occurrences of `"hi"` are the same address; a literal survives being assigned and released in a loop | `42` |
| `tests/surface_string_interop.tk` | a `string` passed to `puts`, to `panic`-shaped `str` parameters, to `tk_str_len` and to an `extern`; `new string(p)` from a raw `str`, proving it copies by mutating the source afterwards | `42` |
| `tests/surface_string_concat.tk` | `+` on two literals, on two heap strings and on a mix; the result's `Length`; `rt_live()` returning to its starting value once the temporaries sweep | `42` |
| `tests/surface_string_index.tk` | `s[i]` on ASCII and on a multi-byte string; `s[Length - 1]` | `42` |
| `tests/surface_string_index_oob.tk` | `s[Length]` | `70` |
| `tests/surface_string_methods.tk` | `Substring`, `IndexOf`, `LastIndexOf`, `Contains`, `StartsWith`, `EndsWith`, `Trim`, `Replace`, `PadLeft`, `ToUpper`, `Split(…).Length` and the elements it produced | `42` |
| `tests/surface_string_rc.tk` | a `string` field of a class released with the object; a `string[]` released element by element; `rt_live()` back to zero at the end | `42` |

## 15. The crumbs

### N7 — the value (L)

`lib/string.tk`'s class with `nbytes`/`nchars`/`data`/`owned`, the constructor and
destructor, `.Length`/`.Utf8Length`/`.ToString()`/`.Equals`/`.CompareTo`, `operator+` and
`operator==`/`!=`, `string.Empty`/`Concat`/`IsNullOrEmpty`; the literal interning; the two
implicit conversions in all nine slots; the include check. **Depends on nothing** — not on
`docs/specs/decimal.md`, not on `docs/specs/datetime.md`, not on `enum`. It is placed late
because it is expensive, not because anything gates it, and it can be pulled forward at any
time.

**Gate:** `surface_string_value.tk`, `_intern.tk`, `_interop.tk`, `_concat.tk` and `_rc.tk`
at `42`; every existing fixture at its `expect-exit` with `--dump-ast` **byte-identical** —
that is the proof rule 2 held and no existing literal moved; `FIXPOINT OK`; `mc limits`
verdict `ok` with `passes`, `intrin` and `alias` **not moved**;
`sh scripts/check-docs.sh` green. **Owes:** a `string` section in
[types.md](../reference/types.md) stating the `str`/`string` pair, the borrowed-pointer
lifetime rule in [memory.md](../reference/memory.md), the refusals in
[diagnostics.md](../reference/diagnostics.md), `lib/string.tk` in
[runtime.md](../reference/runtime.md), the new module in
[modules.md](../internals/modules.md), the module count in
[`CLAUDE.md`](../../CLAUDE.md) and [docs/README.md](../README.md), and a section in
[guide/10-values-and-types.md](../guide/10-values-and-types.md).

### N8 — the methods, and the index (L)

`s[i]` in `tk_bracket` with its guard; `Substring`, `IndexOf`, `LastIndexOf`, `Contains`,
`StartsWith`, `EndsWith`, `Trim*`, `Replace`, `Pad*`, `ToUpper`/`ToLower`, `Split`, `Join`.
Depends on N7 and on nothing else.

**Gate:** `surface_string_index.tk` and `_methods.tk` at `42`, `_index_oob.tk` at `70`;
everything N7 gated on. **Owes:** runtime.md, the index guard in
[arrays.md](../reference/arrays.md), and the `this[i]` row in
[not-yet.md](../reference/not-yet.md).

### N10 — interpolation (M, after the pin reaches 0.15.25)

§ 9's lowering, one `syntax_expr("$", …)`, one fixture per hole type.
**Blocked** until `mc`'s lexer hands back a `$` before a `"`.

## 16. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **Two string types is a language smell.** A reader has to know which one to write. | § 2's sentence — `str` is the C boundary, `string` is the language's string — put in `types.md` at the top of both entries, and rule 3 making the boundary invisible in the direction that matters. C# has the same pair (`string` and `char*`); it just hides one behind `unsafe`. The alternative, retyping the literal, breaks every fixture and every `extern` on the first day and was rejected for that. |
| **The interned literal's vtable is zero.** An unbalanced `rc_dec`, or a virtual call on a `string`, would jump through it. | The count is pinned at 2^40, the methods are not virtual, and the fixture asserts `rt_live()` does not move. It is also **why** `string`'s methods stay non-virtual, which is C#'s own sealed rule, so nothing is given up. `&global` in an initializer is `initializer must be constant` (measured), so patching word 0 statically is not available; a program-start hook to patch it is a mechanism bought for a pointer nothing reads. |
| **`string` → `uptr` converts implicitly**, which C# has no conversion for. | `str`, `ptr` and `uptr` are one type in teko and always were; `types.md` § Limits already says an overload cannot tell them apart. Written down, not discovered. |
| **A borrowed `str` outlives its `string`.** | Documented in memory.md beside `tk_str_slice`, which has the identical hazard today. A counted `str` is not a thing this design creates. |
| **Two allocations per string.** | `rt_alloc`'s size classes take the 48-byte half from a free list, the destructor returns both, and the alternative — a variable-length tail — needs an allocator `new` does not have and a `NAME_SIZE` that is not a constant. Measured by `_concat.tk`'s `rt_live()` assertions rather than argued. |
| **`"n=" + 5` is refused, and it is the first thing a C# reader writes.** | It is honest until `object` exists, the message names the operator, and `$"n={n}"` is the answer the moment § 9 unblocks. Putting a special case in for `i64` alone would be a surface that grows by accident. |
| **`$"…"` cannot be lexed and the lexer is `mc`'s and frozen.** | § 9: report it, do not work around it (D2). It is two lines on `mc`'s side and it breaks nothing there, which is why it is worth asking rather than designing around. |
| **`.Length` counts code points and C#'s counts UTF-16 units.** | § 6: teko's `char` is a code point and predates this page, so counting units would need a UTF-16 `char` teko does not have. Both numbers are exposed (`Length`, `Utf8Length`) and both are `O(1)`. |
| **N7 is an L that touches the nine conversion slots D33 and D34 just finished.** | It reuses `tk_num_widen`'s exact shape at exactly those sites, and its gate is `--dump-ast` byte-identical on every existing fixture — which is the only proof that a change to those nine slots did not move anything. If that proof fails, the crumb is wrong and the failure is the finding. |
