# Cluster I/O Stream / FS / output / Panic / Misc → Teko (rumo ao F9)

Recon 2026-08-26 (arquiteto). Base `fix/retirement` `c1d4ebb0`. Baseline de pico local gen2 = **1044,2 MB** (D116).

## Veredito curto

O cluster está **muito mais migrado** do que o census de tarefa presumia. Na **rota C (a que valida hoje)**:

- **Saída** (`write`/`ewrite`/`print`/`println`/`eprint`/`eprintln`) — JÁ Teko puro em `src/runtime/rtio.tks`
  sobre `SYS_WRITE` / `abi::os_write_raw` / `WriteFile`. O codegen roteia o intrínseco `print`/`write`/…
  para `cb_fn_name_str("teko::runtime", …)` (codegen.tks:3787-3794) → chama o Teko compilado, NÃO `tk_print`.
- **`exit` / `panic(str)` (keyword)** — JÁ Teko (`rtio.tks:72,86` → `teko::runtime::rt_exit`); codegen roteia
  `panic`/`exit` para `teko::runtime::panic`/`exit` (codegen.tks:3789-3790).
- **FS** (`list_dir`/`mkdir`/`remove_file`/`file_size`/`stat`) — JÁ Teko sobre syscalls (`src/fs/fs.tks`).
- **read/write de arquivo** (`read_file`/`write_file`/`append_file`/`write_file_bytes`) — JÁ Teko sobre
  `read_stream`/`write_stream`/`append_stream` da `FileStream` (`src/io/io.tks` + `src/io/file_stream.tks`),
  buffer ≤1024 B. A saída de 22 MB do codegen JÁ streama (`cg_emit_c_file_mode`, D114).

**As branches host-FFI `read_file`/`write_file`/`write_file_bytes` do codegen (codegen.tks:3744-3747) estão
MORTAS** — `teko::io::*` são `user_declared` → o `cg_call_is_user_declared` desqualifica o intrínseco
(codegen.tks:3742 `addr2 = !user_declared && …`). Os 12 refs a `tk_rt_read_file`/`tk_rt_write_file*` em
`bootstrap/teko.c` são **seed velho** — somem no próximo reseed a partir da árvore atual.

### O que AINDA é C na rota que valida (os alvos deste cluster)

| grupo | símbolos C | quente? | acoplado? |
|---|---|---|---|
| **A · Panic/trap** (emitido inline pelo codegen/lir) | `tk_panic`, `tk_panic_oob`, `tk_panic_oob_at` (6155 emissões), `tk_panic_cast` (+ globais `_tk_cast_loc_line/col`), `tk_panic_div0`, `tk_panic_overflow`, `tk_panic_null_deref_at`, `tk_nn` | HOT (guard só DISPARA em falha → runtime nulo no build seco; só o **símbolo emitido** muda) | NÃO — reusa `teko::runtime::panic` (já vivo) |
| **B · stdin** | `tk_rt_read_line`, `tk_rt_stdin_eof` (+ global `tk_rt_stdin_eof_flag`), `tk_rt_read_stdin`, `tk_rt_read_stdin_n` | FRIO (build seco não lê stdin → valida pós-F9) | NÃO — autocontido |
| **C · residuais** | `tk_flush_out`; `tk_exit_status` (emitido no `main` return, codegen.tks:434); FS dead-routing | morno/trivial | NÃO |

**Fora de escopo (outros clusters):** `tk_rt_fd_fill`/`fd_take_byte`/`fd_wait_readable` são `win_fd_*` do
**process** (`src/process/process.tks:677-683`), Windows-interativo — pertencem ao cluster PROCESS, não a este.
`tk_panic_str` só sobrevive na perna **native** (`lower.tks:1972`, diferida) — na rota C o panic-keyword já
é Teko. As 18 `tk_cov_*` (coverage) são o D113 (todo-ou-nada, à parte). Backtrace (`tk_backtrace`) é C-frozen,
sai no F9.

## Falta de superfície STREAM?

**Não falta.** `FileStream` já cobre `open_read`/`open_write`/`open_append`, `stream_read` (chunk ≤1024),
`stream_write`/`stream_write_prefix` (fatiado ≤1024, zero-copy da base), `stream_seek`/`stream_size`
(offset/append/read-only), `stream_close`. As três variações da lei (offset/seek · append-only · read-only)
existem. O único "materializa" residual é `read_stream` montar o arquivo inteiro num `[total]byte` de
tamanho EXATO — **inerente** (o lexer precisa do fonte inteiro para spans/slices; um lexer por-chunk é
redesenho fora deste cluster e NÃO mandado — a lei mirava o buffer de 22 MB da SAÍDA, que já streama). Sem
`push`: `read_stream` usa scratch ≤1024 + `[total]byte` exato; `stream.tks::Buf.push`/`extend`/`read_all`
(acumuladores `[..x, b]`) são superfície de biblioteca NÃO usada pelo compilador — não tocar aqui (mover-o-
não-usado, D57; expurgo de `push` só onde o compilador usa).

