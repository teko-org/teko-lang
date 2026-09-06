# `tdb` — a proposta do depurador de Teko (0.3.1)

> **Esta é a peça de leitura do `tdb`.** Substitui, como documento a ler,
> `docs/design/debugger-orcamento-0.3.1.md` (1.ª passagem — orçamento e quatro experimentos medidos) e
> `docs/design/debugger-superficie-e-contramedida-0.3.1.md` (2.ª passagem — PoC executável, superfície,
> `kind = "tool"`, e a refutação medida da red-flag 3). **Nenhum dos dois foi reescrito ou editado:**
> ficam onde estão, e esta proposta refere-os pelo detalhe medido, byte a byte, que não vale repetir.
>
> **É uma PROPOSTA.** Propõe o que se constrói, em que ordem, com que superfície, e com que número.
> Obedece à lei de forma do dono (`docs/memory/teko-laws-digest.md:1156`):
>
> > **Um risco NÃO MEDIDO é um PASSO do plano, não um alarme.**
>
> Portanto: onde não se sabe se algo funciona, isso aparece aqui como **um crumb de medição nomeado que
> abre a fase**, e não como aviso. Alarmes entram **provados**, com a medição à vista. Números entram
> como **orçamento dentro do plano**, nunca como argumento contra.
>
> **Nada disto se implementa nesta versão nem na seguinte** (§7 — o portão).

## As decisões que esta proposta obedece, e não reabre

| decisão do dono | consequência aqui |
| --- | --- |
| *"precisaremos de um debugger próprio, como em Go"* | o `tdb` **é o alvo**. Não há alternativa apresentada, nem ponderada |
| *"não me importo (por ter ele) com os demais debuggers de mercado"* | **interop não é objectivo.** gdb, lldb, `cppdbg` e CodeLLDB estão fora do caminho crítico; nenhum crumb desta proposta os serve |
| *"quero que considere tudo que possa auxiliar o dev e dar a melhor experiência"* | o alvo é **a melhor experiência**, não o mínimo viável. Variáveis, tipos, formatação legível e frames fiáveis entram **todos** |
| *"SEM C LANG, somente Teko nativo"* | nenhuma linha de C nossa, nenhuma compilação pela rota C. **§5** mostra como se alcança o SO sob esta ordem |
| *"gastar energia marcando `#line` em C é desnecessário"* | **a Camada 0 não existe.** Não está orçada, não é opcional |
| *"primeiro precisamos do teko 100% nativo (emissão e linhagem)"* | **o PORTÃO, §7**, com as duas metades e o número de hoje |
| `/tdb` na raiz; `kind = "tool"` emite um `.tkl`, compila na máquina do dev, não entra em `[deps]`; um `tool` em `[deps]` é **ignorado**, não recusado | **§4.6** — a forma do projeto |

---

# 1. A tese

> **O `tdb` transforma a informação que o compilador JÁ CALCULA e HOJE DESCARTA numa sessão de
> depuração que fala Teko: pára numa linha de `.tks`, nomeia as funções pelo nome qualificado Teko,
> desenrola a pilha pelo descritor de frame que o nosso próprio backend produziu, e mostra os valores
> com a forma dos nossos tipos.**

O que o `tdb` dá ao dev, e que **hoje não existe de forma nenhuma**:

| hoje | com o `tdb` |
| --- | --- |
| um programa Teko que falha dá **um código de saída** e, no melhor caso, um stack trace **por função** (o `.tsym` v1 resolve `<símbolo>` → `<nome-teko>`, sem linha) | pára-se **na linha**, vê-se a linha do `.tks`, e o `bt` diz `teko::demo::add at src/main.tks:41` |
| para saber quanto vale `x`, escreve-se `println` e recompila-se — **o ciclo é uma recompilação por pergunta** | `print x` responde sem recompilar; `locals` responde a todas de uma vez |
| a pilha, quando existe, é a do runtime e tem os nomes **manglados** do símbolo | os nomes qualificados Teko, que o `.tsym` já carrega nos dois lados (medido, §4.2) |
| não há forma de observar um `defer` a correr, nem qual membro de um `T \| error` está activo | o `step` **entra** nas linhas do `defer` (é código que corre), e o formatador lê a tag e escolhe o membro |
| o editor não sabe pôr um breakpoint num `.tks` | o `tdb` registra a linguagem, e o breakpoint no gutter funciona sem o dev ligar uma opção global do editor |

**E a razão de ser NOSSO, e não um adaptador para o de outra pessoa,** é a que o Go escreveu primeiro e
que §8 verifica na fonte: um depurador de terceiros modela o *seu* modelo de execução, não o nosso.
As três coisas em que o nosso já difere — o nome qualificado, o descritor de frame que **nós** temos
exacto em vez de inferido, e a forma dos nossos rails de união — são exactamente as três em que um
depurador nosso responde melhor **por construção**, não por afinação.

---

# 2. A experiência, do lado do dev

**Isto vem primeiro de propósito.** É uma proposta de ferramenta, não de formato: o formato (§4) existe
para servir o que está nesta secção, e não o contrário.

## 2.1 O que ele escreve

Duas linhas. A primeira constrói com linhagem, a segunda abre a sessão.

```sh
teko build . --debug=vars -o bin
tdb run .
```

Ou, sobre um binário que já existe:

```sh
tdb exec ./bin/hello
```

Ou anexando-se a um processo vivo:

```sh
tdb attach 41207
```

**A flag de construção é nivelada, com valores nomeados**, na forma que o `--opt=<n>` já pratica na
nossa CLI:

| forma | efeito |
| --- | --- |
| `--debug=none` | **o default, em todos os perfis, `--release` incluído.** Informação de depuração nunca é imposta |
| `--debug=lines` | a tabela endereço→linha + os descritores de frame. Responde *"onde estou e como cheguei aqui"* |
| `--debug=vars` | tudo o de `lines`, mais os locais com nome, slot e tipo. Responde *"quanto vale `x`"* |
| `-g` | **alias legal de `--debug=vars`**, porque é isso que `-g` significa em gcc, clang e rustc — e quando `--debug=vars` existir, prometê-lo passa a ser verdade |
| valor desconhecido | **erro duro**, com a lista dos aceites. Nunca degradação silenciosa: um nível de depuração adivinhado custa uma sessão inteira a perseguir um breakpoint que nunca resolveu |

`--release --debug=vars` é **legal e não é recusado**: é como se depura um crash de release, e recusá-lo
obrigaria a reconstruir noutro perfil — o que muda o código e apaga o defeito.

## 2.2 O que ele vê

```
$ tdb run .
tdb 0.3.1 — teko debugger
built bin/hello (--debug=vars)
loaded bin/hello.tsym (v2): 412 functions, 5108 line rows, 412 frame descriptors, 1877 locals
(tdb) break src/main.tks:41
Breakpoint 1 at 0x1133: src/main.tks:41 (in teko::demo::add)
(tdb) run
Breakpoint 1, teko::demo::add at src/main.tks:41
   39 fn add(a: i32, b: i32): i32 {
   40     let doubled = a * 2
-> 41     let s = doubled + b
   42     s
   43 }
(tdb) print s
s: i32 = <not yet assigned>
(tdb) next
Breakpoint 1, teko::demo::add at src/main.tks:42
(tdb) print s
s: i32 = 47
(tdb) locals
a:       i32 = 12
b:       i32 = 23
doubled: i32 = 24
s:       i32 = 47
(tdb) bt
#0  teko::demo::add   at src/main.tks:42
#1  teko::demo::main  at src/main.tks:52
(tdb) frame 1
#1  teko::demo::main at src/main.tks:52
(tdb) print label
label: str = "total" (5 chars, 5 bytes)
(tdb) print res
res: i64 | error = i64(47)
(tdb) continue
process exited with status 0
(tdb) quit
```

**Cinco coisas nesta transcrição são a proposta, e nenhuma delas é o que um depurador genérico daria:**

1. **`teko::demo::add`, não `tk_demo_add`.** O nome qualificado Teko, que o `.tsym` já carrega ao lado
   do símbolo manglado — medido hoje em §4.2. Um depurador de terceiros mostra o segundo.
2. **`-> 41` com contexto de duas linhas acima e abaixo**, listado do `.tks` real. A listagem é o
   depurador a ler o ficheiro, e não depende de nada dentro do processo.
3. **`s: i32 = <not yet assigned>`** antes da atribuição, e o valor depois. **Um local que ainda não foi
   escrito não mostra lixo com confiança** — é a única resposta honesta, e §4.5 é o mecanismo que a
   torna possível.
4. **`label: str = "total" (5 chars, 5 bytes)`** — os **dois** contadores, porque `str` em Teko conta
   caracteres em `.len` e leva os dois. Formatar um `str` **é** o layout de `str`, e é por isso que a
   fase que o formata (§3.5) abre com a medição do layout, e não com um palpite.
5. **`res: i64 | error = i64(47)`** — o **membro activo** da união, escolhido pelo rail (`niche`,
   `InlineTag`, ou box-em-arena). O `tdb` lê o rail do formato; não o adivinha.

## 2.3 Os comandos

Grafia do gdb, por memória muscular de quem depura — a mesma razão pela qual o delve a escolheu.
**Aliases de CLI não são sobrecarga de função:** são um nome com abreviatura, e é assim que todo
depurador se escreve.

```
break <ficheiro.tks>:<linha>   b     põe um breakpoint
break <ns::fn>                 b     põe na primeira linha executável da função
tbreak <...>                         breakpoint de uma vez só
delete <n>                     d     remove
disable <n> / enable <n>             desliga sem perder
info breakpoints                     estado, com contagem de disparos

run [-- args…]                 r     arranca
continue                       c     continua
next                           n     próxima linha, sem entrar
step                           s     próxima linha, entrando
finish                               corre até ao retorno da moldura corrente
until <linha>                        corre até uma linha desta função

bt                                   a pilha, com nomes Teko
frame <n>                      f     selecciona a moldura
up / down                            navega a pilha
list [<linha>]                 l     lista o .tks em volta

print <nome>                   p     o valor de um local ou parâmetro
locals                               todos os locais da moldura selecionada
args                                 só os parâmetros
whatis <nome>                        o tipo Teko, sem avaliar

quit                           q
```

**O que o `tdb` NÃO faz, dito aqui e não depois de prometido:** avaliar expressões (só nomes simples e
acesso a membro por `.`); watchpoints; alterar valores; ler core dumps; e entrar dentro de código de
biblioteca da plataforma — uma chamada a `write` aparece como uma moldura de fronteira nomeada, não
como um passeio pela libc.

