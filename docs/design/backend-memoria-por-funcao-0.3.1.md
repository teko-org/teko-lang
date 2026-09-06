# Escopar a memória do backend nativo por função — 0.3.1

Arquiteto, 2026-08-02. Ramo `cargo/0.3.1.0-backend-memoria-arq` (de `origin/fix/union`).
Documento de DESENHO — nenhuma linha de produto aqui. `bootstrap/teko.c` é SAÍDA.

Regra do dono honrada em todo o documento: **proposta, não contra-argumento; alarme só se provado
com arquivo:linha.**

---

## 0. A correção de enquadramento — o pipeline é MÓDULO-a-módulo, não função-a-função

A tese do issue enuncia o pipeline como *"`lower_function` -> `select` (MInst): `regalloc` ->
`encode` -> anexa bytes ao objeto"*, função a função. **Medi a estrutura real e ela não é essa** — e
essa diferença é o eixo de todo o desenho, por isso vem primeiro e com arquivo:linha.

O topo do backend nativo roda em **três passes de MÓDULO INTEIRO, em série**, cada um com um laço
interno por função (`src/build/project.tks:2640-2646`, a cauda `emit_native_x86`):

```
let sel = select_module_x86(SYSV64, entry)      // TODAS as funções -> MInst
let col = regalloc_module_x86(SYSV64, sel)       // TODAS as funções coloridas
let enc = encode_module_x86(SYSV64, col)         // TODAS as funções -> bytes
```

- `select_module_x86` (`src/backend/isel_x86_64.tks:1893-1903`) acumula `out.funcs` com **todas** as
  `MFuncX86` antes de devolver.
- `regalloc_module_x86` (`src/backend/regalloc_x86.tks:894-907`) constrói uma **segunda** lista com
  todas as funções coloridas.
- `encode_module_x86` (`src/backend/encode_x86_64.tks:2474-2482`) só então dobra tudo em bytes.

**Consequência medida:** o MInst de TODAS as funções fica vivo ao mesmo tempo — de facto o pico tem o
LModule (todo o LIR, de `lower_program`) MAIS a `MModuleX86` não-colorida MAIS a `MModuleX86`
colorida coexistindo. A fronteira que a tese usa — *"o LIR/MInst de A morre depois que os bytes de A
são anexados"* — **não existe na estrutura de hoje**; ela precisa ser CRIADA fundindo os três passes
num laço por função. Os drivers arm64 são espelho exacto (`select_module`/`regalloc_module`/
`encode_module` em `isel_arm64.tks:1951`, `regalloc.tks:2044`, `encode_arm64.tks:2922`).

Isto **não enfraquece** a tese — pelo contrário: o issue está certo que a fronteira é segura por
estrutura; ela só ainda não está lá. O trabalho é materializá-la. E confirma o diagnóstico do issue:
`grep region_new/region_drop src/backend` = vazio (as menções em `lower.tks:3392-3437` são código
GERADO para o programa compilado, não o backend a escopar o SEU próprio scratch).

---

## 1. A fronteira — scratch POR-FUNÇÃO vs persistente ENTRE-FUNÇÕES

### 1a. Grupo A — SCRATCH por-função (escopado-e-largado; morre quando os bytes de A entram no objeto)

| estrutura | onde nasce (arquivo:linha) | o que é |
|---|---|---|
| LIR do corpo da função (`LFunc.blocks`/`insts`) | `src/lir/lower.tks:12819` (`lower_function`), tipo `src/lir/lir.tks:219` | blocos + instruções LIR de UMA função |
| `SelCtxX86` (mirror blocks + `vinfo`) | `src/backend/isel_x86_64.tks:1875` | esqueleto de blocos-espelho, side-table `class_of` por VReg, `next_scratch` |
| `MFuncX86` intermédia (isel) | `isel_x86_64.tks:1874-1878` (`select_lfunc_x86`) | MInst virtual de uma função — hoje SOBREVIVE (vai para `out.funcs`); no desenho fundido vira scratch |
| `order` (RPO `[]u32`) | `src/backend/regalloc_x86.tks:874` | ordem RPO dos blocos |
| `numbered` (`[]NumberedInstX86`) | `regalloc_x86.tks:875` | o stream numerado INTEIRO da função |
| `IntervalSet` (virtual + fixed) | `regalloc_x86.tks:876` | intervalos de vida — tipicamente o maior scratch de regalloc |
| resultado do `linear_scan` (mapa de alocação) | `regalloc_x86.tks:880` | atribuição VReg→físico |
| buffers de codificação por-instrução | `src/backend/encode_x86_64.tks:2357` (`fold_func_into_text_x86`) | scratch antes dos bytes irem para `acc.text` |

