# Plano — §9 C: defaults de parâmetro + args nomeados `:=`

> **Status:** DESIGN. Read+design apenas — nenhum código de produto editado, nenhum build, nenhum
> reseed. Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 C (abaixo, VINCULATIVOS — o plano desenha À
> VOLTA deles, nunca os re-abre).
> **Irmãos:** §9 A (sobrecarga — `plano-secao9A-method-overload.md`) DEPENDE do mecanismo de
> nome-de-parâmetro que §9 C fornece; §9 B (duplicados-de-tipo por-aridade). §9 C é o FUNDAMENTO
> (defaults + args nomeados) sobre o qual §9 A pontua candidatos por nome (ruling 5 de §9 A).
> **Lei permanente:** Teko-only (.tks), W15+Javadoc-completo em TODA declaração nova, law-first.

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

1. **Sintaxe de default:** `nome: T = <const-expr>` numa assinatura; o default é uma expressão
   COMPILE-TIME const, preenchida no CALL-SITE pelo checker. **TRAILING-ONLY** (assim que um param tem
   default, todos os seguintes têm de ter também). `params` (variádico) é SEMPRE o último.
2. **Args nomeados no call-site usam `:=`** — `f(width := 5)`. Regra: posicionais ANTES de nomeados;
   assim que se começa a nomear, TODOS os restantes têm de ser nomeados ("começou nomeado → tudo
   nomeado"). `:=` pode nomear QUALQUER parâmetro.
3. **NOME de parâmetro é discriminante** sob defaults (amarra em §9 A: duas sobrecargas podem diferir
   só pelo nome de um parâmetro nomeável).
4. **Interação com sobrecarga (§9 A):** se uma chamada é AMBÍGUA entre uma sobrecarga e uma aridade
   preenchida-por-default, é erro COMPILE-TIME no call-site (resolvido em §9 A via `select_overload`;
   §9 C só fornece o mecanismo).

---

## 1. Estado de HOJE — o que JÁ está implementado (achado central)

**§9 C está ~85% construído.** O trabalho DEFARGS (2026-07-01) e o `params`-variádico (2026-07-01)
já entregaram quase toda a semântica; a lacuna HEADLINE é puramente de SUPERFÍCIE: os args nomeados
usam hoje `=` (`Assign`), e o ruling 2 manda `:=`. Inventário verificado por leitura:

### 1.1 Parser — feito
- **Default numa assinatura** `nome: T = <expr>`, com TRAILING-ONLY já rejeitado na declaração:
  `parse_params` (`src/parser/parse_decl.tks:83-96`) — `seen_default` marca o primeiro default; um
  param sem default depois dele dá o diagnóstico *"a default-valued parameter must be followed only
  by other defaulted parameters (defaults are trailing-only)"* (`parse_decl.tks:91-92`).
- **`params` variádico, sempre-último** (`parse_decl.tks:55-99`), com *"'params' must be the last
  parameter"* (`parse_decl.tks:98`).
- **Args nomeados no call-site** com a regra posicional-antes-de-nomeado + começou-nomeado→tudo-nomeado
  já enforçada: `parse_call_args` (`src/parser/parse_expr.tks:44-73`), `seen_named` +
  *"a named argument must be followed only by other named arguments (named args are trailing-only)"*
  (`parse_expr.tks:59-60`). **PORÉM o gatilho é `Ident '='` (`lexer::TokenKind::Assign`,
  `parse_expr.tks:52`), NÃO `:=`.** É esta a lacuna principal.

### 1.2 AST — feito
- `Param { … has_default: bool; default_expr: Expr }` (`src/parser/ast.tks:394`).
- `ParsedCallArgs { args; arg_names: []str; next }` — `arg_names[i]` paralelo a `args`, `""` =
  posicional (`src/parser/result.tks:13`).

### 1.3 Checker — feito
- **`Func` carrega `param_names: []str`, `n_required: u64`, `defaults: []Expr`, `variadic: bool`**,
  populados a partir da `parser::Function` em `func_type` (`src/checker/collect.tks:107-139`;
  `n_required` = índice do primeiro default, `collect.tks:112,126`) e no caminho de reconstrução de
  membros (`collect.tks:2506-2508`).
- **`resolve_defargs`** (`src/checker/typer.tks:1471-1522`) — a resolução no call-site: fast-path de
  identidade (`typer.tks:1475`), mapeamento nome→índice, valida args nomeados contra param reais e
  não-já-preenchidos-posicionalmente, preenche defaults para os trailing omitidos. Diagnósticos já
  presentes: *"unknown named argument '…'"* (`:1492`), *"named argument '…' was already provided
  positionally"* (`:1493`), *"missing required argument '…'"* (`:1514`), *"wrong number of
  arguments"* (`:1483`), e *"this function cannot be called with named arguments …"* para
  fn-valor/closure/builtin (`:1477`). **`:=` já pode nomear QUALQUER parâmetro** (o loop
  `:1486-1495` não restringe a trailing).
- **Fiado no `type_call`** (`typer.tks:2124`) para fns livres, e nas dispatch de interface/contrato
  (`typer.tks:1806`, `:1891`).
- **DEFARGS rule E** (restatement de default num contrato) — `collect.tks:821-834`.

### 1.4 Conclusão do inventário
A **semântica** de defaults + args-nomeados (binding, trailing-only, positional-before-named,
começou-nomeado→tudo-nomeado, nome-nomeia-qualquer-param) está PRONTA e testável hoje — apenas com a
grafia `=`. §9 C reduz-se a: **(A) migrar a superfície para `:=`**, **(B) fechar as combinações que
o DEFARGS deixou explicitamente por fazer**, e **(C) endurecer a semântica ao ruling** (default
const, args-nomeados em métodos).

---

## 2. As LACUNAS (o que falta, com file:line)

- **G1 — superfície `:=` (a lacuna HEADLINE, ruling 2).** Não existe token `:=` no lexer
  (`src/lexer/token.tks` só tem `Assign // =` na linha 50; `src/lexer/lexer.tks:686`); e
  `parse_call_args` deteta nomeados por `Assign` (`parse_expr.tks:52`). Verificado: ZERO usos de `:=`
  como operador no corpus (os 3 ficheiros com `:=` — `spine.tks`, `regex.tks`, `spine_test.tkt` — são
  comentários/gramática BNF, nunca código). O token está livre.
- **G2 — args nomeados em CHAMADAS DE MÉTODO.** `parse_expr.tks:550-554` DESCARTA `ca.arg_names` na
  forma `recv.method(…)` (comentário: *"named-call (DEFARGS) is NOT yet supported through
  MethodCall's `.method()` … revisit once OOP methods land"*). O nó `parser::MethodCall`
  (`ast.tks:210`) nem sequer tem campo `arg_names`; e `type_method_call` sintetiza nomes todos-`""`
  (`typer.tks:1803-1805`). OOP JÁ aterrou (classes existem) ⇒ a ressalva expirou. Sob o ruling 2
  (`:=` nomeia QUALQUER parâmetro, sem distinguir fn de método), isto é uma lacuna de completude.
- **G3 — `params` + default/nomeado COMBINADOS.** `resolve_defargs` e `pack_variadic_args` são
  MUTUAMENTE EXCLUSIVOS: `type_call` chama um OU o outro (`typer.tks:2124`,
  `if f.variadic { pack_variadic_args(f, c.args, …) } else { resolve_defargs(f, c.args, c.arg_names) }`).
  `pack_variadic_args` (`typer.tks:1437-1461`) IGNORA `arg_names`. O comentário do design
  (`typer.tks:1466-1468`) admite: *"a function that is BOTH variadic AND has named/default params is
  an unsupported combination for now"*. Além disso o TRAILING-ONLY do parser (`parse_decl.tks:91`)
  REJEITA hoje `fn f(a: i64, b: i64 = 2, params rest: []i64)` (o `params` não-defaultado seguindo um
  default dispara o diagnóstico). **É preciso ruling do dono: `params` conta como "trailing default"
  (inerentemente opcional) ou fica proibido junto de defaults?** Ver §8.
- **G4 — default NÃO validado como const-expr (ruling 1).** Não há verificação de que `default_expr`
  seja compile-time const; o AST é simplesmente inlined no call-site e tipado como qualquer expr
  (`resolve_defargs:1516`). Um default `= agora()` seria hoje aceite e re-avaliado a cada chamada —
  desvia de "COMPILE-TIME const expr".
- **G5 (PRÉ-EXISTENTE; REPORTAR, provavelmente fora de §9 C) — fidelidade cross-`.tkb`.**
  `write_params` (`src/emit/tkb_write.tks:353-364`) só serializa `name + has_type + type_ann`;
  `read_params` (`src/emit/tkb_read.tks:598-609`) reconstrói `is_params=false; has_default=false;
  default_expr=no_expr()`, e o `Func`-tipo tag 8 (`tkb_read.tks:173`) reconstrói `variadic=false;
  param_names=[]; n_required=len; defaults=[]`. Logo assinaturas de MÉTODO lidas de um `.tkb`
  (pacote separadamente compilado, `read_params` usado em `tkb_read.tks:705,859`) PERDEM
  defaults/nomes/variádico. **O variádico já embarcou (2026-07-01) COM esta mesma limitação** ⇒ é
  pré-existente e o build intra-projeto coleta tudo da fonte junto (por isso funciona). Marco isto
  como achado ADJACENTE a reportar para cima, não como âmbito de §9 C (a menos que o dono decida que
  §9 C tem de funcionar através de fronteiras `.tkb` pré-compiladas).

---

## 3. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Cada crumb compila e passa o gate rápido; os RITUAIS (gate completo + fixpoint) estão marcados. A
disciplina de seed é a espinha da ordenação de G1 (o token `:=` tem de existir no SEED antes de a
fonte o USAR).

### Crumb 1 — token `:=` no lexer *(inerte; seed-safe)*
- `src/lexer/token.tks`: novo variante de `TokenKind`, **APENDIDO AO FIM do enum** (a seguir a
  `Service`, `token.tks:188`) por estabilidade de ordinal — `Walrus` nunca é um operador ARMAZENADO
  (só operadores aritméticos/bitwise/compound/comparação são serializados por ordinal para `.tkb`;
  `Walrus`, como `Colon`/`Semicolon`, jamais entra em `kind_byte`), logo apendê-lo não desloca
  nenhum ordinal existente.
- `src/lexer/lexer.tks`: regra de maximal-munch de 2 bytes `if c == b':' && c1 == b'=' { return
  sym(source, pos, 2, TokenKind::Walrus) }`, colocada JUNTO ao `::` (`lexer.tks:654`). Não colide com
  `::` (segundo byte difere) nem com `:` de 1 byte (`lexer.tks:687`), que continua a decidir sozinho.
- **Nota W15/estilo:** o enum `TokenKind` documenta cada variante por comentário inline (convenção do
  ficheiro — ~180 variantes assim); o novo `Walrus` segue a MESMA convenção (`Walrus   // :=`), com o
  contexto Javadoc a viver no cabeçalho do tipo. É um desvio DELIBERADO e local ao ficheiro-molde de
  W15-por-membro, justificado pela consistência com todos os irmãos (retrofit do enum inteiro está
  fora de âmbito).
- Sem call-sites novos; o `Walrus` fica reconhecido mas não usado. *Inerte.*

### Crumb 2 — parser aceita `:=` para nomear *(migração de superfície)*
- `src/parser/parse_expr.tks:52`: trocar o gatilho de nomeado de `Ident Assign` para `Ident Walrus`:
  `var is_named = is_kind_at(tokens, p, lexer::TokenKind::Ident) && is_kind_at(tokens, p + 1,
  lexer::TokenKind::Walrus)`. O `ap = p + 2` e restante lógica ficam iguais.
- **Disciplina de seed (CRÍTICA — ver §6).** O SEED atual (binário anterior) só conhece `=`. Duas
  vias, à escolha do implementador APÓS um grep de verificação:
  - **Se o corpus do compilador NÃO usa nenhuma chamada-nomeada** (provável — a feature é jovem): trocar
    para `:=`-ONLY num único passo é seed-safe (o seed antigo parseia a fonte nova, que não contém `:=`
    nenhum; o compilador novo passa a exigir `:=`, mas nada interno o usa). É a via preferida (uma só
    superfície honesta, ruling 2).
  - **Se existirem chamadas-nomeadas com `=` no corpus**: fazer DOIS passos para evitar deadlock de
    bootstrap — Crumb 2a: ACEITAR ambos (`Assign` OU `Walrus`) como nomeado; reseed → seed passa a
    conhecer `:=`. Crumb 2b (pós-reseed): migrar os call-sites internos `=`→`:=`, depois REMOVER a
    aceitação de `=` como nomeado (volta a `Walrus`-only; um `Ident '=' …` num argumento passa a erro
    de parse honesto, pois atribuição não é expressão). Reseed.
- **RITUAL: gate completo + fixpoint** (esta crumb muda a gramática aceite).

### Crumb 3 — args nomeados em CHAMADAS DE MÉTODO (G2)
- `src/parser/ast.tks:210`: `MethodCall` ganha `arg_names: []str` (paralelo a `args`), Javadoc do
  membro abaixo (§4.1). Atualizar TODOS os construtores de `MethodCall` (grep `MethodCall {`) para
  passar `arg_names` — `parse_expr.tks:541` (forma com type_args, hoje sem nomeados: passar
  `teko::list::empty()`) e `parse_expr.tks:554` (a forma `.m(…)`: passar `ca.arg_names`, deixando de
  os descartar).
- `src/checker/typer.tks:1739` (`type_method_call`) e o ramo de dispatch virtual (`typer.tks:1800-1806`):
  construir `bcnames` a partir de `mc.arg_names` deslocado por 1 (o receiver ocupa o índice 0, sempre
  `""`), em vez do vetor todo-`""` atual (`typer.tks:1803-1805`), e passá-lo a `resolve_defargs`. O
  mesmo para o caminho não-virtual (localizar a chamada gémea a `resolve_defargs`/arg-typing dos
  métodos de struct/classe). `MethodCall` é uma EXPR, não uma assinatura — não entra no codec `.tkb`
  (bodies não são serializados), logo NÃO há mudança de codec aqui.
- **RITUAL: gate completo** (toca um tipo AST central + o typer de métodos).

### Crumb 4 — default é compile-time const (G4)
- Validar `default_expr` como const-expr onde a assinatura é coletada — `func_type`
  (`src/checker/collect.tks:125`, no ramo `if f.params[i].has_default`) e no gémeo de membros
  (`collect.tks:2506`). Reusar a maquinaria de comptime-fold já existente (const-eval do checker) via
  um predicado novo (§4.2). Diagnóstico: *"a parameter default must be a compile-time constant
  expression"*. — Se o dono ACEITAR a semântica atual "inline a expr no call-site" (que já satisfaz
  "filled at the call-site" e é indistinguível para uma expr genuinamente const), esta crumb é
  OPCIONAL; recomendo enforçá-la por fidelidade ao ruling 1. Ver §8.
- **RITUAL: gate completo.**

### Crumb 5 — fixtures de regressão (ACEITAR + REJEITAR) *(§5)*
- Primeiro exercício ponta-a-ponta da superfície `:=` real. **RITUAL.**

### Crumb 6 — reseed + PROVENANCE *(§6)*

### Crumb OPCIONAL (só após ruling do dono) — `params` + default/nomeado (G3)
- Se o dono decidir que `params` é um "trailing default" legal: relaxar o TRAILING-ONLY do parser
  (`parse_decl.tks:91`) para EXEMPTAR um param `is_params` seguinte a defaults; e FUNDIR os caminhos
  `resolve_defargs`/`pack_variadic_args` num só que primeiro resolve nomeados/defaults do prefixo
  fixo e depois empacota a cauda variádica (hoje `pack_variadic_args` ignora `arg_names`,
  `typer.tks:1437`). Se o dono mantiver a proibição, adicionar apenas um diagnóstico EXPLÍCITO na
  declaração (em vez do trailing-only genérico) e uma fixture REJEITAR que o tranca. Ver §8.

---

## 4. Assinaturas / formas que o implementador adiciona (Teko, W15-Javadoc verbatim)

### 4.1 Campo novo em `parser::MethodCall` (Crumb 3)

Doc-comment do membro (verbatim):

```
pub type MethodCall = struct {
    receiver: Expr
    method: str
    args: []Expr
    /**
     * arg_names — the call-site argument NAMES, parallel to `args`: `""` for a positional
     * argument, else the parameter named by a `:=` named argument (§9 C ruling 2). Empty-string
     * for every entry in an all-positional call. Threaded verbatim from `parse_call_args`
     * (`ParsedCallArgs.arg_names`) so an instance dot-call `recv.m(w := 5)` reaches
     * `resolve_defargs` exactly like a free call `m(w := 5)` does. The receiver is NOT an entry
     * here — it is prepended (with name `""`) only when the typer builds the effective call.
     */
    arg_names: []str
    type_args: []TypeExpr
}
```

**Fns EXISTENTES que este campo toca:** todos os construtores `MethodCall {` (grep;
`parse_expr.tks:541,554`), `type_method_call` (`typer.tks:1739`) e o ramo de dispatch virtual
(`typer.tks:1800-1806`). Nenhum novo campo em `TExpr`/TAST é necessário (a resolução acontece antes
da materialização, reusando o pipeline de `resolve_defargs`).

### 4.2 Predicado novo no checker (Crumb 4, se enforçada)

```
/**
 * default_is_const — is a parameter's default-value expression a COMPILE-TIME CONSTANT (§9 C
 * ruling 1)? A default is materialized at every call site by the checker, so it must fold to a
 * constant with no runtime effect: literals, `const` reads, and const-foldable operators over
 * those. Anything that could observe or mutate runtime state (a function call, a `var` read, an
 * allocation) is rejected so `f(x: T = <expr>)` cannot smuggle per-call side effects into an
 * omitted argument.
 *
 * Delegates to the existing comptime-fold evaluator (the same one that vets `const` initializers
 * and array-length const-exprs); this is a thin yes/no wrapper over it, not a new evaluator.
 *
 * @param e      the parameter's default-value expression (`Param.default_expr`)
 * @param env    the typing environment (for `const` name resolution)
 * @param table  the folded type table
 * @return       true iff `e` is a compile-time constant expression
 * @throws       never — a non-const default is reported by the caller (`func_type`) with a located
 *               diagnostic, this predicate only classifies
 * @since §9 C
 */
fn default_is_const(e: parser::Expr, env: Env, table: TypeTable): bool
```

O implementador DEVE localizar o avaliador de comptime-fold já existente (usado por `const` e por
comprimentos de array const) e encaminhá-lo, em vez de escrever um segundo avaliador (evita
divergência de const-semântica).

### 4.3 Fns que NÃO mudam de forma (só de gatilho/uso)
- `parse_call_args` (`parse_expr.tks:40`): só o token-gatilho muda (`Assign`→`Walrus`), a assinatura
  e o resultado (`ParsedCallArgs`) ficam idênticos.
- `resolve_defargs` (`typer.tks:1471`): NENHUMA mudança de assinatura — já aceita `arg_names` e nomeia
  qualquer param. Passa a ser alcançado também pelo caminho de métodos (Crumb 3) e, se G3 avançar,
  pelo caminho variádico-fundido.

---

## 5. Fixtures de regressão (inputs → exit codes nativos)

Layout (confirmado, igual a §9 A): cada regressão ACEITAR é um projeto
`examples/regressions/<nome>/` com `<nome>.tkp` (manifesto), `<nome>.tkr` (Feature/Scenario),
`main.tks`, `src/`; o `main` **codifica em ARITMÉTICA** qual caminho correu e sai com esse número
(`exit`), para que um relapso MOVA o exit-code em vez de passar em silêncio. As REJEITAR dobram no
canal partilhado `examples/regressions/diagnostics/` (`src/<caso>/case.tks` +
`Then diagnostic = "<substring>"`). Os scripts descobrem por diretório.

### 5.1 ACEITAR — `examples/regressions/defaults_named/` (novo projeto)

Assinatura-base: `fn box(w: i64, h: i64 = 2, d: i64 = 3): i64 { w * 100 + h * 10 + d }`.
- **A1 — só posicionais, defaults preenchidos.** `box(5)` → `523`; `box(5, 7)` → `573`;
  `box(5, 7, 9)` → `579`.
- **A2 — nomear um trailing com `:=`, saltando outro.** `box(5, d := 9)` → `529` (h fica no default 2).
- **A3 — nomear no meio.** `box(5, h := 4)` → `543`.
- **A4 — TODOS nomeados, REORDENADOS** (legal: começou-nomeado→tudo-nomeado, ordem livre entre
  nomeados). `box(w := 5, d := 9, h := 4)` → `549`.
- **A5 — `:=` nomeia QUALQUER param, incluindo o primeiro** (ruling 2). `box(w := 6)` → `623`.
- **A6 — `:=` em CHAMADA DE MÉTODO (G2).** `class B(){ fn vol(self, k: i64 = 3): i64 { k * 7 } }`;
  `B().vol()` → `21`, `B().vol(k := 5)` → `35`.
- `exit` final = soma combinada única (ex.: uma expressão que só a combinação certa de todos os ramos
  produz), OU um `exit` por Scenario — seguir o padrão do projeto-molde `overload_resolve`/`builtins`.
  Expectativa `.tkr`: exit-code nativo esperado por Scenario.

### 5.2 REJEITAR — dobrar em `examples/regressions/diagnostics/` (um `src/<caso>/case.tks` cada)

- **R1 — default NÃO-trailing** (tranca `parse_decl.tks:91`): `fn f(a: i64 = 1, b: i64): i64 { … }` →
  `diagnostic = "trailing-only"`.
- **R2 — nomeado ANTES de posicional** (começou-nomeado→tudo-nomeado): `box(w := 1, 2)` →
  `diagnostic = "named argument must be followed only by other named arguments"`.
- **R3 — nome desconhecido**: `box(z := 1)` → `diagnostic = "unknown named argument"`.
- **R4 — nomeado colide com posicional**: `box(5, w := 6)` → `diagnostic = "already provided
  positionally"`.
- **R5 — obrigatório em falta**: `box()` → `diagnostic = "missing required argument"`.
- **R6 — trava a MIGRAÇÃO de superfície (G1):** `box(w = 5)` (o antigo `=`) → após Crumb 2 em
  `:=`-only, isto é erro de parse (atribuição não é expressão) → `diagnostic` do parser. Esta fixture
  é a REDE que prova que `=` deixou de nomear e `:=` é a única superfície.
- **R7 (se G4 enforçada) — default não-const**: `fn f(x: i64 = some_fn()): i64 { x }` →
  `diagnostic = "compile-time constant"`.
- **R8 (se G3 permanecer proibido) — `params` junto de default**: `fn f(a: i64, b: i64 = 2, params
  r: []i64): i64 { … }` → `diagnostic` explícito.

---

## 6. Ritual de reseed + PROVENANCE (Crumb 6)

Só depois de todas as crumbs verdes e do gate completo passar:

1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`).
4. Harvest: `bootstrap/teko.c` (novo seed) + atualizar `bootstrap/PROVENANCE`.
5. NUNCA correr `teko test .` (fuga de memória). Gate por `--no-verify` + os `scripts/*.sh`.

**Nota de disciplina de seed (a espinha de G1).** O token `:=` (Crumb 1) e a sua aceitação no parser
(Crumb 2) têm de estar NO SEED antes de a FONTE do compilador usar `:=`. Por isso:
- Crumb 1+2 aterram SEM que o corpus do compilador use `:=` (o seed antigo parseia-os).
- Reseed → o novo seed conhece `:=`.
- Só DEPOIS (se se optar pela via de dois passos, Crumb 2b) é que call-sites internos migram para
  `:=`. Se a via de um passo (`:=`-only, corpus sem chamadas-nomeadas) for tomada, não há migração
  interna e o reseed único basta.
- As fixtures (Crumb 5) usam `:=` mas vivem em `examples/` (corpus de TESTE), compilado pelo seed já
  reseedado — não afetam o auto-build do compilador, logo não há amarração de seed nelas.

---

## 7. Argumento de FIXPOINT (segurança)

**A migração de superfície é neutra no fixpoint por construção.** O que muda o BYTE de saída do
compilador é apenas: (a) o novo variante de token — apendido ao FIM do enum, sem deslocar ordinais de
operadores serializados (§3 Crumb 1), logo nenhum `.tkb`/`.tkr` existente muda; (b) o gatilho de
parse `=`→`:=` — se o corpus do compilador não contém chamadas-nomeadas (via de um passo), a árvore
de parse de TODA a fonte é idêntica antes e depois, logo a TAST e o código emitido são byte-idênticos
⇒ fixpoint fecha trivialmente. A Crumb 3 (arg_names em `MethodCall`) inicializa o campo a
`teko::list::empty()` em todas as chamadas de método SEM nomeados (a esmagadora maioria), pelo que o
comportamento de resolução é inalterado nesses; só chamadas que ESCREVEM `:=` num método (inexistentes
no corpus do compilador) mudam. A Crumb 4 (const-default) só REJEITA o que hoje já deveria ser const;
o corpus do compilador não tem defaults não-const (a verificar por build), logo é inerte. Conclusão:
para o corpus atual do compilador, TODAS as crumbs de §9 C são aditivas-inertes e o fixpoint fecha na
Crumb do reseed. As fixtures (Crumb 5) introduzem `:=` no CORPUS DE TESTE, não no compilador.

---

## 8. Riscos + tensões de lei (com resolução recomendada) / perguntas ao dono

- **T1 (TENSÃO A RESOLVER, ruling do dono) — `params` + defaults/nomeados (G3).** Os rulings 1 e 2
  listam ambas as features mas NÃO legislam a sua COMBINAÇÃO. Duas leituras law-first:
  - **(preferida) `params` é um "trailing default" inerente** — inerentemente opcional (0-ou-mais),
    logo `fn f(a, b = 2, params r)` é LEGAL e `params` é apenas o último "opcional". Isto passa o
    espírito de "trailing-only" e "`params` sempre último" sem contradição. Requer o trabalho da
    "Crumb OPCIONAL" (§3).
  - **(mínima) proibir explicitamente** a coexistência, com um diagnóstico honesto na declaração.
  Recomendo a leitura PREFERIDA (mais expressiva, sem tensão com os rulings selados), MAS como a
  combinação não está selada é uma **decisão ratificável do dono** — HALT parcial só nesta escolha.
  As crumbs 1–5 NÃO dependem dela e podem aterrar já; adiantei ambos os desenhos.
- **T2 (ruling do dono, menor) — enforçar default-const (G4)?** O ruling 1 diz "compile-time const
  expr". A implementação atual inlina a expr no call-site (satisfaz "filled at the call-site" e é
  correta para uma expr genuinamente const). Enforçar const é FIEL ao ruling e barato (reusa o
  comptime-fold); recomendo enforçar (Crumb 4 + R7). Sem tensão de lei — é fidelidade, não conflito.
- **R-seed (G1) — deadlock de bootstrap na migração de superfície.** Neutralizado pela disciplina de
  §6: adicionar `:=` (aceitar) e reseed ANTES de remover `=`/migrar internos. A via de um passo é
  segura sse e só se o corpus do compilador não tiver chamadas-nomeadas — o implementador DEVE
  confirmar por grep antes de escolher. Sem tensão de lei.
- **G5 (cross-`.tkb`) — ADJACENTE, reportado.** Assinaturas de método perdem defaults/nomes/variádico
  ao passar por `.tkb` (§2 G5). É PRÉ-EXISTENTE (o variádico já embarcou assim) e o build intra-projeto
  não é afetado. REPORTO para cima; não o transformo em issue nem o resolvo aqui, salvo o dono decidir
  que §9 C tem de cobrir pacotes `.tkb` separadamente compilados (aí: estender `write_params`/
  `read_params` com `is_params` + `has_default` + valor-const-do-default, e a tag-8 `Func` com
  variadic/param_names/n_required).
- **Sem outra tensão de lei genuína.** As lacunas G1–G4 encaixam num desenho law-first coerente; o
  único ponto que exige RATIFICAÇÃO do dono é T1 (a combinação `params`×defaults, não selada).

---

## 9. Resumo executivo para o integrador

**§9 C está ~85% pronto.** A semântica (binding de defaults, trailing-only, positional-before-named,
começou-nomeado→tudo-nomeado, nome-nomeia-qualquer-param) JÁ existe e funciona — só com a grafia `=`.
O trabalho de §9 C é: **(1)** dar-lhe a superfície selada `:=` (novo token + gatilho de parser +
reseed disciplinado) — Crumbs 1–2; **(2)** estender args-nomeados a chamadas de método — Crumb 3;
**(3)** endurecer default→const — Crumb 4; **(4)** fixtures + reseed — Crumbs 5–6. **Ratificação
pendente do dono:** a combinação `params`×defaults (T1), não selada. **Achado adjacente reportado:**
perda de metadados de default/variádico através de `.tkb` (G5), pré-existente.
</content>
</invoke>
