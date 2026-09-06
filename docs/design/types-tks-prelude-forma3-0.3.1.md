# `types.tks` no prelúdio — todos os tipos como superfície Teko COM MÉTODOS (Forma-3)

Status: DESIGN (D145, dono 2026-08-28). Não implementa produto — fia crumbs.
Base verificada: `fix/retirement @ 57cdc2ef`.

Supersede o guard-rail "str/char pós-modelo" (D131) para os MÉTODOS: o dono puxou a
visão-futura D131 para agora. A representação de `str` (o builtin `Str`) NÃO muda nesta
onda — ver §6 (colapso `Str`→Named é o terminal owner-present, pós-modelo).

---

## 1. O que o dono pediu (D145, verbatim)

Um `types.tks` no prelúdio com TODOS os tipos como superfície Teko, COM MÉTODOS na
composição, para o dev usar `a.concat(b)` / `s.to_lower()` / `p.wrap<T>()` naturalmente
EM VEZ DE funções soltas de namespace (`teko::str::concat`) ou funções mágicas.
Injetado pelo embed (Forma-3 provenance por injeção). Fixpoint-safe: métodos coexistem/
delegam às impls atuais, call-sites migram por família, funções soltas saem no fim.

Refinamento do `char` (dono, mid-task): `char = u32` newtype (code point Unicode);
`char::from(src: str, enc: Encoding): []char` ESTÁTICO decodifica `str` (bytes crus) em
`[]char` conforme um encoding NOMEADO — SEM assumir UTF-8 escondido. Ver §4.3.

---

## 2. Estado real do `src/` (verificado, NÃO presumido)

| fato | onde | consequência |
|---|---|---|
| `str`/`char`/`byte` são Type-variants builtin (`Str{}`/`Char{}`/`Byte{}`) | `src/checker/type.tks:82-84` | NÃO são `Named` — método-call não acha método |
| `isize`/`usize` são `PrimKind` arch-parametrizados por `checker::Target`/`prim_width` | `src/checker/type.tks:2-8,67-75` | width arch já resolvida SEM `#if` de fonte |
| `builtin_type(name)` intercepta `str`/`ptr`/`isize`/… ANTES da tabela de tipos | `src/checker/scope.tks:242-266` | um `type str=…` no prelúdio coexiste mas NÃO shadowa o builtin |
| `ptr`/`uptr` JÁ são newtype de superfície no disco + builtin coexistente | `src/base/base.tks:7,15` + `scope.tks:263-264` | padrão de coexistência já provado |
| `NewtypeBody { backing, methods }` parseia `type str = []byte { métodos }` | `src/parser/parse_decl.tks:995-999` | newtype-sobre-`[]byte`-com-métodos PARSEIA |
| `type_method_call` só despacha método quando `recv_t` é `Named` | `src/checker/typer.tks:1624` | recv builtin (`Str`/`Prim`/`Char`) → `"method typing is deferred"` |
| `wrap`/`unwrap` já são intrínsecos (`__wrap`/`__unwrap`) landados | `src/checker/typer.tks:888,928` | métodos públicos `wrap`/`unwrap` só delegam ao intrínseco |
| gate de nome-reservado usa `ns_is_base_provenance(ns)` (prefixo `teko::`) | `src/checker/check_modules.tks:174-195` | é o DESVIO D139 (namespace), a corrigir p/ Forma-3 |
| codecs UTF-8 prontos: `decode_utf8_u32(b):[]u32`, `encode_u32_utf8`, `str_to_u32` | `src/text/text.tks:138,189,251` | `char::from(Utf8)` DELEGA — não reimplementa |
| NÃO há enum `Encoding`; NÃO há UTF-16/BOM/ASCII/Win1252 | (busca tree-wide negativa) | gap: enum novo + codecs adicionais incrementais |
| prelúdio embarcado = 12 `#embed` + injeção via VFS | `src/embed/prelude_embed.tks` + `src/build/project.tks:329-362` | `types.tks` entra aqui |
| `SourceFile{path,namespace,content}` / `Item{content,namespace,file,guard}` | `discover.tks:2-6` / `ast.tks:262` | NÃO carregam tag de proveniência — a adicionar |
| `teko::str::*` são builtins reconhecidos por último-segmento (não módulo real) | `src/checker/scope.tks:588-616` | os métodos delegam a estes builtins |

