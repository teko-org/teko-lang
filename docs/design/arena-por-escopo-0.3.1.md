# Arena-por-escopo — a frente de MEMÓRIA do fixpoint nativo (0.3.1)

Arquiteto, 2026-08-07. Base: `fix/retirement` @ `5026006a`. Documento de DESENHO — nenhuma linha de
produto. `bootstrap/teko.c` e os gémeos C do checker/codegen/build são SAÍDA/CONGELADOS; a única C
tocável é `src/runtime/teko_rt.{c,h}` (exceção de runtime), e cada linha dela é justificada aqui.

Regra do dono honrada: proposta com arquivo:linha, alarme só se medido/provado por estrutura. Sem
trailers em nenhum commit/PR referenciado (ruling 2026-07-15).

## 0. O alvo, e a fronteira com o `modelo-de-memoria-por-escopo`

Este documento é a **frente estreita de MEMÓRIA** da rodada de achatamento
(`docs/memory/achatamento-de-n2-plano-0.3.1.md`): trazer o **pico de RSS do self-emit NATIVO** abaixo
do teto HARD de **2,5 GiB** (alvo **1,5 GiB**), que é o **bloqueador confirmado do fixpoint nativo**.
A parede está medida e é exata: o self-emit nativo (o compilador a compilar-se a si próprio pelo
backend próprio) rebenta o teto **na ENTRADA do codegen** — logo a seguir a `consteval 579/579`,
rc=139 sob `ulimit -v 2621440` (`docs/design/n112.0-crash-diagnosis-0.3.1.md` §"Memory-cap finding").

**Isto NÃO é o `modelo-de-memoria-por-escopo-0.3.1.md`, e a fronteira é limpa** — os dois são
COMPLEMENTARES, ortogonais, e não colidem no ficheiro quente:

| eixo | o que ataca | onde vive | estado |
|---|---|---|---|
| `modelo-de-memoria` (C1–C6) | residência POR-ESCOPO DENTRO de cada função (move-on-return, os 5 escopos léxicos), o modelo da LINGUAGEM | `escape.tks`/`spine.tks`/`codegen.tks`/`lower.tks` | desenho; mudança semântica profunda; parte BLOQUEADA (cross-thread) |
| **este (arena-por-escopo)** | a RETENÇÃO CROSS-FASE do compilador — o overhang do frontend que sobrevive à entrada do codegen | `project.tks` (orquestração) + `teko_rt` (uma primitiva) + `lower.tks` (str nativa) | desenho pronto a implementar HOJE, nada bloqueado |

A prova de que são eixos distintos está no §12 do `modelo-de-memoria`, e reforço-a com uma medição
NOVA desta árvore (§1): o driver nativo `emit_native_x86` **JÁ limpa por-item** (C6.3, fusão
lower+encode com `region_new`/`region_drop` por item, `project.tks:4351-4371`), logo o scratch do
codegen **já está limitado**. O que sobra e mata o build é o que o **frontend RETÉM e entrega intacto
ao codegen**. Este eixo é o que o `modelo-de-memoria` explicitamente NÃO cobre — ele limpa dentro das
funções; este limpa entre as FASES.

O `#476`/`#arena_depth`, o profiler-afinador (`o-profiler-como-afinador-de-arenas-0.3.1.md`) e o
per-scope da linguagem ficam FORA. A frente aqui é só: **largar o overhang do frontend antes do
codegen alocar** e **rotear os `str` nativos para região reclamável**.

---

## 1. Mapa de RETENÇÃO — onde vivem os ~1926 MB (traçado à fonte)

A diagnose do profiler (`o-profiler-…` §7) é o ponto de partida, verificado contra o código:

| grandeza (profiler §7) | valor | verificação nesta árvore |
|---|---|---|
| **reclaim ratio** | **0,0 %** | nada é largado durante o build — confirmado por estrutura abaixo |
| **root (never freed)** | **1926,3 MB** | o maior retido; vive o build inteiro, só sai no exit do processo |
| **str unroutable** | **66,4 MB** | 2,17 M buffers fora da árvore de regiões (`tk_obs_mstr`) |
| **scoped (freed at drop)** | **0,0 MB** | escopos abrem regiões e não largam nada de útil |

### 1.1 Porque root nunca larga a meio (a causa estrutural, não uma opinião)

Três factos compostos, cada um com arquivo:linha:

