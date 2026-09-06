# Expurgo dos intrínsecos/inlines — plano fiado da onda dedicada (D161)

Status: DESIGN (architect). Read-and-design ONLY — nenhum código de produto é escrito aqui.
Base: `origin/fix/retirement` HEAD `55e4f239` (já com `cg_emit_self_addr` hoist, D166).
Lei-mãe: **D161** — "Uma função não pode existir somente na lógica do pipeline, tem que ter código
dela." Toda fn tem corpo de superfície real e é CHAMADA pela emissão genérica; carve-out só a
primitiva IRREDUTÍVEL *declarada como superfície* (syscall D134, wrap/unwrap D131, arena D148).

> **O que este doc É.** A tabela A/B/carve-out **VERIFICADA per-sítio** (contra o código, `arquivo:linha`,
> não o censo cru), o cruzamento com `plano-s16-expurgo-libc-completo.md` (NOVO vs já-planejado), e a
> sequência de crumbs `.crumbs/0193+` que fia a onda dedicada do D162. Corrige a conflação do censo
> (que rotulou C-inline como "remover-desvio") e separa o que é barato-mecânico (A) do que é §16
> real-implementação (B).
>
> **O que este doc NÃO É.** Não reescreve o §16 (o desenho por-subsistema do grupo B) nem o W4/W6
> (byte-movers de região e o reball de wrap/unwrap str↔[]byte). REFERENCIA-os e marca as exclusões.

---

## §0 — Método da verificação

Cada sítio foi lido em `src/codegen/codegen.tks` (`emit_call_inner`, 3640–3849) e no espelho
`src/lir/lower.tks` (1260–1275, `native_builtin_symbol` 2098–2112). O **discriminador objetivo**:

- **Grupo A (REMOVER-DESVIO):** o codegen já resolve para um símbolo de **superfície Teko real**
  via `builtin = cb_fn_name_str("teko::runtime", "X")` — e existe `exp fn X` com **corpo** em
  `src/runtime/*.tks`. O expurgo tira o name-detect; a resolução passa a fluir genérica. Barato.
- **Grupo B (ESCREVER-CORPO):** o codegen emite `builtin = "tk_..."` (função **C** de `teko_rt.c`)
  ou um **hand-emit inline** (`({ … })`). NÃO há superfície Teko para rotear → remover o desvio
  QUEBRA. É o **§16** (libc→Teko): escreve-se o corpo primeiro, depois rota genérica.
- **Carve-out:** primitiva irredutível **declarada como superfície** (`__`-intrínseco com assinatura,
  ou receptor de tipo `Region`/`Arena`). Permanece — D161 a permite explicitamente.

A causa-raiz do grupo A (por que o desvio existe apesar de a superfície existir): o **checker**
tipa esses nomes-nus por um **shim** — `builtin_fn(name)` em `src/checker/scope.tks:471` — que
sintetiza uma assinatura `Func` **SEM** namespace, deixando `c.call_ns == ""`. Com `call_ns` vazio,
a emissão genérica não tem símbolo → o codegen name-detecta. **O expurgo de A é, portanto, uma
mudança de RESOLUÇÃO, não só um `delete` de `if`** (ver §5 Risco-1).

---

## §1 — Tabela-mestre VERIFICADA (uma linha por sítio)

### 1.A — Grupo A: REMOVER-DESVIO (superfície Teko real confirmada)

