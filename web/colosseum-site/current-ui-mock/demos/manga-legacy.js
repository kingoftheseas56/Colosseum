/* Conditional legacyWeebCentral route at 2916fe8. The old corridor is NOT mounted.
   Active descendants: MangaReadingRoomThumbnailMock/MangaTankobanLibraryThumbnailMock. */
(() => {
  "use strict";
  const M = MockUI,
    P = M.params || {},
    $ = (s) => document.querySelector(s),
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
  let title = P.title || "One Piece",
    chaptersOnly = !!P.chapterOnly,
    count = chaptersOnly ? 0 : 14,
    selecting = false,
    selected = [],
    dl = M.get("manga.legacy.downloads", {
      v1: "ready",
      v3: "downloading",
      c141: "ready",
    }),
    expanded = false;
  document.body.classList.add("legacy-current");
  $("#content").innerHTML =
    '<header class="legacy-mast"><div class="legacy-identity"><h1>' +
    E(title) +
    '</h1><div class="legacy-meta">ODA Eiichiro · Ongoing · 1997 <span>★ 9.1</span> · ' +
    count +
    " volumes · " +
    (count ? 146 : 24) +
    ' chapters</div><p class="legacy-synopsis">As a child, Monkey D. Luffy was inspired to become a pirate by listening to the tales of “Red-Haired” Shanks. Years later, Luffy sets out to find the legendary One Piece. This local fixture shows the legacy shelf and loose chapters after the last mapped volume.</p><button data-action="legacy-synopsis">more</button></div><div class="legacy-device"><div class="legacy-device-label">ON THIS DEVICE <strong>' +
    (count
      ? Object.entries(dl).filter(
          ([k, s]) => k.startsWith("v") && s === "ready",
        ).length
      : 0) +
    " OF " +
    count +
    '</strong></div><div class="legacy-progress"><i></i></div><div class="legacy-actions"><button class="pill gold" data-action="legacy-read">' +
    (count ? "Open volume 1" : "Read first chapter") +
    '</button><button class="pill" data-action="legacy-library">+ Library</button></div></div></header><section class="legacy-collection" aria-label="Legacy volume and chapter shelf"></section>';
  $(".legacy-progress i").style.width = count ? "7%" : "0%";
  $("#topbar .world").textContent = "COLOSSEUM · TANKOBAN";
  $("#topbar .world").style.cssText =
    "font-size:11px;letter-spacing:3px;color:#9a99a5";
  function b(el, action, label, fn) {
    let n = document.createElement("button");
    n.className = el.className;
    n.innerHTML = el.innerHTML;
    n.dataset.action = action;
    n.setAttribute("aria-label", label);
    el.replaceWith(n);
    n.onclick = fn;
  }
  b($("#topbar .back"), "back", "Back to series", () => M.back());
  [...document.querySelectorAll("#topbar .tools span")].forEach((e, i) =>
    b(
      e,
      ["minimize", "fullscreen", "close"][i],
      ["Minimize", "Toggle fullscreen", "Close series"][i],
      () =>
        i === 1
          ? document.documentElement.requestFullscreen?.().catch(() => {})
          : M.back(),
    ),
  );
  $("[data-action=legacy-synopsis]").onclick = (e) => {
    expanded = !expanded;
    document.body.classList.toggle("legacy-expanded", expanded);
    e.target.textContent = expanded ? "less" : "more";
  };
  $("[data-action=legacy-library]").onclick = (e) => {
    let saved = !M.get("collection." + (P.id || title), false);
    M.set("collection." + (P.id || title), saved);
    e.target.textContent = saved ? "✓ In Library" : "+ Library";
  };
  function read(n, k) {
    M.go("reader.comic", {
      title,
      entry: n,
      kind: k,
      mode: "manga",
      legacy: true,
    });
  }
  $("[data-action=legacy-read]").onclick = () =>
    read(1, count ? "volume" : "chapter");
  function save() {
    M.set("manga.legacy.downloads", dl);
  }
  function stage(ns) {
    ns.forEach((n) => (dl["v" + n] = "downloading"));
    save();
    selected = [];
    selecting = false;
    render();
  }
  function render() {
    const c = $(".legacy-collection");
    c.innerHTML =
      (count
        ? '<div class="legacy-shelf-head"><span>VOLUMES</span><div>' +
          (selecting
            ? '<button data-action="legacy-next10">Download next 10</button>'
            : "") +
          '<button data-action="legacy-select">' +
          (selecting ? "Done" : "Select") +
          '</button></div></div><div class="legacy-cover-grid"></div>'
        : "") +
      '<div class="legacy-tail"><div class="legacy-tail-head"><span>' +
      (count ? "LATEST CHAPTERS" : "CHAPTERS") +
      "</span><small>" +
      (count ? 6 : 24) +
      ' chapters</small></div><div class="legacy-tail-rows"></div></div>';
    if (count) {
      c.querySelector("[data-action=legacy-select]").onclick = () => {
        selecting = !selecting;
        selected = [];
        render();
      };
      c.querySelector("[data-action=legacy-next10]")?.addEventListener(
        "click",
        () =>
          stage(
            Array.from({ length: count }, (_, i) => i + 1)
              .filter((n) => !dl["v" + n])
              .slice(0, 10),
          ),
      );
      for (let n = 1; n <= count; n++) {
        let state = dl["v" + n] || "none",
          tile = document.createElement("button");
        tile.className =
          "legacy-volume" + (selected.includes(n) ? " selected" : "");
        tile.dataset.action = "legacy-volume";
        tile.dataset.volume = n;
        tile.setAttribute(
          "aria-label",
          (selecting ? "Select" : "Open") + " volume " + n,
        );
        tile.innerHTML =
          '<span class="legacy-cover">' +
          (n === 1
            ? '<img src="../assets/fixtures/captures/manga-covers/onepiece.jpg" alt="Series cover fixture">'
            : '<span class="legacy-no-cover"><b>' + n + "</b>NO COVER</span>") +
          '<span class="legacy-chip">' +
          ({ ready: "ON DEVICE", downloading: "DOWNLOADING", failed: "RETRY" }[
            state
          ] || "GET") +
          "</span>" +
          (state === "downloading"
            ? '<span class="legacy-disc">42%</span><span class="legacy-meter"></span>'
            : "") +
          '</span><span class="legacy-volume-number">VOL ' +
          n +
          '</span><span class="legacy-volume-name">' +
          (n === 1
            ? "Romance Dawn"
            : state === "downloading"
              ? "Downloading 42%"
              : "Volume " + n) +
          '</span><span class="legacy-volume-range">Ch ' +
          ((n - 1) * 10 + 1) +
          "–" +
          n * 10 +
          "</span>";
        tile.onclick = () => {
          if (selecting) {
            selected = selected.includes(n)
              ? selected.filter((x) => x !== n)
              : selected.concat(n);
            render();
          } else if (state === "ready") read(n, "volume");
          else
            M.go("manga.sources", {
              title,
              volume: n,
              legacy: true,
              intent: "read",
            });
        };
        c.querySelector(".legacy-cover-grid").append(tile);
      }
    }
    for (let n = count ? 141 : 1; n <= (count ? 146 : 24); n++) {
      let state = dl["c" + n] || "none",
        row = document.createElement("div");
      row.className = "legacy-chapter";
      row.innerHTML =
        '<button class="legacy-chapter-main" data-action="legacy-chapter" data-chapter="' +
        n +
        '"><span class="legacy-chapter-thumb">' +
        n +
        "</span><span><b>Chapter " +
        n +
        "</b><small>" +
        ({
          ready: "On this device",
          downloading: "Downloading 42%",
          failed: "Failed · Retry",
        }[state] || "Available") +
        '</small></span></button><span class="legacy-chapter-state">' +
        ({ ready: "ON DEVICE", downloading: "42%", failed: "RETRY" }[state] ||
          "") +
        '</span><button class="legacy-chapter-utility" data-action="legacy-chapter-download" aria-label="' +
        (state === "ready"
          ? "Read"
          : state === "downloading"
            ? "Cancel"
            : "Download") +
        " chapter " +
        n +
        '">' +
        ({ ready: "✓", downloading: "×", failed: "↻" }[state] || "↓") +
        "</button>";
      row.querySelector(".legacy-chapter-main").onclick = () => {
        if (state === "ready") read(n, "chapter");
        else if (state !== "downloading") {
          dl["c" + n] = "downloading";
          save();
          render();
        }
      };
      row.querySelector(".legacy-chapter-utility").onclick = () => {
        if (state === "ready") read(n, "chapter");
        else {
          if (state === "downloading") delete dl["c" + n];
          else dl["c" + n] = "downloading";
          save();
          render();
        }
      };
      c.querySelector(".legacy-tail-rows").append(row);
    }
    if (selected.length) {
      let s = document.createElement("div");
      s.className = "selected-bar";
      s.innerHTML =
        selected.length +
        ' selected <button class="pill gold" data-action="legacy-download-selected">Download selected</button>';
      s.querySelector("button").onclick = () => stage(selected);
      c.append(s);
    }
    c.insertAdjacentHTML(
      "beforeend",
      '<p class="legacy-fixture">Representative legacy fixture: ' +
        count +
        " mapped volumes and " +
        (count ? 6 : 24) +
        " chapters. Missing cover art uses the source’s NO COVER treatment.</p>",
    );
  }
  $(".legacy-collection").addEventListener("keydown", (e) => {
    const items = [...document.querySelectorAll("[data-action=legacy-volume]")],
      i = items.indexOf(document.activeElement),
      cols = Math.max(
        1,
        Math.floor(($(".legacy-collection").clientWidth + 14) / 164),
      ),
      delta = {
        ArrowLeft: -1,
        ArrowRight: 1,
        ArrowUp: -cols,
        ArrowDown: cols,
        PageUp: -cols * 2,
        PageDown: cols * 2,
      }[e.key];
    if (i >= 0 && delta !== undefined) {
      e.preventDefault();
      items[Math.max(0, Math.min(items.length - 1, i + delta))].focus();
    }
  });
  render();
  M.ready();
})();
