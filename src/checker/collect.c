// src/checker/collect.c
#include "collect.h"

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
