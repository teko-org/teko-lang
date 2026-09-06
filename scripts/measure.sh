#!/bin/sh
# measure.sh BINARY CONFIG [DIR] -- the S1m register (docs/design/
# plano-ngen-entrega4.md §64(f)): given the taught compiler's own binary,
# print the size it landed at, its sections, and `mc limits` for the same
# project/config, so a crumb that changes the parts a compiler is built
# from (D64.1) can quote a real number instead of a guess.
#
# BINARY is the compiler `mc build` produced (`[compiler].out`, e.g.
# `build/teko`); CONFIG is the build config `mc build`/`mc limits` read
# (host-derived from `teko.toml`, per HANDOFF.md §4 -- never `teko.toml`
# itself, which targets the CI's linux/x86_64 leg, and never `mc.toml`, which
# is the package manifest and has no `[project]`); DIR is the project
# directory `mc limits` reads, default `.` (the repository root).
#
# Sections come from `mc --dump-syms` on the GENERATED glue source
# (`BINARY.mc`, `#include <mc/host>` + the parts + the modules) -- the same
# technique `scripts/check-parts.sh` in the `mc` repository uses, and the
# only one that is host-neutral: Mach-O names its sections `__TEXT,__text`,
# ELF names them `.text`, COFF names them `.text` too, so this script prints
# every `section ...` line `--dump-syms` reports rather than assume a name.
#
# No network, no `gh` -- it runs on any of the ngen CI legs' own runner, and
# locally once the compiler and its glue source already exist (§4).

binary="$1"
config="$2"
dir="${3:-.}"

if [ -z "$binary" ] || [ -z "$config" ]; then
    echo "usage: measure.sh BINARY CONFIG [DIR]" >&2
    exit 1
fi
if [ ! -x "$binary" ]; then
    echo "measure.sh: not an executable: $binary" >&2
    exit 1
fi
if [ ! -f "$config" ]; then
    echo "measure.sh: no such config: $config" >&2
    exit 1
fi

bytes=$(wc -c < "$binary" | tr -d ' ')
echo "bytes $bytes"
echo ""

glue="$binary.mc"
echo "sections ($glue)"
if [ -f "$glue" ]; then
    mc --dump-syms "$glue" 2>&1 | awk '$1 == "section" { $1 = ""; print }'
else
    echo "measure.sh: no generated source at $glue, skipping" >&2
fi
echo ""

echo "limits ($dir --config $config)"
mc limits "$dir" --config "$config"
