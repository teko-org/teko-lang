# Teko — O Checker (`src/checker/*`), em Teko + C23

> **Correção doutrinária (alinhada a TEKO_HISTORY §B.37).** Este snapshot congelado
> introduziu, por deriva, o tipo `Unit` (struct vazio "value-less") e usava as formas
> superadas `Error` / `Unit | error` / `-> Unit`. Conforme as quatro regras de §B.37:
> `Unit` **deixa de existir** — "não retorna valor" é `void`, um **marcador de retorno**
> (nunca tipo/valor/membro/binding); `error` é o tipo **nativo minúsculo** (supera
> `Error`); um check falível-sem-valor é `-> error?` (null = ok), nunca `Unit | error`
> nem `void | error`. As ocorrências abaixo foram corrigidas para espelhar o
> `src/checker/type.tks` reconstruído; a lógica é preservada — só as formas que violavam
> as regras mudaram.

O **checker** roda entre o parser e o backend: recebe a **AST** e devolve a **AST
tipada** (anota tipos resolvidos + valida). *"O parser registra estrutura, o checker
dá semântica."* Ele **não desugar** (isso é do codegen) e **não muda a estrutura**.
A árvore que ele produz é a **entrada do backend** e o **conteúdo do `.tkb`** (a
serialização). Construído em **etapas** pra não estourar a sessão.

---

## Decisões do checker (tomadas; corrija se discordar)

1. **Modelo de `Type` semântico** — uma variante (abaixo). Tudo checa contra ele.
2. **Saída — a AST tipada COMPLETA, sem lacunas.** Cada nó carrega seu tipo
   resolvido, numa árvore **única e autossuficiente** (não uma camada de anotação à
   parte). O `.tkb` é essa árvore **serializada**; desserializado, é **exatamente a
   mesma informação** que o codegen recebe — diferença **ZERO**.
3. **Inferência** — **local** (do inicializador: `let x = e`). Bidirecional só se um
   caso real forçar (M.5).
4. **Passes** — **2 passes**: (1) coletar os itens top-level (tipos, funções) no
   escopo; (2) checar os corpos. Necessário pra *forward reference* no módulo.
5. **Escopo** — léxico; nomes de tipo **reservados** (B.19, fechado); shadowing de
   variáveis normais **permitido** (escopo aninhado).
6. **Ownership** — **mínimo no seed** (o padrão funcional consume-return + as regras
   de `mut` bastam); regiões/lifetimes finos = LTS (veredito da arena).
7. **Contra-validação** — antes do backend, o checker **re-valida a árvore final
   completa** (e a mesma validação roda num `.tkb` desserializado). Garante que o
   `.tkb` gerado **foi validado antes de existir**, e detecta **corrupção** num
   `.tkb` lido. Defesa em profundidade (M.1) — o `.tkb` não pode ter lacunas.

---

## Regra de segurança — acesso por nullable (M.1), registrada para quando `T?` chegar

Acessar um campo através de um nullable sem narrowing é null-deref → segfault. Por
**M.1 (exclusão-por-construção)** — a técnica das palavras reservadas — isso é
**inexprimível**:

- `x.campo` quando `x: T?` é **erro de compilação** (o checker conhece a nulidade).
- O acesso só se dá por **safe-access `x?.campo`** (rende `U?`, nulo se `x` é nulo —
  o "elvis"/safe-nav), **ou** após **narrowing por fluxo** (`if x != null { x.campo }`
  — nesse ramo `x: T`, não-nulo).

`T?` e o operador `?.` são **diferidos** (o seed usa flags `has_X: bool`, não
nullables — precedente bitwise), então a regra entra **com** a nulidade (alpha+).
Distinta do `?` de propagação de erro, **banido** por B.16.

---

## Etapa 1 — o modelo de `Type` semântico

O que `TypeExpr` (sintático, do parser) **resolve para**. Nominal (B.13): tipos
nomeados são iguais por **nome**, não por estrutura. (`char` é alpha — B.36 — então
fora do seed; nullability adiciona um caso `Nullable` quando chegar.)

### Teko — `src/checker/type.tks`

```teko
// src/checker/type.tks  (namespace 'teko::checker')

// the injected scalar primitives (B.19) — distinct real types.
type PrimKind = enum {
    U8; U16; U32; U64
    I8; I16; I32; I64
    Bool
}

// Doctrinal correction (TEKO_HISTORY §B.37; mirrors src/checker/type.tks):
//   - `Unit` (the value-less struct) is EXCISED. "Returns no value" is `void`.
//   - `void` is a RETURN-ONLY marker: legal ONLY as `Func.ret`. It is never a
//     value, never a binding type, never a variant member.
//   - `error` is the native lowercase type — it SUPERSEDES the old capitalized `Error`.
//   - `Optional` (`T?`) is the built-in unary type-former for nullability; a variant
//     member may NOT be optional.

// the cases that carry data (Teko has no payload on a bare enum — a variant case
// is a struct). Byte/Str/error/void are markers (no payload).
type Prim     = struct { kind: PrimKind }
type Slice    = struct { element: Type }       // []T — recursive (B.8)
type Optional = struct { inner: Type }         // T? — built-in nullable former (like Slice)
type Named    = struct { name: str }           // a user type, equal by NAME (nominal)
type Variant  = struct { members: []Type }     // A | B | … (two or more); members are COMPLETE types
type Func     = struct { params: []Type; ret: Type }   // (params) -> ret  (ret may be void)
type Byte     = struct { }                     // the octet type
type Str      = struct { }                     // validated UTF-8
type Error    = struct { }                     // the native `error` (lowercase; supersedes `Error`)
type Void     = struct { }                     // return-only marker; legal ONLY as Func.ret

// a semantic type. (Compiler-managed indirection for the recursive cases.)
type Type = Prim | Byte | Str | Slice | Optional | Named | Variant | Func | Error | Void

// nominal type equality (B.13): structural over the shape, but Named is by name.
fn type_eq(a: Type, b: Type) -> bool {
    match a {
        Prim as pa    => match b { Prim as pb    => pa.kind == pb.kind;             _ => false }
        Byte          => match b { Byte          => true;                           _ => false }
        Str           => match b { Str           => true;                           _ => false }
        Error         => match b { Error         => true;                           _ => false }  // native `error`
        Void          => match b { Void          => true;                           _ => false }  // return-only marker
        Slice as sa   => match b { Slice as sb   => type_eq(sa.element, sb.element); _ => false }
        Optional as oa => match b { Optional as ob => type_eq(oa.inner, ob.inner);  _ => false }
        Named as na   => match b { Named as nb   => na.name == nb.name;             _ => false }
        Variant as va => match b { Variant as vb => types_eq(va.members, vb.members); _ => false }
        Func as fa    => match b {
            Func as fb => type_eq(fa.ret, fb.ret) && types_eq(fa.params, fb.params)
            _          => false
        }
    }
}

// element-wise equality of two type lists (same length, each equal in order).
fn types_eq(xs: []Type, ys: []Type) -> bool {
    if xs.len != ys.len { return false }
    mut i = 0
    loop {
        if i >= xs.len { break }
        if !type_eq(xs[i], ys[i]) { return false }
        i++
    }
    true
}
```

### C23 — `src/checker/type.h`

```c
// src/checker/type.h — the checker's semantic Type model. Mirrors type.tks.
#ifndef TK_CHECK_TYPE_H
#define TK_CHECK_TYPE_H

#include "../core.h"
#include "../text/text.h"   // tk_str
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TK_PRIM_U8, TK_PRIM_U16, TK_PRIM_U32, TK_PRIM_U64,
    TK_PRIM_I8, TK_PRIM_I16, TK_PRIM_I32, TK_PRIM_I64,
    TK_PRIM_BOOL,
} tk_prim_kind;

typedef enum {
    TK_TYPE_PRIM, TK_TYPE_BYTE, TK_TYPE_STR, TK_TYPE_SLICE, TK_TYPE_OPTIONAL,
    TK_TYPE_NAMED, TK_TYPE_VARIANT, TK_TYPE_FUNC, TK_TYPE_ERROR, TK_TYPE_VOID,
    // TK_TYPE_VOID is the return-only marker (supersedes the excised TK_TYPE_UNIT);
    // TK_TYPE_ERROR is the native lowercase `error`. (TEKO_HISTORY §B.37.)
} tk_type_tag;

// recursive (the Slice/Variant/Func cases hold tk_type) — the indirection the Teko
// side keeps compiler-managed shows here as a forward declaration + pointers.
typedef struct tk_type tk_type;

struct tk_type {
    tk_type_tag tag;
    union {
        tk_prim_kind prim;                                   // TK_TYPE_PRIM
        struct { tk_type *element; }            slice;       // TK_TYPE_SLICE
        struct { tk_type *inner; }              optional;    // TK_TYPE_OPTIONAL (T?)
        struct { tk_str name; }                 named;       // TK_TYPE_NAMED (nominal)
        struct { tk_type *members; size_t len; } variant;    // TK_TYPE_VARIANT
        struct { tk_type *params; size_t nparams; tk_type *ret; } func;  // TK_TYPE_FUNC
        // BYTE, STR, ERROR, VOID carry no payload
    } as;
};

bool tk_type_eq(const tk_type *a, const tk_type *b);

#endif // TK_CHECK_TYPE_H
```

### C23 — `src/checker/type.c`

```c
// src/checker/type.c — nominal type equality (B.13).
#include "type.h"

static bool tk_str_eq(tk_str a, tk_str b) {
    if (a.len != b.len) return false;
    for (size_t i = 0; i < a.len; i += 1) {
        if (a.ptr[i] != b.ptr[i]) return false;
    }
    return true;
}

static bool tk_types_eq(const tk_type *xs, size_t nx, const tk_type *ys, size_t ny) {
    if (nx != ny) return false;
    for (size_t i = 0; i < nx; i += 1) {
        if (!tk_type_eq(&xs[i], &ys[i])) return false;
    }
    return true;
}

bool tk_type_eq(const tk_type *a, const tk_type *b) {
    if (a->tag != b->tag) return false;          // different shapes → not equal
    switch (a->tag) {
        case TK_TYPE_PRIM:  return a->as.prim == b->as.prim;
        case TK_TYPE_BYTE:  return true;
        case TK_TYPE_STR:   return true;
        case TK_TYPE_ERROR: return true;
        case TK_TYPE_VOID:  return true;
        case TK_TYPE_SLICE: return tk_type_eq(a->as.slice.element, b->as.slice.element);
        case TK_TYPE_OPTIONAL: return tk_type_eq(a->as.optional.inner, b->as.optional.inner);
        case TK_TYPE_NAMED: return tk_str_eq(a->as.named.name, b->as.named.name);   // nominal
        case TK_TYPE_VARIANT:
            return tk_types_eq(a->as.variant.members, a->as.variant.len,
                               b->as.variant.members, b->as.variant.len);
        case TK_TYPE_FUNC:
            return tk_type_eq(a->as.func.ret, b->as.func.ret) &&
                   tk_types_eq(a->as.func.params, a->as.func.nparams,
                               b->as.func.params, b->as.func.nparams);
    }
    return false;
}
```

---

## Etapa 2 — o ambiente (escopo de valores + tipos injetados)

O **escopo de valores** é uma **lista plana** de bindings: definir é **anexar**, e o
`lookup` anda do fim pro começo — então um binding mais novo **sombreia** um mais
antigo (léxico), e o **escopo de bloco sai de graça**: um bloco estende o ambiente
localmente, e ao voltar da recursão o chamador ainda segura o **prefixo** dele (os
bindings do bloco eram extensão local). Os **tipos injetados** (B.19) resolvem por
nome — sem `type` fonte, é o compilador que os conhece.

### Teko — `src/checker/scope.tks`

```teko
// src/checker/scope.tks  (namespace 'teko::checker')

// a value binding visible in scope: a name and its type.
type ValBinding = struct {
    name:   str
    type:   Type
    is_mut: bool      // (B.21): true ONLY for a `mut` local; let/const/params/match-bindings = false
}

// the environment: a FLAT list of bindings. A later binding shadows an earlier of
// the same name (lexical); block scoping is implicit (the caller keeps its prefix
// when the recursion returns — the block's bindings were a local extension).
type Env = []ValBinding

// define a name (append); returns the extended env (ref-less consume-return). `is_mut`
// comes from the call site: a `mut` binding passes true; EVERYTHING else (let, const,
// params, match-bindings, function signatures) passes false (B.21).
fn define(env: Env, name: str, t: Type, is_mut: bool) -> Env {
    teko::list::push(env, ValBinding { name = name; type = t; is_mut = is_mut })
}

// a binding is mutable iff declared `mut` (Let and Const are immutable — B.21).
fn bind_is_mut(k: BindKind) -> bool { k == BindKind::Mut }

// the WHOLE binding (type + mutability), innermost first — for the `mut` write-guard (B.21).
fn lookup_binding(env: Env, name: str) -> ValBinding | error {
    mut i = env.len
    loop {
        if i == 0 { break }
        i = i - 1
        if env[i].name == name { return env[i] }
    }
    error { message = $"undefined name: {name}" }
}

// the type of a name — the common case; a thin wrapper so existing callers are unchanged.
fn lookup(env: Env, name: str) -> Type | error {
    match lookup_binding(env, name) { ValBinding as b => b.type; error as e => e }
}

// resolve an INJECTED built-in type name to its Type (B.19). User types
// (struct/enum/variant) resolve against the collected registry (Etapa 3).
fn builtin_type(name: str) -> Type | error {
    if name == "u8"   { return Prim { kind = PrimKind::U8 } }
    if name == "u16"  { return Prim { kind = PrimKind::U16 } }
    if name == "u32"  { return Prim { kind = PrimKind::U32 } }
    if name == "u64"  { return Prim { kind = PrimKind::U64 } }
    if name == "i8"   { return Prim { kind = PrimKind::I8 } }
    if name == "i16"  { return Prim { kind = PrimKind::I16 } }
    if name == "i32"  { return Prim { kind = PrimKind::I32 } }
    if name == "i64"  { return Prim { kind = PrimKind::I64 } }
    if name == "bool" { return Prim { kind = PrimKind::Bool } }
    if name == "byte"  { return Byte { } }
    if name == "str"   { return Str { } }
    if name == "error" { return Error { } }
    error { message = "not a built-in type" }
}
```

### C23 — `src/checker/scope.h`

```c
// src/checker/scope.h — the checker's value environment + injected types.
#ifndef TK_CHECK_SCOPE_H
#define TK_CHECK_SCOPE_H

#include "type.h"
#include "../core.h"   // TK_LIST, TK_RESULT

typedef struct { tk_str name; tk_type type; bool is_mut; } tk_val_binding;  // is_mut — B.21
TK_LIST(tk_val_binding, tk_env);        // a flat list; later bindings shadow earlier

TK_RESULT(tk_type, tk_type_result);            // Type | error
TK_RESULT(tk_val_binding, tk_binding_result);  // ValBinding | error (carries is_mut — B.21)
TK_RESULT(tk_env, tk_env_result);              // Env | error (env-threading; used by match.c + the typed pass)

tk_env            tk_env_define(tk_env env, tk_str name, tk_type t, bool is_mut);
tk_binding_result tk_env_lookup_binding(tk_env env, tk_str name);  // the whole binding (mut guard)
tk_type_result    tk_env_lookup(tk_env env, tk_str name);          // the type (thin wrapper)
tk_type_result    tk_builtin_type(tk_str name);
bool              tk_bind_is_mut(tk_bind_kind k);   // k == TK_BIND_MUT (Let/Const immutable — B.21)

#endif // TK_CHECK_SCOPE_H
```

### C23 — `src/checker/scope.c`

```c
// src/checker/scope.c
#include "scope.h"
#include <string.h>

static bool name_eq(tk_str n, tk_str m) {
    return n.len == m.len && memcmp(n.ptr, m.ptr, n.len) == 0;
}
static bool name_is(tk_str n, const char *lit) {
    size_t L = strlen(lit);
    return n.len == L && memcmp(n.ptr, lit, L) == 0;
}
static tk_type prim(tk_prim_kind k) {
    return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k };
}

tk_env tk_env_define(tk_env env, tk_str name, tk_type t, bool is_mut) {
    return tk_env_push(env, (tk_val_binding){ .name = name, .type = t, .is_mut = is_mut });
}

bool tk_bind_is_mut(tk_bind_kind k) { return k == TK_BIND_MUT; }   // Let/Const immutable (B.21)

tk_binding_result tk_env_lookup_binding(tk_env env, tk_str name) {
    for (size_t i = env.len; i > 0; i -= 1) {        // innermost (most recent) first
        tk_val_binding b = env.ptr[i - 1];
        if (name_eq(b.name, name)) return (tk_binding_result){ .ok = true, .as.value = b };
    }
    return (tk_binding_result){ .ok = false, .as.error = tk_error_make("undefined name") };
}

tk_type_result tk_env_lookup(tk_env env, tk_str name) {       // the type — thin wrapper
    tk_binding_result r = tk_env_lookup_binding(env, name);
    if (!r.ok) return (tk_type_result){ .ok = false, .as.error = r.as.error };
    return (tk_type_result){ .ok = true, .as.value = r.as.value.type };
}

tk_type_result tk_builtin_type(tk_str name) {
    tk_type t;
    if      (name_is(name, "u8"))    t = prim(TK_PRIM_U8);
    else if (name_is(name, "u16"))   t = prim(TK_PRIM_U16);
    else if (name_is(name, "u32"))   t = prim(TK_PRIM_U32);
    else if (name_is(name, "u64"))   t = prim(TK_PRIM_U64);
    else if (name_is(name, "i8"))    t = prim(TK_PRIM_I8);
    else if (name_is(name, "i16"))   t = prim(TK_PRIM_I16);
    else if (name_is(name, "i32"))   t = prim(TK_PRIM_I32);
    else if (name_is(name, "i64"))   t = prim(TK_PRIM_I64);
    else if (name_is(name, "bool"))  t = prim(TK_PRIM_BOOL);
    else if (name_is(name, "byte"))  t = (tk_type){ .tag = TK_TYPE_BYTE };
    else if (name_is(name, "str"))   t = (tk_type){ .tag = TK_TYPE_STR };
    else if (name_is(name, "error")) t = (tk_type){ .tag = TK_TYPE_ERROR };
    else return (tk_type_result){ .ok = false, .as.error = tk_error_make("not a built-in type") };
    return (tk_type_result){ .ok = true, .as.value = t };
}
```

---

## Etapa 3 — resolução de tipos + o passe de coleta (passe 1)

`resolve_type` converte um `TypeExpr` (sintático, do parser) num `Type` (semântico):
nome → built-in (B.19) ou tipo de usuário (o **registro**); slice/union recursivos
(B.8). O **passe 1** coleta **todos os nomes top-level primeiro** (tipos, depois
assinaturas de função) — só assim uma `fn` pode chamar outra definida **abaixo**, e
um tipo pode referenciar outro à frente.

> O C depende da AST do parser em C (`../parser/ast.h`: `tk_type_expr`, `tk_type_decl`,
> `tk_item`, `tk_function`, `tk_program`, `tk_path`, `tk_param`) — a gerar no port do
> parser; uso os nomes/tags consistentes.

### Teko — `src/checker/resolve.tks`

```teko
// src/checker/resolve.tks  (namespace 'teko::checker')

// a registered user type: its name + its parsed declaration (for later lookup).
type TypeReg   = struct { name: str; decl: TypeDecl }   // TypeDecl from the parser
type TypeTable = []TypeReg

// find a user type by name; error if not registered.
fn type_table_find(table: TypeTable, name: str) -> TypeDecl | error {
    mut i = 0
    loop {
        if i >= table.len { break }
        if table[i].name == name { return table[i].decl }
        i++
    }
    error { message = "not a user type" }
}

// resolve a NAMED type (a path) to a semantic Type: built-in or user type.
fn resolve_named(path: Path, table: TypeTable) -> Type | error {
    let name = path.segments[path.segments.len - 1].name   // seed: last segment
    match builtin_type(name) {                  // u8…u64, byte, str, error, …
        Type as t => return t
        error     => {}
    }
    match type_table_find(table, name) {        // a user struct/enum/variant
        TypeDecl => return Named { name = name }
        error    => {}
    }
    error { message = $"unknown type: {name}" }
}

// resolve a syntactic TypeExpr to a pure semantic Type (recursive — B.8).
fn resolve_type(te: TypeExpr, table: TypeTable) -> Type | error {
    match te {
        NamedType as nt => resolve_named(nt.path, table)
        SliceType as st => {
            let el = match resolve_type(st.element, table) {
                Type as t  => t
                error as e => return e
            }
            Slice { element = el }
        }
        UnionType as ut => {
            mut members = teko::list::empty()
            mut i = 0
            loop {
                if i >= ut.members.len { break }
                let m = match resolve_type(ut.members[i], table) {
                    Type as t  => t
                    error as e => return e
                }
                members = teko::list::push(members, m)
                i++
            }
            Variant { members = members }
        }
    }
}
```

### Teko — `src/checker/collect.tks`

```teko
// src/checker/collect.tks  (namespace 'teko::checker') — pass 1 (forward refs).

// the top-level environment after collecting: user types + function signatures.
type Collected = struct { types: TypeTable; env: Env }

// gather ALL user type declarations first (so a forward reference resolves).
fn collect_types(items: []Item) -> TypeTable {
    mut table = teko::list::empty()
    mut i = 0
    loop {
        if i >= items.len { break }
        match items[i] {
            TypeDecl as td => { table = teko::list::push(table, TypeReg { name = td.name; decl = td }) }
            _              => {}
        }
        i++
    }
    table
}

// a function's signature as a FuncType (resolve its param + return types).
fn func_type(f: Function, table: TypeTable) -> Type | error {
    mut params = teko::list::empty()
    mut i = 0
    loop {
        if i >= f.params.len { break }
        let pt = match resolve_type(f.params[i].type_ann, table) {
            Type as t  => t
            error as e => return e
        }
        params = teko::list::push(params, pt)
        i++
    }
    let ret = match resolve_type(f.return_type, table) {
        Type as t  => t
        error as e => return e
    }
    Func { params = params; ret = ret }
}

// register every top-level function's signature (now that all type names exist).
fn collect_funcs(items: []Item, table: TypeTable) -> Env | error {
    mut env = teko::list::empty()
    mut i = 0
    loop {
        if i >= items.len { break }
        match items[i] {
            Function as f => {
                let ft = match func_type(f, table) {
                    Type as t  => t
                    error as e => return e
                }
                env = define(env, f.name, ft, false)
            }
            _ => {}
        }
        i++
    }
    env
}

// pass 1: types first, then function signatures — the top-level environment.
fn collect(program: Program) -> Collected | error {
    let table = collect_types(program.items)
    let env = match collect_funcs(program.items, table) {
        Env as e     => e
        error as err => return err
    }
    Collected { types = table; env = env }
}
```

### C23 — `src/checker/resolve.h`

```c
// src/checker/resolve.h — type resolution + the user-type registry.
#ifndef TK_CHECK_RESOLVE_H
#define TK_CHECK_RESOLVE_H

#include "type.h"
#include "scope.h"
#include "../parser/ast.h"   // tk_type_expr, tk_type_decl, tk_path, … (the parser's AST)

typedef struct { tk_str name; tk_type_decl decl; } tk_type_reg;
TK_LIST(tk_type_reg, tk_type_table);

TK_RESULT(tk_type_decl, tk_decl_result);   // TypeDecl | error

tk_decl_result tk_type_table_find(tk_type_table table, tk_str name);
tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table);

#endif // TK_CHECK_RESOLVE_H
```

### C23 — `src/checker/resolve.c`

```c
// src/checker/resolve.c
#include "resolve.h"
#include <string.h>
#include <stdlib.h>

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}
// box a Type onto the heap — the compiler-managed indirection, made concrete.
static tk_type *box(tk_type t) {
    tk_type *p = malloc(sizeof *p);
    if (!p) abort();
    *p = t;
    return p;
}

tk_decl_result tk_type_table_find(tk_type_table table, tk_str name) {
    for (size_t i = 0; i < table.len; i += 1) {
        if (name_eq(table.ptr[i].name, name)) {
            return (tk_decl_result){ .ok = true, .as.value = table.ptr[i].decl };
        }
    }
    return (tk_decl_result){ .ok = false, .as.error = tk_error_make("not a user type") };
}

static tk_type_result resolve_named(tk_path path, tk_type_table table) {
    tk_str name = path.segments[path.len - 1].name;       // seed: last segment
    tk_type_result bt = tk_builtin_type(name);            // u8…u64, byte, str, error
    if (bt.ok) return bt;
    tk_decl_result ut = tk_type_table_find(table, name);  // a user type
    if (ut.ok) {
        tk_type t = { .tag = TK_TYPE_NAMED, .as.named.name = name };
        return (tk_type_result){ .ok = true, .as.value = t };
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("unknown type") };
}

tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table) {
    switch (te.tag) {
        case TK_TEXPR_NAMED:
            return resolve_named(te.as.named.path, table);
        case TK_TEXPR_SLICE: {
            tk_type_result el = tk_resolve_type(*te.as.slice.element, table);
            if (!el.ok) return el;
            tk_type t = { .tag = TK_TYPE_SLICE, .as.slice.element = box(el.as.value) };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
        case TK_TEXPR_UNION: {
            tk_type *members = NULL; size_t n = 0;
            for (size_t i = 0; i < te.as.uni.len; i += 1) {
                tk_type_result m = tk_resolve_type(te.as.uni.members[i], table);
                if (!m.ok) { free(members); return m; }
                members = realloc(members, (n + 1) * sizeof *members);
                if (!members) abort();
                members[n] = m.as.value; n += 1;
            }
            tk_type t = { .tag = TK_TYPE_VARIANT, .as.variant = { members, n } };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("bad type expr") };
}
```

### C23 — `src/checker/collect.h` + `collect.c`

```c
// src/checker/collect.h — pass 1: collect top-level names (forward references).
#ifndef TK_CHECK_COLLECT_H
#define TK_CHECK_COLLECT_H
#include "resolve.h"

typedef struct { tk_type_table types; tk_env env; } tk_collected;
TK_RESULT(tk_collected, tk_collected_result);

tk_collected_result tk_collect(tk_program program);
#endif // TK_CHECK_COLLECT_H
```

```c
// src/checker/collect.c
#include "collect.h"

static tk_type_table collect_types(tk_item *items, size_t n) {
    tk_type_table table = tk_type_table_empty();
    for (size_t i = 0; i < n; i += 1) {
        if (items[i].tag == TK_ITEM_TYPE_DECL) {
            tk_type_decl td = items[i].as.type_decl;
            table = tk_type_table_push(table, (tk_type_reg){ .name = td.name, .decl = td });
        }
    }
    return table;
}

static tk_type_result func_type(tk_function f, tk_type_table table) {
    tk_type *params = NULL; size_t n = 0;
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) { free(params); return pt; }
        params = realloc(params, (n + 1) * sizeof *params);
        if (!params) abort();
        params[n] = pt.as.value; n += 1;
    }
    tk_type_result ret = tk_resolve_type(f.return_type, table);
    if (!ret.ok) { free(params); return ret; }
    tk_type *rp = malloc(sizeof *rp); if (!rp) abort(); *rp = ret.as.value;
    tk_type t = { .tag = TK_TYPE_FUNC, .as.func = { params, n, rp } };
    return (tk_type_result){ .ok = true, .as.value = t };
}

tk_collected_result tk_collect(tk_program program) {
    tk_type_table table = collect_types(program.items, program.len);
    tk_env env = tk_env_empty();
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_FUNCTION) {
            tk_function f = program.items[i].as.function;
            tk_type_result ft = func_type(f, table);
            if (!ft.ok) return (tk_collected_result){ .ok = false, .as.error = ft.as.error };
            env = tk_env_define(env, f.name, ft.as.value, false);
        }
    }
    tk_collected c = { .types = table, .env = env };
    return (tk_collected_result){ .ok = true, .as.value = c };
}
```

---
## Etapa 4 — predicados de tipo + regimes de B.22 (compartilhados)

Os **predicados de tipo** e os **regimes de operador** de B.22 — aritmético/bitwise
exigem operandos do **mesmo tipo** (sem promoção) e inteiros; shift quer inteiros (a
contagem pode diferir); comparação é **sign-check** → `bool`. Estes helpers são a
base compartilhada consumida pela camada **typed** (`type_binary`/`type_unary`/
`type_compare`), a única produtora de checagem de expressão restante.

> Ponto marcado: `is_comparable` é permissivo (mesmo-tipo **ou** numérico) — separar
> ordenação (só numérico) de igualdade (mesmo-tipo) é refinamento.

### Teko — `src/checker/expr.tks`

