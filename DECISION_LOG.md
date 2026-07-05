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
