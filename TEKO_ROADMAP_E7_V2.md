# Teko — Roadmap E7 (v2): etapas subdivididas

> Revisão do roadmap: cada etapa restante quebrada em sub-etapas pequenas (uma função
> ou uma regra + seus testes feliz/barreira, par Teko+C), no padrão que funcionou no P2.
> Esforço: **P** pequeno / **M** médio. Ordem = M.4. `tkb`/emissor só recebem dados
> **validados** (checados).
>
> **Sucessor:** front-end + checker + emissores concluídos; o caminho até o PRIMEIRO
> binário executável (via transpile-para-C) está em **TEKO_ROADMAP_BINARY.md**.

---

## Concluído

- **Materialização da spec → árvore de arquivos** (estrutural, antes de prosseguir): todos os
  blocos cercados de `TEKO_SOURCE.md` + `TEKO_CHECKER.md` foram movidos para arquivos sob `src/`
  (76 arquivos: 42 `.tks`/`.tkt` Teko + 32 `.h`/`.c` mirror C, + `main.tks`/`teko.tkp` na raiz),
  via extrator determinístico (concatena multi-bloco em ordem; resolve "appended to …", continuações
  e o delta C do lexer). `CMakeLists.txt` define a estrutura (C23, `EXCLUDE_FROM_ALL` — mirror C ainda
  não conectado: M.4). **Não compilado, não testado** (a pedido). A MD permanece a spec/histórico
  canônico; `src/` é a árvore de trabalho materializada.

- **F1** — token `to` (lexer). ✓
- **F2** — nós `Cast`/`FieldAccess`/`MethodCall` no `Expr` + precedência (B.23). ✓
- **P1** — `parse_type` (união → primário → slice/nomeado) + `parse_path`. ✓
- **P2** — `parse_expr`, a escada inteira, em **P2a–P2e** (átomos · pós-fixos · unário+cast
  · binários+comparação · lógicos) + nó `PathExpr`. ✓
- **P3** — padrões, em **P3a–P3d** (wildcard+literal · bind/nu · field · range+alt · arms). ✓
- **P4** — statements/blocos, em **P4a–P4e** (bloco+ExprStmt+Return · bindings · destructure+Assign
  · loop/break/continue · if/match-expressões). **Diferimento do P2 removido.** ✓
- Pré-requisitos resolvidos em passagem: tokens `Dot` (`.`) e `Semicolon` (`;`); helpers
  `is_sep`/`skip_seps`; refinos `has_binding`, `Return.has_value`.
- **C1** — `check_*` → `type_*` concluído: AST tipada completa (todo nó/stmt/item
  re-derivado), `revalidate` exaustivo, codec `.tkb` com tags 8/9 reservadas p/ `if`/
  `match` (serialização completa → Fase S). Mirror C + `checker_test.tkt`. ✓
  *Gap registrado (não-C1):* reconciliar o driver do checker (`Program`/`Item`) à AST
  pós-R-main (`MainFile`/`Module`).
- **C2** — cast `to` concluído (**inteiros**) — **regra B (↺ redefinição)**: `type_cast`/`cast_check`
  permitem **toda conversão numérica definida** (incl. narrowing/sinal); a perda é pega, não
  silenciosa — **a validação de possibilidade é runtime**: conversão impossível (o valor não cabe)
  → **PÂNICO** (debug E release, paridade ÷0/OOB; ↺ refinado do "definido-release"); **constante**
  fora-de-faixa → erro de compilação (exceção estática). Guarda runtime em codegen (adiado M.4).
  `bool`/não-numérico → erro. Nó `TCast`, `revalidate`
  re-deriva, `write_texpr` tag 10 (round-trip = S1a). Mirror C + testes. ✓ **Reverte** a regra
  antiga (proibir perda em compilação) — registrado em HISTORY/REBOOT_PLAN + Índice de Redefinições.
  *Adiado por M.4:* casts de **float** (sem float em `PrimKind`) e **newtype↔base** (`resolve_named`
  não rebaixa o newtype ao tipo-base) — linhas float de C2a/C2b pendentes.
