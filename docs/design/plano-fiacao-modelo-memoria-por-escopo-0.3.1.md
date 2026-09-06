# Plano de FIAÇÃO — modelo de memória por-escopo, região-como-PARÂMETRO (0.3.1)

> **DESIGN-ONLY.** Nenhuma linha de produto. Este documento amarra os crumbs `0148`–`0159`
> (`MEM-E*/S1/W*`) que TERMINAM A FIAÇÃO do modelo de memória por-escopo já desenhado (4 docs
> abaixo) e o CORRIGEM pelos refinamentos do dono D130 (2026-08-27). Não é design novo — é recuperar
> o desenho, ligar o oráculo que já existe aos consumidores, e trocar o mecanismo AMBIENTE (proibido)
> pela região-como-parâmetro implícito.
>
> **Base:** `fix/retirement` @ `7326ac54`.

## 0. As fontes (a ler ANTES de implementar — são a verdade; onde conflitam, D130 vence)

1. `docs/design/modelo-de-memoria-por-escopo-0.3.1.md` — o modelo geral (residência = JOIN, 5 escopos,
   move-on-return, oráculo, crumbs C1–C6).
2. `docs/design/ast-computed-arena-assessment-0.3.1.md` — as 3 ideias (pré-sizing, elisão, DPS-return),
   merge let/mut, crumbs D0–D6.
3. `docs/design/transicao-move-on-return-e-seletor-n-niveis-0.3.1.md` — a escada rota-C (A0–A7).
4. `docs/design/port-memoria-por-escopo-backend-nativo-0.3.1.md` — a escada nativa (NP0–NP7).

**Os refinamentos D130 (DECISION_LOG:1151, dono 2026-08-27) SUPERSEDEM as 4 docs onde conflitam.** O
conflito é UM e é a fundação: as docs 3/4 conduzem a região do caller por uma **pilha-corrente
thread-local** (`tk_region_current()`/`region_enter`/`region_leave`, "ABI intocada"). **O dono
REJEITA isso** — a região vem como **parâmetro implícito** (estilo DPS: um argumento escondido
threaded pelo compilador). Logo o `region_enter`/`region_leave`/`set_ret_dest`/`ret_dest` AMBIENTES
(hoje sobre `ar_control()` = `_Thread_local`, `arena.tks:291`,`769`,`941`) NÃO são o mecanismo — são o
que o SWEEP remove. O MODELO (morre-no-escopo, move-on-return, residência = JOIN, root só
`service`/cross-thread) fica intacto; só a CONDUÇÃO muda de ambiente para parâmetro.

## 1. O mapa do estado REAL (verificado 2026-08-27)

| peça | estado | ref |
|---|---|---|
| **oráculo `residence_plan`** | **LANDADO e correto** (tiers Scope/Frame/Caller/Root/Unresolved; `residence_tier`; `plan_return` com `is_move`) mas **ZERO consumidores** (`grep residence_plan` fora do ficheiro = nada) → computa-e-ignora | `src/checker/residence.tks` |
| **gancho de root** | **STUBADO**: `is_singleton = false` fixo | `residence.tks:83` |
| **superfície `service <lifetime>`** | **LANDADA INTEIRA** (lexer/parser/ast/checker/emit); ctor de `service` já roteia `Singleton→tk_region_root()`, `Scoped→enclosing`, `Transient→""` — mas SÓ para chamadas de ctor de serviço, NÃO ligada ao oráculo p/ bindings | `parse_decl.tks:760`, `ast.tks:212`, `codegen.tks:3618` |
| **emit por-escopo rota-C** | presente mas dirigido pela heurística ANTIGA de 2 níveis (`_tkbr`/`_tkfr`, `tk_region_new(enclosing)`, `want_block` tecto 64), NÃO pelo plano | `codegen.tks:4735,6225,7920` |
| **símbolos DPS/região no emit** | `region_enter`/`region_leave`/`set_ret_dest`/`ret_dest` mapeados como `CgArenaSym` | `codegen.tks:126,134` |
| **mecanismo de condução** | **AMBIENTE** — `region_enter/leave`→`ar_cur_enter/leave(ar_control())`, `set_ret_dest/ret_dest`→campo `CTRL_RET_DEST` do control-block, `ar_control()`→`tk_arena_control_get()` `_Thread_local` | `arena.tks:769,941,291` |
| **alcance do ambiente** | **CONCENTRADO**: os 40 `ar_control()` vivem TODOS em `arena.tks`, veneer fino sobre a camada `ar_*_w(control, …)` que **já recebe control explícito** | `arena.tks` |
| **primitivas de região em C** | `ar_region_new/alloc/drop/…_w` — exceção-runtime MANTIDA (D90); NÃO migrar/reescrever | `teko_rt`/`arena.tks` |
| **pré-sizing** | crumb `0108 D1-T1` desenha `arena_floor` (chão AST) folded no profiler — D130-refinamento-3 PROMOVE isto a sizing ÚNICO e MANDATÓRIO | `.crumbs/0108` |
| **`_start`** | crumb `0125 RT-ENTRY` abre a entrada per-OS; D130-refinamento-6 faz a root NASCER aqui | `.crumbs/0125` |

