# I/O streaming em Teko — projeto (0.3.1)

Decreto do dono (2026-08-19): a camada de I/O do compilador deixa de **materializar**
arquivos inteiros na RAM. Duas formas — TOTAL (mantida) e STREAM (por chunk ≤ 1024 B,
buffer reusável). Escrita no ponto da construção, em ordem, sem acumular. O compilador
passa a usar **estritamente** a forma STREAM: saída → stream (append-only de preferência),
leitura → stream read-only. Tudo em Teko sobre syscalls, **sem** `from "teko_rt"`.

Causa-raiz: `teko::io::read_file`/`write_file`/`write_file_bytes` são FFI (`tk_rt_*`) que
leem/gravam TUDO de uma vez — o `teko.c` de 22 MB inteiro na RAM antes do `write` (ver
`src/runtime/teko_rt.c:3641` `tk_rt_read_file`, `:3753` `tk_rt_write_file`: `fseek`+`ftell`
dimensiona, `tk_alloc(n)` materializa, `fread`/`fwrite` de uma vez). O `Buf` de
`src/io/stream.tks:33` é acumulador `teko::list::push` (copy-grow) — não é stream.

Co-dependente com o **arena** (`docs/design/arena-em-teko.md`): a arena não segura os
buffers materializados; o streaming ≤ 1024 B sim. As duas cargas fecham juntas na ponta
do codegen (§9).

---

## 1. A máquina já existe — o que este projeto reusa

Nada de novo runtime em C. As costuras P1/P2 do arena já estão na superfície do compilador
(`src/checker/scope.tks::builtin_fn`) e provadas por `src/runtime/{arena,thread,sync}.tks`:

| primitiva (builtin) | forma | papel no streaming |
|---|---|---|
| `teko::sys::syscall0..6(n, a…): i64` | `scope.tks:509-515` | open/read/write/lseek/close/statx no Linux |
| `teko::sys::ptr_word(p: ptr): i64` | `scope.tks:521` | endereço de um `ptr<byte>` para a syscall |
| `teko::sys::word_ptr(w: i64): ptr` | `scope.tks:525` | reconstruir `ptr` de um endereço |
| `teko::mem::buf_ptr(n: u64): ptr<byte>` | `scope.tks:399` | **o buffer scratch de 1024 B na arena** |
| `teko::mem::bytes_from_ptr(p, n): []byte` | `scope.tks:600` | materializar o chunk lido (cópia de n bytes) |
| `teko::mem::load_u64/store_u64` | `scope.tks:605-606` | escrever args em blocos (statx, Windows) |

Padrão por-SO já estabelecido (`src/runtime/thread.tks`, `sync.tks`): Linux = `syscallN`
com `teko::sys::SYS_*`; macOS = `extern fn … from "System"`; Windows = `extern fn …
from "kernel32"` — selecionados por `#os(...)`. Constantes em `src/sys/sys.tks`.

**Um único builtin novo é necessário** — ver §4 (endereço-base de um `[]byte` para
zero-cópia). É irmão de `buf_ptr` e do addr-of que `lower_addr_of_place`
(`src/lir/lower.tks:1376`) já emite; compartilha a máquina que o arena já aterrissou.

---

## 2. Superfície `exp` — a API pública

Módulo `teko::io` (streams) e `teko::fs` (info de arquivo/dir). Só `exp` no que é valor
para o usuário; wrappers `#os` internos ficam `pub`/privados sem doc.

### 2.1 Constante e modos

```teko
/** O teto de um chunk de streaming: 1024 bytes, buffer pequeno e reusável, sem acumulador
 * que cresce. Todo read/write de stream opera em fatias de no máximo este tamanho. */
exp const CHUNK: u64 = 1024
```

### 2.2 Info de arquivo/diretório (stat) — o que o leitor precisa

