# Poda / jardinagem da AST — projeção por fronteira de fase (0.3.1) — v2

**Base de leitura:** `fix/retirement` @ `f2382034`, cruzada com `perf/expurgo-total-mecanico`
(tip ~`271dda2d`). **Escopo:** LER + escrever design-doc. Nenhum código de produto. Este
documento transforma a pergunta do dono numa arquitetura executável (crumb sequence,
contratos de tipo, fixtures, prova de segurança) e termina com uma RECOMENDAÇÃO clara para
o dono decidir.

> **v2 — dois rulings do dono incorporados:**
> 1. **`.tsym` estilo `.pdb`:** posições (`line`/`col`) NÃO são jogadas fora — são MOVIDAS
>    para uma tabela lateral `.tsym` indexada por um id de nó da AST. A AST em memória passa
>    a carregar só o id compacto; a posição é resolvida por lookup quando um erro ocorre.
>    Substitui a conclusão da v1 ("line/col não são podáveis").
> 2. **`.tkb` é SÓ da perna NATIVE:** a rota de binário/C NÃO passa por `.tkb` — o despejo
>    entre fases do C-route é EM MEMÓRIA (jardinagem: cópia-projeção + drop de região).
>    `.tkb` é o formato do OBJETO PORTÁTIL pré-monomorph da perna native.

> **A pergunta do dono (preservada):** *"Uma vez que um trecho da AST é analisado, não dá
> pra 'comprimir'? Ou seja, dropar o que não é usado na próxima etapa, pra reduzir o quanto
> há na árvore para a próxima etapa?"*
>
> **Nomenclatura do dono para o mecanismo: poda / jardinagem (pruning / gardening).** Depois
> que uma fase analisa um trecho, você PODA os ramos/campos que não alimentam a próxima
> etapa — dirigindo a memória só para o que segue vivo. "Jardinagem" é o processo contínuo
> de tender a árvore fase a fase. O documento usa esse nome ao longo do texto.

**Meta do dono:** < 1 GB de RSS no build seco. **Muro atual** (`TEKO_PHASE_RSS`): after
checker 3140 · monomorph 3407 · consteval 3689 · codegen OOM (>4 GiB). O pico é RETENÇÃO
ENTRE FASES.

---

## 1. O que o código revela — por que "comprimir" hoje é impossível sem quebrar aliases

A intuição do dono está certa, mas o obstáculo é mais fundo do que "dropar campos". O
rastreio do código expõe TRÊS fatos que governam todo o desenho:

### 1.1 O typed-AST NÃO é uma cópia do cru — ele É o cru, por referência

`checker::TProgram.items` tem tipo `@TItem()`, cuja lowering é
(`src/checker/tast.tks:97`):

```
TItem() = checker::TFunction | parser::TypeDecl | parser::UseDecl | @TStatement() | checker::TConstDecl
```

Ou seja, **os nós CRUS do parser `parser::TypeDecl` e `parser::UseDecl` são embutidos
DIRETAMENTE no programa tipado** — não há versão tipada deles. E dentro do que É tipado, o
`checker::TFunction` (`tast.tks:71`) carrega campos crus por referência:

- `params: []parser::Param` — o array cru de parâmetros do parser (com `default_expr: Expr`
  cru dentro);
- `doc: parser::DocSpan`, `has_doc: bool` — trivia de doc-comment;
- `guard: parser::Pred` — a árvore de predicado de plataforma crua;
- `type_constraints: []@ConstraintExpr()` — as restrições crus.

`checker::TConstDecl` idem (`doc: parser::DocSpan`, `guard: parser::Pred`). Cada
`checker::TExpr` (`tast.tks:5`) carrega `line: u32; col: u32` — trivia de fonte, o alvo do
ruling `.tsym` (§2).

**Consequência:** o "trecho analisado" que o dono quer comprimir está fisicamente entrelaçado
com o cru. Não existe uma "AST crua" separada, à parte, que se possa soltar — o typed
aponta para dentro dela.

### 1.2 Cada fase RECONSTRÓI a árvore mas RE-ALIASA o cru — prendendo tudo vivo

O driver (`src/build/project.tks:204-256`, `checked_program_of`) roda:

```
checker  (type_program_with_deps_pre_mono) → PreMono { prog: TProgram; table: TypeTable }
mono     (monomorphize(prog, table))       → TProgram novo
comptime (expand_comptime(prog, table))    → TProgram novo
consteval(inline_consts(prog))             → TProgram novo
→ codegen (backend(prog))
```

Cada fase constrói uma árvore NOVA (mono reescreve corpos em `mono_walk`; consteval em
`inline_rw_expr`), mas **carrega os campos crus por referência**. Provas no código:

- `monomorph.tks:1035` — o instanciado sai com `doc = gf.doc`, `guard = gf.guard`
  (aliasa a `DocSpan`/`Pred` do genérico); os corpos passam por
  `default_expr = tmpl.params[pi].default_expr` (`:849`, `:1027`) — aliasa o `Expr` cru.
- `consteval.tks:432,748` — `TConstDecl` reconstruído com `doc = cd.doc; guard = cd.guard`
  (passthrough por referência).
- Toda reconstrução de `TExpr` em mono e consteval faz `line = e.line; col = e.col`
  (dezenas de sítios, `monomorph.tks:469-674`, `consteval.tks:301-654`) — **duplicando as
  posições em cada fase**, o custo que o `.tsym` elimina.

Como o alocador é ARENA em `root` e **`root` nunca é liberado até a saída do processo**
(`ast-computed-arena-assessment-0.3.1.md` §1.2: reclaim 0,0 %), as árvores de TODAS as
fases coexistem: `pre.prog` + `pre.table` + `checked` + `expanded` + `inlined`, cada uma
aliando a anterior. O pico em codegen é essa SOMA. É o "segurar tudo" que
`reducao-memoria-arrays-0.3.1.md` §6bis nomeia como o residual não-push.

