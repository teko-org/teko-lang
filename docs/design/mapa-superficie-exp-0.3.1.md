---
section: design
version: 0.3.1.0-mapa-superficie-exp
created: 2026-08-18
status: DESIGN — blueprint que o passe de visibilidade (pub→exp) segue. Read-only sobre código de
        produto (.tks/.tkt); este arquivo é a ÚNICA edição. SEM build, SEM reseed, `teko test` NÃO
        rodado. Trabalho em worktree isolada off `origin/fix/retirement` (HEAD 266c8ca7).
role: architect — destilar a LEI de visibilidade (CLAUDE.md "Estilo de código"; TEKO_LEGISLATION.md:303
      "Visibility — pub vs exp"; tast.tks M.4) num mapa alvo por decl, cobrindo os 2252 decls de src/**,
      com foco na superfície ABI de macro/comptime.
seals-consumed: LEGISLATION:303 (pub=projeto-interno / exp=.tkh) · tast.tks:249 (só exp alcança o .tkh) ·
      serial-tags-comptime-field-reflection-0.3.1.md (FieldInfo/@fields/synthesis) · plano-macro.md
      (two-family SEAL: Family A `macro` / Family B `comptime`; Node reflection tiers) · header.tks
      (build_header — o construtor do .tkh e seus honest-stops).
---

# Mapa da superfície `exp` — o blueprint do passe pub→exp

Este doc é o **alvo por declaração** que o implementador do passe de visibilidade aplica. Ele NÃO promove
nada — só LÊ e classifica. O passe subsequente (agente separado, coordenador drena) troca a palavra-chave
`pub`→`exp` (ou `pub`→privado) exatamente onde este mapa manda, e SÓ DEPOIS vem o passe de doc-comments
(lei: doc só onde há `exp`).

## 0. Fatos de mecanismo (medidos nesta árvore — restringem o mapa)

Antes de qualquer julgamento, quatro fatos do compilador que **mudam o que "exp" significa na prática** e
que a lei textual não deixa óbvio. Todos verificados no código, não inferidos.

1. **`exp` vs `pub` são idênticos para acesso cross-namespace DENTRO do projeto.** `check_modules.tks:164`
   barra só `Private` no cruzamento de namespaces; `pub` e `exp` passam igual. A ÚNICA diferença semântica
   entre `pub` e `exp` é: **`exp` alcança o `.tkh`** (a ABI pública de um pacote), `pub` não
   (`header.tks:139/148` filtram `vis == Exp`; `tast.tks:249` M.4).

2. **O `.tkh` só é emitido para `Artifact::Package`** (`project.tks:2701-2711` chama `emit::emit_program`
   apenas nesse ramo; `Static`/binário nunca chamam). **O compilador é um binário** → o self-build/fixpoint
   NUNCA constrói um `.tkh` de `src/**`. Consequência crítica: **trocar `pub`→`exp` em `src/**` é inerte
   para o teko.c e para o fixpoint do próprio compilador** — o efeito só aparece quando a stdlib for
   empacotada como biblioteca (`.tkl`).

3. **Codegen só ramifica em `vis` no reverse-FFI** (`ffi_export.tks:176/207/272/352`, ramo `abi="c"`).
   Fora de um artefato `abi="c"`, o C emitido é **byte-idêntico** para `pub` e `exp`. Logo o passe pub→exp,
   sendo troca de token na mesma linha (não desloca linhas), **provavelmente não altera o teko.c** — o
   coordenador confirma via fixpoint e só reseeda se houver delta (§9).

4. **O construtor do `.tkh` HOJE só sabe exportar `struct`/`enum`/`variant`/`class`(como struct).**
   `build_tyexport` (`header.tks:49-128`) tem **honest-stop (erro)** para `alias`, `newtype`, `extern`,
   `extern struct`, `flags`, `interface` e `trait`. Isso NÃO afeta o fixpoint (fato 2), mas **bloqueia o
   empacotamento como biblioteca** de qualquer namespace cuja superfície `exp` inclua interface/trait/alias
   (io `Reader`/`Writer`, iter `Iterator`, os aliases `func<…>` `IntIter`/`ByteIter`/`StrIter`, cmp
   `IEq`/`IOrd`/`IHash` + traits). Ver §8 (dependência reportada para cima).

**Regra de ouro derivada:** promover `pub`→`exp` em `src/**` é **seguro e barato agora** (não quebra
fixpoint); o "custo" é 100% adiado para o dia do empacotamento-biblioteca, e esse custo é a extensão do
`build_tyexport` (interface/trait/alias) — um item de infra, não uma lei. Portanto o mapa classifica
**semanticamente** (o que É superfície consumível), sem se autolimitar pelas lacunas do header.

## 1. A regra de decisão (operacionalizada)

Para CADA decl `pub`/`exp` (e cada `exp` atual, para validar que merece continuar), aplique nesta ordem:

1. **É maquinaria do compilador (lex/parse/check/codegen/lir/backend/build/emit/names/lsp/journal/test)?**
   → default **`pub`** (ou privado se nem cross-namespace precisa). EXCEÇÃO única: o tipo/helper é
   **nomeado por código de usuário em compile-time** (macro/comptime) — então é `exp` (§5).
2. **É superfície de stdlib consumível?** → aplique o TESTE do dono:
   *"em que cenário concreto um usuário da lib se beneficiaria de chamá-la/nomeá-la?"*
   - **Sim, é uma operação/tipo que um programa de usuário chama ou constrói** (`sort`, `nth_i64`,
     `aes_gcm_seal`, `list::push`, `str::concat`, `json::decode`, `BigInt`, `Regex`, `Reader`) → **`exp`**.
   - **É plumbing interno que nenhum usuário chamaria** — passo de um algoritmo, estado de cursor/parser,
     número de syscall, nó de AST de um motor interno, construtor de erro que o usuário RECEBE mas não cria
     (`merge_ord`/`msort_ord`, `quickselect_*`, `CPScan`/`PState`, `SYS_WRITE`, `crypto_error`,
     `region_alloc`) → **`pub`** (ou privado).
3. **Empate genuíno** → marcar **AMBÍGUO** + a dúvida (§7). Não forçar.

Três heurísticas que resolvem 90% dos empates:
- **"Recebe vs constrói":** se o usuário só RECEBE o valor (um `error`, um handle opaco), o construtor é
  `pub`; se ele CONSTRÓI/passa o valor, é `exp`.
- **"Passo vs operação":** um passo nomeado de um algoritmo exposto (o merge de um sort, o sift de um heap)
  é `pub`; a operação inteira é `exp`.
- **"Tipo-nó de motor":** os nós de AST/estado de um parser/motor interno (regex, json parser, iterator
  cursor) são `pub`; o tipo-resultado e as funções de entrada/saída do motor são `exp`.

## 2. Eixo 1 — stdlib (default = `exp`)

Cada namespace: **default** + a **API `exp`** (enumerada) + as **exceções `pub`/privadas** (o plumbing).
Onde um arquivo é inteiramente API ou inteiramente helper, digo em uma linha. Contagens = decls fn/type/const
por arquivo (medidas).

### 2.1 Núcleo de valores — `str` / `list` / `runtime` / `fmt`-primitivo
- **`runtime/teko_rt.tks` (40, já `exp`):** MANTER `exp`. É a superfície `str_*`/`concat`/`fmt_*`/`panic*`/
  `div`/`mod`/`to_u8…` que todo programa usa. `panic*` são `exp` porque o codegen os referencia como alvo,
  mas também são a superfície de aborto — mantidos.
- **`list/list.tks` (1: `grow<T>`):** `grow` (append por `ref`, futuro `push`) → **`exp`** (AMBÍGUO leve —
  §7, renome pendente).
- **`runtime/arena.tks` (15), `runtime/sync.tks` (5), `runtime/thread.tks` (4):** **`pub`** (privado quando
  possível). Alocador de regiões/futex/thread-primitive — máquina de memória §16; o usuário nunca chama
  `region_alloc`/`region_enter`. NÃO promover.

### 2.2 Coleções — `collections/*` (65, hoje todo `pub`) → quase todo `exp`
- **API `exp`:** os tipos `List<T>`, `Map<V>`, `Dictionary<K,V>`, `HashSet<T>`, `PriorityQueue`,
  `SortedDictionary`, `SortedSet` e **todos os seus métodos públicos** (`len`/`is_empty`/`push`/`get`/`set`/
  `pop`/`remove_at`/`to_array`/`insert`/`contains`/`keys`/`remove`/`add`/…). Em `collections.tks`: os
  utilitários de array `arr_replace_at`/`arr_drop_*`/`arr_insert_at`/`arr_swap`/`arr_reverse`/`arr_slice` →
  `exp`.
- **Exceções `pub`:** `dict_find_index<K>` (passo de sondagem interno), `sorted_insert`/`heap_sift_up`/
  `heap_pop_min` (passos do PQ/sorted — plumbing "passo vs operação"). Manter `pub`.
- **Nota .tkh:** os tipos são `class` → exportáveis como struct (fato 4 OK). Métodos de classe NÃO alcançam
  o `.tkh` hoje (só campos), mas a `vis` do método governa a chamada cross-namespace — logo `exp` é o alvo
  semântico correto mesmo que o header ainda não os serialize.

### 2.3 Iteração — `iter/*` (48, todo `pub`) → API `exp`, cursores `pub`
- **API `exp`:** `Iterator<T>` (interface), os aliases `IntIter`/`ByteIter`/`StrIter`/`IntIndexedIter`/
  `IntPairIter` (`func<…>`), os structs de par `IntIndexed`/`IntPair`, e os combinadores/entradas
  `over_array`/`range`/`once`/`empty`/`map`/`filter`/`take`/`zip`/… e os terminais (`int_terminals`).
- **Exceções `pub`:** os **cursores de estado** `ArrayCursor`/`RangeCursor`/`OnceCursor`/`TakeCursor`/… —
  estado interno de iteração ("tipo-nó de motor"). Manter `pub`.
- **Bloqueio .tkh:** `Iterator` (interface) e os `func<…>` (alias) caem no honest-stop do header (fato 4) →
  §8.

### 2.4 IO / streams — `io/*` (39, todo `pub`) → API `exp`
- **API `exp`:** interfaces `Reader`/`Writer`/`Seeker`/`Closer`, `Buf`, `Whence`, as classes de stream
  (`NullStream`/`MemReader`/`MemWriter`/`LimitReader`/`BufReader`/`BufWriter`/`TeeWriter`) + métodos, e as
  funções livres `read_chunk`/`write_chunk`/`read_all`/`read_exact`/`write_all`/`copy`; `io.tks::append_file`.
- **Exceções `pub`:** helpers internos de `compress_stream`/`file_stream` que só existam como passo (avaliar
  caso a caso no passe — a maioria dos 39 é API).
- **Bloqueio .tkh:** as 4 interfaces (fato 4) → §8.

### 2.5 Matemática — `math/*` (187, todo `pub`) → `exp`
- **`math/math.tks` (27):** constantes `PI`/`TAU`/`E`/`SQRT2`/`LN2`/`LN10` e `inf`/`nan`/`is_nan`/`is_inf`/
  `is_finite`/`is_normal`/`abs_*`/`sign_*`/`copysign_f64`/`min_*`/`max_*`/`clamp_*`/`gcd`/`lcm`/`isqrt`/
  `ipow` → **`exp`**.
- **`math/checked.tks` (160):** `checked_add_u8`/`checked_sub_*`/`checked_mul_*`/`checked_div_*`/
  `checked_rem_*`/`checked_neg_*` por largura, retornando `T | error` → **`exp`**. É EXATAMENTE aritmética
  verificada que um usuário chama (TESTE do dono: cenário concreto claro). São a **superfície**, não os
  intrínsecos que o codegen emite inline. (AMBÍGUO fraco §7: se o dono considerar `math::checked` como
  suporte-de-codegen e não API, vira `pub` em bloco — recomendo `exp`.)
- **Nota consts .tkh:** `const` NÃO alcança o `.tkh` (build_header ignora `TConstDecl`, `header.tks:158`) —
  são inlinados por consteval. `exp const` é o marcador semântico correto; não muda bytes de header.

### 2.6 Cast seguro — `casting/casting.tks` (9, todo `pub`) → `exp`
- `u64_to_u32`/`i64_to_u32`/`u64_to_u8`/`u32_to_u8`/…/`u64_to_i32` — narrowing seguro `T | error`. Superfície
  de usuário → **`exp`** (todos os 9).

### 2.7 Tempo — `time/time.tks` (28, todo `pub`) → `exp`
- **API `exp`:** tipos `date`/`time`/`span`/`datetime`/`dto` + `date_from_days`/`time_from_ns`/
  `span_from_ns`/`datetime_of`/`dto_of`/`date_today`/`time_now_utc`/`datetime_now`/`dto_local_now`/
  `span_monotonic_now`/`datetime_add`/`datetime_sub`/`datetime_diff`/`span_add`/`span_sub`/`date_add_days`/
  `date_diff_days`/`timestamp_from_datetime`/`timestamp_to_datetime`/`date_to_string`/`time_to_string`.
- **`pub`/AMBÍGUO:** `CivilDate` + `civil_from_days` — algoritmo civil-from-days (Howard Hinnant); é passo
  interno de conversão. Recomendo **`pub`** (§7 se o dono quiser expor calendário civil).

### 2.8 Regex — `regex/regex.tks` (20, todo `pub`) → MIXED
- **API `exp`:** `Regex` (tipo-resultado), `compile`/`is_match`/`is_match_pattern`/`full_match`.
- **Exceções `pub`:** os nós de AST do motor `RChar`/`RAny`/`RRange`/`RClass`/`RConcat`/`RAlt`/`RStar`/
  `RPlus`/`ROpt`/`RGroup`/`RStart`/`REnd`/`REmpty`, e o estado `CPScan`/`PState`. "Tipo-nó de motor" →
  manter `pub` (≈15 dos 20).

### 2.9 Processo / threads — `process` (15) / `threads` (15)
- **`process/process.tks`:** **`exp`** — `Pipe`/`ProcHandle`, `pipe`/`spawn_redirected`/
  `spawn_redirected_fds`/`wait_one`/`close_fd`/`fd_wait_readable`/`fd_fill`/`read_to_eof`. **`pub`/privado:**
  `SPAWN_FAILED`/`NO_FD` (consts internos), e todo o subsistema `verdict_*`
  (`VERDICT_CHANNEL_ENV`/`verdict_channel_path`/`verdict_emit`) — é canal do harness de teste do compilador,
  não API de usuário. Manter `pub`.
- **`threads/threads.tks`:** **`exp`** — `Rx<T>`/`Tx<T>`/`Ctx`/`Closed`/`IChannelKind<T>` + métodos
  (`pop`/`send`/`close`/`add`/`done`/`wait`). API de concorrência de usuário. (`IChannelKind` interface →
  bloqueio .tkh, §8.)

### 2.10 Syscalls — `sys/sys.tks` (58) → **fica `pub` (0 exp)**
- TODOS os `const` são números de syscall / flags de OS (`SYS_EXIT`/`SYS_WRITE`/`SYS_MMAP`/`CLONE_*`/
  `FUTEX_*`/`PROT_*`/`MAP_*`/`PAGE_*`/`MEM_*`/`WIN_INFINITE`…) — plumbing raw do §16 que o runtime Teko usa.
  Nenhum usuário chama `SYS_WRITE`. **Manter `pub`.** (São `const`, não vão ao `.tkh` de qualquer forma.)

### 2.11 Texto / formatação — `text` / `fmt`
- **`text/text.tks` (2):** `valid_utf8`/`str_from_utf8` → **`exp`** (a única porta para `str`, B.36).
- **`fmt/fmt.tks` (2):** `format_source`/`run_cli` — ferramenta `teko fmt`. `run_cli` → **`pub`** (entrada de
  subcomando, não lib). `format_source` → **AMBÍGUO** (§7): formatar fonte Teko é plausível como API; lean
  `pub` (tooling).

### 2.12 Comparação — `cmp/*` (9)
- **API `exp`:** interfaces `IEq`/`IOrd`/`IHash`; traits `NeByEq`/`GtByLt`/`LeByLt`/`GeByLt`; chaves
  `StrKey`/`I64Key`. O usuário IMPLEMENTA/USA essas interfaces/traits e as chaves com `Dictionary`. → alvo
  **`exp`** (todos). Bloqueio .tkh: interface+trait (fato 4) → §8.

### 2.13 Numérico grande — `numeric/*` (53)
- **`bigint/bigint.tks` (19) + `bigint/bigint_ops.tks` (20, já exp):** `BigInt` + `zero`/`one`/`of`/`of_u64`/
  `from_str`/`of_lit`/`to_str`/`neg`/`abs`/`add`/`sub`/`mul`/`div`/`rem`/`cmp`/`eq`/… → **`exp`**.
- **`dec/dec.tks` (14):** `Decimal` + `normalize`/`of`/`of_u64`/`from_str`/`of_lit`/`to_str`/`neg`/`add`/
  `sub`/`mul`/`div`/`cmp`/`eq` → **`exp`**.

### 2.14 Compressão — `compress/*` (15)
- **API `exp`:** `crc32_of`/`adler32_of`, `ZipMethod`/`ZipEntry`, `read_zip`/`write_zip`/`write_zip_deflate`,
  e as fachadas `zlib`/`gzip` públicas.
- **Exceções `pub`:** internos de `deflate`/`inflate` que sejam passo do algoritmo (Huffman/janela) —
  avaliar no passe; o motor bruto é plumbing.

### 2.15 Encoding — `encoding/*` (243; metade já `exp`)
Padrão consistente: **os formatos textuais/binários são API `exp`; os structs de estado de parser são `pub`.**
- **JÁ `exp` (MANTER):** `asn1`, `bson`, `cbor`, `fixed`, `ini`, `mime`, `msgpack`, `protobuf`, `toml`,
  `yaml` (mais os `_encode`/`_decode`/`_parse` já exp). Validados — são superfície de serialização de usuário.
- **PROMOVER `pub`→`exp`:**
  - `json/json.tks` (13): `JsonNull`/`JsonBool`/`JsonNumber`/`JsonString`/`JsonArray`/`JsonObject`
    (+ o union `@JsonValue()`), `decode`/`encode`/`object_get`/`array_get` → **`exp`**. `PResult`/
    `PStringResult`/`UEscResult` (estado do parser) → **`pub`**.
  - `base64/base64.tks` (11): `standard_alphabet`/`url_safe_alphabet`/`url_safe_no_pad_alphabet`/`encode*`/
    `decode*` + `Base64Alphabet` → **`exp`**.
  - `url/url.tks` (9): `percent_encode`/`encode_form_component`/`encode_path`/`percent_decode`/`decode_path`/
    `decode_form_component`/`parse_form`/`encode_form` + `FormField` → **`exp`**.
  - `csv/csv.tks` (2): `write_csv`/`parse_csv` (+ `CsvRow`) → **`exp`**.
  - `xml/*` (mixed): a API pública já `exp`; os `pub` remanescentes de `xml`/`xml_c14n`/`yaml_parse` que são
    estado de parser → **`pub`**.

### 2.16 Ordenação — `sort/*` (26 exp + 8 pub)
- **JÁ `exp` (MANTER):** `sort_str`/`sort_str_natural`/`sort_i64`/`sort_u64`/`sort_f64`/`sort_bytes`;
  `search` (busca binária) e `select` (nth) exp.
- **CORREÇÃO (`pub`→`exp`):** **`sort<T: IOrd>` (`sort.tks:423`) está `pub` e DEVE ser `exp`** — é
  literalmente o exemplo do dono (`sort<T:IOrd>` → exp). Achado concreto de misclassificação.
- **Exceções `pub`:** `merge_ord`/`msort_ord` (passos do merge), `quickselect_*` (passo do nth), `sort/cmp.tks`
  helpers — plumbing "passo vs operação". Manter `pub`.

### 2.17 Cripto — `crypto/*` (224, já `exp`) → MANTER `exp`
- Toda a superfície (`hash`/`mac`/`kdf`/`cipher`/`aead`/`rsa`/`ec_p256`/`ed25519`/`des`/`tdes`/`argon2`/
  `scrypt`/`cose`/`jose`/`xmldsig`) é API de usuário — validada, MANTER. **Exceções `pub` (já):** os 4 `pub`
  de `crypto.tks` (dispatch interno) — manter `pub`. **NÃO expor** um eventual construtor `crypto_error`
  (regra "recebe vs constrói") — manter privado.

### 2.18 Assert — `assert/assert.tks` (24, já `exp`) → MANTER `exp`
- API de asserção de teste de usuário. Validada.

## 3. Eixo 2 — maquinaria do compilador (default `pub`/privado)

Regra de bloco: **estes namespaces ficam `pub` (ou privados)**, com a ÚNICA exceção do §5. Isso é cobertura
100% por regra — nenhum decl destes vira `exp` exceto os nomeados em §5.

| namespace | arquivos | decls (pub) | alvo | por quê |
|---|---|---|---|---|
| `lexer` | lexer, token | 4 | pub | tokens/scan — só o `parser` consome |
| `parser` | ast + parse_* + pattern/type | 131 | **pub** (exceto §5) | AST + parsing — interno; `Expr`/`Visibility`/`ExprKind` viram `exp` SÓ via §5 |
| `checker` | typer/resolve/tast/… | 286 | **pub** (exceto §5) | tipagem + TAST — interno; `Type`/`FieldView` viram `exp` SÓ via §5 |
| `codegen` | codegen, ffi_export | 13 | pub | emissão de C |
| `lir` | lir, lower, lir_print | 131 | pub | IR de baixo nível |
| `backend` | minst/isel/regalloc/objfile/dwarf/encode/abi | 270 | pub | backend nativo — o CI o exercita |
| `build` | project/tkr/regression/progress/… | 81 | pub | driver de build |
| `emit` | tkb_*/tkh/header | 4 | pub | codecs de artefato |
| `names` | names | 17 | pub | mangling |
| `journal` | journal, summary | 40 | pub | observabilidade de build |
| `lsp` | server/jsonrpc/symbols/diagnostics | 18 | pub | language server (ferramenta) |
| `test` | test | 10 | pub | harness de teste |

