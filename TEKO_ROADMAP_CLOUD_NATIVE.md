# TEKO — ROADMAP: cloud-native / 12-factor operations

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors` (off `chore/reboot`)
>
> The production-operations layer: configuration, observability (logs/metrics/traces), lifecycle
> (health, graceful shutdown, signals), resilience (retry/backoff/circuit breaker), and background jobs.
> These turn a Teko service from "it runs" into "it runs in a cluster and I can see + control it." 100%
> Teko, over the existing `extern`/`#os` surfaces and the net/web roadmaps.
>
> Companion to [`TEKO_ROADMAP_WEB.md`](TEKO_ROADMAP_WEB.md), [`TEKO_ROADMAP_NET_CRYPTO.md`](TEKO_ROADMAP_NET_CRYPTO.md),
> and [`TEKO_ROADMAP_STDLIB_CORE.md`](TEKO_ROADMAP_STDLIB_CORE.md). Same agent-distributable contract.

---

## 0. Ground rules

- **12-factor aligned** (config in env, logs to stdout as an event stream, disposability/fast shutdown) —
  the same principles the Phase-11 code-quality sweep already targets for the compiler itself.
- **Mostly stdlib-thin contracts + a first-party package for the opinionated glue.** Log/metrics/trace are
  thin facades (an interface + a default sink) so libraries emit through them without pulling a backend;
  exporters (Prometheus scrape endpoint, OTLP) are packages.
- **Observability composes with `teko::web`** (a `/metrics` handler, a `/healthz` handler, trace-context
  middleware) and with `teko::log` — cross-referenced, not duplicated.

---

## 1. Units

### Configuration

**▪ CN0 — `teko::config`.** **Deps:** `teko::env` (Map), encoding (JSON/YAML/TOML).
**Files:** `src/config/config.tks`. Layered configuration with precedence **flags < file < env**, typed
access (`get_int`/`get_str`/`get_bool` → `T | error`), binding a config struct (helped by the **Json/structural traits**),
`.env` file loading, required-key validation. **Verify:** `.tkt` — precedence + typed parse + missing-key
error.

### Observability (the trio)

**▪ CN1 — `teko::log` (structured logging).** **Deps:** `teko::io`, encoding (JSON), `teko::time`.
**Files:** `src/log/log.tks`. Levels (trace..error), structured fields, a `Logger` interface + default
JSON/line sinks to stdout (12-factor: logs are an event stream), child loggers with bound fields,
sampling. **The base facade** the whole ecosystem logs through. **Verify:** `.tkt` for field encoding +
level filtering.

**▪ CN2 — `teko::metrics`.** **Deps:** CN1, net N5 (http, for the scrape endpoint). **Files:**
`src/metrics/metrics.tks`. Counter/Gauge/Histogram/Summary, labels, a registry, and a **Prometheus /
OpenMetrics** text-exposition `/metrics` handler (plugs into `teko::web`). OTLP push is a later package.
**Verify:** `.tkt` for the exposition-format encoder.

**▪ CN3 — `teko::trace` (distributed tracing).** **Deps:** CN1, net (http headers). **Files:**
`src/trace/trace.tks`. Spans, a `Tracer` interface, **W3C Trace-Context** propagation (`traceparent`),
a `teko::web` middleware that continues/starts spans, baggage. **OpenTelemetry OTLP exporter** as a
first-party package. **Verify:** `.tkt` for trace-context parse/inject + span tree.

### Lifecycle

**▪ CN4 — health + readiness.** **Deps:** `teko::web`. Liveness/readiness probe registry + `/healthz`,
`/readyz` handlers aggregating component checks (DB ping, dependency reachability). **Verify:** `.tkt`
aggregation logic; native endpoints.

**▪ CN5 — graceful shutdown + signals.** **Deps:** `#os` externs (`sigaction`/`SetConsoleCtrlHandler`),
stdlib. **Files:** `src/os/signal.tks` (namespace `teko::os::signal`). Catch SIGINT/SIGTERM (POSIX) /
Ctrl-C (Windows), a shutdown `Context` (ties to the async cancellation model,
[[teko-async-concurrency-design]]), drain in-flight requests then exit. **Verify:** native — send signal,
assert clean drain + exit code.

### Resilience

**▪ CN6 — `teko::resilience`.** **Deps:** `teko::time`, closures (W10 ✅). **Files:**
`src/resilience/*.tks`. **Retry** with backoff (exponential + jitter) + max attempts, **circuit breaker**
(closed/open/half-open), **timeout** wrapper, **bulkhead**/semaphore-limit. All as higher-order functions
over a fallible closure `() -> T | error`. **Verify:** `.tkt` — deterministic backoff schedule, breaker
state transitions (fixed clock injected).

### Scheduling / background work

**▪ CN7 — `teko::cron` + jobs.** **Deps:** `teko::time`, async (S8), `teko::resilience`. **Files:**
`src/cron/*.tks`. Cron-expression parser + scheduler, one-shot/interval timers, an in-process job runner;
a durable queue (backed by a `teko::db` table or Redis) is a later package. **Verify:** `.tkt` for the
cron-expression parser + next-fire computation.

## 2. Dependency graph + tiers

```
teko::env + encoding ── CN0 config
teko::io + encoding ─── CN1 log ─┬─ CN2 metrics ── (web /metrics)
                                 └─ CN3 trace  ── (web trace-mw) ── OTLP exporter (pkg)
web ── CN4 health/readiness
#os signals ── CN5 graceful shutdown ── (async cancellation)
time + closures ── CN6 resilience
time + async ── CN7 cron/jobs
```

**Tiers.** **T1 (observable service):** CN0 config, CN1 log, CN4 health, CN5 graceful shutdown.
**T2:** CN2 metrics, CN3 trace, CN6 resilience. **T3:** CN7 cron/jobs, OTLP exporter, durable job queue.

**STDLIB vs PACKAGE:** stdlib = CN0, CN1 (log facade), CN4, CN5, CN6, the metrics/trace **facades**.
Packages (registry) = the Prometheus/OTLP **exporters**, durable job queue — so a service pulls only the
backends it uses (same on-demand spirit as DB C7.20 / web packages).

## 3. Open decisions (ratify with the sibling roadmaps in PR #80)

1. **Signal handling** in a self-hosting all-native runtime: pure `#os` externs (rec) vs a small runtime
   helper — confirm no new C is needed beyond compiler-mirrored twins.
2. **Log facade shape:** a global default logger + injectable `Logger` interface (rec) vs pass-a-logger-
   everywhere. Ties to the DI model (W10c).
3. **Metrics/trace facades in stdlib, exporters as packages** — confirm the split so no binary carries a
   Prometheus/OTLP dependency it doesn't use.
4. **Config binding** relies on the structural `trait`s ([`TEKO_ROADMAP_TRAITS.md`](TEKO_ROADMAP_TRAITS.md)) for typed struct binding — confirm the shared
   dependency.
5. **cron/jobs** depends on the async model (S8) landing — confirm sequencing (T3, after S8).
