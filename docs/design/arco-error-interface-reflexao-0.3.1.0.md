# O arco `error`-como-interface — reflexão de tipos, propriedades em interface, interface em união

> **Status:** MEDIÇÃO E DESENHO. Zero código de produto. Não toca no degrau 22, que já está
> despachado.
> **Base:** `cargo/0.3.1.0-degrau-22-desenho` @ `6e05e4f` (sobre `8c57bcf`).
> **Medido com:** `.gen1b`, sondas em projectos descartáveis fora da árvore.

## Resposta de uma linha

**Quatro lanes em sequência — mas existe um subconjunto mínimo que entrega o `errors.As` SOZINHO,
numa lane só, independente das outras três, e que NÃO faz o valor de interface crescer: o teste de
tipo por identidade de vtable, que são DUAS instruções que já existem (`LGlobalAddr` + `ICmpEq`).**

---

## 0. A peça de arquitectura do dono — confirmada, sem nada a contrariar

> *"os métodos que sugeri seriam açúcar para funções reais em um namespace (que pode forçar
> disponível na superfície)"*

**Confirmado, e a medição anterior já o sustentava sem o saber.** `error::new("x")` e
`teko::error::new("x")` parseiam hoje porque `error` é um identificador comum
(`src/checker/scope.tks:385`), não um token do lexer; o `teko::error::*` já é um namespace de
builtins povoado (`err_loc`, `err_typed`). A fábrica é **resolução de nome num namespace**, o mesmo
mecanismo dos 40+ builtins que já lá vivem.

**Consequência que fixa o desenho:** a fábrica **não toca em interfaces, em vtables, em dispatch nem
na regra da variante**. As duas coisas são ortogonais em ambos os sentidos — a fábrica não precisa
de nada da interface, e a interface não fica mais barata por a fábrica existir. **Nada mediu que
contrarie isto.** A ordem `degrau 22 → fábrica` fica de pé exactamente como estava, e o arco desta
página corre em paralelo ou depois, à escolha do dono.

Nota de coerência: em Teko não há `static` em interface e não há `impl`/extensões, logo um "método
de fábrica" nunca poderia ser outra coisa senão função de namespace. A arquitectura do dono é a
**única** que a linguagem admite — não é uma preferência, é a forma disponível.

---

## A. Reflexão de tipos — o núcleo, e é mais barato do que parece

### A.1 O que já existe

| peça | onde | serve? |
|---|---|---|
| **`LGlobalAddr`** — endereço de um símbolo global nomeado, num VReg `Ptr` | `src/lir/lir.tks:100-104`, construtor `global_addr_inst` em `:342` | **SIM — é a peça-chave** |
| **`ICmpEq`** — comparação de igualdade inteira/ponteiro | `src/lir/lir.tks:36` (`LBinOp`) | **SIM** |
| **valor de interface `{data@0, vtable@8}`** | `lower_iface_fatptr`, `src/lir/lower.tks:3073-3084` | **SIM — já carrega a identidade** |
| **símbolo de vtable `tk_vt_<Class>_<Iface>`** | `src/codegen/codegen.tks:5684`, `:6351`, `:10863` | **SIM — único por par (classe concreta, interface)** |
| **`lower_iface_slot`** (índice de método na vtable) | `src/lir/lower.tks:3099` | não — é dispatch, não identidade |
| **tag de variante** (`store_variant_tag`, `I32` ao offset 0) | `src/lir/lower.tks:6061` | **NÃO** — ver A.2 |

### A.2 A tag de variante NÃO serve, e é importante dizê-lo

`store_variant_tag` escreve um `I32` ao offset 0 do wrapper, e `variant_member_index_of`
(`lower.tks:5728`) resolve esse índice **por tipo, em tempo de compilação**. É um **índice
posicional dentro de UMA declaração de união** — o `error` é o índice 1 em `str | error` e pode ser
o índice 0 noutra união. **Não é identidade de tipo, é posição.** Quem procurar reflexão na tag
encontra um número que não significa nada fora daquela declaração.

