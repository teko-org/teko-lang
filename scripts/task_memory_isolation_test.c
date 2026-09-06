/**
 * scripts/task_memory_isolation_test.c — the F1/F2 guard: per-task memory discipline (tk_task,
 * tk_task_current) and the program region that outlives tasks (tk_region_program).
 *
 * Teko has NO raw-pointer surface, so `tk_region *` / `tk_task *` cannot be exercised from a
 * `.tkt` test. Like scripts/region_drop_subtree_test.c, this harness #includes teko_rt.c directly
 * (a self-contained, main()-less library file) and drives the primitives by hand, asserting the
 * ACTUAL C-level effect.
 *
 * THE ARMS, in this order and for this reason:
 *
 *   1. LIVENESS — assert FIRST that the two lanes really are two tasks: different root regions and
 *      DISTINCT region generations. Without this arm the corruption canary below would pass simply
 *      because there was never a second thing to corrupt (if tk_task_current() handed both lanes
 *      the same task, nothing can be cross-freed and the canary goes green over a blind
 *      instrument). This arm is what makes arm 2's green mean something.
 *
 *   2. THE CORRUPTION CANARY — the exact failure F1 exists to prevent. Lane B checkpoints the
 *      arena, lane A then allocates and fills a pattern, lane B rewinds and reallocates. With one
 *      process-global root (before F1) B's rewind frees A's block and B's next allocation hands
 *      back the very same address, shredding A's pattern. With a root per task, B's rewind cannot
 *      reach into A's root at all.
 *
 *   3. THE REVERSAL — run the identical canary against a SINGLE SHARED task (today's discipline)
 *      and require it to be DESTROYED. An instrument that cannot show the failure cannot certify
 *      its absence, so the guard proves it still detects the corruption it is looking for.
 *
 *   4. F2, both directions — an object in the PROGRAM region survives a task's tk_arena_pop AND
 *      that task's exit; the same object in a TASK ROOT does not. Survival is asserted by
 *      chunk-list REACHABILITY over live registries plus a byte check on memory that was never
 *      freed, so no assertion ever reads through a dangling pointer.
 *
 * Exit 0 on success; a failed assert() aborts (non-zero exit) with a clear source location.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* teko_rt.c keeps tk_task, the task struct and the current-task cell as file-scope statics, and
 * exposes no query API for them. Including it directly is the only way this harness can install a
 * given task on the lane (the plain assignments below) and read a registry back. */
#include "../src/runtime/teko_rt.c"

/** CANARY_BYTES — size of each lane's canary block; a whole number of 16-byte alignment steps. */
#define CANARY_BYTES 4096

/** LANE_A_PATTERN — the byte lane A writes and expects to still find. */
#define LANE_A_PATTERN 0xAB

/** LANE_B_PATTERN — the byte lane B overwrites recycled memory with. */
#define LANE_B_PATTERN 0xEE

/** PROGRAM_PATTERN — the byte the program-region object is stamped with. */
#define PROGRAM_PATTERN 0xC5

/**
 * lane_install — make `t` the task this flow of control runs on.
 *
 * The public lifecycle (tk_task_begin/tk_task_end) is a stack discipline, but the canary must
 * INTERLEAVE two live tasks, so the harness sets the current-task cell directly.
 *
 * @param t  the task to install; must not be NULL
 */
static void lane_install(tk_task *t) { tk_g_current_task = t; }

/**
 * lane_new — allocate a zeroed task, the state every fresh flow of control starts from.
 *
 * @return  a task with an empty memory discipline (no root, no marks, no parked blocks)
 */
static tk_task *lane_new(void) {
    tk_task *t = calloc(1, sizeof *t);
    assert(t != NULL);
    return t;
}

/**
 * bytes_matching — how many of the first `n` bytes at `p` still hold `want`.
 *
 * Counting (rather than asserting on the first mismatch) is what lets the guard report the two
 * numbers the reversal turns on: intact under per-task roots, shredded under a shared root.
 *
 * @param p     the block to inspect
 * @param n     how many bytes to inspect
 * @param want  the byte value expected at every position
 * @return      the count of positions still holding `want` (n = fully intact, 0 = fully destroyed)
 */
