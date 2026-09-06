---
seq: 0139
crumb-id: DI-FIX1
milestone: M5
gate: "[fixpoint]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/di-completion-crumb-plan.md:124-236"          # CR-B fixture set + CR-A/C/D (3 landed)
  - "docs/design/plano-secao7-di-service-svc.md:361-397"        # §A.6 ACCEPT+REJECT fixture tables
  - "docs/design/di-recuperacao-estado-e-crumbs-0.3.1.md:1-90"  # measured state (Part A + CR-A/C/D landed)
---

# 0139 · DI-FIX1 — certificar a DI `svc`/`service` landada (fixtures CR-B) + verify-only CR-A/C/D

> Autorar as fixtures ACCEPT/REJECT que exercitam a superfície DI Parte A JÁ LANDADA (nenhuma existe na
> árvore) e confirmar, verify-only, que CR-A/CR-C/CR-D já estão implementados.

## Goal

A Parte A do DI (`service`/`svc`/`has_svc`/`IService`/lifetimes/escape-por-proveniência/Path 1+Path 2)
está LANDADA (`typer.tks:990-1157`, `collect.tks:10-34,822-840,1495`, `codegen.tks:3598-3665`,
`monomorph.tks:29,76-82`, `parse_decl.tks:75-102,760-793`) mas ZERO fixture a exercita — o corpus
self-host não usa `svc`, então ~600L de codegen `svc` (o slot-call por-lifetime, as cadeias-if de
runtime — o histórico da crash M.1) não têm oráculo nativo. Este crumb é CERTIFICAÇÃO: só fixtures
(paths que o fixpoint NÃO exercita → carve-out da lei de testes). Byte-preserving (nenhum código de
produto; a imagem-self do compilador não muda). Se uma fixture falhar, o defeito está no codegen `svc`
já aterrado e conserta-se DENTRO deste crumb (in-scope §7), não é achado adjacente. Também confirma
verify-only que CR-A (conflito), CR-C (IService-atom), CR-D (static) — que o `di-completion-crumb-plan`
listava como "por fazer" mas landaram desde então — continuam verdes.

## Where

- `examples/regressions/<name>/` — NOVOS projetos-regressão isolados (auto-registados por `.tkp`/`.tkr`
  + `src/**/case.tks`; REJECT usa o sentinela `EXPECT_COMPILE_FAIL`). NENHUM código de produto.
- **verify-only (nenhuma edição):** `collect.tks:936` (CR-D static), `collect.tks:1624-1662,1384`
  (CR-A conflito), `monomorph.tks:29` (CR-C IService-atom) — confirmar presentes e cobertos pelas
  REJECT abaixo.

## How

1. **Autorar as fixtures ACCEPT** (oráculo `Then it exits <valor>`), cada uma um projeto isolado com
   a superfície landada. Copiar as formas Teko VERBATIM de `plano-secao7…` §A.1 (o exemplo `Clock`/`G`/
   `EnHi` full-Javadoc) — a sintaxe é `type N = service <lifetime> [& I …] { static fn ctor(): self … }`.
2. **Autorar as fixtures REJECT** (`EXPECT_COMPILE_FAIL`), cada uma mirando um diagnóstico D5 exato de
   `plano-secao7…` §7-D5. Para `iface_static_instance_mismatch_rejected` (CR-D), MIRAR o buraco RESIDUAL
   de paridade static/instância de zero-args, NÃO o caso-`ctor` (que a assimetria self-param já rejeita
   — ver `di-completion-crumb-plan.md` §GAP-D); confirmar que a REJECT falha SÓ por causa da guarda
   `collect.tks:936`.
3. **Anti-regressão da crash M.1:** `svc_runtime_key_resolves` (Path 2, chave de runtime) DEVE buildar+
   correr verde e `svc_plain_interface_rejected` DEVE rejeitar limpo — a prova executável de que a raiz
   (registry-única + gate IService, `plano-secao7…` §0.1) removeu o M.1.
4. **Escape-por-proveniência:** os dois ACCEPT `svc_local_bind_ok` (`var c = svc<Clock>()`) e
   `svc_factory_return_ok` (`fn mk(): EnHi { return EnHi { } }`) provam que o guard é por-PROVENIÊNCIA
   (`is_svc_claim`), não por-tipo; os três REJECT `svc_escape_{store,arg,return}_rejected` provam a lei.
5. **CR-A conflito:** `svc_conflict_same_type_same_key_rejected` — dois namespaces (`a`,`b`) cada um com
   `type EnHi = service singleton & G { … }` + um `svc<G>("EnHi")`. Se o coletor barrar nomes-simples
   globais ANTES da verificação de conflito, reformular a fixture e REPORTAR a descoberta (não inventar
   issue) — `di-completion-crumb-plan.md` R3.

## Rulings & laws