```teko
// src/checker/expr.tks  (namespace 'teko::checker')

// --- type predicates ---
fn is_bool(t: Type) -> bool {
    match t { Prim as p => p.kind == PrimKind::Bool; _ => false }
}
fn is_integer(t: Type) -> bool {
    match t { Prim as p => p.kind != PrimKind::Bool; _ => false }   // every Prim but Bool
}
// sign-check (B.22): two numerics compare regardless of sign/width; else same type.
fn is_comparable(a: Type, b: Type) -> bool {
    if is_integer(a) && is_integer(b) { return true }
    type_eq(a, b)
}

// --- binary-op regimes (on the op token) ---
fn op_is_shift(op: lexer::TokenKind) -> bool {
    op == lexer::TokenKind::Shl || op == lexer::TokenKind::Shr
}
fn op_is_arith_bitwise(op: lexer::TokenKind) -> bool {
    op == lexer::TokenKind::Plus  || op == lexer::TokenKind::Minus ||
    op == lexer::TokenKind::Star  || op == lexer::TokenKind::Slash ||
    op == lexer::TokenKind::Percent ||
    op == lexer::TokenKind::Amp || op == lexer::TokenKind::Pipe || op == lexer::TokenKind::Caret
}

// These predicates + op-regimes are the shared core of B.22 — the typed layer
// (type_binary/type_unary/type_compare in `typer`) consumes them directly.
```
---
## Etapa 5b-2 — checagem de padrão + exaustividade (B.15, compartilhados)

`check_pattern` checa um padrão contra o subject e estende o env (`Type as name` →
binding imutável — B.21). C7 cobre **todos** os padrões: **Field** `Type { f; g }` (eixo
variante — resolve o struct, binda cada campo imutável via `field_type`), **Range**
`lo ..= hi` (ambos os limites `type_eq` o subject, que deve ser inteiro), e **Alt**
`a | b` (cada opção checada; **nenhuma pode bindar** — regra de eixo settled). A
**exaustividade** (B.15): um `_` **não-guardado** cobre tudo; senão, num subject
`Variant`, **todo caso** precisa de um braço **não-guardado** que o nomeie — e um
`AltPattern` de casos nus **expande** (`RED | GREEN` cobre os dois; B.14). Guardas
`when` **não** contam (SOURCE A.14). Estes helpers são os únicos sobreviventes desta
etapa — a checagem de braço/`match` por inteiro vive na camada **typed** (`type_match`,
C1), que **reusa** estes mesmos helpers (promovidos a `tk_check_pattern`/`tk_exhaustive`,
não-`static`).

### Teko — `src/checker/match.tks`

```teko
// src/checker/match.tks  (namespace 'teko::checker')

// validate a pattern against the subject; return the env extended with bindings.
fn check_pattern(p: Pattern, subject: Type, env: Env, table: TypeTable) -> Env | error {
    match p {
        WildcardPattern   => env
        BindPattern as bp => {
            let ct = match resolve_named(bp.type_name, table) { Type as t => t; error as e => return e }
            define(env, bp.binding, ct, false)     // `Type as name` → name : the case type (match-binding immutable — B.21)
        }
        LiteralPattern as lp => {
            let lt = match type_expr(lp.value, env, table) { TExpr as te => te.type; error as e => return e }
            if !type_eq(lt, subject) { return error { message = "literal pattern does not match the subject type" } }
            env
        }
        FieldPattern as fp => {
            // C7a: variant axis `Type { f; g }` — resolve to a struct, bind each field IMMUTABLE (B.21).
            let nt = match resolve_named(fp.type_name, table) { Type as t => t; error as e => return e }
            let name = match nt {
                Named as n => n.name
                _          => return error { message = "field pattern requires a struct type" }
            }
            let decl = match type_table_find(table, name) {
                TypeDecl as td => td
                error          => return error { message = $"unknown type in field pattern: {name}" }
            }
            let sb = match decl.body {
                StructBody as s => s
                _               => return error { message = $"type {name} is not a struct (no fields)" }
            }
            mut e2 = env
            mut i = 0
            loop {
                if i >= fp.fields.len { break }
                let ft = match field_type(sb, fp.fields[i], table) { Type as t => t; error as e => return e }
                e2 = define(e2, fp.fields[i], ft, false)      // B.21 — match-binding immutable
                i++
            }
            e2
        }
        RangePattern as rp => {
            // C7a: `lo ..= hi` — both bounds type_eq the subject, AND the subject is an integer (M.1/M.2).
            let lo = match type_expr(rp.lo, env, table) { TExpr as te => te.type; error as e => return e }
            let hi = match type_expr(rp.hi, env, table) { TExpr as te => te.type; error as e => return e }
            if !type_eq(lo, subject) { return error { message = "range lower bound does not match the subject type" } }
            if !type_eq(hi, subject) { return error { message = "range upper bound does not match the subject type" } }
            if !is_integer(subject) { return error { message = "range pattern requires an integer subject" } }
            env       // binds nothing
        }
        AltPattern as ap => {
            // C7a: `a | b | …` — each option checked against the subject; NO option may bind (settled axis rule).
            mut i = 0
            loop {
                if i >= ap.options.len { break }
                let opt = ap.options[i]
                match opt {
                    BindPattern as bp => { if bp.has_binding { return error { message = "an alternative (`|`) cannot bind; use a separate arm" } } }
                    FieldPattern      => return error { message = "an alternative (`|`) cannot bind; use a separate arm" }
                    _                 => {}
                }
                // recurse for validation (the subject must accept each option); discard the env — Alt binds nothing.
                match check_pattern(opt, subject, env, table) { Env => {}; error as e => return e }
                i++
            }
            env       // binds nothing
        }
    }
}

// --- exhaustiveness (B.15) ---

// an UNGUARDED `_` covers everything; a guarded `_ when g` does NOT (B.15 — `when` excluded).
fn has_wildcard(arms: []Arm) -> bool {
    mut i = 0
    loop {
        if i >= arms.len { break }
        if !arms[i].has_when {
            match arms[i].pattern { WildcardPattern => return true; _ => {} }
        }
        i++
    }
    false
}

// the case-name(s) a variant pattern selects: a single Bind/Field, or EACH bare option of an Alt.
fn arm_case_names(p: Pattern) -> []str {
    match p {
        BindPattern as bp  => teko::list::push(teko::list::empty(), bp.type_name.segments[bp.type_name.segments.len - 1].name)
        FieldPattern as fp => teko::list::push(teko::list::empty(), fp.type_name.segments[fp.type_name.segments.len - 1].name)
        AltPattern as ap   => {
            mut names = teko::list::empty()
            mut i = 0
            loop {
                if i >= ap.options.len { break }
                match ap.options[i] {
                    BindPattern as bp  => { names = teko::list::push(names, bp.type_name.segments[bp.type_name.segments.len - 1].name) }
                    FieldPattern as fp => { names = teko::list::push(names, fp.type_name.segments[fp.type_name.segments.len - 1].name) }
                    _                  => {}
                }
                i++
            }
            names
        }
        _ => teko::list::empty()
    }
}

// is `name` covered by some UNGUARDED arm (directly via Bind/Field, or via an Alt option)?
fn some_arm_names(arms: []Arm, name: str) -> bool {
    mut i = 0
    loop {
        if i >= arms.len { break }
        if !arms[i].has_when {
            let names = arm_case_names(arms[i].pattern)
            mut j = 0
            loop {
                if j >= names.len { break }
                if names[j] == name { return true }
                j++
            }
        }
        i++
    }
    false
}

// every member of the variant must be named by some unguarded arm.
fn variant_covered(arms: []Arm, members: []Type) -> bool {
    mut i = 0
    loop {
        if i >= members.len { break }
        match members[i] {
            Named as nm => { if !some_arm_names(arms, nm.name) { return false } }
            _           => return false        // a non-named member needs a `_`
        }
        i++
    }
    true
}

// a `_` covers all; otherwise a Variant subject must have every case named.
fn exhaustive(arms: []Arm, subject: Type) -> bool {
    if has_wildcard(arms) { return true }
    match subject {
        Variant as v => variant_covered(arms, v.members)
        _            => false
    }
}
```

### C23 — `src/checker/match.h` + `match.c`

```c
// src/checker/match.h — the shared pattern-checking + exhaustiveness helpers (B.15),
// reused by the typed layer (typer.c) which forward-declares them to avoid a cycle.
#ifndef TK_CHECK_MATCH_H
#define TK_CHECK_MATCH_H

#include "type.h"
#include "scope.h"
#include "resolve.h"
#include "../parser/ast.h"   // tk_pattern, tk_arm, … (the parser's AST)

tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
bool          tk_exhaustive(tk_arm *arms, size_t n, tk_type subject);

#endif // TK_CHECK_MATCH_H
```

```c
// src/checker/match.c
#include "match.h"

// from typer.c — the typed expression pass + the struct field resolver
// (forward-declared, both non-static, to avoid a match↔typer cycle).
tk_texpr_result tk_type_expr(tk_expr e, tk_env env, tk_type_table table);
tk_type_result  field_type(tk_struct_body sb, tk_str field, tk_type_table table);

static tk_env_result eok(tk_env e)     { return (tk_env_result){ .ok = true,  .as.value = e }; }
static tk_env_result efail(tk_error e) { return (tk_env_result){ .ok = false, .as.error = e }; }

// env | error: validate a pattern, extend the env.
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table) {
    switch (p.tag) {
        case TK_PAT_WILDCARD: return eok(env);
        case TK_PAT_BIND: {
            tk_type_result ct = resolve_named_c(p.as.bind.type_name, table); // see resolve.c
            if (!ct.ok) return efail(ct.as.error);
            return eok(tk_env_define(env, p.as.bind.binding, ct.as.value, false));
        }
        case TK_PAT_LITERAL: {
            tk_texpr_result lt = tk_type_expr(p.as.literal.value, env, table);
            if (!lt.ok) return efail(lt.as.error);
            if (!tk_type_eq(&lt.as.value.type, &subject)) return efail(tk_error_make("literal pattern does not match the subject type"));
            return eok(env);
        }
        case TK_PAT_FIELD: {
            // C7a: variant axis `Type { f; g }` — resolve to a struct, bind each field IMMUTABLE (B.21).
            tk_type_result nt = resolve_named_c(p.as.field.type_name, table);
            if (!nt.ok) return efail(nt.as.error);
            if (nt.as.value.tag != TK_TYPE_NAMED) return efail(tk_error_make("field pattern requires a struct type"));
            tk_decl_result decl = tk_type_table_find(table, nt.as.value.as.named.name);
            if (!decl.ok) return efail(tk_error_make("unknown type in field pattern"));
            if (decl.as.value.body.tag != TK_BODY_STRUCT) return efail(tk_error_make("type is not a struct (no fields)"));
            tk_struct_body sb = decl.as.value.body.as.struct_body;
            tk_env e2 = env;
            for (size_t i = 0; i < p.as.field.n_fields; i += 1) {
                tk_type_result ft = field_type(sb, p.as.field.fields[i], table);
                if (!ft.ok) return efail(ft.as.error);
                e2 = tk_env_define(e2, p.as.field.fields[i], ft.as.value, false);   // B.21
            }
            return eok(e2);
        }
        case TK_PAT_RANGE: {
            // C7a: `lo ..= hi` — both bounds type_eq the subject, AND the subject is an integer (M.1/M.2).
            tk_texpr_result lo = tk_type_expr(p.as.range.lo, env, table);
            if (!lo.ok) return efail(lo.as.error);
            tk_texpr_result hi = tk_type_expr(p.as.range.hi, env, table);
            if (!hi.ok) return efail(hi.as.error);
            if (!tk_type_eq(&lo.as.value.type, &subject)) return efail(tk_error_make("range lower bound does not match the subject type"));
            if (!tk_type_eq(&hi.as.value.type, &subject)) return efail(tk_error_make("range upper bound does not match the subject type"));
            if (!(subject.tag == TK_TYPE_PRIM && subject.as.prim != TK_PRIM_BOOL)) return efail(tk_error_make("range pattern requires an integer subject"));
            return eok(env);   // binds nothing
        }
        case TK_PAT_ALT: {
            // C7a: `a | b | …` — each option checked; NO option may bind (settled axis rule).
            for (size_t i = 0; i < p.as.alt.n_options; i += 1) {
                tk_pattern opt = p.as.alt.options[i];
                if (opt.tag == TK_PAT_BIND && opt.as.bind.has_binding)
                    return efail(tk_error_make("an alternative (`|`) cannot bind; use a separate arm"));
                if (opt.tag == TK_PAT_FIELD)
                    return efail(tk_error_make("an alternative (`|`) cannot bind; use a separate arm"));
                tk_env_result r = tk_check_pattern(opt, subject, env, table);   // recurse; discard env — Alt binds nothing
                if (!r.ok) return efail(r.as.error);
            }
            return eok(env);   // binds nothing
        }
    }
    return eok(env);
}

// --- exhaustiveness (B.15) ---

// an UNGUARDED `_` covers everything; a guarded `_ when g` does NOT (B.15 — `when` excluded).
static bool has_wildcard(tk_arm *arms, size_t n) {
    for (size_t i = 0; i < n; i += 1)
        if (!arms[i].has_when && arms[i].pattern.tag == TK_PAT_WILDCARD) return true;
    return false;
}
static bool name_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }
// does pattern `p` name case `name` — directly (Bind/Field) or via a bare Alt option?
static bool pattern_names(tk_pattern p, tk_str name) {
    if (p.tag == TK_PAT_BIND)
        return name_eq(p.as.bind.type_name.segments[p.as.bind.type_name.len - 1].name, name);
    if (p.tag == TK_PAT_FIELD)
        return name_eq(p.as.field.type_name.segments[p.as.field.type_name.len - 1].name, name);
    if (p.tag == TK_PAT_ALT) {
        for (size_t i = 0; i < p.as.alt.n_options; i += 1) {
            tk_pattern opt = p.as.alt.options[i];
            if (opt.tag == TK_PAT_BIND  && name_eq(opt.as.bind.type_name.segments[opt.as.bind.type_name.len - 1].name, name)) return true;
            if (opt.tag == TK_PAT_FIELD && name_eq(opt.as.field.type_name.segments[opt.as.field.type_name.len - 1].name, name)) return true;
        }
    }
    return false;
}
// is `name` covered by some UNGUARDED arm?
static bool some_arm_names(tk_arm *arms, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1)
        if (!arms[i].has_when && pattern_names(arms[i].pattern, name)) return true;
    return false;
}
bool tk_exhaustive(tk_arm *arms, size_t n, tk_type subject) {
    if (has_wildcard(arms, n)) return true;
    if (subject.tag != TK_TYPE_VARIANT) return false;
    for (size_t i = 0; i < subject.as.variant.len; i += 1) {
        tk_type mem = subject.as.variant.members[i];
        if (mem.tag != TK_TYPE_NAMED) return false;
        if (!some_arm_names(arms, n, mem.as.named.name)) return false;
    }
    return true;
}
```

### C23 — `src/checker/check.h`

```c
// src/checker/check.h — the shared checker result type (an `error?`: null = ok).
// (The legacy tk_check_function/item/program driver was retired; the typed
//  layer's tk_type_program is the entry point. revalidate.c still uses this type.)
#ifndef TK_CHECK_H
#define TK_CHECK_H

#include "collect.h"   // pulls resolve.h → tk_error, bool

// `error?` — `ok` (no error) on success; `error` is valid iff `!ok`. The C struct
// is the tagged scaffolding for an `error?` (null when ok).
typedef struct { bool ok; tk_error error; } tk_check_result;

#endif // TK_CHECK_H
```

---
## Etapa 6-1 — a anotação: a AST tipada (decisão 2)

A saída do checker é a **AST tipada**: cada nó de expressão emparelhado com seu
`Type` resolvido, numa **árvore única e autossuficiente** (decisão 2 — sem camada à
parte). É uma estrutura **paralela** em `teko::checker` (não a AST do parser
aumentada), porque a AST do parser **não pode carregar** o `Type` do checker sem um
ciclo no DAG (B.8: checker → AST-do-parser, então a AST-do-parser não depende do
checker). **O codegen lê esta árvore; o `.tkb` serializa esta árvore.**

A mecânica: cada `check_*` da E4/E5 **evolui** num `type_*` — as **mesmas
checagens**, mas em vez de devolver o `Type`, devolve o **nó tipado** (que carrega
seu tipo e tem filhos tipados). **C1 conclui essa evolução: todo nó de expressão,
todo statement e todo item é re-derivado** (não mais representativo). O typer espelha
o modelo de itens do *driver* do checker (`Item`/`Program` — E5c); reconciliar esse
driver à AST pós-R-main (`MainFile`/`Module`) é um passo separado, registrado nas
"Próximas etapas". **Governing Law: M.3** (a árvore tipada é honesta — todo nó *é* o
seu tipo, não o representa) **+ M.1** (nenhum nó escapa destipado — a base da
contra-validação E6-2) **+ M.4** (conclui a camada do checker antes do backend
depender dela).

### Teko — `src/checker/tast.tks`

```teko
// src/checker/tast.tks  (namespace 'teko::checker') — the TYPED AST (decision 2).
// Every node paired with its resolved Type, in one self-sufficient tree (no side
// layer). The codegen reads this; the `.tkb` serializes it. The `type_*` producers
// (typer.tks) build it; the children are themselves typed. (C1: complete.)

// --- typed expressions: a kind + this node's resolved type ---
type TExpr = struct {
    kind: TExprKind
    type: Type
}

// leaves + operators (mirror Expr; children are TExpr — the whole tree is typed).
type TNumber  = struct { value: i64 }
type TVar     = struct { name: str }
type TStrLit  = struct { text: str }
type TByteLit = struct { value: byte }
type TBinary  = struct { op: lexer::TokenKind; left: TExpr; right: TExpr }
type TUnary   = struct { op: lexer::TokenKind; operand: TExpr }
type TCmpTerm = struct { op: lexer::TokenKind; operand: TExpr }
type TCompare = struct { first: TExpr; rest: []TCmpTerm }
type TCall    = struct { callee: Path; args: []TExpr }

// control-flow expressions carry TYPED blocks/arms (B.20/B.15). The `pattern` stays
// syntactic (it binds; pattern typing itself is C7a); `guard` is valid iff has_when.
type TArm       = struct { pattern: Pattern; has_when: bool; guard: TExpr; body: TExpr }
type TIfExpr    = struct { cond: TExpr; then_blk: []TStatement; has_else: bool; else_blk: []TStatement }
type TMatchExpr = struct { subject: TExpr; arms: []TArm }
type TCast      = struct { expr: TExpr }   // `x to T` (C2): the cast's target IS this node's resolved `.type`
type TFieldAccess = struct { receiver: TExpr; field: str }   // `x.field` (C3): `.type` is the field's resolved type (B.29 field-read)

type TExprKind = TNumber | TVar | TStrLit | TByteLit | TBinary | TUnary | TCompare | TCall | TIfExpr | TMatchExpr | TCast | TFieldAccess

// --- typed statements (mirror Statement; children are TExpr / []TStatement) ---
type TBinding      = struct { kind: BindKind; target: BindTarget; bound: Type; value: TExpr }   // `bound`: the type the target binds to (the annotation if present, else the value's)
type TAssign       = struct { name: str; op: lexer::TokenKind; value: TExpr }
type TReturn       = struct { has_value: bool; value: TExpr }   // `value` gated by has_value (B.20) — a bare `return`'s placeholder (Number 0) types harmlessly
type TLoopStmt     = struct { body: []TStatement }
type TBreakStmt    = struct { }
type TContinueStmt = struct { }
type TExprStmt     = struct { expr: TExpr }

type TStatement = TBinding | TAssign | TReturn | TLoopStmt | TBreakStmt | TContinueStmt | TExprStmt

// --- typed top-level items + program (mirror the checker's Item/Program — E5c) ---
type TFunction = struct {
    name:        str
    params:      []Param          // immutable params (B.21) — carried from the parser unchanged
    return_type: Type             // resolved (void when the function returns no value, `-> void`)
    body:        []TStatement
    is_exp:      bool             // exported? (filtered into the `.tkh` later — M.4)
}
type TItem    = TFunction | TypeDecl | UseDecl | TStatement   // TypeDecl/UseDecl carry no expr → pass through (E5c); a loose top-level Statement → typed
type TProgram = struct { items: []TItem }

// --- env-threading carriers: the typed pass advances the env, like check_* did ---
type TypedStmt  = struct { node: TStatement;    env: Env }   // a typed statement + the env it leaves (bindings extend it)
type TypedBlock = struct { stmts: []TStatement; env: Env }   // a typed block + its final env
```

### Teko — `src/checker/typer.tks` (os `check_*` viram `type_*`)

