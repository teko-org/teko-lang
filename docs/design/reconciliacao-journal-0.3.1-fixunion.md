# Reconciliação do subsistema `teko::journal` — vagão fix/union linear

status: DESIGN/ANÁLISE. Nenhuma linha de produto escrita nesta carga. Validação de build é
CI-only, sobre a reconstrução linear (SEM reprodução local — foi o degrau privado não rastreado
que causou o problema).

Autoridade: o spec do dono (artifact `952484a6-...`, "Journaling de corrida — premissa, produto e
entregável") é a régua de 100%. O design em `docs/design/journaling-de-corrida-0.3.1.md` (C1–C9) é
o roteiro dos crumbs. Ambos foram usados como critério.

---

## 1. Veredito da linhagem canônica

**Canônica = a biblioteca da MAINLINE evoluída (journal.tks 1186 linhas / summary.tks 559 linhas),
JÁ presente na `fix/union` em `5b6a638e`.** A `origin/cargo/0.3.1.0-journal-impl` (738/774) é uma
biblioteca PARALELA, desconectada, e é a versão errada. Justificativa por evidência do spec:

1. **Serve de base às peças "rodando" do spec.** As peças marcadas "rodando" são 3.1 binding
   (`8eb85369`) e 3.2 captura C0 (`81a3d9a0`) e 3.9 transporte medido. A captura C0 (`81a3d9a0`,
   toca `src/test/test.tks`, `codegen.tks`, `teko_rt.c`) é o canal de veredicto que o spec quer
   ATRIBUÍDO. Os hooks `note_test_begin/ok/panicked/exited/end` e `note_row_*` da canônica estão
   **fiados** a esse canal — provado em:
   - `src/codegen/codegen.tks:12909-12974` (o arnês de `#test` emitido chama `note_test_end` /
     `note_test_exited`);
   - `src/build/regression.tks:3125-3179` (o runner de regressão chama `note_row_begin/ok/fail/skip/end`).
   A `journal-impl` (738) **não tem nenhum desses hooks** e **não é chamada por lugar nenhum** —
   nunca foi fiada. Ela não serve de base a nenhuma peça "rodando".

2. **Atribuição (o PRODUTO do spec) só a canônica entrega no braço de sinal.** O `tk_journal_note`
   da canônica (runtime em `5b6a638e:src/runtime/teko_rt.c:4694+`) escreve o **prefixo armado**
   `run\twriter\tstop\t` ANTES do número do sinal (`tk_journal_arm(prefix: str)` formata a
   identidade no open, §6 do design). O `tk_journal_note` da `journal-impl`
   (`origin/cargo/0.3.1.0-journal-impl:src/runtime/teko_rt.c`) escreve `stop <sig>\n` FIXO, sem
   run/writer — **perde a atribuição**, que é exatamente o produto que o spec exige. `tk_journal_arm`
   da impl recebe só um `i64` (o fd), não o prefixo. Contradição direta do spec → impl é a errada.

3. **Invariante "sem timestamp": ambas cumprem, canônica de forma auditável.** `record_line`
   (`5b6a638e:src/journal/journal.tks:370`) é 4 campos `run\twriter\tkind\tpayload`, sem tempo;
   `mono_ns_rt` é usado SÓ para cunhar `run_id = <ns>-<pid>` (identidade única da corrida, linha
   245), não como campo de registro. Não é discriminador, mas confirma que a canônica não viola o
   spec.

4. **Completude.** Canônica (1186) é superconjunto da base `a5f66e92` (929) + a fiação J1–J6. Tem
   varredura de corrida-anterior com ferrolho vivo (`lock_pids`/`pid_in_lock`/`record_self_pid`/
   `root_is_live`/`rmtree`/`sweep_under`), `claim_root`, `is_dir`, `open_seg`. A impl (738) não tem
   nada disso — é uma biblioteca menor e incompleta.

Nota sobre §3.4 do spec (Rec{writer,seq,body}, RecBody = variant Assert|Cov, veredito como CAMPO da
asserção e não espécie de registro; MPSC bounded `push -> error|null`; `.tkj` binário append-only;
site_ids ⊆ chegaram; waitgroup; filtro #os/#arch): **NENHUMA das duas** bibliotecas implementa essa
superfície. Ambas usam `type Record{run,writer,kind,payload}` com `kind` string (ok/fail como
ESPÉCIE) e segmentos de texto tab-separados por descritor — o modelo PRÉ-spec. Toda a §3.4 é "por
construir" a jusante desta reconciliação, e aplica-se igualmente às duas; não é discriminador entre
elas. Esta reconciliação NÃO reescreve a superfície para o spec — só garante que a base linear certa
(a canônica, alinhada às peças "rodando" e à atribuição) é a que segue para o theory.

---

## 2. Contribuições genuinamente ÚNICAS da journal-impl — auditoria função-a-função

Comparei os inventários de símbolo das três `journal.tks` e das três `summary.tks`. Símbolos que a
`journal-impl` tem e a canônica não:

| símbolo (journal-impl) | file:line (impl) | tem análogo canônico? | veredito |
|---|---|---|---|
| `open_at(path,run,writer)` | journal.tks:129 | sim: `open_seg` + `open` (5b6a638e:388,406) | REJEITAR (duplicata inferior) |
| `close(j)` | journal.tks:189 | sim: `close_fd_rt` via `tk_rt_close_fd` (5b6a638e:991, usado em `note_open_append_close`:1048) | REJEITAR |
| `append_line(path,line)` | journal.tks:210 | sim: `append_raw` (5b6a638e:479) | REJEITAR (duplicata) |
| runtime `tk_journal_close` | teko_rt.c (+41) | NÃO — canônica fecha com `tk_rt_close_fd` (5b6a638e:3624) | REJEITAR: enxerto = DEAD CODE (zero chamadores na árvore canônica) |
| runtime `tk_rt_rmtree` / `tk_rmtree_cstr` | teko_rt.c (+~90) | NÃO — canônica faz `rmtree` em TEKO puro (5b6a638e:journal.tks:896, via `teko::fs::list_dir`/`remove_file`) | REJEITAR: enxerto = DEAD CODE (zero chamadores); ver §4 (achado reportado) |
| `summary.tks` — máquina `WriterStat`+`fold_writer_record`, `is_dead`, `cov_of_payload`/`cov_add`, `nth_field`/`parse_u64`, `stat_of`, `collect_findings` | summary.tks:109–536 | núcleo produtor MAIS rico que o canônico | **ABSORVER** (§3): portar o núcleo sobre a casca/render canônicos, unir `is_dead`, manter superfície pública |
| `summary.tks` — `LabelReason`/`split_first_tab`, `tally_add`, `writers_of`/`contains_name`, casca de render (`render_dead`/`cov_row`/`closing_line`/…) | summary.tks:140–155,297,314,633–774 | duplicatas do que a canônica já tem | REJEITAR (usar os equivalentes canônicos) |
| `summary_test.tkt` (152 linhas) | src/journal/summary_test.tkt | os `js_*` duplicam os `j4_*`; net-novo = verde-explícito + morto-sem-`incomplete` | **ABSORVER** as 2 provas novas no `journal_test.tkt` (§3.8); NÃO criar o ficheiro |

**Confirmação de coerência (o teste decisivo):** procurei na árvore inteira da `5b6a638e`
(`src/**/*.tks` + `*.tkt`) por qualquer chamador de `tk_journal_close`, `tk_rt_rmtree`,
`close_seg_rt`, `rmtree_rt`, `open_at`, `append_line`, `journal::close` — **zero resultados**. E o
runtime da `5b6a638e` **não define** `tk_journal_close` nem `tk_rt_rmtree`. Logo: enxertar o runtime
da impl é acrescentar C que ninguém chama = dead code puro, contra a lei de "issues são 100% sem
regressão" (introduz superfície morta).

**Conclusão: no `journal.tks` e no runtime, a `journal-impl` não contribui nada que precise ser
enxertado** — tudo é duplicata inferior ou dead code contra o grafo real (o runtime NOVO +152/+41
casa só com a própria impl, que não é chamada). **A ÚNICA contribuição genuína e valiosa está na
`summary.tks`**: o núcleo `WriterStat` (dead-detect por `plan` sem `end` + dobra de cobertura por
registro `cov`), mais rico e mais alinhado ao spec §3.8 que o núcleo canônico. Essa é RESOLVIDA e
absorvida na §3 (não adiada) — sobre a casca/render canônicos, sem re-fiar `project.tks`.

---

## 3. SOLUÇÃO CONCRETA — `summary.tks` unificado (une os dois lados, sem perder nada)

Não há tensão irreconciliável aqui: o `RunSummary` das DUAS versões já é campo-a-campo idêntico
(`run,passed,failed,skipped,never_ran,fails,skips,dead,cov,cov_missing`). A ÚNICA divergência de
struct é o `CovTriple` (nomes dos campos). Logo a união é: **manter a CASCA e o RENDER canônicos
(que são a saída fiada e observável, presos por todos os testes `j4_*`) e trocar SÓ o NÚCLEO
produtor (`summarize` + helpers privados) pela máquina `WriterStat` da impl**, adaptada. Assim
absorvo `is_dead`, a dobra de cobertura e o `cov_missing` medido, sem re-fiar `project.tks` e sem
perder uma linha.

### 3.1 Struct unificado (NÃO muda a superfície pública)

- `RunSummary` — **inalterado** (as duas versões já são idênticas). `project.tks` não muda.
- `CovTriple` — **mantém os nomes canônicos** `functions_cov/functions_val/lines_cov/lines_val/
  branches_cov/branches_val` (são os que `project.tks:7855-7862` e `journal_test.tkt` já escrevem).
  Os campos da impl (`fn_cov/fn_val/line_cov/line_val/br_cov/br_val`) são os MESMOS seis — só renomeio
  ao portar. Zero perda semântica.
- `WriterStat` — **NOVO** (privado, portado da impl `summary.tks:109-139`), com UM campo a mais que
  eu acrescento para não perder a detecção de morto da canônica:

```
/**
 * WriterStat — o agregado por-escritor que `summarize` deriva dos registros de UM escritor: o
 * plano, as contagens de veredicto, se fechou, se um `incomplete` sintético do `fold` o marcou, o
 * teste em que morreu e a cobertura que despejou. Privado: é a forma em que `summarize` dobra cada
 * escritor antes de preencher o `RunSummary`.
 *
 * @since 0.3.1
 */
type WriterStat = struct {
    /** o escritor deste agregado. */
    writer: str
    /** `unit`, `regression`, ou "" para o orquestrador. */
    phase: str
    /** quantos testes o escritor declarou que ia correr (0 quando não declarou plano). */
    plan: u64
    /** se o escritor declarou um plano. */
    has_plan: bool
    /** testes que passaram. */
    ok: u64
    /** testes que falharam. */
    fail: u64
    /** testes saltados. */
    skip: u64
    /** se o escritor escreveu um registro terminal (`end`/`stop`). */
    has_end: bool
    /** se o `fold` sintetizou um `incomplete` para este escritor (morreu com um `begin` aberto). */
    has_incomplete: bool
    /** o último teste que este escritor começou (aquele em que morreu, quando morto). */
    last_begin: str
    /** se o escritor despejou um registro `cov`. */
    has_cov: bool
    /** a cobertura que este escritor despejou. */
    cov: CovTriple
}
```

O campo `has_incomplete` é a ÚNICA adição minha, e é o que UNE as duas deteções de morto (§3.3).

### 3.2 O que se PORTA da impl (com duas substituições mecânicas)

Portar VERBATIM de `origin/cargo/0.3.1.0-journal-impl:src/journal/summary.tks` estas fns privadas,
aplicando as substituições **S1** e **S2** abaixo:

| fn portada | origem (impl summary.tks) |
|---|---|
| `parse_u64` (geral) | :191-211 |
| `nth_field` | :212-238 |
| `cov_of_payload` | :239-258 |
| `cov_add` | :259-275 |
| `stat_of` | :352-374 |
| `fold_writer_record` (+ arm `KIND_INCOMPLETE`, §3.3) | :375-399 |
| `with_last_begin`,`bump_ok`,`bump_fail`,`bump_skip`,`with_end`,`with_cov` | :400-491 |
| `with_incomplete` (NOVA, §3.3) | — |
| `is_dead` (UNIÃO, §3.3) | :492 |
| `ran_of` | :501 |
| `collect_findings` (usar `finding_of` canônico, §3.4) | :512-536 |

**S1 (nomes de CovTriple):** `fn_cov→functions_cov`, `fn_val→functions_val`, `line_cov→lines_cov`,
`line_val→lines_val`, `br_cov→branches_cov`, `br_val→branches_val`.

**S2 (kinds por const, não literal + Record sem qualificar):** `"plan"→KIND_PLAN`, `"ok"→KIND_OK`,
`"fail"→KIND_FAIL`, `"skip"→KIND_SKIP`, `"cov"→KIND_COV`, `KIND_END`/`KIND_STOP` já são const; e
`teko::journal::Record`→`Record` (summary.tks está no mesmo módulo `teko::journal`, ref nua como o
resto da canônica).

**Descartar da impl (redundante com a canônica):** `split_first_tab`/`LabelReason` (a canônica já
tem `payload_label`/`payload_reason`, 5b6a638e:224/234); `tally_add` (usar o `tally_bump` canônico,
:156, equivalente); `writers_of`/`contains_name` (usar `distinct_writers`/`str_list_contains`
canônicos, :188/:171); `pct`,`is_empty_run`,`is_clean_run`,`dead_line`,`render_dead`,`cov_row`,
`render_coverage`,`closing_line`,`render_summary` da impl — a CASCA de render fica a canônica.

**Remover da canônica (substituídas pela máquina WriterStat):** `parse_plan_count` (:259, →`parse_u64`),
`writer_plan` (:281), `is_verdict_kind` (:299), `writer_verdicts_seen` (:314), `never_ran_tally`
(:335). MANTER `finding_of` (:247) — `collect_findings` e a lista `dead` usam-na.

### 3.3 A única decisão semântica: UNIÃO das duas deteções de morto (nada descartado)

- Canônica: morto = todo registro `KIND_INCOMPLETE` (o `fold` sintetiza um por segmento com `begin`
  sem terminal, journal.tks:650-660). Pega quem começou e morreu.
- Impl: `is_dead = has_plan && !has_end`. Pega quem PLANEOU e nunca fechou — inclusive quem morreu
  ANTES de qualquer `begin` (caso que a canônica perde, porque o `fold` não emite `incomplete` sem
  `begin`).

Estas cobrem casos diferentes; **uno as duas** (perde-se zero):

```
/**
 * is_dead — se um escritor morreu: OU o `fold` marcou-o com um `incomplete` (começou um teste e não
 * o fechou), OU ele declarou um plano e nunca escreveu um registro terminal (morreu antes mesmo do
 * primeiro `begin`). A união das duas deteções — a da canônica (via `incomplete`) e a da impl (via
 * `plan` sem `end`) — para nenhum escritor morto escapar ao `never_ran` e à secção DEAD.
 *
 * @param st  o agregado de um escritor
 * @return    true sse o escritor morreu por qualquer das duas vias
 * @since 0.3.1
 */
fn is_dead(st: WriterStat): bool { st.has_incomplete || (st.has_plan && !st.has_end) }
```

E `fold_writer_record` ganha o braço que faltava na impl, para o `incomplete` do `fold` nomear o
teste morto e disparar `has_incomplete`:

```
    if r.kind == KIND_INCOMPLETE { return with_incomplete(st, r.payload) }
```

```
/**
 * with_incomplete — o agregado marcado como morto pelo `incomplete` sintético do `fold`, guardando o
 * teste em que morreu (`label`) para a secção DEAD o nomear.
 *
 * @param st     o agregado até agora
 * @param label  o teste que o `fold` viu aberto sem fecho
 * @return       o agregado atualizado
 * @since 0.3.1
 */
fn with_incomplete(st: WriterStat, label: str): WriterStat {
    WriterStat {
        writer = st.writer; phase = st.phase; plan = st.plan; has_plan = st.has_plan
        ok = st.ok; fail = st.fail; skip = st.skip; has_end = st.has_end; has_incomplete = true
        last_begin = label; has_cov = st.has_cov; cov = st.cov
    }
}
```

`never_ran` por escritor = `is_dead(st) ? max(0, st.plan - ran_of(st)) : 0`. Confere com a canônica
no fixture `j4_records_of_a_dead_shard(300,9)`: `ran=9`, `never_ran=291` (o `incomplete` não é
veredicto), `dead.len=1`. E confere com a impl no `js_records_of_a_dead_shard` (plano sem `end`, sem
`incomplete`): também morto. As DUAS provas passam.

### 3.4 A nova `summarize` (núcleo unificado, superfície canônica intacta)

```
/**
 * summarize — o veredicto de AMBAS as fases, função PURA dos registros que `fold` devolveu, dobrando
 * cada escritor no seu `WriterStat` (o plano, os vereditos, o fecho, a cobertura despejada) e daí o
 * `RunSummary`. `cov`/`cov_missing` saem do que os registros `cov` carregam (spec §3.8, a medição
 * transportada no próprio journal); um chamador que segura os somatórios do site-map completa-os com
 * `with_coverage` (é o que `project.tks` faz, e é o que a produção observa).
 *
 * PURA DE PROPÓSITO: corre sobre registros sintéticos num teste (§13.6) sem suite, e deixa
 * `--replay` reimprimir o mesmo bloco de uma raiz antiga depois do processo vivo ter partido.
 *
 * @param recs  os registros que `fold` devolveu
 * @return      o veredicto agregado
 * @since 0.3.1
 */
pub fn summarize(recs: []Record): RunSummary {
    let writers = distinct_writers(recs)
    mut passed = zero_tally()
    mut failed = zero_tally()
    mut skipped = zero_tally()
    mut never_ran = zero_tally()
    mut dead: []Finding = teko::list::empty()
    mut cov = zero_cov()
    mut cov_missing: u64 = 0
    mut w: u64 = 0
    loop {
        if w >= writers.len { break }
        let st = stat_of(recs, writers[w])
        passed = tally_bump(passed, st.phase, st.ok)
        failed = tally_bump(failed, st.phase, st.fail)
        skipped = tally_bump(skipped, st.phase, st.skip)
        cov = cov_add(cov, st.cov)
        if st.has_plan && !st.has_cov { cov_missing = cov_missing + 1 }
        if is_dead(st) {
            let ran = ran_of(st)
            let nr = if st.plan > ran { st.plan - ran } else { 0 }
            never_ran = tally_bump(never_ran, st.phase, nr)
            dead = teko::list::push(dead, Finding { phase = st.phase; writer = st.writer; label = st.last_begin; reason = "" })
        }
        w = w + 1
    }
    RunSummary {
        run = run_of(recs)
        passed = passed
        failed = failed
        skipped = skipped
        never_ran = never_ran
        fails = collect_findings(recs, KIND_FAIL)
        skips = collect_findings(recs, KIND_SKIP)
        dead = dead
        cov = cov
        cov_missing = cov_missing
    }
}
```

`dead` usa `reason = ""` (não "writer died before its run ended" da impl) para o `render_finding_line`
canônico imprimir exatamente `phase writer label` — a saída que a canônica já dava. `collect_findings`
reusa `finding_of` (canônico), então NÃO preciso do `split_first_tab`/`LabelReason` da impl.

### 3.5 RENDER e `with_coverage`: ficam a CANÔNICA, sem tocar

`render_summary`, `render_tally_line`, `render_finding_line`, `render_findings`, `cov_metric_pct`,
`render_coverage_line`, `run_is_green`, `render_closing_line`, `with_coverage` — **inalterados**
(5b6a638e:415-559). É a saída presa pelos `j4_*` e a que `no_skips_gate.sh` lê. A secção DEAD
canônica (`render_findings(s.dead)`) já imprime a lista `dead`.

### 3.6 `project.tks`: NÃO muda uma linha

`print_journal_summary` (5b6a638e:project.tks:7852-7864) chama `summarize` → `CovTriple{functions_cov
=...}` → `with_coverage(base, cov, 0)`. Como o `RunSummary` e os nomes de `CovTriple` ficam
idênticos, e `with_coverage` SOBRESCREVE `cov`/`cov_missing` com o site-map, a **saída de produção é
byte-a-byte a mesma**. A dobra de cobertura por journal e o `cov_missing` medido que a nova
`summarize` calcula só afloram no caminho `--replay`/sintético (onde `with_coverage` não é chamado) —
que é exatamente a direção do spec §3.8 e hoje não tem emissor de `cov`, logo dá 0. Esta é a ÚNICA
mudança de comportamento, e é intencional, forward-correta e invisível à produção fiada.

### 3.7 `journal.tks`: uma const aditiva

Adicionar (perto dos outros `KIND_`, 5b6a638e:journal.tks:190) — o doc do `Record` já lista `cov`
como kind, só falta a const:

```
/**
 * KIND_COV — o kind que um escritor acrescenta UMA vez ao despejar a sua cobertura: o payload são
 * seis campos `u64` separados por `FIELD_SEP` (functions cov/val, lines cov/val, branches cov/val),
 * que `summary::cov_of_payload` decodifica. A medição transportada no próprio journal (spec §3.8);
 * nenhum escritor a emite ainda, então a dobra dá 0 até o emissor existir — aditivo e sem chamador a
 * quebrar.
 *
 * @since 0.3.1
 */
pub const KIND_COV: str = "cov"
```

### 3.8 Teste: porte do `summary_test.tkt` para dentro do `journal_test.tkt` (nada de ficheiro novo)

O `summary_test.tkt` da impl existia porque aquele ramo NÃO tinha `journal_test.tkt`. A canônica JÁ
tem `journal_test.tkt` com o bloco `j4_*` (5b6a638e:journal_test.tkt:302-410) cobrindo nomeação de
achados, run-vazio-é-falha, morto-por-`incomplete`, tally por fase e `with_coverage`. Portar = ADD
os DOIS provas genuinamente NOVAS da impl (as que o `j4_*` não tem), no MESMO ficheiro, como `#test`
em full-Javadoc:

1. **Caminho verde explícito** (impl `js_a_clean_run_reads_as_passed`): uma corrida em que todo
   escritor planeou, atingiu cada veredicto e fechou com `end` fecha `run PASSED`. O `j4_*` só prova
   os caminhos que falham — falta a prova de que o verde renderiza verde.
2. **Morto por `plan` sem `end` SEM `incomplete`** (a via que a impl acrescenta, §3.3): um escritor
   com `KIND_PLAN` + alguns `KIND_OK` e NENHUM terminal e NENHUM `incomplete` é DEAD e não arredonda
   para verde. É o que prova que a união de `is_dead` (o braço `has_plan && !has_end`) está viva —
   sem ela, este fixture leria verde.

```
#test
/**
 * j4_a_clean_run_reads_as_passed — o positivo da vivacidade: uma corrida em que todo escritor
 * declarou plano, atingiu cada veredicto e fechou com `end` é a ÚNICA forma que lê `run PASSED`. Sem
 * ela, nada prova que o caminho verde renderiza verde — só que os que falham não o fazem.
 *
 * @throws quando uma corrida limpa não fecha `run PASSED`, ou vaza um marcador `run FAILED`
 * @since 0.3.1
 */
fn j4_a_clean_run_reads_as_passed() {
    mut recs: []Record = teko::list::empty()
    recs = teko::list::push(recs, j4_rec("jt4green", "s0", KIND_PLAN, "2"))
    recs = teko::list::push(recs, j4_rec("jt4green", "s0", KIND_BEGIN, "unit_one"))
    recs = teko::list::push(recs, j4_rec("jt4green", "s0", KIND_OK, "unit_one"))
    recs = teko::list::push(recs, j4_rec("jt4green", "s0", KIND_BEGIN, "unit_two"))
    recs = teko::list::push(recs, j4_rec("jt4green", "s0", KIND_OK, "unit_two"))
    recs = teko::list::push(recs, j4_rec("jt4green", "s0", KIND_END, ""))
    recs = teko::list::push(recs, j4_rec("jt4green", "r0", KIND_PLAN, "1"))
    recs = teko::list::push(recs, j4_rec("jt4green", "r0", KIND_BEGIN, "regr_one"))
    recs = teko::list::push(recs, j4_rec("jt4green", "r0", KIND_OK, "regr_one"))
    recs = teko::list::push(recs, j4_rec("jt4green", "r0", KIND_END, ""))
    let block = render_summary(summarize(recs))
    teko::assert::str_contains(block, "run PASSED")
    teko::assert::str_absent(block, "run FAILED")
}

#test
/**
 * j4_a_planned_writer_that_never_ended_is_dead — a via de morte que a canônica sozinha perdia: um
 * escritor que declarou um plano, atingiu alguns vereditos e morreu SEM terminal e SEM um
 * `incomplete` sintético (um fixture puro, não vindo do `fold`) é DEAD à mesma, e o `never_ran` é a
 * distância ao plano. Prova que o braço `has_plan && !has_end` de `is_dead` está vivo.
 *
 * @throws quando um escritor com plano e sem fecho não conta como morto nem enche o never-ran
 * @since 0.3.1
 */
fn j4_a_planned_writer_that_never_ended_is_dead() {
    mut recs: []Record = teko::list::empty()
    recs = teko::list::push(recs, j4_rec("jt4nofin", "s2", KIND_PLAN, "5"))
    recs = teko::list::push(recs, j4_rec("jt4nofin", "s2", KIND_BEGIN, "started_one"))
    recs = teko::list::push(recs, j4_rec("jt4nofin", "s2", KIND_OK, "started_one"))
    let s = summarize(recs)
    teko::assert::eq_u64(s.dead.len, 1)
    teko::assert::eq_u64(s.never_ran.total, 4)
    teko::assert::str_absent(render_summary(s), "run PASSED")
}
```

Um teste extra, aditivo, para a dobra de cobertura por journal (spec §3.8) — prova que um registro
`KIND_COV` é decodificado e somado, e que um plano sem `cov` conta como `cov_missing`:

```
#test
/**
 * j4_a_cov_record_folds_into_the_triple — a medição transportada no journal (spec §3.8): um registro
 * `cov` de seis campos é decodificado por `cov_of_payload` e somado ao triplo, e um escritor com
 * plano mas sem `cov` incrementa `cov_missing`. A prova de que a dobra existe antes de haver emissor.
 *
 * @throws quando o triplo não reflete o registro `cov`, ou o `cov_missing` não vê o dump ausente
 * @since 0.3.1
 */
fn j4_a_cov_record_folds_into_the_triple() {
    mut recs: []Record = teko::list::empty()
    recs = teko::list::push(recs, j4_rec("jt4cov", "s0", KIND_PLAN, "1"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "s0", KIND_BEGIN, "probe"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "s0", KIND_OK, "probe"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "s0", KIND_COV, "3\t4\t50\t60\t7\t8"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "s0", KIND_END, ""))
    recs = teko::list::push(recs, j4_rec("jt4cov", "r0", KIND_PLAN, "1"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "r0", KIND_BEGIN, "regr"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "r0", KIND_OK, "regr"))
    recs = teko::list::push(recs, j4_rec("jt4cov", "r0", KIND_END, ""))
    let s = summarize(recs)
    teko::assert::eq_u64(s.cov.functions_cov, 3)
    teko::assert::eq_u64(s.cov.functions_val, 4)
    teko::assert::eq_u64(s.cov.lines_cov, 50)
    teko::assert::eq_u64(s.cov.branches_val, 8)
    teko::assert::eq_u64(s.cov_missing, 1)
}
```

**NÃO** criar `src/journal/summary_test.tkt` — os `js_*` da impl duplicam os `j4_*`; o net-novo
deles entra como os três `#test` acima no `journal_test.tkt`. (O `js_the_summary_names_every_finding`
== `j4_summarize_names_every_finding`; `js_an_empty_run` == `j4_an_empty_run_is_a_failure_not_a_green`;
`js_a_dead_writer` == `j4_a_dead_writer_cannot_read_as_passed` — todos já existem.)

---

## 4. Achados adjacentes — REPORTADOS, não convertidos em issue por mim

1. **`tk_rt_rmtree`/`tk_rmtree_cstr` (C) da impl** poderia substituir o `rmtree` em Teko da canônica
   (5b6a638e:journal.tks:896) por um recursivo em C, mais rápido e sem depender de `teko::fs`. É
   otimização, não correção; hoje seria dead code. Candidato a follow-up SE a varredura em Teko
   provar ser gargalo. (A detecção de morto e a dobra de cobertura da impl NÃO ficam aqui — foram
   RESOLVIDAS e absorvidas na §3.)

---

## 5. PLANO de reconciliação (sobre a `fix/union` linear `5b6a638e`) — 1 commit

A `fix/union` já traz a linhagem canônica de `journal.tks`/runtime. O único enxerto genuíno é a
UNIÃO da `summary.tks` (§3). Tudo cabe em **UM commit** — `feat(journal): summary unifica dead-detect
+ cov-folding do journal-impl sobre a casca canônica`.

**Mudar (o enxerto da §3):**

1. **`src/journal/summary.tks`** — trocar SÓ o núcleo produtor. Remover `parse_plan_count`(:259),
   `writer_plan`(:281), `is_verdict_kind`(:299), `writer_verdicts_seen`(:314), `never_ran_tally`(:335)
   e a `summarize` antiga(:371-410). Adicionar o tipo `WriterStat` (§3.1), as fns portadas da impl com
   S1+S2 (§3.2), `with_incomplete`+`is_dead` unido (§3.3) e a nova `summarize` (§3.4). MANTER intactos
   os tipos, `finding_of`, `distinct_writers`/`str_list_contains`/`tally_bump`/`zero_*`/`phase_of`/
   `payload_label`/`payload_reason`/`run_of`, todo o RENDER e `with_coverage` (§3.5).
2. **`src/journal/journal.tks`** — adicionar `pub const KIND_COV: str = "cov"` junto aos `KIND_`
   (~:190) (§3.7). Nada mais.
3. **`src/journal/journal_test.tkt`** — adicionar os três `#test` da §3.8 (verde explícito,
   morto-sem-`incomplete`, dobra de `cov`). Manter todos os `j4_*`/`j*` existentes.

**NÃO mudar:**

4. **`src/build/project.tks`** — NÃO tocar (§3.6). `print_journal_summary` continua idêntica; saída
   de produção byte-a-byte igual (`with_coverage` sobrescreve `cov`/`cov_missing`).
5. **`src/runtime/teko_rt.{c,h}`** — MANTER. NÃO enxertar `tk_journal_close`/`tk_rt_rmtree`/
   `tk_rmtree_cstr` da impl (dead code; zero chamadores; a canônica fecha com `tk_rt_close_fd` e faz
   `rmtree` em Teko).
6. **`src/journal/summary_test.tkt`** — NÃO criar; o net-novo dos `js_*` entra no `journal_test.tkt`
   (§3.8).

**Abandonar:** `origin/cargo/0.3.1.0-journal-impl` — sem merge/cherry-pick. O que ela tinha de
valioso na summary está agora absorvido pela §3; o resto é duplicata inferior ou dead code (§2).

**Superfície pública final de `teko::journal` (inalterada para os chamadores):** `open`, `append`,
`append_raw`, `note`, `rename`, `scratch`, `fold`, `sweep`, `run_id`, `run_root`, os `note_test_*`/
`note_row_*`, tipos `Journal`/`Record`, consts `KIND_*` (+`KIND_COV` novo); e de `summary`:
`summarize(recs: []Record): RunSummary`, `with_coverage(s, cov, cov_missing): RunSummary`,
`render_summary(s): str`, tipos `RunSummary`/`PhaseTally`/`CovTriple`/`Finding`. Nenhuma assinatura
pública muda — a fiação da captura C0 e o `project.tks` continuam a compilar e a produzir a mesma
saída.

**Direção do spec:** a nova `summarize` lê a cobertura do PRÓPRIO journal (`KIND_COV`,
`cov_of_payload`/`cov_add`) — a medição transportada de §3.8 — e vê o dump ausente (`cov_missing`,
§2.4), caminhando para a superfície `Rec`/`RecBody` sem ainda a exigir (o `RunSummary`/`Record`
atuais permanecem; a §3.4 do spec reescreve-os depois, num crumb próprio).

**Verificação (CI-only, não corri local):** a matriz de testes que a reconstrução deve fazer passar —
`j4_summarize_names_every_finding`, `j4_an_empty_run_is_a_failure_not_a_green`,
`j4_a_dead_writer_cannot_read_as_passed`, `j4_summarize_tallies_pass_fail_skip_by_phase`,
`j4_with_coverage_replaces_only_the_coverage_fields` (existentes, todos preservados por construção) +
os três novos da §3.8. Tracei manualmente cada um contra a nova `summarize`/`is_dead` na §3.3/§3.8.
