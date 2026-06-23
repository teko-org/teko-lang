# Teko — Auditoria do parser + roadmap de execução (rumo a E7 `to` e ao emissor)

> Levantamento pedido: o parser está fundamentado e sem rachaduras (constitucional)?
> O que falta, o que isso demanda no checker e no serializer, e em quantas etapas /
> com que esforço se executa — com `tkb` e emissor recebendo **dados validados**, e
> **testes (caminho feliz + barreira)** em cada camada. Saída em arquivos.

---

## 1. Estado atual (fatos)

- **Lexer** — completo (scanner inteiro, `teko::lexer`), com 8 `#test` (texto+lexer).
- **Parser** — **só a Parte 1: AST + plumbing.** Existem: a AST inteira (`type.tks`,
  `ast.tks`, `pattern.tks`), a família `Parsed*` e os helpers (`cursor.tks`,
  `optokens.tks` — um predicado por nível de precedência, B.23). **Não existe
  nenhuma função de parse (token→AST).** O próprio doc diz "as funções de parse vêm
  nas Partes 2–4". Essas partes **não foram escritas**.
- **Checker** — E6 fechado (resolução de tipos, `type_eq`, ambiente, re-derivação),
  porém com **dezenas de sítios de cast provisórios** (`u32(…)`, `u64(…)`, `byte(…)`,
  `i64(…)`, `lo8`, `prim_byte`, `kind_byte`) marcados `[cast TBD → E7]`.
- **Serializer/deserializer (`.tkb`)** — round-trip fechado (frame magic+versão+hash,
  string table, `write_type`/`read_type`), **também salpicado dos mesmos casts
  provisórios**.
- **Testes** — `SOURCE`: 8 `#test`. **`CHECKER`: 0 `#test`.** Não há **nenhum** teste
  de parser nem de checker; nenhum `.tkt` de round-trip do `.tkb`.

## 2. As rachaduras (o que trava prosseguir)

Em ordem de gravidade — todas violam M.4 (não construir sobre o incompleto) se
ignoradas:

1. **O parser não produz nada.** Sem as Partes 2–4, lexer→parser→checker→`.tkb`
   nunca roda fim-a-fim. O checker (E6) foi escrito contra tipos de AST que **nada
   ainda gera** — está correto no papel, **inexercitado** na prática. *(M.4.)*
2. **A AST não sabe dizer `x.campo`.** Não há nó `FieldAccess` no `Expr`. O **próprio
   checker usa** `.campo` no seu código (`s.token`, `b.left`) — a linguagem em que ele
   é escrito (Teko) não tem o nó que ele usa. Inconsistência estrutural; "gap real"
   já anotado no checker. *(M.3 honestidade da AST; B.29.)*
3. **A AST não sabe dizer o cast.** Não há nó `Cast`; todo o código usa a forma
   provisória `u32(…)` — exatamente a forma `T(x)` **rejeitada por M.3** (tipo se
   passando por função). O `to` decidido **não existe** no lexer/AST/parser/checker/
   serializer. *(M.2/M.3; decisão `to`.)*
4. **Zero testes de parser e de checker.** As duas camadas que mais importam não têm
   **nem caminho feliz nem barreira**. *(M.1 — a barreira é a prova da segurança.)*
5. **Chamada de método `.` (B.29) não tem nó.** Métodos são constitucionalmente
   disponíveis (1º arg `self` solto), mas **inparseáveis** — sem `MethodCall`. *(B.29;
   a *checagem* de método segue diferida, mas o nó é gap de parser.)*
6. **`.tkb` recebe dados com operadores TBD.** Os casts pendentes atravessam o
   serializer — o dado que entra no objeto usa operador não-finalizado. *(M.3.)*

## 3. Decisões que o levantamento fixa antes de codar

- **`to` entra na fundação, não como retrofit.** Como o parser será escrito agora e o
  cast é cross-cutting (E7), o nó `Cast` + o token `to` + sua precedência entram
  **antes** das Partes 2–4, que já nascem parseando `to`. Evita escrever o parser e
  remendar depois. *(M.5 — não fazer duas vezes.)*
