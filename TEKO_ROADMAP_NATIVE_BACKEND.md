# TEKO_ROADMAP_NATIVE_BACKEND — backend nativo próprio (C3)

> Detalha e substitui a linha **C3** de `TEKO_ROADMAP_INDEPENDENCE.md`
> ("Backend nativo próprio — emitir direto ao metal + linker próprio, aposentando o
> `cc` do host") com um plano de execução completo. `TEKO_ROADMAP_INDEPENDENCE.md`
> continua sendo o índice de eixos; este documento é a fonte de verdade do eixo C3.
>
> **Motivação:** `src/codegen/codegen.{c,tks}` hoje emite **texto C**, que o `cc`/`gcc`
> do host compila e linka. Isso é um degrau (M0), não o design final — todo `.c`
> gerado é intermediário descartável. Este roadmap substitui esse degrau por um
> backend que emite **bytes de objeto nativo diretamente** (ELF/Mach-O/COFF),
> mantendo `teko_rt` como runtime C pré-compilado ao qual o objeto gerado se linka.
>
> **Não aposenta o C imediatamente.** `tk_emit_c` continua sendo o backend padrão até
> o backend nativo alcançar paridade funcional + o portão de teste (D2–D4) verde nos
> mesmos termos do C. Os dois backends coexistem atrás de uma flag até então.
>
> vestígios, sem dead code"). O backend RISC-V inteiro (isel/minst/regalloc/encode/ABI/
> e foi apagado da matriz, das lanes e dos assets publicados. As tabelas abaixo já
> refletem a matriz PÓS-remoção.
>
> **WebAssembly saiu do escopo por ordem do dono (2026-07-30):** *"remova todo código
> e CI sobre wasm, é pra remover, não é mover, não KNOWN-STOP, quando chegar o momento
> reescrevemos certo e do zero."* Revoga o ruling de 2026-07-29 que pinava o buraco
> como KNOWN-STOP. O backend inteiro (`stackify.tks`, `objfile_wasm.tks`), os alvos
> `Wasm32Wasi`/`Wasm64Wasi`/`Wasm32Browser`, o pino de CI e os dois desenhos saíram da
> árvore. Este documento descreve a matriz PÓS-remoção, e não guarda o desenho antigo:
> quando o wasm voltar, será reescrito do zero.

---

## Matriz de alvos (M1 — completo, não fatiado por plataforma)

Direção do usuário: o milestone 1 é **completo** — cobre a matriz inteira, não um
alvo único primeiro. Os 4 primeiros alvos replicam exatamente o que já é validado em
CI (`.github/workflows/native.yml`):

| OS | Arch | Formato de objeto | ABI / calling convention | Linker do sistema (M1) |
|---|---|---|---|---|
| Linux | x86_64 | ELF64 | System V AMD64 | `cc`/`ld` (ou `lld`) |
| Linux | arm64 | ELF64 | AAPCS64 (ARM64 Linux) | `cc`/`ld` |
| macOS | arm64 (Apple Silicon) | Mach-O 64 | AAPCS64 (Apple variant — x18 reservado, args diferem levemente da AAPCS64 padrão) | `cc`/`ld` (Apple `ld64`) |
| Windows | x86_64 | PE/COFF (`.obj`) | Microsoft x64 calling convention (registradores diferentes do System V) | `clang`/`lld-link` (o projeto já usa Clang no Windows por causa do `__int128`) |

Cada linha acima é um **emissor de instrução + codificador de objeto** distinto — não
há atalho estrutural (arquiteturas divergem em registradores/ABI; formatos de objeto
divergem em layout de seções/relocations/símbolos). O plano organiza isso como
**um IR comum + N encoders finos**, não N implementações independentes do zero.

---

## Arquitetura: onde isso entra no pipeline existente

```
lexer → parser → checker (tk_tprogram / TAST)
                        │
                        ├── tk_emit_c        (backend ATUAL — texto C → cc)
                        │
                        └── tk_emit_native   (backend NOVO — bytes de objeto)
                                │
                                ├── lower: TAST → IR de baixo nível (tk_lir)
                                │
                                ├── [família registrador: x86_64/arm64]
                                │     ├── select: IR → instruções por-arquitetura
                                │     ├── encode: instruções → bytes + relocations
                                      └── objwrite: bytes → ELF64 | Mach-O64 | COFF
```

**Ponto-chave já confirmado (pesquisa prévia):** `tk_tprogram` (o TAST consumido por
`tk_emit_c`, `src/checker/tast.h`) já é **agnóstico de backend** — tipos resolvidos,
expressões tipadas, funções tipadas. `tk_emit_native` consome o **mesmo** `tk_tprogram`,
nunca o C gerado. Os dois backends são irmãos, não um em cima do outro.

### Camadas novas (nomes propostos, `src/codegen/native/`)

| Camada | Arquivo (par C+`.tks`, SUPREME RULE) | Responsabilidade |
|---|---|---|
| **LIR** | `lir.{c,h,tks}` | IR de baixo nível: operações de registrador virtual (SSA-lite: cada valor definido uma vez, sem phi — blocos básicos com args explícitos, mais simples que SSA pleno), sem detalhe de arquitetura. Tradução TAST→LIR reaproveita a MESMA lógica de "walk de nós" que `codegen.c` já tem para C, só troca o alvo de emissão de texto para nós de IR. |
| **Seleção de instrução** | `isel_x86_64.{c,h,tks}`, `isel_arm64.{c,h,tks}` | LIR → instruções concretas da arquitetura. Estratégia M1: **tiling por padrão de árvore** (tree-pattern matching simples, tipo o "maximal munch" clássico) — não um seletor otimizante (sem instruction scheduling elaborado); correção antes de performance, igual à filosofia do resto do compilador (honest-stop > esperteza). |
| **Alocação de registrador** | `regalloc.{c,h,tks}` | M1: **linear-scan simples** sobre os blocos básicos do LIR (não graph-coloring). Compartilhado entre as 3 arquiteturas (opera sobre registradores virtuais antes do isel mapear para físicos). |
| **Codificação binária** | `enc_x86_64.{c,h,tks}`, `enc_arm64.{c,h,tks}` | Instrução concreta → bytes de máquina. arm64 é RISC de largura fixa (4 bytes/instrução, encoder é uma tabela de bit-fields — mais simples). x86_64 é CISC de largura variável (encoder precisa dos prefixos REX/ModRM/SIB — mais trabalho, mas bem documentado). |
| **Escritor de objeto** | `obj_elf.{c,h,tks}`, `obj_macho.{c,h,tks}`, `obj_coff.{c,h,tks}` | Bytes de instrução + tabela de símbolos + relocations → arquivo `.o`/`.obj` no formato do alvo. Reaproveita a tabela de símbolos que **já existe** (`tk_emit_tsym` / E3) como base para a symbol table do objeto — mesma informação (símbolo mangled → nome Teko + file:line), dois consumidores. |
| **Driver** | `native_emit.{c,h,tks}` | Orquestra: escolhe alvo (host ou `--target`), roda lower→select→regalloc→encode→objwrite, invoca o linker do sistema (M1: `cc`/`ld`/`lld-link`) ou o linker próprio (M-linker, depois). |

Cada arquivo acima é um par `.c`/`.h` + `.tks` desde o commit inicial — nenhuma
camada nasce C-only (a lição do bug de `flags` self-host, PR #40, é exatamente essa:
C-only é dívida que se paga depois com juros).

---

## Modelo de relocation e linkage com `teko_rt`

`teko_rt.c` continua em C, compilado **uma vez por alvo** (`teko_rt.o` por
OS/arquitetura, artefato de build, não gerado por programa). O objeto nativo emitido
para o programa do usuário referencia símbolos de `teko_rt` (`tk_alloc`,
`tk_region_new`, `tk_panic_div0`, etc.) como **símbolos externos não resolvidos**
(`R_X86_64_PLT32`/`R_AARCH64_CALL26`/relocation equivalente em COFF) — exatamente
como `cc` já faz hoje ao linkar o `.c` gerado contra `teko_rt.o`. **Nenhuma mudança
na ABI de `teko_rt` é necessária** — o backend nativo só precisa gerar a mesma
sequência de chamada (calling convention do alvo) que o C já gera implicitamente.

Chamadas Teko-para-Teko (função definida no mesmo programa) usam o mesmo mangling
namespace-qualificado que já existe (`teko::checker::type_eq` →
`teko__checker__type_eq`, decisão já tomada e testada) — o backend nativo herda o
mangler, não reinventa.

---

## Linker: sistema agora, próprio depois (M-linker)

Confirmado pelo usuário: **M1 usa o linker do sistema** (`ld`/`ld64`/`lld-link`,
via `cc`/`clang` como driver de link, do jeito que o CI já invoca). O
backend nativo gera o `.o`/`.obj`; a etapa de link continua
terceirizada. Isso isola a parte mais arriscada (correção da geração de código) da
parte mais tediosa/menos urgente (reimplementar um linker).

**M-linker (linker próprio) é um eixo FUTURO, registrado aqui para não se perder:**

| # | Entrega | Depende de |
|---|---|---|
| L1 | Linker estático mínimo — resolve símbolos entre `programa.o` + `teko_rt.o`, aplica relocations, produz um executável ELF (Linux primeiro — formato mais simples de layout de segmentos). | M1 (ELF) fechado e estável |
| L2 | Extensão Mach-O (macOS) — segmentos `__TEXT`/`__DATA`, load commands, code signing ad-hoc (macOS moderno exige assinatura mesmo ad-hoc para rodar). | L1 |
| L3 | Extensão PE/COFF (Windows) — import tables (se algum dia houver DLL externa), seções, `.reloc`. | L1 |
| L4 | Link dinâmico (opcional, avaliar se vale a pena vs. só estático) — hoje o projeto já não depende de libs externas além de libc/libm; pode nunca ser necessário. | L1–L3, decisão a rever quando chegar |

**Diferido explicitamente** — não faz parte do M1 nem é bloqueante para ele.

---

## Milestones (M1 completo, conforme direção do usuário)

M1 **não** é um recorte tipo "só `exit(N)`" — é a cobertura funcional real que o
backend C já tem hoje (aritmética, chamadas, structs/variants, match, loops, closures,
generics monomorfizados, etc.), só que emitindo objeto nativo em vez de texto C, para
os 4 alvos nativos da matriz. Dado o tamanho, M1 se
divide em **sub-fases sequenciais** (cada
uma sozinha já testável ponta-a-ponta), não em "plataformas primeiro depois features":

| Sub-fase | Entrega | Critério de saída |
|---|---|---|
| **N1** | LIR + lower TAST→LIR (aritmética, `let`, `return`, chamada de função) para UM alvo de referência (Linux x86_64 — CI já builda nesse alvo nativamente, sem QEMU/cross, loop de iteração mais rápido). | `exit(42)`-equivalente E aritmética+chamada rodam via objeto nativo + `teko_rt` linkado pelo `cc` do sistema. |
| **N2** | Cobertura de tipos/controle de fluxo completa no alvo de referência: structs, variants/match, slices, strings, loops/`defer`, closures, generics monomorfizados — paridade de feature com `tk_emit_c` no mesmo alvo. | Suite de testes/regressão (`teko test .`) passa via binário nativo-x86_64-ELF, resultado idêntico ao backend C (mesma verificação de paridade diferencial já usada, estendida a "native-C==native-obj"). |
| **N3** | Replicar N1+N2 para **arm64 Linux** e **arm64 macOS** (mesma família AAPCS64, encoder compartilhável entre os dois com pequenas variações de ABI Apple). | Mesma suite verde nos 2 alvos arm64. |
| **N5** | **Windows x86_64** (COFF é o formato mais diferente; convenção de chamada Microsoft x64 diverge de System V mesmo na mesma arquitetura x86_64 — não é reuso do encoder Linux, é reuso da tabela de instruções x86_64 com uma convenção de argumentos diferente). | Suite verde no alvo Windows. |
| **N7** | CI: novo workflow (ou extensão do `native.yml`) roda a suite via o backend nativo em paralelo ao backend C, nos 4 alvos nativos — todos precisam concordar (diferencial de 2 vias: native-C == native-obj). | Gate CI verde, todos os motores concordando. |
| **N8** | Flag de seleção de backend exposta (`teko build --backend=c|native`, default a decidir — provavelmente `c` continua default até N7 estar maduro, depois inverte). Documentação. | `teko build` funcional com todos os backends, escolha explícita. |

Cada sub-fase N1–N8 vira, na hora
da execução, seu **próprio PR** — este documento
não implementa nada, só ordena o trabalho (mesmo padrão de `TEKO_ROADMAP_BINARY.md`/
`TEKO_ROADMAP_INDEPENDENCE.md`: plano primeiro, PRs de execução depois, um por vez,
com o portão de 4 passos de sempre).

---

## Verificação (portão, adaptado do padrão já em uso)

O portão existente (`teko-verify-both-with-test-gate`) verifica os dois binários
nativos (bootstrap C e self-hosted) concordarem. Com um terceiro motor, o portão
cresce para uma verificação de 3 vias em qualquer sub-fase N2+:

1. `./build/teko test .` (C nativo de bootstrap — motor de referência)
2. `./bin/teko test .` (self-hosted nativo, via backend C)
3. **novo:** `./build/teko test . --backend=native` (mesmo binário de bootstrap, mas
   emitindo objeto nativo em vez de C, para o alvo host)
4. Os três precisam concordar (mesmos testes passam, mesmos valores). Divergência
   entre (1)/(2) e (3) aponta bug no backend nativo, não regressão nos outros dois.

Alvos cross (arm64/Windows a partir de um host diferente) seguem o padrão
que o CI já usa: builda nativo no runner daquele OS/arch, não tenta emular
objeto-de-outro-alvo no host de dev.

---

## Riscos e decisões em aberto (para revisar quando a sub-fase relevante chegar)

- **PIC vs. não-PIC:** binários modernos (especialmente macOS/Windows) essencialmente
  exigem PIE/PIC. Decisão: **gerar sempre PIC** desde N1, não flertar com endereço
  absoluto (evita retrabalho quando N3+ chegar em alvos que não toleram não-PIC).
- **Debug info (DWARF/PDB):** fora de escopo do M1. O `.tsym` (E3) já resolve stack
  traces sem DWARF; gerar DWARF real fica para um eixo futuro (debugger real), citado
  aqui só para não se perder — não bloqueia M1.
- **Otimização:** M1 é correção, não performance — sem instruction scheduling, sem
  register allocation avançado, sem constant folding além do que o checker já faz.
  Otimização é trabalho pós-M1, deliberadamente adiado (mesma filosofia "DRY-LAST":
  não polir o que ainda pode mudar de forma).
- **`Ref<T>`/regions no backend nativo:** já sinalizado em `TEKO_MASTER_PLAN.md`
  linha 661 — as representações de `ptr<T>`/`Ref<T>`/arenas hoje são "moldadas pelo
  backend C"; podem precisar de ajuste de representação no backend nativo (ex.: um
  campo que hoje é um `struct` C pode virar um layout de bytes explícito). Revisar
  na sub-fase N2 (quando structs/regions entram em jogo), não antes.
