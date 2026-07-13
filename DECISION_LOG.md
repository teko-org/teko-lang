# Teko — Decision Log

Registro de decisões tomadas de forma autônoma durante a execução do backlog, para
**revisão posterior do dono** (diretriz 2026-07-05: aplicar o recomendado sem travar o
fluxo; registrar aqui para revisar de uma vez no gate **LTS = v1.0.0.0**).

Cada entrada traz: a **decisão aplicada**, as **alternativas** preteridas (com motivo), a
**base** (constituição / lei / diretriz — lembrando que estamos na **fase de EVOLUÇÃO**,
pós-reboot) e a **reversibilidade**. Decisões marcadas **⚠️ PENDENTE** aguardam a revisão
do dono especificamente.

Constituição: Laws M.0–M.5 (ver `TEKO_MASTER_PLAN.md` / memória `project-structure`).
Lei suprema operacional: **main-integrity** — nunca mergear para main algo potencialmente
corrompido; só CLEAN.

---

## 2026-07-05 — Redesign do release + reorganização do CI (#249, PR #277)

### D1 · Ferramenta de cross-compile Linux no release → **`zig cc`** ✅
- **Aplicada:** um runner emite o `teko.c` uma vez e `zig cc -target <triple>` cross-compila
  os 6 artefatos Linux (glibc dinâmico + musl estático × x86_64/arm64/riscv64).
- **Alternativas:** (a) runner nativo por arquitetura + `musl-tools`/cross-gcc — musl para
  arm64/riscv é frágil e riscv não tem runner GitHub; (b) qemu por-arch — lento e mais
  peças; (c) imagens docker cross — mais dependências externas. `zig` unifica tudo com uma
  ferramenta e sem emulação.
- **Base:** economia + robustez; sem violar nenhuma lei (o binário é o mesmo C determinístico
  que o CI já provou). **Validado localmente** (zig 0.16 + docker): os 6 compilam com a
  arquitetura correta; arm64 musl/glibc rodam `teko --version`.
- **Reversível:** sim — trocar o job `build-linux` por runners nativos é local ao release.yml.

### D2 · Release NÃO re-valida (1 geração, `--no-verify`) ✅
- **Aplicada:** o release builda uma geração e publica; sem gate `.tkt`, sem gen-2/gen-3,
  sem fixpoint, sem diff VM==native.
- **Alternativa:** manter a cadeia gen-1→2→3 + asserts por plataforma (desenho antigo).
- **Base:** **ruling do dono** "o CI é quem garante, o release não assera"; o fixpoint e o
  gate já rodam no CI do commit. Codegen determinístico ⇒ 1 geração É o artefato-fixpoint.
- **Reversível:** sim.

### D3 · CI roda **só em `pull_request`** (não em `push: main`) ✅
- **Aplicada:** `native`/`sast`/`sanitizers`/`codeql` perderam o trigger `push: main`.
- **Alternativa:** (a) manter CI no push da main (custo dobrado por merge); (b) merge-queue
  (mais robusto contra merge-skew, porém mais complexo — adiado).
- **Base:** **ruling do dono** "se o merge veio de um PR com CI 100%, não re-rodar o CI, só
  o build release". A proteção de branch já exige os checks verdes p/ mergear ⇒ todo commit
  na main já passou 100%.
- **⚠️ Risco registrado (merge-skew):** dois PRs verdes isoladamente, mergeados em sequência,
  podem combinar em uma main nunca testada junta. **Mitigação recomendada ao dono:** ligar
  "Require branches to be up to date before merging" no ruleset (re-roda o CI no PR rebaseado).
- **Reversível:** sim (readicionar `push:`).

### D4 · Guard do release = confiar na proteção de branch (removido o poll ci-green) ✅
- **Aplicada:** como o CI não roda mais no push da main, o release não tem run de CI na main
  para consultar; ele confia que "estar na main" ⇒ veio de PR verde.
- **Alternativa:** job `ci-green` que faz poll dos runs de CI do commit (desenho intermediário
  desta mesma sessão) — inútil sob D3 (não haveria runs na main para consultar).
- **Base:** consequência direta de D3; a garantia migra para a proteção de branch.
- **Reversível:** sim.

### D5 · riscv64 vira **BLOQUEANTE** no job `gate` do native.yml ✅
- **Aplicada:** o `gate` agora FALHA se `riscv64-qemu` falhar (antes: "any result is fine").
- **Base:** **lei main-integrity** — "verde é verde"; o #276 já tornou o job
  `continue-on-error:false` e determinístico (seed do release + cross-compile), então uma
  falha é regressão real, não ruído.
- **Reversível:** sim, mas contra a lei — não deveria.