Tudo isto é derivado do LIR de UMA função e nada externo o referencia depois que os bytes daquela
função entram no buffer do objeto. É o Grupo A que é escopado-e-largado.

### 1b. Grupo B — PERSISTENTE entre-funções (fica na raiz/objeto; NUNCA escopado)

| estrutura | onde vive (arquivo:linha) | por que persiste |
|---|---|---|
| `prog` (programa tipado) | entrada de `emit_native` (`project.tks:2557`) | lido por todas as funções + pelo `.tsym` no fim |
| tabelas globais de lowering: `table`/`enums`/`layouts`/`externs`/`variants`/`ref_fns`/`field_decls` | `src/lir/lower.tks:12998-13005` | setup ÚNICO de `lower_program`, partilhado por todas as funções |
| `LModule.rodata`/`globals`/`layouts` | `src/lir/lir.tks:320`; carregados por-referência através dos 3 passes (`isel_x86_64.tks:1894`, `minst_x86.tks:810-820`) | o encoder emite os bytes deles; isel/regalloc nunca os tocam |
| tabela de símbolos do módulo (`[]Symbol`, um global por função) | `encode_x86_64.tks:2478-2480` | acumula ao longo de todas as funções |
| relocações do módulo (`[]RelocX86`) | `encode_x86_64.tks:2481`, `collect_undefined_x86:2400` | acumulam, re-baseadas pelo `.text` base |
| buffer de texto do objeto (bytes concatenados) | `ModuleTextX86.text` (`encode_x86_64.tks:2377-2387`) | É O ARTEFATO — cresce a cada função |
| bytes finais do objeto (`emit_elf`/`emit_coff`/`emit_macho`) e o `.tsym` | `project.tks:2645`, `2738` | o produto escrito no `.o` |

**A regra da fronteira:** só o Grupo A é escopado-e-largado. O Grupo B é a raiz/objeto e nele os
apêndices por-função (bytes, `Symbol`, `RelocX86`) são COPIADOS por valor.

---

## 2. A prova de não-escape (por estrutura), com as exceções NOMEADAS

Afirmação: no laço fundido, ao largar a região de scratch da função A, **nenhum ponteiro vivo aponta
para dentro dela.** O único output que persiste são os BYTES de A, e eles vão para o buffer do objeto
(Grupo B) por CÓPIA. Prova por estrutura, output a output do `encode` de A:

1. **`.text` bytes de A** — `fold_func_into_text_x86` (`encode_x86_64.tks:2357-2387`) faz
   `acc = fold_func_into_text_x86(acc, f)`; o `acc.text` é o buffer do objeto (Grupo B). O append é
   uma cópia byte-a-byte para o buffer do destino. Depois da cópia, nada em `acc` aponta para o
   scratch de A. ✔ não escapa.
2. **`Symbol` da função A** (`encode_x86_64.tks:2478`) — struct `{ name: str; defined; sect; offset;
   local }`. Os campos escalares copiam por valor. O ÚNICO ponteiro é `name: str`. **Exceção a
   verificar e nomear:** `name` tem de originar em `prog`/tabelas globais (Grupo B, raiz), NÃO ser
   re-internado no scratch de A. Origem: `MFuncX86.symbol` (`minst_x86.tks:784`), que vem de
   `LFunc.symbol` (`lir.tks:219`), mangado em `lower.tks` a partir de `prog` (raiz). Enquanto o
   símbolo não for re-alocado dentro da região de scratch, `name` aponta para a raiz e **sobrevive ao
   drop**. ✔ (é a invariante que o crumb de wiring tem de PRESERVAR — ver §5-C-guard).
