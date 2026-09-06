# Estado da fase Doc-2 + campanha de limpeza — recuperação pós-restart

Registro durável (dono pediu 2026-08-18) para o container efêmero: só o que está em `origin`
sobrevive a restart/compactação. Este doc é o ponto de retomada.

## Doc-2 e Doc-1 (escopo, rulings do dono)
- **Doc-2 prepara TODO o terreno.** Antes do Doc-1 entra: stdlib §1.5 inteira (incl. genéricos
  collections/`sort<T:Ord>`) + keystone genéricos (#254) + §10 concorrência (100%) + §11 + §17 +
  §16 (trocar TODAS as deps C — `teko_rt.c/.h`+`win32_compat`, inclusive a arena — por Teko+FFI-do-SO,
  SEM EXCEÇÕES) + FFI-stdlib (rand/openssl/gpg/net/db/rpc) + ui. Garantir **ambas as pernas (C e
  native) compilando é o trabalho da Doc-2.**
- **Doc-1 só MELHORA a arena** (fase terminal). NÃO despachar; ao chegar no limiar (tudo acima feito,
  duas pernas compilando, só faltando melhorar a arena), AVISAR o dono com resumo completo.

## Leis vigentes (detalhe em CLAUDE.md "Estilo de código" + "Leis de desenvolvimento")
- **Provenance REVOGADA/desabilitada** (letra morta). Reseed = harvest local (fixpoint gen2==gen3),
  sem gate de provenance. Seed do bootstrap aceito à força. Se seed falhar: **CI falha imediato, sem
  fallback** pra versão antiga.
- **NÃO reseedar no meio: limpeza primeiro, reseed só no fim, tudo junto.**
- **Visibilidade:** stdlib default `exp` (superfície consumível); só helper interno fica `pub`.
  Maquinaria do compilador (parser/checker/codegen/lir/backend/build/lexer/names) = `pub`/privado,
  MAS tipos/helpers que macro/comptime alcançam em compile-time = `exp` (ABI curada; hoje só
  `parser::Visibility`). TESTE de `exp` = valor pro usuário ("faz sentido expor? um usuário chamaria?").
- **Doc só em `exp`** (qualquer outro acessor descarta); `//` = 0%; doc curto/factual sem refs.
- **Mensagens:** `arquivo:linha:coluna: "causa clara"`; "encurtar" = MELHORAR a frase (não truncar).
  **Passe de mensagens unificado (compiler-core) = etapa dedicada OBRIGATÓRIA antes do reseed.**
- **Testes (frontend D):** remover tautológicos + comportamento que `src/` exercita + genéricos +
  native; MANTER só erro/diagnóstico + função nunca-chamada.
- **Manter o mecanismo span (B/DocSpan)** — salvaguarda contra doc-bloat futuro (mesmo com ganho atual
  de 0,2%: memória não sofre se doc voltar a crescer).

## Estado atual (branches/commits — TUDO no origin salvo onde marcado)
- **`origin/fix/retirement`** = base da fase. Carrega: fonte do C (#os cross-emit, commits
  `fa3b094c..a22445dc`) + leis + mapa exp (`docs/design/mapa-superficie-exp-0.3.1.md`) + gitignore
  scratchpad + este doc.
- **C (#os cross-emit)** — CI fix, JÁ drenado em fix/retirement. Faz `#os` diferir ao `#if` do C
  (Linux+macOS+Windows num só teko.c). Fixpoint gen2==gen3 sha `8b1368c3` (artefato em worktree do C).
  Reseed pendente pra onda final.
- **B (span-ref/DocSpan)** — branch `feat/issue-docspan-span-doc` @ `b3f5a8ff`. MANTER, integrar na
  onda final. `DocSpan{file_id,byte_offset,byte_len}`+`.read()`; `.tkh` materializa, `.tkb` sem doc.
- **Lotes de limpeza** (dirs disjuntos, sem build, behavior-preserving, visibilidade per mapa + doc +
  `//` + mensagens):
  - `style/clean-frontend` @ `02ee6d10` — build+parser+lexer+names+emit — DONE (msgs adiadas: passe unificado)
  - `style/clean-outside-src` @ `3736c215` — tudo fora de src/ — DONE (preservou debugger-poc/*.tks por DWARF hardcoded)
  - `style/clean-codegen` @ `febff934` — codegen+lir+backend — DONE (35 msgs reescritas)
  - `style/clean-collections` @ `10d1e565` — collections+sort+list+iter+cmp+math — DONE (90 pub→exp; sort<T:IOrd>→exp)
  - `style/clean-checker`, `style/clean-crypto-enc`, `style/clean-rest-src` — RODANDO
- **Mapas (design docs):** exp-surface (drenado) + remoção-de-testes (`design/mapa-remocao-testes` @
  `00b74b8b`, `docs/design/mapa-remocao-testes-0.3.1.md`).

## Sequência restante (ordem obrigatória)
1. Aguardar os 3 lotes restantes.
2. Integrar os 7 lotes (dirs disjuntos → sem conflito) em fix/retirement.
3. **Passe de mensagens unificado** (compiler-core `arquivo:linha:coluna: "causa clara"`).
4. Integrar **B** (docspan).
5. **Frontend D:** remover só os **102 confiantes** (82 `.tkt` + 20 dirs `.tkr`; fora do `teko.tkp`,
   não-protegidos, lei-claros). NÃO tocar ambíguos nem fixtures protegidas pelo gate.
6. **Reseed único** (harvest local, sem provenance) + desabilitar provenance/fallback no CI (falha-fecha).
7. Push → **assinar #110** → observar CI. Triagem: `fixpoint (native)` = ESPERADA; `produce this leg`
   (C ou native) = FALHA REAL (`head=100` pra achar a causa).
8. CI verde nas pernas → **continuar Doc-2**: 3des → genéricos (crumbs desenhados: 9-ops →
   generic-stack-completion → collections-generics-fase1b) → §10 → §11 → §17 → §16 → FFI-stdlib → ui.

## Decisões PENDENTES do dono (NÃO decidir sozinho)
- **Visibilidade ambígua** (deixadas `pub`, sem forçar): `math/checked` (160 decls), `list::grow`,
  `coverage`, `time::CivilDate`, `fmt::format_source`.
- **Frontend D:** tensão de lei (fixtures no gate `teko.tkp [tests] regression` que a lei removeria por
  conteúdo — `generic_union_arg`, `comptime_*`, `macro_*` — mas estão PROTEGIDAS; purga = 2º passe com
  re-medição do lane fail-closed); crypto/regex/compress "nunca-chamado" (lei diz MANTER, custo diz
  remover); achado: `examples/regressions/mem/mem.tkr` está no gate mas o dir NÃO existe (M.3).

## Achados-irmãos abertos (para quando os genéricos entrarem — Doc-2)
Mesmo hazard de nome bare do #4 (colisão de tag de união cross-namespace, já resolvido `e98fd5b0`):
`Base__g__<arg>` (instância genérica) e `TK_E_<E>_<M>` (constante de enum) — precisam de fix+reseed próprios.

## Traps conhecidos (Doc-2 impl)
cast neg i64↔u64 SIGABRT; dois loops sequenciais com i64 mutado; keywords `pub`/`base`/`class` como
param; `bigint::of(i64)` sign-flip; widening união 2→3 no return (destructure+re-return); bitwise em
byte bare (cast to u64); backend native com bug pré-existente `const struct __hdr` (usar sempre
`TEKO_BACKEND=c`). Validação local = COMPILAÇÃO (`--no-verify --release`, `TEKO_BACKEND=c`,
`ulimit -v 6291456`) + fixpoint. **NUNCA `teko test .` local (OOM).** Testes SÓ no CI.
