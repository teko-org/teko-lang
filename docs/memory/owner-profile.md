# owner-profile.md — como o dono delibera (para o agente sair de tensões)

Registro vivo das impressões sobre o **estilo de deliberação do dono** e dos **aprendizados**, para o
agente resolver tensões sobre o que já foi definido como fechado — sem reabrir o que está selado, e sem
pedir de novo o que já foi decidido. Versionado de propósito (pedido do dono).

## Idioma e papel (fatos persistentes do dono)
- Comunicação **sempre em PT-BR**; idioma regional **PT-BR**.
- **Timezone: `America/Sao_Paulo`** (UTC−03). Datas/horas e o "bom dia"/"boa noite" referem-se a este fuso.
- O dono é o **único merger de trunk** do teko-lang (compilador AOT all-native, self-hosting, escrito em
  Teko). O trabalho flui por **agentes**, **um reseed de cada vez**, cada um independentemente gated.

## Como delibera
- **Parte a parte.** Segmenta um tema (§10, item 14, …) e **confirma cada parte** antes de avançar. Não
  atropelar; não gravar sem o martelo dele.
- **Fork + recomendação, não survey.** Apresentar a superfície com opções (tabela) e **uma recomendação
  clara** (a recomendada primeiro). Evitar despejo exaustivo de alternativas.
- **Counter-argue antes de gravar.** Pesquisar o histórico/código, questionar, expor os forks **REAIS**.
  Quando o dono aponta uma inconsistência, ela costuma ser **genuína** (ex.: `RecBody = variant` vs §9.D).
- **Correção honesta, sem teimar.** Quando o agente erra, o dono empurra até acertar (structural traits:
  "cadê os Eq, Ord?"). Reverter a própria recomendação com **base firme** é bem-visto; insistir num erro
  não. Ele lê as letras miúdas e cobra precisão.
- **Grafia atual, não velha.** Não puxar grafia de artefatos antigos (ex.: `let`/`mut` do artifact de
  journaling — já retirados no §1). Sempre validar contra o compilador/decisões atuais.

## Princípios de design que o dono aplica
- **Explícito > mágica (no-shadow).** Se algo tem corpo, o dev vê e escreve. *Structural traits* (síntese
  oculta de `eq`/`hash`/…) foram **aposentadas** por violarem isso.
- **Não criar exceções.** *"Não causar exceções, já temos muitas."* Preferir **colapsar construtos**
  (3→2→1) a manter casos especiais.
- **Minimalismo de construto.** Sobrou **UM** construto de capacidade: a **interface** (tipo, dispatch,
  constraint, e obriga operador por contrato). O **trait-decorador** é mixin de código (achata, não é tipo).
  Nada de terceiro construto.
- **Precisão de mecânica de runtime.** O dono pensa em fat header vs thin transiente, `ref` = alias de
  slot, residência de arena (F1/F2). Registrar a **mecânica**, não só a superfície.
- **FFI é OPCIONAL, nunca obrigatório; erro tarde e no uso (ruling do dono).** Todo recurso da stdlib
  respaldado por lib externa (OpenSSL, GPG, SQLite, Oracle/ODBC, brotli/lzma/zstd, …) é **opt-in**: quem não
  usa **compila/linka/roda sem a lib**. **Erro só se o dev usar** o recurso cuja lib falta, e — **salvo alguns
  casos — link DINÂMICO → erro de RUNTIME** (símbolo ausente no load/1ª chamada), não de compilação. Refina o
  KEYSTONE-LINK ("não linkar o não-usado" → "falhar só no uso, dinamicamente"). Impl **puro-Teko** = default
  zero-dep; provider FFI = alternativa. E o padrão **"bindar, não reimplementar"** (OpenSSL/GPG).
- **CUIDADO com "sync/async" do dono no contexto cripto = simétrico/assimétrico de CHAVE, NÃO execução.**
  "sync" = **chave-única** (simétrica: AES/ChaCha/AEAD/HMAC); "async" = **par public/private** (assimétrica:
  RSA/EC/Ed25519/X25519). O provider deve cobrir **as duas famílias**. Não confundir com o modelo de execução
  `await`→`Intent` (§10.3), que é ortogonal. (Já errei isto uma vez — o dono corrigiu.)

