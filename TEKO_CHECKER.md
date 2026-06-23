# Teko — O Checker (`src/checker/*`), em Teko + C23

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

// the cases that carry data (Teko has no payload on a bare enum — a variant case
// is a struct). Byte/Str/Error are markers (no payload).
type Prim    = struct { kind: PrimKind }
type Slice   = struct { element: Type }        // []T — recursive (B.8)
type Named   = struct { name: str }            // a user type, equal by NAME (nominal)
type Variant = struct { members: []Type }      // A | B | … (two or more)
type Func    = struct { params: []Type; ret: Type }   // (params) -> ret
type Byte    = struct { }                      // the octet type
type Str     = struct { }                      // validated UTF-8
type Error  = struct { }                      // the injected error
type Unit   = struct { }                      // value-less (a block with no trailing expr)

// a semantic type. (Compiler-managed indirection for the recursive cases.)
type Type = Prim | Byte | Str | Slice | Named | Variant | Func | Error | Unit

// nominal type equality (B.13): structural over the shape, but Named is by name.
fn type_eq(a: Type, b: Type) -> bool {
    match a {
        Prim as pa    => match b { Prim as pb    => pa.kind == pb.kind;             _ => false }
        Byte          => match b { Byte          => true;                           _ => false }
        Str           => match b { Str           => true;                           _ => false }
        Error        => match b { Error        => true;                           _ => false }
        Unit         => match b { Unit         => true;                           _ => false }
        Slice as sa   => match b { Slice as sb   => type_eq(sa.element, sb.element); _ => false }
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
    TK_TYPE_PRIM, TK_TYPE_BYTE, TK_TYPE_STR, TK_TYPE_SLICE,
    TK_TYPE_NAMED, TK_TYPE_VARIANT, TK_TYPE_FUNC, TK_TYPE_ERROR, TK_TYPE_UNIT,
} tk_type_tag;

// recursive (the Slice/Variant/Func cases hold tk_type) — the indirection the Teko
// side keeps compiler-managed shows here as a forward declaration + pointers.
typedef struct tk_type tk_type;

struct tk_type {
    tk_type_tag tag;
    union {
        tk_prim_kind prim;                                   // TK_TYPE_PRIM
        struct { tk_type *element; }            slice;       // TK_TYPE_SLICE
        struct { tk_str name; }                 named;       // TK_TYPE_NAMED (nominal)
        struct { tk_type *members; size_t len; } variant;    // TK_TYPE_VARIANT
        struct { tk_type *params; size_t nparams; tk_type *ret; } func;  // TK_TYPE_FUNC
        // BYTE, STR, ERROR carry no payload
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
        case TK_TYPE_UNIT:  return true;
        case TK_TYPE_SLICE: return tk_type_eq(a->as.slice.element, b->as.slice.element);
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
    name: str
    type: Type
}

// the environment: a FLAT list of bindings. A later binding shadows an earlier of
// the same name (lexical); block scoping is implicit (the caller keeps its prefix
// when the recursion returns — the block's bindings were a local extension).
type Env = []ValBinding

// define a name (append); returns the extended env (ref-less consume-return).
fn define(env: Env, name: str, t: Type) -> Env {
    teko::list::push(env, ValBinding { name = name; type = t })
}

// look a name up — most recent (innermost) first; error if undefined.
fn lookup(env: Env, name: str) -> Type | error {
    mut i = env.len
    loop {
        if i == 0 { break }
        i = i - 1
        if env[i].name == name { return env[i].type }
    }
    error { message = $"undefined name: {name}" }
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

typedef struct { tk_str name; tk_type type; } tk_val_binding;
TK_LIST(tk_val_binding, tk_env);        // a flat list; later bindings shadow earlier

TK_RESULT(tk_type, tk_type_result);     // Type | error

tk_env         tk_env_define(tk_env env, tk_str name, tk_type t);
tk_type_result tk_env_lookup(tk_env env, tk_str name);
tk_type_result tk_builtin_type(tk_str name);

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

tk_env tk_env_define(tk_env env, tk_str name, tk_type t) {
    return tk_env_push(env, (tk_val_binding){ .name = name, .type = t });
}

tk_type_result tk_env_lookup(tk_env env, tk_str name) {
    for (size_t i = env.len; i > 0; i -= 1) {        // innermost (most recent) first
        tk_val_binding b = env.ptr[i - 1];
        if (name_eq(b.name, name)) {
            return (tk_type_result){ .ok = true, .as.value = b.type };
        }
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("undefined name") };
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
                env = define(env, f.name, ft)
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
            env = tk_env_define(env, f.name, ft.as.value);
        }
    }
    tk_collected c = { .types = table, .env = env };
    return (tk_collected_result){ .ok = true, .as.value = c };
}
```

---
## Etapa 4 — checagem de expressões (inferência local + regimes de B.22)

`check_expr` dá o `Type` de cada nó, de baixo pra cima. **B.22**: aritmético/bitwise
exigem operandos do **mesmo tipo** (sem promoção) e inteiros; shift quer inteiros (a
contagem pode diferir); comparação (na `Compare`) é **sign-check** → `bool`. `if` e
`match` contêm blocos → ficam na E5 (delegados aqui).

> Dois pontos marcados: (a) `Number` recebe `i64` por ora — **tipar literais em
> contexto anotado** (`let x: u8 = 1`) ainda é decisão pendente (bidirecional/coerção
> de literal); (b) `is_comparable` é permissivo (mesmo-tipo **ou** numérico) — separar
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

// --- the expression checker (local inference) ---

fn check_expr(e: Expr, env: Env, table: TypeTable) -> Type | error {
    match e {
        Number       => Prim { kind = PrimKind::I64 }   // [literal typing: pending]
        StrLit       => Str { }
        ByteLit      => Byte { }
        Var as v     => lookup(env, v.name)
        Binary as b  => check_binary(b, env, table)
        Unary as u   => check_unary(u, env, table)
        Compare as c => check_compare(c, env, table)
        Call as cl   => check_call(cl, env, table)
        IfExpr as f    => check_if(f, env, table)          // Etapa 5 (needs blocks)
        MatchExpr as m => check_match(m, env, table)       // Etapa 5
    }
}

// arithmetic/bitwise: same-type integers → that type (NO promotion — B.22).
// shift: integer value/count → the value's type.
fn check_binary(b: Binary, env: Env, table: TypeTable) -> Type | error {
    let lt = match check_expr(b.left, env, table)  { Type as t => t; error as e => return e }
    let rt = match check_expr(b.right, env, table) { Type as t => t; error as e => return e }
    if op_is_shift(b.op) {
        if !is_integer(lt) || !is_integer(rt) { return error { message = "shift needs integer operands" } }
        return lt
    }
    if op_is_arith_bitwise(b.op) {
        if !is_integer(lt) { return error { message = "arithmetic/bitwise needs an integer" } }
        if !type_eq(lt, rt) { return error { message = "operands must be the same type (no promotion — B.22)" } }
        return lt
    }
    error { message = "not a binary operator" }
}

// unary: `-`/`~` need an integer (→ same type); `!` needs a bool (→ bool).
fn check_unary(u: Unary, env: Env, table: TypeTable) -> Type | error {
    let t = match check_expr(u.operand, env, table) { Type as ty => ty; error as e => return e }
    if u.op == lexer::TokenKind::Minus || u.op == lexer::TokenKind::Tilde {
        if !is_integer(t) { return error { message = "unary -/~ needs an integer" } }
        return t
    }
    if u.op == lexer::TokenKind::Bang {
        if !is_bool(t) { return error { message = "! needs a bool" } }
        return t
    }
    error { message = "not a unary operator" }
}

// a comparison chain (a<b<c): each adjacent pair sign-checked (B.22); result bool.
fn check_compare(c: Compare, env: Env, table: TypeTable) -> Type | error {
    mut prev = match check_expr(c.first, env, table) { Type as t => t; error as e => return e }
    mut i = 0
    loop {
        if i >= c.rest.len { break }
        let cur = match check_expr(c.rest[i].operand, env, table) { Type as t => t; error as e => return e }
        if !is_comparable(prev, cur) { return error { message = "operands are not comparable" } }
        prev = cur
        i++
    }
    Prim { kind = PrimKind::Bool }
}

// a call: look the callee up (a Func), check args against params, → ret type.
fn check_call(c: Call, env: Env, table: TypeTable) -> Type | error {
    let name = c.callee.segments[c.callee.segments.len - 1].name
    let ft = match lookup(env, name) { Type as t => t; error as e => return e }
    match ft {
        Func as f => {
            if c.args.len != f.params.len { return error { message = "wrong number of arguments" } }
            mut i = 0
            loop {
                if i >= c.args.len { break }
                let at = match check_expr(c.args[i], env, table) { Type as t => t; error as e => return e }
                if !type_eq(at, f.params[i]) { return error { message = "argument type mismatch" } }
                i++
            }
            f.ret
        }
        _ => error { message = $"not a function: {name}" }
    }
}
```

### C23 — `src/checker/expr.h`

```c
// src/checker/expr.h — the expression checker.
#ifndef TK_CHECK_EXPR_H
#define TK_CHECK_EXPR_H

#include "type.h"
#include "scope.h"
#include "resolve.h"
#include "../parser/ast.h"   // tk_expr, tk_binary, tk_call, … (the parser's AST)

tk_type_result tk_check_expr(tk_expr e, tk_env env, tk_type_table table);

#endif // TK_CHECK_EXPR_H
```

### C23 — `src/checker/expr.c`

```c
// src/checker/expr.c
#include "expr.h"

static tk_type_result ok(tk_type t)        { return (tk_type_result){ .ok = true,  .as.value = t }; }
static tk_type_result err(const char *m)   { return (tk_type_result){ .ok = false, .as.error = tk_error_make(m) }; }
static tk_type      prim(tk_prim_kind k)   { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }

static bool is_bool(tk_type t)    { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t) { return t.tag == TK_TYPE_PRIM && t.as.prim != TK_PRIM_BOOL; }
static bool is_comparable(tk_type a, tk_type b) {
    if (is_integer(a) && is_integer(b)) return true;
    return tk_type_eq(&a, &b);
}
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}

// defined in Etapa 5 (they need blocks / statements):
tk_type_result tk_check_if(tk_if_expr f, tk_env env, tk_type_table table);
tk_type_result tk_check_match(tk_match_expr m, tk_env env, tk_type_table table);

static tk_type_result check_binary(tk_binary b, tk_env env, tk_type_table table) {
    tk_type_result l = tk_check_expr(*b.left,  env, table); if (!l.ok) return l;
    tk_type_result r = tk_check_expr(*b.right, env, table); if (!r.ok) return r;
    tk_type lt = l.as.value, rt = r.as.value;
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return err("shift needs integer operands");
        return ok(lt);
    }
    if (op_is_arith_bitwise(b.op)) {
        if (!is_integer(lt)) return err("arithmetic/bitwise needs an integer");
        if (!tk_type_eq(&lt, &rt)) return err("operands must be the same type (no promotion — B.22)");
        return ok(lt);
    }
    return err("not a binary operator");
}

static tk_type_result check_unary(tk_unary u, tk_env env, tk_type_table table) {
    tk_type_result o = tk_check_expr(*u.operand, env, table); if (!o.ok) return o;
    tk_type t = o.as.value;
    if (u.op == TK_TOKEN_MINUS || u.op == TK_TOKEN_TILDE) {
        if (!is_integer(t)) return err("unary -/~ needs an integer");
        return ok(t);
    }
    if (u.op == TK_TOKEN_BANG) {
        if (!is_bool(t)) return err("! needs a bool");
        return ok(t);
    }
    return err("not a unary operator");
}

static tk_type_result check_compare(tk_compare c, tk_env env, tk_type_table table) {
    tk_type_result f = tk_check_expr(*c.first, env, table); if (!f.ok) return f;
    tk_type prev = f.as.value;
    for (size_t i = 0; i < c.nrest; i += 1) {
        tk_type_result cur = tk_check_expr(c.rest[i].operand, env, table); if (!cur.ok) return cur;
        if (!is_comparable(prev, cur.as.value)) return err("operands are not comparable");
        prev = cur.as.value;
    }
    return ok(prim(TK_PRIM_BOOL));
}

static tk_type_result check_call(tk_call c, tk_env env, tk_type_table table) {
    tk_str name = c.callee.segments[c.callee.len - 1].name;
    tk_type_result ftr = tk_env_lookup(env, name); if (!ftr.ok) return ftr;
    tk_type ft = ftr.as.value;
    if (ft.tag != TK_TYPE_FUNC) return err("not a function");
    if (c.nargs != ft.as.func.nparams) return err("wrong number of arguments");
    for (size_t i = 0; i < c.nargs; i += 1) {
        tk_type_result at = tk_check_expr(c.args[i], env, table); if (!at.ok) return at;
        if (!tk_type_eq(&at.as.value, &ft.as.func.params[i])) return err("argument type mismatch");
    }
    return ok(*ft.as.func.ret);
}

tk_type_result tk_check_expr(tk_expr e, tk_env env, tk_type_table table) {
    switch (e.tag) {
        case TK_EXPR_NUMBER:  return ok(prim(TK_PRIM_I64));   // [literal typing: pending]
        case TK_EXPR_STR:     return ok((tk_type){ .tag = TK_TYPE_STR });
        case TK_EXPR_BYTE:    return ok((tk_type){ .tag = TK_TYPE_BYTE });
        case TK_EXPR_VAR:     return tk_env_lookup(env, e.as.var.name);
        case TK_EXPR_BINARY:  return check_binary(e.as.binary, env, table);
        case TK_EXPR_UNARY:   return check_unary(e.as.unary, env, table);
        case TK_EXPR_COMPARE: return check_compare(e.as.compare, env, table);
        case TK_EXPR_CALL:    return check_call(e.as.call, env, table);
        case TK_EXPR_IF:      return tk_check_if(e.as.if_expr, env, table);       // Etapa 5
        case TK_EXPR_MATCH:   return tk_check_match(e.as.match_expr, env, table); // Etapa 5
    }
    return err("unknown expression");
}
```

---
## Etapa 5a — statements e blocos

`check_statement` **valida** um statement e devolve o ambiente (estendido por
bindings); `check_block` encadeia, e ao voltar o chamador segura seu prefixo
(escopo de bloco, E2). O **valor** que um bloco rende (pra `if`/`match`) sai na E5b,
do último statement — aqui só validamos e passamos o ambiente.

> Adiados (refinamentos / etapas seguintes): a regra **`mut`** (B.21 — `Assign`
> exige alvo `mut`; precisa rastrear a mutabilidade do binding); `return` vs. o tipo
> de retorno da função (E5c); bindings por **destructuring**; o check do operando do
> operador composto (`+=`); `break`/`continue` só dentro de loop; e a tipagem de
> literal em contexto anotado (vinda da E4).

### Teko — `src/checker/stmt.tks`

```teko
// src/checker/stmt.tks  (namespace 'teko::checker')

// `let/mut/const TARGET [: T] = value` — validate the value; define the name.
fn check_binding(b: Binding, env: Env, table: TypeTable) -> Env | error {
    let vt = match check_expr(b.value, env, table) { Type as t => t; error as e => return e }
    mut bound = vt
    if b.has_type {
        let at = match resolve_type(b.type_ann, table) { Type as t => t; error as e => return e }
        if !type_eq(vt, at) { return error { message = "value type does not match annotation" } }
        bound = at
    }
    match b.target {
        SimpleName as sn   => define(env, sn.name, bound)
        DestructurePattern => env       // [destructuring binding: refinement]
    }
}

// `x = / += / … value` — target and value must match (B.4). [mut rule: refinement]
fn check_assign(a: Assign, env: Env, table: TypeTable) -> Env | error {
    let tt = match lookup(env, a.name) { Type as t => t; error as e => return e }
    let vt = match check_expr(a.value, env, table) { Type as t => t; error as e => return e }
    if !type_eq(tt, vt) { return error { message = "assigned value does not match the target type" } }
    env
}

// `return value` — validate the value. [vs the function's return type: E5c]
fn check_return(r: Return, env: Env, table: TypeTable) -> Env | error {
    match check_expr(r.value, env, table) { Type => env; error as e => return e }
}

// `loop { … }` — validate the body (the only primitive loop, M.5); env is unchanged
// outside (the body's bindings are block-local).
fn check_loop(l: LoopStmt, env: Env, table: TypeTable) -> Env | error {
    match check_block(l.body, env, table) { Env => env; error as e => return e }
}

// a bare expression statement — validate it.
fn check_exprstmt(es: ExprStmt, env: Env, table: TypeTable) -> Env | error {
    // a TOP-LEVEL if/match is a STATEMENT — its value is discarded, so no `else`
    // is required (the compiler's own code uses `if cond { … }` for early returns).
    match es.expr {
        IfExpr as f    => check_if_stmt(f, env, table)
        MatchExpr as m => check_match_stmt(m, env, table)
        _              => match check_expr(es.expr, env, table) { Type => env; error as e => return e }
    }
}

fn check_statement(s: Statement, env: Env, table: TypeTable) -> Env | error {
    match s {
        Binding as b   => check_binding(b, env, table)
        Assign as a    => check_assign(a, env, table)
        Return as r    => check_return(r, env, table)
        LoopStmt as l  => check_loop(l, env, table)
        BreakStmt      => env
        ContinueStmt   => env
        ExprStmt as es => check_exprstmt(es, env, table)
    }
}

// a block — validate each statement in order, threading the env.
fn check_block(stmts: []Statement, env: Env, table: TypeTable) -> Env | error {
    mut cur = env
    mut i = 0
    loop {
        if i >= stmts.len { break }
        cur = match check_statement(stmts[i], cur, table) { Env as e => e; error as err => return err }
        i++
    }
    cur
}
```

### C23 — `src/checker/stmt.h`

```c
// src/checker/stmt.h — statement & block checking.
#ifndef TK_CHECK_STMT_H
#define TK_CHECK_STMT_H

#include "expr.h"
TK_RESULT(tk_env, tk_env_result);   // Env | error

tk_env_result tk_check_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table);
tk_env_result tk_check_statement(tk_statement s, tk_env env, tk_type_table table);

#endif // TK_CHECK_STMT_H
```

### C23 — `src/checker/stmt.c`

```c
// src/checker/stmt.c
#include "stmt.h"

static tk_env_result eok(tk_env e)       { return (tk_env_result){ .ok = true,  .as.value = e }; }
static tk_env_result efail(tk_error e)   { return (tk_env_result){ .ok = false, .as.error = e }; }
static tk_env_result emsg(const char *m) { return efail(tk_error_make(m)); }

static tk_env_result check_binding(tk_binding b, tk_env env, tk_type_table table) {
    tk_type_result v = tk_check_expr(b.value, env, table); if (!v.ok) return efail(v.as.error);
    tk_type bound = v.as.value;
    if (b.has_type) {
        tk_type_result a = tk_resolve_type(b.type_ann, table); if (!a.ok) return efail(a.as.error);
        if (!tk_type_eq(&v.as.value, &a.as.value)) return emsg("value type does not match annotation");
        bound = a.as.value;
    }
    if (b.target.tag == TK_BIND_SIMPLE)
        return eok(tk_env_define(env, b.target.as.simple.name, bound));
    return eok(env);   // [destructuring binding: refinement]
}

static tk_env_result check_assign(tk_assign a, tk_env env, tk_type_table table) {
    tk_type_result tt = tk_env_lookup(env, a.name);        if (!tt.ok) return efail(tt.as.error);
    tk_type_result vt = tk_check_expr(a.value, env, table); if (!vt.ok) return efail(vt.as.error);
    if (!tk_type_eq(&tt.as.value, &vt.as.value)) return emsg("assigned value does not match the target type");
    return eok(env);   // [mut rule (B.21): refinement]
}

// from ctrl.c (E5b): STATEMENT-context if/match (no `else` needed).
tk_env_result tk_check_if_stmt(tk_if_expr f, tk_env env, tk_type_table table);
tk_env_result tk_check_match_stmt(tk_match_expr m, tk_env env, tk_type_table table);

static tk_env_result validate_expr(tk_expr e, tk_env env, tk_type_table table) {
    if (e.tag == TK_EXPR_IF)    return tk_check_if_stmt(e.as.if_expr, env, table);
    if (e.tag == TK_EXPR_MATCH) return tk_check_match_stmt(e.as.match_expr, env, table);
    tk_type_result r = tk_check_expr(e, env, table);
    return r.ok ? eok(env) : efail(r.as.error);
}

tk_env_result tk_check_statement(tk_statement s, tk_env env, tk_type_table table) {
    switch (s.tag) {
        case TK_STMT_BINDING:  return check_binding(s.as.binding, env, table);
        case TK_STMT_ASSIGN:   return check_assign(s.as.assign, env, table);
        case TK_STMT_RETURN:   return validate_expr(s.as.ret.value, env, table);   // [vs fn ret: E5c]
        case TK_STMT_LOOP: {
            tk_env_result r = tk_check_block(s.as.loop_stmt.body, s.as.loop_stmt.nbody, env, table);
            return r.ok ? eok(env) : r;        // body bindings stay block-local
        }
        case TK_STMT_BREAK:    return eok(env);
        case TK_STMT_CONTINUE: return eok(env);
        case TK_STMT_EXPR:     return validate_expr(s.as.expr_stmt.expr, env, table);
    }
    return emsg("unknown statement");
}

tk_env_result tk_check_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table) {
    tk_env cur = env;
    for (size_t i = 0; i < n; i += 1) {
        tk_env_result r = tk_check_statement(stmts[i], cur, table);
        if (!r.ok) return r;
        cur = r.as.value;
    }
    return eok(cur);
}
```

---
## Etapa 5b (1) — `if` e o valor de um bloco

`if` tem **dois modos**, por contexto: como **valor** (vindo de `check_expr`) exige
`else` e os dois ramos têm o **mesmo tipo** → esse é o tipo do `if`; como **statement**
(vindo de `check_exprstmt`) só valida os ramos, sem `else`. `block_type` dá o valor
de um bloco — o último statement, se for uma expressão; senão **`Unit`** (sem valor).
(O `match` — `check_match`/`check_match_stmt` + exaustividade — vem na E5b-2.)

### Teko — `src/checker/ctrl.tks`

```teko
// src/checker/ctrl.tks  (namespace 'teko::checker')

// the value a block yields: the last statement's, if it's an expression; else Unit.
// Threads the env through all but the last, then types the last in that env.
fn block_type(stmts: []Statement, env: Env, table: TypeTable) -> Type | error {
    if stmts.len == 0 { return Unit { } }
    mut cur = env
    mut i = 0
    loop {
        if i >= stmts.len - 1 { break }
        cur = match check_statement(stmts[i], cur, table) { Env as e => e; error as err => return err }
        i++
    }
    match stmts[stmts.len - 1] {
        ExprStmt as es => check_expr(es.expr, cur, table)        // the block's value
        _              => match check_statement(stmts[stmts.len - 1], cur, table) {
                              Env => Unit { }                    // a non-expr last → no value
                              error as e => return e
                          }
    }
}

// `if` as a VALUE (B.20): condition must be bool; `else` REQUIRED; both branches
// must have the same type — which is the `if`'s type.
fn check_if(f: IfExpr, env: Env, table: TypeTable) -> Type | error {
    let ct = match check_expr(f.cond, env, table) { Type as t => t; error as e => return e }
    if !is_bool(ct) { return error { message = "an `if` condition must be a bool" } }
    if !f.has_else { return error { message = "an `if` used as a value needs an `else`" } }
    let tt = match block_type(f.then_blk, env, table) { Type as t => t; error as e => return e }
    let et = match block_type(f.else_blk, env, table) { Type as t => t; error as e => return e }
    if !type_eq(tt, et) { return error { message = "the `if` branches have different types" } }
    tt
}

// `if` as a STATEMENT: condition must be bool; validate the branches; NO `else`
// required (its value is discarded). The env is unchanged outside (branch-local).
fn check_if_stmt(f: IfExpr, env: Env, table: TypeTable) -> Env | error {
    let ct = match check_expr(f.cond, env, table) { Type as t => t; error as e => return e }
    if !is_bool(ct) { return error { message = "an `if` condition must be a bool" } }
    let _ = match check_block(f.then_blk, env, table) { Env as e => e; error as err => return err }
    if f.has_else {
        let _ = match check_block(f.else_blk, env, table) { Env as e => e; error as err => return err }
    }
    env
}
```

### C23 — `src/checker/ctrl.h`

```c
// src/checker/ctrl.h — control-flow checking (if here; match in E5b-2).
#ifndef TK_CHECK_CTRL_H
#define TK_CHECK_CTRL_H

#include "stmt.h"

tk_type_result tk_block_type(tk_statement *stmts, size_t n, tk_env env, tk_type_table table);
tk_type_result tk_check_if(tk_if_expr f, tk_env env, tk_type_table table);       // value
tk_env_result  tk_check_if_stmt(tk_if_expr f, tk_env env, tk_type_table table);  // statement

#endif // TK_CHECK_CTRL_H
```

### C23 — `src/checker/ctrl.c`

```c
// src/checker/ctrl.c
#include "ctrl.h"

static tk_type        unit_type(void)      { return (tk_type){ .tag = TK_TYPE_UNIT }; }
static tk_type_result tok(tk_type t)       { return (tk_type_result){ .ok = true,  .as.value = t }; }
static tk_type_result terr(tk_error e)     { return (tk_type_result){ .ok = false, .as.error = e }; }
static tk_env_result  eok(tk_env e)        { return (tk_env_result){ .ok = true,  .as.value = e }; }
static tk_env_result  efail(tk_error e)    { return (tk_env_result){ .ok = false, .as.error = e }; }
static bool is_bool(tk_type t)             { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }

// the value a block yields: the last statement's, if an ExprStmt; else Unit.
tk_type_result tk_block_type(tk_statement *stmts, size_t n, tk_env env, tk_type_table table) {
    if (n == 0) return tok(unit_type());
    tk_env cur = env;
    for (size_t i = 0; i + 1 < n; i += 1) {          // all but the last
        tk_env_result r = tk_check_statement(stmts[i], cur, table);
        if (!r.ok) return terr(r.as.error);
        cur = r.as.value;
    }
    tk_statement last = stmts[n - 1];
    if (last.tag == TK_STMT_EXPR)
        return tk_check_expr(last.as.expr_stmt.expr, cur, table);
    tk_env_result r = tk_check_statement(last, cur, table);
    return r.ok ? tok(unit_type()) : terr(r.as.error);
}

tk_type_result tk_check_if(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_type_result c = tk_check_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value)) return terr(tk_error_make("an `if` condition must be a bool"));
    if (!f.has_else)          return terr(tk_error_make("an `if` used as a value needs an `else`"));
    tk_type_result t = tk_block_type(f.then_blk, f.nthen, env, table); if (!t.ok) return t;
    tk_type_result e = tk_block_type(f.else_blk, f.nelse, env, table); if (!e.ok) return e;
    if (!tk_type_eq(&t.as.value, &e.as.value)) return terr(tk_error_make("the `if` branches have different types"));
    return t;
}

tk_env_result tk_check_if_stmt(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_type_result c = tk_check_expr(*f.cond, env, table);
    if (!c.ok) return efail(c.as.error);
    if (!is_bool(c.as.value)) return efail(tk_error_make("an `if` condition must be a bool"));
    tk_env_result tr = tk_check_block(f.then_blk, f.nthen, env, table); if (!tr.ok) return tr;
    if (f.has_else) {
        tk_env_result er = tk_check_block(f.else_blk, f.nelse, env, table); if (!er.ok) return er;
    }
    return eok(env);
}
```

---
## Etapa 5b-2 — `match` e exaustividade (B.15)

`check_arm` checa um braço: o **padrão** estende o env, a guarda `when` é `bool`, o
**corpo** é checado no env estendido → seu tipo. `check_match` (valor) exige todos os
corpos do **mesmo tipo** → o tipo do `match`, e força **exaustividade** (B.15);
`check_match_stmt` (statement) valida sem tipo comum. A exaustividade: um `_` cobre
tudo; senão, num subject `Variant`, **todo caso** precisa de um braço que o nomeie.

> Gaps marcados: (a) **acesso a campo** (`x.campo`) não existe na AST/`check_expr`
> ainda — o compilador usa (`s.token`, `b.left`), então um nó `FieldAccess` + sua
> checagem são uma lacuna a fechar; (b) `check_pattern` cobre Wildcard/Bind/Literal —
> Field (binding dos campos), Range e Alt são refinamentos; (c) guardas `when` não
> contam pra exaustividade, e um `AltPattern` que cobre vários casos é refinamento.

### Teko — `src/checker/match.tks`

```teko
// src/checker/match.tks  (namespace 'teko::checker')

// validate a pattern against the subject; return the env extended with bindings.
fn check_pattern(p: Pattern, subject: Type, env: Env, table: TypeTable) -> Env | error {
    match p {
        WildcardPattern   => env
        BindPattern as bp => {
            let ct = match resolve_named(bp.type_name, table) { Type as t => t; error as e => return e }
            define(env, bp.binding, ct)            // `Type as name` → name : the case type
        }
        LiteralPattern as lp => {
            let lt = match check_expr(lp.value, env, table) { Type as t => t; error as e => return e }
            if !type_eq(lt, subject) { return error { message = "literal pattern does not match the subject type" } }
            env
        }
        FieldPattern => env       // [field-name binding: refinement]
        RangePattern => env       // [range pattern check: refinement]
        AltPattern   => env       // [alternatives: refinement]
    }
}

// one arm: pattern (extends env) + `when` guard (bool) + body (in the extended env).
fn check_arm(a: Arm, subject: Type, env: Env, table: TypeTable) -> Type | error {
    let e2 = match check_pattern(a.pattern, subject, env, table) { Env as e => e; error as err => return err }
    if a.has_when {
        let gt = match check_expr(a.guard, e2, table) { Type as t => t; error as err => return err }
        if !is_bool(gt) { return error { message = "a `when` guard must be a bool" } }
    }
    check_expr(a.body, e2, table)
}

// `match` as a VALUE: all arm bodies the SAME type → the match's type; EXHAUSTIVE.
fn check_match(m: MatchExpr, env: Env, table: TypeTable) -> Type | error {
    let st = match check_expr(m.subject, env, table) { Type as t => t; error as e => return e }
    if m.arms.len == 0 { return error { message = "a `match` needs at least one arm" } }
    let first = match check_arm(m.arms[0], st, env, table) { Type as t => t; error as e => return e }
    mut i = 1
    loop {
        if i >= m.arms.len { break }
        let bt = match check_arm(m.arms[i], st, env, table) { Type as t => t; error as e => return e }
        if !type_eq(bt, first) { return error { message = "the `match` arms have different types" } }
        i++
    }
    if !exhaustive(m.arms, st) { return error { message = "non-exhaustive `match` (cover all cases or add `_`)" } }
    first
}

// `match` as a STATEMENT: validate the arms; exhaustiveness still forced (B.15).
fn check_match_stmt(m: MatchExpr, env: Env, table: TypeTable) -> Env | error {
    let st = match check_expr(m.subject, env, table) { Type as t => t; error as e => return e }
    mut i = 0
    loop {
        if i >= m.arms.len { break }
        let _ = match check_arm(m.arms[i], st, env, table) { Type as t => t; error as e => return e }
        i++
    }
    if !exhaustive(m.arms, st) { return error { message = "non-exhaustive `match` (cover all cases or add `_`)" } }
    env
}

// --- exhaustiveness (B.15) ---

fn has_wildcard(arms: []Arm) -> bool {
    mut i = 0
    loop {
        if i >= arms.len { break }
        match arms[i].pattern { WildcardPattern => return true; _ => {} }
        i++
    }
    false
}

// the case name a variant pattern selects (Bind/Field), or "" otherwise.
fn arm_case_name(p: Pattern) -> str {
    match p {
        BindPattern as bp  => bp.type_name.segments[bp.type_name.segments.len - 1].name
        FieldPattern as fp => fp.type_name.segments[fp.type_name.segments.len - 1].name
        _                  => ""
    }
}

fn some_arm_names(arms: []Arm, name: str) -> bool {
    mut i = 0
    loop {
        if i >= arms.len { break }
        if arm_case_name(arms[i].pattern) == name { return true }
        i++
    }
    false
}

// every member of the variant must be named by some arm.
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
// src/checker/match.h
#ifndef TK_CHECK_MATCH_H
#define TK_CHECK_MATCH_H
#include "ctrl.h"
tk_type_result tk_check_match(tk_match_expr m, tk_env env, tk_type_table table);       // value
tk_env_result  tk_check_match_stmt(tk_match_expr m, tk_env env, tk_type_table table);  // statement
#endif // TK_CHECK_MATCH_H
```

```c
// src/checker/match.c
#include "match.h"

static tk_type_result tok(tk_type t)    { return (tk_type_result){ .ok = true,  .as.value = t }; }
static tk_type_result terr(tk_error e)  { return (tk_type_result){ .ok = false, .as.error = e }; }
static tk_env_result  eok(tk_env e)     { return (tk_env_result){ .ok = true,  .as.value = e }; }
static tk_env_result  efail(tk_error e) { return (tk_env_result){ .ok = false, .as.error = e }; }
static bool is_bool(tk_type t)          { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }

// env | error: validate a pattern, extend the env.
static tk_env_result check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table) {
    switch (p.tag) {
        case TK_PAT_WILDCARD: return eok(env);
        case TK_PAT_BIND: {
            tk_type_result ct = resolve_named_c(p.as.bind.type_name, table); // see resolve.c
            if (!ct.ok) return efail(ct.as.error);
            return eok(tk_env_define(env, p.as.bind.binding, ct.as.value));
        }
        case TK_PAT_LITERAL: {
            tk_type_result lt = tk_check_expr(p.as.literal.value, env, table);
            if (!lt.ok) return efail(lt.as.error);
            if (!tk_type_eq(&lt.as.value, &subject)) return efail(tk_error_make("literal pattern does not match the subject type"));
            return eok(env);
        }
        default: return eok(env);   // Field/Range/Alt: refinements
    }
}

static tk_type_result check_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table) {
    tk_env_result e2 = check_pattern(a.pattern, subject, env, table); if (!e2.ok) return terr(e2.as.error);
    if (a.has_when) {
        tk_type_result g = tk_check_expr(a.guard, e2.as.value, table); if (!g.ok) return g;
        if (!is_bool(g.as.value)) return terr(tk_error_make("a `when` guard must be a bool"));
    }
    return tk_check_expr(a.body, e2.as.value, table);
}

static bool has_wildcard(tk_arm *arms, size_t n) {
    for (size_t i = 0; i < n; i += 1) if (arms[i].pattern.tag == TK_PAT_WILDCARD) return true;
    return false;
}
static tk_str case_name(tk_pattern p) {
    if (p.tag == TK_PAT_BIND)  return p.as.bind.type_name.segments[p.as.bind.type_name.len - 1].name;
    if (p.tag == TK_PAT_FIELD) return p.as.field.type_name.segments[p.as.field.type_name.len - 1].name;
    return (tk_str){ NULL, 0 };
}
static bool name_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }
static bool some_arm_names(tk_arm *arms, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1) if (name_eq(case_name(arms[i].pattern), name)) return true;
    return false;
}
static bool exhaustive(tk_arm *arms, size_t n, tk_type subject) {
    if (has_wildcard(arms, n)) return true;
    if (subject.tag != TK_TYPE_VARIANT) return false;
    for (size_t i = 0; i < subject.as.variant.len; i += 1) {
        tk_type mem = subject.as.variant.members[i];
        if (mem.tag != TK_TYPE_NAMED) return false;
        if (!some_arm_names(arms, n, mem.as.named.name)) return false;
    }
    return true;
}

tk_type_result tk_check_match(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_type_result s = tk_check_expr(*m.subject, env, table); if (!s.ok) return s;
    if (m.narms == 0) return terr(tk_error_make("a `match` needs at least one arm"));
    tk_type_result first = check_arm(m.arms[0], s.as.value, env, table); if (!first.ok) return first;
    for (size_t i = 1; i < m.narms; i += 1) {
        tk_type_result b = check_arm(m.arms[i], s.as.value, env, table); if (!b.ok) return b;
        if (!tk_type_eq(&b.as.value, &first.as.value)) return terr(tk_error_make("the `match` arms have different types"));
    }
    if (!exhaustive(m.arms, m.narms, s.as.value)) return terr(tk_error_make("non-exhaustive `match` (cover all cases or add `_`)"));
    return first;
}

tk_env_result tk_check_match_stmt(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_type_result s = tk_check_expr(*m.subject, env, table); if (!s.ok) return efail(s.as.error);
    for (size_t i = 0; i < m.narms; i += 1) {
        tk_type_result a = check_arm(m.arms[i], s.as.value, env, table); if (!a.ok) return efail(a.as.error);
    }
    if (!exhaustive(m.arms, m.narms, s.as.value)) return efail(tk_error_make("non-exhaustive `match` (cover all cases or add `_`)"));
    return eok(env);
}
```

---
## Etapa 5c — o driver (`check_function` / `check_item` / `check_program`)

`check_program` é a **entrada** do checker: roda o **passe 1** (`collect`, E3) e
depois checa cada item contra o ambiente + tipos coletados. `check_item` despacha;
`check_function` traz os params pro ambiente e valida o corpo. Com isso o checker
checa um programa **de ponta a ponta** (modulo os refinamentos marcados). O sucesso
é `Unit`; a falha é `error`.

> Adiados (refinamentos): (a) **`return` vs. o tipo de retorno** da função — precisa
> fiar o tipo esperado por `check_block`/`check_return` (sem isso, o valor do `return`
> é validado, mas não contra o retorno); (b) a regra **`mut`** (B.21) — precisa do
> `is_mut` no binding (cascata em `ValBinding`/`lookup`); (c) re-validar o corpo de um
> `TypeDecl` (os tipos dos campos já resolvem no `collect`); (d) o gap de **acesso a
> campo** (`x.campo`), que ainda falta na AST/`check_expr`.

### Teko — `src/checker/check.tks`

```teko
// src/checker/check.tks  (namespace 'teko::checker')

// a function: bring the params into the env (immutable — B.21), validate the body.
fn check_function(f: Function, env: Env, table: TypeTable) -> Unit | error {
    mut local = env
    mut i = 0
    loop {
        if i >= f.params.len { break }
        let pt = match resolve_type(f.params[i].type_ann, table) { Type as t => t; error as e => return e }
        local = define(local, f.params[i].name, pt)
        i++
    }
    // [return-consistency vs f.return_type: refinement (needs the expected type threaded)]
    let _ = match check_block(f.body, local, table) { Env as e => e; error as err => return err }
    Unit { }
}

// a top-level item.
fn check_item(item: Item, env: Env, table: TypeTable) -> Unit | error {
    match item {
        Function as f  => check_function(f, env, table)
        TypeDecl       => Unit { }     // [re-validate the body: refinement (collect resolved it)]
        UseDecl        => Unit { }     // [use resolution: seed no-op]
        Statement as s => { match check_statement(s, env, table) { Env => Unit { }; error as e => return e } }
    }
}

// THE ENTRY POINT: pass 1 (collect top-level names) + pass 2 (check each item).
fn check_program(program: Program) -> Unit | error {
    let c = match collect(program) { Collected as col => col; error as e => return e }
    mut i = 0
    loop {
        if i >= program.items.len { break }
        let _ = match check_item(program.items[i], c.env, c.types) { Unit as u => u; error as e => return e }
        i++
    }
    Unit { }
}
```

### C23 — `src/checker/check.h`

```c
// src/checker/check.h — the checker's top-level driver (the entry point).
#ifndef TK_CHECK_H
#define TK_CHECK_H

#include "match.h"
#include "collect.h"

// Unit | error — `ok` on success; `error` is valid iff `!ok`.
typedef struct { bool ok; tk_error error; } tk_check_result;

tk_check_result tk_check_function(tk_function f, tk_env env, tk_type_table table);
tk_check_result tk_check_item(tk_item item, tk_env env, tk_type_table table);
tk_check_result tk_check_program(tk_program program);

#endif // TK_CHECK_H
```

### C23 — `src/checker/check.c`

```c
// src/checker/check.c
#include "check.h"

static tk_check_result cok(void)        { return (tk_check_result){ .ok = true }; }
static tk_check_result cfail(tk_error e) { return (tk_check_result){ .ok = false, .error = e }; }

tk_check_result tk_check_function(tk_function f, tk_env env, tk_type_table table) {
    tk_env local = env;
    for (size_t i = 0; i < f.nparams; i += 1) {           // params are immutable (B.21)
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) return cfail(pt.as.error);
        local = tk_env_define(local, f.params[i].name, pt.as.value);
    }
    // [return-consistency vs f.return_type: refinement]
    tk_env_result b = tk_check_block(f.body, f.nbody, local, table);
    if (!b.ok) return cfail(b.as.error);
    return cok();
}

tk_check_result tk_check_item(tk_item item, tk_env env, tk_type_table table) {
    switch (item.tag) {
        case TK_ITEM_FUNCTION: return tk_check_function(item.as.function, env, table);
        case TK_ITEM_TYPE_DECL: return cok();   // [re-validate body: refinement]
        case TK_ITEM_USE:       return cok();   // [use resolution: seed no-op]
        case TK_ITEM_STATEMENT: {
            tk_env_result r = tk_check_statement(item.as.statement, env, table);
            return r.ok ? cok() : cfail(r.as.error);
        }
    }
    return cfail(tk_error_make("unknown item"));
}

// THE ENTRY POINT.
tk_check_result tk_check_program(tk_program program) {
    tk_collected_result c = tk_collect(program);
    if (!c.ok) return cfail(c.as.error);
    for (size_t i = 0; i < program.len; i += 1) {
        tk_check_result r = tk_check_item(program.items[i], c.as.value.env, c.as.value.types);
        if (!r.ok) return r;
    }
    return cok();
}
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
seu tipo e tem filhos tipados). Abaixo a estrutura e o padrão (representativo:
`type_binary`); aplicar o padrão a todos os nós + statements/itens é a conclusão
mecânica.

### Teko — `src/checker/tast.tks`

```teko
// src/checker/tast.tks  (namespace 'teko::checker') — the TYPED AST.

// a typed expression: a kind + this node's resolved type. Children are TExpr too
// (the whole tree is typed — no gaps).
type TExpr = struct {
    kind: TExprKind
    type: Type
}

// the typed kinds — mirror Expr (Etapa 1 do parser), but children are TExpr.
type TNumber  = struct { value: i64 }
type TVar     = struct { name: str }
type TStrLit  = struct { text: str }
type TByteLit = struct { value: byte }
type TBinary  = struct { op: lexer::TokenKind; left: TExpr; right: TExpr }
type TUnary   = struct { op: lexer::TokenKind; operand: TExpr }
type TCmpTerm = struct { op: lexer::TokenKind; operand: TExpr }
type TCompare = struct { first: TExpr; rest: []TCmpTerm }
type TCall    = struct { callee: Path; args: []TExpr }
// TIfExpr / TMatchExpr mirror IfExpr/MatchExpr with TYPED blocks/arms (the
// block-bearing ones) — same shape, TExpr inside.

type TExprKind = TNumber | TVar | TStrLit | TByteLit | TBinary | TUnary | TCompare | TCall
```

### Teko — `src/checker/typer.tks` (os `check_*` viram `type_*`)

```teko
// src/checker/typer.tks  (namespace 'teko::checker')

// each validator becomes a PRODUCER: the same checks, now building the typed node.
// (Representative: a leaf and a binary; the rest follow the same pattern.)

fn type_number(n: Number) -> TExpr {
    TExpr { kind = TNumber { value = n.value }; type = Prim { kind = PrimKind::I64 } }  // [literal typing: pending]
}

fn type_var(v: Var, env: Env, table: TypeTable) -> TExpr | error {
    let t = match lookup(env, v.name) { Type as ty => ty; error as e => return e }
    TExpr { kind = TVar { name = v.name }; type = t }
}

// the SAME B.22 logic as check_binary — now it also wires the typed children.
fn type_binary(b: Binary, env: Env, table: TypeTable) -> TExpr | error {
    let l = match type_expr(b.left, env, table)  { TExpr as te => te; error as e => return e }
    let r = match type_expr(b.right, env, table) { TExpr as te => te; error as e => return e }
    mut t = l.type
    if op_is_shift(b.op) {
        if !is_integer(l.type) || !is_integer(r.type) { return error { message = "shift needs integer operands" } }
    } else if op_is_arith_bitwise(b.op) {
        if !is_integer(l.type) { return error { message = "arithmetic/bitwise needs an integer" } }
        if !type_eq(l.type, r.type) { return error { message = "operands must be the same type (no promotion — B.22)" } }
    } else {
        return error { message = "not a binary operator" }
    }
    TExpr { kind = TBinary { op = b.op; left = l; right = r }; type = t }
}

// the dispatch (the evolved check_expr). Leaf + operators shown; Call/If/Match
// follow the same evolution.
fn type_expr(e: Expr, env: Env, table: TypeTable) -> TExpr | error {
    match e {
        Number as n  => type_number(n)
        Var as v     => type_var(v, env, table)
        Binary as b  => type_binary(b, env, table)
        // StrLit/ByteLit/Unary/Compare/Call/IfExpr/MatchExpr: same pattern …
        _            => error { message = "typer: node pending evolution" }
    }
}
```

### C23 — `src/checker/tast.h`

```c
// src/checker/tast.h — the TYPED AST.
#ifndef TK_CHECK_TAST_H
#define TK_CHECK_TAST_H

#include "type.h"
#include "../parser/ast.h"   // tk_path, tk_token_kind via the parser's AST

typedef struct tk_texpr tk_texpr;     // recursive (children are tk_texpr*)

typedef enum {
    TK_TEXPR_NUMBER, TK_TEXPR_VAR, TK_TEXPR_STR, TK_TEXPR_BYTE,
    TK_TEXPR_BINARY, TK_TEXPR_UNARY, TK_TEXPR_COMPARE, TK_TEXPR_CALL,
} tk_texpr_tag;

typedef struct { tk_token_kind op; tk_texpr *operand; } tk_tcmp_term;

struct tk_texpr {
    tk_texpr_tag tag;
    tk_type      type;        // this node's resolved type
    union {
        struct { int64_t value; }                              number;
        struct { tk_str name; }                                var;
        struct { tk_str text; }                                str;
        struct { tk_byte value; }                              byte;
        struct { tk_token_kind op; tk_texpr *left, *right; }   binary;
        struct { tk_token_kind op; tk_texpr *operand; }        unary;
        struct { tk_texpr *first; tk_tcmp_term *rest; size_t nrest; } compare;
        struct { tk_path callee; tk_texpr *args; size_t nargs; }      call;
    } as;
};

#endif // TK_CHECK_TAST_H
```

### C23 — `src/checker/typer.c` (representativo)

```c
// src/checker/typer.c
#include "tast.h"
#include "expr.h"   // the predicates op_is_shift, etc. (or re-share)

static tk_texpr *box(tk_texpr t) { tk_texpr *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }

tk_type_result_texpr tk_type_expr(tk_expr e, tk_env env, tk_type_table table);  // TExpr | error

// (Representative: binary. The same B.22 checks as check_binary, now building the node.)
static tk_type_result_texpr type_binary(tk_binary b, tk_env env, tk_type_table table) {
    tk_type_result_texpr l = tk_type_expr(*b.left,  env, table); if (!l.ok) return l;
    tk_type_result_texpr r = tk_type_expr(*b.right, env, table); if (!r.ok) return r;
    tk_type lt = l.as.value.type, rt = r.as.value.type;
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return texpr_err("shift needs integer operands");
    } else if (op_is_arith_bitwise(b.op)) {
        if (!is_integer(lt)) return texpr_err("arithmetic/bitwise needs an integer");
        if (!tk_type_eq(&lt, &rt)) return texpr_err("operands must be the same type (no promotion — B.22)");
    } else {
        return texpr_err("not a binary operator");
    }
    tk_texpr node = { .tag = TK_TEXPR_BINARY, .type = lt,
                      .as.binary = { b.op, box(l.as.value), box(r.as.value) } };
    return texpr_ok(node);
}
```

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
fn check_node_type(stored: Type, expected: Type) -> Unit | error {
    if type_eq(stored, expected) { return Unit { } }
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

fn validate_each(xs: []TExpr) -> Unit | error {
    mut i = 0
    loop {
        if i >= xs.len { break }
        let _ = match validate_texpr(xs[i]) { Unit as u => u; error as e => return e }
        i++
    }
    Unit { }
}

// COUNTER-VALIDATE a typed expression: re-derive its type and confirm it matches
// the stored one (operators/literals), or check it structurally (env-dependent).
fn validate_texpr(te: TExpr) -> Unit | error {
    match te.kind {
        TNumber  => check_node_type(te.type, Prim { kind = PrimKind::I64 })
        TStrLit  => check_node_type(te.type, Str { })
        TByteLit => check_node_type(te.type, Byte { })
        TVar     => Unit { }                       // env-dependent → trust (type is present)
        TCall as c => validate_each(c.args)        // [callee re-derivation is env-dependent]
        TBinary as b => {
            let _ = match validate_texpr(b.left)  { Unit as u => u; error as e => return e }
            let _ = match validate_texpr(b.right) { Unit as u => u; error as e => return e }
            let d = match rederive_binary(b.left.type, b.right.type, b.op) { Type as t => t; error as e => return e }
            check_node_type(te.type, d)
        }
        TUnary as u => {
            let _ = match validate_texpr(u.operand) { Unit as u2 => u2; error as e => return e }
            if u.op == lexer::TokenKind::Bang {
                if !is_bool(u.operand.type) { return error { message = "corrupt: `!` operand not bool" } }
                return check_node_type(te.type, Prim { kind = PrimKind::Bool })
            }
            if !is_integer(u.operand.type) { return error { message = "corrupt: `-`/`~` operand not integer" } }
            check_node_type(te.type, u.operand.type)
        }
        TCompare as cmp => {
            let _ = match validate_texpr(cmp.first) { Unit as u => u; error as e => return e }
            mut i = 0
            loop {
                if i >= cmp.rest.len { break }
                let _ = match validate_texpr(cmp.rest[i].operand) { Unit as u => u; error as e => return e }
                i++
            }
            check_node_type(te.type, Prim { kind = PrimKind::Bool })   // a comparison is bool
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

// Unit | error: ok if the subtree's stored types match their derivations.
tk_check_result tk_validate_texpr(const tk_texpr *te);

#endif // TK_CHECK_REVALIDATE_H
```

