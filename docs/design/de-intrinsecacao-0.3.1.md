# De-intrinsecação — toda operação de MEMÓRIA vira função de superfície sobre a ARENA (0.3.1)

> **Status:** DESENHO / architect-first. ZERO código de produto. Base `fix/retirement` (tip com D201).
> **Fonte-lei:** DECISION_LOG **D201** (o princípio consolidado — fim dos intrínsecos; a arena é a DONA da
> memória), **D128** (arena 100% superfície/VIVA), **D148** (arena = Teko + codegen, ZERO adição de C),
> **D161** (toda fn tem corpo; nenhuma existe só na lógica do pipeline), **D187/D188** (zero-exceção-backend,
> lista FECHADA de builtin-legítimo), **D130** (modelo memória-por-escopo, região = parâmetro), **D199**
> (arco sem-união), **D189/D191** (censo de classificação + mecanismo de surfaciamento já provado), **D197**
> (`slice_view` = view real, reinterpret-class), **D68** (ratchet).
> Este doc **encoda** D201; não redelibera. É a **FUNDAÇÃO** que vem ANTES de retomar o arco sem-união
> (D199) — o `zero<T>` limpo (arena-superfície) é pré-requisito da tríplice/`null<T>`.

---

## 0. Leis reproduzidas (governam CADA crumb)

- **NO-PUSHES / 4-naturezas / purge-na-reatribuição** (CLAUDE.md). Nenhuma conversão pode introduzir array
  dinâmico, `push`, `grow_inplace` nem acumulador `x = [..x, y]`. A migração em massa de call-sites pré-aloca
  `[n]T` do tamanho contado.
- **D148 — ZERO adição de C.** Nenhum `teko_rt.{c,h}` novo, nenhum `tk_*` novo, nenhuma dep de libc. Os corpos
  de superfície nascem em `.tks` (`arena.tks` já é 100% Teko/VIVA); o codegen apenas EMITE a chamada compilada
  (rota-C) e o `lower.tks` emite via ABI/syscall/linker (rota-native).
- **D161/D187/D188 — toda fn tem corpo; caminho GENÉRICO nas 2 rotas.** Um builtin/name-detect/inline-synth do
  backend é ACOPLAMENTO a expurgar. Ao expurgar, a resposta NÃO é rotear o builtin pro símbolo de superfície
  num caso especial — é **DEIXAR DE SER builtin**: promover a `exp global fn`, e apagar os **três lados**
  (builtin-sig em `scope.tks`, name-detect no `codegen.tks`, dispatch no `lower.tks`) → a chamada cai no
  genérico. O `global` já é o roteador (bare + `teko::` D170/D180); não se constrói máquina nova.
- **Lista FECHADA de privilégio legítimo (D188):** (1) **pontos de arena** (região/DPS/injeção — bypass
  anti-overflow, a maquinaria de região não recursa pelo caminho normal); (2) **operador↔opcode**;
  (3) **reinterpret** (`wrap`/`unwrap` e a construção de header de slice — mesma-base, zero-custo);
  (4) **`syscall` raw**. **D201 acrescenta ao chão** apenas o par de **fatos-de-monomorfização**
  `sizeof<T>`/`alignof<T>` e confirma **load/store de memória** (`ar_load`/`ar_store`) como chão. Fora disso,
  TUDO que toca memória é **função de superfície sobre a arena**.
- **D197 — surfacear um bypass-de-memória PRESERVA a semântica.** Vários intrínsecos faziam um bypass
  (view/reinterpret/zero-cópia direto no codegen). Ao surfacear, a fn de substituição **preserva** a semântica
  de memória (via primitiva reinterpret declarada), **NUNCA** regride pra CÓPIA (com reclaim 0%, cada cópia
  VAZA — `slice_view` view→cópia custou +1815 MB no crumb 5 do str).
- **D68 — RATCHET (estrito).** O pico do build seco (`teko: memory: peak <N> MB`) só pode CAIR; flat = regressão.
  Como de-intrinsecação é remodelagem preservante, o piso é **NÃO-CRESCER**; cada crumb mede maçã-com-maçã.
  O ponto de risco é `zero<T>` de tipo-valor (§6.3) — desenhado memória-neutro (escreve no slot DPS, não aloca).
- **Gate por crumb de compiler-core (D164/D166/D185/D190/D191):** fixpoint gen2==gen3 byte-idêntico +
  gen0-do-seed-commitado builda o tip + ASan+UBSan limpo + os 3 harnesses C standalone (`scripts/*_test.sh`) +
  grep zero-ref do removido + varredura **árvore-inteira** (`src` + `cases` + `examples` + `tklib` + `tooling` +
  `main.tks`). O agente MATA o corpo morto na própria onda, gated pelo gate completo (D190).
- **ZERO TESTE (§9).** Nenhum crumb cria `.tkt`/`.tkr` — a prova é o self-build/fixpoint (o compilador USA
  cada operação de memória ao se compilar).

---

## 1. O PRINCÍPIO (D201) — encodado, não redeliberado

O probe (`.{}` vs `wrap<T>(0)`) + a leitura do `__wrap` revelou que TODOS os "intrínsecos" de memória
(`zero`/`of_len`/`slice_view`/`__wrap`/`__unwrap` + a família reinterpret + os alocantes de `mem::`) são
**name-detect por string no codegen/lower, sem corpo de superfície** — exatamente a "função que existe SÓ na
lógica do pipeline" que o **D161 proíbe**. O `__wrap` ainda é reconhecido por `mc.method == "__wrap"`
(`typer.tks:958`) + `emit_ptr_wrap` (`codegen.tks:3941`), e retorna a **união** `T | error | null`.

**A ARENA é a dona da memória** (D128, 100% superfície) e recebe a responsabilidade: **TUDO que trabalha com
memória — alocar, zerar, copiar, view, reinterpret, load/store — vira fn de SUPERFÍCIE sobre a arena,
recebendo a injeção de região** (`_tkrgn`/`cg_fn_needs_region`, D130, já parcial — §5). O backend deixa de
reconhecer-por-nome e a chamada cai no **caminho genérico** nas duas rotas, como faria com código de usuário.

