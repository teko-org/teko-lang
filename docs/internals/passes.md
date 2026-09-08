# The pass model

`mc` parses a unit, then runs every `pass()` a module registered, in registration order,
over the whole tree. Teko registers **fifteen** — `mc limits` reports `passes 15/30` — and
the order they are registered in, at the bottom of `teko_init()`
([`teko.tk`](../../teko.tk)), is the order they run in.

Most of teko is **not** a pass. `while`, `for`, `foreach`, the `switch` statement, `new`,
`.` on a receiver the parser can type, a class body, a trait flattening and a generic
instance are all lowered while the source is being read. A pass exists only where the
answer does not exist yet at parse time — because it is a question about the whole unit, or
about a static type a deferred access does not carry.

## The fifteen, in order

| # | pass | in | does |
|---|---|---|---|
| 1 | `tk_partial_pass` | `teko_class.tk` | closes a `partial class` no use ever closed, emitting its vtable, release, initializer and allocators |
| 2 | `tk_ns_pass` | `teko_ns.tk` | mangles every namespaced free function and prototype, and resolves every bare call written inside a namespace or under a `using` |
| 3 | `tk_fwd_pass` | `teko_fwd.tk` | a name the pre-scan reserved and no declaration ever adopted is `is used but never declared`; then resolves the `new` and `Type.member` sites deferred against a type declared below |
| 4 | `tk_di_pass` | `teko_di.tk` | resolves every `inject` placeholder into a getter call or a scope local, emits the memoized getters and prepends the locals to their blocks |
| 5 | `tk_array_pass` | `teko_array.tk` | rewrites an index into a **global** fixed array, which parse time could not see |
| 6 | `tk_typeof_pass` | `teko_typeof.tk` | the oracle: rewrites every deferred `.` into the load, store or call it stands for, now that the whole unit can be asked about |
| 7 | `tk_ref_pass` | `teko_ref.tk` | a `ref`/`out` parameter reads and writes through its pointer |
| 8 | `tk_deleg_pass` | `teko_deleg.tk` | a call on a delegate-typed name becomes a typed `callp` through the object's code pointer |
| 9 | `tk_ternary_pass` | `teko_ternary.tk` | the `?:` placeholder becomes a local plus an `if`, hoisted above the statement that used it — and, from inside the same walk, so do the `??` and `?.` placeholders (`teko_null.tk`, D45), which is why neither operator registers a pass |
| 10 | `tk_switch_guard_pass` | `teko_switch.tk` | checks the level a bare `continue` reaches once every `N_LOOP` is in the tree |
| 11 | `tk_ops_pass` | `teko_ops.tk` | an operator over operands of declared type becomes the call to the static member that declares it |
| 12 | `tk_params_pass` | `teko_params.tk` | builds the `T[]` of a `params` call site out of its arguments, for a name declared once, and refuses an index nothing resolved |
| 13 | `tk_default_pass` | `teko_default.tk` | fills the missing trailing arguments of a name declared exactly once |
| 14 | `tk_over_pass` | `teko_over.tk` | gives each overload of one name its own symbol and rewrites every call site, defaults and `params` lists included |
| 15 | `tk_rc_pass` | `teko_rc.tk` | injects the reference counting over every body: owning stores, releases at `}`, at a jump and at `return`, and the parking of temporaries |

## Why that order

Read as constraints rather than as a list, the order is forced almost everywhere.

**Declarations first.** Pass 1 closes a partial class, and what it emits are ordinary
declarations every pass below has to see. Pass 2 gives every namespaced declaration its
FINAL symbol, because passes 12, 13 and 14 all census the unit **by name** and would
otherwise census the wrong one. Pass 3 is the backstop for the pre-scan, placed before
anything censuses by name for the same reason.

**Pass 5 before pass 12.** A global array's index is the one `N_INDEX` that survives the
parse legitimately, and pass 12 refuses every one still standing when it runs. Nothing
stronger ties the two: since `params` became a `T[]` modifier, `xs[i]` on a list resolves at
parse time like any other array, and pass 12 does not walk an index at all.

**Pass 6 in the middle.** Passes 7 to 15 all ask about a static type, and a `.` the parser
could not type carries none until the oracle has rewritten it. Passes 7 and 8 sit right
behind it and ahead of the rest, so a body reaching passes 9 to 15 has no pointer parameter
and no delegate call left in raw form.

**Pass 12 behind the oracle, `ref`, the delegates and the operators.** Those are what give
an argument its type, and a `params` call site has to type every argument it packs: a
delegate call is only a typed `callp` once pass 8 has run, an overloaded operator only a
call once pass 11 has. It runs **ahead** of 13 and 14, which census by arity — the call has
to already carry its single argument — and ahead of 15, which is what parks the array.

**Pass 10 before pass 15.** The reference-counting pass relocates a `break`/`continue` it
wraps in a release block to a **fresh node index**, and the guard pass looks a jump up under
the index it was recorded at.

**Pass 11 before pass 14.** The call `tk_ops_pass` puts in the tree already names the
member's own symbol, so a mangling pass has nothing left to pick there.

**Pass 13 before pass 14, and only for a name declared once.** A name declared more than
once is left untouched by the default fill on purpose; the overload resolution has a round
of its own that handles those, defaults included.

**Pass 12 too, and for the same reason.** A `params` list whose name carries a second
signature is left as written: which candidate a site means is a question about all of them
at once. Pass 14's fifth round answers it and calls pass 12's own expansion back, so the
chain and its ownership are built in one place either way.

**Pass 15 last.** Ownership is a question about a value's static type: the deferred accesses
have to be resolved, the operators have to be the calls they are, and the overloads have to
carry their own symbols — two functions spelled the same are one name until pass 14, and
only one of them may answer with an object.

## What a pass may emit

Four of these passes add a top-level declaration: pass 4 (a service's memoized getter and
its slot), pass 12 (the allocator, release and element store of a `T[]` row a call site is
the first to build), pass 1 (everything a class close emits) and pass 14 (nothing new, but
it renames). Both run after the parse, where `p_decl_name()` is already 0 and nobody reads
it any more, so pass 4's bare `top_add` is safe; pass 12 goes through `tk_top_emit` anyway,
because the three declarations it asks for are the same ones `new T[n]` asks for **during**
the parse, from one shared `tk_ha_ensure_put`.

Pass 1 is not, and neither is anything a handler emits **during** the parse. `top_add`
clears `p_decl_name()` as a side effect, and teko generates declarations from expressions —
a thunk at `new Op(fn)`, the vtable and release of a class that closes at its first use,
`tk_ix` at an array-field index, a whole generic instance at a typed declaration. Each of
those fires while a declaration of the program's own is still being read, and two module
tables are keyed by exactly that name. So:

> **Every top-level declaration teko emits from inside a body goes through
> `tk_top_emit`** (`teko_struct.tk`), which saves `p_decl_name()`, calls `top_add` and puts
> the name back. A generator that re-parses whole declarations saves and restores the name
> with the rest of its scratch instead, because the core writes it itself once per
> declaration the replay produces.

The mirror of the same invariant: a handler that owns a declaration and reads its body
**itself**, rather than letting the core read it, has to say whose the statements are. An
arrow accessor (`get => e;`) is such a handler, and calls `p_set_decl_name` for that reason.
A generated declaration is invisible to the parse it interrupts, or it is a defect.

## Adding a pass

`mc limits` is the budget: `passes 15/30` today. A new pass is registered in
`teko_init()` at the position its constraints force, with a comment saying which constraint
that is — every registration there carries one. A pass that only **moves** an existing one
must not change the count; a delta in `passes` where none was intended is a design error,
not a rounding.