### 1.3 O sharing por referência é o cadeado (risco R2 / UAF)

O diagnóstico do agente anterior está correto e agora tem endereço: `params = f.params`,
`doc = gf.doc`, `TProgram.items` embutindo `parser::TypeDecl`. **Dropar a região de uma fase
anterior com o alias intacto = use-after-free**, exatamente o modo de falha que derrubou a
passagem `arena-por-escopo` (clone em massa + drop = UAF). A poda ingênua ("solta o cru")
corrompe o vizinho. A poda SEGURA precisa primeiro QUEBRAR o alias.

**É esse o insight central deste documento: "comprimir a AST" não é remover campos — é
QUEBRAR O ALIAS com uma cópia-projeção total, para então poder derrubar a região da fase
anterior INTEIRA (não só a árvore: também o scaffolding do checker — `Env`, escopos, tabelas
de resolução — que domina o baseline de 3140 MB).** A poda de campos (§3) é um refinamento
secundário sobre a cópia; o golpe grande é o DROP DE REGIÃO habilitado pela cópia. A
externalização das posições para `.tsym` (§2) é a segunda alavanca: torna a árvore SLIM em
si (sem `line`/`col` duplicados por nó em cada fase).

---

## 2. As posições vão para uma tabela lateral `.tsym` (estilo `.pdb`) — não são jogadas fora

**Ruling do dono (v2).** A v1 concluiu corretamente que `line`/`col` são EMITIDOS no `teko.c`
(`_tk_cast_loc_line = <e.line>`, `codegen.tks:2411-2440,:3465,:3574-3607`, diagnóstico de
overflow de cast em runtime) e usados na cobertura (`emit_cov_branch`, `:4264,:5723`), logo
não dá pra dropá-los ingenuamente. **A correção não é dropar — é MOVER** as posições para uma
tabela lateral, deixando na AST só um id compacto. Exatamente como C# faz com `.pdb`: o
binário carrega tokens/ids, e o `.pdb` resolve token → arquivo:linha quando um stack trace é
pedido.

### 2.1 O que existe hoje

`.tsym` **já existe como stub**: `codegen::tk_emit_tsym(prog)` (`codegen.tks:9685`) retorna
apenas `"# teko symbol map (.tsym v1): disabled (debug-only, native backend)\n"`, e o
empacotador já grava uma entrada `.tsym` no `.tkl` (`project.tks:1146-1157`). **O gancho de
artefato está pronto; falta o conteúdo real e o id de nó na AST.** Este ruling dá propósito
ao stub.

### 2.2 O id de nó compacto

Adicionar à AST um id de nó estável e compacto no lugar do par `line`/`col`:

- Em `parser::Expr`/`checker::TExpr` (e nos poucos nós com `line`/`col`: `MultiBind`,
  `TFunction`, `TConstDecl`, decls), trocar `line: u32; col: u32` por `nid: u32` (um id de
  nó — 4 bytes no lugar de 8).
- O `nid` é atribuído no PARSE, em ordem determinística de pré-visita por arquivo (um
  contador monotônico por unidade de compilação, ou `(file_ord << k) | seq`). É o token do
  `.pdb`: estável, denso, independente de endereço.
- **Nós SINTÉTICOS** criados por mono/consteval/lower (que não têm posição de fonte) recebem
  `nid = 0` (sentinela "sem posição") OU herdam o `nid` do nó de origem que os gerou — hoje
  eles herdam `line/col` do nó-fonte (`line = e.line`), então herdar o `nid` preserva o
  comportamento exato. É a MESMA propagação que já existe, agora com 4 bytes em vez de 8.

### 2.3 A tabela `.tsym` (schema)

Uma tabela lateral, gravada uma vez, indexada por `nid`:

```
.tsym v2 (por unidade de compilação):
  header:   magic, versão, contagem
  strings:  tabela de paths internados (dedup)  -- path aparece uma vez
  entradas: por nid crescente:
              nid: u32
              path_idx: u32   -- índice na tabela de paths
              line: u32
              col:  u32
              (extensível: byte_offset/byte_len p/ o texto do span, se o diagnóstico quiser
               grifar a fonte — hoje `DocSpan` já carrega isso; unificar aqui)
```

Formato determinístico (R6): entradas em ordem CRESCENTE de `nid` (a ordem de pré-visita do
parse), paths internados em ordem de primeira-visita, sem timestamp, sem endereço. Mesma
entrada → mesmo `.tsym` byte-a-byte. Pode ser um arquivo binário simples ou, se o dono
preferir consulta rica, uma base SQLite (schema equivalente: tabela `nodes(nid PK, path,
line, col)`); o schema lógico é o mesmo — a escolha do container (blob binário vs SQLite) é
secundária e não afeta o desenho de memória.

### 2.4 Como o codegen resolve id → posição no emit (duas opções; recomendo a byte-preservante)

Hoje o codegen faz `cb_u64_digits(buf, e.line)`. Sob `.tsym`, `e` carrega `nid`, não `line`.
Duas rotas:

**Opção A — resolver no EMIT, `teko.c` byte-idêntico (recomendada como primeiro passo):**
o codegen mantém a tabela de posições viva em memória (um array indexado por `nid`, o mesmo
que vira `.tsym`) e, ao emitir, faz `pos = tsym_lookup(nid); cb_u64_digits(buf, pos.line)`.
O `teko.c` sai **idêntico** ao de hoje → `gen2==gen3` sem reseed. O ganho de memória vem só
da AST slim (id em vez de par), não de mudar a saída. A tabela de posições é UMA cópia
compacta (não duplicada por fase), viva só até o fim do codegen; pode inclusive ser gravada
em `.tsym` e relida sob demanda em vez de residir.

