# `teko::c_types` + the safe↔unsafe marshalling boundary — design 0.3.1

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented.** Every claim about the tree below is a
> READ measurement with a file:line citation; nothing here was built or run (the assignment forbids
> it), and every place where a number is missing says so and says how to get it.
>
> **Owner rulings PINNED here and treated as law:**
> * *"havendo c_types com tipos conhecidos em teko (não esqueçamos de c_string que sempre termina em
>   \0 (NULL)), podemos determinar as funções que fazem extern como unsafe e funções safe que usam
>   Marshall para transicionar entre safe e unsafe. A borda é desconhecida, mas se sabemos o que
>   esperar, estamos falando de um unsafe que é safe (só usa o keyword `unsafe` para habilitar
>   ponteiros/sigilos)."*
> * *"Não mente, você mesmo disse: `type c_int = i32`; não está mentindo e quem escreve um FFI espera
>   por isso e deve conhecer a representação em teko também."*
> * **[PIN-1, embedded NUL]** *"O caso do c_string, cabe ao desenvolvedor e não ao compilador, C verá
>   truncado e não há o que fazer."*
> * **[PIN-2, void]** *"Ok, uptr ou ptr<byte> para os casos void"* — superseding the earlier
>   `type c_void = null` proposal, which is WITHDRAWN.
> * **[PIN-3, nullability]** *"nos casos onde pode chegar nulo de ou para C, devemos declarar como
>   `uptr | null` ou `ptr<T> | null` nos opacos."* — this closes a question the corpus had
>   explicitly escalated rather than guessed (`regressor.tkr:87-89`). See §5.4.
>
> **Companions:** `docs/design/marshall-spec.md` (the ratified `ptr ↔ ref` crossing, issue #498),
> `docs/design/star-ref-and-ffi-0.3.1.md` (§4 own-backend-first FFI), `docs/design/
> memory-unsafe-backend-remodel.md` (§2 unsafe-by-TYPE), `docs/memory/teko-mem-model-empirical.md`.
>
> **NOT a dependency:** `cargo/20-extern-return-narrowing` fixes an adjacent, independent defect
> (`examples/probes/arena_bottom/src/bottom.tks:50` — the native backend does not narrow an
> `extern fn`'s declared `i32` return). §9.3 states precisely why this design neither needs that fix
> nor changes its shape.

---

## 1. The thesis, and why the layer is not a blanket

The owner's framing inverts the usual reading of `unsafe`. `unsafe` here is a **capability
enabler** — it turns on pointers and sigils — not a hazard sticker. The safety does not come from
hiding the machine; it comes from the **marshalling being TOTAL**: every input, including the bad
ones, has a defined result. And the layer is **type plus documentation for a reader who already
knows both representations**, not a wrapper that pretends the C side is Teko.

That framing has a sharp consequence for scope, and it is the one that governs every YAGNI call in
§5: a `c_types` that tries to spell all of C before it has a caller is debt, not a layer. Every
entry below is anchored to a **real, named demand** in this tree or in a sibling cargo.

**The evidence that the layer already exists, unnamed.** `examples/probes/arena_teko/src/
full_gate.tks:45`:

```teko
pub extern fn c_setenv(name: u64, value: u64, overwrite: i32): i32 = "setenv"
```

`setenv` takes `const char *`. The probe passes addresses as `u64`, and the obligation "this must be
NUL-terminated" is written **in prose, inside an `@param`**. Nothing checks it. The marshalling is
already here — hand-written, un-named, un-typed, un-checked. This document gives it a name, a type,
and a totality proof.

---

## 2. As-built — what the tree actually does today

Every row is a read measurement.

| fact | as-built | citation |
|---|---|---|
| `tk_str` is a **VIEW**: `{ptr, len}`, **not** NUL-terminated, embedded NUL tolerated | yes | `src/runtime/teko_rt.h:44-48` |
| an `extern fn` param must be `Prim` / `Byte` / `Ptr` / `Uptr` / an **`extern type`** handle | **a hard checker gate**, C7.1a | `src/checker/typer.tks:5109-5121` (`extern_type_ok`), enforced at `typer.tks:5744-5747` |
| `str` is legal on an extern **only** `from "teko_rt"` (a closed exception, C7.2) | yes | `typer.tks:5731`, `teko_rt_type_ok` at `typer.tks:5128-5140` |
| an extern return may additionally be **`Void`** | yes, explicit arm | `typer.tks:5761-5763` |
| a **user struct** may NOT be an extern param/return (`Named` needs an `ExternBody`) | yes | `typer.tks:5115-5118` |
| a user struct crossing an extern boundary is a **C compile error** even where allowed | measured, recorded | `src/time/time.tks:21-34` |
| `extern type Name` exists — an opaque foreign handle, lowers to `typedef void *tk_t_Name;` | yes | `src/parser/ast.tks:463`, `parse_decl.tks:822-824`, `src/codegen/codegen.tks:9029-9035` |
| `ptr<byte>` as an extern param **links and runs** | proven twice in the regressor | `examples/regressions/bulk/src/q026_buf_ptr_memset_roundtrip/body.tks:1`, `q170_unsafe_rawbuf_roundtrip/body.tks:14` |
| `-> void` on a **raw libc** extern is proven | yes | `feat/issue-runtime-em-teko` `src/runtime/teko_rt.tks:676` (`rt_abort() = "abort" from "c"`) |
| `Ptr{inner=null}` (the opaque `ptr`) lowers to literally `void *` | yes | `codegen.tks:1549-1551` |
| `Uptr` lowers to literally `uintptr_t` | yes | `codegen.tks:1553` |
| `Void` lowers to literally `void` | yes | `codegen.tks:1502` |
| `Null` lowers to `uint8_t` | yes | `codegen.tks:1555` |
| a type ALIAS is **TRANSPARENT** — it resolves THROUGH to its target | yes | `src/checker/resolve.tks:794` |
| `unsafe_carrying` finds an unsafe stamp only on a **`Named`** (nominal) type | yes | `resolve.tks:1158` |
| `ptr<T>` is unsafe-carrying **only if `T` is** — `ptr<byte>` is SAFE today | yes | `resolve.tks:1163` |
| `uptr` is **not** unsafe-carrying at all (falls to the `_ => false` arm) | yes | `resolve.tks:1174` |
| the U2 fn-signature containment gate | `reject_unsafe_signature_contagion` | `src/checker/collect.tks:39-43`, applied at `:96` and `:112` |
| the U2 field-contagion gate (what `c55` pins) | `reject_unsafe_field_contagion` | `collect.tks:1974-1977`, applied at `:2077` |
| calls are **UNCOLORED** — a safe fn may CALL an unsafe fn; it may not NAME unsafe types | ratified, in force | `collect.tks:29-31` |
| `unsafe` never reaches codegen (`TFunction` carries no `is_unsafe` field) | yes | `src/checker/tast.tks:170-172`; `unsafe type` "carries zero new lowering", `src/mem/unsafe/rawbuf.tks:7-8` |
| `teko::mem::as_cstr` → `tk_cstr_dup`: `malloc`s, and the buffer is **never freed anywhere** | yes | `src/runtime/teko_rt.c:157-164`; no `free` of it in `teko_rt.c` |
| `teko::mem::str_from_cstr` → `tk_str_from_cstr`: an **UNBOUNDED `strlen`** on a foreign pointer | yes | `teko_rt.c:167` |
| `teko::mem::bytes_from_ptr` is **already bounded** (`n` is a required param) | yes | `teko_rt.c:178`, signature at `src/checker/scope.tks:638-642` |
| `as_ptr` / `as_cstr` / `str_from_cstr` have **ZERO callers** in the whole tree | measured | `rg 'as_cstr\|str_from_cstr\|as_ptr\(' --glob '*.tks'` returns only `scope.tks`, `codegen.tks`, doc-comments |
| `teko::mem::buf_ptr(len): ptr<byte>` bump-allocates into the **ENCLOSING** region | yes | `scope.tks:366-373`, `codegen.tks:3171-3186` |
| `teko::mem::region_new(): uptr` / `region_alloc(uptr, T): ptr<T>` — a **NAMED** region | yes | `src/checker/typer.tks:935-1000` |
| `unsafe #must_free type Arena { region: uptr }` — leaking the region is a **compile error** | shipped | `src/mem/unsafe/arena.tks` |
| Teko has **NO macros**, and none are planned before 1.0 | confirmed | zero hits for `macro` in `src/lexer`, `src/parser`, `src/parser/ast.tks`; `docs/memory/teko-laws-digest.md` "Metaprogramming-out-of-LTS" |
| `#os("…")` may precede **a function ONLY** — a `type` decl cannot be OS-guarded | yes | `parse_decl.tks:1239` |
| native targets | `Arm64Macho`, `Arm64Linux`, `X8664Linux`, `X8664Windows` | `src/build/project.tks` |
| native AOT is the sole engine | ruled | `docs/memory/teko-laws-digest.md` ("Twins retired, 2026-07-13, #524") |
| `Ptr` and `Uptr` are both **admissible union members** (they fall to the permissive arm) | yes | `resolve.tks:1642-1658` (`variant_member_admissible`) |
| `ptr<T> \| null` lowers to a **bare `T *` with NULL meaning null** — zero overhead, C-identical | yes | `cg_type_is_niche_able`'s `Ptr => true` arm, `codegen.tks:1868-1877`; the emit path at `codegen.tks:1532-1534` |
| `uptr \| null` is **NOT** niche-able — it falls to `_ => false` and lowers to a **tagged struct** | yes | `codegen.tks:1868-1877` (no `Uptr` arm) |
| a `Variant` is **rejected** as an extern param/return today (`extern_type_ok` has no arm) | yes | `typer.tks:5109-5121`, the `_ => false` arm |
| the checker defines **no `uptr <-> u64` cast**, and there is **no pointer arithmetic** | yes | `examples/probes/arena_bottom/src/bottom.tks:8-12`; `star-ref-and-ffi-0.3.1.md` §0 ("`*` operator: NOT an operator — glyph FREE") |

### 2.1 The owner's macro question — CONFIRMED, and the layer does not need them

`macro` appears nowhere in the lexer, the parser, or the AST, and `teko-laws-digest.md` defers
comptime/macros to post-`1.0.0.0`. The owner's read is correct on both halves:

* **Type aliases already exist** and are transparent (`parse_decl.tks:731-734` parses `type Name =
  <full type-expression>`; `resolve.tks:794` resolves through). `type c_int = i32` therefore needs
  **zero** new grammar.
* **Per-target variation has `#os()`** — but with a limit the owner's framing did not have: **`#os`
  guards FUNCTIONS ONLY** (`parse_decl.tks:1239`, `"#os(\"…\")` may only precede a function`"). A
  per-target *type* declaration is **not expressible today**. That single fact decides three
  exclusions in §5.2, and it is the one correction this section makes to the brief.

One residual measurement the tree cannot answer by reading: **no alias to a bare PRIMITIVE exists
in the corpus.** Every alias in the tree targets a slice (`pub type TypeTable = []TypeReg`,
`resolve.tks:24`), a union (`pub type Res = Thing | error`), a function type (`pub type IntIter = ()
-> i64 | null`, `src/iter/iter.tks:40`) or another named type (`pub type AggConstMap = ScalarConstMap`,
`comptime_fold.tks:139`). The resolution path for a bare prim is mechanically identical
(`resolve_named` → `builtin_type`, `resolve.tks:782` → `scope.tks:338`), so it *should* work — but it
is unproven. **Crumb C1 is the measurement**, and it is deliberately the first product crumb so the
whole design fails fast if that assumption is wrong.

---

## 3. Why no raw-libc `extern fn` ever took a `str` — it is a GATE, not a habit

The brief observed that no raw-libc extern in the tree has ever received a `str`, and inferred that
the `str → libc` crossing is unexplored territory. The measurement sharpens that: it is not
unexplored, it is **forbidden**.

`extern_type_ok` (`typer.tks:5109-5121`) admits `Prim`, `Byte`, `Ptr`, `Uptr`, and a `Named` whose
declaration body is an `ExternBody`. `Str` is not in the list. The only externs that take `str` are
those declared `from "teko_rt"`, which route to `teko_rt_type_ok` (`typer.tks:5128-5140`) — a
closed, seven-symbol exception for Teko's own runtime, which speaks Teko's `tk_str` ABI natively.

This is load-bearing in three directions:

1. **The crossing cannot be made implicit.** There is no coercion site to hang one on: the checker
   rejects the signature before any conversion could be considered. Whatever crosses is spelled at
   the call.
2. **`c_string` can never be an extern parameter type** if it is a struct — `extern_type_ok`'s
   `Named` arm requires an `ExternBody`, and `time.tks:21-34` records, empirically, that even where a
   struct is admitted the two nominally-distinct C typedefs make it a C compile error. §7 builds on
   this rather than around it.
3. **The `u64`-as-address idiom in the probes is not laziness** — it is the only spelling the author
   had, because `ptr<byte>` was available but the probes predate its use in this position. `q026` and
   `q170` prove `ptr<byte>` works. The idiom can be upgraded.

---

## 4. The unsafe frontier — the spelling ALREADY EXISTS, and one doc-comment lies about it

**Finding.** `pub extern unsafe fn f(…): T = "sym"` **parses today**, and the flag survives.

The modifier chain in `parse_function` consumes `extern` first (`parse_decl.tks:315-319`) and calls
`consume_unsafe_modifier` **after** (`:320-322`); the resulting extern `Function` node carries
`is_unsafe = is_unsafe` (`:373`). `func_type` then applies `reject_unsafe_signature_contagion` with
that flag (`collect.tks:96`, `:112`) for externs exactly as for any other function.

**The lie.** The doc-comment on `consume_unsafe_modifier` (`parse_decl.tks:206-210`) says:

> *"Placed AFTER `extern` in both `parse_function` and `parse_type_decl`'s modifier chain, so `pub
> unsafe fn`/bare `unsafe fn` parse while `unsafe extern fn` is rejected (the `extern` consume runs
> first and wins)."*

The parenthetical is true for that **token order** — `unsafe extern fn` fails at `expected \`fn\``
because `unsafe` is consumed and then `extern` is found where `fn` was required. But the sentence
reads as *"an extern fn cannot be unsafe"*, which is false, and it is almost certainly why not one
extern in the tree carries the marker. **Correcting that doc-comment is the entire grammar work of
this issue.** The same holds for types: `pub extern unsafe type Name` parses (`parse_decl.tks:798-805`).

### 4.1 What `unsafe` enables here, and why the new frontier FITS the existing vertical

The U2 vertical contains unsafe **by TYPE, not by block** (`memory-unsafe-backend-remodel.md` §2;
`rawbuf.tks:4`). Three consequences compose exactly into the owner's "unsafe que é safe":

* `unsafe` on a function is permission for its **signature** to name unsafe-carrying types
  (`collect.tks:39-43`). Nothing else.
* **Calls are uncolored** (`collect.tks:29-31`). A plain `pub fn` may CALL an `extern unsafe fn`;
  it simply cannot NAME the unsafe types in its own signature.
* Therefore a **safe wrapper over an unsafe extern is honestly safe**, provided the wrapper's own
  signature names only safe types (`str`, `i32`, `[]byte`) and the conversion in its body is total.
  That is the owner's sentence, expressed in the checker's own vocabulary: `unsafe` enables the
  pointer; totality is what makes the wrapper safe.

**Does it contradict `c55`?** No — it is a sibling of it. `examples/regressions/diagnostics/src/
c55_unsafe_field_in_safe_struct/case.tks` pins a SAFE struct holding an `unsafe type RawBuf` field,
rejected by `reject_unsafe_field_contagion` with the diagnostic `"struct/class 'Wrapper' has an
unsafe-typed field 'bu"`. A safe struct holding a `c_string` trips **the same gate with the same
message shape**, because `c_string` is declared `pub unsafe type … = struct { … }` (§7.1). c_types
adds **no new gate**, changes **no** gate, and needs **no** codegen: `unsafe` never reaches codegen
(`tast.tks:170-172` — `TFunction` has no `is_unsafe` field).

### 4.2 The one honest gap in the frontier, stated rather than hidden

`ptr<byte>` is **not** unsafe-carrying today (`resolve.tks:1163` recurses into the pointee; `byte`
is safe). So `extern unsafe fn c_setenv(name: ptr<byte>, …)` is, right now, **voluntary discipline,
not enforcement** — a plain `extern fn` with `ptr<byte>` params compiles.

`marshall-spec.md` §4 / crumb C0 (issue #498, ruling PINNED there) makes `Ptr` **unconditionally**
unsafe-carrying, which turns the discipline into enforcement. This design is built so that:

* **c_types does not depend on #498.** The gate that bites is `c_string`'s **own** `unsafe type`
  stamp (`resolve.tks:1158`, the nominal `Named` arm), which fires regardless.
* **#498 does not regress c_types.** Every extern this design touches is already marked `unsafe`,
  so C0 lands as a no-op on this corpus instead of a migration.

That is the whole coupling, and it runs one way.

---

## 5. THE INVENTORY — complete and closed

### 5.0 The admission rule (so the list is derivable, not curated)

An entry enters `teko::c_types` only if **both** hold:

1. **A named, real caller demands it** — an extern in this tree, an extern the sibling
   `cargo/20-concorrencia-adiantada` design names, or a `teko_rt` binding. No theoretical completeness.
2. **Its Teko mapping is the SAME on every target the compiler can emit** (`project.tks:1421`:
   `arm64-macos`, `arm64-linux`, `x86_64-linux`, `x86_64-windows`).

Rule 2 is the operational form of the owner's *"não mente"*. A `c_types` name is ONE name used
corpus-wide; if its width differs per target, a single alias would be a lie on some target, and
`#os` cannot save it because **`#os` guards functions only** (`parse_decl.tks:1239`). A type that
fails rule 2 is not "deferred" — it is **not expressible**, and saying so is the honest answer.

### 5.1 IN — the closed list

**Direct aliases (`type c_X = <teko type>`), all SAFE (they carry no address).** Each is exactly
what the owner ratified: *"`type c_int = i32`; não está mentindo"*.

| C type | Teko | form | the demand that admits it |
|---|---|---|---|
| `char` | `type c_char = byte` | alias | `setenv`/`unsetenv`/`getenv` payload octets; `memset`/`memcpy` buffers |
| `signed char` | `type c_schar = i8` | alias | a C API that declares `signed char` explicitly (the only case where the signedness is C's, not the target's) |
| `unsigned char` | `type c_uchar = u8` | alias | same, unsigned |
| `short` | `type c_short = i16` | alias | 16-bit on every target; the `wall_offset_minutes` shape (`src/time/time.tks:137` returns `i16`) |
| `unsigned short` | `type c_ushort = u16` | alias | symmetry with `c_short`; port numbers in any future socket binding |
| `int` | `type c_int = i32` | alias | **the owner's own example.** `setenv`'s `overwrite` and its return; `memset`'s `value`; `abs`; `getpid` |
| `unsigned int` | `type c_uint = u32` | alias | symmetry with `c_int`; flag words |
| `long long` | `type c_longlong = i64` | alias | the 64-bit signed C integer that is 64-bit **everywhere** (unlike `long`) |
| `unsigned long long` | `type c_ulonglong = u64` | alias | `aligned_alloc`'s `alignment`/`size`, `memset`/`memcmp`/`memcpy`'s `count` — i.e. every SIZE in the probes today |
| `float` | `type c_float = f32` | alias | admitted for symmetry with `c_double`; no caller yet — **see the note below** |
| `double` | `type c_double = f64` | alias | `libm` is the first C library any FFI reaches; `f64` is 8 bytes on every target |
| `_Bool` | `type c_bool = bool` | alias | C99 `_Bool` is one byte and Teko's `bool` is `PrimKind::Bool`; needed the moment any C API returns `_Bool` |

**`c_float`/`c_double` honesty note.** These two are the only entries whose caller is *anticipated*
rather than present. They are admitted because they are the two IEEE widths that C fixes by standard
(`float` = binary32, `double` = binary64) and Teko fixes identically (`f32`/`f64`), so rule 2 holds
absolutely and the entry can never become wrong. If the reviewer applies rule 1 strictly, **drop
both** — nothing else in this document depends on them. Flagged rather than smuggled.

**Nominal forms (their own shape, not an alias).**

| C shape | Teko | form | why it cannot be an alias |
|---|---|---|---|
| `char *` (NUL-terminated, Teko-owned) | `pub unsafe type c_string = struct { ptr: ptr<byte>; len: u64 }` | **nominal `unsafe` struct** | §7 — it carries an INVARIANT (`ptr[len] == 0`) and a LIFETIME, neither of which an alias can hold; and an `unsafe` stamp on an alias is **silently inert** (§9.2) |
| an opaque foreign handle (`FILE *`, `pthread_attr_t *`, a library context) | `pub extern unsafe type <Name>` | **`extern type`** — already in the language | `parse_decl.tks:822-824`; lowers to `typedef void *tk_t_<Name>;` (`codegen.tks:9029-9035`) and is admitted by `extern_type_ok` (`typer.tks:5115-5118`). A nominal one-word handle Teko never dereferences. **No `c_types` entry** — this is a per-library declaration, and c_types documents the mechanism, not the instances |
| a pointer that **may be NULL**, in either direction | `ptr<T> \| null` | **a null-union, not a type** | §5.4 [PIN-3]. It is a per-symbol DECLARATION, authored from the C contract, so there is no `c_types` entry to add — the spelling is what c_types teaches |

**`void`, both of its C meanings — NO ENTRY [PIN-2].**

| C shape | Teko | entry? |
|---|---|---|
| `void f(…)` (return position) | `void` | **NO ENTRY.** It is literally the same thing, already spelled in one word, already proven on a raw libc extern (`rt_abort() = "abort" from "c"`) and admitted by an explicit checker arm (`typer.tks:5761-5763`). A `type c_void = void` alias would be a **synonym** — a second name for a thing that has a name and is the same thing. §5.2 records it as excluded |
| `void *` (an address of the unknown) | `ptr<byte>` **or** the opaque `ptr` — never `uptr`. Rule in §5.3 | **NO ENTRY.** The opaque `ptr` lowers to literally `void *` (`codegen.tks:1550`); an alias would again be a synonym |

*Measured, for the record, because the earlier proposal must be answerable rather than merely
withdrawn:* `type c_void = void` **would** have worked (`void` lexes as a plain `Ident` — it is
absent from `TokenKind`, `src/lexer/token.tks:3-60` — so `parse_type` builds a `NamedType`, and
`resolve_named` → `builtin_type("void")` returns `Void{}`, `scope.tks:359`). `type c_void = null`
**would NOT**: `null` in a type position resolves to `checker::Null` (`parse_type.tks:36-45`,
`resolve.tks:1673`), and `extern_type_ok` has no `Null` arm — it falls to `_ => false`
(`typer.tks:5119`), so `extern fn … -> c_void` would be **rejected** with *"an `extern` function
return must be a primitive…, or absent (C7.1a)"*. Had it slipped past the checker, `Null` lowers to
`uint8_t` (`codegen.tks:1555`), which declares `abort()` as `uint8_t abort(void)` — a wrong C
prototype. The owner's revised ruling avoids both.

### 5.2 OUT — and the reason for each

| C type | why it is OUT |
|---|---|
| **`long` / `unsigned long`** | **Fails rule 2, irreducibly.** 64-bit on LP64 (`x86_64-linux`, `arm64-macos`), 32-bit on LLP64 (`x86_64-windows`). One alias would be a lie on half the targets, and `#os` **cannot guard a type declaration** (`parse_decl.tks:1239`). **The honest workaround, which costs nothing:** an FFI author binding a `long` API names the exact width for the target being bound (`c_int` or `c_longlong`) — which is precisely what the corpus already does with bare `u64`, so this is a zero-regression exclusion. **The unblocker, named:** OS/width-guarded type declarations. REPORTED UP, not turned into an issue here |
| **`size_t` / `ssize_t` / `ptrdiff_t`** | **Fails rule 2.** 64-bit on every native target the compiler emits, but Teko has no target-width integer (`PrimKind` is fixed-width only: `type.tks:11-16`), and `uptr` is `uintptr_t`, which is an *address* word, not a *size* word — using it for a length is the same category error §5.3 rejects. **Workaround, already in use:** `c_ulonglong` (`u64`) for the 64-bit targets, which is exactly what `aligned_alloc`/`memset`/`memcmp` declare today. **The unblocker, named:** a `usize`/`isize` primitive. REPORTED UP |
| **`intptr_t` / `uintptr_t`** | Teko's `uptr` already IS `uintptr_t` (`codegen.tks:1553`) and spells it in one word. An alias would be a synonym (M.5: one name, one meaning) |
| **`uptr \| null`** | **Not buildable, and the reason is representational, not stylistic** (§5.4.3): `Uptr` has no arm in `cg_type_is_niche_able` (`codegen.tks:1868-1877`) because an integer has no spare bit-pattern — `0` is data. It lowers to a two-word tagged struct, which is the wrong ABI, silently. Nullability at the boundary is `ptr<T> \| null`, always |
| **`void`** | see §5.1 — a synonym for a thing that already has a one-word name [PIN-2] |
| **`void *`** | ditto: the opaque `ptr` lowers to literally `void *` [PIN-2] |
| **a C FUNCTION POINTER** (`void *(*)(void *)` — `pthread_create`'s `start_routine`) | `extern_type_ok` rejects `Func` outright (`typer.tks:5119`), and **Teko has no way to produce the address of a Teko function** as a C-callable value. The sibling `cargo/20-concorrencia-adiantada` reached the identical conclusion independently and named the answer: `star-ref-and-ffi-0.3.1.md` §4.4 G3's `cabi fn(T…): R` parameter type with coercion from a non-capturing top-level fn — **and explicitly rejected** an `fn_addr -> u64` intrinsic as "o mesmo trocadilho de ABI que o `tk_cov_dump` já teve rejeitado por M.3". c_types must not name a type it cannot construct. **This belongs to the concurrency cargo, not here** |
| **`pthread_t`** | On `x86_64-linux` it is `unsigned long` (rule 2 again); the Windows analogue is a `HANDLE` (`void *`) — a different *kind*, not merely a different width. The concurrency design's answer is the right one and needs no c_types entry: declare the binding **per-`#os` on the FUNCTION** (which `#os` does allow), `u64` in the POSIX arm and the opaque `ptr` in the Win32 arm |
| **struct-by-value C types** (`struct timespec`, `struct stat`, …) | `extern_type_ok`'s `Named` arm admits only an `ExternBody` (`typer.tks:5115-5118`), and `time.tks:21-34` records empirically that a struct across an extern boundary is a **C compile error** under the current codegen. Not expressible; not this issue's to fix |
| **`wchar_t` / `char16_t` / `char32_t`** | No caller anywhere, in this tree or in any sibling cargo. Teko's text is UTF-8 end to end (`teko_rt.h:44`). Pure YAGNI |
| **`long double`, `_Complex`, bit-fields, unions, varargs** | No caller; each is a separate ABI project. `star-ref-and-ffi-0.3.1.md` §4.3 already owns varargs |
| **`errno`** | It is a *macro* over a TLS lvalue, not a type. Teko has no macros (§2.1). Binding it needs `__errno_location()`/`_errno()` per platform — a library-level `extern fn`, not a c_types entry |

### 5.3 `void *` — the RULE, decided with argument [PIN-2 leaves this choice open]

The ruling permits `uptr` **or** `ptr<byte>`. They are not interchangeable, and leaving the choice
to the caller without a criterion is how a type namespace becomes decoration. The rule:

> **A `void *` maps to a POINTER type — `ptr<byte>` when Teko reads or writes the bytes at that
> address, the opaque `ptr` when the address is a token Teko only ever hands back to the same
> foreign library. `void *` NEVER maps to `uptr`. `uptr` is reserved for a word that **C itself
> declares as an integer** (`uintptr_t`/`intptr_t`).**

The argument, in four parts:

1. **`uptr` IS `uintptr_t`, and mapping `void *` to it is an ABI pun.** `Uptr` lowers to literally
   `uintptr_t` (`codegen.tks:1553`); the opaque `ptr` lowers to literally `void *`
   (`codegen.tks:1550`). They are the same width on every target, which is exactly why the pun is
   *silent*. This is the same family of pun the concurrency cargo rejected under M.3 for
   `fn_addr -> u64`, and rejecting it there while accepting it here would be incoherent.

2. **Provenance is the entire product of this layer, and only the pointer type carries it.** The
   probes' 20 address slots typed `u64` (§9.4) are the concrete cost: a length and an address are the
   same type, so swapping them type-checks and the failure is a segfault far from the mistake. A
   `ptr<byte>` does **not** widen from an integer — `ptr_widens_to_opaque` (`resolve.tks:838-848`)
   permits **typed → opaque only**, never integer → pointer and never opaque → typed. Choosing
   `ptr<byte>` is what makes the swap a compile error; choosing `uptr` preserves the bug.

3. **Question 3 (ownership) requires the typed form, and this is decisive.** A lifetime model must
   know an address is an address before it can say anything about it. `Uptr` is not even
   unsafe-carrying today — it falls to `unsafe_carrying_at`'s `_ => false` arm (`resolve.tks:1174`)
   — while `marshall-spec.md` §4's PINNED ruling makes `Ptr` unconditionally unsafe-carrying.
   Spelling FFI payload addresses `uptr` would opt the entire surface **out** of the one containment
   gate being built for it. That alone settles it.

4. **The precedent already runs this way, from both ends.** `marshall-spec.md` §8.4 ratified
   *"Handles are TYPED POINTERS, not words… the `to_uptr`/`from_uptr` bridge remains for genuine
   WORD-transport seams — transport, not storage"*, and put the shipped `Arena.region: uptr` (D35) on
   notice for re-tagging. The concurrency design independently chose `ptr<byte>` from
   `teko::mem::buf_ptr` for `pthread_create`'s `void *arg` — correct under this rule, because those
   context bytes ARE read back via `bytes_from_ptr`. Three documents, one rule.

**Applied to the corpus:** every `u64` in `full_gate.tks`/`control.tks` that holds an address becomes
`ptr<byte>` (the bytes are read and written); `Arena.region` and `region_new()`'s result are the
opaque-token case and would become the opaque `ptr` under marshall-spec §8.4's re-tag — **that
re-tag is #498's, not this issue's**, and is left alone.

**One honest exception, and the probe already argued it.** `arena_bottom/src/bottom.tks:8-12` records
why the arena floor carries `u64` and not `uptr`: *"the checker defines no `uptr <-> u64` cast
(`cast_kind` admits only `Prim` and `Byte`), and an arena IS address arithmetic: without a numeric
carrier there is no bump pointer."* That argument survives this rule intact, because `ptr<byte>` has
**no arithmetic either** — the raw-pointer operators are designed in `marshall-spec.md` §5.5 and are
**unbuilt**. So `bottom.tks`/`word.tks`'s arena floor stays on `u64` until #498's operator crumb
lands; only the non-arithmetic bindings (`setenv`/`unsetenv`/`getenv`) migrate in §10 C8. Naming this
is the difference between a rule and a wish.

### 5.4 Nullability at the boundary [PIN-3] — the ruling, measured

**What the ruling closes.** `regressor.tkr:87-89` states, verbatim: *"FFI `ptr<T> | null`/`uptr |
null` (§3.1 'FFI') is NOT authored — there is no existing in-tree `ptr<T>`/`uptr` USAGE pattern to
model a snippet against safely; **flagged for the architect/owner, not guessed**."* That flag now has
its answer, and the pin can be promoted to a positive scenario when §5.4.3's crumb lands — which the
file itself anticipates: *"The day support lands each pin fails LOUDLY, forcing a conscious
promotion."*

#### 5.4.1 The defect the ruling repairs, in the tree's own words

`arena_bottom/src/bottom.tks:16-18`:

```teko
 * @return           the block's base address, or 0 on failure
pub extern fn c_aligned_alloc(alignment: u64, size: u64): u64 = "aligned_alloc"
```

and its caller, `bottom.tks:152-157`:

```teko
pub fn acquire_chunk(): u64 {
    let block = c_aligned_alloc(arena_align(), default_chunk_bytes())
    demand(block != 0, 10)
    ...
```

The nullability lives **in a comment**; the check is **manual and voluntary**. Omit the `demand` and
the program writes to address zero, silently. It is the identical shape as `c_setenv`'s
NUL-termination obligation (§1): a contract in prose that nothing enforces.

And the doc-comment's own defence of the idiom is worth quoting, because it explains why the shape
looked right: *"A `u64` return is ABI-identical to `void *` on every 64-bit target — both arrive in
the integer return register — so this is a faithful binding, not a workaround."* **That is true about
the machine and lacunary about the type system**, and the gap between those two sentences is exactly
what this layer exists to close. Faithful to the ABI is not the same as honest to the reader.

With `-> ptr<byte> | null`, the checker forces the match before the value is usable, and the before/
after is demonstrable on this very file (§11, `c_null_union_forces_check`).

#### 5.4.2 Nullability is DECLARED, never derived — and this must be written down

The C type `void *` says nothing about whether NULL is possible. *"`malloc` returns NULL on
failure"*, *"`getenv` returns NULL when unset"*, *"`pthread_create`'s `attr` may be NULL to request
the default"* — all three are **semantic knowledge, per function, from the C library's contract**,
not syntax the compiler can read. There is nothing to infer from, and a compiler that guessed would
be inventing a guarantee.

So `| null` is **information the FFI author supplies by declaration**. That is exactly coherent with
the owner's framing — *"quem escreve um FFI espera por isso e deve conhecer a representação em teko
também"* — and it is stated here explicitly so nobody later tries to derive it. It is the same class
of authored knowledge as the bound in §6: the developer holds the fact, and the type system's job is
to make stating it mandatory at the point where it matters and impossible to forget afterwards.

#### 5.4.3 `ptr<T> | null` is buildable; `uptr | null` is NOT — and the reason is representational

The owner's sentence offers two spellings. Measurement decides between them, and it decides cleanly.

| spelling | resolves? | lowers to | verdict |
|---|---|---|---|
| `ptr<T> \| null` | **yes** — `Ptr` falls to `variant_member_admissible`'s permissive arm (`resolve.tks:1657`) | a **bare `T *`, with NULL meaning `null`** — `cg_type_is_niche_able` has an explicit `checker::Ptr => true` arm (`codegen.tks:1870`), and the emit path returns the niche member's own C type (`codegen.tks:1532-1534`) | **THE spelling.** Byte-identical to a plain `ptr<T>`: same one word, same register, zero overhead. The null test lowers to `(x == 0)` and the null literal to `0` (`codegen.tks:6357`, `:5891`) |
| `uptr \| null` | resolves, but | `Uptr` is **absent** from `cg_type_is_niche_able` (`codegen.tks:1868-1877`) — it has no spare bit-pattern, because for an integer `0` is **data**, not a hole. It therefore falls through to the boxed/tagged rail and lowers to a **tagged union struct** | **NOT usable at the boundary.** Two words where C expects one: the wrong ABI, silently |

> **The rule, therefore: nullability at the C boundary is spelled `ptr<T> | null`, always. A `uptr`
> never takes `| null`** — not by preference but because it cannot: an integer has no spare
> bit-pattern, so `0` in a `uptr` is a legal value and `| null` would be claiming a distinction the
> representation does not have.

**This converges with §5.3 from a second, independent direction, and that convergence is the
strongest evidence in this document.** §5.3 chose `ptr<byte>` over `uptr` on provenance and
containment grounds, before nullability was on the table. The niche table then turns out to admit
**only** the pointer type. Two unrelated parts of the compiler — the unsafe-containment walk and the
codegen's null-union representation — reward the same choice. A rule that two subsystems agree on
independently is a rule, not a taste.

**Reading the ruling's phrasing.** The coordinator read *"`uptr | null` ou `ptr<T> | null` nos
opacos"* as possibly meaning "`ptr<T>` for typed opaques, `uptr` for the rest". My reading differs,
and here is why: the measurement makes `uptr | null` **not buildable**, so the two spellings are not
a choice between styles — one of them does not lower correctly. I take the ruling's intent to be
*"nullability must be in the TYPE, not in a comment"*, and I satisfy that intent with the one
spelling that carries it. Where a value is a genuine `uintptr_t` integer that may be zero, zero is
ordinary data and is compared as data — no union, no claim.

**RATIFIED (owner, 2026-07-27).** The measurement reached him and he ruled for this reading:
`ptr<T> | null` is THE spelling at the boundary, always. `uptr | null` is out — not by preference
but because it does not lower correctly, and honouring it literally would require a niche arm for
`Uptr` that is a *fiction* (an integer has no unused encoding: `0` is data). This section is no
longer a proposal awaiting a decision; the rest of the document may be read as settled on this
point, and a reviewer who finds `uptr | null` anywhere in the tree should treat it as a defect.

#### 5.4.4 The gap, named: `extern_type_ok` rejects a union today

`extern_type_ok` (`typer.tks:5109-5121`) admits `Prim`, `Byte`, `Ptr`, `Uptr`, and an `ExternBody`
`Named`. **`Variant` falls to `_ => false`** — so `extern fn f(…): ptr<byte> | null` is **rejected
right now** with *"an `extern` function return must be a primitive (int/float/bool), `byte`, `ptr`,
`uptr`, an `extern type` handle, or absent (C7.1a)"*. This is precisely the missing in-tree usage
pattern `regressor.tkr:87-89` refused to guess at.

**It is a crumb, not an impediment** (§10 C2b), and it is a small one with an unusually strong
safety argument: admit a `Variant` **only** when it is a two-member absence union whose niche member
is itself already admissible — i.e. exactly when `cg_union_niche_member` returns a type that passes
`extern_type_ok`. Under that condition the C type emitted is **literally unchanged** (`codegen.tks:
1532-1534` emits the niche member's own type), so the ABI cannot shift. `uptr | null` is excluded by
the same predicate, automatically, for the right reason — no special case, no hard-coded name.

#### 5.4.5 The scan — where `| null` goes, in both directions

The ruling covers *"de ou para C"*, so both directions are scanned. Every row is the C library's
documented contract; **each is authored knowledge, per §5.4.2, not inferred.**

**RETURN direction (C hands Teko a pointer that may be NULL):**

| symbol | today | becomes | why |
|---|---|---|---|
| `aligned_alloc` | `-> u64`, `demand(block != 0)` by hand (`bottom.tks:18,154`) | `-> ptr<byte> \| null` | NULL on allocation failure — **the canonical case** |
| `getenv` | `-> u64` (`arena_teko/src/control.tks:8`) | `-> ptr<byte> \| null` | NULL when the variable is unset — the most-forgotten NULL in all of libc |
| `memset` / `memcpy` | `-> u64` | `-> ptr<byte>` (no `\| null`) | returns its own `dest` argument; never NULL |
| `setenv` / `unsetenv` / `memcmp` / `getpid` / `abs` | `-> i32` | `-> c_int` | no pointer in the return |

**PARAMETER direction (Teko hands C a pointer that may legitimately be NULL):**

| symbol | parameter | becomes | why |
|---|---|---|---|
| `free` | `block` | `ptr<byte> \| null` | `free(NULL)` is a **defined no-op** — the probe's own doc already says so (`bottom.tks:25`, *"0 is a documented no-op"*), again in prose |
| `pthread_create` | `attr` | `ptr<…> \| null` | NULL **is** how the default attributes are requested — a nullable parameter is not an error path here, it is the normal call |
| `pthread_create` | `arg` | `ptr<byte> \| null` | a NULL context is legal and common |
| `setenv` / `unsetenv` | `name`, `value` | `ptr<byte>` (**no** `\| null`) | NULL is EINVAL/UB — declaring it nullable would state a permission the API does not grant |
| `memset` / `memcpy` / `memcmp` / `strlen` | the buffers | `ptr<byte>` (**no** `\| null`) | UB on NULL |

**The asymmetry is the point.** `| null` is not decoration applied uniformly to every pointer; it is
a *claim about one specific API's contract*, and roughly half the pointers above must NOT carry it.
A blanket `| null` would be as dishonest as the current blanket `u64` — it would force a match at
every call site including the ones where NULL is undefined behaviour, teaching the reader that the
annotation carries no information. **Declared, per symbol, from the C contract — that is the whole
discipline.**

---

## 6. Question 2 — the buffer with no terminator, on its own merit

**The question stands apart from PIN-1 and must not inherit it by analogy.** PIN-1's failure mode is
a *wrong value*: the string truncates, the program continues, the result is observable and contained.
This one's failure mode is a *read past the end of a mapping* — the damage is not contained in the
value, and "cabe ao desenvolvedor" cannot mean "and the process may read arbitrary memory".

But the honest other half is that **the compiler cannot know the bound.** Only the C API's contract
knows how long that buffer is. So the answer cannot be a compiler-supplied limit either.

**The resolution: the bound is a REQUIRED PARAMETER.** The developer supplies the number — it is
knowledge only they have — but the signature makes omitting it impossible. That is precisely the
split the owner's framing demands: `unsafe` enables the capability; the signature guarantees
totality.

**This is not a new principle; it is the one the house already ratified for bytes.**
`teko::mem::bytes_from_ptr(p, n)` has taken a mandatory `n` since C7.1a (`scope.tks:638-642`,
`teko_rt.c:178`). Strings were simply the omission — `str_from_cstr(p)` takes no bound and calls
`strlen` on a foreign pointer (`teko_rt.c:167`). **That is a live, shipped, unbounded foreign read**,
and under NO-DEFERRAL (`teko-laws-digest.md`: *"Toda falha achada … é resolvida AGORA, in-wave"*) it
is fixed in this wave, not noted for later. Cost of fixing it: **zero migration** — the builtin has
zero callers in the entire tree (§2, measured).

```teko
/**
 * Copies a NUL-terminated C string from a foreign address into a fresh Teko `str`, reading AT MOST
 * `max` octets. The bound is REQUIRED and it is the caller's assertion: only the C API's own
 * contract knows how long that buffer is, so the compiler cannot supply it — but the signature
 * makes omitting it impossible, which is the whole safety property. Replaces the unbounded
 * `teko::mem::str_from_cstr`, whose `strlen` on a foreign pointer could scan past the end of the
 * mapping.
 *
 * TOTAL on every input, with no undefined case:
 *   * `p` is null            -> the empty `str` (never a read)
 *   * a NUL at octet i < max -> the first `i` octets (the ordinary case)
 *   * no NUL in [0, max): exactly `max` octets, and the result is NOT the prefix of a C
 *                              string. This is TRUNCATION, not a panic, and deliberately so: a C
 *                              API may legitimately hand back a FIXED-WIDTH, un-terminated field
 *                              (a `utmp.ut_line`, a fixed-size id array), and panicking would make
 *                              that shape unexpressible. The read stayed inside the bound the
 *                              caller declared, so it is memory-safe by that assertion.
 *   * `max` is 0             -> the empty `str` (never a read)
 * The ONE residual — "is `p` really readable for `max` octets?" — is irreducibly the caller's, and
 * it is the SAME residual `unsafe` already owns everywhere (marshall-spec.md §5.1's
 * region-membership row). It is stated here, not hidden.
 *
 * @param ptr<byte> p  a foreign address the caller asserts is readable for `max` octets
 * @param u64 max  the maximum number of octets to read — the caller's declared bound
 * @return str  a fresh Teko `str` copied out of the foreign buffer
 * @see teko::mem::bytes_from_ptr  the already-bounded byte twin this mirrors
 * @since 0.3.1
 */
pub unsafe fn str_from_c(p: ptr<byte>, max: u64): str
```

**Where it lives, and why it is a builtin rather than Teko code.** It CANNOT be written in Teko
today: `*` is not an operator (`star-ref-and-ffi-0.3.1.md` §0 — *"`*` operator: NOT an operator —
glyph FREE"*), so a byte-scan through a `ptr<byte>` is inexpressible. It therefore stays a compiler
builtin under `teko::mem`, replacing `str_from_cstr`: one arity change in `scope.tks:637`, one
mapping in `codegen.tks:3684`, one runtime function in `teko_rt.{c,h}` (maintained C, permitted by
the Teko-only law's stated exception). `teko::c_types` re-exports nothing; it documents the pairing.

---

## 7. Question 3 — POSSE: `c_string` is a HANDLE, and the ABI decides it, not taste

### 7.1 The shape is forced, in three steps

1. A `const char *` parameter is **one machine word**. Any Teko struct is the wrong ABI shape.
2. The checker **refuses** a struct in an extern signature (`extern_type_ok`'s `Named` arm needs an
   `ExternBody`, `typer.tks:5115-5118`), and `time.tks:21-34` records that even where a struct is
   admitted the nominally-distinct C typedefs make it a **C compile error**.
3. Therefore `c_string` is **not the thing that crosses**. It is the thing that **owns the bytes the
   crossing word points into**. An owner with a lifetime is a handle, not a value.

```teko
/**
 * A NUL-terminated C string OWNED BY TEKO — the safe-side carrier of the one invariant a
 * `const char *` has and a Teko `str` does not: `ptr[len] == 0`. It is a HANDLE, not a value, and
 * the ABI forces that: a `const char *` parameter is ONE machine word, and the checker refuses a
 * struct in an `extern` signature (C7.1a, `typer.tks:5109`), so this type NEVER crosses the
 * boundary — only the single word `c_string_ptr` extracts from it does.
 *
 * It is `unsafe type` and NOT an alias, and that is load-bearing twice over. A `type` alias is
 * TRANSPARENT (`resolve.tks:794`): it resolves THROUGH to its target, so an `unsafe` modifier on a
 * structurally-typed alias is SILENTLY INERT (the stamp is looked up on a `Named`,
 * `resolve.tks:1158`, and the alias's own name never survives resolution). Only a NOMINAL struct
 * body keeps the stamp — which is what makes a `c_string` field in a safe struct trip the SAME
 * gate `c55_unsafe_field_in_safe_struct` pins, and a `c_string` in a safe signature trip
 * `reject_unsafe_signature_contagion` (`collect.tks:39`).
 *
 * LIFETIME: a `c_string` owns bytes INSIDE a region it did not open, so it is NOT `#must_free` and
 * has no destructor — a bump region frees in bulk, never per-object. The free obligation lives on
 * the REGION handle (`teko::mem::unsafe::Arena`, already `#must_free`), never here. Which region is
 * NAMED at construction and never inferred: see `c_string_for_call` and `c_string_in_arena`.
 *
 * @see docs/design/c-types-and-marshalling-0.3.1.md §7
 * @since 0.3.1
 */
pub unsafe type c_string = struct {
    /**
     * The base address of a `len + 1` octet buffer whose final octet is 0. The buffer lives in the
     * region named at construction and is valid exactly as long as that region is.
     */
    ptr: ptr<byte>

    /**
     * The payload length in octets, EXCLUDING the terminator — i.e. the length of the Teko `str`
     * this was built from. It is deliberately NOT "what C will see": if the source `str` held an
     * embedded NUL, C stops at that NUL and reads FEWER octets than `len` (owner ruling: the
     * truncation is the developer's, and there is nothing the compiler can do). Anything that needs
     * "what C saw" must read back THROUGH the terminator with `teko::mem::str_from_c`, never
     * through this field.
     */
    len: u64
}
```

### 7.2 Ownership answered with the model, not with an intention

**The current state, measured:** `tk_cstr_dup` (`teko_rt.c:157-164`) `malloc`s the copy, and that
buffer is **never freed anywhere** in the runtime — the C comment says *"the caller owns the buffer
(whole-program lifetime in the seed)"*. So today every `as_cstr` is a permanent leak, **outside the
arena**, invisible to the memory model. That is not a policy; it is an unstated one, and it is what
the design replaces.

**The model already has the tiers. The answer is to NAME one at the conversion site.** There is no
default and no inference, because the caller is the only party who has read the C API's contract
about whether the pointer is retained:

| tier | mechanism | valid until | correct when |
|---|---|---|---|
| **enclosing region** | `teko::mem::buf_ptr(len)` — bump-allocates into the enclosing region (`codegen.tks:3171-3186`) | the enclosing region dies | **C reads during the call and forgets.** POSIX `setenv`/`unsetenv` COPY both arguments; `fopen`, `printf`, `strlen`, `abs` all read-and-forget. **This covers every raw-libc extern measured in this tree** |
| **dev-named region** | `unsafe #must_free type Arena` + a named-region byte allocation | `teko::mem::free(a)` | **C RETAINS the pointer** for a span the developer knows: `putenv`, a registered callback context, a library that stores the string. `#must_free` makes leaking the region a COMPILE error, so this tier is leak-barred by an existing gate rather than by discipline |

There is **no third tier and no "forever"**, and that is a real, named limitation rather than an
omission: `#must_free` requires a free on every path, so a deliberately-immortal `c_string` is not
expressible without defeating the gate. If a C API retains a pointer for the life of the process,
the honest shape is an `Arena` freed at the top of `main`'s exit path — the lifetime is stated, not
wished.

```teko
/**
 * Copies `s` into a fresh NUL-terminated buffer in the ENCLOSING region and hands back the owning
 * `c_string` — tier 1. The result is valid for exactly as long as the enclosing region, so it is
 * correct when the C side READS DURING THE CALL AND FORGETS (POSIX `setenv`/`unsetenv` copy both
 * arguments; `fopen`, `printf`, `strlen` read and return). **If the C side RETAINS the pointer, this
 * is the wrong constructor** — use `c_string_in_arena` and the failure becomes a lifetime the
 * developer controls instead of a dangling read.
 *
 * TOTAL: every `str` produces a `c_string`; there is no failure path and no `| error`. An EMBEDDED
 * NUL in `s` is copied verbatim, so the C side sees a TRUNCATED string — the octets up to the first
 * NUL and no further. That is stated, not prevented: owner ruling, *"cabe ao desenvolvedor e não ao
 * compilador, C verá truncado e não há o que fazer"*. The buffer still holds all `len` octets and
 * `len` still counts them; only C's view is short. The empty `str` yields a valid 1-octet buffer
 * holding just the terminator, which is what C expects for `""`.
 *
 * @param str s  the string to copy across the boundary
 * @return c_string  an owning handle to a NUL-terminated copy in the enclosing region
 * @since 0.3.1
 */
pub unsafe fn c_string_for_call(s: str): c_string {
    c_string { ptr = teko::mem::as_cstr(s); len = s.len }
}

/**
 * Copies `s` into a fresh NUL-terminated buffer in the DEV-NAMED region `a` and hands back the
 * owning `c_string` — tier 2. The result is valid until `teko::mem::free(a)`, so it is the
 * constructor for a C API that RETAINS the pointer (`putenv`, a registered callback context, any
 * library that stores the string). Leaking is already barred: `Arena` is `#must_free`, so a path
 * that drops `a` without freeing it is a COMPILE error (`src/mem/unsafe/arena.tks`).
 *
 * TOTAL under exactly the same terms as `c_string_for_call` — same embedded-NUL truncation, same
 * absence of a failure path. The ONLY difference is which region owns the octets, and that is the
 * entire reason the two constructors are separate names rather than one with a flag: the lifetime
 * is visible at the call site (M.5).
 *
 * @param Arena a  the manual region that will own the copy until it is freed
 * @param str s  the string to copy across the boundary
 * @return c_string  an owning handle to a NUL-terminated copy inside `a`
 * @since 0.3.1
 */
pub unsafe fn c_string_in_arena(a: Arena, s: str): c_string {
    c_string { ptr = teko::mem::region_buf(a.region, s.len + 1, s); len = s.len }
}

/**
 * Extracts the single machine word an `extern fn` actually receives — the base address of the
 * NUL-terminated buffer. This is the ONE hop across the boundary, and it is deliberately a named,
 * greppable function rather than an implicit coercion: the `c_string` itself can never be an extern
 * parameter (C7.1a rejects a struct, `typer.tks:5115`), so something must do the extraction, and
 * making it visible is the whole of M.5.
 *
 * @param c_string s  the owning handle
 * @return ptr<byte>  the base address of `s`'s NUL-terminated buffer, valid for `s`'s region
 * @since 0.3.1
 */
pub unsafe fn c_string_ptr(s: c_string): ptr<byte> {
    s.ptr
}
```

### 7.3 The two seams `c_string_in_arena` needs, stated plainly

* **`teko::mem::as_cstr` must return `ptr<byte>`, not the opaque `ptr`.** Today it returns
  `Ptr{inner=null}` (`scope.tks:636`), and `ptr_widens_to_opaque` (`resolve.tks:838-848`) permits
  **typed → opaque only** — so an opaque `ptr` cannot initialize a `ptr<byte>` field. One-line
  retype; strictly more honest (it does return a byte buffer); zero callers to migrate.
* **`teko::mem::region_buf(region: uptr, len: u64, init: str): ptr<byte>` does not exist and must
  be added.** `buf_ptr(len)` allocates into the ENCLOSING region only; `region_alloc(region, init)`
  puts ONE value into a named region (`typer.tks:978-1000`). Neither gives "`len` contiguous octets,
  NUL-terminated, in region `r`". The new builtin is the named-region twin of `buf_ptr`, lowering to
  `tk_region_alloc(r, len)` + the copy + the terminator — the same body `tk_cstr_dup` already has,
  with `malloc` swapped for `tk_region_alloc`. This is crumb C5 and it is the one place this design
  adds a builtin.

### 7.4 The residual, named rather than papered over

The type-level guarantee this layer delivers is **on the Teko side**: a `c_string` value cannot be
constructed except by a constructor that NUL-terminates. The **last hop** — `c_string_ptr(s)` →
`ptr<byte>` → the extern — is a plain `ptr<byte>`, and a determined caller can bypass it by passing
`teko::mem::as_ptr(s)` (a raw, **non**-terminated view) to the same parameter. That would type-check.

Closing that hop would require the extern's parameter to be a distinct nominal type. The only nominal
type `extern_type_ok` admits is an `extern type` (`typer.tks:5115-5118`) — and an `extern type` has
**no literal form and no conversion**, so Teko could name it but never construct one. The alternative
was considered and it is blocked, not merely unchosen.

So the residual is **one greppable call site per extern** (`c_string_ptr(…)`), and it is stated here
rather than claimed away. A future lint — *"every raw-libc `ptr<byte>` extern argument is fed by
`c_string_ptr` or `buf_ptr`"* — would close it; that is REPORTED UP, not invented here.

---

## 8. Question 6 — the shape of the marshalling, and what makes it TOTAL

**Free `unsafe fn`s in the `teko::c_types` namespace.** Not a method, not an implicit coercion, not a
cast. The matrix:

| candidate | verdict |
|---|---|
| **implicit coercion at the boundary** | **REJECTED, and the checker already agrees.** There is no coercion site to hang one on — `extern_type_ok` rejects the signature before any conversion could be considered (§3). Worse, an implicit `str → c_string` would hide the copy AND the region choice, and the region choice is the entire answer to Q3 (M.5: you see the copy) |
| **a `to` / `as` cast** | **REJECTED.** `to` is a checked VALUE conversion and `as` is the "cast shadow" M.3 forbids for reinterprets. `str → c_string` is neither: it is an allocation **plus** a copy **plus** a lifetime decision. Spelling it a cast hides all three |
| **methods / static factories** (`c_string::for_call(s)`) | Viable — the house has the shape (`Arena::new`, `Owned<T>::make`), and a method on an `unsafe type` inherits its unsafe-ness (`collect.tks:171-173`). **Not chosen** only for consistency with the RATIFIED sibling: `marshall-spec.md` §6 resolved the identical axis law-first in favour of namespace functions, and two boundary namespaces spelling the same thing two ways is its own divergence |
| **free namespace fns** | **CHOSEN.** Zero grammar. Maximally greppable (`c_types::`, one name per operation). Direction and lifetime both visible in the name (`for_call` vs `in_arena` vs `str_from_c`), which is what M.2/M.5 ask for. Precedent-locked by `marshall-spec.md` §6 and by the shipped `teko::mem::*` FFI primitives |

**Totality — the claim, input by input.** Every operation in the boundary set has a defined result
for every input:

| operation | input | result |
|---|---|---|
| `c_string_for_call` / `c_string_in_arena` | any `str` | a `c_string`. Always |
| " | the empty `str` | a valid 1-octet buffer holding only the terminator |
| " | a `str` with an embedded NUL | a `c_string`; C sees the prefix. **[PIN-1]** Truncation, documented, no error path |
| `c_string_ptr` | any `c_string` | its base address. Infallible |
| `str_from_c` | `p` null | `""`, no read |
| " | NUL at `i < max` | the first `i` octets |
| " | no NUL in `[0, max)` | exactly `max` octets (§6) |
| " | `max == 0` | `""`, no read |
| every `c_*` alias | any value of the aliased Teko type | itself — an alias is the identity (`resolve.tks:794`) |
| a `ptr<T> \| null` extern result | NULL from C | the `null` arm, which the checker **forces** the caller to handle before any use (§5.4) |
| " | any non-NULL address | the `ptr<T>` arm, bit-identical to the returned word (the niche rail, `codegen.tks:1532-1534`) |

**No input to any operation in `teko::c_types` produces undefined behaviour.** The residuals that
remain are exactly two, both irreducible and both named: *is `p` readable for `max` octets* (§6), and
*is the region still alive* (§7.2) — the same residual `unsafe` already owns across the whole
vertical (`memory-unsafe-backend-remodel.md` §2c).

---

## 9. What can go wrong in SILENCE — the obligatory section

Silent divergence is this house's characteristic failure. Nine, ordered by how quietly they fail.

### 9.1 `c_string.len` ≠ `strlen(c_string.ptr)` — PIN-1's second-order shadow
The truncation itself is ruled (developer's). The *silence* is downstream: anyone round-tripping a
`c_string` back to a `str` **through `.len`** recovers the FULL payload, including the octets C never
saw. The round trip "succeeds" while the C side acted on a shorter string, and nothing differs.
**Mitigation, built in:** the read-back path is `str_from_c` (through the TERMINATOR), never `.len`,
and the `len` field's own doc-comment says so in full (§7.1).

### 9.2 An `unsafe` modifier on a STRUCTURAL alias is SILENTLY INERT — a live checker hole
`type X = <structural type>` resolves THROUGH (`resolve.tks:794`), so `resolve_type` returns the
target and the alias's own name never survives. `unsafe_carrying_at` looks the stamp up on a `Named`
(`resolve.tks:1158`), which no longer exists. So:

```teko
pub unsafe type Danger = ptr<byte>       // compiles; the `unsafe` does NOTHING
pub fn safe_fn(d: Danger): u64 { 0 }   // ACCEPTED — the gate never sees an unsafe type
```

`validate_field_slot` (`collect.tks:2067`) checks the alias's RHS for contagion, so an alias to an
unsafe **Named** type is still caught (the target is nominal). The hole is **structural targets
only** — `ptr<T>`, `[]T`, `T?`, a union. **This is why `c_string` is a nominal struct** (§7.1), and
it is a **REPORTED FINDING**, not mine to file.

### 9.3 The adjacent `extern fn` return-narrowing defect — mentioned, not incorporated
`arena_bottom/src/bottom.tks:50` records that the native backend does not narrow an `extern fn`'s
declared `i32` return, which is why the probes declare `u64` and narrow by hand.
`cargo/20-extern-return-narrowing` is closing it in parallel. **This design does not depend on the
fix and is not shaped around it.** `type c_int = i32` is the identity, so a `-> c_int` return has
*exactly* the same defect and *exactly* the same fix as a `-> i32` return: being an alias, c_types
neither creates nor cures it. The one interaction worth writing down is a scheduling one — a fixture
asserting a `c_int` return value is asserting the sibling's fix, so §11 marks those fixtures and they
should land after it, or assert a value that is width-insensitive.

### 9.4 Address-as-`u64`: a length and an address are the same type today
The probes carry **20 address slots typed `u64`** at HEAD, enumerated so the number is auditable:
`arena_bottom/src/bottom.tks` — 6 (`aligned_alloc` ret; `free` param; `memset` param + ret; `memcmp`
2 params); `arena_teko/src/word.tks` — 9 (the same 6 plus `memcpy`'s 2 params + ret);
`arena_teko/src/control.tks` — 2 (`getenv` param + ret); `arena_teko/src/full_gate.tks` — 3
(`setenv` 2, `unsetenv` 1). Re-derive with `rg -n 'extern fn' examples/probes --glob '*.tks'`.

Passing a length where an address was wanted **type-checks and runs**, and the failure is a segfault
far from the mistake. `ptr<byte>` closes it because no integer widens into a pointer
(`ptr_widens_to_opaque`, `resolve.tks:838-848`, permits typed→opaque only). This is the concrete,
countable win the layer buys, and §5.3's rule is what banks it.

### 9.5 `as_cstr`'s buffer is a permanent leak, and nothing warns
`tk_cstr_dup` `malloc`s and nothing frees it (`teko_rt.c:157-164`; no matching `free` in the file).
It is invisible today **only** because the builtin has zero callers. If the implementer *adds*
`c_string_for_call` alongside a still-live `as_cstr`, the leak survives and no gate fires. **Hence
crumb C7 RETIRES `as_cstr`; it does not merely add a sibling.**

### 9.6 `bytes_from_ptr` over-reads a short buffer, silently
`tk_bytes_from_ptr` handles a NULL pointer (`{buf, (n && p) ? n : 0}`, `teko_rt.c:178-184`) but not a
non-null buffer shorter than `n`. Same class as §6, and it **cannot** be fixed — the length is
knowledge only the caller has. **Document, do not "repair"**: a repair here would be a fabricated
guarantee, which is worse than the honest gap.

### 9.7 A NULL contract that lives only in a doc-comment is enforced by nobody
`acquire_chunk` (`bottom.tks:152-157`) checks its allocator result with a hand-written
`demand(block != 0, 10)`. **Delete that line and the code compiles, links, runs, and writes to
address zero.** The same shape appears three more times in the same probe family (`free`'s
documented NULL no-op, `getenv`'s unset-variable NULL, `memset`'s never-NULL return), and every one
of them is a comment. This is [PIN-3]'s entire justification, and it is measurable: §5.4.5 lists ten
pointer slots, of which **four carry a real NULL contract that nothing enforces today**.

The second-order silence, worth stating because it is the trap in the fix: a `| null` applied
uniformly would be *equally* silent, in the opposite way — an annotation on a pointer where NULL is
undefined behaviour trains the reader to treat every match as ceremony. §5.4.5's asymmetry is
load-bearing, not fussiness.

### 9.8 `uptr | null` would compile and mis-ABI in silence
It resolves (`variant_member_admissible` is permissive for `Uptr`, `resolve.tks:1657`) but is not
niche-able (`codegen.tks:1868-1877`), so it lowers to a **two-word tagged struct** where C expects
one word. Nothing in the current checker stops the declaration — `extern_type_ok` rejects it today
only because it rejects *every* `Variant` (§5.4.4), i.e. **for the wrong reason, by accident.**
C2b's predicate must exclude it *deliberately*, via the niche test, so that the day someone widens
the extern gate the exclusion survives on its own merit. `c_uptr_null_union_extern_rejected` (§11)
exists precisely to pin that.

### 9.9 A per-target width alias would diverge silently, per target
Recorded so nobody "helpfully" adds `c_long`/`c_size` later: their widths differ across emittable
targets (§5.2), `#os` cannot guard a type declaration, and a wrong-width alias produces a **wrong
value on one platform and a correct one on another** — the worst possible signal. The exclusion is
load-bearing, not a gap.

---

## 10. Crumb sequence

Smallest safe steps, each independently gate-able. Every crumb uses only features already in the
current seed (transparent aliases, `unsafe type` structs, `pub unsafe fn`, `extern unsafe fn`,
`ptr<byte>`, the shipped `teko::mem` builtins), so **nothing here is blocked by a bootstrap
ordering** — the two crumbs that add compiler surface (C2, C5) put it in the compiler, and only the
crumbs *after* them use it from the corpus.

**C0 — the doc-comment correction + the spelling probe. [UNBLOCKED]**
Fix `parse_decl.tks:206-210`: it asserts an extern fn cannot be unsafe, while the code threads
`is_unsafe` into the extern `Function` node (`:373`). No code change; the contract stops lying.
*Delivers:* the frontier's spelling, confirmed and citable. *Unblocks:* every later crumb marks its
externs `unsafe` on evidence rather than hope. *Fixture:* `c_extern_unsafe_accepted`.
*I could not build (the assignment forbids it) — the fixture IS the measurement.*
**Ritual: full gate** (it touches the parser's documented contract).

> **ONDE O MÓDULO MORA — corrigido em campo, 2026-07-27.** O módulo saiu de `src/c_types/` para
> `staged/c_types/`, fora do `source = "src"` do `teko.tkp`. Não é recuo de desenho: o arquivo está
> inteiro e na grafia final. É a escada. Quem tipa `src/` é o SEED 0.3.0, e o módulo consome
> `region_buf` (builtin novo) e o `as_cstr` retipado — ambos nascidos NESTA carga. Os cinco portões
> ficaram vermelhos em 994bcc4, em todos os hosts, exatamente nisso. A regra que a carga irmã
> (`#arena_size`) já tinha enunciado vale aqui sem exceção: uma carga que ensina um builtin novo é
> ADITIVA — entrega o lado COMPILADOR (`checker/scope.tks`, `codegen/codegen.tks`, que compilam sob
> o seed por serem código novo e não uso novo) e `src/` NÃO adota. As sete fixtures não dependem do
> módulo: cada uma carrega uma cópia local declarada como tal, e é por elas que os builtins novos
> são exercitados através do gen1. Reentrada em `src/c_types/`, sem edição, no primeiro degrau cujo
> seed já traga `as_cstr -> ptr<byte>` e `region_buf`.

**C1 — `staged/c_types/c_types.tks`: the alias inventory ONLY. [UNBLOCKED]**
The twelve aliases of §5.1, full Javadoc each, plus a module doc-comment carrying §5.2's exclusion
table verbatim (the reasons must live in the code, not only here). Zero checker, zero codegen.
*Delivers:* the layer's safe half. *Also delivers the §2.1 measurement:* it is the first
alias-to-a-bare-primitive in the corpus. *Sequenced first deliberately* — if that assumption is
wrong, the design fails here and cheaply. *Fixtures:* `c_types_alias_identity` (accept),
`c_types_alias_no_implicit_widen` (reject).

**C2 — `str_from_c`, the bounded inbound scan; retire `str_from_cstr`. [UNBLOCKED]**
Runtime (maintained C, permitted): replace `tk_str_from_cstr(const void *)` with
`tk_str_from_c(const void *, uint64_t)` in `teko_rt.{c,h}`. Checker: `scope.tks:637` becomes
`(ptr<byte>, u64): str`. Codegen: `codegen.tks:3684` remaps. Zero callers to migrate (§2).
*Delivers:* Question 2's answer, and closes a live unbounded foreign read.
*Fixtures:* `c_str_from_c_totality` (all four cases, one exit code).
**Ritual: full gate** (a shipped builtin's arity changes).

**C2b — admit a NICHE null-union as an extern param/return. [UNBLOCKED]**
`extern_type_ok` (`typer.tks:5109-5121`) gains one arm: a `Variant` is admissible **iff**
`cg_union_niche_member` yields a member that is itself admissible — i.e. exactly a two-member
absence union over a niche-able type. The emitted C type is **literally unchanged**
(`codegen.tks:1532-1534` emits the niche member's own type), so no ABI can shift, and
`uptr | null` is excluded automatically by the same predicate rather than by a special case.
*Delivers:* [PIN-3] becomes expressible — §5.4's whole answer.
*Unblocks:* `regressor.tkr:87-89`'s escalated FFI pin can be **promoted to a positive scenario**;
the file's own convention is that such a pin "fails LOUDLY" when support arrives, forcing a
conscious promotion rather than a silent one.
*Fixtures:* `c_null_union_extern_return` (accept), `c_null_union_forces_check` (reject),
`c_uptr_null_union_extern_rejected` (reject).
**Ritual: full gate** (an extern-signature gate changes).

**C3 — `as_cstr` retyped to `ptr<byte>`. [UNBLOCKED]**
One line at `scope.tks:636` (`ret = Ptr { inner = null }` → `Ptr { inner = Byte { } }`). Strictly more
honest; unlocks the `c_string` field initialization (§7.3). Zero callers.
*Fixture:* folded into C4's.

**C4 — `c_string` + `c_string_for_call` + `c_string_ptr` (tier 1). [UNBLOCKED, needs C1+C3]**
The nominal `unsafe type` and the two tier-1 operations of §7.1/§7.2.
*Delivers:* the whole out-bound crossing at the lifetime that covers **every** extern in this tree.
*Fixtures:* `c_string_roundtrip` (accept), `c_string_embedded_nul_truncates` (accept — the exit code
proves C saw the prefix), `c_string_field_in_safe_struct_rejected` (reject, `c55`'s sibling),
`c_string_in_safe_signature_rejected` (reject).
**Ritual: full gate** (the first nominal unsafe type outside `teko::mem::unsafe`).

**C5 — `teko::mem::region_buf`, the named-region byte allocation. [UNBLOCKED]**
The one builtin this design adds: checker signature + codegen lowering to `tk_region_alloc`,
mirroring `buf_ptr` (`scope.tks:366-373`, `codegen.tks:3171-3186`) and `region_alloc`
(`typer.tks:978-1000`).
*Delivers:* tier 2's missing primitive. *Fixture:* `mem_region_buf_roundtrip`.

**C6 — `c_string_in_arena` (tier 2). [UNBLOCKED, needs C4+C5]**
*Delivers:* Question 3's answer, complete — both tiers exist and the caller names one.
*Fixture:* `c_string_outlives_enclosing_region` — build in an `Arena`, let the enclosing region turn
over, read back, `mem::free(a)`; the exit code proves the octets survived past where tier 1 would
have died. **A lifetime differential, needing no C stub** — which matters, because the C-death ladder
(`teko-laws-digest.md`) forbids leaning on a fixture that ships C.
**Ritual: full gate** (the ownership model is complete — the core of the issue).

**C7 — retire `teko::mem::as_cstr` in favour of `c_string_for_call`; delete `tk_cstr_dup`. [needs C4]**
Keep `as_ptr` — it is the correct primitive for `ptr+len` C APIs and has no terminator question, and
it is precisely the thing `c_string` must stay distinguishable from (§7.4).
*Delivers:* §9.5's permanent leak is gone from the tree. **Ritual: full gate.**

**C8 — corpus adoption: the NON-ARITHMETIC probe externs. [needs C1+C2b+C4]**
`full_gate.tks:45,53` and `control.tks:8` re-declare as
`pub extern unsafe fn c_setenv(name: ptr<byte>, value: ptr<byte>, overwrite: c_int): c_int =
"setenv"` / `pub extern unsafe fn c_getenv(name: ptr<byte>): ptr<byte> | null = "getenv"`, and are
fed `c_string_ptr(c_string_for_call(name))`. Per the fixture convention (a fixture/probe cannot `use`
the compiler's own stdlib — `rawbuf.tks:30-33`, `c55_.../case.tks`), each probe carries a **local
copy** of the `c_string` shape; that the local copy lowers identically is itself part of what the
crumb proves.
*Delivers:* two prose obligations become **types** — `@param name  the address of a NUL-terminated
variable name` (§1) and `@return … or 0 on failure` (§5.4.1). The hand-rolled, un-named marshalling
ends where it can end.
*SCOPE LIMIT, stated rather than discovered later:* `bottom.tks` and `word.tks` — the arena floor —
**do NOT migrate.** Their own doc-comment argues correctly that an arena IS address arithmetic
(`bottom.tks:8-12`), and `ptr<byte>` has no arithmetic operators; those are designed in
`marshall-spec.md` §5.5 and are **unbuilt**. They stay on `u64` until #498's operator crumb lands.
Migrating them now would trade a typed carrier for an unusable one.
*Note:* the `-> c_int` returns here are §9.3's interaction — land after the sibling cargo, or assert
a width-insensitive value.
**Ritual: full gate** (the issue's proposal is delivered end to end).

---

## 11. Regression fixtures

Every oracle is native (`teko-laws-digest.md`, #524). Prior briefs mentioned both engines; now only native remains and the fixtures validate accordingly.

Fixture mechanics follow the house exactly: exit-code oracles are a namespace under
`examples/regressions/bulk/src/qNNN_<name>/body.tks` plus a `Scenario` in `bulk.tkr`
(`Given env = ["TEKO_REGR_ENTRY=…"]` / `When built and run` / `Then exit = N`); compile-fail oracles
are a namespace under `examples/regressions/diagnostics/src/cNN_<name>/case.tks` plus a `Scenario`
pinning a diagnostic prefix (`When compilation fails` / `Then diagnostic = "…"`).

**ACCEPT — exit-code oracles (native).**

| fixture | exit | proves |
|---|---|---|
| `c_extern_unsafe_accepted` | `0` | C0 — `pub extern unsafe fn … = "abs"` parses, checks, links, runs |
| `c_types_alias_identity` | `7` | C1 — a value assigned through `c_int`/`c_uchar`/`c_ulonglong` and back is unchanged; **and that an alias to a bare primitive resolves at all** (§2.1) |
| `c_str_from_c_totality` | `0` | C2, all four §6 cases in one program: null→`""`, NUL-in-bound, no-NUL-in-bound→`max` octets, `max==0`→`""` |
| `c_string_roundtrip` | `0` | C4 — `str` → `c_string` → a libc extern (`strlen` via `ptr<byte>`) → `str_from_c` → compare |
| `c_string_embedded_nul_truncates` | `3` | C4 + **PIN-1** — a `str` of `"abc\0def"` crosses; the exit code is what C measured (3), **not** `len` (7) |
| `mem_region_buf_roundtrip` | `0` | C5 — allocate in an `Arena`, write, read back, `mem::free` |
| `c_string_outlives_enclosing_region` | `0` | C6 — the tier-2 lifetime differential (§10 C6) |
| `c_null_union_extern_return` | `0` | C2b + [PIN-3] — `extern fn … -> ptr<byte> \| null = "getenv"`; the program `match`es both arms, asks for a variable it set and one it did not, and the exit code distinguishes them. **This is `regressor.tkr:87-89`'s missing usage pattern, authored at last** |
| `c_probe_setenv_typed` | `0` | C8 — `full_gate`'s `setenv`/`unsetenv` round trip with `ptr<byte>` + `c_int` |

**REJECT — compile-fail oracles, each pinning a diagnostic prefix.**

| fixture | rejects | expected diagnostic prefix |
|---|---|---|
| `c_types_alias_no_implicit_widen` | assigning an `i64` to a `c_int` without a cast — proves the alias IS `i32` and nothing wider | the existing type-mismatch text |
| `c_string_field_in_safe_struct_rejected` | a `c_string` field in a non-`unsafe` struct — `c55`'s sibling through the SAME gate | `"struct/class '…' has an unsafe-typed field '…"` (`collect.tks:1976`) |
| `c_string_in_safe_signature_rejected` | a non-`unsafe fn` naming `c_string` in its signature | `"safe fn '…' names unsafe type in its signature"` (`collect.tks:42`) |
| `c_string_as_extern_param_rejected` | declaring `extern fn f(s: c_string)` — pins §3/§7.1's forcing constraint so a later refactor cannot quietly "enable" it | `"an \`extern\` function parameter must be a primitive (int/float/bool), \`byte\`, \`ptr\`, \`uptr\`, or an \`extern type\` handle (C7.1a)"` (`typer.tks:5746`) |
| `c_null_union_forces_check` | using a `ptr<byte> \| null` extern result **without** matching the `null` arm — the before/after of §5.4.1, and the whole value of [PIN-3] in one fixture | the existing null-union "bare use of a nullable" text |
| `c_uptr_null_union_extern_rejected` | `extern fn … -> uptr \| null` — pins §5.4.3's representational exclusion so nobody later adds a fictional `Uptr` niche arm | the C7.1a extern-return text (C2b's predicate excludes it) |

**A fixture the design deliberately does NOT ship:** an oracle for "the developer passed
`as_ptr`'s non-terminated buffer to a `ptr<byte>` extern". It type-checks by construction (§7.4) and
there is nothing to assert. Naming its absence is the honest move; papering over it with a fixture
that tests something adjacent would be worse.

---

## 12. Ritual points

The full gate must pass at **C0** (the parser's documented contract), **C2** (a shipped builtin's
arity), **C2b** (an extern-signature gate changes), **C4** (the first nominal unsafe type outside
`teko::mem::unsafe`), **C6** (the ownership model completes — the core of the issue), **C7** (a
shipped builtin is retired and runtime C is deleted), and **C8** (the issue's proposal is delivered
end to end). C1, C3 and C5 are additive and gate on their own fixtures.

One non-gate ritual, easy to forget and loud when missed: **C2b must promote `regressor.tkr:87-89`'s
FFI pin**, per that file's own stated convention that a pin fails loudly the day support lands.

---

## 13. Risks and law tensions

1. **The alias-to-a-bare-primitive assumption (§2.1).** No corpus precedent. Mechanically it must
   work; it is unproven. **Mitigated by sequencing:** C1 is the first product crumb, so the design
   fails fast and cheaply if it is wrong. *Measurement needed and how to get it:* build a
   single-declaration project containing `type c_int = i32` and a function taking one.

2. **Law tension — NO-DEFERRAL vs the excluded types (§5.2).** `teko-laws-digest.md` forbids
   "follow-up / não bloqueia / completar-depois" for a real failure, but explicitly preserves
   *"recorte de roadmap (feature futura não-começada que nenhuma falha exige)"*. `c_long`/`c_size`
   are the second kind: **no measured failure requires them** (every extern in the tree uses `u64`
   for sizes and works), and they are not merely deferred — they are **inexpressible**, because
   `#os` cannot guard a type declaration. **Resolved law-first, not escalated.** The two unblockers
   (`usize`/`isize`; OS-guarded type declarations) are REPORTED UP.

3. **Law tension — NO-DEFERRAL vs `bytes_from_ptr`'s short-buffer over-read (§9.6).** A real gap
   that **cannot** be closed: the bound is knowledge only the caller has. Resolved by M.3 (honesty
   over a fabricated guarantee): document it, do not repair it. This is an honest-stop, not a
   deferral.

4. **Coupling with #498 (`marshall-spec.md`) runs one way, by construction (§4.2).** c_types does not
   need `Ptr` to become unconditionally unsafe-carrying, and when it does, c_types is already
   compliant. If #498 lands first, C0/C8 simply become mandatory instead of voluntary — no rework.

5. **Coupling with the C-death ladder.** Every fixture here is a Teko program plus a libc symbol; not
   one ships a `.c` file. C2/C5 touch `teko_rt.{c,h}`, which the Teko-only law names as **maintained**
   C. Nothing in this design adds an emitter, a `.c`, or a `cc` dependency
   (`star-ref-and-ffi-0.3.1.md`'s own-backend-first mandate).

6. **The last-hop residual (§7.4).** Stated, bounded to one greppable call site per extern, with the
   blocked alternative shown. Not a tension — a named limit.

7. **The one place [PIN-3] and the measurement may diverge (§5.4.3).** The ruling names two
   spellings; only `ptr<T> | null` lowers correctly, because `Uptr` has no spare bit-pattern to make
   a niche out of. I have taken the ruling's *intent* — nullability belongs in the type, not in a
   comment — and satisfied it with the spelling that works, rather than building a fictional niche.
   **If the owner intended `uptr | null` as a required spelling, this measurement is what must reach
   him**, because honouring it literally would mean claiming an encoding an integer does not have.
   This is a report, not a HALT: the intent is satisfiable as written, and §5.4.5 satisfies it.

8. **A latent nullability tension the scan surfaced, and the resolution.** Roughly half the pointers
   in §5.4.5 must NOT carry `| null` (NULL is UB for them). The temptation to apply `| null`
   uniformly "to be safe" is the exact failure mode the tree already has with blanket `u64`: an
   annotation that is everywhere carries no information, and a forced match at a call site where
   NULL is undefined behaviour teaches the reader to ignore the match. Resolved by M.3: the
   annotation is a claim about one API's contract, per symbol, or it is nothing.

---

## 14. Reported up (adjacent findings — not turned into issues here)

1. **`parse_decl.tks:206-210`'s doc-comment is wrong** about `extern` + `unsafe`, and has almost
   certainly kept the corpus from using a spelling that already works (§4).
2. **An `unsafe` modifier on a structurally-typed alias is silently inert** (§9.2) — a live checker
   hole independent of this issue.
3. **`teko::mem::str_from_cstr` performs an unbounded `strlen` on a foreign pointer**
   (`teko_rt.c:167`) — fixed by C2, but it is a shipped defect, not a design gap.
4. **`tk_cstr_dup`'s buffer is a permanent leak** (`teko_rt.c:157-164`) — fixed by C7.
5. **Teko has no target-width integer** (`usize`/`isize`), which is what excludes `size_t`/`ptrdiff_t`.
6. **`#os` cannot guard a type declaration** (`parse_decl.tks:1239`), which is what excludes `c_long`.
7. **`Arena.region: uptr` predates §5.3's rule** and `marshall-spec.md` §8.4 already flags it for
   re-tagging — left alone here; it belongs to #498.
8. **A possible future lint:** every raw-libc `ptr<byte>` extern argument is fed by `c_string_ptr`
   or `buf_ptr` (§7.4).
9. **`regressor.tkr:87-89`'s escalated FFI pin now has its ruling** ([PIN-3]) and is promotable to a
   positive scenario at crumb C2b (§10). Flagged so the promotion is conscious, which is what that
   file asks for.
10. **`extern_type_ok` admits no `Variant` at all** (`typer.tks:5109-5121`) — C2b fixes it for the
    niche case, but the broader question of which unions may cross a C boundary is untouched and
    unasked. Noted, not answered here.
11. **`acquire_chunk`'s `demand(block != 0, 10)`** (`bottom.tks:154`) and **`free`'s "0 is a
    documented no-op"** (`bottom.tks:25`) are two more prose-only pointer contracts in the same file
    that §5.4.5 would type — but the file cannot migrate until pointer arithmetic exists (§10 C8's
    scope limit). Recorded so the pair is not lost between the two waves.

---

*Base: `docs/design/marshall-spec.md` (§4, §6, §8.4), `docs/design/star-ref-and-ffi-0.3.1.md` (§0,
§4.4), `docs/design/memory-unsafe-backend-remodel.md` (§2, §2c),
`docs/memory/teko-mem-model-empirical.md`, `docs/memory/teko-laws-digest.md`.*