- **C3–C6** (lote "agentes rascunham, eu integro" + portão de tensão): **C3** `FieldAccess`
  (`type_field_access`, nó `TFieldAccess`, tag 11→S1b); **C4** regra `mut` (`ValBinding.is_mut` +
  `lookup_binding`, recusa escrita em imutável); **C6** literal anotado **Side D** (leaf as-is i64,
  binding adota via `value_fits` — veredito de tribunal + aval do legislador); **C5** return vs tipo
  **DEFER** (`check_returns`/`check_trailing_value` no `type_function`, inclusão de membro B.14; guard
  contra `loop` divergente; divergência completa = item próprio). ✓ C3/C4 limpos; C5/C6 via tribunal.
- **Dívida `check_*`/`type_*` QUITADA** (a transição que C1 mandou, agora *concluída*): a camada legada
  `check_*` (16 funções, Etapas 4/5a/5b/5c) foi **retirada** (Teko + mirror C) — o checker vivo é
  `type_program`. Mantidos por serem **compartilhados** (não duplicatas): predicados `is_bool`/
  `is_integer`/`is_comparable` + regimes `op_is_*`; helpers de padrão/exaustividade `check_pattern`/
  `exhaustive` (C promovido a `tk_*` não-`static` — conserta mismatch de linkagem pré-existente); typedef
  `tk_check_result`; `tk_env_result` realocado p/ `scope.h`. **Portão de subsunção** (agente) provou
  zero-perda antes da demolição (M.4); leis M.5+M.3+M.1. *Resolve também a transversal F4* (`check_expr`
  legado não-exaustivo sobre `Cast`/`FieldAccess`). Verificado: 90 cercas (par), 0 refs pendentes, 20 `#test`.
- **C7 — checagem de padrões + exaustividade** (lote "agente rascunha, eu integro" + portão de tensão):
  **C7a** `check_pattern` ganha Field (resolve struct → binda campos imutáveis via `field_type`, B.21),
  Range (`lo..=hi` ambos `type_eq` o subject inteiro), Alt (cada opção; bindar em `|` → erro). **C7b**
  exaustividade exclui guardas `when` e **expande** `AltPattern` de casos nus (`RED|GREEN` cobre os dois,
  B.14). **Tensão do eixo do Alt → tribunal → você escolheu B** → **redefinição §VI de A.14** (registrada
  em LEGISLATION/HISTORY Índices): opção de Alt pode ser caso-variante; bindings em Alt proibidos.
  Mirror C (`field_type` promovida a não-`static`) + 6 `#test`. ✓
- **Fase S — codec `.tkb` (round-trip)** (lote "agente rascunha, eu integro"): **S1a** tag `Cast` (10),
  **S1b** tag `FieldAccess` (11) — write/read/`collect_strings` em Teko + mirror C; o tipo do nó já viaja
  por `te.type`, então o payload é só o filho (Cast) ou `receiver`+`field` internado (FieldAccess). **S2**
  satisfeito por construção (serialize consome a AST tipada). Novo `tkb_test.tkt` (`teko::emit`): round-trip
  de Cast/FieldAccess + adulteração→hash FNV-1a (3 `#test`). **Sem tensão.** ✓
  *Diferido (item próprio):* codec de `if`/`match` (tags 8/9) — exige um **serializador de statement**
  (`[]TStatement`) que ainda não existe; read rejeita 8/9 visivelmente (M.1). `MethodCall` sem nó tipado.
- **Fase X — casts honestos** (M.3): **X0** estendeu o cast a `byte ↔ inteiro` (byte AS u8, B.36 — `cast_kind`
  unificou `cast_check`/`const_range_check`, espelho C, +3 `#test`), completando o byte-cast diferido de C2.
  **X1b** varreu 34 casts call-style falsos `u32(…)`/`u64(…)`/`i64(…)`/`byte(…)` nos blocos Teko do codec →
  `x to T` (a forma falsa "aparenta função, não é" = M.3). **X1a** N/A (SOURCE limpo). Descoberta-chave do
  legislador: `u32(…)` não passa nas leis (M.3) — só `x to T` é cast legal. *Fica E7:* ordinal de enum
  (`prim_byte`/`kind_byte`/`kind_of` — funções reais, não casts falsos) e `str↔bytes` (camada de codepage).

---

## P3 — padrões do `match` (depende de P2 p/ guard e body)

