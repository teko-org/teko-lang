#include "unity.h"
#include "../../src/runtime/teko_convert.h"
#include <stdlib.h>
#include <string.h>

// Phase 16 (Casting / Type Conversions & Parsing) — KATs for the culture-invariant conversion
// runtime (the single source of truth). Fully deterministic: no locale, no libc number calls.

static void expect_i64(long long v, const char* want) {
    char* s = teko_convert_i64_to_string(v);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING(want, s);
    free(s);
}

void test_teko_convert_i64_to_string(void) {
    expect_i64(0, "0");
    expect_i64(1, "1");
    expect_i64(-1, "-1");
    expect_i64(42, "42");
    expect_i64(-42, "-42");
    expect_i64(2147483647LL, "2147483647");          // INT32_MAX
    expect_i64(-2147483648LL, "-2147483648");        // INT32_MIN
    expect_i64(9223372036854775807LL, "9223372036854775807");   // INT64_MAX
    expect_i64(-9223372036854775807LL - 1, "-9223372036854775808"); // INT64_MIN (no overflow)
    expect_i64(1000000, "1000000");                  // no digit grouping (culture-invariant)
}

void test_teko_convert_bool_to_string(void) {
    char* s;
    s = teko_convert_bool_to_string(0);  TEST_ASSERT_EQUAL_STRING("false", s); free(s);
    s = teko_convert_bool_to_string(1);  TEST_ASSERT_EQUAL_STRING("true", s);  free(s);
    s = teko_convert_bool_to_string(42); TEST_ASSERT_EQUAL_STRING("true", s);  free(s); // any non-zero
    s = teko_convert_bool_to_string(-1); TEST_ASSERT_EQUAL_STRING("true", s);  free(s);
}

void test_teko_convert_str_concat(void) {
    char* s;
    s = teko_convert_str_concat("foo", "bar"); TEST_ASSERT_EQUAL_STRING("foobar", s); free(s);
    s = teko_convert_str_concat("", "x");      TEST_ASSERT_EQUAL_STRING("x", s);      free(s);
    s = teko_convert_str_concat("x", "");      TEST_ASSERT_EQUAL_STRING("x", s);      free(s);
    s = teko_convert_str_concat("", "");       TEST_ASSERT_EQUAL_STRING("", s);       free(s);
    s = teko_convert_str_concat(NULL, "y");    TEST_ASSERT_EQUAL_STRING("y", s);      free(s);
    s = teko_convert_str_concat("y", NULL);    TEST_ASSERT_EQUAL_STRING("y", s);      free(s);
    s = teko_convert_str_concat("x = ", "42"); TEST_ASSERT_EQUAL_STRING("x = 42", s); free(s);
}

void test_teko_convert_parse_i64_valid(void) {
    long long v = 999;
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("0", &v));    TEST_ASSERT_EQUAL_INT64(0, v);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("42", &v));   TEST_ASSERT_EQUAL_INT64(42, v);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("-42", &v));  TEST_ASSERT_EQUAL_INT64(-42, v);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("+5", &v));   TEST_ASSERT_EQUAL_INT64(5, v);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("  7 ", &v)); TEST_ASSERT_EQUAL_INT64(7, v);  // ws
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("9223372036854775807", &v));
    TEST_ASSERT_EQUAL_INT64(9223372036854775807LL, v);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_i64("-9223372036854775808", &v));
    TEST_ASSERT_EQUAL_INT64(-9223372036854775807LL - 1, v);
}

void test_teko_convert_parse_i64_invalid(void) {
    long long v = 123;
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("", &v));      TEST_ASSERT_EQUAL_INT64(123, v); // unchanged
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("abc", &v));
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("12a", &v));   // trailing junk
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("-", &v));     // sign only
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("1 2", &v));   // interior space
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("99999999999999999999999", &v)); // overflow
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("9223372036854775808", &v));     // INT64_MAX+1
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("-9223372036854775809", &v));    // INT64_MIN-1
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_i64("1,000", &v)); // grouping rejected
    TEST_ASSERT_EQUAL_INT64(123, v);                               // never written on failure
}

void test_teko_convert_parse_bool(void) {
    int b = -7;
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_bool("true", &b));   TEST_ASSERT_EQUAL_INT(1, b);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_bool("false", &b));  TEST_ASSERT_EQUAL_INT(0, b);
    TEST_ASSERT_EQUAL_INT(1, teko_convert_parse_bool(" true ", &b)); TEST_ASSERT_EQUAL_INT(1, b);
    b = -7;
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_bool("True", &b));   // case-sensitive
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_bool("1", &b));
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_bool("yes", &b));
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_bool("", &b));
    TEST_ASSERT_EQUAL_INT(0, teko_convert_parse_bool("truex", &b));  // trailing junk
    TEST_ASSERT_EQUAL_INT(-7, b);                                    // never written on failure
}