**`coverage/coverage.tks` (18, hoje `exp`) — RECLASSIFICAR para `pub` (AMBÍGUO forte, §7).** Toda função
recebe `checker::TProgram` (`cov_cobertura(prog: checker::TProgram)`, `functions_coverage`, `line_coverage`,
…) ou `SiteMap`. É **ferramenta interna** que opera sobre o IR do compilador — o `teko cov`/`teko test`. Estar
`exp` arrasta `checker::TProgram` e adjacências para a ABI sem cenário de usuário-de-lib. Recomendo **`pub`**
(mantendo `SiteMap`/`CovCount` como tipos de saída internos), a MENOS que o dono queira publicar
`teko::coverage` como biblioteca de análise — nesse caso `TProgram` entra na trilha do §5. É o principal
caso de "validar que um `exp` atual merece continuar exp": **este não merece**, salvo intenção explícita.

## 4. Resumo do Eixo 1 (o trabalho de promoção)

Promoções `pub`→`exp` estimadas por namespace (do passe):

| namespace | pub hoje | → exp | fica pub | nota |
|---|---|---|---|---|
| collections | 65 | ~60 | ~5 | heap/dict-probe ficam |
| iter | 48 | ~40 | ~8 | cursores ficam |
| io | 39 | ~35 | ~4 | |
| math (math+checked) | 187 | ~187 | 0 | checked = API |
| casting | 9 | 9 | 0 | |
| time | 28 | ~26 | ~2 | CivilDate ambíguo |
| regex | 20 | ~5 | ~15 | nós de motor ficam |
| process | 15 | ~10 | ~5 | verdict/consts ficam |
| threads | 15 | 15 | 0 | |
| text | 2 | 2 | 0 | |
| cmp | 9 | 9 | 0 | |
| numeric (bigint+dec) | 33 | 33 | 0 | |
| compress | 15 | ~13 | ~2 | |
| encoding (json/b64/url/csv/xml-rest) | ~47 | ~40 | ~7 | parser-state fica |
| sort | 4 (+1 sort) | ~1 | ~3 | `sort<T>` corrige p/ exp |
| list/runtime | ~1 | ~1 | 24 | arena/sync/thread ficam |
| sys | 58 | 0 | 58 | syscalls ficam pub |