| # | Entrega | AST / Lei | Esf. | Testes (feliz / barreira) |
|---|---|---|---|---|
| P3a | `_` (Wildcard) + literal (`LiteralPattern`); `parse_pattern` entry | B.15 | P | `_`, `5`, `"s"` / literal vazio |
| P3b | bind (`Foo as x`) + field (`Foo { a; b }`) — eixo variante | B.15; B.29 | P | `Foo as x`, `Foo { a; b }` / `as` sem nome, `{` não fechado |
| P3c | range (`lo ..= hi`) + alt (`a \| b \| c`) — eixo valor | B.15 | P | `1 ..= 5`, `1 \| 2` / range sem `hi`, alt com `\|` solto |
| P3d | `parse_arm` (`pattern [when guard] => body`) + lista de arms | B.15 | M | arm com/sem `when` / arm sem `=>` |

## P4 — statements + blocos (depende de P2)

| # | Entrega | AST / Lei | Esf. | Testes |
|---|---|---|---|---|
| P4a | `parse_block` (`{ … }`) + `parse_statement` dispatch: `ExprStmt` + `Return` | B.20, B.26 | M | bloco de expr/return / `{` não fechado |
| P4b | bindings `let`/`mut`/`const` (alvo nome + anotação `: T` opcional) | B.13, B.21 | P | `let x = 1`, `mut y: u8 = 2` / bind sem `=` |
| P4c | alvo destructure (`let { x; y } = …`) + `Assign` (`x =`, `x += …`) | B.4, B.13 | P | destructure, `x += 1` / alvo malformado |
| P4d | `loop` + `break` + `continue` | M.5; B.20 | P | corpo de loop, break/continue / `loop` sem `{` |
| P4e | ligar `IfExpr` + `MatchExpr` ao `parse_atom` (usa P4 bloco + P3 arm) | B.15, B.20 | M | `if c { a } else { b }`, `match x { … }` / if sem corpo — **remove o diferimento do P2** |

## R-main — refatorar a modelagem de arquivo (correção: `main.tks` é uma `main` virtual)

> Corrige a nota errada de `fn main()`. Um `.tks` é **ou** um **arquivo-main** (`MainFile` =
> cabeçalho `use` + **corpo de statements** = a main virtual; sem decls de tipo/função) **ou**
> um **módulo** (`Module` = `use` + **declarações** Function/TypeDecl; sem statements soltos).
> `main.tks` é **obrigatório** p/ `.tkp` `executable`, **proibido** p/ `library`. Isto
> **reformula a AST inicial** (`Program = { items: []Item }`, com `Item` incluindo `Statement`)
> e **reformula o P5** (dois pontos de entrada). **Lei: M.2** (a forma diz o que é) **+ M.5**
> (sem `fn main(){}`) **+ B.33**.

| # | Entrega | Esf. | Testes (feliz / barreira) |
|---|---|---|---|
| R-main-a | AST: `MainFile` (uses + `[]Statement`) e `Module` (uses + `[]Decl`, `Decl = Function \| TypeDecl`); aposenta `Item`-com-`Statement` | P | (estrutural) |
| R-main-b | `parse_main_file` (cabeçalho `use` → corpo de statements; **rejeita** `type`/`fn`) | M | `main.tks` com use+statements / `type`/`fn` dentro → erro |
| R-main-c | `parse_module` (use + decls; **rejeita** statement solto) — usa os parsers de decl do P5 | M | módulo com decls / statement solto → erro |
| R-main-d | regra do `.tkp`: `main.tks` obrigatório p/ `executable`, proibido p/ `library` (checagem) | P | exe com main / exe sem main, lib com main → erro |

> **Ordem:** R-main-a/b antes do P5; R-main-c depende de P5a–d (os parsers de declaração);
> R-main-d entra com o checker/build. O **P5 abaixo** vira "os parsers de declaração que o
> `parse_module` consome" (Function/TypeDecl/UseDecl/params); o `parse_program` antigo (P5e)
> é **substituído** pelos dois pontos de entrada de R-main.

## P5 — itens top-level (depende de P1–P4)

| # | Entrega | AST / Lei | Esf. | Testes |
|---|---|---|---|---|
| P5a | `parse_params` + `Function` (`[exp] [doc] fn n(p) -> ret { … }`) | B.21, B.29 | M | fn com/sem exp/doc/params / fn sem `-> ret` |
| P5b | `TypeDecl` struct (`type T = struct { campos }`) | B.13 | P | struct / struct sem `}` |
| P5c | `TypeDecl` enum + variant (`enum { m }`, `variant T = A \| B`) | B.13, B.14 | P | enum, variant / membro malformado |
| P5d | `UseDecl` (`use a::b [as c]`) | B.33 | P | `use a::b`, `use a as b` / use sem caminho |
| P5e | `parse_item` dispatch + `parse_program` (laço top-level) | B.33 | M | programa multi-item / item desconhecido |

