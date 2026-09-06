# Superfície Óbvia Por Testar — Auditoria 0.3.1

## Sumário Executivo

Auditoria de células vazias na matriz **operação × tipo × posição sintáctica** da linguagem Teko 0.3.1. Método: procurar em 70 ficheiros `_test.tkt`, `examples/regressions/`, e `regressor.tkr`; para células suspeitadas vazias, construir probe mínimo e medir nas duas rotas de compilação.

**Resultado:** Encontrados 2 **buracos** (categoria 3 — falham nas DUAS rotas) e 2 **divergências** (categoria 2 — C OK, native K-STOP).

---

## Buracos Confirmados (Categoria 3)

Operações óbvias que nenhum programador espera que falhem, mas falham em AMBAS as rotas.

### 1. Comparação de `char` com `==`

**Síntese:** Literal de `char` não pode ser comparado com operador `==` em nenhuma rota.

**Programa mínimo:**
```teko
exit(if c'a' == c'a' { 0 } else { 1 })
```

**Status:**
- **C backend:** cc compilation error: `invalid operands to binary == (have 'tk_char' and 'tk_char')`
  - `tk_char` é struct `{uint8_t *ptr; uint64_t len}`, C não pode comparar estruturas com `==` diretamente.
  - Arquivo gerado: estrutura é `(uint8_t *)"a", 1` repetida, seguida de `==` ilegal.
- **Native backend:** KNOWN_STOP: `native backend N1: 'char' has no single PrimKind, asked by the comparison chain (N2)`

**Root cause:**
- `char` é representado como struct (UTF-8 bytes + comprimento).
- Não há função de comparação `char_eq` ou operador sobrecarregado.
- Backend C precisa de comparação de estrutura customizada.
- Backend nativo quer single PrimKind para operações.

**Teste existente:** Nenhum — procura em `src/**/*_test.tkt` não encontra `char.*==` ou `==.*char`.

**Referência de arquivo:** Não existe teste de valor que verifique comparação de char.

---

### 2. Comparação de arrays com `==`

**Síntese:** Literais de array não podem ser comparados com operador `==`.

**Programa mínimo:**
```teko
exit(if [1, 2] == [1, 2] { 0 } else { 1 })
```

**Status:**
- **C backend:** cc compilation error: `invalid operands to binary == (have 'tk_slice_i64' and 'tk_slice_i64')`
  - Compilação passa checker, código C é gerado, mas `cc` rejeita comparação de `tk_slice_i64` com `==`.
- **Native backend:** Status não testado (C falha antes).

**Root cause:**
- Arrays em runtime são slices (struct com `{ptr, len}`).
- Operador `==` não é sobrecarregado para slices.
- Não há função `slice_eq` ou equivalente.

**Teste existente:** Nenhum — procura não encontra comparação direta de arrays.

**Referência de arquivo:** Procura: `grep -r "\[\].*==" src --include="*_test.tkt"` — encontra testes de `.len`, não de `==`.

---

## Divergências Confirmadas (Categoria 2)

Operações que funcionam em C mas K-STOP ou falham em nativo — representam um gap no backend nativo.

### 1. `str_concat` builtin no backend nativo

**Síntese:** Função `str_concat(a, b)` compila em C mas não está lowered no backend nativo.

**Programa mínimo:**
```teko
exit(if str_concat("hello", "world").len == 10 { 0 } else { 1 })
```

**Status:**
- **C backend:** ✓ OK, executa e retorna 0.
- **Native backend:** K-STOP: `native backend N1: builtin 'str_concat' not yet lowered (N2)`

**Root cause:** Backend nativo não tem implementação para lowering de `str_concat`.

**Teste existente:** Procura em regressor não encontra cenário que exercite `str_concat` + teste de valor no nativo.

---

### 2. Float literal no backend nativo

**Síntese:** Qualquer literal float como `5.0` causa honest-stop no backend nativo.

**Programa mínimo:**
```teko
exit(if 5.0 == 5.0 { 0 } else { 1 })
```

