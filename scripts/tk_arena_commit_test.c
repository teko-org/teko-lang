/**
 * scripts/tk_arena_commit_test.c — direct unit test for tk_arena_commit (the enabling primitive
 * folding the arena design's Crumb 1, staged off — no compiler source calls it yet).
 *
 * Mirrors region_drop_subtree_test.c: Teko has no raw-pointer surface to observe a root-region
 * bump-position rewind/keep directly, so this drives tk_arena_push / tk_arena_pop /
 * tk_arena_commit by hand and asserts the ACTUAL C-level effect (the root region's chunk + used-
 * offset bookkeeping) rather than a string match on emitted C.
 *
 * A single 64-byte pre-warm allocation forces the root region to already have a live head chunk
 * with room to spare, so every assertion below observes pure bump-OFFSET movement within ONE
 * chunk — never a chunk teardown/rebuild, which would depend on malloc's own address-reuse
 * heuristics rather than the arena's own mark/rewind bookkeeping.
 *
 * Asserts:
 *   1. tk_arena_pop REWINDS: a same-size allocation right after a pop lands at the IDENTICAL
 *      address as the one made before it — the bump offset was rewound back to the mark.
 *   2. tk_arena_commit KEEPS: an allocation right after a commit lands STRICTLY PAST the
 *      committed block (the bump offset was never reset), and the committed bytes still read
 *      back correctly — commit never reclaims them.
 *   3. Depth bookkeeping (tk_arena_msp) stays balanced across mixed push/commit/pop cycles —
 *      neither leaks nor underflows over repeated cycles.
 *
 * Exit 0 on success; a failed assert() aborts (non-zero exit) with a clear source location.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* teko_rt.c defines tk_arena_push/tk_arena_pop/tk_arena_commit and their file-scope mark stack as
 * `static` — including it directly (rather than linking) is the only way this harness can drive
 * tk_region_alloc against the SAME root region those functions checkpoint. */
#include "../src/runtime/teko_rt.c"

/**
 * main — pre-warm the root region, then assert pop's rewind, commit's keep, and balanced depth
 * bookkeeping directly against the C-level bump-offset state.
 *
 * @return  0 on success (every assertion held); a failed assert aborts non-zero.
 */
int main(void) {
    tk_region *root = tk_region_root();
    tk_region_alloc(root, 64);   /* pre-warm: one live chunk, plenty of spare capacity below */

    /* (1) pop REWINDS: the second alloc reuses the first's exact address. */
    tk_arena_push();
    void *a1 = tk_region_alloc(root, 64);
    memset(a1, 0x11, 64);
    tk_arena_pop();
    void *a2 = tk_region_alloc(root, 64);
    assert(a2 == a1);

    /* (2) commit KEEPS: the second alloc lands strictly past the committed block, and the
     * committed bytes are still exactly what was written — never reclaimed. */
    tk_arena_push();
    void *b1 = tk_region_alloc(root, 64);
    memset(b1, 0x22, 64);
    tk_arena_commit();
    void *b2 = tk_region_alloc(root, 64);
    assert(b2 != b1);
    assert((char *)b2 >= (char *)b1 + 64);
    {
        unsigned char *p = (unsigned char *)b1;
        for (int i = 0; i < 64; i += 1) assert(p[i] == 0x22);
    }

    /* (3) balanced depth across mixed push/commit/pop cycles. */
    assert(tk_arena_msp == 0);
    tk_arena_push();
    tk_arena_push();
    tk_arena_commit();
    tk_arena_pop();
    assert(tk_arena_msp == 0);
    for (int i = 0; i < 50; i += 1) {
        tk_arena_push();
        tk_region_alloc(root, 32);
        if (i % 2 == 0) { tk_arena_commit(); } else { tk_arena_pop(); }
    }
    assert(tk_arena_msp == 0);

    printf("tk_arena_commit_test: PASS (pop rewinds, commit keeps, depth stays balanced)\n");
    return 0;
}