```teko
// src/checker/typer.tks  (namespace 'teko::checker')
// Each check_* (E4/E5) EVOLVES into a type_*: the SAME checks (B.22 regimes, B.20
// `if`, B.15 `match` + exhaustiveness), but the producer BUILDS the typed node
// instead of returning a bare Type. Predicates (is_bool/is_integer/op_is_*),
// resolvers (resolve_type), env (lookup/define) and the pattern + exhaustiveness
// logic (check_pattern/exhaustive) are reused from their stages — re-derivation,
// not duplication (M.5). C1: every node, statement and item re-derived.

// ---- leaves ----
fn type_number(n: Number) -> TExpr {
    TExpr { kind = TNumber { value = n.value }; type = Prim { kind = PrimKind::I64 } }   // [literal typing in annotated context: C6]
}
fn type_strlit(s: StrLit) -> TExpr {
    TExpr { kind = TStrLit { text = s.text }; type = Str { } }
}
fn type_bytelit(b: ByteLit) -> TExpr {
    TExpr { kind = TByteLit { value = b.value }; type = Byte { } }
}
fn type_var(v: Var, env: Env) -> TExpr | error {
    let t = match lookup(env, v.name) { Type as ty => ty; error as e => return e }
    TExpr { kind = TVar { name = v.name }; type = t }
}

// ---- operators (the SAME B.22 regimes as check_binary/check_unary/check_compare) ----
fn type_binary(b: Binary, env: Env, table: TypeTable) -> TExpr | error {
    let l = match type_expr(b.left, env, table)  { TExpr as te => te; error as e => return e }
    let r = match type_expr(b.right, env, table) { TExpr as te => te; error as e => return e }
    if op_is_shift(b.op) {
        if !is_integer(l.type) || !is_integer(r.type) { return error { message = "shift needs integer operands" } }
        return TExpr { kind = TBinary { op = b.op; left = l; right = r }; type = l.type }
    }
    if op_is_arith_bitwise(b.op) {
        if !is_integer(l.type) { return error { message = "arithmetic/bitwise needs an integer" } }
        if !type_eq(l.type, r.type) { return error { message = "operands must be the same type (no promotion — B.22)" } }
        return TExpr { kind = TBinary { op = b.op; left = l; right = r }; type = l.type }
    }
    error { message = "not a binary operator" }
}

fn type_unary(u: Unary, env: Env, table: TypeTable) -> TExpr | error {
    let o = match type_expr(u.operand, env, table) { TExpr as te => te; error as e => return e }
    if u.op == lexer::TokenKind::Minus || u.op == lexer::TokenKind::Tilde {
        if !is_integer(o.type) { return error { message = "unary -/~ needs an integer" } }
        return TExpr { kind = TUnary { op = u.op; operand = o }; type = o.type }
    }
    if u.op == lexer::TokenKind::Bang {
        if !is_bool(o.type) { return error { message = "! needs a bool" } }
        return TExpr { kind = TUnary { op = u.op; operand = o }; type = o.type }
    }
    error { message = "not a unary operator" }
}

fn type_compare(c: Compare, env: Env, table: TypeTable) -> TExpr | error {
    let first = match type_expr(c.first, env, table) { TExpr as te => te; error as e => return e }
    mut prev = first.type
    mut terms = teko::list::empty()
    mut i = 0
    loop {
        if i >= c.rest.len { break }
        let cur = match type_expr(c.rest[i].operand, env, table) { TExpr as te => te; error as e => return e }
        if !is_comparable(prev, cur.type) { return error { message = "operands are not comparable" } }
        terms = teko::list::push(terms, TCmpTerm { op = c.rest[i].op; operand = cur })
        prev = cur.type
        i++
    }
    TExpr { kind = TCompare { first = first; rest = terms }; type = Prim { kind = PrimKind::Bool } }
}

fn type_call(c: Call, env: Env, table: TypeTable) -> TExpr | error {
    let name = c.callee.segments[c.callee.segments.len - 1].name
    let ft = match lookup(env, name) { Type as t => t; error as e => return e }
    match ft {
        Func as f => {
            if c.args.len != f.params.len { return error { message = "wrong number of arguments" } }
            mut args = teko::list::empty()
            mut i = 0
            loop {
                if i >= c.args.len { break }
                let a = match type_expr(c.args[i], env, table) { TExpr as te => te; error as e => return e }
                if !type_eq(a.type, f.params[i]) { return error { message = "argument type mismatch" } }
                args = teko::list::push(args, a)
                i++
            }
            TExpr { kind = TCall { callee = c.callee; args = args }; type = f.ret }
        }
        _ => error { message = $"not a function: {name}" }
    }
}

// ---- cast `to` (C2): a DEFINED conversion is allowed; loss is caught, never silent (M.1) ----
// Two-pronged — the ÷0/overflow/OOB lineage applied to conversions (homeostasis, Constitution §II):
//   • a CONSTANT operand the compiler can see out of range → COMPILE ERROR (fail early — M.1);
//   • a runtime value the compiler cannot see → the metal converts and the loss is caught at
//     runtime (debug-panic / release-defined, exactly like overflow) — that guard is CODEGEN's,
//     deferred (M.4). M.1 forbids only SILENT loss; a check/panic is not silent, so narrowing and
//     signed↔unsigned ARE allowed (the value might fit — M.0 keeps the metal conversion).
// Scoped to the seed's scalars: Bool casts and non-numeric casts are UNDEFINED (rejected).
// Floats/Decimal are alpha (not in PrimKind); newtype↔base awaits `resolve_named` lowering a
// newtype to its base — both deferred (M.4). *(Redefines the early compile-forbid rule — see the
// Redefinitions Index; the prior rule over-applied M.1, forbidding what a guard makes non-silent.)*

// does a constant i64 value fit the target primitive's range? (shared with C6 — literal range.)
fn value_fits(v: i64, k: PrimKind) -> bool {
    if k == PrimKind::U8  { return v >= 0 && v <= 255 }
    if k == PrimKind::U16 { return v >= 0 && v <= 65535 }
    if k == PrimKind::U32 { return v >= 0 && v <= 4294967295 }
    if k == PrimKind::U64 { return v >= 0 }                       // i64 max < u64 max → any v>=0 fits
    if k == PrimKind::I8  { return v >= -128 && v <= 127 }
    if k == PrimKind::I16 { return v >= -32768 && v <= 32767 }
    if k == PrimKind::I32 { return v >= -2147483648 && v <= 2147483647 }
    if k == PrimKind::I64 { return true }                         // v is already an i64
    false   // Bool — not a numeric range (guarded by the caller)
}

// is `from -> to` a DEFINED conversion? Any integer↔integer is (the loss is runtime/codegen's,
// not a type error — B). Only genuinely-undefined conversions are rejected: Bool, and crossing
// to/from a non-numeric type. M.1 — no silent loss; M.3 — names the barrier. Reused by the
// counter-validation (E6-2) so the rule has ONE source of truth (M.5).
// byte casts AS u8 (B.36 "byte = u8 newtype"): byte's values ARE u8 values, so the effective
// PrimKind for range/cast rules is U8. Bool and non-numeric types have no cast kind (rejected).
// One source of truth (M.5) for cast_check + const_range_check.
fn cast_kind(t: Type) -> PrimKind | error {
    match t {
        Prim as p => {
            if p.kind == PrimKind::Bool { error { message = "bool casts are not defined in the seed" } }
            else { p.kind }
        }
        Byte      => PrimKind::U8
        _         => error { message = "cast not defined for this type in the seed (Named/Str/Slice/… — pending)" }
    }
}

// is `from -> to` a DEFINED conversion? Any integer/byte ↔ integer/byte is (the loss is
// runtime/codegen's — B; byte casts AS u8 — B.36). Only Bool and non-numeric ends are rejected.
// Reused by the counter-validation (E6-2) — ONE source of truth (M.5).
fn cast_check(from: Type, to: Type) -> error? {                  // fallible-no-value: null = ok, error = failure
    if type_eq(from, to) { return null }                         // same type — a no-op
    let _ = match cast_kind(from) { PrimKind as k => k; error as e => return e }
    let _ = match cast_kind(to)   { PrimKind as k => k; error as e => return e }
    null                                                          // any numeric/byte → any numeric/byte is defined (B; byte AS u8)
}

// a CONSTANT literal already out of the target's range is a compile error (M.1 — fail early);
// non-constant operands are guarded at runtime by codegen. Direct literals only (comptime folding deferred).
fn const_range_check(e: Expr, target: Type) -> error? {          // fallible-no-value: null = ok, error = failure
    match e {
        Number as n => match cast_kind(target) {                 // byte target → U8 range (0..255)
            PrimKind as k => {
                if value_fits(n.value, k) { null }
                else { error { message = "constant out of range for the cast target (M.1 — fail early)" } }
            }
            error         => null                                // non-numeric target: cast_check already rejected it
        }
        _ => null                                                // not a constant literal → runtime-guarded (codegen)
    }
}

// C6 — an annotated binding `let x: T = <value>` whose value type ≠ T. A numeric LITERAL ADOPTS T
// if it fits: the leaf stays i64 (stored AS-IS — Side D) and T lives in the binding's `bound`.
// The fit is proven HERE at type time via `value_fits`. A NON-literal mismatch is a hard error —
// no implicit conversion (M.2; use `to`). An out-of-range literal fails EARLY (M.1).
// NOTE: the counter-validation does NOT yet re-prove the binding-level fit — a forged typed tree
// `bound=u8, value=300` would currently pass revalidate. That re-proof lands with `validate_statement`
// (deferred; named gap — §VI). The front-line type check above already rejects such source.
fn annotated_literal_ok(value: Expr, ann: Type) -> error? {      // fallible-no-value: null = ok, error = failure
    match value {
        Number as n => match ann {
            Prim as p => {
                if value_fits(n.value, p.kind) { null }
                else { error { message = "literal out of range for the annotated type (M.1 — fail early)" } }
            }
            _ => error { message = "value type does not match annotation" }
        }
        _ => error { message = "value type does not match annotation" }
    }
}

// `x to T` — type the inner, resolve the target, demand the conversion is DEFINED, then reject a
// CONSTANT literal already out of range (fail early — M.1). The runtime fit-check for non-constant
// operands is codegen's (debug-panic / release-defined) — deferred (M.4).
fn type_cast(c: Cast, env: Env, table: TypeTable) -> TExpr | error {
    let inner = match type_expr(c.expr, env, table) { TExpr as te => te; error as e => return e }
    let target = match resolve_type(c.target, table) { Type as t => t; error as e => return e }
    match cast_check(inner.type, target) { null => {}; error as e => return e }
    match const_range_check(c.expr, target) { null => {}; error as e => return e }
    TExpr { kind = TCast { expr = inner }; type = target }        // the cast's type IS the resolved target
}

// ---- field access `x.field` (C3): read a struct field; `.type` is the field's resolved type ----
// B.29: FIELD read only (method *checking* stays deferred). M.3: nominal — resolve the receiver's
// `Named` type to its `TypeDecl` body and read the field's declared type. Receiver must be a struct.

// find a field by name in a struct body and resolve its annotation (M.3 — exact).
fn field_type(sb: StructBody, field: str, table: TypeTable) -> Type | error {
    mut i = 0
    loop {
        if i >= sb.fields.len { break }
        if sb.fields[i].name == field { return resolve_type(sb.fields[i].type_ann, table) }
        i++
    }
    error { message = $"no such field: {field}" }
}

fn type_field_access(fa: FieldAccess, env: Env, table: TypeTable) -> TExpr | error {
    let recv = match type_expr(fa.receiver, env, table) { TExpr as te => te; error as e => return e }
    let name = match recv.type {
        Named as n => n.name
        _          => return error { message = "field access requires a struct receiver" }
    }
    let decl = match type_table_find(table, name) {
        TypeDecl as td => td
        error          => return error { message = $"unknown type for field access: {name}" }
    }
    let ft = match decl.body {
        StructBody as sb => match field_type(sb, fa.field, table) { Type as t => t; error as e => return e }
        _                => return error { message = $"type {name} is not a struct (no fields)" }
    }
    TExpr { kind = TFieldAccess { receiver = recv; field = fa.field }; type = ft }
}

// ---- the expression dispatch (the evolved check_expr) ----
fn type_expr(e: Expr, env: Env, table: TypeTable) -> TExpr | error {
    match e {
        Number as n    => type_number(n)
        StrLit as s    => type_strlit(s)
        ByteLit as b   => type_bytelit(b)
        Var as v       => type_var(v, env)
        Binary as b    => type_binary(b, env, table)
        Unary as u     => type_unary(u, env, table)
        Compare as c   => type_compare(c, env, table)
        Call as cl     => type_call(cl, env, table)
        IfExpr as f    => type_if(f, env, table)
        MatchExpr as m => type_match(m, env, table)
        FieldAccess as fa => type_field_access(fa, env, table)
        Cast as c      => type_cast(c, env, table)
        MethodCall     => error { message = "method typing is deferred (B.29 / M.4)" }
        PathExpr       => error { message = "path-expr typing pending (Enum::Member)" }
    }
}

// ---- the value-type a TYPED block yields: the last stmt's, if an ExprStmt; else void (no value) ----
fn tblock_type(stmts: []TStatement) -> Type {
    if stmts.len == 0 { return Void { } }
    match stmts[stmts.len - 1] {
        TExprStmt as es => es.expr.type
        _               => Void { }
    }
}

// ---- a typed block: thread the env, collect the typed statements ----
fn type_block(stmts: []Statement, env: Env, table: TypeTable) -> TypedBlock | error {
    mut cur = env
    mut out = teko::list::empty()
    mut i = 0
    loop {
        if i >= stmts.len { break }
        let ts = match type_statement(stmts[i], cur, table) { TypedStmt as t => t; error as e => return e }
        out = teko::list::push(out, ts.node)
        cur = ts.env
        i++
    }
    TypedBlock { stmts = out; env = cur }
}

// ---- if as a VALUE (B.20): cond bool; `else` required; both branches one type ----
fn type_if(f: IfExpr, env: Env, table: TypeTable) -> TExpr | error {
    let c = match type_expr(f.cond, env, table) { TExpr as te => te; error as e => return e }
    if !is_bool(c.type) { return error { message = "an `if` condition must be a bool" } }
    if !f.has_else { return error { message = "an `if` used as a value needs an `else`" } }
    let tb = match type_block(f.then_blk, env, table) { TypedBlock as bk => bk; error as e => return e }
    let eb = match type_block(f.else_blk, env, table) { TypedBlock as bk => bk; error as e => return e }
    let tt = tblock_type(tb.stmts)
    let et = tblock_type(eb.stmts)
    if !type_eq(tt, et) { return error { message = "the `if` branches have different types" } }
    TExpr { kind = TIfExpr { cond = c; then_blk = tb.stmts; has_else = true; else_blk = eb.stmts }; type = tt }
}

// ---- if as a STATEMENT: cond bool; branches validated; no `else`; value discarded → void ----
fn type_if_stmt(f: IfExpr, env: Env, table: TypeTable) -> TExpr | error {
    let c = match type_expr(f.cond, env, table) { TExpr as te => te; error as e => return e }
    if !is_bool(c.type) { return error { message = "an `if` condition must be a bool" } }
    let tb = match type_block(f.then_blk, env, table) { TypedBlock as bk => bk; error as e => return e }
    if f.has_else {
        let eb = match type_block(f.else_blk, env, table) { TypedBlock as bk => bk; error as e => return e }
        return TExpr { kind = TIfExpr { cond = c; then_blk = tb.stmts; has_else = true; else_blk = eb.stmts }; type = Void { } }
    }
    TExpr { kind = TIfExpr { cond = c; then_blk = tb.stmts; has_else = false; else_blk = tb.stmts }; type = Void { } }   // else_blk gated by has_else
}

// ---- one typed arm: pattern extends env; `when` guard bool; body typed in that env ----
fn type_arm(a: Arm, subject: Type, env: Env, table: TypeTable) -> TArm | error {
    let e2 = match check_pattern(a.pattern, subject, env, table) { Env as e => e; error as err => return err }
    let body = match type_expr(a.body, e2, table) { TExpr as te => te; error as err => return err }
    mut guard = body                                  // gated by has_when (placeholder reuses body — never read)
    if a.has_when {
        let g = match type_expr(a.guard, e2, table) { TExpr as te => te; error as err => return err }
        if !is_bool(g.type) { return error { message = "a `when` guard must be a bool" } }
        guard = g
    }
    TArm { pattern = a.pattern; has_when = a.has_when; guard = guard; body = body }
}

// ---- match as a VALUE: all arm bodies the SAME type → the match's type; EXHAUSTIVE (B.15) ----
fn type_match(m: MatchExpr, env: Env, table: TypeTable) -> TExpr | error {
    let s = match type_expr(m.subject, env, table) { TExpr as te => te; error as e => return e }
    if m.arms.len == 0 { return error { message = "a `match` needs at least one arm" } }
    let a0 = match type_arm(m.arms[0], s.type, env, table) { TArm as a => a; error as e => return e }
    let first = a0.body.type
    mut arms = teko::list::empty()
    arms = teko::list::push(arms, a0)
    mut i = 1
    loop {
        if i >= m.arms.len { break }
        let ai = match type_arm(m.arms[i], s.type, env, table) { TArm as a => a; error as e => return e }
        if !type_eq(ai.body.type, first) { return error { message = "the `match` arms have different types" } }
        arms = teko::list::push(arms, ai)
        i++
    }
    if !exhaustive(m.arms, s.type) { return error { message = "non-exhaustive `match` (cover all cases or add `_`)" } }
    TExpr { kind = TMatchExpr { subject = s; arms = arms }; type = first }
}

// ---- match as a STATEMENT: validate the arms; exhaustiveness forced; value discarded → void ----
fn type_match_stmt(m: MatchExpr, env: Env, table: TypeTable) -> TExpr | error {
    let s = match type_expr(m.subject, env, table) { TExpr as te => te; error as e => return e }
    mut arms = teko::list::empty()
    mut i = 0
    loop {
        if i >= m.arms.len { break }
        let ai = match type_arm(m.arms[i], s.type, env, table) { TArm as a => a; error as e => return e }
        arms = teko::list::push(arms, ai)
        i++
    }
    if !exhaustive(m.arms, s.type) { return error { message = "non-exhaustive `match` (cover all cases or add `_`)" } }
    TExpr { kind = TMatchExpr { subject = s; arms = arms }; type = Void { } }
}

// ---- statements (the evolved check_* — produce the typed node + advance the env) ----

fn type_binding(b: Binding, env: Env, table: TypeTable) -> TypedStmt | error {
    let v = match type_expr(b.value, env, table) { TExpr as te => te; error as e => return e }
    mut bound = v.type
    if b.has_type {
        let at = match resolve_type(b.type_ann, table) { Type as t => t; error as e => return e }
        if !type_eq(v.type, at) {
            match annotated_literal_ok(b.value, at) { null => {}; error as e => return e }   // C6: a fitting literal adopts T (leaf stays i64)
        }
        bound = at
    }
    let node = TBinding { kind = b.kind; target = b.target; bound = bound; value = v }
    match b.target {
        SimpleName as sn   => TypedStmt { node = node; env = define(env, sn.name, bound, bind_is_mut(b.kind)) }
        DestructurePattern => TypedStmt { node = node; env = env }    // [destructuring binding: refinement]
    }
}

fn type_assign(a: Assign, env: Env, table: TypeTable) -> TypedStmt | error {
    let tb = match lookup_binding(env, a.name) { ValBinding as vb => vb; error as e => return e }
    if !tb.is_mut { return error { message = $"cannot assign to immutable `{a.name}` — declare it `mut` (B.21)" } }
    let v = match type_expr(a.value, env, table) { TExpr as te => te; error as e => return e }
    if !type_eq(tb.type, v.type) { return error { message = "assigned value does not match the target type" } }
    TypedStmt { node = TAssign { name = a.name; op = a.op; value = v }; env = env }   // mut rule enforced (B.21)
}

fn type_return(r: Return, env: Env, table: TypeTable) -> TypedStmt | error {
    let v = match type_expr(r.value, env, table) { TExpr as te => te; error as e => return e }
    TypedStmt { node = TReturn { has_value = r.has_value; value = v }; env = env }    // value gated by has_value (B.20); the return-type match is enforced by type_function's check_returns (C5)
}

fn type_loop(l: LoopStmt, env: Env, table: TypeTable) -> TypedStmt | error {
    let tb = match type_block(l.body, env, table) { TypedBlock as bk => bk; error as e => return e }
    TypedStmt { node = TLoopStmt { body = tb.stmts }; env = env }     // body bindings stay block-local
}

fn type_exprstmt(es: ExprStmt, env: Env, table: TypeTable) -> TypedStmt | error {
    // a TOP-LEVEL if/match is a STATEMENT — its value is discarded, so no `else` is required.
    match es.expr {
        IfExpr as f    => {
            let te = match type_if_stmt(f, env, table) { TExpr as x => x; error as e => return e }
            TypedStmt { node = TExprStmt { expr = te }; env = env }
        }
        MatchExpr as m => {
            let te = match type_match_stmt(m, env, table) { TExpr as x => x; error as e => return e }
            TypedStmt { node = TExprStmt { expr = te }; env = env }
        }
        _              => {
            let te = match type_expr(es.expr, env, table) { TExpr as x => x; error as e => return e }
            TypedStmt { node = TExprStmt { expr = te }; env = env }
        }
    }
}

fn type_statement(s: Statement, env: Env, table: TypeTable) -> TypedStmt | error {
    match s {
        Binding as b   => type_binding(b, env, table)
        Assign as a    => type_assign(a, env, table)
        Return as r    => type_return(r, env, table)
        LoopStmt as l  => type_loop(l, env, table)
        BreakStmt      => TypedStmt { node = TBreakStmt { }; env = env }
        ContinueStmt   => TypedStmt { node = TContinueStmt { }; env = env }
        ExprStmt as es => type_exprstmt(es, env, table)
    }
}

// ---- items + program (the evolved check_function/check_item/check_program — E5c) ----

// the resolved return type: the annotation if present, else void (the `-> void` marker — no value).
fn function_return(f: Function, table: TypeTable) -> Type | error {
    if !f.has_return { return Void { } }
    resolve_type(f.return_type, table)
}

// ---- C5 (DEFER scope): `return`/final-expr vs the declared return type ----
// Every `return e` reachable as a statement must yield the declared return type, and — when the
// body's trailing statement is an expression — that value too. A `return <member>` into a variant
// return type is admissible (B.14 — a member is automatically the union, no cast). The FULL
// "every path yields a value" guarantee (divergence/definite-return analysis) is a SEPARATE later
// item (M.4): a body ending in a diverging `loop`/`if`/`match` makes NO trailing-value claim here,
// so the seed's value-fns that end in a breakless `loop` (exits are inner `return`s) are NOT
// false-rejected. (Lives in the typed pass — the producer; the superseded `check_*` layer is not
// dual-walked — see the check_*/type_* duplication note in "Próximas etapas".)

// is a value of type `from` admissible where `to` is expected? (B.14 — variant member inclusion.)
fn assignable_to(from: Type, to: Type) -> bool {
    if type_eq(from, to) { return true }
    match to {
        Variant as v => {
            mut i = 0
            loop {
                if i >= v.members.len { break }
                if type_eq(from, v.members[i]) { return true }
                i++
            }
            false
        }
        _ => false
    }
}

// check every `return` reachable as a statement (descend into loop bodies and `if`-blocks;
// match-arm returns await the divergence item — arm bodies are expressions, not statements).
fn check_returns(stmts: []TStatement, ret: Type) -> error? {     // fallible-no-value: null = ok, error = failure
    mut i = 0
    loop {
        if i >= stmts.len { break }
        match check_return_stmt(stmts[i], ret) { null => {}; error as e => return e }
        i++
    }
    null
}

fn check_return_stmt(s: TStatement, ret: Type) -> error? {       // fallible-no-value: null = ok, error = failure
    match s {
        TReturn as r => {
            if r.has_value {
                if assignable_to(r.value.type, ret) { null }
                else { error { message = "return value does not match the function's declared return type" } }
            } else {
                if assignable_to(Void { }, ret) { null }
                else { error { message = "bare `return` in a function that declares a non-void return type" } }
            }
        }
        TLoopStmt as l  => check_returns(l.body, ret)
        TExprStmt as es => check_returns_inexpr(es.expr, ret)
        _               => null
    }
}

// returns can also live inside a top-level `if`'s blocks (`if c { return e }`).
fn check_returns_inexpr(e: TExpr, ret: Type) -> error? {         // fallible-no-value: null = ok, error = failure
    match e.kind {
        TIfExpr as f => {
            match check_returns(f.then_blk, ret) { null => {}; error as e => return e }
            check_returns(f.else_blk, ret)
        }
        _ => null
    }
}

// the trailing-value check — ONLY when the last statement is an expression (else: NO claim, the
// guard against false-rejecting a body that ends in a diverging loop/if/match — the every-path item).
fn check_trailing_value(stmts: []TStatement, ret: Type) -> error? {   // fallible-no-value: null = ok, error = failure
    if stmts.len == 0 { return null }
    match stmts[stmts.len - 1] {
        TExprStmt as es => {
            if assignable_to(es.expr.type, ret) { null }
            else { error { message = "the function's final expression does not match its declared return type" } }
        }
        _ => null
    }
}

fn type_function(f: Function, env: Env, table: TypeTable) -> TFunction | error {
    mut local = env
    mut i = 0
    loop {
        if i >= f.params.len { break }
        let pt = match resolve_type(f.params[i].type_ann, table) { Type as t => t; error as e => return e }
        local = define(local, f.params[i].name, pt, false)        // params are immutable (B.21)
        i++
    }
    let ret = match function_return(f, table) { Type as t => t; error as e => return e }
    let tb = match type_block(f.body, local, table) { TypedBlock as bk => bk; error as e => return e }
    match check_returns(tb.stmts, ret)       { null => {}; error as e => return e }   // C5: each `return e` matches `ret`
    match check_trailing_value(tb.stmts, ret) { null => {}; error as e => return e }  // C5: trailing value (when present) matches
    TFunction { name = f.name; params = f.params; return_type = ret; body = tb.stmts; is_exp = f.is_exp }
}

fn type_item(item: Item, env: Env, table: TypeTable) -> TItem | error {
    match item {
        Function as f  => {
            let tf = match type_function(f, env, table) { TFunction as x => x; error as e => return e }
            tf
        }
        TypeDecl as td => td       // carries no expr → passes through (collect resolved its fields)
        UseDecl as ud  => ud       // seed no-op (alias resolution is the checker's, later)
        Statement as s => {
            let ts = match type_statement(s, env, table) { TypedStmt as x => x; error as e => return e }
            ts.node
        }
    }
}

// THE TYPED ENTRY POINT: pass 1 (collect top-level names) + pass 2 (type each item).
fn type_program(program: Program) -> TProgram | error {
    let c = match collect(program) { Collected as col => col; error as e => return e }
    mut items = teko::list::empty()
    mut i = 0
    loop {
        if i >= program.items.len { break }
        let ti = match type_item(program.items[i], c.env, c.types) { TItem as x => x; error as e => return e }
        items = teko::list::push(items, ti)
        i++
    }
    TProgram { items = items }
}
```

### C23 — `src/checker/tast.h`

```c
// src/checker/tast.h — the TYPED AST (the checker's output; codegen reads it, .tkb
// serializes it). C1: complete — every expression node, statement and item.
#ifndef TK_CHECK_TAST_H
#define TK_CHECK_TAST_H

#include "type.h"
#include "scope.h"           // tk_env (the typed pass threads it like check_*)
#include "../core.h"         // TK_RESULT, TK_LIST
#include "../parser/ast.h"   // tk_path, tk_token_kind, tk_bind_kind, tk_bind_target,
                             // tk_param, tk_pattern, tk_type_decl, tk_use_decl

typedef struct tk_texpr tk_texpr;            // recursive (children are tk_texpr*)
typedef struct tk_tstatement tk_tstatement;  // recursive (blocks are tk_tstatement*)

// --- typed expressions ---
typedef enum {
    TK_TEXPR_NUMBER, TK_TEXPR_VAR, TK_TEXPR_STR, TK_TEXPR_BYTE,
    TK_TEXPR_BINARY, TK_TEXPR_UNARY, TK_TEXPR_COMPARE, TK_TEXPR_CALL,
    TK_TEXPR_IF, TK_TEXPR_MATCH, TK_TEXPR_CAST, TK_TEXPR_FIELD_ACCESS,
} tk_texpr_tag;

typedef struct { tk_token_kind op; tk_texpr *operand; } tk_tcmp_term;

typedef struct {
    tk_pattern pattern;       // syntactic (binds; its own typing is C7a)
    bool       has_when;
    tk_texpr  *guard;         // valid iff has_when
    tk_texpr  *body;
} tk_tarm;

struct tk_texpr {
    tk_texpr_tag tag;
    tk_type      type;        // this node's resolved type
    union {
        struct { int64_t value; }                                    number;
        struct { tk_str name; }                                      var;
        struct { tk_str text; }                                      str;
        struct { tk_byte value; }                                    byte;
        struct { tk_token_kind op; tk_texpr *left, *right; }         binary;
        struct { tk_token_kind op; tk_texpr *operand; }             unary;
        struct { tk_texpr *first; tk_tcmp_term *rest; size_t nrest; } compare;
        struct { tk_path callee; tk_texpr *args; size_t nargs; }      call;
        struct { tk_texpr *cond; tk_tstatement *then_blk; size_t nthen;
                 bool has_else; tk_tstatement *else_blk; size_t nelse; } if_expr;
        struct { tk_texpr *subject; tk_tarm *arms; size_t narms; }    match_expr;
        struct { tk_texpr *expr; }                                    cast;   // `x to T` — target rides the node's `type`
        struct { tk_texpr *receiver; tk_str field; }                  field_access; // `x.field` (C3) — `.type` is the field's type
    } as;
};

// --- typed statements ---
typedef enum {
    TK_TSTMT_BINDING, TK_TSTMT_ASSIGN, TK_TSTMT_RETURN, TK_TSTMT_LOOP,
    TK_TSTMT_BREAK, TK_TSTMT_CONTINUE, TK_TSTMT_EXPR,
} tk_tstatement_tag;

struct tk_tstatement {
    tk_tstatement_tag tag;
    union {
        struct { tk_bind_kind kind; tk_bind_target target; tk_type bound; tk_texpr value; } binding;
        struct { tk_str name; tk_token_kind op; tk_texpr value; }                            assign;
        struct { bool has_value; tk_texpr value; }                                           ret;   // value gated by has_value
        struct { tk_tstatement *body; size_t nbody; }                                        loop_stmt;
        struct { tk_texpr expr; }                                                            expr_stmt;
    } as;
};

// --- typed items + program (mirror the checker's Item/Program — E5c) ---
typedef struct {
    tk_str         name;
    tk_param      *params; size_t nparams;   // immutable (B.21), carried unchanged
    tk_type        return_type;              // void when the function returns no value (`-> void`)
    tk_tstatement *body;   size_t nbody;
    bool           is_exp;
} tk_tfunction;

typedef enum { TK_TITEM_FUNCTION, TK_TITEM_TYPE_DECL, TK_TITEM_USE, TK_TITEM_STATEMENT } tk_titem_tag;
typedef struct {
    tk_titem_tag tag;
    union {
        tk_tfunction  function;
        tk_type_decl  type_decl;   // pass-through (no expr to type)
        tk_use_decl   use_decl;    // pass-through
        tk_tstatement statement;   // a loose top-level statement, typed
    } as;
} tk_titem;

typedef struct { tk_titem *items; size_t nitems; } tk_tprogram;

// env-threading carriers (the typed pass advances the env, like check_*).
typedef struct { tk_tstatement node;  tk_env env; }            tk_typed_stmt;
typedef struct { tk_tstatement *stmts; size_t n; tk_env env; } tk_typed_block;

// result stamps (T | error). tk_texpr_result is the canonical home (tkb_read.c reuses it).
TK_RESULT(tk_texpr,      tk_texpr_result);        // TExpr      | error
TK_RESULT(tk_tarm,       tk_tarm_result);         // TArm       | error
TK_RESULT(tk_typed_stmt, tk_typed_stmt_result);   // TypedStmt  | error
TK_RESULT(tk_typed_block,tk_typed_block_result);  // TypedBlock | error
TK_RESULT(tk_tfunction,  tk_tfunction_result);    // TFunction  | error
TK_RESULT(tk_titem,      tk_titem_result);        // TItem      | error
TK_RESULT(tk_tprogram,   tk_tprogram_result);     // TProgram   | error

#endif // TK_CHECK_TAST_H
```

### C23 — `src/checker/typer.h` + `typer.c`

```c
// src/checker/typer.h — the typed producers (the evolved check_*).
#ifndef TK_CHECK_TYPER_H
#define TK_CHECK_TYPER_H

#include "tast.h"
#include "resolve.h"   // tk_type_table

tk_texpr_result      tk_type_expr(tk_expr e, tk_env env, tk_type_table table);
tk_typed_stmt_result tk_type_statement(tk_statement s, tk_env env, tk_type_table table);
tk_tfunction_result  tk_type_function(tk_function f, tk_env env, tk_type_table table);
tk_titem_result      tk_type_item(tk_item item, tk_env env, tk_type_table table);
tk_tprogram_result   tk_type_program(tk_program program);

// cast legality (C2) — exposed so the counter-validation (revalidate.c) re-derives it.
bool tk_cast_ok(tk_type from, tk_type to);

#endif // TK_CHECK_TYPER_H
```

