# Migração do runtime C → Teko — roadmap ORDENADO (camada-2 do expurgo)

> **ROADMAP DESIGN-AHEAD — DEPENDE DO FIXPOINT NATIVO FECHAR PRIMEIRO.** Nenhuma fase deste
> documento pode pousar enquanto o backend próprio não compilar o programa do compilador
> (`docs/design/gate-sem-c-0.3.0.31.md` §2.3: gen1 nativo para em 53 paradas honestas distintas,
> família de ponto-flutuante inteira dentro). Este é o **desenho** que fica pronto para o
> implementador resumir em minutos quando o fixpoint fechar. Nada aqui toca produto, nada bumpa,
> nada abre PR. Cada afirmação carrega arquivo:linha medido.
>
> **A camada.** Três camadas matam a dependência de C, em tempos distintos:
> - **Camada-1** — a EMISSÃO de C morre quando o fixpoint nativo fechar (o emissor `codegen.tks`
>   sai; `docs/design/expurgo-do-c-e-a-busca-por-linker-0.3.1.md` §9).
> - **Camada-2 (ESTE documento)** — o RUNTIME de execução (`src/runtime/teko_rt.{c,h}` +
>   `src/win32_compat.h` + `src/assert/assert.{c,h}`) migra para Teko. É a resposta direta à
>   pergunta do dono: *fechar o link nativo mata `teko_rt.c`/`win32_compat.h`?* — **só quando o
>   runtime migrar; o link nativo é pré-condição, não a causa** (medido em §2.4 do gate-sem-c: apagar
>   os `.c` quebra a ligação de QUALQUER programa, hello world incluído, porque `build_cc_argv:923-924`
>   recompila `teko_rt.c`+`assert.c` a cada link).
> - **Camada-3** — o linker próprio (Fase E), que remove `cc`/`ld` como driver. FUTURO, fora deste
>   documento exceto onde a camada-2 encosta nele (o bloco de processo do Win32, §3.3).
>
> **A lei do congelamento e esta migração.** A lei permanente diz que `teko_rt.{c,h}` + o seed de
> assert são C MANTIDA (exceção ao congelamento). Leitura law-first: essa exceção é a **PONTE, não o
> destino**. O decreto do dono (`expurgo…` §3: *"vai para Teko. É projeto, não tradução"*) faz da
> camada-2 exatamente o trabalho de **retirar** a exceção. Durante a transição o C é mantido (é
> onde `tk_str_concat_r`/`region_enter` acabaram de ser adicionados como "C MANTIDA" —
> `modelo-de-memoria-por-escopo-0.3.1.md` §9); no fim, ele é deletado. Não há tensão: a exceção
> cobre a ponte enquanto ela existe.

---

## 1. INVENTÁRIO — coberto (`teko_rt.tks`) × faltante (só em `teko_rt.c`)

`teko_rt.c` = 4.149 linhas, ~156 símbolos. `teko_rt.tks` = 772 linhas. `teko_rt.h` = 1.474 linhas
(o SEAM). `win32_compat.h` = 340 linhas (shim POSIX→Win32). `assert.c` = 256 linhas (seed).

### 1.1 O que `teko_rt.tks` JÁ realiza em Teko (o gêmeo canônico — Camada L0)

| subsistema | funções cobertas em `teko_rt.tks` | natureza |
|---|---|---|
| **io / panic / exit** | `print`/`println`/`write`/`ewrite`/`eprint`/`eprintln` (como `extern fn … from "teko_rt"`, `:62-101`), `rt_abort` (`= "abort" from "c"`, `:680`), `panic`/`panic_div0`/`panic_oob`/`panic_cast`/`panic_overflow`/`panic_oob_at` (`:695-746`) | corpo Teko; io/panic ainda **ligam ao símbolo `tk_*` do C** (B2, circular) |
| **conversão int→str** | `u64_to_str`/`i64_to_str`/`str_concat`/`str_of_bytes`/`one_byte`/`concat` (`:115-184`) | Teko puro sobre `[]byte` + `teko::list` |
| **formatação** | `pad_left`/`group_thousands`/`to_radix`/`fmt_d`/`fmt_x_upper`/`fmt_x_lower`/`fmt_b`/`fmt_n_i`/`fmt_dyn_i64`/`fmt_dyn_u64`/`fmt_lead_lower`/`fmt_parse_prec` (`:206-487`) | Teko puro |
| **formatação FLOAT** | `float_parse`/`ftoa`/`fmt_f`/`fmt_e`/`fmt_g`/`fmt_n_f`/`fmt_p`/`fmt_dyn_f64`/`f64_g17` (`:107-599`) | **DIFERIDO** — rodam sobre `teko::float::parse`/`teko::fmt::f64_g17`, um FFI-bottom de host ainda não realizado; o gêmeo C (`tk_ftoa`/`tk_fmt_*`) carrega o `%.*f` preciso |
| **query/slice de str** | `str_eq`/`str_hash` (FNV-1a)/`str_compare`/`str_slice`/`str_slice_to`/`str_slice_from`/`str_len`/`str_ends_with`/`str_contains` (`:496-594`) | Teko puro |
| **guardas F3** | `div`/`mod` (checked)/`to_u8` (narrowing checked) (`:754-772`) | Teko puro |

**Veredito L0:** o subsistema io/panic/fmt/str-query/guardas está **~90% em Teko já**. O que falta
para fechá-lo NÃO é traduzir mais — é (a) fechar B2 (a circularidade io→`tk_write`), (b) realizar o
FFI-bottom de float, e (c) o fixpoint nativo compilar os corpos.

### 1.2 O que vive SÓ em `teko_rt.c` — por subsistema (o faltante)

