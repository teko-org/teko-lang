# Fold-and-prune da AST por passo do pipeline — 0.3.1 (D153)

**Base:** `fix/retirement` (arena-tipo `Region`/`Arena` LANDADA — D149-D152; modelo por-escopo
LANDADO — D130). **Escopo:** DESIGN-ONLY (crumbs + doc, zero código de produto).
**Diretiva do dono (D153):** *"o fold tem que ocorrer a CADA passo do pipeline; quando fizer um
fold, tem que REMOVER as folhas (PODAR) — isso derruba a memória; não garanto queda drástica do
pico, mas com certeza ajuda a diminuir o peso."*

**Régua (D153):** `< 1 GB` de RSS no build seco é um **DESEJO, não portão** — a meta é chegar o
MAIS PERTO possível. Cada MB conta; o ratchet D68 segue (só baixa).

> **Este documento ATUALIZA** `projecao-ast-entre-fases-0.3.1.md` (v2), que desenhou a mesma
> jardinagem quando a arena ainda eram **funções soltas** (`region_new`/`region_drop_subtree`/
> `region_enter`). A arena virou **TIPO com métodos** (D149-D152); o mecanismo de poda re-expressa-se
> em `Region.child_sized()` / `Region.drop_subtree()` / `Region.alloc()`. As três provas de fundo do
> v2 (§1 alias-por-referência, §6 drop-safety, §2 `.tsym`) seguem VÁLIDAS e são referenciadas, não
> reproduzidas. O que muda: (a) superfície = métodos, sem intrínsecos/loose-fns (correção D153); (b)
> enquadramento POR-PASSO (não só por fase); (c) o segundo lever — **drena-por-unidade** (D118/RM-C9);
> (d) a ordem relativa ao sweep em voo (W1-W6 / migração 0184).

---

## 1. Onde está o peso HOJE (confirmado no código, não presumido)

O pipeline (`src/build/project.tks:204` `checked_program_of`) roda, em `TProgram` inteiro
(whole-program, `items: []@TItem()` — `tast.tks:96`):

```
parse    → parser::Program
check    → PreMono { prog: TProgram; table: TypeTable }   (typer.tks:5573)
mono     → TProgram novo                                  (monomorphize, project.tks:227)
comptime → TProgram novo                                  (expand_comptime, :238)
consteval→ TProgram novo                                  (inline_consts, :245)
codegen  → backend(prog)                                  (:1201, :2461)
```

**Três fatos do código que governam tudo (do v2 §1, reconfirmados):**

1. **O typed-AST NÃO é cópia do cru — ele É o cru, por referência.** `TItem() = TFunction |
   parser::TypeDecl | parser::UseDecl | @TStatement() | TConstDecl` (`tast.tks:95`) embute nós CRUS
   do parser direto no programa tipado. `TFunction` carrega `params: []parser::Param`,
   `doc: parser::DocSpan`, `has_doc: bool`, `guard: parser::Pred` (`tast.tks:75-91`); `TConstDecl`
   idem (`:93`). Cada `TExpr` carrega `line: u32; col: u32` (`:8-9`).
2. **Cada fase RECONSTRÓI a árvore mas RE-ALIASA o cru** (mono/consteval copiam `doc = gf.doc`,
   `guard`, `default_expr`, e duplicam `line`/`col` por nó). Ex.: `project.tks:2694` reconstrói
   `TFunction` re-aliando `params`/`doc`/`guard` e copiando `line`/`col`.
3. **A arena é `root`/`program`-region e reclaim mid-build = 0,0 %** (`ast-computed-arena-assessment-0.3.1.md`
   §1.2, `:87`). Todas as árvores das fases COEXISTEM: `pre.prog` + `pre.table` + `checked` +
   `expanded` + `inlined`, cada uma aliando a anterior. O pico em codegen é essa SOMA + o
   scaffolding do checker (`Env`, escopos, caches de resolução) que domina o baseline (~3140 MB
   pré-jardinagem no muro `TEKO_PHASE_RSS`).

