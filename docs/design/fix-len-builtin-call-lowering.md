# Fix — the `len` builtin CALL is not lowered by the native backend (N2) — RECON + minimal patch

> **Status:** DESIGN + proposed patch. Read-only RECON on product code; this file is the SOLE edit. NO
> build, NO reseed, `teko test` NOT run in any form. Isolated worktree off `origin/fix/retirement`,
> branch `design/len-lowering-fix`; the main checkout + other agents' worktrees UNTOUCHED. For an
> implementer (or `ac69221b` post-§14) to apply.
>
> **What broke.** The CI mem-paranoid gate ran CLEAN through every heavy pass (checker / monomorph /
> consteval ×2 — no arena growth, NO leak) and failed only in codegen:
> `native backend N1: builtin len not yet lowered (N2) [in teko::checker::mono_slice_and_optional_mangle]`.
> That fn is a `#test` at `src/checker/checker_test.tkt:592`. This is a PRE-EXISTING gap (the S4-generics,
> pre-9-ops "len sem lowering" the owner remembered) that was masked by the Env drift and resurfaced.

---

## 1. PINPOINT — the exact `len`, and its form (NOT `.len`, NOT a fat side-table miss)

The failing `len` is the **builtin `len` CALL**, at TWO sites inside `mono_slice_and_optional_mangle`:

- **`checker_test.tkt:619`** — `teko::assert::is_true(teko::str::len(mono_type_mangle(Slice { element = i64t() })) > 0)`
- **`checker_test.tkt:627`** — `teko::assert::is_true(teko::str::len(mono_type_mangle(Reference { inner = i64t() })) > 0)`

Both are `teko::str::len(<call returning str>)` — the CALL spelling of the length query, **not** the `.len`
FIELD form (`xs.len`). The fn body contains no `.len` field access at all (its other assertions use
`teko::runtime::str_eq`). So the failure is 100% the builtin `len` call.

**Correcting the working hypothesis in the task.** The candidate path in the brief — `lower_len_field`
(`lower.tks:13725`) → `lower_fat_expr(receiver)`, the fat side-table keyed by local NAME (`len_vregs`/
`has_len`) — is the **`.len` FIELD** path. It is NOT the site that errors here. `lower_len_field` is only
reached from `lower.tks:13674` (`if fa.field == "len" && is_fat_type(fa.receiver.type)`), i.e. for a
`TFieldAccess`. The test uses a `TCall`, which never reaches `lower_len_field`. The `.len` field path
already works (it is how the whole compiler reads lengths, which is why it self-hosts); the CALL path does
not.

---

## 2. The lowering trace — WHY the `len` CALL falls to `_ => error` (N2)

`len` is a checker BUILTIN, not a declared function:
`src/checker/scope.tks:1095` — `if name == "len" { return Func { params = [str_t]; ret = PrimKind::U64; … } }`
(the signature `teko::str::len(str): u64`). Because it resolves to a builtin (no `TFunction` declaration),
the checker leaves the call with an **empty `call_ns`** — the checker's own "a builtin, or nothing" signal.

At lowering, `lower_call` (`lower.tks:3151`) has interception arms only for a fixed set of empty-`call_ns`
builtins (`last_index_of`, `str_from_utf8`, `err_loc`, `err_typed`, `f64_bitcast`, `float_parse`,
`buf_ptr`, `load_u64`, `store_u64` — lines `3155-3163`). **There is no `len` arm.** So it falls through to
`call_symbol` (`3164`), which for an empty `call_ns` (`call_symbol`, `lower.tks:5340-5352`) tries, in order:

1. `assert_seed_symbol(callee)` → null,
2. `native_builtin_symbol("len")` → **null** — `len` is in NONE of the builtin-symbol tables;
   `builtin_str_query_symbol` (`lower.tks:5044-5048`) carries only `ends_with`/`contains`,
3. `is_list_builtin_call` → false,
4. `last == "parse"` → false,
5. → `unresolved_builtin_stop("len")` (`lower.tks:5351`, body `:5397-5399`):
   `error { message = concat("native backend N1: builtin `", "len", "` not yet lowered (N2)") }`.

That is the exact error string observed. **Root cause: the `len` builtin has a checker signature but no
native lowering — neither a `native_builtin_symbol` twin nor a `lower_call` interception.** It only ever
worked through the DISTINCT `.len` field path, so the CALL form is an unlowered pre-existing hole. The mem
gate exposed it because it compiles the checker's `.tkt` (which uses the rare call form) to native.

---

## 3. The minimal fix — RECOMMENDED (a): lower the `len` CALL as a fat-length read