```teko
/** FileInfo — o essencial de um path para planejar reads sem estouro: o tamanho exato em
 * bytes e se é diretório. `size` é o total; o leitor divide em `ceil(size / CHUNK)` reads e
 * o último traz `size % CHUNK` bytes — nunca padding de zeros. */
exp type FileInfo = struct {
    /** O tamanho total do arquivo em bytes. */
    size: u64
    /** Verdadeiro quando o path é um diretório. */
    is_dir: bool
}

/** stat — a info de `path` sem abrir um stream de dados.
 * @param path  o caminho a inspecionar
 * @return a info, ou um erro quando o path não pode ser consultado */
exp fn stat(path: str): FileInfo | error

/** file_size — o tamanho exato de `path` em bytes, para dimensionar o leitor.
 * @param path  o caminho a medir
 * @return o total em bytes, ou um erro quando o path não pode ser consultado */
exp fn file_size(path: str): u64 | error
```

### 2.3 Primitivas cruas — o dev controla abertura, buffer e fechamento

```teko
/** FileStream — um handle de arquivo do SO aberto: um fd (Linux/macOS) ou HANDLE (Windows)
 * carregado como `i64`, mais o cursor lógico. O dev que quiser controlar abertura,
 * fechamento e o buffer que usa opera estas primitivas diretamente. */
exp type FileStream = struct {
    /** O fd POSIX ou HANDLE do Windows, carregado como palavra. */
    handle: i64
}

/** open_read — abre `path` somente-leitura, cursor no início.
 * @param path  o caminho a ler
 * @return o stream aberto, ou um erro quando o arquivo não pode ser aberto */
exp fn open_read(path: str): FileStream | error

/** open_write — abre `path` para escrita, truncando (cria quando ausente), cursor no início.
 * @param path  o caminho a (re)escrever
 * @return o stream aberto, ou um erro quando o arquivo não pode ser aberto */
exp fn open_write(path: str): FileStream | error

/** open_append — abre `path` em modo append-only (cria quando ausente): toda escrita vai
 * para o fim do arquivo, sem posição.
 * @param path  o caminho a apendar
 * @return o stream aberto, ou um erro quando o arquivo não pode ser aberto */
exp fn open_append(path: str): FileStream | error

/** stream_read — lê até `into.len` bytes (no máximo CHUNK) do cursor para `into`, avançando
 * o cursor; devolve a contagem lida (0 = fim de arquivo). O chamador usa `into[0..count]` —
 * o retorno é a fronteira exata, então o último chunk parcial nunca carrega zeros de padding.
 * @param s     o stream a ler (o cursor avança)
 * @param into  o buffer reusável de destino, escrito no lugar
 * @return os bytes lidos (0 no EOF), ou um erro de leitura */
exp fn stream_read(ref s: FileStream, into: ref []byte): u64 | error

/** stream_write — grava `data` no stream, fatiando internamente em pedaços de no máximo
 * CHUNK bytes e apendando cada um em ordem. Nada acumula; opera o ponteiro-base de `data`
 * (R do `ref []T`: sem cópia da carga).
 * @param s     o stream a escrever (o cursor avança)
 * @param data  os bytes a gravar, na ordem construída
 * @return o total de bytes gravados, ou um erro de escrita */
exp fn stream_write(ref s: FileStream, data: ref []byte): u64 | error

/** stream_seek — reposiciona o cursor: `off` relativo a Start / Current / End. Um stream
 * append-only ignora o cursor na escrita (o SO força o fim), mas seek+read ainda posicionam
 * a leitura.
 * @param s       o stream a reposicionar
 * @param off     o deslocamento (pode ser negativo com Current/End)
 * @param whence  a origem do deslocamento
 * @return a nova posição absoluta, ou um erro em alvo negativo */
exp fn stream_seek(ref s: FileStream, off: i64, whence: Whence): u64 | error

/** stream_close — fecha o handle e libera o recurso do SO.
 * @param s  o stream a fechar
 * @return null em sucesso, ou o erro de fechamento */
exp fn stream_close(ref s: FileStream): error | null
```