| # | nome (call) | codegen sítio | superfície (corpo) | retorno | aloca? | onda |
|---|---|---|---|---|---|---|
| A01 | `write` | codegen:3760 | `rtio.tks:40 exp fn write` | void | não | **EX-A1** |
| A02 | `print` | 3756 | `rtio.tks:47` | void | não | EX-A1 |
| A03 | `println` | 3757 | `rtio.tks:51` | void | não | EX-A1 |
| A04 | `ewrite` | 3761 | `rtio.tks:55` | void | não | EX-A1 |
| A05 | `eprint` | 3762 | `rtio.tks:62` | void | não | EX-A1 |
| A06 | `eprintln` | 3763 | `rtio.tks:66` | void | não | EX-A1 |
| A07 | `exit` | 3759 | `rtio.tks:90` | noreturn | não | EX-A1 |
| A08 | `panic` | 3758 | `rtio.tks:76` | noreturn | (msg interna, throwaway) | **EX-A2** |
| A09 | `panic_div0` | 3804 | `teko_rt.tks:239` | noreturn | não | EX-A2 |
| A10 | `panic_oob` | 3805 | `teko_rt.tks:242` | noreturn | não | EX-A2 |
| A11 | `panic_cast` | 3806 | `teko_rt.tks:245` | noreturn | não | EX-A2 |
| A12 | `panic_overflow` | 3807 | `teko_rt.tks:248` | noreturn | não | EX-A2 |
| A13 | `str_hash` | 3810 | `teko_rt.tks:101` | u64 | não | **EX-A3** |
| A14 | `str_compare` | 3811 | `teko_rt.tks:109` | i64 | não | EX-A3 |
| A15 | `len` → `str_len` | 3812 | `teko_rt.tks:140` | u64 | não | EX-A3 |
| A16 | `ends_with` → `str_ends_with` | 3823 | `teko_rt.tks:144` | bool | não | EX-A3 |
| A17 | `contains` → `str_contains` | 3824 | `teko_rt.tks:152` | bool | não | EX-A3 |
| A18 | `is_alpha` | 3818 | `teko_rt.tks:174` | bool | não | **EX-A4** |
| A19 | `is_digit` | 3819 | `teko_rt.tks:180` | bool | não | EX-A4 |
| A20 | `is_space` | 3820 | `teko_rt.tks:186` | bool | não | EX-A4 |
| A21 | `peak_rss` | 3845 | `rtio.tks:311` | u64 | não | **EX-A5** |
| A22 | `stdin_eof` | 3765 | `rtio.tks:242` | bool | não | EX-A5 |
| A23 | `parse` → `float_parse` | 3767 | `teko_rt.tks:1` | f64 | não | EX-A5 |
| A24 | `intern_get` | 3801 | `teko_rt.tks:501` | str | região-própria (intern) | **EX-A6†** |
| A25 | `intern_put` | 3802 | `teko_rt.tks:517` | str | região-própria (intern) | EX-A6† |
| A26 | `intern_reset` | 3803 | `teko_rt.tks:540` | void | não | EX-A6† |
| A27 | `read_line` | 3764 | `rtio.tks:217` | str | SIM (caller region) | EX-A6† |
| A28 | `read_stdin` | 3766 | `rtio.tks:253` | str | SIM | EX-A6† |
| A29 | `slice`/`slice_to`/`slice_from` | 3730–3732 (`emit_str_slice`) | `teko_rt.tks:123/132/136` | str | SIM (subslice, sem cópia) | EX-A6† |

**†EX-A6 = POST-W4.** A24–A29 retornam `str` (ou usam região) → roteá-las genéricas ACIONA a
maquinaria de região-por-param do **W4** (`sweep/w4-region-param`, em voo). Fora do byte-mover de
risco → **só landam DEPOIS do W4 fechar**. A01–A23 são **abaixo-da-linha** (void/noreturn/bool/int,
D159) → NÃO conduzem região → seguras e paralelas AGORA, independentes do W4.

**Nota — `str_concat`/`one_byte`/`i64_to_str`/`u64_to_str`/`ftoa`/`f64_g17`/`fmt_*` (15)/`concat`
(3708)/interp (`emit_interp`):** superfície A, mas **ALOCANTES já na 1ª fatia do W4 (D160)** →
**EXCLUÍDOS desta onda** (não duplicar).

### 1.B — Grupo B: ESCREVER-CORPO (= §16; C-inline `tk_*` ou hand-emit, SEM superfície)

