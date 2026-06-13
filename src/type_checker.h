#ifndef TYPE_CHECKER_H
#define TYPE_CHECKER_H

#include "parser_types.h"
#include "parser_statements.h"
#include "symbol_table.h"

typedef enum {
    TYPE_ERR_NONE = 0,
    TYPE_ERR_INCOMPATIBLE_ASSIGN,
    TYPE_ERR_IMMUTABLE_WRITE,
    TYPE_ERR_UNDECLARED_SYMBOL,
    TYPE_ERR_ESCAPE_VIOLATION     // New: Error if there is an illegal scope escape
} TypeErrorKind;

typedef struct TypeCheckResult {
    TypeErrorKind error_kind;
    char* error_message;
    TypeInfo* resolved_type;
} TypeCheckResult;

// Public functions of the expanded Type Checker
TypeCheckResult check_statement_types(SymbolTableScope* scope, const StatementASTNode* stmt);
TypeCheckResult check_expression_type(SymbolTableScope* scope, const char* raw_expression);
TypeCheckResult validate_elvis_operator_types(TypeInfo* left_type, TypeInfo* right_type);
TypeCheckResult validate_safe_navigation_types(TypeInfo* object_type, const char* property_name);
void perform_escape_analysis(SymbolTableScope* scope, StatementASTNode* stmt, const char* return_target_type);
void free_type_check_result(TypeCheckResult result);

#endif // TYPE_CHECKER_H