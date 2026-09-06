# Plano — §7: Injeção de dependência `service`/`svc` com lifetimes de arena (RE-DESIGN)

> **Status:** DESIGN. Read+design (nenhum código de produto, nenhum reseed). Este documento é O
> ARTEFACTO; o único commit é ele próprio.
> **Fonte de lei:** `mudancas-superficie-0.3.1.md` §7 (SAI/ENTRA/RESOLVE/exemplo). Backend do
> lifetime-de-arena: `arena-especificacao-unica-0.3.1.md` (Doc 1) §8 — a COSTURA. Constraint
> `T: IService` / `singleton & IService`: §9.2b (tratada abaixo, mínimo-agora). Precedente de
> intrínseco comp-time reconhecido-no-typer sem gramática nova: §5 (`plano-secao5-marshall.md`,
> `type_ptr_wrap`/`type_ptr_unwrap` + a costura `emit_ptr_wrap_guard`). Precedente de
> remoção-de-superfície-preservando-o-mecanismo: §6 (`plano-secao6-aposentar-unsafe.md`).
> **Branch:** `fix/retirement` @ `b2d73949` (generics completos, §1–§6, bare block, delegados
> func/action, `void` aposentado — base verde).
>
> **PORQUE ISTO É UM RE-DESIGN.** Uma implementação anterior do §7 foi PERDIDA (um reset de
> working-tree). A superfície `service`/`svc` NUNCA foi lançada — o corpus self-host de hoje só tem a
> DI-anotação antiga (`src/checker/di.tks`, `DiKind`, `di_kind`), medida presente. Além da perda, o
> dono RULOU correções que SUPERSEDEM o plano velho, e o caminho de chave-em-runtime (Path 2)
> SIGSEGV'ou o compilador (`FATAL signal — a generated program crashed (M.1)`). Este plano dobra TODOS
> os rulings numa desenho coerente e implementável que CONSERTA a crash na RAIZ (sem honest-stop).
>
> **RULINGS DO DONO INCORPORADOS (LEI — desenha-se À VOLTA delas):**
> 1. **Sintaxe `type Nome = kind {}` uniforme.** O `service Clock singleton {}` nu do plano velho era
>    uma ANOMALIA. Serviço é `type Clock = service singleton { … }`, onde `service` é um KIND (como
>    class/struct/interface) e `singleton`/`scoped`/`transient` é um MODIFICADOR de lifetime sobre o
>    kind (análogo a `type C = virtual class {}`). A implementar uma interface:
>    `type EnHi = service singleton & G { … }`.
> 2. **Um serviço auto-conforma a uma interface sintetizada `IService`.** O compilador SINTETIZA
>    `exp type IService = interface { exp static ctor(): self }`. Isto exige DUAS capacidades novas de
>    contrato-de-interface (hoje interfaces são só métodos-de-instância): **(a) contrato de método
>    `static`**; **(b) `self` como tipo-de-retorno num contrato** (= devolve uma instância do tipo
>    implementador).
> 3. **Assinaturas dos intrínsecos:** `svc<T: IService>(key: str | null = null): T` (chave OPCIONAL:
>    null = por tipo; str constante = por nome; str de runtime = service-locator);
>    `has_svc<T: IService>(key: str): bool` (chave OBRIGATÓRIA). Nome `has_svc` (o estabelecido).
> 4. **FORK da conformância-de-interface: RESOLVIDO PELO DONO = opção (a).** Uma interface de
>    utilizador participa da resolução ESTENDENDO IService: `type G = interface & IService { … }`. Ver §3.
> 5. **A regra de escape chaveia em PROVENIÊNCIA, não em tipo.** Restringe o valor que VEIO de
>    `svc<T>()` (um serviço reclamado), NÃO qualquer valor cujo TIPO seja serviço. Um factory que
>    devolve `EnHi { }` fresco está OK — `ctor` é um factory `static` ordinário, mesmas regras de
>    qualquer função (Teko não tem construtores, só factories).
> 6. **Path 2 (chave-em-runtime) CONSERTADO, não honest-stopped.**

Este plano tem DUAS partes nítidas, como o §5. **O implementador executa a PARTE A (superfície).** A
PARTE B (o binding lifetime→slot-de-arena por-thread) é o Doc 1 §8, que preenche a costura que a Parte A
deixa aberta com o corpo simplest-correct.

---

## 0. Recap normativo + a REGRA DE FASE

**SAI (superfície da era-anotação):** a DI por anotação — `#singleton`/`#scoped`/`#transient` sobre
`type` (`TypeDecl.di_kind`), o overlay `#inject(...)` sobre `fn` (`Function.has_inject`/`injects`), o
campo-chave inerte (`has_di_key`/`di_key`), a pass inteira `src/checker/di.tks` (`choose_factory`,
`build_di_registry`, `check_inject_overlays`, `di_lower_use`, materializadores), e o `#singleton` de
BINDING (residência-raiz, `parse_stmt.tks:248` → `residence.tks`). Cobertura MIGRA, não se perde.

**ENTRA:** o kind `service` (SEMPRE selado) no slot de kind de `type N = …`; os três lifetimes; a
interface sintetizada `IService` + as duas capacidades de contrato (static, `self`-retorno); os
intrínsecos `svc<T: IService>` / `has_svc<T: IService>`; a regra de escape por-proveniência; o erro de
conflito mesmo-tipo-mesma-chave; o constraint `T: IService` (e o composto `singleton & IService`,
§10-nota).

**RESOLVE:** resolução determinística em comp-time por uma TABELA ESTÁTICA NOMEADA (tipo canónico,
chave) → provedor (`build_service_registry`, pré-walk). Chave CONSTANTE ausente = erro de comp-time
(a tabela é estática — ver D6). Chave de RUNTIME = locator de runtime por-IService-alvo (miss = panic,
guardado por `has_svc`). Conflito mesmo-tipo-mesma-chave = erro na construção da tabela.

### REGRA DE FASE (idêntica ao §5/§6)

A Parte A faz a SUPERFÍCIE AGORA. O **binding lifetime→slot-de-arena por-thread** é o Doc 1 §8. Onde o
lowering de `svc` coloca o valor num slot de arena, a Parte A deixa uma COSTURA NOMEADA
(`svc_scope_expr`) cujo corpo preenche com o mais-simples-correto (raiz-do-PROGRAMA / frame) e o Doc 1
§8 troca por raiz-da-thread / sub-raiz-da-thread. É o análogo direto de `emit_ptr_wrap_guard` do §5.

---

## 0.1 A CRASH — root cause VERIFICADO + o sítio de conserto exato

A crash (`FATAL signal — a generated program crashed (M.1)`, ver `gen3_repro.log`) manifesta-se DURANTE
a checagem do corpus self-host, num nó `svc`/`has_svc` meio-resolvido cuja avaliação/locator toca
provedores nunca validados. Inspecionei o C gerado da versão que CRASHAVA (`out2/teko.c`) e confirmei o
root-cause que o dono descreveu. **A inconsistência é DUPLA:**

