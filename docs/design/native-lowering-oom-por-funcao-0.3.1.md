# Escopar a memória do LOWERING nativo por função — 0.3.1 (C6 / Tier 2)

Arquiteto, 2026-08-03. Ramo `cargo/0.3.1.0-native-lowering-oom-arq` (de `origin/fix/union`, tip `5d90884a`).
Documento de DESENHO — nenhuma linha de produto aqui. `bootstrap/teko.c` é SAÍDA (congelado).

É a continuação direta e nomeada de `docs/design/backend-memoria-por-funcao-0.3.1.md` §5 **C6 — [Tier 2,
follow-on] lowering por-função**. Aquele documento fechou o Tier 1 (encode fundido por função, já em
produção — `encode_module_fused_x86`); este projeta o Tier 2 que ele deixou nomeado como o item que
ainda faltava. Leia os dois em conjunto: reuso a fronteira Grupo A / Grupo B, o mecanismo
`region_enter/leave/drop` e as exceções E1–E3 ali provados, e ACRESCENTO as exceções novas (E4–E6) que
o lowering introduz.

Regra do dono honrada: **proposta, não contra-argumento; alarme só se provado com arquivo:linha.**

---

## 0. Diagnóstico — o sítio de acumulação e a fronteira de reclaim que falta (arquivo:linha)

### 0a. O Tier 1 está feito; o OOM sobrevive porque o LOWERING não foi fundido

A cauda x86-linux (a que o portão nativo exercita) é `emit_native_x86` (`src/build/project.tks:2769`):

```
let entry = wrap_native_entry(lmod)                    // lmod = lower_program(prog), JÁ INTEIRO
encode_module_fused_x86(entry) …                       // encode POR-FUNÇÃO, já limitado (Tier 1)
```

O `lmod` é produzido por `lower_program(prog)` (`project.tks:2560`, chamado dentro de `emit_native`
antes de despachar por alvo) **inteiro, de uma vez, ANTES do encode**. O Tier 1 limitou o *encode*
(`encode_module_fused_x86` → `fold_lfunc_scoped_x86`, `project.tks:2737-2746` e `2709-2722`) mas o
*lowering* continua a construir TODO o LModule na raiz. O pico à entrada do codegen já é
`Σ(LIR de todas as funções) + Σ(scratch de lowering de todas as funções)` — e o encode fundido,
por mais limitado que seja, corre só DEPOIS desse pico já existir.

### 0b. O laço que acumula (arquivo:linha) e por que nada é recuperado

`lower_program` (`src/lir/lower.tks:13732`), corpo `13732-13757`. O laço por-item é `13745-13752`:

```teko
loop {
    if i >= prog.items.len { break }
    let step = match lower_item(m, loose, prog.items[i], …) { LowerItemOut as x => x; error as err => return err }
    m = step.module          // m.funcs cresce; m.rodata cresce
    loose = step.loose
    lifted = step.lifted
    i++
}
```

Este laço corre **inteiramente na região-RAIZ da task**. Nenhum `region_new`/`region_enter` o envolve
(contraste medido: `grep -n "region_" src/lir/lower.tks` só encontra o código GERADO para o programa
compilado — os `NativeRegionFrame`/`emit_region_*` de `1083-1478` são a residência do código EMITIDO,
não o compilador a escopar o SEU próprio scratch). Cada `lower_item` → `lower_item_function`
(`14727`) → `lower_function` bump-aloca na raiz DUAS classes de coisa:

1. **O output que sobrevive** — o `LFunc` (`add_func(with_rodata(m, lf.rodata), lf.func)`, `14731`) e
   as entradas novas de `rodata`. Necessário para o encode.
2. **Scratch transitório enorme** — todo o *churn* de listas persistentes do estilo funcional. Cada
   `ctx_append`/`teko::list::push`/`ctx_with_*` (`lower.tks:1140-1350`) devolve uma lista NOVA; a
   versão anterior fica na raiz como lixo até o fim do processo. O corpo de UMA função gera milhares
   desses `Lowered`/`LowerCtx` intermédios superados. O NP4 AUMENTOU esta classe: acrescentou
   `region_stack: []NativeRegionFrame` e `bracket_depth` por-binding ao `LowerCtx` (`lower.tks:1083-1111`,
   `1092`, `1103`), copiados em cada `ctx_with_*`.

Como `tk_alloc` bump-aloca sempre de `tk_region_current()` (a raiz até um `tk_region_enter`,
`src/runtime/teko_rt.c:1854-1860`, `4035-4044`) e um `tk_region_drop` só liberta chunks de uma
região-FILHA, **nada da classe 2 é recuperado entre itens**. O pico é:

```
pico = Σ_todos_os_itens( LIR do LFunc )   +   Σ_todos_os_itens( scratch transitório )
       └── necessário, limitado por programa      └── ILIMITADO — é o OOM
```

### 0c. A distinção que o issue pede: acumulação, não um item gigante

