---
section: design
created: 2026-09-03
status: DESIGN — plano executável (implementer Opus) do gap que HALTou o Degrau 1 de
        `ir-builders-chunked-redesign-0.3.1.md`. Escopo: instanciar+despachar um genérico
        CONCRETO (não-phantom) cross-namespace — `teko::collections::Chunked<LInst>::make()`
        e `inst.method(...)` chamados de `teko::lir`. Prerequisite de Degrau 1; NÃO redesenha
        os Degraus 1-6 (esses seguem como documentados).
---

# Cross-namespace concrete generic instance dispatch (0.3.1)

## 0. PASSO 0 — base confirmada

`git fetch origin fix/retirement && git checkout -B arch-crossns-generic origin/fix/retirement`.
HEAD = `74dbf0a5` (`fix(checker): resolve cross-namespace generic static factory calls`, 2026-09-03).
Canário `BindKind = enum { Var; Const }` presente (`src/checker/comptime_fold.tks:1234` e outros usam
`parser::BindKind`). `src/collections/chunked.tks` presente (Degrau 0 landado — commit `928f37a4`).
Base correta, NÃO é o clone de julho.

`74dbf0a5` já corrigiu a METADE do problema: `retarget_generic_static_callee` (`typer.tks:1800-1807`)
agora usa `path_prefix_through` para preservar o prefixo de namespace (`teko::collections::`) ao
reescrever o segmento-dono para o nome mangled da instância — sem essa correção, o qualificador virava
`Chunked__g__LInst` bare, perdendo `teko::collections::`. Esse fix é necessário mas **não suficiente**:
o gate seguinte em `type_call` ainda rejeita instâncias concretas. É esse gate — e um segundo, irmão,
em `type_method_call` — que este documento fecha.

## 1. O diagnóstico exato (auditado, arquivo:linha)

### 1.1 O coração: `name_is_phantom_instance` é usado como o gate de roteamento, mas só reconhece phantom

`typer.tks:1436-1452` — `name_is_phantom_instance(name, table)` faz split do nome mangled
(`Chunked__g__LInst`) em tokens por `_` e retorna `true` só se **pelo menos um** token corresponde a um
`TypeDecl` que é `is_type_param_decl` (i.e., um type PARAMETER, não um tipo concreto). Para
`Chunked__g__LInst` nenhum token (`Chunked`, `g`, `LInst`) é um type-param — retorna `false`.

Dois call-sites usam esse gate como a ÚNICA porta para o roteamento via TEMPLATE
(`type_phantom_instance_call`, que resolve o factory/método substituindo os type-params do template
pelos argumentos — SEM depender de `lookup_call`/`env`):

- **`typer.tks:2092`** (`type_call`, static factory `Owner<Args>::method()`):
  ```
  if c.callee.segments.len >= 2 && name_is_phantom_instance(c.callee.segments[c.callee.segments.len - 2].name, table) {
      return type_phantom_instance_call(c, env, table)
  }
  ```
- **`typer.tks:1651`** (`type_method_call`, instance dispatch `recv.method()` quando `recv`'s tipo é uma
  instância genérica):
  ```
  match recv_t {
      Named as rn => {
          if name_is_phantom_instance(rn.name, table) {
              var psegs = method_dispatch_callee(rn.name, mc.method).segments
              var pcargs: []parser::Expr = [mc.receiver, ..mc.args]
              return type_call(parser::Call { callee = parser::Path { segments = psegs }; args = pcargs; arg_names = method_call_arg_names(mc); owner_type_args = []; callee_type_args = mc.type_args }, null, env, table)
          }
      }
      _ => { }
  }
  ```

Para `Chunked__g__LInst` (100% concreto), ambos os gates dão `false` e o código cai no caminho normal
(`lookup_call(env, c.callee)` em `type_call`, `type_table_find`+`decl.body.methods` em
`type_method_call`).

### 1.2 Por que o caminho "normal" também falha para o caso fully-qualified

O caminho normal PODE funcionar (achei evidência de que funciona hoje para chamadas CONCRETAS
BARE-qualificadas, ex. `Chunked<LInst>::make()` dentro do próprio `teko::collections` ou via `use`) —
mas quebra especificamente para a forma FULLY-QUALIFIED cross-namespace
(`teko::collections::Chunked<LInst>::make()`), que é a forma que `teko::lir` precisa usar:

- `register_one_instance_methods` (`collect.tks:278-292`) registra os métodos de TODA instância
  concreta stampada (`table_generic_instance_regs`, `resolve.tks:2061-2074`) em `env` com
  `ns = inst.name` — **bare**, ex. `"Chunked__g__LInst"` — SEM o prefixo `teko::collections::` (ao
  contrário de `type_member_sig`, `collect.tks:224-225`, que usa `item.namespace ~ "::" ~ td.name`
  para tipos DECLARADOS normalmente).
- `lookup_call`/`call_binding_matches` (`scope.tks:139-143`) casam o binding via
  `qualifier_selects_ns(b.ns, path_qualifier(callee, callee.segments.len-1))`
  (`resolve.tks:229-232`): `ns==qual` OU `ns.ends_with("::"~qual)`. Para a chamada fully-qualified, o
  `qual` da call site é `"teko::collections::Chunked__g__LInst"` (3 segmentos), mas `b.ns` é
  `"Chunked__g__LInst"` (bare) — **nem igual, nem sufixo** (o `ns` é mais curto que o `qual`) →
  NÃO casa. `lookup_call` retorna `error` → `"unknown function: make"`.

Ou seja: mesmo achando o gate certo, o caminho "normal" tem um bug de qualificação DIFERENTE e
adjacente em `register_one_instance_methods`. **Não precisamos consertá-lo** — a rota via TEMPLATE
(`type_phantom_instance_call`) não usa `lookup_call`/`env` NENHUMA vez; ela resolve direto contra
`template.decl` (achado por `find_generic_template`, `resolve.tks:2045-2059`) + `Subst`. Rotear
CONCRETO por ali também contorna esse segundo bug, sem tocá-lo.

### 1.3 Por que `phantom_owner_subst`/`resolve_phantom_arg_token` já servem para tokens CONCRETOS

`type_phantom_instance_call` (`typer.tks:1859-1904`) computa o `Subst` via `phantom_owner_subst`
(`typer.tks:1827-1845`), que faz `split_phantom_args` no sufixo mangled e resolve CADA token via
`resolve_phantom_arg_token` (`typer.tks:1847-1857`):
```
fn resolve_phantom_arg_token(tok: str, table: TypeTable, ref_ns: str): @Type() | error {
    match resolve_type(parser::NamedType { path = single_seg_path(tok); args = [] }, table, ref_ns) {
        (@Type()) as t => return t
        error => { }
    }
    var qualified = qualified_type_name_by_last(tok, table)
    if teko::runtime::str_eq(qualified, tok) {
        return error { message = $"cannot resolve generic factory owner argument '{tok}'" }
    }
    Named { name = qualified }
}
```
Isso JÁ resolve tanto um token type-param (`"T"`, phantom) quanto um token de tipo concreto
(`"LInst"`, via `resolve_type` bare relativo a `ref_ns` = `env.cur_ns`, o namespace do CALLER — exatamente
onde `LInst` mora). `type_mangle` (`resolve.tks:1398-1414`) usa `name_last_segment` para `Named` — o
token mangled NUNCA carrega qualificador, então `resolve_phantom_arg_token` sempre resolve relativo ao
caller, que é o comportamento certo aqui (o caller de `Chunked<LInst>::make()` está em `teko::lir`, onde
`LInst` é visível bare). **Nenhuma mudança é necessária em `phantom_owner_subst`/
`resolve_phantom_arg_token`** — já são genéricos o bastante; só o GATE que os precede é estreito demais.

Limitação pré-existente e FORA de escopo (não bate no caso `Chunked<LInst>`, que não tem argumento
genérico aninhado): `phantom_owner_subst` erra com "a nested generic type-argument is not supported
here" se um token do sufixo mangled contiver, ele mesmo, outro `__g__` (ex. `Chunked<Foo<Bar>>`), porque
`split_phantom_args` corta cegamente em `__`. Registrar, não resolver aqui.

### 1.4 Por que a monomorfização (emissão do corpo concreto) já é genérica — não precisa de mudança

