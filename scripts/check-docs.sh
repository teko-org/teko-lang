#!/bin/sh
# check-docs.sh [MC] -- the docs gate (docs/history/design/plano-docs-site.md §3, D1).
# Four checks, in this order, run from the repository root as the `docs` job in
# .github/workflows/ngen.yml does:
#
#   1. links        every relative markdown link under docs/, plus the two root pages
#                    that link into it (README.md, CONTRIBUTING.md), resolves to a file
#                    that exists.
#   2. legacy       no page under docs/, outside docs/history/, names a path of the
#                    retired standalone compiler: `ngen/`, `.tks`, `teko.tkp`,
#                    `fetch_teko.sh`, `bootstrap/teko.c`, or a bare `src/` that is not
#                    `mc`'s own (a line naming `mc` is read as `mc`'s still-alive src/,
#                    everything else as the retired tree).
#   3. diagnostics  every `"teko: ..."` literal string in teko*.tk (the taught compiler's
#                    own sources) appears in docs/reference/diagnostics.md. The list is
#                    extracted from source, never written down here, so a new diagnostic
#                    fails this check until it is documented.
#   4. samples      every fenced ```teko block under docs/ carries `// expect-exit: N`
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

find docs -name '*.md' | sort > "$tmp/mdfiles"
printf '%s\n%s\n' README.md CONTRIBUTING.md >> "$tmp/mdfiles"

# docs/history/ is a FROZEN, verbatim copy of retired documents (README.md, this file's
# own header): it is never rewritten to satisfy a gate that postdates it, so it is exempt
# from both checks below -- a page that also has to stay a faithful, unedited history
# could not be checked the same way a live page is.
grep -v '^docs/history/' "$tmp/mdfiles" > "$tmp/live_mdfiles"

# ------------------------------------------------------------------- 1. links
: > "$tmp/badlinks"
while read -r md; do
    [ -f "$md" ] || continue
    d=$(dirname "$md")
    grep -oE '\]\([^)]+\)' "$md" | sed -E 's/^\]\(//; s/\)$//' > "$tmp/targets"
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
    done < "$tmp/targets"
done < "$tmp/live_mdfiles"
nlinks=$(grep -rhoE '\]\([^)]+\)' $(cat "$tmp/live_mdfiles") 2>/dev/null | grep -Evc 'http|mailto:')
if [ -s "$tmp/badlinks" ]; then
    fail "unresolved links" "$(cat "$tmp/badlinks")"
else
    echo "ok links: $nlinks relative links resolve"
fi

# -------------------------------------------------------------- 2. legacy paths
# docs/brand/ documents file-type ICONS (`.tks`/`.tkt`/`.tkp`/`.tkl`) by what they used to
# mean; it is brand asset metadata the site consumes as-is (docs/history/design/
# plano-docs-site.md §1), not a claim about what exists today, so it is exempt too.
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
    fail "banned legacy references outside docs/history/" "$(cat "$tmp/legacy")"
else
    echo "ok legacy: no ngen/, .tks, teko.tkp, fetch_teko.sh, bootstrap/teko.c or bare src/ outside docs/history/"
fi

# ------------------------------------------------------------- 3. diagnostics
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

# ------------------------------------------------------------------ 4. samples
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
    echo "docs ok: $nlinks links, $ndiag diagnostics, $nblocks samples"
    exit 0
fi
echo "$fails documentation check(s) failed"
exit 1
