# Reificação de `str` — `type str = []byte { métodos }` (D192)

Estado: DESENHO (aguarda ratificação do dono na superfície). Base `fix/retirement @ f7a839c5`.
Keystone owner-ruled D192. NÃO re-abre D145/D133/D134/D187/D188 (deliberados).

## 0. Fatos-âncora verificados no código (não re-derivar)

- **Precedente exato já existe — `bigint`/`dec`.** `scope.tks:268` mapeia o nome de
  superfície `bigint` → `Named { name = "teko::numeric::bigint::BigInt" }`; `typer.tks:266`
  faz o caminho de volta (Named→"bigint") pro operador. `str` reifica pelo MESMO mecanismo,
  trocando `scope.tks:267 if name == "str" { return Str { } }` por um `Named`.
- **Ponte de coexistência já é idioma no `type_eq`.** `type.tks:114/122/131`: `Error` é
  type-equal a `Named{"teko::checker::Error"}`, `Null` a `Named{"teko::checker::Null"}`. A
  mesma técnica faz `Str{}` ≡ `Named{"teko::base::str"}` durante a migração — degrau verde.
- **Provenance/reserved já landado (D133).** `check_modules.tks:165 reserved_type_name`
  já inclui `"str"`/`"char"`; `ns_is_base_provenance` libera `teko::*` a redefinir. Ou seja
  `type str = ...` num arquivo `teko::base` PASSA o gate; um `type str` de usuário é rejeitado
  hoje. Zero trabalho novo de provenance.
- **Newtype sobre slice faz parse e preserva rep.** `parse_decl.tks:992-996` aceita
  `type X = <tipo> { métodos }` (NewtypeBody, backing = qualquer tipo, inclusive `[]byte`);
  `emit_newtype_typedef` (codegen:8654) emite `typedef <backing> <nome>` → layout idêntico a
  `[]byte` = `{ptr,len}` = idêntico a `tk_str`. Rep preservada por construção.
- **Injeção de prelúdio VFS LIVE.** `project.tks:329 inject_runtime_prelude` lê de
  `teko::embed::files()`; `ns_of_prelude_key` já mapeia `teko::/src/base/` → `teko::base`, e
  `prelude_ns_wanted` injeta `teko::base` em TODO artefato. Basta o arquivo de prelúdio existir.
- **Dispatch de método genérico p/ `Named` LIVE.** `typer.tks:1611 type_method_call` exige
  receptor `Named`, acha `.methods` na NewtypeBody (`typer.tks:1642`). Quando `str` for `Named`,
  `s.ends_with(x)` resolve SEM tocar o resolvedor.
- **Predicados-únicos já existem parcialmente.** `codegen.tks:3355 cg_is_str_type`,
  `lower.tks` `is_str`-like. Os ~188 `match {Str=>}` (24 arquivos; codegen 25, lower 17,
  typer 18, scope 13, encoding 17, tkb 3-de-tag, etc.) são a superfície do byte-mover.
- **Branch fundação `expurgo/str-char-surface @ cd28ab50`** já tem os 8 corpos de superfície
  (`bytes_of_str`/`as_ptr`/`len_chars`/`chars`/`char_at`/`str_slice_chars`/`to_lower`/
  `to_upper`) como `exp global fn` em `teko::runtime`, o reinterpret str↔[]byte↔char, e o C
  removido. Verde (fixpoint/ASan/3 harnesses). É a BASE — os corpos viram os métodos de `str`.

## A. Decisões-SUPERFÍCIE (A/B — pro dono ratificar)

### A1 — `char` junto ou `str`-first? → **RECOMENDO str-first; `char` fica `Char {}` transitório.**
Fato que decide: na branch fundação, `char` HOJE é um **fat view `{ptr,len}`** (um sub-slice de
codepoint), não um escalar. `chars`/`char_at`/`str_slice_chars` retornam essa view; o reinterpret
str↔char trata as duas como mesma rep `{ptr,len}`. Reificar `char = u32` (D131) é uma **mudança de
REPRESENTAÇÃO** (de `{ptr,len}` 16 bytes → escalar 4 bytes) — toca layout, codegen `emit_type`,
lower, comparação, e a semântica de `to_lower`/`to_upper` (`one_byte`). É uma migração de peso
PRÓPRIO, ortogonal ao byte-mover de `str`.

