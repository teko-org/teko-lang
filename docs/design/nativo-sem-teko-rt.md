# A rota nativa sem `teko_rt.{c,h}` — mapa e plano

Carga `cargo/0.3.1.0-nativo-sem-teko-rt-mapa`, a partir de `origin/fix/union`.
Papel: **arquiteto** — este documento só LÊ código e PROPÕE. Nada aqui é implementado
(ver §6, a relação com o `tdb` e o gate self-host).

Diagnóstico do dono (2026-08-02):

> *"Mesmo a rota nativa ainda tem utilização (FFI) com `teko_rt.c` e `teko_rt.h`,
> sendo que não deveria, por ser nativo."*

É verdade, e é **precisamente localizável**. Este mapa nomeia cada símbolo `tk_*` que
a rota nativa referencia, com `arquivo:linha`, separa o que é **fronteira de syscall**
(irredutível sem um runtime nativo mínimo) do que é **lógica pura que já tem gêmeo em
Teko hoje**, e propõe UM caminho — não contra-argumentos. Onde levanto um alarme, eu o
provo com `arquivo:linha` em vez de usá-lo para desencorajar (lei do dono).

`src/runtime/teko_rt.{c,h}` É semente escrita à mão e é o objeto de estudo — citada, não
tratada como saída. `bootstrap/teko.c` é SAÍDA, nunca entrada, e não aparece aqui.

---

## 0. O achado que reorganiza o problema

Duas descobertas mudam a forma da pergunta antes de qualquer contagem.

**(0.a) A superfície inteira mora em `src/lir/lower.tks`.** As menções a `tk_*` em
`src/backend/isel_x86_64.tks` (linhas 1382–1685) e `src/backend/objfile_coff.tks`
(linhas 191, 521) são **todas doc-comments** — descrevem a ABI de `tk_str` (par
`ptr,len`) e a relocação de um símbolo indefinido como `tk_exit`, mas **não emitem
nenhum símbolo**. `src/backend/isel_arm64.tks`, `src/backend/objfile_elf.tks` e
`src/backend/objfile_macho.tks` têm **zero** ocorrências de `tk_`. `src/lir/lir.tks`
(linhas 88, 349, 350, 412) também só cita em doc-comment. Prova:

```
grep -oE '"tk_[a-z_0-9]*"' src/lir/lower.tks | sort -u | wc -l   # 109
grep -c 'tk_' src/backend/isel_arm64.tks src/backend/objfile_elf.tks \
              src/backend/objfile_macho.tks                          # 0, 0, 0
```

Consequência de arquitetura: **isel e objfile são transporte agnóstico de símbolo**. O
isel só decide como o par `(ptr,len)` de um `tk_str` viaja pelos registradores da ABI
(`isel_x86_64.tks::pin_fat_pairs_by_ref_x86`, ~1575); o objfile só emite a relocação de
qualquer nome indefinido (`objfile_coff.tks:191`). Nenhum dos dois conhece a *lista* de
símbolos `tk_*`. **A lista é autorada em `lower.tks` e em lugar nenhum mais.** Reduzir a
FFI nativa é, portanto, uma mudança confinada a `lower.tks` mais os corpos Teko em
`src/runtime/teko_rt.tks` — isel/objfile não precisam ser tocados.

**(0.b) Os gêmeos Teko das famílias de lógica pura JÁ EXISTEM.** `src/runtime/teko_rt.tks`
já contém corpos Teko reais para `str_concat` (linha 115), `u64_to_str` (133),
`i64_to_str` (149), `str_of_bytes` (161), `one_byte` (169), `concat` (179), `str_eq`
(496), `str_hash` (511), `str_compare` (527), `str_slice`/`_to`/`_from` (544–558),
`str_len` (563), `str_ends_with` (569), `str_contains` (580), a família `fmt_*`
(277–455) e as guardas de pânico (`panic` 695, `panic_div0` 705, `panic_oob` 712,
`panic_cast` 720, `panic_overflow` 728, `panic_oob_at` 742) mais `div` 754 / `mod` 759.
O próprio arquivo declara a rota que falta, no seu doc-comment de cabeçalho (~linhas
44–56):