A trilha DOWNSTREAM (gerar o `TFunction` mangled de `make`/`push`/etc. para `Chunked__g__LInst`, com
corpo já substituído `T→LInst`, pronto pro codegen) roda por
`monomorphize` (`monomorph.tks:1078`) → `stamp_all_instance_methods` (`monomorph.tks:1041-1055`) →
`table_generic_instances(table)` (`resolve.tks:2076-2088`, que devolve TODA instância concreta stampada
em `table`, `type_params.len==0`) → `stamp_instance_methods`/`stamp_one_instance_method`
(`monomorph.tks:1014-1039`). Esse laço **não depende do call-site nem do caller ser genérico** — ele
varre `table_generic_instances(table)` inteiro e emite os métodos de TODA instância lá stampada,
sempre. A stampagem em si (`instantiate_types`, `resolve.tks:2003-2029`, disparada de `collect.tks:1677/
1681` e de novo em `typer.tks:5810` antes do type-check) roda sobre TODO `NamedType` com `args.len>0`
descoberto por `seed_inst_sites`/`scan_*_insts` (`resolve.tks:1750-1942`) — incluindo `var x:
teko::collections::Chunked<LInst>` (posição de TIPO, sempre corretamente qualificado porque
`scan_texpr_insts` encaminha o `NamedType` tal como o parser produziu, sem reconstruir o `Path`) e
CALL sites com `owner_type_args` (via `scan_call_site_args`, ver §1.5).

**Conclusão:** a maquinaria de monomorfização de tipo genérico concreto — stampagem, emissão de método
substituído, nome mangled — já é 100% genérica e correta hoje; ela não sabe (nem precisa saber) se a
instância nasceu de um `var` tipado ou de uma call site. O gap inteiro está no TYPE-CHECK do call-site
(§1.1) mais um bug de robustez adjacente na DESCOBERTA por call-site (§1.5). **Não se propõe nenhuma
mudança em `monomorph.tks`.**

### 1.5 Gap #4 — `scan_call_site_args` tem o MESMO bug que `retarget_generic_static_callee` tinha antes de `74dbf0a5`

`resolve.tks:1817-1822`:
```
fn scan_call_site_args(call: parser::Call, ref_ns: str, out: ref []InstSite, idx: u64): u64 {
    if call.owner_type_args.len == 0 { return idx }
    if call.callee.segments.len < 2 { return idx }
    var owner_name = call.callee.segments[call.callee.segments.len - 2].name
    scan_site_args(parser::Path { segments = [parser::Segment { name = owner_name }] }, call.owner_type_args, ref_ns, out, idx)
}
```
Constrói um `Path` BARE de um segmento só (`[Chunked]`), descartando `teko::collections::`. Isso alimenta
`stamp_inst_site` (`resolve.tks:2018-2029`) → `generic_template_reg` (`resolve.tks:1958-1975`), que
TENTA primeiro `resolve_type_reg` qualificado e cai num fallback LENIENTE (`tt_cands(table, name)` +
match só por `name_last_segment(r.name)==name`, ignorando namespace) se o qualificado falhar. Com o
`Path` bare, o fallback SEMPRE é quem resolve — funciona "por sorte" enquanto só existir UM tipo
genérico chamado `Chunked` no programa inteiro, mas é uma AMBIGUIDADE LATENTE: se `teko::lir` (ou
qualquer outro namespace) algum dia declarar seu PRÓPRIO tipo genérico `Chunked<T>`, o fallback
bare pode casar o ERRADO silenciosamente — instância mal-stampada, sem erro de compilação, corpo de
método errado. Este é EXATAMENTE o bug que `74dbf0a5` consertou em `retarget_generic_static_callee`
(`typer.tks:1800-1807`, que já usa `path_prefix_through`); `scan_call_site_args` é a contraparte no
lado da DESCOBERTA (`resolve.tks`) que ficou pra trás. Fixture `chunked_crossns_collision` (§3.2) prova
o buraco e a correção.

## 2. O fix — 3 edições, zero máquina nova

Lei aplicável (`CLAUDE.md`, "o roteador de superfície já existe"): a resposta correta a um gate estreito
demais não é inventar uma segunda rota — é alargar o gate pra reusar a rota que já existe e já é
suficientemente genérica (§1.3, §1.4). As três edições abaixo são as ÚNICAS mudanças necessárias.
Nenhuma tem `exp`/é superfície stdlib → **nenhuma leva doc-comment** (lei de estilo: doc só onde há
`exp`).

### 2.1 `typer.tks:2092` — `type_call`, alargar o gate do factory estático