3. **`RelocX86` de A** (`encode_x86_64.tks:2405-2408`, `2430`) — `{ offset; sym: str; kind; addend;
   sect }`. Mesmo caso do `Symbol`: o único ponteiro é `sym: str`, cuja origem é o nome-alvo da
   chamada, vindo do LIR/prog (raiz). ✔ com a MESMA invariante.

**As exceções nomeadas** (as estruturas por-função que SÃO referenciadas depois e por isso NÃO podem
ser escopadas, ou têm de ser copiadas antes do drop):

- **E1 — `Symbol.name` / `RelocX86.sym` (`str`)**: os bytes do `str` do NOME. Ficam na raiz por
  origem; a garantia é *não re-internar nomes dentro da região de scratch*. Se algum passo de
  encode/isel construir um nome NOVO (ex.: um símbolo de bloco-de-split, um thunk) via `str::concat`
  DENTRO da janela escopada, esse `str` cai no scratch e o `Symbol`/`Reloc` que o referencia fica
  pendurado. Mitigação: a cópia dos outputs para o Grupo B acontece ANTES do drop (a janela ainda
  viva), então basta que o append para o Grupo B force uma cópia do `str` para a raiz — que é
  exactamente o que `tk_slice_push` para um buffer raiz-residente já faz para os bytes; para os
  `str` de nome, o append da lista de símbolos tem de residir na raiz (Grupo B).
- **E2 — `rodata`/`globals`/`layouts` carregados por-referência** (`minst_x86.tks:810-820`): NUNCA
  entram na região de scratch — são Grupo B desde `lower_program`. Um refactor que os copiasse para
  dentro do laço quebraria isto; ficam explicitamente FORA da janela.
- **E3 — inlining/lifting cross-função**: `lower_program` já resolve `lifted` funcs e
  `fold_lifted_funcs` ANTES do backend (`lower.tks:13018-13019`), no LModule persistente. Nenhuma
  função referencia o LIR de OUTRA no backend. Se um passo futuro de inline cross-função no backend
  passar a indexar o MInst de outra função, ESSA tabela sobe para o Grupo B. Hoje não existe. ✔

Nada aqui relaxa análise de escape geral. A garantia é a fronteira de fase: o output de A é copiado
para outra região antes de A ser largada. O alarme do dono (relaxar escape = UAF) não se aplica.

---

## 3. O mecanismo — onde abrir, onde largar, reusando o que existe

### 3a. O achado que decide o mecanismo (arquivo:linha)

`tk_alloc` — o alocador default por trás de `list::push` e toda alocação implícita — bump-aloca
**sempre** de `tk_region_root()`, a região-raiz da task corrente (`src/runtime/teko_rt.h:118-125`).
**Não há "região corrente" trocável.** Regiões-filhas existem (`tk_region_new(parent)`/`tk_region_drop`,
`teko_rt.h:149-152`, com listas de chunks SEPARADAS) mas o alocador default NUNCA as mira — só código
gerado com `tk_region_alloc(r, n)` explícito o faz.

O único mecanismo por-escopo ligado ao caminho default é `tk_arena_push`/`tk_arena_pop`
(`teko_rt.h:178-184`), uma **pilha de marcas de bump na única raiz** — e é ele que já está exposto ao
Teko como builtin (`src/checker/scope.tks:740-742`; mapeado em `src/lir/lower.tks:3981-3982`) e cujos
únicos chamadores hoje são o portão de teste (`#109`).

**A armadilha do `arena_push/pop` para este caso** (medida, não suposta): a marca de bump é uma
PILHA linear na raiz. Tudo alocado depois da marca é libertado no `pop`, INCLUINDO um acumulador que
tenha crescido nessa janela. No laço fundido o acumulador (bytes/símbolos/relocs do objeto) cresce a
CADA iteração; se ele crescer dentro da janela `push…pop`, o `pop` liberta-o junto com o scratch. É a
mesma verdade que o dono estabeleceu em `docs/medicoes/onde-a-limpeza-por-escopo-falha.md`
(*"out pertence à arena da função"*), aqui invertida: não dá para largar scratch intercalado com um
acumulador na MESMA região linear.