- **Rota A (recomendada) — str-first:** `str` vira `Named`; `char` permanece a variante builtin
  `Char {}` (fat view), inalterada. Os métodos `char_at`/`chars`/`str_slice_chars` de `str`
  RETORNAM `char`/`[]char` builtin como hoje. Superfície do usuário: `s.char_at(i)` já funciona;
  `c` continua sendo o tipo builtin `char`. Zero mudança de rep. Risco baixo, byte-drift contido a
  `str`. `char=u32` (D131) fica onda posterior, desbloqueada mas não tanglada.
- **Rota B — char+str coordenados:** reifica `char = u32` newtype no mesmo lote. Superfície:
  `char` ganha métodos (`c.to_lower()`), mas custa a mudança de rep 16→4 bytes ATRAVESSANDO o
  reinterpret str↔char (que hoje depende de mesma-largura) — some o reinterpret, entra conversão
  de valor. Dobra o byte-mover e o risco de fixpoint num único keystone.

Recomendação: **A**. `str` é o keystone pedido; `char=u32` é seu próprio keystone (D131), melhor
não somar dois byte-movers de rep. Registrar `char=u32` como onda-seguinte já desbloqueada.

### A2 — forma da decl no prelúdio-base + como o literal `"abc"` tipa
- **Decl (em `src/base/base.tks`, ns `teko::base`, provenance-base):**
  ```teko
  /**
   * str — the UTF-8 string surface: a newtype over `[]byte` sharing its `{ptr,len}` fat layout,
   * carrying the string methods. Reserved-name; only base provenance may declare it.
   */
  exp global type str = []byte {
      fn ends_with(needle: str): bool { teko::runtime::str_ends_with(self, needle) }
      fn contains(needle: str): bool { teko::runtime::str_contains(self, needle) }
      fn char_at(i: i64): char { teko::runtime::char_at(self, i) }
      fn chars(): []char { teko::runtime::chars(self) }
      fn len_chars(): i64 { teko::runtime::len_chars(self) }
      fn slice_chars(from: i64, to: i64): str { teko::runtime::str_slice_chars(self, from, to) }
      fn bytes(): []byte { self }
      fn as_ptr(): ptr { teko::runtime::as_ptr(self) }
  }
  ```
  `exp global` = roteador (D170/D180), visível bare e `teko::`. `self` = 1º-param sintético
  sem-tipo (idioma de método atual, `parse_decl.tks:531`). Os corpos DELEGAM aos `exp global fn`
  já existentes em `teko::runtime` (a branch fundação) — os métodos são finos; o peso mora em
  `teko::runtime`, chamado pelo caminho genérico. (Alternativa: mover os corpos pra DENTRO do
  método. Recomendo delegar — mantém `teko::runtime::str_*` chamável e minimiza o byte-drift, já
  que os corpos já estão landados na branch.)
- **Literal `"abc"`:** `typer.tks:9 type_strlit` hoje produz `type = Str {}`. Passa a produzir
  `type = str_named_type()` (= `Named{"teko::base::str"}`). Enquanto a ponte `type_eq` estiver
  ativa, um `"abc"` : `Named{str}` casa com qualquer `Str{}` residual — degrau verde.

### A3 — lista de métodos + magic-legítimo vs método-normal
- **Método normal (caminho genérico, corpo de superfície):** `ends_with`, `contains`, `char_at`,
  `chars`, `len_chars`, `slice_chars`, `len` (já `teko::runtime::str_len`), `slice`/`slice_to`/
  `slice_from` (str views), `index_of`/`last_index_of`, `starts_with`. Todos delegam a
  `teko::runtime`/`teko::str`.
- **Reinterpret (magic legítimo — `wrap`/`unwrap`, D131 caso 3):** `bytes()`/`as bytes` e o
  caminho `[]byte as str`. `self` (str) → `[]byte` é reinterpret zero-cópia (mesma `{ptr,len}`).
  Fica magic (é um dos 4 privilégios D188). `as_ptr` é reinterpret p/ endereço (idem).
