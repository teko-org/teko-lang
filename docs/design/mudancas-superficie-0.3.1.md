---
section: design
created: 2026-08-10
status: DESENHO — especificação das mudanças de SUPERFÍCIE da onda 0.3.1 (front + FFI). Companheiro do
        arena-especificacao-unica-0.3.1.md (o backend/arena). Consolida os §2–§7c do master
        lang-evolution-0.3.1-memory-and-surface.md numa ficha por mudança.
source: rulings do dono (var; ->:; self/base/static; Marshall; DI; aposentar unsafe; size/usize;
        sobrecarga) + lang-evolution-0.3.1-memory-and-surface.md
---

# Mudanças de superfície — o que sai, o que entra, o que resolve (0.3.1)

> **Estas são as mudanças de front-end e FFI da onda 0.3.1** — as que tocam gramática e assinatura, e
> por isso **obrigam reseed**. Pela ordem do dono, elas vêm PRIMEIRO (superfície → reseed), e só depois
> o backend faz o novo tratamento de arena (`arena-especificacao-unica-0.3.1.md`). Cada mudança abaixo é
> uma ficha: o que muda, o que sai, o que entra, o que resolve, com exemplo antes→depois.

**Disciplina de migração (aditivo primeiro, sweep depois):** cada mudança entra em uma janela
**aditiva** (a gramática nova é aceita AO LADO da velha, escrita na grafia velha para o seed atual
parsear), o reseed captura um seed que fala a grafia nova, e só então o **sweep** reescreve o `src/` e
remove a grafia velha. Isso mantém o bootstrap sempre construível.

---

## 1. `let`/`mut` → `var` — todo local é mutável

**O que muda.** Existe um único kind de binding local: `var`. `Const` permanece (é outro eixo — a
história de comp-time). `let` e `mut` deixam de ser kinds distintos.

**O que sai.** `BindKind::Let` e `BindKind::Mut` colapsam num só. Os três portões de intenção
(`&x`/`ref x`/`mem::free` exigirem `is_mut`) viram sempre-passa. A rejeição de "reatribuir um `let`"
some.

**O que entra.** `var` como a keyword de local. `is_mut` vira sempre-verdadeiro para locais. **`let`/`mut`
NÃO ficam como no-op** (ruling do dono): são varridos para `var` e depois **erram** — superfície que não
existe mais, não uma grafia tolerada. (Durante a janela aditiva o parser aceita ambos só para o seed
construir o `src/` ainda-não-varrido; pós-sweep+reseed, `let`/`mut` são erro de parse.) CF3
(const-propagation) re-baseia em **fluxo-de-atribuição-única** (um local escrito exatamente uma vez É
imutável de fato — derivável, byte-preservante).

**O que resolve.** Remove um eixo de binding redundante. A segurança nunca vinha da keyword
(`ast-computed-arena-assessment` §4.8): UAF/overflow são da arena, aliasing é da exclusividade F1. Sob
DPS, a inicialização de destino é interna ao lowering (abaixo do portão de borrow do usuário), então
manter `let` obrigaria a ginástica de "`let a = fun()` é mutável por referência"; `var` a dissolve.
Parâmetros e binds de match **continuam imutáveis** (eixo B.21, separado — não é `let`).

**Exemplo.**
```teko
// antes
let a = 0
mut b = 1
b = b + a        // ok; reatribuir `a` seria erro

// depois
var a = 0
var b = 1
b = b + a        // ok
a = 5            // ok agora — todo local é mutável
const K = 42     // Const permanece: um eixo separado
```

---

## 2. `->` → `:` no tipo de retorno — um operador de tipo só

**O que muda.** O tipo de retorno de função/closure usa `:`, o **mesmo** operador que já anota o tipo de
uma variável. Não há mais dois operadores de anotação de tipo.

**O que sai.** O token `Arrow` (`->`) — do lexer e do `token.tks`, no sweep final.

**O que entra.** Na janela aditiva, o parser aceita **`Arrow` OU `Colon`** no retorno (grafia velha
ainda parseia). No sweep, `src/` migra para `:` e o `->` deixa de parsear.

**O que resolve.** Equaliza a linguagem: se `x: T` define o tipo de `x`, `fn f(): T` define o tipo de
`f`. Um operador, uma regra.

**Exemplo.**
```teko
// antes
fn soma(a: i64, b: i64): i64 { return a + b }
var f = fn(x: i64): i64 { return x * 2 }

// depois
fn soma(a: i64, b: i64): i64 { return a + b }
var f = fn(x: i64): i64 { return x * 2 }
```

---

## 3. Remover `-> ref T` — retorno por referência sai

**O que muda.** Não existe mais retorno tipado-referência. `ref` sobrevive **só em parâmetros**.

**O que sai.** O arm de tipo de retorno `Reference` e seu cluster de portões (`check_ref_return_passdown`
e cia., ~5 funções de checker + 2 call-sites + 1 diagnóstico) e a rota de lowering do ref-return.

**O que entra.** Nada novo — é uma **remoção**. `ref` fica exclusivamente um dispositivo
caller→callee (borrow write-through, ex. `grow_inplace(ref []T)`).

**O que resolve.** Sob DPS todo retorno de agregado já aterrissa na storage do caller, então um retorno
por referência é redundante — o caller já tem o valor onde queria. Uso em produção: **zero** (só uma
probe e duas regressões de rejeição). Escape passa a ser uniformemente por DPS; borrow por F1 — dois
mecanismos ortogonais em vez de `ref` cavalgando os dois.

**Exemplo.**
```teko
// antes (vestigial — identity pass-down)
fn hand_back(ref ch: Ch): ref Ch { return ch }

// depois — o form não parseia; o valor já vem na arena do caller por DPS
fn hand_back(ch: Ch): Ch { return ch }
// `ref` só em parâmetro, para borrow:
fn grow(ref buf: []i64, v: i64) { push(ref buf, v) }
```

---

## 4. `self` / `base` / `static` — receptor e fábrica

**O que muda.** Métodos referenciam o objeto por `self` (keyword, receptor sintético — não um parâmetro
solto). `base` referencia o método da super numa sobrescrita. `static` marca uma fábrica/membro de tipo
(sem receptor).

**O que sai.** O receptor solto (o primeiro parâmetro untyped que hoje faz de `self`) e o
`allow_untyped_first` — no sweep.

**O que entra.** `self` sintético; `static` e `base` como **keywords reservadas** (ruling do dono: se
`base` vira keyword, não fica também como nome de local — "mesma coisa" de `mut`). Os poucos sítios de
produção que usam `base` como local (`driver.tks`, `resolve.tks`, `zlib.tks`) são **varridos** para outro
nome no sweep. `base` = o super-receptor numa sobrescrita. Um parâmetro de usuário chamado `self` é
rejeitado.

**O que resolve.** OOP explícito e consistente: o receptor é uma keyword, não uma convenção posicional; a
super é nomeável; membros de tipo (fábricas) têm marca própria. Ctor de `service` (DI) usa `static`.

**Exemplo.**
```teko
// antes (receptor solto)
fn area(c) { return c.raio * c.raio * 3 }

// depois
type Circle = struct { raio: i64 }
fn Circle::area(): i64 { return self.raio * self.raio * 3 }
static fn Circle::unit(): Circle { return Circle { raio: 1 } }   // fábrica, sem receptor

fn Sub::render(): i64 { return base.render() + 1 }               // `base` = super, contextual
var base = 7                                                      // ainda um local válido
```

### 4.1 Construção — nominal `Tipo { … }` e target-typed `.{ … }`; `self { }` sai (ruling do dono)

Há **duas** formas de construir um valor de struct:

- **Nominal** — `Tipo { campo = valor }`: sempre válida, o tipo é escrito.
- **Target-typed** — `.{ campo = valor }`: um `.` **unário prefixado** (sem receptor à esquerda) que
  constrói **o tipo esperado do contexto**. Exige **tipo-alvo conhecido na declaração** — retorno anotado
  (incl. `static fn (): self`), variável anotada (`var x: Tipo = .{ … }`), parâmetro, campo. **Sem
  tipo-alvo → erro** (`var x = .{ … }` é ilegal). Parse inequívoco (`.{` não colide com `..`/`.5`).

**`self { }` como construtor é RETIRADO.** `self` fica **só como receptor** (métodos de instância) e como
**tipo** (`(): self`), nunca como construtor. Quem construía o próprio tipo numa fábrica usa `.{ }` (o alvo
é o `self`):

```teko
static fn Counter::zero(): self { .{ _n = 0 } }   // era `self { _n = 0 }` — agora target-typed
var c: Counter = .{ _n = 5 }                        // ok — alvo anotado
var d = .{ _n = 5 }                                 // ERRO — sem tipo-alvo
```

**Por que retirar `self { }`.** Ele produzia `Named` com o nome **bare** (`Counter`), enquanto o tipo `self`
resolve para o **canônico qualificado** (`ns::Counter`); `type_eq` compara `Named` por string exata e não há
upcast de struct, então `self { }` falhava em todo tipo sob namespace (todo o corpus + a Intent). `.{ }`
constrói com o nome canônico do alvo **por construção**, dissolvendo o conflito — sem o conserto de
canonização que a via `self { }` exigiria.

---

## 5. Marshall — ponteiros opacos com `__wrap`/`__unwrap`

**O que muda.** O ponteiro deixa de ser genérico (`ptr<T>`) e vira **opaco atômico** (`ptr`/`uptr`), sem
aritmética. O acesso ao valor por trás é por marshalling explícito.

**O que sai.** `ptr<T>`/`Uptr {}` genéricos e seu handling. Aritmética de ponteiro
(`p + 1`, `p[0]`) é rejeitada.

**O que entra.** Dois tipos atômicos opacos `ptr`/`uptr` e **quatro funções — duas estáticas
(construtoras, infalíveis) e duas de instância (acessoras, falíveis):**
- `ptr::__unwrap<T>(ref T): ptr` e `uptr::__unwrap<T>(ref T): uptr` — **estáticas, infalíveis.** Pegam uma
  **referência a um valor** `T` e devolvem o **ponteiro opaco**. Infalível porque só tomam o endereço —
  **`__unwrap` devolve o PONTEIRO, não o valor**; não há nada a checar.
- `ptr.__wrap<T>(): T | error | null` e `uptr.__wrap<T>(): T | error | null` — **de instância, falíveis.**
  Do ponteiro opaco recuperam o **VALOR** `T`, checando liveness da arena + tag do tipo: `error` (arena
  morta / tag divergente), `null` (endereço 0).

**O que resolve.** Ponteiro cru seguro por construção: nada de aritmética, e o acesso passa por uma
checagem dinâmica (tag + liveness) no `__wrap`. É o que torna possível **aposentar `unsafe`** (§6) — o
`__wrap` supre a confiança que o `unsafe` dava por decreto. É também o handle honesto para FFI (embrulhar
um `ptr` estrangeiro).

**Exemplo.**
```teko
// antes
fn read(p: ptr<Node>): i64 { return p.value }   // deref genérico, confiança implícita

// depois — construir o ponteiro opaco a partir de um valor (estática, infalível → devolve o PONTEIRO)
var node = Node { value: 42 }
var p = ptr::__unwrap<Node>(ref node)            // : ptr

// recuperar o VALOR do ponteiro opaco (instância, falível → checa tag + liveness)
fn read(p: ptr): i64 | error {
    var n = p.__wrap<Node>()                     // : Node | error | null
    return match n { Node => n.value, error => n, null => 0 }
}
```

---

## 6. Aposentar `unsafe` e ponteiros crus

**O que muda.** A keyword `unsafe` e a contaminação por ela deixam de existir. Memória manual sai.

**O que sai.** `unsafe` (keyword + `is_unsafe` + contágio), memória manual (`mem::free`/`#must_free`/
`Arena` não-lexical/`RawBuf`/`Owned<T>`). Tudo no sweep, em passos: reclassificar as intrínsecas de
`teko::mem`/região como **seguras** → aposentar a memória manual → **deletar** a keyword `unsafe` por
último (quando nada mais a conter).

**O que entra.** Nada de keyword. O único resíduo de "confiança" é **FFI**: `extern fn` permanece, e a
fronteira estrangeira é cruzada embrulhando o `ptr` estrangeiro com `__wrap<T>()` (o honest-stop
dinâmico do §5). A `Arena` manual vira um **escopo** (região lexical); `mem::free` é obsoletado pelo drop
de região.

**O que resolve.** A segurança de memória é da arena (capacidade + tempo de vida) + F1, não de um selo
`unsafe` (`memory-unsafe-backend-remodel`). Com ponteiro opaco + `__wrap`, não há mais operação crua a
proteger — o selo perde função. Uma keyword a menos, e a promessa "isto é perigoso, confie" é substituída
por uma checagem real.

**Exemplo.**
```teko
// antes
unsafe fn poke(p: ptr<byte>) { mem::free(p) }

// depois — sem `unsafe`; região dropa no escopo; FFI embrulha o ptr estrangeiro
extern fn c_open(path: ptr): uptr
fn use_it(path: ptr): i64 | error {
    var h = c_open(path).__wrap<Handle>()   // confiança na fronteira = checagem, não decreto
    return match h { Handle => h.fd, error => h, null => -1 }
}
```

---

## 7. Injeção de dependência — `service`/`svc` com lifetimes de arena

**O que muda.** DI de primeira classe: classes de serviço com lifetime (`singleton`/`scoped`/
`transient`), resolvidas por **`svc<T: service>(key: str | null = null): T`** em comp-time (o constraint
`T: service` — §9.2b — garante que só serviços se resolvem; a `key` é opcional: sem chave resolve por tipo,
com chave constante desambigua/resolve por nome — inclusive `svc<Tx<T>>("chave")` do canal, §10.2). Os
lifetimes SÃO tempos de vida de arena (detalhe em `arena-especificacao-unica-0.3.1.md` §8).

**O que sai.** A DI por anotação da era anterior (`#singleton`/`#inject`, `src/checker/di.tks`
`choose_factory`) é substituída pela tabela estática nomeada.

**O que entra.** A keyword `service` (**sempre selado** — um serviço **não pode** ser `virtual` nem
`abstract`, então `sealed` é redundante e não se escreve; é o mesmo default do `class`, que só se abre com
`virtual`/`abstract`), os lifetimes `singleton`/`scoped`/`transient`, o ctor
`static`, `svc<T: service>(key: str | null = null): T` como **intrínseco de comp-time** (o compilador
substitui o call-site inline por código por-lifetime; sem ABI de runtime) — **chave ausente = `panic`**, e há
**`has_svc<T: service>(key): bool`** para checar antes. Chaves de string desambiguam múltiplos provedores de uma
interface — e a política de conflito é dura (ruling do dono): **se já há um registro do mesmo tipo sob a
mesma chave, é ERRO DE COMPILAÇÃO** (chaves distintas coexistem; mesmo-tipo-mesma-chave colide em
comp-time, nunca "último vence" silencioso). **Regra de escape:** valor de serviço nunca armazenado em campo, passado como argumento, ou
retornado em código de usuário — forçando o `claim` explícito via `svc<T: service>(...)`; o backend é isento mas o
que segura fica arena-bounded.

**O que resolve.** Resolução determinística em comp-time (tabela estática por análise), sem custo de
runtime, e um modelo de lifetime que casa com a arena em vez de brigar com ela. A regra de escape impede
que um serviço vaze para além da sua arena. **Sob threads** (ruling do dono 08-10), a resolução é **por
thread**: `singleton` vive na raiz da THREAD (não do programa), `scoped` na sub-raiz da thread — a faceta
de arena está no Doc 1 §8. É o "considerar DI no front e no back": a superfície é a mesma, o tempo de vida
é o da thread.

**Exemplo.**
```teko
service Clock singleton {
    static fn ctor(): self { return Clock { } }
    fn now(): i64 { return 0 }
}

fn tick(): i64 {
    var c = svc<Clock>()      // resolvido em comp-time: singleton → slot na raiz
    return c.now()
}

// rejeitado (regra de escape):
// var g; g = svc<Clock>()          // store proibido
// fun(svc<Clock>())                // passar como argumento proibido
```

---

## 8. `size`/`usize` — tipos de palavra de máquina

**O que muda.** Entram `size` (com sinal) e `usize` (sem sinal), os tipos de largura-de-palavra da
máquina. Posições de memória/coleção passam a usá-los.

**O que sai.** Nada por remoção; é aditivo. No sweep, posições `u64` de memória/coleção são reeboladas
para `usize`/`size`.

**O que entra.** `Size`/`Usize` no `PrimKind` + predicados + a tabela de lowering prim→tipo-de-máquina
(`Usize`/`Size` ⇒ `i64` em 64-bit). `usize` é um kind **distinto** de `uptr` (endereço opaco). Fica
**inerte** até ser usado — `src/` ainda diz `u64`, então byte-idêntico.

**O que resolve.** Correção de largura em posições de memória (`.len`/`.cap` de slice, índices, offsets,
tamanhos de arena, o header de slice, a maquinaria DPS/AL3) sem casts. Em 64-bit `usize == u64`, então o
reball é **byte-preservante** (é a prova de que `usize` baixa idêntico a `u64` no alvo). Posições de
fonte (`line`/`col`, `u32`) ficam intocadas.

**Exemplo.**
```teko
// antes
fn index(xs: []i64, i: u64): i64 { return xs[i] }

// depois — largura de máquina explícita; em 64-bit baixa idêntico a u64
fn index(xs: []i64, i: usize): i64 { return xs[i] }
```

---

## 9. Sobrecarga (método, operador, tipo) e tipos com métodos sobre primitivo/enum/flag

**O que muda.** Funções de mesmo nome com assinaturas diferentes (sobrecarga de método). Operadores com
comportamento definido pelo usuário (sobrecarga de operador — **comportamento**, não casting; nada de
"explicit/implicit operator"). E **tipos** de mesmo nome com aridade genérica distinta — `Intent` (opaco,
sem dado) e `Intent<T>` (carrega a cópia do dado) coexistem: é a razão do dono para overload servir também
a tipos, não só a métodos/operadores.

**O que sai.** A rejeição de "mesmo nome" hoje — relaxada para **distinção por assinatura de parâmetro**.

**O que entra.**
- **Método:** `select_overload` no caminho de chamada; sufixo de símbolo para conjuntos sobrecarregados.
  Inerte até existir um conjunto de sobrecarga.
- **Operador:** um branch de lookup de dunder por operador + o mapa operador→dunder + derivação
  automática de `__eq`/`__lt` para comparação. Inerte até um tipo definir um dunder; o caminho de prim
  fica byte-idêntico.
- **Tipo com métodos sobre primitivo/enum/flag** (o que dá aos operadores um lar): um `type` respaldado
  por um primitivo (ou `enum`/`flags`) pode carregar **métodos** — **estáticos**, **de instância
  (readonly)** e **operadores** — mas **NÃO tem campos** (o valor É o primitivo). Métodos são aceitos
  também em **`enum`** e **`flags`**. Sem corpo `{}`, o tipo **comporta-se como o próprio primitivo**
  (transparente — um alias/newtype sem superfície nova).

**O que resolve.** Ergonomia: mesma operação, nomes/tipos diferentes, sem inventar nomes distintos;
operadores que fazem sentido para tipos do usuário (um `Vec2 + Vec2`) com o comportamento definido pelo
autor — sem coerção implícita escondida; e **dar comportamento (métodos/operadores) a um primitivo, enum
ou flag** sem embrulhá-lo numa struct (nem pagar um campo).

**Exemplo.**
```teko
// sobrecarga de método
fn draw(p: Point) { }
fn draw(p: Point, cor: i64) { }        // mesmo nome, assinatura distinta

// sobrecarga de operador numa struct
type Vec2 = struct { x: i64, y: i64 }
fn Vec2::__add(o: Vec2): Vec2 { return Vec2 { x: self.x + o.x, y: self.y + o.y } }
var v = Vec2 { x:1, y:2 } + Vec2 { x:3, y:4 }   // usa __add

// tipo sobre primitivo, com métodos — SEM campos
type Celsius = i32 {
    static fn from_f(f: i32): Celsius { return (f - 32) * 5 / 9 }   // estático
    fn to_f(): i32 { return self * 9 / 5 + 32 }                     // instância (readonly)
    fn Celsius::__add(o: Celsius): Celsius { return self + o }      // operador
}
var t = Celsius::from_f(212) + 10        // usa __add; `t` É um i32 por baixo

type Meters = i32                        // sem corpo → comporta-se como i32 puro

// métodos em enum / flags
enum Dir { N, S, E, W } {
    fn oposto(): Dir { return match self { N => Dir::S, S => Dir::N, E => Dir::W, W => Dir::E } }
}
```

### 9.1 Valores default de parâmetro

**O que muda.** Um parâmetro pode ter valor default (`bounds: usize = 1`) — pedido antigo do projeto,
formalizado agora (é o que o `chan<T>::make(bounds: usize = 1)` usa). Uma chamada que omite o argumento
recebe o default.

**O que entra.** Sintaxe `nome: T = <const-expr>` na assinatura; o default é uma **expressão de
comp-time** (const), preenchida no call-site pelo checker. Só na **cauda** da lista de parâmetros (um
parâmetro com default não pode preceder um sem default). Interação com sobrecarga (§9): se uma chamada
fica **ambígua** entre uma sobrecarga e uma aridade preenchida por default, é **erro de compilação** —
mesma disciplina "conflito colide em comp-time" da DI.

**Exemplo.**
```teko
static fn chan<T>::make(bounds: usize = 1): self
var a = chan<i32>::make()      // bounds = 1 (default)
var b = chan<i32>::make(0)     // bounds = 0 (unbounded)
```

### 9.2b Uniões `|` — DECLARAÇÃO vs CONSTRAINT (dois `|` distintos); `variant` para tipo-soma nomeado

**O que muda.** A união `|` tem **duas posições de uso distintas**, e nunca é um `type` nomeado (ruling do dono):

1. **União de DECLARAÇÃO / retorno / CAMPO — estrutural, inline.** Vale em declaração de variável,
   parâmetro, retorno **e campo de struct**: `fn f(): T | error | null`, `struct { x: A | B | C }`. É
   composição estrutural no ponto de uso (o valor É um dos ramos). A extensão a **campo** (§9.D) é o que
   substitui os antigos `variant` nomeados: um campo que antes tinha tipo `Type` agora carrega a **união
   dos membros, escrita por extenso**.

   **Precedência `[]` > `|` — array de tipo-soma exige parênteses (ruling do dono).** O former de slice
   `[]` liga mais forte que a união `|`, então `[]A | B` parseia como `([]A) | B` (um array-de-A, OU um
   B). Um array **cujo elemento é a união** deve ser circundado: **`[](A | B | …)`**. Ex.:
   `members: [](Prim | Byte | … | Null)` (array da união) vs `[]Prim | Byte` (array de Prim, ou Byte).

2. **União de CONSTRAINT — de FORMAS, no bound de um genérico.** Um constraint é uma **disjunção de
   conjunções**: `<T: Alt1 | Alt2 | …>`, onde cada `Alt` é `Termo & Termo & …`. Os **termos** são:
   - **palavras de forma:** `class`, `service`, `struct` — forçam a *forma* do tipo. `service` **aceita um
     lifetime** (`service singleton` / `service scoped` / `service transient`), forçando também **como** o
     serviço vive;
   - **interfaces** (`Ifce`) e **tipos concretos** (`str`, …) — forçam conformidade/identidade;
   - **`notnull`** — o único termo que **só entra por `&`, nunca por `|`** (não é uma forma-alternativa, é
     um modificador): proíbe o genérico de ser nulo, **nem na definição**.

   ```teko
   <T: class & Ifce | struct & OutraIfce | str>   // T = (class que faz Ifce) | (struct que faz OutraIfce) | str
   <T: notnull>                                     // T não pode ser nulo (único uso de notnull sozinho)
   <T: class & notnull>                             // classe não-nula
   <K: service singleton & IChannelKind<T>>         // o constraint do transporte de canal (§10.2): service SINGLETON
   ```

   **Por que forçar o lifetime é honesto (ruling do dono).** O canal força `service singleton` no `make`
   (§10.2). Sem isso, o usuário poderia declarar um transporte `service transient` (uma instância nova a cada
   resolução — quebra o "um canal por chave em F2") e o compilador teria de **redirecionar deliberadamente**
   essa implementação errônea para singleton, escondendo o erro. Com o lifetime no constraint, um transporte
   que não seja `singleton` **falha na compilação** no ponto do `make` — falha honesta, não coerção silenciosa.

**O que resolve.** Separa o `|` estrutural (o valor é um dos ramos) do `|` de forma (o *tipo* tem uma das
formas), e dá ao genérico poder de exigir forma (`class`/`service [lifetime]`/`struct`), conformidade
(interface/tipo) e não-nulidade (`notnull`) — sem nunca virar um `type` nomeado. **Não há tipo-soma
nomeado** (ruling do dono, §9.D): `type X = variant …` sai e `type X = A | B` (alias estrutural) **continua
rejeitado** — a única forma-soma é a **união `|` estrutural inline**, escrita por extenso onde usada.
A **verbosidade da fonte é abreviável por MACRO** (Família A, §14.1: uma `macro Type()` cujo `lowering`
alarga para a união entre parênteses; usa-se `[]@Type()`) — açúcar **puramente sintático**, resolvido
*antes* do typecheck, então **não** reintroduz a nominalidade (depois da expansão só há a união literal
inline, nenhum `type` nomeado). O que continua **proibido** é o **alias/wrapper/newtype nominal**
(`type X = A | B`, `struct { case: … }`, `newtype`), que criaria um *tipo* com identidade. Ver **§9.D**
para a migração dos ~28 ADTs do compilador.

### 9.2 Tipos de closure — `func<…>` / `action<…>`

**O que muda.** O **tipo** de uma closure passa a ser um subtipo de comp-time, ao modo do C# — em vez da
forma antiga baseada em `fn`. A closure **literal** continua `(params) => expr` (ou `(params) => { … }`) —
**sem `fn`, sem `-> R`**, retorno inferido (já era assim, `parse_expr.tks:308`).

**O que sai.** A anotação de tipo `fn(T): R` / `(): T?` — que **colide** com uniões de retorno:
`fn(T): R | null` é ambíguo entre `(fn(T): R) | null` e `fn(T): (R | null)`.

**O que entra.** Dois construtores de tipo genéricos, **subtipos de comp-time** (monomorfizados, sem a
sobrecarga de um objeto delegate em runtime):
- **`func<T1, …, Tn, R>`** — recebe `T1…Tn`, **retorna `R`** (o último parâmetro de tipo é o retorno).
- **`action<T1, …, Tn>`** — recebe `T1…Tn`, **sem retorno**.

**O que resolve.** Declaração mais simples e **sem colisão**: `func<Record, str> | null` é inequívoco.

**Exemplo.**
```teko
var dobro: func<i32, i32>            = (x) => x * 2
var log:   action<str>              = (m) => print(m)
var fmt:   func<Record, str> | null = null           // sem colisão (era `fn(Record): str | null`, ambíguo)
```

### 9.3 Atribuição múltipla e decomposição de array — `var a, b = …` e `var [a, b] = arr` (sem tipo tupla)

**O que muda.** Entram a **atribuição múltipla** (vários alvos = vários valores, posicional) e a
**decomposição de array** (desempacotar um `[]T` em alvos posicionais), **sem tipo tupla**. Atribuição
múltipla é a base do `await` (§10.3) e dá **retorno múltiplo** de graça. (A decomposição por NOME
`{ x; y }` de campos de struct, B.13, continua.)

**O que entra.**
- `var a, b, c = e1, e2, e3` — liga posicional; **tipos inferidos por posição** (cada alvo pega o tipo do
  seu valor).
- `var a, b: T = e1, e2` — **um tipo só** anotado, compartilhado por todos os alvos.
- **Decomposição de array:** `var [a, b, c] = arr` — desempacota um **array `[]T`** (um tipo REAL) em
  ligações posicionais. **Fica** — é importante para trabalhar com arrays.
- **Retorno múltiplo:** `fn div(): (i32, i32)` (a assinatura lista os retornos); o chamador liga com
  `var q, r = div(17, 5)`. **Não há tipo tupla** — o `( )` só aparece na assinatura e no ponto de ligação,
  nunca como valor de 1ª classe (`var t = div(…)` não existe).

**O que resolve.** Ligar/retornar vários valores e desempacotar arrays sem struct nomeado nem tipo tupla —
sistema de tipos enxuto. A atribuição múltipla é a notação natural para várias tasks (§10.3), matando
`when_all`/`when_any` **e** o `await` de array (mas a decomposição de array em si fica).

