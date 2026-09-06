# Plano de crumbs — completar a DI `service`/`svc` (§7)

> **Status:** DESIGN / crumb-plan. Read+design apenas (nenhum código de produto neste commit; o único
> artefacto é este documento). Fonte de lei: `docs/design/plano-secao7-di-service-svc.md` (o desenho
> `service`+`svc`). Branch: `fix/retirement`. Puxado-para-a-frente pela Lei 4 (a DI é upstream do
> trabalho de canais/concorrência — fix-now-or-pull-forward, não pergunta ao dono).
>
> **PORQUÊ ESTE PLANO É CURTO.** Uma análise-de-lacuna anterior estimou a DI em ~60% com quatro peças
> grandes em falta (registry ~200L, locators-de-codegen ~300-400L, átomos de constraint ~30L, estático
> ~1-2L). **Verificado contra o código no branch, a feature está ~90% construída e a análise está
> DESATUALIZADA em três dos quatro pontos.** Este plano regista o que REALMENTE falta (medido com
> `file:line`), sinaliza cada divergência, e sequencia o mínimo-correto. O grosso do trabalho restante é
> COBERTURA DE TESTE de codegen já emitido mas nunca exercitado em runtime.

---

## 0. Estado medido (o que JÁ existe — verificado, não re-derivado)

| capacidade | estado | evidência (`file:line`) |
|---|---|---|
| lexer/parser `service` + lifetimes | PRESENTE | `parser/parse_decl.tks:97-102` (constraint form), `is_service_lifetime_at`/`parse_service_lifetime` |
| `ClassBody.is_service`/`service_lifetime`, `ServiceLifetime` | PRESENTE | usado em `typer.tks:1045`, `codegen.tks:3279` |
| síntese de `IService` (prelude, `exp static ctor(): self`) | PRESENTE | `collect.tks:10-34` (`iservice_ctor_sig`/`iservice_decl`, seed em `collect_types`) |
| `type_satisfies_iservice` (concreto auto-conforma; interface via extends) | PRESENTE | `collect.tks:801-812`, `interface_extends_iservice:788-799` |
| `type_svc` / `type_has_svc` (typecheck completo) | PRESENTE | `typer.tks:1159-1219` |
| chave const → resolução inline; chave runtime → **cadeia if-then-else tipada** | PRESENTE | `svc_runtime_chain:1114`, `svc_has_runtime_chain:1133` |
| **lowering de codegen (Path 1 e Path 2)** | PRESENTE | `emit_svc_slot_call:3224`, `svc_scope_expr:3263`, `cg_service_ctor_lifetime:3271`, `emit_call:3286` |
| erro "no ctor" (não-conforma-a-IService) | PRESENTE | `collect.tks:1456` |
| escape-por-proveniência (store/arg/return) | PRESENTE | fixtures `svc_escape_{store,arg,return}_rejected` verdes |
| rejeições virtual/abstract, non-IService, plain-interface, missing-const-key | PRESENTE | `examples/regressions/svc_reject/` (9 cenários) |
| constraint `service singleton & I<T>` (forma+termo-genérico) | PRESENTE | `monomorph.tks:68-82` (`constraint_form_satisfied`), fixture `constraint_service_lifetime` |

**Conclusão-chave: `service`/`svc`/`has_svc`/`IService`/lifetimes/escape/Path 1+Path 2 estão
IMPLEMENTADOS.** O corpus `.tks` self-host não usa nenhum deles; só as fixtures os exercitam.

---

## 1. O que REALMENTE falta (medido) — e as três divergências da análise-de-lacuna

### GAP-A — Deteção de conflito (mesmo-tipo-mesma-chave). **REAL, em falta.**
`svc_provider_for_key` (`typer.tks:1189-1197`) devolve o **primeiro** provedor que casa a chave e ignora
duplicados silenciosamente. `svc_runtime_chain` gera um braço-if por provedor: dois provedores sob a mesma
chave produzem dois braços, o primeiro ganha, sem diagnóstico. **Colisão real:** dois serviços com o mesmo
`name_last_segment` em namespaces distintos, ambos implementando a interface `G` — `svc<G>("EnHi")` fica
ambíguo. Não há qualquer verificação. Nenhuma fixture cobre isto.