Contagem de call-sites (`grep -hoE 'teko::str::[a-z_]+'`):
`concat` 486 · `ends_with` 37 · `slice` 34 · `slice_to` 24 · `slice_from` 22 ·
`contains` 20 · `last_index_of` 6 · `starts_with` 1.

---

## 3. Capacidades: JÁ existem ou pré-requisito de ensino?

1. **`global type` newtype** — EXISTE (`base.tks` compila `exp global type ptr = isize {}`
   hoje). A spec do dono escreve `exp global byte = u8` sem `type`: é lapso — usar
   `exp global type byte = u8`. `global` só aceita fn+const+type; a forma correta é `type`.

2. **newtype-sobre-`[]byte`/primitivo COM métodos (parse + type)** — PARSEIA e TIPA
   (`NewtypeBody`, `value_type_body_methods`, `newtype_base_is_indexed`). PORÉM o
   DESPACHO de método sobre receptor BUILTIN falta → **PRÉ-REQUISITO: a ponte de
   método-primitivo** (crumb TY-C0). É a única capacidade nova real desta onda.

3. **`#if SYS_WIDTH == 64`** — NÃO EXISTE condicional de compile-time de fonte, e NÃO é
   preciso: `isize`/`usize` já são primitivos arch-parametrizados por `prim_width`/
   `checker::Target`. O `#if SYS_WIDTH` da spec é ILUSTRATIVO da semântica. **Resolução
   law-first (sem moda): NÃO introduzir `#if` de fonte** — `types.tks` declara `isize`/
   `usize` como reserved-newtype sobre o builtin (largura arch já correta). Um `#if` de
   fonte novo seria um preprocessador inteiro = moda proibida. (Fork §7-a.)

4. **Injeção em TODO artefato (incl. Package)** — EXISTE: `wants_base_prelude` já retorna
   true para Binary/Static/Shared/Package/Tool (`project.tks:275-283`). `types.tks` herda.

5. **enum `Encoding` + codecs não-UTF-8** — NÃO existem (gap). O UTF-8 está pronto
   (`text.tks`); `char::from(Utf8)` delega. Os demais encodings são superfície aditiva
   incremental (§4.3) — o self-build só usa UTF-8, então o gap NÃO bloqueia fixpoint.

---

## 4. O `types.tks` desenhado (tipos + métodos)

Arquivo novo `src/types/types.tks` (namespace `teko::types`), embarcado no VFS e injetado
como Base em todo artefato. Cada tipo reservado é declarado UMA vez aqui (dogfood: o `src/`
do compilador vira consumidor). Todos os métodos LEEM a base e produzem valores novos
(base READONLY, D131). Corpos DELEGAM à impl atual (fixpoint-safe — §5).

### 4.1 Numéricos, `isize`/`usize`, `ptr`/`uptr`
- `type u8=u8` … `f64`, `bool`, `type byte = u8`. Métodos-instância (delegam):
  `to_str(): str`; inteiros `abs()`, `min(o)`, `max(o)`; float `floor()`, `is_nan()`.
  Estático: `parse(s: str): T | error`. (Só o que agrega valor ao dev.)
- `type isize=isize`, `type usize=usize` — SEM `#if`; width arch já correta. `to_str()`.
- `type ptr=isize { fn wrap<T>(): T|error|null { self.__wrap<T>() } }` (delega ao
  intrínseco). `ptr::unwrap<T>(ref x): ptr` estático empacota `__unwrap`. `uptr` idem.
  Movidos de `base.tks` para cá (base.tks é esvaziado).

