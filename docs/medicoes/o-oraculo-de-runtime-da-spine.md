# O oráculo de runtime da spine — o eixo `pt` está errado em ~61 %

Experimento de teoria pedido pelo dono: *"um algoritmo de spine ligado poderia ser um portão em
runtime que gera reports… entregues ao PGO para ele analisar e sugerir o tuning."*

Ramo `theory/spine-runtime-oraculo`. Relatório completo em `theory/spine/RESULTADOS.md`, dados brutos
em `theory/spine/dados/`. **Nada promovido, nada ligado em produção.**

## 1. SOLIDEZ — a taxa de classificação errada é ~61 %

Unidade: **instância de célula** (uma execução de um binding/atribuição de `[]T`/`str`). Duas corridas
independentes, *stride* 256, **62 223 instâncias julgadas cada**:

| resultado | corrida A | corrida B |
|---|---:|---:|
| sobreviveu, referente na arena/dados | 39,44 % | 39,68 % |
| **sobreviveu, incluindo pilha viva** | **60,84 %** | **61,99 %** |
| morreu com a moldura | 39,16 % | 38,01 % |

±1,2 pp entre corridas. **De todas as células que a spine classifica como `PtFrame` — "morre com a
moldura" — ~61 % SOBREVIVEM à moldura.**

**A causa é estrutural e está no código:** `seed_pt` põe tudo em `PtFrame` e **não existe regra de
transferência para "o valor é devolvido"**. Verificado por *join*: os 80 sítios do topo são **80/80**
`PtFrame` no censo — nada assumido.

### O que isto significa para quem quiser rotear pelo eixo

**Se o roteamento fosse ligado ao eixo `pt` hoje, ~61 % das células seriam libertadas ainda vivas.**
É o *use-after-free* que eu tinha nomeado como alarme — agora com número.

**Nota de fronteira, e importa:** o actuador em construção (`cargo/0.3.1.0-atuador-regiao`) **NÃO
roteia pelo eixo `pt`** — roteia por **destino**, o `frame` do `codegen.tks:3726`. Este número **não o
condena**; condena qualquer plano que use o eixo `pt` como decisor de libertação.

## 2. PRECISÃO — a pergunta não tem denominador

`PtRoot { }` **nunca é construído** em `spine.tks`: `fn_spine` só produz `PtParam` (`:395`), `PtFrame`
(`:397`), `PtAdopter` (`:562`) e `PtTop` (`:506/507/511`). Censo do corpus: **21 010 células, 4 721
funções, `PtFrame` 99,871 %, `PtParam` 27, `PtRoot`/`PtAdopter`/`PtTop` = 0.**

A pergunta *"das `PtRoot`, quantas morreram com a moldura?"* **não deu zero — não tem população.**

Re-apontado para o `escape.tks`, que é **quem decide roteamento hoje**: ele **discorda do eixo `pt` em
92,12 % das células**. Duas análises no mesmo compilador, e a que está em uso não é a que se estava a
medir.

## 3. A LISTA DE ALVOS — e duas instrumentos independentes concordam

**Topo por bytes recuperáveis: `src/checker/resolve.tks:1752` — o `out` de `variant_siblings`.**

```
468 MB frescos · ~355 MB recuperáveis · 2 692 290 execuções
```

**Uma ordem de grandeza à frente do segundo.** E a tabela RA1 do `TEKO_ARENA_OBS` — que **não sabe
nada de spine** — aponta a **MESMA função como #1**: 915,6 MB em 1 220 313 *copy-grows*. Dois
instrumentos construídos por caminhos diferentes, mesma resposta.

### E o `?` do sítio de chamada 0 está resolvido

**O "sítio 0" de 1 694 MB (87,1 % da raiz) é `tk_slice_push_r`** — o RA0 culpa **o wrapper do
alocador**, não o programa. A atribuição útil está uma moldura acima, e é o RA1 que a dá.

Isto fecha a pergunta que arrastei o dia inteiro, e a resposta é que **eu estava a perseguir o
endereço errado**: o sítio de topo nunca ia nomear código de produto.

## 4. BYTES — 566 MB de 1944,5 MB medidos como recuperáveis

| | MB |
|---|---:|
| frescos (de-duplicados por objecto, 0 despejos) | 1014,0 (**52,1 %** da raiz) |
| sobreviveram (arena/dados) | 388,9 |
| sobreviveram (só pilha) | 34,9 |
| **morreram com a moldura** | **565,9** |

Os outros **~930 MB não são observados por este instrumento** — auto-boxes de struct, classes, `str`
em `malloc`. **Não foram extrapolados.**

## 5. CUSTO — é diagnóstico, e o número diz porquê

| arranjo | custo |
|---|---|
| carimbar e **não** ligar | **zero** — C emitido byte-idêntico ao controlo, binários byte-idênticos (`cmp` limpo) |
| emitir os 6 701 carimbos | zero tempo, **+25 MB** |
| **a sonda ligada** | **+35,2 s (+158 % do lado Teko), +270,7 MB de pico** |

**Não há configuração barata *e* informativa** — o custo é dominado pelas varreduras, e são elas que
dão resolução. **Logo o oráculo é instrumento de diagnóstico, não de produção**, e isso está dito com
o número em vez de suposto.

## 6. O BRAÇO DE INVERSÃO APANHOU UMA MENTIRA — e é a parte mais importante deste documento

Calibração (`theory/spine/calib`): o valor que **obviamente morre** → **1999/2000 morto**; o que
**obviamente sobrevive** → **2000/2000 vivo**. Separação total.

**Mas na primeira versão o braço que morre obviamente deu 50 % VIVO.** O rastreio localizou o
referente numa palavra do segmento de dados dentro de `tk_g_main_task`: **a cache de cauda-viva do
`#148` (`push_cache`) e as caixas de `mem::free`** — *bookkeeping* do runtime a nomear memória que o
programa já largou.

> *"Sem o braço de inversão eu tinha reportado «50 % de classificação errada» com cara séria."*

**Um instrumento sem calibração teria produzido um número plausível e falso**, e ele teria entrado
neste relatório como facto.

## 7. Os buracos, declarados

1. **~930 MB dos 1944 MB fora do alcance** do instrumento.
2. **43 % da amostra não julgada** pelo teste sólido — a passagem forçada mostra que inclina **92 %
   para VIVO**, logo **os 61 % são PISO, não estimativa central**.
3. Escritas de campo/índice/`Ref<T>` **não carimbadas**.
4. `addr2line` sem `file:line` porque o release não leva `-g`.
5. **Uma só máquina, uma só carga.**
