#!/usr/bin/env sh
# scripts/fixpoint_gate.sh — THE SELF-HOSTING FIXPOINT, run inside each producing sublane.
#
# Owner ruling 2026-07-27: *"quando o vagão 20 fechar verde e coletarmos o teko.c que ele produziu,
# não haverá mais escada a não ser fazer build o dobrado (sobre o mesmo fonte) de 3 gens para bater
# gen2 e gen3, mas ambos gen1, 2 e 3 gerados por este último teko.c, não deverá mais emitir um
# teko.c"*, and then: *"pode colocar o fixpoint logo após as compilações nos ambientes (direto em
# cada sublane)"*.
#
# WHAT IT PROVES, and why it is NOT the ladder wearing a different hat. The ladder chained
# generations over DIFFERENT sources (distinct SHAs) to cross a capability gap. This chains
# generations over the SAME source to prove the compiler reproduces itself.
#
# THE CHAIN, AND WHICH LINK THE VERDICT COMPARES. Owner ruling 2026-07-28:
#
#   release 0.3.0.31 --TEKO_BACKEND=c--> gen0 --TEKO_BACKEND=c--> gen1 (emits teko.c)
#                    --native--> gen2 --native--> gen3            ASSERT gen2 == gen3
#
# The published release builds gen0 and gen0 builds gen1, both down the C route; gen1 then builds
# gen2 and gen2 builds gen3, both on the native backend. The first two links belong to
# `build_with_seed_fallback.sh` (the ladder, which hands this script its gen1); THIS script owns
# the last two and the verdict.
#
# THE VERDICT DOES NOT MOVE. Owner ruling 2026-07-28: *"gen1 nunca será igual a gen2 — são
# geradores diferentes (cc vs backend próprio do Teko). O veredito do fixpoint não muda: continua
# gen2 == gen3."* gen1 is deliberately NOT compared, and not because it is untrustworthy: it came
# out of `cc` over emitted C while gen2 comes out of Teko's own backend. Two different code
# generators emitting the same program cannot agree byte for byte — the same reason one
# `out/teko.c` compiled with `cc` and with `musl-gcc` yields two different assets. The fixpoint
# begins where the compiler starts feeding on its own output, and that is gen2.
#
# SUPERSEDED, RECORDED RATHER THAN DELETED: the 2026-07-27 ruling *"o fixpoint irá migrar de gen2
# === gen3 para gen1 === gen2, quando tivermos o último teko.c"* described a chain in which gen1
# and gen2 would eventually share a generator. Under the 0.3.1.0 chain they never do — gen1 is C
# route BY CONSTRUCTION (it is the generation that emits the teko.c we harvest) and gen2 is native
# — so the migration it anticipated has no arrival date and the operands are FIXED at gen2/gen3.
# This script therefore has ONE comparison, in every state of the world, and no mode that moves it.
#
# WHAT IS OBSERVED IS THE PROVENANCE OF gen1, AND THE CRITERION IS A DECLARATION, NOT A FILE.
# `scripts/degrau.sh` answers "is there a declared degrau?" for this gate and for the ladder, from
# one versioned claim (`bootstrap/DEGRAU`). Owner ruling 2026-07-28: *"só podemos usar teko.c se e
# somente se identificarmos degrau."* The OLD discriminator — "does `bootstrap/teko.c` exist?" —
# is retired here: under this chain the C is an OUTPUT, versioned after a green run, so its mere
# presence would have re-routed gen1 through `cc` forever and in silence. It changes nothing about
# the verdict either way; it is reported so a log reader knows which chain produced the gen1 in
# hand.
#
# WHY IT DID NOT EXIST UNTIL NOW, stated plainly because the gap is the interesting part: the
# fixpoint is cited as a standing guardrail in README.md, TEKO_MASTER_PLAN.md, TEKO_HISTORY.md,
# DECISION_LOG.md, docs/BUILDING.md and TEKO_ROADMAP_NET_CRYPTO.md — and no workflow ever computed
# it. Searched, 2026-07-27: the only generation-to-generation comparison in the repository lived in
# `theory_generation_decay.sh`, an EXPERIMENT written that same day. `pr.yml` builds a `gen2-mp`,
# but under TEKO_MEM_PARANOID — a memory probe that builds and discards, comparing nothing. Six
# documents asserted a gate that was never wired.
#
# THE PATH TRAP, and why gen2 and gen3 are built at the SAME output path. A compiler that bakes any
# part of its output path into the binary would make two builds differ for a reason that has
# nothing to do with the fixpoint, and the failure would read as a real non-determinism. So this
# builds gen2 at $W/out, MOVES it aside, deletes $W/out, and builds gen3 at the very same $W/out.
# Identical path, identical cwd, identical everything the build can observe about where it is.
#
# ZERO-C IS CHECKED HERE TOO, because the owner's stopping criterion is one event seen from two
# sides: *"um teko.c que não gera outro teko.c"* is the same moment as the native backend learning
# to build the compiler. A gen2/gen3 that still emits C is a fixpoint that has not arrived, even if
# the bytes match — so both are asserted, and the C check is reported separately from the byte
# check so a log reader can tell WHICH half is outstanding.
#
# THE EMISSION CHECK IS ARMED AGAIN, ON THE LEGS THAT GENERATE NATIVE. Owner ruling 2026-07-28:
# *"Só tem uma falha, gen2 e gen3 estão emitindo C e não deveriam, reabilite o check de emissão
# (somente nas pernas Linux), verá que não passará nada."* It was disarmed on 2026-07-27 (*"pode
# remover por hora a validação de no-c para este caso"*) for a state of the world in which every
# leg ran gen2/gen3 down the C route ON PURPOSE — enforcing it then would have red every run for
# doing what the version asked. That is no longer the state of the world on Linux: those four legs
# now pass `TEKO_FIXPOINT_BACKEND=native`, and a NATIVE generation emitting C is a defect in every
# version. So the arming is DERIVED FROM THE BACKEND rather than configured beside it (see
# `zero_c_enforced_default`) — the legs that still run `c` are untouched, and no future leg can
# flip to native while quietly keeping the emission check off.
#
# It is scoped to gen2 and gen3 ONLY — never the ladder, never gen1. Owner ruling 2026-07-27: *"ele
# tem que saber quem está testando, logo, ele não pode nem deve avaliar a escada e nem a gen1,
# apenas gen2 e 3"*. gen1 emits C BY CONSTRUCTION (it is the last generation built down the C
# route — that emission IS the teko.c this train harvests), so judging it would fail the gate for
# doing exactly what it must do. That ruling is also why the degrau declaration read below changes
# no operand: the ladder's business is the ladder's. This is also why `scripts/no_emitted_c.sh` is
# NOT wired: it sweeps the whole worktree and cannot tell whose emission it found. THIS gate can
# attribute, and that is the whole difference — it sweeps the output directory it just handed a
# named generation, and it BRACKETS each build with a before/after scan of the project tree, so
# every `.c` it names was written by the generation under test and by nothing else.
#
# Usage:  sh scripts/fixpoint_gate.sh <gen1-binary> [PROJECT_DIR] [WORK_DIR]
#
# Env:
#   TEKO_FIXPOINT_RT_DIR   runtime dir pinned onto each build (default: <PROJECT_DIR>/src/runtime).
#                          Same reason build_with_seed_fallback.sh pins it: a compiler resolves
#                          teko_rt.{h,c} relative to ITS OWN argv[0], so a generation living
#                          outside the tree would otherwise compile against the wrong runtime.
#   TEKO_FIXPOINT_BACKEND  which backend gen2 and gen3 are built with (default `c` — see
#                          `build_gen`, which is the ONLY place it is read). THE LEVER, and it is
#                          PULLED on the four Linux legs as of 2026-07-28: they pass `native`,
#                          from `scripts/ci_producer_matrix.sh`'s `fixpoint_backend` field. macOS
#                          and Windows keep `c` until the platform-sequenced plan reaches them.
#                          The default stays `c` so that a caller which names no backend gets the
#                          route that has always worked.
#   TEKO_FIXPOINT_ZERO_C   `1` to make the emission check BITE, `0` to report it. UNSET IS THE
#                          NORMAL CASE and it is DERIVED from the backend: a `native` generation
#                          that emits C is a defect on any leg, in any version, so native arms it
#                          and `c` does not. Setting it explicitly overrides the derivation in
#                          both directions — that is for the 0.3.1.4 wagon, which arms it on the
#                          C legs too as it retires the route.
#   TEKO_FIXPOINT_GEN1_C   the C emitted ALONGSIDE gen1 (default: <dirname gen1>/teko.c) — the
#                          harvest candidate this run stages as <WORK_DIR>/gen1.c.
#   TEKO_FIXPOINT_MODE     `degrau` | `normal` — force the PROVENANCE LABEL instead of observing
#                          the declaration. It selects no operand and no assertion (2026-07-28:
#                          the verdict is gen2 == gen3 in every state); it exists to test the
#                          observation itself. CI must let it observe.
#   TEKO_FIXPOINT_SOFT     set to 1 to REPORT the verdict and exit 0 anyway. Intended for the
#                          window in which the native backend cannot yet build the compiler at all
#                          (`fat-pointer receiver call not yet lowered (N2)`), where a hard failure
#                          here would mask every other signal in the lane rather than add one.
#                          It prints the same verdict either way — it never hides the result.
set -eu

