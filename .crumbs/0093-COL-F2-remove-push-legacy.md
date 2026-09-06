---
seq: 0093
crumb-id: COL-F2
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [COL-Q1, COL-Q3, COL-Q5, COL-Q7, COL-Q9, COL-Q10, COL-Q13, COL-Q14, COL-Q15, COL-Q16, COL-Q17, COL-Q18, COL-Q19, COL-Q20, COL-QFile, RM-C3, RM-C4, RM-C5, RM-C6, RM-C7, RM-C8]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:27-38"                 # Eixo A — matar o crescimento
  - "docs/design/arena-especificacao-unica-0.3.1.md:74-83"             # o copy-grow vive na slice
  - "docs/design/plano-mestre-0.3.1-implementacao.md:263"              # M3 COL-F2 row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:253-257"          # no-tombstone expurgo discipline
  - "src/collections/collections.tks:94"                               # sorted_insert/heap_ dead surface
  - "src/list/list.tks:15"                                             # push wrapper
---

# 0093 · COL-F2 — remove `push`/single-array/`empty`/`with_cap`/`grow_inplace` + dead `arr_*`/`sorted_insert`/`heap_*`

> The E2 collections expurgo: with every consumer migrated to known-size arrays (all COL-Q*, RM-C3..C8),
> DELETE the dynamic-growth surface — `teko::list::push`/`empty`/`with_cap`/`grow_inplace`, the
> single-array growth path, and the now-dead `arr_*`/`sorted_insert`/`heap_*` — and clean lexer/parser/
> checker of any grammar/typing that only served them. NO tombstone diagnostic.

## Goal

This crumb removes the ROOT of the 93% memory peak (`tk_slice_push_r`, 4980 MB — the copy-grow that
leaks the abandoned buffer into the never-freed `root` arena) at the LANGUAGE-SURFACE level: the
`teko::list::push`/`empty`/`with_cap`/`grow_inplace` family and the dead heap/sorted-insert helpers that
were the only remaining callers of dynamic growth. By this seq, **every** `src/` producer has already
been converted to the four known-size natures (MAP / PARSE-SCAN / FILTER / OUTPUT-BUFFER) by the M2 RM-C
and COL-Q waves, so the removal deletes surface with no live caller. It is a **byte-mover** expurgo (the
self-compile enumerates every residual reference as a hard compile error — that error list IS the
cleanup worklist per the build-first methodology) and it DRIVES an expurgo reseed (E2). The removal
follows the no-tombstone law: `push`/`empty`/`with_cap`/`grow_inplace`/`sorted_insert`/`heap_*` become
simply UNKNOWN symbols — an old-form call gets the same generic unknown-symbol error a never-existent
name gets, with no "was removed" message.

## Where

- `src/list/list.tks:15` — `List<T>::push` and the module `teko::list::push`/`empty`/`with_cap`/
  `grow_inplace` free functions — DELETE the declarations and their bodies.
- `src/list/*.tks` — the single-array dynamic-growth backing (the `tk_slice_push_r`/`grow_inplace`
  bridge decls) — DELETE; nothing references it after RM-C3..C8.
- `src/collections/collections.tks:94` `sorted_insert`, `:108` `heap_sift_up`, `:122` `heap_pop_min`
  (and the rest of the `arr_*`/`heap_*` cluster) — DELETE (dead: their only callers were push-based).
- `src/checker/typer.tks` — any typing rule keyed to the removed builtins (`push`/`empty`/`with_cap`/
  `grow_inplace` arity/receiver checks, and `assign_frees_old`'s `list::push` value arm) — DELETE the
  dead arms; `type_index_assign` + `[]T`/`[n]T = []` STAY (they are the replacement, already live).
- `src/parser/*.tks` / `src/lexer/*.tks` — NO dedicated grammar/token existed for these (they were plain
  calls), so the parser/lexer clean is a CONFIRM-NONE step: grep for any special-cased identifier and
  remove it if found; otherwise the removal is checker-and-library only.
- `src/runtime/teko_rt.c` — the C slice-grow machinery (`tk_slice_push_r`, `tk_slice_grow_inplace`,
  `tk_slice_with_cap`) becomes dead once codegen stops emitting calls; it is REMOVED here on the array
  path (the runtime C-death of the array machinery — the free-old-in-C approach was abandoned).

## How

1. **Confirm zero live callers FIRST** (build-first methodology, step 4-5). Grep `src/` for
   `list::push`, `list::empty`, `with_cap`, `grow_inplace`, `sorted_insert`, `heap_` — every hit MUST be
   in test/dead code by this seq (COL-Q* + RM-C3..C8 migrated all producers). Any live hit is a
   migration gap in a DEP, REPORTED up, not patched here.
2. **Delete the library surface** (`src/list/list.tks:15` + the module free fns + single-array backing +
   `collections.tks:94/108/122` cluster). The element layout is UNCHANGED (zero-fill `{ptr,len}`, no
   tag) so removal moves no bytes in surviving arrays — only deletes the growth path.
3. **Delete the checker's dead arms** (`typer.tks`): the `push`/`empty`/`with_cap`/`grow_inplace`
   builtin-typing and the `assign_frees_old` `list::push` value arm. Per "não detectar/barrar o que não
   existe", these become dead code the instant the surface is gone — remove, do not reword.
4. **Delete the runtime C array-growth machinery** (`teko_rt.c`: `tk_slice_push_r`/`grow_inplace`/
   `with_cap`) — sanctioned by the "nada em teko_rt.c pro expurgo; a máquina de slice-grow vira código
   morto a REMOVER" ruling. This is the C-death of the ARRAY path (distinct from RM-C9's control-slot
   transcription).
