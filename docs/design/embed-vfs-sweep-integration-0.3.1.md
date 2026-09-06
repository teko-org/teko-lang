# `#embed`/VFS + prelúdio-do-VFS + tipos-base + provenance + onda-intrínsecos, tecidos com o SWEEP do modelo de memória — anexo de fiação (0.3.1)

Arquiteto, 2026-08-27. Design-only. Supersede `embed-vfs.md` onde conflita (rulings D135/D134/dono-2026-08-27). Ancora os crumbs `0162-0174` + reordena/anota o sweep `0156-0161`+`0155`.

Base verificada contra `src/` (branch `design/embed-vfs-sweep-wire` de `origin/fix/retirement`). NÃO presume — cada citação foi conferida.

---

## 0. Sumário-executivo (o que este anexo fixa)

1. **Superfícies cravadas pelo dono (D135 + correções em voo):** `EmbedCompress`, `FileSystem` (classe, conforma interface read-only), `RoFile`, o accessor **`get(path): RoFile | error`** (NÃO `read(path): RoFile?`), `exists`, `list`; `RoFile.content: FileStream` read-only sobre fatia de rodata; **o DEV descomprime** (supersede ruling 7).
2. **Resolução Tier-A-interno = VIÁVEL, sem fork, sem #594 Tier-B.** Os quatro heaps são `[]byte`/escalar (Tier-A puro); o `FILES` é uma vista fina construída em runtime sobre os quatro consts Tier-A (zero-cópia), nunca um agregado ponteiro-bearing serializado em rodata. O `FileStream`-sobre-rodata é um cursor de memória (fatia `[off..off+len]` do blob) — extensão aditiva do `FileStream` atual.
3. **Ancoragem de path (2 formas):** nu (`"docs/x"`) = **file-relative** (dir do `.tks` corrente); `/` inicial (`"/docs/x"`) = **project-root** (onde está o `.tkp`). Chave final sempre normalizada pra `<project-name>::/path-relativo-à-raiz`; `<project-name>::` é **compiler-injetado**. `..`-escape e drive-absoluto Windows = erro (M.1).
4. **Prelúdio do VFS:** o prelúdio-pequeno (~13 arquivos runtime/sys/abi/assert) embarca no VFS; `inject_runtime_prelude` passa a ler da **memória do binário**, não do disco. Tipos-base (`str`/`[]byte`/`char`/`ptr`/`uptr`/`isize`/`usize`/`u8`…) vivem no prelúdio embarcado, injetados em **TODOS os artefatos incl. Package**.
5. **Provenance (D133):** tag base-injetado-vs-user na unidade/decl, derivada da injeção-de-prelúdio; gate no check de colisão-de-tipo pra barrar redefinição de nome reservado. Ortogonal ao `<project-name>::` (asset-namespacing).
6. **Onda intrínsecos→superfície (D134):** onda PRÓPRIA, não-tanglada no byte-mover de região; `wrap`/`unwrap` **já landados** (drift — ver §7).
7. **Ordenação:** #embed/VFS → prelúdio/tipos-base/provenance → intrínsecos → sweep-de-região (RESEED-FINAL). Pontos de reseed e de ratchet marcados por fase.

---

## 1. Superfícies exatas (conferidas contra o `src/`)

### 1.1 `EmbedCompress` (exp, D135 §1)

```teko
/**
 * EmbedCompress — the closed set of codecs an `#embed` directive may name. `None` stores bytes
 * verbatim; `Deflate` is raw DEFLATE (`teko::compress::deflate`); `Gzip` is the gzip container
 * (`teko::compress::gzip_compress`). The codec is applied AT BUILD TIME; the bytes sit compressed in
 * rodata and the VFS returns them RAW — the reader decompresses (owner D135, supersedes ruling 7).
 *
 * @since 0.3.1
 */
