# The module map

Thirty-one files: [`teko.tk`](../../teko.tk) and the thirty `teko_*.tk` modules it
`#include`s. The order of those includes is not cosmetic — a module may forward-declare a
**function** a later module defines, but a **global** has to already exist where it is
read, so a file that touches another's tables is included after it. `teko_type.tk` opens
the list because its words have to be reserved before anything else registers, and
`teko_switch.tk` closes it because it consumes what `teko_loop.tk` and `teko_ternary.tk`
built.

`teko_init()` in `teko.tk` is the only place a registration happens; the modules define the
handlers and the tables. What each one registers is listed below in the include order.

| module | teaches | registers | keeps |
|---|---|---|---|
| `teko_type.tk` | `bool` `char` `byte` `isize` `usize` `ptr` `str`, `true`/`false`/`null` | seven `type_alias` over core ids, one `type_new` for `params` | — |
| `teko_prefix.tk` | `!b[1]`, `-a.x`, `~g[2]` read as C# reads them | nothing: it is a helper `tk_dot`/`tk_bracket` call | — |
| `teko_float.tk` | `f32`/`f64` | calls `float_init()` and the two float machines of mc's `<float>` | — |
| `teko_struct.tk` | the type table itself, and `struct` | `syntax("struct")`, `on_stmt` for a local of struct type, one `type_new` per declared type | types, fields, locals in scope, the node table, array-field addresses, counted stores |
| `teko_array.tk` | `T a[N]` local and global, `a[i]`, `a.Length` | `on_stmt` for the `N_VAR`/`N_GLOBAL` the core builds | local arrays per scope, global arrays, deferred writes |
| `teko_const.tk` | `const i64 N = 10` at top level and as a member | `syntax("const")` | folded constants, global and per type |
| `teko_ns.tk` | `namespace A.B`, file-scoped `namespace A.B;`, `using`, `import`, qualified names | `syntax` for the three words, plus `syntax_stmt`/`syntax_expr` per namespace segment and per short type name | namespaces, segments, short names, `using` directives, functions read inside a block namespace |
| `teko_fwd.tk` | free order of declaration for a type name | `source_claim` and `on_source` — the two lexer callbacks, both registered before any word is reserved | the names the pre-scan found |
| `teko_ref.tk` | `ref T` / `out T` parameters and arguments | two `type_new`, `on_stmt`, `syntax_expr("ref")`/`("out")` | the pointee of every `ref`/`out` parameter and argument |
| `teko_iface.tk` | `interface`, its default bodies, its static signatures | `syntax("interface")` | interface methods, `(class, interface)` pairs, one conformance list per class |
| `teko_trait.tk` | `trait` and `use A, B;` — PHP's flattening | `syntax("trait")` | traits, queued `use` entries, flattening depth |
| `teko_generic.tk` | `class Box<T, const N: i64>` by record and replay | `syntax_stmt` per generic name | generics, their parameters, the recorded parts, the instances made |
| `teko_class.tk` | `class`, the vtable, `virtual`/`override`, constructors, destructors, `abstract`, `partial` | `syntax("class")`, and the honest stop for `type` | methods, virtual slots, constructors, default arguments |
| `teko_di.tk` | the three lifetime markers, `inject`, `scope { }` | `syntax_expr("inject")`, `syntax_stmt("scope")` | services, `inject` sites, open scopes, the cycle stack, per-scope locals |
| `teko_prop.tk` | `T Name { get; set; }`, arrow accessors, `value`, per-accessor visibility | nothing of its own: read by `teko_class.tk`'s member reader | properties, summed over all types |
| `teko_this.tk` | the receiver a method does not declare, `this`, `base.m()` | `syntax_expr("this")` | which type's body is open, and whether it is static |
| `teko_access.tk` | `public` `private` `protected` `internal` `static` `abstract` `partial`, and `Type.member` | `syntax` for the four leading words, and `syntax_expr`/`syntax_stmt` per type name | the project root `internal` is measured from, deferred static accesses |
| `teko_typeof.tk` | nothing of the surface: it is the static-type oracle | one `pass()` | the names of one function, and the accesses waiting for the pass |
| `teko_deleg.tk` | `delegate`, contextual and explicit values, lambdas, `use (...)` | `syntax("delegate")`, `on_stmt` for the capture taint | `(delegate, function)` thunk pairs, capture lists, by-reference lambdas |
| `teko_heaparr.tk` | `T[]` on the heap, `new T[n]`, its guarded index | `syntax_type` — the one in this compiler | one type row per element type, global `T[]` declarations |
| `teko_ternary.tk` | `c ? a : b` | `syntax_infix("?")` | the placeholder calls the pass rewrites |
| `teko_stmt.tk` | every `{` block, and the honest stops for `var`, `match`, `when`, a local `const` | `syntax_stmt("{")` and one per stopped word | — |
| `teko_expr.tk` | `new`, `.` as field/method/property access | `syntax_expr("new")`, `syntax_infix(".")` | forward-deferred `new` sites |
| `teko_params.tk` | `params`, instantiated once per argument count | `syntax_infix("[")` | the functions declared with a list, and their instances |
| `teko_default.tk` | `i64 add(i64 a, i64 b = 10)` | `syntax_param` — the one in this compiler | one row per free declaration with a parameter list |
| `teko_over.tk` | overload of a top-level function by signature | nothing: one `pass()` | every declaration of the unit, and the names declared more than once |
| `teko_ops.tk` | `operator+` and its siblings as static members, and the unary `+` | `syntax_expr("+")` | the operators each type declares |
| `teko_rc.tk` | nothing of the surface: reference counting injected over every body | one `pass()` | the loops open at one point of one function |
| `teko_loop.tk` | `while`, `do`, `for`, `foreach`, and `++`/`--`/`+=`/`-=` | four `syntax_stmt`, and `word_add` for the lexemes the rewrite compares against | — |
| `teko_switch.tk` | both `switch` spellings, `case`, `when` guards, `default` | `syntax_stmt("switch")`, `syntax_infix("switch")` | the jump levels a guard has to check after the parse |