O **chão irredutível mínimo** (declarado, reconhecido por **identidade** — NÃO name-detect-por-string) é a
única coisa que ainda bottom-out no codegen. Tudo o mais tem corpo de superfície.

---

## 2. CENSO — TODOS os intrínsecos / name-detects (grep-ancorado, `src/` real)

O dispatch de name-detect vive em **`codegen.tks` emit_call_inner** (`4257`-`4401`, um if-chain gigante de
`if l == "…"`), espelhado por **`lower.tks`** (`is_*_call`/`lower_*`, `1269`-`1857`, `1752`+) e pelas
**assinaturas-builtin** de **`scope.tks` `builtin_fn`** (`480`-`597`). Três lados, sempre.

### 2.1 Intrínsecos de MEMÓRIA (o alvo de D201) — viram fn de superfície sobre a arena

| # | intrínseco | como é reconhecido (src real) | o que faz | classe | vira |
|---|---|---|---|---|---|
| 1 | **`zero<T>`** | `call_is_zero_builtin` typer.tks:795; node `TZeroOf` tast.tks:51; `emit_zero_of` codegen:4742 (`(T){0}`); `lower_zero_of`/`_fat` lower:5776/5731 | valor todos-os-bits-zero de T | **mem (zerar)** | `exp global fn zero<T>(): T` (§4.1) — zera o slot DPS via `sizeof`+store; ZERO alloc pra valor |
| 2 | **`of_len<T>`** | `type_of_len_builtin` typer.tks:821; node `TSliceOfLen` tast.tks:42; lower `lower_slice_of_len`/`_fat` lower:5722/5708; scope builtin | `[]T` de `n` elementos memset-0 | **mem (alocar+zerar)** | `exp global fn of_len<T>(n: usize): []T` (§4.2) — `region.alloc(n*sizeof<T>)` + zero |
| 3 | **`slice_view`** | codegen:4350 `emit_slice_view`:3987; lower `is_slice_view_call`:1752/`lower_slice_view_call`; scope:526 | `{ptr+from, to-from}` de `[]T`, **zero-cópia** (D197) | **reinterpret (view)** | `exp global fn slice_view<T>([]T, usize, usize): []T` (§4.3) — header-reinterpret, **preserva view** |
| 4 | **`__wrap`/`ptr.wrap<T>`** | typer.tks:958 `type_ptr_wrap` (`mc.method=="__wrap"`); codegen:4261/`emit_ptr_wrap`:3941; `call_ns=="teko::__ptr_wrap"` | recupera T de word opaco; retorna **`T\|error\|null`** | **reinterpret + UNIÃO** | reinterpret-floor (identidade) **+ dobra em D199** (§4.4) |
| 5 | **`__unwrap`/`ptr.unwrap<T>`** | typer.tks:917 `type_ptr_unwrap` (`last=="__unwrap"`); codegen:4260/`emit_ptr_unwrap`:3918; `call_ns=="teko::__ptr_unwrap"` | endereço de um `var` como word opaco | **reinterpret** | reinterpret-floor (identidade, §4.4) |
| 6 | **`ref_word`** | typer.tks:940 `type_ref_word` (`last=="ref_word"`); codegen:4262/`emit_ref_word`:3934; `call_ns=="teko::__ref_word"` | `&var` como `i64` | **reinterpret** | colapsa em `unwrap` (§4.4) |
| 7 | **`ptr_word`** | codegen:4352/`emit_ptr_word`:4016; scope:511 | `ptr → i64` (bitcast) | **reinterpret** | colapsa em `unwrap` (§4.4) |
| 8 | **`word_ptr`** | codegen:4353/`emit_word_ptr`:4022; scope:515 | `i64 → ptr` (bitcast) | **reinterpret** | colapsa em `wrap` (§4.4) |
| 9 | **`str`/`str_of_bytes`** | codegen:4313; scope:531 | `[]byte → str` (mesma rep) | **reinterpret** | `wrap`/`unwrap` str↔[]byte (§4.4) |
| 10 | **`byte_ptr`** | codegen:4347/`emit_byte_ptr`:3520; scope:569 | `&slice[i]` (ptr aritmético) | **reinterpret** | `unwrap` base + offset (§4.4) |
| 11 | **`bytes_from_ptr`** | codegen:4344 (`tk_bytes_from_ptr` host-ffi); scope:558 | `{ptr,len}` de ptr cru | **reinterpret (header)** | `slice_from_raw<T>` (§4.4) |
| 12 | **`buf_ptr`** | codegen:4346/`emit_buf_ptr`:3512; scope:568 | `region_alloc(n)` cru | **mem (alocar)** | `exp fn buf_ptr(r, n): ptr` = `region.alloc` (§4.5) |
| 13 | **`region_buf`** | codegen:4351/`emit_region_buf`:4003; scope:570 | alloc + copia str + NUL | **mem (alocar+copiar)** | `exp fn region_buf(...)` sobre `region.alloc`+`copy` (§4.5) |
| 14 | **`as_cstr`** | codegen:4348/`emit_as_cstr`:3965; scope:556 | alloc + copia + NUL-termina | **mem (alocar+copiar)** | `exp fn as_cstr(str): ptr` sobre arena (§4.5) |
| 15 | **`str_from_c`** | codegen:4349/`emit_str_from_c`:3975; scope:557 | alloc + copia até NUL | **mem (alocar+copiar)** | `exp fn str_from_c(...)` sobre arena (§4.5) |
| 16 | **`concat` (1-arg fold)** | codegen:4328/`emit_concat_fold`:4163; scope:538 | junta `[]str` num `str` | **mem (alocar+juntar)** | `exp fn concat([]str): str` sobre arena (§4.5) |
| 17 | **`teko::mem::place`** | typer.tks:853; `cg_is_mem_value_call`:3466; codegen:4267/`emit_place`:3476; lower:2146 | `region_alloc(sizeof T)` + store | **mem (alocar+store)** | `exp fn place<T>(r, T): *T` sobre arena+store (§4.5) |
| 18 | **`teko::mem::read`** | typer.tks:863; codegen:4268/`emit_read`:3496; lower:2146 | `*p` (deref) | **mem (load)** | chão load (§4.6) / `exp fn read<T>(*T): T` |
| 19 | **`teko::mem::write`** | typer.tks:871; codegen:4269/`emit_write`:3503; lower:2146 | `*p = v` (store) | **mem (store)** | chão store (§4.6) / `exp fn write<T>(*T, T)` |
| 20 | **`deep_copy`** | codegen:4363/`emit_deep_copy`:3882; scope:524/`deep_copy_signature`:473 | cópia profunda; retorna **`T\|error`** | **mem (copiar) + UNIÃO** | `exp fn deep_copy<T>` sobre arena **+ dobra em D199** (§4.5) |
| 21 | **`copy`** | JÁ superfície `arena.tks:1152` (`exp fn copy`) | `dst[at+i]=src[i]` in-place | **mem (copiar)** | JÁ-FEITO — modelo do que os demais viram |
| 22 | **`append_fo`** | codegen:4294; scope:486 | copy-grow de `[]byte` | **mem (copy-grow) — BANIDO** | **REMOVER** (NO-PUSHES; zero call-site vivo — confirmar por grep) |

