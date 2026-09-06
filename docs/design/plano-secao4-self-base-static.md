---
section: design
created: 2026-08-11
updated: 2026-08-11 (rulings finais do dono: D1 base reservado; D2 self sem slot; métodos
        SÓ in-body — sem `::` free-standing; self como tipo de retorno; fábrica = static fn)
status: PLANO FINAL (arquiteto, read+design) — sequência de crumbs para §4. NÃO implementado.
source: recon @ fix/retirement 96e70826 + rulings do dono (D1/D2/D3 + E/F/G fechados)
---

# Plano FINAL §4 — `self` / `base` / `static` (in-body)

## 0. Rulings firmes do dono (o que este plano obedece)

- **D1 — `base` é KEYWORD RESERVADA.** Varrer os ~30 arquivos que usam `base` como local/param/CAMPO
  → renomear ANTES de reservar. Campos `.base` (54 acessos) renomeiam junto com a declaração do campo.
- **D2 — `self` é keyword RESERVADA e SEM SLOT.** Receptor sintético IMPLÍCITO: `fn area(): i64 {
  self.raio }` (nenhum `self` na lista de params). Os 55 receptores já se chamam `self` → strip do slot
  é mecânico. Param de usuário chamado `self` = erro.
- **D3 — TODO método é IN-BODY.** NÃO existe declaração `fn Tipo::metodo` fora do corpo, nem coleta
  out-of-body. `::` e `.` são operadores de CHAMADA (já existem: `ColonColon`/`Dot`):
  - instância: in-body, `self` sintético, chamada `circle.area()`;
  - estático: in-body com keyword `static` (sem receptor), chamada `Circle::unit()`.
  O exemplo do doc `fn Circle::area()` lê-se como o método `area` DENTRO de `Circle`.
- **`self` como TIPO DE RETORNO** (`static fn make(): self { Circle{…} }`) = o TIPO ENVOLVENTE (o
  receptor numa instância; o próprio `Tipo` num `static fn`). Serve às fábricas.
- **Sem construtor inventado.** "Construtor" = FÁBRICA = um `static fn` que devolve um LITERAL de struct
  (`Circle { raio: 1 }`). NÃO há keyword `constructor`/`init`/`new` nem sintaxe de ctor. Confirmado:
  este plano NÃO introduz nenhum construto de construtor.
- **E — 3 reseeds** (fechado): mantém-se §12.1 (superfície removida ERRA); a inversão static⇄instance +
  R0 não lexar `static fn` impedem comprimir para 2. Sequência de 3 reseeds abaixo, sem exceção.
- **F — nomes SIGNIFICATIVOS por sítio** (fechado): o rename dos `base`-identificadores NÃO usa um
  `base_` cego; o implementador escolhe um nome que diga o papel de cada sítio (ex.: `super_ref`, `parent`,
  `sect_base`, `off_base`, `tmpl_base`, …).
- **G — `base` SUBSTITUI o binding nomeado de super** (fechado, um jeito só): o mecanismo atual
  `class Pai(nome) { … }` + `nome.metodo()` (AST `has_base_binding`/`base_binding_name`; checker
  `type_method` 5933-6010; vtable base 1396-1422) DEIXA DE EXISTIR. Super é acessado SEMPRE por `base`.
  Derivada declara-se `type Filho = class Pai { … }` (só o tipo-pai, sem nome de binding), e dentro de um
  método de instância `base.metodo()` alcança a super. Escala do sweep de call-sites: **ZERO** (§2b).
- **Reseeds no mínimo (= 3, por E).**

## 1. Modelo real da fonte (recon)

- Método é in-body: `parse_fields`/`parse_class_fields`/`parse_trait_fields`/`parse_interface_body`
  (`src/parser/parse_decl.tks`) coletam métodos via `parse_function(..., allow_receiver=true, ...)`.
- Receptor HOJE = param[0] untyped (`has_type=false`), sempre nomeado `self` no corpus. Tipo injetado
  pelo checker: `receiver_canonical_name`/`method_func_type` (`src/checker/collect.tks:133/172`).
- **static-vs-instance HOJE = forma do param:** `is_instance = params.len>0 && !params[0].has_type`
  (`src/checker/typer.tks:1368`; idem `:5768`). Sem receptor (params vazio OU 1º tipado) = static.