- **Convergência com o backend C:** depois que N7 fecha (todos os motores concordando),
  decidir SE e QUANDO `tk_emit_c` é aposentado (item C4/self-hosting do
  `TEKO_ROADMAP_INDEPENDENCE.md` já previa "aposentar o C" como consequência do
  self-hosting — este documento não antecipa essa decisão, só a deixa mapeada).

---

## Relação com outros documentos

- **`TEKO_ROADMAP_INDEPENDENCE.md` (C3):** este documento é o detalhamento de C3;
  a linha C3 lá passa a apontar para cá.
- **`TEKO_MASTER_PLAN.md`:** tracked como **ROUND N** (independente/paralelo,
  não bloqueia nem é bloqueado pela sequência crítica GATE→ROUND 0→…→ROUND 6).
  Adicionado à lista de ROUNDs em 2026-07-01 depois de uma auditoria apontar que
  esse trabalho só existia como prosa solta (bucket `⏸️ DEFERRED`), desconectada do
  mecanismo de tracking real do projeto.
- **PR #39 (ROUND 2, OOP CLASS) já toca `src/codegen/*`** — correção: uma versão
  anterior deste plano afirmou erroneamente que #39 não tocava codegen. Na
  verdade #39 adicionou tratamento de `TK_BODY_CLASS` em `emit_type_decl`
  (layout de struct para CLASS, só modelo-de-dados). Não é conflito de design
  (é lógica aditiva num arquivo irmão de `src/codegen/native/*`, que ainda nem
  existe), mas quem começar a execução do ROUND N precisa rebasear por cima dos
  commits de codegen do ROUND 2, não assumir o arquivo intocado.
- **SUPREME RULE:** todo arquivo novo (`lir.c`, `isel_*.c`, `enc_*.c`, `obj_*.c`,
  `native_emit.c`) nasce com seu par `.tks` no mesmo commit/PR,
  sem exceção.
- **`.github/workflows/native.yml`:** a matriz de alvos já validada em CI é
  reaproveitada tal-qual para a matriz deste roadmap (linux-x86_64, linux-arm64,
  macos-arm64, windows-x86_64) — N7 estende os jobs existentes, não cria uma
  família nova.
