#!/usr/bin/env sh
# scripts/ci_producer_matrix.sh — THE PRODUCER LEG TABLE, in one place.
#
# Prints the `strategy.matrix` JSON for the artifact-producing layer. Two callers, one table:
#   pr.yml     `plan` → the `artifact` root's matrix (light or full)
#   nightly.yml `plan` → always full; a nightly that minted fewer than seven assets would leave the
#                        release unable to promote a complete set
#
# WHY THE TABLE IS HERE AND NOT INLINE IN EACH WORKFLOW: scripts/produce_assets.sh already made
# the STEPS single-sourced; the leg table is the other half of the same definition. A producer
# added to one workflow and forgotten in the other is a nightly that publishes six assets, or a
# PR that gates six and ships seven — and both failures surface at the release, which is the
# worst possible place to discover them.
#
# ── ONE LEG PER PUBLISHED LABEL: THE FOUR LINUX SUBLANES ──────────────────────────────────────
# Owner order, 2026-07-27: *"espero ver 4 sublanes de execução, não duas, não três, não uma, para
# Linux"*. Each Linux label is its OWN leg, and each leg walks the WHOLE path by itself — seed,
# ladder, dry build, its one asset. The ladder work IS duplicated across the four, deliberately,
# and the owner ruled on exactly that: *"Tem que duplicar, pois depois do teko.c, a escada não
# depende mais dele, e se fosse para não duplicar, estaríamos fazendo o primeiro degrau antes de
# todas as sublanes."*
#
# WHICH CLOCK THIS OPTIMISES, because the two point opposite ways here. The owner is explicit:
# *"Não estou falando de relógio de máquina (cicle time), mas sim relógio de processo (o maior
# tempo de todos os tempos)"* — DELIVERY time, the longest single path end to end.
#
#   shape                       machine cost   delivery (slowest leg)   observability
#   one job per arch (before)          1.0x     726s  x86_64            two labels share one log
#   ladder job → 4 asset jobs          1.0x    ~690s+ x86_64            SPLIT ACROSS TWO JOBS
#   one job per label (this)          ~1.8x     667s  x86_64            whole path, one log
#
# The middle shape — hoisting the shared ladder into its own job the four legs consume — was
# BUILT AND THEN REJECTED, and the reason is a BARRIER: the four sublanes cannot start until the
# ladder finishes, so delivery becomes `ladder + upload + queue + download + asset`, which is
# WORSE than a duplicated leg that starts at t=0. It also destroys the thing the owner actually
# asked for: *"A questão é a percepção, delivery time, que não tem hoje ou pior, não é auditável /
# observável."* With a hand-off, no single job's duration is the time it took to deliver an asset;
# with one leg per label, every leg's own wall clock IS that number, readable off the run page.
#
# AND `teko.c` IS NOT WORTH AN ARCHITECTURE. Sharing it was the middle shape's whole justification,
# and the owner disposed of it twice over: *"se é para garantir teko.c, tem que ser feito para
# todas as sublanes"* (a guarantee across four legs is a COMPARISON, not a shared file — that is
# `cross-arch-determinism`'s job and it already does it), and *"eu não me importo com teko.c, pois
# será emitido apenas no primeiro degrau"* (it is scaffolding for gen1 and dies when the native
# backend emits objects directly). Building a job graph around a file scheduled to disappear buys
# permanence for the wrong thing.
#
# ── THE FIELDS ────────────────────────────────────────────────────────────────────────────────
#   producer  names the leg and its uploaded artifact (`teko-assets-<producer>`). For a Linux leg
#             this IS the label, because the leg mints exactly one.
#   os        the runner
#   timeout   job budget, minutes
#   kind      `linux` (the asset is built by scripts/native_linux_asset.sh from the emitted
#             teko.c, with the runner's OWN toolchain and NO CONTAINER) or `native` (the runner
#             IS the target; the dry build's own binary is the asset)
#   seed      the released asset label whose binary RUNS on this runner — NOT always what the leg
#             produces, so it is a field rather than a derivation the caller can get wrong
#   produces  the CONTRACT: the asset labels this leg must mint
#   fixpoint_backend
#             the backend gen2 and gen3 are built with in `scripts/fixpoint_gate.sh` — `native`
#             or `c`. THE PLATFORM-SEQUENCING LEVER, and it lives here because the leg table is
#             where a platform's stage is declared. See the block below.
#
# ── THE LEVER: WHICH LEGS GENERATE NATIVE (owner ruling 2026-07-28) ───────────────────────────
# Owner, 2026-07-28: *"Só tem uma falha, gen2 e gen3 estão emitindo C e não deveriam, reabilite o
# check de emissão (somente nas pernas Linux), verá que não passará nada."*
#
# REFINADO pelo dono em 2026-07-29: *"pode pegar uma perna musl do x86 e uma glibc do arm64 e
# colocar em c, assim tem duas pernas para cada lado em libc diferentes"*. Passam a ser DUAS as
# pernas nativas, não quatro, escolhidas para que cada backend cubra os dois arcos E as duas libc:
#
#   | perna                | backend  |
#   |----------------------|----------|
#   | linux-x86_64-glibc   | native   |
#   | linux-arm64-musl     | native   |
#   | linux-x86_64-musl    | c        |
#   | linux-arm64-glibc    | c        |
#
# A régua continua a medir (x86 + arm, glibc + musl) e ganham-se duas pernas verdes que provam o
# resto do oleoduto — produção de asset, testes, determinismo — que o vermelho total escondia.
# macOS e Windows carregam `"c"`, porque o plano sequenciado por plataforma
# (docs/memory/0.3.1-plano-sequenciado-por-plataforma.md) os migra em 0.3.1.1–0.3.1.4 e retira a
# rota C depois disso.
#
# THE RED IS THE PRODUCT, NOT AN ACCIDENT. The native backend does not build the compiler yet —
# `docs/memory/0.3.1.0-linux-native-first-stop.md` names the stop it reaches today, by address —
# so the TWO native Linux legs are EXPECTED to fail, and that failure is the honest measurement of
# how far the native self-build gets. Reduzir de quatro para duas NÃO abranda a régua: as duas que
# ficam cobrem os dois arcos e as duas libc, logo qualquer divergência entre arco ou entre libc
# continua a aparecer. The owner asked for it in as many words: *"verá que não passará
# nada."* It is not to be softened with `continue-on-error`, a narrowed criterion, or files
# excluded from the emission check; the address it reports IS the deliverable.
#
# ── A ESCADA DE VALIDAÇÃO DA ALAVANCA (ruling do dono, 2026-07-29) ────────────────────────────
# O 2×2 de hoje é o PRIMEIRO de quatro estados, não o estado final. Literal do dono:
#
#   *"Assim, quando ambas as 4 pernas percorrerem verdes todo o caminho, inverte o fixpoint, para
#   garantir, e depois as 4 em c e por último as 4 em native (que fecha o arco e estabiliza tudo)."*
#
#   | # | estado                                   | o que prova                                    |
#   |---|------------------------------------------|------------------------------------------------|
#   | 1 | 2 native + 2 c (HOJE)                    | a régua mede, e o oleoduto a jusante volta a correr |
#   | 2 | INVERTER o par                           | o self-host nativo não é específico das pernas escolhidas |
#   | 3 | as 4 em `c`                              | a rota C não regrediu em lado nenhum com o trabalho nativo |
#   | 4 | as 4 em `native`                         | fecha o arco e estabiliza                      |
#
# O estado 1 avança para o 2 **só quando as quatro pernas percorrerem verdes TODO o caminho** — o
# que hoje não acontece, porque as duas nativas param no degrau da vez
# (`docs/memory/0.3.1.0-linux-native-first-stop.md` nomeia-o por endereço).
#
# O estado 2 é o passo que a intuição saltaria, e é o que apanha o erro mais caro: se o self-host
# nativo funcionasse por acidente de arco ou de libc, o 2×2 de hoje NÃO o revelaria — só inverter
# revela. Não o saltes para chegar mais depressa ao 4.
#
# A inversão do estado 2 é: `linux-x86_64-musl` e `linux-arm64-glibc` passam a `"native"`;
# `linux-x86_64-glibc` e `linux-arm64-musl` passam a `"c"`.
#
# TO TURN IT BACK: set the leg's `fixpoint_backend` to `"c"`. Nothing else moves — the emission
# check is DERIVED from this field inside `fixpoint_gate.sh` (a native generation that emits C is
# a defect on any leg), so one word per leg is the whole switch, in both directions.
#
# WHY A FIELD AND NOT AN `if:` ON THE STEP: `endsWith(matrix.producer, '-musl')`-style predicates
# already exist in pr.yml for the toolchain install, and every one of them is a place a seventh leg
# gets forgotten. This table is consumed by pr.yml AND nightly.yml from ONE definition; a stage
# declared as a field cannot be set for a leg in one workflow and missed in the other.
#
# LEG ORDER IS SLOWEST-FIRST. GitHub starts matrix legs in declaration order, so the slowest
# producer must not queue behind cheap ones — and with one leg per label that ordering matters
# more, not less, since there are now six legs contending for runners instead of four.
#
# Usage:  ci_producer_matrix.sh <light|full>
#
# POSIX sh only.
set -eu

