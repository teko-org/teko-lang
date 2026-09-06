# As formas dos canais nativos nas três plataformas

Levantamento pedido pelo dono em 2026-07-30, para além do caso dos testes: *"unix dá suporte
full-duplex, e no Windows tem suporte broadcast… quais as formas suportadas pelos canais nativos das
3 plataformas, e se há como dizer se quer uma conexão de mão dupla, broadcast, só leitura ou só
escrita"*. E, em 2026-07-31, a segunda metade da pergunta, que é a que decide o desenho: *"se
possível múltiplos escritores, todos os threads poderiam escrever no mesmo canal e um consumidor (o
orquestrador) o leria, eliminando a necessidade de um handler em cada canal para escrever em outro
(interno)… e esse é o açúcar"*.

**Tudo aqui está MEDIDO nas quatro pernas.** A primeira versão deste documento tinha as colunas de
macOS e de Windows marcadas como *(conhecimento)*; as sondas de `theory/canais/` correram no CI e
substituíram cada uma dessas células por um número. **Três células viraram do avesso** — e estão
nomeadas na secção "O que a medição desfez", porque o erro é mais instrutivo do que o acerto.

Sondas: `theory/canais/posix_probe.c` (formas), `theory/canais/posix_mpsc.c` (N escritores),
`theory/canais/win_probe.c`, `theory/canais/win_mpsc.c`. Fluxo:
`.github/workflows/theory-canais-nativos.yml`. Corrida de referência: **30593502637**.

---

## 1. As formas do canal — as quatro colunas

| capacidade | linux-x86_64 | linux-arm64 | macos-arm64 | windows-x86_64 |
|---|---|---|---|---|
| **mão dupla** | SIM | SIM | SIM | SIM (`PIPE_ACCESS_DUPLEX`) |
| **meia-direção em execução** | SIM (`EPIPE`) | SIM | SIM | **NÃO — não há `shutdown`** |
| **direção declarada na criação** | não se aplica | — | — | SIM (`INBOUND`/`OUTBOUND`) |
| **`SOCK_SEQPACKET`** | SIM | SIM | **NÃO — `errno 43`** | não se aplica |
| **fronteira preservada** | `SEQPACKET`, `DGRAM` | idem | **só `DGRAM`** | `PIPE_TYPE_MESSAGE`, mailslot |
| **fronteira perdida (controle)** | `STREAM`: 3+5 → 8 | idem | `STREAM`: 3+5 → 8 | `PIPE_TYPE_BYTE`: 3+5 → 8 |
| **pipe anônimo é half-duplex** | SIM (`EBADF`) | SIM | SIM | SIM |
| **broadcast por nome** | **NÃO** (`EADDRINUSE`) | NÃO | NÃO | só **mailslot** |
| **namespace sem artefato** | SIM (abstrato) | SIM | **NÃO — `errno 2`** | SIM (NPFS) |
| **`SO_SNDBUF` concedido → útil** | 212992 → **180224** (84,6%) | idem | **8192 → 8192** (100%) | — |
| **`SCM_RIGHTS`** | SIM | SIM | SIM | **sem equivalente** |

## 2. N escritores, um leitor, um só canal — a pergunta do açúcar

A propriedade de que o `chan<Rec>` depende: **N escritores concorrentes cabem num extremo só sem se
estragarem uns aos outros?** Se não coubessem, o desenho seria obrigado a dar um canal a cada thread
e a pôr um handler a copiar de cada um para um canal interno — exatamente a peça que o dono quer
apagar.

**A prova de não-corrupção é por CONTEÚDO, não por contagem.** Cada registo carrega o id do
escritor, um número de sequência e um corpo preenchido com um padrão derivado dos dois. Contar 2000
registos não prova nada: dois registos rasgados a meio ainda contam dois. O que prova é cada registo
**verificar**.

| propriedade | linux-x86_64 | linux-arm64 | macos-arm64 | windows-x86_64 |
|---|---|---|---|---|
| **fan-in por nome, 1 fd no leitor** | `DGRAM` 2000/2000 | 2000/2000 | `DGRAM` 2000/2000 | **mailslot** 2000/2000 |
| corrompidos | 0 | 0 | 0 | 0 |
| todo escritor foi ouvido | 500×4 | 500×4 | 500×4 | 500×4 |
| **N threads, 1 descritor partilhado** | 0 rasgados/2000 | 0 | 0 | 0 (pipe nomeado) |
| **cheio: queda silenciosa?** | **NÃO** (`EAGAIN`) | NÃO | **NÃO** (`ENOBUFS`) | **NÃO** |
| aceite == entregue | 11 == 11 | — | 32 == 32 | 2000 == 2000 |
| **teto de uma mensagem** (buffer por omissão) | 212960 | 212960 | **2048** ² | ≥ 1 MiB ¹ |
| **teto com `SO_SNDBUF` a 4 MiB** | 4194304 | — | **4194288** ² | — |