```c
// src/checker/typer.c — each check_* EVOLVED into a type_* producing the typed node
// (C1: every expression node, statement and item). Predicates are re-declared static
// (the per-file convention of expr.c/ctrl.c/match.c); the pattern + exhaustiveness
// logic is shared from match.c (promoted to non-static for the typed pass).
#include "typer.h"
#include "collect.h"   // tk_collect, tk_collected_result

// shared from match.c (E5b-2), promoted to non-static for reuse:
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
bool          tk_exhaustive(tk_arm *arms, size_t n, tk_type subject);

static tk_texpr *box(tk_texpr t) { tk_texpr *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }
static tk_type prim(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
static tk_type void_t(void)         { return (tk_type){ .tag = TK_TYPE_VOID }; }   // return-only marker (no value)
static bool is_bool(tk_type t)      { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t)   { return t.tag == TK_TYPE_PRIM && t.as.prim != TK_PRIM_BOOL; }
static bool is_comparable(tk_type a, tk_type b) { if (is_integer(a) && is_integer(b)) return true; return tk_type_eq(&a, &b); }
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}
static tk_texpr_result xok(tk_texpr t)     { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
static tk_texpr_result xerr(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }
static tk_texpr_result xferr(tk_error e)   { return (tk_texpr_result){ .ok = false, .as.error = e }; }

// growable lists for the typed children (teko::list realized — file-local stamps).
TK_LIST(tk_tcmp_term,  tk_tcmp_list)
TK_LIST(tk_texpr,      tk_texpr_list)
TK_LIST(tk_tarm,       tk_tarm_list)
TK_LIST(tk_tstatement, tk_tstmt_list)
TK_LIST(tk_titem,      tk_titem_list)

// forward decls (mutual recursion: expr ↔ block ↔ statement).
static tk_typed_block_result type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table);
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table);
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table);

// ---- leaves ----
static tk_texpr_result type_var(tk_var v, tk_env env) {
    tk_type_result t = tk_env_lookup(env, v.name); if (!t.ok) return xferr(t.as.error);
    return xok((tk_texpr){ .tag = TK_TEXPR_VAR, .type = t.as.value, .as.var = { v.name } });
}

// ---- operators (same B.22 regimes as check_binary/unary/compare) ----
static tk_texpr_result type_binary(tk_binary b, tk_env env, tk_type_table table) {
    tk_texpr_result l = tk_type_expr(*b.left,  env, table); if (!l.ok) return l;
    tk_texpr_result r = tk_type_expr(*b.right, env, table); if (!r.ok) return r;
    tk_type lt = l.as.value.type, rt = r.as.value.type;
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return xerr("shift needs integer operands");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith_bitwise(b.op)) {
        if (!is_integer(lt)) return xerr("arithmetic/bitwise needs an integer");
        if (!tk_type_eq(&lt, &rt)) return xerr("operands must be the same type (no promotion — B.22)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    return xerr("not a binary operator");
}

static tk_texpr_result type_unary(tk_unary u, tk_env env, tk_type_table table) {
    tk_texpr_result o = tk_type_expr(*u.operand, env, table); if (!o.ok) return o;
    tk_type ot = o.as.value.type;
    if (u.op == TK_TOKEN_MINUS || u.op == TK_TOKEN_TILDE) {
        if (!is_integer(ot)) return xerr("unary -/~ needs an integer");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    if (u.op == TK_TOKEN_BANG) {
        if (!is_bool(ot)) return xerr("! needs a bool");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    return xerr("not a unary operator");
}

static tk_texpr_result type_compare(tk_compare c, tk_env env, tk_type_table table) {
    tk_texpr_result f = tk_type_expr(*c.first, env, table); if (!f.ok) return f;
    tk_type prev = f.as.value.type;
    tk_tcmp_list terms = tk_tcmp_list_empty();
    for (size_t i = 0; i < c.nrest; i += 1) {
        tk_texpr_result cur = tk_type_expr(c.rest[i].operand, env, table); if (!cur.ok) return cur;
        if (!is_comparable(prev, cur.as.value.type)) return xerr("operands are not comparable");
        terms = tk_tcmp_list_push(terms, (tk_tcmp_term){ c.rest[i].op, box(cur.as.value) });
        prev = cur.as.value.type;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE, .type = prim(TK_PRIM_BOOL),
                           .as.compare = { box(f.as.value), terms.ptr, terms.len } });
}

static tk_texpr_result type_call(tk_call c, tk_env env, tk_type_table table) {
    tk_str name = c.callee.segments[c.callee.n - 1].name;
    tk_type_result ftr = tk_env_lookup(env, name); if (!ftr.ok) return xferr(ftr.as.error);
    tk_type ft = ftr.as.value;
    if (ft.tag != TK_TYPE_FUNC) return xerr("not a function");
    if (c.nargs != ft.as.func.nparams) return xerr("wrong number of arguments");
    tk_texpr_list args = tk_texpr_list_empty();
    for (size_t i = 0; i < c.nargs; i += 1) {
        tk_texpr_result a = tk_type_expr(c.args[i], env, table); if (!a.ok) return a;
        if (!tk_type_eq(&a.as.value.type, &ft.as.func.params[i])) return xerr("argument type mismatch");
        args = tk_texpr_list_push(args, a.as.value);
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = *ft.as.func.ret,
                           .as.call = { c.callee, args.ptr, args.len } });
}

// ---- cast `to` (C2): a DEFINED conversion is allowed; loss is caught, never silent (M.1) ----
// Two-pronged: a constant out-of-range operand → compile error here; a runtime value's loss →
// codegen guard (debug-panic / release-defined, like overflow), deferred (M.4). Bool/non-numeric
// undefined. (Redefines the early compile-forbid rule — see the Redefinitions Index.)
static bool value_fits(int64_t v, tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   return v >= 0 && v <= 255;
        case TK_PRIM_U16:  return v >= 0 && v <= 65535;
        case TK_PRIM_U32:  return v >= 0 && v <= 4294967295;
        case TK_PRIM_U64:  return v >= 0;                         // i64 max < u64 max
        case TK_PRIM_I8:   return v >= -128 && v <= 127;
        case TK_PRIM_I16:  return v >= -32768 && v <= 32767;
        case TK_PRIM_I32:  return v >= -2147483648 && v <= 2147483647;
        case TK_PRIM_I64:  return true;                          // v is already an i64
        case TK_PRIM_BOOL: return false;                         // guarded by the caller
    }
    return false;
}
// C6: an annotated binding whose value type ≠ T. A numeric literal adopts T if it fits (leaf stays
// i64 — Side D); a non-literal mismatch or out-of-range literal is rejected. NULL = ok.
// Used by the typed type_binding (reuses value_fits).
const char *annotated_literal_reason(tk_expr value, tk_type ann) {
    if (value.tag != TK_EXPR_NUMBER || ann.tag != TK_TYPE_PRIM) return "value type does not match annotation";
    if (!value_fits(value.as.number.value, ann.as.prim)) return "literal out of range for the annotated type (M.1 — fail early)";
    return NULL;
}
// is `from -> to` a DEFINED conversion? NULL = yes; else the M.3 barrier message. Any integer ->
// any integer is defined (B — the loss is runtime/codegen's). Only Bool / non-numeric are rejected.
// byte casts AS u8 (B.36 "byte = u8 newtype"): the effective prim kind for range/cast rules.
// false for bool / non-numeric (no cast kind); true with *out set otherwise. M.5 — shared by
// cast_reason + the constant-range check in type_cast.
static bool cast_kind(tk_type t, tk_prim_kind *out) {
    if (t.tag == TK_TYPE_PRIM) {
        if (t.as.prim == TK_PRIM_BOOL) return false;
        *out = t.as.prim;
        return true;
    }
    if (t.tag == TK_TYPE_BYTE) { *out = TK_PRIM_U8; return true; }
    return false;
}
static const char *cast_reason(tk_type from, tk_type to) {
    if (tk_type_eq(&from, &to)) return NULL;                     // same type — a no-op
    if ((from.tag == TK_TYPE_PRIM && from.as.prim == TK_PRIM_BOOL)
        || (to.tag == TK_TYPE_PRIM && to.as.prim == TK_PRIM_BOOL))
        return "bool casts are not defined in the seed";        // bool on either end — distinct message (C2)
    tk_prim_kind kf, kt;
    if (!cast_kind(from, &kf)) return "cast not defined for this type in the seed (Named/Str/Slice/… — pending)";
    if (!cast_kind(to,   &kt)) return "cast not defined: a primitive to a non-primitive type";
    return NULL;                                                // any integer/byte -> any integer/byte (B; byte AS u8)
}
bool tk_cast_ok(tk_type from, tk_type to) { return cast_reason(from, to) == NULL; }

static tk_texpr_result type_cast(tk_cast c, tk_env env, tk_type_table table) {
    tk_texpr_result inner = tk_type_expr(*c.expr, env, table); if (!inner.ok) return inner;
    tk_type_result tgt = tk_resolve_type(c.target, table);     if (!tgt.ok) return xferr(tgt.as.error);
    const char *why = cast_reason(inner.as.value.type, tgt.as.value);
    if (why != NULL) return xerr(why);
    // fail early (M.1): a constant literal already out of the target's range is a compile error.
    // The target's effective kind comes from cast_kind, so `… to byte` checks the U8 range (0..255).
    tk_prim_kind ck;
    if (c.expr->tag == TK_EXPR_NUMBER && cast_kind(tgt.as.value, &ck)
        && !value_fits(c.expr->as.number.value, ck))
        return xerr("constant out of range for the cast target (M.1 — fail early)");
    return xok((tk_texpr){ .tag = TK_TEXPR_CAST, .type = tgt.as.value, .as.cast = { box(inner.as.value) } });
}

// ---- field access `x.field` (C3): read a struct field; `.type` is the field's resolved type ----
static bool tk_str_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }

// non-static: shared with match.c (the FieldPattern case forward-declares it — C7a).
tk_type_result field_type(tk_struct_body sb, tk_str field, tk_type_table table) {
    for (size_t i = 0; i < sb.n_fields; i += 1)
        if (tk_str_eq(sb.fields[i].name, field)) return tk_resolve_type(sb.fields[i].type_ann, table);
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("no such field") };
}

static tk_texpr_result type_field_access(tk_field_access fa, tk_env env, tk_type_table table) {
    tk_texpr_result recv = tk_type_expr(*fa.receiver, env, table); if (!recv.ok) return recv;
    if (recv.as.value.type.tag != TK_TYPE_NAMED) return xerr("field access requires a struct receiver");
    tk_decl_result decl = tk_type_table_find(table, recv.as.value.type.as.named.name);
    if (!decl.ok) return xerr("unknown type for field access");
    if (decl.as.value.body.tag != TK_BODY_STRUCT) return xerr("type is not a struct (no fields)");
    tk_type_result ft = field_type(decl.as.value.body.as.struct_body, fa.field, table);
    if (!ft.ok) return xerr("no such field");
    return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = ft.as.value,
                           .as.field_access = { box(recv.as.value), fa.field } });
}

// ---- the expression dispatch (the evolved check_expr) ----
tk_texpr_result tk_type_expr(tk_expr e, tk_env env, tk_type_table table) {
    switch (e.tag) {
        case TK_EXPR_NUMBER: return xok((tk_texpr){ .tag = TK_TEXPR_NUMBER, .type = prim(TK_PRIM_I64), .as.number = { e.as.number.value } });
        case TK_EXPR_STR:    return xok((tk_texpr){ .tag = TK_TEXPR_STR,  .type = (tk_type){ .tag = TK_TYPE_STR },  .as.str  = { e.as.str.text } });
        case TK_EXPR_BYTE:   return xok((tk_texpr){ .tag = TK_TEXPR_BYTE, .type = (tk_type){ .tag = TK_TYPE_BYTE }, .as.byte = { e.as.byte.value } });
        case TK_EXPR_VAR:    return type_var(e.as.var, env);
        case TK_EXPR_BINARY: return type_binary(e.as.binary, env, table);
        case TK_EXPR_UNARY:  return type_unary(e.as.unary, env, table);
        case TK_EXPR_COMPARE:return type_compare(e.as.compare, env, table);
        case TK_EXPR_CALL:   return type_call(e.as.call, env, table);
        case TK_EXPR_IF:     return type_if(e.as.if_expr, env, table);
        case TK_EXPR_MATCH:  return type_match(e.as.match_expr, env, table);
        case TK_EXPR_FIELD_ACCESS: return type_field_access(e.as.field_access, env, table);
        case TK_EXPR_CAST:         return type_cast(e.as.cast, env, table);
        case TK_EXPR_METHOD_CALL:  return xerr("method typing is deferred (B.29 / M.4)");
        case TK_EXPR_PATH:         return xerr("path-expr typing pending (Enum::Member)");
    }
    return xerr("unknown expression");
}

// ---- the value-type a typed block yields ----
static tk_type tblock_type(tk_tstatement *stmts, size_t n) {
    if (n == 0) return void_t();
    if (stmts[n - 1].tag == TK_TSTMT_EXPR) return stmts[n - 1].as.expr_stmt.expr.type;
    return void_t();
}

// ---- a typed block: thread the env, collect typed statements ----
static tk_typed_block_result type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table) {
    tk_env cur = env;
    tk_tstmt_list out = tk_tstmt_list_empty();
    for (size_t i = 0; i < n; i += 1) {
        tk_typed_stmt_result ts = tk_type_statement(stmts[i], cur, table);
        if (!ts.ok) return (tk_typed_block_result){ .ok = false, .as.error = ts.as.error };
        out = tk_tstmt_list_push(out, ts.as.value.node);
        cur = ts.as.value.env;
    }
    return (tk_typed_block_result){ .ok = true, .as.value = { .stmts = out.ptr, .n = out.len, .env = cur } };
}

// ---- if as a VALUE ----
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_type_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    if (!f.has_else)               return xerr("an `if` used as a value needs an `else`");
    tk_typed_block_result tb = type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_typed_block_result eb = type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
    tk_type tt = tblock_type(tb.as.value.stmts, tb.as.value.n);
    tk_type et = tblock_type(eb.as.value.stmts, eb.as.value.n);
    if (!tk_type_eq(&tt, &et)) return xerr("the `if` branches have different types");
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = tt, .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, true, eb.as.value.stmts, eb.as.value.n } });
}

// ---- if as a STATEMENT (value discarded → void) ----
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_type_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    tk_typed_block_result tb = type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_tstatement *eb_stmts = tb.as.value.stmts; size_t eb_n = 0;   // gated by has_else
    if (f.has_else) {
        tk_typed_block_result eb = type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
        eb_stmts = eb.as.value.stmts; eb_n = eb.as.value.n;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = void_t(), .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, f.has_else, eb_stmts, eb_n } });
}

// ---- one typed arm ----
static tk_tarm_result type_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table) {
    tk_env_result e2 = tk_check_pattern(a.pattern, subject, env, table);
    if (!e2.ok) return (tk_tarm_result){ .ok = false, .as.error = e2.as.error };
    tk_texpr_result body = tk_type_expr(a.body, e2.as.value, table);
    if (!body.ok) return (tk_tarm_result){ .ok = false, .as.error = body.as.error };
    tk_texpr *bodyp = box(body.as.value);
    tk_texpr *guard = bodyp;                         // gated by has_when (placeholder reuses body)
    if (a.has_when) {
        tk_texpr_result g = tk_type_expr(a.guard, e2.as.value, table);
        if (!g.ok) return (tk_tarm_result){ .ok = false, .as.error = g.as.error };
        if (!is_bool(g.as.value.type)) return (tk_tarm_result){ .ok = false, .as.error = tk_error_make("a `when` guard must be a bool") };
        guard = box(g.as.value);
    }
    return (tk_tarm_result){ .ok = true, .as.value = { .pattern = a.pattern, .has_when = a.has_when, .guard = guard, .body = bodyp } };
}

// ---- match as a VALUE ----
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_type_expr(*m.subject, env, table); if (!s.ok) return s;
    if (m.narms == 0) return xerr("a `match` needs at least one arm");
    tk_tarm_result a0 = type_arm(m.arms[0], s.as.value.type, env, table); if (!a0.ok) return xferr(a0.as.error);
    tk_type first = a0.as.value.body->type;
    tk_tarm_list arms = tk_tarm_list_empty();
    arms = tk_tarm_list_push(arms, a0.as.value);
    for (size_t i = 1; i < m.narms; i += 1) {
        tk_tarm_result ai = type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        if (!tk_type_eq(&ai.as.value.body->type, &first)) return xerr("the `match` arms have different types");
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = first, .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}

// ---- match as a STATEMENT (value discarded → void) ----
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_type_expr(*m.subject, env, table); if (!s.ok) return s;
    tk_tarm_list arms = tk_tarm_list_empty();
    for (size_t i = 0; i < m.narms; i += 1) {
        tk_tarm_result ai = type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = void_t(), .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}

// ---- statements ----
static tk_typed_stmt_result sok(tk_tstatement node, tk_env env) { return (tk_typed_stmt_result){ .ok = true, .as.value = { node, env } }; }
static tk_typed_stmt_result sfail(tk_error e)   { return (tk_typed_stmt_result){ .ok = false, .as.error = e }; }
static tk_typed_stmt_result smsg(const char *m) { return sfail(tk_error_make(m)); }

static tk_typed_stmt_result type_binding(tk_binding b, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_type_expr(b.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_type bound = v.as.value.type;
    if (b.has_type) {
        tk_type_result a = tk_resolve_type(b.type_ann, table); if (!a.ok) return sfail(a.as.error);
        if (!tk_type_eq(&v.as.value.type, &a.as.value)) {       // C6: a fitting literal adopts T (leaf stays i64)
            const char *why = annotated_literal_reason(b.value, a.as.value);
            if (why != NULL) return smsg(why);
        }
        bound = a.as.value;
    }
    tk_tstatement node = { .tag = TK_TSTMT_BINDING, .as.binding = { b.kind, b.target, bound, v.as.value } };
    if (b.target.tag == TK_BIND_SIMPLE)
        return sok(node, tk_env_define(env, b.target.as.simple.name, bound, tk_bind_is_mut(b.kind)));
    return sok(node, env);   // [destructuring binding: refinement]
}

static tk_typed_stmt_result type_assign(tk_assign a, tk_env env, tk_type_table table) {
    tk_binding_result tb = tk_env_lookup_binding(env, a.name); if (!tb.ok) return sfail(tb.as.error);
    if (!tb.as.value.is_mut) return smsg("cannot assign to immutable binding — declare it `mut` (B.21)");
    tk_texpr_result v = tk_type_expr(a.value, env, table); if (!v.ok) return sfail(v.as.error);
    if (!tk_type_eq(&tb.as.value.type, &v.as.value.type)) return smsg("assigned value does not match the target type");
    tk_tstatement node = { .tag = TK_TSTMT_ASSIGN, .as.assign = { a.name, a.op, v.as.value } };
    return sok(node, env);   // mut rule enforced (B.21)
}

static tk_typed_stmt_result type_return(tk_return r, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_type_expr(r.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_RETURN, .as.ret = { r.has_value, v.as.value } };
    return sok(node, env);   // value gated by has_value (B.20); return-type match enforced by tk_type_function's check_returns (C5)
}

static tk_typed_stmt_result type_loop(tk_loop_stmt l, tk_env env, tk_type_table table) {
    tk_typed_block_result tb = type_block(l.body, l.nbody, env, table); if (!tb.ok) return sfail(tb.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_LOOP, .as.loop_stmt = { tb.as.value.stmts, tb.as.value.n } };
    return sok(node, env);   // body bindings stay block-local
}

static tk_typed_stmt_result type_exprstmt(tk_expr_stmt es, tk_env env, tk_type_table table) {
    tk_texpr_result te;
    if (es.expr.tag == TK_EXPR_IF)         te = type_if_stmt(es.expr.as.if_expr, env, table);
    else if (es.expr.tag == TK_EXPR_MATCH) te = type_match_stmt(es.expr.as.match_expr, env, table);
    else                                   te = tk_type_expr(es.expr, env, table);
    if (!te.ok) return sfail(te.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_EXPR, .as.expr_stmt = { te.as.value } };
    return sok(node, env);
}

tk_typed_stmt_result tk_type_statement(tk_statement s, tk_env env, tk_type_table table) {
    switch (s.tag) {
        case TK_STMT_BINDING:  return type_binding(s.as.binding, env, table);
        case TK_STMT_ASSIGN:   return type_assign(s.as.assign, env, table);
        case TK_STMT_RETURN:   return type_return(s.as.ret, env, table);
        case TK_STMT_LOOP:     return type_loop(s.as.loop_stmt, env, table);
        case TK_STMT_BREAK:    return sok((tk_tstatement){ .tag = TK_TSTMT_BREAK }, env);
        case TK_STMT_CONTINUE: return sok((tk_tstatement){ .tag = TK_TSTMT_CONTINUE }, env);
        case TK_STMT_EXPR:     return type_exprstmt(s.as.expr_stmt, env, table);
    }
    return smsg("unknown statement");
}

// ---- items + program ----
static tk_type function_return(tk_function f, tk_type_table table) {
    if (!f.has_return) return void_t();    // returns no value → the `-> void` marker
    tk_type_result r = tk_resolve_type(f.return_type, table);
    return r.ok ? r.as.value : void_t();   // collect validated signatures; a bad annotation surfaces there
}

// ---- C5: return / final-expr vs the declared return type (see the Teko twin; NULL = ok) ----
static bool assignable_to(tk_type from, tk_type to) {   // B.14 — variant member inclusion
    if (tk_type_eq(&from, &to)) return true;
    if (to.tag == TK_TYPE_VARIANT)
        for (size_t i = 0; i < to.as.variant.len; i += 1)
            if (tk_type_eq(&from, &to.as.variant.members[i])) return true;
    return false;
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret);   // fwd (mutual)
static const char *check_returns_inexpr(const tk_texpr *e, tk_type ret) {
    if (e->tag == TK_TEXPR_IF) {
        const char *t = check_returns(e->as.if_expr.then_blk, e->as.if_expr.nthen, ret); if (t) return t;
        return check_returns(e->as.if_expr.else_blk, e->as.if_expr.nelse, ret);
    }
    return NULL;   // match-arm returns await the divergence item
}
static const char *check_return_stmt(const tk_tstatement *s, tk_type ret) {
    switch (s->tag) {
        case TK_TSTMT_RETURN:
            if (s->as.ret.has_value)
                return assignable_to(s->as.ret.value.type, ret) ? NULL
                     : "return value does not match the function's declared return type";
            return assignable_to(void_t(), ret) ? NULL
                 : "bare `return` in a function that declares a non-void return type";
        case TK_TSTMT_LOOP: return check_returns(s->as.loop_stmt.body, s->as.loop_stmt.nbody, ret);
        case TK_TSTMT_EXPR: return check_returns_inexpr(&s->as.expr_stmt.expr, ret);
        default:            return NULL;
    }
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret) {
    for (size_t i = 0; i < n; i += 1) { const char *e = check_return_stmt(&stmts[i], ret); if (e) return e; }
    return NULL;
}
static const char *check_trailing_value(const tk_tstatement *stmts, size_t n, tk_type ret) {
    if (n == 0) return NULL;
    const tk_tstatement *last = &stmts[n - 1];
    if (last->tag != TK_TSTMT_EXPR) return NULL;   // trailing loop/if/match → no claim (guard)
    return assignable_to(last->as.expr_stmt.expr.type, ret) ? NULL
         : "the function's final expression does not match its declared return type";
}

tk_tfunction_result tk_type_function(tk_function f, tk_env env, tk_type_table table) {
    tk_env local = env;
    for (size_t i = 0; i < f.nparams; i += 1) {           // params immutable (B.21)
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) return (tk_tfunction_result){ .ok = false, .as.error = pt.as.error };
        local = tk_env_define(local, f.params[i].name, pt.as.value, false);
    }
    tk_type ret = function_return(f, table);
    tk_typed_block_result tb = type_block(f.body, f.nbody, local, table);
    if (!tb.ok) return (tk_tfunction_result){ .ok = false, .as.error = tb.as.error };
    { const char *why = check_returns(tb.as.value.stmts, tb.as.value.n, ret);        // C5: each `return e` matches
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    { const char *why = check_trailing_value(tb.as.value.stmts, tb.as.value.n, ret); // C5: trailing value (when present)
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_tfunction tf = { .name = f.name, .params = f.params, .nparams = f.nparams,
                        .return_type = ret, .body = tb.as.value.stmts, .nbody = tb.as.value.n, .is_exp = f.is_exp };
    return (tk_tfunction_result){ .ok = true, .as.value = tf };
}

tk_titem_result tk_type_item(tk_item item, tk_env env, tk_type_table table) {
    switch (item.tag) {
        case TK_ITEM_FUNCTION: {
            tk_tfunction_result tf = tk_type_function(item.as.function, env, table);
            if (!tf.ok) return (tk_titem_result){ .ok = false, .as.error = tf.as.error };
            return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_FUNCTION, .as.function = tf.as.value } };
        }
        case TK_ITEM_TYPE_DECL: return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_TYPE_DECL, .as.type_decl = item.as.type_decl } };
        case TK_ITEM_USE:       return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_USE, .as.use_decl = item.as.use_decl } };
        case TK_ITEM_STATEMENT: {
            tk_typed_stmt_result ts = tk_type_statement(item.as.statement, env, table);
            if (!ts.ok) return (tk_titem_result){ .ok = false, .as.error = ts.as.error };
            return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node } };
        }
    }
    return (tk_titem_result){ .ok = false, .as.error = tk_error_make("unknown item") };
}

tk_tprogram_result tk_type_program(tk_program program) {
    tk_collected_result c = tk_collect(program);
    if (!c.ok) return (tk_tprogram_result){ .ok = false, .as.error = c.as.error };
    tk_titem_list items = tk_titem_list_empty();
    for (size_t i = 0; i < program.len; i += 1) {
        tk_titem_result ti = tk_type_item(program.items[i], c.as.value.env, c.as.value.types);
        if (!ti.ok) return (tk_tprogram_result){ .ok = false, .as.error = ti.as.error };
        items = tk_titem_list_push(items, ti.as.value);
    }
    return (tk_tprogram_result){ .ok = true, .as.value = { .items = items.ptr, .nitems = items.len } };
}
```

### Teko — `src/checker/checker_test.tkt` (testes do typer — C1)

