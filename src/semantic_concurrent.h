#ifndef SEMANTIC_CONCURRENT_H
#define SEMANTIC_CONCURRENT_H

#include "parser.h"
#include "parser_concurrent.h"
#include "parser_statements.h"

// Semantic error types mapped to the concurrency infrastructure
typedef enum {
    CONC_ERR_NONE = 0,
    CONC_ERR_INVALID_DECLARATION, // Attempt to use let/mut for channels/waiters/mutexes
    CONC_ERR_ILLEGAL_METHOD       // Unknown method or invalid call on channels
} ConcurrentSemanticError;

// Structure representing the result of a concurrency validation
typedef struct ConcurrentValidationResult {
    ConcurrentSemanticError error_type;
    char* error_message;
} ConcurrentValidationResult;

// Public signatures of the Concurrency Semantic Validator
ConcurrentValidationResult validate_concurrency_variable_creation(const StatementASTNode* var_node);
ConcurrentValidationResult validate_channel_method_access(const char* method_name);
void free_concurrent_validation_result(ConcurrentValidationResult result);

#endif // SEMANTIC_CONCURRENT_H