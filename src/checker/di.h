// src/checker/di.h — W10c/MEM-3 dependency injection (the DI wiring pass).
//
// FROZEN C-mirror header (historical navigation only — the maintained source is di.tks; the
// C bootstrap is no longer built). The DI pass runs after collect + module enforcement, over
// the raw program: it reads a type's `#lifetime` (di_kind) and a fn's `#inject` overlay, builds
// the compile-time provider registry, and validates every overlay (each injected type must have
// a registered provider — the never-null guarantee).
//
// Scope is CORE DI: the `#inject` overlay, the three lifetimes (#singleton/#scoped/#transient),
// registration + duplicate-error, and recursive/constructor DI. Keys (`@`), decorator pipelines,
// and `#wire` are out (issue #173); the AST carries the key fields inert and the pass rejects
// them with an honest stop.
//
// A pass either succeeds (null verdict) or fails with an `error`, exactly like the other
// checker passes (check_modules, analyze_program).
