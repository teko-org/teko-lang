# Consolidação do corpus de regressão em 10 projetos (0.3.1)

**Estado:** DESIGN-ONLY (plano mecânico de fold). Nada aqui foi executado. Nenhum arquivo movido,
nenhum `.tkp` editado, nenhum bump. Este documento é o mapa que um implementer segue.

**Ruling do dono (2026-08-02):** o corpus de regressão tem, NO TOTAL, exatamente **10 projetos**:
`regressor.tkr` (raiz, auto-regressão do compilador) + **9 temáticos**. Teto = 10. Regra vigente:
PROIBIDO criar `.tkp` novo — todo destino REUTILIZA um projeto existente como hospedeiro.

**Coordenação em voo (não recriar / não colidir):**
- O FFI já folou `native_host_ffi` DENTRO de `own_native` (membros do corpus). Não recriar.
- O cargo `move-on-return` (worktree `wt-move-impl`, `cargo/0.3.1.0-move-on-return-impl`) traz três
  fixtures novas — `mem_accum_block`, `mem_accum_return`, `mem_loop_per_iter` — que caem no projeto
  `mem`. Elas NÃO existem em `fix/union` hoje: são DESIGN-AHEAD (§7).

---

## 0. Mecânica do runner que este plano explora (verificado no código)

Fatos apurados em `src/build/regression.tks` e nas fixtures, que fundam cada decisão abaixo:

1. **`regr_dir` = dirname do `.tkr` listado.** Um `.tkr` liga-se ao seu projeto por VIVER na mesma
   pasta do `.tkp`. O runner constrói essa pasta.
2. **Build de PROJETO (`RegrBuildKind::Project`) é ÚNICO por `.tkr`** (`tkr_ensure_built`), com cache
   chaveada na FORMA do build e **ignorando o env** (`regr_built_serves`). Logo o env do PRIMEIRO
   cenário fixa a configuração de TODOS os cenários que correm off o build de projeto. Misturar
   cenários de rota nativa e rota C **no mesmo build partilhado** faria os seguintes correrem o
   binário errado em silêncio.
3. **`Given source = "<file>"` gera um build STANDALONE** (scratch), com manifesto sintetizado
   (`snippet_manifest_text`), cache por `(file, env)` (`regr_src_key`). Ou seja: **o env E o target
   PODEM variar por linha** quando a linha usa `Given source`. Vários cenários apontando o MESMO
   arquivo partilham UM build standalone. O arquivo é UM ficheiro só, dividido em declarações
   (→ `src/regr_decls.tks`) e statements (→ `main.tks`) por `split_snippet_decls_and_stmts` — **sem
   namespaces**: um snippet não pode carregar `ns::` cross-namespace.
4. **`Given target = "..."`** → build para outro alvo; para cross real, o RUN é honest-skip e
   `Then object well-formed` afirma que o objeto emitido tem o formato do alvo.
5. **`Given dependency = "dep"`** → build de dependência (crossmodule): `dep/` é compilado a `.tkl`,
   provisionado num `packages/` scratch, e o consumidor é buildado contra ele.
6. **`When compilation fails`** sem `Given source` → verificado contra o build de PROJETO (que tem de
   falhar). Com `Given source` → build standalone que tem de falhar.
7. **PARSE vs CHECKER é uma FRONTEIRA DURA e é LEI** (`parse_diagnostics.tkr` §3-8, dono): *"a parse
   error stops the compiler BEFORE the checker runs, so a checker-stage assertion can never fire in a
   build that fails in the parser, and a parse-stage assertion can never fire in a build that reaches
   the checker."* Um único build de projeto NÃO pode hospedar asserções de parser E de checker.
8. **Manifesto quebrado falha ANTES de lexar qualquer `.tks`** (`manifest_kind_unknown.tkr`). Um
   build standalone (`Given source`) sintetiza o SEU próprio manifesto e é IMUNE ao `.tkp` quebrado do
   hospedeiro.
9. **Convenção de MEMBRO do corpus** (`own_native/main.tks` + `scenario.tks`, guardada por
   `own_native_pairing_test.tkt`): um driver `main.tks` anuncia cada caso por NOME
   (`scenario("nome", f_probe(), esperado)` / `scenario_named_ok("nome", <bool>)` /
   `scenario_true("nome", <bool>)`) imprimindo `scenario <nome>: ok`; a linha do `.tkr` exige
   `Then stdout pattern = "scenario <nome>: ok"`. Os dois lados têm de nomear o MESMO conjunto.

**Corolário-chave do fold:** dentro de um projeto hospedeiro, cenários agrupam-se por CONFIGURAÇÃO de
build (target × env × sucesso/falha × estágio). O maior grupo de MESMA configuração vira o corpus
`src/` partilhado (membros, 1 build). Todo grupo de configuração distinta vira linhas
`Given source = "cases/<nome>.tks"` (1 build por ficheiro, env/target/veredito preservados). É
exatamente o padrão que `own_native` já ratificou.

