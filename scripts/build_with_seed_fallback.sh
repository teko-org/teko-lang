#!/usr/bin/env sh
# scripts/build_with_seed_fallback.sh — build gen1 (the tip's compiler) from the released
# seed, with a STAGED BOOTSTRAP fallback for when the seed cannot compile the tip directly.
#
# ── THE CHAIN THIS SCRIPT WALKS (owner ruling 2026-07-28) ─────────────────────────────────────
#
#   release 0.3.0.31 --TEKO_BACKEND=c--> gen0 --TEKO_BACKEND=c--> gen1 (emits teko.c)
#                    --native--> gen2 --native--> gen3            ASSERT gen2 == gen3
#
# THIS SCRIPT OWNS THE FIRST TWO LINKS and hands `$OUT_DIR/teko` — gen1 — to
# `scripts/fixpoint_gate.sh`, which owns the last two and the verdict.
#
# gen0 IS NEW, AND IT IS NOT A SECOND LADDER. Every route below (the declared degrau, the released
# seed, the committed host seed, the pinned SHA rungs) ends at ONE compiler built from the tip's
# source; that compiler is gen0, and `gen0_to_gen1` then has it rebuild the very same source. The
# doubling is what makes gen1's emitted `teko.c` the output of a compiler whose own algorithm is
# THIS tree's — which is the only C worth versioning, since versioning it is the point (owner:
# the C *"deixa de ser ENTRADA e passa a ser SAÍDA"*).
#
# `bootstrap/teko.c` IS NO LONGER AN INPUT UNLESS A DEGRAU SAYS SO. Owner ruling 2026-07-28: *"só
# podemos usar teko.c se e somente se identificarmos degrau."* The old rung -1 keyed off the FILE's
# presence, which was sound while the file could only exist as a bootstrap emergency; now that the
# same file is this train's own harvested OUTPUT, presence means nothing and a versioned
# declaration (`bootstrap/DEGRAU`, see scripts/degrau.sh) means everything.
#
# WHEN A DEGRAU IS DECLARED IT IS THE SEED — FORCED — AND ITS FAILURE IS FATAL. Owner ruling
# 2026-08-18 (CLAUDE.md "PROVENANCE/reseed"): provenance is REVOKED. A declared `bootstrap/DEGRAU`
# short-circuits the ENTIRE chain below — the released seed, the committed host seed and the pinned
# SHA ladder are NEVER tried. `bootstrap/teko.c` is compiled straight into gen0 and `gen0_to_gen1`
# doubles it to gen1. If that gen0 cannot build the tip, the script EXITS NON-ZERO on the spot: no
# release probe, no ladder, no version-old seed. The release predates this tree's syntax (it dies on
# the retired `T?`/`i128`/`Ref<T>`), so falling back to it only buries the real failure under the
# wrong one and, worse, would publish gen0 from the release instead of from this tree's own compiler.
# The degrau ends by DELETING `bootstrap/DEGRAU` the day the released seed reaches the tip again —
# never by a silent fallback. Everything from the FAST PATH down is reached ONLY with no degrau.
#
# INVARIANT (owner ruling 2026-07-24, replacing the older "seed builds the tip" rule): the
# released seed only has to build the PR's BASE lineage. A compiler built from an ancestor is
# itself a valid seed for a newer commit, so when the RAW released seed cannot compile the tip
# (a genuine language/codegen capability jump landed since the seed was cut), this script walks
# a chain of intermediate generations until one of them reaches the tip. "We run the tests
# under gen1, not under the seed" (the .30 ruling) extends naturally: gen1 may come from a few
# extra hops when the seed alone cannot reach it.
#
# FAST PATH (unchanged, zero extra cost): the seed builds the tip directly. This is the
# common case and costs exactly what it always did — one build, no git history probing.
#
# FALLBACK (only entered when the fast path fails) — the LADDER, and it is PINNED, not probed
# (owner ruling 2026-07-25: "se sabe quais compilam, seja determinístico e os utilize"):
#
#   for each rung in $LADDER_RUNGS, in order:  build it with the compiler in hand
#   then:                                      build the tip with the last rung's compiler
#
# The pair in LADDER_RUNGS was DISCOVERED by the probing algorithm (still here, behind
# TEKO_LADDER_DISCOVER=1) and CONFIRMED by a green run (PR #92, run 30158725410): rung 1 is the
# last commit the released 0.3.0.30 seed can build, and rung 2 is the last commit that rung 1's
# compiler can build. Both are required — the log of that run shows the seed probing and REJECTING
# rung 2, so the pair is irreducible and nobody should "optimize" the first one away.
#
# WHY PINNING: the probe is a linear walk that builds one project per candidate commit and reads
# the failure. In that same run it cost ~75 failed builds and ~2.5 minutes per lane BEFORE any
# useful work — on every lane, every platform, every PR, multiplied again under qemu. The rungs are
# a property of the train's own history, not of the runner, so they belong in the file.
#
# WHY IT NEVER SILENTLY FALLS BACK TO PROBING: if a pinned rung fails to build, the pin is stale
# and this script FAILS LOUD, naming the commit and how to rediscover the pair. Quietly reverting
# to a probe is exactly how the determinism would be lost again (M.3 — a fallback that hides the
# obsolete pin is worse than no pin).
#
# Why the pair had to be discovered in the first place: a fixed rung (the merge-base with the base
# branch) is not necessarily buildable by the previous generation. Proven on the .31 train — the
# cast-width wagon ADDED the W-RULE to the checker and then DELETED the now-redundant manual casts
# from the corpus, so its own head requires a W-RULE-capable compiler while an older generation dies
# on it with B.22 ("operands must be the same type"). The buildable rung is the older wagon that has
# the new CAPABILITY but not yet the corpus that DEPENDS on it.
#
# A push directly to main never enters any of this: main's own merge-base with itself is itself, so
# the "no bootstrap gap" guard fails loud instead of pretending a fallback exists — a released seed
# that cannot build main is a real regression, not a capability gap.
#
# TRANSITIONAL: the FALLBACK ladder is a transitional measure. With 0.3.0.31 published, the normal
# chain starts at that release and every rung below the fast path exists only for the day it does
# not — which is precisely the day a degrau has to be DECLARED rather than inferred.
#
# The intermediate builds are DRY (`--no-verify`, no test gate) because CI already gated each of
# those commits on the PR that landed it; owner ruling 2026-07-24: the dry intermediate build is
# a TRANSITIONAL measure, to be undone with the ladder and not repeated.
#
# Usage:   sh scripts/build_with_seed_fallback.sh [OUT_DIR]
#          OUT_DIR defaults to "bin" and receives GEN1 — the tip's compiler, one generation past
#          whatever route reached the tip first (see `gen0_to_gen1`) — plus, while the C route is
#          alive, `OUT_DIR/teko.c`, which is GEN1's OWN emitted C and the harvest candidate.
#          Callers do not need to know which route was taken.
#
# Env:
#   TEKO_DEGRAU_FILE                the degrau declaration (default: bootstrap/DEGRAU). See
#                                   scripts/degrau.sh — it decides whether `bootstrap/teko.c` is a
#                                   rung or merely a payload this train produced.
#   TEKO_DEGRAU_LDFLAGS             extra link flags forwarded verbatim to the rung -1 degrau `cc`
#                                   invocation (default: empty). This rung is C-linked OUTSIDE teko's
#                                   own run_cc, so any linker adjustment that path normally applies
#                                   must be supplied here by the caller. CI's Windows leg sets it to
#                                   `-Wl,/STACK:67108864` (64 MiB PE stack reserve) because consteval
#                                   recursion overflows Windows' ~1 MiB default; other platforms leave
#                                   it empty and the link is unchanged.
#   TEKO_SEED_FALLBACK_SEED_BIN     the seed command to invoke (default: teko, resolved on PATH)
#   TEKO_SEED_FALLBACK_BASE_BRANCH  the branch the fallback bootstraps from (default: GITHUB_BASE_REF
#                                   when Actions sets it — a STACKED PR bootstraps from its BASE
#                                   branch, the predecessor wagon — else main)
#   TEKO_LADDER_DISCOVER            set to 1 to REDISCOVER the rungs by probing instead of using the
#                                   pins. A HUMAN tool, for refreshing LADDER_RUNGS when a pin goes
#                                   stale; never set in CI (that would restore the cost the pins
#                                   removed, and hide the staleness the pinned path reports).
set -eu

