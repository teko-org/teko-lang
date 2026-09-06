# Arco "SEM UNIÃO" — eliminar toda união de VALOR (0.3.1)

> **Status:** DESENHO / architect-first. ZERO código de produto. Base `fix/retirement @ 7c911029`.
> **Fonte-lei:** DECISION_LOG **D199** (o arco CONSOLIDADO — tríplice `(bool,T,error)`, forks resolvidos,
> Fase III entra gated no reclaim/D130), **D198** (roadmap tipos-à-superfície), **D197/D192** (template
> str-reificação, provado), **D130** (modelo-de-memória-por-escopo / reclaim — pré-requisito DURO da
> Fase III), **D68** (ratchet), **D187/D188** (zero-exceção-backend / lista fechada de builtins),
> **D131/D133/D134** (surface/provenance/VFS), **D30** (interface value-type VIVA).
> Este doc **bate com D199**: não redelibera; **mapeia** a demolição, sequencia os crumbs escalonados-
> verdes, e separa o **executável-já** (Fases 0-II) do que **depende do reclaim** (Fase R → Fase III).

---

## 0. Leis reproduzidas (governam CADA crumb)

- **NO-PUSHES / 4-naturezas / purge-na-reatribuição** (CLAUDE.md). Nenhuma conversão pode introduzir
  array dinâmico, `push`, `grow_inplace`, nem acumulador `x=[..x,y]`. Migração de assinatura em massa =
  pré-aloca `[n]T` do tamanho contado. `List<>` só no crescimento irredutível-e-sem-reatribuição.
