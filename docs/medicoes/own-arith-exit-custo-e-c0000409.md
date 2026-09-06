# `own_arith_exit`: o custo e o `0xC0000409` — dois defeitos, e nenhum é o que o nome dizia

Medição encomendada depois de o CI mostrar, na mesma fixture, 593,5 s no Linux, 1448–3220 s no
Windows, e `exit -1073740791`. **As duas atribuições estavam penduradas no nome errado, e as duas
causas reais estão agora nomeadas.**

## 0. A atribuição está errada, e a razão é estrutural

**`own_arith_exit` não tem build próprio.** É a **primeira fila** de um `.tkr` de PROJECTO, e o cache
é preguiçoso (`src/build/regression.tks:2177` `tkr_ensure_built`, `:2312`
`let fresh = !regr_built_serves(built, RegrBuildKind::Project)`). O `compile_ns` da fila
(`src/build/regr_timing.tks:35-36`) é portanto **o build do projecto inteiro** — 21 ficheiros, 619
itens — e o `run_ns` é **a execução do binário inteiro**, os 192 cenários.

O `f_arith` real é `mut n = 3; n * 10 + 12` (`examples/regressions/own_native/src/corpus.tks:8`). A
anotação *"fold-opaque"* é verdadeira **e irrelevante para ambos os defeitos**.

**Consequência para quem lê relatórios de tempo:** num `.tkr` de projecto, a primeira fila paga a
conta de todas. Dizer *"esta fixture é 89 % do nível"* é ler o instrumento, não o programa.

## 1. DEFEITO A — o selector de instruções é QUADRÁTICO. `isel_arm64.tks:91`

Seis amostras de pilha em seis, todas no mesmo sítio:

```
tk_slice_push_fo / tk_slice_push_r / memcpy / tk_region_alloc
  <- teko.backend.vinfo_set
  <- select_const_int_x86 | select_alu_x86 <- select_inst_x86 <- ... <- build.emit_native_x86
```

**`src/backend/isel_arm64.tks:91` — `vinfo_set`** reconstrói **as três listas paralelas inteiras a
cada escrita de um VReg**. Companheiro: **`:66` `vinfo_grow`**, que também percorre tudo a cada
chamada. Chamadores: **`isel_x86_64.tks:110`** e **`isel_arm64.tks:229`** — a definição é partilhada,
**as duas ISAs pagam**.

Custo por VReg = O(V) de cópia + 3 listas novas ⇒ **O(V²) em tempo E em bytes alocados, por função.**

| N instruções | nativo tempo | nativo pico | rota C tempo | rota C pico |
|---|---|---|---|---|
| 250 | 0,606 s | 88,7 MB | 0,491 s | 13,6 MB |
| 500 | 0,989 s | 304,2 MB | 0,519 s | 20,1 MB |
| 1000 | 2,715 s | 1131,0 MB | 0,661 s | 32,1 MB |
| 2000 | 10,478 s | **4437,0 MB** | 0,942 s | 56,5 MB |
| 4000 | **morto por OOM aos 303,7 s** | **> ~14 GB** | — | — |

**A rota C é linear nos dois eixos; a própria é quadrática nos dois** (≈N^1,97). O eixo *profundidade
de expressão* dá o mesmo. A variável é **o número de VRegs numa função**.

Ligação à fixture: o corpus tem 363 funções pequenas, mas `__teko_native_vmain` tem **192 chamadas
`scenario(...)` num só corpo** — é essa função que paga.

**E é isto que explica a OOM da camada de regressão, o `Killed: 9` do macOS e as ondas de recuperação
de runner.** Não é o `str`, não é a leitura de ficheiro, não é o roteamento de arena: **é o backend
próprio a alocar ao quadrado.**

## 2. DEFEITO B — o `0xC0000409` é um PÂNICO DELIBERADO, não corrupção de pilha

O log do CI dá a vítima em três linhas, e ninguém as tinha lido:

```
argv: …/own_native.exe; captured stderr tail:
teko: deliberate panic: ftoa_nonfinite_text: assertion failed: eq_i64 — expected 0, got 3
teko: stack trace unavailable on this platform (no execinfo)
```

**`0xC0000409` é o código que o `abort()`/`__fastfail` do Windows produz** — o `tk_panic` a sair. O
cenário é **`ftoa_nonfinite_text`**: texto de um `f64` não-finito (NaN/Inf). **Falha só no Windows.**

### As três hipóteses que foram eliminadas POR MEDIÇÃO, e valeu a pena

