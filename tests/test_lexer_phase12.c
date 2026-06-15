#include "unity.h"
#include "../src/lexer.h"
#include <string.h>

// Lex the first token of `src` and return its type.
static TokenType tok1(const char* src) {
    Lexer lex;
    lexer_init(&lex, src);
    Token t = lexer_next_token(&lex);
    return t.type;
}

// Phase 12 P12-A: the reserved keyword matrix lexes to dedicated tokens, and the
// keywords[] sentinel-ordering bug is fixed (so `required` is matched again).
void test_phase12_reserved_keywords(void) {
    // Regression for the {NULL,…}-before-"required" sentinel bug.
    TEST_ASSERT_EQUAL_INT(TOKEN_REQD, tok1("required"));

    // Resilience
    TEST_ASSERT_EQUAL_INT(TOKEN_CIRCUIT, tok1("circuit"));
    TEST_ASSERT_EQUAL_INT(TOKEN_FALLBACK, tok1("fallback"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DELAYED, tok1("delayed"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RETRY, tok1("retry"));
    TEST_ASSERT_EQUAL_INT(TOKEN_EXPONENTIAL, tok1("exponential"));
    TEST_ASSERT_EQUAL_INT(TOKEN_LOGARITHMIC, tok1("logarithmic"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ATTEMPTS, tok1("attempts"));
    TEST_ASSERT_EQUAL_INT(TOKEN_TIMEOUT, tok1("timeout"));

    // OOP & concurrency
    TEST_ASSERT_EQUAL_INT(TOKEN_CLASS, tok1("class"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ABSTRACT, tok1("abstract"));
    TEST_ASSERT_EQUAL_INT(TOKEN_TRAIT, tok1("trait"));
    TEST_ASSERT_EQUAL_INT(TOKEN_EVENT, tok1("event"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RAISE, tok1("raise"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SUBSCRIBE, tok1("subscribe"));
    TEST_ASSERT_EQUAL_INT(TOKEN_FANOUT, tok1("fanout"));
    TEST_ASSERT_EQUAL_INT(TOKEN_FIRE_AND_FORGET, tok1("fire_and_forget"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SHARED, tok1("shared"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ATOMIC, tok1("atomic"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ROUTINES, tok1("routines"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DUPLEX, tok1("duplex"));

    // Web
    TEST_ASSERT_EQUAL_INT(TOKEN_API, tok1("api"));
    TEST_ASSERT_EQUAL_INT(TOKEN_MIDDLEWARE, tok1("middleware"));
    TEST_ASSERT_EQUAL_INT(TOKEN_GET, tok1("get"));
    TEST_ASSERT_EQUAL_INT(TOKEN_POST, tok1("post"));
    TEST_ASSERT_EQUAL_INT(TOKEN_PUT, tok1("put"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DELETE, tok1("delete"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RPC, tok1("rpc"));
    TEST_ASSERT_EQUAL_INT(TOKEN_WEBSOCKET, tok1("websocket"));

    // Tooling
    TEST_ASSERT_EQUAL_INT(TOKEN_PARSE, tok1("parse"));
    TEST_ASSERT_EQUAL_INT(TOKEN_JSON, tok1("json"));
    TEST_ASSERT_EQUAL_INT(TOKEN_CSV, tok1("csv"));
    TEST_ASSERT_EQUAL_INT(TOKEN_XML, tok1("xml"));
    TEST_ASSERT_EQUAL_INT(TOKEN_HTML, tok1("html"));
    TEST_ASSERT_EQUAL_INT(TOKEN_BUNDLE, tok1("bundle"));
    TEST_ASSERT_EQUAL_INT(TOKEN_MINIFY, tok1("minify"));
    TEST_ASSERT_EQUAL_INT(TOKEN_CRYPTO, tok1("crypto"));
    TEST_ASSERT_EQUAL_INT(TOKEN_HASH, tok1("hash"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ENCRYPT, tok1("encrypt"));

    // Core
    TEST_ASSERT_EQUAL_INT(TOKEN_COMPTIME, tok1("comptime"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SOA, tok1("soa"));

    // Pre-existing keywords still resolve, and a non-keyword stays an identifier.
    TEST_ASSERT_EQUAL_INT(TOKEN_FN, tok1("fn"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RETURN, tok1("return"));
    TEST_ASSERT_EQUAL_INT(TOKEN_IDENTIFIER, tok1("not_a_keyword_xyz"));
}
