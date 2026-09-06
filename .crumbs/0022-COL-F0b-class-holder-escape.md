---
seq: 0022
crumb-id: COL-F0b
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:126-128"   # FASE 0 teaching item 3
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:88-98"     # GATE-1 conservative default
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md:67-91"       # §0.0 Correção A + owner gate §8
---

# 0022 · COL-F0b — class-holder escape (region-drop-via-escape, conservative GATE-1 default)

> Class-holder escape (region-drop-via-escape, conservative GATE-1 default) — a collection holding a
> `class` element raises that element's residence to the collection's region.

## Goal

Teach `src/checker/escape.tks` the CLASS-holder residence rule: a collection is a **holder that raises a
stored `class` element's residence LUB to the collection's region**, so the element is freed by region-drop
when the collection drops. This is the conservative, leak-safe, never-UAF default for the memory model's
`class` category (Doc-2 three-category law, `mudancas-superficie-0.3.1.md:1614-1637`): a plain `class` is
NOT refcounted (that is the `wrapped` kind only, COL-F0c) — it is region-dropped via escape-analysis. It
resolves the owner's open **GATE-1** (a `class` removed early from a long-lived collection: region-drop
vs. promote-to-wrapped) on the SAFE side — nothing is BLOCKED by GATE-1, only the class-early-remove
*eager-free optimization* is gated (an additive follow-up when GATE-1 closes toward promote-to-wrapped).
Purely ADDITIVE: it extends the residence LUB the escape pass already computes; no `src/` collection stores
a `class` element yet (FASE 1 is the first), so a `[dry]` build is byte-identical. Its seed folds into
SM-R1.

## Where

- `src/checker/escape.tks:35` — `mark_expr` — the residence/escape marking walk; extend it so a `class`
  value stored into a collection slot is marked as escaping to the collection's region (residence raised),
  not to the enclosing scope.
- `src/checker/escape.tks:188` — `fn_escaping_vars` — the per-function escaping set the collection-holder
  rule feeds; a stored `class` element joins the collection's residence, so it is not freed at the storing
  scope's end.
- `src/checker/escape.tks:194,200` — `fn_frees_old_vars` / `collect_frees_old_names` — the purge-on-reassign
  set; a `class` holder does NOT eager-free on remove under the conservative default (GATE-1) — it waits for
  the collection's region-drop.
- `src/runtime/arena.tks:696,700` — `region_drop`/`region_drop_subtree` — the reclamation primitive the
  holder rule targets (bulk-free at the collection's region death).

NEW: no new module; this is a rule extension inside the existing escape pass.

## How

1. **The holder residence rule.** When a `class`-typed value is stored into a collection (a `push`/`set`
   into a `List`/`Map`/`Sorted*`/`PQ` slot), the escape pass raises the value's residence LUB to the
   collection's region — the collection is a *holder*. The element therefore lives exactly as long as the
   collection and is bulk-freed by `region_drop` when the collection's region dies. NO refcount, NO
   per-object free.

```teko
/**
 * mark_class_holder_escape — raise a stored `class` element's residence LUB to the holding collection's
 * region. A collection is a HOLDER: a `class` value pushed/set into it outlives the storing scope and is
 * reclaimed by region-drop when the collection drops (the conservative GATE-1 default — leak-safe, never
 * UAF). Distinct from `wrapped` (COL-F0c, refcounted) and from VALUE (COL-F0a, bump + bucket). No eager
 * free on early remove under this default; that optimization is gated on GATE-1 closing toward wrapped.
 *
 * @param elem        the `class` element expression being stored
 * @param holder_rgn  the collection's residence region the element joins
 * @param acc         the escaping-names accumulator the pass threads
 * @since 0.3.1
 */
fn mark_class_holder_escape(elem: TExpr, holder_rgn: str, ref acc: []str)
```

2. **GATE-1 = conservative default (owner gate, `colecoes-…:88-98`).** A `class` element removed EARLY
   from a long-lived collection stays alive until the collection's region drops (residence already raised).
   This never leaks past the collection and never dangles — it is the SAFE choice while GATE-1 is open. The
   eager-free-on-early-remove path (promote-to-wrapped, `region_drop` the object at remove-time) is an
   ADDITIVE follow-up (one `[dry]` crumb per collection) that unblocks when the owner closes GATE-1. This
   crumb does NOT block on GATE-1.
3. **No refcount for `class`.** COL-F0b is the region-drop path; the `retain`/`release` refcount is COL-F0c
   and applies to the `wrapped` kind ONLY. The `class` `push`/`get`/`pop` copy the POINTER (C#-like,
   shallow) — the residence rule governs WHEN the region drops, not how the pointer copies.
4. **Stay inert.** No `src/` collection stores a `class` element until FASE 1 (`0077`+); the extended
   residence rule changes no emitted byte on the current corpus → `[dry]` byte-identical.

## Rulings & laws

- **Teko-only:** `src/checker/escape.tks` only; no C twin.
- **W15 full Javadoc** on `mark_class_holder_escape` and any helper; flatten/extract; no inline `//`.
- **Doc-2 three-category memory (SEALED, `:1614-1637` + `:1757`):** value = bump + bucket / **class =
  arena-per-object + region-drop via escape-analysis (THIS crumb)** / wrapped = refcount. `class` is NOT
  refcounted.
- **GATE-1 (owner gate, conservative default):** the class-early-remove eager-free is the only thing gated;
  region-drop-via-escape is the leak-safe, never-UAF default; no item is BLOCKED.
- **Additive/inert:** extends the residence LUB; no corpus collection stores a `class` yet → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

The self-build FIXPOINT exercises the class-holder path once FASE 1 migrates the compiler's own `List`/`Map`
(the compiler stores `class` AST/type nodes in them), so NO happy-path fixture. The GATE-1 boundary the
compiler may not reach itself (a `class` removed early from a long-lived collection) needs one isolated
oracle:

| fixture | asserts | expected |
|---|---|---|
| `class_holder_region_drop` | a `class` element pushed into a collection is freed at the collection's region-drop, not at the storing scope's end; no leak, no premature free (the GATE-1 conservative default) | 0 |
| `class_holder_no_refcount` | a `class` element is NOT refcounted (no inc/dec on get/pop); multiple gets share the same pointer; reclamation is region-drop only | 0 |

## Gate

`[dry]` — compile + the two fixtures + fixpoint (byte-identical; residence rule inert on the current
corpus). "Green" = a stored `class` element's residence raises to the collection's region, it is region-
dropped (not refcounted, not eager-freed under GATE-1), `[dry]` build byte-identical. Reseed-class:
`(folds R1)`.

## Deps

`COL-F0a` (the fixed-backing + place/read/write substrate the holder stores into).

## Done when

`escape.tks` raises a stored `class` element's residence to the holding collection's region (region-drop
reclamation, no refcount, conservative GATE-1 default with no eager early-free), the fixtures pass, and a
`[dry]` build is byte-identical.