## Sítios do compilador que ainda materializam

Leitura de fonte (`assemble.tks:131`, `project.tks:*`, `docspan.tks:8`, `fmt.tks:667`, `journal.tks`,
`fixture_guard.tks`) vai por `teko::io::read_file` → `read_stream` → `[total]byte` exato (read-only, chunk
≤1024). É a forma STREAM read-only da lei, com materialização inerente aceita. Escrita (`.tkl`/`.tsym`/
`.tkh`/plist/objeto) vai por `write_stream`/`write_file_bytes` (append/trunc chunked). **Nenhum sítio de
saída de texto do compilador materializa buffer que cresce** (D114 confirmou: só o fold pairwise, já
resolvido). Nada a converter aqui.

## Impacto no pico (ratchet D68)

Rotear C→Teko **move alocação do `malloc` libc (invisível ao pico) para a arena (contada)** → o ganho real
NÃO aparece na linha de pico; o critério de expurgo é **NÃO-CRESCER (flat)**, não queda (D116/D114). Grupo A
(panic) é branch-em-falha: nunca executa no build seco → pico **flat** (só troca de símbolo emitido). Grupo B
(stdin) é frio. Grupo C é trivial. **Alvo de aceitação de cada crumb: pico ≤ 1044,2 MB (flat).**

## Sequência (cada crumb `[fixpoint]`, reseed-class `fixpoint-rebuild`, reseed ao fim pelo implementer)

1. **0132 · IO-R1 residuais** — `flush`→Teko no-op; `tk_exit_status`→máscara inline `& 0xFF`; remover as
   branches host-FFI mortas `read_file`/`write_file`/`write_file_bytes` (codegen+lir). Pequeno, mecânico.
2. **0133 · IO-R2 stdin** — `read_line`/`stdin_eof`/`read_stdin`/`read_stdin_n` → Teko sobre `SYS_READ(0)`/
   `ReadFile`; flag EOF em slot de controle; idioma lista-de-chunks-+-cópia-exata para o `read_stdin`
   ilimitado (sem `push`). Frio → valida pós-F9.
3. **0134 · IO-R3 panic/trap** — família de traps → `teko::runtime` reusando o sink `panic()`; prefixo de
   posição byte-idêntico via `rtio_write_all(2, …)` cru (bypass do canal, igual ao `fputs(stderr)` do C);
   trocar os símbolos emitidos no codegen (2821/2846/3410/3835-38/3958/4069/4100/7092/7539) + espelho lir
   (268/347/6000); descartar globais `_tk_cast_loc_*` passando `(line,col)` a `panic_cast`. HOT, medir flat.

Medir pico: **0134** (o quente) reporta o pico; 0132/0133 confirmam flat. Nenhum crumb bloqueado.

## Riscos / tensões (resolvidos por lei — sem HALT)

- **Prefixo de posição sob capture (byte-preservação):** o C escreve `"L:C: "` com `fputs(stderr)` CRU
  (out-of-band do canal de teste), depois o marker+msg vão pelo caminho de capture. Reproduzir exato:
  `teko::runtime::panic_oob_at` faz `rtio_write_all(2, "L:C: ")` (cru, fd 2, sem checar canal) e delega a
  `panic("index out of bounds")` (marker+capture). Bytes idênticos nas duas modos. Resolvido no desenho.
- **Backtrace:** o C emite `tk_backtrace()` após a linha de panic; `teko::runtime::panic` (keyword, JÁ vivo)
  NÃO emite — precedente em vigor. Estender aos guards é aplicação consistente, não fork novo; backtrace é
  C-frozen/pós-F9. Scout confirma que nenhum oráculo em `examples/regressions/` casa stderr exato de
  backtrace de oob/div0/cast.
- **`abort()`/SIGABRT vs `rt_exit(134)`:** o guard C aborta (134 + core); `teko::runtime::panic` faz
  `rt_exit(134)` — MESMO exit code observável por `.tkr`, sem sinal. Divergência já ratificada pelo
  panic-keyword. Sem fork.
