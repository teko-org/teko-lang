---
created: 2026-08-20
recon-by: scout (Claude Haiku 4.5)
scope: readiness of parallel test runner + channel/queue infrastructure
protocol: DECISION_LOG + .crumbs/** + docs/design/** + src/
---

# Recon: Parallel Test Runner + Channels + Queues (2026-08-20)

## Executive Summary

**Veredito:** parallel test runner é **PLANEJADO (pauta)**, não é lacuna. Mas há **lacuna genuína** na ESPECIFICAÇÃO do protocolo de canais/filas que o runner usaria.

### 6 Respostas diretas:

1. **Runner paralelo (na pauta?):** ✅ **SIM** — crumb #472 (`SW11.4`, `wave-0.3.1-plan.md:436`)
   - Status: PLANEJADO, não implementado (harness atual sequencial)
   - Deps: SW2 threading (que carrega S10 spawn/chan)

2. **memchan (status):** ✅ **LANDED ~70-85%** — S10-SURF/S10-RT (crumbs 0115/0116)
   - Implementação: `tk_memchan_*` (`teko_rt.c:2605+`) + surface `src/threads/threads.tks`
   - Reutilizável: **SIM**

3. **oschan (status):** ✅ **LANDED ~70-85%** — S10-SURF/S10-RT (crumbs 0115/0116)
   - Implementação: `tk_oschan_*` (`teko_rt.c:2840+`)
   - Uso atual em runner: **NÃO** (usa arquivo-based caseiro `.chan` em vez da real)
   - Reutilizável: **SIM, se #472 quiser IPC real**

4. **spawn (status):** ✅ **LANDED** — S10 S1-S3
   - Funções: `tk_thread_spawn`/join (`teko_rt.c:2554+2567`)
   - Surface: `src/threads/threads.tks`
   - Usado em: `run_pool` (regression.tks)

5. **Queue reutilizável:** ✅ **PLANEJADO** — COL-Q14 (crumb 0082, M2)
   - Tipo: `Queue<T>` (FIFO sobre `Ring<T>`)
   - Pode servir work-queue do runner paralelo

6. **Lacuna genuína:** ⚠️ **SIM** — especificação do protocolo
   - **O que falta:** #472 não detalha qual tipo de canal (`memchan`/`oschan`/arquivo) ou qual estrutura (queue/ring) o runner paralelo usaria para distribuir testes entre workers
   - **Há precedente parcial:** `journaling-de-corrida-0.3.1.md` define journaling com sharding estático + arquivo-based canal para regressões, mas runner de testes unitários não tem equivalente deliberado

---

## 1. Runner de Testes Paralelo — Está na Pauta?

### ✅ SIM — Crumb #472 (SW11.4)

**Referência:** `docs/design/wave-0.3.1-plan.md:436`

```yaml
SW11.4 | M | #472 parallel tests by default (per-process) + serial_group | on 11.1 + SW2 threading
```

### Status Atual

- **Harness sequencial hoje** (`src/test/test.tks`, `src/runtime/teko_rt.c:2199-2516`)
  - Usa C runtime + `capture_panic` backend intrinsic (ratified 2026-08-19, crumb RT-L6)
  - ZERO paralelização de testes unitários
  
- **Regressões já tem `run_pool`** (`src/build/regression.tks:176`)
  - Paralelismo via sliding window de processos (spawna N, colhe 1, lança próximo)
  - Sem canal real — usa arquivo I/O (`.chan` file) como oschan caseiro

### Dependências

- **SW2** (threading): G8 thread-safe `teko_rt` → `teko::threading`/`teko::sync`
- **Predito:** spawn/chan de S10 (0115/0116), já 70-85% landed

### Fixtures Owed (per wave-0.3.1-plan.md:442)

- `process_run_captured` ✓ (já existe em regression_test.tkt:679-689)
- `test_selector_glob` (seleciona subset)
- `test_panic_asserted` / `test_exit_asserted` (diverging test PASSES)
- `parallel_tests_serial_group` (suite com `#serial_group` roda serial)

---

## 2. memchan — Onde? Qual Estrutura?

### ✅ LANDED ~70-85%

**Referência:** DECISION_LOG.md:544 — "S10-SURF + S10-RT: ~70-85% já landed (spawn/chan/journal/threads/ref-guard/sync). Reframe para `verify-and-wire`"

### Implementação

| Aspecto | Detalhe |
|---------|---------|
| **Código C** | `teko_rt.c:2605+` (`tk_memchan_*` primitives) |
| **Surface Teko** | `src/threads/threads.tks` (scaffolding + `IChannelKind<T>`, `Rx<T>`, `Tx<T>`) |
| **Crumbs** | 0115 S10-SURF, 0116 S10-RT (M2, [fixpoint], verify-only) |
| **Estrutura** | In-process memory channel (threads/coroutines, shared arena) |

### Reutilizável?

**SIM** — proto é genérico:
- `Rx<T>`: receiver (pop/receber)
- `Tx<T>`: sender (push/enviar)
- Não dependem de apply (spawn/coroutine) — apenas de canal mecânica

---

## 3. oschan — Onde? Qual Estrutura?

### ✅ LANDED ~70-85%

**Referência:** plano-s10-await-opcao-c-crumbs.md:11-13

```
channels — tk_memchan_* (teko_rt.c:2605+), tk_oschan_* (:2840+), 
tk_waitgroup_* (:2758-2789, futex/condvar coordination v1)
```

### Implementação

| Aspecto | Detalhe |
|---------|---------|
| **Código C** | `teko_rt.c:2840+` (`tk_oschan_*` primitives) |
| **Mecanismo** | IPC entre processos (pipe/socket/shm, per-`#os`) |
| **Estrutura** | Portável: POSIX + Windows branches |

### Uso Atual do Runner

❌ **Runner NÃO usa `tk_oschan_*` real** — usa arquivo-based caseiro:

```teko
fn spec_env_with_channel(spec: ProcSpec): []str {
    /* env var + ".chan" file = DIY oschan */
    teko::list::push(spec.env, VERDICT_CHANNEL_ENV ~ "=" ~ spec.prefix ~ ".chan")
}