### C23 — `src/checker/revalidate.c`

```c
// src/checker/revalidate.c
#include "revalidate.h"
#include "check.h"   // tk_check_result

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

> Um ponto aberto: as conversões inteiras `byte(…)` / `u32(…)` / `u64(…)`
> (truncar/alargar/reinterpretar bits) — a sintaxe exata do cast fica TBD, **mesmo
> ponto aberto do `str(b)`**. No C são casts diretos.

### Teko — `src/emit/tkb_buf.tks`

```teko
// src/emit/tkb_buf.tks  (namespace 'teko::emit')

// extract the low 8 bits as a byte. [integer cast syntax TBD — see note above]
fn lo8(x: u32) -> byte { byte(x & 0xFF) }

fn write_u8(buf: []byte, x: byte) -> []byte { teko::list::push(buf, x) }

fn write_u32(buf: []byte, x: u32) -> []byte {
    mut b = buf
    b = teko::list::push(b, lo8(x))
    b = teko::list::push(b, lo8(x >> 8))
    b = teko::list::push(b, lo8(x >> 16))
    b = teko::list::push(b, lo8(x >> 24))
    b
}

// an i64 as 8 LE bytes (its two's-complement bits, reinterpreted as u64).
fn write_i64(buf: []byte, x: i64) -> []byte {
    mut b = buf
    mut bits = u64(x)                  // [reinterpret cast TBD]
    mut k = 0
    loop {
        if k >= 8 { break }
        b = teko::list::push(b, lo8(u32(bits & 0xFF)))
        bits = bits >> 8
        k++
    }
    b
}

