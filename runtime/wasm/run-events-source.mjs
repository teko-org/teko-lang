// Headless-browser harness for Browser FFI FE-F (event handlers from source): serve
// runtime/wasm/ with COOP/COEP, drive Chromium, load the page whose module was compiled
// by the teko binary from events.tks, click #count, and assert the Teko `fn` handler
// fired via teko_invoke and updated the DOM — #count "0" -> "clicked from teko".
//   usage: node run-events-source.mjs
import { chromium } from "playwright";
import { startServer } from "./server.mjs";

const PORT = Number(process.env.PORT ?? 8098);
const TEXT = "clicked from teko";
const server = await startServer(PORT);
let exitCode = 0;
let browser;
try {
  browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/browser/events-source.html`, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(() => window.__tekoEventsSource !== undefined, { timeout: 40000 });
  const status = await page.evaluate(() => window.__tekoEventsSource);
  const before = await page.evaluate(() => document.getElementById("count").textContent);
  await page.click("#count"); // JS event -> Teko fn handler via teko_invoke
  const after = await page.evaluate(() => document.getElementById("count").textContent);
  if (status?.ok && before === "0" && after === TEXT) {
    console.log(`OK   browser events source: #count "${before}" -> "${after}" (Teko fn handler)`);
  } else {
    console.error(`FAIL browser events source: status=${JSON.stringify(status)} before=${JSON.stringify(before)} after=${JSON.stringify(after)}`);
    exitCode = 1;
  }
} catch (e) {
  console.error("events-source harness error:", e);
  exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  server.close();
}
process.exit(exitCode);
