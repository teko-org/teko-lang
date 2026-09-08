# The nullable probes (Q0)

[`docs/specs/nullable.md`](../specs/nullable.md) § 15 lists ten questions the `T?` design
answers from documentation and has to answer from **measurement** before a line of it is
written. This page is the measurement, on `0a37e491` plus a scratch module built for the
purpose and never committed — a `syntax_type` handler that reads a `?` suffix and answers a
bare `type_new` row (no teko row, no kind, no counting), plus two `syntax_infix`
registrations for `??` and `?.`.

Every probe is a program **outside `tests/`**, compiled with the single-file driver
(`./build/tekoprobe --include=lib FILE.tk -o OUT.o`) and, where it compiles, linked and
run. `mc` 0.15.23, macOS/aarch64.

**A probe that answers "no" is a finding.** Three did, and each one is carried in the row
that follows it: two changed the shape of Q1a's own handlers, and one is a position `T?`
does not reach and is recorded in [not-yet.md](../reference/not-yet.md) instead of worked
around.

---

## Probe 1 — does the `?` reach every declaration position?

`syntax_type` is consulted right after the core reads a type word, at the six places
`mc`'s [hooks documentation](https://github.com/minicompiler/mc) names. The question is
whether teko's **own** readers reach that position too.

| position | written | measured |
|---|---|---|
| a local | `Cell? c = null;` | **yes**, exit 42 |
| a field | `class Holder { public Cell? head; }` | **yes**, exit 42 |
| a parameter of a free function | `i64 use(Cell? c)` | **yes**, exit 42 |
| a return type of a free function | `Cell? none()` | **yes**, exit 42 |
| a method parameter and a method return | `class Box { public Cell? give() }` | **yes**, exit 42 |
| a constructor parameter | `public Node(Cell? x)` | **yes**, exit 42 |
| a cast | `Cell? c = (Cell?) p;` | **yes**, exit 42 |
| a global | `Cell? g;` | **yes**, exit 42 |
| an `extern` | `extern Cell? pick(Cell? c);` | **yes**, compiles |
| a `ref` parameter | `i64 fill(ref Cell? c)` | **yes**, exit 42 |
| an `out` parameter | `i64 give(out Cell? c)` | **yes**, exit 42 |
| a parameter default | `i64 use(Cell? c = null)` | **yes**, exit 42 |
| a property | `public Cell? Head { get; set; }` | **yes**, exit 42 |
| an interface row | `interface IPick { Cell? pick(Cell? c); }` | **yes**, exit 42 |
| a `struct` field | `struct Pair { public Cell? a; }` | **yes**, exit 42 |
| a `struct` local | `Vec? v = null;` | **yes**, exit 42 |
| a value type | `i64? n = 5;` | **yes** (the position); the box is Q1b's |
| `Cell?[]` | an array whose element is a nullable | **NO** — finding 1 |
| `Cell[]?` | a nullable array | **NO** — finding 2 |
| a delegate local | `Op? f = null;` | **NO** — finding 3 |
| a generic argument | `Box<Cell?> b` | **NO** — finding 4 |
| a namespaced short name, as a parameter or a return | `namespace geo { i64 use(Cell? c) }` | **NO** — finding 5 |

### Finding 1 — `Cell?[]`: `take_type` dispatches the chain once

`Cell?[] cs;` answered `name reserved by a syntax/type_alias registration: [`. The core
offers a type position to the handler chain **once**; the scratch handler consumed the `?`,
answered a row, and the `[` was left standing for `parse_var` to read as a name.

**What it changed:** `tk_nl_type` (teko_null.tk) reads the `?`, builds the row, and then
hands the rest of the position to `tk_ha_type` (teko_heaparr.tk) when a `[` follows. The
`[]` is read in exactly one place, as before.

### Finding 2 — `Cell[]?`: the same rule, from the other side

`Cell[]? xs;` answered `name reserved by a syntax/type_alias registration: ?`, for the
mirror reason: the array handler consumed `[]`, answered its row, and the `?` was left.

**What it changed:** `tk_ha_type` reads an optional `?` after `[]` and wraps its row with
`tk_nl_row`. Two lines in each of two handlers, which is what the spec's § 18 predicted.

### Finding 3 — `Op? f`: a delegate local has a reader of its own

`Op? f = null;` answered `name expected`. A delegate declaration in statement position does
not go through `parse_var` at all: `tk_type_stmt` (teko_access.tk) routes it to
`tk_deleg_var_stmt` (teko_deleg.tk), which is the reader that gives a lambda and the
contextual wrap of a plain function their look — and it reads the name straight after the
type word.

**What it changed:** `tk_deleg_var_stmt` reads an optional `?` itself and declares the local
at the nullable row, while the coercion still goes through the delegate row.
`tk_type_stmt` keeps routing there, except for `Op?[]`, which `tk_quest_bracket_follows()`
sends to `parse_var` the way `Op[]` already was.