### A.3 A identidade de tipo JÁ EXISTE em runtime, e ninguém a usa

O símbolo de vtable é `tk_vt_<Class>_<Iface>` — **embebe o nome da classe concreta no símbolo**. É
um objecto estático, um por par (classe, interface), com endereço único no processo. Portanto:

> **Dois valores de interface cujo `vtable` é o mesmo endereço têm a mesma classe concreta.
> A identidade de tipo é o ponteiro de vtable. Já está lá, já é carregada, já é comparável.**

### A.4 A superfície mínima do `errors.As`, e o seu custo

O equivalente a `errors.As` — *"o valor por trás desta interface é do tipo `T`? dá-me `T | null`"* —
reduz-se a **comparar `value.vtable` com o endereço do símbolo `tk_vt_<T>_<Iface>`**:

```
  LGlobalAddr  v1 <- "tk_vt_SimpleErr_ErrIface"     // já existe
  LBin ICmpEq  v2 <- (value.vtable, v1)             // já existe
  LBranch      v2 -> arm_yes, arm_no                // já existe
```

No braço `yes`, `value.data` **é** o endereço de um `T` — nenhuma conversão, nenhuma cópia. Na rota
C é a mesma comparação, escrita `v.vtable == &tk_vt_T_I`.

**Superfície proposta (zero gramática nova):** reutilizar a gramática de padrões que o `match` já
tem, com braços que nomeiam classes concretas — a mesma forma de
`match v { str => …; error as e => … }`:

```teko
/**
 * describe — interrogar a classe concreta por trás de um valor de interface, o equivalente ao
 * `errors.As` do Go. Cada braço compara o ponteiro de vtable do valor com o símbolo estático da
 * classe que nomeia (`tk_vt_<Class>_<Iface>`); o braço `_` é o "não é nenhum destes".
 *
 * @param e  o valor de interface a interrogar
 * @return   a descrição da classe concreta encontrada
 */
fn describe(e: ErrIface): str {
    match e {
        SimpleErr as s => s.msg
        PathErr as p => p.path
        _ => "desconhecido"
    }
}
```

Só uma regra nova de tipagem no checker (um braço de `match` cuja etiqueta é uma classe que
implementa o contrato do receptor). **Zero tokens novos, zero mudança de gramática, zero tabela de
metadados, zero registo de type-ids.**

### A.5 O custo em bytes — **NÃO CRESCE. Digo alto, como pediste.**

| | hoje | com o teste de tipo |
|---|---|---|
| valor de interface | `{data@0, vtable@8}` = **16 bytes** | **16 bytes** |
| cabe no slot gordo do wrapper de 24 bytes? | sim (`payload@8` + `len@16`) | **sim, inalterado** |
| metadados por tipo | vtable (já emitida) | **a mesma vtable, zero acrescento** |
| metadados por valor | nenhum | **nenhum** |

**A identidade não é acrescentada — é lida de onde já estava.** Isto respeita os dois requisitos do
dono à letra: é **reflexão de tipos e não de construção** (não dá para instanciar `T` a partir do
teste, só reconhecê-lo) e **não há invocação dinâmica de biblioteca nativa** (é uma comparação de
ponteiros contra um símbolo resolvido pelo ligador em tempo de compilação).

**Limite honesto:** isto dá identidade e narrowing. **Não** dá enumeração de campos, nomes de
campos, nem construção por nome. Para o `errors.As` é exactamente o suficiente; para "reflexão"
no sentido lato, não é. Vale a pena nomear a facilidade pelo que faz — **teste de tipo**, não
"reflexão" — para não prometer a superfície larga.

---

## B. Interface como membro de variante — porque foi posta, e o que custa levantar

### B.1 Porque foi posta: **de propósito, com dono e com plano escrito**

`src/checker/resolve.tks:1682-1683`, dentro de `variant_member_admissible`. O doc-comment da própria
função (`:1655-1657`) nomeia-a:

> *"An interface `Named` remains an honest stop (the **#28 S4 carve-out** — interface-in-union
> rebases on top of null-union and relaxes this arm itself)."*

O plano está em dois documentos ratificados:

- `docs/design/interface-value-type.md` §5, **Crumb S4**: *"Do NOT relax `resolve.tks:1397` here.
  … add a compile-fail fixture that PINS the current honest-stop (so null-union's Crumb 2, which
  edits that exact validation, **must consciously decide** the interface-in-union arm rather than
  silently flip it)."*
- `docs/design/null-union-c3-c7-0.3.0.30.md` §"Interface-in-union (#28 S4 carve-out)": *"C3's
  decision: keep rejecting `Iface | …` here; interface-in-union rebases ON TOP of the null-union
  base and relaxes that arm itself. **Write `variant_member_admissible` so adding the interface arm
  later is a one-line relaxation, not a rewrite.**"*

**O "yet" da mensagem é literal: foi escrita como travão temporário, deliberado, para forçar uma
decisão consciente.** E a instrução foi cumprida — a função **está** escrita para que o
levantamento seja a remoção de um `if` de duas linhas.

### B.2 O que custa levantar, medido por camada

| camada | custo | estado |
|---|---|---|
| **checker** (`variant_member_admissible`) | **2 linhas** removidas | pronto, por desenho |
| **rota C, `Iface \| null`** (caminho de nicho) | `cg_type_is_niche_able` (`codegen.tks:1868-1877`) trata `Named` só por `cg_is_class_named` — **uma interface devolve `false`** | **a recomendação de desenho NÃO foi cumprida** — ver §E |
| **rota C, `Iface \| T`** (caminho com tag) | segue o caminho genérico já existente | provável zero |
| **backend nativo** | `collect_struct_layouts` (`lower.tks:9902`) **exclui explicitamente interface**: *"a declaration that is not field-shaped (enum/flags/variant/alias/extern/interface/trait) contributes no layout entry"*. Logo `aggregate_box_bytes` → `named_layout_bytes` → `null` → **paragem honesta** | gap nomeado, dimensão de degrau |

**A saída nativa mais barata não é registar um layout de interface** — é reconhecer o par
`{data, vtable}` como membro **GORDO** do wrapper (`store_fat_variant_payload_pair`, que o degrau 21
já factorizou), porque são exactamente duas palavras e o slot gordo do wrapper tem exactamente duas
palavras (`payload@8`, `len@16`). **Sem caixote, sem cópia, sem crescer o wrapper.** É o mesmo
molde que o `str_from_utf8` do degrau 21 usa para o seu braço `ok`.

**Total de B: ~1 lane**, dominada pelo braço nativo, com o checker a custar 2 linhas.

---

## C. Propriedades em interface — o custo das duas saídas

### C.1 A rejeição é no PARSER, não no checker

Sonda: uma interface com `msg: str` em vez de `fn` é rejeitada em
`src/parser/parse_decl.tks:348` com `` expected `fn` ``. **É mudança de gramática**, não relaxamento
de regra semântica — mais cara do que B, que é só checker.

`src/parser/ast.tks:520`: `pub type InterfaceBody = struct { extends: []str; methods: []Function }`.
Um terceiro campo toca **46 sítios consumidores fora do parser**.

### C.2 A tensão de lei, e a sua resolução

`docs/design/interface-value-type.md:113-117` diz da rejeição de acesso a campo
(`typer.tks:2164`):

> *"**Correct and permanent**: a fat pointer has no user fields. **KEEP.**"*

Declarar propriedades em interface parece contradizer uma decisão ratificada. **Não contradiz — e a
resolução é a própria arquitectura do dono.** Um ponteiro gordo genuinamente não pode carregar um
offset de campo: dois implementadores põem `msg` em offsets diferentes, e o valor de interface não
sabe qual é. Logo **uma propriedade em interface só pode ser açúcar para um getter na vtable** —
exactamente o *"açúcar para funções reais"* que o dono escreveu. Sob essa leitura,
`typer.tks:2164` continua verdadeiro **ao nível da representação** e a superfície só ganha açúcar.

