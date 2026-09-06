---
seq: 0210
crumb-id: EX-T1
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [EX-A1, EX-A2, EX-A3, EX-A4, EX-A5, EX-A6, EX-B1, EX-B2, EX-B3, EX-B4, EX-B5, EX-B6, EX-B7, EX-B8, EX-B9, EX-C1]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:4"      # §4 sequência + §7 gate
---

# 0210 · EX-T1 — terminal: remover o shim `builtin_fn` + reseed final + gate ASan+UBSan

> Fecho da onda. Quando A, B e C removeram todos os name-detects, o shim `builtin_fn`
> (`src/checker/scope.tks:471`) que sintetizava assinaturas sem-namespace fica sem consumidores → remove-se
> inteiro; e o `panic("codegen: no resolved namespace")` (codegen 3849) passa a cobrir só erro-interno
> legítimo (nenhum builtin resolve por ausência de namespace). Reseed terminal com prova ASan.

## Goal

Provar que o expurgo está completo: (1) o shim `builtin_fn` é removido (ou reduzido só aos carve-outs que
GENUINAMENTE não são superfície — `syscall`/`__ptr_*`/`arena_*`); (2) nenhuma fn "existe só na lógica do
pipeline" (D161 satisfeito para o conjunto A+B+C desta onda); (3) fixpoint + ASan+UBSan verde do seed
NOVO. Reseed-class `expurgo` (remoção de superfície interna → o `teko.c` muda).

## Where

- `src/checker/scope.tks:471` — `builtin_fn(name)` — remover as entradas já sem consumidor (as de grupo A
  removidas em EX-A*; as de grupo B removidas em EX-B*); manter SÓ o que é carve-out real.
- `src/checker/scope.tks:375,399,422,555` — `assert_builtin_fn`/callers — auditar se ainda necessários.
- `src/codegen/codegen.tks:3849` — `panic("codegen: no resolved namespace")` — confirmar que só dispara em
  erro-interno (todo builtin agora resolve por `call_ns`).
- `src/checker/typer.tks:2049` — `match builtin_fn(name)` — simplificar o fallback.

## How

1. Confirmar todos os EX-A*/EX-B*/EX-C1 landados (deps).
2. Remover as entradas mortas de `builtin_fn` + auditar `assert_builtin_fn`.
3. Confirmar `panic(3849)` inalcançável por builtin (só erro-interno).
4. **Ladder completa do seed NOVO** (D164): gen0 do `bootstrap/teko.c` commitado builda o tip; fixpoint
   `gen2==gen3` byte-idêntico; **build ASan+UBSan limpo** do gen0 compilando o tip.
5. Reseed `bootstrap/teko.c`; deixar gen2/gen3 no scratchpad.

## Rulings & laws

- **Teko-only. D161 (fecho: nenhuma fn só-no-pipeline). D164 (prova ladder do seed NOVO). D166 (gate
  ASan+UBSan). D148.**
- **Removals = clean expurgo, SEM tombstone** (o shim some; nenhuma mensagem cita builtin removido).
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan; ratchet D68 (pico não-cresce vs HEAD
  pré-onda); reseed incondicional (D164/RESEED-INCONDICIONAL).

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` — ladder native completa (rota-C valida/reseeda; native compila) + reseed genuíno +
`gen2==gen3` + ASan+UBSan limpo. "Green" = shim `builtin_fn` reduzido aos carve-outs, D161 satisfeito para
A+B+C, seed NOVO prova a ladder. Reseed-class: `expurgo`.

## Deps

`EX-A1..A6`, `EX-B1..B9`, `EX-C1` (toda a onda).

## Done when

`builtin_fn` não tem entrada para nenhuma fn que agora é superfície resolvível; ladder do seed NOVO fecha
`gen2==gen3` + ASan+UBSan limpo; `bootstrap/teko.c` reseedado.
</content>
