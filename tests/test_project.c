#include "unity.h"
#include "project_manager.h"
#include <stdio.h>
#include <stdlib.h>

void test_teko_project_manifest_parsing(void) {
    // Scenario A: Validates reading a static library (library type)
    const char* mock_tkp_lib = "project minha_lib {\n version: \"1.2.0\";\n author: \"teko\";\n type: \"static_lib\";\n}";
    FILE* f = fopen("test_lib.tkp", "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(mock_tkp_lib, f);
    fclose(f);

    TekoProjectConfig* config = teko_project_load("test_lib.tkp");
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_INT(TARGET_STATIC_LIB, config->target_type);

    // For libraries, the structure validation passes even without main.tks existing on disk!
    bool is_valid_struct = teko_project_validate_structure(config);
    TEST_ASSERT_TRUE(is_valid_struct);

    remove("test_lib.tkp");
    teko_project_free(config);
}