A `str` (and a slice) is a fat `{ptr@0; len@8}` value. `len(s)` is simply the LENGTH half of `s` — the
**same** read `lower_len_field` performs for `s.len`, sourced from the call ARGUMENT instead of a field
receiver. **No runtime call is needed and none should be emitted** (there is no `tk_str_len` twin, and
introducing one would be strictly worse — a call where a register read suffices).

**Decisive enabling fact (verified):** `lower_fat_expr` (`lower.tks:11019`) already dispatches a `TCall`
receiver to `lower_call_fat` (`:11027`). So the test's argument `mono_type_mangle(…)` — a fat-returning
`TCall` — lowers to a `(ptr, len)` pair with ZERO new machinery. The fix reuses `lower_fat_expr` verbatim,
exactly as `lower_len_field` does.

**Placement.** One interception in `lower_call`, alongside the existing empty-`call_ns` builtin arms,
INSERTED after `lower.tks:3163` (the `store_u64` arm) and BEFORE `call_symbol` at `:3164`:

```teko
    if c.call_ns.len == 0 && is_len_builtin_call(c.callee) { return lower_len_builtin_call(ctx, e, c) }
```

**Two small fns to add (full-Javadoc, copy-ready), beside `lower_len_field` (`lower.tks:13725`):**

```teko
/**
 * is_len_builtin_call — is this the unresolved builtin `len(s)` CALL (checker scope.tks:1095:
 * `len(str): u64`)? The checker leaves a builtin with an EMPTY `call_ns` (its "a builtin, or nothing"
 * signal), so keying on `call_ns.len == 0` (the caller's guard) plus a last segment of `len` is safe:
 * a USER function named `len` resolves to a declaration and carries a NON-empty `call_ns`, so it never
 * reaches here. Distinct from the `.len` FIELD form (`lower_len_field`, reached via a `TFieldAccess`) —
 * this is the CALL spelling `teko::str::len(x)`, the rare form the corpus uses at
 * `checker_test.tkt:619`/`:627` that the N1 subset never lowered.
 *
 * @param callee  the call's callee path
 * @return        true iff the path's last segment is the bare builtin name `len`
 * @since 0.3.1 (len-builtin-call lowering)
 */
fn is_len_builtin_call(callee: parser::Path): bool {
    callee.segments.len > 0 && callee.segments[callee.segments.len - 1].name == "len"
}

/**
 * lower_len_builtin_call — lower the builtin `len(s)` CALL as the LENGTH half of its single fat argument's
 * `{ptr, len}` pair: the SAME fat-length read `lower_len_field` performs for `s.len` (no `load` — the
 * length is already in a VReg once the arg lowers via `lower_fat_expr`), sourced from the call argument
 * instead of a field receiver. NO runtime call is emitted — a `str`/slice is already a fat value, so its
 * length is in hand. The `u64` result is the length VReg directly, matching `e.type` (`u64`). Since
 * `lower_fat_expr` (lower.tks:11019) already dispatches a fat-returning `TCall` to `lower_call_fat`, a
 * complex argument such as `mono_type_mangle(...)` (a call returning `str`) lowers with no extra plumbing.
 *
 * @param ctx  the lowering context
 * @param e    the `len(...)` call expression (its type is `u64`)
 * @param c    the call node (exactly one fat argument)
 * @return     the length half of the argument's fat pair, as a `u64` value
 * @throws     when `len` is called with other than one argument (internal — the checker enforces arity),
 *             or propagated from lowering the argument
 * @since 0.3.1 (len-builtin-call lowering)
 */
fn lower_len_builtin_call(ctx: LowerCtx, e: checker::TExpr, c: checker::TCall): Lowered | error {
    if c.args.len != 1 { return error { message = "native backend N1: builtin `len` expects one argument (internal)" } }
    var fo = match lower_fat_expr(ctx, c.args[0]) { LoweredFat as x => x; error as err => return err }
    Lowered { ctx = fo.ctx; vreg = fo.len }
}
```

**Existing fns touched:** `lower_call` (`lower.tks:3151`, one new `else-if`-style interception at `:3163`).
`is_len_builtin_call`/`lower_len_builtin_call` are new; `lower_fat_expr` (`:11019`), `lower_call_fat`
(`:11066`), `LoweredFat` (`.ctx`/`.len`) are reused UNCHANGED — the identical machinery `lower_len_field`
already relies on. No `native_builtin_symbol` change, no runtime twin, no `.tkb`/codec/checker change.