`Whence` (Start/Current/End) já existe em `src/io/stream.tks:2` — reusado.

### 2.4 Helper de writer — o caminho fácil (abre + defer-close + escreve)

```teko
/** write_stream — o caminho recomendado de escrita: abre `path` truncando, grava `data`
 * fatiado em CHUNK, e fecha com defer (mesmo em erro). Onde se constrói a saída é onde se
 * grava, em ordem, sem acumular.
 * @param path  o caminho de destino
 * @param data  os bytes a gravar
 * @return null em sucesso, ou o primeiro erro */
exp fn write_stream(path: str, data: ref []byte): error | null

/** append_stream — como write_stream, mas apenda ao fim de `path` (cria quando ausente).
 * @param path  o caminho de destino
 * @param data  os bytes a apendar
 * @return null em sucesso, ou o primeiro erro */
exp fn append_stream(path: str, data: ref []byte): error | null

/** read_stream — o caminho fácil de leitura read-only: mede `path` por stat, aloca UM buffer
 * do tamanho exato, drena em passos de CHUNK e fecha com defer. Materializa (o lexer precisa
 * do texto inteiro), mas sem o hop FFI e com dimensionamento exato — sem crescer.
 * @param path  o caminho a ler
 * @return os bytes do arquivo, ou o primeiro erro */
exp fn read_stream(path: str): []byte | error
```

### 2.5 As duas formas TOTAL — mantidas como opção, reimplementadas sobre stream

`teko::io::read_file`/`write_file`/`write_file_bytes` **permanecem** na superfície (forma
TOTAL, `str`↔`[]byte`), mas o corpo deixa de ser `extern … from "teko_rt"` e passa a chamar
`read_stream`/`write_stream`. Assinatura idêntica → nenhum chamador muda; o FFI morre.

```teko
/** teko::io::read_file — lê `path` como texto UTF-8 (forma TOTAL, sobre read_stream).
 * @param path  o caminho a ler
 * @return o conteúdo, ou um erro */
exp fn read_file(path: str): str | error

/** teko::io::write_file — (re)escreve `path` com `content` (forma TOTAL, sobre write_stream).
 * @param path     o caminho de destino
 * @param content  o texto a gravar
 * @return null em sucesso, ou o erro */
exp fn write_file(path: str, content: str): error | null

/** teko::io::write_file_bytes — grava bytes crus em `path` (forma TOTAL, sobre write_stream).
 * @param path  o caminho de destino
 * @param data  os bytes a gravar
 * @return null em sucesso, ou o erro */
exp fn write_file_bytes(path: str, data: []byte): error | null

/** teko::io::append_file — apenda `content` a `path` (forma TOTAL, sobre append_stream).
 * @param path     o caminho de destino
 * @param content  o texto a apendar
 * @return null em sucesso, ou o erro */
exp fn append_file(path: str, content: str): error | null
```

---

## 3. Syscalls por SO

O compilador é monólito e cross-compila: cada `#os` emite seu ramo, e o backend C junta
tudo num `teko.c` por `#if`. Adições em `src/sys/sys.tks` (constantes) + wrappers `#os` no
novo `src/io/file_stream.tks`.

### 3.1 Linux — `teko::sys::syscallN` (adicionar em `sys.tks`)

| const | x86_64 | arm64 | nota |
|---|---:|---:|---|
| `SYS_READ` | 0 | 63 | |
| `SYS_WRITE` | 1 | 64 | **já existe** |
| `SYS_CLOSE` | 3 | 57 | |
| `SYS_LSEEK` | 8 | 62 | |
| `SYS_OPENAT` | 257 | 56 | arm64 não tem `open`; usar `openat(AT_FDCWD, …)` nos dois |
| `SYS_STATX` | 332 | 291 | layout de struct **uniforme entre arches** — preferir a `fstat` |
| `AT_FDCWD` | −100 | −100 | |
| `O_RDONLY` | 0 | 0 | |
| `O_WRONLY` | 1 | 1 | |
| `O_CREAT` | 0o100 | 0o100 | |
| `O_TRUNC` | 0o1000 | 0o1000 | |
| `O_APPEND` | 0o2000 | 0o2000 | |
| `SEEK_SET/CUR/END` | 0/1/2 | 0/1/2 | |
| modo de criação | 0o644 | 0o644 | |