- **Chamada estática `Tipo::metodo()`** já é resolvida pela rota de call rooted (`typer.tks:848-1015`)
  + retargeting genérico (`typer.tks:1523-1616`); e `typer.tks:1369` já emite "'{m}' is a static
  method — call it as {T}::{m}(…)". `::` = `ColonColon` (`token.tks:84`).
- `is_name_at` (`src/parser/cursor.tks:40`) aceita `Ident|Type|To|In`. Reservar um token faz `is_name_at`
  devolver false para ele — por isso `base` só pode ser reservado DEPOIS de o corpus estar limpo dele.
- Resolução de tipo: `resolve_type` (`src/checker/resolve.tks:2113`, caso `NamedType` em `:2119`); átomo
  de tipo parseado em `parse_type_primary` (`src/parser/parse_type.tks:84`).

## 2. Escala do sweep (medida)

| item | escala |
|---|---|
| receptores de instância (strip do slot `self`) | **55 métodos** — todos já `self` (0 outro nome) |
| membros static a marcar `static` | **~17** (~6 param-vazio + ~11 1º-param-tipado) |
| `base` local (`var base`) | **59 sítios / 17 arquivos** |
| `base` parâmetro (`base:`) | **~10 fns** |
| `base` CAMPO (`.base`) | **54 acessos** (+ suas declarações de campo) |
| `self`/`static` como identificador real | **0** |

Arquivos do sweep de `base`: parse_type, parse_decl, parse_expr, typer, collect, resolve, monomorph,
codegen, lower, lir, teko_rt, stream, regex, zlib, base64, objfile_elf, encode_x86_64, encode_arm64,
dwarf, project, tkr, regression, journal, checked, math, nav, test, dec (~28-30 arquivos).

### 2b. Escala do super-binding nomeado (G)

- **`class Pai(nome)` no corpus: 0.** As 14 classes reais (io/stream, collections/map, list, file_stream,
  compress_stream) são todas `class & Iface` ou `class` puro — **nenhuma herda um base-class com binding
  nomeado**. Nenhum `.super`/`<binding>.metodo` de super a varrer no `src/`.
- **Fixtures:** nenhuma em `examples/`/`tests/` exercita `class X(binding)` (grep vazio). Se aparecer uma,
  converter `class Pai(nome){…}` → `class Pai{…}` e `nome.metodo()` → `base.metodo()`.
- **Conclusão:** G é quase-inteiramente remoção de GRAMÁTICA/AST/checker — o SWEEP de call-sites é um
  no-op no corpus. O risco de G está na cirurgia do AST/serialização (abaixo), não num sweep grande.

## 3. Por que o mínimo de reseeds é 3 (hard-error) — e a alternativa de 2

O sinal static-vs-instance **inverte**: hoje "sem receptor ⇒ static"; depois "sem `static` ⇒ instance
(self sintético)". Três fatos travam a compressão:
1. **R0 não lexa `static fn`** (`static` não é keyword nele) → `static` só aparece no corpus DEPOIS de
   um reseed que o ensine (R1).
2. O parser injeta `self` sintético para métodos **não-`static`**; se o fizesse antes de os statics
   estarem marcados, um `fn empty()` (static hoje, sem `static`) ganharia `self` → `Buf::empty()` quebra.
   Logo a **marcação de static precede a injeção**.
3. **Strip do `self` explícito** produz `fn area()` (bare); um seed sem injeção lê isso como static
   (forma do param) e o corpo `self.raio` fica sem receptor → não compila. Logo o **strip precede um
   reseed com injeção** (R2), e o **des-ensino** (grafia velha erra, ruling §12.1) precede o **reseed
   final** (R3).

⇒ **3 reseeds** para a versão hard-error (grafia velha = erro). **2 reseeds** SE o dono tolerar a
grafia antiga (`self` explícito no slot) como "aceita-mas-varrida" em vez de erro — ver DECISÃO E.
Recomendo confirmar; abaixo a sequência hard-error (3), com o ponto onde cai o 3º reseed se optar por 2.

## 4. SEQUÊNCIA DE CRUMBS FINAL

