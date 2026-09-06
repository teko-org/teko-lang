# Fix — `undefined reference to __dso_handle` on the x86_64-glibc direct-ld link — RECON + minimal patch

> **Status:** DESIGN + proposed patch. Read-only RECON on product code; this file is the SOLE edit. NO
> build, NO reseed, `teko test` NOT run in any form. Isolated worktree off `origin/fix/retirement`,
> branch `design/dso-handle-fix`; the main checkout + other agents' worktrees UNTOUCHED. For an
> implementer (or `ac69221b` post-§14) to apply.
>
> **What breaks.** Self-host on `x86_64-linux-gnu`: native lowering finishes EVERY item, then the FINAL
> link fails —
> ```
> ld: libc_nonshared.a(atexit.oS): in function `atexit': undefined reference to `__dso_handle'
> ld: <teko>: hidden symbol `__dso_handle' isn't defined
> ld: final link failed: bad value
> teko: .: cc failed to link the own-backend object
> ```
> Plus a separate WARNING `.note.GNU-stack section implies executable stack` (binutils 2.42, new runner).
> **Pre-existing** (present at `c86d8f85`, BEFORE 9-ops — not our regression). **x86-glibc-specific**:
> `arm64-glibc` links CLEAN with the identical runtime (same `atexit(tk_regions_free_all)`).

---

## 1. The link line — a DIRECT `ld`, and what it OMITS

The linux ELF link is `link_object_elf_direct` (`src/build/project.tks:2163`) — a DIRECT linker
invocation (`resolve_linker(spec.arch, Elf)` → `ld.lld`/`ld`, NOT the `cc` driver). Its argv
(`:2167-2191`) pushes, in order:

- `-pie --eh-frame-hdr -m <emu> -dynamic-linker <ld.so>`
- `<crt>/Scrt1.o`, `<crt>/crti.o` — **glibc** CRT startup objects (from `elf_crt_dir`)
- `objfile` (the own-backend `.o`), `objs.rt_obj` (`teko_rt.o`), `objs.assert_obj`
- `-L <crt> -lc` (+ libm/reachable extern libs)
- `<crt>/crtn.o`, `-o <binary>`