- **Precedência do `to` (B.23).** `.`/chamada/`::` = mais alta (pós-fixo primário);
  unário (`- ~ !`) acima do `to`; `to` logo abaixo do unário e **acima** da escada
  binária (`x to u32 * 2` ⇒ `(x to u32) * 2`; `-x to u32` ⇒ `(-x) to u32`). A definir
  no detalhe em **F2/P2**.
- **Semântica do cast já está no histórico** (preserva→permite / perde→erro:
  widening, `f32→f64`, int→float exato, float→int trunca-pra-zero; **erro** em
  narrowing `i32 to i8`, `i64 to f32`, `f64 to f32`, float-grande→int). O checker
  **implementa essa tabela** — não reinventa. *(M.0+M.1.)*
- **`.tkb` e emissor só recebem programa CHECADO.** A entrada de serialização aceita
  apenas a saída do checker (programa tipado e validado). *(M.4.)*
- **Toda etapa entrega par feliz+barreira** em `.tkt` (e o mirror C quando couber). A
  barreira de cada regra proibida é um teste que **deve falhar na compilação** do
  fonte de teste. *(M.1; regra do dual-deliverable.)*

## 4. Sequência de execução (etapas, dependências, esforço, testes)

Esforço relativo: **P**(pequeno) / **M**(médio) / **G**(grande). Ordem = M.4.

### Fase F — Fundação (AST + lexer) — *destrava tudo*
| # | Entrega | Lei/B.x | Esforço | Testes (feliz / barreira) |
|---|---|---|---|---|
| F1 | Lexer: token `to` (word-operator, irmão de `as`/`in`); `to` como ident → erro | M.2; decisão `to` | **P** | `x to u32` tokeniza / `to` usado como nome → erro |
| F2 | AST: nó `Cast {expr; target}` no `Expr`; nó `FieldAccess {receiver; field}`; nó `MethodCall {receiver; method; args}`; precedência do `to` (B.23) | M.3; B.29 | **P–M** | (estrutural — exercitado em P2) |

> Ripple de F2: novas cases no checker (Fase C) e novas tags no serializer (Fase S).

### Fase P — Parser Partes 2–4 (o grosso que falta) — *depende de F*
| # | Entrega | Lei/B.x | Esforço | Testes (feliz / barreira) |
|---|---|---|---|---|
| P1 | `parse_type` (Named/Slice/Union, recursivo) | B.8, B.14 | **M** | `[]u64`, `A \| B` / `[]` sem elemento → erro |
| P2 | `parse_expr` — escada inteira: primário, unário, níveis binários, `Compare`, **`Cast` (`to`)**, **`FieldAccess`/`MethodCall`**, `Call`, `IfExpr`, `MatchExpr` | B.23, B.15, B.20 | **G** | cada nível / operador órfão, paren não fechado → erro |
| P3 | `parse_pattern` (Literal/Range/Alt/Bind/Field/Wildcard) + `parse_arm` + guard `when` | B.15 | **M** | cada padrão / arm sem `=>`, range invertido → erro |
| P4 | `parse_statement` (Binding let/mut/const + target/destructure + anotação; Assign; Return; Loop; Break/Continue; ExprStmt) + `parse_block` | B.4, B.13, B.20, B.21 | **M** | cada statement / `break` fora de loop reservado p/ checker; bind sem `=` → erro |
| P5 | `parse_item` (Function c/ `exp`+doc; TypeDecl struct/enum/variant c/ `exp`+doc; UseDecl; Statement top-level) + `parse_program` + `parse_params` | B.13, B.29, B.33 | **M** | cada item / `fn` sem `-> ret`, struct sem `}` → erro |
| P6 | Integração: string → tokens → `Program` (fim-a-fim) | — | **P–M** | programa real parseia / fonte malformado → erro localizado |

