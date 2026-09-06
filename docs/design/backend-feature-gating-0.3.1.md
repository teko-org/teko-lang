---
section: design
created: 2026-08-03
status: PROPOSTA — arquiteto, encomendada pelo dono ("manda pro arquiteto"). Nenhuma linha de
  produto tocada nesta carga; só design (docs). Teko-only preservado — nenhum mecanismo aqui
  descrito introduz C novo.
branch: cargo/0.3.1.0-backend-feature-gating (de origin/fix/union)
lê:
  - src/build/project.tks (NativeTarget, emit_native, prune_os, target_os)
  - src/build/discover.tks (dsc_walk — o ponto de descoberta de ficheiros-fonte)
  - src/backend/*.tks (isel_x86_64/arm64, encode_*, objfile_elf/macho/coff/ar*, minst*, regalloc*, abi_*)
  - scripts/fixpoint_gate.sh, scripts/degrau.sh, bootstrap/DEGRAU
---

# Feature-gating de backends por target (0.3.1)

## 0. O problema, medido, não suposto

Hoje **todo binário `teko` compila e carrega os quatro backends nativos inteiros** —
`isel_x86_64.tks` + `isel_arm64.tks`, `encode_x86_64.tks`(+`_consts`) + `encode_arm64.tks`(+`_consts`),
`objfile_elf.tks`/`objfile_macho.tks`/`objfile_coff.tks`(+ os três `_ar*`), `minst.tks` +
`minst_x86.tks`, `regalloc.tks` + `regalloc_x86.tks`, `abi_sysv64.tks`/`abi_win64.tks`/`abi_aapcs64.tks`
— **21198 linhas** só em `src/backend/`, independentemente de QUAL target o binário resultante vai
efetivamente emitir.

A razão é estrutural, não um descuido pontual: o descobridor de ficheiros-fonte
(`src/build/discover.tks::dsc_walk`, chamado de `project.tks:415`) caminha `src/` inteiro e
coleta **todo** `.tks`/`.tkt` sob a raiz `source` numa lista `[]SourceFile`, sem noção de "este
ficheiro só interessa ao alvo X" — todos entram no mesmo `parser::Program`, são resolvidos,
tipados e (quando o binário se autocompila) baixados pelo backend nativo juntos. O único filtro
que existe hoje, `prune_os` (`project.tks:116-131`), atua **depois** do parse, por **item**
(`parser::Function.os_guard`), e serve a um problema diferente e mais fino: variantes de UMA
função por SO (ex.: `host_write` vs. `_write`), não "este ficheiro inteiro pertence a outra
arquitetura."

Isso morde de duas formas concretas quando o **próprio compilador** é compilado pelo backend
nativo (o cenário do fixpoint `gen2 == gen3`, `docs/canonical/dev/self-host-fixpoint.md`):

1. **Superfície/memória infladas por construção.** Compilar o compilador para arm64-macOS não
   precisa nunca de baixar `isel_x86_64.tks`/`encode_x86_64.tks`/`objfile_elf.tks`/`objfile_coff.tks`
   — mas hoje baixa, porque esse código, mesmo sendo *sobre* x86_64/ELF/COFF, é só mais texto
   Teko no ponto de vista do backend arm64 que está compilando o compilador. O backend nativo
   "rebaixa TUDO isso dentro de si": mais unidades de tradução, mais estruturas de isel/regalloc
   em memória, mais superfície de crash — por um alvo que o binário resultante nem vai emitir
   nesta corrida.
2. **Bugs de UM backend bloqueiam o self-host de OUTRO, sem relação nenhuma com o alvo real.**
   `append_minst_x86` (`src/backend/minst_x86.tks:1219`, chamado de
   `src/backend/isel_x86_64.tks:50`) é código **inteiramente x86_64** — mas se o backend nativo
   arm64 tropeçar ao baixar (compilar) o CORPO dessa função como parte de compilar o compilador
   inteiro, o self-host em arm64-macOS trava por uma razão que não tem nada a ver com arm64.
   Isolar por target faz esse tropeço **inalcançável** em vez de precisar ser corrigido — a
   mesma filosofia já aplicada alhures na árvore (ex.: `teko-laws-digest.md`, "tornar um defeito
   inalcançável é melhor resultado que corrigi-lo").

O bootstrap/self-host **já opera, de fato, num regime host-only**: uma corrida de self-host
num host arm64-macOS só precisa que o binário resultante saiba compilar-se A SI MESMO nesse
mesmo host — nunca precisa emitir ELF x86_64 nem COFF Windows durante essa corrida. O gate hoje
não explora isso porque não existe mecanismo para "não compilar esse ficheiro nesta build".

## 1. Dois perfis

| Perfil | O que inclui | Para quê |
|---|---|---|
| **`host-only`** | Só os módulos de backend do **alvo do host que está a compilar** (isel + encode + objfile + regalloc + abi específicos desse alvo, mais tudo o que é target-agnóstico) | Self-host + o gate de fixpoint nativo em CI — cada perna mede só o que lhe compete |
| **`cross`** (full) | **Todos** os módulos de backend, para os quatro `NativeTarget` | Release/distribuição — o binário publicado precisa poder `teko build --target=<outro>` |

Um ficheiro do backend cai numa de três categorias:

- **específico de um alvo** (ex.: `isel_x86_64.tks`, `objfile_macho.tks`) — só entra em
  `host-only` quando o alvo do host coincide; sempre entra em `cross`.
- **específico de uma família partilhada** (ex.: `abi_aapcs64.tks` serve arm64-macOS **e**
  arm64-linux) — entra em `host-only` quando o host é QUALQUER membro da família.
- **agnóstico de alvo** (`regalloc.tks`, `minst.tks`, `dwarf.tks`, a baixa de LIR em
  `src/lir/lower.tks`) — entra sempre, nos dois perfis; é a "baixa partilhada" que
  `docs/memory/0.3.1-plano-sequenciado-por-plataforma.md` já nomeia como o que todo alvo
  aproveita de graça.

## 2. Mecanismo — como o manifesto/flag seleciona os módulos

### 2.1 Por que NÃO estender `#os(...)` para isto

`#os(...)` (`src/parser/parse_decl.tks:862-919`, `os_guard` no AST, podado em
`prune_os`/`project.tks:116`) já resolve exatamente este PROBLEMA em miniatura — mas na
granularidade errada para este caso: é um atributo **por função**, e a poda acontece **depois**
do ficheiro inteiro já ter sido lido, léxico e parseado. Aplicar essa mesma disciplina
função-a-função aos ~21 mil linhas de `src/backend/` significaria anotar **centenas** de
funções, uma de cada vez, com o risco real de esquecer uma (o "um `_ =>` engole um caso" já
documentado nesta árvore) — e mesmo perfeito, não evitaria o custo de lexer/parser sobre texto
que nunca deveria ter sido lido. **Não é o instrumento certo para "este ficheiro inteiro é de
outro alvo"; é o instrumento certo para "esta função tem uma variante por SO."** As duas
granularidades convivem sem se substituir.

### 2.2 O mecanismo proposto — uma tabela, não uma nova gramática

Em vez de ensinar o lexer/parser um novo atributo de ficheiro (risco de gramática, mais uma
superfície a manter), a exclusão vive **fora do texto Teko**, num ficheiro de dados simples que
o *driver* de build já sabe ler — o mesmo padrão que `bootstrap/DEGRAU` já usa deliberadamente
para uma decisão de build-system que não é superfície de produto (ver
`scripts/degrau.sh`'s próprio raciocínio: *"`teko.tkp` é PRODUTO... um ficheiro autónomo também
aparece no `git status` e no diff de um PR como uma linha de prosa que um revisor lê"*).

**`src/backend/BACKEND_TARGETS`** (texto simples, versionado, uma linha por ficheiro ou família):

```
# ficheiro:                    alvos que o usam (vazio/ausente = "shared", entra sempre)
isel_x86_64.tks:                x86_64-linux, x86_64-windows
isel_arm64.tks:                 arm64-macos, arm64-linux
encode_x86_64.tks:               x86_64-linux, x86_64-windows
encode_x86_consts.tks:           x86_64-linux, x86_64-windows
encode_arm64.tks:                arm64-macos, arm64-linux
encode_arm64_consts.tks:         arm64-macos, arm64-linux
minst_x86.tks:                    x86_64-linux, x86_64-windows
regalloc_x86.tks:                 x86_64-linux, x86_64-windows
abi_sysv64.tks:                    x86_64-linux
abi_win64.tks:                     x86_64-windows
abi_aapcs64.tks:                   arm64-macos, arm64-linux
objfile_elf.tks:                   x86_64-linux, arm64-linux
objfile_ar.tks:                     x86_64-linux, arm64-linux
objfile_macho.tks:                 arm64-macos
objfile_ar_macho.tks:               arm64-macos
objfile_coff.tks:                   x86_64-windows
objfile_ar_coff.tks:                 x86_64-windows
# minst.tks, regalloc.tks, dwarf.tks, abi comuns e tudo o resto de src/backend/ não listado
# aqui é "shared" por omissão — falha para DENTRO (inclui), nunca para fora (nunca some
# silenciosamente quando alguém esquece de o registar).
```

**Regra de omissão, e é deliberada (M.1):** um ficheiro que não aparece na tabela é `shared` —
entra em qualquer perfil, qualquer alvo. Um ficheiro novo de backend que ninguém registou não
desaparece silenciosamente de um build host-only (o que seria uma "falha barulhenta" só se
alguém for procurar); ele simplesmente continua a ser compilado em todo lado, exatamente como
hoje. A tabela só pode *reduzir* explicitamente, nunca *reduzir por esquecimento*.

Um ficheiro `_test.tkt` **herda o alvo do seu par `.tks`** — `isel_x86_64_test.tkt` segue
`isel_x86_64.tks`, sem precisar de entrada própria (mesma convenção "teste ao lado do código
que testa" já em vigor).

### 2.3 O gancho: `discover()`, antes de ler o ficheiro

O ponto de aplicação é `src/build/discover.tks::dsc_walk` (linhas 48-76), especificamente o
ramo que classifica uma entrada como ficheiro (linha 66-71: `if dsc_is_teko_source(nm) { out =
push(out, SourceFile {...}) }`). Uma nova checagem entra ALI, **antes** de o ficheiro entrar na
lista `[]SourceFile` que `project.tks` depois lê (`read_file`) e passa ao lexer:

```
if dsc_is_teko_source(nm) && backend_target_included(child, active_profile, active_targets) {
    out = teko::list::push(out, SourceFile { path = child; namespace = ns })
}
```

Este é o gancho que entrega o ganho real: um ficheiro excluído **nunca é lido, nunca é
lexado, nunca é parseado, nunca chega ao checker nem ao backend nativo** — o custo evitado é o
pipeline inteiro, não só a fase de emissão. Contraste deliberado com `prune_os`, que só evita
o *checking*/*codegen* de um item já parseado — aqui queremos evitar o parse em si, porque os
ficheiros em causa são grandes o bastante para o parse já ser o custo que importa.

