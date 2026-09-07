# Teko — Decision Log

Registro de decisões tomadas de forma autônoma durante a execução do backlog, para
**revisão posterior do dono** (diretriz 2026-07-05: aplicar o recomendado sem travar o
fluxo; registrar aqui para revisar de uma vez no gate **LTS = v1.0.0.0**).

Cada entrada traz: a **decisão aplicada**, as **alternativas** preteridas (com motivo), a
**base** (constituição / lei / diretriz — lembrando que estamos na **fase de EVOLUÇÃO**,
pós-reboot) e a **reversibilidade**. Decisões marcadas **⚠️ PENDENTE** aguardam a revisão
do dono especificamente.

Constituição: Laws M.0–M.5 (ver `TEKO_MASTER_PLAN.md` / memória `project-structure`).
Lei suprema operacional: **main-integrity** — nunca mergear para main algo potencialmente
corrompido; só CLEAN.

---

**Cut, 2026-09-06 (D1 of `docs/design/plano-docs-site.md`, in `docs/history/`):** entries
D1-D210 record the retired standalone Teko compiler (D211/D212) and move, intact, to
`docs/history/decision-log-legacy.md`. This file keeps D211+, the port of Teko onto `mc`.

---

### D231 · Split do manifesto na raiz — `mc.toml` = só `[package]`, `teko.toml` = a build; `module =` REMOVIDA; lib `teko` / tool `tekoc` (passo 4 do rebase, 2026-09-06) 📦 PACOTES
- **Decisão aplicada:** a raiz passa a ter DOIS manifestos. `mc.toml` carrega **só `[package]`** (`name`/`lib`/`files`/`check`) e um cabeçalho curto — **sem `[project]`**, que é o que faz o registro classificar o nome como BIBLIOTECA (D230 adendo 3). `teko.toml` (novo) carrega a build: `[project]`/`[target]`/`[compiler]`/`[linker]`/`[limits]`/`[include]`, lida por `mc build . --config teko.toml`. **Causa:** `mc pkg hash DIR` lê `DIR/mc.toml` e não aceita `--config`, logo os dois papéis não cabem no mesmo ficheiro quando o pacote É o repositório. Precedente exato: a raiz do `minicompiler/mc`, onde `mc build .` também responde `missing key: project.entry` de propósito.
- **`module =` REMOVIDA:** é chave que o `mc` e o registro ignoram (D230); mantida como "documentação" ela só engordava o hash. O que ela dizia está no `HANDOFF.md` §3.3.
- **Regra dura do `--config`:** relativo, **sem `./`**, na RAIZ do pacote — o `mc` resolve todo caminho contra o diretório do config, e esse diretório é a raiz que o guard de `internal` mede (D224). `./teko.toml` cega o guard; `cfg/teko.toml` não acha `core_teko.mc`.
- **`[include]` é chave de BUILD** → foi para o `teko.toml`. O pacote continua fechado sobre `files`: nenhum `#include` da árvore depende dele (fixtures usam `#include "../lib/rt.tk"`, módulos incluem irmãos pelo nome).
- **Nomes ratificados pelo dono (2026-09-06), fechando o fork g4 do D230 adendo 3/4:** a **lib é `teko`** (esta raiz, sem `[project]`); a **ferramenta é `tekoc`** (`kind = "exe"`), com manifesto próprio num **subpath** deste repositório, em crumb próprio. Este passo NÃO cria o manifesto do tool.
- **Efeito colateral esperado:** o hash de árvore muda (`6e6bb5df…` → `8dd22e12…`), sem consequência antes da 1ª publicação. Como ele mudaria de qualquer forma, entrou junto a limpeza dos 30 comentários que diziam `ngen/…` em 19 ficheiros de código (`--dump-ast` das 45 fixtures byte-idêntico).
- **Reversibilidade:** alta — é movimentação de chaves entre dois ficheiros; o `release.yml` continua lendo `check`/`lib` do `mc.toml`.