O killer é o **segundo somatório** (o scratch de todos os 6842 itens vivo ao mesmo tempo), NÃO
"o lowering de um item é gigante". Prova por estrutura: o alocador de bump nunca recupera dentro da
raiz, e cada item deixa lá o seu churn; 6842 × (churn por função) satura os 16 GB. Um único item
grande caberia; é a soma que não cabe. É por isso que a fronteira de reclaim tem de ser
**por-item**, e é exatamente o que falta.

### 0d. O momento do SIGKILL bate certo com o diagnóstico

O emit nativo é SILENCIOSO: as fases com tick são só `checker`/`monomorph`/`consteval`
(`src/build/project.tks:307-369`); não há fase `lower`/`codegen`. Logo, "consteval 557/557 ✓" seguido
de `Killed: 9` sem nenhuma linha de codegen é o esperado se o OOM ocorre DENTRO de `lower_program`
(mudo) — que é precisamente onde o segundo somatório acresce. O NP1–NP4 (dreno de `lower.tks`)
mexeu na residência do código EMITIDO (memória de runtime do programa compilado), não neste scratch de
COMPILE-TIME — daí não ter cedido, e o NP4 até o ter agravado (classe 2 acima).

### 0e. A fronteira de reclaim que falta, e onde ela JÁ existe

O caminho de ENCODE já tem a fronteira: `fold_lfunc_scoped_x86` (`project.tks:2709-2722`) abre
`region_new(region_root())`, `region_enter`, encoda UMA função, `region_leave`, copia os BYTES para o
acumulador raiz (`fold_encoded_func_x86`), `region_drop(child)`. O caminho de LOWER **nunca recebeu
esse tratamento**. Este documento estende a MESMA janela escopada para cobrir também o lowering do
item — é o C6 nomeado no precedente.

---

## 1. É espelho de mecanismo existente, não runtime novo. E a rota C não faz isto

**Espelho, não novo.** As primitivas `region_root`/`region_new`/`region_drop`/`region_enter`/
`region_leave` já existem como `extern fn` e JÁ são usadas em produção pelo encode fundido
(`project.tks:2628-2673`, mapeadas em `teko_rt.c:1876-1897`). O Tier 2 **não adiciona uma linha de
runtime**; reusa a janela de `fold_lfunc_scoped_x86` e alarga-a para incluir o `lower_item` antes do
encode. `teko_rt.{c,h}` fica intocado.

**A rota C NÃO faz isto — e não precisa.** `tk_emit_c_mode` (`src/codegen/codegen.tks:12518`) acumula
TUDO num único `mut b: []byte` na raiz (`12524` em diante), sem nenhum escopo por-item — igual ao
lowering nativo neste aspeto. A diferença é de VOLUME, não de desenho: a rota C emite TEXTO (bytes
pequenos por função), enquanto o lowering nativo constrói árvores de LIR + o churn funcional de listas
+ (fundido) isel/regalloc/encode scratch, ordens de grandeza maiores. A rota C "safa-se" por volume; o
nativo não. Portanto:

> **Veredicto para o issue:** a rota C **não** faz stream/free por-item — ela também segura tudo na raiz.
> O caminho nativo de ENCODE **já** tem a fronteira por-função (Tier 1). O caminho nativo de LOWER é o
> único que falta, e o remédio é **espelhar** a janela do encode, não inventar mecanismo.

---

## 2. A fronteira estendida — Grupo A e Grupo B para o lowering fundido

Reuso os grupos do precedente (§1) e classifico o que o lowering acrescenta.

### 2a. Grupo A — SCRATCH por-função (escopado-e-largado; morre com a filha do item)

| estrutura | onde nasce (arquivo:linha) | o que é |
|---|---|---|
| LIR do `LFunc` do item + dos seus lifted | `lower_function` via `lower_item_function` (`lower.tks:14730`), tipo `lir.tks:219` | blocos + instruções LIR — **hoje sobrevive**; no desenho fundido é encodado na janela e vira scratch |
| churn de `LowerCtx`/`Lowered` (listas persistentes superadas) | `ctx_append`/`ctx_with_*` (`lower.tks:1140-1350`), `Lowered` (`1134`) | a classe 2 do §0b — o maior scratch |
| `region_stack`/`bracket_depth` por-binding (NP4) | `lower.tks:1083-1111` | estado de residência do código emitido, per-binding |
| `SelCtxX86`/`MFuncX86`/`IntervalSet`/buffers de encode | Grupo A do Tier 1 (§1a do precedente) | o scratch de isel/regalloc/encode do item |

Tudo derivado do item e nada externo o referencia depois que os BYTES do item entram no objeto.

### 2b. Grupo B — PERSISTENTE (raiz/objeto; copiado por valor antes do drop)