| # | nome | codegen sítio | emite hoje | §16? | onda |
|---|---|---|---|---|---|
| B01 | `os` | 3842 | `tk_rt_os` | NOVO (host info, string estática) | **EX-B1** |
| B02 | `arch` | 3843 | `tk_rt_arch` | NOVO | EX-B1 |
| B03 | `version` | 3844 | `tk_rt_version` | NOVO | EX-B1 |
| B04 | `chars` | 3814 | `tk_str_chars` | ◑ chars (§16 char decode) | **EX-B2** |
| B05 | `len_chars` | 3815 | `tk_str_len_chars` | ◑ | EX-B2 |
| B06 | `char_at` | 3816 | `tk_char_at` | ◑ | EX-B2 |
| B07 | `str_slice_chars` | 3817 | `tk_str_slice_chars` | ◑ | EX-B2 |
| B08 | `to_lower` | 3821 | `tk_to_lower` | ◑ | EX-B2 |
| B09 | `to_upper` | 3822 | `tk_to_upper` | ◑ | EX-B2 |
| B10 | `fdiv` | 3768 | `tk_fdiv` | ✔ F1 (math, -lm) | **EX-B3** |
| B11 | `floor` | 3846 | `tk_floor` | ✔ F1 (`math_floor_intrinsic`, fixture S4) | EX-B3 |
| B12 | `f64_bits` | 3769 | `tk_f64_bits` | ✔ (fundacao C5, float-bits) | EX-B3 |
| B13 | `f64_from_bits` | 3770 | `tk_f64_from_bits` | ✔ | EX-B3 |
| B14 | `atomic_cas_u32` | 3779 | `tk_atomic_cas_u32` | ◑ sync (S16-SYNC 0029) | **EX-B4** |
| B15 | `atomic_xchg_u32` | 3780 | `tk_atomic_xchg_u32` | ◑ | EX-B4 |
| B16 | `atomic_add_u32` | 3781 | `tk_atomic_add_u32` | ◑ | EX-B4 |
| B17 | `atomic_load_u32` | 3782 | `tk_atomic_load_u32` | ◑ | EX-B4 |
| B18 | `cov_*` (14: reset/mark/distinct/is_marked/branches_on/branch_reset/enter/leave/branch/branch_hit/lines_on/line_reset/line/line_hit) | 3783–3796 | `tk_cov_*` | ◑ camada-2 (`tk_cov_dump` núcleo irredutível) | **EX-B5** |
| B19 | `err_loc` | 3653 (hand-emit `tk_error_loc`) | inline | ✔ D126 error loc | **EX-B6** |
| B20 | `err_typed` | 3663 (hand-emit `tk_error_types`) | inline | ✔ D126 | EX-B6 |
| B21 | `thread_clone` | 3778 | `tk_thread_clone` | ◑ threads (S10-RT 0116) | **EX-B7** |
| B22 | `task_reset` | 3800 | `tk_task_reset` | ◑ | EX-B7 |
| B23 | `retain` | 3742 (`emit_retain`) | inline | COL-F0c (refcount) — NOVO | **EX-B8** |
| B24 | `release` | 3743 (`emit_release`) | inline | COL-F0c | EX-B8 |
| B25 | `weak_get` | 3744 (`emit_weak_get`) | inline | COL-F0c/F0d | EX-B8 |
| B26 | `deep_copy` | 3745 (`emit_deep_copy`) | inline | COL-F0d | EX-B8 |
| B27 | `var`/`set_var`/`cwd`/`chdir` | 3716–3719 (`emit_host_ffi`) | `tk_rt_*` | ✔ RT-L4-ENV (0124) | **EX-B9** |
| B28 | `args`/`run`/`run_quiet` | 3721–3723 | `tk_rt_*` | ◑ process | EX-B9 |
| B29 | `last_index_of` | 3720 | `tk_rt_last_index_of` | NOVO | EX-B9 |
| B30 | `bytes_from_ptr`/`str_from_utf8`/`as_cstr`/`str_from_c` | 3724/3725/3728/3729 | `tk_*` | NOVO (C-string interop) | EX-B9 |
| B31 | `append_fo` | 3674 (hand-emit `tk_append_bytes_fo`) | inline | **NO-PUSHES expurgo** (banido, D-no-pushes) | **EXCLUÍDO** |