1. **O `virtual-main` NÃO abre região de frame.** `lower.tks:14293` documenta-o à letra: *"A
   VIRTUAL-MAIN opens NO frame … `open_frame_region` always pushes a non-loop frame at index 0"* — o
   virtual-main é o único que não passa por `open_frame_region`. Logo toda alocação que a
   ORQUESTRAÇÃO de topo faz cai na **task-root** (`tk_region_root`, `teko_rt.c:2233`), que
   *"is never dropped in one pass"* (`teko_rt.h:133`) e só morre no exit.

2. **A orquestração do frontend corre nessa camada de topo.** `checked_program_of`
   (`project.tks:328`) encadeia as três fases e cada saída ALIMENTA a próxima:
   `type_program_with_deps_pre_mono(selected): pre` (checker) → `monomorphize(pre.prog, pre.table)
   -> checked` (mono) → `inline_consts(checked): inlined` (consteval). Os intermédios —
   `selected` (parsed), `pre.prog`, `pre.table`, `checked` — ficam TODOS vivos em root até o processo
   sair, porque nada os larga: são valores da moldura de topo que não tem frame que caia.

3. **A análise de escape atual empurra tudo o resto para root.** `escape.tks:9-12` (citado em
   `modelo-de-memoria` §0): *"When the analysis cannot PROVE an allocation is frame-local it is
   treated as ESCAPING (a leak is safe; a use-after-free is a vulnerability)."* O scratch interno do
   checker (Env, scopes, TypeTable, nós TAST intermédios) e — o termo DOMINANTE — os buffers
   abandonados do copy-grow O(n²) (`al1-proof-report.md`: ~1,8 GB de pico medido) residem em root
   porque a análise conservadora não os prova frame-local. Numa região bump, `tk_slice_push_r` num
   grow **aloca um buffer novo maior e ABANDONA o antigo na região** — o antigo só morre no drop da
   região. Com reclaim 0 %, cada metade abandonada de cada slice que dobrou fica em root para sempre.

### 1.2 O quadro de retenção no INSTANTE do crash (entrada do codegen)

O crash é na ENTRADA do codegen (a seguir a `consteval 579/579`), ou seja o frontend COMPLETA sob o
teto e é o codegen que empurra o pico por cima. No instante `emit_native_x86(prog)` começa, root
segura:

```
root (1926 MB) no instante do codegen START:
├─ prog (a TProgram consteval'd)  ......... VIVO — o codegen precisa (lê prog.items no loop)
├─ checked (pós-mono, pré-inline) ......... MORTO — inline_consts já consumiu; RETIDO
├─ pre.prog (TAST pré-mono) ............... MORTO — monomorphize já consumiu; RETIDO
├─ pre.table (tabela de monomorph) ........ MORTO — só o mono a usa; NUNCA precisa do codegen; RETIDO
├─ selected (programa parsed) ............. MORTO — o checker já consumiu; RETIDO
├─ scratch do checker (Env/scopes/types) .. MORTO — RETIDO
└─ buffers abandonados do copy-grow O(n²) . MORTO — o termo dominante (~1,8 GB, al1-proof); RETIDO
```

**A quantificação por-fase que a diagnose suporta:** o `al1-proof-report.md` atribui ~1,8 GB dos
~1,93 GB ao copy-grow O(n²), disperso pelas três fases do frontend. A `TProgram` VIVA (o que o
codegen precisa) é uma FRAÇÃO — o resto é MORTO-mas-retido. **É isto que o eixo ataca: largar o
MORTO antes de o codegen alocar por cima.** O driver do codegen já não contribui para o overhang
(§0, C6.3 por-item); o overhang é 100 % frontend.

**A fronteira de copy-out, respondendo directamente à pergunta §4 do briefing:** a região do checker
PODE ser largada antes do codegen SE — e só se — a `TProgram` final for copiada para fora primeiro.
**O que precisa de sair: exatamente `checker::TProgram` (a saída do `inline_consts`).** O que NÃO
precisa de sair e morre com a região: `pre.table` (o mono consome-a, o codegen nunca a vê),
`pre.prog`, `checked`, `selected`, e todo o scratch. A LIR NÃO é copiada — ela é construída DENTRO do
codegen por `lower_prelude(prog)` (`project.tks:4345`), a partir da `TProgram` já limpa. A fronteira
é limpíssima: **um artefacto, a `TProgram`; tudo o resto é lixo da fase.**

---

## 2. O predicado de LIBERTAÇÃO SEGURA (para nenhum crumb reabrir o UAF que se fechou)

