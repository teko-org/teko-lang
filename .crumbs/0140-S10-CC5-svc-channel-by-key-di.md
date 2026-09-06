---
seq: 0140
crumb-id: S10-CC5
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S10-CC3, S10-CC4, DI-FIX1]
sources:
  - "docs/design/plano-s10-channels-batch-detalhe.md:90-175"        # §C4 chan::make<K> + svc<Rx/Tx> DI-by-key
  - "docs/design/arena-especificacao-unica-0.3.1.md:359-545"        # §7.6-7.8 F2 program-root, svc<Tx/Rx> by key
  - "docs/design/di-recuperacao-estado-e-crumbs-0.3.1.md:1-90"      # measured state; type_svc_channel ABSENT
---

# 0140 · S10-CC5 — `svc<Rx<T>>`/`svc<Tx<T>>` DI-por-chave + `chan<T>::make<K>` (elimina os honest-stops)

> Ensinar o compilador a resolver `svc<Rx<T>>(key)`/`svc<Tx<T>>(key)` contra a registry de canais em F2
> e a fábrica `chan<T>::make<K>` que registra o transporte — REMOVENDO os honest-stops `svc<>` marcados
> em 0136/0137/0138 (D120: nada de honest-stops).

## Goal

Os crumbs de transporte 0136/0137/0138 deixaram honest-stops no ponto de resolução `svc<Rx<T>>`/
`svc<Tx<T>>` porque a resolução por chave dependia do DI (D58.1). O DI está LANDADO (0139 certifica),
mas `Rx`/`Tx`/`Ctx` NÃO são serviços (arena-espec §7.8 L771) — não passam por `svc_providers`/
`type_satisfies_iservice`; hoje `svc<Rx<T>>` seria REJEITADO por `svc_not_iservice_error` (`typer.tks`).
Este crumb ensina a EXTENSÃO DI-por-chave contra a registry de canais em F2 (a região imortal do
programa), fiel a `plano-s10-channels-batch-detalhe.md` §C4 + arena-espec §7.6-7.8 — o único crumb de
canais que toca o compilador. Feature-gated-inert: o corpus self-host NUNCA chama `chan::make`/`svc<Rx>`
(medido em §C4 L23), então a imagem-self do compilador é byte-idêntica; a maquinaria phantom/mono é
exercitada só por programas-usuário + fixtures. Toca o codegen (`type_svc_channel`) → dispara reseed.

## Where

- `src/checker/typer.tks:2005` — `type_svc` — ADICIONAR, como PRIMEIRA coisa após `svc_target_name` e
  ANTES do gate IService (`:2011`): `match type_svc_channel(c, tname, env, table) { TExpr as te => return
  te; error as e => return e; NotSvcOp => { } }` (cai pro path §7 pra todo T não-canal → §7 byte-idêntico).
- `src/checker/typer.tks` — NOVO `type_svc_channel(c, tname, env, table): TExpr | NotSvcOp | error`
  (`tname` ∈ {`Rx`,`Tx`} de `teko::threads` → resolve contra F2; senão `NotSvcOp`).
- `src/threads/threads.tks` — NOVO `chan<T>` (struct-namespace só pra hospedar `make<K>`); WIRE os corpos
  STUB de `Rx.pop`/`Tx.send`/`Tx.close`/`Ctx.add`/`Ctx.done`/`Ctx.wait`/`Ctx.close` aos handles F2
  (`tk_memchan_*`/`tk_oschan_*`/`tk_waitgroup_*`, já em `teko_rt.c`).
- `src/codegen/codegen.tks` — lowering de `chan<T>::make<K>` (const-key: materializa K, `K.init(key)`,
  `tk_region_register` em F2 do transporte + Rx/Tx/wg sintetizados sob ids irmãos, retorna `Ctx`);
  reusa a fábrica phantom-owner F3 (`retarget_generic_static_callee` `typer.tks:3159`, `phantom_owner_subst`
  `:3216`, roteamento `:2920`) — o análogo `Dictionary<K,V>::make`.
- `src/checker/di.tks:3` — `svc_type_id` — REUSAR inalterado pra derivar a chave-de-registry F2.

## How

1. **`chan<T>` stdlib** (`plano-s10-channels-batch-detalhe.md` §C4.1, copiar VERBATIM):