| estrutura | onde vive (arquivo:linha) | por que persiste |
|---|---|---|
| `prog` tipado | entrada de `emit_native` (`project.tks:2557`) | lido por todos os itens + `.tsym` |
| contexto global de lowering: `table`/`enums`/`layouts`/`externs`/`variants`/`ref_fns`/`field_decls` | `lower_program:13733-13740` | setup ÚNICO, partilhado por todos os itens |
| `LModule.rodata` (tabela de literais internados, DEDUP whole-program) | cresce em `lower_item_function` (`14731`, `with_rodata`) | o encoder emite os bytes; a dedup do PRÓXIMO item lê-a |
| `LModule.globals`/`layouts` | `lir.tks:320` | Grupo B desde o setup |
| contagem de `lifted` (para reserva de id única) | `ctx.lifted.len` (`lower.tks:5495`, `5932`) | só o `.len` é lido; nomeia o próximo thunk/lambda |
| `loose` (statements top-level soltos) | acumula em `lower_item` (`14650`) | o virtual-main lê-os DEPOIS do laço |
| acumulador do objeto `ModuleTextX86` (text/syms/relocs) | Grupo B do Tier 1 (`encode_x86_64.tks:2294`) | É O ARTEFATO — cresce por função |

### 2c. As exceções NOVAS que o lowering introduz (análogas a E1)