### D6 · Runner do host Linux = `ubuntu-latest` (não `ubuntu-26.04`/kernel-7) ⚠️ PENDENTE
- **Aplicada:** o job zig-host roda em `ubuntu-latest`.
- **Contexto:** o dono pediu **kernel 7 / Ubuntu 26.04 em todo ponto Linux** ("24.04 tem
  falha de segurança no kernel"). Com **zig-cross**, o artefato Linux é **independente do
  kernel do runner** (o zig embute o libc/headers do target), então o binário publicado NÃO
  herda o kernel do host — a preocupação de segurança do 24.04 não alcança o artefato.
- **Alternativa:** pinar `ubuntu-26.04` — **risco:** não confirmei que o label existe como
  runner hospedado; um label inexistente QUEBRA o release (outward-facing). Não arrisquei
  sem confirmar.
- **⚠️ Revisão do dono:** confirmar disponibilidade do runner `ubuntu-26.04`; se existir e
  ainda quiser, é troca de uma linha. (Vale reavaliar também para `native.yml`.)
- **Reversível:** sim (uma linha por job).

### D7 · CodeQL — dashboard de segurança da default branch ⚠️ PENDENTE
- **Aplicada:** ao tirar `push: main` do CodeQL, o dashboard da branch default deixa de
  atualizar por-merge (a análise de PR continua). Nota deixada no `codeql.yml`.
- **Alternativa:** adicionar `schedule:` semanal ao CodeQL para refrescar o dashboard barato.
- **⚠️ Revisão do dono:** quer o `schedule:` semanal? (Trade-off custo × frescor do painel.)
- **Reversível:** sim.

### D8 · Split libc — glibc **dinâmico** / musl **estático**; glibc pinado a **2.28** ✅
- **Aplicada:** artefatos glibc linkam dinâmico (portável para glibc ≥ 2.28); musl linka
  totalmente estático (roda em qualquer lugar, ex.: Alpine).
- **Base:** padrão de mercado; 2.28 (Debian 10) cobre distros correntes sem símbolos faltando
  no runtime (que só usa libc/libm antigos + `__int128` do compilador).
- **Reversível:** sim (parâmetros do target no release.yml).

### D9 · Runtime musl-portável — guardar `execinfo.h`/`backtrace` sob `TK_HAVE_BACKTRACE` ✅
- **Aplicada:** os includes/chamadas de backtrace no `teko_rt.c` passam a exigir
  `TK_HAVE_BACKTRACE` (só `__APPLE__`/`__GLIBC__`). Bug pego na validação local: o 2º include
  (#148 RA2) e as chamadas em `tk_slice_push_fo` não eram guardados → quebravam os 3 musl.
- **Base:** correção de portabilidade necessária para D1; sem mudança de comportamento em
  macOS/glibc/Windows. `dladdr`/dlfcn (existe no musl) fica sob `!_WIN32`.
- **Reversível:** sim, mas reverter re-quebra o musl.

---

## 2026-07-05 — Fix do release + validação sem qemu (PR ci/fix-zig-riscv, 0.0.1.24)

Contexto: o release 0.0.1.23 (primeiro de 9 targets) FALHOU no `build-linux` — o zig **0.13.0**
tem o glibc de riscv64 incompleto (falta `gnu/stubs-lp64d.h`), quebrando `linux-riscv64-glibc`.
Causa raiz do meu erro: validei local com zig **0.16.0** mas pinei **0.13.0** no CI (versões
diferentes). O #277 já estava mergeado → correção via novo PR (nunca direto na main).

### D1 (adendo) · Versão do zig = **0.16.0** (a que atende TODOS os targets) ✅
- **Aplicada:** `ZIG_VERSION=0.16.0`. **Busca definitiva** (não tentativa-erro): 0.16.0 é o
  stable mais recente E seu tarball contém `riscv-linux-gnu/gnu/stubs-lp64d.h` (confirmado por
  `tar -tJf`; 0.13.0 NÃO tem). Compila os 6 targets (validado local no 0.16 + download/versão
  conferidos em container linux/amd64). Corrigido também o nome do tarball: 0.14.1+ usa
  `zig-x86_64-linux-<v>` (os/arch trocados vs o formato antigo `zig-linux-x86_64-<v>`).
- **Alternativas:** 0.13.0/0.14.0 (riscv glibc incompleto/incerto — descartados por evidência).
- **Fallback futuro (diretriz do dono):** se o zig algum dia NÃO atender um target, usar o `cc`
  específico da arquitetura/SO para aquele target (por-arch), mantendo zig para os demais.

### D10 · Smoke de cross-compile no CI de PR (`release-cross-smoke`) ✅
- **Aplicada:** novo job em `native.yml` roda o MESMO `scripts/cross_compile_linux.sh` (extraído
  do release, fonte única — sem drift) em modo `smoke`: emite teko.c e cross-compila os 6 Linux,
  conferindo a arquitetura de cada um. Bloqueante no `gate`.
- **Por quê:** `release.yml` é disparado por tag, então sua parte Linux não rodava no CI de PR —
  foi por isso que a quebra do 0.13.0 só apareceu PÓS-merge. Agora uma quebra de release
  (versão do zig / target / portabilidade do runtime) gateia o merge. Custa ~1 runner de compile
  por PR relevante; o dono priorizou não publicar release quebrado.
- **Reversível:** sim.

### D11 · Validação por COMPILE + arquitetura, SEM qemu (diretriz do dono) ✅
- **Aplicada:** para validar os binários cross NÃO se roda o binário sob qemu; cross-compile é
  determinístico, então **compilar com sucesso o teko.c (que é o fixpoint byte-idêntico provado
  pelo CI) + conferir a arquitetura via `file`** É o argumento de correção. Removido o smoke por
  qemu que eu vinha usando.
- **Base:** diretriz do dono ("se tem cross-compile, não precisa qemu; verifica por paridade de
  byte") — o C é o mesmo fixpoint; a tradução C→binário por target é determinística.
- **Reversível:** sim (readicionar execução sob qemu se algum dia quisermos runtime-check real).

---

## 2026-07-05 — Fix do miscompile zig + drenagem do backlog validado (#281, #257/#251/#264/#260)

### D12 · Release Linux via zig = `-O0 -fno-sanitize=undefined -DTEKO_VERSION_STRING` ✅
- **Aplicada:** o `cross_compile_linux.sh` casa as flags do build normal do teko (`run_cc`): sem `-O`, define de versão. Bisecção (agente + VPS x86_64 real) provou que o `-O2` do zig explora uma UB no teko.c gerado → miscompila o checker; o compilador está INOCENTE.
- **Alternativas:** manter `-O2` (miscompile); voltar Linux a build nativo por-arch (perde a unificação zig p/ musl/riscv — descartada por ora).
- **Reversibilidade / follow-up:** re-habilitar `-O1/-O2` exige achar+corrigir a UB → issue **#283**. Por ora `-O0` (como todo release nativo sempre foi).

### D13 · Seed AUTO-CURÁVEL (version-check) ✅
- **Aplicada:** `ci_provision` caminha os releases newest-first e rejeita seed cujo binário reporta versão ≠ tag (o 0.0.1.24 zig reportava `0.0.0.0-dev`) → cai pro próximo bom. Contorna o **release imutável** (não deu pra despublicar o 0.0.1.24 ruim). Validado em x86_64 real.
- **Base:** lei main-integrity (nunca seedar de algo corrompido) + robustez contra recorrência.

### D14 · Smoke de release RODA o artefato (não só compila) ✅
- **Aplicada:** o `release-cross-smoke` executa o binário x86_64-glibc sobre o corpus (nativo ao runner, sem qemu). Um smoke compile-only nunca veria um miscompile. É a regressão que teria pego o bug.

### D15 · Merge-skew da DI: defaultar campos DI nos literais ao re-sincronizar ✅
- **Aplicada:** ao re-sync features onto a main pós-DI, os literais `TypeDecl`/`Function` (que a DI ganhou campos `di_kind`/`has_inject`/…) recebem defaults (`DiKind::None`/`false`/empty). Padrão: sed guardado por `/di_kind/!` só nos que faltam + fechar chaves em concatenações de teste. Semanticamente inerte (codec/testes não usam DI).

### D16 · Gate `teko fmt --check` STAGED (desabilitado até o seed ter o CLI) ✅
- **Aplicada:** o #260 comenta o gate no native.yml — o seed (release anterior) não tinha o CLI `fmt --check`. Liga no PR após o release que o carrega (0.0.1.29) → issue **#282**. Mesmo bootstrap-handling da auto-cura do seed. O `fmt_cli_test.sh` já testa o feature no gen1.

### D17 · `.gitattributes eol=lf` (cross-plataforma) ✅
- **Aplicada:** o Windows fazia checkout dos `.tks` como CRLF → o `fmt` (LF canônico) via o corpus não-idempotente → panic. `* text=auto eol=lf` fixa LF no checkout em toda plataforma. Blobs já eram LF no git; só faltava forçar no working-tree. (CI multi-plataforma pegou o que a validação macOS/Linux não via.)

---

## 2026-07-05 — TR3: traits estruturais Eq/Ord/Hash/Clone/Default sintetizados (#177)

### D18 · `Hashable`≡`Hash` e `Comparable`≡`Ord` como SINÔNIMOS (sem interface paralela) ✅
- **Aplicada:** `is_structural_trait` reconhece `Hashable`/`Comparable` como sinônimos de `Hash`/`Ord`; `structural_trait_canonical` os colapsa. A chave-de-Map `<K: Hashable & Eq>` resolve contra um deriver de `Hash`+`Eq` via `type_conforms_to` (o nome canônico fica no `implements` folded). NÃO se introduziu interface `Hashable`/`Comparable` paralela.
- **Alternativas:** criar interfaces nativas `Hashable`/`Comparable` (duplica capacidade + descasa trait-vs-interface); renomear a ruling de collections agora (fora de escopo do #177).
- **Base:** M.0 (no-reflection) + design de traits §2 (structural derives cobrem encoding/collections sem contrato separado) + §5 (conjunto fechado). O trait estrutural É a capacidade.
- **Reversível:** sim (uma linha em `is_structural_trait`). Reconciliar a memória `teko-collections-rulings` p/ "structural traits" quando #180/collections aterrissar — edição de 1 linha, opcional do dono.

### D19 · `Ord` sintetiza `-1` como `Unary(Minus, 1)`, não `Number{value=-1}` ✅
- **Aplicada:** o builder `mk_neg_int` produz o literal negativo como unário-menos sobre `1` positivo (o shape que o parser emite), porque `codegen::cb_i128` faz `v to u128` no carrier — e o guard F3 rejeita negativo→unsigned ("impossible conversion"). Um `Number{value=-1}` direto é o PRIMEIRO a exercitar esse caminho.
- **Base:** M.1 (fail-loud, não quero corromper) + não tocar o codegen congelado de literais.
- **Reportado (adjacente, NÃO nova issue):** `cb_i128` (codegen.tks:149) tem bug latente: `(v to u128)` num i128 negativo faz panic sob o guard F3; nunca disparou porque literais negativos do source são `Unary(Minus, N)`. Follow-up p/ o integrador sequenciar.

### D20 · str-field Hash/Ord via `tk_str_hash`/`tk_str_cmp` (o seam de runtime C permitido) ✅
- **Aplicada:** adicionados `tk_str_hash` (FNV-1a, casa `di_type_id`) e `tk_str_cmp` (lexicográfico unsigned) a `teko_rt.{c,h}`, com gêmeos puro-Teko em `teko_rt.tks`, registro no checker (`scope.tks`), dispatch em `codegen.tks` (→ `tk_str_*`) e intercept no VM (`vm.tks`). VM==native garantido pelos dois gêmeos.
- **Base:** ruling no-mirroring — `teko_rt` é o C mantido (não-twin, runtime); o resto é `.tks`.

### Reportados-up (adjacentes, NÃO novas issues)
- `==` em dois structs type-checka e emite C inválido (expr.tks:19 + codegen.tks) — latente, pré-existente; questão de operator-overloading separada. TR3 NÃO auto-baixa `==`→`.eq()`.
- Slice/Optional/enum sob derive estrutural são honest-stop em v1 (M.1); follow-up TR3.1 natural quando #178 (Json) aterrissar.
- `cb_i128` negativo (ver D19).

---

## 2026-07-05 — Reorganização do backlog por dependência (fases "para trás e fora de ordem")

### D21 · Ondas de dependência sobrepostas às fases; keystone da fase-1 destrava a fase-3 ✅
- **Contexto (o que o dono viu):** fase-1-linguagem 25% / fase-2-packaging 22% / **fase-3-stdlib 2%** (33 abertas) / fase-4 16% / fase-5 50% / fase-6 0%. Merges oportunistas (unblocked-first, monomorphic-first) deram aparência de "fora de ordem": tooling e stdlib-roots aterrissaram à frente de fechar a linguagem.
- **Diagnóstico:** as fases NÃO são topo-ordenadas de fato — um **keystone da fase-1** (cluster monomorfização+128-bit) é o PORTÃO de toda a fase-3 genérica. Enquanto não fecha, 33 issues de stdlib ficam represadas. O monomorphic-first foi deliberado (camada genérica parada atrás do cluster), mas os labels não comunicavam.
- **Aplicada:**
  1. Criado label `keystone`; marcado o cluster #254/#290/#294/#296/#299/#301.
  2. Rotuladas 9 issues sem-fase (#158/#159→fase-2; #163→fase-3; #164/#283→fase-1; #167/#168/#282→fase-6; #233→fase-5).
  3. Espinha de **milestones = ondas de dependência** (delegada à teko-docs) sobre os labels de fase.
  4. Ordem corrigida ratificada (abaixo).
- **Ordem corrigida (recomendação, base: dep-DAG + "o VPS/site é DEPOIS" nas palavras do dono → fundação primeiro):**
  - **Onda 2 (fechar):** #300 (#184) — em rework (review HALTou: tee corrompe dados + flat_map/tee-iterator/compress omitidos → issue-100%).
  - **Onda 3 (KEYSTONE):** sub-cluster A `#290→#301→#254→#294` + sub-cluster B `#296∥#299`. Destrava fase-3 genérica + coleções #163 + parte de async #164.
  - **Onda 4:** fase-1 cleanup independente (#171/#172/#173/#174) + cadeia de traits #178→#179.
  - **Onda 5:** fase-3 stdlib flui (coleções #163 primeiro → math/encodings/compress/crypto/net roots).
  - **Trilha paralela (fillers near-term):** higiene de release/dist #267/#159/#282/#283 (servem o pipeline JÁ ativo); packaging pesado #180/#218-220 e site/servidor de pacotes = DEPOIS (palavra do dono).
- **Alternativas registradas:** (B) puxar fase-2 packaging/dist + site teko-lang.cloud para a frente em paralelo à onda-3 — REJEITADA por ora (dono disse "esse VPS DEPOIS"); (C) priorizar amplitude de stdlib visível (net/http/db/web) para demos — adiada (depende da onda-3 para a camada genérica).
- **Base constitucional:** issues-must-be-100% + backlog-deve-convergir + main-integrity; dependência força keystone-antes-de-dependentes (não é escolha de produto, é topo-ordem). A única escolha de produto (fundação vs site-primeiro) resolvida pela palavra do dono ("depois").

### D22 · #294 (struct sob `<T: Contract>`): constraint É gate de monomorfização, não promoção a dispatch dinâmico ✅ (do architect, law-first)
- **Aplicada:** um struct constrangido por `<T: Contract>` despacha DIRETO ao método concreto estampado (precisa do #254 antes); o fat-pointer/vtable segue exclusivo de `class`, casando o design OOP já assentado. NÃO se promove struct-constrangido a vtable dinâmica.
- **Base:** design OOP assentado (vtable = ref-semantics de class) + monomorfização (constraint = prova em tempo de estampagem). Registrado para revisão no gate LTS. Residual (struct-como-VALOR-de-contrato em slot) reportado, não expandido.

---

## 2026-07-05 — #184 (#300): fix do tee + descoberta de que flat_map é bloqueado pelo keystone

### D23 · #184 é vítima do keystone: núcleo monomórfico fecha, `flat_map`/tee-lazy sequenciam com #301 ✅
- **Contexto:** review adversarial do #300 HALTou com (1) `tee_write_fn` corrompendo dados em sinks assimétricos [bug real] e (2) `flat_map`/tee-iterator/compress "omitidos" [issue-100%].
- **Investigação (leituras, sem build):** o PARKED doc do `iter.tks` já documenta com repro empírico que `flat_map` precisa carregar um iterator interno (closure) em estado mutável (`Ref` cell / campo `IntIter?`) — que é EXATAMENTE o **#301** (closure-in-Ref/optional não round-trip; codegen não mangla optional/slice de function-type; VM dropa closure re-assentada num Ref). `compress_stream.tks` é construível (byte-state `Ref<MemWriter>` + `write_zip`/`read_zip`), só falta um teste exercitando o round-trip.
- **Aplicada:**
  1. **Fix do tee (bug real):** `tee_write_fn` agora drena 100% da região em CADA sink via `write_all` antes de reportar consumo → nunca re-oferece cauda → sem double-feed em sinks assimétricos. Docstring reescrito. Checkpoint commitado local em `fix/issue-184-resync` (NÃO pushado — pega-leve, sem CI até o batch com #301).
  2. **flat_map / fold genérico / tee-lazy de iterator:** NÃO forçados — bloqueados pelo #301. PARKED doc atualizado p/ citar #301 explicitamente.
  3. **Escopo #184:** entrega o núcleo monomórfico (IO0 streams c/ tee corrigido + ITER0 adapters/terminals + IO1 file copy); o remanescente (`flat_map`, tee-lazy, + teste assimétrico do tee, + teste round-trip do compress) sequencia com #301 na onda-3, validado num único CI, então #184 fecha 100%.
- **Base constitucional:** issues-must-be-100% NÃO exige entregar o que o compilador não compila (bloqueio de capacidade = dependência legítima, reportada+folded, não omissão). main-integrity: o bug do tee é real mas vive num PR aberto (não em main) — corrigido; #184 NÃO mergeia até 100% (pós-#301). Alinha com D21 (keystone antes de dependentes).
- **Correção ao review:** o achado "flat_map omitido silenciosamente" foi sobre-sinalização — é deferral documentado e bloqueado, não narrowing. (O achado do tee estava 100% correto.)
- **Reversível:** o fix do tee é independente e correto por si; o resto é aditivo pós-#301.

---

## 2026-07-06 — Compile-time: CI quickwins + gate nativo (VM-out) + plano-mestre do backlog

### D24 · CI 16m→~6m: desabilitar riscv/windows-arm + un-double do gate (#306) ✅
- **Contexto:** o dono flagou 16m31s inaceitável. Architect achou: o 16m é AUTO-INFLIGIDO — o gate nativo (#265, opt-in) rodava como 2º gate em TODA plataforma → cada uma rodava os 863 `#test` DUAS vezes (VM+nativo). Caminho crítico = windows-arm64 973s.
- **Aplicada (#306, merged):** windows-arm64 comentado da matriz build-test (pendência #304); riscv64-qemu `if: false` (rodava `test .--coverage` inteiro sob qemu ~8-9m, 85% execução emulada — pendência #305); gate nativo restrito a linux-x86_64 (un-double). Projeção 16m→~6m. Só `native.yml`, sem bump.
- **Base:** a "All Green" ruleset NÃO exige checks por nome (verificado) → desabilitar jobs não trava merge; o `gate` job trata `skipped` como pass. As duas lanes são PENDÊNCIAS de suporte (#304/#305), não deleções.
- **Alternativas registradas:** smoke de arch em vez de comentar (dono preferiu comentar por hora); nightly.

### D25 · VM fora dos testes = destino via #265+#168; até lá VM é o gate (phasing) ✅
- **Aplicada:** ruling do dono (VM out dos testes, [[teko-native-test-gate]]) é o DESTINO, realizado quando o gate nativo for rápido+completo (#168 compile-once + #265 line/branch cov nativo), então ele SUBSTITUI o VM em tudo. O `native.yml:76` já documentava a ruling de 2026-07-05 ("native regresses build time until #168"), então "siga o que disse antes" = essa posição estabelecida. Interim: VM é o gate de piso de cobertura.
- **Base:** o gate nativo HOJE é mais lento (emit+cc por gate) + só mede cobertura de função → cortar o VM agora regrediria tempo+cobertura. #168+#265-cov consertam antes do corte.

### D26 · Plano-mestre de drain + 5 chamadas autônomas (workflow read-only) ✅
- **Aplicada:** `docs/design/backlog-drain-master-plan.md` (DAG + Batches 0→8 + ready-set de 32 issues + notas). Ordem recomendada: Batch 0 (in-flight) → **K-B (gate nativo, CI mais leve p/ todo o resto)** → K-A (monomorfização #290→#254→#294) → onda-4 → roots stdlib → famílias → qualidade (#234 por último).
- **Chamadas autônomas (law-first, para revisão LTS):** (1) #294 = constraint é gate de monomorfização, não vtable; (2) #265 A5 = `tk_cov_line_at`/`tk_cov_branch_at` no seam `teko_rt` (não-twin, crescimento permitido); (3) K-B antes de K-A (CI mais leve = ganho de todo o backlog); (4) #184 tratado como keystone apesar de un-milestoned (destrava 6+ folhas onda-5); (5) #304/#305 NÃO bloqueiam o drain (viram nightly se precisarem de fix upstream).
- **Decisões ABERTAS que preciso da sua régua antes do batch relevante:** #174 regex NFA-vs-backtracking (bloqueia Batch 3.3 — recomendo NFA por segurança/sem backtracking catastrófico); #254 layer-4 `Env.expected_ret` (alta rotatividade); #233 LSP sem gate de início; #182 TCC/#267-item1 diferidos pós-alpha.
- **Correção de ground-truth:** 74 abertas (não 73); o design pai `onda3-monomorphization-cluster.md` SUB-CONTA sites nos 4 roots → confiar em `drain-onda3-subcluster-A.md`.

---

## 2026-07-06 — OOP syntax: `this` / `base` / `static` (pedido do dono, feedback de dev)

### D27 · `this`/`base`/`static` = rename SÓ de front-end (codegen+VM byte-idênticos) — design pronto, 1 HALT
- **Contexto:** dono (2026-07-06) pediu trocar o receiver (1º arg solto sem tipo) por `this`, o `class Base(binding)` por `base`, e adicionar `static` explícito. Dev achou a sintaxe atual confusa.
- **Ground truth (verificado):** receiver = `params[0]` com `has_type=false`, NOME escolhido pelo autor (`self` hoje); codegen/VM leem `params[0]` POSICIONALMENTE (nunca casam a string) → renomear é fixpoint-neutro. Base-binding já é `let <binding>: <Base> = <this upcast>` sintético no typer (`typer.tks:3110-3128`). Static/instance é `params.len==0 || params[0].has_type` em TODO lugar (`di.tks:119`, `typer.tks:745`, `collect.tks:721`).
- **Keystone de implementação:** o parser INJETA um `Param{name="this"; has_type=false}` sintético p/ método não-`static` → preserva o invariante inteiro do checker, então **#254 (métodos genéricos) + #294 (constraint dispatch), que leem o modelo de receiver atual, precisam de ZERO mudança**.
- **Autônomo (law-first, p/ revisão LTS):** (1) `static` = RESERVADA (sem colisão; M.2); `this`/`base` = CONTEXTUAIS — `base` é nome de local VIVO em `driver.tks`/`resolve.tks`/`zlib.tks`, reservar quebraria produção; (2) add `"this"` ao `cg_is_c_keyword` (kw de C++, não C — fixpoint-neutro); (3) tamanho = **L** (não XL): produção tem 0 classes/4 interfaces/0 traits; massa OOP está nos `.tkt` + `synth.tks` (~89 sites de receiver); codegen/VM intocados.
- **Base constitucional:** M.2 explícito + M.3 honesto = GANHO (torna receiver/staticness visíveis vs convenção implícita do 1º arg solto). Consistente com no-`ref`-keyword, modelo no-GC/arena/Ref-por-lowering (`this` = receiver pointer-lowered arena-backed), e `teko-default-args-named-call` (receiver ainda anda em args[0] sem nome). Sem tensão de Lei.
- **HALT (precisa da régua do dono):** **hard-cut (A) vs transição dual-syntax (B).** Recomendo **(A) hard-cut ANTES da enxurrada fase-3 de coleções**: migração é pequena+codegen-neutra, gramática dual mantém viva a implicitness que o dono quer remover (cheiro M.2/M.3), e #163 (coleções, ABERTO, sem árvore `src/collections` ainda) deve ser escrito já na sintaxe nova, não duas vezes. (B) só p/ proteger #163 em voo.
- **Doc:** `docs/design/oop-this-base-static.md`. Memória: `teko-oop-this-base-static-design`.

### D27-owner · RATIFICAÇÃO (dono 2026-07-06): OOP this/base/static = HARD-CUT ✅
- **Decisão do dono:** aprovada a opção A (hard-cut) da revisão de arquitetura (D27 / docs/design/oop-this-base-static.md). **Justificativa do dono:** "ainda não temos a LTS e nem código em produção, logo, há coisas que podemos remodelar se assim for melhor" — pré-LTS + zero código em produção = SEM dívida de backward-compat → o hard-cut limpo vence a transição dual-syntax (que só carregaria complexidade de parser duplo sem necessidade).
- **Timing vs #163:** deixar o #163 (coleções, in-flight) fechar na sintaxe ANTIGA; o PR do hard-cut reescreve todo o corpus (incl. #163) atomicamente via codemod mecânico (rename self→this + drop base-binding + add static). Sem desperdício, sem re-escrita humana.
- **Base:** é renomeação PURA de front-end (receiver=params[0] posicional; base já é `let <bind> = <this upcast>` sintético) → codegen/VM idênticos → **fixpoint-safe** (#254/#294 zero mudança). Tamanho L. Risco = codemod perturbar codegen → mitigado por rename-só + gate gen2==gen3 (crumb C4). `static`=reservada, `this`/`base`=contextuais (`base` é nome local vivo).
- **Sequência:** #163 fecha → hard-cut OOP (próximo keystone, verificação independente do fixpoint) → resto da fase-3 na sintaxe nova.

---

## 2026-07-06 — Dívida de memória: fix real via K-B Track C (gate nativo default, VM out)

### D28 · Flip do gate default p/ nativo = o fix da dívida de memória (pedido "corrija a dívida de memória") ✅
- **Contexto:** dono pediu explicitamente "corrija a dívida de memória então" — o gate de PR estava em ~1,5 GB (o balão in-process do VM interpretando os testes com env funcional), causando o OOM da lane ASan (7 GB ÷ ASan-3x ÷ corpus crescente). O #323 (cap ASan) foi só STOPGAP; o dono quer a raiz.
- **Diagnóstico (ground truth, drain-265-168):** o ganho de memória é CONSEQUÊNCIA do flip do CI (Track C), não uma reescrita — o gate NATIVO já roda os testes num processo FILHO (`teko::process::run`, project.tks:748), então o pico do compilador cai pro codegen-only (~366 MB) no instante em que o gate VM deixa de ser default. Track A (#265) já fez o gate nativo enforçar os 3 floors (function/line/branch), CI-verificado → o flip é low-risk.
- **Aplicada (Track C, agente Opus, worktree isolado):** (1) `run_gate`/`native_gate_of` (project.tks) default → `native_gate=true`, com opt-out `--vm-gate`; (2) native.yml dropa o step "Test gate (VM)" das 5 plataformas + mantém UMA lane VM nightly (regressão); (3) sanitizers.yml roda o gate NATIVO (mais leve → conserta o OOM de vez + ASan cobre o path de PRODUÇÃO, mais valioso que o VM). `diff-vm-native` intocado (paridade VM==native segue provada).
- **Base constitucional:** realiza a ruling STANDING do dono "VM out of tests" ([[teko-no-gc-vm-role]]: nativo é autoritativo, VM = dev/WASM); respeita o teto ≤300 MB de design (o balão VM era a violação); main-integrity preservada (gate nativo enforça TODOS os floors, não enfraquece). Fixpoint-neutro (muda QUAL gate roda, não o C emitido) — verificado gen2==gen3.
- **Autônomo (law-first, p/ revisão LTS):** (1) ASan passa a sanitizar o NATIVO (não o VM) — decisão minha: o path nativo é produção (autoritativo), então ASan-checá-lo > VM; (2) manter #323 (cap) como defensivo mesmo com o flip (cinto+suspensório); (3) VM regride via UMA lane nightly, não per-PR (governança separada, não bloqueia PR).
- **Secundário (não neste PR):** codegen puro em ~366 MB ainda > 300 (lei de design) → tuning de arena (right-size do first-rung, lição #148 R3b — os novos módulos stdlib re-introduziram sobre-capacidade). Follow-up separado.

### D28b · Desacoplar a lane ASan do #324 (defer native-ASan flip) — landar o fix de memória, sequenciar a UB ✅
- **Contexto:** o #324 flipou a lane ASan pro gate NATIVO, que sob `-fsanitize=function` **expôs uma UB pré-existente** (do #291): método de trait chamado via `R(*)(void*)` sendo definido com receiver concreto (`tk_t_Sq*`) — UB de C (CFI/LTO-frágil) que o gate-VM nunca compilava. #324 vermelho.
- **Decisão (law-first):** o ganho de memória (VM 1566→937MB) é o flip do **build-gate** (`teko . -o bin`), **independente** da lane ASan. Reverti só o step da lane ASan pro gate-VM (+ caps do #323), landando o fix de memória (PR #324 merged, `af223c2`). A UB da vtable é pré-existente + separável → **task_0ab18d5e / branch fix/trait-vtable-ub**: fix na raiz (thunks tipados) que re-flipa a lane ASan e valida sob o próprio UBSan. **NÃO é mascarar** — status quo da lane (nunca pegou essa UB) + fix sequenciado; a alternativa (fix keystone dentro do PR de memória) bloquearia a prioridade do dono.

### D29 · Achados empíricos corrigem a análise + 3 bugs de codegen sequenciados ✅
- **Empírico (worktree descartável, VM+nativo):** (1) grafo cíclico/DLL **compila nativo hoje** via campo de classe (só `Ref`-como-campo é rejeitado) → cliff-2 estava mal-enquadrado; (2) o UAF do `mem::free` aliased é **invisível ao ASan** (free nunca chama libc free) — só `TEKO_MEM_PARANOID=1` pega → corrige "pego pelo ASan" repetido nos round-1/2. Ver [[teko-mem-model-empirical]].
- **Torneio (round-2, 3 juízes):** arena = **default certo mas não suficiente** — vence pragmatista+purista (híbrido), future-architect prefere ownership+move p/ concorrência; consenso = família arena é a fundação, ninguém defende GC/RC/gen-ref como fundação. Fronteira teórica: pilotar **second-class values** (piso de soundness inferível); nada fecha cliff duro barato.
- **3 bugs de codegen (dono ratificou "eu conduzo, serial"):** (A) assert-dispatch nativo + (B) slice-elem region-drop → BUNDLE `fix/native-codegen-parity` (1 ciclo de fixpoint, PEGA LEVE); (C) UB de vtable → `fix/trait-vtable-ub` (keystone, depois). Um build pesado por vez; drenar cada CLEAN.

### D30 · Vtable-fix (#326) mergeado decoupled + a lane ASan nativa = auditoria rolante de UB ✅
- **#325** (bundle A+B): slice-drop consertado (predicado `cg_binding_is_slice_element_borrow` + suprime o drop de view emprestada), assert já resolvido pelo #324 → só fixture. Reviewer adversarial CLEAN (caçou vazamento largo-demais, não achou). **Mergeado** (`b093d71`). Lição: o drain auto-mergeou ANTES do reviewer fechar (CI ficou verde rápido) → pro #326 (keystone) gatear explícito: reviewer PRIMEIRO, drain depois.
- **#326** (C): UB de vtable consertada na raiz via **thunks tipados** (per-(class,contract), reconstrói receiver leaf/base), cobre trait/interface/base; fixpoint gen1==gen2==gen3 (`f2f1b663`); de brinde consertou um vtable-drop latente de método de base. Reviewer adversarial CLEAN (só 1 site de dispatch, cobertura total, sem colisão nova). Mergeável.
- **Descoberta (a lane ASan nativa = auditoria rolante):** o re-flip da lane ASan pro gate nativo é uma auditoria de UB do path nativo — cada UB pré-existente exposta tem que ser consertada na raiz antes do flip aterrissar. Sequência exposta: vtable #291 (fixed no #326) → depois `tk_mul_u16` signed-overflow → depois **alinhamento sistêmico do arena** (`tk_region_alloc` arredonda por `max_align_t`=8 em arm64 mas Expr/TExpr têm `__int128` que exige 16 → todo Expr desalinhado; latente no CI x86_64 onde max_align_t=16). Ver [[teko-arena-int128-alignment]].
- **Decisão (law-first, recomendação do implementer que HALTou no meu threshold "cauda longa / decisão semântica"):** DECOUPLAR o re-flip do #326 — mergear só o vtable-fix (reviewer-clean, lane volta pro VM, verde). O cleanup de runtime (`tk_mul_u16` + alinhamento do arena, arquitetural) + o re-flip = pass dedicada `fix/arena-alignment` off post-#326 main, com re-verificação de fixpoint completa (arena é pervasivo). NÃO mascara — o vtable-fix é ganho puro; o alinhamento é bug pré-existente sequenciado com o rigor que merece.
- **Fila de produção restante:** trap do `run`/`args`/`cwd` (reserved-name-guard, crasha codegen nativo); fixture de self-dispatch de base (#326 reviewer); alinhamento do arena (dedicado); #184 DIRTY (re-sync); contagem stale no header do diff_vm_native.

---

## 2026-07-06 — RATIFICAÇÃO: remodelagem de memória + `unsafe` + backend próprio

Doc de base completo: `docs/design/memory-unsafe-backend-remodel.md`. Fecha a discussão de vários turnos; é a fundação da branch paralela.

### D31 · Modelo de memória híbrido + `unsafe`-por-tipo + direção VM/backend (dono, aprovado 2026-07-06) ✅
- **Enquadramento honesto (o que mudou o entendimento):** o 1,5GB era o **VM** (env funcional interpretando o corpus in-process), NÃO o arena — `#324` (VM→nativo) já matou. O arena do compilador é ~366MB, leak-to-root **batch seguro**. O ganho 8,5GB→293MB veio de free-list+right-sizing+free-old-on-grow, NÃO de reclaim de escopo. ⇒ **o modelo de memória é pras APPS de usuário** (funcionais, vida-longa), não pro compilador batch. Ver [[teko-mem-model-empirical]].
- **Híbrido recomendado:** `arena` (default invisível, BUILT) + **spine** (fato de points-to/unicidade inferido, bounded, sobre `escape.tks` — a aposta de segurança; UNBUILT, contingente ao audit) + `adopt` (opt-in, fecha C1 por bulk-drop) + `unsafe` (piso). **SEM GC** (descartado: barreiras program-wide, sem stack-maps portáveis em C, e o único modo sound colapsa no `adopt`). RC/gen-ref/borrow-checker também descartados. **Teto honesto: 0 cliffs fechados hoje; tudo pende do audit do spine** (uma dívida → 4 garantias, ou 0).
- **`unsafe` = MODIFICADOR de tipo/fn-de-namespace, NÃO bloco (decisão do dono):** *"se o dev usa, assume o risco por completo, não de uma partezinha isolada 'bloco'"*. Contágio no DADO (tipo unsafe só em fn/tipo unsafe; propaga por composição — struct que embute unsafe é unsafe); chamadas NÃO-coloridas (só tipo safe cruza); métodos herdam o unsafe do tipo. **Ganho grande:** contenção é check NOMINAL → `unsafe` DESACOPLA do spine → é o keystone que shippa PRIMEIRO. Casa com M.3 (honesto).
- **Superfície:** lexer ~0 tokens (keywords contextuais, precedente `from`/`base`); parser: `use path::[A,B]` (lista=`[]`+`,`; corpo=`{}`+`;` — invariante confirmada no `src/`), `unsafe` como modificador (junta `pub`/`static`/`extern`), `adopt{}` (molde `defer`), `#must_free` decorator em declaração. `Owned<T>`/`RawBuf`=tipos stdlib; spine=inferência no checker.
- **VM/backend:** construir **backend próprio AOT + linker** (sair do C+cc/linker externos; north-star toolchain-independence/velocidade/bare-metal). VM aposenta — papéis: `run` (não-usado), REPL (o único a decidir), diferencial (MIGRA pra C-backend-vs-próprio, oráculo melhor), LSan-com-rewind (só significativo no gate VM → nightly). **SEM dependência de comptime/const-eval** (verificado — de-risca a aposentadoria). O gate nativo-ASan é **auditoria rolante de UB** (pegou vtable/tk_mul_u16/alinhamento).
- **Gate pré-branch (sequência aprovada):** (1) `#327` caiu → main limpa + release `0.0.1.49`; (2) 2 fixes de de-risk na main (doc-honesty do `mem::free` + guard de reserved-name `run`); (3) abrir a branch paralela. **Rastreado, não-pré-branch:** `#301` (Func-in-Ref, pré-APPS não pré-branch, parka `#184`); `#283` (UB de `-O2`, obsoletado pelo backend próprio); `#184` parkado (bloqueado por `#301`).
- **Base constitucional:** consistente com no-GC/arena ([[teko-no-gc-vm-role]]), M.3-honesto (unsafe total/visível), small-language (spine inferido, superfície mínima), e a ruling VM-out ([[teko-native-test-gate]]). Primeiro movimento da branch: **auditar `escape.tks`** (sobrepõe ou substitui?) — trava o spine; `unsafe` (nominal) e `adopt` (arena-tree) andam em paralelo, independentes do spine.

---

## 2026-07-06 — Execução da onda 0.1.0.0-beta

### D31b · Recon #330 (`escape.tks`): veredito **LAYER** — a spine é query aditiva ✅
- **Veredito (mergeado em #329 via #345, `docs/design/spine-layer-or-replace.md`, doc-only):** LAYER, não REPLACE. Uma lattice bounded (uma-função + um-hop) de points-to/unicidade entra como query NOVA (`fn_spine`/`ref_target_outlives`) ao lado do `fn_escaping_vars` intocado — o name-set continua o piso sound (over-approximação monótona), a spine só *relaxa* pontualmente. REPLACE é estritamente mais perigoso (poderia baixar o escaping-set abaixo do piso → leak vira UAF, M.1/M.5) por zero poder extra.
- **Escopo honesto e estreito (a spine NÃO shippa as 4 garantias barato):** RELAXA ref-para-local (`typer.tks:2450`), `mem::free` afim (`us=unique`), stored-borrow one-hop unique frame-local (`typer.tks:2512`). **REJECT eterno:** ref retornado (`typer.tks:3169` — o recon ACHOU esse gate que faltava no §2b da issue; o bound de uma-função torna o frame do caller invisível), ref em coleção/variant/genérico-arg/nullable (`resolve.tks:1168/1185/1213/1113/1233` — sem âncora de referente).
- **Classificação do build #331:** *additive-query-mas-gate-touching* — a query é aditiva, mas as relaxações de gate que ela habilita são shared-checker → **herda full gate (C+self-host+nativo) + FIXPOINT + `diff_vm_native` + review independente**, NÃO o fast-path aditivo. 5 fixtures nomeadas (padrão `mem_free/`): `stored_borrow_outlives_referent`+`ref_returned_rejected`+`free_aliased_rejected`+`ref_in_collection_rejected` (REJECT) e `stored_borrow_sound` (ACCEPT). Ver [[teko-remodel-memory-unsafe-backend]].

### D32 · 100% de cobertura no código NOVO/ALTERADO — padrão desde já (dono, 2026-07-06) ✅
- **Ruling do dono:** *"tudo que for criado novo ou alterado deve ter cobertura de 100%, para minimizar o retrabalho final (última W15 antes do LTS)."* Cobertura 100% do **delta** (linhas/branches novos/alterados) shippa JUNTO com a mudança — **definition-of-done** de toda issue, não passe posterior.
- **Enforcement:** medido no **gate NATIVO** (`teko test .` `--coverage` Cobertura, [[teko-coverage-cobertura]]/[[teko-native-test-gate]], nunca VM); o **reviewer** checa o delta como checa um teste falhando; o **integrador** não mergeia em #329 sem o número reportado. Degrau ACIMA do floor histórico de branch-cov (49% = mínimo do corpus legado; a lei é 100% no delta).
- **Exceção honesta (M.3):** arm genuinamente inalcançável (`HALT`/`exit`/`panic` pós-match-exaustivo) pode ser excluído SÓ se **listado com justificativa de uma linha no PR** — zero gap silencioso. 100% forçando teste feio/código morto = sinal p/ extrair/remover, não maquiar.
- **Recorrente por onda até o LTS**, ao lado do W15-sweep e do doc-sync (dev-model da epic #340). Torna o W15 final uma VERIFICAÇÃO, não retrabalho de cobertura em massa. Ver [[teko-100-percent-coverage-on-new-code]] · [[teko-w15-style-from-now]] · [[teko-issues-must-be-100-percent]].

### D33 · Metaprogramação FORA da LTS v1 — adiada pro pós-1.0 (dono, 2026-07-06) ✅
- **Ruling do dono:** *"não vamos incluir na LTS (vai ficar para um futuro, se tiver issue, pode remover e reestruturar as referências) metaprogramação. É muita coisa para uma LTS v1."*
- **Escopo do que SAI:** metaprogramação = **comptime geral / meta-code execution / macros** (execução de código em compile-time, geração/manipulação de AST pelo usuário, quasi-quote). Fica pra versão **futura pós-`1.0.0.0`**, fora de TODAS as ondas 0.X que compõem a LTS.
- **NÃO confundir — FICA na LTS (não é metaprog):** traits estruturais TR0–TR5 (#177/#298, derive de `Eq`/`Ord`/`Hash`/`Clone`/`Default`/`Json` via **compile-time field-view**, ZERO reflexão em runtime — lei M.0); `#`-decorators (`#inject`/`#singleton`/…/`#must_free`, wiring compile-time); genéricos+mono. O `#derive` como atributo já fora REJEITADO em favor do trait estrutural.
- **Estado factual:** varredura (todas as issues + docs) → **NÃO existe issue de metaprog** em nenhum milestone; só aparecia como *"General comptime ('meta-code execution') stays a DEFERRED separate proposal"* (`TEKO_MASTER_PLAN.md:621`). O remodel já é **sem dependência de comptime/const-eval** (D31, `memory-unsafe-backend-remodel.md:146`) — VM-retirement e backend próprio não precisam de metaprog. ⇒ nada a remover; ação = **endurecer a referência** (L621 → "post-1.0 / out of LTS v1"), dobrado no escopo do **#341** (doc-sync da onda 0.1).
- **Enforcement:** qualquer issue de metaprog que aparecer num milestone de onda (0.X) ou apontando pra LTS = **removida do milestone → bucket futuro pós-1.0**, referências reestruturadas. Metaprog nunca é dependência de nada da LTS. Ver [[teko-metaprogramming-out-of-lts]].

### D34 · Rulesets que ENFORÇAM + lição de merge + coverage deferido (dono, 2026-07-06) ✅
*(o D34 original foi perdido — o push direto no umbrella foi bloqueado pela própria Merge gate recém-criada; esta é a versão final, pós-reestruturação.)*
- **Gatilho (erro meu):** mergeei #346 (#336) via `gh pr merge` olhando um snapshot PARCIAL de checks — as lanes de sanitizer ainda rodavam e depois falharam (causa = harness, não UB: o fixture negativo `must_free_leak/` quebrava o loop build-all do sanitizer; fix = marker `EXPECT_COMPILE_FAIL`). Violou main-integrity ([[teko-main-integrity-absolute]]).
- **Descoberta:** a ruleset "All Green" (`~ALL`) NÃO tinha `required_status_checks` — nada exigia CI verde pra mergear (nome aspiracional). E `required_status_checks`/`pull_request` em `~ALL` **BLOQUEIA push de WIP** em feature branch (trava os agentes).
- **DESIGN FINAL de 3 rulesets:** (1) **Merge gate** (`main`+`remodel/**`) = `required_status_checks` **CI gate + Sanitizer gate + SAST gate** (os 3 agregadores `if:always()`); (2) **All Green** (`~ALL` EXCLUDE fix/**,docs/**,recon/**,agent-**,worktree-**,chore/**) = `pull_request`+`non_fast_forward`+`code_quality` (baseline, não trava WIP); (3) **Feature branch guardrail** (fix/**,docs/**,recon/**) = só `non_fast_forward` (garantia sem travar WIP). + push-CI em `remodel/**`. **Push direto no umbrella agora bloqueado → mudanças em #329 (DECISION_LOG etc.) via docs-PR.**
- **Processo (permanente):** nunca mergear em snapshot; confirmar TODOS os checks do commit-HEAD `completed`+`success` (`gh api .../commits/SHA/check-runs`); sempre via `gh pr merge`.
- **COVERAGE DEFERIDO:** a regra nativa "Restrict code coverage" (GitHub Code Quality) **só existe em plano Team/Enterprise** — indisponível em conta pessoal (API 404). Além disso a COLETA de coverage estoura memória (VM in-process, o balão do remodel) → OOM no ubuntu 7GB; só passa em macos + caminho nativo `-o bin --coverage`. Dono **desabilitou a ruleset `code_coverage` + tirou dos required checks; PR #357 draft**. Caminho futuro: o **Coverage gate custom (#355)**, independente de plano. Bridge: verificação MANUAL de delta. Ver [[teko-coverage-ci-findings]].

### D35 · `unsafe #must_free type Arena` — arena manual dev-controlada (dono, 2026-07-06) ✅ (=#358, 0.1)
- **Decisão:** adicionar uma **arena MANUAL, não-lexical, só-unsafe** — o dev cria a região, aloca ponteiros ATRELADOS a ela, e faz **bulk-free** num ponto à escolha. É o complemento **unsafe** e **não-lexical** do `adopt` (que é lexical/safe).
- **A elegância — compõe as 2 features da onda, cada uma guardando a metade que sabe:** **`#must_free` (S2)** barra o **leak da região** (largar a Arena sem `mem::free(a)` = erro de compilação, dataflow do #336); **`unsafe` (U1/U2)** contém o **resto** — pós-free os ponteiros viram dangling (aliased-UAF que a spine não rastreia), risco assumido POR COMPLETO pelo dev. Encaixe exato de M.3.
- **Zero gramática:** tipo stdlib (`is_unsafe`+`must_free` coexistem no `TypeDecl`); `Arena::new/alloc<T>/mem::free` mapeiam na árvore `tk_region_new/alloc/free` que **já existe** (a mesma do `adopt`) — expor, não construir alocador novo.
- **Hierarquia de memória (do auto ao cru):** arena-default(invisível) → `#must_free`/`mem::free`/`defer`/`adopt` (= o "C# `using`/`IDisposable`", SAFE e **mais forte** — `#must_free` OBRIGA o free, C# só avisa) → **`unsafe #must_free Arena`** (região dev-controlada) → `RawBuf`/`Owned<T>` (malloc/free cru).
- **Sequência:** =#358, follow-on do #334 (U3, precisa de `RawBuf`/`Owned`/`ptr`), na onda **0.1**. Independente da spine. Ver [[teko-remodel-memory-unsafe-backend]].
- **Implementação (#358, entregue):** handle = `struct { region: uptr }` (o `tk_region *` cru como word opaco). Três builtins especiais no checker+codegen (SEM gramática nova, sem alocador novo): `teko::mem::region_new() -> uptr` (→ `tk_region_new(tk_region_root())`), `teko::mem::region_alloc(region: uptr, init: T) -> ptr<T>` (T inferido de `init`, → `tk_region_alloc` + init, `ptr<T>` atrelado; **não** é fn genérica com corpo Teko, então zero maquinaria de genéricos), e `teko::mem::free(a)` estendido pra aceitar o handle e rotear pro **bulk-drop** `tk_region_drop_subtree` (a árvore inteira, NÃO a free-list). Reconhecimento por **ESTRUTURA** (`is_region_handle_name` = `#must_free` struct de campo único `uptr`), nunca por nome `Arena` fixo — o módulo stdlib e a cópia local do fixture roteiam idênticos.
- **Staging (mesmo padrão do D16/rawbuf):** o seed liberado (0.0.1.50) precede U1/S2 **E** os builtins novos deste PR, então `src/mem/unsafe/arena.tks` embarca só o TIPO `Arena` cru (sem `#must_free`/`unsafe`, sem `new()`/`alloc` que chamariam os builtins) — self-host hoje. A superfície `#must_free unsafe` COMPLETA (`Arena::new` → `region_alloc` → `mem::free` em todos os caminhos, incl. `defer` + ambos os braços do `if`) é provada por `examples/regressions/arena_manual_ok` (exit 62, NATIVE_ONLY, PARANOID limpo) e o reject por `arena_manual_leak` (COMPILE_FAIL), ambos compilados por gen1. Re-tag = mecânico quando um seed carregar U1/S2 + os builtins.
- **GAP reportado (fora de escopo — checker/parser):** a grafia `a.alloc<T>(...)` do AC#3 NÃO é expressável: `parser::MethodCall` não tem `type_args` (o parser não aceita args de tipo em chamada de método), E um type-param de MÉTODO próprio (`fn alloc<T>` num struct não-genérico) não monomorfiza ("unknown type" no receiver, tanto inferido quanto explícito — só o `Owned<T>::make` estático funciona, pois lá o T é do TIPO). Entregue o **thin-wrapper** que o issue abençoa (hazard #2): a alocação é `teko::mem::region_alloc(a.region, init)` (T inferido), semanticamente idêntica; a grafia-método fica pra quando genéricos-de-método + type-args-em-método-call existirem (surface de checker/parser, fora deste issue).

### D36 · Seeds intermediários `*-beta` por-merge — a umbrella se auto-hospeda ao longo da onda (dono, 2026-07-07) ✅
- **Problema:** o seed liberado da main (`0.0.1.50-alpha`) NÃO parseia a sintaxe nova da onda (`unsafe`/`#must_free`/`use ::[…]`/`adopt`). Enquanto as features são só ADITIVAS (o `src/` ainda não as usa), o seed constrói o gen1 e tudo funciona. MAS quando o `src/` do compilador COMEÇAR a usá-las (dogfooding, re-tag do stdlib staged, spine), o seed da main não constrói nem o gen1 → trava. **Decisão do dono:** publicar um **seed intermediário do compilador (`0.1.0.N-beta`) a CADA merge na umbrella**, E **backfill retroativo de UM seed por CADA merge histórico** (dono, plural explícito: "para cada merge que houve" / "as versões retroativas … todas"). Implementação: **eu (integrador), inline + validado** (não-agent).
- **Mecanismo (4 peças, este PR):** (1) **`tag-on-version-bump.yml`** passa a observar `remodel/**` além de `main` (o bump de `teko.tkp` na umbrella cria a tag); (2) **`release.yml`** dispara em tags `*-beta` (além de `*-alpha`/`*-bootstrap`); (3) **`ci_provision_teko.sh`** vira **channel-aware** — canal `beta` (base/branch `remodel/**` OU tag `*-beta`) prefere o `*-beta` mais novo e cai pro `*-alpha` no 1º bootstrap; canal `stable` (main, tags `*-alpha`) EXCLUI `*-beta` (a main NUNCA semeia de uma umbrella em curso); (4) **bump `teko.tkp` BUILD `0.1.0.0→0.1.0.11`** — o tip atual (todas as features + a própria infra) publica como `v0.1.0.11-beta`, o TOPO da cadeia retroativa (os 10 abaixo = os 10 PR merges históricos, ver backfill).
- **A cadeia de dogfooding:** cada sub-PR mergeado na umbrella bumpa o BUILD → tag `0.1.0.N-beta` → `release.yml` publica um seed do tip → o CI do próximo sub-PR semeia DESSE beta (`ci_provision` canal-beta). No build da própria tag `0.1.0.N-beta`, como ela ainda não foi publicada, "beta mais novo" resolve pro seed ANTERIOR (`N-1`) — exatamente a corrente de auto-hospedagem. Bootstrap-friendly: enquanto não houver nenhum `-beta`, o canal-beta cai pro `-alpha` da main (features aditivas → gen1 constrói).
- **Convenção reforçada:** toda umbrella de onda DEVE se chamar `remodel/<slug>` (senão o filtro `remodel/**` não pega) — casa com a convenção já ratificada dos 4 workflows de CI. Quando a onda fecha, a umbrella→main leva o `teko.tkp` em `0.1.0.<final>-beta`, e a main tag-a a release da onda (alpha→beta na main = a virada de estágio da onda).
- **Validação (inline):** YAML dos 2 workflows parseia OK (triggers conferidos); shellcheck limpo nas linhas novas (só o SC2012 info pré-existente na l.84); **7 cenários unit-testados** do seletor de canal (main-PR/remodel-PR/push-remodel/beta-tag-build/1º-bootstrap/alpha-tag/push-main) — todos PASS, incluindo as invariantes críticas (main nunca semeia de beta; remodel dogfooda o beta mais novo). `derive_version.sh` → `v0.1.0.1-beta`. Ver [[teko-remodel-memory-unsafe-backend]].
- **Fix do trigger de release (sem PAT):** o repo NÃO tem `RELEASE_TAG_PAT`, então `tag-on-version-bump` cai no caminho DISPATCH (`gh workflow run release.yml`), que antes disparava no branch default (main) → construiria o compilador da **main** (sem as features novas), não o tip da umbrella que a tag aponta. Corrigido: dispatch **na própria tag** (`--ref "$DERIVED"`) → todo checkout do `release.yml` fixa no commit taggeado (tip da umbrella). Estritamente melhor também pra linha stable (sem corrida se a main andar após taggear). `release.yml` existe na main (registrado p/ o dispatch-by-name) E na tag (tem o `workflow_dispatch`), então `--ref tag` roda a versão da tag. (PAT continua sendo o caminho preferido/auto-trigger, mas agora é opcional.)
- **Backfill retroativo EXECUTADO (10 seeds, cadeia real — dono ratificou "todos os PR merges" + "primeira do alpha, demais da anterior"):** publiquei um seed beta por CADA um dos 10 PR merges na umbrella, na ordem do branch, cada um construído a partir do seed anterior (self-hosting real): `v0.1.0.1` (#345, do `0.0.1.50-alpha`) → `.2` (#351 U1) → `.3` (#346 S2) → `.4` (#347 S1) → `.5` (#359 docs) → `.6` (#343 U2) → `.7` (#344 U3) → `.8` (#348 adopt) → `.9` (#360 docs) → `.10` (#361 Arena). Depois `v0.1.0.11` = tip+infra. **Mecanismo (Option C):** cada tag fica num commit destacado = `C_i` + **bump manifest-only** de `teko.tkp` p/ `0.1.0.i` (o manifesto NÃO é fonte do compilador → binário funcionalmente idêntico ao do merge, mas reporta a versão certa; sem isso `cross_compile`/`derive_version` embutiria `0.1.0.0` em TODOS e o version-sanity do `ci_provision` rejeitaria a cadeia). Disparei `release.yml --ref V_i` sequencialmente (o `ci_provision` ANTIGO em cada commit histórico escolhe naturalmente a maior versão publicada = o predecessor). Todos os 11 assets/plataforma por seed; cada binário verificado reportando `0.1.0.i-beta`. **Correção honesta:** minha 1ª grafia deste D36 colapsou pra "um catch-up só" — ERRADO; o dono queria a cadeia por-merge (registrado aqui pra não repetir).
---

## 2026-07-12 — VM Retirement (Crumbs 1–5): 100% VM removal, native becomes sole engine

### D37 · VM Retirement (Crumbs 1–5) — 100% VM removal, native becomes the sole engine (dono, 2026-07-12/13) ✅
- **Decisão aplicada — os 5 crumbs, na ordem, cada um com seed intermediário:** crumb 1 = CI-first, retirar as lanes VM de falso-negativo (#531) · crumb 2 = driver repoint (`teko test`/`teko run` → nativo; `--vm-gate` REMOVIDO outright; `src/driver.tks` morto deletado) (#533) · crumb 3 = aposentar o REPL (#538) · crumb 4 = coverage → `teko::coverage` + deletar `src/vm/` + **adendo do dono: aposentar o ESPELHO C inteiro** (92 twins + CMakeLists.txt; sobrevive só a família runtime) (#548) · crumb 5 = varredura de prosa/docs + este registro + o pointer §7 devido (#551). Nativo é o único engine; o bootstrap C está arquivado na tag `0.0.1.3-bootstrap` + histórico do git.
- **Micro-decisões RATIFICADAS:** (M1) REPL aposentado — o dev-loop é `teko run` (build debug nativo + exec, molde `cargo run`); `teko repl` cai no caminho genérico "não é projeto" (exit 1 — desvio do exit-2 do design, aceito). (M2) exe C-bootstrap aposentado — o seed é SEMPRE o binário do release (`ci_provision`/`fetch_teko.sh`). (M3) `--vm-gate` removido OUTRIGHT — um `--vm-gate` residual cai como positional desconhecido (o sinal honesto de que a VM se foi); `TEKO_VM_GATE` não é mais lido. (M4) `teko test .` = o gate NATIVO com os 3 floors (function/line/branch, #265). (M5) `teko run` = build debug para `target/debug` + execução em PROCESSO-FILHO, propagando o exit code do programa (o contrato exato que o `run` da VM tinha).
- **Adendo do crumb 4 (dono 2026-07-13):** os 92 pares `.c`/`.h` congelados + `CMakeLists.txt` deletados por inteiro; a família runtime (`teko_rt.{c,h}`, `assert.{c,h}`, `win32_compat.h`) sobrevive com data de morte marcada — **#549: a versão seguinte ao linker próprio (zero C na árvore)**.
- **Evidência ritual:** por-crumb nos corpos dos PRs (#533/#538/#548) — fixpoint byte-idêntico em cada crumb de src/; prova de byte-identidade do Cobertura pré/pós-move (sha256 `7aab136b…b92d8`); o gate 21/21 do #548 é a primeira auto-hospedagem sem C de compilador.
