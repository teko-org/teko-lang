#!/bin/sh
# bootstrap.sh -- the fixpoint rite: the fixed point of the SELF-HOSTED teko,
# in the protocol of the `mc` repository's own `scripts/bootstrap.sh`.
#
#   teko0 = mc build . --config <cfg> --compiler-only     (the stock mc)
#   teko1 = teko0 build . --config <cfg1> --entry-only     over mc_teko.tk
#   teko2 = teko1 ...                                         over mc_teko.tk
#   teko3 = teko2 ...                                         over mc_teko.tk
#   cmp build/teko2.o build/teko3.o          <- the criterion, on the OBJECTS
#   diff of `--dump-asm` between teko2 and teko3   <- the same, in readable form
#   the 45 fixtures compiled by teko1               <- and it is a compiler
#
# `teko1.o` vs `teko2.o` is NOT the criterion (they come from two different
# compilers -- the stock mc's codegen and teko1's own), exactly as `mc1.o` vs
# `mc2.o` is not the criterion there.
#
# No `set -e`: every step checks its own exit code and says which one failed,
# so a failure in the middle never passes silently. Times are printed per
# stage, sizes for every object and binary.
#
# Usage, from the REPOSITORY ROOT (the config path has to stay relative --
# with an absolute one the module treats every file as "outside the project"
# and the `internal` check goes blind, D224):
#
#   sh scripts/bootstrap.sh                 # host from `mc --host`
#   sh scripts/bootstrap.sh --os linux --arch x86_64
#   sh scripts/bootstrap.sh --os windows --arch x86_64 \
#       --linker-toml mc.linker.toml
#
# `--os`/`--arch` name the pair OUT LOUD and are checked against `mc --host`:
# the ladder RUNS every stage it builds, so a target that is not this machine is
# refused rather than cross-built into something nothing can execute.
#
# `--linker-toml FILE` REPLACES `teko.toml`'s own `[linker]` with the blocks
# in FILE -- `[sysroot]` and `[linker]` as the CI's Windows leg writes them.
# Windows has no direct-executable backend and no C runtime, so `cc` is not a
# linker there: the link is `lld-link` against a sysroot of three files
# (`.github/actions/windows-sysroot`). Everywhere else the option is not passed
# and `teko.toml`'s `[linker] cc` stands.
#
# `mc` has to already be on PATH -- this script never downloads one. The `mc`
# the CI puts on PATH before this script runs is the version PINNED by
# `MC_VERSION` (`cat MC_VERSION`, read by `.github/actions/setup-mc`), so a
# local run against a different `mc` is
# comparing against a different fixed point than CI's.
#
# Needs mc >= 0.15.10 (`MC_VERSION`): before it, `source_claim` hid the
# prelude's `while`/`for` from the core sources once the dialect taught the same
# lexeme (plano §70(f)); 0.15.10's TE_RULE fixed it and the ladder closes.
#
# The build config is `teko.toml`; the root's `mc.toml` carries only `[package]`
# and no build reads it. The derived configs are written next to `teko.toml` (an
# entry path is resolved against the CONFIG's own directory, so a config in a
# scratch directory cannot find `mc_teko.tk`) and removed on exit; `teko.toml`
# itself is never touched. They always carry a `[linker]` block on purpose --
# `teko.toml`'s own, or the one `--linker-toml` names: with a linker `mc` writes
# `<out>.o` and hands it over, which is what leaves the object on disk for the
# `cmp`, and without one the built-in executable backend writes the binary and
# no object at all.
#
# On Windows every stage is named `<name>.exe`, because that is what the loader
# there requires and what `[compiler].out` already gets from `mc` itself; `mc`
# derives the object from `[project].out` by appending `.o`, so the objects the
# criterion compares are `teko2.exe.o` and `teko3.exe.o` -- COFF, but the same
# `cmp`.

os=""
arch=""
linker=""
usage="usage: bootstrap.sh [--os OS] [--arch ARCH] [--linker-toml FILE]"
while [ $# -gt 0 ]; do
    case "$1" in
        --os)          os="$2";     shift 2 ;;
        --arch)        arch="$2";   shift 2 ;;
        --linker-toml) linker="$2"; shift 2 ;;
        *) echo "$usage" >&2; exit 1 ;;
    esac
done

if [ -n "$linker" ] && [ ! -f "$linker" ]; then
    echo "FAIL: --linker-toml $linker does not exist" >&2
    exit 1
fi

if [ ! -f teko.toml ]; then
    echo "FAIL: run from the repository root (teko.toml not found)" >&2
    exit 1
fi
if [ ! -f mc_teko.tk ]; then
    echo "FAIL: mc_teko.tk not found" >&2
    exit 1
fi
if ! command -v mc >/dev/null 2>&1; then
    echo "FAIL: no 'mc' on PATH (CONTRIBUTING.md installs it from the release)" >&2
    exit 1
fi