- **NÃO é método de str:** `peak_rss`, `stdin_eof`, `str_from_utf8`/`str_from_c` (construtores —
  ficam `exp global fn`/estáticos), `os()`/`arch()` (retornam str, não operam sobre str).

## B. Sequência de CRUMBS — escalonada-VERDE (fixpoint byte-idêntico a cada degrau)

Princípio (do bigint + do idioma type_eq): **NUNCA flipar o keyword `str`→`Named` antes de TODOS
os sites de DISPATCH reconhecerem ambos.** Ordem: rotear dispatch por predicado-único (comportamento
idêntico, verde) → só então flipar o keyword → então anexar métodos → então expurgar a variante.

Distinção operacional:
- **Site de CONSTRUÇÃO** (`Str {}` construído p/ assinatura builtin — scope.tks, sig helpers):
  pode ficar `Str {}` durante a coexistência; a ponte `type_eq` casa com o Named. NÃO bloqueia.
- **Site de DISPATCH** (`match t { Str => ... }` onde um VALOR str flui — codegen/lower/tkb/
  encoding/comptime): DEVE reconhecer os dois ANTES do flip, senão o valor Named cai no braço
  `Named =>` errado. É o alvo dos crumbs de roteamento.

### CRUMB 0 — incorpora a branch fundação (pré-req)
Faz merge/cherry-pick de `expurgo/str-char-surface` em `fix/retirement` (os 8 corpos em
`teko::runtime`, o reinterpret str↔[]byte↔char, o C removido). Já é verde. **Reseed.**
Risco: baixo (já validado). Fixpoint gen2==gen3.

### CRUMB 1 — ponte de tipo `Str{} ≡ Named{"teko::base::str"}` (aditivo, dormante) — LANDOU `adda7989`
Em `type.tks type_eq`: braço `Str => match b { Str => true; Named as nb => nb.name == "teko::base::str"; _ }`
e recíproco no braço `Named`. **STR_QN = literal INLINE `"teko::base::str"` em cada uso — NÃO um helper.**
**CORREÇÃO (implementer, 2026-08-28):** um `fn str_qual_name(): str { ... }` QUEBRA a dormância — fn que
retorna `str` (alocante) chamada de dentro do `type_eq` força `type_eq` a ganhar param de região que
propaga por ~89 fns do call-graph = mudança de emissão massiva. Usar SEMPRE literal inline (como Error/Null).
NADA produz `Named{str}` ainda → dormância de RUNTIME (o braço nunca casa). **A emissão NÃO é byte-idêntica**
(os 2 braços SÃO compilados no C) — mas **sem reseed**: gen0-do-seed-commitado ainda builda o tip + fixpoint
gen2==gen3 fecha (o seed converge em 1 passo; reseed absorvido no CRUMB 3). Verificado: 2 hunks de append
puro, zero ripple, ASan 0, 3 harnesses. Risco: mínimo. **STR_QN em todo este doc = o literal inline.**

### CRUMB 2 — predicado-único de DISPATCH por camada (roteamento; comportamento idêntico)
Introduz/consolida `fn is_str_type(t): bool` que reconhece `Str{}` **e** `Named{STR_QN}`, e roteia
TODOS os `match {Str=>}` de dispatch por ele. Como ainda nada produz o Named, o resultado é
byte-idêntico. Sub-crumbs por camada (cada um = 1 lote/fixpoint):
- **2a codegen** (~25 sites; `cg_is_str_type` já existe em 3355 — expandir + rotear emit_type,
  cmp, slice-eq, cast, dc-plan). Este é o de maior byte-drift potencial → isolado.
- **2b lower** (~17 sites; `is_str`, LSliceEqPlan, ffi fat).
- **2c tkb/tkh** (o tag de tipo — poucos sites reais; garantir round-trip lê/escreve os dois).
- **2d encoding (yaml/msgpack ~17) + comptime_fold/consteval/revalidate/escape/monomorph.**
- **2e checker restante** (typer/resolve — dispatch, não construção).
Reseed ao FIM da 2a (codegen muda o C emitido? NÃO — dispatch idêntico; medir. Se byte-idêntico,
sem reseed; caso contrário reseed). Demais sub-crumbs: folha → sem reseed.
Risco: MÉDIO em 2a (a camada que emite C). Mitigar: byte-diff do `teko.c` = zero esperado.

