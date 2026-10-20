#!/usr/bin/env node
"use strict";

// Run from any working directory. Keep the complete mock folder together.
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createHash } = require("node:crypto");
const { pathToFileURL } = require("node:url");

const root = path.resolve(__dirname, "..");
const engine = process.argv[2] || "chromium";
const startedAt = new Date().toISOString();
const outputDir = path.resolve(
  process.env.QA_OUTPUT_DIR ||
    path.join(
      path.dirname(root),
      path.basename(root) + "-qa-results",
      engine,
      startedAt.replace(/[:.]/g, "-"),
    ),
);
const timeout = Number(process.env.QA_TIMEOUT_MS || 15000);
const report = {
  startedAt,
  platform: process.platform,
  engine,
  root,
  outputDir,
  viewport: { width: 1280, height: 720 },
  browserVersion: null,
  routes: [],
  issues: [],
  warnings: [],
  externalRequests: [],
  pass: false,
};
let activeRoute = null;
const knownFontMessages = new Map();

function identifyKnownFontMetadata() {
  // These unchanged source bytes have a malformed optional STAT name reference.
  // Firefox discards that metadata table but loads and renders the font outlines.
  const sha256 =
    "4063dec09ee3d7170bd8c6bf1f66a6bec01a3630390a922cdb9bcfbcb7e89bbc";
  for (const relative of [
    "assets/Fraunces-Regular.ttf",
    "assets/fonts/Fraunces-Regular.ttf",
  ]) {
    const file = path.join(root, relative);
    if (
      !fs.existsSync(file) ||
      createHash("sha256").update(fs.readFileSync(file)).digest("hex") !==
        sha256
    )
      continue;
    const source = pathToFileURL(file).href;
    for (const diagnostic of ["STAT: Invalid nameID: 256", "Table discarded"]) {
      const message = `[JavaScript Error: "downloadable font: ${diagnostic} (font-family: "Fraunces" style:normal weight:400 stretch:100 src index:0) source: ${source}"]`;
      knownFontMessages.set(message, { source, sha256, diagnostic });
    }
  }
}

function issue(type, detail) {
  report.issues.push({ route: activeRoute, type, ...detail });
}

function readRoutes() {
  const sandbox = { window: {} };
  vm.runInNewContext(
    fs.readFileSync(path.join(root, "shared/routes.js"), "utf8"),
    sandbox,
    { filename: "shared/routes.js", timeout: 1000 },
  );
  const registry = sandbox.window.MOCK_ROUTES;
  if (
    !registry ||
    typeof registry !== "object" ||
    !Object.keys(registry).length
  ) {
    throw new Error(
      "shared/routes.js must define a nonempty window.MOCK_ROUTES.",
    );
  }
  return Object.entries(registry);
}

async function launchBrowser() {
  if (!["chromium", "firefox"].includes(engine)) {
    throw new Error("Usage: node qa/verify.cjs [chromium|firefox]");
  }
  if (!Number.isFinite(timeout) || timeout <= 0) {
    throw new Error("QA_TIMEOUT_MS must be a positive number.");
  }
  const playwright = require("playwright");
  const executablePath = process.env.QA_BROWSER_EXECUTABLE;
  const helperPath = process.env.QA_FIREFOX_LAUNCH_HELPER;
  if (engine === "firefox" && helperPath) {
    if (executablePath) {
      throw new Error(
        "Use either QA_BROWSER_EXECUTABLE or QA_FIREFOX_LAUNCH_HELPER for Firefox, not both.",
      );
    }
    const helper = require(path.resolve(helperPath));
    if (typeof helper.launchFirefox !== "function") {
      throw new Error(
        "QA_FIREFOX_LAUNCH_HELPER must export async launchFirefox().",
      );
    }
    return helper.launchFirefox();
  }
  return playwright[engine].launch({
    headless: true,
    timeout,
    ...(executablePath ? { executablePath: path.resolve(executablePath) } : {}),
  });
}

