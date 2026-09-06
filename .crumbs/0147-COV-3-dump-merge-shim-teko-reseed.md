---
seq: 0147
crumb-id: COV-3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild (harvest do cluster 0145+0146+0147)"
deps: [0146]
sources:
  - "DECISION_LOG.md:1079"                            # D113 item (4): dump/merge Teko + shim TKCOV1
  - "DECISION_LOG.md:478-480"                         # D47 cov_dump_s(str) ratificado
  - "src/runtime/teko_rt.c:4485-4562"                # TKCOV1 dump/merge/read_section C a reproduzir
  - "src/coverage/coverage.tks:282"                  # `pub extern fn cov_merge ... from "teko_rt"` a remover
  - "src/codegen/codegen.tks:10188,10194,10242,10363-10365"  # shims de dump emitidos
  - "src/lir/lower.tks:1842"                          # takes_one_str_by_value: tk_cov_merge
  - "src/io/file_stream.tks:159-179"                 # teko::sys::abi::os_open_raw/read/write/close (fecho D121)
---

# 0147 · COV-3 — dump/merge TKCOV1 em Teko + reescrita do shim + remoção do extern (terminal, reseed)

> `tk_cov_dump(const char*)`/`tk_cov_merge(tk_str)` reescritos em Teko (`cov_dump_s(str)` D47 + `cov_merge(str)`)
> sobre as syscalls raw `teko::sys::abi::os_*_raw` (mesmo fecho D121 dos sinks), formato `TKCOV1` u64
> host-order byte-idêntico. Remove o `from "teko_rt"` de `coverage.tks`, reescreve os 4 shims emitidos
> (`atexit`/dump inline/analyze) `tk_cov_dump`→`cov_dump_s(tk_str)`, e atualiza o espelho native. Terminal
> do cluster coverage → COLHE o reseed (TODA-OU-NADA). Frio → pico FLAT.

## Goal

Fecha a migração: serialização e merge do `.tkcov` em Teko, matando os últimos `tk_cov_*` vivos
(`tk_cov_dump`, `tk_cov_merge`, `tk_cov_read_section`, `tk_cov_write_section`) e o último `from "teko_rt"`
de coverage. `cov_dump_s`/`cov_merge` moram em `teko::runtime` sobre `teko::sys::abi::os_open_raw/
os_read_raw/os_write_raw/os_close_raw` (as MESMAS primitivas cross-plataforma que `file_stream.tks:159-179`
usa) — mantendo o fecho do prelúdio D121 (nada de `teko::io`/stdlib no runtime) e a disponibilidade no
FILHO sem qualquer wiring de build. O shim emitido `__tk_cov_atexit_dump` some: a emissão passa a
registrar/chamar o símbolo Teko-mangled, e o `getenv` do filho vira leitura Teko do overlay de env já
capturado (D124), passando `tk_str` a `cov_dump_s`. Byte-preservação: o `TKCOV1` gravado/lido é idêntico
ao C (magic `"TKCOV1\0\0"`, 3 seções `(count:u64, ids:u64[count])` fns/branches/lines, host-order). Este é
o crumb que DIRIGE o reseed único do cluster (0145 defs + 0146 reroteio + 0147 dump/merge landam juntos).

## Where

- `src/runtime/coverage_rt.tks` (do 0145) — NOVAS `exp fn cov_dump_s(path: str)` e `exp fn cov_merge(path: str): bool`
  + privados `cov_write_u64`/`cov_write_section`/`cov_read_u64`/`cov_read_section`/`cov_open_wr`/`cov_open_rd`.
- `src/coverage/coverage.tks:282` — REMOVER `pub extern fn cov_merge(path: str): bool = "tk_cov_merge" from "teko_rt"`;
  substituir por delegate Teko: `pub fn cov_merge(path: str): bool { teko::runtime::cov_merge(path) }`
  (mantém a superfície `coverage::cov_merge` que `project.tks:2737,2787,3396` e `regression.tks:824` chamam).
- `src/codegen/codegen.tks:10188` — REMOVER a emissão do shim C `static void __tk_cov_atexit_dump(...)`.
- `src/codegen/codegen.tks:10194` — `atexit(__tk_cov_atexit_dump)` → `atexit(` +
  `cb_fn_name_str("teko::runtime","cov_atexit_dump")` + `)` (o entry Teko `void(void)`).