```teko
/**
 * chan — the channel FACTORY namespace-type. Never instantiated as a value; it exists only to host the
 * static `make<K>` factory (like `Dictionary<K,V>` hosts `make`). `make` creates the transport K under
 * the constant key, registers Rx/Tx/WaitGroup into F2, and returns the owning Ctx.
 *
 * @param T the element type carried by channels this factory makes
 * @since 0.3.1
 */
pub type chan<T> = struct {
    /**
     * make — create a `chan<T>` served by transport `K` under the constant `key` with `bounds` capacity.
     * Calls K.init(key), registers the transport + a synthesized Rx<T>/Tx<T> + a WaitGroup into the F2
     * program region keyed by svc_type_id(derived key symbol), and returns the owning Ctx.
     *
     * @param key    the channel's CONSTANT key (a literal/const; comptime)
     * @param bounds capacity: 1 = bounded-1 (default), N = bounded-N, 0 = unbounded
     * @return the owning Ctx, or an error (key conflict / transport open failure)
     * @throws when the key conflicts (variable key) or K.init fails
     * @since 0.3.1
     */
    pub static fn make<K: service singleton & IChannelKind>(key: str, bounds: usize = 1): Ctx | error {
        .{ }
    }
}
```

   O constraint é o átomo NU `service singleton & IChannelKind` (Gap 1, §C4). `service singleton` dispara
   o gate form-word (K não-singleton = erro de compilação, `monomorph.tks:76-82`); `IChannelKind` é o
   átomo de conformância nominal. NÃO usar a grafia `IChannelKind<T>` (irrepresentável no constraint —
   §C4 Gap 1; se o dono quiser a grafia literal pra paridade-de-doc é item separado, REPORTADO).

2. **`type_svc_channel`** (`plano-s10-channels-batch-detalhe.md` §C4.2, copiar VERBATIM):

```teko
/**
 * type_svc_channel — resolve svc<Rx<T>>(key) / svc<Tx<T>>(key) against the F2 CHANNEL registry rather
 * than the §7 service-provider registry (Rx/Tx are compiler-registered handles, not user services). For
 * a CONSTANT key the compiler knows the concrete transport K it was made with, monomorphizes the handle
 * + its pop/send ops to direct tk_*_recv/tk_*_send calls, and emits a load of the F2-registered handle
 * by svc_type_id(derived key). For a VARIABLE key it emits a runtime tk_region_lookup + a
 * function-pointer table indirection, and a miss panics (guard with has_svc first).
 *
 * @param c     the raw svc<…>(key) call
 * @param tname the target name ("Rx" or "Tx" in teko::threads)
 * @param env   the typing environment
 * @param table the folded type table
 * @return the typed handle-load expression, NotSvcOp when tname is not Rx/Tx, or an error
 * @since 0.3.1
 */
fn type_svc_channel(c: parser::Call, tname: str, env: Env, table: TypeTable): TExpr | NotSvcOp | error
```

   - **chave CONSTANTE:** emparelha cada `svc<Rx<T>>("k")` ao `chan<T>::make<K>("k", …)` da MESMA
     constante (K e bounds conhecidos → ops monomorfizam pra `tk_memchan_*`/`tk_oschan_*` diretos, sem
     lookup). Miss de par constante = erro de compilação (`svc_rx_unmade_key`).
   - **chave VARIÁVEL:** `tk_region_lookup(tk_region_program(), svc_type_id(k))` + tabela de
     ponteiros-de-função (NÃO vtable de interface — arena-espec §7.8 L783); miss = panic (guardar com
     `has_svc`). `svc_type_id` reusado inalterado.
   - O valor é svc-reclamado (`is_svc_claim`) — a lei de escape Parte A segue: `var rx = svc<Rx<T>>("k")`
     (bind local) PERMITIDO (arena-espec §7.8: `var tx = svc<Tx<i32>>("res")`), não armazenar/passar/
     retornar; re-clamar por chave no site de uso.

3. **Lowering de `make` (const-key)** (`§C4.3`): (a) materializa K (`service singleton`) + `K.init(key)`
   (dispatch pela superfície nua `IChannelKind`; `init(key:str)` não referencia T → sem subst owner-T);
   (b) `tk_region_register(tk_region_program(), svc_type_id(derived_key), transport)`; (c) registra
   `Rx<T>`/`Tx<T>` sintetizados (cada um `_id = svc_type_id(derived_key)`) sob ids irmãos
   `derived_key ++ ".rx"`/`".tx"`; (d) WaitGroup (`tk_waitgroup_make`) sob `".wg"`; (e) retorna
   `Ctx { _id = svc_type_id(derived_key) }`. Carrega o `chan<T>::make` abstrato pela tipagem abstrata e
   difere o stamp concreto pra mono (`phantom_owner_subst`) — mecanismo `Dictionary<StrKey,V>::make`, com
   owner type-arg `T` + method type-param `K` (a forma `fold<Acc>`, `typer.tks:3142`).