> *"Rerouting the bare-name dispatch to call INTO this file — so a plain `print("x")`
> anywhere runs this code instead of the parallel C path — is the next step toward
> retiring `teko_rt.c`; it touches the native backend's core call lowering and needs the
> full self-host gate to validate, so it is not this change."*

Ou seja: o trabalho dominante **não é reescrever lógica** — é **religar o despacho**. Os
corpos existem e estão bypassados. Isto reprecifica a §3 inteira.

---

## 1. A superfície completa (todo `tk_*` que a rota nativa referencia)

`lower.tks` referencia **109 símbolos `tk_*` distintos** (todos como literais de string;
lista mecânica reproduzível com o `grep` de §0.a). Eles chegam ao backend por **duas
portas** — e essa distinção é a espinha do plano.

### 1.1 Porta A — resolutores de builtin de nome-nu (`native_builtin_symbol`)

Um builtin chamado pelo nome-nu (`println(...)`, `x.ends_with(y)`) é resolvido por
`native_builtin_symbol` (`lower.tks:4235`), que consulta treze predicados por família,
mais o `call_symbol` (`lower.tks:4304`) que o chama. Cada família:

| Família | Resolutor (`lower.tks`) | Símbolos | Piso |
|---|---|---|---|
| E/S + processo | `builtin_io_symbol:3930` | `tk_exit`, `tk_panic_str`, `tk_print`, `tk_println`, `tk_eprint`, `tk_eprintln`, `tk_write`, `tk_ewrite` (8) | **syscall** (write/abort/exit) |
| coverage | `builtin_cov_symbol:3953` | 14 × `tk_cov_*` | harness (§6) |
| checkpoint de arena | `builtin_arena_symbol:3980` | `tk_arena_push`, `tk_arena_pop`, `tk_arena_commit` (3) | **memória** (§4) |
| predicado de str | `builtin_str_query_symbol:4029` | `tk_str_ends_with`, `tk_str_contains` (2) | **lógica pura** |
| fatia de str | `builtin_str_slice_symbol:4051` | `tk_str_slice_len`, `_to_len`, `_from_len`, `_chars_len` (4) | **lógica pura** (+arena p/ cópia) |
| int→str | `builtin_int_to_str_symbol:4074` | `tk_i64_to_str_len`, `tk_u64_to_str_len` (2) | **lógica pura** (+arena) |
| float→texto | `builtin_float_to_str_symbol:4103` | `tk_ftoa_len`, `tk_f64_g17_len` (2) | **fronteira**: lógica Teko + `snprintf` |
| `str`/`str_of_bytes` | `builtin_str_of_bytes_symbol:4122` | `tk_str_of_bytes_len` (1) | **lógica pura** (+arena) |
| `bytes_of_str` | `builtin_bytes_of_str_symbol:4143` | `tk_bytes_of_str_len` (1) | **lógica pura** (view, zero-cópia) |
| `one_byte` | `builtin_one_byte_symbol:4166` | `tk_one_byte_len` (1) | **lógica pura** (+arena) |
| host-info | `builtin_host_info_symbol:4183` | `tk_rt_os_len`, `tk_rt_arch_len`, `tk_rt_version_len` (3) | **constante de build** |
| `peak_rss` | `builtin_peak_rss_symbol:4199` | `tk_peak_rss` (1) | **syscall** (getrusage) |
| `env::args` | `builtin_env_symbol:4268` | `tk_rt_args` (1) | **syscall** (argv do host) |

Semente-assert: `assert_seed_symbol:4006` mapeia `teko::assert::<nome>` para
`teko__assert__*` (não são `tk_*` — vivem em `src/assert/assert.{c,h}`, o segundo seed C
mantido). A aridade ABI dessas entradas é derivada de `checker::assert_fat_arg_arity`
(`assert_seed_fat_arity:3550`), não relistada.

