---
seq: 0194
crumb-id: EX-A1
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.A A01–A07
---

# 0194 · EX-A1 — remover name-detect da família I/O void + `exit`

> Primeiro degrau de remoção real. `write`/`print`/`println`/`ewrite`/`eprint`/`eprintln`/`exit` já têm
> corpo de superfície (`rtio.tks:40/47/51/55/62/66/90`); com `call_ns` já setado (EX-A0), remove-se o
> name-detect e a emissão flui genérica ao MESMO símbolo. Não-alocante (void/noreturn) → abaixo-da-linha
> → sem região → independe do W4.

## Goal

Remover os ramos `if last=="print"…"exit"` de `emit_call_inner` (codegen 3756–3763, 3759) e o espelho de
`native_builtin_symbol` (lower `builtin_io_symbol` 1995). A chamada passa a resolver via `call_ns`
(EX-A0) e emitir `cb_fn_name(buf,"teko::runtime",X,suffix)` — que DEVE casar byte-a-byte com o
`cb_fn_name_str("teko::runtime",X)` do desvio. Byte-mover mínimo (só o site de emissão do call; símbolo
idêntico) → o fixpoint prova; se o `overload_suffix` diferir, muda bytes → reseed do degrau (esperado).

## Where

- `src/codegen/codegen.tks:3756-3763` — ramos `print/println/panic?/exit/write/ewrite/eprint/eprintln`
  em `emit_call_inner` — **remover** os de A01–A07 (`write/print/println/ewrite/eprint/eprintln/exit`).
  `panic` fica no EX-A2.
- `src/lir/lower.tks:1995` — `builtin_io_symbol(last)` — remover as entradas correspondentes; deixar a
  resolução por `call_symbol`/`call_ns` cuidar.
- NÃO tocar `rtio.tks` (corpo permanece).

## How

1. Remover cada `else if last=="X" { builtin=cb_fn_name_str("teko::runtime","X"); has_builtin=true }`
   para X ∈ {print,println,write,ewrite,eprint,eprintln,exit}.
2. Remover o par em `builtin_io_symbol` (lower.tks:1995) para os mesmos nomes.
3. Confirmar que o ramo genérico (`c.call_ns.len != 0` → `cb_fn_name`) emite o mesmo símbolo. Se o
   `overload_suffix` do call resolvido diferir da forma sem-suffix do desvio, é a fonte legítima de
   mudança de bytes → aceitar + reseed.
4. Verificar (Risco-2) que o oráculo `residence.tks`/`cg_fn_is_below_line` mantém esses void/noreturn
   below-line (NÃO injeta região).
5. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only.**
- **D161:** fn tem corpo, chamada genérica.
- **D159 abaixo-da-linha:** void/noreturn não recebe região; verificar preservação.
- **D148 espelho native:** remover das DUAS rotas no mesmo crumb.
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166);
  ratchet D68; reseed ao fim (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o compiler-core imprime/escreve/aborta ao rodar).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` + ASan+UBSan limpo. "Green" = zero name-detect de I/O-void nas
duas rotas e a emissão casa (byte-idêntica, ou reseed capturado se o suffix mudou). Reseed-class:
`fixpoint-rebuild`.

## Deps

`EX-A0`

## Done when

Nenhum `if last=="print|println|write|ewrite|eprint|eprintln|exit"` em codegen/lower e `gen2==gen3`.
</content>
