# TEKO_CORRECTION_PLAN — contenção + correção doutrinária (void / error / variant / nullable)

> **URGENTE / transversal.** Construções que saíram do prumo entraram sem julgamento e/ou por deriva
> entre documentos congelados. Este plano CONTÉM (para de sangrar) e CORRIGE (excisa), paralelizando ao
> máximo. **Nada de execução de código até ratificação.**

## As 4 regras (a lei, ratificada pelo legislador)

1. **`void` = "não retorna valor".** Uma função **ou** tem um tipo de retorno **ou** `-> void`. `void`
   **NÃO é um tipo**: nunca é valor, nunca é membro de variant, nunca tipo de binding. *(HISTORY §5829–5833,
   §5971: todo fn escreve `-> Type` ou `-> void`.)* **Supera `Unit`** (o struct-vazio) — `Unit` deixa de existir.
2. **Variant = soma de tipos COMPLETOS.** Cada membro é um tipo real: **nativo, enum, struct, ou outra
   variant**. **Sem construtores** (a forma Rust `Caso(payload)` é proibida — "o Rust faz sombra"). **Sem
   `void`. Sem membros anuláveis.** *(LEGISLATION §109; `type.tks` `Variant = members:[]Type`.)*
3. **Nulabilidade = sufixo `?` no tipo inteiro.** `T?` (tipo), `?.` (acesso seguro), `??` (Elvis). **Variant
   não contém anuláveis**: `Value? | i32` é inválido → o dev cria `type Val = Value | i32` e marca no retorno
   `-> Val?`. Domínios disjuntos: null→`?.`/`??`; erro→`match`. *(REBOOT_PLAN §202–203; LEGISLATION §75.)*
4. **`error` é tipo NATIVO (minúsculo)** — supera `Error`/`teko::Error`/`Valor | Error`. *(já em `src/core.tks`.)*

**Consequência composta:** uma função falível **sem valor** NÃO é `void | error` (inválido) — é
**`-> error?`** (null = ok, `error` = falha). Uma função falível **com valor** é `T | error`. Um check que
"dá certo ou erra" → `-> error?` (o C `tk_check_result {ok, error}` JÁ é exatamente um `error?`).

---

## §1 — Revisão da legislação (pontos defasados ↔ corretivos)

| Defasado (corrigir) | Onde | Vira |
|---|---|---|
| `Error` / `Valor \| Error` / `Error as e` | LEGISLATION §49, §66–90 | `error` (nativo) / `valor \| error` |
| `str \| Error`, `[]byte \| Error` | LEGISLATION §121, §135 | `str \| error`, `[]byte \| error` |
| `teko::Error` | LEGISLATION §176 | `teko::error` |
| `write_file(...) -> () \| Error` (void-em-variant) | LEGISLATION §270–271 | `write_file(...) -> error?`; `read_file -> []byte \| error`; `write_err([]byte) -> void` |
| `type Unit = struct {}` ; `Type = … \| Unit` ; `Unit {}` ; `-> Unit` | **TEKO_CHECKER.md:78,81,90,1095,1245–1278** (deriva congelada) | excisar; `void` como marcador de retorno; checks → `error?` |

**Corretivos (a lei que supera, já no cânone):** `void` (HISTORY §5829–5833/§5971) · `?`-nulabilidade
(REBOOT_PLAN §202–203) · `error` nativo (`core.tks`) · variant=tipos-declarados (LEGISLATION §109).

---

## §2 — Contenção (imediata, serial — PARA de sangrar)

- **[Z0] Emenda legislativa** *(serial, primeiro — vira o guard que as correções citam)*. par: `TEKO_LEGISLATION.md` + `TEKO_HISTORY.md`
  > Gravar as 4 regras como decisão (B.x): void (marcador de retorno, nunca tipo/valor/membro), variant
  > (tipos completos, sem construtores/void/anuláveis), nullable (`T?`, nunca membro de variant), `error`
  > nativo supera `Error`. Marcar os pontos defasados de §1 como ↺ corrigidos. Anotar a deriva da
  > `TEKO_CHECKER.md` (a fonte do `Unit`). **HALT/guard:** nenhum código novo pode introduzir `Unit`,
  > `variant{ Caso(...) }`, `void`/anulável dentro de variant, ou `Error` (cap.).

> **Contenção do que já existe:** o `Value` variant do VM e o tipo `Unit` ficam marcados como **NÃO-CONFORMES**;
> não commitar mais nada que dependa deles até a excisão (§3). O commit pendente deve registrar a dívida.

---

## §3 — Correção (excisão) — paralelizada por dependência e grupo-de-arquivos

**Linchpin (serial):**
- **[Z1] Modelo de tipos** — deps: Z0 · par: `src/checker/type.{tks,h,c}`
  > Excisar `Unit` do enum `Type`; introduzir `void` **só** como `Func.ret` (proibido como membro de variant,
  > valor, ou tipo de binding — checagem por construção). Renomear `Error`→`error` no modelo. `type_eq`/etc.
  > ajustados. É a base de tudo abaixo.

**Paralelos (após Z1 — grupos de arquivos disjuntos):**
- **[Z2a] Checker** — deps: Z1 · par: `src/checker/{typer,expr,tast,revalidate,scope,collect,match}.{tks,c,h}` + `check.*`
  > `Unit`→`void`; `Unit | error`→`error?` (o C `tk_check_result` já É `error?` — só renomear conceito);
  > assinaturas internas; `cast_check`/`const_range_check`/`annotated_literal_ok` → `error?`. `Error`→`error`.
- **[Z2b] Codegen** — deps: Z1 · par: `src/codegen/codegen.{c,tks}`
  > `void`-return emite `void`; remover qualquer tratamento de *valor*/tipo `Unit`. Sem regressão no nativo.
- **[Z2c] VM** — deps: Z1, **Z-design** · par: `src/vm/vm.{c,tks}`
  > **Reconstruir `Value`** como variant de **tipos concretos reais** (`A | B`, sem construtores, sem `Unit`).
  > Chamada `-> void` não produz valor (statement). O `tk_value` em C continua união etiquetada (scaffolding C),
  > mas o espelho `.tks` usa a forma legal.
- **[Z2d] Superfícies stdlib** — deps: Z1 (sobrepõe `scope` com Z2a — coordenar) · par: `src/assert/*`, `src/runtime/teko_rt.*`, injeção em `scope`
  > `print`/`println`/`teko::assert::*` → `-> void` (não `-> Unit`).