## Sequenciamento
- **Destrava dependência primeiro.** Antecipar itens do caminho crítico (item 14 antes da Intent).
- **ADIANTAR pré-requisitos FORA da posição de origem — proativamente (ruling do dono, aprendido a duras
  penas).** Um item sequenciado "por último" na ordem nominal deve ser **puxado para frente** quando é
  pré-requisito de outro. Caso concreto: **§14 (macro/comptime) ANTES do §9.D** — o sweep do §9.D (~2000
  sítios) só é sustentável com `@Type()`, que é §14. Disparei o §9.D antes do §14 → o implementer HALTOU
  (premissa falsa). O dono: *"tu tem itens de outras posições que precisam ser adiantados, como 14 macro
  antes de 9.D, isso era claro para mim."* Lição: mapear a cadeia de pré-requisitos e **re-sequenciar sozinho**
  (a ordem nominal do §11/§12 não é sagrada quando um item destrava outro).
- **Adiar alto-churn pro fim.** `exp`/`pub` (visibilidade) **por último** — muitas falhas a corrigir, e a
  grafia é forward-compatible, então atrasá-la minimiza retrabalho.
- **Design-ahead pro arquiteto.** Passar itens abertos/definidos pelo **arquiteto** e **versionar** o
  retorno, para depois **argumentar e definir** (o dono decide; o agente prepara o material).
- **Nada de N agentes em paralelo com reseed** — gera conflito de reseed. Design-only (arquiteto) pode
  paralelizar; reseed é um de cada vez.

## Segurança (regra-mor, aprendida a duras penas)
- **NUNCA rodar `teko test` em forma alguma** — há um leak no `monomorph` (item 13) que **derruba o
  container inteiro**. Nem guardado com `ulimit`/`timeout` em subagentes. Foi a causa de restarts
  repetidos. Única exceção histórica: um check `ulimit -v ~3G + timeout` **autorizado pelo dono**, só na
  sessão principal, nunca em implementer.
- **Build seco:** `TEKO_BACKEND=c <compilador> <dir> -o <FRESCO> --no-verify --release` (o `--no-verify`
  descarta o parser de `.tkt` de propósito). O default do compilador é **native** e não emite `teko.c` —
  fixar `TEKO_BACKEND=c` para o fixpoint.
- **⚠️ TETO DE MEMÓRIA NO BUILD (aprendido em 2 crashes seguidos).** O `--no-verify` **NÃO** pula o estágio
  `monomorph` (só pula o *verify*), então **o build seco pode vazar e derrubar o container** (16 GB) sem um
  teto. Todo build de agente vai num subshell com cap: `( ulimit -v <KB>; export TK_RT_DIR=...; TEKO_BACKEND=c
  … --no-verify --release )` — um leak estoura o teto e mata **só o processo**, não o container. Se um build
  bate no teto → o agente **PARA e reporta**, não re-tenta (leak não se resolve no braço).
- **Commit a cada crumb (protege contra restart).** O container reinicia; trabalho **não-commitado se perde**
  (o reseed do `.{}` perdeu o R1 duas vezes por não commitar). Commitar cada crumb assim que fica **verde**
  (build+fixpoint OK), nunca estado meio-crumb.
- **Reseed:** cc seed `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
  src/assert/assert.c -lm -o gen0`; `TK_RT_DIR` setado; `-o` fresco (cache velho segfaulta self-build);
  fixpoint **byte-idêntico**; **gate independente** após cada reseed.
- **Sweep de `.tkt`/`.tkr` obrigatório** após mudança de AST/campo/assinatura/enum — `--no-verify` não
  compila `.tkt`, então quebras ficam caladas até o CI (§9B/§9C quebraram 32 sítios assim).

## Git / registro
- Onda drena em **`fix/retirement`**, **sem PR**, **forward-only** (force-push desabilitado); fetch+rebase
  antes de push.
- **RESTRIÇÃO (ruling do dono 2026-08-15): todo trabalho drena DIRETO na umbrella `fix/retirement`, SEM
  abrir PR novo** — o PR umbrella já cobre a onda. Um agente em branch isolada **entrega ali** e o
  coordenador **dreno por ff/merge** na `fix/retirement` (nunca abrir PR por crumb/feature). **Nova branch
  SEMPRE se baseia no ÚLTIMO HEAD de `origin/fix/retirement`** (fetch primeiro → `git worktree add -b
  <fresco> origin/fix/retirement`), nunca de um ponto antigo — senão o drain diverge. Regra dada após eu
  abrir o PR #118 por engano (fechado) em vez de drenar `feat/shape-constraint-solver` por ff.
- Commits: **English Conventional-Commits**.
- Decisões vivem no **Doc 2** (`docs/design/mudancas-superficie-0.3.1.md`, a lei) + conversa; atualizadas
  **deliberadamente**.
- O dono **reverte o checkout local de propósito** às vezes (deixa greps em árvore velha). A verdade é
  **`origin/fix/retirement`** — sempre grepar/re-sincronizar contra ela (`git reset --hard`).