**Total estimado:** ~**476 promoções `pub`→`exp`** no Eixo 1; ~**24 reclassificações `exp`→`pub`** (coverage,
mais eventuais `CivilDate`/`format_source`). Números finais saem do passe por-decl — este mapa é o alvo, não a
contagem exata.

## 5. A superfície ABI de macro/comptime (a parte crítica)

**Premissa da lei (dono 2026-08-18, HEAD 266c8ca7):** macro e comptime rodam em TEMPO DE COMPILAÇÃO; o
"usuário" inclui o autor de macro/comptime; **todo tipo/helper do compilador que o código macro/comptime de
usuário NOMEIA em compile-time precisa estar no `.tkh` → `exp`.**

### 5.1 O achado central (que delimita o alarme)

Tracei o que código de USUÁRIO em macro/comptime realmente alcança, contra o que é interno ao COMPILADOR:

- **Family A `macro` (`MacroDecl`, `ast.tks:969`)** expande AST antes do type-check via `lowering { … }` +
  `${param}`. No seed HOJE o mecanismo é só o **splice** (holes `__mhole_<i>`, `macro_expand.tks`): o corpo
  do usuário **não nomeia nenhum tipo do compilador** — ele escreve `lowering { … ${x} … }`. A **reflexão de
  nó** (`.source`/`.len`/`.kind`/`.lhs`/`.rhs`) é o `Node` handle DESIGN de `plano-macro.md` §A.1 (Tier-1
  recomendado como piso; Tier-2 gated) — **ainda não é superfície**.
