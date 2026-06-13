// Headless-browser harness: serve runtime/wasm/ with COOP/COEP, drive Chromium
// via Playwright, load each WASM fixture in-page, and assert behavioral parity
// with the standalone engines, plus crossOriginIsolated (prerequisite for
// Layer B SharedArrayBuffer/threads).
//   usage: node run-browser.mjs   (requires `npm i` + `npx playwright install chromium`)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8092);
const EXPECTED = { channels: 42, scheduler: 15, emitted: 7 };
const server = await startServer(PORT);
let exitCode = 0;
try {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/index.html`, { waitUntil: "load" });
  await page.waitForFunction(() => window.__tekoResults !== undefined, { timeout: 15000 });
  const results = await page.evaluate(() => window.__tekoResults);
  const isolated = await page.evaluate(() => window.__crossOriginIsolated);
  for (const [name, expected] of Object.entries(EXPECTED)) {
    const got = results?.[name];
    if (got === expected) console.log(`OK   browser ${name}: test() = ${got}`);
    else { console.error(`FAIL browser ${name}: ${got}, expected ${expected}`); exitCode = 1; }
  }
  console.log(`crossOriginIsolated = ${isolated}`);
  if (isolated !== true) console.error("WARN: page is not crossOriginIsolated (COOP/COEP)");
  await browser.close();
} catch (e) {
  console.error("browser harness error:", e);
  exitCode = 1;
} finally {
  server.close();
}
process.exit(exitCode);