fn harvest_spec(...): CapResult {
    var chan = teko::io::read_file(spec.prefix ~ ".chan") { ... }
    /* resultado no CapResult.chan_text */
}
```

### Reutilizável?

**SIM, potencialmente** — `tk_oschan_*` existe, mas #472 não especificou se usará a real ou continuará com arquivo.

---

## 4. spawn — Status

### ✅ LANDED

**Referência:** plano-s10-await-opcao-c-crumbs.md:9 (S1-S3 surface), DECISION_LOG.md:544

| Aspecto | Detalhe |
|---------|---------|
| **Primitivo C** | `tk_thread_spawn`/join (`teko_rt.c:2554`/`:2567`) |
| **Surface** | `src/threads/threads.tks` (S1-S3 scaffolding) |
| **Usado** | `run_pool` (regression.tks:187) — `teko::process::spawn_redirected` (subprocess, não thread) |
| **Modelo** | Deep-copy args (S3 ctx-blob) + arena-per-spawn |

---

## 5. Fila/Queue a Reaproveitar

### ✅ PLANEJADO — COL-Q14

**Referência:** `.crumbs/0082-COL-Q14-queue-deque.md`, `wave-0.3.1-plan.md` (M2 collections)

```teko
pub type Queue<T> = class {
    intern items: Ring<T>    /* Ring base — O(1) both ends */
    pub fn enqueue(x: T)     /* back */
    pub fn dequeue(): T | null  /* front, FIFO */
}
```

### Potencial pra #472

- `Queue<u64>` (test ids) como work-queue de distribuição
- `Ring<T>` base (COL-Q3, M2) entrega O(1) wrap sem realloc

### Status Atual

**NÃO há queue no `run_pool` hoje** (linhas 182-203, regression.tks):
- Mantém arrays paralelos: `handles[]`, `spawned_at[]`
- Sliding window manual (launched < window, harvest sequencial)
- Zero work-stealing ou distribuição de carga via queue

---

## 6. Lacuna Genuína — Protocolo de Canais pra #472

### ⚠️ Definição da Lacuna

**#472 não especifica:**
1. Qual tipo de canal transportaria os jobs (memchan? oschan? arquivo?)
2. Qual estrutura de dados (Queue? Ring? Channel<T>?)
3. Como integrar com sharding estático (journaling) vs dinâmico (worker pool)
4. Se testes rodam em threads (S10 spawn) ou processos (`teko::process::spawn`)

### Precedente Parcial: Journaling

`docs/design/journaling-de-corrida-0.3.1.md` (status: DESENHO, crumbs C1-C9) define:
- Modelo: **journal append-only** com **per-writer segmentation** (nenhuma sobreposição)
- Mecanismo: arquivo-based, carimbo de corrida, fold final
- Scope: **regressões** (`run_pool` já implementado)

**Não há equivalente para testes unitários (#472)** — a decisão de "qual protocolo" fica aberta.

### O "Terceiro Canal" Mencionado

`journaling-de-corrida-0.3.1.md:4` cita:
> "o terceiro canal" (src/build/regression.tks)

Referência ao `.chan` file I/O como terceira forma (além de memchan in-process, oschan IPC).

**Mas para testes unitários paralelos, não há decisão equivalente.**

---

## Arquivo de Origem para Verificação

### Deliberado em:

1. **DECISION_LOG.md**
   - Linha 544: S10-SURF/S10-RT 70-85% landed (spawn/chan/journal)
   - Linha 563-566: D54 "Ensino AGORA, uso depois" (await/DI superfície em SM-R1)

2. **docs/design/wave-0.3.1-plan.md**
   - Linha 85: SW11 test infrastructure cluster (#442/#471/#472/#473)
   - Linha 426-443: SW11 detail (parallelismo em #472 depende SW2 threading)

3. **docs/design/plano-s10-await-opcao-c-crumbs.md**
   - Spawn/chan/journal landed (S1-S3, 70-85%)
   - Await lowering via stackful coroutines + arena-per-arm

4. **docs/design/journaling-de-corrida-0.3.1.md**
   - Paralelismo medido (4,22x speedup)
   - Journaling model (sharding + arquivo)
   - **Scope: regressões apenas**

### Código Atual

- `src/build/regression.tks` (run_pool — sliding window, arquivo-based chan)
- `src/threads/threads.tks` (memchan/oschan surface — S10)
- `src/test/test.tks` (harness — sequencial hoje)
- `.crumbs/0082-COL-Q14-queue-deque.md` (Queue<T> planejado, M2)

---

## Recomendação pra o Dono

Se o dono quer #472 (parallel tests) execução sem ambiguidade:

1. **Escolher o protocolo:** memchan (in-thread), oschan real (inter-process), ou arquivo-journal
2. **Escolher o trabalho:** threads (S10 spawn) vs processos (teko::process)
3. **Opcionalmente:** reusar `Queue<T>` (COL-Q14, M2) para distribuição de jobs se trabalhos for dinâmico

Sugestão: sharding estático (como journaling) é mais simples (zero sincronização); if mudar depois.
