# Censo do eixo `pt` da spine

Medição de 2026-07-31, ramo `cargo/0.3.1.0-censo-spine-pt`, semente `teko 0.3.0.31-beta`.
Corpus: o próprio compilador (`src` + `main.tks`, 143 arquivos).

Instrumentação: `src/checker/pt_census.tks` (o contador), `src/checker/pt_census_test.tkt`
(a prova de que o contador conta), `report_pt_census` em `src/build/project.tks` (as duas
linhas que o build imprime). O eixo não foi tocado; nada passou a consumi-lo.

---

## 1. O número

```
teko: pt-census-liveness: NOT LIVE — PtFrame LIVE (21047), PtRoot DEAD (0); a DEAD arm is the axis never producing it (the counter itself is proven on all five arms by pt_census_test.tkt)
teko: pt-census: 4925 fns, 21074 cells (4.28/fn, fattest 67), PtFrame 21047 (99.87%), PtRoot 0 (0.00%), PtParam 27 (0.13%), PtAdopter 0 (0.00%, 0 at top), PtTop 0 (0.00%), unit = cell (deduped name/field key per function)
```

| caso | células | fração |
|---|---:|---:|
| `PtFrame` | 21 047 | **99,87 %** |
| `PtRoot` | 0 | 0,00 % |
| `PtParam` | 27 | 0,13 % |
| `PtAdopter` | 0 | 0,00 % |
| — destes, em `⊤` | 0 | 0,00 % |
| `PtTop` | 0 | 0,00 % |
| **total** | **21 074** | 100 % |

Funções que contribuíram: **4 925**. Média: **4,28 células por função**. Função mais gorda:
**67 células**. A distribuição não é dominada por poucas funções gordas — a razão
maior/média é ~16, e com 21 074 células em 4 925 funções nenhuma função sozinha pesa mais
de 0,32 % do total.

Células em `⊤` (o recuo por orçamento de profundidade): **0**. Veja §4 — o recuo não existe
no código, portanto esse zero não diz nada sobre o orçamento estar apertado ou folgado.

---

## 2. A unidade: é por CÉLULA, e uma célula não é um binding

A unidade contada é a **célula** da spine (`Cell`, `src/checker/spine.tks:31`), e ela **não**
é o que a palavra sugere. A linha impressa carrega a unidade no fim (`PT_CENSUS_UNIT`)
justamente para que ninguém a leia como outra coisa.

Uma célula é uma chave `(name, field)` **deduplicada por função** (`add_cell`,
`src/checker/spine.tks:246`). Consequências, todas verificadas:

* **Dois `let x` em blocos disjuntos da mesma função são UMA célula.** Sombreamento e
  rebind colapsam. Provado em `pt_census_counts_a_rebound_name_once`
  (`src/checker/pt_census_test.tkt`).
* **Cada parâmetro é uma célula**, seja ou não `Ref<T>` (`add_param_cells`,
  `src/checker/spine.tks:259`). Boa parte das 21 074 células é parâmetro, não local.
* **Um caminho de UM salto `x.f` é célula própria**, ao lado do `x` nu. `a.b.c` não é célula
  nenhuma — colapsa e não entra na contagem.
* **Um `let` com padrão de destructuring não nomeia célula alguma** (`add_binding_cell`,
  `src/checker/spine.tks:323`): nomeia zero.
* A contagem não é por **local de alocação**, não é por **ocorrência de binding**, e não é
  por **definição SSA**. É por nome-vivo-dentro-de-uma-função.

Ordem de grandeza que confirma a leitura: o corpus tem 9 504 linhas iniciadas por `let` ou
`mut` e 4 826 declarações `fn` (contagem por `grep`). 21 074 células com apenas 9 504
bindings sintáticos só fecha porque **os parâmetros dominam**.

**Se o seu modelo mental era "uma célula = um objeto alocado", o número muda de
significado**: 99,87 % é a fração dos NOMES que a spine deixa no chão do reticulado, não a
fração dos bytes nem dos objetos.

