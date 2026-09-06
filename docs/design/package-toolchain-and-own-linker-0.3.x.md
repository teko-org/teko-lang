# Package toolchain + own linker — design-ahead (0.3.x roadmap)

> **Status:** design-ahead / roadmap. NOT 0.3.1 execution. No crumbs, no implementation until the
> memory campaign closes (≤1.5 GB C-leg fixpoint + gen2==gen3 + green tests — D52). Reconstructed by the
> coordinator from the `arch-toolchain-linker` architect's report (the original worktree doc was lost to a
> force-remove; content preserved verbatim from the agent's code-grounded findings).

## Why this exists

Teko's first big projects are a multi-paradigm DB (as a local **package**), a **bare-metal kernel**, and
maybe an OS. Two owner rulings drive the toolchain: (1) packages must be readable/addressable/linkable so
side-cars (DB, `tk_data`, …) live as isolated projects; (2) a bare-metal kernel has **no OS `ld`** — teko
needs its **own linker**. The endgame is a **fully self-contained toolchain**: teko emits AND links its own
binaries, zero external tools (no cc/gcc/clang AND, for freestanding, no `ld`).

## 1. Package mode — the reality in `src/` (three corrections to the initial briefing)

1. **Generation ALREADY works** (owner's belief confirmed). `teko build` on an `[artifact] kind="package"`
   emits `<name>-<version>.tkl` — `src/build/project.tks:1173-1203`.
2. **The package UNIT is `.tkl`, not `.tkb`.** `.tkl` is a ZIP of `.tkh` (public header) + `.tkb` (typed
   tree) + `.tsym`. `.tkb` is the typed tree *inside* the package — a vocabulary fix, not a tension.
3. **There is no `package.tks`.** Package logic lives in `src/build/manifest.tks` (`[dependencies]`) +
   `src/build/project.tks` (`load_dep_program`/`load_deps_program`, `:104-183`). **Consumption already
   exists** at the typed-tree level: it reads `packages/<dep>-*.tkl` and merges before monomorphization
   (`:224`). So "teach `package.tks`" really means: **harden that read path + promote `[dependencies]`**
   (keys-only today, `manifest.tks:360`) + add LOCAL `path=` resolution + optionally extract a named
   `package.tks`.

## 2. Own linker — two-level model (Level 1 is already half-built)

- **Level 1 (M4):** teko already links ELF **without cc**, driving `ld`/`lld` directly with its own argv —
  `link_object_elf_direct` (`project.tks:1021-1052`); arch assertion + MinGW ban in `src/build/linker.tks`.
  It still uses the libc crt (`Scrt1.o`/`crti.o`/`crtn.o`, `-lc`). M4 finishes Mach-O/COFF direct + the
  no-C endgame.
- **Level 2 (own linker — a NEW milestone, "M-linker"):** resolve symbols + relocate + section/segment
  layout + synthesize teko's own `_start` + emit a **freestanding** image, with **no `ld`**. It **reuses
  the existing backend**: the linker's object reader is the inverse of `src/backend/objfile_elf.tks` (same
  section/symtab/reloc model, `R_X86_64_*` at `:188-198`); the ELF-writing half already exists; the entry
  seam is `wrap_native_entry`/`native_entry_stub` (`src/lir/lower.tks:6518-6566`), whose freestanding
  variant drops the libc-crt0 assumptions. **The bare-metal kernel is what forces Level 2** — hosted
  programs are content with Level 1 + OS `ld`.

## 3. D54 split (teach now / use later)

- **TEACH NOW (not D52-gated):** the consumption surface — `[dependencies]` promotion + LOCAL `path`
  resolution + `.tkl` read-path hardening.
- **USE, deferred behind D52 (native is last):** `.tkb`→native-`.o` linking + the own linker (Level 2).
- The package **server** stays out of scope (a separate repo, after native).

## 4. Design fork — resolved law-first (no HALT)

The one substantive fork: **Model A** = typed-tree pre-link (the legislated semantic contract, §215-226)
vs **Model B** = the owner's "link `.tkb` into `.o`". Resolution: **A is the semantic contract; B is a
post-native build-cache that the own linker enables.** Not a tension — two layers.

## 5. Adjacent findings (real gaps in `src/`, flagged — not yet issues)

