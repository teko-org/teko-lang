# AL Wave — crumbs (rascunhos de issue versionados na umbrella)

Status: **RATIFICADO (owner, 2026-07-19).** Estes são os textos de crumb/issue da onda
AL, versionados na umbrella `remodel/emit-throughput` (o owner optou por manter o
rascunho aqui, não como issues do GitHub). Design completo: `al-wave-emit-throughput.md`.

Ordem ratificada (REORDENADA pós-AL1, 2026-07-19): **AL1(feito) → [AL0 + AL4a em paralelo,
cedo, sem máquina nova] → F1→F2→F3 (FUNDAÇÃO) → AL3 (ref-push global, o lever) → AL4b
(str-builder/stream) → AL5 (region-per-phase) → AL6 (migração restante).**
**AL2 (endurecer push-cache) SAIU** — AL3 elimina o cache global de vez, e a prova mostra
que os grows >1MB já são saudáveis (a eviction size-aware protege o buffer grande); o
paliativo não compra nada que a ponte de coexistência do AL3 não entregue melhor.
Execução é proof-first: **AL1 fechou os números** (`al1-proof-report.md`).

Legenda: cada crumb roda como WIP na umbrella (commit direto ou branch → PR de volta na
umbrella); bump só quando o owner cortar versão (duplica umbrella → org, PR de seed/bump).

---

## AL1 — Prova do gargalo de emit + censo dos push-sites (proof-first, zero fix) · M

### Contexto
O emit gera o teko.c ~20x mais devagar do que o `cc` o compila. Causa suspeita (a
confirmar, não assumir): `[]byte` é `{ptr,len}` SEM `cap` (`teko_rt.h:59`); a capacidade
vive na tabela global `tk_push_cache` (65536 buckets, `teko_rt.c:2091`); MISS por colisão
de slot força copy-grow do buffer inteiro (incidente "11.5 GB fix", `teko_rt.c:2155` —
~85% do churn).

### Objetivo — SÓ medição, nenhum fix
1. PROVAR/REFUTAR o storm de copy-grow.
2. CENSAR os ~1383 push-sites por nível de conhecimento — base de escopo do AL0/AL6.

### Veredito do storm
- PROVADO sse `copy_amp(emit) > 4·log2(output_bytes)` E a coluna >1MB do histograma de
  miss-reason é dominada por `other-ptr` (colisão) ou `len` (aliasing).
- REFUTADO se dominada por `cap-full` (doublings sãos).

### Método
- Self-build com `TEKO_ARENA_OBS`: histograma de miss-reason
  (`empty|other-ptr|len|cap-full|esz/gen`, coluna >1MB) + tabela dark-matter de str.
- `copy_amp` = bytes copiados / bytes lógicos no emit.
- Timing por fase (parse/check/mono/codegen/emit) — confirmar que emit domina.

### Censo dos push-sites (o número-MANCHETE)
- **CONSTANTE DISFARÇADA** (build-and-return de array fixo, nullary/args-const, sem arg de
  runtime) → const-ificável (AL0). **Número-manchete — a dor real.**
- itens conhecidos → literal `[a,b,c]` (AL0)
- tamanho conhecido, itens dinâmicos → `[N]T` (fundação/AL6)
- genuinamente dinâmico → ref-push (AL3)

### Distinguir causas alternativas (não confirmar a favorita)
- interning O(n²) → timing + dark-matter
- arena sem size-header → miss `esz/gen`
- str-concat (`tk_str_concat`) → dark-matter; se ganhar, o núcleo vira str-builder

### Entregável
Relatório máquina-legível: veredito do storm + causa dominante + censo dos 4 níveis (com
a contagem de constantes disfarçadas em destaque). DECIDE o escopo de AL0. Nenhum código
de produto muda.

---

## Banda EARLY-PARALELA (roda cedo, sem máquina nova, semi-independente do ref-push)