# shellcheck source=scripts/degrau.sh
. "$(dirname "$0")/degrau.sh"

GEN1="${1:?usage: fixpoint_gate.sh <gen1-binary> [PROJECT_DIR] [WORK_DIR]}"
PROJ="${2:-$PWD}"
W="${3:-.fixpoint}"

log() { printf '%s\n' "fixpoint: $*" >&2; }

SOFT="${TEKO_FIXPOINT_SOFT:-0}"

# verdict_fail MESSAGE — the fixpoint does not hold. Fails the lane. THERE IS NO OTHER OUTCOME
# BESIDES PASSED, and that is a correction of my own design, not the original one.
#
# This script briefly carried a third verdict, NOT APPLICABLE, for the case where the native
# backend honest-stops before gen2 exists — on the reasoning that a fixpoint is a claim about two
# generations, so with no pair there is no answer, true or false. Owner ruling 2026-07-27:
# *"discordo veementemente, só há uma saída que pode ser verdadeira"* and *"se nem consegue gerar
# gen2, nem tem que perder tempo com testes, não vai passar"*.
#
# The reasoning was wrong because it measured the wrong thing. The fixpoint is not a curiosity
# about two binaries; it is the statement THIS COMPILER BUILDS ITSELF. A compiler that cannot
# produce gen2 has already failed that statement outright — the missing pair IS the failure, not
# an obstacle to observing one. Dressing it as "not applicable" turned the loudest possible defect
# into a neutral note, and downstream lanes went on spending time testing a compiler that cannot
# reproduce itself.
#
# The cost is real and accepted: a red `artifact` job turns every downstream lane into a SKIP. That
# is the correct shape. There is nothing to learn from testing a compiler that does not self-host.
verdict_fail() {
    log "VERDICT: FAILED — $1"
    if [ "$SOFT" = "1" ]; then
        log "TEKO_FIXPOINT_SOFT=1 — reporting without failing the lane. The verdict above stands."
        exit 0
    fi
    exit 1
}


