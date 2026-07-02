# TEKO — ROADMAP: `teko::math::*`

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors` (off `chore/reboot`)
>
> A specialized mathematics surface under `teko::math::*`: elementary + advanced **functions** and
> numeric **types** that (a) are called by **name, not operators**, and (b) **return `T | error` instead
> of panicking** on a domain/edge failure. Implemented 100% in Teko; the pure units are VM-`.tkt`-testable,
> the libm-backed ones go through the existing `extern` FFI (C7.1) with `#os` guards.
>
> Companion to [`TEKO_ROADMAP_NET_CRYPTO.md`](TEKO_ROADMAP_NET_CRYPTO.md). This doc **resolves two of that
> doc's open decisions**: the crypto wrapping-arithmetic story (→ `teko::math::checked`, M1) and the
> big-integer module placement (→ `teko::math::bigint`, M3, a general numeric module, not crypto-private).
> Same work-distribution contract: each ▪ unit is one agent task with deps, files, and a verify bar.

---

## 0. The core distinction (the whole reason this namespace exists)

Teko's built-in **operators are the fast path and they PANIC** on an edge (M.1 fail-loud): `a / 0` and
`a % 0` call `tk_panic_div0`; `+`/`-`/`*` panic on overflow under `TEKO_OVERFLOW_DEBUG`. That is correct
for hot arithmetic where a bug should abort.

`teko::math` is the **recoverable, explicit surface** for when a failure is *data*, not a bug:

- **No operators — named functions/methods.** `math::checked::div(a, b)`, `x.add(y)` on a `BigInt`,
  `math::real::sqrt(v)`. (Teko has no user operator overloading anyway, so specialized types like
  `BigInt`/`Complex`/`Matrix` are *necessarily* method-based — this namespace embraces that.)
- **Errors are values.** Domain/edge failures return `T | error` (or `error?`), never panic:
  division by zero, integer overflow, `sqrt` of a negative, `log` of a non-positive, `acos` out of
  `[-1,1]`, inverting a singular matrix, stats of an empty set, parsing a bad literal.
- **Three explicit integer-arithmetic modes** (M1), so the caller picks the semantics instead of hoping:
  `checked_*` → `T | error` on overflow; `wrapping_*` → modular wrap; `saturating_*` → clamp to the
  type's min/max. This is the clean answer to "how does crypto get wrap-around math without relying on
  release-mode UB."

**Ground rules:** pure Teko where the algorithm is bounded (VM-`.tkt`-tested against reference vectors);
elementary transcendentals may bind **libm** (`sqrt`/`pow`/`sin`/… `from "libc"`/`"m"`, `#os`-guarded)
for correctness+speed, with the *domain guard* (the error-returning wrapper) written in pure Teko so it
is VM-testable; a pure-Teko polynomial-approximation fallback is an optional later unit for freestanding
targets. Every unit honors the SUPREME RULE and the verify-both gate.

---

## 1. Units

### Foundations

**▪ M0 — `teko::math` root (common).** **Deps:** none. **Files:** `src/math/math.tks` (+`.tkt`).
Constants (`pi`, `tau`, `e`, `sqrt2`, `ln2`, …; `inf`, `nan` producers); float **classification**
(`is_nan`/`is_inf`/`is_finite`/`is_normal`); `abs`/`sign`/`copysign`; `min`/`max`/`clamp` (generic);
integer helpers `gcd`/`lcm`/`isqrt`/`ipow` (checked). Pure Teko. **Verify:** `.tkt`.

**▪ M1 — `teko::math::checked` (integer arithmetic modes) — resolves NET/CRYPTO open-decision #4.**
**Deps:** M0. **Files:** `src/math/checked.tks`.
For every integer width: `checked_add/sub/mul/div/rem/neg/pow/shl/shr` → `<int> | error` (error on
overflow or zero divisor — NO panic); `wrapping_*` → modular result; `saturating_*` → clamp. Generic
over the int types (monomorphization) where possible, else a per-width family. Pure Teko (selects the
non-panicking code paths; independent of `TEKO_OVERFLOW_DEBUG`). **Verify:** `.tkt` — boundary vectors
(MAX+1 wraps/errors/saturates; div0 → error), both engines. **Unblocks constant-time crypto math.**

### Exact / arbitrary-precision types (method API, error-returning)

**▪ M3 — `teko::math::bigint` (arbitrary-precision signed integer) — resolves NET/CRYPTO open-decision #8.**
**Deps:** M0, M1. **Files:** `src/math/bigint/*.tks`. A `BigInt` value type; **methods, no operators**:
`add/sub/mul`, `divmod → (BigInt, BigInt) | error` (error on zero divisor), `pow`, **`modpow`** (crypto),
`gcd`, `mod_inverse → BigInt | error`, `shl/shr`, `compare/eq`, `bit_len`, `parse(str, radix) → BigInt |
error`, `to_str(radix)`, `from_bytes/to_bytes` (big/little-endian, for crypto). **A constant-time flavor**
(fixed-width limbs, no early-out) is required by `teko::crypto::pk` — flag it in the API. Pure Teko; the
single largest math unit. **Verify:** `.tkt` against reference vectors (incl. modpow/mod_inverse used by
RSA/ECC). **Unblocks crypto C7 (pk).**

**▪ M4 — `teko::math::decimal` (arbitrary/fixed-precision base-10).** **Deps:** M3. **Files:**
`src/math/decimal.tks`. Exact decimal (no binary-float error) for money/finance: `add/sub/mul`,
`div(scale, rounding) → Decimal | error` (error on zero divisor), rounding modes (half-even, …),
`parse/to_str`. Method API. **Verify:** `.tkt` (exactness + rounding vectors).

