#include "teko_sdk.h"
#include "parser_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h> // For access()

static const VirtualSDKEntry virtual_sdk_table[] = {
    {"teko::marshall.to_ptr", "ptr"},
    {"teko::marshall.from_ptr", "void"},
    {"teko::marshall.transmute", "void"},
    {"teko::flows.request", "intent"},
    {"teko::flows.notify", "i32"},
    {"teko::flows.send", "intent"},
    {"teko::strings.concat", "str"},
    {"teko::strings.slice", "str"},
    {"teko::strings.from_utf8", "str"},

    // NEW MAPPINGS ADDED:
    {"teko::fs.read_to_string", "str"},
    {"teko::fs.write_string", "void"},
    {"teko::fs.close", "void"},
    {"teko::lists.append", "void"},
    {"teko::lists.len", "i32"},
    {"teko::lists.pop", "void"},
    {"teko::logger.info", "void"},
    {"teko::logger.error", "void"},
    {"teko::sync.sleep_ms", "intent"},
    {"teko::sync.yield_now", "void"},

    {NULL, NULL} // End-of-table sentinel
};

// Physically validates the presence of the SDK's segregated directory
bool teko_sdk_verify_directory(const char* project_root) {
    char sdk_path[512];
    snprintf(sdk_path, sizeof(sdk_path), "%s/sdk/teko.tkp", project_root ? project_root : ".");
    return (access(sdk_path, F_OK) == 0);
}

void teko_sdk_inject_builtins(SymbolTableScope* global_scope) {
    if (!global_scope) return;

    int i = 0;
    while (virtual_sdk_table[i].intrinsic_name != NULL) {
        TypeInfo* type_info = (TypeInfo*)malloc(sizeof(TypeInfo));
        if (type_info) {
            type_info->kind = NODE_TYPE_BASIC;
            type_info->base_name = strdup(virtual_sdk_table[i].return_type_name);
            type_info->is_nullable = false;
            type_info->is_array = false;
            type_info->is_array_elem_mut = false;
            type_info->file_mode = NULL;
            type_info->generic_params = NULL;
            type_info->generic_param_count = 0;

            symbol_table_insert(global_scope,
                                virtual_sdk_table[i].intrinsic_name,
                                SYM_FUNCTION,
                                type_info,
                                false);
        }
        i++;
    }
}

bool teko_sdk_is_reserved_intrinsic(const char* name) {
    if (!name) return false;

    int i = 0;
    while (virtual_sdk_table[i].intrinsic_name != NULL) {
        if (strcmp(virtual_sdk_table[i].intrinsic_name, name) == 0) {
            return true;
        }
        i++;
    }
    return false;
}