**Veredito:** o modelo NÃO está ligado. O oráculo existe; falta (a) CONSUMI-LO nos dois backends,
(b) trocar a condução ambiente por parâmetro implícito, (c) ligar `service`→oráculo, (d) o sizing
compile-time e a elisão `slots==0`, (e) a root no `_start`, (f) objeto-dono-da-arena. É **fiação +
troca de mecanismo**, não implementação do zero.

## 2. Os refinamentos D130 honrados (cada um → onde entra)

1. **Região = PARÂMETRO IMPLÍCITO (não thread-local).** Um argumento escondido, único, threaded pelo
   compilador por TODA função — é ao mesmo tempo a região do caller (para onde o retorno MOVE) e a de
   onde a função abre a sua filha. Unifica o `ret_dest` do assessment com a região-do-caller: UM
   parâmetro, não dois. → `MEM-E4` (thread null-defaulted, byte-idêntico) + `MEM-W4` (o flip).
2. **Objeto DONO da própria arena** — o fat pointer carrega o ptr da arena; membros alocam nela; viaja
   com o objeto (threads de graça → dissolve D127). → `MEM-W5`.
3. **Sizing compile-time ELIMINA `#arena_size`/`#arena_depth`** — o compilador computa o pico de slots
   vivos simultâneos + tamanhos e fixa o tamanho da arena na INICIALIZAÇÃO. → `MEM-E2` (análise) +
   `MEM-W2` (consumo + remoção da superfície).
4. **`slots==0` ⇒ NÃO abre arena; a do pai passa adiante** (elisão como regra dura). → `MEM-W1`.
5. **Array fixo de filhas = SIMPLES**: pico de filhas vivas = profundidade de aninhamento
   (≤ `TK_REGION_STACK_CAP=64`), array fixo compile-time, zera slot no reclaim, loop REUSA o slot
   (fecha anterior antes de abrir próxima → pico plano). NÃO pool/refcount/GC; NÃO lista dinâmica. →
   `MEM-W3`.
6. **A root nasce no `_start`, não em `main`** — `slots==0` uniforme; o `_start` abre a root e passa a
   `main` como o parâmetro de região; substitui o first-touch preguiçoso do `ar_control()`. → `MEM-W6`.
7. **Superfície de root = `service <lifetime>`** (`ServiceLifetime{Singleton;Scoped;Transient}`),
   NÃO `#singleton` de binding. → `MEM-E3`.

### 2a. ADENDO (dono 2026-08-27, DECISION_LOG:1166) — os TIPOS que a máquina do modelo usa pesado