### CRUMB 3 — FLIP do keyword + literal (o degrau que ativa o Named)
Um único lote: `scope.tks:267 str→Named{STR_QN}` **e** `typer.tks:9 type_strlit → Named{STR_QN}`.
Agora todo valor str carrega `Named{STR_QN}`; os predicados do CRUMB 2 já o reconhecem, e a ponte
type_eq (CRUMB 1) casa com os sites de construção `Str{}` residuais. **Este muda o C emitido** (o
ctype de str: decisão de A2 — str-Named DEVE emitir `tk_str`, não o typedef newtype mangled;
adicionar caso em `emit_type`/`cg_named_ctype` que reconhece STR_QN → `tk_str`, preservando o C
atual e minimizando drift). **Reseed obrigatório.** Fixpoint gen2==gen3 + ASan + 3 harnesses.
Risco: **ALTO** — é o flip. Ponto de RITUAL (gate completo).

### CRUMB 4 — anexa os métodos de `str` no prelúdio-base
Adiciona `src/base/base.tks` (ou arquivo `teko::base` dedicado) a decl de A2 com o bloco de
métodos delegando a `teko::runtime`. Como `str` já é `Named` e o dispatch de método p/ Named é
live, `s.ends_with(x)`/`s.char_at(i)` passam a resolver. Verifica que a decl passa
`check_reserved_type_redefs` (base provenance libera). **Reseed** (nova superfície no prelúdio).
Fixpoint + gate completo. Ritual.
Risco: MÉDIO (novo membro no prelúdio injetado em todo artefato; verificar não-colisão).

### CRUMB 5 — varredura de call-site árvore-inteira: `teko::runtime::str_*`/free-fn → método
Converte os CONSUMIDORES no `src/` (e `cases/`+`examples/`+`tklib/` — lei D191) das chamadas
free-fn (`ends_with(s,x)`, `char_at(s,i)`, …) para método (`s.ends_with(x)`, `s.char_at(i)`) ONDE
o dono quer a forma canônica. Grep do qualificador velho = zero nos sítios migrados. Mantém os
`exp global fn` de `teko::runtime` (os métodos delegam a eles). **Reseed.** Gate completo.
Risco: MÉDIO (volume de sítios; mecânico, guiado por grep).

### CRUMB 6 — EXPURGO da variante `Str {}` + name-detects residuais — LANDOU `242a96a5`
Variante `Str{}` removida do enum `Type` + macro; ~40 sítios de construção residuais → `Named{"teko::base::str"}`;
todos os braços `Str=>` de dispatch (guardados por `is_str_type`/`cg_is_str_type` ou fallback `Named`/`_`) removidos;
as duas pontes transitórias (`type_eq` Str≡Named e `type_method_call` Str→str) removidas; tag Str(2) do tkb
retirado (str já serializa como Named tag 6 desde o flip do crumb 3, sem bump); name-detects `len`/`ends_with`/
`contains` (scope.tks sigs + codegen + lower nativo) removidos, caindo no genérico via método; call-sites livres
remanescentes (`main.tks`, `tooling/.../extract.tks`) convertidos p/ método (D191). `concat`/`last_index_of` INTOCADOS
(D194). Fixpoint gen2==gen3 byte-idêntico (gen2.c==gen3.c 21744590 bytes), gen0-do-seed builda o tip, ASan+UBSan
limpo, 3 harnesses verdes, determinismo cross-run, pico 1061 MB (não cresce vs ~1070).

Com o keyword flipado e todo dispatch por predicado, `Str {}` é código morto: remove a variante do
enum `Type` (`type.tks:84`), o macro `Type()`, e todos os `match {Str=>}` (agora inalcançáveis —
o compilador ENUMERA os que sobraram ao auto-compilar, metodologia D125/D181). Remove os
name-detects de str-op remanescentes em codegen/lower (`ends_with`/`contains`/`len` builtin-detect
→ caem no genérico via método). Remove o tag Str do tkb (bump de formato se preciso). A ponte
type_eq (CRUMB 1) sai junto (sem Str{}, sem ponte). **Reseed final.** Gate completo + grep
zero-ref de `Str {}`/`checker::Str` na árvore + 3 harnesses + ASan.
Risco: MÉDIO-ALTO (remoção larga; o fixpoint + o "compilador enumera o morto" é a rede).