**The gap:** the line includes glibc's `Scrt1.o`/`crti.o`/`crtn.o` but **NOT `crtbeginS.o`/`crtendS.o`** —
the COMPILER-provided CRT objects (gcc/clang runtime dir, not glibc's) that a normal `cc`/`clang` driver
injects between `crti.o` and the user objects. **`crtbeginS.o` is where `__dso_handle` is defined.** The
direct-ld link deliberately dropped the driver (0.3.1.0 de-C native link) and, with it, the object that
defines `__dso_handle` — which nothing else on the line supplies.

**Contrast (why the design still has a driver path):** the C-backend route `build_cc_argv`
(`project.tks:1094`) invokes the `cc` DRIVER, which injects `crtbeginS.o` on its own, so THAT route never
sees this. The failure is specific to the own-backend DIRECT-ld route.

---

## 2. The source of the reference — `atexit(tk_regions_free_all)`

`src/runtime/teko_rt.c:2212` — `tk_termination_hook_once` calls `atexit(tk_regions_free_all)` (the
leak-clean termination hook). On glibc ≥ 2.34, `atexit` lives in `libc_nonshared.a(atexit.oS)` and is a
thin wrapper: `atexit(f)` → `__cxa_atexit(f, NULL, &__dso_handle)` — it takes the ADDRESS of the per-DSO
handle `__dso_handle` to key the process-exit finalizer. Pulling in glibc `atexit` therefore pulls an
undefined reference to `__dso_handle`, which `crtbeginS.o` would have defined.

**Why x86 fails and arm64 passes (same runtime):** it is the x86_64-glibc `libc_nonshared.a(atexit.oS)`
that references `__dso_handle`; the aarch64-glibc build links clean (its atexit path does not leave the
hidden reference undefined against the same direct-ld line). Both arches omit `crtbeginS.o`; only x86
exposes it — and binutils 2.42 on the new runner is strict enough to turn the hidden undefined into a
`final link failed: bad value` rather than tolerating it. This is a real code-path gap the stricter
toolchain surfaced — **NOT purely environmental** (a durable fix belongs in our code, not in hoping the
linker stays lax).

---

## 3. Minimal fix — RECOMMENDED (A): define `__dso_handle` WEAK + hidden in `teko_rt.c`

Provide the symbol from the MAINTAINED runtime (the editable boundary — `teko_rt.{c,h}`), so the
direct-ld line resolves it without depending on `crtbeginS.o`. **This is the standard workaround for
"undefined `__dso_handle`" under a custom / no-driver link**, and it is robust across BOTH link routes and
BOTH linux arches.

**Patch — add just above the atexit hook (`teko_rt.c:2204`, so the reason lives beside the `atexit`
call that pulls it):**

```c
// __dso_handle — glibc's atexit() lives in libc_nonshared.a and takes the ADDRESS of a per-DSO handle
// to register process-exit finalizers through __cxa_atexit. The compiler's own crtbeginS.o normally
// DEFINES this symbol, but teko's DIRECT ld link (link_object_elf_direct, project.tks:2163 — no cc
// driver) deliberately omits crtbeginS.o, so on a strict x86_64-glibc toolchain (binutils 2.42) the
// hidden reference from atexit.oS is undefined and the FINAL link fails ("hidden symbol __dso_handle
// isn't defined"). Provide it WEAK + hidden from the maintained runtime: when a crtbeginS.o IS on the
// line (the cc-driver route, build_cc_argv) its STRONG definition wins; on the direct-ld route this
// satisfies the reference. The VALUE is irrelevant for a MAIN executable — atexit passes &__dso_handle
// and program-exit finalization sweeps the NULL-keyed handler list regardless. aarch64-glibc links
// clean (it never left the reference undefined), so this is inert there; macOS/Windows/musl are not
// __GLIBC__ and never compile it (avoiding any clash with the system ___dso_handle on Mach-O).
#if defined(__GLIBC__)
__attribute__((weak, visibility("hidden"))) void *__dso_handle = 0;
#endif
```

**Why this is correct and safe (each concern checked):**
- **Resolves the failure:** the direct-ld line now finds `__dso_handle` in `teko_rt.o` (a defined, hidden
  symbol at a stable address) → the hidden undefined reference is satisfied → link succeeds.
- **Does not break the cc-driver route:** there `crtbeginS.o` defines `__dso_handle` STRONG; a `weak`
  definition is overridable, so the strong one wins — no multiple-definition error. `visibility("hidden")`
  matches crtbeginS.o's own hidden visibility, so no visibility conflict either.
- **Does not break arm64:** guarded to `__GLIBC__` (covers arm64-linux too), but the symbol is simply
  UNREFERENCED there → inert. If a future aarch64 glibc ever did reference it, the weak def would satisfy
  it identically.
- **Does not break macOS/Windows/musl:** `#if defined(__GLIBC__)` excludes them; on macOS the system owns
  `___dso_handle` and we must not shadow it — the guard guarantees we do not compile our own there.
- **Value `0` is correct for a main executable:** glibc uses `&__dso_handle` (the address) as the key, and
  process-exit finalization runs the NULL-keyed sweep, so the stored value is never dereferenced for a
  single executable. (If maximal fidelity to `-pie` crtbeginS.o is wanted, `= &__dso_handle` mirrors it
  exactly — a trivial, equally-correct alternative; `= 0` is the conventional minimal form.)
- **Boundary respected:** edits only `src/runtime/teko_rt.c` (the maintained C-runtime exception). The
  frozen C-twins (checker/codegen/vm `.c`) are NOT touched.

**Scope of `__GLIBC__` vs a broader ELF guard.** The symptom is glibc-specific: `__dso_handle` is pulled
by glibc's `libc_nonshared.a(atexit.oS)`, which musl has no equivalent of. `#if defined(__GLIBC__)` is the
precise guard — narrow enough to avoid the macOS/musl/Windows cases, broad enough to cover both glibc
arches. (If a future non-glibc ELF target ever hit the same, widen to `#if defined(__ELF__) &&
!defined(__APPLE__)`; not needed now.)

---

## 4. Rejected / secondary options

**Option B — add `crtbeginS.o`/`crtendS.o` to the direct-ld line.** The "textbook complete" fix: inject
the exact objects the driver would. REJECTED as the primary because locating `crtbeginS.o` requires
querying the compiler runtime dir (`cc -print-file-name=crtbeginS.o`), which RE-INTRODUCES a `cc`-driver
dependency into the pure-`ld` path that `link_object_elf_direct` exists to remove — more fragile, more
host-probing, and toolchain-version-sensitive (the object's path varies by gcc/clang version). Option A
gets the one symbol that is actually missing without dragging back the driver. (Option B is the fallback
ONLY if some future glibc atexit needs more of crtbeginS.o than `__dso_handle` — not the case today.)

**Option C — keep relying on the looser linker.** REJECTED: it is not a fix; the failure is a real missing
definition that the stricter binutils correctly reports. Not "purely environmental."

**Companion (SECONDARY, not the blocker) — the `.note.GNU-stack` warning.** binutils 2.42 warns when an
input object lacks a `.note.GNU-stack` section (it assumes an executable stack). The OWN-BACKEND ELF
emitter does not emit that section; the `cc`-compiled `teko_rt.o` does. This is a WARNING, not the link
failure — the `__dso_handle` fix alone unblocks the link. Two ways to silence it:
- **Minimal (recommended companion):** push `-z noexecstack` into the ELF link argv in
  `link_object_elf_direct` (`project.tks`, after `-pie` at `:2169`) — one token; it also correctly marks
  the binary non-exec-stack. Editable build boundary, no frozen-twin touch.
- **Proper (adjacent, larger):** emit an empty `.note.GNU-stack` (SHT_PROGBITS, flags = 0) section in the
  own-backend ELF emitter (`emit_elf`). REPORTED as an adjacent hardening, not built here — it is an
  emit-path change, whereas `-z noexecstack` closes the warning globally at the link with one line.

---

## 5. Recommendation

**Apply Option A** (weak+hidden `__dso_handle` in `teko_rt.c`, guarded `#if defined(__GLIBC__)`) — the
minimal, robust, boundary-respecting fix that unblocks the x86_64-glibc final link without touching the
frozen twins, without adding a toolchain probe, and without regressing arm64/macOS/Windows/musl or the
cc-driver route. **Companion:** add `-z noexecstack` to `link_object_elf_direct` to silence the
GNU-stack warning and mark the stack non-exec (optional; not required to unblock). It is NOT a purely
environmental issue — a code fix is warranted and durable.

This clears the last x86_64-glibc link blocker for the #110 full mem-paranoid verdict (mem was already
clean on every heavy pass; the `len`-builtin codegen arm, `18a1d589`, was the other blocker).

---

## 6. Anchors (verified on `origin/fix/retirement` this session)

| what | file:line |
|---|---|
| the DIRECT ELF link line (omits `crtbeginS.o`) | `src/build/project.tks:2163` (`link_object_elf_direct`); argv `:2167-2191`; CRT objects `Scrt1.o`/`crti.o` `:2175-2176`, `crtn.o` `:2188` |
| the `atexit` call that pulls `__dso_handle` | `src/runtime/teko_rt.c:2212` (`tk_termination_hook_once`) |
| the cc-DRIVER route (injects `crtbeginS.o`, so unaffected) | `src/build/project.tks:1094` (`build_cc_argv`) |
| linker RESOLUTION (direct `ld.lld`/`ld`, no driver) | `src/build/linker.tks:167` (`linker_candidates`, ELF arm), `:371` (`resolve_linker`) |
| platform-guard idiom to match | `src/runtime/teko_rt.c:24` (`#if defined(__APPLE__) || defined(__GLIBC__)`), `:1571` (`__GNUC__`/`__clang__`) |
| proposed patch insertion point | `src/runtime/teko_rt.c:2204` (just above `tk_termination_hook_once`) |

*Grounding: all `file:line` real on `origin/fix/retirement`. No build/test/reseed run. Fix confined to
`src/runtime/teko_rt.c` (+ an optional one-line `-z noexecstack` in `src/build/project.tks`); no frozen
C-twin touched.*
