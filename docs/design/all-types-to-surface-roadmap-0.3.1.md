# Roadmap "todos-os-tipos-à-superfície" + resgate do `error`-como-interface (0.3.1)

> **Status:** DESENHO / CONSOLIDAÇÃO. Zero código de produto. Não re-abre D131/D133/D134/
> D145/D187/D188/D192/D195/D197 (deliberados). Base `fix/retirement @ 1521a87a`.
> **Objetivo:** (A) resgatar e consolidar a deliberação do `error`-como-interface; (B) fixar
> o roadmap único de "todos os tipos viram superfície Teko", com template, ordem e riscos.
> Ratificação-surface (A/B do §D) pendente do dono ANTES de qualquer implementação.

---

## PARTE A — `error` como interface: a deliberação resgatada

### A.0 Onde a deliberação vive (citações)

A proposta do dono — *"nada impede de que `error` pudesse ser interface e não estrutura"*, o
**espelho completo do Go** (`interface { Error() string }`, o usuário cria seus próprios erros) —
está deliberada e **totalmente desenhada**, distribuída por três documentos e um pino ratificado:

| fonte | o que fixa | data |
|---|---|---|
| `degrau-22-error-layout-e-fabrica-0.3.1.0.md` §10 | as **4 perguntas medidas** (interface em união? métodos? `err_loc` sobrevive? há `errors.As`?) + a ordem que a medição sustenta | 0.3.1.0 |
| `arco-error-interface-reflexao-0.3.1.0.md` (doc inteiro) | o **desenho completo em 4 lanes**, o `errors.As` por identidade-de-vtable (2 instruções LIR que já existem), propriedades-como-açúcar-de-getter, o portão de vazão | 0.3.1.0 |
| `interface-value-type.md` §5 Crumb S4 + §6.2 | o **pino consciente** em `resolve.tks` (interface-em-união é honest-stop deliberado, relaxamento de 1 linha quando a hora chegar) | keystone 0.3.0.29 |
| `null-union-c3-c7-0.3.0.30.md` §"Interface-in-union (#28 S4 carve-out)" | contrato: escrever o gate de variante pra o braço de interface ser **relaxamento de 1 linha, não reescrita** | ratificado 0.3.0.30 |

**Precisão honesta (o dono deve saber):** o que está **ratificado** é o *sequenciamento* e os *pinos*
(interface-em-união fica honest-stopped de propósito; `Iface | null` reusa o nicho de null-union). O
enunciado *"`error` É uma interface"* como decisão FINAL **não tem número de DECISION_LOG** — existe
como **proposta do dono + desenho pronto** (o arco de 4 lanes). Este doc consolida esse desenho e o
devolve pronto pra ratificar. Relacionado mas DISTINTO: D75/D77 ("reforma união→interface primeiro",
2026-08-24) é o **mecanismo de boxing de variantes grandes** (memória), não o `error`-surface.

### A.1 Estado ATUAL do `error` no compilador (verificado, não da doc)

- **`error` NÃO é palavra do lexer** — é identificador comum resolvido por nome:
  `src/checker/scope.tks:270` → `if name == "error" { return Error { } }`.
- **`Error` é etiqueta vazia** no enum `Type`: `src/checker/type.tks:85` `pub type Error = struct { }`,
  membro da macro `Type()` (`type.tks:95`).
- **Assimetria construção/leitura** (a chave do degrau 22): construção permite **1** campo
  (`message`, `typer.tks`), leitura permite **6** (`message`/`file`/`expected`/`actual`:str,
  `line`/`col`:u32). Os 5 de diagnóstico são legíveis-nunca-construíveis-por-literal.
- **Fábrica/adorno:** `teko::error::err_loc` (10 chamadas) e `teko::error::err_typed` (22) são
  builtins de namespace que copiam-e-adornam; a rota-C liga a `tk_error_loc`/`tk_error_types`
  (`static inline` no `teko_rt.h`, **sem símbolo ligável** → a rota native baixa em Teko).
- **Rep:** `error` é representado pelo **endereço** do seu slot (`ltype_of(Error)==Ptr`); layout
  cresce 16→72 bytes no degrau 22 (rota-C já é 72). `[]error` = **0 sítios**; `error` como campo =
  **0 sítios** → mudar `error` não quebra nenhum agregado.
- **`| error` no corpus:** **1454 ocorrências** em `src/` (o custo de qualquer relaxamento de união).

### A.2 A maquinaria de INTERFACE — EXISTE e está VIVA (para classes, rota-C)

