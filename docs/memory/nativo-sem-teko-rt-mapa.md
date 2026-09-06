# Memória — a rota nativa ainda liga FFI a `teko_rt.{c,h}` (mapa)

Carga `cargo/0.3.1.0-nativo-sem-teko-rt-mapa`, de `origin/fix/union`. Arquiteto: só leu e
escreveu. Plano completo em `docs/design/nativo-sem-teko-rt.md`. Isto é o resumo.

## O que ficou provado

- **A superfície inteira mora em `src/lir/lower.tks`** — 109 símbolos `tk_*` distintos,
  todos literais. `isel_x86_64.tks`/`objfile_coff.tks` só citam `tk_` em doc-comment;
  `isel_arm64`/`objfile_elf`/`objfile_macho` têm **zero**. Logo isel/objfile são
  transporte agnóstico de símbolo; a redução de FFI é confinada a `lower.tks` +
  `teko_rt.tks`.
- **Os gêmeos Teko da lógica pura JÁ EXISTEM** em `src/runtime/teko_rt.tks` (`str_eq:496`,
  `str_ends_with:569`, `str_contains:580`, `str_concat:115`, `u64_to_str:133`, `fmt_*`,
  guardas de pânico…). Estão bypassados: o nome-nu resolve direto para `tk_*` em
  `native_builtin_symbol` (`lower.tks:4235`), nunca entrando nos corpos Teko. O próprio
  cabeçalho de `teko_rt.tks` (~linha 55) nomeia a religação como *"the next step toward
  retiring teko_rt.c"*.

## Taxonomia de três camadas (fronteira = syscall)

1. **Piso syscall/libc (irredutível):** write/fwrite (`teko_rt.c:2311–2345`),
   abort/exit (`2363–2410`), malloc (`1507→1535`), snprintf/strtod (`416`, `2349`), e o
   host surface (io/env/fs/process/time/crypto/names). É a semente C mantida — fica.
2. **Memória crua (bloqueada por 2 lacunas de linguagem):** `tk_region_*`/`tk_arena_*`/
   `tk_slice_*`/`tk_mem_copy` — aritmética de ponteiro pura, Teko-able só quando existir
   `teko::mem::load_u64`/`store_u64` + cast `u64->ptr` (`docs/design/arena-em-teko.md:62,67`).
   Fundo já provado (`examples/probes/arena_bottom`, exit 42).
3. **Lógica pura com gêmeo Teko pronto (candidata imediata):** comparação/hash/format de
   bytes. Religar = mudança de fiação, não reescrita.

## Caminho recomendado

- **Rota (a): religar o nome-nu ao mangle de `teko::runtime::<gêmeo>`**
  (`mangle_fn_symbol`, `lower.tks:885`) para a Camada 3. Máx. de FFI eliminada, mín. de
  código novo, alvo-neutro (corpo Teko baixa uma vez). Move a fronteira FFI para baixo —
  não elimina o piso (`str_concat`→`list::push`→`tk_slice_push`→`malloc`, provado).
- **Rota (b): inline no backend** — só para triviais já-inline (`f64_bits`, precedente
  `lower.tks:4224`) e `tk_mem_copy` como afinação futura. Duplica lógica por-alvo; perde.

## Ordem

- **Degrau 0 (sem arena):** puras-de-bytes — `str_eq/cmp/hash/ends_with/contains`,
  `slice_eq_bytes`, guardas de mensagem. Saem JÁ.
- **Degrau 1 (bloqueado por arena):** todas as alocantes (concat, *_to_str, slice, fmt,
  slice_push, region/arena). Espera as lacunas de `arena-em-teko.md`.
- **Degrau 2 (bloqueado por float-em-Teko):** ftoa/f64_g17/float_parse.
- **Piso mantido:** Camada 1 + host surface. A meta não é "zero `tk_*`"; é piso mínimo
  nomeado de syscall, sem lógica pura acima dele.

## Trava do trabalho (NÃO implementar ainda)

Nada se implementa até `teko` ser 100% nativo. `mapa-native-6-pernas-0.3.1.0.md`: Probe D
(suíte) é **falso-verde** — `teko test .` ignora `TEKO_BACKEND=native`, sempre rota C. O
gate nativo/`tdb` (`docs/design/tdb-proposta-0.3.1.md`) tem de estar de pé e verde antes
de qualquer virada de chave. Este documento é mapa + sequência de crumbs + fixtures,
pronto para retomar em minutos.

## Reportado ao dono (não virou issue nova)

1. `native_builtin_symbol` é a costura única — isel/objfile não precisam mudar.
2. Probe D é falso-verde no nativo — precisa existir antes de C1.
3. As 2 lacunas de arena são o bloqueio real do Degrau 1.