- **Family B `comptime` (`ComptimeDecl`, `ast.tks:987`)** roda pós-type-check; args são valores tipados;
  resultado inlinado. `@sizeof<T>()`/`@typename<T>()` retornam **escalares** (`u64`/`str`) — **não expõem
  nenhum tipo do compilador**. `@fields<T>()` (reflexão de campo, Extent-3) retorna `[]FieldInfo` — **ainda
  não implementado** (design-ahead, `serial-tags-comptime-field-reflection-0.3.1.md`).
- **A pass de síntese** (`synthesize_serializers`) usa `TProgram`/`TFunction`/`Type`/`FieldView`/
  `resolve_type`/`deriver_field_view` **internamente** — o usuário NUNCA nomeia esses; são internos ao
  compilador. → **ficam `pub`.**

**Conclusão arquitetural (governa o passe):** a ABI de macro/comptime **NÃO é um export atacadista de
`parser::`/`checker::`.** É um **conjunto CURADO de handles de reflexão** que o design deliberadamente expõe.
Exportar `Expr`/`ExprKind`/`Type` crus seria violar M.0 (linguagem pequena) e congelar a AST inteira como ABI
permanente — o oposto do que `plano-macro.md` §A.1 recomenda (handle OPACO Tier-1). O passe **não deve**
promover a AST em bloco por causa de macro; deve promover **só a lista abaixo**.