Erro de syscall = retorno em `[-4095, -1]` (mesmo teste de `thr_mmap`,
`src/runtime/thread.tks:16`).

### 3.2 macOS — `extern fn … from "System"`

`open`, `read`, `write`, `lseek`, `close`. Flags BSD: `O_RDONLY=0`, `O_WRONLY=1`,
`O_CREAT=0x0200`, `O_TRUNC=0x0400`, `O_APPEND=0x0008`. Padrão idêntico ao
`os_mmap`/`os_mprotect` já usados (`arena.tks:130`, `thread.tks:5`).

### 3.3 Windows — `extern fn … from "kernel32"`

`CreateFileW`, `ReadFile`, `WriteFile`, `SetFilePointerEx`, `GetFileSizeEx`, `CloseHandle`.
Args por bloco via `store_u64`/`buf_ptr` (padrão do `VirtualProtect`, `thread.tks:46`).
**`CreateFileW` exige UTF-16** — o path `str` (UTF-8) precisa de widening (§11, risco).

### 3.4 Tamanho do arquivo — sem parsear struct stat

Para `file_size`/`FileInfo.size`, a rota portátil e sem layout-de-struct é
**`lseek(fd, 0, SEEK_END)` + `lseek(fd, 0, SEEK_SET)`** (POSIX) / `GetFileSizeEx` (Windows).
Vale nas três plataformas sem depender do layout de `struct stat` (que difere Linux×macOS e
x86_64×arm64). `is_dir` (raro, só para info de diretório) usa `statx`/`fstat` em Linux/macOS
e `GetFileAttributesW` no Windows — isolado, fora do caminho quente. Decisão em §11.

---

## 4. O idioma zero-cópia — writer recebe `ref []byte` + chunk 1024

O buffer de leitura é UM scratch de 1024 B da arena, reusado a cada volta:

```teko
var scratch: []byte = teko::mem::bytes_from_ptr(teko::mem::buf_ptr(CHUNK), CHUNK)
```

`stream_read` escreve os bytes lidos **dentro** de `scratch` no lugar (R do `ref []T`:
ponteiro-de-posição) e devolve `count`; o chamador consome `scratch[0..count]`. Zero
crescimento, zero acumulador.

`stream_write` recebe `data: ref []byte` e fatia por índice sem copiar a carga:

```
var off: u64 = 0
loop {
    if off >= data.len { break }
    var take = data.len - off
    if take > CHUNK { take = CHUNK }
    var base = teko::sys::ptr_word(teko::mem::byte_ptr(data, off))   // &data[off], sem cópia
    var n = os_write(s.handle, base to u64, take)                    // syscall(SYS_WRITE, …)
    ...
    off = off + n
}
```

**O único builtin novo:** `teko::mem::byte_ptr(xs: []byte, i: u64): ptr<byte>` = `&xs[i]`, o
endereço-base de um elemento sem materializar. Irmão de `buf_ptr`; lowera pela mesma
máquina de endereço-de-índice que `lower_addr_of_place` (`src/lir/lower.tks:1376`) já emite
para `ref x[i]`. Custo: uma entrada em `builtin_fn` + um braço em `lower.tks`. É a mesma
classe de mudança que P1 do arena — e se P1 já aterrissou, isto é uma linha-irmã.

*Alternativa sem builtin novo (fallback):* `as_ptr` sobre o reinterpret `str`↔`[]byte` (mesma
rep `{ptr,len}`, sem cópia — CLAUDE.md, cast implícito do expurgo): `as_ptr(str_of_bytes(
data))` daria a base. Depende do cast implícito ter aterrissado. `byte_ptr` é mais honesto e
não amarra os dois eixos. Decisão em §11.