```teko
// src/checker/checker_test.tkt — tests for the typer (C1). NO main: PUBLIC #test
// functions the compiler collects (a runner — ALPHA). C mirror: a `main` harness.
// The typer is unit-tested on HAND-BUILT AST (the parser is tested elsewhere): each
// type_* must produce a fully typed node (happy) or refuse an ill-typed one (barrier).

// --- small Type/AST constructors (keep the tests legible — M.2) ---
fn u32t()  -> Type { Prim { kind = PrimKind::U32 } }
fn u8t()   -> Type { Prim { kind = PrimKind::U8 } }
fn i64t()  -> Type { Prim { kind = PrimKind::I64 } }
fn boolt() -> Type { Prim { kind = PrimKind::Bool } }
fn empty_env()   -> Env       { teko::list::empty() }
fn empty_table() -> TypeTable { teko::list::empty() }
fn path1(name: str) -> Path { Path { segments = teko::list::push(teko::list::empty(), Segment { name = name }) } }
fn prim_is(t: Type, k: PrimKind) -> bool { match t { Prim as p => p.kind == k; _ => false } }

#test
fn types_leaves() {
    // happy: each literal leaf carries its type.
    let n = type_number(Number { value = 5 })
    assert prim_is(n.type, PrimKind::I64)
    match n.kind { TNumber as tn => assert tn.value == 5; _ => assert false }
    let s = type_strlit(StrLit { text = "hi" })
    match s.type { Str => assert true; _ => assert false }
    let b = type_bytelit(ByteLit { value = b'A' })
    match b.type { Byte => assert true; _ => assert false }
}

#test
fn types_var_and_binary() {
    // happy: a var resolves via the env; a same-type binary yields that type (B.22).
    let env = define(empty_env(), "x", u32t(), false)
    let vx = match type_var(Var { name = "x" }, env) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(vx.type, PrimKind::U32)
    let bin = Binary { op = lexer::TokenKind::Plus; left = Var { name = "x" }; right = Var { name = "x" } }
    let tb = match type_binary(bin, env, empty_table()) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(tb.type, PrimKind::U32)
    match tb.kind {
        TBinary as bb => {
            assert prim_is(bb.left.type, PrimKind::U32)
            assert prim_is(bb.right.type, PrimKind::U32)
        }
        _ => assert false
    }
}

#test
fn types_compare_and_unary() {
    // happy: a comparison chain is bool; `!` on a bool is bool.
    let env = define(empty_env(), "x", u32t(), false)
    let crest = teko::list::push(teko::list::empty(), CmpTerm { op = lexer::TokenKind::Lt; operand = Var { name = "x" } })
    let cmp = Compare { first = Var { name = "x" }; rest = crest }
    let tc = match type_compare(cmp, env, empty_table()) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(tc.type, PrimKind::Bool)
    let un = Unary { op = lexer::TokenKind::Bang; operand = cmp }   // !(x < x) — operand is bool
    let tu = match type_unary(un, env, empty_table()) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(tu.type, PrimKind::Bool)
}

#test
fn types_call() {
    // happy: a call checks args against the signature and yields the return type.
    let env0 = define(empty_env(), "x", u32t(), false)
    let params: []Type = teko::list::push(teko::list::empty(), u32t())
    let env = define(env0, "f", Func { params = params; ret = boolt() }, false)
    let args: []Expr = teko::list::push(teko::list::empty(), Var { name = "x" })
    let tcall = match type_call(Call { callee = path1("f"); args = args }, env, empty_table()) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(tcall.type, PrimKind::Bool)
    match tcall.kind { TCall as cc => assert cc.args.len == 1; _ => assert false }
}

#test
fn types_if_value() {
    // happy: an `if` value — cond bool, both branches i64 → i64.
    let env = define(empty_env(), "x", u32t(), false)
    let crest = teko::list::push(teko::list::empty(), CmpTerm { op = lexer::TokenKind::Lt; operand = Var { name = "x" } })
    let cond = Compare { first = Var { name = "x" }; rest = crest }
    let thenb: []Statement = teko::list::push(teko::list::empty(), ExprStmt { expr = Number { value = 1 } })
    let elseb: []Statement = teko::list::push(teko::list::empty(), ExprStmt { expr = Number { value = 2 } })
    let iff = IfExpr { cond = cond; then_blk = thenb; has_else = true; else_blk = elseb }
    let ti = match type_if(iff, env, empty_table()) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(ti.type, PrimKind::I64)
    match ti.kind { TIfExpr as tif => assert tif.has_else; _ => assert false }
}

#test
fn types_match() {
    // happy: a `match` over an integer — a literal arm + a `_` valve (exhaustive via `_`).
    let env = define(empty_env(), "n", i64t(), false)
    let lit  = Arm { pattern = LiteralPattern { value = Number { value = 0 } }; has_when = false; guard = Number { value = 0 }; body = Number { value = 10 } }
    let wild = Arm { pattern = WildcardPattern { };                              has_when = false; guard = Number { value = 0 }; body = Number { value = 20 } }
    let arms: []Arm = teko::list::push(teko::list::push(teko::list::empty(), lit), wild)
    let m = MatchExpr { subject = Var { name = "n" }; arms = arms }
    let tm = match type_match(m, env, empty_table()) { TExpr as e => e; error => { assert false; return } }
    assert prim_is(tm.type, PrimKind::I64)
    match tm.kind { TMatchExpr as tmm => assert tmm.arms.len == 2; _ => assert false }
}

#test
fn types_function_and_program() {
    // happy: a whole function types into a fully typed body; the program driver runs
    // collect (pass 1) + type each item (pass 2).
    let p = Param { name = "x"; type_ann = NamedType { path = path1("u32") } }
    let params: []Param = teko::list::push(teko::list::empty(), p)
    let body: []Statement = teko::list::push(teko::list::empty(), ExprStmt { expr = Var { name = "x" } })
    let f = Function {
        name = "id"
        params = params
        has_return = true
        return_type = NamedType { path = path1("u32") }
        body = body
        is_exp = false
        has_doc = false
        doc = ""
    }
    let tf = match type_function(f, empty_env(), empty_table()) { TFunction as x => x; error => { assert false; return } }
    assert prim_is(tf.return_type, PrimKind::U32)
    assert tf.body.len == 1
    match tf.body[0] { TExprStmt as es => assert prim_is(es.expr.type, PrimKind::U32); _ => assert false }

    let items: []Item = teko::list::push(teko::list::empty(), f)
    let tp = match type_program(Program { items = items }) { TProgram as x => x; error => { assert false; return } }
    assert tp.items.len == 1
    match tp.items[0] { TFunction as ff => assert prim_is(ff.return_type, PrimKind::U32); _ => assert false }
}

#test
fn rejects_ill_typed() {
    // barrier: no untyped node escapes (M.1/M.3) — each of these MUST fail to type.
    let env2 = define(define(empty_env(), "x", u32t(), false), "y", u8t(), false)

    // mixed-width arithmetic — no promotion (B.22).
    let mix = Binary { op = lexer::TokenKind::Plus; left = Var { name = "x" }; right = Var { name = "y" } }
    match type_binary(mix, env2, empty_table()) { TExpr => assert false; error => assert true }

    // an undefined name has no type.
    match type_var(Var { name = "z" }, empty_env()) { TExpr => assert false; error => assert true }

    // an `if` used as a VALUE needs an `else` (B.20).
    let crest = teko::list::push(teko::list::empty(), CmpTerm { op = lexer::TokenKind::Lt; operand = Var { name = "x" } })
    let cond  = Compare { first = Var { name = "x" }; rest = crest }
    let thenb: []Statement = teko::list::push(teko::list::empty(), ExprStmt { expr = Number { value = 1 } })
    let noelse = IfExpr { cond = cond; then_blk = thenb; has_else = false; else_blk = thenb }
    match type_if(noelse, env2, empty_table()) { TExpr => assert false; error => assert true }

    // a non-exhaustive `match` (integer subject, only a literal arm, no `_`).
    let envi = define(empty_env(), "n", i64t(), false)
    let lit  = Arm { pattern = LiteralPattern { value = Number { value = 0 } }; has_when = false; guard = Number { value = 0 }; body = Number { value = 1 } }
    let arms: []Arm = teko::list::push(teko::list::empty(), lit)
    match type_match(MatchExpr { subject = Var { name = "n" }; arms = arms }, envi, empty_table()) { TExpr => assert false; error => assert true }
}

// --- C2 cast helpers ---
fn i8t()  -> Type { Prim { kind = PrimKind::I8 } }
fn i16t() -> Type { Prim { kind = PrimKind::I16 } }
fn i32t() -> Type { Prim { kind = PrimKind::I32 } }
fn strt() -> Type { Str { } }
// a `varname to target_name` cast over the parser AST.
fn cast_of(varname: str, target_name: str) -> Cast {
    Cast { expr = Var { name = varname }; target = NamedType { path = path1(target_name) } }
}
// `varname: src` cast to `target_name` — true iff it types to the primitive `want`.
fn cast_types_to(varname: str, src: Type, target_name: str, want: PrimKind) -> bool {
    let env = define(empty_env(), varname, src, false)
    match type_cast(cast_of(varname, target_name), env, empty_table()) {
        TExpr as te => prim_is(te.type, want)
        error       => false
    }
}
// does the cast fail to type (the barrier)?
fn cast_errors(varname: str, src: Type, target_name: str) -> bool {
    let env = define(empty_env(), varname, src, false)
    match type_cast(cast_of(varname, target_name), env, empty_table()) {
        TExpr => false
        error => true
    }
}
// a CONSTANT literal `v to target_name` — true iff it types to `want`.
fn cast_const_types_to(v: i64, target_name: str, want: PrimKind) -> bool {
    let c = Cast { expr = Number { value = v }; target = NamedType { path = path1(target_name) } }
    match type_cast(c, empty_env(), empty_table()) { TExpr as te => prim_is(te.type, want); error => false }
}
// does a CONSTANT cast fail (out of range — the fail-early barrier)?
fn cast_const_errors(v: i64, target_name: str) -> bool {
    let c = Cast { expr = Number { value = v }; target = NamedType { path = path1(target_name) } }
    match type_cast(c, empty_env(), empty_table()) { TExpr => false; error => true }
}

#test
fn casts_allowed() {
    // C2a (B) — every DEFINED numeric conversion type-checks; loss (if any) is a RUNTIME guard
    // (codegen), not a type error. The value might fit, so the metal conversion is kept (M.0).
    assert cast_types_to("a", i8t(),  "i32", PrimKind::I32)   // widening
    assert cast_types_to("c", i32t(), "i8",  PrimKind::I8)    // NARROWING — allowed (runtime-guarded), not a compile error
    assert cast_types_to("d", i32t(), "u32", PrimKind::U32)   // signed → unsigned — allowed (runtime-guarded)
    assert cast_types_to("e", u32t(), "i8",  PrimKind::I8)    // unsigned → narrower signed — allowed
    assert cast_types_to("f", i32t(), "i32", PrimKind::I32)   // same-type no-op
    // a CONSTANT that FITS the (narrower) target types fine — no fail-early trip.
    assert cast_const_types_to(200, "u8",  PrimKind::U8)
    assert cast_const_types_to(5,   "i8",  PrimKind::I8)
}

#test
fn casts_forbidden() {
    // C2b (B) — only UNDEFINED conversions and COMPILE-TIME-KNOWN out-of-range constants error.
    assert cast_errors("a", boolt(), "i32")    // bool casts undefined in the seed …
    assert cast_errors("b", i32t(),  "bool")   // … either direction
    assert cast_errors("c", strt(),  "i32")    // non-numeric source
    // a CONSTANT the compiler can see out of range → compile error (fail early — M.1).
    assert cast_const_errors(300, "i8")          // 300 ∉ [-128, 127]
    assert cast_const_errors(-1,  "u8")          // -1 ∉ [0, 255]
    assert cast_const_errors(5000000000, "i32")  // > i32 max
}

#test
fn revalidate_rederives_casts() {
    // the counter-validation RE-PROVES the conversion is DEFINED (M.3 — not a rubber stamp).
    let goodinner = TExpr { kind = TVar { name = "y" }; type = i32t() }
    let good = TExpr { kind = TCast { expr = goodinner }; type = i8t() }    // i32 → i8 is a DEFINED cast (B)
    match validate_texpr(good) { null => assert true; error => assert false }

    let badinner = TExpr { kind = TVar { name = "s" }; type = strt() }
    let bad = TExpr { kind = TCast { expr = badinner }; type = i32t() }     // str → i32 is UNDEFINED
    match validate_texpr(bad) { null => assert false; error => assert true }
}

// --- byte↔int cast helpers (byte casts AS u8 — B.36) ---
fn bytet() -> Type { Byte { } }
// byte is the cast TARGET: the result type is Byte{} (not a Prim), so assert via type_eq.
fn cast_types_to_byte(varname: str, src: Type) -> bool {
    let env = define(empty_env(), varname, src, false)
    match type_cast(cast_of(varname, "byte"), env, empty_table()) {
        TExpr as te => type_eq(te.type, Byte { })
        error       => false
    }
}
// a CONSTANT `v to byte` — true iff it types (result is Byte{}).
fn cast_const_types_to_byte(v: i64) -> bool {
    let c = Cast { expr = Number { value = v }; target = NamedType { path = path1("byte") } }
    match type_cast(c, empty_env(), empty_table()) { TExpr as te => type_eq(te.type, Byte { }); error => false }
}

#test
fn byte_int_casts_allowed() {
    // byte casts AS u8 (B.36): byte ↔ any integer is DEFINED, both directions.
    assert cast_types_to("a", bytet(), "u32", PrimKind::U32)   // byte → wider unsigned (lossless — M.1)
    assert cast_types_to("b", bytet(), "u8",  PrimKind::U8)    // byte → u8 (its own values)
    assert cast_types_to("c", bytet(), "i64", PrimKind::I64)   // byte → wide signed
    assert cast_types_to_byte("d", u8t())                      // u8  → byte
    assert cast_types_to_byte("e", u32t())                     // u32 → byte (narrowing — runtime-guarded)
    assert cast_const_types_to_byte(200)                       // constant 200 ∈ [0, 255]
}

#test
fn byte_int_casts_forbidden() {
    assert cast_const_errors(300, "byte")     // 300 ∉ [0, 255] — fail early (M.1)
    assert cast_const_errors(-1,  "byte")     // -1  ∉ [0, 255]
    assert cast_errors("a", boolt(), "byte")  // bool → byte undefined (bool has no cast kind)
    assert cast_errors("b", bytet(), "bool")  // byte → bool undefined (either direction)
    assert cast_errors("c", bytet(), "str")   // byte → non-numeric undefined
}

#test
fn revalidate_rederives_byte_casts() {
    // the counter-validation RE-PROVES byte↔int is DEFINED (M.3).
    let gi  = TExpr { kind = TVar { name = "b" }; type = bytet() }
    let g   = TExpr { kind = TCast { expr = gi }; type = u32t() }    // byte → u32 DEFINED (B; byte AS u8)
    match validate_texpr(g) { null => assert true; error => assert false }
    let gi2 = TExpr { kind = TVar { name = "x" }; type = u8t() }
    let g2  = TExpr { kind = TCast { expr = gi2 }; type = bytet() }  // u8 → byte DEFINED
    match validate_texpr(g2) { null => assert true; error => assert false }
    let bi  = TExpr { kind = TVar { name = "b" }; type = bytet() }
    let bad = TExpr { kind = TCast { expr = bi }; type = boolt() }   // byte → bool UNDEFINED
    match validate_texpr(bad) { null => assert false; error => assert true }
}

// --- C3 field-access helpers (reuse path1/empty_env/define/prim_is from above) ---
// a TypeTable with one struct `Foo { token: u8 }`.
fn foo_table() -> TypeTable {
    let fld = Field { name = "token"; type_ann = NamedType { path = path1("u8") } }
    let fields = teko::list::push(teko::list::empty(), fld)
    let td = TypeDecl { name = "Foo"; body = StructBody { fields = fields }; is_exp = false; has_doc = false; doc = "" }
    teko::list::push(teko::list::empty(), TypeReg { name = "Foo"; decl = td })
}
fn foo_env() -> Env { define(empty_env(), "s", Named { name = "Foo" }, false) }   // s : Foo

#test
fn field_read_types_to_field_type() {
    // happy: s: Foo, Foo = struct { token: u8 } ⇒ s.token : u8.
    let fa = FieldAccess { receiver = Var { name = "s" }; field = "token" }
    match type_field_access(fa, foo_env(), foo_table()) {
        TExpr as te => assert prim_is(te.type, PrimKind::U8)
        error       => assert false
    }
}

#test
fn field_not_found_errors() {
    // barrier: a field absent from the struct.
    let fa = FieldAccess { receiver = Var { name = "s" }; field = "nope" }
    match type_field_access(fa, foo_env(), foo_table()) { TExpr => assert false; error => assert true }
}

#test
fn field_on_non_struct_errors() {
    // barrier: receiver is not a struct (a plain integer var).
    let env = define(empty_env(), "n", Prim { kind = PrimKind::I32 }, false)
    let fa = FieldAccess { receiver = Var { name = "n" }; field = "token" }
    match type_field_access(fa, env, foo_table()) { TExpr => assert false; error => assert true }
}

#test
fn revalidate_rederives_field_access() {
    // a well-formed field access over a Named receiver re-validates …
    let recv = TExpr { kind = TVar { name = "s" }; type = Named { name = "Foo" } }
    let good = TExpr { kind = TFieldAccess { receiver = recv; field = "token" }; type = Prim { kind = PrimKind::U8 } }
    match validate_texpr(good) { null => assert true; error => assert false }
    // … a forged field access over a non-Named receiver is corruption (struct-receiver invariant).
    let badrecv = TExpr { kind = TVar { name = "n" }; type = Prim { kind = PrimKind::I32 } }
    let bad = TExpr { kind = TFieldAccess { receiver = badrecv; field = "token" }; type = Prim { kind = PrimKind::U8 } }
    match validate_texpr(bad) { null => assert false; error => assert true }
}

#test
fn mut_rule_assign() {
    // C4 (B.21): a write to a `mut` binding type-checks; a write to an immutable one errors.
    let menv = define(empty_env(), "m", u32t(), true)    // mut m: u32
    let ok = Assign { name = "m"; op = lexer::TokenKind::Assign; value = Var { name = "m" } }
    match type_assign(ok, menv, empty_table()) { TypedStmt => assert true; error => assert false }

    let lenv = define(empty_env(), "l", u32t(), false)   // let l: u32 (immutable)
    let bad = Assign { name = "l"; op = lexer::TokenKind::Assign; value = Var { name = "l" } }
    match type_assign(bad, lenv, empty_table()) { TypedStmt => assert false; error => assert true }
}

#test
fn annotated_literal() {
    // C6 (Side D): `let x: u8 = 1` — the binding adopts u8; the leaf stays i64 (stored as-is).
    let b = Binding { kind = BindKind::Let; target = SimpleName { name = "x" }; has_type = true; type_ann = NamedType { path = path1("u8") }; value = Number { value = 1 } }
    match type_binding(b, empty_env(), empty_table()) {
        TypedStmt as ts => match ts.node {
            TBinding as tb => {
                assert prim_is(tb.bound, PrimKind::U8)         // binding adopts u8
                assert prim_is(tb.value.type, PrimKind::I64)  // leaf stored AS-IS — i64 (Side D)
            }
            _ => assert false
        }
        error => assert false
    }
    // barrier: a constant out of range fails EARLY (M.1).
    let b2 = Binding { kind = BindKind::Let; target = SimpleName { name = "y" }; has_type = true; type_ann = NamedType { path = path1("u8") }; value = Number { value = 999 } }
    match type_binding(b2, empty_env(), empty_table()) { TypedStmt => assert false; error => assert true }
    // barrier: a NON-literal mismatch — no implicit conversion (M.2; use `to`).
    let env = define(empty_env(), "w", i32t(), false)
    let b3 = Binding { kind = BindKind::Let; target = SimpleName { name = "z" }; has_type = true; type_ann = NamedType { path = path1("u8") }; value = Var { name = "w" } }
    match type_binding(b3, env, empty_table()) { TypedStmt => assert false; error => assert true }
}

// build `fn f(x: u32) -> <ret> { body }` for the C5 return-checking tests.
fn fn_x_u32(ret: TypeExpr, body: []Statement) -> Function {
    let p = Param { name = "x"; type_ann = NamedType { path = path1("u32") } }
    let params = teko::list::push(teko::list::empty(), p)
    Function { name = "f"; params = params; has_return = true; return_type = ret; body = body; is_exp = false; has_doc = false; doc = "" }
}

#test
fn return_type_checks() {
    // happy: trailing expression matches the declared return type.
    let body1: []Statement = teko::list::push(teko::list::empty(), ExprStmt { expr = Var { name = "x" } })
    match type_function(fn_x_u32(NamedType { path = path1("u32") }, body1), empty_env(), empty_table()) { TFunction => assert true; error => assert false }

    // happy: an explicit `return x` matching (trailing is the return → no extra claim).
    let body2: []Statement = teko::list::push(teko::list::empty(), Return { has_value = true; value = Var { name = "x" } })
    match type_function(fn_x_u32(NamedType { path = path1("u32") }, body2), empty_env(), empty_table()) { TFunction => assert true; error => assert false }

    // happy: body ENDS in a diverging `loop` whose inner `return` matches — NOT false-rejected (M.4 guard).
    let loopbody: []Statement = teko::list::push(teko::list::empty(), Return { has_value = true; value = Var { name = "x" } })
    let body3: []Statement = teko::list::push(teko::list::empty(), LoopStmt { body = loopbody })
    match type_function(fn_x_u32(NamedType { path = path1("u32") }, body3), empty_env(), empty_table()) { TFunction => assert true; error => assert false }

    // happy: `return <u32>` into a `u32 | error` return — variant member inclusion (B.14).
    let umembers: []TypeExpr = teko::list::push(teko::list::push(teko::list::empty(), NamedType { path = path1("u32") }), NamedType { path = path1("error") })
    let body4: []Statement = teko::list::push(teko::list::empty(), Return { has_value = true; value = Var { name = "x" } })
    match type_function(fn_x_u32(UnionType { members = umembers }, body4), empty_env(), empty_table()) { TFunction => assert true; error => assert false }
}

#test
fn return_type_mismatch_errors() {
    // barrier: trailing expression's type ≠ declared return.
    let body1: []Statement = teko::list::push(teko::list::empty(), ExprStmt { expr = Var { name = "x" } })
    match type_function(fn_x_u32(NamedType { path = path1("u8") }, body1), empty_env(), empty_table()) { TFunction => assert false; error => assert true }
    // barrier: `return x` (u32) in a function declaring u8.
    let body2: []Statement = teko::list::push(teko::list::empty(), Return { has_value = true; value = Var { name = "x" } })
    match type_function(fn_x_u32(NamedType { path = path1("u8") }, body2), empty_env(), empty_table()) { TFunction => assert false; error => assert true }
}

// --- C7: pattern checking (Field/Range/Alt) + exhaustiveness helpers ---
// a variant Color = RED | GREEN | BLUE.
fn color_subject() -> Type {
    mut members: []Type = teko::list::empty()
    members = teko::list::push(members, Named { name = "RED" })
    members = teko::list::push(members, Named { name = "GREEN" })
    members = teko::list::push(members, Named { name = "BLUE" })
    Variant { members = members }
}
// register RED/GREEN/BLUE as (bodyless) user types so `resolve_named` finds them.
fn color_table() -> TypeTable {
    mut t: TypeTable = teko::list::empty()
    mut i = 0
    let names = teko::list::push(teko::list::push(teko::list::push(teko::list::empty(), "RED"), "GREEN"), "BLUE")
    loop {
        if i >= names.len { break }
        let td = TypeDecl { name = names[i]; body = StructBody { fields = teko::list::empty() }; is_exp = false; has_doc = false; doc = "" }
        t = teko::list::push(t, TypeReg { name = names[i]; decl = td })
        i++
    }
    t
}
// a bare (non-binding) case pattern `CASE`.
fn case_pat(name: str) -> Pattern { BindPattern { type_name = path1(name); has_binding = false; binding = "" } }
// an unguarded / guarded arm with the given pattern.
fn arm_of(p: Pattern) -> Arm { Arm { pattern = p; has_when = false; guard = Number { value = 0 }; body = Number { value = 0 } } }
fn guarded_arm_of(p: Pattern) -> Arm { Arm { pattern = p; has_when = true; guard = Number { value = 1 }; body = Number { value = 0 } } }

#test
fn field_pattern_binds_fields() {
    // happy: `Foo { token }` binds `token : u8` (B.21 immutable).
    let fp = FieldPattern { type_name = path1("Foo"); fields = teko::list::push(teko::list::empty(), "token") }
    let e2 = match check_pattern(fp, Named { name = "Foo" }, foo_env(), foo_table()) { Env as e => e; error => { assert false; return } }
    match lookup(e2, "token") { Type as t => assert prim_is(t, PrimKind::U8); error => assert false }
}

#test
fn field_pattern_barriers() {
    // barrier: an unknown field on a real struct → error (M.1).
    let badfield = FieldPattern { type_name = path1("Foo"); fields = teko::list::push(teko::list::empty(), "nope") }
    match check_pattern(badfield, Named { name = "Foo" }, foo_env(), foo_table()) { Env => assert false; error => assert true }
    // barrier: a field pattern over a non-struct (a primitive) → error.
    let prim = FieldPattern { type_name = path1("u8"); fields = teko::list::empty() }
    match check_pattern(prim, Named { name = "Foo" }, foo_env(), foo_table()) { Env => assert false; error => assert true }
}

#test
fn range_pattern_check() {
    // happy: `0 ..= 9` against i64 — both bounds i64, subject integer; binds nothing.
    let env = define(empty_env(), "n", i64t(), false)
    let rp = RangePattern { lo = Number { value = 0 }; hi = Number { value = 9 } }
    match check_pattern(rp, i64t(), env, empty_table()) { Env => assert true; error => assert false }
    // barrier: a range over bool → error (not an integer subject — M.2).
    let rpb = RangePattern { lo = Number { value = 0 }; hi = Number { value = 1 } }
    match check_pattern(rpb, boolt(), env, empty_table()) { Env => assert false; error => assert true }
    // barrier: a mistyped bound (a str literal) against i64 → error.
    let rps = RangePattern { lo = StrLit { text = "a" }; hi = Number { value = 9 } }
    match check_pattern(rps, i64t(), env, empty_table()) { Env => assert false; error => assert true }
}

#test
fn alt_pattern_check() {
    // happy: `RED | GREEN` against Color — each a bare case; binds nothing.
    mut opts: []Pattern = teko::list::push(teko::list::empty(), case_pat("RED"))
    opts = teko::list::push(opts, case_pat("GREEN"))
    let alt = AltPattern { options = opts }
    match check_pattern(alt, color_subject(), empty_env(), color_table()) { Env => assert true; error => assert false }
    // barrier: a BINDING option inside an Alt → error (settled axis rule, M.1+M.2).
    mut bopts: []Pattern = teko::list::push(teko::list::empty(), case_pat("RED"))
    bopts = teko::list::push(bopts, BindPattern { type_name = path1("GREEN"); has_binding = true; binding = "g" })
    let balt = AltPattern { options = bopts }
    match check_pattern(balt, color_subject(), empty_env(), color_table()) { Env => assert false; error => assert true }
}

#test
fn exhaustive_with_alt() {
    // happy: `RED | GREEN` + `BLUE` covers the whole variant (Alt expansion + B.14).
    mut opts: []Pattern = teko::list::push(teko::list::empty(), case_pat("RED"))
    opts = teko::list::push(opts, case_pat("GREEN"))
    mut arms: []Arm = teko::list::push(teko::list::empty(), arm_of(AltPattern { options = opts }))
    arms = teko::list::push(arms, arm_of(case_pat("BLUE")))
    assert exhaustive(arms, color_subject())
}

#test
fn exhaustive_barriers() {
    // barrier: missing BLUE → non-exhaustive.
    mut opts: []Pattern = teko::list::push(teko::list::empty(), case_pat("RED"))
    opts = teko::list::push(opts, case_pat("GREEN"))
    let arms1: []Arm = teko::list::push(teko::list::empty(), arm_of(AltPattern { options = opts }))
    assert !exhaustive(arms1, color_subject())
    // barrier: a `when`-guarded last case does NOT complete coverage (B.15 — `when` excluded).
    mut arms2: []Arm = teko::list::push(teko::list::empty(), arm_of(case_pat("RED")))
    arms2 = teko::list::push(arms2, arm_of(case_pat("GREEN")))
    arms2 = teko::list::push(arms2, guarded_arm_of(case_pat("BLUE")))
    assert !exhaustive(arms2, color_subject())
    // barrier: a `when`-guarded `_` does NOT make a match total.
    let arms3: []Arm = teko::list::push(teko::list::empty(), guarded_arm_of(WildcardPattern { }))
    assert !exhaustive(arms3, color_subject())
}
```

> **C mirror dos testes (harness `main`, alpha).** Os `.tkt` acima são canônicos; o
> harness C roda os mesmos casos sobre AST construída à mão — feliz: cada `tk_type_*`
> devolve um `tk_texpr`/`tk_tfunction`/`tk_tprogram` com `.type`/`.return_type`
> resolvido e filhos tipados (asserts sobre `.tag`, `.type.tag`/`.as.prim`,
> `.nargs`/`.nitems`); barreira: largura mista (sem promoção — B.22), nome indefinido,
> `if`-valor sem `else`, `match` não-exaustivo — todos `!.ok` (`error`). **Casts (C2, regra B):**
> feliz — TODO inteiro↔inteiro (incl. `i32→i8`, `i32→u32`, `u32→i8`) tipa para `TK_TEXPR_CAST`
> com `.type` = alvo (a perda é guarda de runtime, codegen), e constante que cabe (`200 to u8`);
> barreira — `bool→i32`/`i32→bool`, `str→i32`, e constante fora-de-faixa (`300 to i8`, `-1 to u8`,
> `5000000000 to i32`) → `error`; e `tk_validate_texpr` rejeita um `TK_TEXPR_CAST` forjado `str→i32`
> (re-derivação viva); `i32→i8` forjado é ACEITO (conversão definida).

> A árvore tipada é o que a **E6-2** re-valida (contra-validação) e o que a **E6-3**
> serializa no `.tkb` — desserializado, é **exatamente** ela (diferença ZERO).

---
## Etapa 6-2 — a contra-validação (decisão 7)

Depois de a E6-1 produzir a **árvore tipada**, a contra-validação a **percorre e
re-deriva** o tipo de cada nó *independentemente*, conferindo que bate com o tipo
guardado. Roda em **dois momentos**: antes do backend (pega um bug do checker — o
inválido é pego mesmo depois da checagem, M.1) e sobre um **`.tkb` desserializado**
(pega corrupção — um byte trocado produz um nó cujo tipo não fecha mais com os
filhos). Nós **sem ambiente** (operadores, literais) são re-derivados por inteiro;
os **com ambiente** (`var`, `call`) são checados estruturalmente (o tipo está
presente + os filhos são válidos).

> A nível de expressão abaixo. A nível de programa (`validate_program` →
> `validate_function` → … → `validate_texpr`) segue assim que os statements/itens
> tipados (a conclusão da E6-1) existirem.

### Teko — `src/checker/revalidate.tks`

```teko
// src/checker/revalidate.tks  (namespace 'teko::checker')

fn is_bool(t: Type) -> bool    { match t { Prim as p => p.kind == PrimKind::Bool; _ => false } }
fn is_integer(t: Type) -> bool { match t { Prim as p => p.kind != PrimKind::Bool; _ => false } }

// the stored type must equal the type derived independently here.
fn check_node_type(stored: Type, expected: Type) -> error? {     // fallible-no-value: null = ok, error = failure
    if type_eq(stored, expected) { return null }
    error { message = "corrupt typed tree: a node's type does not match its derivation" }
}

// re-derive a binary's result type from its operands (B.22), independently.
fn rederive_binary(lt: Type, rt: Type, op: lexer::TokenKind) -> Type | error {
    if op_is_shift(op) {
        if !is_integer(lt) || !is_integer(rt) { return error { message = "corrupt: shift operands not integer" } }
        return lt
    }
    if op_is_arith_bitwise(op) {
        if !is_integer(lt) { return error { message = "corrupt: arith/bitwise operand not integer" } }
        if !type_eq(lt, rt) { return error { message = "corrupt: binary operands differ (no promotion)" } }
        return lt
    }
    error { message = "corrupt: unknown binary operator" }
}

fn validate_each(xs: []TExpr) -> error? {                        // fallible-no-value: null = ok, error = failure
    mut i = 0
    loop {
        if i >= xs.len { break }
        match validate_texpr(xs[i]) { null => {}; error as e => return e }
        i++
    }
    null
}

// COUNTER-VALIDATE a typed expression: re-derive its type and confirm it matches
// the stored one (operators/literals), or check it structurally (env-dependent).
fn validate_texpr(te: TExpr) -> error? {                         // fallible-no-value: null = ok, error = failure
    match te.kind {
        TNumber  => check_node_type(te.type, Prim { kind = PrimKind::I64 })
        TStrLit  => check_node_type(te.type, Str { })
        TByteLit => check_node_type(te.type, Byte { })
        TVar     => null                           // env-dependent → trust (type is present)
        TCall as c => validate_each(c.args)        // [callee re-derivation is env-dependent]
        TBinary as b => {
            match validate_texpr(b.left)  { null => {}; error as e => return e }
            match validate_texpr(b.right) { null => {}; error as e => return e }
            let d = match rederive_binary(b.left.type, b.right.type, b.op) { Type as t => t; error as e => return e }
            check_node_type(te.type, d)
        }
        TUnary as u => {
            match validate_texpr(u.operand) { null => {}; error as e => return e }
            if u.op == lexer::TokenKind::Bang {
                if !is_bool(u.operand.type) { return error { message = "corrupt: `!` operand not bool" } }
                return check_node_type(te.type, Prim { kind = PrimKind::Bool })
            }
            if !is_integer(u.operand.type) { return error { message = "corrupt: `-`/`~` operand not integer" } }
            check_node_type(te.type, u.operand.type)
        }
        TCompare as cmp => {
            match validate_texpr(cmp.first) { null => {}; error as e => return e }
            mut i = 0
            loop {
                if i >= cmp.rest.len { break }
                match validate_texpr(cmp.rest[i].operand) { null => {}; error as e => return e }
                i++
            }
            check_node_type(te.type, Prim { kind = PrimKind::Bool })   // a comparison is bool
        }
        TIfExpr    => null    // [block-bearing: deep revalidation lands with program-level — E6-2]
        TMatchExpr => null    // [idem — typed blocks/arms validate once validate_statement exists]
        TCast as c => {
            match validate_texpr(c.expr) { null => {}; error as e => return e }
            match cast_check(c.expr.type, te.type) {           // RE-PROVE the cast independently (M.3 — not a rubber stamp)
                null  => null
                error => error { message = "corrupt: illegal cast in typed tree" }
            }
        }
        TFieldAccess as f => {
            match validate_texpr(f.receiver) { null => {}; error as e => return e }
            match f.receiver.type {                            // struct-receiver invariant (field type is table-dependent → trust, as TVar/TCall)
                Named => null
                _     => error { message = "corrupt: field access on a non-struct receiver" }
            }
        }
    }
}
```

### C23 — `src/checker/revalidate.h`

```c
// src/checker/revalidate.h — the counter-validation of the typed tree.
#ifndef TK_CHECK_REVALIDATE_H
#define TK_CHECK_REVALIDATE_H

#include "tast.h"
#include "scope.h"   // tk_type_result (we reuse the error machinery)

// `error?` (null = ok): ok if the subtree's stored types match their derivations.
tk_check_result tk_validate_texpr(const tk_texpr *te);

#endif // TK_CHECK_REVALIDATE_H
```

### C23 — `src/checker/revalidate.c`