### 2.2 Refcount / weak (arena-adjacente — D189 fork (d), entra junto)

| # | intrínseco | src real | classe | vira |
|---|---|---|---|---|
| 23 | **`retain`** | codegen:4360/`emit_retain`:3529 (**já chama** `wrap_retain` superfície); scope:519 | arena-adjacente | dropar name-detect; re-resolver bare→`teko::runtime` (§4.7) |
| 24 | **`release`** | codegen:4361/`emit_release`:3536 (**já chama** `wrap_release`); scope:519 | arena-adjacente | idem §4.7 |
| 25 | **`weak_get`** | codegen:4362/`emit_weak_get`:3560 (chama `wrap_weak_get` **+ constrói união** `T\|null`); scope:523/`weak_get_signature`:466 | arena-adjacente + UNIÃO | superfície **+ dobra em D199** (§4.7) |

### 2.3 CHÃO IRREDUTÍVEL — fica, reconhecido por IDENTIDADE (NÃO expurgar)

| chão | src real | por que é chão |
|---|---|---|
| **load/store** `load_u64`/`store_u64`/`load_u8`/`store_u8` | codegen:4354-4357/`emit_load_u64`:4028…; lower:1269-1272/1805+; scope:564-567; embrulhados por `ar_load`/`ar_store` (arena.tks:163/167) | acesso cru à memória — não há corpo de superfície abaixo |
| **`syscall0`-`syscall6`** | codegen:4381-4387; scope:499-505 | fronteira do SO, irredutível |
| **operador↔opcode** | `binop_c` (caminho default, sem entrada por-nome); `os`/`arch` compile-time | a primitiva ABI direta |
| **`sizeof<T>`/`alignof<T>`** | **NÃO EXISTE hoje** (só `sizeof(...)` emitido inline em `emit_array_lit`:4773 / `emit_place`:3489) | fato de monomorfização — **NET-NEW** (§4.0) |
| **reinterpret `wrap`/`unwrap` + `slice_from_raw`** | `__ptr_wrap`/`__ptr_unwrap` (D131 intrínseco declarado) | mesma-base, zero-custo; a única coisa entre um word e um T |

### 2.4 PONTOS DE ARENA — magic-bypass LEGÍTIMO (D187) — ficam como superfície + seam fixo

`region_alloc`/`region_new`/`region_new_sized`/`region_drop`/`region_drop_subtree`/`region_root`/
`region_current`/`region_enter`/`region_leave`/`arena_push`/`arena_pop`/`arena_commit`/`region_register`/
`region_lookup`/`alloc`/`set_ret_dest`/`ret_dest`/`free_block` — enum `CgArenaSym` (codegen.tks:119-155),
**já têm corpo de superfície em `arena.tks`** e o codegen os referencia por um **seam de macro fixo**
`TK_ARENA_*` (`cg_emit_arena_provider_ladder`:172, macro → `teko_teko__runtime__*`). Este é o bypass
legítimo: se `alloc` fosse pelo caminho genérico de injeção-de-região, **recursaria infinitamente** (alocar a
região exige uma região). **FICAM.** (`arena_push`/`pop`/`commit`/`task_reset` name-detectam em codegen:4393-4396,
mas roteiam pelo seam — é o mesmo bypass; não é alvo de D201.)

### 2.5 NÃO-memória — censados, OUTRAS ondas (fora do escopo D201; já no backlog D189)

Reportados para não perder o mapa, mas **não** são desta fundação: **error-factory** `err_loc`/`err_typed`
(codegen:4273/4283, scope:548/552 → fábrica `Err` de D199); **host-I/O** `var`/`chdir`/`cwd`/`set_var`/
`last_index_of`/`args`/`run`/`run_quiet`/`str_from_utf8`/`read_line`/`stdin_eof`/`read_stdin` (codegen:4336-4376,
§16 grupo B); **float/misc** `parse`/`fdiv`/`floor`/`f64_bits`/`f64_from_bits`/`version`/`peak_rss`
(codegen:4377-4399); **fmt_\*** alocantes (scope:571-594, **já surfaciados** por `is_runtime_alloc_builtin`
D191 — o modelo que este doc generaliza); **concorrência** `thread_clone`/`atomic_*` (codegen:4388-4392, D189
fork b); **unwind** `capture_panic`/`capture_longjmp` (codegen:4358-4359, D189 fork c); **assert** seeds
(codegen:4371, D189 fork e).