### D230 · VALIDADO com o mc: o modelo de pacotes NÃO muda para a teko agora; distribuição = pacote `teko` (fonte) + duas estradas para o compilador ensinado (dono pediu validar, 2026-09-06) 📦 PACOTES
- **Fato (sessão do mc, NOTICES-teko 2026-09-06):** o modelo publicado (`docs/reference/packages.md`, 0.15.8: lock, MVS, hash de árvore, `[package] name/lib/files/check`, registro, `mc pkg check`, `mc` reservado) **não muda**; a 0.15.9 só troca o registro padrão para `https://pkg.minicompiler.dev`. O "redesenho" é DESENHO em spec (amendment M44 passo 6 + M47 §27, alvo pós-S7, sem versão): (a) `lib/` do mc como pacote `mclib` (asset de release, ADIÇÃO — o blob continua); (b) ferramentas como pacotes `[project] kind="exe"` + `[[tool]]`, `mc tool install`; (c) biblioteca = `kind="obj"`; (d) `[[permission]]` (fs/net/exec/env) validado pela caixa e confirmado na instalação, gravado no lock; (e) site à la NuGet.
- **Para a teko, nada quebra:** `<mc/core_min>`/`<mc/host>`/`<float>`/`<sys>` seguem no blob com a MESMA grafia; `[compiler] core/modules` iguais; `[package] name="teko" lib="lib/rt.tk" files check` corretos. **`module=` NÃO é chave do mc nem do registro (ignorada)** — fica como documentação até haver semântica (seria pedido). Self-host S4.2 (`mc_teko.tk`) não muda.
- **Distribuição (decidido):** o usuário crava `[deps] teko = "x.y.z"` e faz `#include <teko>` (runtime como fonte); para o COMPILADOR ensinado: **(i) hoje** `[compiler] modules = ["<teko/teko.tk>", "user.mc"]` no projeto do usuário (o `mc build` dele constrói o `teko` localmente, cravado pelo hash); **(ii) depois** (M44 passo 6) `teko` como pacote-ferramenta `kind="exe"` + `[[tool]]`, `mc tool install teko`. Publicação só em versão estável (D-anterior do dono).
- **Cuidados registrados:** nomes `mc*`/`minicompiler*`/`deps`/`build` reservados; `files` contido; as unidades de `check` compilam SOZINHAS na caixa linux/x86_64 do validador, sem rede — **DÍVIDA a fechar antes de publicar:** `check=["teko.tk","lib/rt.tk"]` — `teko.tk` não é unidade autônoma (precisa de `<mc/host>` + partes); provável trocar por uma unidade que inclua o compilador inteiro (`mc_teko.tk` da S4.2 é candidata); `[project]` no manifesto raiz é construído e RODADO pelo validador (o `hello.tk`).
- **Adendo (mc, 2026-09-06):** a unidade de `check` é compilada pelo **`mc` DE PRATELEIRA** do registro (linux/x86_64, sem módulo teko carregado): `<mc/host>` = host da caixa, `<mc/core_min>` = bundle desse mc. Logo a unidade só passa se for escrita na **superfície do NÚCLEO** — `mc_teko.tk` serve enquanto os módulos `.tk` forem transliteração (núcleo puro). **Consequência para o fork g3 (§64):** teko-ificar os módulos do compilador com sintaxe que só a teko ensina (S4.4+) tornaria o pacote INVALIDÁVEL pelo registro (`check` recusado pelo parser de prateleira) — a menos que a unidade de `check` seja outra (só o runtime `lib/rt.tk`, se ele ficar em núcleo) ou o registro passe a aceitar compilador ensinado. Pesa a favor de manter o compilador em núcleo (transliteração) e teko-ificar só o que o usuário consome. Dono decide no g3.
- **Adendo 2 (mc, 2026-09-06 — registro OPERA):** `pkg.minicompiler.dev` publicou os primeiros pacotes (`mc` 0.15.6..0.15.12), fluxo real (token da org → poll → checkout da tag → hash → `check` compilado na caixa → row). Regras que valem para o `teko` v0.1.0: **(1) o manifesto do pacote tem que estar na RAIZ do repo** (subpath só depois do R1 deles) — logo `[package]` sai de `ngen/mc.toml` e vai para um `mc.toml` na raiz com `lib`/`check`/`files` apontando para `ngen/...`; `ngen/mc.toml` fica só com `[project]`/`[compiler]`/`[target]`/… para o `mc build ngen`; **(2) sem `[project]` no `mc.toml` do pacote** (a 2ª caixa roda sobre a árvore vendorada = mc.toml + files; `kind` vira propriedade do nome na 1ª publicação, `exe` = tool para sempre); **(3) o validador usa mc PINADO** (subindo de 0.15.4 para 0.15.12 hoje); proposta deles: `[package].mc = ">= x"`. Pinar também o nosso CI.
- **Adendo 3 (registro R1 em produção, mc 2026-09-06 18:50Z):** o validador aplica a regra do KIND — `[project]` ausente ou `kind = "obj"` = **biblioteca**; `kind = "exe"` = **ferramenta** (tool); `[project]` presente SEM kind = recusado; **o kind fica FIXO pelo nome na 1ª publicação**. Valida `[[permission]]` e `[tools]`; rows ganham `kind`/`bin`/`licence`/`permissions`/`tools`; `[package].test` lido, ainda não rodado. Subpath (`ngen/`) existe via admin (`register subpath=ngen`) — pedir quando o manifesto estiver pronto; pino do validador = 0.15.12. **FORK ABERTO ao dono (g4):** `ngen/mc.toml` hoje tem `[project] kind = "exe"` → publicado assim, `teko` vira TOOL para sempre e deixa de servir a `[deps] teko` + `#include <teko>` (a estrada (i) do D230). Precisa de dois nomes: a **biblioteca** (runtime `lib/rt.tk` + módulos, sem `[project]`) e a **ferramenta** (o binário `teko`, `kind = "exe"`, `mc tool install`). Proposta: lib = `teko`; tool = `tekoc` (ou `teko-cli`). Decidir ANTES da 1ª publicação; o rebase do `ngen/` para a raiz (org) já separa manifesto do pacote (sem `[project]`) e config de build.
- **Adendo 4 (mc, 2026-09-06):** `[tools]` é permitido em biblioteca (resolve no MVS do consumidor, row `kind = "tool"` no lock, instalada por `mc tool install` = C3 do mc, pós-1.0.0; até lá a estrada (i) `[compiler] modules` é a única que roda e segue válida depois); `bin` = `[package].bin` da tool (ausente = basename de `[project].out`); **lib `teko` sem `[project]` + tool `tekoc` `kind = "exe"` é o desenho confirmado pelo mc**; registro na raiz quando a URL pública (org) existir. Falta só a ratificação do dono (g4).

### D229 · DONO: Dependency Injection resolvida em TEMPO DE COMPILAÇÃO — `IServiceSingleton`/`IServiceScoped`/`IServiceTransient`; Singleton recebe Scoped (tem escopo próprio) (dono 2026-09-06) 🔧 SUPERFÍCIE / roadmap obrigatório
- **DI em comptime não pode ficar fora do roadmap.** O dev marca a implementação das suas classes como
  `IServiceSingleton`, `IServiceScoped` ou `IServiceTransient`; **no ato da compilação** faz-se o registro
  dessas classes, e a injeção é **resolvida em comptime** (sem container de runtime, sem reflexão).
- **Diferença da teko para o C#:** um **Singleton PODE receber a injeção de um Scoped**, entendendo que o
  Singleton tem o **seu próprio escopo**. Um **Transient** ou **herda o escopo** (de Scoped ou Singleton) ou
  abre o próprio se não existir nenhum — caso que o dono julga impossível (transient é sempre invocado dentro
  de um dos outros dois escopos).
