---
seq: 0134
crumb-id: IO-R3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - "docs/design/io-panic-cluster-expurgo-0.3.1.md:38-43"    # grupo A panic (hot) + riscos
  - "src/runtime/rtio.tks:68-94"                              # teko::runtime::panic/exit já vivos (sink)
  - "DECISION_LOG.md:1096-1100"                               # D116 baseline 1044.2 + flat
---

# 0134 · IO-R3 — família panic/trap emitida → `teko::runtime` (reusa o sink `panic`)

> Os traps inline do codegen/lir (`tk_panic_oob_at` [6155], `tk_panic_null_deref_at`, `tk_panic_cast`,
> `tk_panic_div0`, `tk_panic_overflow`, `tk_panic_oob`, `tk_panic`, `tk_nn`) → fns Teko em `teko::runtime`
> reusando o sink `panic()` JÁ vivo. Byte-idêntico no stderr. HOT mas branch-em-falha → pico flat.

## Goal

`teko::runtime::panic(str)`/`exit(i64)` JÁ são o caminho vivo do panic-keyword (rtio.tks:72-94: capture →
`capture_deliver`; senão `ewrite`+`rt_exit(134)`). Os TRAPS (bounds/null/cast/div0/overflow) ainda são
símbolos C `tk_panic_*` que o codegen EMITE inline em ternário/comma-expr. Este crumb adiciona as fns Teko
irmãs em `teko::runtime` que reproduzem os corpos C byte-a-byte e reusam `panic()` como sink, e TROCA os
símbolos emitidos (codegen rota C validada + espelho lir native). O guard só DISPARA em falha → nunca executa
no build seco → o único delta é o **símbolo emitido**; pico flat. Descarta os globais `_tk_cast_loc_line/col`
passando `(line,col)` a `panic_cast` (padrão do `oob_at`). Anchor do F9 (maior bloco C do runtime a morrer).

## Where

- `src/runtime/rtio.tks` (novo, junto de `panic`) — as fns trap Teko (`exp`, sink = `panic`):
  `panic_oob_at(line: u32, col: u32)`, `panic_null_deref_at(line: u32, col: u32)`,
  `panic_cast(line: u32, col: u32)`, `panic_div0()`, `panic_overflow()`, `panic_oob()`. O prefixo de
  posição usa `rtio_write_all(2, …)` CRU (bypass do canal — igual ao `fputs(stderr)` do C).
- `src/codegen/codegen.tks:2821-2823,2846-2848` — REMOVER a emissão dos setters `_tk_cast_loc_line/col`
  (globais mortos); a posição passa a ir por argumento em `panic_cast`.
- `src/codegen/codegen.tks:3835-3838` — `panic_div0`/`panic_oob`/`panic_cast`/`panic_overflow` builtins:
  `builtin = tk_panic_*` → `builtin = cb_fn_name_str("teko::runtime", "panic_div0"/"panic_oob"/
  "panic_cast"/"panic_overflow")`. (`panic_cast` agora recebe `(line,col)` — casar aridade com os args
  que os setters injetavam.)
- `src/codegen/codegen.tks:4069,4100,7092` — `tk_panic_oob_at(` → `cb_fn_name_str("teko::runtime",
  "panic_oob_at")` (mesmos args `line,col`).
- `src/codegen/codegen.tks:3958` `tk_nn` — substituir o wrapper por ternário inline de null-check:
  `(p ? p : (teko::runtime::panic_null_deref_at(l,c), p))` mangled; DROP `tk_nn`.
- `src/codegen/codegen.tks:3410` (str slice OOB) e `:7539` (OOM do bloco) — `tk_panic("…")` →
  `cb_fn_name_str("teko::runtime","panic")` com o literal str.
- `src/lir/lower.tks:268,347,6000` (espelho native, escrito-não-rodado) — trocar
  `"tk_panic_div0"`/`"tk_panic_cast"`/`"tk_panic_oob_at"` pelos símbolos Teko-mangled correspondentes.

## How

1. As fns trap em `rtio.tks` reproduzem os corpos C (teko_rt.c:3280-3312) byte-idênticos:

```teko
/**
 * teko::runtime::panic_oob_at — the positioned bounds-check trap: write "L:C: " raw to stderr
 * (out-of-band of any test channel, matching the C fputs), then panic with "index out of bounds"
 * through the shared panic sink (marker + capture handling).
 *
 * @param line  the source line of the indexing site
 * @param col   the source column of the indexing site
 * @since 0.3.1
 */
exp fn panic_oob_at(line: u32, col: u32) {
    rtio_write_all(2, teko::str::concat(teko::str::u64_to_str(line to u64), ":",
        teko::str::u64_to_str(col to u64), ": "))
    panic("index out of bounds")
}
```

   `panic_null_deref_at` idem com `"null reference dereference"`; `panic_cast(line,col)` idem com
   `"impossible conversion"`; `panic_div0()` = `panic("division by zero")`; `panic_overflow()` =
   `panic("integer overflow")`; `panic_oob()` = `panic("index out of bounds")`.
