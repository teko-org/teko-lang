// src/checker/collect.c
#include "collect.h"
#include "tast.h"   // tk_tprogram, tk_titem, TK_TITEM_TYPE_DECL (C7.12)

static tk_type_table collect_types(tk_item *items, size_t n) {
    tk_type_table table = tk_type_table_empty();
    for (size_t i = 0; i < n; i += 1) {
        if (items[i].tag == TK_ITEM_TYPE_DECL) {
            tk_type_decl td = items[i].as.type_decl;
            // tag the reg with the declaring namespace (A3 provenance on the item) + the
            // decl's visibility — the inputs to W-vis-enforce's module-system pass.
            table = tk_type_table_push(table, (tk_type_reg){
                .name = td.name, .namespace = items[i].namespace, .vis = td.vis, .decl = td });
        }
    }
    return table;
}

static tk_type_result func_type(tk_function f, tk_type_table table) {
    tk_type *params = NULL; size_t n = 0;
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) { tk_free0(params); return pt; }
        params = tk_realloc0(params, (n + 1) * sizeof *params);
        if (!params) abort();
        params[n] = pt.as.value; n += 1;
    }
    // No `-> ret` ⇒ a void return (M.3) — f.return_type is the empty `no_type()` placeholder
    // (a NAMED type-expr with an empty path), which must NOT be resolved (it is not a value type).
    // Mirrors typer.c's function typer (`if (!f.has_return) return void_t()`).
    tk_type_result ret = f.has_return
        ? tk_resolve_type(f.return_type, table)
        : (tk_type_result){ .ok = true, .as.value = (tk_type){ .tag = TK_TYPE_VOID } };
    if (!ret.ok) { tk_free0(params); return ret; }
    tk_type *rp = tk_alloc(sizeof *rp); if (!rp) abort(); *rp = ret.as.value;
    tk_type t = { .tag = TK_TYPE_FUNC, .as.func = { params, n, rp } };
    return (tk_type_result){ .ok = true, .as.value = t };
}

// C7.12 — reconstruct a TypeTable from a typed program's pass-through TypeDecl items.
// Used by the package backend in driver.c. The namespace is set to "" — sufficient for
// resolve_type; W-vis-enforce is not invoked by the header emitter.
tk_type_table tk_type_table_of(tk_tprogram prog) {
    tk_type_table table = tk_type_table_empty();
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag == TK_TITEM_TYPE_DECL) {
            tk_type_decl td = prog.items[i].as.type_decl;
            tk_str empty = { .ptr = (const unsigned char *)"", .len = 0 };
            table = tk_type_table_push(table, (tk_type_reg){
                .name = td.name, .namespace = empty, .vis = td.vis, .decl = td });
        }
    }
    return table;
}

tk_collected_result tk_collect(tk_program program) {
    tk_type_table table = collect_types(program.items, program.len);
    tk_env env = tk_env_empty();
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_FUNCTION) {
            tk_function f = program.items[i].as.function;
            tk_type_result ft = func_type(f, table);
            if (!ft.ok) return (tk_collected_result){ .ok = false, .as.error = ft.as.error };
            env = tk_env_define_fn(env, f.name, ft.as.value, program.items[i].namespace);   // #41: carry the fn's ns
        }
    }
    tk_collected c = { .types = table, .env = env };
    return (tk_collected_result){ .ok = true, .as.value = c };
}
