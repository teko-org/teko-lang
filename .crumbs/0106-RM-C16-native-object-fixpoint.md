---
seq: 0106
crumb-id: RM-C16
milestone: M4
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild (native seed)"
deps: [RM-C15, S16-SWEEP]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:322-327"              # C16 native-object fixpoint + C removal
  - "docs/design/plano-mestre-0.3.1-implementacao.md:291"              # M4 RM-C16 row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:398-410"          # fixpoint migration + C-route removal
  - "docs/design/arena-especificacao-unica-0.3.1.md:43-56"            # native-only destination
  - "src/backend/objfile_ar.tks:128"                                   # objfile_ar determinism audit
---

# 0106 · RM-C16 — native-object fixpoint + remove the C route (`teko.c` + `cc`)

> Migrate the fixpoint criterion from `gen2.c==gen3.c` to **native-object-reproducible** (deterministic
> `.o`: no timestamp, stable symbol/section order, no absolute path, canonical relocations). When the 4
> native CI legs are green AND the object reproduces, REMOVE the C route entirely (`teko.c` + `cc`); the 2
> C CI legs become native; the bootstrap reseed becomes the native object/binary.

## Goal

The "no C" milestone made literal for the COMPILE path: the fixpoint that guarantees the compiler rebuilds
itself byte-for-byte moves from comparing two emitted `teko.c` files to comparing two emitted native
OBJECTS. This requires the `.o`s to be DETERMINISTIC — no embedded timestamp, stable symbol/section
ordering, no absolute paths, canonical relocations (auditing every `objfile_*` + `objfile_ar`). Once that
holds AND the four native CI legs (x86_64-linux, arm64-linux, arm64-macos, x86_64-windows) are green, the
C route is REMOVED: the emitted `teko.c` and the `cc` invocation are deleted, the 2 remaining C CI legs are
retargeted to native, and the bootstrap seed becomes the native object/binary. This is the terminal
milestone: `teko.c` was the transitional crutch; the endgame is a binary linkable by `ld` with NO C
compiler. It is the reseed that flips the reseed CRITERION (native seed migration; not a new teaching — 1
folded), and it is a **byte-mover** (the artifact under the fixpoint changes from C to object) driving the
reseed. It REQUIRES S16-SWEEP done (the C runtime dependency gone) — the native endgame removes the COMPILE
dependency; §16 removed the RUNTIME dependency; both must close for "no C" to be true.

## Where

- `src/backend/objfile_elf.tks` / `objfile_macho.tks` / `objfile_coff.tks` / `objfile_ar.tks:128` — the
  determinism audit: no timestamp, stable symbol/section ordering, no absolute path, canonical
  relocations. Any non-determinism is fixed HERE (it is what the new fixpoint detects).
- The fixpoint harness / CI scripts — change the fixpoint comparison from `gen2.c==gen3.c` (text) to
  native-object byte-identity (`gen2.o==gen3.o` per unit / the joined binary).
- `src/codegen/codegen.tks` + the C-emit path — DELETE the C route (`teko.c` emission) once the native
  fixpoint is green on all four legs.
- The build driver (`project.tks`) — remove the `cc` invocation / `TEKO_CC` C-compile path; the OS `ld`
  becomes the only external tool.
- The bootstrap seed — becomes the native object/binary (the reseed harvest).

## How

1. **Make the objects deterministic FIRST** (audit `objfile_*` + `objfile_ar`): strip timestamps, fix
   symbol/section ordering to a stable canonical order, remove absolute paths, canonicalize relocations.
   This is the precondition — the new fixpoint cannot pass on non-deterministic `.o`s.

```teko
/**
 * object_reproduces — whether two independently-emitted native objects for the same unit are byte-identical:
 * the native fixpoint criterion replacing gen2.c==gen3.c. Requires deterministic emission (no timestamp,
 * stable symbol/section order, no absolute path, canonical relocations). A mismatch is the detector that a
 * non-determinism leaked into an objfile writer — STOP and fix the writer, never relax the criterion.
 *
 * @param a  the first emitted object bytes
 * @param b  the second emitted object bytes (independent rebuild)
 * @return true when byte-identical; false names the first differing offset for the writer fix
 * @since 0.3.1
 */
fn object_reproduces(a: []byte, b: []byte): bool
```

2. **Migrate the fixpoint criterion**: the CI/harness compares native objects (per-unit `.o` and/or the
   joined binary) for byte-identity instead of the two `teko.c` texts. `gen2.o==gen3.o` becomes the
   detector.
3. **Gate the C-route removal on 4 green native legs + object reproduction**: only when all four native CI
   legs pass AND the object reproduces, proceed to removal.
4. **Remove the C route**: delete the `teko.c` emission path (codegen C-emit) and the `cc` invocation; the
   2 C CI legs are retargeted to native (CI triage). The OS `ld` is the only external tool left.
5. **Reseed the bootstrap as the native object/binary**: the seed is no longer `bootstrap/teko.c` but the
   reproduced native artifact. This is the native seed migration (folded, not a new teaching).
6. **CI triage** (owner): a `fixpoint (native)` failure was EXPECTED while native stopped at a degree — now
   it must be GREEN; a `produce this leg` failure is REAL (read head=100).

## Rulings & laws

- **Teko-only:** `src/backend/*.tks` + build/CI; the C route is DELETED (the crutch is spent).
- **W15 full Javadoc** on `object_reproduces` + the determinism helpers; flatten; no `//`.
- **`teko.c` é muleta; endgame é binário linkável por `ld` sem compilador C (owner 2026-08-19):** the C
  route sai quando as pernas native fecham verde + o objeto reproduz.
- **Fixpoint migrates, byte-determinism is the detector (arena-espec §55-56 / reducao §322-327):** a
  reproduction miss is a root-cause fix in the objfile writer, NEVER a relaxed criterion.
- **Provenance gate is REVOKED (owner):** the native seed is accepted à força; if it fails, CI fails
  immediately — no fallback to a published version, no new provenance machinery.
- **Removals = clean expurgo, NO tombstone:** the C route vanishes; no "C route removed" diagnostic.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each green step;
  reseed ONLY here (the native seed migration); the fixpoint is now native-object-reproducible; sweep
  `.tkt`/`.tkr` after the harness change.

## Fixtures

none — the fixpoint self-build exercises this. The native-object-reproducible fixpoint IS the test: the
compiler emits its own objects twice and compares them byte-for-byte across all four native CI legs. A
non-determinism surfaces as a `gen2.o!=gen3.o` fixpoint break, not a missed fixture (owner: "backend native
— REMOVER; o CI exercita").

## Gate

`[RITUAL]` — full native ladder + a genuine reseed (the native seed migration). "Green" = the four native
CI legs pass, `gen2.o==gen3.o` byte-identical (per unit + joined binary), the C route (`teko.c` + `cc`) is
REMOVED, the 2 former-C legs are native, and the bootstrap seed is the reproduced native object/binary.
Reseed-class: `fixpoint-rebuild (native seed)`.

## Deps

`RM-C15, S16-SWEEP` — verbatim from 000-INDEX (the per-unit `.o` emit + the §16 C-runtime death; both the
COMPILE and RUNTIME C dependencies must be closable for "no C").

## Done when

The fixpoint is native-object-reproducible (`gen2.o==gen3.o` byte-identical on all four legs), the C route
(`teko.c` + `cc`) is removed, the CI legs are all native, the bootstrap seed is the native artifact, and
"no C compiler" is literally true — the binary is linked solely by the OS `ld`.