### 5.2 A lista `exp` exigida por macro/comptime — por decl, com motivo e status

| decl (arquivo) | tipo | alvo | motivo | status |
|---|---|---|---|---|
| `parser::Visibility` (`ast.tks:607`) | enum | **`exp`** | `FieldInfo.vis: parser::Visibility` — o único tipo do compilador que a reflexão de campo (design) referencia transitivamente; enum → header OK (fato 4) | **PRONTO agora** (barato, enum) |
| `FieldInfo` (`collect`-adjacente, design) | struct | **`exp`** | retorno de `@fields<T>()`; o usuário itera `{name,tag,is_nullable,vis}` em comptime | **BLOQUEADO** (Extent-3 não implementado) — nasce `exp` |
| `JsonSpec` / tag-spec por formato (`encoding`, design) | struct | **`exp`** | retorno de `@parse_json_tag`/helpers comptime que o autor de formato chama | **BLOQUEADO** (comptime tag-parser) — nasce `exp` |
| `Node` (handle opaco, `plano-macro` §A.1 Tier-1) | tipo curado | **`exp`** | `.source`/`.len`/indexação que o corpo de `macro` usa; **NÃO** é `parser::Expr` cru | **BLOQUEADO** (Tier-1 não seedado) — nasce `exp` |
| `NodeKind` + acessores (`plano-macro` §A.1 Tier-2) | enum | **`exp`** | `.kind`/`.lhs`/`.rhs`/`.callee` para macros dirigidos por estrutura | **BLOQUEADO** (Tier-2 gated) — nasce `exp` |