**Why this is the right fix, not just the owner default.** The `len` builtin CALL is legitimate surface
(the owner's point: a length read over any expr-slice/str is legitimate). The call form is genuinely
unlowered for EVERY caller, not just this test — (a) closes the actual capability gap. It is ~18 lines,
adds NO new lowering primitive (a fat-length read is exactly `lower_len_field`'s body), and cannot regress
the corpus: the interception fires only when `call_ns` is empty AND the last segment is `len`, which today
always hard-errors.

**Inertness / correctness checks (verified against the trace):**
- `lower_call:3154` `if is_fat_type(e.type)` does NOT pre-empt this — `len`'s result type is `u64` (not
  fat), so control reaches the new arm.
- The closure/iface guards (`:3152-3153`) do not apply to a builtin `len`.
- Arg is always `str` per the checker signature (`scope.tks:1095`), but `lower_fat_expr` handles both
  `str` and slice, so the arm is robust if a slice-`len` call ever appears.

---

## 4. The alternative — (b) rewrite the test line — VIABLE but NARROWER (owner may veto (a) for it)

Option (b): edit `checker_test.tkt:619`/`:627` to avoid the `len` call — e.g. bind the mangle result to a
local and read the `.len` FIELD (which already lowers), or assert the mangle another way:

```teko
    var m_sl = mono_type_mangle(Slice { element = i64t() })
    teko::assert::is_true(m_sl.len > 0)           // `.len` FIELD form (lower_len_field) — already lowered
    // …and likewise for the Reference case at :627
```

- **Cost:** ~2 test-line edits, zero backend change.
- **Why NOT trivially better:** it only paints over THIS occurrence. The builtin `len` CALL stays an
  unlowered N2 hole for any other/future caller (`teko::str::len(x)` is legal, checker-typed surface). It
  trades a real capability gap for a local workaround — a latent trap the next `len(x)` call re-triggers.
  It also leaves the native subset asymmetric (`.len` lowers, `len(x)` does not) with no honest reason.

**Recommendation: (a).** It is the owner's default AND the correct closure of the gap, self-contained and
regression-safe. (b) is a legitimate quick unblock IF the owner wants the test lane green in one commit
before touching the backend — but it should be a STEP toward (a), not a substitute. **Deferring the veto to
the owner as instructed.**

---

## 5. Verification the fix is complete (design-level)

- The only unlowered symbol in `mono_slice_and_optional_mangle` is `len` (the other calls —
  `teko::runtime::str_eq`, `teko::assert::is_true`, `mono_type_mangle` — already lower: `str_eq` is a
  resolved runtime twin, `mono_type_mangle` is a Teko-Teko call, `assert` a declared fn). Closing `len`
  clears the fn.
- No other `len`-CALL site is required for THIS failure, but (a) fixes them all program-wide (it is a
  capability arm, not a point patch), so no second occurrence can re-trigger the same stop.
- The mem verdict for #110 was already CLEAN on every heavy pass; this codegen arm is the last blocker to
  a full green mem-paranoid run + the test lanes.

**Suggested regression fixture (native exit code).** `examples/regressions/own_native/len_builtin_call/` —
a `main` that computes `teko::str::len(some_fn_returning_str())` (a fat-returning CALL argument, mirroring
the test) and returns it as the exit code; a nonzero, correct length proves the CALL form lowers to the
fat-length read. Add a second assert reading `len` of a slice-returning call if a slice-`len` call form is
in scope.

---

## 6. Anchors (verified on `origin/fix/retirement` this session)

| what | file:line |
|---|---|
| failing test fn | `src/checker/checker_test.tkt:592` (`mono_slice_and_optional_mangle`); the two `len` calls at `:619`, `:627` |
| `len` checker builtin signature (`len(str): u64`) | `src/checker/scope.tks:1095` |
| `lower_call` (the interception site) | `src/lir/lower.tks:3151`; empty-`call_ns` builtin arms `:3155-3163`; insert new arm after `:3163` |
| `call_symbol` empty-`call_ns` path → the stop | `src/lir/lower.tks:5340-5352`; `unresolved_builtin_stop` `:5397-5399` |
| `native_builtin_symbol` (no `len`) | `src/lir/lower.tks:5250`; `builtin_str_query_symbol` (only `ends_with`/`contains`) `:5044-5048` |
| `lower_fat_expr` covers a `TCall` receiver | `src/lir/lower.tks:11019` (dispatch `:11027` → `lower_call_fat` `:11066`) |
| `lower_len_field` (the `.len` FIELD twin the fix mirrors) | `src/lir/lower.tks:13725`; field dispatch `:13674` |

*Grounding: all `file:line` real on `origin/fix/retirement`. No build/test/reseed run.*