## 2.4 No editor

O `tdb` fala DAP **como modo do próprio binário** (`tdb dap`), na forma verificada do delve — não há um
segundo executável adaptador para manter (§8.1).

`.vscode/launch.json`, e é tudo:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar",
      "type": "tdb",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "cwd": "${workspaceFolder}",
      "stopOnEntry": false,
      "preLaunchTask": "teko: build --debug=vars"
    }
  ]
}
```

**Não há `MIMode`, `miDebuggerPath`, `sourceFileMap`, `sourceMap`, nem
`debug.allowBreakpointsEverywhere`.** Os quatro primeiros não existem porque o adaptador é nosso e
resolve os caminhos com o nosso formato. O quinto não é preciso porque a extensão que registra o tipo
`tdb` **também registra a linguagem `teko`** — e é essa a diferença concreta e mensurável de ter
depurador próprio: as vias de terceiros exigem que o utilizador ligue uma opção global do editor para
poder clicar no gutter de um `.tks`; a nossa não.

E a extensão que registra o tipo é **um `package.json` com `contributes.debuggers`, zero JavaScript** —
o que também é virtude de lei: o roadmap de tooling já ratificou que nenhuma invocação de processo a
partir de um editor usa concatenação de string para shell, por causa de um achado real de injeção. Um
adaptador registado por manifesto **não tem código onde essa falha caiba**.

## 2.5 A frase que a documentação leva

> `--debug=lines` responde **"onde estou e como cheguei aqui"**. `--debug=vars` responde
> **"quanto vale `x`"**. O default é `none`, e um binário construído sem a flag **diz** que não tem
> linhagem em vez de parar em silêncio no sítio errado.

Recusa nomeada, nunca omissão — a regra que o expurgo já fixou para o MinGW.

---

# 3. O plano — oito fases, cada uma utilizável por si

**Unidade: crumb** — passo pequeno, prova própria, reversível.

**A regra de ordenação, e é o que faz esta secção ser um plano e não uma lista de desejos:** cada fase
entrega **algo que se usa sem a fase seguinte existir**. Uma fase que só valha quando a próxima aterrar
não é uma fase, é meio crumb grande.

**E onde há incerteza, a fase ABRE com um crumb de medição nomeado**, marcado **`M`**. Um `M` não
produz produto: produz **um número que decide os crumbs a jusante**, e o seu custo é sempre uma fracção
do que decide. A prova histórica de que isto vale está na própria lane: a red-flag 3 foi levantada por
raciocínio, custou a rejeição de um arco inteiro, e quando foi medida **era falsa**. Custo por medir:
nenhum. Custo por não medir: uma decisão errada.

## 3.0 Fase 0 — o arnês e o oráculo · **3 crumbs** · entrega: um activo de teste que não expira

**O que se usa no fim da fase:** uma suíte que afirma factos sobre tabelas de depuração **sem precisar
de um `tdb`, sem precisar de um compilador Teko correcto, e sem precisar de um depurador de terceiros**.
É a fase que se pode fazer antes do portão sem desperdício (§7.2), e a razão está em §6.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **F0.1** | os dois objetos escritos à mão do PoC da 2.ª passagem entram como fixtures do arnês: `docs/design/debugger-poc/mini.s` (tabelas de correcção conhecida, byte a byte) e `adv.s` (cinco molduras através de **todas** as formas de frame do nosso encoder) | `reproduce.sh` já corre e sai 0. O crumb é a promoção de "documentação que executa" a **fixture citada por um teste** |
| **F0.2** | o arnês de asserção: dado um objeto e uma tabela esperada, afirmar **profundidade de pilha através de uma função frameless**, e não `>= N`. Uma afirmação de `>=` passa com uma cadeia truncada | o ramo negativo é metade do crumb: uma tabela deliberadamente errada tem de **falhar** |
| **F0.3** | um terceiro fixture, novo: uma pilha de **seis** molduras que atravessa uma fronteira de biblioteca da plataforma, para fixar o que o `bt` faz nessa fronteira (moldura nomeada, não passeio) | o teste que afirma que o desenrolar **pára** na fronteira em vez de inventar molduras |

**Porque três e não um:** F0.1 é promoção de activo existente, F0.2 é a máquina de asserção, F0.3 é o
único caso de fronteira que os dois fixtures actuais não cobrem e que o `bt` do `tdb` vai encontrar em
todo programa real que escreva no stdout.

## 3.1 Fase 1 — a linhagem no compilador (`.tsym` v2) · **11 crumbs** · entrega: stack traces de produção com LINHA EXACTA

**O que se usa no fim da fase, e nada disto precisa do `tdb`:** o stack trace nativo que hoje resolve
**por função** passa a resolver **por linha**. Mesma informação, melhor granularidade, mesmo ficheiro,
e todo programa Teko em produção beneficia. **É a fase de maior valor por crumb da proposta**, e é a
única que rende antes de existir uma linha de depurador.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M1.0 — MEDIR PRIMEIRO** | **quanto custa o `.tsym` v2, de verdade?** A estimativa de mesa é ~1,2 MB no corpus do compilador (≈50 k linhas × ≈24 bytes) e "tempo de build desprezável". Nenhum dos dois foi medido. Este crumb gera o ficheiro contra o corpus real e mede **bytes e segundos**. Decide, a jusante, se o `L` fica em texto ou passa a binário compacto | o número, num relatório. Se o texto passar de um limiar que o próprio crumb fixa, o formato binário entra como crumb; se não passar, poupam-se os crumbs de codificador e descodificador |
| **C1.1** | `LFunc` ganha `file` e `decl_line`. **Medido hoje: não os tem** — `LFunc` é `{ symbol, n_params, param_types, ret_type, blocks, next_vreg }`, e o `file`/`line` existem só a montante, em `checker::TFunction` | `lower_test.tkt`: o `file`/`decl_line` de uma função lowered casa com o do `TFunction` de origem |
| **C1.2** | `MLineMark` — a pseudo-instrução de **zero bytes** que carrega `(linha, coluna)` pela fila de instruções de máquina, no lado **x86-64** | golden do encoder: o objeto com marcas é **byte-idêntico** ao sem marcas. Zero bytes quer dizer zero bytes |
| **C1.3** | o mesmo, no lado **arm64** | idem, na perna aarch64 |
| **C1.4** | **o produtor de `.tsym` MUDA DE CASA.** Medido hoje, e é o achado desta passagem: `tk_emit_tsym` vive em `src/codegen/codegen.tks:12187` — **o emissor de C** — e é chamado pela rota **nativa** (`src/build/project.tks:1845` e `:2642`, em `finish_native_object`). A fatia 6 do expurgo manda *"REMOVER DO FONTE … a emissão de C"*. Sem este crumb, o dia do expurgo leva o produtor de linhagem com ele | o `.tsym` v1 emitido depois da mudança é **byte-idêntico** ao de hoje, e nenhuma chamada atravessa `codegen::` |
| **C1.5** | a linha **`L`** — endereço→linha, uma por `MLineMark` sobrevivente ao alocador | golden: uma função com um derramamento produz linhas `L` **na ordem dos endereços finais**, não na ordem de lowering |
| **C1.6** | a linha **`F`** — o descritor de frame, serializando o que **já é calculado**: `compute_frame_layout_x86` → `FrameLayoutX86` (`src/backend/encode_x86_64.tks:1501`) e `compute_frame_layout` → `FrameLayout` (`src/backend/encode_arm64.tks:1066`) | golden das duas arquiteturas, e o teste que afirma que a linha `F` de uma função **frameless** diz frameless |
| **C1.7** | a linha **`V`** — locais: o JOIN por `vreg_id` sobre `assign_lookup`, e a **fixação a slot** sob o perfil `vars` | `lower_test.tkt`: sob o perfil, duas `let` + um shadow dão **três** entradas na ordem de ligação; **sem** o perfil, o LIR é **byte-idêntico** ao de hoje. A segunda metade é o que torna o crumb seguro |
| **C1.8** | o **registo de tipos de layout congelado**, explícito e enumerado — nunca "tudo o que não sabemos que muda" | um tipo **fora** do registo produz **nenhuma** descrição; um dentro produz a esperada. O ramo negativo é metade do crumb |
| **C1.9** | a superfície: `--debug=none\|lines\|vars`, `-g` como alias de `vars`, e **a linha em `project_arg_of`** | `help_test.tkt` + o teste que prova que `--debug=lines` **não** é lido como o caminho do projeto (§4.7) |
| **C1.10** | **a especificação escrita do `.tsym` v2**, em `docs/` | é entregável de documentação, e é o contrato: emissor e leitor viverão em repos diferentes (§4.6), e um formato definido pela implementação não sobrevive à separação |

**Porque `M1.0` abre a fase:** o formato ser texto é uma escolha que se paga em bytes e se cobra em
simplicidade de leitor (um separador feito à mão, no molde do parser TOML de `manifest.tks` — medido: não
há `teko::str::split` no corpus). A escolha é **boa se o número for pequeno**, e o número não foi medido.
Medir custa um crumb; escolher errado custa o leitor inteiro reescrito na fase 4.

## 3.2 Fase 2 — o chão de controlo · **7 crumbs** · entrega: `tdb exec` corre um programa sob controlo

**O que se usa no fim da fase:** `tdb exec ./bin/hello` arranca o programa **parado**, deixa-o correr,
recolhe o estado de saída **decodificado** e reporta-o. Não pára em linhas ainda — mas o chão está
provado, e um programa que sai com sinal deixa de ser um número opaco.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M2.0 — MEDIR PRIMEIRO, e é o crumb mais importante desta proposta** | **um `extern` de libc implícito, com parâmetros de largura de ponteiro, liga e chama pela rota nativa?** Medido hoje: o checker tem `ptr` e `uptr` (`src/checker/scope.tks:387-388`, *"opaque FFI pointer (transport-only)"*), e o `TExternDecl` tem `from_lib: str` com o comentário **`"" = implicit libc`** (`src/checker/tast.tks:172`). Medido também: **os 24 `pub extern fn` da árvore levam TODOS `from "teko_rt"`** — nenhum exercita o caminho de libc implícito. O mecanismo está declarado; o caminho está **por andar**. Este crumb declara `extern fn getpid(): i32 = "getpid"`, constrói pela rota nativa, e corre | o binário nativo imprime o pid correcto. Se falhar, o crumb reporta **onde** falha (checker, símbolo indefinido, ou linha de linker), e o conserto é um crumb nomeado em vez de uma descoberta a meio da fase 3 |
| **T2.1** | `ptrace` declarado como `extern` de libc implícito, com os pedidos que interessam como constantes nomeadas | um teste que anexa e desanexa de um processo filho trivial |
| **T2.2** | `fork` + `execvp` + `PTRACE_TRACEME`: nasce um filho **parado no `execvp`** | o filho existe, está parado, e o pai sabe o pid |
| **T2.3** | `waitpid` + a **decodificação do estado**, com a regra 128+N que `tk_rt_run` já pratica na árvore | a matriz: saída normal, saída por sinal, e paragem — três resultados distinguíveis, nenhum colapsado |
| **T2.4** | os registos: `PTRACE_GETREGS`/`SETREGS` para uma vista de bytes, e **acesso nomeado** ao contador de programa, ao topo de pilha e à base de frame, por arquitetura | ler o contador de programa de um filho parado devolve um endereço **dentro** do mapa do executável |
| **T2.5** | a memória: ler e escrever uma palavra no espaço do filho | escreve-se e lê-se de volta, e o byte original recupera-se |
| **T2.6** | o esqueleto: `tdb exec`, `tdb version`, e o laço de despacho de comandos | `tdb exec` sobre um programa que sai 5 reporta 5; sobre um que aborta, reporta o sinal pelo nome |

**Nota de forma:** as declarações desta fase são o **único** sítio da proposta onde a superfície toca o
SO, e §5 mostra que a forma obedece a *"SEM C LANG"* sem nenhuma linha de C nossa.

## 3.3 Fase 3 — breakpoints · **3 crumbs** · entrega: parar num endereço e continuar, fiavelmente

**O que se usa no fim da fase:** põe-se um breakpoint por **endereço**, o programa pára lá, continua, e
**pára lá outra vez na segunda passagem**. Esse "outra vez" é a fase inteira: o defeito clássico de um
depurador novo é o breakpoint que dispara uma vez e desaparece.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **T3.1** | x86-64: guardar o byte original, escrever `0xCC`, e **no sinal recuar o contador de programa um byte** antes de restaurar | um breakpoint num laço de 3 iterações dispara **3 vezes**, e o programa termina correctamente. O ciclo restaurar → single-step → **re-armar** é o crumb |
| **T3.2** | arm64: `BRK #0` é uma palavra de 4 bytes e **o contador de programa NÃO recua**. A assimetria é real e é a razão de ser um crumb próprio | o mesmo teste de 3 iterações, na perna aarch64 |
| **T3.3** | a tabela endereço→byte-original, com `delete`/`disable`/`enable` e contagem de disparos | remover um breakpoint restaura o byte; `info breakpoints` conta certo; e **sair do `tdb` deixa o processo íntegro** |