- `load_dep_program` picks the **first** `.tkl` with **no version check** and **ignores the `.tkh` public
  gate**.
- `[dependencies]` **sub-keys are silently dropped** (`manifest.tks:360`).
- The **`freestanding` knob is a live stub that detours to `invoke_cc`** (`project.tks:921`) — **the last
  place the native path still reaches for a C driver.** This is a concrete no-C endgame target (it belongs
  with the M4 / M-linker work).

## 6. Kernel / freestanding compiler-toolchain requirements

> **Status:** design-ahead, post-native / post-M-linker era. NO crumbs, NO implementation. This section is
> the **compiler + native backend + M-linker** contract that teko's bare-metal **kernel** (a first big
> project) forces on top of the Level-2 own linker (§2). It is code-grounded against `src/backend/`,
> `src/lir/`, `src/parser/` seams as they stand today, so the implementer resumes in minutes once native +
> M-linker close. **Scope boundary:** this is the TOOLCHAIN side ONLY. The kernel's own internals — boot
> protocol, MMU/paging policy, drivers, scheduler — are a **separate future kernel-architecture study** and
> are NOT designed here. Where a feature touches the kernel (e.g. the syscall handlers), this doc stops at
> the compiler seam and names the boundary.

### 6.0 Why the kernel forces this (and the surface-form ruling)

The kernel runs with **no OS `ld`, no libc, no crt0** — exactly the Level-2 (M-linker) world of §2. Beyond
"resolve + relocate + layout + synthesize `_start`", six toolchain capabilities are load-bearing for
bare-metal code that hosted programs never need. Each is designed as **(a) a source surface** (a Teko
attribute/decl form) and **(b) a backend seam** (a named existing `.tks` emitter/lowering point it plugs
into). The C bootstrap twins stay FROZEN — every feature is planned in `.tks` on the native path; the only
maintained-C touchpoint is `src/runtime/teko_rt.{c,h}` (§6.6, the `tk_alloc` re-root).

**Surface-form ruling (law-first, corpus-consistency — no HALT).** The owner's brief writes some of these
Rust-style (`#[naked]`). Teko's real, shipped directive grammar is `#name` / `#name(args)` — `#test`,
`#os("…")`, `#arena_size(N)`, the DI lifetimes, `#must_free` — all parsed by one collector,
`parse_decl_attributes` (`src/parser/parse_decl.tks:869`), threaded through `parse_function` onto
`parser::Function` (`src/parser/ast.tks`, alongside `os_guard`/`is_unsafe`/`has_arena_size`/`arena_size`). A
bracketed `#[…]` form would fork the grammar for no semantic gain. **Resolution: these decls use the shipped
`#name(args)` directive form** — `#naked`, `#section("…")`, `#align(N)`. A naming decision settled by corpus
consistency, not a tension.

**The universal threading spine** (every attribute below rides it — stated once):

1. **Lex/parse** — a new arm in `parse_decl_attributes` (`parse_decl.tks:869`), storing onto new
   `parser::Function` fields (mirroring `has_arena_size`/`arena_size`).
2. **Check** — the field copies verbatim onto `checker::TFunction` (a pure carry — no new typing rule beyond
   the §6.2 naked-body guards; the many `TFunction { … }` re-constructors, e.g. `comptime_fold.tks:2684`,
   `monomorph.tks:1013`, each gain the new fields).
3. **Lower** — `src/lir/lower.tks` copies them onto `lir::LFunc` / `lir::LGlobal` (`src/lir/lir.tks:144` /
   `:232`), which today carry NONE of them — these structs gain the new fields.
4. **Emit** — `src/backend/objfile_elf.tks` (+ COFF/Mach-O twins) + the M-linker read the LIR fields.

### 6.1 Named / special output sections — `#section("…")`

**Surface.** A directive on a `fn` or a module-level datum: `#section(".text.boot")`, `#section(".bss.noinit")`,
`#section(".data.isr_vectors")`, etc.

**Backend seam — and the real cost.** `objfile_elf.tks` is a **fixed seven-section model**: `elf_section_names`
(`objfile_elf.tks:556`) hard-codes `["", ".text", ".rodata", ".symtab", ".strtab", ".shstrtab", ".rela.text"]`
(+ optional `.rela.rodata`); every function is `GLOBAL|FUNC` at a `.text`-relative offset with `st_shndx = 1`
(`:263`); every rodata datum collapses onto the single `.rodata` **section symbol** with an addend (`:391`,
`:513`). Named sections break the "one text, one rodata" assumption. The design:

- **LIR carries the placement.** `lir::LFunc` and `lir::LGlobal` gain `section: str` (`""` = the default
  `.text`/`.data`/`.bss` bucket by class). No new LIR op — placement is metadata on the symbol.
- **`objfile_elf` generalizes from fixed sections to section BUCKETS.** Replace the seven-name constant with a
  computed ordered set: the fixed sections PLUS one entry per distinct non-empty `section` string. Each
  function/datum lands in its named bucket at a bucket-relative offset; its `Symbol.shndx` is the bucket's
  index instead of the hard-wired 1/2. `sh_type`/`sh_flags` derive from the name PREFIX (`.text*` →
  `PROGBITS|ALLOC|EXECINSTR`; `.bss*` → `NOBITS|ALLOC|WRITE`, no file bytes; `.data*`/`.rodata*` → `PROGBITS`
  ± `WRITE`). Relocations partition per patch-site section — `minst.tks` already anticipates this: `RelocSect`
  (`minst.tks:47`) distinguishes `Text` vs the data section for a patch site, so the reloc-bucketing machinery
  is half-present.
- **The M-linker honors placement** by mapping named input sections to output segments in the image layout
  (§6.5): `.text*` → the executable segment, `.bss*` → the zero-fill (NOBITS) segment, a vector /
  page-aligned section pinned to its `#align` (§6.4) and a script-declared virtual address (§6.5).
- **COFF/Mach-O twins** get the same generalization (`objfile_coff.tks`, `objfile_macho.tks`) — lower priority;
  bare-metal is ELF-first.

### 6.2 `#naked` — prologue-less functions

**Surface.** `#naked` suppresses the compiler-emitted prologue/epilogue — for ISRs, the syscall-entry stub
(§6.6), the boot stub.

**Backend seam — the cleanest of the six, because the concept already exists.** `FrameLayoutX86` already
defines a FRAMELESS function: `size == 0` means "no prologue/epilogue" (`encode_x86_64.tks:58`), and
`compute_frame_layout_x86` (`:1084`) already returns a zero-size layout when a function has no frame slots and
no callee-saves. `#naked` is therefore **not new machinery — it is a forced verdict** on that path:

- `lir::LFunc` gains `naked: bool`.
- `compute_frame_layout_x86` (and the arm64 `compute_frame_layout` mirror) short-circuits to
  `{ size = 0; saved_gpr = []; saved_fpr = []; slot_offsets = [] }` when `naked` — skipping the
  `push rbp`/`mov rbp,rsp`/`sub rsp,N` prologue, the callee-save spills, and the `leave`/`pop` epilogue.
- **Checker guards** keep `#naked` honest (the ONLY new typing rule in §6.1–6.4): a naked body may contain
  **only** `asm(…)` blocks and returns — no locals needing a frame slot, no spillable temporaries, no
  by-value struct params isel would frame-copy (cf. `isel_x86_64.tks:978`). Reject anything the frameless
  layout cannot honor, with a precise diagnostic, rather than miscompile silently. This guard is also what
  stops `regalloc_x86.tks` from ever reaching for a `frame_base` spill slot in a naked body.

### 6.3 Raw inline assembly — `asm(…)`

**Surface.** A statement form with template + input/output operands + clobbers, for `cli`/`sti`, `cr3`/`cr0`
loads, port I/O, `rdmsr`/`wrmsr`, `syscall`/`sysret`. Illustrative shape:

```
#naked
fn load_cr3(root: u64) -> void {
    asm(
        template = "mov cr3, {0}",
        in = [ AsmOperand { constraint = "r"; vreg = root } ],
        out = [],
        clobbers = [ "memory" ]
    )
}
```

**Surface shapes (the Teko types the implementer adds).** Illustrative, design-ahead:

```
/** One `asm(…)` operand: a constraint string bound to a Teko value/l-value; `{n}`-referenced positionally. */
pub type AsmOperand = struct { constraint: str; vreg: u32 }

/** A raw inline-asm block: literal template + operand bindings + clobbers. Opaque to isel; unsafe-gated. */
pub type LAsm = struct { template: str; inputs: []AsmOperand; outputs: []AsmOperand; clobbers: []str }
```