async function settle(frame) {
  await frame.waitForLoadState("load", { timeout });
  // A new iframe can briefly have a null document/documentElement in Firefox.
  await frame.waitForFunction(
    () => {
      const doc = typeof document === "undefined" ? null : document;
      return (
        !!doc?.documentElement &&
        (!doc.fonts || doc.fonts.status === "loaded") &&
        Array.from(doc.images).every((image) => image.complete)
      );
    },
    null,
    { timeout },
  );
  await frame.evaluate(async () => {
    if (document.fonts) await document.fonts.ready;
    await Promise.all(
      Array.from(document.images).map((image) =>
        typeof image.decode === "function"
          ? image.decode().catch(() => {})
          : undefined,
      ),
    );
    await new Promise((resolve) =>
      requestAnimationFrame(() => requestAnimationFrame(resolve)),
    );
  });
}

async function getView(page) {
  const element = await page.waitForSelector("#view", {
    state: "attached",
    timeout,
  });
  const frame = await element.contentFrame();
  await element.dispose();
  if (!frame) throw new Error("The host #view iframe has no content frame.");
  await frame.waitForFunction(
    () => {
      const doc = typeof document === "undefined" ? null : document;
      return doc?.documentElement?.dataset.mockReady === "true";
    },
    null,
    { timeout },
  );
  return frame;
}

async function facts(frame) {
  return frame.evaluate(() => ({
    url: location.href,
    title: document.title,
    text: document.body?.innerText.slice(0, 500) || "",
    width: document.documentElement.scrollWidth,
    height: document.documentElement.scrollHeight,
    brokenImages: Array.from(document.images)
      .filter((image) => !image.complete || !image.naturalWidth)
      .map((image) => image.currentSrc || image.src),
    failedFonts: document.fonts
      ? Array.from(document.fonts)
          .filter((font) => font.status === "error")
          .map((font) => font.family)
      : [],
    fontFaces: document.fonts
      ? Array.from(document.fonts).map((font) => ({
          family: font.family,
          status: font.status,
        }))
      : [],
    resourceErrors: window.__mockQAResourceErrors || [],
  }));
}

function checkFacts(value, scope) {
  for (const url of value.brokenImages) issue("broken-image", { scope, url });
  for (const family of value.failedFonts)
    issue("font-error", { scope, family });
  for (const error of value.resourceErrors)
    issue("resource-error", { scope, ...error });
}