---

## 3. Classificação — a regra de decisão (por classe, não caso-a-caso)

Cada intrínseco cai em EXATAMENTE uma classe:

- **(a) operação de memória** (alocar/zerar/copiar/juntar/place) → **fn de superfície sobre a arena**, recebendo
  injeção de região; bottom-out só no chão. #1,2,12,13,14,15,16,17,20,21,22 + place.
- **(b) opcode** (operador→instrução) → fica como lowering de operador (caminho default, sem name-detect).
  Nenhum item de memória é (b).
- **(c) `syscall`** → primitiva de chão declarada. #syscall0-6. Fica.
- **(d) reinterpret** (`wrap`/`unwrap` + header-de-slice) → **chão reinterpret reconhecido por identidade**
  (D131/D188 caso 3), zero-cópia. #3,4,5,6,7,8,9,10,11. **`__wrap`/`weak_get`/`deep_copy` são também
  consumidores de UNIÃO** (`T|error|null` / `T|null` / `T|error`) → **dobram no arco sem-união (D199)** —
  a reinterpret fica, a forma-de-retorno-união é reformada junto (§4.4, §7).
- **(e) load/store** (`read`/`write`/`load_u*`/`store_u*`) → chão de memória declarado (`ar_load`/`ar_store`).

**O chão mínimo, medido no código, é EXATAMENTE:** load/store (#18,19 + load_u*/store_u*), `syscall`, opcode,
`sizeof`/`alignof` (a introduzir), reinterpret `wrap`/`unwrap`/`slice_from_raw`. **Nenhum intrínseco de memória
ficou sem redução a esse chão nem sem corpo de superfície** — não há fork aqui (§10).

---

## 4. O PLANO — cada memória-intrínseco vira arena-superfície (contratos que compilam)

Todos moram no prelúdio-runtime injetado (`arena.tks`/`teko::runtime`, provenance-base) e são chamados pelo
caminho GENÉRICO. A injeção de região existente (`cg_fn_needs_region`, §5) passa `_tkrgn` a quem aloca — o
MESMO mecanismo que já serve os `fmt_*` de D191.

### 4.0 O chão NET-NEW: `sizeof<T>` / `alignof<T>` (fato de monomorfização)

Os corpos de `zero`/`of_len`/`place` precisam do tamanho de `T` em bytes. Hoje o codegen emite `sizeof(...)` do
C **inline**; não há primitiva de superfície. Introduzir (reconhecida por IDENTIDADE, não string):

```teko
/**
 * sizeof — the byte size of `T`, resolved as a monomorphisation fact (an opcode-class floor, not a
 * name-detected call). Bottoms out to the concrete size the monomorphiser already knows.
 *
 * @param T  the type whose storage size is taken
 * @return   the size of `T` in bytes
 * @since 0.3.1
 */
exp global fn sizeof<T>(): usize

/**
 * alignof — the alignment of `T` in bytes, a monomorphisation fact (floor).
 *
 * @param T  the type whose alignment is taken
 * @return   the alignment of `T` in bytes
 * @since 0.3.1
 */
exp global fn alignof<T>(): usize
```

Reconhecimento pela **identidade da decl intrínseca** (como `wrap`/`unwrap` de D131) — o checker resolve o
type-arg, o codegen/lower emite a constante `sizeof(<ctype de T>)` / `_Alignof`. É o UM acréscimo ao chão que
D201 autoriza. NÃO é name-detect-por-string: é uma decl de identidade única no prelúdio.

### 4.1 `zero<T>(): T` — refaz o C0.1 como arena-superfície (o keystone de D201)

```teko
/**
 * zero — the all-bits-zero value of `T`: a null pointer for a reference-shaped `T`, an every-field-zero
 * slot for a value-shaped `T`. Writes into this call's DPS return slot; allocates nothing for a value type
 * (ratchet-neutral vs the old `(T){0}`). Bottoms out at `sizeof<T>` (floor) plus store-zero (floor).
 *
 * @param T  the type whose zero value is produced
 * @return   the zero value of `T`
 * @since 0.3.1
 */
exp global fn zero<T>(): T
```

Contrato: recebe a região/dest via injeção (é retorno-por-destino, DPS); zera `sizeof<T>()` bytes do slot de
retorno com `store` (chão); **NÃO aloca** um bloco novo (o dest já foi alocado pelo caller que recebe o valor)
— memória-neutro vs `(T){0}`. Referência: `sizeof=8`, zerado = ponteiro nulo (consistente). Valor: slot zerado
= zero-value. **Expurga:** `call_is_zero_builtin`/`type_zero_builtin`/`zero_builtin_element` (typer.tks:795-819),
o node `TZeroOf` (tast.tks:51 + os ~10 `match {TZeroOf=>}` em codegen/lower/escape/consteval/comptime/monomorph/
tkb), `emit_zero_of` (codegen:4742), `lower_zero_of`/`_fat` (lower:5776/5731). A chamada `zero<T>()` cai no
genérico.

> **Tensão de ratchet (mitigada, §6.3):** o corpo tem que zerar o **slot DPS**, nunca alocar. Se o implementer
> não conseguir endereçar o slot DPS em Teko puro sem uma nova primitiva, isso NÃO é fork novo — `ret_dest()`
> (arena.tks:1010) já expõe o destino de retorno; o corpo escreve ali. Medir byte-diff e pico por camada.

### 4.2 `of_len<T>(n: usize): []T` — alocante zerado

```teko
/**
 * of_len — a fresh `[]T` of `n` zeroed elements, allocated in the caller's region. The `count`/watermark
 * idiom fills `[0..count)` and slices; reads of an unfilled value slot return the zero value. Bottoms out at
 * `sizeof<T>` (floor) + `region.alloc` (arena point) + store-zero (floor).
 *
 * @param n  the element count
 * @return   a zeroed `[]T` of length `n`
 * @since 0.3.1
 */
exp global fn of_len<T>(n: usize): []T
```

Corpo: `var bytes = n * sizeof<T>(); var p = <região>.alloc(bytes)`; zera os bytes; devolve o header `[]T`
(`slice_from_raw<T>(p, n)`, reinterpret). Memória-neutra (a alocação é a mesma que `[n]T` já faz). **Expurga:**
`type_of_len_builtin` (typer.tks:821), node `TSliceOfLen` (tast.tks:42 + os `match`), `lower_slice_of_len`/`_fat`
(lower:5722/5708), o sig em `scope.tks`. Nota: `[n]T = []` (sintaxe de tipo do array-fixo, CLAUDE.md) rebaixa
para `of_len<T>(n)` — a superfície de tipo permanece; só o intrínseco vira corpo.

### 4.3 `slice_view<T>([]T, usize, usize): []T` — VIEW, preserva zero-cópia (D197)

```teko
/**
 * slice_view — a `{ptr + from, to - from}` sub-view of a `[]T`, sharing the backing (zero-copy). A
 * reinterpret-class floor (D188 case 3, D197): it MUST stay a view — regressing to a copy leaks under
 * reclaim-0% (+1815 MB precedent). Bottoms out at pointer arithmetic + `slice_from_raw` (reinterpret).
 *
 * @param s     the source slice
 * @param from  the inclusive start index
 * @param to    the exclusive end index
 * @return      the sub-view, aliasing `s`
 * @since 0.3.1
 */
exp global fn slice_view<T>(s: []T, from: usize, to: usize): []T
```

Corpo: `slice_from_raw<T>(base_ptr(s) + from * sizeof<T>(), to - from)` — puro reinterpret, **sem alocar**.
**Expurga:** `emit_slice_view` (codegen:3987, o `if l=="slice_view"` em 4350), `is_slice_view_call`/
`lower_slice_view_call` (lower:1752/1765), o sig em scope:526. **Ressalva de aliasing (D197, in-plan):** a view
aliasa `s`; hoje (reclaim 0%) 100% segura; sob o modelo-por-escopo (Fase R do D199) a residência do resultado
amarra na de `s` (análise de aliasing-no-retorno, pós-este-doc).

### 4.4 Reinterpret — colapsar a família em `wrap`/`unwrap` + `slice_from_raw` (chão, identidade)

A família reinterpret (`ptr_word`/`word_ptr`/`ref_word`/`str`/`str_of_bytes`/`byte_ptr`/`bytes_from_ptr`) são
**especializações de um único chão reinterpret**. O plano (D189 "~12 a migrar pra wrap/unwrap"):

- **`wrap<T>`/`unwrap<T>`** (D131, já intrínseco declarado) — o bitcast escalar. Reconhecido por **identidade**
  (a decl `ptr.wrap`/`ptr::unwrap`), NÃO por `mc.method=="__wrap"` string. Fica no chão.
- **`slice_from_raw<T>(p: ptr, len: usize): []T`** — construção de header de slice `{ptr,len}` (reinterpret).
  **UMA** primitiva que subsume `bytes_from_ptr` (o caso `[]byte`) e a cauda de `slice_view`. Declarada por
  identidade.
- As demais colapsam em expressões: `ptr_word` = `unwrap` do ptr; `word_ptr` = `wrap` pra ptr; `ref_word` =
  `unwrap` do endereço; `str`/`str_of_bytes` = `wrap`/`unwrap` str↔[]byte (mesma rep `{ptr,len}`); `byte_ptr` =
  `unwrap` da base do slice + offset (`sizeof<byte>=1`).

**`__wrap` retorna UNIÃO (`T | error | null`)** (`type_ptr_wrap`, typer.tks:976 `Variant{members}`). D201:
"dobra na abolição de união (D199)". Logo o **reinterpret fica** (o bitcast é chão), mas a **forma de retorno**
(a união que codifica null-address/guard-fail/T) **é reformada junto com D199** — vira tríplice
`(bool, T, error)` ou T-cru-com-guard, conforme o arco sem-união decidir para o `__wrap`. Enquanto D199 não
land nesse elo, o `__wrap` mantém a união (dormante) — a de-intrinsecação **remove o name-detect-por-string** e
resolve por identidade, mas **não altera a assinatura de retorno** (isso é trabalho de D199, §7). Idem
`weak_get` (`T|null`) e `deep_copy` (`T|error`).

### 4.5 Alocantes/copiadores → corpo sobre `region.alloc` + `copy` (o modelo `arena.tks:1152`)

`buf_ptr`/`region_buf`/`as_cstr`/`str_from_c`/`concat`/`place`/`deep_copy` viram `exp fn` cujo corpo usa os
pontos de arena (`region.alloc`/`alloc`) + a `copy` que **já é superfície** (`arena.tks:1152`) + `sizeof`. Ex.:

```teko
/**
 * buf_ptr — reserve `n` raw bytes in the given region (the surface form of the old codegen name-detect).
 *
 * @param r  the target region
 * @param n  the byte count
 * @return   a raw pointer into `r`
 * @since 0.3.1
 */
exp fn buf_ptr(r: Region, n: usize): ptr { r.alloc(n) }

/**
 * as_cstr — a NUL-terminated copy of `s` in the caller's region (surface form; preserves the alloc+copy).
 *
 * @param s  the source string
 * @return   a raw pointer to the NUL-terminated bytes
 * @since 0.3.1
 */
exp fn as_cstr(s: str): ptr {
    var p = <região>.alloc(s.len + 1)
    teko::runtime::copy(<bytes de p>, 0, s.bytes())
    teko::mem::store_u8(unwrap(p) to u64 + s.len, 0)
    p
}
```

(Assinaturas exatas de `region_buf`/`str_from_c`/`concat`/`place`/`deep_copy` seguem os sigs atuais de
`scope.tks:299/557/538/853/473` — os corpos replicam o que os `emit_*` faziam, sobre arena, **preservando a
semântica de memória** — D197: `region_buf`/`as_cstr`/`str_from_c` COPIAM por natureza, então copiam mesmo;
`slice_view`/`byte_ptr` são VIEW, então NÃO copiam.) **`deep_copy` retorna `T|error`** → o corpo é superfície,
a forma-união dobra em D199. **`append_fo` NÃO ganha corpo — REMOVE** (banido NO-PUSHES; grep de call-site vivo
deve dar 0 antes de remover o sig/name-detect).

### 4.6 load/store (`read`/`write`) → chão declarado

`teko::mem::read<T>(*T): T` e `teko::mem::write<T>(*T, T)` são deref/store — o chão de memória. `read` = `load`,
`write` = `store` (com `sizeof<T>` para larguras > 8). Reconhecer por identidade (a decl de `mem::read`/`write`
no prelúdio), não pelo `cg_is_mem_value_call` string (codegen:3466) nem pelo `is_mem_value` de typer.tks:887.
**`place`** é (a)-memória (`alloc`+`store`), não chão — vira corpo (§4.5).

### 4.7 Refcount `retain`/`release`/`weak_get` (D189 fork d — entra junto)

`emit_retain`/`emit_release` (codegen:3529/3536) **já chamam** as superfícies `wrap_retain`/`wrap_release`
(arena.tks:1116/1125) — só falta **dropar o name-detect** e re-resolver o bare→`teko::runtime` (o mecanismo
D191, §5). `weak_get` idem (chama `wrap_weak_get`, arena.tks:1141) **mas constrói `T|null`** → superfície +
forma-união dobra em D199.

---

## 5. Injeção de região (D130) — o que já existe e o que a de-intrinsecação USA

**Pergunta (task item 5): de-intrinsecação precisa do D130 completo, ou usa o mecanismo parcial atual?**
**Resposta: usa o parcial. NÃO bloqueia no D130 completo.**

O que EXISTE hoje (D130 parcial):
- **`cg_fn_needs_region(prog, f)`** (codegen.tks:1373) — análise per-função: uma fn que aloca recebe o parâmetro
  de região `_tkrgn`. `cg_call_needs_region`/`cg_call_region`/`cg_frame_region`/`cg_alloc_region`
  (codegen:1378/7283/7298/7266) já threadam a região por chamada.
- **O precedente PROVADO (D191):** os `fmt_*`/str-number alocantes (`is_runtime_alloc_builtin`, scope.tks:607,
  consumido em typer.tks:2105) já foram surfaciados: o bare-name re-resolve para `resolved_ns="teko::runtime"`,
  e o **caminho genérico** emite o símbolo + injeta a região automaticamente (`call_wants_region`, codegen:4408).
  **Este é exatamente o mecanismo que os memória-intrínsecos usam.**
- **Fallback ambiente:** `cg_alloc_region`/`cg_call_region` caem em `region_current()` (arena.tks:822 →
  `ar_cur_current`, a pilha CTRL_CUR_STACK) quando não há região injetada. É o resquício ambiente que o D130
  COMPLETO elimina (`region_control(r)`, arena.tks:1026, é a ponte adicionada-sem-caller para o SWEEP).

**Conclusão:** os corpos de superfície de §4 são fns que **alocam** → `cg_fn_needs_region` os marca → recebem
`_tkrgn` pelo caminho genérico, exatamente como os `fmt_*`. **De-intrinsecação NÃO exige o SWEEP do D130** (o
flip onde o oráculo `residence_plan` dirige a emissão): funciona com a injeção parcial + fallback ambiente
atuais. É **memória-neutra** (não muda residência; só troca inline-emit por chamada-de-fn que aloca na MESMA
região que o inline usava — `cg_frame_region`/`cg_alloc_region`). O D130 completo (Fase R do D199) vem DEPOIS e
é ortogonal: quando o SWEEP land, os corpos já-superfície colhem o reclaim de graça.

---

## 6. Sequenciamento — de-intrinsecação é FUNDAÇÃO, ANTES do sem-união

### 6.1 A ordem (D201 é explícito)

```
DE-INTRINSECAÇÃO (este doc)
  └─ sizeof/alignof (chão) → zero<T> arena-superfície → of_len/slice_view/alocantes → reinterpret-colapso
                                                                                         │
                                                                                         ▼
ARCO SEM-UNIÃO (D199)  ── depende do `zero<T>` LIMPO (a tríplice usa zero<error>()/zero<T>())
  └─ Fase 0 (zero já limpo!) → null<T> → tríplice → … → Fase R (reclaim) → Fase III (AST→iface)
```

- **`zero<T>` arena-superfície é pré-requisito de D199.** O arco (§4.3/§4.4 de `arco-sem-uniao`) usa
  `zero<error>()` (slot 3 do sucesso) e `zero<T>()` (slot 2 da falha) em TODA a tríplice. D201: "de-intrinsecação
  vem ANTES de retomar o null/sem-união (que depende do `zero` limpo)". O C0.1 do arco (que landou como
  name-detect) é **refeito aqui** — quando D199 retomar, o `zero<T>` já é superfície.