- `src/codegen/codegen.tks:10242` — `{ const char *dp = getenv("TEKO_TKCOV"); if (dp) tk_cov_dump(dp); }` →
  chamada única emitida `cb_fn_name_str("teko::runtime","cov_atexit_dump")` + `();`.
- `src/codegen/codegen.tks:10363-10365` (analyze) — o dump per-fn `%s/IDX.tkcov` via `snprintf`+`tk_cov_dump(p)`
  → construir o path em Teko e chamar `cov_dump_s`: emitir `cov_atexit_dump_to(<dir-env>, IDX)` (novo entry
  Teko que monta `<dir>/<idx>.tkcov` e chama `cov_dump_s`), ou manter o `snprintf` emitido e trocar só
  `tk_cov_dump(p)` por `cov_dump_s((tk_str){p, <len>})`. DECISÃO: entry Teko `cov_atexit_dump_to` (zero
  `snprintf`/char[] emitido; consistente com o atexit).
- `src/lir/lower.tks:1842` — `takes_one_str_by_value`: `tk_cov_merge` → `teko_teko__runtime__cov_merge`
  (padrão do `teko_teko__runtime__intern_get` já na lista, linha 1846).

## How

1. `cov_atexit_dump()` (entry `void(void)` que o `atexit` registra) — lê o env em Teko e serializa:

```teko
/**
 * Coverage atexit entry: dumps the three sinks to the path named by the TEKO_TKCOV environment
 * variable, or does nothing when it is unset. Registered with atexit by the emitted test main;
 * replaces the C `__tk_cov_atexit_dump` shim so no `const char*` crosses the boundary.
 *
 * @since 0.3.1
 */
exp fn cov_atexit_dump() {
    var p = cov_env_path()
    if p.len == 0 { return }
    cov_dump_s(p)
}
```

   `cov_env_path(): str` privado = leitura de `TEKO_TKCOV` pelo overlay de env program-resident (mesma
   fonte de `teko::env`, mas o parse mínimo mora AQUI para não quebrar o fecho D121: caminha
   `teko::runtime::environ_slot()` procurando a chave, retorna o valor ou `""`). `cov_atexit_dump_to(dir: str, idx: u64)`
   idem, montando `<dir>/<idx>.tkcov` por `teko::str::concat` (analyze).

2. `cov_dump_s(path: str)` — grava `TKCOV1` byte-idêntico ao C (teko_rt.c:4504-4529), sobre raw syscalls:

```teko
/**
 * Serializes the three coverage sinks (functions, branches, lines) to `path` in the TKCOV1 format:
 * an 8-byte magic then three (count:u64, ids:u64[count]) sections in host byte order. Migrates the
 * C `tk_cov_dump`; the line hash-set is compacted to its live ids first. Silent on open failure.
 *
 * @param path  the destination `.tkcov` file
 * @since 0.3.1
 */
exp fn cov_dump_s(path: str) {
    var fd = cov_open_wr(path)
    if fd < 0 { return }
    cov_write_magic(fd)
    var st = cov_state()
    cov_write_section(fd, teko::mem::load_u64(st + COV_FN_PTR), teko::mem::load_u64(st + COV_FN_N))
    cov_write_section(fd, teko::mem::load_u64(st + COV_BR_PTR), teko::mem::load_u64(st + COV_BR_N))
    cov_write_lines(fd, st)
    _ = teko::sys::abi::os_close_raw(fd to i32)
}
```

   - `cov_open_wr(path): i64` = `os_open_raw(cstr, O_WRONLY|O_CREAT|O_TRUNC, 0644)` (mesmas flags do
     `write_stream`); `cov_open_rd` = `O_RDONLY`.
   - `cov_write_magic(fd)`: escreve os 8 bytes `b"TKCOV1\0\0"` de um buffer de pilha via `os_write_raw`.
   - `cov_write_u64(fd, v)`: escreve 8 bytes host-order (grava `v` num slot com `store_u64`, `os_write_raw`
     8 bytes) — host-order idêntico ao `fwrite(&n, 8, 1)` do C (pai e filho são o mesmo build, D113).
   - `cov_write_section(fd, ptr, n)`: `cov_write_u64(fd, n)`; loop `i<n` escrevendo `load_u64(ptr+i*8)`
     (por bloco de 8 B — buffer ≤1024 B, D116: agrupa até 128 ids/write).
   - `cov_write_lines(fd, st)`: compacta os slots não-vazios da hash-set numa passada (conta `n`, escreve
     `n` + os ids vivos) — espelho de teko_rt.c:4517-4526, mas sem `malloc` temporário: escreve direto do
     `COV_LN_PTR` filtrando `id!=0`.

