---
section: security-architecture
created: 2026-07-13
source: docs/design/memory-unsafe-backend-remodel.md (D31 norte), .claude/skills/dispatch/SKILL.md
---

# Teko Security North Star

**"Segurança vence na borda; simplicidade vence no caminho comum"** (#530 norte)

Cost proportional to risk: opt-in machinery (unsafe, raw allocation, `adopt`) at trust boundary; simple, safe core (arena, references, error values) in common path.

**Containment layers:** arena (default, invisible) → `adopt` (lexical, bulk-drop) → `unsafe` (type modifier, full risk visible) → RawBuf (malloc/free cru, explicit). Each layer explicit at declaration; no spillover.

**Fail-loud inside / fail-soft at trust boundary:** panics/asserts inside (safe code aborts on invariant violation), but FFI boundary catches (C errors → `error` values, no crash); WASM capability firewall (no import = no capability).

**Blazor anchor (explicit contract like JSInvokable):** per-module `extern` FFI surface (allowlist), per-module capability firewall (WASM Model 1/2 #530/#535), leadbox container (bounded by libc + runtime seam), language-neutral contract.
