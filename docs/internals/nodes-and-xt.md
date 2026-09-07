# Nodes, `tk_xt`, and rewriting in place

`mc` hands a module the AST as **node numbers**. A node has a kind, a name, a type, two
children and a `nd_next` that threads it into a sibling list; there is no room in it for
what teko needs to remember about it. So teko keeps side tables keyed by the node number
itself, and every rule on this page follows from that one decision.

## The node table

`tk_xt` ([`teko_struct.tk`](../../teko_struct.tk)) is four parallel arrays plus a count:

| column | holds |
|---|---|
| `xt_node` | the node number |
| `xt_str` | the row of the type table, or `-1` for a scalar |
| `xt_ty` | the type id itself, scalars included |
| `xt_pure` | 1 when re-evaluating the node is free of effects |

Two writers, `tk_xt_put(n, si, ty, pure)` and `tk_xt_add(n, si, pure)` — the second reads
the type id off the row. Three readers: `tk_xt_find(n)` for the row, `tk_xt_ty(n)` for the
type id, and `tk_pure(n)`.

The lookup `tk_xt_at` walks **backwards** from the newest entry, so a later registration on
the same node wins. There is no removal: a node that has been rewritten simply stops being
asked about.

Recording `xt_ty` separately from `xt_str` is what lets a **scalar** answer. A field of
`i64` is no row of the type table, and answering `-1` for it once left `this.side`
untypable to everyone downstream, the overload resolution included. `-1` in `xt_str` means
"not a struct"; no entry at all means "nothing known", and those are different answers.

`xt_pure` is what makes `new C().m()` a refusal instead of two allocations: an `N_IDENT` is
pure by definition and a field load this module built is registered pure, while a
constructor and a method call answer 0, so a rewrite that would evaluate the receiver twice
declines to.

Three sibling tables use the same key: `ax_*` for the address of an inline array field
(with its element type, its count and its name, so an out-of-range constant index has a
message that names the field), `os_*` for a store into a slot of counted type, marked at
parse time and read by the reference-counting pass, and `slv_*` for every local the core
declared, whatever its type.

## `node_assign` copies `nd_next`

An in-place rewrite is `node_assign(n, replacement)`: the parent keeps pointing at the same
number, so the tree above it does not have to be touched. But `node_assign` copies the
**whole** node, `nd_next` included — and `n` may be one argument of several, threaded
through exactly that field. Overwriting it with the replacement's `nd_next` truncates the
list silently.

Every in-place rewrite therefore goes through a two-line helper that saves the field and
puts it back — `tk_ref_replace` in [`teko_ref.tk`](../../teko_ref.tk), `tk_node_replace` in
[`teko_this.tk`](../../teko_this.tk):

```mc
void tk_node_replace(i64 n, i64 r) {
    i64 keep = nd_next(n);
    node_assign(n, r);
    set_nd_next(n, keep);
}
```

A bare `node_assign` is correct only for a leaf that is known to stand alone.

## The registration goes on the FINAL node

A shaper may wrap the node it was asked to build. The clearest case is the return type of
an indirect call: the core reads a cast **directly** on a `callp` as the declaration of what
the call returns, and any node inserted between the two undoes the match **in silence** — it
compiles, it runs, and it answers with the wrong register file. `tk_callp_ret`
([`teko_array.tk`](../../teko_array.tk)) is where that envelope is put on, at the site that
builds the call and never in a later pass.

The consequence is the rule:

> Every registration by node position — `tk_xt_put`, `tk_xt_add`, and the `node_assign` or
> `tk_node_replace` that copies a result into the tree — is about the node the shaper
> **returns**, never about the inner node it wrapped.

Registering on the inner `callp` reads as correct until a counted return value goes through
the shaper, and then the count is read under a node nothing points at any more.

## Generated declarations keep the parse honest

`top_add` clears `p_decl_name()` as a side effect, and teko generates declarations from
expressions: a delegate thunk at `new Op(fn)`, the vtable and release of a class that closes
at its first use, `tk_ix` at an array-field index, three functions at `new T[n]`, a whole
re-parsed declaration at a generic instantiation. Every one of those fires while a
declaration of the program's own is still being read, and two module tables are keyed by
that name — the escape check for a by-reference capture, and the row a parameter default
opens.

So a generated top-level declaration goes through `tk_top_emit`
([`teko_struct.tk`](../../teko_struct.tk)):

```mc
void tk_top_emit(i64 n) {
    uptr owner = p_decl_name();
    top_add(n);
    p_set_decl_name(owner);
}
```

Restoring **once, at the end** is not enough when several `top_add` calls follow one
another: each of them clears the name again. Either every call goes through `tk_top_emit`,
or the whole group is bracketed by one save and one restore — which is what the generic
replay does, because the core writes the name itself once per declaration the replay
produces.

A `top_add` that runs at top level (inside a class or namespace body the core is reading)
or from inside a `pass()` (where the parse is over and `p_decl_name()` is already 0) needs
none of this. [passes.md](passes.md) lists which are which.

## Positions

A node teko builds has no position of its own. `tk_line`/`tk_file`
([`teko_struct.tk`](../../teko_struct.tk)) carry the one the module was last at, and are
what a declaration-level error reports; a handler that has a real token in hand reports
`p_line()`/`p_file()` instead. `p_start()` is **not** usable after a hygienic substitution:
a generic instantiation replaces the token's text with a lexeme that lives in the arena, so
scanning forward from it reads garbage. A handler that has to peek past the current token
uses `p_cp()`, the lexer cursor — where the next token will be read from, in the same buffer
`p_src_end()` bounds ([`teko_access.tk`](../../teko_access.tk), telling `Shape.made = 1;`
from `Shape s = new Shape;` with one token of lookahead).