# shellcheck source=scripts/ci_phase_clock.sh
. "$(dirname "$0")/ci_phase_clock.sh"
phase_clock_init

# shellcheck source=scripts/degrau.sh
. "$(dirname "$0")/degrau.sh"

OUT_DIR="${1:-bin}"
SEED_BIN="${TEKO_SEED_FALLBACK_SEED_BIN:-teko}"
BASE_BRANCH="${TEKO_SEED_FALLBACK_BASE_BRANCH:-${GITHUB_BASE_REF:-main}}"

WORKTREE_DIR=""

log() { printf '%s\n' "teko-ci: $*" >&2; }

# cleanup — removes the scratch worktree this script creates while bootstrapping the fallback,
# on any exit path (success or failure). Every stage's output lives INSIDE that worktree, so
# removing it reclaims all of them.
cleanup() {
  [ -n "$WORKTREE_DIR" ] && [ -d "$WORKTREE_DIR" ] && git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1
  return 0
}
trap cleanup EXIT

# ── THE CHAIN'S FIRST QUESTION, ASKED ONCE ────────────────────────────────────────────────────
# Whether `bootstrap/teko.c` is a rung or a payload is decided HERE, from a versioned human claim,
# and every route below reads the answer instead of re-deriving it. A broken declaration is fatal
# on the spot: it means someone tried to bridge a gap and the bridge is not where they said, and
# quietly walking the chain they declared broken would report the wrong failure.
DEGRAU_RC=0
degrau_scan "$PWD" || DEGRAU_RC=$?
if [ "$DEGRAU_RC" = "2" ]; then
  log "FATAL: the degrau declaration is broken (see the lines above) — refusing to pick a chain."
  exit 1
fi
degrau_report
[ "$DEGRAU_RC" = "0" ] || degrau_note_undeclared_c "$PWD"

# build_project BIN DIR OUT LOGFILE [RT_DIR] — runs "BIN . -o OUT --no-verify --release"
# with cwd DIR, tees combined output to LOGFILE, and returns the build's own exit status.
# RT_DIR, when non-empty, pins TK_RT_DIR for the build: the compiler otherwise locates
# teko_rt.{h,c} relative to ITS OWN binary (argv[0]), and every fallback stage breaks under
# that rule — CI provisions the seed into <tip>/.seed, so an ancestor probe would compile
# ancestor-generated C against the TIP's runtime header (proven: probes died on the exact
# tk_rt_datetime_* symbols the time-redesign wagon removed), and an intermediate compiler
# lives inside the probe worktree, so the tip build would symmetrically pick the ANCESTOR's
# runtime. Each stage passes the runtime dir of the tree it is actually compiling.
#
# THE BACKEND IS PINNED HERE, AND HERE IS EVERY COMPILER SELF-BUILD THERE IS. Rung -1, the seed's
# own attempt, rung 0 and every pinned ladder stage all funnel through this one function, so one
# pin covers the whole ladder and cannot miss a stage.
#
# THE DEFECT IT CLOSES, measured on run 30376861988 (owner: *"tem erros e ainda finalizou verde"*).
# The committed `bootstrap/teko.c` used to be wagon 15's compiler, whose default backend was C. It
# is now THIS wagon's compiler, whose default is NATIVE — and the native backend cannot yet build
# the compiler (`fixpoint_gate.sh`: *"gen1 defaults to the NATIVE backend, which cannot yet build
# the compiler"*). So the moment the seed was replaced, rung -1 began failing on the aggregate-push
# degrau, the ladder absorbed it, and the leg reported SUCCESS having walked two pinned rungs
# instead of using the seed at all. Green by detour, and the seed unproven.
#
# NARROW ON PURPOSE. `fixpoint_gate.sh` records what a wide pin costs: `TEKO_BACKEND=c` *"LEAKS
# INTO EVERYTHING THAT RUNS BELOW IT"* — it turned `diagnostics.tkr` green by compiling the suite
# down the road we are retiring. This function builds THE COMPILER and nothing else; the suite, the
# regressors and every user project keep the native default, which is what they exist to test.
#
# TEKO_SELFHOST_BACKEND IS THE PER-PLATFORM HOOK the staged plan needs (owner, 2026-07-28): a leg
# whose native route is ready sets it to `native` and proves itself, while every other leg inherits
# `c` and keeps behaving exactly as it does today. It defaults to `c` because that is the only
# route that self-hosts right now.
build_project() {
  bin="$1"; proj_dir="$2"; out="$3"; logfile="$4"; rt_dir="${5:-}"
  bp_backend="${TEKO_SELFHOST_BACKEND:-c}"
  if [ -n "$rt_dir" ]; then
    ( cd "$proj_dir" && TK_RT_DIR="$rt_dir" TEKO_BACKEND="$bp_backend" "$bin" . -o "$out" --no-verify --release ) >"$logfile" 2>&1
  else
    ( cd "$proj_dir" && TEKO_BACKEND="$bp_backend" "$bin" . -o "$out" --no-verify --release ) >"$logfile" 2>&1
  fi
}