**Backend seam.** `LAsm` is a new `lir::LOp` variant (the `LInst.op` union). It does NOT go through
instruction selection — it is a **pass-through**:

- A new physical minst variant `MRawX86` (added to the `MInstX86` union at `minst_x86.tks:491`; arm64 mirror
  `MRaw` in the `minst.tks:806` union) carries the substituted template + the regalloc-visible use/def sets
  derived from operands and clobbers.
- **Regalloc sees it as a black box with declared uses/defs/clobbers** (`regalloc_x86.tks` treats operand
  vregs as uses/defs and the clobber list as call-clobbered, so it will not keep a live value in a clobbered
  register across the block). This is the one place isel/regalloc must respect an opaque instruction — the
  operand model is exactly what makes that safe.
- The **encoder** emits it. **Recommendation: seed with a fixed, enumerated instruction whitelist** — the
  kernel's real need (`cli`, `sti`, `hlt`, `mov cr*`, `rdmsr`/`wrmsr`, `in`/`out`, `syscall`/`sysret`,
  `iretq`) encoded natively in `encode_x86_64.tks`, so **no external assembler is ever required** (matching
  the no-C/no-`ld` endgame). A free-form text template is a later widening, not the seed.
- **`asm(…)` is unsafe-gated:** legal only inside `unsafe fn` / `unsafe { }` (it can violate every memory and
  ABI invariant the checker otherwise guarantees).

### 6.4 Page alignment — `#align(N)`

**Surface.** `#align(N)` aligns a symbol/section to `N` bytes (page = 4 KiB, larger for huge pages) — for
page tables / MMU structures, e.g. `#align(4096)` + `#section(".data.page_tables")` on a `[512]u64`.

**Backend seam.** `N` is a bare power-of-two literal (parsed like `#arena_size(N)`, `parse_arena_size_arg` at
`parse_decl.tks:929`, with an added power-of-two check). It threads to `lir::LGlobal`/`lir::LFunc` as
`align: u32` — the LIR already has the CONCEPT (`LAlloca.align` `lir.tks:83`, `LStructLayout.align` `:527`),
just not on top-level symbols. Emission:

- **`objfile_elf`** sets the owning section's `sh_addralign` to the max `#align` of its members and pads each
  member to its own `#align`; `#align` on a datum in a `#section` bucket raises that bucket's alignment.
- **The M-linker** propagates `sh_addralign` up to SEGMENT granularity: a page-aligned section forces its
  output segment to a page boundary in both file and virtual layout (§6.5) — exactly what the MMU requires for
  a page-table root.

### 6.5 Linker-script symbols + freestanding image layout

**Surface (the script).** The M-linker consumes a **teko link script** (proposed `*.tkld`, teko's own minimal
format — NOT GNU `ld` syntax, per the no-external-tools endgame) that declares the image's SEGMENT layout and
DEFINES boundary symbols the kernel reads: load address + virtual base (a higher-half kernel places text at
e.g. `0xffffffff80000000`); segment order + per-segment page alignment; and section-boundary symbols
(`__text_start`/`__text_end`, `__bss_start`/`__bss_end`, `__kernel_end`, per-section load vs. virtual address)
emitted as **absolute symbols** in the final image.

**Surface (the kernel side).** The kernel references a linker-defined symbol as an **`extern` datum whose
ADDRESS is the symbol** (it reads the address, never a value): init zeroes BSS by walking `&__bss_start`..
`&__bss_end`; the allocator seeds its heap at `&__kernel_end`. This reuses the shipped `extern` machinery
(`is_extern`/`c_symbol`, `parse_decl.tks`) — a linker-defined symbol is just an external symbol the M-LINKER
(not libc) resolves. No new source surface beyond documenting the convention.

**Backend seam.** The M-linker's image writer is the inverse of `objfile_elf.tks`'s ELF program-header half;
§2 already names the entry seam (`wrap_native_entry`/`native_entry_stub`, `lower.tks:6531`/`:6564`), whose
freestanding variant drops the crt0 assumption. The freestanding layout: (1) read every input object's section
buckets (§6.1); (2) merge same-named buckets, assign each output SEGMENT a virtual address + page alignment
(§6.4) from the script; (3) synthesize the script's boundary symbols as absolute symbols at the resolved
segment/section edges, then resolve the kernel's `extern` references to them (the same `R_X86_64_*` reloc
model at `objfile_elf.tks:188`); (4) emit an ELF executable with program headers (`PT_LOAD` segments) + the
script's entry point — no `PT_INTERP`, no dynamic section, no crt.