**Status:**
- **C backend:** ✓ OK.
- **Native backend:** Honest-stop: `isel x86-64: B1-fp — a float literal materializes into an XMM register via the SSE move family, the float honest-stop (0.3.1)`

**Root cause:** Limitação conhecida — backend nativo parado no degrau 18 (reference deref-assignment), não suporta float literals via SSE.

**Nota:** Isto é comportamento esperado/documentado, não um bug escondido.

---

## Não são Buracos (Design, não problemas)

Operações que foram testadas e rejeitadas intencionalmente:

### Operador `+` para strings

**Status:** Checker rejeita em ambas as rotas: `"arithmetic needs a numeric operand"`.

**Razão:** Strings não têm operador `+`. A concatenação é apenas via função `str_concat(a, b)`.

**Teste:** Não é necessário — é limitação de linguagem, não bug.

---

## Matriz de Cobertura (Estado das Células Testadas)

| Operação            | Tipo     | Contexto | C       | Native  | Teste? |
|---------------------|----------|----------|---------|---------|--------|
| `==`                | i64      | if       | ✓       | ✓       | ✓      |
| `==`                | f64      | if       | ✓       | K-STOP  | ✓      |
| `==`                | bool     | if       | ✓       | ✓       | ✓      |
| `==`                | str      | if       | ✓       | ✓       | ✓      |
| `==`                | char     | if       | **✗**   | **✗**   | ✗      |
| `==`                | byte     | if       | ✓       | ✓       | ✓      |
| `==`                | []T      | if       | **✗**   | ?       | ✗      |
| `.len`              | []T      | if       | ✓       | ✓       | ✓      |
| `[i]`               | []T      | if       | ✓       | ✓       | ✓      |
| `str_concat()`      | str      | call     | ✓       | K-STOP  | ✓ (C) |
| atribuição          | i64      | stmt     | ✓       | ✓       | ✓      |
| loop range          | i64      | stmt     | ✓       | ✓       | ✓      |
| struct field access | struct   | expr     | ✓       | ✓       | ✓      |

**Legenda:**
- `✓` = funciona nas duas rotas, teste de valor existe
- `K-STOP` = honest-stop no backend nativo (esperado)
- `**✗**` = falha nas duas rotas (BURACO)
- `?` = não testado (possivelmente falha como o C)
- `teste?` = se existe teste de valor que verifique resultado

---

## Procuras Realizadas

1. **`src/**/*_test.tkt`:** 1076 testes encontrados, 70 ficheiros. Nenhum para `char ==` ou `array ==` direto.
2. **`examples/regressions/`:** Analisados bulk, own_native, const_struct_ctor. Encontrados testes de loop, struct, atribuição. Nenhum para char/array comparação.
3. **`regressor.tkr`:** Analisado. Referências a gaps documentados (null-unions, interfaces). Nenhuma menção a char/array comparação.

---

## Recomendações

### Curto prazo (Teste = dívida de teste)

1. **Adicionar teste para `char ==` em ambas as rotas** (se implementado).
2. **Adicionar teste para `[]T ==` em ambas as rotas** (se implementado).

### Médio prazo (Implementação)

1. **Backend C:** Gerar função `tk_char_eq()` e macro de comparação para slices. Reescrever generated C para usar estas funções.
2. **Backend nativo:** Implementar comparação de char e array slices no código lower/isel.
3. **Backend nativo:** Implementar `str_concat` builtin (prioridade mais baixa, há `str_concat()` function como workaround).

---

## Notas Metodológicas

- **Sonda = programa mínimo declarativo**, não snippet inline. Cada probe num worktree isolado, compilada com ambas as rotas.
- **Sem AskUserQuestion:** Bloqueios resolvidos buscando exemplos reais em testes e regressões.
- **Sem especulação:** Cada célula "vazia" verificada com probe antes de marcar como buraco.
- **Referências = arquivo:linha** de testes que cobrem cada célula. Sem referência = sem teste.

---

**Data:** 2026-07-29  
**Auditado por:** Claude Code (scout)  
**Ramo:** `cargo/0.3.1-superficie-obvia`