# rt_dir_of BASE — echoes BASE's in-tree runtime dir (src/runtime, then the bundled-install
# runtime layout), or empty when neither exists (the caller then leaves the compiler's own
# argv[0]-relative resolution in charge).
rt_dir_of() {
  base="$1"
  if [ -f "$base/src/runtime/teko_rt.h" ]; then
    printf '%s\n' "$base/src/runtime"
  elif [ -f "$base/runtime/teko_rt.h" ]; then
    printf '%s\n' "$base/runtime"
  else
    printf '%s\n' ""
  fi
}

# resolve_bin DIR — echoes the built teko binary under DIR (teko or teko.exe), failing if
# neither is an executable file.
resolve_bin() {
  dir="$1"
  if [ -x "${dir}/teko" ]; then
    printf '%s\n' "${dir}/teko"
  elif [ -x "${dir}/teko.exe" ]; then
    printf '%s\n' "${dir}/teko.exe"
  else
    return 1
  fi
}

# gen0_to_gen1 ORIGIN — THE DOUBLING. The compiler now sitting in $OUT_DIR was built from the tip's
# source by ORIGIN, whatever route reached it: that binary is gen0. This moves it to `.gen0/` and
# has IT rebuild the identical source, so what $OUT_DIR finally holds is gen1 — and every caller
# downstream (produce_assets.sh, fixpoint_gate.sh, every workflow) keeps its contract unchanged,
# because the contract was always "the tip's compiler at $OUT_DIR".
#
# WHY THE SECOND BUILD IS NOT WASTE, stated in the terms that make it worth its ~90s. gen0 was
# LOWERED by an older compiler — the release, or the degrau's C — so gen0's `teko.c` is that older
# algorithm's rendering of this tree. gen1's is THIS tree's algorithm rendering itself, which is
# the only C that can honestly be committed as the next bootstrap (owner ruling 2026-07-28: the
# harvest is *"colhido do gen1 (a última geração que emite C)"*). It is also the generation the
# fixpoint stands on, and standing it on gen0 would mix "did the new compiler change?" with "did
# the old compiler lower it differently?" — two questions, one red.
#
# THE ARTEFACT IT LEAVES: $OUT_DIR/teko.c IS gen1's C, because gen0 emitted it while producing
# gen1. gen0's own C is kept beside gen0 for diagnosis rather than overwritten in place.
#
# ON FAILURE IT LEAVES NO BINARY AT $OUT_DIR, deliberately: gen0 was MOVED, not copied, so a lane
# that ignored this function's status cannot go on holding gen0 while believing it holds gen1.
gen0_to_gen1() {
  g_origin="$1"
  if ! g_gen0="$(resolve_bin "$OUT_DIR")"; then
    log "FATAL: $g_origin reported success but left no teko binary in $OUT_DIR — no gen0, no gen1."
    return 1
  fi
  g_stage="$PWD/.gen0"
  rm -rf "$g_stage"
  mkdir -p "$g_stage"
  mv "$g_gen0" "$g_stage/$(basename "$g_gen0")"
  [ -f "$OUT_DIR/teko.c" ] && cp "$OUT_DIR/teko.c" "$g_stage/teko.c"
  g_bin="$g_stage/$(basename "$g_gen0")"
  log "gen0 ready, built by $g_origin ($("$g_bin" --version 2>&1 | head -n1)) — building gen1 = gen0(source)"
  g_log="$(mktemp)"
  if ! build_project "$g_bin" "$PWD" "$OUT_DIR" "$g_log" "$(rt_dir_of "$PWD")"; then
    log "FATAL: gen0 does not rebuild the source it came from — the chain breaks at gen1."
    log "----- gen0's build of the tip (failure) -----"
    sed 's/^/teko-ci:   | /' "$g_log" >&2
    rm -f "$g_log"
    return 1
  fi
  cat "$g_log"
  rm -f "$g_log"
  phase_mark "gen1 = gen0(source)"
  log "gen1 ready at $OUT_DIR — and $OUT_DIR/teko.c, when present, is gen1's own emitted C"
  return 0
}

# logical_head — the commit whose ancestry should be compared against $BASE_BRANCH. A
# `pull_request`-triggered checkout sits on GitHub's synthetic merge commit (parent 1 = the
# base branch, parent 2 = the PR's own tip) — merge-basing against parent 1 would trivially
# return the base branch itself, defeating the fallback. When a second parent exists, use it;
# otherwise (a plain, non-merge HEAD) use HEAD as-is.
logical_head() {
  if head2="$(git rev-parse -q --verify HEAD^2 2>/dev/null)"; then
    printf '%s\n' "$head2"
  else
    git rev-parse HEAD
  fi
}

# ensure_full_history — the fast path never needs git history at all, so CI checkouts stay
# shallow by default; the fallback does need enough of it to compute a merge-base, so this
# unshallows once, only now that the fallback has actually been engaged.
ensure_full_history() {
  if [ "$(git rev-parse --is-shallow-repository)" = "true" ]; then
    git fetch --unshallow origin
  fi
  git fetch origin "$BASE_BRANCH"
}