> **DIVERGÊNCIA vs análise-de-lacuna (peça 1 "central registry ~200L").** Um `Env.svc` +
> `ServiceRegistry` + `build_service_registry` **não se justifica** contra o código atual: o codegen já
> NÃO enumera uma registry própria — consome as cadeias-if construídas pelo typer (`svc_runtime_chain`)
> e resolve o slot on-demand (`svc_providers` percorre a `TypeTable`). A `TypeTable` dobrada JÁ É a
> registry única (é o que o `svc_reject.tkr:3-4` documenta como a correção-da-raiz da crash histórica).
> Construir um `Env.svc` paralelo seria maquinaria-morta (percorre-se a `TypeTable` na mesma) e
> reintroduziria a divergência-de-duas-registries que causou o M.1. **Recomendação law-first
> (Issue-é-100% = o COMPORTAMENTO do proposal, não uma struct interna):** entregar a deteção-de-conflito
> como uma passagem de diagnóstico focada sobre a `TypeTable`, sem novo campo de `Env` nem nova struct.

### GAP-B — Cobertura de runtime do codegen `svc` (Path 1 + Path 2). **REAL, em falta (só teste).**
O lowering existe (`emit_svc_slot_call`, `svc_scope_expr`, cadeias-if) mas **nenhuma fixture o corre em
runtime.** A única ACCEPT (`constraint_service_lifetime`) chama `Fast::ctor()` DIRETO e `wants<Fast>()` —
nunca `svc<…>()`. Logo `emit_svc_slot_call`/`svc_scope_expr`/a cadeia-if de runtime têm ZERO oráculo
nativo. O caminho de chave-em-runtime (o histórico M.1) não tem prova-de-não-regressão executável.

> **DIVERGÊNCIA vs análise-de-lacuna (peça 2 "codegen runtime locators, nada lowera para C, ~300-400L").**
> Está DESATUALIZADA: o Path 2 É lowered — não por um `emit_svc_locators` enumerando uma registry
> separada (esse desenho foi substituído), mas por o TYPER sintetizar uma cadeia `TIfExpr` de chaves
> (`svc_runtime_chain:1114`) que o codegen lowera pelo caminho-de-if normal, com cada `ctor` interno a
> receber o slot-de-lifetime via `emit_call`→`cg_service_ctor_lifetime`→`emit_svc_slot_call`. **Não há
> código de produto a escrever aqui — há fixtures a AUTORAR.** Se uma fixture de runtime falhar, o defeito
> está no codegen `svc` já aterrado e é consertado DENTRO do fecho do §7 (in-scope), não é achado
> adjacente.

### GAP-C — `IService` como PREDICADO de constraint (não nome-de-tipo). **REAL, em falta (~3L).**
`constraint_atom_satisfied` (`monomorph.tks:23-39`), ramo `InterfaceBody`, testa
`n.name == atom_name || type_conforms_to(n.name, atom_name, table)`. Para o átomo `IService` com um
`concrete` que é um **serviço concreto** (p.ex. `Clock`), `type_conforms_to("Clock","IService")` é
**falso** — o serviço auto-conforma a IService mas NÃO lista `IService` no seu `implements`. Logo
`fn f<T: IService>()` instanciado com um serviço concreto **falha o bound**. Bug real.