## 3.4 Fase 4 — a posição · **4 crumbs** · entrega: **o depurador de terminal completo**

**O que se usa no fim da fase, e é o marco grande:** `break src/main.tks:41`, `run`, `bt` com nomes
Teko, `next`, `step`, `finish`, `list`, `frame`. **Toda a transcrição de §2.2 excepto as três linhas de
`print`/`locals`.**

| crumb | conteúdo | prova |
| --- | --- | --- |
| **T4.1** | o leitor de `.tsym` v2, escrito **contra a especificação de C1.10**, não contra o emissor. Separador de linhas e campos à mão, no molde do parser TOML de `manifest.tks` | os fixtures de F0.1: um `.tsym` de correcção conhecida lê-se para as tabelas esperadas; um `.tsym` v1 lê-se **degradando** (sem linhas, sem frames, sem locais) em vez de falhar; um cabeçalho desconhecido **para honestamente** |
| **T4.2** | as duas resoluções: `ficheiro:linha → endereço` (para o `break`) e `endereço → ficheiro:linha` (para o `bt`). A segunda é a que precisa de cuidado: um endereço **entre** duas linhas resolve para a **anterior**, nunca para a seguinte | a matriz de fronteiras: primeiro endereço de uma função, último, e um endereço no meio de uma linha |
| **T4.3** | o desenrolar **pela linha `F`** — o descritor que o compilador emitiu, não análise de prólogo. É a clamação mais forte do arco, porque é a única que não depende da inferência de ninguém | os três fixtures de F0.*, incluindo a pilha de seis molduras que atravessa a fronteira de biblioteca |
| **T4.4** | os comandos de navegação: `break`/`tbreak` por linha e por `ns::fn`, `bt`, `frame`/`up`/`down`, `next`, `step`, `finish`, `until`, `list` | `next` sobre uma linha com `defer` **executa** as linhas do `defer` e volta — o comportamento certo, e §8.2 explica porquê |

## 3.5 Fase 5 — variáveis e legibilidade · **5 crumbs** · entrega: `print`, `locals`, `args`, `whatis`

**O que se usa no fim da fase:** as três linhas que faltavam em §2.2, e a proposta está cumprida do lado
do terminal.

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M5.0 — MEDIR PRIMEIRO** | **o layout de `str` está congelado?** Medido hoje: `src/runtime/teko_rt.h:44-48` define `tk_str` com **duas** palavras e o comentário `length in BYTES`; a decisão registada do dono é que `.len` conta **CARACTERES** e que `str` leva **os dois** contadores — uma terceira palavra. **A decisão existe, o layout não mudou.** Este crumb lê o layout no dia em que a fase abrir e diz qual é | o registo de tipos congelados de C1.8 é actualizado pelo número medido, não pela intenção. Se `str` ainda for duas palavras, `str` fica **fora** do registo e os locais de tipo `str` não recebem descrição — o que é honesto — e o resto avança |
| **T5.1** | `print <nome>` e `locals`/`args`: ler o slot pelo offset da linha `V`, e formatar pelo tipo | um escalar de cada largura, um `bool`, um `char`, e um local **antes** da atribuição a dar `<not yet assigned>` em vez de lixo |
| **T5.2** | o formatador de agregados: `struct` por campo, `[]T` por elementos com comprimento, e acesso a membro por `.` | um struct aninhado imprime aninhado; um slice vazio imprime vazio e não falha |
| **T5.3** | o formatador de **uniões**, lendo o **rail** do formato: `niche` (uma palavra, o padrão do ponteiro discrimina), `InlineTag` (palavra de tag + payload), e box-em-arena | os três rails, um teste cada. E o teste que importa: um `T \| null` no rail `niche` com o ponteiro nulo imprime `null`, e com ponteiro válido imprime o membro |
| **T5.4** | `whatis` e o formatador de `str` com **os dois contadores**, condicionado ao veredicto de M5.0 | `"total" (5 chars, 5 bytes)`, e um `str` com um caractere multi-byte a mostrar contadores **diferentes** — que é o teste que prova que os dois contadores são lidos e não calculados |

**Porque o rail vai no formato e não no formatador:** a regra de `niche` é uma **optimização**, logo vai
mudar. Um formatador que codifique *"ponteiro nulo significa `null`"* replica uma decisão interna do
gerador de código; no dia em que a regra mudar, ele mente em silêncio. **Um campo por tipo no `.tsym` v2
custa um campo e poupa uma mentira silenciosa** — e o formatador **lê** o rail em vez de o adivinhar.

## 3.6 Fase 6 — o editor · **5 crumbs** · entrega: depurar Teko no VSCode

| crumb | conteúdo | prova |
| --- | --- | --- |
| **T6.1** | o enquadramento `Content-Length: <n>\r\n\r\n<json>` e o laço de pedidos. **A camada JSON é grátis** — `src/encoding/json/json.tks` já expõe codificação e descodificação | golden de mensagens: um pedido partido em dois pacotes lê-se inteiro; um `Content-Length` que mente **para honestamente** |
| **T6.2** | o ciclo de arranque: `initialize`, `launch`, `attach`, `setBreakpoints`, `configurationDone`, e os eventos `initialized`/`stopped`/`continued`/`exited`/`terminated` | uma sessão simulada, dirigida por um ficheiro de pedidos, produz a sequência de eventos esperada — **sem VSCode** |
| **T6.3** | os pedidos de navegação: `threads`, `stackTrace`, `scopes`, `continue`, `next`, `stepIn`, `stepOut`, `pause` | idem, por golden |
| **T6.4** | os pedidos de dados: `variables`, `evaluate` (nomes simples), `source` | idem |
| **T6.5** | a extensão que registra o tipo `tdb` **e a linguagem `teko`** — um `package.json`, zero JavaScript | o breakpoint no gutter de um `.tks` aceita o clique **sem** `debug.allowBreakpointsEverywhere` |

## 3.7 Fase 7 — os portes · **8 crumbs** · entrega: `tdb` em macOS e em Windows

**Uma plataforma de cada vez, e a ordem é a do uso.** O aviso de escopo é medido e vem do precedente:
o delve, com uma década de manutenção e patrocínio corporativo, cobre **cinco** pares de plataforma —
e não cobre `darwin/arm64`. **Isto não é argumento contra o `tdb`; é o que ordena esta fase por último
e a divide em duas metades independentes.**

| crumb | conteúdo | prova |
| --- | --- | --- |
| **M7.0 — MEDIR PRIMEIRO (macOS)** | **o entitlement.** Depurar em macOS exige que o binário do depurador seja assinado com `com.apple.security.cs.debugger`. Este crumb mede o que isso custa **no nosso pipeline de release**, não em crumbs de código | o `tdb` assinado anexa-se a um processo próprio numa máquina Darwin. É custo de engenharia de release, e é o menos agradável do lote — por isso é medido antes de qualquer código de porte |
| **P7.1 · P7.2 · P7.3** | macOS: `task_for_pid` + `mach_vm_read_overwrite`/`mach_vm_write`, os registos por `thread_get_state`, e as excepções de depuração | a matriz das fases 2 e 3, repetida na perna Darwin |
| **M7.4 — MEDIR PRIMEIRO (Windows)** | o modelo de eventos de Windows é **empurrado** (`WaitForDebugEvent`), não **puxado** (`waitpid`). Este crumb mede se o laço de sessão da fase 2 absorve a inversão ou se precisa de uma camada de adaptação | o número de sítios do laço que mudam. Decide se P7.5 é um crumb ou três |
| **P7.5 · P7.6 · P7.7** | Windows: `DebugActiveProcess` + `WaitForDebugEvent`, `ReadProcessMemory`/`WriteProcessMemory`, `GetThreadContext`/`SetThreadContext` | idem, na perna Windows |

**Windows não precisa de contentor de depuração nenhum.** O `tdb` lê o `.tsym` v2, um ficheiro ao lado
do `.exe`. PE, COFF, CodeView e PDB **não entram nesta proposta** — nem como item, nem como custo.

## 3.8 O total

