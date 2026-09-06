---
seq: 0145
crumb-id: COV-1
milestone: M2
gate: "[fixpoint]"
reseed-class: "none (folds into 0147)"
deps: []
sources:
  - "DECISION_LOG.md:1074-1079"                       # D113 mapa coverage TODA-OU-NADA
  - "DECISION_LOG.md:1077"                            # region_program + slots CTRL_* (padrão names_state)
  - "src/runtime/teko_rt.c:4351-4483"                # os 16 sinks C a reproduzir (fn/branch/line + fn-stack)
  - "src/runtime/teko_rt.tks:548-611"                # names_state: precedente de estado program-resident
  - "src/runtime/arena.tks:45-115,844-855"           # CTRL_* + accessor slot (names_state_slot)
---

# 0145 · COV-1 — sinks de coverage + estado em `region_program` (Teko)

> Os 16 sinks de coverage do C (`tk_cov_*`, `teko_rt.c:4357-4483`) reescritos em `teko::runtime`
> sobre um bloco de estado em `region_program()` endereçado por um slot novo `CTRL_COV_STATE` —
> espelho EXATO de `names_state`. Ainda NÃO reroteados (0146 faz o flip); aqui só DEFINE. Frio no
> build seco → pico FLAT.

## Goal

Entrega a maquinaria Teko dos sinks de coverage, definida e `exp`, mas ainda não consumida pela
emissão (a emissão segue apontando `tk_cov_*` até 0146). Byte-mover apenas por ADIÇÃO de defs em
`teko.c` (as fns novas entram na superfície `exp` → emitidas), sem tocar nenhum sítio de chamada
existente. Reproduz os corpos C byte-a-byte (mesmo packing de id, mesma dedup linear, mesma hash-set
open-addressing com rehash), trocando o backing `realloc`/`malloc` (heap libc) por crescimento manual
em `region_program()` — que SOBREVIVE ao `tk_arena_pop` (a propriedade que o C obtinha da heap libc).
O estado é per-task de graça: o control-block da arena é `_Thread_local` (`ar_control`), então
`CTRL_COV_STATE` já isola cada lane, preservando a semântica per-task do C. NÃO dirige o reseed
sozinho — o SEED do cluster (0145+0146+0147) é colhido UMA vez no 0147 (TODA-OU-NADA, D113).

## Where

- `src/runtime/arena.tks:113` (após `CTRL_PANIC_PENDING = 34544`) — NOVO `const CTRL_COV_STATE: u64 = 34552`
  (1 slot livre; `CTRL_BYTES = 34800` → 30 slots restantes). NÃO mover `CTRL_BYTES`.
- `src/runtime/arena.tks:855` (após `set_names_state_slot`) — NOVOS accessors `pub fn cov_state_slot(): u64`
  / `pub fn set_cov_state_slot(addr: u64)` (cópia literal do par `names_state_slot`, campo `CTRL_COV_STATE`).
- `src/runtime/coverage_rt.tks` (NOVO arquivo, `use teko::runtime` implícito no namespace `teko::runtime`) —
  o bloco de estado (`cov_state()`, offsets `COV_*`), os 16 sinks `exp`, e os helpers privados de
  crescimento/rehash. Namespace `teko::runtime` (dentro do fecho do prelúdio D121 — só toca `teko::mem`/
  `teko::sys`/`teko::runtime`, zero `teko::io`/stdlib).

NEW decls (todas `exp`, exceto helpers privados): `cov_state`, `cov_reset`, `cov_mark`, `cov_distinct`,
`cov_is_marked`, `cov_branches_on`, `cov_branch_reset`, `cov_enter`, `cov_leave`, `cov_branch`,
`cov_branch_at`, `cov_branch_hit`, `cov_lines_on`, `cov_line_reset`, `cov_line`, `cov_line_at`,
`cov_line_hit` + privados `cov_u64_grow`, `cov_dedup_add`, `cov_branch_id`, `cov_line_id`,
`cov_line_rehash`, `cov_line_insert`. NENHUM sítio existente é editado.

## How

1. Bloco de estado (offsets, mesma disciplina de `NS_*` em teko_rt.tks:570-592):