### 1.2 Porta B — `extern fn ... = "tk_..."` declarados na stdlib

O checker e o lowering carregam **uma exceção fechada** para um `extern fn` que toma
`str` ligado a `teko_rt` (documentada em `is_str_arg_builtin:3458` e no cabeçalho de
`teko_rt.tks:33–41`). A stdlib usa essa porta para declarar o **verdadeiro contorno de
host**. `find_extern_symbol` (consultado por `call_symbol:4317`) passa o símbolo
verbatim. Inventário (todos syscall/libc no fundo):

| Módulo | Símbolos `tk_*` | Piso |
|---|---|---|
| `src/io/io.tks` | `tk_print`, `tk_println`, `tk_write`, `tk_ewrite`, `tk_eprint`, `tk_eprintln`, `tk_flush_out`, `tk_rt_read_file`, `tk_rt_read_line`, `tk_rt_read_stdin`, `tk_rt_stdin_eof`, `tk_rt_write_file`, `tk_rt_write_file_bytes`, `tk_rt_append_file` | write/read/open |
| `src/env/env.tks` | `tk_rt_args`, `tk_rt_getenv`, `tk_rt_setenv`, `tk_rt_getcwd`, `tk_rt_chdir`, `tk_rt_version` | getenv/chdir |
| `src/fs/fs.tks` | `tk_rt_mkdir`, `tk_rt_remove_file`, `tk_rt_list_dir` | mkdir/unlink/open |
| `src/process/process.tks` | `tk_rt_run`, `tk_rt_run_quiet`, `tk_rt_spawn_redirected`, `tk_rt_wait_one`, `tk_rt_pipe`, `tk_rt_pipe_read_fd`, `tk_rt_pipe_write_fd`, `tk_rt_close_fd`, `tk_rt_fd_fill`, `tk_rt_fd_take_byte`, `tk_rt_fd_wait_readable` | fork/exec/pipe/wait |
| `src/time/time.tks` | `tk_rt_monotonic_ns`, `tk_rt_wall_ns_of_day`, `tk_rt_wall_days`, `tk_rt_wall_offset_minutes` | clock_gettime |
| `src/crypto/rand.tks` | `tk_rt_secure_bytes` | getrandom |
| `src/names/names.tks` | `tk_names_*` (9: capacity/close/status/slot_of/generation_of/live_count/cell_open/cell_read/cell_status) | tabela de nomes (host) |
| `src/coverage/coverage.tks` | `tk_cov_merge` | harness (§6) |
| `src/test/test.tks` | `tk_test_scope`, `tk_test_capture_probe`, `tk_test_capture_last_code` | harness (§6) |
| `src/assert/assert.tks` | `tk_assert_scenario_set`, `tk_assert_scenario_ok`, `tk_assert_scenario_prefix` | harness (§6) |

### 1.3 Emissões diretas em `lower.tks` (nem-porta-A-nem-B)

Alguns símbolos são emitidos por `call_inst`/`void_call_inst` com literal fixo, a partir
de baixadas de operador ou de codegen de agregados — não passam por resolutor de nome:

- **Igualdade/comparação de str e slice** (baixada de `==`/`<`): `tk_str_eq`
  (`lower.tks:1921`), `tk_str_cmp`, `tk_slice_eq_bytes`, `tk_slice_str_eq`,
  `tk_slice_f32_eq`, `tk_slice_f64_eq`. **Lógica pura** (memcmp).
- **Crescimento/boxing de slice** (baixada de `list::push`, `[]`): `tk_slice_push`,
  `tk_slice_push_fo`, `tk_slice_elem_box`, `tk_append_bytes_fo`. **Memória** (§4).
- **Cópia de agregado** (`tk_mem_copy`, emitido em `lower.tks:9919`, `11357`, `12149`).
  **Memória** (§4) — é `memcpy` (`teko_rt.c:3698`).
- **Alocação boxed** (`tk_region_alloc` `lower.tks:3437`, `tk_region_root`).
  **Memória** (§4).
