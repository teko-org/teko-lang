# TEKO — ROADMAP: web + API patterns (`teko::web` · `teko::auth` · API standards)

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors` (off `chore/reboot`)
>
> The layer that turns the net/crypto/encoding **primitives** into "I write an API comfortably": the
> web/API **patterns**. Built entirely on `teko::net::http`/`ws`/`sse`, `teko::io` streams, and the
> `teko::encoding` codecs. All 100% Teko.
>
> Companion to [`TEKO_ROADMAP_NET_CRYPTO.md`](TEKO_ROADMAP_NET_CRYPTO.md),
> [`TEKO_ROADMAP_STDLIB_CORE.md`](TEKO_ROADMAP_STDLIB_CORE.md) (io/iter),
> [`TEKO_ROADMAP_DB.md`](TEKO_ROADMAP_DB.md), and [`TEKO_ROADMAP_CLOUD_NATIVE.md`](TEKO_ROADMAP_CLOUD_NATIVE.md)
> (config/observability/resilience). Same agent-distributable contract.

---

## 0. The stdlib-vs-package boundary (ratify first)

Teko's doctrine is a small surface (Law M.0). So this roadmap splits deliberately:

- **STDLIB (thin, non-opinionated contracts + primitives):** the router/middleware **contract**, cookies,
  static files, the high-level HTTP client, content negotiation, request binding. Shared by everyone.
- **FIRST-PARTY PACKAGES (opinionated, shipped over the registry, not in every binary):** a full web
  framework, GraphQL, OAuth2/OIDC **server**, OpenAPI generation, the ORM (its own later roadmap). These
  ride the package registry + the on-demand-dependency rule (same spirit as DB's C7.20) so a consumer only
  pays for what it imports.

**Cross-cutting prerequisite — the `Json`/structural TRAITS ([`TEKO_ROADMAP_TRAITS.md`](TEKO_ROADMAP_TRAITS.md)):**
typed request binding, validation, and OpenAPI generation are dramatically better when a type can adopt a
compiler-known `Json`/schema trait (structural (de)serialization synthesized from its fields) instead of a
hand-written codec per type. Without it, layer B is per-type boilerplate. This is the `trait` *language*
decision (supersedes the rejected `#derive` attribute) and should be ratified before B is sliced. Doc-
comments are already parsed (`has_doc`/`doc`), so OpenAPI/descriptions can reuse them.

---

## A. Web foundation (STDLIB — thin)

**▪ W0 — `teko::web` router + middleware contract.** **Deps:** net N5 (http), stdlib `io`,
interfaces (W10b.IF ✅). **Files:** `src/web/router.tks`, `src/web/context.tks`.
```teko
type Context = struct { /* request, response writer, path params, values */ }
type Handler    = interface { fn handle(self, ctx: Ref<Context>) -> error? }
type Middleware = interface { fn wrap(self, next: Handler) -> Handler }
```
Route table (static + `:param` + `*wildcard`), method dispatch, 404/405, param extraction, a
`ServeMux`/`Router` that composes a middleware chain around a `Handler`. **The composability backbone** —
like `io::Reader` for bytes, `Handler`/`Middleware` for requests. **Verify:** `.tkt` for route matching +
param extraction (pure Teko); native request against a loopback server exercising the chain.

**▪ W1 — standard middleware.** **Deps:** W0, `teko::compress` (gzip), `teko::log`. Logging, recover
(panic→500), CORS, request-ID, timeout, gzip/br compression, basic rate-limit. **Verify:** `.tkt` per
middleware behavior.

**▪ W2 — cookies + sessions.** **Deps:** W0, crypto (C2 HMAC / C5 AEAD). Cookie parse/serialize (attrs,
SameSite), **signed and encrypted** session cookies + a pluggable session store. **Verify:** `.tkt`
roundtrip + tamper-detection.

**▪ W3 — static file serving.** **Deps:** W0, `io` streaming, `teko::fs`. Cache headers (ETag/Last-Modified),
conditional + **range** requests, directory handling, streamed bodies. **Verify:** native range/conditional.

**▪ W4 — high-level HTTP client.** **Deps:** net N5 client, `io`, encoding. Retries + backoff, redirects,
connection pooling, timeouts, JSON request/response helpers, multipart upload. **Verify:** native against
the W0 server (loopback).

**▪ W5 — request bodies: multipart + uploads + streaming.** **Deps:** W0, `io`. `multipart/form-data`
parse/build, streamed uploads (no whole-file-in-memory), `application/x-www-form-urlencoded`. **Verify:**
`.tkt` for the multipart codec.

**▪ W6 — SSE + WebSocket handlers.** **Deps:** W0, net N6 (ws codec), N7 (sse codec). The *handler-side*
integration: upgrade a `Context` to a WebSocket/SSE stream, connection lifecycle, broadcast helper.
**Verify:** native echo (WS) + event stream (SSE) over loopback.

