# DI (`service`/`svc`) — recuperação do desenho ATUAL, estado medido e crumbs de fecho

> **Status:** RECUPERAÇÃO (read+design). NÃO é um re-desenho — é o registro do desenho JÁ deliberado e
> do que dele está LANDADO vs. por fazer, medido contra `fix/retirement @ 40bcd813`. Regra de fork
> "mais recente vence": onde houver várias versões do desenho, aplica-se a mais recente citada abaixo.
> **O desenho do DI existe e está em grande parte IMPLEMENTADO — a premissa "DI perdido, 13 linhas" é
> DESATUALIZADA** (D65, 2026-08-20). A maquinaria DI foi reimplementada e realocada de `src/di/di.tks`
> (hoje só `svc_type_id`, 13 linhas) para `typer.tks`/`collect.tks`/`codegen.tks`/`monomorph.tks`/
> `parse_decl.tks`. Este doc reconcilia.

## 1. Onde o desenho ATUAL do DI vive (fontes autoritativas, por camada)

| camada | fonte de lei (a mais recente) |
|---|---|
| superfície `service`/`svc`/`has_svc`/`IService`/lifetimes/escape (Parte A) | `docs/design/plano-secao7-di-service-svc.md` (re-design que dobra os rulings do dono; RESOLVE a crash M.1 na raiz) |
| fecho dos 4 gaps (conflito, fixtures, IService-atom, static) | `docs/design/di-completion-crumb-plan.md` |
| binding lifetime→arena-região por-thread (Parte B, seam `svc_scope_expr`) | `docs/design/arena-especificacao-unica-0.3.1.md` §7.8/§8 + crumb `.crumbs/0117-D1-DI-arena-lifetime-binding.md` |
| resolução de canais `svc<Rx<T>>`/`svc<Tx<T>>` por chave (exceção F2) | `docs/design/arena-especificacao-unica-0.3.1.md` §7.6-7.8 + `docs/design/plano-s10-channels-batch-detalhe.md` §C4 |
| decisões-âncora | `DECISION_LOG.md` D31 (memória híbrida + DI-scoped), D54 (superfície DI-scoped), D56 (`place()` handle), D58.1 (canais→DI), D120 (nada de honest-stops, DI pra frente) |

**SUPERSEDIDO — não usar como fonte:** `.crumbs/0012-SM-G6-di-service-taint.md` (M1, fonte `lang-evolution-0.3.1-memory-and-surface.md`) é o plano ANTIGO. Prevê um módulo `service_taint.tks` (NUNCA criado) e cita `di.tks:16/98/116/134/258` (`DiProvider`/`register_item_providers`/`register_over_implements`/`choose_factory`/`di_key_rejected`) — TODAS linhas da impl PERDIDA (D65); o `di.tks` atual só tem `svc_type_id`. O `plano-secao7-di-service-svc.md` SUPERSEDE-o explicitamente (dobra os rulings do dono 2026-08 + conserta a crash M.1) e a impl LANDADA segue ELE: escape-por-PROVENIÊNCIA via `is_svc_claim` no `typer.tks` (medido), registry-única-na-`TypeTable`, gate `type_satisfies_iservice` — NÃO o modelo taint-module/DiProvider-row do SM-G6. Regra "mais-recente-vence" → SM-G6 descartado como fonte.

## 2. Estado MEDIDO (verificado contra 40bcd813 — `file:line`)

**LANDADO (Parte A da superfície DI — completa):**
- Parser: `parse_service_body`/`parse_service_lifetime`/`is_service_lifetime_at` (`parse_decl.tks:75,760,773,951`); `ConstraintForm{form="service",lifetime}` (`parse_decl.tks:92-102`); `ClassBody.is_service`/`service_lifetime` + `ServiceLifetime` enum.
- Checker: `iservice_decl`/`iservice_ctor_sig` seed no prelúdio (`collect.tks:10-34,317,1691`); `type_satisfies_iservice`/`interface_extends_iservice` (`collect.tks:822-840`); `type_svc`/`type_has_svc`/`svc_providers`/`svc_runtime_chain`/`svc_provider_for_key`/`svc_not_iservice_error` (`typer.tks:990-1157,2015-2020`); erro "no ctor" (`collect.tks:1495`).
- Codegen: `emit_svc_slot_call`/`svc_scope_expr`/`cg_service_ctor_lifetime` (`codegen.tks:3598-3665`).
- monomorph: constraint-atom `IService` (`monomorph.tks:29`) + `constraint_form_satisfied` para lifetimes (`monomorph.tks:76-82`).

**LANDADO (os 3 dos 4 gaps do `di-completion-crumb-plan.md` — a análise foi escrita ANTES de landarem):**
- CR-A conflito mesmo-tipo-mesma-chave: `service_conflict_diags`/`_pair`/`_from` (`collect.tks:1624-1662`), ligado em `collect.tks:1384`, texto D5 exato.
- CR-C `IService` como predicado de constraint: `monomorph.tks:29`.
- CR-D enforcement `static` em `method_sig_matches`: `collect.tks:936` (`if req.is_static != impl_m.is_static { return false }`).

