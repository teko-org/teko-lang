#!/bin/sh
# check-docs.sh [MC] -- the docs gate. Five checks, in this order, run from the
# repository root as the `docs` job in .github/workflows/ngen.yml does:
#
#   1. links        every relative markdown link under docs/ and site/, plus the two root
#                    pages that link into them (README.md, CONTRIBUTING.md), resolves to a
#                    file that exists. Fenced code is skipped: `ops[0](3, 4)` inside a
#                    ```teko block is a call, not a link.
#   1b. fragments   every `#slug` on a link names a heading of the page it lands on, by the
#                    id mcsite gives it (u_slug + md_unique_id) -- the site's own --check rule.
#   2. legacy       no page under docs/ names a path of the retired standalone compiler:
#                    `ngen/`, `.tks`, `teko.tkp`, `fetch_teko.sh`, `bootstrap/teko.c`, or
#                    a bare `src/` that is not `mc`'s own (a line naming `mc` is read as
#                    `mc`'s still-alive src/, everything else as the retired tree).
#   3. language     no tracked source carries Portuguese. This repository is English-only
#                    (DECISION_LOG.md D18): Portuguese belongs in chat with the owner or
#                    in the private history repository. Any Portuguese diacritic, or any
#                    word of the ASCII list below, fails with `file:line`.
#   4. diagnostics  every `"teko: ..."` literal string in teko*.tk (the taught compiler's
#                    own sources) appears in docs/reference/diagnostics.md. The list is
#                    extracted from source, never written down here, so a new diagnostic
#                    fails this check until it is documented.
#   4b. refusals     every `// expect-refuse:` message under tests/refuse/ contains a
#                    documented compiler literal (10+ characters) -- the other direction.
#   5. samples      every fenced ```teko block under docs/ carries `// expect-exit: N`
#                    (built with the taught compiler and RUN, exit code compared) or
#                    `// no-run` (an illustrative fragment, left uncompiled); anything
#                    else fails the check.
mc="${1:-mc}"

if command -v "$mc" >/dev/null 2>&1; then
    mc=$(command -v "$mc")
elif [ ! -x "$mc" ]; then
    echo "FAIL: compiler '$mc' not found or not executable"
    exit 1
fi

if [ ! -f teko.toml ]; then
    echo "FAIL: run from the repository root (teko.toml not found)" >&2
    exit 1
fi

fails=0
tmp="${TMPDIR:-/tmp}/check-docs.$$"
mkdir -p "$tmp" build/docs

cleanup() { rm -rf "$tmp" mc.checkdocs.*.toml; return 0; }
trap cleanup EXIT INT TERM

fail() {
    printf '%s\n' "FAIL $1"
    shift
    for line in "$@"; do printf '%s\n' "     $line"; done
    fails=$((fails + 1))
}

# site/ is documentation too -- site/README.md describes how docs/ becomes the website,
# and site/public/ is generated (and gitignored), so it never carries a page of its own.
find docs site -name '*.md' | sort > "$tmp/live_mdfiles"
printf '%s\n%s\n' README.md CONTRIBUTING.md >> "$tmp/live_mdfiles"