## B. API standards / patterns

**▪ W7 — REST helpers.** **Deps:** W0, S-JSON. **Problem Details (RFC 7807)** typed error responses,
content negotiation (`Accept`), pagination helpers, ETag/conditional-request helpers, standard status
mapping (a `T | error` handler result → status + problem+json). **Verify:** `.tkt` for problem+json +
negotiation.

**▪ W8 — typed binding + validation.** **Deps:** W0, encoding (JSON/form/query), **TRAITS (Json/structural)**. Bind a
request (body/query/path/header) into a typed struct with constraints (required/range/pattern/…), return a
typed response encoded by content negotiation. The single biggest ergonomics win — **gated on the
structural-`trait` decision.** **Verify:** `.tkt` binding + validation-failure → 400 problem+json.

**▪ W9 — OpenAPI generation (PACKAGE).** **Deps:** W0, W8, **TRAITS (schema)**, doc-comments. Emit an OpenAPI 3.1
spec from routes + typed handlers + doc-comments; optional Swagger-UI static serve. API-first. **Verify:**
`.tkt` spec snapshot for a sample API.

**▪ W10 — GraphQL (PACKAGE).** **Deps:** W0, S-JSON. Schema type system, query parser, executor +
resolver interface, introspection. Large; first-party package. **Verify:** `.tkt` parse + execute against
a sample schema.

**▪ W11 — JSON-RPC + WebHooks (PACKAGE).** **Deps:** W0, S-JSON, crypto (HMAC for webhook signatures).
JSON-RPC 2.0 server/client; webhook dispatch + signature verify. **Verify:** `.tkt` codecs.

## C. Auth / application security — `teko::auth` (mostly PACKAGE)

**▪ W12 — sessions + tokens (STDLIB-thin).** **Deps:** W2, crypto (JWT app-helper C9, HMAC/AEAD). JWT
issue/verify wrapper, API-key middleware, bearer-token extraction, RBAC primitive (role/permission check).
**Verify:** `.tkt` token issue/verify + RBAC decisions.

**▪ W13 — CSRF + security headers (STDLIB-thin).** **Deps:** W0, W2. CSRF token middleware, security
headers (HSTS/CSP/X-Frame-Options), the CORS/rate-limit pair from W1. **Verify:** `.tkt`.

**▪ W14 — OAuth2 / OIDC (PACKAGE).** **Deps:** W4 (client), crypto (C7 pk for JWS verify, C8 x509),
S-JSON. OAuth2 client (auth-code + PKCE, client-credentials), OIDC relying-party (discovery, ID-token
verify); optionally an authorization-server later. First-party package. **Verify:** native flow against a
loopback IdP stub.

## 4. Dependency graph + tiers

```
net::http (N5) + io ── W0(router/mw) ─┬─ W1 std-mw ── W13 csrf/headers
                                            ├─ W2 cookies/sessions ── W12 tokens/RBAC
                                            ├─ W3 static
                                            ├─ W4 http client ── W14 oauth/oidc (pkg)
                                            ├─ W5 multipart/uploads
                                            ├─ W6 sse/ws handlers
                                            └─ W7 REST helpers ─┬─ W8 binding+validation [needs TRAITS]
                                                                ├─ W9 OpenAPI (pkg)   [needs TRAITS]
                                                                ├─ W10 GraphQL (pkg)
                                                                └─ W11 JSON-RPC/webhooks (pkg)
```

**Tiers.** **T1 (a real API today):** W0, W1, W2, W3, W4, W5, W7. **T2:** W6, W8 (needs TRAITS), W12,
W13, OpenAPI (W9). **T3:** W10 GraphQL, W11, W14 OAuth/OIDC, authorization-server.

**STDLIB vs PACKAGE:** stdlib = W0–W8, W12, W13 (contracts + primitives). Packages (over the registry) =
W9 OpenAPI, W10 GraphQL, W11, W14, and any full opinionated framework composing them.

## 5. Open decisions (ratify with the sibling roadmaps in PR #80)

1. **Structural `trait`s (cross-cutting, [`TEKO_ROADMAP_TRAITS.md`](TEKO_ROADMAP_TRAITS.md)):** ship the `Json`/schema structural traits (rec — they
   unblock W8/W9 and the whole `teko::encoding` auto-(de)serialize story) vs hand-written per-type
   codecs. This is a *language* decision; ratify before slicing B.
2. **stdlib/package split** above — confirm the boundary.
3. **Handler result shape:** handler returns `error?` writing to `Ctx` (rec, Go-style) vs returning a typed
   `Response` value the framework writes.
4. **Middleware model:** `interface`-based `wrap(next) -> Handler` (rec) vs closure `(Handler) -> Handler`.
5. **Full web framework:** keep it a first-party PACKAGE built on W0–W8 (rec), not stdlib — so the small
   surface (M.0) holds and consumers opt in via the registry.