**POR FAZER (o que este doc sequencia):**
1. **CR-B — fixtures de `svc`/`service` (certificação).** Medido: ZERO fixture de svc/service/svc_reject existe na árvore (`ls examples/regressions | grep svc` = vazio). O `di-completion-crumb-plan.md` afirmava `svc_reject/` (9 cenários) + `constraint_service_lifetime` PRESENTES — **estão AUSENTES** (perdidos/nunca commitados). ~600L de codegen `svc` (Path 1 + Path 2) sem oráculo de runtime. → **crumb 0139**.
2. **Binding de canais `svc<Rx<T>>`/`svc<Tx<T>>` (C4).** Medido: `type_svc_channel` AUSENTE; `chan<T>::make<K>` AUSENTE; `src/threads/threads.tks` tem os tipos `Rx`/`Tx`/`Ctx`/`IChannelKind`/`Closed` mas os corpos são STUBS (`pop → Closed{}`, `send → null`, `add/done/wait/close → {}`). Runtime C `tk_memchan_*`/`tk_region_program`/`tk_waitgroup` LANDADO (`teko_rt.c`, 67 hits). → **crumb 0140** — é o que ELIMINA os honest-stops de `svc<>` em 0136/0137/0138 (D120).
3. **Parte B — seam `svc_scope_expr` → arena por-thread.** Medido: `svc_scope_expr` ainda devolve raiz-do-PROGRAMA para Singleton (`codegen.tks:3639`), não a sub-raiz-da-thread. Já é o crumb `0117` (needs-impl, M5) — NÃO re-numerado aqui; 0140 é pré-requisito da sua verificação da exceção-F2.

## 3. O binding de canais recuperado (fiel a §C4 + arena-espec §7.6-7.8) — SEM redesenho

`Rx`/`Tx`/`Ctx` **NÃO são serviços** (arena-espec §7.8 L771) — não passam por `svc_providers`/`type_satisfies_iservice`. A resolução é uma EXTENSÃO DI-por-chave contra a **registry de canais em F2** (a região imortal do programa), não a registry §7:

- **`chan<T>::make<K: service singleton & IChannelKind>(key, bounds)`** cria o transporte `K` (um `service singleton`), chama `K.init(key)`, e REGISTRA em F2 (`tk_region_program()`) sob `svc_type_id(chave-derivada)`: o transporte + um `Rx<T>` + um `Tx<T>` sintetizados (ids irmãos `.rx`/`.tx`) + a WaitGroup (`.wg`); devolve `Ctx`. Reusa a fábrica-estática phantom-owner F3 (`retarget_generic_static_callee` `typer.tks:3159`, `phantom_owner_subst` `:3216`) — o análogo `Dictionary<K,V>::make`.
- **`svc<Rx<T>>("k")`/`svc<Tx<T>>("k")`** → `type_svc_channel` (novo, ANTES do gate IService em `type_svc`): chave CONSTANTE emparelha com o `make<K>` da MESMA constante (K e bounds conhecidos em comp-time → ops monomorfizam pra `tk_memchan_*`/`tk_oschan_*` diretos, sem lookup); chave VARIÁVEL → `tk_region_lookup(tk_region_program(), svc_type_id(k))` + tabela de ponteiros-de-função (miss = panic, guardar com `has_svc`). `svc_type_id` (`di.tks:3`, FNV-1a) reusado inalterado.
- **Escape (Parte A, preservado):** o handle é svc-reclamado; `var rx = svc<Rx<T>>("k")` (bind local) é PERMITIDO (arena-espec §7.8: `var tx = svc<Tx<i32>>("res")`); não se armazena/passa/retorna — re-clama por chave no site de uso.
- **Lifetime/escopo (D31/D56):** o `singleton` do transporte é a EXCEÇÃO F2 — vive na raiz do PROGRAMA (imortal, cross-task), não na sub-raiz-da-thread (arena-espec §7.8 L522). O `Ctx` é o dono transient; seu teardown desregistra a entrada de F2 (reclamação por-entrada, C5/§7.8). Isto é `svc_scope_expr` para o caso-canal que 0117 §4 verifica.

## 4. Sequência de crumbs (a partir de 0139) + reseed

| seq | crumb | classe | reseed | dep |
|---|---|---|---|---|
| 0139 | DI-FIX-1 — fixtures de `svc`/`service` (CR-B) + verify-only CR-A/C/D | certificação (só fixtures) | none/fixpoint | — (Parte A landada) |
| 0140 | S10-CC5 / C4 — `type_svc_channel` + `chan<T>::make<K>` + corpos threads.tks | compilador+stdlib | fixpoint+reseed | 0116/0137 (transporte), 0139 |
| 0117 | D1-DI Parte B (existente) — seam `svc_scope_expr` → arena por-thread + verificação exceção-F2 | fixpoint-rebuild | fixpoint | 0140, RM-C16 |

**0139 primeiro** (retira o maior desconhecido: ~600L de svc-codegen nunca exercitado). **0140** é o eliminador dos honest-stops de 0136/0137/0138 (D120). **0117** fica como está (não re-numerar); 0140 é pré-requisito da verificação da exceção-F2 dele.

## 5. Forks — NENHUM genuíno pendente

Todo o desenho está deliberado (§1). O único ACHADO a reportar (não é fork, é drift de premissa): **a premissa do dispatch "DI perdido/13 linhas" está desatualizada** — a Parte A + 3 dos 4 gaps estão LANDADOS; o que falta é fixtures (CR-B), o binding de canais (C4) e a Parte B (0117). Zero edição de `teko_rt.c/.h`/`assert`/`win32` (D90); o DI é puro-Teko, o transporte-C já existe. Ratchet (D68/D118): 0140 é feature nova (não expurgo) — mede o pico; crescimento esperado modesto e sub-percentual (corpus não chama `chan::make`/`svc<Rx>`, self-imagem byte-idêntica).

*Fontes: `plano-secao7-di-service-svc.md`, `di-completion-crumb-plan.md`, `arena-especificacao-unica-0.3.1.md` §7.6-7.8/§8, `plano-s10-channels-batch-detalhe.md` §C4, `.crumbs/0117`, DECISION_LOG D31/D54/D56/D58.1/D120. Verificado @ 40bcd813.*
