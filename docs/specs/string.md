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
| `null` | `string?` | **implicit** into the nullable slot only — `string s = null;` is refused, D43 supersedes D32's "C# strings are nullable" reading | D43 |
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
    if (tk_str_len(c) != 8) return 8;            // ...and in rt.tk's own text readers

    string d = new string("raw");                // an explicit copy of a `str`
    if (d != "raw") return 9;

    string? e = null;                            // D43: null lands only in a T? slot
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
    string w = $"n={n}";          // invalid hole -- the lexer gets there first (§ 9)
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
| `$"…"` | `invalid hole` — the lexer gets there first, re-measured on the N7b pin (`mc` 1.0.1) and still true, § 9 |
| `s.Lenght` | `teko: unknown member of string: Lenght` — the wording a class member already gets (measured) |
| `string` used without the include | the core's own `expected ; after expression` — **not** the hint above this row, D48's limit re-measured for a class rather than an `enum` (D88, § 5's own correction just above) |

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
mechanism: `[` is `teko_params.tk`'s `tk_bracket` (registered `teko.tk:441`; not
`teko_array.tk` — corrected here, measured on the tree by N7a's own scout), and it refuses
a receiver that is not an array today (``teko: `[` needs an array``). It gains one row: a
receiver typed `string`
lowers to `tk_string_at(s, i)`. That is a targeted addition and **not** a general indexer —
`this[i]` on a user class stays untaught, and stays a `not-yet.md` row.

**Ordering (D86, the review's question).** `.CompareTo` is ordinal over the UTF-8 BYTES,
unsigned: the first byte that differs decides, a common prefix loses to the shorter string.
That is code-point order. C#'s `String.CompareOrdinal` orders UTF-16 code units, which agrees
everywhere except where a code point above U+FFFF meets one in U+E000..U+FFFF: UTF-16 puts the
surrogate pair (`D800`..`DBFF`) first, UTF-8 puts the four-byte sequence (`F0`..) last.
teko keeps code-point order — the same reason § 6 keeps code points for `.Length` — and
states the divergence rather than emulating a surrogate sort.

## 7. The API

Split across two crumbs; § 12 says which is which.

| static | instance — N7 | instance — N8 |
|---|---|---|
| `string.Empty`, `new string()` (the same value) | `.Length`, `.Utf8Length` | `.Substring(i64)`, `.Substring(i64, i64)` |
| `string.Concat(string, string)` | `.ToString()` — identity | `.IndexOf(string)`, `.IndexOf(char)`, `.LastIndexOf(string)` |
| `string.IsNullOrEmpty(string?)` | `.Equals(string)`, `.CompareTo(string)` | `.Contains(string)`, `.StartsWith(string)`, `.EndsWith(string)` |
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
| `a == null`, `null == a` | **not** the operator above (D43, which post-dates this row's own D32): a comparison against the `null` literal is the CORE's own raw-pointer check against zero, on any reference-shaped operand — `operator==` is never even a candidate, so `string` needs no clause for it | `i64` 0/1 |
| `a < b` and the other three | not declared, so not taught: `.CompareTo` is the form | — |
| `string + i64`, `string + f64`, `string + Foo` | refused | — |

**Every row above is a mechanism that already runs.** An operator is a `public static`
member resolved over both operands (D8, [classes.md](../reference/classes.md) § Operators),
`tk_ops_pass` lowers it, and `tk_rc_pass` sees a call returning a counted row and parks the
temporary. There is no new operator machinery on this page at all.

`"n=" + 5` is legal C# and is **refused** here. It needs a `ToString` on every type, which
needs a root type, which needs `object`, which needs boxing — § 11. `"n=" +
tk_num(n)` is the road today and `$"n={n}"` is the road when § 9 unblocks.

## 9. `$"..."` needs `mc` 0.15.25, where a module claims `$` — and the pin is past it

C#'s interpolation is `$"a{x}b"`. In `mc` up to 0.15.24 the lexer could not hand it over:
`$` opens a **hole** token — `$1`, `$name`, `$$name`, the machinery `#rule` substitution
uses — and `$` followed by anything that is not a digit or a letter was `invalid hole`,
raised inside `lex_next` and therefore before any hook could see the token. Measured on the
tree at the 0.15.23 pin: `puts($"hi")` is `invalid hole` at the `$`.

**`mc` 0.15.25 made it pure surface.** When `$` is not a hole (not followed by a name, a
digit or `$`), the lexer falls through to the punctuation matcher, so a module that
registered `syntax_expr("$", …)` owns the token and reads the string literal after it with
`p_cp()`/`p_take_lit`; the three `#rule` forms are untouched, nothing in `mc`'s core changed.
N10 waited for one thing only: teko's pin reaching 0.15.25 (a `MC_VERSION` crumb proved by
the whole recipe, D29/D35/D37). **D64 raised the pin to 0.16.1, so that wait is over** —
N10 is now blocked by nothing but its own crumb, and this page's refusal (`teko: string
interpolation is not taught yet`) is reachable the day the module claims `$`. The
measurement above is kept as the record of what the 0.15.23 lexer did.

**Re-measured on N7b's own pin, and still `invalid hole` (D88).** `mc` 1.0.1, no module
registering `syntax_expr("$", …)`: `puts($"hi {n}")` dies at the `$` with `invalid hole`,
the exact pre-0.15.25 wording this section says the pin left behind. This CONTRADICTS the
paragraph above's own claim that the wait ended at 0.16.1 — N7b did not chase the
contradiction down (out of its own boundary: N7b interns a literal into a `string` slot,
it does not claim `$`), and did not add the `syntax_expr("$", …)` registration either,
since doing so with the lexer still raising `invalid hole` first would prove nothing and
would move `mc limits`' `syntax` row for no reachable gain. Left for N10 to re-measure
first, with a minimal pure-`mc` reproducer if the contradiction holds (D2).

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
| `teko_params.tk` | one row in `tk_bracket`: a `string` receiver lowers to `tk_string_at` (§ 6; not `teko_array.tk` — corrected here) |
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

| fixture | asserts | `expect-exit` | crumb |
|---|---|---|---|
| `tests/surface_string_interop.tk` | `new string(raw)` from a `str`, proving it copies by mutating the source afterwards; `.ToString()`; `.Length`/`.Utf8Length` on ASCII and on a multi-byte string; `string.Empty`; `IsNullOrEmpty` over a `string` and a `string?` | `42` | N7a, landed |
| `tests/surface_string_concat.tk` | `+` on heap strings, chained three deep, `string.Concat`; `Equals`/`CompareTo`/`==`/`!=`, the prefix case and the empty string | `42` | N7a, landed |
| `tests/surface_string_rc.tk` | `rt_live()` back to its floor after a block's own strings drop, churned in a loop; a `string` held in a class FIELD surviving the block that built it; `GetHashCode` equal for equal values | `42` | N7a, landed |
| `tests/surface_string_value.tk` | a literal in every one of the nine slots; `==` and `!=` by value against a second literal with the same bytes and a different one; `null` in and out of a `string?` slot | `42` | N7b |
| `tests/surface_string_intern.tk` | `rt_live()` and `rt_used()` do **not** move across a hundred uses of the same literal; two occurrences of `"hi"` are the same address; a literal survives being assigned and released in a loop | `42` | N7b |
| `tests/surface_string_index.tk` | `s[i]` on ASCII and on a multi-byte string; `s[Length - 1]` | `42` | N8 |
| `tests/surface_string_index_oob.tk` | `s[Length]` | `70` | N8 |
| `tests/surface_string_methods.tk` | `Substring`, `IndexOf`, `LastIndexOf`, `Contains`, `StartsWith`, `EndsWith`, `Trim`, `Replace`, `PadLeft`, `ToUpper`, `Split(…).Length` and the elements it produced | `42` | N8 |

D52's own harness, the six refuse fixtures N7a's class already makes reachable — every one
the GENERIC row a class already answers with, no new code:

| fixture | `expect-refuse` |
|---|---|
| `tests/refuse/string_to_numeric.tk` | `teko: a value of type string does not convert to i64` (D34) |
| `tests/refuse/numeric_to_string.tk` | `teko: a value of type i64 does not convert to string` (D34) |
| `tests/refuse/string_str_implicit.tk` | `teko: a value of type uptr does not convert to string` — a `str`, literal or not, until N7b's conversion lands |
| `tests/refuse/string_plus_mismatched.tk` | ``teko: no operator `+` takes these operands`` |
| `tests/refuse/string_unknown_member.tk` | `teko: unknown member of string: Lenght` — the mechanism's own wording, not this page's `` string has no member Lenght `` prose |
| `tests/refuse/string_null_nonnullable.tk` | `teko: null needs a slot declared string?` (D43, superseding this page's own `string e = null;` sample under D32) |

The named `` `string` needs #include "string.tk" `` refusal (§ 5's own table) is **not**
reachable yet: N7a teaches no such check, so `string` with no include is a plain parse
error today — N7b's own fixture, once `teko_string.tk` exists to raise it.

## 15. The crumbs

N7 splits into two, on the D81→D83/D84/D85 precedent (`i128`/`u128` landed the same way, one
crumb for the type and one for the rest): N7a is the class and its members, self-contained
and provable with no other half of this page built yet; N7b is everything that reaches INTO
the class from outside it — a literal, an implicit conversion, the include check.

### N7a — the class and its members (L) — **landed, D86**

`lib/string.tk`'s class with `nbytes`/`nchars`/`data`/`owned`, the constructor (`new
string(raw)` from a `str`, rule 4 — the road N7a builds an instance with — and `string()`,
the no-argument form, which is the EMPTY string rather than the zeroed object teko's
allocator would otherwise hand out, D86) and destructor, `.Length`/`.Utf8Length`/`.ToString()`/`.Equals`/`.CompareTo`/`.GetHashCode()`,
`operator+` and `operator==`/`!=`, `string.Empty`/`Concat`/`IsNullOrEmpty`. **Depends on
nothing** — not on `docs/specs/decimal.md`, not on `docs/specs/datetime.md`, not on `enum`.

**What N7a did NOT build**, left to N7b (landed below, D88): the literal interning of § 4
and the two implicit conversions of § 3 in D33's nine slots — both now in place — and the
`` `string` needs #include "string.tk" `` named refusal, which N7b measured NOT reachable
(D48's own limit, § 5's row corrected rather than built).

**Gate, measured:** `surface_string_interop.tk`, `_concat.tk` and `_rc.tk` at `42`, plus six
`tests/refuse/` fixtures for every refusal reachable through the class alone
(`string_to_numeric.tk`, `numeric_to_string.tk`, `string_str_implicit.tk`,
`string_plus_mismatched.tk`, `string_unknown_member.tk`, `string_null_nonnullable.tk`) —
every one of them the GENERIC row a class already answers with, no new code; every base
fixture at its `expect-exit` with `--dump-ast --include=lib --include=tests`
**byte-identical**, since no base fixture includes `lib/string.tk`; `FIXPOINT OK`; `mc
limits` verdict `ok` with `passes`, `syntax`, `alias`, `types`, `intrin` and `on_stmt` **not
moved** — `lib/string.tk` is program code, outside `[compiler].modules`, so the compiler's
own build never parses it; `sh scripts/check-docs.sh` green. **Owed and landed:** a
`string` section in [types.md](../reference/types.md), a NEW `string` section in
[not-yet.md](../reference/not-yet.md) naming N7b's and N8's own gaps, `lib/string.tk` in
[runtime.md](../reference/runtime.md), the module count in [`CLAUDE.md`](../../CLAUDE.md),
this page's own N7a/N7b split and its three `teko_array.tk` → `teko_params.tk`
corrections, and `DECISION_LOG.md` D86. **Not landed, out of N7a's own scope:** the
borrowed-pointer lifetime rule in [memory.md](../reference/memory.md) (N7b's, once the
`string` → `str` conversion exists to make a borrowed pointer reachable at all) and
[guide/10-values-and-types.md](../guide/10-values-and-types.md) (a guide page reads best
once N7b's literal makes `string s = "hi";` legal, rather than teaching `new string(raw)`
first and rewriting the page a crumb later).

**Two adjacent findings, neither this crumb's compiler code to fix, both routed around in
`lib/string.tk` by naming rather than worked around:** `teko_class.tk`'s `tk_new_fn` (the
allocator wrapper every `new ClassName(...)` lowers through) declares a local literally
named `p` in the same scope that clones the constructor's own parameter list to build the
call, so a constructor parameter ALSO spelled `p` is silently shadowed and the caller's
argument is lost — general to every class, not to `string`, measured with `Cell(i64 p){v=p;}`
against `Cell(i64 x){v=x;}`. And a bare `this` used as a VALUE (not as `this.field`'s
receiver) types as `uptr` rather than as the enclosing class — `teko_class.tk`'s own `this`
parameter is declared `param_new(TY_UPTR, ...)` and no bare-`this` node is ever re-typed
against it — so `.ToString()` returns a fresh copy of the same bytes rather than `this`.

### N7b — the literal, the conversions, the `#include` check (L) — **landed, D88**

`teko_string.tk` (new module): the literal interning of § 4 (an `N_STR` in a `string` slot
becomes an `N_IDENT` naming a module-private, gensym'd global, deduped by value), the two
implicit conversions of § 3 in the nine slots `tk_num_widen`'s own siblings reach, and the
measurement of the named `#include` refusal against D48's own limit (§ 5 above, this
page's own row corrected rather than the message built — it is not reachable, and D48
already proved why for the identical shape, `DateTimeKind`). Depends on N7a.

**`passes` did NOT move**, against this page's own forecast: every one of the nine slots'
conversions rides a CALL already made from inside an EXISTING pass (`teko_rc.tk`'s own
pass, `teko_typeof.tk`'s deferred judge that already rides `tk_over_pass`, `teko_ops.tk`'s
`tk_ops_pass`, `teko_null.tk`'s `??` lowering inside `tk_tern_scan`'s walk,
`teko_params.tk`'s pack), so `teko_string.tk` registers no pass of its own — the forecast
above assumed a module means a pass, and this one does not.

**A regression the gate caught, and the general rule it left behind.** `tk_rc_call_args`
(teko_rc.tk) walks EVERY `N_CALL` of every function body by parameter INDEX, generated
bodies and generated plumbing calls included — a method's own RECEIVER at parameter 0
(declared `uptr`, `this`), and `lib/rt.tk`'s reference-management primitives
(`rt_own`/`rt_store`/`rt_store_own`/`rt_drop`/`rt_park`/`rt_free`/`rc_inc`/`rc_dec`, every
one of them taking its argument as a `uptr` GENERICALLY). A `string`-typed value crossing
either one matched `tk_str_borrows` (`uptr` beside `string`) before this crumb excluded
them by name, and read `.data` off the receiver or the plumbing's own argument instead of
keeping the object reference — `a.Length` on a `string a` and `s = v;= string` field store
both measured broken (a wrong `.Length`, then a bus error) before the two exclusions
landed. Neither is a new mechanism of its own; both are the same rule stated once: the
`str`/`ptr`/`uptr` conversion is never a GENERIC pointer sink's business, only a genuinely
typed `str` parameter's.

**Two measured gaps outside D33's nine slots, left as `not-yet.md` rows rather than
chased:** an OVERLOADED free/method call still fails to match a literal against a
`string` parameter (`tk_ov_args_fit`, `teko_over.tk` — overload SELECTION, a different
question from the nine slots' own conversion; a name declared once, `greet(string who)` of
§ 5's own sample, already works); and `c ? "yes" : s` still refuses (`teko_ternary.tk`'s
`tk_tern_lower` needs its two arms' types already equal, for any type, `string` included —
`??`, a different mechanism, is this crumb's own and closed).

**`$"…"` re-measured on the N7b pin (`mc` 1.0.1) and still `invalid hole`**, contradicting
this page's own § 9 claim that the wait ended at `mc` 0.16.1 (D64) — recorded there rather
than chased, since claiming `$` needs a `syntax_expr` registration this crumb has no
reachable use for while the lexer still raises `invalid hole` first, and diagnosing why is
N10's own first step.

**Gate** (`mc` 1.0.1, macos/aarch64): `mc build . --config mc.macos.toml` clean;
`sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **129 passed, 143 refused as
expected, 0 failed** (125/141 on the base: `surface_string_value.tk`, `_intern.tk`,
`_capture.tk` and `_op_str.tk` added, plus `refuse/string_user_class.tk` and
`refuse/string_user_class_wide.tk`; `string_str_implicit.tk`'s own comment corrected — the
refusal itself unmoved, a NON-literal `str` still does not fit); `--dump-ast --include=lib
--include=tests` **byte-identical** on all 125 pre-existing fixtures against the base
compiler, N7a's own five `string`-including ones among them — none of them writes a LITERAL
into a `string` slot, so none of their dumps could move by construction; `sh
scripts/bootstrap.sh --os macos --arch aarch64` → `FIXPOINT OK`; `sh scripts/check-docs.sh`
→ `docs ok: 703 links, 78 fragments, 412 diagnostics, 143 refusals, 154 samples, manifest
listed`; `mc build . --config mc.macos.toml --limits` (`rm -rf build` first) — the
`build/teko.mc` leg verdict `ok`, and on the `tests/hello.tk` leg, the one that reads the
TAUGHT compiler's own registrations, `passes`(15)/`syntax`(20)/`alias`(25)/`types`(18)/
`intrin`(8)/`on_stmt`(4) every one exactly where the base leaves them (that leg's verdict
is `grew` on the base too, unmoved). **Owes, left
out of this crumb's own boundary:** the borrowed-pointer lifetime rule in
[memory.md](../reference/memory.md) and
[guide/10-values-and-types.md](../guide/10-values-and-types.md) — neither named by this
crumb's own dispatch, both still true and both N8-adjacent.

### N8 — the methods, and the index (L) — landed, D89

`s[i]` in `tk_bracket` (`teko_params.tk`) with its guard; `Substring`, `IndexOf`,
`LastIndexOf`, `Contains`, `StartsWith`, `EndsWith`, `Trim*`, `Replace`, `Pad*`,
`ToUpper`/`ToLower`, `Split`, `Join`. Depends on N7a and N7b.

**Landed as designed, with one divergence D89 records.** `s[i]` lowers to a PLAIN
top-level `tk_string_at(s, i)` (`lib/string.tk`), not a method call, exactly as this
section already named — a targeted row in `tk_bracket`, and the only compiler change
this crumb makes. Every other member is an ordinary class method or static, zero
compiler work. `.IndexOf(char)` is spelled `IndexOfChar` instead of a second `IndexOf`
overload: a class method is resolved by name-and-ARITY alone (`tk_method_pick`,
`teko_class.tk`), unlike a free function's `teko_over.tk`, so a second one-argument
`IndexOf` is genuinely ambiguous today (`teko: ambiguous overload; two signatures take
this many arguments: IndexOf`, measured) — extending method dispatch to read argument
types is real machinery this crumb's own module table does not authorize, so the two
searches keep their own names (D89).

**Gate:** `surface_string_index.tk` and `_methods.tk` at `42`, `_index_oob.tk` at `70`;
the write refusal (`tests/refuse/string_index_write.tk`, `teko: a string is immutable`);
everything N7a/N7b gated on. Proof, mc 1.0.1, macos/aarch64: `mc build . --config
mc.macos.toml` clean; `sh scripts/fixtures.sh ./build/teko mc.macos.toml` → **132
passed, 144 refused as expected, 0 failed**; `sh scripts/bootstrap.sh --os macos --arch
aarch64` → `FIXPOINT OK`; `sh scripts/check-docs.sh` → `docs ok`; `mc limits`
`passes`/`syntax`/`alias`/`types`/`on_stmt`/`intrin` unmoved on both the compiler leg and
the `tests/hello.tk` leg; `--dump-ast --include=lib --include=tests` byte-identical over
all 257 base fixtures (122 `tests/*.tk` + 135 `tests/refuse/*.tk`) that do not include
`string.tk`. **Owed:** runtime.md, the index guard in [arrays.md](../reference/arrays.md),
and the `this[i]` row in [not-yet.md](../reference/not-yet.md) — all landed alongside
this entry.

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
