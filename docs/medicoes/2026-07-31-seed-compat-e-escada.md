# O seed publicado, o literal nu e a escada podre — dois defeitos empilhados, ambos medidos

Encomenda: o run **30613192858** (`296079e0`, ramo `remodel/0.3.1.0-linux-native-2`) perdeu **nove
pernas de artefacto**. A hipótese de partida era que o seed publicado não propaga o tipo esperado
para dentro dos braços de um `match`. **A hipótese está CONFIRMADA, e a confirmação é uma medição,
não uma leitura de código** — mas a confirmação exigiu fabricar primeiro um oráculo, porque o binário
que a caixa tem **não é** o seed publicado.

## 0. O instrumento: uma RÉPLICA funcional do seed publicado

A rede de *releases* não é alcançável desta caixa (`gh api repos/.../releases` → **403**,
`api.github.com/.../releases` → **403**). O binário em `~/.teko-seed` **reporta** `0.3.0.31-beta`,
mas **não é** o seed publicado: construído contra a ponta, o seu checker passa `8595/8595` itens sem
uma queixa, e o sítio que o CI acusa nunca falha.

O que é o seed publicado, então? A *tag* mais recente que não é *nightly* é **`v0.3.0.31-beta`**, e
aponta para **`4e6c4e4bee787ce6ea19d80a8f7ca7359c9d5b4d`** (2026-07-28). Esse commit **é** o
`merge-base` de `HEAD` com `origin/main`.

**O oráculo:** a árvore de `4e6c4e4b`, construída localmente, dá um compilador cujo comportamento é
determinado pela SUA fonte — que é exactamente a fonte do seed publicado.

```
teko . -o .replica-out --no-verify --release      (em .replica-wt @ 4e6c4e4b)
  → built .replica-out/teko    76,9 s    pico 1476,5 MB
  → .replica-out/teko --version → teko 0.3.0.31-beta
```

## 1. DEFEITO 1 — o seed recusa a lane. `src/build/project.tks:5873`

A réplica contra a ponta, com a invocação **exacta** do CI
(`scripts/build_with_seed_fallback.sh:171`, `teko . -o OUT --no-verify --release`, `TEKO_BACKEND=c`):

```
checker  6317/6317 items  11,7s  ✗
  src/build/project.tks:5873:4: the function's final expression does not match its declared return type
teko: .: src/build/project.tks:5873:4: the function's final expression does not match its declared return type
```

**Byte a byte o diagnóstico do CI.** O sítio:

```teko
fn stage_rc_of_env(key: str): i32 {
    match teko::env::var(key) { str as v => bp_parse_uint(v) to i32; error => 0 }
}
```

`bp_parse_uint` (`src/build/project.tks:5056`) devolve `u64`; o braço `str` vale `i32` pelo cast, e o
braço `error` é um literal nu que **tipa `i64` por omissão**. A junção vale `i32 | i64`.

### A causa, com o commit que a fecha

A capacidade que falta tem nome e data. `src/checker/typer.tks` ganhou, **depois** do commit de
lançamento do seed:

| símbolo | ficheiro | commit | data |
|---|---|---|---|
| `match_arm_literal_leaf` / `match_join_anchor` / `adopt_match_arm_literals` | `src/checker/typer.tks:3877-3928` | **`c64178e9`** | 2026-07-29 |
| `array_elem_bare_literal` / `array_join_anchor` / `adopt_array_elem_literals` | `src/checker/typer.tks` | `1cfe5d83` | 2026-07-29 |

`git show 4e6c4e4b:src/checker/typer.tks | grep -c 'adopt_match_arm_literals\|match_join_anchor'`
→ **0**. Na ponta → **8**. E `git merge-base --is-ancestor 4e6c4e4b c64178e9` → o lançamento
**precede** a correcção.

O próprio doc-comment de `adopt_match_arm_literals` nomeia o sintoma que o CI viu:

> *…so `match k { i32 as n => n; null => 0 }` joins to `i32`, not the accidental `i32 | i64` a
> bare-defaulted literal would otherwise force into the union.*

### A correcção, e a varredura que a fecha

`src/build/project.tks:5874` — **uma anotação, nada mais**:

```teko
match teko::env::var(key) { str as v => bp_parse_uint(v) to i32; error => 0 to i32 }
```

**A varredura por OUTROS sítios não foi um `grep` — foi o oráculo.** Com a anotação no sítio, a
réplica do seed publicado constrói a **ponta inteira**:

```
codegen 5987/5987 ✓ · emit C 10,0 MB · cc 60,8 s
built .tip-by-replica/teko   81,2 s   pico 1600,2 MB
```

Um checker que reporta **todos** os diagnósticos que encontra (não pára no primeiro) não encontrou
mais nenhum. **Literais nus em `if`-valor, em cauda de bloco e em `return` estão portanto varridos
também** — por construção, não por padrão textual.

### Porque é que os OUTROS literais nus da árvore não são defeito

A regra não é *"literal nu em braço de match"*. Medido sobre `src/**/*.tks`, essa regra ingénua dá
**29 ocorrências**, todas legítimas:

* `src/checker/type.tks:65` `U8 => 8; U16 => 16; U32 => 32; U64 => 64` numa função `-> u32` —
  **todos** os braços são literais nus, não há âncora concreta com que discordar, a junção fica
  literal e adopta a anotação no sítio de uso. O seed constrói-a;
* `src/io/stream.tks:319` `let base: i64 = match whence { Start => 0; Current => self.pos to i64; … }`
  — a âncora é `to i64`, que **é** o tipo por omissão do literal nu. Não há divisão.

