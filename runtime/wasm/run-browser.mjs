// Headless-browser harness: serve runtime/wasm/ with COOP/COEP, drive Chromium
// via Playwright, load the WASM in-page, and assert the channel round-trip plus
// crossOriginIsolated (the prerequisite for Layer B SharedArrayBuffer/threads).
//   usage: node run-browser.mjs   (requires `npm i` + `npx playwright install chromium`)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8092);
const server = await startServer(PORT);
let exitCode = 0;
try {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/index.html`, { waitUntil: "load" });
  await page.waitForFunction(() => window.__tekoResult !== undefined, { timeout: 10000 });
  const result = await page.evaluate(() => window.__tekoResult);
  const isolated = await page.evaluate(() => window.__crossOriginIsolated);
  console.log(`browser channel round-trip = ${result} | crossOriginIsolated = ${isolated}`);
  if (result !== 42) { console.error(`FAIL: expected 42, got ${result}`); exitCode = 1; }
  if (isolated !== true) { console.error("WARN: page is not crossOriginIsolated (COOP/COEP)"); }
  await browser.close();
} catch (e) {
  console.error("browser harness error:", e);
  exitCode = 1;
} finally {
  server.close();
}
process.exit(exitCode);
