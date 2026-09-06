# Plano de ação — achatamento + de-O(n²) (item 2 do roadmap)

Data: 2026-08-06. Origem: spec do dono (rodada de achatamento/de-O(n²), item 2 do roadmap) +
reconciliação com a AL Wave existente. Meta: reduzir **memória e tempo** do self-host nativo (pico
de build < 2.5 GiB, alvo 1.5 GiB) sem mover um byte do emit (o fixpoint `gen2==gen3` é inegociável).

## 0. A TESE — a economia está na FORMA, não na lógica (ruling do dono)

> *"Muito da economia está na forma como escrevemos e não na lógica."*

Isso reorganiza o plano inteiro. O trabalho parte em **duas classes**, e a primeira é a maior:

- **FORMA — como escrevemos.** Estático-primeiro; valores repetidos compartilhados (`const`, não
  reconstruídos); busca direta (índice/hash), nunca varredura; string internada, não re-concatenada;
  nada redeclarado; zero magic values. É **reescrita mecânica de idioma, byte-preservante** (o emit
  não muda; o fixpoint continua idêntico). Baixo risco, alto volume. **A maior parte da economia.**
- **LÓGICA — o algoritmo.** Trocar varredura linear por hash, memoizar re-computação. Muda a
  **complexidade**, não o **resultado**. Concentrada no maior consumidor de tempo (o checker). Risco
  médio, exige prova por medição + fixpoint.

A disciplina de FORMA é uma extensão do W15 (§4): não é feature, é como o código passa a ser escrito
daqui pra frente.

## 1. Os 7 alvos do spec → duas frentes

| # | alvo do spec | classe | frente | estado |
|---|---|---|---|---|
| 1 | **string interns** | forma+lógica | Emit (manglados) + Checker (geral) | AL4a parcial |
| 2 | **scoped hash maps** (busca direta, não varredura) | lógica | **Checker** | **novo** |
| 3 | **economia em nested type checking** | forma+lógica | **Checker** | **novo** |
| 4 | **overload resolution** (memoizar) | lógica | **Checker** | **novo** |
| 5 | **máximo estático / min. iterações / valores estáticos repetidos compartilhados (memoization + constants)** | **forma** | Emit + transversal | AL0 parcial |
| 6 | **sai de redeclaração** | **forma** | transversal | novo (sweep) |
| 7 | **sai de magic values** | **forma** | transversal | W15 (contínuo) |

Leitura: a **AL Wave** cobre o lado do **emit** (bytes alocados/copiados). Os alvos 2/3/4 são um
**eixo novo no CHECKER** (onde o compilador itera/busca/re-checa), que nenhum doc atual cobre.

## 2. Frente EMIT — a AL Wave (já desenhada, aqui só re-priorizada por classe)

Docs: `al-wave-emit-throughput.md`, `al-wave-crumbs.md`, `al1-proof-report.md`, `al4a-interning-design.md`.

| crumb | o quê | classe | ganho medido |
|---|---|---|---|
| **AL0** | const-ificar produtores *build-and-return* de array fixo → tabela `const` em rodata | **forma** | tira arrays de corrida; sizes S mecânicos |
| **AL4a** | interning/memoização de nomes manglados (escrever o nome estrutural direto; igualdade de mangle **sem strings**; cachear a chave de dedup) | forma+lógica | **mata ~84% dos 99 MB** de nomes manglados |
| **AL4b** | str-builder "stream-não-concat" | **forma** | elimina cópias O(n²) de concat imutável |
| **AL1 / AL3** | prova (FECHADA) do copy-grow O(n²) + a cura global `push(&x, v)` (ref-push) | lógica | domina o pico ~1.8 GB medido |
| **AL5** | region-per-phase | lógica | achata o modelo de alocação por fase |

Pré-requisito de AL3/AL5: a fundação F1/F2/F3 (borrow `&x`, `let` profundo, array cap/len).
**AL0 e AL4a são banda EARLY-PARALELA** — não dependem da fundação, entram já.

## 3. Frente CHECKER — o eixo NOVO (de-O(n²) no front-end)

O checker é o maior consumidor de tempo do self-host e hoje **busca por varredura linear**. Provas:

- `lookup_binding` (`src/checker/scope.tks:204`) percorre `env.bindings` com `loop` — **linear por
  lookup**, O(n) por nome resolvido, O(n²) sobre o corpo. Dezenas de `loop` de busca no mesmo arquivo.
- `resolve_type` (`src/checker/resolve.tks:1853`) e a `TypeTable` são varridas por nome.
- A família da sonda por nome nu já cataloga tabelas globais varridas (cobertura `tk_cov_mark`/
  `tk_covb_add` = O(distintos) por acerto — a **mesma classe**, provada).