static size_t bytes_matching(const unsigned char *p, size_t n, unsigned char want) {
    size_t hits = 0;
    for (size_t i = 0; i < n; i += 1) if (p[i] == want) hits += 1;
    return hits;
}

/**
 * addr_reachable — is `addr` inside a chunk of some region still live on `registry`?
 *
 * The survival test that never dereferences a dangling pointer: it walks only registries that are
 * still live and compares integers, so a freed region simply is not on the list to be walked.
 *
 * @param registry  head of a live-region registry list (NULL = empty registry)
 * @param addr      the address to look for
 * @return          1 if some live region on the list still backs `addr`, 0 otherwise
 */
static int addr_reachable(const tk_region *registry, uintptr_t addr) {
    for (const tk_region *r = registry; r != NULL; r = r->reg_next) {
        for (const struct tk_chunk *c = r->head; c != NULL; c = c->next) {
            uintptr_t base = (uintptr_t)c->data;
            if (addr >= base && addr < base + c->cap) return 1;
        }
    }
    return 0;
}

/**
 * arm_liveness — prove the two lanes are genuinely two tasks BEFORE anything else is claimed.
 *
 * @param a  the first lane's task
 * @param b  the second lane's task
 */
static void arm_liveness(tk_task *a, tk_task *b) {
    lane_install(a);
    tk_region *root_a = tk_region_root();
    uint64_t gen_a = root_a->gen;

    lane_install(b);
    tk_region *root_b = tk_region_root();
    uint64_t gen_b = root_b->gen;

    assert(a != b);
    assert(root_a != NULL && root_b != NULL);
    assert(root_a != root_b);
    assert(gen_a != gen_b);
    assert(a->root == root_a && b->root == root_b);
    assert(a->regs != b->regs);
    printf("liveness: two tasks, roots %p vs %p, generations %llu vs %llu\n",
           (void *)root_a, (void *)root_b,
           (unsigned long long)gen_a, (unsigned long long)gen_b);
}

/**
 * arm_canary_per_task — lane B checkpoints and rewinds while lane A holds a live block.
 *
 * @param a  the lane whose block must survive
 * @param b  the lane doing the checkpoint/rewind
 * @return   how many of lane A's CANARY_BYTES still hold LANE_A_PATTERN
 */
static size_t arm_canary_per_task(tk_task *a, tk_task *b) {
    lane_install(b);
    tk_arena_push();

    lane_install(a);
    unsigned char *held = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(held, LANE_A_PATTERN, CANARY_BYTES);

    lane_install(b);
    tk_arena_pop();
    unsigned char *recycled = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(recycled, LANE_B_PATTERN, CANARY_BYTES);

    lane_install(a);
    assert(held != recycled);
    return bytes_matching(held, CANARY_BYTES, LANE_A_PATTERN);
}

/**
 * arm_canary_shared — the identical canary with BOTH lanes on one task (the pre-F1 discipline).
 *
 * The rewind now reaches the block, so the next allocation is handed the same address back. If
 * this returns anything but 0 the canary is measuring something other than cross-lane corruption.
 *
 * @return  how many of the CANARY_BYTES still hold LANE_A_PATTERN (0 = correctly destroyed)
 */
static size_t arm_canary_shared(void) {
    tk_task *shared = lane_new();
    lane_install(shared);

    tk_arena_push();
    unsigned char *held = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(held, LANE_A_PATTERN, CANARY_BYTES);

    tk_arena_pop();
    unsigned char *recycled = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(recycled, LANE_B_PATTERN, CANARY_BYTES);

    assert(held == recycled);   // one root: the rewind really did hand the address back
    size_t survivors = bytes_matching(held, CANARY_BYTES, LANE_A_PATTERN);

    tk_registry_free(&shared->regs);
    free(shared);
    return survivors;
}

