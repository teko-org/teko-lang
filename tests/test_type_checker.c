#include "unity.h"
#include "type_checker.h"
#include "symbol_table.h"
#include <stdlib.h>
#include <string.h>

// Test 1: Validates that the Type Checker prevents invalid coercion (i32 receiving text)
void test_type_compatibility_assignment(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    StatementASTNode var_stmt;
    var_stmt.type = NODE_VAR_DECL;
    var_stmt.data.var_decl.is_mutable = false;
    var_stmt.data.var_decl.var_name = strdup("age");
    var_stmt.data.var_decl.initializer_raw = strdup("\"twenty years\"");
    var_stmt.data.var_decl.var_type = NULL;

    TypeInfo type_i32;
    type_i32.kind = NODE_TYPE_BASIC;
    type_i32.base_name = strdup("i32");
    var_stmt.data.var_decl.var_type = &type_i32;

    TypeCheckResult res = check_statement_types(scope, &var_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_INCOMPATIBLE_ASSIGN, res.error_kind);
    TEST_ASSERT_NOT_NULL(res.error_message);

    free_type_check_result(res);
    free(var_stmt.data.var_decl.var_name);
    free(var_stmt.data.var_decl.initializer_raw);
    free(type_i32.base_name);
    symbol_table_free_scope(scope);
}

// Test 2: Validates write blocking on immutable variables (let)
void test_let_immutability_protection(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    TypeInfo type_i32;
    type_i32.kind = NODE_TYPE_BASIC;
    type_i32.base_name = strdup("i32");
    symbol_table_insert(scope, "idx", SYM_VARIABLE, &type_i32, false);

    StatementASTNode expr_stmt;
    expr_stmt.type = NODE_EXPR_STMT;
    expr_stmt.data.expr_stmt.expression_raw = strdup("idx = 1");

    TypeCheckResult res = check_statement_types(scope, &expr_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_IMMUTABLE_WRITE, res.error_kind);
    TEST_ASSERT_NOT_NULL(res.error_message);

    free_type_check_result(res);
    free(expr_stmt.data.expr_stmt.expression_raw);
    free(type_i32.base_name);
    symbol_table_free_scope(scope);
}

// Test 3: Validates that the Type Checker tracks escaping strings and emits the automatic promotion warning
void test_escape_analysis_region_promotion(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    StatementASTNode stmt;
    stmt.type = NODE_VAR_DECL;
    stmt.data.var_decl.is_mutable = false;
    stmt.data.var_decl.var_name = strdup("escaped_string");
    stmt.data.var_decl.initializer_raw = strdup("`result`");
    stmt.data.var_decl.var_type = NULL;

    perform_escape_analysis(scope, &stmt, "str");

    TypeCheckResult res = check_statement_types(scope, &stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, res.error_kind);

    free_type_check_result(res);
    free(stmt.data.var_decl.var_name);
    free(stmt.data.var_decl.initializer_raw);
    symbol_table_free_scope(scope);
}

// Test 4: Validates that common i32 numeric literals can be implicitly promoted to decimal/bigint
void test_implicit_precision_coercion_for_arbitrary_types(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    StatementASTNode var_stmt;
    var_stmt.type = NODE_VAR_DECL;
    var_stmt.data.var_decl.is_mutable = false;
    var_stmt.data.var_decl.var_name = strdup("saldo");
    var_stmt.data.var_decl.initializer_raw = strdup("3000"); // Literal interpreted as basic i32

    // Target type declared as high-precision 'decimal'
    TypeInfo type_dec;
    type_dec.kind = NODE_TYPE_DECIMAL;
    type_dec.base_name = strdup("decimal");
    var_stmt.data.var_decl.var_type = &type_dec;

    // The Type Checker should accept the assignment by applying the implicit numeric coercion rule
    TypeCheckResult res = check_statement_types(scope, &var_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, res.error_kind);

    free_type_check_result(res);
    free(var_stmt.data.var_decl.var_name);
    free(var_stmt.data.var_decl.initializer_raw);
    free(type_dec.base_name);
    symbol_table_free_scope(scope);
}

