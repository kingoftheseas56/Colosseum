(() => {
  "use strict";
  const M = window.MockUI,
    $ = (s) => document.querySelector(s),
    $$ = (s) => [...document.querySelectorAll(s)],
    P = M.params || {};
  const bookId = P.id || (P.book === "phm" ? "biblio-1" : "biblio-0"),
    bookTitle =
      P.title || (bookId === "biblio-1" ? "Project Hail Mary" : "Dune");
  const esc = (s) =>
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
  const btn = (text, fn, cls = "") => {
    let b = document.createElement("button");
    b.textContent = text;
    b.className = cls;
    b.onclick = fn;
    return b;
  };
  function activate(el, name, fn) {
    let b = document.createElement("button");
    b.className = el.className;
    b.id = el.id;
    b.innerHTML = el.innerHTML;
    b.setAttribute("aria-label", name);
    b.title = name;
    b.dataset.action = name.toLowerCase().replaceAll(" ", "-");
    b.onclick = fn;
    el.replaceWith(b);
    return b;
  }
  let dismiss = null;
  function dialog(title, html, setup) {
    if (dismiss) dismiss();
    const focus = document.activeElement,
      e = document.createElement("div");
    e.className = "book-overlay";
    e.innerHTML = `<section class="book-dialog" role="dialog" aria-modal="true" aria-label="${esc(title)}"><h2>${esc(title)}</h2>${html}<button data-action="cancel">Cancel</button></section>`;
    document.body.append(e);
    let dispose;
    dismiss = () => {
      e.remove();
      if (dispose) dispose();
      dismiss = null;
      focus?.focus();
    };
    dispose = M.registerOverlay(e, dismiss);
    e.querySelector("[data-action=cancel]").onclick = dismiss;
    e.addEventListener("click", (ev) => {
      if (ev.target === e) dismiss();
    });
    e.addEventListener("keydown", (ev) => {
      if (ev.key === "Tab") {
        let a = [...e.querySelectorAll("button,input,select,textarea")].filter(
          (x) => !x.disabled,
        );
        if (ev.shiftKey && document.activeElement === a[0]) {
          a.at(-1).focus();
          ev.preventDefault();
        } else if (!ev.shiftKey && document.activeElement === a.at(-1)) {
          a[0].focus();
          ev.preventDefault();
        }
      }
    });
    setup?.(e);
    e.querySelector("button,input")?.focus();
    return e;
  }
  function openReader(audio = false) {
    M.openSession({
      kind: audio ? "audiobook" : "book",
      id: audio ? "biblio-audio" : "biblio-book",
      title: bookTitle,
      route: audio ? "reader.audio" : "reader.book",
      params: { id: bookId, title: bookTitle },
    });
    M.go(audio ? "reader.audio" : "reader.book", {
      id: bookId,
      title: bookTitle,
    });
  }
  if ($(".cover-column")) {
    if (P.book === "phm" || bookId === "biblio-1") {
      document.querySelector("h1").textContent = "Project Hail Mary";
      document.querySelector("#cover").src =
        "../assets/feria/assets/phm-cover.jpg";
      document.querySelector("#cover").alt = "Project Hail Mary cover";
      document.querySelector(".eyebrow").textContent =
        "SCIENCE FICTION · ANDY WEIR · 2021";
      document.querySelector(".synopsis").textContent =
        "A lone astronaut must solve a mystery that threatens Earth. Andy Weir’s science-fiction novel combines a survival story with scientific problem-solving.";
    }
    document.title = document.querySelector("h1").textContent + " · Biblio";
    let state = P.state || "ready";
    $(".read-status").textContent = "Read will choose a tracked edition";
    activate($(".back"), "Back", () => M.back());
    activate($(".read"), "Read", choose);
    const library = activate($(".library"), "Collection", () => {
      M.set("collection." + bookId, !M.get("collection." + bookId, false));
      drawCollection();
    });
    function drawCollection() {
      library.textContent = M.get("collection." + bookId, false)
        ? "✓ In Collection"
        : "+ Collection";
    }
    drawCollection();
    const notice = document.createElement("p");
    notice.className = "book-ui-notice";
    notice.textContent =
      "Interactive fixture · downloads and playback are simulated. Reader text uses a public-domain sample.";
    $(".actions").append(notice);
    const torrents = document.createElement("div");
    torrents.className = "section";
    torrents.id = "torrents";
    torrents.innerHTML =
      '<div class="section-label">TORRENTS · 4</div><div class="panel"></div><button data-action="see-more">See 2 more</button>';
    $("#editions").before(torrents);
    const torRows = [
      bookTitle + " · EPUB",
      bookTitle + " · English · Collected edition",
      bookTitle + " · PDF · Illustrated edition",
      bookTitle + " · AZW3",
    ];
    torRows.forEach((name, i) => {
      let b = btn("", () => acquire(b, "torrent" + i));
      b.className = "focus-row";
      b.innerHTML = `<span class="metadata">${name}<small style="display:block;color:#9a99a5">▲ ${[122, 85, 34, 12][i]} &nbsp; ${[3.4, 2.8, 12, 3.1][i]} MB · sample indexer result</small></span><span class="arrow">↓</span>`;
      b.dataset.action = "torrent-" + i;
      if (i > 1) b.hidden = true;
      torrents.querySelector(".panel").append(b);
    });
    torrents.querySelector("button[data-action]").onclick = (e) => {
      let all = e.target.textContent === "See 2 more";
      [...torrents.querySelectorAll(".focus-row")]
        .slice(2)
        .forEach((b) => (b.hidden = !all));
      e.target.textContent = all ? "See less" : "See 2 more";
    };
    $$(".focus-row[id]").forEach((el, i) => {
      const audio = i > 2;
      let b = activate(
        el,
        (audio ? "Audiobook" : "Edition") + " " + (i + 1),
        () => acquire(b, el.id, audio),
      );
    });
    function acquire(b, id, audio = false) {
      let current = M.get("biblio.download." + id, "");
      if (current === "done") {
        dialog(
          audio ? "Audiobook ready" : "Edition ready",
          `<p>${audio ? "This audiobook is ready in the staged library." : "This edition is ready in the staged library. Explicit edition acquisition does not automatically start reading."}</p><button class="gold" data-action="open-ready">${audio ? "Listen" : "Read"}</button>`,
          (e) =>
            (e.querySelector("[data-action=open-ready]").onclick = () =>
              openReader(audio)),
        );
        return;
      }
      let a = b.querySelector(".arrow");
      a.textContent = "0%";
      b.disabled = true;
      setTimeout(() => {
        b.disabled = false;
        if (state === "error") {
          a.textContent = "Retry";
          b.title = "Sample download failed. Retry";
          state = "ready";
        } else {
          a.textContent = "✓";
          M.set("biblio.download." + id, "done");
          if (audio) M.set("biblio.audio.ready", true);
        }
      }, 650);
    }
    function choose() {
      dialog(
        "Choose an edition",
        "<p>Read will continue into Reader2 when this exact edition is ready.</p>" +
          [
            "EPUB · 1.8 MB · English",
            "PDF · 2.5 MB · English",
            "AZW3 · 2.1 MB · English",
          ]
            .map(
              (x, i) =>
                `<button class="choice" data-action="choose-${i}"><span>${x}<small>Tracked sample edition</small></span><b>Read</b></button>`,
            )
            .join(""),
        (e) =>
          e.querySelectorAll(".choice").forEach(
            (b) =>
              (b.onclick = () => {
                b.disabled = true;
                b.lastElementChild.textContent = "Preparing…";
                setTimeout(() => openReader(), 350);
              }),
          ),
      );
    }
    if (state === "empty" || state === "loading") {
      $$(".section .panel").forEach(
        (el) =>
          (el.innerHTML = `<div class="state-line">${state === "empty" ? "No results found" : "Searching…"} <button data-action="retry">Retry</button></div>`),
      );
      $("[data-action=see-more]").hidden = true;
      $$(".section-label").forEach(
        (el) =>
          (el.textContent =
            el.textContent.split("·")[0] +
            " · " +
            (state === "empty" ? "0" : "SEARCHING…")),
      );
      $("[data-action=read]").disabled = true;
      $(".read-status").textContent =
        state === "empty"
          ? "No tracked edition available"
          : "Searching for editions…";
    }
    $$("[data-action=retry]").forEach(
      (b) => (b.onclick = () => M.go("book.detail", {})),
    );
    if (P.section === "editions") $("#editions").scrollIntoView();
    if (P.section === "audio") $("#audiobook").scrollIntoView();
    if (P.modal === "editions") choose();
    M.ready();
    return;
  }
  if (!$(".reader")) return;
  // Preserve the baseline paper, prose, chrome and exact 348px appearance column.
  const reader = $(".reader"),
    article = $("#p1 .article");
  article.querySelector(".prose p:last-child")?.remove();
  const original = article.innerHTML,
    second = $("#p2 .article").innerHTML;
  let page = Number(P.page || M.get("biblio.page", 1)),
    panel = null,
    disposePanel = null,
    lastFocus = null;
  let marks = M.get("biblio.bookmarks", []),
    highlights = M.get("biblio.highlights", []);
  let opts = Object.assign(
    {
      theme: "Paper",
      font: "Literata",
      size: 100,
      weight: 400,
      line: 1.6,
      word: 0,
      letter: 0,
      paragraph: 0,
      indent: "Book",
      margin: 72,
      justify: "Justified",
      flow: "Pages",
      width: 960,
      hyphens: true,
      columns: "Single",
      ruler: false,
      band: 96,
      dim: 40,
      y: 50,
      invert: false,
      text: "#3a362c",
      background: "#e9e4d8",
      css: "",
    },
    M.get("biblio.appearance", {}),
  );
  let audio = Object.assign(
    {
      playing: false,
      position: 150,
      index: 0,
      speed: 1,
      volume: 70,
      mute: false,
      sleep: 0,
      follow: false,
      mode: "Sentence",
      scale: 1,
      sync: "Ready",
      paused: false,
    },
    M.get("biblio.audio", {}),
  );
  const audioOnly = location.pathname.includes("audio-reader");
  $("#panel").innerHTML = "";
  $("#panel").classList.remove("open");
  $("#returnChip").style.display = "none";
  const left = document.createElement("aside");
  left.className = "panel left-panel";
  left.setAttribute("aria-label", "Reading panel");
  reader.append(left);
  const ruler = document.createElement("div");
  ruler.className = "ruler";
  reader.append(ruler);
  const sample = document.createElement("div");
  sample.className = "sample-label";
  sample.textContent =
    "Public-domain reading sample · Jane Austen, Pride and Prejudice (1813)";
  reader.append(sample);
  const mini = document.createElement("div");
  mini.className = "mini-audio";
  reader.append(mini);
  const chromeBtns = $$(".topright .iconbtn");
  [
    "Search",
    "Contents",
    "Appearance",
    "Bookmark",
    "Minimize",
    "Fullscreen",
    "Close",
  ].forEach((n, i) =>
    activate(chromeBtns[i], n, () =>
      ({
        Search: search,
        Contents: () => leftPanel("contents"),
        Appearance: appearance,
        Bookmark: bookmark,
        Minimize: () => M.back(),
        Fullscreen: () => {
          if (!document.fullscreenElement)
            document.documentElement.requestFullscreen?.().catch(() => {});
          else document.exitFullscreen?.();
        },
        Close: closeReader,
      })[n](),
    ),
  );
  activate($(".back"), "Back", () => M.back());
  activate($("#leftTurn"), "Previous page", () => turn(-1));
  activate($("#rightTurn"), "Next page", () => turn(1));
  $(".railrow").innerHTML =
    '<input aria-label="Book progress" data-action="progress" type="range" min="1" max="8" value="1">';
  $(".railrow input").oninput = (e) => {
    page = +e.target.value;
    renderPage();
  };
  function closePanel() {
    if (panel) panel.classList.remove("open");
    panel = null;
    disposePanel?.();
    disposePanel = null;
    lastFocus?.focus();
  }
  function openPanel(el) {
    closePanel();
    lastFocus = document.activeElement;
    panel = el;
    el.classList.add("open");
    disposePanel = M.registerOverlay(el, closePanel);
    el.querySelector("button,input")?.focus();
  }
  function header(title) {
    return `<button class="close-panel" aria-label="Close ${title}" data-action="close-panel">×</button><h2>${title}</h2>`;
  }
  function bindClose(el) {
    el.querySelector("[data-action=close-panel]").onclick = closePanel;
  }
  function renderPage() {
    page = Math.max(1, Math.min(8, page));
    article.innerHTML = page % 2 ? original : second;
    if (page > 2)
      article.insertAdjacentHTML(
        "afterbegin",
        `<div class="chno">Sample reading location ${page}</div>`,
      );
    article
      .querySelector(".prose p")
      ?.insertAdjacentHTML(
        "beforeend",
        ' <button class="footnote" data-action="footnote" aria-label="Open footnote">[1]</button>',
      );
    article.querySelector(".footnote").onclick = (e) =>
      footnote(anchorOf(e.currentTarget.getBoundingClientRect()));
    $("#pageCount").textContent = page + " of 8";
    $("#bookPercent").textContent = Math.round(((page - 1) / 7) * 100) + "%";
    $(".railrow input").value = page;
    $("#leftTurn").disabled = page === 1;
    $("#rightTurn").disabled = page === 8;
    M.set("biblio.page", page);
    $("[data-action=bookmark]").style.color = marks.includes(page)
      ? "#f0c44a"
      : "";
    if (audio.follow) {
      audio.index = Math.min(3, Math.floor((page - 1) / 2));
      saveAudio();
    }
    apply();
  }
  function turn(d) {
    page += d;
    renderPage();
  }
  function bookmark() {
    marks = marks.includes(page)
      ? marks.filter((x) => x !== page)
      : [...marks, page];
    M.set("biblio.bookmarks", marks);
    renderPage();
  }
  function closeReader() {
    dialog(
      "Close book?",
      '<p>Your reading position is saved in this mock session.</p><button class="gold" data-action="end-session">Close book</button>',
      (e) =>
        (e.querySelector("[data-action=end-session]").onclick = () => {
          M.closeSession("biblio-book");
          M.back();
        }),
    );
  }
  function saveAudio() {
    M.set("biblio.audio", audio);
    mini.style.display =
      M.get("biblio.audio.ready", false) || audioOnly ? "flex" : "none";
    mini.innerHTML = `<button data-action="mini-play">${audio.playing ? "Pause" : "Play"}</button><span>Chapter ${audio.index + 1} · ${time(audio.position)}</span><button data-action="mini-audio">Audio</button>`;
    mini.querySelector("[data-action=mini-play]").onclick = () => {
      audio.playing = !audio.playing;
      saveAudio();
      if (panel === left) leftPanel("audio");
    };
    mini.querySelector("[data-action=mini-audio]").onclick = () =>
      leftPanel("audio");
  }
  function time(t) {
    return (
      Math.floor(t / 60) + ":" + String(Math.floor(t % 60)).padStart(2, "0")
    );
  }
  function leftPanel(tab) {
    left.innerHTML =
      header("") +
      `<div class="panel-tabs">${["contents", "bookmarks", "highlights", "audio"].map((t) => `<button data-tab="${t}" class="${t === tab ? "gold" : ""}">${t[0].toUpperCase() + t.slice(1)}</button>`).join("")}</div><div id="left-content"></div>`;
    let c = left.querySelector("#left-content");
    if (tab === "contents")
      c.innerHTML =
        P.state === "empty"
          ? "<p>No chapters</p>"
          : Array.from(
              { length: 4 },
              (_, i) =>
                `<button class="list-row" data-page="${i * 2 + 1}">${i + 1} &nbsp; Chapter ${["I", "II", "III", "IV"][i]}<small>Sample locations ${i * 2 + 1}–${i * 2 + 2}</small></button>`,
            ).join("");
    if (tab === "bookmarks") {
      c.innerHTML = marks.length
        ? marks
            .map(
              (n) =>
                `<div style="display:flex"><button class="list-row" data-page="${n}">Page ${n}<small>Saved reading location</small></button><button data-remove="${n}" aria-label="Remove bookmark ${n}">×</button></div>`,
            )
            .join("")
        : "<p>No bookmarks yet</p>";
      c.querySelectorAll("[data-remove]").forEach(
        (b) =>
          (b.onclick = () => {
            marks = marks.filter((n) => n !== +b.dataset.remove);
            M.set("biblio.bookmarks", marks);
            leftPanel(tab);
            renderPage();
          }),
      );
    }
    if (tab === "highlights") {
      c.innerHTML = highlights.length
        ? highlights
            .map(
              (h, i) =>
                `<button class="list-row" data-highlight="${i}">Page ${h.page}<small>${esc(h.text)}</small>${esc(h.note || "")}</button>`,
            )
            .join("")
        : "<p>No highlights yet</p>";
      c.querySelectorAll("[data-highlight]").forEach(
        (b) =>
          (b.onclick = () =>
            selection(
              highlights[+b.dataset.highlight].text,
              +b.dataset.highlight,
              anchorOf(b.getBoundingClientRect()),
            )),
      );
    }
    if (tab === "audio") {
      if (!M.get("biblio.audio.ready", false) && !audioOnly)
        c.innerHTML =
          '<p>Download the audiobook from this book’s page to read along.</p><button data-action="book-details">Open book page</button>';
      else c.innerHTML = audioPanel();
    }
    c.querySelector("[data-action=book-details]")?.addEventListener(
      "click",
      () => M.go("book.detail", { section: "audio" }),
    );
    c.querySelectorAll("[data-page]").forEach(
      (b) =>
        (b.onclick = () => {
          page = +b.dataset.page;
          renderPage();
          closePanel();
        }),
    );
    left
      .querySelectorAll("[data-tab]")
      .forEach((b) => (b.onclick = () => leftPanel(b.dataset.tab)));
    if (tab === "audio") bindAudio(c);
    bindClose(left);
    openPanel(left);
  }
  function audioPanel() {
    return `<div class="audio-card"><img src="../assets/feria/assets/dune-book.jpg" alt="Dune cover"><b>Dune</b><p>Frank Herbert · sample audio pairing</p><small>● Downloaded for this book</small><div class="transport"><button data-audio="back">−15</button><button data-audio="play">${audio.playing ? "Pause" : "Play"}</button><button data-audio="forward">+15</button></div><input type="range" aria-label="Audio seek" data-audio="position" min="0" max="1800" value="${audio.position}"><small>${time(audio.position)} / 30:00 · staged transport</small><label>Speed <select data-audio="speed">${[0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3].map((v) => `<option ${audio.speed === v ? "selected" : ""}>${v}</option>`).join("")}</select></label><label>Volume <input data-audio="volume" aria-label="Volume" type="range" value="${audio.volume}"></label><label>Mute <input data-audio="mute" type="checkbox" ${audio.mute ? "checked" : ""}></label><label>Sleep timer <select data-audio="sleep">${[0, 15, 30, 45, 60].map((v) => `<option value="${v}" ${audio.sleep === v ? "selected" : ""}>${v ? v + " minutes" : "Off"}</option>`).join("")}</select></label></div><div class="audio-card"><label>Follow my reading <input data-audio="follow" type="checkbox" ${audio.follow ? "checked" : ""}></label><p>Audio keeps pace with your page turns</p></div><div class="audio-card"><b>Sync status</b> <button data-audio="sync-pause">${audio.paused ? "Resume" : "Pause"}</button><p>${audio.sync === "Failed" ? "One chapter needs attention." : audio.paused ? "Sync paused" : audio.sync === "Queued" ? "Queued · sample sync state" : "4 of 4 chapters ready"}</p>${["Ready", "Ready", audio.sync, "Ready"].map((s, i) => `<p>${i + 1} &nbsp; ${s}${s === "Failed" ? '<button data-audio="retry">Retry</button>' : ""}</p>`).join("")}<button data-audio="restart">Restart sync</button></div><div class="audio-card"><b>Text sync</b><label>Mode <select data-audio="mode">${["Sentence", "Word", "Sentence + Word"].map((v) => `<option ${v === audio.mode ? "selected" : ""}>${v}</option>`).join("")}</select></label><label>Word size <input data-audio="scale" aria-label="Word size" type="range" min=".8" max="2" step=".1" value="${audio.scale}"></label></div><h3>Playlist</h3>${Array.from({ length: 4 }, (_, i) => `<button class="list-row ${audio.index === i ? "gold" : ""}" data-audio-index="${i}">${i + 1}. Chapter ${i + 1} ${audio.index === i ? (audio.playing ? "· playing" : "· paused") : ""}</button>`).join("")}`;
  }
  function bindAudio(c) {
    c.querySelectorAll("[data-audio]").forEach((b) => {
      let key = b.dataset.audio;
      if (b.tagName === "BUTTON")
        b.onclick = () => {
          if (key === "play") audio.playing = !audio.playing;
          if (key === "back") audio.position = Math.max(0, audio.position - 15);
          if (key === "forward")
            audio.position = Math.min(1800, audio.position + 15);
          if (key === "sync-pause") audio.paused = !audio.paused;
          if (key === "retry") audio.sync = "Ready";
          if (key === "restart") {
            dialog(
              "Restart sync",
              '<p>Re-run all chapters?</p><button data-action="confirm-sync" class="gold">Confirm</button>',
              (e) =>
                (e.querySelector("[data-action=confirm-sync]").onclick = () => {
                  audio.sync = "Queued";
                  saveAudio();
                  dismiss();
                  leftPanel("audio");
                }),
            );
            return;
          }
          saveAudio();
          leftPanel("audio");
        };
      else
        b.onchange = () => {
          audio[key] =
            b.type === "checkbox"
              ? b.checked
              : ["mode"].includes(key)
                ? b.value
                : +b.value;
          saveAudio();
          leftPanel("audio");
        };
    });
    c.querySelectorAll("[data-audio-index]").forEach(
      (b) =>
        (b.onclick = () => {
          audio.index = +b.dataset.audioIndex;
          audio.position = 0;
          audio.playing = true;
          saveAudio();
          leftPanel("audio");
        }),
    );
  }
  const themes = {
    Paper: ["#e9e4d8", "#3a362c"],
    Sepia: ["#e5d5b8", "#6b5b40"],
    Slate: ["#232830", "#c1c8d4"],
    Night: ["#111013", "#eee9de"],
    Contrast: ["#000000", "#ffffff"],
    Custom: [opts.background, opts.text],
  };
  const range = (key, label, min, max, step = 1) =>
    `<label>${label}<input data-opt="${key}" aria-label="${label}" type="range" min="${min}" max="${max}" step="${step}" value="${opts[key]}"><output>${opts[key]}</output></label>`;
  const select = (key, label, values) =>
    `<label>${label}<select data-opt="${key}">${values.map((v) => `<option ${opts[key] === v ? "selected" : ""}>${v}</option>`).join("")}</select></label>`;
  const check = (key, label) =>
    `<label>${label}<input type="checkbox" data-opt="${key}" ${opts[key] ? "checked" : ""}></label>`;
  function appearance() {
    const a = $("#panel");
    a.innerHTML =
      header("Appearance") +
      `<section class="group"><div class="group-label">Theme</div><div class="swatch-grid">${Object.keys(
        themes,
      )
        .map(
          (n) =>
            `<button class="swatch ${opts.theme === n ? "active" : ""}" style="background:${themes[n][0]};color:${themes[n][1]}" data-theme="${n}">${n}</button>`,
        )
        .join(
          "",
        )}</div>${opts.theme === "Custom" ? `<label>Text <input type="color" data-opt="text" value="${opts.text}"></label><label>Background <input type="color" data-opt="background" value="${opts.background}"></label><p class="book-ui-notice">Choose a readable text and background pair.</p>` : ""}${check("invert", "Invert images in dark themes")}</section><section class="group"><div class="group-label">Typeface</div><div class="font-grid">${["Literata", "Fraunces", "Inter"].map((f) => `<button class="font-card ${opts.font === f ? "active" : ""}" data-font="${f}"><strong style="font-family:${f === "Inter" ? "Switzer" : f}">Aa</strong><em>${f}</em></button>`).join("")}</div>${range("weight", "Weight", 300, 900, 100)}</section><section class="group"><div class="group-label">Size</div><div class="stepper"><button data-size="-5" aria-label="Decrease font size">A−</button><span>${opts.size} %</span><button data-size="5" aria-label="Increase font size">A+</button></div></section><section class="group"><div class="group-label">Spacing</div>${range("line", "Line", 1, 2.2, 0.1)}${range("word", "Word", 0, 1, 0.05)}${range("letter", "Letter", 0, 0.5, 0.01)}${range("paragraph", "Paragraph", 0, 2, 0.1)}${select("indent", "Indent", ["Book", "None", "Indent"])}</section><section class="group"><div class="group-label">Layout</div>${range("margin", "Margins", 24, 160, 4)}${select("justify", "Justify", ["Justified", "Ragged"])}${select("flow", "Flow", ["Pages", "Scroll"])}${range("width", "Line width", 400, 1600, 50)}${check("hyphens", "Hyphenation")}${select("columns", "Columns", ["Single", "Spread"])}</section><section class="group"><div class="group-label">Reading ruler</div>${check("ruler", "Focus band while reading")}${range("band", "Band height", 48, 200, 4)}${range("dim", "Dim outside", 0, 100, 2)}${range("y", "Band position", 0, 100, 2)}</section><section class="group"><div class="group-label">Advanced</div><label for="custom-css">Custom CSS (inert demonstration)</label><textarea id="custom-css" data-opt="css" placeholder="Custom CSS…">${esc(opts.css)}</textarea><button data-action="use-default">Use as default for all books</button><button data-action="reset-appearance">Reset to default</button><p class="book-ui-notice">Returns fonts, spacing, size, theme and colours to your default.</p></section>`;
    a.querySelectorAll("[data-font]").forEach(
      (b) =>
        (b.onclick = () => {
          opts.font = b.dataset.font;
          apply();
          appearance();
        }),
    );
    a.querySelectorAll("[data-size]").forEach(
      (b) =>
        (b.onclick = () => {
          opts.size = Math.max(50, Math.min(300, opts.size + +b.dataset.size));
          apply();
          appearance();
        }),
    );
    a.querySelectorAll("[data-theme]").forEach(
      (b) =>
        (b.onclick = () => {
          opts.theme = b.dataset.theme;
          apply();
          appearance();
        }),
    );
    a.querySelectorAll("[data-opt]").forEach(
      (el) =>
        (el.oninput = () => {
          let key = el.dataset.opt;
          opts[key] =
            el.type === "checkbox"
              ? el.checked
              : ["range", "number"].includes(el.type)
                ? +el.value
                : el.value;
          if (el.nextElementSibling?.tagName === "OUTPUT")
            el.nextElementSibling.textContent = el.value;
          apply();
          if (key === "flow" || key === "columns") appearance();
        }),
    );
    a.querySelector("[data-opt=columns]").disabled = opts.flow === "Scroll";
    a.querySelector("[data-opt=width]").disabled =
      opts.columns === "Spread" && opts.flow === "Pages";
    a.querySelector("[data-action=use-default]").onclick = () =>
      dialog(
        "Use as default for all books",
        '<p>Save this appearance as the default for this mock session?</p><button class="gold" data-action="confirm-default">Use as default</button>',
        (e) =>
          (e.querySelector("[data-action=confirm-default]").onclick = () => {
            M.set("biblio.defaults", { ...opts });
            dismiss();
          }),
      );
    a.querySelector("[data-action=reset-appearance]").onclick = () =>
      dialog(
        "Reset to default",
        '<p>Reset this book’s fonts, spacing, size, theme and colours?</p><button class="gold" data-action="confirm-reset">Reset</button>',
        (e) =>
          (e.querySelector("[data-action=confirm-reset]").onclick = () => {
            opts = Object.assign(
              opts,
              M.get("biblio.defaults", {
                theme: "Paper",
                font: "Literata",
                size: 100,
                weight: 400,
                line: 1.6,
                word: 0,
                letter: 0,
                paragraph: 0,
                indent: "Book",
                margin: 72,
                justify: "Justified",
                flow: "Pages",
                width: 960,
                hyphens: true,
                columns: "Single",
                ruler: false,
                band: 96,
                dim: 40,
                y: 50,
                invert: false,
                text: "#3a362c",
                background: "#e9e4d8",
                css: "",
              }),
            );
            apply();
            dismiss();
            appearance();
          }),
      );
    bindClose(a);
    openPanel(a);
  }
  function apply() {
    themes.Custom = [opts.background, opts.text];
    let t = themes[opts.theme] || themes.Paper;
    document.documentElement.style.setProperty("--paper-bg", t[0]);
    document.documentElement.style.setProperty("--paper-ink", t[1]);
    document.documentElement.style.setProperty(
      "--paper-soft",
      opts.theme === "Paper" ? "#827968" : t[1],
    );
    Object.assign(article.style, {
      fontFamily:
        opts.font === "Inter" ? "Switzer,sans-serif" : opts.font + ",serif",
      fontSize: (18 * opts.size) / 100 + "px",
      fontWeight: opts.weight,
      lineHeight: opts.line,
      wordSpacing: opts.word + "em",
      letterSpacing: opts.letter + "em",
      textAlign: opts.justify === "Justified" ? "justify" : "left",
      hyphens: opts.hyphens ? "auto" : "none",
      width:
        opts.columns === "Spread" && opts.flow === "Pages"
          ? "calc(100% - " + opts.margin * 2 + "px)"
          : opts.width + "px",
      maxWidth: "calc(100% - " + opts.margin * 2 + "px)",
      columnCount: opts.columns === "Spread" && opts.flow === "Pages" ? 2 : 1,
      columnGap: "64px",
    });
    article.querySelectorAll("p").forEach((p) => {
      p.style.marginBottom = opts.paragraph + "em";
      p.style.textIndent =
        opts.indent === "None" ? "0" : opts.indent === "Indent" ? "1.5em" : "";
    });
    $("#p1").style.overflowY = opts.flow === "Scroll" ? "auto" : "hidden";
    ruler.style.display = opts.ruler ? "block" : "none";
    let mid = (720 * opts.y) / 100,
      top = mid - opts.band / 2,
      bot = mid + opts.band / 2;
    ruler.style.background = `linear-gradient(to bottom,rgba(0,0,0,${opts.dim / 100}) 0px,rgba(0,0,0,${opts.dim / 100}) ${top}px,transparent ${top}px,transparent ${bot}px,rgba(0,0,0,${opts.dim / 100}) ${bot}px)`;
    M.set("biblio.appearance", opts);
  }
  function search() {
    dialog(
      "Search in book",
      '<label for="book-search">Search</label><div style="display:flex;gap:8px"><input id="book-search" type="search" placeholder="Search text…"><button data-action="clear-search" aria-label="Clear search">×</button></div><div id="search-results"><p>Search the public-domain sample.</p></div>',
      (e) => {
        let i = e.querySelector("input"),
          r = e.querySelector("#search-results");
        function run() {
          let q = i.value.trim().toLowerCase();
          r.innerHTML = !q
            ? "<p>Search the public-domain sample.</p>"
            : (original + second).toLowerCase().includes(q)
              ? `<button class="choice" data-action="search-result">Chapter I · ${esc(i.value)}<span>Go to page</span></button>`
              : "<p>No results found</p>";
          r.querySelector("button")?.addEventListener("click", () => {
            page = original.toLowerCase().includes(q) ? 1 : 2;
            renderPage();
            dismiss();
          });
        }
        i.oninput = run;
        e.querySelector("[data-action=clear-search]").onclick = () => {
          i.value = "";
          run();
          i.focus();
        };
        i.focus();
      },
    );
  }
  // Exact Reader2Logic.selectionMenuPos geometry: centre/clamp X; above else below/clamp Y.
  let selectionAnchor = { x: 160, y: 180, w: 300, h: 28 };
  function anchorOf(rect) {
    return { x: rect.x, y: rect.y, w: rect.width, h: rect.height };
  }
  function anchoredCard(kind, html, anchor, width, height, setup) {
    if (dismiss) dismiss();
    const focus = document.activeElement,
      e = document.createElement("div");
    e.className = "reader-popover-layer";
    e.innerHTML = `<section class="reader-popover ${kind}" role="dialog" aria-label="${kind === "footnote-card" ? "Footnote" : kind === "dictionary-card" ? "Dictionary definition" : "Text selection"}" tabindex="0">${html}</section>`;
    reader.append(e);
    const card = e.firstElementChild;
    let dispose;
    dismiss = () => {
      e.remove();
      dispose?.();
      dismiss = null;
      focus?.focus();
    };
    dispose = M.registerOverlay(e, dismiss);
    e.setAttribute("aria-modal", "false");
    card.setAttribute("aria-modal", "false");
    e.onclick = (ev) => {
      if (ev.target === e) dismiss();
    };
    function place() {
      const w = width || Math.ceil(card.scrollWidth),
        h = height || Math.ceil(card.scrollHeight),
        margin = kind === "selection-card" ? 10 : 12,
        gap = 12;
      let x = Math.max(
        margin,
        Math.min(1280 - w - margin, anchor.x + anchor.w / 2 - w / 2),
      );
      let above = anchor.y - h - gap,
        y = above >= margin ? above : anchor.y + anchor.h + gap;
      y = Math.max(margin, Math.min(720 - h - margin, y));
      Object.assign(card.style, {
        width: w + "px",
        height: h + "px",
        left: x + "px",
        top: y + "px",
      });
      card.dataset.anchor = JSON.stringify(anchor);
    }
    card.style.width = width ? width + "px" : "max-content";
    setup?.(e, place);
    place();
    (card.querySelector("button,textarea") || card).focus();
    return e;
  }
  function footnote(anchor) {
    anchoredCard(
      "footnote-card",
      '<div class="footnote-body">This sample note demonstrates the reader’s footnote card. The reading text is public domain.</div>',
      anchor,
      360,
      null,
    );
  }
  function dictionary(text, anchor) {
    const word = text.split(/\s+/)[0].replace(/[^\p{L}\p{N}-]/gu, "") || text;
    const state = P.dictionaryState || (/fortune/i.test(word) ? "ok" : "empty");
    const content =
      state === "loading"
        ? "<p>Looking up…</p>"
        : state === "ok"
          ? '<div class="definitions"><i>noun</i><p><span>1.</span> Wealth, or the way chance affects a person’s life.</p><small>Authored offline sample definition.</small></div>'
          : '<p>No definition found.</p><button data-action="open-wiktionary">Open in Wiktionary</button>';
    anchoredCard(
      "dictionary-card",
      `<h3>${esc(word)}</h3>${content}`,
      anchor,
      320,
      null,
      (e) => {
        e.querySelector("[data-action=open-wiktionary]")?.addEventListener(
          "click",
          () => {
            M.set("biblio.externalNavigation", {
              provider: "Wiktionary",
              word,
              url: "https://en.wiktionary.org/wiki/" + encodeURIComponent(word),
              simulated: true,
            });
            e.querySelector("p").textContent = "Wiktionary navigation staged.";
            e.querySelector("button").textContent = "Wiktionary · staged";
            e.querySelector("button").disabled = true;
          },
        );
      },
    );
  }
  function selection(text, index = -1, anchor = selectionAnchor) {
    selectionAnchor = anchor;
    const swatches = [
      ["Gold", "#F0C24A"],
      ["Slate", "#94A6C4"],
      ["Moss", "#97BB98"],
    ];
    const dots = swatches
      .map(
        ([n, c]) =>
          `<button class="highlight-dot" data-color="${c}" aria-label="Highlight ${n}" title="Highlight ${n}" style="--dot:${c}"></button>`,
      )
      .join("");
    const row = `<div class="selection-row">${dots}<span class="selection-divider"></span>${index < 0 ? '<button data-action="note">Note</button><button data-action="define">Define</button><button data-action="copy-selection">Copy</button>' : '<button data-action="delete-highlight">Delete</button>'}</div>`;
    function save(color, note = "") {
      const h = { text, page, note, color };
      if (index >= 0) highlights[index] = h;
      else highlights.push(h);
      M.set("biblio.highlights", highlights);
      dismiss();
      let first = article.querySelector(".prose p");
      if (first) {
        first.classList.add("selected-mark");
        first.style.background = color + "55";
      }
    }
    anchoredCard("selection-card", row, anchor, null, 44, (e) => {
      e.querySelectorAll("[data-color]").forEach(
        (b) =>
          (b.onclick = () =>
            save(
              b.dataset.color,
              index >= 0 ? highlights[index].note || "" : "",
            )),
      );
      e.querySelector("[data-action=note]")?.addEventListener("click", () =>
        anchoredCard(
          "selection-card note-editor",
          '<textarea id="highlight-note" aria-label="Note" placeholder="Add a note…"></textarea><div class="note-actions"><button data-action="cancel-note">Cancel</button><button data-action="save-highlight" disabled>Save</button></div>',
          anchor,
          300,
          128,
          (n) => {
            const input = n.querySelector("textarea"),
              saveButton = n.querySelector("[data-action=save-highlight]");
            input.oninput = () => (saveButton.disabled = !input.value.trim());
            n.querySelector("[data-action=cancel-note]").onclick = dismiss;
            saveButton.onclick = () => save("#F0C24A", input.value.trim());
            setTimeout(() => input.focus(), 0);
          },
        ),
      );
      e.querySelector("[data-action=define]")?.addEventListener("click", () =>
        dictionary(text, anchor),
      );
      e.querySelector("[data-action=copy-selection]")?.addEventListener(
        "click",
        () => {
          navigator.clipboard?.writeText(text).catch(() => {});
          dismiss();
        },
      );
      e.querySelector("[data-action=delete-highlight]")?.addEventListener(
        "click",
        () => {
          highlights.splice(index, 1);
          M.set("biblio.highlights", highlights);
          dismiss();
          renderPage();
        },
      );
    });
  }
  article.addEventListener("mouseup", (event) => {
    if (event.target.closest("button")) return;
    let text = window.getSelection()?.toString().trim();
    if (text) {
      const anchor = anchorOf(
        window.getSelection().getRangeAt(0).getBoundingClientRect(),
      );
      setTimeout(() => selection(text, -1, anchor), 0);
    }
  });
  article.addEventListener("dblclick", () => {
    if (audio.follow) {
      audio.position = page * 30;
      audio.playing = true;
      saveAudio();
    }
  });
  document.addEventListener("keydown", (e) => {
    if (
      dismiss ||
      panel ||
      ["INPUT", "TEXTAREA", "SELECT"].includes(e.target.tagName)
    )
      return;
    if (e.key === "ArrowRight") {
      turn(1);
      e.preventDefault();
    }
    if (e.key === "ArrowLeft") {
      turn(-1);
      e.preventDefault();
    }
    if (e.key.toLowerCase() === "b") bookmark();
    if ((e.ctrlKey || e.metaKey) && e.key === "f") {
      e.preventDefault();
      search();
    }
  });
  renderPage();
  if (P.state === "sync-error") audio.sync = "Failed";
  if (audioOnly) {
    M.set("biblio.audio.ready", true);
    leftPanel("audio");
  } else if (P.panel === "appearance") appearance();
  else if (P.panel) leftPanel(P.panel);
  if (P.state === "error" || P.state === "loading") {
    let e = document.createElement("div");
    e.className = "status-error";
    e.innerHTML = `<h2>${P.state === "error" ? "Couldn't open this book" : "Opening book…"}</h2><p>Staged Reader2 ${P.state} state</p><button data-action="retry-reader">${P.state === "error" ? "Go back" : "Retry"}</button>`;
    reader.append(e);
    if (P.state === "error") e.classList.add("failed-open");
    article.style.visibility = "hidden";
    e.querySelector("button").onclick = () => {
      if (P.state === "error") {
        M.back();
        return;
      }
      e.remove();
      article.style.visibility = "visible";
    };
  }
  saveAudio();
  M.ready();
})();
