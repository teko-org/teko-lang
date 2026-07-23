# Drain plan — fase-3 stdlib (33 issues): ORDER + first crumbs

Architect deliverable. Read-only design. NO builds were run producing this.

## 0. Dependency reality check (verified against live `gh` + `src/`)

The "generic layer" that fase-3 collections were feared blocked on is **almost fully
landed**:

| Dep | Meaning | State |
|-----|---------|-------|
| #162 | S6 — operate on `T` under a constraint (methods/operators via ConstraintExpr) | **CLOSED** |
| #169 | net/crypto ratification + **C7.19 byte-buffer transport** (the keystone) | **CLOSED** |
| #185 | M0+M1 — `teko::math` root + `teko::math::checked` | **CLOSED** |
| #254 | S4c — **methods on generic types** (monomorphize method bodies) | **OPEN** |

So the ONLY open piece of the generic layer is **#254**. #163 collections needs it
(Map/List/Set are classes-with-methods over `<K,V>`/`<T>`). Everything else in fase-3 is
either monomorphic-buildable now, or blocked only on a sibling stdlib root, not on generics.

`teko::list::{empty,push,...}` is today a **builtin free-function** surface (used pervasively
in the compiler — `scope.tks`, `vm.tks`). There is NO `List`/`Map`/`Set` class file yet.
`-lm` is ALREADY linked (`src/build/project.tks:423` adds `-lm` unless freestanding), so
`math::real` (#186) needs zero new link wiring — only `extern fn ... from "m"` declarations
plus a manifest `[extern.libs.*]` entry pattern (see crumbs).

## 1. The split: MONOMORPHIC-NOW vs NEEDS-GENERIC-LAYER (#254)

### A. Monomorphic-buildable NOW (no generics — the #184 iterator precedent: concrete types, closures, `[]byte`)
These operate on `f64`, `str`, `[]byte`, concrete structs — expressible with today's
language (per `src/io/stream.tks`, `src/encoding/json/json.tks`, `src/compress/compress.tks`
which are all monomorphic pure-Teko today).

- **#186** `math::real` — libm FFI over `f64` (ROOT, most-ready)
- **#189** encodings T1 — TOML + CSV + URL/MIME + base64 (ROOT; pure `str`/`[]byte`)
- **#192** compress — DEFLATE/gzip/zlib (ROOT; pure `[]byte`; ZIP-STORE + CRC32 already exist)
- **#194** crypto core — hash (SHA-2/3/BLAKE/MD5) + CSPRNG (ROOT; pure `[]byte` + `getentropy` FFI)
- **#187** math bigint/decimal/rational (concrete struct arithmetic; no generics)
- **#188** math complex/linalg/stats/numtheory/random (concrete; some want `List` for linalg — degrade to `[]f64`)
- **#190** binary encodings protobuf/ASN.1/CBOR/MessagePack (pure `[]byte`)
- **#191** doc encodings XML/YAML (pure `str`)
- **#193** compress advanced brotli/lzma/zstd (pure `[]byte`; after #192)
- **#195** crypto HMAC/KDF/password (over #194 hash primitives)
- **#196** crypto cipher/AEAD (pure `[]byte` block math; after #194)
- **#197/#198** crypto pk/x509/PGP (bigint from #187 + hash from #194)
- **#209** `teko::log` (facade + JSON/pretty writers; concrete)
- **#215** `teko::config` (layered; consumes TOML #189)

### B. NEEDS the generic layer (blocked on #254 generic methods)
- **#163** `teko::collections` — Map/List/Set/LinkedList/BTree AS CLASSES over `<K,V>`/`<T>`. Direct blocked-on #254.
- **#184** io — combinators re-typed against `Reader`/`Writer` **interface-as-value** (OOP ROUND 3 D2/D3), and the `teko::iter` protocol wants generic `Iter<T>`. Partially monomorphic today (closures twin already shipped in `stream.tks`); the generic re-typing waits.

### C. Blocked on OTHER stdlib roots (not on generics) — the net/db/web/cloud tail
Sequenced by their own root, drained after the roots below. NOT part of "first crumbs".
- net: #199 (core) → #200 (TLS) → #201 (HTTP/1.1) → #202/#203/#204 → then web #210–#214, cloud #216/#217
- db: #205 (interface) → #206/#207 → #208
- These consume #163 collections + #184 io + #189 encodings + #194 crypto. They are LEAVES.

## 2. THE ORDER (topological)

```
WAVE G (unblock generics)          #254  ── OPEN, deps CLOSED → start immediately
     │
     ├──────────────► WAVE C  #163 collections   (needs #254)
     │
WAVE R (monomorphic ROOTS — parallelizable, independent of #254)
     #186 math::real     ─┐
     #189 encodings T1   ─┼─ all four start in parallel with WAVE G
     #192 compress        │  (no dep on #254 or on each other)
     #194 crypto core    ─┘
     │
WAVE R2 (monomorphic, depend only on a WAVE-R root)
     #187 bigint (→ #197 pk)      #193 compress-adv (→needs #192)
     #188 math-adv               #195 crypto KDF (→#194)   #196 cipher (→#194)
     #190 binary-enc  #191 doc-enc  #209 log  #215 config(→#189 TOML)
     │
WAVE L (leaves — need collections/io + a root): net #199→…, db #205→…, web #210→…, cloud
```

**Recommended drain start (parallel):** `#254` (generic methods — unblocks C) **and** the
three most-ready monomorphic roots `#186`, `#189`, `#192`. `#194` starts alongside once a
CSPRNG FFI decision is confirmed (getentropy/getrandom — already ratified in #169 package).

Rationale (law-first): Laws favour draining KEYSTONES first (#254 gates all of onda-C and
the class-shaped collections that net/db/web consume). But keystones don't stall the
independent monomorphic roots — "adiantar o que der" mandates running R in parallel.

## 3. FIRST CRUMBS

All snippets below are **already in full-Javadoc W15 style** — implementers copy verbatim.
`teko::regex`/`teko::io::stream` precedent: **library modules are `.tks` + `.tkt` ONLY**, no
C twin (SUPREME RULE binds compiler C↔.tks pairs, not library code). New files therefore
add a `.tks` + a `.tkt`; no `.c`/`.h`.

Ritual points (where the FULL gate must pass — both engines · paranoid · diff_vm_native ·
parity · fixpoint): at the END of EACH crumb that adds a new `.tks` to the corpus, because
every new corpus file changes the self-build. Extern-touching crumbs additionally must run
the **native** leg (VM rejects `extern fn` with `vm_unsupported` — see `time.tks` header),
so their `.tkt` fixtures gate `teko build` output, and VM fixtures assert the honest-stop.

---

### 3.1 #186 — `teko::math::real` (libm FFI) — MOST READY

New file `src/math/real.tks` (+ `src/math/real_test.tkt`). Pattern mirrors `time.tks`
externs. `-lm` already linked; the extern `from "m"` names the libm soname.

**Crumb sequence:**

1. **manifest extern pattern** — confirm/add `m = []` under `[extern.libs.linux]`,
   `[extern.libs.macos]`, `[extern.libs.windows]` in `teko.tkp` IF the extern resolver
   requires an explicit lib entry (today `-lm` is force-added by `project.tks`; the crumb is
   to verify the extern-resolution path finds `from "m"` without a manifest entry — REPORT
   up if a manifest row is needed, do not silently add). Gate: none (config only).

2. **the extern surface** — one `extern fn` per libm entry, `f64 -> f64` (and 2-arg for
   `pow`/`atan2`/`hypot`). Native-only (VM honest-stops). Doc-comment EVERY decl:

```teko
/**
 * Square root of a non-negative f64 (libm `sqrt`).
 *
 * @param x the radicand; negative input yields NaN per IEEE-754
 * @return the non-negative square root, or NaN for x < 0 / NaN input
 * @since 0.0.1.4
 */
pub extern fn sqrt(x: f64) -> f64 = "sqrt" from "m"

/**
 * Natural logarithm (libm `log`).
 *
 * @param x the operand; x <= 0 yields NaN (0 yields -inf per IEEE-754)
 * @return ln(x), or NaN/-inf at the domain edges
 * @since 0.0.1.4
 */
pub extern fn log(x: f64) -> f64 = "log" from "m"

/**
 * Two-argument arctangent (libm `atan2`) — the angle of the vector (x, y).
 *
 * @param y the ordinate
 * @param x the abscissa
 * @return the angle in radians in (-pi, pi]
 * @since 0.0.1.4
 */
pub extern fn atan2(y: f64, x: f64) -> f64 = "atan2" from "m"
```
   Full set: `sqrt cbrt exp exp2 expm1 log log2 log10 log1p pow(2) sin cos tan asin acos
   atan atan2(2) sinh cosh tanh asinh acosh atanh hypot(2) fmod(2) floor ceil round trunc`.
   Gate: **full ritual** (native leg authoritative; VM fixture asserts honest-stop).

3. **precision-vector fixtures** `real_test.tkt` — `#test` cases asserting each fn against
   known values within an ULP tolerance helper. Because VM can't call externs, these
   fixtures are `teko build`-gated (native): input `f64` → expected `f64` within epsilon.
   Add a `fn approx_eq(a: f64, b: f64, eps: f64) -> bool` guard in the `.tkt`.
   Expected exit codes: native run exit 0 (all assert pass); VM run of an extern caller =
   honest-stop nonzero (assert the stop, per `time_test` precedent).

**Type shapes touched:** none in compiler core. New: ~30 `pub extern fn` in `teko::math`.
**Risk/law tension:** MD5-style — none. libm is a de-facto libc component; matches the
roadmap's "FFI libm por decisão" (already ratified). Only open micro-question: whether the
extern resolver needs a manifest `m = []` row — RESOLVE by inspecting the resolver path;
REPORT up if a row is required (do not create an issue).

---

### 3.2 #192 — `teko::compress` DEFLATE/gzip/zlib — READY (scaffolding exists)

Extends existing `src/compress/compress.tks` (has CRC32 + ZIP-STORE writer already). Pure
`[]byte`, monomorphic, NO extern. NOTE the existing file is pre-W15 (`//` comments); any
NEW decl added must be full-Javadoc, and per "toque = limpa" the touched neighbouring decls
get flattened/Javadoc'd as they are edited.

**Crumb sequence:**

1. **Adler-32** (zlib checksum) — mirror the existing `crc32_of` shape:
```teko
/**
 * Adler-32 checksum (RFC 1950) of a byte slice — the zlib trailer checksum.
 *
 * @param data the bytes to checksum
 * @return the 32-bit Adler-32 value (high 16 bits = sum-of-sums mod 65521, low = sum)
 * @since 0.0.1.4
 */
fn adler32_of(data: []byte) -> u32 { /* running s1=1,s2=0 mod 65521 */ }
```
   Gate: full ritual (new corpus decl).

2. **DEFLATE decoder (inflate)** first (needed to round-trip test the encoder):
   fixed + dynamic Huffman, LZ77 back-references. Returns `[]byte | error`.
```teko
/**
 * Inflate a raw DEFLATE (RFC 1951) bit stream to its original bytes.
 *
 * @param deflated the raw DEFLATE payload (no zlib/gzip header)
 * @return the decompressed bytes, or an error on malformed input / bad back-reference
 * @since 0.0.1.4
 */
pub fn inflate(deflated: []byte) -> []byte | error { ... }
```
   Sub-helpers (all Javadoc'd, flattened via early-return): `BitReader` value-struct
   (`data: []byte; byte_pos: u64; bit_pos: u8`) with value-threading `read_bits_step` (the
   `stream.tks` B.7 pattern — returns advanced reader + value), Huffman `CodeTable` built
   from code-length counts. Gate: full ritual.

3. **DEFLATE encoder (deflate)** — start with STORED blocks (type 00) + a fixed-Huffman
   path; dynamic-Huffman is a follow-up sub-PR. `pub fn deflate(raw: []byte) -> []byte`.
   Gate: full ritual + a **round-trip fixture** `inflate(deflate(x)) == x`.

4. **gzip / zlib wrappers** — header + checksum framing over `deflate`/`inflate`:
```teko
/**
 * Wrap DEFLATE output in a gzip container (RFC 1952): 10-byte header, deflate body,
 * CRC-32 + ISIZE trailer.
 *
 * @param raw the bytes to compress
 * @return a complete gzip member
 * @since 0.0.1.4
 */
pub fn gzip(raw: []byte) -> []byte { ... }

/**
 * Wrap DEFLATE output in a zlib stream (RFC 1950): 2-byte header, deflate body,
 * Adler-32 trailer.
 */
pub fn zlib(raw: []byte) -> []byte { ... }
```
   Plus `gunzip`/`zlib_inflate` inverses. Gate: full ritual.

5. **ZIP DEFLATE entries** — add method=8 entries to the existing `write_zip` (currently
   STORE-only). New `ZipEntry` field or a `method` selector; the deterministic-archive
   guarantee (no timestamps) must hold. Gate: full ritual.

**Fixtures `compress_test.tkt`:** known DEFLATE vectors (RFC test payloads) →
inflate==expected; round-trip `inflate(deflate(x))==x` over several inputs; gzip/zlib
header-byte assertions; empty-input edge (exit 0). VM==native parity REQUIRED (pure Teko,
both engines run it). Expected: exit 0 both engines.
**Risk:** DEFLATE is the largest single algorithm here — split into inflate/deflate/wrappers
sub-PRs. Memory: LZ77 window + Huffman tables allocate; watch self-build peak (auto-reported)
— use the arena, avoid O(n²) `slice_push` growth (known lesson: amortized push).

---

### 3.3 #189 — `teko::encoding` T1 (TOML + CSV + URL/MIME + base64) — READY

New files under `src/encoding/`: `base64.tks`, `url.tks`, `csv.tks`, `toml.tks`
(+ `_test.tkt` each). Pure `str`/`[]byte`, monomorphic, no extern. Order WITHIN the issue:
base64 → url → csv → toml (toml is the biggest; base64 is reused by url/MIME).

**Crumb sequence:**

1. **base64** (`base64.tks`) — encode/decode, standard + URL-safe alphabets, padding:
```teko
/**
 * Encode bytes to standard Base64 (RFC 4648 §4) with `=` padding.
 *
 * @param data the bytes to encode
 * @return the Base64 ASCII string
 * @since 0.0.1.4
 */
pub fn encode(data: []byte) -> str { ... }

/**
 * Decode a standard Base64 string (RFC 4648 §4).
 *
 * @param text the Base64 text (padding optional on decode)
 * @return the decoded bytes, or an error on an invalid alphabet char / bad length
 * @since 0.0.1.4
 */
pub fn decode(text: str) -> []byte | error { ... }
```
   Gate: full ritual. Fixtures: RFC 4648 test vectors (`""`→`""`, `"f"`→`"Zg=="`,
   `"foobar"`→`"Zm9vYmFy"`), invalid-char → error. Exit 0 both engines.

2. **url** (`url.tks`) — percent-encode/decode + `application/x-www-form-urlencoded`
   (form pairs → `[](str,str)` or a small struct list). `pub fn percent_encode(s: str,
   reserved: str) -> str`; `pub fn form_decode(body: str) -> []FormPair | error`.
   Gate: full ritual. Fixtures: space→`%20`/`+` per mode, round-trip.

3. **csv** (`csv.tks`) — RFC 4180 reader/writer (quotes, embedded commas/newlines, CRLF).
   `pub fn parse(text: str) -> [][]str | error`; `pub fn write(rows: [][]str) -> str`.
   Gate: full ritual. Fixtures: quoted field with comma, embedded `""` escape, trailing
   newline. Exit 0 both engines.

4. **toml** (`toml.tks`) — RFC-compliant subset parser → a value DOM (mirror the S-JSON DOM
   shape in `json.tks`: a `TomlValue` enum). Feeds #215 config + `.tkp` manifest.
   `pub fn parse(text: str) -> TomlValue | error`. Gate: full ritual.
   Fixtures: tables `[a.b]`, arrays, basic/multiline strings, ints/floats/bools/datetime,
   error on duplicate key. Exit 0 both engines.

5. **multipart/MIME** — `multipart/form-data` boundary split + base64 MIME (reuses base64).
   `pub fn parse_multipart(body: []byte, boundary: str) -> []Part | error`. Gate: full ritual.

**Type shapes:** new DOM enum `TomlValue = enum { Str; Int; Float; Bool; ... }` (named-type
pattern, no inline variant bodies — the no-variant-fork ruling), `FormPair`/`Part` structs.
**Risk:** none major; all pure. TOML datetime values should reuse `teko::time` types where
they land, else carry raw str first (REPORT the coupling, don't block).

---

### 3.4 #194 — crypto core (hash + CSPRNG) — READY once CSPRNG source confirmed

New dir `src/crypto/`: `hash.tks` (SHA-2/SHA-3/BLAKE2/MD5-legacy over `[]byte`),
`rand.tks` (CSPRNG via OS entropy FFI). Hash is 100% pure Teko (no extern); rand needs ONE
extern per OS. `math::checked` (#185, CLOSED) available for the wrapping u32/u64 arithmetic
hashes require.

**Crumb sequence (hash first — fully monomorphic, no FFI):**

1. **SHA-256** (`hash.tks`) — the reference hash; pure `[]byte`, wrapping u32 add/rotate
   (use `teko::math::checked` wrapping mode). `pub fn sha256(data: []byte) -> []byte` (32-byte
   digest). Gate: full ritual. Fixtures: NIST vectors (`""`, `"abc"`, the 448-bit message).
   Exit 0 BOTH engines (pure Teko — VM==native REQUIRED).
2. **SHA-512 / SHA-224 / SHA-384** — 64-bit variant + truncations. Gate: full ritual.
3. **SHA-3 / Keccak** — the permutation is `[]u64` state; distinct code path. Gate: full ritual.
4. **BLAKE2b/2s** + **MD5-legacy** (marked `@deprecated` in Javadoc — legacy only). Gate: full ritual.
5. **rand** (`rand.tks`) — CSPRNG over OS source. ONE extern per OS into `teko_rt` (the
   maintained-C runtime seam IS allowed to grow — it is not a frozen twin):
```teko
/**
 * Fill a byte buffer with cryptographically secure random bytes from the OS CSPRNG
 * (getentropy on macOS/BSD, getrandom on Linux, BCryptGenRandom on Windows).
 *
 * @param n the number of random bytes requested
 * @return exactly `n` secure random bytes, or an error if the OS source is unavailable
 * @throws error when the OS entropy syscall fails (rare: EFAULT/EINTR exhaustion)
 * @since 0.0.1.4
 */
pub extern fn secure_bytes(n: u64) -> []byte = "tk_rt_secure_bytes" from "teko_rt"
```
   This requires a `tk_rt_secure_bytes` in `src/runtime/teko_rt.c` (maintained C, per the
   no-mirroring ruling — this is the runtime seam, NOT a frozen twin; the implementer edits
   `teko_rt.c`/`.h`). Gate: full ritual, native leg authoritative; VM asserts honest-stop.
   Fixtures: `secure_bytes(32).len == 32`, two calls differ (native only); VM = honest-stop.

**Type shapes:** hashes are `[]byte -> []byte`. Runtime seam adds `tk_rt_secure_bytes`.
**Risk/law tension:** MD5 shipped as `@deprecated`-tagged (legacy interop only) — law-clean
(honesty M.3: labelled, not hidden). CSPRNG error model returns `[]byte | error` (errors are
values, B.*). The OS-source selection is `#os`-gated in `teko_rt.c`; getentropy vs getrandom
was ratified in #169. No unresolved tension.

---

### 3.5 #254 — generic methods (the WAVE-G keystone that unblocks #163)

Compiler-core change (NOT a stdlib module). Design ALREADY captured in the issue body (5
layers, crumb-1 fix pre-written+validated+reverted by the #163 implementer). This is a
CROSS-CUTTING compiler change; the architect crumb-plan is posted as a COMMENT on #254 (per
its "Crumb-plan de architect entra como comentário aqui"). Summary of the 5-crumb sequence:

1. **type-param table threading** — `collect.tks::method_func_type` + `typer.tks::type_method`
   must seed the type-param table with the ENCLOSING type's `type_params`, not just the
   method's. (4 call-sites in `collect.tks`, 2 in `typer.tks` — the pre-validated crumb 1.)
   Gate: full ritual.
2. **`resolve.tks::subst_body_names`** — stop dropping methods when stamping a generic
   `StructBody` (currently `methods = empty()`); ADD a `ClassBody` arm. Gate: full ritual.
3. **`monomorph.tks`** — new pass: per-instantiation re-type/rewrite method bodies
   (recursive, incl. self-construction `Box<T>{...}`); emit method-sets per instance in
   BOTH codegen and VM. Gate: full ritual + fixpoint (gen1==gen2) — this is the highest-risk
   crumb (touches codegen AND vm).
4. **typer return-type-as-expected** — thread declared return type as expected-context so
   `fn box_make<T>(v: T) -> Box<T> { Box { value = v } }` infers. Gate: full ritual.
5. **fixtures** — `generics_test.tkt` gains: generic struct WITH method; generic class with
   factory+methods; method that constructs its own type; trait-fold chain. VM==native parity.

**Sequencing law:** #254 shares machinery (monomorph/typer/resolve/collect) with the
now-CLOSED #162; verify no residual conflict on branch base. #254 must MERGE before #163
starts C2+ (List/LinkedList as classes).

---

### 3.6 #163 — collections (BLOCKED on #254) — DESIGN-AHEAD scaffolding

Cannot build methods until #254 merges, BUT everything that does NOT need generic methods is
done now (the "adiantar" mandate):

- **Ratified rulings (design, ready to encode):** `array` NATIVE; Map/List/Set/LinkedList/
  BTree = CLASSES in `teko::collections`; `in` ARRAY-ONLY; Map key `K: Hashable & Eq`
  (S6/#162 — CLOSED, so the constraint IS expressible); NO untyped empty (element type at
  decl); env-as-Map migration is the acceptance driver.
- **Module skeletons that compile TODAY** (honest-stop bodies, full Javadoc) — new files
  `src/collections/{list,map,set}.tks` with the class/interface DECLARATIONS + method
  SIGNATURES + doc-comments, bodies = `panic("not yet implemented: #163 pending #254")` so
  the corpus compiles. When #254 merges, the implementer fills bodies in minutes.
- **`Hashable`/`Eq` trait bindings:** TR3 structural traits (#177/#298, MERGED) synthesize
  Eq/Ord/Hash — the Map key constraint `K: Hashable & Eq` rides those. Confirm the trait
  names match (`Hash` vs `Hashable`) — REPORT if the synthesized trait is `Hash` not
  `Hashable` (naming reconciliation, not a new issue).
- **Fixtures drafted now** (`collections_test.tkt`): `List<i64>` push/get/len; `Map<str,i64>`
  insert/get/contains/remove; `Set<i64>` add/has/union; env-as-Map access. Expected exit 0
  both engines once bodies land; today they honest-stop (assert the stop).

**STILL BLOCKED:** the method BODIES and the env-as-Map cutover (they need generic method
monomorphization from #254). Everything above (skeletons, sigs, traits wiring, fixtures) is
deliverable now.

## 4. Risks + law tensions (cross-cutting)

- **VM==native parity split:** pure-Teko modules (#192, #189, #194-hash, #163) MUST pass on
  BOTH engines. Extern-touching (#186, #194-rand) run native-authoritative + VM honest-stop
  fixtures (the `time.tks` precedent). Sequence extern crumbs so their `.tkt` never asserts
  a value the VM can't produce.
- **Self-build memory:** each new corpus `.tks` grows gen-1; DEFLATE (#192) and hashes
  (#194) allocate the most — use arenas, amortized push, watch auto-reported peak (≤ the
  standing floor). Report regressions, don't absorb.
- **W15 on TOUCHED old files:** `compress.tks` and `math.tks` are pre-W15 (`//` comments).
  Editing them triggers "toque = limpa": new decls full-Javadoc, touched neighbours
  flattened. Do NOT rewrite the whole file — only touched decls.
- **No new issues:** adjacent findings (manifest `m=[]` row need, `Hash` vs `Hashable`
  naming, TOML↔time coupling) are REPORTED to the integrator, never turned into issues.
- **Bootstrap seed:** new modules must not USE a feature absent from the released seed. All
  four roots use only shipped features (extern, closures, `[]byte`, enums) — safe. #163
  bodies use generic methods (#254) — hence the skeleton-first sequencing.

## 5. HALT check
No unresolved law tension. All design questions resolve law-first (libm ratified, CSPRNG
source ratified in #169, MD5 shipped `@deprecated`, collections rulings ratified). The one
genuinely open ITEM (does the extern resolver need a manifest `m=[]` row) is an
implementation probe, not a design tension — resolved by the implementer inspecting the
resolver path, reported up either way. NO HALT.