// a length-prefixed byte run (used by the string table).
fn write_bytes(buf: []byte, s: str) -> []byte {
    mut b = write_u32(buf, u32(s.len))
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
        if t.strings[i] == s { return u32(i) }
        i++
    }
    0xFFFFFFFF                          // sentinel: not found
}

// intern s → its index, adding it if new (dedup = exact round-trip + compact).
fn st_intern(t: StrTable, s: str) -> Interned {
    let found = st_find(t, s)
    if found != 0xFFFFFFFF { return Interned { table = t; index = found } }
    let idx = u32(t.strings.len)
    Interned { table = StrTable { strings = teko::list::push(t.strings, s) }; index = idx }
}

// serialize the whole string table into the buffer (count, then each string).
fn write_strtable(buf: []byte, t: StrTable) -> []byte {
    mut b = write_u32(buf, u32(t.strings.len))
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
        h = h ^ u64(data[i])           // [widening cast TBD]
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
        Error        => write_u8(buf, 3)
        Unit         => write_u8(buf, 4)
        Slice as s   => write_type(write_u8(buf, 5), t, s.element)
        Named as n   => write_u32(write_u8(buf, 6), st_find(t, n.name))
        Variant as v => write_types(write_u8(buf, 7), t, v.members)
        Func as f    => write_type(write_types(write_u8(buf, 8), t, f.params), t, f.ret)
    }
}