### 2.4 Selecionando o perfil ativo

Uma nova variável de ambiente, com o MESMO escopo apertado que `TEKO_FIXPOINT_BACKEND` já
disciplina (`scripts/fixpoint_gate.sh`'s comentário: *"lives in THIS subshell, around THESE two
builds, and nowhere else"* — um pin global vaza para tudo por baixo e derruba cenários que
dependem do comportamento por omissão):

```
TEKO_BACKEND_PROFILE = host-only | cross      (omisso → cross, o comportamento de hoje)
```

Distinto, de propósito, de duas variáveis já existentes com quem não deve ser confundido:
- **`TEKO_BACKEND`** (`c` vs. nativo) escolhe a ROTA de emissão (C-transpile vs. backend
  próprio) — ortogonal: um build `host-only` ainda pode emitir via C ou nativo.
- **`TEKO_TARGET`** escolhe, EM TEMPO DE EXECUÇÃO, para qual `NativeTarget` o backend nativo
  já incluído deve emitir — ortogonal também: `TEKO_BACKEND_PROFILE` decide o que está
  **presente no binário**; `TEKO_TARGET` decide, dentro do que está presente, o que **correr
  agora**.

Um flag de CLI espelha a mesma escolha para uso fora de CI: `teko build --backend-profile=host-only`.
Se nem a flag nem a env var forem dadas, o manifesto pode fixar um perfil por omissão do
projeto — mas ver §5.5 sobre se isto deve ou não viver em `teko.tkp` (superfície de produto)
ou noutro sítio, no mesmo espírito do `bootstrap/DEGRAU`.