**Conclusão (o alvo do fold-and-prune):** o peso NÃO é a AST final; é a **retenção acumulada** de
todas as fases + scaffolding, viva porque nada é dropado no meio. A AST não é imutavelmente
long-lived — ela deve ENCOLHER conforme o pipeline dobra+poda. Reconcilia o achado D153
("pico=AST/TAST"): a AST é o gargalo *porque não é podada*, não porque é irredutível.

**Estimativa de qual poda tira mais (ordenada; a instrumentar em FP-0):**

| Poda | O que reclama | Magnitude estimada |
|---|---|---|
| **Drop pós-consteval** (checker-scaffolding + `TypeTable`) | `Env`/escopos/caches + `regs`/`by_last`/`comptimes` — o dominante dos ~3140 MB | **MAIOR** (centenas de MB) |
| **Drop na fronteira mono** (parse + pré-mono) | árvores de parse + typed pré-mono após projetar | grande (o delta 3140→3407) |
| **`.tsym`** (posições fora da árvore) | `line`/`col` duplicados em ~4 fases → `nid` de 4 B + tabela única | fatia bounded, ~centenas de MB de trivia |
| **Drena-por-unidade** (corpo de fn pós-emit) | corpos de função após seu codegen — por-namespace | incremental, escala com o nº de fns |
| **Poda de campo** (`doc`/`type_constraints`) | trivia não-lida a jusante | menor (refina a cópia) |

---

## 2. Os PONTOS DE FOLD do pipeline — o que cada passo consome e o que fica MORTO

"Fold" = a transformação que consome o IR de entrada e produz o próximo. "Folha morta" = campo/nó
que NENHUMA fase a jusante lê (rastreado no código; v2 §3). PODAR = não copiar na projeção +
dropar a região da fase consumida.

| Passo (fold) | Entrada consumida | Folhas MORTAS após o fold (podáveis) | Sobrevive (copiar) |
|---|---|---|---|
| **parse → check** | `parser::Program` + scaffolding do parser | árvores de parse cruas NÃO-embutidas no typed; após check o `Env`/escopos/caches do checker | o typed `PreMono{prog,table}` |
| **check → mono** | `PreMono.prog` (pré-mono) | `doc`/`has_doc`/`DocSpan` de toda decl (mono só COPIA, `monomorph.tks:1035`) | `prog` mono + `table` |
| **mono → comptime → consteval** | `checked`/`expanded` | — (passthrough) | `inlined` |
| **consteval → codegen** | `inlined` + `table: TypeTable` | a **`TypeTable` INTEIRA** (`regs`/`by_last`/`comptimes`) — NÃO é passada ao `backend` (`project.tks:2461` recebe só `prog`); `DocSpan`/`doc`/`has_doc` (codegen nunca lê); posições literais → `.tsym` | `prog` slim (typed final) |
| **codegen: por-namespace** | `prog.items` por namespace | o **CORPO** (`body`) de cada `TFunction` após ser emitido (só o próprio emit o lê) | `TypeDecl`/`UseDecl`/`Param` (cross-lidos por `cg_find_decl`) até o FIM |

**Nota de segurança do último passo (drena-por-unidade):** codegen LÊ o cru `parser::TypeDecl`
CROSS-namespace (`codegen.tks` `cg_find_decl`, corpos de struct/class/interface, `f.params`) — logo
TypeDecl/UseDecl/Param **NÃO** são podáveis por-namespace; só o **corpo de função** (lido apenas pelo
seu próprio emit) é. Ver §4.

---

## 3. O MECANISMO DE PODA — via a arena-tipo (`Region` métodos), zero C

O golpe grande NÃO é remover campos — é **QUEBRAR O ALIAS** com uma cópia-projeção total, para então
DROPAR a região INTEIRA da fase consumida (incl. o scaffolding, não só a árvore). A poda de campo
(§2) é refinamento secundário sobre a cópia.

### 3.1 O bracket de jardinagem, em MÉTODOS (D149-D152)

```
child   = parent.child_sized(floor)      // Region-filha própria da fase N
child.enter()                            // aloca-corrente = child (ver 3.2)
  out_raw = <roda a fase N; TODA alocação cai em child>
child.leave()                            // restaura o pai
out_slim = project_program(out_raw, parent)   // deep-copy TOTAL p/ o pai, PODA doc, copia só nid
child.drop_subtree()                     // reclama TODO o working-set da fase N (árvore + scaffolding)
// out_slim vive em `parent`, alias-free
```

