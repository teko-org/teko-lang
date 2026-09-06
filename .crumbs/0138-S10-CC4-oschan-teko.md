---
seq: 0138
crumb-id: S10-CC4
milestone: F7
gate: "[fixpoint]"
reseed-class: expurgo
deps: [S10-CC1, S10-CC2]
sources:
  - "docs/design/plano-s10-concurrency-cluster-migration.md:1-90"
  - "DECISION_LOG.md:865-866"   # D87 canais Windows named pipe já desenhado
  - "docs/design/concorrencia-isolate-spawn-chan-0.3.1.md"
---

# 0138 · S10-CC4 — oschan (canal cross-proc, tkr) em Teko syscalls, per-OS

> Reescreve o transporte de canal cross-processo (`tk_oschan_*`, AF_UNIX SOCK_DGRAM, C
> Linux-only) em Teko sobre syscalls per-SO, adicionando a ABI de socket e movendo o
> cache de writer-fd do `_Thread_local` para a region_program.

## Goal

`tk_oschan_make/send/recv/close/end` é um transporte MPSC AF_UNIX `SOCK_DGRAM` (Linux
raw; Windows = panic-stub) — o canal cross-processo dos testes `tkr` (D62/D58.1). Migra
para Teko: Linux via syscalls crus `socket/bind/sendto/recvfrom/setsockopt` (adicionar à
`sys.tks`/abi); macOS via `teko::sys::abi` (`from "System"`); Windows via **named pipe
`FILE_FLAG_OVERLAPPED`** (D87, já desenhado). O cache per-thread de socket-writer
(`_Thread_local` no C) vira slot na `region_program`. Corpos C mortos até F9. **byte-mover**
(nova ABI + símbolos) → reseed. FRIO no build seco → pico flat. **Maior/risco** do cluster
(números de syscall per-arch; named pipe Win).

## Where

- `src/sys/sys.tks` — NOVO: `SYS_SOCKET`/`SYS_BIND`/`SYS_SENDTO`/`SYS_RECVFROM`/
  `SYS_SETSOCKOPT` per-arch (`#os("linux")`: x86_64 = 41/49/44/45/54; aarch64 =
  198/200/206/207/208) + consts `AF_UNIX`(1), `SOCK_DGRAM`(2), `SOL_SOCKET`, `SO_RCVBUF`,
  `MSG_DONTWAIT`.
- `src/sys/abi/mac.tks` — NOVO `pub extern socket`/`bind`/`sendto`/`recvfrom`/`setsockopt`
  (`from "System"`).
- `src/sys/abi/windows.tks` — NOVO `pub extern CreateNamedPipeA`/`ConnectNamedPipe`/
  `CreateFileA`/`WriteFile`(existe)/`ReadFile`(existe)/`CloseHandle`(existe) (`from "kernel32"`).
- NOVO `src/threads/oschan.tks` — tipo `OsChan` + `oschan_make`/`send`/`recv`/`close`/
  `end` (`exp`), 3 corpos `#os`; `sockaddr_un` per-`#os` (R3).
- `src/threads/threads.tks` — `IChannelKind` do transporte oschan; cablagem (resolução
  `svc<>` = honest-stop DI).
- **NÃO tocar** `teko_rt.c` `tk_oschan_*` — D90; mortos até F9.

## How

1. **ABI de socket** (passo 1, `sys.tks`/abi). Linux usa `teko::sys::syscall*` com os
   números per-arch; macOS os externs `from "System"`; Windows os de named pipe.

2. **`sockaddr_un` no abstract namespace** (Linux): `sun_family=AF_UNIX`, `sun_path[0]=0`,
   `"tkchan:"`+chave (trunca p/ caber) — espelha `tk_oschan_bind_name` (`teko_rt.c:2579`).
   Construído num buffer da region (tamanho exato).

```teko
/**
 * oschan_make — bind a cross-process MPSC channel to `key`. Linux/macOS use an AF_UNIX
 * SOCK_DGRAM reader socket in the abstract namespace; Windows uses an overlapped named
 * pipe. `bounds != 0` makes send non-blocking (observable back-pressure).
 *
 * @param elem_size  bytes per element (fixed-width v1)
 * @param bounds     capacity hint, or 0 for blocking send
 * @param key_ptr    address of the key bytes
 * @param key_len    key length
 * @return           handle (control-block address) of the channel
 * @since 0.3.1
 */
exp fn oschan_make(elem_size: u64, bounds: u64, key_ptr: u64, key_len: u64): u64
```

```teko
/**
 * oschan_send — send one element across the transport.
 *
 * @param ch        handle from oschan_make
 * @param elem_ptr  address of the element (elem_size bytes)
 * @return          0 on success, -1 on error / would-block (bounded back-pressure)
 * @since 0.3.1
 */
exp fn oschan_send(ch: u64, elem_ptr: u64): i64
```

