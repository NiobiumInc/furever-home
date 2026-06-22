#!/usr/bin/env node
// relay.js — static file server + a dumb encrypted-blob relay for the
// cross-device hand-off (Stage 3). Run with the homebrew node:
//   /opt/homebrew/bin/node web/relay.js [port]
// then open the printed LAN URL on your device.
//
// Serves the repo root (so pages reach web/ and dsl/rubric.dat) AND offers:
//   POST /relay/bundle           body = applicant bundle JSON   -> { code }
//   GET  /relay/bundle/:code     -> bundle JSON           (404 if unknown)
//   POST /relay/result/:code     body = result JSON       -> { ok:true }
//   GET  /relay/result/:code     -> result JSON           (404 until posted)
//
// The relay only ever holds ciphertext + PUBLIC/eval keys (the bundle) and the
// encrypted result. It never receives a secret key, so it cannot read anything.
// Entries are in-memory and expire after ~15 minutes.
const http = require("http");
const fs = require("fs");
const path = require("path");
const os = require("os");
const crypto = require("crypto");

const ROOT = path.resolve(__dirname, "..");           // pet-adoption repo root
const PORT = Number(process.argv[2] || process.env.PORT || 8800);
const TTL_MS = 15 * 60 * 1000;
const MAX_BODY = 32 * 1024 * 1024;                    // 32 MB cap (a bundle is ~18-24 MB)

const store = new Map();                              // code -> { bundle, result, t }
const MAX_ENTRIES = 24;                               // bound memory on a small instance (entries are tens of MB)
const RATE_WINDOW_MS = 60 * 1000, RATE_MAX = 90;      // per-IP /relay/ requests / min (survey polls ~30/min)
const hits = new Map();                               // ip -> [timestamps]
// crypto-random, unguessable hand-off code (Math.random was predictable/enumerable)
const newCode = () => crypto.randomBytes(6).toString("hex");
function sweep() {
  const now = Date.now();
  for (const [c, e] of store) if (now - e.t > TTL_MS) store.delete(c);
}
// public-exposure guards (the relay is now reachable from the open internet):
function rateLimited(req) {                            // simple per-IP write throttle
  const ip = String(req.headers["x-forwarded-for"] || req.socket.remoteAddress || "").split(",")[0].trim();
  const now = Date.now();
  const recent = (hits.get(ip) || []).filter(t => now - t < RATE_WINDOW_MS);
  recent.push(now); hits.set(ip, recent);
  if (hits.size > 5000) for (const [k, v] of hits) if (!v.some(t => now - t < RATE_WINDOW_MS)) hits.delete(k);
  return recent.length > RATE_MAX;
}
function evictIfFull() {                               // drop the oldest entry when at the cap
  if (store.size < MAX_ENTRIES) return;
  let oldest = null, ot = Infinity;
  for (const [c, e] of store) if (e.t < ot) { ot = e.t; oldest = c; }
  if (oldest) store.delete(oldest);
}

const TYPES = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css",
  ".wasm": "application/wasm", ".json": "application/json", ".dat": "text/plain",
  ".svg": "image/svg+xml", ".ico": "image/x-icon" };

const SEC = {                                         // baseline security headers on every response
  "X-Content-Type-Options": "nosniff",
  "X-Frame-Options": "SAMEORIGIN",
  "Referrer-Policy": "no-referrer",
  // 'unsafe-eval' is required by the Emscripten WASM glue; 'unsafe-inline' by the
  // pages' inline scripts/styles. So CSP is defense-in-depth here (the real XSS
  // fix is output escaping) — but object-src/base-uri/frame-ancestors/connect-src
  // still close off real attack classes.
  "Content-Security-Policy":
    "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; " +
    "style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; " +
    "frame-ancestors 'self'; base-uri 'self'; object-src 'none'",
};
function send(res, code, body, type = "application/json") {
  res.writeHead(code, { "Content-Type": type, ...SEC });
  res.end(body);
}
function readBody(req, cb) {
  let size = 0; const chunks = [];
  req.on("data", c => { size += c.length; if (size > MAX_BODY) { req.destroy(); } else chunks.push(c); });
  req.on("end", () => cb(Buffer.concat(chunks).toString("utf8")));
}

function serveStatic(req, res) {
  let rel = decodeURIComponent(req.url.split("?")[0]);
  if (rel === "/") rel = "/web/index.html";
  const fp = path.join(ROOT, path.normalize(rel));
  if (!fp.startsWith(ROOT)) return send(res, 403, "forbidden", "text/plain");
  fs.readFile(fp, (err, data) => {
    if (err) return send(res, 404, "not found", "text/plain");
    send(res, 200, data, TYPES[path.extname(fp)] || "application/octet-stream");
  });
}

const server = http.createServer((req, res) => {
  const url = req.url.split("?")[0];
  sweep();

  // throttle ALL relay traffic — writes and code lookups (stops code enumeration);
  // static file serving is not throttled.
  if (url.startsWith("/relay/") && rateLimited(req))
    return send(res, 429, '{"error":"rate limited"}');

  if (req.method === "POST" && url === "/relay/bundle") {
    return readBody(req, body => {
      try { JSON.parse(body); } catch { return send(res, 400, '{"error":"bad json"}'); }
      const code = newCode();
      evictIfFull();
      store.set(code, { bundle: body, result: null, t: Date.now() });
      send(res, 200, JSON.stringify({ code }));
    });
  }
  let m;
  if (req.method === "GET" && (m = url.match(/^\/relay\/bundle\/([a-z0-9]+)$/))) {
    const e = store.get(m[1]);
    if (!e || !e.bundle) return send(res, 404, '{"error":"unknown code"}');
    const bundle = e.bundle;
    e.bundle = null;                                  // one-time fetch: free the ~MBs once delivered
    return send(res, 200, bundle);
  }
  if (req.method === "POST" && (m = url.match(/^\/relay\/result\/([a-z0-9]+)$/))) {
    return readBody(req, body => {
      const e = store.get(m[1]);
      if (!e) return send(res, 404, '{"error":"unknown code"}');
      e.result = body; e.t = Date.now();
      send(res, 200, '{"ok":true}');
    });
  }
  if (req.method === "GET" && (m = url.match(/^\/relay\/result\/([a-z0-9]+)$/))) {
    const e = store.get(m[1]);
    if (!e) return send(res, 404, '{"error":"unknown code"}');
    if (!e.result) return send(res, 404, '{"pending":true}');
    const result = e.result;
    store.delete(m[1]);                               // hand-off complete — drop the session
    return send(res, 200, result);
  }
  // Redirect the bare root to the app entry so the browser base becomes /web/ —
  // the pages use relative links (survey.html, style.css, app.js), which only
  // resolve correctly when the URL path is under /web/ (as it is locally).
  if (req.method === "GET" && url === "/") {
    res.writeHead(302, { Location: "/web/index.html", ...SEC });
    return res.end();
  }
  if (req.method === "GET") return serveStatic(req, res);
  send(res, 405, '{"error":"method not allowed"}');
});

function lanIPs() {
  const out = [];
  for (const ifs of Object.values(os.networkInterfaces()))
    for (const i of ifs) if (i.family === "IPv4" && !i.internal) out.push(i.address);
  return out;
}

server.listen(PORT, "0.0.0.0", () => {
  console.log(`Furever relay + static server on :${PORT}`);
  console.log(`  local:   http://localhost:${PORT}/web/index.html`);
  for (const ip of lanIPs())
    console.log(`  network: http://${ip}:${PORT}/web/index.html   <- open this so the QR is scannable by other devices`);
});
