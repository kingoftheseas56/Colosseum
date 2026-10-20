/* Source-pinned fixture-only helpers for Universes, Vault and Downloads. */
window.UV = {
  esc: (s) =>
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
  img: (p) => "../assets/" + p,
  cover: (type, name) => "../assets/fixtures/captures/" + type + "/" + name,
  btn: (label, action, extra = "") =>
    `<button data-action="${action}" ${extra}>${label}</button>`,
  route: (id, params) => MockUI.go(id, params),
  dialog(title, html, setup, center = false) {
    const previous = document.activeElement,
      layer = document.createElement("div");
    layer.className = "overlay-layer";
    layer.innerHTML = `<section class="dialog ${center ? "center" : ""}" role="dialog" aria-modal="true" aria-labelledby="dialog-title"><button class="close" data-action="close" aria-label="Close dialog">×</button><h2 id="dialog-title">${title}</h2>${html}</section>`;
    const app = document.querySelector("#app");
    app.inert = true;
    document.body.append(layer);
    let dispose = () => {};
    const close = () => {
      dispose();
      layer.remove();
      app.inert = false;
      previous?.focus();
    };
    layer.querySelector("[data-action=close]").onclick = close;
    layer.addEventListener("keydown", (e) => {
      if (e.key === "Tab") {
        const f = [
            ...layer.querySelectorAll("button,input,select,a[href]"),
          ].filter((x) => !x.disabled && x.offsetParent !== null),
          i = f.indexOf(document.activeElement);
        if (e.shiftKey && i === 0) {
          e.preventDefault();
          f.at(-1)?.focus();
        } else if (!e.shiftKey && i === f.length - 1) {
          e.preventDefault();
          f[0]?.focus();
        }
      }
    });
    dispose = MockUI.registerOverlay(layer, close);
    layer.querySelector("button").focus();
    setup?.(layer, close);
    return { layer, close };
  },
  bind(root, handlers) {
    root.querySelectorAll("[data-action]").forEach((b) => {
      if (handlers[b.dataset.action])
        b.onclick = (e) => handlers[b.dataset.action](b, e);
    });
  },
  media(title, kind, img, params = {}) {
    return `<button class="media-card" data-media="${kind}" data-title="${this.esc(title)}" data-params='${this.esc(JSON.stringify(params))}'>${img ? `<img src="${img}" alt="">` : `<div class="book-cover">${this.esc(title)}</div>`}<strong>${this.esc(title)}</strong><p>${kind === "book" ? "Biblio" : kind === "manga" || kind === "comic" ? "Tankoban" : "Theatre"}</p></button>`;
  },
  wireMedia(root) {
    root.querySelectorAll("[data-media]").forEach(
      (b) =>
        (b.onclick = () => {
          const k = b.dataset.media;
          MockUI.go(
            {
              book: "book.detail",
              manga: "manga.series",
              comic: "comics.series",
              video: "theatre.detail",
            }[k],
            { title: b.dataset.title, ...JSON.parse(b.dataset.params || "{}") },
          );
        }),
    );
  },
};