**Exemplo.**
```teko
var e, f = 1, "abc"           // e : i32, f : str — tipos por posição (atribuição múltipla)
var g, h: i32 = 1, 2          // g, h : i32 — tipo compartilhado

var [x, y, z] = [1, 2, 3]     // decomposição de array []i32 (tipo real)

fn div(a: i32, b: i32): (i32, i32) { return (a / b, a % b) }   // retorno múltiplo (NÃO um tipo)
var q, r = div(17, 5)                                           // q = 3, r = 2
```

---

### 9.4 Traits (decorador achatável) — e a aposentadoria das *structural traits*

**Onda própria** (não embutida na implementação de §9/§10/§11). Uma `trait` deixa de ser "contrato de
assinaturas" e passa a ser **decorador**: achata-se no tipo que a compõe (nem sub, nem super objeto) e
**não é tipo**. Redefine o comportamento das traits.

**O que uma trait É — mixin concreto auto-contido:**
- **Só membros CONCRETOS** — **campos** (com tipo, default opcional), **métodos-com-corpo** (properties
  `get`/`set`, `static fn`, métodos de instância) e **`const`s**. **Zero membro abstrato/sem-corpo** — isso é
  papel da *interface*. (Campos **VOLTAM**: como tudo achata, um campo de trait é só um campo que achata no
  host — bani-los era preciosismo. Um `const` não é campo, é comp-time; sempre coube.)
- **Auto-contida:** todo `self.X` que um corpo referencia é **declarado na própria trait** (ou numa trait que
  ela **compôs**, abaixo). Uma trait apontando pra algo que **não declara/desconhece** está **visivelmente
  errada** → erro na própria trait, não na composição. Cai o antigo "contrato-sobre-host": o backing `_nome`
  do getter `nome` mora **na trait** (o getter lê `self._nome`, nunca a property — senão recursão).
- **Não é tipo:** sem `var`/parâmetro/retorno/campo/constraint tipado por trait, sem dispatch, sem
  trait-object. Reside só (1) na definição da trait e (2) no `&`-compose de um tipo.
- **`self` = o host que a compõe**, valor (`self._x`) e tipo (`static fn new(): self`).
- **`&` compõe, e traits compõem traits:** `trait C & A & B { … }` achata A e B em C — e C **conhece** A/B,
  logo pode referenciá-los (a auto-contenção estende-se ao que a trait compôs). Achata em **classe
  (selada/virtual/abstrata), struct e service**.
- **Traits carregam interface:** `trait T & IFoo { … }` = T implementa **IFoo inteira** (auto-contida); todo
  host que compõe T passa a **satisfazer** IFoo — é o **host** (tipo) que despacha, a trait não. Padrão
  parcial: uma trait pode só **contribuir** métodos sem declarar `& IFoo`; aí o **host** declara `& IFoo` e a
  composição inteira fecha o contrato (ex.: `NeByEq`). É o substituto **explícito** da structural aposentada
  (um `trait Equatable & IEq` / `Comparable & IOrd` escrito à mão, composto pra dar a capacidade).
- **Colisão = identidade estrutural, comparada pela AST.** Dois membros de mesmo nome ao achatar:
  **compara-se a AST dos membros conflitantes** (pós-parse — normaliza espaços/comentários, mas é
  estrutural: identificadores e ordem contam, `a+b` ≠ `b+a`, nome de parâmetro conta). **AST igual** →
  **absorve num só**; **qualquer diferença** → **erro de compilação**. Subsume o **diamante** (mesma trait 2× = idêntica =
  absorvida, **idempotente**) e integra **sobrecarga** (assinaturas diferentes = overload, coexistem; só
  entram na regra os de mesma assinatura). Resolve o micro-fork struct-vs-trait: redefinir um membro de trait
  com corpo diferente = **erro**, sem override silencioso. *Nota:* dois privados idênticos porém
  semanticamente distintos viram **um campo compartilhado** (absorção é estrutural, não sabe de intenção).
- **Sem `match` sobre trait:** trait não tem discriminante; nome de trait em *subject*/*case* de match =
  **erro nomeado**. Traits compostas idem.
- **Modelo mental:** mixin (estado + comportamento concretos que achatam), estilo trait-de-Scala/`partial` —
  mas **não é tipo** (não instancia, não referencia, não despacha por si; o **host** é que vira o tipo).

**A regra de desambiguação por AST é GERAL, não específica de trait.** Onde quer que a composição por `&`
**funda membros de mesmo nome**, aplica-se a mesma regra — **AST igual → funde num só; qualquer diferença →
erro**: **interface ∘ interface** (`IFoo & IBar`, ou um tipo que implementa várias — duas assinaturas
homônimas idênticas viram um contrato; conflitantes erram), **tipo ∘ (trait/interface)**, e a
composição de **operador-interfaces** (`IEq & IOrd`…). Uma regra uniforme e decidível, não uma por construto.

**Structural traits (`Eq`/`Ord`/`Hash`/`Clone`/`Default` + sinônimos `Hashable`/`Comparable`) —
APOSENTADAS.** Eram *compiler-shadow*: nomes **hardcoded** (`is_structural_trait`, `resolve.tks`) cujos
corpos o compilador **sintetizava dos campos** (`synthesize_structural_methods`, `synth.tks` — gera
`eq`/`compare`/`hash`/`clone`/`default` campo-a-campo, sem metadata de runtime, Law M.0). A máquina
**funciona**, mas **uso real em `/src/**/*.tks` = zero** (nenhum derive `& Eq…`, nenhuma chamada
`.eq()`/`.compare()`/…), e **contradiz o princípio no-shadow** — o dev nunca vê nem escreve o corpo.
Decisão do dono: **remover** — síntese + reconhecedores (`resolve.tks`, `collect.tks`, `monomorph.tks`).
Quem precisar de igualdade/ordem/hash **implementa interface explícita**, visível. (Deleção de código do
compilador — tarefa de implementer numa onda.)

**A capacidade renasce como interface que OBRIGA um operador por contrato** (ruling do dono — habilitado
pelo overload de operador, §9). Em vez da síntese-shadow, `Eq`/`Ord`/… viram **interfaces cujo contrato é
um operador** (`operator __eq`/`__lt`/…, §9). O tipo cumpre **escrevendo o operador** (explícito, visível);
o genérico constrange na interface e **despacha o operador através de T** — exatamente o que a structural
NÃO conseguia (era opaca-em-T, `resolve.tks:863` devolvia superfície vazia; foi por isso que o `Map`
desistiu e virou `str`-keyed). A interface despacha por vtable, então `<K: IEq & IHash>` **destrava** o
`Map<K, V>` genérico que nunca funcionou.

**A interface OBRIGA a counter-part do operador** — completude, sem meia-comparação. Igualdade por
**negação** (`==` obriga `!=`, com `!=` = `!(==)`); ordem por **reflexão** (`<` obriga `>` = operandos
trocados, e `<=` obriga `>=`). O contrato lista **as duas** assinaturas; um **trait-decorador opcional**
entrega o corpo óbvio da counter-part (zero shadow — quem quiser um `!=` não-trivial, tipo NaN, escreve o
próprio em vez de compor o decorador).

```teko
type IEq = interface {
    operator __eq(left: self, right: self): bool     // contrato: ==
    operator __ne(left: self, right: self): bool     // counter-part OBRIGATÓRIA: !=
}

// decorador opcional: entrega o corpo óbvio de !=, sem mágica de compilador
type NeByEq = trait {
    operator __ne(left: self, right: self): bool { !(left == right) }
}

type Point = struct IEq & NeByEq {                   // == à mão, != herdado do decorador
    x: i32
    y: i32
    operator __eq(left: self, right: self): bool { left.x == right.x && left.y == right.y }
}

fn index_of<T: IEq>(xs: []T, needle: T): i32 {       // == despacha pelo contrato IEq — REAL
    var i = 0
    loop { if i >= xs.len { return -1 }; if xs[i] == needle { return i }; i++ }
}
```

Compartilhar o *corpo* de um operador entre tipos de mesma forma é papel do **trait-decorador** (achata
`operator __eq(...) { … }`); comparação campo-a-campo genérica (o que a síntese fazia por mágica) **não**
volta — cada tipo escreve o seu, ou compartilha via decorador quando faz sentido. **Constraint =
interface-only, sem exceção.**

**Sobra UM construto de capacidade:**

| construto | é tipo? | pode constraint? | corpo |
|---|---|---|---|
| **interface** | sim (dispatch) | **sim** | assinaturas (o dev implementa) |
| **trait-decorador** | não (achata) | não | métodos-com-corpo (o dev escreve) |

### 9.D Aposentar `type X = variant` — a união `|` estrutural inline, por extenso (ruling do dono)

**A decisão.** O tipo-soma **nomeado** (`type X = variant A | B | …`) **sai**. Não é substituído por
wrapper (`struct { case: … }`) nem por alias (`type X = A | B`) — **a única forma-soma é a união `|`
estrutural inline**, escrita **por extenso** onde é usada (var/param/retorno/**campo**, §9.2b). Os
**membros** continuam tipos nomeados (`type Prim = struct { … }`); só o **agregado** perde o nome.

```teko
// ANTES (variant nominal):
pub type Slice = struct { element: Type }
pub type Type  = variant Prim | Byte | Char | Str | Slice | Named | Variant | Func | Error | Void | Ptr | Uptr | Reference | Null

// DEPOIS (§9.D — o agregado 'Type' some; a união vai por extenso em cada campo):
pub type Slice   = struct { element: Prim | Byte | Char | Str | Slice | Named | Variant | Func | Error | Void | Ptr | Uptr | Reference | Null }
pub type Variant = struct { members: [](Prim | Byte | Char | Str | Slice | Named | Variant | Func | Error | Void | Ptr | Uptr | Reference | Null) }
pub type Func    = struct { params: [](Prim | … | Null); ret: Prim | … | Null; variadic: bool; … }
```

Note os **parênteses obrigatórios no array-de-união** (`members: [](Prim | … | Null)`): `[]` liga mais
forte que `|`, então sem eles `[]Prim | … | Null` seria "array-de-Prim, ou Byte, ou …" (§9.2b, precedência).

**A verbosidade é o preço aceito** (ruling do dono, verbatim: *"Vai ser verboso mesmo, e isso não é
problema, é o preço."*) — mas **abreviável por MACRO**, não por alias. Uma `macro` da Família A (§14.1)
nomeia a expansão sem criar um tipo:
```teko
pub macro Type() { lowering { (Prim | Byte | Char | Str | Slice | Named | Variant | Func | Error | Void | Ptr | Uptr | Reference | Null) } }
pub type Variant = struct { members: []@Type() }   // @Type() alarga para a união entre parênteses, pré-typecheck
```
O `@Type()` **copia-se in loco e alarga a AST antes do typecheck** — o checker vê a união literal inline,
**sem** `type` nomeado nenhum, então o §9.D segue honrado (o proibido é o alias/wrapper/newtype *nominal*,
não a macro sintática). Escala real: `: Type` completa aparece em **~630 sites** de valor direto (+ 70
`[]Type`, + 29 `| null`); `MInst` tem 32 membros, `LOp` 16 — a macro é o que torna isso sustentável.

**Por que fecha o crux (o "risco de 1ª ordem", `plano-match-universal §4.2`).** Nada vira referência:

- **Recursão — box implícito da própria união.** `Slice.element` carrega a união que **inclui `Slice`**; o
  descritor fat `{tag@0; ptr@8; len@16}` que a união `|` já emite (o *compiler-managed indirection* que o
  `variant` sempre teve) é quem boxa a fronteira recursiva. Sem `ref` explícito. Um campo `[]…` (lista) já é
  indireto pela slice, então nem entra na conta; o núcleo que depende do box são os **~10–20 campos de
  valor-direto auto-referente** (`Slice.element`, `Func.ret`, `Ptr.inner`, `Reference.inner`,
  `TCall.callee_type`, `TLambda.ret`, …).
- **`match` e construção — inalterados.** Os membros seguem nomeados, então `match x { Prim as pa => … }`
  e a coerção membro→união (`Named { name = … }` flui para um slot-união) ficam **byte-a-byte iguais**. Os
  112 arquivos de `match` não mudam de grafia.
- **Membro compartilhado — trivial.** `Function`/`TypeDecl` aparecem em várias uniões ao mesmo tempo
  (`Decl`, `ItemKind`, `TItem`); sem nome nem herança, cada união apenas **os lista**. O problema que
  forçaria split (herança) não existe.

**Escopo e ordem.** ~28 ADTs (a contagem correta é 28 — `BindElem` incluído). A **espinha do
`plano-9d-migracao-variant.md`** — a classificação por-ADT, a lista de campos recursivos, a ordem de
fixpoint folhas→raízes (`Type` por último por causa do carrier `Variant`), as 8 fixtures e o wire `.tkb` —
**segue válida**; o que muda é a FORMA-alvo: **não** a "Solução A / newtype-tagged-value-union" que aquele
doc recomendava (wrapper), e **sim** a união inline por extenso deste ruling. A migração é **source-only** —
a união emite o mesmo descritor que o `variant` emitia, então o gate é **byte-identidade** no fixpoint.

---

## 10. Concorrência — a superfície (`spawn`/`chan`, `await`, journaling)

A faceta de **arena** dela está no Doc 1 §7; aqui é a **superfície** que o usuário vê. Recomposto de
`concorrencia-isolate-spawn-chan` (08-03), `journaling-de-corrida` (07-30) e `paralelizacao-eixo1/eixo2`
(08-02).

### 10.1 Estratégia de token — tudo contextual

`spawn` (dispara) e `await` (suspende/prefixo de ligação) são **keywords contextuais** (reconhecidas pelo
parser por posição, sem reserva no lexer — a mesma norma que `class`/`abstract`/`virtual`/`override`
seguem); `chan` é um **tipo genérico** (`chan<T>`). **Não há necessidade de uma keyword `async`** (o
`await` prefixo já basta, §10.3) **nem de um `isolate`** (uma thread no mesmo processo ainda pode corromper
e só fala por canais do SO — quem precisa de isolação real usa outro binário). Medido:
**zero identificadores Teko hoje** se chamam `spawn`/`chan`/`await`, então reconhecê-los por posição não
quebra corpus.

### 10.2 `spawn` (corotina) + `chan<T>` — paralelismo real, memória isolada

`spawn` é uma **keyword de corotina** (estilo Go), **não uma função** — `spawn f(args)` lança `f` como
uma **thread** fire-and-forget — sub-raiz de arena própria (F1), argumentos **por cópia**, **sem retorno**
e **sem `join`**. Não há isolamento tipo-processo na linguagem (quem precisa usa outro binário). Doc 1 §7.6.

```teko
spawn f()                                          // KEYWORD (não função): dispara e segue, SEM id, args por cópia

// chan<T> MPSC (fan-in: N escritores, 1 leitor). TODO transporte é um `service singleton`; a chave é CONSTANTE.
// TRANSPORTE = contrato extensível (SO, memória, Kafka, RabbitMQ, RPC, WS, HTTP, …):
type IChannelKind<T> = interface { fn init(key: str); fn send(v: T); fn recv(): T; fn end() }

// make: cria, chama K.init(key), registra o serviço na RAIZ DO PROGRAMA (F2) sob a chave,
// e devolve o CONTEXTO (ctx) — o WaitGroup MANUAL do canal + um fecho de reserva:
static fn chan<T>::make<K: service singleton & IChannelKind<T>>(key: str, bounds: usize = 1): Ctx | error
                                                   // error: conflito de chave (runtime) ou falha do K.init (abrir o transporte)
fn has_svc<T: service>(key: str | null = null): bool   // checar existência antes de resolver (evita o panic do svc)
              // key:    CONSTANTE (literal/const, comp-time) — o "nome" do canal
              // bounds: 1 (default)=bounded-1 · N=bounded-N · 0=UNBOUNDED

// Os extremos são clamados por svc, pela MESMA chave constante (svc = comp-time, inline):
var rx = svc<Rx<T>>("chave")                       // LEITOR único
var tx = svc<Tx<T>>("chave")                       // ESCRITOR (N)
fn  Tx<T>::send(v: T): null                        // devolve null; a propriedade tx.closed diz se fechou (drenado)
fn  Tx<T>::close()                                 // o PRODUTOR fecha: invoca o end() (fecha + drena)
fn  Rx<T>::pop(): T | Closed                       // recebe; o erro ESPECÍFICO `Closed` quando encerrado e drenado

// WaitGroup MANUAL — add/done disponíveis TAMBÉM nos handles (o ctx é transient; o worker não o alcança):
fn  Tx<T>::add()                                   // um PRODUTOR se registra pelo SEU handle
fn  Tx<T>::done()                                  // um PRODUTOR sinaliza fim pelo SEU handle
fn  Rx<T>::add()                                   // um CONSUMIDOR se registra pelo SEU handle
fn  Rx<T>::done()                                  // um CONSUMIDOR sinaliza fim pelo SEU handle
fn  ctx::add(n: usize)                             // o CRIADOR registra N (antes do spawn = race-free)
fn  ctx::wait()                                    // o criador bloqueia até o contador zerar
fn  ctx::close()                                   // fecho de reserva do canal

fn hardware_parallelism(): usize                   // paralelismo concedido pelo SO
```

Um fluxo mínimo — a `main` cria o canal (recebe o **ctx**), faz o **`add` manual**, uma corotina resolve o
escritor por chave, escreve e faz **`done` manual**; a `main` lê até encerrar e `wait`. **Nenhum id trafega:**

```teko
fn produz() {                            // função sem retorno — SEM id (resolve por chave)
    var tx = svc<Tx<i32>>("nums")        // extremo de escrita, pela chave constante
    var i = 0
    loop while i < 100 { tx.send(i); i = i + 1 }
    tx.close()                           // o PRODUTOR fecha → o pop do leitor passará a devolver `Closed`
    tx.done()                            // done() pelo próprio handle (o ctx é transient, inalcançável aqui)
}

fn main() {
    var ctx = chan<i32>::make<OsChan<i32>>("nums", 64)   // cria em F2 sob "nums"; devolve o ctx
    ctx.add(1)                            // add() MANUAL — 1 task a esperar
    spawn produz()                        // dispara e segue (sem join) — SEM id
    var rx = svc<Rx<i32>>("nums")         // o LEITOR também por svc, pela chave
    loop {
        var v = rx.pop()                  // v: i32 | Closed
        match v {
            i32    => usar(v),
            Closed => break               // erro específico: encerrado e drenado — a sincronização
        }
    }
    ctx.wait()                            // bloqueia até o done()
}
```

**Sem `join`:** dados só cruzam a fronteira por **cópia** (via `chan`); espera-se pelo **fecho do canal**
(o `pop` devolver `Closed` após `end()`) ou pelo `ctx.wait()`. A camada de linguagem **não reimplementa**
limite/contrapressão/fecho — pede uma vez na abertura e confia no transporte. *(A primitiva
`fork_join` de baixo nível sobrevive como mecanismo INTERNO do backend, para paralelizar o codegen — não
é superfície de usuário; o usuário escreve `spawn`.)*

**`make` devolve o `ctx`; ambos os extremos por `svc` (ruling do dono).** A criação é por `make`, que devolve
o **contexto** (`ctx`) — o **WaitGroup** do canal e um fecho de reserva. Os extremos são clamados por chave:
`svc<Rx<T>>(key)`, `svc<Tx<T>>(key)` — coordenação sem trafegar id.

**WaitGroup MANUAL; `add`/`done` também nos handles — não bloquear o dev (ruling do dono).** A contagem é **do
usuário**, **nunca automática** (o `spawn` não registra nada, a saída da task não faz `done` sozinha). Como o
`ctx` é **transient** e um worker não o alcança, mas sempre segura o seu `tx`/`rx` (estável, F2), tanto
**`add`** quanto **`done`** ficam **também nos handles**: `tx.add()`/`tx.done()` (produtor) e `rx.add()`/
`rx.done()` (consumidor). O criador ainda tem `ctx.add(n)` e `ctx.wait()`. **Caveat (não trava):** adicionar
**depois** do `spawn` (via handle) em vez de **antes** (via `ctx.add`) é a corrida "add-depois-do-spawn", e é
**responsabilidade do dev** — o `ctx.add(n)` antes do `spawn` é o caminho race-free, mas ele coordena como quiser.

**Registro (compilador) vs materialização (`make`) — dois modos de chave (ruling do dono).** `Ctx`/`Rx<T>`/
`Tx<T>` **não** são serviços do usuário — são handles que o compilador registra e o `make` materializa. O
registro tem duas fases, e o modo é escolhido pela **forma da chave**:
- **Chave CONSTANTE (literal/`const`) — registro ESTÁTICO, inline (o default, custo zero).** O compilador
  pareia cada `svc<…>("literal")` ao `make` da mesma constante, **conhece o `K`**, monomorfiza `Ctx`/`Rx`/`Tx`
  + as ops do transporte, reserva o slot **inline**; o `make` só **materializa**. **Sem lookup em runtime.**
  Conflito (mesmo `chan<T>`+chave em dois `make`) = **erro de compilação**.
- **Chave VARIÁVEL (`str` de runtime) — PRÉ-REGISTRO + FINALIZAÇÃO em runtime.** Para o caso em que a chave
  não é conhecida na compilação (um canal por conexão/request/usuário), o compilador **pré-registra a FORMA**
  (monomorfiza `(chan<T>, K)`: handles por `T` + um **registro de ops** do transporte), e o `make` **finaliza**
  a ligação chave→instância num **registro processo-inteiro chaveado pela string** (F2). Aí `svc<Rx<T>>(var_key)`
  é um **lookup em runtime**, e as ops de `Rx`/`Tx` viram **chamada indireta** (ponteiro de função) — **não**
  vtable de interface, **não** o Round 3. **Custo:** um lookup + uma indireção por op. Conflito = **erro em
  runtime** e sai como **`error`** — por isso `make` devolve **`Ctx | error`** (o `error` cobre o conflito de
  chave e a falha do `K.init`, p.ex. abrir o socket). **`svc` de chave não encontrada = PANIC (ruling do
  dono)** — o `svc` é infalível no tipo (devolve `T`); chave ausente é `panic`. Para checar **antes** há
  **`has_svc<T: service>(key): bool`**. (O `T` continua estático nos dois modos; só o `K` é apagado no `svc` variável.)

**O `ctx` É O DONO do lifetime — transient (ruling do dono, ratificado).** O serviço singleton do canal vive em F2, mas
**quem o possui é o `ctx`** (transient, na região do criador). Quando o `ctx` cai, ele **cascateia o
teardown**: `end()` → desregistra a chave → **libera** o serviço + `Rx` + `Tx` de F2. O canal, os dois
extremos e o serviço morrem **junto com o `ctx`**. O **UAF é fechado por construção**: (a) `ctx.wait()`
barreira — o criador espera todas as tasks antes de a região cair, então nada vivo aponta para o que morre;
(b) resolução por **chave** (não ponteiro) — depois do teardown, `svc<…>("chave")` **falha** em vez de
pendurar. O **"free"** é a **reclamação por-entrada de F2** disparada pelo drop do `ctx` (disciplinada pela
arena, não um `mem::free` manual) — a única capacidade nova de arena que isto exige (Doc 1 §7.8).

**Quem fecha o canal — o PRODUTOR (ruling do dono).** O **`Tx` tem o `close()`** — quem sabe que não há mais
mensagens é o produtor, então é ele que fecha (`tx.close()` → `end()`; convenção Go); com N produtores,
coordena por `ctx.wait()` e fecha **uma vez** (idempotente). O fecho também fica **no `ctx`** (`ctx.close()`),
**caso precise**. O **consumidor (`Rx`) não fecha** — faz `pop()` até receber o erro **específico `Closed`**
(encerrado + drenado, distinto de um `error` de transporte). Ambos observam: `Rx` pelo `Closed`, `Tx` por
`tx.closed`.

**Transporte extensível — `service singleton & IChannelKind<T>` (ruling do dono).** O transporte **não é um enum
fechado**: é uma **interface** genérica com `init(key)`/`send(T)`/`recv(): T`/`end()` (o `init` é o método
prévio que liga o transporte à chave; o `end()` sinaliza fecho + dreno). A linguagem entrega os built-ins
`OsChan<T>` (default — `SOCK_DGRAM`/mailslot) e `MemChan<T>` (fila em-processo, sem syscall), e o dev
**pluga o seu** — Kafka, RabbitMQ, RPC, UDP, WebSocket, HTTP, o que for. É **native-only**: código Teko
falando o protocolo, ou link dinâmico FFI a uma lib de sistema, nunca um `.c` local.

**Totalmente estático — DI por CHAVE CONSTANTE (ruling do dono).** Todo transporte é um **`service singleton`**
(o constraint `K: service singleton` faz um transporte não-singleton **falhar na compilação** no `make`, em
vez de o compilador redirecionar silenciosamente uma implementação errada — §9.2b), e o
canal é resolvido pela **chave constante** (um literal/`const`, comp-time), como `svc<T>("chave")`. `make<K:
service singleton & IChannelKind<T>>(key, bounds): ctx` cria o canal, chama `K.init(key)`, registra o serviço
na **raiz do programa (F2)** sob a chave e devolve o **ctx** (WaitGroup); **ambos** os extremos vêm por
`svc<Rx<T>>("chave")` / `svc<Tx<T>>("chave")` — resolvidos **inline em comp-time**. Assim **nada trafega id**
(nem no `spawn`), e **nada reconstrói o tipo**: a chave constante + o tipo dizem tudo em compilação. O serviço
do canal vive em **F2** (exceção de lifetime — não raiz-de-thread — por ser comunicação entre tasks), do
`make` ao fecho. **Não** é dispatch dinâmico do Round 3 — pré-requisito é só a conformidade estática de
interface (já existe) + o `service` DI por chave. **Conflito de chave = erro de compilação** (§7).

```teko
type IChannelKind<T> = interface { fn init(key: str); fn send(v: T); fn recv(): T; fn end() }

// o dev escreve o seu transporte — um `service singleton` que satisfaz a interface (conformidade estática):
service KafkaChan<T> singleton & IChannelKind<T> {   // singleton: forçado pelo constraint do make
    brokers: str
    topic: str
    fn init(key: str) { /* liga o transporte ao tópico derivado da chave `key` */ }
    fn send(v: T)     { /* serializa v e publica (Teko puro ou FFI dinâmico) */ }
    fn recv(): T      { /* consome e desserializa em T */ }
    fn end()          { /* fecha o produtor e drena */ }
}

var ctx = chan<Order>::make<KafkaChan<Order>>("orders", 64)  // cria, init("orders"), serviço → F2; devolve o ctx
var rx = svc<Rx<Order>>("orders")                            // leitor resolvido por chave (comp-time)
var tx = svc<Tx<Order>>("orders")                            // escritor resolvido por chave (comp-time)
```

### 10.3 `await` — alarga o retorno para `Intent<T>` (sem necessidade de `async`)

**Não há necessidade de `async`, a função NÃO declara `Intent`, e o `await` é um PREFIXO de ligação/atribuição** (não um
operador de expressão). A função retorna o seu tipo normal; `await var a = f()` faz `a` receber um
`Intent<T>` em vez do valor cru — **alarga**, ao contrário das linguagens que **estreitam** a assinatura
para `Task<T>`/`Promise<T>`. (Consequência: não há `await` inline numa expressão — liga-se primeiro.)

```teko
fn calc(x: i32, y: i32): i32 { x + y }

await var a = calc(1, 2)       // a : Intent<i32> — o `await` prefixa a ligação
if a.canceled { println("canceled") }
else          { println($"r = {a.value}") }

await var b, c = fb(), fc()    // atribuição múltipla (§9.3): b, c : Intent<…>
await a = fa()                 // reatribui um `a` existente (remanescente de outro intent)
```

`await f(args)` **suspende** (cede, sem bloquear a thread), executa `f`, e o retorno cai em **`.value`** de
um `Intent<T>` criado no caller. Campos: **`.value: T`**, **`.canceled: bool`**, **`.failure: error | null`**
(razão do cancelamento). Erro-de-negócio fica no `.value`; cancelamento é `.canceled`+`.failure`. `Intent`
(sem `T`) = o desfecho de esperar uma função **sem retorno** (só `.canceled`/`.failure`). É onde se
**garante a execução** — o `spawn` é fire-and-forget, sem retorno.

**A `Intent` é PROTEGIDA — o dev LÊ, o runtime GRAVA, ninguém inicializa à mão.** Struct puro (sem trait):
backing fields **privados** (`_value`/`_canceled`/`_failure`, visíveis só a runtime/compilador), face de
leitura por **`exp get`**, escrita do desfecho por **`pub set`** (o runtime grava dado no sucesso, ou
`canceled`+`failure` no cancelamento), e a única construção é **`pub static fn new`** — interna. A grafia
`exp`/`pub` é **forward-compatible**: hoje tudo lê como `exp` (sem enforcement); quando o **§11** entrar,
`pub` passa a valer e a proteção **auto-corrige**, sem refactor.

```teko
exp type Intent = struct {           // esperar uma função SEM retorno — só o desfecho
    _canceled: bool
    _failure: error | null

    exp get canceled(): bool          { self._canceled }
    exp get failure(): error | null   { self._failure }
    pub set canceled(v: bool)         { self._canceled = v }
    pub set failure(v: error | null)  { self._failure = v }

    pub static fn new(c: bool, f: error | null): self {
        .{ _canceled = c; _failure = f }              // target-typed (§4.1); o alvo é `self`
    }
}