---

## 5. Convivência com `src/io/*` e `src/fs/*`

| arquivo/decl | destino |
|---|---|
| `src/io/stream.tks` `Buf` (struct acumulador, `push`/`extend`/`concat` via `list::push`) | **SAI** — é copy-grow, o oposto do decreto. Substituído por `[]byte` cru + buffer 1024 |
| `stream.tks` `MemWriter`/`BufWriter`/`read_all`/`copy` (acumulam) | **SAI** — acumuladores. `read_stream` dimensiona exato; sem `read_all` |
| `stream.tks` `Reader`/`Writer`/`Seeker`/`Closer`/`Whence` (interfaces) | **FICA** `Whence`; interfaces re-expressas sobre `[]byte`/`ref []byte` (sem `Buf`) |
| `stream.tks` `MemReader`/`LimitReader`/`BufReader`/`NullStream`/`TeeWriter` | reavaliar: úteis para I/O em memória, mas re-basear em `[]byte`; fora do caminho do compilador |
| `src/io/file_stream.tks` `FileReader` (slurpa via `read_file`) / `file_flush` | **SAI** — materializa; substituído pelas primitivas de §2.3 + helpers de §2.4 |
| `src/io/io.tks` `read_file`/`write_file`/`write_file_bytes`/`append_file` | **corpo troca** FFI→stream; assinatura intacta (§2.5) |
| `src/io/io.tks` `read_line`/`stdin_eof`/`read_stdin`/`print`/`println`/`write`/`flush` | **FICA FFI** por ora — stdin/stdout não são arquivos seekable (§11, fora de escopo) |
| `src/fs/fs.tks` `list_dir`/`mkdir`/`remove_file` | **FICA FFI** por ora; `stat`/`file_size`/`FileInfo` novos em `teko::fs` |
| `src/io/compress_stream.tks` | reavaliar após `Buf` sair (consome `Buf`) |

---

## 6. Sítios do compilador a migrar (`teko::io::*` → stream)

Contagem no `origin/fix/retirement` (fora de `_test`/`src/io`/`src/fs`):

### Escrita (saída → stream, append-only de preferência)

| arquivo | `write_file` | `write_file_bytes` | maior/crítico |
|---|---:|---:|---|
| `src/build/project.tks` | 14 | 4 | **`teko.c` (`:1490`,`:2832`,`:3232`,`:3740`,`:3941`,`:4541`)** — o de 22 MB; `.tkh` (`:2650`); `.o`/`.tkl`/`.a`/wasm (`:2130`,`:2350`,`:2352`,`:2399`,`:1460`); `.tsym`, `.plist`, cobertura |
| `src/build/regr_group.tks` | 6 | 0 | projetos de regressão sintéticos |
| `src/build/regression.tks` | 6 | 0 | fixtures `.out`/`.err`/stdin |
| `src/build/init.tks` | 3 | 0 | `scaffold` de projeto novo |
| `src/fmt/fmt.tks` | 1 | 0 | reescrita in-place do fonte formatado (`:788`) |
| **total** | **30** | **4** | |

### Leitura (leitura → stream read-only)

| arquivo | `read_file` | maior/crítico |
|---|---:|---|
| `src/build/project.tks` | 14 | `.tkl` binário (`:169`), manifesto, `teko_rt.h`, deps |
| `src/build/regression.tks` | 9 | fixtures `.out`/`.err`/`.rc`, fontes `.tkr` |
| `src/build/init.tks` | 3 | probes de existência de manifesto |
| `src/build/assemble.tks` | 2 | **fonte de cada `SourceFile` (`:228`)** — o caminho de leitura do compilador |
| `src/build/regr_group.tks` | 2 | fontes de grupo |
| `src/fmt/fmt.tks` | 1 | fonte a formatar (`:775`) |
| **total** | **31** | |