- **Onde `wrap`/`unwrap`-união encaixa:** a de-intrinsecação **remove o name-detect-por-string** de
  `__wrap`/`__unwrap` (resolve por identidade) mas **NÃO mexe na assinatura-união** de `__wrap`/`weak_get`/
  `deep_copy` — isso é **elo de D199** (a união some no arco). Logo: de-intrinsecação entrega o reinterpret
  limpo-de-name-detect; D199 reforma a forma-de-retorno. Os dois se encontram no `__wrap` sem colisão (ordem:
  de-intrinsecação primeiro, D199 depois; ou o elo `__wrap`-união fica dormante até D199).

### 6.2 Byte-movers de risco

- **`zero<T>` (C-DI.2)** é o de maior risco: muda a emissão de TODO `zero`/campo-default/slot-de-união, e tem a
  tensão de ratchet (§6.3). Mitigação: predicado que reconhece ambos (node `TZeroOf` E chamada-de-superfície)
  antes do flip; medir byte-diff + pico por camada.
- **`of_len`/`[n]T`** toca todo array-fixo — volume alto, mas preservante (mesma alocação). Fixpoint por lote.
- Os reinterpret e alocantes são localizados (poucos call-sites cada; a família str↔bytes é a maior).

### 6.3 Custo de memória (D68) — o ÚNICO ponto de crescimento-risco

