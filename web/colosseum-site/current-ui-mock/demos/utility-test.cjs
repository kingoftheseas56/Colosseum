const fs = require("fs"),
  path = require("path"),
  http = require("http"),
  assert = require("assert"),
  { chromium } = require("playwright");
const root = path.resolve(__dirname, ".."),
  bundle = "/workspace/scratch/af0fb5da5f81/browser-bundle",
  browserName = process.env.TEST_FIREFOX ? "firefox" : "chromium",
  out = root + "/evidence/utilities/" + browserName;
fs.mkdirSync(out, { recursive: true });
const server = http.createServer((req, res) => {
  let f = path.join(root, decodeURIComponent(req.url.split("?")[0]));
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
  let p = await browser.newPage({
      viewport: { width: 1280, height: 720 },
      reducedMotion: "reduce",
    }),
    errors = [],
    checks = [],
    actions = [];
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
    await p.waitForTimeout(30);
  }
  async function shot(name) {
    await p.screenshot({ path: out + "/" + name + ".png" });
    let broken = await p
      .locator("img")
      .evaluateAll((a) =>
        a.filter((x) => !x.complete || !x.naturalWidth).map((x) => x.src),
      );
    checks.push({ name, broken });
  }
  async function click(a) {
    await p
      .locator('[data-action="' + a + '"]')
      .first()
      .click();
  }
  const routes = JSON.parse(
    fs.readFileSync(root + "/manifests/utilities.json"),
  ).routes;
  for (let r of routes) {
    await go(r.file.replace("demos/", ""), r.params);
    await shot(r.id);
    for (let s of r.states) {
      await go(r.file.replace("demos/", ""), { ...r.params, ...s.params });
      await shot(r.id + "-" + s.id);
    }
  }
  await go("account.html", { section: "security" });
  await click("password");
  await p.fill("#current", "DEMO secret");
  await p.click("[data-reveal=current]");
  await p.fill("#new", "DEMOpassword");
  await p.fill("#confirm", "DEMOpassword");
  await shot("account-password");
  await click("save-password");
  assert.equal(await p.locator("#overlay").count(), 0);
  assert.match(await p.locator("#status").innerText(), /cleared/);
  actions.push(
    "Password modal validation/save clears fields, including revealed secrets",
  );
  await click("signout-all");
  await shot("account-signout-all");
  await p.keyboard.press("Escape");
  assert.equal(await p.locator("#overlay").count(), 0);
  assert.equal(
    await p
      .locator("[data-action=signout-all]")
      .evaluate((x) => x === document.activeElement),
    true,
  );
  actions.push("Escape cancels and returns focus");
  await go("account.html", { section: "devices" });
  await click("revoke-1");
  await shot("account-device-revoke");
  await click("confirm-revoke-1");
  assert.equal(await p.locator(".device-row").count(), 1);
  await go("account.html", { section: "privacy" });
  for (let a of [
    "clear-search",
    "clear-activity",
    "export",
    "delete-account",
  ]) {
    await click(a);
    await shot("account-" + a);
    await p.keyboard.press("Escape");
  }
  await go("account.html", { section: "recovery" });
  await click("replace-key");
  await shot("account-replace-key");
  await p.fill("#current", "DEMO");
  await click("generate-key");
  await p.waitForSelector(".key");
  await click("saved-key");
  actions.push("Recovery replacement key acknowledgement");
  await go("account.html", { mode: "create" });
  await p.fill("#auth-user", "demo");
  await p.fill("#auth-pass", "samplepass");
  await p.fill("#auth-confirm", "samplepass");
  await click("auth-submit");
  await p.waitForSelector(".key");
  actions.push("Create account to one-time inert key");
  await go("utility.html", { page: "connections" });
  await click("dossier-trakt");
  await click("import");
  await click("choice-0-0");
  await click("choice-1-0");
  await click("match");
  await click("match-pick");
  await click("confirm-import");
  assert.match(await p.locator("#status").innerText(), /Native History/);
  actions.push("Import individual choices + match + confirm");
  await click("dossier-trakt");
  await click("import");
  await p.locator('[data-bulk="0"]').check();
  await p.locator('[data-bulk="1"]').check();
  await click("bulk");
  await shot("connections-bulk");
  await click("bulk-keep");
  assert.match(
    await p.locator("#overlay").innerText(),
    /Keep Colosseum progress/,
  );
  await p.keyboard.press("Escape");
  await p.keyboard.press("Escape");
  await click("dossier-trakt");
  await click("export");
  await click("confirm-export");
  assert.match(await p.locator("#status").innerText(), /marked sent/);
  actions.push("Export selection confirmation");
  await go("utility.html", { page: "stremio" });
  await click("stremio-disconnect");
  await shot("stremio-disconnect-confirm");
  await click("confirm-stremio-disconnect");
  await click("stremio-primary");
  await click("stremio-primary");
  assert.match(await p.locator("h1").innerText(), /Stremio Sync/);
  actions.push("Stremio disconnect/reconnect staged lifecycle");
  await go("utility.html", { page: "wallpaper" });
  await click("pick-1");
  await click("apply-Home");
  assert.match(await p.locator("#status").innerText(), /Home/);
  await p.fill("#wall-search", "zzzzz");
  await click("wall-search");
  assert.equal(await p.locator(".wallpaper-card:visible").count(), 0);
  actions.push("Wallpaper apply/search");
  await go("utility.html", { page: "settings" });
  await click("rows-Movies");
  await click("row-hide-0");
  await click("row-up-1");
  await p.locator('[data-row-name="0"]').fill("Demo row");
  await click("row-save");
  await click("rows-Movies");
  assert.equal(await p.locator('[data-row-name="0"]').inputValue(), "Demo row");
  actions.push("Catalogue row hide/reorder/rename");
  await p.setViewportSize({ width: 1600, height: 1000 });
  for (let r of routes) {
    await go(r.file.replace("demos/", ""), r.params);
    await shot(r.id + "-large");
  }
  fs.writeFileSync(
    out + "/results.json",
    JSON.stringify(
      {
        browser: browser.version(),
        errors,
        checks,
        actions,
        pass: !errors.length && checks.every((x) => !x.broken.length),
      },
      null,
      2,
    ),
  );
  console.log(
    JSON.stringify({
      browser: browser.version(),
      screenshots: checks.length,
      actions,
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