declared_degrau_rung() {
  if [ "$DEGRAU_RC" != "0" ]; then
    log "rung -1: no degrau is declared — the committed C, if any, is this train's OUTPUT and is"
    log "         NOT an input. Skipping to the published release, which is where the chain starts."
    return 1
  fi
  cc_src="$DEGRAU_C"
  # On Windows the toolchain MUST be clang, not MinGW gcc (owner ruling 2026-08-05). MinGW gcc is
  # pathologically slow on the 10 MB bootstrap C (its -O2 optimizer is superlinear — a single
  # produce step measured 55 min), its separate `cc1` backend breaks under any PATH wrapper
  # (`cannot execute 'cc1': CreateProcess`), and its MSVC-family linker has no `m.lib` so `-lm` is a
  # hard link error. clang (x86_64-pc-windows-msvc, already on the runner) is monolithic, fast, and
  # needs no libm — the same Windows rules build_cc_argv already applies for gen1 and beyond.
  deg_cc="cc"; deg_std="-std=c2x"; deg_libm=""; deg_tgt=""; deg_pthread="-pthread"; deg_syslibs=""
  case "$(uname -s 2>/dev/null)" in
    # Windows: the §16 sync primitives call WaitOnAddress/WakeByAddressSingle/WakeByAddressAll, which
    # live in Synchronization.lib (an API-set lib, NOT auto-linked like kernel32). teko's own run_cc
    # adds it for gen1+ via [extern.libs.windows], but this rung -1 links the raw C directly, so it
    # names the lib here or the link dies with LNK2019 unresolved externals.
    MINGW*|MSYS*|CYGWIN*|Windows_NT) deg_cc="clang"; deg_std="-std=c23"; deg_libm=""; deg_tgt="--target=x86_64-pc-windows-msvc"; deg_pthread=""; deg_syslibs="-lSynchronization" ;;
  esac
  if ! command -v "$deg_cc" >/dev/null 2>&1; then
    log "rung -1: a degrau is declared at $cc_src but no $deg_cc is on PATH — the forced seed cannot be built"
    return 1
  fi
  cc_out="$PWD/.rung-c"
  rm -rf "$cc_out"; mkdir -p "$cc_out"
  cc_log="$(mktemp)"
  # NO `-fno-pie -no-pie` HERE, and that is a measurement, not an oversight. Disabling PIE was the
  # standing hypothesis for the generation-to-generation slowdown; measured on the wagon it made the
  # x86_64 lane SLOWER (780s -> 869s), so the flag was reverted everywhere. It survived in this
  # function only because rung -1 was written while the experiment was still live.
  log "rung -1: building the degrau's compiler from $cc_src (cc=$deg_cc)"
  if ! "$deg_cc" "$deg_std" $deg_tgt -w -O2 $deg_pthread ${TEKO_DEGRAU_LDFLAGS:-} \
        -I src/runtime -I src/assert \
        "$cc_src" src/runtime/teko_rt.c src/assert/assert.c $deg_libm $deg_syslibs \
        -o "$cc_out/teko" >"$cc_log" 2>&1; then
    log "rung -1: the declared C did not compile — the forced seed cannot be built. cc said:"
    sed 's/^/teko-ci:   | /' "$cc_log" >&2
    rm -f "$cc_log"
    return 1
  fi
  rm -f "$cc_log"
  log "rung -1: degrau compiler ready ($("$cc_out/teko" --version 2>&1 | head -n1))"
  cc_tip_log="$(mktemp)"
  if ! build_project "$cc_out/teko" "$PWD" "$OUT_DIR" "$cc_tip_log" "$PWD/src/runtime"; then
    log "rung -1: the degrau's compiler could not build the tip — the forced seed does not reach it."
    log "----- tip build with the declared-C compiler (failure) -----"
    sed 's/^/teko-ci:   | /' "$cc_tip_log" >&2
    rm -f "$cc_tip_log"
    return 1
  fi
  cat "$cc_tip_log"
  rm -f "$cc_tip_log"
  phase_mark "rung -1 (declared degrau, no ladder)"
  log "rung -1: gen0 was built from the DECLARED C — NO LADDER WAS WALKED"
  return 0
}

# THE DECLARED DEGRAU IS THE SEED — FORCED — AND ITS FAILURE IS FATAL (owner ruling 2026-08-18,
# CLAUDE.md "PROVENANCE/reseed"). Provenance is REVOKED: when `bootstrap/DEGRAU` is declared, the C
# it names IS gen0's seed, used DIRECTLY, and the published release and the pinned SHA ladder are
# NOT tried at all — not before it, not after it. A degrau is only ever DECLARED because the
# released seed cannot build this tip, so "fall back to the release when the degrau fails" would
# fall back to a seed we already know is broken, wear the fixpoint green by detour, and — worst —
# publish gen0 from the 0.3.0.31 release instead of from this tree's own current-syntax compiler.
# Measured on PR #110: the fall-through did exactly that, and the release died on `T?`/`i128`/`Ref<T>`
# because it predates their removal. So when a degrau is declared and it cannot build the tip, CI
# FAILS HERE, IMMEDIATELY, WITHOUT PROBING ANYTHING OLDER. What ends the degrau is DELETING THE
# DECLARATION the day the released seed reaches the tip again — never a silent fallback.
if [ "$DEGRAU_RC" = "0" ]; then
  if declared_degrau_rung; then
    gen0_to_gen1 "rung -1 (the DECLARED degrau — forced seed)" || exit 1
    exit 0
  fi
  log "FATAL: a degrau is DECLARED ($DEGRAU_FILE) — it IS the forced seed — but it could not build"
  log "the tip (the failure is above). Owner ruling 2026-08-18: with a declared degrau there is NO"
  log "fallback to the published release and NO pinned ladder; the release predates this tree's"
  log "syntax and probing it would only report the wrong failure while burying this one."
  log "Fix bootstrap/teko.c so it compiles this tree, or DELETE bootstrap/DEGRAU to return to the"
  log "normal released-seed chain."
  exit 1
fi

# ── NO DEGRAU DECLARED: THE NORMAL RELEASED-SEED CHAIN, UNCHANGED, RUNS BELOW ──────────────────
# Everything from here down (fast-path release, rung 0 committed seed, the pinned SHA ladder) is
# reached ONLY when no degrau is declared. With a degrau declared, the block above has already
# either produced gen1 or failed the run — it never reaches this point.

# THE SEED MUST EXIST BEFORE IT CAN FAIL. With no degrau declared, the published release IS the
# chain's first link — so a missing seed is not a slow path, it is a broken premise, and the
# fallbacks below cannot repair it: rung 0 needs a committed blob this train no longer ships and
# the SHA ladder needs rungs the squash-merge destroyed. Saying so HERE names the real cause;
# letting it fall through would report "the tip is unreachable after N stages" instead.
if ! command -v "$SEED_BIN" >/dev/null 2>&1; then
  log "FATAL: no degrau is declared and '$SEED_BIN' is not on PATH — the chain has no first link."
  log "Provision the published release first (scripts/ci_provision_teko.sh <label>), or, if the"
  log "release genuinely cannot build this tree, declare the degrau:"
  degrau_recipe
  exit 1