### Finding 4 — `Box<Cell?>`: a type argument travels as a lexeme

`Box<Cell?> b;` answered `Box__Cell? instantiated from …: expected { in the class body`. A
generic argument is recorded as the **spelling** of the type, substituted into the
template's body and mangled into the instance's own declaration name — and `Cell?` is a
lexeme the lexer can never form, so the replay reads `class Box__Cell?` and stops on the
`?`.

**Not fixed, refused where it stands.** `teko: a nullable is not a generic argument yet`,
one line in `tk_gen_read_targ`, and a row in [not-yet.md](../reference/not-yet.md). Making
it work means teaching the generic replay a mangled spelling, which is a crumb of its own.

### Finding 5 — a namespaced short name never reaches the chain

Inside `namespace geo { … }`, `i64 use(Cell? c)` and `Cell? none()` both answered
`name expected`. The reason is **not** the `?`: `tk_ns_param_ty` (teko_ns.tk) resolves a
namespaced short type name and returns it **without calling `p_type()`**, so the
`syntax_type` chain never runs at that position at all.

**It is pre-existing and it is not this crumb's.** Measured on the stock `build/teko` of
`0a37e491`, with no probe module and no `?` anywhere:

| written, inside a namespace | stock `0a37e491` |
|---|---|
| `i64 use(Cell[] cs)` | `name expected` |
| `Cell[] make()` | `name expected` |
| `Cell[] cs = new Cell[2];` (a local) | ``teko: `[` needs an array`` |

`T[]` has had the same gap since K3. It is a row of
[not-yet.md](../reference/not-yet.md), not a workaround here, and not a defect on `mc`'s
side: the position is teko's own reader.

---

## Probe 1b — does the `null` literal reach the compatibility check at every slot?

Measured by instrumenting `tk_check_scalar_compat` (teko_typeof.tk) to refuse **every**
`null` it is handed, then compiling one program per slot. A slot that still compiled is a
slot the check never sees.

| slot | seen |
|---|---|
| an initializer, `Cell c = null;` | **yes** |
| an assignment, `c = null;` | **yes** |
| a `return null;` | **yes** |
| an argument of a free call | **yes** |
| an argument of a method call | **yes** |
| an argument of a virtual call | **yes** |
| an argument of an interface call | **yes** |
| an element of a `params Cell[]` | **yes** |
| a field store, `h.c = null;` | **yes** |
| a constructor argument, `new N(null)` | **yes** |
| a delegate initializer, `Op f = null;` | **yes** |
| a parameter default, `i64 use(Cell c = null)` | **yes** |
| an element of a `Cell[]`, `cs[0] = null;` | **no** — see below |
| a comparison, `c == null` | **no**, and that is required (§ 3) |
| a global left with no initializer | **no**, and that is required (§ 9) |

The array element is the one store the scalar check does not own: `tk_ha_store`
(teko_heaparr.tk) checks its value through `tk_check_field_store` (teko_struct.tk), which
returned at once for a `null`. Rule 1 is applied there instead, in that function's own
`null` arm — which is why `cs[0] = null` on a `Cell[]` is refused and on a `Cell?[]` is not.

---

## Probe 2 — does a second `syntax_type` handler coexist with the array's?

**Yes, and with no trace.** With the scratch handler registered behind `tk_ha_type` and
answering 0 for every type word that is not followed by `?`, the `--dump-ast` of **all 51
fixtures** is byte-identical to the one without it (`diff -rq`, no output). `i64[]`,
`Circle[]`, `Op[]` and `params T[]` all still compile and run. Handlers are tried in
registration order and the first non-zero answer wins, so the two never race: each declines
on a token the other owns.

---

## Probe 3 — does `??` lex as one token beside the ternary's `?`, and does `?.` beat `?` then `.`?

**Yes to both.** `syntax_infix("??", 1, …)` and `syntax_infix("?.", 12, …)` registered
beside the ternary's `syntax_infix("?", 1, …)`:

| written | measured |
|---|---|
| `i64 x = a ?? b;` | the `??` handler fires; exit 42 |
| `i64 x = k > 0 ? 42 : 1;` | the ternary is untouched; exit 42 |
| `return a?.abcd;` | the `?.` handler fires; exit 42 |
| `i64 x = k ? 42 : 1;` | a bare `?` followed by a space still reads as the ternary; exit 42 |

`mc`'s lexer takes the longest punctuation prefix, so `??` and `?.` win wherever the
characters are adjacent and `?` alone wins everywhere else.

---

## Probe 4 — does a precedence of 1 on `??` give the associativity § 5 claims?

**Yes, and the divergence § 5 names is real.** Both measured with a deliberately
non-associative `pick`, so the two readings differ in the exit code:

| written | reading | measured |
|---|---|---|
| `a ?? b ?? c` with `pick(x, y) = x - y`, `a=0 b=5 c=42` | right-associative is `0 - (5 - 42) = 37`; left is `-47` | **37** — right-associative |
| `a \|\| b ?? c` with `a=0 b=0 c=42` | C# reads `a \|\| (b ?? c)` → `1`; a table starting at 1 reads `(a \|\| b) ?? c` → `42` | **42** — the divergence § 5 records |
| `k > 0 ? 1 : b ?? c` with `k=0 b=0 c=42` | tied at 1 with the ternary: `k > 0 ? 1 : (b ?? c)` | **42**, which is C#'s own reading of that shape |

---

## Probe 5 — does `err_at` at the `?` report the declaration's own line?

**Yes.** Every refusal `tk_nl_type` raises is reported at the line the type word stands on:
`Cell?? c = null;` on line 3 of the probe answers
`…:3: teko: a nullable of a nullable is not taught`, and `void? f()` on line 2 answers
`…:2: teko: void? is not a type`.

---

## Probe 6 — what does `--dump-ast` print for a `type_new` name carrying `?`?

The name itself: `VAR type=Cell? name=c`. A `?` is safe in a type NAME for the same reason
`[`/`]` are — it is never a linker symbol. A generated symbol goes through
`tk_ty_mangle_name`, which answers `opt_Cell` for the row (and `arr_opt_Cell` for a
`Cell?[]`), the rule `arr_Cell` already lived by.

---

## Probe 7 — do `tk_ld`/`tk_stn` round-trip a payload at `+24`?

**Yes, for every width Q1b needs, with no cast a later refusal would mistake for a
hand-written one.** Measured through the construct that already stores at that offset: a
`T[]` of heap, whose elements begin at `+24` and are written and read with the very same
`tk_stn`/`tk_ld` pair a box would use.

| element type | written, read back | measured |
|---|---|---|
| `i8` | `-5` | round-trips, sign extended |
| `i16` | `-300` | round-trips, sign extended |
| `f64` | `2.5` | round-trips |
| `enum Color : u8` | `Color.Blue` | round-trips |
| `TimeSpan` | `TimeSpan.FromHours(2.0)`, `.Ticks` read back | round-trips |

Exit 42.

---

## Probe 8 — does a 32-byte block come back to the 32-byte free list?

**Yes.** A class with two `i64` fields is exactly 32 bytes (vtable, count, two words) — a
box's own size. Churned a million times in a loop, `rt_live()` ends at 0 and `rt_peak()`
never passes 64 bytes: one block is reused for the whole loop. Exit 42.

---

## Probe 9 — can teko_typeof.tk's walk see an assignment before the read it has to judge?

**Yes, from the walk's own construction**, and this is the one probe answered from the
source rather than from a program — Q3 is the crumb that owed the measurement.

**Measured (D46): yes, and the walk it rides is one of its OWN, in the same file and inside
the same registered `pass()`.** The construction below is right about what the tree carries;
what it is wrong about is that a single pre-order visitor can read it. Setting the bit when
an assignment is VISITED would vouch for that assignment's own right-hand side (`a = a + 1`
on an unassigned `a`), and a visitor that sees only one node at a time cannot tell an
`N_ASSIGN`'s target — which is not a read — from an `N_IDENT` that is. So `tk_da_walk`
recurses itself, with three clauses (a declaration, an assignment, an address) over the same
mark-per-`N_BLOCK` shape, and `passes` still does not move, which is what the probe was
asked.
`tk_ty_walk_list` (teko_typeof.tk) calls the visitor on every node of every function body
**in the order the source wrote it**, under the scope that holds at that node: a block
pushes a mark and pops it, and a local joins the scope only after the statement that
declares it has been walked. A bit per local, set when an assignment to it is visited and
read when a use of it is, is therefore answerable on that walk with no pass of its own.

---

## Probe 10 — what does `mc limits` say after one extra row is registered?

**`types` moves by exactly one per distinct `T?` spelled, and nothing else moves.** The
same program with and without the suffix, through `teko limits FILE.tk`:

| program | `types` used | `syntax_type` used |
|---|---|---|
| `Cell c = null;` | 12 | 2 |
| `Cell? c = null;` | 13 | 2 |

And the compiler itself, `mc limits . --config mc.macos.toml`, before and after Q1a:

| row | before | after |
|---|---|---|
| `types` | 11 | 11 |
| `alias` | 19 | 19 |
| `syntax` | 15 | 15 |
| `passes` | 15/30 | 15/30 |
| `intrin` | 8/16 | 8/16 |
| `syntax_type` | 1/8 | 2/8 |

The compiler registers no new primitive: a nullable row is a `type_new` made **while
compiling a program that spells `T?`**, and the compiler's own sources spell none. The one
row that moves is the registration count, which is what a second handler is.