¹ **Medido de novo, por bisseção com 16 MiB — e o sistema continuou a não recusar.** A primeira
sonda parou por esgotar o próprio buffer de 1 MiB; a segunda subiu a busca para 16 MiB e o mailslot
com `nMaxMessageSize = 0` aceitou **16777216 bytes**, com erro 0. **Isto é outra vez o teto da sonda,
não o do sistema** — o teto real do mailslot local é **≥ 16 MiB** e continua por fechar. Registo o
erro em vez de o esconder: subi o limite e não subi o suficiente.

O que ficou **medido de verdade** é mais útil do que o teto: **um `nMaxMessageSize` declarado É
RESPEITADO.**

| `nMaxMessageSize` | maior aceite | erro na recusa |
|---|---|---|
| 0 (sem limite) | 16777216 (= o teto da sonda) | — |
| 512 | **512** | 87 (`ERROR_INVALID_PARAMETER`) |
| 4096 | **4096** | 87 |
| 65536 | **65536** | 87 |
| 1048576 | **1048576** | 87 |

O parâmetro é um **limite**, não uma sugestão — o que torna o mailslot dimensionável na criação, tal
como o `SO_SNDBUF` torna o `AF_UNIX`.

### O pipe nomeado do Windows, e uma célula que eu quase publiquei errada

| `nOutBufferSize` pedido | maior mensagem aceite |
|---|---|
| 0 (o sistema decide) | **0** ³ |
| 4096 | 4096 |
| 65536 | 65536 |
| 1048576 | 1048576 |
| 8388608 | 8388608 |

O teto **segue o `nOutBufferSize`** exatamente, apesar de a documentação lhe chamar *conselho*.

³ **A linha do `0` é um artefacto da MEDIÇÃO, não uma propriedade do pipe.** A sonda escreve em
`PIPE_NOWAIT` — obrigatória, porque com escrita bloqueante e o leitor no mesmo fio uma mensagem maior
do que o buffer pendura. Em `PIPE_NOWAIT` uma escrita só passa se **couber no buffer**, e com
conselho 0 o buffer é 0. **Em modo bloqueante, com um leitor a drenar, um pipe de conselho 0 leva
quantidade arbitrária.** Publicar "o pipe por omissão não leva nada" seria a mesma patologia dos 2048
do macOS, uma casa adiante.