- **[Z2e] Documentos** — deps: Z0 · par: `TEKO_LEGISLATION.md`, `TEKO_CHECKER.md`, `TEKO_SOURCE.md`, `TEKO_HISTORY.md`
  > Aplicar a tabela de §1 (Error→error; §270 `error?`; excisar `Unit` da CHECKER.md; registrar supersessão).

**Nulabilidade `T?` — COMPLETA na semente (Z3 = a). Tecida em Z1/parser/Z2:**
- **[Z-parser] Parse de `T?` / `?.` / `??`** — deps: Z0 · par: `src/lexer/*` (tokens `?`/`?.`/`??` se faltarem) + `src/parser/parse_type.*` (sufixo `?` no tipo) + `parse_expr.*` (`?.`/`??`).
  > `T?` em posição de tipo; `?.` acesso seguro; `??` Elvis. Domínio só-nulabilidade (erro→`match`, disjunto).
- **[Z3] Opcionais no checker** — deps: Z1, Z-parser, Z2a · par: `src/checker/*`
  > Tipar `T?` (estado: valor / `null` / default), `?.`/`??`; **regra por construção: membro de variant não pode ser anulável** (erro). Inicialização: `?`/default são as únicas ausências.
- **[Z3-codegen/vm] Repr. de opcional** — deps: Z3, Z2b, Z2c · par: `codegen.*`, `vm.*`
  > Baixar `T?` (presença + valor; niche/hasbits = otimização evolução) no nativo e no VM.

**Auditoria final:**
- **[Z4] Varredura de conformidade** — deps: Z1–Z3 · grep por `Unit`, `variant {`-com-construtor, `\bError\b`,
  `void`/anulável em membro de variant, em TODO `.tks`/`.c`/`.h`/`.md` → zero ocorrências não-conformes.

---

## §4 — Decisões de design

- **[Z-design] RESOLVIDA — modelo `Value` do VM.** Membros de variant = tipos completos (qualquer nativo,
  enum, struct, ou variant). Logo o valor-runtime é a união dos tipos concretos que um valor pode assumir:
  `type Value = u8 | u16 | u32 | u64 | i8 | i16 | i32 | i64 | bool | byte | str | []Value` (sem construtores,
  sem `Unit`). Uma chamada `-> void` não produz `Value` (statement). O `tk_value` em C segue união etiquetada
  (largura/sinal); o espelho `.tks` usa a forma acima. *(floats/`error`-como-valor entram quando o VM crescer.)*
- **[Z3 escopo] RESOLVIDA = (a) COMPLETO.** `T?` entra na semente com suporte pleno (type-former embutido,
  não exige generics): modelo de tipos + parser (`?`/`?.`/`??`) + checker + codegen + VM. A regra "variant não
  contém anulável" sai junto.

---

## §5 — Conjunto numérico nativo completo (legislador)

**Tipos numéricos nativos da Teko:** `u8 u16 u32 u64 u128` · `i8 i16 i32 i64 i128` · `f16 f32 f64` · `dec`
(decimal 256×256 = 512 bits) · `bigint` (precisão arbitrária) · `bool` · `byte`. *(O `Value` do VM e o
`PrimKind` crescem para conter todos — sem violar o alinhamento: só entram quando lexer/checker/codegen
suportam.)*

**Escopo ratificado: Tier 1 agora** (`u128`/`i128` + `f16`/`f32`/`f64` end-to-end); **`dec` e `bigint` =
crumbs runtime-backed diferidos.**

Crumbs (transversal: lexer → `PrimKind`/`scope` → checker → codegen → VM → runtime):
- **[N0]** registrar o conjunto como lei (LEGISLATION/HISTORY) + des-diferir floats. *(docs)*
- **[N1] inteiros 128** — `PrimKind += U128/I128`; `tk_builtin_type` += nomes; **valor de literal e portador do
  `tk_value` widened p/ 128** (`__int128`); AST `number` cabe 128; codegen `__int128`; VM 128; guards F3 (÷0/cast)
  estendidos; emit `.tkb` tags. *(sem questão de design — direto)*
- **[N2] floats** — `PrimKind += F16/F32/F64` (kind float); **literais float no lexer** (`.`/expoente — NOVO);
  checker (tipagem/inferência/conversões); codegen (`_Float16`/`float`/`double`); VM (kind float + aritmética).
  **Bloqueado nas decisões de floats (abaixo).**
- **[N3] `dec` (512-bit)** — runtime decimal em `src/runtime` (struct + add/sub/mul/div/round). **diferido.**
- **[N4] `bigint`** — bignum em `src/runtime` (limbs em heap). **diferido ("se possível").**

**Decisões de floats — RESOLVIDAS (legislador):** literal `3.14`/`1.5e3`, **tipo-default `f64`** (f16/f32 por
anotação) · **`÷0` float → PÂNICO** (igual int, `tk_panic_div0` — intercepta na origem, M.1) · **`float↔int`
via `to` permitido com GUARD em runtime** (trunca em direção a zero; overflow/NaN/∞ que não cabe → PÂNICO,
paridade com a guarda de cast inteira). *(O VM-debug pode aproximar f16/f32 via `double`+largura; o nativo usa
`_Float16`/`float`/`double` reais — divergência de arredondamento anotada.)*

## §6 — match / if-valor / desconstrução / `when` (sintaxe ✅ + execução pendente)

**Sintaxe (feito):** declarações de variant nos `.tks` corrigidas para `type X = variant A | B` (era a forma
superada `type X = A | B`; o parser exige `variant` — parse_decl.c:125; E.6 §6876). As `match` em si já eram
canônicas (`X as v => …`, `;`/newline); a desconstrução-em-match (`Type { f; g } =>`) e `let { x; y }` **não
são usadas** em `.tks` nenhum.

**Execução (pendente — o gap real):** `match`, `if`-como-valor, desconstrução e o guard `when` são
**parseados + tipados** (incl. `has_when`/`guard` no `tast`, exaustividade exclui arms guardados) mas **NÃO
são baixados** — codegen e VM os recusam ("not yet supported"). Logo `when` nunca executa. Crumbs:
- **[ME-cg] codegen**: baixar `if`-valor (ternário/bloco C), `match` (cadeia de testes por tag de variante +
  bindings `as`/field-form + ranges + Alt), `when` (guard condicional no arm), desconstrução (`let { } =` →
  extração de campos). Exaustividade já garantida pelo checker.