O checker também **injeta** `read_file`/`write_file`/`write_file_bytes` como host-primitives
(`src/checker/scope.tks:473-476`) e o codegen as lowera (`src/codegen/codegen.tks:3642-3645`,
`emit_host_ffi`, `ures_byteslice` `:5522`). Como a assinatura TOTAL fica idêntica (§2.5), o
checker/codegen **não mudam** — só o corpo em `io.tks` deixa de ser `extern`. (Se o corpo em
Teko tornar a injeção de host-primitive redundante, removê-la é limpeza REPORTADA, não parte
deste issue.)

**Migração dos sítios = trocar a chamada TOTAL pela forma STREAM** onde a saída é construída
em ordem (o `teko.c` do codegen, o `.tkh`), e read-only nos reads. A maioria dos sítios menores
(`.tsym`, `.plist`, fixtures) já é pequena e pode ficar na forma TOTAL sobre stream (§2.5) sem
prejuízo — o decreto "estritamente stream" é satisfeito porque a forma TOTAL agora É stream por
baixo. O ganho de memória real está no **codegen emitir direto no writer** (§9).

---

## 7. Crumb sequence

Cada crumb é o menor passo seguro, gate-ável isolado. Gate seco = compilação
(`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`, `ulimit -v 6815744`) +
fixpoint (gen2==gen3). Rituais (gate cheio) marcados **[RITUAL]**.

1. **Constantes de syscall** em `src/sys/sys.tks`: `SYS_READ/CLOSE/LSEEK/OPENAT/STATX`
   (x86_64+arm64), `AT_FDCWD`, `O_RDONLY/WRONLY/CREAT/TRUNC/APPEND`, `SEEK_SET/CUR/END`,
   modo 0o644; macOS `O_*` BSD; Windows nada (usa flags de API). Folha, sem reseed.
2. **Builtin `teko::mem::byte_ptr`**: entrada em `builtin_fn` (`scope.tks`, ao lado de
   `buf_ptr`, `:607`) + braço em `src/lir/lower.tks` (reusa `lower_addr_of_place`) + emissão
   no codegen. **[RITUAL]** — muda superfície do compilador, exige reseed. *(Bloqueia se P1 do
   arena ainda não aterrissou o addr-of de índice; ver §10.)*
3. **`src/io/file_stream.tks` reescrito**: wrappers `#os` `os_open/os_read/os_write/os_lseek/
   os_close/os_size` (Linux syscall, macOS/Windows extern) + `FileStream`, `open_read/write/
   append`, `stream_read/write/seek/close`. Folha nova (não referenciada ainda), compila
   isolado.
4. **stat**: `teko::fs::FileInfo`, `stat`, `file_size` (via `lseek`-to-end / `GetFileSizeEx`).
   Folha.
5. **Helpers**: `write_stream`, `append_stream`, `read_stream` (open + defer-close + loop
   1024). Folha; testável por um `.tkr` de round-trip (§8).
6. **Formas TOTAL sobre stream**: reescrever o corpo de `read_file`/`write_file`/
   `write_file_bytes`/`append_file` em `src/io/io.tks` — de `extern … from "teko_rt"` para
   chamada aos helpers. Assinatura idêntica. **[RITUAL]** — muda o C emitido (some o edge FFI),
   reseed.
7. **Migrar reads do compilador** para `read_stream`/forma-TOTAL-sobre-stream: `assemble.tks`
   (fonte), `fmt.tks`, `project.tks`, `regression.tks`. Preservante. **[RITUAL]** (fixpoint).
8. **Migrar writes do compilador** para `write_stream`/`append_stream`: `project.tks`
   (`teko.c`, `.tkh`, artefatos), `fmt.tks`, `regr_group.tks`, `init.tks`. Byte-idêntico.
   **[RITUAL]** (fixpoint: `teko.c` gen2==gen3).
9. **Retirar o morto**: `Buf` acumulador, `MemWriter/BufWriter/read_all/copy`, `FileReader`/
   `file_flush`, deps `tk_rt_read_file/write_file/write_file_bytes` do `teko_rt.c/.h`. O
   compilador auto-compilando ENUMERA cada referência sobrevivente ao remover a raiz (metodologia
   do expurgo). **[RITUAL]** — reseed final do lote.

