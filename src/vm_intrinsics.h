#ifndef VM_INTRINSICS_H
#define VM_INTRINSICS_H

#include "vm_core.h"
#include <stdint.h>

// Unique internal identifiers for each native @ library
typedef enum {
    INTRINSIC_MARSHALL_TO_PTR = 1,
    INTRINSIC_MARSHALL_FROM_PTR,
    INTRINSIC_FLOWS_REQUEST,
    INTRINSIC_FLOWS_NOTIFY,
    INTRINSIC_FLOWS_SEND,
    INTRINSIC_STRINGS_CONCAT,
    INTRINSIC_UNKNOWN = 0
} TekoIntrinsicKind;

// Public signatures of the Intrinsics subsystem
TekoIntrinsicKind vm_intrinsic_resolve(const char* qualified_name);
int32_t vm_intrinsic_execute(TekoIntrinsicKind kind, TekoVM* vm, int32_t* args, int arg_count);

#endif // VM_INTRINSICS_H