**Explicitamente NÃO `exp` (ficam `pub`), apesar de "tocados em compile-time":**
- `parser::Expr` (`ast.tks:311`) e o union `@ExprKind()` + todos os nós (`Number`/`Var`/`Call`/`Binary`/…):
  o usuário toca via o **handle curado `Node`**, não o tipo cru. A AST crua permanece interna.
- `checker::Type`/`FieldView`/`TProgram`/`TFunction`/`resolve_type`/`deriver_field_view`: usados pela pass de
  síntese e pelo motor `eval_const` **internamente**; nunca nomeados pelo usuário. `@typename` devolve `str`,
  não `Type`.
- `MacroDecl`/`ComptimeDecl` (`ast.tks:969/987`): são a DECLARAÇÃO do facility no AST do compilador, não algo
  que o usuário nomeia. `pub`.

### 5.3 Ação imediata vs design-ahead

- **Fazer AGORA no passe:** promover **`parser::Visibility` → `exp`**. É a única peça pronta, é barata (enum,
  header-safe), e destrava o `FieldInfo` do serial-tags no minuto em que Extent-3 fechar.
- **Design-ahead (nasce `exp` quando implementado):** `FieldInfo`, `JsonSpec`/tag-specs, `Node`/`NodeKind`.
  O implementador desses (quando o facility comptime/reflection fechar) já os declara `exp` por default —
  este mapa é a autoridade. **NÃO** antecipar export de `Expr`/`Type` crus.

