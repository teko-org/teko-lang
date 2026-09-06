---
seq: 0177
crumb-id: TY-C2
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [TY-C1]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:4"        # §4 types.tks desenhado
  - "DECISION_LOG.md:1151-1157"                              # D145
  - "src/base/base.tks:7,15"                                 # ptr/uptr a mover
  - "src/embed/prelude_embed.tks"                            # lista de embed
  - "src/build/project.tks:306-318"                          # ns_of_prelude_key
  - "src/text/text.tks:138,189,251"                          # codecs UTF-8 a reusar
---

# 0177 · TY-C2 — `src/types/types.tks` (tipos reservados + métodos) + `Encoding` + injeção Base

> Cria o `types.tks` no prelúdio: todos os tipos reservados como reserved-newtype COM MÉTODOS que
> DELEGAM à impl atual (str byte-level, char code-point, numéricos, ptr/uptr). Adiciona o enum
> `teko::text::Encoding` e `char::from(str, enc): []char`. Embarca no VFS + injeta como Base em todo
> artefato. Move `ptr`/`uptr` de `base.tks` para cá. Reseed (gen0 ganha as decls+métodos).

## Goal

Consolidar as defs-base reservadas num arquivo injetado (Forma-3 dogfood). Com a ponte (TY-C0) já
landada, os métodos ficam CHAMÁVEIS. Corpos delegam aos builtins de `scope.tks`/`teko_rt.tks`/
`text.tks` → behavior-preserving. A fronteira byte(str)↔char([]char) fica EXPLÍCITA via
`char::from(src, enc)`. Nenhum call-site migra AINDA (isso é TY-M*); esta fundação só ENSINA a
superfície e semeia o gen0.

## Where

- NOVO `src/types/types.tks` (namespace `teko::types`) — as reserved-newtypes + métodos (§4 do doc).
- `src/text/text.tks` — NOVO `exp type Encoding = enum { Utf8; Utf8Bom; Ascii; Utf16Le; Utf16Be;
  Win1252 }` + `exp fn decode_for(src: str, enc: Encoding): []u32 | error` (dispatch: Utf8→
  `decode_utf8_u32`; resto → `error{"unsupported encoding: …"}` honest-stop) + `exp fn encode_for(
  cps: []u32, enc: Encoding): []byte | error` (Utf8→`encode_u32_utf8`).
- `src/base/base.tks` — ESVAZIAR: `ptr`/`uptr` migram para `types.tks` (base.tks vira só doc-less
  stub OU é removido do embed — decidir no crumb: manter o arquivo vazio quebra o embed; **remover
  `base.tks` do `prelude_embed.tks` e apagar o arquivo**, pois `types.tks` o substitui).
- `src/embed/prelude_embed.tks` — trocar a linha `#embed(".../src/base/base.tks", …)` por
  `#embed("/src/types/types.tks", Deflate, 6)`.
- `src/build/project.tks:306-313` `ns_of_prelude_key` — mapear `teko::/src/types/` → `teko::types`.
- `src/build/project.tks:315-318` `prelude_ns_wanted` — `teko::types` é wanted como base (retorna
  `true` incondicional, igual a `teko::base` hoje).

## How

1. Escrever `types.tks` conforme §4 do doc. Métodos-instância delegam (uma linha), ex.:

```teko
/**
 * str — a raw byte string: a same-representation newtype over `[]byte` carrying the byte-level
 * string surface as methods. Code-point operations live on `[]char` via `char::from` — `str`
 * never assumes an encoding.
 * @since 0.3.1
 */
exp global type str = []byte {
    /**
     * concat — this string followed by `o`, as a new string.
     * @param o  the string appended after `self`
     * @return  the concatenation
     * @since 0.3.1
     */
    fn concat(o: str): str { teko::str::concat(self, o) }
    // (slice/slice_to/slice_from/ends_with/starts_with/contains/last_index_of/eq/compare/len/bytes
    //  idem — delegam a teko::str::* / str_* / o wrap de bytes)
}
```
(O `//` acima é ilustrativo do doc — no `.tks` real NÃO há `//`; cada método é uma linha.)

2. `char` (§4.3): `type char = u32 { instância to_lower/to_upper/is_*/to_u32 }` + o ESTÁTICO
   `char::from(src, enc): []char` que chama `teko::text::decode_for(src, enc)` e reinterpreta
   `[]u32`→`[]char` zero-cópia (mesma-rep). `char::encode(cps: []char, enc): str` simétrico.

3. Numéricos/`isize`/`usize`/`ptr`/`uptr` conforme §4.1. `ptr.wrap<T>()` delega a `self.__wrap<T>()`;
   `ptr::unwrap<T>(ref x)` empacota o intrínseco.

4. `Encoding` + `decode_for`/`encode_for` em `text.tks` reusando os codecs UTF-8 landados — NÃO
   reimplementar. Os encodings não-UTF-8 honest-stop com mensagem `arquivo:linha:coluna:` clara.

5. Remover `base.tks` do embed + apagar; ajustar `ns_of_prelude_key`/`prelude_ns_wanted`.

6. O gen0 NÃO conhece `teko::types` ainda → staging de reseed pode exigir DUAS voltas (ensina as
   decls, reseeda; landa os métodos que as usam, reseeda) — seguir o padrão iterativo D132 se o
   build seco acusar.

## Rulings & laws

- **Teko-only.** `src/base/base.tks` é `.tks` (não é o C congelado) → pode mover/remover.
- **D145:** todos os tipos como superfície COM MÉTODOS, injetados pelo embed (Forma-3).
- **D131:** base primitiva READONLY dentro dos métodos (leem, produzem novos valores).
- **Reuso (dono mid-task):** `char::from`/`decode_for` CONSOMEM `teko::text::*` — não reimplementam.
- **stdlib default = exp (dono 2026-08-18):** a superfície de `types.tks`/`Encoding` é `exp`.
- **W15 full Javadoc só em `exp`; no `//`; doc nunca maior que o método.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3`; reseed
  incondicional (toca compiler-core). Ratchet D68: wrappers finos → pico ~flat; medir e reportar.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `types_char_from_utf8` | `char::from("áb", Encoding::Utf8).len == 2` (code points) | `0` |
| `types_char_from_unsupported` | `char::from(s, Encoding::Utf16Le)` retorna error | `0` |

(Ambos exercitam caminho que o self-build NÃO dirige — decode explícito com encoding nomeado e o
honest-stop. Os métodos de `str`/numérico ficam exercitados pelo self-build a partir de TY-M*.)

## Gate

`[RITUAL]` — build gen2 + os oráculos + `gen2==gen3` byte-idêntico + reseed genuíno do
`bootstrap/teko.c` (staging iterativo se o gen0 exigir). "Green" = `types.tks` injeta como Base em
todo artefato, os métodos tipam via a ponte, `char::from(Utf8)` decodifica, os não-UTF-8 param
honestos, e o fixpoint fecha. Reseed-class: `fixpoint-rebuild`.

## Deps

`TY-C1`

## Done when

`src/types/types.tks` + `Encoding` landam, injetam como Base, `ptr`/`uptr` saem de `base.tks`, os
métodos são chamáveis, os oráculos passam, `gen2==gen3` e o seed é colhido.