### AL0 — Const-ificar produtores build-and-return de array fixo · S mecânico · HONESTIDADE
**A prova rebaixou AL0 de perf-lever a crumb de HONESTIDADE/W15**: o censo achou só **5
sites** const-ificáveis (~1,4 KB rodata), não a dor de perf. Mantido mesmo assim pelo
princípio owner **"código não mente sobre o que é"**: um produtor determinístico de dado
fixo É `const`, não função — independente do ganho. Sem máquina nova (literais + `const`
T-B6 já no seed). Ritual: fixpoint por sub-lote; onde muda bytes (runtime→rodata),
justificar. Behavior: muda-C-do-compilador (5 sites).
- **Produtor build-and-return** (nullary/args-const, compile-time-constante) → `const X=[...]`.
- **Produtor parametrizado** (arg de runtime) → CONTINUA função com literais internos.
- **Chamador que muta**: `mut c = CONSTANTE` (cópia eager, valor já existente) + `grow(&c,…)`.

#### AL0 — spec de implementação (auditoria de const-eval)
**NÃO é edit mecânico.** O const-eval (`consteval_form.tks::is_const_expr`) aceita HOJE:
literais (Tier 0), cast (1), unário `~`/`-` + binário `+ - * / % & | ^ << >>` (2), ref a
const nomeada + membro enum/flags (3), literal array/struct/variant com componentes const
(4), call SÓ na allowlist FECHADA `{preg, teko::f64_from_bits, teko::f32_from_bits}` (5).
**REJEITA**: `TIndex` (`GZIP_MAGIC[0]`), loop/range, qualquer call fora da allowlist.

Veredito por alvo:
- **`fixed_litlen_lengths` (inflate.tks:253) → MANTER como gerador.** Perf ~0 (é a lib de
  compress, NÃO o hot-path de emit); um literal de 288 valores destrói a legibilidade das
  ranges do RFC 1951; "código não mente" é FRACO aqui (é genuinamente um mapeamento
  range→comprimento, honestamente um gerador). Const exigiria range-expansion no const-eval
  (extensão real, não mecânica) sem ganho.
- **`fixed_dist_lengths` (inflate.tks:272) → MANTER como gerador** (irmão do acima; perf ~0).
  Um `const [5,5,…]` de 32 valores é viável HOJE (Tier-4) mas repetitivo; manter por
  consistência e legibilidade. (Se o owner insistir na honestidade, é literal-32, trivial.)
- **`gzip_header` (gzip.tks:33) → PRECISA ESTENDER const-eval.** Usa `GZIP_MAGIC[0..1]`
  (`TIndex` de const — rejeitado) E chama `gzip_cm_deflate()` (fora da allowlist). Para
  const-ificar sem duplicar o magic (D39): (i) **Tier-6 novo: `TIndex` de const agregada por
  índice const-expr**; (ii) trocar `gzip_cm_deflate()` por um `const GZIP_CM_DEFLATE: byte = 8
  to byte` (ele já é `{ 8 to byte }`). Então `const GZIP_HEADER: []byte = [GZIP_MAGIC[0],
  GZIP_MAGIC[1], GZIP_CM_DEFLATE, 0 to byte, …]`.
- **`wasm_preamble` (objfile_wasm.tks:172) → PRECISA ESTENDER const-eval** (mesmo Tier-6
  `TIndex`-de-const para `WASM_MAGIC[0..3]`; `WASM_VERSION_1` já é Tier-3 OK).
- **`wasm_narrow_msg_bytes` (stackify.tks:4503) → PRECISA ESTENDER const-eval** (str→[]byte em
  posição const) OU manter gerador. Um byte-array literal perde a string legível; a extensão
  limpa é permitir `const MSG: []byte = "…"` (coerção str→bytes const).

**Conclusão AL0:** o gargalo é uma **extensão de const-eval (Tier-6: `TIndex` de const
agregada por índice const-expr; opcional str→[]byte)** — um crumb próprio, não um sweep. 2
dos 5 alvos (Huffman) ficam como geradores (perf 0, legibilidade). 3 dependem da extensão.
Ganho de perf ~zero; o valor é honestidade/W15 + o Tier-6 reusável. **Nenhum chamador muta
os 5** (todos retornam e o consumidor lê) → ref direto, sem `mut c = CONST`.