exp type Intent<T> = struct {         // esperar uma função COM retorno T — o valor cai em `.value`
    _value: T | null
    _canceled: bool
    _failure: error | null

    exp get value(): T | null         { self._value }
    exp get canceled(): bool          { self._canceled }
    exp get failure(): error | null   { self._failure }
    pub set value(v: T | null)        { self._value = v }
    pub set canceled(v: bool)         { self._canceled = v }
    pub set failure(v: error | null)  { self._failure = v }

    pub static fn new(v: T | null, c: bool, f: error | null): self {
        .{ _value = v; _canceled = c; _failure = f }   // target-typed (§4.1)
    }
}
```

**Dependências da forma** (todas forward-compatible): **§9** (properties `get`/`set`, factory estática,
construção target-typed `.{}` — §4.1, o `self { }` construtor foi retirado), **item 14** (value-struct
mutável — o `set` escreve `self._x`, o que a regra "struct é
readonly" proíbe hoje), **§11** (enforcement `exp`/`pub`). E uma **consequência de arena**: value-struct +
preenchimento pelo runtime exige que o `set` acerte **a mesma instância** que o awaiter segura — nada de
cópia entre o fill e a leitura (residência F1/F2, §10.2).

**`cancel()` — global, como `panic`, ciente de suspensão.** O runtime marca se a tarefa corrente está sob
`await`. `cancel()` **em suspensão** → cancela o `Intent` corrente (`.canceled = true`, `.failure` = a
razão); **fora de suspensão** → dispara um `panic`. Um verbo só que degrada de cancelamento cooperativo
para panic.

**Várias tasks — atribuição múltipla (§9.3), sem `when_all`/`when_any` nem `await` de array.** O `await`
prefixa uma ligação múltipla e **cada alvo vira um `Intent`**, todas esperadas:

```teko
await var a, b, c = fa(), fb(), fc()   // a, b, c : Intent<…> — todas esperadas em paralelo
```

Como **não há throwing** (cancelada ou não, a task sempre executa até um desfecho), esperar todas é seguro;
inspeciona-se cada `Intent` (`.value`/`.canceled`/`.failure`). Isso torna `when_all`/`when_any` e o `await`
de array desnecessários. O dev nunca escreve `Intent` num retorno; só o `await` o produz.

**Descartar o retorno — `await _ = f()`.** Espera `f` completar (garantia de execução, por suspensão) **sem
capturar** o desfecho — como esperar uma `Task` em C# sem guardar o resultado. Usa o `_` (o mesmo descarte
do resto da linguagem), que não abre variável nem materializa o `Intent`. Difere do `spawn`, que **não**
espera:

```teko
await _ = liberar_cache()      // espera; nenhum Intent capturado
await _, x = fa(), fb()        // descarta o 1º, captura o 2º em x : Intent<…>
```

- **Arena/fundações** (Doc 1 §7.9): **I/O** — reator (`epoll`/`kqueue`/`IOCP`), sem thread nova; **CPU** —
  corotina isolada de um pool (F1). Em ambos o `await` cede e retoma, nunca bloqueia.

**Não** existe thread compartilhando arena sem F1. E **`ref` não cruza** a fronteira: nem `spawn`/`chan`
aceitam `ref`, nem `<ref T>` genérico (§10.6) — preserva UAF; cruza cópia ou id.

### 10.4 `teko::journal` — o módulo de journaling

O journal é um **registro append-only, carimbado por corrida, segmentado por escritor**; a sumarização
**relê** (`fold`), não funde. Para quem escreve testes hoje, **nada muda** de grafia (`teko::test::scoped`
segue igual). Segue **exatamente o mesmo modelo dos canais** (ruling do dono): o **sink** é um
`service singleton` que satisfaz uma interface, o `make` recebe a **chave constante** e devolve o **ctx**, e
o **escritor** é clamado por `svc` — tudo residindo na **raiz do programa (F2)** (Doc 1 §7.10):

```teko
pub type Record = struct { run: str, writer: str, kind: str, payload: str }

// o SINK do journal é um contrato — um `service singleton`, como o transporte de canal:
type IJournalKind = interface {
    fn init(key: str)                 // método prévio: liga o sink à chave constante
    fn append(rec: Record)            // grava um registro (write(2) O_APPEND, sem buffer)
    fn end()                          // fecha + flush
}

// make: cria, chama K.init(key), registra o service singleton em F2 sob a chave, devolve o ctx:
static fn journal::make<K: service singleton & IJournalKind>(
    key: str, roll: Roll = Roll::none, fmt: func<Record, str> | null = null): ctx

var jw = svc<Jw>("chave")            // o ESCRITOR do journal, clamado por chave (Jw = handle de escrita)
fn  Jw::append(kind: str, payload: str): null | error
fn  Jw::close()                      // o produtor fecha (invoca end()); reserva em ctx.close()

// nível de corrida (funções de corrida, sem chave de segmento) — inalterado:
static fn journal::run_id(): str                 // <ns monotônico>-<pid> — nomear a corrida mata o lixo calado
static fn journal::run_root(): str               // bin/.tkrun/<run_id()>
static fn journal::fold(root: str, run: str): []Record   // relê; descarta lixo/linha rasgada; sintetiza `end`
static fn journal::scratch(base: str): str       // o compositor único de caminhos isolados
static fn journal::sweep(keep: str): usize       // limpeza é da corrida SEGUINTE, nunca da própria
```

O sink default é o **`FileJournal`** (`service singleton & IJournalKind` — `write(2)` `O_APPEND` sem buffer,
com rolling); o dev pode plugar outro sink (um serviço remoto de log, etc.), como qualquer transporte de canal.

**Rolling e formatação — configuráveis pelo dev** (ruling do dono; é aqui que um journal de 10 GB se
gere). O `make` recebe, com defaults, a **política de rolling** (QUANDO rotacionar para um novo ficheiro)
e o **formato** (COMO cada registro é serializado):

```teko
enum Roll {                       // QUANDO rolar (política de rotação)
    none,                         // um único ficheiro (default)
    size(usize),                  // rola ao atingir N bytes  → ex.: Roll::size(100 * 1024 * 1024)
    daily,                        // rola por data (um ficheiro por dia)
    custom(func<SegStat, bool>)   // o predicado do dev decide (tamanho, idade, contagem, o que for)
}

// fmt é uma CLOSURE (func<Record, str>), default null: se null (ou omitido), usa-se a formatação padrão
// (o formato interno "kind<TAB>payload"); senão, a closure do dev serializa cada registro.

// ex.: padrão (FileJournal, fmt omitido → formatação padrão), rolando a cada 100 MB:
var ctx = journal::make<FileJournal>("app", Roll::size(100 * 1024 * 1024))   // devolve o ctx
var jw  = svc<Jw>("app")                              // escritor por chave
jw.append("evt", "iniciado")

// ex.: formatação própria via closure — literal `(params) => expr`, sem `fn`:
var ctx2 = journal::make<FileJournal>("audit", Roll::daily, (r) => r.kind + "|" + r.payload)
```

O rolling é do runtime de durabilidade (o `append` verifica a política e rotaciona por `rename` atômico
antes de escrever quando ela dispara); o `fold` relê todos os ficheiros de um segmento em ordem,
transparente ao rolling. A closure `fmt` vive com o journal (arena raiz) e é chamada por `append`.

**Papel duplo + ciclo de vida (rulings do dono, Parte 4).**
- **Ambos os papéis, um módulo só.** O journal é **ao mesmo tempo** durabilidade dev-facing **e** o veículo
  do **journaling de concorrência** — incluindo o produto que o motivou: escrever as saídas de teste em
  **paralelo** para pós-análise. Tudo passa pelo **sink** (destino IO) e pelo **out** (formato); a mesma
  abstração cobre **texto e binário**.
- **Escrita concorrente binária.** O caso motivador: N escritores → `chan<Rec>` (bounded MPSC) →
  orquestrador agrega/sumariza → **`.tkj`** (binário, append-only, quadro **prefixado por comprimento**,
  precedente `.tkb`); relê-se com `teko journal corrida.tkj -o corrida.log`. Binário **por enquadramento**
  (o `expected`/`got` de uma asserção tem quebras de linha; um quadro prefixado torna a carga opaca e
  incorruptível ao enquadramento), **portátil** (little-endian fixo — `tkb_buf.tks` desloca bytes, um
  journal de CI arm64 lê num x86_64). O registro carrega `writer`+`seq` (atribuição + perda **detectável**);
  a captura de `panic`/`exit` mantém o **processo vivo**. Desenho completo do produto: artifact das leis
  §17–§27 (dono, 2026-07-30).
- **Lazy.** O journaling **liga no primeiro uso** (custo zero antes) e **fica ligado até o fim** (residência
  **F2**). Nem sempre-ligado, nem compilado-fora, nem nível-em-runtime.

### 10.5 Modelo de concorrência — resumo (decisões fechadas)

| tema | fechado (08-10) |
|---|---|
| **`spawn`** | keyword (Go-style); dispara uma **função sem retorno** como **thread** fire-and-forget; args por cópia; sem `join` |
| **`isolate`** | **sem necessidade** — thread no mesmo processo ainda pode corromper e só fala por canais do SO; isolação real = outro binário |
| **`chan<T>`** | tipo genérico MPSC; `make<K: service singleton & IChannelKind<T>>(key, bounds = 1): ctx`; WaitGroup **manual** — `add`/`done` no `ctx` **e** nos handles (`tx`/`rx`, pois o ctx é transient), `ctx.wait()` no criador; extremos por `svc<Rx>`/`svc<Tx>`; unbounded = `make(key, 0)` |
| **transporte** | `IChannelKind<T>` = `interface { init(key); send(T); recv(): T; end() }` **extensível**; built-ins `OsChan`/`MemChan` + plug do dev (Kafka/Rabbit/RPC/UDP/WS/HTTP); **DI por chave** — todo transporte é `service singleton` (não-singleton = erro), vive em **F2**; chave **constante** = registro estático/inline, chave **variável** = pré-registro + lookup runtime; **elimina id no `spawn`**; não dispatch dinâmico |
| **fecho** | **responsabilidade do PRODUTOR** — `tx.close()` invoca `end()` (fecha + drena), idempotente; reserva em `ctx.close()`; consumidor faz `pop()` até o erro específico `Closed`. `Rx::pop(): T \| Closed`; `Tx::send(): null` + `tx.closed` (ambos observam) |
| **`await`** | **sem necessidade de `async`**; prefixo de ligação que **alarga** o retorno para `Intent<T>` por suspensão; sem inline; `await _ = f()` descarta o retorno |
| **`Intent<T>`** | `.value` / `.canceled` / `.failure`; **`cancel()`** global (cancela o Intent, ou `panic` fora de suspensão) |
| **várias tasks** | por **atribuição múltipla** (`await var a, b = fa(), fb()`), sem `when_all`/`when_any` |
| **`ref`** | não cruza fronteira; `<ref T>` proibido (§10.6) |
| **namespace / afinidade** | `teko::threads`; afinidade **válida, registrada como capacidade futura** |

### 10.6 `ref` e genéricos sob concorrência — a regra de UAF (ruling do dono 08-10)

`ref` (borrow) **não pode cruzar fronteira de MT/async**, e um genérico **não pode ser parametrizado por
referência** — `<ref T>` é rejeitado pelo checker. Um borrow que atravessasse uma task/continuação
penduraria quando a arena do outro lado dropasse (UAF). Consequências de superfície:

- `spawn`/`svc<Tx<T>>`/`svc<Rx<T>>`/uma função esperada por `await` **rejeitam** parâmetro/valor `ref`.
- `Foo<ref T>` não parseia/typa. `ref` sobrevive só como borrow **local** caller→callee que **não** cruza
  fronteira de concorrência (§3: `ref` já é só parâmetro).
- O que cruza a fronteira é **cópia** (valor) ou **nome** (a chave constante do canal), nunca borrow.

---

## 11. Sequência (ordem do dono: superfície → reseed → backend)

1. **Aditivo (front/FFI):** cada mudança entra aceitando a grafia velha ao lado da nova, escrita na
   grafia velha (o seed atual parseia). Ordem entre elas é livre (são mutuamente independentes).
2. **Sweep:** reescreve `src/` para a grafia nova e remove a velha (dropa `->`, `let`/`mut`, receptor
   solto, `unsafe`, memória manual; rebola `u64`→`usize`).
3. **R1 — reseed** (uma vez): captura um seed que fala a grafia nova. Gate = self-reproduce da rota C +
   provenance + testes de superfície verdes. **O `gen2==gen3` nativo NÃO é gate do reseed nesta ordem** —
   ele vira o marco que fecha a fase seguinte.
4. **Backend — novo tratamento de arena** (`arena-especificacao-unica-0.3.1.md`): DPS/piso/elisão. Fecha
   o `gen2==gen3` nativo por edição de fonte, **sem segundo reseed** (o seed é da rota C, que já funciona;
   os bugs nativos são de fonte).

**Ordem dos itens de roadmap dentro desta janela** (rulings do dono):
- **item 14 — value-struct mutável** (C#-style via property, tirando o bloqueio "struct é readonly"):
  **ANTECIPADO.** Está no caminho crítico da Intent (o `pub set` que escreve `self._x`, §10.3) e do `Ctx`
  performático; entra cedo.
- **expansão da stdlib — ANTES da §11.** O cerne é **aumentar os recursos do dev** (um **catálogo** de novos
  itens — passo focado próprio, separado de "como separa"); e decidir a **separação** que permite o
  **self-link com o programa final**. É quem **povoa** o `exp`/`pub` que a §11 depois formaliza. Bônus: **já
  reduz parte dos problemas de memória** (o RSS super-linear do item 13 — o programa linka contra o monólito
  pré-compilado em vez de re-arrastá-lo como fonte).
  - **Mecanismo (ruling do dono): self-`.tkh` do monólito.** Teko é monolítico; **nada de extrair pacote**. O
    compilador, ao compilar, **exporta o `.tkh` dele próprio**, e o programa final **linka contra o monólito**
    via esse header. Só o que é **`exp`** entra no self-`.tkh` → reforça a necessidade de `exp` no visível ao
    dev. **Substitui** a ideia de pacote-separado (o fork P2 da prep).
- **§11 — visibilidade `exp`/`pub`: PENÚLTIMA** (não mais a última). O enforcement vira muita coisa
  hoje-visível em **ilegal** (muitas falhas a corrigir), e a grafia já é **forward-compatible** (hoje tudo
  lê como `exp`; a proteção da Intent auto-corrige quando ela entrar), então atrasá-la minimiza retrabalho.
  **Forma a superfície de visibilidade** sobre a qual a §12 opera.
- **§12 — libc-direct / `#if` / `#os` / macro: ÚLTIMA.** Invertida com a §11: precisa **operar sobre a §11
  já formada** (a visibilidade `exp`/`pub` decidida), então só entra depois dela.

### 11.1 Regra de ouro `exp`/`pub`/privado — a triagem de visibilidade

Três camadas, uma regra por camada (ruling do dono):

- **`exp` = tudo visível ao desenvolvedor** (o consumidor da linguagem). É o que entra no **self-`.tkh`**
  do monólito e, portanto, o que o programa final enxerga ao linkar. Toda a superfície de API que o dev
  usa — tipos, funções, operadores, consts, `global`s — é `exp`. Regra prática: **se o dev pode escrever o
  nome, é `exp`**.
- **`pub` = visível só pelo próprio projeto/monólito** — entre namespaces do próprio Teko, **mas fora do
  self-`.tkh`**. É o encapsulamento de implementação: o dev nunca vê, o programa final não linka contra.
- **(sem `exp`/`pub`) = namespace-local** — visível entre **arquivos-irmãos do mesmo namespace** (o
  namespace = diretório), mas **não** por outros namespaces. **Não há nível file-local**: o mais estreito é o
  namespace. **Consequência real (ruling do dono 2026-08-15, confirmado pelo compilador):** dois `fn`/`type`
  nus homônimos em arquivos-irmãos do MESMO namespace **colidem** (overload ambíguo) — foi a causa do
  incidente da perna C (`rotl32`/`le_u32_at` entre `cipher.tks`/`hash.tks`/`mac.tks` no `teko::crypto` flat).
  Helpers internos compartilhados entre arquivos de um namespace precisam de **nome distinto** ou de uma
  ÚNICA definição; querer "file-local" não é possível.

**Consequência de triagem:** popular `exp` corretamente **é** o trabalho da expansão-da-stdlib (§11-antes);
a §11 depois só **formaliza/enforça**. O que hoje lê como `exp` por default (forward-compatible, tudo lê como
`exp` sem enforcement) precisa ser reclassificado item a item quando a §11 entrar: o que for detalhe de
implementação **desce para `pub`**; o resto **fica `exp` explícito**. Atrasar a §11 para depois da stdlib
minimiza esse retrabalho (a superfície já nasce triada).

**Amarra com o resto do Doc:** a Intent (`exp get` / `pub set`, §10.3), o `sizeof` (`exp global comptime`,
§14.2/§15), os operadores de interface (`operator __eq` exportado, §9.4) — todos já grafados sob esta regra.
`global` compõe direto: **`exp global …`** = superfície ambiente exportada (`println`, `sizeof`, `list`,
`str`); **`pub global …`** = global visível ao monólito mas fora do self-`.tkh`.

### 11.2 Grafo de dependências dos itens restantes — ordenação topológica (MANTIDO ATUALIZADO)

> **Regra do dono:** o item **mais dependido** cujas dependências estão **resolvidas (ou nulas)** é
> executado **primeiro**. Escopo: só os itens **restantes do Doc 2** — o **Doc 1** (backend/arena) vem
> inteiro **depois**, porque **o Doc 2 resolve as dependências do Doc 1**. Esta seção é **re-computada e
> atualizada a cada item que aterrissa** (marca ✅/🔄/⏳ + re-eleição do topo).

---

#### 🔄 ESTADO ATUAL (2026-08-17): EMISSÃO LIMPA — tabela de literais + fold + casts + tipagem forte (ANTES do §16)

**RULING DO DONO (2026-08-17):** puxado pra Doc-2 e colocado **antes do §16**, porque é a **causa-raiz única**
de quatro problemas — magic values no artefato, binário inchado (RODATA duplicado), cast à toa, e o **seed C
não cross-compilável** (que o §16 precisa resolver). História linear aqui, **sem arquivo paralelo** (o
antigo `plano-tabela-literais-e-casts.md` foi absorvido neste bloco).

**O problema (raiz).** O codegen emite constantes/literais de forma ingênua: **dobra** toda constante para o
valor no ponto de uso (`SYS_MUNMAP`→`((int64_t)11ULL)`), **não deduplica** literais (a string `"syscall2"`
sai 3× no seed), e emite **cast à toa**. Como o número dobrado é o do **host que emitiu**, o `bootstrap/teko.c`
— UM monólito compilado por `cc` em toda arquitetura/SO — carrega só o alvo do host e **quebra em arm64**.

**O modelo.**
1. **Tabela de literais estáticos.** Todo value-type imutável (primitiva, string, array de primitivas/strings)
   vira **uma entrada nomeada deduplicada**; todo uso emite **referência** (nunca o valor dobrado). Emissão C
   via `#define` (funciona em toda posição, inclusive tamanho de array fixo). Constantes de plataforma
   (`#os`/`#arch`) saem **gated por `#if`** → o monólito carrega todos os alvos e o `cc` escolhe. **O
   cross-compile do §16 é subcaso disto.**
2. **Regra de fold.** Dobra só quando **nada guardado** participa da expressão; **algo guardado** (qualquer
   entidade sob `#if`/`#os`/`#arch`, não só constante) **contamina qualquer fold**, tudo-ou-nada — os operandos
   value-type entram na tabela e a conta fica pro C/runtime.
3. **Conversões/casts (3 baldes).** Segura/custo-zero (widening numérico implícito; travessias de tipo nomeado
   `subtipo↔base`/enum/flag/`char↔u32`/`int→bigint`/`float→decimal`/`str→[]char` **com `to`**) · pode-perder/
   checada (via `teko::casting` **que já existe**, `T | error`) · desnecessária/eliminar (a **maioria** é tipo
   mal-casado na API — ex.: `i64` onde o domínio é `u64`/word — mais `literal to T` de inferência). **Não
   relaxa segurança**; a maioria dos casts some **arrumando os tipos** (criar tipos novos onde ajuda, ex.: word
   de 64 bits nos args de syscall).
4. **Convenção de tipagem forte.** A codebase do Teko é tipada explícita e fortemente, via a flag **`--explicit`**
   (o checker só barra `var` sem tipo **com** a flag; **default off**, pra não travar quem usa Teko). Os builds
   do Teko adotam a flag — recaída fica impossível (o build barra), inclusive as dos agentes.

**Sequência de trabalho (drain+reseed no fluxo normal; TESTES SÓ NO CI):**
1. ✅ **flag `--explicit`** (2026-08-17, reseed `bea94859`, drenado em `fix/retirement` `7c9f2935`) — via
   Env-threading: `Env.explicit` injetado no root, gate nos PONTOS DE INFERÊNCIA (diretriz do dono, não
   varredura). `explicit_binding_gate` em `type_binding` barra `var`/`const` **nomeado** sem tipo; **EXCLUI**
   aliases estruturais (match `as`, cabeçalho/var de loop — `type_loop` limpa a flag no preâmbulo), discard
   `_`, e destructure. **Retorno fora do escopo** (Teko não infere retorno de fn nomeada; ausência de `→ T` já
   é void completo); `const`/params/campos **já obrigatórios** no parser. `explicit_cast_gate` (o gate C):
   conversão desnecessária vira **ERRO** sob a flag e **silenciosa** no default (o warning `warn_redundant_cast`
   foi aposentado; o `TCast` fica, o teto D4 ainda conta). **Default OFF** — o checker nunca barra sem a flag
   (inferência segue recurso da linguagem). Fixpoint **LIMPO** (tc1==tc2==tc3), MEM_PARANOID exit 0. Ainda
   **NÃO ligada** em nenhum build (aguarda a codebase 100% tipada — passo 3).
2. 🔄 **varredura de tipagem** — DIMENSIONADA (2026-08-17): ~**14.131** `var` sem tipo na codebase
   (concentração: `lir/lower.tks` 1319, `codegen.tks` 1031, `typer.tks` 784, `project.tks` 446, `emit/
   tkb_read.tks` 365, …). Escala massiva → **DECISÃO DE ESTRATÉGIA DO DONO PENDENTE**: (A) construir uma
   FERRAMENTA de auto-anotação — o compilador já infere o tipo de cada `var`; um codemod insere
   `: <tipo>` preservando formatação, roda uma vez sobre toda a árvore (risco: mapear tipo-INTERNO do
   checker → tipo de SUPERFÍCIE grafável); (B) faseado por subsistema com agentes (inviável 100% manual
   a 14k); (C) híbrido — a ferramenta gera o rascunho, agentes revisam por área. Recomendação do
   coordenador: **(A)** ou **(C)**. NÃO despachar em massa sem a estratégia ratificada. Nota: tipar uma
   `var` não deve mudar os bytes emitidos (o tipo anotado = o já inferido) → provável **sem reseed** por
   arquivo-folha; confirmar no primeiro lote.
   - **SPIKE DE VIABILIDADE (2026-08-17) — viabilidade RESOLVIDA, aguarda só o A/B/C do dono.** Varredura
     full-tree instrumentada: **10.609** bindings `var`/`const` sem tipo (a população exata que o gate
     `--explicit` marca; menor que o ~14.131 textual porque este inclui vars de cabeçalho-de-loop — que o
     gate limpa — + destructure + consts de módulo tipados por outro caminho). Todos os 10.609 são sites
     distintos (sem duplo-count de monomorfização). Split: **EASY 10.444 (98,4%)** — prim/base 4534, tipo
     nomeado 3884, slice `[]T` 1715, união-anônima `A|B` 311; **GENERIC instance 164 (1,5%)** (precisa de
     renderer `base<args>` a partir dos type-args estruturados; ~93% em `src/parser/` via `Parsed<T>`,
     concentrados em ~13 arquivos); **FUNC/closure 1**; **HARD/não-grafável 0** — as formas internal-only
     (`Ref<T>`, slice-sentinela `[]_`, `void`/`null` puros) **não podem ocorrer em binding sem tipo**: o
     `type_binding` já as rejeita independentemente da flag. **167 de 180 arquivos são 100% EASY.**
   - **Hipótese sem-reseed CONFIRMADA:** protótipo (codemod) tipou `src/math/checked.tks` (16 vars) → árvore
     recompilou VERDE (checker→codegen→cc, backend C) e o `teko.c` emitido ficou **byte-idêntico**
     (22.619.975 B, sha256 `bea84859…` antes=depois). Anotar com o tipo já-inferido muda zero bytes → reseed
     é **um único passo whole-tree**, não por-arquivo.
   - **Recomendação do spike:** **(A) puro-ferramenta, mas a ferramenta OBRIGA um renderer de superfície
     grafável** (distinto do `type_render`, que é diagnóstico) divergindo em 4 pontos: generic instance
     `base<args>` (dos type-args estruturados, NÃO de-manglando a string achatada), `ptr<T>`,
     `func<>`/`action<>`, e `ns::Name` cross-namespace. Com esse renderer o tool cobre ~100% (mecânico, sem
     julgamento). Única razão p/ (C) híbrido: **review humano de estilo bounded aos ~13 arquivos genéricos**
     do parser (as uniões demanglada ficam verbosas, ex. `Parsed<Binding | Assign | …>` — chamada de
     legibilidade, não de correção). Rota: **construir o tool (A) com o renderer genérico → rodar tree-wide
     → review C-style leve nos ~13 arquivos → 1 reseed whole-tree.**
   - **⚠️ Achado adjacente (infra, fora do escopo do sweep):** o `.teko/teko` commitado está **STALE** vs
     `fix/retirement` (é anterior aos tipos `func<>`, não compila o branch) e `fetch_teko.sh` é inutilizável
     aqui sem `GH_TOKEN` válido. Todo agente neste branch bate na mesma parede e precisa hand-buildar
     `bootstrap/teko.c`. Vale o integrador refrescar o seed commitado.
3. ⏳ **ligar `--explicit` nos builds + CI** (SÓ após 100% tipado, senão o build quebra) + atualizar memórias
   (o build seco passa a incluir `--explicit`).
4. ⏳ **tabela de literais** — emitir por referência + dedup + gating `#if` das guardadas → **reseed**.
5. ⏳ **casts** — os 3 baldes; criar tipos que evitam conversão; estender `teko::casting` onde falta.

---

