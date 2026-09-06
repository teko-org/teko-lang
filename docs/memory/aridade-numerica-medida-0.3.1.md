# Aridade numérica — o que o compilador FAZ hoje, medido (2026-07-30)

O dono pediu um teste e deu a régua:

> Tamanhos menores devem caber em tamanhos maiores e de mesma aridade [...] o mesmo vale para
> floats e para bigint e decimal. Fazendo pânico somente se ocorrer (em runtime) overflow

O trecho que ele escreveu, com as anotações dele:

```teko
mut a: i32 = 1000
mut b: i64 = 1
b += a // deveria funcionar
b *= a // deveria funcionar
b = a  // deveria funcionar
a += b // panico
```

## A medição, uma linha por build, com `teko 0.3.0.31-beta`

| caso | hoje |
| --- | --- |
| `b += a` (i64 += i32) | **REJEITA** — `assigned value does not match the target type` |
| `b *= a` (i64 *= i32) | **REJEITA** — mesma mensagem |
| `b = a` (i64 = i32) | **REJEITA** — mesma mensagem |
| `a += b` (i32 += i64) | REJEITA em compilação, não em runtime |
| `u32 → u64` implícito | REJEITA |
| `i32 → bigint` implícito | REJEITA |
| `i32 → dec` implícito | REJEITA |
| `f32 → f64` implícito | REJEITA |
| `f32 → f64` **explícito** (`to f64`) | **REJEITA — a conversão entre floats não existe** |
| `f64 → f32` explícito | REJEITA |
| `i32 → i64` explícito (`to i64`) | funciona, valor correto |
| `i64 → i32` explícito, `b = 2^40` | **`a = 0`, calado** |
| `u64 → u8` explícito, `b = 300` | **`a = 300`, calado** |

Não há conversão numérica implícita em NENHUMA direção. As três linhas que o dono espera que
funcionem são as três que o compilador recusa.

## Os dois achados calados, e o segundo é pior

`i64 → i32` com 2⁴⁰ dá `0`. É truncamento aritmeticamente correto (2⁴⁰ & 0xFFFFFFFF == 0) mas
**silencioso**, o que a régua do dono proíbe: pânico só em overflow real, e este É overflow real.

`u64 → u8` com 300 dá **300**. Isto não é truncamento errado — é um `u8` a guardar um valor que o
tipo não pode representar (o truncamento correto seria 44). O cast é um **no-op** ali. A corrupção
não fica contida no `print`: qualquer coisa que depois dependa da largura declarada — um campo de
struct com layout de 1 byte, um `memcpy`, um índice — lê outra coisa.

Os dois são a classe que esta lane persegue desde o início: **resposta errada calada**, pior que
paragem barulhenta.

## O pedido parte em duas naturezas

**REPARO, sem decisão pendente.** Um `u8` não pode conter 300, e um estreitamento não pode produzir
valor errado em silêncio. A régua já foi dada: panicar **só** se houver overflow em runtime.

**FEATURE, já decidida pelo dono.** Alargamento implícito dentro da mesma família: `i32 → i64`,
`u32 → u64`, `f32 → f64`, e para `bigint`/`dec`. Note que `f32 → f64` não é só "falta o implícito" —
falta a conversão inteira, porque nem o `to f64` existe.

## A ambiguidade que eu levantei, e como a tratei

No trecho do dono, `a += b` com `b == 1000` dá 2000, que **cabe** num i32. Pela frase final
— *"pânico somente se ocorrer overflow"* — essa linha **não deve panicar**. Tratei o `// panico`
dele como ilustração da DIREÇÃO (estreitamento), não do valor. Se a intenção for panicar sempre que
a direção estreite, independente do valor, o desenho muda e isto tem de ser renegociado.

## Prior art no próprio compilador, que quem implementar deve ler ANTES

`--arith-cast-gate` já existe (`src/checker/typer.tks:2022`, `run_arith_cast_gate` em
`project.tks`, `arith_cast_rate_*` em `src/checker/metrics.tks`): há uma métrica que conta
expressões aritméticas e quantas levam conversão. A feature nova deve alimentar esse medidor, não
passar por fora dele.