fn write_types(buf: []byte, t: StrTable, xs: []Type) -> []byte {
    mut b = write_u32(buf, u32(xs.len))
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
    mut b = write_u32(buf, u32(p.segments.len))
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
    mut b = write_u32(buf, u32(xs.len))
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
    mut b = write_u32(buf, u32(ts.len))
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
        case TK_TYPE_ERROR:   return tk_write_u8(b, 3);
        case TK_TYPE_UNIT:    return tk_write_u8(b, 4);
        case TK_TYPE_SLICE:   return tk_write_type(tk_write_u8(b, 5), t, *ty.as.slice.element);
        case TK_TYPE_NAMED:   return tk_write_u32(tk_write_u8(b, 6), tk_st_find(t, ty.as.named.name));
        case TK_TYPE_VARIANT: return write_types(tk_write_u8(b, 7), t, ty.as.variant.members, ty.as.variant.len);
        case TK_TYPE_FUNC:
            b = write_types(tk_write_u8(b, 8), t, ty.as.func.params, ty.as.func.nparams);
            return tk_write_type(b, t, *ty.as.func.ret);
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
        b = teko::list::push(b, lo8(u32(v & 0xFF)))   // [integer casts → E7]
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
    RU32 { r = d.r; value = u32(a.value) | (u32(b.value) << 8) | (u32(c.value) << 16) | (u32(d.value) << 24) }  // [casts → E7]
}