| hipótese | veredicto | número |
|---|---|---|
| quadro de pilha gigante | **ELIMINADA** | maior quadro Win64 **808 B**; profundidade máxima **952 B** (COFF) / **6,8 KB** (ELF ligado) contra 1 MB = **0,66 %** |
| recursão | **ELIMINADA** | grafo de chamadas **acíclico**: 0 SCC>1, 0 auto-chamadas, 745 funções |
| ABI Win64 no `.o` emitido | **LIMPO** | 280 chamadoras: **0** com shadow space < 32 B, **0** com RSP desalinhado, **19/19** salvam XMM6–XMM15 |

**Nenhuma era a causa.** A causa é uma divergência de renderização de não-finito na CRT da Microsoft —
a família do `%.17g`, onde o MSVC e a glibc discordam.

## 3. As duas rotas — paridade de comportamento, divergência de CUSTO

| | own-native | rota C |
|---|---|---|
| build | 1,21–1,31 s | 1,15 s |
| pico | **303,5 MB** | **47,2 MB** |
| stdout | 192 linhas | 192 linhas, **byte-idênticas** |
| exit | 0 | 0 |

**Não há divergência de comportamento — há 6,4× de memória.** E o pico de um projecto de brinquedo
(303,5 MB) já bate no alvo de ≤300 MB que era para ser o do build inteiro.

## 4. O instrumento é CEGO na metade que interessa

A rota própria imprime `lexer / parser / checker / monomorph / consteval` **e mais nada**. Todo o
`lower → isel → regalloc → encode → objfile → link` do backend próprio **não tem relógio**. Prova
directa: no probe de 2000 instruções as cinco fases reportam `0.0s ✓` **cada uma** e o processo levou
**10,478 s**.

**É por isso que o CI só conseguiu dizer "99 % em `compile`"** — não havia mais nada para dizer. E é
por isso que o defeito quadrático viveu invisível: **a fase onde ele mora não é cronometrada.**

## 5. O que ficou por medir

1. **Os 593,5 s e os 1448–3220 s não foram reproduzidos** — aqui a mesma forma dá 1,3 s. Eliminadas:
   paranoid (1,41 s), rota C (1,15 s), compilador auto-hospedado (impossível — para no `#594`).
   Candidata **não provada**: contenção de memória no runner, agravada pelo defeito A.
2. **Chamadas indirectas não entram no grafo de pilha** — 5 sítios `call *%r` de lambdas no COFF. O
   limite de 6,8 KB é sobre a parte estática.
3. As eliminações do §2 valem para **`baacb08f`**, não necessariamente para o commit que produziu os
   logs do CI.

---

## 6. O `ftoa_nonfinite_text` — caracterizado, e o defeito é da FIXTURE

Investigado depois, com o CI a dar `expected 0, got 3` em três amostras independentes.

**A verificação 3 é a do NaN** (`examples/regressions/own_native/src/corpus.tks:3916-3917`):

```teko
let nan_text = teko::ftoa(d27_not_a_number())
if !teko::str::ends_with(nan_text, "nan") { return 3 }
```

**E o doc-comment do próprio cenário previu isto — para as libcs erradas** (`:3905-3907`):

> *"a NaN's sign bit and payload are the host libc's business, and glibc prints `-nan` for `inf - inf`
> where another conforming libc may print `nan` — **this corpus runs on glibc AND on musl**, so
> asserting the exact spelling would pin a platform, not a behavior."*

**Ele enumerou duas libcs. A CRT da Microsoft é a terceira.** O `tk_ftoa` (`teko_rt.c:405-407`)
delega ao `snprintf(tmp, sizeof tmp, "%.17g", x)` — logo **a grafia do não-finito é inteiramente do
libc do hospedeiro**, e o MSVC imprime `-nan(ind)` para o NaN indefinido, que **não termina em
`"nan"`**.

### Porque isto NÃO é defeito do compilador

O `%.17g` é o que o C exige; as três grafias são conformes. O cenário escolheu afirmar pelo
**sufixo** precisamente para não pinar plataforma — e o sufixo escolhido pina duas de três.

### O que NÃO medi, e é o que impede o conserto de ser óbvio

**Não corri em Windows.** A cadeia é: falha na verificação 3 (a única cujo texto é do libc) + o
`tk_ftoa` delega ao `%.17g` + o MSVC documenta `-nan(ind)`. **É inferência forte, não medição.**
Quem pegar isto deve **imprimir o texto real** antes de alargar a asserção — porque alargar uma
asserção com base numa suposição é trocar um vermelho por um verde que não prova nada.

**E há uma segunda leitura possível que não posso excluir daqui:** se o valor que chega ao `ftoa`
não for o NaN esperado (por defeito de codegen do não-finito no Windows), o texto seria outro por
outra razão, e aí **seria** defeito do compilador. **A impressão do texto real separa as duas
hipóteses numa corrida.**
