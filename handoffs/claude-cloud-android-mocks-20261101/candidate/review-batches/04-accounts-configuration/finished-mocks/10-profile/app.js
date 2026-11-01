"use strict";
// Local, source-faithful interaction study. No network, app, shell, or backend calls.
(() => {
  const device = document.getElementById("device");
  const app = document.getElementById("app-screen");
  const scroller = document.getElementById("profile-scroll");
  const fitOutput = document.getElementById("fit-output");
  const eventOutput = document.getElementById("event-output");
  const scaleOutput = document.getElementById("scale-output");
  const fields = {
    width: document.getElementById("width-control"),
    height: document.getElementById("height-control"),
    top: document.getElementById("top-control"),
    bottom: document.getElementById("bottom-control"),
    scale: document.getElementById("scale-control")
  };
  const presets = [...document.querySelectorAll(".preset")];
  const queries = new URLSearchParams(location.search);
  const capture = queries.get("capture") === "1";
  const endpoints = {
    collections: "openNamedCollectionsPage()",
    downloads: "openDownloadsPage()",
    extensions: "openExtensionsPage()",
    colosseum: "openHistoryStatsPage()",
    trackers: "openSyncCenterPage()",
    stremio: "openStremioSyncPanel()",
    account: "accountFlyout.toggleAt(...)"
  };
  const state = {
    width: 412, height: 900, top: 0, bottom: 0, scale: 1
  };
  const clamp = (number, min, max, fallback) =>
    Number.isFinite(Number(number)) ? Math.min(max, Math.max(min, Number(number))) : fallback;
  const updateOutput = () => {
    scaleOutput.textContent = state.scale.toFixed(2) + "×";
    const viewport = scroller.getBoundingClientRect();
    const last = document.querySelector(".profile-row:last-child");
    const lastBottom = last.getBoundingClientRect().bottom;
    const column = document.querySelector(".profile-column").getBoundingClientRect();
    const horizontal = column.right > viewport.right + 0.5 || column.left < viewport.left - 0.5;
    const requiresScroll = scroller.scrollHeight > scroller.clientHeight + 1;
    const reachLast = !requiresScroll || lastBottom <= viewport.bottom + (scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight) + 1;
    fitOutput.textContent =
      "Canvas: " + state.width + "×" + state.height +
      " logical px · " + (state.width >= 840 ? "92px original wide rail" : "original bottom world tabs") +
      "\nProfile scroll: " + (requiresScroll ? "yes, independently scrollable" : "not needed") +
      " · Horizontal overflow: " + (horizontal ? "CHECK" : "none") +
      "\nSource rows: " + document.querySelectorAll(".profile-row").length +
      " · Minimum hit height: " + Math.round(Math.min(...[...document.querySelectorAll(".profile-row")].map(x => x.getBoundingClientRect().height))) +
      "px · Last row reachable: " + (reachLast ? "yes" : "CHECK") +
      "\nInsets: top " + state.top + " / bottom " + state.bottom + " px · Font: " + state.scale.toFixed(2) + "×";
  };
  const apply = (resetScroll = false) => {
    state.width = clamp(state.width, 320, 1920, 412);
    state.height = clamp(state.height, 300, 1400, 900);
    state.top = clamp(state.top, 0, 100, 0);
    state.bottom = clamp(state.bottom, 0, 100, 0);
    state.scale = clamp(state.scale, 1, 1.5, 1);
    device.style.width = state.width + "px";
    device.style.height = state.height + "px";
    device.style.setProperty("--safe-top", state.top + "px");
    device.style.setProperty("--safe-bottom", state.bottom + "px");
    device.style.setProperty("--font-scale", state.scale);
    app.classList.toggle("is-wide", state.width >= 840);
    for (const [key, field] of Object.entries(fields)) field.value = state[key];
    presets.forEach(button => {
      const selected = Number(button.dataset.width) === state.width &&
                       Number(button.dataset.height) === state.height;
      button.classList.toggle("is-selected", selected);
      button.setAttribute("aria-pressed", String(selected));
    });
    if (resetScroll) scroller.scrollTop = 0;
    requestAnimationFrame(updateOutput);
  };
  if (capture) document.body.classList.add("is-capture");
  {
    state.width = queries.get("w") || state.width;
    state.height = queries.get("h") || state.height;
    state.top = queries.get("top") || 0;
    state.bottom = queries.get("bottom") || 0;
    state.scale = queries.get("scale") || 1;
  }
  presets.forEach(button => button.addEventListener("click", () => {
    state.width = Number(button.dataset.width);
    state.height = Number(button.dataset.height);
    apply(true);
  }));
  for (const [key, field] of Object.entries(fields)) {
    // Numeric controls commit on blur/Enter so replacing all digits remains possible.
    field.addEventListener(key === "scale" ? "input" : "change", () => {
      if (field.value.trim() === "") return;
      const value = Number(field.value);
      if (Number.isFinite(value)) state[key] = value;
      apply(false);
    });
  }
  document.getElementById("reset-control").addEventListener("click", () => {
    Object.assign(state, { width: 412, height: 900, top: 0, bottom: 0, scale: 1 });
    apply(true);
  });
  document.getElementById("stress-control").addEventListener("click", () => {
    Object.assign(state, { width: 354, height: 792, top: 24, bottom: 24, scale: 1.3 });
    apply(true);
  });
  document.querySelectorAll(".profile-row").forEach(button => {
    button.addEventListener("pointerdown", () => button.classList.add("is-pressed"));
    for (const name of ["pointerup", "pointerleave", "pointercancel", "blur"])
      button.addEventListener(name, () => button.classList.remove("is-pressed"));
    button.addEventListener("click", () => {
      const key = button.dataset.key;
      eventOutput.textContent = 'rowRequested("' + key + '") → ' + endpoints[key] +
        "  [destination intentionally not mocked]";
      window.dispatchEvent(new CustomEvent("colosseum:rowRequested", { detail: { key } }));
      Batch4.emit("rowRequested",{key});
      const target={account:"account-flyout",stremio:"stremio-sync"}[key];
      if(target)Batch4.route(target);
    });
  });
  document.querySelectorAll(".world-tab").forEach(button => {
    button.addEventListener("click", () => {
      const name = button.dataset.world;
      eventOutput.textContent = name === "Profile"
        ? "profileRequested() → Profile remains visible"
        : 'worldRequested("' + name + '")  [world destination intentionally not mocked]';
      window.dispatchEvent(new CustomEvent("colosseum:shellRequest", { detail: { name } }));
    });
  });
  document.fonts.ready.then(updateOutput);
  window.addEventListener("resize", updateOutput);
  apply(true);
})();