exp type EmbedCompress = enum { None; Deflate; Gzip }
```

### 1.2 O contrato read-only + `FileSystem` (classe)

`src/checker/resolve.tks:821` prova que **classe→interface** conforma como valor (upcast landado); `:909` mostra struct→interface honest-stopped (S1). Logo o `FileSystem` que "implementa interface read-only" é uma **CLASSE** (não struct) — casa com `interface-value-type.md §6.4` sem mecanismo novo.

```teko
/**
 * ReadableFS — the segregated read-only file-system contract (interface-value-type.md §6.4): a bodyless
 * interface a read-only VFS conforms to. `get` fetches a file HANDLE (not its content); `exists` is the
 * error-free presence companion; `list` enumerates. Passing a `FileSystem` where a `ReadableFS` is
 * expected up-casts to an interface value (class up-cast, resolve.tks:821) — no new mechanism.
 *
 * @since 0.3.1
 */
exp type ReadableFS = interface {
    /** get — the handle of the embedded file at `path`, or an error when absent. */
    fn get(self, path: str): RoFile | error
    /** exists — whether a file at `path` was embedded (no error branch). */
    fn exists(self, path: str): bool
    /** list — every embedded logical path, in emit order. */
    fn list(self): []str
}

/**
 * FileSystem — the process-global, read-only, compile-time-materialized VFS. Every `#embed` across the
 * whole program accumulates into ONE instance (`teko::embed::FILES`). Internally FLATTENED over four
 * Tier-A rodata consts (§3); this class is a thin cursor. EMPTY when no `#embed` exists (zero entries,
 * costing nothing). Conforms to `ReadableFS`.
 *
 * @since 0.3.1
 */
exp type FileSystem = class ReadableFS {
    /** The concatenated UTF-8 path heap (a zero-copy slice over the `EMBED_NAMES` Tier-A const). */
    names: []byte
    /** The concatenated (possibly compressed) content heap (a slice over `EMBED_BLOBS`). */
    blobs: []byte
    /** The packed fixed-width record table (a slice over `EMBED_TABLE`). */
    table: []byte
    /** The entry count. */
    count: u64

    /**
     * get — fetch the handle of the embedded file whose normalized key matches `path`. Builds a `RoFile`
     * whose `content` is a read-only `FileStream` over the blob slice `[off..off+len]` (zero copy into
     * rodata) and whose `compress` is the stored codec tag. Does NOT read or decompress content — the
     * caller drives `content` and decompresses per `compress`.
     *
     * @param path  the embedded logical path (bare = as embedded, or a `<project>::/...` qualified key)
     * @return      the file handle, or an error when no such path is embedded
     * @throws      `no such embedded file: "<path>"` when absent
     */
    pub override fn get(self, path: str): RoFile | error { /* §4 */ }

    /** exists — whether a file at `path` was embedded. O(count) over the packed table. */
    pub override fn exists(self, path: str): bool { /* §4 */ }

    /** list — every embedded logical path, in directive/emit order. */
    pub override fn list(self): []str { /* §4 */ }
}
```

### 1.3 `RoFile` (D135 §3) + `FileStream` read-only sobre rodata

```teko
/**
 * RoFile — an embedded-file handle: its logical name, its content as a read-only stream over the raw
 * (possibly compressed) rodata bytes, and the codec tag. The content is NOT decompressed — the caller
 * reads `content` (chunk-by-chunk, ≤1024 B) and, when `compress != None`, inflates with
 * `teko::compress::inflate` (Deflate) or `gzip_decompress` (Gzip). Nothing materializes the whole file.
 *
 * @since 0.3.1
 */
exp type RoFile = struct {
    name: str
    content: FileStream
    compress: EmbedCompress
}
```

**`FileStream` hoje (`src/io/file_stream.tks:20`) é fd-backed** (`handle: i64` + buffer de escrita ≤1024 B) e estende `Stream` (sink de ESCRITA: write/flush/close). Para ser cursor read-only sobre uma fatia de bytes, **estende-se `FileStream` aditivamente** com um backing de MEMÓRIA (owner cravou `content: FileStream` verbatim → estende o tipo, não cria outro):

```teko
    /** The in-memory backing slice for a read-only rodata cursor; empty for an fd/HANDLE stream. */
    mem: []byte
    /** The read cursor into `mem` (memory-backed) — advanced by `stream_read`. */
    rpos: u64