**Total: 7 crumbs** (0-6), com sub-crumbs em 2 (2a-2e ⇒ ~5 lotes). Reseeds: 0, 3, 4, 5, 6
(e 2a se medir drift). Rituais (gate completo): 3, 4, 5, 6.

## C. Riscos / landmines

1. **Flip prematuro (CRUMB 3 antes do 2 completo)** → valor `Named{str}` cai no braço `Named =>`
   de um dispatch não-roteado → C errado / crash. Mitigação: ordem estrita; 2 inteiro verde antes
   de 3; grep de `Str =>` remanescente em dispatch antes do flip.
2. **ctype de str vira `tk_slice_byte`/typedef mangled em vez de `tk_str`** → byte-drift MASSIVO no
   teko.c + possível const-ness. Mitigação (A2/CRUMB 3): `emit_type`/`cg_named_ctype` reconhece
   STR_QN → emite `tk_str` (preserva o C atual). Medir byte-diff no CRUMB 2a = 0.
3. **tkb/tkh round-trip** (serialização de tipo): ler um `.tkb` antigo com tag Str vs novo com
   Named. Mitigação: CRUMB 2c lê os dois; bump de formato só no CRUMB 6 quando o tag Str sai.
4. **Memória / ratchet D68.** A reificação é expurgo — o piso é NÃO-CRESCER (sanção D68 p/ expurgo).
   O método delega (não aloca a mais que a free-fn). `chars`/`char_at` já são no-push (pré-aloca
   `[n]char`). Medir `peak MB` do build seco a cada reseed; degrau que CRESCE = corrige antes de
   drenar. Sem novo buffer dinâmico.
5. **Interação com o modelo de arena / W4 (região=param) — NÃO TANGLAR.** `str` como `Named`
   NÃO deve virar "objeto dono da própria arena" (§5 do modelo) nesta onda — é uma **view** sobre
   bytes emprestados (`{ptr,len}` que aponta pro backing do caller/prelúdio), semântica idêntica à
   de hoje. NÃO adicionar arena-no-fat-pointer a str aqui; isso é W4/modelo-de-memória, onda
   separada. `chars()` retorna views que emprestam de `self` (já é assim na branch). Manter str
   como valor-view puro evita colisão com o byte-mover de região.
6. **`char` transitório (rota A).** `char_at`/`chars` retornam `Char{}` builtin (fat view). Quando
   `char=u32` (D131) reificar, os RETORNOS desses métodos mudam de rep — re-visitar então. Marcado
   como onda-seguinte; não bloqueia str.
7. **Provenance coarse.** `ns_is_base_provenance` hoje = "começa com teko::" — qualquer ns teko
   pode declarar `str`. É suficiente (o `src/` é dogfood e não redefine); registrar que o
   endurecimento por provenance-de-injeção (D133 pleno) é refinamento posterior, não bloqueia.
8. **ZERO C adicionado (D148).** Todos os corpos são Teko (branch fundação) ou delegação; nenhum
   `tk_*`/`teko_rt.c` novo. O ctype `tk_str` já existe. Conforme.

## D. Fixtures de regressão (paths que o self-build NÃO exercita)

Só oráculos `.tkr` isolados p/ path que o fixpoint não dirige (lei de testes). O self-build JÁ
exercita `s.ends_with`/`char_at`/etc. ao se compilar (o compilador os chama) → NÃO se escreve teste
afirmativo pra eles. Escrever SÓ:
- **Rejeição (path que o self-build nunca dirige):** `type str = i32` num arquivo de USUÁRIO
  (ns não-base) → `EXPECT_COMPILE_FAIL` "type 'str' is reserved" (exercita
  `check_reserved_type_redefs` no caminho de FALHA). 1 oráculo.
- **Rejeição:** `s.metodo_inexistente()` sobre str → `no such method '…' on struct 'str'`
  (caminho de erro de `type_method_call`, que o self-build não dispara). 1 oráculo.
Nada afirmativo além disso (lei dura CLAUDE.md — reincidência proíbe testes de vez).