| fase | entrega utilizável por si | crumbs |
| --- | --- | --- |
| **0 — arnês e oráculo** | um activo de teste que não expira, sem depender de `tdb` nem de compilador correcto | **3** |
| **1 — linhagem no compilador** | **stack traces de produção com linha exacta**, sem depurador nenhum | **11** |
| **2 — chão de controlo** | `tdb exec` corre sob controlo e decodifica a saída | **7** |
| **3 — breakpoints** | parar num endereço e voltar a parar | **3** |
| **4 — posição** | **o depurador de terminal completo** (tudo excepto valores) | **4** |
| **5 — variáveis e legibilidade** | `print`, `locals`, `args`, `whatis`, com os nossos tipos | **5** |
| **6 — editor** | depurar Teko no VSCode, sem opção global e sem JavaScript | **5** |
| **7 — portes** | macOS e Windows | **8** |

> **Linux completo (fases 0–6): 38 crumbs. Três plataformas: 46.**
> **Dos 38, seis são de medição** (`M1.0`, `M2.0`, `M5.0` e os três dos portes contam 2 fora dos 38) —
> e são o que impede os outros de serem escritos contra um palpite.

**Duas leituras honestas do número, e as duas pertencem ao plano:**

**(a) Onde o número SUBIU em relação à 2.ª passagem, e porquê.** Ela orçava 18 crumbs para o `tdb` em
Linux e 8 para o piso do compilador — 26. Este plano diz 38. A diferença **não** é pessimismo novo: é
o alvo, que mudou por ordem do dono. *"Quero que considere tudo que possa auxiliar o dev e dar a melhor
experiência"* põe variáveis, tipos, formatação de uniões e `str` legível **dentro** do arco em vez de
os deixar como fase opcional; e acrescenta os crumbs que a 2.ª passagem não tinha (a mudança de casa do
produtor de `.tsym`, a especificação escrita, e os seis de medição).

**(b) Onde o número DESCEU, e é maior do que subiu.** Saem, por *"não me importo com os demais
debuggers de mercado"*: as três seções DWARF, os tipos DWARF, a CFI DWARF, o CodeView, o DWARF-em-PE, e
os pretty-printers de gdb e de lldb — **17 crumbs que a 2.ª passagem orçava e que esta proposta não
tem.** O interop sair do caminho crítico é a maior economia do plano, e é uma decisão do dono, não uma
optimização minha.

**E o número que interessa mais do que o total:** **a fase 1, com 11 crumbs, rende sozinha** — stack
traces por linha em produção, para todo programa Teko, sem uma linha de depurador escrita.

---

# 4. O que o compilador tem de passar a produzir

## 4.1 O `.tsym` é a semente, e a legislação já o disse

`TEKO_LEGISLATION.md:350`, literal e verificado nesta árvore hoje:

> **`.tsym`** — *Teko Symbols* (debug symbols: file:line + names **for the debugger** + stack traces
> — Eixo E).

**A legislação já designou o `.tsym` como o artefacto de informação de depuração "for the debugger".**
Estender o `.tsym` é **obedecer**; inventar um formato novo seria abrir um segundo para um propósito já
legislado. Não há tensão a resolver: a lei decidiu antes de a pergunta existir.

E o formato foi desenhado para evoluir. O cabeçalho que o emissor escreve hoje
(`src/codegen/codegen.tks:12189`) **leva a versão**:

```
# teko symbol map (.tsym v1): <c-symbol>\t<teko-name>\t<file>:<line>
```

Logo: um leitor v2 aceita um v1 **degradando** (sem linhas, sem frames, sem locais), e um leitor v1 vê
um cabeçalho que não reconhece e **para honestamente**. Nada a inventar, e a compatibilidade é aditiva.

## 4.2 As quatro tabelas, e o estado medido HOJE

Medido nesta árvore, no SHA `e317b44`. **Metade do trabalho já está feita, e é isso que separa
"construir um depurador" de "construir um leitor sobre tabelas que já temos".**

| tabela | o `tdb` precisa? | estado medido hoje |
| --- | --- | --- |
| **função → nome Teko, ficheiro, linha de declaração** | sim | **JÁ EXISTE e JÁ É EMITIDO.** `tk_emit_tsym` escreve `<símbolo-manglado>\t<nome-teko>\t<ficheiro>:<linha>` por função, e `checker::TFunction` carrega `file` e `line` (`src/checker/tast.tks:166-167`, com o comentário *"for the .tsym symbol map"*) |
| **descritor de frame** (framed?, tamanho, base de frame, offset do retorno) | **sim, e é o que dá o `bt` sem inferência** | **JÁ É CALCULADO, e é exacto.** `compute_frame_layout_x86` → `FrameLayoutX86` (`src/backend/encode_x86_64.tks:1501`) e `compute_frame_layout` → `FrameLayout` (`src/backend/encode_arm64.tks:1066`). **Falta serializá-lo** — uma linha de texto por função |
| **locais** (nome → slot → tipo) | sim, para `print x` | **JÁ EXISTE em `LEnv` e é DESCARTADO.** `LEnv` é `{ names, vregs, len_vregs, has_len, is_slot, is_scalar_slot, slot_ltype }` (`src/lir/lower.tks:33`) — nome, slot **e** tipo de máquina, tudo lá. E `lenv_bind_scalar_slot(env, name, slot, ty)` (`:113`) **já liga um local nomeado a um slot de frame com o seu tipo** |
| **endereço → linha** | **sim, é o coração** | **NÃO EXISTE.** `LInst` carrega `line: u32; col: u32` com valores reais — e o doc-comment em `src/lir/lir.tks:206-209` diz literalmente *"the source position propagated from the TExpr (for the .tsym map and **future debug info**)"*. **A posição chega ao LIR e morre lá.** `MInst` não a carrega |

**Três linhas dizem "já existe" ou "já é calculado". Uma diz "não existe".** O único produtor novo é o
que leva a posição do LIR até aos bytes finais — e §4.3 explica por que forma.

## 4.3 O achado desta passagem, medido hoje: o produtor de `.tsym` tem de MUDAR DE CASA

**Isto não é um alarme; é um crumb com uma ordem obrigatória** (`C1.4`, §3.1), e é a razão de ele
existir.

Medido:

| facto | onde |
| --- | --- |
| o produtor de `.tsym` vive **dentro do emissor de C** | `fn tk_emit_tsym(prog: checker::TProgram): str` em `src/codegen/codegen.tks:12187`, e o comentário admite-o: *"mirror of codegen.c tk_emit_tsym"* |
| e é chamado de **QUATRO** sítios, entre eles a rota **NATIVA** | em `src/build/project.tks`, hoje `:1781` e `:1845` (em `backend`), **`:2749` (em `finish_native_object` — a rota nativa)** e `:3912` (em `build_debug_binary`). **Quatro, e não dois:** medi dois na primeira leitura e a base avançou entretanto, o que **agrava** o achado em vez de o aliviar |
| e o expurgo vai apagar a casa | `docs/design/expurgo-do-c-e-a-busca-por-linker-0.3.1.md`, fatia **6**: *"REMOVER DO FONTE … a emissão de C"* — **pendente** |

**A consequência, e é aritmética simples:** no dia em que a fatia 6 correr, a rota nativa perde o
produtor de `.tsym` se ele ainda estiver em `codegen.tks`. Não é uma incompatibilidade futura — é uma
dependência viva **hoje**, do caminho que fica sobre o caminho que sai.

**A resolução, e é barata:** `C1.4` move o produtor para um módulo próprio (`src/backend/tsym.tks`),
**antes** de qualquer crumb de linhagem, com a prova de que o `.tsym` v1 emitido depois da mudança é
**byte-idêntico** ao de hoje. O crumb não depende do portão e não depende do `tdb`: **é higiene do
expurgo que a proposta descobriu, e reporto-a para cima como tal.**

E há uma segunda razão para a mudança de casa, que é do formato e não do expurgo: `tk_emit_tsym` usa
`cb_fn_name` — o **manglador de símbolos do emissor de C** — para escrever a chave de cada linha. A
chave tem de casar com o símbolo que está na tabela de símbolos do objeto **nativo**. Medido, e é boa
notícia: `LFunc.symbol` é documentado como *"its ALREADY-mangled symbol"* (`src/lir/lir.tks:216-219`),
logo a rota nativa já carrega o nome manglado e a chave casa. **O produtor mudar de casa é o que torna
essa coincidência um contrato em vez de sorte.**

## 4.4 O `.tsym` v2, linha por linha

**Texto, separado por tabulações, como o v1.** Deliberado: legível, diffável, e o leitor não precisa de
um descodificador binário — o que poupa crumbs na fase 4. **E é a escolha que `M1.0` mede antes de a
fixar** (§3.1): se o número de bytes no corpus real passar o limiar que o próprio crumb fixa, entra um
formato compacto e o leitor muda; se não passar, o texto fica e a poupança é real.

| linha | forma | o que dá ao dev |
| --- | --- | --- |
| cabeçalho | `# teko symbol map (.tsym v2)` + a legenda das linhas | o leitor v1 para honestamente; o v2 sabe o que segue |
| **`S`** (já existe como linha sem prefixo no v1) | `S <símbolo> <nome-teko> <ficheiro> <linha-decl>` | os nomes qualificados Teko no `bt` |
| **`L`** | `L <símbolo> <offset-hex> <linha> <coluna>` — uma por posição sobrevivente | `break ficheiro.tks:41`, a listagem, `next`, `step` |
| **`F`** | `F <símbolo> <framed 0\|1> <tamanho> <reg-base> <offset-do-retorno>` | o `bt` **pela verdade do compilador**, em qualquer arquitetura |
| **`V`** | `V <símbolo> <nome> <offset-do-slot> <tipo> <linha-decl>` | `print x`, `locals`, `args` |
| **`T`** | `T <tipo> <forma> <rail>` — a descrição dos tipos de layout **congelado**, e o rail da união quando é união | a formatação legível de §2.2, sem o formatador adivinhar invariantes internos |

**Duas regras de honestidade que ficam escritas na especificação (`C1.10`), e não no emissor:**

1. **Só se descreve tipo cujo layout esteja CONGELADO.** Um local cujo tipo não está no registo **não
   recebe linha `V`** — e o `tdb` responde *"`s`: str — layout not frozen at build time"*, que é uma
   resposta honesta, em vez de mostrar bytes com confiança. É a regra que faz `M5.0` ser uma medição e
   não um bloqueio: se `str` ainda for de duas palavras no dia da fase 5, os locais de tipo `str` ficam
   sem descrição e **tudo o resto avança**.
