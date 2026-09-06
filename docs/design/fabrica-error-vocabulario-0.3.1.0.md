# A fábrica `error::*` — o argumento que se sustenta sozinho: redução de vocabulário

> **Status:** MEDIÇÃO E DESENHO. Zero código de produto. **Não toca no degrau 22**, que está a ser
> implementado noutra branch; esta página não altera nenhum ficheiro que o implementador leia.
> **Base:** `cargo/0.3.1.0-degrau-22-desenho` @ `86efecf`.
> **Substitui** duas conclusões de `docs/design/degrau-22-error-layout-e-fabrica-0.3.1.0.md` §6.5 e
> §9.3, que mediram o eixo errado. As medições lá continuam válidas; as leituras delas não.

## Resposta de uma linha

**São 238 sítios de chamada e 6 assinaturas verbosas que enfiam ficheiro/linha/coluna ao lado de um
`error`, servidos por 13 nomes distintos.** A fábrica não acrescenta cobertura — colapsa 13 nomes em
3. E o campo `file` não tem zero utilizadores: tem **um utilizador que o contorna, 26 vezes**.

---

## 1. As duas leituras erradas, corrigidas

### 1.1 "`join` aterra sem chamador interno" — **falso; aterra com 7, sob dois outros nomes**

A medição estava certa (0 desembrulhos, 0 junções de pares **escritas como junções**). A conclusão
não. O `join` **já existe no produto**, partido em dois nomes e um nível de indirecção:

| o que faz | como se chama hoje | sítios |
|---|---|---|
| junta N diagnósticos numa `str` | `join_checker_diags([]str): str` | **3** |
| junta N diagnósticos num `error` | `diags_error([]str): error` | **4** |
| concatena duas listas de diagnósticos | `concat_diags([]str, []str): []str` | **31** |
| idem, outro nome | `append_diags(…)` | **3** |
| embrulha UM diagnóstico numa lista | `one_diag(str): []str` | **23** |

**64 sítios**, cinco nomes, para uma operação — compor diagnósticos. `diags_error` é literalmente
`error { message = join_checker_diags(diags) }`: a fábrica `join` **é** esta função, com o nome
curto e sem o desvio pelo `[]str`.

**Critério corrigido:** o valor não é cobertura funcional nova. É **redução de superfície de
nomes**. Sob esse critério o `join` é a peça mais bem justificada das três.

### 1.2 "`error.file` tem zero leituras e zero escritas" — **certo, e é exactamente o sintoma**

Zero leituras porque está **por implementar**, não porque seja inútil. A prova está na assimetria da
função que o dono apontou (`src/checker/diagnostics.tks:37-41`):

```teko
fn located_diag(file: str, decl_line: u32, decl_col: u32, inner: error): str {
    let line = if inner.line != 0 { inner.line } else { decl_line }
    let col  = if inner.line != 0 { inner.col }  else { decl_col }
    diag_at(file, line, col, inner.message)
}
```

`line` e `col` são **preferidos do erro** quando existem. `file` é **passado ao lado, sempre, sem
alternativa** — porque o erro não o carrega. **A assimetria é a medição.** O "ou deveriam" do dono
está confirmado: os três deviam vir do mesmo sítio, e dois já vêm.

Com o `file` escrito na origem, a assinatura colapsa de quatro parâmetros para um:

```teko
/**
 * located_diag — a linha de diagnóstico `file:line:col: texto` de um erro do checker.
 *
 * O erro carrega a sua PRÓPRIA origem (`error::new_pos`), por isso o renderizador só formata:
 * os quatro parâmetros anteriores (`file`, `decl_line`, `decl_col`, `inner`) existiam porque o
 * `error` não carregava o ficheiro e podia não carregar a posição. Deixa de haver preferência a
 * resolver e deixa de haver contexto a enfiar pela pilha abaixo.
 *
 * @param inner  o erro do checker a renderizar
 * @return       a linha `file:line:col: texto`
 */
fn located_diag(inner: error): str { … }
```

---