`teko::math::checked_*` também já existe, com testes por largura (`checked_u8_edges`,
`checked_i64_edges`, ...): a aritmética verificada existe como biblioteca. Falta o operador de
conversão usar a mesma honestidade.

---

# Confirmação, correção e entrega (2026-07-30, ao implementar)

Medi de novo, no mesmo compilador `0.3.0.31-beta`, e **agora nas duas rotas**, que é a diferença que
muda tudo. A rota C escolhe-se com `TEKO_BACKEND=c`.

## O que se confirma da medição acima

Os dois valores errados calados existem, exatamente como descritos, e o `u8` guarda mesmo 300 — não é
artefacto do `print`: `if a == 300` dá verdadeiro e `if a == 44` dá falso. A ausência de conversão
numérica implícita em qualquer direção confirma-se, e as três linhas que o dono espera que funcionem
eram exatamente as três recusadas.

## O que se REFUTA, e é a correção que importa

**Os valores errados eram da rota NATIVA, só dela.** A rota C já panicava, e posicionada:

```
2:13: teko: deliberate panic: impossible conversion
```

A tabela acima mediu uma rota e leu-a como o compilador. Medido lado a lado, a lista de divergências
é maior do que os dois casos e todas são bug do nativo pela régua do oráculo:

| caso | nativo (antes) | rota C (sempre) |
| --- | --- | --- |
| `u64 300 to u8` | **300** | pânico |
| `i64 2^40 to i32` | **0** | pânico |
| `i64 2^40 to u64` | **0** | 1099511627776 |
| `i64 -5 to i8` | **4294967291** | -5 |
| `i64 -1 to u32` | **4294967295** | pânico |
| `u64 44 to u8` | 44 | 44 |

A causa é uma só: `lower_cast` emitia `Trunc`, e `Trunc` seleciona um **mov de 32 bits** nos dois
isels — nunca chega à largura do destino, e apaga o sinal e a metade alta pelo caminho.

**`f32 → f64` explícito NUNCA foi recusado.** `cast_check` admite qualquer par numérico. A recusa que
a tabela registou vinha de `print(y)` a rejeitar um argumento float, uma linha depois do cast. O que
faltava era só o implícito. O que a rota nativa não tem é float NENHUM (`isel x86-64: B1-fp`), o que é
lane de outro agente, não uma conversão em falta.

**`teko::casting` não é inalcançável por um defeito de `pub`.** Medido: `teko::math::checked_add_u32`
está igualmente inalcançável de um projeto de utilizador. Nenhum módulo `teko::*` cruza para um
projeto externo — não há stdlib instalada para projetos de utilizador, só builtins injetados
(`print`, `teko::list::push`). Isso é uma decisão de produto do dono, muito maior do que esta lane, e
está reportada como adjacente. Dentro do projeto `teko` o módulo é alcançável, e é lá que a adoção
acontece.

## O que a lane entregou

1. **A guarda do cast nu na rota nativa** (`lower_cast_fit_guard`): verifica o intervalo do destino em
   runtime, panica com o MESMO `tk_panic_cast` que a rota C chama, e **não tem caminho de
   truncamento** — provado o ajuste, a conversão é a identidade e nenhuma instrução sobra.
2. **`teko::casting` com o seu primeiro consumidor**: `tkr_read_i32` lia o exit esperado de um `.tkr`
   com um `to i32` nu.
3. **Alargamento implícito** em cada sítio que declara tipo, com o conjunto sem perda do W-RULE — a
   MESMA predicada da aritmética. Espelha o C#, diverge do Rust de propósito, e a divergência está
   nomeada.
4. **A conversão entre floats**: o implícito `f32 → f64` passa a existir; o `f64 → f32` explícito
   segue IEEE-754 e está decidido e documentado em `cast-width-hygiene-0.3.1.md` §2.6.

Uma linha do trecho do dono continua a ser recusada em compilação, de propósito e reportada:
`a += b` (i32 += i64). É direção de ESTREITAMENTO implícito, o C# recusa-a igualmente, e a própria
frase do ruling novo põe a verificação em runtime no cast **explícito**. Não a decidi sozinho.