Categorização dos ~156 símbolos. **Nenhum tem gêmeo Teko hoje** (só notas honestas de SUPREME-RULE
em `teko_rt.tks:602-669` documentando o par).

| subsistema | símbolos-chave `teko_rt.c` (linha) | por que ainda é C |
|---|---|---|
| **S0 — alloc** | `tk_alloc` (`:2101`) | seam de alocação; `malloc(n\|\|1)`, OOM-panic |
| **S1/S2 — arena/regiões** | `tk_chunk_alloc/_free/_try`, `tk_region_new/_alloc/_drop/_drop_subtree/_register/_lookup/_root/_current/_enter/_leave/_program`, `tk_region_new_u`/`_root_u`/`_drop_u`/`_enter_u`, `tk_regions_free_all`, `tk_arena_push/_pop/_commit`, `tk_free_take/_block/_purge`, `tk_region_gen_next`, `tk_termination_hook_once` (`:1096-2199`) | **memória crua** — ponteiros `void*`/`tk_region*`; sem superfície de ponteiro em Teko (o decreto: "vai para Teko") |
| **slice/box (5º gap)** | `tk_slice_push`/`_push_r`/`_push_fo`/`tk_slice_elem_box`/`tk_append_bytes_fo`/`tk_slice_with_cap`/`_r`/`tk_mem_copy`/`tk_push_slot`/`tk_push_cache_purge` (`:3762-3995`) | crescimento/boxing de agregado; a mesma memória crua do arena |
| **str/bytes construção** | `tk_str_concat`/`_concat_r`/`_of_bytes`/`tk_one_byte`/`tk_u64_to_str`/`tk_i64_to_str`/`tk_bytes_of_str`/`tk_cstr_dup`/`tk_str_from_cstr`/`tk_as_ptr`/`tk_bytes_from_ptr` + os `_len` (`:143-370`) | **têm gêmeo Teko em L0** — o C é só o caminho do emissor morrendo; `_concat_r` é a versão rotável (§4.1) |
| **UTF-8 char** | `tk_char_to_u32`/`tk_str_len_chars`/`tk_str_chars`/`tk_char_at`/`tk_str_slice_chars`(`_len`)/`tk_is_alpha`/`tk_is_digit`/`tk_is_space`/`tk_to_lower`/`tk_to_upper`, `rt_valid_utf8`/`tk_rt_str_from_utf8`(`_ok`) (`:287-1041`) | decodificação UTF-8 + validação RFC 3629; `chars` exige `str→char` (cast hoje indefinido) |
| **slice-eq / char-eq** | `tk_str_eq`(tem gêmeo)/`tk_char_eq`/`tk_slice_eq_bytes`/`_f32_eq`/`_f64_eq`/`_char_eq`/`_str_eq` (`:718-768`) | comparação estrutural de slice/char |
| **FFI host — fs/env** | `tk_rt_read_file`/`_read_line`/`_read_stdin`/`_stdin_eof`/`_getenv`/`_setenv`/`_write_file`/`_write_file_bytes`/`_append_file`/`_chdir`/`_mkdir`/`_remove_file`/`_getcwd`/`_list_dir`/`_last_index_of`(`_ok`)/`tk_sort_names` (`:2665-2960`) | **syscalls POSIX/Win32**; retornam SRES/URES/SLRES; consumidores do `win32_compat.h` fs-half |
| **FFI host — processo** | `tk_rt_run`/`_run_quiet`/`_spawn_redirected`/`_wait_one`/`_pipe`/`_pipe_read_fd`/`_pipe_write_fd`/`_close_fd`/`_fd_wait_readable`/`_fd_fill`/`_fd_take_byte` + os helpers de redirect (`_open_redirect`/`_child_bind_all`/`_next_nul_token`/…) (`:2969-3363`) | **fork/execvp/waitpid + CreateProcess**; consumidores do `win32_compat.h` process-half (o BLOCO GRANDE) |
| **FFI host — ambiente** | `tk_set_args`/`tk_rt_args`/`_os`/`_arch`/`_nproc`/`_version`/`_secure_bytes` (getrandom)/`tk_peak_rss` (getrusage) (`:3363-3520`) | probes de plataforma |
| **tempo/data** | `tk_wall_now_ns`/`tk_jdn_to_ymd`/`tk_rt_date_*`/`_wall_days`/`_wall_ns_of_day`/`_wall_offset_minutes`/`_monotonic_ns` (`:4053-4149`) | `clock_gettime`/`localtime_r` |
| **interning** | `tk_intern_find/_dup/_get/_put/_reset` (`:1285-1355`) | hash-map sobre arena |
| **isolamento por task** | `tk_task_current/_begin/_end/_reset/_reset_transient` (`:1217-2101`) | roots por task; reset transitório |
| **names (F4 — handles/canais)** | `tk_names_open/_lookup/_close/_status/_grow/_take/_forget/_live_count/_capacity/_cell_open/_cell_status/_cell_read` (`:1939-2073`) | tabela de handles geracionais; célula em `tk_region_program()` (cross-thread, §4.2) |
| **cobertura** | `tk_cov_reset/_mark/_enter/_leave/_branch(_at/_hit)/_line(_at/_hit)/_dump/_merge/_lines_on/_branches_on/…` (`:3532-3762`) | instrumentação; `tk_cov_dump(char*)` é o furo de ABI (§4.4) |
| **harness de teste** | `tk_test_begin/_end/_run/_report/_summary/_shard_take/_scope/_capture_*`, `tk_assert_scenario_*`, `tk_chan_*` (`:2199-2516`) | **setjmp/longjmp** (`:2304-2330`) — captura de panic sem matar a suíte |
| **backtrace/crash** | `tk_tsym_load/_resolve`/`tk_backtrace`/`tk_rt_crash_handler` (`:60-137`) | `signal`+`backtrace`+`backtrace_symbols_fd` |
| **observabilidade** | `tk_obs_enabled/_add/_mstr_note/_dump(_table)` (`:1518-1618`) | `TEKO_ARENA_OBS`; `dladdr` |
| **math** | `tk_fdiv`/`tk_f64_bits`/`tk_f64_from_bits`/`tk_rt_float_parse_bits` (`:2554-4037`) | bit-reinterpret + div checado |
| **exit/status** | `tk_exit`/`tk_exit_status`/`tk_panic_str`/`tk_flush_out` (`:2516-2611`) | têm quase-gêmeo em L0; `tk_exit` é 1 dos 11 símbolos vivos |