- **E4 — delta de rodata** (a exceção que o precedente já anunciou em C6: *"a rodata internada por
  função tem de ser COMMITADA ao Grupo B antes do drop"*). No item fundido, as entradas NOVAS de
  `rodata` (o sufixo de `lf.rodata` além do `m.rodata` de entrada) nascem na filha. Têm de ser copiadas
  para um acumulador de rodata RAIZ antes do `region_drop`, porque (a) a dedup do próximo item lê a
  tabela e (b) o encoder emite os bytes no `.rodata` do objeto. Cada `LRodata = { symbol: str; bytes;
  … }`: o `bytes` é cópia byte-a-byte; o `symbol` é um `str`. **Sub-exceção medida:** o `symbol` de um
  literal pode ser SINTETIZADO na janela (via `str::concat`/`rodata_symbol`, `lower.tks:5465-5478`) —
  cai na filha — logo o commit tem de copiar o `str` do símbolo para a raiz também (idêntico a E1 do
  precedente). Um símbolo de const agregado deriva do nome em `prog` (raiz) e podia partilhar por
  referência, mas o commit uniforme (copiar sempre) é mais barato de raciocinar e igualmente correto.
- **E5 — lifted funcs**: os `LFunc` lifted NOVOS do item (`step.lifted` além da contagem de entrada)
  são encodados NA MESMA janela (bytes → raiz, Grupo B), exatamente como o `LFunc` de topo do item —
  **não** sobrevivem como LIR. A única coisa que atravessa entre itens é a CONTAGEM (`lifted.len`),
  para a reserva de id única (`lower.tks:5495`, `5932` só leem `.len`, nunca desreferenciam entradas —
  verificado). Mantém-se um acumulador de lifted RAIZ com o comprimento correto (ver E5-wiring no §5).
- **E6 — loose**: os elementos de `loose` são `checker::TStatement` residentes em `prog` (raiz); só o
  ESPINHA da lista é construída por-item. O append tem de residir na raiz (feito depois do
  `region_leave`), para o virtual-main os ler depois de todas as filhas largadas.

**Guardas whole-program — para onde vão:**
- `fat_divergence_guard` (`lower.tks:13734`) — corre ANTES do laço, lê só `prog`. Fica no prelúdio
  (Grupo B), inalterado.
- `frame_escape_guard` (`frame_escape.tks:56`) — itera `m.funcs` mas cada verificação é PER-FUNÇÃO e
  independente (`func_returns_frame_address(m.funcs[i])`, `frame_escape.tks:69-77`). **Move-se para
  DENTRO da janela do item**: verifica-se `lf.func` (e cada lifted) antes do drop. Não precisa de todo
  o LIR coexistente — é o detalhe que torna a fusão possível sem quebrar a guarda.

---

## 3. A prova de não-escape (por estrutura) para o lowering

Afirmação: ao largar a filha do item A, nenhum ponteiro vivo aponta para dentro dela. Os únicos outputs
que persistem são (i) os BYTES de A (do LFunc e dos seus lifted) → `ModuleTextX86` por cópia (Tier 1,
E1); (ii) o delta de rodata → acumulador raiz por cópia (E4); (iii) o append de `loose` → raiz (E6);
(iv) o incremento da contagem de lifted (E5, um `u32` por valor). Item a item:

1. **BYTES do LFunc e dos lifted de A** — via `fold_encoded_func_x86` (Tier 1 provado, §2 do
   precedente): cópia byte-a-byte para `ModuleTextX86.text`; `Symbol.name`/`RelocX86.sym` originam em
   `prog`/tabelas globais (raiz) ou são copiados. ✔
2. **Delta de rodata** — copiado para o acumulador raiz ANTES do drop, com `bytes` e `symbol` copiados
   (E4). Depois da cópia nada em Grupo B aponta para a filha. ✔
3. **`loose`** — append de referências a `prog` (raiz), espinha construída na raiz pós-`leave` (E6). ✔
4. **Contagem de lifted** — `u32`, por valor (E5). ✔

Nada aqui relaxa análise de escape. A garantia é a MESMA fronteira de fase do Tier 1 estendida ao
lowering: o output do item é copiado para a raiz antes de o item ser largado. O FIXPOINT byte-idêntico
(gen2==gen3) é o detetor de qualquer escape para a região errada.

---

## 4. O mecanismo — o laço totalmente fundido (lower + encode por item)

A fusão vive em `project.tks` (a cauda `emit_native_x86`), chamando `pub fn` por-item e por-função que
já existem ou que os crumbs C6.1/C6.2 expõem — para NÃO editar os ficheiros de colisão quente. Estrutura
alvo (pseudo-Teko; formas exatas no §6):

```
prelude = lower_prelude(prog, flat_symbols=false)   // Grupo B: contexto + módulo inicial (layouts + rodata de const-agregado)
mut robase   = prelude.module.rodata                // acumulador de rodata RAIZ
mut liftedN  = 0 to u32                              // contagem de lifted RAIZ (E5)
mut loose    = teko::list::empty()                  // RAIZ (E6)
mut mt       = empty_module_text_x86()              // acumulador do objeto RAIZ (Tier 1)

// laço por item — cada item na SUA filha
loop over prog.items[i]:
    child = region_new(region_root()); region_enter(child)
    seed  = LModule { funcs = empty; rodata = robase; globals = prelude.module.globals; layouts = prelude.module.layouts }
    step  = lower_item(seed, loose, prog.items[i], prelude.<contexto…>, liftedN_placeholder)   // LFunc(s) + rodata crescida + loose', tudo na FILHA
    // guarda per-função (movida para cá)
    frame_escape_check(step.module.funcs ++ new_lifted(step))   // honest-stop propaga
    // encode do LFunc do item + dos seus lifted NOVOS, na MESMA filha
    encoded = encode_lfuncs_in_region_x86(step.module.funcs ++ new_lifted(step))
    region_leave()
    // commits para a RAIZ (Grupo B), filha ainda viva
    mt      = fold_encoded_funcs_x86(mt, encoded)               // E1 (bytes)
    robase  = commit_rodata_delta(robase, step.module.rodata)   // E4 (delta de literais)
    loose   = commit_loose(loose, step.loose)                   // E6 (referências)
    liftedN = liftedN + count_new_lifted(step)                  // E5
    region_drop(child)

// virtual-main na sua própria filha
child = region_new(region_root()); region_enter(child)
vmain = lower_virtual_main(loose, prelude.layouts, prelude.enums, robase, liftedN_placeholder, …)
enc_vm = encode_lfuncs_in_region_x86( wrap_entry_funcs(vmain) )   // vmain renomeado + stub (ver §4a)
region_leave()
mt = fold_encoded_funcs_x86(mt, enc_vm)
robase = commit_rodata_delta(robase, vmain.rodata)
region_drop(child)

enc = finish_encoded_module_x86(mt, robase, prelude.module.globals)   // Tier 1
emit_elf(enc) → finish_native_object(…)
```

**Pico resultante:** `Σ(bytes do objeto)` [necessário, pequeno] + `rodata` [necessário, limitado por
programa] + `contexto global` [único] + **UM item** vivo de cada vez (LIR + churn + isel/regalloc/encode
scratch). O segundo somatório do §0b é ELIMINADO — o scratch de cada item é largado antes do seguinte.

### 4a. `wrap_native_entry` no mundo fundido

`wrap_native_entry` (`lower.tks:14928-14932`) faz duas coisas whole-module: renomeia o `main` virtual
para `NATIVE_ENTRY_VMAIN_SYMBOL` e acrescenta um `native_entry_stub()`. No laço fundido isto reduz-se a
duas operações locais na janela do virtual-main: encodar o `vmain` sob o símbolo renomeado
(`rename_lfunc`, já existe, `lower.tks:14952`) e encodar o stub (um `LFunc` minúsculo). Nenhuma tabela
whole-module é materializada.

### 4b. Reset de `m.funcs` — por que `lower_item` é reusável sem edição

`lower_item_function` faz `add_func(with_rodata(m, lf.rodata), lf.func)` (`14731`). Se o `seed.funcs` de
cada iteração for VAZIO, `add_func` devolve `funcs = [lf.func]` — só o LFunc DESTE item. O driver lê
`step.module.funcs` (o novo LFunc), encoda-o, e nunca acumula `funcs` num módulo. Assim `lower_item`
fica **inalterado**; só precisa de ser alcançável (um `pub` fino, C6.1). É o truque de menor colisão:
a lista de funcs do módulo deixa de ser o acumulador; o acumulador passa a ser `mt` (bytes).

---

## 5. A ordem em crumbs (bootstrap-safe, cada um gate-able)

Continuação da numeração do precedente (que fechou em C5; C6 era o follow-on). O seed é o `teko`
lançado anterior; nenhum crumb usa feature fora do seed (as primitivas de região já estão no seed —
usadas pelo Tier 1). Colisão minimizada: as edições concentram-se em `project.tks` (a cauda) e num
`pub` fino em `lower.tks`; os ficheiros de encode/isel/regalloc não são tocados.

**C6.0 — [proteção de restart] commit vazio + push.** Sem código. (Feito na abertura do ramo.)

**C6.1 — `lower.tks`: prelúdio + entrada por-item `pub` (ADITIVO).** Extrair de `lower_program`
(`13732-13757`) o setup Grupo B num `pub fn lower_prelude(prog, flat_symbols): LowerPrelude | error`
(guardas + `collect_*` + módulo inicial com layouts registados + rodata de const-agregado interná). Expor
`lower_item` como `pub` (ou um wrapper `pub fn lower_item_pub(...)` com a mesma assinatura). `lower_program`
PERMANECE, reimplementado sobre `lower_prelude` + o laço existente, **byte-idêntico** (a rota C e o
static-lib continuam a chamá-lo). **Colisão:** `lower.tks` (quente) — edição de extração + `pub`, sem
mudar semântica. **Gate:** builda; `teko test .` verde; ninguém usa o novo `pub` ainda ⇒ FIXPOINT
byte-idêntico trivial (`lower_program` inalterado no comportamento). **Ritual: NÃO.**

**C6.2 — `backend`/`project.tks`: helpers de encode-lista + commit (ADITIVO).** `pub fn
encode_lfuncs_in_region_x86(fs: []lir::LFunc): []backend::EncodedFuncX86 | error` (envolve o
`encode_lfunc_in_region_x86` existente, `project.tks:2687`, para uma LISTA — o LFunc do item + os seus
lifted); `fn fold_encoded_funcs_x86(mt, efs)` (envolve `fold_encoded_func_x86` num laço, já existe
`2716`); `fn commit_rodata_delta(robase, grown): []lir::LRodata` (copia o sufixo novo com `bytes` e
`symbol` copiados — E4); `fn commit_loose(acc, delta)` (E6). **Colisão:** `project.tks` (concentrado).
**Gate:** helpers não-usados ⇒ FIXPOINT byte-idêntico. **Ritual: NÃO.**

**C6.3 — `project.tks`: a cauda x86-linux totalmente fundida (A PROVA).** Reescrever `emit_native_x86`
(`2769`) para o laço do §4: prelúdio → laço por-item (filha: `lower_item` com `seed.funcs` vazio →
`frame_escape_check` per-item → `encode_lfuncs_in_region_x86` → `leave` → commits E1/E4/E5/E6 → `drop`) →
virtual-main na sua filha (com o entry-wrap do §4a) → `finish_encoded_module_x86` → `emit_elf`. A
montagem final de símbolos/relocs é a MESMA de Tier 1 (`finish_encoded_module_x86`), agora alimentada
incrementalmente. **Guard E4/E5/E6 (§2c/§3):** os acumuladores `robase`/`loose`/`mt` residem na raiz; os
commits copiam `str`/`bytes` para a raiz enquanto a filha vive; nenhum nome novo internado é deixado na
filha. **Colisão:** `project.tks`. **Gate — RITUAL COMPLETO:** buildar gen2 `TEKO_BACKEND=native`
(a auto-hospedagem que este fix destrava); `teko test .` verde; **FIXPOINT gen2==gen3 byte-idêntico**;
diff C-vs-own inalterado (`scripts/diff_c_own.sh`); portão nativo `scripts/fixpoint_gate.sh
TEKO_FIXPOINT_BACKEND=native` deixa de OOMar em gen2; métrica de memória (§7) registada.

**C6.4 — replicar às outras três caudas.** `emit_native_arm64` (`2587`), `emit_native_arm64_linux`
(`2612`), `emit_native_win` (`2801`) — o MESMO laço fundido. **Nota:** estas caudas ainda usam
`select_module`/`regalloc_module`/`encode_module` whole-module (nem sequer têm o Tier 1); a fusão dá-lhes
os dois tiers de uma vez. Manter a invariante "as duas caudas arm64 só diferem em UMA chamada"
(`emit_elf_arm64` vs `emit_macho`). **Colisão:** `project.tks`. **Ritual: SIM por cauda** (FIXPOINT do
seu alvo; o gate x86 do C6.3 é o obrigatório para fechar o issue).

**C6.5 — `emit_static_lib`: mesma fusão.** `emit_static_lib_x86` (`2961`) e espelhos (`2969`+) usam os
passes module-at-a-time; alinhar com a cauda fundida (partilham `finish_static_archive`). O `static`
precisa de `strip_virtual_main` (`2938`) — no fundido, simplesmente NÃO se encoda o virtual-main.
**Colisão:** `project.tks`. **Ritual: SIM** (o `ar_link_run` real-link+run).

**C6.6 — [opcional/limpeza] retirar os passes module-at-a-time do lowering.** Depois de todas as caudas
fundidas, `select_module_x86`/`regalloc_module_x86`/`encode_module_x86` (Tier 1 já os deixou sem
chamador de PRODUÇÃO no x86; o C6.4/C6.5 remove os restantes). Mantê-los se os `*_test.tkt` os
exercitam; senão remover. **Ritual: SIM** (remoção de superfície pública).

**Ponto ritual obrigatório para FECHAR o issue:** C6.3 (a cauda x86-linux fundida). É o crumb que faz o
portão `TEKO_FIXPOINT_BACKEND=native` deixar de OOMar em gen2.

---

## 6. Assinaturas Teko que o implementador adiciona (full Javadoc — copiar verbatim)

Formas, não corpos. Estilo Javadoc completo (W15).

```teko
/**
 * LowerPrelude — o contexto whole-program de lowering, computado UMA vez e residente na
 * raiz (Grupo B): o módulo inicial (layouts registados + rodata dos const-agregados
 * internada) mais as tabelas globais que cada item lê. É o valor que o laço fundido
 * mantém vivo por todo o build enquanto larga o scratch de cada item; nenhum campo é
 * per-item.
 *
 * @since 0.3.1
 */
pub type LowerPrelude = struct {
    /**
     * module — o módulo semente: layouts registados e a rodata dos const-agregados,
     * antes de qualquer função lowerar. O laço reusa `rodata`/`globals`/`layouts`;
     * `funcs` fica vazio (o acumulador de código passa a ser os BYTES, não este campo).
     */
    module: LModule
    /**
     * layouts — a tabela de layouts de struct/classe do programa, lida por cada item.
     */
    layouts: []LStructLayout
    /**
     * enums — a tabela de enums do programa.
     */
    enums: []LEnumInfo
    /**
     * externs — a tabela de `extern fn` do programa.
     */
    externs: []LExternFn
    /**
     * variants — a tabela de definições de variante do programa.
     */
    variants: []LVariantDef
    /**
     * ref_fns — a tabela de funções-referência (`Ref<T>`) do programa.
     */
    ref_fns: []LRefFnInfo
    /**
     * field_decls — a tabela de tipos-de-campo declarados (coerção de store/argumento).
     */
    field_decls: []LFieldDecls
    /**
     * table — a type-table do programa (resolver de alias de `typeexpr_is_fat`).
     */
    table: checker::TypeTable
}

/**
 * lower_prelude — o setup whole-program de `lower_program` (guardas + `collect_*` +
 * módulo inicial) extraído para ser partilhado pelo laço fundido do backend nativo.
 * Corre as guardas `fat_divergence_guard` (whole-program, lê só `prog`) e devolve o
 * contexto Grupo B; `frame_escape_guard` NÃO corre aqui — passa a ser per-função
 * dentro da janela de cada item.
 *
 * @param prog          o programa tipado e checado
 * @param flat_symbols  a carve-out de arquivo estático (mangle bare vs prefixado)
 * @return              o contexto whole-program residente na raiz, ou o honest-stop das guardas
 * @since 0.3.1
 */
pub fn lower_prelude(prog: checker::TProgram, flat_symbols: bool): LowerPrelude | error

/**
 * encode_lfuncs_in_region_x86 — encoda uma LISTA de `LFunc` (o LFunc de topo do item
 * mais os seus lifted novos) já DENTRO da região de scratch entrada, cada um via o
 * `encode_lfunc_in_region_x86` provado (isel → regalloc → encode). Devolve os deltas
 * por-função que o driver copia para o acumulador do objeto (Grupo B) antes do drop.
 * Qualquer honest-stop de encode propaga inalterado.
 *
 * @param fs  as funções lowered do item, na ordem de emissão
 * @return    um `EncodedFuncX86` por função, ou o honest-stop propagado
 * @since 0.3.1
 */
pub fn encode_lfuncs_in_region_x86(fs: []teko::lir::LFunc): []teko::backend::EncodedFuncX86 | error

/**
 * commit_rodata_delta — copia para o acumulador de rodata RAIZ as entradas NOVAS que o
 * lowering de um item internou na sua região de scratch (o sufixo de `grown` além do
 * comprimento de `robase`), com `bytes` E `symbol` copiados byte-a-byte/`str` para a
 * raiz (exceção E4). Chamado com a filha AINDA viva, antes do `region_drop`, para que
 * a dedup do próximo item e o `.rodata` do objeto vejam entradas residentes na raiz.
 *
 * @param robase  o acumulador de rodata raiz (o prefixo já commitado)
 * @param grown   a tabela de rodata devolvida pelo lowering do item (prefixo + delta)
 * @return        `robase` estendido com o delta copiado para a raiz
 * @since 0.3.1
 */
fn commit_rodata_delta(robase: []teko::lir::LRodata, grown: []teko::lir::LRodata): []teko::lir::LRodata

/**
 * commit_loose — anexa ao acumulador de `loose` RAIZ as statements soltas novas de um
 * item (referências a `prog`, residente na raiz), com a espinha da lista construída na
 * raiz (exceção E6). Chamado depois do `region_leave` para que o virtual-main as leia
 * depois de todas as filhas largadas.
 *
 * @param acc    o acumulador de loose raiz
 * @param delta  as loose statements devolvidas pelo item (prefixo + delta)
 * @return       `acc` estendido, residente na raiz
 * @since 0.3.1
 */
fn commit_loose(acc: []checker::TStatement, delta: []checker::TStatement): []checker::TStatement
```

Funções existentes que o driver fundido TOCA (chama, não edita): `lower_item`/`lower_item_function`
(`lower.tks:14647`/`14727` — só precisam de ficar alcançáveis via C6.1), `lower_virtual_main`
(`14762`), `rename_lfunc`/`native_entry_stub` (para o entry-wrap por-função, §4a), `frame_escape.tks`
`func_returns_frame_address` (per-função, movida para a janela), e todo o maquinário Tier 1 já `pub`
(`encode_lfunc_in_region_x86`, `fold_encoded_func_x86`, `finish_encoded_module_x86`, `emit_elf`).
Editado de facto: as caudas `emit_native_*`/`emit_static_lib_*` em `project.tks` e a extração de
`lower_prelude` em `lower.tks`.

**Nota E5-wiring (a contagem de lifted).** `lower_function` reserva ids de thunk/lambda por
`ctx.lifted.len` (`lower.tks:5495`, `5932` — só lê `.len`, nunca desreferencia entradas). Para preservar
a unicidade sem segurar o LIR dos lifted entre itens, o driver mantém `liftedN` (raiz, `u32`) e passa ao
`lower_item` uma lista de comprimento `liftedN` cujas entradas são placeholders leves residentes na raiz
(um `LFunc` só-símbolo, blocos vazios) — nunca desreferenciadas, portanto seguras. **Alternativa mais
limpa (opcional, toca `lower.tks` quente):** trocar a reserva por um contador `u32` threadado no
`LowerCtx` em vez de `lifted.len`; elimina o placeholder mas altera dois sítios de lowering. Recomendo o
placeholder no Tier 2 (menor colisão) e o contador como limpeza posterior.

---

## 7. Medição e o seu constrangimento (o mesmo do Tier 1)

O pico do self-build nativo COMPLETO só fecha num box adequado; o próprio issue confirma o SIGKILL a
~16 GB. O que É mensurável e prova direção/ordem-de-grandeza sem esse box:

- **(a) `TEKO_ARENA_OBS` num projeto pequeno sob `TEKO_BACKEND=native`.** Antes (C6 por aplicar):
  `reclaimed 0.0%` no lowering, tudo na raiz, ~1 região largada. Depois: "scoped (freed at region drop)"
  > 0 e **nº de regiões largadas ≈ nº de itens** (uma filha por item + uma por virtual-main). É a âncora
  de aceitação de MEMÓRIA (não de correção).
- **(b) Pico `CHUNKS` antes/depois no mesmo projeto.** O termo dominante cai de
  `Σ(LIR+scratch de todos os itens)` para `Σ(bytes) + rodata + UM item`.
- **(c) Rota C — baseline de não-regressão.** Não exercita este caminho (§1); serve só para provar que o
  resto do pipeline não regrediu.
- **(d) Só um box >16 GB fecha** o pico absoluto do self-build nativo inteiro — a prova final de "deixou
  de OOMar". Nomeado como o único item que precisa de hardware; não é tensão de lei, é limite físico.

---

## 8. Fixtures de regressão (input → exit-code native esperado)

Todas sob **gen2 `TEKO_BACKEND=native`**. Exit-codes nativos. Colocadas junto às fixtures native
existentes (`examples/regressions/own_native/`).

| fixture | input | exit esperado | o que prova |
|---|---|---|---|
| `lmem_two_fns` | dois `fn` triviais + `main` que soma | soma (ex.: `7`) | lowering fundido emite ambos; bytes/símbolos por-item corretos |
| `lmem_rodata_shared` | duas funções que usam o MESMO literal string | igualdade / `0` | **E4**: delta de rodata commitado à raiz; dedup preservada entre itens (o `symbol` sintetizado sobrevive ao drop) |
| `lmem_rodata_many` | 200+ funções cada com um literal DISTINTO | soma/hash conhecido | **E4** em volume: a tabela de rodata cresce corretamente na raiz enquanto cada item é largado |
| `lmem_lambda` | função com closure/lambda liftada + `main` que a chama | valor conhecido | **E5**: contagem de lifted preserva ids únicos; lifted encodado na janela, não segurado |
| `lmem_lambdas_many` | 64+ funções cada com o seu lambda liftado | soma conhecida | **E5** em volume: ids de lifted únicos com o placeholder-count; nenhum vazamento cross-item |
| `lmem_loose_main` | vários statements soltos top-level + expressão final | exit-code do último | **E6**: `loose` residente na raiz; virtual-main lê-os depois de todas as filhas largadas |
| `lmem_call_chain` | `main`→`f`→`g` (relocs de call cruzam itens) | valor propagado | reloc `sym` (E1) sobrevive ao drop; re-base de `.text` correta entre itens fundidos |
| `lmem_fixpoint` | o próprio `src/` (self-build) | gen2==gen3 byte-idêntico | ritual C6.3: fundir o lowering NÃO altera um byte emitido |

Os sete primeiros são `.tks` pequenos com `assert`/exit conhecido; o oitavo é o FIXPOINT do self-build e
a prova de que o portão nativo deixou de OOMar. Âncora de MEMÓRIA (não de correção): `TEKO_ARENA_OBS`
"scoped > 0" e "regiões largadas ≈ nº de itens", contra o `0.0%` / ~1-região de hoje no lowering.

---

## 9. Riscos e tensões de lei — com resolução

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — E4 (símbolo de rodata sintetizado na janela)**: um `str` de símbolo construído por `str::concat`/`rodata_symbol` dentro da filha fica pendurado após o drop → dedup lê lixo / `.rodata` corrompida. | `commit_rodata_delta` copia `symbol` E `bytes` para a raiz enquanto a filha vive (E4), antes do `region_drop`. Fixtures `lmem_rodata_shared`/`lmem_rodata_many` cobrem. NÃO é relaxamento de escape — é cópia antes do drop, idêntico ao E1 do Tier 1. |
| **R2 — E5 (id de lifted colide/salta)**: se a contagem de lifted não for threadada, dois lambdas em itens diferentes ganham o mesmo símbolo → link duplicado/errado. | `liftedN` raiz threadado; placeholder-count de comprimento certo (só `.len` é lido, `lower.tks:5495`/`5932` verificado). Fixtures `lmem_lambda`/`lmem_lambdas_many`. |
| **R3 — FIXPOINT quebra (bytes mudam)** | Impossível por estrutura se os outputs vão para o Grupo B por cópia idêntica (os bytes são função pura do LIR, que é função pura de `prog`; escopar só muda ONDE o scratch bump-aloca). O FIXPOINT byte-idêntico é o detetor; se quebrar, algo escapou para a região errada — parar e reexaminar §3. |
| **R4 — `frame_escape_guard` per-função perde cobertura** | A verificação já é independente por função (`func_returns_frame_address`, `frame_escape.tks:69-77`); movê-la para a janela do item verifica exatamente as mesmas funções, só mais cedo. A mensagem de erro agrega igual (uma por função ofensora). Sem perda. |
| **R5 — colisão em `lower.tks` (quente)** | C6.1 é EXTRAÇÃO + `pub` (sem mudança de semântica; `lower_program` fica byte-idêntico e continua a servir a rota C/static). O resto concentra-se em `project.tks`. O E5-wiring usa placeholder (não toca a reserva de id) como primário. |
| **R6 — runtime C congelado** | ZERO runtime novo: as primitivas `region_*` já existem e já são usadas pelo Tier 1 (`teko_rt.c:1876-1897`, `project.tks:2628-2673`). Sem tensão. |
| **R7 — Teko-only / Javadoc** | Todo o produto novo é `.tks`; snippets já em Javadoc completo; sem `//` inline. |
| **R8 — medição do pico completo** | Constrangimento de hardware NOMEADO (§7d); as métricas (a)/(b) provam direção sem ele. Limite físico declarado, não tensão de lei. |
| **R9 — regressão de residência do código EMITIDO (NP1–NP5)** | Este fix é COMPILE-TIME (as arenas do PRÓPRIO compilador) e não toca a regra de residência do código emitido (`escaping→caller`, `non-escaping→scope`, `wide→root`) nem os `emit_region_*` de `lower.tks:1083-1478`. Ortogonal ao NP-port; os bytes emitidos são idênticos (R3). |

**Nenhuma tensão de lei genuína permanece.** Todas resolvem via Constituição/Leis (R11 arenas lexicais;
runtime C mantido/reusado; Teko-only; issue-100%; residência do emitido intocada). **Não há HALT.** O
único item que requer o dono é HARDWARE (>16 GB) para a prova final do pico absoluto — reportado, não um
HALT de decisão.

---

## 10. Resumo executivo (o pedido do issue, em três frases)

1. **Sítio de acumulação:** `lower_program` (`src/lir/lower.tks:13732`, laço `13745-13752`) corre na
   região-RAIZ e nunca a recupera; cada `lower_item` deixa lá o LIR (necessário) MAIS o churn funcional
   de listas + estado NP4 (ilimitado) → o pico é `Σ(scratch de todos os 6842 itens)`, o OOM.
2. **Fronteira de reclaim em falta:** não há região por-item no caminho de LOWER; a de ENCODE já existe
   (`fold_lfunc_scoped_x86`, `project.tks:2709`). **É espelho, não mecanismo novo** — o C6 já nomeado em
   `backend-memoria-por-funcao-0.3.1.md §5`. A rota C também segura tudo na raiz mas safa-se por volume;
   o nativo não.
3. **Fix:** fundir lower+encode por item numa filha, largando-a depois de copiar para a raiz só os BYTES
   (E1), o delta de rodata (E4), o `loose` (E6) e a contagem de lifted (E5); pico passa a
   `Σ(bytes) + rodata + UM item`. Crumbs C6.1–C6.6; ritual obrigatório em C6.3 (cauda x86-linux, o portão
   `TEKO_FIXPOINT_BACKEND=native` deixa de OOMar).