- **Guardas F3** (`tk_panic_div0`, `tk_panic_oob_at`, `tk_panic_cast`). Mensagem é
  **lógica pura**; o abort é **syscall** (§2).
- **Interpolação `$"…"`** — os especificadores de formato `tk_fmt_*_len` (13:
  `b/d/e/f/g/n_f/n_i/p/x_lower/x_upper` mais `dyn_f64/dyn_i64/dyn_u64`), mais
  `tk_str_concat_len`, `tk_i64_to_str_len`, `tk_u64_to_str_len`, `tk_ftoa_len`.
  **Lógica pura** (exceto o render float, fronteira).
- **Interning** (`tk_intern_get`, `tk_intern_put`) e helpers de extern (`tk_cstr_dup`,
  `tk_as_ptr`, `tk_set_args`). Contorno de host.
- **UTF-8 / char** (`tk_str_len_chars`, `tk_str_chars`, `tk_char_at`,
  `tk_rt_str_from_utf8_ok`) e float (`tk_float_parse`, `tk_rt_float_parse_bits`).
  Ver §2.

---

## 2. O que é intrinsecamente C vs. o que é lógica pura

Três camadas, cada afirmação com `arquivo:linha`. **A fronteira é a syscall/libc** — é o
que não sai sem um runtime nativo mínimo. Tudo acima dela é candidato a Teko.

### Camada 1 — Piso de syscall/libc (irredutível sem runtime nativo mínimo)

Provado em `teko_rt.c`:

- **Escrita de host**: `tk_print`/`tk_println`/`tk_write`/`tk_ewrite`/`tk_eprint`/
  `tk_eprintln` → `fwrite`/`fputc` sobre `stdout`/`stderr` (`teko_rt.c:2311–2345`).
- **Terminação**: `tk_panic`/`tk_panic_str` → `fputs`+`fwrite`+`abort` (`2363–2395`);
  `tk_exit` → `exit` (`2406`).
- **Alocação**: `tk_region_alloc` → `tk_chunk_try` → `malloc` (`1507`, `1163`, `1535`);
  `tk_slice_push`/`tk_slice_elem_box` → `tk_alloc`/`memcpy` (`3678`, `3688`).
- **Render/parse float**: `tk_ftoa` → `snprintf("%.17g")` (`416`); `tk_float_parse` →
  `strtod` (`2349`).
- **Host surface**: getenv/setenv/chdir/mkdir/unlink/read/open/fork/exec/pipe/wait
  (contagem em `teko_rt.c`: `malloc` 52×, `free` 84×, `write` 23×, `read` 30×, `open`
  16×, `fork` 10×, `exit` 26×, `abort` 17×, `snprintf` 27×, `getenv` 8×, `clock_gettime`
  3×, `getrusage` 2×) mais `tk_peak_rss` → `getrusage` (`3235`), `tk_rt_args` (`3162`).

`teko_rt.tks` já **admite honestamente** que esta camada não tem espelho Teko fiel:
linhas 631–669 dizem, sobre `tk_alloc`/`tk_region_*`/`tk_regions_free_all`, que *"Teko has
NO raw-pointer surface … so there is NO faithful functional mirror"* — e por isso os
mantêm como `extern`/C. `ftoa` (`teko_rt.tks:189`) e `float_parse` (`107`) delegam
explicitamente ao *"DEFERRED host float renderer/strtod (FFI bottom — C1)"*.

### Camada 2 — Primitivas de memória crua (bloqueadas por 2 lacunas de linguagem)

`tk_region_*`, `tk_arena_*`, `tk_slice_*`, `tk_mem_copy`, `tk_slice_elem_box` são
**aritmética de ponteiro pura** — bump-allocator sobre um bloco. Poderiam ser Teko,
**exceto** que Teko ainda não tem como manipular um endereço cru. Isto NÃO é um alarme
para desencorajar; é um bloqueio **provado e já em resolução paralela**
(`docs/design/arena-em-teko.md`):