### 3b. A primitiva recomendada — a REGIÃO-CORRENTE trocável (região-filha para scratch)

Como o scratch do backend são milhares de `list::push` implícitos (não roteados), rotear cada sítio
explicitamente (estilo `RegionFrame` do codegen C) é inviável. A resposta é uma **pilha de
região-corrente** no runtime, para que o `tk_alloc` default mire a região-filha entrada:

- `tk_region_enter(child)` — empilha `child` como região-corrente; `tk_alloc` passa a bump-alocar
  nela.
- `tk_region_leave()` — desempilha; `tk_alloc` volta à raiz.
- scratch da função → região-filha (por default, sem rotear nada); acumulador do objeto → raiz
  (append acontece DEPOIS do `leave`, copiando os outputs da filha ainda-viva para a raiz);
  `tk_region_drop(child)` liberta LIR+MInst+intervalos+encode-scratch de uma vez.

Isto **reusa** `tk_region_new`/`tk_region_drop` já existentes (`teko_rt.h:149-152`, listas de chunks
separadas — nada de armadilha de bump intercalado) e materializa o modelo R11 (*"arenas lexicais"*).
A ÚNICA adição de runtime é o par `enter/leave` (um ponteiro thread-local de região-corrente que o
`tk_alloc` lê no lugar de `tk_region_root()` fixo). `teko_rt.{c,h}` é C MANTIDA (exceção explícita ao
congelamento) — permitido. A exposição ao Teko é trabalho `.tks` (builtins em `scope.tks` +
mapeamento em `lower.tks`, espelhando `arena_push`).

### 3c. Alternativa (se a equipa preferir zero-conceito-novo de runtime)

`arena_push`/`arena_pop` (JÁ builtins, JÁ com chamador provado no portão de teste) para o scratch, com
o acumulador do objeto residente na **região de PROGRAMA** (`tk_region_program()`, `teko_rt.h:170-175`
— chunks separados, sobrevive a `arena_pop`). Custo: precisa de um push roteado-para-programa do
acumulador (novo binding), e o modelo mental é mais subtil (duas regiões com semânticas diferentes).
Menos limpo que 3b; fica registado como plano-B ratificável.

**Recomendação law-first:** 3b (região-corrente + filha). É o que casa com R11, reusa as filhas já
construídas, evita a armadilha de bump, e deixa o acumulador do objeto SEM roteamento especial.

### 3d. Onde abrir/largar — o laço fundido (a estrutura-alvo)

A fusão vive em `project.tks` (as caudas `emit_native_*`), chamando as `pub fn` por-função que já
existem, para NÃO editar os ficheiros de colisão quente. Abre a região no topo da iteração da função
A; larga no fim, depois de copiar os outputs de A para os acumuladores do Grupo B.

---

## 4. A medição e o seu constrangimento (>16 GB) — e o portão em gen2 NATIVE

**Constrangimento duro:** o build NATIVO inteiro OOMa a ~15,8 GB num box de 16 GB. Provar o pico
antes/depois do compilador-inteiro EXIGE hardware >16 GB — nenhuma engenharia de medição contorna
isso para o corpus completo. Digo-o explicitamente: **o número "pico do self-build nativo inteiro
antes vs depois" só fecha num box >16 GB.**

O que É mensurável sem esse box, e prova a direcção e a ordem-de-grandeza:

- **(a) Função isolada / projeto pequeno, backend nativo.** Compilar um `.tks` de N funções sob
  `TEKO_BACKEND=native` com `TEKO_ARENA_OBS=<path>` (o instrumento já existe no runtime, usado em
  `docs/medicoes/onde-esta-a-memoria-do-compilador.md`). Antes: `reclaimed 0.0%`, tudo na raiz.
  Depois: o scratch de cada função aparece em "scoped (freed at region drop)" e a taxa de recuperação
  sobe. Métrica de aceitação: **N regiões largadas ≈ N funções** (hoje 11 de 5007) e "scoped" > 0.
