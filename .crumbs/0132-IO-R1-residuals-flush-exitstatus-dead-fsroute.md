---
seq: 0132
crumb-id: IO-R1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - "docs/design/io-panic-cluster-expurgo-0.3.1.md:40-52"   # veredito residuais
  - "DECISION_LOG.md:1096-1100"                              # D116 baseline 1044.2 + shift de contabilidade
---

# 0132 · IO-R1 — residuais de I/O: `flush` no-op, `exit_status` inline, dead-routing FS

> `tk_flush_out`→Teko no-op; `tk_exit_status`→máscara `& 0xFF` inline no `main`; remover as branches
> host-FFI mortas `read_file`/`write_file`/`write_file_bytes` (codegen+lir). Byte-mover mínimo.

## Goal

Fecha os três residuais triviais do cluster I/O na rota C. (1) `teko::io::flush` ainda extern
`tk_flush_out` (`fflush(stdout)` do libc) — mas a saída Teko é `SYS_WRITE` DIRETO (sem stdio buffered),
então flush é **no-op** em Teko. (2) O `main` emitido retorna `tk_exit_status((int)(…))` (codegen.tks:434),
que é só `code & 0xFF` — inline a máscara, some a dependência. (3) As branches host-FFI
`read_file`/`write_file`/`write_file_bytes` (codegen.tks:3744-3747) estão MORTAS (os `teko::io::*` são
`user_declared` → `addr2` false), mas devem sair para o F9 poder deletar os corpos C sem referência viva no
codegen. Byte-mover (muda `main` return + remove extern flush); os 12 refs `tk_rt_read_file`/`write_file*`
do seed velho somem no reseed. Alvo: pico flat ≤ 1044,2 MB.

## Where

- `src/io/io.tks:61` `flush` — trocar `exp extern fn flush() = "tk_flush_out" from "teko_rt"` por
  `exp fn flush() { teko::runtime::flush() }` (delega ao no-op Teko).
- `src/runtime/rtio.tks` (novo, junto de `write`) — `exp fn flush()` no-op (saída é `SYS_WRITE` direto,
  nada bufferizado a drenar).
- `src/codegen/codegen.tks:434` `CG_MAIN_RETURN_OPEN` — `"return tk_exit_status((int)("` →
  `"return (int)(("` e fechar com `") & 0xFF)"` no ponto de close correspondente (localizar
  `CG_MAIN_RETURN_*`/onde o open é fechado; a máscara `& 0xFF` substitui a chamada).
- `src/codegen/codegen.tks:3744,3746,3747` — remover as 3 linhas `if l == "read_file"/"write_file"/
  "write_file_bytes" { return emit_host_ffi(…, "tk_rt_read_file"/"tk_rt_write_file"/"tk_rt_write_file_bytes", …) }`.
  MANTER `l == "var"` (getenv), `"chdir"`, `"cwd"`, `"set_var"` (env ainda C, fora deste cluster).
- `src/lir/lower.tks:1516,1522,1523,2093` — remover os ramos native espelho `tk_rt_read_file`/
  `tk_rt_write_file`/`tk_rt_write_file_bytes` (`lower_ffi_*`) e o `if last == "read_file" { return
  "tk_rt_read_file" }` (2093). MANTER getenv/chdir/cwd.

## How

1. `flush` no-op em `rtio.tks`:

```teko
/** teko::runtime::flush — no-op: stdout is written straight to the OS via SYS_write with no buffered
 * stdio layer, so there is nothing to drain. */
exp fn flush() { }
```

   e `src/io/io.tks:61` passa a delegar: `exp fn flush() { teko::runtime::flush() }`.
2. `exit_status` inline: o `main` emitido vira `return (int)((<code>) & 0xFF);` — a máscara portável
   (byte baixo) que `tk_exit_status` fazia (`code & TK_EXIT_STATUS_MASK`, `TK_EXIT_STATUS_MASK==0xFF`).
   Ajustar `CG_MAIN_RETURN_OPEN` e o token de close pareado; verificar que só há UM sítio de uso.
3. Remover as 3 branches FS mortas no codegen e os espelhos no lir. São dead-code (nunca disparam com
   `teko::io::*` user_declared); a remoção é byte-neutra na emissão ATUAL — o scout confirma que gen1 da
   árvore atual NÃO emite `tk_rt_read_file`/`write_file*` (só o seed velho os tem). O reseed purga o seed.
4. NÃO tocar `stream.tks`/`file_stream.tks`/`fs.tks` (já Teko). NÃO tocar env (getenv/chdir/cwd seguem C).

Sem superfície `exp` nova além do `flush` no-op (doc acima). Sem `//`.

## Rulings & laws

- **Lei I/O streaming (CLAUDE.md):** saída do compilador é `SYS_WRITE` direto — flush não drena stdio.
- **D116 (DECISION_LOG:1099):** rotear C→Teko é shift de contabilidade; critério = NÃO-CRESCER (flat).
- **D90:** `teko_rt.c`/`.h` intocados; `tk_flush_out`/`tk_exit_status`/`tk_rt_read_file`/`write_file*` viram
  MORTOS, deletados só no F9.
- **Não-detectar-o-inexistente:** remover branch morta que a superfície não alcança (não reescrever).
- **Teko-only / W15:** `.tks` só; doc apenas no `exp flush`; sem `//`.
- **Fork protocol:** residuais deliberados no design-doc; sem fork aberto.
- **Testes:** o self-build exercita flush (harness) e o `main` return — NENHUM `.tkr` afirmativo novo.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do `bootstrap/teko.c`;
  commit por passo; fixpoint `gen2==gen3` byte-idêntico; reportar pico; reseedar `bootstrap/teko.c` ao fim;
  gen2/gen3 no scratchpad.

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador chama `flush` no harness e todo binário
emitido usa o `main` return mascarado; o fixpoint dirige ambos).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico + pico ≤ 1044,2 MB (flat). "Green" = flush é no-op
Teko, `main` retorna `(code) & 0xFF` sem `tk_exit_status`, codegen/lir sem branches FS mortas, `teko.c`
reproduz e não referencia mais `tk_flush_out`/`tk_exit_status`/`tk_rt_read_file`/`tk_rt_write_file*`.
**Reseed-class:** `fixpoint-rebuild`.

## Deps

`—`

## Done when

`tk_flush_out`/`tk_exit_status`/`tk_rt_read_file`/`tk_rt_write_file`/`tk_rt_write_file_bytes` sem referência
viva na árvore (mortos até F9), pico flat, `[fixpoint]` gen2==gen3, `bootstrap/teko.c` reseedado.