### 1.3 `win32_compat.h` — o que é, e a quem serve

Shim PURO POSIX→Win32, **consumido exclusivamente pelos `tk_rt_*` de fs+processo** de `teko_rt.c`.
Nada mais o inclui. Duas metades:

| metade | superfície (`win32_compat.h`) | consumidor em `teko_rt.c` |
|---|---|---|
| **fs** | `chdir→_chdir`, `mkdir→_mkdir`, `getcwd→_getcwd`, `setenv→_putenv_s`, dirent shim (`tk_opendir/_readdir/_closedir` sobre `FindFirstFileA`) (`:42-100`) | `tk_rt_chdir/_mkdir/_getcwd/_setenv/_list_dir` |
| **processo** | `tk_win32_quote_arg`, `tk_win32_spawnvp`, `tk_win32_spawn_redirected` (CreateProcessA + STARTUPINFOA/PROCESS_INFORMATION), `tk_win32_wait_one`, `tk_win32_redirect_handle*`, `tk_win32_build_envblock` (`:119-336`) | `tk_rt_run/_run_quiet/_spawn_redirected/_wait_one/_pipe` |

**Consequência de inventário:** `win32_compat.h` não tem vida própria — ele é uma FOLHA dos
subsistemas FFI-host fs+processo. Ele morre exatamente quando esses dois migram (§3.3), em DUAS
etapas (fs primeiro, processo depois).

### 1.4 O seed de assert (`assert.{c,h}`)

256 linhas, símbolos `assert`, `assert__eq_i64`/`_u64`/`_f64`/`_str`/`_bytes_eq`/`_ge_i64`/`_gt_i64`/
`_is_error`/`_is_absent`, etc. Twin C separado, linkado ao lado de `teko_rt.c` na mesma linha de `cc`.
É `teko::assert`. **Fica no MESMO grupo da migração** (não é irredutível — é comparação de valores +
`panic`, tudo L0-shaped), mas migra ao lado do harness de teste (Fase 6), porque é o que o harness
consome. Enquanto for C, é a segunda metade da exceção-de-congelamento a retirar.

---

## 2. A ORDEM de migração — folha → primitiva-de-SO, e como o nativo LINKA seu runtime Teko

### 2.1 O princípio de ordenação (dependência de SO crescente)

A ordem NÃO é por tamanho — é por **profundidade de acoplamento ao SO**. Um subsistema migra quando
tudo abaixo dele já migrou OU é alcançável por `extern fn`:

```
L0  io/panic/fmt/str-query/guardas      ── computação pura sobre []byte; NENHUM estado de SO
L1  alloc + arena/regiões + slice/box   ── UMA syscall de memória (aligned_alloc), via extern fn
L2  UTF-8 char + str-construção rotável  ── só depende de L1 (memória); computação pura
L3  FFI host fs/env + tempo/data         ── syscalls POSIX/Win32 folha (open/read/getenv/clock)
L4  FFI host processo/pipes/redirect     ── fork/exec/CreateProcess + STRUCT-BY-VALUE FFI
L5  interning/task/names/cobertura       ── estado de processo sobre L1; names = cross-thread
L6  harness de teste + assert + backtrace ── setjmp/longjmp (IRREDUTÍVEL parcial) + signal
```

**A regra de bootstrap-seed manda a sequência:** o seed é o `teko` lançado anterior; o corpus não
pode USAR uma feature ainda ausente no seed. Portanto cada camada só declara `extern fn`/builtin que
o seed da vez já reconhece. L1 é o gargalo: exige P1/P2 (§2.3) no compilador ANTES de o arena em
Teko poder ser semeado.

### 2.2 Como o backend nativo LINKA seu próprio runtime em Teko — SEM passar por `cc`

Este é o mecanismo que torna a camada-2 possível, e ele **já foi provado** (`afdb1fd8`, citado em
`expurgo…` §3): um `extern fn` Teko mira o SÍMBOLO C declarado, não o mangling Teko. `call_symbol →
find_extern_symbol`, `collect_undefined_x86` deriva `GLOBAL|NOTYPE / SHN_UNDEF`, `objfile_elf.tks`
emite `R_X86_64_PLT32`. Prova: `pub extern fn c_getpid(): i32 = "getpid"` liga a `U getpid`, roda.

Consequência para o runtime:

1. **Os 11 símbolos de runtime vivos no caminho nativo** (`expurgo…` §2: `tk_print`/`tk_println`/
   `tk_write`/`tk_eprint`/`tk_eprintln`/`tk_ewrite`/`tk_panic_str`/`tk_exit`/`tk_str_concat`/
   `tk_i64_to_str`/`tk_u64_to_str`) deixam de ser referências a um objeto C separado e passam a ser
   **`exp fn` Teko compiladas NO PRÓPRIO objeto do programa**. Não há runtime separado para linkar,
   logo não há runtime para EMBUTIR no compilador distribuído (mata a "Fase 7" de bundling por
   construção — `expurgo…` §3).