- **(b) Pico por-função vs pico-de-módulo, no mesmo projeto pequeno.** Como o pico de hoje é
  `LIR + 2×MModule` coexistentes (§0), a fusão baixa o termo dominante para
  `LIR + 1×(MFunc corrente)`. Instrumentar o `CHUNKS` do `TEKO_ARENA_OBS` (cap malloc'd) antes/depois
  mostra a queda mesmo num projeto que não OOMa.
- **(c) Rota C, onde os intermédios são menores.** A rota C não exercita o backend nativo, então NÃO
  valida este fix (ver §6-impacto). Serve só de baseline de que o resto do pipeline não regrediu.
- **(d) O que só um box >16 GB fecha:** o pico absoluto do self-build nativo completo (gen2-native a
  compilar `src/`), e portanto a prova final de "deixou de OOMar". Nomeado como o único item que
  precisa de hardware maior.

### 4a. O PORTÃO de correcção e a rotina de validação (gen2 NATIVE — correcção do dono)

O portão DEVE rodar em **gen2 compilado com `TEKO_BACKEND=native`**, não em gen1 (gen1 é base C e
NUNCA exercita o backend nativo, onde o OOM e os defeitos native-only vivem). Rotina:

1. Buildar gen2 com `TEKO_BACKEND=native` (a auto-hospedagem nativa que este próprio fix destrava).
2. Rodar `teko test .` com esse gen2-native — verde.
3. **FIXPOINT byte-idêntico:** gen2 == gen3 sob ESTE compilador (o gen2-native recompila-se a si e o
   gen3 tem de bater byte-a-byte com gen2). É a prova de não-regressão de codegen: escopar/largar
   scratch NÃO pode alterar um único byte emitido — os bytes vão para o Grupo B por cópia, idênticos.
4. Diferencial C-vs-own no ponto de paragem honesto (`scripts/diff_c_own.sh`) inalterado.

Ponto ritual (gate completo) obrigatório em: o crumb que liga a PRIMEIRA cauda fundida (x86-linux) e
o crumb que retira os passes module-at-a-time. Ver §5.

---

## 5. A ordem em crumbs (com colisões)

Sequência bootstrap-segura. Cada crumb é independentemente gate-able. O seed é o `teko` lançado
anterior; nenhum crumb usa feature ainda não no seed (a primitiva de runtime vem antes do seu uso).

**C0 — [FEITO] commit vazio + push** (proteção contra restart). Sem código.

**C1 — Runtime: a região-corrente trocável.** `src/runtime/teko_rt.{c,h}` (C MANTIDA — permitido):
adicionar `tk_region_enter(tk_region*)`/`tk_region_leave(void)` (pilha thread-local de
região-corrente) e fazer `tk_alloc` ler o topo dela em vez de `tk_region_root()` fixo (topo vazio ⇒
raiz, comportamento-idêntico). Builtins Teko `region_enter`/`region_leave` em
`src/checker/scope.tks` (espelhar `:740-742`) + mapeamento em `src/lir/lower.tks` (espelhar
`:3981-3982`). **Colisão:** `lower.tks` (quente, muitos agentes) — a edição é ADITIVA (duas linhas de
mapeamento de nome, ao lado das de `arena_push`). **Gate:** builda; `teko test .` verde; ninguém
chama enter/leave ainda ⇒ FIXPOINT byte-idêntico trivial. Ritual: NÃO (aditivo, sem uso).

**C2 — Backend: wrappers `pub` de encode por-função.** Adicionar `pub fn encode_func_x86(abi, f:
MFuncX86): EncodedFuncX86 | error` envolvendo `fold_func_into_text_x86` (`encode_x86_64.tks:2357`)
para UMA função, devolvendo `{ text: []byte; syms: []Symbol; relocs: []RelocX86 }`; idem `encode_func`
arm64 (`encode_arm64.tks:2676`). `select_lfunc_x86`/`regalloc_func_x86` já são `pub`
(`isel_x86_64.tks:1874`, `regalloc_x86.tks:873`); os pares arm64 também (`isel_arm64.tks:1936`,
`regalloc.tks:2024`). **Colisão:** `encode_x86_64.tks`, `encode_arm64.tks` (agentes de encode) — a
edição é ADITIVA (novo `pub fn`, nenhum caller existente muda). **Gate:** wrappers não-usados ⇒
FIXPOINT byte-idêntico. Ritual: NÃO.

**C3 — project.tks: cauda fundida x86-linux (a prova).** Novo `emit_native_x86` (ou
`emit_native_x86_fused` atrás da MESMA entrada) que, em vez de `select_module_x86`→`regalloc_module_x86`
→`encode_module_x86`, faz um laço por função sobre `entry.funcs`:
`region_enter(child)` → `select_lfunc_x86` → `regalloc_func_x86` → `encode_func_x86` →
`region_leave()` → copiar `text/syms/relocs` para os acumuladores raiz (Grupo B) → `region_drop(child)`.
Montar a `EncodedModuleX86` final (símbolos na ordem LOCAL→GLOBAL→UNDEFINED, re-base de relocs por
`.text` base — a MESMA lógica de `encode_module_x86:2474-2482`, agora incremental) e chamar
`emit_elf`. **Colisão:** `project.tks` (as caudas) — edição concentrada aqui, longe dos ficheiros
quentes. **Guard E1/E2/E3 (§2):** a montagem dos acumuladores de `syms`/`relocs` tem de residir na
RAIZ e o append copiar o `str` de nome para a raiz; nenhum nome novo internado dentro da janela
escopada. **Gate — RITUAL COMPLETO:** buildar gen2 `TEKO_BACKEND=native`; `teko test .` verde;
**FIXPOINT gen2==gen3 byte-idêntico**; diff C-vs-own inalterado; medição (a)/(b) do §4 registada.

**C4 — Replicar às outras três caudas.** `emit_native_win` (`project.tks:2672`), `emit_native_arm64`
(`:2587`), `emit_native_arm64_linux` (`:2612`) — o MESMO padrão fundido, um por cauda, cada um seu
crumb + FIXPOINT do seu alvo. Manter a invariante "as duas caudas arm64 só diferem em UMA chamada"
(`emit_elf_arm64` vs `emit_macho`), senão o diferencial arm64-macos deixa de cobrir arm64-linux.
**Colisão:** `project.tks`. Ritual: SIM por cauda.

**C5 — [opcional/limpeza] retirar os passes module-at-a-time.** Depois de todas as caudas fundidas,
`select_module_x86`/`regalloc_module_x86`/`encode_module_x86` (e espelhos arm64) ficam sem chamador de
produção. Mantê-los se os testes de módulo (`*_test.tkt`) os exercitam; senão remover. **Colisão:**
`isel_*`/`regalloc*`/`encode_*` (muitos agentes) — deixar por último, isolado, só se limpar. Ritual:
SIM (é remoção de superfície pública).

**C6 — [Tier 2, follow-on] lowering por-função.** Estender a janela escopada para incluir
`lower_function` por função (freeing o LIR de A também). Requer separar em `lower_program`
(`lower.tks:12997-13022`) o setup global persistente (`table`/`enums`/`layouts`/…, `:12998-13005`) da
acumulação por-item (`rodata`/`lifted` internados por `lower_item`, `:13012-13015`): a rodata internada
por função tem de ser COMMITADA ao Grupo B antes do drop (é a exceção nova, análoga a E1). Nomeado
como follow-on; o Tier 1 (C1–C5) já elimina a acumulação module-wide de MInst, que é o alvo direto do
`grep region_new/region_drop src/backend = vazio`.

### Nota de colisão adicional (coordenação)
O agente backend-instr acrescentou/vai acrescentar `phase_begin` no backend (aditivo, instrumentação
de fase) — os crumbs acima não tocam nesse ponto; se o `phase_begin` passar a marcar fronteira de
fase por-função, ele é o sítio NATURAL para ancorar o `region_enter`/`leave` de C3 (coordenar, não
duplicar).

---

## 6. Impacto a jusante (nomear consumidores — pedido da coordenação)

Este fix é o gargalo do caminho crítico do endgame "sem emissão de C". Cadeia de dependência que os
próximos arquitetos herdam:

1. **Destrava gen2-native.** Sem o fix, gen2 `TEKO_BACKEND=native` OOMa a 15,8 GB e nem existe.
2. **gen2-native destrava o portão de testes native.** O motor `teko test` é 100% rota-C hoje, em
   dois sítios de emissão, nenhum lê `TEKO_BACKEND`/`m.backend`:
   - `native_gate_build` (`src/build/project.tks:3549`) — `codegen::tk_emit_c_test(prog, true)` +
     `run_cc` (`:3553-3564`).
   - `build_regression_cov_exe` (`src/build/project.tks:5523`) — `codegen::tk_emit_c_cov` + `run_cc`
     (`:5531-5543`).
   `scripts/no_c_in_tests_gate.sh:24` já regista que `run_native_gate` é *"a separately-scoped
   change"*. Rotear esses dois sítios por `m.backend == Native` (emitir objeto native + link em vez
   de C+cc) é o trabalho a JUSANTE — **este documento não o projeta; nomeia-o** para o próximo
   arquiteto ter o alvo exacto.

