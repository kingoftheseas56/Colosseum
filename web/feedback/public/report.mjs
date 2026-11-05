import { environment, sendFeedback, validConfiguration } from "./feedback-core.mjs";

const byId = (id) => document.getElementById(id);
const dialog = byId("feedback-dialog");
const form = byId("feedback-form");
const summary = byId("summary");
const description = byId("description");
const email = byId("email");
const details = byId("more-details");
const submit = byId("submit");
const status = byId("submission-status");
const query = new URLSearchParams(location.search);
const feature = query.get("kind") === "feature";
const env = environment(location.search);
const draftKey = `colosseum-home-feedback-${feature ? "feature" : "bug"}-v1`;
const fields = ["summary", "description", "email", ...(feature ? ["benefit"] : ["steps", "expected"])];
const submitLabel = feature ? "Submit request" : "Submit report";
let reference = crypto.randomUUID();
let config;
let sending = false;
let sent = false;
let challengeToken = "";

if (feature) {
  byId("dialog-title").textContent = "Feature Request";
  byId("dialog-intro").textContent = "Have an idea for Colosseum? Tell us what you would like to do.";
  byId("summary-label").textContent = "Feature name";
  summary.placeholder = "Give your idea a short name";
  byId("description-label").textContent = "What would you like to do?";
  description.placeholder = "Describe the feature or improvement you have in mind.";
  byId("bug-details").hidden = true;
  byId("feature-details").hidden = false;
}

function notice(message) {
  status.textContent = message;
  status.hidden = !message;
  if (message) status.scrollIntoView({ block: "nearest" });
}

function update() {
  submit.textContent = sending ? "Sending…" : submitLabel;
  submit.disabled = sending || sent || !config || !summary.value.trim() || !description.value.trim() ||
    (!!config.turnstileSiteKey && !challengeToken);
}

function saveDraft() {
  if (sent) return;
  const values = Object.fromEntries(fields.map((field) => [field, form.elements[field].value]));
  try {
    localStorage.setItem(draftKey, JSON.stringify({ values, expanded: details.open, reference }));
  } catch {
    dialog.querySelector(".footer-note").textContent = "Draft kept while this popup is open.";
  }
}

try {
  const draft = JSON.parse(localStorage.getItem(draftKey));
  if (draft) {
    for (const field of fields) {
      const input = form.elements[field];
      if (typeof draft.values?.[field] === "string")
        input.value = draft.values[field].slice(0, input.maxLength);
    }
    details.open = draft.expanded === true;
    if (typeof draft.reference === "string" && /^[\w-]{1,64}$/.test(draft.reference))
      reference = draft.reference;
  }
} catch {}

function close() {
  saveDraft();
  dialog.close();
  location.hash = "close-feedback";
}

for (const id of ["close-dialog", "cancel", "back-home"])
  byId(id).addEventListener("click", close);
dialog.addEventListener("cancel", (event) => { event.preventDefault(); close(); });
details.addEventListener("toggle", saveDraft);
form.addEventListener("input", () => { saveDraft(); update(); });
form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (submit.disabled) return;
  if (!email.validity.valid) details.open = true;
  if (!form.reportValidity()) return;
  const payload = {
    type: feature ? "Feature idea" : "Bug report",
    area: "General",
    title: summary.value.trim(),
    message: description.value.trim(),
    app_version: env.version || "Not supplied",
    platform: env.platform,
    reference,
    _gotcha: form.elements._gotcha.value,
  };
  const optional = feature ? { benefit: "benefit" }
    : { steps_to_reproduce: "steps", expected_behavior: "expected" };
  for (const [key, field] of Object.entries(optional)) {
    const value = form.elements[field].value.trim();
    if (value) payload[key] = value;
  }
  if (email.value.trim()) payload.email = email.value.trim();
  if (challengeToken) payload["cf-turnstile-response"] = challengeToken;
  saveDraft();
  sending = true;
  form.inert = true;
  notice("");
  update();
  try {
    await sendFeedback(config, payload);
    sent = true;
    try { localStorage.removeItem(draftKey); } catch {}
    form.hidden = true;
    byId("success-summary").textContent = payload.title;
    byId("success").hidden = false;
    byId("back-home").focus();
  } catch (error) {
    notice(error.message);
  } finally {
    form.inert = false;
    sending = false;
    if (config.turnstileSiteKey && window.turnstile) {
      challengeToken = "";
      window.turnstile.reset();
    }
    update();
  }
});

dialog.showModal();
summary.focus();
update();
try {
  const response = await fetch("./config.json", { cache: "no-store", credentials: "omit" });
  if (!response.ok) throw new Error();
  const settings = await response.json();
  if (!validConfiguration(settings)) throw new Error();
  config = settings;
  if (config.turnstileSiteKey) {
    const script = document.createElement("script");
    script.src = "https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit";
    const challengeError = () => { challengeToken = ""; update(); notice("Verification could not load."); };
    script.onerror = challengeError;
    script.onload = () => window.turnstile.ready(() => window.turnstile.render("#challenge", {
      sitekey: config.turnstileSiteKey, theme: "dark",
      callback: (token) => { challengeToken = token; update(); },
      "expired-callback": () => { challengeToken = ""; update(); },
      "error-callback": challengeError,
    }));
    document.head.append(script);
  }
  update();
} catch {
  notice("Feedback is unavailable right now. Your draft is saved.");
}