## O que está FECHADO (não reabrir sem informação nova)
- **§9.A–F**, **match-universal fase 1**, **properties** — reseeded.
- **Traits = decorador achatável** (não-tipo; só métodos-com-corpo; sem campos; contrato estrutural
  `_nome`↔`nome`; `&`-composição em class/struct/service; colisão = erro salvo sobrecarga; sem match sobre
  trait). **Structural aposentadas.** **Constraint = interface-only.** Capacidade via **interface que
  obriga operador** + **counter-part** (igualdade por negação, ordem por reflexão).
- **match `as` = alias-ref transparente** (readonly raso: variável não re-vincula, props/métodos internos
  transparentes; mutabilidade herda do subject; sem sintaxe nova).
- **§10 inteiro** (spawn/chan MPSC/await/Intent/journal). **Intent protegida** — struct puro, backing
  privados, `exp get`/`pub set`, `pub static new`, campo do valor = **`value`**; exp/pub forward-compatible.
- **item 14** — tudo `var`; `val` só marca interna (alias de match + literais); parâmetros = `var`; `self`
  = ref implícito (mutação gruda, método mutante inferido); fat header (struct) vs thin transiente (subtipo
  primitivo/enum/flags, readonly `val-ref`); **`readonly` opt-in** (struct inteira estilo C# e campo; só
  definível na construção/default).
- **§9.D — `type X = variant` aposentado** (ruling do dono). NÃO vira wrapper (`struct { case: … }`) nem
  alias (`type X = A | B`): a única forma-soma é a **união `|` estrutural inline, escrita POR EXTENSO** em
  cada campo/param/retorno/var. O agregado nomeado some; **os membros seguem nomeados** (`match … as` e
  coerção membro→união inalterados, byte-idênticos). Recursão fecha pelo **box que a própria união já emite**
  (`{tag;ptr;len}`). **Array-de-união exige parênteses** — `[](A | B)` (`[]` liga mais forte que `|`).
  **Verbosidade é o preço, aceito** (verbatim: *"Vai ser verboso mesmo, e isso não é problema, é o preço."*),
  **mas abreviável por MACRO** (Família A: `macro Type() { lowering { (A | B | …) } }` + `[]@Type()`) — a
  macro **alarga a AST pré-typecheck**, então não é alias nominal (o proibido continua sendo alias/wrapper/
  newtype *nominal*, não a macro sintática).
- **§4.1 — construção: três formas (ruling do dono).** `Tipo { … }` nominal = **sempre válida** ("à gosto do
  cliente"); `.{ … }` **target-typed** = válida onde há **tipo-alvo conhecido** na declaração (retorno
  anotado incl. `(): self`, var anotada, param, campo), **sem alvo = ERRO**; `self { … }` **construtor
  REMOVIDO** — `self` fica só **receptor** (`self.x`) e **tipo** (`(): self`). Retirar o `self{}` **dissolve**
  o bug bare→canônico (o `self{}` gerava `Named` bare, o tipo `self` resolve para o qualificado, `type_eq`
  compara string exata) — em vez de consertar, remove-se. `.{}` é a construção que materializa o cabeçalho fat
  do item 14.

## Aprendizados sobre sair de tensões
- Tratar o **fechado como vinculante**; não re-litigar decisão tomada.
- Tensão entre **artefato antigo e decisão selada** → a **decisão selada ganha** (ex.: RecBody/variant →
  união inline no campo).
- Antecipar-se em **dependência**, mas **não decidir sozinho** bifurcações de superfície com impacto —
  essas são do dono; trazer fork + recomendação.
- Reverter local sem drama; a verdade é `origin`.
- O dono confia no agente para **sair de tensões** sobre o que já foi conversado — este perfil é a rede.

## Aprendizados desta sessão (§9.D + arquitetos)
- **A solução do dono costuma ser mais enxuta que a do arquiteto.** No §9.D o arquiteto trouxe wrapper /
  newtype / classe selada (Soluções A/B/C); o dono cortou tudo com uma **união inline por extenso**
  (*"Tão simples e o arquiteto gastou tokens pra… nada"*). Lição: **não sobre-engenheirar**; quando houver
  uma via que evita nominalidade/abreviação/wrapper, ela costuma ser a preferida — mesmo que verbosa.
- **O valor do arquiteto é a RECON, não a forma.** O aproveitável do §9.D foi o **fato descoberto no
  código** (a `variant` já é descritor fat `{tag;ptr;len}` com payload boxed → a recursão fecha sem virar
  referência), não a recomendação de representação. Despachar arquiteto para **levantar fatos + 3+ opções
  com exemplos**; esperar que o dono escolha uma quarta via mais simples.
