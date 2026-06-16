#include "teko_object.h"
#include <stdlib.h>

// Phase 15 (15.A) — object model runtime. See teko_object.h. Pure C (malloc + an index into a
// register-width cell vector), no scheduler/threads dependency, so it is the one source of truth
// for both the native runner (teko_rt_object_* wrappers) and the wasm32 reactor.

struct TekoObject {
    int      nfields;
    intptr_t cells[]; // flexible array: nfields zero-initialized POINTER-width cells (Windows-safe)
};

TekoObject* teko_object_new(int nfields) {
    if (nfields < 0) nfields = 0;
    if (nfields > TEKO_OBJECT_MAX_FIELDS) nfields = TEKO_OBJECT_MAX_FIELDS;
    // calloc zero-initializes the header + every cell (the Windows wild-free lesson: never leave
    // an allocation's fields unset).
    TekoObject* o = (TekoObject*)calloc(1, sizeof(TekoObject) + (size_t)nfields * sizeof(intptr_t));
    if (!o) return NULL;
    o->nfields = nfields;
    return o;
}

void teko_object_free(TekoObject* o) {
    free(o);
}

void teko_object_set(TekoObject* o, int idx, intptr_t value) {
    if (!o || idx < 0 || idx >= o->nfields) return; // defensive: never write OOB
    o->cells[idx] = value;
}

intptr_t teko_object_get(const TekoObject* o, int idx) {
    if (!o || idx < 0 || idx >= o->nfields) return 0; // defensive: 0 for OOB/NULL
    return o->cells[idx];
}

int teko_object_nfields(const TekoObject* o) {
    return o ? o->nfields : 0;
}
