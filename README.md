# teko

A C#-like language taught to [`mc`](https://github.com/minicompiler/mc) — classes,
interfaces, traits, generics, delegates, compile-time dependency injection and
scope-based reference counting — as hook modules, on five native `(os, arch)` pairs.
`mc` compiles itself and is small enough to read; teko extends it without touching its
`src/`, the way `mc`'s own `examples/lang` teaches it a class system.

**Documentation: [`docs/README.md`](docs/README.md).** Guides, reference, specs and
internals all start there.

## Building

Needs the `mc` toolchain on `PATH`, at the version `MC_VERSION` names (`cat MC_VERSION`).
From the repository root:

```sh
mc build . --config teko.toml
build/teko-hello   # exits 42
```

`teko.toml` is the build config (`mc build . --config teko.toml`); `mc.toml` is the
package manifest (`[package]` only, read by `mc pkg hash .`). See the
[guide](docs/guide/README.md) for the full walkthrough.

## Fixtures and self-hosting

Every `tests/*.tk` carries a `// expect-exit: N` oracle; `sh scripts/bootstrap.sh` proves
the fixed point of the taught compiler (`teko0 → teko1 → teko2 → teko3`). Both run in CI
(`.github/workflows/ngen.yml`) on five native legs.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

## License

Dual-licensed under [Apache-2.0](LICENSE-APACHE) OR [MIT](LICENSE-MIT).