- **Verbosidade > nominalidade oculta.** O dono aceita repetir a união em centenas de sites para não ter
  um alias/tipo-soma nominal escondendo mágica. Explícito ganha de conciso quando conciso = ocultação.
- **Artefato de avaliação = código real do codebase.** Quando o dono pede material para avaliar, ele quer
  **onde e como** algo é usado hoje, com snippets reais (não abstrações) e a solução confrontada com cada
  padrão de uso. Levantar do `origin`, por posição de declaração (campo/param/retorno/var).
- **Apontar o furo com honestidade.** O dono valoriza quando o agente **nomeia o furo comum** de todas as
  opções (ex.: auto-recursão exige indireção — é teorema) em vez de vender uma recomendação.

## Aprendizados desta sessão (construção `.{}` + memória + precisão)
- **Ler as palavras do dono ao pé da letra.** *"Stdlib é 9-ops"* era *"Stdlib **e** 9-ops"* (dois itens
  distintos, não identidade). Não inflar uma conjunção em tese. Quando o dono corrige a grafia de uma
  palavra, a diferença é semântica e importa.
- **🚨 UM RESTART = VAZAMENTO DE MEMÓRIA = INCIDENTE (ruling do dono).** Um restart do container **nunca é
  normal**. Significa **uma de duas coisas**: (a) algum agente ou comando executou **algo fora dos padrões**
  (build sem `ulimit`, backend `native`, sem `--no-verify`, `teko test`, builds em paralelo), ou (b) o leak
  do `monomorph` (item 13) **precisa de correção imediata**. Ao ver um restart: **parar, identificar o
  agente/comando culpado, e ou blindar (teto de memória) ou consertar o leak** — não re-disparar cegamente.
  Todo build de agente é **obrigatoriamente** guardado por `ulimit -v` (teto tight, ~6 GB sobre os 3.5 normais)
  e **para+reporta** ao bater no teto (o OOM localiza o construct que dispara o leak = achado para consertar).
- **A memória é a restrição dura, não só o `teko test`.** O build seco **também** vaza (via `monomorph`) e
  derruba o container sem `ulimit`. Toda invocação de build de agente vai com teto de memória, um build de
  cada vez. Dois crashes seguidos ensinaram isto.
- **Investigação de memória (baseline blindado, `ulimit -v` + monitor de RSS).** A árvore atual builda
  **limpa a ~3.5 GB RSS** (exit 0, limitado) — o baseline **NÃO vaza**. Cresceu de ~2.8 → ~3.5 GB com a
  onda-das-traits (mais código de compilador = mais RSS; **crescimento limitado, não leak**). Logo o teto
  certo é **tight (~5 GB, o normal ≈3.5)**, **não** alto: **10 GB era acomodar o leak, não investigá-lo**
  (ruling do dono — *"algo aumentou o vazamento e precisa de investigação e correção"*). O crash foi
  **ausência de `ulimit`** + provável caminho novo (`.{}`) tocando o leak do `monomorph`. Com teto tight, um
  leak vira **OOM diagnosticável** (localiza o construct) em vez de derrubar o container — o teto é
  **instrumento de investigação, não muleta**.
- **Distinguir propriedade de RUNTIME de propriedade de COMPILE-TIME (correção ODBC).** A bitness ODBC
  `_32`/`_64` depende do **driver instalado**, não do CPU (o processo é x64, o driver pode ser só-32-bit) →
  é **par de funções por sufixo** (`connect_32`/`connect_64`), **não `#arch`**. Quando o dono corrige uma
  proposta, a nuance costuma ser exatamente essa: runtime/instalado ≠ compile-time/arquitetura.
- **Após 2 falhas iguais, checar antes de repetir.** Não re-despachar cegamente um reseed que travou duas
  vezes; blindar (teto de memória, commit-por-crumb) e **confirmar com o dono** antes da terceira tentativa.