4. **WIRE os corpos threads.tks** — `Rx.pop`→`tk_memchan_recv`/`tk_oschan_recv` (0→`Closed{}`, 1→`v`);
   `Tx.send`→`tk_memchan_send`; `Tx.close`/`Ctx.close`→`tk_memchan_close`; `Ctx.add/done/wait`→
   `tk_waitgroup_*` (C5/0136). Elimina os stubs `pop → Closed{}`/`send → null`/`add → {}`.

5. **Remover os honest-stops** de 0136/0137/0138 no ponto de resolução `svc<Rx/Tx>` — agora resolvido de
   verdade por `type_svc_channel` (D120).

## Rulings & laws

- **Teko-only:** `.tks` só; `teko_rt.c/.h`/`assert`/`win32` frozen (D90) — o transporte-C `tk_memchan_*`/
  `tk_region_program`/`tk_waitgroup` JÁ existe (landado por 0137/C0b), ZERO edição aqui.
- **D120 (dono 2026-08-26):** NADA de honest-stops — este crumb É a implementação-adiantada que os
  remove; se algo faltar é implementação, não stop.
- **Fork protocol / mais-recente-vence:** o desenho é `plano-s10-channels-batch-detalhe.md` §C4 +
  arena-espec §7.6-7.8 (o transporte `K` = `service singleton & IChannelKind` extensível, F2 = exceção
  do canal, `Ctx` dono transient — rulings do dono ratificados). NÃO redesenhar; NÃO usar SM-G6.
- **Escape (Parte A, preservado):** o handle svc-reclamado nunca é armazenado/passado/retornado; bind
  local ok (arena-espec §7.8).
- **Comment convention (W15):** `/** */` só em `exp`/`pub` de superfície; sem `//`; flatten.
- **Testes:** só as fixtures nomeadas abaixo (paths que o fixpoint não exercita — corpus não usa canais).
- **Ratchet (D68/D118):** feature nova (não expurgo); mede o pico do build seco; crescimento esperado
  sub-percentual (corpus não chama `chan::make`/`svc<Rx>` → self-imagem byte-idêntica). Reportar o pico.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `[fixpoint]` `gen2==gen3` byte-idêntico;
  sweep `.tkt`/`.tkr` após a mudança de assinatura (novo `chan<T>` + `type_svc_channel`).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `chan_make_svc` | mini-flow chave CONSTANTE + `MemChan<i32>`: `svc<Tx<i32>>("nums")` envia 0..99, `spawn`, lê via `svc<Rx<i32>>("nums")` até `Closed`, `ctx.wait()`; soma correta | `0` |
| `chan_make_nonsingleton` | `chan<i32>::make<NotSingletonChan<i32>>(…)` transporte não-`singleton` → erro form-word (Gap 3) | `EXPECT_COMPILE_FAIL` |
| `svc_rx_unmade_key` | `svc<Rx<i32>>("never_made")` chave constante sem par `make` (miss comp-time) | `EXPECT_COMPILE_FAIL` |
| `svc_tx_variable_key` | `svc<Tx<T>>(var_key)` chave de RUNTIME resolvida por lookup F2 (path variável) | valor lido |
| `svc_channel_local_bind_ok` | `var tx = svc<Tx<i32>>("k")` (bind local do handle svc-reclamado) permitido | valor |

## Gate

`[fixpoint]` — dry-build + `gen2==gen3` byte-idêntico; `chan_make_svc`/`svc_tx_variable_key` correm
nativo. "Green" = `svc<Rx<T>>`/`svc<Tx<T>>` resolvem contra F2 (const e variável), `chan<T>::make<K>`
registra+retorna `Ctx`, os corpos threads.tks operam os transportes reais, os honest-stops de
0136/0137/0138 SUMIRAM, e o §7 DI ordinário segue byte-idêntico. Reseed-class: `fixpoint-rebuild` (o
codegen `type_svc_channel` move bytes → reseed único).

## Deps

`S10-CC3` (`0137`, memchan), `S10-CC4` (`0138`, oschan), `DI-FIX1` (`0139`, DI certificado). Habilita a
verificação da exceção-F2 de `0117` (Parte B).

## Done when

`svc<Rx<T>>(key)`/`svc<Tx<T>>(key)` resolvem de verdade contra a registry de canais em F2 (const inline +
variável por lookup), `chan<T>::make<K>` cria e registra o transporte, os corpos de `Rx`/`Tx`/`Ctx`
operam `tk_memchan_*`/`tk_oschan_*`/`tk_waitgroup_*`, nenhum honest-stop `svc<>` resta em 0136/0137/0138,
e o rebuild é byte-idêntico.