**Opção B — emitir o `nid`, resolver em RUNTIME via `.tsym` (o `.pdb` verdadeiro; passo
seguinte, sob reseed):** o codegen emite `_tk_cast_loc_nid = <nid>` (menor que line+col), e o
runtime, ao falhar um cast/assert, abre o `.tsym` do binário e resolve `nid → path:line` para
a mensagem. Isso ENCOLHE o `teko.c` (menos dígitos por sítio) e remove as posições literais
do binário — mas MUDA o `teko.c` deliberadamente → **reseed + fixpoint re-valida**
(`gen2==gen3` no novo formato). Opção B é o modelo `.pdb` pleno; entra depois de A, quando o
ganho de tamanho do binário justificar o reseed.

### 2.5 O ganho de memória (quantificar; alvo do ruling)

O ganho tem DUAS fontes, ambas na AST residente:

1. **Encolher cada nó:** `line: u32; col: u32` (8 B) → `nid: u32` (4 B) = **-4 B por nó**.
2. **Eliminar a DUPLICAÇÃO por fase:** hoje as posições são copiadas em cada reconstrução
   (checker → mono → comptime → consteval = ~4 cópias vivas simultâneas de 8 B/nó). Com
   `.tsym`, cada fase copia só 4 B/nó, e a TABELA de posições existe UMA vez (não por fase).
   Efeito combinado ≈ **de (8 B × N × ~4) para (4 B × N × 4) + 1 tabela** — corta perto da
   METADE do custo de trivia residente e remove a tabela do caminho de duplicação.

**Magnitude (GUESS honesto, a instrumentar no PJ-1):** o compilador tem da ordem de milhões
de nós de expressão (o censo al1 fala em 20,3 M crescimentos de push, com `inline_rw_block`/
`type_block`/`mono_block` entre os densos). A um id por nó e ~4 cópias vivas, a fatia de
`line`/`col` residente é da ordem de **centenas de MB** dentro dos 3140 — o dono a nomeia como
"parte grande". Não afirmo um número fechado: **PJ-1 mede** (marca de região antes/depois de
projetar a trivia). O que é certo é a DIREÇÃO: é uma fatia bounded, residente, e duplicada —
exatamente o tipo de custo que externalizar remove.

### 2.6 Onde `.tsym` é gerado/lido; determinismo (R6)

- **Gerado:** no parse, o `nid` é atribuído; a tabela `nid → (path,line,col)` é acumulada por
  unidade e materializada por `codegen::tk_emit_tsym` (hoje stub) no fim do build. No C-route
  ela pode ser gravada como artefato lateral OU mantida em memória só até o codegen (opção A).
- **Lido:** (opção A) pelo codegen no emit; (opção B) pelo runtime no erro; e por qualquer
  ferramenta de diagnóstico/LSP que hoje lê `line`/`col`.
- **Determinismo:** `nid` em ordem de pré-visita de parse (estável), tabela ordenada por
  `nid`, paths internados por primeira-visita. Mesma entrada → mesmo `.tsym` → (opção A) mesmo
  `teko.c`. Sem `map`/`hashset` no caminho, sem endereço, sem timestamp.

**Interação com a jardinagem:** o `.tsym` torna a árvore SLIM (menos bytes por nó), e a
jardinagem (§3-§4) DERRUBA as fases anteriores. São ortogonais e SOMAM: `.tsym` reduz o
tamanho de cada cópia; a jardinagem reduz o NÚMERO de cópias vivas. O `.tsym` também
SIMPLIFICA a jardinagem: com posições fora da árvore, o deep-copy de projeção copia só o
`nid` (4 B), não precisa reconciliar `line`/`col`.

---

## 3. O mapa por fronteira — o que cada fase LÊ / o que é DROPÁVEL

Rastreado no código. "Dropável" = nenhuma fase a jusante lê o campo.

### Fronteira parse → check
O checker lê TUDO do cru. **Nada dropável por campo aqui.** O valor: depois do checker, o
scaffolding do checker (`Env`, escopos, caches de resolução) e as árvores de parse tornam-se
lixo — reclamáveis por DROP DE REGIÃO (§4).

### Fronteira check → mono
Mono lê `prog` + `table: TypeTable` (`monomorph.tks:946`). Mono NÃO lê `doc`/`has_doc`
(só copia, `:1035`). **Dropável ao entrar em mono:** `doc`/`has_doc`/`DocSpan` de toda decl.

### Fronteira mono → comptime → consteval
Após consteval, `table: TypeTable` está MORTA — NÃO é passada ao codegen (o
`backend`/`codegen_and_report` recebe só `prog`, `project.tks:2388`). **Dropável após
consteval:** a `TypeTable` inteira (`regs`, `by_last`, `comptimes`).

### Fronteira consteval → codegen
- **Posições (`nid` / ex-`line`/`col`): EXTERNALIZADAS para `.tsym`** (§2) — não vivem mais
  na árvore como par de 8 B; a árvore carrega só o `nid` de 4 B, e a tabela é única.
- **`DocSpan`/`doc`/`has_doc`: dropáveis** — nenhum sítio de codegen/mono/consteval/backend/lir
  os lê (grep vazio fora de checker/collect/tkh); só `emit/tkh.tks` e o LSP, fora do build
  seco do `teko.c`.
- **Codegen LÊ o cru `parser::TypeDecl` intensivamente** (`cg_find_decl`, corpos de
  struct/class/interface/trait/enum, `f.params`; `codegen.tks:554-856`) → `parser::TypeDecl`,
  `parser::UseDecl`, `parser::Param` **sobrevivem até o codegen** (não dropáveis).

**Resumo do mapa:**

