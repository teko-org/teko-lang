# Teko — Roadmap E7 (v2): etapas subdivididas

> Revisão do roadmap: cada etapa restante quebrada em sub-etapas pequenas (uma função
> ou uma regra + seus testes feliz/barreira, par Teko+C), no padrão que funcionou no P2.
> Esforço: **P** pequeno / **M** médio. Ordem = M.4. `tkb`/emissor só recebem dados
> **validados** (checados).

---

## Concluído

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
| C1 | concluir `check_*` → `type_*` (todo nó/stmt/item re-derivado) | M.3 | M | árvore tipada / nó destipado |
| C2a | cast `to`: conversões **permitidas** (widening, exato, float→int trunca) | M.0+M.1 | P | `i8 to i32`, `3.7 to i32` ok |
| C2b | cast `to`: conversões **proibidas** (narrowing/perda) → erro | M.1 | P | `i32 to i8`, `f64 to f32`, `1e20 to i32` → erro |
| C3 | `FieldAccess` (tipo do campo via `TypeDecl`) [`MethodCall` segue diferido] | B.29 | M | `s.token` tipa / campo inexistente → erro |
| C4 | regra `mut` (B.21, `is_mut`) — atribuir a imutável → erro | M.1; B.21 | P | `mut x` reatribui / `let x` reatribuído → erro |
| C5 | `return`/expr-final vs `return_type` declarado | M.3 | P | tipos batem / retorno divergente → erro |
| C6 | literal anotado (`let x: u8 = 1` adota a anotação) | M.2 | P | `: u8 = 1` ok / `: u8 = 999` fora de faixa → erro |
| C7a | padrões Field/Range/Alt no `match` (checagem) | B.15 | M | match com cada padrão / padrão mal-tipado → erro |
| C7b | exaustividade no eixo variante (`when` não conta) | M.1; B.15 | M | match exaustivo / faltando caso → erro |

## Fase S — serializer/deserializer `.tkb` (depende de C)

| # | Entrega | Lei | Esf. | Testes |
|---|---|---|---|---|
| S1a | tag `Cast` no codec (write/read) | M.1 | P | round-trip de `Cast` / bytes adulterados → hash |
| S1b | tags `FieldAccess`/`MethodCall` no codec | M.1 | P | round-trip dos nós / idem |
| S2 | entrada de serialização aceita **só programa checado** | M.4 | P | checado serializa / não-checado recusado |

## Fase X — limpeza cross-cutting (`to` em todo lugar; com F pronto)

| # | Entrega | Lei | Esf. | Testes |
|---|---|---|---|---|
| X1a | trocar casts provisórios `u32(…)`/… por `x to T` em `SOURCE` | M.3 | P | recompila/round-trip verde |
| X1b | idem em `CHECKER` (todos os `[cast TBD → E7]`) | M.3 | M | idem |

## Depois (novo arquivo MD)

- **Emissor `.tkh` + driver da pipeline** — constrói o `Header` do programa **checado**
  (filtra `is_exp`), chama `emit_tkh`. Recebe dados validados (pré-req S2). *(M.4 — só
  após C e S.)* Subdivisível na hora: E-emit-a (driver/Header), E-emit-b (round-trip
  cross-projeto com o 2º projeto).

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