**Passa em todas as Leis. Sem HALT.**

### C.3 As duas saídas, medidas

| | **Saída 1 — sem propriedades** (leituras viram chamadas) | **Saída 2 — propriedades como açúcar de getter** |
|---|---|---|
| edições no corpus | **118**, em **18 ficheiros** | **0** — o texto `e.message` mantém-se válido |
| quebra de superfície para utilizadores | **sim**, todo o programa que lê `.message` parte | **não** |
| gramática | inalterada | **muda** (`parse_decl.tks:348`) |
| `InterfaceBody` + consumidores | inalterado | +1 campo, **46 sítios** |
| conformidade + slot de vtable | inalterado | gerar slot de getter por propriedade |
| custo em runtime | 118 chamadas indirectas | **as mesmas 118 chamadas indirectas** |

**A saída 2 é mais barata no que conta.** As duas pagam o mesmo em runtime (o açúcar não elimina o
dispatch — só o esconde), mas a saída 1 paga **118 edições e uma quebra de superfície** por cima. A
saída 2 concentra o custo no compilador, onde é pago uma vez, em vez de o espalhar por todo o
código que já existe e por todo o código de utilizador que existir.

**Alerta de vazão, reportado:** as duas saídas trocam **118 cargas de memória por 118 chamadas
indirectas**, e 107 delas estão no caminho de diagnóstico do próprio compilador. A lane tem
doutrina de vazão com portão duro (`null-union-c3-c7-0.3.0.30.md` §6.3). Isto tem de ser **medido
antes**, não depois — e é a razão mais forte para o `error` **não** ser interface, independentemente
de tudo o resto nesta página.

---

## D. As 123 leituras — o número

Recontagem exacta em `src/**/*.tks`:

| leitura | ocorrências |
|---|---|
| `.message` (total) | 112 |
| menos as que estão sobre receptores NÃO-`error` (`lc.`/`r.`/`res.`) | −5 |
| **`.message` sobre um `error`** | **107** |
| `.line`/`.col`/`.expected`/`.actual` sobre um `error` | **11** |
| **TOTAL** | **118** |
| ficheiros tocados | **18** |

(A estimativa anterior de 123 estava 5 acima: contava leituras `.message` de outras estruturas.)

Com **propriedades como açúcar (saída 2): 0 edições.** Sem propriedades **(saída 1): 118 edições
em 18 ficheiros**, todas mecânicas, mais uma quebra de superfície para todo o código de utilizador.

---

## E. Achado adjacente — REPORTADO, não convertido em issue

`null-union-c3-c7-0.3.0.30.md` recomendou explicitamente:

> *"Recommend C3's niche classifier be written to recognize an interface `Named` fat-pointer shape
> as a niche member ALREADY (**dormant** until interface-in-union relaxes the resolve gate), so the
> keystone only relaxes the GATE, never adds a rep path."*

**Não foi cumprido.** `cg_type_is_niche_able` (`src/codegen/codegen.tks:1868-1877`) trata `Named`
só através de `cg_is_class_named` — uma interface devolve `false`. O reconhecimento dormente não
existe. Consequência: quem levantar a regra em `resolve.tks:1683` vai encontrar `Iface | null` a
cair no caminho **com tag** em vez do caminho de **nicho**, contra o desenho ratificado, e
silenciosamente (é uma escolha de representação, não uma paragem). São ~2 linhas em
`cg_type_is_niche_able`, mas têm de ser **acrescentadas com o levantamento**, não descobertas
depois.

---

## F. O tamanho do arco, e o subconjunto mínimo

### F.1 As quatro lanes