```c
// src/checker/revalidate.c
#include "revalidate.h"
#include "check.h"   // tk_check_result

bool tk_cast_ok(tk_type from, tk_type to);   // from typer.c — re-derive cast legality (C2)

static tk_check_result cok(void)         { return (tk_check_result){ .ok = true }; }
static tk_check_result cfail(const char *m) { return (tk_check_result){ .ok = false, .error = tk_error_make(m) }; }
static tk_type         prim(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
static bool is_bool(tk_type t)    { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t) { return t.tag == TK_TYPE_PRIM && t.as.prim != TK_PRIM_BOOL; }
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}

static tk_check_result node_is(tk_type stored, tk_type expected) {
    return tk_type_eq(&stored, &expected) ? cok()
         : cfail("corrupt typed tree: a node's type does not match its derivation");
}

tk_check_result tk_validate_texpr(const tk_texpr *te) {
    switch (te->tag) {
        case TK_TEXPR_NUMBER: return node_is(te->type, prim(TK_PRIM_I64));
        case TK_TEXPR_STR:    return node_is(te->type, (tk_type){ .tag = TK_TYPE_STR });
        case TK_TEXPR_BYTE:   return node_is(te->type, (tk_type){ .tag = TK_TYPE_BYTE });
        case TK_TEXPR_VAR:    return cok();                          // env-dependent → trust
        case TK_TEXPR_CALL: {
            for (size_t i = 0; i < te->as.call.nargs; i += 1) {
                tk_check_result r = tk_validate_texpr(&te->as.call.args[i]);
                if (!r.ok) return r;
            }
            return cok();
        }
        case TK_TEXPR_BINARY: {
            tk_check_result l = tk_validate_texpr(te->as.binary.left);  if (!l.ok) return l;
            tk_check_result r = tk_validate_texpr(te->as.binary.right); if (!r.ok) return r;
            tk_type lt = te->as.binary.left->type, rt = te->as.binary.right->type;
            tk_token_kind op = te->as.binary.op;
            if (op_is_shift(op)) {
                if (!is_integer(lt) || !is_integer(rt)) return cfail("corrupt: shift operands not integer");
                return node_is(te->type, lt);
            }
            if (op_is_arith_bitwise(op)) {
                if (!is_integer(lt)) return cfail("corrupt: arith/bitwise operand not integer");
                if (!tk_type_eq(&lt, &rt)) return cfail("corrupt: binary operands differ (no promotion)");
                return node_is(te->type, lt);
            }
            return cfail("corrupt: unknown binary operator");
        }
        case TK_TEXPR_UNARY: {
            tk_check_result o = tk_validate_texpr(te->as.unary.operand); if (!o.ok) return o;
            tk_type ot = te->as.unary.operand->type;
            if (te->as.unary.op == TK_TOKEN_BANG) {
                if (!is_bool(ot)) return cfail("corrupt: `!` operand not bool");
                return node_is(te->type, prim(TK_PRIM_BOOL));
            }
            if (!is_integer(ot)) return cfail("corrupt: `-`/`~` operand not integer");
            return node_is(te->type, ot);
        }
        case TK_TEXPR_COMPARE: {
            tk_check_result f = tk_validate_texpr(te->as.compare.first); if (!f.ok) return f;
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) {
                tk_check_result r = tk_validate_texpr(te->as.compare.rest[i].operand);
                if (!r.ok) return r;
            }
            return node_is(te->type, prim(TK_PRIM_BOOL));
        }
        case TK_TEXPR_IF:
        case TK_TEXPR_MATCH:
            return cok();   // [block-bearing: deep revalidation with program-level — E6-2]
        case TK_TEXPR_CAST: {
            tk_check_result e = tk_validate_texpr(te->as.cast.expr); if (!e.ok) return e;
            if (!tk_cast_ok(te->as.cast.expr->type, te->type))      // RE-PROVE the cast (M.3)
                return cfail("corrupt: illegal cast in typed tree");
            return cok();
        }
        case TK_TEXPR_FIELD_ACCESS: {
            tk_check_result r = tk_validate_texpr(te->as.field_access.receiver); if (!r.ok) return r;
            if (te->as.field_access.receiver->type.tag != TK_TYPE_NAMED)   // struct-receiver invariant
                return cfail("corrupt: field access on a non-struct receiver");
            return cok();
        }
    }
    return cfail("corrupt: unknown typed expression");
}
```

---
## Etapa 6-3c-1 — `.tkb`: a infraestrutura de serialização

Layout do **`.tkb`** (little-endian):

```
[0..4)   magic     "TKB\0"
[4..8)   version   u32
[8..16)  hash      u64   (FNV-1a sobre o corpo, [16..fim))
---- corpo (entra no hash) ----
string table:  count u32, depois cada string: len u32 + bytes
typed program: os itens tipados (nó = tag + tipo + filhos; strings por índice u32)
```

O hash é recomputado na desserialização e comparado — pega **alteração manual**.
(Assinatura por chave é evolução: protege pacotes no **servidor de distribuição**,
não a compilação.) Strings são **deduplicadas** numa tabela e referenciadas por
índice — round-trip exato e compacto. Aqui a infra; `Type`/`TExpr` na E6-3c-2.

> As conversões inteiras/byte usam o operador real **`x to T`** (Fase X): inteiro↔inteiro
> por C2 (regra B) e **byte↔inteiro** tratando `byte` como `u8` (B.36). Ainda aberto: só
> `str(b)` (texto↔bytes), que depende da camada de codepage. No C são casts diretos.

### Teko — `src/emit/tkb_buf.tks`

```teko
// src/emit/tkb_buf.tks  (namespace 'teko::emit')

// extract the low 8 bits as a byte.
fn lo8(x: u32) -> byte { (x & 0xFF) to byte }

fn write_u8(buf: []byte, x: byte) -> []byte { teko::list::push(buf, x) }

fn write_u32(buf: []byte, x: u32) -> []byte {
    mut b = buf
    b = teko::list::push(b, lo8(x))
    b = teko::list::push(b, lo8(x >> 8))
    b = teko::list::push(b, lo8(x >> 16))
    b = teko::list::push(b, lo8(x >> 24))
    b
}

// an i64 as 8 LE bytes. codec i64s are non-negative (TNumber magnitudes), so the value conversion is exact.
fn write_i64(buf: []byte, x: i64) -> []byte {
    mut b = buf
    mut bits = x to u64                // codec i64s are non-negative (TNumber magnitudes), so the value conversion is exact
    mut k = 0
    loop {
        if k >= 8 { break }
        b = teko::list::push(b, lo8((bits & 0xFF) to u32))
        bits = bits >> 8
        k++
    }
    b
}

// a length-prefixed byte run (used by the string table).
fn write_bytes(buf: []byte, s: str) -> []byte {
    mut b = write_u32(buf, s.len to u32)
    mut i = 0
    loop {
        if i >= s.len { break }
        b = teko::list::push(b, s[i])
        i++
    }
    b
}

// --- the string table: dedup; nodes reference strings by u32 index ---

type StrTable = struct { strings: []str }
type Interned = struct { table: StrTable; index: u32 }

fn st_empty() -> StrTable { StrTable { strings = teko::list::empty() } }

fn st_find(t: StrTable, s: str) -> u32 {
    mut i = 0
    loop {
        if i >= t.strings.len { break }
        if t.strings[i] == s { return i to u32 }
        i++
    }
    0xFFFFFFFF                          // sentinel: not found
}

// intern s → its index, adding it if new (dedup = exact round-trip + compact).
fn st_intern(t: StrTable, s: str) -> Interned {
    let found = st_find(t, s)
    if found != 0xFFFFFFFF { return Interned { table = t; index = found } }
    let idx = t.strings.len to u32
    Interned { table = StrTable { strings = teko::list::push(t.strings, s) }; index = idx }
}

// serialize the whole string table into the buffer (count, then each string).
fn write_strtable(buf: []byte, t: StrTable) -> []byte {
    mut b = write_u32(buf, t.strings.len to u32)
    mut i = 0
    loop {
        if i >= t.strings.len { break }
        b = write_bytes(b, t.strings[i])
        i++
    }
    b
}

// --- FNV-1a (alteration detection; keyed signing is evolution) ---

fn fnv1a(data: []byte) -> u64 {
    mut h = 0xCBF29CE484222325         // offset basis
    mut i = 0
    loop {
        if i >= data.len { break }
        h = h ^ (data[i] to u64)       // data[i] is a byte; byte→u64 widening
        h = h * 0x100000001B3          // FNV prime
        i++
    }
    h
}
```

### C23 — `src/emit/tkb_buf.h` + `tkb_buf.c`

```c
// src/emit/tkb_buf.h — .tkb serialization primitives.
#ifndef TK_EMIT_TKB_BUF_H
#define TK_EMIT_TKB_BUF_H

#include "../core.h"
#include "../text/text.h"

TK_LIST(tk_byte, tk_bytes);        // the growable output buffer

tk_bytes tk_write_u8(tk_bytes b, tk_byte x);
tk_bytes tk_write_u32(tk_bytes b, uint32_t x);
tk_bytes tk_write_i64(tk_bytes b, int64_t x);
tk_bytes tk_write_bytes(tk_bytes b, tk_str s);

typedef struct { tk_str *ptr; size_t len; size_t cap; } tk_strtable;
tk_strtable tk_st_empty(void);
uint32_t    tk_st_find(tk_strtable t, tk_str s);          // 0xFFFFFFFF if absent
uint32_t    tk_st_intern(tk_strtable *t, tk_str s);       // mutates t; returns index
tk_bytes    tk_write_strtable(tk_bytes b, tk_strtable t);

uint64_t tk_fnv1a(const tk_byte *data, size_t n);

#endif // TK_EMIT_TKB_BUF_H
```

```c
// src/emit/tkb_buf.c
#include "tkb_buf.h"
#include <string.h>

tk_bytes tk_write_u8(tk_bytes b, tk_byte x) { return tk_bytes_push(b, x); }

tk_bytes tk_write_u32(tk_bytes b, uint32_t x) {
    b = tk_bytes_push(b, (tk_byte)(x      ));
    b = tk_bytes_push(b, (tk_byte)(x >>  8));
    b = tk_bytes_push(b, (tk_byte)(x >> 16));
    b = tk_bytes_push(b, (tk_byte)(x >> 24));
    return b;
}

tk_bytes tk_write_i64(tk_bytes b, int64_t x) {
    uint64_t bits = (uint64_t)x;                 // reinterpret two's-complement bits
    for (int k = 0; k < 8; k += 1) { b = tk_bytes_push(b, (tk_byte)(bits & 0xFF)); bits >>= 8; }
    return b;
}

tk_bytes tk_write_bytes(tk_bytes b, tk_str s) {
    b = tk_write_u32(b, (uint32_t)s.len);
    for (size_t i = 0; i < s.len; i += 1) b = tk_bytes_push(b, s.ptr[i]);
    return b;
}

tk_strtable tk_st_empty(void) { return (tk_strtable){ NULL, 0, 0 }; }

static bool str_eq(tk_str a, tk_str b) { return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0; }

uint32_t tk_st_find(tk_strtable t, tk_str s) {
    for (size_t i = 0; i < t.len; i += 1) if (str_eq(t.ptr[i], s)) return (uint32_t)i;
    return 0xFFFFFFFFu;
}

uint32_t tk_st_intern(tk_strtable *t, tk_str s) {
    uint32_t f = tk_st_find(*t, s);
    if (f != 0xFFFFFFFFu) return f;
    if (t->len == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 8;
        tk_str *np = realloc(t->ptr, nc * sizeof *np); if (!np) abort();
        t->ptr = np; t->cap = nc;
    }
    t->ptr[t->len] = s;
    return (uint32_t)(t->len++);
}

tk_bytes tk_write_strtable(tk_bytes b, tk_strtable t) {
    b = tk_write_u32(b, (uint32_t)t.len);
    for (size_t i = 0; i < t.len; i += 1) b = tk_write_bytes(b, t.ptr[i]);
    return b;
}

uint64_t tk_fnv1a(const tk_byte *data, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i += 1) { h ^= (uint64_t)data[i]; h *= 0x100000001B3ull; }
    return h;
}
```

---
## Etapa 6-3c-2 — `.tkb`: serializar `Type` e `TExpr`

Cada `Type` e cada `TExpr` viram bytes: um **tag** + o payload, recursivo; strings
por **índice u32** na tabela. Cada nó de expressão escreve **primeiro o seu tipo**,
depois o tag/payload — e os **doc-comments não entram** (o `.tkb` é o objeto). O
**frame** (coletar strings + header + hash) vem na E6-3c-3, com o deserialize.

> Os casts inteiros e o `enum→byte` (`prim_byte`/`kind_byte`, o ordinal do enum)
> são da **E7** — aqui usados como pendentes.

### Teko — `src/emit/tkb_write.tks`

```teko
// src/emit/tkb_write.tks  (namespace 'teko::emit')

// a Type → tag byte + payload. (prim_byte/kind_byte = the enum's ordinal — E7.)
fn write_type(buf: []byte, t: StrTable, ty: Type) -> []byte {
    match ty {
        Prim as p    => write_u8(write_u8(buf, 0), prim_byte(p.kind))
        Byte         => write_u8(buf, 1)
        Str          => write_u8(buf, 2)
        Error        => write_u8(buf, 3)                  // native `error`
        Void         => write_u8(buf, 4)                  // return-only marker (was Unit, excised)
        Slice as s   => write_type(write_u8(buf, 5), t, s.element)
        Named as n   => write_u32(write_u8(buf, 6), st_find(t, n.name))
        Variant as v => write_types(write_u8(buf, 7), t, v.members)
        Func as f    => write_type(write_types(write_u8(buf, 8), t, f.params), t, f.ret)
        Optional as o => write_type(write_u8(buf, 9), t, o.inner)   // T?
    }
}

fn write_types(buf: []byte, t: StrTable, xs: []Type) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_type(b, t, xs[i])
        i++
    }
    b
}

// a path (callee) → count + each segment's name index.
fn write_path(buf: []byte, t: StrTable, p: Path) -> []byte {
    mut b = write_u32(buf, p.segments.len to u32)
    mut i = 0
    loop {
        if i >= p.segments.len { break }
        b = write_u32(b, st_find(t, p.segments[i].name))
        i++
    }
    b
}

// a list of typed expressions (call args) → count + each.
fn write_texprs(buf: []byte, t: StrTable, xs: []TExpr) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_texpr(b, t, xs[i])
        i++
    }
    b
}

// the comparison terms → count + each (op byte + operand).
fn write_terms(buf: []byte, t: StrTable, ts: []TCmpTerm) -> []byte {
    mut b = write_u32(buf, ts.len to u32)
    mut i = 0
    loop {
        if i >= ts.len { break }
        b = write_texpr(write_u8(b, kind_byte(ts[i].op)), t, ts[i].operand)
        i++
    }
    b
}

// a TExpr → its type, then a tag + payload (children recursive).
fn write_texpr(buf: []byte, t: StrTable, te: TExpr) -> []byte {
    let b = write_type(buf, t, te.type)        // every node carries its type
    match te.kind {
        TNumber as n   => write_i64(write_u8(b, 0), n.value)
        TVar as v      => write_u32(write_u8(b, 1), st_find(t, v.name))
        TStrLit as s   => write_u32(write_u8(b, 2), st_find(t, s.text))
        TByteLit as bl => write_u8(write_u8(b, 3), bl.value)
        TBinary as bn  => write_texpr(write_texpr(write_u8(write_u8(b, 4), kind_byte(bn.op)), t, bn.left), t, bn.right)
        TUnary as u    => write_texpr(write_u8(write_u8(b, 5), kind_byte(u.op)), t, u.operand)
        TCompare as c  => write_terms(write_texpr(write_u8(b, 6), t, c.first), t, c.rest)
        TCall as cl    => write_texprs(write_path(write_u8(b, 7), t, cl.callee), t, cl.args)
        // typed if/match: reserved tags 8/9. Full (de)serialization needs a typed-STATEMENT
        // serializer ([]TStatement) — a separate later item; read rejects tags 8/9 as
        // "bad texpr tag" (visible — M.1). (MethodCall has no typed node — typing deferred.)
        TIfExpr        => write_u8(b, 8)
        TMatchExpr     => write_u8(b, 9)
        // S1a — Cast: the target type IS te.type (already written by the leading write_type); payload is just the inner expr.
        TCast as c     => write_texpr(write_u8(b, 10), t, c.expr)
        // S1b — FieldAccess: receiver THEN the interned field-name index (write order = read order).
        TFieldAccess as f => write_u32(write_texpr(write_u8(b, 11), t, f.receiver), st_find(t, f.field))
    }
}
```

### C23 — `src/emit/tkb_write.h` + `tkb_write.c`

```c
// src/emit/tkb_write.h
#ifndef TK_EMIT_TKB_WRITE_H
#define TK_EMIT_TKB_WRITE_H
#include "tkb_buf.h"
#include "../checker/tast.h"
tk_bytes tk_write_type(tk_bytes b, tk_strtable t, tk_type ty);
tk_bytes tk_write_texpr(tk_bytes b, tk_strtable t, const tk_texpr *te);
#endif // TK_EMIT_TKB_WRITE_H
```

```c
// src/emit/tkb_write.c
#include "tkb_write.h"

// prim_byte / kind_byte: the enum's ordinal byte — E7 (the enum→int cast).
static tk_byte prim_byte(tk_prim_kind k) { return (tk_byte)k; }   // [E7]
static tk_byte kind_byte(tk_token_kind k) { return (tk_byte)k; }  // [E7]

static tk_bytes write_types(tk_bytes b, tk_strtable t, const tk_type *xs, size_t n) {
    b = tk_write_u32(b, (uint32_t)n);
    for (size_t i = 0; i < n; i += 1) b = tk_write_type(b, t, xs[i]);
    return b;
}

tk_bytes tk_write_type(tk_bytes b, tk_strtable t, tk_type ty) {
    switch (ty.tag) {
        case TK_TYPE_PRIM:    return tk_write_u8(tk_write_u8(b, 0), prim_byte(ty.as.prim));
        case TK_TYPE_BYTE:    return tk_write_u8(b, 1);
        case TK_TYPE_STR:     return tk_write_u8(b, 2);
        case TK_TYPE_ERROR:   return tk_write_u8(b, 3);   // native `error`
        case TK_TYPE_VOID:    return tk_write_u8(b, 4);   // return-only marker (was UNIT, excised)
        case TK_TYPE_SLICE:   return tk_write_type(tk_write_u8(b, 5), t, *ty.as.slice.element);
        case TK_TYPE_NAMED:   return tk_write_u32(tk_write_u8(b, 6), tk_st_find(t, ty.as.named.name));
        case TK_TYPE_VARIANT: return write_types(tk_write_u8(b, 7), t, ty.as.variant.members, ty.as.variant.len);
        case TK_TYPE_FUNC:
            b = write_types(tk_write_u8(b, 8), t, ty.as.func.params, ty.as.func.nparams);
            return tk_write_type(b, t, *ty.as.func.ret);
        case TK_TYPE_OPTIONAL: return tk_write_type(tk_write_u8(b, 9), t, *ty.as.optional.inner);   // T?
    }
    return b;
}

static tk_bytes write_path(tk_bytes b, tk_strtable t, tk_path p) {
    b = tk_write_u32(b, (uint32_t)p.len);
    for (size_t i = 0; i < p.len; i += 1) b = tk_write_u32(b, tk_st_find(t, p.segments[i].name));
    return b;
}

tk_bytes tk_write_texpr(tk_bytes b, tk_strtable t, const tk_texpr *te) {
    b = tk_write_type(b, t, te->type);                   // every node carries its type
    switch (te->tag) {
        case TK_TEXPR_NUMBER: return tk_write_i64(tk_write_u8(b, 0), te->as.number.value);
        case TK_TEXPR_VAR:    return tk_write_u32(tk_write_u8(b, 1), tk_st_find(t, te->as.var.name));
        case TK_TEXPR_STR:    return tk_write_u32(tk_write_u8(b, 2), tk_st_find(t, te->as.str.text));
        case TK_TEXPR_BYTE:   return tk_write_u8(tk_write_u8(b, 3), te->as.byte.value);
        case TK_TEXPR_BINARY:
            b = tk_write_u8(tk_write_u8(b, 4), kind_byte(te->as.binary.op));
            b = tk_write_texpr(b, t, te->as.binary.left);
            return tk_write_texpr(b, t, te->as.binary.right);
        case TK_TEXPR_UNARY:
            b = tk_write_u8(tk_write_u8(b, 5), kind_byte(te->as.unary.op));
            return tk_write_texpr(b, t, te->as.unary.operand);
        case TK_TEXPR_COMPARE: {
            b = tk_write_texpr(tk_write_u8(b, 6), t, te->as.compare.first);
            b = tk_write_u32(b, (uint32_t)te->as.compare.nrest);
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) {
                b = tk_write_u8(b, kind_byte(te->as.compare.rest[i].op));
                b = tk_write_texpr(b, t, te->as.compare.rest[i].operand);
            }
            return b;
        }
        case TK_TEXPR_CALL: {
            b = write_path(tk_write_u8(b, 7), t, te->as.call.callee);
            b = tk_write_u32(b, (uint32_t)te->as.call.nargs);
            for (size_t i = 0; i < te->as.call.nargs; i += 1) b = tk_write_texpr(b, t, &te->as.call.args[i]);
            return b;
        }
        case TK_TEXPR_IF:    return tk_write_u8(b, 8);   // reserved — typed if/match needs a stmt serializer (later); read rejects (M.1)
        case TK_TEXPR_MATCH: return tk_write_u8(b, 9);   // reserved — idem (MethodCall has no typed node)
        case TK_TEXPR_CAST:                                                      // S1a — payload = inner expr (target rides te->type)
            return tk_write_texpr(tk_write_u8(b, 10), t, te->as.cast.expr);
        case TK_TEXPR_FIELD_ACCESS:                                             // S1b — receiver THEN field index
            b = tk_write_texpr(tk_write_u8(b, 11), t, te->as.field_access.receiver);
            return tk_write_u32(b, tk_st_find(t, te->as.field_access.field));
    }
    return b;
}
```

---
## Etapa 6-3c-3 (1/2) — `.tkb`: o frame de serialização

`serialize` monta o arquivo: **coleta** todas as strings (pré-passe, espelha os
helpers de escrita) → escreve a **tabela** → escreve o **corpo** (a árvore tipada,
strings por índice) → calcula o **hash** (FNV-1a do corpo) → emite **header**
(`magic` + versão + hash) + corpo. Com isto o `.tkb` é gravável de ponta a ponta; a
**leitura** (deserialize + verificação do hash) é a 2/2.

### Teko — `src/emit/tkb_frame.tks`

```teko
// src/emit/tkb_frame.tks  (namespace 'teko::emit')

fn write_u64(buf: []byte, x: u64) -> []byte {
    mut b = buf
    mut v = x
    mut k = 0
    loop {
        if k >= 8 { break }
        b = teko::list::push(b, lo8((v & 0xFF) to u32))
        v = v >> 8
        k++
    }
    b
}

fn append_bytes(dst: []byte, src: []byte) -> []byte {
    mut b = dst
    mut i = 0
    loop {
        if i >= src.len { break }
        b = teko::list::push(b, src[i])
        i++
    }
    b
}

// --- collect every string into the table (pre-pass; mirrors the write helpers) ---

fn collect_type_list(t: StrTable, xs: []Type) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = collect_type_strings(tab, xs[i])
        i++
    }
    tab
}

fn collect_type_strings(t: StrTable, ty: Type) -> StrTable {
    match ty {
        Named as n   => st_intern(t, n.name).table
        Slice as s   => collect_type_strings(t, s.element)
        Variant as v => collect_type_list(t, v.members)
        Func as f    => collect_type_strings(collect_type_list(t, f.params), f.ret)
        _            => t
    }
}

fn collect_path(t: StrTable, p: Path) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= p.segments.len { break }
        tab = st_intern(tab, p.segments[i].name).table
        i++
    }
    tab
}

fn collect_texprs(t: StrTable, xs: []TExpr) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = collect_strings(tab, xs[i])
        i++
    }
    tab
}

fn collect_terms(t: StrTable, ts: []TCmpTerm) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= ts.len { break }
        tab = collect_strings(tab, ts[i].operand)
        i++
    }
    tab
}

fn collect_strings(t: StrTable, te: TExpr) -> StrTable {
    let t1 = collect_type_strings(t, te.type)        // the node's type may name things
    match te.kind {
        TVar as v     => st_intern(t1, v.name).table
        TStrLit as s  => st_intern(t1, s.text).table
        TBinary as b  => collect_strings(collect_strings(t1, b.left), b.right)
        TUnary as u   => collect_strings(t1, u.operand)
        TCompare as c => collect_terms(collect_strings(t1, c.first), c.rest)
        TCall as cl   => collect_texprs(collect_path(t1, cl.callee), cl.args)
        TCast as c    => collect_strings(t1, c.expr)                                    // S1a — recurse into the inner expr
        TFieldAccess as f => st_intern(collect_strings(t1, f.receiver), f.field).table  // S1b — receiver AND the field name (must intern)
        _             => t1
    }
}

// --- the frame: header (magic + version + hash) + the body ---

fn serialize(te: TExpr) -> []byte {
    let table = collect_strings(st_empty(), te)
    let body = write_texpr(write_strtable(teko::list::empty(), table), table, te)
    let h = fnv1a(body)
    mut out = teko::list::empty()
    out = write_u8(out, b'T')
    out = write_u8(out, b'K')
    out = write_u8(out, b'B')
    out = write_u8(out, 0)
    out = write_u32(out, 1)                          // version 1
    out = write_u64(out, h)                          // FNV-1a of the body
    append_bytes(out, body)
}
```

### C23 — `src/emit/tkb_frame.h` + `tkb_frame.c`

```c
// src/emit/tkb_frame.h
#ifndef TK_EMIT_TKB_FRAME_H
#define TK_EMIT_TKB_FRAME_H
#include "tkb_write.h"
tk_bytes tk_write_u64(tk_bytes b, uint64_t x);
tk_bytes tk_serialize(const tk_texpr *te);
#endif // TK_EMIT_TKB_FRAME_H
```

```c
// src/emit/tkb_frame.c
#include "tkb_frame.h"

tk_bytes tk_write_u64(tk_bytes b, uint64_t x) {
    for (int k = 0; k < 8; k += 1) { b = tk_bytes_push(b, (tk_byte)(x & 0xFF)); x >>= 8; }
    return b;
}

static tk_bytes append_bytes(tk_bytes dst, tk_bytes src) {
    for (size_t i = 0; i < src.len; i += 1) dst = tk_bytes_push(dst, src.ptr[i]);
    return dst;
}

// --- collect every string (mutates the table via intern) ---

static void collect_type(tk_strtable *t, tk_type ty);
static void collect_type_list(tk_strtable *t, const tk_type *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) collect_type(t, xs[i]);
}
static void collect_type(tk_strtable *t, tk_type ty) {
    switch (ty.tag) {
        case TK_TYPE_NAMED:   tk_st_intern(t, ty.as.named.name); break;
        case TK_TYPE_SLICE:   collect_type(t, *ty.as.slice.element); break;
        case TK_TYPE_OPTIONAL: collect_type(t, *ty.as.optional.inner); break;
        case TK_TYPE_VARIANT: collect_type_list(t, ty.as.variant.members, ty.as.variant.len); break;
        case TK_TYPE_FUNC:    collect_type_list(t, ty.as.func.params, ty.as.func.nparams);
                              collect_type(t, *ty.as.func.ret); break;
        default: break;
    }
}
static void collect(tk_strtable *t, const tk_texpr *te) {
    collect_type(t, te->type);
    switch (te->tag) {
        case TK_TEXPR_VAR: tk_st_intern(t, te->as.var.name); break;
        case TK_TEXPR_STR: tk_st_intern(t, te->as.str.text); break;
        case TK_TEXPR_BINARY: collect(t, te->as.binary.left); collect(t, te->as.binary.right); break;
        case TK_TEXPR_UNARY:  collect(t, te->as.unary.operand); break;
        case TK_TEXPR_COMPARE:
            collect(t, te->as.compare.first);
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) collect(t, te->as.compare.rest[i].operand);
            break;
        case TK_TEXPR_CALL:
            for (size_t i = 0; i < te->as.call.callee.len; i += 1) tk_st_intern(t, te->as.call.callee.segments[i].name);
            for (size_t i = 0; i < te->as.call.nargs; i += 1) collect(t, &te->as.call.args[i]);
            break;
        case TK_TEXPR_CAST: collect(t, te->as.cast.expr); break;                 // S1a
        case TK_TEXPR_FIELD_ACCESS:                                              // S1b
            collect(t, te->as.field_access.receiver);
            tk_st_intern(t, te->as.field_access.field);                          // CRITICAL: intern the field name
            break;
        default: break;
    }
}

tk_bytes tk_serialize(const tk_texpr *te) {
    tk_strtable table = tk_st_empty();
    collect(&table, te);
    tk_bytes body = tk_write_texpr(tk_write_strtable((tk_bytes){0}, table), table, te);
    uint64_t h = tk_fnv1a(body.ptr, body.len);
    tk_bytes out = {0};
    out = tk_write_u8(out, (tk_byte)'T'); out = tk_write_u8(out, (tk_byte)'K');
    out = tk_write_u8(out, (tk_byte)'B'); out = tk_write_u8(out, 0);
    out = tk_write_u32(out, 1);                  // version 1
    out = tk_write_u64(out, h);                  // FNV-1a of the body
    return append_bytes(out, body);
}
```

---
## Etapa 6-3c-3 (2/2-A) — `.tkb`: o `Reader` e a leitura de `Type`

A leitura é o inverso da escrita. No modelo ref-less o `Reader` é **encadeado**: cada
leitor devolve `{ r: Reader; value }` (o leitor avançado + o valor). `read_type` espelha
`write_type` (tags 0–8). `read_texpr` + o `deserialize` (com a verificação do hash) são
a parte **B**. No C o `Reader` é mutável com um flag `ok` — mais curto.

### Teko — `src/emit/tkb_read.tks`