- **[ME-vm] VM**: interpretar os mesmos — `if`-valor, `match` (discriminar a tag do `tk_value`/variante +
  binds + guard `when` + ranges + Alt), desconstrução. Espelhar a semântica do codegen.
- **[ME-use] usar nos `.tks`**: onde o estilo pede, trocar `X as x => x.f` por field-form `X { f } =>` e
  usar `let { … } =` — *opcional/estilo, após a execução funcionar*.

> Nota: `match`/`if`/desconstrução não existem como STATEMENT no `tast` (só como `TExpr` IF/MATCH + binding
> destructure); a execução baixa as formas de expressão + a binding-destructure.

## §7 — Backlog-mestre de EQUALIZAÇÃO (auditoria canon→semente)

Resultado da auditoria (6 frentes). Ordenado por dependência. Meta: 100% antes do commit.

**Onda 1 — Antipatterns de match nos `.tks` (correção, fonte canônico, barato):**
- **W1a** `vm.tks`: ~17 padrões-construtor `Type(x) =>` → `Type as x =>` (linhas 184–197, 314–337). [a queixa direta]
- **W1b** `vm.tks`: `match <bool>`/`match try_builtin {…}` → `if` (try_builtin retorna bool → use `if`). Aninhamento triplo (l.378–383) → achatar com `when`/early-return.
- **W1c** varrer demais `.tks` por qualquer `Type(x)`/match-bool residual.

**Onda 2 — Literais + sintaxe faltante (lexer/parser/AST, MANDADO-mas-ausente):**
- **W2a** literais: `true`/`false` (bool), `null`, char `''` (= byte zero) — tokens + AST (`BoolLit`/`NullLit`) + parser.
- **W2b** nulabilidade: token `?`, `?.`, `??`; **`T?` no parser de tipos** (`TypeExpr += OptionalType`) — a lei diz que a semente implementa `T?` *fully*, e o parser não parseia. CRÍTICO.
- **W2c** operadores faltantes: `&&=`, `||=` (compostos), `$` (concatenação, distinto de `+`), `in` (membership).
- **W2d** keywords faltantes: `pub` (visibilidade ≠ `exp`), `defer` (cleanup block-scoped) — tokens + AST + parser.

**Onda 3 — Checker (MANDADO-mas-ausente):**
- **W3a** tipar `true`/`false`/`null`/char; tipar `T?` + `?.`/`??`; regra "membro de variant não anulável" (já existe) cobre.
- **W3b** **definite-assignment / init analysis** (REBOOT §83): init obrigatória, use-before-init = erro, não-usado = erro(local)/warning(privado). AUSENTE — gap real.
- **W3c** visibilidade `pub`/`exp` (enforcement; parte é fase de linker — confirmar escopo).

**Onda 4 — Camada de VALOR struct/variant (codegen + VM) — A FUNDAÇÃO:**
- **W4** representar valores de `struct` (campos nomeados) e `variant` (discriminante + payload) em C e no `tk_value`; construção; **field access** (VM). Destrava W5/W6.

**Onda 5 — Execução de controle (codegen + VM):**
- **W5a** `if`-como-valor (ternário/bloco) + `match` escalar (literal/range/Alt) + **`when` guards** — não precisa de W4.
- **W5b** `match` sobre variant + **desconstrução** (`let { } =`, field-form em arm) — depende de W4.

**Onda 6 — `T?` lowering (codegen + VM):** representação presença+valor; `?.`/`??`.

**Onda 7 — Frontier restante (evolução-ish):** parâmetros de função (multi-segmento/módulo), `[]T` slice no codegen, tipos named/struct no codegen.

> **Já equalizado (auditoria confirmou):** Unit excisado, void/error/optional no modelo, conversões B.38, same-type (B.22), exaustividade (`_` opcional, `when` excluído), range-pattern int-only, nominal, float-default-f64, ÷0 float panic, Tier-1 numérico, variant-keyword nos `.tks`.

## §8 — Sistema de módulos (legislador — falhas grotescas, codebase-wide)

1. **Qualificação de namespace OBRIGATÓRIA.** Um tipo/símbolo de OUTRA namespace deve ser referenciado por
   **caminho absoluto** `teko::checker::TBinary` **ou** por `use teko::checker` (liga `checker`) + `checker::TBinary`.
   Bare só para a MESMA namespace. Hoje: `vm.tks` usa `TExpr`/`TBinary`/`Path` bare; **zero `.tks` têm `use teko::`**;
   alguns qualificam `lexer::X` sem o `use`. → **[W-ns]** varredura: corrigir TODA referência cross-namespace nos `.tks`.