De:
```
    if c.callee.segments.len >= 2 && name_is_phantom_instance(c.callee.segments[c.callee.segments.len - 2].name, table) {
        return type_phantom_instance_call(c, env, table)
    }
```
Para:
```
    if c.callee.segments.len >= 2 && name_is_g_instance(c.callee.segments[c.callee.segments.len - 2].name) {
        return type_phantom_instance_call(c, env, table)
    }
```
(`name_is_g_instance(name: str): bool`, `resolve.tks:2032-2041`, já `pub`, já não recebe `table` —
assinatura mais simples que a que substitui.)

### 2.2 `typer.tks:1651` — `type_method_call`, alargar o gate do instance dispatch

De:
```
    match recv_t {
        Named as rn => {
            if name_is_phantom_instance(rn.name, table) {
```
Para:
```
    match recv_t {
        Named as rn => {
            if name_is_g_instance(rn.name) {
```
(resto do bloco — `psegs`, `pcargs`, o `return type_call(...)` — inalterado.)

### 2.3 `resolve.tks:1817-1822` — `scan_call_site_args`, preservar o qualificador (mesma técnica de `74dbf0a5`)

De:
```
fn scan_call_site_args(call: parser::Call, ref_ns: str, out: ref []InstSite, idx: u64): u64 {
    if call.owner_type_args.len == 0 { return idx }
    if call.callee.segments.len < 2 { return idx }
    var owner_name = call.callee.segments[call.callee.segments.len - 2].name
    scan_site_args(parser::Path { segments = [parser::Segment { name = owner_name }] }, call.owner_type_args, ref_ns, out, idx)
}
```
Para:
```
fn scan_call_site_args(call: parser::Call, ref_ns: str, out: ref []InstSite, idx: u64): u64 {
    if call.owner_type_args.len == 0 { return idx }
    if call.callee.segments.len < 2 { return idx }
    var owner_idx = call.callee.segments.len - 2
    scan_site_args(path_prefix_through(call.callee, owner_idx), call.owner_type_args, ref_ns, out, idx)
}
```
`path_prefix_through` (`typer.tks:1789-1798`) é um `fn` bare (não-`pub`) — visível de `resolve.tks`
porque os dois arquivos vivem no MESMO namespace `teko::checker` (diretório `src/checker/`, `teko.tkp`
`source="src"` — namespace = caminho relativo ao source root). Não precisa de `pub`/import; é a MESMA
técnica que `retarget_generic_static_callee` já usa. Zero função nova.

### 2.4 O que NÃO muda (verificado, não tocar)

- `monomorph.tks` inteiro (§1.4).
- `phantom_owner_subst`/`resolve_phantom_arg_token`/`type_phantom_instance_call` (§1.3) — corpo
  inalterado, só passam a ser alcançados por mais chamadas.
- `name_is_phantom_instance` (`typer.tks:1436`) — continua viva e necessária no seu terceiro call-site,
  `typer.tks:2802` (`explicit_inst_target`, construção de struct-literal `Name<Args> { … }` — feature
  DIFERENTE, não é call/method-dispatch; ali a distinção phantom-vs-stampada-vs-inválida importa de
  verdade, porque o caminho já tenta `type_table_find` primeiro e só cai no phantom como EXPLICAÇÃO do
  "não achei"). **Não remover, não generalizar esse terceiro site.**
- `register_one_instance_methods`/`env` bindings bare (§1.2) — o bug de qualificação ali seguirá
  existindo, mas fica INALCANÇÁVEL para dispatch de instância genérica depois de 2.1/2.2 (toda chamada
  cujo dono é `name_is_g_instance` agora é interceptada ANTES de `lookup_call`). Ver §5 (achado
  adjacente, reportar, não consertar aqui).

## 3. Oráculos isolados (lei: só o self-build NÃO exercita ainda → `.tkr` isolado autorizado)

Hoje `grep "Map<\|Dictionary<\|Chunked<\|::make()" src/checker src/lir src/backend` = 0 — nenhum
consumidor real no `src/` usa instanciação genérica CONCRETA cross-namespace ainda (Degrau 1 é o
primeiro). Os dois fixtures abaixo são os ÚNICOS autorizados por este crumb — mesmo padrão do
`chunked_grow.tkr` do Degrau 0: **temporários**, removidos assim que Degrau 1+ (`LBlock.insts:
Chunked<LInst>`) passar a exercitar o mesmo caminho pelo self-build.