---

### AL4a — Interning/memoização de nomes manglados (ACHADO NOVO da prova) · M · CEDO
**Alvo nomeado (RA-medido):** `cg_variant_typename_str` reconstrói a MESMA string
determinística **6,65 MILHÕES de vezes** (99 MB); `cg_opt_key` 2,85M (20 MB);
`cg_variant_key` 1,03M (22 MB); `checker::qualify` 1,1M (17 MB). O nome manglado de um tipo
é DETERMINÍSTICO → computa 1× por tipo distinto e cacheia (memo table type→str). 6,65M+2,85M+
1,03M allocs → ~milhares. **Semi-independente do ref-push, grande E barato** → entra CEDO, em
paralelo à fundação (como o AL0). Behavior: preserva-tudo (só cacheia; a string é idêntica).
Ritual: fixpoint + probe: allocs de `cg_variant_typename_str` no dark-matter → ~milhares.

**Design do cache (spec):**
- **Keyed por:** a IDENTIDADE ESTRUTURAL do tipo. `Map<V>` (collections/map.tks) é keyed por
  `str` → key = um **hash estrutural u64 em hex curto**, computado por um FOLD recursivo
  barato sobre o `Type`/`Variant` (SEM alocação por nó, ao contrário do concat). Value = o
  nome manglado memoizado. **Hit verificado por igualdade estrutural de tipo** (a máquina de
  equality de `type.tks`) para descartar colisão de hash. O nome é função PURA determinística
  do tipo → a key estrutural o determina unicamente.
- **Onde vive:** um CAMPO no contexto de codegen (`CgCtx`), NÃO global de módulo — per-build,
  sem estado cross-build, thread-safe no futuro. `checker::qualify` vive no checker → intern
  SEPARADO no contexto do checker. Threaded por `&` quando F1 aterrissar; pré-F1, um `Map`
  threaded/retornado (funciona hoje).
- **Invalidação:** NENHUMA — o nome manglado é estável durante todo o build; o intern vive a
  fase de codegen e morre com ela (região da fase, AL5).
- **Sites que consultam:** os 5 produtores viram `if hit = intern_lookup(&cache, t) { return
  hit }; let s = <compute atual>; intern_insert(&cache, t, s); s` — `cg_variant_typename_str`
  (1197), `cg_member_key_str` (~1180), `cg_opt_mangle_str`/`cg_opt_key`, `cg_variant_key`
  (codegen); `checker::qualify` (intern do checker).
- **Prova behavior-preserving:** a string memoizada é LITERALMENTE o retorno do compute
  inalterado na 1ª ocorrência; um hit só retorna quando a igualdade estrutural bate, e o
  compute é função pura determinística do tipo → cacheada == recomputada, byte-a-byte.
  **Preserva-tudo.** A win: troca 6,65M concats por 6,65M folds-de-hash baratos + ~milhares
  de concats (só nos misses).

---

## FUNDAÇÃO (pré-requisito do ref-push) — a rascunhar

- **F1** — borrow mutável seguro `&x` (grafia A: dessugar pra `Reference` existente,
  spine `is_unique_at` autoriza) · L · preserva-alvo/muda-C-do-compilador — **CRUMBS ABAIXO**
- **F2** — `let` imutável profundo (auto-enforçado; ~7 sites, baixo risco) · S/M · preserva
- **F3** — array Model A `[N]T` `{ptr,len,cap}` sem zero-fill · L · preserva-alvo/muda-rep

### F1 — sequência de crumbs (design PRONTO, 2026-07-19; owner-ruled, não reabrir)

**Estado atual (file:line — a máquina JÁ existe, `&x` só a torna explícita na superfície):**
- `Reference = struct { inner: Type }`, never-null R2, C rep `<T> *` — `src/checker/type.tks:90`
  (nota `:82-88`). Superfície = `Ref<T>` (um `NamedType` segmento "Ref").