```teko
// src/emit/tkb_read.tks  (namespace 'teko::emit')

type Reader  = struct { data: []byte; pos: u64 }
type RByte   = struct { r: Reader; value: byte }
type RU32    = struct { r: Reader; value: u32 }
type RU64    = struct { r: Reader; value: u64 }
type RStr    = struct { r: Reader; value: str }
type RType   = struct { r: Reader; value: Type }
type RTypes  = struct { r: Reader; value: []Type }
type RTable  = struct { r: Reader; value: []str }

fn read_u8(r: Reader) -> RByte | error {
    if r.pos >= r.data.len { return error { message = "truncated .tkb" } }
    RByte { r = Reader { data = r.data; pos = r.pos + 1 }; value = r.data[r.pos] }
}

fn read_u32(r: Reader) -> RU32 | error {
    let a = match read_u8(r)   { RByte as x => x; error as e => return e }
    let b = match read_u8(a.r) { RByte as x => x; error as e => return e }
    let c = match read_u8(b.r) { RByte as x => x; error as e => return e }
    let d = match read_u8(c.r) { RByte as x => x; error as e => return e }
    RU32 { r = d.r; value = (a.value to u32) | ((b.value to u32) << 8) | ((c.value to u32) << 16) | ((d.value to u32) << 24) }
}

fn read_u64(r: Reader) -> RU64 | error {
    let lo = match read_u32(r)    { RU32 as x => x; error as e => return e }
    let hi = match read_u32(lo.r) { RU32 as x => x; error as e => return e }
    RU64 { r = hi.r; value = (lo.value to u64) | ((hi.value to u64) << 32) }
}

// a string reference = a u32 index into the (already-read) table.
fn read_str(r: Reader, table: []str) -> RStr | error {
    let idx = match read_u32(r) { RU32 as x => x; error as e => return e }
    if (idx.value to u64) >= table.len { return error { message = "bad string index in .tkb" } }
    RStr { r = idx.r; value = table[idx.value] }
}

// the string table: count, then each (len + bytes).
fn read_strtable(r: Reader) -> RTable | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut table = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let len = match read_u32(rr) { RU32 as x => x; error as e => return e }
        mut bytes = teko::list::empty()
        mut br = len.r
        mut j = 0
        loop {
            if j >= (len.value to u64) { break }
            let by = match read_u8(br) { RByte as x => x; error as e => return e }
            bytes = teko::list::push(bytes, by.value)
            br = by.r
            j++
        }
        table = teko::list::push(table, str(bytes))      // []byte → str [wrap form TBD]
        rr = br
        i++
    }
    RTable { r = rr; value = table }
}

// inverse of prim_byte (PrimKind ↔ byte).
fn prim_of(b: byte) -> PrimKind {
    if b == 0 { return PrimKind::U8 }
    if b == 1 { return PrimKind::U16 }
    if b == 2 { return PrimKind::U32 }
    if b == 3 { return PrimKind::U64 }
    if b == 4 { return PrimKind::I8 }
    if b == 5 { return PrimKind::I16 }
    if b == 6 { return PrimKind::I32 }
    if b == 7 { return PrimKind::I64 }
    PrimKind::Bool
}

fn read_types(r: Reader, table: []str) -> RTypes | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let t = match read_type(rr, table) { RType as x => x; error as e => return e }
        xs = teko::list::push(xs, t.value)
        rr = t.r
        i++
    }
    RTypes { r = rr; value = xs }
}

// inverse of write_type — tags 0=Prim 1=Byte 2=Str 3=error 4=void 5=Slice 6=Named 7=Variant 8=Func 9=Optional.
fn read_type(r: Reader, table: []str) -> RType | error {
    let tag = match read_u8(r) { RByte as x => x; error as e => return e }
    if tag.value == 0 {
        let k = match read_u8(tag.r) { RByte as x => x; error as e => return e }
        return RType { r = k.r; value = Prim { kind = prim_of(k.value) } }
    }
    if tag.value == 1 { return RType { r = tag.r; value = Byte { } } }
    if tag.value == 2 { return RType { r = tag.r; value = Str { } } }
    if tag.value == 3 { return RType { r = tag.r; value = Error { } } }
    if tag.value == 4 { return RType { r = tag.r; value = Void { } } }
    if tag.value == 5 {
        let el = match read_type(tag.r, table) { RType as x => x; error as e => return e }
        return RType { r = el.r; value = Slice { element = el.value } }
    }
    if tag.value == 6 {
        let nm = match read_str(tag.r, table) { RStr as x => x; error as e => return e }
        return RType { r = nm.r; value = Named { name = nm.value } }
    }
    if tag.value == 7 {
        let ms = match read_types(tag.r, table) { RTypes as x => x; error as e => return e }
        return RType { r = ms.r; value = Variant { members = ms.value } }
    }
    if tag.value == 8 {
        let ps = match read_types(tag.r, table) { RTypes as x => x; error as e => return e }
        let rt = match read_type(ps.r, table) { RType as x => x; error as e => return e }
        return RType { r = rt.r; value = Func { params = ps.value; ret = rt.value } }
    }
    if tag.value == 9 {
        let inr = match read_type(tag.r, table) { RType as x => x; error as e => return e }
        return RType { r = inr.r; value = Optional { inner = inr.value } }
    }
    error { message = "bad type tag in .tkb" }
}
```

### C23 — `src/emit/tkb_read.h` + `tkb_read.c` (parcial — `Reader` + `read_type`)

```c
// src/emit/tkb_read.h
#ifndef TK_EMIT_TKB_READ_H
#define TK_EMIT_TKB_READ_H
#include "tast.h"

// a mutable cursor with an `ok` flag — a read past the end sets ok=false.
typedef struct { const tk_byte *data; size_t len; size_t pos; bool ok; } tk_reader;
typedef struct { tk_str *ptr; size_t len; } tk_strs;   // the read string table

uint8_t  tk_read_u8(tk_reader *r);
uint32_t tk_read_u32(tk_reader *r);
uint64_t tk_read_u64(tk_reader *r);
tk_str   tk_read_str(tk_reader *r, tk_strs t);
tk_strs  tk_read_strtable(tk_reader *r);
tk_type  tk_read_type(tk_reader *r, tk_strs t);

#endif // TK_EMIT_TKB_READ_H
```

```c
// src/emit/tkb_read.c
#include "tkb_read.h"
#include <stdlib.h>

uint8_t tk_read_u8(tk_reader *r) {
    if (r->pos >= r->len) { r->ok = false; return 0; }
    return r->data[r->pos++];
}
uint32_t tk_read_u32(tk_reader *r) {
    uint32_t a = tk_read_u8(r), b = tk_read_u8(r), c = tk_read_u8(r), d = tk_read_u8(r);
    return a | (b << 8) | (c << 16) | (d << 24);
}
uint64_t tk_read_u64(tk_reader *r) {
    uint64_t lo = tk_read_u32(r), hi = tk_read_u32(r);
    return lo | (hi << 32);
}
tk_str tk_read_str(tk_reader *r, tk_strs t) {
    uint32_t i = tk_read_u32(r);
    if (i >= t.len) { r->ok = false; return (tk_str){ NULL, 0 }; }
    return t.ptr[i];
}
tk_strs tk_read_strtable(tk_reader *r) {
    uint32_t n = tk_read_u32(r);
    tk_str *xs = malloc(n * sizeof *xs); if (n && !xs) abort();
    for (uint32_t i = 0; i < n; i += 1) {
        uint32_t len = tk_read_u32(r);
        tk_byte *bytes = malloc(len ? len : 1); if (!bytes) abort();
        for (uint32_t j = 0; j < len; j += 1) bytes[j] = tk_read_u8(r);
        xs[i] = (tk_str){ bytes, len };
    }
    return (tk_strs){ xs, n };
}
static tk_prim_kind prim_of(uint8_t b) {
    switch (b) { case 0: return TK_PRIM_U8; case 1: return TK_PRIM_U16; case 2: return TK_PRIM_U32;
        case 3: return TK_PRIM_U64; case 4: return TK_PRIM_I8; case 5: return TK_PRIM_I16;
        case 6: return TK_PRIM_I32; case 7: return TK_PRIM_I64; default: return TK_PRIM_BOOL; }
}
static tk_type *box(tk_type t) { tk_type *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }

tk_type tk_read_type(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    switch (tag) {
        case 0: return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = prim_of(tk_read_u8(r)) };
        case 1: return (tk_type){ .tag = TK_TYPE_BYTE };
        case 2: return (tk_type){ .tag = TK_TYPE_STR };
        case 3: return (tk_type){ .tag = TK_TYPE_ERROR };
        case 4: return (tk_type){ .tag = TK_TYPE_VOID };
        case 5: return (tk_type){ .tag = TK_TYPE_SLICE, .as.slice.element = box(tk_read_type(r, t)) };
        case 6: return (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = tk_read_str(r, t) };
        case 7: {
            uint32_t n = tk_read_u32(r); tk_type *m = malloc(n * sizeof *m); if (n && !m) abort();
            for (uint32_t i = 0; i < n; i += 1) m[i] = tk_read_type(r, t);
            return (tk_type){ .tag = TK_TYPE_VARIANT, .as.variant = { m, n } };
        }
        case 8: {
            uint32_t n = tk_read_u32(r); tk_type *p = malloc(n * sizeof *p); if (n && !p) abort();
            for (uint32_t i = 0; i < n; i += 1) p[i] = tk_read_type(r, t);
            tk_type ret = tk_read_type(r, t);
            return (tk_type){ .tag = TK_TYPE_FUNC, .as.func = { p, n, box(ret) } };
        }
        case 9: return (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional.inner = box(tk_read_type(r, t)) };
    }
    r->ok = false; return (tk_type){ .tag = TK_TYPE_VOID };
}
```

---
## Etapa 6-3c-3 (2/2-B) — `.tkb`: `read_texpr` e o `deserialize`

`read_texpr` lê **o tipo, o tag, e os filhos** (inverso de `write_texpr`).
`deserialize` confere `magic`/versão, **recomputa o FNV-1a do corpo e compara** com o
hash guardado (diverge → alterado/corrompido), lê a tabela e a árvore. Round-trip
fechado, com a diferença ZERO garantida pela contra-validação (E6-2) sobre a árvore lida.

### Teko — `src/emit/tkb_read.tks` (continuação)

```teko
type RI64    = struct { r: Reader; value: i64 }
type RTExpr  = struct { r: Reader; value: TExpr }
type RTExprs = struct { r: Reader; value: []TExpr }
type RTerms  = struct { r: Reader; value: []TCmpTerm }
type RPath   = struct { r: Reader; value: Path }

fn read_i64(r: Reader) -> RI64 | error {
    let u = match read_u64(r) { RU64 as x => x; error as e => return e }
    RI64 { r = u.r; value = u.value to i64 }               // codec i64s are non-negative (TNumber magnitudes), so the value conversion is exact
}

// inverse of kind_byte (operator TokenKind ↔ byte). The byte→enum ordinal is E7's cast.
fn kind_of(b: byte) -> lexer::TokenKind { /* E7: the byte → operator-TokenKind */ }

fn read_path(r: Reader, table: []str) -> RPath | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut segs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let nm = match read_str(rr, table) { RStr as x => x; error as e => return e }
        segs = teko::list::push(segs, Segment { name = nm.value })
        rr = nm.r
        i++
    }
    RPath { r = rr; value = Path { segments = segs } }
}

fn read_texprs(r: Reader, table: []str) -> RTExprs | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let e = match read_texpr(rr, table) { RTExpr as x => x; error as e => return e }
        xs = teko::list::push(xs, e.value)
        rr = e.r
        i++
    }
    RTExprs { r = rr; value = xs }
}

fn read_terms(r: Reader, table: []str) -> RTerms | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut ts = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let op = match read_u8(rr) { RByte as x => x; error as e => return e }
        let o = match read_texpr(op.r, table) { RTExpr as x => x; error as e => return e }
        ts = teko::list::push(ts, TCmpTerm { op = kind_of(op.value); operand = o.value })
        rr = o.r
        i++
    }
    RTerms { r = rr; value = ts }
}

// inverse of write_texpr: the type FIRST, then tag (0..7), then the payload.
fn read_texpr(r: Reader, table: []str) -> RTExpr | error {
    let ty = match read_type(r, table) { RType as x => x; error as e => return e }
    let tag = match read_u8(ty.r) { RByte as x => x; error as e => return e }
    if tag.value == 0 {
        let v = match read_i64(tag.r) { RI64 as x => x; error as e => return e }
        return RTExpr { r = v.r; value = TExpr { kind = TNumber { value = v.value }; type = ty.value } }
    }
    if tag.value == 1 {
        let nm = match read_str(tag.r, table) { RStr as x => x; error as e => return e }
        return RTExpr { r = nm.r; value = TExpr { kind = TVar { name = nm.value }; type = ty.value } }
    }
    if tag.value == 2 {
        let s = match read_str(tag.r, table) { RStr as x => x; error as e => return e }
        return RTExpr { r = s.r; value = TExpr { kind = TStrLit { text = s.value }; type = ty.value } }
    }
    if tag.value == 3 {
        let bl = match read_u8(tag.r) { RByte as x => x; error as e => return e }
        return RTExpr { r = bl.r; value = TExpr { kind = TByteLit { value = bl.value }; type = ty.value } }
    }
    if tag.value == 4 {
        let op = match read_u8(tag.r) { RByte as x => x; error as e => return e }
        let l  = match read_texpr(op.r, table) { RTExpr as x => x; error as e => return e }
        let rt = match read_texpr(l.r, table)  { RTExpr as x => x; error as e => return e }
        return RTExpr { r = rt.r; value = TExpr { kind = TBinary { op = kind_of(op.value); left = l.value; right = rt.value }; type = ty.value } }
    }
    if tag.value == 5 {
        let op = match read_u8(tag.r) { RByte as x => x; error as e => return e }
        let o  = match read_texpr(op.r, table) { RTExpr as x => x; error as e => return e }
        return RTExpr { r = o.r; value = TExpr { kind = TUnary { op = kind_of(op.value); operand = o.value }; type = ty.value } }
    }
    if tag.value == 6 {
        let f  = match read_texpr(tag.r, table) { RTExpr as x => x; error as e => return e }
        let ts = match read_terms(f.r, table)   { RTerms as x => x; error as e => return e }
        return RTExpr { r = ts.r; value = TExpr { kind = TCompare { first = f.value; rest = ts.value }; type = ty.value } }
    }
    if tag.value == 7 {
        let cal = match read_path(tag.r, table)   { RPath as x => x; error as e => return e }
        let ar  = match read_texprs(cal.r, table) { RTExprs as x => x; error as e => return e }
        return RTExpr { r = ar.r; value = TExpr { kind = TCall { callee = cal.value; args = ar.value }; type = ty.value } }
    }
    if tag.value == 10 {
        // S1a — Cast: target type is ty.value (already read); read just the inner expr.
        let inner = match read_texpr(tag.r, table) { RTExpr as x => x; error as e => return e }
        return RTExpr { r = inner.r; value = TExpr { kind = TCast { expr = inner.value }; type = ty.value } }
    }
    if tag.value == 11 {
        // S1b — FieldAccess: receiver THEN field (inverse of write order).
        let rc  = match read_texpr(tag.r, table) { RTExpr as x => x; error as e => return e }
        let fld = match read_str(rc.r, table)    { RStr as x => x; error as e => return e }
        return RTExpr { r = fld.r; value = TExpr { kind = TFieldAccess { receiver = rc.value; field = fld.value }; type = ty.value } }
    }
    error { message = "bad texpr tag in .tkb" }
}

// THE ENTRY: verify the header + hash, then read the table and the typed tree.
fn deserialize(data: []byte) -> TExpr | error {
    let r0 = Reader { data = data; pos = 0 }
    let m0 = match read_u8(r0)   { RByte as x => x; error as e => return e }
    let m1 = match read_u8(m0.r) { RByte as x => x; error as e => return e }
    let m2 = match read_u8(m1.r) { RByte as x => x; error as e => return e }
    let m3 = match read_u8(m2.r) { RByte as x => x; error as e => return e }
    if m0.value != b'T' || m1.value != b'K' || m2.value != b'B' || m3.value != 0 {
        return error { message = "not a .tkb (bad magic)" }
    }
    let ver = match read_u32(m3.r) { RU32 as x => x; error as e => return e }
    if ver.value != 1 { return error { message = "unsupported .tkb version" } }
    let stored = match read_u64(ver.r) { RU64 as x => x; error as e => return e }
    if data.len < 16 { return error { message = "truncated .tkb header" } }
    if fnv1a(slice(data, 16, data.len)) != stored.value {
        return error { message = ".tkb altered or corrupt (hash mismatch)" }
    }
    let table = match read_strtable(stored.r) { RTable as x => x; error as e => return e }
    let te = match read_texpr(table.r, table.value) { RTExpr as x => x; error as e => return e }
    te.value
}
```

### C23 — `src/emit/tkb_read.c` (continuação)

```c
// tk_texpr_result now provided by checker/tast.h (the canonical home — C1).
static tk_texpr_result texpr_ok(tk_texpr t)  { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
static tk_texpr_result texpr_err(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }

static tk_token_kind kind_of(uint8_t b) { return (tk_token_kind)b; }   // [E7: byte→enum]
static tk_texpr *boxe(tk_texpr t) { tk_texpr *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }

tk_texpr tk_read_texpr(tk_reader *r, tk_strs t) {
    tk_type ty = tk_read_type(r, t);
    uint8_t tag = tk_read_u8(r);
    tk_texpr e = { .type = ty };
    switch (tag) {
        case 0: e.tag = TK_TEXPR_NUMBER; e.as.number.value = (int64_t)tk_read_u64(r); return e;
        case 1: e.tag = TK_TEXPR_VAR;    e.as.var.name  = tk_read_str(r, t); return e;
        case 2: e.tag = TK_TEXPR_STR;    e.as.str.text  = tk_read_str(r, t); return e;
        case 3: e.tag = TK_TEXPR_BYTE;   e.as.byte.value = tk_read_u8(r); return e;
        case 4: e.tag = TK_TEXPR_BINARY; e.as.binary.op = kind_of(tk_read_u8(r));
                e.as.binary.left = boxe(tk_read_texpr(r, t)); e.as.binary.right = boxe(tk_read_texpr(r, t)); return e;
        case 5: e.tag = TK_TEXPR_UNARY;  e.as.unary.op = kind_of(tk_read_u8(r));
                e.as.unary.operand = boxe(tk_read_texpr(r, t)); return e;
        case 6: {
            e.tag = TK_TEXPR_COMPARE; e.as.compare.first = boxe(tk_read_texpr(r, t));
            uint32_t n = tk_read_u32(r); tk_tcmp_term *ts = malloc(n * sizeof *ts); if (n && !ts) abort();
            for (uint32_t i = 0; i < n; i += 1) { ts[i].op = kind_of(tk_read_u8(r)); ts[i].operand = boxe(tk_read_texpr(r, t)); }
            e.as.compare.rest = ts; e.as.compare.nrest = n; return e;
        }
        case 7: {
            e.tag = TK_TEXPR_CALL;
            uint32_t np = tk_read_u32(r); tk_segment *segs = malloc(np * sizeof *segs); if (np && !segs) abort();
            for (uint32_t i = 0; i < np; i += 1) segs[i].name = tk_read_str(r, t);
            e.as.call.callee = (tk_path){ segs, np };
            uint32_t na = tk_read_u32(r); tk_texpr *as = malloc(na * sizeof *as); if (na && !as) abort();
            for (uint32_t i = 0; i < na; i += 1) as[i] = tk_read_texpr(r, t);
            e.as.call.args = as; e.as.call.nargs = na; return e;
        }
        case 10:                                                                /* S1a — Cast: target rides ty; read inner */
            e.tag = TK_TEXPR_CAST;
            e.as.cast.expr = boxe(tk_read_texpr(r, t));
            return e;
        case 11: {                                                              /* S1b — FieldAccess: receiver THEN field */
            e.tag = TK_TEXPR_FIELD_ACCESS;
            e.as.field_access.receiver = boxe(tk_read_texpr(r, t));
            e.as.field_access.field    = tk_read_str(r, t);
            return e;
        }
    }
    r->ok = false; return e;
}

tk_texpr_result tk_deserialize(const tk_byte *data, size_t len) {
    tk_reader r = { data, len, 0, true };
    if (tk_read_u8(&r) != 'T' || tk_read_u8(&r) != 'K' || tk_read_u8(&r) != 'B' || tk_read_u8(&r) != 0)
        return texpr_err("not a .tkb (bad magic)");
    if (tk_read_u32(&r) != 1) return texpr_err("unsupported .tkb version");
    uint64_t stored = tk_read_u64(&r);
    if (len < 16) return texpr_err("truncated .tkb header");
    if (tk_fnv1a(data + 16, len - 16) != stored) return texpr_err(".tkb altered or corrupt (hash mismatch)");
    tk_strs table = tk_read_strtable(&r);
    tk_texpr te = tk_read_texpr(&r, table);
    if (!r.ok) return texpr_err("truncated/corrupt .tkb");
    return texpr_ok(te);
}
```

### Teko — `src/emit/tkb_test.tkt` (round-trip do codec — S1a/S1b)

`serialize`→`deserialize` deve devolver **a mesma** árvore (diferença ZERO, decisão 2);
e qualquer adulteração do corpo deve cair no hash FNV-1a (M.1 — falha visível). Cobre as
tags **10 (`TCast`)** e **11 (`TFieldAccess`)**, fechadas em S. (`if`/`match` — tags 8/9 —
seguem reservadas até existir um serializador de statement.)

```teko
// src/emit/tkb_test.tkt — tests for the .tkb codec (S1a/S1b). NO main: PUBLIC #test

fn u8ty()  -> Type { Prim { kind = PrimKind::U8 } }
fn i64ty() -> Type { Prim { kind = PrimKind::I64 } }

#test
fn tkb_roundtrip_cast() {
    // S1a: TCast { expr = (TNumber : i64) } : u8 — exact round-trip (tag 10 + type + inner).
    let inner = TExpr { kind = TNumber { value = 7 }; type = i64ty() }
    let orig  = TExpr { kind = TCast { expr = inner }; type = u8ty() }
    match deserialize(serialize(orig)) {
        TExpr as got => match got.type {
            Prim as p => {
                assert p.kind == PrimKind::U8                  // target type carried via ty.value
                match got.kind {
                    TCast as c => match c.expr.kind {
                        TNumber as n => assert n.value == 7
                        _ => assert false
                    }
                    _ => assert false
                }
            }
            _ => assert false
        }
        error => assert false
    }
}

#test
fn tkb_roundtrip_field_access() {
    // S1b: TFieldAccess { receiver = (TVar s : Named Foo); field = "token" } : u8 — round-trips exactly.
    let recv = TExpr { kind = TVar { name = "s" }; type = Named { name = "Foo" } }
    let orig = TExpr { kind = TFieldAccess { receiver = recv; field = "token" }; type = u8ty() }
    match deserialize(serialize(orig)) {
        TExpr as got => match got.kind {
            TFieldAccess as f => {
                assert f.field == "token"                       // interned + read back (collect_strings fix)
                match f.receiver.kind { TVar as v => assert v.name == "s"; _ => assert false }
            }
            _ => assert false
        }
        error => assert false
    }
}

#test
fn tkb_tamper_caught() {
    // barrier (M.1): a corrupted body (last byte dropped) fails the FNV-1a hash check → error.
    let inner = TExpr { kind = TNumber { value = 7 }; type = i64ty() }
    let orig  = TExpr { kind = TCast { expr = inner }; type = u8ty() }
    let bytes = serialize(orig)
    let short = slice(bytes, 0, bytes.len - 1)
    match deserialize(short) { TExpr => assert false; error => assert true }
}
```

---
## Etapa 6-3d — o emissor `.tkh` (a interface exportada: assinaturas `exp` + docs)

O **`.tkh`** é a **interface exportada** de um projeto: as assinaturas das funções
`exp` e as declarações de tipo `exp`, com seus **docs**. Está para o `.tkb` como um
`.h` está para um `.o` — o `.tkb` carrega o **programa tipado** (o objeto) e
**descarta** os doc-comments; o `.tkh` **não carrega corpos**, só assinaturas, e
**guarda** os docs. *(**M.3** honestidade: o header é a face honesta da interface, e o
doc pertence à face, não ao objeto; o `.tkb` larga o doc porque o objeto não o usa.)*
Os tipos de uma assinatura são o **`Type` semântico já resolvido** (do checker), então
um consumidor checa contra eles **sem re-resolver** *(**M.3**: nada de "resolva você
mesmo" — o header diz o tipo resolvido; **M.1**: o consumidor recebe exatamente o que
checar)*. Reusa o **codec do `.tkb` inteiro** — `write_type`/`read_type`, a tabela de
strings, o frame com FNV-1a — **sem nenhum primitivo novo** *(**M.5**: peso×uso; a
serialização já existe, o `.tkh` é só uma carga diferente sobre ela)*. Mesma
**detecção de adulteração** do `.tkb` *(**M.1** defesa em profundidade: um `.tkh`
alterado também é pego pelo hash)*. Com isto o trabalho do `.tkb`/`.tkh` **fecha**.

Layout do **`.tkh`** (little-endian — gêmeo do `.tkb`, só muda o `magic` e o corpo):

```
[0..4)   magic     "TKH\0"
[4..8)   version   u32
[8..16)  hash      u64   (FNV-1a sobre o corpo, [16..fim))
---- corpo (entra no hash) ----
string table:   count u32, depois cada string: len u32 + bytes   (reusa o .tkb)
exported types: count u32, depois cada TyExport                  (decls exp)
exported fns:   count u32, depois cada FnSig                     (assinaturas exp)
```

> **Decisão de uniformidade (M.5 — poucas regras, ordem clara).** Um `TyExport` sempre
> serializa as **três** listas (campos / membros / casos); a `shape` diz qual é a que
> vale, e as outras duas vêm **vazias** (count 0). Vazio com count 0 é honesto (**M.3**
> — "0 campos" é verdade para um enum), e o codec fica **sequencial e sem ramificação**
> nos dois lados (**M.1** — mais simples, mais robusto). O custo (8 bytes por tipo nas
> duas listas vazias) é desprezível; a regra única vence a micro-otimização.

> **Diferido — métodos (B.29).** Funções com 1º arg `self` solto (métodos de instância)
> pertencem a um tipo e exigem uma representação de export que o checker ainda não
> fixou (o checker difere a checagem de método, como difere `FieldAccess`). O `.tkh`
> exporta **funções livres** + **declarações de tipo**; o export de método entra quando
> a checagem de método entrar (**M.4** — não construir o export antes da camada que ele
> serializa).

### Teko — `src/emit/tkh.tks`

