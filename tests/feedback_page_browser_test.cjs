const { chromium } = require("playwright");
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");

(async () => {
  const root = path.resolve(__dirname, "../web/feedback/dist");
  const screenshots =
    process.env.FEEDBACK_SCREENSHOTS ||
    path.join(os.tmpdir(), "colosseum-feedback-preview");
  await fs.mkdir(screenshots, { recursive: true });
  let config = JSON.parse(
    await fs.readFile(path.join(root, "config.json"), "utf8"),
  );
  const types = {
    ".html": "text/html",
    ".mjs": "text/javascript",
    ".css": "text/css",
    ".json": "application/json",
    ".svg": "image/svg+xml",
    ".ttf": "font/ttf",
  };
  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, "http://localhost");
      if (url.pathname.endsWith("/config.json")) {
        res.setHeader("Content-Type", "application/json");
        res.end(JSON.stringify(config));
        return;
      }
      const name = url.pathname.replace(/^\/feedback\//, "") || "index.html";
      const file = path.resolve(root, name);
      if (!file.startsWith(root + path.sep)) {
        res.writeHead(403).end();
        return;
      }
      res.setHeader(
        "Content-Type",
        types[path.extname(file)] || "application/octet-stream",
      );
      res.end(await fs.readFile(file));
    } catch {
      res.writeHead(404).end();
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const browser = await chromium.launch({
    executablePath:
      process.env.CHROME_PATH ||
      "C:/Program Files/Google/Chrome/Application/chrome.exe",
    headless: true,
  });
  try {
    const context = await browser.newContext({
      viewport: { width: 1440, height: 1100 },
    });
    const errors = [];
    context.on("page", (page) =>
      page.on("pageerror", (e) => errors.push(e.message)),
    );
    const requests = [];
    let status = 200;
    let accepted = true;
    await context.route(
      /https:\/\/(submit-form\.com|api\.web3forms\.com)\//,
      async (route) => {
        requests.push({
          url: route.request().url(),
          body: route.request().postDataJSON(),
        });
        await route.fulfill({
          status,
          contentType: "application/json",
          body: JSON.stringify({ success: accepted }),
        });
      },
    );
    const page = await context.newPage();
    const url = `http://127.0.0.1:${server.address().port}/feedback/?platform=windows&version=1.2.1`;
    await page.goto(url);
    await page.locator("#send:enabled").waitFor();
    await page.evaluate(() => document.fonts.ready);
    for (const width of [1440, 1024, 900, 800, 360, 320]) {
      await page.setViewportSize({
        width,
        height: width === 1440 ? 1100 : 900,
      });
      if (width === 360) {
        await page.goto(url.replace("platform=windows", "platform=android"));
        await page.locator("#send:enabled").waitFor();
      }
      assert.equal(
        await page.evaluate(
          () => document.documentElement.scrollWidth <= innerWidth,
        ),
        true,
        `horizontal overflow ${width}`,
      );
      if (width === 1440 || width === 360)
        await page.screenshot({
          path: path.join(screenshots, `feedback-${width}.png`),
          fullPage: true,
        });
    }
    await page.setViewportSize({ width: 1440, height: 1100 });
    await page.goto(url);
    await page.locator("#send:enabled").waitFor();
    assert.equal(await page.locator("#area").isVisible(), false);
    assert.equal(await page.locator("#environment").getAttribute("aria-label"), "Colosseum 1.2.1 · Windows");
    assert.match(await page.locator("#platform-icon").getAttribute("src"), /platform-windows/);
    assert.equal(await page.locator(".kind-options").evaluate(el => [...el.children].every(item => item.offsetTop === el.firstElementChild.offsetTop && item.scrollWidth <= item.clientWidth)), true, "desktop categories fit one row");
    await page.click("#area-trigger");
    await page.getByRole("option", { name: "Theatre", exact: true }).click();
    assert.equal(await page.inputValue("#area"), "Theatre");
    assert.equal(await page.locator("#area-trigger").evaluate(el => el === document.activeElement), true);
    await page.locator("#area-trigger").press("ArrowDown");
    await page.locator("#area-menu").press("Home");
    await page.locator("#area-menu").press("ArrowDown");
    await page.locator("#area-menu").press("Enter");
    assert.equal(await page.inputValue("#area"), "Tankoban");
    await page.click("#area-trigger");
    await page.locator("#area-menu").press("End");
    await page.locator("#area-menu").press(" ");
    assert.equal(await page.inputValue("#area"), "Vault");
    await page.click("#area-trigger");
    await page.locator("#area-menu").press("b");
    await page.locator("#area-menu").press("Enter");
    assert.equal(await page.inputValue("#area"), "Biblio");
    await page.click("#area-trigger");
    await page.locator("#area-menu").press("ArrowUp");
    await page.locator("#area-menu").press("Escape");
    assert.equal(await page.inputValue("#area"), "Biblio", "Escape preserves committed category");
    assert.equal(await page.locator("#area-menu").isVisible(), false);
    await page.click("#area-trigger");
    await page.click("#heading");
    assert.equal(await page.locator("#area-menu").isVisible(), false, "outside click closes");
    await page.click("#area-trigger");
    await page.locator("#area-menu").press("Tab");
    assert.equal(await page.locator("#title").evaluate(el => el === document.activeElement), true, "Tab follows form order");
    await page.click("#area-trigger");
    await page.locator("#area-menu").press("Shift+Tab");
    assert.equal(await page.getByRole("radio", { name: "Report a bug" }).evaluate(el => el === document.activeElement), true, "Shift+Tab returns to selected type");
    assert.equal(await page.locator("img").evaluateAll(images => images.every(image => image.complete && image.naturalWidth > 0)), true, "all world and platform icons load");
    await page.click("#area-trigger");
    await page.screenshot({ path: path.join(screenshots, "feedback-desktop-menu.png"), fullPage: true });
    await page.locator("#area-menu").press("Escape");
    assert.deepEqual(await page.locator("#area option").allTextContents(), [
      "General / I’m not sure",
      "Tankoban",
      "Biblio",
      "Theatre",
      "Universes",
      "Vault",
    ]);
    await page.getByRole("radio", { name: "Suggest a feature" }).check();
    assert.equal(
      await page.locator("#message-label").textContent(),
      "What would you like to see?",
    );
    await page.click("#area-trigger");
    await page.getByRole("option", { name: "Tankoban", exact: true }).click();
    await page.getByRole("radio", { name: "Universe request" }).check();
    assert.equal(await page.locator("#title").getAttribute("placeholder"), "Name the universe or franchise");
    assert.equal(await page.locator("#message-label").textContent(), "Which universe would you like to explore?");
    await page.fill("#title", "Keep this draft");
    await page.fill(
      "#message",
      "Please add a way to organize my series by language.",
    );
    status = 402;
    await page.click("#send");
    await page.getByText(/reached its capacity/).waitFor();
    assert.equal(await page.locator("#success").isVisible(), false);
    await page.reload();
    await page.locator("#send:enabled").waitFor();
    assert.equal(
      await page.inputValue("#message"),
      "Please add a way to organize my series by language.",
    );
    assert.equal(await page.getByRole("radio", { name: "Universe request" }).isChecked(), true);
    assert.equal(await page.locator("#area-value").textContent(), "Tankoban");
    config = {
      provider: "web3forms",
      accessKey: "11111111-2222-3333-4444-555555555555",
    };
    await page.reload();
    await page.locator("#send:enabled").waitFor();
    status = 200;
    accepted = false;
    await page.click("#send");
    await page.getByText(/could not be sent/).waitFor();
    assert.equal(await page.locator("#success").isVisible(), false);
    accepted = true;
    await page.click("#send");
    await page.locator("#success:not([hidden])").waitFor();
    assert.equal(
      await page.evaluate(() => localStorage.getItem("colosseum-feedback-v1")),
      null,
    );
    assert.equal(requests.length, 3);
    assert.equal(requests[0].body.reference, requests[2].body.reference);
    assert.equal(requests[0].body._email.subject, requests[2].body.subject);
    assert.match(requests[2].body.subject, /\[Universe request\] \[Tankoban\]/);
    assert.equal(Object.hasOwn(requests[2].body, "email"), false);
    for (const request of requests) assert.equal(request.body.type, "Universe request");
    await page.click("#another");
    assert.equal(await page.getByRole("radio", { name: "Report a bug" }).isChecked(), true);
    assert.equal(await page.locator("#area-value").textContent(), "General / I’m not sure");
    for (const platform of ["android", "linux", "osx", "unknown"]) {
      await page.goto(url.replace("platform=windows", `platform=${platform}`));
      await page.locator("#send:enabled").waitFor();
      await page.locator("#platform-icon").evaluate(img => img.decode());
      assert.equal(await page.locator("#platform-icon").evaluate(img => img.naturalWidth > 0), true);
    }
    assert.deepEqual(errors, []);
    console.log(
      "PASS feedback page: desktop/mobile layouts, glass menu mouse/keyboard/focus/closing, icons, universe drafts, quota failure, backend switch, provider rejection and accepted submission. All sends intercepted locally.",
    );
    console.log(`Screenshots: ${screenshots}`);
    await context.close();
  } finally {
    await browser.close();
    await new Promise((resolve) => server.close(resolve));
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