**A peça mais importante do resgate: interface como value-type + dispatch dinâmico JÁ ESTÁ EMBARCADO.**
Não é keystone-a-construir. Confirmado no código atual e em `interface-value-type.md` §0 (reproduzido) +
DECISION_LOG D30 (#326, UB de vtable consertada por thunks tipados, cobre trait/interface/base,
fixpoint gen1==gen2==gen3):

| peça | onde | estado |
|---|---|---|
| interface resolve como value-type (param/campo/retorno/local/elem-slice) | `resolve.tks` `resolve_named` | **vivo** |
| classe conforme faz **upcast** pra interface | `resolve.tks` `widens_into_at` (`is_class && is_interface && conforms`) | **vivo** |
| **fat pointer** `{data@0, vtable@8}` (16 bytes) | `codegen.tks` `emit_as` CONTRACT slot | **vivo** |
| **vtable estática** por par `tk_vt_<Class>_<Iface>` | `codegen.tks` (13 refs, `emit_iface_call`) | **vivo** |
| **dispatch dinâmico** via `recv.vtable[slot](recv.data,…)` | `codegen.tks` `emit_iface_call` | **vivo** |
| slices heterogêneos + upcast covariante | regressão `class_slices` (#102) | **vivo** |

**O que NÃO existe (o gap real pra `error`-interface):**
1. **interface como membro de variante** (`Iface | error`, `Iface | null`) — **honest-stop deliberado**
   em `src/checker/resolve.tks:1251` (*"an interface cannot be a variant member yet — … no `I | error`
   unions"*). Relaxamento = **2 linhas no checker** + **~2 linhas de nicho** em `cg_type_is_niche_able`
   (o reconhecimento dormente que `null-union` recomendou e **não foi cumprido** — arco §E) + o braço
   **gordo** nativo (`store_fat_variant_payload_pair`, dimensão-degrau). Depende de null-union (base).
2. **`errors.As` / teste-de-tipo** (interrogar a classe concreta atrás de uma interface) — **não existe**
   em lugar nenhum da árvore. MAS é **barato e autônomo**: `LGlobalAddr` + `ICmpEq` (as 2 instruções já
   existem e já são emitidas) comparando `value.vtable` com `&tk_vt_<T>_<Iface>`; **zero crescimento**
   (o valor de interface fica 16 bytes), zero token novo (reusa padrões do `match`). Serve TODA interface
   (`Reader`/`Writer` ganham já), não só o `error`.
3. **propriedades em interface** (pra `e.message` continuar válido sem virar `e.message()`) — hoje
   `typer.tks` rejeita acesso a campo num valor de interface (*"exposes no fields — only contract
   methods"*, correto e permanente na REP). Duas saídas medidas (§A.4).

**Veredito keystone:** `error`-interface **NÃO exige construir maquinaria de interface** (existe). Exige
**3 lanes de superfície** (interface-em-união, errors.As, propriedades-açúcar) + a migração — todas
desenhadas, nenhuma é fork de "construir do zero". **Não é keystone-próprio-bloqueante.**

### A.3 As 4 perguntas do dono, respondidas por medição (degrau-22 §10, revalidado)

- **P1 — interface pode ser membro de união hoje?** NÃO (honest-stop `resolve.tks:1251`). Levantar
  serve as **1454** ocorrências de `| error` — é uma lane sobre a **representação de variantes**, a
  montante. Cai no checker, não no backend (o dispatch já está completo).
- **P2 — que métodos, e os 5 campos?** Com interface, `e.message`→`e.message()` (**112** reescritas)
  e o contrato declara **6** métodos (não 1 `Error()` como Go, porque o compilador lê 6) — OU os 5 de
  diagnóstico ficam no tipo concreto e exigem o downcast da P4. **Saída-açúcar (getter): 0 reescritas.**
- **P3 — `err_loc`/`err_typed` sobrevivem?** Não na forma atual (retornam cópia adornada; interface é
  imutável de fora). Viram `with_pos(l,c): ErrIface` no contrato → **32 sítios de redesenho**.
- **P4 — há `errors.As`?** NÃO (zero downcast na árvore). **É a lane 1 (§A.2 item 2), barata e autônoma.**

### A.4 O desenho consolidado (o que o dono ratifica)

**Definição da interface `error` (espelho pragmático do Go, ajustado à REP Teko):**

```teko
/**
 * error — o contrato que todo valor de erro satisfaz. Espelha o `error` do Go
 * (`interface { Error() string }`) com os seis acessores que o compilador lê hoje
 * como MÉTODOS (açúcar de getter na vtable — a REP não ganha campos, D-arco §C.2).
 * Um usuário cria um erro próprio declarando `class MyErr & error { … }`.
 *
 * @see errors::as — o teste-de-tipo que interroga a classe concreta (lane 1)
 * @since 0.3.1 (arco error-interface, lane 4)
 */
exp interface error {
    fn message(): str
    fn line(): u32
    fn col(): u32
    fn file(): str
    fn expected(): str
    fn actual(): str
}
```

**Como o usuário declara um erro custom:**
```teko
exp class ParseErr & error {
    pub path: str
    fn message(): str { $"parse failed at ${self.path}" }
    fn line(): u32 { 0 }
    /* … os demais getters … */
}
```

**Como `| error` de retorno funciona:** `fn f(): T | error` continua idêntico na SUPERFÍCIE; sob o
capô o membro `error` da variante passa a ser um **valor de interface** (fat pointer `{data,vtable}`),
caixotado no wrapper pelo braço GORDO (16 bytes cabem no slot gordo de 24 sem crescer — arco §B.2).
Depende do relaxamento de `resolve.tks:1251` (lane 2), que herda o nicho de null-union.

**Como o `match` sobre error funciona (o `errors.As`):** reusa a gramática de padrões do `match` com
braços que nomeiam a classe concreta — compara `value.vtable` com `&tk_vt_<Class>_error`:
```teko
match e {                    /* e: error */
    ParseErr as p => p.path
    IoErr as io => io.message()
    _ => "desconhecido"
}
```
Uma **regra nova de tipagem** no checker (braço cuja etiqueta é classe que implementa o contrato do
receptor). Zero token novo, zero crescimento (arco §A.4).

**Provenance:** o `error`-interface mora no prelúdio-base (`teko::base`/`teko::runtime`), injetado via
VFS em TODO artefato (D134); o nome `error` já é reservado por provenance (`reserved_type_name` +
`ns_is_base_provenance`) → um `type error` de usuário é barrado, o do prelúdio passa. Zero trabalho novo.

**As 4 lanes (arco §F), em ordem, cada uma independentemente gate-able:**

| # | lane | depende de | valor sozinha | dimensão |
|---|---|---|---|---|
| **1** | **teste-de-tipo por identidade-de-vtable** (`errors.As`) | **nada** | **alta** (serve toda interface) | curta — 2 instr LIR existentes + 1 regra de tipagem, **zero crescimento** |
| **2** | interface como membro de variante | null-union (base) | média | checker 2 linhas + nicho 2 linhas + braço gordo nativo (degrau) |
| **3** | propriedades-como-açúcar-de-getter | 2 | baixa isolada | gramática (`parse_decl`) + `InterfaceBody`+1 campo (46 consumidores) + slots de getter |
| **4** | `error` como interface | 1+2+3 | é o objetivo | migração + **portão de vazão** (as 118 leituras viram 118 chamadas indiretas, 107 no caminho de diagnóstico do compilador) |

**A tensão de lei GENUÍNA (não HALT, mas é onde o dono decide):** a lane 4 troca **118 cargas de
memória por 118 chamadas indiretas**, 107 delas no caminho quente de diagnóstico do próprio compilador.
A campanha de memória tem portão-duro de vazão (ratchet D68). **Isto tem de ser MEDIDO antes da lane 4**,
e é a razão mais forte pra o `error` talvez **não** precisar ser interface: com a lane 1 (`errors.As`)
entregue, *"o error precisa MESMO ser interface?"* re-abre — a maior parte do valor (interrogar erro
tipado) já terá sido entregue **sem tocar no `error`** e sem o custo de vazão.

**Recomendação de agendamento (arco §F.3):** lane 1 primeiro (autônoma, alto valor, zero crescimento),
depois **decisão do dono** se o arco continua; lanes 2/3/4 só depois, e a 4 só depois de medir a vazão.

---

## PARTE B — Roadmap "todos-os-tipos-à-superfície"

### B.0 A visão (D131/D133/D145; CLAUDE.md "VISÃO FUTURA")

Todos os tipos viram superfície Teko, retro-alimentando o `.tkh` (sistema de tipos auto-descrito):
`type str = []byte` ✅(em curso, falta crumb 6), `type char = u32` newtype, primitivos-verdadeiros
`global exp extern type u8 = "u8" from "teko"`, `error` = interface (Parte A). O primitivo-verdadeiro
fica no **floor** (ABI/opcode); o TIPO é superfície com métodos; **provenance barra redefinição**.

### B.1 O TEMPLATE PROVADO (extraído da reificação de `str`, D192-D197)

`str-reification-0.3.1.md` provou uma metodologia **escalonada-verde** reutilizável por CADA tipo:

1. **Fundação (crumb 0):** corpos de superfície como `exp global fn` num namespace-base + os
   reinterpret-class (`wrap`/`unwrap`/`slice_view`) — verde isolado (fixpoint/ASan/3 harnesses).
2. **Ponte de coexistência `type_eq` (crumb 1, DORMANTE):** `Variante{} ≡ Named{"teko::base::X"}`
   no `type_eq` (idioma já usado por `Error`/`Null`: `type.tks:122/130`). Braços inertes que nunca
   casam em runtime → **carrega SEM reseed** enquanto o gate D164 fechar (D193); o reseed do FLIP
   absorve os dormantes. **Lei D193:** em código de dispatch quente (`type_eq`), QN/constante-string
   vai **INLINE**, nunca via fn que retorna `str` (senão injeta região-param → ripple de ~89 fns).
3. **Predicados de dispatch reconhecem AS 2 FORMAS (crumb 2, dormante):** antes do flip, cada
   `match {Variante=>}` de dispatch passa a reconhecer também o `Named`.
4. **O FLIP (crumb 3, RISCO ALTO, RESEED):** `scope.tks` keyword/literal → `Named{"teko::base::X"}`,
   **preservando o ctype** (`str`→`tk_str`) — a lição cara nº1 (ctype tem de sobreviver ao flip).
5. **Anexa métodos (crumb 4, RESEED):** `type X = <backing> { métodos finos delegando }` em
   `src/base/*_surface.tks`; `s.metodo()` resolve pelo dispatch genérico de `Named` (bigint/dec
   provam) SEM tocar o resolvedor.
6. **Converte call-sites (crumb 5):** free-fn → método, tree-wide (`src` + `cases` + `examples` +
   `tklib`, D191). **Lição cara D197:** ao surfacear um builtin que fazia **bypass de memória**
   (view/reinterpret/zero-cópia), a fn de substituição **PRESERVA a semântica** via `slice_view`/
   `wrap`/`unwrap` — **NUNCA regride pra cópia** (com reclaim 0%, cópia VAZA: `str.slice` view→cópia
   custou +1815 MB).
7. **Expurga a variante (crumb 6, RISCO MÉDIO-ALTO, RESEED FINAL):** remove `Variante{}` do enum
   `Type` + macro `Type()` + todos `match{Variante=>}` mortos (o compilador ENUMERA a morte,
   D125/D181) + name-detects residuais + ponte `type_eq`. Grep zero-ref.

**Lição transversal D195 (o end-state, não scaffold):** `is_str_type`-like é **shadow/special-case**
transitório — o end-state PURO resolve **newtype→underlying genérico** (onde a máquina de
REP/arena/reclamação casa `Slice`/`Str` cru, resolver o `Named`-newtype ao backing). str fica **opaco
(Named) só pra dispatch de MÉTODO**, transparente (`[]byte`) pra REP/arena/layout. Vale pra QUALQUER
newtype-sobre-primitivo → **cada tipo deste roadmap deve mirar o resolvedor genérico, não um
predicado-por-tipo.**

**Gate por crumb que toca compiler-core (D164/D166/D185):** fixpoint gen2==gen3 byte-idêntico +
gen0-do-seed-commitado builda o tip + ASan+UBSan limpo + 3 harnesses C standalone + grep zero-ref.

### B.2 Por tipo — especificidades, risco, deps

#### `str = []byte` — ✅ EM CURSO (crumbs 0-5 landados; crumb 6 pendente)
Estado atual verificado: `Str{}` variante **ainda existe** (`type.tks:84`), ponte `type_eq`
Str≡Named viva (`type.tks:121`), métodos vivos (`s.ends_with(x)` é idioma). **Falta crumb 6**
(expurgo de `Str{}`). **Ressalva dura D194:** `teko::str::concat` (**833 sítios**) e `last_index_of`
(**7**) ficaram free-fn sem método — o crumb 6 tem de adicionar os métodos `concat`/`last_index_of`
a `str` E converter os 840 call-sites ANTES de remover os name-detects. Risco médio-alto. **É o
pré-requisito-modelo dos demais tipos: terminar str antes de abrir char/primitivos.**

#### `char = u32` — MUDANÇA DE REPRESENTAÇÃO (risco próprio, destanglado do str por D192-A1)
**Achado corrigindo a doc A1 (que dizia "char é fat-view"):** hoje há **assimetria de rota**.
- **Rota-C:** `tk_char` é fat `{ptr,len}` **16 bytes** (`teko_rt.h:54-57`, a view dos 1-4 bytes
  UTF-8); decodifica pra escalar via `tk_char_to_u32`.
- **Rota native:** char-lit já baixa como **escalar** (`lower_char_lit` → `const_int` do codepoint);
  `elem_byte_stride Char => 4`.
`char = u32` = **colapsar a fat-view da rota-C para o escalar 4 bytes que o native já usa** —
16→4 bytes, toca `emit_type` (`tk_char`→`u32`), comparação, `tk_char_to_u32` (some — char É u32),
e a semântica de `to_lower`/`to_upper`/`char_at`/`chars` (que hoje retornam a view). **Atravessa o
reinterpret str↔char** (que dependia de mesma-largura `{ptr,len}`): some o reinterpret, entra
conversão-de-valor codepoint↔bytes. Deps: **str crumb 6 fechado** (o reinterpret str↔char precisa da
forma final de str). Risco: ALTO (rep-change de rota). Template: os 7 crumbs, mas o FLIP é
rep-change, não só resolução-de-nome.

#### Primitivos escalares (`u8`..`u64`/`i8`..`i64`/`f32`/`f64`/`bool`/`byte`/`isize`/`usize`) → `extern type`
Hoje: `Prim{kind}` (`scope.tks:252-264`), `Byte{}` (`byte`). A visão: `global exp extern type u8 =
"u8" from "teko"` — o primitivo-verdadeiro no floor (opcode/ABI), o TIPO superfície com métodos.
**Achado:** a forma `extern type Name = "u8" from "teko"` **NÃO existe** — o `extern type` atual é
só **opaco** (`extern type Name`) OU **C-ABI struct** (`= struct{…} from "tag"`, `ExternStructBody`,
`parse_decl.tks:1045-1072`). O `from` atual mapeia **tag de struct C**, não um símbolo-de-tipo-floor.
→ **superfície NOVA** (decisão B1). Risco: os primitivos são o tipo mais entranhado (`match{Prim=>}`
em todo backend, aritmética, casting, opcodes) — **NÃO fazer sem str+char provados** e provavelmente
**um por família** (int-unsigned, int-signed, float, bool). Provenance já reserva os nomes
(`reserved_type_name` inclui `str`/`char`; estender aos primitivos). Deps: char (o precedente
newtype-escalar); resolvedor newtype→underlying genérico (D195) maduro. Risco: MUITO ALTO.

#### `error` → interface — Parte A (4 lanes). Deps: null-union (lane 2), errors.As-machinery (lane 1, nova mas barata).

#### `bigint`/`dec`/`ptr`/`uptr` — ✅ JÁ superfície `Named` (confirmado)
`scope.tks:268-273`: `bigint`→`Named{teko::numeric::bigint::BigInt}`, `dec`→`Named{…Decimal}`,
`ptr`→`Ptr{inner}`, `uptr`→`Uptr{}`. bigint/dec são o **precedente Named-com-método** que str seguiu
(operador→método, `typer.tks:266`). ptr/uptr são newtypes-sobre-isize com métodos + `wrap`/`unwrap`
intrínseco (D131). **O que falta:** ptr/uptr ainda são variantes `Ptr{}`/`Uptr{}` no enum `Type`, não
`Named` puro — alinhá-los ao end-state (variante→Named) é limpeza de baixo risco, onda tardia; bigint/
dec = nada a fazer.

### B.3 Ordem recomendada + dependências

```
str crumb 6 (fecha o keystone-modelo)          ← PRÉ-REQUISITO de tudo
  → errors.As (lane 1, autônoma, alto valor)   ← independe de todo o resto; entrega já
  → char = u32 (rep-change, destanglado)        ← depende de str final (reinterpret)
  → [decisão do dono: arco error continua?]
  → interface-em-união (lane 2)  ← depende de null-union
  → propriedades-getter (lane 3)
  → error-interface (lane 4)     ← só após MEDIR vazão das 118 chamadas
  → primitivos escalares → extern type  ← por família, o mais entranhado, por último
  → ptr/uptr variante→Named (limpeza)
```

**Racional:** str é o modelo — terminá-lo primeiro dá o resolvedor newtype→underlying genérico
(D195) maduro que char e primitivos reusam. `errors.As` é a fruta-baixa (autônoma, serve toda
interface). char antes dos primitivos (precedente newtype-escalar de menor raio). Primitivos por
último (raio máximo). O arco-error corre em paralelo, gate-a-gate, com a decisão-do-dono após lane 1.

---

## PARTE D — Decisões-SURFACE (A/B) — RATIFICADAS pelo dono (2026-08-29, DECISION_LOG D198)

> **Status:** todas as 4 RULADAS. Roadmap EXECUTÁVEL. Ordem: `errors.As` (lane 1) → `char=u32`
> → resto do arco error só sob medição. Ver DECISION_LOG D198.

- **D1 — RATIFICADO: `exp global type u8 = u8 { … }` (forma auto-referente).** O dono escolheu a
  forma visível no fonte (dogfooda, mesma cara do str), aceitando a "redundância" `= u8` — preterindo
  (A) construto novo `= "<sym>" from "teko"` (não existe, fica end-state do `.tkh`) e (B) `.tkh`-view
  implícito. **Regra de checker a implementar:** decl de nome-reservado sob provingência-base cuja base
  é o PRÓPRIO nome ⇒ backing resolve pro `Prim{kind}` verdadeiro (floor opcode/ABI, magic-legítimo
  D188), NÃO ciclo; o `type` vira o `Named` de superfície que carrega só os métodos. Modelo str: opaco
  (`Named`) só pra dispatch de método, transparente (`Prim`) pra REP/opcode.

- **D2 — RATIFICADO: `char` = newtype `type char = u32 { métodos }` + literal `'x'`=char (u32
  codepoint) / `b'x'`=byte (u8).** Casa o template str, dá métodos, resolvedor newtype→underlying
  (D195) trata a REP como u32. Rep-change de rota (rota-C hoje fat-view `{ptr,len}` 16B; native já
  escalar 4B) → colapsa a fat-view da rota-C pro escalar 4B. Vem **depois do str fechado** (reinterpret
  str↔char precisa da forma final de str). Desenho confirma o lexer (hoje `'x'` sem prefixo).

- **D3 — RATIFICADO: error-interface é a decisão do dono (era proposta sem número); NÃO há fork.** O
  "custo" (118 leituras de campo do caminho quente → chamada indireta por vtable + boxing) já é
  governado pelo **ratchet D68** — não é decisão de dono, é o agente obedecendo a lei: mede e, se o
  caminho quente subir o pico, **estagia** (error embutido concreto até a conversão ser neutra).
  Sequenciamento (arco §F.3, sob ratchet): **lane 1 (`errors.As`) primeiro** (autônoma, serve TODA
  interface, 2 instr LIR existentes, zero crescimento) → lanes 2-4, a 4 só após MEDIR a vazão. A
  maquinaria de interface JÁ está viva (D30) → não é keystone-a-construir.

- **D4 — RATIFICADO: propriedade = getter/setter, sintaxe do dono `getter propriedade(): T { … }`
  acessa `a.propriedade`; simétrico `setter propriedade(v: T) { … }` acessa `a.propriedade = v`
  (confirmado).** Açúcar de método puro — a REP NÃO ganha campo (`typer` "no fields"
  continua verdadeiro). Só vive se o arco error passar da lane 1. Peça do **modelo OO de membros
  (D196)** → implementa naquela onda, não agora.

---

## Achados adjacentes — REPORTADOS, não convertidos em issue

1. **Drift da doc A1 do str-reification:** dizia "char é fat-view" genericamente; o correto é
   **assimetria de rota** (C = fat `{ptr,len}` 16B; native = escalar 4B). Atualizar A1 quando char abrir.
2. **Nicho dormente de interface NÃO cumprido** (arco §E): `cg_type_is_niche_able` trata `Named` só
   via `cg_is_class_named` → interface devolve `false`. Quem levantar `resolve.tks:1251` (interface-em-
   união) tem de acrescentar as ~2 linhas de reconhecimento de fat-pointer de interface **no mesmo
   commit**, senão `Iface | null` cai no caminho-com-tag em vez do nicho, silenciosamente.
3. **`error.file`**: zero leituras e zero escritas em toda a árvore. Carregado pela rota-C, aceito
   pelo checker, nunca preenchido. Se `error` virar interface, o método `file()` nasce sem chamador.