```

```teko
/**
 * of_slice — a read-only, memory-backed FileStream over the byte slice `data` (e.g. a slice into embedded
 * rodata). No OS handle is opened; `stream_read` pulls from `data` and advances an internal cursor. The
 * write path is unused on such a stream.
 *
 * @param data  the backing bytes (borrowed; a rodata slice is zero-copy)
 * @return      a fresh read-only stream positioned at 0
 * @since 0.3.1
 */
exp fn of_slice(data: []byte): FileStream
```

`stream_read` (`file_stream.tks:332`) ganha um ramo de memória ANTES do `os_read`:

```teko
exp fn stream_read(s: FileStream, into: []byte): u64 | error {
    var want = into.len
    if want > CHUNK { want = CHUNK }
    if want == 0 { return 0 to u64 }
    if s.handle < 0 {                       // memory-backed sentinel
        var left = s.mem.len - s.rpos
        if left == 0 { return 0 to u64 }
        var take = if want < left { want } else { left }
        var j: u64 = 0
        loop { if j >= take { break } into[j] = s.mem[s.rpos + j]; j = j + 1 }
        s.rpos = s.rpos + take
        return take
    }
    var n = os_read(s.handle, buf_base(into, 0), want to i64)
    if is_os_error(n) { return error { message = "teko::io::stream_read: read failed" } }
    n to u64
}
```

`of_slice` seta `handle = -1` (sentinela memory-backed), `mem = data`, `rpos = 0`. `FileStream::of_handle` inicializa `mem = []`, `rpos = 0`. Byte-preserving pra o caminho fd (sentinela `handle < 0` nunca ocorre num fd real). O SWEEP-de-região prova o cursor via `stream_read` no self-build (o compilador lê seus próprios fontes por stream).

### 1.4 `FILES` — a vista fina (a resolução Tier-A, §3)

`teko::embed::FILES` é obtido por uma vista fina construída em runtime sobre os quatro consts Tier-A — **não** um agregado ponteiro-bearing em rodata (que seria Tier-B). Ver §3 pra o mecanismo e §3.2 pra a única escolha veto-open (nome-const vs accessor).

---

## 2. Ancoragem de path — DUAS formas (correção do dono em voo)

`resolve_embed_path` recebe **o dir do `.tks` que carrega a diretiva** (`SourceFile.path`, `discover.tks:2`) + a **raiz do projeto** (dir do `.tkp`, `m.name`/`m.source` em `project.tks`).

| Forma escrita | Âncora | Exemplo (diretiva em `src/foo/bar.tks`, raiz `<R>`) | Chave normalizada |
|---|---|---|---|
| `"docs/x.ansi"` (nu, sem `/`) | **dir do arquivo-fonte** (file-relative) | `<R>/src/foo/docs/x.ansi` | `teko::/src/foo/docs/x.ansi` |
| `"/docs/x.ansi"` (`/` inicial) | **raiz do projeto** | `<R>/docs/x.ansi` | `teko::/docs/x.ansi` |

Regras M.1 (exclusão-por-construção):
- Resolve a âncora → caminho absoluto → **normaliza pra relativo-à-raiz**.
- Qualquer resultado que **escape a raiz** (via `..` que sobe além de `<R>`) = **erro**. `..` interno que não escapa é permitido (re-ancora, não escapa).
- **Drive-absoluto Windows** (`X:\`) = **erro** (não é âncora válida).
- Chave final = `<project-name>::` (compiler-injetado do manifesto) + `/` + caminho-relativo-à-raiz. Habilita o **futuro linker fazer JOIN de VFSs** por origem. Namespacing de ASSET — **ortogonal à provenance D133**.

### 2.1 Tabela de erros ATUALIZADA (supersede `embed-vfs.md §2.5`)

| Entrada | Resultado | Mensagem (estilo compilador) |
|---|---|---|
| `"../secret.txt"` (escapa a raiz) | erro | `#embed path escapes the project root: "../secret.txt"` |
| `"/etc/passwd"` | **OK, re-ancorado** = `<raiz>/etc/passwd` (in-project) — erro SÓ se não existir | (segue como leitura in-project; supersede ruling 2) |
| `"C:\\x"` | erro | `#embed path must not be drive-absolute: "C:\x"` |
| mesmo path por 2 diretivas | **PANIC / erro de compilação** | `#embed path conflict: "<key>" embedded by two directives` |
| `"f.bin", Deflate, 99` | erro (const-range) | `#embed compression level 99 out of range for Deflate (0..9)` |
| `"missing.txt"` | erro | `#embed: no such file in the project: "missing.txt"` |
| `"f", Bogus, 1` | erro (enum fechado) | `#embed compression type must be one of None, Deflate, Gzip` |
| runtime `FILES.get("x")` ausente | `error` | `no such embedded file: "x"` |

