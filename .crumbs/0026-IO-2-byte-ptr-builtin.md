---
seq: 0026
crumb-id: IO-2
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [SM-P1]
sources:
  - "docs/design/io-streaming-0.3.1.md:277-286"   # §4 the one new builtin byte_ptr
  - "docs/design/io-streaming-0.3.1.md:357-360"    # §7 crumb 2 (registration + lower + emit)
  - "docs/design/io-streaming-0.3.1.md:425-442"    # §10 unblocked by P1 addr-of-index
---

# 0026 · IO-2 — `teko::mem::byte_ptr` builtin + lower (`lower_addr_of_place`) + codegen

> `teko::mem::byte_ptr` builtin + lower (`lower_addr_of_place`) + codegen — the zero-copy address-of-index
> the streaming I/O layer needs.

## Goal

Add the ONE new builtin the io-streaming layer rests on: `teko::mem::byte_ptr(xs: []byte, i: u64):
ptr<byte>` = `&xs[i]`, the base address of a slice element WITHOUT materializing a copy — so a syscall write
loop can hand `os_write(handle, byte_ptr(data, off) as word, take)` the raw address of `data[off]`
(io-streaming §4). It is the sibling of `buf_ptr`, lowering through the SAME address-of-index machinery
`lower_addr_of_place` (`src/lir/lower.tks:1376`) already emits for `ref x[i]` — the exact class of change
SM-P1 pins (the addr-of-index the DPS/arena work needs), which is why IO-2 depends on SM-P1 and is unblocked
once that addr-of-index is reachable from source. Purely ADDITIVE: `byte_ptr` has no `src/` caller on the
hot compiler path until IO-3 (`0044`, M2) uses it in `file_stream.tks`, so a `[dry]` build is byte-identical.
The io-streaming doc marked crumb 2 `[RITUAL]` in isolation, but per the master plan IO-2 FOLDS into the
single M1 teaching reseed (SM-R1, `0030`) — it mints no reseed of its own (`(folds R1)`).

## Where

- `src/checker/scope.tks` — NO CURRENT LANDING for `byte_ptr_signature` or a `builtin_fn` arm (to be added)
  — the intrinsic type, beside `buf_ptr_signature`; will be dispatched by name in `builtin_fn` once the
  signature is registered.
- `src/lir/lower.tks:1376` — `lower_addr_of_place` — the address-of-index machinery `byte_ptr` REUSES; the
  `byte_ptr` call arm will lower `byte_ptr(xs, i)` as `&xs[i]` through it (the native leg; the C leg is
  the emitter below).
- `src/codegen/codegen.tks` — NO CURRENT LANDING for `emit_byte_ptr` (to be added as a C-leg special emitter,
  `&((uint8_t*)…)[i]`), dispatched by bare last segment `byte_ptr`.
