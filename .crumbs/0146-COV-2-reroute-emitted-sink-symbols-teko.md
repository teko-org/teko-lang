---
seq: 0146
crumb-id: COV-2
milestone: M2
gate: "[fixpoint]"
reseed-class: "none (folds into 0147)"
deps: [0145]
sources:
  - "DECISION_LOG.md:1079"                            # D113 item (3): 16 fns não-arquivo reroteadas
  - "src/codegen/codegen.tks:3798-3811"              # builtin dispatch cov_* → tk_cov_*
  - "src/codegen/codegen.tks:19-42,7915"             # emit_cov_line/branch + cov_mark direto
  - "src/codegen/codegen.tks:10192-10193,10225-10227,10241,10359-10360"  # shims de toggle nos mains de teste
  - "src/lir/lower.tks:1983-1997"                    # espelho native builtin_cov_symbol
---

# 0146 · COV-2 — reroteia os 16 símbolos de sink emitidos → `teko::runtime::cov_*`

> Todos os sítios que EMITEM `tk_cov_*` (dispatch de builtin + `emit_cov_line`/`emit_cov_branch` +
> `cov_mark` direto + os toggles nos `main` de teste + o espelho native do `lower.tks`) passam a emitir
> os símbolos Teko-mangled `teko_teko__runtime__cov_*` do 0145. Padrão idêntico ao `intern_get`
> (codegen.tks:3816) e ao 0134. Frio → pico FLAT.

## Goal

Flip atômico do backing: depois deste crumb, TODO `cov_*` emitido (no programa de teste FILHO e nas
queries do COMPILADOR em `coverage.tks`) escreve/lê os sinks Teko do 0145 em vez dos globais C. Os
`tk_cov_*` C do lado-sink ficam MORTOS (referenciados só a partir daqui, agora reroteados). O único
delta de bytes no `teko.c` é: (a) `coverage.tks` (parte do compilador) passa a chamar
`teko_teko__runtime__cov_branch_hit`/`_line_hit`/`_is_marked`/`_distinct` — muda o C do compilador; (b) a
emissão dos mains de teste muda — mas o self-build NÃO compila com `-coverage` → o `teko.c` do próprio
compilador NÃO contém emissão de teste → esse delta é INVISÍVEL ao fixpoint. Portanto o fixpoint segura
com a mudança só das 4 queries de `coverage.tks`. TODA-OU-NADA (D113): o split transitório de estado
entre C e Teko é invisível ao build seco (frio) e a corretude é validada no SHADOW do 0147.

## Where

- `src/codegen/codegen.tks:3798-3811` — as 14 entradas do dispatch de builtin: trocar
  `builtin = "tk_cov_reset"` → `builtin = cb_fn_name_str("teko::runtime", "cov_reset")` e idem para
  `cov_mark`, `cov_distinct`, `cov_is_marked`, `cov_branches_on`, `cov_branch_reset`, `cov_enter`,
  `cov_leave`, `cov_branch`, `cov_branch_hit`, `cov_lines_on`, `cov_line_reset`, `cov_line`, `cov_line_hit`
  (mesma forma do `intern_get` na linha 3816).
- `src/codegen/codegen.tks:23,33` — `emit_cov_line`/`emit_cov_branch`: `buf.write("tk_cov_line_at(")` →
  `buf.write(cb_fn_name_str("teko::runtime", "cov_line_at")); buf.write("(")`; idem `cov_branch_at`.
  (O sufixo `ULL,`/args segue igual — os sinks Teko recebem `u64`/`u32` na mesma ordem.)
- `src/codegen/codegen.tks:7915` — `buf.write("    tk_cov_mark(")` → mangled `cov_mark`.
- `src/codegen/codegen.tks` — TODOS os literais `tk_cov_<sink>(` emitidos nos `main`/test-call de teste:
  `:10192-10193`, `:10225-10227`, `:10241`, `:10252-10253` (variante ProgramCov `branches_on/lines_on(true)`),
  `:10307` (`cov_enter`), `:10309` (`cov_leave`), `:10359-10360` (`emit_test_call_analyze`
  `reset/branch_reset/line_reset`+`enter`), `:10362` (`cov_leave`) — trocar cada literal `tk_cov_<x>(`
  pelo mangled. **REGRA (não só as linhas listadas):** o implementer faz `grep "tk_cov_"` no codegen APÓS
  as trocas e garante que só sobram `tk_cov_dump`/`__tk_cov_atexit_dump` (0147); qualquer outro sink =
  reroteado. (Os DUMPs `tk_cov_dump`/`getenv`/`atexit` NÃO são tocados aqui — ficam para o 0147.)
