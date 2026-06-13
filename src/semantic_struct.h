#ifndef SEMANTIC_STRUCT_H
#define SEMANTIC_STRUCT_H

#include "parser_messaging.h"

typedef enum {
    STRUCT_ERR_NONE = 0,
    STRUCT_ERR_MISSING_REQUIRED
} StructSemanticError;

typedef struct StructValidationResult {
    StructSemanticError error_type;
    char* error_message;
} StructValidationResult;

// Validates whether the provided keys contain all fields defined as 'required'
StructValidationResult validate_required_properties(const MessageProperty* defined_props, int defined_count,
                                                   const char** initialized_names, int initialized_count);
void free_struct_validation_result(StructValidationResult result);

#endif // SEMANTIC_STRUCT_H