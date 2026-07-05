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