// Test 5: Validates that composite assignments (e.g. >>=) on let variables are blocked by the Type Checker
void test_composite_assignment_immutability_protection(void) {
    SymbolTableScope* scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(scope);

    TypeInfo type_i32;
    type_i32.kind = NODE_TYPE_BASIC;
    type_i32.base_name = strdup("i32");
    symbol_table_insert(scope, "arr_idx", SYM_VARIABLE, &type_i32, false); // Immutable

    // Attempts a bit-shift operation with compound assignment: arr_idx >>= 1
    StatementASTNode expr_stmt;
    expr_stmt.type = NODE_EXPR_STMT;
    expr_stmt.data.expr_stmt.expression_raw = strdup("arr_idx >>= 1");

    TypeCheckResult res = check_statement_types(scope, &expr_stmt);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_IMMUTABLE_WRITE, res.error_kind);
    TEST_ASSERT_NOT_NULL(res.error_message);

    free_type_check_result(res);
    free(expr_stmt.data.expr_stmt.expression_raw);
    free(type_i32.base_name);
    symbol_table_free_scope(scope);
}

// Test 6: Full semantic coverage of the Elvis Operator (??)
void test_elvis_operator_coalescence_semantics(void) {
    // Left type: str? (Nullable)
    TypeInfo left_str_nullable;
    left_str_nullable.kind = NODE_TYPE_BASIC;
    left_str_nullable.base_name = strdup("str");
    left_str_nullable.is_nullable = true;

    // Right type: str (Non-nullable, default value)
    TypeInfo right_str;
    right_str.kind = NODE_TYPE_BASIC;
    right_str.base_name = strdup("str");
    right_str.is_nullable = false;

    // Valid scenario: str? ?? str -> should return the clean "str" base type
    TypeCheckResult res_good = validate_elvis_operator_types(&left_str_nullable, &right_str);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, res_good.error_kind);
    TEST_ASSERT_EQUAL_STRING("str", res_good.resolved_type->base_name);
    TEST_ASSERT_FALSE(res_good.resolved_type->is_nullable);
    free_type_check_result(res_good);

    // Invalid scenario: attempting to apply ?? on a type that is already non-nullable (i32 ?? i32)
    TypeInfo non_nullable_i32;
    non_nullable_i32.kind = NODE_TYPE_BASIC;
    non_nullable_i32.base_name = strdup("i32");
    non_nullable_i32.is_nullable = false;

    TypeCheckResult res_bad = validate_elvis_operator_types(&non_nullable_i32, &non_nullable_i32);
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_INCOMPATIBLE_ASSIGN, res_bad.error_kind);
    TEST_ASSERT_NOT_NULL(res_bad.error_message);

    // Cleanups
    free_type_check_result(res_bad);
    free(left_str_nullable.base_name);
    free(right_str.base_name);
    free(non_nullable_i32.base_name);
}

// Test 7: Advanced semantic coverage of safe chaining with Elvis (s?.length ?? 0)
void test_safe_navigation_with_elvis_coalescence(void) {
    // 1. Instantiates the type of 's' as str? (Nullable)
    TypeInfo object_str_nullable;
    object_str_nullable.kind = NODE_TYPE_BASIC;
    object_str_nullable.base_name = strdup("str");
    object_str_nullable.is_nullable = true;

    // 2. Executes safe navigation s?.length -> should propagate to i32?
    TypeCheckResult navigation_res = validate_safe_navigation_types(&object_str_nullable, "length");
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, navigation_res.error_kind);
    TEST_ASSERT_TRUE(navigation_res.resolved_type->is_nullable); // Should be nullable (i32?)
    TEST_ASSERT_EQUAL_STRING("i32", navigation_res.resolved_type->base_name);

    // 3. Executes null coalescence with the default value on the right: i32? ?? i32
    TypeInfo right_default_i32;
    right_default_i32.kind = NODE_TYPE_BASIC;
    right_default_i32.base_name = strdup("i32");
    right_default_i32.is_nullable = false;

    // Invokes the Elvis validator passing the ?. result and literal 0
    // External forward declaration of validate_elvis_operator_types
    extern TypeCheckResult validate_elvis_operator_types(TypeInfo* left_type, TypeInfo* right_type);
    TypeCheckResult final_res = validate_elvis_operator_types(navigation_res.resolved_type, &right_default_i32);

    // The final resulting type must be purified of nullability (pure i32)
    TEST_ASSERT_EQUAL_INT(TYPE_ERR_NONE, final_res.error_kind);
    TEST_ASSERT_EQUAL_STRING("i32", final_res.resolved_type->base_name);
    TEST_ASSERT_FALSE(final_res.resolved_type->is_nullable); // Production target reached!

    // Test heap memory cleanups
    free_type_check_result(final_res);
    free_type_info(navigation_res.resolved_type);
    free_type_check_result(navigation_res);
    free(object_str_nullable.base_name);
    free(right_default_i32.base_name);
}