[ -x "$GEN1" ] || { log "gen1 '$GEN1' is not an executable file"; exit 1; }
GEN1="$(cd "$(dirname "$GEN1")" && pwd)/$(basename "$GEN1")"
PROJ="$(cd "$PROJ" && pwd)"

RT_DIR="${TEKO_FIXPOINT_RT_DIR:-$PROJ/src/runtime}"
[ -d "$RT_DIR" ] || RT_DIR=""

rm -rf "$W"
mkdir -p "$W"
W="$(cd "$W" && pwd)"

# build_gen COMPILER LOGFILE — builds $PROJ at the FIXED path $W/out with COMPILER, dry
# (`--no-verify --release` — the test gate belongs to the test lanes, owner ruling 2026-07-27).
# Echoes nothing; the caller inspects $W/out.
# THE BACKEND IS PINNED HERE, INSIDE THE SUBSHELL, AND THAT SCOPE IS THE WHOLE POINT.
#
# `gen1` (the wagon's own compiler) defaults to the NATIVE backend, which cannot yet build the
# compiler — so without asking for the C route there is no gen2 and no fixpoint. `TEKO_BACKEND=c`
# is what the two-route plan restores it for (owner, 2026-07-27: *".31 com as duas rotas, .32
# ensina o nativo, .33 remove"* — resequenced by platform on the 0.3.1 lane, where the Linux leg
# goes native first and the C lives until 0.3.1.4).
#
# WHY NOT EXPORT IT IN THE WORKFLOW STEP, which is the obvious place: because a global
# `TEKO_BACKEND=c` LEAKS INTO EVERYTHING THAT RUNS BELOW IT, and that is not a hypothetical — it
# was measured. Running the suite with it set turned `diagnostics.tkr` red: the scenario
# *"an unsupported TEKO_TARGET is an honest compile error"* got exit 0, because `TEKO_TARGET` is
# only consulted while the NATIVE backend is active. The regressors exist to exercise the native
# route; handing them the C one makes them assert something else entirely, and `cwd_build` went
# "green" the same way — not fixed, just compiled by another road.
#
# So the variable lives in THIS subshell, around THESE two builds, and nowhere else. The suite
# keeps running on the native default, which is what it is for.
#
# ── THE LEVER, AND IT IS THIS LINE ────────────────────────────────────────────────────────────
# `bg_backend="$FIXPOINT_BACKEND"` below is where the 0.3.1.0 chain is switched on, and the value
# arrives from `scripts/ci_producer_matrix.sh`'s `fixpoint_backend` field — `native` on the four
# Linux legs since 2026-07-28, `c` on macOS and Windows until the platform-sequenced plan reaches
# them. The default when nobody names one stays `c`.
#
# HOW TO FLIP A LEG, in either direction: change that ONE word in the leg's row in
# `scripts/ci_producer_matrix.sh`. `pr.yml` passes it as `TEKO_FIXPOINT_BACKEND` on the fixpoint
# STEP — never on the job or the workflow. The scope argument above is the whole reason the
# variable is read HERE and nowhere else: a global pin leaks into the suite and turns regressors
# green by compiling them down another road.
#
# `pr.yml` IS THE ONLY CALLER THAT MATTERS, and that is by design. `nightly.yml` reads the same leg
# table but does NOT run this gate, and must not start. Owner ruling 2026-07-28: *"Nightly não tem
# que correr fixpoint mesmo, não é pra isso"* — the nightly proves the BETWEEN-runs property (same
# tree + same seed + same toolchain ⇒ same bytes, against the gated PR run's assets); this gate
# proves `gen2 == gen3` WITHIN one run. Two questions, two workflows. The `fixpoint_backend` field
# simply travels with the table it lives in.
#
# THE FOUR LINUX LEGS ARE EXPECTED TO BE RED WHILE THIS STANDS, and the red is the deliverable.
# Owner ruling 2026-07-28: *"verá que não passará nada."* The native backend does not build the
# compiler yet; `docs/memory/0.3.1.0-linux-native-first-stop.md` names the stop it reaches today,
# by address. The gate's job here is to report that address every run — softening it (a warning, a
# `continue-on-error`, a narrowed criterion) would delete the only number this lane exists to
# produce.
#
# The pin disappears entirely — not flipped, deleted — when the C route is retired (0.3.1.4 on the
# platform-sequenced plan), because then there is only one backend to pin it to.
FIXPOINT_BACKEND="${TEKO_FIXPOINT_BACKEND:-c}"

