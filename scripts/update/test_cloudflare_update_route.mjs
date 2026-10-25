import test from "node:test";
import assert from "node:assert/strict";
import { serveStableFeed, stableUpdateOrigin, stableUpdatePath } from "./cloudflare_update_route.mjs";
import * as route from "./cloudflare_update_route.mjs";

const feed = JSON.stringify({
  tag_name: "v1.1.9",
  draft: false,
  prerelease: false,
  assets: [
    { name: "colosseum-update-v1.json" },
    { name: "colosseum-update-v1.json.sig" },
    { name: "Colosseum-1.1.9-setup.exe" },
  ],
}) + "\n";

function fetcher(body = feed, status = 200) {
  return async (url, options) => {
    assert.equal(url, stableUpdateOrigin);
    assert.equal(options.method, "GET");
    assert.equal(options.redirect, "manual");
    return new Response(body, { status, headers: { ETag: '"release-1.1.9"' } });
  };
}

const request = (method = "GET", path = stableUpdatePath, headers = {}) =>
  new Request("https://getcolosseum.world" + path, { method, headers });

test("unrelated site routes are untouched", async () => {
  assert.equal(await serveStableFeed(request("GET", "/")), null);
  assert.equal(await serveStableFeed(request("GET", "/demos/theatre.html")), null);
});

test("stable JSON is forwarded without modifying its bytes or making it executable", async () => {
  const result = await serveStableFeed(request(), fetcher());
  assert.equal(result.status, 200);
  assert.equal(await result.text(), feed);
  assert.match(result.headers.get("Content-Type"), /application\/json/);
  assert.equal(result.headers.get("ETag"), '"release-1.1.9"');
  assert.equal(result.headers.get("X-Content-Type-Options"), "nosniff");
  assert.equal(result.headers.get("Cache-Control"), "public, max-age=300, must-revalidate");
});

test("HEAD has headers but no payload; non-read methods fail", async () => {
  const head = await serveStableFeed(request("HEAD"), fetcher());
  assert.equal(head.status, 200);
  assert.equal(await head.text(), "");
  const post = await serveStableFeed(request("POST"), fetcher());
  assert.equal(post.status, 405);
});

test("ETag validators pass through and 304 is preserved", async () => {
  const conditional = request("GET", stableUpdatePath, { "If-None-Match": '"release-1.1.9"' });
  const response = await serveStableFeed(conditional, async (url, options) => {
    assert.equal(options.headers.get("If-None-Match"), '"release-1.1.9"');
    return new Response(null, { status: 304, headers: { ETag: '"release-1.1.9"' } });
  });
  assert.equal(response.status, 304);
  assert.equal(await response.text(), "");
});

test("broken, redirect, missing or invalid GitLab data fails closed", async () => {
  const cases = [
    async () => new Response("missing", { status: 404 }),
    async () => new Response(null, { status: 302, headers: { Location: "https://evil.example" } }),
    async () => new Response("<html>not json</html>", { status: 200 }),
    async () => new Response(JSON.stringify({ tag_name:"v1.1.9", draft:true, prerelease:false, assets:[1,2,3] }), {status:200}),
    async () => new Response(feed, { status:200, headers: { "Content-Length":String(2 * 1024 * 1024 + 1) } }),
    async () => { throw Error("network down"); },
  ];
  for (const attempt of cases) {
    const r = await serveStableFeed(request(), attempt);
    assert.equal(r.status, 503);
    assert.equal(r.headers.get("Cache-Control"), "no-store");
  }
});


test("deployable Worker forwards the stable feed including query URLs", async (t) => {
  assert.equal(typeof route.default?.fetch, "function", "feed helper needs a Worker entrypoint");
  t.mock.method(globalThis, "fetch", fetcher());
  const response = await route.default.fetch(request("GET", stableUpdatePath + "?channel=stable"));
  assert.equal(response.status, 200);
  assert.equal(await response.text(), feed);
  assert.match(response.headers.get("Content-Type"), /application\/json/);
});

test("Worker keeps a missing release inactive and HEAD bodyless", async (t) => {
  assert.equal(typeof route.default?.fetch, "function", "feed helper needs a Worker entrypoint");
  t.mock.method(globalThis, "fetch", async () => new Response("missing", {status:404}));
  const get = await route.default.fetch(request());
  assert.equal(get.status, 503);
  assert.equal(get.headers.get("Cache-Control"), "no-store");
  assert.equal(get.headers.get("Retry-After"), "300");
  const head = await route.default.fetch(request("HEAD"));
  assert.equal(head.status, 503);
  assert.equal(await head.text(), "");
});