| Campo | Lido por | Tratamento |
|---|---|---|
| `line`/`col` (par por nó) | codegen (emite no `.c`) + cobertura | **externalizar p/ `.tsym`; AST carrega `nid`** (§2) |
| `doc: DocSpan` + `has_doc` | checker diag, `.tkh`, LSP | **podar** na entrada de mono |
| `TypeTable` (table) | mono, comptime | **dropar** após consteval (não vai a codegen) |
| `type_constraints` | mono | podar pós-mono (verificar codegen — R-B) |
| `parser::TypeDecl`/`UseDecl`/`Param` | codegen | preservar (não dropável) |

Duas alavancas: (i) `.tsym` encolhe a trivia de posição residente; (ii) o drop de região
(§4) reclama o scaffolding morto — o golpe grande.

---

## 4. O que "poda / jardinagem" realmente é, mecanicamente

Operações compostas, por fronteira de fase:

1. **Projeção (quebra de alias):** deep-copy TOTAL da saída da fase N para uma região que
   SOBREVIVE, incluindo copiar/internar strings (R2, §6), sem apontar para NADA da região N.
2. **Poda (compressão):** durante a cópia, OMITIR os campos que a fase N+1 não lê (`DocSpan`,
   `doc`, `has_doc`; e, pós-consteval, a `TypeTable`). As posições já saíram para `.tsym`.
3. **Despejo (drop):** derrubar a região INTEIRA da fase N (`region_drop_subtree`), agora sem
   ponteiros vivos apontando para dentro dela.

O pico deixa de ser a SOMA das fases e passa a ser o MÁXIMO de (uma fase viva + a árvore slim
projetada). É o §6bis "despejo entre estágios" de `reducao-memoria-arrays-0.3.1.md`, na
variante WHOLE-PROGRAM e EM MEMÓRIA.

---

## 5. Mecanismo — EM MEMÓRIA para o C-route; `.tkb` é só da perna native

### 5.1 C-route: despejo EM MEMÓRIA (projeção deep-copy + drop de região) — NUNCA `.tkb`

**Ruling do dono (v2):** a rota de binário/C NÃO passa por `.tkb`. O despejo entre fases é
o bracket de jardinagem em memória:

```
R_phase   = region_new(surviving_parent)     // região própria da fase N
region_enter(R_phase)
  out_raw = <roda a fase N; TODA alocação cai em R_phase>
region_leave()
out_slim  = project_*(surviving_parent, out_raw)   // deep-copy total p/ o pai, poda doc
region_drop_subtree(R_phase)                        // reclama TODO o working set da fase N
// out_slim vive em surviving_parent, livre de alias
```

`project_*` aloca no `surviving_parent`, interna cada string, reconstrói cada nó (copiando só
o `nid`, não `line`/`col`). Depois do drop, o scaffolding da fase some; `out_slim`, alias-free,
permanece. **Custo:** pico transitório de 2× a ÁRVORE durante a cópia, pago para reclamar tudo
o que R_phase tinha além da árvore (o scaffolding — a maior parte de 3140 MB). Sem IO, sem
serializer, sem O(n²).

**Determinismo (R6):** walk em ordem fixa de campo, sem `map`/`hashset`, sem ponteiro, sem
timestamp.

### 5.2 INVESTIGAÇÃO: o C-route de hoje passa por `.tkb` sem necessidade?

O dono suspeita disso. **Rastreio no código — resultado: o build de BINÁRIO NÃO passa por
`.tkb`; o custo só aparece em fluxos de pacote/dependência.**

- `emit::serialize_program(prog)` é chamado em **UM único sítio**: `project.tks:1145`, dentro
  de `Artifact::Package` (empacotar um `.tkl`). O build de `Artifact::Binary` (incluindo o
  self-build do compilador) **não o chama**.
- `emit::deserialize_program(...)` é chamado em **UM único sítio**: `project.tks:152`, dentro
  de `load_dep_program` — ao consumir um `.tkl` de DEPENDÊNCIA. O runtime do próprio compilador
  é INJETADO COMO FONTE (`rt_inject_namespaces`/`rt_prelude_paths_for`, `project.tks:264-280`),
  não como `.tkl` — então o self-build **não deserializa nada**.

**Veredito:** no build seco (binário/self-build) do C-route, `.tkb` NÃO está no caminho — não
há custo de `.tkb` a remover ali. A suspeita do dono se confirma como PRINCÍPIO (o C-route não
deve tocar `.tkb`) e o código já o respeita nesse caminho. **Dois custos residuais a registrar
(fora do build seco, mas reais para usuários):**

1. **`serialize_program` é O(n²)** (`write_u8 = [..buf, x]` por byte, `emit/tkb_buf.tks:4`) e
   roda em todo build de PACOTE — sobe o pico de quem empacota. Conserto = Eixo A no `emit/`
   (duas-passadas). Custo a remover, mas não do build seco.
2. **`deserialize_program` puxa o programa de dep INTEIRO** (com corpos) para a memória em
   `load_dep_program` — para projetos com deps `.tkl`. O `.tkh` (só assinaturas `exp`) deveria
   bastar ao checker de quem consome; puxar o `.tkb` completo do dep é candidato a poda futura
   (usar `.tkh` no consumo, reservar `.tkb` completo à perna native). Registrado; fora do build
   seco do compilador.

**Conclusão:** o desenho de jardinagem do C-route é 100% em memória. `.tkb` fica reservado à
perna native (§5.3). Nenhuma fronteira do C-route usa `.tkb`.

### 5.3 A perna NATIVE: `.tkb` é o OBJETO PORTÁTIL pré-monomorph (capítulo breve)

**Ruling do dono (v2):** a rota de binário/C só emite o header de símbolos exportados (`exp`,
o `.tkh`); ela não usa `.tkb`. O `.tkb` é o formato do **objeto portátil da perna native**:

```
fonte → check → .tkb (objeto PORTÁTIL, PRÉ-monomorph, target-independente)
         → [escolhe target] → monomorph → isel/regalloc/encode → objeto final (.o) → ld
```