# build_gen COMPILER LOGFILE — see the block above. Brackets the build with `scan_project_c` so a
# `.c` written ANYWHERE in the project tree is attributable to the generation that wrote it.
build_gen() {
    bg_bin="$1"; bg_log="$2"
    rm -rf "$W/out"
    bg_backend="$FIXPOINT_BACKEND"
    # DIAGNOSTIC (2026-08-03, native-codegen-layer3): with the OOM cured (C6), the native self-build
    # now reaches a codegen SIGSEGV (M.1). TEKO_NATIVE_TRACE_ITEMS names the last per-item scoped
    # region entered before the crash, so the streamed log pins the exact function/const/type it
    # died on. Native-only (the trace is emitted solely by emit_native_x86); remove once pinned.
    bg_trace=""
    if [ "$bg_backend" = "native" ]; then bg_trace="1"; fi
    # DIAGNOSTIC (2026-08-03, native-rodata-const-align): the same native-only gate as
    # TEKO_NATIVE_TRACE_ITEMS above, for the macOS-arm64 native fixpoint "ld: pointer not aligned"
    # / Linux native gen1 const-corruption crash — encode_rodata's own reloc-alignment check names
    # the owning rodata symbol + offset of the first pointer-bearing relocation whose ABSOLUTE
    # __const/.rodata offset is not 8-aligned, straight to stderr, so the streamed log pins the
    # exact culprit const instead of only ld's nearest-preceding-symbol guess. Remove once pinned.
    bg_align_check=""
    if [ "$bg_backend" = "native" ]; then bg_align_check="1"; fi
    # DIAGNOSTIC (2026-08-03, native-region-crash): the same native-only gate as the two above,
    # for the arm64-macos/x86_64-glibc native fixpoint SIGSEGV inside `tk_region_alloc` reached
    # deep in the fused per-item lowering driver (`fuse_lower_item_arm64`/`emit_native_x86`,
    # `docs/design/native-lowering-oom-por-funcao-0.3.1.md`). The shared linear-scan register
    # allocator's own scan output is checked, per function, for an `InReg` assignment that
    # crosses a call yet holds a caller-saved physreg (a violation `IntervalSet`'s own doc
    # comment already names as forbidden) — straight to stderr, so the streamed log pins the
    # exact vreg/physreg/function whose value a later call would clobber, instead of leaving the
    # eventual corrupted-region-pointer read to surface as an unattributed SIGSEGV items later.
    # Remove once pinned.
    bg_call_safety_check=""
    if [ "$bg_backend" = "native" ]; then bg_call_safety_check="1"; fi
    # DIAGNOSTIC (2026-08-03, native-region-crash, ROUND 2): the symbolized macOS-arm64 backtrace
    # (head 284c91c) localized the SIGSEGV precisely to copy_encoded_func_to_current_region_arm64
    # (project.tks) doing tk_slice_push into a CORRUPT current region — region-STATE corruption, not
    # pointer alignment (the align check above returned null). This gate asserts, per item AND for the
    # virtual-main, that the current region really returned to the ROOT after each region_leave() in
    # the fused loop, BEFORE the copy-out commits — converting the distant tk_region_alloc SIGSEGV into
    # a named honest-stop naming the exact item ("item N/M") and both region handles, so the streamed
    # log pins whether the region stack is imbalanced there. Native-only; byte-identical when off.
    # Remove once pinned.
    bg_region_check=""
    if [ "$bg_backend" = "native" ]; then bg_region_check="1"; fi
    # DIAGNOSTIC (2026-08-03, native-rodata-const-align, ROUND 2): the align check above returned
    # null even though macOS ld rejected 'pointer not aligned in AAPCS64.gpr_arg+0x1320' — so the
    # bug is NOT a misaligned reloc offset but a const SIZE/OFFSET mismatch: a fat field allotted 8
    # bytes instead of 16 (a typeexpr_is_fat / is_fat_type disagreement) shifts every following
    # field, so a later pointer lands misaligned (macOS ld) AND the const's bytes read as garbage
    # (x86 gen1 crash — x86 tolerates unaligned reads, so its crash proves DATA corruption, not
    # alignment). This gate cross-checks each serialized aggregate const's ConstImage against its
    # registered layout (`check_const_image_size`, lower_const.tks) and honest-stops naming the
    # exact const + fat field + allotted-vs-required width, straight to the streamed log's tail.
    # Native-only; byte-identical when off. Remove once pinned.
    bg_const_size_check=""
    if [ "$bg_backend" = "native" ]; then bg_const_size_check="1"; fi
    scan_project_c "$W/project-c.before"
    # Stream the build LIVE (prefixed) to the lane log AND capture to $bg_log. Owner ruling
    # 2026-08-03: "expor tudo que ele faz e não ocultar nenhuma saída". Before, the build was
    # redirected to a file dumped ONLY on failure, so a mid-build OOM-kill (Killed: 9) lost the
    # buffered tail and hid WHERE it died. POSIX sh has no PIPESTATUS, so the compiler's exit code
    # is stashed in a status file (kept out of the streamed log by its own redirect) and returned;
    # `set +e`/`set -e` brackets keep the pipe's own failure from tripping the caller early.
    set +e
    if [ -n "$RT_DIR" ]; then
        { ( cd "$PROJ" && TK_RT_DIR="$RT_DIR" TEKO_NATIVE_TRACE_ITEMS="$bg_trace" TEKO_NATIVE_RODATA_ALIGN_CHECK="$bg_align_check" TEKO_NATIVE_CALL_SAFETY_CHECK="$bg_call_safety_check" TEKO_NATIVE_REGION_CHECK="$bg_region_check" TEKO_NATIVE_CONST_SIZE_CHECK="$bg_const_size_check" TEKO_BACKEND="$bg_backend" "$bg_bin" . -o "$W/out" --no-verify --release ); echo $? >"$W/bg_status"; } 2>&1 | tee "$bg_log" | sed 's/^/fixpoint:   | /' >&2
    else
        { ( cd "$PROJ" && TEKO_NATIVE_TRACE_ITEMS="$bg_trace" TEKO_NATIVE_RODATA_ALIGN_CHECK="$bg_align_check" TEKO_NATIVE_CALL_SAFETY_CHECK="$bg_call_safety_check" TEKO_NATIVE_REGION_CHECK="$bg_region_check" TEKO_NATIVE_CONST_SIZE_CHECK="$bg_const_size_check" TEKO_BACKEND="$bg_backend" "$bg_bin" . -o "$W/out" --no-verify --release ); echo $? >"$W/bg_status"; } 2>&1 | tee "$bg_log" | sed 's/^/fixpoint:   | /' >&2
    fi
    set -e
    return "$(cat "$W/bg_status")"
}

