// Local-only preview for the size-limited Argon source snapshot. Not a deployment server.
import http from "node:http";
import { readFile, stat } from "node:fs/promises";
import { dirname, extname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const manifest = JSON.parse(await readFile(join(root, "ARGON_SITE_MANIFEST.json"), "utf8"));
const omitted = new Map(manifest.omitted_media.map((item) => [item.path.replaceAll("\\", "/"), item]));
const type = {
  ".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml",
  ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
  ".webp": "image/webp", ".avif": "image/avif", ".gif": "image/gif",
  ".ico": "image/x-icon", ".ttf": "font/ttf", ".otf": "font/otf",
  ".woff": "font/woff", ".woff2": "font/woff2"
};
function send(res, status, message) {
  res.writeHead(status, { "content-type": "text/plain; charset=utf-8" });
  res.end(message);
}
function contained(file) {
  const part = relative(root, file);
  return !isAbsolute(part) && part !== ".." && !part.startsWith(".." + sep);
}
const server = http.createServer(async (req, res) => {
  if (req.method !== "GET" && req.method !== "HEAD") return send(res, 405, "GET or HEAD only");
  let urlPath;
  try { urlPath = decodeURIComponent(new URL(req.url, "http://localhost").pathname); }
  catch { return send(res, 400, "Invalid URL"); }
  const key = urlPath.replace(/^\/+/, "");
  let file = resolve(root, key || "index.html");
  if (!contained(file)) return send(res, 403, "Outside website root");
  try {
    let info = await stat(file).catch(() => null);
    if (info?.isDirectory()) {
      file = join(file, "index.html");
      info = await stat(file).catch(() => null);
    }
    const contentType = type[extname(file).toLowerCase()] || "application/octet-stream";
    if (info?.isFile()) {
      const bytes = await readFile(file);
      res.writeHead(200, { "content-type": contentType, "content-length": bytes.length });
      return res.end(req.method === "HEAD" ? undefined : bytes);
    }
    const media = omitted.get(key);
    if (!media) return send(res, 404, "No such local source or manifest-listed media file: " + key);
    // The canonical website source is separately pinned. Public image files may change.
    // A raw GitHub source URL can return 404 for a private canonical repository, so
    // serve live public artwork only through this allowlisted host and exact path.
    const upstreamURL = new URL(media.production_url);
    if (upstreamURL.protocol !== "https:" || upstreamURL.hostname !== "getcolosseum.world" ||
        upstreamURL.pathname !== "/" + key || upstreamURL.search || upstreamURL.hash) {
      return send(res, 500, "Public asset URL rejected");
    }
    const upstream = await fetch(upstreamURL, { redirect: "follow" });
    if (!upstream.ok) return send(res, 502, "Public artwork returned HTTP " + upstream.status);
    const bytes = new Uint8Array(await upstream.arrayBuffer());
    res.writeHead(200, {
      "content-type": type[extname(key).toLowerCase()] || "application/octet-stream",
      "content-length": bytes.length,
      "x-source": "live-getcolosseum-world",
      "x-asset-size-matches-pinned-source": String(bytes.length === media.bytes)
    });
    res.end(req.method === "HEAD" ? undefined : bytes);
  } catch (err) {
    send(res, 502, "Preview could not serve " + key + ": " + err.message);
  }
});
const port = Number(process.env.PORT || 4173);
server.listen(port, "127.0.0.1", () => console.log("Colosseum Argon preview: http://127.0.0.1:" + port + "/"));