> **DIVERGÊNCIA vs análise-de-lacuna (peça 3 "singleton/scoped/transient como nomes-de-tipo, ~30L").**
> `singleton`/`scoped`/`transient` JÁ SÃO predicados: o parser roteia `service singleton` para
> `ConstraintForm{form="service",lifetime=…}` (`parse_decl.tks:97-102`) e `constraint_form_satisfied`
> (`monomorph.tks:68-82`) verifica-os corretamente (fixture `constraint_service_lifetime` verde). O
> resíduo real é APENAS o átomo `IService`, que resolve na `TypeTable` (é uma interface sintetizada) e
> por isso cai no ramo `InterfaceBody` errado. Correção ~3L, não ~30L. **Nota de gramática:** a lei
> escreve `T: singleton & IService`; a gramática atual exige `T: service singleton` para a faceta de
> lifetime. Um átomo NU `singleton` cai em `ConstraintAtom{name="singleton"}` → `type_table_find` falha →
> `builtin_type` falha → `false`. **Recomendação:** NÃO adicionar átomos-de-lifetime nus (duplicaria
> `ConstraintForm`); o composto lei-canónico é `service singleton & IService`, ambos os lados já
> suportados após GAP-C. Se o dono quiser o açúcar `singleton` nu, é um item de superfície separado —
> REPORTADO, não incluído.

### GAP-D — Enforcement de método `static` em `method_sig_matches`. **REAL, em falta (~1-2L).**
`method_sig_matches` (`collect.tks:896-923`) **não** compara `req.is_static == impl_m.is_static`.
`parser::Function.is_static` existe (`ast.tks:192`) e `merge.tks:314` já o compara para igualdade de
overload — o predicado de conformância diverge. Adicionar a guarda.

> **NUANCE HONESTA (para o implementador não escrever uma fixture que passa por acidente):** o exploit
> mais óbvio — uma instância `fn ctor(): self` a satisfazer o contrato `static ctor(): self` do IService —
> **já está bloqueado** pela assimetria de self-param: uma instância tem `params[0]` self-sem-tipo →
> `istart=1`; o contrato estático tem `params` vazio → `rstart=0`; `rstart != istart` (`collect.tks:901`)
> → já falha (é por isto que `service_no_ctor_rejected` já rejeita). A guarda `is_static` é
> **endurecimento de correção** que alinha `method_sig_matches` com `merge.tks:314` e torna o requisito
> D3 (factory FIXADO a `ctor` estático) explícito e diagnosticável, fechando o buraco residual (um
> método estático de zero-args a casar um requisito de instância de zero-args, ou vice-versa, onde a
> assimetria de self-param não dispara). É in-scope, barato e correto; a fixture deve mirar esse buraco
> residual, não o caso-ctor já coberto.

---

## 2. Sequência de crumbs (ordem recomendada + arestas de dependência)

**Arestas de dependência: NENHUMA entre os quatro.** Os quatro crumbs são independentes e
individualmente gate-áveis (own-fixpoint-green). A afirmação da análise-de-lacuna "a registry desbloqueia
2 e 3" é FALSA contra o código atual — 2 (codegen) já está aterrado e 3 (constraint) não depende de
registry. Ordena-se por RETIRADA-DE-RISCO (o maior desconhecido primeiro), não por dependência.

**Nenhum reseed.** Nenhum crumb adiciona campo de AST nem muda o wire `.tkb` (os campos `is_service`/
`service_lifetime` já são serializados — a feature já existe). O corpus `.tks` não usa `svc`, logo cada
gen1 builda com o MESMO seed. Ritual por crumb = dry-build + fixpoint (gen2==gen3 byte-idêntico); o
fixpoint é indiferente porque a forma emitida do corpus não muda. Ritual final único.

---

### CR-B — [CERTIFICAÇÃO, só-fixtures] Oráculos de runtime de `svc` (Path 1 + Path 2). **PRIMEIRO.**

Retira o maior risco: certifica ~600L de codegen `svc` nunca exercitado. Zero código de produto —
autoram-se fixtures ACCEPT com oráculo de exit nativo. Se alguma falhar, o defeito está no codegen `svc`
já aterrado e conserta-se aqui (in-scope §7).

- **Ficheiros:** só `examples/regressions/**` (novos diretórios auto-registados por `.tkp`/`.tkr` +
  `src/**/case.tks`; REJECT usa o sentinela `EXPECT_COMPILE_FAIL`, como `svc_reject/`).
- **Assinaturas tocadas:** NENHUMA (certificação).
- **Fixtures ACCEPT — oráculo `Then it exits <valor>`:**