**B31 (`append_fo`)** é copy-grow de array dinâmico — **proibido** e removido pelo expurgo NO-PUSHES,
NÃO por esta onda. Excluído.

### 1.C — Carve-outs (permanecem; primitiva irredutível declarada como superfície)

| nome | sítio | classe | ação |
|---|---|---|---|
| `syscall0`–`syscall6` | 3771–3777 | D134 chão syscall | KEEP (documentar) |
| `arena_push`/`arena_pop`/`arena_commit` | 3797–3799 (`cg_arena_sym`) | D148 arena | KEEP |
| `__ptr_wrap` / `__ptr_unwrap` | 3640–3641 | D131 reinterpret (surface `__`-decl) | KEEP |
| `__ref_word` | 3642 (`&(expr)`) | D131 address-of reinterpret (surface `__`-decl) | KEEP |
| `mem::place`/`read`/`write` | 3645–3649 | raw-memory primitivo | **EX-C1** (auditar: surface-decl OU fold) |
| `load_u64`/`store_u64`/`load_u8`/`store_u8` | 3736–3739 | raw load/store | EX-C1 |
| `ptr_word`/`word_ptr` | 3734–3735 | reinterpret | EX-C1 |
| `buf_ptr`/`byte_ptr`/`region_buf` | 3726/3727/3733 | ptr/arena view | EX-C1 |
| `as_ptr` | 3841 (`tk_as_ptr`) | reinterpret | EX-C1 / W6 |
| `bytes_of_str` (3813) / `str`/`str_of_bytes` (3693) | `tk_bytes_of_str` / hand-emit | **str↔[]byte = flagship wrap/unwrap D131** | **W6 reball** (EXCLUÍDO desta onda) |
| `capture_panic`/`capture_longjmp` | 3740–3741 | setjmp/longjmp | **NAT-B0 0126** (graceful-stop) — EXCLUÍDO |

**EX-C1 é hygiene, não expurgo-de-corpo:** D161 exige que o carve-out tenha "identidade/assinatura de
superfície, NÃO name-detect escondido". `syscall`/`__ptr_wrap`/`__ref_word` já cumprem (`__`-decl /
surface). Os raw-memory (`place`/`read`/`write`/`load_*`/`store_*`/`ptr_word`/`word_ptr`/`buf_ptr`/
`byte_ptr`/`region_buf`) são name-detect PURO sem decl-de-superfície → EX-C1 lhes dá a decl-intrínseca
de superfície (forma `__`-prefixada, como wrap/unwrap) **OU** os funde a `wrap`/`unwrap` onde
redutível. `str↔[]byte` (`bytes_of_str`/`str_of_bytes`/`as_ptr`) é o **flagship de D131** → **W6**
(uso-em-massa reball), fora desta onda.

---

## §2 — Cruzamento com o §16 (NOVO vs já-planejado)

O grupo B é, em grande parte, o expurgo libc que o `plano-s16-expurgo-libc-completo.md` já mapeia.
**Esta onda NÃO reescreve o corpo B** — ela é o **removedor do name-detect** que segue a landar do §16.
Ou seja: cada crumb EX-B* é um **follow-up de remoção**, BLOQUEADO no crumb §16 que escreve o corpo.

| onda EX-B | §16 / crumb dono do CORPO | estado |
|---|---|---|
| EX-B1 host-info (os/arch/version) | §16 NOVO (contrato: string estática por (os,arch)) | corpo NOVO — §16 escreve |
| EX-B2 char-ops | §16 ◑ chars/char_at/to_lower/fdiv (censo D162) | parcial — §16 escreve |
| EX-B3 float (fdiv/floor/f64_bits/f64_from_bits) | §16 **F1** (`math_floor_intrinsic` S4; float-bits fundacao C5) | ✔ planejado |
| EX-B4 atomics | §16 SYNC-const (crumb 0029) + corpo atomics | ◑ planejado |
| EX-B5 cov_* | §16 camada-2 (`tk_cov_dump` irredutível) | ◑ planejado |
| EX-B6 err_loc/err_typed | §16 **D126** (`error_make`/loc) | ✔ planejado |
| EX-B7 thread_clone/task_reset | §16 threads (S10-RT 0116) | ◑ planejado |
| EX-B8 retain/release/weak_get/deep_copy | COL-F0c/F0d (refcount) — **NÃO é §16-libc**, é runtime-novo | NOVO |
| EX-B9 host env/process FFI | §16 RT-L4-ENV (0124), process NOVO | ◑ / NOVO |