2. **O fundo de SO é alcançado por `extern fn` direto ao símbolo da libc**, sem protótipo (o nativo
   emite a chamada ao símbolo; NÃO gera protótipo, então não colide com `<stdlib.h>` — provado em
   `arena-em-teko.md` §1, `examples/probes/arena_bottom`: `U aligned_alloc@GLIBC_2.16`, roda).
3. **A costura str→(ptr,len).** `extern fn` geral só aceita primitivo/`byte`/`ptr`/`uptr`/handle
   (C7.1a) — `str` é fat-pointer `{ptr,len}`, proibido salvo os 7 nomes io/panic (`teko_rt.tks:33-40`).
   Logo o runtime migrado que chama `write(fd,buf,n)` do host passa **endereços como `u64`** (o
   `arena_bottom` já faz: `c_setenv(name:u64, value:u64, …)`), decompondo `str` em `s.ptr to u64` +
   `s.len`. Isto é a disciplina `c_types`/marshalling em voo (`c-types-and-marshalling-0.3.1.md`).

**Portanto o link do runtime Teko é o link normal do programa:** `ld` (camada-2) e depois o linker
próprio (camada-3) resolvem os símbolos da libc/kernel32 como undefined externos; nenhum passo
recompila `teko_rt.c` porque ele não existe mais. O que hoje é `build_cc_argv:923-924` empurrando
`teko_rt.c`+`assert.c` para o `cc` **desaparece** — é exatamente o defeito B1 (`expurgo…` §9.2) que
esta migração remove pela raiz.

### 2.3 O gargalo L1 — as duas costuras de compilador que o arena exige (JÁ desenhadas)

O arena em Teko está **desenhado e com implementação de referência pronta** em
`examples/probes/arena_teko/` (13 arquivos, 12 funções, gates de mutação — `arena-em-teko.md` §7).
O que falta é a superfície de compilador, e ela é MÍNIMA:

- **P1 — load/store de palavra em endereço calculado.** Duas entradas em `scope.tks::builtin_fn`
  (`teko::mem::load_u64(addr)`/`store_u64(addr,v)`) + dois braços em `lower.tks`, cada um lowerando
  para `LLoad`/`LStore` (`lir.tks:92-98`) — instruções que TODO acesso a campo já emite. **A
  propriedade que quebra a circularidade: load/store crus NÃO alocam** (`arena-em-teko.md` §2 P1).
- **P2 — uma palavra mutável de módulo.** UM slot `ARENA_CONTROL: u64` lowerando para `LGlobalAddr`
  apontando `.bss` (`lir.tks:100-104`). Todo o resto mora dentro do bloco de controle, alcançado por
  P1. Fallback sem mudança de linguagem: `mmap`/`VirtualAlloc` com endereço FIXO (plano B, `§2 P2`).

Sem P1/P2 no seed, L1 não pode ser semeado — e como L2+ alocam através do seam de L1, **P1/P2 são o
primeiro trabalho de compilador da camada-2**, e são pré-requisito de tudo o mais.

### 2.4 As folhas primeiro — por que L0 e L2 não esperam L3+

L0 (fmt/str-query) e L2 (UTF-8/str-construção) são **computação pura sobre `[]byte`**: não tocam
estado de SO, só o seam de alocação (L1). Migram assim que L1 existe, INDEPENDENTES de fs/processo.
Isto é a mandato "adiantar o que der": todo o L0/L2 pode fechar enquanto o bloco Win32 de processo
(L4, o mais difícil) ainda é C. O corte é limpo porque `teko_rt.tks` já provou a forma (L0 já é
~90% Teko).

---

## 3. O NÚCLEO IRREDUTÍVEL e o que mata `win32_compat.h`

### 3.1 O que NÃO é núcleo irredutível (esclarecimento — a maioria não é)

O fundo de SO (aligned_alloc/mmap/write/read/open/fork/execvp/CreateProcess/abort/getenv/clock_gettime)
é **a PLATAFORMA, alcançada por `extern fn`** — não é "C que mantemos". Esses símbolos nunca migram
porque nunca foram nossos: são linkados, como qualquer libc. Chamá-los de "irredutível" seria erro
de categoria. O irredutível verdadeiro é só o C-que-MANTEMOS que não tem como virar Teko nem `extern`.

### 3.2 O núcleo irredutível-até-Fase-E (curto e nomeado)

| item | por que é irredutível hoje | destino |
|---|---|---|
| **entrypoint / argc-argv** | o `main` nativo tem ZERO parâmetros (`gate-sem-c` §2.2f: `lower_virtual_main` → `new_func("main",0,…)`; nenhuma ocorrência de `argc`/`argv` em `src/lir`+`src/backend`). Todo binário nativo vê `teko::env::args()` VAZIO | trabalho do **backend próprio/linker** (crt0 entrega argc/argv), não runtime-C — reportar ao vagão do backend |
| **setjmp/longjmp do harness** | `tk_test_run` (`:2330`) captura um panic sem matar a suíte via setjmp/longjmp (`:2304-2323`). Teko NÃO tem superfície de controle não-local | **tensão real** — precisa de primitiva de captura Teko OU um intrínseco do backend; senão, um shim C mínimo sobrevive à Fase 6 (§7 R3) |
| **fatal-signal handler** | `tk_rt_crash_handler` instala handler SIGSEGV/BUS/ILL/FPE (`:120`) + `backtrace` | o CORPO vira Teko; a instalação (`signal`/`sigaction`) e `backtrace`/`backtrace_symbols_fd` são `extern fn` — migrável, baixa prioridade |
| **`tk_cov_dump(const char*)`** | único símbolo do contrato do gate cujo parâmetro é `char*` e não `tk_str` (`gate-sem-c` §2.2e/§4.2) | **decisão do dono** — superfície nova `tk_cov_dump_s(tk_str)` (simétrica a `tk_cov_merge`), ou builtin; até lá, o dump fica C |