---

## 7. Assinaturas Teko que o implementador adiciona (full Javadoc — copiar verbatim)

Todas em estilo Javadoc completo (W15). Formas, não corpos (o implementador preenche).

```teko
/**
 * EncodedFuncX86 — os três outputs por-função do encode de UMA função x86-64: os
 * bytes de `.text`, os símbolos definidos (um global) e as relocações, tudo em
 * ordem de emissão. É o valor que ATRAVESSA a fronteira de fase — copiado para os
 * acumuladores raiz do objeto (Grupo B) ANTES da região de scratch da função ser
 * largada, por isso nenhum campo pode apontar para dentro da região escopada
 * (ver §2 do design; `sym`/`name` originam em `prog`, não re-internados).
 *
 * @since 0.3.1
 */
pub type EncodedFuncX86 = struct {
    /**
     * text — os bytes de `.text` desta função, em ordem de codificação.
     */
    text: []byte
    /**
     * syms — os símbolos definidos por esta função (um global), `name` residente na raiz.
     */
    syms: []Symbol
    /**
     * relocs — as relocações desta função, offsets relativos ao início do seu `.text`.
     */
    relocs: []RelocX86
}

/**
 * encode_func_x86 — codifica UMA `MFuncX86` colorida nos seus bytes de `.text`,
 * símbolos e relocações, sem tocar em rodata/globais (Grupo B). O tijolo por-função
 * do laço fundido: envolve `fold_func_into_text_x86` para uma única função e
 * devolve o delta que o driver copia para os acumuladores do objeto. Propaga
 * qualquer honest-stop de encode (ex.: spill de FPR) inalterado.
 *
 * @param abi  o register file do alvo (`SYSV64`/`WIN64`)
 * @param f    a função já colorida
 * @return     o delta por-função, ou o honest-stop propagado
 * @since 0.3.1
 */
pub fn encode_func_x86(abi: AbiDescriptor, f: MFuncX86): EncodedFuncX86 | error

/**
 * region_enter — empilha `child` como região-corrente da task: toda alocação
 * default subsequente (`list::push` e afins) passa a bump-alocar em `child` até ao
 * `region_leave` correspondente. Reusa a árvore de regiões-filhas do runtime
 * (`tk_region_new`/`tk_region_drop`), cujos chunks são SEPARADOS da raiz — sem a
 * armadilha de bump intercalado do `arena_push`/`arena_pop`. O par por-escopo do
 * backend nativo para o seu scratch por-função.
 *
 * @param child  a região-filha (de `tk_region_new(tk_region_root())`) que recebe o scratch
 * @return       void
 * @since 0.3.1
 */
pub fn region_enter(child: /* handle de região */ )

/**
 * region_leave — desempilha a região-corrente: a alocação default volta ao seu
 * destino anterior (a raiz, no topo do laço). Chamado ANTES de copiar os outputs da
 * função para os acumuladores do objeto e ANTES do `tk_region_drop` da filha.
 *
 * @return  void
 * @since 0.3.1
 */
pub fn region_leave()
```

