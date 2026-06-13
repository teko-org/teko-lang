#include "unity.h"
#include "../src/symbol_table.h"
#include "../src/semantic_struct.h"
#include <string.h>
#include <stdlib.h>

// Validates nested scopes, redeclarations, and the mutability rule based on let/mut
void test_symbol_table_scopes_and_mutability(void) {
    // 1. Creates the global scope and attempts to insert symbols
    SymbolTableScope* global = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(global);

    bool s1 = symbol_table_insert(global, "MY_CONST", SYM_CONSTANT, NULL, false);
    TEST_ASSERT_TRUE(s1);

    // Prevents redeclaration at the same level
    bool s2 = symbol_table_insert(global, "MY_CONST", SYM_VARIABLE, NULL, true);
    TEST_ASSERT_FALSE(s2);

    // 2. Creates a local scope and validates valid scope shadowing
    SymbolTableScope* local = symbol_table_create_scope(global);
    bool s3 = symbol_table_insert(local, "idx", SYM_VARIABLE, NULL, true); // mut idx
    TEST_ASSERT_TRUE(s3);

    Symbol* lookup_idx = symbol_table_lookup(local, "idx");
    TEST_ASSERT_NOT_NULL(lookup_idx);
    TEST_ASSERT_TRUE(lookup_idx->is_mutable); // Validates that the compiler remembers it is 'mut'

    Symbol* lookup_const = symbol_table_lookup(local, "MY_CONST");
    TEST_ASSERT_NOT_NULL(lookup_const);
    TEST_ASSERT_FALSE(lookup_const->is_mutable); // Validates that it was found by walking up to the global scope and is immutable

    symbol_table_free_scope(local);
    symbol_table_free_scope(global);
}

// Validates the enforcement of the required-fields contract
void test_required_properties_validation(void) {
    MessageProperty defined[1];
    defined[0].prop_name = "message";
    defined[0].is_required = true;
    defined[0].is_mutable = false;
    defined[0].prop_type = NULL;

    // Valid scenario: required field was initialized
    const char* init_good[] = {"message"};
    StructValidationResult res_good = validate_required_properties(defined, 1, init_good, 1);
    TEST_ASSERT_EQUAL_INT(STRUCT_ERR_NONE, res_good.error_type);
    free_struct_validation_result(res_good);

    // Invalid scenario: required field was omitted
    const char* init_bad[] = {"wrong_field"};
    StructValidationResult res_bad = validate_required_properties(defined, 1, init_bad, 1);
    TEST_ASSERT_EQUAL_INT(STRUCT_ERR_MISSING_REQUIRED, res_bad.error_type);
    TEST_ASSERT_NOT_NULL(res_bad.error_message);
    free_struct_validation_result(res_bad);
}