- **Ordem de reseed por segurança quando delegada:** aditivo/**byte-idêntico primeiro** (o gate byte-idêntico
  é a rede mais forte — se o `teko.c` sai igual, não há regressão), **mudança de layout invasiva por último**
  (item 14 fat header muda `teko_rt.c`+codegen+layout, byte-identidade não se mantém). §9.D migração é
  source-only byte-idêntica → das mais seguras; ortogonal à construção.
- **O dono pede artefato para avaliar e argumentar.** Status, catálogos, confronto de opções — montar como
  **artefato** (não só texto), com dados reais do `origin`. O **report grande é só quando ele pedir** (ele
  avisou: "amanhã de manhã") — não construir proativamente.
- **Higiene de worktrees por regra, não caso-a-caso.** Limpar as **drenadas+limpas** direto; as **DIRTY /
  NOT-drained** só **sem agentes** (build seco concorrente com reseed compete por memória) e com **build seco
  de confirmação** antes de drenar. Nunca mexer nas worktrees de trabalho paralelo do dono (`cargo/*`,
  `theory/*`, `native/*`).

## Lições da wave 0.3.1 — 2026-08-14 (sessão autônoma noturna)

- **Container é orquestração, NÃO build.** PROIBIDO `teko test .` e `TEKO_MEM_PARANOID` locais (derrubam o container). Build/fixpoint GUARDADO plano (`ulimit -v 6291456`, ~3.6-4.3GB, sem test/mem-paranoid) é a fronteira tolerada. Toda validação (fixpoint, testes, mem-paranoid, regressões) é via CI no PR #110.
- **Reseed é validado por CI (`teko test .` + fixpoint), NÃO por guarded build.** O build `--no-verify` não compila os `.tkt`; drift de teste, match não-exaustivo sobre variante nova, e gaps de lowering nativo só afloram no `teko test .` do CI. Disciplina: cada crumb que adiciona/muda variante faz grep+update de TODOS os sites de match (produto `.tks` E teste `.tkt`). Provado 4×: Env drift (c0f5e780), len, §14-match, native-gaps.
- **Não assumir "pré-existente" sem ler o log.** Descartei o match não-exaustivo do §14 como "len pré-existente" — era regressão nova. Ler, não assumir.
- **Memória: era inflação de PICO (sobre-alocação transitória), não leak.** O `mem-paranoid` mede arena não-liberada → correu limpo em todos os passes pesados (2×). Os restarts eram pico do 9-ops (`value_op_owner` alocando no caminho quente) + execução local pesada. Fix: `methods_declare_operator` alloc-free.
- **False-green da rota-C.** O Probe D corria `cc` incondicionalmente, mascarando honest-stops do backend nativo. O gate estrito (falha-se-há-C) desmascara o backlog `.32` do subset N1/N2 — a native-test-lane vermelha é WIP-conhecido por design, não regressão. O gate `mem-paranoid` conflaciona Q1(memória, sem leak) com Q2(completude nativa, WIP).
- **Coordination hazard:** worktrees que compartilham o ref `fix/retirement` movem o main tree. Agentes design usam worktree ISOLADO (`-b <fresh> origin/fix/retirement`); folds via detached-worktree merge+push sem tocar o ref compartilhado.

## Aprendizados desta sessão (falsos-forks §9.D final + recuperação de contexto)
- **Nem todo "fork" é fork — filtrar ANTES de escalar.** A sessão filha travou os 2 últimos ADTs (`TStatement`/`Type`) alegando "2 design calls"; ambos eram falsos. Q1 (união-de-uniões) cai do `union_collect` (achata+dedupa) + o splice `@X()` já provado por `ItemKind` (`ast.tks:944`, que chama `@Statement()` no corpo). Q2 (duplo-null) cai do swap superfície→backend no front-end. O dono farejou na hora (*"me parece simples demais para o agente ter deduzido que é fork"*). Lição: re-investigar no código se a bifurcação não é consequência do que já está deliberado, ANTES de gastar o dono.
- **`null`/`error` superfície ≡ `Null`/`Error` backend — mesmo elemento, não família nova.** O swap é no lexer/resolve (`lexer.tks:357`, `resolve.tks:2032`); no backend só há a forma de backend. Repetição DEDUPA sem erro (nunca "colisão"). Não inventar marcador/`NullType` — seria família onde não há.
- **"Se a ferramenta não existe, ensina" — mas checar se já existe.** O dono manda usar as ferramentas deliberadas e, se faltar (ex. macro-chama-macro), implementar. Antes de assumir que falta: `ItemKind` já chama `@Statement()` no corpo → macro-em-macro já funciona. Verificar o precedente antes de "ensinar".
- **Após perda de contexto, RECONECTAR antes de alarmar.** Commits inesperados em `fix/retirement` eram de uma SESSÃO FILHA que eu mesmo abri e esqueci (compactação). Checar `list_sessions mine=true` por `parent_session_id` = esta ANTES de tratar como corrente externa. Não assustar o dono com "trabalho paralelo desconhecido".
- **Não sair fazendo enquanto o dono ajuda.** Quando o dono está no meio de me ajudar a diagnosticar, NÃO despachar/executar — ele quer entender e decidir primeiro. Despachar arquiteto no meio disso foi erro (ele matou). Executar só com o verde explícito (*"registre e despache"*).