O `.tkb` é o typed-AST serializado ANTES da monomorfização — portátil porque a monomorfização
(e a seleção de instrução) dependem do target; guardar pré-mono deixa um artefato reusável que
depois é especializado por target. É o análogo do "objeto portátil" (bitcode-like) da
linguagem: distribui-se o `.tkb`, e cada target o baixa → mono → `.o`.

**Lacuna a registrar (não deste crumb):** o `.tkb` de HOJE (`project.tks:1145`) serializa o
`prog` já POST-mono/consteval (o que sai de `checked_program_of`), não o pré-mono. Para servir
ao ruling native, a serialização precisa capturar o typed **pré-mono** (a saída do checker,
antes de `monomorphize`). Isso é trabalho da perna native (alinhado a
`reducao-memoria-arrays-0.3.1.md` C15/RM-C13), NÃO da jardinagem do C-route. Também exige o
serializer O(n) (§5.2 custo 1). Registrado como dependência da perna native, fora do escopo
deste doc.

### 5.4 Por que NÃO usar `.tkb` para o despejo em memória (fechamento)

Mesmo ignorando o ruling, `.tkb` seria o mecanismo errado para o despejo entre fases residente:
(a) é O(n²) hoje; (b) três coisas coexistiriam no pico — árvore-fonte + buffer `[]byte` +
árvore-resultado; (c) só ganharia no trough se o buffer fosse a DISCO, o que é IO puro contra a
projeção em memória. A projeção deep-copy (§5.1) é mais leve, determinística e drop-safe pelo
drop de região.

---

## 6. Drop-safety (R2) — prova de que nada aliasa a memória dropada

**Obrigação:** após `project_*(parent, out_raw)`, NENHUM ponteiro em `out_slim` endereça
`R_phase`. Só então `region_drop_subtree(R_phase)` é seguro.

**Aliases a quebrar:**

| Alias | Sítio | Como a projeção quebra |
|---|---|---|
| `TProgram.items` embute `parser::TypeDecl`/`UseDecl` | `tast.tks:97` | deep-copy do `TypeDecl`/`UseDecl` inteiro |
| `TFunction.params = f.params` (Param cru) | `monomorph.tks` passthrough | copiar o array `Param`, incl. `default_expr` recursivo |
| `doc = gf.doc` (`DocSpan`) | `monomorph.tks:1035`, `consteval.tks:432` | PODAR (vira `docspan_none()`) |
| `guard = gf.guard` (`Pred`) | idem | deep-copy da árvore `Pred` |
| posições | ex-`line`/`col` | já FORA da árvore (`.tsym`); copia-se só o `nid` (4 B) |
| strings (`str` = `{ptr,len}` → backing) | onipresente | **internar** cada string em `parent` (copiar o backing) |

**O ponto sutil e LOAD-BEARING (a armadilha do R2):** em Teko `str` é `{ptr,len}` para um
backing. Copiar um `str` copia o CABEÇALHO mas ALIASA o backing. Se os backings vivem em
`R_phase`, o `out_slim` "copiado" ainda aponta para `R_phase` → o drop libera o backing sob
cabeçalhos vivos = UAF. **O deep-copy TEM de internar/copiar os backings para `parent`.** Um
deep-copy que esquece isso passa superficial e corrompe — a exata classe do R2. (O `.tkb` faz
esse intern de graça via `StrTable`; a projeção em memória replica a disciplina à mão.)

**Postura conservadora:** projeção TOTAL por padrão; a poda só remove o que provadamente
NENHUMA fase a jusante lê. Dúvida → COPIA (leak-safe, nunca UAF). Mesma postura de `escape.tks`
("dúvida → escapa").

---

## 7. Preservação do fixpoint (R6) — o `teko.c` byte-idêntico

- **`.tsym` opção A (recomendada):** codegen resolve `nid → line` no emit e emite o MESMO
  `_tk_cast_loc_line = <line>` → `teko.c` byte-idêntico, sem reseed. **`.tsym` opção B** muda
  o `teko.c` deliberadamente (emite `nid`) → reseed + fixpoint re-valida no novo formato.
- **Poda de `doc` invisível ao codegen:** codegen nunca lê `DocSpan` → remover `doc` não muda
  um byte do `.c`.
- **Determinismo do walk e do `.tsym`:** ordem fixa de campo / ordem de `nid`; sem
  `map`/`hashset`, sem endereço, sem timestamp.
- **Independência do gensym / RM-C10:** a jardinagem é WHOLE-PROGRAM — emissão num único
  buffer global; os nomes temporários derivados de `buf.len` NÃO mudam. A jardinagem **não
  depende de RM-C10** (ao contrário do Eixo C por unidade).

**Guarda por crumb:** cada crumb que insere drop/troca posições valida `gen2==gen3`
byte-idêntico (opção A) E a identidade contra o build pré-jardinagem. Divergência → PARAR e
achar o alias/campo errado; nunca maquiar.

---

## 8. Relação com o desenho existente (Eixo C, RM-C10, DPS)

**Jardinagem = §6bis "despejo entre estágios" na variante whole-program, em memória:**

| | Jardinagem (este doc) | Eixo C (RM-C11/12/13) |
|---|---|---|
| Unidade de despejo | fase inteira (whole-program) | namespace (por unidade) |
| Mecanismo | projeção deep-copy + drop de região (memória) | AST-incompleta + LINK + fusão por unidade |
| Reduz o pico a | máx(uma FASE + slim) | máx(uma UNIDADE) |
| Custo de construção | BAIXO | ALTO |
| Depende de RM-C10 | **NÃO** | SIM |
| Usa `.tkb` | **NÃO** (memória) | disco na barreira (RM-C13) / native |

