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
  const headers = await fs.readFile(path.join(root, "_headers"), "utf8");
  const csp = headers.match(/^\s*Content-Security-Policy:\s*(.+)$/m)[1];
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
      if (name.endsWith(".html")) res.setHeader("Content-Security-Policy", csp);
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
    const lookups = [];
    await context.route("https://en.wikipedia.org/w/api.php?**", async route => {
      const request = route.request();
      const params = new URL(request.url()).searchParams;
      assert.equal(params.get("origin"), "*");
      assert.equal(params.get("gsrnamespace"), "0");
      assert.equal(params.get("explaintext"), "1");
      assert.match(request.headers()["api-user-agent"], /^ColosseumUniverseRequest/);
      const query = params.get("gsrsearch");
      lookups.push(query);
      if (query === "Slow query") await new Promise(resolve => setTimeout(resolve, 900));
      const result = { pageid: 1234, index: 1, title: "Example & Co. (franchise)", extract: "A film and game series." };
      const pages = query === "Name with no article" ? [] : [result, {
        pageid: 5678, index: 2, title: "Example (disambiguation)", pageprops: { disambiguation: "" },
      }];
      await route.fulfill({
        status: query === "Offline example" ? 503 : 200,
        json: { query: { pages } },
      }).catch(error => {
        if (query !== "Slow query") throw error;
      });
    });
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
    assert.equal(await page.getByRole("radio").count(), 2);
    await page.fill("#title", "Keep this draft");
    await page.fill(
      "#message",
      "Please add a way to organize my series by language.",
    );
    await page.evaluate(() => {
      const key = "colosseum-feedback-v1";
      const draft = JSON.parse(localStorage.getItem(key));
      localStorage.setItem(key, JSON.stringify({ ...draft, kind: "universe" }));
    });
    await page.reload();
    await page.locator("#send:enabled").waitFor();
    assert.equal(await page.inputValue("#title"), "Keep this draft");
    assert.equal(await page.getByRole("radio", { name: "Suggest a feature" }).isChecked(), true);
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
    assert.equal(await page.getByRole("radio", { name: "Suggest a feature" }).isChecked(), true);
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
    assert.match(requests[2].body.subject, /\[Feature idea\] \[Tankoban\]/);
    assert.equal(Object.hasOwn(requests[2].body, "email"), false);
    for (const request of requests) assert.equal(request.body.type, "Feature idea");
    await page.click("#another");
    assert.equal(await page.getByRole("radio", { name: "Report a bug" }).isChecked(), true);
    assert.equal(await page.locator("#area-value").textContent(), "General / I’m not sure");
    for (const platform of ["android", "linux", "osx", "unknown"]) {
      await page.goto(url.replace("platform=windows", `platform=${platform}`));
      await page.locator("#send:enabled").waitFor();
      await page.locator("#platform-icon").evaluate(img => img.decode());
      assert.equal(await page.locator("#platform-icon").evaluate(img => img.naturalWidth > 0), true);
    }
    config = { provider: "formspark", formEndpoint: "https://submit-form.com/test-form" };
    await page.goto(url + "&mode=universe");
    assert.equal(await page.getByRole("radio").count(), 0);
    assert.equal(await page.getByLabel("Franchise name").isVisible(), true);
    assert.equal(await page.getByRole("button", { name: "Send typed name" }).isDisabled(), true);
    await page.fill("#title", "Slow query");
    await page.waitForRequest(request => request.url().includes("gsrsearch=Slow+query"));
    await page.fill("#title", "Example & Co.");
    await page.getByRole("button", { name: /Example & Co. \(franchise\)/ }).click();
    await page.locator("#wiki-selected:visible").waitFor();
    await page.reload();
    await page.locator("#wiki-selected:visible").waitFor();
    assert.equal(await page.locator("#wiki-title").textContent(), "Example & Co. (franchise)");
    assert.deepEqual(lookups, ["Slow query", "Example & Co."]);
    status = 402;
    await page.getByRole("button", { name: "Send request" }).click();
    await page.getByText(/reached its capacity/).waitFor();
    assert.equal(await page.locator("#success").isVisible(), false);
    assert.equal(await page.inputValue("#title"), "Example & Co.");
    assert.equal(await page.locator("#wiki-selected").isVisible(), true);
    status = 200;
    await page.getByRole("button", { name: "Send request" }).click();
    await page.locator("#success:not([hidden])").waitFor();
    const article = requests.at(-1).body;
    assert.equal(article.type, "Universe request");
    assert.equal(article.area, "Universes");
    assert.equal(article.platform, "Windows");
    assert.equal(article.app_version, "1.2.1");
    assert.equal(article.title, "Example & Co. (franchise)");
    assert.equal(article.wikipedia_page_id, 1234);
    assert.equal(article.wikipedia_url, "https://en.wikipedia.org/wiki/Example_%26_Co._(franchise)");
    assert.equal(article.reference, requests.at(-2).body.reference);
    await page.click("#another");
    await page.fill("#title", "Name with no article");
    await page.getByText(/No matching articles/).waitFor();
    await page.fill("#title", "Offline example");
    await page.getByText(/Wikipedia lookup is unavailable/).waitFor();
    await page.getByRole("button", { name: "Send typed name" }).click();
    await page.locator("#success:not([hidden])").waitFor();
    const manual = requests.at(-1).body;
    assert.equal(manual.type, "Universe request");
    assert.equal(manual.title, "Offline example");
    assert.equal(Object.hasOwn(manual, "wikipedia_url"), false);
    assert.equal(Object.hasOwn(manual, "wikipedia_page_id"), false);
    assert.equal(await page.evaluate(() => localStorage.getItem("colosseum-universe-request-v1")), null);
    await page.setViewportSize({ width: 1280, height: 550 });
    await page.goto(url.replace("/feedback/?", "/feedback/universe.html?"));
    const dialog = page.getByRole("dialog", { name: "Request a Universe" });
    assert.equal((await dialog.boundingBox()).width, 880);
    assert.equal(await page.locator("#franchise").evaluate(input => input === document.activeElement), true);
    assert.equal(await page.getByText("What’s missing?").isVisible(), true);
    assert.equal(await page.getByRole("button", { name: "Submit request" }).isDisabled(), true);
    await page.screenshot({ path: path.join(screenshots, "universe-dialog-empty.png") });
    await page.getByRole("button", { name: "Avatar" }).click();
    await page.getByRole("radio", { name: /Example & Co/ }).click();
    await page.evaluate(() => window.dispatchEvent(new CustomEvent("colosseum-universe-catalogue", {
      detail: ["Example & Co."],
    })));
    assert.equal(await page.getByRole("button", { name: "Already available" }).isDisabled(), true);
    await page.evaluate(() => window.dispatchEvent(new CustomEvent("colosseum-universe-catalogue", {
      detail: [],
    })));
    await page.getByRole("button", { name: "Submit request" }).click();
    await page.getByText("Request received").waitFor();
    assert.equal(requests.at(-1).body.wikipedia_page_id, 1234);
    await page.getByRole("button", { name: "Back to Universes" }).click();
    await page.waitForURL((current) => current.hash === "#close-universe");
    const homeReport = url.replace("/feedback/?", "/feedback/report.html?");
    await page.goto(homeReport + "&kind=bug");
    assert.equal(await page.getByRole("dialog", { name: "Report a Bug" }).isVisible(), true);
    assert.equal(await page.getByRole("button", { name: "Submit report" }).isDisabled(), true);
    await page.getByLabel("Short summary").fill("Test bug title");
    await page.getByLabel("What went wrong?").fill("Test bug description");
    await page.getByText("Add more details").click();
    await page.getByLabel("Steps to reproduce").fill("Open the Home carousel");
    await page.getByRole("button", { name: "Cancel" }).click();
    await page.waitForURL((current) => current.hash === "#close-feedback");
    await page.goto(homeReport + "&kind=feature");
    assert.equal(await page.getByRole("dialog", { name: "Feature Request" }).isVisible(), true);
    assert.equal(await page.getByLabel("Feature name").inputValue(), "");
    await page.getByLabel("Feature name").fill("A test feature");
    await page.getByLabel("What would you like to do?").fill("Describe a new Home action");
    await page.getByText("Add more details").click();
    await page.getByLabel("How would this help?").fill("Fewer clicks");
    await page.getByRole("button", { name: "Submit request" }).click();
    await page.getByText("Feedback received").waitFor();
    assert.equal(requests.at(-1).body.type, "Feature idea");
    assert.equal(requests.at(-1).body.benefit, "Fewer clicks");
    await page.setViewportSize({ width: 360, height: 740 });
    await page.goto(homeReport + "&kind=bug");
    assert.equal(await page.getByLabel("Short summary").inputValue(), "Test bug title");
    assert.equal(await page.getByLabel("Steps to reproduce").inputValue(), "Open the Home carousel");
    await page.screenshot({ path: path.join(screenshots, "home-feedback-narrow.png") });
    status = 429;
    await page.getByRole("button", { name: "Submit report" }).click();
    await page.getByText(/Too many attempts/).waitFor();
    const bugReference = requests.at(-1).body.reference;
    await page.reload();
    status = 200;
    await page.getByRole("button", { name: "Submit report" }).click();
    await page.getByText("Feedback received").waitFor();
    assert.equal(requests.at(-1).body.type, "Bug report");
    assert.equal(requests.at(-1).body.steps_to_reproduce, "Open the Home carousel");
    assert.equal(requests.at(-1).body.reference, bugReference);
    assert.deepEqual(errors, []);
    console.log(
      "PASS feedback page: desktop/mobile layouts, glass menu mouse/keyboard/focus/closing, icons, legacy draft migration, quota failure, backend switch, provider rejection, and Universe lookup/selection/manual submission. All external requests intercepted locally.",
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
