import { environment, sendFeedback, validConfiguration } from "./feedback-core.mjs";
import { searchWikiArticles, wikiArticle } from "./universe-wiki.mjs";

const element = (id) => document.getElementById(id);
const dialog = element("request-dialog");
const form = element("request-form");
const input = element("franchise");
const results = element("results");
const summary = element("selection-summary");
const send = element("submit-request");
const scope = element("scope-message");
const success = element("success");
const env = environment(location.search);
const draftKey = "colosseum-universe-request-v1";
let reference = crypto.randomUUID();
let articles = [];
let available = [];
let selected = null;
let fallback = false;
let sending = false;
let config;
let challengeToken = "";
let searchTimer;
let searchController;

function text(tag, className, value) {
  const node = document.createElement(tag);
  node.className = className;
  node.textContent = value;
  return node;
}

function notice(message, error = false) {
  scope.hidden = !message;
  scope.textContent = message;
  scope.classList.toggle("error", error);
}

function empty(heading, message) {
  const card = text("div", "no-results", "");
  card.append(text("h3", "", heading), text("p", "", message));
  results.replaceChildren(card);
}

function availableInCatalogue(article) {
  return !!article && available.includes(article.title.replace(/ \(franchise\)$/i, "").toLowerCase());
}

function updateAction() {
  const existing = availableInCatalogue(selected);
  send.textContent = sending ? "Sending…" : existing ? "Already available"
    : !selected && fallback ? "Send typed name" : "Submit request";
  send.disabled = sending || existing || (!selected && !fallback) || !config ||
    (!!config.turnstileSiteKey && !challengeToken);
}

function articleUrl(article) {
  return "https://en.wikipedia.org/wiki/" + encodeURIComponent(article.title.replaceAll(" ", "_"));
}

function save() {
  try {
    localStorage.setItem(draftKey, JSON.stringify({
      title: input.value, selectedArticle: selected, reference, savedAt: Date.now(),
    }));
  } catch {}
}

function choose(article) {
  selected = article;
  Array.from(results.children).forEach((row, index) => {
    if (row.role === "radio") row.setAttribute("aria-checked", String(articles[index] === article));
  });
  summary.replaceChildren(text("strong", "", article?.title || "No page selected"));
  if (article) {
    const link = text("a", "", "View on Wikipedia");
    link.href = articleUrl(article);
    summary.append(link);
    const message = availableInCatalogue(article)
      ? article.title + " is already in Universes. You can find it in the catalogue behind this window."
      : /\(franchise\)$/i.test(article.title) ? "This page identifies the whole franchise." : "";
    notice(message);
  } else {
    summary.append(text("p", "", fallback ? "You can send the franchise name you typed." : "Select a Wikipedia page to continue."));
  }
  updateAction();
  save();
}

function renderArticles() {
  results.replaceChildren(...articles.map((article, index) => {
    const row = text("button", "result", "");
    row.type = "button";
    row.role = "radio";
    row.setAttribute("aria-checked", "false");
    const description = text("span", "", "");
    const title = text("span", "result-title", article.title);
    if (availableInCatalogue(article))
      title.append(text("span", "result-kind", "Already available"));
    else if (/\(franchise\)$/i.test(article.title))
      title.append(text("span", "result-kind franchise", "Franchise"));
    else {
      const kind = article.title.match(/\(([^()]*\b(?:film|novel|series|manga|game))\)$/i)?.[1];
      if (kind) title.append(text("span", "result-kind", kind));
    }
    description.append(title, text("span", "result-description", article.extract));
    row.append(text("span", "wiki-icon", "W"), description, text("span", "radio-mark", "✓"));
    row.addEventListener("click", () => choose(article));
    row.addEventListener("keydown", (event) => {
      const keys = ["ArrowDown", "ArrowRight", "ArrowUp", "ArrowLeft", "Home", "End"];
      const key = keys.indexOf(event.key);
      if (key === -1) return;
      event.preventDefault();
      const next = key === 4 ? 0 : key === 5 ? articles.length - 1
        : (index + (key < 2 ? 1 : -1) + articles.length) % articles.length;
      results.children[next].focus();
      choose(articles[next]);
    });
    return row;
  }));
}