3. `cov_merge(path: str): bool` — lê `TKCOV1` e reinsere nos sinks (teko_rt.c:4543-4562):

```teko
/**
 * Merges the TKCOV1 dump at `path` into this task's coverage sinks: function ids via cov_mark,
 * branch ids via the branch dedup set, line ids via the raw line-set insert (bypassing the on-gate
 * and fn-stack packing). Migrates the C `tk_cov_merge`.
 *
 * @param path  the `.tkcov` file to merge
 * @return      true on a well-formed dump, false on a missing/short/bad-magic file
 * @since 0.3.1
 */
exp fn cov_merge(path: str): bool {
    var fd = cov_open_rd(path)
    if fd < 0 { return false }
    if !cov_check_magic(fd) { _ = teko::sys::abi::os_close_raw(fd to i32); return false }
    cov_merge_fns(fd)
    cov_merge_branches(fd)
    cov_merge_lines(fd)
    _ = teko::sys::abi::os_close_raw(fd to i32)
    true
}
```

   - `cov_read_u64(fd): u64|erro` lê 8 bytes (`os_read_raw` num slot, `load_u64`); short-read → falha.
   - `cov_read_section(fd, sink)`: lê `n`; loop lê cada id e reinsere pelo sink certo — fns via `cov_mark`,
     branches via `cov_dedup_add(...COV_BR...)`, lines via `cov_line_insert` (o insert-raw: bypassa gate/
     stack, espelho de `tk_line_insert_raw`, teko_rt.c:4495-4502 — reusa `cov_line_insert` do 0145 pois ele
     já dedup por id puro).
   - `cov_check_magic(fd)`: lê 8 bytes, compara com `b"TKCOV1\0\0"`.

4. codegen: aplicar as trocas de shim (Where). O `atexit` registra `teko_teko__runtime__cov_atexit_dump`
   (símbolo `void(void)` — mangling de fn Teko sem args/retorno casa a assinatura que `atexit` exige).
5. `coverage.tks:282`: dropar o `extern ... from "teko_rt"`, virar delegate Teko. Os call-sites
   (`project.tks`/`regression.tks`) não mudam.
6. lir: `takes_one_str_by_value` aponta o mangled de `cov_merge`.
7. **Byte-check TKCOV1 (obrigatório):** o SHADOW (Fixtures) grava com o gen2 novo e compara o `.tkcov`
   byte-a-byte com um dump do BASE (gen1) do mesmo programa/execução — magic + 3 seções + host-order
   idênticos. Divergência = bug de serialização.

## Rulings & laws

- **D113 item (4) (DECISION_LOG:1079):** "dump/merge em Teko sobre teko::io/fs (formato TKCOV1, u64
   host-order) + reescrita do shim emitido __tk_cov_atexit_dump". Recuperado FIEL — realizado sobre
   `teko::sys::abi::os_*_raw` (as MESMAS primitivas raw que `teko::io`/`fs` embrulham) EM VEZ do módulo
   `teko::io`, para preservar o fecho do prelúdio D121 (runtime não pode importar stdlib) e a
   disponibilidade no filho sem wiring de build. Mesmo I/O, camada escolhida no runtime.
- **D47 (DECISION_LOG:478-480):** `cov_dump_s(path: str)` é lei-primeira, str-typed, zero C residual.
- **D120 (zero honest-stops):** o env-read do shim vira Teko (`cov_env_path`) — nada de `const char*`
   atravessando a fronteira, nada de stop; converte TUDO.