```teko
/**
 * oschan_recv — receive one element; blocks until available or closed.
 *
 * @param ch       handle from oschan_make
 * @param out_ptr  address of the destination (elem_size bytes)
 * @return         1 if received, 0 on the zero-length CLOSED sentinel
 * @since 0.3.1
 */
exp fn oschan_recv(ch: u64, out_ptr: u64): i64
```

```teko
/**
 * oschan_close — send the zero-length CLOSED sentinel to the reader.
 *
 * @param ch  handle from oschan_make
 * @since 0.3.1
 */
exp fn oschan_close(ch: u64)
```

```teko
/**
 * oschan_end — tear down the reader endpoint (idempotent).
 *
 * @param ch  handle from oschan_make
 * @since 0.3.1
 */
exp fn oschan_end(ch: u64)
```

3. **Corpos** (espelham `teko_rt.c:2593-2660`):
   - `make`: `socket(AF_UNIX, SOCK_DGRAM, 0)`; `bind` no nome derivado; se `bounds` →
     `setsockopt(SO_RCVBUF, bounds*elem_size*2)`. Windows: `CreateNamedPipeA` overlapped
     (D87).
   - `send`: writer-fd do cache per-região (passo 4); `sendto(fd, elem, elem_size,
     bounded?MSG_DONTWAIT:0, addr)`; retry em `EINTR`; retorna -1 em erro/would-block.
   - `recv`: `recvfrom(reader_fd, out, elem_size, 0)`; `r==0`→0 (closed); retry `EINTR`.
   - `close`: `sendto(fd, "", 0, addr)` (datagrama zero = sentinela).
   - `end`: fecha o reader_fd.
4. **Cache de writer-fd na region_program** (não `_Thread_local`): slot `CTRL_OSCHAN_WCACHE`
   novo em `arena.tks` (padrão dos ~30 slots livres) — array pequeno fixo `{ch,fd}`
   (8 entradas, tamanho fixo, NO PUSHES; overflow abre fd não-cacheado).
5. **Cablagem** `threads.tks`: transporte oschan em `IChannelKind`; `Tx.send`/`Rx.pop`/
   `close`/`end` → `oschan_*`. **Aresta DI (D58.1):** resolução por chave via `svc<>`
   bloqueada em DI → honest-stop; transporte puro (handle/chave explícita) roda no shadow.

## Rulings & laws

- **Teko-only / D90:** `teko_rt.c` intocado; `tk_oschan_*` mortos até F9.
- **R3 (struct per-`#os`):** `sockaddr_un` por SO; **D87:** Windows named pipe (não AF_UNIX).
- **§16 sem atalhos (R1-R5):** se existe em C, existe em Teko — Windows NÃO fica panic-stub;
  named pipe real (D87). macOS via libSystem (D101).
- **NO PUSHES:** cache de writer fixo (8 entradas); buffers de tamanho exato.
- **region_program (D110/D113):** cache de writer sai do `_Thread_local` p/ slot `CTRL_*`.
- **exp (D111):** `oschan_*` linkável → `exp`.
- **D58.1 (design-ahead):** resolução `svc<>` bloqueada em DI → honest-stop.
- **D117 shadow; D118 pico flat.**
- **Fork protocol:** sem fork (D87/R3/D101 cobrem os per-SO).
- **Safety:** subshell `ulimit -v 4718592`; sem `teko test .`; reseed no `[fixpoint]`.

## Fixtures

`none — o fixpoint não exercita oschan; prova de runtime é o SHADOW (D117).`

**SHADOW (scratchpad):** W writers (spawn/CC1) × 1 reader; cada writer envia M
datagramas; reader conta até o sentinela de close; conferir contagem == W·M e término
por sentinela (espelha `tk_oschan_selftest`, `teko_rt.c:2700`); exit 0. Rodar no
Linux (macOS/Windows validados nas pernas de plataforma pós-marco/native).

## Gate

`[fixpoint]` — gen2 + shadow verde (Linux) + `gen2==gen3` byte-idêntico; reseed `expurgo`.

## Deps

S10-CC1 (spawn dirige writers), S10-CC2 (Ctx/WaitGroup).

## Done when

O transporte oschan roteia por syscalls Teko per-SO (Linux raw / macOS System / Windows
named pipe — zero AF_UNIX-C, zero `_Thread_local`), o shadow W×R roda verde no Linux,
cablagem `Tx`/`Rx` pronta (resolução `svc<>` em honest-stop DI), e o fixpoint gen2==gen3
é byte-idêntico reseedado.
</content>
