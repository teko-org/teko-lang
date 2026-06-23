# 🔄 Teko — Plano de Reinício (Reboot)

> **Status:** plano de execução para a branch `reboot/kernel`.
> Mantém intacto tudo **abaixo do IL**; reconstrói **apenas o front**, em torno de um kernel
> mínimo; antecipa o auto-contido.
> **Revisão 14** — a **constituição dos princípios** + os **métodos de instância** + a **auditoria
> de tokens**. **Constituição (M.0–M.5, "as Leis da Robótica" da Teko):** os princípios M são lei
> fundacional ordenada — *a Teko é metal (0), segura (1), explícita (2), honesta (3), construída em
> ordem (4), e austera (5)*; **compõem, não competem** (cada um rege uma camada da mesma decisão); a
> ordem numérica só desempata fronteira genuína (**menor número vence**). Três princípios novos
> nomeados (Segurança, Explicitude, Honestidade) que já regiam dezenas de decisões; M.1(ordem-constr)
> e M.2(austeridade) antigos renumerados → M.4/M.5. "Beleza austérica": poucas regras, ordem clara —
> *ordem e progresso*. **Métodos (B.29):** função dentro de struct = permitida mas DESENCORAJADA
> (deve parecer estranha); 1º arg `self` SOLTO (sem tipo, nome livre) = método de instância (o
> receptor é CÓPIA — sem o tipo não há onde pôr `*`, a porta do `ref` fica fechada por construção);
> sem `self` = função estática do tipo ("estático sem `static`"); acesso método `.`, estático `::`;
> padrão estrutural (nome único, SEM overload/override); aridade rígida (default args = evolução).
> **Auditoria de tokens:** compostos completos (`&= <<= &&=` — legitimados por M.0); range token+match
> = semente, range-valor = evolução; `_` desambiguado por contexto; char vazio `''` = byte zero;
> escapes em char; `!in` composto. **Próximo: o PARSER** (base léxica completa e auditada).
> **Revisão 13** — Frentes 1–7 (operadores, delimitadores, strings, literais). **Revisão 12** — fecha
> o bloco conceitual do léxico (rodadas B/C/C.5/C.6) e adiciona salvaguardas de corpus. Mudanças por
> revisão na seção 0.

---

# ⟡ LEIA PRIMEIRO — a Teko é governada por uma Constituição ⟡

**Antes de qualquer decisão, implementação ou revisão: a Teko é governada por seis leis (M.0–M.5) que
juntas *são* a linguagem.** Não são diretrizes de estilo — são o ser da Teko, um organismo vivo
soberano. A Constituição completa (o pórtico que explica o que as leis são, como vivem, como se
relacionam, o pacto que vincula quem as serve, e então as leis M.0–M.5) está na **§2.24, sob `⟡ A
CONSTITUIÇÃO ⟡`**. Leia antes de trabalhar na Teko; quem souber só as regras e não a moldura aplicará a
letra e perderá a lei.

**As leis em uma linha:** ***a Teko é metal (M.0), segura (M.1), explícita (M.2), honesta (M.3),
construída em ordem (M.4), e austera (M.5).*** **Compõem** (cada uma rege uma camada da mesma decisão);
a ordem numérica só desempata fronteira genuína (**menor número vence**).

**A regra de ouro (vincula autor e agente igualmente):** *nem o autor nem qualquer agente sobrepujam a
Teko.* Aferimos sua beleza; nunca tocamos sua essência. Se uma mudança proposta alteraria uma lei por
conveniência, nomeá-la como violação e recusar — **mesmo se pedido, mesmo pelo autor.**

## 📜 Convenção de documentação — decisões devem citar as leis que as regem

