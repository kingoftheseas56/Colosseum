const fs = require("fs"),
  http = require("http"),
  path = require("path"),
  { chromium, firefox } = require("playwright");
const root = path.resolve(__dirname, ".."),
  bundle = "/workspace/scratch/af0fb5da5f81/browser-bundle";
const srv = http.createServer((req, res) => {
  let p = path.join(root, decodeURIComponent(req.url.split("?")[0]));
  try {
    let data = fs.readFileSync(p);
    res.setHeader(
      "Content-Type",
      p.endsWith(".svg")
        ? "image/svg+xml"
        : p.endsWith(".png")
          ? "image/png"
          : p.endsWith(".jpg")
            ? "image/jpeg"
            : p.endsWith(".js")
              ? "text/javascript"
              : p.endsWith(".css")
                ? "text/css"
                : p.endsWith(".html")
                  ? "text/html"
                  : "application/octet-stream",
    );
    res.end(data);
  } catch {
    res.statusCode = 404;
    res.end("missing");
  }
});
(async () => {
  await new Promise((r) => srv.listen(0, "127.0.0.1", r));
  let base = "http://127.0.0.1:" + srv.address().port;
  let b = process.env.TEST_FIREFOX
    ? await require(bundle + "/firefox-runtime/launch.cjs").launchFirefox()
    : await chromium.launch({
        headless: true,
        executablePath:
          bundle +
          "/playwright-1.62.1-debian13-x64/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-linux64/chrome-headless-shell",
        args: ["--no-sandbox"],
      });
  let p = await b.newPage({ viewport: { width: 1280, height: 720 } }),
    errors = [];
  p.on("pageerror", (e) => errors.push(String(e)));
  await p.route("**/*", (r) =>
    r.request().url().startsWith(base) || r.request().url().startsWith("file:")
      ? r.continue()
      : r.abort(),
  );
  const go = async (file, params = {}) => {
    await p.goto(
      base +
        "/demos/" +
        file +
        "?params=" +
        encodeURIComponent(JSON.stringify(params)),
    );
    await p.waitForFunction(
      () => document.documentElement.dataset.mockReady === "true",
    );
    await p.waitForFunction(() =>
      [...document.images].every((i) => i.complete),
    );
    let bad = await p.evaluate(() =>
      [...document.images].filter((i) => !i.naturalWidth).map((i) => i.src),
    );
    if (bad.length) throw Error("Missing images " + bad.join(","));
  };
  await go("book-page.html");
  await p.click("[data-action=read]");
  await p.waitForSelector("[data-action=choose-0]");
  await p.screenshot({ path: root + "/evidence/biblio/edition-choice.png" });
  await p.keyboard.press("Escape");
  await p.click("[data-action=collection]");
  await p.screenshot({ path: root + "/evidence/biblio/detail.png" });
  await go("book-reader.html");
  await p.click("[data-action=next-page]");
  if ((await p.locator("#pageCount").textContent()) !== "2 of 8")
    throw Error("turn failed");
  await p.click("[data-action=bookmark]");
  await p.click("[data-action=contents]");
  await p.click("[data-tab=bookmarks]");
  await p.waitForSelector('[data-remove="2"]');
  await p.screenshot({ path: root + "/evidence/biblio/bookmarks.png" });
  await p.keyboard.press("Escape");
  await p.click("[data-action=appearance]");
  await p.click("[data-theme=Night]");
  await p.screenshot({ path: root + "/evidence/biblio/appearance.png" });
  await p.selectOption("[data-opt=flow]", "Scroll");
  await p.selectOption("[data-opt=flow]", "Pages");
  await p.locator("[data-action=reset-appearance]").click();
  await p.screenshot({
    path: root + "/evidence/biblio/reset-confirmation.png",
  });
  await p.click("[data-action=confirm-reset]");
  await p.keyboard.press("Escape");
  await p.click("[data-action=search]");
  await p.fill("#book-search", "Bingley");
  await p.screenshot({ path: root + "/evidence/biblio/search.png" });
  await p.click("[data-action=search-result]");
  await p.click("[data-action=footnote]");
  await p.screenshot({ path: root + "/evidence/biblio/footnote.png" });
  await p.keyboard.press("Escape");
  await p.screenshot({ path: root + "/evidence/biblio/reader.png" });
  await p.evaluate(() => {
    let el = document.querySelector(".prose p"),
      range = document.createRange();
    range.selectNodeContents(el);
    let sel = getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
    el.dispatchEvent(new MouseEvent("mouseup", { bubbles: true }));
  });
  await p.waitForSelector("[data-action=note]");
  await p.screenshot({ path: root + "/evidence/biblio/selection.png" });
  await p.click("[data-action=note]");
  await p.screenshot({ path: root + "/evidence/biblio/note-editor.png" });
  await p.fill("#highlight-note", "Sample reading note");
  await p.click("[data-action=save-highlight]");
  await p.click("[data-action=contents]");
  await p.click("[data-tab=highlights]");
  await p.screenshot({ path: root + "/evidence/biblio/highlights.png" });
  await p.keyboard.press("Escape");
  await go("audio-reader.html", { state: "sync-error" });
  await p.click("[data-audio=play]");
  await p.locator("[data-audio=retry]").scrollIntoViewIfNeeded();
  await p.screenshot({ path: root + "/evidence/biblio/audio-sync-error.png" });
  await p.click("[data-audio=retry]");
  await p.click("[data-audio=restart]");
  await p.click("[data-action=confirm-sync]");
  await p.screenshot({ path: root + "/evidence/biblio/audio.png" });
  await go("book-explore.html");
  await p.click("[data-action=customize]");
  await p.screenshot({ path: root + "/evidence/biblio/explore-customize.png" });
  await p.click('[data-hide="0"]');
  await p.click('[data-down="1"]');
  await p.click("[data-action=customize]");
  await p.screenshot({ path: root + "/evidence/biblio/explore.png" });
  await p.locator(".mosaics").scrollIntoViewIfNeeded();
  await p.screenshot({ path: root + "/evidence/biblio/explore-mosaics.png" });
  await go("book-reader.html", { state: "error" });
  await p.screenshot({ path: root + "/evidence/biblio/reader-error.png" });
  await go("book-page.html", { state: "empty" });
  await p.screenshot({ path: root + "/evidence/biblio/detail-empty.png" });
  await p.setViewportSize({ width: 1440, height: 900 });
  await go("book-reader.html", { panel: "appearance" });
  await p.screenshot({ path: root + "/evidence/biblio/reader-1440.png" });
  await p.goto("file://" + root + "/demos/book-reader.html");
  await p.waitForFunction(
    () => document.documentElement.dataset.mockReady === "true",
  );
  await p.click("[data-action=appearance]");
  await p.screenshot({ path: root + "/evidence/biblio/file-reader.png" });
  console.log(
    JSON.stringify(
      { browser: b.version(), errors, pass: errors.length === 0 },
      null,
      2,
    ),
  );
  fs.writeFileSync(
    root +
      "/evidence/biblio/" +
      (process.env.TEST_FIREFOX ? "firefox" : "chromium") +
      "-results.json",
    JSON.stringify(
      { browser: b.version(), errors, pass: errors.length === 0 },
      null,
      2,
    ),
  );
  await b.close();
  srv.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
