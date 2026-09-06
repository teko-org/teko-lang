# Plano — §9 A: sobrecarga de método/função (method/function overloading)

> **Status:** DESIGN. Read+design apenas — nenhum código de produto editado, nenhum build, nenhum
> reseed. Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (onda de migração-de-superfície 0.3.1; drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 A (abaixo, VINCULATIVOS — o plano desenha À
> VOLTA deles, nunca os re-abre). Doc 2 §9: "select_overload no caminho de chamada; sufixo de
> símbolo para conjuntos sobrecarregados. INERTE até existir um conjunto de sobrecarga."
> **Irmãos:** §9 B (duplicados-de-TIPO passam a chaveados-por-aridade — `check_no_duplicate_types`),
> §9 C (defaults + args nomeados `:=`). §9 A depende do mecanismo de §9 C (nomes de parâmetro como
> discriminante) que JÁ EXISTE hoje (`resolve_defargs`, `Func.param_names/n_required/defaults`).
> **Lei permanente:** Teko-only (.tks), W15+Javadoc-completo em TODA declaração, law-first.

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

1. **Definição SEMPRE permitida.** Zero análise de colisão entre sobrecargas na DECLARAÇÃO. O
   compilador NUNCA bloqueia definir uma fn/método porque outra do mesmo nome existe.
2. **Resolução no CALL-SITE, em comp-time.** A sobrecarga é escolhida na chamada, a partir de: tipos
   dos argumentos posicionais + nomes dos parâmetros nomeados/default.
3. **Ambiguidade = erro NO CALL-SITE** (nunca na definição, nunca em runtime). >1 candidato
   compatível ⇒ erro de ambiguidade na chamada. Regra UNIFORME: qualquer conjunto de assinaturas que
   individualmente compilam é legal; a CHAMADA falha se não der informação para escolher exatamente 1.
4. **Desambiguação explícita:** um cast no call-site resolve — `a(32 to i32, 8 to u8)`.
5. **NOME de parâmetro é discriminante** quando defaults/args-nomeados entram em jogo (a assinatura
   efetiva inclui os nomes dos parâmetros nomeáveis; duas sobrecargas podem diferir só por nome sob
   defaults). Amarra em §9 C.
6. **INERTE até existir conjunto de sobrecarga.** O caminho single-decl / primitivo tem de ficar
   byte-idêntico — sem conjunto de sobrecarga ⇒ zero mudança de comportamento ⇒ fixpoint seguro.

---

## 1. Estado de HOJE — onde a "rejeição de mesmo-nome" realmente vive (achado importante)

**Achado central (a verificar mentalmente pelo implementador, confirmado por leitura):** hoje NÃO
existe um diagnóstico de checker que rejeite duas funções com o mesmo `(namespace, nome)`. A
"rejeição" que o ruling manda RELAXAR é emergente de dois mecanismos, não de um `check`:

- **Registo de função sem gate de duplicado.** `collect_fn_signature`
  (`src/checker/collect.tks:329-332`) chama `define_fn` INCONDICIONALMENTE. Idem métodos
  (`collect.tks:402`, `:464`) e a reconstrução (`collect.tks:2515`).
- **Env plano com SHADOWING.** `Env` (`src/checker/scope.tks:72`) é uma lista plana; `define_fn`
  (`scope.tks:207`) faz `push`. A resolução `lookup_call` (`scope.tks:421`) e o índice
  `call_base_pos` (`scope.tks:394`) devolvem o match de MAIOR posição — logo a ÚLTIMA declaração
  vence, silenciosamente sombreando a primeira (nenhum erro).
- **Colisão de símbolo C.** `cb_fn_name` (`src/codegen/codegen.tks:558`) e o gémeo nativo
  `mangle_fn_symbol` (`src/lir/lower.tks:933`) chaveiam o símbolo por `(ns, name)`. Duas fns do mesmo
  `(ns,name)` emitem o MESMO símbolo `teko_<ns>__<name>` ⇒ **erro de redefinição do `cc`** (ou
  símbolo duplicado no backend nativo).

⇒ A "rejeição" é: (a) shadow silencioso no checker + (b) colisão `cc`/nativo no link. **Não há
diagnóstico para apagar.** RELAXAR = tornar a resolução ciente-de-sobrecarga E tornar o mangling
distinto-por-sobrecarga. Isto é o oposto de §9 B, cujo lado-TIPO TEM um validador explícito
(`check_no_duplicate_types`, `collect.tks:2014`, que §9 B re-chaveia por aridade); o lado-FUNÇÃO não
tem análogo — o análogo é a colisão de mangling.

**Contraste com §9 B (não confundir):** `check_no_duplicate_types` (`collect.tks:2014`) /
`duplicates_of_reg` (`collect.tks:2036`) são sobre a `TypeTable` (só `TypeDecl` entra —
`collect_types`, `collect.tks:54-63`). Funções NUNCA entram na `TypeTable`; vivem no `Env`. §9 A não
toca `check_no_duplicate_types`.

---

## 2. Representação do CONJUNTO de sobrecarga

O `Env` plano JÁ permite N bindings do mesmo nome coexistirem (é o que o shadowing usa). Não muda a
estrutura. Adiciona-se:

- **Enumeração de candidatos** — nova fn em `scope.tks` que devolve TODOS os bindings que resolvem um
  callee (não só o de maior posição), preservando a ordem de declaração:

```
/**
 * lookup_call_candidates — EVERY value binding that resolves the callee `callee` under the same
 * namespace rules as `lookup_call` (`call_binding_matches`), in DECLARATION order (base globals then
 * locals, low position first). The overload-resolution input: `lookup_call` returns only the
 * highest-position match (the shadow), which is correct for a single declaration but hides an
 * overload SET; this returns the whole set so `select_overload` can pick by argument types.
 *
 * A single-declaration name yields a one-element list, and `select_overload` on a singleton is a
 * pass-through — that is what keeps the non-overloaded path byte-identical (ruling 6).
 *
 * @param env     the typing environment (local bindings + sealed globals + current namespace)
 * @param callee  the call callee path
 * @return        every resolving binding, declaration order; empty when the callee is unbound
 * @since §9 A
 */
pub fn lookup_call_candidates(env: Env, callee: parser::Path): []ValBinding
```

- **Índice de conjunto-de-sobrecarga `(ns, name) -> count`** para o GATE do sufixo (ver §4). Chave
  `(ns, name)` — NUNCA o nome nu (ver risco-fixpoint §5). Construído UMA vez sobre `env.base_slots`
  logo após `seal` (`scope.tks:118`), filtrando bindings cujo `type` é `Func` e `is_const == false`.
  Exposto como predicado:

```
/**
 * env_is_overloaded — is `(ns, name)` a declared FUNCTION OVERLOAD SET: more than one non-const
 * `Func` binding sharing exactly this declaring namespace AND bare name in the sealed globals? The
 * GATE for symbol suffixing (§4): false ⇒ the sole declaration keeps its historic unsuffixed symbol
 * (ruling 6, fixpoint-safe); true ⇒ every member of the set carries a signature suffix.
 *
 * Keyed on `(ns, name)` — never the bare name alone: two same-named functions in DIFFERENT
 * namespaces (`teko::numeric::dec::div` vs `teko::numeric::bigint::div`) are NOT an overload set,
 * mangle to distinct symbols already, and must stay untouched (§5).
 *
 * @param env   the SEALED typing environment
 * @param ns    the function's declaring namespace
 * @param name  the function's bare name
 * @return      true iff two or more function declarations share this exact `(ns, name)`
 * @since §9 A
 */
pub fn env_is_overloaded(env: Env, ns: str, name: str): bool
```

Nota de implementação (verificar): para métodos, o `ns` é o pseudo-namespace de método de `collect`
(`<owner-ns>::<TypeName>`, `collect.tks:365`), portanto duas sobrecargas do mesmo método de uma
classe partilham o mesmo `(ns,name)` e formam um conjunto — o mecanismo é uniforme fn/método.

---

## 3. Resolução no call-site — `select_overload`

Ponto único: `type_call` (`src/checker/typer.tks:2028`). Hoje resolve o callee via um único
`lookup_call` (chamado em `typer.tks:2076`, `:2096`, `:2097`) e ramifica em `match ft { Func as f =>
… }` (`:2123`). A mudança é cirúrgica: substituir a obtenção do `ft` único por uma SELEÇÃO sobre os
candidatos, ANTES do bloco existente de tipagem-de-argumentos (`:2123-2250`) — que permanece
intacto, operando sobre o `Func` vencedor.

**Ordem preservada:** os fast-paths builtin/list/ptr/svc (`typer.tks:2050-2095`) e o ramo
panic/exit (`:2078-2095`) correm ANTES da seleção, inalterados (disparam só quando o lookup falha —
`!in_scope`). A seleção entra onde hoje está `var ft = match lookup_call(...)` (`:2097`).

### 3.1 Algoritmo (comp-time, no call-site)

```
/**
 * select_overload — choose exactly ONE overload of `callee` for the argument list, at the call site,
 * in compile time (rulings 2/3). Given the candidate set (`lookup_call_candidates`), it scores each
 * candidate against the CALL and returns the unique best, or a located error.
 *
 * Per candidate, in order:
 *   1. ARITY / NAMED-ARG shape: run the candidate through `resolve_defargs` (named/default binding,
 *      typer.tks:1472) or `pack_variadic_args` for a variadic tail; a candidate whose arity or named
 *      arguments do not bind is DROPPED (not an error — it just is not this overload). This is where
 *      ruling 5 lives: a named/default argument selects by PARAMETER NAME, so two overloads that
 *      differ only by a nameable parameter's name are told apart here.
 *   2. TYPE compatibility: every already-typed positional argument must fit its parameter under the
 *      SAME rule an ordinary call uses — `assignable_to` (typer.tks:4764) / `numeric_widens_implicitly`
 *      (expr.tks:198). A candidate with any non-fitting argument is DROPPED.
 *   3. Surviving candidates are ranked into two TIERS to stop a widening match from spuriously
 *      colliding with an exact one:
 *        - EXACT tier: every positional argument's type is `type_eq` (type.tks:152) to its parameter.
 *        - WIDENED tier: at least one argument fits only by implicit widening.
 *      If the EXACT tier has exactly one member, it wins outright (an exact match beats any number of
 *      widened matches — no ambiguity). Otherwise resolution is decided within the highest non-empty
 *      tier.
 *
 * Outcome:
 *   - exactly 1 in the deciding tier -> bind it (return its ValBinding / Func).
 *   - 0 survivors                    -> "no matching overload for '<name>'" + rendered arg types.
 *   - >1 in the deciding tier        -> AMBIGUITY error AT THE CALL SITE (ruling 3), message listing
 *                                       every tied candidate's rendered signature.
 *
 * A one-candidate set skips scoring entirely and returns that candidate (byte-identical single-decl
 * path, ruling 6).
 *
 * @param cands  the candidate bindings (`lookup_call_candidates`)
 * @param args   the call's already-typed positional arguments (post arg-typing)
 * @param names  the call's argument names (parallel to the source args; "" for positional)
 * @param table  the collected type table (for `assignable_to`)
 * @return       the chosen binding, or a located ambiguity / no-match error
 * @throws       when 0 candidates fit, or >1 fit with no unique best
 * @since §9 A
 */
fn select_overload(cands: []ValBinding, args: []TExpr, names: []str, table: TypeTable): ValBinding | error
```

**Interação widening × exato (ruling 3, anti-ambiguidade espúria):** sem os tiers, dado
`fn f(x: i64)` e `fn f(x: i32)` a chamada `f(a)` com `a: i32` seria compatível com AMBAS (i32 exato,
i64 por widening) ⇒ ambíguo — indesejável. Os tiers dão prioridade ao match exato: `f(a: i32)`
escolhe `f(i32)`. Só quando NÃO há exato único é que o widening decide; dois widenings distintos
(p.ex. `i32` que cabe em `i64` e em `f64` — se ambos existissem e fossem lossless) permanecem
ambíguos, resolvidos por `to` explícito (ruling 4, que muda o tipo do argumento e re-pontua).

**Ligação ao bloco existente:** após `select_overload` devolver o binding vencedor, extrai-se o seu
`Func` e o seu `ns` (o binding carrega `ns`) e o fluxo entra no `match ft { Func as f => … }`
existente (`typer.tks:2123`) SEM alteração — a inferência de genéricos, defaults, auto-ref e
coerção-de-argumento (`:2149-2249`) ficam byte-idênticas. `resolved_ns` passa a vir do binding
vencedor em vez de `call_ns(env, c.callee)` (`typer.tks:2096`).

**Nota de ordem de tipagem (verificar):** hoje os argumentos são tipados DENTRO do ramo `Func`
(`typer.tks:2128-2148`), DEPOIS de escolher `ft`. Para pontuar por tipos, a seleção precisa dos
tipos dos argumentos ANTES. Duas opções para o implementador escolher (law-first, preferir a menos
invasiva):
  - (A) tipar os argumentos posicionais uma vez ANTES da seleção (sem `expected`-type, pois o
    parâmetro-alvo ainda não é conhecido), pontuar, depois deixar o bloco existente RE-tipar com
    `expected` (custo: dupla tipagem dos args). Nota: literais sem sufixo (`TNumber` default i64/f64)
    podem exigir o tier de widening para caber num parâmetro estreito — aceitável.
  - (B) fatorar a tipagem-de-argumentos para fora do ramo `Func` e passá-la à seleção.
  O implementador DEVE confirmar que a opção escolhida não altera diagnósticos existentes de
  argumento (ordem/mensagem) no caso single-decl. Preferir (A) se preservar mensagens; senão (B).

---

## 4. Mangling — sufixo por-sobrecarga (os DOIS backends, em paridade)

O símbolo de uma função é produzido em DOIS manglers que `cgt_mangle_parity_c_and_native` prova
byte-idênticos:
- C: `cb_fn_name` (`src/codegen/codegen.tks:558`).
- Nativo/LIR: `mangle_fn_symbol` (`src/lir/lower.tks:933`).

**Fonte única da verdade = o CHECKER.** Para garantir paridade e o gate INERTE, o checker computa o
sufixo e CARIMBA-o na TAST; ambos backends apenas o anexam. Isto evita re-derivar o sufixo duas vezes
(risco de divergência) e concentra a decisão no checker (onde a lei manda a resolução viver).

### 4.1 Novos campos na TAST

- `TFunction` (`src/checker/tast.tks:176`): novo campo
  `overload_suffix: str` — "" quando a fn é a única declaração do seu `(ns,name)`; senão o mangle da
  assinatura (ver 4.3). Carimbado quando o typer materializa a `TFunction`, consultando
  `env_is_overloaded(env, f.namespace, f.name)`.
- `TCall` (`src/checker/tast.tks:39`): novo campo
  `overload_suffix: str` — o sufixo do candidato ESCOLHIDO por `select_overload` (idem "" no caso
  single-decl). Carimbado nos vários `TExpr { kind = TCall { … } }` construídos em `type_call`
  (`typer.tks:2192`, `:2207`, `:2249`).

Doc-comment do campo (verbatim para o implementador):

```
    /**
     * overload_suffix — the signature mangle that distinguishes this member of a FUNCTION OVERLOAD
     * SET from its siblings in the linker-symbol space (§9 A). Empty for the sole declaration of a
     * `(namespace, name)` — which keeps its historic unsuffixed symbol byte-for-byte, so a program
     * with no overload set reproduces the pre-§9-A output exactly (ruling 6 / fixpoint). Both
     * backends append it verbatim after the `(ns, name)` mangle (`cb_fn_name` / `mangle_fn_symbol`),
     * so definition, prototype, call, closure literal and `.tsym` all agree by construction.
     */
    overload_suffix: str
```

### 4.2 Sítios que anexam o sufixo (todos os produtores do símbolo)

Cada sítio que produz o símbolo de uma fn de utilizador tem de anexar o sufixo do MESMO objeto:
- Def/protótipo C: `emit_function_sig` (`codegen.tks:9585`), `emit_function_mode` (`codegen.tks:12843`),
  `.tsym` writer (`codegen.tks:12701`).
- Call C: `emit_call` (`codegen.tks:4523`).
- Closure-literal C (fn-como-valor): `codegen.tks:7699`.
- Def nativa: `mangle_fn_symbol(f.namespace, f.name)` em `to_lir`/`new_func` (`lower.tks:15155`).
- Call nativa: `lower.tks:5274`.
- Closure nativa: `lower.tks:5764`.
- `.tsym`/DWARF/reachability address-map: `src/build/project.tks:3257` (`DwarfFuncFact`).

**Forma recomendada (menos invasiva, paridade garantida):** dar a `cb_fn_name` e a
`mangle_fn_symbol` um parâmetro default novo `suffix: str = ""`, e cada call-site passa
`f.overload_suffix` (lado-def) ou `c.overload_suffix` (lado-call). Default "" ⇒ todos os call-sites
NÃO-tocados ficam byte-idênticos; só os sítios acima passam o novo argumento. Assinaturas:

```
fn cb_fn_name(buf: []byte, ns: str, name: str, suffix: str = ""): []byte      // codegen.tks:558
pub fn mangle_fn_symbol(ns: str, name: str, flat: bool = false, suffix: str = ""): str   // lower.tks:933
```

(Verificar: `mangle_fn_symbol` já tem um default `flat`; §9 C/defaults têm de aceitar dois defaults
posicionais — confirmar que o corpus-seed já suporta dois parâmetros default, senão passar por nome
`suffix := …` ou sequenciar após §9 C aterrar. Ver §7 sequenciação.)

### 4.3 Forma do sufixo (determinística, estável, C-legal)

O sufixo mangla a assinatura EFETIVA. Recomendação: `"__ov_" ~ <mangle dos tipos de parâmetro em
ordem, juntos por "_">`, reusando o mangler de tipo já existente (`mangle_type_name`/`cb_tysym`,
`codegen.tks:532/482`) para cada parâmetro, e — quando o nome é discriminante (ruling 5, §9 C) —
incluindo os nomes dos parâmetros nomeáveis. Requisitos:
- **Determinístico e independente de ordem-de-declaração** (o sufixo é função só da assinatura, não
  da posição no ficheiro) — senão o fixpoint quebra sob reordenação.
- **Injetivo sobre assinaturas distinguíveis** — dois membros do conjunto NUNCA colidem (senão volta
  a colisão `cc`). Como o conjunto só se forma quando as assinaturas diferem (defs sempre permitidas,
  ruling 1), o mangle das assinaturas é naturalmente distinto.
- **C-legal** (só `[A-Za-z0-9_]`) — reusar `cb_tysym`/`cb_ident` garante-o.
O implementador DEVE fixar a gramática exata do sufixo num comentário Javadoc na fn que o produz
(nova fn `fn overload_suffix_of(f: Func /* ou TFunction */): str` no checker) e cobri-la por fixture.

---

## 5. Segurança de FIXPOINT (o argumento, e o scan de risco)

**Argumento (aditivo):** o sufixo só é não-vazio quando `env_is_overloaded(ns, name)` é verdadeiro,
i.e. quando ≥2 declarações partilham exatamente `(ns, name)`. **Nenhum tal conjunto existe na fonte
atual do compilador** — se existisse, os DOIS mesmos símbolos `teko_<ns>__<name>` já colidiriam no
`cc`/nativo e a árvore NÃO compilaria hoje. Logo, para todo o corpus atual, `env_is_overloaded` é
falso em todo o lado ⇒ `overload_suffix == ""` em toda `TFunction`/`TCall` ⇒ `cb_fn_name`/
`mangle_fn_symbol` produzem o símbolo histórico ⇒ o bootstrap reproduz-se byte-a-byte. `select_overload`
sobre um conjunto-de-um é pass-through, e a resolução por maior-posição continua a valer (o único
candidato É o vencedor). INERTE, como Doc 2 §9 exige.

**Risco-fixpoint identificado e neutralizado — o gate TEM de chavear `(ns, name)`, nunca o nome nu.**
Scan heurístico (grep de nomes de fn repetidos na árvore) revela nomes bare repetidos CROSS-namespace:
`div` ×3 (`teko::numeric::dec`, `teko::numeric::bigint`, runtime), `to_str` ×3, `read` ×7, `write`
×5, `len`/`seek`/`cmp`/`close`/`append_bytes` ×3, e ~30 pares ×2 (p.ex. `type_is_void`, `prim_kind_of`,
`str_eq`). TODOS confirmados/esperados em namespaces DIFERENTES (verifiquei `div`: `dec` vs `bigint`
vs runtime). **Estes NÃO são conjuntos de sobrecarga** — mangleiam para símbolos distintos hoje via o
namespace, e a `lookup_call` namespace-aware já os separa. Se o gate chaveasse pelo nome nu, estes
~40 nomes tornar-se-iam falsamente "conjuntos sobrecarregados", ganhariam sufixo, e o fixpoint
QUEBRARIA. **Mitigação (lei do plano):** `env_is_overloaded` e o índice de conjunto chaveiam o par
`(ns, name)` exato; a contagem só sobe com colisão no MESMO namespace. Cobrir por um teste-de-checker
que afirma `env_is_overloaded("teko::numeric::dec","div") == false`.

**Verificação obrigatória do implementador (não pude correr build):** após implementar, confirmar por
build que nenhum par same-`(ns,name)` acidental existe (incluindo métodos same-name na MESMA classe,
que partilham o pseudo-ns `<owner-ns>::<Class>`); qualquer par pré-existente teria estado a
compilar-por-shadow e passaria AGORA a ganhar sufixo, mudando o símbolo e quebrando o fixpoint. O scan
acima não é conclusivo para métodos (o grep não distingue owner) — o implementador DEVE confirmar via
o gate real. Se aparecer algum, REPORTAR para cima (não é âmbito de §9 A resolvê-lo aqui).

---

## 6. Fixtures de regressão

Layout (confirmado): cada regressão é um projeto `examples/regressions/<nome>/` com `<nome>.tkp`
(manifesto), `<nome>.tkr` (Feature/Scenario), `main.tks`, `src/`. ACEITAR usa
`Then stdout pattern = "…"` (código lido por `println`/`exit`); REJEITAR usa `Then diagnostic = "…"`
contra um build-que-falha partilhado (canal `examples/regressions/diagnostics/`, um ficheiro-fonte
por construção rejeitada sob `src/<caso>/case.tks`). Os scripts descobrem por diretório — não é
preciso registar nomes à mão (verificar `scripts/no_skips_gate.sh`/`ci_*`, que iteram `examples/`).

### 6.1 ACEITAR — `examples/regressions/overload_resolve/` (novo projeto)

Um `main.tks` cujo `exit`/`println` codifica em ARITMÉTICA qual sobrecarga correu (padrão da fixture
`builtins`), para que um relapso mova o valor em vez de passar em silêncio:

- **A1 — resolução por tipo posicional.** `fn area(w: i64): i64 { w * w }` e
  `fn area(w: f64): i64 { (w * w) to i64 + 100 }`. `area(3)` → 9 (ramo i64), `area(3.0)` → 109 (ramo
  f64). Soma → `exit`/stdout esperado que só ambos os corpos certos produzem.
- **A2 — discriminação por NOME de parâmetro sob defaults (ruling 5 / §9 C).**
  `fn mk(width: i64 = 1): i64 { width * 2 }` e `fn mk(height: i64 = 1): i64 { height * 3 }`.
  `mk(width := 5)` → 10, `mk(height := 5)` → 15. (Confirma que o nome desambigua duas assinaturas de
  tipo idêntico.)
- **A3 — exato-bate-widening (anti-ambiguidade).** `fn f(x: i32): i64 { 1 }` e
  `fn f(x: i64): i64 { 2 }`; `var a: i32 = …; f(a)` → escolhe `f(i32)` (retorna 1), NÃO ambíguo.
  stdout/exit distingue de "2".

Expectativas `.tkr`: `Then stdout pattern = "<valor combinado>"` (um único número que só a combinação
correta produz).

### 6.2 REJEITAR — dobrar no canal `examples/regressions/diagnostics/`

Cada caso um ficheiro `src/<caso>/case.tks` (sem `Given source`, contra o build-que-falha partilhado)
+ um Scenario com `Then diagnostic = "<substring>"`:

- **R1 — ambiguidade no call-site.** `fn g(x: i32): i64 {…}`, `fn g(x: u32): i64 {…}`; `g(0)` (literal
  `0`/`i64` default cabe em ambos por widening, nenhum exato) → `Then diagnostic = "ambiguous"` (e a
  mensagem lista os candidatos). Confirma ruling 3: falha na CHAMADA, não na definição.
- **R2 — ambiguidade resolvida por `to` (ruling 4) — este é ACEITAR, par de R1.** Melhor colocá-lo em
  6.1 como A4: mesmas duas `g`, mas `g(0 to u32)` compila e corre o ramo `u32`. Prova que o cast
  desambigua. (Fica no projeto ACEITAR, stdout pattern.)
- **R3 — sem sobrecarga compatível.** `fn h(x: str): i64 {…}`, `fn h(x: bool): i64 {…}`; `h(3)` →
  `Then diagnostic = "no matching overload"`.

(Nota: R1/R3 exercitam o call-site; a DEFINIÇÃO dos pares nunca é rejeitada — se o build partilhado
recusasse a definição, isso já seria um bug contra ruling 1, pelo que estas fixtures também PROVAM que
definir é sempre permitido.)

---

## 7. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Cada crumb compila e passa o gate rápido; os pontos de RITUAL (gate completo) estão marcados. §9 A
DEPENDE do mecanismo de nomes-de-parâmetro de §9 C (`resolve_defargs`, `param_names`), que JÁ existe
no corpus atual — logo §9 A pode aterrar independentemente. A ÚNICA amarração a confirmar é o
double-default em `mangle_fn_symbol` (crumb 5): se o seed não suportar dois parâmetros default
posicionais, usar `suffix := …` por-nome ou sequenciar crumb 5 após §9 C aterrar no seed.

1. **Enumeração de candidatos.** Adicionar `lookup_call_candidates` + `env_is_overloaded` + o índice
   `(ns,name)->count` em `scope.tks` (após `seal`). Puro, sem call-sites novos ainda (código morto
   compilável, Javadoc completo). Teste-de-checker: `env_is_overloaded("teko::numeric::dec","div") ==
   false` e um caso positivo sintético. — *inerte, byte-idêntico.*
2. **Campos TAST `overload_suffix`.** Adicionar a `TFunction` (`tast.tks:176`) e `TCall`
   (`tast.tks:39`), inicializados a "" em TODOS os construtores existentes (grep por `TCall {` e
   `TFunction {`). — *inerte: "" em todo o lado ⇒ nada muda.* **RITUAL: gate completo** (toca tipos
   centrais; garantir que todos os construtores foram atualizados).
3. **`select_overload` + fio no `type_call`.** Implementar `select_overload` (§3.1) e substituir o
   `lookup_call` único (`typer.tks:2097`) pela seleção sobre candidatos; carimbar `overload_suffix` do
   vencedor nos `TCall`. Resolver a ordem-de-tipagem-de-args (opção A ou B, §3). — *com conjunto-de-um,
   pass-through byte-idêntico.*
4. **Carimbar `TFunction.overload_suffix`** onde o typer materializa a `TFunction`, via
   `env_is_overloaded`. — *ainda "" em todo o corpus atual.* **RITUAL: gate completo.**
5. **Mangling nos dois backends.** Parâmetro `suffix: str = ""` em `cb_fn_name` (`codegen.tks:558`) e
   `mangle_fn_symbol` (`lower.tks:933`); passar `f.overload_suffix`/`c.overload_suffix` nos sítios de
   §4.2 (def/protótipo/call/closure/tsym, C e nativo). Confirmar `cgt_mangle_parity_c_and_native`
   continua verde. — *default "" ⇒ símbolo histórico byte-idêntico.* **RITUAL: gate completo** (é a
   crumb que pode mover símbolos; a paridade C↔nativo é a rede).
6. **Fixtures ACEITAR** (`examples/regressions/overload_resolve/`, casos A1–A4). Primeira crumb com
   um conjunto de sobrecarga REAL no corpus ⇒ exercita sufixo + resolução ponta-a-ponta. **RITUAL.**
7. **Fixtures REJEITAR** (dobrar R1/R3 em `examples/regressions/diagnostics/`, novos `src/<caso>/case.tks`
   + Scenarios). **RITUAL.**
8. **Reseed + PROVENANCE** (crumb final, §8).

---

## 8. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate completo passar:

1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → produz `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). Como §9 A é aditivo-inerte no corpus atual, o fixpoint DEVE fechar já
   na crumb 5; as crumbs 6/7 introduzem conjuntos de sobrecarga no CORPUS DE TESTE (fixtures), não no
   compilador, logo não afetam o auto-mangling do próprio compilador.
4. Harvest: `bootstrap/teko.c` (o novo seed) + atualizar `bootstrap/PROVENANCE` (novo hash/proveniência).
5. NUNCA correr `teko test .` (fuga de memória). Gate por `--no-verify` + os `scripts/*.sh`.

---

## 9. Riscos + tensões de lei (com resolução recomendada)

- **R-fixpoint (gate por nome nu).** Neutralizado: gate `(ns,name)` (§5). Recomendação FIRME, não
  opcional.
- **Ordem de tipagem de argumentos vs. `expected`-type.** §3 opção A pode re-tipar args; risco de
  mudar mensagens de erro no single-decl. Resolução: preferir a opção que preserva mensagens; cobrir o
  single-decl por um fixture que fixa a mensagem exata. Sem tensão de lei.
- **Double-default em `mangle_fn_symbol`.** Amarração de seed possível (§7 crumb 5). Resolução:
  `suffix := …` por-nome, ou sequenciar após §9 C no seed. Sem tensão de lei.
- **Reachability chaveia por nome nu** (`src/build/reachability.tks:181` `push_new`/`entry_roots`).
  Um conjunto de sobrecarga partilha o nome ⇒ `close_reachable` puxa TODOS os corpos do nome
  (conservador, SEGURO — nunca deixa cair um símbolo referido, só faz DCE menos agressivo). Sem ação
  requerida; VERIFICAR que nenhum passo dedup dropa uma sobrecarga. Sem tensão de lei.
- **Sem tensão de lei genuína identificada.** Todos os rulings selados encaixam num desenho
  law-first coerente. Nenhum HALT necessário.

---

## 10. Perguntas ao dono/integrador (não-bloqueantes; adiantei tudo o que não depende delas)

1. **Gramática EXATA do sufixo** (`__ov_` + mangle-de-params; incluir nomes de params só quando são
   discriminantes, ou sempre?). Adiantei a forma recomendada e os invariantes (determinístico,
   injetivo, C-legal); o dono pode fixar a grafia. Não bloqueia as crumbs 1–4.
2. **Preferência exato×widening além de 1 argumento** — o desenho usa "tier exato tem membro único ⇒
   vence; senão decide o tier mais alto não-vazio". Se o dono quiser um score por-argumento mais fino
   (p.ex. "menor distância de widening total"), é uma afinação LOCAL de `select_overload` sem impacto
   nos outros crumbs. Recomendo a regra-de-tiers (mais simples, passa os rulings).
3. **Métodos same-name na mesma classe** — o scan não é conclusivo (grep não vê o owner). Se o gate
   real revelar um par pré-existente, REPORTO para cima (não invento issue).