## 3. Como o dispatch `emit_native`/`NativeTarget` degrada

`NativeTarget` (`project.tks:2115`, `enum { Arm64Macho; Arm64Linux; X8664Linux; X8664Windows }`)
e o `match` exaustivo de `emit_native` (`project.tks:3037-3045`) **não mudam de texto entre
perfis** — esta é a propriedade central do desenho, e é o que evita ter de ensinar `match` a
"faltar um braço de propósito" (que não existe hoje e seria uma mudança de linguagem muito
maior do que este problema pede):

```
fn emit_native(...): i32 {
    match native_target(...) {
        NativeTarget::X8664Linux   => emit_native_x86(...)
        NativeTarget::X8664Windows => emit_native_win(...)
        NativeTarget::Arm64Macho   => emit_native_arm64(...)
        NativeTarget::Arm64Linux   => emit_native_arm64_linux(...)
    }
}
```

O que muda é **qual ficheiro fornece a definição de `emit_native_x86` etc.** Hoje as quatro
funções vivem juntas dentro de `project.tks` (linhas 3068, 3092, 3795, e a janela Windows). A
proposta separa cada uma para o seu próprio ficheiro, registado na mesma
`BACKEND_TARGETS`, com um par real/stub:

```
project_emit_x8664linux.tks:        x86_64-linux          # a definição real
project_emit_x8664linux_stub.tks:   !x86_64-linux          # o stub — o COMPLEMENTO do alvo real
```