fn read_u64(r: Reader) -> RU64 | error {
    let lo = match read_u32(r)    { RU32 as x => x; error as e => return e }
    let hi = match read_u32(lo.r) { RU32 as x => x; error as e => return e }
    RU64 { r = hi.r; value = u64(lo.value) | (u64(hi.value) << 32) }                                          // [casts → E7]
}

// a string reference = a u32 index into the (already-read) table.
fn read_str(r: Reader, table: []str) -> RStr | error {
    let idx = match read_u32(r) { RU32 as x => x; error as e => return e }
    if u64(idx.value) >= table.len { return error { message = "bad string index in .tkb" } }
    RStr { r = idx.r; value = table[idx.value] }
}

// the string table: count, then each (len + bytes).
fn read_strtable(r: Reader) -> RTable | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut table = teko::list::empty()
    mut i = 0
    loop {
        if i >= u64(n.value) { break }
        let len = match read_u32(rr) { RU32 as x => x; error as e => return e }
        mut bytes = teko::list::empty()
        mut br = len.r
        mut j = 0
        loop {
            if j >= u64(len.value) { break }
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
        if i >= u64(n.value) { break }
        let t = match read_type(rr, table) { RType as x => x; error as e => return e }
        xs = teko::list::push(xs, t.value)
        rr = t.r
        i++
    }
    RTypes { r = rr; value = xs }
}