> Ritual antes de cada build: `export TK_RT_DIR="$PWD/src/runtime"`. Reseed é à mão pelo dono
> (bootstrap/teko.c→bin; `TEKO_BACKEND=c bin build . --no-verify --release` → OUT/teko.c; fixpoint
> byte-idêntico; colhe→bootstrap/teko.c; build seco, sem testes). "Build seco" = verificação do
> implementador; o RESEED é ordem do dono.

### FASE PRÉ — sweep de `base` (sob R0, grafia velha, SEM reseed)

**Crumb 0 — renomear todo uso de `base` como identificador.** Precede a reserva (senão a reserva
quebra o próprio corpus). Escopo: os ~28-30 arquivos da §2. Três classes de sítio, cada uma token-aware
e por-escopo:
- **local `var base`** (59): renomear a ligação e todas as suas leituras no MESMO escopo → `base_`
  (ou `sub`/`super_ref` onde for super-binding semântico). Reescrita por-função (o nome só vive no
  corpo da função) — segura porque `base` local não escapa.
- **parâmetro `base:`** (~10): renomear no cabeçalho + no corpo da função.
- **campo `base` + acessos `.base`** (54): renomear a DECLARAÇÃO do campo (`base: T` → `base_: T`) E
  todos os `X.base` do tipo JUNTOS, por-struct (o campo e seus acessos são co-locais ao tipo). Confirme
  cada struct: o `.base` é sempre acesso a um campo literalmente chamado `base`; renomeie o par.
- **RITUAL:** build seco sob R0 (grafia velha; só renomeio). Verde ⇒ corpus livre de `base` identificador.

> Armadilha (§7): alguns `base` locais são *espelhos* de um binding de super (ex. em typer/collect a
> lógica de vtable base). Renomear o LOCAL não toca o mecanismo de super (`base_name`/
> `base_binding_name` no AST, que são STRINGS, não o identificador `base`). Escolha nomes SIGNIFICATIVOS
> (F): `super_ref`/`parent` para espelhos de super, o papel do dado nos demais; confira que nenhum rename
> colide com um nome já existente no escopo.
>
> **Ordenação Crumb 0 ↔ G (explícita):** o rename dos `base`-LOCAIS é AQUI, sob R0 (base ainda é
> `Ident`), em direção CONTRÁRIA a `base` (renomeia PARA LONGE de `base`). A conversão super-binding→`base`
> (G) vai em direção OPOSTA (renomeia PARA `base`) e exige `base` já RESERVADO — logo só pode ocorrer
> pós-teach (R1+), num crumb separado. As duas nunca se cruzam: os call-sites de super hoje usam o nome
> do binding (ex. `parent.m()`), NÃO `base`, então Crumb 0 não os toca; e como há 0 bindings no corpus
> (§2b), o corpus real nem tem esses call-sites. (Se existisse um binding literalmente chamado `base`,
> Crumb 0 o deixaria intacto — mas não há.)

### FASE TEACH — pacote §4 aditivo (sob R0) → **RESEED #1 (R1)**

**Crumb 1 — reservar keywords + aceitar a grafia nova AO LADO da velha.** Um só commit, construído por
R0 (o source do compilador segue na grafia velha: `self` explícito, statics sem `static`, `base` já
renomeado).
- `src/lexer/token.tks`: `TokenKind += SelfKw, Base, Static`.
- `src/lexer/lexer.tks:keyword_kind` (l.332): `if text=="self" {SelfKw}`, `"base" {Base}`,
  `"static" {Static}`. (Reservar `base` é seguro agora — Crumb 0 limpou o corpus.)
- `src/parser/parse_expr.tks`: aceitar `SelfKw` e `Base` como PRIMÁRIOS de expressão (→ `Var{name="self"}`
  / `Var{name="base"}`, ou nós dedicados `SelfExpr`/`BaseExpr`). Necessário porque, reservados, deixam de
  passar por `is_name_at` — os corpos dizem `self.raio`/`base.render()`.