fi

FAST_LOG="$(mktemp)"
if build_project "$SEED_BIN" "$PWD" "$OUT_DIR" "$FAST_LOG"; then
  cat "$FAST_LOG"
  log "the published release built gen0 directly — fast path, no fallback engaged"
  phase_mark "release -> gen0 (fast path, no ladder)"
  rm -f "$FAST_LOG"
  gen0_to_gen1 "the published release" || exit 1
  exit 0
fi
log "seed FAILED to build the tip directly — engaging the staged bootstrap ladder"
# WHY, NOT JUST THAT. This branch used to discard $FAST_LOG entirely: the log said the seed
# failed and never said what it said. That is the single most expensive unexplained line in the
# pipeline — engaging the ladder costs 392s of a 780s job on x86_64, 215s of 412s on arm64 — and
# it was unauditable, so "why does the seed fail" only ever had guesses. Owner ruling on the
# regressor's twin defect, 2026-07-27: *"Isso não seria problema em diagnosticar se estivesse
# imprimindo a saída completa do comando de build"*. Same rule, dearer instance.
log "----- the seed's own build of the tip (failure) — this is WHY the ladder is engaging -----"
sed 's/^/teko-ci:   | /' "$FAST_LOG" >&2
log "----- end of the seed's failed build -----"

# ── RUNG 0: THE COMMITTED SEED. Tried BEFORE the pinned SHA ladder, and the ordering is the
# whole point (owner acceptance criterion 2026-07-26 — the counter-machine's PR on the ORG must
# go green FIRST TRY).
#
# The pinned ladder below stands on two intermediate commits of this train. The merge train
# SQUASH-merges its wagons, so those SHAs never become reachable from `main`, and
# `ensure_full_history`'s `git fetch --unshallow origin` fetches BRANCHES — not the refs of a
# squashed-and-deleted wagon. On the org, therefore, the ladder's first `git checkout <rung>` is
# a guaranteed failure: the ladder depends on repository state that the merge strategy destroys.
#
# `bootstrap/seeds/` does not. It is IN THE TREE, sha256-verified against SEEDS.sha256 before
# it is decompressed, and it is cut FROM the train's own tip — so it builds the tip DIRECTLY,
# with no rungs at all. pr.yml's `seed-debut` proves exactly that on all five hosts on every
# full run, which is why this is a rung and not a hope.
#
# It is deliberately rung 0 and not rung -1: the newest RELEASED seed is still tried first (fast
# path above), because the committed blob is a TRANSITIONAL .31 measure that the first .32 wagon
# deletes. When it goes, this rung self-disables — `commit_seed_bin` finds no manifest and says so
# — and the fast path plus the ladder are what remain, unchanged.
#
# host_seed_label — the committed seeds are keyed by HOST THAT RUNS THE COMPILER, not by release
# target, so this derives the host from uname rather than taking a label from the caller: a seam
# the caller could get wrong is a seam that silently picks the wrong blob.
host_seed_label() {
  hs_os="$(uname -s 2>/dev/null || echo unknown)"
  hs_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$hs_os" in
    Linux)
      case "$hs_arch" in
        x86_64|amd64)  printf '%s' "linux-x86_64" ;;
        aarch64|arm64) printf '%s' "linux-arm64" ;;
        *)             printf '%s' "" ;;
      esac ;;
    Darwin) printf '%s' "macos-arm64" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT)
      case "$hs_arch" in
        x86_64|amd64)  printf '%s' "windows-x86_64" ;;
        *)             printf '%s' "" ;;
      esac ;;
    *) printf '%s' "" ;;
  esac
}

# commit_seed_rung — provision bootstrap/seeds/'s blob for THIS host and build the tip with it.
# Returns 0 only when the tip actually built; every giving-up path logs WHY, because a rung that
# fails silently is indistinguishable from a rung that was never tried.
commit_seed_rung() {
  cs_label="$(host_seed_label)"
  if [ -z "$cs_label" ]; then
    log "rung 0: no committed host seed exists for $(uname -s)/$(uname -m) — skipping"
    return 1
  fi
  cs_manifest="${TEKO_SEEDS_DIR:-bootstrap/seeds}/SEEDS.sha256"
  if [ ! -f "$cs_manifest" ]; then
    log "rung 0: no committed seed manifest at $cs_manifest — skipping (expected once .32 drops it)"
    return 1
  fi
  cs_log="$(mktemp)"
  if ! TEKO_SEED_PREFER_COMMITTED=1 sh scripts/ci_provision_teko.sh "$cs_label" >"$cs_log" 2>&1; then
    log "rung 0: the committed seed for '$cs_label' could not be provisioned:"
    sed 's/^/teko-ci:   | /' "$cs_log" >&2
    rm -f "$cs_log"
    return 1
  fi
  rm -f "$cs_log"
  cs_bin=""
  if [ -x .seed/teko ]; then cs_bin="$PWD/.seed/teko"
  elif [ -x .seed/teko.exe ]; then cs_bin="$PWD/.seed/teko.exe"
  else
    log "rung 0: provisioning reported success but no .seed/teko[.exe] is executable — skipping"
    return 1
  fi
  cs_tip_log="$(mktemp)"
  if build_project "$cs_bin" "$PWD" "$OUT_DIR" "$cs_tip_log" "$(rt_dir_of "$PWD")"; then
    cat "$cs_tip_log"
    rm -f "$cs_tip_log"
    log "rung 0: the COMMITTED seed ('$cs_label') built the tip directly — no SHA ladder needed"
    return 0
  fi
  log "rung 0: the committed seed ('$cs_label') could not build the tip either:"
  tail -20 "$cs_tip_log" | sed 's/^/teko-ci:   | /' >&2
  rm -f "$cs_tip_log"
  return 1
}

