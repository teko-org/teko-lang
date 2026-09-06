# Reforma da soma grande `TExprKind` — representação boxeada `tag+ptr` na rota C

> DESIGN-AHEAD (arquiteto). Documento de projeto — **nenhum `.tks` de produto tocado, sem reseed,
> sem bump**. Branch `design/uniao-interface-texprkind` FRESCA de `origin/fix/retirement`
> (HEAD `757e8c0a`). Todas as citações `arquivo:linha` são contra essa base. Executa a direção
> D75 (dono, 2026-08-24): "reforma união→interface no `TExprKind` primeiro".
>
> **O doc reservado pelo dono (`docs/design/somas-grandes-do-compilador-para-interface.md`) NÃO
> EXISTE na árvore.** Este documento é o desenho dessa rodada, com nome descritivo. O desenho-mestre
> adjacente que EXISTE é `plano-match-universal-e-migracao-variant.md` — e a §0 abaixo explica por que
> ele NÃO cobre o `TExprKind` desta bite (é outro eixo, com tensão HALT que esta bite evita).

---

## 0. TL;DR — a premissa "toca todos os match" está errada; o nativo já faz o alvo

Três achados medidos que reescrevem o enunciado:

1. **O desperdício é da ROTA C, e SÓ dela.** O backend nativo/LIR **já** representa toda união como
   `tag+ptr+len` (`variant_wrapper_bytes() = 24`, `src/lir/lower.tks:3828`), com o payload boxeado no
   tamanho EXATO por variante (`variant_payload_box_bytes`/`box_aggregate_value`,
   `lower.tks:3851/4462`) — escalares de registrador ficam inline no slot, sem box
   (`is_register_value_type → 0`). **A representação-alvo desta reforma é EXATAMENTE a que o LIR já
   implementa.** Quem paga 304 B é só o emissor C (`codegen.tks:7479-7498`), que emite a união
   `struct { tag; union { <todas as 26 variantes> } as; }` dimensionada-ao-máximo.