---

## 3. Resolução Tier-A-interno — VIÁVEL, sem #594 Tier-B (o ponto que o dono mandou resolver)

**Veredito: viável, sem fork.** O bloqueio Tier-B (`serialize_const` honest-stop, `lower.tks`) só morde um agregado **ponteiro-bearing** materializado como const de rodata. A DATA do VFS é toda Tier-A; o `FILES` evita o agregado-ponteiro.

### 3.1 A DATA = quatro consts Tier-A

O passe comptime de embed emite, no namespace `teko::embed`, quatro consts module-level:

| const | tipo | Tier |
|---|---|---|
| `EMBED_NAMES` | `[]byte` (todos os paths UTF-8 concatenados) | **A** (`[]byte` = fatia auto-contida em rodata) |
| `EMBED_BLOBS` | `[]byte` (todo conteúdo cru/comprimido concatenado) | **A** |
| `EMBED_TABLE` | `[]byte` (registro packed por entrada: `name_off,name_len,blob_off,blob_len,orig_len,comp_tag` — POD fixo) | **A** |
| `EMBED_COUNT` | `u64` (escalar) | **A** |

`[]byte`/`str`/POD-plano são Tier-A com **zero mudança de backend** (`embed-vfs.md §7.2`, plan §6.4). Um literal de string já vai pra rodata hoje (`lower.tks` `str_to_bytes`→`LRodata`, `find_const_rodata`) — um `const []byte` segue o mesmo caminho.

### 3.2 O `FILES` — vista fina em runtime (NÃO agregado-const)

Um `const FILES: FileSystem = FileSystem { names = EMBED_NAMES, ... }` seria um agregado com campos-fatia = **ponteiro-bearing = Tier-B → bloqueado**. Evita-se construindo a vista **em runtime** sobre os quatro consts Tier-A (zero-cópia — as fatias apontam pra rodata; o objeto-classe é boxeado na região do caller, não em rodata):

```teko
/**
 * FILES — the process-global read-only VFS. A thin `FileSystem` view built over the four Tier-A embed
 * consts (`EMBED_NAMES`/`EMBED_BLOBS`/`EMBED_TABLE`/`EMBED_COUNT`) — zero-copy (the slices point into
 * rodata). When no `#embed` exists the consts are empty and every accessor is a costless empty answer.
 *
 * @since 0.3.1
 */