**(1) DUAS registries construídas independentemente, com naming DIVERGENTE:**
- **Checker** — `build_service_registry` (`out2/teko.c:87037`, corpo mangled do `.tks`): regista
  provedores com nomes CANÓNICOS — concreto `.name = impl_canon = qualify(ns, last(td.name))`,
  interface `.name = canonical_service_name(iface, ns, table)`, `.key = impl_bare`.
- **Codegen** — `cg_add_service_providers` (`out2/teko.c:158372`, `codegen.tks:9658-9690`): regista com
  nomes CRUS — concreto `.name = td.name`, interface `.name = cb0.implements[j]` (o texto-fonte
  não-qualificado, p.ex. `"G"`), `.key = bare`.

**(2) O gate do checker e a emissão do codegen DISCORDAM sobre T=interface:**
- `svc_intrinsic_target` (`out2/teko.c:117111`, `checker …:1023`) GATEIA em
  `type_is_service(name)` — logo `svc<G>`/`has_svc<G>` com `G` interface é REJEITADO no call-site
  ("svc<T> requires T to be a service …; 'G' is not").
- MAS `emit_svc_locators` / `emit_svc_locator_protos` (`out2/teko.c:158813`/`:158770`,
  `codegen.tks:9868-9869`/`:9845-9846`) enumeram `cg_svc_keyed_ifaces(reg)` — TODO provedor de chave
  não-vazia, i.e. TODA interface que QUALQUER serviço implementa — INCONDICIONALMENTE, independente de
  algum `svc<G>` ter sido aceite. Emitem um locator-C por-interface `<mangle(G)> …svc_locate_G(key,r)`
  cujo corpo faz upcast de cada provedor via `svc_upcast_expr` (`out2/teko.c:159126`) →
  `(G){ .data = <materializer>(r), .vtable = tk_vt_<impl>_<G> }`.

**O nó da crash.** O caminho de chave-de-runtime — `type_svc_runtime` / `type_has_svc_runtime`
(`out2/teko.c:117310`/`:117556`, `checker …:1050`/`:1099`) — SINTETIZA um `TCall` para
`svc_locator_name(mangle_ns_frag(name))` com `call_ns = "teko::__svc_locate"` cujo alvo é o locator
enumerado pelo codegen. Como (i) o checker rejeita `svc<G>` porém (ii) o codegen já emitiu o locator de
`G` sobre a registry CRUA e (iii) `resolve_service` (`out2/teko.c:87292`, `checker …:141-148`) tem um
loop de FALLBACK por `name_last_segment` que casa por nome-simples entre registries de naming diferente
— o nó meio-resolvido refere um locator construído sobre um conjunto de provedores cuja conformância o
checker NUNCA validou e cujo `tk_vt_<impl>_<G>` só é materializado quando a conformância serviço→interface
foi de facto registada. Ao chegar à avaliação comp-time desse nó, a lista de provedores materializada e a
lista de keyed-ifaces DIVERGEM (canónico vs cru), e o programa comp-time desreferencia um slot
`.vtable`/`.data` nunca populado → o `M.1`.

**Os índices `tk_panic_oob_at(...)` nos locators (`out2/teko.c:158750` §9831, `:158823/:158830` §9868/9869,
`:158859` §9885, `:158909` §9911) são o SINTOMA** (a enumeração cruzada de duas listas de tamanhos
diferentes indexa fora-de-limites quando o naming não bate) — **não a causa.** A causa é a discordância
checker↔codegen.

### O CONSERTO na RAIZ (3 sítios `.tks`, desenhados como crumbs abaixo)

1. **UMA registry, UM naming.** DELETAR `cg_build_service_registry`/`cg_add_service_providers`
   (`codegen.tks:9658-9690`) inteiros; o codegen CONSOME a `ServiceRegistry` do CHECKER (já enfiada em
   `env.svc` / `CgProg`). Uma registry canónica, um naming — elimina a divergência crua-vs-canónica na
   raiz. (Sítio: `src/codegen/codegen.tks` — a função `emit_svc_*` recebe `reg` do checker, não
   reconstrói.)
2. **O gate vira `type_satisfies_iservice`.** `svc_intrinsic_target` (o predicado do checker, molde do
   `out2/teko.c:117111`) aceita T sse T é um `service` concreto OU uma `interface & IService` (§3). É
   ESTE o ponto de concordância checker↔codegen: sob fork (a), `svc<G>` é ACEITE sse G satisfaz
   `T: IService`; uma interface PLANA é erro-de-tipo limpo e NUNCA entra no conjunto de keyed-ifaces.
3. **O locator é emitido SÓ para T conformante-a-IService E de facto reclamado.** `cg_svc_keyed_ifaces`
   / `emit_svc_locators` enumeram apenas interfaces (i) IService-conformantes e (ii) efetivamente
   reclamadas por um `svc<T>`/`has_svc<T>` aceite — o checker regista cada alvo-de-locator ao ACEITAR o
   intrínseco (um `[]str` de alvos-runtime no resultado da pré-walk). Uma interface plana rejeitada
   nunca é registada → o locator nunca é emitido → sem OOB.

O modelo IService (rulings 2/4) é EXATAMENTE o que faz checker e codegen concordarem; remove a raiz sem
honest-stop.

---

## 1. Blast radius (sítios semânticos REAIS)

**Achado decisivo (medido no branch):** o corpus self-host `.tks` usa ZERO `service`/`svc`/`IService`
(perdidos) e ZERO anotação-DI (`#singleton`/`#inject`/… fora de strings/comentários em `src/**/*.tks`
= 0). Só os `.tkt`/fixtures exercitam a DI antiga. Logo a MIGRAÇÃO de USO é toda em teste, e a remoção
de MAQUINARIA não re-rota chamador de corpus. **`IService` é sintetizado pelo compilador — o corpus não
o escreve — então nada no corpus precisa da feature nova para buildar (seed indiferente).**

### (a) ADICIONAR — kind `service` + lifetimes (sintaxe `type N = service lifetime {}`)

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| a1 | `keyword_kind` (palavra→TokenKind) | `lexer/lexer.tks` (`Class` `~360`) | ADICIONAR `service` → `TokenKind::Service` (RESERVADA — introduz kind, como `class`/`interface`; ver D1) |
| a2 | enum `TokenKind` | `lexer/token.tks` (`Class`) | ADICIONAR variante `Service` (apêndice, ordinal no fim) |
| a3 | dispatch de kind em `parse_type_body` | `parser/parse_decl.tks:745-789` | ADICIONAR branch `Service` → `parse_service_body` (junto ao `Interface` `:749`) |
| a4 | corpo de serviço | `parser/parse_decl.tks` (novo, molde `parse_class_body` `:602`) | ADICIONAR `parse_service_body`: consome `service`, lê lifetime, lê `& I …` (reusa `parse_amp_name_list`), lê `{ membros }` (reusa `parse_fields`-shape de classe), marca `is_service`/`service_lifetime` |
| a5 | lifetimes contextuais | `parser/parse_decl.tks` (novo, junto a a4) | ADICIONAR `parse_service_lifetime` (contextual `singleton`/`scoped`/`transient` no slot após `service`; ver D2) |
| a6 | AST: enum lifetime + marcas no `ClassBody` | `parser/ast.tks` (junto a `ClassKind`/`DiKind`) | ADICIONAR `ServiceLifetime = enum { Singleton; Scoped; Transient }`; marcar `ClassBody.is_service: bool` + `ClassBody.service_lifetime: ServiceLifetime` (o serviço REUSA `ClassBody`; é uma classe selada com lifetime) |
| a7 | rejeitar `virtual`/`abstract service` | `parser/parse_decl.tks:746` (o branch class já apanha `Virtual`/`Abstract` antes de `service`) | um `type N = virtual service …` cai no branch class e falha em `!Class` → melhorar a msg (D5): serviço é sempre selado |