# scan_project_c OUTFILE — the sorted list of every `.c` in the project tree, EXCLUDING `.git` and
# this gate's own work directory. Two of these, one before a build and one after, name exactly the
# `.c` files that build created, which is the attribution `scripts/no_emitted_c.sh` cannot do (it
# sweeps a worktree and cannot say whose emission it found).
#
# WHY THE PROJECT TREE AND NOT JUST `$W/out`: "gen2 and gen3 emit no C" is a claim about the
# generation, not about one directory. `-o` is where the compiler is ASKED to put its output; a
# check that looked only there would pass a generation that wrote its C beside the sources. The
# scan costs a `find` over a few thousand paths, twice per generation.
#
# PATHS ARE ABSOLUTE on purpose: the two scans and the `$W/out` sweep all feed the same failure
# report, and a reader chasing a named file should not have to work out which directory the name
# was relative to.
scan_project_c() {
    find "$PROJ" -name '*.c' -not -path "$PROJ/.git/*" -not -path "$W/*" \
        | LC_ALL=C sort > "$1"
}

# record_emission NAME — the EMISSION CHECK's evidence for one generation: the sorted list of
# every `.c` that generation wrote, at $W/NAME.emitted-c-list, and its count at $W/NAME.emitted-c.
#
# TWO SOURCES, ONE LIST, and both are needed to make the claim honestly. `$W/out` is where the
# generation was told to put its output; the before/after delta over the project tree catches a `.c`
# written anywhere ELSE. A check that watched only the output directory would pass a generation that
# emitted C beside the sources, which is not the claim the owner asked to be proven.
#
# IT NAMES THE FILES rather than counting them, because "emitted-c=1" sends a reader hunting and a
# path does not. Owner ruling 2026-07-28: the check must *"falhar alto e nomear o(s) ficheiro(s)"*.
#
# NOTHING IS EXCLUDED. The C route's own `teko.c` lands in this list and that is correct: on a leg
# still running `c` the check is not armed, and on a leg running `native` that file is precisely the
# defect. An allow-list here is how an armed check rots back into a rubber stamp.
record_emission() {
    re_name="$1"
    [ -f "$W/project-c.before" ] || {
        log "internal: no pre-build .c scan for $re_name — the emission check cannot attribute"
        exit 1
    }
    scan_project_c "$W/project-c.after"
    {
        find "$W/out" -name '*.c' 2>/dev/null || true
        comm -13 "$W/project-c.before" "$W/project-c.after"
    } | LC_ALL=C sort -u > "$W/$re_name.emitted-c-list"
    wc -l < "$W/$re_name.emitted-c-list" | tr -d ' \t' > "$W/$re_name.emitted-c"
}

# take_gen NAME — moves the just-built compiler out of $W/out to $W/NAME, PRESERVES the teko.c that
# build emitted (as $W/NAME.c), and records what it emitted. Frees $W/out so the NEXT generation can
# be built at the identical path.
#
# THE EMITTED C IS KEPT, NOT JUST COUNTED, and that is the point of this function beyond moving a
# binary. Owner ruling 2026-07-28: *"fechando tudo verde, coletou o teko.c emitido no vagão 20?
# Acredito que seja importante a coleta, enviar o último teko.c estável da escada."*
#
# WHAT gen2.c AND gen3.c ARE FOR, now that the harvest candidate is gen1's C (see `stage_gen1_c`):
# they are the EVIDENCE, not the payload. Two different binaries emitting byte-identical C for one
# source is the C emission's own fixpoint, one layer below the binary one, and it is only visible
# if both files survive the run. They exist at all only while gen2/gen3 still run down the C route;
# once the `TEKO_FIXPOINT_BACKEND` lever flips, `emitted-c` reads 0 and there is nothing to keep.
# `rm -rf "$W/out"` still runs; it just no longer takes the C with it.
take_gen() {
    tg_name="$1"
    record_emission "$tg_name"
    if [ -f "$W/out/teko.c" ]; then
        cp "$W/out/teko.c" "$W/$tg_name.c"
    fi
    if [ -x "$W/out/teko" ]; then
        mv "$W/out/teko" "$W/$tg_name"
    elif [ -x "$W/out/teko.exe" ]; then
        mv "$W/out/teko.exe" "$W/$tg_name"
    else
        return 1
    fi
    rm -rf "$W/out"
}