Tudo o mais — arena, str, UTF-8, fs, processo, interning, task, names, cobertura, tempo — é
**migrável**, ou por Teko puro (sobre L1) ou por `extern fn` ao símbolo do SO.

### 3.3 O que mata `win32_compat.h` — ESPECIFICAMENTE, em duas etapas

`win32_compat.h` morre quando seus DOIS únicos consumidores (fs e processo de `teko_rt.c`) deixam de
ser C. Cada metade tem um gate distinto:

**Etapa A — a metade fs morre com L3.** `tk_rt_chdir/_mkdir/_getcwd/_setenv/_list_dir` viram
`extern fn` Teko que **selecionam o símbolo por alvo**: POSIX `chdir`/`mkdir`/`getcwd`/`setenv`/
`opendir`+`readdir` vs Win32 `_chdir`/`_mkdir`/`_getcwd`/`_putenv_s`/`FindFirstFileA`+`FindNextFileA`.
Isto exige **uma superfície de seleção-de-símbolo-por-alvo na declaração `extern`** que hoje NÃO
existe (`arena-em-teko.md` §6 risco Windows: *"O `extern fn` precisa selecionar por alvo, o que hoje
não tem forma na declaração"*). Esse é o ÚNICO bloqueio de compilador da metade fs. Fechado ele, os
`#define chdir _chdir` etc. do `win32_compat.h:42-100` ficam órfãos e saem.

**Etapa B — a metade processo morre com L4 + encosta na Fase E.** `tk_rt_spawn_redirected`/`_run`/
`_wait_one`/`_pipe` viram Teko, mas o corpo Win32 (`tk_win32_spawn_redirected`, `:298`) preenche
`STARTUPINFOA` e lê `PROCESS_INFORMATION` — **structs passadas/retornadas por valor no ABI Win32**.
Isto DEPENDE do **ABI de FFI de struct que acabou de ser resolvido** (`star-ref-and-ffi-0.3.1.md`
§4: FFI own-backend-first, layout de struct + reverse-FFI sem `cc`). Sem struct-by-value FFI, o
bloco de processo não pode virar Teko. E o `CreateProcessA`/`DuplicateHandle`/`WaitForSingleObject`
precisam do **linker próprio resolvendo `kernel32`** (a import library do Win32) — território
Fase E/camada-3. Portanto a metade processo de `win32_compat.h` é a ÚLTIMA coisa a morrer, e sua
morte encosta na camada-3.

**Resumo:** `win32_compat.h` morre quando (A) existe seleção-de-símbolo-`extern`-por-alvo [fecha a
metade fs] E (B) o struct-by-value FFI ABI + o linker próprio de import-lib Win32 existem [fecham a
metade processo]. A metade fs pode sair em L3; a metade processo só em L4/Fase-E. O arquivo some no
commit em que a última metade sai — não antes.

---

## 4. INTERAÇÕES com o trabalho em voo (a residência é a mesma do dono)

O runtime em Teko é ELE PRÓPRIO um programa Teko — logo é súdito da MESMA regra de residência que o
compilador impõe ao código de usuário (`modelo-de-memoria-por-escopo-0.3.1.md`). Isto NÃO é
opcional: um runtime que vaze para root viola o modelo que ele existe para servir.

### 4.1 Modelo de memória por região — o runtime honra "morre no escopo / move no return"

A regra do dono (`modelo…` §0): toda variável morre no fim do seu escopo léxico; só `#singleton` e
cross-thread (`chan`/`wait_group`) alcançam root/programa; um `return` MOVE para a arena do caller.
Impacto no runtime migrado:

- **Funções de runtime que PRODUZEM `str`** (`str_concat`, `str_slice`, `one_byte`, `fmt_*`,
  `u64_to_str`) **não podem alocar em root** — têm de rotear pela **região-corrente** do caller.
  É exatamente por isso que `tk_str_concat_r(tk_region*, …)` foi adicionado (`modelo…` §9,
  `teko_rt.c:158`): o buffer fresco bumpa em `r`, morrendo com o escopo. O runtime Teko herda esse
  seletor N-níveis: um `str` de runtime é MOVIDO para o caller no return, nunca vaza. **O
  `tk_str_concat` sem `_r` (root) fica só para residência-programa.**
- **A arena é o CASO-LIMITE legítimo de root.** O bloco de controle P2 (`ARENA_CONTROL`,
  `arena-em-teko.md` §3.4) é `#singleton`-like por natureza — UMA palavra de processo. É uma das
  DUAS origens legítimas de root do §1 do modelo (declarada/estrutural), não um fallback. Sem tensão.
- **`region_enter`/`region_leave` são a primitiva ÚNICA compartilhada** (`modelo…` §3b/§14): o mesmo
  par que o `backend-memoria-por-funcao` adiciona para o scratch do compilador serve as regiões de
  escopo do programa gerado. O runtime migrado NÃO reimplementa — reusa. Uma primitiva, três
  consumidores (scratch do compilador, escopos do programa, o próprio runtime).

### 4.2 Cross-thread → região de PROGRAMA (o subsistema `names`)

O subsistema **names** (`tk_names_*`, `teko_rt.c:1939-2073`) é o SEAM de handles/canais. Sua célula
é alocada em `tk_region_program()` (`teko_rt.h:265`, `teko_rt.c:1842`) — a região "owned by NO task
… survives the task's exit" (`teko_rt.h:170-175`). Quando `names` migrar (L5), ele é o ÚNICO
subsistema de runtime cuja residência é **programa por design** — porque é o assento de `chan`/
`wait_group`, os únicos residentes-programa legítimos (`modelo…` §2). O runtime Teko honra isto:
`names` aloca em `tk_region_program()`, tudo o mais em região de escopo. Isto ANTECIPA a chegada de
`chan` (que ainda não tem superfície, `spine.tks:1555`): quando `chan` chegar, o seam já reside
certo.

### 4.3 ABI de FFI de struct (recém-resolvido) — o que o runtime-em-Teko exige dele

Dois pontos de contato:

- **Os `tk_rt_*` de host retornam structs por valor** — `tk_ffi_sres`/`_ures`/`_slres`/`_u64res`
  (`{ok, value, err}`, `{ok, ptr, len}`). Quando `read_file`/`getenv`/`list_dir`/`getcwd` migrarem
  (L3), o lift `{ok,value,err}` cruza a fronteira FFI **como struct-by-value** — tem de honrar a
  classificação sret/register-pair do ABI recém-resolvido (`c-types-and-marshalling-0.3.1.md` §5,
  `star-ref-and-ffi-0.3.1.md` §4). O runtime NÃO inventa ABI; consome o do backend próprio.
- **O bloco Win32 de processo** (L4) passa `STARTUPINFOA`/`PROCESS_INFORMATION` por valor —
  o caso mais pesado de struct-by-value, que **bloqueia** L4 até o ABI de struct + reverse-FFI
  estarem prontos no backend próprio (§3.3 etapa B).

### 4.4 Boxing de agregado/slice (o 5º gap) — o runtime honra a MESMA regra de boxing

Os símbolos `tk_slice_push`/`_push_r`/`_push_fo`/`tk_slice_elem_box`/`tk_append_bytes_fo`
(`teko_rt.c:3762-3968`) SÃO o seam de boxing de agregado/slice do 5º gap. Interação dupla:

- **O runtime é CONSUMIDOR do boxing**: todo `teko::list::push` nos corpos de `teko_rt.tks`
  (`str_concat`, `u64_to_str`, …) lowera para `tk_slice_push` → box no arena. Quando o boxing
  migrar para respeitar a residência (`_push_r` roteando para a região-corrente, não root — a mesma
  correção de `modelo…` §4), o runtime herda a regra automaticamente: um `[]byte` construído num
  escopo do runtime morre com o escopo.
- **O runtime é também o PROVEDOR** desses símbolos (eles vivem em `teko_rt.c`). Ao migrar (L1, com
  o arena), a versão Teko de `tk_slice_push_r` tem de bump-boxar no `r` corrente e honrar o ceil16
  do take / floor16 do park (`arena-em-teko.md` §4.1) — a assimetria que guarda o decreto de
  aliasing. Uma implementação de boxing que aloque em root reabre o vazamento que `modelo…` fecha.

**Regra única:** o boxing do runtime-em-Teko usa o mesmo `ResidencePlan` (`modelo…` §14) que o
código de usuário. Não há um "boxing de runtime" e um "boxing de usuário" — é um só, e o runtime é
o primeiro cliente a provar que a regra fecha sobre si mesma.

---

## 5. ROADMAP ORDENADO em FASES (cada uma com critério de prova)

Convenção de prova: **FIXPOINT** = gen2==gen3 byte-idêntico sob `TEKO_BACKEND=native`; **`teko
test .`** = suíte verde; **own==C** = `diff_vm_native` / diff C-vs-nativo inalterado DURANTE a
transição (os dois motores concordam enquanto ambos existem). Ritual = ponto onde o gate COMPLETO
tem de passar. Todas as fases estão BLOQUEADAS atrás do fixpoint nativo (banner) exceto onde diz
"design-ahead pronto".

| fase | conteúdo | depende de | critério de prova | ritual |
|---|---|---|---|---|
| **F0 — este mapa** | inventário + ordem + irredutível + interações | — | entregue | não |
| **F1 — P1/P2 (costuras de compilador L1)** | `load_u64`/`store_u64` (builtin+`lower`), slot `ARENA_CONTROL` (`.bss`). Design-ahead PRONTO (`arena-em-teko.md` §2) | fixpoint | store 0xDEADBEEF em offset calculado, load idêntico; slot sobrevive a 2 chamadas | **SIM** (toca `scope.tks`+`lower.tks`) |
| **F2 — arena/regiões/slice-box em Teko (L1)** | portar as 12 fns + free-list + paranoid, do `examples/probes/arena_teko` para `src/runtime/`. Implementação de REFERÊNCIA pronta e com gates de mutação | F1 | os 6 gates de grupo (`arena-em-teko.md` §5); prova de VOLUME `predicted_chunks` bate com o gêmeo C; `TEKO_ARENA_OBS` scoped>0 | **SIM** |
| **F3 — L0/L2 fecham (str/fmt/UTF-8)** | fechar B2 (rerotear bare-name io→corpos `teko_rt.tks`); realizar float-bottom OU manter diferido nomeado; portar UTF-8 char (exige cast `str→char`); mover str-construção para `_r` (residência) | F2 | `teko test .` verde; own==C nos casos de fmt/str; FIXPOINT (bytes de fmt idênticos) | **SIM** |
| **F4 — FFI host fs/env + tempo/data (L3)** | `read_file`/`write_file`/`getenv`/`getcwd`/`chdir`/`mkdir`/`list_dir`/`args`/`os`/`arch`/`nproc`/`secure_bytes`/`clock_gettime` como `extern fn` per-target; SRES/URES por struct-by-value. **Mata a metade fs de `win32_compat.h`** | F2 + seleção-símbolo-por-alvo + struct-FFI-ABI | fixtures de fs (ler/escrever/listar) own==C; per-target: POSIX verde, Win32 compila | **SIM** |
| **F5 — FFI host processo/pipes (L4)** | `run`/`spawn_redirected`/`wait_one`/`pipe`/`fd_*` como Teko; bloco Win32 via struct-by-value + reverse-FFI. **Mata a metade processo de `win32_compat.h`** | F4 + struct-FFI-ABI completo + linker de import-lib Win32 (encosta Fase E) | fixtures de subprocesso own==C (POSIX); Win32 compilado; `win32_compat.h` REMOVIDO | **SIM** |
| **F6 — interning/task/names/cobertura + harness + assert + backtrace (L5/L6)** | portar hash-interning, roots-por-task, `names`→`tk_region_program()`, instrumentação de cobertura; harness com **decisão do dono sobre setjmp/longjmp** (§3.2) e sobre `tk_cov_dump_s` (§4.4); `assert.c`→Teko; crash-handler via `signal`/`backtrace` extern | F2–F5 | `teko test .` roda pelo harness NATIVO; cobertura não-zero; captura de panic preserva a suíte | **SIM** |
| **F7 — deleção + prova de sozinho** | apagar `teko_rt.{c,h}` + `win32_compat.h` + `assert.{c,h}` DEPOIS do gen1 (a ordem de `expurgo…` §9); remover `build_cc_argv:923-924` (B1) | F1–F6 | hello-world sozinho (`expurgo…` §9.1): nenhum `.c`/`.h`, `TK_RT_DIR` desarmado, `share/teko` inexistente → `hello, teko` exit 0; FIXPOINT gen2==gen3 sem os arquivos | **SIM — o ritual final** |

**Sequência bootstrap-segura (a escada não pode quebrar):** F1/F2 primeiro porque L2+ alocam por
L1. F3 (folhas puras) adianta enquanto F4/F5 (SO) esperam as superfícies de compilador. F7 (deleção)
é ÚLTIMA e é o TESTE, não a consequência — apagar antes do gen1 nasce provaria menos (`expurgo…`
§9). Enquanto o seed publicado emitir C, `ci_provision_teko.sh` provisiona o runtime da era do seed
(`expurgo…` §4.4) — a migração não quebra a escada porque o C do seed vem do release do seed.

---

## 6. Assinaturas / formas que o implementador adiciona (full Javadoc — copiar verbatim)

As formas de compilador (F1) e as costuras de runtime já estão desenhadas nos docs-irmãos e são
reusadas aqui, não reinventadas. As NOVAS deste roadmap (per-target `extern`, resultado FFI):

```teko
/**
 * TargetSymbol — a seleção de símbolo foreign POR ALVO que a metade fs/processo do runtime exige
 * para matar `win32_compat.h` sem um shim C. Um `extern fn` anotado resolve `posix` em alvos POSIX
 * e `win32` no alvo Windows, escolhido no lowering pelo alvo de build — o que hoje NÃO tem forma na
 * declaração (`arena-em-teko.md` §6, risco Windows). Fecha a Etapa A de §3.3.
 *
 * @param posix  o símbolo da libc no alvo POSIX (ex.: "chdir")
 * @param win32  o símbolo do CRT/kernel32 no alvo Windows (ex.: "_chdir")
 * @return       o binding de símbolo que o backend resolve por alvo
 * @since 0.3.1
 */
pub type TargetSymbol = struct {
    /** símbolo resolvido em alvos POSIX (Linux/macOS). */
    posix: str
    /** símbolo resolvido no alvo Windows. */
    win32: str
}

/* ═══════════════════════════════════════════════════════════════════════════
   WITHDRAWN (owner 2026-08-19, D-TS1) — per-target symbol selection IS the
   existing target-guarded `extern fn` mechanism (`#os` / `#arch` guards +
   `prune_cc`), not this struct. See `target-symbol-extern-selection-0.3.1.md`
   and precedent `src/io/file_stream.tks` for os_open/os_read/os_write/os_close.
   ═══════════════════════════════════════════════════════════════════════════ */