### (b) ADICIONAR — `IService` sintetizada + as duas capacidades de contrato

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| b1 | síntese de `IService` (prelude) | `checker/collect.tks` (junto à síntese de traits estruturais `~1678`) | SEED de `exp type IService = interface { exp static ctor(): self }` na TypeTable/items ANTES da conformância; nominal, referenciável em `& IService` |
| b2 | contrato `static` numa interface (2a) | `parser/parse_decl.tks:668-692` (`parse_interface_body`) + conformância `collect.tks:976-1004`/`1056-1064` | PERMITIR `static` num membro de interface (`parse_function` já consome `static` `:206`); `method_sig_matches` já tem paridade instância/estático (`:981-983`); ADICIONAR os métodos ESTÁTICOS do implementador ao conjunto candidato de conformância (hoje só instância) |
| b3 | `self`-retorno num contrato (2b) | `checker/collect.tks:147` (`return_type_is_self` já existe) + `method_sig_matches:998-1003` | TEACH: quando `req.return_type` é o átomo `self`, casa contra o retorno do impl LIGANDO `self`→`type_name` (o tipo conformante), em vez de resolver `self` no `ref_ns` da interface |
| b4 | `type_satisfies_iservice(T, table)` | `checker/collect.tks` (novo, pequeno) | T concreto `is_service` → true (auto-conforma; o factory `static ctor(): self` É o contrato IService); T interface cujo extends transitivo inclui `IService` → true; senão false |

### (c) ADICIONAR — `svc`/`has_svc` comp-time + registry única + escape-por-proveniência

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| c1 | intrínsecos no typer (molde `type_ptr_wrap`) | `checker/typer.tks` (padrão `~1030`) + dispatch em `type_call` | ADICIONAR `type_svc` + `type_has_svc` (consomem `callee_type_args`; gate `type_satisfies_iservice`); chave const → resolve inline; chave runtime → nó de locator (Path 2 CONSERTADO) |
| c2 | `Env.di` → `Env.svc` | `checker/scope.tks:72,84-90` (+ as ~9 cópias de struct) | RE-FERRAMENTA: slot `di: DiRegistry` → `svc: ServiceRegistry` (mesmo slot; `injects` some) |
| c3 | pré-walk da tabela + conflito + alvos-de-locator | `checker/di.tks:79-122` → nova `build_service_registry` | entradas `(tipo canónico, chave)`; conflito mesmo-tipo-mesma-chave = erro; ADICIONA um `[]str` de IService-alvos reclamados por chave-runtime (para gatear o codegen) |
| c4 | escape POR PROVENIÊNCIA | `checker/typer.tks` (guardas nos 3 sinks) + flag no `TExpr` | marcar `TExpr.is_svc_claim` na materialização de `svc<T>()`; rejeitar store/arg/return de um valor `is_svc_claim` (NÃO type-directed — ver §GENUÍNO-2) |
| c5 | UMA registry no codegen (o conserto §0.1-1) | `codegen/codegen.tks:9658-9690` (`cg_build_service_registry`/`cg_add_service_providers`) | DELETAR; o codegen consome `reg` do checker via `CgProg`/`env.svc` |
| c6 | locator gateado (o conserto §0.1-3) | `codegen/codegen.tks:9831-9911` (`cg_svc_keyed_ifaces`/`emit_svc_locators`/`svc_emit_locator`/`svc_emit_has_locator`) | enumerar SÓ IService-alvos reclamados (de c3); manter `svc_scope_expr` (a costura) |
| c7 | id de tipo determinístico (FNV-1a) | `checker/di.tks:373` (`di_type_id`) | MANTER (renomear `svc_type_id`); é o tag do slot, reusado pela costura |

### (d) REMOVER — a DI por anotação da era anterior (idêntico ao plano velho, medido)

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| d1 | `src/checker/di.tks` exceto `di_type_id` | `checker/di.tks:1-372` | DELETAR `DiProvider`/`build_di_registry`/`choose_factory`/`check_inject_overlays`/`di_lower_use`/materializadores; PRESERVAR `di_type_id`(→`svc_type_id`) e a re-ferramenta `build_service_registry` (c3) |
| d2 | campos AST DI | `parser/ast.tks` (`Binding.di_kind`, `DiKind`, `InjectBinding`, `Function.has_inject`/`injects`, `TypeDecl.di_kind`/`has_di_key`/`di_key`) | DELETAR (sweep dos literais, ver d-sweep) |
| d3 | parse de atributos DI de type/fn | `parser/parse_decl.tks:896-953` (`parse_decl_attributes` DI-arms), `974-977` (`lifetime_kind_of`) | DELETAR arms `#singleton`/`#scoped`/`#transient`/`#inject`/`@key` |
| d4 | `#singleton` de BINDING (residência) | `parser/parse_stmt.tks:217-248` (`parse_binding_di`) | DELETAR (superfície da era-anotação; residência-raiz passa a faceta do lifetime de serviço — D4) |
| d5 | literais `di_kind = DiKind::None` em Binding | `parser/loop_head.tks:84,98,407`; `parser/result.tks:32` | SWEEP |
| d6 | consumo de `di_kind` na residência | `checker/residence.tks:268`,`166-167` | RE-FERRAMENTA: `residence_tier` MANTÉM o param `is_singleton` (mecanismo preservado, §6); a FONTE vira `false` — o `service singleton`→raiz-da-thread liga-se pela costura (Doc 1 §8) |
| d7 | materializadores + exclusões `di_kind==Singleton` | `codegen/codegen.tks` (materializadores DI, protos, exclusões de residência) | DELETAR os da DI-anotação; PRESERVAR `svc_scope_expr` |
| d8 | serialização `.tkb` de `di_kind`/`has_di_key`/`di_key`; add `is_service`/`service_lifetime` | `emit/tkb_read.tks`, `tkb_write.tks` | REMOVER I/O DI, ADICIONAR I/O de `ClassBody.is_service`/`service_lifetime` + do marcador `static`/`self`-contrato; **BUMPAR `TKB_EXPR_VERSION`(=3→4)+`TKB_PROGRAM_VERSION`(=5→6)** (`tkb_frame.tks:413,422`) |
| d9 | consumidores residuais de `di_kind`/`DiKind` | `checker/{collect,comptime_fold,consteval,monomorph,resolve,synth,tast,typer}.tks` (as ~cópias de struct que propagam `di_kind`) | SWEEP (a rede é o checker rejeitar literal com campo em falta no dry build) |

