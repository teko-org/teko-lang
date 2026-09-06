---
seq: 0126
crumb-id: NAT-B0
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L6]
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-260"   # this campaign map (métrica, graceful-stop, escopo A+B)
  - "src/codegen/codegen.tks:3215"                                     # emit_capture_panic (uses __builtin_setjmp:3228)
  - "src/codegen/codegen.tks:3249"                                     # emit_capture_longjmp (__builtin_longjmp:3250)
  - "src/codegen/codegen.tks:3513"                                     # dispatch capture_panic / capture_longjmp builtins
  - "DECISION_LOG.md:1010"                                             # D105 — tail-§16: capture_panic C-leg via setjmp/longjmp (SUPERSEDED here)
  - "owner ruling 2026-08-26"                                          # graceful-stop: capture/exit/cancel by return, NOT longjmp
---

# 0126 · NAT-B0 — capture/exit/cancel como GRACEFUL-STOP (elimina setjmp/longjmp)

> Redesenhar `capture_panic`/exit/cancel como controle de fluxo PURO — flag de panic-pendente por-task +
> return cooperativo com defers/drops por frame — MESMA lógica nas rotas C e native, eliminando
> `__builtin_setjmp`/`__builtin_longjmp` e o mac-blocker que eles causam.

## Goal

Hoje a captura de panic é emitida pela rota C com `__builtin_setjmp`/`__builtin_longjmp`
(`codegen.tks:3215`/`:3249`), o que (a) **NÃO compila no macOS** (`bootstrap/teko.c:191627:
__builtin_longjmp is not supported for the current target` no CI #1135) e (b) exige uma primitiva de
unwind/PC-SP-restore que o backend native teria de espelhar. O dono (2026-08-26) decide o mecanismo:
**graceful-stop** — uma **flag de panic-pendente por-task** (na área de captura por-task) que cada função
que possa entrar em panic consulta **após cada call**; se `panicking()`, a função roda os defers/drops do
próprio frame e faz `return <sentinela>`; o frame de CAPTURA (o `capture { … }`) testa a flag, consome-a e
segue o braço de recuperação. É **controle de fluxo normal** — reusa o caminho de defer/drop do return —,
portável Linux/Windows/Mac, zero-libc, e **IDÊNTICO nas duas rotas** (o native emite o mesmo check-flag +
cleanup + return). Supersede o C-leg longjmp de `0064`/D105 (mais-recente-vence). Byte-mover na rota C
(`teko.c` muda) → **drives reseed**; write-only no native.

## Where

- `src/codegen/codegen.tks:3215` — `emit_capture_panic` — REESCREVER: em vez de `__builtin_setjmp`, emitir o
  frame de captura como `{ arm_capture_slot(); <corpo>; if (panicking()) { <consume + braço recovery> } }`.
- `src/codegen/codegen.tks:3228` — a linha `"; if (__builtin_setjmp("` — REMOVER (não emitir builtin).
- `src/codegen/codegen.tks:3249` — `emit_capture_longjmp` — REESCREVER: setar a flag de panic-pendente da
  task (`set_panicking()`) + `return <sentinela do tipo de retorno da fn>`; NÃO emitir `__builtin_longjmp`.
- `src/codegen/codegen.tks:3250` — a linha `"__builtin_longjmp(...)"` — REMOVER.
- `src/codegen/codegen.tks:3513`/`:3514` — dispatch dos builtins `capture_panic`/`capture_longjmp` — manter
  os nomes de builtin (a superfície não muda), mudar só o CORPO emitido.
- `src/codegen/codegen.tks` — emitir, após cada `TCall` que possa entrar em panic dentro de uma fn sob
  captura, o guard `if (panicking()) { <defers/drops do frame>; return <sentinela>; }` (reusa o gerador de
  defer/drop do return já existente).
- `src/lir/lower.tks` — espelho native: NOVOS arms `lower_capture_frame` (arma o slot + testa a flag) e
  `lower_panic_return_guard` (o check-flag+cleanup+return pós-call). SEM primitiva de unwind — controle de
  fluxo comum reusando o lowering de return/defer.
- Área de captura por-task — a flag `panicking` mora no bloco de captura por-task já existente (o mesmo que
  o C-leg setjmp usava como `jmp_buf`); vira um `u8`/`bool` + payload do panic.

## How

1. **Estado por-task.** A flag de panic-pendente e o payload do panic ficam num slot por-task (reusa o
   espaço que o `jmp_buf` ocupava). `panicking()` lê a flag; `set_panicking(payload)` a arma; `clear_panicking()`
   a consome no frame de captura.

```teko
/**
 * panicking — whether a panic is pending on the current task's capture slot, tested after every
 * call that may panic so the frame can run its defers/drops and return the sentinel.
 *
 * @return  true if a panic is pending and unconsumed on the current task
 * @since 0.3.1
 */
exp fn panicking(): bool
```

```teko
/**
 * set_panicking — mark a panic pending on the current task and stash its payload; the calling frame
 * then runs its own defers/drops and returns its sentinel value (no stack unwind primitive).
 *
 * @param payload  the panic value the enclosing `capture { }` frame will recover
 * @since 0.3.1
 */
exp fn set_panicking(payload: teko::panic::Payload)
```

2. **Corpo da fn sob captura (codegen C).** Após CADA `TCall` que possa entrar em panic, emitir o guard
   (reusando o gerador de defer/drop do `return`):

```
<vreg> = <callee>(...);
if (panicking()) { <defers+drops deste frame>; return <sentinela>; }
```

O sentinela é o zero-value do tipo de retorno (void → `return;`). O guard é PURO controle de fluxo — nenhum
builtin de unwind.

3. **Frame de captura (`emit_capture_panic` reescrito).** Emitir:

```
<corpo do bloco capturado>
if (panicking()) { <payload> = take_panic(); clear_panicking(); <braço de recovery> }
```

4. **`emit_capture_longjmp` reescrito (o ponto de panic).** Em vez de `__builtin_longjmp`, emitir
   `set_panicking(<payload>); return <sentinela>;`. O nome de builtin `capture_longjmp` no dispatch (`:3514`)
   permanece — só o corpo muda.

5. **Espelho native (`lower.tks`).** `lower_capture_frame` e `lower_panic_return_guard` emitem EXATAMENTE o
   mesmo controle de fluxo em LIR: teste da flag (`ICmpNe(panicking, 0)`), bloco de cleanup (reusa o lowering
   de defer/drop do return), e branch/return. **Não há `LOp` de unwind** — some a primitiva de PC-SP-restore
   do mapa de ensino.

6. **Confirmar mac-unblock.** Nenhum `__builtin_setjmp`/`__builtin_longjmp` é emitido → o `teko.c` compila no
   macOS. `grep __builtin_.*jmp bootstrap/teko.c` = vazio após reseed.

## Rulings & laws

- **Teko-only:** `src/codegen/*.tks` + `src/lir/*.tks`; sem C twin. O runtime de panic mora em Teko (sem
  `from "teko_rt"`).
- **Comment convention (W15, owner 2026-08-19):** `/** */` só em `exp`; sem `//`/`/* */`; doc nunca maior que
  o código.
- **Fork protocol (owner 2026-08-19):** o mecanismo é DELIBERADO (dono 2026-08-26, graceful-stop) e SUPERSEDE
  o setjmp/longjmp de D105/`0064` (mais-recente-vence) — NÃO HALT.
- **W15 full Javadoc** em toda decl nova `exp`; flatten com early-return; sem `//`.
- **Removals = clean expurgo:** os builtins `__builtin_setjmp`/`__builtin_longjmp` saem do C emitido sem
  tombstone; a superfície de builtin (`capture_panic`/`capture_longjmp`) é PRESERVADA (só o corpo muda).
- **Safety:** NUNCA `teko test .`; build em subshell `ulimit -v 4718592` (4,5 GiB, dono 2026-08-24) — estouro
  é correção de causa-raiz, nunca teto maior; commit por passo verde; reseed no fim (RT-L6/R#2); fixpoint C
  `gen2.c==gen3.c` byte-idêntico; sweep após mudança de assinatura. **Ratchet D68:** o graceful-stop não pode
  CRESCER o pico vs. o setjmp/longjmp que substitui (piso não-crescer do expurgo de runtime).
- Ruling-base: dono 2026-08-26 (graceful-stop) SUPERSEDE `DECISION_LOG.md:1010` (D105 capture C-leg longjmp).

## Fixtures

`none — o self-build fixpoint exercita isto` na rota C (o compilador usa panic/capture ao rodar). O caminho
de RECOVERY (o braço que roda só quando um panic dispara) NÃO é dirigido pelo self-build → UM oráculo:

| fixture | asserts | expected |
|---|---|---|
| `capture_recovers_panic` | `capture { <fn que dá panic> }` roda defers do frame, consome a flag e segue o braço de recovery (sem longjmp) | `0` |

## Gate

`[fixpoint]` — build gen2 (rota C) + regressão escopada + `gen2.c==gen3.c` byte-idêntico; e o `teko.c`
compila no macOS (nenhum `__builtin_*jmp` emitido). O espelho native é WRITE-ONLY (compila no self-build,
não roda). Reseed-class: `fixpoint-rebuild` (rides o reseed de F8/RT-L6, R#2).

## Deps

`RT-L6` (0064) — o harness/crash em que a captura de panic vive; este crumb REDESENHA o mecanismo que 0064
introduziria via setjmp/longjmp. Roda no MESMO reseed de fase (R#2/F8).

## Done when

`capture`/exit/cancel emitem controle de fluxo por flag+return (defers/drops por frame) nas duas rotas,
nenhum `__builtin_setjmp`/`__builtin_longjmp` aparece no `teko.c`, o `teko.c` compila no macOS, e o fixpoint
C é byte-idêntico.
