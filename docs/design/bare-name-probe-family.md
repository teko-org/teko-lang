---
section: design-open
created: 2026-07-27
source: achados residuais da carga cargo/20-mono-order (vagão 20, .31)
status: DOIS PONTOS ABERTOS — nenhum fechado nesta versão
---

# A família "sonda por nome nu" — o defeito que já apareceu três vezes

Este documento existe porque a mesma falha foi encontrada e corrigida **três vezes em pontos
diferentes** do checker, sempre descoberta por acidente e sempre tratada como caso isolado. Ela não
é um caso isolado: é uma **família**, e a família tem membros ainda abertos. Registrá-los aqui é o
que impede a quarta descoberta acidental.

## A forma da falha

Um nome escrito no código-fonte é **relativo ao namespace que o escreveu**. Quando o checker
resolve esse nome consultando a tabela por **nome nu** — sem passar o namespace de referência — a
tabela responde com o **primeiro registro que casa**, e "primeiro" é a ordem em que os itens do
programa foram registrados, que é a ordem em que o build descobriu as fontes.

O resultado não é um erro: é uma resposta **plausível e errada**. O compilador segue adiante com o
símbolo de outra pessoa. E como a resposta depende da ordem de descoberta, o mesmo código produz
resultados diferentes em máquinas diferentes — que é a razão de a família ser **inimiga direta do
fixpoint** `gen2 == gen3` byte-idêntico.

**Por que ficou invisível tanto tempo:** enquanto a descoberta de fontes vinha do `readdir`, a
ordem era instável mas ninguém a lia como contrato — as falhas eram intermitentes e culpavam-se
outras coisas. Ordenar a descoberta (vagão 20) tornou a ordem **estável**, e uma ordem estável e
errada é reproduzível: foi aí que os membros da família começaram a cair.

## Os três já fechados

| # | onde | o que a sonda nua devolvia | fechado em |
|---|---|---|---|
| 1 | `canon_class_bases` — eixo de interfaces | a base homônima de outro namespace | #152 |
| 2 | `instantiate_types` — base genérica do sítio de instanciação | a decoy **não-genérica** `q006::Box` no lugar de `q084::Box<T>`; a checagem de aridade descartava o sítio e `Box__g__i64` nunca era carimbado | `37b44d28` (carga `cargo/20-mono-order`) |
| 3 | fragmento de argumento de instância genérica | `Gen<teko::io::Reader>` e `Gen<teko::emit::Reader>` colidiam no mesmo símbolo | #109 W3, via `mangle_ns_frag` |

O #2 é o mais instrutivo porque mostra que a falha **não precisa de duas genéricas homônimas** para
morder: bastou uma genérica e uma não-genérica com o mesmo nome nu em projetos distintos do mesmo
build.

## PONTO ABERTO 1 — `ConstraintAtom` carrega nome nu

`src/checker/resolve.tks`, três sítios sobre o mesmo campo `a.name`:

- linha ~481 — `parser::ConstraintAtom as a => atom_surface(a.name, table)`
- linha ~577 — `if is_trait_name(a.name, table)` / `if is_interface_name(a.name, table)`

`a.name` é o nome como escrito na cláusula de constraint, **sem namespace de referência**. As três
sondas consultam a tabela por esse nome nu. Duas interfaces homônimas em namespaces distintos, ou
uma interface e um trait homônimos, resolvem para o que registrou primeiro.

O agravante deste sítio é o **modo de falha silencioso**: `constraint_interfaces` devolve lista
vazia quando o nome não é nem trait nem interface. Uma resolução para o símbolo errado que por
acaso não seja nenhum dos dois não vira erro — vira **constraint vazia**, isto é, uma restrição
genérica que simplesmente deixa de restringir. Um `where T: Comparable` que silenciosamente não
exige nada é pior que um erro de compilação.

**O que fechá-lo exige:** `ConstraintAtom` precisa carregar o namespace de escrita, exatamente como
`InstSite` passou a carregar (foi assim que o #2 fechou). É mudança de AST (`src/parser/ast.tks`),
não só de checker — por isso não coube no vagão 20.

## PONTO ABERTO 2 — o mangle de instância perde o namespace da BASE

`src/checker/resolve.tks:1795`:

```
pub fn generic_inst_name(base: str, args: []Type): str
```

O nome carimbado de uma instância genérica é `<base>__g__<arg0>[__<arg1>…]`. Os **argumentos** são
qualificados: `mangle_ns_frag` troca cada `::` por `__`, e o comentário que o acompanha diz por que
ele existe, com todas as letras:

> *Keeps a generic-instance name (`Gen__g__teko__emit__Reader`) a valid C identifier AND distinct
> from `Gen<teko::io::Reader>` — `name_last_segment` would collide the two.*

A **base**, não. No sítio de `resolve.tks:2515` ela é extraída assim:

```
nt.path.segments[nt.path.segments.len - 1].name
```

— o último segmento, com o namespace explicitamente descartado. Logo `a::Box<i64>` e `b::Box<i64>`
carimbam **o mesmo** `Box__g__i64`.

**A assimetria é o achado.** O projeto já diagnosticou esta colisão exata, já escreveu a correção
(`mangle_ns_frag`), e aplicou-a **a um lado só** do nome mangleado. O lado que ficou de fora é o
lado que nomeia o tipo.

**O que hoje mascara a colisão** — e é a razão de não haver falha observada: `resolve_bare_g_instance`
(linha ~1798) resolve um nome carimbado nu a partir de **qualquer** namespace, deliberadamente, e é
consultado ANTES do erro de colisão entre namespaces. Isso é correto para o propósito dele (um
`Box__g__i64` bare num `-> Box<T>` precisa resolver), mas significa que duas bases homônimas não
produzem erro de colisão: produzem **uma** instância, e quem a recebe é quem registrou primeiro.
Ordem de registro de novo.

**Estado da prova:** o mecanismo está estabelecido por leitura de código (o descarte do namespace em
2515 é explícito, e `resolve_bare_g_instance` é explícito sobre resolver de qualquer namespace).
**Não há teste que o demonstre** — o corpus atual não tem duas genéricas homônimas em namespaces
distintos. O teste que provaria é o mesmo padrão do `instantiate_order_test.tkt`: dois projetos no
mesmo build, cada um com sua `Box<T>` genérica de layout diferente, instanciadas com o mesmo
argumento, exigindo dois símbolos distintos. **Escrever esse teste é o primeiro passo do fecho, e
ele deve ser escrito mesmo que passe** — se passar, a premissa deste ponto cai e isso também é
resultado.

## A lição de método, que vale mais que os dois pontos

O #2 foi encontrado porque uma carga **ordenou** a descoberta de fontes e a ordem estável expôs uma
dependência de ordem que a instável escondia. A regra geral:

> **Determinismo não é só um requisito do fixpoint — é um instrumento de diagnóstico.** Tornar uma
> ordem estável não conserta a dependência de ordem; ela a torna **reproduzível**, e só então
> testável.

Corolário operacional: sempre que uma sonda de tabela for escrita, a pergunta de revisão é *"qual
namespace está perguntando?"*. Se a resposta for "nenhum", é membro desta família até prova em
contrário.
