import { searchWikiArticles, wikiArticle } from "./universe-wiki.mjs";
import {
  DRAFT_KEY,
  environment,
  validate,
  validConfiguration,
  sendFeedback,
} from "./feedback-core.mjs";
const form = document.querySelector("form");
const status = document.querySelector("#status");
const send = document.querySelector("#send");
const fields = ["kind", "area", "title", "message", "email"];
const env = environment(location.search);
const universeMode = new URLSearchParams(location.search).get("mode") === "universe";
const draftKey = universeMode ? "colosseum-universe-request-v1" : DRAFT_KEY;
const title = form.elements.title;
const wikiStatus = document.querySelector("#wiki-status");
const wikiResults = document.querySelector("#wiki-results");
const wikiSummary = document.querySelector("#wiki-selected");
let selectedArticle = null;
let searchTimer;
let searchController;

if (universeMode) {
  document.body.classList.add("universe-request");
  document.title = "Request a Universe · Colosseum";
  document.querySelector(".form-panel").setAttribute("aria-label", "Request a Universe");
  document.querySelector("#title-label").textContent = "Franchise name";
  title.placeholder = "Enter a franchise name";
  title.setAttribute("aria-describedby", "title-error wiki-status");
  document.querySelector("#wiki-picker").hidden = false;
  document.querySelector(".privacy").textContent =
    "Names you search are sent to English Wikipedia. Your request goes privately to the Colosseum team.";
  document.querySelector("#success .eyebrow").textContent = "Request received";
  document.querySelector("#success-heading").textContent = "Thank you for your request.";
  document.querySelector("#success-heading + p").textContent = "Your request reached the Colosseum team.";
  document.querySelector("#another").textContent = "Send another request";
}

function displayArticle(article) {
  selectedArticle = article;
  wikiSummary.hidden = !article;
  wikiResults.replaceChildren();
  if (article) {
    document.querySelector("#wiki-title").textContent = article.title;
    document.querySelector("#wiki-preview").textContent = article.extract;
    wikiStatus.textContent = "";
  }
}

function queueWikiSearch() {
  clearTimeout(searchTimer);
  searchController?.abort();
  searchController = null;
  displayArticle(null);
  const query = title.value.trim();
  if (query.length < 2) {
    wikiStatus.textContent = "Type a name to find articles on English Wikipedia.";
    return;
  }
  wikiStatus.textContent = "Searching Wikipedia…";
  searchTimer = setTimeout(() => searchWiki(query), 300);
}