- `parent.child_sized(floor)` (arena.tks:1239) — abre a filha dimensionada.
- `child.drop_subtree()` (arena.tks:1247) — `region_drop_subtree` → `ar_munmap` devolve ao SO.
- `project_program(out_raw, parent)` — deep-copy total; aloca em `parent` (via `parent.enter()`
  interno, ou `parent.alloc()` explícito nos construtores), interna cada string, reconstrói cada nó
  copiando só o `nid` (4 B). NÃO aponta para NADA de `child`.

O pico deixa de ser a SOMA das fases e passa a `MÁX(uma fase viva + a árvore slim projetada)`.

### 3.2 A adição de teaching necessária — `Region.enter()` / `Region.leave()` (método, não intrínseco)

O bracket precisa dizer "as alocações desta fase caem em `child`". Hoje isso são as loose-fns
`region_enter`/`region_leave` (arena.tks:824/828) — a **correção D153** manda usar a NOVA superfície
(métodos, sem intrínsecos/loose). Adiciona-se dois métodos finos ao tipo `Region`, delegando às
loose existentes (que na Passada-3 do 0184 já viram privadas):

```teko
/** enter — torna esta região o alvo de alocação corrente (empilha a anterior). */
fn enter() { region_enter(region_to_ptr(self.addr)) }

/** leave — restaura o alvo de alocação anterior a esta região. */
fn leave() { region_leave() }
```

É o MESMO mecanismo ambiente que a emissão por-escopo do codegen já usa
(`open/close_native_region`). Custo: 2 métodos delegadores, zero comportamento novo.

### 3.3 Zero-dangling (drop-safety R2) — a armadilha LOAD-BEARING

Após `project_program`, NENHUM ponteiro em `out_slim` pode endereçar `child`. Aliases a quebrar
(v2 §6, reconfirmados no código):

| Alias | Sítio | Como a projeção quebra |
|---|---|---|
| `TProgram.items` embute `parser::TypeDecl`/`UseDecl` | `tast.tks:95` | deep-copy do `TypeDecl`/`UseDecl` inteiro |
| `TFunction.params` (Param cru, com `default_expr`) | `project.tks:2694` passthrough | copiar o array `Param` recursivo |
| `doc = gf.doc` (`DocSpan`) | `monomorph.tks:1035` | **PODAR** (vira `docspan_none()`) |
| `guard = gf.guard` (`Pred`) | idem | deep-copy da árvore `Pred` |
| posições | ex-`line`/`col` | já FORA da árvore (`.tsym`); copia só o `nid` (4 B) |
| **strings** (`str` = `{ptr,len}` → backing) | ONIPRESENTE | **internar** cada string em `parent` (copiar o backing) |

**A armadilha do R2 (a que derrubou `arena-por-escopo`):** `str` é `{ptr,len}`. Copiar um `str` copia
o CABEÇALHO mas ALIASA o backing. Se o backing vive em `child`, o `out_slim` "copiado" ainda aponta
para `child` → o `drop_subtree` libera o backing sob cabeçalhos vivos = UAF. **O deep-copy TEM de
internar/copiar cada backing para `parent`** (`intern_str_into`, §6-contrato). Postura: projeção
TOTAL por padrão; a poda só remove o que provadamente NENHUMA fase a jusante lê; **dúvida → COPIA**
(leak-safe, nunca UAF) — a mesma postura de `escape.tks`.

**Guard automático:** o modelo por-escopo já emite o **guard de null-deref/PARANOID** — uma poda
errada (drop com alias vivo) dispara pânico `arquivo:linha:coluna` em vez de segfault cru, e o
fixpoint gen2==gen3 diverge. O guard PEGA a poda errada; não se maquia — PARA e acha o alias.

---

## 4. Drena-por-unidade (o segundo lever, D118/RM-C9)

Complementa a jardinagem por-fase. Em vez de todos os módulos vivos até o fim do codegen, **poda o
que cada namespace não mais precisa DEPOIS de emitir**.

