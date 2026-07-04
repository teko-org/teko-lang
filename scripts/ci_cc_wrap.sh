#!/usr/bin/env sh
# scripts/ci_cc_wrap.sh — install a `cc` wrapper on PATH that prepends the given flags to
# every host-cc invocation teko makes. Under seed-from-release CI the compiler is the
# downloaded binary, so the way to sanitize the compiler's C is to build gen1 with an
# instrumented cc: `teko . -o bin` then invokes this wrapper → an ASan/TSan-built gen1,
# whose execution (`bin/teko test .`) sanitizes the generated C + the runtime seam.
#
# Usage:   sh scripts/ci_cc_wrap.sh "-fsanitize=address,undefined -fno-omit-frame-pointer -g"
# Env:     CC_UNDERLYING — the real compiler the wrapper calls (default clang).
set -eu

FLAGS="${1:?usage: ci_cc_wrap.sh '<cc flags>'}"
BASE="${CC_UNDERLYING:-clang}"

mkdir -p .ccwrap
cat > .ccwrap/cc <<EOF
#!/bin/sh
exec ${BASE} ${FLAGS} "\$@"
EOF
chmod +x .ccwrap/cc

if [ -n "${GITHUB_PATH:-}" ]; then
  printf '%s\n' "$(pwd)/.ccwrap" >> "$GITHUB_PATH"
fi
echo "ci_cc_wrap: cc -> ${BASE} ${FLAGS}"