Ordem obrigatória: 1→2 (primitiva) → 3→4→5 (helper+stat, folhas) → 6 (formas TOTAL) →
7→8 (migração) → 9 (limpeza). Reseed só ao fim de cada crumb **[RITUAL]**, nunca no meio.

---

## 8. Fixtures de regressão (`.tkr`, rodados isolados — evitam o OOM do `teko test .`)

Casos que o self-build NÃO exercita (I/O de arquivo com caminhos de erro e fronteiras de
chunk), portanto merecem fixture:

| fixture | entrada | exit esperado |
|---|---|---|
| `io_stream_roundtrip` | `write_stream` de N bytes + `read_stream`, comparar igualdade | 0 |
| `io_stream_chunk_boundary` | gravar exatamente 1024, 1025, 2048, 2049 bytes; reler; comparar | 0 |
| `io_stream_last_partial` | arquivo de tamanho `k*1024 + r` (r≠0); afirmar último chunk = r bytes, sem zeros | 0 |
| `io_stream_append` | `append_stream` 3× ; ordem preservada byte-a-byte | 0 |
| `io_stream_seek_read` | `stream_seek(End,-4)` + read; conferir os 4 últimos bytes | 0 |
| `io_open_missing` | `open_read` de path inexistente → ramo `error` | exit próprio (ex. 40) |
| `io_write_readonly_dir` | `open_write` em dir sem permissão → `error` | exit próprio (ex. 41) |
| `io_file_size` | `file_size` de arquivo conhecido == tamanho esperado | 0 |

Prova de fixpoint (não é fixture, é ritual): após crumb 8, `teko.c` de gen2 e gen3
**byte-idêntico** — a migração write_file→write_stream grava exatamente os mesmos bytes na
mesma ordem, então a identidade tem de sobreviver.

---

## 9. Co-dependência com a arena — o que fecha junto

O ganho de memória verdadeiro NÃO é a troca de FFI por syscall (o `read_file` continua
materializando: o lexer precisa do texto inteiro). É o **codegen emitir direto no writer** em
vez de construir o `csrc` de 22 MB inteiro antes do `write`:

- Hoje: `codegen` constrói `csrc: str` (o `cb`/buffer que cresce, 93% do pico de memória —
  `tk_slice_push_r`), depois `write_file(cfile, csrc)`. Duas materializações.
- Fim-de-jogo: `codegen` recebe um `FileStream` (append-only) e emite cada peça
  (`outs[i] = [..b"#define …", ..sym, b'\n']`) direto por `stream_write`, em ordem, sem `cb`.

Essa segunda metade é o **eixo da arena** (`docs/design/arena-em-teko.md`) e do expurgo do
`cb`/`append_fo` — NÃO são crumbs deste issue. Este projeto entrega o **sink** (a camada de
§2) que o codegen vai consumir. O que precisa fechar JUNTO:

1. **`byte_ptr` (crumb 2) compartilha P1 do arena** (addr-of de índice, `lower_addr_of_place`).
   Se P1 aterrissou, `byte_ptr` é irmão de uma linha; se não, crumb 2 fica bloqueado (§10).
2. **O buffer scratch de 1024 B vive na arena** (`buf_ptr`) e deve ser estável durante o loop
   de read — nenhum `arena_pop` no meio da drenagem. A semântica de posse (purge-on-reassign,
   CLAUDE.md) NÃO afeta o scratch porque ele não é reatribuído; é escrito no lugar.
3. **A reescrita do codegen-emite-direto** só cabe depois de (a) esta camada existir e (b) o
   `cb`/`append_fo` sair — é a mesma volta `ENSINAR→SEED→SWEEP` do expurgo. Este doc é
   pré-requisito; a reescrita é carga da arena/expurgo, reportada como próximo passo.

