# Superfície da teko v0.1.0 — inventário construto a construto

A teko da v0.1.0 é o `ngen/`: um **módulo do [`mc`](https://github.com/minicompiler/mc)** que
ensina a superfície teko sem tocar no `src/` do mc nem no `src/` deste repositório (D211/D212).
Cada construto abaixo é provado por uma fixture de `ngen/tests/*.tk` que o compilador ensinado
**compila e roda** — o cabeçalho `// expect-exit: N` é o oráculo, e as cinco pernas nativas do CI
rodam as 45. Base deste inventário: `fix/retirement` `d1b7c362`, mc **0.15.12**.

Nomes de fixture são relativos a `ngen/tests/` (sem o `.tk`). "§" = seção de `ngen/HANDOFF.md`.

## 1. O que a v0.1.0 tem

### 1.1 Tipos e classes

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| `struct` | `struct Point { u8 a; i64 b; }` | `types_struct` | sem reclaim próprio (§5 dívidas conhecidas) |
| `class`, campo, método, `virtual`/`override` | `class Square : Shape { public override i64 area() { ... } }` | `types_class`, `order_bases` | uma base por classe |
| `this` implícito e `base` | `side` é `this.side`; `: base(n)` no construtor | `types_class`, `order_bases` | D219 |
| construtor / destrutor | `public Cell(i64 x) { ... }` · `~Cell() { ... }` | `surface_reclaim` | um destrutor por classe |
| `new` | `Cell c = new Cell(40);` | `types_class`, `surface_reclaim` | — |
| `public`/`private`/`protected`/`internal`/`static` | `protected i64 seed;` | `surface_property`, `types_class` | D220; **sem tipo aninhado** |
| `abstract class` / membro `abstract` | `public abstract i64 area();` | `surface_abstract` | `abstract` em `trait` não ensinado |
| `partial class` | `partial class Shape { ... }` (várias partes) | `surface_partial` | **sem método parcial**; parte depois do 1º uso é recusada |
| ordem LIVRE de declaração | usar `Circle` acima do `class Circle` | `order_types`, `order_bases` | base/interface QUALIFICADA declarada abaixo, e base de outro namespace (§5 §50) |
| `const` (topo e membro) | `const i64 BASE = 30;` | `surface_const` | sem `const` local |
| sobrecarga de método e de função livre | `i64 area()` + `i64 area(i64 k)` | `surface_overload_method`, `surface_overload_free` | duas sobrecargas que diferem só por `ref`/`out` são recusadas |
| oráculo de tipo estático | `pick(p.n)`, `pick(n - 1)` resolvem a sobrecarga | `surface_typeof_param`, `surface_typeof_expr` | receptor que é PARÂMETRO não é tipado no parse (§5 V0 item 3) |

### 1.2 Interfaces e traits

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| `interface` + tabela de interface | `class Square : Shape, Named { ... }` | `types_interface` | membro de interface é público |
| corpo DEFAULT, propriedade e `static abstract` em interface | `i64 doubled() { return area() * 2; }` | `surface_iface_default` | interface não declara campo |
| herança de interface | `interface I2 : I1, I0 { }` | `surface_iface_inherit` | sem covariância/contravariância |
| `trait` (modelo PHP, flatten em compile-time) | `trait Counted { i64 n; }` + `use Counted;` | `types_trait` | `insteadof`/`as` não ensinados; trait não traz `const` (D216) |

### 1.3 Genéricos

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| genérico de tipo + parâmetro `const` | `class Box<T, const N: i64> { T items[N]; }` | `surface_generics` | argumento `const` é literal inteiro ou `const` declarado |
| aninhamento e fecho em `>>` | `Holder<Box<Circle, 2>>` | `surface_generics` | genérico QUALIFICADO (`geo.Box<i64>`) fora (D31.14) |

### 1.4 Delegates e lambdas

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| `delegate` nomeado, `null`, chamada | `delegate i64 Op(i64 a, i64 b);` · `Op f = add;` | `surface_delegate` | `delegate` genérico e `op.Invoke(x)` fora |
| lambda explícita, função local, captura `use (a, &b)` | `Op f = new Op((i64 a, i64 b) => a + b);` | `surface_lambda` | grafia curta em argumento de função livre/sobrecarregada; `return x => e;`; lambda aninhada (§5 §41) |
| chamada de delegate `null` = pânico | `Op f = null; return f(1, 2);` | `surface_panic_null` | exit 70 |

### 1.5 Arrays

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| array fixo local/global, `a[i]`, `+=`, `.Length` | `u8 a[4]; a[0] = 1;` | `surface_arrays` | tamanho é literal inteiro positivo |
| `T[]` de heap, `new T[n]`, `.Length`, RC por elemento | `i64[] xs = new i64[3];` | `surface_array_heap` | `params T[]` e `ref`/`out T[]` fora |
| `T[]` global | `i64[] g;` … `g = new i64[n];` | `surface_array_global` | global é RAIZ, nunca liberada; namespaced fica BARE |
| índice fora do array = pânico | `xs[9]` | `surface_panic_index` | exit 70 |
| campo array inline | `T items[N]`, alcançado por `this.items[i]` | `surface_generics`, `surface_foreach` | campo array de tipo declarado ABAIXO não ensinado |

### 1.6 Namespaces, `import` e escopo

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| `namespace A.B { }` e file-scoped `namespace A.B;` | `namespace geo { ... }` | `surface_namespace` | **aninhado NÃO ensinado** (um nível) |
| `using A.B;` | `using Geo;` | `surface_namespace`, `order_types` | sem `using G = geo;` nem `using static` |
| função livre em namespace (mangling por passe) | `geo.area(3)` | `surface_namespace_fn` | — |
| `import A.B;` | `import parts.geo;` | `surface_import` | vem antes de todo namespace, e é recusado dentro de um; `#include "x.tk"` cru não é varrido |
| escopo léxico de bloco | `{ Ledger s; }` não vaza para fora | `surface_scope` | — |

### 1.7 Controle de fluxo

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| `loop` e `break N` do núcleo do mc | `break 2;` | `surface_loops`, `surface_switch` | D221 — `loop` fica |
| `while`, `do ... while`, `for` | `for (i64 i = 1; i <= n; i++) { ... }` | `surface_loops` | o lado esquerdo do passo do `for` é um nome |
| `foreach` | `foreach (i64 x in xs) { ... }` | `surface_foreach` | fontes: `T[]` local, array fixo local, campo array; **não** sobre parâmetro/global/forward; o tipo do elemento só alarga |
| `switch` STATEMENT | `switch (n) { case 1: ... break; default: ... }` | `surface_switch` | rótulo é expressão constante; controle não cai para fora de um `case` |
| `switch` EXPRESSION (açúcar sobre o ternário) | `v switch { 1 => 100, 2 or 3 => 200, _ => 0 }` | `surface_switch` | precisa de um braço `_`; `when` no `_` FINAL é recusado |
| ternário | `i64 x = c ? 42 : 0;` | `surface_ternary` | os dois braços têm o mesmo tipo |

### 1.8 Parâmetros

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| valor default (método e função livre) | `i64 add(i64 a, i64 b = 10)` | `surface_default_method`, `surface_default_free` | o default é constante; nenhum parâmetro sem default vem depois de um com default |
| `params` (lista variádica) | `i64 total(params xs)` · `xs_len`, `xs[i]` | `surface_params` | grafia do núcleo (`params xs`, não `params i64[] xs`); no máximo doze argumentos; **float fora** |
| `ref` / `out` | `void bump(ref i64 x)` · `void mkbox(out Box b)` | `surface_refout` | só em posição de parâmetro; recebe NOME, nunca chamada; `ref f64` devolve valor errado (§5 K2w) |

### 1.9 Propriedades e operadores

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| propriedade auto, expression-bodied e block-bodied | `public i64 Label { get => 7; }` · `{ get; set; }` | `surface_property` | acessor não é mais visível que a propriedade; `value` é o nome que o `set` recebe |
| sobrecarga de operador (C#: `public static`, nomeia os dois operandos) | `public static Vec operator+(Vec a, Vec b)` | `surface_operator` | operador é de classe ou struct, não de interface; sem `virtual`/`abstract`; sem default no parâmetro |

### 1.10 Injeção de dependência (resolvida em compile-time, D229)

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| marcadores de lifetime | `class Svc : IServiceSingleton { }` (`IServiceScoped`/`IServiceTransient`) | `surface_di` | serviço é classe; classe abstrata não é serviço |
| `inject T` e injeção por CONSTRUTOR | `IClock a = inject IClock;` | `surface_di` | um só construtor injetável; sem chave, factory ou decoração |
| `scope { }` | `scope { ... }` | `surface_di_scope` | escopo é LÉXICO; `inject` dentro de lambda toma o serviço do escopo de fora |

### 1.11 Memória

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| arena fixa de 4 MiB com free list por classe de tamanho | `ngen/lib/rt.tk` | `surface_reclaim` | tamanho fixo |
| refcount por ESCOPO (RC injetado no passe) | implícito; `~Cell()` roda quando zera | `surface_reclaim` | `struct`, pacote de `params` e campo `static` sem reclaim (§5) |
| pânico com `exit 70` | `panic("...")` (`ngen/lib/rt.tk`) | `surface_panic_null`, `surface_panic_index` | — |

### 1.12 Primitivos

| construto | grafia | fixture(s) | limite conhecido |
| --- | --- | --- | --- |
| escalares e aliases (`byte`, `char`, `isize`, `usize`, `bool`) | `usize u = (usize) b + (usize) c;` | `primitives_scalar`, `hello` | alias sobre id do núcleo: `isize` É `i64`, `usize` É `u64` (D131) |
| float `f32`/`f64` e reinterpret de bits | `f64 sum = a + b; u64 bits = tk_f64_bits(sum);` | `primitives_float` | acesso INDIRETO a float tem limites (§5 higiene 4) |
| `ptr`/`uptr` | `ptr p = at(tbl, 0); return ld64(p);` | `primitives_ptr` | `ptr` e `uptr` colapsam no mesmo ponteiro opaco |
| `str` (view, zero cópia) | `str s = "hi"; str tail = tk_str_slice(s, 1);` | `primitives_str` | `str` é o `uptr` do mc; não há tipo string próprio |

## 2. O que fica FORA — recusado com mensagem

A regra do corte (plano §76/§77) é **zero resultado errado silencioso**: o que não está ensinado
é recusado no estilo `teko: <causa curta>`, com `arquivo:linha`.

| construto | mensagem exata | onde registrado |
| --- | --- | --- |
| `T[][]` / multidimensional | `teko: an array of arrays is not taught yet` | `ngen/teko_heaparr.tk:275`; §5 (§41, §50) |
| `namespace` aninhado | `teko: a nested namespace is not taught` | `ngen/teko_ns.tk:1060`; D31.1 |
| método `partial` | `teko: a partial method is not taught; only a partial class` | `ngen/teko_class.tk`; D224 |
| tipo aninhado | `teko: a type is declared at top level; there is no type inside a type` | `ngen/teko_struct.tk`; D220 |
| float como argumento de `params` | ``teko: a `params` list holds words; a float argument is not taught yet`` | plano §76(a) |
| elemento de `params` lido como float | ``teko: a `params` list holds words; its element does not read as a float`` | plano §76(a) |
| `when` no braço `_` FINAL da switch expression | ``teko: the last `_` arm of a switch expression cannot carry a `when` `` | plano §76(b) |
| `params` fora de posição de parâmetro | ``teko: `params` declares a parameter list, nothing else`` | `ngen/teko_params.tk` |
| `[` sobre receptor que o parse não tipa | ``teko: `[` indexes a `params` list only`` | §5, dívida do C8 |
| `match`, `when` solto, `var` | `teko: match not taught yet` · `teko: when not taught yet` · `teko: var not taught yet` | D218 (fora da superfície) |
| `Func<>`/`Action<>` e multicast `+=`/`-=` de delegate | não há tipo nem operador declarado: `teko: not a generic type` (`teko_generic.tk:542`) e `teko: <Tipo> declares no operator ` / `teko: no operator <op> takes these operands` (`teko_ops.tk:527/573`) | §5 (dívidas §41); `ngen/README.md` |
| DI por chamada genérica (`Services.Get<T>()`) | não há superfície; a forma ensinada é marcador + `inject` | D229; `ngen/README.md` |
| `using static` e genérico qualificado | não ensinados | D31.14; `ngen/README.md` |
| `params T[]` embrulhando lambda, e target-typing de lambda | sem mensagem `teko:` própria: `params T[]` não existe (o núcleo não tem `[` em posição de parâmetro) e a grafia curta só resolve nas posições cobertas — a forma explícita `new Op(...)` é a ensinada | §5 (dívidas §41); `ngen/README.md` |

## 3. Números (tip `d1b7c362`, host macOS/aarch64, mc 0.15.12)

| item | valor |
| --- | --- |
| fixtures | **45** (`ngen/tests/*.tk`) mais 3 partes auxiliares (`ngen/tests/parts/`) |
| linhas dos módulos | **17 077** em `ngen/*.tk` (32 arquivos; o maior é `teko_class.tk`, 1 572) |
| linhas do runtime | **370** em `ngen/lib/rt.tk` |
| linhas das fixtures | 5 310 |
| compilador ensinado | `ngen/build/teko` = **1 508 545 B** (`mc build ngen --config <host>`, 2,2 s) |
| `mc limits ngen` | `verdict ok`; `intrin 8/16`, `types 7/14`, `heap 530 864/33 554 432` |
| mc pinado | **0.15.12** (`ngen/MC_VERSION`; `.github/actions/setup-mc` lê o arquivo) |
| pernas de CI | **5 nativas** (linux/x86_64, linux/aarch64, macos/aarch64, windows/x86_64, windows/aarch64) e **5 de `fixpoint`** nos mesmos pares; agregador `mc build ngen && run` |
| hash do pacote | **9318b19cb5b76629c03ca1b354d0d93bc3860e8a4f7747945cd19e235a5e5004** (pós-V0, medido 2026-09-06); o hash muda a cada byte de `mc.toml`/`files` e é re-medido ao cortar a tag. Valor anterior a V0 (antes das 3 mudanças): `057e7aed…`; primeira tentativa: `d41a0c80…` |

## 4. O que a teko é hoje (notas da Release)

A teko v0.1.0 é uma linguagem de superfície C#-like que vive inteiramente como módulo do `mc`:
classes com herança e vtable, interfaces com corpo default e herança, traits no modelo PHP,
genéricos com parâmetro `const`, delegates e lambdas com captura, arrays fixos e de heap com
checagem de índice, namespaces com `import`/`using`, `switch` nas duas vertentes do C#, ternário,
`ref`/`out`/`params`/defaults, propriedades, sobrecarga de operador e injeção de dependência
resolvida em tempo de compilação — tudo sobre uma arena de 4 MiB com refcount por escopo,
destrutores e pânico com `exit 70`. As 45 fixtures compilam e RODAM em cinco pares (os, arch)
nativos, e o compilador se reproduz: a escada `teko0 → teko1 → teko2 → teko3` fecha ponto fixo
byte-idêntico nos mesmos cinco pares. O que ainda não é ensinado não é aceito: é recusado com
`teko: <causa>` — a regra do corte é zero resultado errado silencioso.