---

## 1. Taxonomia FINAL validada (10 projetos)

| # | Projeto (dir) | `.tkp name` | Absorve | Config do corpus `src/` | Linhas `Given source` / especiais |
|---|---|---|---|---|---|
| 1 | `.` (raiz) | `teko` | — | `regressor.tkr` (intocado) | — |
| 2 | `own_native` | `own_native` | +native_union_known_stop, +native_union_nested_known_stop, +native_iface_fat_known_stop, +one_byte_native, +one_byte_c (e já: native_host_ffi) | nativo (existente) | one_byte_c (env C); native_iface_fat (compile-fail) |
| 3 | `mem` | `mem` | mem_block_dies, mem_str_scope, mem_free_arena_leak, mem_free_arena_ok, mem_free_slice_ok, mem_region_new_ok, mem_singleton_root (+DESIGN-AHEAD: mem_accum_block, mem_accum_return, mem_loop_per_iter) | rota C (família scope-memory) | 4 nativos (no-env); mem_free_arena_leak (compile-fail) |
| 4 | `iso` | `iso` | iso_intern_no_leak, iso_cov_no_leak | nativo (iso_cov) | iso_intern (env C) |
| 5 | `const` | `const_regr` | const_struct_ctor, const_slice_of_str | nativo (ambos membros) | — |
| 6 | `diagnostics` | `diagnostics` | +parse_diagnostics | checker (existente, ~44) | 24 parse (compile-fail) + signature-walk existentes |
| 7 | `builtins` | `builtins` | builtin_name_not_hijacked (1:1) | nativo (existente) | — |
| 8 | `manifest` | `manifest` | manifest_kind_unknown, seed_literal_arm | manifesto-quebrado (falha) | seed_literal_arm (positivo, imune ao `.tkp` quebrado) |
| 9 | `crossmodule` | `crossmodule` | crossmodule (1:1) | dependência (existente) | — |
| 10 | `syntax` | `syntax` | ref_mutable_binder (1:1) | nativo (existente) | — |

**Refinamentos sobre a proposta do dono (validados law-first, sem HALT):**

- **`const` como `.tkp name`:** `const` é palavra-reservada. O dir pode chamar-se `const` (FS aceita),
  mas o campo `name` do `.tkp` deve ser `const_regr` (ou `constants`) para não arriscar rejeição do
  leitor de manifesto / lexer. O `name` de uma fixture só serve para o seu próprio endereçamento e
  não é importado por ninguém, então o valor exato é livre. O que a lista de regressão referencia é o
  CAMINHO do `.tkr`.
- **`manifest ← manifest_kind_unknown + seed_literal_arm` é MECANICAMENTE SÃO** apesar do `.tkp`
  quebrado: `seed_literal_arm` entra como linha `Given source` (build standalone, manifesto
  sintetizado, IMUNE ao `.tkp` "binari"). Detalhe em §2.8. (Alternativa mais limpa tematicamente —
  mover `seed_literal_arm` para `syntax` — existe e é anotada em §6, mas como o dono fixou o par e o
  mecanismo é são, VALIDO o par como está.)
- **`diagnostics ← diagnostics + parse_diagnostics` respeita a fronteira dura parse/checker** (§0.7)
  hospedando os 24 casos de parser como linhas `Given source` compile-fail — NÃO no `src/` do build de
  projeto (que reporta o estágio checker). Custo honesto de builds em §5 e tensão em §6.

---

## 2. Mapa de fold — projeto a projeto

Notação: **survivor** = projeto existente reutilizado como hospedeiro (seu `.tkp` é editado, não
recriado). **[MOVE]** = fonte migra para `src/<ns>/` como MEMBRO. **[CASE]** = fonte é achatada num
único `cases/<nome>.tks` e vira linha `Given source`. Nenhuma asserção some — cada uma é rastreada.

### 2.1 `own_native` (survivor: `own_native`)

Config do corpus partilhado: **nativo, no-env** (primeiro cenário `own_arith_exit`).

- **native_union_known_stop** (nativo, stdout `42`) → **[MOVE]** `src/probe.tks` vira
  `f_native_union_scalar()` no `corpus.tks`; driver `main.tks` ganha
  `scenario("native_union_scalar", f_native_union_scalar() to i64, 42)`; `.tkr` ganha
  `Scenario: native_union_scalar` / `Then stdout pattern = "scenario native_union_scalar: ok"`.
- **native_union_nested_known_stop** (nativo, `42`) → **[MOVE]** idem, `f_native_union_nested()`,
  `scenario("native_union_nested", f_native_union_nested() to i64, 42)`.