- The result will be an opaque `ptr` that widens to any `ptr<T>` at the use-site via the existing
  `ptr_widens_to_opaque` path (exactly as `buf_ptr`'s result does).

NEW: no new module; one builtin to be registered in the existing checker + emitted on the C leg (native leg
reuses `lower_addr_of_place`).

## How

1. **Register the signature** (already shaped as `byte_ptr_signature` at `scope.tks:265`):

```teko
/**
 * byte_ptr — the address `&xs[i]` of a `[]byte` element WITHOUT materializing a copy: the zero-copy
 * address-of-index the streaming I/O loop hands a syscall (`os_write(handle, ptr_word(byte_ptr(data, off)),
 * take)`). Sibling of `buf_ptr`; lowers through the same `lower_addr_of_place` machinery the compiler emits
 * for `ref x[i]`. The result is an opaque `ptr` that widens to any `ptr<T>` at the use-site
 * (`ptr_widens_to_opaque`); it is a COMPILER INTRINSIC (no surface `to` cast) — the sanctioned reinterpret
 * carve-out, so `cast_check` never fights it and the opaque-ptr law stays intact.
 *
 * @param xs  the byte slice
 * @param i   the element index whose address is taken (`0 <= i <= xs.len`)
 * @return    an opaque `ptr` to `xs[i]`, widening to `ptr<byte>` at the use-site
 * @since 0.3.1
 */
fn byte_ptr(xs: []byte, i: u64): ptr
```

2. **Wire the C-leg emitter** (`emit_byte_ptr`, `codegen.tks:2589`): emit the element address
   `&((uint8_t *)<xs.ptr>)[<i>]` (or the equivalent over the fat's `ptr`), dispatched by bare last segment
   `byte_ptr` in the `teko::mem` block (`codegen.tks:2972`) — a special emitter (it needs the cast), not a
   bare name-substitution.
3. **Wire the native-leg lower** (`lower.tks:1376`): the `byte_ptr(xs, i)` arm reuses `lower_addr_of_place`
   on `xs[i]` — identical to `ref x[i]`. If the native addr-of-index is not yet reachable (SM-P1's facet),
   it HONEST-STOPS in `lower_call`'s terminal `_ =>` "not yet lowered (N2)", exactly as
   `ptr_word`/`word_ptr` do — the native lowering is Doc-2 terminal (NAT-*, M4).
4. **Depends on SM-P1 (addr-of-index).** `byte_ptr` is the same change class SM-P1 pins (the addr-of-index
   the DPS/arena machinery needs); once that addr-of-index is reachable from source, `byte_ptr` is a
   sibling-line addition. Prefer the dedicated `byte_ptr` over the `as_ptr`-over-`str↔[]byte`-reinterpret
   fallback (io-streaming §4/§11): it does not tie the I/O axis to the implicit-cast expurgo axis and is the
   more honest surface.
5. **Stay inert.** `byte_ptr` has no hot-path `src/` caller until IO-3 (`0044`) adopts it in
   `file_stream.tks`; a `[dry]` build is byte-identical.

## Rulings & laws

- **Teko-only:** checker/lir/codegen `.tks`; no C twin (the emitter is codegen `.tks`).
- **W15 full Javadoc** on `byte_ptr` and the emitter helper; flatten/extract; no inline `//`.
- **Opaque-ptr law (`ptr_opaque_error`) intact:** `byte_ptr` is a COMPILER INTRINSIC (no surface `u64→ptr`
  cast introduced); the result widens via `ptr_widens_to_opaque` exactly as `buf_ptr`.
- **Master-plan reseed folding:** io-streaming marked crumb 2 `[RITUAL]` standalone, but IO-2 rides the ONE
  M1 teaching reseed (SM-R1, `0030`) — `(folds R1)`, no reseed of its own.
- **Additive/inert:** no hot-path caller until IO-3 → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

`src/io/file_stream.tks:181` already references `byte_ptr` (the streaming address-of-index), but that path
is only reached once IO-3+ is built and is a leaf until then; the zero-copy address contract itself is not
otherwise self-exercised — one isolated oracle:

| fixture | asserts | expected |
|---|---|---|
| `byte_ptr_addr_of_index` | `ptr_word(byte_ptr(xs, i))` equals the base + `i` (no copy); writing through it mutates `xs[i]` | 0 |
| `byte_ptr_zero_copy_write` | a syscall-shaped `write(handle, byte_ptr(data, off) as word, take)` reads the original bytes with no intermediate buffer | 0 |

## Gate

`[dry]` — compile + the two fixtures + fixpoint (byte-identical; `byte_ptr` inert on the hot path). "Green" =
`byte_ptr` type-checks (opaque `ptr` widening), emits `&xs[i]` on the C leg, native leg honest-stops, `[dry]`
build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`SM-P1` (pins the addr-of-index facet `byte_ptr` reuses via `lower_addr_of_place`).

## Done when

`teko::mem::byte_ptr(xs, i)` is registered in the checker, emits the zero-copy `&xs[i]` on the C leg (native
= honest-stop reusing `lower_addr_of_place`), the fixtures pass, and a `[dry]` build is byte-identical
(inert on the hot path until IO-3 adopts it).