# declared_degrau_rung — RUNG -1: build the compiler from the C a DEGRAU DECLARATION names, and let
# THAT build gen0. Tried before every other rung, and when it works there is no ladder at all: no
# pinned SHAs, no ancestor checkouts, no network beyond the checkout already in hand.
#
# THE IDEA IS THE OWNER'S (2026-07-27): *"não precisamos construir escada na org, apenas no vagão e
# versionar a saída teko.c"* — walk the ladder ONCE, on the wagon, and commit the C it produced.
#
# WHAT CHANGED ON 2026-07-28, and it is the whole reason this rung is now gated by a declaration:
# the C the wagon commits is no longer only an emergency bridge, it is the train's HARVESTED
# OUTPUT. Presence therefore stopped meaning "the seed cannot build this tree", and a rung that
# still keyed off presence would divert gen0 through `cc` on every push, forever, without a line in
# the log saying why. *"Só podemos usar teko.c se e somente se identificarmos degrau."*
#
# WHY A `.c` AND NOT A BINARY, which is what `commit_seed_rung` (rung 0) was designed to hold: a
# binary is per-host, so the same bootstrap needs five blobs and each one is opaque. A `teko.c` is
# ONE file that every host specialises with its own `cc` — the same property that makes it the
# published bootstrap format in the first place.
#
# WHY THIS SOLVES WHAT NOTHING ELSE COULD. The released seed cannot build the tip, and after the
# cast-width debt was paid the reason moved from the checker to codegen: `cyclic value-type
# dependency` on `checker::Ptr.inner: Type | null`, a recursion whose fix ALREADY EXISTS in the
# source (`3b0e480`, `315f0d0`) but not in the seed's compiled-in algorithm. Source is only DATA to
# an already-built binary, so no repository change can reach back and fix it. A committed `teko.c`
# carries that fix compiled INTO it — which is why this rung works where every other approach was
# stuck choosing between rewriting the compiler's foundational recursive type and keeping a ladder.
#
# THE `.c` IS A GENERATION'S OUTPUT, and since 2026-07-28 it is gen1's specifically — the last
# generation the chain builds down the C route. Because the file is TRACKED, `no_emitted_c.sh`
# ignores it for free: that gate defines an emission as a `.c` git does NOT track.
#
# TRANSITIONAL BY DESIGN, AND THE DECLARATION IS WHAT EXPIRES. Once the published release reaches
# the tip again, `bootstrap/DEGRAU` is deleted and this rung self-disables in the same commit —
# the C may stay in the tree as a harvested payload without ever being walked on again. Nothing
# here should outlive that (owner: *"dado que morre no próximo trem, não tem motivos"*).

if commit_seed_rung; then
  rm -f "$FAST_LOG"
  gen0_to_gen1 "rung 0 (the committed host seed)" || exit 1
  exit 0
fi
log "rung 0 did not reach the tip — falling through to the PINNED SHA ladder (canonical-repo only:"
log "its rungs are unreachable wherever the wagons were squash-merged)"

if ! ensure_full_history; then
  log "FATAL: could not fetch '$BASE_BRANCH' history from origin — no fallback path exists"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  exit 1
fi

HEAD_FOR_MERGE_BASE="$(logical_head)"
if ! MERGE_BASE_SHA="$(git merge-base "$HEAD_FOR_MERGE_BASE" FETCH_HEAD)"; then
  log "FATAL: no merge-base found between the tip and origin/$BASE_BRANCH — no fallback path exists"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  exit 1
fi

if [ "$MERGE_BASE_SHA" = "$HEAD_FOR_MERGE_BASE" ]; then
  log "FATAL: the tip IS the merge-base with origin/$BASE_BRANCH — there is no bootstrap gap to bridge"
  log "the seed-fallback invariant only requires the seed to build $BASE_BRANCH; a failure here is a real regression, not a capability gap"
  log "----- seed build of the tip (failure) -----"
  cat "$FAST_LOG"
  exit 1
fi

# The ladder worktree and every stage's output live INSIDE the workspace, not the system temp
# dir: on the Windows runners the C toolchain demonstrably works on the workspace drive but
# fails opaquely when teko builds from %TEMP% (proven by probe logs — Teko compiles, cc dies).
# Same-workspace scratch also keeps everything on one filesystem.
WORKTREE_DIR="${GITHUB_WORKSPACE:-$PWD}/.teko-seed-fallback-wt"
rm -rf "$WORKTREE_DIR"
git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1 || true
git worktree add --detach "$WORKTREE_DIR" "$MERGE_BASE_SHA" >/dev/null

MAX_STAGES=4
MAX_PROBES=64
CURRENT_BIN="$SEED_BIN"
CURRENT_DESC="the released seed"
STAGE=0
PROBE_FROM="$MERGE_BASE_SHA"
LAST_RUNG=""
TIP_RT_DIR="$(rt_dir_of "$PWD")"
TIP_LOG="$(mktemp)"

# THE PINNED PAIR. Discovered by the probe below, confirmed green in PR #92 (run 30158725410):
# rung 1 is the last commit the released 0.3.0.30 seed can build; rung 2 is the last commit rung 1's
# compiler can build. BOTH are needed — that run shows the seed probing and rejecting rung 2 — so
# do not "optimize" the first one away. Refresh with TEKO_LADDER_DISCOVER=1 (by hand, never in CI)
# when a pin goes stale, and paste the rungs the discovery log names back into this line.
#
# THE INVARIANT THAT WAS MISSING, and the measurement that found it (2026-07-31, run 30613192858 —
# nine artifact legs, docs/medicoes/2026-07-31-seed-compat-e-escada.md). A RUNG OLDER THAN THE SEED'S
# OWN RELEASE COMMIT IS POISON. These two are 0.3.0.30-era (2026-07-24) and were discovered for the
# 0.3.0.30 seed; the released seed is now 0.3.0.31-beta (tag v0.3.0.31-beta = 4e6c4e4b), and 0.3.1
# REMOVED `i128`/`u128`/`f16` and the `T?` sugar. Building rung 1 with that seed was measured here:
# 124 removed-type diagnostics across ~40 files. The ladder walks BACKWARDS across a language
# removal, so it cannot climb — and it says so only when the seed itself fails, which is why the
# defect stayed latent until a tip stopped being seed-buildable.
#
# THESE PINS ARE KNOWINGLY LEFT AS THEY ARE, and that is a report, not an oversight. Discovery cannot
# replace them from this era: `git merge-base HEAD origin/main` IS 4e6c4e4b, the seed's own release
# commit, so every candidate the probe can reach (newest first-parent ancestor at-or-before the
# merge-base) is the seed or older than it — a rung with zero capability gain, or the pre-0.3.1 wall.
# The ladder has nothing left to climb until a NEWER seed is released (from a commit >= c64178e9);
# until then the invariant that keeps CI green is the one the unit tier now guards, that the tip
# stays buildable by the published seed (`compiler_sources_carry_no_seed_hostile_match_arm`).
LADDER_RUNGS="71c763d0ccec64df9fcd6c285a6782c642254e38 071c9c172f70c4fec5ff495e285cfc9cdef97fcb"

