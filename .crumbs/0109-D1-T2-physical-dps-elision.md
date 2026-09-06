---
seq: 0109
crumb-id: D1-T2
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-A2, RM-C16]
sources:
  - "docs/design/arena-especificacao-unica-0.3.1.md:199-252"           # §5 DPS — the key
  - "docs/design/plano-mestre-0.3.1-implementacao.md:302"              # M5 D1-T2 row
  - "src/lir/lower.tks:7245"                                           # lower_return / lower_return_fat
  - "src/lir/lower.tks:1740"                                           # lower_call (alloc_call_dest)
---

# 0109 · D1-T2 — physical DPS elision (backend returns direct into caller arena)

> Realize the copy elision the DPS ABI (SM-A2) declared: the backend physically returns an aggregate
> DIRECTLY into the destination in the caller's arena — no local frame slot, no copy-out. The virtual
> `ret_dest` becomes a real machine destination pointer in the native encoders.

## Goal

SM-A2 (M1) instrumented the DPS ABI + `lower_return_into_dest`/`alloc_call_dest` and lowered the RETURN
virtually (`ret_dest = null` = today's byte-identical path; DPS enters only when a destination is passed).
This crumb makes the elision PHYSICAL on the native route: the backend passes the caller-arena destination
as a hidden pointer argument (AAPCS64/SysV64/Win64 sret convention) and the callee's `return` CONSTRUCTS
the aggregate in place at that destination + emits a bare `ret` — no local frame slot, no copy-out. Tail
merges feeding a return (an `if`/`match` in tail position) write each arm into the SAME `ret_dest`. This is
Doc-1 Idea 3, THE KEY — it attacks retention AND correctness (the return-boundary). The destination is
allocated by the caller in its CURRENT region (`alloc_call_dest` via `region_current`), never a fresh child
that could be dropped under the returned value (that would reopen the arena-por-escopo UAF). It removes
`own_returned_value` (`lower.tks:11715`) on the DPS path (the post-hoc box that copied the aggregate after
the fact); `frame_escape_guard` stays satisfied-by-construction + as the net for register/closure returns.
It is a **byte-mover** on the native route driving a **fixpoint-rebuild** (byte-identity: the DPS lowering
is deterministic).

## Where

- `src/lir/lower.tks:7245` `lower_return` / `:7278` `lower_return_fat` — construct into `ret_dest` + emit a
  bare `ret` on the DPS path (the virtual return SM-A2 declared, now physical).
- `src/lir/lower.tks:1740` `lower_call` — `alloc_call_dest` allocates the destination in the caller's
  CURRENT region (`region_current`) and passes it as the hidden sret pointer.
- `src/lir/lower.tks:11715` `own_returned_value` — REMOVE on the DPS path (the post-hoc box the DPS
  supersedes); keep the register/closure-return net.
- `src/backend/abi_aapcs64.tks` / `abi_sysv64.tks` / `abi_win64.tks` — the sret convention: an aggregate
  return is passed a hidden destination pointer in the ABI's designated register (x8 AAPCS64 / rdi-shift
  SysV64 / rcx-shift Win64) — the ONE ABI-descriptor addition D1-T2 needs.
- `src/backend/isel_*` / `encode_*` — materialize the hidden sret pointer arg + the construct-in-place
  store; the `ret` carries no aggregate value.

## How

1. **Pass the destination as a hidden sret pointer** (ABI descriptors): an aggregate-returning callee takes
   a hidden first pointer arg (the caller's destination). The descriptor names which register carries it
   (x8 AAPCS64, an rdi-shift SysV64, an rcx-shift Win64).

```teko
/**
 * dps_returns_aggregate — whether a function uses the DPS return path physically: its return type is an
 * aggregate (struct/fat/union of known layout), so isel passes a hidden sret destination pointer and the
 * callee constructs into it + emits a bare ret. A register-valued return (scalar/enum/ptr) is NOT DPS
 * (ret_dest = null, byte-identical to today). Composes with SM-A2's virtual lowering — this crumb makes it
 * physical on the native encoders.
 *
 * @param f      the callee function
 * @param table  the checker type table (return-type layout)
 * @return true when the return is an aggregate carried via a hidden sret destination pointer
 * @since 0.3.1
 */
fn dps_returns_aggregate(f: checker::TFunction, table: checker::TypeTable): bool
```

2. **Allocate the destination in the caller's CURRENT region** (`lower_call:1740` `alloc_call_dest` via
   `region_current`) — NEVER a fresh child (that would be droppable under the returned value → the
   arena-por-escopo UAF). It is exactly the region the caller would allocate the result in after a copy.
3. **Construct-in-place + bare ret** (`lower_return:7245`): the callee's `return agg` writes each field into
   `ret_dest` and emits `ret` with no aggregate copy-out.
4. **Tail merges into the same `ret_dest`**: a tail `if`/`match` writes every arm into the SAME destination
   (closing the tail-merge-into-return boundary in one).
5. **Remove `own_returned_value` on the DPS path** (`lower.tks:11715`); keep `frame_escape_guard` as the
   register/closure-return net (satisfied-by-construction for DPS — no frame slot to escape).
6. **Fixpoint**: DPS lowering is deterministic; the native object reproduces; `gen2==gen3`.

## Rulings & laws

- **Teko-only:** `src/lir/*.tks` + `src/backend/*.tks`; no C twin.
- **W15 full Javadoc** on `dps_returns_aggregate` + the sret-materializing helpers; flatten; no `//`.
- **Destination in the caller's CURRENT region (arena-espec §5.1):** never a fresh child (reopens the UAF);
  `ret_dest = null` = today's byte-identical register-return path.
- **DPS subsumes `-> ref T`** (§5.4) — already removed (SM-G4); `ref` survives only in PARAMETERS.
- **`own_returned_value` retired on DPS, `frame_escape_guard` kept as the net (§5.2).**
- **Optimization + correctness — no new surface:** the ABI was declared at SM-A2; this realizes it
  physically.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[fixpoint]` — native-object-reproducible `gen2==gen3`; sweep `.tkt` after the ABI descriptor + lowering
  change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler returns aggregates pervasively (every
`LModule`/`MFunc`/checker record it builds and returns); the DPS elision is exercised at scale by the
self-build, and correctness is proven by the native fixpoint reproducing + the CI legs running the joined
binary. A DPS miscompile surfaces as a fixpoint break or a wrong native exit, not a missed fixture.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` native-object byte-identity. "Green" = an
aggregate return constructs directly into the caller-arena destination (hidden sret pointer, no copy-out),
tail merges share `ret_dest`, `own_returned_value` is gone on the DPS path, the peak memory drops, and the
native object reproduces. Reseed-class: `fixpoint-rebuild`.

## Deps

`SM-A2, RM-C16` — verbatim from 000-INDEX (the DPS ABI declared at SM-A2; the native route it realizes on).

## Done when

The backend physically returns aggregates into the caller-arena destination via a hidden sret pointer (no
frame slot, no copy-out), tail merges write the same `ret_dest`, `own_returned_value` is retired on the DPS
path, and the native-object `gen2==gen3` fixpoint reproduces.
