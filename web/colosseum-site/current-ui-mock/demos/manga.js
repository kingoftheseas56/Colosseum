/* Current source: MangaSeries → MangaReadingRoom/MangaChapterSeriesView, pin 2916fe8.
   Existing baseline hero/story/shelf markup is retained; fixed story loop replaced by real state. */
(() => {
  "use strict";
  const M = window.MockUI,
    P = M.params || {},
    $ = (s) => document.querySelector(s),
    $$ = (s) => [...document.querySelectorAll(s)],
    esc = (s) =>
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
  if (P.legacy) {
    const script = document.createElement("script");
    script.src = "manga-legacy.js";
    document.body.append(script);
    return;
  }
  let mode =
      (["chapters", "volumes"].includes(P.mode)
        ? P.mode
        : M.get("manga.mode." + (P.title || "One Piece"), "volumes")) ===
        "chapters" ||
      P.chapterOnly ||
      P.legacy
        ? "chapters"
        : "volumes",
    rebuilt = !!P.rebuilt,
    lang = P.language || M.get("manga.language", "en"),
    page = Number(P.page) || 1,
    focus = Math.max(
      1,
      Math.min(115, Number(P.volume) || M.get("manga.volume", 1)),
    ),
    selecting = false,
    selected = [],
    overlayDispose = null,
    restoreFocus = null;
  const names = [
    "Romance Dawn",
    "Buggy the Clown",
    "Don’t Get Fooled Again",
    "The Black Cat Pirates",
    "For Whom the Bell Tolls",
    "The Oath",
    "The Crap-Geezer",
    "I Won’t Die",
    "Tears",
    "OK, Let’s Stand Up!",
  ];
  let dl = M.get("manga.downloads", { v1: "ready" }),
    fixture = P.state || "",
    title = P.title || "One Piece";
  if (P.cover && P.cover.startsWith("../assets/")) $("#banner").src = P.cover;
  document.body.classList.toggle("legacy", !!P.legacy);
  const titleNode = $("h1");
  titleNode.textContent = title;
  const lib = document.createElement("button");
  lib.className = "library-icon";
  lib.dataset.action = "library";
  titleNode.append(lib);
  function library() {
    let saved = M.get("collection." + (P.id || "onepiece"), false);
    lib.classList.toggle("saved", saved);
    lib.setAttribute(
      "aria-label",
      saved ? "Remove from Library" : "Add to Library",
    );
    lib.innerHTML =
      '<img src="../assets/icons/lucide/bookmark-' +
      (saved ? "check" : "plus") +
      '.svg" alt="">';
  }
  lib.onclick = () => {
    M.set(
      "collection." + (P.id || "onepiece"),
      !M.get("collection." + (P.id || "onepiece"), false),
    );
    library();
  };
  library();
  $(".mode-switch").innerHTML =
    '<button data-action="volumes">Tankoban Mode</button><button data-action="chapters">Chapter Mode</button>';
  $(".language").innerHTML =
    '<label for="language">Language</label><select id="language" data-action="language" aria-label="Chapter language"><option value="en">English</option><option value="es">Español</option><option value="ja">日本語</option></select>';
  $("#language").value = lang;
  $("#language").onchange = (e) => {
    lang = e.target.value;
    M.set("manga.language", lang);
    page = 1;
    fixture = "";
    render();
  };
  $$(".mode-switch button").forEach(
    (b) =>
      (b.onclick = () => {
        mode = b.dataset.action;
        M.set("manga.mode." + title, mode);
        page = 1;
        fixture = "";
        render();
      }),
  );
  if (P.chapterOnly || P.legacy) $(".mode-switch").hidden = true;
  if (P.legacy) {
    $(".language").hidden = true;
    $(".hero-actions").innerHTML =
      '<button class="action primary" data-action="legacy-read">Read</button>';
    $(".hero-actions button").onclick = () => read(1, "chapter");
  }
  document.body.classList.toggle("rebuilt", rebuilt);
  if (rebuilt) {
    $(".eyebrow").textContent = "MANGA · TANKOBAN";
    $(".hero").insertAdjacentHTML(
      "beforeend",
      '<img class="rebuilt-cover" src="../assets/fixtures/captures/manga-covers/onepiece.jpg" alt="Series cover"><div class="rebuilt-genres"><span>Action</span><span>Adventure</span><span>Comedy</span></div>',
    );
  }
  $("#topbar .back").outerHTML =
    '<button class="back" data-action="back">‹ Back</button>';
  $("#topbar .back").onclick = () => M.back();
  $$("#topbar .tools span").forEach((el, i) => {
    let b = document.createElement("button");
    b.innerHTML = el.innerHTML;
    b.setAttribute(
      "aria-label",
      ["Minimize", "Toggle fullscreen", "Close series"][i],
    );
    b.dataset.action = ["minimize", "fullscreen", "close"][i];
    el.replaceWith(b);
    b.onclick = () =>
      i === 1
        ? document.documentElement.requestFullscreen?.().catch(() => {})
        : M.back();
  });
  if (rebuilt) {
    $("#topbar .tools").innerHTML =
      '<button data-action="search" aria-label="Search"><img src="../assets/icons/search.svg" alt=""></button><button data-action="window-menu" aria-label="Window and app">•••</button>';
    $("#topbar [data-action=search]").onclick = () => M.go("search.tankoban");
    $("#topbar [data-action=window-menu]").onclick = windowMenu;
  }
  if (rebuilt) {
    let more = document.createElement("button");
    more.className = "synopsis-more";
    more.textContent = "More";
    more.dataset.action = "more-synopsis";
    more.onclick = () => {
      let expanded = document.body.classList.toggle("synopsis-expanded");
      more.textContent = expanded ? "Less" : "More";
    };
    $(".story").append(more);
  }
  const chapterBody = document.createElement("section");
  chapterBody.className = "chapter-body";
  chapterBody.setAttribute("aria-label", "Chapters");
  $("#content").append(chapterBody);

  function windowMenu() {
    closeDialog();
    restoreFocus = document.activeElement;
    let o = document.createElement("div");
    o.className = "overlay";
    o.style.background = "transparent";
    o.innerHTML =
      '<section class="dialog window-menu" role="dialog" aria-label="Window and app"><button data-action="menu-minimize">Minimize</button><button data-action="menu-fullscreen">Fullscreen</button><button data-action="menu-quit">Quit Colosseum…</button><button data-action="menu-cancel">Cancel</button></section>';
    document.body.append(o);
    overlayDispose = M.registerOverlay(o, closeDialog);
    o.querySelector("[data-action=menu-minimize]").onclick = () => {
      closeDialog();
      M.back();
    };
    o.querySelector("[data-action=menu-fullscreen]").onclick = () => {
      closeDialog();
      document.documentElement.requestFullscreen?.().catch(() => {});
    };
    o.querySelector("[data-action=menu-quit]").onclick = () =>
      dialog("Quit Colosseum?", "Close this mock title view.", "Quit", () =>
        M.go("home"),
      );
    o.querySelector("[data-action=menu-cancel]").onclick = closeDialog;
  }

  function save() {
    M.set("manga.downloads", dl);
  }
  function dialog(heading, body, ok, action) {
    closeDialog();
    restoreFocus = document.activeElement;
    const o = document.createElement("div");
    o.className = "overlay";
    o.innerHTML =
      '<div class="dialog" role="dialog" aria-modal="true" aria-label="' +
      esc(heading) +
      '"><h2>' +
      esc(heading) +
      "</h2><p>" +
      esc(body) +
      '</p><div class="buttons"><button class="pill" data-action="cancel">Cancel</button><button class="pill gold" data-action="confirm">' +
      esc(ok) +
      "</button></div></div>";
    document.body.append(o);
    overlayDispose = M.registerOverlay(o, closeDialog);
    o.querySelector("[data-action=cancel]").onclick = closeDialog;
    o.querySelector("[data-action=confirm]").onclick = () => {
      closeDialog();
      action();
    };
    o.querySelector("button").focus();
    o.addEventListener("keydown", (e) => {
      if (e.key === "Tab") {
        let a = [...o.querySelectorAll("button")];
        if (e.shiftKey && document.activeElement === a[0]) {
          e.preventDefault();
          a[1].focus();
        } else if (!e.shiftKey && document.activeElement === a[1]) {
          e.preventDefault();
          a[0].focus();
        }
      }
    });
  }
  function closeDialog() {
    document.querySelector(".overlay")?.remove();
    overlayDispose?.();
    overlayDispose = null;
    restoreFocus?.focus();
  }
  function read(n, kind = "volume") {
    M.set("manga.mode." + title, mode);
    M.set("manga.volume", focus);
    const params = {
      title,
      entry: n,
      kind,
      language: lang,
      returnMode: mode,
      rebuilt,
      mode: "manga",
    };
    M.openSession({
      kind: "reader",
      id: "manga-" + kind + "-" + n,
      title: title + " · " + (kind === "chapter" ? "Chapter " : "Vol. ") + n,
      route: "reader.comic",
      params,
    });
    M.go("reader.comic", params);
  }
  function sources(n, intent = "acquire") {
    M.set("manga.volume", n);
    M.go("manga.sources", { volume: n, title, intent, rebuilt });
  }
  function batch(ns) {
    dialog(
      "Download " + ns.length + " volumes",
      "This stages the selected volumes in the local browser mock. No files are acquired.",
      "Stage downloads",
      () => {
        ns.forEach((n) => (dl["v" + n] = "downloading"));
        save();
        selecting = false;
        selected = [];
        render();
      },
    );
  }
  function rememberSubview() {
    M.updateParams?.({ mode, language: lang, page, volume: focus });
  }
  function render() {
    rememberSubview();
    let chapters = mode === "chapters";
    if (rebuilt) $(".language").hidden = !chapters;
    $(".shelf").hidden = chapters;
    chapterBody.hidden = !chapters;
    $$(".mode-switch button").forEach((b) =>
      b.classList.toggle("active", b.dataset.action === mode),
    );
    $("#language").disabled = !chapters;
    $("#language").title = chapters
      ? "Chapter language"
      : "Available in Chapter Mode";
    $("#language").value = chapters ? lang : "en";
    if (chapters) renderChapters();
    else renderVolumes();
  }
  function renderVolumes() {
    rememberSubview();
    let s = $(".shelf");
    s.querySelector(".heading").innerHTML =
      '<h2>Volumes</h2><span class="desc">115</span><div class="batch">' +
      (selecting
        ? '<button data-action="download-next">Download next 10</button>'
        : "") +
      '<button data-action="select">' +
      (selecting ? "Done" : "Select") +
      "</button></div>";
    s.querySelector("[data-action=select]").onclick = () => {
      selecting = !selecting;
      selected = [];
      renderVolumes();
    };
    s.querySelector("[data-action=download-next]")?.addEventListener(
      "click",
      () =>
        batch(
          Array.from({ length: 115 }, (_, i) => i + 1)
            .filter((n) => !dl["v" + n])
            .slice(0, 10),
        ),
    );
    const flow = $("#flow");
    flow.innerHTML = "";
    let width = innerWidth;
    for (let n = Math.max(1, focus - 5); n <= Math.min(115, focus + 5); n++) {
      let d = Math.abs(n - focus),
        scale =
          d === 0 ? 1.1 : d === 1 ? 1 : d === 2 ? 0.95 : d === 3 ? 0.9 : 0.86;
      let b = document.createElement("button");
      b.className =
        "volume" +
        (n === focus ? " current" : "") +
        (selected.includes(n) ? " selected" : "");
      b.dataset.action = "volume";
      b.dataset.volume = n;
      b.setAttribute(
        "aria-label",
        (selecting ? "Select " : "Open ") + "volume " + n,
      );
      b.style.transform = `translateX(${width / 2 - 92.5 + (n - focus) * 203}px) scale(${scale})`;
      b.style.opacity =
        n === focus ? "1" : String(Math.max(0.34, 1 - 0.075 * d));
      b.style.zIndex = 20 - d;
      let state = dl["v" + n] || "available";
      b.innerHTML =
        '<span class="cover"><span class="fallback"><b>' +
        n +
        "</b><i>NO COVER</i></span>" +
        (n === 1
          ? '<img src="../assets/fixtures/captures/manga-covers/onepiece.jpg" alt="One Piece series cover fixture">'
          : "") +
        (state === "ready" ? '<span class="owned"></span>' : "") +
        (selected.includes(n) ? '<span class="selected-tick">✓</span>' : "") +
        '</span><span class="caption"><span class="num">VOL ' +
        n +
        '</span><span class="name" style="display:block">' +
        esc(names[n - 1] || "") +
        '</span><span class="state">' +
        (state === "downloading"
          ? "42% | downloading"
          : state === "failed"
            ? "failed"
            : "") +
        "</span></span>";
      b.onclick = () => {
        if (selecting) {
          selected = selected.includes(n)
            ? selected.filter((x) => x !== n)
            : selected.concat(n);
          renderVolumes();
        } else if (n !== focus) {
          focus = n;
          M.set("manga.volume", focus);
          renderVolumes();
        } else if (state === "ready") read(n);
        else sources(n, "read");
      };
      flow.append(b);
    }
    for (let [label, delta, c] of [
      ["Previous volume", -1, "prev"],
      ["Next volume", 1, "next"],
    ]) {
      let b = document.createElement("button");
      b.className = "navvol " + c;
      b.textContent = c === "prev" ? "‹" : "›";
      b.dataset.action = c + "-volume";
      b.setAttribute("aria-label", label);
      b.disabled = focus + delta < 1 || focus + delta > 115;
      b.onclick = () => {
        focus += delta;
        renderVolumes();
      };
      flow.append(b);
    }
    let state = dl["v" + focus] || "available";
    $("#volName").innerHTML =
      "Vol. " + focus + " <small>" + esc(names[focus - 1] || "") + "</small>";
    s.querySelector(".controls").innerHTML =
      '<button class="control download" data-action="download" ' +
      (state === "ready" || state === "downloading" ? "disabled" : "") +
      ">Download</button>" +
      (state === "downloading"
        ? '<button class="control download" data-action="cancel-download">Cancel</button>'
        : "") +
      '<button class="control read" data-action="read-volume">' +
      (state === "failed"
        ? "Retry Read"
        : state === "downloading"
          ? "Reading when ready"
          : (focus === 1 && M.get("reader.resume.manga-volume-1", 0) > 1
              ? "Continue Vol. "
              : "Read Vol. ") + focus) +
      "</button>";
    s.querySelector("[data-action=download]").onclick = () => sources(focus);
    s.querySelector("[data-action=read-volume]").onclick = () =>
      state === "ready" ? read(focus) : sources(focus, "read");
    s.querySelector("[data-action=cancel-download]")?.addEventListener(
      "click",
      () => {
        delete dl["v" + focus];
        save();
        renderVolumes();
      },
    );
    s.querySelector(".selected-bar")?.remove();
    if (selecting && selected.length) {
      let bar = document.createElement("div");
      bar.className = "selected-bar";
      bar.innerHTML =
        selected.length +
        ' selected <button class="pill gold" data-action="download-selected">Download selected</button>';
      bar.querySelector("button").onclick = () => batch(selected.slice());
      s.append(bar);
    }
    if (fixture === "failed") {
      dl["v" + focus] = "failed";
      fixture = "";
      renderVolumes();
    }
    if (fixture === "downloading") {
      dl["v" + focus] = "downloading";
      fixture = "";
      renderVolumes();
    }
  }
  function renderChapters() {
    rememberSubview();
    let count = lang === "ja" ? 0 : lang === "es" ? 30 : P.legacy ? 24 : 1190,
      pages = Math.ceil(count / 10),
      start = (page - 1) * 10 + 1,
      end = Math.min(start + 9, count);
    chapterBody.innerHTML =
      '<div class="chapter-head"><span class="label">CHAPTERS ' +
      count +
      "</span>" +
      (rebuilt
        ? '<button class="pill" data-action="prev-page" ' +
          (page <= 1 ? "disabled" : "") +
          ">‹</button>"
        : "") +
      '<select aria-label="Chapter page selector" data-action="chapter-page" ' +
      (!pages ? "disabled" : "") +
      ">" +
      Array.from(
        { length: pages },
        (_, i) =>
          '<option value="' +
          (i + 1) +
          '" ' +
          (page === i + 1 ? "selected" : "") +
          ">Page " +
          (i + 1) +
          (rebuilt ? " of " + pages : "") +
          "</option>",
      ).join("") +
      "</select>" +
      (rebuilt
        ? '<button class="pill" data-action="next-page" ' +
          (page >= pages ? "disabled" : "") +
          '>›</button><input class="jump" type="number" min="1" max="' +
          count +
          '" aria-label="Go to chapter" placeholder="Go to chapter">'
        : "") +
      '</div><div class="chapter-ledger"></div>';
    chapterBody.querySelector("select").onchange = (e) => {
      page = Number(e.target.value);
      renderChapters();
      chapterBody.scrollTop = 0;
    };
    chapterBody
      .querySelector("[data-action=prev-page]")
      ?.addEventListener("click", () => {
        page--;
        renderChapters();
      });
    chapterBody
      .querySelector("[data-action=next-page]")
      ?.addEventListener("click", () => {
        page++;
        renderChapters();
      });
    chapterBody.querySelector(".jump")?.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        page = Math.max(
          1,
          Math.min(pages, Math.ceil(Number(e.target.value) / 10)),
        );
        renderChapters();
      }
    });
    let ledger = chapterBody.querySelector(".chapter-ledger");
    if (P.legacy) {
      chapterBody.querySelector(".chapter-head").hidden = true;
      start = 1;
      end = 24;
    }
    if (
      fixture === "loading" ||
      fixture === "error" ||
      fixture === "empty" ||
      fixture === "unavailable" ||
      !count
    ) {
      ledger.innerHTML =
        '<div class="statebox"><p>' +
        ({
          loading: "Loading chapters...",
          error: "Could not load chapters for this language.",
          empty: "No chapters available",
          unavailable: "Tankoyomi is not enabled.",
        }[fixture] || "No chapters available in 日本語") +
        '</p><button class="pill" data-action="' +
        (fixture === "unavailable" ? "extensions" : "retry") +
        '">' +
        (fixture === "unavailable"
          ? "Open Extensions"
          : fixture === "loading"
            ? "Finish fixture loading"
            : "Retry") +
        "</button></div>";
      ledger.querySelector("button").onclick = () => {
        if (fixture === "unavailable") M.go("extensions");
        else {
          fixture = "";
          renderChapters();
        }
      };
      return;
    }
    ledger.innerHTML =
      '<div class="ledger-head"><h2>Chapters ' +
      start +
      "–" +
      end +
      "</h2><small>" +
      (end - start + 1) +
      ' chapters</small><button class="pill" data-action="download-page">↓ Download page</button></div>';
    for (let n = start; n <= end; n++) {
      let key = "c" + lang + n,
        state = dl[key] || "available";
      if (fixture === "chapter-failed" && n === start) {
        state = "failed";
        dl[key] = "failed";
        fixture = "";
      }
      if (fixture === "chapter-downloading" && n === start) {
        state = "downloading";
        dl[key] = "downloading";
        fixture = "";
      }
      let row = document.createElement("div");
      row.className = "chapter-row";
      row.innerHTML =
        '<button class="chapter-open" data-action="read-chapter" data-chapter="' +
        n +
        '"><span class="chapter-no">' +
        n +
        '</span><span class="chapter-thumb">' +
        n +
        '</span><span class="chapter-info"><b>' +
        (lang === "es" ? "Capítulo " : "Chapter ") +
        n +
        "</b><small>" +
        ({
          ready: "Downloaded",
          downloading: "Downloading 42%",
          failed: "Failed · Retry",
        }[state] || "Available") +
        '</small></span></button><button class="chapter-download" data-action="chapter-download" aria-label="' +
        ({
          ready: "Delete download of",
          downloading: "Cancel download of",
          failed: "Retry",
        }[state] || "Download") +
        " chapter " +
        n +
        '">' +
        ({ ready: "✓", downloading: "✕", failed: "↻" }[state] || "↓") +
        "</button>";
      row.querySelector(".chapter-open").onclick = () => read(n, "chapter");
      row.querySelector(".chapter-download").onclick = () => {
        if (state === "ready")
          dialog(
            "Delete chapter download?",
            "Remove this staged local chapter. You can download it again.",
            "Delete download",
            () => {
              delete dl[key];
              save();
              renderChapters();
            },
          );
        else {
          if (state === "downloading") delete dl[key];
          else dl[key] = "ready";
          save();
          renderChapters();
        }
      };
      ledger.append(row);
    }
    if (rebuilt) {
      let b = ledger.querySelector("[data-action=download-page]");
      chapterBody.querySelector(".chapter-head").append(b);
      chapterBody.querySelector(".label").textContent =
        count + " chapters · " + start + "–" + end;
    }
    chapterBody.querySelector("[data-action=download-page]").onclick = () => {
      for (let n = start; n <= end; n++) dl["c" + lang + n] = "ready";
      save();
      renderChapters();
    };
  }
  window.addEventListener("keydown", (e) => {
    if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName) || $(".overlay")) return;
    if (
      mode === "volumes" &&
      ["ArrowLeft", "ArrowRight", "PageDown", "PageUp"].includes(e.key)
    ) {
      e.preventDefault();
      focus = Math.max(
        1,
        Math.min(
          115,
          focus +
            { ArrowLeft: -1, ArrowRight: 1, PageDown: 10, PageUp: -10 }[e.key],
        ),
      );
      renderVolumes();
    }
    if (
      mode === "chapters" &&
      rebuilt &&
      ["PageDown", "PageUp"].includes(e.key)
    ) {
      e.preventDefault();
      page = Math.max(
        1,
        Math.min(
          lang === "es" ? 3 : 119,
          page + (e.key === "PageDown" ? 1 : -1),
        ),
      );
      renderChapters();
    }
  });
  window.addEventListener(
    "resize",
    () => mode === "volumes" && renderVolumes(),
  );
  render();
  M.ready();
})();
