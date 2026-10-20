const {
  chromium,
} = require("/opt/codex/runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");
const { launchFirefox } = require(
  process.cwd() + "/browser-bundle/firefox-runtime/launch.cjs",
);
const fs = require("fs"),
  path = require("path"),
  http = require("http");
const root = path.resolve(__dirname, ".."),
  out = path.join(root, "evidence/vinyl");
fs.mkdirSync(out, { recursive: true });
const server = http.createServer((req, res) => {
  try {
    const f = path.join(root, decodeURIComponent(req.url.split("?")[0]));
    res.setHeader(
      "Content-Type",
      f.endsWith(".js")
        ? "text/javascript"
        : f.endsWith(".css")
          ? "text/css"
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
  for (const name of ["chromium", "firefox"]) {
    const b =
      name === "chromium"
        ? await chromium.launch({
            executablePath:
              process.cwd() +
              "/browser-bundle/playwright-1.62.1-debian13-x64/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-linux64/chrome-headless-shell",
            headless: true,
            args: ["--no-sandbox"],
          })
        : await launchFirefox();
    const p = await b.newPage({ viewport: { width: 1280, height: 720 } });
    let errors = [];
    p.on("pageerror", (e) => errors.push(String(e)));
    for (const [id, params] of Object.entries({
      home: { page: "home" },
      new: { page: "home", profile: "new" },
      skipped: { page: "home", profile: "skipped" },
      retrying: { page: "song", source: "retrying" },
      album: { page: "album" },
      song: { page: "song" },
      discover: { page: "discover" },
      list: { page: "list" },
      taste: { page: "taste" },
      credits: { page: "credits" },
      songTall: { page: "song", fixture: "mounamela" },
      songVideo: { page: "song", fixture: "without-me", video: true },
      finding: { page: "song", source: "finding" },
      offline: { page: "discover", offline: true },
      edition: { page: "album", edition: 2 },
    })) {
      await p.goto(
        base +
          "/demos/vinyl.html?params=" +
          encodeURIComponent(JSON.stringify(params)),
      );
      await p.waitForFunction(
        () => document.documentElement.dataset.mockReady === "true",
      );
      await p.screenshot({ path: out + "/" + name + "-" + id + ".png" });
      if (id === "album") {
        await p.locator("[data-action=save]").click();
        if (
          !(await p
            .locator("[data-action=save]")
            .textContent()
            .then((x) => x === "Saved"))
        )
          throw Error("Save failed");
        await p.locator('[data-track="1"]').click();
        await p.locator("[data-action=credits]").click();
        await p.keyboard.press("Escape");
        if (await p.locator("#overlay").isVisible())
          throw Error("Escape failed");
        await p.locator("[data-section=Editions]").click();
        await p.screenshot({ path: out + "/" + name + "-editions.png" });
      }
      if (id === "edition") {
        await p.locator('[data-disc="1"]').click();
        await p.screenshot({ path: out + "/" + name + "-disc2.png" });
      }
      if (id === "song") {
        await p.locator("[data-action=notes]").click();
        await p.screenshot({ path: out + "/" + name + "-notes-off.png" });
        await p.locator("[data-action=queue]").click();
        await p.screenshot({ path: out + "/" + name + "-queue.png" });
        await p.keyboard.press("Escape");
      }
      if (id === "home") {
        await p.locator('[data-slide="1"]').click();
        await p.screenshot({ path: out + "/" + name + "-carousel.png" });
      }
      if (id === "taste") {
        await p.locator("#artist-search").fill("Michael");
        await p.locator("[data-artist]").first().click();
        if ((await p.locator("[aria-pressed=true]").count()) < 1)
          throw Error("Taste failed");
        await p.screenshot({ path: out + "/" + name + "-taste-selected.png" });
      }
      if (id === "discover") {
        await p.locator("#content").evaluate((el) => (el.scrollTop = 380));
        await p.locator("[data-filter=language]").selectOption("Telugu");
        await p.screenshot({ path: out + "/" + name + "-filtered.png" });
      }
      const broken = await p
        .locator("img")
        .evaluateAll((imgs) =>
          imgs.filter((i) => !i.complete || !i.naturalWidth).map((i) => i.src),
        );
      if (broken.length) errors.push({ id, broken });
    }
    fs.writeFileSync(
      out + "/" + name + "-results.json",
      JSON.stringify({ errors }, null, 2),
    );
    await b.close();
    if (errors.length) throw Error(JSON.stringify(errors));
  }
  server.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