async function main() {
  fs.mkdirSync(path.join(outputDir, "screenshots"), { recursive: true });
  let browser;
  try {
    const routes = readRoutes();
    identifyKnownFontMetadata();
    report.expectedRoutes = routes.length;
    browser = await launchBrowser();
    report.browserVersion = browser.version();
    const context = await browser.newContext({
      viewport: report.viewport,
      deviceScaleFactor: 1,
      serviceWorkers: "block",
    });
    context.setDefaultTimeout(timeout);
    context.setDefaultNavigationTimeout(timeout);
    await context.route(/^https?:/i, async (route) => {
      const url = route.request().url();
      report.externalRequests.push({
        route: activeRoute,
        url,
        transport: "http",
      });
      issue("external-request", { url });
      await route.abort("blockedbyclient");
    });
    // Never connect routed WebSockets to their remote server.
    await context.routeWebSocket(/^wss?:/i, (socket) => {
      const url = socket.url();
      report.externalRequests.push({
        route: activeRoute,
        url,
        transport: "websocket",
      });
      issue("external-request", { url });
      socket.close();
    });
    context.on("requestfailed", (request) => {
      if (request.url().startsWith("file:")) {
        issue("local-request-failed", {
          url: request.url(),
          error: request.failure()?.errorText || "Unknown request failure",
        });
      }
    });
    await context.addInitScript(() => {
      window.__mockQAResourceErrors = [];
      window.addEventListener(
        "error",
        (event) => {
          const target = event.target;
          if (target && target !== window && (target.src || target.href)) {
            window.__mockQAResourceErrors.push({
              tag: target.tagName,
              url: target.currentSrc || target.src || target.href,
            });
          }
        },
        true,
      );
    });
    context.on("page", (page) => {
      page.on("pageerror", (error) =>
        issue("javascript-error", { error: String(error) }),
      );
      page.on("console", (message) => {
        if (message.type() === "error") {
          const detail = {
            error: message.text(),
            location: message.location(),
          };
          const metadata =
            engine === "firefox" && knownFontMessages.get(detail.error);
          if (metadata) {
            report.warnings.push({
              route: activeRoute,
              type: "known-font-metadata",
              ...detail,
              ...metadata,
            });
          } else {
            issue("console-error", detail);
          }
        }
      });
      page.on("crash", () =>
        issue("page-crash", { error: "Browser page crashed." }),
      );
      page.on("dialog", async (dialog) => {
        issue("unexpected-dialog", { error: dialog.message() });
        await dialog.dismiss();
      });
    });
    const page = await context.newPage();
    await page.goto(pathToFileURL(path.join(root, "index.html")).href, {
      waitUntil: "load",
    });
    await page.waitForFunction(() => typeof window.MockHost?.go === "function");
    await settle(await getView(page));
    await settle(page.mainFrame());
    report.host = await facts(page.mainFrame());
    checkFacts(report.host, "host");

    for (const [index, [id, route]] of routes.entries()) {
      activeRoute = id;
      const result = {
        id,
        file: route.file,
        params: route.params || {},
        pass: false,
      };
      report.routes.push(result);
      try {
        const routeFile = path.resolve(root, route.file);
        const relative = path.relative(root, routeFile);
        if (
          relative.startsWith(".." + path.sep) ||
          relative === ".." ||
          path.isAbsolute(relative)
        ) {
          throw new Error("Route file escapes the mock folder: " + route.file);
        }
        fs.accessSync(routeFile, fs.constants.R_OK);
        await page.evaluate(
          ({ id, params }) => window.MockHost.go(id, params),
          { id, params: result.params },
        );
        const frame = await getView(page);
        // Readiness must belong to the requested file, never an old/detaching frame.
        if (
          new URL(frame.url()).pathname !==
          new URL(pathToFileURL(routeFile)).pathname
        ) {
          throw new Error(
            "Host opened an unexpected route file: " + frame.url(),
          );
        }
        await settle(frame);
        await settle(page.mainFrame());
        result.facts = await facts(frame);
        checkFacts(result.facts, "route");
        checkFacts(await facts(page.mainFrame()), "host");
      } catch (error) {
        issue("route-failure", { error: String(error) });
      }
      // Capture failures too, whenever the browser page is still usable.
      const filename =
        String(index + 1).padStart(3, "0") +
        "-" +
        id.replace(/[^a-zA-Z0-9._-]/g, "_") +
        ".png";
      try {
        await page.screenshot({
          path: path.join(outputDir, "screenshots", filename),
          timeout,
        });
        result.screenshot = "screenshots/" + filename;
      } catch (error) {
        issue("screenshot-failure", { error: String(error) });
      }
      result.issues = report.issues.filter((item) => item.route === id);
      result.pass = result.issues.length === 0;
      console.log(`${result.pass ? "PASS" : "FAIL"} ${id}`);
    }
    activeRoute = null;
  } catch (error) {
    issue("fatal", { error: error.stack || String(error) });
  } finally {
    if (browser) {
      try {
        await browser.close();
      } catch (error) {
        issue("browser-close", { error: String(error) });
      }
    }
    // Include any late asynchronous errors before deciding the exit code.
    for (const route of report.routes) {
      route.issues = report.issues.filter((item) => item.route === route.id);
      route.warnings = report.warnings.filter(
        (item) => item.route === route.id,
      );
      route.pass = route.issues.length === 0 && !!route.screenshot;
    }
    report.finishedAt = new Date().toISOString();
    report.pass =
      report.routes.length > 0 &&
      report.routes.length === report.expectedRoutes &&
      report.routes.every((route) => route.pass) &&
      report.issues.length === 0;
    report.summary = {
      expected: report.expectedRoutes || 0,
      tested: report.routes.length,
      passed: report.routes.filter((route) => route.pass).length,
      failed: report.routes
        .filter((route) => !route.pass)
        .map((route) => route.id),
      issues: report.issues.length,
      warnings: report.warnings.length,
      externalRequests: report.externalRequests.length,
    };
    fs.writeFileSync(
      path.join(outputDir, "results.json"),
      JSON.stringify(report, null, 2) + "\n",
    );
    console.log(
      JSON.stringify({
        pass: report.pass,
        ...report.summary,
        report: path.join(outputDir, "results.json"),
      }),
    );
    process.exitCode = report.pass ? 0 : 1;
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
