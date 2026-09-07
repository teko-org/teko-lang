// core_teko.mc -- the taught compiler's own main(), naming the parts of
// D64.1 (the delivery-4 plan §64) instead of the whole
// `<mc/core>` bundle: `<mc/core_min>` comes from `[compiler].core` in
// `teko.toml` (this file assumes it is already included), and this file adds
// the four parts teko actually uses --
//
//   <mc/core_machines>  arm64 and x86-64, both machines every CI leg needs
//   <mc/core_writers>   macho/backend_exe, backend_elf/backend_elf_exe,
//                       backend_coff -- every writer a leg links with
//   <mc/core_build>     `mc build --entry-only`, what the CI (and this
//                       project's own `teko.toml`) compiles each fixture with
//   <mc/core_bundle>    `#include <name>`, which `lib/rt.tk` needs for
//                       `<sys>`
//
// `<mc/core_pkg>` and `<mc/core_sandbox>` are left out on purpose: nothing
// in this repository calls `mc pkg`, `mc update` or `mc sandbox`, and no fixture
// exercises them either. `main()` below is `src/main.mc`'s own list with
// those two omitted and `mc_pkg_init()`/`mc_sandbox_init()` gone with them.
//
// S2 (D64.3): `mc_build_init()` (src/core_build.mc) is `lex_set_libs` +
// `sysroots_init` + three `subcommand()` registrations + `on_plan(&mc_plan)`.
// Calling it here would register the mc's OWN "build"/"limits"/"sysroot"
// entries alongside teko's -- `subcommand_find` is last-wins, so teko's own
// registrations below would still be the ones that DISPATCH, but
// `subcommand_usage()` prints every registration in table order regardless
// of which one wins, so `teko` with no argument would show both. Calling
// the four public pieces `mc_build_init()` is made of directly, and never
// that function itself, keeps the table exactly teko's own two entries.
// `sysroot` is left out: only the Windows CI leg needs a sysroot, and that
// leg builds it with the release `mc`, never with `teko`
// (`.github/workflows/ngen.yml`'s own "build the Windows sysroot" step).
//
// A clean subcommand table is still not the whole story: `mc` with no
// argument at all falls through `mc_main` into `cli.mc`'s own `usage()`,
// which prints three FIXED lines of its own (`usage: mc [--dump-tokens|...]
// source.mc [-o out]`, `mc --host`, `mc --version`) ahead of
// `subcommand_usage()` -- lines this file must not edit (`src/` there is
// frozen). `argc < 2` is the one case that reaches that path with nothing
// useful to show (every other case either dispatches a subcommand or names
// a source file), so main() intercepts exactly that case and prints teko's
// own table with the same `subcommand_usage()` call the core would have
// made anyway -- never mc_main's `usage()`, never its three lines.
#include <mc/core_machines>
#include <mc/core_writers>
#include <mc/core_build>
#include <mc/core_bundle>

i64 main(i64 argc, uptr argv, uptr envp) {
    host_init(envp);
    mc_machines_init();
    mc_writers_init();
    mc_bundle_init();
    lex_set_libs(&libs_open, 0);
    sysroots_init();
    on_plan(&mc_plan);
    subcommand("build", &tk_build,
        "usage: teko build [DIR] [--config FILE] [--entry-only] [--compiler-only] [--limits|--fix-limits] [--sysroot-dir DIR] [--libs-dir DIR]\n");
    subcommand("limits", &tk_limits,
        "       teko limits [DIR|FILE.tk]\n");
    if (argc < 2) { subcommand_usage(); return 1; }
    return mc_main(argc, argv, envp);
}
