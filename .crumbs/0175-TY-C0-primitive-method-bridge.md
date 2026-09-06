---
seq: 0175
crumb-id: TY-C0
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: [MEM-W5]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:3"        # §3.2 capacidade nova
  - "DECISION_LOG.md:1151-1157"                              # D145
  - "src/checker/typer.tks:1605-1705"                        # type_method_call
  - "src/checker/scope.tks:242-266"                          # builtin_type
---

# 0175 · TY-C0 — ponte de método sobre receptor primitivo/builtin

> Ensina o `type_method_call` (e o path estático) a despachar métodos quando o receptor é um
> tipo BUILTIN (`Str`/`Char`/`Byte`/`Prim`/`Ptr`/`Uptr`) achando a decl reserved-newtype de mesmo
> nome na tabela de tipos. Additiva e INERTE enquanto nenhuma decl reserved-newtype com métodos
> existe (TY-C2) → byte-preserving. É a ÚNICA capacidade nova da onda `types.tks`.

## Goal

Hoje `type_method_call` (`typer.tks:1624`) exige `recv_t` ser `Named`; um receptor builtin
(`Str`/`Prim`/…) cai em `"method typing is deferred"`. Para o dono usar `s.concat(b)` /
`x.to_str()` / `c.to_lower()` sobre os tipos reservados, o despacho precisa mapear o receptor
builtin ao seu NOME reservado (`Str`→`"str"`, `Prim{U64}`→`"u64"`, `Char`→`"char"`, `Byte`→`"byte"`,
`Ptr`→`"ptr"`, `Uptr`→`"uptr"`), achar a `TypeDecl` newtype desse nome (`type_table_find`), e
despachar aos `nb.methods` como já faz para `Named`. Byte-preserving: sem decl reserved-newtype
(pré-TY-C2) o mapeamento não acha método → comportamento idêntico.

## Where

- `src/checker/typer.tks:1605` `type_method_call` — antes do `struct_name = match recv_t { Named … }`
  (linha 1624), inserir: se `recv_t` é builtin e existe reserved-newtype de mesmo nome COM o método
  pedido, resolver `struct_name` para esse nome e seguir o fluxo `NewtypeBody` existente.
- `src/checker/typer.tks` — NOVO helper `prim_reserved_name(t: @Type()): str | null` (o mapa
  builtin→nome reservado).
- O path de método ESTÁTICO (`Owner::method`, resolvido em `type_call`) — reconhecer
  `str::from_bytes`/`char::from`/`u64::parse`/`ptr::unwrap` como estáticos da reserved-newtype
  (delegar ao já-existente lookup de método estático de `Named`, agora alcançável pelo nome builtin).

## How

1. Helper de mapeamento (NÃO exp — plumbing do checker):

```teko
fn prim_reserved_name(t: @Type()): str | null {
    match t {
        Str => "str"
        Char => "char"
        Byte => "byte"
        Ptr => "ptr"
        Uptr => "uptr"
        Prim as p => prim_kind_reserved_name(p.kind)
        _ => null
    }
}
```
`prim_kind_reserved_name` mapeia `U8→"u8"` … `F64→"f64"`, `Bool→"bool"`, `Isize→"isize"`,
`Usize→"usize"`.

2. Em `type_method_call`, ANTES da linha 1624, quando `recv_t` NÃO é `Named`: se
   `prim_reserved_name(recv_t)` dá um nome E `type_table_find(table, nome, "")` acha uma
   `TypeDecl` com `NewtypeBody` cujos `methods` contêm `mc.method` → tratar `struct_name = nome`
   e cair no MESMO caminho `NewtypeBody` (linha 1635 em diante). Se não acha método → NÃO
   interceptar (segue o erro atual, para não engolir diagnósticos).

3. `self` do método liga ao receptor builtin (mesma-rep). O corpo do método (TY-C2) delega ao
   builtin solto, então a chamada emitida é o wrapper `teko__types__str__concat(self,b)`; o
   fixpoint valida a igualdade.

4. Preservar as precedências existentes: os intrínsecos `type_ptr_wrap`/`type_ptr_unwrap`
   (`__wrap`/`__unwrap`) e `type_flags_method` continuam ANTES da ponte. A ponte só cobre o
   `"method typing is deferred"` que hoje é dead-end.

5. Nada a remover (additiva). Sem `//`. Doc W15 só se algum helper virar `exp` (não vira).

## Rulings & laws

- **Teko-only.**
- **Ensino AGORA (dono 2026-08-19):** a superfície (aceitar `s.metodo()`) ensina-se já; o USO
  pesado (migrar call-sites) vem nos lotes TY-M*.
- **NÃO detectar o inexistente:** a ponte só dispara quando existe reserved-newtype com o método;
  fora disso o erro atual permanece — sem ramo morto.
- **Fork protocol (2026-08-19):** capacidade nova deliberada em D145; sem HALT.
- **W15 full Javadoc; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; esta é byte-preserving → `[dry]`
  + fixpoint trivial (sem mudança de byte emitido). Ratchet: additiva inerte → pico não cresce.

## Fixtures

`none — the fixpoint self-build exercises this` (a ponte fica exercitada quando TY-C2/TY-M* usam
métodos; até lá é inerte e o fixpoint prova byte-identidade).

## Gate

`[dry]` — compila + fixpoint trivial (gen2==gen3 byte-idêntico, pois inerte sem decls). "Green" =
a árvore compila com a ponte presente e ZERO mudança de C emitido. Reseed-class: `none`.

## Deps

`MEM-W5` (a onda types.tks entra após o sweep W1-W5).

## Done when

`type_method_call` despacha métodos sobre receptores builtin quando existe a reserved-newtype
correspondente, sem alterar o C emitido da árvore atual (fixpoint byte-idêntico).