Funções existentes que o driver fundido TOCA (chama, não edita): `select_lfunc_x86`
(`isel_x86_64.tks:1874`), `regalloc_func_x86` (`regalloc_x86.tks:873`), o novo `encode_func_x86`;
espelhos arm64 `select_lfunc`/`regalloc_func`/`encode_func`. Editado de facto: as caudas
`emit_native_*` em `project.tks:2587-2678`.

---

## 8. Fixtures de regressão (input → exit-code native esperado)

Todas exercitadas sob **gen2 `TEKO_BACKEND=native`** (não gen1/rota-C). Exit-codes nativos.

| fixture | input | exit esperado | o que prova |
|---|---|---|---|
| `bmem_two_fns` | dois `fn` triviais que retornam constantes distintas; `main` retorna `a() + b()` | soma (ex.: `7`) | fusão emite AMBAS as funções corretamente; símbolos/relocs por-função copiados |
| `bmem_call_chain` | `main` chama `f` que chama `g` (relocs de call cruzam funções) | valor propagado | `RelocX86.sym` (E1) sobrevive ao drop; re-base de `.text` correta entre funções |
| `bmem_many_small` | 64+ `fn` pequenas somadas em `main` | soma conhecida | N regiões largadas ≈ N funções (métrica §4a); nenhum vazamento de nome cross-função |
| `bmem_rodata_shared` | duas funções que usam o MESMO literal string (rodata partilhada, Grupo B) | igualdade / `0` | E2: rodata carregada por-referência NÃO cai na região de scratch; partilha preservada |
| `bmem_loop_fn` | função com `loop` + valor loop-carried (regalloc com back-edge, o maior scratch `IntervalSet`) | valor conhecido | o scratch de intervalos é escopado-e-largado sem afetar os bytes |
| `bmem_fixpoint` | o próprio `src/` (self-build) | gen2==gen3 byte-idêntico | ritual: escopar não altera um byte emitido |

