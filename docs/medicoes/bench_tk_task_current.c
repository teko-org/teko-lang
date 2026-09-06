/* Microbenchmark: cost of the tk_task_current() accessor per allocation.
   Three shapes, same allocation loop:
     A = today: plain static globals (baseline)
     B = _Thread_local tk_task* fetched PER ACCESS (naive F1)
     C = _Thread_local fetched ONCE per call, passed down (tk_alloc_in mitigation)
   The loop mirrors tk_alloc -> tk_region_root() -> tk_region_alloc(r,n) with its
   two `r == root` compares. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define ALIGN 16
#define CHUNKCAP (1u << 20)

typedef struct chunk { struct chunk *next; size_t cap, used; char data[]; } chunk;
typedef struct region { chunk *head; uint64_t gen; } region;
typedef struct task { region *root; region *regs; int msp; } task;

static double now(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static chunk *mkchunk(size_t cap) {
    chunk *c = malloc(sizeof(chunk) + cap);
    c->next = NULL; c->cap = cap; c->used = 0; return c;
}

/* ---------- A: today's globals ---------- */
static region *g_root_A = NULL;
static region *root_A(void) { return g_root_A; }
static void *alloc_in_A(region *r, size_t n) {
    size_t an = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (r == g_root_A) { if (0) return NULL; }              /* free-list probe: r==root compare */
    if (r->head) {
        size_t base = (r->head->used + ALIGN - 1) & ~(size_t)(ALIGN - 1);
        if (base <= r->head->cap && an <= r->head->cap - base) {
            r->head->used = base + an; return r->head->data + base;
        }
    }
    chunk *c = mkchunk(an > CHUNKCAP ? an : CHUNKCAP);
    c->used = an; c->next = r->head; r->head = c; return c->data;
}
static void *alloc_A(size_t n) { return alloc_in_A(root_A(), n); }

/* ---------- B: _Thread_local, fetched per access ---------- */
static _Thread_local task *g_cur_B = NULL;
static task *cur_B(void) { return g_cur_B; }
static region *root_B(void) { return cur_B()->root; }
static void *alloc_in_B(region *r, size_t n) {
    size_t an = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (r == cur_B()->root) { if (0) return NULL; }          /* free-list probe re-fetches */
    if (r->head) {
        size_t base = (r->head->used + ALIGN - 1) & ~(size_t)(ALIGN - 1);
        if (base <= r->head->cap && an <= r->head->cap - base) {
            r->head->used = base + an; return r->head->data + base;
        }
    }
    chunk *c = mkchunk(an > CHUNKCAP ? an : CHUNKCAP);
    c->used = an; c->next = r->head; r->head = c; return c->data;
}
static void *alloc_B(size_t n) { return alloc_in_B(root_B(), n); }

/* ---------- C: _Thread_local fetched ONCE, threaded through ---------- */
static _Thread_local task *g_cur_C = NULL;
static task *cur_C(void) { return g_cur_C; }
static void *alloc_in_C(task *t, region *r, size_t n) {
    size_t an = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (r == t->root) { if (0) return NULL; }
    if (r->head) {
        size_t base = (r->head->used + ALIGN - 1) & ~(size_t)(ALIGN - 1);
        if (base <= r->head->cap && an <= r->head->cap - base) {
            r->head->used = base + an; return r->head->data + base;
        }
    }
    chunk *c = mkchunk(an > CHUNKCAP ? an : CHUNKCAP);
    c->used = an; c->next = r->head; r->head = c; return c->data;
}
static void *alloc_C(size_t n) { task *t = cur_C(); return alloc_in_C(t, t->root, n); }

#define N 50000000ull

int main(void) {
    region rA = {0,1}, rB = {0,1}, rC = {0,1};
    task tB = {&rB,0,0}, tC = {&rC,0,0};
    g_root_A = &rA; g_cur_B = &tB; g_cur_C = &tC;
    volatile size_t sink = 0;
    double best[3] = {1e9,1e9,1e9};

    for (int rep = 0; rep < 5; rep++) {
        double t0 = now();
        for (unsigned long long i = 0; i < N; i++) sink += (size_t)alloc_A(16 + (i & 15));
        double t1 = now();
        for (unsigned long long i = 0; i < N; i++) sink += (size_t)alloc_B(16 + (i & 15));
        double t2 = now();
        for (unsigned long long i = 0; i < N; i++) sink += (size_t)alloc_C(16 + (i & 15));
        double t3 = now();
        if (t1-t0 < best[0]) best[0] = t1-t0;
        if (t2-t1 < best[1]) best[1] = t2-t1;
        if (t3-t2 < best[2]) best[2] = t3-t2;
    }
    printf("A globals (today)      : %.3f s  = %.3f ns/alloc\n", best[0], best[0]/N*1e9);
    printf("B TLS per-access       : %.3f s  = %.3f ns/alloc  (+%.3f ns, %+.1f%%)\n",
           best[1], best[1]/N*1e9, (best[1]-best[0])/N*1e9, (best[1]/best[0]-1)*100);
    printf("C TLS fetched once     : %.3f s  = %.3f ns/alloc  (+%.3f ns, %+.1f%%)\n",
           best[2], best[2]/N*1e9, (best[2]-best[0])/N*1e9, (best[2]/best[0]-1)*100);
    return sink == 12345 ? 1 : 0;
}
