const fs = require("fs"),
  path = require("path"),
  http = require("http"),
  { chromium } = require("playwright");
const root = path.resolve(__dirname, ".."),
  bundle = "/workspace/scratch/af0fb5da5f81/browser-bundle",
  out = root + "/evidence/universes-vault";
fs.mkdirSync(out, { recursive: true });
const server = http.createServer((req, res) => {
  const f = path.join(root, decodeURIComponent(req.url.split("?")[0]));
  try {
    res.setHeader(
      "Content-Type",
      f.endsWith(".js")
        ? "text/javascript"
        : f.endsWith(".css")
          ? "text/css"
          : f.endsWith(".svg")
            ? "image/svg+xml"
            : f.endsWith(".html")
              ? "text/html"
              : "application/octet-stream",
    );
    res.end(fs.readFileSync(f));
  } catch {
    res.statusCode = 404;
    res.end("missing");
  }
});
(async () => {
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const base = "http://127.0.0.1:" + server.address().port;
  const browser = process.env.TEST_FIREFOX
    ? await require(bundle + "/firefox-runtime/launch.cjs").launchFirefox()
    : await chromium.launch({
        headless: true,
        executablePath:
          bundle +
          "/playwright-1.62.1-debian13-x64/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-linux64/chrome-headless-shell",
        args: ["--no-sandbox"],
      });
  const p = await browser.newPage({
      viewport: { width: 1280, height: 720 },
      reducedMotion: "reduce",
    }),
    errors = [],
    checks = [];
  p.on("pageerror", (e) => errors.push(String(e)));
  p.on("response", (r) => {
    if (r.status() >= 400) errors.push(r.status() + " " + r.url());
  });
  await p.route("**/*", (r) =>
    r.request().url().startsWith(base) ? r.continue() : r.abort(),
  );
  async function go(file, params = {}) {
    await p.goto(
      base +
        "/demos/" +
        file +
        "?params=" +
        encodeURIComponent(JSON.stringify(params)),
    );
    await p.waitForSelector("html[data-mock-ready=true]");
  }
  async function shot(name) {
    await p.screenshot({ path: out + "/" + name + ".png" });
    const broken = await p
      .locator("img")
      .evaluateAll((a) =>
        a.filter((x) => !x.complete || !x.naturalWidth).map((x) => x.src),
      );
    checks.push({ name, broken });
  }

  const scenarios = [
    [
      "universe-page.html",
      { name: "Dune", template: "SAGA", section: "continue" },
      "delta-universe-continue",
    ],
    [
      "universe-page.html",
      { name: "Dune", template: "SAGA", section: "matrix" },
      "delta-saga-matrix",
    ],
    [
      "universe-page.html",
      { name: "Dune", template: "SAGA", section: "creators" },
      "delta-saga-creators",
    ],
    [
      "universe-page.html",
      { name: "One Piece", template: "UNIVERSE", section: "stills" },
      "delta-shelves-stills",
    ],
    ...["Gotham", "Metropolis", "Future Gotham", "Watch Tower"].map((h) => [
      "tribute-page.html",
      { tribute: "dcau", hub: h },
      "delta-dcau-" + h.replaceAll(" ", "-"),
    ]),
    ["vault.html", {}, "delta-vault-browse"],
    ["vault.html", { recent: true, section: "recent" }, "delta-vault-recent"],
    ["vault.html", { detail: "dune" }, "delta-vault-extras"],
  ];
  for (const [f, pa, n] of scenarios) {
    await go(f, pa);
    await p.waitForTimeout(150);
    await shot(n);
  }
  await go("universe-page.html", {
    name: "Dune",
    template: "SAGA",
    section: "matrix",
  });
  await p.locator('[data-u-select="3"]').click();
  if (
    !(await p.locator("#saga-selected").textContent()).includes(
      "Dune: Part Two",
    )
  )
    throw Error("Selected-work panel did not update");
  await shot("delta-saga-film-selected");
  await p.evaluate(() => {
    window.capturedRoute = null;
    MockUI.go = (id, params) => (window.capturedRoute = { id, params });
  });
  await p.click("[data-u-open]");
  if ((await p.evaluate(() => capturedRoute.id)) !== "theatre.detail")
    throw Error("Saga route mismatch");
  await go("universe-page.html", { name: "One Piece", template: "UNIVERSE" });
  const before = await p.locator("[data-u-remove]").count();
  await p.locator("[data-u-remove]").first().click();
  if ((await p.locator("[data-u-remove]").count()) !== before - 1)
    throw Error("Continue removal failed");
  await go("tribute-page.html", { tribute: "dcau", hub: "Gotham" });
  if (!(await p.locator("[data-media=comic]").count()))
    throw Error("Missing DCAU comic rail");
  await p.evaluate(() => {
    window.capturedRoute = null;
    MockUI.go = (id, params) => (window.capturedRoute = { id, params });
  });
  await p.locator("[data-media=comic]").first().click();
  if ((await p.evaluate(() => capturedRoute.id)) !== "comics.series")
    throw Error("DCAU comic route mismatch");
  await p.locator(".dcau-interior section").last().scrollIntoViewIfNeeded();
  await shot("delta-dcau-theatre");
  await go("vault.html");
  await p.click("[data-action=just-arrived]");
  await p.locator(".vault-recent-rail").scrollIntoViewIfNeeded();
  await p.click('[data-v-slide="1"]');
  if (
    (await p.locator(".vault-recent-rail").evaluate((e) => e.scrollLeft)) <= 0
  )
    throw Error("Recent paging failed");
  await shot("delta-vault-carousel-next");
  await p.click("[data-v-folder=Films]");
  await p.waitForSelector("[data-action=folder][data-folder=Films]");
  await shot("delta-vault-folder-rails");
  await go("vault.html", { detail: "dune" });
  await p.locator("[data-action=extra]").first().scrollIntoViewIfNeeded();
  await shot("delta-vault-extra-actions");
  await p.evaluate(() => {
    window.capturedRoute = null;
    MockUI.go = (id, params) => (window.capturedRoute = { id, params });
  });
  await p.locator("[data-action=extra]").first().click();
  if ((await p.evaluate(() => capturedRoute.id)) !== "player.video")
    throw Error("Extra playback route mismatch");
  await p.keyboard.press("Escape");
  if (await p.locator(".overlay-layer").count())
    throw Error("Modal Escape failed");
  await p.setViewportSize({ width: 1600, height: 1000 });
  for (const [f, pa, n] of scenarios.filter((x, i) => [1, 4, 8].includes(i))) {
    await go(f, pa);
    await shot(n + "-large");
  }
  fs.writeFileSync(
    out +
      "/" +
      (process.env.TEST_FIREFOX ? "firefox" : "chromium") +
      "-delta-results.json",
    JSON.stringify(
      {
        browser: browser.version(),
        errors,
        checks,
        pass: errors.length === 0 && checks.every((x) => !x.broken.length),
      },
      null,
      2,
    ),
  );
  console.log(
    JSON.stringify({
      browser: browser.version(),
      screenshots: checks.length,
      errors,
      broken: checks.filter((x) => x.broken.length),
    }),
  );
  await browser.close();
  server.close();
  if (errors.length || checks.some((x) => x.broken.length))
    process.exitCode = 1;
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