**▪ M5 — `teko::math::rational` (fractions over BigInt).** **Deps:** M3. `Rational {num, den}` normalized;
`new(num, den) → Rational | error` (error on zero denominator); `add/sub/mul/div`; `to_f64`. Method API.
**Verify:** `.tkt`.

### Real / complex analysis

**▪ M2 — `teko::math::real` (elementary real functions).** **Deps:** M0. **Files:** `src/math/real.tks`.
`sqrt`/`cbrt`/`hypot`/`fma`; `pow`/`exp`/`exp2`/`expm1`; `log`/`ln`/`log2`/`log10`/`log1p`; trig
`sin`/`cos`/`tan` + inverses + `atan2`; hyperbolic; `floor`/`ceil`/`round`/`trunc`/`rint`. **Domain-guarded
variants return `f64 | error`** (`sqrt(<0)`, `log(<=0)`, `pow` NaN cases, `asin`/`acos` outside `[-1,1]`);
a raw variant returning NaN (IEEE) is also offered for hot paths. **Decision to ratify:** libm-backed via
`extern` (rec — correct + fast, `#os` `from "m"`/libc) with the pure-Teko domain guard on top, vs a fully
pure-Teko approximation set (optional later, freestanding). **Verify:** native against known values +
`.tkt` for the pure domain-guard logic.

**▪ M6 — `teko::math::complex`.** **Deps:** M2. `Complex {re, im}`; `add/sub/mul/div` (div → `Complex |
error` on zero), `abs`/`arg`/`conj`/`exp`/`ln`/`pow`/`sqrt`. Method API. **Verify:** `.tkt`.

### Applied

**▪ M7 — `teko::math::linalg` (vectors + matrices).** **Deps:** M2. `Vec2/3/4` + a dynamic `Matrix`;
`dot`/`cross`/`norm`/`normalize`, `add`/`scale`/`matmul`/`transpose`, `determinant`, `inverse → Matrix |
error` (error if singular), `solve(A, b) → Vec | error`. Specialized types, method API. **Verify:** `.tkt`
(identities + singular-matrix → error).

**▪ M8 — `teko::math::stats`.** **Deps:** M0. Over `[]f64`/`[]i64`: `mean`/`median`/`mode`/`variance`/
`stddev`/`percentile`/`quantile`/`min`/`max`/`sum`; each returns `f64 | error` (error on **empty input**).
Pure Teko. **Verify:** `.tkt`.

**▪ M9 — `teko::math::numtheory`.** **Deps:** M3. `is_prime` (Miller–Rabin, probabilistic + deterministic
for small n), `next_prime`, `factorize`, `mod_inverse`, `crt`, `factorial`/`binomial` (checked → error on
overflow for machine ints; BigInt otherwise), `totient`. Shared with crypto key-gen. **Verify:** `.tkt`
vectors. **Assists crypto C7.**

**▪ M10 — `teko::math::random` (NON-crypto PRNG).** **Deps:** M0. A **seedable, deterministic** generator
(xoshiro256\*\* / PCG) for simulation, sampling, tests: `uniform_int(lo, hi)`, `uniform_f64`,
`range → error` on empty range, `shuffle`, `sample`, distributions (`normal`/`exponential`/`poisson`).
**Explicitly distinct from `teko::crypto::rand`** (CSPRNG): this one is reproducible and MUST NOT be used
for keys/nonces (documented at the top of the module; cross-link both ways). Pure Teko. **Verify:** `.tkt`
(fixed seed → fixed sequence).

**▪ M11 — `teko::math::special` (T3, optional).** `gamma`/`lgamma`/`erf`/`erfc`/`beta` for statistics;
libm-backed where available. Later tier.

---

## 2. Dependency graph + tiers

```
M0 ─┬─ M1(checked)                 [resolves crypto wrap-arith]
    ├─ M2(real) ─┬─ M6(complex)
    │            └─ M7(linalg)     └─ M11(special, T3)
    ├─ M8(stats)
    └─ M10(random, non-crypto)
M0,M1 ─ M3(bigint) ─┬─ M4(decimal)
                    ├─ M5(rational)
                    └─ M9(numtheory)   [M3 unblocks crypto C7 pk]
```

**Tiers.** **T1:** M0, M1, M2. **T2:** M3 (bigint — crypto prerequisite), M8, M10. **T3:** M4, M5, M6, M7,
M9, M11. **No keystone dependency** — all of `teko::math` is pure Teko or libm-extern, so it can start
immediately, in parallel with the net/crypto pure units. M1 + M3 are the two that unblock crypto.

## 3. Open decisions (ratify with the net/crypto ones in PR #80)

1. **libm-backed vs pure-Teko** for M2 elementary functions. *(rec: libm extern + pure-Teko domain guard; pure approximations optional later)*
2. **Error vs NaN default** for domain failures in M2: does the primary `sqrt`/`log`/… return `f64 | error`, with a `*_raw` NaN variant — or the reverse? *(rec: error-returning is the primary/named surface; `_raw` for hot paths)*
3. **Generics vs per-width** for M1 checked/wrapping/saturating families. *(rec: generic over int types via monomorphization if the constraint system allows; else a generated per-width family)*
4. **BigInt constant-time mode** shape for crypto (fixed-width limbs, branchless) — part of M3 or a `bigint::ct` sub-unit? *(rec: a ct sub-flavor in M3, since crypto pk needs it)*
5. **Namespace confirmation:** `teko::math::*` as a sibling umbrella of `net`/`crypto`/`encoding`/`compress`, with `bigint` living here (not under crypto). *(rec: yes)*