MODE="${1:?usage: ci_producer_matrix.sh <light|full>}"

# The arm64 legs lead: their runner pool is the scarcer one, so they must not queue behind x86_64.
A_AG='{"producer":"linux-arm64-glibc","os":"ubuntu-24.04-arm","timeout":90,"kind":"linux","seed":"linux-arm64-glibc","produces":"linux-arm64-glibc","fixpoint_backend":"c"}'
A_AM='{"producer":"linux-arm64-musl","os":"ubuntu-24.04-arm","timeout":90,"kind":"linux","seed":"linux-arm64-glibc","produces":"linux-arm64-musl","fixpoint_backend":"native"}'
A_XM='{"producer":"linux-x86_64-musl","os":"ubuntu-latest","timeout":90,"kind":"linux","seed":"linux-x86_64-glibc","produces":"linux-x86_64-musl","fixpoint_backend":"c"}'
A_WX='{"producer":"windows-x86_64","os":"windows-latest","timeout":90,"kind":"native","seed":"windows-x86_64","produces":"windows-x86_64","fixpoint_backend":"native"}'
A_MAC='{"producer":"macos-arm64","os":"macos-latest","timeout":60,"kind":"native","seed":"macos-arm64","produces":"macos-arm64","fixpoint_backend":"native"}'

# linux-x86_64-glibc is declared in both tiers and is the ONLY Linux leg on light: it is the asset
# every fixed-name consumer downloads (the fail-closed regressor, mem-paranoid), so a light run without
# it would leave those lanes with nothing to fetch.
A_XG='{"producer":"linux-x86_64-glibc","os":"ubuntu-latest","timeout":90,"kind":"linux","seed":"linux-x86_64-glibc","produces":"linux-x86_64-glibc","fixpoint_backend":"native"}'

case "$MODE" in
    full)
        printf '{"include":[%s,%s,%s,%s,%s,%s]}\n' "$A_AG" "$A_AM" "$A_XG" "$A_XM" "$A_WX" "$A_MAC"
        ;;
    light)
        # The light tier mints ONE Linux asset instead of four — the label filter is on the LEG
        # SET, so the tier pays for one Linux build rather than building four and discarding
        # three. windows-x86_64 rides the light tier for the empirical reason in pr.yml's header:
        # this train's wagon-unique failures happened on Windows and only on Windows.
        printf '{"include":[%s,%s]}\n' "$A_XG" "$A_WX"
        ;;
    *)
        echo "ci_producer_matrix: mode must be 'light' or 'full', not '$MODE'" >&2
        exit 1
        ;;
esac
