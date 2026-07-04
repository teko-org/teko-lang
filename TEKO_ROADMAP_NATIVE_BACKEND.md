# TEKO_ROADMAP_NATIVE_BACKEND — backend nativo próprio (C3)

> Detalha e substitui a linha **C3** de `TEKO_ROADMAP_INDEPENDENCE.md`
> ("Backend nativo próprio — emitir direto ao metal + linker próprio, aposentando o
> `cc` do host") com um plano de execução completo. `TEKO_ROADMAP_INDEPENDENCE.md`
> continua sendo o índice de eixos; este documento é a fonte de verdade do eixo C3.
>
> **Motivação:** `src/codegen/codegen.{c,tks}` hoje emite **texto C**, que o `cc`/`gcc`
> do host compila e linka. Isso é um degrau (M0), não o design final — todo `.c`
> gerado é intermediário descartável. Este roadmap substitui esse degrau por um
> backend que emite **bytes de objeto nativo diretamente** (ELF/Mach-O/COFF/**Wasm**),
> mantendo `teko_rt` como runtime C pré-compilado ao qual o objeto gerado se linka.
>
> **Inclui WebAssembly — os DOIS ambientes: WASI e Browser.** Adicionado ao escopo a
> pedido do usuário (2026-07-01; escopo estendido para os dois ambientes 2026-07-01
> no mesmo dia), não uma reformulação do plano: mesmo TAST de entrada, mesma ideia de
> "objeto + link contra `teko_rt`", só que o "objeto" é um módulo Wasm relocável e o
> "linker do sistema" é `wasm-ld` (parte do LLVM, que o projeto já usa via Clang). Ver
> seção dedicada abaixo — a arquitetura de isel/regalloc pensada para as arquiteturas
> de registrador NÃO se aplica 1:1 a Wasm (máquina de pilha), então Wasm tem sua
> própria camada de lowering, não reaproveita `isel_*`/`regalloc`/`enc_*`. **WASI e
> Browser reaproveitam o MESMO objeto Wasm do programa** (o código gerado pelo
> usuário não muda) — a diferença entre os dois é só qual variante de `teko_rt` ele
> linka (syscalls WASI reais vs. um glue JS fino), ver seção dedicada.
>
> **Não aposenta o C imediatamente.** `tk_emit_c` continua sendo o backend padrão até
> o backend nativo alcançar paridade funcional + o portão de teste (D2–D4) verde nos
> mesmos termos do C. Os dois backends coexistem atrás de uma flag até então.

---

## Matriz de alvos (M1 — completo, não fatiado por plataforma)

Direção do usuário: o milestone 1 é **completo** — cobre a matriz inteira, não um
alvo único primeiro. Os 6 primeiros alvos replicam exatamente o que já é validado em
CI (`.github/workflows/native.yml`); Wasm é novo e cobre **os dois ambientes** —
WASI e Browser — ambos ainda sem job de CI (ver "Relação com outros documentos"):

| OS | Arch | Formato de objeto | ABI / calling convention | Linker do sistema (M1) |
|---|---|---|---|---|
| Linux | x86_64 | ELF64 | System V AMD64 | `cc`/`ld` (ou `lld`) |
| Linux | arm64 | ELF64 | AAPCS64 (ARM64 Linux) | `cc`/`ld` |
| Linux | riscv64 | ELF64 | RISC-V LP64D calling convention | `cc`/`ld` (cross, já em CI via QEMU) |
| macOS | arm64 (Apple Silicon) | Mach-O 64 | AAPCS64 (Apple variant — x18 reservado, args diferem levemente da AAPCS64 padrão) | `cc`/`ld` (Apple `ld64`) |
| Windows | x86_64 | PE/COFF (`.obj`) | Microsoft x64 calling convention (registradores diferentes do System V) | `clang`/`lld-link` (o projeto já usa Clang no Windows por causa do `__int128`) |
| Windows | arm64 | PE/COFF (`.obj`) | ARM64 Windows calling convention (variante da AAPCS64) | `clang`/`lld-link` |
| **Wasm** (WASI) | wasm32 | **Wasm binário relocável** (`.o` no formato objeto do Wasm, spec da LLVM/`wasm-ld`) | Wasm não tem registradores — pilha de valores + locais indexados; "ABI" = convenção de import/export de função. Host FFI (`teko::env`/`io`/`process`/`fs`) resolve para **syscalls WASI reais**. | `wasm-ld` (parte do LLVM/Clang já usado no projeto) |
| **Wasm** (Browser) | wasm32 | **Mesmo binário Wasm relocável** que o alvo WASI (código do programa idêntico) | Mesma ABI Wasm; Host FFI resolve para um **glue JS fino** fornecido na instanciação (`WebAssembly.instantiate(module, { env: {...} })`), não para syscalls WASI. | `wasm-ld`, linkando contra uma variante de `teko_rt` compilada com imports JS em vez de imports WASI |

Cada linha acima é um **emissor de instrução + codificador de objeto** distinto — não
há atalho estrutural (arquiteturas divergem em registradores/ABI; formatos de objeto
divergem em layout de seções/relocations/símbolos). O plano organiza isso como
**um IR comum + N encoders finos**, não N implementações independentes do zero — com
Wasm como a exceção parcial: reaproveita o LIR e o `objwrite` (troca só o formato de
saída), mas NÃO reaproveita isel/regalloc/enc de registrador (ver seção Wasm abaixo).
WASI e Browser, por sua vez, reaproveitam o MESMO `stackify_wasm`/`obj_wasm` (o
código do programa é idêntico); só o `teko_rt` linkado muda.

### Wasm: dois sub-alvos, WASI e Browser (M1 cobre os dois)

**Decisão (2026-07-01, escopo estendido pelo usuário): M1 cobre AMBOS** —
`wasm32-wasi` e `wasm32` para browser. Não é um "depois" do outro; os dois entram
juntos no M1, como sub-fases irmãs (N6a/N6b), não sequenciais por prioridade.

- **WASI (`wasm32-wasi`):** `teko_rt` cross-compila com `clang --target=wasm32-wasi`
  (via `wasi-sdk`/sysroot WASI). As superfícies de host (`teko::env`, `teko::io`,
  `teko::process`, `teko::fs`) mapeiam DIRETO para syscalls WASI reais — paridade
  funcional total com os outros 6 alvos, sem redesenho.
- **Browser (`wasm32`, sem WASI):** o navegador não tem filesystem nem processos —
  não existe syscall equivalente. Em vez de inventar um filesystem virtual (escopo
  não pedido, complexidade desproporcional), o `teko_rt` para este alvo troca a
  IMPLEMENTAÇÃO das superfícies de host por um **glue JS fino** que o embedder
  (a página/app que instancia o módulo) fornece via o objeto de imports do
  `WebAssembly.instantiate`:
  - `teko::env::args`/`env vars` → o glue recebe um array/objeto JS na instanciação
    (o programa não lê `process.argv`, não existe; é injetado de fora).
  - `teko::io` (stdout/stderr) → glue mapeia para `console.log`/`console.error` (ou
    um callback customizado que o embedder passa, ex. escrever num `<textarea>`).
  - `teko::exit` → glue lança uma exceção JS capturável (não existe "processo" pra
    terminar) ou chama um callback `onExit(code)`.
  - `teko::fs`/`teko::process` (arquivo real, `exec`) → **honest-stop em runtime**
    (M.3): a chamada retorna `error` com mensagem clara ("fs/process not available
    in the browser target"), em vez de simular/mentir. Isso é uma decisão de
    LEGISLAÇÃO DE ESCOPO, não um bug — se um projeto real precisar de storage no
    browser, a superfície certa é uma FUTURA API dedicada (ex. IndexedDB-backed),
    não fingir que é o `fs` de sempre.

**Ambos M1** — nenhum dos dois fica "registrado como variante futura"; essa era a
posição anterior deste documento (WASI-only), revista pelo usuário no mesmo dia.

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
                                ├── [família registrador: x86_64/arm64/riscv64]
                                │     ├── select: IR → instruções por-arquitetura
                                │     ├── encode: instruções → bytes + relocations
                                │     └── objwrite: bytes → ELF64 | Mach-O64 | COFF
                                │
                                └── [família pilha: Wasm — WASI e Browser]
                                      ├── stackify: IR → sequência de opcodes Wasm
                                      │   (cada valor LIR vira um local Wasm; sem
                                      │   regalloc — Wasm não tem registrador físico
                                      │   para alocar, só locais indexados)
                                      └── wasmwrite: opcodes → módulo Wasm relocável
                                          (seções type/import/function/code/…)
                                          — MESMO módulo para WASI e Browser; só o
                                          teko_rt.wasm linkado no fim difere.
```

**Ponto-chave já confirmado (pesquisa prévia):** `tk_tprogram` (o TAST consumido por
`tk_emit_c`, `src/checker/tast.h`) já é **agnóstico de backend** — tipos resolvidos,
expressões tipadas, funções tipadas. `tk_emit_native` consome o **mesmo** `tk_tprogram`,
nunca o C gerado. Os dois backends são irmãos, não um em cima do outro.

### Camadas novas (nomes propostos, `src/codegen/native/`)

| Camada | Arquivo (par C+`.tks`, SUPREME RULE) | Responsabilidade |
|---|---|---|
| **LIR** | `lir.{c,h,tks}` | IR de baixo nível: operações de registrador virtual (SSA-lite: cada valor definido uma vez, sem phi — blocos básicos com args explícitos, mais simples que SSA pleno), sem detalhe de arquitetura. Tradução TAST→LIR reaproveita a MESMA lógica de "walk de nós" que `codegen.c` já tem para C, só troca o alvo de emissão de texto para nós de IR. |
| **Seleção de instrução** | `isel_x86_64.{c,h,tks}`, `isel_arm64.{c,h,tks}`, `isel_riscv64.{c,h,tks}` | LIR → instruções concretas da arquitetura. Estratégia M1: **tiling por padrão de árvore** (tree-pattern matching simples, tipo o "maximal munch" clássico) — não um seletor otimizante (sem instruction scheduling elaborado); correção antes de performance, igual à filosofia do resto do compilador (honest-stop > esperteza). |
| **Alocação de registrador** | `regalloc.{c,h,tks}` | M1: **linear-scan simples** sobre os blocos básicos do LIR (não graph-coloring). Compartilhado entre as 3 arquiteturas (opera sobre registradores virtuais antes do isel mapear para físicos). |
| **Codificação binária** | `enc_x86_64.{c,h,tks}`, `enc_arm64.{c,h,tks}`, `enc_riscv64.{c,h,tks}` | Instrução concreta → bytes de máquina. arm64/riscv64 são RISC de largura fixa (4 bytes/instrução, encoder é uma tabela de bit-fields — mais simples). x86_64 é CISC de largura variável (encoder precisa dos prefixos REX/ModRM/SIB — mais trabalho, mas bem documentado). |
| **Escritor de objeto** | `obj_elf.{c,h,tks}`, `obj_macho.{c,h,tks}`, `obj_coff.{c,h,tks}` | Bytes de instrução + tabela de símbolos + relocations → arquivo `.o`/`.obj` no formato do alvo. Reaproveita a tabela de símbolos que **já existe** (`tk_emit_tsym` / E3) como base para a symbol table do objeto — mesma informação (símbolo mangled → nome Teko + file:line), dois consumidores. |
| **Stackify (Wasm)** | `stackify_wasm.{c,h,tks}` | LIR → sequência de opcodes Wasm por função (cada valor LIR = um local `.wasm`; expressões viram sequências `local.get`/opcode/`local.set` na ordem de avaliação — sem seleção de instrução por padrão de árvore nem alocação de registrador, porque Wasm não tem nenhum dos dois). Camada PRÓPRIA, não reaproveita `isel_*`/`regalloc`. |
| **Escritor de módulo Wasm** | `obj_wasm.{c,h,tks}` | Opcodes + tabela de símbolos → módulo Wasm binário relocável (seções `type`/`import`/`function`/`code`/`linking`/`reloc.CODE`, formato objeto do LLVM para Wasm — o que `wasm-ld` consome). Mesma tabela de símbolos de `tk_emit_tsym`, mesmo padrão dos outros `obj_*`. |
| **Driver** | `native_emit.{c,h,tks}` | Orquestra: escolhe alvo (host ou `--target`, incluindo `wasm32-wasi` e `wasm32` browser), roda lower→(select→regalloc→encode→objwrite \| stackify→obj_wasm) conforme a família do alvo, invoca o linker do sistema (M1: `cc`/`ld`/`lld-link`/`wasm-ld`) ou o linker próprio (M-linker, depois). Para `wasm32`, escolhe qual variante de `teko_rt.wasm` linkar (WASI ou glue-JS) — o objeto do programa é o mesmo nos dois casos. |
| **Glue JS (Browser)** | `teko_rt_browser_glue.js` (fora de `src/codegen/native/`; artefato de runtime, não compilador) | Objeto de imports JS que implementa a metade das superfícies de host que fazem sentido no browser (`env`/`io`/`exit`) e honestamente falha (`error` em runtime) para `fs`/`process`. Fornecido ao embedder como um arquivo separado ao lado do `.wasm`, não gerado pelo compilador — é runtime, não codegen. |

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

**Wasm é o mesmo modelo, com uma peça a mais.** Para `wasm32-wasi`, `teko_rt.c`
compila via `clang --target=wasm32-wasi` (usando o `wasi-sdk`, ou o sysroot WASI
que já acompanha builds recentes do Clang) para `teko_rt.wasm` (módulo objeto, não
executável), e chamadas para `tk_alloc`/`tk_region_new`/etc. viram **imports** do
módulo do programa, resolvidos por `wasm-ld` no link — o mesmo papel que o símbolo
externo não resolvido faz em ELF/Mach-O/COFF, só que o mecanismo Wasm pra isso
(import/export declarado na seção `import`) é nativo do formato, não uma
convenção de relocation solta.

**Para o alvo Browser, `teko_rt` compila para `wasm32` SEM WASI** — as funções de
alocação/arena (`tk_alloc`, `tk_region_*`) continuam puro C portável (não dependem
de syscall nenhuma, então compilam igual nos dois sub-alvos), mas as funções de host
FFI (`teko::env`/`io`/`process`/`fs`) trocam de implementação: em vez de chamar uma
syscall WASI, chamam um import JS (`extern` declarado no C do runtime, resolvido
pelo objeto de imports que o embedder passa em `WebAssembly.instantiate`). O link
final ainda passa por `wasm-ld`; a única diferença de linkagem é QUAL variante de
`teko_rt.wasm` entra no link — o objeto do PROGRAMA do usuário (gerado pelo
compilador) é idêntico nos dois casos.

---

## Linker: sistema agora, próprio depois (M-linker)

Confirmado pelo usuário: **M1 usa o linker do sistema** (`ld`/`ld64`/`lld-link`/
`wasm-ld`, via `cc`/`clang` como driver de link, do jeito que o CI já invoca). O
backend nativo gera o `.o`/`.obj`/módulo-Wasm-objeto; a etapa de link continua
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

**Wasm fica de fora do M-linker.** O formato de módulo Wasm já é pensado para ser
simples de linkar (sem os detalhes históricos de ELF/Mach-O/COFF) e `wasm-ld` é
parte do mesmo LLVM que o projeto já depende via Clang — não há motivação para
reimplementar isso. L1–L4 cobrem só a família ELF/Mach-O/COFF.

---

## Milestones (M1 completo, conforme direção do usuário)

M1 **não** é um recorte tipo "só `exit(N)`" — é a cobertura funcional real que o
backend C já tem hoje (aritmética, chamadas, structs/variants, match, loops, closures,
generics monomorfizados, etc.), só que emitindo objeto nativo em vez de texto C, para
os 8 alvos da matriz (6 nativos + Wasm/WASI + Wasm/Browser). Dado o tamanho, M1 se
divide em **sub-fases sequenciais** (cada
uma sozinha já testável ponta-a-ponta), não em "plataformas primeiro depois features":

| Sub-fase | Entrega | Critério de saída |
|---|---|---|
| **N1** | LIR + lower TAST→LIR (aritmética, `let`, `return`, chamada de função) para UM alvo de referência (Linux x86_64 — CI já builda nesse alvo nativamente, sem QEMU/cross, loop de iteração mais rápido). | `exit(42)`-equivalente E aritmética+chamada rodam via objeto nativo + `teko_rt` linkado pelo `cc` do sistema. |
| **N2** | Cobertura de tipos/controle de fluxo completa no alvo de referência: structs, variants/match, slices, strings, loops/`defer`, closures, generics monomorfizados — paridade de feature com `tk_emit_c` no mesmo alvo. | Suite de testes/regressão (`teko test .`) passa via binário nativo-x86_64-ELF, resultado idêntico ao backend C (mesma verificação "VM==native" já usada, estendida a "native-C==native-obj"). |
| **N3** | Replicar N1+N2 para **arm64 Linux** e **arm64 macOS** (mesma família AAPCS64, encoder compartilhável entre os dois com pequenas variações de ABI Apple). | Mesma suite verde nos 2 alvos arm64. |
| **N4** | **riscv64 Linux** (isel/encoder novo — RISC-V é a arquitetura mais distante das outras duas). | Suite verde via QEMU, mesmo padrão que o CI já usa para o backend C. |
| **N5** | **Windows x86_64 + arm64** (COFF é o formato mais diferente; convenção de chamada Microsoft x64 diverge de System V mesmo na mesma arquitetura x86_64 — não é reuso do encoder Linux, é reuso da tabela de instruções x86_64 com uma convenção de argumentos diferente). | Suite verde nos 2 alvos Windows. |
| **N6a** | **Wasm/WASI** — camada própria (`stackify_wasm`/`obj_wasm`, não reaproveita isel/regalloc/enc dos alvos de registrador); `teko_rt` cross-compilado para `wasm32-wasi`; link via `wasm-ld`. | Suite roda via `wasmtime`/`wasmer` (runtime WASI de referência para CI) sobre o módulo `.wasm` gerado, resultado idêntico aos outros alvos. |
| **N6b** | **Wasm/Browser** — reaproveita `stackify_wasm`/`obj_wasm` de N6a (mesmo objeto de programa); nova variante de `teko_rt` para `wasm32` com imports JS em vez de WASI (`env`/`io`/`exit` implementados via glue, `fs`/`process` honest-stop em runtime); escreve `teko_rt_browser_glue.js` de referência. | Suite roda contra o módulo `.wasm` (o MESMO artefato de N6a) instanciado com o glue JS, num runtime JS de CI (ver "Relação com outros documentos" — decisão de qual runtime JS ainda em aberto: Node headless vs. browser real via Playwright); resultado idêntico aos outros alvos nos testes que não usam `fs`/`process`; testes que usam `fs`/`process` verificam o `error` honesto em vez de rodar. |
| **N7** | CI: novo workflow (ou extensão do `native.yml`) roda a suite via o backend nativo em paralelo ao backend C, nos 8 alvos (6 nativos + Wasm/WASI + Wasm/Browser) — todos precisam concordar (diferencial de 3 vias: VM == native-C == native-obj, os dois sub-alvos Wasm inclusos). | Gate CI verde, todos os motores concordando. |
| **N8** | Flag de seleção de backend exposta (`teko build --backend=c|native|wasm-wasi|wasm-browser`, default a decidir — provavelmente `c` continua default até N7 estar maduro, depois inverte). Documentação. | `teko build` funcional com todos os backends, escolha explícita. |

Cada sub-fase N1–N8 (com N6 sendo duas sub-sub-fases irmãs, N6a/N6b) vira, na hora
da execução, seu **próprio PR** — este documento
não implementa nada, só ordena o trabalho (mesmo padrão de `TEKO_ROADMAP_BINARY.md`/
`TEKO_ROADMAP_INDEPENDENCE.md`: plano primeiro, PRs de execução depois, um por vez,
com o portão de 4 passos de sempre).

---

## Verificação (portão, adaptado do padrão já em uso)

O portão de 4 passos existente (`teko-verify-both-with-test-gate`) verifica
VM == native-C. Com um terceiro motor, o portão cresce para uma verificação de
3 vias em qualquer sub-fase N2+:

1. `./build/teko test .` (C nativo de bootstrap — motor de referência)
2. `./bin/teko test .` (VM auto-hospedado)
3. **novo:** `./build/teko test . --backend=native` (mesmo binário de bootstrap, mas
   emitindo objeto nativo em vez de C, para o alvo host)
4. Os três precisam concordar (mesmos testes passam, mesmos valores). Divergência
   entre (1)/(2) e (3) aponta bug no backend nativo, não regressão nos outros dois.

Alvos cross (arm64/riscv64/Windows a partir de um host diferente) seguem o padrão
que o CI já usa: builda nativo no runner daquele OS/arch (ou cross+QEMU para
riscv64), não tenta emular objeto-de-outro-alvo no host de dev. **Wasm (os dois
sub-alvos) roda em qualquer host** (não precisa de runner específico nem QEMU —
`wasmtime`/`wasmer`/Node existem para os 3 OSes), o que os torna, na prática, os
alvos mais baratos de validar em CI apesar de terem a camada de codegen mais
diferente das outras.

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
- **Runtime JS de referência para CI do alvo Browser (EM ABERTO):** rodar contra um
  browser real (Playwright/Puppeteer headless) dá o sinal mais fiel mas é pesado
  (download de browser no runner); rodar contra Node.js (que também executa Wasm e
  aceita o mesmo objeto de imports JS) é mais barato e cobre a MESMA superfície de
  glue, mas não pega bugs específicos de engine de browser (V8 vs. outros). Decisão:
  revisar quando N6b/N7 chegarem — plausível começar com Node (barato) + um
  smoke-test manual em browser real antes de cada release, sem bloquear CI nisso.
- **Superfície `fs`/`process` no Browser — honest-stop, não simulação:** decisão já
  tomada (ver "Wasm: dois sub-alvos" acima) — NÃO construir um filesystem virtual
  agora. Se surgir demanda real, é uma superfície NOVA (`teko::storage` ou similar,
  IndexedDB-backed), não um remendo em cima do `fs` existente — mesma filosofia do
  resto do projeto (superfícies novas em vez de sobrecarregar uma existente com
  semântica diferente por alvo).
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
  `stackify_wasm.c`, `native_emit.c`) nasce com seu par `.tks` no mesmo commit/PR,
  sem exceção.
- **`.github/workflows/native.yml`:** a matriz de 6 alvos já validada em CI é
  reaproveitada tal-qual para a matriz deste roadmap (linux-x86_64, linux-arm64,
  linux-riscv64/qemu, macos-arm64, windows-x86_64, windows-arm64). **Os 2 sub-alvos
  Wasm (WASI e Browser) são os únicos dos 8 SEM job de CI hoje** — N7 precisa criar
  jobs novos, não estender um existente: WASI precisa `wasi-sdk`/sysroot WASI +
  `wasmtime` no runner; Browser precisa do glue JS + um runtime JS (Node como
  primeira opção barata — ver "Riscos e decisões em aberto").