**Complementa e ANTECIPA.** A jardinagem entrega "pico = máx de uma fase" sem a reestruturação
do Eixo C — é o "adiantar o que der". Compartilham a fundação C6 (arena-por-escopo): a
jardinagem é C6 na FRONTEIRA DE FASE. `.tsym` é ortogonal e soma às duas (encolhe cada cópia).
DPS (`ast-computed-arena-assessment`) ataca o eixo de RETORNO — ortogonal; soma. `.tkb`
pré-mono é o objeto portátil da perna native (§5.3), não interage com a jardinagem do C-route.

---

## 9. Contratos de tipo / assinaturas (Teko, full-Javadoc — copiar verbatim)

Módulos sugeridos: `src/checker/gardening.tks` (walk de projeção) + brackets em
`src/build/project.tks`; `.tsym` em `src/codegen` (dar corpo ao `tk_emit_tsym`) + o campo
`nid` na AST.

```teko
/**
 * project_program — a PODA da jardinagem: deep-copy TOTAL de um TProgram para a região
 * corrente (de sobrevivência), quebrando todo alias com a região da fase que o produziu e
 * OMITINDO os campos que nenhuma fase a jusante lê (doc/has_doc/DocSpan). As posições já
 * vivem fora da árvore (.tsym), então só o `nid` de cada nó é copiado.
 *
 * Invariante de drop-safety (R2), load-bearing: ao retornar, NENHUM ponteiro na árvore
 * resultante endereça a região de origem — inclusive os BACKINGS de string, que são
 * internados na região corrente (um str copiado sem copiar o backing ainda aliasa a origem
 * e causaria UAF no drop). Ver docs/design/projecao-ast-entre-fases-0.3.1.md §6.
 *
 * Determinismo (R6): o walk visita campos em ordem fixa, sem iterar map/hashset, sem ler
 * endereço/timestamp — mesma entrada produz a MESMA árvore e o mesmo teko.c.
 *
 * @param prog  o programa tipado a projetar (vive na região da fase de origem)
 * @return      um TProgram equivalente, alias-free na região corrente, com doc podado
 * @since 0.3.1
 */
fn project_program(prog: checker::TProgram): checker::TProgram

/**
 * intern_str_into — interna uma string na região corrente, copiando o backing, de modo que
 * o resultado não aliase a região de origem. O primitivo de segurança de string do
 * project_* (§6): sem ele o deep-copy é incompleto e o drop vira UAF.
 *
 * @param tab  a tabela de intern acumulada nesta projeção (dedup por conteúdo)
 * @param s    a string a internar (seu backing pode viver na região de origem)
 * @return     a tabela atualizada e a string cujo backing vive na região corrente
 * @since 0.3.1
 */
fn intern_str_into(tab: InternTable, s: str): InternResult

/**
 * garden_phase — o BRACKET de jardinagem: roda a fase `run` numa região-filha própria,
 * projeta sua saída para o pai (de sobrevivência) e derruba a região-filha inteira,
 * reclamando todo o scaffolding da fase (Env, escopos, tabelas) que a projeção não copiou.
 * É o C6 (arena-por-escopo) aplicado na FRONTEIRA DE FASE; o drop só é seguro porque
 * project_program quebrou todo alias (§6).
 *
 * @param parent  a região de sobrevivência onde a saída projetada passa a viver
 * @param run     a fase a executar (produz um TProgram na região-filha)
 * @return        a saída da fase, projetada e alias-free em `parent`
 * @since 0.3.1
 */
fn garden_phase(parent: ptr, run: fn(): checker::TProgram | error): checker::TProgram | error

/**
 * tsym_record — registra a posição de fonte de um nó na tabela `.tsym`, indexada pelo id de
 * nó (`nid`) atribuído no parse. A posição sai da AST residente (que passa a carregar só o
 * `nid`) e vive uma única vez nesta tabela lateral, estilo `.pdb` — resolvida por lookup
 * quando um diagnóstico (compile ou runtime) precisa de arquivo:linha.
 *
 * Determinismo (R6): entradas em ordem crescente de `nid` (ordem de pré-visita do parse),
 * paths internados por primeira-visita — mesma entrada produz o mesmo `.tsym` byte-a-byte.
 *
 * @param t     a tabela `.tsym` acumulada
 * @param nid   o id de nó (token estável do parse)
 * @param path  o arquivo-fonte
 * @param line  a linha (1-based)
 * @param col   a coluna (1-based)
 * @return      a tabela atualizada
 * @since 0.3.1
 */
fn tsym_record(t: TsymTable, nid: u32, path: str, line: u32, col: u32): TsymTable

/**
 * tsym_lookup — resolve um id de nó para sua posição de fonte, o inverso de tsym_record. É o
 * caminho que o codegen usa (opção A) ao emitir `_tk_cast_loc_line`/cobertura, mantendo o
 * teko.c byte-idêntico, e que o runtime/LSP usam para diagnósticos.
 *
 * @param t    a tabela `.tsym`
 * @param nid  o id de nó a resolver
 * @return     a posição (path, line, col), ou uma posição vazia se `nid == 0` (sintético)
 * @since 0.3.1
 */
fn tsym_lookup(t: TsymTable, nid: u32): TsymPos
```

**Funções/tipos existentes tocados:**

- `parser::Expr`/`checker::TExpr` (e nós com posição): `line: u32; col: u32` → `nid: u32`.
  Toda reconstrução que hoje faz `line = e.line; col = e.col` (`monomorph.tks:469+`,
  `consteval.tks:301+`, `codegen.tks:8509+`) passa a `nid = e.nid`.
- `codegen::tk_emit_tsym` (`codegen.tks:9685`, hoje stub) — dar corpo: materializar a
  `TsymTable` no formato §2.3.