// inverse of write_type — tags 0=Prim 1=Byte 2=Str 3=error 4=Unit 5=Slice 6=Named 7=Variant 8=Func.
fn read_type(r: Reader, table: []str) -> RType | error {
    let tag = match read_u8(r) { RByte as x => x; error as e => return e }
    if tag.value == 0 {
        let k = match read_u8(tag.r) { RByte as x => x; error as e => return e }
        return RType { r = k.r; value = Prim { kind = prim_of(k.value) } }
    }
    if tag.value == 1 { return RType { r = tag.r; value = Byte { } } }
    if tag.value == 2 { return RType { r = tag.r; value = Str { } } }
    if tag.value == 3 { return RType { r = tag.r; value = error { } } }
    if tag.value == 4 { return RType { r = tag.r; value = Unit { } } }
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
        case 4: return (tk_type){ .tag = TK_TYPE_UNIT };
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
    }
    r->ok = false; return (tk_type){ .tag = TK_TYPE_UNIT };
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
    RI64 { r = u.r; value = i64(u.value) }                 // u64→i64 reinterpret [cast → E7]
}

// inverse of kind_byte (operator TokenKind ↔ byte). The byte→enum ordinal is E7's cast.
fn kind_of(b: byte) -> lexer::TokenKind { /* E7: the byte → operator-TokenKind */ }

