# Phase 12 — Frontend Grammar & Lexer Extension

> Branch `feat/phase-12-frontend-grammar` (PR #5, draft). Builds on Phase 11 (Browser
> FFI + the real `.tks → IL → WASM` interop frontend), merged via PR #4.
>
> Goal (from `docs/plan.md` §Phase 12): get every new **token**, **AST node**, and
> **literal form** into the frontend so the later feature phases (13–16: concurrency,
> OOP, optionals/comptime, networking/web/crypto) have a grammar to compile against.
> This is a *frontend-grammar* phase: recognize and represent the surface; lowering and
> semantics of each feature land in their own phases.

## Scope (what this phase delivers)

### 1. Reserved keyword matrix → lexer tokens
The full token table the lexer must recognize, grouped (per the roadmap):
- **Resilience:** `circuit`, `fallback`, `delayed`, `retry`, `exponential`, `logarithmic`, `attempts`, `timeout`
- **OOP & concurrency:** `class`, `abstract`, `trait`, `event`, `raise`, `subscribe`, `fanout`, `fire_and_forget`, `shared`, `atomic`, `routines`, `duplex`
- **Web:** `api`, `middleware`, `get`, `post`, `put`, `delete`, `rpc`, `websocket` (`use` already exists)
- **Tooling:** `parse`, `json`, `csv`, `xml`, `html`, `bundle`, `minify`, `crypto`, `hash`, `encrypt`
- **Core:** `comptime`, `soa` (`defer`, `null` already exist)

Each becomes a `TOKEN_*` and a row in `keywords[]` (`src/lexer.c`). Purely additive —
recognition only; no parser action yet beyond not erroring.

> **Latent bug fixed here:** `keywords[]` had its `{NULL, …}` sentinel *before*
> `{"required", TOKEN_REQD}`, so the lookup loop stopped early and `required` was never
> matched. The sentinel is now last.

### 1A. Symmetry audit (counterpart/missing tokens)
A pass over the matrix for transform pairs without a counterpart:
- **Added** (P12 1A): `decrypt` (counterpart of `encrypt`; reserved → crypto phase) and
  the base-encoding surface `encode` / `decode` / `base64` / `base32` / `hex` (made
  functional in block C).
- **Added as reserved-with-target** (no dead tokens): `sign`/`verify` → **Phase 13**
  (crypto); `serialize`/`stringify` → **Phase 18** (parsers; static per-type generators,
  Go-style, no runtime reflection); HTTP `patch`/`head`/`options` → **Phase 17** (web).
- **Skipped:** `compress`/`decompress` — not on the roadmap, no target phase to reserve to.

### Governing rule (from this point on)
**No token ships "dead."** Every new token must come with grammar + functional logic +
an executable test, **or** be explicitly marked *reserved — lowering in Phase X* in the
status table below. The existing recognition-only keywords are all **reserved** (post-renumber): crypto
(`crypto`/`hash`/`encrypt`/`decrypt`, and `sign`/`verify` once added) → **Phase 13 (Native
Cryptography)**; resilience/concurrency → Phase 14; OOP → Phase 15; comptime/soa → Phase 16;
web → Phase 17. The base-encoding set (`encode`/`decode` over `base64`/`base32`/`hex`) is
**not** cryptography and is the first group to become **functional** within Phase 12.

### 2. Native literal suffixes (zero runtime cost, captured in the lexer)
- **Time:** `ms`, `s`, `m`, `h`, `d`
- **Data:** `b`, `kb`, `mb`, `gb`
- **Bandwidth:** `kbps`, `mbps`, `gbps`
`lex_number` captures an optional trailing unit suffix and tags the literal token with
its unit/category, so a `500ms` or `2mb` literal carries its dimension into the AST.

### 3. AST node mapping
Extend the AST with nodes representing the new Web / OOP / crypto / resilience
expressions so the rest of the compiler has a representation to lower in later phases.
(The repo's AST is split across `parser.h` / `parser_statements.h` / `parser_ffi.h`
rather than a single `ast.h`; this phase consolidates the *new* nodes coherently.)

### 4. Foundational frontend gaps carried over from Phase 11
Phase 11's interop frontend was deliberately bounded (literal/handle args only). These
foundational pieces unblock a *real* expression frontend and belong early in Phase 12:
- **Named local variables** (`let`/`mut` bindings) with a frontend symbol table and slot
  assignment, lowered to IL load/store.
- **General expressions / arithmetic** from source (precedence-climbing or Pratt parser
  over the existing operator tokens) → IL.
- **Multiple nested handle args** (lift the Phase 11 "one leading nested call" limit) via
  spilling intermediates to named temporaries.

## Increment plan (status filled in as we go)
1. **P12-A** keyword tokens + sentinel fix — ✅ done.
2. **P12-B** native literal suffixes (time/data/bandwidth) — ✅ done.
3. **P12-1A** symmetry audit — ✅ done (`decrypt` + base-encoding tokens added; others listed).
4. **Foundational frontend (D → E → F)** — `let`/`mut` named locals → general expressions
   (Pratt) → multiple nested handle args. *Prerequisite for expressing/calling any feature
   (e.g. `let s = base64.encode(x)`).*
   - **P12-D — named locals. ✅ done.** New IL ops `OP_LOAD_LOCAL`/`OP_STORE_LOCAL` backed
     by real WASM locals (`$v0..$vN`, declared at `$main` open; count threaded via
     `MetalContext`). `let`/`mut NAME [: type] = <init>` → `STORE_LOCAL`; a reference →
     `LOAD_LOCAL`. Initializers: int/string literals, a `@dom` call result, or another
     local (full expressions arrive in P12-E). Locals are `$main`-scoped (handlers excluded).
   - **P12-E — general expressions** (next): Pratt parser over the operator tokens →
     IL; integer arithmetic + comparisons first (float / `&&`/`||` short-circuit deferred).
   - **P12-F — multiple nested handle args**: spill intermediates to named locals.
5. **Base encoding (functional)** — real `base64`/`hex` encode+decode (runtime/intrinsic +
   grammar + executable round-trip test against known vectors). The first clean functional
   win; deterministic, no external deps.
6. **Crypto (`encrypt`/`decrypt`) — GATED.** Needs a real cipher; scope (AES-256-GCM /
   ChaCha20-Poly1305 / …) to be proposed and signed off — likely its own phase (roadmap
   Phase 14/16), not this PR.
7. **P12-C** AST node scaffolding for remaining surfaces — *extend the split AST, no rewrite.*

Discipline (per `CLAUDE.md`): 1 increment per commit; ASan/UBSan on both dispatch paths +
TSan; the 16 native emitter goldens never regress; 4 CI gates green; docs/CLAUDE.md kept
current; no merge/force-push.