## 6. Achados de reclassificação (reportados, não viram issues)

1. **`sort<T: IOrd>` (`sort/sort.tks:423`) está `pub`; deve ser `exp`** — contradiz o exemplo canônico do
   dono. Corrigir no passe.
2. **`coverage/coverage.tks` inteiro está `exp`; recomendo `pub`** — ferramenta interna sobre `TProgram`
   (§3). Principal `exp` que NÃO merece continuar, salvo intenção de publicar `teko::coverage`.
3. **`encoding` é inconsistente hoje** — `asn1`/`bson`/`cbor`/`protobuf`/`yaml`/`toml`/… já `exp`, mas
   `json`/`base64`/`url`/`csv` (os MAIS usados) estão todo `pub`. O passe uniformiza para `exp`.
4. **Interfaces/traits/aliases-`func` marcados `exp` batem no honest-stop do `build_tyexport`** (io/iter/cmp/
   threads) — não quebra fixpoint (fato 2), mas bloqueia empacotamento-biblioteca → §8.

## 7. Ambíguos (para o dono — não forcei)

- **`math/checked.tks` (160):** API de aritmética verificada (recomendo `exp`) OU suporte-de-codegen
  (`pub`)? Peso do bloco é grande; recomendo `exp`, mas é a maior decisão única.