---

# PARTE A — SUPERFÍCIE (implementa AGORA — 1 reseed, ZERO runtime C)

## A.1 Formas Teko que o implementador ADICIONA (full-Javadoc, copiar verbatim)

Um serviço (kind `service` no slot de kind; selado por construção; lifetime após `service`; factory
`static ctor(): self` = o contrato IService):
```teko
/**
 * Um serviço — um KIND selado (nunca `virtual`/`abstract`) com um lifetime de arena, resolvido em
 * comp-time por `svc<T>()`. Escreve-se `type Nome = service <lifetime> { … }`: `service` no slot de
 * kind (como `class`/`interface`), o lifetime (`singleton`/`scoped`/`transient`) governa em que slot
 * de arena o valor reside (Doc 1 §8, por-thread). Auto-conforma a `IService` — DEVE ter o factory
 * `static ctor(): self` que `svc<Clock>()` inlina.
 *
 * @since 0.3.1
 */
type Clock = service singleton {
    /**
     * Constrói a instância — o factory `static` do contrato `IService`; `svc<Clock>()` inlina-o.
     * @return self  a nova instância de `Clock`
     */
    static fn ctor(): self { return Clock { } }

    /**
     * @return i64  o instante corrente (stub determinístico neste exemplo)
     */
    fn now(): i64 { return 0 }
}
```

Uma interface de utilizador que PARTICIPA da resolução de serviço estende `IService` (fork (a), §3):
```teko
/**
 * Uma interface que é TAMBÉM um contrato de serviço: ao estender a `IService` sintetizada, todo o
 * conformante de `G` é conformante de `IService`, logo `svc<G>(key)` resolve os provedores de `G`. Uma
 * interface PLANA (sem `& IService`) NÃO participa — `svc<Plana>` é erro-de-tipo limpo (nunca crash).
 *
 * @since 0.3.1
 */
type G = interface & IService {
    /**
     * @return i64  o cumprimento (o exemplo determinístico do §7)
     */
    fn greet(): i64
}
```

Um serviço que implementa `G` (conforma a `G` E — auto — a `IService`; um provedor sob a chave = nome
do impl):
```teko
/**
 * Um provedor de `G` sob o lifetime singleton. Conforma a `G` (logo a `IService`). `svc<G>("EnHi")`
 * resolve-o por nome; `svc<EnHi>()` resolve-o por tipo concreto.
 *
 * @since 0.3.1
 */
type EnHi = service singleton & G {
    /** @return self  a nova instância. */
    static fn ctor(): self { return EnHi { } }
    /** @return i64  cinco. */
    fn greet(): i64 { return 5 }
}
```

Os dois intrínsecos comp-time (reconhecidos NO typer por forma-de-callee, como `__wrap<T>` do §5 — SEM
gramática nova; a forma-2 `f<T>()` já existe nesta wave):
```teko
/**
 * Resolve o serviço `T` e devolve a instância do slot do seu lifetime (singleton → raiz-da-thread;
 * scoped → sub-raiz-da-thread; transient → fresca). A `key` desambigua provedores: `null` = por tipo;
 * str CONSTANTE = por nome (resolvida inline em comp-time; miss = erro de comp-time); str de RUNTIME =
 * service-locator (miss = panic, guardado por `has_svc`). `T` DEVE satisfazer `IService` (um `service`
 * concreto, ou uma `interface & IService`); senão é erro de tipo.
 *
 * @param T          o serviço a resolver (exatamente um type-arg)
 * @param key        a chave; `null` = por tipo
 * @return T         a instância resolvida (arena-bounded; svc-reclamada — não armazenável/passável/retornável)
 * @throws panic     quando uma chave de RUNTIME não tem provedor (guardar com `has_svc<T>(key)`)
 * @since 0.3.1
 */
// intrínseco: svc<T: IService>(key: str | null = null): T

/**
 * Verdadeiro sse existe um provedor registado sob `(T, key)` — o guarda para `svc<T>(key)`. Com chave
 * CONSTANTE resolve-se em comp-time; com chave de RUNTIME emite o has-locator por-alvo.
 *
 * @param T          o serviço a checar (satisfaz `IService`)
 * @param key        a chave (OBRIGATÓRIA)
 * @return bool      true sse há provedor sob `(T, key)`
 * @since 0.3.1
 */
// intrínseco: has_svc<T: IService>(key: str): bool
```

A `IService` sintetizada (o compilador injeta-a; NÃO se escreve — mostrada só para o contrato ser
explícito; exercita as duas capacidades novas):
```teko
/**
 * IService — o contrato sintetizado a que todo o serviço auto-conforma. Um único membro: o factory
 * `static ctor(): self`. Materializa (a) contrato de método `static` numa interface e (b) `self` como
 * tipo-de-retorno de contrato (= a instância do implementador). NÃO se escreve no código; o compilador
 * seed-a-o no prelude (§b1).
 *
 * @since 0.3.1
 */
// sintetizado: exp type IService = interface { exp static ctor(): self }
```

## A.2 Fns internas que o implementador ACRESCENTA / TOCA (Parte A)

- **Parser:** `parse_service_body` (= corpo de classe selado + `parse_service_lifetime` + `& I` +
  `is_service`); `parse_service_lifetime(tokens, pos): Parsed<ServiceLifetime>` (contextual). Branch
  `Service` em `parse_type_body`.
- **Checker (conformância/IService):** seed de `IService` (b1); `type_satisfies_iservice(name, table):
  bool` (b4); permitir `static` em contrato + incluir estáticos no candidato de conformância (b2);
  ligar `self`-retorno de contrato ao tipo conformante em `method_sig_matches` (b3).
- **Checker (svc):** `type_svc(c, env, table): TExpr | NotSvcOp | error` e `type_has_svc(...)` (molde de
  `type_ptr_wrap`); `build_service_registry(prog, table): ServiceRegistryOutcome` (conflito
  `(tipo,chave)` + `[]str` de alvos-runtime reclamados); `svc_value_escapes` por-proveniência (c4);
  `type_svc_runtime`/`type_has_svc_runtime` (Path 2 — nó de locator).
- **Codegen:** `emit_svc`/`emit_has_svc` (lowering inline por-lifetime); os locators de runtime
  `emit_svc_locators`/`svc_emit_locator`/`svc_emit_has_locator` GATEADOS aos alvos reclamados; a COSTURA
  `svc_scope_expr`. **DELETAR** `cg_build_service_registry`/`cg_add_service_providers` (consome a do
  checker).