**A restrição dura (do código):** codegen lê `parser::TypeDecl`/`Param` CROSS-namespace
(`cg_find_decl`), então TypeDecl/UseDecl/Param de TODOS os namespaces sobrevivem até o fim. **Só o
CORPO de função (`TFunction.body`) é lido exclusivamente pelo seu próprio emit** → podável logo após.

Loop de codegen por-namespace (`backend`/`codegen_and_report`), desenho:

```
decls_survivor = <projeta TypeDecl/UseDecl/Param + assinaturas de TODAS as fns p/ a região de programa>
loop namespace ns in prog:
    child_ns = program.child()
    loop fn f in ns:
        emit_fn_body(f)          // grava no buffer global (whole-program, um cb)
        // o body de f agora está no C emitido; nada mais o lê
    child_ns.drop_subtree()      // reclama os CORPOS do namespace ns
// decls_survivor permanece até o link
```

**Ganho:** o pico de codegen deixa de segurar TODOS os corpos simultâneos; segura
`MÁX(um-namespace-de-corpos + decls-survivor + buffer)`. Incremental, escala com o nº de fns.

**Acoplamento a registrar (RM-C10):** a emissão é whole-program num **buffer global único** (`cb`),
e nomes temporários derivam de `buf.len` (gensym). Dropar o CORPO de `f` após emitir NÃO muda o
buffer nem o gensym (o body já virou bytes no `cb`) → **a drena de corpo NÃO depende de RM-C10**. O
que dependeria de RM-C10 é dividir o próprio `cb` por-unidade (Eixo C pleno) — FORA deste escopo. A
drena-por-unidade aqui é só a poda do body pós-emit, byte-preservante.

---

## 5. `.tsym` — posições para tabela lateral (estilo `.pdb`), não jogadas fora

Ortogonal e SOMA à jardinagem: encolhe cada CÓPIA (a jardinagem reduz o NÚMERO de cópias). Do v2 §2,
inalterado pela arena-tipo:

- Trocar `line: u32; col: u32` (8 B) por `nid: u32` (4 B) em `TExpr`/nós com posição.
- `nid` atribuído no PARSE em ordem determinística de pré-visita. Nós sintéticos (mono/consteval)
  herdam o `nid` do nó-origem (preserva o `line = e.line` de hoje) ou `nid = 0`.
- Tabela `nid → (path,line,col)` materializada por `codegen::tk_emit_tsym` (hoje stub, `codegen.tks:9685`).
- **Opção A (recomendada, primeiro):** codegen resolve `nid → line` no EMIT → `teko.c`
  byte-idêntico, sem reseed. Ganho = AST slim, sem duplicação por fase. **Opção B (depois, sob
  reseed):** emite `nid`, resolve em runtime — encolhe o `teko.c`/binário, muda emissão → reseed.

O `.tsym` também SIMPLIFICA a jardinagem: com posições fora da árvore, o deep-copy copia só 4 B, não
reconcilia `line`/`col`.

---

## 6. Contratos de tipo (Teko, full-Javadoc — copiar verbatim)

Módulos: `src/checker/gardening.tks` (novo — walk de projeção) + brackets em `src/build/project.tks`;
`Region.enter/leave` em `src/runtime/arena.tks`; `.tsym` em `src/codegen`.