# A fenced block is code, not prose: `ops[0](3, 4)` in a ```teko sample reads exactly like
# a markdown link and is not one, and a sample is checked by compiling it (check 5), not by
# resolving its punctuation.
# A fence is what mcsite's md_fence_len says it is: a line whose first non-blank
# run is three or more backticks or tildes (an info string may follow); it closes
# on a line whose run is the same character, at least as long, and alone on the
# line. `~~~` and ```` are fences too, and a ``` line does not close a ~~~ block.
strip_fences() {
    awk '
        { line = $0; sub(/^[ \t]+/, "", line) }
        fenced {
            if (match(line, /^`+|^~+/) && substr(line, 1, 1) == fch && RLENGTH >= flen \
                && substr(line, RLENGTH + 1) ~ /^[ \t]*$/) fenced = 0
            next
        }
        match(line, /^`+|^~+/) && RLENGTH >= 3 { fch = substr(line, 1, 1); flen = RLENGTH; fenced = 1; next }
        { print }'
}

# ------------------------------------------------------------------- 1. links
: > "$tmp/badlinks"
nlinks=0
while read -r md; do
    [ -f "$md" ] || continue
    d=$(dirname "$md")
    strip_fences < "$md" | grep -oE '\]\([^)]+\)' | sed -E 's/^\]\(//; s/\)$//' > "$tmp/targets"
    while read -r target; do
        case "$target" in
            http://*|https://*|mailto:*|"#"*) continue ;;
        esac
        path="${target%%#*}"
        [ -n "$path" ] || continue
        case "$path" in
            /*) p=".$path" ;;
            *)  p="$d/$path" ;;
        esac
        if [ ! -e "$p" ]; then echo "$md -> $target" >> "$tmp/badlinks"; fi
        nlinks=$((nlinks + 1))
    done < "$tmp/targets"
done < "$tmp/live_mdfiles"
if [ -s "$tmp/badlinks" ]; then
    fail "unresolved links" "$(cat "$tmp/badlinks")"
else
    echo "ok links: $nlinks relative links resolve"
fi

# --------------------------------------------------------------- 1b. fragments
# A `#slug` on a link -- in-page or on another page -- has to name a heading of
# the page it lands on, by the id the site generator gives that heading: the
# text lowercased, every run of characters outside [a-z0-9_] one dash, no dash
# at either end, and a repeated id numbered -2, -3, ... in page order (mcsite's
# u_slug and md_unique_id). The site's own `--check` fails on a fragment with no
# target; this is the same rule, run before the site is rendered.
heading_ids() {
    strip_fences < "$1" | awk '
        /^#+[ \t]/ {
            t = $0; sub(/^#+[ \t]+/, "", t); sub(/[ \t]+#*[ \t]*$/, "", t)
            id = ""; dash = 0
            for (i = 1; i <= length(t); i++) {
                c = substr(t, i, 1)
                if (c ~ /[a-z0-9_]/)      { id = id c; dash = 0 }
                else if (c ~ /[A-Z]/)     { id = id tolower(c); dash = 0 }
                else if (id != "" && !dash) { id = id "-"; dash = 1 }
            }
            sub(/-+$/, "", id); if (id == "") id = "section"
            if (id in seen) { k = 2; while ((id "-" k) in seen) k++; id = id "-" k }
            seen[id] = 1; print id
        }'
}
: > "$tmp/badfrags"
nfrags=0
while read -r md; do
    [ -f "$md" ] || continue
    d=$(dirname "$md")
    strip_fences < "$md" | grep -oE '\]\([^)]*#[^)]+\)' | sed -E 's/^\]\(//; s/\)$//' > "$tmp/ftargets"
    while read -r target; do
        case "$target" in http://*|https://*|mailto:*) continue ;; esac
        path="${target%%#*}"; frag="${target#*#}"
        if [ -z "$path" ]; then p="$md"; else
            case "$path" in /*) p=".$path" ;; *) p="$d/$path" ;; esac
        fi
        case "$p" in *.md) ;; *) continue ;; esac
        [ -f "$p" ] || continue
        nfrags=$((nfrags + 1))
        heading_ids "$p" | grep -qx -- "$frag" || echo "$md -> $target" >> "$tmp/badfrags"
    done < "$tmp/ftargets"
done < "$tmp/live_mdfiles"
if [ -s "$tmp/badfrags" ]; then
    fail "fragments with no target heading" "$(cat "$tmp/badfrags")"
else
    echo "ok fragments: $nfrags #slug links land on a heading"
fi

# -------------------------------------------------------------- 2. legacy paths
# docs/brand/ documents file-type ICONS (`.tks`/`.tkt`/`.tkp`/`.tkl`) by what they used to
# mean; it is brand asset metadata the site consumes as-is, not a claim about what exists
# today, so it is exempt from this check.
banned="ngen/ .tks teko.tkp fetch_teko.sh bootstrap/teko.c"
: > "$tmp/legacy"
while read -r md; do
    case "$md" in docs/brand/*) continue ;; esac
    for tok in $banned; do
        grep -n -F -- "$tok" "$md" 2>/dev/null | sed "s#^#$md:#" >> "$tmp/legacy"
    done
    # a bare `src/` is the retired compiler; the mc's own `src/` is fine when the line names
    # it as such (`minicompiler/mc`, `mc/src/`, `` `mc` ``) -- token match, not substring
    grep -n -F -- 'src/' "$md" 2>/dev/null | grep -v -E 'minicompiler/mc|mc/src/|`mc`|<mc/' | sed "s#^#$md:#" >> "$tmp/legacy"
done < "$tmp/live_mdfiles"
if [ -s "$tmp/legacy" ]; then
    fail "banned legacy references" "$(cat "$tmp/legacy")"
else
    echo "ok legacy: no ngen/, .tks, teko.tkp, fetch_teko.sh, bootstrap/teko.c or bare src/"
fi

# ----------------------------------------------------------------- 3. language
# The tracked sources, minus two exemptions: docs/brand/ is asset metadata written in
# whatever the asset says, and THIS file carries the word list itself. tests/ carries
# no exemption: a fixture that genuinely needs an accented character as test data (an
# encoding case) is exempted by name here, with its own justification, not by directory.
if git rev-parse --git-dir > /dev/null 2>&1; then
    git ls-files > "$tmp/all_tracked"
else
    find . -type f | sed 's#^\./##' | sort > "$tmp/all_tracked"
fi
grep -E '\.(md|tk|mc|sh|yml|yaml|toml|cff)$|^\.git(ignore|attributes)$' "$tmp/all_tracked" \
    | grep -v -E '^(docs/brand/|scripts/check-docs\.sh$)' > "$tmp/english_files"

# Two rules. A Portuguese diacritic is decisive on its own -- no English word in this tree
# carries one. The ASCII list catches Portuguese written without accents; every word on it
# is matched whole, and none of them is an English word (`state` is not `esta`).
#
# The accents are spelled as an ALTERNATION of whole characters, never as a bracket class:
# outside a UTF-8 locale a bracket class matches single BYTES, and the continuation bytes
# of `a` are also the continuation bytes of an em dash, a checkmark and a curly quote.
pt_accents='á|à|â|ã|é|ê|í|ó|ô|õ|ú|ü|ç|Á|À|Â|Ã|É|Ê|Í|Ó|Ô|Õ|Ú|Ü|Ç'
pt_words='nao|entao|mesmo|mesma|reabertura|sao|voce|esta|estao|tambem|atraves|divida|dono|dona|arquivo|arquivos|ficheiro|entrega|entregas|escada|primitivas|superficie|nivel|codigo|funcao|porque|quando|isso|dele|dela'
: > "$tmp/pt"
while read -r f; do
    [ -f "$f" ] || continue
    grep -n -E "($pt_accents)" "$f" 2>/dev/null | sed "s#^#$f:#" >> "$tmp/pt"
    grep -n -w -E "($pt_words)" "$f" 2>/dev/null | sed "s#^#$f:#" >> "$tmp/pt"
done < "$tmp/english_files"
nfiles=$(grep -c . "$tmp/english_files")
if [ -s "$tmp/pt" ]; then
    fail "Portuguese in an English-only repository (DECISION_LOG.md D18)" "$(cut -c1-140 "$tmp/pt")"
else
    echo "ok language: $nfiles tracked sources carry no Portuguese"
fi

# ------------------------------------------------------------- 4. diagnostics
grep -ohE '"teko: [^"]*"' teko*.tk | sort -u > "$tmp/diag"
: > "$tmp/diag_missing"
while IFS= read -r d; do
    grep -qF -- "$d" docs/reference/diagnostics.md || echo "$d" >> "$tmp/diag_missing"
done < "$tmp/diag"
ndiag=$(grep -c . "$tmp/diag")
if [ -s "$tmp/diag_missing" ]; then
    fail "undocumented diagnostics (missing from docs/reference/diagnostics.md)" "$(cat "$tmp/diag_missing")"
else
    echo "ok diagnostics: $ndiag teko: strings documented in docs/reference/diagnostics.md"
fi

# ---------------------------------------------------------- 4b. refusals
# The other direction: every `// expect-refuse:` message under tests/refuse/ is
# composed from the compiler's own string literals; the longest literal (10+
# characters) found inside the message is the diagnostic it exercises, and that
# literal has to be documented. A fixture whose message drifted from the sources,
# or a refusal the page never names, fails here.
grep -ohE '"([^"\\]|\\.)*"' teko*.tk | awk 'length >= 12 { print length, substr($0, 2, length - 2) }' | sort -rn | cut -d' ' -f2- > "$tmp/lits"
: > "$tmp/refuse_missing"
nrefuse=0
for f in tests/refuse/*.tk; do
    msg=$(grep -m1 '^// expect-refuse:' "$f" | sed 's#^// expect-refuse: *##')
    [ -n "$msg" ] || continue
    nrefuse=$((nrefuse + 1))
    hit=""
    while IFS= read -r lit; do
        case "$msg" in *"$lit"*) hit="$lit"; break ;; esac
    done < "$tmp/lits"
    if [ -z "$hit" ] || ! grep -qF -- "$hit" docs/reference/diagnostics.md; then
        echo "$f: $msg" >> "$tmp/refuse_missing"
    fi
done
if [ -s "$tmp/refuse_missing" ]; then
    fail "refusal fixtures whose message is not documented in docs/reference/diagnostics.md" "$(cat "$tmp/refuse_missing")"
else
    echo "ok refusals: $nrefuse tests/refuse/ messages documented in docs/reference/diagnostics.md"
fi

# ------------------------------------------------------------------ 5. samples
: > "$tmp/manifest"
fno=0
while read -r md; do
    fno=$((fno + 1))
    awk -v out="$tmp" -v mf="$tmp/manifest" -v src="$md" -v pfx="$fno" '
        /^```teko$/ || /^```teko / {
            if (!inb) {
                inb = 1
                n++
                f = sprintf("%s/b%s_%03d.tk", out, pfx, n)
                printf "" > f
                print src "\t" NR "\t" f >> mf
                next
            }
        }
        inb && /^```[ \t]*$/ { inb = 0; close(f); next }
        inb { print >> f }
    ' "$md"
done < "$tmp/live_mdfiles"

built=0
# Written at the REPOSITORY ROOT, next to teko.toml, and never under $tmp: every path a
# derived config names is resolved against the config's OWN directory (D224), so a config
# outside the root cannot find core_teko.mc or the teko_*.tk modules.
host_cfg="mc.checkdocs.host.toml"
build_taught_compiler() {
    [ "$built" = "1" ] && return 0
    host_os=$("$mc" --host | awk '$1 == "os"   { print $2 }')
    host_arch=$("$mc" --host | awk '$1 == "arch" { print $2 }')
    awk '/^\[/ { skip = ($0 == "[linker]") } !skip' teko.toml \
        | sed -e "s#^os   = .*#os   = \"$host_os\"#" -e "s#^arch = .*#arch = \"$host_arch\"#" \
        > "$host_cfg"
    # With no `[linker]`, a Linux build goes through mc's own ELF writer, whose
    # `PT_INTERP` defaults to MUSL's loader (mc docs/reference/toml.md
    # § [target].libc). On a glibc box -- every Linux runner this gate runs on --
    # such a binary is `not found` the moment a shell tries to run it, which is
    # exactly what a compiled sample below does. So a glibc host names its own
    # loader out loud, the way `ngen.yml`'s five legs already do; where that
    # loader is not at the standard path nothing is added and the default stands.
    loader=""
    if [ "$host_os" = "linux" ]; then
        case "$host_arch" in
            x86_64)  loader=/lib64/ld-linux-x86-64.so.2 ;;
            aarch64) loader=/lib/ld-linux-aarch64.so.1 ;;
        esac
        [ -n "$loader" ] && [ -e "$loader" ] || loader=""
    fi
    if [ -n "$loader" ]; then
        printf 'interp = "%s"\nlibc   = "gnu"\n' "$loader" > "$tmp/target_tail"
        sed -e "/^arch = /r $tmp/target_tail" "$host_cfg" > "$tmp/host_cfg"
        cp "$tmp/host_cfg" "$host_cfg"
    fi
    if ! "$mc" build . --config "$host_cfg" --compiler-only > "$tmp/tout" 2> "$tmp/terr"; then
        cat "$tmp/tout" "$tmp/terr" >&2
        return 1
    fi
    built=1
}

pass=0
noruns=0
while IFS="	" read -r md line blk; do
    name="$md:$line"
    want_exit=$(grep -m1 '// expect-exit:' "$blk" | sed 's/.*expect-exit: *//')
    no_run=$(grep -c '// no-run' "$blk")
    if [ -z "$want_exit" ] && [ "$no_run" = "0" ]; then
        fail "$name" "fenced teko block needs // expect-exit: N or // no-run"
        continue
    fi
    if [ -n "$want_exit" ] && [ "$no_run" != "0" ]; then
        fail "$name" "fenced teko block cannot carry both // expect-exit: N and // no-run"
        continue
    fi
    if [ "$no_run" != "0" ]; then
        noruns=$((noruns + 1))
        echo "ok $name (no-run)"
        continue
    fi
    if ! build_taught_compiler; then
        fail "$name" "cannot build the taught compiler"
        continue
    fi
    base=$(basename "$blk" .tk)
    sample="build/docs/$base.tk"
    cp "$blk" "$sample"
    exe="build/docs/$base"
    cfg="mc.checkdocs.$base.toml"
    sed -e "s#^entry = .*#entry = \"$sample\"#" -e "s#^out   = .*#out   = \"$exe\"#" "$host_cfg" > "$cfg"
    if ! build/teko build . --config "$cfg" --entry-only > "$tmp/tout" 2> "$tmp/terr"; then
        fail "$name" "compilation: $(cat "$tmp/tout" "$tmp/terr")"
        continue
    fi
    "$exe" > /dev/null 2>&1
    got=$?
    if [ "$got" != "$want_exit" ]; then
        fail "$name" "exit $got, expected $want_exit"
        continue
    fi
    pass=$((pass + 1))
    echo "ok $name (runs, exit $got)"
done < "$tmp/manifest"
nblocks=$(grep -c . "$tmp/manifest")
echo "ok samples: $nblocks fenced teko blocks ($pass run, $noruns no-run)"

# ------------------------------------------------------------------- verdict
if [ "$fails" -eq 0 ]; then
    echo "docs ok: $nlinks links, $nfrags fragments, $ndiag diagnostics, $nrefuse refusals, $nblocks samples"
    exit 0
fi
echo "$fails documentation check(s) failed"
exit 1
