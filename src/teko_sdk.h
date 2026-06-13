#ifndef TEKO_SDK_H
#define TEKO_SDK_H

#include "symbol_table.h"
#include <stdbool.h>

typedef struct {
    const char* intrinsic_name;
    const char* return_type_name;
} VirtualSDKEntry;

// Updated signatures for segregated directory management
void teko_sdk_inject_builtins(SymbolTableScope* global_scope);
bool teko_sdk_is_reserved_intrinsic(const char* name);
bool teko_sdk_verify_directory(const char* project_root);

#endif // TEKO_SDK_H