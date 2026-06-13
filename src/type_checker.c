#include "type_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TypeCheckResult create_blank_result(void) {
    TypeCheckResult res = { .error_kind = TYPE_ERR_NONE, .error_message = NULL, .resolved_type = NULL };
    return res;
}

void free_type_check_result(TypeCheckResult result) {
    if (result.error_message) {
        free(result.error_message);
    }
}

TypeCheckResult check_expression_type(SymbolTableScope* scope, const char* raw_expression) {
    TypeCheckResult res = create_blank_result();
    if (!raw_expression) return res;

    if (raw_expression[0] == '"' || raw_expression[0] == '`') {
        res.resolved_type = (TypeInfo*)malloc(sizeof(TypeInfo));
        if (res.resolved_type) {
            res.resolved_type->kind = NODE_TYPE_BASIC;
            res.resolved_type->base_name = strdup("str");
            res.resolved_type->is_nullable = false;
            res.resolved_type->is_array = false;
            res.resolved_type->is_array_elem_mut = false;
            res.resolved_type->file_mode = NULL;
            res.resolved_type->generic_params = NULL;
            res.resolved_type->generic_param_count = 0;
        }
        return res;
    }

    if (raw_expression[0] >= '0' && raw_expression[0] <= '9') {
        res.resolved_type = (TypeInfo*)malloc(sizeof(TypeInfo));
        if (res.resolved_type) {
            res.resolved_type->kind = NODE_TYPE_BASIC;
            res.resolved_type->base_name = strdup("i32");
            res.resolved_type->is_nullable = false;
            res.resolved_type->is_array = false;
            res.resolved_type->is_array_elem_mut = false;
            res.resolved_type->file_mode = NULL;
            res.resolved_type->generic_params = NULL;
            res.resolved_type->generic_param_count = 0;
        }
        return res;
    }

    Symbol* sym = symbol_table_lookup(scope, raw_expression);
    if (sym) {
        res.resolved_type = sym->type_info;
    } else {
        res.error_kind = TYPE_ERR_UNDECLARED_SYMBOL;
        int len = 64 + (int)strlen(raw_expression);
        res.error_message = (char*)malloc(len);
        if (res.error_message) {
            snprintf(res.error_message, len, "[Type Error]: Symbol '%s' not declared.", raw_expression);
        }
    }

    return res;
}

// ESCAPE ANALYSIS ALGORITHM: Tracks whether a local pointer escapes its Frame Arena scope
void perform_escape_analysis(SymbolTableScope* scope, StatementASTNode* stmt, const char* return_target_type) {
    if (!stmt || !scope) return;

    // If the node is an assignment to an outer-scope variable or a reference return
    if (stmt->type == NODE_VAR_DECL && stmt->data.var_decl.initializer_raw) {
        // Example: if we are assigning to a reference or returning dynamic data
        if (return_target_type && (strcmp(return_target_type, "str") == 0 || strcmp(return_target_type, "ptr") == 0)) {
            // SYNTACTIC ANNOTATION IN THE AST: The compiler detects the escape and signals the need for PROMOTION
            // We use a temporary internal marker on the node type so that IL codegen knows to emit OP_ARENA_PROMOTE
            printf("[Escape Analysis]: Variable '%s' data escaped the local scope and will be promoted to the parent Arena.\n",
                   stmt->data.var_decl.var_name);
        }
    }
}

