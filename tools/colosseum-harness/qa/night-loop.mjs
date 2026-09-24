// Night QA loop: keeps ChatGPT (inside ChatGPT Lite) running Colosseum QA tours back to back.
//
// Drives Lite's single ChatGPT page over raw CDP (Lite launched with --playwright exposes
// 127.0.0.1:9333). No dependencies: Node's built-in fetch + WebSocket.
//
// Each tour is a fresh plain ChatGPT chat seeded with one line pointing at TOUR-GUIDE.md.
// The guide makes every reply end with TOUR-STEP-DONE / TOUR-COMPLETE / TOUR-BLOCKED.
// This loop only answers "continue", retries after ChatGPT delivery errors, and opens the
// next tour. It never selects Pro and never sends when the thinking level is not High/Extra High.
//
// Usage: node night-loop.mjs [--until HH:MM] [--max-tours N] [--port 9333]
// Stop early: create artifacts/qa/STOP (night-qa.ps1 stop does this).

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, "..", "..", "..");
const QA_DIR = path.join(REPO, "artifacts", "qa");
const LOG = path.join(QA_DIR, "night-log.jsonl");
const STOP = path.join(QA_DIR, "STOP");
const GUIDE = path.join(HERE, "TOUR-GUIDE.md");

const arg = (name, fallback) => {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : fallback;
};
const PORT = Number(arg("--port", "9333"));
const MAX_TOURS = Number(arg("--max-tours", "20"));
const UNTIL = arg("--until", "07:30");

const POLL_MS = 45_000;
const STALL_MS = 40 * 60_000;       // one reply generating longer than this is stuck
const IDLE_NUDGE_MS = 2 * 60_000;   // idle without a marker this long -> "continue"
const MAX_CONTINUES = 12;           // per tour, then start a fresh tour
const MAX_BLOCKED_IN_A_ROW = 2;

const KICKOFF =
  `Run the next Colosseum QA tour. Read and follow this guide exactly, start to finish: ${GUIDE} ` +
  `(Colosseum repo root: ${REPO}). Use Brotherhood Desktop Commander for every command.`;