5. **NO tombstone** (no-tombstone law): add NO "push was removed / use index-assign" diagnostic. A stale
   `xs = list::push(xs, v)` yields the ordinary unknown-symbol error (`file:line:col: unknown function`),
   identical to a typo — COL FASE2 discipline, owner ruling 3.
6. **Reseed ITERATIVELY** (owner: ensinar→seed→sweep→seed). Remove roots → seed → the self-compile ERRS
   at each residual site → convert → seed → repeat until `gen2==gen3` byte-identical. Do NOT hand-hunt;
   let the compiler enumerate.

No new surface is authored — this crumb only deletes. The replacement machinery (`of_len`/`[n]T = []`/
`type_index_assign`/`mem::copy` join idiom) already lives from M1 (RM-C2, COL-F0a) and M2.

## Rulings & laws

- **Teko-only:** library + checker `.tks`; the C twin is FROZEN except the sanctioned removal of the
  dead array-growth machinery in `teko_rt.c` (owner: it is REMOVED, not patched).
- **W15 full Javadoc** on anything that SURVIVES; removed decls carry no doc (they are gone).
- **Removals = clean expurgo, NO tombstone:** the removed names become generic unknown symbols; no
  "was removed" diagnostic (`plano-mestre:253-257`, owner ruling 3).
- **NO PUSHES + zero dynamic growth (owner 2026-08-18):** `push`/`grow_inplace`/`with_cap` are the
  banned copy-grow class; array maintenance is 100% manual (known-size + index-assign / two-pass /
  widen-then-cut / spread-literal). This crumb enforces the ban by DELETION.
- **Safety:** NEVER `teko test .`; build each iterative step in a subshell with `ulimit -v 6815744`
  (6.5 GiB) — a blown guard is a root-cause fix, never a raised ceiling; commit each green sweep; reseed
  ONLY at this [RITUAL]; the E2 seed harvests once `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the signature deletions.

## Fixtures

The self-build fixpoint exercises every SURVIVING collection path (the compiler is the largest consumer
of known-size arrays). The one path the self-build never takes is the REJECT of a removed name:

| fixture | asserts | expected |
|---|---|---|
| `expurgo_push_unknown_not_tombstone` | `xs = teko::list::push(xs, v)` fails with a GENERIC unknown-symbol error (no "removed"/"use index-assign" text) | `EXPECT_COMPILE_FAIL` |
| `expurgo_grow_inplace_unknown` | `teko::list::grow_inplace(ref xs, v)` is an unknown function, same as a typo | `EXPECT_COMPILE_FAIL` |

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed (E2). "Green" = the surface is gone, every
surviving array path compiles known-size, the two reject fixtures compile-fail with a GENERIC message,
the peak build memory drops (target ≤1.5 GiB trajectory; must fit under 6.5 GiB), and `gen2==gen3` is
byte-identical after the E2 harvest. Reseed-class: `expurgo`.

## Deps

`ALL COL-Q*` (every collection rebased on known-size backing) and `RM-C3..C8` (every memory-reduction
producer converted) — verbatim from 000-INDEX. All must land first so the removal has zero live caller.

## Done when

`teko::list::push`/`empty`/`with_cap`/`grow_inplace`, the single-array growth path, and the dead
`arr_*`/`sorted_insert`/`heap_*` cluster are DELETED from library + checker (+ the dead C array-growth
machinery), a stale call gets the generic unknown-symbol error with no tombstone, the reject fixtures
pass, and the E2 reseed lands `gen2==gen3` byte-identical.