2. **`use` = ALIAS (Go-like), não importa conteúdo (C#).** `use teko::X` liga o último segmento (`X`); NÃO traz
   símbolos para o escopo bare. A implementação atual finge C# (refs bare cross-namespace) — errado. → parte de W-ns
   (+ garantir que o checker resolva por caminho/alias, não por last-segment-global como hoje em A3).
3. **Visibilidade `pub`/`exp` (sem exceções):** privado (default) = **só dentro da própria namespace**; **`pub`** =
   acessível por outras namespaces (interno ao projeto); **`exp`** = pub + exportável externamente (vai p/ `.tkh`).
   Cross-namespace SEM `pub`/`exp` = erro. → **[W-vis]** token `pub` + enforcement no checker + marcar todos os `.tks`.

**Eixo execução = (c) tudo na semente** (decisão: não há o que decidir — deve estar tudo):
- **[W4]** camada de valor struct/variant (codegen+VM). **[W5]** match/if-valor/`when`/desconstrução (codegen+VM).
- **[W6]** `T?` lowering. **[W2b]** `pub`/`defer`/`in`/`&&=`/`||=`/`$`/char. **[W3]** definite-assignment. **[W7]** params/`[]T`/named.

> Ordem de dependência sugerida: **W-vis** (token+modelo) → **W-ns** (usa visibilidade p/ saber o que é referenciável) →
> **W4** (valor struct/variant) → **W5** (match/desconstrução exec) → **W6/W2b/W3/W7**. Cada onda fecha verde.

### §8.1 — Rulings operativos do sweep (law-first; ratificados para a varredura)
Recon (11 ns, 514 símbolos top-level, 797 refs cross-ns, **501 bare cross-ns**, 133 pub-needed, 7 colisões). Doutrina aplicada:
1. **Ref cross-ns a símbolo TOP-LEVEL (tipo/fn) → qualificar** `NS::Sym` + `use teko::NS` (forma curta), ou absoluto `teko::NS::Sym`. (Usuário-confirmado; M.3/M.2.)
2. **Membro de enum em arm de match** (`I8`, `Plus`, `Bool` — membros de PrimKind/TokenKind, NÃO são top-level) → **fica bare**; o tipo (qualificado) do `subject` carrega a origem (M.5 desempata). Regra mecânica: bare só vira qualificado se o nome estiver em `definedBy[outraNS]`.
3. **Tipo-membro de variant** (`TStrLit as s` — caso de variant é tipo declarado, B.37) → **qualificar**.
4. **Prelude já absoluto** (`teko::list::*`, `teko::io::*`, `teko::str::*`, `teko::abort`, `teko::fdiv`, …) → **inalterado** (absoluto sempre válido).
5. **`pub` vs `exp`:** o compilador é `artifact = binary` (sem ABI externa / sem `.tkh` consumidor) → símbolos cross-ns são **`pub`**, nunca `exp` (M.3 — `exp` mentiria uma exportação de header que não ocorre). Privado→`pub` se acessado cross-ns; `pub`/`exp` existentes preservados (exp ⊇ pub).
6. **`use` é file-local** (escopo por arquivo, B.32) → cada arquivo declara seus próprios `use teko::NS`.
7. **Nomes errados** (`TStr`→`TStrLit`, `TLoop`→`TLoopStmt`, `TByte`→`TByteLit` em vm.tks) → correção semântica à parte (flag, não auto-fix no sweep mecânico).
> Promover §8.1 a LEGISLATION/HISTORY após ratificação final do legislador. Colisões (resolver por contexto): `Env`, `Return`, `cast_may_lose`, `define`, `prim_width`, `read_str`, `run`.

### §8.2 — Item/Program (drift de modelo AST) — RESOLVIDO (legislador: opção A — espelhar C)
`ast.tks` dizia Item/Program RETIRED, mas todo o pipeline C (ast.h `tk_item`/`tk_program`, driver.c, collect.c, typer.c, assemble.c) e os mirrors `.tks` (driver/collect/assemble) ainda os usavam — drift grotesco (`.tks` mentia). **Decisão do legislador: opção A — restaurar Item/Program em `ast.tks` espelhando o C** (SUPREME RULE; M.3/M.4/M.5). Feito:
- `ast.tks`: `ItemKind = variant UseDecl | Statement | Function | TypeDecl`; `Item = struct { content: ItemKind; namespace: str }` (a união-tagueada do C + o campo `namespace` de proveniência A3); `Program = struct { items: []Item }` — todos `pub`. Comentário "RETIRED" do `ast.h` corrigido.
- Consumidores migrados ao modelo: `collect.tks`/`typer.tks` casam `item.content` e usam `parser::Item`/`parser::Program`; `assemble.tks` retorna `parser::Program` (+ `use teko::parser`).
- `driver.tks`: construtores proibidos `Item::Use(u)` → `parser::Item { content = u, namespace = "" }`; match `Decl::Function`/`Decl::Type` → `parser::Function`/`parser::TypeDecl`; nomes errados `emit_c`→`codegen::tk_emit_c`, `SourceFiles`→`[]build::SourceFile`, `source_files_push`→`teko::list::push`.
> Resíduo conhecido (deferido, fora do módulo-sistema): `driver.tks` usa `for … in` (não existe — só `loop`/M.5) e surfaces deferidas (`teko::io`/`process`/`env`); o cabeçalho do arquivo já declara "NOT YET SEED-COMPILABLE". Erro de parse pré-existente em `teko build teko.tkp` (parser-completeness / D2) é independente deste trabalho.

### §8.3 — W-vis-enforce: enforcement do sistema de módulos no checker — FEITO (incremento 1: type-level)
Passo dedicado `tk_check_modules` (novo par `check_modules.c`/`.h` ↔ `check_modules.tks`) roda em `type_program` após `collect`, ANTES de tipar (M.1, fail-loud). Lê `item.namespace` (A3) direto — NÃO threada contexto de namespace pela cadeia de tipagem (baixo ripple). Substrato: `TypeReg` (type table) ganhou `namespace` + `vis` (preenchidos por `collect`). Constrói o alias-map dos use-items (file-local, B.32) e valida TODA referência de TIPO (params/return de fn; campos/membro-de-variant de type decl). Regras aplicadas (validado por 6 cenários):
- bare cross-ns → erro ("qualifique"); same-ns bare → OK.
- qualified curto sem `use` → erro ("alias unbound").
- qualified a tipo **privado** cross-ns → erro ("marque `pub`/`exp`").
- qualified ao namespace errado → erro.
- qualified-com-use a `pub` / absoluto a `pub` → OK.
> **Increment 2 (W-vis-enforce-2, task #41):** value-level (callees de chamada + paths de membro de enum cross-ns) — precisa do env/funções com namespace+vis + walk de corpo. C verde; teste de regressão (mesmo-ns bare) OK.

## §9 — W4 (camada de valor struct/variant) — DIAGNÓSTICO + PLANO (design recon feito)
**Descoberta-chave (keystone):** NÃO existe NÓ DE CONSTRUÇÃO de struct/variant no parser/checker da semente. `tk_expr_kind` (ast.h) e `tk_texpr_tag` (tast.h) não têm `StructLit`/construção; o corpus usa `Name { field = value }` em todo lugar (`Item { content = u }`, `TypeReg { … }`, …). **Confirmado: é a causa do erro de parse pré-existente** `teko build teko.tkp` → "expected ';', a newline, or '}' after a statement" (o parser lê `Name` e engasga no `{`). Logo **W4a destrava o self-parse do corpus inteiro** (→ checker + W-vis-enforce rodam sobre o corpus real).

**Ruling (law-first, M.1 sem-ambiguidade + M.2 legível):** literal de struct `Path { … }` **NÃO é parseado na posição de scrutinee** de `if`/`match` (regra estilo-Rust); resetado para permitido dentro de `()`/args. Sintaxe de construção = `Path { field = value (, | ; | newline)* }` (igual ao corpus; `=`, não `:`).

**W4a — nó de construção (parser + checker + tast + tkb):**
- ast.h/ast.tks: `StructLit { type_path: Path; field_names: []str; field_vals: []Expr }` + `TK_EXPR_STRUCT_LIT`.
- parse_expr.c/.tks: threadar flag `allow_struct` pela escada (coalesce→…→atom; ~11 fns); em `parse_atom`, após `parse_path`, se `{` e allow_struct → parse struct-lit. parse_if/parse_match passam allow_struct=false no scrutinee; `()`/args resetam true.
- expr.c/.tks (checker): tipar — resolver o tipo nomeado, checar cobertura+tipo de cada campo (ordem declarada), emitir nó tipado.
- tast.h/.tks: `TK_TEXPR_STRUCT_INIT` (tipo resolvido + exprs por campo, em ordem; guardar índice/ordinal do campo p/ ambos backends concordarem).
- tkb_write/read: serializar o novo nó.

**W4b — VM (vm.c/vm.tks):** tags `TK_VAL_STRUCT { type_name; field_map(names[]/vals[]) }` + `TK_VAL_VARIANT { case_name; payload* }` (membros primitivos do variant ficam bare, tag-discriminados; só casos nominais ganham wrapper). eval da construção; field access (hoje `vm_unsupported`→abort): avaliar receiver→struct, achar campo por nome. VM precisa registrar TK_TITEM_TYPE_DECL p/ resolver layout (hoje pula).

**W4c — codegen (codegen.c/.tks):** mangle `tk_t_<NS>__<T>` (usar `item.namespace`); struct→`typedef struct {…}`; variant→enum-tag + `struct { tag; union as; }`; construção; `x.field`. (Backend C mantido equalizado — fallback/comparativo, B.39.)

> **Diferencial (M.1):** VM e codegen devem concordar (e o futuro backend nativo). W4a é upstream comum dos dois. Ordem: **W4a (parse+check+tast+tkb) → valida self-parse do corpus → W4b (VM) + W4c (codegen) em paralelo (diferencialmente equivalentes) → W5**.

### §9.1 — W4a FEITO + validado (parse+check+tast+tkb, C + mirror .tks)
Nó de construção `Path { f = v, … }` implementado e validado (C verde; projeto com struct-lit **type-checks OK**):
- **parser** (parse_expr.c/.tks): flag `allow_struct` threadada por toda a escada (coalesce→…→atom); reset em `()`/args; `parse_struct_lit` (`,`/`;`/newline seps, empty `{ }`); entradas `parse_expr` (allow) + `parse_expr_no_struct` (scrutinee). parse_if/parse_match (c/.tks) usam a forma no-struct. AST `StructLit` + `TK_EXPR_STRUCT_LIT`.
- **checker** (expr.c / typer.tks): `type_struct_lit` — resolve struct nomeado, exige exatamente os campos declarados (count/missing/dup), tipa cada valor (adapta literal numérico que cabe), `type_eq`, emite na ORDEM declarada. Dispatch wired.
- **tast** (tast.h/.tks): `TK_TEXPR_STRUCT_INIT`/`TStructInit { field_names; field_vals }`. **tkb** (write/read .c+.tks): tag 16. **revalidate** (.c/.tks): caso struct-init.
- Disambiguação struct-lit-vs-bloco validada (if/match scrutinee parseiam; struct-lit parseia). Erro de parse restante do corpus = `for…in` em driver.tks (não-conforme/deferido; só `loop`/M.5), NÃO struct-lit.
> **Gaps registrados:** W2-tkb-mirror (tkb .tks faltam tags 12-15, pré-existente, #45); if-as-value trailing-return type mismatch (pré-existente, #46). **Próximo: W4b (VM) ∥ W4c (codegen)** — execução da camada de valor, diferencialmente equivalentes.

### §9.2 — W4b ∥ W4c FEITO + validado (camada de valor struct, dois backends concordam)
- **W4b (VM):** `tk_value` ganhou `TK_VAL_STRUCT { type_name; fields(names/vals) }`; eval de `TStructInit` (constrói em ordem declarada) + `TFieldAccess` (lê campo por nome). `error` é struct-value (type_name "error"). C + mirror `vm.tks`.
- **W4c (codegen, draft de agente + integrado):** mangle `tk_t_<ns>__<T>`; emite typedef de struct, enum-tag+union de variant, enum; construção = compound literal em ordem declarada; `x.field` já emitia. Forward-typedefs p/ recursão. C + mirror `codegen.tks`.
- **Validação diferencial (B.39):** `m::P { x=7, y=2 }; return p.x` → **VM exit 7 == binário compilado exit 7**. C verde; C gerado compila+roda.
- **Honest frontier (W5):** construção de VALOR de variant (membro→slot de variant, wrapping) + match-sobre-variant; codegen deixa `error`-value e slice/union em membro como fail_node honesto.
> Gap registrado: mangling de FUNÇÃO cross-ns no codegen (call `ns__fn` vs def `fn` bare) + return-implícito-da-última-expr (#49).

## §10 — Diagnósticos com localização (file:line:col) — W-loc
**Diretiva do legislador:** todo erro de compilação deve carregar **arquivo e linha**. **Decisão de design (law-first):** a localização vai **na MENSAGEM** do erro, NÃO em campos do tipo `error` — adicionar campos a `error` quebraria todo `error { message = … }` do corpus (regra de campos-exatos do W4a). Sem amendmento a B.1. (M.3 honesto sobre ONDE; M.5 sem tipo-diagnóstico paralelo.)

**Phase 1 (FEITO + validado) — erros de lex/parse → `file:line:col`:**
- `Token` ganhou `line:u32`/`col:u32`; o lexer estampa cada token (`compute_loc` 1-based + `stamp` em `tokenize`).
- `tk_err_at(t,n,pos,msg)` (cursor) formata `"line:col: msg"` do token ofensor; `err_at(tokens,pos,msg)` no `.tks`.
- ~51 sites de erro do parser varridos para `tk_err_at` (C + mirror `.tks`); `assemble` (`diag_file`) prefixa o arquivo → `"file:line:col: msg"`; `driver fail()` imprime mensagem já-formatada (path vazio = sem prefixo redundante).
- Validado: params/struct-lit/type-decl mostram `src/m/a.tks:L:C: …`. C verde.

**Phase 2 (FEITO + validado) — erros do checker/typer → `file:line:col`:**
- `Function`/`TypeDecl` ganharam `line:col` (parser estampa o token do nome); `Item` ganhou `file` (`assemble` tagueia de `sf.path`).
- `tk_diag_at(file,line,col,msg)` formata `"file:line:col: msg"` (omite partes ausentes). `check_modules` e `type_program` envolvem o erro com `file` (do item) + `line:col` (do decl) — **sem threading pela árvore tipada** (ambos iteram os items do parser, que carregam o decl + file). `driver` passa path vazio (msg já tem file:line:col).
- Validado: cross-ns private → `src/m/a.tks:2:10: …`; argument mismatch → `src/m/a.tks:2:8: …`. C verde; projeto limpo ainda checa OK.
- Granularidade: erros no CORPO da fn localizam na linha do DECL (file+line presentes); coluna exata da expressão = refinamento futuro (nós AST/tast carregarem loc).
> Resíduo registrado: `typer.tks type_program` não threada env entre statements soltos como o C (divergência .tks↔.c pré-existente, fora do escopo W-loc-2).

## §12 — W5 (match / if-valor / desconstrução — execução)
**W5a-checker FEITO (#46 resolvido):** o `if`/`match` FINAL de um bloco é o seu VALOR (B.20). `tk_type_block` agora tipa o `if`-com-`else` / `match` final via a forma de VALOR (`type_if`/`type_match` → tipo do ramo), não a forma-statement (void). `if`-sem-`else` final continua statement; não-final idem. Corrige o #46 (corpo `if`-valor não casava o retorno). C verde + mirror `typer.tks`. Validado: `-> i64 { if … }` e `-> bool { if … }` type-check.

**Descoberta (gap compartilhado):** ambos os backends capturam o valor de uma fn SÓ via `return` explícito — o **valor da última expressão (return implícito)**, incl. `if`/`match`, é descartado (VM `tk_vm_exec_block`/call; codegen idem). W5-exec precisa fiar o valor-de-bloco / return-implícito.

**W5-exec (task #50) — execução, nos DOIS backends, diferencialmente equivalentes:**
- return-implícito / valor-de-bloco (VM + codegen).
- VM: eval de `TK_TEXPR_IF`/`TK_TEXPR_MATCH` como valor (roda o ramo escolhido, rende o valor) + motor de match (literal/range/Alt/caso-de-variant/desconstrução-de-campo + guards `when` + bindings). Hoje `vm_unsupported`.
- codegen: lowering de `if`-valor (statement-expr C ou hoist-para-temp) + match (switch/if-chain sobre o discriminante). Hoje `fail_node`.
- W5a = if-valor + match escalar + `when`; W5b = match sobre variant + desconstrução (usa a camada de valor W4, feita).

### §12.1 — W5a (VM): if-valor + return-implícito + chamada cross-ns — FEITO + validado
- `tk_flow` agora carrega valor opcional (`value`/`has_value`): NORMAL = valor da statement (→ a última expr do bloco é o valor do bloco, B.20); RETURN = valor do return.
- `exec_block` rende o valor da última statement; `exec_if` roda `if` como control-flow (eval cond → roda o ramo → flow do ramo; return/break/continue propagam; trailing vira o valor); `exec_stmt` roteia `if` statement/tail p/ `exec_if`; chamada de fn faz **return implícito** (return explícito OU valor trailing); `tk_vm_run` usa o valor trailing do main como exit; eval de `if`-valor (aninhado em expr) rende o trailing.
- **Fix cross-ns:** `find_function` resolve pelo ÚLTIMO segmento (era só single-segment) → chamadas `ns::fn` funcionam na VM.
- **Validado (`teko run`):** `if`-valor em binding (7/8); `if`-valor trailing no main (7/8); stmt-if + mutação + trailing (9); `if` aninhado (5); chamada cross-ns `m::two()` (2, return implícito) e `m::pick()` (7, if-valor). C verde + mirror `vm.tks`.
> Resta no W5-exec (task #50): **codegen** if-valor (statement-expr/hoist) + **match** (motor de pattern-matching: literal/range/Alt/variant-case/desconstrução + `when` + bindings) em VM **e** codegen; depois prova diferencial VM==binário. Gaps relacionados: literal trailing não adapta ao tipo de retorno (`-> i32 { 2 }`); params de fn na VM (honest-stop); codegen cross-ns fn mangling (#49).

### §12.2 — W5a (codegen): if-valor + return-implícito — FEITO + validado
- **return-implícito**: a ÚLTIMA expr-statement de uma fn (não-void) vira `return <expr>;` (em main: `return (int)(<expr>);`); trailing void fica `<expr>;`.
- **if-valor**: lowering como statement-expression GNU `({ <T> _tkN; if (c) {…_tkN=then;} else {…_tkN=else;} _tkN; })` (clang/gcc aceitam sob o `cc` do projeto); cada ramo atribui o valor trailing a `_tkN`; `return`/`break`/`continue` num ramo divergem via C (igual à VM). Trailing `if` em posição-tail emite como control-flow direto (`if (c) {…return X;} else {…return Y;}`).
- **Fix cross-ns (#49 resolvido):** a chamada lowra para o ÚLTIMO segmento do path (nome bare), casando o decl bare de `emit_function` E o `find_function` da VM. Ambos backends = último-segmento → diferencialmente iguais.
- **Validado:** 12 casos diferenciais VM==binário (if-valor trailing, em binding, return-de-if, dois temps distintos, aninhados, loop+break, void trailing). C verde + mirror `codegen.tks`.

### §12.3 — W5b (VM): motor de match — FEITO + validado
- `eval_match` (match como VALOR): avalia o subject; para cada arm — env-filho compartilhando a cadeia do pai — se `pat_match` casa E (sem `when` OU guard verdadeiro) → avalia o corpo do arm nesse env e retorna; senão descarta os bindings do arm e tenta o próximo; fall-through não-exaustivo = honest stop (o checker garante exaustividade).
- `pat_match`: WILDCARD (sempre); LITERAL (`value_eq(subj, lit_as(...))`); RANGE (subj int; lo/hi via `lit_as`; compara signed/unsigned); ALT (qualquer opção casa; opções não bindam); BIND (`Foo`/`Foo as x` — subj é struct; `type_name` == último segmento do path; `as x` linka o valor inteiro); FIELD (`Type { f; g }` — subj struct; type_name casa; linka cada campo).
- Helpers: `path_last`, `value_eq`, `lit_as` (literal AST → valor comparável, herda width/sign do subject), `env_pop_to` (descarta bindings entre arms). `TK_TEXPR_MATCH` agora `eval_match` (era honest stop).
- **Validado (`teko run`):** escalar literal(20)/range(2)/alt(7)/wildcard(5)/`when`(8)/var(3); variant FIELD-destructure(7,4)/BIND+field(9)/`when`(50)/caso+wildcard(5). C verde.

### §12.4 — Named-variant widening (B.14/B.15) — FEITO + validado
- **Gap pré-existente descoberto:** um caso (`Circle`) NÃO alargava para um variant NOMEADO (`Shape`) — só variants INLINE (`A | B`) alargavam (`resolve_named` devolve sempre NAMED; `assignable_to`/`tk_exhaustive` só viam `TK_TYPE_VARIANT`). Tornava variants nomeados quase inutilizáveis (essencial p/ self-host — `TExprKind`/`TStatement` são variants nomeados).
- **Fix (aditivo, baixo risco — não muda a representação NAMED):** novo `tk_expand_variant(t, table)` (resolve.c) — um NAMED cujo decl é `variant` → seu `TK_TYPE_VARIANT` (membros ficam NAMED → termina). `assignable_to` agora recebe `table` e expande o alvo NAMED-variant (inclusão de caso); `type_binding` usa `assignable_to` (era igualdade estrita) → `let s: Shape = Circle { … }` alarga; a família `check_returns` threada `table`; `tk_exhaustive` recebe `table` e expande o subject NAMED-variant.
- **Validado (`teko run`):** binding-widen + FIELD(7,4); BIND+field(9); `when`(50); caso+wildcard(5). C verde.
> Resta no W5-exec (task #50): **codegen** do match (dispatch por tag + wrapping de valor-variant, camada deixada como fail_node no W4c) — em andamento (agente) com prova diferencial VM==binário; mirror `.tks` (vm/resolve/typer/match) em andamento (agente).

## §13 — W5-cf (bateria de control-flow do legislador) — FEITO + validado
Bateria pedida: if-sem-else, else-if, if/else-if/else encadeado, **ifs sem brackets**, **break/continue com label**, encadeados, return/if/match profundos em loops aninhados.
- **Descoberta:** if-sem-else / else-if / encadeado e loops aninhados (break/continue/return/if-profundo) JÁ funcionavam — validados diferencialmente (VM==binário).
- **W5-cf-1 — bracketless if/else (#51) FEITO:** `if cond` / `else` sem `{}` quando o corpo é **uma** statement na PRÓXIMA linha (separador-gated). Vira um bloco de 1 elemento → checker/VM/codegen tratam igual a um bloco com chaves. Bônus: `else` pode ficar em linha própria após um bloco com chaves. parse_if.c + parse_if.tks. Validado diferencial (then/else/valor/dangling-else/sem-captura-de-irmã).
- **W5-cf-2 — labeled loops + break/continue rotulados (#52) FEITO:** **SINTAXE (decisão do legislador — sinalizar p/ veto):** `loop NOME { … }` (IDENT logo após `loop`), `break NOME` / `continue NOME` (IDENT na MESMA linha; bare = loop mais interno, inalterado) — postfix, sem sigilo, inequívoco. Um break/continue rotulado borbulha pelos loops internos até o loop cujo label casa.
  - Camadas: ast.h (`label` em loop_stmt; `tk_jump{label}` p/ break/continue) · parse_stmt.c · tast.h · typer.c (carrega label + **validador `check_labels`**: jump rotulado deve nomear loop ENVOLVENTE; jump bare deve estar em loop; labels únicos por corpo — senão colidem os goto-labels do C) · vm.c (`tk_flow.label` + matching/propagação no loop) · codegen.c (loop rotulado emite `tk_lbl_<N>_cont:`/`_break:`; `break N`→`goto …_break`, `continue N`→`goto …_cont`).
  - Validado: happy-path diferencial VM==binário (break/continue p/ loop externo, triple-nested break-to-top, bare-innermost preservado, combinação com bracketless-if). Erros LIMPos (Teko, não erro do C): label desconhecido, break/continue fora de loop, label duplicado, break p/ irmão não-envolvente.
  - Mirror `.tks` (ast/parse_stmt/tast/typer/vm/codegen) em andamento (agente).

## §11 — Carregador de pacotes / dependências (DESIGN DIFERIDO — post-correções)
**Pergunta do legislador:** quando o `.tkb` de uma dependência (referenciada no `.tkp`) é importado? O checker não valida `dep::Símbolo` sem os símbolos da dep carregados → a importação tem que estar pronta **antes do checker**.

**Resolução (ancorada na canon do pré-linker — LEGISLATION "package = .tkh + .tkb"):** DOIS momentos distintos:
1. **`.tkh` (INTERFACE — exp types + assinaturas) → carregada ANTES/NO `collect`**: semeia a type table + env do checker com os símbolos `exp` da dep (tagueados com `deproot::ns`), para que referências resolvam + tipem. É o que torna a checagem possível.
2. **`.tkb` (PAYLOAD — árvore tipada completa/IL) → fundido no PRÉ-LINK, antes do codegen** (já é canon: o pré-linker funde as árvores tipadas das deps + a do dev em 1 programa).

**Pipeline (projeto):** ler `.tkp` → **carregar pacotes (`.tkh` [+ `.tkb` guardado])** → discover+assemble do dev → **check (collect semeado com as interfaces das deps)** → **pré-link (funde `.tkb`)** → codegen|VM.

Descartado "depois dos tokens" (tokens são do dev, por-arquivo). "Antes do checker" = certo (o mais tarde possível; semeia o collect). **Encaixa no W-vis-enforce sem mudança**: a type table já tagueia namespace; deps entram como `deproot::ns`. M.1/M.4: deps vêm já-checadas; o dev tipa contra a interface; o pré-link funde árvores já-checadas.

**§11 PERMANECE ABERTO — sistema de pacotes é pós-SELF-HOSTED (legislador).** Assentado até aqui: (a) a posição no pipeline (interface antes do checker; payload no pré-link); (b) um pacote é um **`.tkl` (Teko Library)** = `.tkh` (interface) + `.tkb` (payload) — extensão registrada (LEGISLATION "file-extension registry"); (c) `[dependencies]` no `.tkp` aponta para um `.tkl`.

**A decidir (DIFERIDO até o self-host):** o **sistema de pacotes completo** — manifestos de pacote, instruções/metadados, semântica de import, e o **servidor + cliente (embarcado)** de pacotes — só será construído DEPOIS de chegar ao self-hosted (precisa do compilador em Teko para criar o ecossistema). Itens abertos: sintaxe de `[dependencies]` (chave→localização; alias de import); layout interno do `.tkl` (arquivo único empacotado vs `.tkh`/`.tkb` co-localizados); resolução (path local → registry/versão); o protocolo servidor↔cliente. **Não fechar §11; retomar pós-self-host.**

## Paralelismo (resumo)
**Z0 (serial)** → **Z1 (serial, linchpin)** → **{Z2a, Z2b, Z2c*, Z2d, Z2e} em paralelo** → **Z3** → **Z4**.
(*Z2c espera Z-design. Z2d coordena `scope` com Z2a — ou Z2a absorve a parte de `scope`.)

## §14 — Avaliação de guarda / segurança (SAST gate) — APÓS as correções (legislador)
**Diretriz:** assim que o plano de correção fechar, rodar uma **avaliação de guarda de segurança** para evitar problemas de segurança (alinha com o SAST gate da doutrina de orquestração).
- **Superfície do seed C23:** segurança de memória (sítios `malloc`/`realloc`/`abort`; escritas no `cbuf` do codegen; limites de leitura no `tkb_read`; aritmética de índices no lexer/parser); overflow de inteiros; a superfície do C gerado; caminhos de entrada não-confiável (parsing de `.tkp`/`.tks`/`.tkb`).
- **Forma:** workflow multi-agente adversarial (achar → verificar adversarialmente → sintetizar) quando as correções estiverem completas. Registrado como task #53.

## §15 — SELF-HOST PARITY (legislador: "o mesmo suporte do C deve residir no self-hosted") — EM ANDAMENTO
**Diretriz:** o conjunto de features do seed C deve ser parseável/checável/compilável pelo bootstrap, para o corpus `.tks` (64 arquivos) se auto-hospedar. Método: iterar `teko run .` (o compilador sobre o próprio `src/`), achar cada gap (parser/checker/codegen), implementar em TODAS as camadas (C + mirror `.tks`), validar diferencialmente (VM==binário), repetir. **Achado-chave:** "compilar Teko com Teko é o teste real do compilador C" — os bugs do compilador (ex.: o heap-corruption do `tk_env_define`) afloram aqui.

**Gaps fechados (cada um diferencial VM==binário):**
1. **subscript `x[i]`** (str→byte bounds-checked + `str.len`→u64) — node Index em todas as camadas (#54).
2. **`i++`/`i--`** — desugar p/ `+= 1`/`-= 1` (lexer tokens + parser).
3. **hex/binário** `0x..`/`0b..` — lexer read_number + decoders; guard do float-predicate p/ o `E` hex.
4. **corpo de arm de `match` = bloco** + arms divergentes (`=> return`/`=> {bloco}`/`=> break/continue`) — modelo idêntico ao `if`-branch, nos dois backends.
5. **continuação de linha após operador infixo final** (`a ||⏎ b`) — skip_seps após o operador em cada nível do ladder.
6. **`&&`/`||` lógicos** — gap do CHECKER (`type_binary` não os tipava); ambos backends falhavam.
7. **keyword contextual `type` como nome de campo** (164 usos de `.type`) — `tk_is_name_at` (IDENT | `type`) nos 4 sítios de nome-de-campo.
8. **+bônus:** erros do LEXER agora carregam `file:line:col` (`scan_err_at`) — completa o W-loc no lexer.
- **Bug de heap (tk_env_define) corrigido** + regressão committada (`examples/regressions/match_pattern_bindings`, VM=5==bin=5).

**Em andamento:** (gap #7 do march) **interpolação de strings `$"…{expr}…"`** (21 usos; precisa de runtime str-builder + int→str) — agente.

**Escopo grande restante (backend):** o subsistema **slice/list/`teko::list` stdlib** (codegen tem ZERO suporte a slice) e **parâmetros de função no codegen+VM** (toda fn do corpus tem params; ambos backends honest-stop em params hoje). São features multi-camada grandes — o front-end (parse+check) avança bem mais fácil que o backend (codegen/exec).

**Gaps fechados (cont.) — todos diferenciais VM==binário:**
9. **interpolação `$"…{expr}…"`** — lexer token + parser (re-lexa/parseia cada hole) + checker (holes str/int) + runtime (`tk_str_concat`/`tk_i64_to_str`/`tk_u64_to_str`) + VM + codegen + tkb (tag 18).
10. **type aliases `type X = <type-expr>`** (82 usos; `[]TypeReg`, `str`, named) — body ALIAS no AST; `resolve_named` resolve THROUGH; guard de ciclo.
11. **discard binding `let _ = expr`** — alvo `_`; codegen `(void)(expr);` (sem var C, repetições não colidem).
12. **keywords contextuais `type`/`to` como nome** — `tk_is_name_at` (IDENT|type|to) em campo/param/binding + **valor/path** (parse_atom/parse_path).

**PAREDE atual do march (gap #13): `typer.tks:249` — `null` como PADRÃO de match** (`match err? { null => {} ; error as e => return e }`) → entra no **subsistema de OPTIONALS** (`T?`/`null`/`?.`/`??`), que VM/codegen ainda honest-stop.

**Três grandes subsistemas de BACKEND restantes (cada um multi-feature):**
- **slices/lists/`teko::list` stdlib** — codegen tem ZERO suporte a slice; precisa repr + literais + push/empty + .len + indexação em codegen/VM.
- **parâmetros de função em codegen+VM** — toda fn do corpus tem params; ambos honest-stop hoje.
- **optionals `T?`/`null`/`?.`/`??`** — repr de valor opcional + null-pattern + safe-field + coalesce em codegen/VM.
> O front-end (parse+check) está perto da paridade; o BACKEND (codegen/exec destes 3) é a montanha — campanha multi-sessão.

> **Recomendação de checkpoint:** as correções W5 + 12 gaps de front-end + o fix de heap são um corpo validado e um limite natural de commit. Sugiro commitar isso agora e seguir o self-host de backend (slices/params/optionals/stdlib) como campanha rastreada (task #55), em vez de um único mega-commit. Dívida de mirror `.tks` do último lote (discard-binding + contextual-`to`) em andamento (agente).