---

## 10. Bloqueios (design-ahead) — o que resume em minutos quando a dep fechar

- **Crumb 2 (`byte_ptr`)** depende do addr-of de índice (P1 do arena) estar na LIR alcançável
  do fonte. Estado: `lower_addr_of_place`/`lower_addr_of_arg` existem (`src/lir/lower.tks:1371,
  1376`) e `ref x[i]` já lowera; falta expô-lo como builtin de valor `ptr<byte>`. Se P1 do
  arena já aterrissou os builtins `load_u64`/`store_u64`/`buf_ptr` (e aterrissou — `scope.tks:
  605-607`), então `byte_ptr` é aditivo e NÃO está bloqueado — pode entrar já.
- **Tudo o mais compila hoje**: as constantes (crumb 1), os wrappers `#os` e as primitivas
  (crumbs 3-5) usam só `syscallN`/`ptr_word`/`buf_ptr`/`bytes_from_ptr` já presentes. A camada
  §2 inteira é **folha nova** e pode ser escrita e gate-ada antes de qualquer migração.
- **Nada aqui está bloqueado pela arena-em-Teko não estar em `src/`**: as costuras que este
  projeto usa (`syscallN`, `buf_ptr`, `load/store_u64`) já estão na superfície do compilador e
  provadas por `thread.tks`/`sync.tks` — que ESTÃO em `src/runtime/`.

---

## 11. Riscos e decisões abertas

1. **Tamanho por `lseek`-to-end vs `statx`/`fstat`.** `lseek(0,END)`+`lseek(0,SET)` dá o
   tamanho sem parsear `struct stat` (layout difere Linux×macOS e x86_64×arm64) e vale nas três
   plataformas. `statx` (Linux, layout uniforme) dá também `is_dir`. **Recomendação
   (passa-nas-Leis, mais simples):** `file_size`/`FileInfo.size` por `lseek`-to-end
   (`GetFileSizeEx` no Windows); `is_dir` por `statx`/`GetFileAttributesW` isolado, só quando
   info de diretório for pedida. Menos superfície de struct = menos risco de layout errado.

2. **`byte_ptr` builtin vs `as_ptr` sobre reinterpret `str`↔`[]byte`.** **Recomendação:**
   `byte_ptr` dedicado — não amarra o eixo do cast-implícito-do-expurgo ao eixo do I/O, e é a
   mesma classe de P1 (uma instrução LIR já existente). O fallback `as_ptr(str_of_bytes(data))`
   fica registrado caso o cast implícito aterrisse antes e se queira zero builtin novo.

3. **Windows: widening de path UTF-8→UTF-16 para `CreateFileW`.** Genuína complicação — o
   POSIX toma o path como bytes; o Windows quer `wchar_t*`. Opções: (a) `MultiByteToWideChar`
   (kernel32) — mais uma extern, mas é a rota canônica; (b) conversão UTF-8→UTF-16 manual em
   Teko (o compilador já decodifica UTF-8 no lexer). **Recomendação:** (a) `MultiByteToWideChar`
   no ramo `#os("windows")`, isolado no wrapper `os_open` — não contamina Linux/macOS.
   *Não é tensão de Lei; é trabalho de plataforma. Não HALTa.*

4. **Seek em append-only.** POSIX `O_APPEND` força toda escrita ao fim (o cursor de escrita é
   ignorado pelo SO); mas `stream_seek` + `stream_read` ainda posicionam a LEITURA. **Decisão:**
   `stream_seek` num stream append-only reposiciona só a leitura; a escrita permanece no fim por
   contrato do SO. Documentado no doc-comment de `open_append`. Sem tensão.

Nenhuma decisão é tensão de Lei irresolúvel — todas resolvem law-first (menos superfície,
menos acoplamento de eixos). **Nada a HALTar.** As três recomendações acima são ratificáveis
como escritas; se o dono discordar de (1) ou (2), é troca de uma linha no crumb, não
re-desenho.