- **Toca:** dispatch `svc`/`has_svc` em `type_call` (ANTES da resolução ordinária); `residence_tier`
  mantém `is_singleton`, fonte→`false`; `Env.di`→`Env.svc`.
- **NÃO toca:** `src/runtime/teko_rt.{c,h}` (frozen) nem `teko_rt.tks` — ZERO edição (§5-do-plano).

## A.3 Codegen — a COSTURA que o Doc 1 §8 vai ligar

`svc<T>()`/`has_svc<T>()` são lowered inline por-lifetime, reusando a materialização de instância. A
ÚNICA parte que o Doc 1 §8 troca é ONDE o slot vive:
```
svc<T>()  →  ({ <slot> = svc_scope_expr(<lifetime de T>);
                tk_region_lookup(<slot>, <svc_type_id(T,key)>) ?: build_via_ctor_and_register(...); })
```
A costura é `svc_scope_expr(lifetime: parser::ServiceLifetime): str` em `src/codegen/codegen.tks`:
- **Parte A (simplest-correct):** `Singleton`→`"tk_region_root()"` (raiz do PROGRAMA); `Scoped`→região
  do frame/bloco envolvente (`cg_enclosing_region_expr`, existe); `Transient`→sem slot (build fresco).
- **Doc 1 §8 troca DUAS linhas:** `Singleton`→raiz-da-THREAD; `Scoped`→sub-raiz-da-THREAD. Sem mudança
  de assinatura/typer/tipo/fixture (idêntico a `emit_ptr_wrap_guard`). `svc_type_id` é o tag do slot.

## A.4 Regra de escape (c4) — POR PROVENIÊNCIA, não por tipo (ruling 5)

Um `TExpr` cuja PROVENIÊNCIA é `svc<T>()` (a flag `is_svc_claim` posta na materialização) é REJEITADO
quando é (i) o RHS de um store em campo/atribuição, (ii) um argumento de chamada, ou (iii) o valor de um
`return`. A restrição segue o VALOR RECLAMADO — força o re-`claim` explícito. Isto NÃO é análise de
tipo: um factory `static fn ctor(): EnHi { return EnHi { } }` devolve um valor CONSTRUÍDO (não
svc-reclamado, `is_svc_claim=false`) → PERMITIDO, mesmas regras de qualquer função (Teko não tem
construtores, só factories). `var c = svc<Clock>()` (bind local) é PERMITIDO; reusar exige novo
`svc<Clock>()`. A propagação da flag é rasa (só o nó `svc` directo a carrega; um bind local
`var c = svc<…>()` PERDE-a na leitura de `c` — o escape apanha o call-site directo, que é a lei do
`claim`). Ver §GENUÍNO-2.

## A.5 `svc`/`has_svc` reusam a chamada genérica forma-2 (CONFIRMADO)

`svc<T>(key)`/`has_svc<T>(key)` parseiam como chamada genérica explícita `f<T>(...)` (forma-2). `type_call`
já popula `callee_type_args`; `type_svc`/`type_has_svc` consomem-no como `type_ptr_wrap` consome
`mc.type_args`. NENHUMA gramática de chamada nova. `svc`/`has_svc` NÃO são tokens de lexer — são nomes
de intrínseco reconhecidos por forma-de-callee em `type_call` (como `ptr::__unwrap`), sem colisão com
identificadores fora do slot de callee.

## A.6 Fixtures — Parte A (AUTORADAS; suite NÃO executada — dry build + fixpoint é o gate)

**Recuperar primeiro:** o fixture ACCEPT `examples/regressions/service_svc` existe em
`wip/s7-savepoint` (main.tks, .tkp, src/probes.tks, src/scenario.tks) — recuperar e ADAPTAR à sintaxe
`type N = service lifetime {}` + IService.

**ACCEPT — oráculo nativo (exit/assert = valor):**
| fixture | exercita | exit |
|---|---|---|
| `svc_singleton_resolves` | `type Clock = service singleton` + `svc<Clock>().now()` | valor de `now()` |
| `svc_transient_fresh` | `type Rng = service transient` + dois `svc<Rng>()` frescos | contador esperado |
| `svc_scoped_in_block` | `type Buf = service scoped` resolvido dentro de `{ }` | valor conhecido |
| `svc_keyed_const_disambiguates` | `type G = interface & IService`; `EnHi`/`FrHi` provedores; `svc<G>("EnHi")` + `svc<G>("FrHi")` (chaves CONSTANTES) | soma dos dois |
| `svc_runtime_key_resolves` | `svc<G>(k)` com `k` param de RUNTIME que casa "EnHi" (Path 2 — o locator) | greet do provedor |
| `has_svc_runtime_true_then_svc` | `has_svc<G>(k)` runtime == true → `svc<G>(k)` | greet |
| `has_svc_runtime_false_branch` | `has_svc<G>(k)` runtime == false (chave não registada) → ramo 0 | 0 |
| `svc_by_type_no_key` | `svc<EnHi>()` (por tipo concreto, sem chave) | greet |
| `svc_local_bind_ok` | `var c = svc<Clock>()` (bind local PERMITIDO) | valor |
| `svc_factory_return_ok` | um `fn mk(): EnHi { return EnHi { } }` (retorno de valor CONSTRUÍDO, não svc-reclamado) | greet (prova escape-por-proveniência) |

**REJECT — `EXPECT_COMPILE_FAIL`:**
| fixture | rejeita |
|---|---|
| `service_virtual_rejected` | `type X = virtual service singleton { }` (serviço é sempre selado) |
| `service_abstract_rejected` | `type X = abstract service singleton { }` |
| `svc_non_iservice_rejected` | `svc<i64>()` / `svc<PlainStruct>()` (T não satisfaz IService) |
| `svc_plain_interface_rejected` | `type P = interface { fn f(): i64 }` + `svc<P>("x")` (interface PLANA sem `& IService` — o caso da crash vira erro-de-tipo LIMPO; ver §0.1) |
| `svc_conflict_same_type_same_key_rejected` | mesmo `(tipo, chave)` registado duas vezes |
| `svc_missing_const_key_rejected` | chave CONSTANTE sem provedor (miss comp-time; D6) |
| `svc_escape_store_rejected` | `g = svc<Clock>()` (store do valor reclamado) |
| `svc_escape_arg_rejected` | `fun(svc<Clock>())` (passar o valor reclamado) |
| `svc_escape_return_rejected` | `return svc<Clock>()` (retornar o valor reclamado) |
| `service_no_ctor_rejected` | `type X = service singleton { fn now(): i64 { return 0 } }` (falta `static ctor(): self` — não conforma a IService) |

**Un-teach da DI antiga (junto do crumb de remoção):** `singleton_annotation_rejected`,
`inject_annotation_rejected`, `singleton_binding_rejected`.

## A.7 Sequência de crumbs + reseed (ADICIONAR primeiro, REMOVER por último)

