# TEKO — ROADMAP: database connectors (`teko::db::*`) + on-demand FFI linking

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `main`
>
> A `teko::db::*` surface: one common connection interface + plugable drivers. **Native Teko wire-protocol
> drivers are PREFERRED** (zero external dependency, portable, `FROM scratch`-friendly); FFI drivers exist
> only where a native implementation is impractical (embedded engines like SQLite). Core rule:
> **the compiler links a driver's FFI library ONLY if the user's reachable code actually uses it** — a dev
> who never touches SQLite must not need `libsqlite3` installed.
>
> Companion to [`TEKO_ROADMAP_NET_CRYPTO.md`](TEKO_ROADMAP_NET_CRYPTO.md) (drivers build on `teko::net`
> sockets/TLS) and [`TEKO_ROADMAP_STDLIB_CORE.md`](TEKO_ROADMAP_STDLIB_CORE.md) (`io` streams; error handling via `match`).
> Same agent-distributable contract.

---

## 0. Principles

- **Native over FFI.** A documented wire protocol (Postgres, MySQL, MongoDB, TDS, Redis) is implemented in
  pure Teko over `teko::net::tcp`/`tls` — no driver dependency, one binary, cross-platform for free. This
  is the default; FFI is the exception.
- **FFI only when there's no wire protocol.** Embedded engines (SQLite) are C libraries with no network
  protocol — those bind via `extern` (C7.1). A few network engines may also offer an FFI driver as an
  alternative to the native one.
- **On-demand linking (the keystone).** Declaring a driver in a package must NOT force its library on
  every consumer. The compiler links `-l<lib>` for a driver **iff a reachable `extern` from that library
  survives** in the user's program. Unused drivers cost nothing and require nothing installed.

---

## 1. KEYSTONE — extern reachability / on-demand FFI linking

### ▪ DB-KEYSTONE — `C7.20`: link only the FFI libraries actually used
**Deps:** none (compiler-side). **Files:** `src/checker/*` (reachability), `src/codegen/codegen.{tks,c}`
(emit + lib collection), `src/build/project.{tks}` / driver (linker flags), a smoke example.

**Behavior.**
1. **Reachability over externs.** After type-check, compute the set of `extern` functions reachable from
   the program's `main` (extends the machinery that already flags unused private fns —
   [initanalysis]). Unreferenced externs are **tree-shaken** (no prototype, no call, no lib requirement).
2. **Lib set = surviving externs' `from_lib`.** Collect `from_lib` ONLY from reachable externs; pass just
   those `-l<lib>` (per-OS via `#os` + `.tkp [extern.libs.*]`) to the host `cc`. Today `from_lib` is
   plumbed on every declared extern (`is_extern`/`c_symbol`/`from_lib`); this unit makes the *requirement*
   usage-driven, not declaration-driven.
3. **Manifest declares availability, use drives requirement.** `.tkp [extern.libs.*]` still lists a
   library's per-OS name; but a package can declare `libsqlite3` without forcing it on consumers who never
   call the SQLite driver.
4. **Clear, late error.** If a *used* extern's library is missing on the target, the link failure names
   the `from_lib` + symbol (via `.tsym`), not a raw `cc` error.

**Verify:** a smoke example with TWO extern-backed modules where `main` calls only one → the generated
build links only that one's lib (assert the other's `-l` is absent); native build + `.tkt` where possible;
both twins byte-identical.

> This is the ONLY compiler-side unit in this roadmap. Everything below is pure-Teko library code (native
> drivers) or thin `extern` bindings (FFI drivers) that ride this keystone.

---

## 2. `teko::db` — the common surface

### ▪ DB0 — core interface + value model
**Deps:** STDLIB `io` (nice-to-have), net N1 (tcp). **Files:** `src/db/db.tks`.
```teko
type Value      = variant Null | Int | Float | Text | Bytes | Bool | Timestamp | Decimal
type Row        = struct { cols: []str; vals: []Value }
type Rows       = interface { fn next(self) -> Row? ; fn close(self) -> error? }   // lazy cursor
type ExecResult = struct { affected: u64; last_id: i64? }
type Connection = interface {
    fn query(self, sql: str, params: []Value) -> Rows | error
    fn exec (self, sql: str, params: []Value) -> ExecResult | error
    fn begin(self) -> Tx | error
    fn close(self) -> error?
}
type Tx = interface { fn commit(self) -> error? ; fn rollback(self) -> error? ; /* + query/exec */ }
```
Plus `DbError` (SQLSTATE/driver code + message), a `Pool` (connection pooling), and parameter binding
helpers. Every driver implements `Connection`/`Rows`/`Tx`. **Verify:** `.tkt` for the value model +
param-binding encode (pure Teko); driver-agnostic.