```teko
const COV_FN_PTR: u64 = 0
const COV_FN_N: u64 = 8
const COV_FN_CAP: u64 = 16
const COV_BR_PTR: u64 = 24
const COV_BR_N: u64 = 32
const COV_BR_CAP: u64 = 40
const COV_BR_ON: u64 = 48
const COV_FS_PTR: u64 = 56
const COV_FS_SP: u64 = 64
const COV_FS_CAP: u64 = 72
const COV_LN_PTR: u64 = 80
const COV_LN_CAP: u64 = 88
const COV_LN_N: u64 = 96
const COV_LN_ON: u64 = 104
const COV_BYTES: u64 = 112
const COV_FNV: u64 = 1099511628211
```

2. `cov_state()` — aloca-e-zera o bloco na primeira toca, program-resident (espelho de `names_state`,
   teko_rt.tks:600-611):

```teko
/**
 * Returns the per-task coverage state block, allocating and zeroing it in the program region the
 * first time a sink is touched so the three sinks survive the per-test arena rewind.
 *
 * @return  the state-block address
 * @since 0.3.1
 */
exp fn cov_state(): u64 {
    var existing = teko::runtime::cov_state_slot()
    if existing != 0 { return existing }
    var block = teko::sys::ptr_word(teko::runtime::region_alloc(teko::runtime::region_program(), COV_BYTES)) to u64
    var i: u64 = 0
    loop { if i >= COV_BYTES { break }; teko::mem::store_u64(block + i, 0); i = i + 8 }
    teko::runtime::set_cov_state_slot(block)
    block
}
```

3. Crescimento manual de array u64 em `region_program` (NÃO `list::push` — raw, espelho de names_state
   grow em teko_rt.tks:636). Dobra geométrica, copia por índice, purga-imediata do velho não se aplica
   (region append-only, frio):

```teko
/**
 * Grows the region-backed u64 buffer at (ptr_field, cap_field) of the coverage state to at least
 * `need` slots, copying the live prefix; a no-op when the current capacity already suffices.
 *
 * @param st         the coverage state block
 * @param ptr_field  the state offset holding the buffer base
 * @param cap_field  the state offset holding the buffer capacity (slots)
 * @param used       the number of live slots to copy forward
 * @param need       the minimum capacity required
 * @param seed       the initial capacity when the buffer is empty
 * @since 0.3.1
 */
fn cov_u64_grow(st: u64, ptr_field: u64, cap_field: u64, used: u64, need: u64, seed: u64) {
    var cap = teko::mem::load_u64(st + cap_field)
    if need <= cap { return }
    var ncap = if cap == 0 { seed } else { cap * 2 }
    loop { if ncap >= need { break }; ncap = ncap * 2 }
    var nptr = teko::sys::ptr_word(teko::runtime::region_alloc(teko::runtime::region_program(), ncap * 8)) to u64
    var old = teko::mem::load_u64(st + ptr_field)
    var i: u64 = 0
    loop { if i >= used { break }; teko::mem::store_u64(nptr + i * 8, teko::mem::load_u64(old + i * 8)); i = i + 1 }
    teko::mem::store_u64(st + ptr_field, nptr)
    teko::mem::store_u64(st + cap_field, ncap)
}
```

4. Sink de FUNÇÃO (dedup linear; C teko_rt.c:4357-4375). `cov_reset` zera `n` (mantém buffer):

```teko
/**
 * Records function-entry id `id` in the function-coverage sink, deduping; the sink counts the
 * distinct production functions that executed. Migrates the C `tk_cov_mark`.
 *
 * @param id  the prog.items index of the entered function
 * @since 0.3.1
 */
exp fn cov_mark(id: u64) {
    var st = cov_state()
    var n = teko::mem::load_u64(st + COV_FN_N)
    var p = teko::mem::load_u64(st + COV_FN_PTR)
    var i: u64 = 0
    loop { if i >= n { break }; if teko::mem::load_u64(p + i * 8) == id { return }; i = i + 1 }
    cov_u64_grow(st, COV_FN_PTR, COV_FN_CAP, n, n + 1, 64)
    teko::mem::store_u64(teko::mem::load_u64(st + COV_FN_PTR) + n * 8, id)
    teko::mem::store_u64(st + COV_FN_N, n + 1)
}
```

   `cov_reset()` → `store_u64(cov_state()+COV_FN_N, 0)`. `cov_distinct(): u64` → `load_u64(cov_state()+COV_FN_N)`.
   `cov_is_marked(id: u64): bool` → varre `[0..COV_FN_N)`.