2. **A reforma é uma mudança de CODEGEN, não de linguagem — e toca ZERO sítio de `match` no
   fonte.** `TExprKind` já é uma **união anônima via macro** (`macro TExprKind() { lowering { union } }`,
   `tast.tks:52`), não um `type = variant` nomeado. O ruling do dono "nenhum `type` pode ser união
   nomeada" (`plano-match-universal…` §1) **já está satisfeito**. Os 717 braços `TCall as c`/`TVar as
   v`/… espalhados por 25 ficheiros (contagem medida) **NÃO MUDAM** — a esquerda do `match` continua
   testando `tag`, o `as` continua ligando um alias no payload. A mudança inteira mora em ~10 funções
   de `codegen.tks`.

3. **Portanto NÃO há feature de linguagem nova, NÃO há tensão valor→referência, NÃO há decisão de
   dono pendente.** O caminho que o nome "interface" sugeria (migrar `TExprKind`→hierarquia de
   classe selada, `plano-match-universal…` Parte 2) é ESTRITAMENTE PIOR: exigiria o discriminador de
   type-id em objeto de classe (DP-1, mudança de ABI, hoje honest-stop N2 em `lower.tks:5535`) e a
   virada valor→referência dos ADTs de IR (DP-2, marcada HALT-level em `plano-match-universal…`
   §12.2). Esta bite entrega os mesmos ~140 MB **sem tocar em nada disso**. Ver §9 (o FORK, que é uma
   nota de esclarecimento, não um HALT).

**Ganho:** `TExprKind` 338 MB → ~200 MB (alvo D75), pela substituição do box de 304 B por um wrapper
`tag+ptr` (16-24 B) inline no `TExpr` + um box de payload no tamanho exato por variante (nenhum box
para variantes escalares). Guarda: fixpoint gen2==gen3 + corpus de regressão (exit codes). Ratchet
estrito (D68): pico do build seco TEM que cair.

---

## 1. Como `TExprKind` é representado HOJE (medido)

`TExpr` (`tast.tks:5`) = `struct { kind: @TExprKind(); type: @Type(); line: u32; col: u32 }`.
`@TExprKind()` expande para a união anônima de 26 variantes (`tast.tks:52`).

Layout real (D74, `sizeof` compilados do `teko.c`, `DECISION_LOG.md:729`):

| grandeza | valor |
|---|---|
| `TExpr` | **160 B** (hdr 8 + `kind`* 8 + `Type` INLINE 136 + line/col 8) |
| pointee `TExprKind` (o box) | **304 B** (tag + union dimensionada à MAIOR variante, `TCall`/`TLambda`) |
| nós `TExpr` medidos | **~1,168 M** |
| **`TExprKind` na arena root** | **304 B × 1.166.238 = 338 MB** |

**Por que `kind` já é ponteiro:** a união é RECURSIVA — `TBinary { left: TExpr; right: TExpr }`
contém `TExpr` por valor, e `TExpr.kind` é a própria união → ciclo. `cg_field_boxed`
(`codegen.tks:1101`) detecta a recursão (`cg_te_reaches_byvalue`, `:1046`) e emite o campo `kind`
como ponteiro para um heap-alloc. `union_repr_class` (`:1603`) classifica a união em três reps
existentes — `Niche` / `BoxInArena` (ambas SÓ para `X | null` de 2 membros) / **`InlineTag`** — e
`TExprKind` (26 membros, sem `null`) cai em **`InlineTag`**: `struct { tag; union { … } as; }`
dimensionada ao máximo (`cg_emit_variant_ctype`/`:1677`, emissão em `:7479-7498`).

**O desperdício exato:** um `TBoolLit` (1 B), um `TNullLit` (0 B), um `TVar` (~40 B) — cada um paga
304 B, porque a `union` é dimensionada à maior variante. Todo nó paga o pior caso.

---

## 2. A representação NOVA — `UnionRepr::TagPtr` (espelho do LIR)

Adicionar uma QUARTA rep a `UnionRepr = enum { Niche; InlineTag; BoxInArena; TagPtr }`
(`codegen.tks:1536`). Quando ativa para uma união:

### 2.1 O wrapper (mesma forma do LIR, 24 B)
```c
typedef struct tk_variant_<key> {
    tk_tag_<key> tag;      /* offset 0  */
    void        *payload;  /* offset 8  — box exato, OU escalar inline */
    uint64_t     len;      /* offset 16 — só populado p/ payload que precisa (espelha LIR) */
} tk_variant_<key>;
```
Espelha `variant_wrapper_bytes()=24`, `variant_payload_offset()=8`,
`variant_payload_len_offset()=16` (`lower.tks:3828-3832`). O `len` existe por unidade com o LIR; para
a maioria das variantes de `TExprKind` fica zero.

### 2.2 Payload no tamanho EXATO
- **Variante de agregado** (`TCall`, `TLambda`, `TBinary`, `TStructInit`, …) → `payload` aponta para
  um `tk_region_alloc(sizeof(<variante>))` — box do tamanho EXATO da variante, não do máximo.
- **Variante escalar/registrador** (`TBoolLit` 1 B, `TByteLit` 1 B, `TNullLit` 0 B) → o valor vive
  INLINE no slot de 8 B `payload`, **sem alocação** (espelha `is_register_value_type → box_bytes=0`,
  `lower.tks:3852`). Nós `TNullLit`/`TBoolLit`/`TByteLit` deixam de custar QUALQUER heap.

### 2.3 O wrapper vira INLINE no `TExpr` (uma indireção a menos)
Com o payload atrás de um `void*`, o ciclo de recursão QUEBRA no ponteiro →
`cg_te_reaches_byvalue(TExprKind, TExpr)` passa a devolver `false` →
`cg_field_boxed` NÃO boxeia mais o campo `kind` → `kind` é emitido INLINE (24 B) no `TExpr`. Isto é
automático (o detector de recursão já existe); espelha como o LIR embute o wrapper de 24 B no struct
recipiente e boxeia só o payload.

Resultado por nó: **`TExpr` cresce 160 → ~176 B** (24 B de wrapper inline no lugar do `kind`* de 8 B),
**mas** o box de 304 B some, trocado por 0 B (escalares) ou o tamanho exato (agregados). Contagem de
allocs por nó: hoje 1 box de 304 B; depois 1 box exato (agregados) ou 0 (escalares) — **nunca mais que
hoje**, sem fragmentação nova.

### 2.4 O que muda em quem CONSTRÓI e quem faz `match` (no CODEGEN, não no fonte)
- **Construção** (`emit_variant_wrap*`, `codegen.tks:4476-4553`; hoje emite
  `(T){ .tag=TK_TAG_…, .as.<mk>=<v> }`, `:4446-4468`): passa a emitir
  `({ tk_variant_<key> _w; _w.tag=TK_TAG_…; _w.payload=<box-exato-ou-escalar-inline>; _w; })`.
- **Teste de tag no `match`** (`:5265-5295`): `subj.tag == TK_TAG_…` — **INALTERADO**.
- **Bind/acesso de payload** (`:2326`, `:5452-5482`; hoje `subj.as.<mk>`): passa a
  `(*(<variante>*)subj.payload)` para agregado, ou o escalar reinterpretado do slot. O `as` continua
  um alias-ref para o payload (§4).
- **Typedef da união** (`cg_emit_variant_ctype:1677`, `:7479-7498`): emite o wrapper `{tag,ptr,len}`
  no lugar de `{tag; union{…}}`.
- **`cg_union_tag_ctype`** (`:1524`) e o mangle (`cg_variant_typename`, `:1407`) — reusados
  como estão (a chave do tipo é a mesma; muda o corpo do typedef).

**Fonte `.tks`: ZERO mudança.** `tast.tks` fica idêntico — `TExprKind` continua `macro … { union }`.

---

## 3. Raio dos match-sites — QUANTIFICADO

| eixo | contagem | muda? |
|---|---|---|
| ficheiros `.tks` com braço `<VarianteTExpr> as` | **25** (medido: grep dos 26 nomes + ` as`) | **NÃO** |
| braços `as`-binder sobre variantes de `TExprKind` | **717** ocorrências | **NÃO** |
| sítios `match … .kind` (todos os `.kind`, superconjunto) | 348 em 51 fich. | **NÃO** |
| funções de `codegen.tks` a editar | **~10** (§2.4) | **SIM** |
| ficheiros de codegen a editar | **1** (`codegen.tks`) | **SIM** |
| `src/lir/lower.tks` (nativo) | 0 | **NÃO** (já é `tag+ptr`) |
| serialização `.tkb`/`.tkh` | 0 | **NÃO** (serializa a estrutura LÓGICA dos membros, não o layout C) |

Este é o achado central que corrige "toca todos os match" (nota do coordenador em D75): a reforma de
REPRESENTAÇÃO toca 0 `match` no fonte. O "717 sítios" seria o raio da reforma de SUPERFÍCIE
(variant→classe selada, Parte 2 do `plano-match-universal…`) — que esta bite NÃO faz.

Top-5 dos consumidores (por nº de braços, para o SCOUT confirmar que nenhum precisa de edição):
`comptime_fold.tks` 70 · `escape.tks` 60 · `consteval.tks` 44 · `spine.tks` 35 · `typer.tks` 31.
Todos continuam `match e.kind { TCall as c => … }` verbatim.

---

## 4. Aliasing / lifetime

- **Mesma arena.** O payload é alocado com `tk_region_alloc` na região CORRENTE — a mesma que segura
  o nó `TExpr`. Vive exatamente enquanto o nó vive (R11: `ref` válida enquanto viva a arena do alvo).
  Idêntico à semântica atual (hoje o payload vive DENTRO do box de 304 B, na mesma arena). Espelha o
  LIR (`box_aggregate_value` → `tk_slice_elem_box` na região corrente).
- **Nenhuma variante compartilha/aliasa payload.** Cada construção boxeia o seu próprio; não há
  interning nem partilha. Uma variante não referencia o payload de outra.
- **O bind `as` é alias-ref, não cópia** (ruling do dono, `plano-match-universal…` §1). Com `TagPtr`,
  `TCall as c` liga `c = (checker::TCall*)subj.payload` — um alias no payload boxeado. É EXATAMENTE a
  semântica de hoje (`c` hoje aliasa `subj.as.tcall`, dentro do box). Readonly raso preservado.
- **Uma sutileza a garantir (o SCOUT verifica):** hoje o payload é CONTÍGUO ao tag (dentro do mesmo
  box); com `TagPtr` fica em alocação SEPARADA. Nenhum código de fonte toma `&kind.as.mk` como
  ponteiro cru contíguo ao tag — todo acesso é por campo, gerado pelo codegen, logo consistente. Sem
  superfície de UAF nova. É por isso que o fixpoint segura o comportamento.

---

## 5. Ganho por estágio + a PRIMEIRA BITE segura

O flip de rep é ATÔMICO por união: `union_repr_class` é determinístico sobre o conjunto de membros,
então uma união flipa em TODOS os seus sítios de uma vez (typedef, construção, match, acesso). Não há
estado intermédio meio-flipado. A gatilhagem escolhe QUAIS uniões flipam.

**Gatilho recomendado (principiado, não threshold frágil): união `InlineTag` RECURSIVA.** Uma união
que se alcança a si mesma por valor já é HOJE boxeada (`cg_field_boxed`) e é exatamente o caso
patológico dimensionado-ao-máximo. `cg_union_inline_recursive` (`codegen.tks:1616`) já existe (hoje só
consultado no caso `X|null` de 2 membros); generalizá-lo para N-ário é a chave do gatilho.

| bite | escopo (gatilho) | uniões alcançadas | ganho estimado | reseed |
|---|---|---|---|---|
| **1** | `InlineTag` recursiva | `TExprKind` (26), `TStatement` (11), `ExprKind`, `Statement` | **338 MB → ~200 MB** no `TExprKind` (~140 MB) + folga de `TStatement`/parser | sim (fixpoint gen2==gen3, ratchet) |
| 2 | `InlineTag` não-recursiva com máx-membro ≥ T (threshold medido) | demais somas grandes por valor | por medir | sim |
| — | `Type` (14 variantes) — **eixo SEPARADO** | é INLINE 136 B (não boxeada); lever "de-inline" (D68 lever D), mecanismo distinto (tornar `TExpr.type` ponteiro) | ~80-250 MB (D74) | fora desta bite |

**Bite 1 é a mais segura E a mais atômica:** um gatilho principiado (recursividade), uma família
coerente de uniões (os ADTs de AST auto-referentes), o nativo já correto, o fonte intocado. Se o dono
quiser medir SÓ o `TExprKind` isolado antes de estender, refinar o gatilho para
`recursiva && membros > 20` restringe a bite 1 a `TExprKind`/`ExprKind` — recomendo o conjunto
recursivo inteiro (é a mesma máquina, todos são hogs de AST, e `TStatement`/`Statement` também são
`InlineTag` recursivas por conterem blocos de statements).

**Medição obrigatória (ratchet, D68):** o implementer mede `teko: memory: peak` do build seco ANTES e
DEPOIS. Recomendo também um probe de histograma de variantes (`TEKO_ARENA_OBS`) para caracterizar a
distribuição dinâmica e prever o ganho — mas a régua que LANDA é o pico do build seco. Cair → landa;
flat/subir → conserta antes de landar.

---

## 6. Sequência de crumbs (cada um gate-able)

**RITUAL:** *gate-cheio* = build seco (`--no-verify --release`, `TEKO_CC=clang`, cap `ulimit -v
4718592`) + fixpoint gen2==gen3 byte-idêntico + medição de pico (ratchet) + reseed
`bootstrap/teko.c`. *fixture-gate* = os oráculos `.tkr` nomeados no crumb, rodados isolados.

### Crumb 1 — `UnionRepr::TagPtr` + detector de recursão N-ário. **M. Só codegen, aditivo. RITUAL: build seco (sem reseed ainda).**
Adiciona o membro `TagPtr` ao enum, generaliza `cg_union_inline_recursive` para N membros, e faz
`union_repr_class` devolver `TagPtr` para união `InlineTag` recursiva — SEM ainda emitir a nova forma
(a rep é decidida mas o emissor ainda cai no caminho `InlineTag`; é um no-op observável). Valida que a
classificação não quebra nada. Toca: `union_repr_class:1603`, `cg_union_inline_recursive:1616`,
`cg_union_field_inline_tag:1113`.

```teko
/**
 * union_repr_class — classifica a representação C de uma união; agora com TagPtr para somas
 * recursivas, que deixam de ser dimensionadas-ao-máximo e passam a tag+ptr+payload-exato
 * (espelho do wrapper de 24 B do backend nativo, lower.tks variant_wrapper_bytes).
 *
 * @param prog          o programa em codegen (type-table + probes)
 * @param v             a união a classificar
 * @param forced_inline força InlineTag (caminho legado de campo forçado)
 * @return              Niche | BoxInArena | TagPtr | InlineTag
 */