**`zero<T>` de tipo-valor.** O `(T){0}` atual é stack puro (zero bytes de heap). Um corpo de superfície que
**alocasse** `sizeof<T>` por chamada VAZARIA sob reclaim-0% (ratchet violado). **Mitigação de desenho (§4.1):**
o corpo escreve no **slot DPS de retorno** (que o caller já alocou), NÃO aloca bloco novo → **memória-neutro**.
Todos os demais (`of_len`/`slice_view`/alocantes) preservam a alocação que o inline já fazia (of_len já alocava;
slice_view NÃO aloca — view; as_cstr/region_buf já alocavam). **Nenhum crumb pode crescer o pico** — mede-se
maçã-com-maçã por lote; `zero<T>` mede DUAS vezes (o de valor e o de referência).

---

## 7. CRUMB-SEQUENCE — escalonado-verde (template str D192-D197)

Princípio: **construir o chão + o corpo novo (dormante) → coexistir → re-resolver (flip) → expurgar o
name-detect por último.** "Dormante" = carrega SEM reseed (gate D164 fecha; o reseed do FLIP absorve).
Gate por crumb = o gate completo de compiler-core (§0). **ZERO teste** (§9).

### C-DI.1 · `sizeof<T>`/`alignof<T>` — chão NET-NEW (aditivo, dormante)
Ensina a decl-intrínseca por identidade (typer resolve type-arg; codegen emite `sizeof(ctype)`; lower emite a
constante de monomorph). Nada consome ainda além do próprio self-build. **Sem reseed** se não muda C existente
(mede byte-diff=0). Risco: BAIXO.