fn read_path(r: Reader, table: []str) -> RPath | error {
    let n = match read_u32(r) { RU32 as x => x; error as e => return e }
    mut rr = n.r
    mut segs = teko::list::empty()
    mut i = 0
    loop {
        if i >= u64(n.value) { break }
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
        if i >= u64(n.value) { break }
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
        if i >= u64(n.value) { break }
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
TK_RESULT(tk_texpr, tk_texpr_result);
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
    mut b = write_u32(buf, u32(xs.len))
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
    mut b = write_u32(buf, u32(xs.len))
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
    mut b = write_u32(buf, u32(xs.len))
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
    mut b = write_u32(buf, u32(xs.len))
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
    mut b = write_u32(buf, u32(xs.len))
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
        if i >= u64(n.value) { break }
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
        if i >= u64(n.value) { break }
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
        if i >= u64(n.value) { break }
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
        if i >= u64(n.value) { break }
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
        if i >= u64(n.value) { break }
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
- **Refinamentos do checker** (marcados pelas etapas): concluir a evolução
  `check_*` → `type_*` (todos os nós/statements/itens); a regra `mut` (B.21, precisa
  de `is_mut`); `return` vs. o tipo de retorno; o nó **`FieldAccess`** (`x.campo`) na
  AST/parser/`check_expr` — gap real; tipagem de literal anotado; padrões
  `Field`/`Range`/`Alt`.
- **Export de método (B.29) — diferido** junto com a checagem de método: quando o
  checker fixar como um método (1º arg `self` solto) é checado e associado ao seu tipo,
  o `.tkh` ganha a forma de export correspondente (não antes — **M.4**).