| # | lane | depende de | valor sozinha | dimensão |
|---|---|---|---|---|
| **1** | **teste de tipo por identidade de vtable** (`errors.As`) | **nada** | **alta** — serve toda a interface, não só o `error` (as famílias `Reader`/`Writer` de `src/io/stream.tks` ganham já) | **1 lane curta** — 2 instruções LIR já existentes + 1 regra de tipagem, **zero crescimento** |
| **2** | interface como membro de variante | null-union (fechada) | média | ~1 lane — checker 2 linhas, nicho C 2 linhas, braço gordo nativo dimensão-degrau |
| **3** | propriedades em interface (açúcar de getter) | 2 | baixa isolada | 1–2 lanes — gramática + AST + 46 consumidores + conformidade + slots |
| **4** | `error` como interface | 1+2+3 | é o objectivo | ~1 lane de migração + o portão de vazão da §C.3 |

**Arco total: quatro lanes em sequência.** Não é uma lane. Não cabe numa esteira a fechar.

### F.2 O subconjunto mínimo — **existe, e é a lane 1 sozinha**

> **O teste de tipo NÃO precisa que o `error` seja interface.** É uma facilidade sobre valores de
> interface em geral. Entrega o `errors.As` **hoje**, para qualquer interface, sem a lane 2, sem a
> lane 3 e sem a lane 4.

O que a torna o candidato óbvio a destacar:

- **não faz nada crescer** — o valor de interface fica em 16 bytes, o wrapper de variante fica em 24;
- **não usa peça nova nenhuma** — `LGlobalAddr` e `ICmpEq` já existem e já são emitidas;
- **não muda a gramática** — reutiliza os padrões de `match`;
- **não quebra superfície** — é aditiva;
- **fecha os pontos 1 e 4 do dono ao mesmo tempo**, que era exactamente o que ele pediu que se
  medisse;
- **é independente do degrau 22 e da fábrica** — corre em qualquer ordem em relação a ambos.

### F.3 A recomendação de agendamento

**Para a esteira a fechar: nada deste arco entra.** Nem sequer a lane 1 — é aditiva e boa, mas é
superfície nova de linguagem e paga ciclo de semente.

**Depois da promoção, por esta ordem:**

```
[promoção]
  → lane 1: teste de tipo (errors.As)            ← autónoma, alto valor, zero crescimento
  → [decisão do dono: o arco continua?]
  → lane 2: interface em união
  → lane 3: propriedades em interface
  → lane 4: error como interface  ← só depois de MEDIR a vazão das 118 chamadas indirectas
```

A decisão do dono depois da lane 1 é real e barata de tomar: com o `errors.As` a existir, a
pergunta *"o `error` precisa mesmo de ser interface?"* volta a abrir-se, porque **a maior parte do
valor que motivava a troca — interrogar um erro tipado — já terá sido entregue sem tocar no
`error`**.

---

## G. Riscos e tensões de lei

| # | risco | resolução |
|---|---|---|
| G1 | "reflexão" prometer superfície larga (campos, construção) | **nomear a facilidade `teste de tipo`, não `reflexão`.** Dá identidade e narrowing; não dá enumeração nem construção — o que o dono pediu ("de tipos, não de construção") |
| G2 | propriedades em interface contra `interface-value-type.md:113-117` ("permanent, KEEP") | **sem tensão**: uma propriedade só pode ser açúcar de getter na vtable (dois implementadores põem o campo em offsets diferentes). A representação mantém-se sem campos; só a superfície ganha açúcar. É a arquitectura do dono, à letra |
| G3 | **vazão**: 118 cargas viram 118 chamadas indirectas, 107 no caminho de diagnóstico do compilador | **medir ANTES da lane 4**, contra o portão duro de vazão. É a razão mais forte contra o `error` ser interface, e é independente de todo o resto |
| G4 | levantar `resolve.tks:1683` sem o nicho dormente (§E) | acrescentar as ~2 linhas de `cg_type_is_niche_able` **no mesmo commit** do levantamento |
| G5 | lei da semente | cada peça de superfície nova (lanes 1, 3) paga um ciclo. Sequenciar depois da promoção |

**Nenhuma tensão de lei genuína. Nada a HALTAR.**