# stage_gen1_c — copies the C emitted ALONGSIDE gen1 into $W/gen1.c, so a passing run leaves the
# harvest candidate next to the generations that vouch for it.
#
# THE HARVEST POINT MOVED, AND THIS FUNCTION IS THE MOVE. It used to be `gen3.c`, on the reasoning
# that the freshest C came from the generation just proven byte-equal to its successor. Under the
# 0.3.1.0 chain gen2 and gen3 are built NATIVE and emit no C at all — there is no `gen3.c` to take.
# The last generation that emits C is gen1, and its emission is the one file that `cc` turns back
# into a working compiler.
#
# THE PROOF DID NOT WEAKEN, IT MOVED WITH IT: the C is not trusted because it came from the last
# generation, but because gen2 and gen3 DESCEND from the binary it encodes and proved the fixpoint.
# A run whose gen2 != gen3 harvests nothing — the callers gate the upload on this script's exit.
#
# IT ALSO RECORDS gen1's EMISSION IN THE EMISSION CHECK'S OWN FORMAT ($W/gen1.emitted-c{,-list}),
# though nothing reads it yet. Owner ruling 2026-07-28: *"a ideia é que gen1 aprenda a compilar
# native sem emitir C"*. The day that lands, adding `gen1` to `ZERO_C_GENERATIONS` is the whole
# change; the observation it needs is already being taken. gen1's emission cannot be measured by
# the before/after bracket `build_gen` uses — it happened before this script was invoked — so what
# is recorded is the file found BESIDE the gen1 binary, which is where the ladder leaves it.
stage_gen1_c() {
    sg_src="${TEKO_FIXPOINT_GEN1_C:-$(dirname "$GEN1")/teko.c}"
    if [ ! -f "$sg_src" ]; then
        : > "$W/gen1.emitted-c-list"
        printf '0' > "$W/gen1.emitted-c"
        log "gen1 emitted no C beside it ($sg_src is absent) — nothing to harvest from this run."
        log "  That is expected only once gen1 itself is built by the native route; while the"
        log "  chain's first links run with TEKO_BACKEND=c, a missing C means the ladder changed."
        return 0
    fi
    printf '%s\n' "$sg_src" > "$W/gen1.emitted-c-list"
    printf '1' > "$W/gen1.emitted-c"
    cp "$sg_src" "$W/gen1.c"
    log "gen1's emitted C staged: $W/gen1.c ($(wc -c < "$W/gen1.c") bytes, from $sg_src)"
    return 0
}

# ── observe the PROVENANCE of gen1 — it explains the run, it selects nothing ───────────────────
DG=0
degrau_scan "$PROJ" || DG=$?
if [ "$DG" = "2" ]; then
    log "the degrau declaration is broken — see the lines above. This gate refuses to guess which"
    log "chain produced its gen1: a gate that guesses its own input is not a gate."
    exit 1
fi
if [ -n "${TEKO_FIXPOINT_MODE:-}" ]; then
    PROVENANCE="$TEKO_FIXPOINT_MODE"
    log "provenance = $PROVENANCE (FORCED via TEKO_FIXPOINT_MODE — CI must not do this; it labels"
    log "             the run and chooses no operand)"
elif [ "$DG" = "0" ]; then
    PROVENANCE=degrau
    degrau_report
    log "provenance = degrau — gen1 descends from the DECLARED C, through cc"
else
    PROVENANCE=normal
    degrau_report
    degrau_note_undeclared_c "$PROJ"
    log "provenance = normal — release -> gen0 -> gen1, the 0.3.1.0 chain"
fi

log "gen1 = $GEN1"
log "source = $PROJ"
"$GEN1" --version >&2 2>&1 || true
stage_gen1_c

log "building gen2 = gen1(source) ..."
if ! build_gen "$GEN1" "$W/gen2.log"; then
    log "----- gen1's build of the source (streamed live above) did not complete -----"
    verdict_fail "gen1 does not build the source it came from — the compiler does not self-host, so gen2 does not exist and the fixpoint cannot hold. See the address above."
fi
take_gen gen2 || verdict_fail "the gen2 build reported success but left no binary at $W/out"
log "gen2 ready ($(wc -c < "$W/gen2") bytes)"

# LEFT and RIGHT are the two generations that share a code generator, and there is exactly one such
# pair — gen2 and gen3. They are named rather than inlined because every assertion below reads them,
# and because the pair being FIXED is itself a decision (owner ruling 2026-07-28: *"O veredito do
# fixpoint não muda: continua gen2 == gen3"*), not an accident of this file's shape.
log "building gen3 = gen2(source) ..."
if ! build_gen "$W/gen2" "$W/gen3.log"; then
    log "----- gen2's build of the source (streamed live above) did not complete -----"
    verdict_fail "gen2 built but does not rebuild the source — the chain breaks at the second generation. See the address above."
fi
take_gen gen3 || verdict_fail "the gen3 build reported success but left no binary at $W/out"
log "gen3 ready ($(wc -c < "$W/gen3") bytes)"
LEFT=gen2
RIGHT=gen3

# ── the two assertions, reported SEPARATELY so a partial arrival is legible ────────────────────
FIX_OK=1

if cmp -s "$W/$LEFT" "$W/$RIGHT"; then
    log "byte-identity: $LEFT == $RIGHT  ✓"
else
    FIX_OK=0
    log "byte-identity: $LEFT != $RIGHT  ✗"
    log "  $LEFT $(wc -c < "$W/$LEFT") bytes, $RIGHT $(wc -c < "$W/$RIGHT") bytes"
    cmp "$W/$LEFT" "$W/$RIGHT" >&2 2>&1 || true
fi