- O fundo está **provado** — `examples/probes/arena_bottom` alcança `aligned_alloc`/
  `free`/`memset`/`memcmp` via `extern fn` no backend nativo, exit 42
  (`arena-em-teko.md:19–37`).
- Faltam **duas lacunas**: (1) não há `teko::mem::load_u64(addr)`/`store_u64(addr,v)`
  (`arena-em-teko.md:62`); (2) não existe cast `u64 -> ptr<byte>` (`:67`). O carrier de
  endereço é `u64`, não `uptr` (`:39–41`).

Enquanto essas lacunas não fecham, a Camada 2 **não pode** virar Teko. Isto é o que
sustenta a §4 (a ordem): memória vem primeiro, e vem por um projeto irmão.

### Camada 3 — Lógica pura que JÁ TEM gêmeo Teko (candidata imediata)

Comparação/hash/slice/format de bytes — nenhuma syscall, só `memcmp`/`memcpy` e loops.
`teko_rt.c` as implementa com memcmp (ex.: `tk_str_eq:704`, `tk_str_ends_with:926`), e
**`teko_rt.tks` já tem o corpo Teko de cada uma** (§0.b). Candidatas, com o gêmeo Teko:

| `tk_*` (C) | Gêmeo Teko (`teko_rt.tks`) | Aterrissa em |
|---|---|---|
| `tk_str_eq`, `tk_str_ends_with`, `tk_str_contains`, `tk_str_cmp` | `str_eq:496`, `str_ends_with:569`, `str_contains:580`, `str_compare:527` | só bytes (nada) |
| `tk_str_hash` | `str_hash:511` | só bytes |
| `tk_str_concat_len`, `tk_i64_to_str_len`, `tk_u64_to_str_len` | `str_concat:115`, `i64_to_str:149`, `u64_to_str:133` | arena (Camada 2) |
| `tk_str_of_bytes_len`, `tk_bytes_of_str_len`, `tk_one_byte_len` | `str_of_bytes:161`, (view), `one_byte:169` | arena |
| `tk_str_slice*_len` | `str_slice*:544–558` | arena (cópia) |
| `tk_fmt_*_len` (int/dispatch) | `fmt_d:277`, `fmt_x_*:290/298`, `fmt_b:306`, `fmt_n_i:316`, `fmt_dyn_*:395–435` | arena |
| `tk_panic_div0/_cast` (mensagem) | `panic_div0:705`, `panic_cast:720` | Camada 1 (abort) |
| `tk_ftoa_len`, `tk_f64_g17_len` | `ftoa:189`, `f64_g17:598` | **fronteira**: Teko + `snprintf` |

**A fronteira dentro da Camada 3**: a lógica de formatação inteira e de comparação é
100% Teko-able hoje; o **renderizador float** (`%.17g`) e o **parser float** (`strtod`)
têm a moldura em Teko mas o núcleo numérico é um bottom de libc genuíno
(`teko_rt.tks:190`, `599`, `108`). Eles só saem da FFI quando um render/parse de float
em Teko existir (fora de escopo aqui; não bloqueia o resto).

---

## 3. O custo e o caminho — duas rotas, medidas

### Rota (a) — Religar as famílias de lógica pura ao Teko; deixar só a casca de syscall

A leitura ingênua diz "reimplementar as famílias em Teko". **A medição diz outra coisa**:
os corpos Teko **já existem** (§0.b). A rota (a) é, na prática, uma **mudança de fiação
em `native_builtin_symbol`/`call_symbol`** — fazer o nome-nu de uma família da Camada 3
resolver para o mangle de `teko::runtime::<gêmeo>` (`mangle_fn_symbol("teko::runtime",
name)` → `teko__runtime__<name>`, `lower.tks:885`) em vez do literal `tk_*`. O corpo Teko
então baixa pelo caminho normal e o símbolo C some do link.

- **Custo por símbolo**: ~1 linha de resolutor + fixture de regressão. Nenhuma lógica
  nova; uma implementação canônica única (o corpo Teko), não duas.