- `src/parser/parse_decl.tks:parse_function`: consumir a keyword `static` na cadeia de modificadores
  (após intern/abstract|virtual|override, antes de extern/fn); novo campo `is_static:bool` em `Function`
  (`ast.tks`), threadado nas 3 construções. Atualizar `struct_item_is_method`/`class_item_is_method`
  para pular um `static` opcional no lookahead.
  ```teko
  /**
   * consume_static_modifier — consome a keyword `static` (D3) que marca um método de TIPO (sem
   * receptor), na cadeia de modificadores de método. Só significativa em corpo de tipo; parseada
   * universalmente (inócua no top-level, como pub/exp/intern).
   * @param tokens  o fluxo de tokens
   * @param pos     o índice na (possível) keyword `static`
   * @return        Parsed<bool> — presença de `static` e o índice logo após
   */
  fn consume_static_modifier(tokens: []lexer::Token, pos: u64): Parsed<bool> {
      if !is_kind_at(tokens, pos, lexer::TokenKind::Static) { return Parsed<bool> { node = false; next = pos } }
      Parsed<bool> { node = true; next = pos + 1 }
  }
  ```
- **Back-compat do receptor:** `parse_params`/`parse_function` AINDA aceitam o `self` explícito no slot
  (agora token `SelfKw` na 1ª posição untyped) → mesmo param[0] `has_type=false`, name="self". Mantém o
  corpus pré-sweep construível sob R1.
- **`self` como TIPO** (adendo do dono): `src/parser/parse_type.tks:parse_type_primary` (l.84) aceita
  `SelfKw` como átomo de tipo → um nó `SelfType` (ou `NamedType` de segmento único "self"). Escopo
  mínimo: posição de RETORNO (o parser de retorno já chama `parse_type`); aceitar como átomo geral é
  inócuo e o checker resolve. No checker, `resolve_type` (`resolve.tks:2113`, caso NamedType `:2119`):
  quando o átomo é `self`, resolver para o TIPO ENVOLVENTE — o `ref_ns`/nome-de-tipo do método já
  disponível na resolução de membros (mesma fonte que `receiver_canonical_name` usa). Num `static fn` é
  o próprio `Tipo`; numa instância, o tipo do receptor.
- **`static`-vs-instance durante a janela = FORMA DO PARAM (legado), inalterado** (`typer.tks:1368/5768`
  ficam como estão). `is_static` é ACEITO mas ainda não-autoritativo → `static fn empty()` e `fn empty()`
  se comportam idênticos. Crumb 1 é no-op de comportamento.
- **`base` super (G) + `Tipo::x` estático no checker:** confirmar que a rota de chamada rooted
  (`typer.tks:848-1015`) resolve `Tipo::metodo(...)` para o MEMBRO `static` do tipo (hoje já resolve
  métodos sem receptor; com `is_static` marcado será o mesmo alvo).
  **`base` como super-receptor (aditivo):** ensinar `type_method` (`typer.tks:5918`) a resolver o
  identificador-keyword `base`, num método de INSTÂNCIA de uma classe COM base, para o super — reusando
  EXATAMENTE o mecanismo de reinterpret que hoje serve o binding nomeado (`inject_base_binding`
  5933; `define(local, base_binding_name, base_type)` 5974; o bind-stmt de cast 6010; a vtable base
  1396-1422). A ÚNICA diferença: o alvo do `define`/lookup passa a ser o nome fixo `"base"` em vez de
  `base_binding_name`. Durante a janela, os DOIS coexistem (o binding nomeado antigo e o `base` novo)
  — inerte, pois o corpus tem 0 bindings (§2b). `base` já foi reservado acima (lexer).
- **Rejeitar param de usuário `self`** (D2): em `parse_params`, um `self` fora da posição/forma de
  receptor → `err_at(... "self é o receptor sintético; não pode nomear um parâmetro")`. No-op (0 usos).
- **RITUAL:** build seco sob R0.

**Crumb 2 — RESEED #1 → R1.** Colhe seed que: reserva self/base/static; aceita `static fn`, `self`
explícito (back-compat), `self` de retorno; resolve `base`/`Tipo::x`. Gate: self-reproduce rota C +
provenance + suíte de superfície verde.

### FASE SWEEP-A — marcar os statics (sob R1)

**Crumb 3 — prepender `static` aos ~17 membros static.** Detecção mecânica, token-aware: num corpo de
`struct`/`class`/`trait`, um membro `fn NOME(` cujo 1º token de param é `)` (vazio) OU `nome :` (tipado)
→ prepende `static `. Membros com `self` untyped em [0] NÃO recebem. Byte-comportamento preservado
(forma do param ainda autoritativa em R1). **RITUAL:** build seco sob R1.