# build_rung SHA STAGE — check the ladder worktree out at SHA and build it with $CURRENT_BIN into a
# stage-private output dir, echoing that dir. Returns the build's own status.
build_rung() {
  rung_sha="$1"; rung_stage="$2"
  RUNG_OUT="$WORKTREE_DIR/.rung-out-$rung_stage"
  git -C "$WORKTREE_DIR" checkout -q --detach "$rung_sha"
  # Clean stale artifacts of the previous stage, but PRESERVE every .rung-out-* — those hold the
  # compilers this ladder is standing on (including the one about to run).
  git -C "$WORKTREE_DIR" clean -fdxq -e '.rung-out-*'
  rm -rf "$RUNG_OUT"
  mkdir -p "$RUNG_OUT"
  build_project "$CURRENT_BIN" "$WORKTREE_DIR" "$RUNG_OUT" "$RUNG_LOG" "$(rt_dir_of "$WORKTREE_DIR")"
}

# stale_pin_fatal SHA — the M.3 heart of the pinned ladder: a pinned rung that will not build means
# the pin is OBSOLETE, and this says so and stops. It deliberately does NOT fall back to probing:
# a silent fallback restores the ~75 wasted builds per lane AND hides the staleness, which is how
# the determinism the owner asked for would quietly evaporate.
stale_pin_fatal() {
  log "FATAL: pinned ladder rung $1 FAILED to build — the pin in LADDER_RUNGS is obsolete."
  log "This script does NOT fall back to probing (that is what the pins removed)."
  log "READ THE RUNG LOG BELOW FIRST. If it names REMOVED types or syntax ('type i128 was removed',"
  log "'the \`T?\` nullable sugar has been removed'), the rung PREDATES the released seed and no"
  log "refresh can help: discovery only walks BACK from the merge-base with $BASE_BRANCH, and when"
  log "that merge-base is at-or-before the seed's own release commit every candidate is the seed or"
  log "older. The fix is then to keep the TIP buildable by the published seed, or to release a newer"
  log "seed — never to re-pin. (Measured 2026-07-31: docs/medicoes/2026-07-31-seed-compat-e-escada.md)"
  log "Otherwise, when the rung failed on a capability the seed genuinely lacks, refresh by hand:"
  log "  TEKO_LADDER_DISCOVER=1 sh scripts/build_with_seed_fallback.sh   # run by hand, not in CI"
  log "then paste the rung SHAs it reports into LADDER_RUNGS in this file."
  log "----- pinned rung build log ($1) -----"
  sed 's/^/teko-ci:   | /' "$RUNG_LOG" >&2
  exit 1
}

if [ "${TEKO_LADDER_DISCOVER:-0}" != "1" ] && [ -n "$LADDER_RUNGS" ]; then
  log "using the PINNED ladder (set TEKO_LADDER_DISCOVER=1 to rediscover the rungs instead)"
  # Everything up to here — the fast-path attempt, rung 0, ensure_full_history, the scratch
  # worktree — is one lump in the phase table: by the time it is legible as a NAME here, it is
  # already done, so this is the earliest point that can honestly report its combined cost.
  phase_mark "seed + setup da escada"
  RUNG_LOG="$(mktemp)"
  for RUNG_SHA in $LADDER_RUNGS; do
    STAGE=$((STAGE + 1))
    if ! build_rung "$RUNG_SHA" "$STAGE"; then stale_pin_fatal "$RUNG_SHA"; fi
    if ! NEXT_BIN="$(resolve_bin "$RUNG_OUT")"; then
      log "FATAL: pinned rung $RUNG_SHA built but no teko/teko.exe was found in $RUNG_OUT"
      exit 1
    fi
    CURRENT_BIN="$NEXT_BIN"
    CURRENT_DESC="gen$STAGE(pinned rung $RUNG_SHA)"
    log "ladder stage $STAGE: built pinned rung $RUNG_SHA — that compiler is the new rung"
    # THE PER-GENERATION REPORT, printed on SUCCESS and not only on failure. `phase_mark` gives
    # the stage's wall clock; this gives its BREAKDOWN — checker, codegen, cc, peak RSS — which is
    # the only way to tell "this generation did more work" from "this generation ran the same work
    # slower". Owner observation, 2026-07-27: *"cada degrau fica mais lento, e pior, eu notei isso
    # até buildando o mesmo código na mesma sessão, a próxima geração parece ficar mais lenta"*.
    # A degradation across generations is invisible to the stage timings alone, because each stage
    # compiles a DIFFERENT (larger) tree; only the per-phase numbers separate the two causes. The
    # log is ~15 lines per stage — the cheapest possible price for the one measurement that can
    # confirm or kill that observation.
    log "----- gen$STAGE build report (rung $RUNG_SHA) -----"
    sed 's/^/teko-ci:   | /' "$RUNG_LOG" >&2
    phase_mark "ladder gen$STAGE"
  done
  rm -f "$RUNG_LOG"
  if ! build_project "$CURRENT_BIN" "$PWD" "$OUT_DIR" "$TIP_LOG" "$TIP_RT_DIR"; then
    log "FATAL: the tip does not build with $CURRENT_DESC — the last pinned rung no longer reaches"
    log "this tip, i.e. the pins are stale for the capability this branch adds. Refresh them with"
    log "TEKO_LADDER_DISCOVER=1 (by hand) and update LADDER_RUNGS."
    log "----- tip build with the last pinned rung (failure) -----"
    cat "$TIP_LOG"
    exit 1
  fi
  cat "$TIP_LOG"
  phase_mark "gen0 (after $STAGE pinned ladder stage(s))"
  rm -f "$FAST_LOG" "$TIP_LOG"
  log "staged bootstrap complete — gen0 built by $CURRENT_DESC after $STAGE PINNED ladder stage(s)"
  gen0_to_gen1 "$CURRENT_DESC" || exit 1
  exit 0
fi

log "DISCOVERY MODE: probing for the ladder rungs (TEKO_LADDER_DISCOVER=1) — report the rungs this"
log "run names back into LADDER_RUNGS; CI must never take this path."

