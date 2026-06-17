#include "unity.h"
#include "../../src/runtime/teko_router.h"
#include <string.h>

/* Phase 19 (ROUTER-CORE Wave 0) -- KATs for the AOT static radix-tree router.
 * Tests: tree build, exact match, param match, method mismatch (405),
 * no-match (404), middleware order, and a synthetic end-to-end dispatch. */

/* ----- KAT 1: build -- teko_router_new / teko_router_add / teko_router_free ---- */
void test_teko_router_build(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/users", 10, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "POST", "/users", 11, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/users/:id", 12, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "DELETE", "/users/:id", 13, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/health", 99, NULL, 0));

    teko_router_free(r);
}

/* ----- KAT 2: exact path match ------------------------------------------------ */
void test_teko_router_exact_match(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/users", 10, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/health", 99, NULL, 0));

    TekoRouteMatch m = teko_router_dispatch(r, "GET", "/users", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK,  m.status);
    TEST_ASSERT_EQUAL_INT(10,             m.handler_id);
    TEST_ASSERT_EQUAL_INT(0,             m.param_count);

    TekoRouteMatch m2 = teko_router_dispatch(r, "GET", "/health", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m2.status);
    TEST_ASSERT_EQUAL_INT(99,            m2.handler_id);

    teko_router_free(r);
}

/* ----- KAT 3: path-parameter capture ------------------------------------------ */
void test_teko_router_param_match(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/users/:id", 20, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/items/:category/:item", 21, NULL, 0));

    TekoRouteMatch m = teko_router_dispatch(r, "GET", "/users/42", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m.status);
    TEST_ASSERT_EQUAL_INT(20,            m.handler_id);
    TEST_ASSERT_EQUAL_INT(1,             m.param_count);
    TEST_ASSERT_EQUAL_STRING("id", m.params[0].key);
    TEST_ASSERT_EQUAL_STRING("42", m.params[0].value);

    TekoRouteMatch m2 = teko_router_dispatch(r, "GET", "/items/books/978", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK,      m2.status);
    TEST_ASSERT_EQUAL_INT(21,                 m2.handler_id);
    TEST_ASSERT_EQUAL_INT(2,                  m2.param_count);
    TEST_ASSERT_EQUAL_STRING("category",      m2.params[0].key);
    TEST_ASSERT_EQUAL_STRING("books",         m2.params[0].value);
    TEST_ASSERT_EQUAL_STRING("item",          m2.params[1].key);
    TEST_ASSERT_EQUAL_STRING("978",           m2.params[1].value);

    teko_router_free(r);
}

/* ----- KAT 4: literal beats param when both are registered at the same depth --- */
void test_teko_router_literal_beats_param(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    /* Register param route FIRST, then the specific literal -- ordering must not matter */
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/users/:id",   20, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/users/profile", 30, NULL, 0));

    /* "/users/profile" must match the literal handler, not :id */
    TekoRouteMatch m = teko_router_dispatch(r, "GET", "/users/profile", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m.status);
    TEST_ASSERT_EQUAL_INT(30,            m.handler_id); /* literal wins */
    TEST_ASSERT_EQUAL_INT(0,             m.param_count);

    /* "/users/99" must still match :id */
    TekoRouteMatch m2 = teko_router_dispatch(r, "GET", "/users/99", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m2.status);
    TEST_ASSERT_EQUAL_INT(20,            m2.handler_id);
    TEST_ASSERT_EQUAL_INT(1,             m2.param_count);
    TEST_ASSERT_EQUAL_STRING("id", m2.params[0].key);
    TEST_ASSERT_EQUAL_STRING("99", m2.params[0].value);

    teko_router_free(r);
}

/* ----- KAT 5: method mismatch returns 405 ------------------------------------- */
void test_teko_router_method_mismatch_405(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/orders", 40, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "POST", "/orders", 41, NULL, 0));

    /* Path exists but method is wrong */
    TekoRouteMatch m = teko_router_dispatch(r, "DELETE", "/orders", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_405, m.status);
    TEST_ASSERT_EQUAL_INT(-1,             m.handler_id);

    /* Correct method works */
    TekoRouteMatch m2 = teko_router_dispatch(r, "POST", "/orders", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m2.status);
    TEST_ASSERT_EQUAL_INT(41,            m2.handler_id);

    teko_router_free(r);
}

/* ----- KAT 6: no-match returns 404 ------------------------------------------- */
void test_teko_router_no_match_404(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/known", 50, NULL, 0));

    TekoRouteMatch m = teko_router_dispatch(r, "GET", "/unknown", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_404, m.status);
    TEST_ASSERT_EQUAL_INT(-1,             m.handler_id);

    TekoRouteMatch m2 = teko_router_dispatch(r, "GET", "/known/extra", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_404, m2.status);

    teko_router_free(r);
}

/* ----- KAT 7: middleware order preserved in dispatch match -------------------- */
void test_teko_router_middleware_order(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    int mw[] = {100, 101, 102};
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/secured", 60, mw, 3));

    TekoRouteMatch m = teko_router_dispatch(r, "GET", "/secured", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m.status);
    TEST_ASSERT_EQUAL_INT(60,            m.handler_id);
    TEST_ASSERT_EQUAL_INT(3,             m.middleware_count);
    TEST_ASSERT_EQUAL_INT(100,           m.middleware_ids[0]);
    TEST_ASSERT_EQUAL_INT(101,           m.middleware_ids[1]);
    TEST_ASSERT_EQUAL_INT(102,           m.middleware_ids[2]);

    teko_router_free(r);
}