---

## 3. A vivacidade FALHOU, e esse é o achado principal

O braço pedido — afirmar `PtFrame > 0 && PtRoot > 0` antes de reportar — **não passa**:
`PtRoot = 0` sobre o corpus inteiro.

O contador não está quebrado. `src/checker/pt_census_test.tkt` conduz os cinco braços a
partir de um `Spine` construído à mão e afirma cada um, inclusive um eixo só de `PtRoot`
(`pt_census_counts_a_root_only_axis`) e o `PtAdopter(⊤)`
(`pt_census_counts_every_lattice_arm`). O zero é do **eixo**.

**`PtRoot` nunca é construído em código de produção.** A única construção de `PtRoot { }` em
toda a árvore está na fixture: `src/checker/spine_test.tkt:291`. O caminho de produção é
fechado:

* `seed_pt` (`src/checker/spine.tks:389`) semeia **só** `PtParam` (para um parâmetro
  `Ref<T>`) ou `PtFrame`. Nada mais.
* A única transferência do eixo, `pt_transfer_*` (`src/checker/spine.tks:566`–`696`), só
  junta `PtAdopter` (via `join_pt_adopter_at`, `src/checker/spine.tks:562`).
* `pt_join` (`src/checker/spine.tks:504`) só produz `PtTop` — e só quando já existem dois
  locais concretos distintos, o que exige adotantes.

**O que isso faz ao número.** 99,87 % **não** significa "99,87 % das células são
provadamente locais de moldura". Significa **"99,87 % das células nunca foram levantadas do
chão do reticulado"**, porque o eixo não tem nenhuma regra que alguma vez levante uma célula
a `PtRoot`. O caso `PtRoot` — a fuga segura, justamente o caso que distinguiria uma célula
que sobrevive à moldura de uma que morre com ela — está **morto** hoje.

Portanto:

* Como **teto**, o número é honesto e é o que foi pedido: nenhuma limpeza por escopo pode
  recuperar mais do que as células que a spine classifica como locais de moldura, e são
  99,87 %. O teto é ~100 %.
* Como **estimativa do que é recuperável**, o número é **vazio de informação**: um eixo cujo
  denominador de fuga é estruturalmente zero classifica tudo como fundo por construção. O
  teto de 99,87 % **não desempata nada** — não separa candidatos de não-candidatos, porque o
  eixo ainda não sabe dizer que uma célula escapa.

Ou seja: o eixo `pt` mede hoje a **confinação em `adopt { }`** (foi para isso que a
transferência foi escrita, §5.1 do plano da spine), e **não** a fuga da moldura. Usar a
fração `PtFrame` dele como proxy de "quanto é local" é ler um instrumento fora da escala
dele.

---

## 4. `PtAdopter` = 0 e `⊤` = 0 — dois zeros com causas diferentes

**`PtAdopter = 0` tem causa banal e verificada: o corpus do compilador não contém um único
bloco `adopt { }`.** Toda ocorrência de `adopt` em `src/` é o parser reconhecendo a palavra
(`src/parser/parse_stmt.tks:50`, `:121`) ou prosa em doc-comment. Zero usos. Como a única
transferência do eixo é a confinação em adotante, um corpus sem adotantes deixa o eixo
inteiro no chão. Isso explica, sozinho, tanto `PtAdopter = 0` quanto `PtTop = 0`.

**`⊤` = 0 tem causa diferente e é achado próprio: o recuo por orçamento NÃO EXISTE no
código.** O doc-comment de `PtAdopterId` (`src/checker/spine.tks:55`–`63`) promete que
"`top` marca o recuo seguro `PtAdopter(⊤)`: qualquer alocação dentro de QUALQUER adotante
cujo id de região preciso exceda o orçamento de uma função". Não há orçamento nenhum na
árvore. O único ponto que constrói um `PtAdopterId` é `join_pt_adopter_at`
(`src/checker/spine.tks:562`), e ele escreve `top = false` **literal**, sem comparação com
limite algum; `state.next_region` (`src/checker/spine.tks:522`) incrementa sem teto.
`top = true` só é construído em `src/checker/spine_test.tkt:631`.