Dois desenhos DELIBERADOS-mas-não-implementados. **Recuperar, NÃO inventar** (fontes: crumbs
`0015-SM-G9`, `0011-SM-G5`; `lang-evolution-0.3.1-memory-and-surface.md` §6/§7b; D-#358). Estado
verificado: **parte já landada** — `PrimKind` tem `Size; Usize` (`type.tks:7`) com predicados ligados;
`Ptr`/`Uptr` são kinds (`type.tks:51,54`), MAS `Ptr` ainda é GENÉRICO (`struct { inner }`), não o opaco.

- **`isize`/`usize`** (`MEM-E0a`, ENSINO). Recupera `0015`. DELTAS: (a) **RENOMEAR o signed `size`→
  `isize`** — o dono flagrou colisão de `size` com identificador usado; `usize` fica. (b)
  **Target-parametrizar `prim_width`** — hoje `64` fixo (`type.tks:37`); recuperar `prim_width(k, target)`
  = largura da palavra do ALVO (a ÚNICA porta de target-dependência). Byte-idêntico em 64-bit
  (`usize==u64`, `isize==i64` bit-a-bit, §7b.5). `usize`/`isize` DISTINTOS de `ptr`/`uptr` (medida vs
  endereço); `usize↔u64` é `to`-cast, `uptr↔usize` é ponte Marshall NUNCA implícita (§7b.2).
- **`ptr`/`uptr` opacos + `wrap`/`unwrap`** (`MEM-E0b`, ENSINO). Recupera `0011`. Torna `Ptr` ATÓMICO
  opaco (remove `inner`; sem aritmética `p+n`/`p[n]` por construção). Os DOIS métodos CANÔNICOS (dono,
  VERBATIM, iguais para `ptr` E `uptr`): **`ptr::unwrap<T>(ref T): ptr`** (ESTÁTICO — ref-tipada → ptr
  opaco) e **`ptr.wrap<T>(): T`** (INSTÂNCIA — ptr opaco → valor-tipado). É a **conversão DIRETA de
  mesma-base — zero-cópia, zero-cast, sem cálculo extra** (`[]str` e `[]byte` partilham a base
  `{ptr,len}`-de-bytes → `unwrap` de `ref []str` dá `ptr`, `.wrap<[]byte>()` devolve `[]byte`). Lower =
  reinterpret nu; a segurança é compile-time (opaco + sem aritmética + mesma-base). O tag/arena-liveness
  de `0011` fica como camada OPCIONAL checada para FFI/endereço estrangeiro. O tag (quando usado) vive no
  **header de alocação da arena** (não na palavra do ptr) → `ptr`/`uptr`
  ficam palavra-nua, SEM fat pointer, SEM mudança de ABI. **USO FLAGSHIP HOJE (dono): interop
  ZERO-CÓPIA `str`↔`[]byte`** — um `str` É `{ptr,len}` de bytes, mesma rep que `[]byte`; o wrap/unwrap
  reinterpreta um como o outro SEM copiar, e a checagem de arena-viva torna-o SEGURO (um reinterpret cru
  poderia devolver slice pendente). É o cast implícito de mesma-rep que o expurgo já quer (CLAUDE.md) e
  MATA o copy-loop que `str_of_bytes` é hoje (`teko_rt.tks:64-69`, byte-a-byte). Twin
  `region_alloc_tagged(r, n, tag)` em
  **`arena.tks` (Teko)** — a arena é 100% Teko (D128), então o antigo FORK C-vs-Teko (DECISION_LOG:680)
  está RESOLVIDO (mais-recente-vence): NÃO é patch em C, é Teko. Reusa `di_type_id` (`di.tks:373`, UMA
  hash) e `region_lookup` (`arena.tks:965`, já existe). Inerte/byte-idêntico até um `__wrap` ler o tag.

**O USO (reball) entra JUNTO com o modelo** (byte-preservante em 64-bit → fixpoint-gated, §7b.5): o campo
de arena no fat é `uptr` (não `u64`, `MEM-W5`); tamanhos de slot/array são `usize` (não `u64`,
`MEM-E2`/`MEM-W2`); ponteiros de região/param são `ptr`/`uptr` opacos (`MEM-E4`/`MEM-W6`). O reball
mecânico tree-wide das posições/ponteiros crus cavalga o sweep (crumbs `0034 SM-S6`/`0091 SM-S4` são o
reball de massa; a fatia do modelo entra em `MEM-W6`). `MEM-E0a`/`MEM-E0b` compõem: o fat carrega a
arena como `uptr` (valor do objeto); o ENDEREÇO do objeto é `ptr`/`uptr` opaco com tag no header — eixos
distintos que compõem limpo.

## 3. A DECISÃO derivada load-bearing (alcance do control-block) — resolução law-first, NÃO um HALT

Removido o `ar_control()` `_Thread_local`, as ~40 wrappers de `arena.tks` (region_program, panic,
intern, names, environ, fd_stage, ret_dest) precisam alcançar o **control-block**. Resolução derivada
dos refinamentos 2+6 (arena viaja com o objeto; root nasce no `_start`): **a região carrega/alcança o
control** — a root (nascida no `_start`) POSSUI o control-block; cada filha alcança-o pela cadeia de
pai. Logo `ar_control()` → `region_control(<região-param>)` (anda até à root e lê o control). Isto
dissolve o `_Thread_local` sem `global var` nem tid-table. **É a leitura fiel** (refinamento-6 diz
literalmente que a root-no-`_start` "SUBSTITUI o first-touch preguiçoso do `ar_control()`"). Como os 40
sítios são um veneer sobre `ar_*_w(control,…)` já-explícito, o SWEEP é mecânico (`MEM-W6`). **Se o dono
preferir control threaded como parâmetro SEPARADO (não via região), é pivô barato** — flag, não bloqueio.

## 4. A METODOLOGIA (as 5 etapas do dono — a espinha do plano)

```
(1) ENSINAR (aditivo/byte-idêntico, SEM build entre passos)   MEM-E0a E0b E1 E2 E3 E4 E5
        └─ agrupa todo o ensino → minimiza reseed
(2) RESEED-1 (harvest do ensino; gate do MEM-E5)              [RITUAL]
(3) SHADOW no scratchpad (os mem_* do §6, D117 não-versionados) + correções   MEM-S1  [sem reseed]
        └─ prova CADA flip perigoso em isolado ANTES do sweep tree-wide
(4) SWEEP + FLIP (byte-movers landam, ambiente morre)         MEM-W1 W2 W3 W4 W5 W6
        └─ ordenados por risco; cada um gate-ável contra os mem_* + gen2==gen3
(5) RESEED-FINAL (gate do MEM-W6)                             [RITUAL]
```

**Porque o ensino é byte-idêntico e batchável:** E0a/E0b (tipos) são inertes até usados (`src/` segue
`u64`, tag-path sem leitor); E1 (runtime) adiciona funções sem chamador (não-emitidas); E2 (análise de
sizing) é função nova sem consumidor; E3 enriquece o plano (inconsumido); E4 thread o parâmetro de região
**null-defaulted** (`null` → caminho ambiente de hoje = byte-idêntico, gen2==gen3 determinístico); E5
plumba o plano no ctx de emit sem rotear por ele. Escreve E0a→E5 SEM build; um único build no RESEED-1.
**O ambiente COEXISTE com o parâmetro durante o ensino** — só morre no SWEEP.

**Porque os flips vivem no SWEEP:** cada W* é um byte-mover perigoso (residência/elisão/move). A SHADOW
(3) de-risca-os em programas avulsos no scratchpad com o consumo ligado localmente; o SWEEP aplica-os
tree-wide + remove o ambiente, com UM reseed final. Se a SHADOW acender UAF num flip, o coordenador
PODE partir o sweep em reseeds iterativos (lei do expurgo ENSINAR→SEED→SWEEP→SEED) — recomendação:
`MEM-W3` (residência de escopo) e `MEM-W4` (move) são os PARANOID-gated; se qualquer acender ASan,
harvest próprio.

## 5. A sequência de crumbs (ordem, etapa, o que ensina/remove)

| crumb | etapa | o que faz | gate |
|---|---|---|---|
| `0148 MEM-E0a` | ENSINO | tipos: recupera `0015` — renomeia signed `size`→`isize`; target-parametriza `prim_width(k,target)`; `usize`/`isize` resolvem; ponte `uptr↔usize` não-implícita. Inerte (`src/` segue `u64`) | `[dry]` |
| `0149 MEM-E0b` | ENSINO | tipos: recupera `0011` — `Ptr` atómico opaco (sem `inner`, sem aritmética); `__wrap<T>`/`__unwrap<T>`; tag no header via `region_alloc_tagged` em `arena.tks` (Teko); inerte até um `__wrap` ler | `[dry]` |
| `0150 MEM-E1` | ENSINO | runtime: `region_control(r)` (alcança control pela root) + entradas de alloc/return cientes-de-região aditivas; ambiente INTACTO | `[dry]` |
| `0151 MEM-E2` | ENSINO | checker: `region_slots(scope): usize` (pico de slots vivos simultâneos + tamanhos) + `scope_slot_count`; estende `arena_floor`; sem consumidor | `[dry]` |
| `0152 MEM-E3` | ENSINO | checker: liga `service <lifetime>` ao oráculo — `residence_plan` recebe a `TypeTable`; binding de tipo `service singleton` força `Root`; des-stub `is_singleton` (residence.tks:83) | `[dry]` |
| `0153 MEM-E4` | ENSINO | codegen+lower: thread o PARÂMETRO de região implícito (`ptr`) por toda fn, **null-defaulted** (byte-idêntico); `region_from_param` fallback | `[fixpoint]` |
| `0154 MEM-E5` | ENSINO | codegen+lower: plumba `residence_plan` no ctx dos dois motores, INCONSUMIDO (byte-idêntico) — **RESEED-1** | `[RITUAL]` |
| `0155 MEM-S1` | SHADOW | scratchpad: os `mem_*` (§6) + baseline + correções; prova cada flip isolado; **sem reseed** (D117) | `[dry]` |
| `0156 MEM-W1` | SWEEP | elisão: `slots==0` ⇒ não abre região, repassa o param do pai (refinamento 4); consome `scope_slot_count` | `[RITUAL]` |
| `0157 MEM-W2` | SWEEP | pré-sizing: abre a região no tamanho de `region_slots` (`usize`); **REMOVE `#arena_size`/`#arena_depth` da superfície** (refinamento 3) | `[RITUAL]` |
| `0158 MEM-W3` | SWEEP | residência de escopo + seletor N-níveis: consome tier `Scope`; região por escopo léxico; array fixo de filhas reusado no loop (refinamento 5) — PARANOID | `[RITUAL]` |
| `0159 MEM-W4` | SWEEP | move-on-return via PARÂMETRO: o retorno constrói na região-param do caller; consome `ReturnResidence`; retira `set_ret_dest`/`ret_dest` ambiente — PARANOID | `[RITUAL]` |
| `0160 MEM-W5` | SWEEP | objeto-dono-da-arena: fat pointer carrega o ptr da arena como `uptr`; membros alocam nela; viaja com o objeto (refinamento 2) | `[RITUAL]` |
| `0161 MEM-W6` | SWEEP | root no `_start`→`main` como param (refinamento 6); remove ambiente (`ar_control` first-touch, `region_enter/leave`, `ar_cur_*`); `ar_control()`→`region_control(param)`; retira `#arena_*` residual; reball posições→`usize`/ponteiros→`ptr`/`uptr` — **RESEED-FINAL** | `[RITUAL]` |

Ordem de dep: E0a,E0b,E1,E2,E3 paralelos → E4 → E5 (reseed-1) → S1 → W1 → W2 → W3 → W4 → W5 → W6 (reseed-final).
A rota C valida-se INTEIRA por gen2==gen3 (rota-C RODA já); o runtime do NATIVO valida quando o link
native fechar (D-gate memória) — a ESCRITURA do `lower.tks` entra AGORA (lei "escreve as duas direções").

## 6. Os fixtures SHADOW (`mem_*`, scratchpad, NÃO-versionados — D117; asseveram STDOUT, gen2)

Do §11 do modelo + §6 das docs 3/4. Corrupção = fingerprint ERRADO (não só leak). Sob gen2 (a rota-C
valida já; a leg native quando o link fechar).

| fixture | o que prova |
|---|---|
| `mem_block_dies` | local só usado dentro de `{}` reside na região do bloco e morre na saída (churn N ciclos netam a zero) |
| `mem_scope_kinds` | os 5 escopos (bloco/loop/if/when/fn) tratados idênticos — um local em cada morre na saída |
| `mem_loop_per_iter` | acumulador crescido em loop: array fixo de filhas REUSA o slot; pico PLANO em 1M iterações (refinamento 5) |
| `mem_move_return` | valor construído no callee e retornado é MOVIDO para a região-param do caller (usado após a chamada) |
| `mem_move_transitive` | valor retornado por N frames (move N vezes; aterra na região do consumidor de topo) |
| `mem_str_scope` | `str` concatenado num `{}` rota pela região do escopo e morre com ele |
| `mem_no_root_leak` | corpo só com locais+retornos; scoped>0 e unresolved=0 (nada em root exceto o frame do `main`) |
| `mem_service_root` | binding de tipo `service singleton` reside em root (lido após o `{}` declarante fechar → não morreu) |
| `mem_elide_leaf` | fn `slots==0` (ex.: `fn a(): i32 { b() }`) NÃO abre arena; a região do pai passa direto (refinamento 4) |
| `mem_fixpoint` | o próprio `src/` gen2==gen3 byte-idêntico |

Regra do dono: os `mem_*` são AVULSOS no scratchpad (D117), não `.tkr` versionados; o regressor de UAF
real é o self-host sob `TEKO_MEM_PARANOID`/ASan. Zero `.tkr`/`.tkt` novo versionado (lei de testes).

## 7. Riscos e tensões de lei

| risco | resolução (law-first) |
|---|---|
| **R1 — remover ambiente reabre o alcance do control** | §3: região alcança control pela root (refinamentos 2+6); 40 sítios num só ficheiro sobre `_w` já-explícito. Pivô barato se o dono quiser control como param separado — flag, não HALT. |
| **R2 — thread o param de região = ABI de toda fn** | null-defaulted no ENSINO (byte-idêntico, gen2==gen3); flip no SWEEP. O param unifica caller-region + ret_dest (UM arg). |
| **R3 — move-on-return abre UAF** | residência = JOIN(usos) domina todos os usos (modelo §1, teorema LUB); a região-param do caller ⊒ callee sempre (disciplina de pilha). PARANOID/ASan é o detector. |
| **R4 — sizing não é exato p/ alloc runtime** | sob NO-PUSHES não há push dinâmico; `region_slots` é o CHÃO da 1ª chunk; alloc runtime cresce por chunk-list (nunca UAF; over-floor leak-safe). "Sizing" = chão inicial, não cap exato — honesto. |
| **R5 — colisão nos quentes** (`codegen.tks`/`lower.tks`) | crumbs separados por rota/passo; coordenar com quem toca `lower.tks`. A escritura native entra já; a validação de runtime espera o link. |
| **R6 — reseed grande no sweep** | SHADOW de-risca cada flip; se W3/W4 acender ASan, harvest iterativo (lei do expurgo). Recomendado 2 reseeds; permitido mais. |
| **R7 — objeto-dono-da-arena (W5) é fat-pointer redesign** | é o mais pesado e novo; sequenciado ÚLTIMO, in-plan (não deferido a 0.3.2). Habilita a dissolução de threads (D127). |
| **R8 — tipos (E0a/E0b) são recuperação, não invenção** | `PrimKind` já tem `Size/Usize`, `Ptr/Uptr` são kinds — as deltas são o rename `size→isize`, o target-param de `prim_width`, tornar `Ptr` opaco, e os métodos `wrap`/`unwrap`. Inerte/byte-idêntico até o reball (fixpoint-gated). O antigo FORK do `region_alloc_tagged` (C-vs-Teko) morreu com a arena-100%-Teko (D128). Sem HALT. |
| **R9 — `size` como identificador em `src/`** | é exatamente por isso que renomeia para `isize` (não há sweep de type-name `size` porque `0015` deixou `usize`/`size` INERTES — `src/` nunca adotou; confirmar zero ocorrências de `: size` antes do rename). |

**O ponto derivado (alcance do control, §3) resolve-se law-first pelos refinamentos 2+6** — decisão
confirmável, não bloqueio.

## 8. FORK REAL — o GATE de privilégio da capacidade newtype-ponteiro-cru (HALT)

O dono impôs uma CONSTRAINT DURA: a capacidade newtype-sobre-escalar COM `wrap`/`unwrap` de reinterpret
CRU é **PRIVILEGIADA** — o compilador só pode usá-la no PRÓPRIO código (compiler-base), NUNCA num programa
de usuário; o checker deve BARRAR um user program de definir/usar essa capacidade crua; e conjeturou que
`global` "provavelmente" marca a base privilegiada. **Verifiquei o código: o gate NÃO está deliberado em
lugar nenhum, e a intenção conjeturada não bate com a semântica atual — é FORK REAL.** Evidência:

1. **`global` hoje = NOME GLOBAL SEM-NAMESPACE, não "base privilegiada".** `check_modules.tks:174-232`:
   um decl `global` é chamável sem qualificador de namespace, e o único check é colisão de assinatura
   `global` entre namespaces. Não há semântica de privilégio/trust. Reusar `global` como marcador de
   base-privilegiada é semântica NOVA (o dono só supôs).
2. **NÃO existe fronteira compiler-base vs user-program no checker.** O compilador compila o `src/` (a si
   mesmo) e um programa de usuário pela MESMA pipeline; nada marca um decl como "base confiável". Não há
   flag/namespace-reservado que o checker use para distinguir os dois.
3. **TENSIONA uma lei RATIFICADA — o §6 aposentar-`unsafe`** (`plano-secao6-aposentar-unsafe.md`): esse
   design REMOVEU de propósito o split privilegiado/não-privilegiado — as intrínsecas cruas
   (`buf_ptr`/`bytes_from_ptr`) são livremente chamáveis de um `pub fn` nu, **seguras-por-arena, SEM
   gate** (fixture `mem_intrinsics_safe_callable`). Um gate de privilégio novo reintroduz exatamente o
   trusted/untrusted que o §6 aposentou.

**O crux (o que decide o gate):** o `wrap<T>` é seguro-por-construção quando o checker PROVA que `T`
partilha a base-representação do ponteiro (mesma-base = reinterpret são). Se essa prova for airtight para
todo `T`, NÃO precisa de gate de base — é seguro-por-construção, consistente com o §6. Se o checker NÃO
consegue provar mesma-base para `T` arbitrário (um user faz `p.wrap<Qualquer>()` reinterpretando memória
alheia), então a capacidade É insegura e precisa do gate base-only — NOVO e tensiona o §6.

### 8a. A VISÃO do dono (por quê importa, coordenador)

A capacidade — definir tipos ricos (newtype sobre escalar-base + métodos) NA PRÓPRIA BASE do compilador —
é a fundação para o dono depois **refinar o código do compilador, introduzir features, e tornar a
linguagem mais OO-like**. Não é só `ptr`/`uptr`; é a maquinaria "definir tipos de superfície" (estilo
Go `type X Underlying` + métodos) que o compiler-base usa. **A capacidade já EXISTE** (`NewtypeBody`,
verificado) → a visão está desbloqueada para o compiler-base HOJE.

### 8b. O ENFORCEMENT proposto (coordenador) mapeado no código REAL — recuperado vs faltante

Mecânica proposta: o checker dá **"tipo já definido com esse nome"** para barrar user-code de
criar/redefinir; o **compiler-base (marcado `global`) é o único PERMITIDO a (re)definir/enriquecer** um
tipo por essa via. Verifiquei — o que EXISTE e o que FALTA:

- **EXISTE (a metade "barrar"):** `check_no_duplicate_types`/`duplicates_of_reg` (`collect.tks:1366`) —
  dois tipos de mesmo nome+namespace+arity → erro "duplicate type"; e `global_type_collision_at`
  (`check_modules.tks:227`) — dois `global` de mesmo nome/arity → erro. **MAS ambos aplicam a TODOS
  igualmente, SEM exceção-base**, e a colisão `global` só pega global-vs-global (um `type ptr` NÃO-global
  do usuário NÃO colide com o `global ptr` da base hoje).
- **FALTA / CONTRADIZ (a metade "permitir base (re)definir/enriquecer"):**
  1. **NÃO existe mecânica de REOPEN/ENRICH de um tipo** — um 2º decl de nome+arity existente é REJEIÇÃO
     plana (duplicate), não merge/enrich. O único merge é de MEMBROS de trait (`merge_named_members`,
     `merge.tks:340`: `Absorb` se AST-idêntico / `Conflict` se mesma-assinatura corpo-diferente /
     `Overload`) — é absorção de trait, NÃO um "a base re-abre/enriquece o tipo".
  2. **O `global` atual FORBIDS exatamente o que a proposta quer PERMITIR:** hoje um 2º `global` de mesmo
     nome é ERRO — o oposto de "a base pode redefinir". Repropor `global` como marcador de reopen-base
     CONTRADIZ a sua semântica de colisão vigente.
  3. **NÃO há assimetria base-vs-user** em nenhum dos dois checks — nada distingue "a base pode".
  4. **Crumbs `0011`/`0015` não deliberam nada disto** (definição one-shot / PrimKind-interno).

### 8c. O FORK preciso (HALT) — a pergunta ao dono

A metade "barrar user" é extensão NATURAL do check existente (generalizar a colisão para pegar um decl de
usuário que colide com um `global` da base = "tipo já definido") — recomendável law-first, sem invenção.
**O que é GENUINAMENTE não-deliberado e HALTa é a metade "a base (re)define/enriquece":**

> **"Redefinir um tipo por ele próprio" (o caso permitido) é (i) definição ÚNICA one-shot — a base
> define `ptr`/`uptr` UMA vez, sem reopen (então "redefinir/enriquecer" é só barrar-o-resto, e nada de
> novo é preciso além da colisão-contra-`global`), OU (ii) um mecanismo REAL de REOPEN/enrich de um tipo
> existente (novo — precisa de regras de merge: absorve-se-idêntico como trait? add-only? override?), e
> nesse caso, já que o `global`-collision atual REJEITA um 2º `global` de mesmo nome, o que o SUPERSEDE
> para permitir o reopen da base?"**

Recuperei as duas matérias-primas existentes (rejeição-duplicate; absorb-if-identical de trait) mas
NENHUMA é a mecânica "base (re)define/enriquece, user não" — ela não está no código nem nos desenhos
recuperados. Reconciliar também com o §6 (aposentar-`unsafe`: seguro-por-arena, sem split
privilegiado): o gate supersede o §6 para o newtype-ponteiro-cru, OU a segurança é estrutural
(opaco + sem-aritmética + `wrap` mesma-base provado)?

### 8d. DESIGN-AHEAD (D120) — o que segue SEM o fork

A capacidade + os `ptr`/`uptr` newtypes + `wrap`/`unwrap` + TODO o uso NO COMPILER-BASE (o modelo de
memória inteiro) são VÁLIDOS sob QUALQUER resolução (o compilador É a base — define os tipos UMA vez,
caso (i), que funciona hoje). Logo `MEM-E0b` e os 14 crumbs AVANÇAM para o compiler-base agora. **O
ÚNICO bloqueado é a mecânica de (re)definição/enriquecimento privilegiada + a assimetria base-vs-user**
(o enforcement gate) — que NÃO bloqueia o modelo de memória (100% compiler-base).