- **Redução de FFI**: **das ~40 funções C de lógica pura da Camada 3, todas as que
  aterrissam só em bytes ou em arena saem do link.** O que resta é o piso: arena (Camada
  2, até as lacunas fecharem), E/S, terminação, render/parse float, e o host surface
  (Porta B). A superfície de *funções C distintas* colapsa de 109 para o piso (~35–45,
  quase todo o host surface da Porta B).
- **Alarme, provado (não para desencorajar)**: religar a Camada 3 **não reduz o piso de
  syscall** — `str_concat` chama `teko::list::push` → `tk_slice_push` → `tk_region_alloc`
  → `malloc` (`teko_rt.c:3678→1507→1535`). Prova: `str_concat` em `teko_rt.tks:116–121`
  usa `teko::list::push`. Portanto a rota (a) **move a fronteira FFI para baixo** (para
  arena+io+float), não a elimina. Isso é bom e é o objetivo: menos símbolos C, fronteira
  mais funda e mais nítida.
- **Risco**: baixo *por símbolo*, mas cada religação precisa provar que o transitivo do
  gêmeo Teko cai só em {arena, io, float-render} antes de virar a chave — é um gate por
  crumb (§5). Igualdade de str (`tk_str_eq`, `str_hash`, `ends_with`, `contains`) é a
  mais segura: aterrissa **em nada além de bytes**, zero dependência de arena.

### Rota (b) — Backend emite a primitiva inline em vez de chamar

O isel/lower emitiria os bytes da primitiva (ex.: `str_eq` como um loop de `memcmp` em
LIR) em vez de um `LCall`.

- **Prós**: remove o símbolo por completo, sem hop de chamada.
- **Contras, medidos**: duplica a lógica **no backend e por-alvo** (x86 + arm64 — dois
  `isel`), enquanto a rota (a) baixa o corpo Teko UMA vez, alvo-neutro (a mesma razão de
  §0.a: a lista de símbolos e a lógica ficam em um lugar). `str_eq`/`str_slice` precisam
  de **loop**, o que exige emissão de laço em `lower`, mais complexo que uma chamada. E
  perde a fonte canônica única (o corpo em `teko_rt.tks`).
- **Precedente onde (b) já é certo**: `f64_bits`/`f64_from_bits` já são
  reinterpretação-de-bits de **zero instruções**, interceptados em `lower_call` sem
  virar chamada (`lower.tks:4224`). `tk_mem_copy` é o próximo candidato natural (um
  `memcpy` pode virar `rep movsb`/loop inline) — mas isso é **otimização**, não
  prioridade de eliminação de FFI.

### Veredito

**Rota (a) para toda a Camada 3** — reduz o máximo de FFI pelo mínimo de código novo e de
risco, porque o corpo já existe e é alvo-neutro. **Rota (b) reservada** para primitivas
triviais já-inline (`f64_bits`, precedente `:4224`) e, mais tarde, `tk_mem_copy` como
afinação. A Camada 1 fica; a Camada 2 espera a §4.

---

## 4. A ordem — o que sai JÁ e o que depende do modelo de arena

O dono colocou memória/threading antes de outras coisas. A ordem cai naturalmente das
três camadas.

**Degrau 0 (sem tocar memória) — as puras-de-bytes.** `tk_str_eq`, `tk_str_cmp`,
`tk_str_hash`, `tk_str_ends_with`, `tk_str_contains`, `tk_slice_eq_bytes`,
`tk_slice_str_eq`, e as guardas de mensagem `tk_panic_div0`/`tk_panic_cast`. Nenhuma
aterrissa em arena — o gêmeo Teko (`str_eq:496` etc.) só lê bytes. **Podem ser religadas
sem esperar a arena.** É o corte mais seguro e o primeiro a fazer quando a implementação
começar.

