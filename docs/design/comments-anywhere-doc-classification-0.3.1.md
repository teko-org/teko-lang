# Comentários em qualquer posição · classificação de doc-comments — ruling do dono 2026-08-06

status: RULING (a implementar) — mudança de front-end (lexer + parser), sem tocar checker/codegen
origem: fallout do split 0.3.1 (banners `/** */` antes de `use`/entre decls quebravam o parse:
        "expected a declaration; loose statements belong in main.tks")

## A régra (literal do dono)

> "aceitar comentários (inclusive `/** */`) em qualquer posição, doc-comments devem ser
> classificados quando estiverem juntos (sem quebra adicional) a assinaturas de constantes, tipos
> e funções (antes ou depois de atributos `#…`). Creio que é uma mudança apenas no front, lexer e
> parser para isso."

## O que muda

1. **Comentário em QUALQUER posição é aceito** — `//`, `/* */` e `/** */` deixam de ser um erro
   de parse quando não estão anexados a uma declaração. Um `/** */` solto (banner de topo de
   arquivo, entre decls, antes de `use`, etc.) é apenas um comentário, descartado — NUNCA
   "expected a declaration".

2. **Classificação de doc-comment é POSICIONAL, não sintática.** Um comentário (incluindo `/** */`)
   só é CLASSIFICADO como doc-comment (documentação anexada, indexada) quando está **adjacente —
   sem quebra adicional (linha em branco)** — à **assinatura** de uma `const`, `type` ou `fn`,
   **antes OU depois** dos atributos `#…` da declaração. Fora dessa adjacência, é comentário comum.

   - adjacência = imediatamente contíguo (sem linha em branco entre o comentário e a assinatura/os
     atributos). Uma quebra em branco desclassifica: vira comentário solto, aceito e ignorado.
   - "antes ou depois de atributos `#…`": tanto `/** doc */ #attr fn f()` quanto
     `#attr /** doc */ fn f()` classificam o doc para `f`.

## Escopo

- **Front-end apenas**: lexer (tokenizar `/** */` em qualquer posição sem erro) + parser
  (aceitar comentários soltos como trivia; classificar como doc só na adjacência a
  const/type/fn ± atributos).
- Não toca checker/codegen/vm. Comportamento emitido inalterado (comentários não geram código).

## Por que importa

Teria evitado a quebra em massa do split 0.3.1 (banners `/** */` de arquivo + doc-comments
cortados no corte de arquivo). Torna o split/reorganização de arquivos robusto: um banner de
topo pode ser `/** */` sem quebrar o parse.

## Fixtures esperadas (regressão)

- `/** solto */` no topo do arquivo (antes de qualquer decl/`use`) → aceito, ignorado.
- `/** */` seguido de LINHA EM BRANCO e depois `fn f()` → comentário solto, `f` sem doc.
- `/** doc */ fn f()` e `/** doc */ #attr fn f()` e `#attr /** doc */ fn f()` → doc classificado para `f`.
- `/** */` entre duas decls (sem adjacência) → aceito, ignorado.