- `src/lir/lower.tks:1984-1997` — `builtin_cov_symbol`: cada `return "tk_cov_<x>"` → `return
  "teko_teko__runtime__cov_<x>"` (espelho native, escrito-não-rodado; padrão `builtin_int_to_str_symbol`
  na linha 2036 que já retorna `teko_teko__runtime__i64_to_str_len`).

`coverage.tks` NÃO é editado (as queries `teko::cov_*` são chamadas de builtin — o reroteio do dispatch
já as redireciona). `scope.tks` NÃO é editado (as assinaturas de builtin permanecem; só o símbolo
emitido muda).

## How

1. Aplicar as trocas de símbolo listadas em Where. Cada `cb_fn_name_str("teko::runtime", "cov_X")` produz
   `teko_teko__runtime__cov_X` — o mesmo mangling que o 0145 define. Aridade/tipos casam 1:1 com os sinks
   do 0145 (o dispatch já validou as assinaturas via `scope.tks:488-522`).
2. `cov_line_at`/`cov_branch_at`/`cov_mark` são emitidos por texto direto (não via dispatch) → usar
   `cb_fn_name_str` para o nome e reaproveitar a lista de args existente sem mudança.
3. lir: `builtin_cov_symbol` retorna o mangled; `native_builtin_symbol` (lower.tks:2076) já roteia por ele.
   A perna native é escrita agora, RODA pós-marco (lei "escreve as DUAS direções").
4. **Consistência de estado (D113 TODA-OU-NADA):** TODOS os 16 sinks/queries viram Teko de uma vez — não
   deixar metade em `tk_cov_*`. Após este flip, o único backing vivo é o Teko (region_program); os globais
   C `tk_cov_ids/covb_ids/fn_stack/line_ids` deixam de ser escritos por qualquer emissão.
5. Confirmar (scout) que NÃO há outro sítio emitindo `tk_cov_<sink>(` fora dos listados (grep
   `tk_cov_` no `src/`): sobram apenas `tk_cov_dump`/`tk_cov_merge` (0147) e as chamadas C internas
   entre sinks C (mortas).

## Rulings & laws

- **D113 item (3) (DECISION_LOG:1079):** "16 fns não-arquivo reroteadas em scope.tks/lower.tks/codegen.tks
   p/ símbolos Teko-mangled". Recuperado FIEL (scope.tks não precisa de edição — as assinaturas ficam;
   codegen+lir fazem o reroteio; conta os 16 = 14 dispatch + `cov_line_at` + `cov_branch_at`).
- **D113 pico FLAT:** o self-build não compila `-coverage` → a emissão de teste não entra no `teko.c` do
   compilador; o único delta é o reroteio das 4 queries em `coverage.tks`. Frio → NÃO-CRESCER. Reportar pico.
- **Precedente `intern_get` (codegen.tks:3816) + 0134 (panic family):** mesmo mecanismo `cb_fn_name_str`.
- **D90:** `tk_cov_reset/mark/distinct/is_marked/branches_on/branch_reset/enter/leave/branch/branch_at/
   branch_hit/lines_on/line_reset/line/line_at/line_hit` = C MORTO após este crumb (deletados no F9);
   `teko_rt.c` intocado.
- **Teko-only / W15:** `.tks` só; sem `//`; sem doc novo (só edições de emissão).
- **Não-detectar-o-inexistente:** troca de emissão de construção real, não ramo impossível.
- **Fork protocol:** mapa D113 é a deliberação — sem fork.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do `bootstrap/teko.c`;
   commit por passo; **sweep `.tkt`/`.tkr`** (o codegen mudou string emitida); SEM reseed aqui (colhido no
   0147); fixpoint `gen2==gen3` byte-idêntico; reportar pico.

## Fixtures

`none — the fixpoint self-build exercises this` (o self-build compila `coverage.tks` com as queries
reroteadas e o fixpoint reproduz; a corretude runtime do coverage é o SHADOW do 0147).

## Gate

`[fixpoint]` — build gen2 + `gen2.c==gen3.c` byte-idêntico (o reroteio das queries de `coverage.tks`
reproduz-se; a emissão de teste não entra no self-build) + regressão verde. SEM harvest de seed (folds
0147). Verde = compila, fixpoint estável, pico NÃO-CRESCE.

## Deps

0145

## Done when

`grep tk_cov_ src/` só encontra `tk_cov_dump`/`tk_cov_merge` (0147) e chamadas C internas mortas; toda
emissão de sink aponta `teko_teko__runtime__cov_*`; fixpoint gen2==gen3 segura.