O seed é o `teko` lançado anterior. O §7 ENSINA `service`/`svc`/`IService` (idioma novo) e REMOVE a
DI-anotação. Como o corpus `.tks` não usa NENHUM dos dois (medido) e `IService` é sintetizada (não
escrita), cada crumb builda gen1 com o MESMO seed. Reseed único no RITUAL FINAL.

- **C0 — [DOC] Este plano.** Commit deste plano. Doc-only. **Ritual: NÃO.**

### ADICIONAR (superfície nova primeiro)

- **C1 — [ADITIVO] kind `service` + lifetimes + parse.** a1-a7: `TokenKind::Service`;
  `parse_service_body`/`parse_service_lifetime`; branch em `parse_type_body`; `ClassBody.is_service`/
  `service_lifetime` + `ServiceLifetime`; rejeitar `virtual/abstract service`.
  **Ficheiros:** `lexer/lexer.tks`, `lexer/token.tks`, `parser/parse_decl.tks`, `parser/ast.tks`.
  **Teach:** `type N = service <lifetime> [& I …] { … }`. **Gate — RITUAL COMPLETO.** **Ritual: SIM.**
- **C2 — [ADITIVO] `IService` sintetizada + as duas capacidades de contrato.** b1-b4: seed de
  `IService`; `static` em contrato + estáticos no candidato de conformância; `self`-retorno de
  contrato→tipo conformante; `type_satisfies_iservice`. **Ficheiros:** `checker/collect.tks`
  (+ `parse_decl.tks` se `static` em interface precisar destravar). **Teach:** `interface & IService`,
  contrato `static`/`self`, auto-conformância de `service`. **REJECT** `service_no_ctor_rejected`,
  `svc_plain_interface_rejected` (parcial — a rejeição final vem com o gate em C3). **Gate — RITUAL
  COMPLETO.** **Ritual: SIM.**
- **C3 — [ADITIVO] `svc`/`has_svc` comp-time + registry ÚNICA + escape-por-proveniência + Path 2.**
  c1-c7 (+ o conserto §0.1): `build_service_registry` (conflito + alvos-runtime); `type_svc`/
  `type_has_svc` + dispatch; `type_svc_runtime`/`type_has_svc_runtime` (locator gateado);
  `svc_scope_expr` (costura); `emit_svc`/`emit_has_svc`; DELETAR `cg_build_service_registry`/
  `cg_add_service_providers` (codegen consome `reg` do checker); escape-por-proveniência; `Env.di`→
  `Env.svc`. **Ficheiros:** `checker/typer.tks`, `checker/scope.tks`, `checker/di.tks` (nova
  `build_service_registry`), `codegen/codegen.tks`. **Teach:** `svc<T>()`/`has_svc<T>()` + escape +
  conflito + chave-runtime. **Gate — RITUAL COMPLETO** (fixtures A.6 verdes; a crash NÃO reaparece —
  o teste `svc_runtime_key_resolves` prova o Path 2 corrigido). **Ritual: SIM.**

### SWEEP + REMOVER (a DI-anotação por último)

- **C4 — [SWEEP, test-corpus] Migrar os `.tkt`/fixtures da DI antiga.** Ver §6. Migrar arms
  `#singleton`/`#inject`/`choose_factory`/`DiProvider`/`di_kind` → `service`/`svc` (cobertura
  REJECT/ACCEPT preservada) OU remover; de-registar dos `.tkr` e das arrays CORPUS em `scripts/*.sh`.
  **Gate — RITUAL COMPLETO.** **Ritual: SIM.**
- **C5 — [REMOÇÃO] Deletar `di.tks` (exceto `svc_type_id`) + campos AST DI.** d1-d7, d9. PRESERVAR
  `svc_type_id` e o param `is_singleton` de `residence_tier` (fonte→`false`). **Un-teach:**
  `#singleton`/`#inject`/`@key` deixam de parsear. **REJECT** `singleton_annotation_rejected`,
  `inject_annotation_rejected`, `singleton_binding_rejected`. **Gate — RITUAL COMPLETO.** **Ritual: SIM.**
- **C6 — [REMOÇÃO/SWEEP] Serialização `.tkb`.** d8: remover I/O DI, ADICIONAR I/O de `is_service`/
  `service_lifetime` + marcador de contrato static/self; **BUMPAR `TKB_EXPR_VERSION`(3→4) +
  `TKB_PROGRAM_VERSION`(5→6)** (`tkb_frame.tks:413,422`). **Ficheiros:** `emit/tkb_read.tks`,
  `tkb_write.tks`, `tkb_frame.tks`. **Gate — RITUAL COMPLETO** — o crumb que MOVE bytes; o fixpoint
  prova-o. **Ritual: SIM.**

- **RITUAL FINAL — dry build + reseed manual (1×) + fixpoint + provenance + self-suficiência.** §8.

---

## 2. Contagem de reseed + justificação

**1 reseed** (no RITUAL FINAL). Justificação (idêntica ao §5/§6):

- O seed é o binário `teko` lançado anterior, INDIFERENTE a `service`/`svc`/`IService` (o corpus `.tks`
  não os usa; `IService` é sintetizada, não escrita; só os testes usam a superfície nova, e testes não
  gatilham o seed). Cada crumb builda gen1 com o MESMO seed.
- **Crumb gatilho do fixpoint (onde os bytes se movem):** **C6** (serialização `.tkb` + bump de versão)
  — muda a forma emitida. C1-C3 adicionam parse/checagem/codegen que o corpus `.tks` não exercita;
  C5 remove maquinaria sem chamador de corpus → tendem a fixpoint byte-idêntico já. C6 exige o BUMP e a
  prova gen2==gen3 sobre o novo wire. Cada crumb "Ritual: SIM" corre dry-build+fixpoint; o AVANÇO do
  seed committado é único (final).

---

## 3. Fork da conformância-de-interface — RESOLVIDO PELO DONO: opção (a)

**LEI (ruling 4): uma interface de utilizador participa da resolução ESTENDENDO IService.**
`type G = interface & IService { … }`. O utilizador DECLARA, na própria interface, que ela é um contrato
de serviço (combinando com a `IService` sintetizada, que carrega `exp static ctor(): self`). Então:

```teko
type G = interface & IService { fn greet(): i64 }            // G estende IService
type EnHi = service singleton & G { static fn ctor(): self { return EnHi { } } fn greet(): i64 { return 5 } }
type FrHi = service singleton & G { static fn ctor(): self { return FrHi { } } fn greet(): i64 { return 9 } }

pub fn only_has(key: str): i64 { if has_svc<G>(key) { return 1 } return 0 }   // G satisfaz IService → typecheck
pub fn pick(key: str): i64 { return svc<G>(key).greet() }                     // resolve provedores de G, key-desambiguado

type P = interface { fn f(): i64 }                            // interface PLANA — NÃO estende IService
// svc<P>("x")  →  ERRO DE TIPO LIMPO: "svc<T> requires T to satisfy IService …" (NUNCA a crash pré-codegen)
```