- **Teko-only:** só `examples/regressions/**` + `.tks`; zero edição de `teko_rt.c/.h`/`assert`/`win32`.
- **Comment convention (W15):** `/** */` só em `exp`; sem `//`; nas fixtures, doc mínimo.
- **Fork protocol:** o desenho `service`/`svc` está deliberado (`plano-secao7…`, superseda SM-G6);
  se uma fixture expuser um bug ESTRUTURAL (M.1-like), HALT + reportar — o desenho da raiz diz que a
  `TypeTable`-única já o previne.
- **Testes (lei do dono):** SÓ oráculos `.tkr` isolados para paths que o fixpoint NÃO exercita — é
  exatamente o caso (o corpus não chama `svc`). NENHUM teste do que o self-build já exercita. As
  fixtures nomeadas abaixo são as ÚNICAS autorizadas neste crumb.
- **Safety:** NUNCA `teko test .`; build em subshell `ulimit -v 4718592` (4,5 GiB); `[fixpoint]`
  `gen2==gen3` byte-idêntico.

## Fixtures

ACCEPT (oráculo `exits <valor>`):

| fixture | exercita | expected |
|---|---|---|
| `svc_singleton_resolves` | `type Clock = service singleton`; `svc<Clock>().now()` | valor de `now()` |
| `svc_transient_fresh` | `type Rng = service transient`; dois `svc<Rng>()` frescos | contador |
| `svc_scoped_in_block` | `type Buf = service scoped` resolvido em `{ }` | valor conhecido |
| `svc_by_type_no_key` | `svc<EnHi>()` (por tipo concreto, sem chave) | `greet` |
| `svc_keyed_const_disambiguates` | `G = interface & IService`; `EnHi`/`FrHi`; `svc<G>("EnHi")+svc<G>("FrHi")` | soma |
| `svc_runtime_key_resolves` | `svc<G>(k)` chave de RUNTIME casando `"EnHi"` (Path 2 — anti-M.1) | `greet` |
| `has_svc_runtime_true_then_svc` | `has_svc<G>(k)==true` → `svc<G>(k)` | `greet` |
| `has_svc_runtime_false_branch` | `has_svc<G>(k)==false` (não registada) → ramo 0 | `0` |
| `svc_local_bind_ok` | `var c = svc<Clock>()` (bind local PERMITIDO) | valor |
| `svc_factory_return_ok` | `fn mk(): EnHi { return EnHi { } }` (valor construído, não svc-reclamado) | `greet` |
| `iservice_constraint_accepts_service` | `fn wants<T: IService>(): i64 {…}` chamado `wants<Clock>()` (CR-C) | `0` |
| `service_static_ctor_still_conforms` | `service singleton` com `static ctor(): self` conforma+resolve (CR-D não regride) | valor |

REJECT (`EXPECT_COMPILE_FAIL`):

| fixture | rejeita |
|---|---|
| `service_virtual_rejected` | `type X = virtual service singleton { }` |
| `service_abstract_rejected` | `type X = abstract service singleton { }` |
| `svc_non_iservice_rejected` | `svc<i64>()` / `svc<PlainStruct>()` |
| `svc_plain_interface_rejected` | `interface { fn f(): i64 }` + `svc<P>("x")` (o caso da crash → erro-de-tipo limpo) |
| `svc_conflict_same_type_same_key_rejected` | mesmo `(tipo, chave)` duplicado (CR-A) |
| `svc_missing_const_key_rejected` | chave CONSTANTE sem provedor (miss comp-time; D6) |
| `service_no_ctor_rejected` | `service singleton` sem `static ctor(): self` |
| `svc_escape_store_rejected` | `g = svc<Clock>()` |
| `svc_escape_arg_rejected` | `fun(svc<Clock>())` |
| `svc_escape_return_rejected` | `return svc<Clock>()` |
| `iface_static_instance_mismatch_rejected` | paridade static/instância zero-args divergente (CR-D, buraco residual) |
| `iservice_constraint_rejects_nonservice_rejected` | `wants<PlainStruct>()` falha o bound (CR-C) |

## Gate

`[fixpoint]` — dry-build (`TEKO_BACKEND=c <compiler> build . -o out --no-verify --release`) + `gen2==gen3`
byte-idêntico; as fixtures ACCEPT correm nativo (o oráculo é o exit). "Green" = todas ACCEPT saem no
valor, todas REJECT falham a compilação, `svc_runtime_key_resolves` corre (M.1 não reaparece), e a
imagem-self do compilador é byte-idêntica (nenhum código de produto mudou). Reseed-class: `none`.

## Deps

`—` (Parte A + CR-A/C/D já landados). Precede 0140 (retira o desconhecido do svc-codegen antes do
binding de canais construir por cima).

## Done when

O conjunto ACCEPT/REJECT acima existe, corre verde, o M.1 é provado-ausente por
`svc_runtime_key_resolves`+`svc_plain_interface_rejected`, e o fixpoint é byte-idêntico.
