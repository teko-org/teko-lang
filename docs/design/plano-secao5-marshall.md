# Plano — §5 Marshall: `ptr`/`uptr` opacos atômicos + `__wrap`/`__unwrap`

> **Status:** DESIGN. Read+design (nenhum código de produto, nenhum reseed/commit).
> **Correção de rumo do dono (2026-08-11):** o §5 NÃO é "completo agora". Vale a **REGRA DE FASE** —
> a SUPERFÍCIE (venha do doc1 ou doc2) é feita AGORA (doc2); o BACKEND dela fica PRONTO/ABERTO para o
> doc1 encaixar. O mecanismo de tag/liveness (side-table) **NÃO se decide agora** — é doc1.
> **Fatos do dono (travados):** `ptr` = UMA palavra do tamanho de `size`; `uptr` = UMA palavra do
> tamanho de `usize`. Opacos, **NÃO-fat** (nunca dois campos). Como `size`/`usize` (§8) ainda não
> existem, representa-se ptr/uptr como palavra de largura-de-ponteiro AGORA, alinhável ao futuro
> size/usize.
> **Fonte de lei:** `mudancas-superficie-0.3.1.md` §5 (SAI/ENTRA), §6 (aposenta `unsafe`), §8 (size/
> usize), §11 (superfície→reseed→backend), §12. **Backend:** `arena-especificacao-unica-0.3.1.md`
> (Doc 1). **Retirado:** `marshall-spec.md` (#498, banner já adicionado apontando o §5).
> **Branch:** `fix/retirement` @ 7947a692.

Este plano tem DUAS partes nítidas. **O implementador executa SÓ a PARTE A.** A PARTE B é o plano do
doc1 que preenche a costura que a Parte A deixa aberta.

---

## 0. O que o §5 pede (recap normativo)

- **SAI:** `ptr<T>`/`Uptr{}` genéricos + handling; aritmética de ponteiro (`p+1`, `p[0]`) REJEITADA.
- **ENTRA:** dois tipos opacos atômicos `ptr`/`uptr` (sem parâmetro, sem aritmética) + 4 métodos
  (assinaturas FINAIS, não mudam entre A e B):
  - `ptr::__unwrap<T>(ref T): ptr` / `uptr::__unwrap<T>(ref T): uptr` — **estáticos, infalíveis** (tomam
    o endereço da ref → devolvem a PALAVRA).
  - `ptr.__wrap<T>(): T | error | null` / `uptr.__wrap<T>(): T | error | null` — **instância, falíveis**
    (`null`=endereço 0; `error`=arena morta OU tag divergente; senão o `T`).

---

## 1. Blast radius MEDIDO (é o mesmo para A; B não toca aqui)

`ptr<` em 94 linhas, `uptr` em 85 — a maioria doc-comment/mangling/serialização. Genericidade `ptr<T>`
**realmente consumida** = 7 sítios, **TODOS internos** (arena/FFI/backend). **Zero superfície de
usuário** usa `ptr<T>`. `uptr` já É opaco (`Uptr = struct {}`).

| # | sítio | arquivo:linha | ação do sweep (Parte A) |
|---|---|---|---|
| 1 | `region_alloc` devolve `Ptr{inner=init.type}` | `checker/typer.tks:1025` | retorno → `Ptr{inner=null}` opaco (`init.type` fica só p/ `sizeof`) |
| 2 | campo `ptr: ptr<byte>` do `RawBuf` | `mem/unsafe/rawbuf.tks:21` | → `ptr` opaco |
| 3 | prims FFI `buf_ptr`/`bytes_from_ptr`/`as_ptr`/`as_cstr`/`str_from_cstr` | typer + `lir/lower.tks:4216..4515` + `codegen.tks:3834..3914` | assinam `ptr` opaco |
| 4 | cast-partner `ptr<byte> <-> u64` | `checker/typer.tks:1862..1940` | opaco; texto de erro |
| 5 | união de ausência extern `{null, ptr<T>}` | `checker/typer.tks:5469..5551` | forma opaca |
| 6 | serialização tag 12 = `ptr<T>` | `emit/tkb_write.tks:52` / `tkb_read.tks:169` | remover tag 12; sobram 10 (`ptr`) + 11 (`uptr`) |
| 7 | honest-stop de export `ptr<T>` | `codegen/ffi_export.tks:86,93` | texto p/ opaco |

O resultado de `region_alloc`/`buf_ptr` **flui para outra prim raw** (`bytes_from_ptr`), nunca é
value-recovered por deref → **nenhum sítio de `src/` chama `__wrap`/`__unwrap`** (decisivo p/ 1 reseed).

---

# PARTE A — SUPERFÍCIE (implementa AGORA — 1 reseed, ZERO runtime C)

## A.1 Representação de `ptr`/`uptr` (palavra atômica, não-fat)

- `ptr` = uma palavra do tamanho de `size`; `uptr` = uma palavra do tamanho de `usize`. **Não-fat**
  (só o endereço; nunca {addr, tag}). Como `size`/`usize` (§8) ainda não existem, a C-repr AGORA é a
  largura-de-ponteiro (`void *` para `ptr`, `uintptr_t` para `uptr`) — que já é o que `Ptr`/`Uptr`
  emitem hoje. **Nada muda na largura**; só se documenta a intenção (quando §8 aterrissar, a largura de
  `ptr` É `size` e a de `uptr` É `usize` — idênticas à palavra de ponteiro nos alvos 64-bit).
- No checker, `Ptr.inner` passa a ser **sempre `null`** (opaco). O campo permanece na struct só para o
  código de mangling/serialização existente distinguir; nenhum `Ptr{inner=T}` é mais produzido.

## A.2 As 4 assinaturas (Teko, full-Javadoc — o implementador adiciona verbatim)
```teko
/**
 * Constrói o ponteiro opaco a partir do ENDEREÇO de uma referência. Estática, infalível: toma o
 * endereço que a `ref T` já carrega (a `Reference{T}` é um `T *` nu) e o devolve como a PALAVRA
 * opaca `ptr` (largura de `size`). Devolve o PONTEIRO, não o valor — nada a checar.
 *
 * @param T r  a referência cujo endereço embrulhar (parâmetro `ref` — §3 mantém `ref` como parâmetro)
 * @return ptr  o endereço de `r` como palavra opaca
 * @since 0.3.1
 */
static fn ptr::__unwrap<T>(ref r: T): ptr

/**
 * Recupera o VALOR `T` por trás do ponteiro opaco. De instância, falível. O arm `null` (endereço 0)
 * está VIVO agora; o arm `error` (arena morta / tag divergente) é preenchido pela COSTURA de backend
 * (Doc 1, Parte B) — HOJE a costura só confia+reinterpreta, então `error` é inalcançável até o doc1
 * ligar tag+liveness. A assinatura `T | error | null` é FINAL e não muda quando o doc1 encaixar.
 *
 * @return T | error | null  o valor; `error` = arena morta ou tag divergente; `null` = endereço 0
 * @since 0.3.1
 */
fn ptr.__wrap<T>(): T | error | null
```
(`uptr::__unwrap<T>(ref T): uptr` e `uptr.__wrap<T>(): T | error | null` — idênticos, `uptr` no lugar
de `ptr`, palavra de largura `usize`.)

## A.3 Codegen — e a COSTURA que o doc1 vai ligar (o ponto central)

`ptr`/`uptr` são META-tipos (`Type` variants `Ptr`/`Uptr`), não classes `Named` → os métodos são
**intrínsecos reconhecidos no typer** (como `type_region_alloc`, `typer.tks:1010`). Sem gramática nova
(`p.__wrap<T>()` e `ptr::__unwrap<T>(x)` já parseiam como method-call / static-call genéricos).

**`__unwrap` (Parte A — reinterpret puro + COSTURA de track vazia):**
```
ptr::__unwrap<T>(ref r)  →  ({ void *__a = (void *)(<r>); /*SEAM-UNWRAP*/ __a; })
```
A costura `SEAM-UNWRAP` é **vazia na Parte A**. Doc1 a preenche com `tk_ptr_track(__a, <tag(T)>)`.

**`__wrap` (Parte A — null vivo + COSTURA de guarda constante-true):**
```
p.__wrap<T>()  →  ({ void *__a = (void *)(<p>);
                     __a == 0 ? <NULL-arm>
                              : ( /*SEAM-WRAP-GUARD*/1 ? <T-arm: *(T *)__a>
                                                       : <ERROR-arm> ); })
```
- `<NULL-arm>`/`<T-arm>`/`<ERROR-arm>` são construídos na representação-C da união `Variant{T,Error,Null}`
  já existente (a mesma maquinaria de `T | error | null` do checker/codegen). **O ERROR-arm é emitido e
  compila** (constrói um membro `error` da união), mas é **código morto na Parte A** porque a guarda é
  a constante `1`.
- A costura `SEAM-WRAP-GUARD` é o **literal `1`** na Parte A. Doc1 o troca por
  `tk_ptr_check(__a, <tag(T)>)` (a única edição no codegen do __wrap) — nesse momento o ERROR-arm passa
  a ser alcançável.

**Onde a costura mora (para o doc1 achar):** ambas as costuras ficam em UMA função de codegen cada, em
`src/codegen/codegen.tks`:
- `emit_ptr_unwrap_seam(buf: []byte, addr_c: str, tag_c: str): []byte` — **Parte A: retorna `buf`
  inalterado** (costura vazia). Doc1: emite `tk_ptr_track(<addr_c>, <tag_c>);`.
- `emit_ptr_wrap_guard(buf: []byte, addr_c: str, tag_c: str): []byte` — **Parte A: emite `1`**. Doc1:
  emite `tk_ptr_check(<addr_c>, <tag_c>)`.

Assim o doc1 troca DUAS linhas de corpo de função de codegen (e adiciona o runtime, Parte B) —
**nenhuma mudança de assinatura, de typer, de tipo ou de fixture** entre A e B. O `tag_c` (materialização
do tag de T) é passado às costuras já na Parte A **como string vazia / placeholder** (o typer já
resolve T concreto), para que a assinatura das costuras não mude no doc1; simplesmente é ignorado na
Parte A. *(Alternativa mais simples aceitável: `tag_c` só começa a ser computado no doc1; as costuras
recebem o `TExpr` do receptor/tipo e derivam o que precisarem — deixo à escolha do implementador do
doc1, pois não afeta a Parte A.)*

## A.4 Rejeição de aritmética
`type_binary`/`type_unary`/`type_index`: `p+n`/`p-n`/`p[n]`/`*p`/`&p` sobre `Ptr`/`Uptr` → erro claro
("`ptr` é opaco atômico: sem aritmética nem deref — use `__wrap<T>()`"). Ninguém usa hoje → puro ganho.

## A.5 Fns internas que o implementador ACRESCENTA / TOCA (Parte A)
- **Acrescenta (typer):** `type_ptr_unwrap(c, env, table): TExpr | NotPtrOp | error` (reconhece
  `ptr::__unwrap`/`uptr::__unwrap`, exige arg `ref T`/endereçável, retorna `Ptr{inner=null}`/`Uptr{}`);
  `type_ptr_wrap(mc, env, table): TExpr | NotPtrOp | error` (receptor `Ptr`/`Uptr` + `__wrap` +
  type-arg `T`, retorna `Variant{T,Error,Null}`).
- **Acrescenta (codegen):** `emit_ptr_unwrap` / `emit_ptr_wrap` (os snippets de A.3) + as duas costuras
  `emit_ptr_unwrap_seam` / `emit_ptr_wrap_guard`.
- **Toca (typer):** `type_region_alloc` retorno → `Ptr{inner=null}`; despacho de method-call (branch p/
  receptor `Ptr`/`Uptr` ANTES do path genérico de `type_method_call`); `type_call` (branch p/ path
  `ptr::`/`uptr::`); rejeição de aritmética.
- **Toca (checker):** `scope.tks:527-528,2442` (`ptr` sempre `inner=null`; **rejeitar** `ptr<...>`/
  `uptr<...>`); `resolve.tks:2226-2512` + `monomorph.tks:158-232` (colapsar arms de mangling `ptr<T>`);
  `emit/tkb_write.tks:52`+`tkb_read.tks:167-171` (remover tag 12).
- **Toca (stdlib/backend-lowering):** `mem/unsafe/rawbuf.tks:21`; `lir/lower.tks:4216-4515`;
  `codegen.tks:3834-3914`; `ffi_export.tks:86-93`; cast-partner `typer.tks:1862-1940`; união extern
  `typer.tks:5469-5551` (assinaturas/textos → `ptr` opaco).
- **NÃO toca:** `src/runtime/teko_rt.c` / `.h` / `.tks` — **ZERO runtime C na Parte A** (confirmado:
  `__unwrap` = reinterpret; `__wrap` = null-check + reinterpret + costura constante; nenhuma fn `tk_*`
  nova, nenhum símbolo de runtime referenciado).

## A.6 `__unwrap(ref T)` casa com o ref-param (pergunta 4) — SIM
`ref` como parâmetro sobrevive por §3. `Reference{T}` tem C-repr = `T *` nu (`type.tks:120`), então
`__unwrap` = reinterpret desse `T *` como palavra opaca. No call-site `ptr::__unwrap<Node>(ref node)`,
o `ref node` auto-refa um `var` para `Reference` (`typer.tks:1757`). **A confirmar (DECISÕES):** R7
ordinário vs operand-law do marshall antigo — recomendo **R7 ordinário**.

## A.7 Fixtures — Parte A (rodam AGORA)
**ACCEPT — oráculo nativo (exit = valor):**
| fixture | exercita | exit |
|---|---|---|
| `marshall_unwrap_wrap_roundtrip` | `ptr::__unwrap<i64>(ref x)` → `p.__wrap<i64>()` == x | valor de `x` |
| `marshall_uptr_unwrap_wrap_roundtrip` | idem com `uptr` | valor |
| `marshall_wrap_null_returns_null` | `__wrap<T>()` de `ptr` addr 0 → arm `null` | ramo null |
| `marshall_region_alloc_opaque` | `region_alloc` → `ptr` opaco flui p/ `bytes_from_ptr` | soma bytes |

**REJECT — `EXPECT_COMPILE_FAIL`:**
| fixture | rejeita |
|---|---|
| `marshall_ptr_generic_rejected` | anotar `ptr<int>` |
| `marshall_uptr_generic_rejected` | anotar `uptr<int>` |
| `marshall_ptr_add_rejected` | `p + 1` sobre `ptr` |
| `marshall_ptr_index_rejected` | `p[0]` sobre `ptr` |
| `marshall_ptr_deref_star_rejected` | `*p` / `&p` sobre `ptr` |
| `marshall_wrap_on_non_ptr_rejected` | `.__wrap<T>()` em receptor não-`ptr`/`uptr` |
| `marshall_unwrap_non_addressable_rejected` | `ptr::__unwrap<T>(<valor não-endereçável>)` |

*(Os fixtures do arm `error` — arena-morta e tag-divergente — são da Parte B; ver B.4. Na Parte A a
guarda é constante-true, então esses ramos são inalcançáveis e não têm oráculo.)*

## A.8 Sequência de crumbs + reseed (Parte A)
1 reseed, porque nenhum sítio de `src/` chama os intrínsecos e o sweep não injeta gramática nova em
`src/`.
- **A-C0 — retirar `marshall-spec.md`** (banner RETIRADO apontando §5). FEITO. Doc-only.
- **A-C1 — teach intrínsecos + costuras [ADITIVO, sem gramática, sem runtime C].** `type_ptr_unwrap`/
  `type_ptr_wrap`; `emit_ptr_unwrap`/`emit_ptr_wrap` com as costuras vazias/constante-true. **Ritual:
  gate cheio** (fixtures ACCEPT/REJECT de A.7 verdes).
- **A-C2 — rejeitar aritmética [ADITIVO].**
- **A-C3 — sweep interno [7 sítios].** `ptr<T>`→`ptr` opaco; **NÃO** insere `__wrap`.
- **A-C4 — des-ensinar `ptr<T>`/`uptr<T>` [REMOÇÃO].** `scope.tks:2442`; remover tag 12; colapsar
  mangling.
- **RITUAL FINAL — build seco + reseed manual (1x):** `bootstrap/teko.c`→binário; `TEKO_BACKEND=c
  binário build . --no-verify --release` → OUT/teko.c; **fixpoint byte-idêntico** (gate); sem testes.

**Confirmação:** Parte A = **1 reseed, ZERO toque no runtime C.**

---

# PARTE B — BACKEND (Doc 1, NÃO agora — preenche a costura da Parte A)

Documentação do que o doc1 encaixa. **Não implementar agora.** O mecanismo (side-table) fica proposto,
mas **a ratificação é do doc1** (o dono não decide o side-table agora).

## B.1 Onde a costura liga (o delta exato A→B)
O doc1 faz apenas isto no compilador:
- `emit_ptr_wrap_guard` passa a emitir `tk_ptr_check(<addr_c>, <tag_c>)` no lugar do literal `1` →
  o ERROR-arm da união (já emitido na Parte A) vira alcançável.
- `emit_ptr_unwrap_seam` passa a emitir `tk_ptr_track(<addr_c>, <tag_c>);`.
- `tag_c` = `checker::di_type_id(<símbolo canônico de T>)` como constante `u64` (comp-time).
**Nenhuma mudança de assinatura, typer, tipo ou fixture.** É o sentido da fase: a superfície já está
pronta; o doc1 só liga o backend por dentro das duas costuras + adiciona o runtime.

## B.2 O que o doc1 adiciona no runtime C (`teko_rt.c`/`.h`) — REUSA a Doc 1
A Doc 1 já provê as duas metades da liveness (nada a inventar):
- **`region->gen`** (`teko_rt.c:1250`, via `tk_region_gen_next` atômico) — stamp único por região,
  NUNCA reusado. Oráculo de liveness pronto.
- **`tk_g_regs`** — lista global de regiões vivas (desencadeada no `tk_region_drop` antes do `free`).
  "Existe região viva com gen G?" = varredura comparando `r->gen==G`, sem deref de região liberada.
- **`di_type_id(name): u64`** (`checker/di.tks:373`) — tag de tipo determinístico (FNV-1a), já a chave
  da DI. É o tag, materializado em comp-time.

Novo (mecanismo PROPOSTO, a ratificar no doc1): **side-table global `addr → {tag, gen}`** (opção (a)),
com as fns:
```c
tk_region *tk_region_of(const void *addr);          // contenção: a região viva que contém addr (ou NULL)
void tk_ptr_track(const void *addr, uint64_t tag);   // __unwrap: addr -> {tag, tk_region_of(addr)->gen}
int  tk_ptr_check(const void *addr, uint64_t tag);   // __wrap: 1=reinterpretar, 0=error(morta|tag)
```
Semântica de `tk_ptr_check`: miss → **estrangeiro-confiável** (ptr de FFI, sem metadado teko) → 1;
hit → se nenhuma região viva tem o `gen` guardado → **arena morta** → 0; se `tag` guardado ≠ `tag(T)`
→ 0; senão 1. (o `null`, addr==0, já foi resolvido no codegen antes de chamar.)

## B.3 Trade-off M.0 do side-table (a decidir NO doc1, não agora)
- **(a) side-table**, registrando só no `__unwrap`: entrega a semântica plena (null / error-morta /
  error-tag / estrangeiro-confiável-do-FFI). Custo: tabela lateral global (~1.5 MiB fixos) + spinlock
  entre tasks; sem limpeza no drop (a unicidade eterna de `gen` corrige entradas estale). Hot path do
  alocador intocado (não registra em `region_alloc`).
- **(b) tabela por-região** (reusa `entries`/`tk_region_register`): auto-liberada no drop (mais amiga
  do M.0), mas **não distingue arena-morta de ptr-estrangeiro** → semântica mais fraca.
- **(c) header em `addr-16`:** insound (memória do chunk reciclada). Rejeitada.
Recomendação para o doc1 avaliar: **(a) restrita ao `__unwrap`**. **Decisão do dono fica para o doc1.**

## B.4 Fixtures da Parte B (autorar agora, gate quando o doc1 ligar)
| fixture | exercita | bloqueio |
|---|---|---|
| `marshall_wrap_dead_arena_error` | `__unwrap` num valor de região-filha; dropa a região; `__wrap` → **error** | precisa `tk_ptr_check` + `gen`-liveness (doc1) |
| `marshall_wrap_tag_mismatch_error` | `__unwrap<A>` então `__wrap<B>` → **error** | precisa registro de tag (doc1) |
| `marshall_wrap_foreign_trusts` | `__wrap<T>()` de ptr NÃO-registrado (FFI) → confia, reinterpreta | idem (na Parte A já "confia" por vacuidade; vira teste real com (a)) |
Assinaturas/exit autorados agora; ativados quando `emit_ptr_wrap_guard`/`_seam` passarem a emitir as
chamadas de runtime.

## B.5 Adendo à Doc 1 (apontar)
A Doc 1 §7.2 especifica `region->gen` só p/ o `push_cache`. Adicionar à Doc 1 uma subseção declarando
que `gen`+`tk_g_regs` é também o oráculo de liveness do `__wrap`, `di_type_id` é o tag, e a side-table
é o único metadado lateral novo (fora do hot path). Aditivo; **nada muda de comportamento na arena**.

---

## 2. DELTA A vs B (o que o implementador da Parte A faz vs o que fica para o doc1)

| item | PARTE A (agora) | PARTE B (doc1) |
|---|---|---|
| tipos opacos `ptr`/`uptr` (palavra size/usize, não-fat) | **SIM** | — |
| 4 assinaturas finais `__unwrap`/`__wrap` | **SIM** (imutáveis) | — |
| `__wrap` arm `null` (addr==0) | **VIVO** | — |
| `__wrap` arm `error` (tag/liveness) | ERROR-arm emitido mas MORTO (guarda=`1`) | vira alcançável (guarda=`tk_ptr_check`) |
| `__unwrap` reinterpret endereço→palavra | **SIM** | — |
| `__unwrap` registro de tag/gen | costura VAZIA | vira `tk_ptr_track` |
| rejeição de aritmética | **SIM** | — |
| sweep 7 sítios `ptr<T>`→`ptr` | **SIM** | — |
| retirar `marshall-spec.md` | **FEITO** | — |
| runtime C (`tk_ptr_track`/`tk_ptr_check`/`tk_region_of`, side-table) | **ZERO** | **SIM** |
| decisão do mecanismo side-table (a/b) | não se decide | **ratifica** |
| reseed | **1** | **0 extra** (edição de fonte no mesmo build) |

**A COSTURA (o ponto que o doc1 liga):** as duas funções de codegen `emit_ptr_wrap_guard` (Parte A
emite `1`) e `emit_ptr_unwrap_seam` (Parte A não emite nada), em `src/codegen/codegen.tks`. O doc1
troca o corpo dessas duas funções por chamadas a `tk_ptr_check`/`tk_ptr_track` e adiciona o runtime.
Assinaturas, typer, tipos e fixtures de superfície ficam intocados.

---

## 3. DECISÕES PARA O DONO (só as da Parte A; o mecanismo é do doc1)
1. **Operand-law:** `ptr::__unwrap<T>(ref T)` usa R7 ordinário (auto-ref de `var`) ou herda a proibição
   de borrow-down do marshall antigo? Recomendo **R7 ordinário**.
2. **Placeholder do `tag_c` nas costuras:** passar `tag_c` já na Parte A (ignorado) para congelar a
   assinatura das costuras, ou o doc1 deriva o tag do `TExpr`? Recomendo **passar já** (costura estável).
3. **Confirmar Parte A = 1 reseed, ZERO runtime C.** (Confirmado neste plano.)
*(A decisão do MECANISMO de tag/liveness — side-table (a) vs (b) — é explicitamente do doc1, não agora.)*

---

## 4. Respostas diretas ao briefing
- **Parte A (agora):** tipos opacos palavra-size/usize; 4 assinaturas finais; `__wrap` null vivo +
  costura constante-true (error morto); `__unwrap` reinterpret + costura vazia; rejeição de aritmética;
  sweep dos 7 sítios; marshall-spec retirado. **1 reseed, zero runtime C** — confirmado.
- **Parte B (doc1):** side-table (a) com `di_type_id`+`region->gen`+`tk_g_regs`, fns `tk_ptr_track`/
  `tk_ptr_check`; documentada como o plano que preenche a costura.
- **Costura explícita:** codegen do `__wrap` chama `emit_ptr_wrap_guard` (emite `1` agora); codegen do
  `__unwrap` chama `emit_ptr_unwrap_seam` (vazia agora). Doc1 troca o corpo dessas duas por
  `tk_ptr_check`/`tk_ptr_track` + o runtime. Nada mais entre A e B muda.

---

*Fonte: `mudancas-superficie-0.3.1.md` §5/§6/§8/§11/§12. Backend: `arena-especificacao-unica-0.3.1.md`
(Doc 1). Retira: `marshall-spec.md` (#498). Read+design apenas — nenhum código de produto, nenhum
reseed/commit.*