A forma hostil é a **MISTURA**: um braço de literal nu ao lado de um braço cuja cauda é um cast
**estreitante** (`to i8/i16/i32/u8/u16/u32/u64/f32/byte` — nunca `to i64`/`to f64`). Sob essa regra,
sobre a árvore real:

| árvore | ocorrências |
|---|---|
| antes da correcção | **1** (`src/build/project.tks:5874`) |
| depois da correcção | **0** |

Zero falsos positivos em `src/**/*.tks`, com varredura ciente de várias linhas e de literais.

### O guarda que a fixa

* `src/build/fixture_guard.tks` — `seed_hostile_arms_under` e as suas folhas
  (`code_only_text`, `match_arm_values`, `is_bare_number`, `ends_with_narrowing_cast`,
  `seed_hostile_match_body`, `seed_hostile_arm_lines`).
* `src/build/fixture_guard_test.tkt` — `compiler_sources_carry_no_seed_hostile_match_arm` varre
  `src/` no nível **unitário**, que toda a lane paga antes de construir uma fixture; mais sete casos
  sintéticos, com a forma medida byte a byte **e a sua inversão**.
* `examples/regressions/seed_literal_arm/` — a metade comportamental: as duas grafias, a anotada e a
  adoptada, valem o mesmo na mesma largura (`Then exit = 123`).

## 2. DEFEITO 2 — a escada. `scripts/build_with_seed_fallback.sh:539`

```
LADDER_RUNGS="71c763d0ccec64df9fcd6c285a6782c642254e38 071c9c172f70c4fec5ff495e285cfc9cdef97fcb"
```

Os dois degraus são de **2026-07-24** e declaram `version = "0.3.0.30"`. O comentário no guião diz-o
por extenso: foram descobertos para **o seed 0.3.0.30**.

### A medição

Degrau 1 (`71c763d0`) construído com a réplica do seed publicado:

```
✗ src/build/init.tks:282:58: the `T?` nullable sugar has been removed — write 'T | null' instead
✗ src/time/time.tks:86:15:  type 'i128' was removed (0.3.1)
… 124 diagnósticos de tipo/açúcar removidos, em ~40 ficheiros
   (isel_arm64, isel_riscv, minst_oracle, stackify, progress, borrow, collect,
    comptime_fold, consteval, di, initanalysis, …)
   + unknown type: Tkr / TkrFeature / TkrMatch / Type
```

**Não é um pino marginalmente atrasado: é uma ERA inteira atrás.** O seed subiu de `0.3.0.30` para
`0.3.0.31-beta` e o `0.3.1` removeu `i128`/`u128`/`f16` e o açúcar `T?`; os degraus ficaram do lado
errado dessa remoção. O defeito é **latente** — só aparece quando o seed falha, e o seed só falhou
por causa do defeito 1.

### É refrescável daqui? NÃO — e a razão não é a rede

A rede de `git` **é** alcançável (o `origin` responde; `ensure_full_history` só precisa de
`git fetch origin main`). A descoberta falha por uma razão **estrutural**, e é esta:

```
git merge-base HEAD origin/main
  → 4e6c4e4bee787ce6ea19d80a8f7ca7359c9d5b4d
  → exactamente o commit de lançamento do seed (tag v0.3.0.31-beta)
```

A descoberta (`scripts/build_with_seed_fallback.sh:646-690`) procura **o ancestral primeiro-pai mais
NOVO em-ou-antes do `merge-base`** que o compilador corrente consegue construir. Com o `merge-base`
**igual ao commit do próprio seed**, todo o candidato é o seed ou mais velho que ele:

* um degrau **em** `4e6c4e4b` dá um compilador com **exactamente** a capacidade do seed — zero ganho,
  e a etapa seguinte volta a falhar na ponta;
* qualquer degrau **anterior** cai na parede pré-`0.3.1` medida acima (os 124 diagnósticos), até
  esgotar `MAX_PROBES=64` ou disparar a guarda de não-progresso.

Acrescenta-se que a descoberta **nem sequer arranca** quando o seed constrói a ponta: o guião sai no
caminho rápido antes de lá chegar (`:484-488`), e com a correcção do defeito 1 o seed **constrói**.

**Conclusão, com evidência: a escada não tem nada para subir nesta lane.** Não porque o pino esteja
mal escolhido, mas porque a distância entre o seed e a ponta deixou de ser transponível por um
ancestral — o `main` já *é* o seed. **Não inventei pino.** As duas saídas honestas, ambas decisão do
dono e nenhuma delas desta encomenda:

1. **manter a ponta construível pelo seed** (é o que o defeito 1 restaura, e o que o guarda unitário
   passa a garantir de antemão) — enquanto isso valer, a escada nunca engata;
2. **lançar um seed novo** a partir de um commit ≥ `c64178e9` e só então repinar; um pino cujo commit
   seja ANTERIOR ao commit de lançamento do seed é, por construção, veneno — foi essa a invariante
   que faltou e que produziu este defeito.

## 3. Armadilha do harness confirmada por leitura

`tkr_ensure_built` compila um projecto **uma vez por `.tkr`**, com o env do PRIMEIRO cenário: a chave
da fatia de projecto (`regr_built_serves`, `src/build/regression.tks:2196`) **ignora o env**, ao
contrário da chave de fonte (`regr_src_key`, `:2059`). `examples/regressions/seed_literal_arm/` traz
por isso **um só cenário e uma só rota**, e o `.tkr` di-lo no cabeçalho.

## 4. Auditoria M.3 do registo de regressões

`teko.tkp:57` — 14 registos, 13 directórios em `examples/regressions/`:

```
registos: 14 · inexistentes: [] · dirs: 13 · registados: 13
dir sem registo: [] · registo sem dir: []
```

Todos os registos apontam para dentro da árvore e todos os directórios estão registados.