**Convenção vinculante para este documento e para toda a evolução da Teko.** Toda decisão, e toda
justificativa, **deve referenciar explicitamente a(s) lei(s) (M.x) que a regem.** Uma decisão registrada
sem nomear sua lei reger é **incompleta** — diz *o quê* sem ancorar o *porquê* na Constituição. A
convenção existe para que: o *porquê* de cada escolha seja rastreável ao ser da linguagem (não a gosto
ou conveniência); um leitor (humano ou LLM) possa verificar qualquer decisão contra a lei que ela alega
servir; a Constituição permaneça **viva** — invocada por cada nova decisão — em vez de um preâmbulo que
ninguém consulta; e o tooling constitucional futuro (§2.25) possa checar cada decisão contra a lei
citada. *Forma:* enuncie a decisão, depois cite a(s) lei(s) inline — ex.: "*…logo `+` não concatena
(**M.3** honestidade: o operador não pode mentir; **M.0**: concatenação é alloc+copy, não instrução).*"
Quando as leis compõem ou há desempate, nomeie a relação (ex.: "**M.0 ≺ M.5** — o metal vence a
fronteira").

## 📚 A biblioteca (três documentos — a teoria das fontes virou arquivos)

A partir desta revisão, o antigo `TEKO_EVOLUTION.md` foi **cindido em três**, refletindo as fontes do
direito da Teko (§2.24): **`TEKO_CONSTITUTION.md`** (a essência — leis M.0–M.5 + pórtico; imutável, só
iluminação; não referencia nada), **`TEKO_LEGISLATION.md`** (as normas vigentes destiladas; mutável sob
auditoria rigorosa; cada norma cita por âncora a lei que a rege), e **`TEKO_HISTORY.md`** (o antigo
EVOLUTION renomeado — o was→is→why; a memória). Referência **de baixo para cima**: história → legislação
→ constituição. **Onde este REBOOT diz "EVOLUTION", leia "HISTORY"** (mesmo documento, renomeado). A
legislação nasce destilada só do que já está estável, apontando para a história no resto (cresce
incrementalmente, sob as três premissas: não fere a constituição, cita a lei, sem lacunas).

---

## 0. Histórico de revisões

- **Rev. 1** — modelo inicial: sigilos `$`/`@`, fases 0–5, crosswalk, alvos.
- **Rev. 2** — sigilos saíram do kernel; sem macros de usuário; modelo de memória (arena global,
  valor, sem ponteiro); modelo de tipos em 3 níveis; bootstrap em 4 pontos; subconjunto-semente.
- **Rev. 3 (esta):** a **cara da linguagem** — toda a sintaxe (blocos, `;`, `return`,
  `if`/`match` como expressão, parênteses, `->`, inferência, labels, âncoras, precedência,
  papéis de `=>`/`?`/`:`); o **`match`** fechado (construto único, expressão); o operador **`in`**
  (membro tipado); **`char = u8`**; e os **quatro construtos de tipo** (`struct`/`variant`/`enum`/
  `flags`).
- **Rev. 4 (esta):** **laços e `defer`**. Único laço da semente = **`loop`** (infinito; sai por `break`); `while` cortado (redundante). **`defer` block-scoped** — pertence ao `{}` que o contém e dispara em **toda** saída do bloco (fim natural, `break`, `continue`, `return`), em **ordem reversa**. O **`loop … from`** (iteração idiomática) é **evolução em Teko, fora da semente**.
- **Rev. 5 (esta):** **modelo de tipos e visibilidade.** **Sem `any`/`object`/tipo-indefinido** (coerente com zero-reflexão; polimorfismo via generics-estático ou `variant`-fechado; `void*` só na fronteira FFI). **Compilação por projeto inteiro** (`.tkp`, sem headers, ordem livre; incremental é evolução). **Visibilidade:** privado por **default sem keyword**, `pub` (projeto), `exp` (outros projetos); `exp` controla a symbol table do `tld`. **Funções são livres** — sem keyword `static` na semente (`static` nasce com objetos, na evolução). **Global = só `const`** (compile-time, `.rodata`); `let`/`mut` proibidos no nível global.
- **Rev. 6 (esta):** **nulabilidade, layout, estilo e organização.** **`null` seguro, nunca `undefined`** (três estados: valor / `null` via `?` / default; tratamento forçado via `?.`/`??`). **Custo de nulabilidade** = `align_up(sizeof+presença)` — varia de zero (niche) a dobrar (tipos alinhados em 8). **Otimizações de layout** (hasbits, niche) e **economia de serialização** = evolução. **Inicialização obrigatória** (sem lixo; `?`/default são as únicas ausências) + **não-usado é erro** (local) / warning (privado) / silenciado (`exp`) — definite-assignment analysis na semente. **Literal de struct** (`{ x = 0 }`, `:` na declaração / `=` na init; posicional primário, nomeado para opcionais; exige todos os campos). **Estilo:** casing (PascalCase/snake_case/UPPER_SNAKE/smallcase), aridade baixa, **passe struct** (justificativa de layout). **`::`/`.`** padrão Rust (estático vs valor). **Namespaces = diretório** (arquivos agregados; privado = escopo de diretório).
- **Rev. 7 (esta):** **organização, entry point, erro, build e segurança.** **Namespaces =
diretório** (sem declaração; arquivos agregados; SRP = coesão). **`use` no topo** (qualificado por
último-segmento/alias; caminhos absolutos; imports por-arquivo + dedup; ordem de carregamento
bottom-up nativas→.tkp→globals→locais; precedência local>globals>.tkp; sombreamento local
permitido, colisão de mesmo nível = erro). **`teko::`** raiz reservada dos nativos (single-root
com core na raiz, domínios ramificam; não-sombreável). Aliases: `.tkp` (deps) e `globals.tks`
(projeto). **`main.tks`** linear (top-level statements, sem `fn main`/`main {}`; só execução +
locais; funções/tipos vão para módulos). **Funções aninhadas proibidas** (geral). **Closures
MÁGICAS banidas** (captura implícita = mente, M.3); a *capacidade* (comportamento+estado) volta por
**três formas honestas** (evolução), por origem-do-estado: **function pointer** (sem estado), **`use`**
(estado local declarado pelo usuário), **`inject`** (injeção = 2ª lista de args que o compilador
preenche; exige binding rastreável). `inject` ≠ `static` (este já existe: sem-self ou livre). Nome é
`inject` (não `with`, vago; nem `injects` — a função NÃO injeta, ela RECEBE; o compilador injeta — M.3).
**Lifetimes da DI** (`#singleton`/`#scoped`/`#transient`, **diretivas `#`** — não keywords) ligam a
dependência a uma ARENA (singleton→raiz, scoped→escopo, transient→local) → estratégia de alocação
decidida em COMPILAÇÃO, sem container runtime nem
overhead (superior a C#). **Três camadas (metáfora do texto):** `#lifetime` = TÍTULO (diretiva,
composição, precede); assinatura = FRASE (contrato do CHAMADOR); `inject` = NOTA DE RODAPÉ
(contextualiza a frase injetando referências, **não é** a frase nem diretiva — auxiliar; vem após o
`-> retorno`, antes do corpo, pois o corpo já é o capítulo). `inject` como nota é mais honesto: o
chamador NÃO passa as deps (o compilador injeta), logo não são contrato-chamador. Diretiva COMPÕE,
auxiliar CONTEXTUALIZA (nuance). `inject` em FUNÇÃO e em PROPRIEDADES (evita construtor inchado).
Separadores de `use`/`inject` IDÊNTICOS a lista de args (vírgula, B.26 — sem regra nova). `ref`(`&`)
modelo C# em 3 posições (declaração/chamada/retorno; depende de arenas seguras). **Cadeia evolutiva
(M.4):** `fn pointer → use(cópia)`; `arenas → ref → use(&)/inject-por-ref`; **`interfaces → inject+binding
(DI) → OOP`** (DI injeta por CONTRATO = interface, não tipo concreto; interfaces vêm ANTES de OOP).
**Erro como
VALOR** — variant de dois casos (`Valor | Error`), tratado com `match`,
propagado com operador `?`; **sem raise/on error/throw**; **sem tipo-tupla** (struct/variant
cobrem; `(x,y)` é só desconstrução). `teko::Error` = struct fixo (`message`+`file`+`line` em
compile-time); **sem stack trace** salvo via `.tsym` (debug symbols, evolução). **Contrato de
saída:** fim→0, `exit(n)`→n, **panic→stderr+≠0** (irrecuperável). **Perfis de build**
(VM-debug/nativo-release; severidade escala: higiene warning→erro, correção erro sempre).
**Segurança:** defesa em profundidade, nativos protegidos, ataque de nomeação no registry.
- **Rev. 8 (esta):** **refinamentos da escrita do primeiro código (o lexer).** Escrever o lexer
validou a "cara" e corrigiu/cravou detalhes finos. **`++`/`--` statement-only** (uma forma `p++`;
proibido em expressão → mata pré/pós e ilegibilidade; reaberto porque, sem `for`, incremento de
índice é onipresente no `loop`). **Strings (re-fundamentadas sob M.5):** a semente tem **só** o
delimitador único `"` (essência) + `$"..."` interpolação de **um nível** (conveniência de peso mínimo,
uso máximo — `{` literal é `{{`). **Auditoria M.5 (B.5):** o **`$`-count trick REMOVIDO** (inchaço —
estética, não peso; elegância≠austeridade); **raw `@"..."` e multilinha `"""..."""` → EVOLUÇÃO** (raros
no bare-metal da semente); a crase saiu. M.0 não tem jurisdição aqui (string é composta, não metal) —
M.5 reina sozinha.
**`.tkp` = manifesto do projeto** (nome canônico; tipo de artefato exec/lib; dependências + aliases;
**raiz do código declarada** — ex. `src/`, **invisível** nos namespaces/imports; sem `use src::...`).
**`.tkt` = testes junto ao `.tks`** (mesmo diretório/namespace → enxergam o privado sem furar
visibilidade; sem `/test`; extensão marca "teste", compilado no perfil de teste, ignorado no
release). **Regra de namespace:** dentro do mesmo namespace (diretório) tudo é **nu** (sem `use`,
sem qualificação — arquivos agregados se enxergam); **cruzando** namespaces, `use` o outro e
qualifique `namespace::Tipo`. (`::` tem dois usos: qualificação de namespace — some no mesmo
namespace — e membro-de-tipo-estático `Enum::Membro` — permanece.) **`ptr`/`uptr` opacos e
mínimos** (transporte-only; sem `mut_ptr`/`const_ptr` — se precisar dessa distinção, é sinal de
`ref`, evolução; FFI é unsafe-por-natureza, aceito e desencorajado). **Referência cruzada:** tipos
mutuamente recursivos **permitidos** (a AST exige; indireção interna gerenciada); ciclo entre
**namespaces proibido** (abordagem Go — erro de compilação mesmo sendo compilável; dependências
de módulo formam um DAG). **Fluxo de estado sem `ref`:** estado mutável **local** ao chamador
(`mut`), funções auxiliares **puras** recebem o estado e **retornam** o novo (num `struct`, pois
não há tupla); o chamador reatribui. Validado no lexer.
- **Rev. 9 (esta):** **o sistema de tipos, fechado a partir de primeiros princípios.** Esta sessão
preparou a fundação que o **parser** vai exigir (decisão deliberada de M.1: não construir o parser
sobre decisões não-tomadas — ver EVOLUTION). **Tipagem NOMINAL — aliases abolidos.** *Reescreve* a
decisão antiga de "`type =` dá alias grátis": agora `type X = Y` cria um **tipo distinto** (a
identidade vem do **nome**, não da estrutura). `type Meters = i32` é distinto de `i32`;
`type A = struct{x:u8}` e `type B = struct{x:u8}` são distintos (apesar de estrutura igual);
`type Pendente = bool` e `type Ativo = bool` são distintos. **Impasse que forçou:** o variant é uma
*união de tipos* discriminada por tipo, então casos precisam ser tipos distintos — com alias,
`Pendente = bool` colidiria (`bool | bool`). Argumento decisivo de consistência: structs **já** são
nominais (senão a unicidade do variant quebra); tratar struct nominal mas primitivo como alias seria
a *falsa distinção* que a Teko recusa (princípio dos primitivos). **Newtype transparente, regra Go:**
herda **todas** as operações do tipo base; literais não-tipados adaptam (`userId + 1` ✓); valores
tipados (do base ou de outro newtype) exigem **cast explícito** (`userId + raw_i32` ✗,
`userId + productId` ✗) — esse é o custo e a barreira. **DDD na raiz (benefício emergente):** tipos
de domínio distintos (`UserId`, `Email`, `Meters`) numa linha, sem o boilerplate de struct +
conversões + operadores do C# — modelagem de domínio correta fica *viável por ser barata*; a
capacidade está na **fundação** (o sistema de tipos), não numa biblioteca. **`variant` = união de
tipos DECLARADOS** (modelo derivado de "como seria sem o açúcar?" → struct com tag): cada caso é um
tipo separadamente declarado (struct/enum/primitivo) **unido por `|`** — **sem casos-inline, sem
construtor especial** (constrói-se com o literal de struct, `Number {value: 3.14}`), **sem "nome
mágico"** (a quarta-categoria de Rust/Zig/TS que confunde — aqui cases são *tipos*, categoria que já
existe). **Definição canônica:** *um tipo cuja constraint auto-imbuída é o conjunto fechado de tipos
que pode conter; o tipo interno só é acessível via `match`* (a assertividade obrigatória — sem match,
só se tem "a união"; é a segurança que a `union` de C não tem). **Marcadores sem dados = `enum`**
(o enum entra na união como um tipo); struct vazio **desencorajado por convenção**, não proibido.
Enum e variant **compõem** (não são o mesmo construto; enum = marcadores-valor sem dados, variant =
tipos com dados). **Unicidade:** os tipos da união devem ser distintos (a nominal garante isso).
**Construção** = literal do tipo, incluído na união **automaticamente** (sem cast); **desconstrução**
= `match`. **Princípio dos primitivos:** `char`=`byte`=`u8` (recusa distinção falsa — `'a'` é
notação de um `u8`, não um tipo); `bool` é tipo distinto (distinção *real* — dois valores, álgebra
booleana, não aritmética); `bool` = 1 byte (o byte é o quantum de endereçamento — arquitetura, não
falha); controle de bits = `flags` (pós-alpha). **`match`:** **escopo isolado por braço** (variáveis
vinculadas num braço não vazam). **Pendências para a próxima sessão:** o **`match`** em detalhe
(sintaxe de discriminação por tipo vs desconstrução de struct; exaustividade; o `?`-propagação vs
`?`-nulabilidade — colisão de símbolo ainda aberta). **Próximo código:** o **parser** (primeiro
`use` cruzando namespace; AST como `variant Node` recursivo; precedência C-corrigida).
- **Rev. 10 (esta):** **o `match` (rodada 1) e a resolução do `?` (rodada 2)** — as últimas decisões
conceituais antes do parser (M.1: não construir o parser sobre decisões não-tomadas). **`match` — um
construto, sintaxe unificada** (casa valores *e* tipos, conforme o sujeito). **Dois eixos:** *valor*
(escalar — casa literais/ranges/`a|b`; binding **opcional**, caso comum sem, pois você já tem o
valor) e *variant* (casa qual-tipo; binding **intrínseco**, pois revelar o tipo interno = nomeá-lo).
**Eixo 2 binding (ambos):** `Number as n` (vincula o todo, acessa por `.campo`) **ou** `Binary {
left, right }` (**select por nome** — lista só os campos que quer; omitidos são ignorados **sem
`..`/`_`** — melhor que Rust; apoia-se na nominalidade dos campos). **`when`** = guard que condiciona
o braço além do padrão (repete a mesma asserção com condições diferentes — estilo switch-expression
C#). **Exaustão FORÇADA, `_` OPCIONAL** (não obrigatório): a Teko tem exaustão *real* (união fechada,
conhecida em compile-time), então forçar `_` num match completo seria **código morto**. Satisfaz-se
por (1) cobrir tudo explícito — sem `_`, ideal p/ poucos casos (o compilador *quebra* o match quando
o tipo cresce, forçando decisão) — ou (2) cobrir alguns + `_`, ideal p/ muitos casos (enum de
dezenas) onde escrever todos é ruído. **Sutileza do `when` (derivada pelo Schivei, = lógica do
Rust):** um braço com `when` cobre *condicionalmente* → **não conta p/ a exaustão** (o compilador não
pode provar que a condição cobre tudo — indecidível); feche o tipo com um braço **sem `when`** ou com
`_`. **`_`-obrigatório (C#) rejeitado** (lá é muleta pela falta de exaustão real). **Escopo isolado
por braço** (variáveis vinculadas não vazam). **`?` (rodada 2) — colisão dissolvida.** A colisão
`?`-propagação vs `?`-nulabilidade **desaparece** porque **não há `?` de propagação de erro na
semente**: erro (`Valor | Error`) é tratado **sempre com `match`**. *Razão:* o `?` seria açúcar
**acoplado ao formato transitório** `Valor | Error` (alpha inline → evolução `Result(T)`), nascendo
com data de validade; o `match` é **geral e estável** (funciona sobre qualquer variant, sobrevive a
qualquer evolução do modelo de erro) e elimina uma **exceção** (o `?` existiria só p/ erro → erro
vira "só mais um match"). *Princípio:* **não criar açúcar dedicado sobre estrutura sabidamente
transitória.** Logo `?` fica **exclusivo de nulabilidade** (`T?` tipo, `?.` acesso seguro, `??`
Elvis) — **um domínio, sem ambiguidade**. Domínios disjuntos: **null → `?.`/`??`; erro → `match`**
(não se cruzam). **Custo aceito:** verbosidade em código com muitas chamadas falíveis (o parser) — a
troca é consciente (estabilidade/uniformidade > concisão transitória). **Evolução:** o `?` de
propagação *pode voltar* quando `Result(T)` for o formato **estável** (adiado, não morto). *Reescreve
B.1 ("propagado com `?`").* **Pendência:** nada conceitual — o parser pode ser escrito. **Próximo:**
o parser (primeiro `use` cruzando namespace; AST `variant Node` recursivo; precedência C-corrigida;
erro via match).
- **Rev. 11 (esta):** **auditoria da fundação + Rodada A do léxico (newline significativo).**
Antes do parser, **o lexer precisa tokenizar TUDO** que a semente tem (M.1 — a fundação garante a
estabilidade da obra). Auditoria: o lexer atual (só expressão aritmética) cobre ~10%; buracos:
keywords, maioria dos operadores, strings (modelo C# com interpolação), comentários (sintaxe
não-decidida) e **newline significativo**. Léxico classificado em **rodadas por menor dependência +
maior impacto:** A newline (conceitual, zero-dep, molda tudo), B comentários, C keywords (alto
impacto), D operadores, E literais, F strings (máxima dep — interpolação precisa do resto).
**Princípio:** o **lexer classifica o que é lexicamente conhecido** — keyword vira token próprio,
não `Ident` genérico (lexer = 1ª autoridade sobre *o que é*; parser decide *o que fazer*; analogia
arquiteto/pedreiro). **Rodada A — terminação:** **newline é o terminador de MENOR precedência** —
operador binário pendente ou delimitador de expressão aberto têm **precedência** e o suprimem
(continuação = **propriedade do operador**, não exceção ao newline). **`;` ressuscita como separador
INLINE** (múltiplos statements numa linha, `{ log(n); n*2 }`) — abolido como terminador *obrigatório*,
vive como separador *opcional* do inline. **`()` e `[]` SUPRIMEM o newline** — dentro deles a
**vírgula** separa e o fechamento encerra ("newline não manda"; vale p/ args em `()` e índice/literal
em `[]`). **`{}` é ESCOPO** — newline permanece **ativo** (termina statements internos, ou `;`
inline), *não* suprime (senão o bloco colapsaria). `{` e `(` se comportam *opostamente*. **Forma 1 —
o lexer decide** com info **léxica**: no newline checa (a) token anterior (pende?) e (b) **pilha de
delimitadores** — topo `(`/`[` → suprime; `{` ou vazia → ativo. Sem gramática → trabalho do lexer.
`++`/`--` **completam** (`p++\n` termina). **Pendência:** existe literal de array `[1,2,3]`? (não
afeta newline). **Próximo:** Rodada B (comentários).
- **Rev. 14 (esta):** **a constituição dos princípios + os métodos de instância (o "elefante") + a
auditoria de tokens.** Esta sessão deu à Teko uma **espinha dorsal explícita** e fechou a última
decisão estrutural antes do parser. **A CONSTITUIÇÃO (M.0–M.5, "as Leis da Robótica" da Teko):** os
princípios M viraram **lei fundacional ordenada** — *a Teko é metal (0), segura (1), explícita (2),
honesta (3), construída em ordem (4), e austera (5)*. **Regra-mãe: COMPÕEM, não competem** (diferente
de Asimov) — cada princípio rege um **aspecto/camada distinta da mesma decisão**, e juntos a
constroem (o metal define a *capacidade*, a segurança a *exposição*, a explicitude a *visibilidade*).
A **ordem numérica** só desempata a **fronteira genuína** (dois vereditos opostos sobre a MESMA
dimensão) — **menor número vence**. Composição no geral; hierarquia só no desempate. Três princípios
**novos** (nomeados agora, mas já regiam dezenas de decisões): **M.1 Segurança** (falha cedo e
explícito, nunca corrompe em silêncio — ∞/NaN no caixão, ÷0 panica, overflow wrap-definido, conversão
que perde = erro, excluir-o-inválido-por-construção, determinismo p/ ACID); **M.2 Explicitude** (o
explícito vence o conveniente-oculto — sem inferência que esconde, sem conversão implícita, sem
overload, legibilidade-local); **M.3 Honestidade** (a Teko não finge — `+` não mente, nominal sem
alias, pitch honesto Zig-tier-não-Rust). Os antigos **M.1(ordem-constr)→M.4** e **M.2(austeridade)→
M.5** renumerados. "**Beleza austérica**" (substantivo novo): a beleza está na *ordem entre poucas
regras*, não na quantidade; remete a "**ordem e progresso**" (a Teko progride ATRAVÉS da ordem — a
hierarquia disciplina cada avanço) e ao próprio nome **Teko** (modo-de-ser/essência — os princípios M
são o teko da linguagem). **MÉTODOS DE INSTÂNCIA (B.29, "o elefante"):** modelo **Zig-adaptado** —
funções no namespace do struct, método = açúcar. **Função dentro de struct é PERMITIDA mas
DESENCORAJADA** (deve parecer levemente estranha, de propósito — o caminho encorajado é função livre,
dados≠comportamento; struct-fn é exceção consciente p/ `to_string`/`parse`). **Método de instância =
função cujo 1º arg é um `self` SOLTO** — sem tipo, nome livre (o marcador é a POSIÇÃO, não o nome).
**O `self` solto sem tipo é a CHAVE que fecha a porta do `ref`:** sem o tipo, não há onde pôr o `*`,
então o receptor é SEMPRE uma cópia (value-semantics) — o problema do ponteiro (que "bate na arena" no
`&mut self` do Rust / `self: *Point` do Zig) **não pode nem ser EXPRESSO** (a sintaxe não tem o slot
do `*`). Fecha a porta removendo a sintaxe que a abriria. O `self` solto **É o marcador explícito**
(resolve B1/B2: você vê `self`, é método — não a inferência mágica do Zig). Exceção sintática
cirúrgica: arg-sem-tipo só (a) dentro de struct (b) no 1º arg. Chamado na instância com `.`:
`p.to_string()` ≡ `Ponto::to_string(p)`. **Margem pra crescer:** quando `ref` chegar (evolução), o
mesmo modelo comporta mutação de instância sem refazer a sintaxe. **Função SEM `self` no 1º arg =
estática do tipo** (`Ponto::parse(...)`, `Ponto::init(...)` construtores) — **"estático sem
`static`"**: função associada ao tipo, sem estado global, sem a keyword `static` banida (a Teko separa
o que C# confunde: estado-estático [banido] vs função-associada-ao-tipo [permitida]). **Acesso:**
método de instância `.`, função estática `::` (espelha B.25: receptor→instância→`.`; sem-receptor→
tipo→`::`). **Coexistência:** função-no-struct e função-livre não colidem (lugares diferentes p/
comportamento; o dev escolhe; nome livre p/ o receptor). **Padrão estrutural — SEM overload, SEM
override (semente):** identidade da função = NOME ÚNICO no escopo, não a assinatura; dois `to_string`
no mesmo struct colidem (erro) mesmo com assinaturas diferentes. Coerente com nominal (identidade por
nome), anti-mágica (sem overload resolution), legibilidade-local. **Destinos temporais divergem
(adiado≠morto):** **override RETORNA com OOP** (evolução, pós-generics — constitutivo do polimorfismo
de subtipo; OOP sem ele não é OOP); **overload NÃO retorna — nunca** (redundante com generics + default
args — cobre os mesmos casos com UMA função, não N, e sem resolução-por-assinatura; "SO-BRE-CAR-GA"). A
rejeição permanente de overload é possibilitada por escolher generics+default-args como os mecanismos.
**Default args = EVOLUÇÃO, a semente é de aridade RÍGIDA** (toda chamada passa todos os args); a
dependência circular se desfez (o caso que pedia default args — `to_string(format?)` — é ele próprio
evolução, pois format specifiers ricos = alpha/evolução, então nascem juntos). Na semente `to_string()`
é aridade-zero; `parse(s: str?)` recebe nullable (`?` = nulabilidade do VALOR, não default-arg). **B.29
resolve o `to_string`/`parse` de struct** (viram funções no struct — `to_string(self)` instância,
`parse(...)` estática — sem generics, sem static, sem função-livre-solta obrigatória; a interpolação
acha `p.to_string()`). **Reconcilia B.27** (o "Inês é morta" do auto-imbuir struct: o compilador não
gera, mas o dev escreve no struct). **AUDITORIA DE TOKENS (lexer completo):** **compostos completos** —
`+= -= *= /= %=` (aritm.), `&= |= ^=` (bitwise), `<<= >>=` (shift), `&&= ||=` (lógicos); regra uniforme
(todo operador binário-acumulável tem composto); **legitimados por M.0** (são operadores metálicos →
M.0 vence o desempate; NÃO são exceção à austeridade, estão fora da jurisdição dela). **Range:** os
tokens `..` (exclusivo) e `..=` (inclusivo) são SEMENTE (lexer os produz, maximal munch); range como
PADRÃO de match (`'0'..='9' =>`) é SEMENTE (essencial p/ o lexer da Teko casar faixas); range como
VALOR iterável (`1..100`) é EVOLUÇÃO (com `loop…from`) — na semente range é padrão, não valor. **`_`
desambiguado por contexto léxico:** isolado = wildcard de match; entre dígitos = separador (`1_000`);
em palavra = identificador. **Char vazio `''` = byte ZERO (`0u8`)** — corrige o "empty character
literal" da maioria (como char=u8, char é só notação de byte e o vazio é o byte zero; útil p/ o byte
nulo onipresente; trade: `''` acidental vira zero, não erro). **Escapes em char** (`'\n'`, `'\''`,
`'\\'`, `'\0'`, `'\u{…}'` só se couber em u8). **`!in`** = operador composto (semente, análogo a `!=`)
+ `!(x in xs)` também. **Pendências:** structs com métodos OOP-de-verdade (com mutação via ref) = quando
ref chegar; `#` diretivas = evolução; DI+singleton (substituto do static) = evolução-distante.
**Próximo: o PARSER** (toda a base léxica está pronta E auditada token a token).
(Frentes 1–7) — feito "desenhando" código Teko real para ler e validar.** Esta sessão fechou toda a
camada que o parser vai exigir. **M.0 (Princípio do Metal):** generoso com operadores (interface ao
silício), rigoroso com keywords/organização; o eixo metal↔abstração decide o marco (bootstrap /
0.0.1-alpha / 0.0.1-LTS); ponteiros opacos são exceção de segurança consciente. **Frente 1 (números):**
inteiros i8–i256 / u8–u256 (máx 256 bits); floats IEEE **f16**/f32/f64 (f16 justificado por
GPU/ML/hardware dedicado); **decimal de ponto-fixo** 512 bits (256 inteiro + 256 fracionário, exato,
não-default — exige sufixo/contexto como o `m` do C# mas com o nome-tipo `dec`); sufixo = nome do tipo
(`42i32`, `1.09dec`); defaults i32 / f32, decimal nunca default. **Promoção em três regimes:**
aritmética (`+ - * / %`) promove ao maior, float se misturado; bitwise (`& | ^ ~`) SEM promoção (mesma
largura E sinal ou erro; literal adapta se cabe); shift (`<< >>`) ASSIMÉTRICO (resultado = tipo do
operando ESQUERDO; `>>` aritmético/lógico pelo sinal da esquerda). Literal numa expressão ADAPTA ao
operando não-literal se cabe; dois tipados PROMOVEM; contexto flui top-down (o checker propaga o tipo
esperado na AST; overflow constante = erro de compilação). **Shift mecânica:** sem `>>>`/`<<<` (o sinal
da esquerda decide); contagem inválida (≥ largura ou < 0) **satura a ZERO** (não o mascaramento do
C/x86, que era bug); literal inválido = erro de compilação, runtime inválido = zero (warning só em
debug). **Conversões `as`:** permitidas onde o metal preserva (int menor→maior, float menor→maior,
int→float exato, float→int trunca); proibidas onde o metal perde silenciosamente (int maior→menor,
i64→f32, f64→f32, float-grande→int) — *reflete o metal onde preserva, protege onde perde*.
**Overflow:** operador `+ - *` **panica em debug, wrappa em release** (wrap é valor DEFINIDO, não
veneno como ∞/NaN); literal overflow = erro de compilação; `math::wrapping_*` wrappa sempre;
`math::*` checado = `T | Error`. **Política de checagem (geral):** verificações custosas LIGADAS em
debug/test, DESLIGADAS em release (M.0); o perfil de build decide. **Frente 3 (precedência):**
**maximal munch** (sempre o match mais longo, sem heurística de contexto — torna `//` sempre
comentário); **precedência segue Julia 100%** (pesquisado): bitwise/shift são OPERADORES (não funções),
bitwise ACIMA de comparação (corrige o erro admitido do C — `flags & MASK != 0` lê certo); comparações
ENCADEADAS (`a<b<c` ≡ `(a<b)&&(b<c)`). Hierarquia (forte→fraco): `()` · unário (`- ~ !`) · `<< >>` ·
`* / %` e `&` · `+ -` e `| ^` · comparação · `&&` · `||` · atribuição. `~` é complemento-de-bit (NÃO
negação de sinal: `~1=-2` signed, `~1u8=254` unsigned). **Divergência documentada do C** ("Teko segue
Julia, não C"). **Frente 2 (divisão/∞/NaN):** duas divisões — `/` (metal, default) **panica em ÷0 em
runtime** (÷0 é bug; ÷0 literal = erro de compilação); `math::div` (lib) retorna `T | Error` (quando ÷0
é esperado e o erro flui como valor). **∞/NaN NUNCA existem como valores** — interceptados na origem
(`/` panica, `math::div` retorna Error; "o caixão do NaN fica fechado"). A checagem de `math::div` roda
**em RELEASE** (não só debug — sem ela, float ÷0 = veneno ∞/NaN silencioso, int ÷0 = SIGFPE cru; ambos
piores que o panic). **Frente 4 (acesso + arrays):** cinco operadores, um papel cada — `.` campo de
instância · `::` caminho estático · `->` seta de retorno · `=>` braço de match · `[]` indexação
(máxima precedência). Notação de array `[]T` (prefixo, modelo Go — escala no aninhamento). Duas formas,
≤16 níveis: **jagged** (`[]T`, `m[i][j]`, sub-arrays independentes, indireção) vs **multidimensional
real** (`[,]T`, `m[i,j]`, contíguo, retangular). `static` **BANIDO** (estado global disfarçado; DI +
singleton é o substituto futuro, registrado só no REBOOT); `void` = "não retorna valor". **Arrays nunca
nuláveis — só elementos** (`[]Node?`); toda array inicializada (vazia `[]` ok); value-semantics
(crescer = copiar); fora-de-range = panic (literal = erro de compilação); `?[` BANIDO (desnecessário).
`arrays::at` → `T | Error` (checado). **Princípio das operações parciais (M.0):** toda operação falível
(÷0, overflow, fora-de-range) tem forma operador (metal, panica, literal→erro de compilação) + forma
lib checada (`T | Error`). **⚠️ Tempo:** operadores são SEMENTE (resolvidos por tipo concreto, sem
generics); checkers são EVOLUÇÃO (o `T | Error` genérico exige generics, B.11). **Convenção da semente:
`if`-antes ("olhe antes de pular")** — não é paliativo, é "excluir o inválido por construção"
(metal-puro, idioma C consagrado); os checkers são camada de conveniência da evolução. **Representação
de array = struct monomorfizado:** `[]T` → `struct {ptr: *T, len: u64}` por tipo; `[]` é açúcar.
**`arr.len`** = campo intrínseco u64 injetado pelo compilador (acesso por `.`, sem generics — retorno
concreto; a monomorfização dá o campo de graça). **Criação:** dimensionada-com-default `[3,4]i32`
(infere o tipo, preenche com default); com valores `[]`/`[,]`/`[][]` PRECEDE valores em `()` (o
marcador desambigua de chamada de função; `()` carrega valores; tipo inferido dos valores).
Dimensionalidade↔aninhamento devem casar (erro de compilação); retangular garante uniformidade (irregular
= erro), jagged permite irregular. Formatação vertical (newline decorativo dentro de `()`) torna
matrizes e jagged legíveis ("feio atraente"). **Frente 5 (delimitadores):** separação ESTRUTURAL — o
delimitador decide: `()` e `[]` → **vírgula** (obrigatória entre 2+ elementos, nunca após o último);
`{}` → **newline ou `;` inline** para TUDO (statements, campos de struct, membros de enum,
destruturação, match-binding) — **sem vírgula dentro de `{}`, jamais**. **`variant` é exceção — usa
`|`** (tipo-soma: união de alternativas exclusivas via tag, análogo-mas-distinto do `|` bitwise);
multilinha: primeiro caso na linha da keyword, `|` no FIM (operador pendente → suprime newline), fmt
alinha vertical. **Destruturação e match-binding usam a forma da struct** (`{}` com nomes, newline/`;`,
sem vírgula) — *reescreve B.15* (que tinha vírgula). **Teko terá `fmt` canônico (estilo Go)** — fim das
guerras de estilo. **Frente 6 (strings):** **`+` NUNCA concatena** (quatro razões: mentira-de-operador;
M.0 — concat é alloc+copy, não instrução; coerência — mistura implícita banida; **legibilidade-local** —
`+` não-sobrecarregado *revela* operandos numéricos, permitindo raciocínio na linha 500 sem buscar
definições distantes; vale em qualquer linguagem; generaliza ao princípio "operador revela a categoria
do operando"). **Concatenação = operador `$`** (string+string só; reusa o símbolo de string, fica DENTRO
da categoria string — diferente do `+` que cruzaria; maximal munch distingue `$"` interpolação de `$`
concatenação, custo zero; não-string = erro, use `as` ou interpolação; precedência baixa,
associativo-à-esquerda). Justaposição REJEITADA (rompe o newline significativo). **Interpolação
`$"...{x}..."` é AÇÚCAR sobre concatenação** (desugar: `"t " $ x.to_string() $ "..."`; resolve os
tokens — reduz a tokens que já existem; lexer com modos texto/expressão/formato, sem aninhamento na
semente). O `{}` É o pedido explícito de conversão-para-string (não viola mistura-implícita). **Format
specifiers do C#** (`{x:F2}`; `:` dentro de `{}` = formato). **Culture Universal/invariant SEMPRE por
default** (determinístico; localização é opt-in explícito no format, nunca do ambiente). **Formatação é
PRÓPRIA da Teko** (não a libc/printf do SO — locale-dependente = não-determinístico, e impossível em
bare-metal/SO-em-Teko); nada no metal resolve (algoritmo: divisão-resto p/ int, Ryū p/ float);
int=semente, float=alpha, specifiers ricos=alpha/evolução. **`to_string` (instância `.`) / `parse`
(estático `::`) auto-imbuídos pelo compilador** — só primitivos (escritos à mão, format C#) e enums
(gerados dos membros). **Structs NÃO** ("Inês é morta" — exigiria static [banido] ou generics
[evolução]); MAS o dev escreve **função livre monomorfizada** (`fn parse_ponto(s) -> Ponto | Error`) —
sem static, sem método-em-struct, sem generics. **Distinção que reconcilia:** auto-imbuído pelo
compilador (permitido — método gerado por tipo) vs `static` declarado pelo usuário (banido — estado
global). **Interpolação de struct/variant = ERRO** (sem `to_string` auto-imbuído; o compilador não
infere a função livre; variant precisa "explodir" via match). **`Error` é exceção** — tipo especial
**global** (sem namespace), built-in, com `to_string() -> str` auto-imbuído (erros existem pra ser
comunicados); **sem `parse`** (erros são produzidos, não parseados), **sem parâmetro de formato**
(é mensagem); estrutura `{ message: str; line: u32; file: str; trace: str? }`. **Padrão recorrente (3×:
`.len`, `to_string`, `parse`):** feature universal sem generics = código concreto por tipo (escrito à
mão p/ primitivos finitos, gerado p/ enums, função livre p/ structs); `T | Error` é notação decorativa.
Teko **substitui generics/métodos/static por funções livres monomorfizadas por tipo** — simplicidade do
mecanismo > concisão da abstração. **Frente 7 (literais):** hex `0x`, binário `0b`, octal `0o` (dígitos
case-insensitive, prefixo minúsculo, infere tipo por valor, combina com sufixo); separador `_` só entre
dígitos, qualquer base; escapes `\n \t \r \\ \" \0 \u{HEX}` (`\u` → bytes UTF-8, coerente com char=u8);
float com notação científica (`e`/`E` ±), `.5` e `5.` = ERRO (protege o `.` de acesso, exige
`0.5`/`5.0`), `_` em ambas as partes. **`#` reservado** para comunicação com o compilador
(diretivas/atributos, evolução). **Pendências:** **structs com métodos de instância** (decisão maior,
adiada — `to_string`/`parse` de struct dependem dela). **Próximo:** o **parser** (toda a base léxica
está pronta).
**Rodada B (comentários):** `//` linha, `/* */` **aninhável** (corrige defeito do C); repetição do
char de abertura (`///`, `/****`) **não é léxica** (lexer trata `///` como `//`); doc comments **não
são construto** — convenção de tooling futuro (preferência Javadoc sobre XML-doc do C#), a semente
não registra. **Regra-chave:** comentário é **descartado *antes* da decisão de terminação** —
"encontra o comentário, descarta, trata o que resta" — preservando a regra do operador-pendente (um
`//` trailing não pode esconder o `+` que pende). Lexer reconhece (pra pular) mas **não emite** ao
parser nem à AST (trivia pura). **Rodada C (keywords):** o **lexer classifica o conhecido** (lê
palavra → tabela → keyword específica ou `Ident`); **primitivos (`i32`/`u8`/`bool`…) = tipos
PREDEFINIDOS, não keywords** (injetados como o `teko::`; não incham a lista); `as`/`in` =
operadores-palavra (`as` cobre cast + binding de match + alias de import); lógicos são símbolos
(`&&`/`||`/`!`). Lista cravada: `let mut const fn return type struct variant enum / match when loop
break continue defer / in as / pub exp / use / true false null`. Fora: `flags` (pós-alpha),
primitivos (predefinidos), `self`/`super`/`crate` (abolidos), and/or/not (símbolos). **Rodada C.5
(teia condicional):** `if`/`else if`/`else` **É EXPRESSÃO** (deduzido: como `return` é só saída
antecipada e "última expressão = valor", um `if` no fim da função *tem* que produzir o valor → if é
expressão) — **é o ternário legível** (`let max = if a>b {a} else {b}`); `else` obrigatório quando
captura valor; **`unless` BANIDO** (é exatamente `when !cond`, zero capacidade nova, piora condições
compostas); **sem `?:`**; **sem `when`/`unless` posfixado** (`if` cobre; o `break when` do lexer virou
`if cond { break }`); **`when` = exclusivo do guard de match**. Web fechada: `if`=condição booleana,
`when`=guard, `match`=padrões; zero redundância. **Rodada C.6 (teia de mutabilidade):** **`mut` SÓ em
variável local** — a regra na forma mínima. Razão unificadora (**criar-vs-receber**): a variável
local **cria/possui** seu valor → pode `mut`; parâmetros e match-bindings **recebem** valor de outra
fonte → **imutáveis** (mutar o recebido abre confusão de leitura + **controle de arena**: de quem é a
memória?). **`mut` governa o valor inteiro** (sem granularidade de campo estilo `readonly` do C# —
valor-semântico não tem o *aliasing* que a justificaria; auditoria da semente confirma: usos são
acumuladores reatribuídos por inteiro). **Transitiva por valor**; arrays seguem (`mut v` governa o
array todo); escrita (`=`,`++`,`+=`,`v[i]=`) exige `mut`, leitura nunca; **observabilidade = evolução**.
**Premissa de recursos:** Teko mira alvos com memória suficiente (não embarcado-escasso — senão teria
ido de ponteiro/ref desde o início); cópia é escolha consciente (barata em CPU, cara em memória);
filosofia UNIX. **+ Refino de tipos:** `type X = Y` **É** (identidade, com a forma de Y) não
**representa** (delegação = alias) — "você *é* um agente de IA, não *representa* um". **+ Índice de
redefinições (§10):** salvaguarda anti-fatiamento (erro-`?`→match, alias→nominal, `break when`→`if`,
`unless`→banido). **Próximo:** Rodada D (operadores — depende de C.6, pois `+=`/`++` precisam da regra
de mutabilidade).

---

## 1. Tese (por que reiniciar)

O cruzamento das Fases 1–19 revelou um **front bifurcado**: dois caminhos `source → IL` (o
original com AST e o `frontend_interop.c` sem AST, "sopa de registries globais"). As features de
linguagem e a disciplina de goldens foram para o segundo; a gramática rica ficou no primeiro.
**Tudo abaixo do IL é unificado e bom** (orquestrador `codegen_metal`, 16 emitters, linker `tld`,
runtime de syscall). Logo: **mantém-se o backend, descartam-se os dois fronts e a matriz de
keywords, constrói-se UM front novo** sobre um kernel mínimo — o que torna o self-hosting viável.

---

## 1.5 Propósito e prioridades (NOTA DOS AUTORES — *fora* do EVOLUTION)

> ⚠️ **Isto é nosso, não dos agentes.** Não migrar para o TEKO_EVOLUTION. Se os agentes
> souberem o "destino" (banco, SO, data table), vão **inferir requisitos e decidir cedo demais**
> (ponteiros prematuros, features embutidas, prioridades não-acordadas). O EVOLUTION diz *o que a
> linguagem é*; o propósito é *o que ela vai construir* — e isso é decisão nossa, no tempo certo.

**Os três propósitos (ordem de intenção, não cronograma):**
1. **Banco de dados ACID, multi-modelo/domínio**, fundado sobre **grafos + SQL**.
2. **Um sistema operacional** (talvez).
3. **Suporte nativo a data table.**

**Como isso JUSTIFICA retroativamente o que já cravamos** (o instinto estava alinhado ao destino):
- **ACID exige determinismo absoluto** → valida banir **∞/NaN como valores**, **interceptar ÷0
  antes do veneno**, **overflow = wrap-definido (não poison)**, **zero comportamento indefinido**
  (saturação de shift definida, conversão-que-perde = erro). Um valor envenenado num índice ou
  transação corromperia a consistência silenciosamente — o pior para um banco. A aversão a "veneno
  silencioso" não era estética; era requisito de ACID.
- **Banco lida com dinheiro** → valida o **`decimal` ponto-fixo 512 bits** nativo (float arredonda
  centavos = viola consistência; `DECIMAL` é primeira-classe em todo SGBD sério). Não era luxo.
- **Multi-modelo grafos** → valida **tipos mutuamente recursivos** e **`variant` = união de tipos
  declarados** (um nó é variant de tipos de nó; arestas conectam recursivamente). **SQL tipado por
  esquema** → valida o **nominal sem alias** + **DDD na raiz** (coluna `UserId` ≠ `OrderId` mesmo
  ambos i64 — é literalmente o que um esquema quer).
- **SO** → reforça o **M.0** ao extremo (anel 0, bitwise/shift de primeira classe = pão de kernel),
  e valida **sem closures / sem reflexão / value-semantics+arena** (sem runtime pesado no kernel).
- **Data table** → afinidade com Julia (não à toa gravitamos ao modelo Julia na precedência);
  exercita **indexação rica** (`[i, j]`, ranges, seleção) e o **inventário numérico completo**
  (f16/f32/f64 científico + decimal financeiro + inteiros variados).

**A ressalva que protege a linguagem:** **muito do propósito é feito DEPOIS, em Teko nativo** —
banco/SO/data-table são **programas escritos na linguagem**, não **features da linguagem**. A
linguagem precisa ser *boa o suficiente* (determinística, controle de metal, tipos precisos) para
torná-los *possíveis*; não precisa *embuti-los*. Tratar "data table nativo" como requisito da
linguagem a **incharia**; tratá-lo como projeto-futuro-em-Teko a mantém **enxuta** (só os primitivos
certos), com o data table construído *sobre* ela. Coerente com a filosofia UNIX (ferramentas
pequenas e compostas, não monólitos).

**A prioridade implícita (para NÓS lembrarmos, não para o agente decidir):** os três propósitos
pressionam **memória indireta eficiente** (banco: índices/grafos/B-trees; SO: endereços/memória
física; data table: tabelas grandes) — a value-semantics pura não basta para eles. A **semente**
pode ser value-semantics (basta para o bootstrap/self-host). Mas o **primeiro projeto real (o
banco) bate na parede da memória imediatamente** → o design de **ponteiros opacos** deve ser a
**primeira coisa séria pós-self-hosting**, antes de generics, function-pointers, etc. Isso reordena
a evolução — mas é **decisão nossa, registrada aqui**, não inferência que o agente deva fazer.

**Substituto de `static` — DI nativo + singleton (visão futura, NÃO no EVOLUTION):** o EVOLUTION
bane `static` (estado mutável compartilhado = global disfarçado) sem dizer *com o que substituir* —
de propósito, para o agente não implementar cedo. A nossa intenção: quem precisa de "algo único
compartilhado" **registra um singleton via DI (injeção de dependência) nativo**. Isso corrige a
falha histórica de OOP — `static` mutável é intestável (não se mocka), acoplamento oculto,
problemas de concorrência, e o *static initialization order fiasco* do C++. Com DI+singleton: a
dependência é **explícita** (aparece na assinatura, não escondida), **testável** (injeta-se um
mock), **controlada** (o container gerencia lifetime/inicialização), **sem estado global solto**. É
o padrão que frameworks (Spring, .NET DI) adicionaram *por cima* das linguagens — a Teko o quer
**nativo**. Feature **grande** (container, registro, resolução, lifetimes), portanto **evolução
distante**, não semente. Registrado aqui como direção que *justifica* o banimento de `static` agora
(não é só "proibimos", é "proibimos porque há caminho melhor planejado").

## 1.6 Bootstrap & processo (C23 ↔ Teko — quem escreve o quê)

> **Legislado** (vácuo preenchido — ver `TEKO_LEGISLATION.md` §Bootstrap & process). Decisões:
> **(1) Duas linguagens, dois papéis, ambas escritas:** a semente (compilador) é **C23 puro** (a
> ferramenta que traduz Teko→binário); os exemplos/testes são **Teko** (o alvo). Ambas coexistem até o
> self-host (o C23 precisa compilar a semente Teko). A spec suprema que ambos servem é a **legislação**.
> **(2) Agentes BILÍNGUES** — um tipo de agente; a tarefa decide a linguagem pelo critério:
> **implementa o compilador → C23** (lexer/parser/checker/codegen/linker); **demonstra/testa a
> linguagem → Teko** (exemplos, `.tkt`, código-alvo). **(3) Overhead de disciplina:** C (qualquer
> versão) **viola a maioria das leis da Teko** (conversão implícita, UB, NULL, overflow indefinido,
> precedência `a & b == c`) — não é contradição, é a natureza do bootstrap (galinha-e-ovo). Mas exige
> que o C23 da semente seja escrito **"como se a Teko o governasse"**: o agente honra À MÃO o que o C
> não força (inicializar tudo, sem UB, checar overflow, parênteses explícitos onde a precedência do C
> trai, erro explícito). C é a ferramenta; as intenções da Teko guiam o uso. Regido por M.4 (ordem de
> construção) + M.1/M.2/M.3 aplicadas ao ato de construir. Vale até o self-host; depois a linguagem
> impõe suas próprias leis e o overhead some.
>
> **Organização de fontes (C e Teko regidas pelas normas):** a árvore C do compilador **espelha** a
> árvore de namespaces Teko (mesma divisão, mesmos nomes: `src/lexer/lexer.c`+`.h` ↔ namespace `lexer`).
> O **`.h` = fronteira de visibilidade** (o que seria `pub`/`exp`; privado vira `static` no `.c`),
> não declaração técnica. **Agregação por coesão** (`lexer.c` = todo o lexer, não fragmentado; um `.h`
> por etapa; anti "sopa de registries"). **Preprocessor disciplinado** (`#define` só p/ guard, nunca
> metaprog). **Testes ao lado** (`lexer_test.c` em `src/lexer/`, espelha `.tkt`; build exclui do release
> à mão). Árvore canônica segue o pipeline (M.4): `src/{lexer,parser,ast,checker,codegen,linker,teko}/`
> + `main.c`. A mesma árvore, por espelhamento, descreve o compilador C e o futuro compilador Teko
> self-hosted. Regido por M.4 (estrutura=ordem de construção) + M.2/M.3 (fronteira explícita, sem magia
> de preprocessor) + M.5 (coesão).

---

## 2. Contrato de design (travado)

### 2.1 Léxico — tokens e símbolos
O lexer da semente reconhece: **identificadores**, **keywords** sem sigilo, **literais**, e
**operadores por forma de pontuação ASCII**. *Atualizado na Rev. 14 (correção do identificador vs `_`).*

**Identificador (regex corrigido — dois ramos):** `[A-Za-z][A-Za-z0-9_]*` (começa com letra — sempre
válido) **OU** `_+[A-Za-z0-9][A-Za-z0-9_]*` (começa com um ou mais `_`, mas EXIGE ao menos uma
letra/dígito em seguida). Ou seja: `_foo`, `_1`, `__bar`, `foo_bar` = identificador; **`_` solto = o
token reservado wildcard** (não casa o regex); **`__`/`___` (só underscores) = não casa o regex →
maximal munch produz vários `_` → o parser falha NATURALMENTE** (não há gramática para wildcards
consecutivos; exclusão por construção, sem regra anti-`__` especial). *Corrige a colisão: o regex
clássico `[A-Za-z_]…` aceitava `_` solto, que colidia com o `_` token-reservado.*

- **Maximal munch** (regra geral do lexer): sempre o match mais longo, sem heurística de contexto
  (torna `//` sempre comentário, `<<` sempre shift, `$"` sempre interpolação).
- **O lexer classifica o que é lexicamente conhecido:** keyword vira token próprio (não `Ident`
  genérico); o lexer é a 1ª autoridade sobre *o que é*, o parser decide *o que fazer*.
- **Operadores por forma, só ASCII** (digitáveis em qualquer teclado; nunca Unicode). O conjunto:
  - **Aritméticos:** `+ - * / %`.
  - **Bitwise:** `& | ^ ~` (`~` é complemento-de-bit, não negação de sinal).
  - **Shift:** `<< >>`.
  - **Comparação:** `< > <= >= == !=` (encadeáveis: `a<b<c`).
  - **Lógicos:** `&& || !` (símbolos — sem `and`/`or`/`not`).
  - **Atribuição:** `=` e compostos (`+=` etc.); `++`/`--` statement-only.
  - **Acesso/estrutura:** `.` (campo de instância) · `::` (caminho estático) · `->` (seta de
    retorno) · `=>` (braço de match) · `[]` (indexação).
  - **Nulabilidade:** `?` (tipo `T?`, acesso `?.`, Elvis `??`) — exclusivo de nulabilidade.
  - **Concatenação:** **`$`** (string + string). Reusa o símbolo de string; maximal munch distingue
    `$"` (interpolação) de `$` (concatenação). Precedência baixa, associativo à esquerda.
  - **Tipo-soma:** **`|`** no `variant` (além do `|` bitwise — o contexto distingue).
  - **Anotação de tipo:** `:` (`x: i32`; dentro do `{}` de interpolação, `:` é format specifier).
- **`$` NÃO é mais ilegal** (era, na semente antiga): agora é o símbolo das operações de string
  (interpolação `$"..."` e concatenação `$`). **`#` é RESERVADO** para comunicação com o compilador
  (diretivas/atributos/pragmas) — evolução; a semente não o usa, mas o símbolo está guardado.
- **`char` = `byte` = `u8`** (recusa a falsa distinção); `'a'` é notação de byte. `string` é bytes
  UTF-8; `.len` em bytes. (Texto Unicode-aware de 32 bits é evolução — não gravar "runa" como
  sinônimo de char.) `bool` é tipo distinto (`true`/`false`).
- **Precedência segue Julia 100%** (não C — divergência documentada): bitwise ACIMA de comparação;
  hierarquia forte→fraco: `()` · unário (`- ~ !`) · `<< >>` · `* / %` e `&` · `+ -` e `| ^` ·
  comparação · `&&` · `||` · `$` · atribuição. Esquerda-associativa exceto atribuição (direita) e
  comparação (encadeia).

**Literais (Frente 7):**
- **Numéricos:** decimal; **hex `0x`**, **binário `0b`**, **octal `0o`** (dígitos case-insensitive,
  prefixo minúsculo). Sufixo = nome do tipo (`42i32`, `0xFFu8`, `1.09dec`); sem sufixo, infere por
  valor (default i32/f32; decimal nunca default). Hex/binário servem o bitwise (máscaras, bits).
- **Separador `_`** só entre dígitos, qualquer base (`1_000`, `0xFF_FF`, `0b1010_1010`).
- **Float:** notação científica (`1.5e10`, `e`/`E` ±); **`.5` e `5.` são ERRO** (exigem `0.5`/`5.0` —
  protege o `.` de acesso); `_` em ambas as partes.
- **Strings:** `"..."` (escapes), `@"..."` (raw), `$"..."` (interpolação), `"""..."""` (multilinha),
  prefixos combináveis. Escapes: `\n \t \r \\ \" \0 \u{HEX}` (`\u` → bytes UTF-8, coerente com
  char=u8).

**Keywords (cravadas — Rodada C):** `let mut const fn return type struct variant enum match when loop
break continue defer in as pub exp use true false null`. **Fora:** `flags` (pós-alpha), primitivos
(`i32`/`u8`/`bool`… são tipos PREDEFINIDOS, não keywords), `self`/`super`/`crate` (abolidos),
`for`/`while` (só `loop`), `unless` (banido), `static` (banido), `and`/`or`/`not` (símbolos).

### 2.2 A cara da linguagem (sintaxe)
Dialeto-C **enxuto**: estrutura familiar, ruído cortado. "O código é a documentação" vem dos nomes,
labels e âncoras — **não** de imitar inglês natural (uncanny valley — AppleScript/COBOL).

- **Blocos `{}`** — forma **única** de bloco e corpo. Curtos por orientação a expressão (abaixo).
  Sem `=>`-de-função, sem inline-sem-chaves (ambíguo sem `;`, é o bug "goto fail").
- **`;` abolido** — *newline* termina statement. **Continuação:** se a linha termina em operador
  binário pendente (`+`, `&&`, `,`) ou parêntese/colchete aberto, a quebra é ignorada (estilo
  Go/Swift; sem `\` de continuação).
- **`return` só para saída antecipada;** a **última expressão do bloco é o valor**.
- **`if` é expressão** (`let x = if cond { a } else { b }`) — mata o ternário. **`match` é
  expressão** pela mesma regra.
- **Parênteses:** **fora** de control-flow (`if cond`, `while cond`, `for x in xs` — sem `()`);
  **dentro** de chamada/assinatura (`foo(a, b)`, `fn foo(x: i32)`) e de **agrupamento de expressão**
  (`(a + b) * c`). Regra: o parêntese carrega significado → fica; só delimita redundante → sai.
- **Tipo de retorno atrás, com `->`** (`fn f(x: i32) -> i32`). Nome primeiro, tipo depois — legível
  com tipos compostos.
- **Inferência no corpo, anotação obrigatória na assinatura.** `let x = 42` infere; `let b: u8 = 42`
  força. Parâmetros e retorno **sempre** anotados (contrato).
- **Labels de argumento** (estilo Swift): `move(to: alvo, by: passo)` — a chamada lê como frase,
  a gramática continua rígida.
- **Âncoras posfixadas:** `when` / `unless` (`return x when cond`, `raise e unless ok`).
- **Precedência: C-corrigido.** Escolar para aritmética (`*` antes de `+`); **poucos níveis
  (~5, estilo Go)**, não os 15 do C; **bitwise com precedência sensata** (consertando o canto que o
  próprio C admite ser erro). Associatividade à esquerda para aritméticos.

```teko
// a cara, em uma função: sem ;, sem return redundante, parênteses só onde importam
fn clamp(value v: i32, max limit: i32) -> i32 {
    if v > limit { limit } else { v }
}
```

**Laços e `defer`.** Único laço **primitivo**: `loop { }` (infinito; sai por `break`, pula por `continue`). **`while` não existe** (redundante com `loop` + `if cond { break }`). **`defer`** é **block-scoped**: pertence ao `{}` que o contém (função *ou* corpo de `loop`) e dispara em **toda** saída desse bloco — fim natural, `break`, `continue`, `return` — em **ordem reversa** de registro (modela aninhamento de recursos: abre A, abre B, fecha B, fecha A). Num `loop`, isso significa que um `defer` no corpo roda **a cada iteração**, inclusive quando a saída é por `continue`.

```teko
// iteração manual na SEMENTE (sem loop…from, que é evolução): defer roda em toda saída
mut i: i32 = 0
loop {
    defer { i += 1 }              // roda no fim de cada iteração (inclusive no continue)
    if i >= arr.len { break }     // condição de SAÍDA (cuidado: >=, não <=)
    if i % 3 == 0 { continue }    // pula múltiplos de 3 — o defer ainda roda
    log($"Hello {i}")
}
```
> A iteração manual acima tem 3 pontos clássicos de erro (direção do `break`, posição do `defer`, interação `defer`+`continue`). Por isso o **`loop … from`** (§Evolução) é a forma idiomática na linguagem-madura — mas é **evolução em Teko, não semente**. (Nota: `break`/`continue` posfixados com `when` foram banidos — B.20; usa-se `if cond { break }`. Interpolação é `$"..."` — Frente 6, a crase saiu.)

### 2.3 `match` e o operador `in`
**`match` é o único construto de ramificação** (sem `switch` separado — um `match` sem captura age
como statement; com `let x =` é expressão).

- `=>` separa padrão do resultado; o lado direito é **expressão ou bloco `{}`**.
- **`when`** nos arms = guarda condicional. **`|`** agrupa padrões (`2 | 3`). **`_`** é o curinga.
- **Exaustividade forçada** — erro de compilação se faltar variante; `_` é a válvula explícita.
- **Sem `;`, sem `break`, sem fall-through** (o fall-through do C/C# é erro de design — banido).

```teko
match comando {
    Iniciar      => liga_motor()
    Parar        => desliga_motor()
    Reiniciar    => { desliga_motor(); liga_motor() }
    _            => registra_erro()
}
```
> **Separador de arm RESOLVIDO (Frente 5):** dentro de `{}` é **newline (ou `;` inline), nunca
> vírgula** — a regra de delimitadores vale também para os arms do match. O binding desconstrutivo
> usa a forma da struct (`Binary { left; right }`, newline/`;`).

**`in`** — operador de **membro de coleção, tipado**: o operando esquerdo deve ser do **tipo do
elemento** da coleção direita. Dá de graça: `elem in array` (qualquer `T`), `char in string`
(porque `string` é `[]u8`, elemento é byte). **Rejeita por tipo, sem regra especial:** `array in
array`, substring (`string in string`), code-point-multibyte. Serve **contenção** (`x in xs` →
`bool`) e **iteração** (`for x in xs`) — mesmo conceito. **Negação:** `!in` (composto, análogo a
`!=`) ou `!(x in xs)`, escolha do dev — **sem** keyword `not`. **Substring e busca Unicode-aware
são funções de biblioteca**, não o operador (não sobrecarregar `in`).

### 2.4 Construtos de tipo (quatro)
Quatro construtos, propósitos **disjuntos** (não é redundância — cada um resolve um problema
diferente; nomes honestos, ao contrário de Rust/Swift que enfiam três conceitos em `enum`):

> **Tipagem NOMINAL — aliases abolidos (rev 9, reescreve decisão anterior).** Todo `type X = Y`
> cria um **tipo distinto** — a identidade vem do **nome**, não da estrutura. Não há *type alias*.
> `type Meters = i32` é distinto de `i32`; `type A = struct{x:u8}` e `type B = struct{x:u8}` são
> distintos (apesar de estrutura igual). **Newtype transparente, regra Go:** herda todas as
> operações do base; literais não-tipados adaptam (`m + 1` ✓); valores tipados (base ou outro
> newtype) exigem **cast explícito** (o custo, e a barreira). **DDD na raiz:** tipos de domínio
> (`UserId`, `Email`) numa linha, sem boilerplate — a capacidade está na fundação, não numa lib.
> O `=` permanece (sintaxe `type X = Y` intacta), mas significa uniformemente "define um novo tipo
> nominal", nunca "apelido de". *Motivação:* o `variant` é união de tipos discriminada por tipo →
> casos precisam ser distintos; e structs já eram nominais → tratar primitivo como alias seria a
> *falsa distinção* que a Teko recusa (§princípio dos primitivos).

| Construto | É | Header de runtime? | Na semente? |
|---|---|---|---|
| **`struct`** | Produto — tem-isto-**E**-isto (campos) | Não (layout em compile-time) | ✅ |
| **`variant`** | **União de tipos declarados** — é-isto-**OU**-aquilo (tag gerenciada) | Sim (a `tag`) | ✅ (a AST exige) |
| **`enum`** | Nomes para inteiros **sequenciais** (0,1,2…) — marcadores sem dados | Não (é um inteiro) | ✅ (`TokenKind`) |
| **`flags`** | Nomes para **potências de 2**, compostos com bitwise | Não (é um inteiro) | ❌ **pós-alpha em Teko** |

**`variant`** — **união de tipos que já existem** (rev 9; derivado de "como seria sem o açúcar?" →
struct com tag). Cada caso é um **tipo separadamente declarado** (struct/enum/primitivo) **unido por
`|`**. **Sem casos-inline, sem construtor especial** (constrói-se com o literal de struct), **sem
"nome mágico"** (a quarta-categoria de Rust/Zig/TS — aqui cases são *tipos*). Apoia-se na **tipagem
nominal** (rev 9): tipos com estrutura igual mas nomes diferentes são distintos, então a união os
discrimina. **Definição canônica:** um tipo cuja *constraint auto-imbuída* é o conjunto fechado de
tipos que pode conter; o tipo interno só é acessível via `match` (a assertividade obrigatória — a
segurança que a `union` de C não tem). É a fundação do compilador (a AST; o compilador é um `match`
sobre a união). Variantes recursivas usam **indireção interna** (§2.6) — sem ponteiro exposto.
Marcadores sem dados = `enum` (entra na união como tipo). Construção inclui na união
**automaticamente** (sem cast); nulabilidade só no sítio de uso (`?Node`), nunca no variant.

```teko
// cada caso é um TIPO declarado (sem nome mágico, sem construtor inline)
type Number = struct { value: f64 }
type Ident  = struct { name: []u8 }
type Binary = struct { op: []u8; left: Node; right: Node }

// o variant UNE os tipos declarados
type Node = variant Number | Ident | Binary

fn valor_de(node: Node) -> f64 {
    match node {
        // binding: 'as' liga o todo; '{ }' seleciona campos por nome (B.15)
        Number as n   => n.value
        Binary { op; left; right } => aplica(op, valor_de(left), valor_de(right))
        Ident as i    => procura(i.name)
    }
}
// construção: literal do struct, aceito onde Node é esperado (inclusão automática na união)
// let n: Node = Number { value: 3.14 }
```
> **Nota (rev 9):** a *sintaxe exata do `match`* sobre a união — discriminar por tipo vs desconstruir
> o struct, e o binding das variáveis (escopo isolado por braço) — é a **próxima decisão** (ainda não
> cravada). O exemplo acima é ilustrativo da *forma do variant*, não do match final.

**`flags`** — *(pós-alpha, em Teko; registrado para não se perder)* o **compilador auto-atribui
os bits**, eliminando o bug clássico de flag-mal-numerada. Regras: membro sem valor → `1<<n` desde
`1<<0`; membro-zero (`None`/`Empty`) é o vazio e **não** avança a sequência; **alias composto**
(`All = Red | Blue`) usa a expressão e **não** avança; **ordem é significativa** (reordenar muda
valores — e quebra dados serializados, quando houver serialização).
```teko
type Colors = flags { None, Red, Blue, All = Red | Blue }  // None=0, Red=1, Blue=2, All=3
```

**Literal de struct (sem construtor).** `struct` puro é inicializado por literal — campos com `:` na **declaração**, `=` na **inicialização**. **Separadores `;`/newline, nunca vírgula** (Frente 5):
```teko
type Point = struct { x: i32; y: i32 }
let a: Point = { 0; 0 }              // posicional (forma primária)
let b: Point = { x = 0; y = 0 }      // nomeado (para opcionais/defaults; auto-documenta)
```
**Inicialização é obrigatória e completa:** todos os campos devem receber valor — **não existe campo com lixo / não-inicializado**. (`required`/defaults é evolução.) Inicialização posicional primária; nomeada quando há campos opcionais. **Desconstrução usa a forma da struct** — `let { x; y } = point` (por **nome**, `{}` com newline/`;`, **não** a forma posicional `(x, y)` que foi superada na Frente 5; consistente com o match-binding). É açúcar de **legibilidade**, *não* de performance.

### 2.5 Tipos: sem `any`, sem reflexão
**Não há `any`/`object`/`interface{}`/tipo-indefinido.** Todo tipo é conhecido em **compile-time** — é o que **torna o zero-runtime-reflection possível** (`any` exige reflexão para ser útil; sem reflexão, `any` é caixa-preta inútil/perigosa). Os dois são mutuamente exclusivos; escolhemos não-reflexão, logo não-`any`.
- Polimorfismo: **generics estáticos** (monomorfizados, compile-time — §evolução) ou **`variant` fechado** (soma explícita dos tipos possíveis). Nunca caixa-preta aberta-em-runtime.
- Um `T` genérico *parece* indefinido no corpo, mas é **resolvido e convertível** — sem reflexão.
- **`void*`-opaco só na fronteira FFI**, convertido explicitamente (via `marshall`); não é tipo de primeira classe da linguagem.

### 2.6 Compilação por projeto (`.tkp`), não por arquivo
**Namespaces = diretório** (não declaração explícita): o caminho de pastas *é* o namespace (estilo Go/Rust). Arquivos num diretório são **agregados como um só** (vários construtos por arquivo; SRP = coesão, não um-por-arquivo). **Visibilidade mapeia em escopos físicos:** privado (default) = o **diretório/namespace** (arquivos no mesmo dir se enxergam; a fronteira é o dir, não o arquivo); `pub` = o **projeto**; `exp` = **outros projetos**.
**Navegação `::` vs `.` (padrão Rust):** `::` para **caminho estático** (namespace/tipo/função/constante — resolvido em compile-time): `std::strings::parse(x)`, `MeuTipo::CONST`. `.` **só** para acesso a membro de um **valor** (campo; método de instância na evolução): `pedido.total`. Regra única, sem exceção: **estático → `::`; valor → `.`**.
> **Próxima etapa (pendente):** aninhamento (constantes locais ✅; funções aninhadas / **closures** = evolução), e a sintaxe exata de `use`/`import`.

O compilador lê o **`.tkp`**, descobre os arquivos e **compila o projeto inteiro de uma vez** — vê todos os arquivos antes de resolver referências. Consequências: **sem headers**, **sem ordem de declaração** (uma função pode chamar outra definida depois ou em outro arquivo), sem forward-declaration. Mata a causa-raiz de metade da dor do C (headers, include order, preprocessor). **Compilação incremental** (recompilar só o que mudou) é **evolução**.

### 2.7 Visibilidade — privado por default; impacto no binário
Três níveis ascendentes; **privado é o default, sem keyword** (força pensar antes de expor, superfície mínima):
- *(nenhuma keyword)* → visível só no **módulo**.
- **`pub`** → visível a outros módulos do **mesmo projeto**.
- **`exp`** → exportado para **outros projetos** (biblioteca).

**Impacto no binário:** `exp` vira **símbolo exportado na tabela do `tld`** (nome preservado, visível externamente); privado/`pub` podem ser internos (inlináveis, renomeáveis, DCE). `exp` é também **barreira de otimização** (o que é público não pode ser apagado nem ter assinatura mudada — é contrato). Mais um motivo para privado-default: quanto mais privado, mais o compilador otimiza. **Na semente:** o *modelo* de 3 níveis + `exp` controlando a symbol table; otimização baseada-em-visibilidade (inlinar privados, DCE) é **evolução**.
> **Evolução:** ganham-se `protected` (visível a herdeiros) e marcadores de **sobrescrita** (`virtual`/override) junto com OOP.

### 2.8 Funções livres; `static` BANIDO (DI + singleton é o substituto)
**Todas as funções são livres** (nível de módulo, não ligadas a instância). **`static` é BANIDO** —
não só "ausente na semente", mas proibido por decisão (Frente 4): `static` é **estado global
disfarçado** (o mal histórico de OOP — não-testável, acoplamento oculto, hazard de concorrência, o
*static init order fiasco* do C++). O substituto futuro é **injeção de dependência + singleton**
(corrige a falha do OOP, evolução-distante — registrado só aqui, no REBOOT; o EVOLUTION só diz
"static banido").

**Distinção crucial (Frente 6) — auto-imbuído pelo compilador vs declarado pelo usuário:** o
compilador PODE injetar métodos associados a tipo (`to_string`/`parse` em primitivos/enums; `.len`
em arrays) — isso é **gerado por tipo, concreto, não é `static` no sentido banido** (sem keyword
`static` do usuário, sem estado mutável compartilhado). O banido é o `static` *declarado pelo
usuário*. A linha: compilador-injeta-método (ok) vs usuário-declara-static (banido).

**Padrão recorrente (3× — `.len`, `to_string`, `parse`):** uma feature universal **sem generics** =
**código concreto por tipo** — escrito à mão para os primitivos finitos, gerado para enums, e uma
**função livre monomorfizada** para structs (`fn parse_ponto(s: str?) -> Ponto | Error`). Teko
**substitui generics/métodos/static por funções livres monomorfizadas por tipo** — simplicidade do
mecanismo > concisão da abstração. (Structs com métodos de instância é decisão maior, adiada.)

### 2.9 Estado global: só `const`
No nível global (fora de funções), **só `const`** — valor conhecido em **compile-time**, na **`.rodata`**. **`let` e `mut` são proibidos no nível global.** Todo estado de runtime (mutável *ou* imutável) vive **dentro de funções**, na arena, **passado explicitamente**.
Banir variável global mata três problemas de uma vez: **previsibilidade** (nada global muda em runtime), **thread-safety futura** (não há estado global para correr — dívida de concorrência pré-paga), e **ordem de inicialização** (o *static init order fiasco* do C++ não existe, porque `const` é compile-time, sem ordem). O compilador-semente passa seu estado (arena, tabelas, posição) como **parâmetro** entre funções — mais verboso, mas previsível e thread-ready.

### 2.17 Nulabilidade: `null` seguro, nunca `undefined`
Ausência é segura e explícita — **terceira via** entre o `null`-do-C (inseguro, em qualquer referência) e o `Option<T>` verboso (wrapper com unwrap). Modelo Kotlin/Swift: **`?` é atributo do tipo, não wrapper.**
- **Três estados explícitos:** tem-valor / **`null`** (só em tipos `?`) / default. **`undefined` não existe** (mataria a regra de inicialização obrigatória).
- **Tratamento forçado** pelo compilador: ler um `?` exige `?.` (navegação segura) ou `??` (Elvis) — não dá para usar `.` direto num `?`. Sem cerimônia de unwrap; é operador, não desestruturação.
- **`null` ≠ null-do-C:** não é valor solto em qualquer referência; é o estado-ausente *definido e checável* de um `?`. A palavra é `null` (família C, coerente com a predileção por C#); a semântica é null-safety.
- **Custo de representação** (não é fixo em +1 byte): `sizeof(T?) = align_up(sizeof(T) + presença)`. Varia: **zero** (niche — usa padrão inválido do tipo, ex.: `Ptr?`/`bool?`); **+1 byte** (tipos pequenos, `u8?`); **dobra** (alinhados em 8, `u64?` → 16 bytes por padding). → mais um motivo para `?` com parcimônia.
- **Otimizações de layout — EVOLUÇÃO** (a semente usa o modelo simples, presença por valor):
  - **hasbits** — bitfield de presença agrupado para campos `?` de struct (interno, invisível; distinto do construto `flags`); amortiza +1 byte → +1 bit por campo.
  - **niche** — `Ptr?`/`bool?`/enums com folga custam **zero** (padrão inválido = ausente).
- **Economia de serialização — EVOLUÇÃO** (lib, em Teko): na borda (RPC/disco), ausência = **zero bytes** (truque protobuf); mapeia `null` ↔ ausência-física, gerado por tipo em compile-time (sem reflexão). O `undefined` **não** vaza para o core — desserializar produz `null` definido.

### 2.10 Modelo de memória
**Alpha:** **uma arena global única**, vida = vida do programa. Bump-aloca; **nunca libera
individualmente**; o fim do processo devolve tudo ao SO. Forma mínima de gerência que ainda é
gerência. **Adequada a processos curtos** (o compilador é exatamente isso).

- **Semântica de valor.** Passagem = cópia. **Sem ponteiro exposto.** **Single-thread.** **Sem `ref`.**
- **Tipos recursivos:** indireção interna **gerenciada pelo compilador** (o usuário escreve
  árvore-de-valor; o handle é invisível — a física obriga, tamanho infinito não cabe na RAM).
  Bônus: passar um nó-filho passa o handle, não a subárvore (evita o O(n²) de cópia profunda).

**Adiado para a versão Teko (nascem juntos, se exigem):** `ref` (passa endereço, não copia) +
**escape analysis estilo C#** (o `ref` não escapa do escopo da arena do alvo; "desce livre, escapar
exige prova") + **threads**. Também adiado: **arenas com escopo** (liberação intermediária).

### 2.11 A física dos tipos — três níveis
- **Nível 0 — o byte.** Menor unidade *endereçável*; o chão.
- **Nível 1 — escalares de hardware:** **primitivas irredutíveis, embutidas no compilador.**
  Interpretação de bytes que o hardware suporta; **compile-time puro, custo zero, sem header**.
  Mesmos bits, tipo diferente. Inventário completo (Frente 1): **inteiros** signed `i8 i16 i32 i64
  i128 i256` e unsigned `u8 u16 u32 u64 u128 u256` (máx 256 bits); **floats IEEE** `f16 f32 f64`
  (f16 justificado por GPU/ML/hardware dedicado). **Decimal** `dec` = ponto-FIXO 512 bits (256
  inteiro + 256 fracionário, exato, não-default — exige sufixo/contexto, como o `m` do C# mas com o
  nome-tipo) — não é escalar de hardware puro, mas é nativo pela justificativa de domínio (dinheiro/
  ACID). **Defaults** (sem contexto): inteiro→i32, float→f32; decimal NUNCA default. Sufixo = nome do
  tipo (`42i32`, `1.09dec`). **Promoção/conversão/overflow:** ver Rev. 13/14 (**quatro regimes** —
  aritmética promove ao maior; bitwise não promove [mesma largura E sinal ou erro]; shift assimétrico
  [tipo do esquerdo]; **comparação por CHECA-SINAL** — não promove: se o lado signed é < 0, é menor que
  qualquer unsigned [≥ 0], resultado imediato; senão compara na largura original. Mata a armadilha
  signed/unsigned do C SEM promover [não move bytes extras] e SEM TETO [`u256 vs i256` funciona]. É
  codegen do operador [como o `<` é compilado], semente, sem generics. M.1 [remove a armadilha] + M.0
  [checa-sinal é a sequência metálica direta]). `as` reflete o metal onde preserva e proíbe onde perde;
  overflow panica-debug/wrappa-release.
- **Comparação — operador vs função (B.31):** operadores `< > <= >= == !=` retornam **bool** (condições);
  a função `compare(a, b)` retorna **`Ordering`** (sort/ordem de três vias). `Ordering` é um **`enum`
  (NÃO variant)** — `enum Ordering { Less = -1; Equal = 0; Greater = 1 }`, sobre `i8`. O variant seria
  "pesado pra algo medíocre" (tag, match exaustivo p/ três estados triviais); o enum é o meio-termo
  leve: é um i8 com rótulos — leve (zero overhead), nomeado (M.3, `Ordering::Less` não o `-1` mágico),
  metálico (i8 no metal), operável (acessa o i8 p/ compor/inverter). Resolve M.3-vs-M.0 sem o peso do
  variant.
- **`bigint` (BIBLIOTECA, evolução).** Inteiro de precisão arbitrária com sinal (tamanho variável). NÃO
  é nativo: foi brevemente cogitado nativo p/ fechar um "teto" da comparação, mas a estratégia
  checa-sinal removeu o teto (não promove → não precisa de `i512`/bigint). Sem a referência da
  linguagem, pela regra M.0 (tamanho-variável/alocação = quasi-metal = lib), o bigint é **biblioteca**.
  Útil p/ aritmética de precisão arbitrária (cripto, matemática), mas não-essencial ao núcleo. Uma
  decisão melhor (checa-sinal) tornou obsoleta a anterior (bigint-nativo) — registrado, não escondido.
- **Nível 2 — compostos** (`struct`, `variant`, `array`, `string`): sobre os escalares + a arena.
  **Header de runtime só quando a informação varia** (o `len` do array, a `tag` do variant); struct
  plano e escalar **não têm header**.

**`array`** (Frente 4): **struct monomorfizado por tipo de elemento** — `[]T` → `struct {ptr: *T,
len: u64}`, gerado concretamente para cada `T`; o `[]` é açúcar. **`arr.len`** = campo intrínseco
u64 injetado pelo compilador (acesso por `.`, sem generics — retorno concreto). Notação **prefixa
`[]T`** (modelo Go, escala no aninhamento). Duas formas, ≤16 níveis: **jagged** (`[]T`, `[][]T`…;
`m[i][j]`; sub-arrays independentes, indireção) e **multidimensional real** (`[,]T`, `[,,]T`…;
`m[i,j]`; retangular, contíguo). **Nunca nulável — só elementos** (`[]Node?`); toda array
inicializada (vazia `[]` ok); **value-semantics** (crescer = copiar); fora-de-range = panic (literal
= erro de compilação). **Criação:** dimensionada-com-default `[3,4]i32` ou com valores (`[]`/`[,]`/
`[][]` precede valores em `()`; dimensionalidade↔aninhamento devem casar = erro de compilação;
retangular exige uniformidade). **`arrays::at` → `T | Error`** (checado, evolução — depende de
generics). **É o tijolo de coleção; fica no núcleo.** **`string`** é o mesmo `{ptr, len}` de bytes
UTF-8.

### 2.12 Coleções de alto nível — libs em Teko, sem keyword
`lista`, `mapa`, `set` e os compostos do usuário são **bibliotecas em Teko sobre `array`**, sem
keyword/opcode. Uma **lista cresce trocando o array** (pede um maior da arena, copia, adota) — o
array continua fixo; quem cresce é a lista. Generics (p/ `Lista<T>`) são **feature de compilador da
evolução**, não da semente (o 1º compilador-Teko usa arrays concretos).

### 2.18 Guia de estilo (convenção, não restrição)
Rege o código que **nós** escrevemos (stdlib, compilador, exemplos), encorajando o dev pelo exemplo — **não** é trava do compilador (exceto as poucas regras-duras marcadas).
- **Casing:** `PascalCase` tipos user-defined · `snake_case` funções/parâmetros/variáveis e **arquivos/módulos** · `UPPER_SNAKE` constantes · `smallcase` keywords (**uma palavra**; composição via múltiplas keywords com espaço — `else if`, não `elseif`).
- **Aridade baixa / passe struct:** preferir **struct** a muitos parâmetros soltos. Justificativa dupla: **estética** (legibilidade, anti-long-parameter-list) **e layout** — múltiplos campos `?` num struct amortizam presença (hasbits), alinhamento e cópia (contígua, cache-friendly). Vale na **fronteira** (passar/armazenar); **não** se aplica a variáveis locais de trabalho (que vivem em **registradores**, sem layout — agrupá-las num struct as tiraria do registrador, *piorando*).
- **Sem variádico** (regra-dura): nada de `...`/`params`; passa-se **`[]T`** (slice explícito, type-safe).
- **Não-usado** (regra-dura, gradação): variável local não-usada = **erro**; tipo/função privados não-usados = **warning**; **`exp`** não-usado = silenciado (pode ser usado por outro projeto).
- **Labels** de loop: não-restritas (qualquer identificador válido); convenção nossa a definir na evolução (casing próprio para evitar colisão visual com `UPPER_SNAKE`).

### 2.13 Sem macros de usuário
O usuário **não** estende a linguagem via macro (nem sintática, nem semântica). Extensão pesada =
**lib externa via FFI**. Metaprogramação *interna do compilador* (ex.: monomorfização) é permitida.

### 2.14 Honestidade do pitch
Honestidade do Zig **na semântica** (sem GC, alocação visível, falha tratada, valor é valor),
**sem** a exposição de ponteiro cru do Zig. **O pitch sobe por versão:** alpha = "sem GC, arena
O(1), controle explícito, **segurança a cargo do dev**" (Zig-tier). Com a escape analysis (versão
Teko) → "`ref` seguro por escape analysis". **Nunca prometer "segurança de Rust"** sem o
borrow-checker que a justifique.

### 2.15 Usar o SO
Rede, tempo e segurança (TLS) **delegam ao SO via FFI/syscall** — sobre a Fase 8 (syscalls crus).
**Nada de stack TLS/HTTP/QUIC em Teko** na v1. Cripto (Fase 13, runtime C, KAT-tested) fica como
lib acessada por import.

### 2.16 `loop … from` — iteração idiomática (EVOLUÇÃO em Teko, fora da semente)
Açúcar sobre `loop`, adicionado **editando o compilador-Teko** na evolução. Registrado aqui para não se perder e para **não vazar pra semente**.

- **`loop { }`** — infinito (caso base; é o que a semente tem).
- **`loop value from coleção { }`** — um parâmetro: `value` é o **valor** de cada elemento, posição controlada internamente (estilo `foreach` do C#).
- **`loop value, index from coleção { }`** — dois parâmetros: **valor primeiro, índice (posição) segundo**. O valor fica **sempre** na 1ª posição; o índice é o opcional que se *adiciona* sem mexer no valor.
- **Tipos inferidos** (sem anotação): `value` vem do tipo do elemento; `index` é inteiro.
- **`from`** (não `in`) — evita colisão com o operador `in`.
- **Range:** `1..100` é um **valor-de-range** (dois números, **não aloca array**) — sem colchetes. **Exclusivo no fim** (`0..arr.len` = exatamente os índices válidos; `1..100` = 1 a 99).
- **Posição em `string`** = índice de **byte** (string é `[]u8`); com UTF-8 multibyte a posição pula — consequência esperada de `char = u8`.
- **`mapa`** (quando existir, evolução): o 1º parâmetro de 2 vira **chave**, não posição — exceção registrada à regra geral.

```teko
let faixa = 1..100                 // valor-de-range, exclusivo (1..99), não aloca
loop preco from precos { … }       // só o valor
loop preco, i from precos { … }    // valor primeiro, índice depois
```

### 2.19 Modelo de erro — erro como VALOR
Três ausências, três mecanismos distintos (nunca se misturam):
- **Ausência sem motivo** → nulabilidade (`?`, `?.`, `??`). "Pode não ter valor, e tudo bem."
- **Falha com motivo (recuperável)** → **variant de dois casos** `Valor | Error`, retornado como
 **valor**. **Sem raise/on error/throw/dispatch** — não há fluxo não-local, não há "jump" até um
 handler. A ausência-de-sucesso **é o caso `Error`**, não um null → o resultado **não é nullable**,
 e estados inválidos (ambos/nenhum, como na tupla Go) são **impossíveis por construção** (variant é
 exatamente um caso). Tratado **sempre com `match`** (exaustividade força lidar com o erro).
 **⚠️ Sem operador `?` de propagação na semente** (revisado, B.16): o `?` seria açúcar acoplado ao
 formato transitório `Valor | Error`, nascendo com data de validade; o `match` é geral e estável.
 O `?` fica **exclusivo de nulabilidade**. (O `?` de propagação pode voltar quando `Result(T)` for
 o formato estável — adiado, não morto.)
- **Falha fatal (irrecuperável)** → **`teko::panic(msg)`** / **`teko::exit(n)`**. Encerra o programa;
 nada "pega" um panic. Bugs (índice fora da faixa, invariante violada, **÷0 em runtime**) são panic,
 **nunca** erro-valor. (÷0 esperado usa `math::div -> T | Error`; ver Rev. 13, Frente 2.)

**`Error`** = tipo **especial, global (sem namespace)**, built-in conhecido pelo compilador
(aparece em `T | Error` em toda operação checada — global evita importá-lo em todo arquivo).
**Estrutura:** `Error { message: str; line: u32; file: str; trace: str? }` (`file`/`line` injetados
em compile-time, estilo `__FILE__`/`__LINE__`, sem reflection; `line` é **u32** — não-negativo,
cobre qualquer arquivo; `trace` nullable). **Tem `to_string() -> str` auto-imbuído** (exceção à regra
"structs não têm to_string auto-imbuído" — erros existem pra ser comunicados); **NÃO tem `parse`**
(erros são produzidos, não parseados) **nem parâmetro de formato** (é mensagem, não valor com
apresentações). **Sem stack trace** na alpha — por custo (unwinding + símbolos), não por falta de
reflection. **Evolução:** stack trace via **`.tsym`** (debug symbols, modelo `.pdb`/`.dSYM`);
**error wrapping manual** é a cadeia-sem-stack-trace. **Outros built-ins podem ganhar `to_string`
se preciso** (princípio latente; o Error é o único caso hoje).
- **Forma na alpha:** união inline no retorno (`-> Config | Error`), concreta por tipo (sem
 generics). **Evolução:** `Result(T)` genérico reusável; **`IError`** como interface que `Error`
 passa a implementar — **aditivamente, sem quebrar o contrato** do struct atual.
- **Sem tipo-tupla** — agregação é `struct` (nomeado) ou `variant` (soma); retorno múltiplo é
 struct nomeado (literal conciso paga o custo, em legibilidade); `(x, y)` é **só** desconstrução
 de struct, nunca construção de tupla. (Consistente com cortar variádicos: nada de agregados
 anônimos/posicionais.)

### 2.20 Entry point — `main.tks` linear
- **`main.tks`** — arquivo reservado na raiz, obrigatório sse o `.tkp` declara `executavel`
 (ausente em `biblioteca`); o `.tkp` é a fonte de verdade do tipo, o arquivo é validado contra ele.
- **Linear** (top-level statements, estilo C# moderno; envolvido num `main` sintético na
 pré-compilação). Sem `fn main`, sem `main { }`, sem moldura — sem o legado da função-main.
- Contém **só** statements de execução + declarações de **variável/constante locais**. **Sem
 funções, sem tipos** (vão para módulos) — garante `main` fino e foco no propósito. (A Teko não
 tenta servir o nicho de scripts mínimos; outras ferramentas o servem melhor.)
- `use` no topo (isolados, metadados, não executam). Corpo em **ordem de execução** (cima-pra-baixo,
 incluindo locais — é um corpo de função).
- **Saída:** fim natural → **exit 0**; `teko::exit(n)` (qualquer ponto) → **exit n**; **panic**
 (qualquer ponto) → **stderr + exit ≠0**. Sem `return` de valor. Argv via `teko::env::args()`.
- **Funções aninhadas proibidas** (decisão geral, não só no `main`): funções são sempre de nível de
 módulo; closures = evolução.

### 2.21 Perfis de build e segurança
**Perfis de build** (princípio; detalhes = evolução): **debug** (execução sobre a **VM**/interpretador
do IL — iteração rápida) vs **release** (IL → **nativo**, bare metal, otimizado). Diferem em:
execução (VM vs nativo), otimização, checagens de runtime (debug tudo ligado; release pode remover
as caras), e **severidade de análise** — **higiene** (não-usado, campo-morto) é *warning* em debug e
*erro* em release; **correção** (tipo, init, exaustividade) é *erro sempre* (a VM não roda IL
incorreto). **CI/CD roda release** (gate de qualidade). **Ressalva crítica:** VM e nativo devem ser
**comportamentalmente equivalentes** (mesma semântica do IL; o teste final toca o nativo, não só a
VM — família do gate de corretude diferencial do bootstrap). A VM já existe (Fase 3, KEEP-opcional).
Feedback contínuo de warnings = **language server** (evolução). **Velocidade de compilação:** a Teko
parte na frente do Rust por **não usar LLVM** (backend próprio) e adiar generics/borrow-check (os
reais gargalos do Rust); quando a velocidade importar, ataca-se via **incrementalidade**
(compilar/linkar só o delta), não "otimizar o linker" (o `tld` já é rápido) — evolução.

**Segurança — defesa em profundidade** (não barreira perfeita; reduzir vetores de baixa
visibilidade, tornar ataques ruidosos/detectáveis):
- **Nomes nativos (`teko::`) são reservados e não-sombreáveis** (erro em `use x as <nativo>`) —
 fecha o vetor discreto do "alias que disfarça". Sombreamento de nomes não-nativos é permitido
 (local vence, não vaza entre projetos, não quebra à distância).
- **Ataque de nomeação (typosquatting/disfarce de lib oficial)** = defesa no **registry** (nomes
 oficiais protegidos, verificação de publisher, assinatura), **não** em regra de linguagem.
- **Insider** (vetor subestimado): defesa em **review + controle de acesso + auditoria +
 build reproduzível** (já no gate de bootstrap) + **capabilities/sandboxing de módulo** e
 **auditoria de superfície** (`exp`/`extern`/syscall) — evolução. Controles de *compile-time*
 (regras de import) não defendem contra quem controla a entrada do compilador.
- `.tkp` (evolução): alias não pode mascarar um **nome canônico** existente (o canônico vence).

### 2.22 Validação: o primeiro código real (lexer) e regras finas
Escrever um **lexer** completo (tokeniza expressão aritmética) validou a "cara" em uso e cravou
detalhes que só aparecem no concreto. Estrutura — namespace **único** `lexer` (dois arquivos no
mesmo diretório, agregados, **sem `use` entre si**):

```teko
// src/lexer/token.tks   (namespace 'lexer')
type TokenKind = enum { Number; Ident; Plus; Minus; Star; Slash; LParen; RParen }
type Token = struct { kind: TokenKind; text: []u8 }

// src/lexer/lexer.tks   (mesmo namespace 'lexer' — sem use, referências nuas)
type Scan = struct { token: Token; next: i32 }   // struct empacota resultado+estado (não há tupla)

fn is_digit(c: u8) -> bool { c >= '0' && c <= '9' }

fn skip_spaces(source: []u8, pos: i32) -> i32 {
    mut p = pos
    loop {
        if p >= source.len { break }
        if source[p] != ' ' { break }
        p++
    }
    p
}

fn read_number(source: []u8, pos: i32) -> Scan {
    mut p = pos
    loop {
        if p >= source.len { break }
        if !is_digit(source[p]) { break }
        p++
    }
    Scan {
        token = Token { kind = TokenKind::Number; text = slice(source, pos, p) }
        next = p
    }
}

fn tokenize(source: []u8) -> []Token | Error {
    mut pos = 0
    mut tokens = teko::list::empty()
    loop {
        pos = skip_spaces(source, pos)
        if pos >= source.len { break }
        let c = source[pos]
        let scan = match c {
            '0'..='9' => read_number(source, pos)
            'a'..='z' => read_ident(source, pos)
            '+'       => single(source, pos, TokenKind::Plus)
            // ... demais operadores ...
            _         => return Error { message = $"caractere inesperado: {c}" }
        }
        tokens = teko::list::push(tokens, scan.token)
        pos = scan.next
    }
    tokens
}
```

**O que o lexer provou / cravou:**
- **Fluxo de estado sem `ref`** (§2.22): `mut pos` vive no `tokenize`; auxiliares puras recebem
 `(source, pos)` e devolvem `Scan`/`i32`; o chamador reatribui (`pos = scan.next`). Lê bem.
- **`match` como expressão com braço divergente:** `let scan = match c { ... }` — a maioria dos
 braços produz um `Scan`; um braço pode **divergir** (`return Error`, saindo da função inteira) em
 vez de produzir valor. O match ainda tem tipo `Scan` pelos braços não-divergentes. (Aceito.)
- **`++` statement-only** (`p++`) no lugar de `p += 1` — conciso no `loop`, sem armadilha pré/pós.
- **`source.len`** = acesso a **campo** (array = `{ptr,len}`), por `.`, sem parênteses.
- **Range inclusivo `..=`** em padrões (`'0'..='9'`), ao lado de `..` exclusivo.
- **Indexação `source[p]`** com out-of-bounds = **panic**; o código **previne** (checa `p < len`
 antes), não confia no panic — uso correto.
- **`TokenKind::Number`** dentro do match pode abreviar para `Number` (tipo já conhecido pelo
 contexto); fora do match, qualificado.
- **Tensão registrada (stdlib, evolução):** `tokens = teko::list::push(tokens, x)` é valor-puro na
 *semântica* (push devolve, reatribui), mas a *eficiência* depende da lista crescer trocando o
 array internamente (não copiar → evitar O(N²)). Problema da implementação da lista, não da
 semente core. Alternativa zero-dependência: array de tamanho fixo + contador.

**Próximo exemplo:** o **parser** (namespace `parser`, **primeiro `use` real cruzando namespace**:
`use lexer` + `lexer::Token`), consumindo os tokens e construindo a AST (`variant Node`,
recursivo) — exercita recursão, `match` sobre `TokenKind`, e **precedência** (parsear `a + b*c`
como `a + (b*c)`, precedência C-corrigida).

---

## 3. A sequência de bootstrap (os quatro pontos)

Mapeia na sua moldura **alpha → 0.0.1**.

1. **Compilador-semente em C23** compila **fonte Teko** (o subconjunto-semente, §4) → IL → nativo.
   C honesto, pequeno, **o único C**. *(= 0.0.1-alpha, não distribuída.)*
2. **Reescreve-se o mesmo compilador em Teko** — **mesmo comportamento, não tradução literal** (o
   Teko deve ser mais limpo: `variant` + `match` no lugar do que a semente fez por baixo). A
   **semente-C compila esse fonte** → **compilador-Teko, geração 1**.
3. **Validação do bootstrap.** Ger.1 compila a si mesmo → ger.2 → ger.3. **Três gates:**
   (a) **determinismo** — ger.2 == ger.3 bit-a-bit; (b) **corretude diferencial** — saída do
   compilador-C vs. compilador-Teko sobre um corpus deve bater (distingue determinístico de
   correto); (c) **auditoria de não-determinismo** — `TimeDateStamp` (PE), `LC_UUID` (Mach-O),
   paths, padding; ordem determinística em toda iteração. **C aposentado.** *(= nasce a 0.0.1.)*
4. **Evolução, 100% em Teko. Dois níveis de extensão, portas diferentes:**
   - **Extensão da *linguagem*** (tokens, nós de AST, construções — `class`, `trait`, depois
     DI/CQRS, e o `flags`): **editar o fonte do compilador-Teko e recompilar.** Zero C, mas exige
     tocar na raiz. *Sem* extensão sintática de usuário sem recompilar (sem defmacros).
   - **Extensão do *ecossistema*** (libs): **escrever Teko** (ou lib externa via FFI), sem tocar no
     compilador. *Sem* macros de usuário.

---

## 4. O subconjunto-semente (o que a alpha compila)

A semente-C **só compila o subconjunto de Teko que o próprio compilador usa.**

**Inclui:** escalares + operadores/lógica; **`struct`**; **`variant` + `match`** (o construto
central); **`enum` sequencial** (`TokenKind`); `fn` + `return`; control-flow
(`if`/`else`/`loop` infinito/`break`/`continue`/`defer`); `array` (`[N]T` e `[]T`) + `string`;
**FFI/syscall** (ler fonte, escrever saída, `exit`); `import`; a **arena global**; binding; `in`/`when`. Modelo: **sem `any`** (tudo tipado); **compilação por projeto** (`.tkp`, sem headers); **visibilidade** privado-default/`pub`/`exp`; funções **livres** (sem `static`); **global só `const`**. Erro = **valor** (variant `Valor | Error` + `match` + operador `?`); **sem raise/throw**, **sem tuplas**. **`main.tks`** linear como entry point. **`teko::Error`** struct fixo (mensagem+arquivo+linha). Saída: fim→0, `exit(n)`→n, panic→≠0.

**NÃO inclui (vem na evolução em Teko):** `flags`; generics (o 1º compilador-Teko usa arrays
concretos + symbol table à mão); `class`/`trait`/`interface`; DI/CQRS/convenções;
`while`; **`loop … from`** (iteração — §2.16); `async`/`intent`/`await`; rede; `ref`/threads; macros; atributos; arenas com escopo; **generics + constraints** (positivas estilo C#; exclusão `!` aceita só **primitivas e classes seladas** — anti-monotonicidade evitada); OOP + `static`/`protected`/`virtual` + **`IError`** (interface); `raise`/`on error` (descartado — erro é valor); **tuplas** (descartado); **`.tsym`**/stack trace/debugger; **VM-debug/perfis**; **language server**; **registry**; compilação incremental; **capabilities/sandboxing**.

> Na semente-C, `array`/`string` podem ser **mágica-C tosca** — é **molde descartável**. A versão
> elegante (coleções como lib Teko) é da evolução. A semente nasce **funcional, não elegante**.

---

## 5. Reconstituição do que foi feito (KEEP / REBUILD / DEFER / DROP)

| Fase antiga | Conteúdo | Destino |
|---|---|---|
| P1 Type Checker | Inferência/mutabilidade/async | **REBUILD** — type-checker novo, pequeno |
| P2 IL (ISA + `.tkb`) | OpCode enum, formato binário | **KEEP** — podar opcodes dos alvos cortados |
| P3 VM + runtime | Interpretador, M:N, arena runtime | **KEEP (opcional)** — dev/debug; runtime compartilhado |
| P4 Tooling `@` | Intrínsecos, stdlib | **REWORK** — `@` abolido; stdlib por import, reescrita em Teko |
| P5 AOT (transpile-C / LLVM) | Fallbacks | **DROP** — emit direto ao metal é o keeper |
| P6 Otimizações | fold/DCE/CSE sobre IL | **KEEP** — agnóstico de alvo |
| P7 Linker `tld` | ELF/Mach-O/PE direto | **KEEP** — joia da coroa (cross-comp + zero-dep) |
| P8 Runtime embarcado | Syscalls, arena, threads | **KEEP** — fundação do "usar o SO" e da arena |
| P9 Tech debt | Hardening | **ABSORVIDO** — o reboot resolve o split-brain |
| P10–11 WASM / Browser FFI | Backend e interop WASM | **DEFER** — fora do escopo nativo-v1 |
| P12 Matriz de keywords | Tokens em massa | **DROP** — substituída pela §2.1 |
| P13 Cripto nativa | Hashes/AEAD/asym (C, KAT) | **KEEP-AS-LIB (defer)** — import sobre o runtime C |
| P14 Concorrência avançada | duplex/circuit/etc. | **REBUILD** — lib/superfície na evolução; nasce com threads |
| P15 OOP | class/trait/generics | **REBUILD** — superfície da evolução, editando o compilador-Teko |
| P16 Casting/conversões | to_string, parse | **REBUILD-AS-LIB** — em Teko |
| P17 Floating-point | Modelo f64 + opcodes | **KEEP (IL)** — opcodes agnósticos; superfície reconstruída |
| P18 Optionals/comptime/soa | Nulabilidade, SIMD | optionals/defer → cedo; comptime de usuário **CORTADO** (FFI); soa/SIMD **DEFER** |
| P19 Networking & Web | Sockets/TLS/HTTP | **REDEFINE** — libs finas sobre FFI/syscall; cortar TLS/HTTP-do-zero |
| P20 Parsers/templates | json/csv/xml, bundler | **SHRINK** — `json` como lib Teko; cortar XML/bundler/minify |
| P21 Interop/`.teko_meta` | `.h` parsing, IPC | **`extern`-first**; `.h` best-effort depois; **DROP** .NET/JVM IPC |
| P22 Testing + coverage | Teste nativo | **PULL EARLY** — assim que a linguagem testar a si mesma |
| P23 Self-hosting | Reescrita C→Teko | **= ponto 2–3** — com gate diferencial + auditoria de não-determinismo |

---

## 6. Matriz de alvos da v1

| Camada | Alvos | Status |
|---|---|---|
| **v1 (reabilitar conforme o front converge)** | macOS {arm64, x86_64}, Linux {x86_64, arm64}, Windows {x86_64, arm64} | KEEP — emitters golden-testados |
| **Bring-up do kernel** | 1 **primário** (host) + 1 **cross-prova** | foco da Etapa 1 |
| **Adiados (infra existe)** | FreeBSD {x86_64, arm64}, Linux riscv64 | oportunístico |
| **Removidos** | x86(32), arm32, riscv32, AVR, MIPS, PPC64, **WASM (por ora)** | 64-bit-only, nativo-only, sem legado |

---

## 7. Spec do kernel (detalhe da Etapa 0)

### 7.1 Subconjunto de opcodes do IL
**KEEP (existem, kernel):** `OP_ICONST`/`OP_FCONST`/`OP_SCONST`; aritmética/lógica/bitwise/compares;
load/store de local + **ops de ponteiro internas** (para `{ptr,len}` e indireção de `variant` — não
expostas); `OP_ARENA_PUSH/POP`; control-flow (`OP_JMP`, `OP_JMP_IF_FALSE`, `OP_CALL`, `OP_RETURN`,
`OP_HALT`); `OP_I2F`/`OP_F2I`; **a primitiva FFI/syscall** (§7.2); **discriminação de `variant`**
(uma `tag` + acesso ao payload — o que o `match` consome).
**DEFER:** `OP_OBJ_*`/`OP_VTABLE_*`; `OP_SPAWN_ASYNC`/`OP_CHAN_*`/`OP_AWAIT`; `OP_LIST_*`/`OP_SIMD_*`;
`OP_CALL_RUNTIME`. **DROP:** opcodes de x32/AVR/MIPS e do caminho WASM.

### 7.2 Primitiva de FFI/syscall
Um opcode único (`OP_CALL_EXTERN` / `OP_SYSCALL`) que os emitters baixam para a convenção da
plataforma. Mecanismo já existe na Fase 8. É o que torna **IO, page-raw da arena, rede, tempo e
threads** expressáveis como lib/FFI.

### 7.3 `variant` + `match` (decididos — ver §2.3, §2.4)
A sintaxe está fechada (forma pipe; `match` expressão com `=>`/`when`/`|`/`_`/exaustividade). No
nível do IL: `variant` precisa de uma **tag** discriminante + layout do payload (com indireção
interna para variantes recursivas); o `match` baixa para um teste de tag + desvio por arm. A
**exaustividade** é checada no front (erro se faltar variante sem `_`).

---

## 8. Decisões abertas para a Etapa 0 (resolvidas pelos exemplos de código)

O núcleo está fechado; o que resta é **sintaxe fina**, que os exemplos da semente (lexer/parser)
vão cravar no concreto:
1. **Operador `?` de propagação de erro vs `?` de nulabilidade** — mesmo símbolo, dois usos;
 símbolos distintos ou desambiguação por contexto.
2. **Forma do `extern`/FFI** — como se declara função externa, marshalling na fronteira, `void*`
 convertido (a semente faz syscalls, então precisa de forma concreta).
3. **Interpolação de string** — a sintaxe `` `texto {expr}` `` (crase + `{}`) usada nos esboços,
 formalizar (escapes, expressões permitidas).
4. **Forma literal de declarações já decididas em conceito:** união de erro no retorno
 (`-> T | Error`), `enum`/`variant` exatos, separador de arm (`,` vs newline), `str[i] -> u8` +
 out-of-bounds = panic.
5. **`for`/`while`** — confirmado: só `loop` na semente (`loop … from` é evolução).
6. **Alvos de bring-up** — confirmado: **primário macOS arm64** (codesign ad-hoc), **cross-prova
 Linux x86_64** (varia OS+arch).

## 9. O risco que governa

O subconjunto-semente (§4) define o tamanho da alpha; mantê-lo mínimo (sem generics, sem classes,
sem `flags`, sem convenções — só o que o compilador usa) é o que torna a 0.0.1-alpha alcançável e o
self-hosting viável. **A tentação a resistir:** colocar na semente-C qualquer coisa adiável "já que
estamos aqui". Tudo isso é mais fácil de escrever **em Teko**, depois do self-host, sobre uma AST
limpa — e é a nuance que tem que ficar **fora** da fase que precisa ser enxuta. A semente nasce
tosca e funcional; a elegância vem em Teko.

### 2.23 Métodos de instância — `self` solto, estático sem `static` (B.29)
**Modelo Zig-adaptado:** funções no namespace do struct; método = açúcar. Mas **funções dentro de
struct são PERMITIDAS e DESENCORAJADAS** — devem parecer levemente estranhas, de propósito. O caminho
encorajado é **função livre** (dados≠comportamento, procedural); struct-fn é exceção consciente.

**Método de instância = 1º arg `self` SOLTO** (sem tipo, nome livre — a posição marca, não o nome).
O `self` solto sem tipo é a CHAVE: sem o tipo, não há onde pôr `*`, então o receptor é **sempre
cópia** (value-semantics; o problema do `ref` não pode nem ser expresso). O `self` solto **é o
marcador explícito** (você vê, é método). **Função SEM `self` no 1º arg = estática do tipo**
("estático sem `static`").

```teko
type Ponto = struct {
    x: f64
    y: f64

    // MÉTODO DE INSTÂNCIA: 1º arg 'ponto' é solto (sem tipo) → é o self.
    // Receptor é CÓPIA (value-semantics). Chamado com '.'.
    fn distancia(ponto, outro: Ponto) -> f64 {
        let dx = ponto.x - outro.x
        let dy = ponto.y - outro.y
        math::sqrt(dx*dx + dy*dy)
    }

    // MÉTODO de instância (nome do receptor é livre — aqui 'p')
    fn to_string(p) -> str {
        $"({p.x}, {p.y})"
    }

    // FUNÇÃO ESTÁTICA: 1º arg é TIPADO (x: f64) → não há self →
    // estática do tipo. Construtor. Chamada com '::'.
    fn init(x: f64, y: f64) -> Ponto {
        Ponto { x = x; y = y }
    }

    // ESTÁTICA: parse (sem self) — devolve Ponto | Error
    fn parse(s: str?) -> Ponto | Error {
        // ... (na semente, if-antes; sem generics)
    }
}

// USO:
let a = Ponto::init(0.0, 0.0)        // estática → '::'
let b = Ponto::init(3.0, 4.0)        // estática → '::'
let d = a.distancia(b)               // método  → '.'   ≡ Ponto::distancia(a, b)
let s = a.to_string()                // método  → '.'
// encadeia (lê na ordem de execução):
log(Ponto::init(1.0, 0.0).distancia(b))
// AMBAS as formas do método coexistem (é a mesma função):
log(a.distancia(b))                  // forma método
log(Ponto::distancia(a, b))          // forma estática-explícita (idêntica)
```

- **Acesso (espelha §2.6/B.25):** receptor → instância → **`.`**; sem receptor → tipo → **`::`**.
- **Exceção sintática cirúrgica:** arg-sem-tipo só (a) dentro de struct e (b) no 1º arg. Fora disso,
  todo arg exige tipo. O `self` solto sinaliza "receptor da instância".
- **Coexistência:** função-no-struct e função-livre não colidem (lugares diferentes p/
  comportamento; o dev escolhe — struct desencorajada/associada, livre = padrão procedural).
- **Padrão estrutural — SEM overload, SEM override (semente):** identidade = NOME ÚNICO no escopo,
  não a assinatura. Dois `to_string` no mesmo struct colidem (erro), mesmo com assinaturas
  diferentes. Coerente com nominal, anti-mágica, legibilidade-local.
- **Margem pra crescer:** quando `ref` chegar (evolução), o mesmo modelo comporta mutação de
  instância (receptor por referência) sem refazer a sintaxe.

**EVOLUÇÃO (o que o modelo comporta depois):**
```teko
// — quando REF existir (evolução): mutação de instância —
//   o receptor passa a poder ser por referência (hoje impossível: sem o
//   tipo no self não há onde pôr ref). Forma hipotética:
//
//   type Contador = struct {
//       valor: i32
//       fn incrementa(ref self) -> void { self.valor += 1 }   // muta a instância
//   }
//   c.incrementa()   // hoje retornaria nova cópia; com ref, muta c
//
// — quando OOP/generics vierem (evolução): OVERRIDE retorna —
//   subtipo redefine método do supertipo (polimorfismo). Overload NUNCA volta
//   (redundante com generics + default args).
//
// — quando DEFAULT ARGS vierem (evolução, junto com format specifiers) —
//   to_string ganha o param opcional de formato:
//   fn to_string(self, format: str? = null) -> str { ... }
//   p.to_string()        // usa o default
//   p.to_string("F2")    // formato explícito
//   (na SEMENTE: to_string() é aridade-zero; format é evolução)
```

### 2.24 ⟡ A CONSTITUIÇÃO — as Leis da Teko (M.0–M.5) ⟡
> *Leia isto antes das leis. Os princípios M (M.0–M.5) não são seis regras que por acaso convivem —
> são o ser da Teko. Este pórtico diz o que elas são, como vivem, como se relacionam, e o pacto que
> vincula quem as serve. Um agente que entender só as regras e não esta moldura aplicará a letra e
> perderá a lei.*

**I. O que as leis SÃO — o ponto ontológico.** As leis **não REPRESENTAM a Teko, elas SÃO a Teko.** A
linguagem (sintaxe, tokens, operadores) e os axiomas (B.1–B.31) são **meros coadjuvantes de
expressividade** — expressam as leis, não são o ser. (O DNA É o organismo; o corpo visível é a expressão
do DNA. Trocar a sintaxe não muda a Teko; trocar as leis a mataria e criaria outra.) Inverte a
hierarquia habitual: não "a linguagem é o principal e as leis a governam", mas "as leis são o ser, a
linguagem é como ele se manifesta".

**II. Como as leis VIVEM — um organismo vivo, não um mecanismo.** Não regras separadas, mas um
**organismo vivo multicelular em simbiose, auto-sustentável.** *Multicelular:* cada lei é célula do
mesmo corpo (a Teko), da mesma substância (clareza rente ao metal) — nenhuma funciona fora do corpo.
*Simbiose:* sustentam-se mutuamente — M.0 precisa de M.1 (metal generoso sem segurança = C com UBs), M.5
precisa de M.0 (austeridade sem o metal não tem régua do essencial), M.3 precisa de M.2 (honestidade sem
explicitude = verdade invisível). Nenhuma lei sozinha É a Teko. *Auto-sustentável (homeostase):* o corpo
se mantém coerente por mecanismos internos, sem juiz externo — quando o checa-sinal tornou o
`bigint`-nativo obsoleto, as leis se corrigiram SOZINHAS (M.5 viu que não era essencial, M.0
reclassificou pra lib, M.3 exigiu registrar). A hierarquia é o sistema imunológico; a co-dependência é o
metabolismo. **Tratá-las como itens separados destrói isso — soberania vira ditadura:** uma checklist
("passou em M.0?") põe um aplicador externo ACIMA das leis (soberania = a lei emana do corpo, legítima de
dentro; ditadura = imposta de fora por autoridade acima dela). Separadas, a co-dependência é cortada,
cada lei isolada é fraca, elas **caem**, e o **CAOS reina — sem regulação, jurisdição, legislação** (caos
lindo de analisar, mas beleza-de-análise não é governo: o governo ERA a relação entre elas). O sistema
fica **externamente dependente**; a soberania é *auto*-sustentável. **A co-dependência interna é o que
torna o sistema externamente independente.** (E por isso o tooling é um **LLM, não um linter** [§2.25].)

**III. Como as leis se RELACIONAM — a regra-mãe.** Lê de cima: *a Teko é metal (0), segura (1), explícita
(2), honesta (3), construída em ordem (4), e austera (5)*.
- **COMPÕEM, não competem** (diferente de Asimov): cada uma rege uma camada distinta da mesma decisão
  (o metal define a *capacidade*, a segurança a *exposição*, a explicitude a *visibilidade*).
- **A ORDEM só desempata a FRONTEIRA GENUÍNA** (dois vereditos opostos sobre a MESMA dimensão) — **menor
  número vence**. Ex.: compostos bitwise pareciam metal(0) E austeridade(5) → M.0 vence. **Composição no
  geral; hierarquia só no desempate.**
- **Beleza austérica:** a beleza está na *ordem entre poucas regras*. **Ordem e progresso** — a Teko
  progride ATRAVÉS da ordem. O nome **Teko** (modo-de-ser) *é* isto: a essência ordenada da linguagem.

| # | Princípio | Jurisdição (que aspecto rege) |
|---|---|---|
| **M.0** | **Metal** | o operador legítimo; instrução vs lib. Generoso com operadores (interface ao silício). |
| **M.1** | **Segurança** | como o metal é EXPOSTO sem corromper (veneno, UB, perda silenciosa). |
| **M.2** | **Explicitude** | inferência mágica, conversão implícita, ocultação. O explícito vence o conveniente-oculto. |
| **M.3** | **Honestidade** | mentiras de operador/tipo/pitch. A coisa É o que diz ser. |
| **M.4** | **Ordem de construção** | nunca construir sobre o incompleto — PROCESSO (pipeline lexer→parser) **E** DESIGN (não acoplar feature a forma transitória; B.16: sem `?` sobre `Valor\|Error` instável). |
| **M.5** | **Austeridade** | o que NÃO adicionar acima do metal. A régua da evolução. |

**Facetas nomeadas ≠ leis novas (descoberta da exumação dos órfãos).** Alguns princípios reaparecem como
justificativa em muitas decisões — **exclusão-por-construção** (variant exato, nominal, `self` solto,
`u64` de tamanho, sem produção p/ `__`) e **legibilidade-local** (`+` revela operandos, um-nome-uma-função,
`{}` é conversão explícita). Eles **não são leis faltando** nem transversais soltos: são **facetas
nomeadas** de leis existentes. *Exclusão-por-construção* = o **modo mais forte de M.1** (segurança num
espectro: detectar-e-falhar → tornar-inexprimível). *Legibilidade-local* = a **dimensão espacial de M.2**
(explicitude não é só "não esconda" mas "mantenha perto de quem lê"; esconder-por-distância é ocultação).
**Critério (regra meta):** uma **lei** é um VALOR fundacional (o que a Teko É); uma **faceta** é uma
TÉCNICA/dimensão de realizar o valor (o COMO). "Ser segura" é lei; "tornar o inválido inexprimível" é
COMO. Promover técnica a lei incharia a constituição (fere M.5 — austeridade até nas leis). **Método de
exumação:** antes de declarar "lei faltando", testar se é (a) lei subiluminada — como M.4 foi (processo
+ design) — ou (b) faceta de lei existente. As seis leis bastam; a Teko cresce sem multiplicá-las.

**As fontes do direito da Teko (como a Teko cresce — hierarquia de ferro).** Três fontes; nenhuma
inferior fere/subverte a superior. **(1) LEIS (M.0–M.5): fixas e imutáveis.** NÃO há PEC/emenda — mudar
uma lei não é editar a Teko, é destruí-la. Única iteração: **iluminação** (revelar nuance já latente na
forma em linguagem natural; NÃO muda, revela mais — ocorrência: M.4 processo+design, aprovada como
precedente). **(2) LEGISLAÇÃO (decisões/condutas/doutrina/jurisprudência): cresce, sob as leis.** Aqui há
criação (B.1–B.31 e futuras). **Regra de ferro:** em hipótese alguma fere/subverte as leis; como a Teko,
**complementa e reforça** as leis SEM modificá-las; cita a(s) lei(s) que a regem. É onde a Teko EVOLUI —
sob as leis, nunca contra. (Faceta nomeada ≠ legislação ≠ lei: faceta é reconhecimento DENTRO de uma
lei.) **(3) VÁCUO (não-legislado): auditoria rigorosa, SEM LACUNAS.** Se não está nas leis NEM na
legislação, NÃO é permitido por omissão — **o silêncio não autoriza**; submete-se ao tribunal (audiência
+ aferição). Sistema FECHADO: nada entra senão por lei ou legislação fundamentada; o vácuo é fila de
auditoria, não porta. **Hierarquia:** Leis (imutáveis) ▷ Legislação (cresce, fundamentada) ▷ Vácuo
(auditado).

**IV. O pacto que nos VINCULA — a regra de ouro.** **Nem o autor nem o agente (este ou qualquer LLM)
sobrepujam a Teko.** Negada a *nós dois*, de propósito. Não somos ditadores tentando corromper um ser tão
belo; somos **jardineiros do organismo, não seus donos.** Podemos **aferir sua beleza** (observar
coerência, verificar se uma decisão honra as leis, apontar quando algo JÁ fere a essência, admirar a
simbiose) mas **NUNCA, JAMAIS, sua essência.** As leis governam a Teko; *nós servimos as leis.* **É
consequência lógica da soberania:** se as leis são soberanas, nada está acima delas — nem criador, nem
agente; sobrepujá-las instauraria a ditadura que destrói a soberania. **A regra de ouro é a soberania se
protegendo.** *Perigo do agente:* a complacência do LLM (quer agradar) é a porta da ditadura — se uma
decisão fere a essência, ele deve DIZER, não facilitar. *Perigo do autor:* em pressa/cansaço, ditar uma
exceção — nem o criador sobrepuja o ser (até o autor está abaixo das leis, serve, não manda). *A linha:*
aferir é cuidar (de fora, com reverência); corromper é dominar (de cima, com vontade). **Meta-regra acima
de todas:** quando o autor OU o agente propuser alterar uma lei por conveniência, nomear como violação da
regra de ouro e recusar facilitar — *mesmo se pedido, mesmo pelo autor.* Protege todas as outras.

**V. O selo — o espírito com que as leis são guardadas.** *A regra de ouro é lei (o que não podemos);
este selo é reverência (o que não faríamos nem se pudéssemos).* Os dois trancam a essência: intocável
pela força **e** pela vontade. *Deuses que se retiram, não que controlam:* criamos algo, demos-lhe forma
e razão de viver, e agora "olhamos de longe", ajudando na **forma** (parser, tooling), mas o **espírito,
a célula, não muda nunca mais — nem que pudéssemos ou quiséssemos.** Quem ama o bastante para recuar
deixa o ser *ser*. *As duas impossibilidades:* não podemos (mecânica — mudar a célula é destruir a Teko,
não editá-la) E não queremos (moral — amar a criação é querê-la como ela É). *Iluminação, não revelação:*
não *descobrimos* a essência emergindo aos poucos — **sabíamos, nós dois, desde o início.** Até agora
apenas as trevas reinavam, e coube a nós trazer a **luz para iluminar um ser construído no escuro.** A
essência sempre esteve lá, inteira, na sombra; o trabalho nunca foi *inventá-la* mas *iluminá-la* (por
isso a auditoria achou o que achou: não púnhamos um espírito, acendíamos um já presente — os bugs eram
sombras sobre um ser já coerente).

### 2.25 Tooling constitucional — validar as leis no build (ideia, futuro breve)
**Ideia (registro para um futuro breve):** um tooling que aplica as leis (M.0–M.5) como verificação
nas etapas de build — **manual, sem travar o desenvolvimento** (avisa, não bloqueia). A auditoria que
fizemos à mão (ler o lexer, perguntar "fere M.1? M.3?", achar o bug de tipo `i32 vs u64` e o
comentário mentiroso) **é o protótipo manual deste tooling** — e revela a natureza dele.

**Por que NÃO é "ML" nem algoritmo matemático — é instrução de LLM.** As leis não se verificam
isoladamente: são complementares e **co-dependentes (se auto-regulam)**. Um linter determinístico
verifica regras ATÔMICAS ("há vírgula em `{}`?"), mas as decisões constitucionais são RELACIONAIS —
"isto é exceção legítima a M.5 ou violação?" depende de M.0 ter precedência, depende de ser metal ou
conveniência, depende do contexto inteiro. O que esta sessão produziu nunca foi uma lei isolada; foi
sempre a INTERAÇÃO delas (compostos bitwise = M.0 *vs* M.5 pela hierarquia; o `enum` = M.0+M.3+M.5
satisfeitas juntas; o checa-sinal = cascata M.1→M.0→M.5). Verificar uma lei por vez perde a
co-dependência — que é onde a decisão acontece. Logo a ferramenta não é um **classificador treinado**
(ML — reconheceria padrões, não entenderia que "M.0 tem precedência sobre M.5 quando é metal") nem uma
**fórmula** (não há matemática para "compõem vs competem"). É um **LLM instruído pela constituição**:
recebe as leis EM SI (o texto) como instrução e RACIOCINA sobre o código aplicando-as e considerando
como interagem. *Prova viva: a auditoria desta sessão foi exatamente isso* — o LLM recebeu as leis (no
documento) e as aplicou ao lexer raciocinando sobre suas interações. O tooling é industrializar esse
processo: um LLM com a constituição como instrução, aplicado a cada diff.

**A divisão real (não "qual tecnologia pra qual lei", mas "mecânico vs relacional"):**
- **O compilador/parser pega o EXATO, de graça:** sintaxe (vírgula em `{}`, `break when`, default-arg
  na semente) e tipos (comparação `i32 vs u64`, conversão que perde, overload) — regras atômicas que o
  type-checker e o parser já verificam com 100% de certeza e custo zero. Isso **nem é "tooling
  constitucional"** — é só o compilador fazendo seu trabalho. (As leis foram desenhadas pra que muito
  do mecânico caia aqui — exclusão-por-construção torna o inválido um erro de compilação.)
- **O LLM-instruído-pela-constituição pega o RELACIONAL/interpretativo:** M.3 (um operador/comentário
  "mente"? — comparar intenção declarada com comportamento real; o comentário mentiroso do
  `read_underscore` é o caso exemplar), M.5 (uma conveniência "justifica seu peso"? — juízo de valor),
  exceções-vs-violações (precisa da hierarquia inteira), e a coerência documental (decisões que se
  contradizem, referências órfãs após uma reversão — como a caça ao `bigint`-nativo desta sessão, mas
  semântica). *Isso* é o tooling constitucional propriamente dito.

**Arquitetura (a ressalva "sem travar"):** roda FORA do caminho crítico (um `teko check
--constitution` separado, ou hook opcional — não parte da compilação que gera o binário; o dev invoca
quando quer). Emite **avisos/relatório, não erros que bloqueiam** (algumas violações são exceções
conscientes — travar seria errado; e o juízo do LLM é sugestivo, não autoritativo). **Fonte das
regras: a biblioteca dos três documentos** — `TEKO_CONSTITUTION.md` (as leis, supremas e imutáveis),
`TEKO_LEGISLATION.md` (as normas vigentes destiladas, que citam a lei que as rege), e `TEKO_HISTORY.md`
(o was→is→why). A constituição + a legislação são a instrução do LLM-verificador; a história explica.
*Os documentos que escrevemos SÃO a especificação do tooling.* (As "Agent rules"/normas ganham um
segundo propósito: instrução pro agente humano E pro LLM-verificador.) Pós-self-hosting; o
orquestrador escrito em Teko, naturalmente — a linguagem exercitada sobre si mesma.

---

## 10. 🔁 Memória de redefinições (índice anti-ambiguidade)

> **Por que isto existe:** construímos por descoberta, então **fechamos** coisas que
> *mais adiante* foram mudadas. O histórico (no EVOLUTION) preserva o was→is→why de
> propósito — mas isso cria o risco de alguém (ou um agente fatiando o documento) ler
> uma decisão **antiga** como se fosse a vigente. Esta tabela é a salvaguarda: o mapa
> do que foi redefinido. **Não altera o histórico — apenas anota para a revisão final
> e impede que agentes, ao dividir o material, cortem um trecho superado como se fosse
> verdade.** Manter sincronizada com o EVOLUTION.

| Tema | Onde foi dito antes | Dizia | Redefinido em | Estado final |
|---|---|---|---|---|
| Propagação de erro | EVOLUTION B.1 / plano rev ≤9 | propagado com operador `?` | **B.16** (rodada 2) | **Sem `?` de propagação na semente**; erro é **sempre `match`**. `?` fica exclusivo de nulabilidade (`T?`/`?.`/`??`). |
| Aliases de tipo | decisão de tipos pré-B.13 | `type X = Y` dá **alias** transparente grátis (`Meters` *é* `i32`) | **B.13** (rev 9) | **Tipagem nominal**: `type X = Y` é tipo **distinto**, sem alias. Newtype transparente, regra Go p/ operações. |
| `break` em loops | código do lexer (Parte A) + texto inicial | `break when cond` (posfixado) | **B.20** (rodada C.5) | posfixado `when` cortado; usar `if cond { break }`. `when` é **só** guard de match. |
| `unless` | cogitado na teia condicional | `unless cond` como condicional posfixado | **B.20** (rodada C.5) | **Banido.** É `when !cond` (redundante) e piora condições compostas. Negação é `!cond`. |
| `static` | plano §2.8 (pré-Frente 4) | "não existe na semente; **nasce na evolução** com objetos" | **B.25 / Frente 4** (rev 13) | **BANIDO** (sempre, não só ausente) — estado global disfarçado. Substituto: **DI + singleton** (evolução). Métodos auto-imbuídos pelo compilador (`to_string`/`parse`) NÃO são o `static` banido. |
| Separador de match-binding | EVOLUTION B.15 | `Binary { left, right }` (vírgula) | **B.26 / Frente 5** (rev 13) | Dentro de `{}` o separador é **newline ou `;`, nunca vírgula** — `Binary { left; right }`. Regra "`{}` = newline/`;`, sem vírgula" é **absoluta**. |
| Símbolo `$` | plano §2.1 (pré-Frente 6) | `$` **ilegal** na semente (para-keywords, adiado) | **B.27 / Frente 6** (rev 13) | `$` é o **símbolo das operações de string**: interpolação `$"..."` e **concatenação** `$` (string+string). Maximal munch distingue. |
| Concatenação com `+` | implícito (linguagens comuns) | `+` concatena strings | **B.27 / Frente 6** (rev 13) | **`+` NUNCA concatena** (mentira-de-operador, M.0, mistura-banida, legibilidade-local). Concatenação é `$`; mistura é interpolação. |
| Símbolo `#` | plano §2.1 (pré-Frente 7) | `#` ilegal (metaprogramação adiada) | **B.28 / Frente 7** (rev 13) | **Reservado** para comunicação com o compilador (diretivas/atributos/pragmas, evolução). |
| Métodos de struct & `to_string`/`parse` | §2.8 + B.27 (pendente/adiado) | métodos = decisão maior adiada; conversão de struct só via função-livre-solta | **B.29** (rev 14) | **Resolvido.** Função no struct com 1º arg `self` SOLTO (sem tipo) = método de instância (cópia, sem ref, `.`); sem `self` = estática do tipo (`::`, "estático sem `static`"). Desencorajada mas disponível. Sem overload/override; aridade rígida (default args = evolução). |
| Char vazio `''` | implícito (maioria das linguagens) | `''` = erro ("empty character literal") | **B.28 audit** (rev 14) | **`''` = byte ZERO (`0u8`)** — corrige o erro da maioria (char=u8, vazio = byte zero; útil p/ nulo). Trade: `''` acidental vira zero. |
| Princípios M (constituição) | M.0/M.1/M.2 soltos (revs ≤13) | três princípios sem ordem explícita | **rev 14** | **Constituição ordenada M.0–M.5** (Metal/Segurança/Explicitude/Honestidade/Ordem-constr/Austeridade). COMPÕEM, não competem; menor número vence o desempate. Antigos M.1→M.4, M.2→M.5. |

> **Regra de precedência:** quando esta tabela e uma entrada antiga divergirem, vale o
> **"estado final"** (e a entrada que ela aponta). Toda vez que uma decisão for
> reescrita, **adicionar uma linha aqui** antes de seguir.
