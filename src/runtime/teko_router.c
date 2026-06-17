/* teko_router.c -- AOT static radix-tree router (Phase 19, ROUTER-CORE Wave 0).
 * Target-agnostic: no OS calls, no sockets, no libc beyond string.h / stdlib.h.
 * See teko_router.h for the full design rationale and security posture. */

#include "teko_router.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Internal types
 * ========================================================================== */

/* A method/handler pair stored at a leaf node. */
typedef struct {
    char method[TEKO_ROUTE_MAX_METHOD]; /* NUL-terminated, e.g. "GET" */
    int  handler_id;
    int  middleware_ids[TEKO_ROUTE_MAX_MW];
    int  middleware_count;
} TekoRouteEntry;

/* A node in the radix tree.
 * Each node matches ONE path segment (literal or parameter).
 * Literal nodes sort before param nodes (enforced in teko_router_add). */
struct TekoRouteNode {
    /* Segment stored in this node.  For param nodes, stored WITHOUT the leading
     * ':' so the capture key is directly usable (e.g. ":id" stored as "id"). */
    char segment[64];
    int  is_param; /* 1 if this is a ':name' parameter node, 0 for literal */

    /* Children (up to TEKO_ROUTE_MAX_CHILDREN) */
    struct TekoRouteNode* children[TEKO_ROUTE_MAX_CHILDREN];
    int child_count;

    /* Method entries at this leaf (populated only if this node terminates a route) */
    TekoRouteEntry entries[TEKO_ROUTE_MAX_METHODS];
    int entry_count;
};

struct TekoRouter {
    TekoRouteNode* root;  /* virtual root node (segment="" is_param=0) */
    int route_count;      /* total registered routes (capped at TEKO_ROUTE_MAX_ROUTES) */
};

/* ============================================================================
 * Helpers
 * ========================================================================== */

/* Bounded strlen -- returns length up to max, stops at max (does NOT NUL-check
 * beyond max). Safe on attacker-controlled input. */
static int rt_strnlen_safe(const char* s, int max) {
    if (!s) return 0;
    int i = 0;
    while (i < max && s[i] != '\0') i++;
    return i;
}

/* Bounded string copy into a fixed buffer of size `dst_sz` (including NUL).
 * Always NUL-terminates. Returns 0 on success, -1 if src was truncated. */
static int rt_strncpy_safe(char* dst, int dst_sz, const char* src, int src_len) {
    int copy = (src_len < dst_sz - 1) ? src_len : (dst_sz - 1);
    memcpy(dst, src, (size_t)copy);
    dst[copy] = '\0';
    return (copy < src_len) ? -1 : 0;
}

/* Validate a single path segment: reject ".", "..", empty, or any segment
 * containing '\0' or '/'. Returns 0 if valid, -1 if rejected. */
static int rt_validate_segment(const char* seg, int len) {
    if (len == 0) return -1;
    if (len == 1 && seg[0] == '.') return -1;
    if (len == 2 && seg[0] == '.' && seg[1] == '.') return -1;
    for (int i = 0; i < len; i++) {
        if (seg[i] == '\0' || seg[i] == '/') return -1;
    }
    return 0;
}

/* Split `path` on '/' into at most `max_segs` segments.
 * `segs[i]` points into `buf` (must be a copy of `path` of length path_len).
 * `seg_lens[i]` gives each segment length.
 * Returns the number of segments, or -1 on overflow / empty-segment error. */
static int rt_split_path(char* buf, int path_len,
                          const char** segs, int* seg_lens, int max_segs) {
    int n = 0;
    int start = 0;

    /* Skip leading '/' */
    if (path_len > 0 && buf[0] == '/') start = 1;

    for (int i = start; i <= path_len; i++) {
        if (i == path_len || buf[i] == '/') {
            int len = i - start;
            if (len == 0) {
                /* trailing slash or double slash -- skip empty segment */
                start = i + 1;
                continue;
            }
            if (n >= max_segs) return -1; /* overflow */
            segs[n]    = buf + start;
            seg_lens[n] = len;
            n++;
            start = i + 1;
        }
    }
    return n;
}