host_os=$(mc --host   | awk '$1 == "os"   { print $2 }')
host_arch=$(mc --host | awk '$1 == "arch" { print $2 }')
if [ -z "$os" ];   then os="$host_os";     fi
if [ -z "$arch" ]; then arch="$host_arch"; fi
if [ -z "$os" ] || [ -z "$arch" ]; then
    echo "FAIL: could not resolve the host pair (mc --host)" >&2
    exit 1
fi

# The ladder RUNS every stage it builds, and the taught compiler of stage 0 is
# written by the HOST's executable backend either way (mc docs/build.md
# § [compiler]) -- so a target that is not this machine cannot be a fixed point,
# it is a cross build with nothing to execute. `--os`/`--arch` are there to say
# the pair OUT LOUD in a CI log, not to cross-compile.
if [ "$os" != "$host_os" ] || [ "$arch" != "$host_arch" ]; then
    echo "FAIL: $os/$arch is not this machine ($host_os/$host_arch); the ladder runs what it builds" >&2
    exit 1
fi

# What an executable is called here. `mc` appends it to `[compiler].out` by
# itself (src/driver.mc, host_exe_suffix); `[project].out` is this script's to
# name, and the two agree because target and host are the same pair.
exe=""
if [ "$os" = "windows" ]; then exe=".exe"; fi

teko0="build/teko$exe"
teko1="build/teko1$exe"
teko2="build/teko2$exe"
teko3="build/teko3$exe"

cfg0="mc.boot0.toml"
cfg1="mc.boot1.toml"
cfg2="mc.boot2.toml"
cfg3="mc.boot3.toml"
cfgf="mc.bootfix.toml"
asm2="${TMPDIR:-/tmp}/teko2.$$.asm"
asm3="${TMPDIR:-/tmp}/teko3.$$.asm"
out="${TMPDIR:-/tmp}/bootstrap.$$.out"
err="${TMPDIR:-/tmp}/bootstrap.$$.err"
tail_="${TMPDIR:-/tmp}/bootstrap.$$.target-tail"
trap 'rm -f "$cfg0" "$cfg1" "$cfg2" "$cfg3" "$cfgf" "$asm2" "$asm3" "$out" "$err" "$tail_"' EXIT

# The extra `[target]` lines every derived config carries, written once here and
# read back by `derive`. Empty everywhere except linux/glibc, and that case is
# the ladder's own: the taught compiler is written by the HOST's executable
# backend (mc docs/build.md § [compiler]), whose ELF writer defaults the program
# interpreter and the libc soname to musl -- so on a glibc machine stage 1 gets
# `build/teko: not found` at exit 127, the loader's way of saying the
# interpreter named in the binary does not exist. The ladder RUNS every stage it
# builds, so the loader THIS machine actually has is the oracle: named when it is
# there, and on a musl machine nothing is appended and the musl defaults stand.
# (`libc` is a family since mc 0.12.1; the ladder needs 0.15.10 anyway.)
glibc_loader_of() {
    case "$1" in
        x86_64)  echo "/lib64/ld-linux-x86-64.so.2" ;;
        aarch64) echo "/lib/ld-linux-aarch64.so.1" ;;
    esac
}

write_target_tail() {
    : >"$tail_"
    [ "$os" = "linux" ] || return 0
    loader=$(glibc_loader_of "$arch")
    [ -n "$loader" ] && [ -e "$loader" ] || return 0
    printf 'interp = "%s"\nlibc   = "gnu"\n' "$loader" >"$tail_"
    echo "-- glibc machine: [target] interp $loader, libc gnu --"
}

# `teko.toml` minus the `[linker]` block when one is being substituted; the
# whole file otherwise. Same `awk` the CI's leg config uses.
base_config() {
    if [ -z "$linker" ]; then
        cat teko.toml
        return 0
    fi
    awk '/^\[/ { skip = ($0 == "[linker]") } !skip' teko.toml
}

# derive CONFIG ENTRY OUT -- teko.toml with the host's own target and one
# stage's entry/output, the same `sed` shape CONTRIBUTING.md uses for a fixture
derive() {
    base_config \
        | sed -e "s#^os   = .*#os   = \"$os\"#" \
              -e "s#^arch = .*#arch = \"$arch\"#" \
              -e "s#^entry = .*#entry = \"$2\"#" \
              -e "s#^out   = .*#out   = \"$3\"#" \
        | sed -e "/^arch = /r $tail_" > "$1"
    if [ -n "$linker" ]; then
        printf '\n' >> "$1"
        cat "$linker" >> "$1"
    fi
}

# Millisecond stage times where there is a `perl`, whole seconds where there is
# not -- the ladder must not fail over a stopwatch.
if command -v perl > /dev/null 2>&1; then
    now() {
        perl -MTime::HiRes=time -e 'printf "%.3f\n", time'
    }

    dt() {
        perl -e 'printf "%.3f", '"$2"' - '"$1"''
    }
else
    now() {
        date +%s
    }

    dt() {
        echo $(($2 - $1))
    }
fi

size_of() {
    wc -c < "$1" | tr -d ' '
}

