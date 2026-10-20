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
  const routes = JSON.parse(
    fs.readFileSync(root + "/manifests/universes-vault.json"),
  ).routes;
  for (const r of routes) {
    await go(r.file.replace("demos/", ""), r.params);
    await shot(r.id);
    for (const s of r.states) {
      await go(r.file.replace("demos/", ""), { ...r.params, ...s.params });
      await shot(r.id + "-" + s.id);
    }
  }
  await go("vault.html", { state: "empty" });
  await p.click("[data-action=add]");
  await shot("vault-folder-picker");
  await p.click("[data-action=select]");
  await p.click("[data-action=finish]");
  await p.waitForSelector(".vault-grid");
  await p.click("[data-action=menu][data-id=unknown]");
  await shot("vault-context-menu");
  await p.click("[data-action=identify]");
  await p.locator("[data-action=use]").first().click();
  await shot("vault-confirm-identity");
  await p.keyboard.press("Escape");
  if (await p.locator(".overlay-layer").count()) throw Error("Escape failed");
  await go("vault.html");
  await p.fill("#search", "nothingmatches");
  await p.waitForSelector(".empty");
  await p.click("[data-action=reset]");
  await p.click("[data-action=detail][data-id=dune]");
  await p.click("[data-action=ceremony]");
  await p.click("[data-action=copy]");
  await shot("vault-copy-ceremony");
  await p.keyboard.press("Escape");
  await go("downloads.html", { ledger: "theatre", group: "theatre" });
  await p.click("[data-action=cancel-job][data-id=theatre]");
  await shot("downloads-cancel-confirm");
  await p.keyboard.press("Escape");
  await p.click("[data-action=menu][data-id=film]");
  await shot("downloads-menu");
  await p.click("[data-action=delete]");
  await shot("downloads-delete-confirm");
  await p.click("[data-action=confirm]");
  await p.waitForSelector("[data-action=remote][data-id=film]");
  await p.click("[data-action=remote][data-id=film]");
  await p.setViewportSize({ width: 1600, height: 1000 });
  for (const r of routes) {
    await go(r.file.replace("demos/", ""), r.params);
    await shot(r.id + "-large");
  }
  await go("vault.html");
  await shot("vault-1600");
  await go("tribute-page.html", { tribute: "onepiece" });
  await shot("tribute-onepiece-1600");
  fs.writeFileSync(
    out +
      "/" +
      (process.env.TEST_FIREFOX ? "firefox" : "chromium") +
      "-results.json",
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