- Sítios de emit de posição (`codegen.tks:2411-2440,:3465,:3574-3607`, `emit_cov_branch`):
  `cb_u64_digits(buf, e.line)` → `cb_u64_digits(buf, tsym_lookup(t, e.nid).line)` (opção A).
- `src/build/project.tks:204` `checked_program_of` — inserir os brackets `garden_phase`.
- Reuso: `src/runtime/arena.tks` `region_new`/`enter`/`leave`/`drop_subtree` (prontos).

---

## 10. Fixtures de regressão (inputs → códigos de saída nativos)

| fixture | asserta | exit |
|---|---|---|
| `gardening_teko_c_identical` | build COM jardinagem+`.tsym` (opção A) emite `teko.c` byte-idêntico ao build SEM | 0 |
| `tsym_cast_loc_resolves` | um cast que estreita inteiro emite `_tk_cast_loc_line`/`col` corretos via `tsym_lookup(nid)` | 0 |
| `tsym_roundtrip_deterministic` | mesmo fonte → `.tsym` byte-idêntico em dois builds (ordem de `nid`, paths internados) | 0 |
| `tsym_synthetic_node_no_pos` | um nó sintético (nid=0) de mono/consteval resolve para posição vazia sem crash | 0 |
| `gardening_docspan_pruned` | após a projeção, o `.doc` de toda decl é `docspan_none()` | 0 / (inversão falha) |
| `gardening_string_backing_copied` | inversão R2: projeção que copia header de str mas NÃO o backing + drop DEVE dar SIGSEGV/UAF | (inversão falha) |
| `gardening_typetable_dropped` | a `TypeTable` é liberada após consteval; build verde | 0 |
| `gardening_generic_instance_survives` | fn genérica instanciada, cujo genérico vivia na região dropada, emite corpo correto | 0 |
| `gardening_pred_guard_survives` | decl sob `#os(...)` (guard `Pred`) projetada da região dropada filtra por plataforma | 0 |
| `gardening_fixpoint` | `gen2==gen3` byte-idêntico com jardinagem+`.tsym`(A) ligados | 0 |

Fixtures de `.tkb` NÃO entram aqui — pertencem à perna native (RM-C13/C15). A jardinagem do
C-route é memória-pura.

---

## 11. Sequência de crumbs (do maior/mais-seguro ganho primeiro)

Fundação provada-idêntica ANTES de qualquer drop; depois drops do maior working-set ao menor;
`.tsym` intercalado (byte-preservante primeiro). Fixpoint `gen2==gen3` a cada harvest; guard
6,5 GiB inviolável.

### PJ-1 — Instrumentar o split retenção-por-fase + a fatia de posições (baseline)
Estender `report_phase_rss` para reportar, por fase, ÁRVORE viva vs scaffolding reclamável, e
medir a fatia residente de `line`/`col` (o alvo do `.tsym`). Baseline registrada. Sem mudança
de comportamento. Gate: build + `teko test .`. Ritual: NÃO.

### PJ-2 — `.tsym` opção A: `nid` na AST + tabela lateral, `teko.c` byte-idêntico
Trocar `line`/`col` por `nid` nos nós; atribuir `nid` no parse (ordem de pré-visita); dar
corpo a `tk_emit_tsym`; codegen resolve `nid → line` no emit. **Byte-preservante por
requisito:** `teko.c` idêntico, validado byte-a-byte. Ganho: AST slim (trivia de posição sai
da árvore, sem duplicação por fase). Gate: `[fixpoint]` + `tsym_cast_loc_resolves` +
`tsym_roundtrip_deterministic`. Ritual: SIM.

### PJ-3 — `project_program` + `intern_str_into` como transformação-IDENTIDADE (aditivo)
Walk de deep-copy total (com intern de string obrigatório, §6), podando `doc`/`has_doc`.
Inserir como passe no-op-ish (projeta, mantém origem viva, NÃO dropa ainda). Prova de cópia
TOTAL e fiel ANTES de qualquer drop — a rede de segurança do R2. Gate: `[fixpoint]`
byte-idêntico + `gardening_string_backing_copied`. Ritual: SIM.

### PJ-4 — Drop pós-consteval: derrubar a `TypeTable` + a região do checker
Rodar checker→mono→comptime→consteval numa região-filha; `garden_phase` projeta o `inlined`
p/ o pai; `region_drop_subtree` reclama a filha (o scaffolding que domina 3140 MB). **Maior
queda isolada.** Gate: `[fixpoint]` + `gardening_typetable_dropped` + RSS pós-drop medido.
Ritual: SIM.

### PJ-5 — Drop na fronteira mono: derrubar a árvore pré-mono + parse
Subdividir: checker numa região; projetar `PreMono{prog,table}`; derrubar parse/checker-
scaffolding ANTES de mono. Reclama o pico entre checker (3140) e mono (3407). Gate:
`[fixpoint]` + `gardening_generic_instance_survives` + `gardening_pred_guard_survives`.
Ritual: SIM.

### PJ-6 — Poda de campo confirmada (`type_constraints`) — medir antes
Se codegen comprovadamente não ler `type_constraints`, podá-las pós-mono. Só campos com poda
provada NENHUM-leitor-a-jusante. Gate: `[fixpoint]` + `gardening_docspan_pruned`. Ritual: SIM.

### PJ-7 — (opcional, sob reseed) `.tsym` opção B: emitir `nid`, resolver em runtime
Emitir `_tk_cast_loc_nid = <nid>` e resolver `nid → path:line` em runtime via `.tsym`
embarcado (o `.pdb` pleno). Encolhe o `teko.c` e remove posições literais do binário. MUDA o
`teko.c` deliberadamente → **reseed** + fixpoint re-valida no novo formato. Só depois de PJ-2
verde e se o ganho de tamanho justificar o reseed. Ritual: SIM.