# `sha256sum` on Linux, `shasum -a 256` on macOS -- the same pair the CI's own
# summary step keeps, so a hash printed here is comparable with the one there
sha256_of() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# step DESCRIPTION CMD... -- runs CMD, times it, and stops the whole script
# with the command's own output when it fails
step() {
    desc="$1"; shift
    t0=$(now)
    "$@" >"$out" 2>"$err"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL: $desc (exit $rc)" >&2
        echo "--- command: $* ---" >&2
        cat "$out" "$err" >&2
        exit 1
    fi
    t1=$(now)
    echo "  $desc: $(dt "$t0" "$t1")s"
    cat "$out"
}

echo "=== S4.2 -- fixed point of the self-hosted teko: teko0 -> teko1 -> teko2 -> teko3 ==="
echo "-- target $os/$arch, entry mc_teko.tk --"
write_target_tail

t_total0=$(now)

echo "-- stage 0: mc build . --compiler-only -> $teko0 --"
derive "$cfg0" "tests/hello.tk" "build/teko-hello$exe"
step "mc builds teko0" mc build . --config "$cfg0" --compiler-only
echo "  size $teko0: $(size_of "$teko0") bytes"

echo "-- stage 1: teko0 mc_teko.tk -> $teko1 --"
derive "$cfg1" "mc_teko.tk" "build/teko1$exe"
step "teko0 compiles mc_teko.tk" "$teko0" build . --config "$cfg1" --entry-only
echo "  size $teko1.o: $(size_of "$teko1.o") bytes"
echo "  size $teko1:   $(size_of "$teko1") bytes"

echo "-- stage 2: teko1 mc_teko.tk -> $teko2 --"
derive "$cfg2" "mc_teko.tk" "build/teko2$exe"
step "teko1 compiles mc_teko.tk" "$teko1" build . --config "$cfg2" --entry-only
echo "  size $teko2.o: $(size_of "$teko2.o") bytes"

echo "-- stage 3: teko2 mc_teko.tk -> $teko3 --"
derive "$cfg3" "mc_teko.tk" "build/teko3$exe"
step "teko2 compiles mc_teko.tk" "$teko2" build . --config "$cfg3" --entry-only
echo "  size $teko3.o: $(size_of "$teko3.o") bytes"

echo "-- criterion 1: cmp $teko2.o $teko3.o --"
if ! cmp "$teko2.o" "$teko3.o"; then
    echo "FAIL: teko2.o != teko3.o -- no fixed point" >&2
    echo "diagnosis: diff <($teko2 --dump-asm mc_teko.tk) <($teko3 --dump-asm mc_teko.tk)" >&2
    exit 1
fi
echo "  ok: teko2.o == teko3.o"

# Provenance of the run, printed and NOT gated (higiene 4 item B). A divergence
# of `teko1.o` between two runs of this script was reported once and has not
# reproduced since; the four hashes plus the compiler that seeded the ladder are
# what makes the NEXT one attributable without rerunning anything: teko0 is the
# only stage the stock `mc` writes, so an identical teko0 with a different
# teko1.o is a compiler nondeterminism, a different teko0 is a different input
# (mc version, tree, or a stale build), and teko1.o == teko2.o says the
# ladder was already at the fixed point on its first turn.
echo "-- provenance (reported, not gated) --"
echo "  mc:       $(mc --version 2>&1 | head -1)"
echo "  teko0:    $(sha256_of "$teko0")"
echo "  teko1.o:  $(sha256_of "$teko1.o")"
echo "  teko2.o:  $(sha256_of "$teko2.o")"
echo "  teko3.o:  $(sha256_of "$teko3.o")"
if cmp -s "$teko1.o" "$teko2.o"; then
    echo "  teko1.o == teko2.o: yes (fixed point on the first turn)"
else
    echo "  teko1.o == teko2.o: no (the stock mc's codegen differs from teko1's own)"
fi

echo "-- criterion 2: --dump-asm of teko2 vs teko3 --"
"$teko2" --dump-asm mc_teko.tk > "$asm2" 2>&1
"$teko3" --dump-asm mc_teko.tk > "$asm3" 2>&1
if ! diff "$asm2" "$asm3" > "$out"; then
    echo "FAIL: the two dumps differ" >&2
    head -40 "$out" >&2
    exit 1
fi
echo "  ok: $(wc -l < "$asm2" | tr -d ' ') lines, diff empty"

echo "-- criterion 3: teko1 compiles the fixtures --"
pass=0
fail=0
for src in tests/*.tk; do
    n=$(basename "$src" .tk)
    want=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
    derive "$cfgf" "tests/$n.tk" "build/$n$exe"
    if "$teko1" build . --config "$cfgf" --entry-only >"$out" 2>"$err"; then
        "build/$n$exe"
        got=$?
    else
        got="build-fail"
    fi
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "  FAIL $n: exit $got, want $want" >&2
        head -3 "$out" "$err" >&2
    fi
done
echo "  fixtures: $pass passed, $fail failed"

t_total1=$(now)
echo "=== total: $(dt "$t_total0" "$t_total1")s ==="

if [ "$fail" -ne 0 ]; then
    echo "FAIL: teko1 does not compile every fixture" >&2
    exit 1
fi
echo "FIXPOINT OK"