## 2. O número que justifica `new_pos` e `join` de uma vez

### 2.1 Sítios que enfiam ficheiro/linha/coluna AO LADO de um `error`

| forma | assinatura de hoje | sítios |
|---|---|---|
| posição derivada e **assada no texto**, nada anexado | `err_at(tokens, pos, msg): error` | **187** |
| posição passada a três, ao lado do erro | `located_diag(file, line, col, inner): str` | **18** |
| idem, reencaminhando `reg.file`/`decl.line`/`decl.col` | `decl_error_diag(reg, inner): str` | **8** |
| idem, e devolvendo `error` em vez de `str` | `surface_at(file, line, col, inner): error` | **8** |
| formatador de quatro parâmetros | `diag_at(file, line, col, msg): str` | **10** |
| construir e posicionar em dois passos | `err_loc(error { message = … }, n.line, n.col)` | **7** |
| **TOTAL** | | **238** |

Mais **6 assinaturas verbosas** (`err_at`, `located_diag`, `decl_error_diag`, `surface_at`,
`diag_at`, `surface_at`'s par em `typer.tks:6129`).

**Só no checker, excluindo os 187 do parser: 51 sítios.**

### 2.2 O vocabulário completo: **13 nomes, 357 sítios**

| nome | sítios |
|---|---|
| `err_at` | 187 |
| `concat_diags` | 31 |
| `one_diag` | 23 |
| `err_typed` | 22 |
| `located_diag` | 18 |
| `diag_at` | 10 |
| `err_loc` | 10 |
| `decl_error_diag` | 8 |
| `surface_at` | 8 |
| `diags_error` | 4 |
| `join_checker_diags` | 3 |
| `append_diags` | 3 |
| `error { message = … }` (o literal) | 928 |
| **13 nomes** | **1 255** |

A fábrica proposta pelo dono é **`new`, `new_pos`, `join` — três.**

### 2.3 As duas provas mais duras

**Prova A — a posição escrita DUAS VEZES.** `src/checker/initanalysis.tks:193-194`:

```teko
let msg = diag_at(file, b.value.line, b.value.col, $"unused local `{sn.name}` — …")
return teko::error::err_loc(error { message = msg }, b.value.line, b.value.col)
```

`b.value.line`/`b.value.col` aparecem **duas vezes na mesma expressão**: uma assada no texto, outra
anexada à estrutura. `surface_at` (`typer.tks:6132-6133`) faz o mesmo. Com `error::new_pos` é **uma
chamada, uma vez, e o renderizador formata no fim**.

**Prova B — `file` enfiado por três níveis de recursão só para render.**
`src/checker/initanalysis.tks:175/185/213`:

```
check_locals(stmts, fbody, file)  →  check_local_stmt(s, fbody, file)  →  check_locals_expr(e, fbody, file)
```

`file` não participa em análise nenhuma. Existe nessas três assinaturas **apenas** porque, três
níveis abaixo, um `diag_at` precisa dele e o `error` não o pode carregar. O mesmo padrão em
`typer.tks:5954`, `:6031` e no `with_file(env, …)` de `scope.tks:132`.

### 2.4 Achado adjacente — REPORTADO, não convertido em issue

**Os 187 erros do parser carregam a posição só como TEXTO.** `err_at`
(`src/parser/cursor.tks:22-29`) devolve `error { message = $"{line}:{col}: {msg}" }` e **não chama
`err_loc`** — logo `.line` fica a **0** em todos eles. Não é bug vivo (os diagnósticos do parser
seguem por `parse_diags: []str`, `project.tks:326`, e nunca chegam a `located_diag`), mas **são duas
formas de posicionar um erro no mesmo compilador**: o parser assa no texto, o checker anexa à
estrutura. Um único `error::new_pos` unificaria as duas — e é o maior bloco isolado dos 238.

---

## 3. A síntese, e o que o degrau 22 passa a significar

> **Os dois pontos do dono são o mesmo ponto: a API é verbosa porque o `error` é pobre.**

`located_diag(file, decl_line, decl_col, inner)` tem quatro parâmetros onde devia ter um; `file`
atravessa três níveis de recursão para chegar a um formatador; a posição é escrita duas vezes na
mesma expressão. **Nada disto é um defeito de quem escreveu essas funções** — é a única forma
possível quando o valor que viaja não carrega a sua própria origem.

**Isto reenquadra o degrau 22 e reforça-o.** Crescer o layout deixa de ser só *"fechar a divergência
entre a rota C e a nativa"* e passa a ser **a pré-condição para a API encolher**. Os `line`/`col`
que o degrau 22 põe no layout são o que torna `err_loc` útil de facto; o `file` que ele põe ao lado
é o que torna `new_pos` possível e o que apaga o parâmetro `file` de 26 assinaturas e de três níveis
de recursão.

**Nada disto altera o plano de crumbs do degrau 22, nem uma linha.** O degrau 22 já põe os seis
campos, já zera os cinco não construídos e já baixa `err_loc`/`err_typed`. Esta página só nomeia o
que isso destranca a seguir.

---

## 4. Recomendação actualizada

| peça | recomendação anterior | **recomendação actualizada** | porquê mudou |
|---|---|---|---|
| `error::new(msg)` | fazer | **fazer** | inalterado — substitui as 928 formas literais, com o literal a permanecer |
| `error::new_pos(msg, line, pos, file)` | fazer | **fazer, e é a peça de maior valor das três** | 238 sítios e 6 assinaturas colapsam; `file` ganha o seu primeiro escritor; a dupla escrita da posição desaparece |
| `error::join(a, b)` achatado | *"aterra sem chamador interno"* | **fazer** — tem **64 sítios sob 5 nomes** | o critério era cobertura; o critério certo é vocabulário |
| campo `file` no layout | *"zero leituras, existe só por paridade"* | **é o campo que destranca 26 assinaturas** | zero leituras era o sintoma, não a causa |
| `error::wrap(inner, ctx)` | reportado como a forma que o corpus pede 89× | **continua reportado, e continua fora do âmbito** | é decisão do dono acrescentar ou não; a medição não mudou |

**A `join` continua achatada.** A decisão do dono mantém-se intacta: as 5 formas que ela substitui já
são todas achatadas (`join_checker_diags` junta por `\n`), logo o achatamento **não perde nada que
exista hoje**. E como nenhuma delas guarda a lista, não há desembrulho a preservar.

---

## 5. Agendamento — a leitura do coordenador está certa

**Não muda.** Confirmo, com um reforço:

```
degrau 22 (em curso)
  → fábrica error::new / new_pos / join      ← ganha justificação PRÓPRIA
  → [promoção]
  → lane 1: teste de tipo (errors.As)
  → [decisão do dono: o arco continua?]
```

O que muda é **de que é que a fábrica depende para se justificar**:

- **Antes:** a fábrica era arrumação de superfície, e o seu melhor argumento — *"esconde a
  representação para o dia em que o `error` for interface"* — dependia de um arco de quatro lanes
  que fica para depois da promoção.
- **Agora:** a fábrica justifica-se por **238 sítios e 13 nomes** que colapsam **hoje**, sem
  interface, sem `errors.As`, sem reflexão. **A justificação deixou de estar refém do arco.**

A única dependência que resta é a que já estava medida e não mudou: **`error::new_pos` é
inimplementável no backend nativo antes do degrau 22**, porque escreve `line`/`col`/`file` e esses
campos ainda não existem no layout nativo. **Degrau 22 → fábrica**, por essa ordem, e por essa única
razão.

E fica registada a consequência de agenda: a conversão dos 928 literais continua **opcional** e
continua a pagar um ciclo de semente (não há `bootstrap/DEGRAU` declarado — a semente é o binário
publicado). As 238 chamadas verbosas, essas, **não pagam ciclo nenhum**: são chamadas a funções
internas do compilador, não superfície de linguagem, e podem colapsar assim que a fábrica exista.
**É aí que está o ganho a curto prazo, e é maior que o dos 928.**
