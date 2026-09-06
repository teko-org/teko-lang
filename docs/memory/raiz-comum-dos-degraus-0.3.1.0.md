# A raiz comum dos degraus — 0.3.1.0

**Encomenda:** testar a hipótese do dono (2026-07-29, literal) *"Ouso dizer que é provável que muito
dos nossos travamentos se dão pelo runtime precisar de correções"* contra o registo real, e devolver
um veredicto com números. Confirmar OU refutar.

**Resposta curta, para quem só lê uma linha:** a hipótese **REFUTA-SE na letra e confirma-se no
espírito**. O runtime foi tocado em **2 dos 10 degraus**; `src/lir/lower.tks` foi tocado em **10 dos
10**; `src/backend/**` foi tocado em **0 dos 10**. Mas nos DOIS toques do runtime a razão escrita no
próprio cabeçalho `teko_rt.h` é a mesma: o backend nativo não consegue capturar um resultado de dois
words. O runtime não precisa de correções — **o backend precisa de deixar de pedir ao runtime aquilo
que ele próprio não sabe fazer**.

---

## 0-bis. RULING APROVADO — as strings multi-byte. A decisão inteira, num sítio só

> **"Caso das strings multi-byte aceito e aprovado"** — dono, 2026-07-29

O pacote foi aprovado. A decisão nasceu espalhada por várias trocas do mesmo dia; fica aqui
consolidada para que ninguém tenha de a reconstruir. As medições que a sustentam estão em §7-bis,
§7-ter e §7-quater; esta secção é só o que ficou DECIDIDO.

### O que foi decidido

| # | decisão | nota |
|---|---|---|
| 1 | **`str` leva os DOIS contadores** — caracteres e bytes | é o terceiro word |
| 2 | **`str.len` conta CARACTERES**; o outro contador conta bytes | *"O usuário do sistema não sabe bytes e nem imagina que um caractere acentuado ocupa 2 bytes e um emoji ocupa 4"* |
| 3 | **A iteração dá CHARS** | sai de graça — ver a armadilha abaixo |
| 4 | **`char` continua AÇÚCAR**, em vagão próprio, e **`c'X'` baixa para `[]byte`** | *"char sendo açúcar, um c'X' deveria fazer lowering para []byte, e aqui está o seu char gordo em C"* |
| 5 | **O acesso a bytes é EXPLÍCITO**, por funções em **`teko::text::`**, extensível a outros encodings | `text` não colide; `str` colidiria. `teko::text::` já existe com 4 usos (`valid_utf`, `str_from_utf`, `concat`) |
| 6 | **A migração de `.len` faz-se com ERRO DURO por uma versão** (a opção D2 de §7-quater.4) | *"Bora fazer barulho"* |

### As duas coisas que não se podem separar

**(3) sai de graça, e a razão está medida.** O for-each é um desugar SINTÁCTICO no parser
(`parse_loop_foreach_slice`, `src/parser/loop_head.tks:668`) para `0 .. _src.len` + `_src[_i]`. Não
existe máquina de iteração. **Se `.len` e `[i]` passarem a chars, a iteração segue sozinha — zero
linhas de parser.**

**A ARMADILHA, medida, e é o outro lado da mesma moeda:** se só `.len` mudar, `"Aéz"` itera **3**
vezes indexando **bytes** — visita `'A'`, `0xC3`, `0xA9`, e **`'z'` nunca é visitado**. `'é'` vem
partido em duas metades sem sentido. **Os dois eixos movem-se juntos ou nenhum se move.**

**(6) é o que torna (2) verificável pela máquina.** Os 25 sítios de aritmética de bytes sobre
`str.len` (§7-quater.4) são todos mecânicos, e ambos os contadores são `u64` — trocar o significado
de `.len` deixaria os 25 a compilar em silêncio, e em ASCII puro nenhum teste do projecto mudaria de
cor. O erro duro converte **25 riscos silenciosos em ~394 erros que o compilador encontra sozinho**.
Cobre `.len` E `[i]`, pela mesma razão da armadilha acima.

**(4) é uma linha, e tem lugar fixo na fila.** `lower_char_lit_fat` delega em `lower_str_bytes_fat`
(`src/lir/lower.tks:6622`), que já interna bytes UTF-8 em rodata e devolve o par. Sequenciado
**LOGO A SEGUIR ao R0, nunca antes** — sem o R0 os dois predicados de "é gordo" discordam e o campo
`char` fica com 8 bytes onde o store escreve 16 (§7-quater.3-bis).

### A sequência aprovada

| ordem | trabalho | lane |
|---|---|---|
| 1 | **R0-R7** (R0 = unificar os predicados; R5 = gémeos `_len`; R7-min = o `char`) | **esta** (0.3.1.0) |
| 2 | migrar o compilador para `[]byte` | pode correr em paralelo com 1 |
| 3 | **`.len` conta chars** + erro duro de transição | lane de LINGUAGEM, **com bump de versão e o seed a acompanhar** |
| 4 | `get_bytes`/`from_bytes` em `teko::text::` + encodings | lane de STDLIB |

**Por que (3) exige o bump e não cabe aqui:** o `fixpoint` compara gen2 com gen3, e um compilador
com semântica de `.len` diferente da do seu seed não é comparável com o anterior. A mudança tem de
atravessar uma versão com o seed a acompanhar.

### O que a aprovação NÃO resolve — pendente, não fechado

1. **O R0 está a ser implementado AGORA, noutro vagão** (`cargo/0.3.1.0-R0-predicados-gordos`; à
   data desta escrita ainda não publicado no remoto). Nada aqui deve duplicá-lo.
2. **A previsão P11 ainda NÃO voltou** (§7-quater.3-bis). Ela pergunta se o crumb R3, já aterrado,
   passou a **ESCREVER por cima do campo vizinho** em vez de apenas ler lixo, no caso de um campo
   gordo atrás de um alias de tipo. Verificação: compilar a sonda `aliasobs`
   (`type S = str; struct H { s: S; n: i32 }` com `n = 41`) no vagão e afirmar `h.n`. **Se `h.n` já
   não for 41, a previsão confirma-se e a urgência de tudo muda** — R0 deixa de ser o próximo crumb
   e passa a ser correcção destrutiva a fechar primeiro. Enquanto não voltar, isto fica ABERTO.

---

## 0. Método, e o compilador com que isto foi medido

| etapa | comando | resultado |
|---|---|---|
| gen1 (trampolim, `bootstrap/teko.c` commitado) | `sh scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1` | ✓, `.gen1/teko` 9 408 936 bytes |
| gen1c (SOURCE actual desta lane, rota C) | `TEKO_BACKEND=c ./.gen1/teko . -o .gen1c --no-verify --release` | ✓ |
| auto-construção nativa | `TEKO_BACKEND=native ./.gen1c/teko . -o .gen2-native --no-verify --release` | pára no degrau 10 (secção 2) |

Todas as sondas desta análise foram compiladas com `.gen1c` — o compilador do tip actual
(`d4ad700`), com o degrau 9 dentro. A rota C é o ORÁCULO, pela regra já legislada neste ficheiro
irmão (`0.3.1.0-linux-native-first-stop.md`, "THE ORACLE RULE").

Nada abaixo é afirmado sem a linha de código ou a saída do programa colada junto.

---

## 1. A tabela dos degraus fechados — um por linha

A coluna "ficheiro CORRIGIDO" foi lida do `git show --stat` de cada commit, não do texto do registo.

| # | mensagem literal de paragem | função onde rebentou | commit | ficheiro(s) de facto CORRIGIDO(s) | classe da causa |
|---|---|---|---|---|---|
| 1 | `native backend N1: a push whose element is an AGGREGATE (…) needs slice elements held BY VALUE` | `teko::backend::emit_u32_le` | `c436cdd` | `src/lir/lower.tks` | LOWERING partilhada (classe de máquina em falta: `byte`) |
| 2 | `native backend N1: unknown variant case `u32` (internal)` | `teko::backend::encode_alu_imm` | `ac1b6c5` | `src/checker/resolve.tks` **+** `src/lir/lower.tks` | LOWERING partilhada + **CHECKER** |
| 3 | `native backend N1: integer operator not yet lowered (N2)` | `teko::backend::needs_lr_save` | `e5357d2` | `src/lir/lower.tks` | LOWERING partilhada (forma de controlo em falta) |
| 4 | `native backend N1: a push whose element is an AGGREGATE (…)` | `teko::backend::wrap_plain_words` | `c46808b` | `src/lir/lower.tks` **+** `src/runtime/teko_rt.{c,h}` | LOWERING partilhada **+ RUNTIME** (`tk_slice_elem_box`) |
| 5 | `native backend N1: fat-pointer receiver `match-expression` not yet lowered (N2)` | `teko::backend::encode_one` | `ab00643` | `src/lir/lower.tks` | LOWERING partilhada (**dois words**) |
| 6 | `native backend N1: slice match pattern not yet lowered (N2)` | `teko::backend::encode_one` | `52da0b4` | `src/lir/lower.tks` **+** `src/lir/lower_const.tks` | LOWERING partilhada (**dois words**) |
| 7 | ``native backend N1: `str` has no single PrimKind, asked by the comparison chain (N2)`` | `teko::backend::name_in_symbols` | `b8cfb32` | `src/lir/lower.tks` | LOWERING partilhada (**dois words**) |
| 8 | ``native backend N1: a push whose element is the nominal type `teko::backend::MInst`, which has no registered layout`` | `teko::backend::push_minst_block` | `78a7254` + `2e8e73b` | `src/lir/lower.tks` | LOWERING partilhada (agregado POR ENDEREÇO) |
| 9 | ``native backend N1: fat-pointer receiver `string interpolation` not yet lowered (N2)`` | `teko::backend::ar_header` | `24ee25c` | `src/lir/lower.tks` **+** `src/runtime/teko_rt.{c,h}` | LOWERING partilhada (**dois words**) **+ RUNTIME** |
| 10 | ``native backend N1: a push onto a slice of FAT elements (`[]str`/`[][]T`) needs the two-word element stride the array-literal store and the index load do not carry yet (N2)`` | `teko::backend::global_symbol_names` | ABERTO | `src/lir/lower.tks` (três sítios, não dois — ver §5) | LOWERING partilhada (**dois words**) |

### Correcções ao que me foi dado como controlo

O pedido dizia "os degraus 1, 2, 3, 5, 6, 7 e 8 foram, tanto quanto sei, `src/lir/lower.tks`".
Verificado um a um, com duas correcções:

1. **O degrau 2 não foi só `lower.tks`.** `ac1b6c5` toca também `src/checker/resolve.tks` — a
   correcção foi criar `checker::builtin_case_name`, o inverso da tabela de `scope.builtin_type`.
   É o ÚNICO degrau desta lane com uma metade no CHECKER.
2. **O degrau 6 não foi só `lower.tks`.** `52da0b4` toca também `src/lir/lower_const.tks`
   (`const_variant_tag_bytes`: a imagem em rodata de uma variante de caso vazio passou a ser os 24
   bytes inteiros).

Os degraus 4 e 9 estão exactamente como descritos: 4 = boxing de arena (`tk_slice_elem_box`), 9 = um
braço `TInterp` em falta no `lower_fat_expr` MAIS três funções novas no runtime C
(`tk_str_concat_len`, `tk_i64_to_str_len`, `tk_u64_to_str_len`, `src/runtime/teko_rt.h:223-225`).

**Uma metade do degrau 9 não estava no enunciado e é importante:** o mesmo commit fechou também o
builtin `teko::str::concat`, que `call_symbol` não conhecia e que por isso **manglava em silêncio
para um símbolo indefinido** em vez de parar honestamente. Isso não é um detalhe — é o mecanismo da
previsão P2 (§7).

---

## 2. O veredicto sobre a hipótese do dono, com a contagem

### Contagem por FICHEIRO tocado (10 degraus, os commits acima)

| ficheiro | degraus em que foi corrigido | fracção |
|---|---|---|
| `src/lir/lower.tks` | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 | **10/10 (100%)** |
| `src/lir/lower_const.tks` | 6 | 1/10 |
| `src/runtime/teko_rt.{c,h}` | 4, 9 | **2/10 (20%)** |
| `src/checker/resolve.tks` | 2 | 1/10 |
| `src/backend/**` (isel, encode, objfile, regalloc) | *nenhum* | **0/10 (0%)** |

### Contagem por CLASSE DE CAUSA

| classe | degraus | contagem |
|---|---|---|
| LOWERING partilhada | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 | **10** |
| RUNTIME (como metade de um degrau de lowering) | 4, 9 | **2** |
| CHECKER (como metade de um degrau de lowering) | 2 | **1** |
| EMISSÃO por plataforma (`src/backend/**`) | *nenhum* | **0** |
| outra | *nenhuma* | **0** |

**Nenhum degrau foi RUNTIME de facto.** Não existe um único degrau desta lane cuja correcção tenha
sido no runtime E SÓ no runtime. Os dois toques no runtime (4 e 9) são metades secundárias de
degraus cuja metade principal está em `src/lir/lower.tks`, e em ambos a razão está escrita no
próprio código:

- degrau 4, `tk_slice_elem_box` — *"the LIR has no memcpy op and no allocation op, and inventing an
  unrolled load/store copy in the lowering would have put an ALLOCATION POLICY … into codegen, where
  the arena is invisible"* (registo do degrau 4);
- degrau 9, `tk_str_concat_len` e twins — `src/runtime/teko_rt.h:214-222`, literal:

  > *"A genuine C caller never needs these (a `tk_str` return already carries both halves); they
  > exist for the native backend's own `LCall`, whose result capture is ONE register
  > (`select_call_result_x86`/`select_call_result_arm64`) — the same width a plain scalar/pointer
  > call answers with, one short of the two-eightbyte SysV/AAPCS64 register pair a `tk_str`-by-value
  > return actually occupies."*

### O veredicto, dito claramente

**A hipótese está REFUTADA na letra: 2 em 10, e nenhum deles um degrau de runtime "de facto".** O
número que domina é outro e é esmagador: `src/lir/lower.tks`, 10 em 10.

**E está CONFIRMADA no espírito, com uma inversão da direcção da seta.** Nos dois casos, não foi o
runtime que estava errado — foi o backend que não sabia falar com ele. O runtime estava certo antes
e depois; o que se acrescentou foi uma PRÓTESE para uma limitação do backend, e o próprio ficheiro
de cabeçalho diz qual (uma só register de resultado). Reformulada de modo a passar no teste:

> **Muitos dos nossos travamentos dão-se porque o backend nativo não sabe pedir ao runtime aquilo
> que precisa — e o sintoma aparece no runtime porque as funções do runtime devolvem `tk_str`, que é
> gordo.**

Que é, palavra por palavra, a suspeita que me foi pedida para testar. Ver §3-§5.

---

## 3. A raiz comum — a suspeita, testada

A suspeita entregue: *"os valores gordos são cidadãos de segunda no backend nativo"*.

**CONFIRMA-SE**, e a prova mais forte não é minha: está escrita no runtime pela mão de quem fechou o
degrau 9, citada acima. Mas confirma-se com **uma correcção importante que muda o plano**: não é
UMA raiz, são DUAS, e elas alternam. Ver §4.

### O facto de arquitectura, numa linha

`src/lir/lir.tks:20` — a LIR não tem classe de máquina gorda nenhuma:

```
pub type LType = enum {
    I8; I16; I32; I64
    F32; F64
    Ptr
    Void
}
```

`src/lir/lir.tks:66,70,134` — e a instrução tem exactamente UM resultado e UM valor de retorno:

```
pub type LCall = struct { symbol: str; args: []u32; variadic: bool }
pub type LRet = struct { has_value: bool; value: u32 }
pub type LInst = struct { result: u32; has_result: bool; op: LOp; line: u32; col: u32 }
```

`src/lir/lower.tks:270,276` — e o mapa do tipo semântico para a máquina colapsa `str`/`[]T` em `Ptr`:

```
pub fn ltype_of(t: checker::Type, enums: []LEnumInfo): LType {
    match t {
        checker::Prim as p => ltype_of_prim(p.kind)
        checker::Byte => LType::I8
        checker::Void => LType::Void
        checker::Named as n => ltype_of_named(n, enums)
        _ => LType::Ptr          // <-- Str e Slice caem AQUI. 8 bytes onde a verdade é 16.
    }
}
```

**A expressão `ltype_size(ltype_of(t, enums))` é a raiz textual.** Onde quer que ela apareça sobre um
tipo gordo, o backend escreve 8 onde a verdade é 16. O próprio doc-comment de `ltype_of` já avisa
para a doença gémea (`byte` dentro de um contentor) — *"CAUTION: only apply this mapping to an
operand's OWN type, never to a container's element type"* — sem reparar que `Str`/`Slice` sofrem do
mesmo mal na posição de OPERANDO, não só na de elemento.

### As NOVE representações de "dois words" que este backend tem hoje

Nenhuma delas é partilhada com as outras. Cada uma foi inventada no sítio onde fez falta. É esta
dispersão, e não a LIR de um word, que faz os degraus parecerem separados.

