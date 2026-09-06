---
seq: 0060
crumb-id: RT-L2
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L1]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:62-64"      # §1.2 str/bytes construction + UTF-8 char families
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L2 = UTF-8 char + rotatable str-construction over L1
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:172-178"    # §2.4 leaves first — L2 pure over []byte, independent of L3+
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:244-250"    # §4.1 str-producers route by _r (residence)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:317"        # §5 F3 — port UTF-8 char (needs str→char cast)
---

# 0060 · RT-L2 — runtime C→Teko L2: UTF-8 char + rotatable str-construction (pure over L1)

> Close the L2 layer: UTF-8 decode/validate + the rotatable str-construction family run in pure Teko over the
> L1 arena — computation over `[]byte`, touching no OS state.

## Goal

L2 is **pure computation over `[]byte`** sitting only on L1 (the arena, `0059`): UTF-8 char handling and the
rotatable str-construction family. Two clusters still live only in `teko_rt.c` (`migracao…` §1.2): (a) the
**UTF-8 char** family — `tk_char_to_u32`/`tk_str_len_chars`/`tk_str_chars`/`tk_char_at`/`tk_str_slice_chars`
(`_len`)/`tk_is_alpha`/`tk_is_digit`/`tk_is_space`/`tk_to_lower`/`tk_to_upper` + `rt_valid_utf8`/
`tk_rt_str_from_utf8`(`_ok`) (`teko_rt.c:287-1041`) — RFC 3629 decode/validate; and (b) the **str/bytes
construction** family — `tk_str_concat`/`_concat_r`/`_of_bytes`/`tk_one_byte`/`tk_u64_to_str`/`tk_i64_to_str`/
`tk_bytes_of_str`/`tk_cstr_dup`/`tk_str_from_cstr`/`tk_as_ptr`/`tk_bytes_from_ptr` (`teko_rt.c:143-370`), which
already have an L0 twin — the C is only the dying emit path. This crumb migrates the UTF-8 char decode/validate
to Teko and moves str-construction fully onto the **rotatable `_r`** form (bumping into the current region, not
root), closing the residence seam (`migracao…` §4.1). The one compiler prerequisite the UTF-8 `chars` path
needs is the `str→char` cast (today undefined, `migracao…` §1.2) — this crumb states the decision to realise it
as an L2 decode primitive over `[]byte`, not a language-level pun. Byte-preserving for existing programs
(fixpoint guards existing-case residence; `migracao…` R8); a `fixpoint-rebuild` swap, no teaching reseed.

**BLOCKED (design-ahead, honest).** Behind the **native fixpoint closing** (`migracao…` banner) AND its dep
**RT-L1** (the arena the str/char boxing bumps through). This is the design; the implementer resumes in
minutes when both close. L2 is a leaf: it closes independently of L3+ (fs/process) — "adiantar o que der"
(`migracao…` §2.4), the whole L2 can land while the Win32 process block (L4) is still C.

## Where

- `src/runtime/teko_rt.c:287-1041` — the UTF-8 char family (`tk_char_to_u32`/`tk_str_len_chars`/`tk_str_chars`/
  `tk_char_at`/`tk_str_slice_chars`(`_len`)/`tk_is_alpha`/`tk_is_digit`/`tk_is_space`/`tk_to_lower`/
  `tk_to_upper`, `rt_valid_utf8`/`tk_rt_str_from_utf8`(`_ok`)) — MIGRATE to Teko bodies decoding RFC 3629 over
  `[]byte`; the C bodies go DEAD (deleted at M3, not here).
- `src/runtime/teko_rt.tks:115-184` — `str_concat`/`str_of_bytes`/`one_byte`/`u64_to_str`/`i64_to_str`/`concat`
  (already Teko) — confirm they route through the rotatable `_r` region form, not root.
- `src/runtime/teko_rt.tks:496-594` — the str-query family (`str_slice*`/`str_len`/`str_ends_with`/
  `str_contains`) — confirm no residual C on the slice-construction path.
- `src/checker/scope.tks` — the `str→char` decode primitive (an L2 builtin over `[]byte`, NOT a pointer pun) —
  add its builtin signature + lower arm if not already present; the decision this crumb makes (see How §3).
- NO new PUBLIC user surface beyond the `char` decode primitive; the str-construction/char names pre-exist.

## How

1. **Migrate UTF-8 decode/validate to Teko** over `[]byte`: `char_to_u32` decodes a 1-4 byte sequence per RFC
   3629; `str_len_chars`/`char_at`/`str_slice_chars` walk code-point boundaries; `is_alpha`/`is_digit`/
   `is_space`/`to_lower`/`to_upper` classify/map. `valid_utf8`/`str_from_utf8`(`_ok`) reject overlong /
   surrogate / out-of-range sequences (RFC 3629). All pure `[]byte`, allocating boxed results through L1.