function queueSearch(restore = null) {
  clearTimeout(searchTimer);
  searchController?.abort();
  searchController = null;
  articles = [];
  fallback = false;
  notice("");
  choose(null);
  element("clear-search").classList.toggle("hidden", !input.value);
  const query = input.value.trim();
  if (query.length < 2) {
    empty("What’s missing?", "Search for a franchise to see matching Wikipedia pages.");
    return;
  }
  empty("Searching Wikipedia…", "Looking for matching franchise pages.");
  searchTimer = setTimeout(async () => {
    const controller = new AbortController();
    searchController = controller;
    const timeout = setTimeout(() => controller.abort(), 8000);
    try {
      const matches = await searchWikiArticles(query, controller.signal);
      if (searchController !== controller) return;
      articles = matches;
      if (matches.length) renderArticles();
      else empty("No matching pages", "You can still send the franchise name you typed.");
    } catch {
      if (searchController !== controller) return;
      empty("Wikipedia is unavailable", "You can still send the franchise name you typed.");
    } finally {
      clearTimeout(timeout);
    }
    fallback = articles.length === 0;
    choose(articles.find((item) => item.pageid === restore?.pageid) || null);
  }, 300);
}

function close() {
  location.hash = "close-universe";
}

window.addEventListener("colosseum-universe-catalogue", (event) => {
  available = event.detail.map((name) => name.toLowerCase());
  if (articles.length) renderArticles();
  choose(selected);
});
element("close-dialog").addEventListener("click", close);
element("back-universes").addEventListener("click", close);
dialog.addEventListener("cancel", (event) => { event.preventDefault(); close(); });
input.addEventListener("input", () => queueSearch());
input.addEventListener("keydown", (event) => {
  if (event.key !== "ArrowDown" && event.key !== "Enter") return;
  event.preventDefault();
  if (articles.length) results.firstElementChild.focus();
});
element("clear-search").addEventListener("click", () => {
  input.value = "";
  queueSearch();
  input.focus();
});
for (const sample of document.querySelectorAll("[data-sample]")) {
  sample.type = "button";
  sample.addEventListener("click", () => {
    input.value = sample.dataset.sample;
    queueSearch();
    input.focus();
  });
}
element("another-request").addEventListener("click", () => {
  form.hidden = false;
  success.classList.add("hidden");
  queueSearch();
  input.focus();
});
form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (send.disabled) return;
  sending = true;
  updateAction();
  form.querySelector(".dialog-body").inert = true;
  const title = selected?.title || input.value.trim();
  const payload = {
    type: "Universe request", area: "Universes", title, message: input.value.trim(),
    app_version: env.version || "Not supplied", platform: env.platform, reference,
  };
  if (selected) {
    payload.wikipedia_url = articleUrl(selected);
    payload.wikipedia_page_id = selected.pageid;
  }
  if (challengeToken) payload["cf-turnstile-response"] = challengeToken;
  try {
    await sendFeedback(config, payload);
    try { localStorage.removeItem(draftKey); } catch {}
    input.value = "";
    reference = crypto.randomUUID();
    form.hidden = true;
    element("success-name").textContent = title;
    success.classList.remove("hidden");
    element("back-universes").focus();
  } catch (error) {
    notice(error.message, true);
  } finally {
    sending = false;
    form.querySelector(".dialog-body").inert = false;
    if (config.turnstileSiteKey && window.turnstile) {
      challengeToken = "";
      window.turnstile.reset();
    }
    updateAction();
  }
});

let restored = null;
try {
  const draft = JSON.parse(localStorage.getItem(draftKey));
  if (draft && Date.now() - draft.savedAt < 7 * 86400000) {
    if (typeof draft.title === "string") input.value = draft.title.slice(0, 120);
    if (typeof draft.reference === "string" && /^[\w-]{1,64}$/.test(draft.reference))
      reference = draft.reference;
    restored = wikiArticle(draft.selectedArticle);
  }
} catch {}
queueSearch(restored);
dialog.showModal();
input.focus();

try {
  const response = await fetch("./config.json", { cache: "no-store", credentials: "omit" });
  const settings = response.ok ? await response.json() : null;
  if (!validConfiguration(settings)) throw new Error();
  config = settings;
  if (config.turnstileSiteKey) {
    const script = document.createElement("script");
    script.src = "https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit";
    script.onload = () => window.turnstile.ready(() => window.turnstile.render("#challenge", {
      sitekey: config.turnstileSiteKey, theme: "dark",
      callback: (token) => { challengeToken = token; updateAction(); },
      "expired-callback": () => { challengeToken = ""; updateAction(); },
      "error-callback": () => { challengeToken = ""; updateAction(); notice("Verification could not load.", true); },
    }));
    script.onerror = () => notice("Verification could not load.", true);
    document.head.append(script);
  }
  updateAction();
} catch {
  notice("Requests are unavailable right now. Please try again later.", true);
}
