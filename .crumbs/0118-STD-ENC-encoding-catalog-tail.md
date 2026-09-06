---
seq: 0118
crumb-id: STD-ENC
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/plano-stdlib-catalogo-expansao.md:400-484"        # area D encoding/serialization
  - "docs/design/mudancas-superficie-0.3.1.md:1271-1285"          # owner sequence: encoding tail before §16
---

# 0118 · STD-ENC — encoding/serialization catalog tail (csv · toml · ini · yaml · protobuf · asn1 · fixed)

> Anchor the REMAINING pure-Teko encoding formats into the master sequence. The owner's Doc-2 stdlib
> sequence (Doc-2:1281) shipped json/xml/cbor/bson/msgpack/base64/url/mime already; the tail
> (csv·toml·ini·yaml·protobuf·asn1·fixed) is Doc-2 scope but is NOT in the 112-crumb plan.

## Goal

`estado-doc2` and Doc-2:1271-1285 place the WHOLE §1.5 stdlib before Doc-1; the master plan merged
collections/io/crypto-adjacent but NOT the `teko::encoding` catalog tail. These are **pure-Teko leaves
over `[]byte`** (no new language feature, catalog §7 safety confirmation) — each a `[dry]` module with
NO reseed (leaf; the compiler does not consume them). This crumb is a CLUSTER ANCHOR: the fine-grained
per-format recipe lives in `plano-stdlib-catalogo-expansao.md` area D; this crumb fixes their place in
the ordered plan and their `exp` posture.

## Where

- `src/encoding/` — new leaf modules: `csv.tks`, `toml.tks`, `ini.tks`, `yaml.tks`, `protobuf.tks`,
  `asn1.tks`, `fixed.tks` (fixed-width records). Each `exp` (consumable surface); internal helpers
  `pub`. No compiler machinery touched.

## How

Per the catalog area D (`plano-stdlib-catalogo-expansao.md:400-484`), each format is a parse/emit pair
over `[]byte`, composing the existing `teko::io`/`teko::iter` seams. Author each as an independent leaf;
sequence per-unit so each uses only seeded features (catalog §5 note 11 — no reseed hazard). ASN.1/
protobuf ride the existing `teko::numeric`/`bigint` for varints.

```teko
/**
 * decode — parse a CSV document from a byte slice into rows of fields, honouring RFC 4180 quoting.
 *
 * @param bytes  the CSV source
 * @param sep    the field separator (`,` default)
 * @return       the parsed rows, or an error at the first malformed record
 * @throws       when a quote is unterminated or a record is malformed
 * @since 0.3.1
 */
exp fn decode(bytes: []byte, sep: byte): [][]str | error
```

## Rulings & laws

- **Teko-only:** `.tks` leaves; no compiler change.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Visibility (owner):** stdlib default `exp`; internal helpers `pub` (estado-doc2 visibility rule).
- **Fork protocol:** no undecided fork — pure leaves over `[]byte`.
- **W15 full Javadoc** on every `exp` decl; flatten; no `//`.
- **No reseed hazard (catalog §5 n.11):** adds NO language feature; sequence per-unit on seeded features.
- **Safety:** NEVER `teko test .`; `[dry]` — compile + scoped `.tkr` + trivial fixpoint; validate a
  compiled binary behaviourally (not `teko test .`).
- Rests on: catalog area D + Doc-2:1281.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `enc_csv_roundtrip` | CSV encode→decode roundtrips with quoted fields | `0` |
| `enc_toml_parse` | a representative TOML doc parses to the expected tree | `0` |
| `enc_protobuf_varint` | protobuf varint encode/decode matches the reference vector | `0` |

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (leaf; no emitted-byte change to the compiler).
"Green" = each format module compiles, its roundtrip fixtures pass. Reseed-class: `none`.

## Deps

`—` (pure leaves). May be authored in parallel with other M2 `[dry]` leaves.

## Done when

`teko::encoding` covers csv/toml/ini/yaml/protobuf/asn1/fixed as `exp` leaves with passing roundtrip
fixtures, no compiler change, no reseed.
