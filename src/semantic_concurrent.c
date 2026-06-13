#include "semantic_concurrent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Validates whether the user tried to create concurrency primitives using let/mut instead of :=
ConcurrentValidationResult validate_concurrency_variable_creation(const StatementASTNode* var_node) {
    ConcurrentValidationResult result = {CONC_ERR_NONE, NULL};

    if (!var_node || var_node->type != NODE_VAR_DECL) {
        return result;
    }

    if (var_node->data.var_decl.var_type && var_node->data.var_decl.var_type->base_name) {
        const char* type_name = var_node->data.var_decl.var_type->base_name;

        if (strcmp(type_name, "chan") == 0 ||
            strcmp(type_name, "waiter") == 0 ||
            strcmp(type_name, "mutex") == 0) {

            result.error_type = CONC_ERR_INVALID_DECLARATION;

            int msg_len = 128 + (int)strlen(type_name) + (int)strlen(var_node->data.var_decl.var_name);
            result.error_message = (char*)malloc(msg_len);
            if (result.error_message) {
                snprintf(result.error_message, msg_len,
                         "[Semantic Error]: Illegal instantiation of variable '%s'. Primitives of type '%s' must be created with the quick assignment operator ':='.",
                         var_node->data.var_decl.var_name, type_name);
            }

            fprintf(stderr, "%s\n", result.error_message);
        }
    }

    return result;
}

// Explicit implementation of method validation for the concurrent ecosystem
ConcurrentValidationResult validate_channel_method_access(const char* method_name) {
    ConcurrentValidationResult result = {CONC_ERR_NONE, NULL};
    if (!method_name) return result;

    // List of native methods allowed in the Teko asynchronous and concurrent ecosystem
    if (strcmp(method_name, "put") == 0 ||
        strcmp(method_name, "add") == 0 ||
        strcmp(method_name, "done") == 0 ||
        strcmp(method_name, "wait") == 0 ||
        strcmp(method_name, "lock") == 0 ||
        strcmp(method_name, "unlock") == 0) {
        return result; // Valid operation!
    }

    result.error_type = CONC_ERR_ILLEGAL_METHOD;
    int msg_len = 64 + (int)strlen(method_name);
    result.error_message = (char*)malloc(msg_len);
    if (result.error_message) {
        snprintf(result.error_message, msg_len,
                 "[Semantic Error]: The method '.%s' is not a valid operation in the concurrent ecosystem.",
                 method_name);
    }
    fprintf(stderr, "%s\n", result.error_message);
    return result;
}

// Safe deallocation of diagnostic strings
void free_concurrent_validation_result(ConcurrentValidationResult result) {
    if (result.error_message) {
        free(result.error_message);
    }
}