### 3.1 `chunked_crossns` — prova a capacidade fim-a-fim

`examples/regressions/chunked_crossns/chunked_crossns.tkp`:
```
name = "chunked_crossns"
source = "src"

[artifact]
kind = "binary"
```

`examples/regressions/chunked_crossns/main.tks`:
```
exit(caller::run())
```

`examples/regressions/chunked_crossns/src/caller/caller.tks` (namespace `caller` — distinto de
`teko::collections`, mimicando `teko::lir`; `Rec` mimicando `LInst`, declarado no MESMO namespace que
chama, como no caso real):
```
type Rec = struct { a: i64; b: i64 }

pub fn run(): i64 {
    var q = teko::collections::Chunked<Rec>::make()
    q.push(Rec { a = 1; b = 2 })
    q.push(Rec { a = 3; b = 4 })
    q.push(Rec { a = 5; b = 6 })
    if q.len() != 3 { return 1 }
    if q.is_empty() { return 2 }
    var r0 = q.get(0)
    if r0.a != 1 { return 3 }
    if r0.b != 2 { return 3 }
    var f = q.first()
    if f.a != 1 { return 4 }
    var l = q.last()
    if l.a != 5 { return 5 }
    var it = q.iter()
    var sum: i64 = 0
    loop {
        match it.next() {
            Rec as r => sum = sum + r.a + r.b
            null => break
        }
    }
    if sum != 21 { return 6 }
    0
}
```
`main.tks` (namespace raiz) NÃO qualifica `caller::run` com `use` — chamada fully-qualified, igual ao
caso real. `q = teko::collections::Chunked<Rec>::make()` é escrita SEM anotação de tipo no `var`
(inferido) — de propósito: a ÚNICA descoberta de instanciação nesse ponto é a própria call site
(`scan_call_site_args`, §1.5/§2.3), não uma posição de tipo em outro lugar do arquivo. Cobre: static
factory cross-ns fully-qualified (`make`), instance dispatch cross-ns (`push`/`len`/`is_empty`/`get`/
`first`/`last`/`iter`/`next`), narrowing de `T|null` sobre `T` = struct concreto.

`examples/regressions/chunked_crossns/chunked_crossns.tkr`:
```
Feature: cross-namespace concrete generic instance dispatch — teko::collections::Chunked<T> called from a user namespace

  Scenario: caller (a distinct namespace) statically instantiates Chunked<Rec>::make() fully-qualified and dispatches every instance method (push/len/is_empty/get/first/last/iter/next) cross-namespace
    When the program is built and run
    Then it exits 0
```
Fixtures de falha (inputs → exit nativo esperado): qualquer regressão nos 6 `return N` acima FALHA com
o `N` correspondente em vez de 0 — cada `return` isola um método diferente do contrato, útil pro
implementer localizar qual método regrediu se o oráculo falhar.

### 3.2 `chunked_crossns_collision` — prova que o fix de §2.3 tem dentes (não só "funciona por sorte")

Sem o fix de §2.3, o fallback lenient de `generic_template_reg` pode casar um `Chunked<T>` LOCAL do
caller em vez do `teko::collections::Chunked<T>` pedido explicitamente — corrupção silenciosa
(compila, mas o corpo errado). Este fixture cria exatamente essa colisão:

`examples/regressions/chunked_crossns_collision/chunked_crossns_collision.tkp`:
```
name = "chunked_crossns_collision"
source = "src"

[artifact]
kind = "binary"
```

`examples/regressions/chunked_crossns_collision/main.tks`:
```
exit(caller::run())
```

