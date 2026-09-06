# Handoff — sessão local do `ngen/` (port teko → mc)

Documento de entrada para uma **sessão local** assumir o trabalho do `ngen/`.
Escrito pela sessão remota coordenadora; leia inteiro antes do primeiro commit.

## 1. O que é o `ngen/`

O **port do teko para o `mc`** (minicompiler.dev, `minicompiler/mc`), morando dentro
deste repositório. O teko passa a ser uma **linguagem ensinada ao `mc`** por
módulos (hooks), em vez de um compilador próprio. Desde S4.1 (plano §64(f)) os
31 módulos do pacote `teko` (`teko.tk` + os 30 `teko_*.tk`) e `lib/rt.tk` são
`.tk`, transliterados do `.mc` original -- `core_teko.mc` (o `main()` deste
repositório) e `user.mc` (o driver do projeto) ficam `.mc`, por não serem do
pacote (D64.7).

**O compilador antigo (`src/`) saiu no passo 1 do rebase.** Todo trabalho vive na raiz deste repositório (era `ngen/` até o rebase).

Contexto completo: `docs/design/port-teko-mc.md` e as entradas **D211, D212,
D213, D214** do `DECISION_LOG.md`. Leia-as — são leis, não sugestões.

### 1.1 Corte v0.1.0 (dono 2026-09-06)

**v0.1.0 é release INTERMEDIÁRIA** — o port continua até o mc chegar a 1.0.0 (§7
segue valendo). O corte fecha com: zero resultado errado silencioso, recusas V0
para o que ainda não é ensinado, `mc` pinado por `MC_VERSION`, fixpoint
provado nas cinco pernas, `release.yml` publicando os assets, e a publicação no
registro do mc gated por variável (D230 adendo 2). Sequência decidida:
1. Squash de `fix/retirement` na `main` do fork `schivei/teko-lang`.
2. PR do fork para `teko-org/teko-lang`.
3. Já na org: remover o legado (`src/`, workflows do compilador antigo) e
   rebasear `ngen/` para a raiz do repositório.
Consequência da mudança de raiz: o manifesto do pacote (`[package]`) migra para
o `mc.toml` da raiz, **sem `[project]`** (regra do registro, D230 adendo 2);
`mc.toml` (ou o que restar dele na raiz) fica só com `[project]`/
`[compiler]`/`[target]`/… para o `mc build`. Detalhe completo em
`docs/design/pr-org-ngen.md` §7 e `DECISION_LOG.md` D230.

**Passo 1 do rebase FEITO** (`docs/design/plano-rebase-raiz.md` §4): saíram 1 261 ficheiros /
38,5 MB de legado — `src/`, `bootstrap/`, `.crumbs/`, `examples/`, `cases/`, `scripts/`,
`tooling/`, `packaging/`, os 22 `TEKO_*.md` e todo o `docs/` menos `docs/brand/` e as nove docs
de design vivas; ficam `ngen/`, a raiz cívica, `.github/` e o `DECISION_LOG.md`.

**Passos 2+3 do rebase FEITOS** (mesmo §4, um commit só): `ngen/` deixou de existir — os 89
ficheiros (`mc.toml`, `mc_teko.tk`, `core_teko.mc`, `user.mc`, os 31 `teko*.tk`, `lib/`,
`tests/`, `scripts/`, `MC_VERSION`, `README.md`, este `HANDOFF.md`) são a **raiz** do
repositório, e os caminhos do CI, do `bootstrap.sh`, do `measure.sh` e das receitas abaixo
acompanharam (`mc build .`, `build/teko build . --config … --entry-only`, `sh
scripts/bootstrap.sh`, `mc pkg hash .`, `cat MC_VERSION`). O nome do agregador do ruleset
continua **literalmente** `mc build ngen && run` (§5 R2 do plano: check exigido casa por NOME;
renomear é passo coordenado com o ruleset da org). Faltam o passo 4 (split do manifesto:
`mc.toml` só `[package]`, `teko.toml` com o build) e o passo 5 (docs).

## 2. Leis que valem aqui (resumo do que mais pega)

- **Comunicação com o dono é sempre em PT-BR.** Nunca use menu de opções/quiz;
  pergunta é em prosa curta.
- **D213 — reuse a base do mc, ensine só o DELTA.** O core do mc já dá a
  gramática Pratt, `fn`, `if`, `return`, expressões e os tipos nativos
  (`u8/u16/u32/u64/i64/uptr/void`). **Não reimplemente nada disso.** Onde a
  forma do mc já resolve, **adote a do mc**: fidelidade sintática ao
  teko-clássico **não é requisito**, funcionalidade é.
- **D214 — ordem das entregas:** (1) primitivas → (2) tipos (`class`,
  `struct`, `interface`, `trait`) → (3) crescer superfície e comportamento base.
- **D197 — não regrida memória ao surfacear:** o que era view/reinterpret
  (zero-cópia) continua view. Surfacear um bypass-de-memória como fn-que-copia
  é regressão.
- **NADA DE `Variant` nesta versão teko-mc (dono 2026-09-04, D217).** Não se ensina
  união dinâmica/valor etiquetado em runtime; tipos são estáticos e conhecidos pelo
  oráculo. Se um construto "pedir" Variant, é fork — parar e perguntar.
- **Forward-only, sem PR.** Dreno para `fix/retirement` por cherry-pick.
- **Base-lock antes de trabalhar:** parta de `origin/fix/retirement`, confirme
  que o HEAD é de 2026-09+ e que `src/parser/ast.tks:92` diz
  `BindKind = enum { Var; Const }` (sem `Let`/`Mut`). Base velha já causou
  retrabalho caro.
- **Nada de tocar** `src/`, `bootstrap/`, `cases/`, `examples/`, `tklib/`,
  `tooling/`, `main.tks` da raiz.

## 3. Estado atual

- **Branches:** `fix/retirement` (base canônica) e
  `claude/conversation-recovery-memory-cu1x6d` — mantidas **tree-idênticas**.
  Trabalhe a partir de **`fix/retirement`**.
- **Entrega 1 (fatia vertical) — LANDADA.** `ngen/` com `mc.toml`, `teko.mc`
  (registro `user_init`), `teko_{type,class,stmt,expr}.mc`, `lib/rt.mc`,
  `tests/hello.tk`, e o CI `.github/workflows/ngen.yml`.
  Ensinado: **`bool`** (`type_alias` → `TY_U8`) + honest-stops para
  `class`/`type`/`interface`/`namespace`/`import`/`using`, `var`/`const`/
  `match`/`when`, `new`.
- **Entrega 2 (primitivas) — LANDADA.** Ensinado por `type_alias` (identidade
  pura): **`char`** (`u32`), **`byte`** (`u8`), **`isize`/`usize`**,
  **`ptr`** (colapsa em `uptr`, o único ponteiro opaco do core), **`str`**
  (**NUL-terminated `uptr`** — D215 registra a forma). **`f32`/`f64`**: a lib
  `<float>` do mc (M24) **já registra essas mesmas palavras** — foi só
  **conectar** (`teko_float.mc`), não reimplementar. `lib/rt.mc` ganhou
  as fns ordinárias (não-hook): `tk_str_len`/`tk_str_slice` (**view por
  ponteiro, zero-cópia — D197**) e `tk_f64_bits`/`tk_f64_from_bits` (o
  reinterpret concreto `f64↔u64`; a forma genérica `<T>` do `wrap`/`unwrap`
  precisa de generics record/replay e fica para a entrega de tipos).
- **Entrega 3 (tipos) — COMPLETA: `struct`, `class`, `interface` e `trait`
  LANDADOS** (SHAs `984f268e`, `06db615d`, `a8757cef`, `2821c261`). Todos
  passam pela mesma tabela de tipos (`teko_struct.mc`), e `struct`/`class`/
  `interface` são registrados com `type_new(nome, 8, 8, TK_INT)` — identidade
  estática preservada para o `.` resolver membro.
  - `class`: palavra 0 da vtable é a itab, slots virtuais depois
    (`TK_VT_FIXED 1`); campos base-first; `virtual`/`override` contextuais.
  - **método NÃO declara receptor** (D219, entrega 5 crumb 0): `i64 area() { return side; }`
    — o compilador injeta `this`; nome não-qualificado que não é local nem parâmetro é
    membro de `this`; `this.campo` explícito; `base.m()` chama a implementação da base
    DIRETO (sem vtable). `self` é recusado (`teko: methods take no explicit receiver;
    use this`).
  - `interface`: assinaturas + conformidade checada dentro do `Classe_vt_init`;
    despacho por `tk_itab` (`lib/rt.mc:44`), dinâmico.
  - `trait` (**D216 — modelo do PHP**): corpo gravado por `p_skip_balanced` e
    re-parseado por classe via `p_push_source`; flattening pela MESMA máquina de
    membros; precedência classe > trait > base; **não é tipo** (sem `type_new`);
    `use` lida como identificador contextual, nunca registrada.
  - **Polimorfismo FUNCIONA**, por interface E por classe base, inclusive sobre
    parâmetro (verificado: `area_of(Shape s)` com a derivada devolve o
    `override`; três níveis `A→B→C` devolvem 1/2/3). O despacho não depende do
    tipo estático porque a vtable da derivada é extensão-PREFIXO da base — o
    slot é o mesmo em toda a cadeia. Ver a dívida do §5 para o limite real.
- **CI VERDE, com execução real** (Linux x86_64, ~15 s ponta-a-ponta): baixa
  `mc-0.10.0-linux-x86_64`, confere checksum, `mc build ngen` constrói o
  compilador ensinado `build/mc-teko`, e **9 fixtures compilam, linkam e
  RODAM**: `hello.tk` + `primitives_{float,ptr,scalar,str}.tk` +
  `types_{struct,class,interface,trait}.tk`, todas com exit 42.

## 3.1 CI do repositório — só o `ngen` conta (dono 2026-09-04)

O CI do compilador antigo (`pr.yml` fixpoint/self-host, nightly, seeds, `theory/*`,
release do bootstrap, tag-on-version-bump) está **desativado** no GitHub — 17
workflows em `disabled_manually`; os arquivos seguem no repo, `src/` está congelado.
Ativos: **`ngen (mc) CI`**, CodeQL, Branch policy, Mirror PR. O ruleset `main` passou
a exigir **só** o check `mc build ngen && run` (antes: "CI gate" e "Test suite gate" do
`pr.yml`, que nunca mais fechariam). `fix/retirement` não tem proteção; o ruleset
`others` só barra force-push fora de `main`/`fix/**`/`theory/**`/`f1/**`/`cargo/**`.

### As CINCO pernas nativas (dono 2026-09-04)

`ngen.yml` é uma **matriz de 5 pernas**, cada uma no runner do próprio SO **e**
arquitetura — nada é cross-compilado e deixado sem rodar. Cada perna baixa a
release do `mc` daquele par, confere o `.sha256`, **assere `mc --host`** contra o
par da perna, constrói o compilador ensinado e **executa** `hello` + as 17
fixtures do glob (`// expect-exit: N` como oráculo) — 18 programas no total.

| runner | `[target]` os/arch | asset da release | `[linker]` |
|---|---|---|---|
| `ubuntu-latest` | `linux`/`x86_64` | `linux-x86_64` | **nenhum** — ELF dinâmico do M42; `[target]` glibc (`interp`/`libc`) |
| `ubuntu-24.04-arm` | `linux`/`aarch64` | `linux-arm64` | **nenhum** — ELF dinâmico do M42; `[target]` glibc (`interp`/`libc`) |
| `macos-latest` | `macos`/`aarch64` | `macos-arm64` | **nenhum** (backend `macho-exe`) |
| `windows-latest` | `windows`/`x86_64` | `windows-x86_64` | `lld-link` |
| `windows-11-arm` | `windows`/`aarch64` | `windows-arm64` | `lld-link` |

O nome do asset usa `arm64`, o `[target]` usa `aarch64` — os dois nomes convivem
na matriz. O check dos legs é `ngen (<os>/<arch>)`; o **agregador** mantém o nome
exato que o ruleset exige, `mc build ngen && run`, e falha se qualquer perna falhar
(nada a trocar no ruleset).

O config de cada perna é **derivado do `mc.toml`** (nunca editado): remove-se o
`[linker]`, troca-se `[target] os`/`arch` e o `out` ganha `.exe` no Windows; o resto
(`[project]`/`[compiler]`/`[include]`/`[limits]`) não pode divergir entre pernas.

**Windows não tem C runtime nem backend de executável direto**, então a perna monta
antes o sysroot que o link precisa, tudo com o próprio `mc` + LLVM da imagem:
`winstart.obj` (`#include <sys_windows_start>`) e `mcrt.obj`
(`#include <sys_windows_host>`) compilados com `--backend=coff-obj-{arm64,x86_64}`, e
`kernel32.lib` gerado por `llvm-dlltool` a partir da lista de 15 exports (a mesma de
`minicompiler/mc` `scripts/sysroot-windows.sh`). A linha de link é a do próprio `mc`
(`src/mc.windows-*.toml`): `-entry:mc_start -nodefaultlib -stack:8388608`.

### A SEXTA perna: `fixpoint` — as MESMAS CINCO pernas (S4.3 + S4.3b, 2026-09-06)

Job **`fixpoint`**, fora da matriz `leg`, nos **cinco** pares que as pernas nativas cobrem —
`fixpoint (linux/x86_64)`, `fixpoint (linux/aarch64)`, `fixpoint (macos/aarch64)`,
`fixpoint (windows/x86_64)` e `fixpoint (windows/aarch64)`. Roda
`sh scripts/bootstrap.sh --os <os> --arch <arch>`: teko0 (`mc build .
--compiler-only`, mc de prateleira) → teko1 → teko2 → teko3 sobre `mc_teko.tk`, com os
TRÊS critérios do plano §64(f) — `cmp` dos objetos `teko2.o`/`teko3.o`, `--dump-asm` de teko2
vs teko3 com diff vazio, e teko1 compilando e rodando as 45 fixtures. As pernas provam "a
fixture sai 42"; esta prova "o compilador se reproduz". O script imprime o tempo de CADA
estágio e o tamanho de cada objeto/binário. **Cinco e não três porque o que a escada prova é
propriedade DE UMA MÁQUINA** — provar em três e afirmar em cinco seria exatamente a afirmação
cross-compilada que este workflow existe para recusar.

**O que o job reporta e NÃO barra:** o passo de `summary` publica tamanho e **`sha256` de
`teko0`/`teko1.o`/`teko2.o`/`teko3.o`** (o `teko0` entrou na higiene 4: é o único estágio que o
`mc` de prateleira escreve, então é ele que separa "não-determinismo do compilador" de "entrada
diferente"), e o próprio `bootstrap.sh` imprime a mesma provenance com a versão do `mc`. Quando
`teko1.o != teko2.o`, o job arquiva por 14 dias o `--dump-asm` do teko0 e o do teko1 sobre
`mc_teko.tk` com o `diff` — a forma legível que um objeto não dá. O `cmp` dentro do run já prova o
ponto fixo; se o `teko2.o` é byte-idêntico ENTRE runs e ENTRE máquinas é outra afirmação — imprimir
é o que permite conferi-la antes de pinar. Quando estabilizar, vira golden versionado, no molde do
`tests/golden/mc2.sha256` do mc.

**O agregador NÃO mudou.** `mc build ngen && run` continua dependendo só da matriz `leg` —
o nome é o que o ruleset exige e o significado dele fica sendo o que diz. Promover a escada
a check obrigatório é decisão de ruleset, fora deste workflow.

**Como o Windows entra (S4.3b).** A escada precisa do `<out>.o` em disco, o que exige
`[linker]` — e no Windows não há `cc` nem C runtime: o link é `lld-link` contra o sysroot de
três arquivos (`winstart.obj`, `mcrt.obj`, `kernel32.lib`). Duas peças, nenhuma duplicada:

- **`.github/actions/windows-sysroot`** (action composta) monta o sysroot e é usada pelos DOIS
  jobs Windows (a perna e o fixpoint), que antes teriam a mesma montagem escrita duas vezes.
  Ela também põe o LLVM no `$PATH` em forma WINDOWS (o `mc` chama o linker por
  `CreateProcessA`, que lê o PATH do Win32) e aponta o `TMPDIR` para o temp do runner (o `mc`
  nativo não abre os caminhos `/tmp/...` do MSYS).
- **`bootstrap.sh --linker-toml FILE`** troca o `[linker] cc` do `mc.toml` pelos blocos
  `[sysroot]`/`[linker]` do arquivo (a matriz do job escreve a MESMA linha que a perna usa).
  O `awk` que remove o bloco velho é o mesmo do config das pernas.

No Windows todo estágio se chama `<nome>.exe` (o `mc` já anexa o sufixo ao `[compiler].out`
sozinho; o `[project].out` é o script que nomeia), e como o `mc` deriva o objeto do `out` por
`+ ".o"`, o critério compara `teko2.exe.o` com `teko3.exe.o` — COFF, mesmo `cmp`. O `--dump-asm`
não muda. O `mc` vai para o `$GITHUB_PATH` em forma Windows (`cygpath -w`) pelo mesmo motivo do
LLVM.

**`--os`/`--arch` agora são CONFERIDOS contra `mc --host`.** O compilador ensinado do estágio 0
é escrito pelo backend de executável do **HOST** de qualquer jeito, e a escada **executa** todo
estágio que constrói — então um alvo que não é esta máquina nunca foi ponto fixo, é um
cross-build sem nada para rodar. Recusa imediata, com a causa, em vez de `exit 127` três
estágios depois (foi assim que o sufixo `.exe` se revelou o do HOST, não o do `[target]`).

**`[target]` de linux, achado do CI:** o compilador ensinado é escrito pelo backend de
executável do **HOST**, e o writer ELF do mc põe interpretador/soname **musl** por padrão;
num runner glibc o `build/teko` existe e mesmo assim não executa (`not found`, exit
127 — é o loader falando). O `bootstrap.sh` passou a anexar `interp`/`libc = "gnu"` ao
`[target]` derivado **quando o alvo é linux e aquele loader existe na máquina**; máquina
musl não anexa nada. (As pernas não sofriam: elas já traziam `interp`/`libc` na matriz.)

O passo que baixa e verifica o `mc` é o MESMO nas dez: `.github/actions/setup-mc` (action
composta) resolve a versão **PINADA por `MC_VERSION`** (§3.2) — `latest` só se o
chamador pedir explicitamente por `inputs.version` —, baixa o asset do par, confere o
`.sha256` e assere `mc --host` — nenhum job pode testar um compilador diferente do outro.

### Cortar uma versão: `release.yml` (R1, 2026-09-06)

**Como se corta.** A tag É a versão; não existe arquivo de versão.

```sh
git tag -a v0.3.1 -m "teko 0.3.1" && git push origin v0.3.1
```

ou, para uma tag que **já existe**:

```sh
gh workflow run release.yml --ref v0.3.1
```

(ou aba Actions → *Release* → *Run workflow*, escolhendo a tag `v0.3.1` como ref do
dispatch). Versão com `-` (`0.3.1-rc1`) publica como **pre-release**: nunca vira "latest" e
o `mc pkg add` só a escolhe se nomeada.

**Quatro jobs**, em `.github/workflows/release.yml`:

| job | o que faz |
|---|---|
| `version` | deriva/valida tag e versão do `github.ref_name`/`github.ref_type` da própria run |
| `gate` | **é o próprio `ngen.yml`**, chamado por `workflow_call` — herda o ref do caller |
| `release` | anexa os 10 arquivos à Release da tag, com notas geradas |
| `publish-to-registry` | pré-voo do pacote; anúncio ao registro **atrás de uma variável** |

**Não existe job `assets`, de propósito.** Um job que compilasse o compilador de novo
publicaria bytes que **nenhum portão viu** — foi exatamente por isso que a `release.yml`
anterior (do compilador velho) virou promoção. Então quem empacota é a PRÓPRIA perna, com a
action `.github/actions/package-teko`, logo depois de rodar as fixtures contra aquele
binário. `ngen.yml` ganhou `workflow_call` com as entradas `package`/`version`: num push ou
PR comum nenhuma delas vem, e o workflow se comporta como antes. O `push` do `ngen.yml`
passou a filtrar **branches**, para a tag não disparar a matriz duas vezes.

**CodeQL: sem `ref` de input (2026-09-06).** O `workflow_call` chegou a ganhar um input
`ref`, repassado cru pro `actions/checkout` das duas jobs (`leg`/`fixpoint`) — o GHAS marcou
6 alertas *high* de "Cache Poisoning via execution of untrusted code" nesse caminho. Correção
de raiz: o input `ref` **saiu** — um `workflow_call` já roda no ref do CALLER, e a
`release.yml` só chama o `ngen.yml` a partir de uma run que já está na tag (evento `push` da
tag, ou `workflow_dispatch --ref vX.Y.Z`), então nenhum checkout precisa de um `ref` próprio
para confiar ou desconfiar. O input `version` da `release.yml` saiu pelo mesmo motivo: a
versão deriva de `github.ref_name`, validado contra `github.ref_type == 'tag'`.

**O que a Release carrega:** 10 arquivos — `teko-<ver>-<os>-<arch>.tar.gz` + `.sha256` para
os cinco pares (`linux-x86_64`, `linux-arm64`, `macos-arm64`, `windows-x86_64`,
`windows-arm64`, a mesma grafia de asset do `mc`) — e notas geradas com a **versão do mc**
que construiu, o bloco de checksums e as **tabelas de provenance do fixpoint** (tamanho e
`sha256` de `teko0`/`teko1.o`/`teko2.o`/`teko3.o` nos dois pares). Dentro do tarball: o
binário (`teko`, `teko.exe` no Windows), `lib/rt.tk` (um PROGRAMA teko inclui o runtime por
caminho), `INSTALL.txt` gerado, `README.md` do `ngen/` e o `LICENSE`. O empacotamento é
**reproduzível** (mtime fixo, lista de membros explícita e ordenada, `ustar`, `gzip -n`) —
a mesma receita do `scripts/release-assets.sh` do `mc`.

**`publish-to-registry` tem duas metades.** A primeira roda SEMPRE: `mc pkg hash .` e a
compilação de cada unidade de `[package].check` pelo `mc` DE PRATELEIRA — que é o que o
validador do registro faz na caixa dele (guia 27 §5). A segunda é o anúncio
(`minicompiler/register-action@v1`), **atrás da variável de repositório
`TEKO_REGISTRY_PUBLISH`**: só com ela igual a `1` o anúncio acontece; sem ela o job imprime
o plano e sai 0 — nunca vermelho, nunca mentindo que publicou. O pacote precisa ser
registrado **uma vez, por uma pessoa**, em <https://minicompiler.dev/me> (guia 27 §3); antes
disso o registro responde `404 not registered`.

(Detalhe medido: o runner baixa o repositório da action no *Set up job*, mesmo quando o passo
está `if`-desligado — então `minicompiler/register-action@v1` sumir do GitHub deixaria o job
vermelho ainda que ninguém publicasse. Hoje ela existe e é pública.)

**`mc pkg check` NÃO é o que roda aqui:** ele recebe um arquivo de ÍNDICE
(`mc pkg check INDEX.toml`) e é o gate do CI do próprio registro sobre uma linha publicada —
não um validador de diretório. O equivalente local do que o validador faz é o par
hash + compilação das unidades de `check`, que é o que o pré-voo executa.

**ARMADILHA JÁ PAGA — o arquivo estava DESLIGADO.** O GitHub identifica um workflow pelo
CAMINHO DO ARQUIVO, não pelo `name:`. `.github/workflows/release.yml` era o *Bootstrap
Release* do compilador velho e estava `disabled_manually` na limpeza de 2026-09-04, então a
primeira tag de teste **não disparou nada** — sem erro, sem run. Foi preciso religar
(`gh api -X PUT repos/<owner>/<repo>/actions/workflows/<id>/enable`, id `316148340` no fork)
e re-empurrar a tag. **No repositório da org isto vai acontecer de novo** se o `release.yml`
lá nascer/for herdado desligado: conferir `gh workflow list --all` depois do primeiro push.

## 3.1a Repositórios do mc migraram para a organização `minicompiler` (2026-09-05)

`schivei/mc` → **`minicompiler/mc`** (e os privados `mc-registry`/`mc-ops`). O GitHub redireciona os
nomes antigos, mas o CI (`ngen.yml`: API de releases e base de download) e a receita do §4 já apontam
para `minicompiler/mc`. Tags, releases e checksums não mudam. A org é a casa dos pacotes oficiais e a
identidade admin do registro.

## 3.3 Pacotes: validado com o mc — nada muda agora; como a teko será distribuída (2026-09-06, D230)

O dono pediu validar com o mc um redesenho de pacotes ("libs e bundles via empacotamento"). Resposta
do mc (NOTICES-teko 2026-09-06): o modelo publicado NÃO muda; o redesenho é spec futura (pacote `mclib`
das libs do mc como ADIÇÃO ao blob; ferramentas como pacotes `kind = "exe"` + `[[tool]]` +
`mc tool install`; `[[permission]]`; site). Para o `ngen/`: `<mc/core_min>`/`<mc/host>` seguem no blob com
a mesma grafia; `[compiler]` e `[package]` estão certos; **`module =` é chave ignorada pelo mc** (fica
como documentação). Distribuição futura: `[deps] teko = "x.y.z"` + `#include <teko>` para o runtime;
compilador ensinado via `[compiler] modules = ["<teko/teko.tk>", "user.mc"]` no projeto do usuário
(hoje) ou `mc tool install teko` (depois). **Dívida antes de publicar:** `check = ["teko.tk",
"lib/rt.tk"]` -- o validador compila cada unidade SOZINHA na caixa linux/x86_64 sem rede, e `teko.tk`
não é unidade autônoma; candidata: uma unidade que inclua o compilador inteiro (`mc_teko.tk`). Publicar
só em versão estável (decisão do dono). Detalhe em `DECISION_LOG.md` D230.
**Adendo (mc):** a unidade de `check` é compilada pelo `mc` DE PRATELEIRA da caixa (sem teko carregada) →
tem que ser superfície do NÚCLEO. `mc_teko.tk` serve enquanto os módulos forem transliteração; teko-ificar o
compilador (S4.4+, fork g3) tornaria o `check` recusado pelo parser de prateleira -- pesa contra o g3
"obrigatório". mc 0.15.9 publicado (só o registro padrão muda para `https://pkg.minicompiler.dev`); o patch
`TE_RULE` que destrava o S4.2 vem como 0.15.10.

**DÍVIDA FECHADA (R1, 2026-09-06): `check = ["mc_teko.tk"]`.** Medido com o mc 0.15.12, de dentro de
`ngen/`, as DUAS entradas antigas falhavam no `mc` de prateleira — `mc teko.tk` →
`machine_arm64_float:332: initializer must be constant` (é fragmento: assume `<mc/host>` + o núcleo já
incluídos) e `mc lib/rt.tk` → `lib/rt.tk:64: type expected in parameter` (o `str` de
`void panic(str msg)` é palavra que a TEKO ensina, logo o parser de prateleira não a conhece). A única
unidade inteira e ainda 100% núcleo é **`mc_teko.tk`** (`mc mc_teko.tk -o x.o`, exit 0). `files` ganhou
`mc_teko.tk` + os dois arquivos que ela inclui e o pacote não embarcava (`core_teko.mc`, `user.mc`) —
o hash de árvore cobre só `files`, e unidade de `check` fora dele é recusada. É a mesma forma do pacote
do próprio `mc` (`check = ["src/mc_linux_x86_64.mc"]` + o `src/user.mc` que ela precisa).

Hash de árvore do pacote (`mc pkg hash .`), depois da mudança:

```
9318b19cb5b76629c03ca1b354d0d93bc3860e8a4f7747945cd19e235a5e5004   (pós-V0; o R1 mediu 057e7aed…; antes d41a0c80…1a7ada)
```

**O hash literal MUDA a cada byte** de `mc.toml` ou de qualquer ficheiro de `[package].files` —
inclusive um byte de COMENTÁRIO, porque o que ele digere é o ficheiro, não o TOML interpretado. É
isso que ele existe para dizer. Logo nenhum número colado num documento é permanente: a **fonte** é
sempre `mc pkg hash .` rodado na raiz, e o valor escrito aqui é só o registro do commit que o mediu.
O `9318b19c…` acima é **pré-V1**. A `main` da org media
`379fc9896f633d5db6669dbf07108a17e88fa06e44f5b0e5d1e487f57b6fc26b`, e o rebase para a raiz
(passos 2+3) o preserva enquanto `mc.toml` não muda — o `git mv` não toca byte nenhum de
`[package].files`, medido. Com os quatro comentários de `mc.toml` que ainda diziam `ngen`
corrigidos (`mc build .`, `mc limits .`, `lib/rt.tk`, `HANDOFF.md`; nenhuma chave mudou), passa a
`6e6bb5dfc0daccb68cd9b2955e56853db208d2535ef690b831540dac3670f5d1`. O split do manifesto (passo 4)
muda `mc.toml` outra vez e **vai** mudar o hash de novo; é esperado e sem consequência antes da 1ª
publicação.

A ressalva do adendo continua **viva**: teko-ificar o compilador (S4.4+, fork g3) torna `mc_teko.tk`
ilegível para o parser de prateleira e o pacote perde o `check`. Não há hoje uma segunda unidade
candidata — `lib/rt.tk` só voltaria a ser `check` se `panic` deixasse de usar `str` na assinatura.

## 3.2 O mc que o CI usa hoje: 0.15.13 (2026-09-06)

**Fonte da verdade: `MC_VERSION` (V2, 2026-09-06).** O CI não resolve mais `latest` por
padrão — `.github/actions/setup-mc` lê `MC_VERSION` (uma linha, sem `v`) quando o chamador
não passa `inputs.version`, e pina exatamente essa release; `latest` só entra se pedido
explicitamente. "O mc que o CI usa" é sempre `cat MC_VERSION` — não é preciso ler o log de
um run para saber. Motivo (medido, 0.15.12): `latest` quebrou o CI sem aviso quando os 12
globais de `src/driver.mc` viraram acessores (parágrafo abaixo) — um patch release do mc, hoje,
entraria no gate de todo PR aberto ANTES de qualquer um destes textos ser atualizado.

**Como subir a versão pinada.** (1) baixar a release nova e rodar o baseline local (§4) — as
45 fixtures têm que fechar 45/45 contra o `mc` novo; (2) `sh scripts/bootstrap.sh` contra
o `mc` novo tem que fechar `FIXPOINT OK`; **só depois** dos dois verdes (3) trocar o conteúdo de
`MC_VERSION` para a versão nova e registrar o que mudou nesta seção, no mesmo padrão dos
parágrafos abaixo. Nunca trocar o arquivo primeiro e validar depois — é o mesmo acidente do
`latest` sem aviso, só que manual.

**0.15.13 (PR #43): um cast DIRETAMENTE sobre um `callp` declara o tipo de retorno da chamada indireta.**
`res_expr`, arm `N_CAST` (`mc/src/gen_resolve.mc`): quando o filho imediato é o `callp` (`nd_kind(a) ==
N_CALL && res_kind(a) == RK_INTRIN && res_decl(a) == IN_CALLP`), o tipo do cast desce para o nó do `callp`
(`set_res_type(a, nd_type(n))`) — `(f64) callp(&dbl, 2.0)` tipa o nó como f64, `walk_ret_type()` responde
float e o `fa_result` do `<float>` move `d0`/`xmm0` para o destino. Sem cast, segue `TY_I64` como antes; um
cast EXTERNO (que não é pai imediato) vira identidade; `(i32) callp(...)` recebe o estreitamento M45 e o cast
repete um `sxtw` idempotente. É o contrato que o V1 consome nos cinco construtores de `callp` da teko (§5,
bloco V1) — fecha o item 1 do §74(b). Também na release: as conversões single do `<float>` no arm64
(`FI_SCVTF_D..FI_UCVTF_D`, `FI_FCVTZS_D..FI_FCVTZU_D` com `+2` — item 2 do §74(b)) e a ordem
resultado/restauração no Win64. Bump validado nesta máquina ANTES de trocar o arquivo, na ordem do parágrafo
acima: 45/45 com o `mc` novo e `sh scripts/bootstrap.sh` `FIXPOINT OK` (`teko1.o == teko2.o == teko3.o`,
`b45a0446…`), sobre a árvore da base `a66b80c9`.

**0.15.12 (PR #42, "dieta de globais" do driver): os 12 globais de `src/driver.mc` viraram UM registro de arena
com acessores** -- `cfg_file` → `cfg_file()`, `drv_lim_mode = 1` → `set_drv_lim_mode(1)` (e `drv_os()`,
`drv_arch()`, `drv_target()`…). A teko lia dois deles (`teko_access.tk` `tk_access_init`; `teko.tk` `tk_limits`)
e quebrava com `teko_access.tk:76: unknown name`; corrigido nos dois sítios (o CI resolve `latest`, então a
quebra seria imediata). Regra: NÃO ler global do driver; sempre o acessor. **0.15.11 (PR #41):** `machine()` só
vira corrente quando não há nenhuma, nome NOVO, ou substitui o que É corrente -- o re-registro dos 3 nomes pelo
`teko_float.tk` não move mais; `teko --dump-machine` agora dá `arm64 (current)` e o modo cru lowera no host
(medido).

### 3.2b O anterior: 0.15.10

**0.15.10 (PR #40): `TE_RULE`** -- lexema criado por `#rule`/`#token`/`#infix`/`#prefix` nunca é escondido
pelo `source_claim` (`lex_word_id` devolve o id em qualquer fonte); o handler do módulo sobre esse lexema só
despacha em fonte reivindicada; em fonte não reivindicada vai à regra/núcleo. Fora de escopo, documentado:
lexema que é literal de regra E tipo ensinado. **0.15.9**: só o registro padrão (`https://pkg.minicompiler.dev`).
**Pendente no mc:** `machine(name, tab)` faz `mach_tab = tab` sem condição (hooks.mc:834) -- o re-registro dos
três nomes pelo `teko_float.tk` em `user_init` move a máquina corrente do modo CRU para a última (`x86_64-win`);
`teko build --config` não sofre. Patch aceito (re-registro preserva o current); NÃO aplicamos o contorno
`machine_use_if(host_machine())` -- esperamos o núcleo.

### 3.2a O anterior: 0.15.8

**0.15.8 (PR #37, patch de cooperação): `void source_claim(uptr fn)`**, handler `i64 f(uptr name)`,
1 = "esta fonte é do meu dialeto". Chamado de `lex_push_mem`, UMA vez por quadro, com o nome que
`lex_file()` imprimiria; a cadeia inteira é reperguntada para os quadros já abertos no instante em
que um handler registra (a entrada inclusive), no molde do `on_source`; e o handler NÃO pode
empurrar fonte. O que a resposta muda é UMA coisa: numa fonte que ninguém reivindica, uma palavra
que o módulo registrou por `word_add` (`syntax`/`syntax_stmt`/`syntax_expr`/`syntax_infix`/
`type_alias`/`type_new`) deixa de ser palavra e lexa como identificador comum -- é o que permite a
um compilador ensinado ler os fontes do PRÓPRIO núcleo, onde `type`, `out` e `params` são nomes de
parâmetro. Fora do escopo: `#rule`/`#infix`/`#prefix`/`#token`, `intrinsic()` e o `i32` do núcleo.
**Adotado pela S4.2** (`teko_fwd.tk`, `tk_source_claim`/`tk_push_source`; §5) -- e a ressalva
medida está lá: a marca é por ENTRADA de token, então um lexema que o dialeto ensina E que é
literal de `#rule` do prelúdio (`while`, `for`) some das fontes não reivindicadas. `mc limits`
ganha a linha `source_claim`.

(Registro anterior, 0.15.3:)

**0.15.3 (PR #31, patch de cooperação): `void on_source(uptr fn)`**, handler `void f(uptr name,
uptr src, i64 len)`, chamado de `lex_push_mem` como última instrução do push, para TODA fonte que
o lexer abre -- a entrada, `#include` que o NÚCLEO resolve sozinho, `<bundle>`/`<pack/...>` e todo
`p_push_source`. `name` é o que `lex_file()` imprimiria para aquele quadro. **A entrada também é
anunciada:** como `lex_init` empurra a entrada antes de `user_init()` rodar, registrar o handler
dispara um REPLAY, só para ele, de toda fonte já aberta -- então a varredura manual da entrada
que `tk_fwd_init` fazia deixa de ser necessária. **Adotado** (`teko_fwd.mc`,
`tk_fwd_on_source`/`tk_fwd_is_source_name`): `tk_fwd_init` agora só chama `on_source(&tk_fwd_
on_source)`; o callback filtra por SUFIXO `.tk` (nenhum outro nome que o lexer anuncia termina
assim -- um frame de `p_push_source` sempre junta palavras com espaço ou dois-pontos, um nome de
`<bundle>` é uma palavra nua tipo `mc/core`, e um `#include "../lib/rt.mc"` que um PROGRAMA `.tk`
escreve termina em `.mc`) e chama `tk_fwd_scan` só para os que passam. Nenhuma tabela de
"já varrido" nova: o `lex_seen` do próprio núcleo já impede um `#include`/`import` repetido de
empurrar (e portanto de anunciar) o mesmo nome duas vezes. `tk_import` (`teko_ns.mc`) parou de
chamar `tk_fwd_scan` depois do seu próprio `lex_include` -- o push já dispara o callback registrado,
por conta própria. Fecha a dívida do O2: um `#include "parts/x.tk"` CRU (fora de `import`) agora
tem seu conteúdo varrido no instante em que é empurrado, então um uso escrito ACIMA da declaração
real, mas DENTRO do arquivo incluído, resolve -- provado por probe (fora de `tests/`): com o
compilador da base (pré-0.15.3) o mesmo programa dava `type expected in parameter` (a palavra nunca
tinha sido reservada); com o callback, compila e roda normal. `mc limits` ganha a linha `on_source`
(1 registro, na segunda tabela -- a que mede a compilação REAL de um `.tk` pelo compilador
ensinado; a primeira tabela, da compilação de `teko.mc` em si, nunca chega a rodar `user_init`).
Baseline local no 0.15.3: 42/42, `same=42 diff=0` contra a base `0a0bd0f4` (nenhuma fixture tocada
por este item).

(Registro anterior, 0.14.1:)

**0.14.1 (PR #25, patch de cooperação):** `continue N;` no núcleo, espelho de `break N;` — N
níveis de laço contados do mais interno; `continue;` = `continue 1;` (mesmo nó de antes, inerte,
`nd_val` 0 lido como 1); `continue 0;` → `continue expects a positive level`; N além da
profundidade → `continue out of range`; `continue outside loop` inalterado; `on_jump` recebe o nó
ANTES da checagem de nível (o `depth` do gancho continua sendo profundidade de BLOCO, não de laço).
Consumido pela entrega 5 crumb "adoção do 0.14.1": `tk_switch_rewrite_continue_stmt`
(`teko_switch.mc`, era `tk_switch_no_continue_stmt`) e `tk_loop_rewrite_stmt`
(`teko_loop.mc`) leem `nd_val` de `N_CONTINUE` a mesma forma que já liam de `N_BREAK`;
`tk_rc_jump` (`teko_rc.mc`) idem. Baseline local no 0.14.1: 32/32.

(Registro anterior, 0.13.0:)

**0.13.0 (PR #22, M45):** `i32` (`type_new` pelo NÚCLEO, kind `TK_SINT`, sinal por kind); **uma
chamada devolve o que declara** (D5) — todo `extern` que devolve C `int` passa a `extern i32`
(corrigido em `tests/surface_overload_free.tk`'s `chmod`); **`p_cp()`** público (o cursor do
lexer sob substituição, usado em `teko_access.mc`'s `tk_dot_follows`); e o falso positivo
`region crosses a file boundary` no fim de um arquivo incluído, corrigido no núcleo (nada a tirar
aqui — `teko_generic.mc` não tinha contorno algum, só o design região-por-parte). O mesmo release
também respondeu ao lote C5b: `+` unário tem site por `syntax_expr("+")` + `parse_expr(11)` (a
precedência acima de `*`/`/`/`%`, a mais alta do `--dump-rules`), sem linha nova no núcleo —
`teko_ops.mc`'s `tk_unary_plus`. Baseline local no 0.13.0: 25/25.

(Registro anterior, 0.12.1:)

**0.12.1 (PR #21, patch de cooperação):** `[target].libc = "gnu"|"musl"` (FAMÍLIA; a grafia
soname é recusada), `[target].link = "dynamic"|"static"` (static = asserção; com importação
recusa nomeando `[linker]`), flags `--libc/--link/--interp` só com `--exe`. O CI do ngen escolhe
a grafia pela versão (`sort -V` vs 0.12.1) — nada a fazer. Baseline local no 0.12.1: 23/23.
Resposta do mc ao D227: `on_jump` = `blk_depth` (blocos), a pilha de laços só existe no
walker (`gen_walk.mc`), o parser não sabe o que é laço (`while`/`for` são `#rule`) —
`p_loopdepth()` enfileirado como lacuna a preçar, não entra no M45. M45 em correção.

(Registro anterior, 0.12.0:)

**0.10.3 = M41.5** ("the follow-ups the ngen consumer exposed"): **`syntax_param(&f)`** +
`p_decl_name()` — o hook de declaração de função que faltava (**C6 desbloqueado**); e
**`syntax_infix` sobre operador do core funciona** (`ops_init` lazy). **0.11.0 = M40** (AVR).
**0.12.0 = M42**: `--exe` em Linux sem `[linker]`/sysroot. Baseline 18/18 no 0.12.0. Plano §22.
Os avisos de release da sessão do mc **não chegam** por mensagem — o dono repassa; conferir
`gh api repos/minicompiler/mc/releases/latest` ao começar o dia.

(Registro anterior, 0.10.2:)

A sessão do mc coopera por **patch release** (`x.x.N`), sem mensagem: o CI resolve
`latest` sozinho. **0.10.1** = M41 (core composto em cinco partes, `type_disable`/
`intrinsic_disable`, largura declarada de `uptr`). **0.10.2** = os defeitos que o
`ngen` achou: `--exe` resolve o host (antes: Mach-O sempre), `.note.GNU-stack` em todo
ELF, `examples/lang` avaliava o receptor duas vezes na chamada virtual (o achado do
crumb 2), README do `lx` com `MAXPARAMS` 12. **Segue aberto no mc:** hook de declaração
de função (C6 — `on_param` ou geral) e `syntax_infix` sobre operador do core morrendo em
silêncio no `ops_init()` (rota do C5 é `pass()` de qualquer forma). Ao sair release nova:
baixar, trocar o symlink, **reconferir o baseline** (§4).

## 3.4 Gates e releases para a org (R1, 2026-09-06)

O dono vai cortar uma versão estável, mergear `fix/retirement` na `main` e abrir PR para
`teko-org/teko-lang`, que passa a ser o repositório de trabalho. O que a org precisa exigir,
e o que deixa de existir:

### Os checks que o ruleset da `main` deve exigir

| check | de onde vem | obrigatório? |
|---|---|---|
| `mc build ngen && run` | job `gate` de `ngen.yml` (agrega as 5 pernas) | **SIM** — é o único hoje |
| `fixpoint (linux/x86_64)` | job `fixpoint` de `ngen.yml` | opcional, **recomendado** |
| `fixpoint (macos/aarch64)` | job `fixpoint` de `ngen.yml` | opcional, **recomendado** |

O agregador continua sendo o nome que o ruleset atual exige e **não mudou de significado**:
ele depende só da matriz `leg` e fica verde quando as cinco pernas ficam. As cinco pernas
individuais (`ngen (linux/x86_64)`, …) NÃO precisam entrar no ruleset — o agregador já
falha se qualquer uma falhar; listá-las só duplica.

Promover as duas pernas de `fixpoint` a obrigatórias é decisão de ruleset, e a recomendação é
SIM: elas provam coisa diferente das pernas (que o compilador se reproduz, `teko2.o == teko3.o`),
custam ~1 min e já rodam em todo push/PR. O único efeito colateral é que um PR que quebre o ponto
fixo passa a ser barrado em vez de apenas reportado — que é o que se quer de uma linguagem
auto-hospedada.

### O que SAI

* **"CI gate" e "Test suite gate"** (do `pr.yml`) — os checks do compilador velho. Já não
  fechariam nunca; o `pr.yml` foi apagado nesta entrega.
* **Os 17 workflows legados.** Oito arquivos foram removidos (`pr.yml`, `nightly.yml`,
  `reseed-bootstrap.yml`, `seed-linux-fork.yml`, `tag-on-version-bump.yml`, `theory.yml`,
  `theory-generation-decay.yml`, `mirror-pr-to-org.yml`); o resto dos `disabled_manually` são
  workflows de branches de teoria que nunca existiram nesta árvore.
* **`mirror-pr-to-org.yml`** em especial: com o trabalho MUDANDO para a org, espelhar PR do fork
  para lá deixa de fazer sentido.
* **A perna `c-cpp` do CodeQL**, que compilava `src/runtime/teko_rt.c` e `src/assert/assert.c` —
  árvore congelada. Fica a perna `actions` (o CI agora é workflow + action composta com
  `contents: write`, que é a classe de coisa que esse analisador existe para ler).

### O que FICA, com ressalva: `branch-policy.yml`

O check chama-se **`Branch policy gate`** e recusa um PR cuja ORIGEM seja `theory/**` ou
`cargo/**` contra base `main`/`remodel/*`. São namespaces do fluxo de **vagão/esteira** que morre
com esta limpeza: sem `theory/*` e sem vagões, o gate é no-op, não regra. **Proposta ao dono:**
apagar `branch-policy.yml` junto com o resto do fluxo antigo, ou — se ele quiser manter uma cancela
de origem — reescrevê-la para o que a org realmente vai proibir (ex.: PR direto de branch de agente
para `main` sem passar por `fix/retirement`). Não foi tocada nesta entrega porque mudar o que um
ruleset pode exigir é decisão do dono, não do implementador.

### Órfãos que sobraram (relatados, não removidos)

`scripts/**` (a maquinaria shell/PowerShell da escada velha: `produce_assets.sh`,
`nightly_tag.sh`, `fixpoint_gate.sh`, `ci_producer_matrix.sh`, `win/*.ps1`, …) e os dois arquivos
de dados que só o `pr.yml` lia — `.github/ci-lane-exceptions.txt` e `.github/sast-baseline.txt` e `.github/lsan-suppressions.txt` —
não têm mais nenhum leitor. Nenhum dos quatro workflows sobreviventes os referencia. Apagá-los é
varredura de `src/` congelado, fora do escopo desta entrega.

### Religar o `release.yml` na org

Ver a armadilha no fim do §3.1: o GitHub identifica workflow por **caminho de arquivo**. Se o
`release.yml` chegar à org herdando o estado `disabled_manually` do *Bootstrap Release*, a tag não
dispara NADA e não há erro nenhum para ler. Conferir com `gh workflow list --all` depois do
primeiro push, e religar por
`gh api -X PUT repos/teko-org/teko-lang/actions/workflows/<id>/enable`.

## 4. Loop local — o `mc` vem da RELEASE, não de submodule

A sandbox remota **não consegue rodar o `mc`** (rede para o GitHub bloqueada, 403);
lá só se valida estaticamente e o CI é o gate. **Localmente roda-se o `mc` de
verdade**, e é onde esta sessão rende mais — o ciclo fecha em ~1 s.

**Instalação (uma vez, e a cada release nova).** Baixa-se o EXECUTÁVEL das releases
de `minicompiler/mc` — **nada de submodule**, e **não se usa binário de dentro do clone do
mc** (pode estar à frente do que o CI usa). Troque `macos-arm64` pelo seu alvo:

```sh
ver=$(cat MC_VERSION); tag="v$ver"
gh release download "$tag" --repo minicompiler/mc --pattern "mc-$ver-macos-arm64.tar.gz*"
shasum -a 256 -c "mc-$ver-macos-arm64.tar.gz.sha256"
mkdir -p ~/.local/mc && tar xzf "mc-$ver-macos-arm64.tar.gz" -C ~/.local/mc
ln -sf ~/.local/mc/mc-$ver-macos-arm64/mc ~/.local/bin/mc
```

O CI PINA a release em `MC_VERSION` (§3.2) — não resolve `latest` por padrão —, então
uma release nova do mc só entra no gate quando o arquivo mudar (o processo de bump está no
§3.2). O binário local segue o mesmo arquivo, para nunca validar contra um `mc` diferente do
que o CI usa.

**Config de host.** O `mc.toml` versionado mira `linux/x86_64`, o alvo do CI, e
não linka neste host. Deriva-se um config em scratch — `os = "macos"`,
`arch = "aarch64"` (`arm64` **não** é aceito; `mc --host` diz o par certo) — sem o
bloco `[linker]`, para o alvo sair pelo backend `macho-exe` embutido:

```sh
sed -e 's#^os   = .*#os   = "macos"#' -e 's#^arch = .*#arch = "aarch64"#' mc.toml \
  | grep -v '^\[linker\]' | grep -v '^cmd  = ' | grep -v '^args = ' > mc.macos.toml
mc build . --config mc.macos.toml
./build/teko-hello; echo $?          # 42
```

**As fixtures**, no mesmo laço que o CI usa — `--entry-only` reaproveita o compilador
ensinado em vez de reconstruí-lo por fixture:

```sh
for src in tests/*.tk; do
  n=$(basename "$src" .tk); w=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
  sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n\"#" \
      mc.macos.toml > "mc.$n.toml"
  build/teko build . --config "mc.$n.toml" --entry-only && "build/$n"
  echo "$n exit=$?  want=$w"; rm -f "mc.$n.toml"
done
```

**Config sempre RELATIVO, cwd no repo** (`mc.macos.toml`, não `/abs/...`): com caminho
absoluto o módulo trata todo arquivo como "fora do projeto" e a checagem de `internal` fica
cega — o CI pegou um defeito que a validação absoluta não via (D224).

Hoje isso dá **45/45 em exit 42/70** (o número de fixtures cresceu desde que este texto foi
escrito; o laço em si não mudou). `mc.macos.toml`, os `mc.*.toml` transientes
e `build/` **nunca se commitam**, e `mc.toml` fica intacto por padrão — só o
**crumb que o autoriza explicitamente** (S1, plano §64/§65: `[compiler]` ganhou `core`/
`modules`; S2, plano §64/§66: `[compiler].out` virou `"build/teko"`) pode tocá-lo, e só
as chaves que esse crumb nomeia. Editá-lo fora de um crumb autorizado quebra o CI. **S4.1**
(módulos `.tk`, §5) tocou `[compiler].modules`/`[package]` por nome de arquivo, não por chave nova
— o laço acima e o `sed` de derivação de host continuam idênticos, o `mc build`/`mc limits`
não distingue `.mc` de `.tk` num módulo.

**O `mc` NÃO emite C.** Ele emite objeto nativo e linka; não existe passo de `gcc`
sobre saída do compilador ensinado. Compile sempre por `mc build DIR --config FILE`
(ver armadilha 1 do §5.1).


## 5. Entrega 4 em curso — estado e fila

Plano executável em `docs/design/plano-ngen-entrega4.md` (leia §1 descobertas medidas,
§6 correção de rota do escopo, §7-§8 C7b/C8/C7c e a fila revista).

**Landados em `fix/retirement`** (18 fixtures verdes, cada crumb com verificação
independente e revalidação pós-cherry-pick):
- **C0** glob do CI aceita `surface_*.tk`; erratas do handoff.
- **C1** default de parâmetro em MÉTODO (`i64 scale(i64 k = 2)`), inclusive em
  assinatura de `interface`; só constante `fold()`-ável; nó clonado por sítio.
- **C2** sobrecarga de MÉTODO por assinatura; slots virtuais chaveados por (nome,
  assinatura); símbolo do 1º método preservado, sobrecargas com sufixo
  (`shape_area__i64`). Resolução por **aridade nível a nível** na cadeia (não é o
  hiding por nome do C#). Mesma aridade com tipos diferentes → erro até o oráculo
  entrar nesse ponto (`tk_method_pick` devolve `-3`).
- **C3** oráculo de tipo estático em `pass()` (`teko_typeof.mc`), consumido quando o
  nome não decide o tipo; costurado com C1/C2; prova de no-op nas 11 anteriores.
- **C7 + C7b** `params` (`i64 total(params xs)`): pacote alocado por sítio na arena,
  `xs[i]` com guard `rt_panic` nos dois lados, reentrante; teto 10 fixos / 12 por sítio.

- **Escopo** (plano §6): tabela de locais com escopo por bloco via `syntax_stmt("{")`
  + `p_blockdepth()` (sem `on_jump` — a pilha é de parse e o parser sempre chega ao
  `}`); busca de membro restrita ao tipo do receptor (`unknown member of A: extra`);
  `-2` desambiguado (`wrong number of arguments for X` ≠ tipo desconhecido); o oráculo
  também com escopo por bloco. Fecha os dois defeitos silenciosos da entrega 3.
- **C4** sobrecarga de FUNÇÃO DE TOPO por `pass()` (`teko_over.mc`, registrado depois
  de `params` e do oráculo): **toda** sobrecarga é renomeada (`pick__i64`, `pick__Vec`),
  nenhuma fica plana — sítio não reescrito vira erro de link, não fallback silencioso.
  Guards: `&f`, colisão ABI com `params`, `extern`/`main`, ambiguidade.

- **C8** genéricos com CONSTANTES por record/replay (`teko_generic.mc`):
  `class`/`struct Name<T, const N: i64>` gravado por `p_skip_balanced` e re-parseado
  por instância (`p_subst_name`/`p_subst_int` + `p_push_source`), memoizado por
  (nome, argumentos), mangling `Box__Circle__4`; a instância entra na tabela de tipos
  pelo mesmo `type_new` de uma classe qualquer. Nasce no primeiro uso: posição de
  declaração (o nome do genérico vira `syntax_stmt`), tipo de campo/parâmetro/retorno,
  e `new`. `>>` desmontado por `p_resplit_punct(1)`. Campo array inline `T items[N]`
  faz o layout crescer com a constante, e `items[k]` com `k` literal fora de `[0, N)`
  é **erro de compilação** na instância (índice não-literal recebe guard de runtime
  `tk_ix`, emitido uma vez e só no programa que indexa).

- **C3b** oráculo tipa `N_BINARY`/`N_UNARY` espelhando `res_binary` do core (usa o
  próprio `cmp_cond`) e membro escalar (`xt_ty`); o `.` deferido vira chamada a
  `tk_unresolved_member` — sem o pass, o `res_call` do core recusa `call to unknown
  function` com `arquivo:linha` (antes: `INT 0` e binário errado em silêncio).

- **C3c** `.` sobre receptor ESCALAR (`b.side.x` com `side: i64`) era SIGSEGV em
  runtime; agora `teko: i64 has no members: x` em compile-time. `tk_ty_struct_of`
  saiu do oráculo — "sem tipo" (−1, deferível) ≠ "tem tipo e não tem membros".
- **C7c** `params` instanciado por `N` constante (cópia de AST, `total__k` por sítio;
  o record TEXTUAL de função é inalcançável — medido, `cannot redefine core keyword:
  i64`): `xs[lit]` fora de `[0, N)` é erro de compilação; não-literal mantém guard.
  Guard de forma `(uptr, i64)` do C4 removido (obsoleto).
- **CI com 5 pernas nativas** (linux x86_64/arm64, macos arm64, windows x86_64/arm64),
  agregador `mc build ngen && run`; sem filtro de `paths:`.

- **C5** sobrecarga de OPERADORES por `pass()` sobre `N_BINARY` (`teko_ops.mc`).
  **SUPERSEDIDO pelo C5b da entrega 5** (D218: a forma com receptor implícito não é mais
  aceita). O que sobreviveu inteiro: a **regra de endereço** — `N_BINARY` construído pelo
  próprio ngen (`ld64(p+OFF)`, `items[i]`) é reconhecido e nunca tratado como operador — e
  o **core+core não se toca**.

**Entrega 4 FECHADA, C6 incluso** (desbloqueado pelo `syntax_param` do mc 0.10.3 — ver
o bloco "C6 LANDADO" logo abaixo da fila da entrega 5).

**Entrega 5 — crumb 0 LANDADO: `this` implícito e `base`** (D219, plano §16), o SWEEP que
vem antes do reclaim e do C5b porque os dois escreveriam código na forma velha:
- `teko_this.mc` (novo) — o receptor que o método não declara. `tk_params`
  (`teko_class.mc`) prepende `this` e RECUSA um parâmetro `self`; `this` é palavra
  (`syntax_expr`) válida só dentro de corpo de tipo; `base` é CONTEXTUAL (só dentro de
  corpo de tipo — segue nome comum em `i64 offset_total(i64 base, ...)`) e `base.m()`
  chama o símbolo da base DIRETO, escolhido por assinatura como o C2 faz.
- **Nome não-qualificado** vira membro de `this` **no pass** (`teko_typeof.mc`, mesma
  caminhada do oráculo): o core entrega um `N_IDENT`/`N_ASSIGN`/`N_CALL` cru e não há
  hook na posição de identificador; o pass é também onde parâmetros e locais são
  legíveis, que é o que a regra do C# exige — **local/parâmetro sombreia o campo**.
  Um campo de tipo classe também vale como RECEPTOR (`inner.v`), e uma chamada
  não-qualificada alcança método declarado ABAIXO (o pass vê o corpo inteiro).
- Limite conhecido: **campo array inline** só pelo receptor escrito (`this.items[i]`) —
  o `[` é rebaixado pelo handler do `.`, que o nome nu não alcança; a recusa diz isso
  (`teko: an array field is reached through \`this.\`: items`).
- As **18 fixtures** foram reescritas na forma nova (`grep -rn self tests` = 0) e a
  AST final de 16 delas é **byte-idêntica** à da forma velha depois de renomear o
  receptor (`name=self` → `name=this`); as duas que divergem são `types_class` e
  `types_interface`, onde o `override` passou a usar `base.area()` (a diferença é
  exatamente a chamada direta no lugar do corpo antigo).

**Entrega 5 — crumb "membros C#" LANDADO** (D220, plano §17), o modelo de membros que o
reclaim (construtor/destrutor `public`) e o C5b (`public static … operator+`) já escrevem:
- **Modificadores como em C#**, em qualquer ordem e antes do tipo
  (`public static i64 f()`): `public`/`private`/`protected`/`static` em MEMBRO,
  `public`/`internal` em TIPO de topo (`class`/`struct`/`interface`/`trait`).
  **Defaults do C#:** tipo sem modificador é `internal`, membro sem modificador é `private`.
- **Palavras:** só `public` e `internal` são reservadas (elas ABREM uma declaração de topo);
  `private`/`protected`/`static` seguem **contextuais**, como `virtual`/`override` — valem
  dentro de corpo de tipo, que é onde este módulo é o parser, e continuam nomes comuns fora.
- **`internal` = código do PROJETO — a regra exata implementada.** O mc não tem unidade de
  compilação (`core-language.md:422`), então a unidade sai da **origem da declaração**:
  *uma declaração é do projeto quando o arquivo de que foi lida é um caminho **dentro do
  diretório do projeto** — o diretório do `mc.toml` que a build usou (`cfg_file`), e, sem
  config (CLI de arquivo único), o diretório do arquivo de entrada.* São EXTERNOS: caminho
  absoluto, caminho que sobe (`../fora/x.tk`) e `#include <bundle>`, que é nome e não caminho.
  Todo nome de arquivo que o lexer produz é normalizado contra o mesmo lugar do config
  (`path_join`/`path_norm`), então um prefixo puro responde, sem syscall e sem heurística.
  Distinguem-se **duas** origens (o projeto e todo o resto), logo `internal` lê-se
  "declaração e sítio de uso têm a mesma origem" — dois pacotes externos distintos não se
  distinguem entre si (o mc não tem identidade de pacote). Local e CI batem: o config é
  `mc.*.toml`, o diretório do projeto é `ngen`, e `<float>`/`<mc/core>` ficam de fora.
  Declaração que **não vem de arquivo** não pergunta: instância de genérico
  (`p_push_source`, cujo "arquivo" é o nome do frame) recebe a origem do **template**, e
  membro copiado de trait vira membro da **classe**, com a origem dela.
- **Checagem em todo sítio:** `.` no parse e no pass deferido, chamada de método, `new`,
  `base.`, lista `: Base, Iface`, `use` de trait, campo estático `Tipo.campo` e nome
  não-qualificado dentro do tipo. `protected` = o próprio tipo e as derivadas; `private` =
  só o próprio tipo. Mensagens: `arquivo:linha: teko: X.m is private` / `is protected` /
  `X is internal to another project`.
- **`static`:** campo vira **um global manglado `Tipo_campo`** — nenhum byte no objeto, e os
  offsets seguintes não mudam (`POINT_SIZE` segue 24 em `types_struct.tk`); método não recebe
  `this` e é chamado por `Tipo.m()`. O nome do tipo passou a ser palavra em **três** posições:
  tipo, expressão (`Tipo.campo`, que o `parse_primary` do core recusaria) e statement — e o
  statement só desvia quando um `.` segue o nome, senão entra no `parse_var` do próprio core,
  de forma que `Point p = new Point;` é o statement que sempre foi.
- **Recusas com mensagem própria:** `this`/`base` em membro estático; membro de instância
  alcançado de método estático; membro estático alcançado por objeto (`p.made`); método de
  instância alcançado por tipo (`Tipo.m()`); `private`/`protected` em membro de interface;
  método estático em interface (a forma C# 8 com corpo não é ensinada); **tipo dentro de
  tipo** (o par excludente do D220 escolheu `internal`, logo não há aninhado); modificador de
  visibilidade repetido; `public` diante do que não é tipo; método não-público implementando
  interface.
- **Fixtures:** as 18 escrevem `public` onde acessam membro de fora; o que só o próprio tipo
  alcança fica sem modificador e prova o default (`items`/`count` de `Box`, os dois `pick`);
  `Counted.n` é `protected`; `types_struct.tk` ganhou o par estático. A AST final de 17 das 18
  é **byte-idêntica** à da base `c9b8c596` — visibilidade é checagem, não muda a árvore —, e a
  única que diverge é `types_struct`, exatamente pelo `static` que entrou nela.

**Entrega 5 — crumb "propriedades + interface v2" LANDADO** (D223, plano §20), o modelo de
membro completo que o reclaim (construtor/destrutor) e o C5b escrevem contra:
- **`teko_prop.mc` (novo)** — `T Nome { ... }` em `class`/`struct`/`trait`, nas três
  formas do C#: **auto** (`{ get; set; }`, com campo de apoio `private` gerado, `Nome__backing`),
  **`=> expressão;` / `=> statement;`** (o `set` é um STATEMENT porque `side = value` é um —
  `=` não está na tabela infixa do core) e **bloco** (`get { } set { }`).
- **Cada acessor é um MÉTODO comum** da tabela do `teko_class.mc` (`get_X`/`set_X`, a grafia
  do próprio C#) — daí saem de graça: **slot de vtable POR ACESSOR** (`override` que redeclara
  só o `get` herda o `set`), `static`, símbolos de sobrecarga, e **visibilidade por acessor**
  (`{ get; private set; }`, nunca mais aberta que a da propriedade).
- **`value`** é o parâmetro do `set` e nada mais (nome comum, sombreável por local); **`get`/`set`
  são lidos SÓ dentro das chaves da propriedade** — nenhuma palavra foi confiscada, e
  `public T get()` de `surface_generics.tk` segue método comum.
- `p.X`, `p.X = e`, `X` nu dentro do tipo e `Tipo.X` (estática) resolvem nos **dois caminhos**
  (`tk_member_of` no parse, `tk_pend_*` no pass).
- **Interface v2:** método com **corpo default** (C# 8) compilado como `iface_m(uptr this)` —
  o itab da classe que não redeclara aponta para esse símbolo; `this` no default é o receptor
  IMPLEMENTADOR tipado como a interface, e todo membro alcançado ali despacha **pelo itab**
  (`this.m()` e o nome nu), então a classe que redeclara é quem responde, inclusive para o
  default que chamou. **`static abstract`** (C# 11): o tipo fornece como `static`, `Tipo.m()`
  resolve em compile-time, e a entrada **não ocupa slot** — o slot passa a ser a posição entre
  os membros de instância (`tk_ifslot`/`tk_ifinst`). Interface também declara **propriedade**
  (`i64 X { get; set; }` = assinaturas). Substitui a recusa de `static` em interface do D220.
- **Recusas próprias:** atribuir a `get`-only; `value` fora de `set`; propriedade sem acessor;
  acessor duplicado; acessor mais visível que a propriedade; auto misturado com corpo; chamar a
  propriedade; tipo que não fornece o `static abstract` ou o fornece como instância; `static`
  sem `abstract`; `abstract` com corpo; acessor de interface com corpo; e o acessor faltante
  **nomeado pela propriedade** (``o `set` de uma propriedade de `I` ``, não `set_X`).
- **Fixtures:** `surface_property.tk` e `surface_iface_default.tk` (20/20 em exit 42); a AST
  final das **18 anteriores é byte-idêntica** à de `5579c34b` — nenhuma usa propriedade nem
  default de interface.
- **Limite conhecido (precisado pelo verificador):** num corpo default de interface, só o
  acesso **`this.X()` explícito** é sensível à ordem (resolve no parse: membro declarado ABAIXO
  dá `unknown member` claro). A chamada **nua** (`area()`) resolve no pass e é insensível à
  ordem — funciona em qualquer posição.

**Entrega 5 — D224 LANDADO** (`b60e7dfe`, 22 fixtures): `abstract class`/membro/propriedade
abstratos como C# (slot de vtable sem corpo; derivada concreta sem `override` é erro nomeando
o acessor); **`partial class`** fecha no primeiro uso ou no fim da unidade (parte depois do uso
é erro); tabelas por posse (`fd_cls`/`vs_cls`/`ci_cls`) — tipo declarado entre partes não
corrompe o layout; base só numa parte antes de membros; interfaces em união; método parcial não.

**Entrega 5 — RECLAIM LANDADO** (D218, plano §14/§15; 23 fixtures): a arena fixa de 4 MiB ganhou
**free list por classe de tamanho** e o **refcount por escopo**, e o `ngen` devolve memória — 1M
`new` num laço não esgota mais nada.
- **Layout:** cabeçalho de **16 B** (vtable@+0, contagem@+8 — o `+8` NÃO estava reservado, os
  campos começavam em 8) e vtable com `&Nome_release` na palavra 0 e a itab na palavra 1
  (`TK_VT_FIXED 2`, o layout do `lx`). É o release na palavra 0 que deixa `rc_dec` liberar um
  objeto cuja classe ele não conhece. As quatro fixtures que afirmam offset foram corrigidas
  (`SHAPE_SIDE` 8→16 etc.); `types_struct.tk` não muda (struct não tem cabeçalho).
- **`Nome(params) { }`** é construtor, sobrecarregável por assinatura, com **`: base(args)`** como
  no C# (e a exigência do C#: base que só declara construtor com argumento tem de ser nomeada).
  `new Nome(args)` escolhe pela contagem de argumentos; `new Nome` sem construtor que case segue
  entregando o objeto zerado — é por isso que as 22 fixtures anteriores não mudaram de forma.
  **`~Nome() { }`** é destrutor (sem modificador, sem parâmetro, um por classe), chamado pelo
  release **antes** dos campos, derivada antes da base.
- **`teko_rc.mc` (novo)** — o passe que injeta o RC. **Vai no PASSE, não no parse** (ver o §5.2
  abaixo: é a decisão que o crumb mandava reportar). Saída de bloco em ordem reversa, `break N`/
  `continue`/`return`, `x = e`, `p.f = e`, `Tipo.f = e`, `x[i] = e` e o `set` de propriedade,
  `rt_drop` para a referência que ninguém pegou, e **temporários** (`rt_park`/`rt_mark`/`rt_sweep`)
  para o valor possuído que cai em posição sem dono — argumento, receptor de `.`, operando.
- **Sem RC nesta fatia (dívida declarada em `lib/rt.mc`):** `struct` (não tem vtable, logo não tem
  release nem contagem) e o pacote de `params` (nasce e morre dentro de uma expressão, não há nome
  para segurá-lo). `rt_live()` conta os dois, então um programa que os mistura com classes vê um
  piso acima de zero em vez de uma resposta errada. Campo `static` de tipo classe guarda a
  referência corretamente, mas nunca é liberado (vive o programa inteiro).

**Entrega 5 — C5b LANDADO** (D218, plano §15/§27; 23 fixtures): operadores refeitos **como C#** —
a forma velha do C5 (receptor implícito) deixa de ser aceita, com mensagem própria.
- **Declaração** `public static T operator<op>(A a[, B b])` em `class` e em `struct`. O operador é
  um membro **estático**: sem `this`, sem slot de vtable, e é a assinatura que diz tudo. Binários
  `+ - * / % == != < <= > >= & | ^ << >>`, unários `- ! ~ +`. O `+` unário TEM SÍTIO (M45, entrega
  5 crumb 0): `teko_ops.mc`'s `tk_unary_plus`, `syntax_expr("+")` + `parse_expr(11)`; sobre um tipo
  do NÚCLEO o pass colapsa o nó no próprio operando (`+x == x`), o gen do núcleo nunca vê o `N_UNARY`.
- **Resolução pelos DOIS operandos** (`teko_ops.mc`, tabela `op_*`): candidatos = operadores
  declarados pelo tipo de QUALQUER operando **e pelas bases dele**; pelo menos um parâmetro tem
  de ser do tipo declarante. Três rodadas, nessa ordem — **exata**, **literal** (a do C4: um
  `N_INT` cai em `i64` na 1ª e em qualquer inteiro do core na 2ª) e **base** (operando de tipo
  DERIVADO num parâmetro da base — zero bits de conversão, o objeto derivado já é um da base).
  Duas declarações na mesma rodada = ambiguidade recusada, **exceto na rodada base**: entre
  `GrandBase`/`MidA`/`Kid` sem redeclarar, `Kid + 2` escolhe o operador de `MidA` (o ancestral
  MAIS PRÓXIMO), pelo "better function member" do C# (§12.6.4) — `tk_op_pick_best` mede a
  distância na cadeia de `base` de cada candidato e só desempata quando um domina o outro nos
  dois operandos; ambiguidade real entre bases não-relacionadas segue recusada. `2 + v`
  (reversed) e `-a` (unário) resolvem por essa mesma máquina.
- **Pares obrigatórios** (`==`/`!=`, `<`/`>`, `<=`/`>=`) checados quando a unidade fecha (no
  `pass`), então `partial class` pode escrever as duas metades em partes diferentes.
- **Visibilidade checada NO SÍTIO** (`tk_check_member`, o achado 3 do crumb de membros que o C5
  não fazia): `Vec.operator+ is private` de fora, aceito dentro do próprio tipo.
- **Rota = `pass()` sobre `N_BINARY`/`N_UNARY`, não `syntax_infix`.** Desde o 0.10.3 o
  `syntax_infix` sobre operador do core FUNCIONA (M41.5), mas continua sendo a rota errada aqui:
  no parse o tipo de um operando que é parâmetro/`.` deferido não existe, e o handler não veria a
  **regra de endereço** do §12 (o `ld64(p+OFF)` que o próprio ngen constrói). O cabeçalho do
  `teko_ops.mc` registra os dois motivos.
- **Posse:** o resultado de um operador que devolve classe é **possuído** — o `tk_xt_put` do pass
  é o que diz isso ao `teko_rc.mc`. Medido com `rt_live()`: `(a+b)==c` não muda a contagem (o
  temporário é parked/sweeped com a statement), `-a` e `2+a` sobem 1 cada (o local segura), e a
  saída do bloco volta a **0**.
- **Recusas próprias:** forma velha (`an operator is static and names both operands`); `==` sem
  `!=`; ambiguidade; `private` de fora; operando sem tipo; `this` num operador; token unário com
  dois parâmetros e vice-versa; 0 ou 3+ parâmetros; nenhum parâmetro do tipo declarante; default
  em parâmetro; retorno `void`; `virtual`/`override`/`abstract`; token não sobrecarregável; e
  `operator` em `interface` (a mensagem confusa que o plano §12 apontou como adjacente).
- **Fixture** `surface_operator.tk` reescrita na forma nova; a AST final das **22 outras é
  byte-idêntica** à de `05dc7181`.

**Entrega 4 — C6 LANDADO** (`syntax_param`, `teko_default.mc`, 24 fixtures): default de
parâmetro em FUNÇÃO DE TOPO, `i64 add(i64 a, i64 b = 10)` → `add(1)` completado em
`add(1, 10)` por um `pass()` (`tk_default_pass`), registrado logo antes do `tk_over_pass`.
- **Duas rotas, uma tabela.** `tk_default_pass` resolve sozinho toda chamada a um nome
  declarado UMA VEZ na unidade (`tk_default_decl_count`, varredura de `root` — NÃO a
  tabela de parâmetros, que só tem linha para declaração com ≥1 parâmetro; uma sobrecarga
  de aridade zero, tipo `tally()` ao lado de `tally(i64)`, nunca aciona `syntax_param` e
  ficaria invisível se a contagem fosse pelas linhas). Um nome declarado mais de uma vez
  (C4) é deixado intocado aqui de propósito: `tk_over_pass` ganhou uma QUARTA rodada
  (`tk_ov_fits_default`/`tk_ov_match_default`), tentada só depois das duas de aridade exata
  falharem — o que dá de graça a regra do C# (§12.6.4.5, "candidato sem default vence"):
  `add(1)` com `add(i64)` e `add(i64, i64 = 10)` resolve para o primeiro na rodada exata,
  sem jamais consultar a tabela de defaults.
- **Reuso total do C1** (`tk_param_default(mark)`, a tabela `df_node`/`tk_ndflt` e
  `tk_fill_defaults`, todos de `teko_class.mc`) — zero regra ou mensagem duplicada; a
  função de topo é só mais um chamador da mesma máquina que método/construtor já usam.
- **Achado que exigiu correção:** `tk_over_pass` renomeia a declaração (`tk_ov_rename`,
  para o símbolo com sufixo) ANTES de resolver qualquer chamada — então a quarta rodada
  não pode casar pela `nd_name(d)` corrente (já mangled); casa pelo `od_name_at(i)`, o
  nome ORIGINAL que `tk_ov_collect` guardou no instante da coleta, antes do rename tocar
  o nó. `teko_default.mc` expõe `tk_default_ndef_of_name`/`tk_default_d0_of_name` (por
  NOME, não por nó) exatamente por isso.
- **Recusas:** `params` com `=` no mesmo parâmetro (`teko: a \`params\` list has no
  default`); `extern` com qualquer default, mesmo que nenhum call-site precise dele —
  checado por declaração, não por chamada (`teko: an extern parameter has no default`);
  `na < nreq` (`teko: <fn> takes at least N arguments`); mais as duas regras herdadas do
  C1 (constante, sem-default-após-default), mesma mensagem.
- **`p_decl_name()` distingue membro de função de topo sem precisar de `p_set_decl_name`
  do C1:** `tk_params` (membros, `teko_class.mc`) tem loop PRÓPRIO e nunca chama o
  `parse_params()` do core, então `syntax_param` simplesmente nunca dispara para um
  parâmetro de membro — zero colisão, zero checagem extra necessária.
- **Fixture** `surface_default_free.tk` (1 e 2 defaults, 0/1/2/3 args, sobrecarga
  sem-default vencendo, chamada dentro de método de classe); AST das outras 23 é
  byte-idêntica à base.

**Entrega 5 — LOOPS LANDADO** (D218/D221/D226, plano §29; 25 fixtures): `while`, `do ... while`
e `for` como em C# (`teko_loop.mc`, novo), rebaixados no PARSE ao `loop`/`if`/`break N` do
núcleo (mantidos, D221) — a mesma forma que `lib/prelude.mc` já mostra, só que via `syntax_stmt`
em vez de `#rule`, para caber o rewrite de saltos abaixo e para um `.tk` cru ganhar as palavras
sem `#include`.
- **`while (c) stmt`** → `loop { if (!(c)) break; stmt }`. O corpo fica na MESMA profundidade que
  o programador escreveu — nenhum `break`/`continue` dentro dele precisa de ajuste.
- **`do stmt while (c);`** → `loop { loop { stmt break; } if (!(c)) break; }`; **`for (init; cond;
  step) stmt`** → `{ init loop { if (!(cond)) break; loop { stmt break; } step; } }`. As duas
  embrulham o corpo num loop-de-uma-volta extra, para que `continue` (que sempre reinicia o loop
  MAIS INTERNO) caia no passo/condição em vez de voltar ao topo do corpo — e é esse loop invisível
  que exige o rewrite: `tk_loop_rewrite_stmt` caminha o corpo ANTES de embrulhar, contando quantos
  `N_LOOP` do PRÓPRIO corpo ficam entre ele e cada `break`/`continue`; um `break k` que já
  ultrapassa o que o corpo abriu (`k > profundidade`) também precisa ultrapassar o embrulho novo
  (`k+1`), e um `continue` na profundidade 0 vira `break 1` (cai onde o `break;` do embrulho
  cairia). Compõe corretamente aninhado (para `for` dentro de `for`, o `break 2` do usuário sai
  com o nível certo mesmo depois de cada camada aplicar seu próprio `+1`) — a fixture
  `surface_loops.tk` prova o caso de dois `for`s com `break 2`.
- **`x++;` `x--;` `x += e;` `x -= e;`**, como statement solto E como passo de `for`: os quatro
  tokens são registrados direto por `word_add` (sem `#token`, evitando o dobro-registro), e a
  forma solta usa a MESMA rota que `+=`/`++` de `lib/prelude.mc` — um `#rule` empurrado por
  `p_push_source` a partir de `user_init()`, antes do primeiro token do programa real (o
  `drv_parse` chama `lex_init` → `user_init()` → `parse_unit()`, nessa ordem, então o push cai
  ANTES do primeiro `next()`). A rota é a mesma proposta como opção A no crumb, mas usando a forma
  do prelúdio em vez de `st64`/`ld64` crus: `x += e;` vira `x = x + e;`, então herda de graça o
  `operator+` que uma classe declarar (C5b) e o RC de `x`, coisa que sintetizar `st64(&x, ...)`
  diretamente NÃO teria (perderia posse/overload). O passo do `for` lê os mesmos tokens
  diretamente (sem passar pelo `#rule`, já que não há `;` de fechamento ali).
- **Fixture** `surface_loops.tk` (25/25 em exit 42): `while` com bloco e com statement único,
  `do...while` com `continue` provando que cai na condição, `for` clássico com `i++`, `for` com
  `continue` que executa o passo, `for (;;)` com `break`, `for` aninhado com `break 2`, `for`
  dentro de método de `class` (o passe de RC caminhando a árvore sintetizada) e `x += 3;`. AST das
  24 fixtures anteriores **byte-idêntica**.

**Entrega 5 — M45 crumb LANDADO** (mc 0.13.0, 25 fixtures): três itens, nenhum novo `.tk`.
- **Adoção do mc 0.13.0**: `p_cp()` público troca a leitura crua de `cp` em `teko_access.mc`'s
  `tk_dot_follows`; `tests/surface_overload_free.tk`'s `chmod` (C ABI, devolve `int`) passa a
  `extern i32` (D5 — uma chamada devolve o que declara, sign-extended); nenhum outro `extern` do
  `ngen` chama C que devolve `int` (`lib/rt.mc` é 100% `callp`). Sem contorno de "region crosses a
  file boundary" a remover — o núcleo corrigiu o falso positivo, e o `teko_generic.mc` nunca teve um.
- **`+` unário** (fecha a dívida do C5b acima): `teko_ops.mc`'s `tk_unary_plus`, registrado por
  `syntax_expr("+")`, lê o operando por `parse_expr(11)` (uma acima da maior precedência infixa) e
  devolve o MESMO `N_UNARY` que o núcleo constrói para `-v`. `tk_ops_unary` resolve `T.operator+`
  quando o operando é de um tipo teko; sobre um tipo do núcleo colapsa o nó no próprio operando
  (`tk_ops_replace`, `+x == x`) — o gen do núcleo nunca vê um `N_UNARY` de `+`.
- **`true`/`false`**: `teko_type.mc`'s `tk_true`/`tk_false`, `N_INT` de 1/0 tipado `TY_I64` (como
  o oráculo já tipa toda comparação/`!`/`&&`/`||`, não `TY_U8`/`bool` — `bool` é a largura de uma
  DECLARAÇÃO, não o tipo de um valor de verdade). `syntax_expr` reserva as duas palavras: `i64 true
  = 1;` é recusado (`name reserved by a syntax/type_alias registration: true`).
- **Fixtures**: `surface_operator.tk` ganhou `operator+(Vec a)` unário e os sítios `+v`/`+i`;
  `surface_loops.tk` ganhou `while (true) { ... break; }` com dois `bool`. AST das 22 fixtures
  realmente não tocadas (todas menos essas duas e `surface_overload_free.tk`) **byte-idêntica**
  contra o compilador da base em `545b26b5`, os dois no mc 0.13.0.

**Entrega 5 — N1 LANDADO** (D218/D226, plano §31/§32; 26 fixtures): `namespace A.B { ... }` e
`namespace A.B;` (file-scoped, C# 10), `using A.B;` e tipos qualificados, `teko_ns.mc` (novo).
- **Nome real = `A__B__Circle`** (`type_new`, sem alias); o nome CURTO nunca é `type_alias`
  (`alias_find` responde só o último registrado em silêncio — colidiria entre namespaces). É
  registrado como palavra própria (`syntax`/`syntax_stmt`/`syntax_expr`, as mesmas duas últimas
  que `tk_type_word` já usa) e a identidade sai da lista de busca **no sítio de uso**: namespace
  corrente para fora, prefixo a prefixo, e só então os `using` do ARQUIVO — nunca memoizada.
- **Declaração nunca busca:** qualifica com o namespace corrente e procura EXATO
  (`tk_ns_qualify`, chamado uma vez por construto que declara tipo — `class`/`struct`/
  `interface`/`trait`); fora de um namespace é a identidade, então nenhum programa sem
  `namespace` muda de forma. O reopen check de `partial` (`teko_class.mc`) precisou da mesma
  qualificação (read-only, `tk_ns_qualified_name`) para não fundir dois `partial class Foo` de
  namespaces diferentes.
- **Qualificado (`geo.Circle`, `geo.Circle.made`, `new geo.Circle()`)** resolve pelo 1º SEGMENTO
  (`geo`), lido com `p_name()`+`p_next()` — nunca `p_ident()`, pois o segmento é palavra reservada
  a partir do 2º uso. `tk_ns_walk` cresce o nome acumulado por `.` enquanto o que já foi lido é um
  namespace conhecido, e para no instante em que vira um tipo declarado — `tk_static_member`
  (já existente) consome o resto (`.made`, `.tally()`).
- **`tk_struct_find` ganhou um fallback** (namespace corrente + `using`s do arquivo,
  `tk_ns_resolve`) que responde -1 sem custo quando o programa não usa namespace nenhum — mas o
  scan original teve de virar `tk_struct_find_exact`, chamado por todo sítio que testa uma
  string que ELE MESMO construiu (dentro de `teko_ns.mc`, e o reopen check/`tk_gen_close`), pois
  `tk_struct_find` chamando de volta `tk_ns_resolve` sobre seu próprio candidato recursa sem
  convergir (medido: estouro de pilha via `lldb bt`, ver plano §32 item 1).
- **`tk_newname` (teko_struct.mc) aceita uma palavra namespaced curta já reservada** por OUTRO
  namespace (`mesh { class Circle }` depois de `geo { class Circle }`), checando o exato
  qualificado antes de aceitar — o mesmo texto reservado por dois namespaces diferentes não é
  duplicata; pelo mesmo texto no MESMO namespace, ainda é.
- **`tk_ns_top`** é a única posição de tipo que o core lê sem hook nenhum (`Circle f(Circle c)`
  no topo, `syntax(curto, ...)`); `tk_gen_ty` (teko_generic.mc) e `tk_default_param`
  (teko_default.mc) ganharam o mesmo ramo namespaced ANTES de `p_type()`/`type_of_token` para o
  campo/parâmetro/retorno de membro e o parâmetro de função livre, respectivamente.
- **Achados/correções do próprio crumb** (plano §32): `tk_type_stmt`/`tk_type_expr` precisaram
  de um `if (si < 0) err_at2(...)` explícito (liam lixo fora da tabela sem); `tk_ns_seg_stmt` usa
  `p_id() != tk_ns_dot` (não `tk_dot_follows`, que responde outra pergunta depois que o nome
  qualificado inteiro já foi consumido); `tk_new` precisou reler `name = sr_name_at(si)` antes de
  montar o símbolo do alocador (senão `new Circle()` bare, resolvido via `using`, chamava
  `circle_new` em vez de `geo__circle_new`); `tk_gen_declstmt` cedeu seu próprio rabo
  (`tk_var_after_type`, agora em `teko_ns.mc`) para `tk_ns_seg_stmt` reusar — nenhum dos dois pode
  entregar o tipo já resolvido ao `parse_var` do core, que insiste em consumir a palavra-tipo ele
  mesmo, e os dois já consumiram mais de um token antes de saber o tipo.
- **Dívidas registradas (não escondidas):** cast de nome curto namespaced; `global`/`extern`/
  `main` dentro de namespace FILE-SCOPED (só o BLOCO é pego, pelo laço que este módulo controla);
  generic declarado dentro de namespace continua com nome CURTO simples (D31.14); instanciação de
  genérico qualificada; `using`/namespace fora do topo do arquivo sem checagem de ordem.
- **Probes de recusa** (fora de `tests/`): dois `using` ambíguos
  (`teko: ambiguous name Circle (geo, mesh)`); `namespace` sem `{` nem `;`; tipo curto sem
  `using` nem qualificação (`teko: unresolved name: Circle`); `extern`/global/`main` dentro de
  bloco de namespace; `partial` reaberto em outro namespace (confirmado: NÃO funde, cada um só
  enxerga seu próprio campo); `using` dentro de bloco (recusa natural do core, `using` só existe
  em posição de topo).
- **N1b (errata, plano §32):** a lista `:` de base/interface e o `use` de trait namespaced,
  ambos furos abertos pelo verificador do N1, fechados — `tk_conf_name` e `tk_use` leem o nome
  por `p_name()`+`p_next()` (D31.3) em vez de um único `p_ident()`.

**Entrega 5 — N2 LANDADO** (D218/D226, plano §31/§32/§33; 27 fixtures): função livre declarada
dentro de um namespace, mangled e resolvida por um `pass()` (`teko_ns.mc`'s `tk_ns_pass`).
- **Duas varreduras.** Sweep 1 mangla toda função livre/protótipo namespaced (bloco OU
  file-scoped) para `geo__area`, ANTES de qualquer pass que censa por nome (`params`, oráculo,
  operadores, defaults, sobrecarga); sweep 2 resolve o sítio não-qualificado pela lista de busca
  (o namespace corrente do SÍTIO, prefixo a prefixo, e só então os `using` do arquivo — D31.6),
  reescrevendo só quando o candidato qualificado EXISTE (`decl_find`, D31.10 — `rt_alloc`/uma
  função plana chamada de dentro de um namespace fica achatada). O namespace de um bloco é
  anotado no PARSE (`tk_ns_decl_note`, no laço de `tk_namespace`, por identidade do nó); o de um
  sítio sai de graça do próprio nome, JÁ mangled pelo sweep 1 (`tk_ns_of_name`, novo, extrai
  `geo` de volta por prefixo — o mais específico, para `A`/`A.B` coexistirem).
- **`geo.area(x)` qualificado NÃO passa pelo pass:** `tk_ns_seg_expr`/`tk_ns_seg_stmt` do N1 só
  resolviam tipo; ganharam `tk_ns_qualified_call` — quando o segmento não é um tipo mas É um
  namespace conhecido, o nome cheio é montado DIRETO (sem `decl_find`, que não veria uma
  declaração ainda não mangled nem uma escrita mais abaixo no arquivo).
- **`teko_default.mc` ganhou `tk_default_rename`:** a tabela de defaults de função livre é
  chaveada por PONTEIRO do nome no instante do parse (`fpd_name`); sem mover essa chave junto com
  `set_nd_name`, o default de uma função namespaced sumia (`tk_default_row_of_name` nunca achava
  a linha, e o `teko_over.mc`'s quarto round também não).
- **Achados/correções:** um global SINTETIZADO por uma classe namespaced (o `_vt`, emitido bem
  depois do parse por `tk_class_close`) caía na recusa "global fora de todo namespace" quando a
  varredura de sweep 1 passou a alcançar TODO nó de topo — a guarda usa o MESMO `tk_ns_of_name`
  (só recusa um global que ainda NÃO carrega prefixo); a guarda de colisão de identidade (§31 (c))
  não vale contra `decl_find` para função — duas assinaturas de uma função namespaced (C4)
  aterrissam de propósito no mesmo nome cheio, e só a tabela de TIPOS é checada. O **furo do
  destrutor** que o verificador do N1 achou: `teko_class.mc`'s `tk_member_dtor` comparava o token
  que a fonte escreveu (o nome CURTO, `Base`) contra o nome QUALIFICADO (`geo__Base`) — nunca
  batia. Corrigido com `tk_ns_short_of` (novo, o inverso de `tk_ns_of_name`); o CONSTRUTOR já
  estava correto (`tk_gen_ty` resolve por TIPO, não por palavra), mas a mesma classe de bug
  também escondia o diagnóstico `void Name(...)` (C#'s own mistake) logo abaixo — corrigida junto.
- **Fixture** `surface_namespace_fn.tk` (27/27 em exit 42): qualificado + sobrecarga C4, bare de
  dentro e de fora (via `using`) do namespace, default C6 bare e qualificado, função namespaced
  chamando uma plana (achatada), construtor E destrutor pelo nome curto com `: base(v)`. AST das
  26 fixtures anteriores **byte-idêntico** (prova de no-op). Probes fora de `tests/`:
  `main`/`extern`/global em namespace FILE-SCOPED; dois `using` ambíguos; chamada sem namespace
  nem `using` (erro do core); `rt_live()` bare de dentro de um namespace; `void Name(...)`.

**Entrega 5 — N3 LANDADO, série namespace fechada** (D218/D226, plano §31/§34; 28/28 em exit
esperado — hello.tk + 27 fixtures do glob): `import A.B;`, sugar sobre o `lex_include` do core
mais um `using A.B;` implícito, e dois consertos de dívida do verificador do N2.
- **`teko_ns.mc`'s `tk_import`** — `lex_include(tk_ns_path_of(full), line)` com o contrato do
  lookahead (`lex_include` chamado ainda sobre o `;`, `p_next()` só depois); once-only pelo
  `lex_seen` do próprio core (uma reabertura não empurra o arquivo de novo, só repete a `using`,
  inofensivo). `tk_ns_path_of` reusa o mesmo scanner de `tk_ns_dotted` (`tk_ns_sep_replace`),
  trocando "__" por "/" e sufixando `.tk`. Recusado dentro de um bloco de namespace aberto
  (mesmo guard de `tk_namespace`) e recusado quando o ARQUIVO do `import` já declarou um
  namespace seu (D31.13, "no topo, antes de qualquer namespace") — a tabela nova `nsd_file`
  marca isso por `p_file()` no instante em que `tk_namespace` roda, então um namespace
  declarado dentro do arquivo IMPORTADO (outro arquivo) nunca marca o importador, e o
  once-only da segunda `import` nunca reroda o corpo pra marcar duas vezes.
- **Item 2 (dívida do verificador do N2) — `&f` de função em namespace.** `tk_ns_walk_calls_in`
  (sweep 2) só reescrevia `N_CALL`; estendido a `N_ADDR` (o nó de `&nome`, que carrega o nome
  bare do mesmo jeito que uma chamada) — `tk_ns_rewrite_call` já lê/escreve por `nd_name`,
  então o mesmo rewrite serve os dois sem código novo. A forma QUALIFICADA (`&geo.f`) precisou
  de ensino: o core exige que o operando de `&` seja `N_IDENT`, e `tk_ns_qualified_call` só
  sabia montar `N_CALL`. Agora, quando o nome não é seguido de `(`, devolve `tk_id(full)` (um
  `N_IDENT` bare com o nome cheio) em vez de errar "unresolved qualified name" — mesma filosofia
  do D31.10 (uma referência que não existe chega ao linker faltando, não é checada aqui).
- **Item 3 — convenção de mensagem.** `teko: a constructor is written without a return type:
  geo__Circle` interpolava o nome QUALIFICADO onde o dev escreveu o CURTO (o nome de uma classe
  dentro do próprio corpo dela nunca carrega o namespace) — corrigido com `tk_ns_short_of`.
  Convenção adotada, aplicada em `teko_class.mc` e `teko_trait.mc` (grep completo dos dois):
  **nome da PRÓPRIA declaração** (a classe sendo lida agora, `tk_member`/`tk_base_init`/
  `tk_class_reconf`/`tk_class_reopen`) mostra o **CURTO** (`tk_ns_short_of`), porque o dev nunca
  o escreve qualificado; **nome REFERENCIADO** (o `:` de base/interface em `tk_conf_name`, o
  `use` de trait em `tk_use`, e as mensagens de `base(...)` sobre a base em `tk_base_ctor_call`/
  `tk_base_init`) mostra **`A.B.Nome`** (`tk_ns_dotted`, novo — reusa o mesmo scanner de
  `tk_ns_path_of`), porque é exatamente o que o dev pode ter escrito. `nm`/`full`/`acc` (o texto
  cru, "__"-juntado) seguem dirigindo toda resolução; só o argumento passado à mensagem muda.
  (`teko_access.mc`'s `tk_deny_member`/`tk_check_member` — a mensagem `X.m is private` — usa um
  formato próprio, hífen entre `sr_name_at(owner)` e o membro; fora do grep pedido pelo crumb,
  registrado como achado adjacente, não tocado aqui.)
- **Fixture** `surface_import.tk` + `tests/parts/geo.tk` (com `namespace parts.geo;`
  file-scoped): `import parts.geo;` DUAS vezes (once-only), `Circle` sem modificador
  (`internal` por D220) alcançada de dentro do projeto, forma qualificada
  (`parts.geo.Circle`/`parts.geo.twice`) e bare via o `using` implícito, `&twice`/
  `&parts.geo.twice` cada um passado a `callp`. AST das **27 fixtures anteriores** (as 26 do
  glob + `hello.tk`) **byte-idêntico** contra o compilador da base `5e401b01`; `mc limits ngen`
  `ok`. Probes fora de `tests/`: `import` de namespace sem arquivo (`mc: cannot open:
  .../nope/here.tk`, a mensagem crua do core); `import` dentro de `namespace { }` (recusa);
  `import` depois de um `namespace` no MESMO arquivo (recusa).

**Entrega 5 — N3b LANDADO** (plano §35, correção de bug real do verificador do N3): a ordem de
resolução de um nome bare era furada em dois pontos -- `&f` reescrevia para a função `geo.f`
mesmo com uma LOCAL `f` em escopo, e uma chamada `f(...)` fora de qualquer namespace perdia, em
silêncio, para o `geo.f` que um `using geo;` trazia mesmo com uma `f` PLANA de topo já visível.
Ordem final: local/parâmetro → namespace corrente e prefixos (D31.6, inalterado) → SÓ se isso não
achar nada, uma declaração plana de topo do nome exato → os `using`s do arquivo (um `using` nunca
vence o que já era visível sem ele). `tk_ns_walk_calls_in` ganhou o mesmo escopo por bloco que
`teko_typeof.mc` já mantém para seu próprio passe (`sc_name`/`tk_nscope`, reusado, não uma
terceira tabela).

**Entrega 5 — N3c LANDADO** (plano §35, correção de bug real do verificador do N3b): um membro do
tipo corrente (método, inclusive herdado da base, inclusive estático) vencia em C#, mas perdia
para o `using` porque `tk_ns_pass` reescrevia a chamada bare ANTES de `tk_this_call` sequer ver o
nome; ordem final: local/parâmetro → membro do tipo corrente → namespace corrente e prefixos →
declaração plana de topo → `using`s (`tk_ns_call_cls`, lido de `teko_class.mc`'s `tk_method_of_fn`/
`tk_method_named_find`, os dois já usados por `teko_this.mc` para o mesmo passe posterior).

**Entrega 5 — CONST LANDADO** (D218, plano §36; 29 fixtures): `const` como açúcar sobre o `#define`
do mc, `teko_const.mc` (novo).
- **Mecanismo:** NEM `do_directive()` NEM `p_push_source`/laço-de-topo — o handler `syntax("const",
  &tk_const_top)` chama `fold(parse_expr(0))` (o mesmo que `#define` chama) e `def_add(nome, valor,
  linha, arquivo)` DIRETO, o mesmo par que o `enum` demo do próprio mc usa (`mc docs/reference/
  hooks.md` § `syntax()`). Zero indireção: nenhum texto é montado/empurrado.
- **Topo:** `const i64 N = 10;` — fora de namespace o nome do `#define` É o nome curto, então o
  `parse_primary` do NÚCLEO já resolve toda referência bare (inclusive como tamanho de array —
  `parse_dim` chama `fold(parse_expr(0))` também — e em expressão comum) sem código nenhum daqui.
  Dentro de `namespace geo { }` o nome é qualificado ANTES do `#define` (`tk_ns_qualified_name`,
  NÃO `tk_ns_qualify` — esta última reservaria a palavra curta como TIPO, errado para uma
  constante): `geo.N` resolve no PARSE (`tk_ns_qualified_call`, o mesmo segmento de `geo.area(x)`,
  agora também respondendo por const) e `N` bare de dentro de `geo` (ou via `using`) resolve num
  PASSE novo (`tk_ns_rewrite_ident`, estendendo `tk_ns_walk_calls_in` para `N_IDENT` — uma chamada
  é resolvida por RENOME porque o núcleo procura o símbolo em tempo de lowering, mas um const não
  tem essa procura: o nó é SUBSTITUÍDO por um `N_INT`, o mesmo que `parse_primary` teria construído).
- **Genérico:** `Box<T, const N: i64>` instanciado com o NOME de um const (`Box<Circle, N>`) —
  `tk_gen_targs` lia só `T_INT` cru; ganhou um ramo que aceita `T_IDENT`, procura bare e depois
  qualificado pelo namespace corrente na tabela de `teko_const.mc`, e lê o valor já dobrado (sem
  reparsear).
- **Membro:** `public const i64 MAX = 4;` — `const` entra em `tk_member_mods` como mais um modificador
  contextual (`static const` é recusado: "already static"); `tk_member_const` (teko_const.mc) lê
  tipo+nome+valor e registra `Tipo__MAX` (dois underscores, distinto do `Tipo_campo` de um static
  field). `Nome.MAX` de fora entra em `tk_static_member` (checado ANTES de field/método, já que um
  const não ocupa slot nenhum); `MAX`/`STEP` bare de dentro entram em `tk_this_ident`'s fallback
  (`tk_this_const`, mesma prioridade que um field), visibilidade pelo `tk_check_member` de sempre.
  Não há RC nem layout — `const` nunca ocupa slot; herda pela cadeia de base como um field.
- **Local recusado, de propósito:** `teko_stmt.mc`'s `tk_stop_const` (a posição de ESTATUTO, dentro
  de corpo de função) segue reservada, mensagem própria — `#define` é uma tabela única do programa,
  sem escopo de local.
- **Achado (não previsto pelo crumb, registrado):** a redefinição de um `const` de topo NÃO cai no
  `duplicate #define` de `def_add` — `p_ident()` do núcleo já recusa ANTES, com `name already
  defined by #define` (o guard embutido em toda leitura de nome de declaração, `check_def()`).
  Mensagem diferente da esperada, mas 100% do núcleo, igualmente clara.
- **Fixture** `surface_const.tk` (29/29 em exit 42): const de topo em expressão, como tamanho de
  ARRAY GLOBAL (`i64 arr[SIZE];` — um array LOCAL com `[i]=v;` não é suportado por este compilador,
  achado adjacente, não é regressão do `const`: `[` em posição de expressão é `teko_params.mc`'s
  `tk_bracket`, que só resolve um campo-array ou uma lista `params`; um `N_INDEX` sobre array
  comum nunca é lowered — dívida pré-existente, registrada abaixo), como argumento `const N` de
  genérico (`Box<i64, SIZE>`), const em `namespace geo` bare e qualificado, const membro `public`
  acessado `Nome.X` de fora e bare dentro, `private const` bare dentro. AST das **28 fixtures
  anteriores** (as 27 do glob + `hello.tk`) **byte-idêntica** contra o compilador da base
  `8adf0f93`. `mc limits ngen` `ok`. Probes fora de `tests/`: `static const` ("already static; drop
  static"); local const (a mensagem acima); `private const` de fora ("Foo.SECRET is private");
  atribuição a um const de topo (`N = 5;` → o núcleo recusa com `left side of assignment must be a
  name`, porque `N` já virou `N_INT` no parse — nunca chega a ser um `N_IDENT` atribuível);
  redefinição (achado acima); valor não-constante (`const i64 N = g();` → "teko: const requires a
  constant expression", mensagem própria).
- **Dívida nova:** um array LOCAL comum (`i64 arr[N];` dentro de função, fora de struct/`params`) não
  suporta `arr[i] = v;` — o `[` cai em `teko_params.mc`'s `tk_bracket`, que só sabe lowerar um campo
  de array (`this.items[i]`) ou uma lista `params`; sobre qualquer outro array o `N_INDEX` que ele
  constrói nunca é resolvido (nem lido nem escrito), e uma ATRIBUIÇÃO a ele já é recusada no parse
  (`left side of assignment must be a name`, o núcleo exige `N_IDENT`). Pré-existente ao `const`
  (achado ao testar o array de tamanho fixo); registrado, não fechado aqui.
- **Dívidas herdadas dos verificadores N3b/N3c (ainda não constavam, registradas agora):** `&campo`
  bare dentro de um método não resolve (só `&funcao` livre passa por `tk_ns_rewrite_call`/`N_ADDR`);
  um membro `private` da BASE bloqueia com erro de visibilidade em vez de cair no fallback `using`
  quando um `using` traria um candidato de mesmo nome de outro namespace — o gate de acesso roda
  ANTES da decisão "achou member, tenta using", então a mensagem é "is private" em vez de resolver
  pelo `using`. Nenhum dos dois é tocado por este crumb.

**Entrega 5 — TERNÁRIO LANDADO** (D228, plano §37; 30 fixtures): `c ? a : b`, associativo à
direita, mesma precedência de `||` (`teko_ternary.mc`, novo).
- **Mecanismo:** o núcleo não tem controle de fluxo em posição de expressão, então
  `syntax_infix("?", TK_TERN_PREC, &tk_tern_infix)` só constrói um PLACEHOLDER — chamada a
  `tk_ternary(c, a, b)`, o mesmo truque de `tk_defer_member` (se o passe não rodar, o núcleo
  recusa `call to unknown function`, nunca miscompila). `TK_TERN_PREC` é **1**, não 0 —
  `syntax_infix` recusa precedência fora de 1..100, e 1 já é a linha mais baixa da tabela
  (empatada com `||`); ler `b` com `parse_expr(TK_TERN_PREC)` (o MESMO piso, não piso+1) é o
  que dá a associatividade à direita.
- **Posição do passe: logo DEPOIS de `tk_typeof_pass`, ANTES de `tk_ops_pass`** — não entre `ns`
  e `params` como o crumb sugeria de partida. Motivo: `tk_ty_of` (o oráculo que tipa os braços)
  só responde um `.` sobre receptor que o parser não tipou depois que `tk_typeof_pass` já
  reescreveu o placeholder deferido no load/call que ele representa; rodar antes faria um braço
  com `.` deferido responder "tipo desconhecido" em vez do tipo real do campo. `teko_rc.mc` roda
  por último pelo MESMO motivo ("depois que o oráculo resolveu todo acesso deferido..."). `params`/
  `typeof` não perdem nada rodando antes do ternário — nenhum dos dois olha a FORMA da árvore
  (bloco/if/statement), só censo por nome e tipo — e `ops`/`default`/`over`/`rc`, todos DEPOIS do
  ternário, passam a ver `if`/local comuns, nenhum precisa saber que um ternário existiu.
- **Braços preguiçosos com aninhamento:** `tk_tern_lower` hoista `c` (junto do `if` que ele mesmo
  dirige — roda sempre, então não custa nada) e reduz `a`/`b` cada um DENTRO do seu próprio ramo,
  antes de tipar — um ternário aninhado num braço (`c ? (x?y:z) : w`, ou o encadeamento à direita
  `c1 ? a : c2 ? b : d`, que vira exatamente `tk_ternary(c1, a, tk_ternary(c2, b, d))`) hoista de
  dentro pra fora, mas o `if` interno cai DENTRO do ramo externo — preguiça sobrevive ao
  aninhamento. Provado por probe (não fixture): `0!=0 ? side(1) : (1!=0 ? side(2) : side(3))`
  chama `side` uma vez só.
- **Condição de `while`/`for`:** cai dentro do bloco do corpo do loop (o `if (!c) break;` que
  `teko_loop.mc` já constrói), reavaliada a cada volta — provado na fixture.
- **`return`/`if` sem chaves:** `tk_tern_branch` embrulha o statement solto num bloco só quando ele
  de fato hoistou algo, a mesma cerca que `teko_rc.mc`'s `tk_rc_branch` já usa para um temporário
  parked.
- **Tipo:** os dois braços do MESMO tipo pelo oráculo, senão `teko: the two arms of ?: have
  different types`; objeto teko nos dois braços funciona igual (o temporário é uma local comum, o
  passe de RC — que roda DEPOIS do ternário — trata como qualquer outra).
- **Fixture** `surface_ternary.tk` (30/30 em exit 42): inicializador, argumento, encadeamento à
  direita, preguiça com contador, condição de `while`, braços de objeto (`rt_live()` prova que
  nenhum objeto novo nasce só para a escolha), `return` dentro de `if` sem chaves, dentro de
  método. AST das **29 fixtures anteriores** byte-idêntica contra o compilador da base. `mc limits
  ngen` `ok`. Probes fora de `tests/`: braços de tipos diferentes (`i64`/`f64`) → recusa; `?` sem
  `:` → "expected ':' in a ternary"; `a ? b` sem `:` como statement solto → mesma recusa; ternário
  como lado esquerdo de atribuição → recusa do núcleo ("left side of assignment must be a name").
- **Dívida documental do `const` ainda aberta (registrada no §5, não fechada aqui):** o array LOCAL
  comum sem `[i]=v;` (achado do crumb `const`) segue sem fechamento — fora do escopo do ternário.

**Entrega 5 — SWITCH LANDADO** (D222/D228, plano §19/§38; 31 fixtures): as duas vertentes do C#,
`teko_switch.mc` (novo).
- **Statement** (`syntax_stmt("switch")`) rebaixa, no PARSE, a um `loop` de uma volta: `x` lido
  UMA vez (`i64 $t = x;`, o 1º statement do loop), um `if` por grupo de rótulos que compartilha um
  corpo (`case 2: case 3: … break;` — rótulo vazio cai no próximo; corpo não-vazio tem de terminar
  em `break`/`return`/`continue`/`break N`, senão `teko: control cannot fall out of a case`),
  `default` (em qualquer posição — movido para o FIM da sequência de `if`s, C#), `case <const> when
  <cond>:` (sem pattern de tipo — só constante+guarda opcional) e um `break;` incondicional final
  que fecha o loop mesmo sem match. `case` duplicado (mesmo valor, sem guarda) e `default` duplicado
  são recusados; `default` combinado com um `case` no MESMO grupo gera as duas coisas (o `if`
  posicional E o corpo clonado como fallback, `tk_clone_list`).
- **`break`/`break N` no corpo do case NÃO são reescritos** — o loop do switch já É o nível que a
  fonte enxerga, então um `break;` cru já sai do switch; um `break N` que alcança mais longe é
  pego pelo rewrite de um `do`/`for` EXTERNO (`tk_loop_rewrite_stmt`), que enxerga o loop do switch
  como só mais um `N_LOOP` descoberto — a MESMA composição que já vale para loop-dentro-de-loop
  (plano §29). Prova: `break 2` atravessando um `switch` dentro de um `for` na fixture.
- **`continue` dentro de um case reescreve, não recusa mais** (mc 0.14.1, `continue N`; crumb
  "adoção do 0.14.1", `tk_switch_rewrite_continue_stmt`): um `continue k` na profundidade 0 do case
  vira `continue k + 1`, a MESMA regra de `break` (`tk_loop_rewrite_stmt`), nunca convertido a
  `break` — o loop do switch é de uma volta só, continuá-lo direto é sempre seguro. Um `switch` sem
  laço envolvente é interceptado com mensagem própria em vez do "continue out of range" cru do
  núcleo. Um `continue` dentro de um loop que o PRÓPRIO corpo do case abre passa normalmente.
- **Errata (crumb "guarda do continue sem laço", 2026-09-05):** a checagem acima corria no PARSE
  (`tk_realloop_depth`, só `while`/`do`/`for`) e dava falso positivo num `loop { }` cru envolvente
  (o núcleo não avisa módulos de laço bare); movida para um `pass()` (`tk_switch_guard_pass`,
  registrado logo após `tk_ternary_pass`, ANTES de `tk_rc_pass` — esse relocaliza o `continue` pra
  um índice de nó novo ao envolvê-lo em release, plano §38 detalha) que enxerga TODO `N_LOOP`
  igual, bare incluso; só olha um `continue` bare marcado (`sw_bare`, `teko_switch.mc` — nunca um
  campo do próprio nó, que corromperia `--dump-ast`/`tk_clone`) contra o `N_LOOP` que o número
  alcança, contando um `loop {}` de switch (`nd_val=TK_SWITCH_LOOP_MARK`) como inválido também.
- **Expression** (`x switch { 1 => a, 2 or 3 => b, _ when c => d, _ => e }`, `syntax_infix("switch",
  TK_TERN_PREC)`, D228): NENHUMA máquina própria — constrói a MESMA cadeia de placeholders
  `tk_ternary(...)` que `teko_ternary.mc`'s `?:` constrói, dobrada da ÚLTIMA armação para trás; a
  condição da última armação NUNCA é testada (é a base incondicional da cadeia) — por isso exige-se
  ao menos um braço `_` em algum lugar (`teko: a switch expression needs a` _` arm` se faltar),
  idealmente o último escrito. `or` só entre constantes (sem patterns). `x`, se não for um nome
  simples, é lido uma vez via um `N_VAR` real embutido NO MEIO da expressão (`teko_switch.mc`'s
  `tk_switch_xleft`) — hoisted pelo MESMO passe do ternário (`teko_ternary.mc` ganhou
  `tk_tern_hoist_var`, reconhecendo um `N_VAR` embutido como um segundo tipo de placeholder, ao
  lado de `tk_ternary`; hoisted incondicionalmente, igual à condição `c` de um ternário comum).
  Prova de avaliação única: `switchval(v) switch {...}` incrementa um contador exatamente 1×.
- **Achado no registro:** `when` já estava reservado (`syntax_stmt("when", &tk_stop_when)`, entrega
  1) — `tk_kw("when")` (que só casa `T_IDENT`) nunca bate contra a palavra reservada; corrigido para
  `tk_word("when")` (`teko_class.mc`, "a mesma pergunta para uma palavra que TAMBÉM pode estar
  reservada"). E `syntax_infix` já entrega o operador CONSUMIDO ao handler (`hooks.md` § syntax_infix
  — "the operator already consumed") — `tk_switch_infix` não deve chamar `p_next()` de novo (o
  `tk_tern_infix` do ternário já não chamava; o erro apareceu como "expected { after switch" comendo
  o `{` de verdade).
- **Fixture** `surface_switch.tk` (31/31 em exit 42): `case` múltiplo, fall-through de rótulos
  vazios, `default` fora de ordem combinado com um `case`, `when`, `const` como rótulo, `break 2`
  atravessando um `for`, `switch` dentro de método (`Bucket.describe`), expression em inicializador
  E em `return`, aninhada (o valor de um braço é outro `x switch {...}`), `or`, `when`, braços de
  objeto (`rt_live()` prova que a escolha não aloca). AST das **30 fixtures anteriores**
  byte-idêntica contra o compilador da base `3f223f9a`. `mc limits ngen` `ok` (`intrin` segue 8/8 —
  nenhum registrado). Probes fora de `tests/`: braço sem `break` → recusa; `case` duplicado →
  recusa; `case` não-constante → recusa; `switch` expression sem `_` → recusa; `continue` dentro de
  `switch` → recusa (decisão acima); `switch` sem `{` → recusa do núcleo.
- **Dívida registrada:** um `case`/braço namespaced (`geo.N`) não resolve como rótulo — o `#define`
  do núcleo dobra um nome BARE no parse, mas um const namespaced só resolve num passe posterior
  (`teko_ns.mc`), depois que o rótulo já teria de estar dobrado; fora do escopo deste crumb. Um
  `when` guardando o braço `_` TEXTUALMENTE ÚLTIMO da expression não é testado (é a base
  incondicional da dobra) — escrever a guarda no último braço é ignorado; documentado, não
  fechado (o núcleo não tem exceção em runtime para cobrir o caso sem match).

**Entrega 5 — ARRAYS FIXOS LANDADO** (plano §39; 32 fixtures): `T a[N];` local e global, `a[i]`,
`a[i] = e`, `a[i] += e`/`-=`/`++`/`--` e `a.Length` — o núcleo já lê a DECLARAÇÃO (`N_VAR`/
`N_GLOBAL` com `nd_val` = a contagem, `language.md` § Locals/§ Globals); o `[`/`.` são só deste
crumb (`teko_array.mc`, novo). Um fix pequeno do `switch` num commit separado (abaixo).
- **LOCAL, resolvido no parse** — a mesma máquina do campo-array de `teko_struct.mc`. O `N_VAR` de
  um array é observado por um SEGUNDO `on_stmt` (`tk_arr_on_stmt`, ao lado do `tk_on_stmt` que já
  existia) e registrado numa tabela própria (`av_*`/`tk_narr`), com escopo por bloco: `tk_block`
  (`teko_stmt.mc`) ganhou uma SEGUNDA marca/restauração (`amark`/`tk_narr`), ao lado da que já
  cuidava de `tk_nlocal`. `tk_bracket` (`teko_params.mc`, o mesmo dono do `[` desde o C7) checa a
  tabela ANTES do fallback de `params`; achando, resolve tudo ali — leitura, `=`, `+=`/`-=`/`++`/
  `--` (`tk_arr_index_of`) — sem deixar nó pendente.
- **GLOBAL, resolvido num `pass()`** — `on_stmt` não vê declaração de topo (`hooks.md` § on_stmt) e
  não existe hook público sobre uma; um `pass(&tk_array_pass)`, registrado ANTES de
  `tk_params_pass`, varre `nnodes` por `N_GLOBAL` com `nd_val != 0` (`tk_garr_collect`). Uma
  LEITURA que o parser não resolveu já é o `N_INDEX` que o `[` de `params` também deixa (o mesmo
  fallback de sempre, `teko_params.mc`'s próprio cabeçalho) — o passe acha só os que nomeiam um
  array global e reescreve em `node_assign`, deixando os outros (o `xs[i]` de um `params`) intactos
  para o `tk_params_pass` de sempre. Uma ESCRITA não pode esperar o passe — o núcleo recusa
  `g[i] = e;` no PRÓPRIO parse (`left side of assignment must be a name`) — então `tk_bracket` lê
  `=`/`+=`/`-=`/`++`/`--` também no fallback, e devolve um placeholder (`tk_call("tk_unresolved_
  array", 0)`, o MESMO idioma do `.` deferido de `teko_typeof.mc`) que o passe resolve ou recusa
  (`teko: not a known array`, uma recusa estritamente NOVA — antes disso o núcleo já recusava
  qualquer escrita não resolvida, então não há regressão).
- **Largura e sinal:** `ld8`/`ld16`/`ld32`/`ld64`/`st8`/`st16`/`st32`/`st64` por `type_width`
  (`tk_ldn`/`tk_stn`, já de `teko_struct.mc`). `ld32` é sempre zero-extending (`language.md` §2 —
  "the signed read of raw memory... spelled `(i32) ld32(p)`"); um elemento `TK_SINT` mais estreito
  que a palavra — só `i32`, hoje — é envolvido num `CAST` pro próprio tipo depois do load
  (`tk_arr_load`), o MESMO idioma documentado. `i64` não precisa (já é a palavra inteira).
- **Bounds:** um índice LITERAL fora de `[0, N)` é erro de compilação, nas duas rotas
  (`tk_arr_bounds`, mensagem `teko: index K is out of range for NAME[N]`, o formato do próprio
  crumb). Um índice não-literal NÃO tem guard em runtime nesta fatia — dívida abaixo.
- **`a.Length`** — só para um array LOCAL (o `[` já sabe; um global exigiria a mesma deferência da
  leitura, fora do escopo desta fatia). `tk_dot` (`teko_expr.mc`) checa a tabela `av_` ANTES de
  `tk_struct_of_expr`; achando, devolve a constante `N` e recusa qualquer outro membro. `a.Length =
  e` cai na recusa do próprio núcleo (`tk_int` não é um nome).
- **Recusado, não contornado:** um array de tipo struct/classe (`Circle cs[2];`), local OU global —
  o elemento seria um slot de objeto sem nome próprio para `teko_rc.mc` percorrer, vazando a cada
  sobrescrita; mensagem própria nas duas rotas (`tk_arr_on_stmt`/`tk_garr_collect`).
- **Fixture** `surface_arrays.tk` (32/32 em exit 42): array local com leitura/escrita por índice
  variável num `for`, `a.Length` no laço, `a[i] += e`/`-=`, um global sem inicializador (`u8 g[8]`)
  e um com (`i64 t[] = {…}`), as três larguras (`u8`/`u16`/`i32`, a última provando o sinal — um
  valor negativo que voltaria positivo se `ld32` não fosse casteado), e um array local ao corpo de
  um MÉTODO (`Grid.product3`, a mesma prova de que o passe de RC caminha por uma árvore sintetizada
  sem se importar com arrays escalares no meio dela). AST das **31 fixtures anteriores**
  byte-idêntica ao compilador da base `6cf49db1` — EXCETO `surface_switch.tk` (tocada pelo commit
  do fix abaixo; idêntica entre o commit do fix e este). `mc limits ngen` `ok` (`intrin` segue 8/8
  — nada de novo registrado). Probes fora de `tests/`: índice constante fora do range → recusa;
  `Circle cs[2]` → recusa; `a[1] = e` sobre um `i64` escalar → recusa (`teko: not a known array`);
  `a.Length = 3` → recusa do núcleo.
- **Fix do `switch` (commit separado, ANTES deste):** `tk_switch_check_end` só olhava o último nó
  de TOPO do corpo do `case` — um corpo escrito como bloco explícito (`case 1: { …; break; }`, C#
  comum) caía direto no "control cannot fall out of a case" mesmo terminando em `break`. Recursa em
  `N_BLOCK` agora, olhando o último statement DENTRO do bloco — a mesma recursão que
  `tk_switch_no_continue_stmt` já fazia, ao lado. `surface_switch.tk` ganhou um `case` de bloco
  explícito (`case 20: { r = 55; break; }`) provando o conserto.
- **Dívidas registradas:** índice DINÂMICO sem guard em runtime (precisa de `panic` de superfície,
  que este crumb não tem); array de objeto/struct (local ou global) — o pacote inteiro de `T[]` em
  heap com RC próprio, fora de escopo; `T[]` como parâmetro (o nome de um array decai pro endereço,
  como em C, mas `void f(i64 xs[])` na ASSINATURA não foi ensinado); `.Length` sobre um array
  GLOBAL (só o local resolve); um `params xs[i]` dentro do corpo replay-instanciado de outra
  `params` que TAMBÉM usa um array global — o passe de arrays roda uma vez, antes da instanciação
  de `params`, então um global usado só dentro do corpo REPLAYED de um `params` cairia no `[` de
  `params` sem chance de resolver; nenhuma fixture combina os dois, registrado como aresta rara.

**Fila:** closures/`ref`/`out` (D221, architect-first) →
compilador teko de `<mc/core_min>` (plano §26). **Fora:** `var`, `type`, `match`, Variant,
método parcial, nested, `foreach` (precisa de iteráveis), herança de interface, `using G = geo;`/
`using static`, genérico qualificado (D31.14), namespace aninhado (D31.1).

**Dívida achada pelo verificador do C6 (registrada aqui, crumb futuro):** o `ngen` é um parser de
UMA passada — um tipo/classe precisa estar declarado ANTES do primeiro uso no arquivo, o que C#
não exige (ordem livre). Mesma família da limitação de método do §5.1 item 7, mas para
tipo/classe; fechar os dois junto exige record/replay de topo, não só de método.

**Dívida do C8:** `p.items[i]` sobre um receptor que o parser NÃO tipa (um parâmetro,
que só o oráculo do `pass()` resolve) não chega ao `[` de array — cai no `[` do `params`
e é recusado com `teko: \`[\` indexes a \`params\` list only`. Recusa clara, nunca
miscompilação; fechar isso é trabalho no `teko_typeof.mc` (C3b, em voo em paralelo).

**Dívidas conhecidas:** `struct`, pacote de `params` e campo `static` de classe sem reclaim
(acima). Fechadas: a arena bump sem reclaim (D218), o `syntax_infix` sobre operador do core
(mc 0.10.3/M41.5 — mas a rota do C5b segue sendo o `pass()`, pelos dois motivos do cabeçalho
de `teko_ops.mc`), o default em função de topo (C6, acima) e o `+` unário sem sítio (M45,
`syntax_expr("+")` + `parse_expr(11)` em `teko_ops.mc`'s `tk_unary_plus`).

### Por que o RC ficou no PASSE e não no parse (achado do crumb do reclaim)

O crumb mandava injetar o RC no parse, como o `lx` (`lang_stmt.mc` `lg_decs_from`), e
**parar e reportar** se o `on_jump` não desse a contabilidade ou se parse e pass
divergissem. As duas coisas apareceram, e são de fundo:

1. **`on_jump` dá profundidade de BLOCO, não de laço.** O `lx` conta laços porque é dono
   de `while`/`for` e empilha uma marca em cada um (`lg_lp`). Aqui `loop` é palavra do
   CORE e `word_add` recusa sequestrar keyword do core (`mc/src/hooks.mc:237-241`), então
   não existe `syntax_stmt("loop")` para empilhar marca — `break N` não teria como saber
   quantos escopos atravessa. Na árvore o nó `N_LOOP` está lá e a conta sai de graça.
2. **A POSSE não é decidível no parse.** Se `e` já carrega uma referência própria é uma
   pergunta sobre o TIPO ESTÁTICO de `e`, e no `ngen` o `.` sobre receptor que o parser
   não tipa é **deferido por desenho** (`teko_expr.mc` `tk_defer_member`): no parse o nó é
   um placeholder sem tipo nenhum. Chutar ali é vazamento silencioso (um incremento a
   mais) ou use-after-free silencioso (um a menos) — exatamente o que o crumb proíbe. O
   `lx` não tem esse buraco porque tipa todo receptor no parse (o `self` dele é parâmetro
   explícito).

Logo **escopo e posse têm UM dono só, o passe** — que é o que a própria sessão do mc
sugeriu no alerta do §23 do plano ("candidato a unificar, o pass como fonte única, quando
o reclaim/RC entrar"). A pilha de locais do parse fica intacta e segue fazendo o que
sempre fez, resolver `.`. **Nada ficou híbrido.**

**Entrega 5 — PREFIXOS VEEM PÓS-FIXOS LANDADO** (32 fixtures, `surface_operator.tk`
estendida): `!b[1]`, `-a.x`, `~g[2]` — `.`/`[` (`syntax_infix`, prec 12) agora binding
mais apertado que `- ! ~`, como em C#. **Não é `syntax_expr("-", ...)`** (a rota do `+`
não se generaliza): medido com um handler forçado a devolver um valor distinto, ele nunca
disparou para `-x` — `parse_unary()` do núcleo acha `-`/`!`/`~` na sua PRÓPRIA tabela de
prefixo (`ops_init`) e resolve o `N_UNARY` ali, sem nunca chegar em `parse_primary` (onde
`syntax_expr` mora). Só `+` reaparece por `syntax_expr` porque `ops_init` nunca o
registrou. A correção real é em `tk_dot`/`tk_bracket` (`teko_prefix.mc`, novo):
sinkam pela cadeia de `- ! ~` até o operando de verdade, resolvem o `.`/`[` NELE, e
reembrulham o resultado na mesma cadeia — `-a.x` vira o mesmo `N_UNARY(-, DOT(a,x))` que
o núcleo constrói para `-(a.x)` escrito com parênteses. `tk_bracket` ganhou uma guarda
(`tk_bracket_no_write`) para o operando-base de uma cadeia sinkada nunca virar alvo do
deferral de array GLOBAL (`-arr[i]` é valor, nunca lvalue, como em C#). AST das 31
fixtures não tocadas byte-idêntica à base `e2d4d936`; `mc limits` `ok` (`intrin` 8/8,
nada de novo registrado).

**Errata (crumb "adoção do 0.14.1", 2026-09-05):** a guarda `tk_bracket_no_write` só protegia o `[`
de array GLOBAL deferido — `tk_arr_index_of` (array LOCAL, `teko_array.mc`) e `tk_array_index`
(campo-array, `teko_struct.mc`) aceitavam `=`/`+=`/`-=`/`++`/`--` sem consultá-la, e `!b[1] = 3` só
era recusado por acidente (`value of type void` sobre o `!`, ou pior — `call to unknown function`
no site de instanciação de um genérico, no caso de um campo-array). Os dois agora consultam a
guarda antes de aceitar uma escrita e recusam com mensagem própria (`teko: the left side of = is
not a place`); a declaração de `tk_bracket_no_write` mudou de `teko_params.mc` para
`teko_prefix.mc`, seu dono lógico.

**K1 LANDADO (entrega 5, D221/§41, 2026-09-05):** `delegate` nomeado, ponteiro de função tipado,
`callp` tipado, `null`. Arquivos: `teko_deleg.mc` (novo — a tabela de assinatura `dg_*`,
`tk_delegate`, o wrap `Op f = fn;`/`new Op(fn)` via thunk memoizado por (delegate, função), a
chamada `f(a, b)` rebaixada num `pass()`), `teko_struct.mc` (`TK_KDELEG`, `tk_is_deleg`,
`tk_is_counted` — zero linha nova em `teko_rc.mc` além de generalizar `tk_rc_needed` para
`tk_is_counted` em vez de `tk_is_class`/`tk_is_iface` hardcoded, o que também é o que faz um
programa SÓ com delegate, sem classe, disparar o reclaim), `teko_type.mc` (`tk_null`), `teko_access.mc`
(`public`/`internal delegate` reaproveita `tk_decl_head`, `tk_delegate` só forward-declarado — ele é
definido depois de `teko_typeof.mc`, de quem `tk_deleg_pass` precisa), `teko_expr.mc` (`tk_new`
ganha `new Op(fn)`; `tk_field_use` ganha a chamada de campo de tipo delegate, `h.cb(2, 3)`),
`lib/rt.mc` (`tk_deleg_code`, `panic` — o `panic` de superfície É o `tk_deleg_code` chamando
`rt_panic`, então a fixture de pânico exercita as duas funções novas juntas, não uma morta).

**Desvio do §41, medido e necessário:** `gadd`, referenciado BARE (não numa chamada) dentro de
`namespace geo { GOp f = gadd; }`, não é tocado por `tk_ns_pass` (esse só reescreve `N_CALL`/
`N_ADDR`/um `N_IDENT` de `const` — nunca um `N_IDENT` de FUNÇÃO usado como valor), então
`decl_find("gadd")` falhava depois da renomeação para `geo__gadd`. `tk_deleg_pass` ganhou seu
PRÓPRIO walk (não o `tk_ty_pass_walk` de `teko_typeof.mc`, que não expõe "em que função estou"),
rastreando o namespace de cada função (`tk_ns_of_name`) e reusando os MESMOS `tk_ns_call_try_prefixes`/
`tk_ns_call_try_usings` que `tk_ns_pass` já usa para uma chamada — `tk_deleg_resolve_fn`. `new
Op(fn)` (tempo de parse, antes da renomeação) não precisou do mesmo tratamento.

Fixtures: `surface_delegate.tk` (`expect-exit: 42` — declaração no topo e em `namespace`, `Op f =
add;` contextual e `new Op(mul)` explícita, `f(3, 4)`, delegate como parâmetro (`apply(Op)`) e como
retorno (`choose`), sobrecarga `apply(Op)` vs `apply(i64)`, campo de classe de tipo delegate chamado
(`h.cb(2, 3)`), `null` guardado por `if`, `rt_live()` de volta ao piso depois de cada bloco) e
`surface_panic_null.tk` (`expect-exit: 70`, delegate nulo chamado).

Gate: 34/34 (32 anteriores + as 2 novas); AST das 32 fixtures anteriores **aditiva apenas** —
`diff` mostra SÓ as duas funções novas de `lib/rt.mc` (`panic`/`tk_deleg_code`, herdadas por TODA
fixture via `#include "../lib/rt.mc"`), ZERO linha removida ou alterada nas 29 que incluem `rt.mc`
(as outras 3 — `hello`, `primitives_ptr`, `primitives_scalar` — não incluem `rt.mc` e saem
byte-idênticas); `mc limits ngen` `ok`, `intrin` 8/16 (crescimento 0, nenhum intrínseco novo).
Probes (fora de `tests/`): delegate de 11 parâmetros recusado (`method with too many parameters`);
`new Op()`/`new Op` (sem alvo) recusados (`expression expected`/`expected ( after the delegate
name`); atribuir `add3` (3 parâmetros) a `Op` (2) recusado (`add3 does not match the delegate
Op(i64, i64)`); `delegate` dentro de corpo de função recusado pelo núcleo (`expression expected`,
a palavra não abre declaração de topo ali).

Fila K2→K5 (§41(d)): **K2** `ref`/`out` (dois `type_new`, tabela de apontado, `syntax_expr`,
`tk_ref_pass` ANTES de `tk_deleg_pass`); **K3** `T[]` de heap (parcialmente bloqueado — pede
`syntax_type` ao mc, dívida registrada em §41(e); `new T[n]`/`xs[i]`/`.Length`/`T[]` como campo e
parâmetro não dependem dele); **K4** lambda/função local/`use` (estende `teko_deleg.mc`); **K5**
`foreach` (`teko_loop.mc`).

**Item 0 LANDADO** (entrega 5, 2026-09-05, commit separado antes do K2): `Op f = 5;` compilava
limpo e segfaultava — `tk_deleg_var` só interceptava um inicializador `N_IDENT`. `tk_deleg_coerce`
(`teko_deleg.mc`) é o validador único que os quatro sítios de um slot de delegate agora usam
(var, atribuição de nome nu, `return`, argumento de chamada não-sobrecarregada): `null`, um valor
já tipado (local/param/campo/retorno/chamada-aninhada-por-delegate, via `tk_deleg_expr_ty`), ou um
nome de função compatível (embrulhado no mesmo thunk de sempre) passam; qualquer outra coisa é
`teko: Op takes a function, another Op, or null`. Zero fixture mudou (`--dump-ast` byte-idêntico);
a atribuição ganhou de graça a coerção de nome de função (`f = mul;` funciona agora). Ver plano §43.

**K2 LANDADO** (entrega 5, D221/§41, 2026-09-05) -- *errata K2w (§72, abaixo): o parâmetro NÃO é
mais construído com `nd_type` = apontado; nasce com largura de PONTEIRO e o apontado sai de
`tk_param_ty`/`tk_decl_param_ty`, inclusive para `tk_ty_scope_params`, que por isso mudou* --:
`ref T`/`out T`, C#'s by-reference, sobre uma
tabela de apontado chaveada pelo NÓ do parâmetro (não `(owner, idx)` — dois desvios medidos, ver
plano §43). `teko_ref.mc` (novo) — `type_new("ref"/"out", 8, 8, TK_INT)`, a tabela
`tk_rp_add`/`tk_rp_kind`/`tk_rp_pointee`, o mangling `tk_ty_sfx(p)`, o sítio obrigatório
`syntax_expr("ref"/"out")` (`tk_ref_arg`/`tk_out_arg`, tageado por `tk_rfarg_tag` para o validador
de chamada e o casador de sobrecarga distinguirem um endereço-por-`ref` de um valor que só parece
um), o rebaixamento `tk_ref_pass` (ANTES de `tk_deleg_pass`, DEPOIS do oráculo — `x` vira `ldW(x)`,
`x = e` vira `stW(x, e)` exceto pointee CONTADO, deixado para a exceção de `teko_rc.mc`), o
prólogo DPS de `out` contado (`st64(x, 0);`) e a checagem barata "nunca atribuído". `teko_default.mc`
(`tk_default_param` estendido para função livre), `teko_class.mc` (`tk_params` estendido para
método; `tk_sig_of` usa `tk_ty_sfx`), `teko_over.mc` (`tk_ov_sig` idem; `tk_ov_arg_ty`/
`tk_ov_args_fit` ganham a checagem de KIND, o que faz `f(i64)`/`f(ref i64)` resolverem por
sobrecarga; `tk_ov_judge` recusa `f(ref i64)` + `f(out i64)`), `teko_rc.mc` (`tk_rc_assign` ganha a
ÚNICA exceção: um parâmetro `ref`/`out` de tipo contado escreve por `tk_id(name)`, não
`tk_addr(name)`). `teko_typeof.mc` não mudou NADA — `nd_type` do parâmetro já É o apontado por
construção, então `tk_ty_scope_params` já servia de graça.

Achado que exigiu correção (medido): o guard de entrada de `tk_ref_pass` só olhava `tk_nrp`
(parâmetros `ref`/`out` DECLARADOS) — um `ref`/`out` usado só no ARGUMENTO contra um parâmetro POR
VALOR não registra nenhum parâmetro em lugar nenhum, e o pass saía sem tocar a árvore, deixando
`bump(ref a)` contra `void bump(i64 x)` compilar por engano. Corrigido: o guard também olha `tk_nrf`
(argumentos `ref`/`out` escritos); e o passe caminha TODA função, não só uma que DECLARA `ref`/`out`
— quem CHAMA raramente é uma delas.

Fixture: `surface_refout.tk` (`expect-exit: 42`) — `ref` escalar com sobrecarga por valor, `out`
duplo (`split`), `ref` sobre campo e elemento de array local, `ref` em método, `ref` de pointee
CONTADO com `rt_live()`/destrutor provando a troca. Gate: 35/35; `--dump-ast` das 34 anteriores
**byte-idêntico** a `5e83bb3a`; `mc limits ngen` `ok`, `intrin` 8/16 (zero crescimento). Probes (fora
de `tests/`): `f(a)` sem `ref`; `ref` num parâmetro por valor (também para chamada de MÉTODO);
`ref i64 x = 1` (default); `out` nunca atribuído; `f(ref i64)` + `f(out i64)`; `ref x;` como local.

Dívidas: atribuição-por-caminho para `out` (herdada do §41); `f(out i64 a)` inline; `ref p.inner.x`
(mais de um nível); `ref` sobre campo implícito (`this.x`) dentro do próprio corpo do método.

**K3 LANDADO** (entrega 5, D221/§41, 2026-09-05): `T[]` de heap -- o bloqueio do §41(e) caiu
(mc 0.14.2 trouxe `syntax_type`, o irmão de `syntax_param` na posição de TIPO). Um objeto por
elemento distinto, com o MESMO shape de um delegate: vtable de duas palavras (release + a
palavra de itab que ninguém usa), contagem, e daí `len`/os dados -- `rc_dec` libera um array sem
saber nada sobre ele, através da MESMA máquina que já libera classe/interface/delegate
(`tk_is_counted` estendido, zero linha nova em `teko_rc.mc`).

Arquivos: `teko_struct.mc` (`TK_KARRAY`, `tk_is_ha`, `tk_ha_row` -- memoizado por elemento,
a palavra reservada é a forma com colchetes `"i64[]"`/`"Circle[]"`, um lexema que o lexer nunca
forma, o mesmo truque de `lib/user_typearr.mc` do mc; `tk_ty_mangle_name`, a forma SEGURA pra um
símbolo mangled, `"arr_i64"`, já que a palavra reservada do tipo carrega `[`/`]` e um símbolo não
pode; a tabela `tk_hp_*`, ver abaixo), `teko_heaparr.mc` (novo -- `tk_ha_type` o handler
`syntax_type`, `tk_new_array`, `tk_ha_index` leitura/escrita/`+=`/`-=`/`++`/`--`, `tk_ha_member_of`
o `.Length` só-leitura, `tk_ha_deleg_call` pra `ops[i](args)` sobre um `Op[]`, e o gerador
vtable/release/alloc por elemento, lazy no primeiro `new`), `lib/rt.mc` (`tk_arr_at(a, i, w)`, o
endereço guardado -- `panic` fora de `[0, Length)`), `teko_expr.mc` (`tk_new`/`tk_member_of`),
`teko_params.mc` (`tk_bracket`), `teko_default.mc`/`teko_ref.mc` (o tipo de um parâmetro passa a
ler por `p_type()` em vez do `type_of_token()`+`p_next()` manual, pra o sufixo `[]` registrar via
`syntax_type`; `teko_class.mc`'s `tk_params` já caía em `p_type()` de graça, por `tk_gen_ty`).

**§5.1 armadilha nova: sítios do módulo que leem tipo precisam de `p_type()` pra ver `[]`.**
`tk_gen_ty()` (teko_generic.mc) JÁ chamava `p_type()` como fallback (campo/parâmetro/retorno de
MEMBRO, de graça) -- mas `teko_default.mc`'s `tk_default_param` e `teko_ref.mc`'s `tk_ref_param`
liam por `type_of_token(p_id())` seguido de um `p_next()` manual, que NUNCA dispara `syntax_type`
(o hook mora dentro de `take_type`, que só o `p_type()` público chama). Um novo `syntax_type` que
não muda esses dois sítios "funciona" pra tipos de membro e não funciona pra parâmetro de função
livre nem pro apontado de `ref`/`out` -- silenciosamente (`i64[] x` vira `i64` seguido de um `[`
sobrando, que `p_ident()` tenta ler como nome e recusa com uma mensagem confusa). Regra: todo
sítio que lê um tipo fora do `p_type()` genérico do núcleo tem que rotear por ele (ou decair
ANTES de saber se é seu) assim que qualquer `syntax_type` for registrado.

**`T[]` PARÂMETRO resolve no PARSE, não no oráculo -- desvio medido do §41(b).** O §41(b) previa
`teko_array.mc`'s `gd_*`/`decl_param_type` como o caminho de um parâmetro `T[]`; medido, esse
caminho corrompe o `p_decl_name()` da PRÓPRIA declaração sendo lida se o gerador (`top_add`) rodar
no meio do parâmetro (`teko_default.mc`'s teste de "nova declaração" leria 0 pro parâmetro
seguinte), e o `N_INDEX`/o placeholder de escrita ficam invisíveis a qualquer passe ANTES do
oráculo pendurados fora da árvore (o mesmo formato do `tk_pend_recv` de `.`). A saída medida:
`teko_struct.mc`'s `tk_hp_*` -- uma tabela PEQUENA, resetada uma vez por declaração
(`tk_hp_reset`, chamada de `tk_default_param`'s "nova declaração" e do topo de `tk_params`) e
preenchida no MESMO instante em que o parâmetro é lido -- então `tk_bracket`/`tk_dot` respondem
`xs[i]`/`xs.Length` sobre um parâmetro exatamente como respondem sobre um local, no PARSE, sem
placeholder e sem passe extra. Provado pelo caso que quebraria a alternativa: `cs[i].area()` sobre
um parâmetro `Circle[] cs` -- se `cs[i]` ficasse como `N_INDEX` cru até o oráculo, `.area()`
deferiria sobre um RECEPTOR sem tipo e resolveria por NOME (`tk_pend_by_name`), o que "funciona"
com uma única classe declarando `area` e mascara silenciosamente a ambiguidade com duas.

Fixtures: `surface_array_heap.tk` (`expect-exit: 42`) -- `n` de runtime, `xs[i]`/`xs[i]=e`/`+=`/
`-=`/`++`, `.Length` como bound de `while` E de `for`, `u8`/`i32` provando largura e sinal, `T[]`
como parâmetro de função livre E de método e como campo (`this.items`), `Circle[]` com
`rt_live()` provando o piso duas vezes (substituir um elemento libera o antigo; liberar o array
libera os três ainda vivos), `Op[]` chamado por índice com uma função nua coagida no slot.
`surface_panic_index.tk` (`expect-exit: 70`) -- índice além do fim.

Gate: 37/37 (35 anteriores + as 2 novas); `--dump-ast` das 35 anteriores -- as 3 que não incluem
`rt.mc` (`hello`, `primitives_ptr`, `primitives_scalar`) **byte-idênticas**; as 32 que incluem
`--dump-ast` com o diff **puramente ADITIVO**: só a nova `tk_arr_at` aparece, `grep '^<'` vazio
nas 35; `mc limits ngen` `ok`, `intrin` 8/16 (zero crescimento), `passes` 13 (zero pass nova --
tudo resolve no parse ou generico via `tk_is_counted`). Probes (fora de `tests/`): `new i64[-1]`
(`a negative array length`, exit 70); `xs.Length = 3` (`is read-only`); `xs[i]` sobre um `uptr`
cru (recusado pelo núcleo, `expression with no codegen`); `i64[][]` (`an array of arrays is not
taught yet`, a checagem lê o SEGUNDO `[` logo após consumir o primeiro, já que `take_type` só
despacha uma vez por posição); `new i64[]` sem tamanho (`` `new T[]` needs a length``); `ref
i64[] x` (recusa limpa, `not a known array` -- `tk_hp_add` só registra o parâmetro na forma
PLANA, nunca em `ref`/`out`, porque o valor de um `ref T[]` é o ENDEREÇO do slot do caller, não o
objeto, e tratá-lo como se fosse um seria silenciosamente errado, não uma dívida honesta).

Dívidas: `T[]` como GLOBAL -- o TIPO é aceito em toda posição (`p_type()`, extern, cast, retorno,
campo, parâmetro), mas leitura/escrita/`.Length` sobre uma GLOBAL de `T[]` não resolvem (nem
`tk_struct_of_expr` nem `teko_array.mc`'s próprias tabelas rastreiam uma global fora do `nd_val`
de tamanho fixo); `ref`/`out T[]`; `params T[]` (já era dívida do §41); `T[][]`/multidimensional;
`p.items[i]` sem `this.` explícito (herdada do limite de campo-array do D219); array de heap como
elemento de outro array de heap.

**K4 LANDADO** (entrega 5, D221/§41, 2026-09-05): lambda, função local nomeada e `use (a, &b)`,
inteiramente sobre a forma EXPLÍCITA `new Op((T a, T b) [use (...)] => corpo)` -- a mesma máquina de
objeto do K1, com um allocator/release/vtable/corpo gerados por lambda (não memoizados por par
como o thunk do K1, já que cada ocorrência pode capturar valores diferentes).

**Desvio medido do §41, decidido por risco de AST (não por preguiça):** o §41(a) decisão 19 previa
DUAS grafias -- contextual (`Op f = (a,b) => e;`, sem `new`) e explícita (`new Op((a,b) => e)`).
Só a EXPLÍCITA foi ensinada. Motivo medido: `parse_primary`'s ramo de `(` (`mc/src/parse.mc:873`)
decide cast-ou-agrupamento pelo PRIMEIRO token após `(` -- `(i64 a, i64 b) => ...` começa
IDENTICAMENTE a um cast `(i64)`, então `parse_expr(0)` sozinho já morre em `expected ) in cast`
antes de qualquer hook rodar. A única forma de alcançar a grafia contextual seria registrar
`syntax_expr("(", &f)` -- o que INTERCEPTARIA TODO `(` de expressão do programa inteiro (grouping E
cast), sem fallback ao núcleo (`docs/reference/hooks.md` § 3: "an expression position has no empty
node to fall back on"), arriscando as 37 fixtures anteriores que usam `(`/cast livremente. A forma
explícita evita isso por inteiro: `tk_new_deleg` já possui seu PRÓPRIO ponto de parse (depois de
`new Op(`), então o `(` de um parâmetro de lambda nunca passa por `parse_primary`. Cobre TODOS os
casos do fixture (inclusive "função local nomeada", que vira `Op twice = new Op((i64 x) => x*2);`)
sem tocar o núcleo. A forma contextual bare e o `x => e` sem parênteses (decisão 19's forma curta,
"só quando o alvo dá o tipo") ficam como DÍVIDA registrada, não código morto -- os dois são
recusados hoje pelo próprio núcleo (`expected ) in cast` / `expected ; after declaration`), nunca
silenciosamente.

**Como o corpo é lido:** NEM record/replay (`p_skip_balanced`+`p_push_source`, o idioma do C8/K3)
NEM `parse_function` puro -- os DOIS, conforme a grafia. A lista de parâmetros da lambda é lida por
`parse_params()` (o mesmo leitor público de `delegate`/função livre, que já dá de graça `ref`/`out`/
`T[]`/defaults em um parâmetro de lambda, embora não exigido pelo §41). O `use (...)` é lido a
seguir, ANTES do `=>` (posição escolhida: única posição sem ambiguidade -- depois dos parâmetros,
antes do corpo). O corpo: `=> { ... }` chama `parse_function(ret, nome, params)` (o body-depth reset
de M31 é dela, não meu); `=> expr` monta `tk_ret(parse_expr(0))`/`tk_stmt(...)` à mão, no estilo do
`tk_deleg_thunk_fn` do K1. `p_set_decl_name`/`p_decl_name` são salvos e restaurados ao redor de
TUDO isso -- a lambda é uma declaração nova por identidade (gensym `Op__lam0`, `Op__lam1`, ...,
`tk_ns_qualify`da como um `delegate`), então a tabela de defaults (`teko_default.mc`'s
`owner != tk_dflt_owner`) e o `tk_hp_reset` do K3 disparam de graça.

**Layout e captura:** o objeto é o MESMO do K1 (`vt/count/code` + N slots de captura, 8 bytes cada),
mas o allocator agora recebe UM PARÂMETRO POR CAPTURA (o valor, para uma por-valor; o ENDEREÇO cru,
para uma por-referência) -- o call site (`new Op(...)`) fornece `tk_id(nome)`/`tk_addr(nome)` NO
INSTANTE da construção, o que É o "congelamento" que D221 decisão 20 pede: uma cópia por-valor
muda de dono na hora (o allocator faz `rc_inc` antes de gravar, se o tipo for contado -- a MESMA
árvore que o release desfaz, um `rc_dec` por captura por-valor contada, nunca por uma por-referência
que é só um endereço). Dentro do corpo gerado, uma captura por-valor vira um LOCAL DE VERDADE
(`T nome = ld<W>(__env+off);`, a mesma largura de `teko_struct.mc`'s `tk_ldn`) -- o reclaim comum
(`teko_rc.mc`) já borrow-to-own e libera no fim da chamada de graça, ZERO código de RC novo aqui.
Uma captura por-referência vira um `uptr` interno (`__lamrefN`) carregando o ENDEREÇO, e toda leitura/
escrita do nome original dentro do corpo é reescrita para `ld`/`st` através dele (`teko_array.mc`'s
`tk_arr_load`/`tk_arr_store`, os MESMOS dois helpers que `teko_ref.mc`'s `tk_ref_walk` já usa para
dereferenciar `ref`/`out`) -- capturar por referência um tipo CONTADO é recusado (dívida honesta,
não silêncio: "a capture by reference of a counted type is not taught yet"), já que o slot seria o
endereço do PONTEIRO do declarante, não o objeto, e um `rt_store` correto ali pediria a mesma
exceção que `teko_rc.mc` já dá a um parâmetro `ref` contado -- generalizar essa exceção para uma
captura fica fora do K4.

**O nome capturado tem que já ser um local:** `teko_struct.mc`'s `tk_on_stmt` (M21.5's hook) ganhou
uma tabela NOVA, `tk_slv_add`/`tk_slv_find` -- toda declaração `N_VAR`, de QUALQUER tipo (não só
struct/class, que é tudo que `tk_local_add` já rastreava), grava (nome, tipo); `use (nome)` consulta
essa tabela NO INSTANTE em que lê a cláusula. **Dívida honesta:** um PARÂMETRO da função declarante
não é capturável hoje (só uma tabela de PARÂMETROS por-declaração resolveria isso, e nenhuma das
existentes -- K2's `rp_*`, K3's `tk_hp_*` -- serve; ficaria para quem generalizar `tk_slv_add` para
o site de `parse_params`/`teko_class.mc`'s `tk_params` também).

**"não capturado" e as duas recusas de escape:** o corpo recém-construído é percorrido por
`tk_lam_walk` (o MESMO desenho de escopo em pilha de `teko_typeof.mc`'s `tk_ty_scope_*`, reusado
diretamente -- zero tabela de escopo nova): todo `N_IDENT` que não é parâmetro/local/captura E não é
`decl_find`/`tk_struct_find` (função ou tipo global) morre em `teko: X is not captured; add it to
use (...)`, a frase exata do §41. As duas recusas da decisão 21 (`&`-captura escapando por `return`
ou por campo) são UMA função, `tk_lam_escapes(e)` -- "`e` é uma chamada ao allocator de uma lambda
com captura por referência" --, chamada nos TRÊS pontos onde um slot de delegate é ESCRITO:
`tk_deleg_return` (K1, já existia), `teko_expr.mc`'s `tk_field_use` (campo de instância) e
`teko_access.mc`'s `tk_static_use` (campo estático) -- a terceira é gratuita (mesma forma de store),
`tk_lam_escapes` é `forward`-declarada nesse arquivo (incluído ANTES de `teko_deleg.mc`). **Não
verificado:** GLOBAL -- é moto por construção, `parse_global`'s próprio `global initializer must be
constant` já recusa qualquer expressão não-literal antes que a checagem exista para checar.

Fixture: `surface_lambda.tk` (`expect-exit: 42`) -- sem captura, `use (k)` por valor congelado
(mutar `k` depois não muda o que a closure lê), `use (&acc)` mutando o local do declarante entre
duas chamadas, duas lambdas do MESMO delegate com capturas diferentes, `Op twice = new Op(...)`
(função local nomeada), lambda guardada num campo e chamada via `this.cb(x)` + lambda passada como
argumento de MÉTODO e chamada lá dentro, captura de um objeto contado (`Box`) com `rt_live()`
provando que o release do closure solta o objeto só depois que a ÚLTIMA referência (closure OU
local) cai, a mesma máquina dentro de um `namespace`, corpo em BLOCO (`=> { ...; return e; }`), e
`new Op(...)` como argumento de uma função LIVRE. Gate: 38/38 (37 anteriores + a nova); `--dump-ast`
das 37 anteriores **byte-idêntico** ao compilador da base `6ddae6a3` (`same=37 diff=0`); `mc limits
ngen` `verdict ok`, zero linha `grew` (o `intrin` bate igual entre base e K4 -- nenhum intrínseco
novo). Probes (fora de `tests/`): `use` de um nome de FUNÇÃO (não é local, recusado); nome livre no
corpo sem `use` (a frase exata); `(i64 x) => e` sem `new Op(...)` (recusado pelo próprio núcleo,
`expected ) in cast`); `x => e` sem parênteses (recusado, `expected ; after declaration`); 11
parâmetros de lambda contra um delegate de 2 (mismatch); `use (k, k)` duplicado; `&`-captura
devolvida por `return` de uma função que retorna o delegate; `&`-captura atribuída a um campo de
instância dentro de um método -- as duas com a MESMA frase de decisão 21.

Dívidas: a grafia CONTEXTUAL (`Op f = (a,b) => e;`, sem `new`) e a forma curta `x => e` (ambas
pedem hookar `(` em posição de expressão -- risco descrito acima, fora de escopo); captura de um
PARÂMETRO da função declarante; captura por referência de tipo CONTADO; `op.Invoke(x)` (herdada);
`Func<>`/`Action<>` (herdada); `params T[]` embalando uma lambda (herdada); alvo-tipagem de lambda
em argumento de função LIVRE sem `new Op(...)` (a forma explícita já cobre esse caso, então esta
dívida do §41(e) está fechada na prática -- `apply(new Op((i64 x) => x - 1), 43)` já funciona).

**K4b LANDADO** (entrega 5, D221/§41, 2026-09-05): fecha as três ressalvas do verificador do K4.

1. **Captura de delegate POR VALOR quebrava.** O prólogo gerado (`Op inner = ld64(addr);`) é um
   `N_CALL` que `tk_deleg_coerce` (a mesma passada `tk_deleg_pass`) recusava. Corrigido marcando o
   nó do `ld64` com o tipo do delegate (`tk_xt_put`), o mesmo idioma que `tk_field_use` já usa para
   um load de campo delegate — o nó é o FINAL (nada o copia depois), então a marcação resolve sem
   tocar `tk_deleg_coerce`.
2. **A recusa de `&`-captura escapando era por FORMA LITERAL, não por TAINT.** `tk_lam_escapes` só
   reconhecia o `N_CALL` do alocador — `Op f = new Op(...) use (&acc) => ...; return f;`
   (indireção por variável) e `cb = new Op(...) use (&x) => ...;` com `this` implícito (não passava
   por `tk_field_use`) escapavam sem aviso. Corrigido com um taint flow-insensitivo por NOME (um
   novo `on_stmt`, `tk_lam_taint_stmt`, marca o nome escrito por um `N_VAR`/`N_ASSIGN` cujo lado
   direito já escapa) — `tk_lam_escapes` aceita agora o `N_CALL` OU um `N_IDENT` tainted, checado em
   `return`, campo explícito, campo estático, campo implícito via `this` (`tk_this_assign`, novo) e
   elemento de `T[]` (`tk_ha_index`, novo). Array FIXO de delegate não precisou de check — já é
   inalcançável (`teko_array.mc` recusa qualquer linha da tabela de tipos como elemento).
3. **Grafia contextual e curta.** `(` de expressão continua SEM hook (o risco do §46 seguiu
   correto). A contextual entra nos pontos que já conhecem o tipo alvo antes do inicializador:
   `Op f = <init>;` (`tk_type_stmt`, teko_access.mc, desviando para `tk_deleg_var_stmt` quando o
   tipo é um delegate ESCALAR — `Op[] ops` cai fora via `tk_bracket_follows`) e um argumento de
   MÉTODO sem sobrecarga (`tk_call_method_args`/`tk_args_typed`, teko_expr.mc, gated por
   `tk_method_name_count(si, m) == 1` — picar overload por nome só, sem contar argumentos ainda,
   não dá para saber qual sinatura vale). A decisão `(`-é-lambda usa um LOOKAHEAD NÃO-CONSUMIDOR
   (`tk_paren_lambda_follows`, varredura de bytes a partir de `p_cp()`) em vez de
   `p_skip_balanced`+`p_push_source`: medido que o push descarta o lookahead pendente (o `=>` que a
   decisão precisa) assim que uma fonte nova é empurrada — `p_push_source` certo é para replay
   DEPOIS que a decisão já foi tomada por outro meio, não para decidir. `tk_lambda_build` virou um
   wrapper fino sobre `tk_lambda_finish` (o rabo compartilhado), reusado pela forma curta
   (`tk_deleg_short_lambda`, um parâmetro implícito do tipo que o delegate já declara).

Fixture: `surface_lambda.tk` ganha `deleg_byval_check` (item 1), `contextual_check`/`short_check`
(item 3) e uma chamada contextual em `method_check`. Item 2 não ganha fixture — as recusas ficam em
probes fora de `tests/`. Gate: 38/38; `--dump-ast` das 37 anteriores byte-idêntico à base `68b38174`
(`same=37 diff=0`); `mc limits ngen` `verdict ok`, `intrin` 8/16 em ambos os lados.

**Dívidas que seguem abertas** (não fechadas por este crumb, registradas): `return (params) => e;`
(o `return` é palavra do núcleo, sem hook — fica `return new Op(...)`); captura por referência de um
tipo CONTADO (herdada do K4); captura de um PARÂMETRO da função declarante (herdada do K4);
`op.Invoke(x)`/`Func<>`/`Action<>`/`params T[]` embalando lambda (herdadas do §41(e)).

**K4c LANDADO** (entrega 5, D221/§41, 2026-09-05): três correções pequenas sobre o K4b.

1. **Taint chaveado por (função, nome), não por nome global.** `taint_name` era uma tabela de NOMES
   da unidade inteira, nunca resetada — um `f` com `&`-captura numa função contaminava `return f;`
   de uma lambda LIMPA chamada `f` em outra função. Corrigido com uma coluna `taint_owner` e um
   resolvedor `tk_taint_owner()`: `p_decl_name()` durante o parse (a função corrente, per
   `docs/reference/hooks.md`), `tk_cur_fn_name` (novo, `teko_typeof.mc`) uma vez o parse termina e
   o resto do check roda numa pass (`tk_this_assign`/`tk_deleg_return`) — setado pelos dois loops
   que andam por `N_FUNC` numa pass (`tk_ty_pass_walk`, o próprio loop de `tk_deleg_pass`). No
   caminho, achado e corrigido um SIGSEGV latente: `top_add()` limpa `p_decl_name()` como efeito
   colateral, e `tk_lambda_finish` restaurava o nome salvo ANTES do seu PRÓPRIO `top_add(f)` (e dos
   de vtable/release/allocator) — o restauro foi movido para o FIM, depois de todo `top_add`.
   Flow-insensitivo dentro da função continua (reatribuição pra lambda limpa ainda é tainted).
2. **O achado adjacente do §47 não reproduz.** `apply(Op f, i64 x) { return f(x); }` +
   `Op g = add; apply(g, 41);` compila e roda limpo (42) tanto nesta branch quanto no compilador da
   base `a11623ca` — a dívida já não existe (fechada por alguma correção entre o `68b38174` que a
   viu e o `a11623ca` que abre este crumb). Sete variações tentadas a partir do CONTEXTO de
   `surface_lambda.tk` (função livre, método, com/sem captura, nome-de-função puro, dentro de
   `namespace`, com overload) — todas verdes. Dívida fechada por não-reprodução, sem fixture.
3. **Coerção de ternário num delegate.** `Op g = flag ? add : mul;` morria em "Op takes a function,
   another Op, or null" — `tk_deleg_pass` roda ANTES de `tk_ternary_pass`, então `tk_deleg_coerce`
   via o placeholder CRU (`tk_ternary(c, a, b)`), não um `if`/temporário já rebaixado. Ensinado um
   shape a mais: um `tk_ternary` faz `tk_deleg_coerce` recursar em cada braço, religando a mesma
   lista de irmãos com os braços coeridos — `tk_ternary_pass`, rodando depois, já vê os dois braços
   do tipo do delegate, e o check de tipos-diferentes fecha sozinho. Cobre `Op g = <ternário>;`,
   `g = <ternário>;`, `return <ternário>;` e ternário aninhado.

Fixture: `surface_lambda.tk` ganha `clean_f_returns_check` (item 14, item 1) e `ternary_check`
(item 15, item 3) — zero fixture nova; item 2 sem fixture (não reproduziu). Gate: 38/38;
`--dump-ast` das 37 anteriores byte-idêntico à base `a11623ca` (`same=37 diff=0`); `mc limits ngen`
`verdict ok`, zero linha `grew`. Detalhe completo, incluindo as sete variações do item 2 e as
probes de runtime do item 3, em `docs/design/plano-ngen-entrega4.md` §48.

**K5 LANDADO — série §41 fechada** (entrega 5, D221/§41 decisão 23, 2026-09-05): `foreach (T x in
xs)`, açúcar sobre a MESMA máquina do `for` (`teko_loop.mc`), sobre as três fontes com `Length`
conhecido: `T[]` de heap (K3), array fixo local e campo-array inline (ambas de `teko_array.mc`/
`teko_struct.mc`). `in` é lido como o identificador contextual que é (`tk_kw`, teko_class.mc) —
nunca registrado como palavra.

**Desvio medido do §41(b) (a "Assinatura nova" do plano previa um `tk_fe_source(pkind, plen,
pety)`; saiu com uma assinatura maior, pelo motivo abaixo — não é frouxidão, é o que o `p.items`
sem colchete exige).** A fonte NÃO é lida por `parse_expr(0)` genérico: um campo-array inline sem
`[`/`=` logo depois (`foreach (i64 v in this.items)`, exatamente a forma que o crumb pede) é
recusado por `tk_array_of` (teko_struct.mc) com "an array field is read one element at a time" —
a MESMA guarda que pega um `p.items;` solto em qualquer outro lugar do programa. `tk_fe_source`
(`teko_loop.mc`) é por isso um parser PEQUENO e dedicado: lê um nome bare OU `name.member` (`this`
incluso, pela mesma tabela `tk_local_find` que já registra `this` — teko_class.mc, D219) e resolve
pela MESMA cedo-no-parse que `[`/`.` já usam para cada uma das três formas (`tk_arr_find`/
`tk_hp_find`/`tk_local_find`+`tk_field_find`) — nunca um `parser cannot type -> defer` que as três
já evitam. Saiu com CINCO saídas (`pkind`, `pety`, `pnel`, `psrc`, `poff`) em vez de três: um
campo-array precisa do nome do RECEPTOR (`psrc`) e do OFFSET do campo (`poff`) para que o
endereço possa ser reconstruído FRESCO em cada um dos dois lugares que o usam (o bound e a carga
por elemento) sem violar "um nó vive em uma lista de irmãos só" — a mesma razão pela qual
`tk_ha_len` já recebe um NOME em vez de um nó.

**Achado que o build local precisou confirmar antes de reusar `tk_loop_rewrite_stmt` (registrado
como armadilha nova, §5.1 item 19): `break;` puro já nasce `nd_val = 1` no núcleo, não 0** — só
por isso a mesma função que `for`/`do` já usam, chamada exatamente do mesmo jeito
(`tk_loop_rewrite_stmt(stmt, 0)` sobre o corpo do usuário, ANTES de embrulhar no `loop` de uma
volta), soma corretamente os dois níveis extras que CADA `foreach` introduz (o `once` de uma volta
e o `loop` externo do bound) em qualquer profundidade de aninhamento — verificado ponta-a-ponta com
`break 2` escapando de dois `foreach` aninhados (`nestedcheck`, abaixo) sem UMA linha de lógica de
"foreach" na própria `tk_loop_rewrite_stmt`.

**Tipo do elemento — alarga, nunca estreita.** `i64 x in u8[]` (ou qualquer largura menor)
alarga implícito (`tk_cast`, o mesmo idioma de `teko_array.mc`); mesma largura com base diferente,
ou o inverso (estreitando), é recusado (`teko: the foreach variable's type does not fit the array
element`) — D131/D132 aplicado ao único lugar deste crumb que precisa decidir.

Fixture: `surface_foreach.tk` (`expect-exit: 42`) — `T[]` de heap com `break`/`continue`
(`heapcheck`), array fixo local (`localcheck`), dois `foreach` aninhados com `break 2`
(`nestedcheck`), `Circle2[]` com `rt_live()` provando que a variável de iteração só EMPRESTA (o
array segue dono; `circle_dtors2` fica em 0 durante o laço, os três destrutores só disparam em
`cs = null;`, e `rt_live()` volta ao piso nas duas pontas), `foreach` dentro de método sobre
`this.xs` (`T[]` campo) e `this.items` (campo-array inline) — `methodcheck` —, e o alargamento
implícito (`widencheck`).

Gate: `--entry-only` **39/39** (as 38 anteriores + a nova); `--dump-ast` das 38 anteriores
**byte-idêntico** ao compilador da base `5b21e790` (`same=38 diff=0`); `mc limits ngen`
`verdict ok`, `intrin` 8/16 em ambos os lados (zero intrínseco novo), `passes` 13/13 (zero pass
nova) — tudo por sintaxe pura em `teko_loop.mc`. Probes (fora de `tests/`, descartadas depois de
rodar): `foreach` sobre um escalar (`teko: not a known array`); `in` faltando (`teko: expected in
after the foreach variable`); `var x in xs` (recusado pelo próprio núcleo, `type expected` — D218
confirmado sem mensagem dedicada); tipo estreito (`u8 x in i64[]`, a mensagem dedicada acima);
`x = 99;` no corpo — **NÃO recusado** (compila e roda seguro, só não é o `readonly` do C# — registrado
como dívida abaixo, "se barato" do crumb não se aplicou: não há tabela de nomes somente-leitura
para locais nesta base).

**Dívidas — consolidado da série §41 inteira (K1-K5), nada escondido:** `lambda` aninhada +
localização de erro errada (não investigada nesta série); `return (…) => e;`/`return x => e;` (o
`return` é palavra do núcleo, sem hook — usa-se `return new Op(...)`); `T[]` como GLOBAL (o tipo é
aceito em toda posição, leitura/escrita/`.Length` sobre a global não); `params T[]` (embalar o
pacote variádico num `T[]` de heap); `ref`/`out T[]` (o valor de um `ref`/`out` é o ENDEREÇO do
slot do caller, não o objeto — `tk_hp_add` só registra a forma PLANA); `T[][]`/multidimensional;
`&`-captura de um tipo CONTADO (a exceção que `teko_rc.mc` dá a um parâmetro `ref` contado não foi
generalizada para uma captura); atribuição-por-CAMINHO para `out` (só a forma barata "nunca
atribuído" é checada); `f(out i64 a)` inline (declaração C# do parâmetro no próprio sítio de
chamada); `Func<>`/`Action<>` (prelúdio de delegates genéricos sobre a máquina de `delegate`
nomeado); `+=`/`-=` de delegate (multicast, exige lista de invocação); `x = 99;` dentro de um
`foreach` não é recusado (K5, acima); `p.items`/`p.xs` como fonte de `foreach` sobre um PARÂMETRO
(só um LOCAL ou `this` resolve — a mesma dívida "sem `this.` explícito" herdada do K3/D219);
captura de um PARÂMETRO da função declarante (K4); grafia CONTEXTUAL/curta de lambda como argumento
de função LIVRE ou método SOBRECARREGADO (a forma explícita `new Op(...)` já cobre as duas
posições, na prática); `op.Invoke(x)`; covariância/contravariância de delegate; `delegate`
genérico.

Plano: `docs/design/plano-ngen-entrega4.md` §49 (fechamento da série, próxima onda). Sem PR, sem
dreno — branch `feat/ngen-k5-foreach`, forward-only para `fix/retirement`.

**O1 LANDADO** (§50, 2026-09-05): ordem LIVRE de declaração de tipos, o débito do C6 -- um campo,
um parâmetro, um tipo de retorno, um local ou um global agora resolvem um `class`/`struct`/
`interface`/`delegate` declarado ABAIXO. A causa-raiz: `teko_fwd.mc` (novo) varre os BYTES
crus da fonte de entrada (`tk_fwd_init`, primeira linha de `user_init`, ANTES do prelúdio de
`tk_loop_init`) e de todo arquivo que um `import` empurra (`tk_ns_pass`-side, `teko_ns.mc`'s
`tk_import`), pulando comentário/string/char/diretiva, seguindo `namespace A.B {`/`namespace
A.B;` e reservando a PALAVRA (`type_new` + `syntax_expr`/`syntax_stmt`, mais `tk_ns_register` para
o nome curto namespaced) de cada declaração encontrada NA PROFUNDIDADE de um namespace/top-level
(D220: tipo aninhado nunca registrado) e que não seja um genérico (`Nome<` é pulado). A LINHA
`sr_*` continua nascendo tarde -- no primeiro USO (`tk_struct_find_fwd`/`tk_struct_by_ty`, ambos
com um fallback `tk_fwd_row`/`tk_fwd_row_by_ty` que materializa um placeholder `TK_PFWD`,
teko_struct.mc) ou na declaração real (`tk_type_add`, que ADOTA o placeholder em vez de acrescentar
outra linha) -- porque o índice da linha é EMITIDO em `tk_itab` (teko_iface.mc), e criar linhas
durante a varredura reordenaria todo slot de vtable de interface. `trait` não ganha `type_new`
(D216): a varredura captura o SPAN inteiro do corpo (contando chaves/comentários/strings) e o
arquiva direto na tabela de traits (`tk_trait_scan`, teko_trait.mc); a declaração real sobrescreve
com o span autoritativo de `p_skip_balanced` e marca `tr_declared_at`, então `use T;` acima de
`trait T` funciona sem esperar nada.

Sítios identidade-só (campo/parâmetro/retorno via `tk_gen_ty`→core `p_type()` ou
`tk_ns_param_ty`→`tk_ns_resolve_fwd`; local via `tk_type_stmt`→`tk_struct_find_fwd`;
`on_stmt`/`tk_is_counted` via `tk_struct_by_ty`) resolvem cedo; um sítio que precisa do CORPO --
`tk_conf_name` (base/interface), `tk_new`/`tk_deleg_find` (construção) -- continua na função
`tk_struct_find`/`tk_ns_resolve` ORIGINAL, nunca a `_fwd`, e recusa exatamente como antes quando o
tipo não foi declarado ainda (O2/O3). `tk_newname` ganha o ramo `tk_fwd_pending` (a palavra é um
forward ainda não declarado → aceita, `p_next()`+retorna) ANTES do ramo namespaced existente;
`tk_type_word` fica idempotente (reusa o `ty` da varredura em vez de chamar `type_new` de novo, o
que orfanaria o id -- `alias_add` nunca recusa uma segunda registração do mesmo nome). `tk_fwd_pass`
(logo após `tk_ns_pass`) varre a tabela da varredura: uma linha ainda `TK_PFWD` no fim da unidade é
o backstop de um falso positivo (decisão 14) -- `is used but never declared`.

Fixture: `tests/order_types.tk` (`expect-exit: 42`) -- `Holder`/`ShapeUser` usam `Circle`
(namespaced, via `using Geo;` escrito ANTES do `namespace Geo { class Circle }`) e `Shape` (tipo
plano) como campo, parâmetro e retorno, ambos declarados abaixo; `BoxUser`/`OpUser` têm um local
`Box b = null;`/`Op f = null;` de classe e delegate declarados mais abaixo ainda; `TraitUser` tem
`use T;` acima de `trait T`; tudo é declarado e usado normalmente em `main()`.

Gate: `--entry-only` **40/40** (as 39 anteriores + a nova); `--dump-ast` das 39 anteriores
**byte-idêntico** ao compilador da base `9acc3eda` (`same=39 diff=0`); `mc limits ngen` `verdict
ok`, `intrin` 8/16 em ambos os lados (zero intrínseco novo), `passes` 14/13 (a `tk_fwd_pass` nova,
esperada). Probes (fora de `tests/`, descartadas depois de rodar): nome de tipo usado como
VARIÁVEL antes da declaração -- `teko: name reserved by a syntax/type_alias registration: Box`
(divergência de C# documentada: a palavra é reservada pelo PROGRAMA inteiro, não por escopo);
`class`/`interface` mencionado dentro de um comentário `//`/`/* */` e de uma string literal --
NÃO reserva a palavra (compila normal, os identificadores ficam livres); `new B();` antes de
`class B` (sem nenhum uso anterior que materialize a linha) -- `teko: unknown struct or class
after \`new\`: B`, a recusa de sempre, intacta (O2). O backstop `is used but never declared`
(decisão 14) foi verificado por revisão de código/rastreamento manual do `tk_fwd_pass`, não por um
programa construído: disparar SÓ ele exige um falso positivo genuíno da varredura (o whole-word
match + skip de comentário/string/diretiva + o gate de profundidade D220 blindam contra os casos
óbvios), e todo caminho tentado para simular um (`#include` não varrido, genérico, trait com corpo
malformado, namespace mal-aninhado) dispara um erro MAIS CEDO — a compilação aborta antes de
`tk_fwd_pass` sequer rodar, o que é o sinal de que o desenho está correto, não uma lacuna.

Dívidas (decisão (d), nada escondido): base/interface QUALIFICADA ou de outro namespace declarada
abaixo (O3); `.`/`new`/`Tipo.membro` estático sobre um tipo ainda não declarado (O2); herança de
interface (`I2 : I1`, I1); `T[]` GLOBAL; `#include "x.tk"` cru não varrido (use `import`); lambda
contextual contra um `delegate` declarado abaixo.

Plano: `docs/design/plano-ngen-entrega4.md` §50 (O1 desta série; O2/O3/I1/G1 seguem na fila). Sem
PR, sem dreno -- branch `feat/ngen-o1-fwd`, forward-only para `fix/retirement`.

**O2 LANDADO** (§50, 2026-09-05/06): usos que precisam do CORPO -- `.`, `new`, membro estático --
deferidos para um pass, fechando o bloco §50 (b2) inteiro e três ressalvas do verificador do O1.

- **`.` sobre receptor PFWD** (`teko_expr.mc`'s `tk_dot`): quando `tk_struct_of_expr` acha um `si`
  cujo `sr_part_at(si) == TK_PFWD` -- um local, um campo ou um retorno de tipo declarado abaixo cuja
  identidade já foi materializada, mas o corpo (campos/métodos) ainda não -- o acesso DEFERE pela
  MESMA máquina que um parâmetro já usava (`tk_defer_member`/`tk_pend_do`, teko_typeof.mc): nenhuma
  linha nova, o oráculo resolve pelo TIPO uma vez que a declaração real tiver sido lida.
- **`new Nome(args)` sobre PFWD** (`teko_expr.mc`): duas tabelas novas (`nf_*`, o pending de `new`;
  `TK_MAXNFWD 32`) -- os argumentos são lidos no PARSE (o parser é quem tem os tokens), o nó fica
  tipado desde já (`tk_xt_add(n, si, 0)`, pure=0: possuído) e a escolha do construtor, a recusa de
  `abstract` e `tk_close_open` esperam `tk_fwd_resolve_all_new`, chamado de `tk_fwd_pass` DEPOIS do
  backstop de decisão 14. `tk_new()` também materializa `tk_fwd_row(name)` quando `new` é o PRIMEIRO
  uso do nome (nenhum campo/local o adiantou), e recusa cedo `interface`/`delegate` sobre uma linha
  ainda PFWD com mensagem própria (delegado: dívida nova, nunca testada pelo fixture).
- **`Tipo.membro` estático sobre PFWD** (`teko_access.mc`): a mesma ideia, tabela `st_*`
  (`TK_MAXSTFWD 32`) -- só a FORMA (load/store/call) e o argumento/valor já parseados são gravados
  (`tk_fwd_defer_static`); `tk_fwd_resolve_static_one` refaz o dispatch de quatro vias que
  `tk_static_member` já fazia (const, então `call`→método, então propriedade, então campo) sobre o
  `si` agora adotado.
- **Ressalva 3 do O1 fechada** (a mais séria: `tk_struct_find`/`tk_deleg_find` aceitavam um
  placeholder como se fosse corpo cheio). `tk_deleg_find` (teko_deleg.mc) e `tk_conf_name`
  (teko_class.mc, base/interface -- o sítio que O3 ainda vai herdar) agora tratam
  `sr_part_at(si) == TK_PFWD` como "não encontrado": um delegado ainda-PFWD cai no refuse claro de
  `tk_new()` em vez de ser tratado como um delegado de zero parâmetros, e uma base ainda-PFWD cai na
  recusa "unknown base class or interface" de sempre em vez de derivar um layout vazio. Provado por
  probe: `abstract class B` declarada abaixo + `B b = new B();` → `an abstract class is not
  instantiated: B` **na linha do sítio** (não da declaração).
- **Ressalva 2 fechada** (tipo QUALIFICADO `A.Item` acima da declaração): duas peças. (1)
  `tk_ns_walk` (teko_ns.mc) reconhece um segmento cujo JOIN ainda é só forward-scanned
  (`tk_fwd_row(probe) >= 0`) e para de estender exatamente como já parava para um tipo real; (2)
  **achado que o §50 (b2) não previu**: o SEGMENTO da esquerda (`A`/`Geo`) só vira palavra reservada
  (`syntax_stmt`/`syntax_expr` → `tk_ns_seg_stmt`/`tk_ns_seg_expr`) quando a declaração REAL de
  `namespace A { ... }` é lida (`tk_ns_seg_register`, teko_ns.mc) -- então `Geo.Item` escrito ANTES
  de `namespace Geo { ... }` nem chegava a disparar o hook qualificado. Fechado ensinando a VARREDURA
  a reservar esse mesmo segmento (`tk_fwd_try_namespace` chama `tk_ns_seg_register(seg0)`,
  idempotente, teko_fwd.mc) -- `tk_ns_seg_stmt`/`tk_ns_seg_expr` então tratam `sr_part_at(si) ==
  TK_PFWD` do jeito que `tk_type_expr` trata (`tk_fwd_defer_static`). `tk_ns_seg_stmt`'s próprio
  ramo de var-decl (`A.Item x;`) já funcionava sem mudança nenhuma -- é identidade-só (O1).
- **Coerção de delegate posterior NÃO precisou de código novo** (`Op f = add;` com `delegate Op`
  abaixo): `tk_type_stmt` já roteia por `tk_struct_find_fwd` (O1) e `tk_is_deleg(si)` já responde
  certo sobre um PFWD (o `kind` é conhecido desde a varredura); `tk_deleg_var`/`tk_deleg_coerce`
  rodam em `tk_deleg_pass`, que só executa DEPOIS que a unidade inteira -- incluindo a declaração real
  do delegate -- já foi lida. A entrada do (b2) para esse sítio já descrevia o estado atual, não uma
  dívida.
- **Ressalva 1 (`#include "x.tk"` cru não varrido) permanece dívida, registrada, não fechada.** Não
  há hook do núcleo que avise um módulo de um `#include` cru (`do_directive` é interno), e ler o
  arquivo incluído do disco por fora do lexer exigiria uma primitiva de I/O que a superfície do `mc`
  hoje não expõe a um módulo -- e o pedido `on_source(&fn)` do §50(d) resolveria isso de graça e
  cobre TODA fonte empurrada (incluída, embutida ou `import`ada), sem inventar uma segunda rota
  paralela só para o caso cru. Teko escreve `import`; o `#include` cru fica sem a ordem livre.
- **Fixture** `order_types.tk` cresceu (exit 42 recalculado): `box_value(Box b)` chama um método
  sobre um PARÂMETRO de tipo posterior; `o2_checks()` constrói `new Box(41)` (construtor COM
  argumento) antes de `class Box`, lê `Box.STEP` (const) e `Box.made` (`static`, incrementado no
  ctor) antes da declaração real, e fecha com `rt_live()` de volta ao piso (`floor + 2` até o
  `return`, `floor` depois -- só `b`/`oau` ficam vivos ali dentro); `OpAddUser` coage `op_add` (uma
  função livre) para `Op f`, delegate declarado abaixo; `qual_check()` constrói `new Geo.Item()`
  ANTES de `namespace Geo { ... }` (a mesma `Geo` que só abre de verdade mais abaixo no arquivo).
  Probes (fora de `tests/`, descartadas): `new` de `abstract` posterior -- recusa na linha do SÍTIO,
  não da declaração; `b.nope()` sobre `Box` posterior -- `unknown member of Box: nope`; `new Box(3)`
  sobre `Box` com construtor `private` -- `Box.Box is private`; um tipo NUNCA declarado (`Ghost`) --
  reproduz o mesmo achado do O1 (decisão 14, o backstop "is used but never declared" só dispara por
  um falso positivo real da varredura; um nome genuinamente inexistente já falha antes, no núcleo,
  com `expected ; after expression` -- nenhum programa sintaticamente válido chega a acionar SÓ esse
  backstop).

Gate: `rm -rf build`, build do zero; `--entry-only` **40/40** (nenhuma fixture nova); `--dump-ast`
das **39 não tocadas byte-idêntico** ao compilador da base `d9e51b5b` (`same=39 diff=0`); `mc limits`
`verdict ok`, `intrin` 8/16 nos dois lados (zero intrínseco novo), `passes` 14/13 (a mesma `tk_fwd_pass`
do O1, sem pass nova -- O2 estendeu o corpo dela).

Plano: `docs/design/plano-ngen-entrega4.md` §50 (O2 desta série; O3/I1/G1 seguem na fila; §52 tem a
errata). Sem PR, sem dreno -- branch `feat/ngen-o2-defer`, forward-only para `fix/retirement`.

**O3 LANDADO** (§50, 2026-09-06): `class Dog : Animal`/`class Sq : IShape` declarados ACIMA de
`Animal`/`IShape` -- a última ressalva do C6, base/interface fora de ordem.
- `teko_fwd.mc`: a varredura (`tk_fwd_try_decl`) passa a capturar o SPAN inteiro (`class`/`struct`/
  `interface` até o `}` de fechamento, `fw_span`/`fw_spanlen`) de todo tipo escaneado, além do nome
  já registrado pelo O1 -- refatorado para compartilhar `tk_fwd_brace_end` com `tk_fwd_try_trait`
  (que já andava esse mesmo laço). Uma SEGUNDA ocorrência escaneada do mesmo nome (`fw_multi`) marca
  o candidato como não-materializável -- é um `partial` em duas ou mais partes, cujo span isolado
  construiria um objeto incompleto. `tk_fwd_skip_decl(fi)` -- a declaração real, alcançada depois de
  materializada, é PULADA (nome, `: lista` opcional via `tk_ns_read_path`, corpo via
  `p_skip_balanced`), sem mensagem. `tk_fwd_in_flight`/`tk_fwd_flight_push`/`tk_fwd_flight_pop` --
  a pilha de nomes em materialização, para o ciclo `A : B`/`B : A`.
- `teko_class.mc`: `tk_fwd_materialize(fi)` (novo, ao lado de `tk_conf_name` -- precisa de
  `tk_own_methods`/`tk_nconf`/`tk_ntu`/`tk_nud`/`tu_tr`/`ud_tr`, do `teko_trait.mc` incluído ANTES
  dele, e não caberia de volta em `teko_fwd.mc`, incluído antes desse) empurra o span capturado com
  `p_push_source` e chama `parse_top()` UMA vez, salvando/restaurando o mesmo estado que
  `tk_gen_replay` (teko_generic.mc) já salva/restaura ao redor do seu próprio push -- uma base fora
  de ordem pode derivar de outra também fora de ordem, recursão real (a cadeia de três níveis do
  fixture). O token que o contrato do push gasta (hooks.md §4) não é o nome da base -- é o que quer
  que o `:`-list estivesse prestes a ler a seguir (uma `,` antes de outra interface, ou o `{` do
  corpo da classe DERIVADA) -- regenerado como um token extra ao fim do texto empurrado
  (`p_name()` do token corrente, colado depois do span com um espaço), que sai de volta do frame
  assim que a declaração materializada termina de ser lida. `tk_conf_name` tenta a materialização
  só quando o nome é BARE e resolve para o MESMO namespace do uso (`tk_ns_qualified_name`, decisão
  12 do §50) -- qualificado (`geo.Base`) ou só alcançável por `using` continuam a recusa de hoje.
  `tk_class()` ganhou o espelho -- checa `fw_mat_at` antes de `tk_newname`, e pula com
  `tk_fwd_skip_decl` quando já materializado.
- `teko_iface.mc`: `tk_interface()` ganhou o MESMO espelho de `tk_class()` (só o lado do pulo --
  `interface` ainda não tem `:` própria, D216-adjacente à espera do crumb I1).
- **Item pequeno da ressalva do O2** (`teko_typeof.mc`): `h.items[0] = 9` sobre um campo-array
  alcançado por um receptor que só o pass tipa (parâmetro/local de tipo PFWD) ganhou o ramo que
  faltava -- `TK_PIXLOAD`/`TK_PIXSTORE` ao lado de `TK_PLOAD`/`TK_PSTORE`/`TK_PCALL`, o índice lido
  em `tk_defer_member` (o mesmo `parse_expr(0)` que `tk_array_index`, teko_struct.mc, já lê),
  carregado em `pd_na` (ocioso nas outras formas), e `tk_pend_field` reaproveitando `tk_ax_index` +
  `type_width` para o endereço e o guard -- as mesmas três recusas do caminho estático (`is not an
  array`, `is read one element at a time`, `is assigned one element at a time`), sem linha nova de
  runtime.
- **Decisão registrada -- `partial` como base fora de ordem:** um `partial` com uma parte SÓ
  materializa normal (o span dessa única parte já é a declaração inteira -- `partial` na palavra vira
  um no-op no replay, e a real, alcançada depois, é pulada). Duas ou mais partes ESCANEADAS RECUSAM
  na hora da materialização (`fw_multi`, "teko: a partial base is not forward-declarable yet") -- o
  span de UMA parte só nunca é o objeto inteiro, e materializar todas juntaria texto de lugares
  diferentes do arquivo sem um span único onde apontar erros. Dívida estreita, não silêncio.
- **Fixture** `order_bases.tk` (novo, exit 42): `GrandDog : Dog : Animal` -- três níveis fora de
  ordem, recursão real da materialização -- dentro de `namespace Zoo`, com `: base(n)` encadeando o
  construtor de `Animal`, `override`/`base.speak()` em dois níveis, e `using Zoo;` alcançando os três
  de fora do namespace; `Sq : IShape` acima de `interface IShape`, fora de qualquer namespace;
  `rt_live()` de volta ao piso (a checagem em `checks()`, função à parte, para os locais morrerem
  antes do `return`). Probes (fora de `tests/`, descartados): ciclo `A : B`/`B : A` -- `teko: cyclic
  base: B` (a mensagem sai da materialização ANINHADA que de fato encontra o ciclo, não da primeira
  tentativa -- ver a nota abaixo); base qualificada abaixo (`: geo.Base`) -- `unknown base class or
  interface: geo.Base`; base só alcançável por `using` (outro namespace) -- mesma mensagem; `partial`
  em duas partes como base -- `a partial base is not forward-declarable yet`; `partial` em UMA parte
  só como base -- materializa e roda normal (exit 42); os quatro probes do item pequeno (`h.items[0]
  = 9` deferido, índice não-literal com `+`, campo não-array indexado, leitura de array sem `[`,
  índice literal fora do limite) -- todos com a mesma mensagem do caminho estático.
- **Nota de custo do ciclo:** a materialização de `A` (dentro de `B`, dentro do uso original de `A`)
  reentra em `A` uma SEGUNDA vez antes de `B` reaparecer como já-em-voo e disparar o erro -- porque a
  linha de `A` ainda não existe (o `tk_type_add` dela só roda depois que a lista `:` termina de ser
  lida) no momento em que `B` tenta achá-la. A pilha da decisão 14 ainda TERMINA (profundidade
  limitada pelo tamanho do ciclo, não infinita) e a mensagem sai correta; só não é o caminho mais
  curto possível -- aceitável, já que é um programa que nunca compilaria de qualquer forma.

Gate: `rm -rf build`, build do zero; `--entry-only` **41/41** (as 40 anteriores + `order_bases`);
`--dump-ast` das **40 anteriores byte-idêntico** ao compilador da base `7113acbd` (`same=40 diff=0`);
`mc limits` `verdict ok`, `intrin` 8/8 nos dois lados (zero intrínseco novo), `passes`/`syntax` 14/14
(nenhuma pass/palavra nova -- O3 só estendeu corpos já registrados por O1/O2).

Plano: `docs/design/plano-ngen-entrega4.md` §50 (O3 desta série; I1/G1 seguem na fila; §53 tem a
errata). Sem PR, sem dreno -- branch `feat/ngen-o3-bases`, forward-only para `fix/retirement`.

**I1 LANDADO** (§50, 2026-09-06): `interface I2 : I1, I0` -- a dívida "herança de interface" que o
O3 registrou (`teko_iface.mc`, "`interface` ainda não tem `:` própria") fecha aqui.
- `teko_iface.mc`: `tk_interface()` ganhou a MESMA leitura `: lista` de `tk_class()`
  (`tk_iface_base_name`/`tk_iface_conf`, lidos ANTES de `set_sr_m0_at` -- ler depois dobraria o
  range de assinaturas próprias de `I2` sobre as de uma base materializada no meio da lista,
  corrompendo as duas). Cada nome tem de ser outra interface (`tk_is_iface`); classe/struct/trait
  recusam com a mesma mensagem de sempre; uma base abaixo materializa pela MESMA
  `tk_fwd_materialize` do O3, sem código novo -- confirmado com um probe onde `I1` (com corpo
  default) fica abaixo de `I2 : I1`, que fica acima da classe: exit 42 de primeira.
- **Fecho por ARESTA achatada, não seguida em runtime (decisão 15 do §50):** `tk_iface_conf_close(ci,
  fi)` registra `fi` no conjunto de conformidade de `ci` (a MESMA tabela `(classe, interface)` de
  sempre -- nada exige que o dono seja uma classe) e copia, sem recursão, o conjunto JÁ FECHADO de
  `fi` (`tk_iface_nbase`/`tk_iface_base_at`, o mesmo `sr_ni_at`/`ci_if_at` que uma classe usa para
  seu próprio itab). Uma classe que só nomeia `I2` ganha DUAS entradas de itab -- uma para `I2`, uma
  para `I1` -- e `tk_conform`/`tk_mt_fill` (inalterados) cobram o método de `I1` da classe do jeito
  de sempre; o corpo DEFAULT de `I1` responde por ela sem precisar saber que chegou via `I2`.
  `tk_conf_apply` (teko_class.mc, `:` de uma classe) passou a chamar `tk_iface_conf_close` em vez de
  `tk_impl_add` cru -- é essa troca de uma linha que dá à classe as interfaces herdadas.
- **Ciclo `I1 : I2` / `I2 : I1` pego pelo fecho, não pela pilha de materialização:** a pilha
  `tk_fwd_in_flight` do O3 só guarda o span de UM lado em replay -- o outro lado, cujo `tk_type_add`
  já rodou, resolve como fila normal. `tk_iface_conf_close` recusa ANTES de gravar, com `tk_impl_has(fi,
  ci)`: quem quer que dos dois feche por ÚLTIMO encontra o outro já conformando a si mesmo, porque
  fechar o primeiro já achatou a cadeia inteira nele. `teko: cyclic interface base: <nome>`.
  `tk_ifmeth_find_deep(si, m, pdecl)` (nova) -- a posição de `m` em `si` OU numa base sua,
  devolvendo em `pdecl` a interface que de fato o declara, para o `.` sobre um valor tipado `I2`
  chamar um membro só de `I1` despachar pelo itab CERTO (`tk_iface_call`/`tk_pend_iface`/
  `tk_this_iface_call`, os três sítios que já resolviam `m` -- trocado `tk_ifmeth_find` raso por
  esta versão funda, `si`/`sr_m0_at(si)` por `di`/`sr_m0_at(di)`, sem mecanismo novo).
- `tk_impl_via`/`ci_via` (teko_iface.mc, nova coluna na mesma tabela): -1 quando a classe nomeou a
  interface no PRÓPRIO `:`, ou a interface que a puxou por herança -- lida por
  `tk_conform_missing` para dizer QUAL interface do `:` da classe é a dona do método que falta
  (`teko: method of \`I1\` not implemented (via \`I2\`)`, verificado por probe).
- **`interface I2 : I1 { }` -- corpo vazio agora é legítimo** (grupo puro, zero membro próprio):
  `tk_interface()`'s `"interface with no methods"` só dispara quando `sr_mn_at(si) == 0 E
  tk_iface_nbase(si) == 0` -- uma interface que só reúne bases não é mais "sem métodos" quando as
  bases têm os seus.
- **Fixture** `tests/surface_iface_inherit.tk` (exit 42): `I2 : I1` com `Sq : I2` respondendo
  aos dois -- `i2.area()` (só de `I1`, despachado fundo) e `i2.tag()` (de `I2`); `I1 x = q` chamando
  o mesmo `area()` direto; `Sq.unit()` (`static abstract` de `I1`, herdado); diamante `IC : IA, IB`
  com `m()` repetido nas duas e uma única implementação em `Box`, as três formas (`IA`/`IB`/`IC`)
  despachando pro mesmo símbolo; `rt_live()` de volta ao piso (`checks()` à parte). Probes (fora de
  `tests/`, descartados): base de interface classe/struct/trait -- recusa; ciclo `I1:I2`/`I2:I1` --
  `teko: cyclic interface base`; método herdado não implementado -- `... not implemented (via
  \`I2\`)`; base declarada abaixo (com corpo default) -- funciona de primeira, sem dívida.

Gate: `rm -rf build`, build do zero; `--entry-only` **42/42** (as 41 anteriores + a nova);
`--dump-ast` das **41 anteriores byte-idêntico** ao compilador da base `86bc343d` (`same=41 diff=0`);
`mc limits` `verdict ok`, `intrin` 8/16 nos dois lados (zero intrínseco novo), `passes` 14/14 (zero
pass nova -- I1 só estendeu `tk_interface`/`tk_conf_apply`/os três sítios de despacho de método).

Plano: `docs/design/plano-ngen-entrega4.md` §50 (I1 desta série; G1 segue na fila; §54 tem a errata).
Sem PR, sem dreno -- branch `feat/ngen-i1-iface`, forward-only para `fix/retirement`.

**I1b LANDADO — errata: propriedade herdada de interface** (§56, 2026-09-06): a ressalva do I1 --
`tk_iface_member_of` decidia "é propriedade?" por `tk_prop_find(si, m)`, que só percorre
`sr_base_at` (cadeia de CLASSE) e nunca as arestas de herança de interface -- fecha aqui.
- `teko_prop.mc`: `tk_ifprop_find_deep(si, m, pdecl)` (nova, ao lado de `tk_prop_find`), o mesmo
  padrão de `tk_ifmeth_find_deep`: própria (`tk_prop_own`) primeiro, senão recursa em
  `tk_iface_nbase`/`tk_iface_base_at`, devolvendo em `pdecl` a interface que de fato declara `m`.
  `tk_prop_find` em si NÃO muda -- percorrer `tk_iface_nbase` por ali passaria a aceitar, para uma
  CLASSE, a propriedade abstrata de uma interface que ela implementa mas não redeclarou, o que
  despacharia para um acessor nunca compilado.
- **Três sítios trocados** (o mesmo padrão do I1 -- `si`/`sr_m0_at(si)` cru por `di`/`sr_m0_at(di)`,
  sem tabela nova): `tk_iface_member_of` (teko_expr.mc, `.` sobre valor de tipo de interface já
  conhecido no PARSE) e `tk_iface_prop_use`/`tk_pend_iface_prop` passam a receber a interface
  DECLARANTE (não mais o tipo estático do receptor); `tk_pend_iface` (teko_typeof.mc, o mesmo `.`
  quando só o `pass()` tipa o receptor -- um parâmetro); `tk_this_iface_prop` (teko_this.mc, `X`/`X
  = e` bare dentro do corpo DEFAULT de uma interface que ela mesma não declara, herdado de uma base).
- **Fixture** `tests/surface_iface_inherit.tk` cresceu (exit 42 recalculado): `I1` ganhou
  `i64 Value { get; set; }`; `I2 : I1` ganhou um corpo DEFAULT `doubled()` que lê `Value` bare (só de
  `I1`); `Sq` implementa `Value` como auto-propriedade própria; `bump_via_i2(I2 v)` lê e escreve
  `v.Value` sobre um PARÂMETRO (o sítio pass-deferred). `checks()` cobre os quatro sítios: getter e
  setter através de um valor tipado `I2` (`i2.Value`), o corpo default de `I2` (`doubled()`) e o
  parâmetro deferido (`bump_via_i2`). AST das **41 fixtures anteriores** byte-idêntica ao
  compilador da base `0a0bd0f4` (`same=41 diff=0`). Probe (fora de `tests/`, descartado): o mesmo
  programa contra o compilador PRÉ-fix reproduz o defeito relatado ao pé da letra --
  `teko: unknown member of I2: Value`; com o fix, compila e roda.

Gate: `rm -rf build`, build do zero; `--entry-only` **42/42** (nenhuma fixture nova, uma
tocada); `--dump-ast` das **41 fixtures não tocadas byte-idêntico** (`same=41 diff=0`); `mc limits`
`verdict ok`, `intrin` 8/8, `passes`/`syntax` 14/14 nos dois lados (zero intrínseco/pass/palavra
nova -- I1b só estendeu três sítios de despacho já existentes).

Plano: `docs/design/plano-ngen-entrega4.md` §56 (I1b, esta errata). Sem PR, sem dreno -- branch
`feat/ngen-onsource`, forward-only para `fix/retirement`.

**G1 LANDADO — bloco §50 fechado** (2026-09-06): `T[]` GLOBAL de heap -- `i64[] g;` no topo, sem
contagem própria, `g = new i64[n];` mais adiante -- fecha a dívida "`T[]` como GLOBAL" que o K3
registrou e a última fila do §50.
- **Mesma tabela `hg_*`, dentro do MESMO `tk_array_pass`** (`teko_array.mc`): `tk_hg_collect()`
  varre `N_GLOBAL` cuja `nd_type` é linha `TK_KARRAY` (`tk_is_ha`) -- ao contrário do array FIXO
  (`tk_garr_collect`, que chaveia por `nd_val != 0`), um `T[]` global não carrega contagem própria
  na declaração, então a chave é o TIPO. `tk_hg_find`/`tk_ty_global_ha` (por nome) e `tk_hg_rewrite_index`/
  `tk_hg_resolve_write` (leitura/escrita) reusam a MESMA plumbing de parse (`tk_bracket` já deixa o
  `N_INDEX`/o placeholder de escrita, teko_params.mc: zero linha nova) e a MESMA resolução de
  endereço do K3 -- `tk_ha_load`/`tk_ha_store`/`tk_ha_compound` (`teko_heaparr.mc`), sobre `tk_arr_at`
  (RUN-TIME checado), nunca o bound compile-time do array fixo (a contagem só existe depois de um
  `new T[n]`).
- **Oráculo decide o global (`teko_typeof.mc`):** `tk_ty_of`'s ramo `N_IDENT` cai em
  `tk_ty_global_ha` quando o escopo não conhece o nome (um global nunca é LOCAL); `tk_pend_emit`
  ganhou um ramo `tk_is_ha(si)` -> `tk_pend_ha_length` para `g.Length` (só-leitura, a mesma recusa
  de `teko_heaparr.mc`'s `tk_ha_member_of`, reescrita para o FORM já lido em PARSE time em vez de
  `p_id()`, que não significa nada numa pass tardia).
- **Achado que exigiu correção: `cs[i].area()` sobre um GLOBAL.** `cs[i]` nunca entra na árvore que
  `tk_array_pass` percorre -- o `.` que segue DEFERE `cs[i]` inteiro como RECEPTOR
  (`tk_defer_member`), e o nó vira órfão, alcançável só por `pd_recv`, nunca por `nd_next`/`nd_a`
  (o MESMO formato de uma cadeia `p.inner.x`). `tk_pend_do` já persegue essa cadeia para o `.`
  encadeado (`tk_pend_at(recv)`); G1 usa o MESMO ponto para também rodar
  `tk_array_maybe_rewrite_index(recv)` quando `recv` ainda é `N_INDEX`, ANTES de perguntar o tipo --
  sem isso, `total + cs[i].area()` compilava até o `N_INDEX` cru chegar ao codegen e morrer com
  `expression with no codegen` (achado por build real, não hipotético).
- **`T[]` global em `namespace` (`teko_ns.mc`):** a proibição de global-dentro-de-namespace
  (`tk_ns_reject_topkind`, "a global is declared outside every namespace") ganhou UMA exceção,
  `tk_ns_topglobal_ha`, para uma linha `TK_KARRAY` -- deixada **sem renomear** (dívida honesta,
  registrada no próprio comentário): qualificar cada uso BARE de um global mutável como
  `tk_ns_rewrite_ident` qualifica uma CONST pediria consultar `hg_*`, e `tk_ns_pass` roda ANTES de
  `tk_array_pass` popular essa tabela. Um `T[]` global declarado dentro de um `namespace geo { }`
  compila e roda, usado BARE de dentro do próprio namespace; colisão de nome entre dois namespaces
  cada um com seu próprio `T[]` global fica sem resolver, mesma classe das outras dívidas de
  qualificação que este arquivo já lista.
- **Fixture** `tests/surface_array_global.tk` (exit 42): `i64[] g;` com `n` de runtime,
  `g[i]`/`g[i] = e`/`+=`/`-=`/`++`, `g.Length` em `while` E em `for`; `u8[]`/`i32[]` globais provando
  largura e sinal; `Circle[] cs` global -- um ROOT, `rt_live()` medido ANTES do `circlecheck()` (os
  outros globais heap já são roots por si, então o piso muda antes dele) sobe `+4` e NUNCA volta;
  `T[]` global (`pts`) dentro de `namespace geo { }`, usado bare de dentro de `geo.sum_pts`, chamado
  de fora por `geo.sum_pts(4)`. Probes (fora de `tests/`, descartados): `g.Length = 3` ->
  `teko: is read-only: Length`; índice além do fim (`g[n]` com `n == g.Length`) -> `teko: index past
  the end of an array`, exit 70; `foreach (i64 x in g)` sobre o global -> `teko: not a known array: g`
  (a dívida já registrada do K3/§50, recusa clara, sem tabela nova).

Gate: `rm -rf build`, build do zero; `--entry-only` **43/43** (as 42 anteriores + a nova);
`--dump-ast` das **42 anteriores byte-idêntico** ao compilador da base `20d78560` (`same=42
diff=0`); `mc limits` `verdict ok`, `intrin` sem crescimento, `passes` sem pass nova (G1 só estendeu
`tk_array_pass`, já registrado).

Plano: `docs/design/plano-ngen-entrega4.md` §50 (G1, última fila do bloco), §57 tem a errata. Sem
PR, sem dreno -- branch `feat/ngen-g1-global`, forward-only para `fix/retirement`.

**Dívidas consolidadas do bloco §50 (G1 fecha a fila; o que sobra, tree-wide):** `#include "x.tk"`
cru não varrido (use `import`); base ou interface QUALIFICADA declarada abaixo, e base de OUTRO
namespace; lambda contextual contra um `delegate` declarado abaixo; `foreach` sobre `T[]` (local,
parâmetro OU global) e sobre um forward; `b.x += 1` sobre receptor deferido; tipo aninhado (D220);
`T[]` global nunca liberado (raiz, decisão 16); `T[]` global namespaced fica BARE, sem qualificação
de nome (este crumb); `params T[]`, `T[][]`, `ref`/`out T[]`; covariância de interface e `I1 x =
<valor I2>` em posição de ARGUMENTO sobrecarregado.

**DI1 LANDADO** (D229, plano §58; 44 fixtures): os três marcadores de lifetime
(`IServiceSingleton`/`IServiceScoped`/`IServiceTransient`), o registro em comptime e `inject T` de
um singleton sem dependência.
- **`teko_di.mc` (novo).** Um marcador é um NOME que `tk_di_marker` reconhece onde a lista `:`
  já lê um (`tk_conf_name`, teko_class.mc:1159; `tk_iface_base_name`, teko_iface.mc) -- ANTES de
  qualquer busca na tabela de tipos, então um programa sem os três nomes nunca cria uma linha, um
  global ou um símbolo por causa deste arquivo (§58 (h) risco 1: o laço de `tk_di_pass` sobre uma
  tabela vazia é o mesmo no-op que `tk_params_pass` já prova para um programa sem `params`).
  `tk_conf_apply` (teko_class.mc:1417) consome o scratch e registra (classe, lifetime); as CHAVES
  são a própria classe mais o fecho de interfaces que ela já implementa (`ci_if_at`/`tk_nimpl`,
  §50 I1). `interface IServiceSingleton { }` do usuário é recusado em `tk_newname`
  (`teko_struct.mc`), e o marcador numa lista `:` de INTERFACE é recusado em `tk_iface_base_name`.
- **`inject T`** (`syntax_expr`, lido como `new`): sempre DEFERE com um placeholder
  (`tk_unresolved_inject`, o idioma de `tk_unresolved_new`), resolvido em `tk_di_pass` -- entre
  `tk_fwd_pass` e `tk_array_pass`, depois que `tk_partial_pass` já fechou toda classe parcial e
  todo serviço da unidade já está registrado, qualquer que seja a ordem em que o fonte os nomeou.
  Um Singleton (e, nesta fatia, um Scoped-de-raiz -- decisão 10) ganha um slot global + um getter
  memoizado (`<cls>_di_get`, emitido na primeira necessidade); o construtor é o de zero argumentos
  necessários (`tk_new_pick`, o mesmo de `new Nome` sem argumento), com uma recusa própria quando a
  classe só declara construtores que pedem algo esta fatia não sabe suprir (`teko: no constructor
  of this service takes only services` -- a dependência real é DI2).
- **RC: zero regra nova** (decisão 13) -- o getter devolve `uptr` (o RC nunca o toca) e o SÍTIO
  (`IClock a = inject IClock;`) recebe `xt_pure = 1` só depois que o passe resolve o lifetime, então
  `tk_rc_var` incrementa via `rt_own` como qualquer valor emprestado; o objeto do singleton nunca é
  liberado (a 1ª referência, a da própria alocação, nunca é decrementada -- `rt_live()` prova o piso).
- **Fixture** `tests/surface_di.tk`: `Clock : IClock, IServiceSingleton`, duas injeções (uma
  pela interface, outra pela classe concreta) provando a MESMA instância (`tick()` chega a 2),
  `rt_live() == 1` no fim (o singleton, nunca liberado). Probes (fora de `tests/`, descartados):
  marcador numa `interface`; dois marcadores na mesma classe; `abstract class : IServiceScoped`;
  `interface IServiceSingleton { }` do usuário; `inject` de tipo sem registro; duas implementações
  da mesma chave; construtor que só aceita um argumento não-serviço -- as cinco primeiras batem as
  mensagens exatas do plano, as duas últimas (fora da lista do crumb) confirmam a máquina de qualquer
  forma.
- Gate: `rm -rf build`, build do zero; `--entry-only` **44/44**; `--dump-ast` das **43
  anteriores byte-idêntico** ao compilador da base `b95d14ec` (`same=43 diff=0`); `mc limits ngen`
  `verdict ok`, `passes` 14->15 (`tk_di_pass`), `intrin` sem crescimento (8->8). A tabela `syntax`
  do relatório fica em 14 nos dois lados -- ela é o MÁXIMO entre `syntax()`/`syntax_stmt()`/
  `syntax_expr()` (as três chamam `grow(T_SYNTAX, ...)`, mc's `hooks.mc`), e `syntax()` (14, os
  honest-stops de topo) já domina `syntax_expr()` (8->9 com `inject`); a leitura correta do gate é
  "verdict ok", não o número aparecer.

**DI2 LANDADO** (D229, plano §58; 44 fixtures): injeção por CONSTRUTOR, o grafo de dependências, o
ciclo, e a emissão do singleton em cadeia -- mais o item herdado do verificador do DI1.
- **`tk_di_ctor_pick(ci)`** (`teko_di.mc`) escolhe, entre os construtores da própria classe
  (`ctr_*`, teko_class.mc), o de MAIS parâmetros satisfazíveis (C# §12.6.4): cada parâmetro é OU um
  tipo de serviço registrado (`tk_di_key_exists`, sobre `tk_struct_by_ty(nd_type(p))` -- o tipo bruto
  de um parâmetro é o id do `type_new` do núcleo, não a linha da tabela de structs que `sv_cls_at`/
  `ci_if_at` indexam, e a conversão é exatamente o que `tk_struct_by_ty` já existe para fazer) OU tem
  DEFAULT (`tk_fill_defaults`, a máquina do C1/C6) -- um serviço injetável sempre vence seu próprio
  default quando tem os dois. Sem candidato satisfazível: a mensagem do DI1 (`no constructor of this
  service takes only services`), agora cobrindo o caso misto (um parâmetro `i64` sem default no meio
  de outros injetáveis). Dois candidatos satisfazíveis empatados na contagem: `teko: two constructors
  of this service take the same number of injectable parameters`.
- **`tk_di_ctor_args(i)`** constrói a lista de argumentos do construtor escolhido, na ordem
  declarada: um parâmetro de tipo-serviço recursa em `tk_di_find_impl` + `tk_di_resolve` NA LINHA
  DAQUELE PARÂMETRO (não do sítio de `inject` que disparou a cadeia) -- "o construtor que pede",
  então uma chave sem implementação ou ambígua aponta exatamente para o parâmetro que a pede, em
  qualquer profundidade da cadeia. Um parâmetro sem chave clona o default que `tk_di_ctor_pick` já
  confirmou existir.
- **Ciclo** (`di_stk`/`tk_di_push`/`tk_di_pop`, decisão 18): `tk_di_new_call` empilha o serviço ANTES
  de escolher/montar seu construtor e desempilha ao fim; achar o mesmo serviço já na pilha é
  exatamente a recursão infinita que o COMPILADOR faria ao tentar construir um singleton que depende
  dele mesmo (direto ou por uma cadeia), pega ANTES de estourar a pilha do compilador --
  `teko: cyclic service: A -> B -> A`, com a cadeia inteira.
- **Emissão em cadeia**: nenhuma regra nova. `Svc_di_get()` (Singleton) chama `Svc_new(args)` cujos
  argumentos são `Repo_di_get()`/`Clock_di_get()` -- a MESMA máquina do getter memoizado do DI1,
  recursiva; `tk_rc_call_owned` (teko_rc.mc) já deriva posse pelo tipo de retorno DECLARADO da função
  chamada (`uptr` para um getter = emprestado; o tipo da classe para um alocador = próprio), então
  nenhuma das duas peças precisou de marcação de posse nova -- só o `<cls>_di_get`/`<cls>_new__sig`
  que já existiam.
- **Item herdado (decisão 17, verificador DI1): `inject T.m()` direto agora é recusado em TODA
  rota**, não só na vtable/itab. `tk_di_is_inject(n)` (`teko_di.mc`) responde 1 para o MESMO índice de
  nó que `tk_inject()` criou -- sobrevive ao `node_assign` de `tk_di_pass` porque este reescreve o
  CONTEÚDO do nó, nunca seu índice -- e `tk_dot` (teko_expr.mc) recusa `.` sobre ele ANTES de
  despachar para qualquer rota (método comum, virtual ou itab): `teko: bind the injected service to a
  variable before calling it`. A recusa antiga (`tk_pure`, "a virtual call needs a name or a field on
  the left") só cobria a dupla leitura da vtable; um método comum (`slot < 0` em `tk_emit_call`) nunca
  a consultava, e `inject Widget.tag()` compilava.
- **Fixture** `tests/surface_di.tk` cresce para a cadeia de três níveis `Svc(IRepo, IClock) <-
  Repo(IDb) <- Db`: `Db(i64 seed = 5)` (construtor com um parâmetro default preenchido, sem
  dependência nenhuma), `Repo(IDb db)` e `Svc(IRepo repo, IClock clock)` (cada um injetado por
  construtor), uma chamada através da interface (`IRepo r = inject IRepo; r.load()`, provando o
  itab), e `rt_live() == 4` no fim (Clock, Db, Repo, Svc -- os quatro singletons, nunca mais). Probes
  (fora de `tests/`, descartados): ciclo `A -> B -> A`; duas implementações da chave pedida por um
  construtor; construtor que pede `i64` sem default; dois construtores com a mesma contagem
  injetável; `inject Widget.tag()` sobre método comum.
- Gate: `rm -rf build`, build do zero; `--entry-only` **44/44**; `--dump-ast` das **43 fixtures
  não tocadas byte-idêntico** ao compilador da base `39a75156` (`same=43 diff=0`); `mc limits ngen`
  `verdict ok`, zero linha `grew` (`intrin` 8->8, `passes` 15->15, `syntax` 14->14 -- DI2 não abre
  hook novo, só cresce funções dentro de `teko_di.mc`/`teko_expr.mc`).

**DI3 landado** (plano §58 (f), decisões 8-14) -- `scope { ... }` (`syntax_stmt("scope")`,
`tk_scope_stmt`, teko_di.mc): o corpo é lido por `parse_stmt()`, o mesmo molde do corpo de
`tk_while` (teko_loop.mc) -- o token corrente é `{`, então cai direto em `tk_block` e devolve o
MESMO `N_BLOCK` que um bloco solto teria; um `scope` sem `{` é recusado (`teko: scope expects a
block`) antes de empilhar qualquer coisa. Uma pilha de parse (`discope_stk`) marca o `scope { }`
mais interno aberto; `tk_inject` grava esse número (`tk_di_cur_scope`, decisão 9) em cada sítio
`ds_scope`, e `tk_di_resolve` o carrega por TODA a cadeia de um construtor (`tk_di_ctor_args`), de
modo que um Transient construído dentro de um escopo resolve suas PRÓPRIAS dependências Scoped
contra o MESMO escopo que o pediu ("Transient herda o escopo de quem o recebe"). Um Scoped
resolvido sem `scope` aberto responde pelo getter de raiz de sempre (decisão 10, DI1/DI2
inalterado); um resolvido DENTRO de um escopo vira um LOCAL do bloco (`tk_di_scope_local`), um
`N_VAR` comum de tipo classe construído na primeira necessidade e devolvido por nome nas
injeções seguintes da mesma chave no MESMO escopo -- `tk_rc_block` (teko_rc.mc) o libera como
libera qualquer outro local, sem regra nova (decisão 13); os locais de um escopo são acumulados
em ORDEM DE DEPENDÊNCIA (`sc_head`) e prependados à cabeça do bloco só depois que TODO `inject`
da unidade resolveu (`tk_di_scopes_finish`, decisão 15). Aninhado: cada `scope { }` é o seu
próprio id -- o interno NÃO herda os Scoped do externo (instâncias PRÓPRIAS, como C#); decorre da
própria numeração léxica, sem mecanismo extra. Um Singleton constrói seu grafo sob o sentinela
`tk_scope_svcbld`, que recusa um parâmetro Scoped com mensagem própria (`teko: a singleton taking
a scoped service is not taught yet`) -- o escopo PRÓPRIO do singleton (decisão 11) é DI4.
Fixture: `tests/surface_di_scope.tk` (duas injeções do mesmo Scoped no mesmo `scope { }`,
dois `scope { }` seguidos, Transient fresco a cada injeção partilhando o Scoped do escopo
hospedeiro, `scope { }` aninhado, `return` de dentro do escopo, `rt_live()`/contador de destrutor
de volta ao piso). Probes (fora de `tests/`, descartados): `scope` sem `{`; `scope { }` dentro de
um `loop` (uma instância por volta, `rt_live()` plano). Gate: `rm -rf build`, build do zero;
`--entry-only` **45/45**; `--dump-ast` das **44 anteriores byte-idêntico** ao compilador da base
`01f5c81a` (`same=44 diff=0`); `mc limits ngen` `verdict ok`, `intrin` sem crescimento (8->8, com
e sem `scope` no programa).

**DI4 landado** (plano §58 (f)/(g), decisões 1-4/6/11, D229) -- fecha o desenho da série DI.
Namespace e interface-base como chave **não precisaram de código novo**: `tk_inject` já lia o
nome com `tk_ns_walk` + `tk_struct_find_fwd` (N1/O2), que já entra em `tk_ns_resolve_fwd` (busca
por `using`) e aceita um caminho qualificado (`app.ICache`); e `tk_di_sv_matches` já varre
`tk_nimpl`, onde a base de uma interface (`ICache : IBase`) já é achatada no `:` list da classe
desde o fecho §50 I1 (`tk_iface_conf_close`). Confirmado por fixture, não por código: `inject
app.ICache` (qualificado), `ICache`/`IBase` (bare, via `using app;`) resolvem ao MESMO Singleton.

**O Singleton que recebe um Scoped (decisão 11, a regra teko ≠ C#)** é o único mecanismo novo:
`tk_di_singleton_scope(sv)` aloca uma linha do MESMO pool que `scope { }` usa
(`tk_di_scope_new`, extraído de `tk_di_scope_open`) -- uma por Singleton, nunca duas
compartilhando. `tk_di_getter_sym` usa essa linha como `buildscope` no lugar do sentinela
`tk_scope_svcbld` de DI3 (removido); `tk_di_resolve` trata esse `buildscope` exatamente como o de
um `scope { }` (`scope >= 0` -> `tk_di_scope_local`), então um Scoped pedido pelo grafo do
Singleton vira um LOCAL do corpo do getter, um por (Singleton, Scoped), memoizado e capturado nos
campos que o guardam -- a mesma máquina do DI3, zero regra de RC nova. Os locais acumulados em
`sc_head_at(buildscope)` durante essa construção são splicados na frente do `p = <cls>_new(...)`
pelo próprio `tk_di_getter_sym`, e a linha é zerada ali para `tk_di_scopes_finish` nunca a
reprocessar (ela só varre `scope { }` léxicos). Provado por fixture: um Singleton com DOIS
dependentes que pedem o MESMO Scoped got a instância ÚNICA; DOIS Singletons com o MESMO Scoped
recebem instâncias DIFERENTES.

**Duas ressalvas de diagnóstico fecham junto:** um construtor `private`/`protected` de serviço
agora recusa com mensagem PRÓPRIA (`teko: the constructor of this service is not accessible: X`),
não mais a genérica de `tk_check_member` (DI nunca tem uma classe-que-pede à qual um `protected`
responderia); e `tk_di_new_call` chama `tk_check_type_use` na classe do serviço antes de tudo, o
mesmo `internal`-de-outro-projeto de qualquer outro alcance de tipo -- confirmado tanto para uma
INTERFACE `internal` de outro projeto quanto para uma CLASSE `internal` cuja interface é pública.

**A lacuna do verificador do DI3 (item 5)**: um `inject` de Scoped lido lexicamente dentro do
corpo de uma lambda que está dentro de um `scope { }` é recusado -- a lambda vira uma função de
TOPO separada (teko_deleg.mc), então um local prependado ao bloco do `scope { }` de fora seria
inalcançável de dentro dela. `tk_lam_body_depth` (teko_deleg.mc) conta profundidade de lambda em
voo; `tk_inject` recusa quando esse contador é > 0 E um `scope { }` está aberto (`teko: inject
inside a lambda takes the service from the enclosing scope; bind it outside and capture it with
use (...)`) -- um `inject` de Singleton dentro de uma lambda SEM `scope { }` aberto continua
funcionando normalmente (confirmado por probe), porque não há local nenhum sendo prependado.

**Tabela final de compatibilidade** (quem recebe quem, §58 (c) confirmado pelas fixtures):

| recebe ↓ / recebido → | Singleton | Scoped | Transient |
|---|---|---|---|
| **Singleton** | o slot global (mesma instância) | o escopo PRÓPRIO do singleton -- uma instância por Singleton, compartilhada por toda a sua subárvore | construído dentro do escopo do singleton |
| **Scoped** | o slot global | a instância DAQUELE escopo | construído no escopo que o recebe |
| **Transient** | o slot global | a instância do escopo herdado (léxico, ou o do serviço que o construiu) | novo a cada injeção |
| **raiz** (fora de `scope { }`) | o slot global | um slot global, uma instância no programa | novo a cada injeção |

Fixtures: `surface_di.tk` cresce com `namespace app { ... }`/`using app;` (qualificado e bare,
chave `ICache`/base `IBase`), `Board`/`WrapA`/`WrapB` (um Singleton com dois dependentes do
MESMO Scoped) e `GaugeX`/`GaugeY` (dois Singletons, Scopeds distintos), e `ClockView : Clock`
(classe derivada de serviço que NÃO é serviço, construída com `new` comum). `surface_di_scope.tk`
cresce com `namespace mon { ... }`/`using mon;` dentro de um `scope { }` (qualificado e bare
resolvem ao MESMO local de escopo). Zero fixture nova. Probes (fora de `tests/`, descartados):
serviço `internal` de outro projeto (interface E classe); ctor `private` -> mensagem própria;
duas classes implementando interfaces com base comum -> `two services implement`; `inject` dentro
de lambda dentro de `scope { }` -> mensagem própria; `inject` dentro de lambda SEM `scope { }` ->
funciona; `inject app.ICache` sem `app` existir -> `unknown type after inject: app`.

Gate: `rm -rf build`, build do zero; `--entry-only` **45/45**; `--dump-ast` das **43
fixtures não tocadas byte-idêntico** ao compilador da base `8d8721ff` (`same=43 diff=0`); `mc
limits ngen` `verdict ok`, `intrin` sem crescimento (8->8), `passes`/`syntax` inalterados (nenhum
passe/hook novo -- DI4 reusa `tk_di_pass`/`inject`/`scope` de DI1-DI3).

**Dívidas que sobram (plano §58 (g), inalteradas)**: genérico como chave (`IRepo<T>`); escopo
DINÂMICO por chamada (D229 decisão 9 é léxico, não dinâmico, de propósito); `delegate`/`struct`
como serviço (sem vtable, sem contagem); `inject` em inicializador de campo (não existe); factory
explícita, decoração/substituição de registro, serviço com chave (`[FromKeyedServices]`).

**COMPAT+HIGIENE LANDADO** (D226, plano §63; 45 fixtures): três itens independentes num crumb só.
- **Item 1** -- compatibilidade de tipo em atribuição/inicialização/argumento/retorno, C#'s own
  implicit reference conversion (`tk_row_fits`, `teko_struct.mc`): idêntico, deriva (classe->base),
  implementa (`tk_impl_has` já fecha herança de interface e herança de classe), `null` sempre cabe;
  `T[]`/delegate só idêntico. Dois sítios por TIMING: parse (`tk_check_field_store`, receptor já
  tipado) e pass (`tk_check_compat`, `teko_typeof.mc`, o oráculo cheio) -- `tk_rc_var`/
  `tk_rc_assign`/`tk_rc_return` (`teko_rc.mc`) rodam a checagem ANTES do gate `tk_is_counted`, e
  `tk_rc_pass` agora dispara também para unidade só-`struct` (`tk_compat_needed`, `tk_nstruct > 0`).
  Argumento de chamada é de graça para função livre/método direto/ctor: `tk_rc_call_args`
  (`teko_rc.mc`, dentro de `tk_rc_walk`) usa `decl_param_type` do próprio núcleo, sem tabela nova --
  vtable/itab (`callp`) ficam de fora, dívida registrada (aridade já checada, tipo não). **Dois bugs
  reais que a checagem nova encontrou, corrigidos na raiz:** o alocador de `struct` (`tk_ctor`)
  devolvia `p: uptr` sem marcar o tipo do struct (igual ao que `tk_new_fn` de classe já fazia);
  o temporário hospedado de um ternário/switch-expression (`tk_tern_lower`) inicializava com `i64 0`
  mesmo quando o braço é `Op` -- virou `tk_null()`. E um argumento de construtor resolvido por
  `inject` (`tk_di_ctor_args`) precisou do MESMO tag (`tk_xt_add`), com `pure` fazendo trabalho de
  verdade: a 1ª tentativa marcou `pure=0` e `surface_di.tk` quebrou em runtime ("reference count
  below zero") porque o getter memoizado devolve referência EMPRESTADA, não fresca.
- **Item 2** -- `tk_arr_at` (`lib/rt.mc`) ganhou o guard `if (a == 0) panic(...)` antes do
  `ld64(a+16)` que segfaultava num `T[]` nunca atribuído -- aditivo puro, as mesmas 7 linhas em
  todo fixture que inclui `rt.mc`.
- **Item 3** -- `tk_lam_check_name` (`teko_deleg.mc`) parava de recusar global/`const`/função livre
  dentro do corpo de uma lambda com a mesma mensagem de um typo genuíno. Sem hook sobre declaração
  de topo (`parse_top`, não `parse_stmt`) -- a resposta é DEFERIDA (`tk_lg_add`) para o fim de
  `tk_deleg_pass` (que já roda sempre que há lambda), que resolve contra um scan de `N_GLOBAL`
  (`tk_lam_resolve_globals`/`tk_global_find`). Escrita em global e `const` bare já funcionavam (sem
  bug ali). Dívida registrada: `use (g)` de um global segue com a mensagem genérica ("captures a
  local"), não a específica "globals are visible without use" -- deferir essa checagem tocaria a
  tabela de captura de forma síncrona, risco não justificado por uma melhoria cosmética.
- **Fixtures:** `surface_iface_inherit.tk` ganhou `Animal`/`Dog` (derivada->base, ao lado do
  class->interface já existente); `surface_lambda.tk` ganhou `global_const_free_check` (item 16,
  zero `use (...)`). Zero fixture nova. Gate: `rm -rf build`; build do zero; `--entry-only`
  **45/45**; `--dump-ast` das 43 fixtures não tocadas byte-idêntico contra `088bf795`, `same=43`
  fora do universo de `rt.mc` (que ganha só a linha aditiva do item 2 em todo fixture que o inclui);
  `mc limits ngen` `verdict ok`. Probes das recusas (item 1: classe não relacionada, interface não
  implementada, downcast, `T[]`/delegate de tipo diferente, argumento errado, retorno errado; item 2:
  índice em array nulo, exit 70; item 3: nome genuinamente desconhecido) rodados fora de
  `tests/` e descartados, não commitados.

**S1 LANDADO** (D225, plano §64/§65 — auto-hospedagem etapa 1, "as partes em vez do bundle").
`core_teko.mc` (novo) traz `<mc/core_machines>`/`<mc/core_writers>`/`<mc/core_build>`/
`<mc/core_bundle>` + `main()` (`host_init`, os quatro `*_init`, `mc_build_init()` — S1 ainda o
chama, D64.3 tira na S2 — e `mc_main`); `mc.toml` `[compiler]` ganhou `core = "<mc/core_min>"`
e `modules = ["core_teko.mc", "teko.mc"]`. `<mc/core_pkg>`/`<mc/core_sandbox>` ficam de fora — nada
sob `ngen/` chama `pkg`/`update`/`sandbox`. Medido no host macOS/aarch64, `mc` 0.15.5: binário
**1 489 364 B** contra **1 588 681 B** do bundle inteiro (**−6,25 %**, bate a previsão do §64(b));
`--dump-ast` das 45 fixtures **byte-idêntico** ao compilador da base `63e28f90` (`same=45 diff=0`
— menos partes não muda a árvore de nenhuma); `mc limits ngen` `verdict ok` em toda tabela, sem
`grew`, os números da compilação do próprio `teko.mc` MENORES (menos fonte no pré-scan, esperado);
usage sem argumento perdeu as 5 linhas de `pkg`/`update`/`sandbox`, manteve as 6 de
`mc`/`build`/`limits`/`sysroot`. `--entry-only` **45/45**. A derivação por perna do CI
(`.github/workflows/ngen.yml`) foi conferida contra o `mc.toml` novo: o `awk`/`sed` preserva
`[compiler]` (a diferença de espaçamento entre `out   = ` de `[project]`, 3 espaços, e
`out     = ` de `[compiler]`, 5, é o que impede o `sed` de tocar o segundo) — nenhuma mudança em
`ngen.yml`. Tabela completa em `docs/design/plano-ngen-entrega4.md` §65.

**S1m LANDADO** (mesmo crumb): `scripts/measure.sh`, POSIX `sh`, dado o binário + o config
imprime bytes, seções (`--dump-syms` sobre o `.mc` gerado, técnica de `scripts/check-parts.sh` do
mc, sem nome de seção hardcoded — Mach-O/ELF/COFF diferem) e `mc limits DIR --config CONFIG`.
Roda local hoje (`sh scripts/measure.sh build/mc-teko mc.macos.toml ngen`); ainda
NÃO ligado ao CI (S4.3 decide isso). `mc.toml` é o **único** arquivo deste crumb que muda fora
da adição de arquivos novos — a nota do §4 abaixo reflete essa autorização pontual.

**S2 LANDADO** (`core_teko.mc`, `teko.mc`, `mc.toml`, `.github/workflows/ngen.yml` —
plano §64/§66): `teko` ganhou driver próprio. `main()` troca `mc_build_init()` (D64.3) pelas peças
públicas que ela é feita de (`lex_set_libs`/`sysroots_init`/`on_plan(&mc_plan)`) + a tabela própria
de subcomandos (`tk_build`/`tk_limits`), e intercepta `argc < 2` para chamar `subcommand_usage()`
direto — achado fora do §64(c): `mc` sem argumento cai primeiro nas três linhas fixas de `cli.mc`'s
`usage()`, que este arquivo não pode editar (`src/` do mc intocado). `tk_build` (D64.4/D64.5) resolve
`DIR/teko.toml` antes de `DIR/mc.toml`, sem `--config`, e delega a `drv_build` sem reimplementar o
driver; `tk_limits` (achado adicional, dentro do escopo — é o próprio critério de aceite do §64(f))
faz o mesmo para o `.tk` que o `drv_is_source` do núcleo (só `.mc`) não reconhecia. `[compiler].out`
virou `"build/teko"` (única chave tocada); `ngen.yml` trocou a UMA linha que nomeava
`build/mc-teko` (o laço de fixtures) por `build/teko`. Prova: build do zero, **45/45**
via `teko build ... --entry-only`; `--dump-ast` das 45 contra a base `477ea715` — `same=45 diff=0`;
`mc limits ngen` `verdict ok`; `teko` sem argumento — só as duas linhas, exit 1; `teko limits
tests/hello.tk` roda (exit 3, o "grew" normal de um arquivo avulso sem plano de projeto).
Detalhe completo em `docs/design/plano-ngen-entrega4.md` §66.

**S2d LANDADO** (mesmo crumb, plano §66 + `README.md`): censo `type_disable`/
`intrinsic_disable` (D64.6) — **lista vazia**. `bool`/`char`/`byte`/`isize`/`usize`/`ptr`/`str` são
`type_alias` (identidade, nada a desabilitar); `f32`/`f64` vêm do `type_new` de `<float>`, só
conectados; `i32`/`ld64`/`st64`/`ld8`/`st8`/`ld32`/`callp` são usados pelas fixtures E pelos fontes
do núcleo (desabilitar qualquer um quebraria a auto-hospedagem da etapa 4). Nenhum código muda.

**S3 LANDADO** (`teko.mc`, `user.mc` (novo), `mc.toml` — plano §64(d)/§67, D64.7):
o pacote `teko`, "ambos" (módulo + runtime versionados juntos). `teko.mc` para de definir
`user_init` e passa a exportar `void teko_init()` — mesmo corpo, só o nome, seguindo
`docs/reference/packages.md` §3 ("um pacote nunca define `user_init`; exporta `<nome>_init()` e o
projeto o chama") e o precedente `tests/pkg/src/teach-1.0.0` do mc (`module = "mc_teach.mc"`,
exporta `teach_init()`). `user.mc` é NOVO, do PROJETO — não do pacote —, seis linhas:
`void user_init() { teko_init(); }`. `mc.toml`'s `[compiler].modules` ganha `"user.mc"` no
fim (depois de `"teko.mc"`, para `teko_init()` já estar declarado). `[package]` novo: `name =
"teko"`, `lib = "lib/rt.mc"` (o que um PROGRAMA inclui), `module = "teko.mc"` (o que um COMPILADOR
inclui), `files` = os 30 `teko_*.mc` irmãos + `teko.mc` + `lib/rt.mc` (**`core_teko.mc` e
`user.mc` FICAM DE FORA** — são deste repositório, não do pacote: mesmo precedente
`teach-1.0.0`, cujo `files` só lista `mc_teach.mc`, nunca o `user.mc`/driver que o consome), `check
= ["teko.mc", "lib/rt.mc"]` (as duas unidades que `[package]`'s "ambos" produz; toda entrada já
está em `files`). Nenhuma outra chave de `mc.toml` muda.

Gate: `rm -rf build`; build do zero (host macOS/aarch64, `mc` 0.15.5); `--entry-only`
**45/45**; `--dump-ast` das 45 contra o compilador da base `faac4d56` — `same=45 diff=0`; `mc
limits ngen` `verdict ok` (mesma régua de S1/S2 — `teko limits ngen`, diretório, recompila o
próprio `build/teko.mc` PELO teko taughtado e bate na colisão de palavra `type` do §64(e)/S4 —
esperado, fora de escopo aqui, não é regressão de S3: o comando certo para esta medição é sempre `mc
limits ngen`, o binário de RELEASE, como S1/S2 já faziam); `teko limits tests/hello.tk`
inalterado (exit 3, "grew", mesmo de sempre). `mc pkg hash ngen` **estável** entre dois runs —
`0f85d3fbbced52f69716fd36366c9209cea99a706121de3962061e1a8435fce4` — e **idêntico** rodando de
dentro de `ngen/` com `mc pkg hash .` (confirma a correção do `dep_under` do mc 0.15.4 para
`dir == "."`). Offline: `mc pkg verify ngen` roda e devolve "verified 0 packages against
mc.lock" (sem `[deps]`, sem lock, exit 0); `mc pkg check` não se aplica aqui — lê um `index/
<nome>.toml` de REGISTRO, não uma árvore local, e o registro segue fechado (§64(d) mesmo
bloqueio: "tudo menos a publicação pode ser feito hoje"). `.github/workflows/ngen.yml` **não
muda**: o `awk`/`sed` que deriva `mc.ci.toml` só corta a seção `[linker]` e reescreve
`[target]`/`entry`/`out` — `[package]` (sem `out`, sem `[linker]`) atravessa intacto, inerte, como
qualquer outra seção nova já demonstrou em S1 com `[compiler]`. Publicação (Release + tree hash +
PR em `minicompiler/mc-registry`) fica de fora, como o crumb previu — o registro do mc ainda não
abriu para o `teko`.

**S4.1 LANDADO** (plano §64(f)/§68): dois commits. (1) renomeia os identificadores internos que
colidem com palavra registrada pela teko (`word_add`) dentro do próprio `*.mc`: `scope` →
`dscope` (21 sítios, `teko_di.mc`), `out` → `dst` (37, `teko_rc.mc`/`teko_ternary.mc`) e — achado
fora do censo do §64(e), que só auditara `scope`/`out` — `params` → `prs` (75, sete arquivos):
`type_new("params", ...)` (`teko_type.mc:68`) passa por `alias_add` → `word_add`, a MESMA reserva
program-wide que `syntax()`/`syntax_stmt()` fazem (`hooks.mc:555-568`); auditoria completa contra
as 39 palavras que o ngen registra (censo mecânico, não à mão) confirma só estes três colidindo
dentro do ngen. (2) `git mv` dos 31 módulos do pacote (`teko.mc` + os 30 `teko_*.mc`) e
`lib/rt.mc` para `.tk`; `core_teko.mc`/`user.mc` ficam `.mc` (não são do pacote, D64.7/§67);
`#include`s internos, os 39 fixtures + `parts/ns_file.tk` que incluem `../lib/rt.tk`, e
`mc.toml` (`[compiler].modules`, `[package]`) atualizados. **Nenhum shim de 1 linha
necessário**: o mc estoque não exige sufixo `.mc` em `[compiler].modules` (`drv_include` escreve o
path cru, sem checar extensão) nem em `#include`/`[package]` (`lex_include` abre por caminho real;
`libs_open`/`packages.md` — "a trailing `.mc` is dropped... a payload with another extension keeps
it"). Gate (host macOS/aarch64, `mc` 0.15.5): `rm -rf build`, build do zero, **45/45**;
`--dump-ast` das 45 byte-idêntico contra a base `6c50aa98`; `mc limits ngen` `verdict ok`;
`build/teko` **byte-idêntico** (`cmp` limpo) entre antes/depois do `git mv` — mais forte que "a
menos das strings de nome de arquivo": o único diff é a linha `#include` do glue `build/teko.mc`
(caminho de módulo), o binário compilado não embute o próprio caminho fonte; `mc pkg hash ngen` →
`331ee075474088484b875fefc2a740bbbb27863852e0109ff24b35c723a2a253` (estável entre duas execuções).
Detalhe completo em `docs/design/plano-ngen-entrega4.md` §68.

**HIGIENE 2 LANDADO** (plano §69, 2026-09-06, base `4e9c87ea`, três commits): seis dívidas de
checagem/ergonomia do §5, quatro fechadas, duas registradas com o caminho técnico já mapeado.

- **Item 1 -- argumento via vtable/itab sem checagem de tipo (fechado, `46a0ab63`).**
  `tk_vcall_args_check` (teko_expr.tk, sobre `decl_param_type` de `decl_find(mt_fn_at(mi))`) e
  `tk_ifargs_check` (teko_iface.tk, nova coluna `im_prs` na tabela de membros de interface, o
  MESMO `prs` que já vira `sig`) aplicam `tk_check_compat` no PARSE-TIME call site, onde o
  método escolhido (`mi`/`k`) já é conhecido -- `tk_rc_call_args` (a checagem de argumento já
  existente) só alcança uma chamada cujo nó nomeia o símbolo mangled que `decl_find` resolve, e
  as duas formas de despacho indireto viram `callp`, opaco. Achado no caminho: o oráculo
  PASS-TIME (`tk_ty_of`) ainda não existe nesse ponto do parse -- `tk_pty_of` (teko_struct.tk: a
  tag já gravada de um nó, ou o tipo declarado de um local bare via `tk_slv_find`, o mesmo
  fallback do `use (...)` do K4) alimenta a checagem, ignorando em silêncio o que o parse sozinho
  não sabe (a mesma regra do `tk_check_field_store`). `types_class.tk` ganha
  `VShape.useCircle(Circle)`, prova pelo caminho vtable; o caminho itab só por probe.
- **Item 2 -- `ref`/`out` com tipo apontado errado (fechado, `04588e39`).** `tk_ref_check_pointee`
  (teko_ref.tk) recusa um apontado diferente, IDÊNTICO apenas -- mais estreito que o
  deriva/implementa de `tk_row_fits` (C#: `ref`/`out` não é covariante, nem de uma classe pra sua
  base). `tk_ref_addr`'s branch de local bare passou a gravar o apontado via `tk_slv_find` em vez
  de descartá-lo (`0 - 1`) -- o mesmo `tk_pty_of`/fallback do item 1. `surface_refout.tk` ganha
  `gcheck` (uma segunda classe, `Gem`, provando o match idêntico); o mismatch E o
  derivado-pra-base (também recusado, `ref` não é covariante) só por probe.
- **Item 3 -- `use (g)` de global (DÍVIDA, não fechada).** Sem tabela de "nomes já declarados como
  global" acessível no PARSE (não existe hook `on_global`; `top_add` é write-only; `decl_find`/
  `global_find` só respondem depois -- o segundo em CODEGEN, tarde demais). Corrigir por-mensagem
  sem essa tabela seria adivinhar. Caminho mapeado, não implementado: um pré-scan tipo O1
  (`teko_fwd.tk` já tem `skip_ws`/`skip_quoted`/`skip_line`/`skip_block_comment`/`word`/`word_eq`
  reusáveis) que reconheça `TYPE IDENT (';'|'='|'[')` em profundidade 0, excluindo as palavras que
  hoje abrem outra coisa (`namespace`/`using`/`import`/`class`/...) -- maior que ~60 linhas feito
  direito (a exclusão de falsos positivos é o grosso), registrado como dívida em vez de arriscar
  uma versão frágil.
- **Item 4 -- `T[]` global em `namespace` fica BARE (DÍVIDA, não fechada).** Confirmado por probe:
  um global ESCALAR bare dentro de `namespace` já é recusado hoje (`a global is declared outside
  every namespace`) -- a premissa do item ("se `tk_ns_pass` já qualifica globais escalares") não
  se sustenta; só um `T[]` atravessa, pela exceção do G1. Qualificar de verdade exige (a)
  renomear a DECLARAÇÃO no laço do bloco do `namespace` (o mesmo instante que `tk_ns_decl_note`
  já usa pra função livre) e (b) uma tabela PRÓPRIA (não `hg_*`, que só existe depois do
  `tk_array_pass`) de "nomes de `T[]` global namespaced vistos até agora", consultada pelo
  rewrite de identificador (`tk_ns_walk_calls_in`) -- desvia o bloqueio de fase que o G1 registrou
  (não depende de `hg_*`), mas ainda é tabela nova + hook novo + resolvedor por prefixo/using
  (o mesmo padrão do `const`) -- feature, não item de higiene. Caminho mapeado, registrado.
- **Item 5 -- `foreach` sobre `T[]` global (DÍVIDA, não fechada).** MESMA raiz do item 4: `tk_fe_source`
  resolve TUDO no parse (`tk_hp_find`/`tk_local_find`, tabelas de parâmetro/local); um `T[]`
  GLOBAL só existe em `hg_*`, PASS-TIME. K5 desenhou `tk_fe_source` deliberadamente SEM forma
  deferida ("nunca um `parser cannot type -> defer`") -- ensinar isso exigiria uma segunda forma
  de `foreach` (placeholder + rewrite tardio do `cond`/da carga de elemento, não um load/store
  isolado) ou o MESMO pré-scan do item 4/3. Registrado, não implementado.
- **Item 6 -- lambda aninhada, K4c (fechado, `91de53ca`).** Causa raiz achada e corrigida: a
  tabela de captura `use (...)` (`lc_name`/`lc_ty`/`lc_byref`, indexada por `tk_nlc`) resetava pra
  0 no INÍCIO e no FIM de `tk_lambda_finish` -- uma lambda construída DENTRO do corpo de outra
  termina o próprio ciclo (`use`, corpo, limpeza) antes do parse do corpo da lambda ENVOLVENTE
  retornar, e o reset da INTERNA apagava as capturas da EXTERNA ainda em voo. Reproduzido contra o
  código pré-fix: `io:25: teko: k is not captured; add it to use (...)` -- o nome de arquivo
  errado (`io`) é a segunda metade do relato ("localização de erro errada"). `tk_lc_base` faz da
  tabela uma PILHA (salva/restaura a janela `[tk_lc_base, tk_nlc)` de cada lambda em vez de
  zerar); os quatro geradores que assumiam índice absoluto 0 (`tk_lambda_prologue`/
  `tk_lambda_release_fn`/`tk_lambda_alloc_params`/`tk_lambda_alloc_stmts`) calculam o offset do
  objeto do closure relativo a `tk_lc_base`. `TK_MAXLAMCAP` sobe 8->32 (a tabela agora soma
  captures de toda lambda em voo, não só uma). `surface_lambda.tk` ganha `nested_check`.

Gate (host macOS/aarch64, `mc` 0.15.5, os três commits juntos): `rm -rf build`, build do
zero; `--entry-only` **45/45** (nenhuma fixture nova, três tocadas: `types_class.tk`,
`surface_refout.tk`, `surface_lambda.tk`); `--dump-ast` das **42 fixtures não tocadas
byte-idêntico** à base `4e9c87ea` (`same=42`, as 3 diffs são exatamente as tocadas); `mc limits
ngen` `verdict ok`, `intrin` 8/8 nos dois lados (zero intrínseco novo). Probes (fora de
`tests/`, descartados): item 1 vtable (`Square` onde o parâmetro pede `Circle`, através do
vtable) e itab (idem, através de uma interface) -- as duas recusadas, e a forma correta (mesmo
tipo) confirmada sem falso positivo; item 2 mismatch (`ref Circle` chamado com `ref Square`) e
derivado-pra-base (`ref Animal` chamado com `ref Dog`) -- as duas recusadas; item 6, o mesmo
`nested_check` rodado contra o código PRÉ-fix reproduz o defeito relatado ao pé da letra.

Verificador independente (2026-09-06, APROVADO-COM-RESSALVA; `same=42` também no `--dump-syms`,
cherry-pick limpo sobre `4e9c87ea`, 45/45 no dreno). Duas dívidas que o parágrafo acima NÃO
registrava, achadas por probe:
- **`ref`/`out` de ESCALAR não é checado (item 2, dívida).** `tk_ref_check_pointee` só compara
  apontado quando `tk_struct_by_ty(pty)` responde (classe/struct); para escalar devolve `<0` e a
  checagem sai cedo. `ref i64` chamado com `ref u8` COMPILA e crasha em runtime (SIGSEGV, 139).
  Pré-existe desde K2/D221 (não é regressão), mas C# barra `ref` de tipos distintos sejam eles
  escalares ou não. Caminho: comparar o `pty` cru quando os dois lados são escalares conhecidos.
- **Teto PRÁTICO de captura por lambda = 12, não 32 (item 6, relato impreciso).**
  `tk_lambda_alloc_fn`/`tk_lambda_alloc_params` geram o alocador do closure com UM parâmetro REAL
  por captura, sujeito ao `MAXPARAMS=12` do ABI; `TK_MAXLAMCAP=32` governa só a tabela do parser
  (orçamento de ANINHAMENTO, não de uma lambda). Na faixa `[13, 31]` a falha é a mensagem do CORE
  (`io:N: at most 12 parameters`, nome de arquivo errado); só a partir de 32 dispara a recusa
  própria (`too many captures in one lambda`). Caminho: alocador recebendo o objeto e gravando as
  capturas por `st64` em vez de recebê-las como parâmetros (remove o teto do ABI de vez).
- Achado positivo: `b.useCircle(h.sq)` (campo `Square` de outro objeto) É recusado -- o campo já
  chega tagueado por `tk_field_use`/`tk_xt_ty`, então o limite "field load não tagueado" é mais
  estreito do que o texto do item 1 sugere; só o retorno de chamada (`f()` devolvendo `Square`)
  passa silencioso.

Plano: `docs/design/plano-ngen-entrega4.md` §69 (detalhe completo, incluindo os caminhos técnicos
mapeados dos itens 3/4/5). Sem PR, sem dreno -- branch `feat/ngen-hygiene2`, forward-only para
`fix/retirement`.

**S4.2 LANDADO PARCIAL** (D225, plano §64(e)/§70 -- auto-hospedagem, rota A): a superfície toda
existe e o fixpoint FECHA com um único bloqueio removido, que é do lado do `mc` (abaixo).

- **`source_claim` adotado** (`teko_fwd.tk`, mc 0.15.8). `tk_source_claim(name)` devolve 1 para
  **(a)** todo nome terminado em `.tk` -- um programa, uma fixture, `lib/rt.tk`, os 31 módulos do
  pacote quando o compilador compila a si mesmo -- e **(b)** todo quadro que a PRÓPRIA teko empurra,
  que passa a ir por `tk_push_source` (a mesma assinatura de `p_push_source`, entre `tk_claim_own = 1`
  e `= 0`): a instância de genérico (`teko_generic.tk`), o corpo de trait (`teko_trait.tk`), a
  declaração materializada do §50 O3 (`teko_class.tk`) e o prelúdio de `+=`/`-=`/`++`/`--`
  (`teko_loop.tk`). Sem (b) esses quadros -- cujo NOME é uma frase (`Box__Circle__4 instantiated
  from f.tk:12`, `<teko-loop-prelude>`), não um arquivo -- seriam lidos com o vocabulário do núcleo,
  e `class`/`str`/`public` no texto que a própria teko escreveu lexariam como identificador comum.
  Tudo o mais (`<mc/host>`, `<mc/objmodel>`, `<sys>`, `<prelude>`, `core_teko.mc`, `user.mc`, todo
  `.mc`) NÃO é reivindicado: ali `type`, `out` e `params` voltam a ser nomes de parâmetro. **O fork
  g1 do §64 está RESOLVIDO pelo `source_claim`, não pelo renome do núcleo** -- e o renome interno que
  a S4.1 fez (`scope`/`out`/`params` dentro do `ngen/`) segue necessário, porque os módulos são `.tk`
  e portanto SÃO reivindicados.
- **`mc_teko.tk`** (novo) -- a unidade única da rota A (D64.9): cinco linhas de `#include`
  (`<mc/host>`, `<mc/core_min>`, `core_teko.mc` com as outras quatro partes + `main()`, `teko.tk`
  com os 30 irmãos, `user.mc` com `user_init`), a mesma ordem do glue que `mc build` gera
  (`build/teko.mc`). É `.tk`, logo reivindicado -- e por isso não tem um identificador próprio:
  toda linha é um include, nada nele pode colidir com palavra ensinada.
- **`scripts/bootstrap.sh`** (novo, POSIX `sh`, sem `set -e`, tempo por etapa) -- teko0 (`mc
  build ngen --compiler-only`) -> teko1 -> teko2 -> teko3, todos por `teko build ngen --config
  <derivado> --entry-only` sobre `mc_teko.tk`; critérios: `cmp build/teko2.o build/teko3.o`,
  `--dump-asm` de teko2 vs teko3 com diff vazio, e as 45 fixtures compiladas por **teko1**. Os
  configs derivados (`mc.boot{0,1,2,3}.toml`, `mc.bootfix.toml`) nascem do `mc.toml` pelo
  MESMO `sed` do §4, ficam ao lado dele (o `entry` resolve contra o diretório do CONFIG, não do
  projeto -- um config em scratch não acha `mc_teko.tk`) e somem no `trap EXIT`; `mc.toml` não
  é tocado. **O `.o` vem do fluxo `build`, sem modo cru:** o derivado MANTÉM o bloco `[linker]` --
  com linker o `mc` escreve `<out>.o` e o entrega ao `cc`, e é isso que deixa o objeto em disco; sem
  linker o backend embutido escreve só o executável. (O modo cru `teko mc_teko.tk -o x.o` também
  funciona e dá objeto byte-idêntico -- 1 708 248 B nos dois -- mas o `--dump-asm` do modo cru sai em
  x86-64 mesmo com `[target] macos/aarch64`, então ele serve de diagnóstico, não de alvo.)
- **Tetos de tabela subidos para a escala de UMA UNIDADE** (o núcleo tem ~8 500 linhas a mais do que
  qualquer fixture): `TK_MAXFDECL` 256->4096 (declarações livres com parâmetro; medidas ~2 300),
  `TK_MAXSLV` 512->8192 (locais da unidade; ~3 800), `TK_MAXODECL` 1024->8192 (declarações da
  unidade), `TK_MAXGARR` 64->512 e `TK_MAXGDEF` 64->512 (arrays globais; ~180), `TK_MAXARR`
  256->1024. Cada um foi o erro SEGUINTE do self-compile, na ordem; nenhuma outra tabela estourou.
- **PROVA do fixpoint, com o bloqueio do `mc` removido experimentalmente** (probe fora do commit:
  as duas linhas `syntax_stmt("while"/"for")` comentadas): teko0 compila `mc_teko.tk` (4,0 s) ->
  teko1 (`teko1.o` 1 708 248 B, binário 1 529 192 B); teko1 -> teko2 (3,9 s); teko2 -> teko3 (4,4 s);
  **`cmp teko2.o teko3.o` LIMPO** -- e mais: `teko1.o == teko2.o` também, o compilador já está no
  ponto fixo na primeira volta. Ou seja a rota A está inteira: falta só o item do `mc`.
  **Ressalva do verificador (2026-09-06):** a sonda NÃO satisfaz o critério "teko1 compila as 45"
  da tabela §64(f) -- com as duas linhas comentadas o teko1 da sonda dá **38/45**, falhando exatamente
  as 7 fixtures que usam `while`/`for` (`surface_array_global`, `surface_array_heap`, `surface_arrays`,
  `surface_foreach`, `surface_loops`, `surface_switch`, `surface_ternary`), porque a sonda desliga a
  semântica de laço da teko. Fecha só com o patch do `mc`; "fixpoint FECHA" aqui = critérios 1+2 (objeto
  e `--dump-asm`), não os três.
  **FECHOU (2026-09-06, mc 0.15.10 = `TE_RULE`):** `scripts/bootstrap.sh` tal como está, sem contorno:
  teko0 1,8 s → teko1 3,8 s (`teko1.o` 1 712 808 B) → teko2 4,2 s → teko3 3,8 s; `cmp teko2.o teko3.o` limpo;
  `--dump-asm` 220 651 linhas, diff vazio; **teko1 compila as 45 (45/45)**; total ~40 s; `FIXPOINT OK`. Os três
  critérios da tabela §64(f) fecham. A teko está auto-hospedada sobre o mc (rota A).

**O BLOQUEIO (pedido ao mc, um só).** `word_add` marca a ENTRADA de token como `TE_TAUGHT`, e a
entrada é COMPARTILHADA entre a estrada de módulo e a de diretiva (`tok_add` é idempotente por
lexema). A teko ensina `while` e `for` (`syntax_stmt`, `teko_loop.tk` -- os handlers dela fazem
escopo/RC e reescrita de nível de `break N`/`continue N`, que a regra do prelúdio não faz), então as
entradas `while`/`for` ficam marcadas -- e `lex_word_id` esconde toda palavra marcada em fonte NÃO
reivindicada. Resultado: o `while` do `<prelude>`, que o NÚCLEO usa ~150 vezes, some das fontes do
núcleo. Sintoma exato: `mc/objmodel:212: expected ; after expression`, na linha
`while (i < 16 && ld8(s + i)) {` de `name16`. A release note do 0.15.8 afirma o contrário ("Fora do
escopo: `#rule`/`#infix`/`#prefix`/`#token` (o `while`/`for` do prelude, que o núcleo usa)") -- e
isso vale só enquanto o dialeto NÃO ensinar o mesmo lexema; a teko ensina. **Pedido:** numa fonte
não reivindicada, um lexema que também é literal de despacho de `#rule`/`#token` tem de continuar
PALAVRA e despachar para a REGRA -- nunca para o handler do módulo (`parse_stmt` consulta
`syntax_stmt_find` antes de `rule_find`, `parse.mc:1256`), o que sugere um segundo bit por entrada
(setado pela estrada de diretiva) mais um guard nas buscas `syntax_*_find` para fonte não
reivindicada. Repro mínimo: um módulo com `syntax_stmt("while", &h)` + `source_claim` de `.tk`
compilando qualquer `.mc` que use `while`. **Contorno NÃO foi feito** (a saída "desmarcar com
`tok_set_taught(id, 0)`" faria o handler da teko parsear o `while` do núcleo -- semântica errada e
transparência perdida).

Gate (host macOS/aarch64, `mc` 0.15.8): `rm -rf build`, build do zero; `--entry-only`
**45/45**; `--dump-ast` das 45 byte-idêntico ao compilador da base `c7357b9b` (`same=45 diff=0`) --
o `source_claim` e os tetos não movem uma árvore; `mc limits ngen --config` `verdict ok`, com a
linha nova `source_claim 1/8` e `intrin 8/8` (zero intrínseco novo); nenhuma fixture nova
(`tests/` intocado); `scripts/bootstrap.sh` chega ao **stage 1** e para no bloqueio acima,
com a mensagem do compilador impressa. Detalhe completo em `docs/design/plano-ngen-entrega4.md` §70.

**HIGIENE 3 LANDADO** (plano §71, 2026-09-06, base `0fd12888`, dois commits): as DUAS dívidas que o
verificador da higiene 2 registrou acima, as duas fechadas.

- **Item A -- `ref`/`out` de ESCALAR passa a ser checado (fechado, `4052205f`).**
  `tk_ref_check_pointee` (teko_ref.tk) saía cedo para todo apontado que não fosse classe/struct;
  agora a IDENTIDADE do apontado decide para TODO tipo (`ref i64` recusa um `ref u8`, com
  `teko: a value of type u8 does not convert to i64`), e o deriva/implementa segue recusado
  (`ref` não é covariante, C#). A peça nova é `tk_ref_arg_pointee`: a tag de parse
  (`tk_rfarg_pointee` -> `tk_slv_find`) é unit-wide e **nunca vê um PARÂMETRO**, então o repass
  `lvl_c(ref x)` dentro de `lvl_b(ref i64 x)` responderia com o `Box x` de `outcheck` e recusaria
  uma fixture correta -- com classe-contra-classe isso nunca aparecia porque o `pty` escalar saía
  antes do lookup. A pass pergunta à DECLARAÇÃO que percorre (`tk_ref_cur_fn`): parâmetros
  primeiro, depois o `N_VAR` do corpo; um global, ou um nome que dois blocos irmãos declaram sob
  tipos diferentes, responde -1 e é silêncio (a regra do `tk_check_field_store`). Limite:
  `type_alias` é identidade pura, então `bool`/`byte`/`u8` (e `char`/`u32`, e `str`/`ptr`/`uptr`)
  são o mesmo apontado aqui. Fixture: `surface_refout.tk` ganha `scopecheck`; mismatch é probe.
- **Item B -- as capturas de uma lambda não custam mais um parâmetro cada (fechado, `6ef10a24`).**
  O alocador do closure recebia UM parâmetro por captura, então o `MAXPARAMS=12` do ABI do mc ficava
  na frente do `TK_MAXLAMCAP`: treze capturas morriam em `io:N: at most 12 parameters` (mensagem do
  núcleo, arquivo errado). Agora **o alocador recebe o OBJETO** (`uptr p`) e nada mais, e o bloco é
  alocado no SÍTIO DE CRIAÇÃO já com as capturas escritas -- `tk_lambda_capture_chain` encadeia um
  `tk_cap_put`/`tk_cap_own` (duas funções novas em `lib/rt.tk`) por captura sobre `rt_alloc(objsize)`,
  cada elo devolvendo o bloco, a mesma forma da lista de `params` e pela mesma razão (um store é
  instrução; o closure tem de ser UMA expressão). `tk_cap_own` faz o `rc_inc` que o alocador fazia.
  **Nada mais mudou**: layout `{vt, rc@+8, code@+16, captures@+24…}`, prólogo, release -- e o nó mais
  EXTERNO do inicializador continua sendo a chamada ao alocador (a cadeia é o argumento dela), então
  `tk_lam_escapes`/coerção de delegate/`tk_xt_add` leem o que sempre leram. Slot de captura é uma
  PALAVRA (grava `st64`, o prólogo lê com `tk_ldn`); `f64` atravessa bit a bit (medido). Teto real
  agora é só `TK_MAXLAMCAP`: **32 capturas somadas sobre as lambdas em voo**, 33 recusada pela
  mensagem da teko com arquivo/linha certos (14/20/31/32 compilam e rodam). Fixture:
  `surface_lambda.tk` ganha `manycap_check`, quinze capturas.

Gate (host macOS/aarch64, `mc` 0.15.8): `rm -rf build`, build do zero; `--entry-only` **45/45**
(nenhuma fixture nova, duas tocadas); `mc limits ngen --config` `verdict ok`, `intrin 8/8`;
`mc.toml` intocado. `--dump-ast` das 45 contra o compilador da base `0fd12888`: **3
byte-idênticas** (as que não incluem `lib/rt.tk`), **40 com o MESMO diff** (hash igual, 29 linhas =
as duas declarações novas de `lib/rt.tk`, nada mais), **2 com diff próprio** (as tocadas). Isto
desvia do "43 byte-idênticas" que o crumb pediu, e o motivo é estrutural: um helper novo em
`lib/rt.tk` aparece na árvore de TODA unidade que a inclui. A alternativa era emitir a cadeia com o
`tk_va_put` do `params` (zero diff, mas acopla closure a variádico por um nome que mente) --
preferiu-se a superfície própria com a prova mecânica de que os 40 diffs são o mesmo byte a byte.

**Dívida ADJACENTE achada (FECHADA pelo K2w abaixo, plano §72): `ref T`/`out T` de um escalar mais
ESTREITO que uma palavra era quebrado em runtime desde o K2.** Um parâmetro `ref T` era declarado
com o tipo do APONTADO (desenho do K2) mas carrega um ENDEREÇO, e o mc trunca um ponteiro passado a
um parâmetro de largura 1/2/4: `void bumpb(ref u8 x)` com um `u8` segfaltava (139) -- e o mesmo em
dialeto mc puro (`void poke(u8 x) { st8(x, 5); }` com `&b`) segfalta igual, então não era lowering
da teko, era o tipo declarado do parâmetro.

**K2w LANDADO** (plano §72, 2026-09-06, base `cc539258`, dois commits): o parâmetro `ref`/`out`
nasce com largura de PONTEIRO (`TY_UPTR`) nos dois sítios que constroem um (`tk_default_param`,
`tk_params`), e o APONTADO fica só na tabela lateral da K2. **`tk_param_ty(p)` e
`tk_decl_param_ty(d, i)` (teko_ref.tk) são o par ÚNICO por onde todo leitor do tipo de um parâmetro
passa** -- o mangling (`tk_ty_sfx`, então `bump__ref_i64` continua o mesmo símbolo), o deref
(`tk_arr_load`/`tk_arr_store`), o `tk_is_counted` do prólogo de `out` e da reescrita de atribuição,
a checagem de identidade da higiene 3 (`tk_ref_param_ty`/`tk_ref_check_pointee`, APONTADO ×
APONTADO), o oráculo (`tk_ty_scope_params`, que é também o que `tk_rc_assign` lê), a sobrecarga
(`tk_ov_sig`/`tk_ov_args_fit`/`tk_ov_refout_pair_clash`), toda checagem de argumento por índice
(teko_rc.tk, teko_expr.tk), a assinatura de delegate (teko_deleg.tk), a chamada por itab
(teko_iface.tk), os operandos de operador (teko_ops.tk) e a DI (teko_di.tk). Censo completo em
tabela no plano §72(b).

Segundo commit: **o thunk de delegate era um parâmetro GERADO com o mesmo defeito** --
`tk_deleg_thunk_fn` declarava o forwarder com o apontado, e o thunk nasce DURANTE o pass de
delegate, depois de o `tk_ref_pass` já ter passado, então nada o corrigia (a LAMBDA nunca foi
afetada: os parâmetros dela vêm de `parse_params`, logo já nascem no slot certo). A assinatura de
delegate passa a carregar o KIND de cada parâmetro (`dg_pk`, ao lado de `dg_pty`); `dg_pslot_at` é
o que o thunk declara, e com o kind na assinatura passam a existir três checagens que não podiam:
`tk_deleg_check_sig` (uma função por valor não preenche mais um slot `ref`, nem o inverso),
`tk_lambda_check_params` e `tk_deleg_check_arg_kinds` no sítio de chamada (as mesmas duas mensagens
de `tk_ref_check_call`). A mensagem de incompatibilidade agora soletra o kind: `Bump(ref u8)`.

Gate (host macOS/aarch64, `mc` 0.15.8): `rm -rf build`, build do zero; `--entry-only`
**45/45** (nenhuma fixture nova, uma tocada: `surface_refout.tk` ganha `narrowcheck`);
`mc limits ngen --config` `verdict ok`, `intrin 8/16` e `passes 15/30` -- os MESMOS da base;
`mc.toml` e `lib/rt.tk` intocados. `--dump-ast` das 45 contra a base `cc539258`: **44
byte-idênticas** (nenhuma outra fixture declara `ref`/`out`) e **1 com diff próprio** -- a tocada,
cujo diff é 14 linhas `PARAM type=<apontado>` viradas `PARAM type=uptr` mais o `narrowcheck`.
`--dump-syms` das 45, base × tip sobre as fixtures atuais: **byte-idêntico nas 45**. Probes (fora
de `tests/`), cada um rodado TAMBÉM contra o compilador da base para separar correção de
regressão: os de largura estreita (`ref u8`/`ref i32`/`out u16`/`ref bool`, campo e elemento de
array `u8`, sobrecarga, método, interface/virtual, genérico, delegate por função nua) dão **139 na
base e 42 no tip**; os de largura 8 (`ref`/`out` de classe com destrutor e `rt_live()==0`,
`ref i64` por delegate) dão **42 nos dois**. Recusas mantidas, todas verificadas.

**Dívida ADJACENTE do K2w (não tocada): `ref f64` devolve valor errado, no tip E na base.** A
largura é 8, então NÃO é o defeito do K2w; a causa é o deref usar o par INTEIRO `ld64`/`st64`
(`tk_arr_load`/`tk_arr_store`) para um apontado de ponto flutuante. Precisa do par de load/store de
FLOAT. **Ampliação (verificador do K2w, 2026-09-06):** a causa é `tk_ldn`/`tk_stn`
(`teko_struct.tk`), que mapeiam SÓ pela largura (1/2/4/8 → `ld8..ld64`/`st8..st64`) sem olhar se o tipo
é float; logo atinge TODO acesso indireto `f64` por esse par -- `ref f64` E campo de classe `f64`
(`teko_this.tk`, `p.v = p.v + 1.0` errado) -- e NÃO o array fixo local (`a[0] = a[0] + 1.0` correto,
caminho próprio). O mc já expõe `ldf64`/`stf64`/`ldf32`/`stf32` (`lib/float.mc:424-427`), nunca usados no
`ngen/`; conserto = ramo de float em `tk_ldn`/`tk_stn` antes do `type_width`. Higiene 4.

**HIGIENE 4 item A LANDADO** (plano §74(a), 2026-09-06, base `55ec9ffe`): o acesso INDIRETO a
`f32`/`f64` deixa de usar o par de load/store INTEIRO -- fecha a dívida que o verificador do K2w
ampliou (`ref f64`, campo `f64`).
- **`tk_ldn`/`tk_stn` (`teko_struct.tk`) perguntam o KIND antes da largura:** `type_kind(ty) ==
  TK_FLOAT` -> `ldf32`/`ldf64`/`stf32`/`stf64`, os acessores que `<float>` já registra
  (`lib/float.mc:424-427`, M24, zero intrínseco novo -- `mc limits` segue `intrin 8/16`). Largura
  4/8 é a mesma de um inteiro, mas o valor mora no OUTRO banco de registradores (`v`/`x` sob
  AAPCS64), então mover os bytes certos com o par inteiro é entregá-los no lugar errado. Um só
  conserto cobre todos os sítios que chamam o par: apontado de `ref`/`out`, campo de classe e de
  struct, elemento de array local/heap (`new f64[n]`)/inline, backing de propriedade, captura de
  closure, campo `static`.
- **`tk_ops_is_mem` (`teko_ops.tk`)** ganhou os quatro nomes: é a lista que diz "o 1º argumento
  deste intrínseco é um ENDEREÇO", sem a qual a espinha `obj + OFF` de um campo `f64` seria lida
  como operando de um `operator+` do tipo do objeto.
- **A captura de closure passou a ser gravada pelo banco certo.** `tk_cap_put` recebe o valor num
  parâmetro `i64` e um argumento float viaja no banco de float: a palavra gravada era LIXO. Isso não
  aparecia porque a leitura (`ld64`) também estava errada e o valor lido vinha, por acidente, do
  registrador de float que a própria lambda deixara para trás -- o "f64 atravessa bit a bit
  (medido)" da higiene 3 era esse acidente, provado agora com um probe que SOBRESCREVE a variável
  capturada depois de criar a closure (base: valor errado; tip: 42). `tk_cap_writer`
  (`teko_deleg.tk`) escolhe por kind entre `tk_cap_put`/`tk_cap_own` e os novos
  `tk_cap_putf`/`tk_cap_putf32` (`lib/rt.tk`, parâmetro DECLARADO `f64`/`f32`, gravando com
  `stf64`/`stf32`).
- **O cast de retorno estreito de delegate** (`tk_deleg_build`) era `type_width(ret) < 8`, outra
  decisão por largura pura: sobre `f32` emitia uma CONVERSÃO numérica do resultado inteiro da
  chamada. `tk_deleg_ret_narrow` espelha a regra do próprio núcleo (`walk_narrow`, só
  `TK_INT`/`TK_SINT`; "float é do módulo").
- **NÃO consertado, porque é do mc (pedido registrado, sem contorno):** `callp` é tipado `TY_I64`
  pelo núcleo (`src/gen_resolve.mc:446`), logo `walk_ret_type()` numa chamada INDIRETA sempre diz
  inteiro e o `fa_result` de `<float>` nunca move `d0` para o destino -- toda chamada indireta que
  devolve float (delegate, virtual, interface: as três são `callp`) só acerta por coincidência de
  registrador. Repro mínimo e o segundo pedido (as conversões `f32` de `fa_w` em arm64) no plano
  §74(b). Enquanto isso, fixture e probes de `f32` COMPARAM contra literais `f32` em vez de castar
  para `i64`.
- **Dívida adjacente registrada, NÃO fechada (é superfície, não higiene):** uma lista `params` não
  tem tipo de ELEMENTO -- é lista de PALAVRAS (`tk_va_put`/`tk_va_at` gravam e devolvem `i64`), e
  `params i64[] xs` foi deliberadamente recusado. Então um argumento float cai na MESMA armadilha da
  captura: `f64 total(params xs)` com `total(1.5, 2.5)` responde errado **no tip E na base** (probe,
  não é regressão). Fechar exige decidir como uma `params` ganha tipo de elemento. Plano §74(b).
- Gate (host macOS/aarch64, `mc` 0.15.10): `rm -rf build`, build do zero; `--entry-only`
  **45/45** (nenhuma fixture nova; `surface_refout.tk` ganha `floatcheck`, e contra o compilador da
  base a MESMA fixture sai 131 = `130 + 1`); `mc limits ngen --config` `verdict ok`, `intrin 8/16` e
  `passes 15/30`, os mesmos da base; `mc.toml` intocado. `--dump-ast` das 45, compilador+árvore
  da base × tip: **3 byte-idênticas** (`hello`, `primitives_ptr`, `primitives_scalar` -- as que não
  incluem `lib/rt.tk`) e **42 com o MESMO diff** (mesmo sha256 nos 42: as 26 linhas das duas
  declarações novas de `lib/rt.tk`, nada mais), a mesma partição estrutural que a higiene 3 explicou.

**HIGIENE 4 item B — `teko1.o` não determinístico: NÃO REPRODUZ** (plano §74(c), 2026-09-06). A
dívida que o verificador do S4.3 registrou (logo abaixo) foi caçada em **44 corridas** nesta
máquina (macOS/aarch64, mc 0.15.10, `build` apagado antes de cada uma) e **não apareceu uma
vez**: 10 escadas completas na árvore do tip (`teko1.o == teko2.o == teko3.o`, sempre o mesmo
`sha256`), **4 escadas na árvore da BASE `55ec9ffe`**, que reproduzem o `689dc9a6…` publicado nas
quatro (o controle: o número documentado É o que esta máquina produz), 20 recompilações de
`mc_teko.tk` pelo MESMO teko0, 6 estágios 0+1 completos, mais ambiente perturbado (env de +4 KB e
+60 KB, dois `TMPDIR`, sob carga), duas cópias da árvore compilando EM PARALELO fora do
repositório, e um estágio 1 com o config em caminho ABSOLUTO -- todos o mesmo objeto.
- **Descartado com medição:** versão do mc (só a 0.15.10 chega a escrever um `teko1.o` neste
  commit — com a 0.15.8 o estágio 1 morre no bloqueio `TE_RULE` do §70(f) sem emitir objeto);
  endereço/ASLR (a `heap[]` da arena do mc é BSS num binário PIE e os chunks vêm de `mmap(0,…)`:
  um ponteiro vazado para a saída teria divergido nas 44); slot de tabela lido além de `n` (BSS e
  `mmap` anônimo são ZERADOS — a leitura fora de `n` é errada, se houver, mas determinística);
  caminho/cwd (as cópias em `/tmp` e o config absoluto dão o mesmo objeto).
- **A instrumentação é o entregável.** `scripts/bootstrap.sh` passou a imprimir um bloco
  `provenance` (não-gated) com `mc --version` e o `sha256` de **teko0** (`build/teko`, que
  nenhum relatório tinha), `teko1.o`, `teko2.o` e `teko3.o`, mais a resposta explícita de
  `teko1.o == teko2.o`; e o job `fixpoint` do CI publica `teko0`/`teko3.o` no `summary` e, **só
  quando `teko1.o != teko2.o`**, arquiva por 14 dias o `--dump-asm` do teko0 e o do teko1 sobre
  `mc_teko.tk` com o `diff` dos dois. Assim a PRÓXIMA divergência é atribuível sem rerodar nada:
  teko0 igual + `teko1.o` diferente = não-determinismo do compilador; teko0 diferente = entrada
  diferente (mc, árvore, ou `build` sujo).
- Gate (host macOS/aarch64, `mc` 0.15.10): `rm -rf build`; `sh scripts/bootstrap.sh` →
  `FIXPOINT OK`, 45/45, 38,5 s, `teko2.o == teko3.o`, `--dump-asm` 221 316 linhas diff vazio;
  `mc.toml` e `tests/` intocados neste commit.

**S4.3 LANDADO** (plano §64(f)/§73, 2026-09-06, base `e50ab97b`, branch `feat/ngen-s43-ci`,
dois commits): a escada do fixpoint virou a **SEXTA** perna do CI — job `fixpoint`, matriz
própria de dois runners, `fixpoint (linux/x86_64)` e `fixpoint (macos/aarch64)`, rodando
`sh scripts/bootstrap.sh --os <os> --arch <arch>` e provando os três critérios do §64(f).
Descrição no §3.1 acima; detalhe e medições no plano §73.

- **`.github/actions/setup-mc` (novo)** — a resolução do `latest` de `minicompiler/mc`, o
  download, a conferência do `.sha256` e a asserção de `mc --host` saíram do job `leg` para uma
  action composta que as cinco pernas E o fixpoint usam. Saídas: `mc` (relativo), `dir`
  (absoluto, para o `$GITHUB_PATH` — `bootstrap.sh` chama `mc` pelo NOME), `tag`, `version`.
- **`bootstrap.sh` ganhou o `[target]` de linux** (segundo commit, causa-raiz do primeiro CI
  vermelho): o compilador ensinado é escrito pelo backend de executável do HOST, e o writer ELF
  do mc põe `PT_INTERP`/soname musl por padrão → em runner glibc o `build/teko` existe e
  não executa (`not found`, exit 127, o loader falando). `write_target_tail` anexa
  `interp`/`libc = "gnu"` **só quando o alvo é linux e aquele loader existe na máquina**; musl
  não anexa nada; macOS não muda. A escada RODA o que constrói, então o loader desta máquina é
  o oráculo.
- **Medido no CI** (run `34043945146`, mc 0.15.10, tudo verde — 5 pernas + 2 fixpoint +
  agregador): linux teko0 1,924 s → 4,085 / 4,089 / 4,084 s, `--dump-asm` 221 221 linhas diff
  vazio, 45/45, total **26,887 s**, `teko1.o == teko2.o` = 2 062 312 B, `sha256` `33e7df95…`;
  macOS teko0 3,032 s → 6,927 / 4,963 / 5,232 s, 221 134 linhas, 45/45, total **45,267 s**,
  `teko1.o == teko2.o` = 1 714 920 B, `sha256` `689dc9a6…` — **byte-idêntico ao objeto do host
  local**, primeira evidência de reprodutibilidade entre máquinas; e dois runs consecutivos da
  branch deram o MESMO `sha256` nos dois pares (evidência entre runs).
- **`teko1.o` NÃO é determinístico entre corridas (verificador do S4.3) -- NÃO REPRODUZIU na higiene 4:**
  mesma máquina/commit, duas escadas: numa `teko1.o` (`90485ed5…`) ≠ `teko2.o`; noutra os três iguais.
  `teko2.o == teko3.o` e o hash final (`689dc9a6…`) fecharam sempre. A higiene 4 (bloco acima, plano
  §74(c)) rodou 44 corridas -- inclusive 4 escadas da árvore DESTE commit, que dão `689dc9a6…` nas
  quatro -- sem uma divergência, e descartou versão do mc, endereço/ASLR, leitura além de `n` e
  caminho/cwd; o que ficou foi a instrumentação de provenance no `bootstrap.sh` e no CI, para a
  próxima divergência ser atribuível. Segue pré-requisito do golden.
- **Não é gate (ainda):** o `sha256` é impresso no `summary`, não comparado; vira golden
  versionado quando estabilizar (molde do `tests/golden/mc2.sha256` do mc). O agregador
  `mc build ngen && run` segue dependendo só da matriz `leg`.
- **Dívidas:** escada no **Windows** (precisa do sysroot `lld-link` dentro do job) e em
  **linux/aarch64** (o par tem perna; ficou fora só para o job custar dois runners).
- Gate local (host macOS/aarch64, `mc` 0.15.10): `rm -rf build`; `sh scripts/bootstrap.sh`
  → `FIXPOINT OK`, 45/45, 45,2 s; `mc.toml`, `*.tk` e `tests/` intocados;
  `git status` limpo.

**V0 — RECUSAS LANDADO** (plano §76, 2026-09-06, base `77019bd6`, três commits): o primeiro crumb do
desvio "v0.1.0 estável" — a regra do corte é **ZERO resultado errado silencioso**: o que não está
ensinado é RECUSADO com mensagem, no estilo `teko: <causa curta>`. Nenhuma superfície nova, nenhuma
fixture nova (as recusas só têm probe — o corpus não tem fixtures de erro), `mc.toml` intocado.

- **Item 1 — float numa lista `params` (fechado, `271718b8`, `teko_params.tk`).** A lista é de
  PALAVRAS: `tk_va_put`/`tk_va_at` (lib/rt.tk) gravam e devolvem por parâmetro `i64`, então um valor
  que viaja no banco de float não é gravado nem devolvido por esse par — `f64 total(params xs)` com
  `total(1.5, 2.5)` respondia com o que o banco inteiro tivesse (dívida adjacente da higiene 4,
  §74(b)). As DUAS pontas recusam onde o tipo é VISÍVEL:
  - **o ARGUMENTO** que cai na lista (`tk_va_check_float_args`, só sobre a cauda empacotada — um
    parâmetro fixo mantém o tipo declarado dele). O tipo vem do próprio nó para literal (`1.5` é um
    `N_INT` de tipo `f64`, `fl_lit` de `lib/float.mc`) e cast, e do oráculo de parse (`tk_pty_of`)
    no resto. Mensagem: ``teko: a `params` list holds words; a float argument is not taught yet``.
  - **o ELEMENTO lido como float** (`tk_va_check_float_read`, roda ANTES do walk rebaixar o índice),
    nos três destinos que soletram o tipo: inicializador de local float, atribuição a um, e o
    `return` de uma instância cujo retorno declarado é float. Mensagem: ``teko: a `params` list holds
    words; its element does not read as a float``.
  - **Limites documentados (silêncio deliberado):** um parâmetro float repassado (a tabela
    `tk_slv_find` NUNCA vê parâmetro — higiene 3) e uma leitura que atravessa uma CHAMADA (a palavra
    entra como o `i64` que é e o que volta é o tipo do callee). A mesma regra do
    `tk_check_field_store`: só recusa o que sabe.
- **Item 2 — `when` no braço `_` final da switch expression (fechado, `ec223f14`,
  `teko_switch.tk`).** A dobra vai do ÚLTIMO braço para trás, então esse braço é a base
  incondicional e a condição dele — a guarda inclusive — nunca é testada; um `when` escrito ali era
  descartado em silêncio e o braço tomado assim mesmo (dívida registrada pelo crumb SWITCH, D228).
  Recusa na linha do próprio braço: ``teko: the last `_` arm of a switch expression cannot carry a
  `when` ``. **Nada mais muda:** `_ when c` no MEIO continua dobrando para `1 && c` e sendo testado,
  e um braço escrito DEPOIS de um `_` puro segue morto (não errado) — o `1` do `_` casa primeiro.
- **Item 3 — retorno de chamada passa a ser tipado pelo oráculo de parse (fechado, `1d43f904`,
  `teko_struct.tk`).** `tk_pty_of` respondia por nó já tagueado e por local bare, e -1 no resto
  — logo `b.useCircle(f())` com `f()` devolvendo `Square` onde se pede `Circle` passava em silêncio
  (o único furo que o verificador da higiene 2 deixou: campo de outro objeto já era pego, `tk_field_use`
  tagueia). Agora responde para `N_CALL` por `decl_find`/`decl_ret` — o MESMO par que o oráculo
  PASS-TIME (`tk_ty_of`) já lê para o mesmo nó, então posição de argumento diz o que posição de
  inicializador sempre disse. Os dois consumidores ganham juntos (`tk_vcall_args_check`,
  `tk_ifargs_check`); `ref`/`out` recebe NOME, nunca chamada, e a DI não lê esse oráculo — não há
  terceiro sítio.
  - **Limites, medidos:** callee declarado ABAIXO do sítio (o core responde sobre o que já parseou;
    um protótipo acima também não fechou), `callp` indireto que declaração nenhuma nomeia, e — limite
    ANTIGO da checagem, não deste oráculo — **receptor que é PARÂMETRO**, cuja classe o parse não
    conhece, então a checagem de argumento nem chega a rodar (probe: com receptor LOCAL a mesma
    chamada é recusada, com receptor PARÂMETRO passa; vale na base e no tip).

Probes (em `_probe/`, apagado; cada um também rodado contra o compilador da base para separar
correção de regressão): item 1 — literal float, local float, `f64 v = xs[0];`, `v = xs[1];` e
`return xs[0] + xs[1];` de uma instância `f64` (cinco recusas); item 2 — `_ when g` como último braço
(recusa) e um controle com `_ when g` no meio + braço guardado depois do `_` (compila, roda 42,
guarda honrada); item 3 — `Square` por vtable e por itab (recusa `teko: a value of type Square does
not convert to Circle` no tip, **exit 0 silencioso na base**) e um controle com o tipo CERTO nas duas
formas de despacho + chamada escalar em posição de argumento (compila, roda 42).

Gate (host macOS/aarch64, `mc` **0.15.12**): `rm -rf build`, build do zero; `--entry-only`
**45/45**; `--dump-ast` das **45 byte-idêntico** ao compilador da base `77019bd6` (`same=45 diff=0`)
— recusa não muda código aceito; `mc limits ngen --config` `verdict ok`, `intrin 8/16`, `passes
15/30`, os mesmos da base; `sh scripts/bootstrap.sh` → **`FIXPOINT OK`** (teko1.o == teko2.o ==
teko3.o, `sha256` `034843cd…`, `--dump-asm` 191 586 linhas diff vazio, teko1 compila as 45, 62,4 s);
`mc.toml`, `tests/` e `scripts/` intocados; `git status` limpo.

**Dívida ADJACENTE achada, NÃO fechada (não é deste crumb):** `xs[0]` de uma lista `params` usado como
ARGUMENTO de uma chamada por vtable morre em `expression with no codegen` — o `N_INDEX` sobrevive ao
walk da instância porque a chamada já foi rebaixada a `callp` no parse. Reproduzido na BASE e no tip
(não é regressão), registrado aqui.

**S4.3b LANDADO — o `fixpoint` cobre as CINCO pernas** (plano §78, 2026-09-06, base `756fd924`,
branch `feat/ngen-s43b-fixpoint-all`, três commits): a escada deixa de rodar em dois pares e passa
a rodar nos **mesmos cinco** que as pernas nativas cobrem. Descrição no §3.1 acima; medições no
plano §78. Zero mudança em `*.tk`, `mc.toml` e `tests/`.

- **linux/aarch64** (`ubuntu-24.04-arm`) era só custo de runner: o par já tinha perna e o
  `write_target_tail` já conhecia o loader dele (`/lib/ld-linux-aarch64.so.1`). Terceira entrada
  da matriz, nada mais.
- **Windows** (as duas arquiteturas) exigiu as duas peças que a dívida previa.
  **`.github/actions/windows-sysroot`** (nova) é a montagem do sysroot fatorada para fora da perna
  — os dois jobs Windows a usam, então não há como montarem sysroots diferentes; ela também põe o
  LLVM no `$PATH` em forma Windows e aponta o `TMPDIR` para o temp do runner. E
  **`bootstrap.sh --linker-toml FILE`** troca o `[linker] cc` do `mc.toml` pelos blocos
  `[sysroot]`/`[linker]` do arquivo (a matriz escreve a MESMA linha `lld-link` da perna). POSIX
  `sh`, sem `set -e`, status por passo, como o resto do arquivo.
- **Nomes com `.exe` e o objeto que o critério compara.** O `mc` anexa o sufixo do host ao
  `[compiler].out` sozinho; o `[project].out` é o script que nomeia, e o objeto sai de `out + ".o"`
  — logo no Windows o `cmp` é entre `teko2.exe.o` e `teko3.exe.o` (COFF). O laço das 45 fixtures
  roda `.exe`. **Nada foi preciso do lado do `mc`:** `--dump-asm` de COFF, `--entry-only`,
  `--compiler-only` e o link por `[linker]` funcionaram como nos outros pares, na 0.15.12 pinada.
- **`--os`/`--arch` agora são conferidos contra `mc --host`** (armadilha nova, §5.1 item 32): a
  escada executa todo estágio que constrói, então alvo ≠ máquina é cross-build sem nada para rodar.
- **Medido no CI** (run `34052547541`, mc 0.15.12, **11/11 verde** — 5 pernas + 5 fixpoint +
  agregador), estágios teko0 / 0→1 / 1→2 / 2→3 e `sha256(teko2.o)`:

  | par | teko0 | 0→1 | 1→2 | 2→3 | total | `--dump-asm` | `sha256(teko2.o)` |
  |---|---:|---:|---:|---:|---:|---:|---|
  | linux/x86_64 | 1,494 s | 3,183 s | 3,157 s | 3,163 s | 20,682 s | 192 502 linhas | `d37e4cb3…` |
  | linux/aarch64 | 2,578 s | 7,457 s | 7,189 s | 7,519 s | 41,938 s | 191 667 linhas | `6e80a42e…` |
  | macos/aarch64 | 2,805 s | 5,792 s | 4,166 s | 4,199 s | 33,751 s | 191 586 linhas | `034843cd…` |
  | windows/x86_64 | 2,360 s | 4,842 s | 4,835 s | 4,857 s | 41,930 s | 191 564 linhas | `a45444fd…` |
  | windows/aarch64 | 3,062 s | 7,491 s | 7,409 s | 7,405 s | 71,524 s | 191 564 linhas | `30aed5f0…` |

  45/45 fixtures e `teko1.o == teko2.o == teko3.o` nos CINCO. O objeto de macos/aarch64
  (`034843cd…`) é **byte-idêntico ao do host local**, a mesma evidência entre-máquinas que o §73
  registrou.
- **Dívida que FICA:** o `sha256` segue **reportado, não barrado** (golden versionado só quando
  estabilizar, molde do `tests/golden/mc2.sha256` do mc) e o **agregador não mudou** — `mc build
  ngen && run` continua dependendo só da matriz `leg`; promover a escada a check obrigatório é
  decisão de ruleset. Os cinco nomes novos de context estão listados em
  `docs/design/pr-org-ngen.md` §2/§5.
- Gate local (host macOS/aarch64, `mc` 0.15.12): `rm -rf build`;
  `sh scripts/bootstrap.sh` → **`FIXPOINT OK`**, 45/45, `034843cd…` nos três objetos,
  `--dump-asm` 191 586 linhas diff vazio; e o MESMO run por `--linker-toml` com um bloco `cc`
  equivalente reproduz os três hashes byte a byte (é a prova de que a substituição do `[linker]`
  deriva um config equivalente); `*.tk`, `mc.toml` e `tests/` intocados.

**V1 — RETORNO FLOAT POR CHAMADA INDIRETA LANDADO** (plano §79 e
`docs/design/plano-v1-float-callp.md`, 2026-09-06, base `a66b80c9`, cinco commits): o segundo crumb
do desvio "v0.1.0 estável", e o último resultado errado silencioso conhecido. `callp` não nomeia
callee, então o núcleo tipava o nó `TY_I64`, `walk_ret_type()` respondia inteiro em TODA chamada
indireta e o `fa_result` do `<float>` nunca movia `d0`/`xmm0` para o destino — delegate, método
virtual e método de interface (as três são `callp`) só acertavam por coincidência de registrador.
Com o **mc 0.15.13** (§3.2) um cast DIRETAMENTE sobre o `callp` declara o retorno, e é essa a única
grafia possível: a teko passa a emiti-la nos cinco construtores.

- **UM shaper, `tk_callp_ret(ret, call)`** (`teko_array.tk`, logo abaixo de `tk_cast`): envolve
  num cast o retorno FLOAT e o INTEIRO ESTREITO — este pela regra do próprio núcleo (`walk_narrow`,
  `mc/src/gen_walk.mc`: `uptr` é a palavra da máquina e nunca é estreito, o que não é `TK_INT`/
  `TK_SINT` é do módulo, `void` não toma cast) — e devolve a chamada intocada no resto. Espelhar a
  regra do núcleo em vez de chamar `walk_narrow` é deliberado: é símbolo INTERNO de `gen_walk.mc`, e
  a 0.15.12 já quebrou a teko renomeando internos (§3.2).
- **`tk_deleg_ret_narrow` APAGADO** (`teko_deleg.tk`): existia só para manter o float LONGE de um
  cast que, sem o contrato do núcleo, converteria o resultado inteiro da chamada. Com o contrato, os
  dois casos são a mesma declaração na mesma sintaxe, e o shaper único os cobre.
- **Os cinco sítios**, todos envolvendo DENTRO do construtor: `tk_deleg_build`
  (`teko_deleg.tk`, delegate/lambda, `dg_ret_at`), `tk_emit_call` (`teko_expr.tk`, virtual por
  receptor tipado no parse), `tk_this_emit` (`teko_this.tk`, virtual pelo `this` implícito),
  `tk_pend_emit_call` (`teko_typeof.tk`, virtual por receptor que só o oráculo tipa) e
  `tk_itab_emit` (`teko_iface.tk`, interface, `im_ret_at(sr_m0_at(si) + j)`). Todo call-site funila
  num desses cinco — DI e lambda não têm forma própria (lambda é delegate; serviço injetado é campo
  ou local e cai no virtual/itab).
- **ARMADILHA — a regra de identidade** (quebra em SILÊNCIO, compila e devolve o valor errado de
  novo): o núcleo casa o cast com o filho **IMEDIATO** do `callp`, então (a) o envelope só pode
  acontecer no sítio que CONSTRÓI a chamada — um nó inserido depois, entre o cast e o `callp`,
  desfaz o casamento sem erro nenhum; e (b) todo registro por POSIÇÃO de nó (`tk_xt_put`/`tk_xt_add`
  e o `node_assign`/`tk_node_replace` que copia o resultado para o nó da árvore) vale sobre o nó que
  o shaper DEVOLVE, nunca sobre o `callp` interno — em `tk_emit_call` o `tk_xt_add` vem
  deliberadamente DEPOIS do shaper.
- **Fixtures, um caso por forma de despacho, nenhuma fixture nova:** `surface_delegate.tk`
  `fdcheck` (`delegate f64 Scale(f64 x)` sobre função nua, sobre `new Scale(dbl)`, de campo de
  classe, aninhado em `1.0 + d(2.0)`, e o gêmeo `f32`), `types_class.tk` `fvcheck`
  (`virtual f64 area()` + `override`, pelo receptor escrito, pelo `this` implícito e por parâmetro
  tipado na BASE) e `types_interface.tk` `ficheck` (membro `f64 span()` por receptor de tipo
  interface e por parâmetro). `expect-exit: 42` nas três.
- **Probes (`_probe/`, apagado), base → tip:** o repro do §74(b) sem nada do ngen — o MESMO
  binário responde 5 com o cast e 3 sem ele, isto é, o contrato do núcleo já valia e o que faltava
  era a teko EMITIR o cast; delegate `f64`/`f32` (base `1.0 + d(2.0)` = 3.0, erro no 2º check → tip
  42); virtual `f64` (base erra já no `p.area()` DIRETO, mais fundo que o delegate → tip 42);
  interface `f64` (base erra no direto → tip 42); laço de 100 closures `f64` com `rt_live()==0`
  (42 nos dois — o reclaim continua lendo o nó certo); `(i64)(f32 * 10.0f)` em arm64 (25 nos dois, a
  conversão single que a 0.15.13 trouxe).
- **Achado do plano §5 probe 4, MEDIDO: o estreitamento inteiro por despacho indireto JÁ estava
  certo na BASE** nos sítios #2–#5 (`virtual i32`/`u8`, `override`, por `this`, por parâmetro,
  `interface i32`: 42 na base e no tip). Quem estende é o CALLEE (extensão M45), então o cast que o
  shaper unificado passa a pôr nesses quatro é cinto-e-suspensório, não conserto — o `sxtw`
  idempotente que o §74(b) previa. Não era defeito vivo.
- **Dívida ADJACENTE, medida e NÃO fechada (não é regressão, é a dívida de superfície do `params`):**
  um float vindo de chamada indireta como argumento de uma lista `params`. Com V1 a recusa do V0
  item 1 passa a alcançar as formas construídas no PARSE — `total(p.area())` agora para em
  ``teko: a `params` list holds words; a float argument is not taught yet`` —, mas **não** a forma
  delegate: o `tk_params_pass` roda ANTES do `tk_deleg_pass` (ordem em `teko.tk`), então
  `total(d(2.0))` ainda é um `N_CALL` cru quando `tk_va_arg_ty` o examina e segue em silêncio (base
  48, tip 32 — lixo dos dois lados, mesma classe). Fechar exige o tipo de ELEMENTO da `params`
  (`params T[]`, decisão de superfície do §74(b)/§76) ou mover a checagem para depois do passe de
  delegate; um palpite pela tabela `tk_slv_find` recusaria programa CORRETO (armadilha 27).
- Gate (host macOS/aarch64, `mc` **0.15.13**): `rm -rf build`, build do zero; `--entry-only`
  **45/45**; `--dump-ast` das 45 contra o compilador+árvore da base `a66b80c9`, com o MESMO mc dos
  dois lados: **`same=42 diff=3`** — só as três fixtures tocadas, e o diff de cada uma é ADITIVO
  fora da renumeração dos temporários `$gN` (nenhuma linha removida que não seja um `$gN`
  deslocado); `mc limits ngen --config` `verdict ok`, `intrin 8/16`, `passes 15/30` — os mesmos da
  base; `sh scripts/bootstrap.sh` → **`FIXPOINT OK`** (teko1.o == teko2.o == teko3.o,
  `8482faa6…`, `--dump-asm` 191 811 linhas diff vazio, teko1 compila as 45); `mc.toml`,
  `lib/rt.tk`, `scripts/` e `.github/` intocados; `git status` limpo.

## 5.1 Armadilhas já pagas (não repita)

1. **`mc --exe` emite Mach-O SEMPRE.** `minicompiler/mc` `src/main.mc:227` faz
   `--exe → bname = "macho-exe"`, ignorando host e `[target]`. Num runner/host
   Linux isso gera `Exec format error` (ENOEXEC, exit 126). **Compile sempre
   pelo caminho `mc build DIR --config FILE`**, que honra `[target] os/arch` e
   `[linker]` do toml. O CI já faz assim (gera um toml por fixture a partir do
   `mc.toml`, trocando só `[project].entry`/`out`).
2. **O runner injeta `bash -e`.** Um passo de CI que pretende acumular falhas
   (`status=1; continue`) precisa de **`set +e`** no topo, senão aborta na
   primeira e esconde o estado das demais.
3. **Confira o CI da branch de feature ANTES de drenar.** Drenar primeiro e
   olhar depois já deixou as duas branches canônicas vermelhas uma vez.
4. **`ld` avisa `missing .note.GNU-stack`** em todo `.o` emitido pelo mc → o
   binário sai com stack executável. É item do lado do mc, não do `ngen/`.
5. **Registrar tipo SEM `type_new` = identidade colapsa.** Usar `type_alias`
   num struct/class (ex.: `type_alias("Point", TY_UPTR)`) faz o id colar ao
   `TY_UPTR` → `.` resolve membro por NOME cru, sem distinguir tipos
   não-relacionados que declarem o mesmo campo. **Usar `type_new(name, 8, 8,
   TK_INT)`** — preserva identidade estática; se dois tipos de-fato-não-ligados
   declaram `x`, é error claro (`"type of the left side of '.' is not known"`).
   Auditado contra `mc docs/reference/hooks.md:350`; SEM regressão de ABI
   (8/8 = pointer).
6. **O core reporta o tipo de PARÂMETRO — mas só depois de a declaração
   fechar.** As cinco `decl_*` do M31 respondem a assinatura já parseada, e
   `decl_param_type` devolve o id de `type_new` sem colapsar em `TY_*` (medido).
   Dentro do corpo que está sendo parseado não há resposta — daí o `.` sobre
   receptor de tipo desconhecido ser **DEFERIDO** ao `pass()` (`teko_typeof.mc`),
   onde a unidade inteira existe e o tipo declarado do parâmetro se lê. Resolver
   pelo NOME do membro **não** é aceitável nesse caso: o nome que só OUTRO tipo
   declara não é membro deste receptor (foi o defeito 2 da entrega 3, corrigido
   na entrega 4). O por-nome só sobra como último recurso DENTRO do pass, depois
   de o oráculo dizer que não sabe o tipo — um global, ou expressão que ninguém
   tipa.
7. **Ordem de declaração:** método só chama métodos ACIMA dele (mesma limitação
   do `examples/lang`); consertar exige record/replay. Planejado pra release
   seguinte do mc.
8. **Sem construtor com argumentos** (`new Nome` apenas) (`base.m()` existe desde a entrega 5, crumb 0 — D219) —
   ainda não ensinados. Fila de D215.
9. **Achado no repo do mc (não confirmado):** `examples/lang/lang_expr.mc:42-44`
   reutiliza nó do receptor em `ld64` vtable e na lista de args; método virtual
   de aridade ≥1 quebraria. No `ngen/` está contornado por clonagem com guarda.

10. **Trait como tipo de declaração dá mensagem GENÉRICA.** `new Trait` e
    `class C : Trait` acusam com mensagem dedicada e clara, mas `A a;` (trait
    como tipo de variável) falha antes, no parser do core, com
    `expected ; after expression` — sem dizer que a causa é "trait não é tipo".
    Rejeita corretamente, mas o diagnóstico é pobre; é consequência de o trait
    não ter `type_new` (por desenho, D216). Dívida cosmética conhecida.
11. **Campos vindos de trait entram DEPOIS dos campos próprios da classe**,
    independentemente de onde o `use` aparece no corpo (`teko_class.mc:439`).
    Duas classes que usam o mesmo trait têm offsets independentes e corretos.

12. **`extern` de libc POSIX não linka no Windows** (achado ao abrir as 5 pernas).
    O Windows não tem C runtime nenhum: o link é `-nodefaultlib` + kernel32, e o que
    resolve os nomes POSIX é a camada de sistema do `mc` (`mcrt.obj`, quinze nomes:
    `open`/`read`/`write`/`close`/`creat`/`exit`/`_exit`/`mmap`/`chmod`/`mkdir`/
    `unlink`/`posix_spawnp`/`waitpid`/…). `surface_overload_free.tk` declarava
    `extern i64 getpid()` e dava `undefined symbol: getpid` nas duas pernas Windows —
    trocado por `chmod` (existe nas cinco), perguntado sobre um path inexistente e
    comparado contra resultado POSITIVO, porque POSIX responde -1 e o shim do Windows
    responde 0. **Fixture nova só declara `extern` que esteja nessa lista.** Um
    `extern` declarado e NÃO chamado não custa nada: o `mc` só emite símbolo
    indefinido para o que é referenciado (é por isso que os `<sys>` de `lib/rt.mc`
    — `munmap`, `_NSGetEnviron`, `posix_spawnp` — não quebram o link).

13. **`p_start()` NÃO aponta para a fonte quando o token foi substituído.** A
    substituição higiênica do `p_subst_name` (instância de genérico) troca
    `tok_start`/`tok_len` pelo LEXEMA DE SUBSTITUIÇÃO, que mora na arena
    (`mc/src/lex.mc` `subst_apply`) — então `p_start() + tamanho-do-nome` cai em
    lugar nenhum e varrer a partir dali é lixo. Quem precisa **espiar o que vem
    depois do token atual** (o `Tipo.campo` do D220 tem que distinguir
    `Shape.made = 1;` de `Shape s = new Shape;`, e o parser guarda UM token de
    lookahead) usa **`cp`**, o cursor do lexer — é de onde o próximo token vai ser
    lido, no mesmo buffer que `p_src_end()` limita, e é o que o próprio
    `stmt_syntax` do core compara no seu guard. Um comentário entre o nome e o `.`
    não é lido pela varredura (só espaço em branco), e o caso cai na recusa clara
    do `parse_var`, nunca em silêncio.

14. **mc ≥ 0.12.1 (patch pós-M42): `[target].libc` vira FAMÍLIA (`"gnu"|"musl"`) e a grafia
    soname (`"libc.so.6"`) é RECUSADA.** O workflow escolhe a grafia pela versão resolvida
    (`sort -V` contra 0.12.1) — as pernas Linux carregam `libc_family: gnu` na matriz.
    Também novo: `[target].link = "dynamic"|"static"`, flags `--libc=`/`--link=`/`--interp=`.
15. **mc ≥ 0.12.0 (M42): o `mc build` Linux escreve ELF dinâmico SEM `[linker]`, com loader
    e soname **musl por default**.** Num runner glibc (ubuntu) o compilador ensinado sai com
    `interp` de musl e o `mc build` falha em `mc: cannot run: build/mc-teko`. As pernas
    Linux do CI nomeiam o par glibc no `[target]` (`interp = "/lib64/ld-linux-x86-64.so.2"` ou
    `"/lib/ld-linux-aarch64.so.1"`, `libc = "libc.so.6"` — mc `docs/build.md` §`[target]`) e
    não têm mais `[linker]`. `mc.toml` versionado (linux/x86_64 + `[linker] cc`) segue
    intacto; o CI deriva o config por perna.

16. **Um lookup com fallback nunca chama a si mesmo (mesma função) sobre uma string que ELE
    PRÓPRIO construiu.** `tk_struct_find` (namespace, entrega 5 N1) ganhou um fallback
    (`tk_ns_resolve`) que tenta candidatos qualificados; um candidato que falha chama de volta a
    função "exact + fallback" original — e o próximo candidato é sempre MAIOR que o anterior
    (mais um prefixo), nunca repete o argumento, então a recursão nunca bate uma base e nunca
    converge. Sintoma: `EXC_BAD_ACCESS`/`SIGSEGV` no meio do parse, sem mensagem — só visível com
    `lldb bt` (a pilha mostra as duas funções alternando centenas de vezes). Correção: separar o
    scan puro (`tk_struct_find_exact`, sem fallback) e fazer todo sítio que testa uma string
    CONSTRUÍDA internamente (o próprio `tk_ns_resolve`, o reopen check de `partial`, uma busca por
    nome já manglado) chamar a versão exata — só o sítio que lê o que a FONTE escreveu chama a
    versão com fallback.

17. **Um prefixo do NÚCLEO nunca vê o pós-fixo do MÓDULO.** `parse_unary()` checa a
    própria tabela de prefixo (`ops_init`: `- ~ ! &`) ANTES de `parse_primary` — onde
    `syntax_expr` mora — e lê o operando por recursão direta em `parse_unary()`, que nunca
    consulta `.`/`[` (`syntax_infix`, prec 12). `!b[1]` chega no `[` já como `N_UNARY(!,
    b)`: o núcleo devolveu o unário ANTES de o `[` do módulo ter a chance de aparecer.
    Registrar `syntax_expr("-", ...)` não conserta nada — é código morto, medido com um
    handler forçado a devolver um valor distinto que nunca disparou para `-x`. Só `+`
    escapa dessa armadilha (M45's `tk_unary_plus`) porque `ops_init` nunca o registrou, daí
    ele cai em `parse_primary` como um token comum. A correção é do lado do PÓS-fixo, não
    do prefixo: `tk_dot`/`tk_bracket` sinkam pela cadeia de `- ! ~` que RECEBERAM como
    `left`, resolvem contra o operando de verdade, e reembrulham (`teko_prefix.mc`).

18. **`top_add()` limpa `p_decl_name()` como efeito colateral -- um `top_add` ANINHADO (chamado
    de DENTRO do corpo de uma declaração ainda sendo lida) apaga o nome da declaração
    ENVOLVENTE, e nada restaura sozinho.** `tk_lambda_finish` (`teko_deleg.mc`, K4) constrói a
    função da lambda e chama `top_add(f)` (e mais três: vtable/release/allocator) ENQUANTO ainda
    está no meio de parsear a instrução do CALLER (`Op f = new Op(...) => ...;`). Restaurar
    `p_decl_name()` para o nome salvo ANTES desses `top_add` (como o código fazia) é inútil: cada
    `top_add` seguinte zera de novo, e o resto da instrução -- e de TUDO que vem depois dela no
    mesmo corpo -- passa a ler `p_decl_name()==0` pelo resto daquela declaração. Ficou invisível
    até o K4c precisar ler `p_decl_name()` DEPOIS de uma lambda construída no mesmo corpo (o taint
    por função, item 1). Regra: **quem chama `top_add` para uma declaração GERADA dentro de outra
    ainda em curso restaura `p_decl_name()` por ÚLTIMO, depois de TODO `top_add` que fizer** --
    não no meio.
19. **`break;` puro (sem número) já nasce `nd_val = 1` no núcleo -- `continue;` puro nasce
    `nd_val = 0`.** As duas palavras NÃO são simétricas. `tk_loop_rewrite_stmt` (`teko_loop.mc`)
    só funciona porque, num `break` puro, `lvl(1) > depth(0)` já é verdadeiro no nível mais externo
    do corpo (por isso um `break;` escrito dentro de um `for`/`foreach` escapa o wrapper de UMA
    volta inteiro, não só ele) -- um `continue;` puro precisaria da MESMA sorte, mas `nd_val` vem
    0, daí o `if (lvl == 0) lvl = 1;` explícito que só o ramo `N_CONTINUE` tem. Confirmado com o
    build local (`for (;;) { infCount++; if (infCount == 4) break; }` de `surface_loops.tk` só
    para no valor certo por causa disso) antes de escrever K5 -- reusar `tk_loop_rewrite_stmt` sem
    entender essa assimetria teria parecido "óbvio" e estaria errado.
20. **Uma varredura léxica pré-parse (§50 O1) roda ANTES do primeiro token, então `p_file()`
    ainda responde 0 -- só `lex_file()` (o topo da pilha de frames do lexer, sem depender de
    lookahead algum) dá o nome do arquivo sendo varrido, tanto no `tk_fwd_init()` de `user_init()`
    quanto logo após um `import` empurrar um novo arquivo (`p_push_source`'s própria ressalva:
    "the push does not touch the pending lookahead token", então `p_file()` ali ainda responderia
    pelo arquivo INCLUIDOR). Usar `p_file()` em vez de `lex_file()` nesse ponto não erra alto —
    devolve um ponteiro nulo que corrompe qualquer coisa que o trate como string (`tk_origin_of_file`
    tem uma guarda `if (f == 0) return 0`, mas um `out_str`/`cstrlen` direto sobre ele segfaulta).
    Também vale a ordem: `tk_access_init()` (que fixa `tk_proj_dir`/`tk_proj_len` a partir de
    `cfg_file`/`lex_file()`) tem que rodar ANTES da varredura, não depois — ela não toca a posição
    do lexer (sem `p_push_source`, ao contrário de `tk_loop_init`), então adiantá-la é seguro e é
    o que dá à varredura um `tk_origin_of_file` que responde certo (um `trait` usado antes de
    declarado, cujo `use` compara a origem do placeholder contra a da classe que o usa, foi o que
    expôs a ordem errada -- "internal to another project" num programa de um projeto só).
21. **Um estado "adotado" separado do próprio `sr_part` do tipo mente sobre o caminho comum.** A
    primeira versão do backstop `tk_fwd_pass` (§50 O1) marcava uma linha `fw_adopted` só dentro do
    ramo de ADOÇÃO de `tk_type_add` (quando a linha já existia como `TK_PFWD`) -- e um tipo
    declarado NA ORDEM NORMAL (nunca usado antes, então `tk_type_add` só acrescenta uma linha
    `TK_PWHOLE` fresca, nunca passa pelo ramo de adoção) nunca settava essa flag, disparando "is
    used but never declared" para TODO tipo do programa, mesmo os 39 fixtures antigos que não têm
    nada a ver com O1. A correção: não existe um segundo bit — `sr_part_at(si) == TK_PFWD` no fim
    da unidade JÁ significa "foi materializado por um uso e nunca adotado"; qualquer outro estado
    (inclusive o `TK_PWHOLE` de uma declaração comum) significa "resolvido", sem tabela extra.
22. **`tk_struct_find_exact` de um JOIN qualificado não basta para dizer "este segmento é uma
    palavra reconhecida" -- o SEGMENTO em si só é reservado pela declaração REAL do namespace
    (§50 O2, achado durante a ressalva 2).** `geo.Circle` só dispara `tk_ns_seg_stmt`/
    `tk_ns_seg_expr` porque `namespace geo { ... }`, ao ser PARSEADA de verdade, chama
    `tk_ns_seg_register("geo")` (`syntax_stmt`/`syntax_expr` sobre a palavra "geo" em si, não sobre
    o nome qualificado "geo__Circle"). Ensinar `tk_ns_walk`/`tk_fwd_row` a materializar
    "geo__Circle" como PFWD não adianta nada se "geo" nunca virou uma palavra hookada -- o parser
    lê `Geo.Item` como dois tokens soltos e erra `expected ; after expression` bem antes de qualquer
    tabela de forward ser consultada. Correção: a VARREDURA (`tk_fwd_try_namespace`, teko_fwd.mc)
    chama `tk_ns_seg_register` no MESMO instante em que reconhece o cabeçalho `namespace A.B { ... }`
    -- idempotente, mesma função que a declaração real chama, sem tabela paralela.
23. **`#include` sequencial dentro de `teko.mc` também é declare-before-use para uma VARIÁVEL global,
    não só para função (§50 O3).** `tk_own_methods`/`tk_nconf`/`tk_ntu`/`tk_nud`/`tu_tr`/`ud_tr`
    (o estado que um `use`/trait em voo mantém) só existem a partir de `teko_trait.mc`, incluído
    DEPOIS de `teko_fwd.mc` -- uma função que precisa deles (o save/restore que `tk_fwd_materialize`
    tem de fazer ao redor do seu próprio `p_push_source`, no molde de `tk_gen_replay`) não cabe em
    `teko_fwd.mc`, mesmo com um protótipo à frente: função forward-se-declara (o resto do arquivo já
    faz isso com `tk_trait_scan`/`tk_ns_register`/etc.), mas uma GLOBAL não tem essa forma aqui. A
    tabela do scanner (`fw_*`) e o que não depende desse estado (`tk_fwd_skip_decl`,
    `tk_fwd_in_flight`, a pilha de voo) ficam em `teko_fwd.mc`; `tk_fwd_materialize` em si mora em
    `teko_class.mc`, ao lado de `tk_conf_name` -- o único chamador, e o primeiro arquivo da cadeia de
    include onde o estado do trait já é visível.
24. **Um handler de `on_source` NÃO pode empurrar fonte de dentro do próprio callback -- e não
    precisa: o que ele quer varrer já chegou.** `on_source` (0.15.3) chama o handler com o frame já
    no topo da pilha do lexer; qualquer `lex_push_mem` alcançado de dentro dele (`p_push_source`,
    `lex_include`, um `#include`) é recusado com `mc: on_source handler pushed a source: <name>` --
    sem a guarda seria recursão até `SIGSEGV` (anunciar um push que abre outro, que anuncia mais um,
    ...). `tk_fwd_on_source` (`teko_fwd.mc`) só LÊ `src`/`len` (a varredura é byte a byte, nunca
    consome um token nem empurra nada), então nunca é alcançado por esse guard; `tk_import`
    (`teko_ns.mc`) continua chamando `lex_include` no PRÓPRIO handler de `import` -- fora do
    callback de `on_source`, então o push em si não viola a guarda, e o arquivo que ele empurra é
    anunciado (e varrido) pelo callback registrado, por conta própria, sem `tk_import` ter de repetir
    a varredura. A regra prática: quem faz uma varredura léxica pré-parse no `on_source` faz só isso
    ali; qualquer push que o construto (import, generic, base fora de ordem) precisar continua
    acontecendo no handler DAQUELE construto, nunca dentro do callback de `on_source`.
25. **Ensinar uma palavra que o `#rule` do prelúdio JÁ possui a tira do núcleo (S4.2).** `word_add`
    marca a ENTRADA de token (`TE_TAUGHT`), e `tok_add` é idempotente por lexema -- a entrada é a
    MESMA que o `#rule stmt: while (...)` do `<prelude>` usa. Como `lex_word_id` esconde toda
    entrada marcada em fonte não reivindicada, o `syntax_stmt("while", &tk_while)` da teko apaga o
    `while` dos fontes do núcleo (que o usam ~150 vezes): `mc/objmodel:212: expected ; after
    expression`. Vale para `while` e `for`, os dois únicos literais de despacho em forma de
    identificador do prelúdio (`+=`/`-=`/`++`/`--` são pontuação, e pontuação não é escopada). Não
    tem contorno do lado da teko que preserve a semântica: desmarcar a entrada
    (`tok_set_taught(id, 0)`) faz o handler DA TEKO parsear o `while` do núcleo, que é justamente o
    que a transparência do §64(h).5 proíbe. É item do lado do `mc` (§5, pedido registrado).
26. **Um teto de tabela dimensionado para fixture não sobrevive à auto-hospedagem.** As tabelas da
    teko são arrays globais de tamanho fixo (`TK_MAX*`) calibrados para um programa de dezenas de
    linhas; a unidade da rota A traz ~8 500 linhas de núcleo junto. Seis estouraram, um por vez, na
    ordem em que o self-compile os alcança (`TK_MAXFDECL`, `TK_MAXSLV`, `TK_MAXGARR`, `TK_MAXGDEF`,
    `TK_MAXARR`, `TK_MAXODECL`) -- cada um com mensagem própria e clara, nenhum com corrupção
    silenciosa. Ao subir, subir contra uma CONTAGEM medida na árvore (grep das declarações), não a
    olho: é o que separa `4096` de um número mágico.
27. **Uma tabela "o mais recente vence" da unidade inteira não responde por um PARÂMETRO -- e a
    resposta errada dela só aparece quando a checagem passa a valer para tipos COMUNS.**
    `tk_slv_find` (teko_struct.tk) guarda todo local da unidade e devolve a declaração mais
    recente do nome; parâmetro nenhum entra ali. Enquanto a checagem de apontado do `ref`/`out`
    só valia para classe/struct (higiene 2), o `pty` escalar saía antes do lookup e o furo ficava
    invisível; ao estender para escalar (higiene 3), `lvl_c(ref x)` dentro de `lvl_b(ref i64 x)`
    passou a "ver" o `Box x` que outra função declarara antes -- recusando uma fixture CORRETA.
    Regra: numa PASS, o tipo de um nome se pergunta à DECLARAÇÃO que se está percorrendo (a lista
    de parâmetros, depois o `N_VAR` do corpo), não a uma tabela global de parse; e o que ela não
    souber responde -1 (silêncio), nunca um palpite.
28. **Gerar uma função com um parâmetro por ITEM de uma lista variável põe o `MAXPARAMS` do ABI
    (12) na frente do teto que o módulo acha que manda.** O alocador de closure do K4 recebia uma
    captura por parâmetro, então `TK_MAXLAMCAP` (32) era decorativo: treze capturas morriam na
    mensagem do NÚCLEO (`io:N: at most 12 parameters`), com nome de arquivo que o programa nunca
    escreveu -- um teto invisível, com diagnóstico que aponta para o lugar errado. A saída não é
    subir nada: é o dado sair do ABI e ir para a MEMÓRIA (o objeto), escrito no sítio de criação
    por uma cadeia de chamadas que devolve o bloco (`tk_cap_put`, o mesmo desenho do `tk_va_put`
    de `params`, porque um store é instrução e a construção tem de ser uma expressão só). Vale
    para qualquer construto futuro com N partes: N nunca vira N parâmetros.

29. **Um parâmetro que carrega um ENDEREÇO tem de ser DECLARADO com largura de ponteiro; o tipo
    lógico mora numa tabela lateral.** O mc gera o store/load do slot de um parâmetro com
    `type_width(nd_type(param))` (`machine_arm64.mc:190`/`:336`,
    `machine_x86_64.mc:227`/`:322`), então declarar um `ref u8` como `u8` TRUNCA o ponteiro na
    entrada da função -- compila limpo e segfalta (o K2 viveu assim até o K2w). Largura 8 esconde
    o defeito por inteiro, e era o que toda fixture usava. Duas consequências de processo: (a) ao
    representar "endereço de T" num parâmetro, o SLOT é `TY_UPTR` e `T` vai para tabela lateral,
    com UM par de acessores (`tk_param_ty`/`tk_decl_param_ty`) por onde TODO leitor passa -- ler
    `nd_type` direto de um parâmetro volta a ser o bug; (b) uma fixture de largura 8 não prova
    nada sobre largura 1/2/4 -- cubra `u8`/`i32` explicitamente. Vale igual para um parâmetro
    GERADO: o thunk de delegate repetia o defeito e não era alcançado pelo `tk_ref_pass`, porque
    nasce depois dele.

30. **`<binário>: not found` com o binário ali, exit 127, é o LOADER falando.** O writer ELF do
    mc põe `PT_INTERP`/soname **musl** por padrão, e o compilador ensinado sai pelo backend de
    executável do HOST (`docs/build.md` § `[compiler]`) — num runner glibc ele existe, tem
    tamanho e não executa. A cura é nomear o loader da máquina em `[target]`
    (`interp` + `libc = "gnu"`), não caçar arquivo sumido. Consequência de processo: um config
    DERIVADO do `mc.toml` não herda o que as pernas do CI carregam na matriz — o que a
    perna resolve com `target_tail` a escada tem de resolver por conta (`write_target_tail` no
    `bootstrap.sh`, e detectando o loader, porque quem executa o que constrói é esta máquina).

31. **Um valor de PONTO FLUTUANTE não atravessa um parâmetro/slot declarado inteiro -- e o
    acidente esconde isso.** O float mora no outro banco de registradores, então: (a) escolher
    `ld64`/`st64` pela LARGURA move os oito bytes certos para o banco errado; (b) passar um `f64`
    a um parâmetro `i64` (o `tk_cap_put` da captura de closure) não passa nada -- o callee lê um
    registrador inteiro que ninguém escreveu; (c) casar uma "medida por largura" (`type_width(ret)
    < 8`) sobre um `f32` produz uma CONVERSÃO numérica onde se queria uma leitura. E o pior: com
    (a) e (b) errados AO MESMO TEMPO o programa acerta por coincidência, porque o valor lido vem do
    registrador de float que a função chamada deixou para trás — foi assim que a higiene 3 mediu
    "f64 atravessa bit a bit". **Probe que separa acerto de acidente: SOBRESCREVA a variável
    (`a = 0.0;`) depois de capturá-la/gravá-la e leia só então**; e teste o valor DENTRO de uma
    expressão maior (`1.0 + f(2.0)`), que muda a profundidade e portanto o registrador de destino.
    Regra: para float, decida por `type_kind(ty) == TK_FLOAT` antes da largura, e DECLARE `f32`/
    `f64` em todo parâmetro por onde o valor passa.

32. **O sufixo `.exe` do compilador ENSINADO é o do HOST, não o do `[target]`.** `drv_teach`
    (`minicompiler/mc` src/driver.mc) monta o nome do binário com `host_exe_suffix()`, porque o
    compilador ensinado tem de RODAR na máquina que o escreveu; já o `[project].out` das etapas
    seguintes é literal — o `mc` não anexa nada e o objeto sai de `out + ".o"`. Consequência: quem
    nomeia os estágios da escada tem de usar o sufixo do HOST nos dois lugares, e um
    `--os windows` numa máquina macOS produz `build/teko` (host) enquanto o script procura
    `build/teko.exe` (alvo) — `exit 127`, com o binário ali. **A cura não é adivinhar sufixo:
    é RECUSAR alvo ≠ máquina**, porque a escada executa todo estágio que constrói e um ponto fixo
    que não roda não é ponto fixo. `bootstrap.sh` confere `--os`/`--arch` contra `mc --host`.

33. **Um cast que DECLARA (não converte) só vale colado no nó que ele tipa — e o registro por
    posição vale sobre o nó de FORA.** O contrato do núcleo para o retorno de uma chamada indireta
    (`(f64) callp(...)`, mc 0.15.13) casa o cast com o filho **IMEDIATO**: qualquer nó inserido
    entre os dois desfaz o casamento **em silêncio** — compila, roda, devolve o valor errado de novo
    —, então o envelope acontece no sítio que CONSTRÓI a chamada (`tk_callp_ret`, V1) e nunca num
    passe posterior. E como o `callp` deixa de ser o nó devolvido, todo registro por POSIÇÃO
    (`tk_xt_put`/`tk_xt_add`, e o `node_assign`/`tk_node_replace` que copia o resultado para a
    árvore) tem de cair sobre o nó que o shaper DEVOLVE: registrar no `callp` interno é a mesma
    classe do bug K1b (contagem lida sob o nó órfão), invisível até um retorno CONTADO passar pelo
    shaper. Em `tk_emit_call` é por isso que o `tk_xt_add` vem depois do envelope, não antes.


## 5.2 Canal com a sessão do mc

`send_message` só funciona teko→mc. A sessão do mc escreve para nós em
**`/Users/schivei/projects/mini_compiler/build/NOTICES-teko.md`** (gitignored) — **ler ao
começar cada lote**; respostas dela e releases estão lá. Plano §23 tem o resumo do que já
respondeu. Regra do dono: **sem 1.0.0 do mc sem coordenação com o ngen**; e o M44 prevê o
ngen como pacote (`teko_init()` exportado, nunca `user_init`).

## 6. Comunicação — coordenador remoto + sessão local

**Sessão remota coordenadora**: guarda o histórico completo da virada (por que o
port existe, o que aposenta, o que já foi decidido) e drena para as duas branches
canônicas — <https://claude.ai/code/session_01VX6NuV7RoBLyW6tBCrwEde>

**Sessão local** (mini_compiler, `/Users/schivei/projects/mini_compiler`):
desenvolve o `mc` paralelo; repo **somente leitura** para o `ngen`. Regras:

1. A sessão local **avisa quando sai release nova** do mc → o `ngen` baixa a
   nova release, troca o symlink, reconfere o baseline (CI usa `latest`).
2. Quando bater **tensão que o ferramental do mc não resolva** (ex.: sintaxe
   de construto novo, capacidade de hook), a sessão local do `ngen` **pergunta
   a ela** por onde se resolve ou se precisa de suporte novo.
3. **Nenhuma edição direta do repo do mc por parte do `ngen`.** Tudo é hook ou
   solicitação de feature ao dono via sessão local.

Consulte o coordenador remoto quando: (a) aparecer um **fork de design** que o
`DECISION_LOG` e `port-teko-mc.md` não resolvam; (b) houver dúvida sobre se
algo **aposenta** ou se porta; (c) for preciso drenar/alinhar as branches.

## 7. Gate do fecho

O port **começou** (o gatilho M24/floats do mc disparou), mas **só fecha quando
o mc chegar a 1.0.0** — o cálculo automático de arena (M13) e o restante da fila
do mc vêm antes. Até lá: crescer o ensino da superfície, sempre por baixo,
sempre com o CI verde.

**Errata (dono 2026-09-06):** este gate NÃO bloqueia a v0.1.0 — v0.1.0 é um
corte INTERMEDIÁRIO (§1.1) na estrada até aqui, não uma antecipação do fecho.