- **`coverage` (18):** `pub` (ferramenta interna, recomendado) OU `exp` (publicar `teko::coverage` como lib,
  arrastando `TProgram` para a trilha §5)?
- **`time::CivilDate`/`civil_from_days`:** passo interno (`pub`) OU calendário civil exposto (`exp`)?
- **`fmt::format_source`:** tooling (`pub`) OU API de formatação de fonte (`exp`)?
- **`list::grow<T>`:** helper `ref` de baixo nível em transição para `push` — `exp` (recomendado) ou `pub`
  até o renome?

## 8. Dependência reportada para cima (bloqueia empacotamento-biblioteca, NÃO o fixpoint)

Para a stdlib ser **empacotada como `.tkl`** com sua superfície `exp` correta, `build_tyexport`
(`emit/header.tks:49-128`) precisa **ganhar export de `interface`/`trait`/`alias`/`func`-alias** (hoje
honest-stops). Alvos afetados: io (`Reader`/`Writer`/`Seeker`/`Closer`), iter (`Iterator`, `IntIter`/
`ByteIter`/`StrIter` `func<…>`), cmp (`IEq`/`IOrd`/`IHash` + traits), threads (`IChannelKind`). **Isto NÃO
bloqueia** o passe pub→exp nem o fixpoint do compilador (o binário nunca emite `.tkh`) — é pré-requisito só do
dia do empacotamento. Reporto como achado adjacente (não crio issue).

## 9. Nota de reseed / ABI

- O passe pub→exp **muda a ABI conceitual** (`.tkh`/`.tkb` de um futuro pacote carregam `exp`+docs).
- Sobre o **teko.c / fixpoint do compilador**: codegen só ramifica em `vis` no reverse-FFI (`abi="c"`), e a
  troca `pub`→`exp` é na mesma linha (não desloca `file:line:col`). Logo o passe **provavelmente não altera o
  teko.c**. O coordenador confirma via fixpoint (tc2==tc3) e **reseeda só se houver delta** — decisão dele, no
  reseed único que agrega este passe + o passe de doc-comments (esse sim desloca linhas e exige reseed).
- Ordem obrigatória (lei): **visibilidade primeiro, doc-comments depois** — doc só onde há `exp`.
