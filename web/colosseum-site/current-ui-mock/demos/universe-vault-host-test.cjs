const fs = require("fs"),
  { chromium } = require("playwright");
const root = require("path").resolve(__dirname, ".."),
  bundle =
    "/workspace/scratch/af0fb5da5f81/browser-bundle/playwright-1.62.1-debian13-x64";
(async () => {
  const b = await chromium.launch({
    headless: true,
    executablePath:
      bundle +
      "/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-linux64/chrome-headless-shell",
    args: ["--no-sandbox"],
  });
  const p = await b.newPage({ viewport: { width: 1280, height: 720 } }),
    errs = [];
  p.on("pageerror", (e) => errs.push(String(e)));
  await p.route(/^https?:/, (r) => r.abort());
  await p.goto("file://" + root + "/index.html");
  await p.waitForTimeout(200);
  const ids = [
    "universes",
    "universe.saga",
    "tribute.dcau",
    "tribute.starwars",
    "tribute.onepiece",
    "tribute.cosmere",
    "vault",
    "downloads",
  ];
  for (const id of ids) {
    await p.evaluate((id) => MockHost.go(id), id);
    await p.waitForTimeout(500);
    await p.frames()[1].waitForSelector("html[data-mock-ready=true]");
    await p.screenshot({
      path: root + "/evidence/universes-vault/host-" + id + ".png",
    });
  }
  fs.writeFileSync(
    root + "/evidence/universes-vault/host-results.json",
    JSON.stringify({ routes: ids, errors: errs, pass: !errs.length }, null, 2),
  );
  console.log(errs);
  await b.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