**Arestas (X → Y = X depende de Y), extraídas do texto:**
- §9.4 (interface-obriga-operador) → **§9** (habilitado pelo overload de operador, l.563)
- §10 Intent → **§9** (properties get/set, l.906) e → **§13** (o `pub set` acerta a mesma instância, l.910/1069)
- §10 concorrência → **§7 + §9.2b (solver de forma) + conformidade-estática-de-interface** (os 3 pré-reqs, §12 l.1134-1138) e → **§13** (Ctx, l.1144)
- §7 DI → **§9.2b + conformidade-de-interface** · §9.2b → **§9 + conformidade-de-interface**
- §17 → **§11** · **§16 → §17** (FFI por-target precisa de `#os`/`#arch`; ruling dono "17 antes de 16") **→ §11** · §11 → expansão-da-stdlib
- §10 concorrência → **keystone genéricos (#254)** (`chan<T>`/`Intent<T>` são genéricos) + **runtime de threads**; ruling dono: **100% antes do Doc-1**

**Estado (2026-08-15):**

| Item | dependido por | in-deg | deps resolvidas? | status impl |
|------|---------------|:---:|:---:|-------------|
| §9 operador (9-ops) | §9.4, §10, §9.2b | 3 | ✅ | ✅ entregue (parse+dispatch+counterpart §9.4; fixtures) |
| conformidade-estática-de-interface | §7, §10, §9.2b | 3 | ✅ | ✅ largely done (`type_conforms_to`, vtable, IService auto-conf) |
| §9.2b solver de constraint de forma | §7, §10 | 2 | ✅ | ✅ **ENTREGUE** — notnull + form-words/`service [lifetime]` + disjunção (drenado em `fix/retirement`, `cf0c70b5`) |
| **§13 item-14 fat-header** | §10 (Intent/Ctx) | 1 | ✅ (§1,§4.1) | ✅ **ENTREGUE** (2026-08-15) — fat header `tk_struct_hdr` (crumb 5) + layout FAT (6a) + receiver-as-ref/`self` gruda (6b) + fixtures A1-A4 + reseed; drenado por ff em `fix/retirement` `6f4d78ba`; sweep 32/32 verde |
| §7 DI service/svc | §10 | 1 | ✅ | ✅ **Part A ENTREGUE** (2026-08-15, `0a246dfe`) — kind `service`+`ServiceLifetime`, `svc`/`has_svc` comp-time, gate `IService`, conserto da crash `svc<iface>`, DI-anotação aposentada, **costura `svc_scope_expr(lifetime, regions)` congelada** (program-root); fixture `service_svc` 11/11 parity, fixpoint inert, lanes verdes. **Part B (binding arena por-thread) = costura deixada p/ Doc 1 §8.** |
| **§14 Família B (comptime)** | — | 0 | ✅ | ✅ **ENTREGUE** (engine B2–B5 já aterrissada `da82d0bd`/`583707d7`/`3b8d9b0c`/`f3768297`/`a0586372`; fixtures `comptime_expand`/`reflect`/`fields`; fixpoint tri-gen byte-id). **Açúcar de alcance `exp global comptime sizeof/typename/fields` pende §15 `global`.** |
| **§15 global** | (§14 sugar, §11) | — | ✅ | ✅ **Mecanismo ENTREGUE** (2026-08-15, `21bbe7ae`) — `global` no lexer/parser/AST (`is_global`), resolução sem-namespace (bare), colisão de assinatura/tipo, composição `exp global comptime` (destrava açúcar §14); byte-idêntico (fixpoint tri-gen), fixtures `global_access`(39)/`global_reject`(4). **Migração shadow→global por-símbolo = costura gated na stdlib.** |
| §11 visibilidade `exp`/`pub` | §16, §17 | 2 | ⏳ (stdlib) | ⏳ ordem: PENÚLTIMA |
| §10 concorrência | — | 0 | ❌ | ⏳ design-only (sink) |
| §16 FFI libc-direct | — | 0 | ⏳ (§11) | ⏳ ordem: ÚLTIMA |
| §17 `#if`/`#os`/`#arch` | — | 0 | ⏳ (§11) | ⏳ ordem: ÚLTIMA |

**RULING DO DONO (2026-08-15): finalizar TODA esta lista (as porções Doc 2 dos 7 itens) ANTES de tocar o
Doc 1.** Não descer pro Doc 1 no primeiro item que encosta na arena. Para os itens cuja *substância* é
arena (**§7 Parte B**, **§10**), "finalizar" = fazer a porção Doc 2 + deixar a **costura nomeada**
(`svc_scope_expr` etc.) como o ponto exato de encaixe do Doc 1 §8. Só quando TODAS as porções Doc-2
estiverem feitas é que o Doc 1 entra (o dono analisa antes).

**CAUDA ENTANGLED — sem próximo mecanismo limpo (2026-08-15, aguarda decisão do dono).** Os 6 itens de
mecanismo-limpo estão entregues. Os 4 restantes NÃO têm deps resolvidas sem uma decisão de sequenciamento:
- **§11 `exp`/`pub`** — deferido-por-último pelo dono (alto churn); depende da **expansão da stdlib**. O
  MECANISMO já existe (`src/emit/header.tks`/`tkh.tks` `emit_tkh`/`read_tkh`, `parser::Visibility`
  private/pub/exp, "only exp reaches the `.tkh`"); falta **ativar enforcement** + **triagem item-a-item**
  (gated na stdlib pronta). Modelo corrigido em §11.1 (sem `exp`/`pub` = **namespace-local**, não file-local).
- **§16 — TROCAR TODAS as dependências C por Teko + FFI-do-SO (ruling do dono 2026-08-15, "sem exceções ou
  desculpas").** §16 = **substituir `src/runtime/teko_rt.c`/`teko_rt.h`/`win32_compat.h` + `src/assert/assert.c`
  INTEIROS** — as duas metades: o **shim de SO** (I/O/syscalls/`win32_compat`) E o **alocador de arena**
  (`tk_alloc`/`tk_region_root`) — MAIS o **`assert.c`** (ruling do dono 2026-08-16: "assert.c é outro que ao
  removermos as deps C precisará ser substituído também" — hoje linkado em TODO artefato ao lado do `teko_rt.c`,
  logo cai no "SEM EXCEÇÕES") — por **Teko sobre FFI-do-SO** (a arena vira Teko sobre `mmap`/syscalls do target).
  **Reescrever o que for necessário, sem exceção.** Deps: **§16 → §17** (`#os`/`#arch` pra selecionar o FFI
  por-target — "17 antes de 16") **→ §11**. **TUDO isso é Doc-2, PRÉ-Doc-1.** (Correção: eu tinha inventado um
  "split §16 / arena=Doc-1 / tensão de native-gate" — ERRADO. A Doc-1 NÃO reconstrói memória; a Doc-1 só
  **MELHORA** a arena-Teko que o §16 já produziu. Sem circularidade.) **NOTA §10:** o `-pthread` transitório
  (spawn C0a) e o `#include <pthread.h>` em `teko_rt.c` também são deps-C que o §16 aposenta — a criação-de-thread
  vai a FFI-do-SO (`clone`/`pthread` via `extern`, per-target) junto com o resto.
- **§17 `#if`/`#os`/`#arch`** — `#os` já existe; falta `#arch`/`#if`; gated no §11; **habilita o §16** (vem
  antes dele).
- **§10 concorrência — 100% DESIGN, 0% IMPLEMENTAÇÃO (checado no código 2026-08-15).** Design fechado
  (§10.5 modelo + §10.6 ref/UAF). Implementação = ZERO: não há `spawn`/`chan`/`await`/`Intent` no
  lexer/parser nem namespace `teko::threads` — só docs de design + probes (`examples/probes/chan_dgram`,
  `spawn_redirected_probe`). Falta TODA a superfície: `spawn`→thread-do-SO, `chan<T>` MPSC + `IChannelKind<T>`
  + `OsChan`/`MemChan`, `await`→`Intent<T>`, atribuição-múltipla, `teko::threads`, regras de checker (ref não
  cruza fronteira, `<ref T>` rejeitado, transporte singleton). **NÃO é monomórfico** — `chan<T>`/`Intent<T>`
  são **genéricos** → dependem do **keystone de genéricos (#254)** + de um **runtime de threads**. **RULING
  DO DONO (2026-08-15): §10 tem que estar 100% ANTES do Doc-1** (Doc-1 precisa de tudo pronto p/ menor
  impacto). **CONSEQUÊNCIA (fork p/ o dono): os genéricos (keystone) teriam que entrar ANTES do Doc-1** pra
  habilitar o §10 — o que os move de "gated/depois" pra "antes do Doc-1". Aguarda confirmação da sequência.

**DECISÃO DO DONO (2026-08-15): (A) — expandir a stdlib.** Escopo = a árvore autoritativa §1.5 de
`plano-stdlib-catalogo-expansao.md` (8+ áreas). **CORREÇÃO DO DONO (2026-08-15): a Doc 2 / a expansão da
stdlib NÃO é "monomórfica-só" — o dono NUNCA afirmou isso.** O enquadramento "fatia monomórfica (sem
keystone/#254/FFI)" era caracterização MINHA (coordenador) e está RETIRADO — ele indevidamente empurrava
genéricos e §10 pra "depois", contra o ruling do dono de que **§10 tem que estar 100% antes do Doc-1** (e §10
exige genéricos). Escopo real antes do Doc-1: a stdlib §1.5 INTEIRA (incl. as partes genéricas —
`collections`/`sort<T:Ord>`) + o **keystone de genéricos (#254)** + **§10 concorrência** + §11/§17.
**Sequência de execução (praticidade, não restrição de escopo):** as lanes que POR ACASO são puro-Teco-sobre-
`[]byte` (crypto/encoding) já estão rodando e drenando primeiro por já estarem prontas para implementar;
`crypto` (hash→mac→kdf→cipher→aead) ✅ → `sort` ✅ → `encoding`
(json✅·xml✅·cbor✅·bson✅·msgpack✅·base64/url/mime✅ · csv·toml·ini·yaml·protobuf·asn1·fixed) → `bigint` ✅ →
document-signing (jose✅·xmldsig✅·cose🔄) → **keystone de genéricos (#254)** → `collections`/`sort<T:Ord>` →
**§10 concorrência** (`spawn`/`chan<T>`/`await`/`Intent<T>`/`teko::threads`) → §11 → **§17 → §16 (FFI-do-SO,
remove teko_rt)** → **stdlib que depende de FFI** (`crypto::rand`/`openssl`/`gpg` · `net` · `db` · `odbc` ·
`rpc`). ~~→ `ui`~~ (**`ui` REMOVIDO do escopo — ruling dono 2026-08-16, ver abaixo**). **SÓ ENTÃO o Doc-1.**
**CORREÇÃO DO DONO (2026-08-15): NADA disso é "pós-Doc-1". É PRÉ-Doc-1.** Eu tinha posto §16/FFI-stdlib/`ui`
como pós-Doc-1 — ERRADO. **Divisão definitiva (ruling do dono): a Doc-2 TROCA TODAS as dependências C** por
Teko + FFI-do-SO (reescrevendo o que for necessário, **sem exceções ou desculpas** — inclui reimplementar o
alocador de arena em Teko sobre `mmap`/syscalls); **a Doc-1 apenas MELHORA** essa arena-Teko (otimização, NÃO
reconstrução). Doc-2 é grande (toda a linguagem + stdlib completa + FFI + `ui` + a reescrita do runtime C→Teko)
e é PRÉ-Doc-1 por inteiro; a Doc-1 é o passo final e focado de **melhoria da arena**, sobre um terreno 100%
pronto e já livre de C.

**⚖️ `ui` REMOVIDO DO ESCOPO (ruling dono 2026-08-16).** O `ui` era uma SUGESTÃO; sai da Doc-2 (e de todo o
roadmap). Racional do dono: "com o poderio do FFI o desenvolvedor já conseguirá usar uma integração por ele
mesmo para isso" — com a FFI-em-runtime (`dlopen`/`dlsym`) e a ABI-nativa-do-SO que o §16 entrega, o dev integra
o toolkit que quiser (GTK/Qt/web/nativo) por conta própria; Teko NÃO embarca um `ui` na stdlib. ⇒ o terreno Doc-2
termina em **FFI-stdlib** (rand/openssl/gpg/net/db/rpc); sem `ui`.

**Progresso stdlib monomórfica — NÚCLEO CRYPTO ✅ COMPLETO:** `hash` ✅ (`f861db43`) · `mac` ✅ (`6f6d5ca4`)
· `kdf` ✅ (`e804f474`) · `cipher` ✅ (`d5299449` — AES 128/192/256 {CBC,CTR,CFB,OFB}, ChaCha20,
cmac_aes/gmac_aes) · `aead` ✅ (`92f48fe6` — AES-GCM, ChaCha20-Poly1305, AES-CCM).

**ASSINATURA DE DOCUMENTO ✅ TRIO COMPLETO (ruling do dono 2026-08-15 — JSON e XML precisam assinar).** No
catálogo §1.5: **`crypto::jose` ✅** (JWS/JWK/JWT sobre JSON, `6473dfd4`) · **`crypto::xmldsig` ✅** (XML-DSig +
C14N sobre XML, `a1355a57`) · **`crypto::cose` ✅** (COSE_Sign1/Mac0 sobre CBOR, `68d88e9f`). Cobre HS/RS/PS/ES256
/EdDSA (JOSE), rsa-sha256/ecdsa-sha256/hmac-sha256 (XML-DSig), ES256/EdDSA/RS256/PS256/HMAC (COSE). Cross-check
byte-exato vs PyJWT/signxml/pycose+cryptography. **Intento do dono (JSON e XML assinam documento) SATISFEITO**
+ CBOR de bônus. Follow-ons documentados: JWE (RSA-OAEP/ECDH), COSE_Sign multi-signer/COSE_Key, exclusive-C14N. Cadeia de dep (tudo monomórfico, cabe na Fase 1): **JWS/HMAC (HS256)** já
dá agora (HMAC ✅ + base64 + JSON ✅); **JWS/assinatura + XML-DSig + COSE** precisam de `math` (bigint) →
`crypto::pk` (RSA-PSS/ECDSA-P256/Ed25519) → `encoding` (base64/xml/cbor) → aí `jose`/`xmldsig`/`cose`.

**✅ `crypto` 3des/DES ENTREGUE (`10630a43`).** DES single-block (IP/FP/Feistel/S-boxes/PC-1/PC-2, tudo u64/u32)
+ 3DES/TDEA EDE (3-key e 2-key, rejeição de chave fraca) + modos ECB/CBC (seam de bloco) + PKCS#7; legacy/Sweet32
documentado. **Validação COMPORTAMENTAL** (binário compilado rodado, não `teko test .`): DES KAT `3fa40e8a984d4815`,
TDES ECB `fbe62b68…`, CBC + roundtrip — todos byte-exato vs pycryptodome 3.23/FIPS-46-3.
**🏁 STDLIB "PRONTA-AGORA" COMPLETA:** crypto (core/pk/password/3des) · encoding (15 fmt) · sort · bigint · +
o bug de compilador #4 corrigido. **Próxima fase = GENÉRICOS** (compiler-touching; impl dos crumbs desenhados).

**▶️ FASE GENÉRICOS ABERTA — 9-ops keystone despachado (branch `feat/9ops-keystone`).** Scout mapeou o terreno
(tudo design-ahead, 0 blockers externos — §9/§9A/#254 aterrissados): (1) **9-ops** (8 crumbs,
`plano-9ops-…-0.3.1.md`) — operador em struct/class/interface + dispatch genérico `==`/`<` sobre `T: IEq/IOrd`,
reusa #254+monomorph sem maquinaria nova; é a RAIZ que destrava tudo a jusante. (2) **generic-stack-completion**
(5 gaps residuais de classe genérica, `generic-stack-completion.md`, ordem `#3→#1→#4+#5‖#2`, #1=dispatch de
trait estrutural sobre `K: Hashable`, o flagship). (3) **collections-generics-fase1b** (`Dictionary<K:IEq&IHash,V>`,
`HashSet`, `SortedSet`, `PriorityQueue`, `SortedDictionary` — `collections-generics-fase1b-crumbs.md`, GATED no
reseed do 9-ops, stdlib pura). Hoje: `List<T>` shipa; `Map<V>` str-keyed com 3 workarounds (= gaps #1/#2/#3 do
generic-stack). Colisões-irmãs do #4 (`Base__g__<arg>`, `TK_E_<E>_<M>`) são latentes mas NENHUM crumb de
genéricos as exercita — sem fix antes. Implementer entrega crumbs 1-7 (parser→dispatch→corpus IEq/IOrd→fixtures);
o coordenador faz o crumb 8 (fixpoint gen2==gen3 + reseed) no drain, padrão compiler-touching.

**✅ 9-ops KEYSTONE COMPLETO (`b778b7d1`) — SEM reseed, e por quê (correção da minha própria moldura).** O
implementer descobriu que a MAQUINARIA do 9-ops (parser `b4598494`, checker `9c374457`, dispatch-genérico
`3cc60c2a`) já eram ANCESTRAIS do seed atual `e98fd5b0` (o reseed do #4) — ou seja, **já estava seedada**. O
corpus `src/cmp/cmp.tks` (IEq/IOrd + NeByEq/GtByLt/LeByLt/GeByLt) fora DROPADO em `07588731` só porque
_precedia_ esse reseed; agora compila. Logo o único delta do drain é o **corpus, um módulo-FOLHA de stdlib**
que o compilador não consome. **Regra do dono: "módulos-folha não exigem reseed; mudança de compilador SIM."**
Confirmado que NÃO há gate seed-vs-emissão: `provenance_gate.sh` só julga um SWAP de `bootstrap/teko.c`; seed
INALTERADO = PASS trivial. Então NÃO reseei — deixar o seed como está é correto e CI-safe (o C-leg constrói
gen0 do seed e compila a árvore verde). **Validação build-real do coordenador (mais forte que o exigido):**
rodei a cadeia C-route completa (cc `bootstrap/teko.c`→gen0→emite tc1→gen1→tc2→gen2→tc3) sob `ulimit -v
6291456` — **tc1==tc2==tc3 byte-idêntico**, sha `d33a61863b2b031551cfc8bdd5d5af419bbec521f7b9d79cd92d09f7295cb709`
(a árvore-com-corpus AUTO-HOSPEDA byte-idêntico). Fixtures (crumb 7, já ancestrais): `capability_iface`
(struct `__eq`, `index_of<T:IEq>`, `min2<T:IOrd>`, `NeByEq` joint), `operator_overload_compose` (overload de
método E operador across-composition), 5/5 diagnósticos rejeitam com a mensagem esperada. **Byte-diff do corpus
puro** (implementer): +163 bytes no `teko.c` = exatamente os 2 typedefs-portadores `tk_t_teko__cmp__IEq`/`IOrd`,
ZERO funções estampadas (contagem 7173 imutável) — os contratos/mixins são inertes até USADOS por um genérico
monomorfizado. **DESTRAVADO agora:** `sort<T:IOrd>` genérico e a migração `Map<K:IEq&IHash,V>` (a metade IEq
entregue; falta `IHash` = interface de MÉTODO `fn hash():u64`, fora do escopo 9-ops) — âmbito stdlib adjacente.
**LIMITE documentado (T-1):** um value-type (`type Ms = i64 {…}`) NÃO compõe trait-mixins — escreve os quatro
operadores de `IOrd` à mão (síntese estrutural continua aposentada). Próximo: generic-stack-completion (agora
desbloqueado) → collections-generics-fase1b.

**✅ generic-stack-completion GAP #3 ENTREGUE + RESEED (`b300ee6a` fix, reseed neste commit).** O re-key
ROOT-DISPATCH: o corpo de um método-de-instância genérico estampado que chama um IRMÃO em `self` (ex.
`self.count()` dentro de `Ctr__g__i64::twice`) agora RE-CHAVEIA do dono-template abstrato para o namespace da
instância concreta — o símbolo C (`..__Ctr__g__i64__count`) casa a definição em vez de falhar no link como
`..__ct__Ctr__count`. Novas `mono_rekey_call_ns` + `mono_rekey_callee_qualifier` (`src/checker/monomorph.tks`),
ligadas nos dois braços TCall de `mono_texpr`. Grounding-gap confirmado CANÔNICO (não bare), como o design
previu. É o substrato que o gap #1 (flagship, dispatch de trait-estrutural sobre `K`) reusa. **MUDANÇA DE
COMPILADOR → RESEED (regra do dono).** Novo seed `bootstrap/teko.c` sha
`481ebae5dfefd467b153425459f91de56a8c42998a6f05c748f1b71583479547`. O byte-diff vs seed anterior NÃO é inerte
(diverge de `d33a6186`) mas SEM divergência comportamental: (a) as 2 funções novas + 2 sítios de wiring; (b)
shifts mecânicos de literais de source-location (`_tk_cast_loc_line`/`tk_panic_oob_at` — o compilador embute
linha/col para panics; inserir ~50 linhas em monomorph.tks os desloca); (c) os módulos-folha acumulados desde o
seed #4 (3des/password/cmp, dobrados aqui pois o seed derivara atrás deles). **GATE self-reproduce:** cadeia
C-route gen0(seed anterior `898dc030`)→tc1→gen1→tc2→gen2→tc3, **tc1==tc2==tc3==`481ebae5`** byte-idêntico (o
re-key NUNCA dispara no corpus — zero instâncias genéricas — logo compilador VELHO e NOVO emitem o self-image
idêntico; o ponto-fixo é atingido em tc1). `provenance_gate.sh` PASS. Regressão
`examples/regressions/generic_sibling_method/` (`Ctr<i64>::make().twice()==6`) LINK-falha no gen0 do seed
anterior e builda+roda verde (exit 0) no compilador reseedado — a forma exata que falhava. **Achado colateral:**
o OOP-hard-cut (D27) JÁ ATERRISSOU no lane (`self` reservado, factories `static fn`, self-construct `.{}`) — o
snippet de fixture do design era pré-hard-cut e foi modernizado. Fold-in de collections (voltar `arr_*` a métodos
privados de instância) DESTRAVADO mas DEFERIDO ao follow-up #163. Próximo: gap #1 (flagship, reusa este re-key).

**⚠️ CORREÇÃO DE ROTA — gap #1 do generic-stack-completion é OBSOLETO (não apenas bloqueado).** Ao despachar o
gap #1 (dispatch de trait-estrutural sobre `K: Hashable & Eq`), o implementer HALTOU com um achado airtight: a
premissa do design ("a síntese estrutural já existe, `synth.tks`") é **FALSA no `fix/retirement`**. O commit
`1aae1145` (2026-08-13) **APOSENTOU deliberadamente** toda a maquinaria de síntese estrutural — deletou
`src/checker/synth.tks` (668 linhas), removeu `is_structural_trait`/`synthesize_structural_methods`, o braço
estrutural de `atom_surface`/`constraint_interfaces`. Verificado: `synth.tks` AUSENTE, ZERO refs vivas. **Causa
raiz:** `generic-stack-completion.md` foi desenhado em **2026-08-11** (última edição), contra o `main`
pré-aposentadoria — ZERO referências a 9-ops/IEq/IOrd. O lane DIVERGIU: (1) aposentou o modelo trait-estrutural
(`Eq`/`Hash`/`Hashable` auto-derivados) em 08-13; (2) no dia seguinte (08-14) ADICIONOU o modelo
interface-capability do **9-ops** (`IEq`/`IOrd` + dispatch genérico de operador sobre `T: IEq`). **Gap #1 é o
modelo velho; o 9-ops o SUPERA.** Un-aposentar `synth.tks` reverteria a decisão de aposentadoria do próprio dono
— NÃO farei isso. **Caminho correto = o design ATUAL do lane: `collections-generics-fase1b-crumbs.md`**, que é
construído sobre interfaces 9-ops: G1 fornece `IHash` ("a metade de hash que o 9-ops NÃO dá — ele só entrega
operadores IEq/IOrd"), dispatch via **#254 method-over-`T`** (VIVO, `typer.tks:1107/2417/…`), `str_hash` já existe
(`teko_rt.tks:529`, FNV-1a). `Map<K: IEq & IHash>` precisa de IEq (9-ops ✓) + IHash (fase1b G1) + #254 (✓) + o
re-key do gap #3 (✓ aterrissado) — **NÃO precisa do gap #1.** **Gap #3 permanece válido** (era ROOT-DISPATCH,
ortogonal à síntese — já entregue). **Gaps #2/#4/#5** (ROOT-NS/codegen) a AVALIAR: podem ainda ser bugs reais na
fase1b (G3+ Dictionary/HashSet como param de free-fn = gap #4, factory cross-ns = gap #5; fase1b usa arrays
paralelos p/ ESQUIVAR o #2). Despachando arquiteto para reconciliar o design velho contra o lane atual e liberar
a fase1b com âncoras frescas. **Task-6 (generic-stack-completion "5 gaps") re-escopada: só gap #3 era aplicável;
#1 obsoleto; #2/#4/#5 sob avaliação como pré-reqs pontuais da fase1b.**

**✅ collections-generics-fase1b G1-G5 + reject ENTREGUE (`f177be09`) — SEM reseed (corpus-folha).** Payoff dos
genéricos: `IHash` (`fn hash():u64`, em `src/cmp/`) + chaves `StrKey`/`I64Key` (IEq&IHash&IOrd+mixins; I64Key.hash
zig-zag sign-safe) + combinadores `arr_*`/`sorted_insert`/`heap_*` → **`Dictionary<K:IEq&IHash,V>`** +
`dict_find_index` → **`HashSet<T:IEq&IHash>`** → **família ordenada** `SortedSet<T:IOrd>`/`PriorityQueue<T:IOrd>`/
`SortedDictionary<K:IOrd,V>`. `Map<V>` INTACTO (str-keyed, `teko::env` depende). **Validação build-real do
coordenador:** gen0(seed `481ebae5`) compila a árvore drenada VERDE, emissão `c6dacf4d` (== relatado pelo
implementer), binário ok; implementer confirmou FIXPOINT gen1==gen2==`c6dacf4d`. **Corpus-folha → sem reseed** (o
seed constrói a árvore; a emissão só cresce por 14 conformers CONCRETOS StrKey/I64Key + `arr_drop_u64_at`, não
genéricos estampados — 0 funções genéricas de coleção estampadas; sem gate seed-vs-emissão). Fixtures rodam verde
(7 cenários exit 0: dict/hashset/sorted roundtrips com `<StrKey>` e `<I64Key>`; reject `dict_key_no_ihash`
falha-ao-compilar com o diagnóstico de constraint). W15 limpo, `bootstrap/` intocado.

**⏳ G6 (`Map.to_dictionary`) PARADO — trip GENUÍNO do gap #2 (contra a reconciliação).** `Dictionary<StrKey, V>::make()`
com o type-param `V` do dono nos type-args da FACTORY → "unknown function: make" (a reconciliação só analisou a
forma struct-literal `StrKey{}`, perdeu a forma factory-call). **3 achados de maquinaria adjacentes** (implementer,
sob TRIAGE de arquiteto): **F1** type-param constrangido por interface não pode ser membro de união (`T|null` com
`T:IOrd` → "an interface cannot be a variant member yet", `resolve.tks:2134`; workaround guard-based entregue —
`dequeue`/`pop` retornam `T` bare, guardados por `is_empty()`, fail-loud); **F2** re-key de monomorph erra quando
o MESMO free-generic é instanciado no type-param (struct) E num `[]u64` concreto (workaround `arr_drop_u64_at`
concreto); **F3=gap#2** a factory genérica com type-param do dono (bloqueia G6); **F4** constraints de instanciação
de CLASSE não são checadas no mono (só free-generic) — `Dictionary<Plain>` link-falha em vez de diagnóstico limpo.
Arquiteto triando qual é fix-agora vs workaround-aceitável vs ruling-do-dono, com plano ordenado. **Adjacente
destravado, NÃO construído:** `sort<T:IOrd>` genérico, `teko::env`→Map. Próximo: triage → fixes de maquinaria →
G6 → sort<T:IOrd> → §10 concorrência.

**✅ TRIAGE das 4 achados: todos in-scope, NENHUM ruling-do-dono** (`fase1b-machinery-findings-triage.md`).
**✅ F1 + F4 ENTREGUES + RESEED (`8611a99e`/`3e874682`, reseed neste commit).** **F1** (checker,
`resolve.tks`): um type-param constrangido (`T:IOrd`, superfície InterfaceBody) agora é ADMITIDO como membro de
união-null (`T|null`) — antes `variant_member_admissible` o rejeitava como "an interface cannot be a variant
member yet". Novo predicado `is_type_param_named` (exatamente 1 constraint, 0 type-params próprios) estreita o
gate: um VALOR de interface genuíno numa união continua rejeitado (é a feature #28 diferida); só o falso-positivo
no `T` constrangido é liberado. Destrava `dequeue():T|null`/`pop():T|null`. **F4** (checker/monomorph,
`monomorph.tks`): instanciações de CLASSE genérica agora são constraint-checadas no monomorph
(`check_instance_constraints`) — `Dictionary<Plain,i64>` sem `IHash` FALHA com diagnóstico limpo em vez de
link-error `Plain__hash`. Fail-loud. **RESEED:** F1+F4 NO-OPam o corpus do compilador → seed novo `3683d71e`;
byte-diff mecânico (2 funções novas + gate + call + shifts de source-loc; sem divergência comportamental).
**GATE self-reproduce:** cadeia C-route gen0(seed `481ebae5`)→tc1→gen1→tc2→gen2→tc3, **tc1==tc2==tc3==`3683d71e`**
byte-idêntico; `provenance_gate.sh` PASS. Regressões: `type_param_union_return` (F1, exit 7) +
`class_key_no_ihash` (F4, falha-limpa no monomorph). **F2 REPRODUZ (triage errou o "não reproduz"):** bug de
inferência genérica na FASE-CHECKER, dependente de ORDEM — mesmo free-generic chamado num `[]u64` concreto ANTES
do `[]T` (type-param) dispara "argument type mismatch"; `HashSet.remove` (hashes[]u64 primeiro) tripa,
`Map`/`Dictionary` (keys primeiro) não. Workaround `arr_drop_u64_at` concreto MANTIDO (são), bug rastreado (não
bloqueia nada). Próximo: **batch B = F3** (factory com type-param do dono, `retarget_generic_static_callee` +
mono `subst_instance_name` fall-through) → adota **G6** (`Map.to_dictionary`) → depois `sort<T:IOrd>` → §10.

**✅ F3 + G6 ENTREGUES (`da075b18` F3 + reseed `c25a9f96`; `e6da376e` G6 leaf).** **F3** (checker+mono): uma
chamada de factory/método estático genérico cujos owner-type-args incluem um type-param ABSTRATO
(`Dictionary<StrKey,V>::make()` dentro de `Map<V>::to_dictionary`) resolvia o owner a um PHANTOM
(`Dictionary__g__StrKey__V`) sem `make` registrado → "unknown function: make". **Correção do implementer (o crumb
F3.1 do triage era PROVAVELMENTE INCOMPLETO** — "deixar na base" tipava o resultado como o phantom do TEMPLATE
`__g__K__V`, quebrando `d.insert`): novo `type_phantom_instance_call` (`typer.tks`) resolve a assinatura contra o
TEMPLATE e a especializa com a subst que o nome-phantom codifica (`phantom_owner_subst`/`split_phantom_args`/
`resolve_phantom_arg_token` + `qualified_type_name_by_last` p/ chave cross-ns), ligado em `type_call` + desugar de
`type_method_call`; mono re-key (`monomorph.tks:407/428`) cai o miss exato p/ `subst_instance_name` (+
`rekey_phantom_qualifier`), concretizando `__g__StrKey__V→__g__StrKey__i64`. F3.3 discovery: SEM enqueue novo (o
re-scan de `stamp_inst_site` já auto-descobre). **G6** (leaf, `src/collections/map.tks` +`to_dictionary():
Dictionary<teko::cmp::StrKey,V>`; Map fica str-keyed, `teko::env` intacto). **RESEED 2-etapas (disciplina):** F3 é
mudança de compilador → reseed a **seedF3 `3d98db74`** (fixpoint tc1==tc2==tc3, gate PASS; gen0 do seed anterior
`3683d71e` compila o FONTE F3 pois o corpus não usa o feature); **G6 é FOLHA → SEM reseed próprio** (o seed
F3-capaz constrói G6). Validação: gen0(seedF3) compila a árvore F3+G6 VERDE, emissão `ac9a9976` (== fixpoint
gen2==gen3 do implementer). Fixtures: `generic_factory_owner_param` (F3, exit 47), `map_to_dictionary` (G6, exit
77). **API de coleções genéricas COMPLETA.** **Limitações adjacentes reportadas (rastreadas, não bloqueiam):**
(a) factory estático QUALIFICADA cross-ns (`ns::Type<…>::make()`) ainda perde o qualifier em
`retarget_generic_static_callee` (primo do gap #5); (b) o gap #2 forma-CONSTRUCT-aninhado continua latente (F3
fechou só a forma factory-call). **GENÉRICOS COMPLETOS** (resta só F2 checker-order-bug com workaround são +
limitações adjacentes rastreadas). Próximo: `sort<T:IOrd>` genérico (adjacente, destravado) → **§10 concorrência
(100%)** → §11 → §17 → §16 → FFI-stdlib. (`ui` REMOVIDO — ruling dono 2026-08-16.)

**⚠️ INCIDENTE DE INFRA (rewind de snapshot):** o container foi restaurado a um snapshot antigo (`6ce8675d`, 231
commits atrás). **NADA de commitado/pushado perdido** — `origin/fix/retirement` @ `7721a1d7` intacto, resync por
ff. Perdido só o NÃO-commitado: o `sort<T:IOrd>` em voo (re-despachado) e o design-doc §10 (reconstruído do
relatório do arquiteto, commitado). Lição: commitar design-docs de subagente imediatamente.

**§10 CONCORRÊNCIA — DESIGN PRONTO (`plano-s10-concorrencia-crumbs.md`), 0% impl.** Spec selada (spawn / `chan<T>`
transportes plugáveis / `Rx`/`Tx`/`Ctx` / await+`Intent<T>` / cancel / `teko::journal` / ns `teko::threads`).
Runtime tem `tk_task_begin/end` (arena) + `tk_region_program` (F2) + `__atomic_*`, mas **SEM criação de thread OS e
SEM scheduler**. **D1 (como spawn cria thread OS): CONSTRAINT-FORCED** → nova primitiva C mantida
`tk_thread_spawn` em `teko_rt.{c,h}` (o bracket task_begin/end tem de viver no trampolim C; pthread-vs-clone é
detalhe INTERNO do runtime, diferido a §16/§17). **D2 (modelo de suspensão do await): RULING-DO-DONO** — a spec
§10.3 pede REACTOR ("cede, nunca bloqueia"), mas NO-VM torna a reificação-de-continuação um transform AOT/arena
grande sem scaffolding. **PERGUNTA AO DONO (ver abaixo):** v1 **thread-per-await** (Opção a, `spawn`+`join`
estrutural, simples, semântica de `Intent` idêntica) e diferir o reactor, OU investir já no lowering
state-machine? **Recomendação do arquiteto: (a) para v1** — único modelo construível no runtime que existe após D1,
preserva todos os observáveis do §10.3. **Só A4 (lowering do await) + metade-suspensão do CN1 (cancel) bloqueiam em
D2;** todo o front-end de await (tipo `Intent<T>`, parser, checker-widening, reconhecimento de cancel) é
independente-de-modelo. **Spine ordenada:** C0a `tk_thread_spawn` [C-rt, PRIMEIRO] → C0b `MemChan` → C0c `OsChan`
(DGRAM do probe) → S1-S3 spawn (parser/checker/codegen, [C]) → C1-C5 canais+DI+WaitGroup ([L], C4 pode virar [C]
se mono não substituir `T` no constraint de `K`) → A1-A3 await front-end ([UNBLOCKED]) → **A4 await-lowering
[BLOQUEADO em D2]** → CN1 cancel → J1 journal. **§16-boundary confirmado:** transportes built-in + spawn caem no
runtime ATUAL (sem §16); só transportes plugáveis-por-usuário (Kafka/Rabbit) são extensão §16-gated. **§10 fica
LEAF** (o axis-2 parallel-codegen usa `fork_join` interno SEPARADO, não a superfície `spawn`/`chan`) → reseeds
mecânicos. Vou dirigir C0a→A3 (desbloqueado) e PARAR em A4 até o ruling D2 do dono.

**✅ §10 SPAWN ENTREGUE (`9549ec9f`..`f3a6a17e`) + `-pthread` no ladder (`268d0da9`) + RESEED (este commit).**
`spawn f(args)` funcional — fire-and-forget, args por CÓPIA, sub-root arena. **C0a** (`teko_rt.{c,h}`):
`tk_thread_spawn` (detached; bracket `tk_task_begin/end` DENTRO da start-routine pthread, precedente
`tk_test_run`) + gêmeos join (await-batch, unused) + selftest (exit 42, drift 0 em 100 ciclos). **S1**
(parser): nós `Spawn`/`TSpawn`, `spawn` contextual. **S2** (checker): ref-guard (sem `ref` cruzando a fronteira
— UAF de arena) + copy-guard. **S3** (codegen): ctx-blob + trampolim `cabi` + DEEP-COPY (escalares inline;
bytes de `str`/slice tail-packed com `ptr` re-apontado DENTRO do bloco malloc'd → nenhum ponteiro da
arena-emissora sobrevive); trampolim libera o blob. Backend native honest-stop `TSpawn`. **`-pthread`
propagado às 5 linhas cc-of-teko_rt.c do ladder** (build_gen1_from_c, build_with_seed_fallback, native_linux_asset
glibc+musl, package_release; não-Windows) — o compilador já ganhou via `project.tks`. **RESEED:** S1-S3 NO-OPam
o corpus (zero `spawn` em `src/`; todo `tk_spawn_*` no self-image é template de string). Seed novo `6cbfb134`.
**GATE self-reproduce (ladder de 1 passo + fixpoint):** gen0(seed anterior `3d98db74`, F3, SEM codegen de spawn)
emite tc1 TRANSITÓRIO `639ab3b2`; gen1 (spawn-capaz) emite tc2 `6cbfb134`; gen2 emite tc3 `6cbfb134`. **tc1!=tc2**
(cascata de temp-name do codegen antigo — a ladder), mas **tc2==tc3 byte-idêntico** (a geração spawn-estável se
reproduz). Seed = geração ESTÁVEL, não o tc1. `provenance_gate.sh` PASS. Fixture `spawn_basic`: 4 workers,
arquivos por tag-`i64` copiado com conteúdo `str` deep-copiado, exit 0, MEM_PARANOID limpo. **Escopo batch-1:**
`char`/structs/classes/closures/`ref` honest-stop (batch posterior); Prim numérico/bool/`byte`/`str`/slices-deles
copiáveis. Próximo §10: canais (`chan<T>`/`MemChan`/`OsChan`) → await front-end (A1-A3) → **A4 para no ruling D2**.

**✅ §10 CANAIS — RUNTIME + SCAFFOLDING ENTREGUES (`a29a5d98`), SEM reseed (leaf); superfície genérica PARADA
num pré-req.** LANDED: **C0b** `tk_memchan_*` (ring FIFO bounded/unbounded, mutex+cond, shell F2 via
`tk_region_program`; probe exit 42), **C0c** `tk_oschan_*` (AF_UNIX DGRAM do probe, key abstract-namespace,
sentinela CLOSED; probe exit 42), **C1** `src/threads/threads.tks` (`IChannelKind<T>` bare-atom, `Closed`,
`Rx<T>`/`Tx<T>`/`Ctx` scaffolding), **C5-runtime** `tk_waitgroup_*`+`tk_region_deregister` (barreira 1000-worker,
exit 42). **Gap-2 PROVADO:** `rx_pop_closed` exit 0 — `Rx__g__i32` estampa, match `i32 | Closed` fecha os dois
braços (a composição união-genérica-com-membro-NOMINAL não-`null` está fechada, sem crumb de contingência).
Canais CONCRETOS-`i64` end-to-end com spawn: `chan_memchan_spawn` (soma 4950, exit 0), `chan_oschan_spawn` (exit
0) — **threads comunicam de verdade** pela superfície Teko. **SEM reseed:** C1 é leaf aditivo (emissão
`6cbfb134`→`de499dad`, +21 linhas `Ctx`/`Closed`; `Rx`/`Tx` genéricos tree-shaken), runtime-C não muda `teko.c`;
fixpoint 2-gen `de499dad`==`de499dad`. **HONEST-STOP (C2/C3/C4/C5-stdlib):** o conformer genérico
`MemChan<T>`/`OsChan<T>` precisa mover um `T` abstrato pelo transporte byte-typed → exige **tamanho genérico
`size_of<T>()`**, que NÃO existe pra type-param abstrato (`@sizeof<T>()` só folda em tipo CONCRETO; §5 marshall só
entregou primitivas de endereço `ptr`/`uptr::__unwrap/__wrap`, sem tamanho). **DECISÃO DO DONO (pendente):** é a
maquinaria de **marshalling de `T` cross-arena** que §5 deferiu — e é a MESMA que spawn (args gerais) e await
(resultados gerais) vão precisar. Escalar/POD (bitwise `sizeof`) resolve com **fold de `@sizeof<T>` diferido ao
monomorph** (T concreto no stamp); não-POD (str/ponteiros-de-arena) exige **deep-copy recursivo/serialize**
(igual o spawn fez ad-hoc pros seus tipos fixos, generalizado). O implementer PAROU certo — não inventou. Próximo:
resolver o marshalling-de-`T` (ruling §5) → destrava C2-C5-stdlib; e o **await/cancel opção (c)** (em desenho
vivo com o dono: `on canceled as c {}` como desenrolar-não-local capturável, não-capturado→panic-cascade).

**⚖️ RULINGS DO DONO (2026-08-16) sobre a arquitetura do §10 await + ordenação:**
- **Modelo do await = OPÇÃO (c)** — coroutines STACKFUL sobre controladores do SO via FFI (ucontext/Fibers pra
  suspensão; io_uring/epoll/kqueue/IOCP pro reactor), escolhida sobre (a) thread-per-await (trava thread) e (b)
  state-machine-stackless (emitir C quebra a perna nativa; o transform teria de ir ao IR). Rust/Zig provam que o
  transform é AOT-possível, mas (c) põe a complexidade no **runtime-scheduler** (o lugar do §16), com `await`
  baixando pra **chamadas de runtime** — backend-agnóstico.
- **Semântica de cancel SELADA:** `<expr> on canceled as c { … }` é **superfície geral do dev** (+ gerada pelo
  compilador no spawn-arm). `cancel(razão)` = **desenrolar não-local que MATA arenas** do ponto até a captura
  (reusa a máquina de `panic`/`exit`, não `setjmp`-no-C); `c` amarra o **`error`** carregado. **Não-capturado →
  desenrola até a raiz; o start-verdadeiro embrulha a `main` num `on canceled` que CONVERTE em `panic`** →
  cascade → programa inteiro. `cancel()` fora de suspensão = panic direto. **When-all** (`await a,b,c=…`): o
  **contexto-do-await** (não o pai) cria N braços (cada um = spawn c/ arena própria) + waitgroup(N) + slots; o pai
  **cede** (coroutine yield), retomado quando o waitgroup zera. Paralelismo vem de spawnar os braços, não do modelo.
- **ORDENAÇÃO (ruling): o §10-(c) entra APÓS o §16** (depende da infra OS-FFI que o §16 constrói), assim como
  outros itens que dependem disso. **`#17 pragmas` adiantados AGORA** (aprovado). Doc-2 reordena:
  **§17 → §16 → §10-(c) + dependentes**, tudo ainda PRÉ-Doc-1. O que já aterrissou do §10 (spawn, canais
  runtime+concreto) fica; a superfície genérica dos canais + o `await` esperam (marshalling §5 / §16).
- **~~PENDENTE~~ RESOLVIDO — ruling do dono (2026-08-16) sobre marshalling-de-`T`:** NÃO é um menu de forks
  (o meu "POD-primeiro vs recursivo / auto-walk vs interface" foi enquadramento errado meu, retirado). O dono
  colapsou: **`marshal<T>` = walk-recursivo profundo ESTAMPADO NO MONOMORPH** (T concreto no stamp → tamanho+
  layout conhecidos; o `sizeof<T>`-abstrato que travou o canal genérico **SOME** — a cópia nunca acontece no
  corpo genérico, só no stamp concreto). Copia o dado apontado (bytes de str, elementos de slice, campos
  aninhados) re-apontando na arena-ALVO — generaliza o packer ad-hoc do spawn (S3). **PRINCÍPIO CENTRAL
  (C#-style):** a **fronteira de concorrência (spawn/await/canal) é TUDO cópia profunda, ZERO `ref`** (o
  `ref`-guard já é lei: `tspawn_reject_ref_params` no S2; A3 do await reusa). **Única exceção = serviços
  singleton**, compartilhados via F2+DI **por chave** (`svc<S>("key")`) — o único elo da arena-filha com a raiz;
  handles de singleton dentro de valor copiado são CHAVES (escalares), não ponteiros → deep-copy-safe. **Por-
  referência (`__wrap`/`__unwrap` rasos §5 Parte A) serve SÓ FFI (zona da ABI) + transferência-de-arena
  (re-parent)** — NÃO toca a concorrência; ficam como estão (corretos nesse domínio). **CORREÇÃO DA SPEC §5:** o
  marshal profundo estava marcado "Doc-1 Parte B" (deferido); como o §10/Doc-2 precisa dele por-valor e Doc-2
  prepara TODO o terreno SEM EXCEÇÕES, ele **é puxado pra Doc-2** e **materializado junto do §16** (arena+FFI já
  em Teko; o §10-(c)/canais-genéricos já são pós-§16). É **um crumb de codegen** (estampar `marshal<T>` no
  monomorph) que serve spawn+canal+await de uma vez. `__wrap` raso-só-hoje = o defeito grave que a Parte-B-deferida
  deixou aberto — fechado no §16.

**⚖️ FORMA CANÔNICA DO BRAÇO DE AWAIT + 4 LEIS (ruling do dono 2026-08-16, materialização A4/CN1 pós-§16):**
o compilador gera, por braço do `await`, uma fn `arm<I: Intent<T>, T>(u: uptr)` e o pai faz `chan.add();
spawn(arm<Intent<T>, T>(uptr::__unwrap(intent)))`. O corpo:
```
fn arm<I: Intent<T>, T>(u: uptr) {
  var intent = match u.__wrap<I>() { I as i => i; error as e => panic(e) }   // ref pro Intent do PAI
  var tx = svc<Tx<bool>>()
  defer { tx.send(!intent.canceled); tx.done() }        // dispara em saída normal E no cancel-unwind
  intent.value = user_func() on canceled as e { intent.canceled = true; intent.failure = e; null }  // marshal no assign
}
```
As 4 leis que isso sela:
1. **`__wrap<T>()` RETORNA um VALOR `T | error | null`** na arena do CALLER (retorno virtual) — **NÃO** um
   `ref T` (retorno-por-referência foi REMOVIDO; `typer.tks:1654` tipa `T|error|null` e @throws se `T` for
   referência). *(CORREÇÃO 2026-08-16: eu havia citado `wrap<T>(p): ref T` do `marshall-spec.md`, que está
   **RETIRED/superseded**. Erro meu.)* **Consequência:** como devolve um VALOR auto-contido no caller (não um
   alias de `*u`), `__wrap` **é OBRIGADO a deep-copiar/reconstruir** o `T` do dado apontado → **`__wrap` É o
   marshal profundo** (não pode ser raso). O braço `error` é a Parte-B (validação), puxada pra Doc-2.
2. **INVARIANTE-DO-WRAP (regra dura, elevada do `unsafe`):** o que passa por `__wrap` **NÃO pode residir na arena
   de quem faz o wrap** — tem de viver numa arena que SOBREVIVA ao wrapper. No braço, o `Intent` vive na arena do
   **PAI** (que espera → sobrevive ao braço), nunca na do braço → sem UAF. É o "event horizon" do §5.2.
3. **WRITE-BACK = OPÇÃO A (canal + deep-copy), RESOLVIDO (ruling do dono 2026-08-16):** o braço produz seu
   resultado/`Intent` como VALOR e **envia pelo CANAL de conclusão**; o contexto-do-await do PAI recebe (marshal
   →arena-do-pai) e grava. O braço NÃO aliasa o Intent do pai (retorno-por-ref removido; `__wrap` devolveria
   cópia). **"await = cópia profunda sem passagem de referência"** (dono). **Mas isso NÃO bloqueia** o usuário de
   usar o escape-hatch `uptr`/`__wrap` que o dono demonstrou (o caminho wrap-refcount, quando ele quer
   compartilhar em vez de copiar).
4. **CANCEL-UNWIND REPLAYA `defer`:** o `defer` (C7.18, `ast.tks:410`, hoje dispara em return/break/continue/
   fall-off) tem de disparar TAMBÉM no desenrolar do cancel (CN1 estende o unwind pra replayar defers) — é assim
   que "braço cancelado ainda sinaliza o waitgroup" se cumpre: o `tx.send(!canceled); tx.done()` sempre dispara,
   o join do pai nunca pendura. O waitgroup é um **canal de conclusão** (`Tx<bool>` singleton via DI + `chan.add()`).
`arm<I: Intent<T>, T>` (type-param único no `__wrap<I>`) esquiva `__wrap<Intent<T>>` (type-arg genérico aninhado).

**✅ §17 COMPILAÇÃO CONDICIONAL ENTREGUE (`4589eafe`..`c4681cc1`) + RESEED (`20d7cb9b`).** `#if/#elseif/#else/
#endif` + `#os`/`#arch` (atalhos p/ item único) + predicados COMPOSTOS (`&&`/`||`/`!`+grupos), **podados no
build-time ANTES do type-check**. Crumbs: **A** AST de predicado (`@PredKind()` = `PTrue|PEq|PFlag|PNot|PAnd|POr`)
+ avaliador puro-booleano `eval_pred` (`src/build/prune.tks`, curto-circuito, eixo-desconhecido/flag-ausente→false;
NÃO é §14 comptime) · **B** `Item.guard: Pred` (só-prune, apagado antes do `.tkb`) + `prune_cc` (substitui
`prune_os` no mesmo slot `frontend_check`) + `target_arch` · **C** `#arch` (canônico `"arm64"`; o `"arch64"` da
spec era typo) · **D** guards em TODOS os itens de topo (fn/type/flags/const/extern) · **E** regiões
`#if/#elseif/#else/#endif` + parser composto (`src/parser/parse_cc.tks`) · **F** `build_flags` honest-stop vazio
(sem fonte de flag inventada). **RESEED (mudança de compilador B-E):** seed novo `20d7cb9b`; corpus sem guards →
prune NO-OP → byte-idêntico por-plataforma → **tc1==tc2==tc3** (sem ladder); `provenance_gate.sh` PASS.
Regressões: `s17_if_region` (arm `#if(os=="windows")` chama símbolo INEXISTENTE e é podado **antes** do
type-check sem erro — prova prune-precede-checker) exit 42; `s17_arch` exit 64; `s17_composite` exit 99;
`s17_if_reject` rejeita 3 diagnósticos. **§17 é o pré-req do §16** (seleciona FFI por-target + ucontext/Fiber e
io_uring/kqueue/IOCP do §10-(c) por `#os`/`#arch`). **Próximo grande alvo = §16.**

**⚖️ MODELO DE MEMÓRIA POR-TIPO + refcount-wrap (design com o dono 2026-08-16):**
- **META-PRINCÍPIO (lei):** o modelo de memória é **transparente por-tipo** ao dev — **valor** = deep-copy
  (marshal na fronteira); **classe** = arena-per-object (caixa própria, semântica de ponteiro, `codegen.tks:5824`);
  **wrapped** (serviço/ref-opaca/FFI) = refcount. **Extensível** (o dev pode definir a forma dele). Garantias:
  **sem UAF + sem memory-leak**.
- **refcount-wrap (mecanismo):** dict na arena RAIZ (endereço→count); `wrap` incrementa, `drop` decrementa; count
  zero → libera tudo; senão remove só o ponteiro RASO (dado sobrevive p/ os holders restantes). **Só o wrapped
  entra** → o bulk-free O(1) do caminho por-valor fica INTACTO; collections TS resolvem a contenção cross-thread.
  Achado: **NÃO há refcount de objeto hoje** (classes são arena-per-object, freed por region-drop), então o
  refcount é maquinaria NOVA sobre a caixa-por-objeto existente — não há refcount pronto p/ reusar.
- **`Table<…>` — collection própria (confirmado dono 2026-08-16):** uma **tabela em-memória enxuta**, multi-índice
  (estilo tabela-de-banco, **≤16 genéricos** = o limite de type-args da Teko), com **acesso e trocas rápidas e
  ATÔMICAS multi-valor** (apoia nas atômicas/sync já aterrissadas do §10) e **poder computacional** (índices
  compostos). Exige seu **próprio conjunto de operações na stdlib** (query/index/swap-atômico). É **item de
  stdlib-genéricos da Doc-2** (uma "fase1c" além da fase1b Dictionary/HashSet/Sorted*), **buildável agora** (a
  maquinaria de genéricos está pronta), **NÃO bloqueia** §16/§10-(c). **NÃO acoplada ao refcount** (que usa
  `Dictionary<addr,count>` no v1); o refcount pode migrar pro `Table` se este amadurecer. A despachar como lane
  stdlib independente quando houver capacidade de build (evitar builds pesados em paralelo com o §16).
- **PLACEMENT = Doc-2 (RULING do dono 2026-08-16; CORRIGE minha recomendação errada de Doc-1):** o wrap-refcount
  é **CAPACIDADE de memória** → é terreno → **Doc-2** (parte da arena do §16). **A Doc-1 é SÓ TUNING** (performance
  da arena já-pronta), NÃO adiciona capacidade. Então a arena do §16 inclui o wrap-refcount desde já (o deep-copy
  é o default; o wrap-refcount é o escape-hatch do usuário — ambos Doc-2). **A ARENA está DESENHADA** pela síntese
  de tudo que deliberamos (arena-per-object + escape-analysis + wrap-refcount + deep-copy-na-fronteira +
  singletons-F2) — NÃO precisa de colaboração-de-design separada; o coordenador arquiteta+constrói do registro.
- **JÁ TEMOS O PRECEDENTE — a ESCAPE-ANALYSIS (achado 2026-08-16):** `src/checker/escape.tks` ("the memory-model
  keystone") já faz "detecta-escape→estende-lifetime" **dentro de uma thread**: uma alocação que ESCAPA o frame
  (return/tail-expr/armazenada-fora) é colocada numa REGIÃO MAIS LONGEVA em vez da região-de-frame (bulk-freed na
  saída), então o ponteiro sobrevive ao drop do frame → **sem UAF**. Conservadora (M.1/M.5: leak-safe, nunca UAF).
  É exatamente o "recria ponteiro pro mesmo dado, cruza a fronteira sem UAF" do dono — para a fronteira de
  **frame/escopo**. **O wrap-refcount é a GENERALIZAÇÃO disso pro cross-arena/cross-thread** (tabela-refcount-na-
  raiz em vez de promoção-frame→região-externa) — MESMA FAMÍLIA, confirmando o meta-princípio (o compilador
  decide frame-local vs promovido vs refcounted). Dentro-da-thread: escape-analysis já resolve. Entre-threads:
  marshal (deep-copy, default) ou wrap-refcount (escape-hatch) — **AMBOS Doc-2** (a Doc-1 só faz tuning).
- **⚖️ O QUE É O TUNING DE ARENA DA DOC-1 (dono 2026-08-16; já deliberado em `o-profiler-como-afinador-de-arenas-
  0.3.1.md`, `ast-computed-arena-assessment-0.3.1.md`, `arena-especificacao-unica-0.3.1.md`). Define o limite
  Doc-2/Doc-1 pra a arena — a Doc-2 (crumb D) entrega a arena CORRETA; estes 3 são otimizações da Doc-1, NÃO
  pré-req de saída da Doc-2:**
  1. **Pré-dimensionamento (arena inicia no tamanho certo).** Como o compilador SABE o tamanho das folhas/blocos,
     inicia a arena já pela **soma dos slots esperados** (união → **o maior slot vence**) → reduz realocação e
     **minimiza o consumo** (é EXATAMENTE o "nosso problema atual" — o OOM de MEM_PARANOID no limite do cap de 6 GB
     que os crumbs vêm tocando; a Doc-1 o cura, não a Doc-2).
  2. **Retorno virtual (backend sem cópia).** As funções, no backend, **não mais retornam por cópia** — alocam
     DIRETO na arena do chamador (pai), reduzindo cópias. (Já é o modelo SEMÂNTICO — sem `ref` no retorno; a Doc-1
     realiza a elisão física da cópia no backend.)
  3. **Elisão de arena (não abre arena onde é desnecessário).** Ex.: `fn a(): T { b() }` → `a` NÃO abre arena
     (só encaminha o resultado de `b`); idem retorno de constantes.
  4. **Reaproveitamento estático de literais + folded-como-constantes (dono 2026-08-16).** Deduplica/compartilha
     literais idênticos (strings, numéricos) e valores de constant-fold — emite UMA vez e referencia, em vez de
     duplicar — reduzindo o **tamanho do BINÁRIO final**. (Otimização de binário, distinta dos itens 1-3 que são
     de memória-de-arena; ambos no balde "melhora" da Doc-1.)
  5. **Mitigar o excesso de `push` / copy-grow (dono 2026-08-16). NÃO é bug de feature faltando — os levers JÁ
     EXISTEM.** O problema: `push` em excesso faz o array dobrar a capacidade → copy-grow do buffer inteiro →
     bloat de arena (é o "out of memory (str concat)" do emit — o `codegen::cb` empurrando no buffer do `teko.c`
     de 21 MB; instrumentado em #148 RA1/RA2; incidente "11.5 GB"). Mitigação = **usar array de tamanho conhecido
     e literais em vez de `push`**. Verificado (2026-08-16) que os mecanismos EXISTEM: **`teko::list::with_cap`**
     (pré-capacidade/reserve no tamanho conhecido → sem doubling — o "array de tamanho fixo onde se sabe o
     tamanho"), **`teko::list::grow_inplace(ref x, v)` / `tk_slice_push_r`** (**ref-push** — não copia o buffer no
     grow), e **array literais**. Isto é o work stream **AL-wave** (`al-wave-crumbs.md`/`al-wave-emit-throughput.md`,
     RATIFICADO dono 2026-07-19; AL1 provado/fechado; AL3 = ref-push, o lever global). ⇒ **NÃO é bug** (a
     capacidade está pronta); o que resta é a **migração wholesale** dos hot-paths pra esses levers = **Doc-1**
     (mesmo balde "melhora o consumo de memória"). A arena-Teko da Doc-2 (crumb D+) é allocation-free (opera em
     memória crua, sem `push`), então NÃO sofre disso; o §16 não constrói a migração AL-wave.
     - **⚠️ SUPERSESSÃO (dono 2026-08-16): a AL-wave FOI UM WORKAROUND; o formato fixo é a resposta certa.** O dono:
       "foi um workaround, mas deveria ter insistido no formato fixo". O copy-grow storm que a AL-wave MITIGA
       (ref-push/`with_cap`/`grow_inplace`) é **ELIMINADO POR CONSTRUÇÃO** quando o array é fixo/imutável (sem
       grow → sem copy-grow → sem "out of memory (str concat)" no emit — a raiz do OOM no cap que os reseeds
       tocam). ⇒ a AL-wave (ref-push) NÃO é o alvo final; ela é **superseded pela ONDA DE ARRAYS-FIXOS** (abaixo).
       A máquina de ref-push (`tk_slice_push_r`/`grow_inplace`/`with_cap`) e o array crescível `{ptr,len,cap}`
       viram **legacy a varrer** junto com a onda. NÃO investir mais na migração AL-wave — ela é substituída, não
       completada.
- **⚖️ DOC-1 DESIGN FECHADO — mecanismos resolvidos (rulings dono 2026-08-16, respondendo as 3 perguntas do coord):**
  - **(1) PRÉ-ALOCAÇÃO = tamanho BASE estático no ponto de materialização, NÃO cap.** Pré-alocar arena = declarar
    estaticamente o tamanho inicial de TODAS as arenas no ponto onde nascem → ao nascer já têm um tamanho MÍNIMO
    (base). **Loops NÃO precisam de contagem:** cada volta constrói-arena→processa→apaga-arena, então UM tamanho
    basta (a arena por-iteração nasce e morre); idem recursão (cada frame é uma arena conhecida, nasce/morre por
    chamada). **Não existe em Teko escapar de não conhecer todas as arenas** (o modelo garante conhecimento
    estático; pode até ACHATAR/flatten quando folhas têm tamanho zero). **O que é dinâmico = objetos e strings/
    arrays** (têm cópias pra evitar realloc) → o tamanho pré-computado é **base, não cap**; o **cap pode nascer no
    dobro do base e crescer mais**. ⇒ a arena não é hard-fixed: base-pré-computado (mata o doubling-from-scratch) +
    crescível pro conteúdo dinâmico.
  - **(2) RETORNO VIRTUAL = sret + DETECÇÃO DE ENCAMINHAMENTO.** O ponteiro-destino é criado no CALLER e passado ao
    CALLEE; o compilador reescreve a fn como "tem retorno mas recebe um ponteiro(ref) como parâmetro", e no ponto
    de retorno **atribui o valor na ref e sai limpo** (sret/RVO). **+ Encaminhamento:** o compilador tem que
    DETECTAR chamada-em-posição-de-retorno e **encaminhar a arena/sret do caller ao callee — MESMO quando a fn
    abre arena** pros próprios locais. Ex.: `fn f(): T { var d = 12; var e = "abc"; f2(d, e) }` — `f` abre arena
    (d/e), mas `f2(d,e)` é o retorno → passa a **arena do caller** a `f2` (f2 escreve direto no destino do caller;
    a arena de d/e morre no retorno de f). Liga com a elisão-de-arena (item 3): o valor-de-retorno encaminha, a
    arena-de-locais é local.
  - **(3) MULTI-THREADING = confirmado ("exatamente tudo isso").** Compilador: lexer/parser paralelos por-arquivo;
    checker/monomorph por ilha independente; codegen por-função; **arena por-thread** (as task-arenas do §10).
    **Runner de testes = a cura do `teko test .` OOM:** paralelizar com **teto-de-memória por-teste** (cada teste
    na sua thread+arena com limite) → o total fica bounded (hoje: todos num processo → OOM). ⇒ **A DOC-1 ESTÁ 100%
    NO PAPEL.**
  - **DISCRIÇÃO DE ANTECIPAÇÃO (dono 2026-08-16): vale pra TODA a Doc-1, não só a onda de arrays.** Se qualquer
    item da Doc-1 puder ser antecipado pro §16/§17 (de-riscar/evitar retrabalho), o coord pode fazer. Read do coord:
    o candidato concreto é **string-u32 no §16** (migração do subsistema de string 1× no modelo novo — já marcado);
    os 3 itens (pré-dimensionamento/retorno-virtual/multi-threading) NÃO encaixam naturalmente no §16/§17 (puxá-los
    churnaria o §16 por ganho marginal) → ficam Doc-1, salvo encaixe claro que surja (ex.: pré-dimensionar um
    buffer quente com o `with_cap` existente durante uma migração de subsistema).
- **🌊 ONDA ARRAYS-FIXOS + STRING-U32 (visão do dono 2026-08-16 — EM DELIBERAÇÃO, ainda NÃO ratificada; faltam
  colocação + mecanismos de `isset`/reclaim).** Intenção de fundo: **arrays SEMPRE foram pra ser de tamanho fixo**
  — fat-pointer `{ptr, len}` SEM cap, **imutável e contíguo**; a variabilidade/mutação é das COLEÇÕES (Table/Hash/
  Map/Dictionary/List), não do array cru. O prêmio: como os tamanhos são **AST-computáveis (folhas→raiz)** e as
  arenas formam uma **btree que lineariza numa árvore-de-ponteiros pré-alocada**, dá pra **pré-alocar tudo
  estaticamente** → cura de fundo do OOM (vs. o in-loco/quase-aleatório de hoje). **7 pontos do dono:** (1) `[N]T`
  fixo com leading-zeros + máquina `isset(a[i])`; `var a: []T = f()` já vem fixo de quem gera; (2) eliminar o
  `push` de hoje — coleções têm os métodos de mutação dentro de si; (3) concat gera cópia nova (`a = [..a,
  ..f()]`); (4) reatribuição apaga/marca-para-apagamento o valor anterior na arena; (5) array em `ref` não aceita
  push (aceita reatribuição/ref-de-posição); (6) revisar TODOS os usos de array do compilador → init fixo+literal
  (sem `teko::list::empty`/`push`); (7) trocar por coleção dedicada onde fizer sentido. **STRING:** internamente
  **array de u32 (largura fixa, indexação O(1), sem cabeçalho por-char)** + **trim pra UTF-8 na barreira do metal**;
  o fat só na string (`{qtd-chars, qtd-bytes, array-u32, encoding}`); o `char` vira **u32 puro** (vs. hoje: array
  de bytes dinâmico com cabeçalho repetido por caractere). **RESOLVIDO (rulings do dono 2026-08-16):**
  - **(A) COLOCAÇÃO = PRIMEIRA ETAPA DA DOC-1 (refinamento dono 2026-08-16: "meio que seria a primeira etapa da
    doc1… mas se algo cabe na doc2 (16 ou 17), fique à vontade").** A CAPACIDADE (arrays-fixos + string-u32 +
    `T|null`-default + concat/reatribuição + codecs) é a **1ª etapa da Doc-1**, NÃO um pré-Doc-1 separado — mas o
    **coordenador tem discrição de puxar peças pra Doc-2 (§16/§17)** onde couber. **Caso concreto avaliado
    (coord):** o **modelo string-u32 pode caber no §16** — o §16 já migra `teko_rt.c`'s string fns (`tk_str`/
    `tk_ftoa`/`tk_fmt_*`/concat) pra Teko; migrar no modelo-velho (byte-array) e depois a onda refazer como u32 é
    TRABALHO DOBRADO → quando o §16 chegar no subsistema de string, AVALIAR adotar u32 direto ali (migrar 1×).
    **Arrays NÃO cabem no §16** (o §16 mexe no runtime C, não no uso-de-array do compilador) → ficam na 1ª etapa
    da Doc-1. A **pré-alocação estática AST-computada + espinha/btree-de-ponteiros** que EXPLORA os tamanhos fixos
    (a redução real de consumo) é a etapa Doc-1 SEGUINTE (item-1 pré-dimensionamento, habilitado pelos fixos).
  - **(B) `isset` = ARRAYS DEFAULT PARA `T | null` (dono: "não inventa maquinaria").** `var x: []T` estoca no
    backend como `[](T | null)`; slot não-inicializado = `null`; `isset(a[i])`/acesso = `match a[i] { null => …; _
    => … }`. Reusa a união existente (ZERO máquina nova de presença — nada de bitmap/sentinela). **O preço: acesso
    a elemento SEMPRE exige `match`** (o dono aceitou explicitamente). Otimização futura (Doc-1): elidir o `match`
    onde flow-analysis prova todos-não-null (ex.: `var a: []T = f()` que vem cheio) — não bloqueia.
  - **(C) REATRIBUIÇÃO (ponto 4) = SÓ MARCAÇÃO, sem reclaim por-objeto (dono: "seria como ter um bucket na
    arena").** O valor anterior é MARCADO (logicamente morto); o **consumo persiste enquanto a arena viver** e é
    liberado no bulk-free do drop da região. **A bump-arena NÃO ganha reclaim mid-região** → o core da arena (crumb
    D) fica INTACTO; a onda de arrays não altera o modelo da arena. (A redução de consumo vem da região ser curta
    + do pré-dimensionamento Doc-1, não de reclaim imediato.)
  - **(D) CODECS DE TEXTO MULTI-ENCODING NA STDLIB (dono 2026-08-16).** O campo `encoding` da string fat implica
    a stdlib ganhar **codecs de CARACTERE** (UTF-8 default, + UTF-16, Latin-1, ASCII, …) — conversão/trim por-
    encoding na barreira do metal. **DISTINTO** do encoding-set já entregue (base64/json/cbor/… = formatos de
    DADOS/serialização; isto são codecs de TEXTO sobre o array-u32). **Colocação: pré-Doc-1, com a Doc-2 já
    FECHADA** — parte da onda string-u32, construído sobre o novo modelo de string (não antes: exige o `{qtd-chars,
    qtd-bytes, array-u32, encoding}` no lugar).
  **⇒ ONDA RATIFICADA (dono 2026-08-16).** Sequência: **§16 (syscall+arena+sweep; string-u32 PODE ser adotado
  aqui) → [Doc-2 fechada] → Doc-1 ETAPA 1 = onda arrays-fixos+string-u32+codecs → Doc-1 ETAPA 2+ = pré-alocação/
  espinha + retorno-virtual + elisão-de-arena + reuso-de-literais + multi-threading.** (A fronteira Doc-2/Doc-1 é
  fluida aqui por concessão do dono; o coord decide caso-a-caso o que adianta pro §16/§17.)
  **CONSEQUÊNCIA PRA O §16 (crumb D em diante):** a arena-Teko da Doc-2 só precisa ser CORRETA (region-per-object +
  deep-copy default + bulk-free + wrap-refcount escape-hatch), NÃO ótima. O pré-dimensionamento / retorno-virtual-
  sem-cópia / elisão-de-arena são Doc-1 — NÃO construir na Doc-2. O OOM no limite do cap durante os reseeds é
  esperado (território Doc-1) e não bloqueia o §16.

**⚖️ §16 C6a HALT — DOIS ACHADOS DE COMPILADOR (implementer, 2026-08-16; ambos verificados empiricamente):**
- **ACHADO A — CORRIGE o modelo "leaf = byte-idêntico".** Adicionar QUALQUER **função** ao `src/` do compilador
  desloca o contador tree-wide de temp-var-ID → o `teko.c` emitido NÃO é byte-idêntico → **exige RESEED**. Só
  adições **const-only** (C2 `teko::sys`) são leaf-sem-reseed de verdade (consts sem referência = DCE, footprint
  zero). Funções são SEMPRE emitidas, independente de alcançabilidade. ⇒ **o veredito do arquiteto "C6 = LEAF"
  estava ERRADO**: C6a/C6b (env/time trazem funções) são **RESEED** (comportamento inerte, byte-idêntico-no-
  rebuild como o §17 — mas o seed precisa reproduzir a árvore crescida). Isto reconcilia com o histórico: os
  módulos stdlib concretos (crypto/encoding) que aterrissaram TAMBÉM reseedaram (protobuf fez fixpoint+reseed);
  só os genéricos (sort<T>, 0-stamped) e o C2 (const-only) foram byte-idênticos. **REGRA (lei): função nova no
  `src/` → RESEED; const-only sem referência → leaf.**
- **ACHADO B — GAP DE CODEGEN: extern-C colide com header-de-sistema.** `extern fn c_getenv(name: ptr): ptr =
  "getenv" from "c"` emite `extern void* getenv(void* name);` (o pass de protótipos, `codegen.tks:13425-13436`,
  declara TODA função exceto `from "teko_rt"`). Como o `teko.c` emitido SEMPRE faz `#include <stdlib.h>`, que
  declara `char* getenv(const char*)` (ISO-mandado), o `cc` dá erro DURO "conflicting types for 'getenv'".
  `setenv`/`unsetenv` NÃO batem (POSIX, não declarados por `<stdlib.h>` sob `-std=c2x`); `clock_gettime` (C1)
  não bateu (`<time.h>` não é incluído incondicionalmente). ⇒ o gap é **símbolos libc declarados pelos headers
  que o emit do teko inclui incondicionalmente**. **O PADRÃO DE FIX JÁ EXISTE:** o pass de protótipos JÁ pula
  os externs `from "teko_rt"` ("já declarados em teko_rt.h com os tipos FFI corretos; re-emitir conflita") — a
  mesma lógica precisa cobrir os símbolos header-declarados de `from "c"`. Compiler-touching (codegen) → reseed.
  **ROTEADO ao arquiteto** p/ o design do fix (skip-set curado dos headers incluídos vs cast-por-fn-pointer no
  call-site) — é a capacidade de codegen que destrava a FFI-a-libc em geral do §16. **BATCH:** como C6a já é
  reseed (Achado A), o fix-de-codegen + `teko::env` completo (get/set/unset) entram num ÚNICO crumb compiler-
  touching (um reseed p/ os dois). Trabalho parcial salvo em `feat/s16-c6a-env` `f62d8f3a` (fixture + módulo no
  espelho-local; set/unset verificados; get bloqueado no Achado B) — NÃO drenado.

**🔴⚖️ VIRADA §16 — libc está FORA; o alvo é SYSCALL / ABI-NATIVA-DO-SO / TEKO-PURO (RULING DO DONO 2026-08-16).**
Eu tinha ligado via `extern fn ... from "c"` (libc: getenv/setenv/clock_gettime). **ERRADO, no fundo.** (1) libc
É C — ligar em libc não remove a dependência de C, só a move de `teko_rt.c` pra libc; o "sweep de C" fica
incompleto. (2) **Overhead + dependência-C no NATIVE:** o binário native (alvo do seed-native da Doc-1) passaria a
linkar+chamar libc (wrapper de libc por cima do syscall) — o dono quer o binário native falando DIRETO com o
kernel. **`from "c"` (libc portável) está BANIDO.**
- **A pergunta-chave do dono (e a resposta = o modelo a copiar):** como Rust/Go/Zig/.NET falam com o SO SEM
  compilador C? Todas **declaram o símbolo externo e deixam o LINKER (ou loader dinâmico) resolvê-lo contra a
  SHARED LIBRARY NATIVA do SO** — artefato binário de ABI estável que o SO já distribui; zero compilador C, zero
  código C. Go no Linux faz **syscall cru** (instrução `syscall`, sem libc); no macOS/Windows chama libSystem/
  kernel32 por stubs asm. Rust/.NET fazem bind da lib nativa via linker/`DllImport`. **O que se evita é o
  COMPILADOR C + o CÓDIGO-FONTE C (`teko_rt.c`), NÃO a shared library.** Linkar a lib nativa é operação de LINKER —
  exatamente a definição da perna-native ("linker-only, sem cc"). Bate.
- **RULING (relaxa o "SEM EXCEÇÕES" absoluto):** "podemos exigir C para alguns casos; havendo shared library, nem
  precisa de C." Ou seja: exigir a **shared library de ABI-C nativa do SO** (libSystem/kernel32) é ACEITÁVEL onde o
  SO não dá syscall estável (Apple/Windows) — não é dependência de compilador C. O que morre é a libc-portável-como-
  runtime-C que a gente arraste.
- **MODELO CORRIGIDO (por-SO):**
  | Caso | Mecanismo | compilador C? |
  |---|---|---|
  | **Linux — core/hot** | **syscall CRU = intrínseco de codegen** (asm inline `syscall` na perna C / instrução nativa na native), números no `teko::sys` | não |
  | **macOS** | bind de símbolos do **libSystem.dylib** (ABI mandada pela Apple), resolvido pelo linker | não |
  | **Windows** | bind de **kernel32.dll/ntdll.dll** (Win32/NT), resolvido pelo linker | não |
  | **Sem syscall nem razão de lib** (env: `environ` é memória) | **Teko puro** sobre `environ` | não |
  | **Libs opcionais** (openssl/gpg/db) | **FFI em runtime** (`dlopen`/`dlsym`) — ruling anterior | não |
- **MECANISMO DO SYSCALL (avalizado pelo dono 2026-08-16, "emita assembly inline em teko.c"):** intrínseco de
  codegen, como o `f64_bits`. **Perna C:** o codegen emite `asm volatile("syscall" ...)` inline no `teko.c` (a
  perna C NÃO pode usar o wrapper `syscall()` da libc — é libc) com as convenções de registrador por-`#arch`
  (x86_64: `rdi/rsi/rdx/r10/r8/r9`, ret em `rax`, clobber `rcx/r11/memory`; aarch64: `x0-x5` + `svc #0`). **Perna
  native:** o codegen emite a instrução de syscall direto. **`teko::sys`** guarda os NÚMEROS por-`#os`/`#arch`
  (`SYS_write`/`SYS_mmap`/`SYS_clock_gettime`/`SYS_getrandom`/`SYS_exit_group`…).
- **SALVA:** **C1** (`extern type = struct` — syscalls E a ABI-nativa levam/retornam structs: `timespec`, `stat`…);
  **C2** (`teko::sys` — agora guarda números de syscall + convenções de símbolo por-SO). A máquina de `extern fn`/
  `extern struct` é reusável pro bind de lib-nativa (macOS/Windows).
- **MORREM (framing libc, descartados — worktrees/branches removidos, NUNCA drenados):** **X1** (skip-set de
  `from "c"` vs `<stdlib.h>` — sem sentido: syscall na perna C é asm inline, sem símbolo/colisão) e **C6a**
  (`teko::env` via libc getenv — env vira Teko puro sobre `environ`). O **plano §11/§12** (refresh libc-FFI) fica
  SUPERSEDED por esta virada. **Novo keystone = o intrínseco de syscall** → re-roteado ao arquiteto.
- **✅ KEYSTONE DE SYSCALL ATERRISSOU (`fb0ec8c7`, reseed `1a03a68e`, 2026-08-16).** `teko::sys::syscall0..6(nr,
  a0..aN: i64): i64` builtins (estilo `f64_bits`) → lowering emite helpers `static inline tk_syscallN` no preâmbulo
  com `asm volatile("syscall" ...)` x86_64 (`#if defined(__x86_64__)`/`#error`; aarch64 design-ahead), **use-gated**
  (`cg_scan_syscall_arities`) → o corpus sem-syscall fica byte-idêntico (fixpoint LIMPO tc1==tc2==tc3, sem ladder).
  `teko::sys` ganhou `SYS_EXIT_GROUP` x86_64(231)/aarch64(94). `#arch` prune CONFIRMADO funcional. MEM_PARANOID
  exit 0 (nota: pico ~5.6 GB no limite do cap de 6 GB — um 1º run bateu no guard "out of memory (str concat)" do
  emit e o re-run passou; artefato de pressão-de-memória do cap = território Doc-1, NÃO erro de correção). Fixture
  `sys_exit_group` = `exit_group(42)` cru → exit 42. Native honest-stop (`_ =>` de `lower_call`). **Bridges
  `ptr_word`/`ref_word` (args-ponteiro) e a migração de subsistemas (write→exit→clock_gettime→getrandom→ARENA-mmap)
  são os próximos crumbs.**
- **⚖️ CRITÉRIO DE ACEITAÇÃO DO §16 = O SWEEP REAL (ruling do dono 2026-08-16).** A **prova de execução** do §16
  NÃO é "adicionar as substituições Teko/syscall ao lado do runtime C". É **DELETAR** `src/runtime/teko_rt.c`,
  `src/runtime/teko_rt.h`, `src/assert/assert.c` e `win32_compat.h` do código — **remover TODOS** — e o build
  **ainda compilar E passar o fixpoint SEM eles**. A linha de build encolhe de `cc bootstrap/teko.c
  src/runtime/teko_rt.c src/assert/assert.c -lm` para essencialmente **`cc bootstrap/teko.c -lm`** (só o C emitido,
  auto-contido, falando com o SO por syscall inline / lib-nativa) — e tc2==tc3 se mantém. **CONSEQUÊNCIA:** o §16
  só fecha quando TODO subsistema desses 4 arquivos (arena É um; mas também print, panic, assert, string/format,
  threads/canais do §10, etc. — ~264 fns) estiver reimplementado em Teko/syscall; então o **crumb de SWEEP** apaga
  os 4 arquivos e prova o build-sem-eles + fixpoint. Esse sweep é a materialização do "sweep de C" da lei e o
  gate terminal do §16 (o crumb F da arena — deleção de `tk_region_*` — é só uma FATIA disso; o sweep completo
  espera TODOS os subsistemas migrados). Enquanto a migração roda, o build de validação AINDA linka os 4 (eles
  proveem o não-migrado); o sweep é o passo FINAL. É também parte do "as duas pernas compilando" do critério de
  saída da Doc-2 (a perna C tem que buildar SEM o runtime-C-à-mão).**
- **⚖️ CHECKLIST DO CRUMB DE SWEEP (ruling do dono 2026-08-16, "fazer o reseed E ajustar o CI a não incluir os
  arquivos C deletados"):** o sweep é uma mudança COORDENADA, não um `rm`:
  1. **Codegen:** parar de emitir `#include "teko_rt.h"` / `#include "assert.h"` (e o `../win32_compat.h` via
     teko_rt.c) no `teko.c` — senão o C emitido não compila sem os headers. Isso muda o emit → **RESEED**.
  2. **Deletar** os 4: `src/runtime/teko_rt.c`, `src/runtime/teko_rt.h`, `src/assert/assert.c`, `win32_compat.h`
     (+ `src/assert/assert.h` se ficar órfão).
  3. **Ajustar o CI + scripts de build/empacotamento** que hoje linkam/copiam esses arquivos (grep 2026-08-16, ~20):
     `scripts/build_gen1_from_c.sh`, `build_with_seed_fallback.sh`, `native_linux_asset.sh`, `package_release.sh`
     (o "PROVEN minimal standalone-bootstrap file set" vira só `teko.c`), `ci_full_mode.sh`, `ci_provision_teko.sh`,
     `lone_binary_criterion.sh`, `no_emitted_c.sh`, `no_c_in_tests_gate_test.sh`, `install_share_runtime_test.sh`,
     `region_drop_subtree_test.*`, `tk_arena_commit_test.*`, `task_memory_isolation_test.*`; workflows
     `.github/workflows/reseed-bootstrap.yml`, `release.yml`, `codeql.yml`; `.github/sast-baseline.txt`,
     `lsan-suppressions.txt`. A linha canônica vira **`cc bootstrap/teko.c -lm -o teko`** (sem `teko_rt.c`/
     `assert.c`, sem `-I src/runtime -I src/assert`). Os testes-C-de-runtime (`*_test.c`) que exercitam a arena-C
     morrem com ela (a arena Teko é testada por fixtures Teko).
  4. **Provar** `cc bootstrap/teko.c -lm` (perna C, sem os 4) compila + `TEKO_BACKEND=c gen . -o out` re-emite
     byte-idêntico (tc2==tc3) + MEM_PARANOID + PROVENANCE/`provenance_gate`. Esse é o gate de aceitação do §16.

**🗺️ §16 MAPEADO (scout, HEAD `41a1817e`):** **7861 linhas de C** em 4 arquivos (`teko_rt.c` 5515 + `teko_rt.h`
1751 + `assert.c` 256 + `win32_compat.h` 339), ~264 fns públicas sobre ~242 libc/syscall. **21 subsistemas**,
ordem ~10 fases (fácil→difícil): strings/format/char/float → env/print/os-arch → **I/O&fs** → time/random →
**ARENA (keystone, §16-core; Doc-1 melhora depois)** → panic/assert → process/exec → test(setjmp) →
crash(opcional) → **threads/sync (pthread TRANSITÓRIO; clone/CreateThread fica §17+)**. **Gaps lado-compilador:**
(1) **`extern type`** (struct C-ABI) não está na gramática; (2) módulo **`teko::sys`** de constantes curadas
por-plataforma (`O_RDONLY`/`CLOCK_*`/…, à mão, sem import de macro-C); (3) syscall-externs diretos; callbacks
(fn-pointer) ficam FORA do §16 (pthread segue via C). Saída ~6700 linhas Teko + ~200 de `teko::sys`. §17 (feito)
é o mecanismo per-target. Quirks: Linux mmap/futex/AF_UNIX · macOS mmap-ANON/kqueue/getentropy · Windows
VirtualAlloc/Events/SEM-AF_UNIX. **Pronto pro design pass do §16.**

**✅ §16 C1 ENTREGUE + RESEED (`c7ac134b`, drenado em `fix/retirement` `03f2766d`, 2026-08-16).** O keystone da
FFI: `extern type Name = struct { … }` — struct C-ABI **header-less** na fronteira extern. Parser ramifica o
caminho `extern type` no `=` (sem `=` = `ExternBody` opaco; `= struct{…}` = novo `ExternStructBody`, rejeita
métodos/consts); AST ganha `ExternStructBody` no union `TypeBody()` (tag `.tkb` 10); checker `extern_struct_field_ok`
(campo = prim numérico/byte/ptr/uptr/extern-struct aninhado) + `extern_named_body_ok` alarga `extern_type_ok`
(admite extern-struct por valor e out-pointer `ref T`); codegen `emit_extern_struct_typedef` emite o typedef **MENOS
a linha `tk_struct_hdr __hdr`** (layout byte-idêntico ao struct C estrangeiro). **Gate coordenador (perna C, BUILD
REAL):** gen0(seed `20d7cb9b`)→tc1 `c7ac134b`; tc1→gen1→tc2; tc2→gen2→tc3; **tc1==tc2==tc3 byte-idêntico, sem
ladder** (corpus declara zero `extern type = struct` → arm de codegen inerte no self-image); **MEM_PARANOID árvore
exit 0**; regressão `extern_type_struct` (`Timespec` = `struct timespec` libc, `clock_gettime` FFI, store+read
`ts.sec`) **exit 0**; `provenance_gate.sh` PASS no swap. **Native honest-stop:** `ExternStructBody` cai no `_ =>`
do native lower (`lir/lower.tks`, `lir/lower_const.tks`) — lowering native do extern-struct **diferida à fase-native
terminal da Doc-2** (perna C é o alvo do C1). **Doc-staleness anotada** (`plano-s16-fundacao-crumbs.md §3.1`):
`extern unsafe fn` (retirado §6), `ptr<Timespec>` (`ptr` opaco, sem arg), `ref` no call-site (é binding, não
operador) são superfície morta — real = `extern fn` + param `ref T` + arg local-plano; corrijo num crumb de doc.
**Sequência de crumbs (plano §7): C1 ✅ → C2 `teko::sys` (leaf) → C3 str/text → C4 char/UTF-8 → C5 float-bits
INTRÍNSECO (compiler-touching) → C6 env/os/arch/time/random (FFI) → C7 deleção de símbolos C (two-legs gate).**
**✅ §16 C2 ENTREGUE (LEAF, drenado `1cb6e5f7`, 2026-08-16).** `src/sys/sys.tks` (`teko::sys`): 4 consts de tempo
`#os`-guarded (`CLOCK_REALTIME`/`CLOCK_MONOTONIC` p/ linux+macos), literais transcritos da ABI, sem import de
macro-C. **Verdict LEAF confirmado (coordenador, build real):** o `teko.c` emitido da árvore+C2 = seed `c7ac134b`
BYTE-IDÊNTICO → seed reproduz a árvore, **sem reseed** (o módulo não é consumido pelo compilador; `consteval` vê 2
consts após o prune, mas dead-code-elimina na emissão). Fixture `sys_clock` **exit 10** (=`CLOCK_MONOTONIC(1)*10 +
CLOCK_REALTIME(0)`) provando o prune §17 manter o bloco linux e dropar macos (`consteval 2/2`, não 4). **Achado
arquitetural (precedente):** projeto de regressão externo NÃO enxerga módulos `src/` internos do compilador — o
fixture carrega um espelho local de `teko::sys` sob seu `src/sys/`, endereçado como `sys::` bare (mesmo precedente
de `generic_sort`/`collections_fase1b`/`chan_dgram`). **Hazard de workflow anotado:** o implementer rodou `git
checkout -B` no worktree PRINCIPAL (moveu meu branch); recuperado por `ff-only` — futuros briefs devem exigir
worktree isolado, nunca `checkout` no principal.

**§16 PRÓXIMA FASE (C3-C6) → ARQUITETO (design-open, 2026-08-16).** O C1 revelou que o `plano-s16-fundacao-crumbs.md`
tem superfície MORTA (`ptr<T>` — `ptr` é opaco; `extern unsafe fn` — retirado §6; `ref` no call-site — é binding).
As fases C3-C6 entram em marshalling FFI real (str↔cstr no `getenv`, `clock_gettime`, `getrandom`) + a escada do C5
float-bits (intrínseco: gen0 ainda emite `tk_f64_bits` no tc1 → um-passo-de-ladder tc1≠tc2==tc3, deleção do símbolo
C só depois do reseed). Roteado ao arquiteto p/ refrescar o plano contra a superfície real, selar o recipe str↔cstr,
e nomear os próximos crumbs implementáveis em ordem com verdict leaf-vs-reseed — ANTES de firar implementer.

**⚖️ `f64_bits`/`f64_from_bits` = INTRÍNSECO DE CODEGEN (ruling do dono 2026-08-16, "intrínseco de codegen então").**
Na fase strings/format/**float** do §16, essas duas são o ÚNICO caso que não vira nem `extern fn` nem Teko puro:
são **type-punning** (reinterpretar os bits crus de um `f64` como `u64` e vice-versa — não conversão numérica).
Não há símbolo libc que faça bit-reinterpret (`extern fn from "c"` impossível), e a linguagem não expõe primitivo
de reinterpret (Teko puro impossível). **Resolução = o modelo Rust/Zig:** o compilador **lowera direto** — union/
`memcpy` no backend C, `bitcast` no nativo — com a fn `.tks` (`f64_bits(x)`) como **fachada fina** sobre o
intrínseco (igual ao `f64::to_bits`→`mem::transmute` do Rust e ao `@bitCast` do Zig, ambos builtins de compilador,
zero runtime). **Correção EMBUTIDA (não só higiene de §16):** o lowering DEVE emitir o pun em MEMÓRIA (memcpy/
bitcast), **nunca** trânsito por registrador FP — em x87/32-bit carregar um *signaling NaN* num registrador FP
CANONIZA os bits (o Rust apanhou disso); uma fn C recebendo `double` por valor perderia o payload do NaN. Efeito
§16: `tk_f64_bits`/`tk_f64_from_bits` (`teko_rt.c:5264-5265`, memcpy) SOME sem deixar buraco → sem símbolo C, sem
dep libc; o intrínseco "passa Teko-only". Consumidores atuais preservados: `protobuf` (wire IEEE-754), `comptime_
fold` (const-fold bit-exato), `math` (inf/nan). **É crumb de CODEGEN na fila do §16, não crumb de FFI.**

**✅ `sort<T:IOrd>` ENTREGUE (`2de16e63`) — SEM reseed (leaf, byte-IDÊNTICO).** `pub fn sort<T: IOrd>(xs: []T):
[]T` — merge sort estável top-down (`msort_ord`/`merge_ord`, left-biased no empate), ordenação via `<=`/`<`
baixando ao `operator __le`/`__lt` de `T` (dispatch 9-ops). ADIÇÃO pura de 76 linhas em `src/sort/sort.tks`; os 6
sorts CONCRETOS de primitivos (`sort_str`/`sort_i64`/`sort_u64`/`sort_f64`/`sort_bytes`/`sort_str_natural`)
intactos (o compilador os usa internamente). **Corpus NUNCA instancia → 0 stamped, emissão `ac9a9976`
BYTE-IDÊNTICA com/sem delta** (mais limpo que "só typedefs inertes"; sem reseed). Fixture `generic_sort` (3
instâncias: I64Key/StrKey/`Ver` struct derivando IOrd; estabilidade verificada) exit 55. **STDLIB §1.5 genéricos
COMPLETA** (collections + sort<T:Ord>). **Achado adjacente (rastreado, fora de escopo):** `sort_by<T>(xs, less:
func<T,T,bool>)` PULADO — a lowering de chamada-de-closure genérica não substitui o type-param no cast do
ponteiro-de-função (emite `tk_t_T` em vez de `int64_t`); todo uso de `func<…>` no tree é a tipos CONCRETOS, então
o caminho type-param nunca foi exercido. Bug de codegen, follow-up. Próximo: **§10 concorrência C0a→A3** (A4 espera
ruling D2).

**✅ `crypto` password ENTREGUE (`12a23253`).** scrypt (RFC 7914, Salsa20/8+BlockMix+ROMix sobre pbkdf2_sha256) +
Argon2id (RFC 9106, multi-lane p≥1, G/GB BlaMka, H'/H0). Cross-check byte-exato: RFC 7914 §12, RFC 9106 §5.3
(vetor oficial t=3/m=32/p=4), `argon2-cffi` 25.1, `hashlib.scrypt`. Construído com o compilador pós-#4 (mangle
novo), build verde. **Dado concreto p/ Doc-1 (arena):** parâmetros grandes (scrypt N=1024/r=8/p=16, argon2
O(m'²) writes) estouram OOM na arena sem-GC atual (lixo por-lane num arena não-liberado) — **correto no KAT,
some com a melhoria de arena da Doc-1**. Achado: `base` também é reservado como nome de VARIÁVEL LOCAL (não só
param).

**✅ ENCODING SET COMPLETO (`d462c437`).** 15 formatos §1.5: asn1·base64·bson·cbor·csv·fixed·ini·json·mime·
msgpack·**protobuf**·toml·url·xml·yaml. `encoding::protobuf` = wire format (varint/zigzag/wire-types 0/1/2/5/
tag/message model), Proto* únicos, FIXPOINT gen2==gen3 byte-idêntico + MEM_PARANOID verde, cross-check vs Python
protobuf. Seam honesto: sem `proto_float` (f32 encode) — o seed não expõe `f32_bits` (só `f64_bits`); decode f32
funciona via widen/narrow.

**✅ `encoding::asn1` + `encoding::fixed` ENTREGUES (`154906bf`).** ASN.1 DER encode/decode (BER definite-length
tolerante), INTEGER via bigint, OID, tagged explicit/implicit — cross-check byte-exato vs `asn1crypto` +
decode field-a-field de um `SubjectPublicKeyInfo` RSA real do openssl (relevante p/ X.509/PKCS). fixed-width
colunas. Nomes `Asn1*`/`Fixed*` únicos. **Novos traps (registrados abaixo):** `class` é keyword reservada;
bitwise em `byte` bare trip B.38 (cast `to u64`).

**✅ `encoding::yaml` ENTREGUE (`399deeef`).** YAML 1.2 core (struct-tagged `YamlValue`): block/flow maps+seqs,
literal `|`/folded `>` com chomping, anchors/aliases, multi-doc, core-schema; cross-check PyYAML 6.0.1;
deferrals honestos (tags `!!`, merge `<<`, directivas). Achou+consertou 2 bugs de parser via binário scratch
compilado (não `teko test .`). Nomes `Yaml*` únicos (sem colisão #4).

**✅ `encoding::toml` + `encoding::ini` ENTREGUES (`7695a869`).** TOML v1.0.0 (struct-tagged `TomlValue`) +
INI (estilo `configparser`, cross-check Python). **`parse_manifest` (`src/build/manifest.tks`) trocado pra usar
`teko::encoding::toml`** — validado por: (1) build 2-gen VERDE (gen2 lê `teko.tkp` com o parser novo), (2)
exemplo `.tkp` construído verde com o compilador-parser-novo, (3) **fixpoint byte-idêntico**. Achado #4
(colisão de tag cross-namespace) descoberto e contornado aqui. INI mantém `.tkt`; TOML sem `.tkt` (dogfood).

**💡 TOML valida por DOGFOODING (ruling do dono 2026-08-15): os `.tkp` JÁ SÃO TOML.** Então a lane TOML
**não gera `.tkt`** — o melhor teste é **trocar o leitor de projeto** (`parse_manifest` em `src/build/manifest.tks`,
~160 ln) pra usar `teko::encoding::toml::parse_toml` + mapear o `TomlValue` Table → struct `Manifest`, e deixar
o **próprio compilador lendo seus `.tkp`** ser o teste. **Validação = build de 2 gerações:** gen1(seed, parser
velho) constrói gen2 do source novo; então **gen2 constrói de novo lendo `teko.tkp` com o parser NOVO** — verde
nesse passo prova o TOML sobre `.tkp` real. (INI mantém `.tkt` — não é dogfooded.)

**Ordem de drive (revisada — a ordem é praticidade, NÃO uma restrição a "só monomórfico"; ver correção do
dono acima):** núcleo crypto ✅ → `sort` ✅ → `encoding` — a lista COMPLETA
do §1.5: **texto** (json✅ · xml✅+C14N · csv · toml · ini · yaml), **binário** (cbor · msgpack · **bson** ·
protobuf · asn1), `fixed` (colunas fixas), **web** (base64✅ · url✅ · mime✅) → `math`/`numeric::bigint` ✅ →
`crypto::pk` (PRÓXIMO) → `crypto::jose`+`xmldsig`+`cose` (assinatura de documento) → `compress` (completar) → `password`. (BSON
pareia com `db::mongodb`.) Costuras que ficam FFI/pesquisa: `rand` (getrandom FFI), `3des` legacy,
`openssl`/`gpg` (providers FFI).

**Entregues stdlib (2026-08-15):** crypto core ✅ (`hash`/`mac`/`kdf`/`cipher`/`aead`) · `sort` mono ✅
(`813f83b7`) · `encoding::base64`/`url`/`mime` ✅ (`a1cbeb8e`) · `encoding::xml`+C14N ✅ (`4c636a7e`) ·
`encoding::cbor`/`msgpack`/`bson` ✅ (`c052fa9f`, bson byte-exato vs pymongo) · **`bigint` ops crypto ✅**
(`18dc3345` — DivMod/gcd/shl/shr/bit_len, **`mod_pow`** square-and-multiply, **`mod_inverse`** Euclides
estendido, I2OSP/OS2IP `from_bytes_be`/`to_bytes_be`, from/to_hex, from/to_dec_str; RSA-ready, cross-check vs
Python int nativo+C exit 0). **Encoding p/ assinatura COMPLETO** (base64+xml+cbor) + **`bigint` pronto** + **`crypto::pk` RSA ✅**
(`383271e0` — `rsa.tks`: PKCS#1-v1.5 + PSS sign/verify sobre bigint, SHA-256/384/512; keys por componentes
brutos n/e/d, sem ASN.1; non-CRT YAGNI. Cross-check byte-exato vs Python `cryptography` 41: PKCS1v1.5 assina
idêntico, PSS aceito pelo verifier OpenSSL. Lane fechou gap: adicionou **SHA-384** a `hash.tks` — faltava).
Desbloqueia **RS256/384/512 + PS256/384/512** (JOSE) · `rsa-sha256` (XML-DSig) · RSA COSE. **`crypto::pk`
parte 2 ✅** (`e46821f0` — `ec_p256.tks`: ECDSA/NIST P-256 short-Weierstrass afim sobre bigint, sign(k
externo)/verify, SEC1 `04||X||Y`, SHA-256/384/512; `ed25519.tks`: Ed25519 RFC 8032 completo, adição Edwards
unificada, `d`/base/`sqrt(-1)` computados dos inteiros RFC, não hex-mágicos. Cross-check: Ed25519 vs RFC 8032
§7.1 TEST 1/2/3 byte-exato; ECDSA P-256 (r,s) aceito pelo verifier OpenSSL de `cryptography`). **`crypto::pk`
COMPLETO** (RSA + ECDSA-P256 + Ed25519) → agora `jose`/`xmldsig`/`cose` fecham o conjunto comum
(HS/RS/PS/ES/EdDSA) de uma vez. Encoding restante (csv/toml/ini/yaml/protobuf/asn1/fixed) fica pra depois.
**Achados de compilador extras (seed .31, TODOS stale-seed/tree-mismatch — contornados; merecem re-check no
toolchain real):** (a) `pub` E `base` são keywords reservadas, não podem ser nome de parâmetro (contornado:
`pubkey`, `ground`). (b) `bigint::of(n)` p/ `i64` positivo pequeno retorna com flag de sinal negativo no seed
(reproduzido ISOLADO em `bigint.tks` intocado; análogo estrutural manual compila/roda certo → artefato de
seed velho, não defeito de fonte). (c) `byte ^ byte` (XOR bitwise entre dois `byte`) é rejeitado pelo checker
do seed ("B.38 needs integer not float") — afeta idêntico o já-landed `rsa_test.tkt` (l.202/258), i.e.
limitação de seed pré-existente/compartilhada, não introduzida. O seed .31 já falha type-check em grande parte
da árvore self-hosted atual ("unknown type") num checkout limpo — daí a prova offline ser o registro válido.
**⚠️ FORK DE NAMESPACE (decisão do dono):** §1.5 do catálogo diz `teko::math::bigint`, mas o **engine já
existe** como `teko::numeric::bigint` (lastreia `dec`, literais `123bi`, e `teko::math::checked` já aponta
callers >64-bit pra ele). O subagente **estendeu o engine existente** (regra "estenda não duplique"), NÃO
criou `math::bigint`. `crypto::pk` (interno) pode `use teko::numeric::bigint` já. **Recomendação:** adotar
`numeric::bigint` como canônico e corrigir §1.5 do catálogo (evita duplicação de engine bignum). Alternativa:
alias `math::bigint`→`numeric::bigint` quando conveniente. Aguarda ruling do dono de §1.5.
Costuras: `encoding::csv` `type CsvRow` visibility bug (lane csv); `pub→exp` dos módulos antigos DEFERIDO ao
§11; cbor float shortest-form + bson decimal128-arith são follow-on. **Dica validável:** value-trees
**struct-tagged** (não inline-union) deixam o seed velho RODAR os vetores → prova comportamental real.

**⚠️ ACHADOS DE COMPILADOR (seed `0.3.0.31-beta`, reportados pelos lanes; contornados in-module):**
(1) **cast-trap** — cast `i64→u64` de negativo (e `u64→i64` bit-alto) **SIGABRT em runtime**; código stdlib
que casta negativos entre signedness precisa contornar (carry/borrow u64-puro). (2) **codegen quirk** — dois
`loop` sequenciais compartilhando um `i64` mutado miscompilam (guard do 1º não pula). Ambos merecem olhar
compiler-side; podem já estar corrigidos na árvore atual (o seed é `.31`, a árvore está à frente).
(5) **`class` é keyword reservada** — não pode ser nome de param/campo/local (`TokenKind::Class`); contornar
(`tag_class`). Junto de `pub`/`base`. (6) **bitwise em `byte` bare** (`b & 0x80`, `b << 4`, etc.) trip
"B.38 needs integer not float" — castar operando `to u64` primeiro (como `cbor`/`bigint_ops` já fazem); é a
generalização do `byte ^ byte` já visto no `rsa_test`.
(3) **codegen: widening de união no `return` (REAL, na árvore atual)** — retornar um valor de união de
2-braços (`MsgRead | error`) de uma função declarada com 3-braços (`MsgRead | error | null`) faz o codegen
emitir o struct de C ESTREITO onde o largo é esperado → GCC rejeita (`incompatible types when returning
'tk_u_MsgRead_error' but 'tk_u_null_MsgRead_error'`). Contornado em `msgpack::decode_scalar` (destructure +
re-return single-arm). **Isto é bug de compilador na árvore ATUAL** (não seed velho) — o widening subtipo→supertipo
de união no `return` precisa ser coagido no codegen. Merece fix compiler-side.
(4) **codegen: colisão de TAG de união cross-namespace (REAL, árvore atual — LANDMINE tree-wide).** A
constante do enum de tag C de uma união `T | error` / `T | null` é nomeada **só pelo nome BARE (sem
namespace) de `T`**. Dois structs homônimos em **namespaces DIFERENTES**, ambos usados numa união
`T|error`/`T|null`, geram a **MESMA** constante C (ex.: `teko::encoding::toml::KeyStep` e
`teko::encoding::ini::KeyStep` → ambos `TK_TAG_U_KEYSTEP_ERROR_KEYSTEP`) → `cc` falha (`incompatible
types`), mesmo os dois type-checando isolados. Contornado renomeando (ini `KeyStep`→`SectionNameStep`).
**✅ RESOLVIDO (`e98fd5b0`, aprovado pelo dono 2026-08-16).** O trace refutou a hipótese upstream com evidência:
**resolver E dedup estavam CORRETOS** — `resolve_named` (`resolve.tks:1339`) monta `Named { name = qualify(...) }`
(canônico), e `type_eq` (`type.tks:205`) compara o nome INTEIRO → mantém distintos (é por isso que não há erro
de tipo duplicado; o critério do próprio dono, seguido até o fim, provou que o `Named.name` já é canônico). **O
codegen é que estirpava** (`cg_opt_mangle_str:2036` chamava `name_last_segment` no nome cheio; os paths sintático
e de pattern liam o spelling bare do AST). **Fix (codegen-only):** helper `cg_ns_c_key` (`::`→`__` sobre o nome
CANÔNICO) nos 4 sítios — leaf resolvido, type-expr sintático, chave de case do match (via C3b subject-derived,
não `ref_ns`, pois o namespace da função pode diferir do subject num match cross-import), stem `TK_TAG_<X>` de
variant nomeada. Prim/error/null/builtin e uniões no namespace ROOT byte-idênticas. **Validação:** build gen2
verde + FIXPOINT gen2==gen3 byte-idêntico (sha `898dc030`; o transitório gen_new emitido pelo compilador
old-mangle NÃO é o seed — o estável gen_new2==gen_new3 é) + fixture `examples/regressions/union_tag_cross_ns`
(a::Thing/b::Thing em `Thing|error`/`Thing|null`) build+run verde com símbolos DISTINTOS + **reseed do bootstrap**
(`898dc030`, provenance_gate PASS). **Irmãos deixados p/ depois (mesma classe, fora do #4):** nomes de instância
genérica `Base__g__<arg>` e constantes de enum `TK_E_<E>_<M>` — mesmo hazard de nome bare, precisam de mudança
lockstep checker+codegen + reseed próprios (relevante quando os genéricos entrarem).

**🔴 INCIDENTE — perna C do CI quebrou (2026-08-15) + correção de PROCESSO.** Causa raiz (diagnosticada
reproduzindo o build da perna C localmente, gen1 do `bootstrap/teko.c`): as lanes de stdlib validaram só com
`teko fmt --check` (que só faz **parse**, NÃO type-check nem codegen), então dois defeitos reais passaram
latentes e derrubaram o build: (a) **colisão de overload no namespace flat `teko::crypto`** — `fn` nu é
**namespace-private** (visível entre arquivos-irmãos do mesmo diretório), então helpers homônimos
independentes (`rotl32` cipher+hash, `le_u32_at`/`push_le_u32` hash-u32 + mac-u64) viraram overloads ambíguos;
o checker REAL rejeita (`ambiguous call to overloaded…`). O subagente do jose achou que era "falso" (assumiu
`fn`=file-private) — **é real**. Corrigido (`856e58dc`): deletado `rotl32` duplicado do cipher, renomeadas as
variantes u64 do mac (`le_u32_at_u64`/`push_le_u32_u64`). (b) o achado (3) acima (msgpack widening). **RESEED
faltante:** as lanes adicionaram módulos que o compilador **empacota no próprio binário** (C emitido 11.4→15.2
MB) mas nunca reseedaram o `bootstrap/teko.c` — velho desde `e99ca8f2` (§7). Sem bootstrap atual, o ladder da
perna C não alcança a ponta. **Reseed local (hand-harvest, `1c137b9f`)** feito: fixpoint C-route
`gen2.c==gen3.c` byte-idêntico (sha `8fdb1893…`) + cc-route self-reproduce + `provenance_gate.sh` PASS, com
PROVENANCE no formato item-14/§7. **CORREÇÃO DE PROCESSO (obrigatória p/ próximas lanes):** validar cada lane
com **build REAL** (gen1 do `bootstrap/teko.c` compila a árvore+delta VERDE end-to-end: checker→codegen→cc)
ANTES de drenar — `fmt --check` sozinho é insuficiente. E **reseedar** quando o C emitido mudar o bastante
p/ o ladder precisar do degrau (regra do dono: "o reseed é local e ocorre quando precisa fazer a escada
chegar ao objetivo").

**🟠 DÍVIDA DE TESTE p/ a FASE 2 "corrigir testes da perna C" (achado 2026-08-15, escopo fechado).** O build
`--no-verify --release` (usado no dreno) NÃO compila os `.tkt`, então erros no caminho de TESTE (`teko test .`,
que junta todos os `.tkt` do namespace) não aparecem no dreno. Levantados (a corrigir na Fase 2, não agora):
- **Colisão de helper nos `.tkt` de crypto** (mesma classe do incidente — namespace flat): `hex_val`×4,
  `hex_bytes`×4 (aead/cipher/kdf/mac), `byte_range`×2, `repeat_byte`×2 (kdf/mac), `hexed`×2 (aead/cipher) —
  **corpos byte-idênticos** entre arquivos. Fix: consolidar numa definição única (helper compartilhado) ou
  nomes distintos por arquivo. Baixo risco, mas **não dá pra build-validar `.tkt` sem `teko test .`** (proibido,
  risco de crash) — por isso fica pra Fase 2, junto com:
- **`const struct: initializer binds no field __hdr`** no codegen — um `const` de value-struct não popula o
  header fat do item-14; reproduz SEM as lanes novas (pré-existente, backend). Bloqueia o `teko test .` no gen1.
- **`byte ^ byte` (B.38 "needs integer not float")** — XOR bitwise entre dois `byte` rejeitado pelo checker do
  seed; afeta `rsa_test.tkt` (l.202/258) e outros. Provável limitação de seed; re-checar no toolchain atual.
Nenhum desses bloqueia o dreno das lanes de PRODUTO (`.tks` compilam verde no build real); são exclusivos do
caminho de teste, endereçados na Fase 2.

**🔒 REGRA DO DONO (2026-08-15): TESTES SÓ NO CI — porque `teko test .` local é CRASH CERTO DE OOM.**
(Correção do dono: eu tinha dito "é política, não crash" — ERRADO. É crash de OOM.) Rodar o gate de teste
localmente estoura a memória (o vazamento de monomorfização / o modelo de memória ainda-não-consertado), com
certeza, não por acaso. **Esse OOM é a própria razão de existir a divisão Doc 2 / Doc 1:** o **Doc 1
(arena/backend)** é o que conserta o modelo de memória; a política CI-only é CONSEQUÊNCIA disso. Ninguém —
coordenador nem subagente — roda `teko test .` ou variante. Validação local permitida = **compilação/build**
(`teko <dir> -o <out> --no-verify --release`, COMPILA mas não roda teste) + cross-check offline em Python. A
execução dos `.tkt` é exclusiva do CI. Todo brief de subagente carrega isso como HARD RULE.

**🧭 RELAÇÃO Doc 2 → Doc 1 (ruling do dono 2026-08-15): a Doc 2 prepara TODO o terreno pra a Doc 1 seguir,
dando suporte total ao que virá.** A Doc 2 = toda a superfície de linguagem/stdlib + **trocar TODAS as deps C
por Teko+FFI** (§16/§17, inclusive reimplementar a arena em Teko) — reescrevendo o que for necessário, sem
exceções. §10, genéricos, §11, §16, §17, FFI-stdlib entram TODOS na Doc 2 (nada "deferido"). A **Doc 1
apenas MELHORA a arena** (já em Teko) — otimização final, não reconstrução — sobre um terreno completo e livre
de C.

**VALIDAÇÃO (modelo correto — Teko NÃO tem VM):** os vetores crypto estão **offline-provados** (cada
subagente cross-checa contra Python `hashlib`/`pycryptodome` + porta o algoritmo Teko exato pro Python) +
**build real** (compile, nunca `teko test .` — testes só no CI, é OOM certo local). **CORREÇÃO DO DONO
(2026-08-15): garantir que AMBAS as pernas (C e native) compilem É O TRABALHO da Doc-2, não uma pré-condição.**
Eu tratava a perna-native-verde como bloqueio do §16 ("não pode arrancar o `teko_rt` até a native se sustentar")
— ERRADO. Reescrever o runtime C→Teko/FFI (§16) é **justamente o que fecha a perna native**; a vermelha de hoje
é o estado que esse trabalho conserta. Então: dentro da Doc-2, a reescrita do runtime deixa **as duas pernas
compilando**; os testes rodam no CI (nunca local). Nada de "native depois da Doc-1".

**⚖️ CRITÉRIO DE SAÍDA DA DOC-2 → ENTREGA À DOC-1 (ruling do dono 2026-08-16, afia o "ambas as pernas
compilando"):** a Doc-2 só fecha quando **AMBAS as pernas — C E NATIVE — fecham 3 portões cada:**
**(1) COMPILA** (árvore+delta atravessa checker→codegen→cc/native VERDE), **(2) FIXPOINT verde** (gen2==gen3
byte-idêntico, em cada backend), **(3) MEMPARANOID verde** (`MEM_PARANOID` limpo em cada backend). São 3×2 = 6
portões. **TESTES ESTÃO FORA DESTE CRITÉRIO** — explicitamente NÃO é sobre testes. `teko test .` (OOM local hoje)
é **papel da Doc-1**, que o conserta quando **(a) melhora o consumo de memória** da arena E **(b) implementa
multi-threading no compilador E nos testes**. A Doc-2 entrega à Doc-1 um terreno onde **as duas pernas compilam,
fazem fixpoint e passam MEM_PARANOID** — nada além disso é pré-req de saída.
**CONSEQUÊNCIA CONCRETA (muda escopo):** o bug pré-existente da perna NATIVE (`const struct: initializer binds no
field __hdr` no codegen, :1737) **DEIXA de ser "roteie ao redor com `TEKO_BACKEND=c`" e VIRA must-fix da Doc-2** —
sem a native compilando+fixpoint+memparanoid não há entrega. A validação-local segue em C (native local é caro/
bugado hoje), MAS a native-verde nas 3 dimensões é **portão de saída não-negociável**, provado no CI. O ganho de
`teko test .` local (o motivo do "testes só no CI") NÃO é portão Doc-2 — é o primeiro entregável da Doc-1
(memória + multi-threading do compilador e dos testes).
**SEQUENCIAMENTO (ruling do dono 2026-08-16): a perna native é a FASE TERMINAL da Doc-2, NÃO intercalada.**
"Manter como tem feito, foco na perna C; quando fechar o Doc-2 [o terreno-C: §16/§10-(c)/§11/FFI-stdlib com a
perna C nas 3 dimensões verde], AÍ eu entro com as correções pra perna native fechar limpa." Ou seja: **NÃO
persigo native-verde item-a-item** durante o §16/etc — sigo validando local em C (backend C, `--no-verify
--release`) o tempo todo. A perna native é um **passo de correção DEDICADO no fim da Doc-2**, depois que todo o
terreno-C está pronto — o momento certo porque a reescrita C→Teko/FFI (§16) muda muito do codegen que a native
emite; consertar a native antes seria retrabalho. **Ordem de saída da Doc-2:** (I) terreno-C completo (perna C nos
3 portões durante toda a jornada) → (II) **fase-native terminal**: consertar `const struct __hdr` + o que mais a
native exigir, até as 3 dimensões native verdes no CI → (III) limiar Doc-1 atingido → avisar o dono. A fase-native
é o ÚLTIMO trabalho da Doc-2, imediatamente antes do handoff.
**⚖️ ENDPOINT DO BOOTSTRAP — o entregável FINAL da Doc-1 é um SEED NATIVE, não mais um seed de C (ruling do dono
2026-08-16).** Hoje `bootstrap/teko.c` é um seed de C (reproduz por byte-identidade do C emitido). O arco completo:
**C-seed → [Doc-2: as duas pernas verdes nos 3 portões] → [Doc-1: melhora arena + multi-threading, e no FIM VIRA O
SEED de C→native] → SEED NATIVE.** Com isso a versão fecha **com binários nativos REAIS** (o seed passa a ser
produzido/reproduzido pelo backend native) **e com o "sweep de C"** (a dependência de C — seed-de-C, backend-C como
veículo de bootstrap — é varrida no final; a linguagem passa a se auto-hospedar em native puro). **POR QUE isso
torna a perna-native-verde da Doc-2 não-negociável, e não só "higiene":** um seed native só é viável se a native
**faz fixpoint byte-idêntico** (gen2==gen3 no backend native) — é exatamente essa propriedade que a fase-native
terminal da Doc-2 garante. A Doc-2 entrega a native reproduzível; a Doc-1, no seu passo final, **flipa o seed pra
native** e varre o C. Ou seja: o portão "native: compila+fixpoint+memparanoid" da Doc-2 **é a fundação do seed-
native da Doc-1** — sem ele, a Doc-1 não teria de onde semear em native. (Detalhe de reprodutibilidade native —
determinismo de codegen/link do backend native — é problema da Doc-1; a Doc-2 só precisa provar que a native fecha
os 3 portões no CI.)
**⚖️ DE-CONFLAÇÃO CRÍTICA — "backend C" ≠ "dependências C" (ruling do dono 2026-08-16).** "Foco na perna C" NÃO
significa pular o sweep de C. Há DOIS sentidos de "C" que eu estava misturando:
1. **BACKEND C** (`TEKO_BACKEND=c`): o compilador **emite código C**, que o `cc` compila. **ISSO FICA na Doc-2** —
   é a "perna C" onde eu foco e valido local.
2. **DEPENDÊNCIAS C** (`src/runtime/teko_rt.c`/`.h`, `src/assert/assert.c`, `win32_compat.h`): os arquivos C
   escritos à mão que o C emitido `#include`/linka. **ESSES são varridos no §16** — trocados por Teko + **FFI
   nativo** (extern/`dlopen`/syscall direto ao SO).
**Portanto o §16 (e o §17) são feitos AINDA em `TEKO_BACKEND=c`, como já planejado.** O estado pós-§16 na perna C:
o compilador **continua emitindo `teko.c`**, mas esse `teko.c` **não linka mais nenhum C à mão** — ele faz **FFI
nativo direto ao SO/libc** (a arena reimplementada em Teko chama `mmap`/`munmap` via FFI; assert/panic/IO idem). A
linha de build encolhe de `cc teko.c runtime/teko_rt.c assert/assert.c -o teko` para essencialmente `cc teko.c -lm
-ldl -o teko` (só o C emitido + libs do SO). **O "sweep de C" do §16 = remover os .c/.h à mão, NÃO remover o
backend-C.** O backend-C só é aposentado no FINAL da Doc-1 (o flip pra seed-native). Ou seja, dentro da Doc-2 eu
FAÇO o sweep das dependências-C (§16) enquanto ainda emito C — as duas coisas convivem: emissão-C + zero-runtime-C-
à-mão + FFI-nativo. É exatamente o que o §16 C1 (`extern type`, structs C-ABI) e o `teko::sys` (constantes curadas)
existem pra habilitar.
**ADENDO — toolchain por perna (ruling do dono 2026-08-16):** a diferença entre as pernas é o que a linha de build
aciona. **Perna C** (`TEKO_BACKEND=c`): emite `teko.c` → **`cc`/`clang`/`gcc` + linker** (`cc teko.c -lm -ldl -o
teko` — compilador C traduz o C emitido, depois linka). **Perna NATIVE**: o compilador emite **código nativo direto**
(objeto/máquina), então **APENAS O LINKER é acionado — SEM gcc/cc/clang** (`ld`/linker-da-plataforma resolve
símbolos + libs do SO → binário). É a essência do "native real": zero toolchain C no caminho — só emit-native →
link. Logo o **seed-native da Doc-1** é produzido assim (emit-native → `ld`), e o "sweep de C" final remove não só
as deps-C mas o **próprio compilador C do toolchain** (a perna C precisa de `cc`; a perna native precisa só de
`ld`). Durante a Doc-2 as duas convivem: valido em C (precisa de `cc`), e a native (linker-only) tem que fechar os
3 portões no CI.

**Entregues (2026-08-15):** §9.D ✅, §9.2b ✅, §13 item-14 ✅, §7 Part A ✅, §14 Família B ✅, **§15 global ✅**
(6 itens). Restam 4 (§11, §16, §17, §10) + a frente stdlib — todos na cauda entangled acima.

---

## 12. Decisões — todas resolvidas pelo dono (2026-08-10)

1. ~~`mut` na janela aditiva~~ — **superfície removida ERRA, sem no-op.** "Vale pra tudo": `let`/`mut`,
   `->`, receptor solto, `unsafe`, `ptr<T>` genérico são varridos para a grafia nova e depois erram — não
   ficam como grafia tolerada. (Na janela aditiva o parser aceita ambos só para o seed construir o `src/`
   ainda-não-varrido; pós-sweep+reseed, a grafia velha é erro de parse.)
2. ~~`base` contextual~~ — **`base` é keyword reservada** (§4); os sítios de produção que o usam como local
   são varridos para outro nome. Não fica como nome válido.
3. ~~chave-string da DI~~ — **mesmo tipo sob a mesma chave = ERRO DE COMPILAÇÃO** (§7); chaves distintas
   coexistem, nunca "último vence" silencioso.
4. ~~`chan_unbounded`~~ — entra, responsabilidade do dev.
5. ~~tipo do canal (enum `os`/`mem`)~~ — **transporte é um `service singleton & IChannelKind<T>` extensível**
   (`interface { fn init(key); fn send(T); fn recv(): T; fn end() }`): built-ins `OsChan` (default) /
   `MemChan`, e o dev pluga Kafka/Rabbit/RPC/UDP/WS/HTTP. **Totalmente estático — DI por CHAVE CONSTANTE:**
   `make<K: service singleton & IChannelKind<T>>(key, bounds): ctx` devolve o **ctx** (WaitGroup); **ambos**
   os extremos por `svc<Rx<T>>("chave")` / `svc<Tx<T>>("chave")` (comp-time, inline). O serviço vive na **raiz
   do programa (F2)** — exceção de lifetime, por ser comunicação entre tasks — do `make` ao fecho. Fechar = do
   **produtor** (`tx.close()`; reserva `ctx.close()`); `Rx::pop()` até o erro específico `Closed`. **Elimina
   passar id no `spawn`.** Não dispatch dinâmico do Round 3.
6. ~~`isolate` / `async`~~ — **sem necessidade de existirem** (o `await` prefixo basta; thread no mesmo
   processo não justifica `isolate` — isolação real = outro binário).

**Nada em aberto na superfície.** *(Pré-requisitos registrados, ordenados, que a onda de concorrência exige —
e nenhum é o dispatch dinâmico do Round 3: (a) **conformidade estática de interface** (`type X = interface
{…}`); (b) o **solver de constraint de forma** — `<T: forma & ifce | … | tipo>`, com `service [lifetime]` e
`notnull` (§9.2b); (c) o **`service` DI resolvido por chave constante** (`svc<T: service>("chave")`). O
transporte de canal depende dos três.)*

---

## 13. Value-struct mutável (roadmap item 14) — remover o bloqueio "struct é readonly"

**Antecipado** (§11): está no caminho crítico da Intent (o `pub set`, §10.3) e do `Ctx` performático. Remove
o bloqueio que torna struct imutável, para mutação **in-place** (comportamento de classe) **sem** virar
objeto — continua **value type** (cópia na atribuição/passagem, sem identidade nem heap), pela performance.

### 13.1 Modelo de mutabilidade — tudo é `var`

Sem `let`/`mut` (§1): **toda variável é `var`, mutável por default.** Não há marca de tipo "mutable struct"
— qualquer struct é mutável. Consequências (rulings do dono):

- **`val` NÃO é binding de usuário** — é **marca interna**, o que restou: o **alias de match** (`as`,
  readonly raso — não re-vincula, transparente a props/métodos internos, ver plano-match-universal) e
  **literais**. Fora disso, não se escreve `val`.
- **Parâmetros de função e método comportam-se como `var`** (garantido) — mutáveis no corpo. `val` fica só
  para aliases e literais.
- **Mutação = escrita direta:** `s.x = v` para campo acessível; **property `set`** para o
  controlado/computado/encapsulado (como a Intent: `_value` privado, só `pub set` escreve).
- **Value semantics (struct) vs identidade (objeto):**
  - **struct (value):** atribuir/passar **copia o valor**; mutar um parâmetro/local é **local**.
  - **classe/serviço (objeto, reference):** atribuir/passar copia a **referência**; `a.b = v` muta o objeto
    **compartilhado** — propaga por **identidade**, sem `ref`. (Sítio gated por visibilidade: `b` acessível
    ou `set` acessível.)
  - **`ref`** é a ponte do value type: `ref a: T` é **alias do slot** da variável do chamador, para
    **qualquer** `T`; permite **reatribuir tudo** (`func(ref a: T) { a = T { … } }`), não só um campo —
    preserva-se só o **ponteiro** do ref. (Borrow local, §3/§10.6.)

### 13.2 `self` — ref implícito, e onde aparece

**No acesso, `self` é `ref`** (Parte 2): num método, `self` é ref implícito do receptor, então a mutação
**gruda** na instância — e **não copia** o struct a cada chamada (a performance que motiva o item 14). Um
método que escreve `self` é **mutante**; um que só lê é **não-mutante**, e o compilador **infere** isso do
corpo — **(i) sem marca `mut fn`**. Chamar um método mutante sobre receptor **imutável** (literal, alias
`val`) = **erro**.

**Serve em qualquer tipo** — `struct` / `class` / `trait` / `service` — **exceto** subtipos de
primitivo/enum/flags, que são **`val-ref` readonly** (self é ref só-leitura; não se mutam).

**As posições de `self`:**
- **como TIPO** (o próprio tipo): em declaração de variável, campo, propriedade, retorno ou parâmetro —
  ex.: `static fn new(): self`, `p: self`.
- **construção:** `.{ … }` (target-typed, §4.1) — o `self { … }` construtor **foi retirado**; numa fábrica
  `(): self`, o alvo do `.{ }` é `self`.
- **acesso:** `self.x` / `self::y` — **aqui `self` é `ref`**.

**Materialização e o ponteiro do ref (representação em runtime):**
- **objeto (class/service):** cada instância tem **arena própria** → ponteiro/identidade intrínseco; o `ref`
  é natural.
- **struct (value):** é **fat** — carrega um **cabeçalho** (mais simples que o de objeto: só `uptr`/`ptr`, o
  ponteiro para si) que habilita o `ref`/`self`. Uma **cópia é nova materialização — novo cabeçalho, novo
  ponteiro** (é o que preserva value semantics: mutar a cópia não toca o original).
- **subtipo de primitivo/enum/flags (readonly):** é **thin** — fica do tamanho do primitivo, **sem
  cabeçalho**; o runtime **define `self` na hora da chamada** do método que o usa (self **transiente**). Por
  serem **readonly**, esse self-ref transiente basta — não há mutação a propagar. (Dava pra usar o mesmo
  transiente em struct, mas a escolha é **cabeçalho** para struct — mais direto que remontar `self` a cada
  chamada.)

### 13.3 `readonly` — imutabilidade opt-in (default é mutável)

Tudo é `var` por default; **`readonly` é o opt-in** de imutabilidade (modelo C#), em dois níveis:

- **struct inteira:** `readonly struct Foo { … }` marca a **struct toda** como somente-leitura (igual ao
  `readonly struct` do C#) — todos os campos são readonly.
- **campo:** `readonly id: u64` marca um **campo** individual como somente-leitura.

**Regra:** um campo readonly — solo ou vindo de uma struct readonly — só pode ser definido na **construção**
(`.{ … }` target-typed ou `Tipo { … }` nominal, §4.1) ou por **valor default** na declaração; **nunca mutado
depois**, nem por fora nem por dentro
(um método que tentasse `self.id = …` sobre campo readonly = **erro**). É a garantia à prova de mutação
**interna** que a property só-`get` (§13.2) não dá — as duas coexistem: `get`-only encapsula, `readonly`
congela.

---

## 14. Macros — duas famílias, identificadas por keyword (roadmap, cluster §12)

Teko ganha **macros** (trazidas do "pós-1.0 / no macros" por ruling do dono — reversão deliberada do
`DECISION_LOG:320`/`TEKO_LEGISLATION:607`), em **duas famílias distintas**, cada uma **identificada por
keyword** (o dev **diz** o que quer, o compilador **sabe** o que é) e chamada com **`@`**. As duas se
distinguem pelo **estágio de pipeline** — e é o estágio que **fixa o que os args são** (sem híbrido).

### 14.1 Família A — sintática *(keyword: `macro`)*
- **Aplica na AST, ANTES do type-check** (pré-passo): `parse → AST → [expand] → TYPE-CHECK do resultado →
  lower`. Args = **AST crua** (`.node`/`.source`/`.len`); os tipos **ainda não existem**, logo **sem
  `.type`/`.value()`** dentro do corpo. Produz **código** (AST), que é **type-checado depois** — código
  mal-tipado gerado falha no type-check normal, apontando a expansão.
- **O mecanismo DEFINIDOR: a macro COPIA-SE no local de uso — ALARGA a AST.** Não é chamada (como função)
  nem despacho — é **expansão inline**: o corpo é **copiado** na posição da chamada, com os args
  substituídos, e a AST **cresce** ali. É essa cópia-in-place que distingue uma macro de uma função/trait.
- **Não explicita tipos** — os args são **anônimos até serem materializados na AST** (e só então
  type-checados). É isso que **sustenta os variadics**: sem tipo explícito, a aridade é livre.
- **Uma macro nomeada ≈ `structural trait`, mas como HELPER que encapsula** — é o herdeiro **explícito e
  visível** do que a *structural trait* tentava ser (encapsular um padrão estrutural), só que **escrito à
  mão, sem shadow**, e copiado in-place no uso.
- **O splice: `lowering { … }` + `${expr}`.** O corpo da macro é **lógica comptime que roda ANTES de
  alargar** e decide **quais blocos `lowering` emitir** (ou nenhum) — expansão condicional/montada. Dentro de
  `lowering { }` tudo é **verbatim** (copiado na íntegra na AST); **exceto `${expr}`**, que **injeta o
  nó/valor computado** pela macro. (Nome `lowering` = "baixa código na AST", no lugar de `quote`; escape
  `${}`.)
  ```teko
  macro log_if(...args) {                       // sem retorno → EMITE código
      if args.len > 0 {                          // lógica comptime — roda antes de alargar
          lowering { teko::io::println(${args[0]}) }   // verbatim, com ${} injetando o nó do arg
      }                                          // if falso → não alarga nada
  }
  @log_if(msg)   // → teko::io::println(msg)   ;   @log_if()  → nada
  ```
- **`macro` NÃO tem retorno** (ruling do dono) — ela **se substitui e alarga a AST** (emite via `lowering`),
  **sempre**. Inspeciona args (`.len`/`.node`) na sua lógica comptime, mas **não retorna** — emite. O caso
  "computa um valor" **nunca é `macro`; é `comptime`** (que retorna literal, §14.2). Ex.: contar args é
  `comptime count(...args): usize { args.len }`, não `macro`.
- **Hygiene = mangle, sem erro** (ruling do dono). Colisão de variável **nunca erra**: um binding que a macro
  introduz no verbatim de `lowering` (o `var t` do `swap`) é **manglado** para um nome único (`t$…`), então
  jamais colide com o `t` do usuário. Dois detalhes forçados:
  - **mangle estável/determinístico** (chave = macro+sítio+nome+índice de expansão), **nunca aleatório** —
    senão duas compilações do mesmo fonte divergem e **quebram o fixpoint byte-idêntico**;
  - **só o verbatim dentro de `lowering` é manglado** (é da macro); o que entra por **`${}`** é **nó do
    usuário e fica INTACTO** (referencia o escopo do usuário — manglá-lo capturaria errado). É essa
    assimetria que É a hygiene. E ela é **estanque:** o `${}` é a **ÚNICA ponte** do escopo comptime da macro
    (fora do `lowering`) pra dentro do verbatim — nada de fora aparece dentro senão por escape. Por isso o
    mangle é **exatamente** o verbatim: o que está lá é da macro; o que entrou por `${}` é do usuário; a
    lógica comptime de fora nem vira AST.
- Modelo **Rust/Lisp** (expand-então-tipa). Funciona com args de runtime (`@count(a, b)` conta nós, não
  avalia nada).

### 14.2 Família B — avaliação comptime *(keyword: `comptime`)*
- **Os tipos JÁ são conhecidos** — roda **DEPOIS do type-check**: `parse → AST → TYPE-CHECK → [comptime eval]
  → lower`. Args = **valores comptime-conhecidos**; computa um **valor** inlined. Modelo **Zig comptime**
  (tipa-então-avalia).
- **Avaliar VALOR de runtime = erro; inferir TIPO de runtime = OK** (ruling do dono — a regra fina). Como
  roda pós-typecheck, o `comptime` **avalia valores** — e um valor só é avaliável se for **comptime-const**;
  **avaliar um local de runtime = erro**. Mas o **TIPO** de qualquer arg é comptime-conhecido (mesmo de um
  runtime), então uma `comptime` **genérica** pode **inferir o tipo** de um arg de runtime **sem avaliá-lo**.
- **O retorno da `comptime` (quando houver) é inlined como LITERAL** (ruling do dono): `var x = @sizeof<i32>()`
  fica como se fosse `var x = 4`.
- **`sizeof` são DOIS construtos** (correção): o **comptime** dá o **slot** de `T` (`T` explícito, sem param
  de valor); a versão que recebe um **valor** é uma **função runtime** (bytes ocupados). O `@` distingue:
  ```teko
  // src/mem
  exp global comptime sizeof<T>(): usize { /* tamanho do SLOT de T, comp-time */ }
  @sizeof<i32>()        // → 4  (comptime, slot)

  exp fn sizeof<T>(t: T | null = null): u64 { if t == null { return 0 } /* bytes OCUPADOS por t */ }
  sizeof(x)             // runtime — tamanho ocupado pelo valor x
  ```
  (o modificador `global` — acesso sem namespace, fim do shadow de parse — fica no §15.)
- **`@sizeof`/`@typename` = macro comptime** (visível, no-shadow) sobre a reflexão de tipo (`T.size`/
  `T.name`), **não builtin oculto**. Sub-decisão que resta: **quanto de reflexão de tipo expor**
  (`.size`/`.name`/`.fields`…) — e a reflexão só computa **valores comptime** (contar campos, somar
  tamanhos), **nunca** toca valor de campo em runtime.
- **NÃO há terceira classe de macro** (ruling do dono). "Runtime macro" é contradição — o código que a macro
  expande **já roda em runtime**; a macro em si é sempre **comp-time**. O limite é claro: **macro = comp-time**
  (A: expande AST; B: computa valor comptime) e **não lê valor de runtime**; **tudo que precisa de valor de
  runtime é FUNÇÃO/MÉTODO real** (stdlib). Um `eq_by_fields` que compara campos em runtime é **função/método**
  (ou a impl de interface+operador, §9.4), **não macro** — a "tensão do `.fields`" **dissolve-se**: nunca foi
  território de macro.

### 14.3 Consequências
- **Fork "o que args é" resolvido pelo estágio:** A = **AST-only**, B = **valores typed**. Sem construto
  híbrido (supera o 1C single-construct do `plano-macro`, a revisar).
- **FFI:** só `extern fn` → libc (§16). **NÃO há `extern macro`/`extern comptime`** — resolver macro/constante
  de header C exigiria toolchain C (gcc/cc/clang), **indeferido** (§16). Constantes de libc são consts Teko à
  mão, por-plataforma.
- **Chamada:** `@nome(...)` nas duas — o **`@` marca "executado pelo COMPILADOR"** (macro OU comptime);
  **bare (sem `@`) é função de RUNTIME** (ruling do dono, pela clareza). É essa a distinção do par `sizeof`:
  `@sizeof<T>()` (comptime, compilador) vs `sizeof(t)` (fn runtime). **Keywords fechadas: `macro` (Família A)
  / `comptime` (Família B)** (ruling do dono).

---

## 15. `global` — acesso sem namespace, e o fim do shadow de parse

**O que resolve (ruling do dono): parar o shadow do parse.** Hoje builtins (`sizeof`, `list`, `str`…) são
**injetados no parse** — shadow, invisível ao dev. Com `global`, viram **declarações reais e visíveis** (em
`src/mem`, `src/list`…) alcançáveis **sem qualificar o namespace**. É o **no-shadow aplicado aos builtins** — o
mesmo princípio que aposentou a structural.

- **`global`** modifica uma declaração de **nível de namespace** — **função, tipo ou constante**.
- **Variável NUNCA é global** — sem estado mutável global.
- **Acesso direto:** um símbolo `global` é alcançável **sem o namespace** — `@sizeof<T>()`, não `mem::sizeof`.
- **Colisão:** se já houver a **mesma assinatura exportada** como global → **erro de compilação** (assinaturas
  diferentes coexistem, como overload).
- **Compõe com visibilidade:** `exp global comptime sizeof<T>(): usize` — `exp` (entra no `.tkh`) + `global`
  (sem namespace) + `comptime`.
- **Tipos global** (confirmado): um `global type Intent` fica alcançável como `Intent`, não
  `teko::threads::Intent` — como `str`/`error` (core) já são, agora **explícito** via `global`.
- **Toda a superfície ambiente vira `global`** (ruling do dono). Tudo que hoje se usa sem qualificação —
  `println`, `sizeof`, ops de `list`/`str`, … — passa a ser **declaração `global` explícita** na stdlib
  (ex.: `global fn println(...)` em `src/io`), no lugar da injeção de parse. **Elimina o shadow por
  completo** — e amarra na triagem `exp` da stdlib (o que é `global` é a superfície ambiente exportada).

---

## 16. FFI libc-direct (roadmap §12) — `extern` aponta pra libc, `teko_rt` desaparece

**A meta (ruling do dono): o `extern … from "teko_rt"` de hoje DESAPARECE** e vira **apontamento FFI pra a
libc de cada OS e arquitetura**. É por isso que **macros e pragmas** (`#os`/`#arch`) importam — a variação
por-plataforma dos bindings vive atrás deles.

- **Forma (Fork A = A3):** a **lógica pura** (os twins `str_eq`…) vira **função Teko** comum (sai do seam,
  **sem `extern`**); o **piso de syscall** (`write`/`read`/`malloc`) vira **`extern fn` apontando pra libc**
  (per OS/arch), resolvido pelo **own-linker** (.33–.34) **sem `cc`**. O `teko_rt` **some por inteiro** —
  parte vira Teko, parte vira extern-libc.
  ```teko
  #os("linux") extern type Stat = struct { dev: u64; ino: u64; mode: u32; size: i64 }  // extern type = layout-C
  extern fn fstat(fd: i32, out: ref Stat): i32 = "fstat" from lib "c"                   // ref → ponteiro opaco
  ```
- **Sem sigilos** (`*`/`&` foram removidos, §5/§6): FFI por-referência é **`ref`** — o compilador troca
  `ref os: Stat` pelo **ponteiro opaco** na fronteira. Nada de `*Stat`/`*u8`.
- **Struct C-ABI = `extern type`, NÃO `#repr`** (ruling do dono): `extern type Stat = struct { … }` declara o
  **layout externo** (ordem/padding do C). Reusa o `extern` (como `extern fn`), explícito (no-shadow), **sem
  pragma nova** — resolve o fork "explícito vs inferência" sem nenhum dos dois.
- **Tipos na fronteira (correção do dono):** **`isize` NÃO existe** — é **`size`** (palavra de máquina
  assinada) ou **`usize`** (§8). O ponteiro é **sempre opaco e vem do `ref`**, nunca de sigilo.
- **O compilador CONVERTE tipos na fronteira FFI** (ruling do dono) — marshalling teko↔C **automático**
  (`str`↔`char*`+len, `i32`↔`int`, `size`↔palavra…) pra **reduzir a opacidade**: o dev **não embrulha tudo à
  mão**. Sub-decisão que resta: **quais conversões o compilador conhece** (a tabela) vs o que fica opaco.
- **RUNTIME FFI — REQUISITO (ruling do dono 2026-08-16):** DUAS classes de FFI. **(1) Core (libc/syscalls, §16):**
  libc está SEMPRE no target → `extern fn … from lib "c"` **linkado em compile-time**, sem exigir nada instalado
  (libc universal). **(2) Libs OPCIONAIS (FFI-stdlib: rand/openssl/gpg/net/db/rpc):** NÃO estão sempre instaladas
  → **FFI em RUNTIME** (`dlopen`/`dlsym` POSIX; `LoadLibrary`/`GetProcAddress` Windows). O programa **COMPILA SEM a
  lib presente**; carrega por nome no RUN; **ausência = erro-como-valor** ("lib não encontrada"), não falha de
  link. Objetivo: **teko NÃO exige tudo instalado na hora de COMPILAR** — só a máquina que RODA precisa da lib.
  Mecanismo built sobre compile-time-FFI a **libdl** (universal). **Consistente com o Fork-D-indeferido:** não
  precisa dos HEADERS (assinatura declarada em Teko, como `teko::sys`) nem da lib no compile — só do símbolo por
  nome no run. **Satisfaz os transportes plugáveis do §10** (Kafka/Rabbit/WS, "dynamic-FFI §16-gated"). **Forma:**
  o `extern fn … from lib` ganha uma **variante RUNTIME** (`from lib "ssl" runtime` / handle `dlopen`+`dlsym`
  por-símbolo, per-`#os`) além da compile-time-linked. Desenhado no pass de **FFI-stdlib**; NÃO muda o §16-core
  (`extern type`/libc compile-time).
- **Fork D (extern macro / extern comptime de C) — INDEFERIDO** (ruling do dono): trazer macro/constante de
  **header C** exigiria um preprocessador/toolchain C (gcc/cc/clang) — **a dependência que estamos
  removendo**. Não cruzamos essa fronteira. Consequência: **constantes/flags de libc (`O_RDONLY`, `SEEK_SET`,
  `EAGAIN`) são consts Teko À MÃO**, por-plataforma (`#os`/`#arch`), numa **`teko::sys` curada** (escrito uma
  vez, certo, por plataforma). Isso também responde **quem escreve os bindings: `teko::sys` curada**, não
  resolução automática de header.

---

## 17. Compilação condicional (roadmap §12) — `#if/#elseif/#else/#endif`, com `#os`/`#arch` como atalhos

**Ruling do dono: as marcas condicionais ENTRAM** (o Fork B3 do prep, "adiar `#if`", cai). `#os`/`#arch` **não
são um eixo à parte** — são **atalhos** (açúcar) de um processo `#if/#elseif/#else/#endif`. É **quase uma
macro, mas um atalho**: um **prune de build-time** (quais itens/regiões sobrevivem ao type-check), **distinto
da família `comptime`** (§14, que computa valor).

- **Geral:** `#if(<pred>) … #elseif(<pred>) … #else … #endif` — região condicional, podada por um **predicado
  de build-time** (sobre `os`, `arch`, e flags de build). Aceita predicado **composto** (`&&`/`||`/`!`).
- **Atalhos:** `#os("linux")` ≡ `#if(os == "linux")`; `#arch("arm64")` ≡ `#if(arch == "arm64")` — a
  forma-atributo pra **um item só**, o caso comum.
- **Alarga a TODOS os itens de topo** (o antigo Fork B1): `#os`/`#arch`/`#if` guardam `fn`, `type` (incl.
  `extern type`, §16), `const`, extern-block — não só função.
- **É prune, NÃO `comptime`:** o predicado é avaliado **no prune** (build-time) e decide o que sobrevive ao
  type-check — não computa valor, não gera código. Exige um **avaliador booleano-sobre-constantes** no prune
  (que o dono aceita como o mecanismo desta família — não reabre o `comptime`).
- **Subsume os Forks B e C do prep:** o `#if` geral é a forma; os atributos são os atalhos.