### 4.2 `str` — SÓ byte-level (backing `[]byte`, mesma-rep, D131)
`str` É `[]byte` cru; NENHUMA operação char-level assume encoding. Métodos-instância
(delegam aos builtins de `scope.tks`/`teko_rt.tks`):
`len(): u64` (bytes) · `slice(a,b): str` · `slice_to(b): str` · `slice_from(a): str` ·
`concat(o: str): str` · `ends_with(suf): bool` · `starts_with(pre): bool` ·
`contains(n): bool` · `last_index_of(n): u64|error` · `eq(o): bool` · `compare(o): i64` ·
`bytes(): []byte` (reinterpret zero-cópia via wrap).
Estáticos: `str::from_bytes(b): str`, `str::of(params pieces: []str): str` (o concat variádico).

**REMOVIDO de `str` (era hidden-UTF-8):** `chars`/`len_chars`/`char_at`/`to_lower`/
`to_upper`. Migram para `[]char`/`char` (§4.3). Isto faz a fronteira byte/char EXPLÍCITA.

### 4.3 `char` — code point + a fronteira byte↔char (refinamento do dono)
`type char = u32 { … }` — o code point Unicode decodificado.

```teko
/**
 * from — decode a raw byte string into Unicode code points under a NAMED encoding.
 * `str` is raw bytes; this is the ONLY bridge from bytes to code points and it never
 * assumes UTF-8 — the caller states the encoding. UTF-8 delegates to the landed
 * `teko::text::decode_utf8_u32`; the `[]u32` is reinterpreted to `[]char` zero-copy
 * (`char` is `u32`). Other encodings are additive (§4.3 gap).
 *
 * @param src  the raw byte string to decode
 * @param enc  the source encoding
 * @return     the decoded code points, or `error` on malformed input / unsupported encoding
 * @since 0.3.1
 */
static fn from(src: str, enc: Encoding): []char | error
```

Instância de `char` (char-level, delegam aos intrínsecos `(char)->…`):
`to_lower(): char` · `to_upper(): char` · `is_alpha(): bool` · `is_digit(): bool` ·
`is_space(): bool` · `to_u32(): u32`.

**Fronteira (o modelo):**
- **`str` (bytes) possui:** comprimento em bytes, fatia por byte, concat, busca de
  substring (bytes), comparação, `bytes()`. Nada de code point.
- **`[]char` (pós-decode) possui:** iterar code points, indexar por code point
  (`chars[i]`), `len` = nº de code points, mapa de caso sobre code points.
- **A travessia é EXPLÍCITA:** `char::from(s, enc): []char` (bytes→chars) e o inverso
  `str::from_bytes(encode_u32_utf8(...))` / um `char::encode(cps, enc): str`. Nunca
  implícita. O antigo `s.char_at(i)` (que assumia UTF-8) vira
  `char::from(s, Encoding::Utf8)[i]` — o encoding fica à vista.

### 4.4 enum `Encoding` (novo, em `teko::text`)
```teko
/**
 * Encoding — a named text encoding for the explicit bytes↔code-points bridge. UTF-8 is
 * the landed path; the rest are additive. `str` never carries an implicit encoding — a
 * decode always names one.
 * @since 0.3.1
 */
exp type Encoding = enum { Utf8; Utf8Bom; Ascii; Utf16Le; Utf16Be; Win1252 }
```
- **`Utf8`** → `teko::text::decode_utf8_u32` (PRONTO). O único caminho que o self-build usa.
- **`Ascii`/`Win1252`** → trivial 1-byte→code-point (aditivo, barato).
- **`Utf8Bom`** → strip BOM + `Utf8`.
- **`Utf16Le`/`Utf16Be`** → decode de surrogate-pairs (aditivo, follow-up).
- Gap honesto: `char::from` liga `Utf8` ao codec pronto e retorna
  `error { "unsupported encoding" }` nos ainda-não-implementados, entregues por incremento
  (não bloqueia — o compilador só decodifica seu próprio fonte UTF-8).