| # | representação | onde vive | quem a escreve / quem a lê |
|---|---|---|---|
| 1 | par de VRegs `LoweredFat{ptr,len}` | `src/lir/lower.tks:5600` | `lower_fat_expr` (5622) |
| 2 | dois registos de entrada consecutivos | `bind_fat_param`, `lower.tks:7541`; `param_ltypes`, `lower.tks:7734` | convenção "tk_str-twin", ruling do dono |
| 3 | dois argumentos achatados na chamada | `lower_arg_vregs`, `lower.tks:1631` | ptr primeiro, len segundo |
| 4 | dois block-args no merge | `merge_fat_value_args`, `lower.tks:6151` | `lower_if_fat` (6269), `lower_match_fat` (6307) |
| 5 | slot de frame de 16 bytes `{ptr@0,len@8}` | `alloc_fat_slot`/`store_fat_slot`/`load_fat_slot`, `lower.tks:6468/6485/6501` | só para um `mut` gordo local |
| 6 | slot escondido de saída para o COMPRIMENTO | `bind_ret_len_slot` (7562) + `lower_return_fat` (2939) + `lower_call_fat` (5667) | a prótese do retorno |
| 7 | gémeos `_len` do runtime com parâmetro de saída | `teko_rt.h:223-225`; `lower_len_out_call`, `lower.tks:6681` | a prótese da chamada ao runtime |
| 8 | `{tag@0, ptr@8, len@16}` no invólucro de 24 bytes | `variant_payload_offset` (4476) / `variant_payload_len_offset` (4486) | degrau 6 |
| 9 | `{ptr@off, len@off+8}` num campo de struct | `field_layout_size`, `lower.tks:8176`; leitor `lower_fat_field`, `lower.tks:6409` | **escritor EM FALTA — §5** |
| — | **elemento gordo de slice** | **não existe** | **degrau 10, aberto** |

Há ainda duas coisas de dois words que este backend trata BEM, e servem de contraste útil: o
ponteiro gordo de INTERFACE (`IfaceFatPtr{data,vtable}`, `lower.tks:1985`) e o de CLOSURE
(`ClosureFatPtr{target,env}`, `lower.tks:2381`). Ambos são um `alloca` de 16 bytes manipulado
SEMPRE POR ENDEREÇO — um só word viaja, e o par nunca precisa de atravessar uma fronteira de ABI.
É por isso que nunca deram degrau nenhum.

### Onde um valor de dois words é tratado como UM SÓ — inventário com ficheiro e linha