## P6 — integração do parser

| # | Entrega | Esf. | Testes |
|---|---|---|---|
| P6 | string → tokens → `Program` ponta-a-ponta | P–M | programa real parseia / malformado falha localizado |

> **Marco:** ao fim de P6, lexer→parser→`Program` roda em fonte real e falha localizado
> no malformado. "Parser sem rachaduras" atingido.

## Fase C — refinamentos do checker (depende de P — precisa de AST real)

| # | Entrega | Lei / B.x | Esf. | Testes |
|---|---|---|---|---|
| C1 ✓ | concluir `check_*` → `type_*` (todo nó/stmt/item re-derivado) | M.3 | M | árvore tipada / nó destipado — **feito** (`TEKO_CHECKER.md` E6-1: `tast`/`typer` + mirror C + `checker_test.tkt`) |
| C2a ✓ | cast `to`: conversões **permitidas** = toda numérica→numérica definida (incl. narrowing/sinal; constante que cabe) | M.0+M.1 | P | **feito (inteiros, regra B)**: `i8 to i32`, `i32 to i8`, `i32 to u32`, `u32 to i8`, `200 to u8` ok (perda = guarda runtime/codegen) · float = *pendente (alpha)* |
| C2b ✓ | cast `to`: **erro** = indefinido (`bool`/não-numérico) + **constante fora-de-faixa** (fail-early) | M.1 | P | **feito**: `bool to i32`, `i32 to bool`, `str to i32`, `300 to i8`, `-1 to u8`, `5000000000 to i32` → erro · `f64 to f32`/`1e20`-runtime = *pendente floats* |
| C3 ✓ | `FieldAccess` (tipo do campo via `TypeDecl`) [`MethodCall` segue diferido] | B.29 | M | **feito**: `s.token` tipa via `type_field_access` (`Named`→`TypeDecl`→`StructBody`→campo); campo inexistente / receiver não-struct → erro · nó `TFieldAccess`, revalidate re-deriva, codec tag 11 → S1b |
| C4 ✓ | regra `mut` (B.21, `is_mut`) — atribuir a imutável → erro | M.1; B.21 | P | **feito**: `ValBinding.is_mut` + `define(…, is_mut)` + `lookup_binding`; `type_assign`/`check_assign` recusam escrita em não-`mut` (params/match/let/const imutáveis). `mut x` reatribui ok / `let x` reatribuído → erro |
| C5 ✓ | `return`/expr-final vs `return_type` declarado | M.3 | P | **feito (DEFER)**: `type_function` faz post-pass `check_returns` (cada `return e` casa, incl. inclusão de membro em variante — B.14) + `check_trailing_value` (valor final só quando o último stmt é expressão — guard p/ não false-rejeitar corpos terminando em `loop` divergente). A análise de divergência ("todo caminho rende valor") é **item próprio** (M.4). |
| C6 ✓ | literal anotado (`let x: u8 = 1` adota a anotação) | M.2 | P | **feito (Side D)**: o literal é gravado AS-IS (leaf i64); o `TBinding.bound` adota T; `value_fits` prova. `: u8 = 1` ok · `: u8 = 999` fora de faixa → erro · `: u8 = <i32 var>` (não-literal) → erro (sem conversão implícita) |
| C7a ✓ | padrões Field/Range/Alt no `match` (checagem) | B.15 | M | **feito**: `check_pattern` checa Field (resolve struct, binda campos via `field_type`, B.21), Range (`lo..=hi` ambos `type_eq` o subject inteiro), Alt (cada opção; **bindar em `\|` → erro** — eixo settled). Campo inexistente/não-struct/range-sobre-bool/binding-em-Alt → erro |
| C7b ✓ | exaustividade no eixo variante (`when` não conta) | M.1; B.15 | M | **feito**: `_`/caso só contam se **não-guardados**; `AltPattern` de casos nus **expande** (`RED\|GREEN` cobre os dois, B.14). `RED\|GREEN`+`BLUE` exaustivo / faltando caso ou guarda no último → erro. **Eixo do Alt ratificado (B) — redefinição §VI de A.14** (tribunal + legislador) |