**A regra ÚNICA:** `T: IService` é satisfeito por (1) um `service` concreto (auto-conforma), OU (2) uma
`interface & IService`. É EXATAMENTE isto que faz checker↔codegen consistentes e remove a raiz da OOB: o
locator só é construído para um T conformante-a-IService, cujo conjunto de provedores o checker validou.
`svc<G>("EnHi")` (o caso motivador original) resolve porque G-conformantes SÃO IService-conformantes; a
interface plana `P` é o REJECT `svc_plain_interface_rejected`.

**Custo de maquinaria (bounded):** a síntese de `IService` (b1) + o predicado `type_satisfies_iservice`
(b4) que anda o extends-chain (`effective_interface_methods`/`find_interface_decl` já andam extends) +
as duas capacidades de contrato (b2/b3). Nenhum solver de constraints novo.

---

## 4. Constraint `T: IService` / `singleton & IService` — mínimo-agora (§9.2b)

A lei escreve `svc<T: IService>` e (nota §10) o channel-maker `make<T: singleton & IService>`. O
mecanismo de constraint JÁ existe: `parse_constraint_atom` (`parse_decl.tks:112`) produz
`ConstraintAtom{name}`, `&`-composto por `parse_constraint_and` (`:121-126` → `ConstraintAnd`), checado
por `constraint_satisfied`/`constraint_atom_satisfied` (`monomorph.tks:55,92-97`). Duas edições pequenas
alavancam o predicado que o intrínseco JÁ precisa:
1. `constraint_atom_satisfied("IService", concrete, table)` → `type_satisfies_iservice(concrete, table)`
   (o átomo `IService` é só um nome — parseia hoje sem mudança);
2. `constraint_atom_satisfied("singleton", concrete, table)` → "concrete é um `service` de lifetime
   Singleton" (o átomo de lifetime, para a nota §10 `singleton & IService`).

Isto torna `svc<T: IService>`, `fn get<T: IService>(): T { return svc<T>() }` e o composto
`singleton & IService` legais EXATAMENTE como a lei soletra, via o `&`-AND existente — sem solver §9.2b
completo. **Fica BLOQUEADO ao §9.2b** apenas a generalização (átomos arbitrários, superfície de método
de constraint) — irrelevante para `svc`/`has_svc`. **Recomendação: mínimo-agora.**

> **NOTA §10 (só nota):** `make<T: singleton & IService>` é o channel-maker do §10. Aqui apenas
> confirmamos que o constraint COMPOSTO `singleton & IService` (um lifetime AND IService) aterra pelo
> `&`-AND existente + os dois átomos acima. Nenhum código de §10 neste plano.

---

## 5. A COSTURA de backend (o Doc 1 §8 preenche) — assinatura congelada AGORA

`svc_scope_expr(lifetime: parser::ServiceLifetime): str` em `src/codegen/codegen.tks`:
- **Parte A:** `Singleton`→`"tk_region_root()"` (raiz do programa); `Scoped`→`cg_enclosing_region_expr`;
  `Transient`→build fresco sem slot.
- **Doc 1 §8:** `Singleton`→slot da RAIZ-DA-THREAD; `Scoped`→sub-raiz-da-THREAD. Troca DUAS linhas de
  corpo; `svc_type_id` inalterado; nenhuma mudança de assinatura/typer/tipo/fixture. O runtime
  (`tk_region_root`/`tk_region_register`/`tk_region_lookup`) já existe — a Parte A já o usa.

## 5.1 `teko_rt.tks` self-suficiência (VEREDITO)