2. O prefixo `"L:C: "` vai por `rtio_write_all(2, …)` CRU — NÃO `ewrite` (que rotearia ao canal sob capture).
   Isto casa o C: posição via `fputs(stderr)` fora do canal, marker+msg pelo caminho de capture. Bytes
   idênticos nos dois modos. (`rtio_write_all` é privada do mesmo módulo — acessível.)
3. codegen: trocar os símbolos emitidos (Where). `panic_cast` passa a receber `(line,col)` — remover os
   setters `_tk_cast_loc_*` (2821/2846) e injetar `line,col` como args na chamada (o codegen já conhece a
   posição do cast ali). `tk_nn` → ternário inline (evita helper; um deref nulo é raro/frio).
4. lir espelho (native, não roda): apontar para os mesmos símbolos Teko para a perna native, quando buildar,
   chamar Teko também (lei "escreve as DUAS direções").
5. **Byte-check obrigatório:** o marker é `"teko: deliberate panic: "` (rtio `PANIC_MARKER` == C
   `TK_PANIC_MARKER`); `u64_to_str` é Teko (D116). Ordem final no stderr (não-capture):
   `"L:C: teko: deliberate panic: index out of bounds\n"` — idêntico ao C (posição ANTES do marker).
6. **Backtrace:** os traps deixam de emitir `tk_backtrace()` (C-frozen, sai no F9), alinhando ao
   panic-keyword que JÁ não emite. Scout confirma que nenhum oráculo casa stderr de backtrace de
   oob/div0/cast em `examples/regressions/`.

## Rulings & laws

- **D116 (DECISION_LOG:1099):** shift de contabilidade; trap é branch-em-falha → nunca roda no build seco →
  pico flat (só troca de símbolo). Critério = NÃO-CRESCER.
- **D90:** `tk_panic*`/`tk_nn`/`_tk_cast_loc_*` viram MORTOS (referenciados só pelo `assert.c` frozen, se
  algum) → deletados no F9; `teko_rt.c` intocado.
- **Byte-preservação (design-doc §riscos):** prefixo de posição por `rtio_write_all(2,…)` cru; marker/capture
  pelo sink `panic()`; `rt_exit(134)` = mesmo exit code que `abort()` (divergência já ratificada pelo
  panic-keyword).
- **Não-detectar-o-inexistente:** trocar emissão de construção real (guards que a superfície produz), não
  ramo impossível.
- **Teko-only / W15:** `.tks` só; doc só nos `exp`; sem `//`. **Fork protocol:** riscos resolvidos por
  precedente (panic-keyword) — sem fork aberto.
- **Testes:** o self-build/fixpoint exercita os guards (o codegen os emite em massa; gen1 executa em larga
  escala) → NENHUM `.tkr` afirmativo novo. Casos de erro: os traps são o caminho de erro; já cobertos por
  oráculos existentes de panic (não criar novos aqui, salvo o scout achar um path não-coberto — então
  nomear).
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do `bootstrap/teko.c`;
  commit por passo; **sweep `.tkt`/`.tkr`** (mudou a assinatura de `panic_cast` no codegen); fixpoint
  `gen2==gen3` byte-idêntico; **reportar pico** (crumb quente); reseedar `bootstrap/teko.c` ao fim;
  gen2/gen3 no scratchpad.

## Fixtures

`none — the fixpoint self-build exercises this` (o codegen emite os traps em toda indexação/cast/div do
`src/` e o fixpoint executa gen1 em larga escala; os oráculos de panic existentes em `examples/regressions/`
já casam o marker+razão — o scout verifica que continuam verdes com a nova emissão, sem adicionar fixture).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico + **pico ≤ 1044,2 MB (flat)** + oráculos de panic
existentes verdes (marker+razão+posição byte-idênticos). "Green" = todos os traps emitidos chamam
`teko::runtime::panic_*`, `_tk_cast_loc_*`/`tk_nn` sumiram da emissão, stderr byte-idêntico, `teko.c`
reproduz. **Reseed-class:** `fixpoint-rebuild`.

## Deps

`—` (independente de 0132/0133; pode ordenar por último por ser o quente/maior blast-radius).

## Done when

Família trap emitida aponta 100% para `teko::runtime::panic_*` (rota C + espelho lir), globais
`_tk_cast_loc_*` e helper `tk_nn` eliminados da emissão, stderr byte-idêntico, pico flat, `[fixpoint]`
gen2==gen3, `.tkt`/`.tkr` varridos, `bootstrap/teko.c` reseedado. Os corpos C `tk_panic*` ficam mortos até F9.
