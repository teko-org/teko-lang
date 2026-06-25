// src/checker/check.h — the shared checker result type (conceptually `error?`).
// (The legacy tk_check_function/item/program driver was retired; the typed
//  layer's tk_type_program is the entry point. revalidate.c still uses this type.)
#ifndef TK_CHECK_H
#define TK_CHECK_H

#include "collect.h"   // pulls resolve.h → tk_error, bool

// `error?` — `ok` ⇒ null (no error); `!ok` ⇒ the `error`. (A fallible check that
// yields no value is `error?`; this struct already IS exactly that.)
typedef struct { bool ok; tk_error error; } tk_check_result;

#endif // TK_CHECK_H