```teko
/**
 * project_program — a PODA da jardinagem: deep-copy TOTAL de um TProgram para a região `dest`,
 * quebrando todo alias com a região da fase que o produziu e OMITINDO os campos que nenhuma fase a
 * jusante lê (doc/has_doc/DocSpan). As posições já vivem fora da árvore (.tsym); copia-se só o `nid`.
 *
 * Invariante de drop-safety (R2), load-bearing: ao retornar, NENHUM ponteiro na árvore resultante
 * endereça a região de origem — inclusive os BACKINGS de string, internados em `dest` (um str
 * copiado sem copiar o backing ainda aliasa a origem e causa UAF no drop_subtree).
 *
 * Determinismo (R6): walk em ordem fixa de campo, sem iterar map/hashset, sem ler endereço/timestamp
 * — mesma entrada produz a MESMA árvore e o mesmo teko.c.
 *
 * @param prog  o programa tipado a projetar (vive na região da fase de origem)
 * @param dest  a região de sobrevivência onde a cópia alias-free passa a viver
 * @return      um TProgram equivalente, alias-free em `dest`, com doc podado
 * @since 0.3.1
 */
fn project_program(prog: checker::TProgram, dest: teko::runtime::Region): checker::TProgram

/**
 * intern_str_into — interna uma string em `dest`, copiando o backing, de modo que o resultado não
 * aliase a região de origem. O primitivo de segurança de string do project_program (§3.3): sem ele
 * o deep-copy é incompleto e o drop_subtree vira UAF.
 *
 * @param dest  a região de sobrevivência (o backing é copiado para cá)
 * @param s     a string a internar (seu backing pode viver na região de origem)
 * @return      a string cujo backing vive em `dest`
 * @since 0.3.1
 */
fn intern_str_into(dest: teko::runtime::Region, s: str): str

/**
 * garden_phase — o BRACKET de jardinagem: roda a fase `run` numa Region-filha própria, projeta sua
 * saída para `parent` (de sobrevivência) e derruba a filha inteira (drop_subtree), reclamando todo o
 * scaffolding da fase (Env, escopos, tabelas) que a projeção não copiou. É o modelo por-escopo (D130)
 * aplicado na FRONTEIRA DE FASE; o drop só é seguro porque project_program quebrou todo alias.
 *
 * @param parent  a Region de sobrevivência onde a saída projetada passa a viver
 * @param run     a fase a executar (produz um TProgram na Region-filha)
 * @return        a saída da fase, projetada e alias-free em `parent`
 * @since 0.3.1
 */
fn garden_phase(parent: teko::runtime::Region, run: fn(): checker::TProgram | error): checker::TProgram | error
```

**Métodos NOVOS em `Region` (arena.tks) — teaching mínimo (§3.2):** `enter()`, `leave()`.
**`.tsym` (v2 §9):** `tsym_record`/`tsym_lookup` + corpo de `tk_emit_tsym`; campo `nid` na AST.

**Funções/tipos existentes tocados:** `checked_program_of` (`project.tks:204`) — inserir brackets;
`TExpr`/nós com posição — `line/col`→`nid`; sítios de emit de posição (`codegen.tks` cast-loc/cov) —
resolver via `tsym_lookup`; `backend`/`codegen_and_report` — loop por-namespace com drop de corpo.

---

## 7. Sequência de crumbs (fixpoint-safe; do mais-seguro/maior-ganho primeiro)

Fundação provada-idêntica ANTES de qualquer drop; `.tsym` byte-preservante primeiro; drops do maior
working-set ao menor. Fixpoint `gen2==gen3` + PARANOID a cada harvest (poda errada = UAF, o guard
pega). Crumbs `0185`-`0192`.

| seq | crumb-id | gate | reseed | o que faz |
|---|---|---|---|---|
| 0185 | FP-0 | [dry] | none | Instrumentar `report_phase_rss`: árvore-viva vs scaffolding-reclamável + fatia `line/col`. Baseline. |
| 0186 | FP-1 | [RITUAL] | fixpoint-rebuild | `.tsym` opção A: `nid` na AST + tabela lateral; codegen resolve no emit → `teko.c` byte-idêntico. |
| 0187 | FP-2 | [RITUAL] | fixpoint-rebuild | `Region.enter/leave` (teaching) + `project_program`/`intern_str_into` como transformação-IDENTIDADE (projeta, NÃO dropa). Rede de segurança R2. |
| 0188 | FP-3 | [RITUAL] | fixpoint-rebuild | `garden_phase` + **drop pós-consteval** (TypeTable + região do checker). **MAIOR queda isolada.** |
| 0189 | FP-4 | [RITUAL] | fixpoint-rebuild | **Drop na fronteira mono** (parse/checker-scaffolding antes de mono). |
| 0190 | FP-5 | [fixpoint] | fixpoint-rebuild | Poda de campo confirmada (`doc`/`type_constraints`) — medir NENHUM-leitor antes. |
| 0191 | FP-6 | [RITUAL] | fixpoint-rebuild | **Drena-por-unidade:** loop codegen por-namespace + drop do CORPO pós-emit (byte-preservante). |
| 0192 | FP-7 | [RITUAL] | fixpoint-rebuild | (opcional) `.tsym` opção B: emitir `nid`, resolver em runtime — encolhe `teko.c`/binário. |