² **Os 2048 não eram teto — eram o valor por omissão, e a primeira versão desta tabela publicou-os
como se fossem propriedade do sistema.** O dono desconfiou (*"algo me diz que conseguimos otimizar
isso"*) e a desconfiança estava certa. Medido: **o teto segue o `SO_SNDBUF`**, menos ~16–32 bytes de
sobrecarga por datagrama.

| pedido | macOS: dado → maior datagrama | Linux: dado → maior datagrama |
|---|---|---|
| (omissão) | 2048 → 2048 | 212992 → 212960 |
| 8 KiB | 8192 → 8176 | 16384 → 16352 |
| 64 KiB | 65536 → 65520 | 131072 → 131040 |
| 256 KiB | 262144 → 262128 | 524288 → 524256 |
| 1 MiB | 1048576 → 1048560 | 2097152 → 2097120 |
| 4 MiB | **4194304 → 4194288** | 8388608 → 4194304 |

O **macOS concede exatamente o que se pede; o Linux duplica** — por isso o concedido é sempre relido
com `getsockopt` em vez de assumido. A patologia do erro é a de sempre nesta lane: **medir um valor
observado e publicá-lo como propriedade do sistema, sem testar se ele se move.**

### E empacotar K registos por datagrama, no volume real (6500 × 80 B)

| K | mensagens | linux-x86_64 | macos-arm64 | windows-x86_64 |
|---|---|---|---|---|
| 1 | 6500 | 2,344 µs/registo | 4,676 µs/registo | 1,509 µs/registo |
| 4 | 1625 | 0,632 µs (3,71×) | 0,836 µs (5,59×) | 0,266 µs (5,68×) |
| 8 | 813 | 0,300 µs (7,82×) | 0,326 µs (14,33×) | 0,185 µs (8,15×) |
| 16 | 407 | 0,152 µs (15,38×) | 0,332 µs (14,11×) | 0,122 µs (12,34×) |
| 25 | 260 | **0,106 µs (22,13×)** | **0,228 µs (20,47×)** | **0,080 µs (18,78×)** |

As três plataformas concordam na ordem de grandeza — **~19× a ~22× a K=25** — e o Windows é o mais
rápido em absoluto no fim da tabela. O empacotamento não é uma optimização de uma plataforma.

**Este é o TETO do ganho, não o ganho**: não inclui a descarga forçada no ponto de captura. Mas dá a
ordem de grandeza — os 2,77× que o socket perdia para o anel viram vantagem já em K=4.

**Conclusão: o açúcar é construível nas três plataformas, e por dois caminhos em cada uma.**

| | fan-in por nome (1 fd no leitor) | N threads num descritor partilhado |
|---|---|---|
| Linux | `AF_UNIX SOCK_DGRAM` | `SOCK_STREAM`, 0 rasgados |
| macOS | `AF_UNIX SOCK_DGRAM` | `SOCK_STREAM`, 0 rasgados |
| Windows | mailslot | pipe nomeado, 0 rasgados |

E a linha que mais importa para a lei do dono — **`push` devolve `error | null`** — é a da queda
silenciosa. Nas três plataformas, **o que foi aceite foi entregue**: o transporte cheio **recusa com
erro** em vez de aceitar e perder. É isso que torna a lei imponível sobre este transporte, e não uma
promessa.

---

## 3. O que a medição desfez

**1. O macOS não tem `SOCK_SEQPACKET` em `AF_UNIX`.** `socketpair(AF_UNIX, SOCK_SEQPACKET)` →
`errno 43 Protocol not supported`. A forma que o Linux dá de graça **não existe nas três
plataformas**. O que sobrevive à interseção é o `SOCK_DGRAM`, que existe nos dois POSIX e preserva
fronteira nos dois — e é por isso que ele, e não o `SEQPACKET`, é a base do desenho portátil.

**2. O critério "o transporte não cria artefato no sistema de ficheiros" só o Linux satisfaz sem
truque.** O namespace abstrato (`sun_path[0] == '\0'`) é uma extensão do Linux: no macOS o mesmo
`bind` dá `errno 2`, e o `bind` com nome deixa sempre um inode de socket de 0 bytes. O Windows
satisfaz o critério por outra via — o `\\.\pipe\` vive no NPFS, que não é um volume.

**3. O buffer do macOS é 26× menor, e a relação concedido-vs-útil é outra.** O arquiteto mediu no
Linux `SO_SNDBUF` a reportar 212992 e o útil a ser 180224 (~15% abaixo) e propôs uma invariante
sobre essa diferença. **Ela é do Linux, não uma lei**: no macOS o reportado é 8192 e o útil é 8192 —
100%. Uma invariante que afirme "o útil fica abaixo do reportado" **falha no macOS por ser verdade a
menos**.

**4. "Várias instâncias" de um pipe nomeado não é difusão.** `nMaxInstances` serve N clientes, mas
**cada conexão é separada**: para alcançar todos escreve-se N vezes. É fan-out de *conexões*, não
difusão de *mensagens* — o mesmo que `listen`/`accept` faz no POSIX.

**5. O broadcast do Windows existe, mas é outra família.** É o **mailslot**: uma escrita em
`\\*\mailslot\nome` chega a todos os servidores com esse nome no domínio. É datagrama, e a mensagem
de broadcast **de rede** está limitada a **424 bytes** *(conhecimento, não medido — o runner é uma
máquina isolada)*. Não é um pipe com outra flag.

**6. E "não confiável" era adjetivo herdado.** Sob a carga que a corrida real tem — 4 escritores ×
500 registos — o mailslot **local** entregou 2000 de 2000, sem um único perdido e sem um único
corrompido. A ressalva de não-fiabilidade aplica-se à difusão de rede; localmente, e nesta medição,
ele não perdeu nada.

---

## 4. Como se declara a intenção — e a consequência de desenho

- **POSIX**: a direção é **retirada em execução**. Cria-se mão dupla e fecha-se um lado com
  `shutdown`. Medido nos dois POSIX: depois do `SHUT_WR` quem fechou recebe `EPIPE`, o par lê 0
  (EOF) **e continua a poder escrever de volta**, e o primeiro **continua a ler**.
- **Windows**: a direção é **declarada na criação** — `PIPE_ACCESS_DUPLEX`/`INBOUND`/`OUTBOUND`.
  Não há `shutdown` para handles de pipe; medido: um servidor `INBOUND` lê o que o cliente escreveu
  e a escrita de volta **falha**.
- **A consequência**: as duas exprimem a mesma intenção em **momentos diferentes**. Uma superfície
  de linguagem que queira as três formas tem de decidir se o modo é **parâmetro de criação** (e
  então o POSIX simula com um `shutdown` logo a seguir) ou **operação** (e então o Windows teria de
  o saber antes, e não pode). O primeiro caminho é implementável nas três; o segundo não é.

## 5. O que isto NÃO cobre

- **Passar descritor pelo canal**: `SCM_RIGHTS` medido nos dois POSIX; **sem equivalente** em
  Windows (`DuplicateHandle` exige o PID do destino — é outro mecanismo, e precisa de um canal
  prévio para trocar o PID).
- **Broadcast em rede**, fora da máquina: só o mailslot o tem, com o limite de 424 bytes.
- **O teto exato da mensagem de mailslot** (ver nota ¹).
- **N escritores em PROCESSOS separados**: as sondas usam N *threads* do mesmo processo. Para o
  `DGRAM` por nome e para o mailslot a diferença é nenhuma por construção (cada escritor abre o seu
  próprio descritor pelo nome), mas para o descritor **partilhado** é uma forma que não se testou —
  e nesse caso o descritor teria de ser herdado, o que é outro mecanismo.
- **Contenção com mais do que 4 escritores**, e sob carga sustentada. 4 × 500 é a ordem de grandeza
  de uma shard, não da corrida inteira (~6500).