Os cinco primeiros são `.tks` pequenos com `assert`/exit conhecido, colocados junto às fixtures
native existentes; o sexto é o FIXPOINT do self-build. A âncora de aceitação de MEMÓRIA (não de
correção) é `TEKO_ARENA_OBS`: "scoped > 0" e "regiões largadas ≈ nº de funções", contra o `0.0%` /
`11 de 5007` de hoje.

---

## 9. Riscos e tensões de lei — com resolução

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — E1 (nome re-internado no scratch)**: um `str` de nome de símbolo construído dentro da janela escopada fica pendurado após o drop → UAF. | Guard estrutural em C3: acumuladores de `syms`/`relocs` residem na raiz; o append copia o `str` para a raiz enquanto a filha ainda vive. Fixture `bmem_call_chain`/`bmem_many_small` cobrem. NÃO é relaxamento de escape — é cópia antes do drop. |
| **R2 — FIXPOINT quebra (bytes mudam)** | Impossível por estrutura se os outputs vão para o Grupo B por cópia idêntica; o FIXPOINT byte-idêntico é o detector. Se quebrar, algo escapou para a região errada — parar e reexaminar §2, não avançar. |
| **R3 — primitiva de runtime toca C congelada** | `teko_rt.{c,h}` é a EXCEÇÃO explícita ao congelamento (C mantida). `enter/leave` é aditivo e comportamento-idêntico com pilha vazia. Sem tensão. |
| **R4 — Teko-only / Javadoc** | Todo o produto novo é `.tks`; snippets deste plano já em Javadoc completo. Sem `//` inline. |
| **R5 — colisão nos ficheiros quentes** | Minimizada por desenho: C1 aditivo em `lower.tks`/`scope.tks`; C2 aditivo em `encode_*`; C3–C4 concentrados em `project.tks`; C5 (remoção) por último e isolado. |
| **R6 — medição do pico completo** | Constrangimento de hardware NOMEADO (§4d): só um box >16 GB fecha o pico do self-build inteiro. As métricas (a)/(b) provam direção e ordem-de-grandeza sem ele. Não é tensão de lei — é limite físico declarado. |

**Nenhuma tensão de lei genuína permanece.** Todas resolvem via Constituição/Leis (R11 arenas
lexicais; exceção de runtime C mantida; Teko-only; issue-100%). **Não há HALT.** O único item que
requer o dono é HARDWARE (>16 GB) para a prova final do pico — reportado, não um HALT de decisão.