`examples/regressions/chunked_crossns_collision/src/caller/caller.tks` — declara um `Chunked<T>` LOCAL
com um `make()` que devolve um sentinela reconhecível (`len()` fixo e diferente de 0), e chama a
`teko::collections::Chunked<i64>` FULLY-QUALIFICADA:
```
type Chunked<T> = class {
    pub static fn make(): Chunked<T> { .{ } }

    pub fn len(): u64 { 999 }
}

pub fn run(): i64 {
    var q = teko::collections::Chunked<i64>::make()
    if q.len() != 0 { return 1 }
    q.push(7)
    if q.len() != 1 { return 2 }
    if q.get(0) != 7 { return 3 }
    0
}
```
Se `scan_call_site_args` ainda descartar o qualificador, `caller::Chunked` (que não tem `push`/`get`)
faria a chamada `.push(7)` FALHAR NO TYPE-CHECK — ou, se o fallback ainda assim casar
`teko::collections::Chunked` por acidente de ordem de `tt_cands`, o teste é frágil; por isso o
`len()==999` no PRIMEIRO `if` é o discriminador robusto (dispara ANTES de `push` ser sequer tentado):
sem o fix, `q.len()` pode devolver `999` (o `Chunked` local) → `exit(1)`; com o fix, sempre resolve
`teko::collections::Chunked` → `len()==0` → segue e sai `0`.

`examples/regressions/chunked_crossns_collision/chunked_crossns_collision.tkr`:
```
Feature: scan_call_site_args preserves the namespace qualifier of a generic static-factory owner

  Scenario: a caller-local type also named Chunked<T> must NOT shadow the fully-qualified teko::collections::Chunked<T> requested at the call site
    When the program is built and run
    Then it exits 0
```

Ambos os fixtures: **remover quando Degrau 1+ do `ir-builders-chunked-redesign-0.3.1.md` passar a
exercitar `Chunked<LInst>` pelo self-build** (mesma regra do `chunked_grow.tkr` do Degrau 0, mesmo
documento §5).

## 4. Ordem de crumbs

Um crumb único é suficiente (as 3 edições são pequenas, correlatas, e um único fixpoint+oráculo valida
as três — "menos build, mais código"); pode ser splitado em 2 se o implementer preferir granularidade
extra (2.1+2.2 num commit, 2.3 + o fixture de colisão noutro), mas NÃO é necessário.

**Crumb ÚNICO — "checker: route concrete cross-namespace generic instance calls through the template
(unblocks IR-builders Degrau 1)":**
1. Editar `typer.tks:2092` e `typer.tks:1651` (§2.1, §2.2).
2. Editar `resolve.tks:1817-1822` (§2.3).
3. Build gen0-do-seed-commitado (`bootstrap/teko.c`, `CC=clang scripts/build_gen1_from_c.sh` —
   NUNCA `fetch_teko.sh`), compilar o tip: **fixpoint gen2.c==gen3.c byte-idêntico**. Verificação
   EXTRA (barata, específica deste crumb): como nenhum consumidor em `src/` exercita ainda o caminho
   novo, o `gen2.c` pós-fix deve ser **byte-idêntico ao `gen2.c` pré-fix** (diff vazio) — se não for,
   algo no `src/` já dependia (silenciosamente) do comportamento antigo do gate; investigar antes de
   prosseguir.
4. Com o compilador recém-buildado (gen1 pós-reseed, ou gen2 — qualquer geração do fixpoint serve),
   compilar+rodar `chunked_crossns` e `chunked_crossns_collision` (§3.1, §3.2) — ambos saem `0`.
5. Rodar os 3 harnesses C standalone (`scripts/*_test.sh`) — não deveriam ser afetados (nenhuma mudança
   toca `teko_rt.{c,h}`/`assert.*`), mas o verificador confirma mesmo assim (D185).
6. Reseed incondicional (`bootstrap/teko.c`), commit+push na branch `arch-crossns-generic`.
7. Verificador independente (2º agente, D164/D166): gen0-do-seed-COMMITADO builda o tip do zero +
   fixpoint confirmado — nunca na palavra do implementer.

**Ritual point:** este crumb é o ÚNICO ritual point próprio — ao fechar verde, ele DESBLOQUEIA
diretamente o Degrau 1 de `docs/design/ir-builders-chunked-redesign-0.3.1.md` (`LBlock.insts:
Chunked<LInst>`), que segue como já documentado ali (§5 daquele doc), sem nenhuma alteração de plano.
Não se propõe outro ritual point aqui — os Degraus 1-6 têm o seu próprio gate (fixpoint por degrau,
D210 pro pico native).

## 5. Riscos + achados adjacentes (reportar, não consertar aqui)

