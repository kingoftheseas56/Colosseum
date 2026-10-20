const U = {
  e: (s) =>
    String(s ?? "").replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    ),
  btn: (label, id, cls = "") =>
    `<button class="${cls}" data-action="${id}">${label}</button>`,
  field: (label, id, secret = false) =>
    `<label class="field" for="${id}">${label}${secret ? `<span class="secret"><input id="${id}" type="password" autocomplete="off" placeholder="DEMO only; never enter a real secret"><button type="button" data-reveal="${id}" aria-label="Show ${label}">Show</button></span>` : `<input id="${id}" autocomplete="off">`}</label>`,
  toggle: (label, id, on = true) =>
    `<label class="switch"><span>${label}</span><input type="checkbox" data-toggle="${id}" ${on ? "checked" : ""} aria-label="${label}"></label>`,
  status: (s) => {
    let t = document.querySelector("#status");
    if (t) t.textContent = s;
  },
  clear: () =>
    document
      .querySelectorAll("input[type=password],input[data-secret=true]")
      .forEach((x) => (x.value = "")),
};
let dispose = null,
  focusReturn = null,
  cancelReturn = null;
U.close = () => {
  cancelReturn = null;
  U.clear();
  let o = document.querySelector("#overlay");
  if (o) o.remove();
  if (dispose) {
    dispose();
    dispose = null;
  }
  document.querySelector("#main").inert = false;
  if (focusReturn && focusReturn.isConnected) focusReturn.focus();
};
U.cancel = () => {
  const cb = cancelReturn;
  U.close();
  if (cb) cb();
};
U.modal = (title, body, actions = "", wide = false, onCancel = null) => {
  if (document.querySelector("#overlay")) U.close();
  focusReturn = document.activeElement;
  let o = document.createElement("div");
  o.id = "overlay";
  o.className = "overlay";
  o.innerHTML = `<section role="dialog" aria-modal="true" aria-labelledby="dialog-title" class="dialog ${wide ? "wide" : ""}"><button class="close" data-action="close" aria-label="Close dialog">×</button><h2 id="dialog-title">${title}</h2><div class="body">${body}</div><div class="actions">${U.btn("Cancel", "close")}${actions}</div><div class="status" id="modal-status" role="status"></div></section>`;
  document.body.append(o);
  document.querySelector("#main").inert = true;
  cancelReturn = onCancel;
  dispose = MockUI.registerOverlay(o, U.cancel);
  o.querySelector("[data-action=close]").focus();
  o.addEventListener("keydown", (e) => {
    if (e.key !== "Tab") return;
    let a = [...o.querySelectorAll("button,input,select,a")].filter(
      (x) => !x.disabled,
    );
    if (e.shiftKey && document.activeElement === a[0]) {
      e.preventDefault();
      a.at(-1).focus();
    } else if (!e.shiftKey && document.activeElement === a.at(-1)) {
      e.preventDefault();
      a[0].focus();
    }
  });
};
document.addEventListener("click", (e) => {
  let b = e.target.closest("[data-action],[data-go],[data-reveal]");
  if (!b) return;
  if (b.dataset.reveal) {
    let i = document.getElementById(b.dataset.reveal);
    i.type = i.type === "password" ? "text" : "password";
    i.dataset.secret = "true";
    b.textContent = i.type === "password" ? "Show" : "Hide";
    return;
  }
  if (b.dataset.go) {
    U.clear();
    MockUI.go(b.dataset.go);
    return;
  }
  if (b.dataset.action === "close") U.cancel();
  else if (b.dataset.action === "back") {
    U.clear();
    MockUI.back();
  } else if (window.handleAction) window.handleAction(b.dataset.action, b);
});
window.addEventListener("pagehide", U.clear);