5. fn-stack + sink de BRANCH (dedup linear; C teko_rt.c:4386-4432). O packing É LEI (bit-por-bit igual ao C):

```teko
/**
 * Packs (fn, line, col, outcome) into a branch-coverage id, matching the C bit layout exactly:
 * bit 54 base, fn at 38, line at 14, low-8 col at 6, low-6 outcome at 0.
 *
 * @return the packed branch id
 * @since 0.3.1
 */
fn cov_branch_id(fn: u64, line: u32, col: u32, outcome: u64): u64 {
    (1 to u64 << 54) + (fn << 38) + ((line to u64) << 14) + (((col to u64) & 255) << 6) + (outcome & 63)
}
```

   `cov_branches_on(on: bool)` → `store_u64(cov_state()+COV_BR_ON, if on {1} else {0})`.
   `cov_branch_reset()` → zera `COV_BR_N` e `COV_FS_SP`.
   `cov_enter(fn: u64)`: se `COV_BR_ON==0` retorna; `cov_u64_grow(st,COV_FS_PTR,COV_FS_CAP, sp, sp+1, 256)`;
   grava `fn` em `sp`; `sp++`. `cov_leave()`: se on e `sp>0`, `sp--`.
   `cov_dedup_add(st, ptr_field, n_field, cap_field, id, seed)` privado = dedup linear + grow (espelho de
   `cov_mark`, parametrizado) — reusado por branch e pelo merge.
   `cov_branch(line: u32, col: u32, outcome: u64)`: se off retorna; `fn = if sp>0 { fs[sp-1] } else { 0 }`;
   `cov_dedup_add(...COV_BR..., cov_branch_id(fn,line,col,outcome), 256)`.
   `cov_branch_at(fn: u64, line: u32, col: u32, outcome: u64)`: se off retorna; add direto (sem stack).
   `cov_branch_hit(fn: u64, line: u32, col: u32, outcome: u64): bool`: varre `COV_BR` por `cov_branch_id(...)`.

6. Sink de LINHA (hash-set open-addressing + rehash; C teko_rt.c:4438-4483). `cov_line_id(fn,line) = ((fn<<24)|line)+1`
   (≥1; 0 = slot vazio). Rehash aloca tabela nova em region + reinsere (NÃO free — region append-only):

```teko
/**
 * Rehashes the line-coverage open-addressing set into a fresh `ncap`-slot table in the program
 * region, reinserting every live id. `ncap` must be a power of two. Migrates the C `tk_line_rehash`.
 *
 * @param st    the coverage state block
 * @param ncap  the new table capacity (power of two)
 * @since 0.3.1
 */
fn cov_line_rehash(st: u64, ncap: u64) {
    var nt = teko::sys::ptr_word(teko::runtime::region_alloc(teko::runtime::region_program(), ncap * 8)) to u64
    var i: u64 = 0
    loop { if i >= ncap { break }; teko::mem::store_u64(nt + i * 8, 0); i = i + 1 }
    var oldp = teko::mem::load_u64(st + COV_LN_PTR)
    var oldcap = teko::mem::load_u64(st + COV_LN_CAP)
    var j: u64 = 0
    loop {
        if j >= oldcap { break }
        var id = teko::mem::load_u64(oldp + j * 8)
        if id != 0 {
            var h = (id * COV_FNV) & (ncap - 1)
            loop { if teko::mem::load_u64(nt + h * 8) == 0 { break }; h = (h + 1) & (ncap - 1) }
            teko::mem::store_u64(nt + h * 8, id)
        }
        j = j + 1
    }
    teko::mem::store_u64(st + COV_LN_PTR, nt)
    teko::mem::store_u64(st + COV_LN_CAP, ncap)
}
```

   `cov_line_insert(st, id)` privado: se `COV_LN_CAP==0` rehash 1024; senão se `n*2>=cap` rehash `cap*2`;
   probe linear com dedup; grava e `n++`. Reusado por `cov_line`/`cov_line_at` e pelo merge (0147).
   `cov_lines_on(on: bool)`; `cov_line_reset()` zera `COV_LN_N` + limpa a tabela (`store 0` em `[0..cap)`).
   `cov_line(line: u32)`: se off ou `line==0` retorna; `fn = topo do fs`; `cov_line_insert(st, cov_line_id(fn,line))`.
   `cov_line_at(fn: u64, line: u32)`: se off ou `line==0` retorna; insert direto.
   `cov_line_hit(fn: u64, line: u32): bool`: probe; false se `cap==0`.