## Fase S — serializer/deserializer `.tkb` (depende de C)

| # | Entrega | Lei | Esf. | Testes |
|---|---|---|---|---|
| S1a ✓ | tag `Cast` no codec (write/read) | M.1 | P | **feito**: `write_texpr`/`read_texpr` tag 10 — alvo viaja em `te.type` (já serializado), payload = só o filho `expr`; `collect_strings` recursa. Mirror C. round-trip + tamper→hash |
| S1b ✓ | tag `FieldAccess` no codec (`MethodCall` N/A) | M.1 | P | **feito**: tag 11 — `receiver` + `field` internado (`collect_strings` interna o nome, senão `st_find`→sentinela). Mirror C. round-trip + tamper→hash. `MethodCall`: **sem nó tipado** (typing diferido) → nada a serializar |
| S2 ✓ | entrada de serialização aceita **só programa checado** | M.4 | P | **por construção**: `serialize` consome `TExpr` (AST tipada) — só o checker (`type_*`) a produz; não há caminho p/ serializar `Expr` não-checado. Guarda em nível de programa = fase do emissor/pipeline (diferida) |

## Fase X — limpeza cross-cutting (`to` em todo lugar; com F pronto)

| # | Entrega | Lei | Esf. | Testes |
|---|---|---|---|---|
| X0 ✓ | **fundação**: cast `byte ↔ inteiro` (byte AS u8 — B.36) — `cast_kind` unifica `cast_check`/`const_range_check` | M.0+M.2+M.1 | P | **feito**: `byte to u32`/`u8 to byte`/`200 to byte` ok; `300 to byte`/`byte to bool`/`byte to str` → erro; revalidate re-prova. Completa o byte-cast diferido de C2 |
| X1a | trocar casts provisórios `u32(…)`/… por `x to T` em `SOURCE` | M.3 | P | **N/A** — `SOURCE` não tem casts call-style provisórios (só prosa) |
| X1b ✓ | idem em `CHECKER` (`u32(…)`→`x to T` falso = M.3) | M.3 | M | **feito**: 34 sítios nos blocos `.tks` do codec (`emit/`) → `x to T` (parênteses onde a precedência exige); inteiro↔inteiro (C2), byte↔int (X0), i64↔u64 (valores não-negativos do codec). Enum-ordinal (`prim_byte`/`kind_byte`/`kind_of`) ficam (funções E7). C nativo intacto |

## Depois

- **E-emit-a ✓ — driver do `Header` + emissão `.tkh`** (em `src/`, agora canônico): `build_header(prog, table)`
  varre o `TProgram` checado, mantém só `is_exp`, resolve as anotações sintáticas (`resolve_type`) em
  `FnSig`/`TyExport`; `emit_program` = `build_header` → `emit_tkh`. Fundação: `TFunction` ganhou `has_doc`/`doc`
  (perdidos na AST tipada; o `.tkh` preserva docs — M.3; isolado: codec serializa só `TExpr`). **Header sempre
  emitido — sem exports, header vazio (counts=0) que diz "nada exportado", nunca nada** (regra do legislador, M.3).
  Novos `src/emit/header.{tks,h,c}` + `header_test.tkt` (keep-only-exports · round-trip · tamper · **empty-still-emits**).
  CMake atualizado. *Diferido:* **E-emit-b** (round-trip cross-projeto, 2º projeto) e o **driver da pipeline completo**
  (read→lex→parse→check→codegen — codegen não existe, M.4).

---

## Contagem

- **Restante:** P3 (4) · P4 (5) · P5 (5) · P6 (1) · C (9) · S (3) · X (2) = **29
  sub-etapas**, cada uma com par feliz+barreira e mirror C.
- **Caminho crítico (M.4):** P3/P4 → P4e → P5 → P6 → C → S → (emissor). X corre em
  paralelo após F (já pronto).
- **Peso:** maioria **P** agora (uma função/regra cada); os **M** são onde há laço de
  dispatch (P4a, P5a, P5e), checagem de árvore (C1, C3, C7) ou volume (X1b).
- **Regra mantida:** cada sub-etapa entrega bloco Teko `.tks` + mirror C23 + `.tkt`
  feliz/barreira; nada entra no `.tkb`/emissor sem ser **checado**.