**Regra dura (não-duplicar):** onde o §16 já tem crumb do corpo, o EX-B* **não** escreve corpo — ele
só remove o name-detect DEPOIS que o corpo landou, e prova byte-equivalência. Onde o corpo é NOVO
(EX-B1, parte do B8/B9/B29/B30), o EX-B* carrega o contrato NOVO e ENTRA no §16 (reportar ao dono para
o §16 absorver, não criar issue). O grupo A é 100% **fora** do §16 (superfície já existe).

---

## §3 — Exclusões explícitas (o que esta onda NÃO pega)

1. **Alocantes grupo-A do W4 1ª fatia:** `str_concat`, `one_byte`, `i64_to_str`, `u64_to_str`,
   `ftoa`/`f64_g17`, `fmt_*` (15), `concat` (3708), interp — já no W4 (D160).
2. **str↔[]byte** (`bytes_of_str`, `str_of_bytes`, `as_ptr`) — **W6** reball wrap/unwrap (D131).
3. **`append_fo`** — expurgo NO-PUSHES (array dinâmico banido).
4. **`capture_panic`/`capture_longjmp`** — NAT-B0 0126 (graceful-stop, elimina setjmp/longjmp).
5. **A24–A29 (intern/read_line/read_stdin/str_slice)** — grupo-A alocante → **POST-W4** (EX-A6).

---

## §4 — Sequência de crumbs (ordem segura)

```
FASE 0 — FUNDAÇÃO (de-risca o Risco-1 em isolamento)                 [fixpoint] byte-idêntico
  0193 EX-A0  resolver nome-nu do prelúdio → call_ns="teko::runtime"  (name-detect AINDA vence)
FASE 1 — GRUPO A não-alocante (SEGURO, paralelo, independe do W4)     [fixpoint por família] deps: EX-A0
  0194 EX-A1  I/O void + exit          (write/print/println/ewrite/eprint/eprintln/exit)
  0195 EX-A2  panic family             (panic/panic_div0/panic_oob/panic_cast/panic_overflow)
  0196 EX-A3  str-query não-alocante   (str_hash/str_compare/str_len/ends_with/contains)
  0197 EX-A4  char predicados          (is_alpha/is_digit/is_space)
  0198 EX-A5  misc não-alocante        (peak_rss/stdin_eof/parse→float_parse)
FASE 2 — GRUPO A alocante (POST-W4, região-aware)                    [fixpoint] deps: W4, EX-A0
  0199 EX-A6  intern + str-returners   (intern_*/read_line/read_stdin/str_slice*)
FASE 3 — GRUPO B removal-follow-ups (BLOQUEADO no §16 dono do corpo) [fixpoint] deps: §16-crumb, EX-A0
  0200 EX-B1  host-info                 deps: §16-host-info(NOVO)
  0201 EX-B2  char-ops                  deps: §16-chars
  0202 EX-B3  float                     deps: §16-F1
  0203 EX-B4  atomics                   deps: §16-SYNC(0029)
  0204 EX-B5  coverage                  deps: §16-cov camada-2
  0205 EX-B6  error loc/typed           deps: §16-D126
  0206 EX-B7  thread/task               deps: S10-RT(0116)
  0207 EX-B8  refcount                  deps: COL-F0c(0023)/F0d(0024)
  0208 EX-B9  host env/process FFI      deps: RT-L4-ENV(0124)
FASE 4 — carve-out hygiene + terminal
  0209 EX-C1  carve-out surface-decl    (place/read/write/load_*/store_*/ptr_word/word_ptr/…)
  0210 EX-T1  terminal: remover shim builtin_fn + reseed + gate ASan+UBSan
```

