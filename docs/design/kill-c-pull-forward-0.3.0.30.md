# Kill-C PULL-FORWARD into 0.3.0.30 — own-backend maturity inventory + FFI own-native validation

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented — the owner ratifies before code.** Addendum to
> `docs/design/star-ref-and-ffi-0.3.1.md`. **Owner standing rule:** *everything that can be pulled
> forward into .30 toward KILLING the C backend must be — to shrink the future waves.* This doc maps
> that CONCRETELY, anchored on the **current** own-backend code.
>
> **OWNER LINK STRATEGY (governs §2/§4 — the E1 dependency is DISSOLVED):**
> 1. Emit `.o` **and** `.a` **directly, own-native, with NO C** (the `objfile_*` already emit
>    ELF/Mach-O/COFF relocatable — COMPLETE the whole-program section/symbol/reloc emission + add `.a`
>    static-archive emission).
> 2. Link with the **system `ld`** (the platform linker — NOT the `cc` driver) into the final per-OS/arch
>    image: `.exe`/`.dll`/`.lib` (Windows/COFF), ELF binary/`.so` (Linux), Mach-O binary/`.dylib` (macOS).
> 3. The **own E1 linker** comes LATER (total link independence) — it **replaces the system `ld`**; it is
>    **NOT a prerequisite to kill C emission.**
>
> **Critical path to kill C is entirely own-native, no external blocker:** (1) close the ~20 **float +
> i128 isel** honest-stops; (2) complete own-native **`.o`/`.a`** emission; (3) swap the build step from
> "emit `teko.c` + `cc`" to "emit `.o`/`.a` + **system `ld`**." E1 is a later link-independence epic.
>
> **Fixed facts:** fixpoint is **gen1==gen2 (NO gen3)**; the C backend is transitional and **dies**;
> **no FFI may depend on `cc` surviving** (the reverse-FFI C *consumer* may use the host `cc`, but Teko
> codegen never does, and the LINK is the system `ld`, not `cc`); **Part A (`ref`) is executed by another
> agent — not touched here.** GATE-G for a PODE-.30 crumb = `teko build . --no-verify --release &&
> ./bin/teko test .` **+ fixpoint gen1==gen2**.
>
> **Sources (read at authoring):** `src/lir/lower.tks` (TAST→LIR, "N1 subset" #221 — 60 honest-stops),
> `src/lir/lir.tks`, `src/backend/isel_{x86_64,arm64,riscv}.tks` (32 honest-stops, dominated by the
> `B1-fp` float family + i128), `src/backend/encode_*`, `objfile_{elf,macho,coff}.tks`,
> `abi_{sysv64,aapcs64,win64,riscv64}.tks`, `regalloc*`, `lir_interp.tks`/`minst_interp.tks`,
> `src/codegen/codegen.tks` (`cb_fn_name` the `__` mangle; `f.c_symbol` extern no-mangle at :7515),
> the IMPORT convention `docs/design/drain-fase3-stdlib-order.md:127/136/146` + `vm-retirement.md:327`
> (`extern fn … = "SYM" from "lib"`), `src/build/regression.tks` + `docs/design/tkr-regression-format.md`,
> `teko.tkp` `[tests]`/`[platforms]`.

---

## 1. Own-backend maturity INVENTORY today (concrete honest-stops / gaps)

The own AOT backend is **mature in its BOTTOM half, immature in its TOP half:**

| layer | file(s) | state |
|---|---|---|
| **TAST → LIR lowering** | `lir/lower.tks` | **"N1 reference subset" (#221) — the big gap.** 60 NAMED honest-stops. Lowers only: integer/float **literals**, **integer** arithmetic/unary, locals, `to` numeric casts, **direct** calls (Teko-Teko + `exit`/`panic`), `let`, `return`, expr-statements, basic `if`/`match`. |
| **isel** | `isel_{x86_64,arm64,riscv}.tks` | mature for **integer/control/memory**; 32 honest-stops in x86 dominated by the **`B1-fp` FLOAT family** + **i128** materialization. |
| **encode** | `encode_{x86_64,arm64,riscv}.tks` | mature, tested (~80 KB each); full integer/mem/control instruction set. |
| **regalloc** | `regalloc{,_x86,_riscv}.tks` | present, tested. |
| **objfile** | `objfile_{elf,macho,coff}.tks`, `objfile_elf_riscv.tks` | emit **relocatable objects** with undef symbols + relocations; needs completion to the **whole-program** section/symbol/reloc set + **`.a` archive** emission. Final link is the **system `ld`**; the own **E1 linker** (`objfile_elf.tks:383`) is a LATER *link-independence* epic, **NOT a kill-C prerequisite** (§4). |
| **ABI** | `abi_{sysv64,aapcs64,win64,riscv64}.tks` | classification tables present, tested — **no varargs** rule set yet. |
| **oracle** | `lir_interp.tks`, `minst_interp.tks` | interpret the covered subset, **mirroring the C backend's honest-stops** — how the native path is validated **without producing binaries** today. |

**The 60 `lower.tks` honest-stops, categorized:**

| class | examples (cite) | substrate ready? |
|---|---|---|
| integer **comparisons** | `lower.tks:807,821` — LIR has `ICmp*` (`lir.tks:33`) | **YES** |
| **bitwise / shifts** | `lir.tks:32` `IAnd/IOr/IXor/IShl/IShrS/IShrU` | **YES** |
| unary `~` | `lower.tks:309` | **YES** |
| **fat pointers** (str/slice) | "fat-pointer ABI N2 gap" (`lower.tks:936,3767`, #382) | mostly (pin the fat ABI) |
| **struct / field / index** | `lower.tks:615` | **YES** (mem loads/stores) |
| **match patterns** | `lower.tks:3034…3389` | mostly (variant repr exists) |
| if-value; **destructuring** binding | `lower.tks:2777,1819` | **YES** |
| **interpolation** holes | integer/bool ready; float/typed/format (`lower.tks:3987`) | integer YES; float blocked |
| **runtime builtins** | `lower.tks:1059,4006` | lowering YES; native RUN needs runtime linked (system `ld`) |
| **FLOAT family** | `lower.tks:277,784` + isel `B1-fp` (`isel_x86_64.tks:198,325,439`) | **NO — own-backend float isel** |
| **i128** | `isel_x86_64.tks:170` | partial — **own-backend i128 isel** |

**Headline:** of ~60 lowering honest-stops, **~40 are integer/control/memory/fat/match/struct/pointer
lowering whose substrate exists → CLOSEABLE in .30**; **~20 are the FLOAT family + i128 → own-backend
float/i128 isel** (own-native, no external dep). Objfile emission must complete to whole-program + `.a`
(own-native). **NONE needs the E1 linker.**

---

## 2. Classification — PODE .30 AGORA vs DEPENDÊNCIA DURA

### (a) Own-backend honest-stops (lowering)

| item | verdict | detail |
|---|---|---|
| integer comparisons; bitwise/shift; `~`; remainder | **PODE .30** | LIR/isel/encode/interp ready — `lower.tks` completion |
| fat-pointer (str/slice) ABI + lowering | **PODE .30** | pin the fat ABI once |
| struct/field/index; match; if-value; destructuring; integer interpolation | **PODE .30** | mem ops / variant repr known |
| **float family** (+ float interpolation/match) | **DEP DURA (own-native)** | X = own-backend float isel (`B1-fp`) + FPR regalloc/spill — no external dep; residual SW13 |
| **i128** ops | **DEP DURA (own-native)** | X = own-backend i128 register-pair isel (#222, SW13) |

### (b) Own-native `.o`/`.a` emission + system-`ld` link (the NEW kill-C substrate)

| item | verdict | detail |
|---|---|---|
| complete whole-program `.o` emission | **PODE .30** | extend `objfile_{elf,macho,coff}` to the whole program |
| `.a` static-archive emission | **PODE .30** | add the archive writer |
| build-step swap ("emit `.o`/`.a` + system `ld`", behind a flag) | **PODE .30 (prep)** | default flips when lowering=100% |
| runnable native binary / full self-host | **ACHIEVABLE with system `ld`** (NOT E1) | gated only on lowering=100% (float+i128) + `.o`/`.a` emission |

### (c) The FFI KC1–KC4 — E1 removed from the chain

| item | verdict | detail |
|---|---|---|
| raw-pointer ops lowering (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | **PODE .30** | LIR mem loads/stores + address arithmetic |
| macro resolver Tier 0 (constants/flags) | **PODE .30** | compiler front-end; no backend |
| macro resolver Tier 2 (body→IR) | **PODE .30** *(after bitwise/shift lowering)* | integer arithmetic/bitwise LIR |
| macro resolver Tier 1 (symbol alias) | binding **.30**; link via **system `ld`** | bind to the real symbol; system `ld` resolves |
| KC1 vararg ABI | PARTIAL **.30**; native call via **system `ld`** | rule set .30; libc variadic call links via system `ld` |
| `#repr("c")` layout / `cabi` callbacks / **`exp fn` C-ABI export** | **PODE .30** | objfile GLOBAL/FUNC + layout codegen; marker = existing `exp` visibility (no `#[export]`), C symbol = explicit `= "SYM"` or flattened canonical when `abi="c"` (§5.2.1; codegen.tks:7515 no-mangle path) |
| `emit_c_header` `.h` | **PODE .30** | pure text |
| KC3 external-C-symbol resolution | **system `ld`** (NOT E1) | own-native undef-symbol/reloc emission (.30) + system `ld` resolves |
| KC4 delete the C-FFI emitter | when lowering=100% + `.o`/`.a` + `ld`-link (NOT E1) | the LAST kill-C step |

### (d) The own E1 linker

| item | verdict | detail |
|---|---|---|
| undef-symbol + reloc EMISSION in objfiles | already present / complete in .30 | the linker INPUT is ready |
| the **E1 own linker** (replace the system `ld`) | **LATER independence epic — NOT a kill-C blocker** | system `ld` carries the link until E1 |

---

## 3. The .30 pull-forward crumb sequence

Each is own-native, additive, seed-safe, riding **GATE-G + fixpoint gen1==gen2**; lowering crumbs are
validated by the **interp oracle** + backend unit tests.

| crumb | size | closes | notes |
|---|---|---|---|
| **KP1** integer **comparison** lowering | S | `lower.tks:807,821` | `ICmp*` + setcc |
| **KP2** **bitwise + shift** lowering | S | `lower.tks:290,309` | unblocks macro Tier 2 |
| **KP3** **struct / field / index** lowering | M | `lower.tks:615` | mem loads/stores |
| **KP4** **fat-pointer ABI** + str/slice literal/`.len`/index | L | `lower.tks:936,3767` | biggest self-host lever |
| **KP5** **match-pattern** lowering | M | `lower.tks:3034…3389` | variant repr known |
| **KP6** if-value + **destructuring** + integer **interpolation** | M | `lower.tks:2777,1819,3987` | float holes blocked |
| **KP7** widen **direct-call / runtime-builtin** lowering | M | `lower.tks:1059` | more builtins into run subset |
| **KP8** **raw-pointer ops** | M | FFI | mem ops; own-native |
| **KP9** **`#repr("c")` layout** + **`extern union`** | M | FFI G2 | objfile-tested |
| **KP10** **`cabi` callback** + **`exp fn` C-ABI export** (explicit `= "SYM"` or flattened canonical, §5.2.1) + FFI-safe gate | M | FFI G3 / reverse-FFI | objfile GLOBAL/FUNC; NO `#[export]` |
| **KP11** **`emit_c_header` `.h`** (prototypes + `#define` consts; close G5) | M | reverse-FFI | pure text |
| **KP12** **macro resolver** Tier 0 + Tier 2 + Tier 3 honest-error | L | FFI macros | Tier 1 link via system `ld` |
| **KP13** **vararg ABI rule set** (SysV/AAPCS64/Win64) + interp | M | FFI KC1 | native call via system `ld` |
| **KP14** **`#cconv` + `teko::ffi::errno()`** | S–M | FFI G4 | common cconv cases |
| **KP15** **`teko_rt_init/shutdown`** + panic-not-into-C | S | reverse-FFI | own-native |
| **KP16** complete whole-program **`.o`** + **`.a` archive** writer | L | §2(b) | the kill-C substrate |
| **KP17** build-step swap PREP ("emit `.o`/`.a` + system `ld`", flag) | M | §2(b) | flips when lowering=100% |

After .30, the only residue before the default flips off C is the **float+i128 isel** — the owner's goal.

---

## 4. Hard dependencies — re-mapped (own-native only; E1 removed as a blocker)

| item | blocked on X | own-native? | earliest |
|---|---|---|---|
| all **float** lowering + float FFI payloads | own-backend float isel (`B1-fp`) + FPR regalloc/spill | **yes** | pull-forward into .30 where regalloc supports; residual SW13 |
| **i128** ops | own-backend i128 register-pair isel (#222) | **yes** | SW13 |
| whole-program **`.o`/`.a`** emission | own-native objfile completion (KP16) | **yes** | **.30** |
| native binary / full self-host / KC4 delete-C | lowering=100% + `.o`/`.a` + system-`ld` link (KP17) | **yes** | as soon as float+i128 isel close |
| KC3 ext-symbol resolution · macro Tier 1 · KC1 native variadic call | the **system `ld`** at link | **yes** | with KP17 |
| own **E1 linker** (replace system `ld`) | the link-independence epic | **yes** | **LATER — NOT a blocker** |

**Re-mapped critical path to kill C:** (1) close float+i128 isel → (2) complete `.o`/`.a` emission →
(3) swap build to `.o`/`.a` + **system `ld`**. All own-native; **E1 blocks nothing.**

---

## 5. FFI VALIDATION — the test lane, cross-OS/arch

Exercised by the **gen1 binary** — `teko build . --no-verify --release` → gen1, then **`./bin/teko test
.`** runs the fixtures. Fixtures need nothing beyond **gen1 + the runner's host toolchain**, and **must
pass own-native** (a fixture passing only via the C emitter is a regression).

### 5.1 Where it wires

- **Surface:** each FFI fixture is a standalone project under **`examples/regressions/ffi/<name>/`**, a
  first-class **`.tkr`** regressive, picked up by **`[tests] regression = ["examples/regressions"]`**.
  The runner (`build/regression.tks`) builds once + runs, asserting **exit/stdout/stderr**;
  `EXPECT_COMPILE_FAIL` for negatives; `trap` for panics.
- **Own-native build:** FFI fixtures build through the **own backend** (`Given backend = "own"`), NOT the
  C emitter. Own-backend-coupled cases are `Given pending = "<crumb>"` and **skipped-green** until landed.
- **Cross-OS/arch matrix:** the lane runs on **each CI host** (`[platforms]` × arch runners);
  `targets = ["host"]`. Platform-specific results use the `.tkr` **`on "<os-arch>"`** override.
- **Link is the system `ld`; `cc` only compiles the reverse-FFI C consumer** (`consumer.c`): the runner
  builds the `abi="c"` artifact own-native (`.o`/`.a` + `.h`), compiles `consumer.c` with host `cc`, and
  **links Teko `.o`/`.a` + the consumer object with the SYSTEM `ld`**, then runs + asserts.

### 5.2 The FFI fixture suite (inputs → expected exit, per platform)

**A. USING C from Teko** (native oracle = own backend):

| fixture | asserts | oracle | crumb |
|---|---|---|---|
| `extern_fn_libc_call` | `extern fn strlen(...) = "strlen"` → exit = len | exit | KP7 + system-`ld` link |
| `extern_macro_const_flag` | Tier-0 `O_RDONLY` inlined; exit = value | exit | **KP12 (.30, no link)** |
| `extern_macro_htonl_expand` | Tier-2 byte-swap → IR; exit = swapped byte | exit | **KP12 (.30)** |
| `extern_macro_complex_rejected` | Tier-3 arbitrary macro | `EXPECT_COMPILE_FAIL` | KP12 |
| `ptr_deref_index_arrow` | `*p`, `p[i]`, `p->f` in `unsafe` | exit | **KP8 (.30)** |
| `ptr_arith_addr_of` | `p+n`, `p-q`, `addr_of(x)` | exit | **KP8 (.30)** |
| `ptr_null_union` | `ptr<T>\|null` / `uptr\|null` null test | exit | **KP8 (.30)** |
| `ptr_deref_in_safe_rejected` / `ptr_void_deref_rejected` / `ptr_optional_question_rejected` | safe deref / `void*` deref / `ptr<T>?` | `EXPECT_COMPILE_FAIL` | KP8 |
| `repr_c_struct_byval` | `#repr("c")` struct to a C stub | exit | **KP9 (.30)** + link |
| `extern_union_layout` | `extern union` read/write | exit | **KP9 (.30)** |
| `cabi_callback_noncapturing` | non-capturing closure as C callback | exit | **KP10 (.30)** + link |
| `cabi_callback_capturing_rejected` | capturing closure coerced to `cabi` | `EXPECT_COMPILE_FAIL` | KP10 |
| `variadic_printf` | `extern fn printf(fmt, ...) = "printf"`; stdout | stdout | KP13 (.30); native call green when KC1 lands |
| `cconv_stdcall` *(windows)* | `#cconv("stdcall")` call | exit | **KP14 (.30)** |
| `ffi_errno_read` | `teko::ffi::errno()` after a failing C call | exit | **KP14 (.30)** |

**B. EXPORTING Teko to C** (reverse-FFI; marker = existing `exp` visibility + `abi="c"`; C symbol =
explicit `= "SYM"` (import-symmetric) **or** flattened canonical default, §5.2.1 — NO `#[export]`):

| fixture | asserts | oracle | crumb |
|---|---|---|---|
| `revffi_export_explicit_symbol` | `exp fn add(a,b) = "teko_add" {…}`; `consumer.c` calls `teko_add(2,3)` | exit = 5 | **KP10 emit (.30); system-`ld` link** |
| `revffi_export_default_flatten` | `exp fn add` (no `= "SYM"`) in root ns `demo` → C symbol `demo_add`; consumer calls it | exit | **KP10 (.30)** |
| `revffi_namespaced_flatten` | `exp fn demo::math::add` (no `= "SYM"`) → C symbol `demo_math_add` | exit | **KP10 (.30)** |
| `revffi_c_header_emitted` | the generated `.h` (iterating the artifact's `exp fn` + referenced `#repr("c")`) compiles under host `cc` (prototypes + `#define` consts) | consumer compiles | **KP11 (.30, pure text)** |
| `revffi_symbol_collision_rejected` | two exports resolving to the same C name | `EXPECT_COMPILE_FAIL` | KP10 |
| `revffi_repr_c_return` | an `exp fn` returns a `#repr("c")` struct; consumer reads fields | stdout | **KP9+KP10 (.30)** |
| `revffi_rt_init_contract` | `consumer.c` calls `teko_rt_init` then an `exp` allocating fn | exit | **KP15 (.30)** |
| `revffi_panic_no_unwind` | an `exp fn` that would panic returns `… \| error`, no unwind into C | exit (error code) | **KP10+KP15 (.30)** |
| `revffi_export_nonffisafe_rejected` | `abi="c"` + `exp fn` with a capturing-closure param / bare-ref return | `EXPECT_COMPILE_FAIL` | KP10 |
| `revffi_export_teko_variadic_rejected` | `abi="c"` + `exp fn` with a `params []T` variadic | `EXPECT_COMPILE_FAIL` | KP10 |

### 5.2.1 The C export SYMBOL-NAME rule (`exp` + `abi="c"`) — RATIFIABLE, symmetric with the IMPORT convention

**The owner's steer:** for IMPORT (FFI-in) the convention is already ratified — the dev names the C
symbol EXPLICITLY in the `extern` clause: **`[pub|exp] extern fn name(...) -> R = "c_symbol" from "lib"`**
(verified: `drain-fase3-stdlib-order.md:127/136/146` — `pub extern fn sqrt(x: f64) -> f64 = "sqrt" from
"m"`; `vm-retirement.md:327` — `exp extern fn cov_merge(path: str) -> bool = "tk_cov_merge" from
"teko_rt"`; also `embed-vfs.md`). The C symbol lives in the **`= "SYM"`** clause, the library in
**`from "lib"`**. **EXPORT is the mirror of this** — the same `= "SYM"` clause names the C symbol; **no
new `#[export]` attribute.**

**The tension it resolves:** the `.tkb`/`.tkh` carry the FULL canonical namespace on an export
(`demo::math::add`); C++ mangles, but **pure C has no namespaces — symbols are flat**. So an exported
`exp fn` needs either an explicit C name (like import) or a deterministic flattening. **As-built mangling
(verified):** the internal Teko fn symbol is `cb_fn_name(ns, name)` = each `::` → `__`, then `__`, then
the name (`teko::emit::foo` → **`teko__emit__foo`**, collision-safe); `extern` emits its raw `c_symbol`
verbatim at `codegen.tks:7515`; types are `tk_t_<ns__name>`.

**THE RULE APPLIES TO EVERY EXPORTED SYMBOL — not just functions.** The owner confirmed: the
flatten-canonical `_` default AND the explicit `= "SYM"` override govern the C name of **every** decl
kind that crosses the boundary — a struct tag, an enum tag + its variant constants, a flags typedef +
its bit constants, a class handle + its method/ctor/dtor symbols, an interface vtable typedef — exactly
as they govern a function symbol. §5.5 spells each decl kind out; the naming rule below is the shared
foundation.

**THE RULE — two spellings, import-symmetric:**

1. **EXPLICIT symbol (symmetric with import — the primary form).** An `exp fn` in an `abi="c"` artifact
   names its C symbol with the SAME **`= "c_name"`** clause `extern` uses on import:
   ```
   exp fn add(a: i64, b: i64) -> i64 = "teko_add" { a + b }     // exports the C symbol `teko_add`
   ```
   Exact mirror of `extern fn add(...) -> i64 = "teko_add" from "lib"` (import): the same `= "SYM"`
   clause names the C symbol on the SAME `fn` grammar; on import the body is ABSENT (foreign), on export
   the body is PRESENT (Teko) — the only difference. **No `#[export]`; no new token — `= "STR"` and `exp`
   already exist.** *(Grammar note: the parser already reads `= "STR"` after an `extern fn` signature;
   here it reads the same `= "STR"` on a NON-extern `exp fn` that is followed by a `{ body }` — a small,
   unambiguous extension, since a normal fn has no `= "STR"` before its block. `from "lib"` is NOT used
   on export — there is no foreign library to link; the symbol is DEFINED here.)*

2. **DEFAULT — flatten the full canonical (no `= "SYM"` given).**
   > C symbol = **`flatten(canonical)`**, `canonical = <f.namespace> :: <f.name>`, each `::` → **`_`**.
   > `demo::math::add` → **`demo_math_add`**.
   Because the canonical root **IS the project name** (`teko.tkp` `name`; source root invisible),
   flattening the full canonical **auto-prefixes every export with the project name** — subsuming the
   `<proj>_name` option for free, with the libc-collision resistance C libs get manually (`sqlite3_open`,
   `SDL_Init`). **Rejected: the bare name** (`add` alone invites libc link collisions).

**The `.tkb`/`.tkh` are UNCHANGED** in both spellings — they keep the full canonical `demo::math::add`
for Teko consumers. **Only the C `.h` (`emit_c_header`) and the emitted object symbol** carry the
explicit / flattened C name; prototype and object symbol agree.

**Codegen wiring:** add `cg_c_export_symbol(f) = <the `= "SYM"` string if present> else
flatten_canonical(f.namespace, f.name)`. In `emit_function_sig`, when `f` is `exp` **and** the artifact
is `abi="c"`, the name branch emits `cg_c_export_symbol(f)` through the **same no-mangle path as the
`extern` arm** (`codegen.tks:7515`, already emits `f.c_symbol` verbatim) — so **7515 now receives EITHER
the explicit `= "SYM"` string OR the flattened canonical, never the bare name or the `__` internal
mangle**. Internal Teko callers resolve to the same emitted symbol (the `exp`+`abi="c"` flag is
consulted in `cb_fn_name`); the fallback (a thin forwarding alias over the internal `__` symbol)
preserves internal mangle if the owner prefers.

**Collisions (handled):**
- **Intra-artifact:** two exports resolving (explicit or flattened) to the same C symbol — a **compile
  error at the export gate**, naming both, requiring an explicit `= "SYM"` on one. The check spans the
  WHOLE emitted symbol set of the `abi="c"` artifact (explicit + flattened exports + `__`-mangled
  internals + `extern` `c_symbol`s).
- **Against libc / other libs:** the flattened default's project-root prefix makes this rare; a genuine
  clash surfaces at the **system `ld`** link and is resolved with an explicit `= "SYM"` — exactly as the
  import side already picks the exact symbol.

### 5.3 The OS/arch matrix

| lane host | arch | notes |
|---|---|---|
| Linux | **x86_64**, **arm64**, **riscv64** | full matrix; riscv via the riscv objfile/encode/abi |
| macOS | **arm64** | AAPCS64 (darwin vararg-on-stack variant) |
| Windows | **x86_64**, **arm64** | COFF; Win64 vararg (float-dup-into-GPR); `#cconv("stdcall")` |

Own-backend-coupled fixtures (`variadic_*` → KC1) are **"green when the crumb lands"** via `Given
pending`. **No fixture is gated on E1** — the link is the system `ld` throughout; when E1 lands the same
fixtures flip to the own linker with no rewrite.

### 5.4 The gen1-executable + no-`cc`-for-Teko constraint

- No fixture may require a tool beyond **gen1 + the runner's host `cc`/`ld`.** The Teko side is
  **always own-native**; the LINK is the **system `ld`**; the host `cc` compiles **only** the
  reverse-FFI `consumer.c`. A fixture passing only via the C emitter is a **regression**.

---

## 5.5 FFI across ALL declaration kinds — both directions (owner: not just `fn`)

The AST decl kinds (`ast.tks:347` `TypeBody = StructBody | EnumBody | FlagsBody | VariantBody |
AliasBody | ExternBody | ClassBody | InterfaceBody | TraitBody`) each need a C mapping in BOTH
directions. Internal type mangle today is `tk_t_<ns__name>` (`cb_tysym`, double-underscore); for the
C-facing boundary the **§5.2.1 rule flattens single-underscore or takes `= "SYM"`** — applied to the
type tag, and (compositionally) to each exported member/method/variant symbol. `#repr("c")` is
**mandatory** on any aggregate that crosses (a Teko-laid-out struct has no stable C ABI).

### EXPORT (artifact `abi="c"` + `exp`) — Teko → C

| decl kind | C mapping (in the generated `.h` + emitted symbols) | member/symbol naming | notes / honest-stops |
|---|---|---|---|
| **struct** (`#repr("c")`) | `typedef struct <tag> { <fields> } <tag>;` — `<tag>` = flattened canonical `ns_Name` or `= "SYM"` | field names carried verbatim (C identifiers) | every field must be **FFI-safe** (scalar / `ptr<T>` / nested `#repr("c")`); a Teko **slice/fat-pointer**, closure, or bare `ref` field ⇒ **compile error** (export it as an explicit `ptr<T>+len` pair or don't export) |
| **enum** | `typedef enum <tag> { ns_Name_A, ns_Name_B, … } <tag>;` | variants → **`<tag>_<Variant>`** constants | ordinals are the **ABI contract** — source order fixes `0..n`; **reordering variants is an ABI break** (documented). C `enum` is int-width — fine for a plain enum |
| **flags** | `typedef <uintN> <tag>;` + **`#define <tag>_<MEMBER> <1u<<k>`** per bit (mirrors the internal `tk_t_<Name>_<MEMBER>` power-of-2 scheme, `codegen.tks:471`) | bits → **`<tag>_<MEMBER>`** | `#define`/`static const`, **not** a C `enum` (flags are `u128`-capable, wider than int); underlying `typedef` picks `uint32/64_t`/`unsigned __int128` by width |
| **class** | **opaque handle** `typedef struct <tag>* <tag>;` (C never sees the layout — matches the Teko class `{data,vtable}` reference-object) | methods → **`<tag>_<method>(<tag> self, …)`**; ctor → `<tag>_new(…) -> <tag>`; dtor → `<tag>_free(<tag> self)` | **polymorphism DOES cross via the exported method wrappers**: each `<tag>_<method>` wrapper does the Teko-side vtable dispatch internally; the C side stays vtable-agnostic. **Honest-stop: C cannot SUBCLASS / override virtuals** (that needs C to supply a vtable — the abstraction can't cross); export is for C to *use*, not *extend* |
| **interface** | **vtable struct-of-function-pointers** `typedef struct <tag> { R (*method)(void* self, …); … } <tag>;` + the boundary value is a `{ void* self; const <tag>* vtable; }` fat pointer | methods → the fn-pointer field names | this is the honest C form of an interface (a vtable). **Dynamic dispatch crosses** (C calls through the fn-pointer table). C MAY implement it (supply its own fn pointers) — the one decl kind that crosses both ways cleanly. Default/generic interface methods ⇒ honest-stop |
| **variant** (tagged union) | `typedef struct <tag> { <int> tag; union { <members> } u; } <tag>;` (`#repr("c")`) | members → `<tag>_<Member>` tag constants | doable as a C tagged struct; a member that is itself non-FFI-safe ⇒ honest-stop |
| **alias / trait** | alias → the aliased C type inline; trait → **nothing** (compile-time only, like today) | — | trait never reaches the boundary |

### IMPORT (`extern`) — C → Teko (today only `extern fn`; extend to types)

| decl kind | surface (proposed, additive) | maps to | naming / notes |
|---|---|---|---|
| **struct with fields** | `extern type Name = struct { f: T; … } [= "c_tag"] [from header "h.h"]` | a Teko `Type` with **implicit `#repr("c")`** C layout (field order/padding = C) | `= "c_tag"` names the C struct tag (optional; default match-by-name); `from header` lets the macro resolver (Tier 0) confirm/complete layout |
| **opaque handle** | `extern type Name` (no body) — **as the plan already cites** | `void *` opaque handle | the `extern type Sqlite3` case; unchanged; now the pointee-less end of the same `extern type` family |
| **union** | `extern type Name = union { a: T; b: U } [= "c_tag"]` | a C **untagged** union (members overlap; size = max) | **unsafe-only** (reading the wrong arm is UB); a new `union` body under `extern` |
| **enum** | `extern type Name = enum { A = 1; B = 4; … } [= "c_tag"] [from header "h.h"]` | a Teko enum whose ordinals ARE the given C values (explicit-valued) | `from header` ⇒ the **Tier-0 macro resolver reads the header's `#define`s** and fills the values (e.g. `errno` constants) — the powerful, consistent tie-in |
| **flags (C bitmask)** | `extern type Name = flags { A = 1; B = 2; … } [from header "h.h"]` | a Teko flags with the given bit values | mirrors `flags`; `from header` auto-fills from `#define`s |

**The import NAMING analog (owner's question):** on import a *function* names its **linker symbol** via
`= "SYM"` (there IS a symbol to resolve). A *type* has **no linker symbol** — what matters is the
**layout** (checked structurally) and, secondarily, the **C tag** for `.h`/`#include` matching. So the
type analog of `= "SYM"` is **`= "c_tag"`** (names the C aggregate/enum tag; optional, default
match-by-name), and **`from header "h.h"`** is the type-level companion of `from "lib"` — it points the
Tier-0 resolver at the header to confirm layout / fill enum/flag values. Symbols that DO resolve (an
imported C **global/const**) keep the `extern`-fn-style `= "SYM"`.

### Wiring (where each lands in the codebase)

- **Parser (`parse_decl.tks`):** extend `extern type Name = …` to accept `struct`/`union`/`enum`/`flags`
  bodies + the optional `= "c_tag"` and `from header "h.h"` clauses (mirrors the `extern fn … = "SYM"
  from "lib"` shape already parsed).
- **Checker (`resolve.tks`/layout):** an `extern` aggregate is implicitly `#repr("c")`; give it the C
  layout; an `exp` aggregate under `abi="c"` runs the **FFI-safe field gate** (§5.5 EXPORT rules).
- **Codegen (`codegen.tks`):** a new `cg_c_export_symbol`/`cg_c_export_tag` (flatten-canonical `_` or
  `= "SYM"`) used for the C-facing name of **every** exported decl (not just `emit_function_sig`'s fn
  path at :7515) — the type path (`mangle_type_name`/`cb_tysym`) grows the abi="c" single-underscore
  flatten variant, and the enum/flags/class/interface member symbols compose from it.
- **`emit_c_header` (KP11, `emit/header.tks`):** iterate the artifact's exported decls and emit, per
  kind, the C form above (struct/enum/flags/class-handle+methods/interface-vtable), pulling in
  transitively-referenced `#repr("c")` types; this is the same pure-text emitter, widened from fn
  prototypes to all decl kinds. Closes the `header.tks:91` opaque-`extern type` honest-stop (G5).

### Fixtures (add to `examples/regressions/ffi/`)

**Export:** `revffi_export_struct_repr_c` (C reads fields) · `revffi_export_struct_nonffisafe_rejected`
(a slice field) · `revffi_export_enum_stable_ordinals` · `revffi_export_flags_bitmask` (C `&`/`|` the
bits) · `revffi_export_class_handle` (C calls `Ns_Class_new`/`_method`/`_free`) ·
`revffi_export_class_virtual_dispatch` (an overridden virtual dispatches correctly through the wrapper)
· `revffi_export_class_subclass_from_c_rejected` (honest-stop) · `revffi_export_interface_vtable` (C
calls through the fn-pointer table) · `revffi_export_variant_tagged`.
**Import:** `ffi_import_struct_fields` (read/write a C struct by field) · `ffi_import_opaque_handle`
(`extern type Sqlite3` → `void*`) · `ffi_import_union_unsafe` (unsafe-only) ·
`ffi_import_enum_from_header` (Tier-0 fills `errno`/`O_*` values) · `ffi_import_flags_from_header` ·
`ffi_import_struct_nonrepr_layout_mismatch_rejected`.

### Honest-stops (called out, per the owner)

- **Class subclassing / virtual override from C** — export is use-only; C supplying a Teko vtable does
  not cross. (Interface *implementation* from C DOES cross via the fn-pointer struct — the exception.)
- **Generics crossing C** — only **monomorphic** instances export (as `ns_Box_i64`); a generic-over-C
  type/fn is an honest-stop.
- **Teko slice / fat-pointer / closure fields** in an exported aggregate — not FFI-safe; export as an
  explicit `ptr<T>` (+ length) or reject.
- **Default / generic interface methods**, and a **variant member that is itself non-FFI-safe** — honest-stop.
- **Enum/flags value stability is an ABI contract** — reordering variants or renumbering bits is an ABI
  break (not a compiler error, a documented discipline).

---

## 6. Summary (counts + the re-mapped picture)

- **Own-backend honest-stops:** **~60 in `lir/lower.tks`** (the TAST→LIR "N1 subset" gap) + **~32 in
  `isel_x86_64.tks`** (`B1-fp` FLOAT family + i128), mirrored in isel-arm64/riscv + the interp oracle.
- **Closeable in .30 (KP1–KP17):** **~40 of the 60** lowering honest-stops **PLUS the kill-C substrate**
  (whole-program `.o`/`.a` emission KP16 + the system-`ld` build-step swap prep KP17) + the FFI
  own-native codegen (raw-ptr ops, `#repr("c")`/union, `cabi`/`exp`-C-ABI export, the `.h` emitter,
  macro Tiers 0+2, vararg ABI tables, cconv/errno, rt-init).
- **Hard deps (own-native, NO external blocker):** the **FLOAT family + i128** isel (~20 honest-stops) —
  the ONLY residue before the default flips off C.
- **E1 DISSOLVED as a blocker:** the link is the **system `ld`**; E1 is a LATER independence epic.
  KC3 / macro-Tier-1 / native-variadic-call resolve via the system `ld`, not E1.
- **Reverse-FFI symbol rule (import-symmetric):** `exp` + `abi="c"` ⇒ the C symbol is **either an
  explicit `= "SYM"`** (the exact mirror of import's `extern fn … = "SYM"`) **or, by default, the
  flattened full canonical** (`demo::math::add` → `demo_math_add`, auto-prefixed by the project root).
  `.tkb`/`.tkh` keep the full canonical; intra-artifact collisions are a compile error; NO `#[export]`
  attribute; codegen routes both through the 7515 no-mangle path.
- **FFI covers ALL decl kinds, both directions (§5.5):** EXPORT — struct→C struct (`#repr("c")`,
  FFI-safe fields), enum→C enum (stable ordinals + `<tag>_<Variant>` constants), flags→C bitmask
  `#define`s, class→opaque handle + `<tag>_<method>(self,…)` functions (virtual dispatch crosses via the
  wrappers; C-subclassing is an honest-stop), interface→vtable struct-of-fn-pointers (crosses both ways).
  IMPORT — `extern type = struct/union/enum/flags [= "c_tag"] [from header "h.h"]` (opaque `extern type`
  → `void*` unchanged; `from header` lets the Tier-0 resolver fill enum/flag values). The §5.2.1 naming
  rule (flatten canonical `_` or explicit `= "SYM"`/`= "c_tag"`) governs **every** exported symbol/tag.
- **FFI validation:** a cross-OS/arch `.tkr` suite under `examples/regressions/ffi/` (both directions +
  pointers + all decl kinds + explicit-symbol/flatten/collision cases), own-native, gen1-executable,
  system-`ld`-linked (host `cc` only for the reverse-FFI C consumer).

*Design-ahead, doc-only. No product code changed. Part A (`ref`) is out of scope here (another agent).
Owner ratifies before implementation. This export-naming proposal is delivered for consolidation with
the concurrent dec/bigint proposal (a46976).*