| fixture | exercita | exit |
|---|---|---|
| `svc_singleton_resolves` | `type Clock = service singleton`; `svc<Clock>().now()` | valor de `now()` |
| `svc_transient_fresh` | `type Rng = service transient`; dois `svc<Rng>()` frescos | contador esperado |
| `svc_scoped_in_block` | `type Buf = service scoped` resolvido dentro de `{ }` | valor conhecido |
| `svc_by_type_no_key` | `svc<EnHi>()` (por tipo concreto, sem chave) | `greet` |
| `svc_keyed_const_disambiguates` | `G = interface & IService`; `EnHi`/`FrHi`; `svc<G>("EnHi")+svc<G>("FrHi")` (chaves CONST) | soma |
| **`svc_runtime_key_resolves`** | `svc<G>(k)` com `k` param de RUNTIME que casa `"EnHi"` (Path 2 — **anti-regressão M.1**) | `greet` |
| `has_svc_runtime_true_then_svc` | `has_svc<G>(k)` runtime `== true` → `svc<G>(k)` | `greet` |
| `has_svc_runtime_false_branch` | `has_svc<G>(k)` runtime `== false` (chave não registada) → ramo 0 | 0 |
| `svc_local_bind_ok` | `var c = svc<Clock>()` (bind local PERMITIDO) | valor |
| `svc_factory_return_ok` | `fn mk(): EnHi { return EnHi { } }` (retorno de valor CONSTRUÍDO, não svc-reclamado) | `greet` |

- **Certifica:** `emit_svc_slot_call`, `svc_scope_expr` (as três facetas de lifetime), a cadeia-if de
  runtime (`svc_runtime_chain`/`svc_has_runtime_chain`), e a lei-de-escape (os dois ACCEPT
  `local_bind`/`factory_return` provam que o guard é por-proveniência, não por-tipo).
- **Gate-ável sozinho:** SIM. **Ritual: SIM** (dry-build + fixpoint; as fixtures ACCEPT correm nativo).

---

### CR-D — [CORREÇÃO ~1-2L] Enforcement de `static` na conformância.

- **Ficheiro/fn:** `src/checker/collect.tks` — `method_sig_matches` (linha 896).
- **Mudança (sem alterar assinatura):** adicionar, junto às guardas de forma-de-param (após linha 901), a
  guarda de paridade estático/instância:
  ```
  if req.is_static != impl_m.is_static { return false }
  ```
  `parser::Function.is_static` já existe (`ast.tks:192`); espelha `merge.tks:314`.
- **Fixtures:**
  - **ACCEPT (guarda-de-regressão)** `service_static_ctor_still_conforms` — um `service singleton` com
    `static fn ctor(): self` continua a conformar a IService e a resolver por `svc<>()`; exits valor. Prova
    que a guarda não regride a conformância legítima.
  - **REJECT** `iface_static_instance_mismatch_rejected` — uma `interface & IService` com um método de
    contrato de zero-args cuja paridade estático/instância diverge do impl (o buraco residual que a
    assimetria de self-param NÃO apanha); `EXPECT_COMPILE_FAIL`. O implementador deve construir o caso
    para FALHAR só depois da guarda (confirmar que falha ANTES seria um falso-positivo — ver a nuance
    §GAP-D).
- **Dependência:** nenhuma. **Gate-ável sozinho:** SIM. **Ritual: SIM.**

---

### CR-C — [CORREÇÃO ~3L] `IService` como predicado de constraint.

- **Ficheiro/fn:** `src/checker/monomorph.tks` — `constraint_atom_satisfied` (linha 23), ramo
  `InterfaceBody`.
- **Mudança (sem alterar assinatura):** estender a disjunção do ramo `InterfaceBody` para reconhecer o
  átomo `IService` via o predicado existente `checker::type_satisfies_iservice` (`collect.tks:801`, `pub`):
  ```
  parser::InterfaceBody => match concrete {
      Named as n => n.name == atom_name
                 || type_conforms_to(n.name, atom_name, table)
                 || (name_last_segment(atom_name) == "IService" && type_satisfies_iservice(n.name, table))
      _ => false
  }
  ```
  Cobre o único caso residual (átomo `IService` + `concrete` serviço concreto). Interfaces de utilizador
  que estendem IService (`T: G`) já resolvem por `type_conforms_to`. Não requer novo `pub`.