fn union_repr_class(prog: CgProg, v: checker::Variant, forced_inline: bool): UnionRepr

/**
 * cg_union_is_tagptr — a união é InlineTag recursiva (alcança-se por valor) e portanto elegível ao
 * boxing tag+ptr? Generaliza cg_union_inline_recursive (hoje só consultado no caso X|null) para
 * qualquer aridade; é o gatilho principiado da bite 1.
 *
 * @param prog o programa em codegen
 * @param v    a união candidata
 * @return     true sse algum membro alcança a própria união por valor
 */
fn cg_union_is_tagptr(prog: CgProg, v: checker::Variant): bool
```

### Crumb 2 — emitir o typedef `tag+ptr+len` para `TagPtr`. **M. Codegen. RITUAL: build seco.**
`cg_emit_variant_ctype:1677` e a emissão de typedef `:7479-7498` passam a emitir
`struct { tag; void* payload; uint64_t len; }` quando a rep é `TagPtr`. `emit_type`/`emit_type_str`
(`:1159`/`:1194`) já roteiam por rep — adicionar o arm `TagPtr` (o tipo do valor é o wrapper, não um
ponteiro-para-union). `cg_field_boxed` passa a NÃO boxear o campo (a recursão quebra no `void*`).

### Crumb 3 — construção: box exato / escalar inline. **L. Codegen. RITUAL: build seco.**
`emit_variant_wrap*` (`:4476-4553`) emite, para `TagPtr`: box de `sizeof(<variante>)` exato via a
primitiva de arena (agregado) OU escalar inline no slot (registrador). Espelha
`store_scalar_variant_payload_value`/`box_aggregate_value` do LIR (`lower.tks:3819/4462`) — mesma
decisão `is_register_value_type`.

```teko
/**
 * cg_tagptr_payload_inline — o payload desta variante cabe inline no slot de 8 B do wrapper (escalar
 * de registrador), dispensando box? Espelha is_register_value_type do LIR, para a rota C não boxear
 * TBoolLit/TByteLit/TNullLit.
 *
 * @param prog o programa em codegen
 * @param mem  o tipo da variante (o membro casado)
 * @return     true sse o payload é escalar de registrador (sem box)
 */
