const fs = require("fs"),
  http = require("http"),
  path = require("path"),
  { chromium } = require("playwright");
const root = path.resolve(__dirname, ".."),
  bundle = "/workspace/scratch/af0fb5da5f81/browser-bundle";
const srv = http.createServer((req, res) => {
  let p = path.join(root, decodeURIComponent(req.url.split("?")[0]));
  try {
    res.setHeader(
      "Content-Type",
      p.endsWith(".js")
        ? "text/javascript"
        : p.endsWith(".css")
          ? "text/css"
          : p.endsWith(".html")
            ? "text/html"
            : p.endsWith(".svg")
              ? "image/svg+xml"
              : p.endsWith(".jpg")
                ? "image/jpeg"
                : "application/octet-stream",
    );
    res.end(fs.readFileSync(p));
  } catch {
    res.statusCode = 404;
    res.end();
  }
});
(async () => {
  await new Promise((r) => srv.listen(0, "127.0.0.1", r));
  const base = "http://127.0.0.1:" + srv.address().port,
    results = [];
  for (const name of ["chromium", "firefox"]) {
    let b =
      name === "firefox"
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
      r.request().url().startsWith(base) ? r.continue() : r.abort(),
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
    };
    const shot = async (id) =>
      p.screenshot({
        path: root + "/evidence/biblio/" + name + "-" + id + ".png",
      });
    const select = async (word = "fortune") => {
      await p.evaluate((word) => {
        let el = document.querySelector(".prose p"),
          walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT),
          node;
        while ((node = walker.nextNode())) {
          let n = node.textContent.indexOf(word);
          if (n >= 0) {
            let range = document.createRange();
            range.setStart(node, n);
            range.setEnd(node, n + word.length);
            let sel = getSelection();
            sel.removeAllRanges();
            sel.addRange(range);
            el.dispatchEvent(new MouseEvent("mouseup", { bubbles: true }));
            return;
          }
        }
        throw Error("word not found");
      }, word);
      await p.waitForSelector(".selection-card");
    };
    const geometry = async (selector, width, height) => {
      let d = await p
        .locator(selector)
        .evaluate((e) => ({
          rect: {
            x: e.offsetLeft,
            y: e.offsetTop,
            w: e.offsetWidth,
            h: e.offsetHeight,
          },
          anchor: JSON.parse(e.dataset.anchor),
        }));
      if (width && d.rect.w !== width)
        throw Error("Width mismatch " + JSON.stringify(d));
      if (height && d.rect.h !== height) throw Error("Height mismatch");
      let margin = selector.includes("selection") ? 10 : 12,
        x = Math.max(
          margin,
          Math.min(
            1280 - d.rect.w - margin,
            d.anchor.x + d.anchor.w / 2 - d.rect.w / 2,
          ),
        );
      let above = d.anchor.y - d.rect.h - 12,
        y = Math.max(
          margin,
          Math.min(
            720 - d.rect.h - margin,
            above >= margin ? above : d.anchor.y + d.anchor.h + 12,
          ),
        );
      if (Math.abs(x - d.rect.x) > 1 || Math.abs(y - d.rect.y) > 1)
        throw Error("Source position mismatch " + JSON.stringify(d));
      return d;
    };
    await go("book-reader.html");
    await select();
    await geometry(".selection-card", null, 44);
    await shot("selection-anchored");
    await p.click("[data-action=note]");
    await geometry(".note-editor", 300, 128);
    await p.fill("#highlight-note", "A local note fixture.");
    await shot("note-anchored");
    await p.click("[data-action=save-highlight]");
    await p.click("[data-action=contents]");
    await p.click("[data-tab=highlights]");
    await p.click('[data-highlight="0"]');
    await geometry(".selection-card", null, 44);
    await shot("existing-highlight");
    await p.click("[data-action=delete-highlight]");
    await p.keyboard.press("Escape");
    await select("fortune");
    await p.click("[data-action=define]");
    await geometry(".dictionary-card", 320);
    await shot("dictionary-ok");
    await p.keyboard.press("Escape");
    await select("wife");
    await p.click("[data-action=define]");
    await geometry(".dictionary-card", 320);
    await shot("dictionary-empty");
    await p.click("[data-action=open-wiktionary]");
    if (
      !(await p.evaluate(
        () => MockUI.get("biblio.externalNavigation", {}).simulated,
      ))
    )
      throw Error("External action not staged");
    await shot("dictionary-staged");
    await p.keyboard.press("Escape");
    await p.click("[data-action=footnote]");
    await geometry(".footnote-card", 360);
    await shot("footnote-anchored");
    await p.keyboard.press("Escape");
    await go("book-reader.html", { dictionaryState: "loading" });
    await select();
    await p.click("[data-action=define]");
    await shot("dictionary-loading");
    await go("book-reader.html", { panel: "appearance" });
    await p.fill("#custom-css", "p { color: teal; }");
    await p.click("[data-action=use-default]");
    await p.click("[data-action=confirm-default]");
    await p.fill("#custom-css", "p { color: red; }");
    await p.click("[data-action=reset-appearance]");
    await p.click("[data-action=confirm-reset]");
    if ((await p.inputValue("#custom-css")) !== "p { color: teal; }")
      throw Error("Default CSS was mutated");
    await p.locator("#custom-css").scrollIntoViewIfNeeded();
    await shot("css-default-restored");
    await go("book-explore.html", { section: "extensions" });
    await p.waitForSelector("[data-kind=extension]");
    await shot("extension-rail");
    await p.evaluate(() => {
      MockUI.go = (id, params) => (window.lastMockNavigation = { id, params });
    });
    await p.locator("[data-kind=extension] [data-see]").click();
    if (
      !(await p.evaluate(
        () =>
          lastMockNavigation.params.pin.sourceKind === "extension" &&
          lastMockNavigation.params.pin.catalogId ===
            "fixture://book-catalogue|book|recent",
      ))
    )
      throw Error("Extension pin contract mismatch");
    await p.click("[data-action=customize]");
    await p.locator("[data-kind=extension]").scrollIntoViewIfNeeded();
    await shot("extension-customize");
    await go("book-explore.html", {
      section: "extensions",
      extensionState: "loading",
    });
    if ((await p.locator(".book-skeleton").count()) !== 6)
      throw Error("Missing skeletons");
    await shot("extension-loading");
    await go("book-explore.html", { extensionState: "empty" });
    if (await p.locator("[data-kind=extension]").count())
      throw Error("Empty extension did not collapse");
    await shot("extension-empty");
    results.push({
      name,
      version: b.version(),
      errors,
      passed: errors.length === 0,
      sourceGeometry:
        "Selection 44px, note 300×128, dictionary 320px, footnote 360px, above/below/clamp verified",
    });
    await b.close();
  }
  fs.writeFileSync(
    root + "/evidence/biblio/popover-results.json",
    JSON.stringify(results, null, 2),
  );
  console.log(JSON.stringify(results));
  srv.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