- **Fixtures:**
  - **ACCEPT** `iservice_constraint_accepts_service` — `fn wants<T: IService>(): i64 { return 0 }` chamado
    `wants<Clock>()` (Clock = serviço concreto); exits 0. (Análogo direto de `constraint_service_lifetime`,
    mas com o átomo `IService`.)
  - **REJECT** `iservice_constraint_rejects_nonservice_rejected` — `wants<PlainStruct>()` falha o bound;
    `EXPECT_COMPILE_FAIL`, diagnóstico do bound de monomorfização.
- **Dependência:** nenhuma (usa `type_satisfies_iservice` já presente). **Gate-ável sozinho:** SIM.
  **Ritual: SIM.**

---

### CR-A — [CORREÇÃO, deteção-de-conflito] Duplicado mesmo-tipo-mesma-chave = erro.

- **Ficheiro:** `src/checker/typer.tks` (junto de `svc_providers`) ou `src/checker/collect.tks` (junto de
  `interface_conflict_diags:1565`, na agregação de diagnósticos `~1506`). **Recomendado: `collect.tks`,
  na passagem de diagnósticos** (é onde os conflitos de conformância já vivem; um-lugar-para-conflitos).
- **Forma de fn a ADICIONAR** (full-Javadoc, copiar verbatim; corpo pelo implementador):
  ```
  /**
   * Deteta registos de serviço duplicados sob a MESMA chave para o MESMO alvo IService — a única colisão
   * possível no modelo `service`/`svc` (a chave é o `name_last_segment` do impl, e dois serviços com o
   * mesmo nome-simples em namespaces distintos, ambos implementando a mesma interface `& IService`,
   * registam sob `(interface, nome)` idêntico). Sem esta verificação, `svc<G>("Name")` escolheria o
   * primeiro provedor silenciosamente. Percorre a `TypeTable` dobrada (a registry única — não há registry
   * paralela; ver `plano-secao7…` §0.1), agrupa os serviços por `(interface-IService-alvo, chave)` e
   * emite um diagnóstico por colisão. Alvos concretos nunca colidem (o nome-de-tipo é único).
   *
   * @param table  a tabela de tipos dobrada (a registry de serviços)
   * @return []str  os diagnósticos de conflito (vazio = sem colisão)
   * @since 0.3.1
   */
  fn service_conflict_diags(table: TypeTable): []str
  ```
  Ligar o resultado à agregação de diagnósticos existente (o padrão `concat_diags(...)` em
  `collect.tks:1506`). Sem novo campo de `Env`, sem nova struct, sem mudança de wire.
- **Diagnóstico (texto exato, D5 de `plano-secao7…`):**
  `"duplicate service registration for '{type}' under key '{key}': same-type-same-key collides at compile time (distinct keys coexist)"`.
- **Fixture (REJECT):** `svc_conflict_same_type_same_key_rejected` — dois namespaces (p.ex. `a`, `b`) cada
  um com `type EnHi = service singleton & G { static fn ctor(): self … }`, e um uso `svc<G>("EnHi")`;
  `EXPECT_COMPILE_FAIL` com o diagnóstico acima. (Verificar que `a::EnHi` e `b::EnHi` coexistem sem um
  erro de colisão-de-nome prévio — namespaces desambiguam; se o coletor já barrar nomes-simples
  duplicados globalmente, ajustar a fixture para forçar a colisão via a mesma chave por outra via, e
  REPORTAR a descoberta.)
- **Dependência:** nenhuma. **Gate-ável sozinho:** SIM. **Ritual: SIM.**

---

## 3. Pontos de ritual