2. **Move str-construction onto the rotatable `_r` form** (`migracao…` §4.1): `str_concat`/`one_byte`/
   `u64_to_str` bump their fresh buffer into the caller's CURRENT region and are MOVED to the caller on return
   — a runtime str never leaks to root. The non-`_r` (root) form remains only for program-residence strings.
3. **DECISION — `str→char` is an L2 decode primitive, not a pun** (`migracao…` §1.2 open gap). A `char` is a
   `u32` code point; `str_chars` decodes a `str`'s bytes into a `[]char`. The cast `str→char` at index `i` is
   `char_at(s, i)` (decode the code point starting at byte boundary `i`), lowering over `[]byte` with the L1
   seam — NOT a reinterpret of the fat pointer. This keeps the char family pure computation, ruling-consistent
   with M.3 (no ABI pun; cf. the rejected `char*` pun in `migracao…` R6).
4. **The link is the normal program link** (`migracao…` §2.2): L2 bodies are `exp fn` Teko compiled into the
   program's object; no separate runtime object.
5. **Fixpoint byte-identity is the detector.** `gen2==gen3` byte-identical proves existing str/char callers did
   not shift residence; a shift means a residence changed unintentionally — stop and re-examine (`migracao…`
   R8).

The W15 signature the implementer copies verbatim for the char decode primitive:

```teko
/**
 * char_at — decode the Unicode code point beginning at byte boundary `i` of `s` (RFC 3629). Pure computation
 * over `s`'s bytes with the L1 seam — it is NOT a reinterpret of the fat `str` pointer (that pun is rejected,
 * `migracao…` R6 / M.3). This is the L2 realisation of the `str→char` cast the UTF-8 `chars` path needs.
 *
 * @param s  the UTF-8 string
 * @param i  the byte offset of a code-point boundary (`i < s.len`)
 * @return   the decoded code point as a `char`, or an error on a malformed/mid-sequence boundary
 * @throws   when the bytes at `i` are not a valid UTF-8 code-point start (RFC 3629)
 * @since 0.3.1
 */
pub fn char_at(s: str, i: u64): char | error
```

Reused (do NOT redeclare): `region_alloc`/`region_enter`/`region_leave` (L1), `str_concat_r` (the rotatable
form, `modelo…` §9), the `ResidencePlan` the char/str boxing consumes as any user code (`migracao…` §4.4).

## Rulings & laws

- **Teko-only:** L2 bodies land in `src/runtime/teko_rt.tks` (+ the char builtin in `scope.tks`); the
  maintained-C exception is the BRIDGE the campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` UTF-8/str C
  goes DEAD; deletion is `0095` RM-C9 (M3).
- **W15 full Javadoc** on every touched declaration; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** removes no surface (char/str names persist); kills the C bodies'
  role. Physical deletion is `0095` RM-C9, clean and tombstone-free.
- **No ABI pun (M.3, `migracao…` R6):** `str→char` is a decode over `[]byte`, never a reinterpret of the fat
  pointer — the same rule that rejects `tk_str→char*`.
- **Residence law (`modelo…` §4/§9, `migracao…` §4.1):** str/char producers route by the current region via
  `_r`, NEVER root; a runtime str is MOVED to the caller on return.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the char-builtin signature is added.

## Fixtures

L2 str-construction is core-consumed, so its happy path is fixpoint-covered; the UTF-8 decode/validate edges
the self-build may not stress get isolated oracles:

| fixture | asserts | expected |
|---|---|---|
| `l2_utf8_decode_roundtrip` | a mixed ASCII/2-3-4-byte string decodes to code points and re-encodes byte-identical | `0` |
| `l2_utf8_reject_overlong` | an overlong / surrogate / out-of-range sequence is REJECTED by `valid_utf8` (RFC 3629) | `0` |
| `l2_char_at_boundary` | `char_at(s, i)` at a mid-sequence byte returns error, at a boundary returns the code point | `0` |
| `l2_str_concat_residence` | a `str` built by `str_concat` in a runtime scope dies with the scope (rotatable `_r`, not root) | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity. "Green" = UTF-8 decode/validate
and str-construction run pure-Teko over L1, `char_at` realises the `str→char` decode (no pun), residence is
honored, no `tk_char_*`/`tk_str_*` C symbol is on the path, and the emitted `teko.c` is byte-identical to
before the swap. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing; no reseed harvested).

## Deps

`RT-L1` (`0059` — the arena the L2 str/char boxing bumps through).

## Done when

UTF-8 char decode/validate + rotatable str-construction run pure-Teko over L1 honoring residence, `char_at`
provides the `str→char` decode with no ABI pun, no C char/str symbol is on the path, the fixtures exit `0`, and
a `[fixpoint]` build is `gen2==gen3` byte-identical.