**Reuso, não reimplementação:** `char::from`/`char::encode` consomem
`teko::text::{decode_utf8_u32, encode_u32_utf8, str_to_u32, u32_to_str}` — os codecs já
landados. O enum `Encoding` é o único artefato novo de texto; os codecs adicionais
estendem `text.tks`, não `types.tks`.

**Estilo:** só `exp` ganha doc W15; corpos delegantes de uma linha, sem `//`.

---

## 5. Migração fixpoint-safe (o cerne — escalonamento)

Princípio: a ponte (TY-C0) mantém a REPRESENTAÇÃO de `str`/`char` intacta (seguem builtin
`Str`/`Char`), então NÃO há rippling de representação — a migração é PURA troca de idioma
nos call-sites (`teko::str::f(x,y)` → `x.f(y)`; `s.char_at(i)` → `char::from(s,Utf8)[i]`),
cada lote guardado por fixpoint gen2==gen3.

| lote | crumb | conteúdo | reseed |
|---|---|---|---|
| Fundação-1 | TY-C0 | ponte método-primitivo (additiva, inerte sem decls) | none (byte-preserving) |
| Fundação-2 | TY-C1 | Forma-3 provenance (namespace→injeção) | **reseed** (fixpoint-rebuild) |
| Fundação-3 | TY-C2 | `types.tks` + `Encoding` + embed + injeção; ptr/uptr movidos | **reseed** (gen0 ganha decls+métodos) |
| Migração-1 | TY-M1 | família slice (`slice`/`slice_to`/`slice_from`, ~80) | none (fixpoint só) |
| Migração-2 | TY-M2 | predicados (`ends_with`/`contains`/`starts_with`/`last_index_of`, ~64) | none |
| Migração-3 | TY-M3 | char-level → `char::from`/`[]char` + numérico `to_str`/`parse` (torna o encoding explícito) | none |
| Migração-4 | TY-M4 | `concat` (486, o maior) | none |
| Terminal | TY-T1 | retira o idioma solto órfão; **reseed final** | **reseed** |

Por que os lotes de migração NÃO reseedam cada um: após TY-C2 o gen0 já conhece a ponte +
os métodos; migrar um call-site usa capacidade que o gen0 já tem; o `teko.c` muda mas o
fixpoint (gen2==gen3) prova, e o reseed acumulado é colhido no fim (lei "limpeza primeiro,
reseed só no fim, tudo junto"). Cada lote é behavior-preserving (o método resolve à MESMA
impl builtin que o call-site solto chamava).

Ordem dos lotes: menores primeiro (slice/predicados validam a ponte em campo estreito),
`concat` (486) por ÚLTIMO. Ratchet D68 governa: métodos são wrappers finos → pico ~flat;
qualquer crescimento é causa-raiz, não teto.

---

## 6. O terminal (§ owner-present, pós-modelo — REGISTRADO, não nesta onda)

A visão D131 completa — `str` deixa de ser o builtin `Str` e passa a ser o `Named` newtype
sobre `[]byte`, `builtin_type` para de interceptar `str`/`char`, e os intrínsecos ganham
CORPO REAL nos métodos (retirando a mágica) — é o rippling de MAIOR risco (todo `Str`/`Char`
da árvore: `type_eq`, literais de string que produzem `Str`, interpolação `$"..."`, codegen).
Recomendação law-first: é o TERMINAL, feito com o dono PRESENTE (como o byte-mover de
região), APÓS a onda de métodos + reseed. Esta onda entrega o IDIOMA de método (a meta do
dono "em vez de funções soltas") sem tocar a representação. O colapso `Str`/`Char`→Named +
retro-feed do `.tkh` fica registrado como a continuação (§7-c).

---

## 7. Forks genuínos (para o dono registrar)