### 6.6 Syscall interface — the naked entry + `Table<…>` dispatch (POSIX ABI)

The kernel communicates ENTIRELY via syscall, so it needs a syscall **table**: the naked syscall-entry stub
reads the syscall number and dispatches number → handler through the stdlib **`Table<…>`** (COL-Q20 / crumb
`0088`). This is where `Table<…>` **earns its purpose on the metal**.

**Surface + composition.**

- **Entry stub — `#naked` + `asm(…)`.** x86-64 `syscall` enters with the number in `RAX`, args in
  `RDI/RSI/RDX/R10/R8/R9`, return via `RCX`/`R11` + `sysret`. The entry is `#naked` (§6.2) because it must NOT
  touch the stack before switching to a kernel stack, and uses `asm(…)` (§6.3) for the `swapgs`/stack-switch/
  `sysret` mechanics. It marshals `(num, args)` and tail-calls the dispatcher.
- **Dispatch via `Table<num, handler>`.** The dispatcher is ordinary (framed) Teko: look the number up in a
  `Table<u64, SyscallHandler>`, call the handler. Illustrative handler shape:

```
/** A syscall handler: the six raw register args → POSIX result (>= 0, or a negated errno). */
pub type SyscallHandler = fn(a0: u64, a1: u64, a2: u64, a3: u64, a4: u64, a5: u64) -> i64
```

- **POSIX ABI (owner ruling).** The table is keyed by POSIX/Linux-x86-64 syscall NUMBERS with POSIX semantics
  (`read=0`, `write=1`, `open=2`, …), so the kernel's userland surface is POSIX-compatible. The numbering is
  DATA (the table keys), not compiler machinery — the toolchain side is only the naked-entry + `Table`
  dispatch; the **handlers themselves are kernel internals** (the separate kernel-architecture study — boundary
  noted).

**Dependency flags (call these out to the implementer):**

- **`Table<…>` lives in the `tk_data` PACKAGE, not core (DECISION_LOG D55, owner 2026-08-20).** D55 rules the
  monolith keeps ONLY what the compiler itself consumes (List/Map/…); everything else — `Table` explicitly
  named — moves to the `tk_data` package. The compiler does not use `Table`, so `Table<…>` will not be a core
  stdlib type: **the kernel depends on the `tk_data` package**, resolved through the package-consumption path
  this very doc designs (§1 `[dependencies]` + LOCAL `path=` + `.tkl` read). So the kernel's syscall table is
  a package dependency, cleanly, not a monolith addition — this ties §6.6 back to §1. (Whether a bare-metal
  target consumes a package `.tkl` the same way a hosted one does is a real question for the M-linker
  milestone — the `.tkb`→native-`.o` path of §3, "use, deferred behind D52.")
- **`Table` generic-key gap.** `Table<u64, …>` needs a NON-`str` key + a function-pointer value; today only
  `Map<V>` exists, `str`-keyed (`collections/map.tks:9-13`, the structural `Hashable & Eq` atom is opaque on
  the current generic stack). So `tk_data`'s `Table` must solve generic keys, OR the syscall table takes the
  **dense-array fallback**: a `[]SyscallHandler` indexed by the (small, contiguous) POSIX number — simpler,
  faster, and it sidesteps both the generic-key gap AND the allocator dependency below. **Recommendation: the
  syscall entry path uses the dense array; a hash `Table` is the general vehicle but not required here.** Not
  a tension — a sequencing note.
- **`tk_alloc` / freestanding allocator (the one maintained-C touchpoint).** A hash `Table<…>` allocates. On
  the metal there is NO libc `malloc`, and `tk_alloc` (the sole allocation seam, `teko_rt.h:158`) wraps
  `malloc` today. For a freestanding `Table`, `tk_alloc` must be **re-rooted onto the `tk_region` bump
  allocator** (already present: `teko_rt.h:176`, `tk_region_alloc` `:198`, current-region stack
  `tk_region_enter` `:369`, designed to back `tk_alloc` behavior-preservingly). The kernel seeds that region
  at `&__kernel_end` (§6.5). This is the standing `teko_rt.{c,h}` exception to the Teko-only law. The
  dense-array fallback needs NO allocator and **removes** this dependency — a second reason to prefer it for
  the entry path.