O bug de region-lifetime que se acabou de fechar (`enter_self_append_fallback`/`bracket_to_root`,
`lower.tks:14355-14385`; N112.0) prova que um free prematuro de uma região que ainda tem uma
referência viva é um use-after-free — a classe exata do crash `assemble_sel` (`one[0] == 0x5`). O
dual, que ESTE eixo usa, é uma condição SUFICIENTE de segurança, não a análise fina do escape:

> **Predicado de libertação (região-de-fase):** uma região R pode ser largada num ponto P se, e só
> se, NENHUM valor vivo depois de P contém um ponteiro para dentro de R. A condição suficiente que
> este eixo garante por CONSTRUÇÃO (não por análise): antes de largar R, **CLONA-se** o único valor
> que sobrevive a P (a `TProgram`) para a região que OUTVIVE R (a root), e a partir daí R não é
> alcançável por nenhum valor vivo. O drop de R torna-se seguro por definição do clone total.

Relação com a análise de escape (`escape.tks`) e com o by-address-escape:

- **O eixo NÃO depende de `is_unique_at`/`binding_is_block_local`/a espinha F1.** Ao contrário do
  `modelo-de-memoria` (que decide residência POR VALOR via `pt_join`), a arena-por-fase não pergunta
  "este valor escapa?": envolve a fase inteira numa região e materializa a FRONTEIRA por um clone
  explícito. Isto é deliberado — o clone é uma barreira grossa e AUDITÁVEL, imune à imprecisão do
  escape. O escape só entra como a garantia de que o clone é TOTAL (§7).