- **CHECKED vs UNCHECKED (dono 2026-08-28):** `exp` devolve `error` no caminho de falha; interno (`pub`/
  privado) **panica**. O `.value` de um `null<T>` de REFERÊNCIA ausente panica **automático** (ponteiro-vazio, maquinaria que já existe) — SEM check forçado no getter; T de valor ausente = zero silencioso (dev's problem).
- **D187/D188 — lista FECHADA de builtin-legítimo:** (1) pontos de arena; (2) operador↔opcode; (3)
  reinterpret `wrap`/`unwrap`/`slice_view`; (4) `syscall` raw. **Tudo o mais é função de superfície pelo
  caminho GENÉRICO nas duas rotas.** `zero<T>` entra como **gêmeo escalar do `of_len<T>`** (D188-legítimo,
  memset-0 declarado como superfície) — único acréscimo à lista, primitiva-de-representação com identidade
  de superfície, NÃO name-detect escondido.
- **ZERO MASKING (D187/D188, reafirmado em D199-c):** nada de representação-condicional-oculta. `null<T>`
  é EXPLÍCITO `{_value,_setted}`, bool VISÍVEL. **O niche-interno foi REJEITADO pelo dono** ("voltar a
  mascarar, coisas ocultas à superfície"). A memória NÃO vem de truque de niche — vem da tríplice + rarez +
  reclaim (§6).
- **D68 — RATCHET (estrito):** o pico do build seco (`teko: memory: peak <N> MB`) só pode CAIR. Flat =
  regressão. Como o arco é remodelagem, o piso é **NÃO-CRESCER**; cada crumb mede maçã-com-maçã e corrige
  antes de drenar. **Três pontos de crescimento-risco (§6.3) — desenhados com mitigação; o mais severo
  (boxing da AST na Fase III) fica GATED no reclaim/D130.**
- **RECLAIM / MODELO-DE-MEMÓRIA-POR-ESCOPO (D130) = pré-requisito DURO da Fase III.** "Ensinar o compilador
  a LIBERAR o que não é mais necessário" (variável morre no fim do escopo léxico; `fold`/consteval que
  descarta subárvore libera; árvore de regiões = árvore de escopos). Ver §7 e
  `docs/design/modelo-de-memoria-por-escopo-0.3.1.md`.
- **Gate por crumb de compiler-core (D164/D166/D185/D191):** fixpoint gen2==gen3 byte-idêntico +
  gen0-do-seed-commitado builda o tip + ASan+UBSan limpo + 3 harnesses C standalone + grep zero-ref +
  varredura **árvore-inteira** (`src`+`cases`+`examples`+`tklib`+`tooling`+`main.tks`).
- **ENSINA-AGORA, adianta-o-necessário (D154/D155):** construir o pré-requisito e fazer certo; nada de
  ponte transitória que morre em seguida (o reclaim/D130 vem ANTES da Fase III em vez de boxear-e-vazar).

---

## 1. CENSO — os consumidores de união (grep-ancorado, números MEDIDOS)

### 1.1 A superfície de união no código
| forma | ocorrências | onde | vira |
|---|---|---|---|
| `\| error` (retorno falível) | **1637** em `src/` (1648 medido no tip; 1661 em todo `.tks`) | tree; núcleos `typer 145`, `lower 281`, `comptime_fold 88`, `math/checked 72`, `codegen 56`, `project 53`, `toml/yaml_parse 36/25`, `tkb_read 40` | **tríplice `(bool, T, error)`** multi-retorno |
| `\| null` (nulável) | **359** em `src/` | tree; `macro_expand 106`, `monomorph 70`, `parse_decl 94`, `typer 331` (inclui idioma) | `null<T>` |
| `\| CryptoError` / `crypto::CryptoError` | **138** | `src/crypto/**` | `class CryptoError & error` + tríplice `(bool, T, error)` |
| `\| NotUserType`/`NotSvcOp`/`NotPtrOp` | ~11 | checker (sentinelas de resolução) | destructar p/ `null<T>` ou pequeno resultado nomeado |
| consumo `match { … error as e => … }` / `null =>` | **2937** sítios em `src/` | tree inteira | **`if`** (bool do tuple / `.has_value()`) + downcast `errors.As` onde o tipo concreto importa |

O `\| error` é **99% compiler-core** (1637 de 1661): o arco é uma **campanha de compiler-core**, reseed
iterativo, ratchet governando — não uma mudança de folha.

### 1.2 O tipo `Variant` e sua maquinaria (o que APOSENTA)
`type Variant = struct { members: []@Type() }` — `type.tks:80`; membro da macro `Type()` (`type.tks:94`).
**729 refs a "variant" em 37 arquivos.** Núcleos do byte-mover:

| camada | refs | peças-chave (verificadas) |
|---|---|---|
| `codegen/codegen.tks` | **244** | `UnionRepr = enum { Niche; InlineTag; BoxInArena; TagPtr }` (2372); `cg_type_is_niche_able` (2284), `cg_union_niche_member` (2295), `cg_niche_*` (2318-2351); `cg_variant_has_null` (2255); `emit_variant_wrap*` (5601-5678, o **union-injection**: exact/widen/transitive pass); `emit_as_r_in` (5228); `cg_emit_variant_ctype` (2654); `cg_emit_inline_variant_typedef` (9055); `emit_pat_test`/`cg_emit_tag_prefix` (6329/6275) |
| `lir/lower.tks` | **270** | lowering nativo do gordo/niche/tag |
| `lir/lower_const.tks` | **42** | const-eval de variante |
| `checker/resolve.tks` | **32** | `variant_member_admissible` (1242) + **honest-stop interface-em-união** (1250-1251); `resolve_type UnionType` (1289); `union_normalize_null`/`union_dedup` |
| `checker/typer.tks` | **29** | tipagem de `| error`/`| null`, "there is no tuple value" (1989) |
| `checker/match.tks` | **28** | análise de braço/downcast — `is_type_discriminant_of` (57-69), `is_interface_name`→`type_conforms_to` (a base do `errors.As`, JÁ existe) |
| `emit/{tkb_write,tkb_read,tkh,header,tkb_frame}` | 12 | tag de tipo serializado (round-trip) |
| `checker/{scope,monomorph,borrow,consteval,expr,collect,check_modules}` | ~25 | construção/checagem |

### 1.3 O ACHADO DECISIVO — as famílias de AST do compilador SÃO Variants
`macro Type() { lowering { Prim | Byte | Char | Slice | Named | Variant | Func | Error | Void | Ptr | Uptr | Reference | Null } }` (`type.tks:94`). O `@Type()` (um `MacroType`) é **expandido no pré-passe Family-A
num `UnionType` → `Variant`** (`resolve.tks:1331` guarda o caso não-expandido). O MESMO vale para
`@ExprKind()` (`ast.tks:89`), `@Statement()`, `@Decl()`, `@Pattern()`, `@TypeExpr()`, `@ConstraintExpr()`
(`ast.tks:163`), `@Item()`. **Toda a espinha AST/tipo do compilador é uma união de valor discriminada.**

Consequência dura (D199 a): **abolir o `Variant` por completo alcança as famílias de AST** — elas viram
`pub type Type = interface {}` + subtipos (**struct OU classe**) que implementam. As **macros do compilador
SAEM**. Isso é ordens de magnitude maior que error/null, e tem uma **tensão de memória própria** (cada nó
AST hoje é valor inline; interface polimórfica boxeia) — por isso é a **Fase III**, e o dono a **gated no
reclaim/D130** (§7): sem liberar, boxear a AST explode o pico; com reclaim, é seguro. **NÃO é mais fork
aberto** — está deliberada em D199.

---

## 2. MOTIVAÇÃO — o arco ganha em TRÊS eixos (dono 2026-08-30)

O arco não é só limpeza de superfície; é ganho medível em três eixos ao mesmo tempo:

1. **MEMÓRIA.** (a) A tríplice `(bool,T,error)` **tira `null<T>` dos 1637 retornos falíveis** (não há mais
   `null<T>`/união no caminho de erro). (b) O expurgo do `Variant` mata o boxing de união (Niche/InlineTag/
   BoxInArena/TagPtr). (c) A Fase III só entra **com o reclaim/D130 vivo** (liberar o que morre no escopo) —
   que é a redução de memória REAL o tempo todo (reclaim-0% era a dívida).
2. **VELOCIDADE.** Hoje os **2937 consumos** são `match f() { T as x => …; error as e => … }` — `match` é
   **fluxo de controle PESADO** (testa tag / dispatcha). Com a tríplice + `null<T>`, o consumo vira **`if`**:
   `if !ok { …e… } else { …v… }` no bool do tuple, `if v.has_value() { … }` no nulável. `if` é **branch
   simples** → mais rápido em runtime **e menos codegen** em milhares de sítios. **A migração de consumo é
   enquadrada como "vira `if`", nunca "vira outro match".**
3. **SIMPLICIDADE.** O `Variant` + `UnionRepr` + `emit_variant_wrap*` (union-injection) + os honest-stops +
   as **macros de família do compilador** SAEM. A espinha AST vira hierarquia de interface honesta (D30
   value-type), evoluível sem gambiarra no pipeline (D161). Menos maquinaria, menos acoplamento.

---

## 3. Decisões travadas (D199) — encodadas, não redeliberadas

1. **`error` = interface + TUPLA TRÍPLICE.** `exp global interface error { getter message(): str;
   getter inner(): null<error> }`, prelúdio-base VFS, nome reservado por provenance. `line`/`col`/`file`/
   `expected`/`actual` SAEM da interface. `Err` concreto `class Err & error` recebe a fábrica. Downcast por
   `match { ParseErr as p => }` (conformance JÁ roda — `match.tks:57-69`). Devolução falível = **tríplice
   `(bool, T, error)`** (§4.3): `(true, v, zero<error>())` sucesso / `(false, zero<T>(), err)` falha.
2. **nulável = `null<T>`** (classe surface, ZERO união). `NULL<T>()`/`NULL()` + literal `null{}` + o 2º
   factory presente `null<T>::new(val)`. `x = null` (NullLit) MORTO — `null` é só TIPO; checagem =
   `.has_value()`. `null<T>` fica SÓ onde é opcional genuíno (ex.: `inner: null<error>`, campos que às
   vezes faltam) — o dev é LIVRE de usar `null<T>` no próprio código.
3. **`zero<T>(): T`** = primitiva nova (memset-0, gêmeo de `of_len<T>`). Produz também `zero<error>()`
   (fat-pointer zerado = "sem erro" no slot 3 do sucesso) e `zero<T>()` (o T silencioso do slot 2 da falha).
4. **Resolução pelo tipo esperado JÁ existe** (`dot_construct_target`, `explicit_inst_target`, overload). SEM
   regra de coerção base→derivada (retirada, era over-eng). ADICIONAR inferência de type-arg **dirigida pelo
   retorno** para fn genérica de zero-args (`NULL()` inferir `<i64>` do expected).
5. **Constraints = ASSERÇÃO/narrowing positivo (RULING, D199 — NÃO é proposta).** `ConstraintOr`
   (`<T: A | B>`) MORRE; `notnull` MORRE (redundante com `null<T>`). O RESTO fica intacto (form word,
   service lifetime, bound de interface/type/slice, `error`→"implementa a interface error", `&` AND). Nada
   de `any`/catch-all; "qualquer coisa" = genérico aberto `<T>`. Detalhe executável em §8.1.
6. **FORKS RESOLVIDOS (D199 — nenhum HALT):** (a) **Fase III ENTRA**, gated no reclaim/D130; (b) `ref T |
   null` **DISSOLVIDO** → `ref t: null<T>`; (c) **SEM masking** — niche-interno rejeitado. Ver §4-§7.

---

## 4. O DESENHO — o novo mundo (contratos que compilam)

### 4.1 `null<T>` — a classe nulável (Fase I)
Toda a maquinaria já existe: **classe genérica com campo de type-param** (`Map<V>`, `List<T>`,
`Dictionary<K,V>` provam), **resolução pelo alvo** (`dot_construct_target`), **guard de deref-zero**.
No prelúdio-base (`src/base/*_surface.tks`, ns `teko::base`, provenance-base):

```teko
/**
 * null — a base virtual da família nulável; a ausência sem tipo concreto,
 * endereçada pelo tipo esperado (`.{}` / literal `null{}`).
 */
exp global type null = virtual class { }

/**
 * null<T> — Option/Maybe: um `T` presente ou a ausência. ZERO união, bool VISÍVEL.
 * @param T o tipo transportado quando presente
 */
exp global type null<T> = class null {
    _value: T
    _setted: bool

    /**
     * has_value — true se um `T` está presente.
     * @return presença
     */
    fn has_value(): bool { self._setted }

    /**
     * value — o `T` presente (get de campo, `x.value`). SEM check: só devolve `_value`.
     * @return o valor
     */
    get value(): T { self._value }

    /**
     * value — grava o `T` e marca presente (set de campo, `x.value = v`).
     * @param val o valor
     */
    set value(val: T) { self._value = val; self._setted = true }

    /**
     * new — o `null<T>` ausente (valor zerado, não-setado).
     * @return a instância ausente
     */
    static new(): self { .{ _value = zero<T>(); _setted = false } }

    /**
     * new — o `null<T>` presente carregando `val` (overload por aridade).
     * @param val o valor presente
     * @return a instância presente
     */
    static new(val: T): self { .{ _value = val; _setted = true } }
}

/**
 * NULL — o `null<T>` ausente, T inferido pelo tipo esperado do sítio.
 * @return a ausência tipada
 */
exp global fn NULL<T>(): null<T> { null<T>::new() }

/**
 * NULL — a ausência base sem tipo concreto (endereçada pelo alvo).
 * @return a ausência base
 */
exp global fn NULL(): null { .{ } }
```

`value` é **get/set de campo OBRIGATÓRIO** (`x.value` / `x.value = v`) — a maquinaria de propriedade está viva
em class; se faltar algo pro caso, o C0.3 implementa (D4). O getter **NÃO força pânico**: só devolve `_value`.
O **pânico é AUTOMÁTICO** pela maquinaria de **ponteiro-vazio que já existe** (dono: "já foi criada"): T de
**referência** — `_value` zerado = ponteiro vazio → o dev usar/desreferenciar dispara o guard sozinho (problema
do dev). T de **valor** (`i64`/struct) — leitura ausente dá `zero<T>()` silencioso (footgun aceito, dev's
problem; o dev checa `has_value()` antes). O `_setted` serve ao `has_value()`, NÃO a um check no getter.

**2º factory presente (D199):** `null<T>::new(val)` constrói o presente por aridade — o dev que já tem o
valor não passa por `setter`. `NULL()`/`NULL<T>()` = atalho global só do ausente.

**Migração `T | null` → `null<T>`:**
- retorno/campo/param `X | null` → `null<X>`.
- construção do ausente: `null` (NullLit) → `NULL()` (alvo-dirigido) ou `null{}`; do presente → `null<X>::new(v)`.
- consumo `match v { X as x => …; null => … }` → **`if v.has_value() { var x = v.value(); … } else { … }`**.
- `ref X | null` (param/local pass-down) → **`ref v: null<X>`** (§4.5).

### 4.2 `error` interface + `Err` concreto (Fase II)
No prelúdio-base:

```teko
/**
 * error — o contrato de todo valor de erro. Nome reservado (provenance-base).
 */
exp global interface error {
    /**
     * message — a causa, no formato `file:line:col: causa`.
     * @return a mensagem
     */
    getter message(): str
    /**
     * inner — o erro encadeado, ausente se raiz.
     * @return o erro interno ou ausência
     */
    getter inner(): null<error>
}

/**
 * Err — o erro concreto padrão do compilador; carrega a fábrica.
 */
exp global type Err = class & error {
    _message: str
    _inner: null<error>

    getter message(): str { self._message }
    getter inner(): null<error> { self._inner }

    /**
     * of — erro simples de mensagem.
     * @param msg a causa
     * @return o Err
     */
    static of(msg: str): Err { .{ _message = msg; _inner = NULL() } }

    /**
     * loc — adorna a mensagem com `file:line:col:` (sucessor de err_loc).
     * @param msg a causa · @param file · @param line · @param col
     * @return o Err posicionado
     */
    static loc(msg: str, file: str, line: u32, col: u32): Err { .{ _message = $"{file}:{line}:{col}: {msg}"; _inner = NULL() } }

    /**
     * typed — anexa expected/actual à mensagem (sucessor de err_typed).
     * @param msg a causa · @param expected · @param actual
     * @return o Err tipado
     */
    static typed(msg: str, expected: str, actual: str): Err { .{ _message = $"{msg} (expected {expected}, got {actual})"; _inner = NULL() } }
}
```

`err_loc`/`err_typed` (name-detect em `scope.tks:548/552`; inline `tk_error_loc`/`tk_error_types` em
`codegen.tks:4271/4281`) → **fábrica `Err::loc`/`Err::typed`** (superfície, caminho genérico). `error { message = m }`
(→ `tk_error_make`, `codegen.tks:5145`) → `Err::of(m)`. `line`/`col`/`file`/`expected`/`actual` da interface
SAEM: o único leitor real é o LSP (`diagnostics.tks:206` lê `e.line/e.col`; `92` idem) → passa a **parsear o
prefixo `file:line:col:`** que `Err::loc` embute (o `strip_leading_location` já existe ali, é a metade da
maquinaria). `typer.tks:5508` (lê `inner.expected/inner.actual` para re-adornar) → o texto já vive na message.

### 4.3 Devolução falível TRÍPLICE `(bool, T, error)` — a máquina JÁ EXISTE
**Multi-retorno é real e completo:** `parse_multi_return_types` (`parse_decl.tks:227`, exige ≥2 tipos),
baixado a **struct manglada** (`mret_mangle`/`mret_struct_item`/`mret_named_type`, `collect.tks:399/414/427`;
`mret_rewrite_returns` reescreve os `return a, b`), com destructuring `var a, b = f()` (`type_multibind_mret`,
`typer.tks:4022`). A tríplice é struct de 3 campos → **ZERO máquina nova** (≥2 já satisfeito).

Convenção do COMPILADOR (o dev é livre de usar `null<T>` no próprio código):
- **sucesso** = `(true, v, zero<error>())` → `return true, v, zero<error>()`.
- **falha** = `(false, zero<T>(), err)` → `return false, zero<T>(), Err::of(msg)`.
- **consumo** = `var ok, v, e = f(); if !ok { return false, zero<U>(), e } else { …use v… }` (propaga).
- **o discriminante é o `bool`** — não se checa null do error nem se dispatcha por tag: só um `if`.

**Footgun-do-bool ACEITO (D199):** ignorar o `bool` e ler o `T` de uma falha dá `zero<T>()` **silencioso**
(à la Go) — falha de construção do DEV, problema DELE (lei checked/unchecked). **NÃO há pânico** nesse
caminho (o `zero<T>()` do slot 2 é bit-válido) — logo **não existe fixture de pânico** para o uso-antes-do-
check da tríplice (contraste com o `null<T>.value()`, que panica).

**Elegância que o arco desbloqueia:** como o `error` volta como **valor autônomo** (3º slot do multi-retorno),
ele **NUNCA é membro de união** → o honest-stop `resolve.tks:1250-1251` ("an interface cannot be a variant
member yet") **nunca precisa ser relaxado** (a lane 2 do roadmap vira MOOT), e o nicho-de-interface dormente
não precisa ser cumprido. A abolição da união **dissolve** o gap de interface-em-união em vez de consertá-lo.

### 4.4 `zero<T>(): T` — primitiva (Fase 0)
Gêmeo escalar de `of_len<T>` (`typer.tks:793/864`): checagem em `typer` (1 type-arg explícito ou inferido, T
storable, não-`Reference`/`Void`), emissão memset-0 em `codegen` (rota-C `(T){0}`) e `lower` (nativo). D188-
legítimo (representação, superfície declarada). Usos: `null<T>::new`, slot 3 do sucesso (`zero<error>()`,
fat-pointer zerado), slot 2 da falha (`zero<T>()`), default de campo, acumulador zerado. Mantém a rede
"campo precisa init" (preterida a opção campo-zero-default implícito).

### 4.5 Nulável de referência DISSOLVIDO (D199-b) — `ref t: null<T>`
Não é fork. `ref T | null` **não existe em retorno** (retorno é DPS; `typer.tks:5016/5202` já rejeita
"optional reference return"). Só param/local pass-down → vira **`ref t: null<T>`**: `ref` é **modificador
ortogonal** (só LValue/write-back) e o tipo é `null<T>` — NÃO `null<ref T>` (o `_value: T` nunca guarda um
`ref T`, então a lei "a reference cannot be stored in a struct" não é violada). O valor gordo já carrega
ponteiro; não precisa `ref` pra compartilhar. **A maquinaria `is_ref_null_variant` (`type.tks:170`,
consumida em `typer.tks:2412/3739/5016/5123/5202`), `sole_sibling_is_null` (`resolve.tks:1232/1246`) e a
cerca-de-deref RETIRA** — dobra no `null<T>`. As duas mensagens "optional reference return" (`typer.tks:5016/
5202`) somem junto (a rejeição continua natural: `ref` não é tipo-de-retorno).

### 4.6 Inferência de type-arg dirigida pelo retorno (Fase 0)
Hoje a inferência de type-arg vem dos ARGUMENTOS; `NULL()` (zero-args) não tem de onde inferir `<T>`. D199
manda **ensinar** a inferência pelo `expected` do sítio (`dot_construct_target(expected)` já entrega o alvo;
falta propagá-lo à instanciação genérica de fn de zero-args). Verificado: `explicit_inst_target`/
`dot_construct_target` existem em `typer.tks:2717`; a extensão é **ligar o `expected` à resolução de
`callee_type_args` vazio de uma fn genérica** — mudança localizada no `type_call`, não máquina nova. (Sem
essa peça, `NULL()` exigiria `NULL<i64>()` — que o dono vetou como workaround.)

---

## 5. CRUMB-SEQUENCE — escalonado-verde (template str D192-D197)

Princípio: **construir o novo (dormante) → coexistir → migrar consumidores por camada → aposentar o velho por
último**. "Dormante" = carrega SEM reseed (gate D164 fecha, o reseed do FLIP absorve). "FLIP" = reseed + ritual.

**Executável-já: Fases 0-II.** **Gated no reclaim: Fase III** (após a Fase R / D130).

### FASE 0 — Fundação (aditiva, dormante/leaf)
- **C0.1 · `zero<T>` primitiva.** typer (gêmeo de `of_len`) + codegen memset-0 + lower. Aditivo; nada consome
  ainda. **Sem reseed** se emissão não muda C existente (mede byte-diff=0). Risco: BAIXO.
- **C0.2 · inferência de type-arg pelo retorno.** `type_call` liga `expected` a `callee_type_args` vazio.
  Aditivo (só habilita casos hoje rejeitados). **Sem reseed.** Risco: BAIXO-MÉDIO (tocar o resolvedor de
  chamada; medir que nenhuma resolução existente muda).
- **C0.3 · prelúdio `null`/`null<T>`/`NULL`/`NULL<T>` (`src/base/null_surface.tks`).** Novo arquivo `teko::base`,
  injetado VFS. Usa `zero<T>` (C0.1) e o 2º factory por aridade. NADA no `src/` usa ainda → dormante.
  **Reseed** (nova superfície no prelúdio de todo artefato). Ritual. Risco: MÉDIO (colisão de nome/provenance
  — estender `reserved_type_name` para `null`).
- **C0.4 · prelúdio `error` interface + `Err` (`src/base/error_surface.tks`).** Interface + classe concreta +
  fábrica. `error` já é reservado por provenance. Dormante (o `Error{}` variante ainda é o que roda).
  **Reseed.** Ritual. Risco: MÉDIO.
- **C0.5 · expurgo de constraint `ConstraintOr` + `notnull` (folha, ISOLADO).** Ver §8.1 — remoção pura de
  maquinaria não-usada (ZERO call-site na árvore). Independente do resto do arco; pode landar cedo. **Reseed**
  (muda o parser/checker/tkb → toca o C emitido). Risco: BAIXO (nada consome; o compilador enumera qualquer
  ramo morto). **Zero teste** (§9) — o compilador enumera a rejeição do `|`/`notnull` sozinho.

### FASE I — nulável: `T | null` → `null<T>`
- **C1.1 · lane errors.As (downcast por identidade-de-vtable).** Autônoma (roadmap lane 1): `LGlobalAddr`+
  `ICmpEq` comparando `value.vtable` com `&tk_vt_<Class>_<Iface>`; regra de tipagem no braço de `match` cuja
  etiqueta é classe conforme (a análise já existe — `match.tks:57-69`). Serve TODA interface. **Zero
  crescimento.** Reseed. Ritual. Risco: MÉDIO. (Vem cedo porque o downcast de `Err`/domain-errors depende
  dela na Fase II — adianta-se o necessário.)
- **C1.2 · ponte `type_eq` `Null{} ≡ Named{"teko::base::null"}` (DORMANTE).** Idioma já usado por Error/Null
  (`type.tks:127/136`). QN **INLINE** (lei D193 — nunca fn que retorna `str`). Nada produz o Named ainda.
  **Sem reseed** (absorve no FLIP). Risco: MÍNIMO.
- **C1.3 · predicados de dispatch reconhecem AS 2 FORMAS.** `is_null_type(t)` reconhece `Null{}` E
  `Named{null-QN}`; roteia os `match {Null=>}` de dispatch (codegen niche/tag, lower, tkb, typer). Byte-
  idêntico (nada produz o Named). Sub-crumbs por camada (codegen/lower/emit/checker). Reseed só se a camada
  medir drift. Risco: MÉDIO (codegen niche).
- **C1.4 · FLIP.** `null` deixa de ser `TokenKind::Null`/NullLit-expr e vira **TIPO** reservado; `T | null`
  resolve para `null<T>` (não mais `Variant` com membro `Null`); `NULL()`/`null{}`/`null<X>::new(v)` constroem.
  Os predicados de C1.3 reconhecem; a ponte C1.2 casa os `Null{}` residuais. `is_ref_null_variant`/
  `sole_sibling_is_null` RETIRAM (§4.5). **Muda o C emitido.** **Reseed obrigatório.** Ritual. Risco: **ALTO**
  (é o flip; + a mudança de rep niche→classe — §6.3).
- **C1.5 · varredura de consumidores `T | null`/`match{null=>}`/`x=null`/`ref X|null` (árvore-inteira).**
  Converter retornos, campos, params e sítios de match para `null<X>`/`.has_value()`/`.value()`/`NULL()`/
  `ref v: null<X>`. **Consumo vira `if`, não match** (§2, eixo velocidade). Guiado por grep; NO-PUSHES na
  reescrita em massa. **Reseed.** Ritual. Risco: MÉDIO-ALTO (volume: 359 declarações + fatia dos 2937 consumos).
- **C1.6 · EXPURGO `Null{}` + niche-null + ref-null.** Remove `Null{}` do enum `Type` + macro; `cg_variant_has_null`
  (2255), `cg_union_normalize_null` (2265), `cg_niche_null_*` (2318-2351), `named_type_is_null` (1164),
  `sole_sibling_is_null` (1232), `is_ref_null_variant` (type.tks:170); ponte C1.2. O compilador ENUMERA o morto
  (D125/D181). **Reseed final da fase.** Ritual + grep zero-ref. Risco: MÉDIO-ALTO.

### FASE II — error: `T | error` → tríplice `(bool, T, error)` + interface
- **C2.1 · fábrica: `error{msg}`/`err_loc`/`err_typed` → `Err::of`/`Err::loc`/`Err::typed`.** Varredura de
  construção (árvore-inteira). Remove os name-detects `err_loc`/`err_typed` (`scope.tks:548/552`) e o inline
  `tk_error_loc`/`tk_error_types`/`tk_error_make` (codegen) — caem no genérico via fábrica de superfície.
  **Reseed.** Ritual. Risco: MÉDIO (volume de construção; `err_typed`/`err_loc` têm ~32 sítios diretos + o
  `error {` tem 466 no typer sozinho).
- **C2.2 · migração de retorno falível `T | error` → tríplice `(bool, T, error)` (POR MÓDULO, coupled com
  callers).** A grande. Cada fn `f(): T | error` → `f(): (bool, T, error)`; `return v` → `return true, v,
  zero<error>()`; `return e` → `return false, zero<T>(), e`; e TODO caller `match f() { T as x => …; error as
  e => … }` → **`var ok, x, e = f(); if !ok { return false, zero<U>(), e } else { …use x… }`** (consumo vira
  **`if`**, eixo velocidade). Encenar **por módulo folha-para-raiz** (encoding/crypto/io antes de checker/
  codegen), fixpoint como guarda. Domain-errors juntam aqui: `class CryptoError & error`, `T | CryptoError` →
  `(bool, T, error)` (downcast via C1.1 onde o tipo concreto importa). **Reseed por lote.** Ritual. Risco:
  **ALTO** (1637 retornos + fatia grande dos 2937 consumos; é o coração do byte-mover). Multi-retorno já
  existe → sem máquina nova, mas o VOLUME é a campanha.
- **C2.3 · FLIP `error` embutido→interface (lane 4 do roadmap) — SÓ APÓS MEDIR VAZÃO.** `Error{}`
  construção/leitura passam pela vtable do `Err`. **Portão-duro D68:** as ~118 leituras de campo do caminho
  quente de diagnóstico viram chamadas indiretas — MEDIR o pico ANTES; se subir, **estagiar** (mantém `Err`
  concreto embutido, adia a indireção até ser neutra). **Reseed.** Ritual. Risco: **ALTO** (vazão + rep).
- **C2.4 · EXPURGO `Error{}` variante.** Remove `Error{}` do enum + macro + ctype `tk_error` name-detect
  (codegen 1995/2026/2831); ponte type_eq de Error. **Reseed.** Ritual + grep zero-ref. Risco: MÉDIO-ALTO.

### FASE R — reclaim / modelo-de-memória-por-escopo (D130)  ⚠️ PRÉ-REQUISITO DURO da Fase III
Não é escopo NOVO do arco — é a campanha D130 que o arco **abraça e ORDENA antes da Fase III** (§7). Sem
reclaim, boxear a AST (Fase III) explode o pico → viola o ratchet. Marco de saída da Fase R (o gate da Fase
III): **reclaim ratio > 0 medido** (o oráculo `residence_plan` DIRIGE a emissão nas duas rotas; variável morre
no fim do escopo; `fold`/consteval que descarta subárvore LIBERA) **e** o pico do build seco em queda/flat sob
D68. **Reseeds/rituais por degrau do SWEEP** (ver o plano D130 próprio). O arco NÃO reimplementa o D130 aqui —
referencia `docs/design/modelo-de-memoria-por-escopo-0.3.1.md` + `plano-fiacao-modelo-memoria-por-escopo-0.3.1.md`
+ `port-memoria-por-escopo-backend-nativo-0.3.1.md`.

### FASE III — abolir o `Variant` genérico + famílias AST  (D199-a — ENTRA, gated na Fase R)
- **C3.1 · famílias AST → interface (aditivo/dormante).** `@Type()`/`@Statement()`/`@Decl()`/`@Pattern()`/
  `@TypeExpr()`/`@ConstraintExpr()`/`@Item()` viram `pub type X = interface {}` + subtipos (**struct OU
  classe** — struct implementa a interface = **valor-inline**, D30, não só heap-class) que implementam. As
  **macros de família SAEM** do compilador. Downcast via C1.1 (errors.As) + conformance de `match`. Encenar
  família-a-família, dormante até o FLIP. Risco: **ALTO** (espinha do compilador).
- **C3.2 · FLIP + medição sob reclaim.** A AST passa a ser hierarquia de interface. **Portão-duro D68:** o
  nó polimórfico vira fat-pointer (boxeado onde não-inline) — o reclaim (Fase R) libera o boxing por escopo;
  MEDIR o pico maçã-com-maçã; struct-implementa-interface fica **valor-inline** onde o tipo é conhecido, só
  boxeia no armazenamento polimórfico. **Reseed.** Ritual. Risco: **ALTO** (vazão × milhões de nós).
- **C3.3 · EXPURGO do `Variant` + toda a maquinaria.** `Variant` + `UnionRepr` (Niche/InlineTag/BoxInArena/
  TagPtr) + `emit_variant_wrap*` (union-injection) + `cg_type_is_niche_able` + os honest-stops SAEM. O
  compilador ENUMERA o morto (D125/D181). **Reseed final do arco.** Ritual + grep zero-ref. Risco: MÉDIO-ALTO.

**Reseeds/rituais (Fases 0-II, executável-já):** C0.3, C0.4, C0.5, C1.1, C1.4, C1.5, C1.6, C2.1, C2.2(por
lote), C2.3, C2.4. **Total Fase 0-II: ~15 crumbs** (com sub-crumbs em C1.3 e C1.5/C2.2 por módulo). C0.5
(constraints) é folha-independente e pode landar a qualquer momento. **Fase R = campanha D130 (própria).
Fase III = C3.1-C3.3, só após a Fase R.**

---

## 6. Ordem, dependências, riscos, memória

### 6.1 Grafo de dependências
```
C0.1 zero<T> ─┐
C0.2 ret-infer┼─► C0.3 null<T> ──► C1.2 ponte ─► C1.3 predicados ─► C1.4 FLIP null ─► C1.5 sweep ─► C1.6 expurgo Null
              └─► C0.4 error-iface                                                                        │
C1.1 errors.As (autônoma, cedo) ───────────────────────────────────────────────────────────────────────┤
                                                                                                          ▼
                       C2.1 fábrica Err ─► C2.2 (bool,T,error) por módulo ─► C2.3 FLIP iface(medir) ─► C2.4 expurgo Error
                                                                                                          ▼
                                            FASE R (reclaim/D130 — pré-requisito DURO)
                                                                                                          ▼
                                 C3.1 AST→iface ─► C3.2 FLIP(medir sob reclaim) ─► C3.3 expurgo Variant
```
`null<T>` (Fase I) ANTES da tríplice (Fase II) — o `null<error>` do `inner` da interface literalmente é
`null<T>`. `errors.As` (C1.1) cedo — o downcast de `Err`/domain-errors depende dela. Fase III **só** após a
Fase R (reclaim vivo).

### 6.2 O que é byte-mover de RISCO
- **C1.4 (FLIP null)**, **C2.2/C2.3 (error)** e **C3.2 (AST→iface)** são os byte-movers de maior risco: mudam
  rep + emissão em massa. Mitigação: predicados-que-reconhecem-ambos ANTES do flip (grep de `Null =>`/`error
  as` remanescente em dispatch = 0 antes do flip); ordem estrita; medir byte-diff do `teko.c` por camada.
- **C2.2** é o maior VOLUME (1637+consumos). Mitigação: por módulo folha→raiz, fixpoint por lote, mecânico
  guiado por grep, NO-PUSHES na reescrita, consumo→`if`.

### 6.3 Custo de memória — TRÊS pontos de crescimento-risco (D68)
1. **`null<T>` perde o niche — SEM masking (D199-c).** Hoje `T | null` para T niche-able usa niche (0 bytes
   extra). `null<T>` como `{ _value: T, _setted: bool }` **adiciona o bool + padding** (~8 bytes) por
   ocorrência. **O niche-interno foi REJEITADO pelo dono** (mascarar = contra D187/D188). A memória NÃO se
   resolve por truque: resolve-se por **(1)** a **tríplice** tirar `null<T>` dos 1637 falíveis (o grosso das
   ocorrências some do caminho de erro); **(2)** `null<T>` fica RARO ("muitos nulos = falha de design"); **(3)**
   o **reclaim (Fase R/D130)** liberar o vivo. O `null<T>` que sobrar paga os +8 bytes honestos, à vista.
   Cada crumb da Fase I mede o pico maçã-com-maçã; a queda vem do colapso do count, não de niche.
2. **`error` interface (C2.3) = vazão.** As ~118 leituras do caminho quente de diagnóstico → chamadas
   indiretas por vtable + boxing do fat-pointer. Governado por D68/D198-D3: **medir e estagiar**. Se subir,
   `Err` concreto embutido fica até a conversão ser neutra.
3. **Boxing da AST (Fase III/C3.2) = o mais severo.** Todo nó AST polimórfico vira fat-pointer boxeado (a
   vazão da lane-4-error × milhões de nós). **É EXATAMENTE por isso que a Fase III é gated na Fase R:** com o
   reclaim vivo (variável morre no escopo, `fold` que descarta libera), o boxing é scoped e liberado, não
   vaza. `struct implementa interface` fica **valor-inline** onde o tipo é conhecido (D30), boxeia só no
   armazenamento polimórfico — minimizando o custo. Medir sob reclaim; estagiar família-a-família.

A tríplice `(bool, T, error)` multi-retorno em si é neutra (struct manglada, mesma classe do que existe) e
**REDUZ** memória (tira `null<T>` dos 1637). O restante da migração (construção/consumo) é preservante e o
consumo→`if` reduz codegen.

---

## 7. FASE R — a dependência do reclaim/D130 (o que "ensinar a liberar" toca)

A Fase III boxeia a espinha AST; sem liberar o que morre, o pico explode. O reclaim é a redução de memória
REAL o tempo todo (reclaim ratio = **0,0%** hoje — nada morre no meio do build, tudo vaza pra root). D199
ORDENA: Fases 0-II → **Fase R (reclaim)** → Fase III. O arco NÃO reimplementa o D130; ancora nele e nomeia os
touchpoints que a Fase III exige:

- **Variável morre no fim do escopo léxico** (os 5 escopos: bloco `{}`, corpo de loop por-iteração, braço if,
  braço when/match, corpo fn). Regiões = árvore de escopos; residência = JOIN(usos) (nunca UAF). Isso torna o
  nó AST boxeado LIBERÁVEL quando o escopo que o produziu fecha.
- **`fold`/consteval que DESCARTA subárvore LIBERA.** O `comptime_fold` (88 `| error`) e o `consteval.tks`
  (`drop_cond_consts:224`, chamado em 782/789/842/882) descartam nós ao dobrar constantes/ramos mortos — hoje
  vazam. Sob reclaim, o descarte de subárvore é liberação eager (purge-na-reatribuição, CLAUDE.md). É o
  gatilho central pro boxing da AST não vazar.
- **O oráculo DIRIGE a emissão (o SWEEP).** `residence_plan`/`region_slots`/`scope_slot_count`
  (`src/checker/residence.tks:49/234/212`, LANDADO com ZERO consumidores hoje; `PtRoot`/`PtTop` em 38/40)
  decide por-binding; codegen.tks (rota-C) E lower.tks (nativo) leem o MESMO plano. O SWEEP é o FLIP onde o
  oráculo passa a dirigir (hoje roda a heurística antiga de 2 níveis). O `PtTop → PtRoot` como refúgio de
  incerteza (o "safe leak", `escape.tks`/`spine.tks:88`) sai de default.
- **Região = PARÂMETRO implícito** (DPS; NUNCA `_Thread_local`/`global var`/tid-table — D130 regra 3). Cada
  escopo abre região filha; descendo passa por arg, subindo (return) MOVE pra região do caller.

Marco de saída (gate da Fase III): **reclaim ratio > 0 medido** + pico do build seco em queda/flat (D68). A
Fase R é campanha própria (docs D130); o arco só a **posiciona** como pré-requisito duro e mede o gate.

---

## 8. Decisões executáveis — constraints (folha, isolada)

### 8.1 Constraints = asserção/narrowing — DECIDIDO (D199, ruling do dono; NÃO é proposta)
Constraint passa a ser **asserção/narrowing positivo** (mais restritiva: assere o que T satisfaz, sem
alternativa). Reescrita CONCRETA:

- **`ConstraintOr` (`<T: A | B>`) — DELETAR.** Remoção pura: **ZERO call-site na árvore** (grep de `<T: A | B>`
  em signatures = 0; sem fixture afirmativo). Sítios da MAQUINARIA a remover:
  - `parser/ast.tks:158` `type ConstraintOr` + `ast.tks:163` membro `ConstraintOr` da macro `ConstraintExpr()`.
  - `parser/parse_decl.tks:133-136` (construção do `ConstraintOr` + os dois erros "notnull não pode ser `|`
    alternativa") e `parse_decl.tks:110` (mensagem que cita "a disjunction").
  - `checker/resolve.tks:523` e `602` (braços `ConstraintOr`), `checker/monomorph.tks:63`
    (`constraint_satisfied` disjunção), `checker/merge.tks:52` (`constraint_equal`).
  - `emit/tkb_write.tks:261` (tag 2), `tkb_read.tks:610`, `tkb_frame.tks:225`.
- **`notnull` (`ConstraintNotNull`) — DELETAR.** Redundante (nulável agora é `null<T>` explícito; um `T` nu já
  é não-nulável). **ZERO call-site real** (só aparece na própria maquinaria/mensagens do parser). Sítios:
  - `parser/ast.tks:160` `type ConstraintNotNull` + `ast.tks:163` membro da macro.
  - `parse_decl.tks:106` (parse do termo `notnull`), `115-116` (`is_notnull_constraint`), `133/135` (os erros).
  - `resolve.tks:531` e `616`, `monomorph.tks:67` + `86` (`constraint_notnull_satisfied`), `merge.tks:54`.
  - `tkb_write.tks:262` (tag 4), `tkb_read.tks:613`, `tkb_frame.tks:226`.
- **INTACTO:** form word, service lifetime, bound de interface/type/slice, `error`→"implementa a interface
  error", e o `&` (AND/interseção). A mensagem de erro-de-termo de constraint (`parse_decl.tks:110`) reescreve
  para o novo conjunto (sem "disjunction", sem "notnull"): estilo compilador padrão.
- **Bump de tag tkb:** tags 2 (`ConstraintOr`) e 4 (`ConstraintNotNull`) saem do serializador de constraint —
  reindexar os tags restantes num único lote (round-trip lê/escreve o novo conjunto). Cabe no C0.5.

Executa em **C0.5** (folha, isolado, reseed). Risco BAIXO — remoção de maquinaria não-consumida; o compilador
enumera qualquer ramo morto ao auto-compilar (D125/D181).

---

## 9. Testes: ZERO (dono 2026-08-30 — endurece a lei no-test)

**REGRA DURA: nenhum crumb deste arco cria teste — nem afirmativo, nem oráculo de rejeição.** ZERO `.tkt`/
`.tkr` novo. (Endurece a lei CLAUDE.md; o dono flagrou um agente criando fixtures desnecessários e mandou
remover TODOS, inclusive os de rejeição.)

A **única prova é o self-build / fixpoint**: o compilador compila o prelúdio (`null_surface.tks`, `error_surface.tks`),
usa `null<T>`, a tríplice, downcast e a fábrica ao se auto-compilar → qualquer bug de parse/check/codegen
aparece sozinho no build. Os caminhos de **rejeição** (nome reservado `type null`/`type error` de usuário;
`value()` em `null<T>` ausente = pânico; `<T: A | B>`/`notnull` que deixou de parsear) são **enumerados pelo
próprio compilador quando alguém os aciona** — não por um arquivo de teste. Não se escreve `.tkr` pra isso.

**Ao drenar: RECUSAR qualquer `.tkt`/`.tkr`/arquivo de teste novo no delta** — kill + re-limpa, não drena.

---

## 10. FORKS — nenhum aberto (D199 resolveu os três)

Os três forks do doc anterior foram **deliberados pelo dono em D199** — não há HALT:

- **(a) Famílias de AST = Variants → FASE III ENTRA** (gated no reclaim/D130). Não é mais fork: as famílias
  viram `pub type X = interface {}` + subtipos (struct OU classe); as macros saem; o `Variant` aposenta por
  completo. Sequência ORDENADA (§5): Fases 0-II → Fase R → Fase III. Ver §6.3-ponto-3 e §7.
- **(b) Nulável de referência (`ref T | null`) → DISSOLVIDO** em `ref t: null<T>` (§4.5). `ref` é modificador
  ortogonal; o retorno já rejeita optional-reference; a maquinaria `is_ref_null_variant`/`sole_sibling_is_null`
  retira. Não é fork.
- **(c) `null<T>` niche vs ratchet → SEM masking** (niche-interno rejeitado). A memória resolve por tríplice +
  rarez + reclaim (§6.3-ponto-1). Não é fork.

**Se um crumb, EM OPERAÇÃO, encontrar um fork NOVO genuíno** (não coberto por D199 nem por este doc), HALT
curto pelo protocolo de fork — não inventar decisão. Neste desenho, nenhum foi encontrado.

---

## 11. Achados adjacentes — REPORTADOS (não viram issue)
- **`error.file`**: zero leituras/escritas na árvore (confirmado). Ao surfacear, o método `file()` nasce sem
  chamador — não recriar (sai da interface).
- **`err_typed`/`err_loc` = name-detect (D187-ilegítimo).** Já eram alvo do expurgo de builtin; o arco os mata
  de graça (viram fábrica de superfície). Ganho colateral.
- **Multi-retorno "não é tuple value"** (`typer.tks:1989`): `(A,B,C)` só vale como RHS único de bind ou de
  return — logo a tríplice NÃO cria tuple de 1ª classe (bom: menos superfície nova). Documentado.
- **`CryptoError` e domain-errors** (138 sítios) precisam implementar `error` no MESMO lote de C2.2 do módulo
  crypto — senão a tríplice `(bool, T, error)` do crypto não fecha. Anotado no crumb.
- **Censo `| error` drift** (1637 no doc anterior → 1648 medido no tip): variação natural da árvore; a campanha
  é guiada por grep no momento de cada crumb, não pelo número fixo.
