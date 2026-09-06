---
section: design
created: 2026-08-13
status: PLANO — crumb-plan implementer-ready do modificador `global` (§15 de
        `mudancas-superficie-0.3.1.md`, SELADO). READ-ONLY sobre código de produto: nenhuma edição,
        nenhum reseed, nenhum build, nenhum `teko test` foi executado ao escrever este plano.
source: mudancas-superficie-0.3.1.md §15 (SELADO) + §16/plano-stdlib (dependência declarada) +
        leitura do estado atual do checker/parser/lexer/codegen
scope: transformar o `global` selado num plano de crumbs — gramática/AST aditiva, resolução
       sem-namespace no checker, a colisão de assinatura-global, e a MIGRAÇÃO shadow→global da
       superfície ambiente injetada hoje no parse. Análise de fixpoint, sweep, fixtures, sequência.
depends-on: plano-stdlib-expansao-e-separacao.md (triagem `exp`, self-`.tkh`) e §16 (libc-direct /
       morte do `teko_rt`) — a MIGRAÇÃO por-símbolo é gated nisso; o MECANISMO não é.
---

# `global` — acesso sem namespace, e o fim do shadow de parse (plano)

> **Papel deste plano.** O §15 está SELADO (modelo abaixo, §0). Este documento entrega (1) o estado
> atual com `file:line` do shadow a remover; (2) a gramática/AST aditiva do modificador; (3) a
> resolução sem-namespace no checker + a colisão de assinatura-global; (4) o inventário e a tabela de
> migração shadow→global, símbolo a símbolo; (5) a análise de fixpoint (por que o MECANISMO é
> byte-idêntico e por que a MIGRAÇÃO por-símbolo NÃO é, e como sequenciar); (6) o sweep `.tkt`/`.tkr`;
> (7) fixtures aceitar/rejeitar; (8) a sequência de crumbs + ritual + reseed.
>
> **Disciplina aditivo-primeiro** (a mesma dos §§1–9): a gramática nova entra AO LADO da velha
> (`global` é aceito, a injeção permanece), o reseed captura um seed que fala `global`, e só então a
> stdlib passa a DECLARAR `global` no lugar da injeção, um símbolo por vez. O bootstrap fica sempre
> construível.

---

## 0. Rulings selados (§15 — NÃO reabrir)

1. **`global`** modifica uma declaração de **nível de namespace** — **função, tipo ou constante**. Uma
   **variável NUNCA é global** (sem estado mutável global) — rejeição de parse/checker.
2. **Acesso direto:** um símbolo `global` é alcançável **sem qualificar o namespace** — `println(...)`,
   `sizeof<T>()`, `str(bytes)`, `Intent` — do mesmo modo que `str`/`error` (core) já são hoje, agora
   **explícito** via `global`.
3. **Colisão:** duas declarações `global` de **mesma assinatura exportada** (mesmo nome + mesma lista de
   tipos de parâmetro) → **erro de compilação**. Assinaturas **diferentes coexistem** (é overload, §9 A).
4. **Compõe com visibilidade e comptime:** `exp global comptime sizeof<T>(): usize` — `exp` (entra no
   self-`.tkh`, §16/plano-stdlib) + `global` (sem namespace) + `comptime` (§14). A ordem no plano é
   `[doc] [pub|exp[(...)]] [global] [intern] [abstract|virtual|override] [static] [extern]` (§2.1).
5. **`global` NÃO liga enforcement de `exp`/`pub`** — isso é a §11. `global` é ortogonal a visibilidade:
   diz "alcançável sem qualificar", não "quem enxerga". `check_modules.tks:143` (a regra cross-ns) fica
   intocada por este plano.
6. **Toda a superfície ambiente vira `global`.** Tudo que hoje se usa sem qualificação — `println`,
   `sizeof`, ops de `str`/`list`, os format helpers, os coverage sinks, `panic`/`exit`, os tipos
   `str`/`byte`/`char`/`error`/`ptr`/`uptr`/`size`/`usize` — passa a ser **declaração `global` explícita**
   na stdlib, no lugar da injeção de parse. Elimina o shadow por completo.

### 0.1 — UMA ambiguidade do texto selado, resolvida law-first (não HALT)

