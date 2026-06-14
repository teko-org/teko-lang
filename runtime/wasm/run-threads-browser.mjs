// Playwright harness for the Layer B (wasm-threads) browser test: serve with
// COOP/COEP, drive headless Chromium, and assert that both shared-memory modules
// hand a value across Web Workers via atomics (reference=777, emitter output=99),
// plus crossOriginIsolated.
//   usage: node run-threads-browser.mjs   (requires `npm i` + chromium)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8093);
const EXPECTED = { threadsRef: 777, threadsEmitted: 99 };
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/threads.html`, { waitUntil: "load", timeout: 30000 });
  // Stress loops run many real Web Worker hand-offs in-page; allow ample time.
  await page.waitForFunction(() => window.__tekoThreads !== undefined, { timeout: 120000 });
  const results = await page.evaluate(() => window.__tekoThreads);
  const isolated = await page.evaluate(() => window.__crossOriginIsolated);
  for (const [name, expected] of Object.entries(EXPECTED)) {
    const got = results?.[name];
    if (got === expected) console.log(`OK   browser ${name}: main() = ${got}`);
    else { console.error(`FAIL browser ${name}: ${got}, expected ${expected}`); exitCode = 1; }
  }
  console.log(`crossOriginIsolated = ${isolated}`);
  if (isolated !== true) { console.error("FAIL: not crossOriginIsolated (COOP/COEP)"); exitCode = 1; }
} catch (e) {
  console.error("threads browser harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