/**
 * arm_program_region — F2 in both directions, across a task's rewind AND its exit.
 *
 * Allocates one object in the program region and one in the task's own root, then rewinds and
 * ends the task. The program object must still be backed by a live region and still hold its
 * pattern; the task-root object must be backed by nothing at all.
 */
static void arm_program_region(void) {
    tk_task *outer = tk_task_current();
    tk_task *worker = lane_new();
    lane_install(worker);

    unsigned char *in_program = (unsigned char *)tk_region_alloc(tk_region_program(), CANARY_BYTES);
    memset(in_program, PROGRAM_PATTERN, CANARY_BYTES);
    uintptr_t program_addr = (uintptr_t)in_program;

    unsigned char *in_task_root = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(in_task_root, PROGRAM_PATTERN, CANARY_BYTES);
    uintptr_t task_root_addr = (uintptr_t)in_task_root;

    tk_arena_push();
    (void)tk_alloc(CANARY_BYTES);
    tk_arena_pop();
    assert(addr_reachable(tk_g_program_regs, program_addr));   // survives the task's own rewind

    tk_task_end(outer);

    assert(addr_reachable(tk_g_program_regs, program_addr));   // survives the task's EXIT
    assert(bytes_matching(in_program, CANARY_BYTES, PROGRAM_PATTERN) == CANARY_BYTES);
    assert(!addr_reachable(tk_g_program_regs, task_root_addr));
    assert(!addr_reachable(tk_task_current()->regs, task_root_addr));
    printf("program region: survives pop and task exit; task root does not\n");
}

/**
 * arm_reuse_after_end — a task that is ENDED and then allocated from again holds no stale witness.
 *
 * tk_task_end frees the task's regions, so every parked free-list block, live-tail push-cache entry
 * and arena mark it still held names reclaimed memory. A task struct that outlives its own end (the
 * main task is never freed) would otherwise false-hit on a dead region. Ending the MAIN task and
 * then allocating from it again is the shape that exposes that, so it is the shape tested.
 */
static void arm_reuse_after_end(void) {
    lane_install(&tk_g_main_task);
    unsigned char *before = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(before, LANE_A_PATTERN, CANARY_BYTES);
    tk_arena_push();

    tk_task_end(&tk_g_main_task);

    assert(tk_g_main_task.root == NULL);
    assert(tk_g_main_task.regs == NULL);
    assert(tk_g_main_task.arena_msp == 0);
    assert(tk_g_main_task.free_large == NULL);

    unsigned char *after = (unsigned char *)tk_alloc(CANARY_BYTES);
    memset(after, LANE_B_PATTERN, CANARY_BYTES);
    assert(tk_g_main_task.root != NULL);   // a fresh root, built lazily on the first allocation
    printf("reuse after end: no stale marks, parked blocks or tail witnesses\n");
}

/**
 * main — run the four arms in order, print the two numbers of the reversal, and assert them.
 *
 * @return  0 on success (every assertion held); a failed assert aborts non-zero.
 */
int main(void) {
    tk_task *lane_a = lane_new();
    tk_task *lane_b = lane_new();

    arm_liveness(lane_a, lane_b);

    size_t per_task = arm_canary_per_task(lane_a, lane_b);
    size_t shared = arm_canary_shared();

    printf("canary bytes intact: per-task roots %zu/%d, shared root %zu/%d\n",
           per_task, CANARY_BYTES, shared, CANARY_BYTES);

    assert(per_task == (size_t)CANARY_BYTES);   // F1: no cross-task corruption
    assert(shared == 0);                        // reversal: the canary can still see the failure

    lane_install(lane_a); tk_registry_free(&lane_a->regs);
    lane_install(lane_b); tk_registry_free(&lane_b->regs);
    lane_install(&tk_g_main_task);
    free(lane_a);
    free(lane_b);

    arm_program_region();
    arm_reuse_after_end();

    printf("task memory isolation: OK\n");
    return 0;
}
