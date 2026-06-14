// Headless-browser harness for Browser FFI MVP-3 (JS->Teko callbacks): serve
// runtime/wasm/ with COOP/COEP, drive Chromium via Playwright, load the events
// page, click #count, and assert the Teko callback fired and updated the DOM —
// #count textContent goes "0" -> "clicked!".
//   usage: node run-events.mjs   (requires `npm i` + `npx playwright install chromium`)
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8094);
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/events.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoEvents !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoEvents);
  const before = await page.evaluate(() => document.getElementById("count").textContent);
  await page.click("#count"); // JS event -> Teko callback via exports.teko_invoke
  // The callback runs synchronously inside the click dispatch; read it back.
  const after = await page.evaluate(() => document.getElementById("count").textContent);
  if (status?.ok && before === "0" && after === "clicked!") {
    console.log(`OK   browser events: #count "${before}" -> "${after}" (JS->Teko callback)`);
  } else {
    console.error(`FAIL browser events: status=${JSON.stringify(status)} before=${JSON.stringify(before)} after=${JSON.stringify(after)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("events harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