## 3. Native wire-protocol drivers (preferred — pure Teko, zero deps)

**▪ DB-PG — `teko::db::postgres`.** **Deps:** DB0, N1 (+N3 TLS), C-crypto (SCRAM-SHA-256 auth → C2 HMAC +
C1 SHA-256). PostgreSQL frontend/backend protocol v3: startup, auth (md5/SCRAM), simple + extended query
(prepared statements/portals), COPY, type decode. **Verify:** `.tkt` for the message codec; native against
a loopback Postgres. **(T1 — the flagship native driver.)**
**▪ DB-MY — `teko::db::mysql`.** **Deps:** DB0, N1 (+N3). MySQL/MariaDB protocol: handshake, auth
(caching_sha2/mysql_native), text + binary protocol, prepared statements. **(T2)**
**▪ DB-MONGO — `teko::db::mongodb`.** **Deps:** DB0, N1, S-BSON (a `teko::encoding::bson` sibling).
MongoDB wire protocol (OP_MSG) + SCRAM auth. **(T2/T3)**
**▪ DB-TDS — `teko::db::mssql`.** **Deps:** DB0, N1, N3. TDS protocol (SQL Server). **(T3)**
**▪ Redis** — already `teko::net::redis` (RESP) in the net roadmap; `teko::db` may re-export a
`Connection`-shaped adapter. **(T2)**

## 4. FFI drivers (only where no wire protocol fits)

**▪ DB-SQLITE — `teko::db::sqlite`.** **Deps:** DB0, DB-KEYSTONE. `extern` bindings to `libsqlite3`
(`sqlite3_open/prepare/step/column_*/finalize/close`), `#os`-guarded, declared in `.tkp
[extern.libs.*]`. Embedded, file-based; no network. **Because of the keystone, a program that imports
`teko::db` but never calls the SQLite driver does NOT require `libsqlite3`.** **Verify:** native
open→create→insert→select roundtrip on a temp DB file. **(T1 — embedded is very common.)**
**▪ DB-ODBC (optional, T3)** — `teko::db::odbc` over the platform ODBC lib, as a universal fallback for
engines without a native driver. Same on-demand-link rule.

## 5. Dependency graph + tiers

```
DB-KEYSTONE (compiler) ── enables all FFI drivers (sqlite/odbc)
DB0 ─┬─ DB-PG   (native; +HMAC/SHA-256 for SCRAM)      ← flagship
     ├─ DB-MY   (native)
     ├─ DB-MONGO(native; +BSON)
     ├─ DB-TDS  (native)
     ├─ DB-SQLITE (FFI; needs DB-KEYSTONE)
     └─ Pool / Tx (shared)
```

**Tiers.** **T1:** DB-KEYSTONE, DB0, DB-PG (native Postgres), DB-SQLITE (embedded). **T2:** DB-MY, Pool,
MongoDB, Redis adapter. **T3:** DB-TDS, ODBC, higher-level query builder/ORM (separate later roadmap).

## 6. Open decisions (ratify with the sibling roadmaps in PR #80)

1. **DB-KEYSTONE granularity**: confirm whether codegen already skips unreferenced externs, or whether
   usage-driven `from_lib` collection is new work. *(likely new, small — in the lib collector.)*
2. **Parameter style**: a single portable placeholder (`?`) rewritten per-driver vs exposing each driver's
   native style (`$1`, `?`, `:name`). *(rec: portable `?` in the common layer, driver rewrites.)*
3. **Rows model**: lazy cursor (`next() -> Row?`, rec) vs eager `[]Row`. *(rec: lazy — composes with `teko::iter`.)*
4. **Native vs FFI default for Postgres/MySQL**: ship native as the blessed path (rec), FFI (`libpq`) only
   as an opt-in alternative.
5. **Async**: sync-first; async DB (`Intent<Rows>`) rides S8, additive — out of scope here.
6. **ORM / query builder**: explicitly a SEPARATE later roadmap, not part of the connector layer.