- **7-a · `#if SYS_WIDTH` não existe e recomendo NÃO criar.** A arch-width já é resolvida
  por `checker::Target`/`prim_width` (D132). Criar preprocessador de fonte = moda.
  Resolução aplicada: `types.tks` declara `isize`/`usize` sem `#if`. Se o dono QUISER `#if`
  literal de fonte, é onda própria (novo construto de linguagem) — fora deste escopo.

- **7-b · ponte método-primitivo é capacidade NOVA (TY-C0).** Sem ela, `s.concat(b)` sobre
  o builtin `Str` não tipa. É o único ensino-pré-requisito. Additiva e inerte até `types.tks`
  landar. (Não é fork de decisão — é o "ensinar agora" da lei; registro por ser a peça nova.)

- **7-c · colapso `Str`/`Char`→Named + retirada dos intrínsecos = terminal owner-present.**
  Não cabe nesta onda fixpoint-safe; recomendo executar com o dono, pós-reseed (§6).

- **7-d · codecs não-UTF-8 (`Utf16`/`Win1252`/`Ascii`/`Utf8Bom`) são gap aditivo.** Não
  bloqueiam (self-build é UTF-8). `char::from` entrega `Utf8` já e honest-stop no resto,
  entregue por incremento. Se o dono quiser todos de saída, é escopo extra em `text.tks`.

---

## 8. Re-fiação da provenance (namespace → injeção)

O gate atual (`check_modules.tks:174-195`) usa `ns_is_base_provenance(ns)` = prefixo
`teko::` no namespace do Item — o DESVIO D139. Forma-3 (D133/D144) quer a etiqueta de
ORIGEM-POR-INJEÇÃO. Desenho:

1. `SourceFile` ganha `base: bool` (`discover.tks`). `discover`/`main.tks` = false;
   `inject_runtime_prelude` = true nos SourceFiles que ele anexa (`project.tks:357`).
2. Fiar `base` por `assemble` (`AsmFileItems` + o merge em `assemble.tks:191,247`) até
   `parser::Item` (novo campo `base: bool` em `ast.tks:262`).
3. `check_reserved_type_redefs` passa a testar `!prog.items[i].base` no lugar de
   `ns_is_base_provenance(...)`; **remover `ns_is_base_provenance`** (código do desvio).
4. Alinha o 0171 (PV-C1) que já desenhou a etiqueta Base/User — esta onda a CONCRETIZA
   pela injeção, dando o dogfood: o `src/` do compilador é `base=false` (User) e consome as
   defs Base injetadas → não pode redefinir `str`/`ptr`/… (prova o gate contra si mesmo).

---

## 9. Encaixe antes do W6 + não-colisão com o sweep

Ordem do dono: sweep **W1-W5** (reclama memória, `0156-0160`) → **`types.tks`** (esta onda,
TY-C0..TY-M4) → **W6** (`0161`, reball final dos nomes reservados). W6 rebala o USO de
`wrap`/`unwrap` em massa (`f64_bits`/`f64_from_bits`/`as_ptr`/`bytes_of_str`) — precisa do
layout Forma-3 correto JÁ landado, por isso vem depois.

Não-colisão: W1-W5 tocam o codegen-DE-REGIÃO (`lower.tks`/param de região em `codegen.tks`)
e `arena.tks`/`residence.tks`; esta onda toca o SISTEMA DE TIPOS (`typer.tks`/`type.tks`/
`scope.tks`/`check_modules.tks`) + build/embed + `text.tks`. Eixos ortogonais. O único ponto
comum é `codegen.tks` (ambos editam), por isso a execução é LINEAR (types.tks landa + reseed
ANTES de W6), nunca paralela no mesmo arquivo. Confirmado: nenhum crumb W1-W5 edita
`typer.tks`/`type.tks`/`check_modules.tks`.

Nota de sequência: os seq 0175+ desta onda são numericamente > 0161 (W6), mas a
EXECUTION-ORDER manda: rodam ENTRE 0160 e 0161. O 0161 (W6) ganha dep na fundação desta onda
(TY-C2). Registrar na `.crumbs/EXECUTION-ORDER.md` ao integrar.