```teko
// src/emit/tkh.tks  (namespace 'teko::emit')
//
// The `.tkh` is a project's EXPORTED INTERFACE — the `exp` function signatures and
// `exp` type declarations, plus their docs. It is to `.tkb` what a `.h` is to a `.o`:
// the .tkb carries the typed program (the object) and DROPS docs; the .tkh carries no
// bodies, only signatures, and KEEPS docs. The types are the RESOLVED semantic `Type`
// (checker), so a consumer checks against them with no re-resolution. It reuses the
// .tkb codec wholesale (write_type/read_type, the string table, the FNV-1a frame) and
// adds no new primitive. Same tamper-detection as the .tkb.

// --- the exported-interface model -------------------------------------------

// a parameter in a signature: a name + its resolved type.
type SigParam = struct { name: str; type: Type }

// an exported FUNCTION's signature (no body — that is the .tkb's). Docs kept.
type FnSig = struct {
    name:    str
    params:  []SigParam
    ret:     Type
    has_doc: bool
    doc:     str
}

// a field of an exported struct: a name + its resolved type.
type SigField = struct { name: str; type: Type }

// which shape an exported type has (B.14 — struct/enum/variant; flags is evolution).
type TyShape = enum { Struct; Enum; Variant }

// an exported TYPE declaration's interface (nominal — B.13; the consumer needs the
// shape to check construction/membership against it). Per `shape`, exactly one of
// fields/members/cases is meaningful; the other two are serialized empty.
type TyExport = struct {
    name:    str
    shape:   TyShape
    fields:  []SigField     // Struct:  the fields  (empty otherwise)
    members: []str          // Enum:    the members (empty otherwise)
    cases:   []Type         // Variant: the cases   (empty otherwise)
    has_doc: bool
    doc:     str
}

// a whole project's exported surface: its types, then its functions.
type Header = struct { types: []TyExport; fns: []FnSig }

// --- the writers (reuse write_type / write_types; strings by u32 index) -----------

// a doc: a presence byte, then (iff present) its string index.
fn write_doc(buf: []byte, t: StrTable, has_doc: bool, doc: str) -> []byte {
    if !has_doc { return write_u8(buf, 0) }
    write_u32(write_u8(buf, 1), st_find(t, doc))
}

fn write_sigparam(buf: []byte, t: StrTable, p: SigParam) -> []byte {
    write_type(write_u32(buf, st_find(t, p.name)), t, p.type)
}

fn write_sigparams(buf: []byte, t: StrTable, xs: []SigParam) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_sigparam(b, t, xs[i])
        i++
    }
    b
}

fn write_fnsig(buf: []byte, t: StrTable, f: FnSig) -> []byte {
    mut b = write_u32(buf, st_find(t, f.name))
    b = write_sigparams(b, t, f.params)
    b = write_type(b, t, f.ret)
    write_doc(b, t, f.has_doc, f.doc)
}

fn write_fnsigs(buf: []byte, t: StrTable, xs: []FnSig) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_fnsig(b, t, xs[i])
        i++
    }
    b
}

fn write_sigfield(buf: []byte, t: StrTable, f: SigField) -> []byte {
    write_type(write_u32(buf, st_find(t, f.name)), t, f.type)
}

fn write_sigfields(buf: []byte, t: StrTable, xs: []SigField) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_sigfield(b, t, xs[i])
        i++
    }
    b
}

// a list of member NAMES (enum) → count + each name index.
fn write_members(buf: []byte, t: StrTable, xs: []str) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_u32(b, st_find(t, xs[i]))
        i++
    }
    b
}

// the TyShape ordinal byte — explicit (no enum→int cast needed). Struct=0 Enum=1 Variant=2.
fn shape_byte(s: TyShape) -> byte {
    if s == TyShape::Struct { return 0 }
    if s == TyShape::Enum   { return 1 }
    2
}

// uniform: all three lists are written; `shape` selects the meaningful one, the others
// come out empty (count 0). Sequential, branch-free both ways (M.1 / M.5).
fn write_tyexport(buf: []byte, t: StrTable, e: TyExport) -> []byte {
    mut b = write_u32(buf, st_find(t, e.name))
    b = write_u8(b, shape_byte(e.shape))
    b = write_sigfields(b, t, e.fields)
    b = write_members(b, t, e.members)
    b = write_types(b, t, e.cases)
    write_doc(b, t, e.has_doc, e.doc)
}

fn write_tyexports(buf: []byte, t: StrTable, xs: []TyExport) -> []byte {
    mut b = write_u32(buf, xs.len to u32)
    mut i = 0
    loop {
        if i >= xs.len { break }
        b = write_tyexport(b, t, xs[i])
        i++
    }
    b
}

fn write_header(buf: []byte, t: StrTable, h: Header) -> []byte {
    write_fnsigs(write_tyexports(buf, t, h.types), t, h.fns)
}

// --- collect every string (pre-pass; mirrors the writers; reuse collect_type_*) ----

fn collect_doc(t: StrTable, has_doc: bool, doc: str) -> StrTable {
    if !has_doc { return t }
    st_intern(t, doc).table
}

fn collect_sigparam(t: StrTable, p: SigParam) -> StrTable {
    collect_type_strings(st_intern(t, p.name).table, p.type)
}

fn collect_sigparams(t: StrTable, xs: []SigParam) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = collect_sigparam(tab, xs[i])
        i++
    }
    tab
}

fn collect_fnsig(t: StrTable, f: FnSig) -> StrTable {
    let t1 = st_intern(t, f.name).table
    let t2 = collect_sigparams(t1, f.params)
    let t3 = collect_type_strings(t2, f.ret)
    collect_doc(t3, f.has_doc, f.doc)
}

fn collect_fnsigs(t: StrTable, xs: []FnSig) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = collect_fnsig(tab, xs[i])
        i++
    }
    tab
}

fn collect_sigfield(t: StrTable, f: SigField) -> StrTable {
    collect_type_strings(st_intern(t, f.name).table, f.type)
}

fn collect_sigfields(t: StrTable, xs: []SigField) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = collect_sigfield(tab, xs[i])
        i++
    }
    tab
}

fn collect_members(t: StrTable, xs: []str) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = st_intern(tab, xs[i]).table
        i++
    }
    tab
}

fn collect_tyexport(t: StrTable, e: TyExport) -> StrTable {
    let t1 = st_intern(t, e.name).table
    let t2 = collect_sigfields(t1, e.fields)
    let t3 = collect_members(t2, e.members)
    let t4 = collect_type_list(t3, e.cases)
    collect_doc(t4, e.has_doc, e.doc)
}

fn collect_tyexports(t: StrTable, xs: []TyExport) -> StrTable {
    mut tab = t
    mut i = 0
    loop {
        if i >= xs.len { break }
        tab = collect_tyexport(tab, xs[i])
        i++
    }
    tab
}

fn collect_header(t: StrTable, h: Header) -> StrTable {
    collect_fnsigs(collect_tyexports(t, h.types), h.fns)
}

// --- the frame: collect → table → body → FNV-1a → header ("TKH\0" + ver + hash) ----

fn emit_tkh(h: Header) -> []byte {
    let table = collect_header(st_empty(), h)
    let body = write_header(write_strtable(teko::list::empty(), table), table, h)
    let hash = fnv1a(body)
    mut out = teko::list::empty()
    out = write_u8(out, b'T')
    out = write_u8(out, b'K')
    out = write_u8(out, b'H')
    out = write_u8(out, 0)
    out = write_u32(out, 1)              // version 1
    out = write_u64(out, hash)           // FNV-1a of the body
    append_bytes(out, body)
}

// --- the readers (inverse; reuse the Reader, read_type, read_types, read_strtable) --

type RDoc        = struct { r: Reader; has_doc: bool; doc: str }
type RSigParam   = struct { r: Reader; value: SigParam }
type RSigParams  = struct { r: Reader; value: []SigParam }
type RFnSig      = struct { r: Reader; value: FnSig }
type RFnSigs     = struct { r: Reader; value: []FnSig }
type RSigField   = struct { r: Reader; value: SigField }
type RSigFields  = struct { r: Reader; value: []SigField }
type RMembers    = struct { r: Reader; value: []str }
type RTyExport   = struct { r: Reader; value: TyExport }
type RTyExports  = struct { r: Reader; value: []TyExport }
type RHeader     = struct { r: Reader; value: Header }

fn read_doc(r: Reader, table: []str) -> RDoc | error {
    let p = match read_u8(r) { RByte as x => x; error as e => return e }
    if p.value == 0 { return RDoc { r = p.r; has_doc = false; doc = "" } }
    let d = match read_str(p.r, table) { RStr as x => x; error as e => return e }
    RDoc { r = d.r; has_doc = true; doc = d.value }
}

fn read_sigparam(r: Reader, table: []str) -> RSigParam | error {
    let nm = match read_str(r, table)     { RStr as x => x; error as e => return e }
    let ty = match read_type(nm.r, table) { RType as x => x; error as e => return e }
    RSigParam { r = ty.r; value = SigParam { name = nm.value; type = ty.value } }
}

fn read_sigparams(r: Reader, table: []str) -> RSigParams | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let p = match read_sigparam(rr, table) { RSigParam as x => x; error as e => return e }
        xs = teko::list::push(xs, p.value)
        rr = p.r
        i++
    }
    RSigParams { r = rr; value = xs }
}

fn read_fnsig(r: Reader, table: []str) -> RFnSig | error {
    let nm = match read_str(r, table)          { RStr as x => x; error as e => return e }
    let ps = match read_sigparams(nm.r, table) { RSigParams as x => x; error as e => return e }
    let rt = match read_type(ps.r, table)      { RType as x => x; error as e => return e }
    let dc = match read_doc(rt.r, table)       { RDoc as x => x; error as e => return e }
    RFnSig { r = dc.r; value = FnSig {
        name = nm.value; params = ps.value; ret = rt.value
        has_doc = dc.has_doc; doc = dc.doc
    } }
}

fn read_fnsigs(r: Reader, table: []str) -> RFnSigs | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let f = match read_fnsig(rr, table) { RFnSig as x => x; error as e => return e }
        xs = teko::list::push(xs, f.value)
        rr = f.r
        i++
    }
    RFnSigs { r = rr; value = xs }
}

fn read_sigfield(r: Reader, table: []str) -> RSigField | error {
    let nm = match read_str(r, table)     { RStr as x => x; error as e => return e }
    let ty = match read_type(nm.r, table) { RType as x => x; error as e => return e }
    RSigField { r = ty.r; value = SigField { name = nm.value; type = ty.value } }
}

fn read_sigfields(r: Reader, table: []str) -> RSigFields | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let f = match read_sigfield(rr, table) { RSigField as x => x; error as e => return e }
        xs = teko::list::push(xs, f.value)
        rr = f.r
        i++
    }
    RSigFields { r = rr; value = xs }
}

fn read_members(r: Reader, table: []str) -> RMembers | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let s = match read_str(rr, table) { RStr as x => x; error as e => return e }
        xs = teko::list::push(xs, s.value)
        rr = s.r
        i++
    }
    RMembers { r = rr; value = xs }
}

// inverse of shape_byte (byte → TyShape).
fn shape_of(b: byte) -> TyShape {
    if b == 0 { return TyShape::Struct }
    if b == 1 { return TyShape::Enum }
    TyShape::Variant
}

fn read_tyexport(r: Reader, table: []str) -> RTyExport | error {
    let nm = match read_str(r, table)          { RStr as x => x; error as e => return e }
    let sh = match read_u8(nm.r)               { RByte as x => x; error as e => return e }
    let fs = match read_sigfields(sh.r, table) { RSigFields as x => x; error as e => return e }
    let ms = match read_members(fs.r, table)   { RMembers as x => x; error as e => return e }
    let cs = match read_types(ms.r, table)     { RTypes as x => x; error as e => return e }
    let dc = match read_doc(cs.r, table)       { RDoc as x => x; error as e => return e }
    RTyExport { r = dc.r; value = TyExport {
        name = nm.value; shape = shape_of(sh.value)
        fields = fs.value; members = ms.value; cases = cs.value
        has_doc = dc.has_doc; doc = dc.doc
    } }
}

fn read_tyexports(r: Reader, table: []str) -> RTyExports | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut xs = teko::list::empty()
    mut i = 0
    loop {
        if i >= (n.value to u64) { break }
        let e = match read_tyexport(rr, table) { RTyExport as x => x; error as e => return e }
        xs = teko::list::push(xs, e.value)
        rr = e.r
        i++
    }
    RTyExports { r = rr; value = xs }
}

fn read_header(r: Reader, table: []str) -> RHeader | error {
    let ts = match read_tyexports(r, table) { RTyExports as x => x; error as e => return e }
    let fs = match read_fnsigs(ts.r, table) { RFnSigs as x => x; error as e => return e }
    RHeader { r = fs.r; value = Header { types = ts.value; fns = fs.value } }
}

// THE ENTRY: verify magic "TKH\0" + version, recompute the FNV-1a, then read.
fn read_tkh(data: []byte) -> Header | error {
    let r0 = Reader { data = data; pos = 0 }
    let m0 = match read_u8(r0)   { RByte as x => x; error as e => return e }
    let m1 = match read_u8(m0.r) { RByte as x => x; error as e => return e }
    let m2 = match read_u8(m1.r) { RByte as x => x; error as e => return e }
    let m3 = match read_u8(m2.r) { RByte as x => x; error as e => return e }
    if m0.value != b'T' || m1.value != b'K' || m2.value != b'H' || m3.value != 0 {
        return error { message = "not a .tkh (bad magic)" }
    }
    let ver = match read_u32(m3.r) { RU32 as x => x; error as e => return e }
    if ver.value != 1 { return error { message = "unsupported .tkh version" } }
    let stored = match read_u64(ver.r) { RU64 as x => x; error as e => return e }
    if data.len < 16 { return error { message = "truncated .tkh header" } }
    if fnv1a(slice(data, 16, data.len)) != stored.value {
        return error { message = ".tkh altered or corrupt (hash mismatch)" }
    }
    let table = match read_strtable(stored.r) { RTable as x => x; error as e => return e }
    let h = match read_header(table.r, table.value) { RHeader as x => x; error as e => return e }
    h.value
}
```

### C23 — `src/emit/tkh.h` + `tkh.c`

```c
// src/emit/tkh.h — the `.tkh` exported-interface emitter. Mirrors tkh.tks.
#ifndef TK_EMIT_TKH_H
#define TK_EMIT_TKH_H

#include "tkb_frame.h"   // tk_write_u64, the writers, the string table, tk_fnv1a
#include "tkb_read.h"    // tk_reader, tk_strs, tk_read_type, tk_read_strtable

// --- the exported-interface model ---
typedef struct { tk_str name; tk_type type; } tk_sigparam;
typedef struct { tk_str name; tk_type type; } tk_sigfield;

typedef struct {
    tk_str       name;
    tk_sigparam *params;  size_t nparams;
    tk_type      ret;
    bool         has_doc; tk_str doc;
} tk_fnsig;

typedef enum { TK_TY_STRUCT, TK_TY_ENUM, TK_TY_VARIANT } tk_ty_shape;

typedef struct {
    tk_str       name;
    tk_ty_shape  shape;
    tk_sigfield *fields;  size_t nfields;     // Struct
    tk_str      *members; size_t nmembers;    // Enum
    tk_type     *cases;   size_t ncases;      // Variant
    bool         has_doc; tk_str doc;
} tk_tyexport;

typedef struct {
    tk_tyexport *types; size_t ntypes;
    tk_fnsig    *fns;   size_t nfns;
} tk_header;

TK_RESULT(tk_header, tk_header_result);

tk_bytes         tk_emit_tkh(const tk_header *h);
tk_header_result tk_read_tkh(const tk_byte *data, size_t len);

#endif // TK_EMIT_TKH_H
```

```c
// src/emit/tkh.c
#include "tkh.h"
#include <stdlib.h>

// --- collect (mutates the table via intern; mirrors the writers) ---
// tkb_frame.c's collect_type is file-static, so we re-derive the tiny piece here.
static void th_collect_type(tk_strtable *t, tk_type ty);
static void th_collect_type_list(tk_strtable *t, const tk_type *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) th_collect_type(t, xs[i]);
}
static void th_collect_type(tk_strtable *t, tk_type ty) {
    switch (ty.tag) {
        case TK_TYPE_NAMED:   tk_st_intern(t, ty.as.named.name); break;
        case TK_TYPE_SLICE:   th_collect_type(t, *ty.as.slice.element); break;
        case TK_TYPE_OPTIONAL: th_collect_type(t, *ty.as.optional.inner); break;
        case TK_TYPE_VARIANT: th_collect_type_list(t, ty.as.variant.members, ty.as.variant.len); break;
        case TK_TYPE_FUNC:    th_collect_type_list(t, ty.as.func.params, ty.as.func.nparams);
                              th_collect_type(t, *ty.as.func.ret); break;
        default: break;
    }
}

static void collect_doc(tk_strtable *t, bool has_doc, tk_str doc) {
    if (has_doc) tk_st_intern(t, doc);
}
static void collect_fnsig(tk_strtable *t, const tk_fnsig *f) {
    tk_st_intern(t, f->name);
    for (size_t i = 0; i < f->nparams; i += 1) { tk_st_intern(t, f->params[i].name); th_collect_type(t, f->params[i].type); }
    th_collect_type(t, f->ret);
    collect_doc(t, f->has_doc, f->doc);
}
static void collect_tyexport(tk_strtable *t, const tk_tyexport *e) {
    tk_st_intern(t, e->name);
    for (size_t i = 0; i < e->nfields; i += 1)  { tk_st_intern(t, e->fields[i].name); th_collect_type(t, e->fields[i].type); }
    for (size_t i = 0; i < e->nmembers; i += 1) tk_st_intern(t, e->members[i]);
    th_collect_type_list(t, e->cases, e->ncases);
    collect_doc(t, e->has_doc, e->doc);
}
static void collect_header(tk_strtable *t, const tk_header *h) {
    for (size_t i = 0; i < h->ntypes; i += 1) collect_tyexport(t, &h->types[i]);
    for (size_t i = 0; i < h->nfns;   i += 1) collect_fnsig(t, &h->fns[i]);
}

// --- writers ---
static tk_bytes write_doc(tk_bytes b, tk_strtable t, bool has_doc, tk_str doc) {
    if (!has_doc) return tk_write_u8(b, 0);
    return tk_write_u32(tk_write_u8(b, 1), tk_st_find(t, doc));
}
static tk_byte shape_byte(tk_ty_shape s) { return (tk_byte)s; }   // Struct=0 Enum=1 Variant=2

static tk_bytes write_type_list(tk_bytes b, tk_strtable t, const tk_type *xs, size_t n) {
    b = tk_write_u32(b, (uint32_t)n);
    for (size_t i = 0; i < n; i += 1) b = tk_write_type(b, t, xs[i]);
    return b;
}
static tk_bytes write_fnsig(tk_bytes b, tk_strtable t, const tk_fnsig *f) {
    b = tk_write_u32(b, tk_st_find(t, f->name));
    b = tk_write_u32(b, (uint32_t)f->nparams);
    for (size_t i = 0; i < f->nparams; i += 1) {
        b = tk_write_u32(b, tk_st_find(t, f->params[i].name));
        b = tk_write_type(b, t, f->params[i].type);
    }
    b = tk_write_type(b, t, f->ret);
    return write_doc(b, t, f->has_doc, f->doc);
}
static tk_bytes write_tyexport(tk_bytes b, tk_strtable t, const tk_tyexport *e) {
    b = tk_write_u32(b, tk_st_find(t, e->name));
    b = tk_write_u8(b, shape_byte(e->shape));
    b = tk_write_u32(b, (uint32_t)e->nfields);
    for (size_t i = 0; i < e->nfields; i += 1) {
        b = tk_write_u32(b, tk_st_find(t, e->fields[i].name));
        b = tk_write_type(b, t, e->fields[i].type);
    }
    b = tk_write_u32(b, (uint32_t)e->nmembers);
    for (size_t i = 0; i < e->nmembers; i += 1) b = tk_write_u32(b, tk_st_find(t, e->members[i]));
    b = write_type_list(b, t, e->cases, e->ncases);
    return write_doc(b, t, e->has_doc, e->doc);
}
static tk_bytes write_header(tk_bytes b, tk_strtable t, const tk_header *h) {
    b = tk_write_u32(b, (uint32_t)h->ntypes);
    for (size_t i = 0; i < h->ntypes; i += 1) b = write_tyexport(b, t, &h->types[i]);
    b = tk_write_u32(b, (uint32_t)h->nfns);
    for (size_t i = 0; i < h->nfns; i += 1) b = write_fnsig(b, t, &h->fns[i]);
    return b;
}

static tk_bytes append_bytes(tk_bytes dst, tk_bytes src) {
    for (size_t i = 0; i < src.len; i += 1) dst = tk_bytes_push(dst, src.ptr[i]);
    return dst;
}

tk_bytes tk_emit_tkh(const tk_header *h) {
    tk_strtable table = tk_st_empty();
    collect_header(&table, h);
    tk_bytes body = write_header(tk_write_strtable((tk_bytes){0}, table), table, h);
    uint64_t hash = tk_fnv1a(body.ptr, body.len);
    tk_bytes out = {0};
    out = tk_write_u8(out, (tk_byte)'T'); out = tk_write_u8(out, (tk_byte)'K');
    out = tk_write_u8(out, (tk_byte)'H'); out = tk_write_u8(out, 0);
    out = tk_write_u32(out, 1);              // version 1
    out = tk_write_u64(out, hash);           // FNV-1a of the body
    return append_bytes(out, body);
}

// --- readers ---
static tk_ty_shape shape_of(uint8_t b) {
    switch (b) { case 0: return TK_TY_STRUCT; case 1: return TK_TY_ENUM; default: return TK_TY_VARIANT; }
}
static void read_doc(tk_reader *r, tk_strs t, bool *has_doc, tk_str *doc) {
    uint8_t p = tk_read_u8(r);
    if (p == 0) { *has_doc = false; *doc = (tk_str){ NULL, 0 }; return; }
    *has_doc = true; *doc = tk_read_str(r, t);
}
static tk_fnsig read_fnsig(tk_reader *r, tk_strs t) {
    tk_fnsig f = {0};
    f.name = tk_read_str(r, t);
    f.nparams = tk_read_u32(r);
    f.params = malloc(f.nparams ? f.nparams * sizeof *f.params : 1); if (!f.params) abort();
    for (size_t i = 0; i < f.nparams; i += 1) { f.params[i].name = tk_read_str(r, t); f.params[i].type = tk_read_type(r, t); }
    f.ret = tk_read_type(r, t);
    read_doc(r, t, &f.has_doc, &f.doc);
    return f;
}
static tk_tyexport read_tyexport(tk_reader *r, tk_strs t) {
    tk_tyexport e = {0};
    e.name  = tk_read_str(r, t);
    e.shape = shape_of(tk_read_u8(r));
    e.nfields = tk_read_u32(r);
    e.fields = malloc(e.nfields ? e.nfields * sizeof *e.fields : 1); if (!e.fields) abort();
    for (size_t i = 0; i < e.nfields; i += 1) { e.fields[i].name = tk_read_str(r, t); e.fields[i].type = tk_read_type(r, t); }
    e.nmembers = tk_read_u32(r);
    e.members = malloc(e.nmembers ? e.nmembers * sizeof *e.members : 1); if (!e.members) abort();
    for (size_t i = 0; i < e.nmembers; i += 1) e.members[i] = tk_read_str(r, t);
    e.ncases = tk_read_u32(r);
    e.cases = malloc(e.ncases ? e.ncases * sizeof *e.cases : 1); if (!e.cases) abort();
    for (size_t i = 0; i < e.ncases; i += 1) e.cases[i] = tk_read_type(r, t);
    read_doc(r, t, &e.has_doc, &e.doc);
    return e;
}
static tk_header read_header(tk_reader *r, tk_strs t) {
    tk_header h = {0};
    h.ntypes = tk_read_u32(r);
    h.types = malloc(h.ntypes ? h.ntypes * sizeof *h.types : 1); if (!h.types) abort();
    for (size_t i = 0; i < h.ntypes; i += 1) h.types[i] = read_tyexport(r, t);
    h.nfns = tk_read_u32(r);
    h.fns = malloc(h.nfns ? h.nfns * sizeof *h.fns : 1); if (!h.fns) abort();
    for (size_t i = 0; i < h.nfns; i += 1) h.fns[i] = read_fnsig(r, t);
    return h;
}

static tk_header_result hdr_err(const char *m) { return (tk_header_result){ .ok = false, .as.error = tk_error_make(m) }; }
static tk_header_result hdr_ok(tk_header h)     { return (tk_header_result){ .ok = true,  .as.value = h }; }

tk_header_result tk_read_tkh(const tk_byte *data, size_t len) {
    tk_reader r = { data, len, 0, true };
    if (tk_read_u8(&r) != 'T' || tk_read_u8(&r) != 'K' || tk_read_u8(&r) != 'H' || tk_read_u8(&r) != 0)
        return hdr_err("not a .tkh (bad magic)");
    if (tk_read_u32(&r) != 1) return hdr_err("unsupported .tkh version");
    uint64_t stored = tk_read_u64(&r);
    if (len < 16) return hdr_err("truncated .tkh header");
    if (tk_fnv1a(data + 16, len - 16) != stored) return hdr_err(".tkh altered or corrupt (hash mismatch)");
    tk_strs table = tk_read_strtable(&r);
    tk_header h = read_header(&r, table);
    if (!r.ok) return hdr_err("truncated/corrupt .tkh");
    return hdr_ok(h);
}
```

---
## Próximas etapas

- **Etapa 6-3 — `.tkb`/`.tkh`: FECHADA.** Round-trip do `.tkb` (E6-3c-1 infra,
  E6-3c-2 escrita, E6-3c-3 frame + deserialize com verificação de hash) **e** o
  emissor `.tkh` (E6-3d: interface exportada — assinaturas `exp` + decls `exp` + docs,
  reusando o codec do `.tkb`, com a mesma detecção de adulteração). Falta apenas
  **fiar a pipeline**: um driver de emit que **constrói** o `Header` (`[]TyExport` +
  `[]FnSig`) a partir do programa **checado** (filtrando `is_exp`) e chama `emit_tkh` —
  hoje o codec existe e é testável por round-trip; o consumo cross-projeto entra com o
  segundo projeto (**M.4**).
- **Evolução `check_*` → `type_*` (C1): CONCLUÍDA.** Todo nó de expressão, statement
  e item é re-derivado na **AST tipada** (`tast.tks`/`typer.tks` + mirror C, testes
  `checker_test.tkt` feliz/barreira). `revalidate` (E6-2) ganhou as arms `TIfExpr`/
  `TMatchExpr` (confiança estrutural — a re-derivação profunda dos blocos entra com a
  validação a nível de programa). O codec `.tkb` (`write_texpr`) reservou as **tags 8/9**
  para `if`/`match` tipados; a **serialização completa** deles (precisa de statements
  tipados serializados) é **Fase S** — `read` rejeita tags > 7 visível (M.1).
- **Cast `to` (C2): CONCLUÍDO (inteiros) — regra B (↺ redefinição).** `type_cast` + `cast_check`
  + `value_fits`/`const_range_check` tipam `x to T`: **toda conversão numérica→numérica definida é
  permitida** (incl. narrowing `i32 to i8` e sinal `i32 to u32` — o valor pode caber, M.0). A perda
  é **pega, nunca silenciosa**: constante fora-de-faixa → **erro de compilação** (fail-early, M.1);
  **a validação de possibilidade de um valor runtime é runtime** — conversão impossível (não cabe)
  → **PÂNICO** (debug E release; paridade ÷0/OOB, não o wrap do overflow), guarda em **codegen**
  (adiado M.4). Recusa só `bool`↔num e não-numéricos (indefinidos). Nó `TCast`; `revalidate` **re-deriva**
  que a conversão é *definida* (M.3); `write_texpr` reservou a **tag 10** (round-trip = **S1a**).
  **Redefine** a regra antiga (proibir perda em compilação) — ver HISTORY conversões + Índice de
  Redefinições. **Float/Named adiados (M.4):** sem float em `PrimKind`; `resolve_named` devolve `Named`
  opaco (não rebaixa newtype→base). As linhas float do roadmap são **pendentes-floats (alpha)**.
- **FieldAccess `x.campo` (C3): CONCLUÍDO.** `type_field_access` resolve o campo via
  `Named`→`type_table_find`→`StructBody`→`field_type` (B.29 leitura de campo; `MethodCall`
  segue diferido); nó `TFieldAccess`, `revalidate` re-deriva (invariante: receiver `Named`),
  `write_texpr` reservou **tag 11** (round-trip = **S1b**). Mirror C + 4 testes.
- **Regra `mut` (C4): CONCLUÍDO.** `ValBinding` ganhou `is_mut`; `define(…, is_mut)` +
  `bind_is_mut(kind)` (só `Mut` é mutável — let/const/params/match-bindings = `false`, B.21);
  `lookup_binding` devolve o binding inteiro e `type_assign` recusa escrita em alvo não-`mut`
  (M.1+M.2). `lookup` virou wrapper fino (callers intactos).
- **Literal anotado (C6): CONCLUÍDO (Side D — veredito de tribunal + aval do legislador).**
  `let x: T = <literal>` grava o literal **AS-IS** (leaf `i64`); a anotação vive no `TBinding.bound`
  e `annotated_literal_ok`/`value_fits` provam a adoção (constante fora-de-faixa → erro *fail-early*;
  não-literal com tipo ≠ → erro, sem conversão implícita — M.2). O arm `TNumber` do `revalidate`
  **fica estrito (i64)** — preservando a re-derivação env-less (decisão #7); a re-prova no nível do
  `TBinding` via `value_fits` entra com `validate_statement` (deferido).
- **`return` vs tipo (C5): CONCLUÍDO (DEFER).** `type_function` faz um post-pass: `check_returns`
  (todo `return e` alcançável como statement — descendo em corpos de `loop` e blocos de `if` — casa
  com o retorno declarado, com **inclusão de membro em variante**, `assignable_to`, B.14) +
  `check_trailing_value` (valor final só quando o último stmt é expressão — **guard** p/ não
  false-rejeitar corpos que terminam em `loop`/`if`/`match` divergente). **Adiado (M.4, item próprio):**
  a análise de divergência/definite-return ("todo caminho rende valor"; returns em braços de `match`).
  C5 vive na camada **typed** — a única camada de checagem restante.
- **Refinamentos restantes**: padrões `Field`/`Range`/`Alt` + exaustividade no eixo variante (C7).
- **Dívida estrutural (M.5): RESOLVIDO.** A camada legada `check_*` (E4/E5) foi **aposentada** —
  um portão de subsunção provou que a camada produtora `type_*` (C1) subsume cada checagem legada
  sem perda. Restam apenas os predicados de tipo compartilhados (E4), os helpers de padrão +
  exaustividade (`tk_check_pattern`/`tk_exhaustive`, E5b-2, agora não-`static` e reusados pela
  camada typed) e a camada `type_*`. O imposto de tocar duas camadas a cada item C acabou.
- **Gap de fundação — driver do checker vs. AST pós-R-main (M.4).** `collect`/
  `type_program`/`type_item` operam sobre o modelo **`Program`/`Item`** (E5c), mas o
  parser pós-R-main entrega **`MainFile`/`Module`** (`parse_main_file`/`parse_module`).
  Reconciliar o driver (e `collect`) a esses dois pontos de entrada é um passo separado
  (não-C1): a camada anterior mudou e a posterior precisa acompanhá-la antes do consumo
  real da pipeline.
- **Export de método (B.29) — diferido** junto com a checagem de método: quando o
  checker fixar como um método (1º arg `self` solto) é checado e associado ao seu tipo,
  o `.tkh` ganha a forma de export correspondente (não antes — **M.4**).