**Ordenação:** `0193 EX-A0` isola o Risco-1 (a religação de resolução) SEM remover name-detect —
byte-idêntico, prova que `call_ns` passa a ser setado sem mudar emissão. Fase 1 é a onda REAL desta
entrega (barata, sem dep externa além de EX-A0). Fase 2 gated no W4. Fase 3 gated no §16 (design-ahead:
contratos prontos, removal-em-minutos quando o corpo landa). `0210 EX-T1` só fecha quando A e B
removeram seus name-detects → aí o shim `builtin_fn` (scope.tks:471) some inteiro e o
`panic("codegen: no resolved namespace")` (3849) vira inalcançável (ou é o único guard restante). É o
reseed terminal + a prova ASan.

---

## §5 — Riscos + tensões de lei

**Risco-1 (CENTRAL) — resolução, não `delete`.** Remover o name-detect de A sem religar a resolução
QUEBRA: `builtin_fn` (scope.tks:471) tipa o nome-nu com `call_ns==""`, e a emissão genérica precisa de
`call_ns=="teko::runtime"` para emitir o símbolo. **Contrato do expurgo A:** o front-end (resolve/nidx)
passa a resolver esses nomes-nus à superfície `teko::runtime::X` do prelúdio injetado (abrindo o
namespace do prelúdio no escopo nu, como um `use` implícito), removendo a entrada correspondente de
`builtin_fn`. Só então o `if last=="X"` sai do codegen e do `native_builtin_symbol` (lower). **Prova de
não-regressão = fixpoint byte-idêntico**: `cb_fn_name_str("teko::runtime","X")` (desvio) deve emitir o
MESMO símbolo que `cb_fn_name(buf,"teko::runtime","X",overload_suffix)` (genérico). Divergência de
`overload_suffix` → muda bytes → **reseed do degrau** (esperado; NÃO é bug). Cada família valida esse
casamento isoladamente antes de somar. **Não é fork** — é lei D161 (fluxo genérico); o mecanismo
(open-implícito do prelúdio) é escolha de implementação, não decisão de design aberta.

**Risco-2 — abaixo-da-linha preserva-se.** A01–A23 são não-alocantes (D159 below-line) → ao fluírem
genéricos NÃO devem receber param de região (senão o W4 os re-tangla). O oráculo `residence.tks`
(`cg_fn_is_below_line`) já os classifica below-line por retornarem não-heap. Verificar por família que
o roteamento genérico NÃO injeta região nesses (postura conservadora do D159 trava-1).

**Risco-3 — ratchet D68.** Rotear genérico pode inflar levemente a auto-compilação (mais nós de call
resolvidos). Piso "não-crescer" (D68 escopo-expurgo). Medir `teko: memory: peak` do build seco por
degrau; crescer = corrigir antes de landar.

**Risco-4 — gate ASan+UBSan (D166).** O fixpoint no sandbox não pega UB que só crasha em certos
toolchains (foi o fogo do 0184). CADA degrau que toca codegen/resolução roda o build ASan+UBSan
(`-fsanitize=address,undefined -fno-omit-frame-pointer -g`) do gen0 compilando o tip, além do fixpoint.

**Risco-5 — Fase 3 bloqueada.** EX-B* NÃO pode landar antes do §16 escrever o corpo. Design-ahead
entrega os contratos; a remoção é minutos quando a dep §16 fecha. Reportado: EX-B1 (host-info),
partes de B8/B9 são corpo NOVO que o §16 ainda não tem crumb — REPORTAR ao dono para o §16 absorver
(não criar issue novo — lei "achados adjacentes são REPORTADOS").