O §15 (linha ~1198) grafa o exemplo de acesso como `@sizeof<T>()`. Todo o resto do §15 — e o resumo
selado desta tarefa, e o objetivo "preservando o comportamento (mesmo símbolo alcançável)" — descreve
acesso **bare, sem sigilo** (hoje `println(x)`/`concat(a,b)` não têm `@`). Um sigilo `@` obrigatório
**reescreveria cada call-site do corpus** e QUEBRARIA a byte-identidade da migração (Lei do fixpoint,
§5). Resolução law-first: **acesso é bare, sem `@`** — o `@` no exemplo é ênfase de doc ("o símbolo
ambiente `sizeof`"), não sintaxe. Este plano projeta acesso bare. **Ponto de ratificação leve:** se o
dono quiser MESMO um sigilo `@` para desambiguar ambiente-vs-local, isso é uma feature separada e
tardia (não bloqueia nada aqui); relatado, não transformado em issue.

---

## 1. Estado atual — onde o shadow vive hoje (o que remover), com `file:line`

O "shadow de parse" do §15 não é literalmente no parser: é a **injeção de sinais no checker** — quatro
resolvedores em `src/checker/scope.tks` mais dois builtins injetados em `typer.tks`, todos alcançados
por **fallback** quando a resolução por nome falha. Eles não têm declaração `.tks` visível; o dev nunca
os vê nem escreve. Esse é o shadow.

### 1.1 — Os quatro resolvedores injetados (`src/checker/scope.tks`)

| resolvedor | `file:line` | o que injeta (a superfície ambiente) |
|---|---|---|
| `builtin_type(name)` | `scope.tks:566` | TIPOS ambiente: `u8/u16/u32/u64/i8/i16/i32/i64/f32/f64/bool/size/usize/byte/char/str/bigint/dec/error/()/ptr/uptr` |
| `builtin_fn(name)` | `scope.tks:964` | FUNÇÕES ambiente: `print/println/write/ewrite/eprint/eprintln`, `os/arch/version/peak_rss/append_fo/abort`, os `cov_*`/`arena_*`/`intern_*`/`task_reset` sinks, `fdiv/floor/f64_from_bits/f64_bits/panic_div0/…`, o str/byte self-host (`slice/str/str_of_bytes/one_byte/str_concat/str_hash/str_compare/i64_to_str/u64_to_str/ftoa/f64_g17/parse/concat/slice_to/slice_from/len/bytes_of_str/str_from_utf8/chars/len_chars/char_at/str_slice_chars/is_alpha/is_digit/is_space/to_lower/to_upper/ends_with/contains/last_index_of`), os `err_loc/err_typed`, o marshalling `teko::mem` (`as_ptr/as_cstr/str_from_c/bytes_from_ptr/load_u64/store_u64/buf_ptr/region_buf`), os `fmt_*` |
| `assert_builtin_fn(name)` | `scope.tks:833` | a família `teko::assert::*` (via `assert_builtin_name`, `scope.tks:868`) |
| `host_surface_fn(name)` | `scope.tks:748` | a superfície host C-nativa `teko::env/io/fs/process` (os que ainda não têm decl real) |

Todos são invocados como **fallback**: veja `builtin_fn` chamado em `typer.tks:2895` (dentro de
`type_call`), e `builtin_type` em `resolve.tks:1231` (dentro de `resolve_named`, o resolvedor de tipos
bare). O comentário em `scope.tks:1050-1053` e `:955-958` já registra a disciplina "a decl REAL vence o
fallback" (`lookup_call`/`resolve_type_ref` ganham antes do builtin) — é exatamente o gancho que a
migração explora.

### 1.2 — Os dois builtins injetados diretamente no typer

- **`panic` / `exit`** — injetados como "GLOBAL diverging builtins" em `typer.tks:2867-2891` (o próprio
  comentário do código já os chama de `injected global`). Disparam SÓ quando `lookup_call` não acha nada
  (`in_scope == false`), para o `fn panic` real do runtime resolver a si mesmo.

### 1.3 — Onde a resolução NÃO-qualificada acontece (os pontos que `global` amplia)

A superfície ambiente é alcançável bare hoje porque a resolução por nome tem um **ramo bare** que, para
NÃO achar no escopo, cai no fallback. Os pontos exatos:

| ponto | `file:line` | regra bare hoje |
|---|---|---|
| chamada (valor) | `scope.tks:348` `call_binding_matches` | não-qualificado casa `b.ns.len == 0 || b.ns == cur_ns` — **mesma-ns ou local** |
| chamada — scan de globais | `scope.tks:370` `call_base_pos` / `:397` `lookup_call` / `:422` `call_ns` | aplicam `call_binding_matches` sobre `env.base_slots` |
| valor `ns::NAME` / bare const | `scope.tks:283` `lookup_value_in_ns` | filtra por nome + `qualifier_selects_ns` |
| tipo bare | `resolve.tks:640` `resolve_bare_type_reg` | casa `r.namespace == ref_ns` (própria ns), depois root (`:669`), depois g-instances |
| tipo bare — ns vencedora | `resolve.tks:820` `type_ref_ns` (ramo `segments.len == 1`, `:824-832`) | idem |

**A observação-chave para o desenho:** já existe hoje o conceito de **"sealed globals"** — `env.base_slots`
(`scope.tks:24` `ValBinding`, `:72` `Env`, `:94` `seal`), onde toda decl de dep exportada (`const`/`fn`)
já resolve **unqualified** (`scope.tks:275` doc: *"a dep's exported const/fn is a global"*). `global`
não inventa um mecanismo novo de resolução: **estende a regra bare** dos pontos acima para aceitar
também `b.is_global` (valor) / `r.is_global` (tipo), independentemente da ns.

---

## 2. Gramática e AST do modificador `global` (ADITIVO, inerte)

### 2.1 — Lexer

`global` entra como keyword reservada, na família de `pub`/`exp`/`intern`/`static` (`lexer.tks:352-368`,
`token.tks:11-31/108-187`). Verificado: **zero** identificadores `global` no corpus de produto (só a
palavra em comentários) — reservar é seguro, sem sweep de renome.

```teko
// src/lexer/token.tks — no bloco de keywords, junto de Intern/Static:
/**
 * Global — the `global` modifier: a namespace-level fn/type/const reachable WITHOUT its namespace
 * qualifier (§15). Reserved keyword in the modifier slot; NEVER legal on a `var`. Composes with
 * `pub`/`exp` (reach) and `comptime` (§14) — it is a distinct axis (reachability), not a 4th vis tier.
 */
Global
```

```teko
// src/lexer/lexer.tks — em keyword_kind, ao lado de `if text == "intern" …`:
/**
 * Classify `global` as the reserved global-modifier keyword.
 *
 * @return TokenKind  Global when the text is exactly "global"
 */
if text == "global" { return TokenKind::Global }
```

> **Janela aditiva:** enquanto o seed atual (pré-reseed) não conhece `TokenKind::Global`, a stdlib NÃO
> escreve `global` — a injeção segue viva. O seed que reconhece `global` é capturado no reseed do Crumb
> G4 (§8) ANTES de qualquer `.tks` usar a keyword.

### 2.2 — AST: um bit `is_global` em cada uma das três formas

Aditivo, default `false`, inerte até existir uma decl `global`.

```teko
// src/parser/ast.tks — Function (após is_static, linha 541):
/**
 * is_global — a `global fn` (§15): reachable WITHOUT its namespace qualifier. Distinct from `vis`
 * (reach) and from `is_static`/`is_intern` — it is the AMBIENT-SURFACE axis. False on every ordinary
 * fn (additive-inert). A `global` method inside a type body is rejected by the checker (globals are
 * namespace-level only); carried here only so the modifier chain never special-cases position.
 */
is_global: bool
```

```teko
// src/parser/ast.tks — TypeDecl (após col, linha 771) e ConstDecl (novo campo):
/** is_global — a `global type`/`global const` (§15): reachable bare from any namespace. Default false. */
is_global: bool
```

`Field` (`ast.tks:631`) **não** ganha `is_global` — campo não é declaração de nível de namespace (ruling
1). Uma variável local (`BindKind`) idem — a rejeição de `global var` é de parse (§2.3).

### 2.3 — Parser: consumir `global` no modifier-chain

Em `parse_decl.tks`, o modifier-chain de fn está em `:366-405`; o de type em `:1302-1307`; o de const em
`parse_const_decl` (`:1458`) / `member_const_peek` (`:1508`). O slot de `global` fica **após `pub`/`exp`
e antes de `intern`** (ordem do ruling 4).

```teko
// src/parser/parse_decl.tks — logo após o bloco pub/exp (parse_function, ~linha 377):
/**
 * consume_global_modifier — consume an optional `global` keyword (§15) in the declaration modifier
 * chain, right after the visibility run. Returns whether it was present and the advanced position.
 * Contextually inert on a `var` (never reaches a decl head); the checker rejects a `global` type-member.
 *
 * @param tokens  the token stream
 * @param pos     the position just past the visibility run
 * @return        Parsed<bool> — node=true iff `global` was consumed; next=advanced position
 */
fn consume_global_modifier(tokens: []lexer::Token, pos: u64): Parsed<bool> {
    if is_kind_at(tokens, pos, lexer::TokenKind::Global) { return Parsed<bool> { node = true; next = pos + 1 } }
    Parsed<bool> { node = false; next = pos }
}
```

Threaded into `parser::Function`/`TypeDecl`/`ConstDecl` construction (o `Function {...}` de
`parse_function`, o `TypeDecl {...}` de `parse_type_decl`, o `ConstDecl {...}` de `parse_const_decl`),
setando `is_global = <consumido>`.

**Rejeições de parse (§2.3, honestas):**
- `global var …` — `is_decl_head` (`parse_decl.tks:1718`) trata `global` só em posição de decl-head; um
  `global` diante de `var` (que não é um decl-head de namespace) → `err_at(tokens, p, "'global' is not
  allowed on a variable — only on a namespace-level fn, type, or const (§15)")`. Fixture `reject`.
- `global` diante de um membro de type body (método/campo) → rejeição no checker (§3.4), não no parser,
  para dar diagnóstico com a ns/tipo.

> **Custo de sweep do bit AST.** Adicionar `is_global` a `Function`/`TypeDecl`/`ConstDecl` obriga TODO
> literal de struct desses tipos no corpus (inclusive os `.tkt` de teste que constroem `TFunction`/
> `TConstDecl` — ver `consteval_test.tkt`, `metrics_test.tkt`, `residence_test.tkt` etc.) a listar `is_global
> = false`. Isso é parseável pelo seed atual (é campo comum) → entra ANTES do reseed, no Crumb G1.
> Nota: `TFunction`/`TConstDecl` (a forma TYPED, checker) espelham a AST — se o campo precisa fluir ao
> codegen/`.tkh` (só quando a migração começar), espelha-se lá; para o mecanismo inerte, basta a AST +
> `ValBinding`/`TypeReg` (§3).

---

## 3. Checker: resolução sem-namespace, colisão, composição

### 3.1 — `ValBinding`/`TypeReg` carregam `is_global`

```teko
// src/checker/scope.tks:24 — ValBinding ganha o bit:
type ValBinding = struct {
    name: str
    type: Type
    is_mut: bool
    ns: str
    is_const: bool
    vis: parser::Visibility
    /** is_global — the binding was declared `global` (§15): resolvable bare from ANY namespace, not
     *  just its own. False for locals and ordinary namespaced fns/consts (additive-inert). */
    is_global: bool
}
```

`define_fn` (`scope.tks:183`) e `define_const` (`:202`) ganham um parâmetro `is_global: bool` (os
call-sites em `collect.tks:335/352/409/471/2887/2915` passam `f.is_global`/`cd.is_global`; para métodos
`:409/:471` passam `false` — método não é global). `define` (locais, `:167`) passa sempre `false`.

```teko
// src/checker/resolve.tks:12 — TypeReg ganha o bit (derivado da decl):
/** is_global — the type was declared `global` (§15): bare-resolvable from any namespace. */
is_global: bool
```

Populado onde `TypeReg` é construído a partir da decl (`resolve.tks:475` `TypeReg { … vis = r.vis; decl
= r.decl }` → adicionar `is_global = r.decl.is_global`); as construções sintéticas/stamped
(`resolve.tks:1108/3137`, monomorph) passam `is_global = false` (instância monomorfizada não é global por
si; herda a bare-visibility do template só se o template for global — ver §3.3).

### 3.2 — Resolução bare de VALOR (fn/const): estender `call_binding_matches`

O único ponto semântico. Hoje (`scope.tks:348`):

```teko
fn call_binding_matches(b: ValBinding, callee: parser::Path, cur_ns: str, qualified: bool): bool {
    if !qualified { return b.ns.len == 0 || b.ns == cur_ns }
    b.ns.len != 0 && ns_matches_call_qualifier(b.ns, callee)
}
```

Passa a:

```teko
/**
 * … (doc existente) … Um binding `global` (§15) é alcançável bare de QUALQUER namespace, então o ramo
 * não-qualificado o aceita além do local (ns "") e do mesmo-ns. O ramo qualificado é intocado — um
 * `global` ainda é alcançável PELO caminho completo (é aditivo, não exclusivo).
 */
fn call_binding_matches(b: ValBinding, callee: parser::Path, cur_ns: str, qualified: bool): bool {
    if !qualified { return b.ns.len == 0 || b.ns == cur_ns || b.is_global }
    b.ns.len != 0 && ns_matches_call_qualifier(b.ns, callee)
}
```

Isso é o coração: `lookup_call`/`call_base_pos`/`call_ns`/`lookup_call_candidates` (todos em `scope.tks`,
compartilham `call_binding_matches`) passam a resolver um `global fn` de outra ns sem o qualificador —
**e `call_ns` devolve a ns REAL do global** (não ""), que é o que faz o codegen mangle para
`teko__io__println` (o ponto de fixpoint, §5). Para const/tipo-valor bare via `lookup_value_in_ns`
(`scope.tks:283`), adicionar simetricamente `|| b.is_global` ao teste de ns.

**Inerte:** nenhum binding é global ainda → byte-idêntico.

### 3.3 — Resolução bare de TIPO: espelhar em `resolve_bare_type_reg`

Um `global type` deve ser bare-visível de qualquer ns, como `str` hoje. Em `resolve.tks:640`
`resolve_bare_type_reg`, após o match de own-ns e o `resolve_root_scope_reg` (`:649`), inserir um terço
irmão:

```teko
/**
 * resolve_global_type_reg — a `global` type (§15) is bare-visible from ANY namespace, mirroring how a
 * root-scope type is. Scanned AFTER own-namespace and root (so a same-name local/root type still wins,
 * preserving today's precedence), arity-selective like the siblings.
 *
 * @param last   the referenced bare type name
 * @param table  the folded type table
 * @param arity  the reference's syntactic generic arity
 * @return       the global registration, or null
 */
fn resolve_global_type_reg(last: str, table: TypeTable, arity: u64): TypeReg | null {
    var cands = tt_cands(table, last)
    var k: u64 = 0
    loop {
        if k >= cands.count { break }
        var r = tt_cand_reg(table, cands, k)
        if r.name == last && r.is_global && r.decl.type_params.len == arity { return r }
        k++
    }
    null
}
```

Chamado em `resolve_bare_type_reg` entre `resolve_root_scope_reg` e `resolve_bare_g_instance`, e o mesmo
ramo bare adicionado a `type_ref_ns` (`resolve.tks:824-832`) para a ns vencedora sair correta.
**Precedência:** own-ns > root > **global** > g-instance — assim um tipo local de mesmo nome nunca é
sombreado por um global (preserva a semântica atual; a colisão de nomes entre globais é §3.4).

### 3.4 — Colisão de assinatura-global (o erro selado)

Regra (ruling 3): duas decls `global` de **mesma assinatura exportada** colidem; assinaturas diferentes
coexistem (overload). "Assinatura" = `(nome, lista de tipos de parâmetro)` — o mesmo critério do
`select_overload`/`env_is_overloaded` (§9 A), que já vive no checker. Enforço num passo novo após o
collect/seal, sobre `env.base_slots`:

```teko
/**
 * check_global_signature_collisions — enforce §15's collision rule: two `global` declarations sharing an
 * exported signature (same bare name AND same parameter-type list) are a COMPILE ERROR; different
 * signatures coexist as an overload set. Runs once over the sealed globals, after collect. O(g·k) over
 * the g global bindings grouped by name. Distinct-signature globals and non-global bindings are ignored.
 *
 * @param env    the sealed typing environment (globals in base_slots)
 * @return       the list of diagnostics (empty when every global signature is unique)
 */
fn check_global_signature_collisions(env: Env): []error {
    // group base_slots by (is_global && binding_is_func) name; within a group, any two whose Func params
    // are type-equal (reuse `func_params_equal`/the §9 A signature key) yield one diagnostic:
    //   "duplicate `global` signature for '<name>' — <ns_a> and <ns_b> export the same signature (§15).
    //    different signatures coexist as overloads; identical ones collide."
    // types/consts: a global type/const collides with another global of the SAME name (no params — the
    // name itself is the signature). Delegated to the type-table twin below.
}
```

Um irmão `check_global_type_collisions(table)` sobre `TypeTable` para `global type`/`global const`
(nome-como-assinatura; dois `global type Foo` colidem, `Foo` e `Foo<T>` NÃO colidem — aridade diferente é
assinatura diferente, coerente com §9 B). Ambos alimentam a lista `all-diagnostics` (pré-walk collect,
`all-diagnostics-pre-walk-0.3.1.md`) — coleta-tudo, não para no primeiro.

**Ordem de invocação:** logo após `seal` e após o collect do type-table, antes do item-walk — junto de
onde `check_modules` já roda (a fase de validação de módulos). Inerte hoje (nenhum global) → nada
dispara.

### 3.5 — Composição e as rejeições de checker

- **`global` + `exp`/`pub`/private:** ortogonal. `is_global` e `vis` são campos independentes; nada a
  cruzar aqui (o enforcement de `exp`/`pub` é §11).
- **`global` + `comptime`:** independente; quando §14 (comptime) landar, o slot já está no chain (§2.1).
- **`global` em membro de type body** (método/campo/const-de-tipo) → erro no collect
  (`collect.tks`, onde métodos são coletados, `:409`): *"`global` is not allowed on a type member —
  only on a namespace-level fn, type, or const (§15)"*. Fixture reject.
- **`global var`** → já barrado no parse (§2.3).

---

## 4. A migração shadow→global — inventário e tabela, símbolo a símbolo

O objetivo do §15: substituir CADA injeção (§1) por uma **declaração `global` real** na stdlib,
**preservando o símbolo alcançável**. A migração é o grosso do trabalho e é **por-símbolo**. Abaixo, o
inventário completo dos símbolos ambiente hoje injetados, agrupados por destino e por o que cada um
precisa para virar uma decl real sem quebrar o codegen (§5).

### 4.1 — Classe A: já têm decl REAL; falta só marcar `global` (baixo risco)

Os símbolos cujo corpo Teko JÁ existe no corpus, e cuja injeção é só um **fallback** que a decl real já
vence (`scope.tks:955-958` documenta isso para `str_from_utf8`). Marcar a decl `global` e **remover a
entrada do fallback**. Alvos: a superfície `teko::text` (`bytes_of_str`/`str_from_utf8` — decl em
`src/text/text.tks`), a superfície host `teko::env/io/fs/process` (decls reais em
`src/env|io|fs|process/*.tks`, hoje alcançadas pelo env-lookup ANTES do `host_surface_fn`,
`scope.tks:1050-1053`). Ação: `global exp fn …` nessas decls + apagar o ramo do fallback
correspondente.

### 4.2 — Classe B: superfície str/byte self-host e `teko::list` (médio — corpo existe no runtime)

`slice/str/str_of_bytes/one_byte/str_concat/str_hash/str_compare/i64_to_str/u64_to_str/concat/slice_to/
slice_from/len/chars/len_chars/char_at/str_slice_chars/is_alpha/is_digit/is_space/to_lower/to_upper/
ends_with/contains/last_index_of` (todos em `builtin_fn`, `scope.tks:1032-1107`) + as ops de
`teko::list` (injetadas/reservadas, `core.tks:25-40`). O corpo existe como twin de runtime
(`tk_str_*`, `teko_rt.tks`). Migrar = declarar `global fn` em `src/str`/`src/list`/`src/text` cujo corpo
**bottom-a** no twin — o que ACOPLA a §16 (o twin, hoje `tk_*` do `teko_rt`, vira ou função Teko pura ou
`extern` libc). Ver §5 para por que esses NÃO são byte-idênticos sem cuidado.

### 4.3 — Classe C: intrínsecos e sinks host (alto — não são "funções" no sentido usual)

- **`panic`/`exit`** (`typer.tks:2867-2891`): já rotulados "injected global". Viram `global fn panic(m:
  error | str)` / `global fn exit(code)` com lowering intrínseco preservado (o codegen já os intercepta
  por nome/divergência, `typer.tks:4953-4980`). A decl `global` dá VISIBILIDADE; o lowering fica.
- **`sizeof<T>()`** (o exemplo-título do §15): **não existe hoje** como builtin Teko (só o `sizeof` do C
  em codegen). É um `global comptime` NOVO a introduzir junto do §14 (comptime) — projetá-lo aqui como
  `exp global comptime sizeof<T>(): usize`, corpo intrínseco no comptime-fold. **Bloqueado no §14.**
- **format helpers** `fmt_*`, **coverage sinks** `cov_*`, **arena/task/intern** `arena_*/task_reset/
  intern_*`, **marshalling** `as_ptr/as_cstr/str_from_c/bytes_from_ptr/load_u64/store_u64/buf_ptr/
  region_buf`, aritméticos `fdiv/floor/f64_bits/f64_from_bits/panic_*`: são **side-channels/intrínsecos
  do host**, não superfície de usuário. Ruling do dono (§15): a superfície AMBIENTE vira global. Estes
  são internos (usados só pelo compilador/harness), não "ambiente de usuário" — a recomendação law-first
  é: migram para decls `global` INTERNAS (`global` sem `exp` — alcançável bare, mas fora do self-`.tkh`),
  OU permanecem intrínsecos nomeados. **Sub-decisão gated na triagem `exp`/interno do plano-stdlib**
  (§6.1 de lá). Projetado, não executado.

### 4.4 — Classe T: os TIPOS ambiente (`builtin_type`, `scope.tks:566`)

`str/byte/char/error/ptr/uptr/size/usize` e os prims `u8..i64/f32/f64/bool`. O §15 confirma
(`linha 1203`): `str`/`error` já são ambiente; agora **explícito** via `global type`. **PORÉM** —
`core.tks:3-7/43-44` legisla que `byte/str/char/error` são **PREDEFINED, INJECTED e RESERVED** (rep
injetada no codegen; `M.1` exclusão-por-construção). Tensão (§5.3): torná-los `global type` reais exige
um lar de declaração com a rep — o que ACOPLA à representação de codegen e ao `core`. Recomendação
law-first: os **prims** (`u8..bool`) e `size/usize/ptr/uptr` ficam **reservados-e-injetados** (não têm
corpo declarável — são a base do sistema de tipos; declarar `global type u8` não faria sentido); os
**derivados** (`str/byte/char/error`) tornam-se `global type` com corpo mínimo em `src/core` SÓ quando a
rep puder ser expressa em Teko (acopla §16). Até lá, permanecem injeção reservada — o §15 é satisfeito
para a superfície de FUNÇÃO primeiro; os tipos-núcleo são o último degrau.

### 4.5 — `teko::assert::*` (`assert_builtin_fn`, `scope.tks:833`)

A família assert é resolvida por uma lista única (`assert_builtin_name`, o teste
`assert_builtin_test.tkt`). É superfície de harness, namespaced (`teko::assert::is_true`), não bare —
então NÃO precisa de `global` para o comportamento atual. Migra por completude (decls reais em
`src/assert/assert.tks`) mas é **Classe A** de risco (a decl real já existiria; o fallback é o shadow).
Baixa prioridade; fora do caminho crítico do §15 (que é sobre acesso BARE).

---

## 5. Fixpoint-safety — o MECANISMO é byte-idêntico; a MIGRAÇÃO não é (e como sequenciar)

### 5.1 — O mecanismo (crumbs G1–G4) é byte-idêntico por construção

Adicionar `is_global` (default false) + o `|| b.is_global` nos ramos bare + os passos de colisão (que não
disparam sem globais): **nenhum binding é global ainda**, então cada predicado devolve exatamente o que
devolvia. O reseed G4 captura um seed idêntico em comportamento (só maior em superfície de gramática).
Prova: as tabelas de decisão (`call_binding_matches`, `resolve_bare_type_reg`) só ganham uma disjunção
cujo operando é constantemente `false` no corpus pré-migração.

### 5.2 — A migração por-símbolo NÃO é byte-idêntica — este é o perigo

O codegen decide `tk_*` (runtime twin) vs `teko__ns__nome` (símbolo de usuário) por **`call_ns`**:
`cg_call_is_user_declared` (`codegen.tks:4158`) = `c.call_ns.len != 0 && <corpo existe>`. Hoje um builtin
tem `call_ns == ""` → roteia para o twin `tk_*` (`emit_call`, `codegen.tks:4166`). No momento em que
`println` vira `global fn println` em `src/io`:

1. `lookup_call` acha a decl real → `call_ns == "teko::io"` (não mais "");
2. `cg_call_is_user_declared` passa a `true` (se houver corpo) → codegen emite `teko__io__println(...)`
   em vez do `tk_*`/intrínseco — **muda o símbolo emitido** e **exige que o corpo exista**.

Ou seja: **mover de injeção para declaração muda o mangle e a rota de emissão.** Não é uma reescrita de
grafia; é uma troca de quem provê o corpo. Portanto cada símbolo migrado precisa que sua decl `global`
**bottom-e no mesmo runtime** — via `extern` (o símbolo `tk_*`/libc, §16) ou via corpo Teko puro que
compila hoje. Enquanto o corpo não existir de forma equivalente, a migração daquele símbolo **regride**.

**Conclusão de sequenciamento:** o MECANISMO landa e reseeda primeiro (inerte). A MIGRAÇÃO roda
**símbolo a símbolo**, cada um sob ritual próprio, e a maioria (Classe B/C/T) fica **BLOQUEADA em §16**
(morte do `teko_rt`: quando o twin vira `extern` libc ou função Teko, a decl `global` tem onde
bottom-ar) **e na triagem `exp`/interno do plano-stdlib** (Classe C: `global exp` vs `global` interno).
A Classe A (fallback que a decl real já vence) é migrável ASSIM QUE o mecanismo existe — é o piloto.

### 5.3 — Ordem/mangle: um cuidado a mais

Dois globais de mesmo nome em ns diferentes viram um **conjunto de overload cross-namespace** (§3.2 faz
ambos candidatos bare). `select_overload` (§9 A) precisa então rodar sobre candidatos de ns distintas — o
que já é suportado (`lookup_call_candidates` varre base + locals), mas o **sufixo de overload**
(`overload_suffix_for_binding`) deve ser estável sob a inclusão de globais de outra ns. Verificar que o
sufixo é função só de `(ns, params)` (é) — então determinístico. A colisão de assinatura-IDÊNTICA (§3.4)
é justamente o que impede o caso ambíguo insolúvel.

---

## 6. Sweep `.tkt` / `.tkr`

- **`.tkt` (testes de unidade):** todo literal que constrói `parser::Function`/`TypeDecl`/`ConstDecl`
  ou `checker::TFunction`/`TConstDecl` ganha `is_global = false` (ver `consteval_test.tkt`,
  `metrics_test.tkt`, `residence_test.tkt`, `pt_census_test.tkt`, `test_assert_test.tkt`). É mecânico,
  parseável pelo seed atual → entra no Crumb G1 junto do campo AST. Novos testes de unidade:
  `scope`/`resolve` para `call_binding_matches`/`resolve_bare_type_reg` com `is_global = true` (um binding
  de ns "a" resolve bare de ns "b"); e o teste de `check_global_signature_collisions`.
- **`.tkr` (regressões executáveis):** adicionar as fixtures do §7. As positivas (acesso bare) cabem
  no canal `own_native` ou numa nova fixture executável pequena; as negativas no canal
  `diagnostics` (checker-stage) e `parse_diagnostics` (parse-stage) — um `cases/*.tks` por rejeição,
  uma cena por diagnóstico (o padrão selado "um build falho reporta todos", `diagnostics.tkr`).
- **Sem-shadow, doc-comments:** cada decl `global` migrada carrega Javadoc completo (Lei W15) — o próprio
  ganho do §15 é o dev PASSAR A VER o que era shadow, então o doc-comment não é opcional.

---

## 7. Fixtures (inputs → exit codes nativos)

### 7.1 — ACEITAR (positivas, executáveis — canal `own_native` / fixture nova)

| # | fixture | conteúdo | esperado |
|---|---|---|---|
| P1 | `global_fn_unqualified` | `global fn ping(): i32 { 7 }` em ns `a`; `fn main()` em ns `b` chama `ping()` bare e `exit(ping())` | exit **7** — global alcançável sem qualificar |
| P2 | `global_fn_also_qualified` | mesmo, mas `main` chama `a::ping()` pelo caminho completo | exit **7** — acesso qualificado segue válido (aditivo) |
| P3 | `global_type_unqualified` | `global type Tag = struct { n: i32 }` em ns `a`; ns `b` declara `var t = Tag { n = 5 }` e retorna `t.n` | exit **5** — tipo global bare-visível de outra ns |
| P4 | `global_overload_distinct_sig` | `global fn f(x: i32): i32` e `global fn f(x: i32, y: i32): i32` em ns distintas; `main` chama ambas bare | exit = soma esperada — assinaturas distintas coexistem |
| P5 | `global_composes_exp` | `exp global fn g(): i32 { 3 }`; consumidor bare | exit **3** — `exp` + `global` compõem |

### 7.2 — REJEITAR (negativas — `diagnostics` / `parse_diagnostics`)

| # | fixture | conteúdo | diagnóstico |
|---|---|---|---|
| R1 | `global_on_var_rejected` (parse) | `global var x = 0` no topo | "`global` is not allowed on a variable — only on a namespace-level fn, type, or const (§15)" |
| R2 | `global_signature_collision_rejected` (checker) | `global fn h(x: i32): i32` em ns `a` E `global fn h(x: i32): i32` em ns `b` (assinatura idêntica) | "duplicate `global` signature for 'h' … same signature (§15)" |
| R3 | `global_type_collision_rejected` (checker) | dois `global type Dup = struct {…}` em ns distintas | "duplicate `global` type 'Dup' (§15)" |
| R4 | `global_on_type_member_rejected` (checker) | `global fn` dentro de um `type … { … }` body | "`global` is not allowed on a type member (§15)" |

> **Prova por aritmética** (padrão dos `.tkr` existentes, ex. `overload_resolve.tkr`): as positivas
> somam num único witness que só a resolução correta produz; um relapso move o total.

---

## 8. Sequência de crumbs, ritual e reseed

Cada crumb é o menor passo gate-ável independentemente. Ritual = gate completo (a suíte de regressão
canônica + self-build) DEVE passar. **NUNCA rodar `teko test`** neste worktree (crash do `monomorph`,
por decreto da tarefa) — o gate é acionado pelo integrador/CI, não por este autor.

### Fase MECANISMO (inerte, byte-idêntico — desbloqueado hoje)

- **G0 — scaffolding + doc (compila hoje).** Este documento. Nenhuma edição de produto.
- **G1 — bit AST + sweep de literais.** Adiciona `is_global` a `Function`/`TypeDecl`/`ConstDecl` (e aos
  espelhos `TFunction`/`TConstDecl` se necessário para fluxo), default `false`; varre TODOS os literais
  (`src/**` + `.tkt`) com `is_global = false`. Parseável pelo seed atual. **Ritual.**
- **G2 — lexer + parser aditivos.** `TokenKind::Global`, `keyword_kind`, `consume_global_modifier`,
  threading para os três construtores + as rejeições de parse (`global var`). Aceita `global` sem ninguém
  o usar. **Ritual.**
- **G3 — resolução + colisão (inerte).** `ValBinding.is_global`/`TypeReg.is_global`, o `|| b.is_global`
  em `call_binding_matches`/`lookup_value_in_ns`, `resolve_global_type_reg` em `resolve_bare_type_reg`/
  `type_ref_ns`, `check_global_signature_collisions` + `check_global_type_collisions` ligados ao
  all-diagnostics. Testes de unidade `.tkt` (§6). Zero decl global no corpus → byte-idêntico. **Ritual.**
- **G4 — RESEED.** Captura o seed que fala `global`. A partir daqui `.tks` pode escrever `global`.
  **Ritual + reseed.**

### Fase MIGRAÇÃO (piloto agora; grosso gated)

- **G5 — piloto Classe A.** Migra 1–2 símbolos cujo fallback a decl real já vence (ex.
  `teko::text::str_from_utf8`, uma superfície host): marca a decl `global`, remove a entrada do fallback,
  adiciona as fixtures P1/P2. Prova a rota fim-a-fim SEM tocar codegen-de-runtime. **Ritual.**
- **G6..Gn — migração por-símbolo.** Um símbolo (ou grupo coeso) por crumb, cada um com sua fixture,
  cada um sob ritual. **BLOQUEADO:** Classe B/C/T dependem de §16 (morte do `teko_rt` → o twin vira
  `extern`/Teko puro, dando onde a decl `global` bottom-ar) e da triagem `exp`/interno do
  **plano-stdlib** §6.1 (Classe C: `global exp` vs `global` interno). `sizeof<T>` depende de §14
  (comptime). Estes ficam desenhados (§4) e resumem em minutos quando os deps fecharem.
- **Gω — remoção do shadow.** Quando o último símbolo ambiente tiver decl `global`, deletar
  `builtin_fn`/`builtin_type`/`host_surface_fn`/`assert_builtin_fn` e a injeção `panic`/`exit` do typer.
  É o "no-shadow completo" do §15. **Ritual + reseed.**

---

## 9. Riscos, tensões de lei, e o que fica bloqueado

- **T1 — `@sizeof` (texto §15).** Resolvido law-first como acesso bare (§0.1); ponto de ratificação leve
  ao dono, não HALT.
- **T2 — mangle/rota de emissão muda na migração (§5.2).** O maior risco. Mitigado por sequenciar
  MECANISMO(inerte)→RESEED→MIGRAÇÃO(por-símbolo, cada um bottom-ando no mesmo runtime). A migração NÃO é
  byte-idêntica; é comportamento-preservante por-símbolo, provada por fixture executável.
- **T3 — tipos-núcleo reservados-e-injetados (`core.tks`, §4.4).** `str/byte/char/error` são
  PREDEFINED/RESERVED/INJECTED por `M.1`. Torná-los `global type` reais acopla à rep de codegen/§16;
  recomendação: função primeiro, tipos-núcleo por último (ou permanecem injeção reservada, que o §15
  admite como o degrau final). Não é tensão insolúvel — é ordem.
- **T4 — Classe C interno vs `exp` (§4.3).** Sinks/intrínsecos não são "ambiente de usuário". A
  sub-decisão `global exp` vs `global` interno é da triagem do **plano-stdlib** §6.1 — declarada como
  dependência, não decidida aqui.
- **BLOQUEIOS honestos:** §16 (libc-direct / morte do `teko_rt`) para as Classes B/T; plano-stdlib
  (triagem `exp`) para a Classe C; §14 (comptime) para `sizeof<T>`. Tudo o que NÃO depende disso — o
  mecanismo inteiro (G1–G4), o piloto Classe A (G5), as fixtures, os passos de colisão — está desenhado e
  é executável agora.

Nenhuma tensão genuína e insolúvel resta. **Sem HALT.**