fs.mkdirSync(QA_DIR, { recursive: true });
const log = (event, extra = {}) => {
  const line = JSON.stringify({ ts: new Date().toISOString(), event, ...extra });
  fs.appendFileSync(LOG, line + "\n");
  console.log(line);
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function pastUntil() {
  const [h, m] = UNTIL.split(":").map(Number);
  const now = new Date();
  const until = new Date(now);
  until.setHours(h, m, 0, 0);
  // A loop started in the evening runs until tomorrow's HH:MM.
  if (until <= loopStart) until.setDate(until.getDate() + 1);
  return now >= until;
}
const loopStart = new Date();

// ---- minimal CDP client -------------------------------------------------------------
class Page {
  static async open() {
    const targets = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
    const t = targets.find((x) => x.type === "page" && x.url.includes("chatgpt.com"));
    if (!t) throw new Error("no chatgpt.com page on the Lite CDP port");
    const p = new Page(t.webSocketDebuggerUrl);
    await p.ready;
    return p;
  }
  constructor(url) {
    this.id = 0;
    this.pending = new Map();
    this.ws = new WebSocket(url);
    this.ready = new Promise((res, rej) => {
      this.ws.onopen = res;
      this.ws.onerror = () => rej(new Error("CDP socket error"));
    });
    this.ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      const cb = msg.id && this.pending.get(msg.id);
      if (cb) {
        this.pending.delete(msg.id);
        msg.error ? cb.rej(new Error(msg.error.message)) : cb.res(msg.result);
      }
    };
    this.ws.onclose = () => { for (const cb of this.pending.values()) cb.rej(new Error("CDP closed")); };
  }
  send(method, params = {}) {
    const id = ++this.id;
    this.ws.send(JSON.stringify({ id, method, params }));
    return new Promise((res, rej) => {
      this.pending.set(id, { res, rej });
      setTimeout(() => { if (this.pending.delete(id)) rej(new Error(`CDP timeout: ${method}`)); }, 30_000);
    });
  }
  async eval(expr) {
    const r = await this.send("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.text || "eval failed");
    return r.result.value;
  }
  close() { try { this.ws.close(); } catch {} }
}

// ---- page reading -------------------------------------------------------------------
const READ_STATE = `(() => {
  const assistants = [...document.querySelectorAll("[data-message-author-role='assistant']")];
  const last = assistants.at(-1)?.innerText ?? "";
  const body = document.body?.innerText ?? "";
  const levelBtn = [...document.querySelectorAll("button, [role=button], [aria-haspopup]")]
    .map((n) => (n.innerText || "").trim())
    .find((t) => /^(extra high|high|medium|low|light|heavy|pro|instant|standard)$/i.test(t)) ?? "";
  return {
    url: location.href,
    generating: !!document.querySelector("[data-testid='stop-button']"),
    composer: !!document.querySelector("#prompt-textarea"),
    lastTail: last.slice(-600),
    deliveryError: /Message delivery timed out|Something went wrong|network error|Hmm\\.\\.\\.something seems to have gone wrong/i.test(body.slice(-4000)),
    level: levelBtn,
  };
})()`;

const markerOf = (tail) => {
  const lines = tail.trim().split("\n").map((l) => l.trim()).filter(Boolean);
  const lastLine = lines.at(-1) ?? "";
  if (/^TOUR-COMPLETE\b/.test(lastLine)) return "complete";
  if (/^TOUR-BLOCKED\b/.test(lastLine)) return "blocked";
  if (/^TOUR-STEP-DONE\b/.test(lastLine)) return "step";
  return null;
};

async function typeAndSend(page, text) {
  await page.eval(`(() => { const b = document.querySelector("#prompt-textarea"); b.focus(); return true; })()`);
  await page.send("Input.insertText", { text });
  await sleep(600);
  await page.send("Input.dispatchKeyEvent", { type: "keyDown", key: "Enter", code: "Enter", windowsVirtualKeyCode: 13 });
  await page.send("Input.dispatchKeyEvent", { type: "keyUp", key: "Enter", code: "Enter", windowsVirtualKeyCode: 13 });
}

const levelOk = (level) => /^(high|extra high)$/i.test(level);

async function openNewChat(page) {
  await page.send("Page.navigate", { url: "https://chatgpt.com/" });
  // The thinking-level chip renders after the composer; wait for it, not just the composer.
  for (let i = 0; i < 45; i++) {
    await sleep(1000);
    const s = await page.eval(READ_STATE).catch(() => null);
    if (s?.composer && !s.url.includes("/c/") && s.level) return s;
  }
  return page.eval(READ_STATE).catch(() => ({ level: "" }));
}

async function startTour(page, n) {
  let s = await openNewChat(page);
  if (!s.level) s = await openNewChat(page); // one reload for a half-hydrated page
  if (!levelOk(s.level)) {
    log("level-refused", { level: s.level, note: "set ChatGPT to High or Extra High; never Pro" });
    return null;
  }
  await typeAndSend(page, KICKOFF);
  for (let i = 0; i < 60; i++) {
    await sleep(1000);
    const u = await page.eval("location.href");
    if (u.includes("/c/")) {
      log("tour-started", { tour: n, url: u, level: s.level });
      return { n, url: u, continues: 0, since: Date.now(), idleSince: null, genSince: null };
    }
  }
  log("tour-start-failed", { tour: n });
  return null;
}

// ---- main loop ----------------------------------------------------------------------
let page = null;
const RESUME = arg("--resume", null); // adopt an already-running tour chat URL
let tour = RESUME ? { n: 1, url: RESUME, continues: 0, since: Date.now(), idleSince: null, genSince: null } : null;
let tours = RESUME ? 1 : 0;
let blockedInARow = 0;
try { fs.unlinkSync(STOP); } catch {}
log("loop-start", { until: UNTIL, maxTours: MAX_TOURS, port: PORT });

while (true) {
  if (fs.existsSync(STOP)) { log("loop-stop", { reason: "STOP file" }); break; }
  if (pastUntil()) { log("loop-stop", { reason: `reached ${UNTIL}` }); break; }
  try {
    if (!page) page = await Page.open();
    if (!tour) {
      if (tours >= MAX_TOURS) { log("loop-stop", { reason: "max tours" }); break; }
      tour = await startTour(page, tours + 1);
      if (tour) tours++;
      else { await sleep(POLL_MS * 2); continue; }
      await sleep(POLL_MS);
      continue;
    }

    const s = await page.eval(READ_STATE);
    // A new chat first lives at /c/WEB:<temp>; ChatGPT then swaps in the real id. Adopt it.
    if (tour.url.includes("/c/WEB:") && /\/c\/(?!WEB:)[0-9a-f-]{36}/.test(s.url)) {
      log("tour-url", { tour: tour.n, from: tour.url, to: s.url });
      tour.url = s.url.split("?")[0];
    }
    if (!s.url.startsWith(tour.url)) {
      // Something navigated away (reload, Hemanth, crash recovery) - go back to the tour.
      await page.send("Page.navigate", { url: tour.url });
      await sleep(8000);
      continue;
    }

    if (s.generating) {
      tour.idleSince = null;
      tour.genSince ??= Date.now();
      if (Date.now() - tour.genSince > STALL_MS) {
        log("stalled-reply", { tour: tour.n });
        await page.eval(`document.querySelector("[data-testid='stop-button']")?.click()`);
        tour.genSince = null;
      }
    } else {
      tour.genSince = null;
      tour.idleSince ??= Date.now();
      const marker = markerOf(s.lastTail);
      if (marker === "complete") {
        log("tour-complete", { tour: tour.n, url: tour.url, continues: tour.continues });
        blockedInARow = 0;
        tour = null;
      } else if (marker === "blocked") {
        log("tour-blocked", { tour: tour.n, url: tour.url, reason: s.lastTail.split("\n").at(-1) });
        tour = null;
        if (++blockedInARow >= MAX_BLOCKED_IN_A_ROW) { log("loop-stop", { reason: "blocked twice in a row" }); break; }
      } else if (marker === "step" || s.deliveryError || Date.now() - tour.idleSince > IDLE_NUDGE_MS) {
        if (tour.continues >= MAX_CONTINUES) {
          log("tour-abandoned", { tour: tour.n, url: tour.url, reason: "continue budget spent" });
          tour = null;
        } else if (!levelOk(s.level)) {
          log("level-refused", { level: s.level, tour: tour.n });
          tour = null;
        } else {
          const why = marker === "step" ? "step-done" : s.deliveryError ? "delivery-error" : "idle-no-marker";
          await typeAndSend(page, s.deliveryError
            ? "The last reply failed to deliver. Continue the tour from where it stopped; keep this reply under 15 tool calls."
            : "continue");
          tour.continues++;
          tour.idleSince = null;
          log("continue", { tour: tour.n, why, n: tour.continues });
        }
      }
    }
  } catch (err) {
    log("error", { message: String(err?.message || err) });
    page?.close();
    page = null;
  }
  await sleep(POLL_MS);
}
page?.close();
log("loop-end", { tours });