### FASE FLIP — injeção sintética vira o default (sob R1) → **RESEED #2 (R2)**

**Crumb 4 — o parser passa a INJETAR `self` sintético para método não-`static`.** Um commit construído
por R1 (o corpus tem statics marcados + instâncias ainda com `self` explícito):
- `parse_function`/`parse_fields`: para um método **não-`static`**, se NÃO houver `self` explícito no
  slot, injetar `Param{name="self"; has_type=false; …}` em param[0]. Como as instâncias correntes ainda
  têm `self` explícito, a injeção não dispara → no-op para o corpus atual.
  ```teko
  /**
   * inject_synthetic_receiver — o receptor sintético `self` (param[0], has_type=false; tipo injetado
   * pelo checker como o tipo envolvente) de um método de INSTÂNCIA (não-`static`) que não escreveu
   * receptor. Ponto único onde o "receptor sintético" da §4 nasce; todo o downstream
   * (receiver_canonical_name/method_func_type/is_instance por forma do param) o consome inalterado.
   * @param params  os params ESCRITOS pelo usuário (sem receptor)
   * @return        params com o `self` sintético em [0]
   */
  fn inject_synthetic_receiver(params: []Param): []Param { … }
  ```
- **O detector do checker NÃO muda:** com a injeção, param[0].has_type=false para instância e ausente/
  tipado para static → `is_instance = params.len>0 && !params[0].has_type` (typer.tks:1368) continua
  correto. (A "inversão" vive só no PARSER: static-kw ⇒ sem injeção; else ⇒ injeção.)
- Seguro agora (não antes) porque, pós-Crumb 3, todo static tem `static` → nenhum `fn semReceptor` sem
  `static` sobrou para a injeção pegar por engano.
- **RITUAL:** build seco sob R1.

**Crumb 5 — RESEED #2 → R2.** Seed que injeta `self` sintético para não-`static` e ainda aceita o `self`
explícito. Gate: self-reproduce rota C + superfície verde.

### FASE SWEEP-B — strip do slot `self` (sob R2)

**Crumb 6 — remover o `self` explícito da lista de params dos 55 métodos de instância.** Token-aware e
seguro (receptor uniformemente `self`): num corpo de tipo, membro `fn NOME(` NÃO-`static`, se o 1º
token de param é `self` seguido de `,`/`)`, apagar `self` (+ `, ` se `,`). Corpo INALTERADO (`self`
segue keyword, resolvido para o param sintético). **RITUAL:** build seco sob R2 (injeção cobre as
instâncias sem receptor).

**Crumb 6b (G) — converter call-sites de super-binding → `base`.** Escopo no `src/`: **ZERO** (§2b —
nenhuma `class Pai(nome)`). O crumb existe para completude e para varrer qualquer FIXTURE que use o
binding: `nome.metodo()` → `base.metodo()`. Precisa de `base` reservado (garantido desde R1); por isso
fica aqui, na janela pós-teach, não no Crumb 0. **RITUAL:** build seco sob R2 (no-op de source no corpus).

### FASE DES-ENSINAR — grafia velha erra (sob R2) → **RESEED #3 (R3)**

**Crumb 7 — remover a aceitação do receptor explícito + o binding nomeado de super (G).** Duas remoções
no mesmo commit de des-ensino, construído sob R2:
- **Receptor:** tirar `allow_untyped_first`/`is_receiver` de `parse_params` e o parâmetro
  `allow_receiver` de `parse_function`/call-sites: um 1º param bare-untyped não é mais aceito; `self`
  escrito no slot vira erro de parse ("o receptor é sintético; use `static fn` para um membro de tipo").
  `parse_discard_param`/paths `_` permanecem.
