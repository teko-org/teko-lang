---
seq: 0208
crumb-id: EX-B9
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, RT-L4-ENV]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
  - ".crumbs/0124-RT-L4-ENV-envp-capture-zerolibc.md:0"
---

# 0208 · EX-B9 — remover name-detect de host env/process FFI após RT-L4-ENV/§16

> Família `emit_host_ffi` + correlatos: `var`/`set_var`/`cwd`/`chdir` (env), `args`/`run`/`run_quiet`
> (process), `last_index_of`, `bytes_from_ptr`/`str_from_utf8`/`as_cstr`/`str_from_c` (C-string interop)
> — codegen 3716–3729, emitem `tk_rt_*`. Grupo B. **BLOQUEADO no RT-L4-ENV (env) + §16 process/interop.**

## Goal (design-ahead)

Env (`var`/`set_var`/`cwd`/`chdir`) → RT-L4-ENV (0124, `environ` overlay Teko, zero C-runtime). Process
(`args`/`run`/`run_quiet`) → §16 process (fork/exec/wait syscall). C-string interop
(`bytes_from_ptr`/`str_from_utf8`/`as_cstr`/`str_from_c`) + `last_index_of` → corpo NOVO §16. Quando os
corpos landarem, remover os name-detects + `emit_host_ffi` + espelhos native.

## Where

- `src/codegen/codegen.tks:3716-3729` — `emit_host_ffi` family + `bytes_from_ptr`/`str_from_utf8`/
  `as_cstr`/`str_from_c`.
- `src/lir/lower.tks:2122` (`builtin_env_symbol`), `2115` (`builtin_hostffi_variant_symbol`), `1261`
  (`is_last_index_of_call`), `1262` (`is_str_from_utf8_call`).
- **REPORTE (adjacente):** process (`run`/`run_quiet`) e C-string interop ainda sem crumb §16 dedicado →
  reportar ao dono para o §16 absorver.

## How

1. **Bloqueado:** aguardar RT-L4-ENV (env) + §16 process/interop.
2. Remover por sub-família à medida que cada corpo landa (env primeiro — RT-L4-ENV já planejado).
3. Fixpoint + ASan + reseed por sub-família.

## Rulings & laws

- **Teko-only. D161. RT-L4-ENV (0124) / §16 process. D148.**
- **Achado adjacente REPORTADO, não vira issue** (process/interop sem crumb §16).
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o driver lê env/args, roda subprocessos; corpo é §16).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **RT-L4-ENV (0124) + §16 process/interop** — HARD BLOCK.

## Done when

Zero `emit_host_ffi`/`tk_rt_*` env-process name-detect em codegen/lower e `gen2==gen3`.
</content>
