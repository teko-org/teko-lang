---
seq: 0119
crumb-id: STD-FFI
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [RT-L3, S16-SWEEP]
sources:
  - "docs/design/plano-stdlib-catalogo-expansao.md:227-236"        # KEYSTONE-LINK on-demand FFI + self-.tkh
  - "docs/design/plano-stdlib-catalogo-expansao.md:331-576"       # areas C net, F databases, G protocols/RPC
  - "docs/design/mudancas-superficie-0.3.1.md:1283-1298"          # owner: FFI-stdlib is the Doc-2 terminus (no ui)
---

# 0119 · STD-FFI — FFI-dependent stdlib (crypto::rand · openssl · gpg · net · db · odbc · rpc)

> Anchor the FFI-gated stdlib into the plan. The owner's Doc-2 terminus (Doc-2:1283-1298) is
> FFI-stdlib — `crypto::rand`/`openssl`/`gpg`, `net`, `db`/`odbc`, `rpc` — over the §16 FFI-do-SO
> ABI. `ui` was REMOVED from scope (Doc-2:1294). None of these are in the 112-crumb plan.

## Goal

These stdlib areas need dynamic-FFI to system libraries (`dlopen`/`dlsym` + the native-OS ABI that §16
delivers) — the KEYSTONE-LINK seam (catalog §2.4). They are the LAST Doc-2 land before Doc-1 (Doc-2:
1283). This crumb is a CLUSTER ANCHOR: each area's per-module recipe lives in
`plano-stdlib-catalogo-expansao.md` (C net, F databases, G protocols/RPC); this crumb fixes their
place AFTER the C-runtime death (`S16-SWEEP` `0096`) + the FFI host layer (`RT-L3` `0061`), and their
`exp` posture. Pure leaves once the FFI seam exists → `[dry]`, no reseed.

## Where

- `src/crypto/rand.tks` — CSPRNG over `getrandom`/`BCryptGenRandom` (already partly via `S16-FS`
  getrandom); `openssl`/`gpg` bindings via dynamic-FFI.
- `src/net/` — TCP/UDP/TLS (OSI 4-7, catalog area C) over the OS socket FFI.
- `src/db/` — `db`/`odbc` client bindings (catalog area F) via dynamic-FFI.
- `src/rpc/` — SOCKS/RPC/gRPC/AMQP (catalog area G).
- All `exp`; internal helpers `pub`. No compiler machinery.

## How

1. **Gate on the FFI seam.** These require `S16-SWEEP` (`0096`, the C runtime is gone and the native
   OS-ABI FFI is the linkage plane) and `RT-L3` (`0061`, host fs/env leaf syscalls) landed. Dynamic
   linking is `extern fn … from lib "…"` per-OS (catalog §2.4 KEYSTONE-LINK), §17-selected.
2. **Author per area, leaf-by-leaf.** Each module binds the system library through the native FFI; no
   emitted C. `crypto::rand` first (smallest, feeds the rest); then `net` → `db` → `rpc`.

```teko
/**
 * fill — fill a byte slice with cryptographically secure random bytes from the OS CSPRNG
 * (`getrandom` on Linux, `getentropy` on BSD/macOS, `BCryptGenRandom` on Windows), selected by `#os`.
 *
 * @param dst  the slice to fill in place
 * @return     unit on success, or an error if the OS entropy source fails
 * @throws     when the OS CSPRNG syscall returns an error
 * @since 0.3.1
 */
exp fn fill(dst: []byte): null | error
```

## Rulings & laws

- **Teko-only + native-FFI:** `.tks` leaves over dynamic-FFI; NO emitted `.c` (Doc-1 §0.2 — the only C
  residue admitted is dynamic-link FFI to system libs, never a local `.c` needing a C compiler).
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Visibility (owner):** default `exp`; internal helpers `pub`.
- **Fork protocol:** `ui` REMOVED (Doc-2:1294, owner 2026-08-16) — do NOT add it; no undecided fork.
- **W15 full Javadoc** on every `exp` decl; flatten; no `//`.
- **Safety:** NEVER `teko test .`; `[dry]`; validate a compiled binary behaviourally.
- Rests on: catalog areas C/F/G + KEYSTONE-LINK §2.4 + Doc-2:1283-1298.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `ffi_rand_fill` | `crypto::rand::fill` returns distinct non-zero bytes | `0` |
| `ffi_net_tcp_echo` | a TCP client round-trips a byte against a loopback echo (probe-gated) | `0` |

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (leaves; no compiler-byte change). "Green" = each
area compiles against the native FFI seam and its behavioural fixtures pass. Reseed-class: `none`.

## Deps

`RT-L3` (`0061`) + `S16-SWEEP` (`0096`) — the FFI host layer + the C-runtime death must land first.

## Done when

`crypto::rand`/`openssl`/`gpg`, `net`, `db`/`odbc`, `rpc` exist as `exp` leaves over native dynamic-FFI
(no emitted C), behavioural fixtures pass, no reseed — the Doc-2 terminus before Doc-1.