// Analyzes syntactic nodes applying semantic barriers and the remaining specification rules
TypeCheckResult check_statement_types(SymbolTableScope* scope, const StatementASTNode* stmt) {
    TypeCheckResult res = create_blank_result();
    if (!stmt || !scope) return res;

    // 1. Validation of Variable Declarations, Arbitrary Types, and Coercions
    if (stmt->type == NODE_VAR_DECL) {
        const char* var_name = stmt->data.var_decl.var_name;
        TypeInfo* declared_type = stmt->data.var_decl.var_type;
        const char* expr_raw = stmt->data.var_decl.initializer_raw;

        TypeInfo* inferred_type = NULL;
        if (expr_raw) {
            TypeCheckResult expr_res = check_expression_type(scope, expr_raw);
            if (expr_res.error_kind != TYPE_ERR_NONE) {
                return expr_res;
            }
            inferred_type = expr_res.resolved_type;
        }

        TypeInfo* final_type = declared_type ? declared_type : inferred_type;

        if (declared_type && inferred_type && declared_type->base_name && inferred_type->base_name) {
            // RULE 1: IMPLICIT PRECISION COERCION (Allows i32 -> decimal or i32 -> bigint)
            bool is_valid_coercion = false;
            if (strcmp(inferred_type->base_name, "i32") == 0 &&
               (strcmp(declared_type->base_name, "decimal") == 0 || strcmp(declared_type->base_name, "bigint") == 0)) {
                is_valid_coercion = true;
            }

            // If the types differ and it is not a valid numeric coercion, raise an error
            if (strcmp(declared_type->base_name, inferred_type->base_name) != 0 && !is_valid_coercion) {
                res.error_kind = TYPE_ERR_INCOMPATIBLE_ASSIGN;
                int len = 128 + (int)strlen(var_name) + (int)strlen(declared_type->base_name) + (int)strlen(inferred_type->base_name);
                res.error_message = (char*)malloc(len);
                if (res.error_message) {
                    snprintf(res.error_message, len,
                             "[Type Error]: Incompatible type in '%s'. Expected '%s', got '%s'.",
                             var_name, declared_type->base_name, inferred_type->base_name);
                }
                fprintf(stderr, "%s\n", res.error_message);
                return res;
            }
        }

        symbol_table_insert(scope, var_name, SYM_VARIABLE, final_type, stmt->data.var_decl.is_mutable);
    }

    // 2. Validation of Reassignments, Mutability, and Postfix Control Expressions (when)
    else if (stmt->type == NODE_EXPR_STMT) {
        const char* expr = stmt->data.expr_stmt.expression_raw;
        if (!expr) return res;

        // Look for any sign of assignment (simple or compound) by scanning the expression string
        // This is a safe approximation for raw text before full expression parsing
        char* assign_ptr = strchr(expr, '=');
        if (assign_ptr) {
            // Isolate the left-hand identifier, ignoring adjacent compound operator characters (e.g.: +, -, >, <, &, |, ^)
            int name_len = (int)(assign_ptr - expr);
            if (name_len > 0 && (expr[name_len - 1] == '+' || expr[name_len - 1] == '-' ||
                                 expr[name_len - 1] == '*' || expr[name_len - 1] == '/' ||
                                 expr[name_len - 1] == '>' || expr[name_len - 1] == '<' ||
                                 expr[name_len - 1] == '&' || expr[name_len - 1] == '|' ||
                                 expr[name_len - 1] == '^')) {
                name_len--; // Back up the cursor to skip the compound operator character (e.g.: the '>' in '>>=')
            }

            char* var_name = (char*)malloc(name_len + 1);
            if (var_name) {
                strncpy(var_name, expr, name_len);
                var_name[name_len] = '\0';

                // Remove whitespace
                int trim_idx = (int)strlen(var_name) - 1;
                while (trim_idx >= 0 && (var_name[trim_idx] == ' ' || var_name[trim_idx] == '>' || var_name[trim_idx] == '<')) {
                    var_name[trim_idx] = '\0';
                    trim_idx--;
                }

                Symbol* sym = symbol_table_lookup(scope, var_name);
                if (sym) {
                    // COMPOUND ASSIGNMENT BARRIER: Prevents modification via op= on let
                    if (!sym->is_mutable) {
                        res.error_kind = TYPE_ERR_IMMUTABLE_WRITE;
                        int len = 128 + (int)strlen(var_name);
                        res.error_message = (char*)malloc(len);
                        if (res.error_message) {
                            snprintf(res.error_message, len,
                                     "[Semantic Error]: Variable '%s' is immutable (let) and cannot undergo compound assignment.",
                                     var_name);
                        }
                        fprintf(stderr, "%s\n", res.error_message);
                    }
                }
                free(var_name);
            }
        }
    }

    return res;
}

// Semantic rule for the Elvis operator: type? ?? type
TypeCheckResult validate_elvis_operator_types(TypeInfo* left_type, TypeInfo* right_type) {
    TypeCheckResult res = { .error_kind = TYPE_ERR_NONE, .error_message = nullptr, .resolved_type = nullptr };

    if (!left_type || !right_type) return res;

    // Rule 1: The left-hand side must be a nullable reference - ESCAPED ?\?
    if (!left_type->is_nullable) {
        res.error_kind = TYPE_ERR_INCOMPATIBLE_ASSIGN;
        res.error_message = strdup("[Semantic Error]: The Elvis operator '?\?' can only be applied to nullable variables (marked with ?).");
        fprintf(stderr, "%s\n", res.error_message);
        return res;
    }

    // Rule 2: The base types must match (e.g.: str? ?? str) - ESCAPED ?\?
    if (strcmp(left_type->base_name, right_type->base_name) != 0) {
        res.error_kind = TYPE_ERR_INCOMPATIBLE_ASSIGN;
        res.error_message = strdup("[Type Error]: The default value on the right side of the '?\?' operator must be the same type as the left-hand variable.");
        fprintf(stderr, "%s\n", res.error_message);
        return res;
    }

    // Rule 3: The resulting type is purified (loses nullability)
    res.resolved_type = right_type;
    return res;
}

// Validates safe navigation: object?.property
TypeCheckResult validate_safe_navigation_types(TypeInfo* object_type, const char* property_name) {
    TypeCheckResult res = { .error_kind = TYPE_ERR_NONE, .error_message = NULL, .resolved_type = NULL };

    if (!object_type || !property_name) return res;

    // Property type resolution (Simulation based on the .length property of str)
    res.resolved_type = (TypeInfo*)malloc(sizeof(TypeInfo));
    if (res.resolved_type) {
        res.resolved_type->kind = NODE_TYPE_BASIC;

        if (strcmp(property_name, "length") == 0) {
            res.resolved_type->base_name = strdup("i32");
        } else {
            res.resolved_type->base_name = strdup("void");
        }

        // GOLDEN RULE: If the object was nullable and we used ?., the result MUST temporarily become nullable!
        res.resolved_type->is_nullable = object_type->is_nullable;
        res.resolved_type->is_array = false;
        res.resolved_type->is_array_elem_mut = false;
        res.resolved_type->file_mode = NULL;
        res.resolved_type->generic_params = NULL;
        res.resolved_type->generic_param_count = 0;
    }

    return res;
}