| crumb | o quê | classe | de → para |
|---|---|---|---|
| **CK1** | **scoped hash maps**: `Env`/`TypeTable`/tabela de símbolos passam a ter índice hash por nome; `lookup_binding`/`resolve_type` fazem **busca direta** | lógica (a FORMA é declarar a tabela já com o índice) | O(n) varredura → O(1) amortizado |
| **CK2** | **memoização de nested type checking**: cachear o resultado de checar um tipo aninhado já visto (chave = forma do tipo), não re-derivar a cada uso | forma+lógica | re-check repetido → 1× por forma |
| **CK3** | **memoização de overload resolution**: cachear a resolução `(nome, tipos-arg) → candidato` | lógica | re-resolver → tabela |
| **CK4** | **string interning geral**: nomes/identificadores atravessam o checker como **id internado** (u32), não `str` copiada; comparação por id, não por bytes | forma+lógica | cópia+compare de bytes → compare de id |

Cada CKx é **gate-able sozinho** e provado por: fixpoint `gen2==gen3` byte-idêntico + medição de
pico/tempo do checker antes/depois. CK4 é o que mais conversa com AL4a (as duas são interning; devem
compartilhar a tabela, não duplicá-la).

## 4. A disciplina de FORMA — o ruling transversal (W15-adjacente)

Aplicada ao **delta** de cada crumb E como convenção daqui pra frente. É a metade "forma" da tese §0.

1. **Estático-primeiro.** Todo produtor que constrói-e-retorna um valor constante (array/tabela fixa)
   é `const` em rodata, não uma função que aloca em corrida. (= AL0, generalizado.)
2. **Valor repetido = compartilhado.** Um literal de domínio que aparece ≥2× vira `const`/enum/flags
   (W15 no-magic-values, alvo 7) — **e nunca se redeclara** (alvo 6): um só ponto de verdade.
3. **Busca direta, nunca varredura.** Toda consulta por nome/chave é índice ou hash. Um `loop` que
   procura um elemento por igualdade é um code-smell a ser trocado por lookup. (= CK1.)
4. **String internada.** Nome que atravessa fases é id internado, comparado por id. (= CK4/AL4a.)
5. **Memoizar, não re-derivar.** Resultado de computação pura repetida (tipo aninhado, overload) é
   cacheado por chave. (= CK2/CK3.)
6. **Menos iterações.** Uma passada que já anda a estrutura carrega o somatório que a próxima
   passada faria (o molde da caminhada de cobertura reusada).

## 5. Ordem de execução + gates

**FORMA primeiro (mecânico, byte-preservante, baixo risco):**
- AL0 (const-ificação) · sweep de redeclaração/magic-values (§4.2) · AL4b (str-builder).
- Gate: fixpoint `gen2==gen3` byte-idêntico (a forma NÃO pode mover bytes do emit) + `teko test .`.

**LÓGICA depois (muda complexidade, exige medição):**
- Emit: AL4a → AL3 (sobre a fundação F1/F2/F3).
- Checker: CK1 → CK4 (interning, compartilhado com AL4a) → CK2 → CK3.
- Gate por crumb: fixpoint byte-idêntico + **pico RSS** e **tempo por fase** antes/depois (o número
  é o que justifica o crumb; sem ganho medido, não entra).

**Regra bootstrap-safe (carga aditiva):** um crumb que ensina forma/idioma novos ao compilador não é
adotado por `src/` na mesma carga — a semente anterior tem de continuar construindo gen1.

## 6. A medição — o oráculo (sem ele, "otimizei" é prosa)

Três números, medidos antes e depois de cada crumb de lógica:

1. **pico RSS do build nativo do gen2** — alvo < 2.5 GiB (hoje ~2.53), meta 1.5 GiB.
2. **tempo por fase** (`phase_begin`/`phase_end_ok` já existem) — checker vs monomorph vs emit.
3. **fixpoint `gen2==gen3` byte-idêntico** — inegociável; qualquer crumb que o quebre está errado.

Inversões (têm de FALHAR quando o mecanismo é removido): um crumb de lógica cujo `.tkt`/medição não
mostre o ganho declarado reprova — o número é o portão, não a intenção.

## 7. Riscos e fronteiras

| risco | guarda |
|---|---|
| a FORMA move bytes do emit (quebra fixpoint) | gate byte-idêntico em CADA crumb de forma; se mover, não é reescrita mecânica, é mudança de lógica disfarçada |
| CK1 (hash) muda a ORDEM de resolução e com ela um diagnóstico/símbolo | a ordem de iteração determinística é preservada onde é observável (a mesma lei da família da sonda por nome nu) |
| interning duplicado (AL4a vs CK4) | uma tabela só, compartilhada; CK4 e AL4a são o mesmo mecanismo |
| medir um ganho que estava noutro processo (ex.: `cc -O2`) | atribuir o tempo por fase ANTES de otimizar (o C0(d) da S8) |
| carga não-aditiva quebra o degrau | regra §5 bootstrap-safe |

## 8. Delegação proposta

Duas frentes, cada uma um workflow Opus/ultracode, worktree em cima da `fix/retirement`, teto 2.5 GiB:

- **Frente EMIT (early-parallel):** AL0 + AL4a — mecânica, ganho medido (84% de 99 MB), sem depender
  da fundação. Entra já.
- **Frente CHECKER:** CK1 (scoped hash maps em `lookup_binding`/`TypeTable`) — o maior consumidor de
  tempo do self-host, provável maior ganho, mas exige medição de pico/tempo do checker.

O sweep de FORMA (§4.2 redeclaração/magic-values) roda contínuo, como o W15.