- **R1 (baixo, oráculo-verificado):** a inferência de `Subst` por unificação de argumentos em
  `mono_call_subst` (`monomorph.tks:408-442`) fica vazia para um factory de ZERO argumentos (`make()`)
  quando chamado de um caller NÃO-genérico — mas o binding real (`T→LInst`) vem de
  `instance_method_subst_l5`/`instance_method_subst` (`monomorph.tks:1005-1012`, derivado do PRÓPRIO
  `idecl` stampado, não da unificação de args) e é composto por cima (`compose_method_subst`,
  `monomorph.tks:464-473`) — deveria funcionar independente de aridade. Não constatei um bug aqui por
  leitura estática; o oráculo `chunked_crossns` (que chama `make()` com zero args) é exatamente o teste
  que confirma isso na prática antes de tocar os IR-builders. Se falhar, o ponto de ataque é
  `instance_method_subst`/`instance_method_subst_l5`, NÃO os 3 gates deste doc.
- **R2 (achado adjacente, reportar — NÃO consertar neste crumb):** `register_one_instance_methods`
  (`collect.tks:278-292`) registra bindings de método de instância genérica com `ns` BARE
  (`inst.name`), inconsistente com `type_member_sig` (`collect.tks:224-225`, que usa
  `namespace ~ "::" ~ name`). Depois de §2.1/§2.2 essa rota fica inalcançável para dispatch de
  instância genérica (interceptada antes por `name_is_g_instance`), mas o bug em si permanece — pode
  morder outro consumidor de `lookup_call_candidates`/overload selection que eu não auditei
  exaustivamente. Reportar para o dono/próxima varredura de "qualificação de binding", não é bloqueador
  do Degrau 1.
- **R3 (achado adjacente, reportar):** `type_phantom_instance_call` (`typer.tks:1859-1904`) não faz
  NENHUMA checagem de `member_accessible`/visibilidade (ao contrário do caminho normal em `type_call`
  linhas 2154-2164 e `type_method_call` linhas 1696-1701). Isso já era verdade pro caso phantom
  (pré-existente); depois deste fix passa a valer TAMBÉM pro caso concreto — um método `pub` (não
  `exp`) de um tipo genérico fica alcançável cross-namespace sem enforcement de privacidade. Não é
  regressão introduzida por este crumb (era um buraco pré-existente, só não alcançado por instâncias
  concretas antes) e consertar aqui arriscaria travar o próprio caso de uso que este crumb desbloqueia
  (o modelo de membro OO ainda não existe — D196, "não implementado ainda"). Reportar para quando D196
  for implementado.
- **R4 (achado adjacente, reportar):** `type_phantom_instance_call` escolhe o método por PRIMEIRO NOME
  IGUAL (`typer.tks:1871-1879`), sem desambiguação de overload — genéricos com métodos sobrecarregados
  não são suportados por essa rota. `Chunked<T>` não tem overloads (irrelevante aqui), mas um genérico
  futuro que overload FALHARIA silenciosamente pegando o candidato errado. Registrar para quando surgir
  um genérico com overload real.
- **Tensão de lei:** nenhuma. A resolução (§2) é law-first — reusa o roteador (`type_phantom_instance_call`)
  em vez de inventar uma segunda rota para instância concreta, exatamente como as leis "o roteador de
  superfície já existe" / "zero exceção pro que o Teko sabe interpretar" mandam. Não há HALT.

## 6. Veredito — a feature é MENOR do que pareceu

O diagnóstico original (`type_call` "só roteia o factory pelo TEMPLATE quando o owner é phantom
instance... precisa rotear pelo template como `type_phantom_instance_call`") já apontava a direção
certa. A investigação confirma que **NÃO é preciso nenhuma máquina nova**: nem em `monomorph.tks`
(já genérico o bastante, §1.4), nem em `phantom_owner_subst`/`resolve_phantom_arg_token` (já resolvem
tokens concretos, §1.3). O gap inteiro fecha com **duas trocas de nome de função num `if` (2.1, 2.2)
mais uma troca de `Path` bare por `path_prefix_through` (2.3)** — ~6 linhas líquidas em 2 arquivos,
reusando helpers que já existem (`name_is_g_instance`, `path_prefix_through`). O "coração" do problema
era genuinamente um gate estreito demais, não uma lacuna de design. O trabalho pesado (stampagem +
monomorfização de instância concreta, §1.4) já estava construído e testado pelo caminho de posição-de-
tipo (`var x: Chunked<LInst>`); faltava só ligar o mesmo cano ao caminho de call-site.