/* Allocate and zero-initialize a new TekoRouteNode. */
static TekoRouteNode* rt_node_new(void) {
    return (TekoRouteNode*)calloc(1, sizeof(TekoRouteNode));
}

/* Recursively free a node and all its children. Safe on NULL. */
static void rt_node_free(TekoRouteNode* n) {
    if (!n) return;
    for (int i = 0; i < n->child_count; i++) {
        rt_node_free(n->children[i]);
    }
    free(n);
}

/* Find a child of `node` matching the given segment string (exact, case-sensitive).
 * `is_param` must match too so literal "id" and param "id" stay distinct. */
static TekoRouteNode* rt_find_child(TekoRouteNode* node,
                                     const char* seg, int seg_len, int is_param) {
    for (int i = 0; i < node->child_count; i++) {
        TekoRouteNode* c = node->children[i];
        if (c->is_param != is_param) continue;
        int clen = rt_strnlen_safe(c->segment, (int)sizeof(c->segment));
        if (clen == seg_len && memcmp(c->segment, seg, (size_t)seg_len) == 0) {
            return c;
        }
    }
    return NULL;
}

/* Insert a new child at `node`.  Literals are inserted before params to ensure
 * literal matches always win over parameter captures at the same depth. */
static int rt_insert_child(TekoRouteNode* node, TekoRouteNode* child) {
    if (node->child_count >= TEKO_ROUTE_MAX_CHILDREN) return -1;
    if (!child->is_param) {
        /* Insert literal before the first param child */
        int insert_pos = node->child_count;
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i]->is_param) { insert_pos = i; break; }
        }
        /* Shift param children one slot to the right */
        for (int i = node->child_count; i > insert_pos; i--) {
            node->children[i] = node->children[i - 1];
        }
        node->children[insert_pos] = child;
    } else {
        /* Append param child at the end */
        node->children[node->child_count] = child;
    }
    node->child_count++;
    return 0;
}

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

TekoRouter* teko_router_new(void) {
    TekoRouter* r = (TekoRouter*)calloc(1, sizeof(TekoRouter));
    if (!r) return NULL;
    r->root = rt_node_new();
    if (!r->root) { free(r); return NULL; }
    return r;
}

void teko_router_free(TekoRouter* r) {
    if (!r) return;
    rt_node_free(r->root);
    free(r);
}

void teko_router_reset(TekoRouter* r) {
    if (!r) return;
    rt_node_free(r->root);
    r->root = rt_node_new(); /* re-allocate a clean root */
    r->route_count = 0;
    /* If the alloc failed, root is NULL -- teko_router_add will fail-loud on next call */
}

/* ============================================================================
 * Route registration
 * ========================================================================== */

