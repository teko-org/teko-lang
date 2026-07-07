/**
 * scripts/region_drop_subtree_test.c — direct unit test for tk_region_drop_subtree (#337).
 *
 * Teko has NO raw-pointer surface, so `tk_region *` cannot be exercised from a `.tkt` test the
 * way every other runtime primitive is (indirectly, through an allocation-heavy Teko-level
 * program). This is the ONE proof close enough to the metal: it #includes teko_rt.c directly (a
 * self-contained, main()-less library file — see its own header comment) and drives
 * tk_region_new / tk_region_drop_subtree by hand, asserting on the ACTUAL C-level effect (chunk
 * bytes freed, region count) rather than a string match on emitted C (which is all
 * cgt_emit_adopt, the codegen unit test, can observe).
 *
 * Builds a parent -> child -> grandchild region chain (mirroring the adopter -> per-object child
 * region tree emit_adopt produces), allocates real bytes in each so the drop is observable, then
 * calls tk_region_drop_subtree on the PARENT and asserts:
 *   1. TEKO_ARENA_OBS accounting sees exactly 3 regions dropped and a non-zero reclaimed-byte sum
 *      (the AC#2 observable: the subtree walk actually frees, not leaks).
 *   2. A SIBLING region (a peer of the parent, NOT reachable via ->parent to the dropped root) is
 *      left untouched — proves the ancestor-reaches-root predicate does not over-collect.
 *   3. tk_region_drop_subtree is NULL-tolerant (a second call / a NULL root is a no-op, not a
 *      crash) — the same idempotency tk_region_drop itself guarantees.
 *
 * Exit 0 on success; a failed assert() aborts (non-zero exit) with a clear source location.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* teko_rt.c defines its own obs statics as `static` file-scope — including it directly (rather
 * than linking) is the only way this harness can read tk_obs_regions_dropped/tk_obs_drop_bytes
 * after the call, since the runtime deliberately exposes NO public obs-query API (the file dump
 * at TEKO_ARENA_OBS is the only sanctioned surface, and this harness needs a numeric assert). */
#include "../src/runtime/teko_rt.c"

/**
 * main — build parent/child/grandchild + an untouched sibling, subtree-drop the parent, and
 * assert the C-level effect directly (obs counters, not string-matched C).
 *
 * @return  0 on success (every assertion held); a failed assert aborts non-zero.
 */
int main(void) {
    tk_obs_on = 1;   /* force obs accounting on without depending on getenv/atexit ordering */

    tk_region *root = tk_region_new(NULL);
    tk_region *parent = tk_region_new(root);
    tk_region *child = tk_region_new(parent);
    tk_region *grandchild = tk_region_new(child);
    tk_region *sibling = tk_region_new(root);   /* NOT under `parent` — must survive the subtree drop */

    memset(tk_region_alloc(parent, 64), 0x11, 64);
    memset(tk_region_alloc(child, 64), 0x22, 64);
    memset(tk_region_alloc(grandchild, 64), 0x33, 64);
    memset(tk_region_alloc(sibling, 64), 0x44, 64);

    unsigned long long dropped_before = tk_obs_regions_dropped;
    unsigned long long bytes_before = tk_obs_drop_bytes;

    tk_region_drop_subtree(parent);

    assert(tk_obs_regions_dropped - dropped_before == 3);   /* parent + child + grandchild — NOT the sibling */
    assert(tk_obs_drop_bytes > bytes_before);                /* AC#2: a non-zero reclaimed byte count */

    /* the registry must no longer list parent/child/grandchild: walk tk_g_regs and confirm none
     * of the three dropped pointers still appear (a stale registry entry would be a use-after-free
     * waiting to happen on the next tk_region_drop_subtree/tk_regions_free_all sweep). */
    for (tk_region *r = tk_g_regs; r != NULL; r = r->reg_next) {
        assert(r != parent);
        assert(r != child);
        assert(r != grandchild);
    }

    /* the sibling (a peer under root, not a descendant of parent) must still be live and readable. */
    int sibling_still_live = 0;
    for (tk_region *r = tk_g_regs; r != NULL; r = r->reg_next) {
        if (r == sibling) { sibling_still_live = 1; break; }
    }
    assert(sibling_still_live);

    /* NULL-tolerant + idempotent: neither call may crash or double-count. */
    tk_region_drop_subtree(NULL);
    unsigned long long dropped_after_noop = tk_obs_regions_dropped;
    tk_region_drop_subtree(parent);   /* parent is already gone from the registry -> a no-op sweep */
    assert(tk_obs_regions_dropped == dropped_after_noop);

    tk_region_drop_subtree(sibling);
    tk_region_drop_subtree(root);

    printf("region_drop_subtree_test: PASS (3 regions reclaimed, sibling untouched, idempotent)\n");
    return 0;
}