- **Por-crumb (CR-B, CR-D, CR-C, CR-A):** cada um é own-fixpoint-green — dry-build
  (`TEKO_BACKEND=c <compiler> build . -o out --no-verify --release`) + fixpoint gen2==gen3 byte-idêntico.
  As fixtures ACCEPT de CR-B correm nativo (o oráculo é o exit).
- **RITUAL FINAL (uma vez, após os quatro):** gate completo — dry-build + fixpoint + a suite de
  regressões inteira (incluindo `svc_reject`, `constraint_service_lifetime` e as novas fixtures) +
  auto-suficiência de `teko_rt.tks` (`grep` de maquinaria-DI = 0, já é 0) + a anti-regressão explícita:
  `svc_runtime_key_resolves` builda e corre, `svc_plain_interface_rejected` rejeita limpo (o M.1 não
  reaparece).
- **Reseed:** NENHUM (sem campo de AST novo, sem mudança de wire `.tkb`). Se, ao autorar CR-B, se
  descobrir um defeito de codegen que exija um campo novo, ISSO dispara um reseed e re-sequencia-se — mas
  o desenho atual não o prevê.

---

## 4. Riscos + tensões de lei

| risco | resolução recomendada |
|---|---|
| **R1 — CR-B expõe um bug latente no codegen `svc`** | É o VALOR do crumb (retira o desconhecido). O conserto é in-scope §7 (não achado adjacente). Se o bug for estrutural (M.1-like), HALT e reportar; o desenho da raiz (`plano-secao7…` §0.1) diz que a `TypeTable` única já o previne. |
| **R2 — a fixture de CR-D passa por acidente** (a assimetria de self-param já rejeita o caso-ctor) | Mirar o buraco RESIDUAL (paridade estático/instância de zero-args), não o ctor. Confirmar que a REJECT falha SÓ após a guarda. Ver §GAP-D. |
| **R3 — CR-A: dois `EnHi` em namespaces distintos colidem no coletor antes de chegar à verificação** | Verificar a coexistência; se o coletor barrar nomes-simples globais, reformular a fixture e REPORTAR a descoberta (não inventar issue). |
| **R4 — divergência do desenho `plano-secao7…`** | Sinalizada em §1: (a) SEM `Env.svc`/`ServiceRegistry`/`build_service_registry` (o codegen consome a `TypeTable` + as cadeias-if do typer, não uma registry paralela — construir uma reintroduziria o M.1); (b) o Path 2 é lowered por cadeias-`TIfExpr` no typer, não por `emit_svc_locators`; (c) só o átomo `IService` precisa da correção-de-predicado, não `singleton/scoped/transient` (já `ConstraintForm`). Todas resolvem law-first por Issue-é-100%-do-COMPORTAMENTO (o proposal entrega-se; a struct interna não é lei). |
| **R5 — o açúcar `singleton & IService` (átomo nu) da lei §7** | A forma canónica suportada é `service singleton & IService`. O átomo nu `singleton` NÃO é predicado hoje. REPORTADO como item de superfície separado; não incluído (não bloqueia `svc`/`has_svc`). |

**Tensão de lei residual que force HALT: NENHUMA.** Teko-only respeitado (nenhuma edição a `teko_rt.c/.h`
frozen; nenhum código C). Bootstrap-safe (corpus não usa `svc`; seed indiferente). Issue-é-100%: os
quatro crumbs + fixtures entregam o comportamento §7 restante (conflito, cobertura de runtime, constraint
IService, enforcement estático) sem regressão. As divergências da análise-de-lacuna são de MEIO
(a feature já está aterrada de forma mais limpa), não de FIM — o proposal entrega-se na íntegra.

---

*Fonte de lei: `docs/design/plano-secao7-di-service-svc.md`. Verificado contra `fix/retirement`:
`typer.tks:1043-1219`, `codegen.tks:3224-3295`, `collect.tks:10-34,801-812,896-923,1456`,
`monomorph.tks:23-82`, `parse_decl.tks:90-113`, `scope.tks:14`. Read+design apenas — nenhum código de
produto, nenhum reseed neste commit.*
