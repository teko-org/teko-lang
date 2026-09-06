---
seq: 0133
crumb-id: IO-R2
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - "docs/design/io-panic-cluster-expurgo-0.3.1.md:44-52"   # grupo B stdin (frio)
  - "DECISION_LOG.md:1047-1048"                              # D109: frio no build seco → escreve agora, valida pós-F9
---

# 0133 · IO-R2 — stdin em Teko sobre syscall (`read_line`/`stdin_eof`/`read_stdin`/`read_stdin_n`)

> `tk_rt_read_line`/`tk_rt_stdin_eof`/`tk_rt_read_stdin`/`tk_rt_read_stdin_n` → Teko sobre `SYS_READ(fd 0)`
> (Linux) / `abi::os_read_raw` (mac) / `ReadFile` (win). Flag EOF em slot de controle. Sem `push`.

## Goal

Migra o lado de ENTRADA de stdin, hoje em C via `fgetc`/`fread` + `tk_byte_list_push` (crescimento) +
global `tk_rt_stdin_eof_flag`. É FRIO no build seco (o compilador nunca lê stdin ao compilar) → escreve-se
AGORA e valida-se pós-F9 (D109/D105). Desenho: um leitor bufferizado ≤1024 B sobre fd 0, residente num slot
de controle (como os `HARNESS_*` de rtio.tks) para carregar o buffer/posição e o flag EOF entre chamadas.
`read_line` lê byte-a-byte do buffer (recarrega ao esvaziar), para em `\n` (consome, não guarda), tira `\r`
final; `read_stdin` drena até EOF acumulando por **lista-de-chunks + cópia-exata** (idioma sancionado, sem
`push`); `read_stdin_n` lê exatamente `n` (ou menos no EOF). Autocontido, sem acoplar harness. Pico flat.

## Where

- `src/io/io.tks:12,15,18` — `read_line`/`stdin_eof`/`read_stdin` deixam de ser `extern … from "teko_rt"`
  e passam a `exp fn … { teko::runtime::stdin_* (…) }` delegando à impl Teko.
- `src/lsp/jsonrpc.tks:3` — `read_stdin_n` idem (`extern … "tk_rt_read_stdin_n"` → delega a
  `teko::runtime::read_stdin_n`).
- `src/runtime/rtio.tks` (novo bloco stdin, junto de `rtio_os_write`) — os `#os` de leitura + o estado:
  - `#os("linux") fn rtio_os_read(fd,addr,count): i64` = `syscall3(SYS_READ, …)`.
  - `#os("macos")` = `abi::os_read_raw`; `#os("windows")` = `ReadFile` sobre `GetStdHandle(-10)` (STDIN).
  - slots de controle novos (após `HARNESS_CAP_CODE`, seguir o esquema de offsets 8-em-8):
    `STDIN_BUF_PTR`, `STDIN_BUF_LEN`, `STDIN_BUF_POS`, `STDIN_EOF_FLAG` — buffer ≤1024 residente na
    `region_program` (aloca uma vez via `region_alloc`, como `chan_alloc`).
  - `stdin_refill(): bool` (lê ≤1024 no buffer, seta pos=0/len=n, EOF quando n==0), `stdin_next_byte():
    i64` (−1 no EOF), e as 4 `exp fn`.

## How

1. `rtio_os_read` por SO (espelha `rtio_os_write`). O buffer de 1024 B vem de `region_alloc(region_program(),
   1024)` guardado em `STDIN_BUF_PTR` (aloca no 1º uso; reusável — sem crescer).
2. `stdin_next_byte`: se `pos >= len` → `stdin_refill`; se refill deu 0 → EOF (`return -1`); senão devolve o
   byte e `pos++`.
3. `read_line`: acumula bytes até `\n` (consumido) ou EOF; seta `STDIN_EOF_FLAG` = (zero bytes antes do EOF);
   tira `\r` final. Tamanho desconhecido → **lista-de-chunks** (nós ≤1024 na `region_program`, como
   `chan_append`) + `[total]byte` exato + cópia por índice. NUNCA `push`/`[..x,b]`.

```teko
/**
 * teko::runtime::read_line — one line from stdin (the trailing '\n' consumed and dropped, a trailing
 * '\r' from a "\r\n" source stripped); "" at a genuine blank line or at EOF. Sets the EOF flag (read
 * by stdin_eof) when zero bytes were seen before end of input, so a caller can tell EOF from a blank
 * line.
 *
 * @return the line's bytes as text, or "" at a blank line / EOF
 * @since 0.3.1
 */
exp fn read_line(): str
```

4. `read_stdin`: drena `stdin_next_byte` até EOF pela mesma lista-de-chunks-+-cópia-exata; NÃO dobra buffer
   (o C dobrava `malloc` — proibido aqui). `read_stdin_n(n)`: `[n]byte` exato, preenche por `stdin_next_byte`
   até `n` ou EOF, devolve o slice do que leu (`slice[0..got]`).
5. `stdin_eof(): bool` = `ctrl_get(STDIN_EOF_FLAG) != 0`.
6. NÃO tocar `tk_rt_fd_*` (process, Windows-interativo) — outro cluster.

## Rulings & laws

- **D109 (DECISION_LOG:1047):** unidade fria no build seco → escreve AGORA, valida pós-F9; corretude
  RACIOCINADA (compila + fixpoint determinístico + prova de símbolos).
- **NO PUSHES:** buffer ≤1024 reusável + lista-de-chunks + cópia tamanho-exato; ZERO crescimento dinâmico.
- **Lei I/O streaming:** stdin em Teko sobre syscall, buffer ≤1024 B, sem `from "teko_rt"`.
- **D90:** `tk_rt_read_line`/`stdin_eof`/`read_stdin`/`read_stdin_n` viram MORTOS, deletados no F9.
- **D116:** shift de contabilidade → critério NÃO-CRESCER (flat).
- **Teko-only / W15:** `.tks` só; doc só no `exp`; sem `//`. **Fork protocol:** sem fork aberto.
- **Testes:** stdin não é exercitado pelo self-build → SEM `.tkr` afirmativo (validação pós-F9, D113/D109);
  não há caminho de erro/rejeição a oraculizar aqui.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do `bootstrap/teko.c`;
  commit por passo; fixpoint `gen2==gen3` byte-idêntico; reportar pico; reseedar `bootstrap/teko.c` ao fim;
  gen2/gen3 no scratchpad.

## Fixtures

`none — validado pós-F9 (D113/D109)` (o self-build não lê stdin; a corretude é raciocinada + prova de
símbolos: 0 refs `tk_rt_read_line`/`tk_rt_stdin_eof`/`tk_rt_read_stdin`/`tk_rt_read_stdin_n`).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico + pico flat ≤ 1044,2 MB + 0 refs aos 4 `tk_rt_*` de
stdin na árvore. "Green" = as 4 fns são Teko sobre syscall, EOF via slot de controle, sem `push`, `teko.c`
reproduz. **Reseed-class:** `fixpoint-rebuild`.

## Deps

`—`

## Done when

`read_line`/`stdin_eof`/`read_stdin`/`read_stdin_n` em Teko sobre syscall (buffer ≤1024, sem crescimento),
0 refs aos `tk_rt_*` de stdin, pico flat, `[fixpoint]` gen2==gen3, `bootstrap/teko.c` reseedado.
