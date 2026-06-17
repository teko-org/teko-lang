#ifndef TEKO_ROUTER_H
#define TEKO_ROUTER_H

#include <stdint.h>
#include <stdlib.h>

/* OP_CALL_RUNTIME id range 175-179 RESERVED for router -- ROUTER-NATIVE (Wave 2) */

/* Phase 19 (ROUTER-CORE, Wave 0) -- AOT static radix-tree router.
 * Target-agnostic: zero OS calls, zero sockets, zero libc beyond string.h/stdlib.h.
 * Compiles identically into native binaries AND the WASM reactor.
 *
 * Security posture: method/path/headers are ATTACKER-CONTROLLED. Every length is
 * bounded before use; no unbounded copies; no path-traversal (`.`/`..` rejected);
 * no format-string calls on user input; integer-overflow-safe sizing.
 *
 * ZERO RUNTIME REFLECTION: handler_id is a compile-time integer (the frontend maps
 * it to an OP_CALL_RUNTIME id at Wave 2); there is no name lookup at runtime. */

/* ---------- capacity caps (hard limits, checked fail-loud) -------------------- */
#define TEKO_ROUTE_MAX_METHOD    8    /* longest HTTP method: "OPTIONS\0" = 8 */
#define TEKO_ROUTE_MAX_PATH    512    /* max path (pattern or request path) */
#define TEKO_ROUTE_MAX_DEPTH    32    /* max path segments per route */
#define TEKO_ROUTE_MAX_CHILDREN 64    /* max children per radix-tree node */
#define TEKO_ROUTE_MAX_ROUTES  256    /* max routes per router */
#define TEKO_ROUTE_MAX_METHODS   8    /* max method/handler pairs per leaf */
#define TEKO_ROUTE_MAX_MW       16    /* max middleware ids per route */
#define TEKO_ROUTE_MAX_PARAMS   16    /* max captured path params per dispatch */

/* ---------- dispatch status codes --------------------------------------------- */
#define TEKO_ROUTE_OK    200
#define TEKO_ROUTE_404   404
#define TEKO_ROUTE_405   405

/* ---------- a single captured path parameter ----------------------------------- */
typedef struct {
    char key[64];    /* the param name, e.g. "id" from ":id" */
    char value[128]; /* the captured segment value, e.g. "42" */
} TekoRouteParam;

/* ---------- dispatch result ---------------------------------------------------- */
typedef struct {
    int status;                              /* TEKO_ROUTE_OK / 404 / 405 */
    int handler_id;                          /* -1 on 404/405 */
    int param_count;
    TekoRouteParam params[TEKO_ROUTE_MAX_PARAMS];
    int middleware_ids[TEKO_ROUTE_MAX_MW];   /* middleware chain, in order */
    int middleware_count;
} TekoRouteMatch;

/* ---------- opaque types ------------------------------------------------------- */
typedef struct TekoRouter    TekoRouter;
typedef struct TekoRouteNode TekoRouteNode; /* exposed for completeness; not dereferenced externally */

/* ---------- lifecycle ---------------------------------------------------------- */

/* Allocate an empty router (calloc, zero-init). Returns NULL on OOM. */
TekoRouter* teko_router_new(void);

/* Free the router and all its nodes. Safe on NULL. */
void teko_router_free(TekoRouter* r);

/* Remove all registered routes; keep the router alive (resets to empty state). */
void teko_router_reset(TekoRouter* r);

/* ---------- route registration ------------------------------------------------ */

/* Register a route.
 *   method          -- HTTP verb, NUL-terminated, max TEKO_ROUTE_MAX_METHOD-1 chars.
 *   path_pattern    -- URL path pattern, NUL-terminated, max TEKO_ROUTE_MAX_PATH-1 chars.
 *                      Segments starting with ':' are path-parameter captures.
 *                      No wildcard '*' segments (deferred to ROUTER-NATIVE Wave 2).
 *   handler_id      -- compile-time integer identifying the handler routine.
 *   middleware_ids  -- ordered array of middleware handler-ids (may be NULL).
 *   middleware_count-- length of middleware_ids; clamped to TEKO_ROUTE_MAX_MW.
 * Returns 0 on success, -1 on invalid args / capacity overflow / traversal attempt. */
int teko_router_add(TekoRouter* r,
                    const char* method,
                    const char* path_pattern,
                    int handler_id,
                    const int* middleware_ids,
                    int middleware_count);

/* ---------- dispatch ----------------------------------------------------------- */

/* Dispatch a request against the registered routes.
 *   method   -- HTTP verb, NUL-terminated.
 *   path     -- URL path (URL-decoded, no query string), NUL-terminated.
 *   headers  -- raw header block (may be NULL); reserved for future middleware use.
 * Returns a TekoRouteMatch:
 *   status == TEKO_ROUTE_OK  (200): handler_id set, params/middleware populated.
 *   status == TEKO_ROUTE_404 (404): path did not match any registered pattern.
 *   status == TEKO_ROUTE_405 (405): path matched but method did not. */
TekoRouteMatch teko_router_dispatch(const TekoRouter* r,
                                    const char* method,
                                    const char* path,
                                    const char* headers);

#endif /* TEKO_ROUTER_H */