fn cg_tagptr_payload_inline(prog: CgProg, mem: @Type()): bool
```

### Crumb 4 — acesso/bind: `(*(T*)subj.payload)`. **L. Codegen. RITUAL: gate-cheio + reseed + medição (RATCHET).**
`:2326` e `:5452-5482` emitem o acesso via `payload` (deref para agregado, reinterpret para escalar) no
lugar de `.as.<mk>`. O teste de tag (`:5265-5295`) fica intacto. **Este é o crumb que FECHA a bite:**
com construção+acesso coerentes, o build seco reproduz-se (fixpoint) e o pico cai. Reseed aqui.

### Crumb 5 — (opcional, bite 2) estender `TagPtr` a somas grandes não-recursivas por threshold. **M. RITUAL: gate-cheio + reseed.**
Só depois de bite 1 verde e medida. `union_repr_class` passa a devolver `TagPtr` também para
`InlineTag` com `cg_type_min_bytes(máx-membro) ≥ T`, T calibrado pela medição. Generaliza para "todas
as somas grandes" (D75).

---

## 7. Fixtures de regressão (input → exit code nativo esperado)

O guarda PRIMÁRIO é o fixpoint (o compilador auto-compilando exercita `TExprKind` em escala massiva —
todo `.tks` do corpus constrói milhões de nós). Por lei (CLAUDE.md), NÃO se escreve teste para o que
o self-build exercita. Logo os oráculos abaixo cobrem só o que o fixpoint NÃO alcança de forma
discriminante — comportamento observável de programa de USUÁRIO com união, para provar equivalência
byte-comportamental:

| fixture (`.tkr`, C-nativo) | forma | exit |
|---|---|---|
| `variant_tagptr_scalar_roundtrip` | união recursiva com braço escalar (`E \| Node`, `Node{left:E;right:E}`), constrói folha escalar, `match` devolve o escalar | valor do escalar |
| `variant_tagptr_aggregate_roundtrip` | mesma união, constrói braço agregado (2 campos), `match … as n` lê `n.left`/`n.right` | soma dos campos |
| `variant_tagptr_deep_tree` | árvore recursiva de profundidade N (exercita box exato aninhado + alias `as`) | fold da árvore |
| `variant_tagptr_null_niche_untouched` | `X \| null` (2 membros) — **continua Niche/BoxInArena**, NÃO vira TagPtr | prova que o gatilho não captura o caso de 2 membros |

Todos C-nativo (`teko build <dir> -o bin`, run). Nenhum teste AFIRMATIVO sobre o `TExprKind` do próprio
compilador (o fixpoint É essa prova). A `variant_tagptr_null_niche_untouched` é a check de FRONTEIRA do
gatilho (garante que Niche/BoxInArena não regridem).

---

## 8. Pontos de ritual (gate-cheio obrigatório)

- **R1** — fim do Crumb 4 (bite 1 completa: `TExprKind`/`TStatement`/`ExprKind`/`Statement` em
  `TagPtr`): gate-cheio + fixpoint gen2==gen3 + **medição de pico (ratchet ESTRITO — TEM que cair)** +
  reseed. As 3 fixtures de roundtrip + a de fronteira.
- **R2** — fim do Crumb 5 (bite 2, se autorizada): gate-cheio + ratchet + reseed.
- Crumbs 1-3 param no build seco (sem reseed) — são construção aditiva; o reseed único vem no fecho da
  bite (R1), por disciplina "limpeza primeiro, reseed no fim" (CLAUDE.md).

---

## 9. Riscos + tensões de lei (com resolução)

1. **[ESCLARECIMENTO, não HALT] duas leituras de "união→interface".** O nome reservado pelo dono
   (`somas-grandes-do-compilador-para-interface.md`) e a nota "toca todos os match" sugerem a reforma
   de SUPERFÍCIE (variant→hierarquia de classe/interface selada). Este desenho toma a leitura de
   REPRESENTAÇÃO (boxing tag+ptr no emissor C), porque **law-first (passa-todas-as-leis vence)** ela é
   estritamente superior: mesmo ganho (~140 MB), ZERO mudança de linguagem, ZERO mudança de match,
   ZERO virada valor→referência, e — decisivo — **o backend nativo JÁ a implementa** (é engenharia
   sobre piso decidido, não capacidade nova). A leitura de superfície dispararia DP-1 (type-id em
   classe, ABI) + DP-2 (valor→referência, HALT-level, `plano-match-universal…` §12.2). **Resolução:
   seguir a representação; a migração de superfície é um track SEPARADO, maior, owner-gated, e NÃO é
   pré-requisito do ganho de memória.** Reportado ao dono como nota, não como bloqueio.

2. **Byte-identidade da saída — a régua real é fixpoint + regressão, não "mesmo `teko.c`".** Correção
   honesta (M.3): a reforma MUDA o `teko.c` emitido (é o ponto — o compilador emite representação mais
   enxuta para toda união afetada, incluindo as suas próprias). O invariante que se preserva é o
   COMPORTAMENTO: (a) fixpoint gen2==gen3 (o compilador novo reproduz-se byte-a-byte sob a rep nova) e
   (b) o corpus de regressão (exit codes inalterados). "Mesmo programa → mesmo `teko.c`" no sentido
   literal NÃO pode valer para o fonte do próprio compilador (ele usa `TExprKind`); vale a
   equivalência comportamental, provada pelas duas guardas.

3. **`TExpr` cresce 160 → ~176 B (wrapper inline).** +16 B/nó × 473k nós `TExpr` = ~7 MB a mais no
   bucket `TExpr`, contra ~140 MB a menos no bucket `TExprKind`. Net fortemente positivo. Alternativa
   conservadora se o inline complicar o detector: manter `kind` boxeado (box de 24 B do wrapper +
   payload exato) — 2 allocs/nó, `TExpr` fica em 160 B, ganho um pouco menor. Recomendo o inline
   (espelha o LIR e é 1 alloc/nó). O SCOUT confirma que `cg_te_reaches_byvalue` devolve `false` com o
   payload atrás de `void*`.

4. **Alcance da bite 1 além do `TExprKind`.** O gatilho "recursiva" também flipa
   `TStatement`/`ExprKind`/`Statement`. É desejável (mais ganho, mesma máquina) e coerente, mas
   AMPLIA o delta do `teko.c` do primeiro reseed. Se o dono preferir isolar `TExprKind` para a
   primeira medição, o gatilho `recursiva && membros > 20` o restringe. Decisão de gradiente, não de
   lei — deixo o conjunto recursivo como recomendação e o refinamento como botão.

5. **`Type` (136 B inline) NÃO entra aqui.** É lever diferente (de-inline, D68 lever D): `Type` é
   INLINE no `TExpr`, não boxeada; encolhê-la é tornar `TExpr.type` um ponteiro, mecanismo distinto do
   `TagPtr`. Listado em §5 como track separado para não misturar as duas mecânicas num reseed.

## 9.1 O que NÃO é FORK (engenharia sobre piso decidido)
A rep `tag+ptr+payload-exato` já é a ratificada no LIR (`variant_wrapper_bytes=24`). Portá-la ao
emissor C é implementação, não decisão. Não requer ruling.

## 9.2 O que É nota-pro-dono (não HALT)
Só o esclarecimento de §9.1 (representação vs superfície). Não há tensão de lei residual: `TExprKind`
já está na forma permitida (união anônima via macro), a rep nova preserva comportamento (fixpoint), e
não há C novo (é codegen que EMITE C mais enxuto, permitido). **Nenhum HALT genuíno.**

---

## 10. Generalização (depois do `TExprKind`)

Ordem de herança da mesma reforma `TagPtr` (todas já `tag+ptr` no LIR; só o emissor C paga):
1. `TStatement` / `Statement` (11/10 membros, recursivas via blocos) — **na bite 1** (mesmo gatilho).
2. `ExprKind` (24, gêmeo parser de `TExprKind`) — **na bite 1**.
3. Somas grandes não-recursivas por threshold (bite 2): candidatos por `cg_type_min_bytes` alto.
4. `TItem`/`ItemKind`/`Decl` (uniões-de-uniões / membros compartilhados) — flipam pela mesma rep sem
   drama (a rep não se importa com partilha de membro; é o eixo de SUPERFÍCIE que se importa).
5. **`Type` (136 B inline) — track SEPARADO** (de-inline, não `TagPtr`).

Os ADTs de IR do backend (`MInst` 31, `MInstX86` 26, `LOp` 16) são consumidos pelo lowering nativo; no
build seco por rota C eles são construídos apenas se o pipeline chega ao LIR — medir o bucket antes de
incluí-los (não estimar).

---

*Grounding: `origin/fix/retirement` HEAD `757e8c0a`. Achados-chave: `codegen.tks:1101,1113,1536,1603,
1616,1677,4446-4553,5265-5482,7479-7498` (rep/union C); `lower.tks:3819-3843,3851,4462`
(`variant_wrapper_bytes=24`, box exato — o alvo já implementado); `tast.tks:5,52` (`TExpr`/`TExprKind`
macro-união); `DECISION_LOG.md:729-730,764,775-778` (censo D74 + direção D75). Companheiros:
`plano-match-universal-e-migracao-variant.md` (a reforma de SUPERFÍCIE, track separado),
`interface-value-type.md` (fat-pointer de interface, não usado aqui).*
