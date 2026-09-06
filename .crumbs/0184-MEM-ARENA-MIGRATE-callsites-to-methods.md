---
seq: 0184
crumb-id: MEM-ARENA-MIGRATE
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-ARENA-TYPE]
sources:
  - "DECISION_LOG.md:1151"                                            # D149 (as fns soltas saem no FIM)
  - "docs/design/arena-region-tipo-com-metodos-0.3.1.md:§3"          # o escalonamento por lote
  - "src/runtime/arena.tks"                                           # auto-chamadas (Lote A) + fns soltas a remover (Passada 3)
  - "src/checker/scope.tks"                                          # consumidor (Lote B)
  - "src/process/process.tks"                                        # consumidor (Lote B)
  - "src/env/env.tks"                                                # consumidor (Lote B)
  - "src/build/project.tks"                                          # consumidor (Lote B)
  - "src/codegen/codegen.tks:103-151"                               # CgArenaSym (Lote C — decisão Fork-1)
---

# 0184 · MEM-ARENA-MIGRATE — migra call-sites loose→método por lote; remove as fns soltas mortas — RESEED-2/3

> A passada de USO: agora que `Region`/`Arena` existem (0183), reescreve os call-sites escrito-à-mão
> loose→método (`region_new(p)`→`p.child()`, `region_alloc(r,n)`→`r.alloc(n)`, `region_program()`→
> `arena.program()`) por LOTE (arena.tks interno → consumidores src/ → emissores), e no fim REMOVE as fns
> soltas agora mortas. O fixpoint (gen2==gen3) guarda cada lote. RESEED-2 (migração) + RESEED-3 (remoção,
> funde no 2 se o lote fechar).

## Goal

Colapsar as ~120 fns soltas na superfície de método. Doc §3 Passada 2/3: (A) as auto-chamadas dentro de
`arena.tks` viram `self.metodo(...)`; (B) os ~30 consumidores src/ (`region_program`×10, `region_alloc`×10,
`region_new`×3, `region_drop`×3, `region_current`/`enter`/`leave`) viram método; (C) os emissores
(`codegen.tks`/`lower.tks`, `CgArenaSym`) — MANTÊM os ~17 símbolos de entrada como delegadores finos (doc
§6 Fork-1, recomendado) OU migram para símbolos method-mangled (owner-steerable). No fim, as fns soltas
não-mais-referenciadas + os privados `ar_*` dobram em método privado / são removidos.

## Where

- `src/runtime/arena.tks` — **Lote A:** auto-chamadas `region_*`/`ar_*_w` → `self.metodo`; os privados
  `ar_*` de região/controle dobram em helper privado de `Region`/`Arena`. **Passada 3:** DELETA as fns soltas
  `pub fn region_*`/`arena_*`/`alloc`/`*_slot` já sem consumidor (as que o codegen NÃO emite; ver Lote C).
- `src/checker/scope.tks`, `src/process/process.tks`, `src/env/env.tks`, `src/build/project.tks`,
  `src/runtime/rtio.tks` — **Lote B:** `teko::runtime::region_program()`→`arena.program()`,
  `teko::runtime::region_alloc(r,n)`→`r.alloc(n)`, `region_new`/`region_drop`→`child`/`drop`, etc. Cada
  módulo é um sub-lote com build próprio só no checkpoint (menos build, mais código — CLAUDE.md).
- `src/codegen/codegen.tks:103-151` + `src/lir/lower.tks` — **Lote C:** decisão Fork-1. Recomendado: os ~17
  `CgArenaSym` seguem emitindo os nomes de entrada estáveis (`region_alloc`/`region_new`/…), que na Passada 3
  viram delegadores finos aos métodos (a ABI codegen↔runtime é um conjunto mínimo estável, não é superfície
  de usuário). Se o dono quiser expurgo total: virar `cg_arena_teko_sym` para o símbolo method-mangled.

## How

1. **Lote A** (arena.tks interno) → build → checkpoint.
2. **Lote B** (consumidores src/, módulo a módulo) → build no fim do lote → checkpoint. **RESEED-2** aqui.
3. **Lote C** (emissores) conforme Fork-1 (recomendado: manter entradas como delegadores).
4. **Passada 3** — remove as fns soltas mortas + dobra os privados `ar_*`; o conjunto de entrada-runtime
   remanescente (Lote C) é o mínimo estável → **RESEED-3** (funde no 2 se o lote fechar limpo).
5. Cada reseed = harvest fixpoint gen2==gen3 byte-idêntico.

## Rulings & laws

- **D149:** as fns soltas SAEM no FIM (Passada 3). **D148 zero C.**
- **Fixpoint = gen2==gen3, NÃO diff-zero-contra-antes:** a migração muda o `teko.c` (chama métodos), mas
  cada harvest é auto-consistente — reseed encenado normal (doc §3).
- **Menos build, mais código (CLAUDE.md):** um build por LOTE fechado, não por edição.
- **Zero fixture versionada:** o self-build exercita a arena; o fixpoint valida a migração (a arena é
  chamada por `list`/`str`/`map`/`io` ao compilar). NÃO escrever `.tkt`/`.tkr` afirmativo.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + MEM_PARANOID + RSS ratchet (não-crescer: é relocação, behavior-preserving).

## Fixtures

Zero fixture versionada (doc; a migração é relocação behavior-preserving exercitada pelo self-build).

## Gate

`[RITUAL]` — **RESEED-2** (+RESEED-3): cada lote `gen2==gen3` byte-idêntico, MEM_PARANOID 0, RSS NÃO-cresce
(relocação, não é redução — a redução real é W3+). "Green" = todos os call-sites escrito-à-mão usam
`r.alloc`/`p.child`/`arena.program`/…, as fns soltas mortas foram removidas, o conjunto de entrada-runtime é
o mínimo estável, `gen2==gen3`, `bootstrap/teko.c` reseedado. Reseed-class: `fixpoint-rebuild`.

## Deps

`MEM-ARENA-TYPE` (0183 — os tipos+métodos que os call-sites passam a chamar).

## Done when

Todo call-site escrito-à-mão de arena usa os métodos de `Region`/`Arena`, as fns soltas agora mortas foram
removidas, os privados `ar_*` dobraram em método privado, o conjunto de entrada-runtime do codegen é o mínimo
estável, e o gate ritual é verde com os reseeds harvestados (RESEED-2/3).
</content>