- **O by-address-escape (a classe N112) é exatamente o que o clone NEUTRALIZA.** O crash N112 nasce de
  um `tk_slice_elem_box` (elemento por-endereço) alojado numa região que dropa enquanto o slice ainda
  segura o endereço. O clone total copia o elemento POR VALOR para a root; depois do clone, `prog`
  não segura nenhum endereço em R_front. **Um clone que copie um slice de structs por-VALOR (não o
  ponteiro do elemento) é precisamente o oposto do padrão N112** — é a rota-C-immune ("holds
  `[]struct` BY VALUE", n112.0 §"The exact fault") aplicada à fronteira de fase.
- **A recusa que fecha o UAF:** o clone é INCOMPLETO ⇒ `prog` segura um ponteiro em R_front ⇒ o drop
  é UAF. Logo o crumb que ACENDE o drop vem DEPOIS do crumb que prova o clone total, e a prova é o
  fixpoint (§3, §7): um campo esquecido no clone produz uma `prog` estruturalmente errada e o
  fixpoint `gen2==gen3` reprova ANTES de qualquer drop existir.

---

## 3. Sequência de crumbs ORDENADA (cada um gate-able sozinho)

Ordem: o mais SEGURO e de MAIOR rendimento primeiro (a fronteira de fase com copy-out limpo), depois
a str nativa, depois o afinamento fino. Cada crumb tem: (a) fixpoint `gen2==gen3` byte-idêntico E
(b) uma queda de pico de RSS NATIVO medida. **Sem ganho medido, não entra.**

### Crumb M0 — instrumento: pico de RSS por FRONTEIRA de fase (pré-requisito de medição)

**Classe:** orquestração (`project.tks`) + (opcional) uma linha C.
**O quê:** imprimir, em cada `phase_end_ok` do frontend e na entrada do codegen, o RSS CORRENTE e o
pico. `report_peak` (`project.tks:5353`) já imprime `teko: memory: peak N.N MB` via `tk_peak_rss`
(`teko_rt.c:3942`, `getrusage`/`ru_maxrss` — PICO, monotónico). Para ATRIBUIR onde o pico sobe,
precisa-se do RSS CORRENTE por fase; `ru_maxrss` só dá o máximo-até-agora.
**A única linha C candidata (justificada, mínima):** `uint64_t tk_cur_rss(void)` — lê
`/proc/self/statm` (Linux) e devolve RSS residente corrente em bytes, `0` onde indisponível (mesmo
contrato de `tk_peak_rss`). Justificação: nenhuma primitiva de runtime dá RSS corrente; sem ela a
atribuição por-fase é cega e nenhum crump seguinte é gate-able por "onde caiu o pico". É aditiva,
comportamento-idêntico (só lê), e é o gémeo exato de `tk_peak_rss` que já existe.
**Colisão:** `project.tks` (aditivo), `teko_rt.{c,h}` (uma função de leitura).
**Gate:** builda; `teko test .` verde; sem chamadores no emit ⇒ FIXPOINT trivial. Ritual: NÃO.
**Medição:** produz a LINHA-DE-BASE — o pico corrente ao fim de checker / mono / consteval / no
codegen START. É contra esta base que M1–M3 provam o ganho.

### Crumb M1a — o clone TOTAL da `TProgram` (a prova de segurança, drop AINDA DESLIGADO)

**Classe:** orquestração pura (`project.tks`) + travessia nova.
**O quê:** `clone_tprogram_into_current_region(prog): checker::TProgram` — uma cópia estrutural
TOTAL, por-VALOR, de `TProgram` (recursiva sobre `TItem`/`TStatement`/`TExpr`/`Type` e todos os
slices/str), alocando na região CORRENTE. Rotear o codegen através dela SEM ainda envolver o frontend
em região nem largar nada:
```
let prog = checked_program_of(...)                   // como hoje, em root
let prog2 = clone_tprogram_into_current_region(prog) // NOVO — clona em root (current == root aqui)
emit_native_x86(dir, od, stem, prog2, m, debug)      // codegen sobre o CLONE
```
Com o drop DESLIGADO, `prog` continua vivo, então um campo ESQUECIDO no clone faz `prog2` ser
estruturalmente MENOR/errada ⇒ o codegen emite bytes diferentes ⇒ **o fixpoint reprova**. É a rede
que prova que o clone é total e value-exact ANTES de o drop existir.
**Precedente de travessia total:** `tkb_write.tks`/`tkb_frame.tks` já serializam a superfície de
tipos (deps) — o clone é a MESMA disciplina de walk total, mas sobre corpos, para memória em vez de
disco. Não reusa o `.tkb` (esse é signatures-only), mas herda a sua forma de "visitar cada campo".
**Colisão:** `project.tks` (aditivo). Ficheiro novo `src/build/clone_program.tks` recomendado, para
isolar a travessia grande do orquestrador.
**Gate — RITUAL COMPLETO:** buildar gen2 `TEKO_BACKEND=native`; `teko test .` verde; **FIXPOINT
gen2==gen3 byte-idêntico** (prova o clone total+exato). Ritual: SIM (é a primeira vez que a `prog`
que o codegen vê passou por um clone).
**Medição:** pico NEUTRO ou ligeiramente ACIMA (o clone coexiste com o original). Aceita-se a subida
temporária — este crumb compra SEGURANÇA, não memória; M1b colhe a memória.

### Crumb M1b — região-por-FRONTEND: largar o overhang antes do codegen (a maior colheita)

**Classe:** orquestração pura (`project.tks`), reusa `region_new`/`enter`/`leave`/`drop`
(`project.tks:3588-3647`, já ligadas ao runtime e já usadas por `emit_native_x86`).
**O quê:** envolver o frontend inteiro numa região filha da root; depois de clonar a `TProgram` para
fora (M1a), largar essa região — reclamando TODO o morto-mas-retido do §1.2 antes de o codegen alocar:
```
let front = region_new(region_root())
region_enter(front)
let prog = checked_program_of(...)          // todo o scratch/copy-grow cai em `front`
region_leave()                              // current volta à root
let prog2 = clone_tprogram_into_current_region(prog)  // clona em root; lê `front` (ainda vivo)
region_drop(front)                          // <-- LIGA AQUI: larga selected/pre/table/checked/scratch
emit_native_x86(dir, od, stem, prog2, m, debug)
```
Depois do `region_drop(front)`, `prog2` não tem ponteiro nenhum em `front` (M1a provou-o por
fixpoint) — o drop é seguro pelo predicado §2. **Esta é a libertação de maior rendimento e a mais
segura**: um copy-out, um drop, a fronteira exata que o codegen consome, e ataca diretamente o
INSTANTE do crash (codegen START).
**Salvaguarda anti-UAF (converte um free prematuro num honest-stop):** espelhar
`check_current_region_is_root` (`project.tks:4294`) — sob `TEKO_REGION_CLONE_CHECK=1`, antes do drop,
o `region_drop` envenena os chunks de `front` (padrão `0x5`, o mesmo valor do crash N112) e uma
passagem de validação sobre `prog2` falta-para se tocar em veneno. Diagnóstico, desligado por
omissão, byte-idêntico quando off.
**Colisão:** `project.tks` (quente — coordenar com `arena-escopo-local`/`backend-memoria`; mas este
crumb toca só a orquestração de topo, não `emit_native_x86` nem `lower.tks`).
**Gate — RITUAL COMPLETO:** gen2 native; `teko test .` verde; **FIXPOINT gen2==gen3**; e o número que
JUSTIFICA o crumb: **pico de RSS no codegen START cai** (M0 mede antes/depois); `TEKO_ARENA_OBS`:
`scoped > 0`, `reclaim ratio > 0`. Ritual: SIM.
**Medição/alvo:** o overhang do frontend é a maioria dos 1926 MB (§1.2, ~1,8 GB copy-grow morto), logo
este crumb é o candidato a, sozinho, cruzar o teto de 2,5 GiB. Se cruzar o HARD mas não chegar ao
alvo de 1,5 GiB, seguem M2/M3.

### Crumb M2 — `tk_str_concat_len_r`: rotear os 66 MB de str NATIVA

**Classe:** runtime-twin (`teko_rt.{c,h}`, uma função) + lowering (`lower.tks`, 3 sítios).
**O quê:** o §4 detalha. Em suma: o backend NATIVO concatena str via `tk_str_concat_len` (par fat
`{ptr,len}`, `lower.tks:11030,12920,12922`), que **não tem gémeo `_r`** — daí os 66 MB irroteáveis.
(`tk_str_concat_r` de 2-word já existe, `teko_rt.h:493`, mas só a ROTA C o usa,
`codegen.tks:4190,4975`.) Adicionar `tk_str_concat_len_r` e rotear os 3 sítios do lowering para a
região corrente.
**Colisão:** `teko_rt.{c,h}` (uma função aditiva), `lower.tks` (3 sítios).
**Gate — RITUAL COMPLETO:** gen2 native; `teko test .` verde; **FIXPOINT gen2==gen3** (o `_r` com
região == root reproduz byte-a-byte o `_len` de hoje); `TEKO_ARENA_OBS`: `str unroutable` cai de
66 MB. Ritual: SIM.
**Medição/alvo:** 66 MB reclamáveis. Ajuda a aproximar o alvo de 1,5 GiB; sozinho não cruza o teto.

### Crumb M3 — [afinamento] região-por-FASE (checker | monomorph | consteval), se M1b não chega a 1,5 GiB

**Classe:** orquestração (`project.tks`) + dois clones extra.
**O quê:** M1b larga o overhang no FIM do frontend; se o PICO acontecer DENTRO do frontend (a
expansão do monomorph), M1b não o corta. M3 dá a cada fase a sua própria região com copy-out na
fronteira: checker→ clona `PreMono {prog, table}` → dropa; mono→ clona `TProgram` → dropa; consteval→
clona `TProgram` → dropa (= M1b). Três clones, três drops; cada drop reclama o copy-grow morto DAQUELA
fase, limitando o pico a `max(working-set de uma fase + o seu clone de entrada)`.
**Porque é afinamento e não a base:** o crash é no codegen START, não no meio do frontend — o
frontend COMPLETA sob o teto hoje. Logo M1b (largar no fim) é provavelmente suficiente para o HARD;
M3 é para o ALVO de 1,5 GiB se a medição M0 mostrar o pico intra-frontend acima de 1,5.
**Nuance de segurança:** a saída de cada fase PARTILHA estrutura com a sua entrada (`monomorphize`
reusa nós de `pre.prog`; `inline_consts` reusa nós de `checked`) — por isso **cada** fronteira EXIGE
o seu clone total; largar a região da entrada sem clonar seria UAF. É o mesmo predicado §2, aplicado
três vezes.
**Colisão:** `project.tks`. **Gate — RITUAL COMPLETO** por fronteira: FIXPOINT + queda de pico
intra-frontend medida (M0). Ritual: SIM.

### O que fica FORA (referenciado, não convertido em issue)

- **O per-scope da LINGUAGEM (move-on-return, os 5 escopos):** é o `modelo-de-memoria` C3/C4. É o
  fix PROFUNDO que reclama-durante-a-construção e acabaria por REMOVER a necessidade do clone (o
  scratch morreria no escopo em vez de sobreviver até o drop-de-fase). Fica FORA daqui — é mudança
  semântica grande e parte dela está bloqueada (cross-thread). Este eixo é o unblock imediato e
  independente; os dois compõem-se (arena-por-fase primeiro, per-scope depois estreita ainda mais).
- **grow-in-place / free-list por região** (reusar o buffer abandonado do copy-grow em vez de o
  abandonar): mataria o termo dominante NA ORIGEM, mas é uma mudança do alocador de regiões
  (`tk_region_alloc`/`tk_slice_push_r`) e é o território do F3-native (n112.0 §"What N112.1/N112.2
  buy"). REPORTADO como o afinamento de maior potencial adjacente; a decisão de o puxar é do dono.
- **`#arena_depth`/profiler-afinador:** `o-profiler-…`; ortogonal.

---

## 4. `tk_str_concat_len_r` — sub-desenho

### 4.1 O achado que corrige a nota do profiler

O `o-profiler-…` §6 declarou *"`tk_str_concat_r` NÃO EXISTE"*. Isso está **DESATUALIZADO** — foi
adicionado pelo `modelo-de-memoria` §9 e está na árvore hoje: `teko_rt.h:493-497`, `teko_rt.c:318-324`,
com a ROTA C já a usá-lo (`codegen.tks:4190`, `codegen.tks:4975`, guardado por `str_region.len > 0`).
**Mas isso é o `str` de 2-word `{ptr,bytes}`.** O backend NATIVO não usa esse: ele passa str como
par fat `(ptr, len)` e concatena via `tk_str_concat_len` (`teko_rt.h:522`), nos três sítios do
lowering — `lower.tks:11030` (concat variádico), `lower.tks:12920` e `:12922` (interpolação
`$"…{}…"`). **`tk_str_concat_len` não tem gémeo `_r`.** Logo, no NATIVO — a rota onde o fixpoint
vive — TODO `str` concatenado/interpolado é malloc'd e irroteável. São os 66 MB.

### 4.2 A assinatura (C MANTIDA, mínima e justificada)

Uma função de runtime. Justificação de cada elemento: o backend nativo exige a forma `(ptr,len)` que
`tk_str_concat_r` (2-word) não oferece, e não há como rotear um `str` nativo para região sem ela;
`r == root`/`NULL` reproduz o malloc atual, garantindo o byte-idêntico.

```c
/* tk_str_concat_len_r — o gémeo com-REGIÃO de tk_str_concat_len (teko_rt.h:522): concatena
 * a[0..a_len] ++ b[0..b_len] num buffer bump-alocado em `r` (tk_region_alloc(r, a_len+b_len))
 * em vez de malloc, devolvendo (ptr, *out_len) como tk_str_concat_len. O resultado vive em `r` e
 * morre quando `r` for largada — um str concatenado dentro de um escopo/fase participa da
 * morte-por-região. `r == tk_region_root()`/programa (ou NULL) delega em tk_str_concat_len:
 * comportamento e BYTES idênticos ao de hoje (a rede do fixpoint). O par (ptr,len) é a mesma ABI
 * de retorno que tk_str_concat_len já usa, então o lowering muda só o alvo, não a forma. */
const tk_byte *tk_str_concat_len_r(tk_region *r,
                                   const tk_byte *a_ptr, uint64_t a_len,
                                   const tk_byte *b_ptr, uint64_t b_len,
                                   uint64_t *out_len);
```

O corpo C é o de `tk_str_concat_len` com o `malloc(n)` trocado por `tk_region_alloc(r, n ? n : 1)` e
o guard `if (r == NULL || r == tk_region_root()) return tk_str_concat_len(a_ptr,a_len,b_ptr,b_len,out_len);`
à cabeça. Contabilidade `tk_obs_mstr_note` mantém-se para `r == root` (continua a contar como str de
root); para `r` de escopo o byte entra na árvore de regiões e o `TEKO_ARENA_OBS` passa a contá-lo
como `scoped`. Mais nada de C.

### 4.3 O rewire do lowering

Os três sítios de `lower.tks` passam a receber a região corrente. O selector de região já existe no
lowering — `region_current_vreg(ctx)` (`lower.tks:1594`) devolve o VReg da região corrente (ou emite
`tk_region_root_u` num stack vazio, o caso byte-idêntico). Os `lower_len_out_call(ctx,
"tk_str_concat_len", four_args(...))` passam a `lower_len_out_call(ctx, "tk_str_concat_len_r",
region_then_four_args(cur, ...))`, com a região corrente como primeiro argumento. O gémeo Teko em
`scope.tks` (espelhar a entrada de `str_concat`, `scope.tks:931`) e o mapeamento de símbolo em
`lower.tks` (o allowlist `lower.tks:4774` que já lista `tk_str_concat`) ganham a entrada `_len_r`.

### 4.4 A prova de byte-idêntico

Dois lados:

1. **Onde a região corrente é a root** (todo o código que hoje não está dentro de um escopo com
   região aberta), `region_current_vreg` devolve a root e `tk_str_concat_len_r(root, …)` delega em
   `tk_str_concat_len` — **os mesmos bytes, o mesmo buffer**. O fixpoint não pode mover-se aí por
   construção.
2. **Onde há região de escopo aberta**, o buffer muda de ENDEREÇO (região em vez de heap) mas os
   BYTES do conteúdo são idênticos (mesma concatenação). O codegen/emit não emite endereços de
   buffers de str — emite os bytes. E a de-duplicação de nomes do codegen é ESTRUTURAL, não por
   endereço (`cg_type_mangle_eq(a,c) = cg_opt_key(a) == cg_opt_key(c)`, compare de bytes,
   `al4a-interning-design.md:39,110-114`; `al4a:50`: um id node-carried *"would not survive copies"*),
   logo mudar o endereço de um buffer de str NÃO altera nenhuma decisão de emit. **O detector é o
   fixpoint gen2==gen3; a prova é que o valor emitido não é função do endereço.**

---

## 5. Portões de MEDIÇÃO

O número é o portão, não a intenção (regra da rodada: *"sem ganho medido, não entra"*).

**Instrumentos que já existem:**
- `report_peak` / `tk_peak_rss` — `teko: memory: peak N.N MB` (`project.tks:5353`, `teko_rt.c:3942`).
- `phase_begin`/`phase_end_ok` — os timers por fase (`progress.tks:255,287`), já a envolver
  checker/monomorph/consteval/codegen (`project.tks:330-374,2501`).
- `TEKO_ARENA_OBS` — o despejo de root/scoped/reclaim/drop-bytes (`teko_rt.c` §obs), a diagnose §7.

**A única adição de instrumento:** `tk_cur_rss` (M0) — RSS CORRENTE por fase, para ATRIBUIR onde o
pico sobe (o `ru_maxrss` é só o máximo). Opcional mas recomendado; sem ela M1b/M3 só se medem pelo
pico final, não pela atribuição por-fase.

**A matriz de gate (cada crumb, antes → depois, no NATIVO):**

| crumb | fixpoint gen2==gen3 | pico RSS nativo (alvo) | sinal `TEKO_ARENA_OBS` |
|---|---|---|---|
| M0 | trivial (sem chamadores no emit) | linha-de-base por fase | — |
| M1a | **byte-idêntico** (prova clone total) | neutro / +clone (aceite) | — |
| M1b | **byte-idêntico** | **cai no codegen START; cruza 2,5 GiB** | `reclaim > 0`, `scoped > 0`, `root` desce |
| M2 | **byte-idêntico** | ajuda (−66 MB) | `str unroutable` cai |
| M3 | **byte-idêntico** | corta o pico intra-frontend → alvo 1,5 GiB | `reclaim` sobe por fase |

**A inversão (tem de FALHAR quando o mecanismo é removido):** com o clone M1a INCOMPLETO (um campo
esquecido), o fixpoint TEM de reprovar — é o que garante que o drop M1b nunca é UAF. `TEKO_ARENA_OBS`
com `reclaim ratio == 0,0 %` depois de M1b **reprova a corrida** (o oráculo do actuador do
`o-profiler-…` §7): a raiz que DESCE é a única grandeza que não se satisfaz por acidente.

**O portão de correção é o NATIVO** (não gen1/rota-C): buildar gen2 `TEKO_BACKEND=native` sob
`ulimit -v 2621440`, `teko test .` verde, FIXPOINT gen2==gen3, diff C-vs-own inalterado.

---

## 6. A restrição NATIVE-FIRST

A parede é o self-emit NATIVO (n112.0 §"Memory-cap finding"): sob o teto, o overflow aflora como
SIGSEGV em `tk_region_alloc` na entrada do codegen; sem teto, o codegen completa mas a ~2,1 GB nas
pernas de artefacto e >2,5 GB no self-emit. **Cada medição de cada crumb é o pico NATIVO**, não o da
rota C. A rota C valida-se só por `teko test .` + fixpoint (não exercita o backend onde o OOM vive).
Consequências de desenho já respeitadas:

- M1a/M1b/M3 são orquestração em `project.tks`, que corre IGUAL nos dois backends — mas o GANHO só se
  mede no nativo (é lá que o overhang mata). O clone e o drop beneficiam ambos; o teto só existe no
  nativo.
- M2 toca a rota NATIVA especificamente (`tk_str_concat_len`, que a rota C nem usa) — é o crumb mais
  native-first dos quatro.
- A salvaguarda de region-check (`check_current_region_is_root`, `project.tks:4294`) já é native-only
  e já existe para exatamente esta classe; M1b reusa-lhe a forma.

A de-C é o palco seguinte; provas na rota C são secundárias. Nenhum crumb aqui depende da de-C.

---

## 7. Riscos e tensões de lei

| risco | produzido por | mitigação |
|---|---|---|
| **R1 — clone INCOMPLETO ⇒ UAF no drop** (a classe N112 reaberta) | um campo esquecido na travessia total ⇒ `prog2` segura ponteiro em `front` | M1a landa o clone com o drop DESLIGADO e o FIXPOINT prova-o total ANTES de M1b acender o drop; `TEKO_REGION_CLONE_CHECK` envenena+valida. O drop nunca precede a prova. |
| **R2 — o clone muda ENDEREÇOS e isso vaza para os bytes emitidos** | se o codegen dependesse de identidade-por-ponteiro (intern/dedup por endereço) | MEDIDO que não depende: dedup/mangle é ESTRUTURAL (`cg_opt_key ==`, `al4a:39,110`), ids node-carried *"would not survive copies"* (`al4a:50`). O clone muda só endereços; o valor emitido não é função do endereço. Detector: fixpoint. |
| **R3 — o clone é caro (uma cópia da TProgram inteira)** | uma travessia grande + a coexistência temporária clone+original (M1a) | o clone copia só o VIVO (a `TProgram`), que é a fração; o MORTO (o dominante, copy-grow) é o que o drop reclama. O balanço é largamente positivo (§1.2). M1a aceita a subida temporária; M1b colhe. |
| **R4 — interação com o fix de region-lifetime / o #112 rep pendente** | os dois mexem em regiões nativas | ORTOGONAL: este eixo é orquestração de topo (`project.tks`), NÃO toca `open_native_region`/`enter_self_append_fallback`/`bracket_to_root` nem a largura de rep. M2 toca só um símbolo de str novo. O #112 rep-flip é independente (n112.0 provou-o). Sem sobreposição de ficheiro no núcleo do bug. |
| **R5 — a região-por-fase muda a ORDEM de alocação e algum diagnóstico/símbolo determinístico** | envolver o frontend numa região desloca WHERE cada alloc cai | os VALORES não mudam (só o endereço); a ordem de iteração observável (diagnósticos, símbolos) é por-índice/estrutura, não por endereço. Fixpoint é o detector; se mover, PARAR e reexaminar (não é reescrita mecânica, é mudança de lógica disfarçada). |
| **R6 — `teko_rt` tocada** | `tk_str_concat_len_r` (M2) e `tk_cur_rss` (M0) | exceção explícita de runtime; ambas aditivas e comportamento-idêntico (delegam / só leem). Cada linha C justificada no §4.2 e §M0. Nenhuma emissão de C pelo compilador. |
| **R7 — colisão em ficheiro quente** | `project.tks` (M1/M3), `lower.tks` (M2) | M1/M3 tocam a ORQUESTRAÇÃO de topo (`compile_project`/`checked_program_of`), não `emit_native_x86` nem o lowering de região — coordena com `arena-escopo-local`/`backend-memoria` mas não colide no núcleo. M2 são 3 sítios pontuais + um allowlist. Clone em ficheiro NOVO (`clone_program.tks`). |

**Tensão de lei residual: NENHUMA que force HALT.** Teko-only respeitado (produto em `.tks`; as duas
funções C são a exceção de runtime, justificadas linha-a-linha). Fixpoint byte-idêntico é o portão
inviolável e o detector de todos os riscos. Issue-100 %: os quatro crumbs entregam a frente inteira
(fase + str), com M3 como afinamento condicional ao alvo. O achado adjacente (grow-in-place) é
REPORTADO, não convertido em issue. Carga aditiva bootstrap-safe: nenhum crumb ensina idioma novo ao
compilador — são orquestração e uma primitiva de runtime, a semente anterior constrói gen1 na mesma.

**Ordem obrigatória (o único ponto de engenharia que não é opinião):** **M1a (prova) SEMPRE antes de
M1b (drop).** Trocá-las é acender um free antes de a rede que o torna seguro existir — exatamente a
família de defeito que este projeto acabou de fechar (N112). M0 antes de tudo (sem base não há gate).
M2 e M3 podem entrar em qualquer ordem após M1b.