7. Doc-comment `exp` em CADA fn `exp` (W15); zero `//`; helpers privados sem doc. Cada sink é a
   tradução DIRETA do corpo C citado — o implementer confere linha-a-linha contra `teko_rt.c:4357-4483`.

## Rulings & laws

- **D113 (DECISION_LOG:1074-1079):** migração TODA-OU-NADA; region_program + slots CTRL_* (padrão
   names_state); "2 dedup-lineares fn/branch, fn-stack, hash-set de linhas c/ rehash". Recuperado FIEL.
- **D113 pico FLAT:** coverage é FRIA no build seco (`cov_*` só emite no `main()` do programa de teste,
   nunca no self-build) → os sinks nunca crescem em compilação normal → critério = NÃO-CRESCER (a adição
   de defs `exp` é o único delta; frio). Reportar pico.
- **Per-task de graça:** `ar_control` é `_Thread_local` → `CTRL_COV_STATE` isola cada lane sem código
   extra (preserva a semântica per-task do C, comentário E1-C1 em teko_rt.c:4355).
- **NO PUSHES:** os sinks NÃO usam `teko::list::push`/`empty` — crescimento é raw region-alloc+copy manual
   (espelho de `names_state` grow, teko_rt.tks:636, precedente ratificado). Frio → não entra nos 93%.
- **D90:** `teko_rt.c`/`.h` INTOCADOS; os `tk_cov_*` C seguem vivos (0146/0147 os deixam mortos). Zero
   novo `from "teko_rt"`.
- **D121 (fecho do prelúdio):** `coverage_rt.tks` é namespace `teko::runtime` e só toca `teko::mem`/
   `teko::sys`/`teko::runtime` → dentro do fecho; NÃO importa `teko::io`/stdlib (dump/merge que precisam
   de I/O ficam no 0147, sobre `teko::sys::abi::os_*_raw`, ainda no fecho).
- **Teko-only / W15:** `.tks` só; doc só nos `exp`; sem `//`; flatten (guardas/early-return).
- **Não-detectar-o-inexistente:** só reproduz sinks de construção real; nenhum ramo impossível.
- **Fork protocol (dono 2026-08-19):** o mapa D113 é a deliberação; recuperado sem redesenho — sem fork.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do `bootstrap/teko.c`;
   commit por passo; SEM reseed aqui (colhido no 0147); fixpoint `gen2==gen3` byte-idêntico como guarda de
   dev; reportar pico.

## Fixtures

`none — the fixpoint self-build exercises this` (validação FUNCIONAL de runtime é o SHADOW do 0147, D117;
o self-build só prova que compila+reproduz. Zero `.tkr` novo — coverage não é exercitado pelo self-build,
mas a lei D113 manda validar pós-F9 via shadow, não fixture versionada).

## Gate

`[fixpoint]` — build gen2 + `gen2.c==gen3.c` byte-idêntico (as novas defs `exp` reproduzem-se) + regressão
existente verde. SEM harvest de seed (folds no 0147). reseed-class `none (folds into 0147)`. Verde =
compila, fixpoint estável, pico reportado NÃO-CRESCE vs baseline D121 (~1047 MB).

## Deps

`—`

## Done when

Os 16 sinks + `cov_state` + `CTRL_COV_STATE` + accessors existem em `teko::runtime`, compilam, o fixpoint
gen2==gen3 segura, e cada corpo casa linha-a-linha com `teko_rt.c:4357-4483` — sem nenhum sítio de emissão
ainda reroteado.
