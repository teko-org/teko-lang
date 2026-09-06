---
seq: 0209
crumb-id: EX-C1
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.C carve-outs
  - "DECISION_LOG.md:1192"                                    # D161 carve = surface-decl
---

# 0209 · EX-C1 — hygiene de carve-out: surface-declarar os primitivos raw-memory/reinterpret

> D161 permite o carve-out SÓ se ele tem "identidade/assinatura de superfície, NÃO name-detect
> escondido". `syscall0-6`, `arena_*`, `__ptr_wrap`/`__ptr_unwrap`/`__ref_word` já cumprem. Mas
> `mem::place`/`read`/`write`, `load_u64`/`store_u64`/`load_u8`/`store_u8`, `ptr_word`/`word_ptr`,
> `buf_ptr`/`byte_ptr`/`region_buf` são name-detect PURO — falta a decl. Este crumb lhes dá a
> decl-intrínseca de superfície (forma `__`-prefixada) OU os funde a `wrap`/`unwrap` onde redutível.

## Goal

Auditar cada raw-memory/reinterpret primitivo e, por primitivo: (a) se IRREDUTÍVEL (raw load/store, view
de ptr) → dar decl-de-superfície `__`-prefixada com assinatura (como `__ptr_wrap`), mantendo o codegen
como emissão-de-primitiva-declarada (não name-detect anônimo); (b) se REDUTÍVEL a `wrap`/`unwrap` (D131)
→ NÃO fazer aqui, marcar para o **W6** (reball str↔[]byte inclui `as_ptr`/`bytes_of_str`/`str_of_bytes`).
Behavior-preserving (a emissão do primitivo não muda; muda a IDENTIDADE de superfície). `str↔[]byte`
(`bytes_of_str`/`str_of_bytes`/`as_ptr`) fica FORA (W6).

## Where

- `src/codegen/codegen.tks:3645-3649` (`emit_place/read/write`, guard `cg_is_mem_value_call`), `3726-3739`
  (`buf_ptr`/`byte_ptr`/`region_buf`/`ptr_word`/`word_ptr`/`load_u64`/`store_u64`/`load_u8`/`store_u8`).
- `src/lir/lower.tks:1267-1271` (`is_buf_ptr_call`/`is_load_u64_call`/`is_store_u64_call`/`is_load_u8`/
  `is_store_u8`), `2133` (`is_mem_value_call`).
- NOVAS decls de superfície `__`-intrínsecas (onde a base define o primitivo) — no prelúdio de runtime.

## How

1. Classificar cada primitivo IRREDUTÍVEL vs REDUTÍVEL-a-wrap/unwrap.
2. Para os irredutíveis: escrever a decl-de-superfície `__`-prefixada (assinatura de tipo; corpo =
   intrínseco reconhecido pela decl, não pelo nome-nu escondido). Documentar como carve-out oficial.
3. Para os redutíveis (`as_ptr`, str↔bytes): NÃO tocar — anotar dep W6.
4. Fixpoint (emissão idêntica; muda só a identidade/decl) + ASan + reseed.

## Rulings & laws

- **Teko-only. D161 (carve = surface-decl). D131 (wrap/unwrap; str↔bytes → W6). D148.**
- **Fork:** se um primitivo não for claramente irredutível NEM redutível a wrap/unwrap, HALT curto (é
  decisão de design de fronteira ptr/mem — surfar ao dono).
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o backend usa load/store/ptr-view ao emitir).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. "Green" = todo raw-memory primitivo tem decl-de-superfície
(ou está marcado W6), zero name-detect anônimo restante para os irredutíveis. Reseed-class:
`fixpoint-rebuild`.

## Deps

`EX-A0`

## Done when

`place/read/write/load_*/store_*/ptr_word/word_ptr/buf_ptr/byte_ptr/region_buf` têm decl-de-superfície
`__`-intrínseca (ou marcados W6) e `gen2==gen3`.
</content>