### C-DI.2 · `zero<T>` arena-superfície (o keystone — refaz o C0.1)
`exp global fn zero<T>()` no prelúdio (corpo escreve o slot DPS via `sizeof`+store, §4.1); re-resolve o bare
`zero`/`teko::zero` para o corpo (mecanismo D191). Coexiste com `TZeroOf` (predicado reconhece ambos). **FLIP:**
`zero<T>()` deixa de virar `TZeroOf` e vira chamada genérica. **Expurga** `call_is_zero_builtin`/
`type_zero_builtin`/`TZeroOf`/`emit_zero_of`/`lower_zero_of` — o compilador ENUMERA o morto (D125/D181).
**Reseed.** Ritual + **medir pico 2×** (valor/referência). Risco: **ALTO** (byte-mover + ratchet).

### C-DI.3 · `of_len<T>` + `[n]T` → arena-superfície
`exp global fn of_len<T>(n)`; `[n]T = []` rebaixa para `of_len<T>(n)`. Coexiste (predicado reconhece
`TSliceOfLen` E chamada); FLIP; **expurga** `type_of_len_builtin`/`TSliceOfLen`/`lower_slice_of_len`. **Reseed.**
Ritual. Risco: MÉDIO-ALTO (volume de array-fixo).

### C-DI.4 · reinterpret-colapso — `slice_from_raw<T>` + `wrap`/`unwrap` por identidade
Introduz `slice_from_raw<T>` (chão reinterpret) e migra `bytes_from_ptr`/`ptr_word`/`word_ptr`/`ref_word`/`str`/
`str_of_bytes`/`byte_ptr` para expressões de `wrap`/`unwrap`/`slice_from_raw` (§4.4). Reconhecer `wrap`/`unwrap`
por **identidade** (não `mc.method=="__wrap"` string). **NÃO** mexe na assinatura-união de `__wrap` (isso é
D199). **Expurga** `emit_ptr_word`/`emit_word_ptr`/`emit_ref_word`/`emit_byte_ptr` + os sigs. **Reseed.**
Ritual. Risco: MÉDIO.

### C-DI.5 · `slice_view` + alocantes/copiadores → corpo sobre arena
`slice_view` (VIEW, §4.3), `buf_ptr`/`region_buf`/`as_cstr`/`str_from_c`/`concat`/`place` (§4.5). **D197: view
fica view, cópia fica cópia** — medir que `slice_view` NÃO vira cópia (loop de `.slice()` RSS flat). **Expurga**
os `emit_slice_view`/`emit_buf_ptr`/`emit_region_buf`/`emit_as_cstr`/`emit_str_from_c`/`emit_concat_fold`/
`emit_place` + `cg_is_mem_value_call` + `is_slice_view_call`/`lower_slice_view_call` + sigs. **Remove
`append_fo`** (grep call-site vivo = 0 antes). **Reseed.** Ritual + **medir pico** (`slice_view`). Risco:
MÉDIO-ALTO (ratchet do slice_view é o precedente D197).

### C-DI.6 · load/store (`read`/`write`) por identidade + refcount surfaciado
`mem::read`/`mem::write` reconhecidos por identidade (não `cg_is_mem_value_call` string); `retain`/`release`
(já chamam superfície — só dropar name-detect + re-resolver). **`weak_get`/`deep_copy`/`__wrap`-união ficam
DORMANTES** (a forma-união dobra em D199 — anotado, não forçado aqui). **Expurga** os name-detects de
read/write/retain/release. **Reseed.** Ritual. Risco: MÉDIO.

### C-DI.7 · EXPURGO final + varredura árvore-inteira
Remove os resíduos do if-chain de `emit_call_inner` (codegen:4257-4401) e das assinaturas-builtin de memória em
`scope.tks builtin_fn`; confirma o dispatch de `lower.tks` limpo. Grep zero-ref dos removidos na árvore INTEIRA
(`src`+`cases`+`examples`+`tklib`+`tooling`+`main.tks`). **Reseed final.** Ritual completo. Risco: MÉDIO.