test("Worker rejects wrong paths and write methods without upstream requests", async (t) => {
  assert.equal(typeof route.default?.fetch, "function", "feed helper needs a Worker entrypoint");
  let fetched = false;
  t.mock.method(globalThis, "fetch", async () => { fetched = true; throw Error("unexpected upstream request"); });
  const post = await route.default.fetch(request("POST"));
  assert.equal(post.status, 405);
  assert.equal(post.headers.get("Allow"), "GET, HEAD");
  const wrong = await route.default.fetch(request("GET", stableUpdatePath + ".html"));
  assert.equal(wrong.status, 404);
  const head = await route.default.fetch(request("HEAD", stableUpdatePath + "/wrong"));
  assert.equal(head.status, 404);
  assert.equal(await head.text(), "");
  assert.equal(fetched, false);
});


test("versioned notes are real HTML with Coming Soon and optional accounts, without fetching", async (t) => {
  t.mock.method(globalThis, "fetch", async () => { throw Error("notes must be local"); });
  const response = await route.default.fetch(request("GET", "/releases/v1.1.9"));
  assert.equal(response.status, 200);
  assert.match(response.headers.get("Content-Type"), /text\/html/);
  const html = await response.text();
  for (const expected of ["Colosseum 1.1.9", "Online catalogues and content API", "Coming Soon", "optional accounts", "Android", "BookWalker"])
    assert.ok(html.includes(expected), expected);
  assert.ok(!html.includes("<script"));
  assert.match(response.headers.get("Content-Security-Policy"), /default-src 'none'/);
  assert.equal(response.headers.get("X-Content-Type-Options"), "nosniff");
  assert.ok(response.headers.get("ETag"));
});

test("1.1.10 notes serve the authored features without claiming publication", async (t) => {
  t.mock.method(globalThis, "fetch", async () => { throw Error("notes must be local"); });
  const response = await route.default.fetch(request("GET", "/releases/v1.1.10"));
  assert.equal(response.status, 200);
  assert.match(response.headers.get("Content-Type"), /text\/html/);
  const html = await response.text();
  for (const expected of ["Colosseum 1.1.10", "Tankoyomi and Tankorent", "MangaSin",
    "Secret Service", "newer than 1.1.9", "Linux updates remain manual"])
    assert.ok(html.includes(expected), expected);
  assert.ok(!html.includes("<script"));
  assert.ok(!html.includes("Release published"));
  assert.equal(response.headers.get("X-Content-Type-Options"), "nosniff");
  assert.ok(response.headers.get("ETag"));
});

test("1.1.10 notes preserve exact GET, HEAD, conditional and method behavior", async () => {
  const path = "/releases/v1.1.10";
  const get = await route.default.fetch(request("GET", path));
  const head = await route.default.fetch(request("HEAD", path + "?from=app"));
  assert.equal(head.status, 200);
  assert.equal(await head.text(), "");
  assert.equal(head.headers.get("ETag"), get.headers.get("ETag"));
  const cached = await route.default.fetch(request("GET", path, {"If-None-Match":get.headers.get("ETag")}));
  assert.equal(cached.status, 304);
  assert.equal(await cached.text(), "");
  const post = await route.default.fetch(request("POST", path));
  assert.equal(post.status, 405);
  assert.equal(post.headers.get("Allow"), "GET, HEAD");
});

test("notes conditional requests, HEAD and writes preserve the scoped route", async () => {
  const get = await route.default.fetch(request("GET", "/releases/v1.1.9"));
  const head = await route.default.fetch(request("HEAD", "/releases/v1.1.9?from=app"));
  assert.equal(head.status, 200);
  assert.equal(await head.text(), "");
  assert.equal(head.headers.get("ETag"), get.headers.get("ETag"));
  const cached = await route.default.fetch(request("GET", "/releases/v1.1.9", {"If-None-Match":get.headers.get("ETag")}));
  assert.equal(cached.status, 304);
  assert.equal(await cached.text(), "");
  const post = await route.default.fetch(request("POST", "/releases/v1.1.9"));
  assert.equal(post.status, 405);
  assert.equal(post.headers.get("Allow"), "GET, HEAD");
  for (const wrong of ["/releases/v1.1.9.html", "/releases/v1.1.90", "/releases/v1.1.8",
    "/releases/v1.1.100", "/releases/v1.1.11"])
    assert.equal((await route.default.fetch(request("GET", wrong))).status, 404);
});


test("notes accept a weak validator from Cloudflare content compression", async () => {
  const get = await route.default.fetch(request("GET", "/releases/v1.1.9"));
  const r = await route.default.fetch(request("GET", "/releases/v1.1.9", {"If-None-Match":"W/"+get.headers.get("ETag")}));
  assert.equal(r.status,304);
});