- **Binding nomeado (G):** em `parse_class_body` (`parse_decl.tks:566-601`), REMOVER o ramo que parseia
  `( bindingName )` após o nome do pai — `class Pai { … }` continua nomeando o pai em `base_name`, mas
  `(nome)` deixa de ser aceito (um `(` ali vira erro: "super é acessado por `base`; não declare um nome
  de binding"). No checker `type_method` (`typer.tks:5918`), remover os parâmetros/ramo
  `has_base_binding`/`base_binding_name`/`inject_base_binding` (o `define`/bind-stmt por nome do binding)
  — o `base`-keyword ensinado no Crumb 1 é agora o ÚNICO caminho de super. Isto constrói sob R2 porque o
  corpus tem 0 bindings.
  - **AST/serialização (minimizar risco):** MANTER os campos `has_base_binding`/`base_binding_name` do
    `ClassBody` (`ast.tks:524-525`) como VESTIGIAIS (sempre `false`/`""`) neste crumb — o parser apenas
    para de PRODUZI-los. Assim NÃO se toca o formato `.tkb` (`tkb_write.tks:424-425`, `tkb_read.tks:784`,
    `tkb_frame.tks:245`) nem as reconstruções de `ClassBody` (`collect.tks:533/1636/1694`,
    `resolve.tks:2704/3329`). A remoção física dos dois campos fica para um W15 pós-§4 (não é gate de
    reseed). Ver risco em §7.
- **RITUAL:** build seco sob R2 (corpus já sem `self` explícito e sem binding → constrói).

**Crumb 8 — RESEED #3 → R3 (final).** Seed da grafia nova pura: `self` sintético; `self` explícito e
receptor bare = erro; `static`/`base` reservados; `self` de retorno; `Tipo::x` estático. Gate final =
self-reproduce rota C + provenance + superfície verde. `gen2==gen3` nativo NÃO é gate aqui (§11).

> **Se o dono optar por 2 reseeds (tolerar a grafia velha):** suprimir os Crumbs 7-8; após o Crumb 6 o
> corpus já fala a grafia nova e o `self` explícito segue *aceito* (não erro). Custo: viola §12.1
> ("superfície removida erra"). Ver DECISÃO E.

## 5. A resolução de `Tipo::metodo()` estático (ponto 3 do pedido)

Métodos continuam coletados IN-BODY (nada muda na coleta — `collect_method_signatures`,
`effective_class_methods`, `sb.methods`/`cb.methods`). `::` é só o operador de chamada. No checker, a
chamada `Circle::unit(args)` chega como `Call` com `callee` = Path de 2+ segmentos; a rota rooted
(`typer.tks:848-1015`) já casa o penúltimo segmento a um TIPO e o último a um método. O ajuste da §4: ao
achar o membro, exigir `is_static` (um método de instância chamado por `::` erra — espelho de
`typer.tks:1369`, que já erra o inverso: instância chamada... na verdade o inverso). Resumo:
- `instancia.metodo(args)` (MethodCall, `.`) → membro **não-`static`** (instância). Erro se `static`.
- `Tipo::metodo(args)` (Call, `::`) → membro **`static`**. Erro se de instância (msg típer.tks:1369).

## 6. Fixtures nativas (entrada → exit) — atualizadas às decisões

Bucket `examples/regressions/syntax` (comportamentais, rota native, 1 cenário/1 rota) e o padrão
compile-fail (`When compilation fails / Then diagnostic = "…"`).

Positivas (exit = valor):
1. **self sintético (instância):**
   `type Circle = struct { raio: i64; fn area(): i64 { self.raio * self.raio * 3 } }`;
   `main`: `var c = Circle{raio=6}; c.area()` → exit 108. (chamada por `.`)
2. **fábrica static devolvendo `self`** (SEM ctor inventado — só monta o LITERAL):
   `type Circle = struct { raio: i64; static fn make(): self { Circle { raio = 7 } } }`;
   `main`: `Circle::make().raio` → exit 7. (chamada por `::`; `: self` = Circle)
3. **fábrica static com params + literal:**
   `static fn scaled(n: i64): self { Circle { raio = n } }`; `Circle::scaled(9).area()`... → valida args.
4. **base super:** `virtual class B { virtual fn r(): i64 { 1 } }` +
   `class S(B) { override fn r(): i64 { base.r() + 1 } }`; `main`: `var s = S{}; s.r()` → exit 2.
5. **static não recebe self indevido:** `static fn zero(): i64 { 0 }` num tipo; `T::zero()` → exit 0
   (garante que a injeção NÃO pegou o static).
6. **`self` de retorno num método de INSTÂNCIA:** `fn twin(): self { Circle { raio = self.raio } }`;
   `c.twin().raio` → devolve o mesmo raio (self-retorno = tipo do receptor).

Compile-fail (diagnóstico pinado):
7. **param de usuário `self`:** `fn m(self: i64)` → "self é o receptor sintético…".
8. **`base` como identificador (pós-reserva):** `var base = 3` → erro de parse (keyword reservada).
9. **receptor bare / self explícito (pós-des-ensino):** `struct { fn a(self) {…} }` ou `fn a(c) {…}` →
   erro ("receptor é sintético").
10. **instância chamada por `::` / static por `.`:** reusar típer.tks:1369.

## 7. Riscos / armadilhas

- **Sweep de `base` (~30 arquivos) — o maior risco.** É rename por-escopo em 3 classes (local/param/
  campo+acessos). Armadilhas: (a) colisão com um `base_`/`sub` já existente no mesmo escopo — checar
  antes; (b) `.base` é acesso a CAMPO — renomear declaração e acessos JUNTOS, por-struct, senão quebra a
  resolução de campo; (c) não confundir o identificador `base` com as STRINGS `base_name`/
  `base_binding_name` do AST (mecanismo de super — NÃO tocar); (d) fazer TUDO num único Crumb 0 sob R0
  antes de reservar, senão o corpus deixa de construir. Recomenda-se um passe token-aware (não regex
  cego) que respeite escopo de binding.
- **A inversão static⇄instance** força a ordem marca(Crumb 3)→injeta(Crumb 4)→strip(Crumb 6); qualquer
  troca de ordem quebra um reseed (o build seco em cada fase é a rede).
- **`self` como tipo** fora de retorno: manter o escopo mínimo (retorno/fábricas). Aceitar `self` como
  átomo de tipo em posição arbitrária é inócuo no parser, mas o checker só sabe resolvê-lo onde há tipo
  envolvente (corpo de método); um `self` de tipo no top-level deve ERRAR ("`self` só é um tipo dentro
  de um método").
- **Byte-fixpoint:** cada Crumb de TEACH/FLIP é construído como no-op de comportamento sobre o corpus
  do momento (marca antes de injetar; injeta antes de strip), preservando o gate self-reproduce.
- **Sem construtor inventado — CONFIRMADO.** Nenhuma keyword `constructor`/`init`/`new`; fábrica é só
  `static fn … { TipoLiteral{…} }`; o objeto nasce do literal de struct. `self`-retorno é açúcar de
  tipo, não um construto de ctor.
- **G — o super-binding tem 0 uso, mas a maquinaria é larga.** O `base_binding_name` é lido em ~9 sítios
  (parser, checker type_method, collect/resolve reconstruções, tkb_read/write/frame). O des-ensino
  (Crumb 7) só REMOVE a produção (parser) e o CONSUMO ativo (checker), deixando os campos AST inertes —
  não tocar a serialização evita um blast-radius de format-change no meio da onda. Rasgar os campos =
  W15 pós-§4. Armadilha: garantir que `type_method` não fique com parâmetros órfãos (assinatura
  5918 + call-site 6452 mudam juntos).
- **Twins C FROZEN:** trabalho só em `.tks`; o reseed usa o binário anterior.

## 8. DECISÕES — TODAS FECHADAS PELO DONO

- **D1** — `base` RESERVADO (Crumb 0 varre ~30 arquivos antes de reservar). ✓
- **D2** — `self` reservado, SEM slot (receptor sintético); param de usuário `self` = erro. ✓
- **D3** — método SÓ in-body; `::`/`.` são operadores de chamada; `static` marca membro de tipo. ✓
- **`self` de retorno** = tipo envolvente; **fábrica** = `static fn` com literal de struct; sem ctor. ✓
- **E** — **3 reseeds**, mantendo §12.1 (superfície removida ERRA). Sem compressão para 2. ✓
- **F** — rename de `base` com **nomes SIGNIFICATIVOS por sítio** (escolha do implementador; não `base_`
  cego). ✓
- **G** — `base` **SUBSTITUI** o binding nomeado de super (um jeito só). O binding `class Pai(nome)` +
  `nome.metodo()` deixa de existir; super é sempre `base`. Escala do sweep de call-sites: **0** no corpus
  (14 classes, nenhuma com base-binding). ✓

**Nada em aberto.** Próximo passo é a implementação seguindo os Crumbs 0→8 (3 reseeds: R1 após o teach,
R2 após a injeção sintética, R3 após o des-ensino).
