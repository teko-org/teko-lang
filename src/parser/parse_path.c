// src/parser/parse_path.c   (namespace 'teko::parser')
//
// Path parsing, the C23 mirror of parser/parse_path.tks.
#include "parse_path.h"
#include "cursor.h"   // tk_is_kind_at
#include "ast.h"      // tk_segs_push

tk_parsed_path_result parse_path(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_name_at(t, n, pos)) {   // a path's first segment may be a contextual keyword (`type`/`to`) used as a name
        return (tk_parsed_path_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected a name") };
    }
    tk_segment *segs = NULL; size_t ns = 0;
    tk_segs_push(&segs, &ns, (tk_segment){ .name = t[pos].text });
    size_t p = pos + 1;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_COLONCOLON)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT)) {
            return (tk_parsed_path_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected a name after '::'") };
        }
        tk_segs_push(&segs, &ns, (tk_segment){ .name = t[p + 1].text });
        p += 2;
    }
    return (tk_parsed_path_result){ .ok = true,
        .as.value = { .node = { .segments = segs, .len = ns }, .next = p } };
}