Logo: **"0 células em `⊤`" não pode ser lido como "o orçamento está folgado"**. Não há
orçamento. Se um dia houver adotantes de verdade no corpus, o recuo prometido não dispara e
o reticulado cresce sem teto em número de regiões.

---

## 5. O eixo NÃO é calculado hoje em todas as funções

A premissa de que o censo sairia de graça porque "o eixo já é calculado em todas as funções"
**não se confirma**. `check_ref_storability` (`src/checker/typer.tks:6026`) tem uma guarda
antes da chamada:

```
fn check_ref_storability(tf: TFunction): error | null {
    if !fn_has_ref_param(tf) {
        if !stmts_have_free(tf.body) { return null }
    }
    check_ref_storability_block(tf.body, fn_spine(tf))
}
```

`fn_spine` só roda para uma função que tenha um parâmetro `Ref<T>` **ou** um
`teko::mem::free` no corpo. No corpus: **3** parâmetros escritos `Ref<` e **17** chamadas
`teko::mem::free(` (contagem por `grep`; o censo mediu 27 células `PtParam` depois da
monomorfização, que carimba cópias). Isso é da ordem de **~20 funções em 4 925 — ~0,4 %**.

O censo re-roda `fn_spine` para as 4 925. É trabalho novo, não reaproveitado: nada é
compartilhado com a guarda, nada é cacheado.

---

## 6. Método

* `pt_census(prog)` (`src/checker/pt_census.tks`) percorre `prog.items`, roda `fn_spine(f)`
  para cada `TFunction` e dobra o array `pt` célula a célula. Um item que não seja função
  contribui zero.
* O programa medido é o **programa final entregue ao backend** — depois da monomorfização e
  com os `#test` removidos no caminho de release. Por isso a contagem de funções (4 925) não
  bate com a de itens de codegen (5 941): estes incluem tipos, constantes e declarações.
* `report_pt_census` imprime no **início** de `codegen_and_report`, o rabo compartilhado de
  todos os caminhos de build, antes do backend — pela mesma razão que
  `report_arith_cast_rate` imprime cedo: é propriedade do programa tipado, logo tem de falar
  mesmo numa árvore cuja emissão falhe depois.
* **Duas linhas, sempre nesta ordem**: vivacidade primeiro, valores depois.
* O eixo não foi alterado. Continua consultado só por `check_ref_storability_block`.

### Custo — A/B limpo

Os dois binários comparados são **ambos geração 1 construídos pela MESMA semente**, a partir
da mesma árvore; a única diferença é a chamada `report_pt_census(prog)` presente num e
ausente no outro (`src/checker/pt_census.tks` é compilado nos dois casos, de modo que a
quantidade de fonte compilada é idêntica). Ambos construíram o mesmo projeto, na mesma
máquina, sem outra carga pesada minha.

| geração 1 | censo roda? | tempo | pico |
|---|---|---|---|
| com a chamada | sim | 84,7 s | 1612,5 MB |
| sem a chamada | não | 85,3 s | 1596,5 MB |

* **Tempo: sem custo mensurável.** A corrida com o censo saiu 0,6 s mais *rápida* que a sem
  — ou seja, o delta está abaixo do ruído. A variância entre execuções desta mesma árvore
  chegou a 14 s quando havia outros processos pesados na caixa (uma corrida contendida deu
  98,8 s). Um custo de 0,7 % não é distinguível desse ruído.
* **Memória: +16,0 MB de pico (+1,0 %)**, esse sim resolvido. É a alocação transiente de
  4 925 `Spine` construídos e descartados um a um.