**Degrau 1 (depende do modelo de arena/sub-regiões, em paralelo) — as
alocantes.** Tudo que constrói um `str`/`[]T` novo: `tk_str_concat_len`,
`tk_*_to_str_len`, `tk_str_of_bytes_len`, `tk_one_byte_len`, `tk_str_slice*_len`,
`tk_fmt_*_len`, e as primitivas `tk_slice_push*`, `tk_slice_elem_box`, `tk_mem_copy`,
`tk_region_*`, `tk_arena_*`. Estas **dependem** das duas lacunas de linguagem de
`arena-em-teko.md` (`load_u64`/`store_u64`, cast `u64->ptr`, §2 Camada 2) — bloqueio
provado, não alarme. Ficam para depois que a carga irmã de arena fechar as lacunas.

**Degrau 2 (fronteira numérica) — render/parse float.** `tk_ftoa_len`,
`tk_f64_g17_len`, `tk_float_parse`. Moldura Teko pronta; o núcleo `%.17g`/`strtod` só sai
quando houver render/parse float em Teko. Independente dos degraus 0 e 1.

**Nunca-sai-sem-runtime-mínimo — Camada 1 + Porta B.** E/S, terminação, e todo o host
surface (io/env/fs/process/time/crypto/names). Estes SÃO a "casca de syscall" que a rota
(a) deixa para trás por design. A meta honesta não é "zero `tk_*`"; é **um piso mínimo,
nomeado, de syscall** e nada de lógica pura acima dele.

---

## 5. Plano executável (sequência de crumbs, para quando a implementação abrir)

Cada crumb é independentemente gate-able. **Nenhum é implementado neste documento** (§6).

1. **C0 — instrumentar a superfície.** Um teste que enumere os `tk_*` que a rota nativa
   ainda emite (a lista de §1 vira asserção), para medir a redução a cada degrau. Gate:
   compila e conta 109.
2. **C1 — religar as puras-de-bytes (Degrau 0).** Em `native_builtin_symbol`/`call_symbol`
   e nos sítios de `==`/`<` (`lower.tks:1921`), resolver `str_eq`/`str_cmp`/`str_hash`/
   `ends_with`/`contains` para `teko__runtime__*`. Gate: cada símbolo some de `nm -u` do
   corpus nativo; fixtures §5.1.
3. **C2 — religar guardas de mensagem.** `panic_div0`/`panic_cast` → `teko::runtime::*`
   (o abort continua Camada 1). Gate: pânico ainda imprime o marcador e sai não-zero.
4. **C3 (bloqueado por arena) — religar as alocantes (Degrau 1).** Só depois de
   `load_u64`/`store_u64` + cast `u64->ptr` existirem. Honest-stop até lá.
5. **C4 (bloqueado por float-em-Teko) — render/parse float (Degrau 2).**
6. **Ponto de ritual (gate completo obrigatório):** ao fim de C1, de C2, e de C3 —
   self-host + gate nativo das 6 pernas + `tdb` (§6). A lógica de `lower.tks` é
   compartilhada pelos 6 alvos (`mapa-native-6-pernas-0.3.1.0.md`), então uma religação
   errada quebra os 6 de uma vez — o ritual não é opcional nesses pontos.

### 5.1 Fixtures de regressão a adicionar (entrada → exit, nativo)

Objetivo: provar que o gêmeo Teko é semanticamente idêntico ao `tk_*` que substitui.
Cada caso em `cases/` roda nativo e compara exit.

| Fixture | Entrada | Exit esperado |
|---|---|---|
| `rt_str_eq_parity` | `"abc"=="abc"`, `"abc"=="abd"`, vazio, embutido-NUL | 0 |
| `rt_ends_contains_parity` | `ends_with`/`contains` com sufixo vazio, sufixo>agulha, hit no fim | 0 |
| `rt_str_cmp_parity` | `<`/`>`/`==` sobre prefixos e comprimentos distintos | 0 |
| `rt_str_hash_parity` | mesmo `str` → mesmo hash, deterministicamente | 0 |
| `rt_panic_div0_marker` | `1/0` | não-zero **e** stderr contém `deliberate panic` (marcador, não código — `teko_rt.tks:688–690`) |
| `rt_panic_cast_marker` | cast impossível | idem |
| `rt_concat_parity` (C3) | `str_concat` de N pedaços, um vazio | 0 |
| `rt_int_to_str_parity` (C3) | `i64::MIN`, 0, negativos | 0 |
| `rt_ftoa_roundtrip` (C4) | float que exige `%.17g` round-trip | 0 |