/**
 * store_u64 — grava a palavra `v` no endereço cru `addr` (P1). NÃO ALOCA — é a propriedade que
 * quebra a circularidade do arena (`arena-em-teko.md` §2 P1): a bookkeeping do arena cabe na memória
 * que ele próprio administra. Lowera para uma única `LStore` (`lir.tks:92-98`), a mesma que todo
 * acesso a campo de struct emite. Só alcançável sob `unsafe` (habilita ponteiros — a moldura
 * `c-types`/marshalling em voo).
 *
 * @param addr  o endereço-destino, um `u64` ABI-idêntico a `void*` em alvo de 64 bits
 * @param v     a palavra de 64 bits a gravar
 * @return      void
 * @since 0.3.1
 */
pub fn store_u64(addr: u64, v: u64)

/**
 * load_u64 — lê a palavra de 64 bits no endereço cru `addr` (P1). NÃO ALOCA. Lowera para uma única
 * `LLoad` (`lir.tks:92-98`). O par de `store_u64`; juntos são as duas ÚNICAS entradas de compilador
 * que L1 (arena) precisa além do slot `ARENA_CONTROL`.
 *
 * @param addr  o endereço-fonte
 * @return      a palavra de 64 bits em `addr`
 * @since 0.3.1
 */
pub fn load_u64(addr: u64): u64
```

Formas de runtime REUSADAS (não redeclarar): `tk_str_concat_r(tk_region*, tk_str, tk_str)`
(`modelo…` §9), `region_enter(child)`/`region_leave()` (`modelo…` §14 — a primitiva ÚNICA
compartilhada), e o `ResidencePlan` (`modelo…` §14) que o boxing do runtime consome como qualquer
código de usuário (§4.4). As 12 funções do arena têm forma e implementação de referência em
`examples/probes/arena_teko/{region,tree,marks,freelist,chunk}.tks`.

Superfície que precisa de DECISÃO DO DONO antes de ter forma: `tk_cov_dump_s(tk_str)` (§4.4, o furo
de ABI de `char*`) e a primitiva de captura não-local que substitui setjmp/longjmp no harness (§3.2).

---

## 7. Riscos e tensões de lei

| # | risco / tensão | resolução (law-first) |
|---|---|---|
| R1 | **A migração viola o congelamento de `teko_rt.{c,h}`** | NÃO: a exceção-de-congelamento é a PONTE; o decreto do dono (`expurgo…` §3) manda migrar. A camada-2 RETIRA a exceção. Durante a transição, aditivos como `tk_str_concat_r` são "C MANTIDA" explícita (`modelo…` §9). Sem tensão. |
| R2 | **O runtime-em-Teko vaza para root e viola o próprio modelo** | O runtime é súdito do `ResidencePlan` como qualquer código: str-produtoras roteiam por `_r`/região-corrente (§4.1); só arena-control (`#singleton`-like) e `names` (cross-thread→programa) residem wide, ambas origens LEGÍTIMAS (`modelo…` §1). |
| R3 | **setjmp/longjmp do harness não tem superfície Teko** — TENSÃO REAL | M.1 (fail-loud) exige o guarda de captura. Três saídas, decisão do dono: (a) primitiva Teko de captura não-local; (b) intrínseco do backend próprio; (c) shim C mínimo de setjmp sobrevive à Fase 6 como último resíduo. Recomendação: (c) como ponte medida + (a)/(b) como fecho — **HALT parcial para o dono se nenhuma superfície for ratificada até F6**. |
| R4 | **`win32_compat.h` (processo) depende de peças não-suas** — struct-by-value FFI + linker Win32 | Sequenciado: a metade fs (só precisa de seleção-símbolo-por-alvo) sai em F4; a metade processo espera o ABI de struct (recém-resolvido, `star-ref…` §4) + a Fase E. NÃO forçar F5 antes disso — reportado, não bloqueante para F0-F4. |
| R5 | **argc/argv nativo vazio** (`gate-sem-c` §2.2f) | NÃO é runtime-C — é backend/linker (crt0). REPORTADO ao vagão do backend; a camada-2 não o resolve nem depende dele exceto para `teko::env::args()`. |
| R6 | **`tk_cov_dump(char*)` — furo de ABI** | `tk_str→char*` é trocadilho rejeitado por M.3 (`gate-sem-c` §4.2). Precisa de `tk_cov_dump_s(tk_str)` — **decisão do dono** (superfície nova, simétrica a `tk_cov_merge`). |
| R7 | **Uma implementação de arena sutilmente errada é PIOR que nenhuma** (`arena-em-teko.md` §6) | Passa nos testes e corrompe a memória de todo programa emitido, inclusive o compilador. Por isso F2 exige os gates de MUTAÇÃO (afirmam comportamento, não ausência de crash) + o oráculo `TEKO_MEM_PARANOID` que sobrevive à morte do ASan (`arena-em-teko.md` §4.2). |
| R8 | **FIXPOINT quebra (bytes mudam)** durante F3–F5 | Impossível por estrutura se a residência dos casos existentes não muda; os casos novos trazem fixtures. O FIXPOINT byte-idêntico é o detector — se quebrar, algo mudou de residência indevidamente: parar e reexaminar. |
| R9 | **A ordem seed→gen quebra a escada** | Enquanto o seed emitir C, `ci_provision_teko.sh` provisiona o runtime da era do seed (`expurgo…` §4.4); a deleção (F7) é DEPOIS do gen1 (`expurgo…` §9). A migração não toca o que o seed faz. |