- **Auto-ref JÁ acontece HOJE, só que IMPLÍCITO em posição de argumento** — `src/checker/
  typer.tks:1170-1191`: param `Reference` + arg `mut` lvalue (`TVar`) do tipo apontado ⇒
  `auto_reffed=true`, mantém `a.type=T`, rejeita binding imutável (`:1181`). Codegen emite o `&`
  em `src/codegen/codegen.tks:2518-2525`; `Reference` lowera pra `<T> *` (`codegen.tks:1000-1001`,
  `:1314`). **F1 = expor esse auto-ref como o operador `&x`, sem sintaxe de tomada de ref hoje.**
- Parser: NÃO há prefixo `&`/`*` hoje — `&` (Amp) é SÓ binário bitwise no nível multiplicativo
  (`src/parser/optokens.tks:25`, `parse_multiplicative` `src/parser/parse_expr.tks:511`); o unário é
  só `- ~ !` (`optokens.tks:9-13`). Um `&` em posição de PREFIXO é inambíguo (binário só ocorre
  após operando esquerdo). `*`/deref NÃO muda em F1 (prospecção, §12 do doc-mãe).
- Spine (#331): queries PURAS já existem — `is_unique_at(s, binding) -> bool`
  (`src/checker/spine.tks:1484`), `ref_target_outlives(s, borrow, referent) -> bool` (`:1438`),
  `BorrowedFrom = BfNone|BfParam|BfLocal|BfTop` (`:146`). A relaxação **L2a** (`bf := BfLocal` no
  ÚNICO sítio sintático de borrow de um ref-bind local) está descrita `:21-23,:139-143,:1268,:1352`
  — é EXATAMENTE a máquina do `mut y = &x`. F1 é o CONSUMO (PR-2/PR-3), não a query.
- Escape-gate: `type_contains_ref` (`src/checker/type.tks:163-177`) proíbe `Reference` em toda
  posição que NÃO seja param escalar bare — é o que hoje bloqueia o sink `mut y = &x`. F1.3 relaxa
  UM caso (ref-bind local mutação-through-ref), spine-autorizado.
- **Lei: SEM tensão.** `TEKO_LEGISLATION.md` (nota pointer-family) já legisla "The SAFE, region-
  checked, never-null, Teko-internal reference is `ref` (evolution S3) — orthogonal to the unsafe
  foreign `ptr`". `&x` é a superfície do `ref` SEGURO (checado pelo spine), não um sigil unsafe
  novo. `*`/deref fica unsafe (não tocado). **Nenhuma nota de lei nova, nenhum HALT.**

**Behavior:** TODOS os crumbs são ADITIVOS — nada no corpus USA `&x` ainda ⇒ o C emitido pro
alvo é byte-idêntico e o fixpoint gen2==gen3 vale INALTERADO. O C do PRÓPRIO compilador só ganha
código morto (caminhos novos não exercitados pelo self-build) até AL3/AL6 migrarem sites.

| # | Crumb | Tam | Ritual |
|---|---|---|---|
| **F1.1** | Parser: prefixo `&x` como expr de borrow → AST | S | build verde + parser_test.tkt |
| **F1.2** | Checker: tipar `&x` → `Reference<T>` (mut/shared inferido); rejeitar `&(let)` mutável | M | checker_test.tkt + fixpoint |
| **F1.3** | Spine: autorizar borrow exclusivo (`is_unique_at`) + lifetime (`ref_target_outlives`); L2a `bf:=BfLocal`; relaxar escape-gate p/ o sink `mut y=&x` | M | spine_test.tkt + fixpoint verde |
| **F1.4** | Codegen: lower `Borrow` → address-of `&` | S | codegen_test.tkt + diff VM==native |
| **F1.5** | Ponte: `teko::list::grow(&x, v)` (coexistência, sem migrar sites) | S | list_test.tkt + fixpoint |
| **F1.6** | Fixtures de regressão + fixpoint verde (prova de aditividade) | S | **RITUAL: fixpoint gen2==gen3 INALTERADO** |

**F1.1 — Parser.** Append um membro NOVO ao fim de `ExprKind` (append = tags estáveis, TKB
byte-layout preservado; "código não mente" — um nó honesto, não `Unary{op=Amp}` sobrecarregado):

```teko
/**
 * A borrow expression `&x` — take a SAFE, exclusive-temporary, lifetime-bounded reference to the
 * mutable lvalue `x` (grafia A: desugars to the existing `Reference`; NOT a raw pointer, NOT
 * `unsafe`). The mutability (exclusive-mut vs shared) is INFERRED by the checker from `x`'s binding
 * and the target parameter — there is no `&mut`. Only the leading-`&` prefix position produces this
 * node; a binary `a & b` (bitwise-and, multiplicative level) is unchanged.
 *
 * @field operand  the borrowed lvalue (a `Var`/`FieldAccess`/`Index` path; checked to be an lvalue)
 * @since 0.x (#AL/F1)
 */
Borrow: struct { operand: Expr }
```

Em `parse_unary` (`src/parser/parse_expr.tks:472`), ANTES do `is_unary`: se `tokens[pos].kind ==
lexer::TokenKind::Amp`, consumir e produzir `Expr { kind = Borrow { operand = <parse_unary(pos+1)> } }`.
Precedência = unário (right-assoc, acima de `to`), igual `- ~ !`. `is_unary`/`is_multiplicative`
INALTERADOS (o binário `&` continua no nível multiplicativo; o prefixo é resolvido antes de descer).

**F1.2 — Checker.** No typer de expressão, um braço `Borrow as b =>`: resolver `b.operand` como
lvalue, achar o binding (reusar `lookup_binding`, `typer.tks:1179`), extrair `T`, produzir
`TExpr { type = Reference { inner = T } }`. **Inferência mut/shared** (sem `&mut`): reusar VERBATIM
a lógica de `typer.tks:1181` — `let`/binding imutável ⇒ borrow SHARED (permitido só em posição de
param shared; `&(let)` numa posição mutável ⇒ ERRO "cannot take a mutable reference to immutable
`x` — declare it `mut`"); `mut` binding + posição de param mutável ⇒ borrow MUT. O `&x` em posição
de ARGUMENTO é o caso behavior-preserving (é o auto-ref implícito de hoje, agora explícito) — faça
ESTE primeiro. O sink local `mut y = &x` fica gated por F1.3.

```teko
/**
 * Type a borrow expression `&x` (F1.2). Resolves the operand to an lvalue, reads its binding, and
 * yields a `Reference{inner=T}`. Mutability is INFERRED, not spelled: an immutable (`let`/non-`mut`)
 * target in a mutable position is rejected here; a `mut` target yields the exclusive-mut borrow the
 * spine (F1.3) then authorizes. Reuses the existing auto-ref mutability check (typer.tks:1181).
 *
 * @param b     the `Borrow` AST node (`&operand`)
 * @param env   the binding environment (for the operand's mutability + type)
 * @param mut_pos  true iff the borrow sits in a mutable-borrow position (param wants `Ref<T>` mut)
 * @return      the typed borrow (`Reference{inner=T}`), or a type error (immutable target, non-lvalue)
 * @throws      error when the operand is not a `mut` lvalue in a mutable position, or not an lvalue
 * @since 0.x (#AL/F1.2)
 */
fn type_borrow_expr(b: parser::Borrow, env: Env, mut_pos: bool) -> TExpr | error
```

**F1.3 — Spine.** No sítio de borrow (o nó `Borrow` OU o ref-bind local `mut y = &x`), CONSUMIR as
queries que já existem: para um borrow MUT, exigir `is_unique_at(spine, cell_of(spine, x))` (senão
ERRO exclusivo-XOR-shared: "cannot borrow `x` mutably — it is already aliased here"); exigir
`ref_target_outlives(spine, borrow_cell, referent_cell)` (lifetime-bound). Estender o `bf_transfer`
(`spine.tks:1268,1352,1384`) pra semear `bf(y) := BfLocal(name_of(x))` no sítio sintático do `&x`
(a relaxação L2a já legislada). Relaxar `type_contains_ref`/o escape-gate (`type.tks:169`) pra
ADMITIR um `Reference` num ref-bind LOCAL quando o spine prova outlives+unique — o fix do
sink-to-value. **Prova de não-quebra (§4.1 doc-mãe):** os casos que hoje dependem da cópia são
LEITURAS (valor copiado por design); a preservação de alias só afeta o caminho mut-exclusivo, que
HOJE nem existe como borrow (mutação é sempre value-thread `x=push(x)`) ⇒ nenhum caso existente
depende da cópia num borrow mutável.

**F1.4 — Codegen.** Braço `Borrow as b =>` em `emit_expr_ctx`: emitir `&` + `emit_expr(b.operand)`
— idêntico ao `&` já emitido no auto-ref (`codegen.tks:2524`). O ref-bind local `mut y = &x` lowera
`y` como `<T> *` (o `Reference` já lowera assim, `codegen.tks:1000,1314`) inicializado com `&x`; um
uso `y.value`/write-through já lowera via o caminho RefDeref existente (`codegen.tks:2584,5832`).
Zero rep novo.

**F1.5 — Ponte de coexistência** (só ENTREGA a capacidade; AL3 muta in-place, F3 dá o `cap`). Nome
NOVO (`grow`), NÃO overload de `push` — evita ambiguidade de resolução e mantém `push` value-form
byte-idêntico (§6 recomenda nome novo). Corpo honesto que COMPILA HOJE, write-through preservando
semântica de valor (o `cap`-no-objeto vem em F3/AL3; aqui a ponte só troca a assinatura):

```teko
/**
 * Grow `x` by appending `v`, mutating `x` IN-PLACE through the exclusive mutable borrow `&x` (F1
 * bridge). This is the coexistence signature for the ref-push migration (AL3): it lives ALONGSIDE
 * the untouched value-thread `push(xs, v) -> []T` so no unmigrated site changes. F1's body writes
 * through the reference (`x.value = push(x.value, v)`) — behavior-identical to the value form; the
 * cap-in-object win that removes the global tk_push_cache lands with F3+AL3, not here. Migration is
 * gradual, fixpoint-green per sub-lote (§6); a final rename to `push` is optional once all sites move.
 *
 * @param x  the target slice, borrowed mutably and exclusively (`&x`) — spine `is_unique_at` proven
 * @param v  the element to append
 * @return   void — the mutation is the effect; nothing to re-assign (kills the `x = push(x)` thread)
 * @throws   panic if cap overflows u64 (M.1 fail-loud) — reached only once F3's cap-doubling lands
 * @since 0.x (#AL/F1.5 bridge; in-place cap = AL3)
 */
pub fn grow[T](x: &[]T, v: T) -> void
```

**F1.6 — Fixtures + ritual.** `.tkt` colocados (o padrão do repo: `src/<mod>/<mod>_test.tkt`, testes
Teko com assert), MAIS 1–2 programas end-to-end rodados VM e native pra paridade de exit code:

| Fixture | Onde | Entrada | Esperado (VM==native) |
|---|---|---|---|
| borrow-parse | `src/parser/parser_test.tkt` | `&x` prefixo → `Borrow{Var}`; `a & b` → `Binary` | AST correta; exit 0 |
| borrow-mut-ok | `src/checker/checker_test.tkt` | `mut x=…; grow(&x, v)` | tipa `Reference`; exit 0 |
| borrow-let-reject | `src/checker/checker_test.tkt` | `let x=…; grow(&x, v)` | erro "immutable"; exit ≠0 (gate rejeita) |
| borrow-alias-reject | `src/checker/spine_test.tkt` | dois borrows mut vivos de `x` | `is_unique_at`=false → erro exclusivo-XOR |
| borrow-outlives | `src/checker/spine_test.tkt` | `mut y=&x` sink local | `ref_target_outlives`=true; escape-gate admite |
| borrow-codegen | `src/codegen/codegen_test.tkt` | `grow(&x, v)` | emite `&x`; `<T> *`; diff VM==native byte-idêntico |
| e2e-noop | programa end-to-end | corpus que NÃO usa `&x` | **fixpoint gen2==gen3 INALTERADO** |

**RITUAL de F1 (o que prova aditividade):** (1) fixpoint gen2==gen3 verde e INALTERADO — nada no
corpus usa `&x`, então o C emitido pro alvo é byte-idêntico; (2) golden do corpus-alvo + diff
VM==native; (3) suites `_test.tkt` novas verdes. NÃO deve haver mudança em bytes emitidos pro alvo
(F1 é aditivo); qualquer diff no golden do alvo é REGRESSÃO, não esperado.

**Risco/tensão:** nenhuma tensão de lei (carve-out `ref` já legislado; `*` não tocado). Risco único
= o escape-gate relaxado (F1.3) admitir um `Reference` que dangla — MITIGADO por gate: o sink local
só passa quando `ref_target_outlives` E `is_unique_at` provam; a máquina de prova já existe (#331).
`&x` em posição de argumento (F1.2) é estritamente o auto-ref de hoje tornado explícito ⇒ risco ~0.

## THROUGHPUT (consome a fundação) — a rascunhar

- **AL3** — **A CURA, o lever global**: ref-push `push(&x,v)`. A prova mostrou copy-grow
  DISTRIBUÍDO (inline_rw_block 117MB, type_param_table 108MB, resolve_type 71MB, cg_lift_block
  59MB, mono_block 58MB, type_block 56MB, cb_byte 43MB, cg_name_reaches_byvalue 41MB) — sem
  vilão único, logo o fix é WHOLESALE (migração de todo push-site), não pontual. Ponte de
  nome novo p/ migração gradual (fixpoint verde a cada sub-lote) · L · preserva-alvo/muda-C
- **AL4b** — str-builder "stream-não-concat": o resto dos 15,5M buffers de str/format
  (`cg_format_c` 1,6M, `member_key`) escreve fragmentos num writer/stream e materializa 1× do
  tamanho final conhecido (alloc única), NÃO concatena em memória (concat realoca como push).
  As entradas "2 allocs" grandes (`tk_emit_c_mode`, `run_native_gate`) JÁ são o padrão bom —
  não mexer. Absorve o antigo "writers nativos" (EmitWriter = `[chunk]byte` em stack, F3) · M
  · preserva
- **AL5** — **ELEVADO pela prova** (reclaim 0%, 1,7 GB de root nunca liberado): region-per-
  phase (arena por fase, bulk-drop no fim). Primitivas já existem (`teko_rt.h:148-152`) · M/L
  · preserva

## MIGRAÇÃO — a rascunhar

- **AL6** — migração dos push-sites dinâmicos restantes (itens→literal, tamanho→`[N]T`,
  dinâmico→ref-push) via ponte de coexistência · M mecânico · preserva-alvo/muda-C

> AL2 (endurecer push-cache) foi DESCARTADO pós-prova: AL3 remove o cache global de vez e os
> grows >1MB já são saudáveis (eviction size-aware). Rascunho completo de F1-AL6 conforme
> cada um entra em execução; a prova (`al1-proof-report.md`) já os calibrou.

## Prospecção (PARKADO — design próprio depois da AL)
Unificar a superfície de referência em `&`/`*` (`mut *x: T ≡ mut x: Reference<T>`,
aposentar keyword `ref`, tirar `Ref<T>` da superfície) — ligado ao "repensar `Ref<T>`".
Toca a lei "sigils unsafe-only"; keystone. Ver `al-wave-emit-throughput.md` §12.