| operação | sítio | o que faz hoje | estado |
|---|---|---|---|
| **captura do resultado de chamada** | `select_call_result_x86`, `src/backend/isel_x86_64.tks:1022` | `mov dst, RAX` — UM registo | **impossível hoje**; contornado pela prótese 6/7 |
| **captura do resultado de chamada** | `select_call_result`, `src/backend/isel_arm64.tks:1577` | `mov dst, x0` — UM registo | idem |
| **retorno** | `select_ret_x86`, `src/backend/isel_x86_64.tks:510` | `mov RAX, v` — UM registo; sem RAX:RDX | idem |
| **retorno** | `select_ret`, `src/backend/isel_arm64.tks:971` | `mov x0, v` — UM registo; sem x0:x1 | idem |
| **passagem de argumentos** | `pin_args_x86` (875) / `pin_args` (`isel_arm64.tks:1537`) | um registo por VReg | **CORRECTO** — a lowering já achatou em dois VRegs (repr. 3) |
| **passagem por referência (Win64)** | `str_pair_by_ref_x86`, `isel_x86_64.tks:942` | rematerializa o par num slot de 16 bytes | **CORRECTO mas de LISTA FECHADA** — só 7 símbolos (`is_str_arg_builtin`, `lower.tks:1523`) |
| **store de campo de struct (construção)** | `store_struct_fields`, `src/lir/lower.tks:6931` | `store_inst(addr, vo.vreg, ltype_of(field.type))` — UM word | **DEFEITO SILENCIOSO — medido, §5** |
| **store de campo de struct (atribuição)** | `lower_assign_field` (7276) → `store_assign_value` (7300), largura `ltype_of(a.bound)` | UM word | **DEFEITO SILENCIOSO — medido, §5** |
| **store de elemento de array literal** | `store_array_elements`, `lower.tks:7060` | `stride` = `ltype_size(ltype_of(elem))` = 8 | **DEFEITO SILENCIOSO — medido, §5** |
| **load por índice** | `lower_index_slice`, `lower.tks:7034` | `elem_ty = ltype_of(e.type)`; um `load` | paragem honesta a jusante (degrau 10) |
| **push** | `lower_list_push` → `fat_element_push_stop`, `lower.tks:5869` | recusa | **paragem honesta** (degrau 10) |
| **merge / block-args** | `promote_env`, `lower.tks:3043` | *"A FAT (str/slice) binding is left UNPROMOTED"* | contornado pela repr. 5 (slot) |
| **payload de variante** | `store_fat_variant_payload` (4618) / `bind_fat_case_payload` (4344) | ptr@8 + len@16, à mão | **CORRECTO** (degrau 6) |
| **leitura de campo gordo** | `lower_fat_field`, `lower.tks:6409` | ptr@off + len@off+8, à mão | **CORRECTO** (#594 T-B6) |

---

## 4. Quais degraus caem nessa lista e quais NÃO caem

Testei a suspeita contra cada degrau, e não a confirmei por cortesia. **Metade não cai.**

| # | é "dois words"? | porquê |
|---|---|---|
| 1 | **NÃO** | `byte` sem classe de máquina. É o oposto: um valor de UM octeto a receber a classe de OITO. Família "largura", não família "dois words". |
| 2 | **NÃO** | caso de variante nomeado por palavra-chave; resolução de nome. Metade no CHECKER. |
| 3 | **NÃO** | `&&`/`||` precisam de curto-circuito. Forma de CONTROLO em falta. |
| 4 | **NÃO** | `alloca` é um slot por INSTRUÇÃO, não por EXECUÇÃO. É a família `NATIVE-AGG-SLICE-BY-ADDRESS` — agregados por endereço. |
| 5 | **SIM** | um `match`-VALOR gordo precisa de DOIS block-args no merge (`merge_fat_value_args`, repr. 4). |
| 6 | **SIM** | *"For a `str`/`[]T` member `ltype_of` answers `Ptr`, so **the LENGTH is dropped at construction**"* — o registo do degrau 6, literal. O invólucro de 24 bytes existe SÓ para caber o par. |
| 7 | **SIM** | *"A `str` is two words and has none"* — o registo do degrau 7, literal, sobre `prim_kind_of` pedir UMA classe de máquina. |
| 8 | **NÃO** | `MInst` é uma variante DECLARADA que chega como `checker::Named`. É a família do degrau 4 (por endereço) + a família da miscompilação do braço. |
| 9 | **SIM** | o cabeçalho `teko_rt.h:214-222` nomeia `select_call_result_x86`/`select_call_result_arm64` explicitamente. |
| 10 | **SIM** | elemento gordo: `fat_element_push_stop` diz-o pelo nome. |

**Cinco em dez.** E a distribuição no TEMPO é o resultado que importa: dos degraus 1 a 4, **zero**
são de dois words; dos degraus 5 a 10, **cinco em seis** são (todos menos o 8).

### O contra-exemplo, que é o que impede este documento de ser uma arrumação

**O degrau 8 é o contra-exemplo e ele é sério.** Está no meio de 6-7-9-10 e NÃO é de dois words: a
sua correcção (`expand_named_variant` em `aggregate_box_bytes`, e o box do payload) não tocou em
nenhuma das nove representações de §3. Ele pertence à OUTRA raiz, e essa outra raiz também está
aberta e também é grave:

> **RAIZ B — o valor de um agregado É o endereço de um slot de frame** (`NATIVE-AGG-SLICE-BY-ADDRESS`,
> §5.1 da lowering). Dela vêm os degraus 4 e 8, o invólucro devolvido que pendura (já reclassificado
> como BUG pelo dono a 2026-07-28) e o SIGSEGV que a retracção do degrau 8 documenta.

Portanto a formulação certa da resposta à pergunta que me foi feita é:

> **Não há UMA raiz comum. Há DUAS, elas alternam, e cada uma explica metade dos degraus.**
> RAIZ A (dois words) explica 5, 6, 7, 9, 10. RAIZ B (agregado por endereço) explica 4 e 8.
> Os degraus 1, 2 e 3 não pertencem a nenhuma das duas — são gaps genuinamente isolados, e já
> fecharam.

Isto é melhor notícia do que uma raiz só, não pior: significa que as duas raízes podem ser fechadas
por vagões independentes, em paralelo, e que a RAIZ A — a que este documento desenha — é a que está
no caminho crítico hoje.

---

## 5. As TRÊS miscompilações silenciosas que o modelo previu, e que foram MEDIDAS aqui

Se a RAIZ A é real, ela tem de ter deixado buracos onde ninguém foi olhar. Fui olhar ao sítio que o
inventário de §3 aponta como o único com LEITOR e sem ESCRITOR: o campo gordo de struct
(representação 9).

**Todas as sondas: projecto standalone, `main.tks` = `exit(probe())`, compilador `.gen1c`.**

### 5.1 Construir um struct com campo `str` e ler `.len` — nativo dá a resposta errada, calado

```teko
pub type Holder = struct { name: str; n: i32 }

pub fn probe(): i32 {
    let h = Holder { name = "abcde"; n = 7 }
    if h.n != 7 { return 1 }
    if h.name.len != 5 { return 2 }
    0
}
```

```
$ TEKO_BACKEND=c      .gen1c/teko … && ./out_c/fatfield ; echo $?     ->  0
$ TEKO_BACKEND=native .gen1c/teko … && ./out_n/fatfield ; echo $?     ->  2
```

**Isolado até ao osso, três sondas mais:**

| sonda | o que mede | C | nativo |
|---|---|---|---|
| `fatobs` — devolve `h.name.len to i32` como exit code | QUAL é o comprimento observado | **5** | **0** |
| `fatptrhalf` — lê `h.name[0]` e `h.name[4]` por índice | a metade PONTEIRO está certa? | 0 | **0** |
| `fatconst` — o mesmo `Holder` como `const` em rodata | o caminho de const está certo? | 0 | **0** |

Ler as três juntas dá a causa exacta e fecha-a: **a metade PONTEIRO é escrita correctamente; a
metade COMPRIMENTO nunca é escrita (lê-se 0); e o caminho de CONST está certo.**

O sítio, com o código colado — `store_struct_fields`, `src/lir/lower.tks:6931`:

```
cur = ctx_append(with_addr, store_inst(addr.vreg, vo.vreg, ltype_of(field_vals[i].type, cur.enums), line, col))
```

`ltype_of(Str)` é `Ptr`. Um store de 8 bytes em `off`. O `off+8` nunca é escrito. E o LEITOR,
`lower_fat_field`, `src/lir/lower.tks:6409`, lê-o:

```
let with_la = ctx_append(with_pr, field_addr_inst(la.vreg, ro.vreg, off + ltype_size(LType::Ptr), …))
… load_inst(lr.vreg, la.vreg, LType::I64, …)
```

**Por que ninguém deu por isto:** `field_layout_size` (`lower.tks:8176`) e `lower_fat_field` nasceram
no crumb #594 T-B6, cujo alvo eram os agregados CONST em rodata — e o escritor de const
(`src/lir/lower_const.tks:420`, `const_fat_field`) ESCREVE os 16 bytes correctamente. A sonda
`fatconst` prova-o: o caminho const passa nas duas rotas. Foi construído um leitor de 16 bytes para
um escritor que só existia para consts, e o escritor de RUNTIME nunca foi alargado.

### 5.2 Atribuir a um campo gordo — o mesmo defeito, o outro escritor

```teko
pub type Holder = struct { name: str; n: i32 }

pub fn probe(): i32 {
    mut h = Holder { name = "abcde"; n = 7 }
    h.name = "xy"
    if h.name.len != 2 { return 3 }
    0
}
```

C: **0**. Nativo: **3**. Sítio: `lower_assign_field` (`lower.tks:7276`) passa
`ltype_of(a.bound, …)` como largura a `store_assign_value` (`lower.tks:7300`) — 8 bytes.

### 5.3 Passar um campo gordo como argumento — a terceira boca do mesmo buraco

```teko
pub type Holder = struct { n: i32; name: str }
fn take(s: str): i32 { s.len to i32 }

pub fn probe(): i32 {
    let h = Holder { n = 7; name = "abcde" }
    if take(h.name) != 5 { return 1 }
    0
}
```

C: **0**. Nativo: **1**. (Com o campo gordo em offset 8 desta vez — o defeito não depende do offset.)

### 5.4 O mesmo com um campo `[]T`, não só `str`

```teko
pub type Holder = struct { xs: []i64; n: i32 }
… if h.xs.len != 3 { return 1 }
```

C: **0**. Nativo: **1**. `is_fat_type` cobre `Str` E `Slice`; o defeito também.

### 5.5 E o literal `[]str` constrói-se em SILÊNCIO com o stride errado

```teko
pub fn probe(): i32 {
    let xs: []str = ["aa", "bbb", "cccc"]
    if xs.len != 3 { return 1 }
    0
}
```

Nativo: **compila e devolve 0**. O buffer JÁ está corrompido — `store_array_elements`
(`lower.tks:7060`) escreveu três ponteiros com stride 8 e nenhum comprimento. Só o LEITOR pára
(`fat-pointer receiver \`index\` not yet lowered`). É por isso que o degrau 10 tem **TRÊS** sítios,
não dois: o store do literal, o load por índice e o `esz` do push — exactamente os três que
`fat_element_push_stop` já nomeia no seu próprio doc-comment (`lower.tks:5869`).

### Por que estes três defeitos silenciosos são o achado mais grave deste documento

**383 das 717 declarações de `struct` em `src/**` (53%) têm pelo menos um campo `str` ou `[]T`.**
(Contagem por varrimento das declarações, comentários removidos.)

E uma delas é o coração da própria lowering — `src/lir/lir.tks:527`:

```
pub type LStructLayout = struct { name: str; size: u32; align: u32; field_names: []str; field_offsets: []u32; field_types: []LType }
```

Quatro campos gordos. É construída em `layout_of_fields` (`lower.tks:8014`) e lida em
`field_offset_of` (6885) e `field_type_of` (6907), ambos por `layout.field_names.len`. Medido em
§5.1, esse `.len` lê **0** no nativo — logo o laço sai imediatamente e toda a procura de campo
devolveria "unknown field". **Um compilador construído por esta rota não conseguiria compilar um
único `struct`.**

Isto NÃO é um degrau. Um degrau pára e diz onde. Isto passa por todos os portões:
`teko test .` corre pela rota C (medido de forma independente em `mapa-native-6-pernas-0.3.1.0.md`,
Probe D); o `fixpoint` compara gen2 com gen3, dois binários do MESMO compilador errado.

---

## 6. O desenho da correcção de raiz — RAIZ A

O objectivo, dito de forma falsificável: **um valor de dois words tem UMA representação em memória
(`{ptr@0, len@8}`), UMA largura (16), UM escritor e UM leitor — e nenhum sítio do ficheiro volta a
calcular largura de valor gordo com `ltype_size(ltype_of(t))`.**

**O que NÃO se faz, e porquê.** Não se acrescenta uma classe gorda à `LType` nem um segundo VReg de
resultado à `LInst`. Isso obrigaria a mexer nos dois isels, no regalloc, no stackify, no printer e
no motor legado de LIR (`src/lir/lir_oracle.tks`) — e não resolveria nada que a convenção de slot
escondido (repr. 6) não resolva já, provada a funcionar (sonda `fatret`: nativo e C ambos 0). A
doença é ARITMÉTICA DISPERSA, não a LIR de um word. Isto alinha com o ruling do dono no degrau 6
(*"uniformity is the opposite of what has bitten this lane"*).

### A sequência de crumbs — a ordem é a lição do degrau 3 e do degrau 6

Cada crumb é fechável e portável sozinho, e cada um tem o seu portão.

---

**R1 — UMA autoridade de largura. Nenhum comportamento muda ainda.**

Introduzir a pergunta única, e converter os sítios que já respondem 16 (`field_layout_size`) a
chamá-la, sem tocar em nenhum que responda 8. Portão: `teko test .` e o corpus `own_native`
byte-idênticos ao de hoje.

```teko
/**
 * fat_value_bytes — a largura em bytes da imagem `{ptr@0, len@8}` de um valor gordo.
 *
 * A ÚNICA resposta do backend à pergunta "quantos bytes ocupa um `str`/`[]T` em memória".
 * Existe para que o campo de struct, o elemento de slice, o slot de frame e o payload de
 * invólucro de variante não possam voltar a discordar: uma largura calculada em dois sítios é
 * uma corrupção silenciosa à espera, não uma paragem.
 *
 * @return u32  a largura, sempre `2 * ltype_size(LType::Ptr)`
 * @see fat_len_offset
 * @since 0.3.1.0 raiz A
 */
fn fat_value_bytes(): u32 {
    ltype_size(LType::Ptr) * (2 to u32)
}

/**
 * fat_len_offset — o deslocamento, dentro da imagem de um valor gordo, da metade COMPRIMENTO.
 *
 * A metade PONTEIRO vive sempre no offset 0 da imagem; esta função nomeia a outra. A ORDEM
 * (ptr primeiro, len depois) é a mesma que `bind_param` liga nos registos de entrada e a mesma
 * dos campos de `tk_str` em `src/runtime/teko_rt.h` — um sítio que a invertesse leria um
 * ponteiro como comprimento, em silêncio.
 *
 * @return u32  o deslocamento da metade comprimento
 * @see fat_value_bytes
 * @since 0.3.1.0 raiz A
 */
fn fat_len_offset(): u32 {
    ltype_size(LType::Ptr)
}

/**
 * value_image_bytes — quantos bytes de memória a imagem de um valor de tipo `t` ocupa quando
 * um contentor o guarda POR VALOR: `fat_value_bytes()` para `str`/`[]T`, a largura escalar da
 * sua classe de máquina para tudo o resto.
 *
 * Esta é a função que substitui `ltype_size(ltype_of(t, enums))` em TODO o sítio que fale de
 * armazenamento — o stride de um literal de array, o passo de um índice, o `esz` de um push, a
 * largura de um store de campo. `ltype_of` responde `Ptr` a um tipo gordo, e `ltype_size(Ptr)`
 * é 8: metade da verdade, exactamente a metade que se perde em silêncio.
 *
 * @param checker::Type t  o tipo cujo valor vai ser armazenado
 * @param []LEnumInfo enums  a tabela de enums declarados (um enum leva a largura do seu carrier)
 * @return u32  a largura da imagem em memória
 * @see fat_value_bytes
 * @since 0.3.1.0 raiz A
 */
fn value_image_bytes(t: checker::Type, enums: []LEnumInfo): u32 {
    if is_fat_type(t) { return fat_value_bytes() }
    ltype_size(ltype_of(t, enums))
}
```

Reescrever, com o corpo inalterado no resultado: `fat_slot_bytes` (`lower.tks:6456`) →
`fat_value_bytes()`; `field_layout_size` (`lower.tks:8176`) → `fat_value_bytes()`;
`variant_payload_len_offset` (`lower.tks:4486`) → `variant_payload_offset() + fat_len_offset()`.

---

**R2 — UM escritor e UM leitor de imagem gorda.** Generalizar `store_fat_slot`/`load_fat_slot`
(`lower.tks:6485`/`6501`), que hoje só servem o slot de `mut` local, para qualquer endereço, e
converter os DOIS sítios que hoje fazem o mesmo à mão (`store_fat_variant_payload` 4618,
`bind_fat_case_payload` 4344, `lower_fat_field` 6409). Ainda sem mudar comportamento — os offsets
são os mesmos. Portão: corpus `own_native` byte-idêntico.

```teko
/**
 * store_fat_image — escrever a imagem `{ptr@0, len@8}` de um valor gordo no endereço `addr`.
 *
 * O ÚNICO escritor de valor gordo em memória do backend. `addr` pode ser o slot de frame de um
 * `mut` local, o offset de um campo de struct, o offset de um payload de invólucro de variante,
 * ou o endereço de um elemento de slice — todos são a MESMA imagem, e é por isso que só há uma
 * função. Escrever só a metade ponteiro é o defeito que a sonda `fatfield` mede (registo de
 * 2026-07-29, §5.1).
 *
 * @param LowerCtx ctx  o contexto de lowering a estender
 * @param u32 addr  o VReg do endereço-base da imagem
 * @param LoweredFat fo  o par (ptr, len) a escrever
 * @param u32 line  a linha de origem a carimbar nas instruções emitidas
 * @param u32 col  a coluna de origem a carimbar nas instruções emitidas
 * @return LowerCtx  o contexto com os DOIS stores acrescentados
 * @see load_fat_image
 * @since 0.3.1.0 raiz A
 */
fn store_fat_image(ctx: LowerCtx, addr: u32, fo: LoweredFat, line: u32, col: u32): LowerCtx {
    let with_ptr = ctx_append(ctx, store_inst(addr, fo.ptr, LType::Ptr, line, col))
    let la = ctx_alloc(with_ptr)
    let with_la = ctx_append(la.ctx, field_addr_inst(la.vreg, addr, fat_len_offset(), line, col))
    ctx_append(with_la, store_inst(la.vreg, fo.len, LType::I64, line, col))
}

/**
 * load_fat_image — ler de volta a imagem `{ptr@0, len@8}` escrita por `store_fat_image`.
 *
 * O ÚNICO leitor. Emparelhado com o escritor pela mesma dupla `fat_len_offset`, para que a
 * pergunta "onde vive o comprimento" tenha uma resposta e não duas.
 *
 * @param LowerCtx ctx  o contexto de lowering a estender
 * @param u32 addr  o VReg do endereço-base da imagem
 * @param u32 line  a linha de origem a carimbar nas instruções emitidas
 * @param u32 col  a coluna de origem a carimbar nas instruções emitidas
 * @return LoweredFat  o par (ptr, len) lido
 * @see store_fat_image
 * @since 0.3.1.0 raiz A
 */
fn load_fat_image(ctx: LowerCtx, addr: u32, line: u32, col: u32): LoweredFat {
    let pr = ctx_alloc(ctx)
    let with_pr = ctx_append(pr.ctx, load_inst(pr.vreg, addr, LType::Ptr, line, col))
    let la = ctx_alloc(with_pr)
    let with_la = ctx_append(la.ctx, field_addr_inst(la.vreg, addr, fat_len_offset(), line, col))
    let lr = ctx_alloc(with_la)
    LoweredFat { ctx = ctx_append(lr.ctx, load_inst(lr.vreg, la.vreg, LType::I64, line, col)); ptr = pr.vreg; len = lr.vreg }
}
```

---

**R3 — O campo gordo de struct passa a ser ESCRITO. Fecha as miscompilações silenciosas de §5.1-5.4.**

`store_struct_fields` (`lower.tks:6931`) e `store_assign_value` (`lower.tks:7300`) ganham a bifurcação
que `store_variant_payload` (`lower.tks:4536`) já tem, e que é o modelo a copiar:

```teko
/**
 * store_field_value — escrever UM valor de campo no endereço já calculado `addr`.
 *
 * Bifurca em `is_fat_type` exactamente como `store_variant_payload` (degrau 6) faz para o
 * payload de um invólucro: um campo gordo escreve a sua imagem de 16 bytes por
 * `store_fat_image`, um campo escalar escreve o seu único word. O layout já reserva 16 bytes
 * para o campo gordo (`field_layout_size`) e `lower_fat_field` já lê 16 — antes desta função
 * escrevia-se 8, e o comprimento lido de volta era o que estivesse no slot (medido: 0).
 *
 * @param LowerCtx ctx  o contexto de lowering a estender
 * @param u32 addr  o VReg do endereço do campo
 * @param checker::Type declared  o tipo DECLARADO do campo (não o do valor — uma colocação
 *                                num slot de união constrói o invólucro)
 * @param checker::TExpr value  a expressão do valor a colocar
 * @param u32 line  a linha de origem
 * @param u32 col  a coluna de origem
 * @return LowerCtx | error  o contexto com o(s) store(s), ou propagado do lowering do valor
 * @throws propagado de `lower_fat_expr`/`lower_value_into_type`
 * @since 0.3.1.0 raiz A
 */
fn store_field_value(ctx: LowerCtx, addr: u32, declared: checker::Type, value: checker::TExpr, line: u32, col: u32): LowerCtx | error {
    if is_fat_type(declared) {
        let fo = match lower_fat_expr(ctx, value) { LoweredFat as x => x; error as err => return err }
        return store_fat_image(fo.ctx, addr, fo, line, col)
    }
    let vo = match lower_value_into_type(ctx, declared, value) { Lowered as x => x; error as err => return err }
    ctx_append(vo.ctx, store_inst(addr, vo.vreg, ltype_of(declared, ctx.enums), line, col))
}
```

**Nota que vale o crumb inteiro:** hoje `store_struct_fields` usa `ltype_of(field_vals[i].type)` — o
tipo do VALOR — e não o do CAMPO. Ao passar ao tipo DECLARADO fecha-se, de graça, a mesma classe de
divergência que o degrau 8 fechou no push (*"a placement into a union slot must build the uniform
tag+payload wrapper"*).

---

**R4 — O elemento gordo de slice. É o degrau 10, e sai inteiro dos crumbs anteriores.**

Três sítios, os que `fat_element_push_stop` já nomeia:

1. `lower_array_lit` (`lower.tks:7048`) e `store_array_elements` (7060): `stride` passa a
   `value_image_bytes(elem_t, ctx.enums)`, e o store de cada elemento a `store_field_value`.
2. `lower_index_slice` (`lower.tks:7034`): quando o elemento é gordo o load é `load_fat_image` — e
   por isso `lower_fat_expr` (`lower.tks:5622`) ganha o braço `checker::TIndex => lower_index_fat`,
   o que fecha a paragem `fat-pointer receiver \`index\`` medida em §5.5.
3. `lower_list_push` (`lower.tks:5839`): `fat_element_push_stop` desaparece; `elem_lt`/`esz` passam
   por `value_image_bytes` (16), e o item vai para um slot de item de 16 bytes por `store_fat_image`.
   **Não há boxing:** um valor gordo JÁ é os seus dois words; o ponteiro que carrega já aponta para
   armazenamento de outrem. Isto é o que o distingue da RAIZ B e é a razão pela qual não abre
   nenhuma questão de posse.

---

**R5 — A prótese do resultado do runtime deixa de ser artesanal.**

`call_symbol` (`lower.tks:1911`) ganha a família `teko::str::*`/`teko::string::*` que hoje lhe falta,
e cada entrada gorda resolve para o seu gémeo `_len`, chamado por `lower_len_out_call`
(`lower.tks:6681`) — a função que o degrau 9 já escreveu e que hoje tem um só cliente. Os gémeos que
o fonte do compilador precisa hoje, em `src/runtime/teko_rt.{c,h}`, ao abrigo do ruling do dono de
2026-07-29 (`d4ad700`) e com a mesma razão escrita no sítio: `tk_str_slice_len`, `tk_str_slice_to_len`,
`tk_str_slice_from_len`.

---

**R6 — O backstop honesto, que é o que impede o próximo silêncio.**

Hoje um builtin de `str` sem entrada em `call_symbol` cai em `mangle_fn_symbol` e produz um símbolo
indefinido — falha de LINKER, não paragem nomeada. **Medido, §7 P2.** `call_symbol` passa a parar
por nome para qualquer callee de `call_ns` vazio com prefixo `teko::str`/`teko::string` sem entrada.
É a aplicação directa da lei de `d4ad700`: *"Silêncio é a palavra que manda. Uma paragem ruidosa e
endereçada é um degrau para fechar, não motivo para ir buscar C."*

---

### As fixtures de regressão — desenhadas para FALHAR sem a correcção

Membros novos do canal `own_native` (`examples/regressions/own_native/`), corpus a sair 42 nas duas
rotas. Cada um foi já MEDIDO a dar o número errado com o compilador de hoje (§5), o que é a prova
que o registo desta lane exige e que uma asserção sozinha não dá.

| membro | o que afirma | nativo HOJE | nativo com R1-R4 | C |
|---|---|---|---|---|
| `fat_field_roundtrip` | `Holder{name:str; n:i32}` construído, `.n`, `.len` e o conteúdo por índice | **exit 2** | 42 | 42 |
| `fat_field_offset_shift` | o mesmo com o campo gordo em segundo lugar (offset 8) | **exit 1** | 42 | 42 |
| `fat_field_reassign` | `h.name = "xy"` e reler `.len` | **exit 3** | 42 | 42 |
| `fat_field_as_argument` | `take(h.name)` a ler `.len` do lado de lá | **exit 1** | 42 | 42 |
| `fat_slice_field` | campo `[]i64`: `.len` e `[2]` | **exit 1** | 42 | 42 |
| `fat_slice_literal_index` | `[]str` literal, `.len` e `xs[i].len` de cada elemento | **PARA** (`fat-pointer receiver \`index\``) | 42 | 42 |
| `fat_slice_push_in_loop` | push de `str` num laço, cada elemento relido pelo seu próprio comprimento | **PARA** (`push onto a slice of FAT elements`) | 42 | 42 |
| `fat_str_slice_builtin` | `teko::str::slice/slice_to/slice_from` e o comprimento de cada resultado | **FALHA A LINKAR** (`undefined reference to teko_slice`) | 42 | 42 |

Duas asserções unitárias em `src/lir/lower_test.tkt`, no espírito de
`lwt_agg_empty_variant_const_interns_its_tag_image` (que foi o teste que apanhou o consumidor
esquecido no degrau 6 — uma asserção de LARGURA é o que apanha um consumidor esquecido):

- `lwt_fat_struct_field_stores_both_halves` — o texto do LIR de um `Holder{name = "x"}` tem DOIS
  stores, `Ptr` no offset do campo e `I64` no offset+8;
- `lwt_fat_slice_element_stride_is_sixteen` — o `alloca` de um `[]str` de 3 elementos é 48, não 24.

### Os pontos de ritual — onde o portão completo tem de passar

| depois de | portão |
|---|---|
| R1 | `teko test .` verde **e** corpus `own_native` byte-idêntico ao de hoje nas duas rotas (R1 não muda comportamento; se mudar, R1 está errado) |
| R2 | idem |
| R3 | rota completa de três estágios + as cinco fixtures de campo gordo a 42 nas duas rotas |
| R4 | rota completa + `fat_slice_*` a 42 + auto-construção nativa a passar `global_symbol_names` |
| R5 | rota completa + `fat_str_slice_builtin` a 42 |
| R6 | `teko test .` + uma sonda que chame um builtin `teko::str::*` não implementado e receba PARAGEM NOMEADA, não erro de linker |

### O tamanho, comparado honestamente com continuar degrau a degrau

**Continuar degrau a degrau.** Medido: 10 degraus fecharam em cerca de dois dias, cada um 1-2
commits. O que resta da RAIZ A, contado do inventário de §3 e das medições de §5 e §7:
elemento gordo (3 sítios), escritor de campo gordo (2 sítios), `str::slice`/`slice_to`/`slice_from`
(80 sítios de chamada no fonte), resultado gordo de closure (`lower.tks:2439`), resultado gordo de
despacho de interface (`lower.tks:2105`), retorno gordo de lambda (`lower.tks:2940`), comparação
ORDENADA de `str`, `[][]T`, e os 29 dos 32 pontos de entrada do runtime que devolvem `tk_str` e ainda
não têm gémeo. **Chamemos-lhe 10 degraus.** E três deles — os de §5 — **NÃO SE ANUNCIAM**: não há
paragem a apontar-lhes o sítio, e o `fixpoint` é cego a eles por construção.

**Correcção de raiz.** Seis crumbs, **um ficheiro de produto** (`src/lir/lower.tks`) mais três
entradas em `src/runtime/teko_rt.{c,h}`. Zero mudanças em `src/lir/lir.tks`, zero em
`src/backend/**`, zero em regalloc/stackify/printer/`lir_oracle.tks`. Nenhuma funcionalidade nova de
linguagem, portanto nenhuma restrição de seed de bootstrap.

**A comparação:** a correcção de raiz custa aproximadamente o mesmo que **3** degraus e fecha **10**,
incluindo os três que não se anunciam. E o argumento decisivo não é o custo, é este: os três
silenciosos de §5 **não são fecháveis degrau a degrau**, porque um degrau é uma paragem e eles não
param. Fechá-los exige exactamente a pergunta que este desenho faz — "quem escreve a imagem de um
valor gordo?" — e essa pergunta só tem uma resposta boa se for feita uma vez.

### Tensões de lei, e a resolução

1. **Alargar o stride de elemento parece a "by-value-element lane" que o dono adiou no degrau 4.**
   Não é, e o teste que separa as duas é o que o próprio ruling do degrau 4 usa: *precisa de uma
   decisão de POSSE?* Um agregado por valor precisa (que região? quem liberta?) — por isso foi
   adiado. Um valor gordo não precisa de nenhuma: os seus dois words SÃO o valor, o ponteiro que
   carrega já aponta para armazenamento que já tem dono, e não se copia nada. R1-R4 não acrescentam
   uma única política de alocação. **Resolvido: R1-R4 cabem dentro do ruling do degrau 4, não o
   reabrem.**
2. **"Não devem existir novas emissões em C".** R5 acrescenta a `teko_rt.{c,h}`, que é o RUNTIME
   contra o qual o nativo LINKA, não emissão do compilador — a distinção que `d4ad700` legisla, e
   com o mesmo padrão de razão escrita no sítio que o degrau 9 deixou como exemplo trabalhado.
   **Resolvido, dentro da permissão datada.**
3. **A regra do oráculo.** Todos os defeitos de §5 têm a rota C a acertar e o nativo a errar. Pela
   regra, são bugs do nativo até prova em contrário, e não questões de arquitectura. **Não há
   ruling a pedir. Não há HALT.**

---

## 7. A previsão — que é o que torna isto útil e não apenas arrumado

Ordenada, falsificável, e com o mecanismo nomeado. Onde pude medir em avanço, medi.

### P1 — O degrau 10 é a paragem corrente. **JÁ VERIFICADA nesta sessão.**

```
teko: .: native backend N1: a push onto a slice of FAT elements (`[]str`/`[][]T`) needs the
two-word element stride the array-literal store and the index load do not carry yet (N2)
[in `teko::backend::global_symbol_names`]
```

Auto-construção nativa com `.gen1c` sobre o tip `d4ad700`. Confirma o enunciado ("degrau 10, aberto,
o passo de elementos gordos") e corrige-o num ponto: são **três** sítios, não dois — o store do
literal também (§5.5).

### P2 — Logo atrás do degrau 10 vem `teko::str::slice`/`slice_to`/`slice_from`, e **NÃO como paragem: como FALHA DE LINKER.** **JÁ VERIFICADA em sonda isolada.**

```
$ TEKO_BACKEND=native .gen1c/teko <sonda com teko::str::slice(s, 1, 3)> …
(.text+0x3a): undefined reference to `teko_slice'
collect2: error: ld returned 1 exit status
```

E a gémea escalar, pelo mesmo mecanismo:

```
(.text+0x29): undefined reference to `teko_contains'
```

**Mecanismo, com o código colado.** `call_symbol` (`lower.tks:1911-1928`): com `call_ns` vazio,
`native_builtin_symbol` (io/cov/arena) não conhece `slice`; `is_list_builtin_call` não casa; o ramo
de paragem honesta só dispara para `segs.len == 1` e `teko::str::slice` tem três segmentos — logo cai
em `mangle_fn_symbol("", "slice", false)` = `teko_slice`, que nunca é definido. **É exactamente o
mesmo mecanismo que o degrau 9 mediu para `concat`** (*"failed the link with 'undefined reference to
teko_concat'"*, mensagem do commit `24ee25c`), e que ficou fechado para `concat` e para mais nenhum.

**Volume:** `teko::str::slice` 34 sítios de chamada, `slice_to` 24, `slice_from` 22 — **80** no fonte
do compilador. `ends_with` 37 e `contains` 20 pelo mesmo mecanismo (escalares, mesma falha de link).

### P3 — Fechados o degrau 10 e o P2, a auto-construção produz um gen2 que **constrói e depois mente**. É a previsão mais importante e é a que vale a pena registar antes de acontecer.

Os três defeitos de §5 não param. **383 das 717 declarações de `struct` do compilador (53%) têm
campo gordo.** A mais central é `LStructLayout` (`src/lir/lir.tks:527`), com quatro; é lida por
`layout.field_names.len` em `field_offset_of` (`lower.tks:6885`) e `field_type_of` (6907), e esse
`.len` mede **0** hoje.

**Previsão específica e falsificável:** assim que o gen2 nativo existir, ele falhará a compilar
qualquer programa com `struct`, com um erro da família `unknown field … (internal)` ou com SIGSEGV,
**antes** de o `fixpoint` chegar a comparar gen2 com gen3. Se em vez disso o gen2 compilar
normalmente, o meu modelo está errado e quero saber.

### P4 — Resultado gordo de CLOSURE e de despacho de INTERFACE, ambos já com paragem nomeada à espera

`lower_call_fat`, `src/lir/lower.tks:5668-5669`:

```
if cl.is_closure_call { return error { message = "native backend N1: fat-pointer closure-call result not yet lowered (N2)" } }
if cl.is_iface_dispatch { return error { message = "native backend N1: fat-pointer interface-dispatch result not yet lowered (N2)" } }
```

A segunda já foi vista em corpus antes (`native_iface_fat_known_stop`, e o mapa das 6 pernas conta-a
como uma das 26). Ambas são a mesma prótese (repr. 6) por estender ao lado do chamado — uma vtable
ou um thunk não reservam hoje o slot escondido. **Previsão: aparecem depois de P2, e a correcção é
uma só, não duas.**

### P5 — Retorno gordo de LAMBDA

`lower_return_fat`, `lower.tks:2940`: `"native backend N1: a fat-typed lambda return is not yet
lowered (N2)"`. Mesma prótese, mesma família de P4.

### P6 — Comparação ORDENADA de `str` (`<`, `>=`)

Paragem nomeada já no ficheiro (degrau 7 deixou-a de propósito). Precisa de `tk_str_cmp` e de um
contrato lexicográfico. **Previsão: só aparece se o fonte do compilador ordenar strings** — se
nenhum `<` sobre `str` existir em `src/**`, não aparece nesta lane.

### P7 — Windows, e só Windows: a lista fechada de `is_str_arg_builtin`

`str_pair_by_ref_x86` (`isel_x86_64.tks:942`) rematerializa o par por referência **só** para os sete
símbolos de `is_str_arg_builtin` (`lower.tks:1523`: `tk_print`, `tk_println`, `tk_eprint`,
`tk_eprintln`, `tk_write`, `tk_ewrite`, `tk_panic_str`). O runtime declara **44** assinaturas que
recebem `tk_str` por valor. Qualquer uma das outras 37, alcançada no Win64, passa dois registos onde
o Win64 quer um endereço — **exactamente o SIGSEGV do `own_print_exit` que o doc-comment desse sítio
descreve**, outra vez. Fora do caminho crítico do Linux; morde na perna Windows em 0.3.1.2/0.3.1.3.

### P8 — O que eu prevejo que NÃO vai acontecer, porque uma previsão sem lado negativo não se pode falsificar

**Não vai aparecer nenhum degrau desta lane cuja correcção seja em `src/backend/**`** (isel, encode,
objfile, regalloc, stackify), enquanto a perna for Linux e o alvo o host. Base: 0 em 10 até aqui, e
as 26 mensagens de LOWERING do mapa das 6 pernas byte-idênticas nas seis plataformas contra 1 de
EMISSÃO (`isel x86-64: B1-args`) que só aparece no Windows. Se aparecer um degrau de `src/backend/**`
na perna Linux, o modelo "a lowering é a estrangulação" está errado e isso é notícia.

---

## 7-bis. ADENDA (2026-07-29) — o `char` é o TERCEIRO tipo gordo, e a sequência R1-R6 NÃO o cobre

Levantado pelo dono no mesmo dia, e medido aqui. Duas citações literais:

> *"sobre esses fat-pointers, não lembro se foi implementado, mas, dado que Teko opera em utf8, se
> não me engano tem mais coisa aí (ou deveria): `type byte = u8` / `type char = []byte` /
> `type str = []char`. Logo, um `.len` diz sobre o array em si, não o valor de um item lá dentro."*

> *"Até pq o multi-byte será necessário ao construir a stdlib"*

**O modelo do dono está implementado no runtime e no checker. O `char` está a FALTAR nas listas da
lowering.** Isto entra na RAIZ A e obriga a corrigir a própria RAIZ A em dois pontos.

### 7-bis.1 A evidência de que `char` é gordo — e de que o backend não sabe

`src/checker/type.tks:92` — `char` é uma tag PRÓPRIA, com o layout escrito no comentário:

```
pub type Char = struct { }   // a UTF-8 codepoint (1–4 bytes); distinct by tag, runtime layout == []byte
```

`src/runtime/teko_rt.h:52-59` — e o layout de facto é `{ptr,len}`, o mesmo de `tk_slice_byte`:

```
} tk_char;
// tk_slice_byte — the runtime layout of a `[]byte` slice. Same {ptr,len} shape as tk_char
```

`src/lir/lower_const.tks:187` — e o serializador de consts JÁ SABE que é gordo:

```
checker::TCharLit => error { message = "const aggregate: char value is fat (Tier-B, T-B) (#594)" }
```

Mas as listas da lowering não sabem:

| lista | ficheiro:linha | o que faz com `Char` | verdade |
|---|---|---|---|
| `is_fat_type` | `src/lir/lower.tks:5590` | `_ => false` | é gordo |
| `ltype_of` | `src/lir/lower.tks:270` | `_ => LType::Ptr` (8 bytes) | 16 bytes |
| `typeexpr_is_fat` | `src/lir/lower.tks:8141` | `SliceType` ou `"str"`; `char` não casa | é gordo |
| `is_register_value_type` | `src/lir/lower.tks:6029` | `_ => false` | correcto (um `char` não é valor de registo) |

**É a mesma expressão `ltype_size(ltype_of(t, enums))` já nomeada como raiz textual da RAIZ A, agora
num TERCEIRO tipo.** E é outra vez o padrão do degrau 1: *a documentação estava certa e o código
discordava dela* — aqui em três sítios (`type.tks:92`, `teko_rt.h:59`, `lower_const.tks:187`) contra
dois (`is_fat_type`, `typeexpr_is_fat`).

### 7-bis.2 Mas há um facto MAIOR do que a largura: o `char` tem DUAS representações

`lower_char_lit`, `src/lir/lower.tks:7174`:

```
fn lower_char_lit(ctx: LowerCtx, e: checker::TExpr, c: checker::TCharLit): Lowered {
    let r = ctx_alloc(ctx)
    Lowered { ctx = ctx_append(r.ctx, const_int_inst(r.vreg, utf8_codepoint(c.bytes), e.line, e.col)); vreg = r.vreg }
}
```

O backend nativo baixa `c'A'` para o **INTEIRO 65** — o ponto de código escalar. A rota C baixa-o
para um `tk_char`, uma VISTA `{ptr,len}` sobre os bytes UTF-8. **Não são o mesmo valor com larguras
diferentes: são representações diferentes do mesmo tipo.** É a décima representação do
inventário de §3 — e a única que se representa como UM word por decisão explícita e não por
esquecimento. **A contagem de §3 sobe de nove para dez.**

### 7-bis.3 O que o `char` errado já está a causar — MEDIDO

Sete sondas, mesmo método de §5 (projecto standalone, `main.tks` = `exit(probe())`, compilador
`.gen1c`).

| sonda | o que faz | C | nativo |
|---|---|---|---|
| `charscalar` | `c'A' to u32`, `c'é' to u32` | 0 | **PARA**: ``native backend N1: `char` has no single PrimKind, asked by the cast source (N2)`` |
| `charseq` | `cs[i] != c'A'` | **FALHA** (`cc failed to build the generated C`) | **PARA**: ``… asked by the comparison chain (N2)`` |
| `charfield` | `struct { c: char; n: i32 }` | **FALHA** (`codegen: cyclic value-type dependency`) | **PARA** (no cast, a montante) |
| `charslice` | `[]char` literal + índice + cast | 0 | **PARA** (no cast) |
| `charat` | `teko::str::char_at(s, i)` | 0 | **PARA** (no cast) |
| `charisalpha` | `teko::str::is_alpha(c)` | 0 | **FALHA A LINKAR**: ``undefined reference to `teko_is_alpha`` |
| `charlen` | `c.len` | **erro do checker** (`field access requires a struct receiver`) | idem |
| `bytesym` (CONTROLO) | campo `byte`, literal `[]byte`, índice e push | 0 | **0** |

**Veredicto sobre o `char`, e é melhor notícia do que eu temia: hoje o `char` NÃO produz nenhuma
resposta errada e calada no nativo.** Toda a via que lá chega bate primeiro em `prim_kind_of` — a
MESMA guarda que fez o degrau 7 (``a `str` … has no single PrimKind``) — e pára honestamente, ou
falha a linkar. O `char` está protegido por acidente: é impossível FAZER alguma coisa com um `char`
no nativo sem passar por um cast ou por uma comparação, e as duas param.

**Isso é exactamente o que torna o `char` perigoso da forma oposta:** a protecção é uma guarda a
montante, não um modelo correcto. No dia em que alguém der a `prim_kind_of` um braço para `char`
para "fechar o degrau do cast", o modelo escalar de `lower_char_lit` passa a correr — e aí sim há
respostas erradas e caladas, porque largura de campo, passo de elemento e ABI de argumento continuam
todos a dizer 8.

**Duas falhas ADJACENTES da rota C, medidas aqui e NÃO corrigidas** (são de outra tarefa, e existem
porque para o `char` o oráculo tem buracos): `struct { c: char }` pára a rota C com
`codegen: cyclic value-type dependency` — a mesma mensagem mal-aplicada da família já registada para
o `null` (secção "A ROTA C E O `null` EM COMPOSIÇÃO" do ficheiro irmão); e `char == char` faz o `cc`
falhar sobre o C gerado.

### 7-bis.4 A pergunta de semântica devolvida ao dono — com factos, sem decisão

**O que o esboço diz:** `type byte = u8` / `type char = []byte` / `type str = []char`.

**O que o código faz, medido, não lido:**

| facto | medição |
|---|---|
| `byte` é `u8` | **SIM.** `ltype_of(Byte) => LType::I8` (`lower.tks:272`), `ltype_size(I8) = 1` (`lir.tks:538`), `is_register_value_type(Byte) => true` (`lower.tks:6032`). Sonda `bytesym`: **0 nas duas rotas**. |
| `char` é `[]byte` no LAYOUT | **SIM.** `tk_char` é `{ptr,len}`, *"Same {ptr,len} shape as tk_slice_byte"* (`teko_rt.h:59`). |
| `char` é `[]byte` no TIPO | **NÃO.** `checker::Char` é tag própria (`type.tks:92`, *"distinct by tag"*), não `Slice{element: Byte}`. E `c.len` é **erro do checker** nas duas rotas: `field access requires a struct receiver`. |
| `str` é `[]char` no TIPO | **NÃO.** `checker::Str` é tag própria (`type.tks:93`). A iteração por codepoint é DERIVADA por `tk_str_chars`. |
| **`.len` de um `str` conta BYTES** | **SIM, e medido nas duas rotas.** `"Aéz€".len == 7` (A=1, é=2, z=1, €=3): sonda `strlenbytes` sai **0 no nativo E 0 no C**. |
| a contagem de CODEPOINTS existe, à parte | **SIM.** `teko::str::chars(s).len == 4` e `teko::str::len_chars(s) == 4` para a mesma string — rota C, exit 0. |
| `s[i]` indexa BYTES | **SIM.** `lower_index_str` (`lower.tks:7023`) usa `elem_ty = LType::I8`, stride 1. |

**Resumo em uma frase, para o dono:** o LAYOUT do esboço está implementado (um `char` É `{ptr,len}`
sobre bytes); a ÁLGEBRA DE TIPOS do esboço não está (`str` e `char` têm tag própria, não são
aliases); e `.len` conta **bytes**, com a contagem de codepoints disponível à parte em
`chars()`/`len_chars()`.

**O custo de `.len` passar a contar chars — medido, e é para o dono decidir, não para mim:**

| o que teria de mudar | quantos sítios |
|---|---|
| todo `.len` no fonte do compilador (nem todos são `str` — é o TECTO) | **3 444** |
| `.len` sobre receptor provavelmente `str` (heurística pelo nome do identificador — ESTIMATIVA, não contagem) | **~394** |
| `lower_index_str` (`lower.tks:7023`) teria de indexar por codepoint | 1 sítio, mas passa de O(1) a O(n) |
| a metade COMPRIMENTO de todo o par gordo passaria a ser derivada, não armazenada | as 10 representações de §3 + 7-bis.2 |
| sítios que HOJE dependem de `.len` contar codepoints | **0** — nenhum ficheiro de `src/**` chama `len_chars` fora das tabelas de builtins |

**Ler o custo com honestidade:** não é o 394 que manda, é a última linha. Hoje NADA no compilador
quer codepoints; tudo quer bytes, e quem quer codepoints já tem `chars()`/`len_chars()`. Mudar
`.len` para codepoints trocaria uma leitura O(1) de um campo por uma contagem O(n) em ~394 sítios
quentes e não desbloquearia nenhum consumidor existente. **É trabalho de OUTRA lane, e de uma lane
de LINGUAGEM, não desta** — esta lane não pode mudar a semântica de `.len` sem pôr o `fixpoint` a
comparar dois compiladores com semânticas diferentes. Fica com o dono; a medição está feita.

### 7-bis.5 A resposta sem otimismo: **a sequência R1-R6 NÃO cobre o `char`**

| crumb | cobre `char` de graça? | porquê |
|---|---|---|
| R1 `value_image_bytes` | **SIM, condicionado** | delega em `is_fat_type(t)`. Um braço `checker::Char => true` chega. |
| R2 `store_fat_image`/`load_fat_image` | **SIM** | são agnósticas do tipo — recebem um `LoweredFat`. |
| R3 `store_field_value` | **SIM, condicionado** | delega em `is_fat_type(declared)`. |
| R4 elemento gordo de slice | **SIM, condicionado** | delega em `value_image_bytes`/`is_fat_type`. |
| R5 gémeos `_len` do runtime | **PARCIALMENTE** | `tk_str_chars` devolve `tk_slice_char` (dois words) e cabe no padrão; mas `tk_char_at`/`tk_to_lower`/`tk_to_upper` devolvem `tk_char`, que é uma SEGUNDA forma de resultado gordo. |
| R6 backstop honesto | **SIM** | é sobre `call_symbol`, indiferente ao tipo. |

**Mas há CINCO sítios que enumeram à mão e ficariam errados**, e é por isso que a resposta é NÃO:

| sítio | ficheiro:linha | o que enumera |
|---|---|---|
| `typeexpr_is_fat` | `src/lir/lower.tks:8141` | `parser::SliceType` ou o nome `"str"` — SINTÁCTICO, não vê `char` |
| `field_layout_size` | `src/lir/lower.tks:8176` | consulta `typeexpr_is_fat` → campo `char` fica com 8 bytes |
| `field_layout_align` | `src/lir/lower.tks:8191` | idem |
| `bind_param` | `src/lir/lower.tks:7523` | `typeexpr_is_fat(p.type_ann)` → parâmetro `char` ocupa 1 registo, não 2 |
| `append_param_ltypes` | `src/lir/lower.tks:7756` | idem, do lado da aridade |

**O achado estrutural que isto revela, e que vale mais do que o `char`:** existem **DOIS** predicados
de "é gordo?", um SEMÂNTICO (`is_fat_type`, sobre `checker::Type`) e um SINTÁCTICO (`typeexpr_is_fat`,
sobre `parser::TypeExpr`), e **nada os obriga a concordar**. Hoje concordam por coincidência sobre
`str`/`[]T`. Sobre `char` já discordariam — e a discordância entre um LAYOUT (que usa o sintáctico) e
um STORE (que usa o semântico) é precisamente a forma da miscompilação silenciosa medida em §5.

A sequência passa portanto a ter **oito** crumbs, com um novo ANTES de tudo e um novo no fim.

---

**R0 — UM só predicado de "é gordo", antes de R1.** (Novo, e é agora o primeiro crumb.)

```teko
/**
 * is_fat_type — é `t` um tipo cujo VALOR são dois words (`{ptr, len}`)?
 *
 * A ÚNICA resposta semântica do backend. `str` e `[]T` sempre o foram; `char` também é
 * (`src/checker/type.tks:92`, *"runtime layout == []byte"*; `src/runtime/teko_rt.h:59`, *"Same
 * {ptr,len} shape as tk_slice_byte"*), e a sua ausência desta lista era a razão pela qual um campo
 * `char`, um elemento de `[]char` e um parâmetro `char` recebiam 8 bytes onde a verdade são 16.
 *
 * @param checker::Type t  o tipo a classificar
 * @return bool  verdadeiro para `str`, `[]T` e `char`
 * @see typeexpr_is_fat  o gémeo SINTÁCTICO, que responde à mesma pergunta sobre uma anotação
 * @since 0.3.1.0 raiz A, crumb R0
 */
fn is_fat_type(t: checker::Type): bool {
    match t {
        checker::Str => true
        checker::Slice => true
        checker::Char => true
        _ => false
    }
}

/**
 * typeexpr_is_fat — o gémeo SINTÁCTICO de `is_fat_type`: é a anotação `te` um tipo de dois words?
 *
 * Existe porque o layout de um `struct` e a aridade de uma lista de parâmetros são calculados a
 * partir da ANOTAÇÃO, antes de haver um `checker::Type` resolvido. Os dois predicados TÊM de
 * responder o mesmo para o mesmo tipo — um layout que reserve 8 bytes e um store que escreva 16 é
 * a corrupção silenciosa medida a 2026-07-29 (§5), e o inverso é a mesma coisa ao contrário.
 * `lwt_fat_predicates_agree` afirma a concordância, tipo a tipo.
 *
 * @param parser::TypeExpr te  a anotação a classificar
 * @return bool  verdadeiro para `[]T`, `str` e `char`
 * @see is_fat_type  o gémeo SEMÂNTICO
 * @since 0.3.1.0 raiz A, crumb R0
 */
fn typeexpr_is_fat(te: parser::TypeExpr): bool {
    match te {
        parser::SliceType => true
        parser::NamedType as nt => named_single_segment_is(nt.path, "str") || named_single_segment_is(nt.path, "char")
        _ => false
    }
}
```

**O portão de R0 é uma asserção unitária, e é ela que impede a próxima divergência:**
`lwt_fat_predicates_agree` — para cada um de `str`, `[]i64`, `char`, `byte`, `i32`, `bool`,
`ptr<i32>` e um struct nominal, `is_fat_type(<tipo>) == typeexpr_is_fat(<anotação>)`. É o mesmo
serviço que `lwt_agg_empty_variant_const_interns_its_tag_image` prestou no degrau 6: uma asserção de
LARGURA é o que apanha o consumidor esquecido.

**R0 não pode aterrar sozinho.** Assim que `is_fat_type(Char)` for verdadeiro, `lower_fat_expr`
recebe `char` e não tem produtor para ele — paragem NOMEADA, não silêncio, mas ainda assim uma
regressão de superfície. R0 aterra no MESMO vagão que R7.

---

**R7 — a representação do `char`, no fim da sequência.** (Novo.)

1. `lower_char_lit` (`lower.tks:7174`) deixa de produzir um inteiro: `c'é'` interna os seus bytes
   UTF-8 em rodata (o que `lower_str_lit_fat` já faz) e produz o par `(ptr, len)` — um
   `lower_char_lit_fat`, braço novo de `lower_fat_expr`.
2. `prim_kind_of` deixa de ser o caminho de um cast de `char`: `char to u32` passa a chamar
   `tk_char_to_u32(ptr, len)` (já existe, `teko_rt.h:242`), pelo padrão que `lower_str_compare` usou
   para `tk_str_eq` no degrau 7.
3. `char == char` idem, por bytes, pelo mesmo `tk_str_eq` — um `char` é uma vista de bytes e a
   igualdade de codepoints É a igualdade dos seus bytes UTF-8.
4. `call_symbol` ganha a família `char` (`char_at`, `is_alpha`, `is_digit`, `is_space`, `to_lower`,
   `to_upper`, `chars`, `len_chars`) — hoje **todas** manglam para um símbolo indefinido (medido:
   `teko_is_alpha`, `teko_chars`, `teko_len_chars`).
5. Os pontos de entrada que devolvem `tk_char` (`tk_char_at`, `tk_to_lower`, `tk_to_upper`) precisam
   do mesmo gémeo de parâmetro de saída que R5 desenha para `tk_str` — é a SEGUNDA forma de
   resultado gordo, e é por isso que R5 só cobre o `char` "parcialmente".

**R7 é o único crumb desta sequência que NÃO está no caminho crítico da auto-construção**: o fonte do
compilador não usa `char` em lado nenhum do caminho de `teko build` (medido — §7-bis.6). Pode
aterrar depois de a lane fechar. **Mas R0 não pode aterrar sem ele**, e é por isso que viajam juntos.

---

### 7-bis.6 A stdlib multi-byte — quem depende de quê, medido, com uma correcção

Foi-me dado que `src/fmt/fmt.tks`, `src/encoding/json/json.tks`, `src/encoding/url/url.tks` e
`src/encoding/base64/base64.tks` exercitam o caminho do `char`. **Verifiquei cada um e a lista está
errada em quatro de quatro** — o que não enfraquece o argumento, fortalece-o, porque o que eles usam
de facto é mais usado ainda:

| módulo | o que usa DE FACTO | é `char`? |
|---|---|---|
| `src/fmt/fmt.tks` | `f_is_digit(c: byte)`, `f_is_alpha(c: byte)`, `f_is_hex(c: byte)` — funções TEKO locais sobre **`byte`** (`fmt.tks:87,91,95`) | **NÃO.** O `fmt` não toca em `char`. E o `byte` está correcto (sonda `bytesym`). |
| `src/encoding/json/json.tks` | `str_slice_chars(…)` ×1 + `bytes_of_str(…)` ×1 | **NÃO** — `str_slice_chars` devolve **`str`**, `bytes_of_str` devolve **`[]byte`**. Família P2. |
| `src/encoding/url/url.tks` | `str_slice_chars(…)` ×3 + `bytes_of_str(…)` ×4 | **NÃO.** Família P2. |
| `src/encoding/base64/base64.tks` | `bytes_of_str(…)` ×1 | **NÃO.** Família P2. |
| **`src/regex/regex.tks`** | **`chars(input)` (`regex.tks:366` e `386`) — o ÚNICO consumidor de `[]char` em todo o `src/**`** | **SIM.** |

**A dependência real da stdlib não é o `char`; é a família de builtins que devolvem valores gordos.**
Contada:

- `bytes_of_str` (`-> []byte`, gordo): **20 sítios em 11 módulos** — `base64`, `csv`, `json`, `url`,
  `io/stream`, `iter/byte_iter`, `iter/str_iter`, `regex`, `text`, `build/regression`,
  `checker/comptime_fold`;
- `str_slice_chars` (`-> str`, gordo): 4 sítios (`json` 1, `url` 3);
- `chars` (`-> []char`, slice de elementos gordos): 2 sítios, ambos em `regex`.

**Medido, e é uma correcção importante ao que me foi dito:**

```
$ TEKO_BACKEND=native … teko::str::chars("Aéz€")
(.text+0x59): undefined reference to `teko_chars'

$ TEKO_BACKEND=native … teko::str::bytes_of_str("Aéz€")
(.text+0x26): undefined reference to `teko_bytes_of_str'

$ TEKO_BACKEND=native … teko::str::str_slice_chars("Aéz€", 0, 2)
(.text+0x3a): undefined reference to `teko_str_slice_chars'

$ TEKO_BACKEND=native … teko::str::len_chars("Aéz€")
(.text+0x15): undefined reference to `teko_len_chars'
```

**`s.chars()` NÃO produz hoje um slice corrompido: falha a LINKAR.** Fica dito assim porque é a
diferença entre um defeito presente e um defeito a um commit de distância. O resto é verdade: **a
corrupção é o que se obtém no minuto em que alguém acrescentar a entrada de `chars` a `call_symbol`
sem R0/R1**, porque aí o slice constrói-se com passo 8 sobre elementos de 16. Ou seja — fechar o
degrau 10 de forma ESTREITA (só `[]str`, enumerando à mão) não é neutro para o `char`: **cria** a
corrupção que hoje não existe.

Com a rota C como oráculo, os números que a stdlib deve dar (`.gen1c`, exit 0):
`"Aéz€".len == 7` (bytes), `chars(s).len == 4` (codepoints), `len_chars(s) == 4`,
`bytes_of_str(s).len == 7`.

### 7-bis.7 O que isto muda nas previsões

**P2 sobe de "80 sítios" para a família inteira, e ganha CINCO instâncias novas MEDIDAS.** Além de
`teko_slice` e `teko_contains` (§7 P2), agora também `teko_chars`, `teko_len_chars`,
`teko_bytes_of_str`, `teko_str_slice_chars` e `teko_is_alpha`. **Sete símbolos indefinidos
distintos, todos medidos, todos pelo mesmo `call_symbol` (`lower.tks:1911-1928`).** A previsão passa
a ser: à medida que a auto-construção avançar, cada builtin de `teko::str::*` que o fonte alcança dá
uma falha de LINKER, uma de cada vez, sem paragem nomeada a dizer onde. **R6 é o crumb que converte
esta série inteira num degrau nomeado, e passa a ser o de melhor retorno por linha da sequência.**

**P9 (nova) — a stdlib multi-byte não arranca sem R5+R6.** `src/regex/regex.tks` é o primeiro módulo
a bater no `[]char` (`regex.tks:366,386`); os outros dez batem primeiro em `bytes_of_str`. Nenhum
está no caminho de `teko build`, por isso **não** bloqueiam esta lane — mas bloqueiam qualquer
programa de utilizador que os importe, e é isso que o dono quer dizer com *"o multi-byte será
necessário ao construir a stdlib"*.

**P10 (nova) — o `char` não vai dar resposta errada e calada ANTES de alguém "corrigir"
`prim_kind_of`.** Medido: toda a via bate na guarda e pára. **Previsão falsificável: se aparecer um
commit que dê a `prim_kind_of` um braço para `char` sem trazer R0+R7 junto, no dia seguinte há
respostas erradas e caladas com `char` — campo de struct, elemento de slice e argumento, exactamente
as três formas de §5.** Se isso não acontecer, o meu modelo do `char` está errado.

### 7-bis.8 O que isto muda na comparação de tamanho

| | antes desta adenda | depois |
|---|---|---|
| crumbs | 6 (R1-R6) | **8** (R0, R1-R6, R7) |
| ficheiros de produto | 1 (`src/lir/lower.tks`) + 3 entradas de runtime | **o mesmo 1**, + runtime (gémeos de `tk_str` **e** de `tk_char`) |
| degraus fechados | 10 | **10, mais a série inteira de símbolos indefinidos** (7 medidos, e são todos os `teko::str::*` que o fonte alcançar) |
| desbloqueia | a auto-construção | **a auto-construção E a stdlib multi-byte** (11 módulos por `bytes_of_str`, `regex` por `[]char`) |
| custo em degraus equivalentes | ~3 | **~4** (R0 e R7 são pequenos; R7 é o único fora do caminho crítico) |

**E o argumento deixa de ser aritmética de degraus.** Fechar o degrau 10 de forma estreita —
enumerando `Str`/`Slice` à mão, que é o caminho natural para quem só quer o `[]str` da paragem —
**cria** a corrupção do `[]char` que hoje não existe (§7-bis.6). A sequência de raiz não é "a mesma
coisa mais barata": é a única versão que não abre um buraco novo ao fechar o antigo.

---

## 7-ter. ADENDA II (2026-07-29) — o dono DECIDIU: `.len` conta CARACTERES. O quanto.

A pergunta de §7-bis.4 deixou de ser pergunta. Ruling do dono, literal:

> *"Semanticamente, quando conto uma string, quero saber quantos caracteres ela tem, pense em uma
> validação de um campo, o tamanho máximo de um texto. O usuário do sistema não sabe bytes e nem
> imagina que um caractere acentuado ocupa 2 bytes e um emoji ocupa 4.*
>
> *Se o dev precisa dos bytes, então teria que usar uma stdlib `teko::strings::get_bytes(str):
> []byte` ou o inverso `teko::strings::from_bytes([]byte): str` e depois isso pode se expandir
> para outros encondings."*

O dono decide o QUÊ; esta secção dá o QUANTO. **Não implemento nada aqui e não escolho nada aqui.**

### 7-ter.1 Boa notícia primeiro: as duas funções que ele pede JÁ EXISTEM

`src/checker/scope.tks:614-615`:

```
if name == "bytes_of_str" { … ret = bytes_t … }   // bytes_of_str(str): []byte
if name == "str_from_utf8" {   // str_from_utf8([]byte): str | error (ROUND 0 / B.36)
```

- `teko::str::bytes_of_str(str): []byte` **é** o `get_bytes` do ruling. Já usado em **20 sítios de
  11 módulos** (§7-bis.6).
- `teko::str::str_from_utf8([]byte): str | error` **é** o `from_bytes` do ruling — e já devolve
  `| error`, isto é, já VALIDA em vez de confiar.

**Nada há a construir do lado do acesso a bytes.** O que falta é (a) o NOME, (b) a extensibilidade a
outros encodings, e (c) a troca do significado de `.len`. Só (c) é trabalho de motor.

### 7-ter.2 A divergência de nome, com o custo de cada opção — apontada, não escolhida

Contagem de usos em `src/**` (varrimento literal):

| namespace | usos | membros |
|---|---|---|
| `teko::str::` | **642** | `concat` (495), `slice` (34), `ends_with` (37), `slice_to` (24), `slice_from` (22), `contains` (20), `bytes_of_str`, `str_from_utf8`, `chars`, `len_chars`, `char_at`, `is_alpha`… |
| `teko::string::` | 13 | `concat` (alias já existente) |
| `teko::text::` | 4 | `valid_utf` (2), `str_from_utf` (1), `concat` (1) |
| **`teko::strings::`** | **0** | não existe |

| opção | custo em sítios | consequência |
|---|---|---|
| (a) `teko::strings::` NOVO, ao lado | **0** a mudar | passam a existir **quatro** namespaces de string (`str`, `string`, `text`, `strings`). É a opção barata e a que mais fragmenta. |
| (b) RENOMEAR `teko::str::` → `teko::strings::` | **655** (642 + 13) | um só namespace, mas toca 655 sítios e o seed de bootstrap tem de aceitar o nome novo ANTES de o corpus o usar (sequenciamento de seed). |
| (c) pôr `get_bytes`/`from_bytes` no `teko::str::` que já existe | **0** a mudar; 2 aliases a acrescentar | um namespace a MENOS do que hoje se `teko::text::` for absorvido. As funções já lá estão com outro nome. |

**Para a extensibilidade a outros encodings que o ruling pede,** o ponto de desenho é o mesmo nas
três: `get_bytes(s)` é `get_bytes(s, Encoding::Utf8)` com o encoding por omissão, e
`from_bytes(b)` é `from_bytes(b, Encoding::Utf8): str | error`. O `| error` já existe em
`str_from_utf8` e é o que torna a extensão segura — um decode que falha PARA em vez de inventar.
**Decisão do dono; a medição está feita.**

### 7-ter.3 O custo de `.len` contar chars — medido, e é o número que manda

**Hoje `.len` é O(1) nas duas rotas.** No nativo é `lower_len_field` (`src/lir/lower.tks:6965`), que
devolve a metade COMPRIMENTO do par já baixado — *"read directly (no `load`)"*. Na rota C é um campo
de `tk_str`. Não há travessia nenhuma.

**A capacidade de contar chars existe e é O(n)** — `src/runtime/teko_rt.h:243-245`:

```
// tk_str_len_chars — count UTF-8 codepoints in s. Walks the byte sequence using lead-byte widths;
// no allocation, no copy. (The `len_chars` builtin lowers to this.)
uint64_t tk_str_len_chars(tk_str s);
```

Trocar o significado de `.len` troca **O(1) por O(n)**. O que isso custa no fonte do compilador,
contado por varrimento (nomes declarados `: str` no ficheiro, e o seu uso na mesma linha):

| classe | sítios | onde dói |
|---|---|---|
| **A — `str.len` como LIMITE DE CICLO**, reavaliado por iteração | **162** | `src/lexer/lexer.tks` **20**, `src/fmt/fmt.tks` 16, `src/codegen/codegen.tks` 14, `src/runtime/teko_rt.tks` 12, `src/build/regression.tks` 11, `src/build/project.tks` 10, `src/build/tkr.tks` 10, `src/build/manifest.tks` 8, `src/build/regr_group.tks` 7, `src/checker/resolve.tks` 6 |
| **B — `str.len` em ARITMÉTICA DE BYTES** (`.len ± N`) | **24** | enumerados abaixo — são os que PARTEM, e partem calados |
| **C — nome `str` INDEXADO por byte** (`s[i]`) | **259** | `codegen` 27, `lexer` 27, `project` 22, `teko_rt.tks` 19, `regression` 17, `fmt` 17, `parse_lit` 15, `lower` 13, `sort/cmp` 13 |
| **D — sítios que HOJE querem codepoints** | **0** | nenhum ficheiro de `src/**` chama `len_chars` fora das tabelas de builtins |

**A classe A é o custo de DESEMPENHO, e é quadrático.** O padrão dominante do compilador é

```
loop { if p >= source.len { break } … p++ }
```

— `src/lexer/lexer.tks:31,134,146,188,209` e mais quinze no mesmo ficheiro. Com `.len` a percorrer a
string, um lexer que hoje é O(n) sobre o ficheiro passa a **O(n²)**. Não é uma regressão de
percentagem: é de ordem, no ficheiro mais quente do compilador.

### 7-ter.4 A classe B — os sítios que PARTEM, e partem em silêncio. Nomeados.

Estes fazem aritmética de BYTES a partir de `.len`. Se `.len` passar a contar chars, cada um lê o
byte errado — **e em ASCII puro nenhum deles muda**, que é exactamente o que os torna perigosos: a
suite de testes fica verde e o comportamento com acentos muda por baixo.

| ficheiro:linha | o que faz |
|---|---|
| `src/checker/resolve.tks:38` | `if name.len != ns.len + 2 + bare.len { return false }` — o comprimento de `ns::bare` em BYTES |
| `src/checker/resolve.tks:42` | `if name[ns.len + 1] != b':' { return false }` — o byte do separador `::`, localizado por `.len` |
| `src/build/assemble.tks:31` | `path[path.len - 9] == b'/'` |
| `src/build/project.tks:1431` | `out_dir[out_dir.len - 1] == b'/'` + `slice_to(out_dir, out_dir.len - 1)` |
| `src/build/project.tks:1957` | `dir[dir.len - 1] == b'/'` + `slice_to(dir, dir.len - 1)` |
| `src/build/project.tks:2820` | idem, terceiro sítio da mesma forma |
| `src/build/project.tks:4135` | `let last = s.len - needle.len` |
| `src/build/regr_group.tks:407` | `slice(split.stmts, 0, split.stmts.len - split.tail.len)` |
| `src/build/regression.tks:632` | `line[line.len - 1] == b'\r'` + `slice_to(line, line.len - 1)` |
| `src/build/tkr.tks:809` | `slice_to(line, line.len - 3)` |
| `src/codegen/codegen.tks:5801` | `let seglen = other.len - s` |
| `src/fmt/fmt.tks:314,329,342` | `last_end = source.len + 1` (sentinela) |
| `src/numeric/dec/dec.tks:268` | `s.len - 1` |
| `src/parser/parse_expr.tks:186` | `spec_src[spec_src.len - 1] != b']'` |

**Vinte e quatro no total.** `src/checker/resolve.tks:38,42` é o pior da lista: é a resolução de
NOMES do checker, a localizar o `::` por aritmética de bytes sobre `.len`. Um namespace ou um
identificador com um acento passaria a resolver mal — silenciosamente.

**E a classe C (259 sítios) é a razão pela qual a classe B não é o fim da história:** `s[i]` indexa
BYTES no backend nativo (`lower_index_str`, `src/lir/lower.tks:7023`, `elem_ty = LType::I8`,
stride 1). Um `.len` que conte chars e um `s[i]` que indexe bytes **deixam de ser o mesmo eixo** —
todo o idioma "limita por `.len`, lê por `[i]`" fica incoerente por construção. Ou `s[i]` também
passa a indexar por codepoint (e aí é O(n) por acesso, ou seja O(n²) outra vez), ou o idioma
inteiro tem de migrar para `bytes_of_str(s)`.

**A migração que isto realmente pede, dita sem rodeios:** o compilador deve deixar de andar sobre
`str` e passar a andar sobre `[]byte`. A ferramenta já existe (`bytes_of_str`, 20 sítios já a usam),
e o resultado seria **mais** correcto do que hoje, porque tornaria explícito que o lexer trabalha em
bytes. Mas são ~259 sítios de migração de FONTE, não uma mudança de motor.

### 7-ter.5 A interacção com R0-R7 — a pergunta mais importante, e a resposta é: REFORÇA

Foi-me perguntado se a mitigação óbvia — guardar as duas contagens no ponteiro gordo, tornando `str`
de **três words** `{ptr, bytes, chars}` — destrói a sequência que está a arrumar dois.

**Não destrói. É o argumento mais forte que este documento tem para fazer R0-R7 PRIMEIRO.**

A razão é que R0-R2 não codificam "dois" em lado nenhum: substituem aritmética dispersa por funções
nomeadas. Contando as edições necessárias para passar `str` a três words, ANTES e DEPOIS:

| o que teria de mudar para `str` ser 3 words | HOJE | DEPOIS de R0-R2 |
|---|---|---|
| a largura da imagem | 10 sítios com `{ptr@0,len@8}` escrito à mão (o inventário de §3 + §7-bis.2) | **1** — `fat_value_bytes()` devolve 24 |
| onde vive a segunda/terceira metade | os mesmos 10 | **1** — `fat_len_offset()` + um irmão |
| o escritor em memória | 5 escritores independentes (campo, elemento, slot, payload de invólucro, const) | **1** — `store_fat_image` |
| o leitor em memória | 4 leitores independentes | **1** — `load_fat_image` |
| a largura do campo de struct | `field_layout_size` **e** `typeexpr_is_fat`, que podem discordar | **1** — os dois já unificados por R0 |
| a aridade de um parâmetro gordo | `bind_fat_param` + `append_param_ltypes`, dois sítios que têm de concordar | 2, mas com o predicado já único |
| o invólucro de variante | 24 → 32 bytes | **1** — `variant_wrapper_bytes()`, já é função desde o degrau 6 |
| o slot escondido do retorno | `bind_ret_len_slot` + `lower_call_fat`, 8 → 16 bytes | 2, já emparelhados |

**Hoje: encontrar dez sítios escritos à mão e não falhar nenhum. Depois de R0-R2: editar duas
funções.** E "não falhar nenhum" é precisamente o que já falhou uma vez — é o defeito de §5.

**Corolário de sequenciamento, e é a recomendação central desta adenda:**

> **A correcção de raiz (R0-R7) é PRÉ-REQUISITO da mudança semântica, não concorrente dela.**
> Fazer a semântica primeiro é mudar um alvo em movimento em dez sítios dispersos; fazer a raiz
> primeiro torna a semântica uma edição de duas funções. A ordem inversa não é mais lenta — é a que
> tem probabilidade de introduzir uma nova miscompilação silenciosa da mesma família de §5.

**E o `char` fica coberto?** Sim, mas só com R0+R7 dentro. O ruling torna isto obrigatório e não
opcional: se `.len` conta caracteres, então a contagem de caracteres deixa de ser um canto da stdlib
(hoje: 2 sítios, ambos em `src/regex/regex.tks`) e passa a estar por baixo de **toda** a contagem de
strings do sistema. O caminho `chars()`/`len_chars()`/`tk_char` passa a ser caminho quente. **Sem R0
(`is_fat_type`/`typeexpr_is_fat` a conhecerem `Char`) e sem R7 (a representação do `char`), a
semântica nova assenta sobre um tipo que o backend ainda dimensiona a 8 bytes.** Está dito, como
pedido: a minha sequência cobre-o, e só o cobre com R0 e R7 lá dentro.

### 7-ter.6 As quatro mitigações, com o custo de cada uma — para o dono escolher

| mitigação | `.len` | custo em memória | o que quebra |
|---|---|---|---|
| **M1** — `.len` chama `tk_str_len_chars` | **O(n)** | 0 | os 162 limites de ciclo passam a O(n²); o lexer é o pior caso |
| **M2** — `str` a TRÊS words `{ptr, bytes, chars}` | **O(1)** | +8 bytes por `str` em todo o programa | fatiar um `str` passa a recontar chars → o custo O(n) muda de sítio, não desaparece; e as duas contagens podem dessincronizar |
| **M3** — içar `.len` para fora dos ciclos no fonte | O(n) mas 1× | 0 | migração de FONTE em ~162 sítios; não resolve as 24 aritméticas de bytes |
| **M4** — o compilador migra para `[]byte` (`bytes_of_str`) e `.len` de `str` fica para o utilizador | **O(1)** para `[]byte` | 0 | ~259 sítios de migração de fonte, mas torna EXPLÍCITO que o lexer anda em bytes — mais correcto do que hoje |

**Sem recomendar entre elas** (é semântica do dono), duas observações de facto: M2 é a única que
mantém `.len` O(1), e é exactamente a que R0-R2 tornam barata (§7-ter.5). M4 é a única que resolve
as classes B e C ao mesmo tempo, e é a única que não deixa incoerência entre `.len` e `s[i]`.

### 7-ter.7 De quem é este trabalho — recomendação de sequenciamento

| trabalho | lane | porquê |
|---|---|---|
| R0-R7 (a raiz A) | **ESTA lane, 0.3.1.0** | está no caminho crítico da auto-construção; sem ela a lane não fecha |
| `get_bytes`/`from_bytes` com nome novo + encodings | **lane própria, de STDLIB** | as funções já existem; é API e nomes, não motor. Zero impacto na auto-construção. |
| `.len` a contar chars | **lane própria, de LINGUAGEM, depois de R0-R7** | muda a semântica observável de 3 444 sítios potenciais e a complexidade do lexer. **Não pode ser feita nesta lane:** o `fixpoint` compara gen2 com gen3, e um compilador com semântica de `.len` diferente do seu seed não é comparável com o anterior — a mudança tem de atravessar um bump de versão com o seed a acompanhar. |
| migrar o compilador para `[]byte` (M4) | **lane própria, e pode correr em PARALELO** | não depende de nenhuma decisão semântica: andar em bytes explicitamente é correcto hoje e continua correcto depois |

**A ordem que recomendo, e a razão em uma linha cada:**

1. **R0-R7** — desbloqueia a auto-construção e torna tudo o resto barato.
2. **M4 (migrar o compilador para `[]byte`)** — pode começar já, em paralelo, e é a única coisa que
   torna a mudança de `.len` segura para o próprio compilador.
3. **`.len` conta chars**, com o bump de versão que o seed exige, depois de 1 e 2.
4. **`get_bytes`/`from_bytes` + encodings**, quando o dono fixar o namespace.

**Um aviso que não quero deixar implícito:** o ponto 3 feito ANTES do ponto 2 põe os 24 sítios de
§7-ter.4 a ler o byte errado, em silêncio, e em ASCII puro **nenhum teste do projecto dá por isso**.
É a mesma classe de defeito que §5 mediu e a mesma classe que o `fixpoint` não vê. Se o dono quiser
o ponto 3 primeiro, o preço mínimo é uma fixture de corpus por cada um dos 24 sítios, com entrada
acentuada. **(REVISTO em 7-quater.4: são 25, todos MECÂNICOS, e o problema não é a dificuldade —
é que nada obriga a fazê-los.)**

---

## 7-quater. ADENDA III (2026-07-29) — a iteração MEDIDA, o `char` como DEFEITO registado, e o que resta

Dois rulings do dono no mesmo dia, ambos literais.

**Ruling 1 — o `char` sai desta lane, e continua açúcar:**

> *"char precisa de um vagão próprio então. Mexe em praticamente a codebase inteira. Mas, concordo
> que dá para manter o apontamento atual, char viraria apenas um açúcar (como já é hoje), mas
> colocaria no str o que havia sugerido, a contagem de chars e de bytes. Como já tem função para
> extrair chars, sai até barato, mas eu diria que, str.len seriam chars, o outro contador seria dos
> ponteiros, mas tem algo maior, iterar em uma string, a maioria das linguagens usam char (ou rune)
> na saída e não bytes."*

**Ruling 2 — a divergência de representação do `char` é DEFEITO, não desenho:**

> *"`o lower_char_lit baixa c'A' para o inteiro 65, enquanto a rota C baixa para uma vista {ptr,len}`
> - para mim é bug mesmo se não fizermos nada sobre str e char"*

### 7-quater.1 (A) A ITERAÇÃO — medida, e é a melhor notícia deste dossiê

**Um `str` É aceite hoje como sujeito de for-each, e produz BYTES.** Medido nas duas rotas com
`"Aéz"` (A=1 byte, é=2, z=1 → 4 bytes, 3 codepoints):

| sonda | o que faz | C | nativo |
|---|---|---|---|
| `iterslice` (controlo) | `loop x in [10,20,30]` conta iterações | 3 | **3** |
| **`iterstr`** | **`loop c in "Aéz"` conta iterações** | **4** | **4** |
| `iterbytes` | `loop b in bytes_of_str("Aéz")` | 4 | falha a linkar (P2) |
| `iterchars` | `loop c in chars("Aéz")` | **3** | falha a linkar (P2) |
| `elemisbyte` | `takes_byte(c)` dentro do `loop c in "Aéz"` | **4 (compila)** | **4 (compila)** |
| `elemischar` | `takes_char(c)` no mesmo sítio | **`argument type mismatch`** | **`argument type mismatch`** |
| `elemvalue` | `if c == 65 to byte` | 1 | **1** |
| `itertuple` | `loop (c, i) in "Aéz"`, último índice | 3 | **3** |

**O elemento é um `byte`, provado pelas duas direcções:** `takes_byte(c)` compila e conta 4;
`takes_char(c)` é erro de tipo na mesma linha. As duas rotas dão exactamente o mesmo.

**E a razão é a melhor possível: NÃO EXISTE máquina de iteração para mudar.** O for-each é um
desugar SINTÁCTICO no PARSER — `parse_loop_foreach_slice`, `src/parser/loop_head.tks:668` — cujo
próprio doc-comment o diz:

> *"lower to the PROVEN counter form `loop mut _i in 0 .. <src>.len` … The element MODE is chosen
> type-blind … Slice-ness / integer-index / element-type are validated by the ordinary checker rules
> on the emitted `.len` / index / element-assignment."*

```
loop x in E { B }        desugara para        let _src = E
                                              loop mut _i in 0 .. _src.len { let x = _src[_i]; B }
```

**COROLÁRIO, e é a resposta ao "algo maior" do dono:** a iteração dá bytes hoje por UMA razão só —
`.len` conta bytes e `s[i]` indexa bytes. **Se `.len` passar a contar chars E `s[i]` a indexar
chars, o for-each passa a dar chars SOZINHO: zero linhas no parser, zero no desugar, zero numa
máquina de iteração que não existe.** O "algo maior" não é trabalho separado — é consequência
automática da decisão sobre `.len`/`[i]`.

**E A ARMADILHA, o outro lado da mesma moeda, MEDIDA:** se `.len` contar chars e `s[i]` continuar a
indexar bytes, o desugar itera `n_chars` vezes indexando bytes. Simulei o desugar à mão sobre
`"Aéz"` (`.len`=4, `len_chars`=3), sonda `trapdesugar`, rota C, **exit 143** = `100 + bytes*10 +
chars` = `100 + 40 + 3`:

```
3 iterações, indexadas por BYTE, visitam 'A'(65), 0xC3(195), 0xA9(169)
   -> 'é' vem partido em duas metades sem sentido, e 'z' NUNCA É VISITADO
```

**Mudar `.len` sem mudar `[i]` junto cria uma corrupção silenciosa nova, no construto mais usado da
linguagem.** Os dois eixos movem-se juntos ou nenhum se move.

**Custo no fonte do compilador: ZERO.** Há **3** sítios de `loop … in …` em todo o `src/**`, e os
três estão dentro de literais de mensagem de erro (`src/parser/loop_head.tks:733`). O compilador não
usa for-each; usa `loop { if i >= n.len { break } … i++ }`. **A mudança de tipo de elemento não toca
uma linha de código do compilador.**

**E os dois eixos de fatiar JÁ EXISTEM** (sonda `sliceaxis`, rota C, **exit 23**):
`teko::str::slice(s,0,2)` → **2 bytes** (eixo de BYTES); `teko::str::str_slice_chars(s,0,2)` →
**3 bytes**, isto é `'Aé'` = 2 CHARS (eixo de CHARS). A API já tem os dois; falta decidir qual deles
`.len` e `[i]` nomeiam.

### 7-quater.2 (B) O que resta da sequência, e onde entra o terceiro word

**Confirmado no vagão `cargo/0.3.1.0-degrau-10`, lido por `git show` SEM merge** (a história fica
linear):

| crumb meu | aterrou como | commit |
|---|---|---|
| R1 (autoridade de largura) | `elem_byte_stride` / `elem_byte_align` (`lower.tks:7106/7121`) — `if is_fat_type(t) { return fat_slot_bytes() }` | `2549c9f` |
| R2 (um escritor/leitor) | `store_array_elem`, `lower_index_fat`, `lower_fat_element_push` | `2549c9f` |
| R3 (campo gordo escrito) | `if is_fat_type(value.type)` no store de campo (`lower.tks:6996`) | `9fa7e5c` |
| R4 (elemento gordo) | os três sítios | `2549c9f` |
| R6 (backstop honesto) | `unresolved_builtin_stop` | `9fa7e5c` |

**A previsão P2 verificou-se DUAS vezes.** Previ `undefined reference to teko_contains` e recomendei
R6 como o crumb de melhor retorno. O degrau 11 é agora ``native backend N1: builtin `contains` not
yet lowered (N2)`` — **o símbolo que nomeei, a anunciar-se como paragem nomeada precisamente porque
R6 aterrou.** Uma falha de linker virou degrau endereçado.

**Falta:** **R0** (unificar os dois predicados), **R5** (os gémeos `_len` — é o degrau 11 e os que
vêm atrás) e **R7**, agora repartido (§7-quater.3).

**Onde entra o terceiro word — com uma correcção ao que eu disse.** Eu previa "editar duas funções".
Medido no vagão, são duas, mas **não é uma**:

```
src/lir/lower.tks:6496   fn fat_slot_bytes(): u32 { ltype_size(LType::Ptr) + ltype_size(LType::I64) }
src/lir/lower.tks:8334       if typeexpr_is_fat(te) { return ltype_size(LType::Ptr) * (2 to u32) }
```

**Duas computações INDEPENDENTES do mesmo 16, ligadas a DOIS predicados diferentes** —
`elem_byte_stride` pergunta ao semântico (`is_fat_type`), `field_layout_size` ao sintáctico
(`typeexpr_is_fat`). O vagão fechou o degrau 10 correctamente e, ao fazê-lo, **criou um segundo sítio
que responde "quanto mede um valor gordo"**. Hoje concordam nos 16 por coincidência aritmética.

Isto não enfraquece a conclusão de §7-ter.5 — **confirma-a e afia-a**: o terceiro word custa hoje
**2** edições em vez das 10 de antes de R1-R4, e custaria **1** se R0 unificasse predicados e
larguras. **R0 deixou de ser um crumb do `char` e passou a ser o crumb do TERCEIRO WORD** — o
pré-requisito directo do ruling do dono sobre `str`, com `char` ou sem ele.

### 7-quater.3 (C) DEFEITO REGISTADO — a representação do `char` diverge entre rotas

**Estado: BUG ABERTO. Dono: ruling literal de 2026-07-29 (Ruling 2, acima). Não corrigido nesta
lane. Sequência abaixo.**

| rota | representação de `c'A'` | sítio |
|---|---|---|
| nativa | **o inteiro 65** (ponto de código escalar) | `lower_char_lit`, `src/lir/lower.tks:7174` — `const_int_inst(r.vreg, utf8_codepoint(c.bytes), …)` |
| C (oráculo) | **`{ptr, len}`**, vista sobre os bytes UTF-8 | `tk_char`, `src/runtime/teko_rt.h:52-57` |

Não é diferença de largura: é de REPRESENTAÇÃO. Pela regra do oráculo já legislada nesta lane é bug
do nativo até prova em contrário — e o dono confirmou-o explicitamente.

#### (3) Qual das duas é a certa — RESPONDIDO PELO DONO

**Ruling 3, literal (2026-07-29):**

> *"E char sendo açúcar, um c'X' deveria fazer lowering para []byte, e aqui está o seu char gordo em
> C"*

Isto fecha a pergunta e resolve a incoerência que eu tinha levantado: **a gordura não é do `char`, é
do `[]byte` em que ele se desfaz.** Um açúcar com representação gorda é coerente porque o açúcar
DESAPARECE e o que fica é um `[]byte` legitimamente gordo. A minha própria análise já apontava para
aí e o dono disse-o melhor:

- **`{ptr,len}` é a representação que faz de `char` um AÇÚCAR** — é literalmente o que `[]byte` já
  é, sem nada acrescentado. `checker::Char` mantém tag própria só para dar erros de tipo melhores;
  por baixo é o mesmo par.
- **O inteiro 65 é o que faria de `char` um PRIMITIVO NOVO** — um quarto tipo de máquina inventado
  no backend, que nenhuma outra camada conhece. É o oposto de açúcar.

**O ruling do oráculo e o ruling do açúcar apontam para o MESMO lado, e o nativo está errado pelos
dois.** A representação certa é `{ptr,len}`.

**O custo honesto dessa resposta, que registo para não parecer gratuita:** o escalar É melhor para
comparar e converter (O(1), sem memória), e o `{ptr,len}` do `tk_char` empresta para dentro da string
de origem — `src/runtime/teko_rt.h:338`: *"Returns a tk_char view INTO s.ptr (no copy); the caller
must ensure s outlives the result"*, um risco de tempo de vida real. Nada disto muda a decisão hoje
(o oráculo decide e o açúcar concorda), mas é matéria para o vagão do `char`, não para se fingir que
não existe.

#### (1) O conjunto MÍNIMO que fecha o bug sem destapar a corrupção

A P10 diz que dar a `prim_kind_of` um braço para `char` sem mais nada destapa as três respostas
erradas de §5. A lista exacta do que tem de vir junto — **mais curta que o R7 inteiro**, porque R3,
R4 e R6 já aterraram:

| peça | o que faz | porque é OBRIGATÓRIA |
|---|---|---|
| **R0** | unificar `is_fat_type` e `typeexpr_is_fat` (o sintáctico passa a perguntar ao tipo RESOLVIDO, não ao nome), ambos com braço `Char`; braço `Char` em `ltype_of` | sem ela `elem_byte_stride` (semântico) diz 16 e `field_layout_size` (sintáctico) diz 8 para o MESMO campo — **e isto NÃO é hipotético: já corrompe hoje atrás de um alias de tipo, medido em §7-quater.3-bis** |
| **P-lit** | `lower_char_lit_fat`: `c'é'` interna os bytes em rodata e devolve o par; braço `checker::TCharLit` em `lower_fat_expr` | é o bug em si |
| **P-cast** | `char to u32` sai de `prim_kind_of` e passa a chamar `tk_char_to_u32(ptr, len)` (já existe, `teko_rt.h:242`) | é a guarda que hoje protege; tirá-la sem substituto É a P10 |
| **P-eq** | `char == char` pelo `tk_str_eq` já usado no degrau 7 (a igualdade de codepoints É a dos seus bytes UTF-8) | mesma guarda, mesmo motivo |
| *(aterrado)* R3 | store de campo gordo escreve as duas metades | um campo `char` passa a ser escrito inteiro, sem trabalho novo |
| *(aterrado)* R4 | `elem_byte_stride` / `lower_index_fat` / `lower_fat_element_push` | `[]char` ganha passo 16, índice e push de graça |
| *(aterrado)* R6 | `unresolved_builtin_stop` | `char_at`/`is_alpha`/`to_lower`/`chars` PARAM por nome em vez de manglar — por isso NÃO precisam de vir agora |

**Quatro peças novas** (R0, P-lit, P-cast, P-eq) — chamo-lhes **R7-min**. Todo o resto do R7 original
(a família de builtins de `char`, os gémeos de saída para `tk_char`) **fica no vagão do `char`**, e
fica em segurança porque R6 fá-los parar por nome.

**A ordem dentro do R7-min importa, e é a lição do degrau 3 e do degrau 6:** R0 primeiro (senão as
duas larguras discordam); P-cast e P-eq ANTES de P-lit (senão, no instante em que `is_fat_type(Char)`
for verdadeiro, `lower_fat_expr` recebe um `char` sem produtor). Aterram no MESMO commit; nenhuma é
portável sozinha.

#### (2) Onde ela vive — o argumento é o inverso do de arrumação

O dono diz que o bug é independente do vagão do `char`. Concordo, e a medição diz mais: **R7-min já
não depende de nada que esteja nesse vagão.** Depende de R0 — que passou a ser o crumb do TERCEIRO
WORD (§7-quater.2) e pertence a ESTA lane de qualquer forma — e de R3/R4/R6, que **já aterraram
aqui**.

**Recomendo: R7-min vem para ESTA lane; o vagão do `char` fica com a superfície** (família de
builtins, gémeos de `tk_char`, semântica de iteração por codepoint, encodings).

O argumento não é conveniência, é o estado intermédio: entre hoje e o vagão do `char`, esta lane vai
mexer em `is_fat_type`, `typeexpr_is_fat`, `fat_slot_bytes` e `field_layout_size` para pôr o terceiro
word no `str`. **São exactamente as quatro funções de que o R7-min precisa.** Fazer o `char` depois é
operar as mesmas quatro duas vezes, a segunda com o alvo já em movimento — a receita de §5 outra vez.
Fazer R0 + R7-min agora é operar uma vez, com o `char` a entrar como mais um `=> true` numa lista que
já ficou única.

**E há um estado intermédio a PROIBIR explicitamente, que é o que a P10 descreve:** enquanto o
R7-min não aterrar, **ninguém pode dar a `prim_kind_of` um braço para `char`**. A paragem honesta é
a guarda; removê-la sem substituto troca uma paragem por uma resposta errada e calada. Fica como
regra escrita, no espírito do que a lane já fez com `NATIVE-AGG-SLICE-BY-ADDRESS`.

#### (4) O teste que apanha a divergência — e por que não podia existir

**Hoje não pode existir um membro de corpus POSITIVO sobre `char`.** Medido em §7-bis.3: toda a via
nativa que toca um `char` pára (`prim_kind_of`) ou falha a linkar, portanto um programa que afirme
comportamento de `char` não constrói pelo nativo, e o corpus `own_native` exige as duas rotas a sair
42.

**O que pode existir HOJE:** uma fixture de PARAGEM CONHECIDA, o padrão que a lane já usa
(`native_iface_fat_known_stop`) — sonda `c'A' to u32`, a fixar pelo nome a mensagem ``native backend
N1: `char` has no single PrimKind, asked by the cast source (N2)``. Vai a vermelho no dia em que o
R7-min aterrar, que é o que uma fixture de paragem conhecida serve para fazer.

**O que passa a poder existir COM o R7-min** — e é o teste que devia ter existido desde sempre:

| teste | o que afirma | onde |
|---|---|---|
| `char_roundtrip` | `c'A' to u32 == 65`, `c'é' to u32 == 233`, `c'A' == c'A'`, `c'A' != c'é'`, um campo `char` de struct relido, um `[]char` indexado | corpus `own_native`, **42 nas duas rotas** |
| `lwt_char_lit_lowers_to_a_fat_pair` | o TEXTO do LIR de `c'A'` é um endereço de rodata **mais** um comprimento, não um `const_int` | `src/lir/lower_test.tkt` |

**E a lição institucional, maior do que o teste:** esta lane já escreveu a regra que teria apanhado
isto, depois da miscompilação da variante DECLARADA — *"UMA linha de corpus por FORMA DE TIPO"*. O
`char` é uma forma de tipo que **ninguém acrescentou ao corpus**. Não faltou mecanismo novo; faltou
aplicar o que já estava escrito. A regra deve passar a enumerar as formas por extenso — `byte`,
`char`, `str`, `[]T`, struct, class, enum, variante inline, variante declarada, união com `null`,
união com `error` — para que *"não me lembrei do `char`"* deixe de ser possível.

### 7-quater.3-bis O RAIO DE EXPLOSÃO, e a leitura do coordenador posta à prova

Duas contagens, verificadas (varrimento literal de `src/**`):

```
b'X' no fonte de produto: 931
c'X' no fonte de produto:  16
```

E os 16 confirmam-se como o coordenador disse: **nenhum é um `char` usado como VALOR.** São a
maquinaria que DESCREVE o literal — `ast.tks:175`, `tast.tks:31`, `typer.tks:33`, `token.tks:157,160`,
`lexer.tks:606,608,711`, `fmt.tks:63,239,364`, `codegen.tks:10192` — e o próprio reconhecedor do
lexer (`lexer.tks:712`) testa `b'c'`, um literal de BYTE. **O compilador descreve o `c'x'` e não o
usa em lado nenhum.** O raio de explosão do produtor é, de facto, mínimo.

**E o produtor já existe, inteiro.** `lower_str_bytes_fat` (`src/lir/lower.tks:6622`) interna bytes
UTF-8 em rodata e devolve o par `(ptr, len)`. `lower_char_lit_fat` é uma delegação de uma linha para
ela. A leitura do coordenador — *"não é dar representação gorda ao `char`, é deixar de fingir que ele
é escalar, reencaminhando para a maquinaria de `[]byte` que R3/R4 acabaram de arrumar"* — está
**CERTA quanto ao produtor**.

**Mas está ERRADA quanto à conclusão de que a P10 pode não se aplicar. E não é opinião minha: é
medição.**

Fui procurar se os dois predicados — `is_fat_type` (semântico) e `typeexpr_is_fat` (sintáctico) — já
discordam hoje, sem `char` nenhum pelo meio. **Discordam, e isso é um DEFEITO PRESENTE, medido no
tip, que ninguém tinha levantado.** Basta um ALIAS DE TIPO: a anotação é `NamedType "S"`, que
`typeexpr_is_fat` não reconhece, enquanto o tipo resolvido é `Str`, que `is_fat_type` reconhece.

| sonda | fonte | C | nativo |
|---|---|---|---|
| `aliasparam` | `type S = str` + `fn takes(x: S, …) { x.len … }` | **0** | **PARA**: ``native backend N1: `x` is not a fat-pointer local (internal)`` |
| `aliasfield` | `type S = str` + `struct H { s: S; n: i32 }`, ler `h.s.len` | **0** | **exit 2 — resposta errada e calada** |
| `aliasslice` | `type L = []i64` + `struct H2 { xs: L; n: i32 }` | **0** | **exit 2 — resposta errada e calada** |

**Isolado até ao valor exacto** (sonda `aliasobs`, com `n = 41` para o distinguir do comprimento 5):

```
C  exit=0   -> h.s.len == 5   (correcto)
N  exit=1   -> h.s.len == 41  (leu o CAMPO VIZINHO como comprimento)
```

A causa, com o código: `field_layout_size` (`lower.tks:8333`) pergunta a `typeexpr_is_fat(NamedType
"S")` → **falso** → reserva **8** bytes para `s` em offset 0, e põe `n` em offset 8. `lower_fat_field`
(`lower.tks:6409`) lê `ptr@0` e `len@0+8` — **que é o slot do `n`**. O leitor sai pela ponta do campo
para dentro do vizinho.

**Isto é a QUARTA miscompilação silenciosa deste dossiê, e é a mais importante para a decisão, porque
não tem nada a ver com `char`:** existe hoje, com `str` e com `[]T`, atrás de um alias de tipo. **R0
deixa de ser higiene ou pré-requisito do terceiro word — é a correcção de um defeito vivo.**

**Veredicto sobre a leitura do coordenador:**

| afirmação | veredicto |
|---|---|
| *"baixar `c'X'` para `[]byte` reencaminha para maquinaria que já existe e já é gorda"* | **CONFIRMADO.** `lower_str_bytes_fat` faz o trabalho todo; a peça nova é uma linha. |
| *"o raio de explosão é minúsculo — ninguém no fonte de produto depende da representação actual"* | **CONFIRMADO.** 16 usos, todos descritivos. |
| *"portanto a P10 pode não se aplicar, e a correcção cabe já nesta lane sem R0"* | **REFUTADO, com medição.** Os dois predicados já discordam hoje sem `char`; pôr `Char` em `is_fat_type` sem pôr em `typeexpr_is_fat` acrescenta um TERCEIRO tipo a um defeito que já está a corromper. |
| *"a correcção é pequena, segura e imediata"* | **CONFIRMADO — mas só COM o R0**, e R0 é ele próprio pequeno e agora obrigatório por mérito próprio. |

**Ou seja, o coordenador chega ao destino certo por um caminho que precisa de uma perna a mais, e a
perna a mais paga-se sozinha.** A correcção CABE nesta lane. O que não cabe é fazê-la sem o R0.

#### P11 — uma previsão sobre o vagão, verificável num comando

R3 (aterrado em `9fa7e5c`) faz o STORE de um campo gordo escrever as DUAS metades. Mas o layout de um
campo gordo ATRÁS DE UM ALIAS continua a reservar 8 bytes (`typeexpr_is_fat` não mudou). **Previsão:
no vagão, a sonda `aliasobs` deixa de ler o vizinho e passa a ESCREVER por cima dele** — o store de
`s` escreve `len@8`, que é o slot do `n`, portanto `h.n` passa a ler **5** (o comprimento) em vez de
41. R3 terá convertido uma leitura errada numa corrupção do campo vizinho.

Verificação, no vagão, sem merge: compilar a sonda `aliasobs` com `h.n` afirmado — se `h.n != 41`, a
previsão confirma-se. **Se se confirmar, R0 passa de "próximo crumb" a URGENTE**, porque R3 tornou o
defeito destrutivo em vez de apenas errado.

#### As DUAS leituras de "açúcar", e qual custa menos

O ruling diz *"c'X' deveria fazer lowering para []byte"*. Há duas maneiras de o cumprir, e a
diferença importa:

| leitura | onde o açúcar desaparece | custo | consequência |
|---|---|---|---|
| **L1 — açúcar na LOWERING** | `checker::Char` sobrevive; `lower_char_lit_fat` produz o par | R0 + P-lit + P-cast + P-eq (o R7-min) | o checker continua a dar erros de tipo bons (`takes_char(byte)` continua a ser erro) |
| **L2 — açúcar no CHECKER** | `char` resolve para `Slice{element: Byte}`; o backend nunca vê `Char` | `is_fat_type` não precisa de braço (já vê `Slice`), mas `typeexpr_is_fat` continua a precisar, **e perde-se a distinção de tipo**: `char` e `[]byte` passam a ser o mesmo tipo, logo `takes_char(some_bytes)` deixa de ser erro | mexe no checker, que é o que o dono disse que *"mexe em praticamente a codebase inteira"* |

**Recomendo L1**, e o argumento é do próprio dono: ele manteve `char` como açúcar *"como já é hoje"*
— e hoje `checker::Char` é tag própria justamente para dar erros de tipo (medido: `takes_char(c)` num
`loop c in str` é `argument type mismatch`, §7-quater.1). L2 deitaria fora essa rede. **L1 põe o
açúcar exactamente onde o ruling o quer — no lowering — e mantém o checker a fazer o seu trabalho.**

Note-se que **em qualquer das duas leituras o R0 é obrigatório**, porque `typeexpr_is_fat` trabalha
sobre a ANOTAÇÃO SINTÁCTICA, antes de qualquer resolução, e é ele que decide o layout do campo e a
aridade do parâmetro. Não há caminho que dispense o R0.

### 7-quater.4 (D) Os sítios de classe B, reclassificados — e são 25, não 24

Recontei com o segundo contador em mente. **São 25** (corrijo a minha contagem anterior).
*MECÂNICO* = troca `.len` pelo contador de bytes e mais nada; *PENSAR* = a intenção do sítio é
comprimento voltado ao utilizador.

| # | sítio | o que faz | veredicto |
|---|---|---|---|
| 1 | `src/build/assemble.tks:31` | `path[path.len - 9] == b'/'` | MECÂNICO |
| 2-4 | `src/build/project.tks:1431,1957,2820` | tirar `/` final + `slice_to(…, .len - 1)` | MECÂNICO |
| 5 | `src/build/project.tks:4135` | `let last = s.len - needle.len` | MECÂNICO |
| 6 | `src/build/regr_group.tks:407` | `slice(stmts, 0, stmts.len - tail.len)` | MECÂNICO |
| 7 | `src/build/regression.tks:632` | tirar `\r` final | MECÂNICO |
| 8 | `src/build/tkr.tks:809` | `slice_to(line, line.len - 3)` | MECÂNICO |
| 9-11 | `src/checker/resolve.tks:38,42,44` | localizar o `::` de `ns::bare` por aritmética de bytes | MECÂNICO |
| 12 | `src/codegen/codegen.tks:5801` | `let seglen = other.len - s` | MECÂNICO |
| 13-15 | `src/fmt/fmt.tks:314,329,342` | `last_end = source.len + 1` (sentinela) | MECÂNICO |
| 16 | `src/numeric/dec/dec.tks:268` | `s.len - 1` | MECÂNICO |
| 17-18 | `src/parser/parse_expr.tks:186,189` | `spec_src[spec_src.len - 1] != b']'` + `slice(…, 1, .len - 1)` | MECÂNICO |
| 19-21 | `src/parser/parse_lit.tks:137,148,160` | sufixos de literal (`bi`, `d`) por byte | MECÂNICO |
| 22-24 | `src/runtime/teko_rt.tks:565,573,577` | `s.len - suffix.len` / `s.len - needle.len` | MECÂNICO |
| 25 | `src/text/text.tks:37` | `if s.len - i <= lead.cont` — **o validador de UTF-8** | MECÂNICO, e o mais eloquente: o validador de UTF-8 a contar bytes |

**Vinte e cinco de vinte e cinco MECÂNICOS. Zero precisam de pensar. Zero queriam chars.** São todos
aritmética de bytes sobre texto interno — caminhos, identificadores, sufixos de literal, código-fonte.
Nenhum é *"quantos caracteres tem este campo"*.

**Isto MUDA a recomendação de sequenciamento, mas não na direcção óbvia.** O risco deixa de ser
"25 sítios difíceis" e passa a ser pior: **25 edições triviais que NADA obriga a fazer.** Os dois
contadores são ambos `u64`; trocar o significado de `.len` mantém os 25 a compilar sem um único
aviso. **O compilador não pode apanhar um esquecido.** E em ASCII puro nenhum teste do projecto muda
de cor.

**A consequência de desenho — e é uma OPÇÃO para o dono, não uma decisão minha:** a DIRECÇÃO da
mudança decide se ela é verificada pela máquina ou pela diligência humana.

| direcção | os 25 sítios | quem os encontra |
|---|---|---|
| **D1** — `.len` passa a chars; nome novo para bytes | continuam a compilar, com o significado trocado | **ninguém**. 25 riscos silenciosos, mais o que houver fora de `src/**` |
| **D2** — `.len` sobre `str` vira ERRO por uma versão (*"diga `.chars` ou `.bytes`"*), e só depois `.len` volta como chars | **deixam de compilar** | **o compilador**. ~394 erros, cada um uma escolha explícita |
| **D3** — `.len` fica bytes; chars ganha nome próprio | intocados | não se aplica — mas não dá a intuição de utilizador que o dono pediu |

**D2 entrega exactamente a semântica que o dono quer** (`.len` conta chars no fim) **e converte 25
riscos silenciosos em ~394 erros que a máquina encontra sozinha.** Custa uma versão de transição. É a
única das três que não depende de alguém se lembrar de nada — e *"não me lembrei"* é literalmente a
causa registada da divergência do `char` (§7-quater.3, ponto 4).

**O mesmo raciocínio aplica-se ao `[i]`:** como `.len` e `[i]` têm de mover juntos (§7-quater.1), D2
deve cobrir os dois — `s[i]` sobre um `str` também erra durante a transição, obrigando a nomear o
eixo. Os dois eixos já existem na API (`slice` vs `str_slice_chars`, medido); falta obrigar quem
escreve a nomear o seu.

### 7-quater.5 A sequência revista, com tudo o que aterrou

| # | trabalho | lane | estado |
|---|---|---|---|
| 1 | R1, R2, R3, R4, R6 | esta | **ATERRADO** (`2549c9f`, `9fa7e5c`) |
| 2 | **R5** — gémeos `_len` do runtime para os builtins de `str` | esta | **ABERTO — é o degrau 11** (``builtin `contains` not yet lowered``) |
| 3 | **R0** — unificar os dois predicados e as duas larguras | esta | **ABERTO e URGENTE.** Deixou de ser crumb do `char` e deixou de ser higiene: corrige uma miscompilação silenciosa VIVA (alias de tipo, §7-quater.3-bis) e é o pré-requisito do TERCEIRO WORD |
| 4 | **R7-min** — R0 + `lower_char_lit_fat` (uma linha, delega em `lower_str_bytes_fat`) + `char to u32` + `char == char` | esta (argumento em 7-quater.3 e 3-bis) | **ABERTO — o DEFEITO registado.** Cabe já nesta lane, com R0 |
| 5 | `str` a três words (`.len` chars + contador de bytes) | lane de LINGUAGEM, depois de 3 | 1 edição se R0 aterrar; 2 se não |
| 6 | iteração por codepoint | **NENHUMA — sai de graça com o ponto 5** | zero linhas de parser (§7-quater.1) |
| 7 | vagão do `char`: builtins, gémeos de `tk_char`, encodings | vagão próprio | seguro atrás do R6 |
| 8 | `get_bytes`/`from_bytes` + namespace | lane de STDLIB | as funções já existem sob outro nome |

**A regra a escrever antes de qualquer uma delas:** enquanto o R7-min não aterrar, **`prim_kind_of`
não recebe braço para `char`**. A paragem é a guarda.

---

## 8. Resumo — confirma ou refuta, e o que recomendo

**A hipótese do dono REFUTA-SE na letra.** O runtime foi tocado em **2 de 10** degraus e nunca
sozinho; `src/lir/lower.tks` foi tocado em **10 de 10**; `src/backend/**` em **0 de 10**. Nenhum
degrau desta lane foi um degrau de runtime.

**E confirma-se no espírito, com a seta invertida.** Nos dois toques, o runtime estava certo e o
backend é que não sabia falar com ele — e a razão está escrita no próprio `teko_rt.h:214-222`: a
captura de resultado do backend é **um** registo, e um `tk_str` devolvido por valor ocupa **dois**.

**A suspeita sobre os valores gordos CONFIRMA-SE, com uma correcção que muda o plano.** Não há uma
raiz comum, há **duas**, e elas alternam:

- **RAIZ A — os valores gordos são cidadãos de segunda.** Degraus 5, 6, 7, 9, 10. **Dez**
  representações de "dois words" espalhadas pelo ficheiro, nenhuma partilhada, e uma décima primeira
  que não existe (o elemento de slice). São **três** os tipos gordos, não dois: `str`, `[]T` e
  **`char`** — e o `char` falta em `is_fat_type`, em `ltype_of` e em `typeexpr_is_fat` (§7-bis).
- **RAIZ B — o valor de um agregado é o endereço de um slot de frame.** Degraus 4 e 8, mais o
  invólucro devolvido que pendura. Já reclassificada como BUG pelo dono.
- Os degraus 1, 2 e 3 não são de nenhuma das duas, e já fecharam.

**O achado mais grave não é nenhum degrau: são três respostas erradas e caladas**, previstas pelo
modelo e MEDIDAS nesta sessão — construir um struct com campo `str`/`[]T` e ler `.len` dá **0**;
atribuir a esse campo não muda o comprimento; passá-lo como argumento leva o comprimento errado. A
metade ponteiro está certa, a metade comprimento nunca é escrita, e o caminho de `const` está certo
(que é a razão pela qual isto sobreviveu). **53% dos structs do compilador têm um campo gordo, e um
deles é `LStructLayout`.**

**E o `char` é o terceiro tipo gordo, que nenhuma lista da lowering conhece** (§7-bis). Hoje não
mente — toda a via bate em `prim_kind_of` e pára, ou falha a linkar (`teko_is_alpha`, `teko_chars`,
`teko_len_chars`, `teko_bytes_of_str`, `teko_str_slice_chars`: **sete** símbolos indefinidos
distintos, todos medidos). Mas está protegido por uma guarda a montante, não por um modelo certo:
fechar o degrau 10 de forma ESTREITA **cria** a corrupção de `[]char` que hoje não existe.

**E o ruling de `.len` a contar caracteres** (§7-ter) não muda o veredicto — muda a ORDEM. Medido:
`.len` é O(1) hoje nas duas rotas; contar chars é O(n) (`tk_str_len_chars`); o compilador tem **162**
limites de ciclo sobre `str.len` (20 só no lexer, que passaria de O(n) a O(n²)), **24** sítios de
aritmética de bytes que partiriam em SILÊNCIO (e em ASCII puro nenhum teste daria por isso —
`src/checker/resolve.tks:38,42` é o pior), **259** sítios que indexam um `str` por byte, e **0**
sítios que hoje queiram codepoints. As duas funções que o dono pede já existem sob outro nome
(`teko::str::bytes_of_str` e `teko::str::str_from_utf8`); `teko::strings::` tem **0** usos hoje
contra **642** de `teko::str::`.

**O que recomendo, por ordem:**

1. **Não fechar o degrau 10 sozinho.** É o crumb R4 de um desenho de oito, e fechá-lo sozinho entrega
   um compilador que compila e mente (P3) — e, se for fechado enumerando `Str`/`Slice` à mão, cria a
   corrupção do `[]char` (§7-bis.6).
2. **Abrir o vagão da RAIZ A com os oito crumbs R0-R7.** Um ficheiro de produto
   (`src/lir/lower.tks`), entradas de runtime, zero mudanças na LIR e em `src/backend/**`. Custa
   cerca de quatro degraus e fecha dez, mais a série inteira de símbolos indefinidos — incluindo os
   três defeitos que nunca se anunciariam.
3. **Pôr as fixtures de §6 no corpus `own_native` ANTES das correcções**, medidas a falhar. Seis já
   têm o número errado registado aqui.
4. **R6 primeiro se houver pressa** — o crumb mais barato, e converte SETE falhas de linker medidas
   numa paragem nomeada. Uma paragem endereçada é um degrau; um `undefined reference` é arqueologia.
5. **A mudança semântica de `.len` é de OUTRA lane, e DEPOIS de R0-R7** — não por hierarquia, por
   custo: hoje passar `str` a três words seria encontrar dez sítios escritos à mão sem falhar
   nenhum; depois de R0-R2 é editar duas funções (§7-ter.5). **A raiz é pré-requisito da semântica,
   não concorrente dela.** E ela não cabe nesta lane por uma razão dura: o `fixpoint` compara gen2
   com gen3, e um compilador com semântica de `.len` diferente do seu seed não é comparável — precisa
   de um bump de versão com o seed a acompanhar.
6. **Migrar o compilador para `[]byte` (M4) pode começar já, em paralelo** — não depende de decisão
   semântica nenhuma, é correcto hoje e continua correcto depois, e é a única coisa que torna a
   mudança de `.len` segura para o próprio compilador.
7. **Reportar, sem transformar em vagão:** a RAIZ B continua aberta e é independente; a lista fechada
   de `is_str_arg_builtin` (P7) morde no Windows mais tarde; e a rota C tem dois buracos novos
   medidos à volta do `char` (`struct { c: char }` e `char == char`).

**As previsões pelas quais quero ser julgado:** P2 (`undefined reference to teko_slice`, já medida)
vem a seguir ao degrau 10; P3 (o gen2 que constrói e depois falha a compilar qualquer `struct`) vem
a seguir a P2; e P10 — se alguém der a `prim_kind_of` um braço para `char` sem trazer R0+R7, no dia
seguinte há respostas erradas e caladas com `char`, nas mesmas três formas de §5. Se P3 não se
verificar — se o gen2 nativo compilar um `struct` normalmente — o modelo deste documento está errado,
e isso é a coisa mais útil que ele pode produzir.

## Padrão transversal: GÉMEOS QUE DIVERGIRAM (observado 2026-07-29)

Três achados independentes do mesmo dia têm a **mesma forma**, e nenhum deles é um erro que alguém
cometeu. São **assimetrias entre caminhos que deviam ser gémeos**, criadas quando um lado evoluiu e o
outro ficou parado.

| gémeos | o lado protegido | o lado esquecido | como foi achado |
|---|---|---|---|
| layout do `error` | rota C: **6 campos** (`tk_error`, 72 bytes) | rota nativa: **1 campo** (16 bytes) | degrau 22, ao parar em `e.line` |
| adopção de literal antes do `type_join` | `type_if`: **tem** (`trailing_number`/`literal_adopts`) | `type_match` e `type_array_lit`: **não têm** | agente a medir outra coisa |
| corte de `str` | `str_slice_chars`: caracteres, **10 usos** | `slice`/`slice_to`/`slice_from`: bytes, **104 usos** | desenho de ranges |
| `texpr_diverges` / `cg_expr_diverges` (D2) | — | **cópia duplicada** com o mesmo defeito | auditoria de fluxo |
| `lower_tail_non_expr` / `..._fat` (degrau 20) | escalar: fechado no degrau 13 | fat: **nunca recebeu o mesmo fallback** | CI, degrau 20 |

**Cinco instâncias, não três.** E o `lower_tail_non_expr_fat` tinha o buraco **escrito no próprio
doc-comment** ("no fixture in this closure needs a diverging fat tail") — estava assumido, não
esquecido, e mesmo assim ninguém voltou lá.

### Porque isto importa mais do que cada caso

Todos foram achados **por tropeço**: um agente a medir outra coisa, ou o CI a bater na paragem
seguinte. Nenhum foi achado por alguém a procurá-lo. E dois deles (`error` e corte de `str`) são
divergências **silenciosas** — não param, produzem valores diferentes conforme o caminho.

### A varredura que ainda não fizemos

Procurar deliberadamente **pares que deviam concordar e não concordam**:

- checker contra codegen (mesma decisão implementada duas vezes — o D2 provou que existe);
- rota C contra rota nativa (o `error` provou que existe);
- `_fat` contra escalar (o degrau 20 provou que existe);
- builtin registado contra função Teko real com o mesmo nome (`text.tks` já mostrou o caso).

Cada par que divergir é um bug já presente, não um por vir. **Provavelmente rende mais que subir
degraus às cegas** — mas não cabe na esteira a fechar; fica proposto para depois da promoção.

## A varredura FOI feita, e rendeu 50 (medido 2026-07-30, degrau 31)

A secção anterior fecha com *"a varredura que ainda não fizemos"* e propõe procurar deliberadamente
pares que deviam concordar. Um desses pares foi varrido, com instrumento, e o número é o argumento:

| par | instrumento | divergências no fonte do próprio compilador |
|---|---|---|
| predicado gordo SINTÁCTICO (`typeexpr_is_fat`) contra a verdade do checker (`resolve_type` + `is_fat_type`) | `teko::lir::fat_divergence_guard` | **50** antes do conserto, **0** depois |

**Cinquenta**, todas da mesma classe — um parâmetro anotado com um alias QUALIFICADO de um gordo
(`vt_table: checker::TypeTable`, `table: checker::TypeTable`, …) — e **nenhuma visível** antes de a
guarda existir. O self-build só mostrava UMA de cada vez, e mostrava-a como paragem interna num
módulo (`teko::codegen::cg_pair_is_iface_vtable`) que nada tinha a ver com a causa.

### O que a varredura ensinou, e não estava previsto

**1. O predicado sintáctico não nasceu por desleixo — nasceu por não ter a quem perguntar.**
`checker::TFunction.params` é `[]parser::Param`, *"carried from the parser unchanged"*, e um
`StructBody` carrega só `type_ann`. O checker resolve o tipo de RETORNO para a TAST e **nunca** os de
PARÂMETRO nem de CAMPO. Não há resposta gravada para o backend ler nesses dois sítios. A hipótese
"é redundante, apaga-se" não sobrevive à `tast.tks`.

**2. A razão profunda é a FORMA DA TABELA, e é a segunda cara do mesmo padrão de gémeos.**
`checker::type_table_of` chaveia CANONICAMENTE (`name = "teko::checker::TypeTable"`, `namespace = ""`);
o braço qualificado do `resolve_type` compara o `name` da entrada com o ÚLTIMO SEGMENTO do caminho e
o `namespace` com o qualificador. Contra chaves canónicas os dois testes falham para todo o tipo com
namespace. **O resolvedor do checker não consegue consultar a tabela que o backend recebe** — e é por
isso que o backend cresceu uma resolução própria e mais fraca. Duas formas da mesma tabela, uma
protegida e uma esquecida: o padrão de §gémeos outra vez, agora numa estrutura de dados.

**3. Uma guarda tem de ser provada por inversão como qualquer outra coisa.**
A primeira versão desta guarda reportou **zero** sobre os 143 ficheiros — enquanto a MESMA build
continuava a parar no `vt_table`. Não era tolerante: era **cega**, exactamente pelo ponto 2. Uma
guarda que não pode falhar não é prova; é decoração. O `checker::type_table_rekeyed` existe só para o
oráculo poder responder.

**4. As duas metades da mesma divergência falham DIFERENTE, e só uma é ruidosa.**
Mesmo alias qualificado, duas colocações:

| colocação | consumidor | sintoma |
|---|---|---|
| PARÂMETRO | `bind_param` / `append_param_ltypes` | paragem honesta — `` `vt_table` is not a fat-pointer local `` |
| CAMPO | `field_layout_size` / `field_layout_align` | **silêncio**: 8 bytes reservados para uma escrita de 16, e o campo seguinte relido como comprimento |

Medido: um campo aliased a `str` devolvia o `43` do vizinho onde a rota C devolvia `5`. **Quem
perseguisse só a paragem fechava metade do defeito e não saberia.**

### O que fica para quem vier

A guarda é o molde, não o fim. O mesmo instrumento aplica-se a cada par da lista de §gémeos, e o
`cg_niche_is_fat` (`src/codegen/codegen.tks`) é um TERCEIRO decisor da mesma pergunta, sobre o tipo
resolvido, que **não** foi varrido aqui.

### Divergência ADJACENTE, medida e NÃO consertada (degrau próprio)

Um campo de struct por VALOR de tipo nomeado **não copia** na rota nativa — alia a origem. Medido nas
duas rotas com nome NU e com nome QUALIFICADO, logo **não** é da família do alias qualificado:

```
mut t = Trio { a = 1; b = 2; c = 3 }
let h = HB { p = t; n = 44 }
t.a = 99
h.p.a   -->  rota C: 1 (correcto)   rota nativa: 99 (alias, não cópia)
```
