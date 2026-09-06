# Onde o bloco escopado dá problema — evidências medidas

Pedido do dono, 2026-07-31: *"Traga-me evidências reais de onde pode dar problema e te aponto a
direção."*

Três sondas, todas na rota C, todas com `TEKO_ARENA_OBS` ligado. O `adopt { }` é a única forma de
bloco com escopo próprio que existe hoje — logo é o que se mede quando se pergunta o que um bloco nu
faria.

## As três medições

| o que se aloca DENTRO do `adopt` | quanto se alocou | recuperado ao fim do bloco |
|---|---|---|
| `str` por concatenação em ciclo (5000×) | **238,4 MB** (`MALLOC str`, 10 005 buffers) | **0,0 MB** |
| `[]No` com 200 000 structs nomeadas | **8,0 MB** (região **root**, 8 chunks) | **0,0 MB** |
| `str` construída lá dentro e atribuída a um binding de FORA | 14 bytes | **sobrevive intacta** ✔ |

O terceiro caso é a boa notícia e vale registá-la: **o escape não corrompe.** Li a `str` escapada
depois de 20 000 alocações posteriores (o amplificador clássico de use-after-free), com e sem
`TEKO_MEM_PARANOID=1`, e o valor e o comprimento vieram certos nas duas. Não há libertação de storage
vivo — porque não há libertação nenhuma.

E é esse o problema: em ambos os casos que alocam a sério, **a região do `adopt` foi largada e
recuperou zero**:

```
reclaimed by region drops: 0.0 MB   (1 of 2 regions dropped)
CHUNKS: 1 regions, 8 chunks, malloc'd cap 8.0 MB, used 8.0 MB
root (process-lifetime, never freed): 8.0 MB
```

**Os 8 MB foram para a `root`, não para o adotante.** A região abriu, fechou, e não tinha nada
dentro.

## Porquê — e são duas linhas do runtime

**1. `tk_alloc` está preso à raiz, e o comentário di-lo por escrito** (`teko_rt.c:1711`):

```c
void *tk_alloc(size_t n) {
    // (S1) Route through the process root region: bump-allocated, never dropped = today's
    // malloc-everywhere leak (M.5).
    …
    return tk_region_alloc(tk_region_root(), n);
}
```

O alocador geral **não consulta região nenhuma** — vai sempre à raiz. Tudo o que passa por ele é
irrecuperável por qualquer drop de região, esteja dentro de que bloco estiver.

**2. O crescimento de lista ACEITA uma região, e não a recebe** (`teko_rt.c:3424` e `:3559`):

```c
void *buf = (region == tk_g_root) ? tk_alloc(cap * esz) : tk_region_alloc(region, cap * esz);
```

O ternário existe: se lhe passarem uma região não-raiz, ela é usada. **Na sonda, os 8 MB foram para a
raiz — logo o codegen não passou o adotante ao crescimento da lista.** A capacidade está no runtime e
não é exercida pelo emissor.

**3. E a `str` não vive em região de todo.** Os 238,4 MB aparecem na linha `MALLOC str total`, um
contador separado, fora da árvore de regiões. Nenhum drop de região a pode alcançar — nem hoje, nem
com um bloco nu, nem com o predicado alargado.

## O que isto significa para as três propostas em cima da mesa

| proposta | o que a medição diz |
|---|---|
| **bloco nu `{ }` reusando o `emit_adopt`** | **não resolve nada hoje** — o `adopt` já abre e larga região, e recupera 0,0 MB. Ganhava-se sintaxe, não memória |
| **alargar o predicado `Named`** | necessário, **e insuficiente sozinho**: mesmo qualificando, a alocação vai por `tk_alloc` → raiz |
| **rotear as alocações para a região envolvente** | **é aqui que está o ganho**, e o ternário do `:3424` mostra que metade do caminho já existe |

## O que NÃO medi, e é onde eu pararia antes de propor