### 6.7 Feature → seam summary

| # | Feature | Source surface | Primary backend seam |
|---|---------|----------------|----------------------|
| 1 | Named sections | `#section("…")` on fn/datum | `objfile_elf.tks:556` (fixed 7-section → buckets); `minst.tks:47` `RelocSect` |
| 2 | Naked fns | `#naked` | `encode_x86_64.tks:58`/`:1084` frameless `size==0` verdict |
| 3 | Inline asm | `asm(template, in, out, clobbers)` (unsafe-gated) | new `LAsm` op → `MRawX86` (`minst_x86.tks:491`) pass-through; native encode |
| 4 | Page align | `#align(N)` | `objfile_elf` `sh_addralign` + M-linker segment page-align |
| 5 | Linker-script symbols | `*.tkld` script + `extern` boundary symbols | M-linker image layout (inverse of `objfile_elf` program-headers); `wrap_native_entry` `lower.tks:6531` |
| 6 | Syscall table | `#naked`+`asm` entry → `Table<u64, SyscallHandler>` (POSIX) | LIR/backend for 1–3; deps: `Table` in `tk_data` pkg (D55), `tk_alloc`→`tk_region` (`teko_rt.h:176`) |

### 6.8 Risks / law tensions

- **No genuine tension → no HALT.** Every fork resolved law-first: directive form via corpus consistency
  (§6.0); `#naked` via the pre-existing frameless verdict; the syscall `Table` via DECISION_LOG D55 (it lives
  in `tk_data`) with a documented dense-array fallback.
- **Biggest real risk — the `objfile_elf` fixed-section rewrite (§6.1).** It touches the symbol table,
  reloc-bucketing, and section-header machinery at once. Mitigate by landing it behind the M-linker milestone
  with a regression corpus that pins byte-exact output for the default (single-`.text`) case FIRST, then adds
  named-section cases — so the generalization cannot regress hosted output.
- **`asm(…)` is the largest new surface.** Contain it: seed with the enumerated instruction whitelist (§6.3),
  unsafe-gated, before any free-form template.
- **Adjacent findings (reported, not issues I open):** (a) `Map<V>`'s `str`-only key
  (`collections/map.tks:9`) will not serve `Table<u64, …>` — the generic-key / `Hashable & Eq` gap must close
  for `tk_data`'s `Table`, or the syscall path takes the dense-array fallback. (b) Consuming a package `.tkl`
  from a bare-metal (freestanding) target is untested — it depends on the `.tkb`→native-`.o` path (§3) the
  own linker enables. Both flagged upward.

### 6.9 Regression-fixture intent (for when impl begins — not now)

Design-ahead, so no fixtures land yet; the intended shape, so the implementer knows the gate:

- **Named sections:** a program placing a fn in `.text.boot` and a datum in `.bss.noinit`; assert the emitted
  ELF has the two extra section headers with correct `sh_type`/`sh_flags`, and that the default (no
  `#section`) corpus stays **byte-identical** (the anti-regression pin).
- **`#naked`:** a naked fn with only an `asm` body; assert NO `push rbp`/`sub rsp` bytes precede its first
  instruction; a naked fn with a frame-needing body is a **checker reject** (fixture asserts the diagnostic).
- **`#align(N)`:** a `#align(4096)` datum; assert `sh_addralign == 4096` and the datum's file/virt offset is
  page-aligned after the M-linker.
- **Linker-script symbols:** a tiny freestanding image defining `__bss_start`/`__bss_end`; assert the kernel
  stub's relocations resolve to the script-declared addresses and the image has `PT_LOAD` headers, no
  `PT_INTERP`.
- **Syscall dispatch:** an entry stub + a 2-entry dispatch (`write`, `exit`); a hosted harness driving the
  dispatcher directly (not a real `syscall`) asserting number→handler routing + the POSIX return convention.
  Native exit-code fixtures follow the existing backend test pattern (`*_test.tkt`).