2. **Uma localização só é válida onde é válida.** Sob o perfil `vars`, **todo local nomeado é fixado a
   um slot de frame** — e a localização passa a ser um único offset válido em **todo** o escopo, em vez
   de um registo que já tem outra coisa fora do intervalo de vida do valor. O mecanismo **já existe na
   árvore**: `lenv_bind_scalar_slot`. É literalmente o que um compilador de C faz sem optimização, e é
   mais barato **e** mais correcto do que descrever localizações por intervalo.

## 4.5 As formas em Teko

O que o implementador acrescenta, com as assinaturas. `void` e sobrecarga são banidos: nenhuma destas
devolve nada sem valor, e cada operação tem um nome só.

```teko
/**
 * MLineMark — a zero-byte machine pseudo-instruction carrying the source position of the
 * instructions that follow it, until the next mark.
 *
 * WHY A MARK AND NOT A PARALLEL ARRAY. The register allocator rewrites the instruction stream
 * one-to-MANY: `rewrite_inst` expands spills and reloads, so an array indexed alongside
 * `MBlock.insts` desynchronises at the FIRST spill and then lies about every position after it,
 * silently. A mark is carried by the stream itself, so it cannot drift from what it marks.
 *
 * Encodes to ZERO bytes. An object built with marks is byte-identical to one built without them,
 * which is the property the golden test asserts and the reason this is safe to add at any time.
 *
 * @since 0.3.1 tdb proposal, fase 1
 * @see LInst  the LIR instruction whose `line`/`col` this carries forward
 */
pub type MLineMark = struct { line: u32; col: u32 }

/**
 * LineRow — one resolved address-to-position row of the `.tsym` v2 `L` table.
 *
 * `offset` is relative to the start of the owning function's symbol, NOT absolute: the debugger
 * resolves the symbol's address from the object's own symbol table, which we already emit, so no
 * relocation and no load-address arithmetic enters this format.
 *
 * @since 0.3.1 tdb proposal, fase 1
 */
pub type LineRow = struct { symbol: str; offset: u64; line: u32; col: u32 }

/**
 * FrameRow — one function's frame descriptor, the `F` table of `.tsym` v2.
 *
 * This is the row that lets `bt` be the STRONGEST claim of the whole arc rather than the most
 * fragile one: it is serialized from `compute_frame_layout_x86`/`compute_frame_layout`, which are
 * EXACT because the compiler computed them. No prologue analysis, no heuristic, no inference —
 * and it is correct on every architecture for the same reason, so arm64 needs no separate proof.
 *
 * @since 0.3.1 tdb proposal, fase 1
 * @see compute_frame_layout_x86  the producer on x86-64
 * @see compute_frame_layout      the producer on arm64
 */
pub type FrameRow = struct { symbol: str; framed: bool; size: u64; base_reg: u32; ra_offset: i64 }

/**
 * LocalRow — one named local's location and type, the `V` table of `.tsym` v2.
 *
 * `slot_offset` is a FRAME-RELATIVE offset and is valid for the WHOLE scope of the binding, because
 * under the `vars` profile every named local is pinned to a frame slot. A location that were only
 * valid inside a value's live range would make `print x` print another variable's value in silence
 * and with confidence — which is worse than refusing to answer.
 *
 * @since 0.3.1 tdb proposal, fase 1
 * @see lenv_bind_scalar_slot  the existing binder this profile routes every named local through
 */
pub type LocalRow = struct { symbol: str; name: str; slot_offset: i64; ty: str; decl_line: u32 }

/**
 * DebugInfo — how much debug information a build emits, the `--debug=<value>` axis.
 *
 * LEVELLED and not boolean, because the two levels answer different questions and the cheaper one
 * is genuinely useful alone: `Lines` answers "where am I and how did I get here" and is what turns
 * production stack traces from per-function into per-line; `Vars` adds "what is `x` worth".
 *
 * @since 0.3.1 tdb proposal, fase 1
 * @see opt_level_of  the sibling build AXIS, likewise CLI-only and absent from `teko.tkp`
 */
pub type DebugInfo = enum { None; Lines; Vars }

/**
 * debug_info_of_value — map the text after `--debug=` to a level, or FAIL.
 *
 * DELIBERATELY UNLIKE `opt_level_of_value`, which resolves a malformed suffix to level 2. An
 * unrecognised optimization level costs speed; an unrecognised DEBUG level costs a whole debugging
 * session spent chasing a breakpoint that silently never resolved. No silent coercion.
 *
 * @param v  the substring after `--debug=`
 * @return   the level for "none", "lines" or "vars"
 * @throws   when `v` is none of the three, naming all accepted values
 */
fn debug_info_of_value(v: str): DebugInfo | error {
    if v == "none" { return DebugInfo::None }
    if v == "lines" { return DebugInfo::Lines }
    if v == "vars" { return DebugInfo::Vars }
    error { message = "teko: --debug=" ~ v ~ ": unknown level — accepted: none, lines, vars" }
}

/**
 * emit_tsym_v2 — serialize the whole debug map for a built program.
 *
 * Lives in its OWN module and not in the C emitter. That is not tidiness: the C emitter is scheduled
 * for deletion by slice 6 of the C purge (`REMOVER DO FONTE … a emissão de C`), and the native route
 * calls this producer today, so leaving it there means the purge takes the native route's symbol map
 * with it.
 *
 * At `DebugInfo::None` this returns the v1 body BYTE-IDENTICAL to what ships today, so the level is
 * additive and no existing consumer changes.
 *
 * @param prog   the checked program (function names, files, declaration lines)
 * @param lines  the resolved address-to-position rows, empty at `DebugInfo::None`
 * @param frames the per-function frame descriptors, empty at `DebugInfo::None`
 * @param locals the named locals, empty unless `DebugInfo::Vars`
 * @param level  the requested debug level
 * @return       the whole `.tsym` text, ready to write beside the binary and into the `.tkl`
 */
pub fn emit_tsym_v2(prog: checker::TProgram, lines: []LineRow, frames: []FrameRow,
                    locals: []LocalRow, level: DebugInfo): str
```

**Onde isto toca o que já existe:**

| função existente | o que muda |
| --- | --- |
| `tk_emit_tsym` (`src/codegen/codegen.tks:12187`) | **sai de `codegen.tks`** e passa a ser o caminho `None` de `emit_tsym_v2` |
| `new_func` / `LFunc` (`src/lir/lir.tks:219`, `:321`) | ganham `file` e `decl_line` |
| `rewrite_inst` (`src/backend/regalloc.tks`) | **não muda** — é a razão de o `MLineMark` ser uma marca no fluxo |
| `compute_frame_layout_x86` / `compute_frame_layout` | **não mudam** — só passam a ter o resultado serializado |
| `lenv_bind_scalar_slot` (`src/lir/lower.tks:113`) | **não muda** — passa a ser o caminho de **todo** local nomeado sob o perfil `vars` |
| `assign_lookup` (`src/backend/regalloc.tks:1475`) | **não muda** — é a chave do JOIN por `vreg_id`, e é já pública |
| `project_arg_of` (`src/main.tks`) | ganha o salto de `--debug=` — **obrigatório**, §4.7 |

## 4.6 A forma do projeto

```
/tdb/tdb.tkp          name = "tdb"; source = "src"; [artifact] kind = "tool"; command = "tdb"
/tdb/main.tks         o virtual-main, sem declarações, como o main.tks do compilador
/tdb/src/…            cli.tks, session.tks, control.tks, breakpoints.tks, tsym.tks, unwind.tks,
                      format.tks, dap.tks
/tdb/tests/…          as fixtures escritas à mão (§6)
```

**`kind = "tool"`, e o estado medido hoje é melhor do que a 2.ª passagem registou.** Ela nomeou dois
defeitos a bloquear a feature. **O primeiro está CONSERTADO na árvore de hoje** — `manifest.tks` já
emite `unknown [artifact] kind "…" — accepted kinds: …` com a lista dos aceites, e existe
`artifact_kinds_listed()` para a produzir. Logo `kind = "tool"` escrito hoje **é erro duro**, que é
exactamente o diagnóstico honesto de *"esta feature ainda não existe"*, e já não um verde falso.

**O segundo continua de pé, e é um passo com ordem obrigatória, não um alarme.** Medido em
`src/build/tkp_rule.tks:16-22`: `check_main_file_rule` exige `main.tks` para `Binary` e **proíbe-o para
tudo o que não seja `Binary`**, com a mensagem *"a library project (static/shared/package) may not have
a main.tks"*. **Um `tool` é um executável e TEM `main.tks`.** Logo o crumb que acrescenta `Tool` ao enum
tem de tocar esta função **no mesmo crumb**, ou toda ferramenta falha a construir com uma mensagem que
enumera três kinds que não a incluem.

**E o `Tool` não é um kind do zero:** é **o caminho do `Binary` mais o tail do `Package`** — as duas
metades já estão escritas no despacho de `backend()`. O que isso resolve, sem tensão nenhuma com o
enunciado do dono: o `.tkl` de um `tool` **não leva binário pré-construído**. Leva o `.tkb` — a árvore
tipada serializada, que já é o payload do `Package` — mais a declaração do comando; e **a máquina do dev
compila-o para o seu alvo**, que é o que ele descreveu. Nada de binários não-portáveis num pacote.

**E a regra de `[deps]`, como o dono a precisou:** um `tool` declarado em `[deps]` **não é recusado — é
ignorado**, sem importar e sem linkar. Isso é mais forte do que uma recusa, e é o que torna o
acoplamento estrutural em vez de voluntário:

> **O `tdb` acopla-se ao compilador por FORMATO (o `.tsym` v2), NUNCA importando `src/`.**

Um `tool` não tem por onde declarar o compilador como dependência, logo o `tdb` **não tem por onde**
importar o checker. A migração para repo próprio deixa de ser disciplina e passa a ser inevitável — que
é o objectivo do dono, obtido por construção. **E o corolário é `C1.10`:** emissor e leitor viverão em
repos diferentes, logo o `.tsym` v2 precisa de **especificação escrita**, e o leitor da fase 4 é escrito
contra ela e não contra o emissor.

## 4.7 A linha que não pode faltar

`project_arg_of` devolve **o primeiro positional que não reconhece como flag**. Uma flag nova que não
entre na sua lista de saltos é lida como **o caminho do projeto**, sem erro de flag desconhecida.
`--debug=vars` tornar-se-ia silenciosamente um caminho, e a construção falharia a dizer que o projeto
não existe.

**Todo crumb que acrescente flag toca `project_arg_of` e paga o teste que o prova.** É uma linha, e a
sua ausência é um defeito de dez minutos a diagnosticar:

```teko
        else if debug_arg_has_prefix(args[i]) { i = i + 1 }
```

---

# 5. Como se chega ao SO

**Um depurador tem de parar um processo, ler-lhe os registos e ler-lhe a memória.** Nenhuma dessas três
coisas existe na nossa superfície hoje. Esta secção mostra a forma que as alcança **sem uma linha de C
nossa e sem a rota C**, e diz o que está medido e o que é o crumb `M2.0`.

## 5.1 O estado medido, hoje

| medição | resultado |
| --- | --- |
| `grep syscall src/ --include=*.tks` | **2 ocorrências, e ambas são COMENTÁRIOS** — `src/runtime/teko_rt.tks:635` e `:643`, os dois a descrever *"the SAME deferred `extern`/syscall bottom"*. **Não existe primitiva de chamada de sistema crua na nossa superfície**, e nem a palavra existe em código |
| a única forma de alcançar o SO | o **FFI, que liga a símbolos POR NOME**: `pub extern fn run(args: []str): i32 = "tk_rt_run" from "teko_rt"` |
| quantos `pub extern fn` existem, e para onde apontam | **24, e TODOS levam `from "teko_rt"`.** Nenhum aponta para uma biblioteca da plataforma |
| existe forma declarada de apontar para a libc? | **sim, e está no tipo.** `TExternDecl.from_lib: str` tem o comentário literal **`("" = implicit libc; valid iff is_extern)`** (`src/checker/tast.tks:172`). Uma declaração **sem** cláusula `from` significa **libc implícita** |
| existe forma declarada de apontar para uma biblioteca de plataforma nomeada? | **sim, e já é usada.** `teko.tkp:72-73` tem `[extern.libs.windows]` com `kernel32 = []`, e `applicable_extern_libs(m, target)` (`src/build/project.tks:1244`) já resolve as declarações aplicáveis por alvo |
| os tipos de transporte de largura de ponteiro existem? | **sim.** `ptr` e `uptr` no checker (`src/checker/scope.tks:387-388`), documentados como *"opaque FFI pointer (transport-only)"* e *"opaque word-size unsigned (transport-only)"* |

**A leitura honesta destas seis linhas:** o mecanismo está **declarado e tipado**; o que está **por andar**
é o caminho de libc implícita pela rota nativa, porque **nenhum dos 24 externs da árvore o exercita**.

**Isso é exactamente um crumb de medição, e é o `M2.0`** (§3.2): declarar `extern fn getpid(): i32 =
"getpid"`, construir pela rota nativa, correr, e ver o pid. Se funcionar, as fases 2 e 3 assentam num
caminho provado. Se falhar, o crumb reporta **onde** falha — checker, símbolo indefinido, ou linha de
linker — e o conserto é um crumb nomeado, em vez de uma descoberta a meio da fase 3. **É o crumb mais
importante da proposta, e é por isso que abre a fase e não fecha.**

## 5.2 A forma das declarações

Nenhuma linha de C. Nenhum ficheiro novo em `src/runtime/`. Nenhuma compilação pela rota C.

```teko
/**
 * ptrace — the Linux process-tracing entry point, bound by name to the platform's C ABI library.
 *
 * NO C OF OURS. This is a Teko declaration that links to a symbol the platform already exports;
 * the same mechanism every one of the 24 existing `extern fn` declarations in this tree uses, with
 * an EMPTY providing library, which `TExternDecl.from_lib` documents as implicit libc.
 *
 * `addr` and `data` are `uptr` because they are transport-only opaque words here: a request either
 * ignores them or treats them as an address in the TRACEE's space, which is not a pointer this
 * process may ever dereference. Typing them as `uptr` says exactly that.
 *
 * @param request  the PTRACE_* request, from the named constants beside this declaration
 * @param pid      the tracee's process id
 * @param addr     the request's address operand, in the TRACEE's address space, or 0
 * @param data     the request's data operand, or 0
 * @return         the request's result; -1 signals failure, which the caller MUST check
 * @since 0.3.1 tdb proposal, fase 2
 * @see dbg_spawn_traced  the only caller that passes PTRACE_TRACEME
 */
pub extern fn ptrace(request: i32, pid: i32, addr: uptr, data: uptr): i64 = "ptrace"

/**
 * dbg_spawn_traced — fork, mark the child traceable, and exec the target, leaving it STOPPED at
 * its first instruction.
 *
 * Stopped-at-entry is not a convenience: it is the only moment at which breakpoints can be planted
 * before any user code runs, so `tdb exec` with a breakpoint in `main` cannot miss it.
 *
 * @param argv  the target program and its arguments, argv[0] being the program
 * @return      the tracee's process id, stopped and waiting
 * @throws      when the fork or the exec fails, naming which and why
 * @since 0.3.1 tdb proposal, fase 2
 */
pub fn dbg_spawn_traced(argv: []str): i32 | error
```

## 5.3 Como "SEM C LANG" se lê, e é um facto de plataforma

**Duas leituras da ordem, e uma delas é inexequível — por facto de plataforma, não por preferência:**

| leitura | consequência |
| --- | --- |
| **(a) sem C ESCRITO por nós e sem BACKEND C**, com `extern` para a biblioteca de ABI-C da plataforma | **possível hoje.** Nenhuma linha de C nossa. É como toda linguagem compilada alcança o SO |
| **(b) sem tocar em nada da plataforma — chamada de sistema crua** | **impossível em dois dos três alvos.** Em macOS a Apple **não garante a ABI de syscall** e quebra-a entre versões; em Windows **não há interface de syscall estável** — a fronteira suportada é `kernel32`/`ntdll` |