- **D116 (buffer ≤1024 B):** dump/merge escrevem/leem em blocos pequenos reusáveis (≤128 ids/write), sem
   acumulador que cresce; sem materializar o arquivo inteiro.
- **D90:** `tk_cov_dump`/`tk_cov_merge`/`tk_cov_read_section`/`tk_cov_write_section` = C MORTO após este
   crumb (com os 16 sinks do 0146 → TODA a família `tk_cov_*` morta, deletada no F9); `teko_rt.c` intocado;
   ZERO novo `from "teko_rt"` (na verdade REMOVE o último de coverage).
- **D122 (portabilidade):** `os_*_raw` já é o seam cross-plataforma (Linux/mac/win) que `file_stream.tks`
   usa — dump/merge herdam a portabilidade; host-order é seguro (pai e filho = mesmo build, D113).
- **NO PUSHES:** dump compacta a hash-set numa passada de tamanho conhecido (`COV_LN_N`); merge reinsere
   por id; nenhum `list::push`.
- **Teko-only / W15:** `.tks` só; doc só nos `exp`; sem `//`; flatten.
- **Não-detectar-o-inexistente:** só reescreve emissão/serialização real.
- **Fork protocol:** mapa D113 + D47 são a deliberação; a escolha `os_*_raw`-em-runtime resolve a tensão
   D113-literal("teko::io") × D121(fecho) POR LEI (fecho ganha, mesma primitiva) — sem fork aberto.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do `bootstrap/teko.c`;
   commit por passo; **sweep `.tkt`/`.tkr`** (mudou emissão + assinatura de `coverage::cov_merge`); fixpoint
   `gen2==gen3` byte-idêntico; **reseed `bootstrap/teko.c`** (harvest do cluster); gen2/gen3 no scratchpad;
   reportar pico.

## Fixtures

`none versionada` — validação FUNCIONAL por **SHADOW no scratchpad (D117)**, NÃO fixture. Projeto Teko
avulso (fora do repo, não-versionado) em
`/tmp/claude-.../scratchpad/cov-shadow/`:
- `main.tks` com ~3 fns de produção e 1 fn de teste que exercita: um branch tomado num só ramo (deixa o
  outro descoberto), N linhas executadas, 1 fn não-chamada (fica descoberta).
- Compilar com `-coverage` pelo gen2 NOVO → RODAR standalone → gera `$TEKO_TKCOV=out.tkcov`.
- Conferir: (a) `out.tkcov` começa com `TKCOV1\0\0`; (b) as 3 seções decodificam (count u64 + ids); (c)
   `cov_distinct` == nº de fns chamadas; (d) o branch descoberto NÃO aparece / o tomado aparece; (e) merge
   de dois `.tkcov` (dois shards) soma distintos sem duplicar.
- **Byte-idêntico vs BASE:** o MESMO `main.tks` compilado+rodado pelo gen1 (base, `tk_cov_*` C) produz
   um `.tkcov`; `cmp` com o do gen2 novo = idêntico (prova a serialização). Espelha o método do 0134/D121.

| fixture | asserts | expected |
|---|---|---|
| (shadow, não-versionado) | TKCOV1 do gen2 == TKCOV1 do gen1 base; counts/branches/lines corretos | byte-idêntico + counts esperados |

## Gate

`[fixpoint]` — build gen2 + `gen2.c==gen3.c` byte-idêntico (dump/merge Teko + delegate + shims reproduzem-se)
+ regressão verde + **SHADOW verde** (TKCOV1 byte-idêntico ao base, counts corretos). reseed-class
`fixpoint-rebuild` — **COLHE o reseed do cluster** 0145+0146+0147. Verde = compila, fixpoint estável,
shadow prova a corretura runtime, pico reportado NÃO-CRESCE vs baseline.

## Deps

0146

## Done when

`grep tk_cov_ src/` = 0 sítios VIVOS (só defs C mortas em `teko_rt.c`, deletadas no F9); `coverage.tks` sem
`from "teko_rt"`; o shadow produz `TKCOV1` byte-idêntico ao base com counts/branches/lines corretos;
fixpoint gen2==gen3 e `bootstrap/teko.c` reseedado.
