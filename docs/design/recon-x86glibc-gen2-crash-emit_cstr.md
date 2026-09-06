# RECON — x86_64-glibc gen2 crash lowering `emit_cstr` (unmasked by the link fix, NOT caused by it)

> **Status:** RECON, read-only (#14). This file is the SOLE edit. NO build, NO test, NO reseed. Isolated
> worktree off `origin/fix/retirement` (HEAD `d9613aea`), branch `design/x86glibc-crash-recon`; main
> checkout + other worktrees UNTOUCHED.
>
> **Symptom (CI `f22e5269`, x86-glibc artifact).** `gen2` builds + links (the `__dso_handle` fix works),
> then CRASHES at runtime while rebuilding the source in the fixpoint:
> `gen2 native-lowering item 49/6896: fn teko::backend::emit_cstr` → `FATAL signal — a generated program
> crashed (M.1)` → `VERDICT: gen2 built but does not rebuild the source`. **`arm64-glibc` and `x86-musl`
> PASS** (gen2 rebuilds clean). Was masked because the x86-glibc link always failed before — gen2 never ran.

---

## 1. Verdict up front

**The crash is NOT caused by my `__dso_handle` fix nor by the `-z noexecstack` companion.** It is a
**PRE-EXISTING, x86_64-glibc-specific own-backend defect** that the now-working link merely UNMASKED (this
is the first time an x86_64-glibc gen2 ever ran). Both suspects are exonerated by the cross-platform pass
matrix below; the crash lives in the intersection **x86_64 ISA ∩ glibc runtime**, triggered by the
fat-pointer / slice-growth primitives that `emit_cstr` exercises.

---

## 2. `-z noexecstack` — EXONERATED (three independent proofs)

I verified the flag WAS applied (`project.tks:2171` pushes `"noexecstack"` after `-z`, inside
`link_object_elf_direct`). But it cannot be the cause:

1. **Same target, opposite outcome (musl vs glibc).** `NativeTarget` has NO separate musl variant — the
   enum is `{ Arm64Macho; Arm64Linux; X8664Linux; X8664Windows }` (`project.tks:2604`), and both x86-musl
   and x86-glibc resolve to **`X8664Linux`** (`:2646-2647`, `:2702`). So both take the IDENTICAL route
   `link_object_elf_direct` (`:1950`) with the IDENTICAL `-z noexecstack`, the SAME x86 isel, the SAME
   machine code — yet **x86-musl passes**. The only run-time difference is the libc.
2. **Same flag on arm64, which passes.** `link_object` routes BOTH `X8664Linux` (`:1950`) AND
   `Arm64Linux` (`:1951`) through `link_object_elf_direct`, so **arm64-glibc gets the exact same
   `-z noexecstack`** and rebuilds clean. If the flag broke exec-stack-needing code, arm64-glibc would
   break too.
3. **The own backend emits no stack-executed code.** Grep across `src/lir`, `src/backend`, `src/codegen`,
   `src/runtime/teko_rt.c` for `trampoline` / `PROT_EXEC` / `mprotect` / nested-fn / "executable stack"
   finds NOTHING. Teko closures are not GCC-style stack trampolines, so a non-exec stack cannot fault
   generated code. `-z noexecstack` only silences the `.note.GNU-stack` warning; it changes no behaviour.

**Consequence:** reverting `-z noexecstack` will NOT fix the crash. It may be kept (it correctly marks the
stack non-exec, and the backend provably needs no exec stack) or dropped for cleanliness — but it is not
the issue, so I do not frame reverting it as the fix.

## 3. The weak `__dso_handle` — NOT the runtime-crash cause (still the correct LINK fix)

`teko_rt.c:2216` — `__attribute__((weak, visibility("hidden"))) void *__dso_handle = 0;` under
`#if defined(__GLIBC__)`.
- **arm64-glibc compiles the identical symbol and passes** → the symbol per se does not crash a glibc run.
- **The crash is mid-run** (gen2 lowering item 49/6896), whereas `__dso_handle` is relevant ONLY at
  process exit (glibc `atexit`/`__cxa_finalize` takes `&__dso_handle`). A mid-compilation SIGSEGV is not
  the exit path. `__dso_handle = 0` is not dereferenced during the run.
- It remains the **real, correct link fix** — the link now passes because of it; keep it.

## 4. What the crash actually is — a pre-existing x86_64-glibc defect, unmasked

`emit_cstr` (`src/backend/dwarf.tks:127`) is a tiny `[]byte`-building loop:
```
loop { if i >= s.len { break }  b = teko::list::push(b, s[i])  i = i + 1 }  teko::list::push(b, 0 to byte)
```
The constructs gen2 must lower here are exactly the **fat-pointer / slice primitives**: a `str` `.len`
read, a `str` byte-index `s[i]`, a `[]byte` `teko::list::push` (the slice-GROWTH builtin `tk_slice_push`/
`grow`), and a `0 to byte` cast. gen2 crashes WHILE LOWERING this function — i.e. gen2's own compiled
lowering routine faults on one of these node kinds.

The pass/fail matrix pins the differentiator to the **x86_64 ∩ glibc** intersection:

| lane | ISA | libc | `-z noexecstack` | weak `__dso_handle` | gen2 rebuild |
|---|---|---|---|---|---|
| arm64-glibc | arm64 | glibc | yes | yes | **PASS** |
| x86-musl | x86_64 | musl | yes | no (`!__GLIBC__`) | **PASS** |
| **x86-glibc** | **x86_64** | **glibc** | yes | yes | **CRASH** |

- Not a pure x86 codegen COVERAGE gap: x86-musl uses the same isel and the same machine code, and passes —
  so the x86 lowering does not simply lack an arm; it produces code that FAULTS only under glibc's runtime.
- Not a pure glibc issue: arm64-glibc passes — so the glibc runtime alone is fine; it needs the x86 code.

**Strongest hypothesis (evidence-consistent):** an x86_64 own-backend codegen bug in the fat-pointer /
slice-growth path (a subtly wrong pointer or length out of a slice-grow / `str`-index) that is HARMLESS
under musl's allocator/heap layout but FATAL under glibc's (glibc's malloc faults/aborts on the corruption
or bad size that musl tolerates), and ABSENT on arm64 (a different isel). It was always present in
x86-glibc but never observed because the link failed and gen2 never ran. The `__dso_handle` fix unblocked
the link and thereby exposed it — exactly the "unmasked a deeper crash" the report describes.

## 5. Recommendation

1. **Keep the weak `__dso_handle`** — it is the correct link fix; the link now passes; arm64-glibc +
   x86-musl coexist with / depend on it. Do NOT revert it.
2. **`-z noexecstack` is exonerated** — reverting it will not fix the crash. Keep it (harmless, correct) or
   drop it for cleanliness; either way it is not the issue.
3. **Treat this as an independent, pre-existing x86_64-glibc codegen/runtime bug.** Where the implementer
   should look (in priority order):
   - The **x86_64 own-backend lowering / isel of the slice-GROWTH builtin** (`teko::list::push` on `[]byte`
     → `tk_slice_push`/`grow`) and the **`str` byte-index `s[i]`** — the two fat-pointer ops `emit_cstr`
     exercises. Diff the x86_64 isel against the arm64 isel for these paths (arm64 passes).
   - The **C runtime `tk_slice_push` / fat-pointer size+alignment** (`teko_rt.c`) for an assumption glibc's
     allocator enforces and musl's does not (e.g. a realloc size or alignment that overruns under glibc).
   - Since gen2 crashes LOWERING `emit_cstr`, the gen2-glibc BINARY itself carries the miscompiled routine:
     run gen2-glibc under a debugger (or `TEKO`-native fault backtrace) to capture the faulting instruction
     while it lowers item 49 — that names the exact bad codegen directly.
4. **Does not block the memory verdict** — that comes from arm64-glibc / x86-musl (both PASS) after the §14
   match fix. This x86-glibc crash is a separate track.

---

## 6. Anchors (verified on `origin/fix/retirement`, HEAD `d9613aea`)

| what | file:line |
|---|---|
| `-z noexecstack` applied (shared ELF direct-link) | `src/build/project.tks:2171` (inside `link_object_elf_direct:2163`) |
| per-target routing (x86 AND arm64 linux → direct-ld) | `link_object:1947` → `X8664Linux:1950` / `Arm64Linux:1951` |
| `NativeTarget` enum (no separate musl → x86-musl == X8664Linux) | `project.tks:2604`; resolvers `:2646-2647`, `:2702` |
| weak `__dso_handle` (glibc-guarded) | `src/runtime/teko_rt.c:2216` |
| `emit_cstr` (the crash trigger) | `src/backend/dwarf.tks:127` — `str.len` + `s[i]` + `[]byte` push + `0 to byte` |
| exec-stack usage in backend/runtime | NONE (grep: no trampoline/PROT_EXEC/mprotect) |

*Grounding: all file:line real on `origin/fix/retirement`. No build/test/reseed run. Conclusion:
`-z noexecstack` and `__dso_handle` are both exonerated; the crash is a pre-existing x86_64-glibc
own-backend defect in the fat-pointer/slice path, unmasked by the working link.*