**E o precedente é o Go — a referência que o dono atribuiu para comportamentos:** o Go faz syscalls
**cruas em Linux**, mas passa por **libSystem em Darwin** e por **`kernel32` em Windows`**. Ou seja: a
própria linguagem nomeada como modelo adopta (a) onde (b) não é possível.

> **A assunção sob a qual a proposta prossegue, declarada:** *"SEM C LANG"* = **nenhuma linha de C
> escrita por nós e nenhuma compilação pela rota C**. `extern` para a biblioteca da plataforma é
> permitido, porque a alternativa não é mais pura — é inexequível em dois dos três alvos.
>
> **Se o dono quiser (b) em Linux por princípio, isso é uma primitiva de linguagem nova e um arco
> próprio**, e tem de ser dito. Não é pré-requisito desta proposta: o `tdb` em Linux funciona por (a), e
> trocar (a) por (b) mais tarde muda **uma declaração por chamada**, não o desenho.

**E isto corrige, por lei, o que a 2.ª passagem recomendou.** Ela orçou o chão de controlo *"pela via
Teko + shim no `teko_rt.c`"*, argumentando que a exceção mantida de `src/runtime/teko_rt.{c,h}` a
tornava legal. **A ordem `"SEM C LANG, somente Teko nativo"` é posterior e mais forte**, e retira essa
via. A rota desta proposta é o `extern` de libc implícita, que **não escreve C nenhum** — e é por isso
que `M2.0` é o crumb que abre a fase 2 em vez de um shim que já não é permitido.

---

# 6. Como o `tdb` se prova

**Esta é a parte mais interessante do desenho, e a restrição que a torna interessante é dura:**

> **A suíte do `tdb` não pode depender do `tdb`, nem de um binário Teko correcto, nem de um depurador de
> terceiros.**

As três dependências proibidas são a mesma dependência: **um oráculo que partilha a origem do que está a
ser testado**. Se o `tdb` lê o `.tsym` v2 que sai do nosso compilador, então o compilador a mentir e o
`tdb` a reportar a mentira fielmente são **indistinguíveis** — a suíte fica verde e a ferramenta ensina
errado.

## 6.1 O oráculo é a FIXTURE, e é escrita à mão

**A resolução é a que o dono ratificou:** as fixtures do `tdb` são **objectos escritos à mão, com
tabelas de correcção conhecida**. Um objecto cujos bytes foram escritos por uma pessoa, com uma tabela
cuja resposta certa é conhecida **antes** de o `tdb` existir, é um oráculo **independente por
construção** — e não precisa que nenhum gdb concorde com ele.

**E os dois ficheiros do PoC da 2.ª passagem já SÃO o primeiro fixture.** Estão versionados, em
`docs/design/debugger-poc/`, e correm hoje:

| ficheiro | o que fixa | porque é oráculo |
| --- | --- | --- |
| `mini.s` | duas molduras, posições conhecidas, tabelas escritas **byte a byte com `.byte`** | **nenhuma ferramenta as gerou.** A resposta certa é a que a pessoa escreveu, e é verificável lendo o ficheiro |
| `adv.s` | **cinco** molduras através de **todas** as formas de frame do nosso encoder — incluindo a função frameless que chama e nunca retorna | prova que o desenrolar não depende da sorte de uma forma de frame ser a comum |
| `hello.tks` | o texto Teko que as tabelas **apontam**, e que **nunca é compilado** | a isolação **é** o ponto: o depurador lista texto Teko enquanto passa por código de máquina que nenhum compilador Teko produziu. Prova-se o leitor, não o compilador |
| `reproduce.sh` | corre e **sai 0** | documentação que executa |

**A promoção destes quatro a fixtures citadas por um teste é o crumb `F0.1`**, e é a razão de a fase 0
ser a que se pode fazer antes do portão sem desperdício (§7.2).

## 6.2 Os três níveis da suíte, e o que cada um pode e não pode usar

| nível | pode usar | prova o quê | onde |
| --- | --- | --- | --- |
| **1 — o leitor contra fixtures escritas à mão** | um objecto `.s` e um `.tsym` escritos à mão; **nenhum compilador** | que o leitor de `.tsym` v2, a resolução linha↔endereço e o desenrolar dão a resposta **conhecida**. E que uma tabela deliberadamente **errada** falha | `/tdb/tests`, e corre em qualquer host |
| **2 — o emissor contra goldens** | o compilador; **nenhum `tdb`** | que o `.tsym` v2 emitido para um `.tks` conhecido é **byte-a-byte** o esperado, e que um objecto com `MLineMark` é byte-idêntico a um sem | `src/`, na suíte do compilador |
| **3 — a ponta a ponta** | os dois | que os níveis 1 e 2 concordam sobre o **mesmo** binário: uma sessão dirigida por script pára onde a fixture diz que devia parar | a perna nativa do CI |

**A propriedade que faz isto ser uma prova e não uma tautologia:** os níveis 1 e 2 têm **oráculos
diferentes**. O nível 2 compara o emissor com bytes escritos por uma pessoa; o nível 1 compara o leitor
com tabelas escritas por uma pessoa. **Uma discordância no nível 3 localiza-se imediatamente** — se o
nível 2 está verde e o 3 vermelho, o defeito é do leitor; se o 1 está verde e o 3 vermelho, é do
emissor. Nenhum dos dois pode encobrir o outro, porque nenhum dos dois é a referência do outro.

## 6.3 O que substitui o gdb como leitor independente

A 2.ª passagem argumentava que o DWARF valia por ser o **único leitor independente** da nossa tabela de
linha — que sem ele, o `tdb` e o compilador a mentirem juntos seriam indetectáveis. **Esse argumento
está satisfeito sem DWARF nenhum, e é o §6.1 que o satisfaz:** a fixture escrita à mão é o leitor
independente, e é melhor nesse papel do que o gdb, por três razões medíveis — corre em qualquer host sem
instalar nada; a sua resposta certa é **legível no próprio ficheiro** em vez de sair de outro programa; e
não muda de comportamento entre versões de uma ferramenta que não controlamos.

**Logo o gdb era conveniência, não necessidade**, e o interop sair do caminho crítico não deixa o `tdb`
sem oráculo. Deixa-o com um melhor.

---

# 7. O portão

**Verbatim, e é o enquadramento inteiro:** *"a ideia é uma proposta, não iremos implementar nada disso
nessa versão ou em outra próxima, primeiro precisamos do teko 100% nativo (emissão e linhagem)."*

**Este documento é PROPOSTA.** Não é fila, não é plano de execução, não é orçamento a consumir. **Nada do
arco `tdb` entra nesta versão nem na seguinte.** Quem despachar um crumb das fases 1 a 7 antes do portão
está a violar isto.

**E o portão tem DUAS metades** — *"emissão"* **e** *"ligação"*. A segunda palavra não é interpretação:
está ratificada em `docs/design/expurgo-do-c-e-a-busca-por-linker-0.3.1.md`, com as palavras do dono:

> *"não é .c que importa, importa o linker, dito isso, nem mesmo cc ou gcc importam, importa o linker pq
> não devemos mais emitir nenhum arquivo .c e todos os arquivos .c e .h presentes no [repo] … incluindo
> ajustes nas lanes e no compilador para procurar pelo linker e não pelo compilador C."*

## 7.1 As duas metades, com o número de HOJE

**Isto está medido em 2026-07-30, no SHA `e317b44`, e é deliberadamente o número de hoje e não o de
ontem** — a metade da emissão mexeu desde que o portão foi enunciado.

### Metade 1 — EMISSÃO: mais perto do que quando o portão foi enunciado

O portão foi enunciado com a paragem viva no **degrau 27** (`ftoa`). **O `ftoa` caiu.** O self-host
nativo atravessa agora, sem parar, o front-end inteiro:

```
lexer 143/143 ✓   parser 143/143 ✓   checker 6449/6449 ✓   monomorph 0/0 ✓   consteval 571/571 ✓
teko: .: native backend N1: `null` match pattern not yet lowered (N2) [in `teko::codegen::emit_variant_wrap`]
```

| item | estado hoje |
| --- | --- |
| a paragem viva | **degrau 30** — `` `null` match pattern `` em `emit_variant_wrap`. **Não** o degrau 27 |
| lexer, parser, checker, monomorph, consteval | **todos passam.** O checker subiu de 6406 para 6449 itens |
| degraus 28 e 29 | **28 fechado no CI**; 29 na escada, com lane |
| o que o degrau 30 separa | **é o ÚNICO obstáculo entre a lane e uma gen2 nativa, que nunca existiu** |
| a família do degrau 30 | a mesma que os degraus 25 e o arco `null-adopt` já tocaram — **há molde** |
| a forma da paragem | **honesta e nomeada** (`native backend N1`), e o pin do CI afirma o par estável (não completa **e** falha por paragem nomeada), deliberadamente não o texto do degrau corrente |

**A leitura, e é boa notícia dentro do plano:** a metade da emissão está a **um degrau** de um marco que
nunca existiu. Isso não abre o portão — o portão exige as duas metades — mas muda o que se pode dizer
sobre o horizonte: em vez de *"a escada está no degrau 27, no meio do front-end"*, o número de hoje é
*"o front-end inteiro passa, e falta um degrau para a primeira gen2 nativa"*.

### Metade 2 — LIGAÇÃO: não mexeu

Medido na tabela de fatias de `expurgo-do-c-e-a-busca-por-linker-0.3.1.md:440`: **4 de 9 entregues.**

| fatia | conteúdo | estado |
| --- | --- | --- |
| 1 | o mapa medido | **entregue** |
| 2 | `ci_provision_teko.sh` provisiona o runtime da era do seed | **entregue** |
| 3a | asserção de arquitetura no asset publicado | **entregue** |
| 3b | **`src/build/linker.tks`** — a busca pelo LINKER, asserção de arquitetura, recusa nomeada de MinGW | **entregue** (363 linhas na árvore) |
| 4 | `teko_rt.tks` fecha o bottom com `extern fn` + o arena vai para Teko | **pendente** |
| 5 | `build_cc_argv` parte-se — a metade de compilar C morre, a de linkar vira linha de linker | **pendente** |
| **6** | **REMOVER DO FONTE a sondagem inteira e a emissão de C** | **pendente** |
| 7 | apagar os oito ficheiros, **depois** do gen1 | **pendente** |
| 8 | gen2 e gen3 nativos com os ficheiros ausentes + o hello world sozinho | **pendente** |
| 9 | as lanes | **pendente** |

**A leitura honesta:** a **busca** pelo linker está entregue — o compilador já sabe procurar um linker em
vez de um compilador de C. O que falta é o **expurgo**: a emissão de C sair do fonte (fatia 6), os
ficheiros desaparecerem (7), e um gen2/gen3 nativo provar-se com eles ausentes (8). **É a metade que
governa o portão hoje**, e não mexeu desde o enunciado.

### O portão, numa linha

> **O portão abre quando o degrau 30 e os seus sucessores fecharem (emissão, hoje a um degrau da
> primeira gen2 nativa) E as fatias 4 a 9 do expurgo fecharem (ligação, hoje 4 de 9, com a fatia 6 — a
> emissão de C sair do fonte — ainda por fazer).**

**E a razão prática de o portão existir, que é o que evita trabalho desperdiçado:** um crumb do `tdb`
escrito hoje assenta numa árvore que ainda liga por `cc`. O `tdb` precisa de parar processos e ler
tabelas de um binário que **ele próprio** ainda não produz de ponta a ponta. Fazê-lo antes do portão é
construir sobre a metade que vai mudar.

## 7.2 O que se pode fazer antes do portão sem desperdício

**Três coisas, e a primeira é a recomendação.**

| item | porque não expira | custo |
| --- | --- | --- |
| **A fase 0 inteira — o arnês e o oráculo (§3.0)** | é **activo de teste**, e é agnóstico de camada, de depurador e de rota de ligação. As suas fixtures são objectos escritos à mão que **não passam pelo nosso compilador** (§6.1), logo nada nelas muda quando a emissão ou a ligação mudarem. E o seu fixture principal **já está versionado neste repo** | **3 crumbs**, e nenhum toca `src/` |
| **`C1.10` — a especificação escrita do `.tsym` v2** | é doc-only, e é o **contrato** de que o `tdb` dependerá num repo diferente. Escrevê-la antes é o que impede o formato de ser definido pela implementação | **1 crumb**, em `docs/` |
| **`C1.4` — o produtor de `.tsym` muda de casa** | **não é do `tdb`: é higiene do expurgo** (§4.3). É uma dependência viva hoje, do caminho que fica sobre o caminho que sai, e o crumb prova-se por identidade byte-a-byte. Vale por si, com ou sem `tdb` | **1 crumb**, e **reporto-o para cima** como item do expurgo, não como crumb desta proposta |

**A recomendação:** se se quiser gastar o mínimo antes do portão, gaste-se a **fase 0**. Três crumbs, zero
toques em `src/`, e o resultado é uma suíte que continua a valer no dia em que tudo o resto mudar — mais
`C1.10`, que é uma folha de `docs/`.

**E o que NÃO se faz antes do portão, dito para não haver dúvida:** as fases 1 a 7. Incluindo a fase 1,
apesar de ela render sozinha, porque toca `src/lir` e `src/backend` — exactamente onde a escada de
degraus está a trabalhar.

---

# 8. As referências

## 8.1 O Go é o precedente DIRECTO, e é o único que o é

O dono nomeou-o: *"um [depurador] próprio (como em Go)"*. **A referência é directa, e verifica-se em
dois pontos.**

**(a) A forma.** O `dlv dap` é **um modo do próprio binário**, não um adaptador separado —
verificado na documentação do delve (`Documentation/api/dap/README.md`): *"starts a single-use DAP-only
server"*, cujo *"primary user … is VS Code Go"*. **`tdb dap` copia isso** (§2.4, §3.6), e é o que evita um
segundo executável para manter, com um segundo ciclo de release e uma segunda versão a divergir.

**(b) O CRITÉRIO — porque é que o Go construiu um.** Verificado (`go.dev/doc/gdb`, literal):

> *"GDB does not understand Go programs well. The **stack management, threading, and runtime** contain
> aspects that differ enough from the execution model GDB expects that they can confuse the debugger and
> cause **incorrect results** … it is not a reliable debugger for Go programs, **particularly heavily
> concurrent ones**."*

E a razão que a documentação do próprio Go dá para preferir o delve: *"It understands the Go runtime,
data structures, and expressions better than GDB"*.

**O critério é claro: um depurador de terceiros modela o SEU modelo de execução.** Onde o nosso difere,
ele não fica incompleto — fica **errado**, o que é pior.

**E o aviso de escopo, medido, que ordena a fase 7 por último:** o delve — uma década de manutenção e
patrocínio corporativo — cobre **cinco** pares de plataforma (`linux/amd64`, `linux/arm64`, `linux/386`,
`windows/amd64`, `darwin/amd64`), e **não cobre `darwin/arm64`**. **Isto não é argumento contra o `tdb`**
— o dono decidiu-o, e um depurador que cobre Linux é infinitamente mais do que nenhum. É o que faz a fase
7 ser **uma plataforma de cada vez**, com uma medição a abrir cada metade.

## 8.2 O que no NOSSO runtime dá o mesmo forçamento — candidatos, e o estado medido

Apliquei o critério do Go a Teko. **O resultado importa mesmo com o `tdb` já decidido, porque diz de onde
vem o valor do `tdb` e portanto o que priorizar nele.**

| propriedade | torna um depurador de terceiros **errado** ou só **incompleto**? | consequência para a proposta |
| --- | --- | --- |
| **`defer` que dispara em toda saída de escopo** | **é o candidato mais forte, e é de COMPORTAMENTO.** Um `next` sobre um `return` **executa código que não está naquela linha** — os `defer` do escopo. Um depurador que assuma "uma linha, um passo" mostra o programa a saltar para linhas que já passaram, ou a demorar num `return` sem explicação | **o `tdb` não esconde e não simplifica:** com granularidade de statement, o `step` **entra** nas linhas do `defer`, porque esse código corre. É `T4.4`, e o teste é o `next` sobre um `return` com `defer` |
| **`T \| error` como valor, com três rails** (`niche`, `InlineTag`, box-em-arena) | **incompleto num, ERRADO noutro.** O rail `niche` de uma palavra **não tem palavra de tag nenhuma** — o padrão do ponteiro discrimina. Um leitor genérico vê uma palavra e não tem como saber que membro está activo; se adivinhar, mente | **é o que faz `T5.3` existir**, e é a razão de o **rail ir no formato** e não no formatador (§3.5) |
| **arena** (região raiz, bump, push/pop) | **incompleto, não errado** | o formatador de arena é **valor** (melhor experiência), não correcção |
| **monomorfização** | **incompleto, e nós já o resolvemos** — o `.tsym` já emite `<símbolo>\t<nome-teko>`, logo o `bt` do `tdb` mostra `ns::fn` em vez do símbolo manglado | vantagem **já paga** pelo formato que existe |
| **`str` com dois contadores** (caracteres e bytes) | **errado, se descrito com o layout errado** | é por isso que `M5.0` mede o layout no dia da fase, e a regra do congelamento (§4.4) impede uma descrição errada de existir |
| **concorrência** | **não hoje.** Medido: o chão desenhado é `pthread_create`/`pthread_join`/`pthread_self` — **threads 1:1 do SO**, sem escalonador nosso e sem pilhas geridas. É exactamente o que um depurador genérico modela nativamente | **o `tdb` não precisa de modelo de tarefas nesta proposta** — e é isto que mantém as fases 2 a 6 no número que têm em vez do dobro |

**A leitura, e é o que fundamenta a tese de §1:** dos seis, **dois** já forçam a resposta hoje — o
`defer`, por comportamento, e o rail `niche`, por representação. Os outros quatro são valor ou já estão
pagos. **Dois forçamentos bastam**, porque são exactamente os dois que aparecem em todo programa Teko
real: um `return` num escopo com `defer`, e um `T | error` a ser inspeccionado.

## 8.3 O gatilho de futuro, nomeado

> **No dia em que Teko ganhar escalonador próprio, tarefas verdes, ou pilhas geridas/crescíveis, o
> precedente do Go morde com muito mais força — e o orçamento da proposta SOBE.**

O `tdb` passaria a precisar de um modelo de tarefas: listar, comutar entre elas, e desenrolar pilhas
segmentadas. E é exactamente a lista que a documentação do Go nomeia (*"stack management, threading, and
runtime"*) como a razão de o gdb não servir.

**Estado medido hoje: a condição NÃO está satisfeita.** A palavra "corrotina" no desenho de concorrência
adiantada tem como chão uma thread do SO. **Reabrir esta secção quando mudar** — e quando mudar, o valor
do `tdb` sobe mais do que o custo, porque é o cenário em que um depurador de terceiros deixa de ser
incompleto e passa a ser errado.

## 8.4 As outras três, e o que cada uma contribui

Atribuições do dono: superfície → Rust, controlo → Zig, addins → C#, comportamentos → Go.

| referência | o que esta proposta adopta | o que **não** adopta, e porquê |
| --- | --- | --- |
| **Rust** → superfície | **o eixo nivelado com valores nomeados.** O rustc documenta `line-tables-only` como *"the minimal amount of debug info for backtraces with filename/line number info, but not anything else"* — **um nível de primeira classe**, distinto de `full`. O nosso `--debug=lines` **é** esse nível, e `--debug=vars` é o `full`. E `-g` significa `full` em gcc, clang **e** rustc, logo é alias de `vars` e de mais nada | `split-debuginfo` (`dsymutil`/`dwp`/PDB): são optimizações de **tamanho** de DWARF, e o DWARF não está nesta proposta |
| **Zig** → controlo | **o padrão de ler informação de posição PRÓPRIA para stack traces**, sem depender de terceiros — que é o que o `.tsym` já faz por função e o que a fase 1 leva à linha. Com a referência ao lado, deixa de ser medição solta e passa a **precedente de desenho** | o Zig lê **DWARF e PDB de verdade** (`SelfInfo` por formato de objeto). Nós propomos um formato nosso, mais barato de ler e legível; **e registo a diferença**: no dia em que o `tdb` quiser entrar em código de biblioteca da plataforma, um leitor de DWARF entra como arco próprio, e o `.tsym` v2 fica como o caminho rápido |
| **C#** → addins | **`kind = "tool"`, e é a única das quatro referências com um TIPO de pacote declarado** para "executável que se instala mas não é dependência". Adopto três coisas nomeadamente: o tipo declarado; **o nome do comando declarado à parte do nome do projeto** (`command = "tdb"`, o `ToolCommandName`), porque é o que evita a colisão silenciosa que `cargo install` e `go install` têm; e a regra de que **o tipo, não a disciplina**, decide a entrada em `[deps]` | **o modelo de depuração do .NET não se aplica, e não o invoco.** É um runtime gerido, com um contrato de depuração dentro do runtime. Teko compila para nativo. E não copio o `PackageOutputPath`: já temos `-o` |

---

# 9. O que medi de novo nesta passagem

Tudo abaixo é desta árvore, no SHA `e317b44`, 2026-07-30. **Duas medições corrigem o registo.**

| # | medição | consequência |
| --- | --- | --- |
| 1 | **`tk_emit_tsym` vive no emissor de C** (`src/codegen/codegen.tks:12187`) **e é chamado de QUATRO sítios**, entre eles a rota **nativa** (`finish_native_object`) e a de `teko run` — e a fatia 6 do expurgo apaga essa casa | **o achado da passagem.** Vira o crumb `C1.4`, com ordem obrigatória antes de qualquer linhagem, e **reporta-se para cima como higiene do expurgo** (não é do `tdb`). **Medi dois sítios; a base avançou durante a escrita e são quatro** — o acoplamento é mais largo do que a primeira leitura, e o crumb vale mais |
| 2 | **`grep syscall src/ --include=*.tks` dá DUAS ocorrências, e ambas são comentários** (`src/runtime/teko_rt.tks:635`, `:643`) | corrige o digest, que registou zero. **A conclusão não muda** — não há primitiva de syscall crua, e nem a palavra existe em código — mas o número certo é 2 |
| 3 | **`TExternDecl.from_lib` documenta `"" = implicit libc`** (`src/checker/tast.tks:172`), **e os 24 `pub extern fn` da árvore levam TODOS `from "teko_rt"`** | o mecanismo de alcançar a libc está **declarado e tipado**, e o caminho está **por exercitar**. Vira `M2.0`, o crumb que abre a fase 2 — e **substitui o shim em `teko_rt.c` que a 2.ª passagem recomendou** e que *"SEM C LANG"* retirou |
| 4 | **`kind` desconhecido JÁ É ERRO DURO** com a lista dos aceites (`src/build/manifest.tks:550`, `artifact_kinds_listed()`) | a RED-FLAG 7 da 2.ª passagem está **CONSERTADA na árvore**. `kind = "tool"` hoje é erro honesto, não verde falso. **Um crumb menos** |
| 5 | **`check_main_file_rule` continua a proibir `main.tks` fora de `Binary`** (`src/build/tkp_rule.tks:16-22`), com a mensagem que enumera `static/shared/package` | continua de pé, e entra como **passo com ordem obrigatória** (§4.6): o crumb que acrescenta `Tool` toca esta função no mesmo crumb |
| 6 | **`LFunc` NÃO tem `file`/`decl_line`** (`src/lir/lir.tks:219`) e **`LInst` TEM `line`/`col`**, com o doc-comment a dizer *"for the .tsym map and future debug info"* (`:206-209`) | confirma `C1.1`, e confirma que a posição **chega ao LIR e morre lá** |
| 7 | **`LFunc.symbol` é *"its ALREADY-mangled symbol"*** (`src/lir/lir.tks:216-219`) | a chave do `.tsym` casa com o símbolo do objeto **nativo**. O produtor mudar de casa (`C1.4`) é o que torna isso um contrato em vez de sorte |
| 8 | **`str` continua com DUAS palavras** e `length in BYTES` (`src/runtime/teko_rt.h:44-48`), apesar de a decisão dos dois contadores estar registada | a decisão existe, o layout não mudou. Vira `M5.0` — **medir no dia da fase**, e a regra do congelamento faz o resto avançar sem ele |
| 9 | **A emissão está no degrau 30**, com lexer/parser/checker (6449)/monomorph/consteval **todos a passar** | o portão está **mais perto** na metade da emissão do que quando foi enunciado (era o degrau 27, `ftoa`). §7.1 |
| 10 | **A ligação está em 4 de 9 fatias**, com `src/build/linker.tks` entregue (363 linhas) e a **fatia 6 pendente** | a metade que **governa** o portão hoje. §7.1 |
| 11 | `FrameLayoutX86`/`FrameLayout` e `compute_frame_layout*` existem e são exactos; `LEnv` carrega `names`/`vregs`/`is_scalar_slot`/`slot_ltype`; `lenv_bind_scalar_slot` e `assign_lookup` são públicas | confirma que **três das quatro tabelas já existem ou já são calculadas** (§4.2), e que os locais são um **JOIN** por `vreg_id`, não plumbing |
| 12 | `TEKO_LEGISLATION.md:350` continua a designar o `.tsym` como *"debug symbols … **for the debugger**"* | estender o `.tsym` é **obedecer**. Sem tensão a resolver |

**Sem alarmes por precaução.** As duas incertezas materiais desta proposta — o caminho de libc implícita
pela rota nativa, e o layout de `str` — estão **dentro do plano**, como `M2.0` e `M5.0`, que é onde a lei
de forma manda que estejam.

**Nada nesta proposta precisa de palavra do dono para ser executável.** A única coisa que ele pode querer
dizer, e que não bloqueia nada, está declarada em §5.3: se quiser **chamada de sistema crua em Linux por
princípio**, em vez do `extern` para a biblioteca da plataforma, isso é uma primitiva de linguagem nova e
um arco próprio — e trocar depois muda **uma declaração por chamada**, não o desenho.