**Tensão de lei — nenhuma HALT-genuína.** Verifiquei os 3 candidatos a fork:
- *place/read/write/load/store = carve ou expurgo?* Resolvido por D131/D134/D148 (raw-memory é chão
  irredutível) + D161 (carve exige surface-decl) → EX-C1 dá a decl. Não é fork.
- *str↔[]byte = expurgo aqui ou W6?* Resolvido por D131 ("uso em massa = reball W6"). Fora desta onda.
- *A24–A29 alocante = agora ou pós-W4?* Resolvido por D155/D159/D160 (não tanglar o byte-mover) →
  pós-W4. Não é fork.

Nenhuma tensão genuinamente aberta → **sem HALT**. Se, ao implementar EX-A1, a resolução do prelúdio
revelar que o open-implícito colide com nome de usuário (provenance D133), isso É deliberado (D133
barra redefinição de reservados) — aplicar, não parar.

---

## §6 — Espelho native (D148) — par C+native por crumb

Cada expurgo reflete nas DUAS rotas. Pares por crumb:

| crumb | rota-C (codegen.tks) | rota-native (lower.tks) |
|---|---|---|
| EX-A1..A6 | remover `if last==` em `emit_call_inner` (3756–3767, 3801–3812…) | remover de `native_builtin_symbol` (2098–2112: `builtin_io_symbol` 1995, `builtin_str_query_symbol` 2045, `builtin_peak_rss_symbol` 2093) + `is_str_arg_builtin` (1833) para A alocante |
| EX-B1 | remover 3842–3844 | `builtin_host_info_symbol` (2086) |
| EX-B2 | remover 3814–3822 | `builtin_str_query`/`str_slice` (2045/2051) + char paths |
| EX-B3 | remover 3768–3770, 3846 | `is_f64_bitcast_call` (1265), float paths |
| EX-B4 | remover 3779–3782 | atomic paths |
| EX-B5 | remover 3783–3796 | `builtin_cov_symbol` (2007) |
| EX-B6 | remover 3653/3663 hand-emit | `is_err_loc_call`/`is_err_typed_call` (1262/1263) |
| EX-B7 | remover 3778/3800 | thread/task paths |
| EX-B8 | remover 3742–3745 hand-emit | refcount paths |
| EX-B9 | remover 3716–3729 | `builtin_env_symbol` (2122), `builtin_hostffi_variant_symbol` (2115), `is_last_index_of_call`/`is_str_from_utf8_call` (1261/1262) |
| EX-C1 | dar surface-decl a place/read/write/load/store/ptr_word/word_ptr/buf_ptr/byte_ptr/region_buf | `is_load_u64_call`/`is_store_u64_call`/`is_buf_ptr_call` (1267–1271), `is_mem_value_call` (2133) |

Regra: **o par nunca diverge** — remover o name-detect só da rota-C deixaria o native emitindo símbolo
inexistente. Cada crumb toca AS DUAS e valida com o fixpoint (que compila as duas rotas; a native
só é EXECUTADA pós-marco, mas COMPILA no self-build).

---

## §7 — Gate + reseed por degrau

- Gate por família: `[fixpoint]` = build gen2 (subshell `ulimit -v 4718592`) + regressão de escopo +
  `gen2==gen3` byte-idêntico + **build ASan+UBSan limpo (D166)** + ratchet D68 (pico não-cresce).
- Reseed: **por degrau** (D164/RESEED-INCONDICIONAL) — cada família que muda o `teko.c` emitido reseeda
  `bootstrap/teko.c` ao fim, prova a ladder do seed NOVO, deixa gen2/gen3 no scratchpad.
- `0209 EX-T1` = reseed terminal + remoção do shim `builtin_fn` + verificação de que
  `panic("codegen: no resolved namespace")` (3849) só cobre erro-interno legítimo.
- Fixtures: **nenhuma afirmativa nova** — o self-build/fixpoint exercita todas essas fns (o compilador
  as chama ao compilar). Só o §16 (grupo B) traz oráculos `.tkr` de path-não-exercitado, e esses são
  do §16, não desta onda. Grupo A e C: `none — the fixpoint self-build exercises this`.
</content>
</invoke>