### Fase C — Refinamentos do checker — *depende de P (precisa de AST real)*
| # | Entrega | Lei/B.x | Esforço | Testes (feliz / barreira) |
|---|---|---|---|---|
| C1 | Concluir `check_*` → `type_*` (todo nó/stmt/item re-derivado) | M.3 | **M** | árvore tipada / nó destipado → erro |
| C2 | **Tipagem do `Cast` (`to`)** — tabela preserva/perde do histórico | M.0+M.1 | **M** | `i8 to i32` ok, `3.7 to i32` ok / `i32 to i8`, `f64 to f32`, `1e20 to i32` → erro |
| C3 | `FieldAccess`/`MethodCall` — tipo do campo via `TypeDecl`; método se em escopo (método-self segue diferido) | B.29 | **M** | `s.token` tipa / campo inexistente → erro |
| C4 | Regra `mut` (B.21) — `is_mut`; atribuir a imutável → erro | M.1; B.21 | **P–M** | `mut x` reatribui / `let x` reatribuído → erro |
| C5 | `return`/expr-final vs. `return_type` declarado | M.3 | **P–M** | tipos batem / retorno divergente → erro |
| C6 | Literal anotado (`let x: u8 = 1`) — literal adota a anotação | M.2 | **P–M** | `: u8 = 1` ok / `: u8 = 999` fora de faixa → erro |
| C7 | Padrões Field/Range/Alt no `match` + exaustividade no eixo variante | M.1; B.15 | **M** | match exaustivo / faltando caso, `when` não conta → erro |

### Fase S — Alinhamento do serializer/deserializer (`.tkb`) — *depende de C*
| # | Entrega | Lei/B.x | Esforço | Testes (feliz / barreira) |
|---|---|---|---|---|
| S1 | Tags de `Cast`/`FieldAccess`/`MethodCall` no codec; round-trip dos novos nós | M.1 | **M** | round-trip exato dos nós novos / bytes adulterados → hash mismatch |
| S2 | Entrada de serialização aceita **só programa checado** (validado) | M.4 | **P** | programa checado serializa / programa não-checado recusado |

### Fase X — Limpeza cross-cutting (`to` em todo lugar) — *com F1/F2 prontos*
| # | Entrega | Lei/B.x | Esforço | Testes |
|---|---|---|---|---|
| X1 | Trocar **todos** os `u32(…)`/`u64(…)`/`byte(…)`/`i64(…)`/`lo8`/`prim_byte`/`kind_byte` (`[cast TBD → E7]`) por `x to T` em `SOURCE`+`CHECKER` | M.3 | **M** | recompila/round-trip seguem verdes |

### Depois (novo arquivo MD, fora deste roadmap)
- **Emissor `.tkh` + driver da pipeline** — constrói o `Header` do programa **checado**
  (filtra `is_exp`) e chama `emit_tkh`. Recebe **dados validados** (pré-requisito S2).
  *(M.4 — só após C e S fecharem.)*

## 5. Esforço total e contagem

- **Fases:** 5 de execução (F, P, C, S, X) + 1 posterior (emissor).
- **Sub-etapas:** **18** (F:2, P:6, C:7, S:2, X:1) — cada uma com par feliz+barreira.
- **Peso:** o **grosso é a Fase P** (parser, esp. **P2** = G — é a camada inteira que
  falta). C e S são **M** cada; F e X são leves. O custo escondido real é **escrever
  os testes de barreira** (≈18 fontes de teste que devem falhar na compilação).
- **Caminho crítico (M.4):** F → P → C → S → (emissor). X corre em paralelo após F.
- **Critério de "sem rachaduras":** ao fim de P6, lexer→parser→`Program` roda em
  fonte real **e** falha localizado em malformado; ao fim de C7+S2, o que entra no
  `.tkb`/emissor é **tipado e validado**, com barreira testada em cada regra da
  legislação/histórico.

## 6. Organização dos arquivos de saída (proposta)

- `SOURCE` recebe **F1, F2, Fase P inteira, X1** (lexer/AST/parser são fonte).
- `CHECKER` recebe **Fase C, Fase S, X1** (checagem + serialização).
- **Novo MD** recebe o **emissor** (como você pediu), ao final.
- Cada etapa: bloco Teko `.tks` **+** mirror C23, e `.tkt` **+** mirror `.tkc` de teste
  (feliz+barreira), conforme a regra de dual-deliverable.

> Próximo passo sugerido: começar pela **Fase F** (destrava o resto e é leve), e em
> seguida atacar **P1→P6**. Confirma o sequenciamento e eu inicio a execução nos
> arquivos.