exp fn files(): FileSystem {
    FileSystem { names = EMBED_NAMES; blobs = EMBED_BLOBS; table = EMBED_TABLE; count = EMBED_COUNT }
}
```

**Única escolha veto-open (estética, NÃO fork):** o dono escreveu `teko::embed::FILES` (um valor). Duas realizações dão a mesma ergonomia `FILES.get(...)`:
- **(recomendado)** `FILES` é um **const computado** que o compilador baixa pro construtor-fino-em-runtime acima (não serializa o agregado; materializa os quatro consts Tier-A e monta a vista no ponto de uso). Preserva o nome `FILES`.
- accessor `fn files()` explícito (o snippet acima).
Ambos são Tier-A. Recomendo o const-computado (preserva o nome cravado). Se o compilador NÃO conseguir baixar um `const` de classe pra construtor-em-runtime (i.e. exigir serializar o objeto → Tier-B), cai-se no accessor `fn files()` — **sem fork**, é troca de nome cosmética. NÃO há caminho que force #594 Tier-B.

### 3.3 Como o `FileStream`-sobre-rodata funciona (cursor em read-time)

`get(path)` acha a entrada em `EMBED_TABLE` (O(count)), lê `blob_off/blob_len`, faz a fatia `EMBED_BLOBS[blob_off..blob_off+blob_len]` (fat pointer `{ptr→rodata, len}`, zero-cópia), e constrói `RoFile.content = of_slice(fatia)`. O cursor (`rpos`) vive no `FileStream` construído na região do caller; a rodata é só lida. `stream_read` (§1.3) copia ≤1024 B por chamada de `mem[rpos..]` — buffer pequeno reusável, **nada materializa o arquivo inteiro** (honra o "file-stream read-only, não read que materializa" do dono). Descompressão fica FORA do VFS: o dev, tendo `content` + `compress`, chama `teko::compress::inflate`/`gzip_decompress` (supersede ruling 7; zero codec no caminho quente do VFS). **Promoção de superfície (coordenador 2026-08-27):** `gzip_*`/`zlib_*` já são `exp`; `deflate` (`deflate.tks:38`) e `inflate` (`inflate.tks:405`) são `pub` → **promover pra `exp`** (o dev precisa de `inflate` pra inflar um embed `Deflate`; `deflate` por simetria). Muda o `.tkh`/ABI → **dobra no reseed do EMB-C4** (const-mat), não reseed separado.

---

## 4. Onde o passe de embed se liga

Passe novo pós-parse, pré-lowering, ao lado de `collect`/`resolve` (`src/checker/`): varre `Module.decls` por `EmbedDecl`, roda `resolve_embed_path` (com o dir do arquivo-fonte + raiz), lê cada arquivo via a costura maintained-C `read_file_bytes` (exceção frozen-C, `teko_rt.{c,h}` — compile-time-only, sem rastro no programa emitido), comprime build-time por diretiva, checa conflito, e entrega os quatro heaps Tier-A à const-materialização. Roda UMA vez por build, no compilador — nunca no programa emitido (M.0).

---

## 5. Prelúdio-do-VFS + tipos-base + provenance

### 5.1 Prelúdio embarcado (o "resto" #8)

Hoje `inject_runtime_prelude` (`project.tks:368`) lê do **disco** via `rt_dir_tks_paths`→`teko::fs::list_dir` sobre `src/runtime|sys|sys/abi|assert` — **exige o dev ter o fonte da Teko** (defeito, D134/R2). Re-fia-se pra ler da **memória do binário** (o VFS embarcado). Só o **prelúdio-pequeno** (~13 arquivos runtime/sys/abi/assert) embarca; **NUNCA a stdlib** (enorme; "se fosse pra stdlib já estaria lá").

- **`inject_runtime_prelude`**: em vez de `list_dir`+`read_file` por caminho, itera as chaves `teko::/src/runtime/*.tks` etc. do VFS e usa `FILES.get(key)` + `stream_read` pro conteúdo.
- **`artifact_wants_runtime_prelude`** (`project.tks:358`): hoje só `Binary`/`Tool`. As **defs-de-tipo-base** (§5.2) injetam em **TODOS** incl. `Package`; a **impl-de-runtime** (arena/io/…) segue só `Binary`/`Tool` (Package não precisa do arena embarcado). Distinguir as duas classes de unidade-de-prelúdio.

### 5.2 Tipos-base no prelúdio (o "resto" #9)

`str`/`[]byte`/`char`/`ptr`/`uptr`/`isize`/`usize`/`u8`… são **defs-de-superfície universais**, consolidadas em UMA unidade do prelúdio embarcado (base do compilador), injetada em todo artefato. `ptr`/`uptr` já existem em `src/sys/marshall.tks` (`exp global type ptr = isize {}` / `uptr = usize {}`). Consolidar as reservadas nessa unidade-base é o custo que a F3-provenance puxa (D133) — **só** as defs-base reservadas migram pro prelúdio agora; o retro-feed completo do `.tkh` (visão-futura D131) fica pós-modelo.

### 5.3 Provenance (o "resto" #10, D133)

Cada unidade/decl carrega a **origem**: `Base` (prelúdio-injetado) vs `User` (fonte-de-usuário), derivada em `inject_runtime_prelude` (a unidade injetada recebe `Base`; o resto `User`). O gate no check de colisão de tipo existente:
- `src/checker/collect.tks:1354` `check_no_duplicate_types`
- `src/checker/check_modules.tks:227` `global_type_collision_at`

Regra: um decl `User` cujo nome ∈ {reservados-por-keyword} **colide com** a def `Base` → **erro** (`user program cannot redefine reserved type "<name>"`); a origem `Base` define-os legitimamente uma vez. É **ortogonal ao `<project-name>::`** do VFS (asset-namespacing, não base-vs-user).

---

## 6. Onda intrínsecos→superfície (o "resto" #11, D134) — onda PRÓPRIA

NÃO-tanglada no byte-mover de região. Censo (D134): codegen 90 nomes/92 sítios; lower 32; overlap zero. Três naturezas:
1. **`tk_*` (dep-C)** → expurga pra Teko/superfície.
2. **C-inline (magia-de-nome:** `floor`/`memcpy`/`__atomic_*`/reinterpret**)** → primitiva de superfície.
3. **`syscall`/raw-emit** → o chão irredutível do SO vira **UMA primitiva raw de superfície** (não some — já é superfície).

`wrap`/`unwrap` = superfície, **já landados** (§7). Cada crumb bisectável, cada um **baixando o ratchet** (natureza-1/2 removem C → pico não sobe; a onda é redução/não-crescer). Assento permanente = `lower.tks` (nativo); codegen-C é muleta (F9 finaliza 100%-nativo).

---

## 7. DRIFT achado entre docs/crumbs e o `src/`

1. **`wrap`/`unwrap` JÁ landados como intrínsecos** (`typer.tks:888` `type_ptr_unwrap` `__unwrap`→`teko::__ptr_unwrap`; `:928` `type_ptr_wrap` `__wrap`→`teko::__ptr_wrap`). D132-escalação-1 e `0161-MEM-W6` dizem "a maquinaria wrap/unwrap-intrínseco fica pro W6" — **desatualizado**: a MAQUINARIA está feita; W6 e a onda-intrínsecos só fazem o **USO em massa** (reball). Corrigir a expectativa dos crumbs (W6 é reball puro, não ensina wrap/unwrap).
2. **`ptr`/`uptr` já são newtypes de superfície** (`marshall.tks:8,16`) — o ADENDO D130 "verificar se newtype-sobre-escalar-com-métodos existe" está RESOLVIDO (existe; `NewtypeBody` landado, D131-ctx).
3. **`Stream` é sink de ESCRITA** (write/flush/close, `file_stream.tks:6`); **não há contrato de leitura**. O "FileSystem implementa interface read-only" NÃO reusa `Stream` — precisa da interface nova `ReadableFS` (§1.2). `RoFile.content` lê por `stream_read` (fn livre), não por método de `Stream`.
4. **`FileStream` é fd-backed** — o "FileStream sobre fatia de rodata" do dono exige a extensão memory-backing (§1.3), aditiva.
5. **`0156-MEM-W1` deps `[MEM-S1]`** vs **D132 "SHADOW (0155) depois do sweep"**: reconciliação — o SCAFFOLD das fixtures `mem_*` (0155) vem ANTES (cada W crumb gateia numa fixture `mem_*`), mas a PASSAGEM (reclaim/pico-plano) só é satisfeita progressivamente W1→W6 e plena em W6. Mantém 0155 antes de W1 como authoring; o critério-de-pass é pós-flip. Sem contradição real.
6. **`inject_runtime_prelude` já GARANTE cobertura por namespace** (`rt_prelude_guard`/`rt_units_cover_all`) — a re-fiação pro VFS preserva essa guarda (a guarda passa a iterar chaves do VFS, não `dsc_walk` de disco).

---

## 8. Ordenação final + pontos de reseed/ratchet

Fases largamente independentes (owner: intrínsecos NÃO-tanglados na região). Ordem por dependência + economia de reseed:

**Fase A — #embed/VFS** (aditivo; piso = NÃO-CRESCER o pico):
`0162 EMB-C0` scaffold [dry] → `0163 EMB-C1` parser [dry] → `0164 EMB-C2` resolver [dry] → `0165 EMB-C3` read-seam+compress build-time [fixpoint] → **`0166 EMB-C4` const-mat Tier-A [RITUAL reseed — toca binário]** → **`0167 EMB-C5` FileStream-mem + get/exists/list + conformância [RITUAL reseed]** → 🔑 SEED-BUMP.

**Fase B — prelúdio/tipos-base/provenance** (cavalga o VFS):
`0168 PRE-C1` embarca prelúdio-pequeno no VFS [fixpoint] → **`0169 BT-C1` consolida tipos-base no prelúdio, injeta em TODO artefato [RITUAL reseed]** → **`0170 PRE-C2` inject_runtime_prelude lê do VFS (memória, não disco) [RITUAL reseed]** → `0171 PV-C1` provenance-gate nome-reservado [fixpoint].

**Fase C — intrínsecos→superfície** (onda própria; ratchet BAIXA):
`0172 INTR-C1` censo + primitiva raw-syscall + confirma wrap/unwrap [dry] → `0173 INTR-C2` expurga `tk_*` natureza-1 [fixpoint, ratchet↓] → **`0174 INTR-C3` C-inline natureza-2→superfície [RITUAL, ratchet↓]**.

**Fase D — sweep de região** (a REDUÇÃO; ratchet BAIXA estrito) — os existentes, reordenados/anotados:
`0155 MEM-S1` shadow-scaffold (authoring, antes de W1) → `0156 W1` elisão → `0157 W2` presize+remove `#arena_*` → `0158 W3` scope-residence+array-fixo (PARANOID) → `0159 W4` move-on-return-via-param (PARANOID) → `0160 W5` object-owns-arena → **`0161 W6` root-in-`_start`+remove-ambient+reball → RESEED-FINAL**.

**Por que A/B antes de D-W6:** a provenance (B) precisa existir antes do reball W6 (que migra em massa os nomes reservados str/[]byte/ptr/uptr); base-types-in-prelude (BT-C1) é pré-req da provenance. **Por que C antes de D:** independentes; C banca reduções de ratchet numa base estável, e evita interleave de reseed dentro da cadeia coesa W1-W6. A/B/C podem, em teoria, interleave, mas os RITUAL de reseed ficam serializados.

**Ratchet:** Fases A/B são ADITIVAS → piso **NÃO-CRESCER** (o runtime/prelúdio em Teko tão enxuto quanto o disco que substitui; o VFS comprimido pode até baixar). Fases C/D são REDUÇÃO → **BAIXAR estrito** (D68). Mede-se a linha canônica `teko: memory: peak <N> MB` do build seco a cada RITUAL.