1. **A rota nativa.** Tudo isto é rota C. O oráculo exige as duas medidas antes de se invocar.
2. **O que acontece se a rota passar a funcionar.** Hoje o escape é seguro *porque nada é libertado*.
   No dia em que a lista for alojada no adotante, a `str` escapada do caso 3 passa a ser uma
   libertação de storage vivo — **e o caso 3 passa de prova de segurança a prova de defeito.** É a
   inversão que qualquer proposta aqui tem de trazer, e não é opcional.
3. **`tk_alloc` tem 3,48 M chamadas no build do compilador** e é a fronteira: mudá-lo mexe em tudo ao
   mesmo tempo. Não medi o custo de lhe dar uma região corrente.

Sondas em `/tmp/.../esc` — reprodutíveis com `TEKO_ARENA_OBS=<path> ./out/esc`.


---

## Adenda: a evidência em CÓDIGO EXISTENTE, não em sonda minha

Correcção do dono: *"E por real, quero dizer código existente, e não sintetizado."* Justo — as sondas
acima são minhas. Isto é o que o corpus diz.

### 1. O `adopt { }` NÃO TEM UM ÚNICO UTILIZADOR em código de produção

```
grep -rn 'adopt {' src/ --include=*.tks | (excluindo linhas de comentário):  VAZIO
```

As 19 ocorrências de `adopt` em `src/` são **todas doc-comments** (15 só em `spine.tks`, a descrever
o eixo `PtAdopter`). E as três ocorrências no corpus de fixtures são **todas de REJEIÇÃO**:

| fixture | o que afirma |
|---|---|
| `c17_adopt_break_outside_loop` | `EXPECT_COMPILE_FAIL` |
| `c18_adopt_break_unknown_label` | `EXPECT_COMPILE_FAIL` |
| `c19_adopt_return_type_mismatch` | `EXPECT_COMPILE_FAIL` |

**Não há um único teste de que o `adopt` FUNCIONA — só de que o `adopt` mal escrito é recusado.**
Parser, checker, eixo da espinha, codegen e diagnósticos foram construídos para uma construção que
ninguém escreve.

### 2. Só DOIS alocadores do runtime inteiro aceitam uma região

```
tk_slice_push_r
tk_slice_with_cap_r
```

São estes dois, na árvore toda. Todo o resto é raiz por construção — e a própria fonte diz por
escrito, em `tk_slice_with_cap` (`teko_rt.c:3571`): *"the **default root-region lowering**"*.

### 3. E o codegen só emite a forma com região quando há uma MOLDURA — nunca por bloco

`codegen.tks:3730`, a linha que decide, em código de produção:

```teko
out = cb(out, if is_fo { " *)tk_slice_push_fo(" }
              else if frame.len > 0 { " *)tk_slice_push_r(" }
              else { " *)tk_slice_push(" })
```

A condição é **`frame.len > 0`** — a região de MOLDURA (o caminho `fn_body_has_frame_local`, que já
passa pelo predicado `Named`). **Não há um ramo que consulte a região de bloco.** Um `adopt`, um braço
de `match`, um corpo de `loop` podem ter aberto região — esta linha não olha para ela.

### 4. E a `str` não tem sequer uma forma com região para chamar

```
tk_str_concat        <- existe
tk_str_concat_len    <- existe
tk_str_concat_r      <- NÃO EXISTE
```

**Não é o codegen que não chama: é que não há o que chamar.** Os 66,4 MB de `MALLOC str` do build do
compilador são inalcançáveis por qualquer desenho de região, hoje, por ausência de superfície no
runtime.

### O que a evidência de código existente diz, junta

O `adopt` foi construído até ao fim — gramática, checker, espinha, codegen, diagnósticos — e:

* **ninguém o usa** (zero sítios de produção, três fixtures e as três de rejeição);
* **não pode libertar listas por bloco**, porque a única linha que escolhe a forma com região olha
  para a moldura e não para o bloco;
* **não pode libertar `str` de todo**, porque a variante com região não existe no runtime.

**Uma construção sem utilizadores, cujo mecanismo de baixo não a alcança.**