**Total: 7 crumbs.** Reseeds/rituais em C-DI.2, C-DI.3, C-DI.4, C-DI.5, C-DI.6, C-DI.7 (C-DI.1 sem-reseed se
byte-diff=0). O elo `__wrap`/`weak_get`/`deep_copy`-união fica dormante para D199 — **de-intrinsecação NÃO
espera D199**; entrega o reinterpret limpo-de-name-detect e a fundação `zero<T>`.

---

## 8. Riscos + tensões de lei (com resolução recomendada)

- **R1 · `zero<T>` de valor vs ratchet (§6.3).** Tensão: `(T){0}` é stack; um corpo que aloca vaza. **Resolução:**
  escreve o slot DPS (`ret_dest`, arena.tks:1010), não aloca — memória-neutro. Medir 2×. NÃO é fork (D201 mandou
  arena-superfície; o DPS é a arena sem alocação nova).
- **R2 · reinterpret ainda é "chão", D201 lista só load/store/syscall/opcode/sizeof.** Tensão aparente:
  reinterpret não está na lista mínima textual de D201. **Resolução:** D188 caso 3 + D131 declaram reinterpret
  (`wrap`/`unwrap`) como intrínseco de **identidade** legítimo — é chão reconhecido por decl, não name-detect.
  D201 item 2 ("reinterpret") confirma que fica; a lista mínima nomeia os bottom-outs de EMISSÃO, e o
  reinterpret é um deles (bitcast). Sem tensão real.
- **R3 · `__wrap`-união × ordem com D199.** Tensão: `__wrap`/`weak_get`/`deep_copy` retornam união; D201 manda
  de-intrinsecar E dobrar em D199. **Resolução:** separar as duas responsabilidades — de-intrinsecação remove o
  name-detect-por-string (resolve por identidade) e **preserva** a assinatura-união dormante; D199 reforma a
  forma-de-retorno. Ordem: de-intrinsecação → D199. Sem colisão.
- **R4 · pontos de arena parecem name-detect (arena_push/pop/commit, codegen:4393).** Tensão: ainda são
  reconhecidos por string. **Resolução:** são o bypass-legítimo D187 (recursariam pelo caminho genérico) e já
  roteiam ao corpo de superfície pelo seam `TK_ARENA_*`. FICAM — fora do escopo D201 (que é memória-sobre-arena,
  não a arena-de-si-mesma). Não mexer.
- **R5 · D130 completo não é pré-requisito (§5).** Tensão possível: "arena-superfície precisa de região=param
  em toda parte". **Resolução:** a injeção parcial (`cg_fn_needs_region` + fallback `region_current`) já serve
  (precedente `fmt_*` D191). O SWEEP do D130 é ortogonal e posterior. Sem bloqueio.

---

## 9. Testes: ZERO (lei no-test endurecida — D199 §9, CLAUDE.md)

**Nenhum crumb cria `.tkt`/`.tkr`.** A prova é o self-build/fixpoint: o compilador **usa** `zero`/`of_len`/
`slice_view`/reinterpret/os alocantes ao se auto-compilar (a stdlib É o compilador) → qualquer bug de
parse/check/codegen/lower aparece sozinho no build. Os caminhos de rejeição (`zero<void>`, `of_len<Reference>`,
`__wrap<void>`) são **enumerados pelo próprio compilador** quando alguém os aciona — não por arquivo de teste.
**Ao drenar: RECUSAR qualquer teste novo no delta** — kill + re-limpa, não drena.

---

## 10. FORKS — nenhum aberto

Cada intrínseco de memória do censo (§2) reduziu-se ao chão OU ganhou corpo de superfície (§3-§4); o chão mínimo
foi medido e é fechado (§2.3); a dependência do D130 foi resolvida como parcial-basta (§5); a colisão `__wrap`×
D199 foi separada por ordem (§6.1/R3). **Nenhum "intrínseco" ficou sem virar superfície nem sem reduzir ao chão
→ não há HALT.**

**Se um crumb, EM OPERAÇÃO, encontrar um fork NOVO genuíno** (não coberto por D201 nem por este doc — ex.: um
alocante cujo corpo de superfície NÃO seja expressável sem uma primitiva de chão nova além de `sizeof`/
reinterpret/load-store), HALT curto pelo protocolo de fork — não inventar decisão. Neste desenho, nenhum foi
encontrado: os candidatos (endereçar o slot DPS de `zero`, construir header de slice) resolvem-se com
`ret_dest`/`slice_from_raw` já existentes ou reinterpret declarado.

---

## 11. Achados adjacentes — REPORTADOS (não viram issue)

- **`is_runtime_alloc_builtin` (scope.tks:607) é o molde pronto** do surfaciamento — os memória-intrínsecos
  entram na MESMA lista de re-resolução (ou uma irmã), sem máquina nova. Ganho: o mecanismo D191 já provou o
  caminho.
- **`retain`/`release` já chamam superfície** (`wrap_retain`/`wrap_release`) — o name-detect é puro resíduo;
  removê-los é quase-grátis.
- **`copy` (arena.tks:1152) já é o modelo** de alocante/copiador de superfície — os demais o espelham.
- **`region_control(r)` (arena.tks:1026) é a ponte D130 sem-caller** — quando o SWEEP land, os corpos
  já-superfície trocam `ar_control()` ambiente por `region_control(<região-param>)` de graça.
- **`append_fo` (scope.tks:486, codegen:4294)** é copy-grow banido (NO-PUSHES) — confirmar grep de call-site
  vivo = 0 e REMOVER, não surfacear.
- **`sizeof` inline** hoje aparece em `emit_array_lit` (codegen:4773) e `emit_place` (3489) — o `sizeof<T>` de
  superfície (§4.0) também limpa esses usos internos (passam a chamar a primitiva de identidade).
