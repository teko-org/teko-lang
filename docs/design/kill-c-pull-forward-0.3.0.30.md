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
> **Therefore the critical path to kill C is entirely own-native, no external blocker:** (1) close the
> ~20 **float + i128 isel** honest-stops; (2) complete own-native **`.o`/`.a`** emission; (3) swap the
> build step from "emit `teko.c` + `cc`" to "emit `.o`/`.a` + **system `ld`**." E1 is a later
> link-independence epic.
>
> **Fixed facts:** fixpoint is **gen1==gen2 (NO gen3)**; the C backend is transitional and **dies**;
> **no FFI may depend on `cc` surviving** (the reverse-FFI C *consumer* may use the host `cc`, but Teko
> codegen never does, and the LINK is the system `ld`, not `cc`); **Part A (`ref`) is executed by another
> agent — not touched here.** GATE-G for a PODE-.30 crumb = `teko build . --no-verify --release &&
> ./bin/teko test .` **+ fixpoint gen1==gen2**.
>
> **Sources (read at authoring):** `src/lir/lower.tks` (TAST→LIR, "N1 subset" #221 — 60 honest-stops),
> `src/lir/lir.tks` (the LIR opcode set), `src/backend/isel_{x86_64,arm64,riscv}.tks` (32 honest-stops
> in x86, dominated by the `B1-fp` float family + i128), `src/backend/encode_*`, `objfile_{elf,macho,
> coff}.tks` + `objfile_elf_riscv.tks` (relocatable objects; the "E1 linker" note `objfile_elf.tks:383`),
> `abi_{sysv64,aapcs64,win64,riscv64}.tks`, `regalloc*`, `lir_interp.tks`/`minst_interp.tks` (the
> differential oracle that mirrors the C backend's honest-stops), `src/build/regression.tks` +
> `docs/design/tkb-regression-format.md` (the test lane), `teko.tkp` `[tests]`/`[platforms]`.

---

## 1. Own-backend maturity INVENTORY today (concrete honest-stops / gaps)

The own AOT backend is **mature in its BOTTOM half, immature in its TOP half:**

| layer | file(s) | state |
|---|---|---|
| **TAST → LIR lowering** | `lir/lower.tks` | **"N1 reference subset" (#221) — the big gap.** 60 NAMED honest-stops. Lowers only: integer/float **literals**, **integer** arithmetic/unary, locals, `to` numeric casts, **direct** calls (Teko-Teko + `exit`/`panic`), `let`, `return`, expr-statements, basic `if`/`match`. |
| **isel** | `isel_{x86_64,arm64,riscv}.tks` | mature for **integer/control/memory**; 32 honest-stops in x86 dominated by the **`B1-fp` FLOAT family** (float ALU/compare/negate/convert/literal → SSE) + **i128** materialization. |
| **encode** | `encode_{x86_64,arm64,riscv}.tks` (+consts) | mature, tested (~80 KB each); the full integer/mem/control instruction set present. |
| **regalloc** | `regalloc{,_x86,_riscv}.tks` | present, tested. |
| **objfile** | `objfile_{elf,macho,coff}.tks`, `objfile_elf_riscv.tks` | emit **relocatable objects** with undef symbols + relocations; needs completion to the **whole-program** section/symbol/reloc set + **`.a` archive** emission. The final link is the **system `ld`**; the own **E1 linker** (`objfile_elf.tks:383`) is a LATER *link-independence* epic, **NOT a kill-C prerequisite** (§4). |
| **ABI** | `abi_{sysv64,aapcs64,win64,riscv64}.tks` | classification tables present, tested — but **no varargs** rule set yet. |
| **oracle** | `lir_interp.tks`, `minst_interp.tks` | interpret the covered subset, **mirroring the C backend's honest-stops** so a fixture honest-stops identically on both — this is how the native path is validated **without producing binaries** today. |

**The 60 `lower.tks` honest-stops, categorized (what the own AOT backend does NOT yet emit):**

| class | examples (cite) | substrate ready? |
|---|---|---|
| integer **comparisons** | `<`/`<=`/`>`/`>=`/`==`/`!=` (`lower.tks:807,821`) — LIR has `ICmp*` (`lir.tks:33`) | **YES** (opcodes + encode + interp exist) |
| **bitwise / shifts** | `&`/`\|`/`^`/`<<`/`>>` (`lir.tks:32` `IAnd/IOr/IXor/IShl/IShrS/IShrU`) — declared, not lowered | **YES** |
| unary `~` | `lower.tks:309` | **YES** |
| **fat pointers** (str/slice) | the "wider fat-pointer ABI N2 gap" (`lower.tks:936,3767`, #382) | mostly (regalloc/encode ready; the fat ABI must be pinned) |
| **struct / field / index** | out-of-subset expr nodes (`lower.tks:615`) | **YES** (mem loads/stores exist) |
| **match patterns** | range/alt/null/slice/field (`lower.tks:3034,3035,3036,3203,3283`) | mostly (variant repr exists; needs LIR lowering) |
| if-without-else as value; **destructuring** binding | `lower.tks:2777,1819` | **YES** |
| **interpolation** holes | integer/bool ready; float/typed/format-spec (`lower.tks:3972,3987,3998`) | integer YES; **float blocked** |
| **runtime builtins** (concat/format/…) | outside the N1 run subset (`lower.tks:1059,4006`) | lowering YES; native RUN needs the runtime linked (system `ld`) |
| **FLOAT family** (all float ops) | binop/unop/cmp/cast/literal (`lower.tks:277,309,784`) + isel `B1-fp` (`isel_x86_64.tks:198,325,439`) | **NO — own-backend float isel** |
| **i128** | isel i128 materialization (`isel_x86_64.tks:170`) | partial — **own-backend i128 isel** |

**Headline:** of the ~60 lowering honest-stops, **~40 are integer/control/memory/fat/match/struct/
pointer lowering whose bottom-half substrate already exists → CLOSEABLE in .30**; **~20 are the FLOAT
family + i128 → own-backend float/i128 isel** (own-native work, no external dep). **Objfile emission
must be completed to whole-program + `.a`** (own-native). **NONE of this needs the E1 linker.**

---

## 2. Classification — each kill-C epic item: PODE .30 AGORA vs DEPENDÊNCIA DURA

### (a) Own-backend honest-stops (lowering)

| item | verdict | detail |
|---|---|---|
| integer comparisons lowering | **PODE .30** | LIR/isel/encode/interp ready — pure `lower.tks` completion |
| bitwise / shift lowering | **PODE .30** | `IAnd/IOr/IXor/IShl/IShrS/IShrU` ready |
| unary `~`, remainder, missing integer ops | **PODE .30** | ready |
| fat-pointer (str/slice) ABI + lowering | **PODE .30** | pin the fat ABI (ptr+len) once; then `.len`/index/literal — big but substrate ready |
| struct/field/index lowering | **PODE .30** | mem ops ready |
| match patterns (null/field/slice/range/alt over int+variant) | **PODE .30** | variant repr known; LIR lowering only |
| if-without-else-value, destructuring, integer interpolation | **PODE .30** | ready |
| **float family** (all float ops + float interpolation/match) | **DEP DURA (own-native)** | X = **own-backend float isel (`B1-fp`) + FPR regalloc/spill** — no external dep; pull-forward where regalloc supports, residual with SW13 |
| **i128** ops | **DEP DURA (own-native)** | X = **own-backend i128 register-pair isel** (#222, SW13) — no external dep |

### (b) Own-native `.o`/`.a` emission + system-`ld` link (the NEW kill-C substrate)

| item | verdict | detail |
|---|---|---|
| **complete whole-program `.o` emission** (all sections/symbols/relocs of the full program, not a subset) | **PODE .30** | extend `objfile_{elf,macho,coff}` from per-fixture objects to the whole program; own-native |
| **`.a` static-archive emission** | **PODE .30** | add the archive writer (ar/COFF-lib format) beside the object writers; own-native |
| **build-step swap: "emit `.o`/`.a` + system `ld`"** path (behind a flag; default flips when lowering=100%) | **PODE .30 (prep)** | add the alternate build path + `ld` invocation per OS/arch; DEFAULT stays C until float/i128 close |
| **producing a runnable native binary / full self-host** | **ACHIEVABLE with system `ld`** (NOT E1) | gated only on (a) lowering=100% (close float+i128 isel) + (b) complete `.o`/`.a` emission — both own-native; the link is the system `ld` |

### (c) The FFI KC1–KC4 (from the star-ref plan) — E1 removed from the chain

| item | verdict | detail |
|---|---|---|
| **raw-pointer ops lowering** (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | **PODE .30** | LIR mem loads/stores + address arithmetic; validate via interp |
| **macro resolver Tier 0** (constants/flags) | **PODE .30** | compiler front-end (header read + constant eval); no backend at all |
| **macro resolver Tier 2** (body→IR) | **PODE .30** *(after bitwise/shift lowering, itself .30)* | expands to integer arithmetic/bitwise LIR |
| **macro resolver Tier 1** (symbol alias) | **binding PODE .30; link via SYSTEM `ld`** | bind to the real symbol in .30; the **system `ld`** resolves it at link (was "E1 hard dep") |
| **KC1 vararg ABI** | **PARTIAL .30; native call via SYSTEM `ld`** | the per-target vararg rule set (SysV/AAPCS64/Win64) is own-native, PODE .30; the real libc variadic CALL links against libc via the **system `ld`** (+ C-call lowering) |
| **`#repr("c")` layout / `cabi` callbacks / `exp fn` C-ABI symbol emission** | **PODE .30** | objfile emits GLOBAL/FUNC symbols + layout is codegen; export marker is the EXISTING `exp` visibility (no `#[export]` attribute), unmangled when `abi="c"` (codegen.tks:7515 no-mangle path) |
| **`emit_c_header` `.h`** | **PODE .30** | pure text from the checked program — backend-independent |
| **KC3 external-C-symbol resolution** (`extern`/`extern from lib` vs `.a`/`.o`/`.so`) | **SYSTEM `ld`** (NOT E1) | own-native undef-symbol/reloc emission (PODE .30) + the **system `ld`** resolves against the C libs at link |
| **KC4 delete the C-FFI emitter** | **when lowering=100% + `.o`/`.a` + `ld`-link work** (NOT E1) | the LAST kill-C step; needs no E1 |

### (d) The own E1 linker

| item | verdict | detail |
|---|---|---|
| undef-symbol + relocation EMISSION in objfiles | **already present / complete in .30** | the linker INPUT is ready; whole-program completion is §2(b) |
| the **E1 own linker** itself (replace the system `ld`) | **LATER independence epic — NOT a kill-C blocker** | a post-kill-C step for total link independence; the system `ld` carries the link until E1 lands |

---

## 3. The .30 pull-forward crumb sequence (own-backend maturity + `.o`/`.a` + FFI own-native)

Each is own-native, additive, seed-safe, and rides **GATE-G + fixpoint gen1==gen2**; lowering crumbs
are validated by the **interp oracle** (`lir_interp`/`minst_interp`) + `isel/encode/objfile` unit tests
(the corpus still builds via the C backend in .30 — these crumbs CLOSE own-backend honest-stops and
build the `.o`/`.a`+`ld` substrate, shrinking kill-C). **Ordered by dependency + impact:**

| crumb | size | closes | notes |
|---|---|---|---|
| **KP1** integer **comparison** lowering | S | `lower.tks:807,821` | `ICmp*` + setcc; interp oracle |
| **KP2** **bitwise + shift** lowering (`&`/`\|`/`^`/`~`/`<<`/`>>`) | S | `lower.tks:290,309` | unblocks macro Tier 2 (KP12) |
| **KP3** **struct / field / index** lowering | M | `lower.tks:615` | mem loads/stores |
| **KP4** **fat-pointer ABI** (ptr+len) + str/slice literal/`.len`/index lowering | L | `lower.tks:936,3767` (#382) | pin the fat ABI once; the biggest single self-host lever |
| **KP5** **match-pattern** lowering (null/field/slice over int+variant; range/alt) | M | `lower.tks:3034…3389` | variant repr known |
| **KP6** if-without-else-value + **destructuring** binding + integer **interpolation** | M | `lower.tks:2777,1819,3987` | float/bool-format holes stay blocked (own-backend float) |
| **KP7** widen the **direct-call/runtime-builtin** lowering surface | M | `lower.tks:1059` | more builtins into the N1 run subset |
| **KP8** **raw-pointer ops** lowering (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | M | FFI (star-ref §2/§4.1) | mem ops; own-native |
| **KP9** **`#repr("c")` layout** + **`extern union`** own-backend layout | M | FFI G2 | own layout algo; objfile-tested |
| **KP10** **`cabi` callback** emission + **`exp fn` unmangled C-ABI export** (reuse the `exp` visibility; C symbol = the bare fn name; unmangled only when the artifact's `abi="c"`) + FFI-safe gate | M | FFI G3 / reverse-FFI | objfile GLOBAL/FUNC; reverse-FFI fixture (§5); NO `#[export]` attribute |
| **KP11** **`emit_c_header` `.h`** (prototypes + `#define` consts; close `header.tks:91` G5) | M | reverse-FFI | pure text; backend-independent |
| **KP12** **macro resolver Tier 0** (constant/flag) + **Tier 2** (body→IR, on KP2) + Tier 3 honest-error | L | FFI macros | Tier 1 symbol-link resolves via the system `ld` |
| **KP13** **vararg ABI rule set** (SysV/AAPCS64/Win64) as own-native tables + interp | M | FFI KC1 | the native libc variadic CALL links via the system `ld` (+ C-call lowering) |
| **KP14** **`#cconv` + `teko::ffi::errno()`** own-backend emission | S–M | FFI G4 | common cconv cases |
| **KP15** **`teko_rt_init/shutdown`** + panic-not-into-C runtime | S | reverse-FFI | own-native runtime |
| **KP16** **complete whole-program `.o` emission** + **`.a` archive** writer | L | §2(b) | own-native; ELF/Mach-O/COFF; the kill-C substrate |
| **KP17** **build-step swap PREP**: alternate "emit `.o`/`.a` + system `ld`" path behind a flag (per OS/arch `ld` invocation), DEFAULT stays C | M | §2(b) | flips to default when lowering=100% (float+i128 closed) |

**KP1–KP17 take the own backend from "N1 integer subset" to "integer+memory+fat+struct+match+FFI-codegen
complete, emitting whole-program `.o`/`.a` linkable by the system `ld`."** After .30, the ONLY residue
before flipping the default off C is the **float+i128 isel** (own-native) — exactly the owner's goal:
**kill-C becomes a small, own-native step with no external/linker blocker.**

---

## 4. Hard dependencies — re-mapped (own-native only; E1 removed as a blocker)

| item | blocked on X | own-native? | earliest |
|---|---|---|---|
| all **float** lowering + float FFI payloads | **own-backend float isel (`B1-fp`) + FPR regalloc/spill** | **yes** | pull-forward into .30 where regalloc supports; residual with SW13 (FPR-spill) |
| **i128** ops | **own-backend i128 register-pair isel** (`#222`) | **yes** | SW13 |
| whole-program **`.o`/`.a`** emission | own-native objfile completion (KP16) | **yes** | **.30** |
| **native binary / full self-host / KC4 delete-C** | lowering=100% **+** `.o`/`.a` emission **+** system-`ld` link (KP17 default-flip) | **yes** | as soon as float+i128 isel close (no external blocker) |
| **KC3** external-C-symbol resolution · **macro Tier 1** link · **KC1** native libc variadic call | the **system `ld`** at link (undef-symbol/reloc emission is .30) | **yes** | with KP17 (system-`ld` link path) |
| **own E1 linker** (replace the system `ld`) | the link-independence epic | **yes** | **LATER — post-kill-C; NOT a blocker** |

**The critical path to kill C (re-mapped):**
1. **Close the ~20 float + i128 isel honest-stops** (own-native — the only real remaining lowering gap).
2. **Complete own-native `.o`/`.a` emission** (KP16 — PODE .30).
3. **Swap the build step** from "emit `teko.c` + `cc`" to "emit `.o`/`.a` + **system `ld`**" (KP17 prep in
   .30; default-flip when 1+2 done).

**E1 is dropped from the critical path** — it is a subsequent *link-independence* epic that swaps the
system `ld` for the own linker, and it blocks NOTHING in kill-C.

---

## 5. FFI VALIDATION — the test lane, cross-OS/arch (owner .30 requirement)

FFI validation lives in the **regression test lane**, exercised by the **gen1 binary** from the dry
build — `teko build . --no-verify --release` produces gen1, then **`./bin/teko test .`** runs the
fixtures. Fixtures must need **nothing beyond gen1 + the runner's host toolchain**, and **must pass
own-native** (a fixture that only passes via the C emitter is a regression, star-ref §11).

### 5.1 Where it wires (mechanism)

- **Surface:** each FFI fixture is a standalone project under **`examples/regressions/ffi/<name>/`**, a
  first-class **`.tkb`** regressive (Gherkin over the reused `Tkr` model — `tkb-regression-format.md`),
  picked up by **`[tests] regression = ["examples/regressions"]`** in `teko.tkp` (add the `ffi` subtree
  alongside P1 `rt_behavior`). The runner (`build/regression.tks`) builds each once and runs it,
  asserting **exit / stdout / stderr**; `EXPECT_COMPILE_FAIL` for negatives; `trap` for panics.
- **Own-native build selection:** FFI fixtures build through the **own backend** (a `.tkb` `Given
  backend = "own"` step / `--backend=own`), NOT the C emitter, so validation is own-native. A fixture
  whose own-backend path is not yet landed is marked `Given pending = "<crumb>"` and **skipped-green
  until its crumb lands** (the runner already supports a skip verdict + per-platform overrides).
- **Cross-OS/arch matrix:** the lane runs on **each CI host** (`[platforms] targets =
  ["macos","linux","windows"]` × the arch runners); `targets = ["host"]` runs the fixture on whatever
  OS/arch the CI job is. Platform-specific exit/stdout differences use the `.tkb` **`on "<os-arch>"`**
  override.
- **Link is the system `ld`; `cc` only compiles the reverse-FFI C consumer.** A reverse-FFI fixture
  carries a minimal **`consumer.c`**; the runner: **(1)** `teko build` the fixture's `abi="c"` artifact
  **own-native** (own backend emits the `.o`/`.a` + `emit_c_header` emits the `.h`); **(2)** compile
  `consumer.c` with the **host `cc`** (the C side of a genuine C consumer) and **link Teko `.o`/`.a` +
  the consumer object with the SYSTEM `ld`** into the final executable; **(3)** run and assert
  exit/stdout. **Teko codegen and the link are `cc`-free** (`cc` only compiles the C source).

### 5.2 The FFI fixture suite (inputs → expected exit, per platform)

**A. USING C from Teko** (native oracle = own backend):

| fixture (`examples/regressions/ffi/…`) | asserts | oracle | own-native crumb |
|---|---|---|---|
| `extern_fn_libc_call` | `extern fn strlen(...)` → exit = len | exit | KP7 + system-`ld` link |
| `extern_macro_const_flag` | Tier-0 `O_RDONLY`/`SOCK_STREAM` inlined; exit = value | exit | **KP12 (.30, no link)** |
| `extern_macro_htonl_expand` | Tier-2 byte-swap expanded to IR; exit = swapped byte | exit | **KP12 (.30)** |
| `extern_macro_complex_rejected` | Tier-3 arbitrary macro | `EXPECT_COMPILE_FAIL` | **KP12 (.30)** |
| `ptr_deref_index_arrow` | `*p`, `p[i]`, `p->f` inside `unsafe` | exit = summed | **KP8 (.30)** |
| `ptr_arith_addr_of` | `p+n`, `p-q`, `addr_of(x)` | exit | **KP8 (.30)** |
| `ptr_null_union` | `ptr<T>\|null` / `uptr\|null` null test | exit | **KP8 (.30)** |
| `ptr_deref_in_safe_rejected` / `ptr_void_deref_rejected` / `ptr_optional_question_rejected` | safe-ctx deref / `void*` deref / `ptr<T>?` | `EXPECT_COMPILE_FAIL` | KP8 (.30) |
| `repr_c_struct_byval` | `#repr("c")` struct passed to a C stub | exit | **KP9 (.30)** + system-`ld` link |
| `extern_union_layout` | `extern union` read/write | exit | **KP9 (.30)** |
| `cabi_callback_noncapturing` | non-capturing closure as C callback; C stub invokes it | exit | **KP10 (.30)** + system-`ld` link |
| `cabi_callback_capturing_rejected` | capturing closure coerced to `cabi` | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `variadic_printf` | `extern fn printf(fmt, ...)`; stdout oracle | stdout | **KP13 tables (.30); native call green when KC1/C-call lands** |
| `cconv_stdcall` *(windows)* | `#cconv("stdcall")` call | exit | **KP14 (.30)** |
| `ffi_errno_read` | `teko::ffi::errno()` after a failing C call | exit | **KP14 (.30)** |

**B. EXPORTING Teko to C** (reverse-FFI; the marker is the EXISTING `exp` visibility + the `abi="c"`
knob — NO `#[export]` attribute. `abi="c"` ⇒ the `exp fn` is emitted unmangled under its **bare fn
name** into the generated `.h`; without `abi="c"` the `exp fn` stays Teko-mangled in the `.tkh` as
today. The FFI-safe gate applies to an `exp fn` only when `abi="c"`.):

| fixture | asserts | oracle | own-native crumb |
|---|---|---|---|
| `revffi_export_scalar` | `exp fn add(a,b)` in an `abi="c"` artifact; `consumer.c` links + calls the bare symbol `add(2,3)` | exit = 5 | **KP10 emit (.30); link via system `ld`** |
| `revffi_c_header_emitted` | the generated `.h` (iterating the artifact's `exp fn` + referenced `#repr("c")`) compiles under the host `cc` (prototypes + `#define` consts) | consumer compiles | **KP11 (.30, pure text)** |
| `revffi_repr_c_return` | an `exp fn` returns a `#repr("c")` struct; `consumer.c` reads fields | stdout | **KP9+KP10 (.30)** |
| `revffi_rt_init_contract` | `consumer.c` calls `teko_rt_init` then an `exp` allocating fn | exit | **KP15 (.30)** |
| `revffi_panic_no_unwind` | an `exp fn` that would panic returns `… \| error`, does NOT unwind into C | exit (error code) | **KP10+KP15 (.30)** |
| `revffi_export_nonffisafe_rejected` | `abi="c"` + `exp fn` with a capturing-closure param / bare-ref return | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `revffi_export_teko_variadic_rejected` | `abi="c"` + `exp fn` with a `params []T` variadic | `EXPECT_COMPILE_FAIL` | KP10 (.30) |

### 5.3 The OS/arch matrix

Every fixture runs on each lane host; the `.tkb` carries `on "<os-arch>"` overrides where a result
differs (Win64 stdcall, macOS-arm64 vararg-on-stack):

| lane host | arch | notes |
|---|---|---|
| Linux | **x86_64**, **arm64**, **riscv64** | full matrix; riscv via the riscv objfile/encode/abi |
| macOS | **arm64** | AAPCS64 (darwin vararg-on-stack variant) |
| Windows | **x86_64**, **arm64** | COFF; Win64 vararg (float-dup-into-GPR); `#cconv("stdcall")` here |

Fixtures whose native path is own-backend-coupled (`variadic_*` → KC1/C-call) are marked **"green when
the crumb lands"** via `Given pending` and skipped-green until then — the suite is authored NOW and
turns green incrementally on every platform at once. **No fixture is gated on E1** — the link is the
system `ld` throughout; when E1 lands, the same fixtures flip to the own linker with no rewrite (the
expected exit/stdout is unchanged — only the link tool changes).

### 5.4 The gen1-executable + no-`cc`-for-Teko constraint (explicit)

- No fixture may require a tool beyond **gen1 + the runner's host `cc`/`ld`.** The Teko side is
  **always own-native** (built by gen1's own backend); the LINK is the **system `ld`**; the host `cc`
  is invoked **only** to compile the reverse-FFI `consumer.c` (the C side of a genuine C consumer).
- A fixture that passes only via the C emitter is a **regression** and fails review.

---

## 6. Summary (counts + the re-mapped picture)

- **Own-backend honest-stops inventoried:** **~60 in `lir/lower.tks`** (the TAST→LIR "N1 subset" gap)
  + **~32 in `isel_x86_64.tks`** (dominated by the `B1-fp` FLOAT family + i128), mirrored in
  isel-arm64/riscv and the `lir_interp`/`minst_interp` oracle.
- **Closeable in .30 (KP1–KP17):** **~40 of the 60** lowering honest-stops (integer comparisons,
  bitwise/shifts, struct/field/index, fat-pointer str/slice, match patterns, if-value/destructuring/
  integer-interpolation, raw-pointer ops, `#repr("c")`/union layout, `cabi`/`exp`-C-ABI export, the `.h`
  emitter, macro Tiers 0+2, vararg ABI tables, cconv/errno, rt-init) **PLUS the kill-C substrate** —
  whole-program **`.o`/`.a` emission** (KP16) + the **system-`ld` build-step swap prep** (KP17).
- **Hard deps (own-native, NO external blocker):** the **FLOAT family + i128** isel (~20 honest-stops,
  own-backend work) — the ONLY residue before the default flips off C.
- **E1 DISSOLVED as a blocker:** the link is the **system `ld`**; E1 is a LATER link-independence epic
  that replaces `ld` and blocks nothing in kill-C. **KC3 / macro-Tier-1 / native-variadic-call are
  resolved by the system `ld`, not E1.**
- **Re-mapped critical path to kill C:** (1) close float+i128 isel → (2) complete `.o`/`.a` emission →
  (3) swap build to `.o`/`.a` + system `ld`. All own-native; no external/linker blocker.
- **FFI validation:** a cross-OS/arch `.tkb` suite under `examples/regressions/ffi/` (both directions +
  pointers), own-native, gen1-executable, system-`ld`-linked (host `cc` only for the reverse-FFI C
  consumer), wired into `[tests] regression` and fired by `./bin/teko test .` on every platform;
  crumb-coupled fixtures authored now and skipped-green until their crumb lands.

*Design-ahead, doc-only. No product code changed. Part A (`ref`) is out of scope here (another agent).
Owner ratifies before implementation.*