**Reseeds:** 0186, 0187, 0188, 0189, 0190, 0191, (0192). Cada um muda a topologia de memória ou a
emissão → gate ritual completo + gen2==gen3.

---

## 8. Ordem relativa ao SWEEP em voo (não colidir)

O fold-and-prune é **efeito SEPARADO** do sweep por-escopo (D153: "un-crumbed / esforço diferente") e
toca os MESMOS arquivos que a migração arena-tipo (0184: `project.tks`, `arena.tks`, `codegen.tks`).
**Ordem obrigatória:**

1. **Sweep de superfície (W1-W6, `0156`-`0161`) + migração arena-tipo (`0184`) LANDAM PRIMEIRO.** O
   fold-and-prune usa `Region.child_sized`/`drop_subtree`/`enter`/`leave` — a migração 0184 tem que
   ter estabilizado a superfície de método e removido as loose-fns mortas.
2. **Depois, o fold-and-prune (`0185`+)** como onda própria pós-sweep. Não puxar pra frente — colide
   com o byte-mover de região do sweep no mesmo `codegen.tks`.

O `.tsym` (0186) e a drena-por-unidade (0191) são independentes do sweep (não tocam a emissão de
região por-escopo) — mas sequenciam DEPOIS por higiene de merge (evitar rebase sobre o codegen que o
sweep está reescrevendo).

---

## 9. Riscos e tensões de lei

- **R2 (drop-safety / UAF) — dominante.** Deep-copy incompleto (esp. backing de string) corrompe.
  RESOLUÇÃO: projeção TOTAL, intern obrigatório no contrato, FP-2 prova identidade ANTES de qualquer
  drop, PARANOID guard + fixpoint pegam a poda errada. Dúvida → copia.
- **R6 (fixpoint) — resolvido por construção.** `.tsym` A preserva o `.c`; `doc` podado é invisível
  ao codegen; drena-de-corpo é pós-emit (não muda o buffer/gensym); walks determinísticos. Opção B é
  mudança deliberada sob reseed.
- **R-A (custo transitório 2×).** A projeção paga 2× a árvore num instante. O ganho (reclamar
  scaffolding) domina; FP-0 mede antes. Se um alvo não couber no guard `ulimit -v 4718592`, aquela
  fronteira migra para o `.tkb`-DISCO do Eixo C (fallback contínuo, mesmo princípio).
- **R-B (poda de `type_constraints`).** Confirmar que codegen não as lê antes de podar (grep +
  medição em FP-5). Dúvida → não poda.
- **R-C (`nid` sintético).** Nós de mono/consteval sem posição: herdam o `nid` do nó-origem ou `nid=0`.

**FORK GENUÍNO (enunciado curto, para o dono ratificar):**

> **O bracket de jardinagem usa a região-corrente AMBIENTE (`Region.enter/leave`) para a fase alocar
> na filha — NÃO thread a região-como-parâmetro através de checker/mono/consteval.** O D130 §3 manda
> "região = PARÂMETRO implícito, NUNCA ambiente". A tensão: threadar região-param através das fases
> inteiras (todo alloc-site do checker/mono) é o **byte-mover de escala Eixo-C**, fora deste escopo;
> o ambiente aqui é single-thread, compile-time, e É o mesmo mecanismo que a emissão por-escopo do
> codegen (`open/close_native_region`) já usa. **Recomendação (law-first):** o veto D130 do ambiente
> mira o `_Thread_local`/tid-table de RUNTIME (correção + desbloqueio de threads); o ambiente
> compile-time do compilador consigo mesmo é fora desse alcance. Logo `Region.enter/leave` no bracket
> **passa** — não é o ambiente que o D130 baniu. Se o dono discordar, a alternativa é threadar
> região-param nas fases (Eixo-C, muito maior) — HALT para decisão só se ele quiser essa forma.

Nenhuma outra tensão de Lei. Teko-only (`.tks`), zero C (D148), reusa `arena.tks`.