`teko.tk` itself teaches nothing. It holds the includes, `teko_init()`, and the two
subcommand handlers `core_teko.mc` registers: `tk_build`, which prefers `DIR/teko.toml`
over mc's own `DIR/mc.toml` default before delegating to the driver unchanged, and
`tk_limits`, which recognises a `.tk` single file the core's own check would not.

## Reading a module

Every module carries a header comment that states the surface it teaches, what the
generated code looks like, and the reason for the shape it has. Those headers are the
account of record; the pages here summarise them and do not replace them.

Three shapes recur:

- **parse-time lowering.** `while`, `do`, `for`, `foreach` and the `switch` statement build
  the core's own `loop`/`if`/`break N` while the source is being read, so nothing later has
  to know they existed.
- **a deferred placeholder.** A `.` on a receiver the parser cannot type, and a ternary, are
  parsed into a call to a name nothing declares; a `pass()` rewrites it. If the pass were
  ever not registered, the core's resolver would refuse the call outright rather than
  compile something wrong.
- **generated declarations.** A vtable, a release function, a thunk, a `params` instance and
  a whole generic instance are declarations teko emits itself. Every one that can fire in
  the middle of a declaration of the program's own goes through `tk_top_emit`
  ([nodes-and-xt.md](nodes-and-xt.md)).

## The tables

Each module owns fixed-size global arrays with a `TK_MAX*` cap and a one-line comment
saying what the cap counts. They are sized against a **measured** count over this tree, not
by guess: the self-hosted unit is around eight and a half thousand lines of core plus the
modules, which is what raised six of them past their original fixture-sized values
([pitfalls.md](pitfalls.md)). Overflowing one is an error with its own message, never
silent corruption.
