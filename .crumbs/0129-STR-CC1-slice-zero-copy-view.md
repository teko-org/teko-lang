---
seq: 0129
crumb-id: STR-CC1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L2]
sources:
  - "docs/design/str-concat-slice-eager-free-0.3.1.md:20-40"     # veredito frente A
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:45"       # query/slice de str = Teko puro
  - "DECISION_LOG.md:1066-1072"                                  # D112
---

# 0129 · STR-CC1 — `str_slice` family = view zero-copy inline (remove símbolo C)

> Migra `teko::str::slice`/`_to`/`_from` do símbolo C `tk_str_slice` para emissão INLINE de view
> `{s.ptr+start, end-start}` — zero alocação, byte-comportamento idêntico, sem o símbolo C.

## Goal

`tk_str_slice`/`_to`/`_from` (`teko_rt.c:711-732`) são VIEW zero-copy — retornam `{s.ptr+start,
end-start}` sem alocar; eliminaram os 762 MB / 108M cópias do `name_last_segment`. A migração pra
Teko NÃO pode reintroduzir cópia (o `teko_rt.tks:98` `str_slice` COPIA — proibido roteá-lo). Este
crumb substitui a CHAMADA ao símbolo C por uma EMISSÃO INLINE do view (padrão `emit_str_from_c`),
removendo a dependência de `tk_str_slice` (habilita F9) com **pico flat** — sub-slice = view É a
semântica de array (`ref []T` = ponteiro-de-posição, CLAUDE.md). Byte-preserving; `fixpoint-rebuild`
(core-consome; ensina nada; sem reseed de ensino).

## Where

- `src/codegen/codegen.tks:3812-3814` — o roteamento `slice`/`slice_to`/`slice_from` →
  `tk_str_slice`/`_to`/`_from`. REMOVER estes três `else if` do bloco de builtins; desviar `teko::
  str::slice`(`_to`/`_from`) para uma emissão inline via nova `emit_str_slice` no ponto de dispatch
  (junto de `bytes_from_ptr`/`str_from_c`, ~`codegen.tks:3727-3732`).
- `src/codegen/codegen.tks:~3368` — `emit_str_from_c` — MODELO a copiar para a nova `emit_str_slice`
  (bloco `({ … })` com gensym, `emit_expr_ctx` dos args, bounds-check, retorno de `(tk_str){…}`).
- `src/runtime/teko_rt.tks:98-113` — `str_slice`/`_to`/`_from` (copiantes) — FICAM como estão
  (exp, linkáveis via `.tkh`), MAS OFF o caminho do codegen: o dispatch de builtin não os chama.
  NÃO roteá-los. (Corpo C `tk_str_slice` fica MORTO até F9.)
- NENHUMA superfície de usuário nova; os nomes `slice`/`slice_to`/`slice_from` pré-existem.

## How

1. Adicionar `emit_str_slice` espelhando `emit_str_from_c`: avalia `s` num temp, avalia `start`/
   `end`, faz o bounds-check (`start>end || end>s.len` → `tk_panic("string slice out of range")`),
   retorna o VIEW `(tk_str){ _s.ptr + start, end - start }`. Zero alocação.
2. `slice_to(s,end)` = view `{_s.ptr, end}` com check `end>_s.len`; `slice_from(s,start)` = view
   `{_s.ptr+start, _s.len-start}` com check `start>_s.len`. Emitir cada um inline (ou `slice_to`/
   `_from` delegando a `emit_str_slice` com 0/`s.len`).
3. No dispatch (`~3727`), antes do bloco de builtins com `has_builtin`, adicionar os arms
   `if l == "slice" { return emit_str_slice(...) }` (idem `slice_to`/`slice_from`), e REMOVER as
   linhas 3812-3814. O bounds-panic segue usando `tk_panic` (símbolo à parte, escopo F9).
4. Fixpoint byte-identity é o detector: o C emitido do view inline tem que produzir o MESMO
   `{ptr+start,len}` que `tk_str_slice` produzia — nenhum caller muda residência.

O bloco C que a nova `emit_str_slice` emite (o implementer copia a FORMA, não o texto):

```
({ tk_str _ss = (<s>); uint64_t _a = (uint64_t)(<start>), _b = (uint64_t)(<end>);
   if (_a > _b || _b > _ss.len) tk_panic("string slice out of range");
   (tk_str){ _ss.ptr + _a, (size_t)(_b - _a) }; })
```

Nenhuma nova declaração Teko de superfície; `emit_str_slice` é `fn` privada de codegen (sem doc —
lei de estilo: doc só em `exp`).

## Rulings & laws

- **Teko-only / D90:** zero edição em `teko_rt.c`; o corpo C `tk_str_slice` fica MORTO até F9. A
  mudança é 100% em `codegen.tks`.
- **D112 (DECISION_LOG:1066):** string = array; sub-slice = view (semântica de array), nunca cópia.
- **D68 ratchet:** pico só cai/flat; este crumb é byte-comportamento-idêntico ao view C → flat.
- **NO PUSHES / não-detectar-o-inexistente:** nada de acumulador; o `str_slice` copiante do
  `teko_rt.tks` NÃO entra no caminho.
- **Estilo:** sem `//`; doc-comment só em `exp` (a `emit_str_slice` privada não recebe doc).
- **Fork protocol (dono 2026-08-19):** decisão já deliberada em D112 + design-doc; sem fork aberto.
- **Safety:** NUNCA `teko test .`; build em subshell `ulimit -v 4718592` (4,5 GiB); `TEKO_CC=clang`;
  gen0 do `bootstrap/teko.c` (nunca `fetch_teko.sh`); commit por passo; fixpoint `gen2==gen3`
  byte-idêntico; reseedar `bootstrap/teko.c` ao fim (incondicional); deixar gen2/gen3 no scratchpad.

## Fixtures

`none — the fixpoint self-build exercises this` (o próprio compilador chama `teko::str::slice`
massivamente ao compilar — `name_last_segment` etc.; o fixpoint exercita o path).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico. "Green" = `teko::str::slice`(`_to`/`_from`)
emitem view inline, NENHUM `tk_str_slice`/`_to`/`_from` referenciado no `teko.c` emitido, o pico do
build seco mede **≤ 1146,1 MB** (flat), e o `teko.c` reproduz. **Reseed-class:** `fixpoint-rebuild`.

## Deps

`RT-L2` (`0060` — a família str/char sobre a qual isto assenta).

## Done when

`teko::str::slice`/`_to`/`_from` são emitidos como view zero-copy inline, sem símbolo C `tk_str_slice`
no path, pico flat ≤1146 MB, `[fixpoint]` gen2==gen3 byte-idêntico, e `bootstrap/teko.c` reseedado.