**Fora de escopo (perna native, registrar hand-off):** `.tkb` pré-mono como objeto portátil
(§5.3) e o conserto O(n) do `serialize_program` (§5.2) — trabalho de RM-C13/C15, não da
jardinagem do C-route.

**Pontos de ritual:** PJ-2, PJ-3, PJ-4, PJ-5, PJ-6, PJ-7 — cada um muda a topologia de memória
ou a saída; gate nativo completo + `gen2==gen3` byte-idêntico.

---

## 12. Riscos e tensões de lei

- **R2 (drop-safety / UAF) — dominante.** Deep-copy incompleto (esp. backing de string, §6)
  corrompe. RESOLUÇÃO: projeção TOTAL, intern obrigatório no contrato, PJ-3 prova identidade
  ANTES de drops, fixture-inversão `gardening_string_backing_copied`. Dúvida → copia.
- **R6 (fixpoint) — RESOLVIDO por construção.** `.tsym` opção A preserva o `.c`; `doc` podado
  é invisível; walk e `.tsym` determinísticos; jardinagem whole-program não mexe no gensym
  (independe de RM-C10). Opção B é mudança deliberada sob reseed.
- **R-A (custo transitório 2×).** A projeção paga 2× a árvore. O ganho (reclamar scaffolding)
  domina; PJ-1 mede antes. Se um alvo não couber, aquela fronteira migra para o `.tkb`-DISCO
  do Eixo C — princípio compartilhado, fallback contínuo.
- **R-B (poda de `type_constraints`).** Confirmar que codegen não as lê antes de podar. Dúvida
  → não poda (leak-safe).
- **R-C (`nid` para nós sintéticos).** Nós criados por mono/consteval não têm posição de fonte.
  RESOLUÇÃO: herdar o `nid` do nó-origem (preserva o `line = e.line` de hoje) ou `nid = 0`
  (posição vazia). Fixture `tsym_synthetic_node_no_pos`.
- **Teko-only:** tudo em `.tks` (`gardening.tks` + `.tsym` no codegen + brackets no driver);
  reusa `arena.tks`. Nenhum `teko_rt.c` novo. Sem tensão.
- **`.tkb` O(n²) / dep-pull:** custos de fluxos de pacote/dependência (§5.2), NÃO do build seco;
  consertos são Eixo A / perna native. Registrados, não bloqueiam a jardinagem.

**Nenhuma tensão de Lei genuína — nada HALTa.** A única decisão do dono é de PRIORIDADE (§13).

---

## 13. RECOMENDAÇÃO ao dono (v2)

**Sim — dá para "comprimir" a AST entre fases. Em Teko, com os dois rulings, a forma certa é:**

1. **Posições → `.tsym` (estilo `.pdb`), não jogadas fora.** A AST residente carrega só um
   `nid` de 4 B por nó; a tabela `nid → path:line` vive UMA vez, lateral. O codegen resolve
   `nid → line` no emit (opção A) → `teko.c` byte-idêntico, sem reseed, e a fatia de posições
   (duplicada hoje em ~4 fases) some da árvore residente. Depois, opcionalmente, emitir o `nid`
   e resolver em runtime (opção B, `.pdb` pleno, sob reseed).

2. **Despejo entre fases → EM MEMÓRIA (jardinagem), nunca `.tkb` no C-route.** Cópia-projeção
   total que quebra o alias (internando strings, R2) e derruba a região INTEIRA da fase
   anterior — reclamando o scaffolding do checker que domina os 3140 MB. Investigação
   confirmada: o build de binário/self-build **já não passa por `.tkb`** (serialize é
   Package-only, deserialize é dep-only); `.tkb` fica reservado à perna native (objeto portátil
   pré-monomorph). Custos de `.tkb` (O(n²) no empacotar; dep-pull completo) são de fluxos de
   pacote, não do build seco — registrados para Eixo A / native.

**Trade-offs:**

| Caminho | Ganho | Custo/risco | Quando |
|---|---|---|---|
| **`.tsym` opção A** | AST slim (posições fora, sem duplicação por fase); byte-idêntico | R-C (nid sintético) | **AGORA (PJ-2), byte-preservante** |
| **Jardinagem em memória** | pico = máx(uma fase + slim); reclama scaffolding; independe de RM-C10 | 2× transitório; R2 (intern de string) | **AGORA (PJ-3..PJ-5)** |
| **`.tsym` opção B (runtime)** | `teko.c`/binário menores; `.pdb` pleno | reseed + fixpoint | depois, se o tamanho justificar |
| **Eixo C por unidade (`.tkb` disco/native)** | pico = máx(uma unidade); objeto portátil | grande; exige RM-C10 + serializer O(n) | se o residual pós-jardinagem exceder |
| **DPS de retorno** | reclama o eixo de RETORNO + correção nativa | mudança de ABI | ortogonal; sua trilha |

**Prioridade recomendada (decisão do dono, sem tensão de Lei):** construir AGORA, em ordem,
**PJ-1 → PJ-2 (`.tsym` A) → PJ-3 (projeção-identidade) → PJ-4 (drop pós-consteval) → PJ-5
(drop na fronteira mono)**, independente e antes do Eixo C, porque (a) `.tsym` A + jardinagem
entregam o teto "pico = máx de uma fase" e a árvore slim com um único walk de cópia + brackets
de região que já existem em `arena.tks` e um gancho `.tsym` que já existe (stub); (b) não
dependem de RM-C10 nem do serializer O(n); (c) provavelmente já levam o build seco para perto
de < 1 GB (reclamando o scaffolding do checker + tirando as posições duplicadas). Se, medido
pós-PJ-5, o residual ainda exceder a meta, o Eixo C aperta de fase→unidade sobre a MESMA
fundação C6, e o `.tkb` pré-mono habilita a perna native. A jardinagem + `.tsym` são o
"adiantar o que der" do despejo entre estágios.