`src/runtime/teko_rt.tks` NÃO contém maquinaria DI (`grep` de `di_kind`/`DiProvider`/`choose_factory`/
`#inject`/`service` = 0). Declara as primitivas de arena (`tk_region_register`/`tk_region_lookup`/
`tk_region_root`) como host-edge externs honestos — EXATAMENTE as que o lowering de `svc` reusa via a
costura. **§7 exige ZERO edição de código a `teko_rt.tks`** (opcional: atualizar a NOTA que cita "futura
DI `#scoped`" para "`service`/`svc` (§7)"). `teko_rt.c`/`.h` frozen: intocados. Fork CLEARED.

---

## 6. Test-corpus cleanup (MANDATÓRIO) — a lista medida

Corpus `.tks` self-host: ZERO uso de DI-anotação e ZERO `service`/`svc` (medido) → nada a migrar em
`src/**/*.tks`. Os `.tkt`/fixtures que exercitam a DI antiga (arms DI por ficheiro, do plano velho,
re-confirmar no branch):

| ficheiro | destino |
|---|---|
| `src/checker/checker_test.tkt` | migrar ACCEPT/REJECT `#singleton`/`#inject`/registo/conflito/escape → `service`/`svc` |
| `src/parser/parser_test.tkt` | migrar parse `#singleton`/`#inject`/`@key` → `type N = service lifetime` |
| `src/checker/spine_test.tkt` | remover referências a `di_kind`/`DiKind` |
| `src/checker/residence_test.tkt` | `#singleton`-binding→Root vira sem-anotação (fonte `false`); MANTER as arms `residence_tier(is_singleton=true→Root)` (teste do mecanismo preservado) |
| demais `.tkt` com hits residuais de `di_kind`/`DiKind`/materializador | remover referências |
| `examples/regressions/service_svc` (em `wip/s7-savepoint`) | RECUPERAR + adaptar à sintaxe nova (ACCEPT) |

De-registar casos removidos dos `.tkr` e das arrays CORPUS em `scripts/*.sh`. Cobertura MIGRA: cada
REJECT/ACCEPT da DI antiga vira um fixture `service`/`svc` equivalente (§A.6). Feito em C4.

---

## 7. Decisões para o dono (recomendação, cada uma com exemplo)

- **D1 — `service`: RESERVADA.** Recomendo `TokenKind::Service` reservada — introduz um kind de
  declaração (como `class`/`interface`/`struct`), sempre reservado. As ocorrências de `service` no
  corpus são prosa de doc-comment; reservar não quebra nada.
  ```teko
  type Clock = service singleton { … }
  ```
- **D2 — lifetimes `singleton`/`scoped`/`transient`: CONTEXTUAIS** (só no slot após `service`). São
  palavras comuns; reservá-las globalmente custaria sem ganho.
- **D3 — factory FIXADO a `ctor` pelo contrato IService (vs "qualquer static zero-arg").** Recomendo
  FIXAR `ctor`: a `IService` sintetizada exige `static ctor(): self`, logo a conformância É o requisito
  do factory (substitui o `choose_service_factory` "qualquer static zero-arg" do design perdido por um
  requisito nomeado, uniforme e diagnosticável). REJECT `service_no_ctor_rejected`.
- **D4 — `#singleton` de BINDING: REMOVER superfície, PRESERVAR mecanismo** (precedente §6). `residence_tier`
  mantém `is_singleton`; fonte `b.di_kind==Singleton` → `false`; a residência-raiz de `service singleton`
  liga-se pela costura (Doc 1 §8).
- **D5 — texto EXATO dos diagnósticos REJECT:**
  - virtual/abstract: `"a service is always sealed; write 'type N = service <lifetime> { … }' (no 'virtual'/'abstract')"`
  - T não-IService: `"svc<T> requires T to satisfy IService — a 'service' type, or an 'interface & IService'; '{name}' does not"`
  - interface plana: idem (é o mesmo diagnóstico; é o que faz a crash virar erro-de-tipo)
  - conflito: `"duplicate service registration for '{type}' under key '{key}': same-type-same-key collides at compile time (distinct keys coexist)"`
  - escape: `"a claimed service value is arena-bounded: it may not be stored, passed as an argument, or returned — re-claim it with svc<T>() where needed"`
  - sem ctor: `"a service must conform to IService: declare 'static fn ctor(): self'"`
- **D6 — chave CONSTANTE ausente: erro de COMP-TIME.** A resolução const é comp-time e a tabela é
  estática → um miss é diagnosticável (melhor que panic). A chave de RUNTIME mantém o panic literal
  (guardado por `has_svc`) — é a lei do Path 2. `has_svc` continua o guarda para presença condicional.
- **D7 — slot em §7: raiz-do-programa vs frame.** Recomendo `Singleton`→raiz-do-programa,
  `Scoped`→frame, AGORA; o Doc 1 §8 troca por raiz-da-thread / sub-raiz-da-thread (§5).

**Nenhum fork genuíno residual para HALT.** O fork da conformância foi RULADO (opção (a), §3); tudo o
mais resolve-se pela lei. Ver §10.

---

## 8. O ritual

Em CADA invocação de compilador:
1. `export TK_RT_DIR="$PWD/src/runtime"`.
2. **Dry build:** `TEKO_BACKEND=c <compiler> build . -o out --no-verify --release`.
3. **Reseed manual (RITUAL FINAL, 1×):** `bootstrap/teko.c` → binário; `TEKO_BACKEND=c binário build .
   --no-verify --release` → `OUT/teko.c`; **fixpoint byte-idêntico gen2==gen3** (o gate; sem correr a
   suite — as fixtures são AUTORADAS).
4. **PROVENANCE:** atualizar o registo de proveniência do reseed.
5. **Self-suficiência de `teko_rt.tks`:** `grep` de `di_kind`/`DiProvider`/`choose_factory`/`#inject`/
   `service` em `src/runtime/teko_rt.tks` = 0 (é 0 hoje; tem de permanecer 0).
6. **Anti-regressão da crash:** confirmar que `svc_runtime_key_resolves` + `svc_plain_interface_rejected`
   buildам/rejeitam limpo — a versão que crashava (`out2/teko.c`) tinha DUAS registries e um locator
   não-gateado; o dry build sobre o corpus + estas fixtures prova que a raiz foi removida.

---

## 9. Riscos e tensões de lei

| risco | mitigação |
|---|---|
| **R1 — a crash reaparece** | NÃO: o conserto §0.1 é ESTRUTURAL — uma registry (delete a do codegen), o gate `type_satisfies_iservice` (checker↔codegen concordam), locator gateado aos alvos reclamados. `svc<PlainInterface>` é erro-de-tipo LIMPO (fork (a)), nunca enumeração meio-resolvida. Fixture `svc_runtime_key_resolves` trava o Path 2 corrigido. |
| **R2 — `static`/`self` em contrato de interface quebra interfaces existentes** | NÃO: as capacidades são ADITIVAS (um contrato sem `static`/`self` comporta-se igual). `method_sig_matches` já tem paridade instância/estático (`:981`); `return_type_is_self` já existe (`:147`). Só o candidato de conformância ganha os estáticos e o `self`-de-contrato liga ao tipo conformante. |
| **R3 — `IService` sintetizada colide com um `IService` de utilizador** | MEDIR no C4/dry build: `grep IService` no corpus = 0. É um nome sintetizado exp reservado de facto; se um utilizador o declarar, o checker acusa duplicado (não silencioso). |
| **R4 — remover `di_kind` quebra residência** | NÃO: `residence_tier` PRESERVA `is_singleton` (mecanismo); só a fonte vira `false` (D4). Nenhum binding de corpus usa `#singleton` (medido). |
| **R5 — `.tkb` shift de ordinal** | Não é fork (ruling §6): remover DI + adicionar `is_service`/`service_lifetime` + bumpar `TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION`. Reseed builda de FONTE → fixpoint indiferente. |
| **R6 — sweep de literais `di_kind =` erra um** | O checker REJEITA literal de struct com campo em falta no dry build ANTES do fixpoint — a rede é o próprio checker. |
| **R7 — escape-por-proveniência precisa de fluxo** | NÃO: é uma flag `is_svc_claim` no nó `svc` + 3 guardas sintáticas (store/arg/return). NÃO é análise de fluxo (§GENUÍNO-2). |

**Tensão de lei residual: NENHUMA que force HALT.** Teko-only respeitado (`teko_rt.c/.h` frozen
intocados). Issue-100%: os crumbs entregam o §7 inteiro (kind+lifetimes → IService+contratos → svc/has_svc
+conflito+escape+Path 2 corrigido → remoção da DI-anotação), sem regressão. Bootstrap-safe: o seed
anterior compila cada gen1 (corpus não usa nem DI-anotação nem `service`/`IService`).

---

## 10. Forks genuínos (stop-and-report) — TODOS CLEARED, sem HALT

1. **Conformância-de-interface (o crux):** RULADO pelo dono = opção (a) — `interface & IService` (§3).
   Sem HALT.
2. **A crash é uma pass de análise nova?** NÃO — é a discordância checker↔codegen (§0.1), consertada por
   uma registry única + o gate IService + o locator gateado. A tabela é a re-ferramenta de
   `build_di_registry` (`di.tks:79-122`).
3. **Escape via `escape.tks`?** NÃO — é por PROVENIÊNCIA (flag `is_svc_claim` + 3 guardas), maquinaria
   nova mas MÍNIMA no typer; não força a análise de residência. Declarado (§GENUÍNO-2).
4. **`teko_rt.tks` self-suficiente?** SIM, sem edição (§5.1).
5. **`service`/`IService` colidem com identificador no corpus?** NÃO (R3): medido 0.

**Sem fork genuíno pendente. Sem HALT.** O plano é executável na íntegra assim que o dono ratificar as
recomendações D1-D7 (nenhuma delas bloqueia o arranque; são o texto/ergonomia, não a arquitetura).

---

*Fonte: `mudancas-superficie-0.3.1.md` §7. Backend do lifetime-de-arena: `arena-especificacao-unica-0.3.1.md`
(Doc 1) §8. Constraint: §9.2b (mínimo-agora, §4). Precedentes: §5 (`plano-secao5-marshall.md`, costura
`emit_ptr_wrap_guard`), §6 (`plano-secao6-aposentar-unsafe.md`). Re-design que dobra os rulings do dono
(2026-08) e conserta a crash M.1 na raiz. Read+design apenas — nenhum código de produto, nenhum reseed.*