- **Architect-first** (dono: "talvez precise de um arquiteto"): a preocupação é a compilação por arquivo do
  mc. Fato registrado pelo coordenador: o mc compila o programa como UMA unidade (sem TUs; `internal` = dir
  do `mc.toml`), então um `pass()` enxerga todos os registros — a resolução comptime é um pass sobre a
  árvore inteira. O arquiteto define: forma da injeção (construtor, como C#), o que ABRE um escopo, a
  tabela de registros, os erros (implementação faltando/duplicada, ciclo), e a interação com o RC (D227).
- Entra na fila depois do bloco §50 (ordem livre / herança de interface / `T[]` global), antes da
  auto-hospedagem (D225).

### D228 · DONO: operador ternário `c ? a : b` entra na superfície; a `switch` expression é açúcar sobre ele (dono 2026-09-05) 🔧 SUPERFÍCIE
- **Ternário `expr ? a : b`** é bem-vindo — "como está usando o mesmo construto de fluxo do mc"
  (dono, ao ver o desenho do hoist para a `switch` expression). Mesma precedência/associação do C#
  (à direita, abaixo de `||`).
- **Mecanismo (coordenador, sob D226):** o núcleo do mc não tem controle de fluxo em posição de
  expressão, então o ternário rebaixa por **hoist num `pass()`**: o handler infixo devolve um
  placeholder; o pass insere antes do statement envolvente `T __tN; if (c) __tN = a; else __tN = b;`
  e troca o placeholder por `__tN` (braços preguiçosos; `T` pelo oráculo, os dois braços do mesmo
  tipo). Limite conhecido: a avaliação de `c`/`a`/`b` é hoistada para antes do statement.
- **`switch` expression (D222) = açúcar sobre cadeia de ternários** (`x switch { 1 => a, _ => b }`
  ≡ `x == 1 ? a : b`; `when` = `&&` na condição) — nenhuma máquina própria; o `switch` statement
  segue o D222 (loop de uma volta + `if`/`else`).
- Ordem na fila: ternário ANTES do `switch`.

### D227 · COORDENADOR (sob D226): reclaim — RC e posse resolvidos SÓ no pass; vtable com release na palavra 0; refcount@+8 de fato reservado (2026-09-05) 🔧 MEMÓRIA / decidido em modo autônomo
Ao implementar o reclaim (free lists + RC por escopo, ctor/dtor — D218), o crumb mandava injetar
no PARSE como o `lx` e parar se parse e pass divergissem. Mediu-se que não dá: (1) `on_jump` dá
profundidade de BLOCO, não de laço — `loop` é keyword do core e `word_add` recusa sequestrá-la
(`mc/src/hooks.mc:237-241`), então não há marca de laço para `break N`; (2) a POSSE (`own(e)`)
depende do tipo estático, e o `.` deferido é placeholder sem tipo no parse — chutar seria UAF ou
vazamento silencioso. **Decisão (a que a sessão do mc já recomendara, plano §23): escopo de RC e
posse têm UM dono, o pass (`tk_rc_pass`, registrado por último, depois do mangling do C4); a pilha
do parse só resolve `.`. Nada híbrido.** Store só marca no sítio; o pass decide `rt_store` vs
`rt_store_own`; valor possuído em posição sem dono → `rt_park`/`rt_mark`/`rt_sweep`.
Também: **`TK_VT_FIXED 2`** (`&Nome_release` na palavra 0 da vtable, itab na 1 — o layout do lx)
e a **correção de fato**: o `refcount@+8` que o crumb do `class` dizia reservado não existia
(campos começavam em 8); reservar moveu offsets e 4 fixtures mudaram os números de layout.
`struct` sem RC, pacote de `params` e campo `static` de classe = dívida declarada em `rt.mc`.
O dono revisa; se discordar, o ponto a reabrir é só "parse vs pass", que é local ao `teko_rc.mc`.

### D226 · DONO: modo AUTÔNOMO do coordenador do ngen — forks de superfície decididos pelo C#/mercado; impasse validado com a sessão do mc (dono 2026-09-05) 🔧 PROCESSO
O dono deixa o coordenador em modo autônomo: **um agente por vez**; a sessão do mc comunica por
arquivo (`mini_compiler/build/NOTICES-teko.md`); **impasse → validar com a sessão do mc** (ngen na
superfície, mc no core); **via de regra, seguir o exemplo de linguagens de mercado, com sintaxe
próxima ao C#**. Consequência: fork de superfície que o DECISION_LOG não resolva é decidido pelo
C# (ou pelo mercado quando o C# não tem forma), registrado aqui como "decidido pelo coordenador
sob D226" para o dono revisar, e o trabalho segue. HALT só para o que nem C#, nem mercado, nem o
mc resolvem.

### D225 · DONO: o RUMO do port — extensibilidade e override por superfície do mc (M41/M40) são o caminho para a teko se AUTO-HOSPEDAR (dono 2026-09-05) 🔭 RUMO
O que importa nas releases 0.10.1-0.12.0 não é o AVR: é que o mc passou a ser **re-arquitetável
e recompilável por superfície** — `<mc/core>` é a soma de cinco partes (`core_min`, `core_machines`,
`core_writers`, `core_build`, `core_bundle`), e um módulo pode **omitir** partes e **sobrescrever**
o core sem tocar em `src/` (`type_disable`, `intrinsic_disable`, `type_set_width`, `subcommand`,
`backend_default`, `machine_use_if`, `on_plan` — M41; provado pelo M40, que recria um compilador
inteiro com zero linhas em `src/`). **Consequência para o port:** quando estivermos "prontos", o
compilador teko é um mc **recriado das partes** com os módulos teko em cima e o que a teko
substitui desligado — e, com os módulos do `ngen` reescritos na própria teko, **a teko compila a
si mesma** até o ponto fixo (`teko1 → teko2 → teko3` byte-idênticos), como o mc faz (M0-M8).
Caminho: (1) hoje, `<mc/core>` + `teko.mc`; (2) M41: recriar o compilador teko das partes que a
teko usa, `subcommand` para o driver (`teko build`), `type_disable`/`intrinsic_disable` para o que
a teko redefine; (3) M44: o ngen como pacote (`teko_init()`); (4) auto-hospedagem: módulos do
ngen em teko, fixpoint. Coordenado com o mc (sem 1.0.0 sem o ngen). **Complemento (dono):** o 1.0.0 do mc trará itens
novos de roadmap — até um **gerenciador de pacotes** —, e a teko usará **dois sistemas de
pacotes**: o dela e o do mc. Roadmap pedido à sessão do mc por arquivo (`NOTICES-teko.md`);
a entrega 5 se ordena pelo que vem do lado do mc, sem duplicar do lado da teko o que ele entrega.

### D224 · DONO: classes abstratas como C#; classes/métodos PARCIAIS em avaliação (dono 2026-09-04) 🔧 SUPERFÍCIE
- **`abstract` como em C# (ruling):** `abstract class Shape { public abstract i64 area(); }` —
  a classe não é instanciável (`new Shape` é erro claro), o método abstrato não tem corpo e
  obriga `override` na primeira derivada concreta (conformidade checada como a de interface);
  método abstrato ocupa slot de vtable como um `virtual` sem corpo; `abstract` em membro exige
  classe `abstract`. Substitui o honest-stop de `abstract` do trait (D216) para classes.
- **`partial` (RATIFICADO no mesmo dia): classes parciais SIM; métodos parciais NÃO.** Classe
  parcial = a mesma classe declarada em mais de um arquivo/lugar, unida na compilação; o tipo
  fecha no **pass** (mesmo mecanismo do `.` deferido). Texto original da avaliação:** classe parcial = a mesma classe
  declarada em mais de um arquivo/lugar, unida na compilação (o mc já une `namespace` por
  prefixo — precedente de "reabrir"); método parcial = declaração sem corpo cuja implementação
  é opcional (C#: `partial void m();`, sem implementação a chamada some). Registrar a forma e o
  custo (o record/replay de genéricos e o layout de campos precisam de todas as partes antes de
  fechar o tipo — as partes têm de ser vistas antes do 1º uso, ou o tipo fecha tarde, no pass).
  Decisão: **`partial class` entra na fila (depois de `abstract`); `partial` em método é erro claro.**

### D223 · DONO: propriedades (`get`/`set`), corpo default em `interface` e assinatura estática em `interface` — como em C# (dono 2026-09-04) 🔧 SUPERFÍCIE
- **Propriedades como C#:** `public i64 Side { get; set; }` (auto-propriedade: campo de apoio
  gerado), `{ get => side; set => side = value; }` e a forma com blocos; `value` contextual no
  `set`; `get` só = leitura; `p.Side = 3` rebaixa ao `set`, `p.Side` ao `get`; podem ser
  `virtual`/`override`/`static` e ter visibilidade por acessor (`{ get; private set; }`).
- **Corpo default em `interface`** (C# 8): método de interface com corpo; a classe que não o
  redefine usa o default (o itab aponta para o símbolo da interface).
- **Assinatura estática em `interface`** (C# 11 `static abstract`): a interface declara membro
  estático que o tipo implementador tem de fornecer; resolvido em compile-time pelo tipo (sem
  itab — não há receptor).
- Ordem: propriedades e interface v2 entram logo depois do crumb de membros (D220), antes do
  reclaim, porque construtor/destrutor e operadores (D218) escrevem contra o modelo de membros
  completo.

### D222 · DONO: `switch` nas DUAS vertentes do C# (statement e expression); `break N` atravessa o `switch`; `match` eliminado; `when` = guarda de `case` (dono 2026-09-04) 🔧 SUPERFÍCIE
- **`switch` statement** (`switch (x) { case 1: … break; case 2: … break; default: … }`) e
  **`switch` expression** (`x switch { 1 => a, 2 => b, _ => c }`, C# 8) — as duas vertentes.
- **`break N` tem de funcionar em `switch`:** o `switch` conta como um nível — `break` sai do
  `switch` (C#), `break 2` sai do `switch` e do laço de fora. Rota provável (architect confirma):
  o `switch` statement rebaixa para um **`loop` de uma volta** com `if`/`else` e `break` ao fim de
  cada braço — o `break N` por profundidade do core atravessa sem nada novo.
- **`match`: eliminado** — a switch expression cobre. **`when`: mantido** como **guarda de
  `case`** (`case n when n > 0:` / `n when n > 0 => …`), açúcar sobre `if`.
- Supersede a dúvida do D218 sobre `match`/`when`.

### D221 · DONO: manter `loop` e as estruturas nativas do mc; closures = função inline/aninhada + açúcar sobre `&fn`/`callp` (dono 2026-09-04) 🔧 SUPERFÍCIE
- **Manter `loop`, `break N`, `continue`, `if` e o resto das estruturas nativas do mc.** `while`/`for`
  (prelude) são ADIÇÕES, não substituições.
- **Closures:** o mc já dá ponteiro de função por referência (`&fn` como `uptr`, chamado por
  `callp`; `examples/desktop` e `examples/conc` usam para callbacks e `spawn`). Logo o teko-mc dá
  suporte a **escrever uma função inline (lambda) ou dentro de outra função (função local)** e
  constrói **açúcar sobre `&fn`/`callp`**: tipo de função na superfície, chamada de um valor-função
  como `f(x)`, passagem como argumento. Captura de variáveis do escopo envolvente é design-open
  (o chão do mc é um `uptr` puro): architect-first — provável forma = objeto gerado (classe
  anônima com os capturados como campos + método) apontado por `uptr`, no espírito do D218/D219.
- **Captura = modelo do PHP (dono, complemento do mesmo dia):** a função inline/local declara
  EXPLICITAMENTE o que captura com **`use (a, b)`** — só essas variáveis do escopo declarante
  entram; nada implícito. Por **valor** por padrão; **por referência** com `&` (`use (&a)`), que
  é o `&x` do mc (endereço de local) — o próprio chão do mc dá a forma. O objeto gerado tem
  exatamente os campos da lista do `use`. Palavra `use` já é contextual (trait, D216) — mesmo
  reuso que o PHP faz. Fecha o design-open: sobra ao architect só a forma C-like da lambda e o
  tempo de vida do capturado por referência (RC do reclaim / escopo).

- **Mecanismo (dono, complemento):** o mc já permite **criar primitivas novas** (M24, Tier 4:
  `type_new` + `intrinsic`, guia `docs/guide/96-a-new-primitive.md` — id ≥ `TY_MAX` é do
  módulo e toda decisão é delegada; `<float>` foi a primeira). Logo o **ponteiro de função é um
  primitivo próprio** sobre `uptr` — identidade distinta para o oráculo, que rebaixa `f(x)` a
  `callp` e tipa o retorno pela assinatura. **O mesmo vale para `ref T` e `out T`** (parâmetros
  por referência como em C#; `out` = dest-passing, o DPS): primitivos de endereço tipados sobre
  `uptr`, com `&` no sítio e deref implícito no uso.

### D220 · DONO: `public`/`private`/`protected`/`static` como em C#; `internal` se o mc permitir restringir; SEM classes aninhadas (dono 2026-09-04) 🔧 SUPERFÍCIE
- **Modificadores como em C#:** `public`, `private`, `protected`, `static` em membros; `public`/
  `internal` em tipos de topo. **Defaults do C#** (ratificados pelo dono 2026-09-04): tipo de topo sem modificador é
  `internal`; membro sem modificador é `private`. Consequência: as fixtures que hoje leem `p.side`
  de fora passam a escrever `public i64 side;`.
- **`internal`:** o mc **não tem unidade de compilação** (`core-language.md:422` — tudo entra por
  `#include` num arquivo só), então não há "assembly". Mas toda visibilidade é checagem do MÓDULO
  (o core nunca checa acesso; o `lx` faz por mangling) e o módulo conhece a origem de cada
  declaração (`p_file()`/`nd_file`). **Logo `internal` é ensinável. Unidade (dono, ratificado):
  o CÓDIGO DO PROJETO** — tudo que entra pelo `mc.toml` do próprio projeto — e não o namespace
  nem o arquivo; código de outro projeto/pacote (bundle `<…>`, include externo) não vê. O módulo
  decide pela origem da declaração (arquivo do projeto vs. fora).
- **Regra do dono (par excludente, decidido pelo `internal`):** *se `internal` for possível →
  tem `internal` e NÃO tem nested; se não for → tem nested e NÃO tem `internal`.* Como `internal`
  é ensinável (unidade = arquivo-fonte via `nd_file`), fica: **`internal` sim, classes/structs
  aninhadas não.**
- **Ordem:** este crumb entra depois do `this`/`base` (D219) e **antes** do reclaim
  (construtor/destrutor `public`) e do C5b (`public static … operator+`).

### D219 · DONO: métodos SEM `self` — `this` implícito e `base`, como em C# (dono 2026-09-04) 🔧 SUPERFÍCIE
O `self` explícito na lista de parâmetros é a forma do `lx`, não a do teko-mc. **Prefira a forma
inferida do C#:** o método não declara o receptor — `i64 area() { return side; }` —, o
compilador injeta o receptor oculto; dentro do corpo um campo/método sem qualificador resolve
como membro de `this` quando não há local/parâmetro com o nome (local sombreia campo, como em
C#); `this` é palavra contextual para o receptor explícito (`this.side`); **`base.m()`** chama
a implementação da classe base diretamente (não-virtual), como o `lx` já faz (`README.md:71`,
`tests/01-inherit.lx`). Vale para `class`, `struct`, `trait` e assinaturas de `interface`
(`i64 area();`), para construtor (`Nome(i64 s) { side = s; }`) e destrutor (`~Nome()`).
Operador estático (D218) não tem `this`. Sweep: toda a superfície landada e todas as fixtures
passam para a forma nova; o `self` deixa de ser aceito.

### D218 · DONO: rulings de superfície do teko-mc — operadores como C#, construtores/destrutores, `while`/`for`/`namespace`/`break N` pelo mc, `var`/`type`/`match` fora (dono 2026-09-04) 🔧 SUPERFÍCIE
Batelada de rulings do dono sobre a lista de honest-stops e sobre o que já landou:
- **Sobrecarga de operadores: como em C#, e o C5 landado está ERRADO por completo.** Operador é
  membro **estático** do tipo, com os operandos como parâmetros explícitos (sem `self`):
  `Vec operator+(Vec a, Vec b)`, `Vec operator+(i64 k, Vec v)` (o "reversed" que o C5 recusava),
  unários com um parâmetro (`Vec operator-(Vec a)`, `operator!`), resolução por sobrecarga sobre os
  tipos de AMBOS os operandos (candidatos = operadores declarados no tipo de qualquer operando,
  pelo menos um parâmetro do tipo declarante), pares obrigatórios (`==`/`!=`, `<`/`>`, `<=`/`>=`).
  Sem despacho por vtable. Refazer (C5b).
- **`new` → construtores e destrutores** no novo modelo: `Nome(params) { }` é construtor
  (`new Nome(args)` aloca, instala a vtable, chama-o), `~Nome() { }` é destrutor — chamado pelo
  reclaim (RC) quando o count chega a zero, antes de liberar os campos. Substitui o `dispose` do lx.
- **`while`/`for`:** há exemplo no mc (o `<prelude>`: `#rule` + `#token`) — usar.
- **`namespace`:** açúcar sobre `#include` (D212); **`using`/`import`:** exemplo pronto no
  `examples/lang` (`import n;` = `#include "n.lx"` + `using n;`, mangling por prefixo) — usar.
- **`break` por profundidade** (`break N` do core) em vez de etiquetado — já é do core; nada a ensinar.
- **`var`:** desnecessário — fora. (Inferência, se vier, é assunto para a sessão do mc.)
- **`const`:** o mc usa `#define`; construir como açúcar (`#rule`/`syntax_stmt`) sobre `#define`.
- **`match`:** em dúvida se é necessário; a doc do mc mostra como usar a AST se for.
- **`when`:** se `match` e inferência se resolverem, é açúcar sobre `if`.
- **`type`:** abandonar — não se usa.

### D217 · DONO: NADA DE `Variant` na versão teko-mc (dono 2026-09-04) 🔧 SUPERFÍCIE
Ruling curto e duro para o port `ngen/`: **não se ensina `Variant`** — nenhuma união
dinâmica / valor etiquetado em runtime / tipo "qualquer". A superfície teko-mc é
**estaticamente tipada**: todo receptor, argumento e operando tem tipo conhecido pelo
oráculo (`teko_typeof.mc`) ou é erro claro de compilação — é o que a entrega 4 inteira
(escopo, sobrecarga, operadores, genéricos com constantes) assume. Onde o teko-clássico
usaria Variant, o teko-mc usa `interface` (despacho por itab), `class` (vtable) ou
genéricos com constantes (record/replay, inline). Construto que "pedir" Variant é fork:
parar e perguntar ao dono, não emular.

### D216 · DONO: `trait` do `ngen/` funciona IGUAL AO DO PHP — flattening em compile-time, não é tipo (dono 2026-09-04) 🔧 SUPERFÍCIE
Fecha o único fork aberto da entrega 3 (o `trait` não tinha precedente no mc nem mapeamento na §3 de `port-teko-mc.md`). **A forma é a do PHP**, com tudo que ela implica:
- **Flattening em tempo de compilação:** `use A;` dentro do corpo da classe COPIA campos e métodos do trait para dentro dela, pela mesma máquina de layout (offset/alinhamento) do `class`. Não há vtable própria, itab, nem despacho dinâmico envolvido.
- **Trait NÃO é um tipo:** não se declara variável dele, não há `new Trait`, não entra em lista de conformidade — logo não recebe `type_new` nem entrada na tabela de tipos. É o que o torna a construção mais BARATA das quatro da entrega 3, mais barata até que `interface`.
- **Precedência (a do PHP):** membro da própria classe > membro vindo do trait > membro herdado da base.
- **Conflito entre dois traits** com o mesmo nome de membro = erro de compilação claro (fatal em PHP). O `insteadof`/`as` fica **deferido** (honest-stop com mensagem).
- **Trait usa trait:** flattening recursivo, com detecção de ciclo.
- **Fora de escopo por ora** (honest-stop, não se inventa): `abstract` no trait, membros estáticos e visibilidade (`public`/`private`/`protected`) — nenhum dos três existe ainda no `ngen`.
- **Palavra `use`:** lida DENTRO do parser de corpo, sem registro global, para não confiscar `use` do vocabulário do programa inteiro (`using` é outra palavra, já reservada como honest-stop).

Coerente com o D215 (superfície C-like, não herda a sintaxe teko-clássica) e com o D213 (reusar a base, ensinar só o delta).

### D215 · DONO: rulings do port `ngen/` — superfície C-like (não herda a sintaxe teko-clássica); "ensinar" = ZERO toque no mc; mc por RELEASE; canal com a sessão local do mc (dono 2026-09-04) 🔧 PORT / método
Dados na sessão local que assumiu o `ngen/`, no dia em que **`struct` e `class` landaram** (commits `984f268e` e `06db615d`, entrega 3 commits 1 e 2; 7 fixtures verdes — hello + 4 primitivas + `types_struct` + `types_class`, todas exit 42; check-run do CI verde; verificação independente aprovada).

- **1. A superfície do `ngen` é C-like e NÃO reaproveita a sintaxe do teko que consta em `src/` e nas docs.** `struct Nome { }` / `class Nome { }` como declaração de topo, campos em **C de escola**, construção por **`new`** (o dono confirmou o `new` explicitamente). A forma clássica (`type X = struct { }` com o corpo depois do `=`, e o brace-init `Box { v = 42 }`) **não é herdada por este port**. Concretiza o D213: fidelidade sintática ao teko-clássico não é requisito, funcionalidade é.
- **2. "Ensinar o mc" significa NÃO tocar no código do mc.** O ferramental já existe e é o mesmo que o mc usa nos próprios exemplos para `class`/`interface`: módulo `.mc` que registra handlers (`syntax`/`syntax_stmt`/`syntax_expr`/`syntax_infix`/`type_alias`/`type_new`/`on_stmt`). Todo o delta do teko vive em `ngen/`; o repo do mc é **somente leitura** para esta sessão. O dono avalia que **quase tudo que o mc tem hoje já atende** — quando não atender, pergunta-se (item 4) em vez de contornar.
- **3. O mc entra por RELEASE, não por submodule.** Baixa-se o **executável** das releases de `schivei/mc` (checksum conferido) e instala-se no PATH. Não se usa submodule, não se roda o bootstrap do mc (chicken-and-egg), e **não se usa binário de dentro do clone do mc** — ele pode estar à frente da release que o CI consome (o workflow resolve a `latest` dinamicamente).
- **4. Canal com a sessão local que desenvolve o mc** (`/Users/schivei/projects/mini_compiler`): ela **avisa esta sessão a cada release nova** — e aí se baixa a release, troca-se o symlink e **reconfere-se o baseline** do `ngen` antes de seguir. E quando aparecer **tensão que o ferramental atual do mc não resolva**, consulta-se essa sessão para saber por onde se resolve, ou se o caso exige suporte novo no mc. Continua valendo o canal com a sessão remota coordenadora (histórico do port e dreno das branches canônicas).

**Adjacente (registro, não é ruling):** `docs/design/port-teko-mc.md:106` ainda descreve o `str` do teko como `{ptr,len}`, mas a entrega 2 landou **NUL-terminated** (`ngen/teko_type.mc:23` → `TY_UPTR`). O código vence a doc; a §3 precisa de sync.

### D214 · DONO: ordem das entregas do `ngen/` — primitivas → tipos (`class`/`struct`/`interface`/`trait`) → superfície e comportamento base; o PORT SÓ FECHA no mc 1.0.0 (dono 2026-09-04) 🔧 SEQUÊNCIA
Com a **fatia vertical A3 VERDE** (CI provou ponta-a-ponta: `mc-0.10.0-linux-x86_64` baixado + checksum OK, compilador ensinado `build/mc-teko` construído, `tests/hello.tk` compilado POR ele, binário executado com **exit 42** — tudo em ~0,5 s), o dono ordenou o crescimento do `ngen/`:
- **ORDEM DAS ENTREGAS:** (1) **novas primitivas**; (2) **tipos — `class`, `struct`, `interface`, `trait`**; (3) ir **crescendo o ensino da superfície e do comportamento base**. Sempre "por baixo", sempre reusando a base do mc e ensinando só o DELTA (D213).
- **ARENAS:** a parte de **arenas automáticas JÁ EXISTE** no mc; o **cálculo automático** (dimensionar a arena do programa em compile time = **M13**, o nosso `#arena_size`/D133) **só vem mais à frente**.
- **FECHO DO PORT GATED EM mc 1.0.0:** o restante **só se finaliza de fato quando o mc chegar a 1.0.0** (após M41/M40/M28/M29/M30/M33/M35/M36/M13 + dívidas). Ou seja: o **gatilho de INÍCIO** disparou (D211/M24) e o port ESTÁ em execução, mas o **FECHO** espera o 1.0.0 — **não se promete port completo antes disso**.
- **Feedback pro lado mc (achado no 1º CI verde):** o `.o` emitido não carrega `.note.GNU-stack` → o `ld` marca **stack executável** e avisa que a tolerância será removida; e `mc --version` não existe (flag desconhecida).

### D213 · DONO: `ngen/` começa POR BAIXO e REUSA a sintaxe base do mc (C de escola) — ensina-se só o DELTA do teko; foco em funcionalidade, não em fidelidade sintática (dono 2026-09-04) 🔧 MÉTODO
Ruling de método pro `ngen/` (o port começando dentro do repo do teko, D212): **começar por baixo, ensinando as superfícies**, e **REUSAR o máximo** — inclusive, se couber, **a própria sintaxe do mc, que lembra "um C de escola"** — pra **não reinventar sintaxe** e **focar nas FUNCIONALIDADES**. Resultado: **prototipagem rápida sobre um front limpo e fácil de entender**.
- **JÁ É ESTRUTURAL NO MC (não é atalho, é o desenho):** a sintaxe BASE (Pratt, expressões, statements, tipos, `fn`) vem do **core do mc**; os hooks das 3 camadas só **ADICIONAM**. O `examples/lang` (lx) prova: não reimplementa a linguagem, acrescenta classes/generics/interfaces por cima da base.
- **CONSEQUÊNCIA:** o front-end do teko **encolhe pro DELTA** — NÃO se escreve lexer/parser/expressões no `ngen/`; escreve-se só o que o teko **ADICIONA** sobre a base do mc. A tabela **§3 de `docs/design/port-teko-mc.md` vira "a lista de adições"**; o resto é herdado de graça.
- **INTENÇÃO ORIGINAL DO DONO — não é corolário nem concessão (dono confirmou 2026-09-04: "já era uma ideia minha de tornar mais simples, sem reinventar sintaxe"):** simplificar **de propósito**. O **teko-sobre-mc NÃO precisa ser sintaticamente idêntico ao teko-clássico**; onde a forma do mc já resolve, **adota-se a do mc**, sem hesitar e sem justificar. Fidelidade sintática **NUNCA foi requisito**; funcionalidade é.
- **PROIBIDO:** um agente do `ngen/` gastar esforço reimplementando sintaxe que a base do mc já dá, ou reproduzindo a grafia do teko-clássico "porque era assim".

### D212 · DONO: `ngen/` DESCARTADO do teko — a superfície nova nasce no `mc`; as formas vêm dos exemplos do mc (dono 2026-09-04) 🔧 EXPURGO / rumo
O dono descarta as próprias ideias do `ngen/` (30 arquivos: esboço da superfície nova — tipos `floats/strings/signed/unsigned/characters`, operadores-como-interface `arithmatic/equatable/comparable/convertible/bits/shiftable/unary/logical`, coleções `iterator/map/slice/ranges/dictionary/arrays/hashable`, `lir/ir_ops`, `memory/helpers`, `main.tks`, `teko.tkp`). **O `ngen` agora será feito no `mc`.** Motivo: os **exemplos do mc já estão repletos de formas prontas** que o teko pode usar e abusar — **generic constants** (`<T, const N: i64>`, provado em `mc/examples/lang`), **`namespace` açucarado para `#include`** (compilação separada por unidade), entre outras. Não faz sentido evoluir um esboço de superfície paralelo no repo do teko quando o mc já provou as formas e o port (D211) é o rumo.
- **AÇÃO:** `ngen/` removido da árvore. Verificado antes da remoção: não é referenciado por `.github/`/`scripts/`/`Makefile*`/`tooling/`/`src/`/`main.tks` — sketch standalone com `teko.tkp` próprio; a remoção NÃO afeta build nem fixpoint.
- **SUPERSEDE** as referências que tratavam o `ngen/` como plano VIVO: **D203** ("a superfície nova do `ngen/` evolui em cima depois"), **D205** ("alinha com o `ngen` do dono, que já usa `char : byte`") e `docs/design/plano-emissao-objeto-nativo-0.3.1.md` ("`ngen/` vem depois"). Menções puramente HISTÓRICAS (TEKO_HISTORY, narrativa de docs antigas) FICAM como história — não se reescreve o passado; o mais recente vence.
- **As formas do mc entram pelo PORT, não por um ngen paralelo:** já catalogadas em `docs/design/port-teko-mc.md` §3 — generic constants via record/replay (`p_skip_balanced`+`p_subst_int`), `namespace`/`import`/`using` como açúcar de `#include`+mangling.

### D211 · DONO: RUMO — portar o teko sobre o `mc` (minicompiler.dev, `schivei/mc`); os 3 "buracos de núcleo" dissolvem-se; `comptime`/macro do teko APOSENTA, extensão = diretivas do mc (dono 2026-09-03) 🔭 RUMO / port-planning
O dono construiu em paralelo o **`mc`** (minicompiler.dev, repo `schivei/mc`) — construtor de linguagem/compilador **feito HOJE, não de 3 meses atrás como o teko** — e decidiu: **finalizar o trabalho em voo do teko, depois PORTAR o teko sobre o mc** quando o mc estiver pronto. O `mc` **já é** o endgame do teko realizado limpo: emite objeto direto (ELF+Mach-O, sem linker nem compilador C; `backend_exe.mc` auto-assina Mach-O com SHA-256), **arena bump com doubling (O(1) amortizado) que MATA por construção o O(n²)** origem do nosso inferno de memória, e é **extensível por diretiva em 3 camadas** (`#token/#infix/#rule/#opcode/emit/reloc` → `pass()/backend()` → `syntax/type_alias/on_stmt`). O `examples/lang` prova que uma linguagem inteira (classes/genéricos/interfaces/regiões/RC) se ensina **puramente por hooks, com zero mudança no `src/` do mc**.
- **A CAMPANHA DE MEMÓRIA INTEIRA FICA MOOT PÓS-PORT (dono 2026-09-03):** o mc **compila como o C — cada arquivo é uma unidade de tradução (um programa)**: compila unidade → emite objeto → **libera** → próxima → linker junta = **compilação separada clássica**. O exemplo de **namespace como açúcar pra `#include`** faz a estrutura de módulos do teko cair direto nesse modelo. É **literalmente o endgame da nossa própria doc** ("pipeline por unidade → cada namespace emite um objeto → unidade→objeto no disco→libera"), no mc **de nascença**. Logo o pico deixa de ser "a AST do teko inteiro residente" (raiz do reclaim-0% / OOM) e vira "o maior arquivo isolado". A campanha de memória (NO-PUSHES, 4-naturezas, arena scoped, streaming, ratchet D68) fica **moot por DOIS motivos independentes**: (1) doubling-arena mata o O(n²) DENTRO da unidade; (2) compilação separada nunca mais acumula o programa todo. **Reforça em dobro o congelamento da campanha native.**
- **§16 (EXPURGO-LIBC-COMPLETO / ZERO-LIBC) REVERTIDO PELO DONO (2026-09-03):** a lei "se existe em C existe em Teko, nunca linkar libc" nasceu de um mal-entendido — o dono achava que as libc do SO **só existiam dentro do compilador C**. NÃO: `glibc`/`musl` (Linux), `libSystem` (macOS), `kernel32`/`ntdll`/`ucrt` (Windows) são **bibliotecas do SISTEMA** (parte da ABI do SO); o compilador C só as *linka*, não as contém. O chão irredutível é o **syscall do kernel**; a libc mora acima. E o absolutismo zero-libc **nunca foi portável**: só o **Linux tem ABI de syscall estável** (por isso o teko rodou zero-libc lá) — **macOS exige `libSystem`** (syscall cru não-suportado) e **Windows não tem syscall estável** (obrigatório `kernel32`/`ntdll`). O mc adota o modelo **correto e universal**: **usar as libs do SO + escolher o linker + instalar sysroots pra cross-compile** (musl/mingw/Apple SDK + stubs `.tbd` — o M25). Post-port o teko **linka a libc do sistema** como qualquer linguagem madura; o trabalho de syscall feito não se perde (vira base do backend), mas o expurgo-libc absoluto **cai**. **O mc dissolve DUAS campanhas de meses: a de memória E a de expurgo-libc.**
- **DPS DEMOVIDO DE FUNDAÇÃO → OPCIONAL; GTK prova FFI pesada (dono 2026-09-03):** nos exemplos do mc **nem foi preciso DPS** (o mc permite, mas está tudo limpo sem). O DPS (região=param, "return MOVE pra região do caller" — D130 regra 2) era a **peça de MAIOR RISCO** do port; a gente precisava dele porque no build monolítico com reclaim-0% o valor retornado vazava ou tinha que ser movido. Com **bump-arena por-unidade liberada no fim da unidade**, o retornado vive na arena da unidade e morre junto → **sem pressão de reclaim por-escopo → DPS não é preciso pra ficar limpo**; vira **otimização opcional** (menos cópias), não fundação. O byte-mover sai da lista de bloqueadores. E o mc tem **exemplo com GTK** — GObject/sinais/**callbacks** (C↔teu código), ABI enorme: prova o caminho **sysroot+linker+FFI** com lib dinâmica real e ponteiro-de-função nos dois sentidos → a FFI que a stdlib do teko precisa é trivial perto disso.
- **BÔNUS ALÉM DOS BLOQUEADORES + convergência comptime-heap = D133 (dono 2026-09-03):** filosofia do mc = **core aprende só o MECANISMO (entender algo); o COMO-implementar é exemplo/hook** — por isso o core cabe em ~2.800 linhas e hospeda classes/genéricos/regiões/GTK/wasm sem inchar (é a nossa disciplina "núcleo mínimo / não detectar o que não existe", mas como FUNDAÇÃO). Alvos novos endereçados: **wasm/WASI/browser** (+ formatos wat/wasm) = classe de alvo (web/sandbox) que o teko nunca tentou, **de graça no port**; e **futuro multi-arch** (RISC-V, MIPS, x86-32, **ATmega/AVR 8-bit**) pela generalização da tabela de máquina do M17 (desktop→embarcado). **Comptime-heap:** o mc hoje pré-aloca 32 MB e usa <metade; o dono já instruiu **tolerância percentual + cálculo automático do heap necessário → heap pré-dimensionado em comptime** = **literalmente o D133/M13** (`#arena_size`/`#arena_depth` eliminados, compilador fixa o tamanho da arena pelo pico) chegando GENÉRICO no mc. **Registro do de-risking do port dado por COMPLETO aqui.**
- **⚡ GATILHO DISPAROU (dono 2026-09-04) — os 5 bloqueadores LANDARAM; o port está DESTRAVADO.** M17 (walker+tabela de máquina, x86-64, registro de alvos), M19 (Win arm64/COFF/kernel32), M20 (Win x64/ABI Win64), M25 (sysroots+stubs sintetizados) e **M24 (Tier 4: primitivas e instruções PELA SUPERFÍCIE, `<float>`)** — todos landed. Também landaram: **M37/M38 (mc hospedado em Linux E Windows)** → **fecha por completo o caveat #1 da doc de Port** (gap de host); **M39 (RISC-V bare metal ensinado com ZERO linhas em `src/`)** → enterra a estimativa "arch nova = cirurgia de núcleo = meses"; **M39.5** (`mc build` com `[target]` de módulo). **M24 = nosso D187 realizado genérico** (lista de builtin FECHADA; primitiva/instrução são superfície). **Faixa B da doc de Port DESTRAVADA** (o gate M24 de crypto/math/numeric caiu).
  - **SEQUENCIAMENTO (leitura do coordenador, 2026-09-04):** **M41** (núcleo composável, remoção/override de `uptr`) está EM EXECUÇÃO e **M40** (compilador recriado e *debloated*) vem em seguida — os dois remodelam a superfície de hooks e a estrutura que os passos **A3–A5** espelhariam. Logo: **A1/A2 seguros AGORA** (A2 = catalogar a superfície `exp` do teko é 100% lado-teko, zero dependência da churn do mc); **A3–A5 rendem mais DEPOIS de M41/M40 assentarem** (senão constrói-se contra alvo em movimento). Restante do mc pós-gatilho: M40, M28 (lsp), M29 (VS Code), M30 (DWARF), M33 (wasm), M35 (`<bench>`/`<memcheck>`), M36 (multi-target), M13 (backlog), M18 (opcional), dívidas → **1.0.0**.
  - **EXECUÇÃO do port = SESSÃO LOCAL DO DONO** (ele declarou que rodaria A1–A5 local). O coordenador **NÃO inicia A1–A5** aqui pra não duplicar; entrega a doc e acompanha.
- **CUSTO DO PORT — colapsou (não só reduziu):** o mapeamento (Explore, 2026-09-03) marcou 3 "buracos de núcleo" a MESES cada — **os três sumiram:**
  1. **Floats** e **2. multi-alvo (x86_64+COFF)** — o dono **está construindo AGORA** no núcleo do mc (ou como núcleo ou como exemplo); fecham do lado dele, **fora do custo do port**.
  3. **Comptime executável — NÃO era buraco:** o mc **já É** o mecanismo de extensão (as diretivas dele **são** o que a macro tenta ser, no nível do construtor de linguagem). Se o teko-sobre-mc **embarca em si esses mesmos recursos**, o dev do teko estende pela via do mc → a maquinaria de `comptime`/macro do teko vira **REDUNDANTE: aposenta, NÃO se porta.**
- **O que sobra do port = o que sempre foi barato:** reexpressar a **superfície** do teko (lexer/parser/checker/tipos/stdlib) como **prelúdio/hooks do mc** — SEMANAS (precedente forte no `examples/lang`), e o dono já prototipa isso como exemplo do mc.
- **ENQUANTO ISSO (rumo do coordenador):** **finalizar o trabalho em voo do teko** (implementer dos IR-builders Chunked) → verificar (rota-C, capado, sem native) → drenar o drenável. **CONGELAR a campanha native** — NÃO abrir Eixo B, NÃO Passada 2 de streaming (moot com o port). Manter o teko **verde na rota C (~1 GB)** enquanto o mc amadurece. **NÃO iniciar nenhuma iniciativa native nova.**
- **GATILHO do port = M24 (floats) fechar no mc (roadmap do dono, 2026-09-03).** Ordem de execução do mc: M31 concorrência (em curso) → **M17** (walker+tabela de máquina, x86-64, Linux x64) → **M19** (Win arm64: COFF+kernel32+lld-link) → **M20** (Win x64: relocs COFF + ABI Win64) → **M25** (sysroots/cross-link: musl/mingw/Apple SDK, stubs `.tbd`) → **M24** (f32/f64, `#machine` por arch). Os 5 (M17/M19/M20/M25/M24) são os bloqueadores do port (teko é monólito que cross-compila — emite TODOS os alvos — e a stdlib exige float); **M24 é o último na ordem → quando floats landar, os outros já estão verdes atrás.** NÃO-bloqueadores (pós-port): M18 (x86-32, opcional), M28 (LSP), M29 (VS Code), M30 (DWARF), M13 (dimensionar arena compile-time = nosso `#arena_size` D133), dívidas de release. Já provado como exemplo/hook no mc: **arenas injetadas + passagem por referência** (= nosso modelo região=param/DPS D130-D133, a peça de maior risco) e **HTTP+SQLite** (FFI/I/O real). M17 preserva "arm64 byte-a-byte igual ao stage0" (o fixpoint carregado pro refactor de máquina).