`!alvo` na tabela significa "inclui exatamente quando `alvo` está EXCLUÍDO" — a regra garante
por construção que **exatamente uma** das duas definições de `emit_native_x86` entra no
conjunto de ficheiros descobertos, para qualquer perfil × alvo-do-host válido (uma checagem
estática barata: são só 4 alvos × 2 perfis, uma enumeração fechada de 8 combinações a verificar
uma vez, não algo que precise de prova geral).

O corpo do stub é honesto, não um `panic` nem um valor inventado:

```
fn emit_native_x86(dir: str, od: str, stem: str, prog: checker::TProgram, m: Manifest, debug: DebugInfo): i32 {
    teko::io::eprintln("teko: this binary was built with the `host-only` backend profile and does not include the x86_64-linux backend")
    teko::io::eprintln("teko: rebuild with --backend-profile=cross (or install the `cross` distribution) to target x86_64-linux")
    2
}
```

Mesma assinatura do original — o *stub* é uma implementação alternativa da mesma interface,
não um caso especial no chamador. `emit_native` nunca sabe, nem precisa saber, se está a
chamar o backend real ou o stub — a decisão já foi tomada na descoberta de ficheiros, antes do
parse. Isto preserva a M.1 (nunca falhar em silêncio: um alvo ausente diz exatamente que está
ausente e como resolver) e a M.3 (o stub não finge ter o backend — recusa-se, com uma
mensagem, exatamente como o compilador já se recusa noutros honest-stops da árvore).

`NativeTarget` continua com as quatro variantes em qualquer perfil — é só uma etiqueta pequena
(um enum sem payload) usada também por `target_from_name`/`[extern.libs.<os>]`, e mantê-la
inteira evita reabrir o parsing de `TEKO_TARGET`/nomes de alvo por perfil. O que o perfil
afeta é só se, ao tentar **emitir de fato** para um dado alvo, existe backend real ou stub por
trás do símbolo — nunca se o nome do alvo é reconhecido.

## 4. Invariância do fixpoint (`gen2 == gen3`) sob `host-only`

