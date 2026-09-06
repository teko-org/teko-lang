# Auditoria de Superfície Óbvia — Progresso (0.3.1)

## Problemas Identificados

### 1. Comparação de `char` com `==` — FALHA NAS DUAS ROTAS

**Código:**
```teko
exit(if c'a' == c'a' { 0 } else { 1 })
```

**Status:**
- C backend: cc compilation error (estrutura não pode ser comparada com `==`)
- Native backend: KNOWN_STOP — "native backend N1: `char` has no single PrimKind"

**Análise:** `tk_char` é uma estrutura `{ptr: uint8_t*, len: uint64_t}`. Não há comparação trivial nem no C nem no nativo.

**Referência de teste:** Nenhum — `char` comparação não está testada em `src/**/*_test.tkt`

---

### 2. Operador `+` com strings — REJEIÇÃO DE CHECKER (AMBAS AS ROTAS)

**Código:**
```teko
exit(if ("hello" + "world").len == 10 { 0 } else { 1 })
```

**Status:**
- C backend: checker rejects — "arithmetic needs a numeric operand"
- Native backend: checker rejects — "arithmetic needs a numeric operand"

**Análise:** Não há operador `+` para strings. Só existe função `str_concat`.

**Nota:** Isto NÃO é um buraco — é limitação de design. Strings usam `str_concat()`.

---

### 3. `str_concat` builtin no backend nativo — K-STOP

**Código:**
```teko
exit(if str_concat("hello", "world").len == 10 { 0 } else { 1 })
```

**Status:**
- C backend: OK, executa com sucesso
- Native backend: "builtin `str_concat` not yet lowered (N2)"

**Análise:** Backend nativo não lowered `str_concat`. Divergência entre rotas.

**Referência de teste:** Existe teste? Procurando...

---

### 4. Literais float no backend nativo — HONEST-STOP

**Código:**
```teko
exit(if 5.0 == 5.0 { 0 } else { 1 })
```

**Status:**
- C backend: OK
- Native backend: "isel x86-64: B1-fp — a float literal materializes into an XMM register via the SSE move family"

**Análise:** Limitação conhecida do backend nativo (degrau 18).

---

## Matriz de Cobertura (Resumo Inicial)

| Operação             | Tipo    | Posição | C | Native | Status      |
|----------------------|---------|---------|---|--------|-------------|
| `==`                 | i64     | if      | ✓ | ✓      | coberto     |
| `==`                 | str     | if      | ✓ | ✓      | coberto     |
| `==`                 | char    | if      | ✗ | ✗      | **BURACO**  |
| `==`                 | []T     | if      | ? | ?      | por medir   |
| `.len`               | []T     | if      | ✓ | ✓      | coberto     |
| `[i]`                | []T     | if      | ✓ | ✓      | coberto     |
| `str_concat`         | str+str | return  | ✓ | ✗      | divergência |

---

## Próximos Passos

1. Medir comparação de arrays (`[1,2] == [1,2]`)
2. Medir `match` com valores (não apenas int/bool)
3. Procurar por operações óbvias sem testes nos ficheiros existentes
4. Investigar cada "?" da tabela acima