# ── the harvest, and why it is REPORTED here rather than left to the caller ────────────────────
#
# THE CANDIDATE IS gen1's C, and the change of address is the 0.3.1.0 chain's, not this file's.
# Owner ruling 2026-07-28: the `teko.c` *"deixa de ser ENTRADA e passa a ser SAÍDA: versionado
# depois do verde, colhido do gen1 (a última geração que emite C)"*. gen2 and gen3 are built
# native, emit nothing, and are therefore no longer harvestable at all — what they contribute is
# the PROOF, since both descend from the binary that C encodes.
#
# Naming the file in the log — with its size — is what lets a reader of a green run harvest it
# without re-deriving which file to take.
#
# THE AGREEMENT LINE SURVIVES for as long as gen2/gen3 still run down the C route (the lever in
# `build_gen` has not been pulled on this leg yet). `$LEFT.c == $RIGHT.c` means two different
# compiler BINARIES emitted byte-identical C for the same source — the C emission has reached its
# own fixpoint, one layer below the binary fixpoint. A disagreement there would be a real finding
# even with `$LEFT == $RIGHT` green, since the binaries would agree while their C does not. The day
# the lever flips, both files are absent and this block simply says nothing.
if [ -f "$W/$LEFT.c" ] && [ -f "$W/$RIGHT.c" ]; then
    if cmp -s "$W/$LEFT.c" "$W/$RIGHT.c"; then
        log "emitted-C identity: $LEFT.c == $RIGHT.c  ✓ ($(wc -c < "$W/$RIGHT.c") bytes)"
    else
        log "emitted-C identity: $LEFT.c != $RIGHT.c  — the binaries agree but their C does not"
        log "  $LEFT.c $(wc -c < "$W/$LEFT.c") bytes, $RIGHT.c $(wc -c < "$W/$RIGHT.c") bytes"
    fi
fi
if [ -f "$W/gen1.c" ]; then
    log "harvest: $W/gen1.c is this run's candidate for bootstrap/teko.c — the C of gen1, the last"
    log "         generation that emits any, vouched for by the gen2/gen3 that descend from it"
fi

# ── THE EMISSION CHECK, RE-ARMED — AND ARMED BY THE BACKEND, NOT BESIDE IT ────────────────────
#
# Owner ruling 2026-07-28: *"Só tem uma falha, gen2 e gen3 estão emitindo C e não deveriam,
# reabilite o check de emissão (somente nas pernas Linux), verá que não passará nada."*
#
# WHY IT WAS DISARMED, RECORDED RATHER THAN DELETED. Owner ruling 2026-07-27: *"pode remover por
# hora a validação de no-c para este caso"*, inside the release plan of the same night — both
# routes alive, then the native backend taught, then C removed — which the 0.3.1 lane RESEQUENCED
# BY PLATFORM. While EVERY leg built gen2/gen3 down the C route, a generation emitting C was the
# DESIGNED state, and enforcing zero-C would have red every run for doing exactly what the version
# asked of it.
#
# WHAT CHANGED, AND WHY THE SOFTENING IS OVER FOR LINUX: the four Linux legs now build gen2 and
# gen3 with `TEKO_FIXPOINT_BACKEND=native`. A NATIVE generation emitting C is not a designed state
# in any version — it is a defect. So the arming is DERIVED from the backend rather than configured
# next to it: `native` arms, `c` does not. There is no second knob to set, and therefore no way for
# a future leg to migrate to native while its emission check stays quietly off.
#
# `TEKO_FIXPOINT_ZERO_C` still overrides, in BOTH directions, and it is for one caller: the 0.3.1.4
# wagon that retires the C route arms it on the remaining `c` legs as it goes.
#
# IT IS EXPECTED TO BE RED ON LINUX TODAY, and that red is the measurement the owner asked for
# (*"verá que não passará nada"*). Nothing here may be narrowed to reach green — not the file set
# it sweeps, not the severity, not the legs it covers.
#
# WHAT STILL FAILS INDEPENDENTLY, and it is the half that carries the meaning: `gen2 == gen3` byte
# for byte — THIS COMPILER REBUILDS ITSELF. That claim is enforced above and was never relaxed.
#
# THE GENERATION SET IS A VARIABLE, NOT A HARDCODED PAIR, AND THAT IS DELIBERATE. Owner ruling
# 2026-07-28: *"como estamos trabalhando com foco em native, não devem existir novas emissões em C,
# a ideia é que gen1 aprenda a compilar native sem emitir C."* The destination has gen1 emitting no
# C either, at which point `bootstrap/teko.c` has no producer left and the DEGRAU path (the only
# path that consumes it) is the only place C ever appears. Extending this check to that world must
# be a one-token edit — `ZERO_C_GENERATIONS="gen1 $LEFT $RIGHT"` — and not a rewrite, so every
# piece below (`record_emission`, `report_emission`, the loop) takes a generation NAME.
#
# IT IS NOT EXTENDED TODAY, and that is not an oversight: owner ruling 2026-07-27, *"ele não pode
# nem deve avaliar a escada e nem a gen1, apenas gen2 e 3"*. gen1 emits C by construction on the
# current chain; judging it now would fail the gate for doing what this version asks. The hook is
# built, the trigger is the owner's.

# zero_c_enforced_default — 1 when the generations under test were built NATIVE, 0 when they were
# built down the C route. The derivation IS the scoping the owner asked for: only the legs that
# generate native are the legs whose emission is a defect, and `scripts/ci_producer_matrix.sh` says
# which those are.
zero_c_enforced_default() {
    [ "$FIXPOINT_BACKEND" = "native" ] && printf '1' || printf '0'
}