int teko_router_add(TekoRouter* r,
                    const char* method,
                    const char* path_pattern,
                    int handler_id,
                    const int* middleware_ids,
                    int middleware_count) {
    /* --- argument validation -------------------------------------------------- */
    if (!r || !r->root || !method || !path_pattern) return -1;
    if (r->route_count >= TEKO_ROUTE_MAX_ROUTES) return -1;

    int method_len = rt_strnlen_safe(method, TEKO_ROUTE_MAX_METHOD + 1);
    if (method_len == 0 || method_len > TEKO_ROUTE_MAX_METHOD - 1) return -1;

    int path_len = rt_strnlen_safe(path_pattern, TEKO_ROUTE_MAX_PATH + 1);
    if (path_len == 0 || path_len > TEKO_ROUTE_MAX_PATH - 1) return -1;

    if (middleware_count < 0) middleware_count = 0;
    if (middleware_count > TEKO_ROUTE_MAX_MW) middleware_count = TEKO_ROUTE_MAX_MW;

    /* --- split path into segments -------------------------------------------- */
    char path_buf[TEKO_ROUTE_MAX_PATH];
    memcpy(path_buf, path_pattern, (size_t)path_len);
    path_buf[path_len] = '\0';

    const char* segs[TEKO_ROUTE_MAX_DEPTH];
    int seg_lens[TEKO_ROUTE_MAX_DEPTH];
    int nseg = rt_split_path(path_buf, path_len, segs, seg_lens, TEKO_ROUTE_MAX_DEPTH);
    if (nseg < 0) return -1; /* too deep */

    /* validate each segment (path traversal guard) */
    for (int i = 0; i < nseg; i++) {
        /* For param segments, validate the part after ':' */
        const char* seg = segs[i];
        int len = seg_lens[i];
        int is_param_seg = (len > 1 && seg[0] == ':');
        const char* check_seg = is_param_seg ? seg + 1 : seg;
        int check_len = is_param_seg ? len - 1 : len;
        if (rt_validate_segment(check_seg, check_len) != 0) return -1;
        /* Bare ':' with no name is invalid */
        if (is_param_seg && check_len == 0) return -1;
    }

    /* --- walk/build the radix tree ------------------------------------------- */
    TekoRouteNode* cur = r->root;
    for (int i = 0; i < nseg; i++) {
        const char* seg = segs[i];
        int len = seg_lens[i];
        int is_param = (len > 1 && seg[0] == ':') ? 1 : 0;
        /* Param node stores the key without the leading ':' */
        const char* store_seg = is_param ? seg + 1 : seg;
        int store_len = is_param ? len - 1 : len;

        TekoRouteNode* child = rt_find_child(cur, store_seg, store_len, is_param);
        if (!child) {
            child = rt_node_new();
            if (!child) return -1;
            child->is_param = is_param;
            if (rt_strncpy_safe(child->segment, (int)sizeof(child->segment),
                                store_seg, store_len) != 0) {
                free(child);
                return -1; /* segment name too long for internal buffer */
            }
            if (rt_insert_child(cur, child) != 0) {
                free(child);
                return -1; /* TEKO_ROUTE_MAX_CHILDREN exceeded */
            }
        }
        cur = child;
    }

    /* --- add method entry to the leaf node ------------------------------------ */
    if (cur->entry_count >= TEKO_ROUTE_MAX_METHODS) return -1;

    TekoRouteEntry* e = &cur->entries[cur->entry_count];
    /* calloc in rt_node_new guarantees zero-init; just fill in the fields */
    if (rt_strncpy_safe(e->method, (int)sizeof(e->method), method, method_len) != 0) {
        return -1;
    }
    e->handler_id = handler_id;
    e->middleware_count = middleware_count;
    if (middleware_count > 0 && middleware_ids) {
        memcpy(e->middleware_ids, middleware_ids,
               (size_t)middleware_count * sizeof(int));
    }
    cur->entry_count++;
    r->route_count++;
    return 0;
}

/* ============================================================================
 * Dispatch
 * ========================================================================== */

/* Match state threaded through recursive descent */
typedef struct {
    const char** segs;
    int*         seg_lens;
    int          nseg;
    TekoRouteParam params[TEKO_ROUTE_MAX_PARAMS];
    int          param_count;
} MatchState;

/* Recursive descent through the radix tree.
 * Returns the leaf TekoRouteNode if path matches, NULL otherwise.
 * Captured params are stored into `ms->params`. */
static TekoRouteNode* rt_match(TekoRouteNode* node, int depth, MatchState* ms) {
    if (depth == ms->nseg) {
        /* Reached the end of the path -- this node is the leaf. */
        return (node->entry_count > 0) ? node : NULL;
    }

    const char* seg    = ms->segs[depth];
    int         seg_len = ms->seg_lens[depth];

    /* Try literal children first (they are ordered before params by rt_insert_child) */
    for (int i = 0; i < node->child_count; i++) {
        TekoRouteNode* c = node->children[i];
        if (c->is_param) break; /* all literals exhausted; params follow */
        int clen = rt_strnlen_safe(c->segment, (int)sizeof(c->segment));
        if (clen == seg_len && memcmp(c->segment, seg, (size_t)seg_len) == 0) {
            TekoRouteNode* leaf = rt_match(c, depth + 1, ms);
            if (leaf) return leaf;
        }
    }

    /* Try param children (capture the segment value) */
    int saved_param_count = ms->param_count;
    for (int i = 0; i < node->child_count; i++) {
        TekoRouteNode* c = node->children[i];
        if (!c->is_param) continue;
        if (ms->param_count >= TEKO_ROUTE_MAX_PARAMS) continue;

        /* Capture key = c->segment, value = seg */
        TekoRouteParam* p = &ms->params[ms->param_count];
        int klen = rt_strnlen_safe(c->segment, (int)sizeof(c->segment));
        rt_strncpy_safe(p->key,   (int)sizeof(p->key),   c->segment, klen);
        rt_strncpy_safe(p->value, (int)sizeof(p->value), seg, seg_len);
        ms->param_count++;

        TekoRouteNode* leaf = rt_match(c, depth + 1, ms);
        if (leaf) return leaf;

        /* Backtrack: reset param count on failed branch */
        ms->param_count = saved_param_count;
    }

    return NULL;
}