async function searchWiki(query) {
  const controller = new AbortController();
  searchController = controller;
  const timeout = setTimeout(() => controller.abort(), 8000);
  try {
    const articles = await searchWikiArticles(query, controller.signal);
    if (searchController !== controller) return;
    wikiStatus.textContent = articles.length
      ? "Choose a Wikipedia article or send the name you typed."
      : "No matching articles. You can send the name you typed.";
    for (const article of articles) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "wiki-article";
      const text = document.createElement("div");
      const name = document.createElement("strong");
      name.textContent = article.title;
      const preview = document.createElement("span");
      preview.textContent = article.extract;
      text.append(name, preview);
      button.append(text);
      button.addEventListener("click", () => {
        displayArticle(article);
        save();
        update();
        document.querySelector("#wiki-clear").focus();
      });
      wikiResults.append(button);
    }
  } catch {
    if (searchController === controller)
      wikiStatus.textContent = "Wikipedia lookup is unavailable. You can send the name you typed.";
  } finally {
    clearTimeout(timeout);
    if (searchController === controller) searchController = null;
  }
}
const platformFiles = { Windows: "windows", Android: "android", Linux: "linux", macOS: "macos", Web: "web" };
document.querySelector("#environment").setAttribute("aria-label", `Colosseum${env.version ? " " + env.version : ""} · ${env.platform}`);
document.querySelector("#app-version").textContent = env.version || "Colosseum";
document.querySelector("#platform-label").textContent = env.platform;
document.querySelector("#platform-icon").src = `./assets/platform-${platformFiles[env.platform]}.svg`;
const area = form.elements.area;
const picker = document.querySelector(".area-picker");
const trigger = document.querySelector("#area-trigger");
const menu = document.querySelector("#area-menu");
const worldIcons = ["colosseum", "comic-book", "book-library", "projector-theatre", "universes-atom", "vault-folder"];
const options = Array.from(area.options, (option, index) => {
  const item = document.createElement("div");
  item.id = `area-option-${index}`;
  item.role = "option";
  item.dataset.index = index;
  const icon = document.createElement("img");
  icon.src = `./assets/${worldIcons[index]}.svg`;
  icon.alt = "";
  icon.width = icon.height = 22;
  const label = document.createElement("span");
  label.textContent = option.textContent;
  item.append(icon, label);
  item.addEventListener("click", () => chooseArea(index));
  menu.append(item);
  return item;
});
let activeArea = 0;
let searchText = "";
let lastSearch = 0;
function syncArea() {
  document.querySelector("#area-value").textContent = area.selectedOptions[0].textContent;
  document.querySelector("#area-icon").src = `./assets/${worldIcons[area.selectedIndex]}.svg`;
  options.forEach((item, index) => item.setAttribute("aria-selected", String(index === area.selectedIndex)));
}
function highlightArea(index) {
  activeArea = (index + options.length) % options.length;
  options.forEach((item, i) => item.classList.toggle("active", i === activeArea));
  menu.setAttribute("aria-activedescendant", options[activeArea].id);
  options[activeArea].scrollIntoView({ block: "nearest" });
}
function openArea() {
  menu.hidden = false;
  trigger.setAttribute("aria-expanded", "true");
  searchText = "";
  highlightArea(area.selectedIndex);
  menu.focus({ preventScroll: true });
}
function closeArea(restore = false) {
  menu.hidden = true;
  trigger.setAttribute("aria-expanded", "false");
  if (restore) trigger.focus({ preventScroll: true });
}
function chooseArea(index) {
  area.selectedIndex = index;
  area.dispatchEvent(new Event("input", { bubbles: true }));
  area.dispatchEvent(new Event("change", { bubbles: true }));
  closeArea(true);
}
trigger.addEventListener("click", () => menu.hidden ? openArea() : closeArea(true));
trigger.addEventListener("keydown", (event) => {
  if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
    event.preventDefault();
    openArea();
    if (event.key === "Home") highlightArea(0);
    if (event.key === "End") highlightArea(options.length - 1);
  }
});
menu.addEventListener("keydown", (event) => {
  if (event.key === "Tab") {
    // Put focus back on the trigger so native Tab/Shift+Tab follow form order.
    closeArea(true);
    return;
  }
  if (event.key === "Escape") { event.preventDefault(); closeArea(true); return; }
  if (["Enter", " "].includes(event.key)) { event.preventDefault(); chooseArea(activeArea); return; }
  if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
    event.preventDefault();
    highlightArea(event.key === "Home" ? 0 : event.key === "End" ? options.length - 1 : activeArea + (event.key === "ArrowDown" ? 1 : -1));
  } else if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
    event.preventDefault();
    const now = Date.now();
    searchText = (now - lastSearch > 700 ? "" : searchText) + event.key.toLowerCase();
    lastSearch = now;
    const match = Array.from({ length: options.length }, (_, offset) => (activeArea + offset + 1) % options.length).find(index => area.options[index].textContent.toLowerCase().startsWith(searchText));
    if (match !== undefined) highlightArea(match);
  }
});
document.addEventListener("pointerdown", event => {
  if (!menu.hidden && !picker.contains(event.target)) closeArea();
});
picker.addEventListener("focusout", event => {
  if (!picker.contains(event.relatedTarget)) closeArea();
});
const draftNote = document.querySelector("#draft-note");
const defaultDraftNote = draftNote.textContent;
let config = {};
let reference = crypto.randomUUID();
let sending = false;
let challengeReady = false;
function values() {
  return Object.fromEntries(
    fields.map((name) => [name, form.elements.namedItem(name).value]),
  );
}
function notice(text, error = false) {
  status.textContent = text;
  status.classList.toggle("error", error);
}
function update() {
  const prompts = {
    bug: ["What happened?", "Tell us what you were doing, what you expected, and what happened instead.", "What happened?"],
    idea: ["What would you like to see?", "Tell us what you’d like to do and how it would help.", "What would you add or change?"],
  };
  const prompt = prompts[form.elements.kind.value] || prompts.bug;
  document.querySelector("#message-label").textContent = prompt[0];
  document.querySelector("#message-hint").textContent = prompt[1];
  if (!universeMode) title.placeholder = prompt[2];
  syncArea();
  document.querySelector("#character-count").textContent =
    `${form.elements.message.value.length.toLocaleString("en")} / 6,000`;
  if (universeMode && !sending)
    send.textContent = selectedArticle ? "Send request" : "Send typed name";
  send.disabled =
    sending ||
    (universeMode && !title.value.trim()) ||
    !validConfiguration(config) ||
    (!!config.turnstileSiteKey && !challengeReady);
}
function save() {
  try {
    localStorage.setItem(
      draftKey,
      JSON.stringify({ ...values(), ...(universeMode ? { selectedArticle } : {}), reference, savedAt: Date.now() }),
    );
  } catch {
    draftNote.textContent =
      "Keep this page open to preserve your draft.";
  }
}
function clear() {
  form.reset();
  draftNote.textContent = defaultDraftNote;
  reference = crypto.randomUUID();
  try {
    localStorage.removeItem(draftKey);
  } catch {}
  for (const field of ["title", "message", "email"]) {
    document.querySelector(`#${field}-error`).textContent = "";
    form.elements[field].removeAttribute("aria-invalid");
  }
  if (universeMode) queueWikiSearch();
  update();
}
try {
  const draft = JSON.parse(localStorage.getItem(draftKey));
  if (draft && Date.now() - draft.savedAt < 7 * 86400000) {
    for (const name of fields) {
      if (typeof draft[name] !== "string") continue;
      if (name === "kind") {
        if (draft.kind === "universe") {
          form.elements.kind.value = "idea";
          draftNote.textContent =
            "Your older Universe request draft was kept as a feature idea. To request a Universe, use the button in the Universes catalogue.";
        } else if (["bug", "idea"].includes(draft.kind)) form.elements.kind.value = draft.kind;
        continue;
      }
      if (
        name === "area" &&
        !Array.from(form.elements.area.options).some(
          (option) => option.value === draft[name],
        )
      )
        continue;
      form.elements.namedItem(name).value = draft[name];
    }
    if (universeMode && title.value.trim()) {
      const article = wikiArticle(draft.selectedArticle);
      if (article) displayArticle(article);
      else queueWikiSearch();
    }
    if (
      typeof draft.reference === "string" &&
      /^[\w-]{1,64}$/.test(draft.reference)
    )
      reference = draft.reference;
  } else localStorage.removeItem(draftKey);
} catch {}
document.querySelector("#wiki-clear").addEventListener("click", () => {
  queueWikiSearch();
  update();
  save();
  title.focus();
});
form.addEventListener("input", (event) => {
  if (universeMode && event.target === title) queueWikiSearch();
  update();
  save();
});
document.querySelector("#clear-draft").addEventListener("click", () => {
  if (!sending) {
    clear();
    notice("Draft cleared.");
    form.elements.title.focus();
  }
});
document.querySelector("#another").addEventListener("click", () => {
  document.querySelector("#success").hidden = true;
  form.hidden = false;
  notice("");
  form.elements.title.focus();
});
form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (sending || send.disabled) return;
  const data = values();
  data.email = data.email.trim();
  const errors = universeMode
    ? (data.title.trim().length > 120 ? { title: "Keep the franchise name to 120 characters." } : {})
    : validate(data);
  for (const name of ["title", "message", "email"]) {
    document.querySelector(`#${name}-error`).textContent = errors[name] || "";
    form.elements[name].setAttribute("aria-invalid", String(!!errors[name]));
  }
  if (Object.keys(errors).length) {
    form.elements[Object.keys(errors)[0]].focus();
    return;
  }
  save();
  sending = true;
  if (universeMode) {
    title.readOnly = true;
    document.querySelector("#wiki-picker").inert = true;
  }
  update();
  send.textContent = "Sending…";
  notice("");
  const payload = {
    type: universeMode ? "Universe request" : { bug: "Bug report", idea: "Feature idea" }[data.kind],
    area: universeMode ? "Universes" : data.area,
    title: universeMode && selectedArticle ? selectedArticle.title : data.title.trim(),
    message: universeMode ? data.title.trim() : data.message.trim(),
    app_version: env.version || "Not supplied",
    platform: env.platform,
    reference,
    _gotcha: form.elements._gotcha.value,
  };
  if (universeMode && selectedArticle) {
    const slug = encodeURIComponent(selectedArticle.title.replaceAll(" ", "_"));
    payload.wikipedia_url = `https://en.wikipedia.org/wiki/${slug}`;
    payload.wikipedia_page_id = selectedArticle.pageid;
  }
  if (!universeMode && data.email) payload.email = data.email;
  if (config.turnstileSiteKey)
    payload["cf-turnstile-response"] =
      form.querySelector('[name="cf-turnstile-response"]')?.value || "";
  try {
    await sendFeedback(config, payload);
    const sentReference = reference;
    clear();
    form.hidden = true;
    document.querySelector("#success").hidden = false;
    document.querySelector("#reference").textContent =
      `Your reference: ${sentReference}`;
    document.querySelector("#success").focus();
  } catch (error) {
    notice(error.message, true);
  } finally {
    sending = false;
    if (universeMode) {
      title.readOnly = false;
      document.querySelector("#wiki-picker").inert = false;
    } else send.innerHTML = 'Send feedback <span aria-hidden="true">↗</span>';
    if (config.turnstileSiteKey && window.turnstile) {
      challengeReady = false;
      window.turnstile.reset();
    }
    update();
  }
});
update();
try {
  const response = await fetch("./config.json", {
    cache: "no-store",
    credentials: "omit",
  });
  if (!response.ok) throw new Error();
  config = await response.json();
  if (!validConfiguration(config)) throw new Error();
  notice("");
  if (config.turnstileSiteKey) {
    window.colosseumChallengeReady = () =>
      window.turnstile.render("#challenge", {
        sitekey: config.turnstileSiteKey,
        theme: "dark",
        callback: () => {
          challengeReady = true;
          update();
        },
        "expired-callback": () => {
          challengeReady = false;
          update();
        },
        "error-callback": () => {
          challengeReady = false;
          notice(
            "Verification could not load. Check your connection and reload this page.",
            true,
          );
          update();
        },
      });
    const script = document.createElement("script");
    script.src =
      "https://challenges.cloudflare.com/turnstile/v0/api.js?onload=colosseumChallengeReady&render=explicit";
    script.async = true;
    script.onerror = () =>
      notice(
        "Verification could not load. Check your connection and reload this page.",
        true,
      );
    document.head.append(script);
  }
} catch {
  notice(
    "Feedback is being connected. Your draft will stay here; please try again later.",
    true,
  );
}
update();