**Tensão de lei que força HALT:** UMA, parcial e condicional — **R3 (setjmp/longjmp)**. Todas as
outras resolvem via Constituição/Leis (Teko-only com a exceção-ponte de runtime; M.1 nunca-UAF
preservado pela residência; issue-100% com as folhas adiantadas). R3 só HALTa se, ao chegar em F6,
nenhuma superfície de captura não-local tiver sido ratificada — e mesmo aí há a ponte (c) do shim C
mínimo. **Recomendação de fechamento law-first para R3:** o backend próprio ganha um intrínseco de
captura (é ele que já domina a stack no crash-handler); é a saída que não deixa resíduo de C e não
inventa superfície de linguagem que só o harness usa. Levar ao dono junto de R6 (`tk_cov_dump_s`),
que são as duas únicas superfícies-novas que a camada-2 precisa de ratificação.

---

## 8. O que permanece BLOQUEADO (honesto)

- **Todo F1–F7** está atrás do **fixpoint nativo** (o backend próprio compilar o programa do
  compilador — 53 paradas honestas hoje, `gate-sem-c` §2.3). Este documento é o design-ahead; a
  execução resume quando o fixpoint fechar.
- **F5 (processo Win32)** está adicionalmente atrás do **linker próprio de import-lib** (Fase E) e do
  struct-by-value FFI COMPLETO (reverse-FFI incluído, `star-ref…` §4).
- **F4/F5 (matar `win32_compat.h`)** está atrás da **seleção-de-símbolo-`extern`-por-alvo**, que
  hoje não tem forma na declaração — o único bloqueio de compilador da metade fs, desenhado como
  `TargetSymbol` em §6, aguardando ratificação.
- **Duas superfícies precisam de decisão do dono** antes de F6: `tk_cov_dump_s(tk_str)` (R6) e a
  captura não-local do harness (R3).

O que NÃO está bloqueado e pode ser adiantado hoje em design: as formas de §6, as fixtures de cada
fase (§5), a implementação de referência do arena (JÁ existe, `examples/probes/arena_teko/`), e a
prova de que o link do runtime Teko não passa por `cc` (§2.2, já provada por `afdb1fd8`).
