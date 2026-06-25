// src/checker/revalidate.h — the counter-validation of the typed tree.
#ifndef TK_CHECK_REVALIDATE_H
#define TK_CHECK_REVALIDATE_H

#include "tast.h"
#include "scope.h"   // tk_type_result (we reuse the error machinery)
#include "check.h"   // tk_check_result (conceptually `error?`)

// `error?`: null (ok) if the subtree's stored types match their derivations; else the `error`.
tk_check_result tk_validate_texpr(const tk_texpr *te);

#endif // TK_CHECK_REVALIDATE_H