while :; do
  # Does the compiler we currently hold reach the tip?
  if build_project "$CURRENT_BIN" "$PWD" "$OUT_DIR" "$TIP_LOG" "$TIP_RT_DIR"; then
    cat "$TIP_LOG"
    rm -f "$FAST_LOG" "$TIP_LOG"
    log "staged bootstrap complete — gen0 built by $CURRENT_DESC after $STAGE ladder stage(s)"
    gen0_to_gen1 "$CURRENT_DESC" || exit 1
    exit 0
  fi
  if [ "$STAGE" -eq 0 ]; then
    log "confirmed: the released seed cannot build the tip — probing back from merge-base $MERGE_BASE_SHA"
  else
    log "stage $STAGE compiler ($CURRENT_DESC) still cannot build the tip — probing for a newer rung"
    log "tip build error tail:"
    tail -8 "$TIP_LOG" | sed 's/^/teko-ci:   | /' >&2
  fi

  STAGE=$((STAGE + 1))
  if [ "$STAGE" -gt "$MAX_STAGES" ]; then
    log "FATAL: the tip is still unreachable after $MAX_STAGES ladder stages — the capability gap"
    log "between the released seed and this tip is deeper than the ladder is allowed to climb."
    log "----- seed build of the tip (failure) -----"
    cat "$FAST_LOG"
    log "----- last stage's build of the tip (failure) -----"
    cat "$TIP_LOG"
    rm -f "$TIP_LOG"
    exit 1
  fi

  # Find the NEWEST first-parent ancestor at-or-before $PROBE_FROM that $CURRENT_BIN can build.
  RUNG_SHA="$PROBE_FROM"
  RUNG_OUT="$WORKTREE_DIR/.rung-out-$STAGE"
  RUNG_LOG="$(mktemp)"
  PROBES=0
  RUNG_RT_DIR=""
  while :; do
    git -C "$WORKTREE_DIR" checkout -q --detach "$RUNG_SHA"
    # Clean stale artifacts of the previous probe, but PRESERVE every .rung-out-* — those hold
    # the compilers this ladder is standing on (including the one about to run).
    git -C "$WORKTREE_DIR" clean -fdxq -e '.rung-out-*'
    rm -rf "$RUNG_OUT"
    mkdir -p "$RUNG_OUT"
    RUNG_RT_DIR="$(rt_dir_of "$WORKTREE_DIR")"
    if build_project "$CURRENT_BIN" "$WORKTREE_DIR" "$RUNG_OUT" "$RUNG_LOG" "$RUNG_RT_DIR"; then
      break
    fi
    if grep -q "cc failed to build the generated C" "$RUNG_LOG"; then
      log "FATAL: rung candidate $RUNG_SHA compiles at the Teko level but its C compile FAILED — this"
      log "is an ENVIRONMENTAL toolchain problem, not a capability gap; walking further back cannot fix it."
      log "----- full rung build log ($RUNG_SHA) -----"
      sed 's/^/teko-ci:   | /' "$RUNG_LOG" >&2
      rm -f "$RUNG_LOG" "$TIP_LOG"
      exit 1
    fi
    PROBES=$((PROBES + 1))
    if [ "$PROBES" -ge "$MAX_PROBES" ]; then
      log "FATAL: no buildable rung found within $MAX_PROBES first-parent steps of $PROBE_FROM (stage $STAGE)"
      log "----- last rung candidate $RUNG_SHA (failure) -----"
      cat "$RUNG_LOG"
      rm -f "$RUNG_LOG" "$TIP_LOG"
      exit 1
    fi
    if ! PARENT_SHA="$(git -C "$WORKTREE_DIR" rev-parse -q --verify "$RUNG_SHA^" 2>/dev/null)"; then
      log "FATAL: ran out of history walking back from $PROBE_FROM (stage $STAGE) — no buildable rung"
      log "----- last rung candidate $RUNG_SHA (failure) -----"
      cat "$RUNG_LOG"
      rm -f "$RUNG_LOG" "$TIP_LOG"
      exit 1
    fi
    log "probe: $CURRENT_DESC cannot build $RUNG_SHA — stepping back to $PARENT_SHA"
    RUNG_SHA="$PARENT_SHA"
  done
  rm -f "$RUNG_LOG"

  # NO-PROGRESS GUARD. If this stage's probe landed on the very commit the previous stage
  # already built, a newer compiler reached no further — every commit in the base lineage that
  # is buildable at all is already behind us, so the tip needs a capability the tip ITSELF
  # introduces. That is a genuine bootstrap impossibility for this commit, not something more
  # stages can fix: say so instead of burning the remaining stages rebuilding the same rung.
  if [ "$RUNG_SHA" = "$LAST_RUNG" ]; then
    log "FATAL: no progress — stage $STAGE's probe landed on $RUNG_SHA again, the same rung stage"
    log "$((STAGE - 1)) already built. The tip requires a capability that no buildable ancestor"
    log "provides, i.e. one the tip itself introduces; a staged bootstrap cannot bridge that."
    log "----- seed build of the tip (failure) -----"
    cat "$FAST_LOG"
    log "----- last stage's build of the tip (failure) -----"
    cat "$TIP_LOG"
    rm -f "$TIP_LOG"
    exit 1
  fi
  LAST_RUNG="$RUNG_SHA"

  if ! NEXT_BIN="$(resolve_bin "$RUNG_OUT")"; then
    log "FATAL: rung build reported success but no teko/teko.exe binary was found in $RUNG_OUT"
    rm -f "$TIP_LOG"
    exit 1
  fi
  log "ladder stage $STAGE: built $RUNG_SHA ($PROBES probe(s) back from $PROBE_FROM) — that compiler is the new rung"

  # The next stage probes the window BETWEEN this rung and the tip: a newer rung than the one
  # we just built is the only thing that can carry us further, and re-probing from the same
  # point would rebuild what we already hold. Walking forward is not possible with a
  # first-parent walk, so the next probe starts again at the merge-base — but the compiler in
  # hand is strictly newer, so it reaches strictly further before being rejected. Convergence
  # is bounded by MAX_STAGES.
  CURRENT_BIN="$NEXT_BIN"
  CURRENT_DESC="gen$STAGE(rung $RUNG_SHA)"
  PROBE_FROM="$MERGE_BASE_SHA"
done