Referência das outras corridas, para contexto: a semente construiu a árvore base em 91,6 s /
1711,0 MB e a árvore com o censo em 91,7 s / 1718,1 MB (a semente não tem o censo, logo essas
duas medem só o custo de compilar as ~340 linhas novas).

---

## 7. O que ficou por medir

1. **O custo em tempo não foi resolvido — foi limitado.** O A/B do §6 diz que o delta é menor
   que o ruído entre execuções desta caixa, não que ele é zero. Separar um custo de ~1 % do
   ruído exigiria dezenas de repetições, que não foram feitas. O delta de memória (+16,0 MB)
   esse foi resolvido com um par de execuções.
2. **Quantas funções chegam hoje a `fn_spine`, exatamente.** O §5 dá ~20 por `grep` sobre a
   fonte, não por contagem dentro do compilador: `fn_has_ref_param` e `stmts_have_free` são
   privadas de `typer.tks` e o censo não as replicou (replicá-las seria construir análise
   nova, fora do encargo).
3. **A distribuição por função, e não só a média e o máximo.** O censo reporta média (4,28) e
   a função mais gorda (67). Não há histograma, nem mediana, nem percentis: uma cauda gorda
   moderada seria invisível entre esses dois pontos.
4. **A que corresponde uma célula em bytes.** O censo conta nomes, não tamanhos. Não há
   ligação entre estas 21 074 células e as arenas medidas em
   `docs/medicoes/onde-esta-a-memoria-do-compilador.md`.
5. **O eixo sobre um corpus com `adopt { }`.** O compilador não tem nenhum. Todo o braço
   `PtAdopter`/`PtTop` do reticulado está por exercitar fora dos testes unitários, e o censo
   sobre outro corpus (por exemplo `examples/`) não foi rodado.
6. **A fração `PtFrame` sobre um eixo que soubesse produzir `PtRoot`.** É a medição que
   realmente desempataria a decisão, e ela não é possível hoje: exigiria a regra de fuga que
   o eixo não tem (§3). Não foi construída — é análise nova, e o encargo é contar, não
   construir.

---

## 8. Achados para o dono (nomeados, não corrigidos)

| # | ponto | o quê |
|---|---|---|
| A | `src/checker/spine.tks:389` (`seed_pt`) + `src/checker/spine.tks:562` (`join_pt_adopter_at`) | `PtRoot` não é construído por nenhum caminho de produção; a única construção está em `src/checker/spine_test.tkt:291`. O braço da fuga segura está morto. |
| B | `src/checker/spine.tks:55`–`63` (doc de `PtAdopterId`) vs `src/checker/spine.tks:562` | O recuo `PtAdopter(⊤)` por orçamento de profundidade está documentado mas não implementado: `top = false` é literal e não há teto sobre `next_region` (`src/checker/spine.tks:522`). |
| C | `src/checker/typer.tks:6026`–`6031` | `fn_spine` está atrás de uma guarda (`Ref<T>` no parâmetro **ou** `mem::free` no corpo); o eixo NÃO é calculado hoje em todas as funções, e sim em ~0,4 % delas. |
| D | `src/checker/spine.tks:246` (`add_cell`) | A unidade do eixo é o nome deduplicado por função, não o binding nem o local de alocação. Qualquer número derivado do eixo herda essa unidade. |

Nenhum desses foi corrigido: o encargo é contar, não atuar.

---

## 9. Ritual desta medição

* `TEKO_BACKEND=c teko . -o out --no-verify --release` — limpo, **zero avisos de qualquer
  espécie** (nem do compilador, nem do `cc`); 91,7 s, pico 1718,1 MB pela semente.
* `./out/teko test .` — **verde**: 292 testes rodados, 292 passaram, 0 falharam, 0 saíram;
  tier de regressões incluído; 26 min 18 s.
* **FIXPOINT byte-idêntico na rota C**: `gen2/teko.c` e `gen3/teko.c` têm o mesmo
  `sha256 9b6f9032…cd0f`, 10 551 547 bytes cada. O censo é aditivo e não moveu um byte
  emitido.
