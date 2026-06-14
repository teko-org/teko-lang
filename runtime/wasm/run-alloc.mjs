// Phase 11 MVP-4 allocator stress (Node): exercise the real freeing allocator
// exported by EVERY Layer A module (teko_alloc / teko_free / teko_reset) against
// emitted.wasm. Checks: in-range non-overlapping allocations, no cross-corruption,
// free+realloc reuse, coalescing (free-all then one big alloc), double-free safety,
// and reset. No DOM/imports needed — emitted.wasm instantiates with {}.
import { readFile } from "node:fs/promises";

const HEAP_BASE = 16384, HEAP_END = 65536;
const bytes = await readFile(new URL("./samples/emitted.wasm", import.meta.url));
const { instance } = await WebAssembly.instantiate(bytes, {});
const { teko_alloc, teko_free, teko_reset, memory } = instance.exports;

let failures = 0;
const check = (cond, msg) => { if (!cond) { console.error(`FAIL ${msg}`); failures++; } };
const inRange = (p, n) => p >= HEAP_BASE + 8 && p + n <= HEAP_END;
const fill = (p, n, v) => { new Uint8Array(memory.buffer, p, n).fill(v); };
const allSame = (p, n, v) => new Uint8Array(memory.buffer, p, n).every((b) => b === v);

// 1. distinct, in-range, non-overlapping allocations.
const a = teko_alloc(100), b = teko_alloc(200), c = teko_alloc(50);
check(inRange(a, 100) && inRange(b, 200) && inRange(c, 50), "allocations in range");
const overlap = (p1, n1, p2, n2) => p1 < p2 + n2 && p2 < p1 + n1;
check(!overlap(a, 100, b, 200) && !overlap(a, 100, c, 50) && !overlap(b, 200, c, 50),
  "allocations do not overlap");

// 2. no cross-corruption.
fill(a, 100, 0xAA); fill(b, 200, 0xBB); fill(c, 50, 0xCC);
check(allSame(a, 100, 0xAA) && allSame(b, 200, 0xBB) && allSame(c, 50, 0xCC),
  "no cross-region corruption");

// 3. free + realloc reuses freed space (does not run off the end forever).
teko_free(b);
const d = teko_alloc(150);
check(inRange(d, 150) && !overlap(d, 150, a, 100) && !overlap(d, 150, c, 50),
  "realloc after free is valid and disjoint from live blocks");
check(d <= b, "freed block is reused (coalescing/first-fit), not bump-appended");

// 4. free everything, then a single near-full alloc must succeed => coalesced.
teko_free(a); teko_free(c); teko_free(d);
const big = teko_alloc(40000);
check(inRange(big, 40000), "free-all then 40000-byte alloc succeeds (heap coalesced)");

// 5. double-free is safe (no trap, no corruption); heap still usable afterwards.
teko_free(big);
teko_free(big); // double free
const after = teko_alloc(40000);
check(inRange(after, 40000), "heap usable after a double free (no corruption/trap)");

// 6. invalid / wild / null frees are safe no-ops (no trap, heap still usable).
teko_free(0);                  // null
teko_free(after + 3);          // interior / misaligned pointer
teko_free(HEAP_BASE - 100);    // below the heap
teko_free(HEAP_END + 100);     // above the heap
teko_free(0x7fffffff);         // wild
teko_free(after);              // (real) free so the next reset starts clean
const stillOk = teko_alloc(40000);
check(inRange(stillOk, 40000), "invalid/null/wild frees are safe no-ops");
teko_free(stillOk);

// 7. reset bulk-reclaims the whole region.
teko_reset();
const full = teko_alloc(49000);
check(inRange(full, 49000), "teko_reset reclaims the whole heap");
teko_free(full);

// 8. OOM returns 0, not a trap or out-of-range pointer.
const oom = teko_alloc(1 << 20);
check(oom === 0, "over-capacity alloc returns 0 (OOM), no trap");

// 9. 10k-cycle alloc->free->alloc loop: memory is REUSED, the heap does not grow
//    unboundedly, no leak/overflow/double-free. Track the high-water pointer.
teko_reset();
let hiWater = 0;
let prev = 0;
for (let i = 0; i < 10000; i++) {
  const sz = 8 + ((i * 37) % 512);          // varied small sizes
  const p = teko_alloc(sz);
  if (!inRange(p, sz)) { check(false, `cycle ${i}: alloc out of range`); break; }
  fill(p, sz, i & 0xff);                      // touch it
  if (!allSame(p, sz, i & 0xff)) { check(false, `cycle ${i}: write corrupted`); break; }
  hiWater = Math.max(hiWater, p + sz);
  teko_free(p);                               // free immediately => next iter must reuse
  prev = p;
}
// With free+coalesce, a free-then-alloc of similar size must reuse low addresses,
// so the high-water mark stays near the heap base, NOT climbing to HEAP_END.
check(hiWater < HEAP_BASE + 2048,
  `10k alloc/free reuse memory (high-water ${hiWater - HEAP_BASE}B from base, not growing)`);
// And after the loop the whole heap is still allocatable in one block.
teko_reset();
const whole = teko_alloc(49000);
check(inRange(whole, 49000), "heap fully intact after 10k alloc/free cycles");

if (failures === 0) {
  console.log("OK   allocator: range/overlap/reuse/coalesce/double-free/reset/OOM all pass");
  process.exit(0);
} else {
  console.error(`FAIL allocator: ${failures} check(s) failed`);
  process.exit(1);
}