TekoRouteMatch teko_router_dispatch(const TekoRouter* r,
                                    const char* method,
                                    const char* path,
                                    const char* headers) {
    TekoRouteMatch result;
    memset(&result, 0, sizeof(result));
    result.status     = TEKO_ROUTE_404;
    result.handler_id = -1;

    /* Suppress unused-parameter warning for headers (reserved for future middleware) */
    (void)headers;

    /* --- argument validation -------------------------------------------------- */
    if (!r || !r->root || !method || !path) return result;

    int method_len = rt_strnlen_safe(method, TEKO_ROUTE_MAX_METHOD + 1);
    if (method_len == 0 || method_len > TEKO_ROUTE_MAX_METHOD - 1) return result;

    int path_len = rt_strnlen_safe(path, TEKO_ROUTE_MAX_PATH + 1);
    if (path_len == 0 || path_len > TEKO_ROUTE_MAX_PATH - 1) return result;

    /* --- split request path --------------------------------------------------- */
    char path_buf[TEKO_ROUTE_MAX_PATH];
    memcpy(path_buf, path, (size_t)path_len);
    path_buf[path_len] = '\0';

    const char* segs[TEKO_ROUTE_MAX_DEPTH];
    int seg_lens[TEKO_ROUTE_MAX_DEPTH];
    int nseg = rt_split_path(path_buf, path_len, segs, seg_lens, TEKO_ROUTE_MAX_DEPTH);
    if (nseg < 0) return result; /* path too deep -- 404 */

    /* Root path "/" has nseg == 0; match against root node entries if any */

    /* --- radix tree match ----------------------------------------------------- */
    MatchState ms;
    memset(&ms, 0, sizeof(ms));
    ms.segs     = segs;
    ms.seg_lens = seg_lens;
    ms.nseg     = nseg;

    TekoRouteNode* leaf = rt_match(r->root, 0, &ms);
    if (!leaf) {
        result.status = TEKO_ROUTE_404;
        return result;
    }

    /* Path matched -- find the method entry */
    char norm_method[TEKO_ROUTE_MAX_METHOD];
    rt_strncpy_safe(norm_method, (int)sizeof(norm_method), method, method_len);

    for (int i = 0; i < leaf->entry_count; i++) {
        TekoRouteEntry* e = &leaf->entries[i];
        int elen = rt_strnlen_safe(e->method, TEKO_ROUTE_MAX_METHOD);
        if (elen == method_len &&
            memcmp(e->method, norm_method, (size_t)method_len) == 0) {
            /* Full match */
            result.status     = TEKO_ROUTE_OK;
            result.handler_id = e->handler_id;
            /* Copy params */
            result.param_count = ms.param_count;
            if (ms.param_count > 0) {
                memcpy(result.params, ms.params,
                       (size_t)ms.param_count * sizeof(TekoRouteParam));
            }
            /* Copy middleware */
            result.middleware_count = e->middleware_count;
            if (e->middleware_count > 0) {
                memcpy(result.middleware_ids, e->middleware_ids,
                       (size_t)e->middleware_count * sizeof(int));
            }
            return result;
        }
    }

    /* Path matched but method did not */
    result.status = TEKO_ROUTE_405;
    return result;
}
