// user.mc -- the PROJECT's own file, not the `teko` package's (S3, D64.7;
// docs/reference/packages.md §3: "a package never defines `user_init`; it
// exports `<name>_init()` and the project's own module calls it").
// `ngen/mc.toml`'s `[compiler].modules` lists this LAST, after `teko.tk`,
// so `teko_init()` is already declared when `user_init` calls it.
void user_init() {
    teko_init();
}