O fixpoint não depende de o backend nativo conter TODOS os alvos — depende de **gen2 e gen3
serem construídos com o MESMO perfil**, exatamente como hoje já depende de serem construídos
com o mesmo `TEKO_FIXPOINT_BACKEND` (`docs/canonical/dev/self-host-fixpoint.md`, "a fixpoint
begins where the compiler starts consuming its own output"). Sob `host-only`:

- `gen2` é gerado com `TEKO_BACKEND_PROFILE=host-only` — o conjunto de ficheiros-fonte
  compilados é o subconjunto host-only para o alvo do runner.
- `gen3` é gerado a partir de `gen2`, com o **mesmo** `TEKO_BACKEND_PROFILE=host-only` — o
  mesmo subconjunto, pela mesma tabela, para o mesmo alvo.
- A comparação byte-a-byte continua válida porque os dois lados comparam o MESMO programa
  (mesmo conjunto de ficheiros, mesma árvore tipada) compilado duas vezes seguidas — o perfil
  é uma propriedade de QUAL programa se está a comparar consigo mesmo, não uma variável que
  possa diferir entre os dois lados da igualdade sem invalidar a comparação.

**O que isto NÃO permite:** comparar um `gen2` construído `host-only` com um `gen3` construído
`cross` — isso compararia dois programas DIFERENTES (um contém 4 backends, o outro 1) e
qualquer divergência não provaria nada sobre o compilador, só sobre a diferença de conjunto de
ficheiros. A mesma disciplina de escopo apertado que já existe para `TEKO_FIXPOINT_BACKEND`
(ligada só ao redor exatamente das duas builds do gate, nunca vazando para o resto da suíte)
aplica-se identicamente a `TEKO_BACKEND_PROFILE`: o gate fixa-o UMA VEZ para as duas gerações
que compara, nunca deixa o ambiente ambiente escolher um valor diferente a meio.

**O ganho real, e é o que motivou este pedido:** com `host-only`, a perna arm64-macOS do
fixpoint deixa de precisar que o backend arm64 saiba baixar `append_minst_x86` (ou qualquer
outro código exclusivamente x86_64) — porque esse ficheiro simplesmente não está no programa
sendo comparado. Um stop nesse ficheiro deixa de bloquear o self-host arm64, porque deixa de
existir ali. O mesmo vale, simetricamente, para a perna x86_64-Linux não precisar de baixar
`objfile_macho.tks`.

## 5. Interação com bootstrap/seed/degrau

### 5.1 O seed e cada rung só precisam do backend do host

A cadeia de gerações (`self-host-fixpoint.md`) — seed → gen0 → gen1 (rota C) → gen2 → gen3
(rota nativa) — já é, por natureza, uma corrida **host-only** em todo o seu comprimento: uma
corrida de CI num runner arm64-macOS nunca precisa que NENHUMA geração dessa corrida emita
para x86_64-Linux ou Windows. A escada de degraus (`scripts/degrau.sh`,
`bootstrap/DEGRAU`) responde a uma pergunta ortogonal — *"por que o seed publicado não
consegue construir esta árvore"* — e essa pergunta nem menciona backend nativo por alvo; ela
é sobre capacidade de linguagem/codegen do seed em relação ao corpus, não sobre qual `NativeTarget`
está ativo. **O perfil de backend não muda o critério do degrau nem o formato de
`bootstrap/DEGRAU`.**

### 5.2 A rota C (gen0/gen1) não tem o mesmo problema, mas herda o mesmo benefício de menos texto

`gen1` emite C para a árvore inteira (incluindo `src/backend/*` como TEXTO a transpilar), e o
`cc` do host compila esse C sem se importar com QUAL arquitetura o C descreve — não há
"backend arm64 tropeçando ao baixar código x86_64" na rota C, porque quem baixa ali é o `cc`,
não o backend próprio. Mesmo assim, `host-only` ainda reduz o volume de C gerado e compilado
em cada rung — menos ficheiros para `gen1` emitir, menos para `cc` compilar, um ganho de tempo
de build mesmo onde o defeito de memória/crash-surface do nativo não se aplica.

### 5.3 Quem decide o perfil de CADA gen

Proposta: a escada e o gate de fixpoint usam `host-only` por omissão em TODA geração — não há
razão para uma geração intermediária do self-host conter os quatro backends. O binário que
precisa de `cross` é apenas o **último**, o que se torna o artefacto publicado (release) — se e
quando o projeto decidir que o binário distribuído deve poder cross-compilar (ver decisão §6.2).
Isto significa que a corrida de self-host produziria, no fim, um binário `host-only`, e um passo
FINAL e SEPARADO (fora da cadeia de fixpoint, que já terminou) recompilaria (ou recompilar-se-ia
a si mesmo, self-hosted) em `cross` para produzir o artefacto de release — dois artefactos
distintos de propósitos distintos, nunca confundidos: o binário que PROVA o fixpoint e o binário
que se PUBLICA não precisam ser o mesmo ficheiro, desde que ambos venham do mesmo código-fonte
tipado.

## 6. Decisões para o dono

1. **Granularidade do perfil.** Dois perfis bastam (`host-only` binário do host vs. `cross`
   tudo), ou vale um terceiro `custom` com uma lista explícita de alvos (útil para, por
   exemplo, uma perna de CI Linux que queira as duas arquiteturas Linux sem macOS/Windows,
   espelhando os "quatro pernas Linux" já tratadas como unidade em
   `0.3.1-plano-sequenciado-por-plataforma.md`)? Recomendo começar só com os dois e abrir
   `custom` só se um consumidor real precisar — YAGNI, mesmo espírito já aplicado ao
   `#isolation_group` descartado.
2. **O release fica `cross`, sempre?** Ou o projeto quer, por plataforma, um binário
   `host-only` menor como artefacto principal, com cross-compilation servida por um
   artefacto/distribuição separada (`teko-cross`, ou uma flag de instalação)? Isto é decisão de
   produto/distribuição, não só de engenharia — o documento assume `cross` para release por
   omissão (comportamento de hoje preservado), mas não deveria assumir sem confirmação.
3. **O CI usa `host-only` no gate nativo, a partir de já?** A motivação nomeada é
   precisamente destravar o gate de fixpoint que está preso — recomendo que sim, assim que o
   mecanismo existir, mas a sequência (mecanismo primeiro, flip do gate depois, ou os dois no
   mesmo vagão) é uma escolha do dono sobre risco/tamanho do PR.
4. **Onde vive a tabela de exclusão — `src/backend/BACKEND_TARGETS` (ficheiro autónomo, fora
   da gramática de `teko.tkp`) ou uma secção nova do manifesto (`[backend] modules = {...}`)?**
   Recomendo o ficheiro autónomo, pelo mesmo motivo que `bootstrap/DEGRAU` já não é uma chave do
   manifesto: isto é uma preocupação do BUILD-SYSTEM do próprio compilador (só o projeto `teko`
   tem múltiplos backends nativos embutidos), não uma superfície que um projeto de utilizador
   qualquer precisaria de tocar — pôr isto em `teko.tkp` ensinaria o parser do manifesto uma
   chave que existe para contornar uma particularidade deste único projeto.
5. **Nome e forma do lever de seleção de perfil** — `TEKO_BACKEND_PROFILE` +
   `--backend-profile` como proposto aqui, ou outro nome/formato? Peço ratificação explícita
   antes de qualquer implementação, já que colide em vizinhança de nome com `TEKO_BACKEND` e
   `TEKO_FIXPOINT_BACKEND` — vale o dono confirmar que a distinção de escopo (módulos
   presentes vs. rota de emissão vs. alvo ativo) ficou clara o suficiente para não confundir
   quem for operar os três.
6. **Cobertura sob `host-only`.** Excluir ficheiros do conjunto compilado muda o denominador
   dos pisos de `[coverage]` (menos funções/linhas/ramos totais). Isto é aceitável (a
   cobertura mede "do que foi compilado NESTA build", que já é a semântica de hoje) ou o gate
   de CI deve preferir sempre medir cobertura em `cross` (o superconjunto) para manter os
   números comparáveis entre pernas ao longo do tempo? Recomendo a primeira leitura (cobertura
   é sempre relativa ao que a build continha), mas é uma leitura, não uma decisão minha.

## 7. Alternativas consideradas e descartadas

- **Estender `#os(...)` para granularidade de ficheiro inteiro (attribute na primeira linha).**
  Rejeitada como mecanismo PRIMÁRIO por exigir nova gramática de lexer/parser (mais risco) para
  um problema que uma tabela de dados já resolve sem tocar em gramática nenhuma — mas registada
  aqui como alternativa válida se o dono preferir manter a seleção DENTRO do texto Teko em vez
  de num ficheiro de dados irmão. As duas abordagens não são mutuamente exclusivas: nada
  impede adotar a tabela agora e, mais tarde, se `#os`-like guards de ficheiro se revelarem
  úteis para outro caso, migrar.
- **Compilar quatro binários `teko` inteiramente separados (um por alvo), sem um perfil
  `cross` nenhum.** Resolveria a inflação de memória de forma mais radical, mas quebra a
  promessa de cross-compilation de um único binário instalado — rejeitada enquanto o produto
  quiser continuar oferecendo `--target=<outro>` a partir de uma instalação só.
- **Resolver isto inteiramente em tempo de link (deixar tudo compilar, mas não linkar os
  objetos não usados).** Não ataca o problema nomeado — o custo que dói é o do BACKEND NATIVO
  baixando/compilando o texto-fonte desses módulos como parte de compilar o compilador, uma
  fase que termina muito antes de qualquer decisão de linkagem.
