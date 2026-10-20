/* ComicArchiveBoard -> GenreMosaic -> ComicArchiveIndex. LOCG publisher is parked. */
window.renderComicArchives = function ({ view, params: P, container: c }) {
  const M = MockUI,
    E = (s) =>
      String(s).replace(
        /[&<>"']/g,
        (c) =>
          ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;",
          })[c],
      );
  let index = view === "archive-index",
    state = P.state || "",
    box = P.box || P.publisher || "DC Comics",
    covers = [
      "sandman",
      "watchmen",
      "invincible",
      "hellboy",
      "saga",
      "sincity",
    ],
    titles = [
      "The Sandman",
      "Watchmen",
      "Invincible",
      "Hellboy",
      "Saga",
      "Sin City",
    ];
  document.body.classList.add("comic-archive-page");
  document.querySelector(".comic-hero").hidden = true;
  document.querySelector(".comic-tabs").hidden = true;
  let chrome = document.querySelector(".source-top");
  chrome.querySelector("span").hidden = true;
  let close = chrome.querySelector("[data-action=close]");
  close.innerHTML = '<img src="../assets/icons/power.svg" alt="">';
  close.setAttribute("aria-label", "Close archives");
  if (!chrome.querySelector("[data-action=archive-minimize]")) {
    for (let [action, icon, label] of [
      ["archive-minimize", "minimize", "Minimize"],
      ["archive-fullscreen", "fullscreen-exit", "Toggle fullscreen"],
    ]) {
      let b = document.createElement("button");
      b.className = "archive-window-action";
      b.dataset.action = action;
      b.innerHTML = '<img src="../assets/icons/' + icon + '.svg" alt="">';
      b.setAttribute("aria-label", label);
      b.onclick = () =>
        action === "archive-minimize"
          ? M.back()
          : document.documentElement.requestFullscreen?.().catch(() => {});
      chrome.insertBefore(b, close);
    }
  }
  const boxes = [
    ["Marvel Comics", 2486, -1],
    ["DC Comics", 1924, 0],
    ["Image Comics", 984, 4],
    ["Dark Horse Comics", 741, 3],
    ["IDW", 532, -1],
    ["Batman", 674, -1],
    ["Superman", 398, -1],
    ["Spider-Man", 514, -1],
    ["X-Men", 612, -1],
    ["Star Wars", 336, -1],
    ["Vertigo", 285, 0],
    ["Independent", 723, 5],
    ["Hellboy", 84, 3],
    ["Spawn", 190, -1],
    ["Graphic Novels", 640, 5],
  ];
  const visibleIndices =
    box === "DC Comics" || box === "Vertigo"
      ? [0, 1]
      : box === "Image Comics"
        ? [2, 4]
        : box === "Dark Horse Comics" || box === "Hellboy"
          ? [3, 5]
          : [0, 1, 2, 3, 4, 5];
  function render() {
    c.innerHTML =
      '<header class="archive-heading"><div class="eyebrow">' +
      (index ? "WESTERN COMICS · TANKOBAN" : "GETCOMICS · TANKOBAN") +
      "</div><h1>" +
      E(index ? box : "GetComics Archives") +
      "</h1>" +
      (index
        ? "<p><strong>" +
          (state === "empty" ? 0 : visibleIndices.length) +
          '</strong> series archives in the latest releases <span>·</span> <button data-action="all-releases">All ' +
          (P.count || 1924) +
          " releases ›</button></p>"
        : "<p>Publishers &amp; franchises — whole-archive releases (TPBs, omnibuses, runs)</p>") +
      '</header><div class="' +
      (index ? "archive-index-grid" : "archive-taxonomy") +
      '"></div>';
    if (index)
      c.querySelector("[data-action=all-releases]").onclick = () =>
        M.go("comics.releases", {
          title: box,
          view: "releases",
          tag: P.tag || box.toLowerCase().replace(/ /g, "-"),
        });
    if (state === "loading" || state === "empty") {
      c.querySelector(
        index ? ".archive-index-grid" : ".archive-taxonomy",
      ).innerHTML =
        '<div class="archive-state"><p>' +
        (state === "loading"
          ? index
            ? "Reading the archives…"
            : "Loading…"
          : "No series archives surfaced under this box.") +
        "</p>" +
        (state === "loading"
          ? '<button class="pill" data-action="finish-archive-loading">Finish fixture loading</button>'
          : "") +
        "</div>";
      c.querySelector("[data-action=finish-archive-loading]")?.addEventListener(
        "click",
        () => {
          state = "";
          render();
        },
      );
      return;
    }
    if (index) {
      titles.forEach((t, i) => {
        if (!visibleIndices.includes(i)) return;
        let b = document.createElement("button");
        b.className = "archive-index-card";
        b.dataset.action = "archive-series";
        b.innerHTML =
          '<img src="../assets/fixtures/captures/comic-covers/' +
          covers[i] +
          '.jpg" alt="' +
          t +
          '"><strong>' +
          t +
          "</strong><small>" +
          (28 + i * 11) +
          " releases</small>";
        b.onclick = () =>
          M.go("comics.releases", {
            title: t,
            view: "releases",
            tag: t.toLowerCase().replace(/ /g, "-"),
          });
        c.querySelector(".archive-index-grid").append(b);
      });
    } else
      boxes.forEach(([name, count, art], i) => {
        let b = document.createElement("button");
        b.className = "archive-box";
        b.dataset.action = "archive-box";
        b.dataset.box = name;
        b.style.backgroundImage =
          art < 0
            ? "linear-gradient(110deg,#444,#111)"
            : 'linear-gradient(#0007,#0007),url("../assets/fixtures/captures/comic-covers/' +
              covers[art] +
              '.jpg")';
        b.innerHTML =
          "<strong>" + name + "</strong><small>" + count + "</small>";
        b.onclick = () =>
          M.go("comics.archive", {
            view: "archive-index",
            box: name,
            count,
            tag: name.toLowerCase().replace(/ /g, "-"),
          });
        c.querySelector(".archive-taxonomy").append(b);
      });
  }
  render();
  c.addEventListener("keydown", (e) => {
    let list = [
        ...c.querySelectorAll(
          "[data-action=archive-box],[data-action=archive-series]",
        ),
      ],
      i = list.indexOf(document.activeElement),
      cols = index ? 6 : 5,
      d = {
        ArrowLeft: -1,
        ArrowRight: 1,
        ArrowUp: -cols,
        ArrowDown: cols,
        PageUp: -cols * 2,
        PageDown: cols * 2,
      }[e.key];
    if (i >= 0 && d !== undefined) {
      e.preventDefault();
      list[Math.max(0, Math.min(list.length - 1, i + d))].focus();
    }
  });
};