# emission_count NAME — how many `.c` files that generation wrote, or `?` if it was never recorded.
emission_count() {
    cat "$W/$1.emitted-c" 2>/dev/null || printf '?'
}

# report_emission NAME — prints the `.c` files that generation wrote, one per line, so a failure
# names its evidence instead of pointing at a count.
report_emission() {
    re_list="$W/$1.emitted-c-list"
    [ -s "$re_list" ] || return 0
    while IFS= read -r re_path; do
        log "        $1 emitted: $re_path"
    done < "$re_list"
}

# ZERO_C_GENERATIONS — the generations this check judges. See the block above for why gen1 is not
# in it yet and what adding it costs.
ZERO_C_GENERATIONS="$LEFT $RIGHT"
ZERO_C_ENFORCED="${TEKO_FIXPOINT_ZERO_C:-$(zero_c_enforced_default)}"

ZERO_C_OK=1
ZERO_C_TALLY=""
for zc_gen in $ZERO_C_GENERATIONS; do
    zc_n="$(emission_count "$zc_gen")"
    ZERO_C_TALLY="$ZERO_C_TALLY $zc_gen=$zc_n"
    [ "$zc_n" = "0" ] || ZERO_C_OK=0
done
ZERO_C_TALLY="${ZERO_C_TALLY# }"

# zero_c_reason — WHY the check bit, which is not the same sentence in both cases. Derived from the
# backend it is a native generation breaking a rule of its own route; forced by
# TEKO_FIXPOINT_ZERO_C it is the C route being retired on a leg that has not migrated yet. A
# failure that names the wrong reason sends the reader to the wrong file.
zero_c_reason() {
    if [ "$FIXPOINT_BACKEND" = "native" ]; then
        printf '%s' "these generations were built with TEKO_BACKEND=native, and a native generation must emit no C at all"
        return 0
    fi
    printf '%s' "these generations were built with TEKO_BACKEND=$FIXPOINT_BACKEND and zero-C was armed by hand (TEKO_FIXPOINT_ZERO_C=1)"
}

if [ "$ZERO_C_OK" = "1" ]; then
    log "zero-C: no generation under test emitted any .c  ✓ ($ZERO_C_TALLY)"
elif [ "$ZERO_C_ENFORCED" = "1" ]; then
    log "zero-C: $ZERO_C_TALLY  ✗ (ENFORCED — $(zero_c_reason))"
    for zc_gen in $ZERO_C_GENERATIONS; do report_emission "$zc_gen"; done
    FIX_OK=0
else
    log "zero-C: $ZERO_C_TALLY  — REPORTED, not enforced (this leg still builds these"
    log "        generations down the C route; it migrates by 0.3.1.4)."
    for zc_gen in $ZERO_C_GENERATIONS; do report_emission "$zc_gen"; done
fi

[ "$FIX_OK" = "1" ] || verdict_fail "the fixpoint has not arrived — see the two lines above for which half is outstanding"

if [ "$ZERO_C_OK" = "1" ]; then
    log "VERDICT: PASSED — $LEFT == $RIGHT byte for byte, and neither emitted C"
else
    log "VERDICT: PASSED — $LEFT == $RIGHT byte for byte. (They still emit C on the C route: the"
    log "         0.3.1.4 goal, reported above.)"
fi
# CLEAN THE SCRATCH, NEVER THE HARVEST. This used to be a flat `rm -rf "$W"`, which deleted the
# emitted C two lines after the script had just printed
#
#     fixpoint: harvest: …/.fixpoint/gen1.c is this run's candidate for bootstrap/teko.c
#
# — it named the one file worth keeping and then destroyed it, so every downstream step saw an empty
# directory and `upload-artifact` reported "No files were found" over a fixpoint that had PASSED.
#
# What is scratch and what is product: the generation BINARIES (~2.5 MB each), the build LOGS and the
# emitted-c flags exist only to reach the verdict above and are worthless once it is printed. The
# `.c` files are the product — `gen1.c` is the next `bootstrap/teko.c`. So the sweep is enumerated
# rather than blanket, and `*.c` is simply not in it.
#
# THE GENERATION BINARIES SURVIVE TOO, and that is a second reversal. They were scratch while the
# only question was the verdict; they became the INSTRUMENT the moment the question turned into
# "does a DIFFERENT generation behave differently?". Owner ruling 2026-07-28: *"o que é a compilação
# do que um binário executando em runtime a identidade de um código? Entende o paradoxo? Se o erro é
# em runtime, ele pode não morar onde estamos olhando."*
#
# The compiler is a binary executing, so a stop while compiling some project is that binary's OWN
# run time — and gen1's checker was lowered by an older compiler than the one whose source anyone
# reads. Running the same probe under gen1 and under gen2 is what tells a defect in the SOURCE apart
# from a defect in the LINEAGE, and it cannot be done if the gate deletes the generations.
#
# ~2.5 MB apiece, on a runner that is discarded minutes later. The logs, the emitted-c marks and the
# emission check's scans stay in the sweep: those really are spent once the verdict is printed — and
# on a run that FAILS the check they are never reached, so the evidence survives for the reader.
#
# The enumerated glob is why `*.emitted-c-list` and `project-c.*` had to be named here explicitly.
# A `rm -f "$W"/*` would have been shorter and would have taken `gen1.c` with it, which is the exact
# defect this block exists to record.
rm -rf "$W/out"
rm -f "$W"/*.log "$W"/*.emitted-c "$W"/*.emitted-c-list "$W"/project-c.before "$W"/project-c.after
exit 0