- **one_byte_native** (nativo, `255`) → **[MOVE]** as 6 fns viram membros; `score()` vira
  `f_one_byte_score()`; `scenario("one_byte_native", f_one_byte_score(), 255)`. O molde de opacidade
  fold (`opaque_zero`) é o MESMO de `own_arith_exit`, então casa no corpus sem atrito.
- **one_byte_c** (rota C, `255`) → **[CASE]** `cases/one_byte_c.tks` (as mesmas 6 fns + `println`
  do score, achatadas), linha:
  `Given source = "cases/one_byte_c.tks"` + `Given env = ["TEKO_BACKEND=c"]` +
  `When built and run` + `Then stdout pattern = "255"`. É o oráculo de rota C; env distinto ⇒ build
  standalone (idêntico ao que `own_native` já faz nas linhas de guarda de cast em rota C).
- **native_iface_fat_known_stop** (compile-fail, "fat-pointer interface-dispatch result not yet
  lowered") → **[CASE]** `cases/native_iface_fat.tks` (probe achatado). Linha:
  `Given source = "cases/native_iface_fat.tks"` + `When compilation fails` +
  `Then diagnostic = "fat-pointer interface-dispatch result not yet lowered"`. NÃO pode ser membro:
  aborta o build partilhado bem-sucedido.

**Preservação de semântica:** os known-stops que CERRARAM (union/nested) já eram positivos
(`When built and run` / stdout 42) — viram membros idênticos. O known-stop AINDA ABERTO (iface_fat)
mantém-se como compile-fail isolado (`Given source`), preservando a instrução "promote no dia que
fechar" — quando fechar, vira membro do corpus (como fat_match_value já fez). Pairing test
(`own_native_pairing_test.tkt`) tem de ficar verde: cada `scenario(...)` novo em `main.tks` casa com
uma linha do `.tkr`.

### 2.2 `mem` (survivor: `mem_str_scope` renomeado → `mem`)

Config do corpus partilhado: **rota C, `env=["TEKO_BACKEND=c"]`** (família scope-memory). É o maior
grupo de mesma-config e para onde os 3 incoming também apontam.

**Membros do corpus `src/` (rota C, 1 build):**
- **mem_str_scope** (`ms`, `321`) — survivor; `run()` fica; namespace `ms`.
- **mem_block_dies** (`mb`, `9`) → **[MOVE]** `src/mb/mb.tks`.
- *(DESIGN-AHEAD)* **mem_accum_block** (`ab`, `15`), **mem_accum_return** (`ar`, `15`),
  **mem_loop_per_iter** (`lp`, `17`) → **[MOVE]** quando `move-on-return` mergir (§7).

Namespaces `ms/mb/ab/ar/lp` são disjuntos — sem colisão. Driver `mem/main.tks` adota a convenção de
token: para cada membro, `scenario_named_ok("mem_str_scope", ms::run() == "321")` etc., imprimindo
`scenario <nome>: ok`; `.tkr` casa com `Then stdout pattern = "scenario <nome>: ok"`. **A rota C é
fixada pelo env do PRIMEIRO cenário do corpus** (§0.2).

**Linhas `Given source` (config distinta):**
- **mem_free_arena_ok** (nativo, "arena freed") → **[CASE]** `cases/mem_free_arena_ok.tks`, no-env.
- **mem_free_slice_ok** (nativo, "b len=5 sum=10") → **[CASE]** `cases/mem_free_slice_ok.tks`.
- **mem_region_new_ok** (nativo, "region_new ok") → **[CASE]** `cases/mem_region_new_ok.tks`.
- **mem_singleton_root** (nativo, "root-anchor") → **[CASE]** `cases/mem_singleton_root.tks`.
- **mem_free_arena_leak** (compile-fail, "is dropped without being freed") → **[CASE]**
  `cases/mem_free_arena_leak.tks` + `When compilation fails` +
  `Then diagnostic = "is dropped without being freed"`. NÃO membro (aborta o build).

**Preservação:** os `#must_free unsafe type Arena` viram declarações do snippet (vão para
`regr_decls.tks`); o dataflow consume-or-fail é da mesma unidade de compilação → o diagnóstico dispara
igual. As fingerprints numéricas ("9","321","15","17") são independentes de rota; só a propriedade
UAF é que é exercitada na rota C — daí o env C do corpus.

### 2.3 `iso` (survivor: `iso_cov_no_leak` renomeado → `iso`)

Config do corpus: **nativo** (iso_cov é rota nativa por construção).

- **iso_cov_no_leak** (nativo, "iso-cov-no-leak 0 0") — survivor, corpus `src/iso/probe.tks`. Membro
  único do build de projeto: `main.tks` faz `scenario_named_ok("iso_cov_no_leak", iso::probe() ==
  "iso-cov-no-leak 0 0")`.
- **iso_intern_no_leak** (rota C, "iso-intern-no-leak MISS MISS") → **[CASE]**
  `cases/iso_intern_no_leak.tks` + `Given env = ["TEKO_BACKEND=c"]` +
  `Then stdout pattern = "iso-intern-no-leak MISS MISS"`. Rota C (intern builtins staged-off no
  nativo) ⇒ build standalone com env próprio.

> Nota: as duas fixtures usam namespace `iso`. Como iso_intern vira `Given source` (achatado, sem
> namespace), NÃO colide com o corpus `src/iso/` do survivor.

### 2.4 `const` (survivor: `const_struct_ctor` renomeado → `const`)

Config do corpus: **nativo** (ambos positivos nativos — MESMA config).

- **const_struct_ctor** (`reg`, `46`) — survivor, `src/reg/reg.tks`. **[membro]**
- **const_slice_of_str** (`tbl`, `96`) → **[MOVE]** `src/tbl/tbl.tks`. **[membro]**

Namespaces `reg`/`tbl` disjuntos. Driver:
`scenario_named_ok("const_struct_ctor", reg::…() == "46")` e
`scenario_named_ok("const_slice_of_str", tbl::…() == "96")`. **1 build para ambos.** `.tkp name =
"const_regr"` (evita palavra-reservada).

### 2.5 `diagnostics` (survivor: `diagnostics`)

Config do build de projeto: **checker-stage, falha** (inalterado). Os ~44 constructs checker-reject
continuam MEMBROS do build partilhado (um-namespace-por-construct sob `src/`), que reporta todos.

**parse_diagnostics (24 constructs parser-reject) → não podem partilhar o build de projeto** (§0.7).
Migram para `diagnostics/parse_cases/` (FORA de `source="src"`, à imagem do `cases/` de `own_native`)
e viram linhas `Given source` compile-fail:
- Cada `parse_diagnostics/src/cNN/case.tks` → `diagnostics/parse_cases/cNN.tks` (já é ficheiro único,
  isolado, sem cross-namespace — achata trivialmente).
- Linha por construct: `Given source = "parse_cases/cNN.tks"` + `When compilation fails` +
  `Then diagnostic = "<padrão cNN>"`. Todas as 24 preservadas verbatim.

**Otimização de build (verification-gated):** tentar PRIMEIRO um único `parse_cases/parse_rejects.tks`
concatenando os 24 constructs, com as 24 linhas apontando o MESMO ficheiro (1 build, cache por
file+env). Se — e só se — o parser reportar os 24 diagnósticos desse ficheiro (ritual de verificação
obrigatório, §5/§8), fica 1 build. Qualquer construct MASCARADO pela recuperação de erro do parser sai
para o seu próprio `parse_cases/cNN.tks`. Fallback garantido: 24 ficheiros = 24 builds (todos
preservam a asserção). O dono já abençoou builds extras para compile-fail.

**Signature-walk existentes** (`diagnostics/cases/prewalk_signature_stops.tks`,
`cases/target_boundary.tks`) permanecem linhas `Given source` como hoje — inalteradas.

**Marcador `EXPECT_COMPILE_FAIL`:** `diagnostics/EXPECT_COMPILE_FAIL` permanece (o build de projeto
ainda falha no checker). O de `parse_diagnostics/` é removido com o dir; seus consumidores externos
(ver §4) deixam de ver esse dir.

### 2.6 `builtins` (survivor: `builtin_name_not_hijacked` → `builtins`) — 1:1

Sem fold de conteúdo. Renomear dir + `.tkp`/`.tkr` para `builtins`. O projeto já é multi-namespace
(`mine`/`sys`) + extern `getpid` — fica intacto como build de projeto único. `Then stdout pattern =
"42"` preservado.

### 2.7 `crossmodule` (survivor: `crossmodule`) — 1:1

Sem alteração. Já é o único portador do build de dependência (`Given dependency = "dep"`). Mantém
`dep/` (`kind = "package"`). `Then stdout pattern = "42"`.

### 2.8 `manifest` (survivor: `manifest_kind_unknown` → `manifest`)

Build de projeto: **manifesto-quebrado, falha** (`.tkp` com `kind = "binari"`). Serve os 3 cenários
manifest-stage (`When compilation fails` sem `Given source`), inalterados:
- "unknown [artifact] kind", "binari", "accepted kinds: binary, static, shared, package".

**seed_literal_arm** (positivo, nativo, `123`) → **[CASE]** `cases/seed_literal_arm.tks` (as duas
grafias de braço literal, achatadas + `println`). Linha:
`Given source = "cases/seed_literal_arm.tks"` + `When built and run` +
`Then stdout pattern = "123"`. **Funciona porque o build standalone sintetiza o seu próprio
manifesto e NÃO lê o `.tkp` "binari" do hospedeiro** (§0.3/§0.8). O `EXPECT_COMPILE_FAIL` do dir
permanece (só descreve o build de PROJETO; `cases/` é invisível a ele e aos consumidores que buildam
o dir esperando falha).

> A doc de `seed_literal_arm` frisa "UM SÓ CENÁRIO E UMA SÓ ROTA": preservado — é uma única linha
> `Given source` de rota nativa; não partilha o build de projeto (que é compile-fail), logo não há
> conflito de env (§0.2 vs §0.3).

### 2.9 `syntax` (survivor: `ref_mutable_binder` → `syntax`) — 1:1

Sem fold de conteúdo. Renomear dir + `.tkp`/`.tkr` para `syntax`. Build de projeto nativo único,
`Then stdout pattern = "127"` (bit-score) preservado. As metades REJEITADAS da mesma lei já vivem em
`diagnostics` (c68/c69/c70) — continuam lá.

---

## 3. Nova lista `regression` do `teko.tkp` (10 entradas)

```toml
regression = [
    "regressor.tkr",
    "examples/regressions/own_native/own_native.tkr",
    "examples/regressions/mem/mem.tkr",
    "examples/regressions/iso/iso.tkr",
    "examples/regressions/const/const.tkr",
    "examples/regressions/diagnostics/diagnostics.tkr",
    "examples/regressions/builtins/builtins.tkr",
    "examples/regressions/manifest/manifest.tkr",
    "examples/regressions/crossmodule/crossmodule.tkr",
    "examples/regressions/syntax/syntax.tkr",
]
```

Ordem de execução = ordem da lista (owner ruling 2026-07-24: lista explícita, sem glob). `regressor.tkr`
primeiro (auto-regressão). Um caminho listado que não resolva é ERRO DE MANIFESTO (M.3) — logo a lista
só muda no MESMO crumb em que os `.tkr` de destino existem.

---

## 4. Diretórios a remover / renomear e consumidores a reconciliar

**Renomear (survivor → nome canônico):**
- `mem_str_scope` → `mem` (+ `mem_str_scope.tkp`→`mem.tkp` com `name="mem"`, `.tkr`→`mem.tkr`)
- `iso_cov_no_leak` → `iso`
- `const_struct_ctor` → `const` (`.tkp name = "const_regr"`)
- `builtin_name_not_hijacked` → `builtins`
- `manifest_kind_unknown` → `manifest`
- `ref_mutable_binder` → `syntax`
- (`own_native`, `diagnostics`, `crossmodule` mantêm o nome)

**Remover (absorvidos — conteúdo migrado para o hospedeiro):**
`native_union_known_stop`, `native_union_nested_known_stop`, `native_iface_fat_known_stop`,
`one_byte_native`, `one_byte_c`, `mem_block_dies`, `mem_free_arena_leak`, `mem_free_arena_ok`,
`mem_free_slice_ok`, `mem_region_new_ok`, `mem_singleton_root`, `iso_intern_no_leak`,
`const_slice_of_str`, `parse_diagnostics`, `seed_literal_arm`.
*(DESIGN-AHEAD, ao mergir move-on-return: `mem_accum_block`, `mem_accum_return`, `mem_loop_per_iter`.)*

**Remover (ÓRFÃO — achado adjacente, REPORTADO, não vira issue):**
`nproc_floor` — dir vazio (`src/np/` sem ficheiros), NÃO está na lista `regression`, NÃO é
referenciado por nenhum `.tkr`/`.sh`/`.tks`/`.tkp`. É lixo de um fold anterior. Recomendo deleção no
mesmo passo de limpeza. (Não conta no teto de 10 — nunca foi projeto ativo.)

**Consumidores que referenciam `examples/regressions/*` (verificar/atualizar — NÃO editar em
design-only; enumerados para o implementer):**
- `src/build/own_native_pairing_test.tkt` — guarda `own_native/main.tks` ↔ `own_native.tkr`; tem de
  ficar verde após os novos membros (ritual, §8).
- `src/build/fixture_guard.tks` / `fixture_guard_test.tkt` — caminho hardcoded
  `examples/regressions/own_native/src/corpus.tks`; verificar após MOVE.
- `src/build/regression_test.tkt`, `src/build/regr_group_test.tkt`, `src/build/project_test.tkt` —
  fixtures de teste que citam caminhos; reconciliar com os novos nomes.
- `scripts/known_stop_gate_test.sh` — cita `own_native` em logs sintéticos; confirmar que o fold dos
  known-stops não quebra o gate.
- `scripts/target_host_default_test.sh` — depende de "um nome de dir fixo sob examples/regressions/";
  confirmar qual e se sobrevive.
- Marcadores `EXPECT_COMPILE_FAIL` citam `sanitizers.yml` (loop native-build-all-fixtures) e
  `scripts/compile_fail_regressions.sh` como consumidores que buscam esses dirs — CONFERIR o estado
  real desses consumidores (a busca inicial não os localizou; podem estar renomeados/ausentes) e
  reconciliar com os dirs sobreviventes (`diagnostics`, `manifest`).
- `.github/PULL_REQUEST_TEMPLATE.md`, `scripts/install_share_runtime_test.sh`,
  `scripts/ar_link_run_consumer.c` — citam paths; verificar.

---

## 5. Projeção HONESTA de builds (projetos = 10 fixo; builds > 10 por design)

| Projeto | Builds de projeto | Builds standalone (`Given source`) | Total do projeto |
|---|---|---|---|
| regressor | 1 (+ snippets do próprio `regressor.tkr`, inalterado) | — | inalterado |
| own_native | 1 (partilhado, +3 membros) | +2 (one_byte_c, native_iface_fat) e os já-existentes cross/panic/C | 1 + N_existente + 2 |
| mem | 1 (corpus rota C, 2 membros hoje / 5 pós-move) | 5 (4 nativos + 1 compile-fail) | 6 |
| iso | 1 (iso_cov nativo) | 1 (iso_intern env C) | 2 |
| const | 1 (2 membros nativos) | 0 | **1** |
| diagnostics | 1 (checker, ~44 membros) | signature-walk (~2) + parse: **1 concat** (best) … **24** (worst) | 4 … 27 |
| builtins | 1 | 0 | 1 |
| manifest | 1 (compile-fail) | 1 (seed_literal_arm) | 2 |
| crossmodule | 1 (dependência) | 0 | 1 |
| syntax | 1 | 0 | 1 |

**Casos IRREDUTÍVEIS a build separado mesmo dentro do mesmo projeto** (o dono já admitiu builds extras
para compile-fail e cross-target):
1. **Rota distinta (env C vs nativo)** — o build de projeto é único por env (§0.2). Ex.: one_byte_c,
   iso_intern, os 4 nativos de `mem`, os 5 C-route de `mem` que não forem o corpus. Cada um é um build
   standalone.
2. **Compile-fail** — aborta o build; não pode coabitar um build bem-sucedido. Ex.: native_iface_fat,
   mem_free_arena_leak, os 24 parse-reject.
3. **Estágio parser vs checker** (§0.7, LEI) — os 24 parse-reject NÃO podem entrar no build checker de
   `diagnostics`; são standalone (concatenáveis a 1 se o parser reportar todos).
4. **Cross-target** — cada alvo cross é seu build (`own_native` já tem estes; inalterado).

A consolidação **reduz PROJETOS de ~24→10** (o anti-padrão 1:1 que o dono nomeou) mas o número de
BUILDS permanece ~da mesma ordem (cada fixture já era 1 projeto = 1 build). O único crescimento real de
builds é `diagnostics/parse` no pior caso (1→24) — mitigável pela concatenação verificada. É o preço
honesto do teto de 10 dado a fronteira dura parse/checker.

---

## 6. Riscos e tensões de lei — resolução

**T1 — `manifest` hospeda um positivo sob `.tkp` quebrado.** RESOLVIDO law-first via `Given source`
(build standalone imune ao `.tkp` do hospedeiro, §0.3/§0.8/§2.8). Sem HALT. *Alternativa mais limpa
tematicamente:* mover `seed_literal_arm`→`syntax` e deixar `manifest` 1:1. Como o dono FIXOU o par e o
mecanismo é são, mantenho o par; registro a alternativa para o dono decidir se preferir pureza
temática.

**T2 — `diagnostics + parse_diagnostics` cruzam a fronteira dura parse/checker (LEI).** RESOLVIDO
hospedando os 24 parse como `Given source` compile-fail, FORA do `src/` (§2.5). Preserva as 24
asserções. CUSTO: parse_diagnostics deixa de ser "1 build, 24 cenários" e passa a 1 (concat verificado)
… 24 builds. É a única tensão com custo material; não é HALT (resolução mecânica existe e o dono
abençoou builds extras de compile-fail). O ritual de verificação (§8) garante que nenhum diagnóstico
some na concatenação.

**T3 — Colisão de namespace ao MOVER membros.** `mem_str_scope`/`mem_singleton_root` usam ambos `ms`,
mas singleton vira `[CASE]` (achatado, sem namespace), então não colide com o `ms` do corpus.
`const`(reg/tbl), `mem`-corpus(ms/mb/ab/ar/lp), own_native(nomes novos distintos) — todos disjuntos.
MITIGAÇÃO: crumbs adicionam um membro de cada vez com gate (§8), localizando qualquer colisão.

**T4 — `own_native_pairing_test.tkt` exige main.tks ↔ `.tkr` iguais.** Cada `scenario(...)` novo tem
de casar com uma linha `Then stdout pattern = "scenario <nome>: ok"`. Risco de esquecer um lado ⇒ o
próprio teste falha (é a rede). Crumb de own_native adiciona os dois lados juntos + roda o pairing test.

**T5 — `const` palavra-reservada como nome de projeto.** MITIGADO: dir `const`, `.tkp name =
"const_regr"` (§2.4).

**T6 — Consumidores externos de caminhos de dir** (§4). Renomear/remover dirs quebra referências
hardcoded. MITIGAÇÃO: enumerados em §4; crumb dedicado de reconciliação antes do ritual final. Alguns
consumidores citados nos marcadores `EXPECT_COMPILE_FAIL` (sanitizers.yml, compile_fail_regressions.sh)
não foram localizados na busca — REPORTADO: confirmar estado real (achado adjacente, não vira issue).

**T7 — Perda de documentação-como-especificação.** Cada `.tkr` absorvido carrega blocos-doc densos
(known-stops, medições). A convenção "promote no dia que fechar" e o histórico têm de MIGRAR junto do
cenário (para `cases/*` a doc vive ao lado do `Scenario` no `.tkr`, pois um `cases/*.tks` não pode ter
bloco-doc de topo sem declaração — C7.9, e o snippet-split reescreve `main.tks` verbatim). NENHUM bloco
histórico é descartado; migra para junto da linha correspondente no `.tkr` hospedeiro.

Nenhuma tensão é IRRESOLÚVEL ⇒ **sem HALT**. Todas resolvem via mecanismo já ratificado (`own_native`)
+ a fronteira de estágio (lei citada).

---

## 7. DESIGN-AHEAD (bloqueado por dependência)

- **`mem_accum_block` / `mem_accum_return` / `mem_loop_per_iter`** vivem em
  `wt-move-impl` (`cargo/0.3.1.0-move-on-return-impl`), NÃO em `fix/union`. São rota C (env C),
  fingerprints `15`/`15`/`17`, namespaces `ab`/`ar`/`lp` (disjuntos do corpus mem). PLANO PRONTO
  (§2.2): entram como MEMBROS do corpus rota-C de `mem` — 3 namespaces `src/ab|ar|lp` + 3
  `scenario_named_ok(...)` no `main.tks` + 3 linhas no `mem.tkr`.
  **BLOQUEADO até o merge.** Quem mergir por ÚLTIMO (este fold ou o move-on-return) faz a absorção
  final; se `mem` já existir quando move-on-return chegar, o cargo do move-on-return apenas ADICIONA os
  3 membros ao `mem` existente em vez de criar 3 projetos novos (evita reabrir o anti-padrão 1:1).
  Coordenar via o integrador.
- **`native_host_ffi`** — JÁ folado em `own_native` pelo FFI. NÃO recriar, NÃO tocar.

---

## 8. Sequência ORDENADA de crumbs (behavior-preserving; cada um gate-able)

Cada crumb termina com `teko test .` verde e MESMOS vereditos; fixpoint intocado. Ritual = gate cheio.

**Fase A — projetos 1:1 (renomes puros, risco mínimo):**
1. **C1 `crossmodule`** — nenhuma ação (já canônico). *(gate: baseline verde.)*
2. **C2 `builtins`** — `git mv` dir + `.tkp`/`.tkr` de `builtin_name_not_hijacked`→`builtins`; editar
   `name`; atualizar a entrada na lista `regression`. *(gate: `teko test .`.)*
3. **C3 `syntax`** — idem para `ref_mutable_binder`→`syntax`. *(gate.)*

**Fase B — agregados de configuração-única (baixo risco):**
4. **C4 `const`** — survivor `const_struct_ctor`→`const` (`name="const_regr"`); MOVE
   `const_slice_of_str/src/tbl`→`const/src/tbl`; driver `main.tks` com 2 `scenario_named_ok`; `.tkr`
   com 2 cenários; remover `const_slice_of_str`; atualizar lista. *(gate.)*
5. **C5 `iso`** — survivor `iso_cov_no_leak`→`iso`; iso_intern→`cases/iso_intern_no_leak.tks` (env C);
   `.tkr`: 1 membro + 1 `Given source`; remover `iso_intern_no_leak`; lista. *(gate.)*
6. **C6 `manifest`** — survivor `manifest_kind_unknown`→`manifest`; seed_literal_arm→
   `cases/seed_literal_arm.tks`; `.tkr`: 3 compile-fail (inalterados) + 1 `Given source` positivo;
   remover `seed_literal_arm`; lista. *(gate: confirmar que o build de projeto AINDA falha no
   manifesto E que o `cases/` positivo passa.)*

**Fase C — own_native (membros + cases; guardado pelo pairing test):**
7. **C7** — MOVE native_union + native_union_nested como membros (probe→corpus, 2 `scenario(...)`, 2
   linhas `.tkr`); rodar `own_native_pairing_test`. *(gate + pairing.)*
8. **C8** — MOVE one_byte_native como membro (6 fns + `f_one_byte_score`). *(gate + pairing.)*
9. **C9** — CASE one_byte_c (env C) + CASE native_iface_fat (compile-fail); 2 linhas `Given source`.
   Remover os 5 dirs absorvidos de own_native; lista já aponta só `own_native.tkr`. *(gate + pairing +
   `scripts/known_stop_gate_test.sh`.)*

**Fase D — mem (corpus rota C + cases):**
10. **C10** — survivor `mem_str_scope`→`mem`; MOVE mem_block_dies (`mb`) como 2º membro rota-C;
    driver com 2 `scenario_named_ok`; `.tkr` 2 membros. *(gate.)*
11. **C11** — CASE os 4 nativos (arena_ok, slice_ok, region_new, singleton) + CASE mem_free_arena_leak
    (compile-fail). Remover os 6 dirs mem_* absorvidos; lista aponta `mem/mem.tkr`. *(gate.)*
    *(DESIGN-AHEAD: os 3 mem_accum entram aqui quando move-on-return mergir — §7.)*

**Fase E — diagnostics (a mais cara; verificação obrigatória):**
12. **C12** — mover `parse_diagnostics/src/cNN` → `diagnostics/parse_cases/` (achatados). TENTAR
    `parse_cases/parse_rejects.tks` concatenado. **RITUAL DE VERIFICAÇÃO:** rodar o build compile-fail
    e confirmar que os **24** diagnósticos aparecem; qualquer ausente sai para `cNN.tks` próprio.
13. **C13** — reescrever `diagnostics.tkr` para incluir as 24 linhas `Given source` compile-fail
    (verbatim os padrões `Then diagnostic`); remover `parse_diagnostics` e seu `EXPECT_COMPILE_FAIL`;
    lista dropa a entrada `parse_diagnostics`. *(gate: os ~44 checker + 24 parse + signature-walk todos
    disparam.)*

**Fase F — limpeza e ritual:**
14. **C14** — remover órfão `nproc_floor`; reconciliar consumidores de §4 (fixture_guard, testes de
    build, scripts). *(gate.)*
15. **C15 — RITUAL FINAL:** lista `regression` com EXATAS 10 entradas; `teko test .` verde;
    `own_native_pairing_test` verde; contagem de projetos = 10; nenhum veredito mudou; fixpoint
    intocado; `--no-verify` self-host inalterado (a fase de regressão só corre em `teko test .`, não no
    build semente→gen1, R0). *(RITUAL: gate cheio.)*

**Pontos de ritual (gate cheio obrigatório):** C6 (manifesto quebrado + positivo coexistem), C9
(pairing + known_stop gate), C12 (24 diagnósticos preservados), C15 (fechamento).

**Sequenciamento de semente:** o fold NÃO introduz nenhuma feature de linguagem nova — só move fontes e
edita `.tkr`/`.tkp`/lista. A semente publicada compila tudo isto (é o mesmo corpus, reorganizado). Sem
risco de semente.

---

## 9. Contratos de forma (o que o implementer escreve)

Snippets em Javadoc-completo, prontos a copiar. Assinaturas dos drivers dos NOVOS corpora
(`mem`, `iso`, `const`) seguem a forma de `own_native/main.tks` + `scenario.tks` — copiar
`scenario.tks`/`scenario_named_ok` do `own_native` para cada novo hospedeiro que precise (helpers de
projeto, não builtins).

Exemplo de driver-membro (a copiar/adaptar por hospedeiro):

```teko
/**
 * scenario_named_ok — announce a corpus case by NAME when its claim is a boolean, printing the
 * pairing token the `.tkr` row matches.
 *
 * @param name  the case name, identical on both `main.tks` and the `.tkr`
 * @param ok    the case's own pass/fail verdict, computed by its probe
 * @return void
 */
fn scenario_named_ok(name: str, ok: bool) {
    if ok { teko::io::println("scenario " ~ name ~ ": ok") }
    if !ok { teko::io::println("scenario " ~ name ~ ": FAIL") }
}
```

Cada `cases/<nome>.tks` (linhas `Given source`) NÃO leva bloco-doc de topo (C7.9 — um `/** */` sem
declaração após é erro de parse no `main.tks` sintetizado); a doc do caso vive ao lado do `Scenario`
no `.tkr` hospedeiro (padrão `own_native/cases/README.md`).

---

## 10. Fixtures de regressão sobre o PRÓPRIO fold (o que provar)

Não há novas fixtures de *linguagem* — o fold é reorganização. As invariantes a PROVAR (via `teko
test .` e o pairing test) são:
- Projetos na lista `regression`: **exatamente 10**.
- Todo veredito preservado byte-a-byte: mapear cada asserção antiga → sua nova localização (a tabela
  §2 é esse mapa; nenhuma linha sem destino).
- `own_native_pairing_test.tkt` verde (main ↔ `.tkr` iguais).
- Build de projeto de `manifest` AINDA falha no leitor de manifesto; build de projeto de `diagnostics`
  AINDA falha no checker reportando todos; os 24 parse-reject disparam (C12).
- Nenhum dir `examples/regressions/*` órfão remanescente (`nproc_floor` removido).