Cada fixture deve falhar **hoje** de forma nomeada (o gêmeo ainda não está religado) e
passar após o crumb correspondente — a disciplina de honest-stop.

---

## 6. Relação com o `tdb` — isto é PLANO, não implementação

**Nada aqui se implementa até `teko` ser 100% nativo (self-hosted).** Motivos provados,
não retóricos:

- O próprio `teko_rt.tks` diz que religar o despacho de nome-nu para dentro dele *"touches
  the native backend's core call lowering and needs the full self-host gate to validate,
  so it is not this change"* (cabeçalho, ~linha 55).
- O gate de teste nativo ainda **não existe de fato**: `mapa-native-6-pernas-0.3.1.0.md`
  mede que **Probe D (a suíte de testes) não exercita o backend nativo** — `teko test .`
  ignora `TEKO_BACKEND=native` e sempre compila pela rota C via `codegen::tk_emit_c_test`
  + `run_cc`; o "exit 0" no Linux é um **falso verde**. As famílias de harness
  (`tk_cov_*`, `tk_arena_*`, `tk_test_*`, `tk_assert_scenario_*` — §1.1/§1.2) só podem ser
  validadas nativamente quando esse gate nativo existir.
- O `tdb` (`docs/design/tdb-proposta-0.3.1.md`) é o teste que fecha esse laço: enquanto o
  fluxo de teste nativo não estiver de pé e verde por si, religar qualquer `tk_*` para o
  gêmeo Teko não tem como ser provado sem regressão. Por isso **este documento é o mapa e
  a sequência**; a virada de chave espera o self-host 100% nativo e o gate nativo do
  `tdb`.

O que **já** está entregue e pronto para o implementador retomar em minutos: a superfície
completa com `arquivo:linha` (§1), a taxonomia de três camadas com prova de piso (§2), o
veredito de rota (§3), a ordem (§4), a sequência de crumbs e as fixtures (§5). O que
permanece **bloqueado**: C3 (lacunas de arena em `arena-em-teko.md`), C4 (float em Teko),
e todos os pontos de ritual (self-host + gate nativo/`tdb`).

---

## 7. Tensões de lei e resolução

- **Lei "runtime em Teko" vs. `teko_rt.{c,h}` é semente mantida.** Sem tensão: a Camada 1
  (syscall) e a Porta B são exatamente a exceção de semente C mantida
  (`teko_rt.{c,h}` + assert seed). O plano NÃO tenta apagá-las; reduz a lógica pura acima
  delas. Resolução law-first: passa em todas as leis — a semente encolhe para o piso, não
  some.
- **Lei "issues são 100%".** A entrega desta issue é o **mapa + plano** (o proposto). A
  implementação é explicitamente diferida (§6), então o 100% aqui é o documento completo,
  não código. Sem regressão possível — nada de produto muda.
- **Achados adjacentes reportados, não viram issue nova por mim**: (1) `native_builtin_symbol`
  já é a costura única de religação — nenhuma refatoração de isel/objfile é necessária
  (§0.a); (2) Probe D é falso-verde no nativo (`mapa-native-6-pernas`), o que precisa
  estar de pé antes de qualquer virada de chave; (3) as duas lacunas de arena
  (`arena-em-teko.md`) são o bloqueio real do Degrau 1. Reportados aqui para o dono, não
  transformados em trabalho novo.

**Sem HALT.** Não há tensão de lei genuína e não resolvida — o piso de syscall é a semente
C mantida por design, e o resto é sequenciável. O caminho é a rota (a), na ordem da §4,
atrás do gate self-host/`tdb`.