/* ----- KAT 8: synthetic end-to-end -- method + path + params + middleware ----- */
void test_teko_router_synthetic_e2e(void) {
    /* This is the target-agnostic proof: build a realistic mini-API, dispatch a
     * synthetic in-module "request" (no socket), assert the right handler runs. */
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    int auth_mw[] = {1}; /* authentication middleware */
    int log_mw[]  = {2}; /* logging middleware */

    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET",    "/api/v1/users",          200, log_mw,  1));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "POST",   "/api/v1/users",          201, auth_mw, 1));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET",    "/api/v1/users/:id",      202, auth_mw, 1));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "PUT",    "/api/v1/users/:id",      203, auth_mw, 1));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "DELETE", "/api/v1/users/:id",      204, auth_mw, 1));
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET",    "/api/v1/health",         205, NULL, 0));

    /* List users */
    TekoRouteMatch m = teko_router_dispatch(r, "GET", "/api/v1/users", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m.status);
    TEST_ASSERT_EQUAL_INT(200,           m.handler_id);
    TEST_ASSERT_EQUAL_INT(1,             m.middleware_count);
    TEST_ASSERT_EQUAL_INT(2,             m.middleware_ids[0]); /* log_mw */

    /* Create user */
    TekoRouteMatch m2 = teko_router_dispatch(r, "POST", "/api/v1/users", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m2.status);
    TEST_ASSERT_EQUAL_INT(201,           m2.handler_id);
    TEST_ASSERT_EQUAL_INT(1,             m2.middleware_count);
    TEST_ASSERT_EQUAL_INT(1,             m2.middleware_ids[0]); /* auth_mw */

    /* Get user by id */
    TekoRouteMatch m3 = teko_router_dispatch(r, "GET", "/api/v1/users/99", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m3.status);
    TEST_ASSERT_EQUAL_INT(202,           m3.handler_id);
    TEST_ASSERT_EQUAL_INT(1,             m3.param_count);
    TEST_ASSERT_EQUAL_STRING("id",       m3.params[0].key);
    TEST_ASSERT_EQUAL_STRING("99",       m3.params[0].value);

    /* Delete user */
    TekoRouteMatch m4 = teko_router_dispatch(r, "DELETE", "/api/v1/users/77", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m4.status);
    TEST_ASSERT_EQUAL_INT(204,           m4.handler_id);
    TEST_ASSERT_EQUAL_STRING("77",       m4.params[0].value);

    /* Health check */
    TekoRouteMatch m5 = teko_router_dispatch(r, "GET", "/api/v1/health", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, m5.status);
    TEST_ASSERT_EQUAL_INT(205,           m5.handler_id);
    TEST_ASSERT_EQUAL_INT(0,             m5.middleware_count);

    /* Wrong method on user list -> 405 */
    TekoRouteMatch m6 = teko_router_dispatch(r, "PATCH", "/api/v1/users", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_405, m6.status);
    TEST_ASSERT_EQUAL_INT(-1,             m6.handler_id);

    /* Non-existent path -> 404 */
    TekoRouteMatch m7 = teko_router_dispatch(r, "GET", "/api/v2/users", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_404, m7.status);

    teko_router_free(r);
}

/* ----- KAT 9: security / badarg hardening ------------------------------------ */
void test_teko_router_badarg_hardening(void) {
    TekoRouter* r = teko_router_new();
    TEST_ASSERT_NOT_NULL(r);

    /* NULL router / method / path */
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(NULL, "GET", "/x", 1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(r, NULL, "/x", 1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(r, "GET", NULL, 1, NULL, 0));

    /* Method too long (> 7 chars) */
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(r, "TOOLONGMETHOD", "/x", 1, NULL, 0));

    /* Path too long */
    char long_path[600];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(r, "GET", long_path, 1, NULL, 0));

    /* Path traversal attempt */
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(r, "GET", "/a/../b", 1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, teko_router_add(r, "GET", "/a/./b",  1, NULL, 0));

    /* Dispatch with NULL router/path/method */
    TekoRouteMatch m = teko_router_dispatch(NULL, "GET", "/x", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_404, m.status);
    TEST_ASSERT_EQUAL_INT(-1,             m.handler_id);

    TekoRouteMatch m2 = teko_router_dispatch(r, NULL, "/x", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_404, m2.status);

    /* teko_router_reset keeps the router alive */
    TEST_ASSERT_EQUAL_INT(0, teko_router_add(r, "GET", "/alive", 77, NULL, 0));
    TekoRouteMatch mb = teko_router_dispatch(r, "GET", "/alive", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_OK, mb.status);
    teko_router_reset(r);
    TekoRouteMatch mc = teko_router_dispatch(r, "GET", "/alive", NULL);
    TEST_ASSERT_EQUAL_INT(TEKO_ROUTE_404, mc.status);

    teko_router_free(r);
    /* teko_router_free(NULL) must not crash */
    teko_router_free(NULL);
}
