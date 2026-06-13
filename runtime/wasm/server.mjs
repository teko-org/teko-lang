// Minimal static file server that sets the COOP/COEP headers required for
// SharedArrayBuffer (Layer B: --target=wasm-threads + Web Workers) in browsers.
// Serves the runtime/wasm/ directory. Used by the headless-browser harness.
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, normalize, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = fileURLToPath(new URL(".", import.meta.url));
const PORT = Number(process.env.PORT ?? 8092);
const TYPES = {
  ".html": "text/html", ".mjs": "text/javascript", ".js": "text/javascript",
  ".wasm": "application/wasm", ".wat": "text/plain", ".json": "application/json",
};

export function startServer(port = PORT) {
  const server = createServer(async (req, res) => {
    // COOP/COEP: mandatory for crossOriginIsolated => SharedArrayBuffer.
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    try {
      const rel = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname));
      const path = join(ROOT, rel === "/" ? "browser/index.html" : "." + rel);
      const body = await readFile(path);
      res.setHeader("Content-Type", TYPES[extname(path)] ?? "application/octet-stream");
      res.end(body);
    } catch {
      res.statusCode = 404;
      res.end("not found");
    }
  });
  return new Promise((resolve) => server.listen(port, () => resolve(server)));
}

// Run directly: `node server.mjs` to serve for manual local testing.
if (import.meta.url === `file://${process.argv[1]}`) {
  await startServer();
  console.log(`serving runtime/wasm/ on http://localhost:${PORT} (COOP/COEP on)`);
}
