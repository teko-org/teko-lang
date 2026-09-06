---
seq: 0193
crumb-id: EX-A0
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:0"      # §0 método + §5 Risco-1
  - "DECISION_LOG.md:1192"                                    # D161
---

# 0193 · EX-A0 — resolver nomes-nus do prelúdio a `teko::runtime::*` (fundação, byte-idêntico)

> De-risca o Risco-1 do expurgo A EM ISOLAMENTO: o checker passa a RESOLVER os nomes-nus de runtime
> (`print`, `panic`, `str_hash`, …) à superfície `teko::runtime::X` do prelúdio, setando
> `c.call_ns="teko::runtime"`. O name-detect do codegen AINDA vence (curto-circuita antes) → emissão
> **byte-idêntica**. Prova que a religação de resolução é segura, sem ainda remover nenhum desvio.

## Goal

Hoje `builtin_fn(name)` (`src/checker/scope.tks:471`) tipa esses nomes com uma assinatura sintética e
`call_ns` fica `""` → a emissão genérica não tem símbolo → o codegen name-detecta. Este crumb faz a
resolução do nome-nu ENCONTRAR a fn real do prelúdio injetado (`teko::runtime::X`), preenchendo
`call_ns`, SEM tocar o codegen/lower. Como o `has_builtin` (codegen 3870) e o `native_builtin_symbol`
(lower) ainda curto-circuitam ANTES do ramo genérico, o `teko.c` emitido não muda. Byte-mover: **não**
(byte-preserving). Dirige o reseed só porque toca o checker (a tabela de tipos pode mudar); o CODE
emitido é idêntico — fixpoint gen2==gen3 é a prova.

## Where

- `src/checker/scope.tks:471` — `builtin_fn(name)` — **NÃO remover ainda**; garantir que os nomes-nus de
  runtime também sejam alcançáveis pela resolução normal ao prelúdio (open-implícito do namespace
  `teko::runtime` no escopo nu, à la `use teko::runtime::*`), de modo que a resolução preencha `call_ns`.
- `src/checker/resolve.tks` / `src/checker/nidx.tks` — ponto de resolução de call: quando o nome-nu bate
  numa `exp fn` do prelúdio, setar `call_ns="teko::runtime"` no `TCall` (hoje fica `""`).
- `src/checker/typer.tks:2049` — `match builtin_fn(name)` — o caminho que hoje só acha via shim; passa a
  achar via prelúdio primeiro, shim como fallback (mantido este crumb).
- NENHUMA mudança em `src/codegen/codegen.tks` nem `src/lir/lower.tks` neste crumb.

## How

1. Localizar a injeção do prelúdio de runtime (`inject_runtime_prelude`) e confirmar que as `exp fn` de
   `src/runtime/*.tks` entram no `prog` compilado do usuário (elas já entram — são o prelúdio).
2. Fazer a resolução de call de nome-nu consultar o namespace `teko::runtime` do prelúdio ANTES (ou
   junto) do shim `builtin_fn`: se `teko::runtime::<nome>` existe como `exp fn`, resolver a ela e setar
   `call_ns="teko::runtime"`. Preservar precedência: um símbolo do USUÁRIO com o mesmo nome vence
   (provenance D133 barra redefinição de reservados, mas nomes como `print` não são reservados-por-keyword
   → um `fn print` do usuário deve sombrear; manter a regra de escopo existente).
3. NÃO remover entradas de `builtin_fn`; NÃO tocar codegen/lower. O objetivo é SÓ preencher `call_ns`.
4. Provar byte-identidade: com `call_ns` preenchido e o name-detect ainda vencendo, `gen2.c==gen3.c`.

## Rulings & laws

- **Teko-only.** C twins congelados (exceto runtime).
- **D161 (fundação):** este crumb é o pré-requisito de fluxo-genérico; não remove desvio ainda.
- **D133 provenance:** open-implícito do prelúdio NÃO permite usuário redefinir reservado-por-keyword;
  nomes de runtime não-reservados seguem sombráveis por escopo (regra existente).
- **Fork protocol (dono 2026-08-19):** o mecanismo (open-implícito vs resolução direta) é escolha de
  implementação sob D161, não fork; se colidir com provenance, aplicar D133, não parar.
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3` byte-idêntico
  + **build ASan+UBSan limpo (D166)**; ratchet D68 pico não-cresce; reseed ao fim (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (todo o compiler-core chama esses nomes-nus; o
self-build os exercita; a byte-identidade é a prova).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico (emissão INALTERADA) + ASan+UBSan limpo. "Green"
= `call_ns` preenchido para os nomes de runtime E `teko.c` byte-idêntico ao HEAD. Reseed-class:
`fixpoint-rebuild` (toca checker; emissão idêntica → reseed é rebuild, não teaching).

## Deps

`—`

## Done when

Os `TCall` de nomes-nus de runtime carregam `call_ns=="teko::runtime"` e `gen2==gen3` byte-idêntico ao
HEAD (nenhuma mudança de emissão).
</content>
