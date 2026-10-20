(function () {
  "use strict";
  const U = UV,
    A = document.querySelector("#app"),
    P = MockUI.params;
  const initial = [
    {
      id: "dune",
      title: "Dune: Part Two",
      kind: "video",
      year: 2024,
      cover: U.cover("theatre-posters", "dune.jpg"),
      folder: "Films",
      identified: true,
      watched: false,
      present: true,
      path: "Sample Media/Films/Dune Part Two (2024)/Dune Part Two.mkv",
      size: "8.4 GB",
    },
    {
      id: "batman",
      title: "The Batman",
      kind: "video",
      year: 2022,
      cover: U.cover("theatre-posters", "batman.jpg"),
      folder: "Films",
      identified: true,
      watched: true,
      present: true,
      path: "Sample Media/Films/The Batman.mkv",
      size: "6.1 GB",
    },
    {
      id: "show",
      title: "One Piece",
      kind: "show",
      year: 1999,
      cover: U.cover("manga-covers", "onepiece.jpg"),
      folder: "Shows",
      identified: true,
      watched: false,
      present: true,
      path: "Sample Media/Shows/One Piece/",
      size: "14.8 GB",
    },
    {
      id: "book",
      title: "Dune · Novel",
      kind: "book",
      year: 1965,
      cover: "",
      folder: "Books",
      identified: true,
      present: true,
      path: "Sample Media/Books/Dune.epub",
      size: "4.8 MB",
    },
    {
      id: "comic",
      title: "Watchmen",
      kind: "comic",
      year: 1986,
      cover: U.cover("comic-covers", "watchmen.jpg"),
      folder: "Comics",
      identified: true,
      present: true,
      path: "Sample Media/Comics/Watchmen.cbz",
      size: "184 MB",
    },
    {
      id: "unknown",
      title: "Movie.2024.1080p",
      kind: "video",
      year: 2024,
      cover: "",
      folder: "Films",
      identified: false,
      present: true,
      path: "Sample Media/Films/Movie.2024.1080p.mkv",
      size: "2.2 GB",
    },
    {
      id: "away",
      title: "Oppenheimer",
      kind: "video",
      year: 2023,
      cover: U.cover("theatre-posters", "oppenheimer.jpg"),
      folder: "Films",
      identified: true,
      present: false,
      path: "Archive Drive/Films/Oppenheimer.mkv",
      size: "9.2 GB",
    },
    {
      id: "hidden",
      title: "Mad Max: Fury Road",
      kind: "video",
      year: 2015,
      cover: U.cover("theatre-posters", "madmax.jpg"),
      folder: "Films",
      identified: true,
      present: true,
      hidden: true,
      path: "Sample Media/Films/Mad Max.mkv",
      size: "4.5 GB",
    },
  ];
  let rows = MockUI.get("vault.rows", initial),
    state = P.state || MockUI.get("vault.state", "populated"),
    folder = P.folder || "",
    query = "",
    kind = "all",
    watched = "all",
    identity = "all",
    presence = P.state === "unavailable" ? "away" : "all",
    view = P.view || "posters",
    hidden = P.state === "hidden",
    notice = "",
    recentExpanded = P.recent === true,
    arrivalPage = 0;
  const save = () => {
    MockUI.set("vault.rows", rows);
    MockUI.set("vault.state", state);
  };
  function render() {
    A.className = "screen";
    if (["empty", "scanning", "error"].includes(state)) {
      A.innerHTML = `<div class="eyebrow">ON THIS MACHINE</div><h1 style="font:56px var(--display);margin:8px 0 35px">Vault</h1><section class="empty"><img src="../assets/icons/vault-folder.svg" width="48" height="48" alt=""><h2>${state === "scanning" ? "Looking through your folder…" : state === "error" ? "This folder couldn’t be read" : "Add a folder of comics, books, or videos"}</h2><p>${state === "error" ? "Sample drive is unavailable. Reconnect it or choose another sample folder." : "The Vault reads what is already on this machine and keeps it here — nothing is downloaded or moved."}</p>${state === "scanning" ? '<p class="status">Sample scan · 8 entries discovered · waiting for fixture review</p>' + U.btn("Finish sample scan", "finish", 'class="primary"') + U.btn("Cancel scan", "cancelScan") : U.btn(state === "error" ? "Try another folder" : "Add folder", "add", 'class="primary"')}</section><p class="notice">Browser mock: folder selection is sample data. No actual filesystem is read or indexed.</p>`;
      U.bind(A, {
        add,
        finish: () => {
          state = "populated";
          save();
          render();
        },
        cancelScan: () => {
          state = "empty";
          save();
          render();
        },
      });
      return;
    }
    let shown = rows.filter(
      (x) =>
        (hidden ? x.hidden : !x.hidden) &&
        (!folder || x.folder === folder) &&
        (!query || x.title.toLowerCase().includes(query.toLowerCase())) &&
        (kind === "all" ||
          (kind === "video" && x.kind === "show") ||
          x.kind === kind) &&
        (watched === "all" || Boolean(x.watched) === (watched === "watched")) &&
        (identity === "all" ||
          Boolean(x.identified) === (identity === "identified")) &&
        (presence === "all" || x.present === (presence === "here")),
    );
    A.innerHTML = `${!folder && !hidden && !query ? `<section class="vault-hero"><img src="${U.cover("theatre-posters", "dune.jpg")}" alt=""><div><p class="eyebrow">RECENTLY ADDED · ON THIS MACHINE</p><h1>Dune: Part Two</h1><p class="small">2024 · Film · 8.4 GB</p><div class="bar" style="margin-top:20px">${U.btn("Continue watching", "continue", 'class="primary"')}${U.btn("View details", "hero")}</div></div></section>` : ""}<div class="vault-layout"><aside class="vault-rail"><h2 style="font:24px var(--display);margin-bottom:15px">Vault</h2>${U.btn("All folders", "root")}${["Films", "Shows", "Books", "Comics"].map((x) => U.btn(x, "folder", `data-folder="${x}" class="${x === folder ? "on" : ""}"`)).join("")}${U.btn("Hidden", "hidden")}${U.btn("+ Add folder", "add")}${U.btn("Scan folder", "scan")}${U.btn("Downloads ↗", "downloads")}</aside><section class="vault-main"><div class="bar" style="justify-content:space-between;margin-bottom:15px"><div>${U.btn("Vault", "root")} ${folder ? " / " + U.esc(folder) : hidden ? " / Hidden" : ""} <span class="small">${shown.length} items</span></div>${U.btn(view === "posters" ? "List view" : "Poster view", "view")}</div><div class="bar"><label><span class="small">Search </span><input id="search" aria-label="Search Vault" placeholder="Search your Vault" value="${U.esc(query)}"></label>${U.btn("Clear", "clear")}<label class="small">Kind <select id="kind"><option value="all">All media</option><option value="video">Video</option><option value="book">Books</option><option value="comic">Comics</option></select></label><label class="small">Watched <select id="watched"><option value="all">Any</option><option value="watched">Watched</option><option value="unwatched">Unwatched</option></select></label><label class="small">Identity <select id="identity"><option value="all">Any identity</option><option value="identified">Identified</option><option value="unknown">Unidentified</option></select></label><label class="small">Presence <select id="presence"><option value="all">All copies</option><option value="here">On this device</option><option value="away">Unavailable</option></select></label></div>${notice ? `<div class="notice" role="status">${U.esc(notice)}</div>` : ""}<div class="vault-grid ${view === "list" ? "list" : ""}">${shown.map((x) => `<article class="vault-tile"><button class="open" data-action="detail" data-id="${x.id}">${x.cover ? `<img src="${x.cover}" alt="">` : `<div class="book-cover" style="height:190px">${U.esc(x.title)}</div>`}<div><h3>${U.esc(x.title)}</h3><p class="small">${x.kind === "show" ? "2 seasons" : x.year + " · " + x.kind}${x.watched ? " · Watched" : ""}</p><p class="status">${!x.present ? "Drive not connected" : !x.identified ? "Needs identification" : ""}</p></div></button><button class="menu" data-action="menu" data-id="${x.id}" aria-label="Actions for ${U.esc(x.title)}">•••</button></article>`).join("") || `<div class="empty" style="grid-column:1/-1"><h2>${hidden ? "No hidden titles" : "No titles match these filters"}</h2>${U.btn("Clear filters", "reset")}</div>`}</div></section></div>`;
    U.bind(A, {
      root: () => {
        folder = "";
        hidden = false;
        render();
      },
      folder: (b) => {
        folder = b.dataset.folder;
        hidden = false;
        render();
      },
      hidden: () => {
        hidden = true;
        folder = "";
        render();
      },
      view: () => {
        view = view === "posters" ? "list" : "posters";
        render();
      },
      add,
      scan: () => {
        state = "scanning";
        render();
      },
      downloads: () => MockUI.go("downloads"),
      hero: () => detail(rows[0]),
      continue: () => launch(rows[0]),
      detail: (b) => detail(rows.find((x) => x.id === b.dataset.id)),
      menu: (b) => menu(rows.find((x) => x.id === b.dataset.id)),
      clear: () => {
        query = "";
        render();
        A.querySelector("#search").focus();
      },
      reset: () => {
        query = "";
        kind = watched = identity = presence = "all";
        render();
      },
    });
    enhanceVault();
    A.querySelector("#search").oninput = (e) => {
      const pos = e.target.selectionStart;
      query = e.target.value;
      render();
      const input = A.querySelector("#search");
      input.focus();
      input.setSelectionRange(pos, pos);
    };
    for (const [id, val] of Object.entries({
      kind,
      watched,
      identity,
      presence,
    })) {
      A.querySelector("#" + id).value = val;
      A.querySelector("#" + id).onchange = (e) => {
        if (id === "kind") kind = e.target.value;
        if (id === "watched") watched = e.target.value;
        if (id === "identity") identity = e.target.value;
        if (id === "presence") presence = e.target.value;
        render();
      };
    }
  }
  function enhanceVault() {
    // Current VaultPage puts compact continuation / arrival shelves inside the browse grid header.
    A.querySelector(".vault-hero")?.remove();
    if (query || hidden) return;
    const main = A.querySelector(".vault-main"),
      header = main.firstElementChild,
      section = document.createElement("section");
    section.className = "vault-shelves";
    const continuing = rows.filter(
        (x) =>
          !x.hidden && x.present && ["dune", "book", "comic"].includes(x.id),
      ),
      arrivals = rows.filter((x) => !x.hidden && x.present).slice(0, 5);
    const toggle = document.createElement("button");
    toggle.dataset.action = "just-arrived";
    toggle.textContent = "Just arrived";
    toggle.setAttribute("aria-pressed", String(recentExpanded));
    toggle.onclick = () => {
      recentExpanded = !recentExpanded;
      render();
    };
    header.append(toggle);
    section.innerHTML = `<div class="vault-shelf-head"><h2>Pick up where you left off</h2></div><div class="vault-resume-rail">${continuing.map((x, i) => `<article class="vault-resume"><button data-v-resume="${x.id}">${x.cover ? `<img src="${x.cover}" alt="">` : '<div class="book-cover" style="height:100px">' + U.esc(x.title) + "</div>"}<strong>${U.esc(x.title)}</strong><span class="small">${x.kind === "video" ? "Continue watching" : "Continue reading"} · ${42 - i * 9}%</span><div class="progress" style="--pct:${42 - i * 9}%"><i></i></div></button><button data-v-detail="${x.id}" style="font-size:12px;padding:4px 8px">Details</button></article>`).join("")}</div>${recentExpanded ? `<div class="vault-shelf-head"><h2>Just arrived</h2><div class="bar"><button data-v-slide="-1" aria-label="Previous recent arrivals">‹</button><button data-v-slide="1" aria-label="Next recent arrivals">›</button></div></div><div class="vault-recent-rail">${arrivals.map((x) => `<article class="vault-arrival">${x.cover ? `<img src="${x.cover}" alt="">` : ""}<div><strong>${U.esc(x.title)}</strong><div class="bar"><button data-v-resume="${x.id}">${x.kind === "video" || x.kind === "show" ? "▶ Play" : "Read"}</button><button data-v-detail="${x.id}">Details</button></div></div></article>`).join("")}</div>` : ""}${!folder ? `<h2 class="section-title">Your folders</h2><div class="vault-folder-rail">${["Films", "Shows", "Books", "Comics"].map((f) => `<button data-v-folder="${f}"><img src="../assets/icons/vault-folder.svg" alt=""><span>${f}<br><small>${rows.filter((x) => x.folder === f).length} entries</small></span></button>`).join("")}</div>` : ""}`;
    header.after(section);
    section
      .querySelectorAll("[data-v-resume]")
      .forEach(
        (b) =>
          (b.onclick = () =>
            launch(rows.find((x) => x.id === b.dataset.vResume))),
      );
    section
      .querySelectorAll("[data-v-detail]")
      .forEach(
        (b) =>
          (b.onclick = () =>
            detail(rows.find((x) => x.id === b.dataset.vDetail))),
      );
    section.querySelectorAll("[data-v-folder]").forEach(
      (b) =>
        (b.onclick = () => {
          folder = b.dataset.vFolder;
          render();
        }),
    );
    section.querySelectorAll("[data-v-slide]").forEach(
      (b) =>
        (b.onclick = () => {
          const rail = section.querySelector(".vault-recent-rail");
          rail.scrollBy({
            left: Number(b.dataset.vSlide) * 301,
            behavior: "auto",
          });
        }),
    );
    if (P.section === "recent")
      requestAnimationFrame(() =>
        section
          .querySelector(".vault-recent-rail")
          ?.scrollIntoView({ block: "center" }),
      );
  }
  function add() {
    U.dialog(
      "Add folder",
      `<p>Select a sample folder. Your real files are never requested or read.</p><div class="row"><div class="grow"><strong>Sample Media</strong><p>Films, Shows, Books and Comics · 8 sample entries</p></div>${U.btn("Select folder", "select", 'class="primary"')}</div><div class="row"><div class="grow"><strong>Archive Drive</strong><p>Unavailable sample drive</p></div>${U.btn("Select folder", "unavailable")}</div>${U.btn("Cancel", "cancel")}`,
      (el, close) =>
        U.bind(el, {
          select: () => {
            close();
            state = "scanning";
            save();
            render();
          },
          unavailable: () => {
            close();
            state = "error";
            render();
          },
          cancel: close,
        }),
      true,
    );
  }
  function launch(x, episode) {
    if (!x.present) {
      U.dialog(
        "Drive not connected",
        `<p>${U.esc(x.title)} is indexed in your Vault, but its file is on Archive Drive. Reconnect that drive in the native app to open it.</p><p class="small">${U.esc(x.path)}</p>${U.btn("Close", "cancel")}`,
        (el, c) => U.bind(el, { cancel: c }),
        true,
      );
      return;
    }
    MockUI.openSession({ kind: x.kind, id: "vault-" + x.id, title: x.title });
    MockUI.go(
      x.kind === "book"
        ? "reader.book"
        : x.kind === "comic"
          ? "reader.comic"
          : "player.video",
      { title: x.title, source: "vault", episode: episode || 1, sample: true },
    );
  }
  function detail(x) {
    if (x.kind === "show") {
      show(x);
      return;
    }
    // VaultDetailSheet.qml: centered same-window panel; keep its window chrome clear.
    if (!A.contains(document.activeElement)) {
      const invoker =
        A.querySelector(`[data-v-detail="${x.id}"]`) ||
        A.querySelector(`[data-action="detail"][data-id="${x.id}"]`);
      invoker?.focus({ preventScroll: true });
    }
    const sheet = U.dialog(
      x.title,
      `<p>${x.year} · ${x.kind} · 1 copy held</p><div class="bar">${U.btn(x.kind === "book" || x.kind === "comic" ? "Read" : '<img class="vault-play-glyph" src="../assets/icons/play-dark.svg" alt=""> Play', "play", 'class="primary"')}${U.btn("Reveal in Explorer", "reveal")}${U.btn(x.watched ? "Mark unwatched" : "Mark watched", "watched")}</div><h3 class="section-title">Copies you hold</h3><section class="panel"><strong>${x.kind === "video" ? "1080p" : "Original file"} · ${x.size}</strong><p>${x.present ? "Available" : "Drive not connected"}</p><p class="small">${U.esc(x.path)}</p></section><h3 class="section-title">Companions</h3>${x.kind === "video" ? `<ul class="companion-list"><li>${U.esc(x.title)}.en.srt · English subtitles</li><li>${U.esc(x.title)}.commentary.m4a · Audio commentary</li></ul><h3 class="section-title">Extras</h3><div class="bar">${U.btn('<img class="vault-play-glyph" src="../assets/icons/play.svg" alt=""> Making of · sample', "extra", `data-extra="Making of"`)}${U.btn('<img class="vault-play-glyph" src="../assets/icons/play.svg" alt=""> Trailer · sample', "extra", `data-extra="Trailer"`)}</div>` : "<p>No companion files</p>"}<h3 class="section-title">Why Vault believes this</h3><p>${x.identified ? "Title and year matched from the sample folder name. Identity is a local fixture." : "No confident catalogue match. Choose an identity to connect this folder to a title."}</p><div class="bar">${U.btn(x.identified ? "Un-identify" : "Identify…", "identify")}${U.btn(x.hidden ? "Restore" : "Hide", "hide")}${U.btn("Identity changed…", "ceremony")}</div>`,
      (el, close) =>
        U.bind(el, {
          extra: (b) =>
            MockUI.go("player.video", {
              title: x.title + " · " + b.dataset.extra,
              source: "vault-extra",
              sample: true,
            }),
          play: () => launch(x),
          reveal: () => {
            close();
            reveal(x);
          },
          watched: () => {
            x.watched = !x.watched;
            save();
            close();
            render();
            detail(x);
          },
          identify: () => {
            close();
            if (x.identified) {
              x.identified = false;
              save();
              render();
            } else identify(x);
          },
          hide: () => {
            x.hidden = !x.hidden;
            save();
            close();
            render();
          },
          ceremony: () => {
            close();
            ceremony(x);
          },
        }),
    );
    sheet.layer.classList.add("vault-detail-overlay");
    const panel = sheet.layer.querySelector(".dialog"),
      closeButton = panel.querySelector("[data-action=close]"),
      scroll = document.createElement("div");
    scroll.className = "vault-detail-scroll";
    Array.from(panel.childNodes).forEach((node) => {
      if (node !== closeButton) scroll.append(node);
    });
    panel.append(scroll);
    closeButton.setAttribute("aria-label", "Close Vault details");
    setTimeout(() => {
      if (panel.isConnected) panel.querySelector("[data-action=play]")?.focus();
    }, 0);
  }
  function reveal(x) {
    U.dialog(
      "File location",
      `<p>Native Explorer would reveal this file. This browser mock only displays the sample location.</p><section class="panel">${U.esc(x.path)}</section><p>${x.present ? "Sample drive is available." : "Archive Drive is not connected."}</p>${U.btn("Close", "cancel")}`,
      (el, c) => U.bind(el, { cancel: c }),
      true,
    );
  }
  function menu(x) {
    U.dialog(
      "Actions · " + U.esc(x.title),
      `<div class="bar" style="flex-direction:column;align-items:stretch">${U.btn("Open details", "details")}${U.btn("Reveal in Explorer", "reveal")}${U.btn(x.identified ? "Un-identify" : "Identify…", "identify")}${U.btn(x.watched ? "Mark unwatched" : "Mark watched", "watched")}${U.btn(x.hidden ? "Restore" : "Hide", "hide")}${U.btn("Cancel", "cancel")}</div>`,
      (el, close) =>
        U.bind(el, {
          details: () => {
            close();
            detail(x);
          },
          reveal: () => {
            close();
            reveal(x);
          },
          identify: () => {
            close();
            if (x.identified) {
              x.identified = false;
              save();
              render();
            } else identify(x);
          },
          watched: () => {
            x.watched = !x.watched;
            save();
            close();
            render();
          },
          hide: () => {
            x.hidden = !x.hidden;
            save();
            close();
            render();
          },
          cancel: close,
        }),
      true,
    );
  }
  function identify(x) {
    U.dialog(
      "Identify this Vault folder",
      `<p>Folder: ${U.esc(x.title)}</p><p class="small">Search offline catalogues first. Your files stay in place.</p><label class="field" for="match">Title or catalogue ID</label><input id="match" value="${U.esc(x.title)}"><div class="bar" style="margin:12px 0">${U.btn("Search", "search", 'class="primary"')}${U.btn("Clear", "clear")}</div><div id="candidates"></div>${U.btn("Cancel", "cancel")}`,
      (el, close) => {
        const results = () => {
          const q = el.querySelector("#match").value.trim();
          el.querySelector("#candidates").innerHTML = q
            ? ["Dune (2021)", "Dune (1984)", q + " · Manual match"]
                .map(
                  (t, i) =>
                    `<div class="row"><div class="grow"><strong>${U.esc(t)}</strong><p>${i === 2 ? "Manual identity · sample only" : "Offline catalogue · film"}</p></div>${U.btn("Use this", "use", `data-name="${U.esc(t)}"`)}</div>`,
                )
                .join("")
            : "<p>Enter a title to find a match.</p>";
          U.bind(el, {
            use: (b) => {
              const chosen = b.dataset.name;
              close();
              U.dialog(
                "Confirm identity",
                `<p>Connect <strong>${U.esc(x.title)}</strong> to <strong>${U.esc(chosen)}</strong>?</p><p>Your sample files stay in place.</p><div class="bar">${U.btn("Cancel", "cancel")}${U.btn("Use this identity", "confirm", 'class="primary"')}</div>`,
                (d, c) =>
                  U.bind(d, {
                    cancel: c,
                    confirm: () => {
                      x.title = chosen
                        .replace(/ \(\d+\)$/, "")
                        .replace(" · Manual match", "");
                      x.identified = true;
                      save();
                      c();
                      notice = "Identity updated for " + x.title + ".";
                      render();
                    },
                  }),
                true,
              );
            },
          });
        };
        U.bind(el, {
          search: results,
          clear: () => {
            el.querySelector("#match").value = "";
            results();
            el.querySelector("#match").focus();
          },
          cancel: close,
        });
        results();
      },
      true,
    );
  }
  function ceremony(x) {
    U.dialog(
      "The media at this path changed",
      `<p>The sample file at this path has changed. Should it keep the same reading or watching state?</p><p class="small">${U.esc(x.path)}</p><div class="bar">${U.btn("Same Media", "same", 'class="primary"')}${U.btn("New Media", "new")}${U.btn("This looks like a copy", "copy")}${U.btn("Cancel", "cancel")}</div>`,
      (el, c) => {
        const apply = (msg) => {
          notice = msg;
          c();
          save();
          render();
        };
        U.bind(el, {
          same: () => apply("Same media: existing progress retained."),
          new: () => {
            x.watched = false;
            apply("New media: progress reset for this sample copy.");
          },
          copy: () => {
            c();
            U.dialog(
              "This looks like a copy",
              `<p>This sample file appears to be another copy of ${U.esc(x.title)}.</p><div class="bar">${U.btn("Use Existing State", "existing", 'class="primary"')}${U.btn("Separate Copy", "separate")}${U.btn("Cancel", "cancel")}</div>`,
              (d, close) =>
                U.bind(d, {
                  existing: () => {
                    notice = "Copy linked to existing sample state.";
                    close();
                    render();
                  },
                  separate: () => {
                    notice = "Copy kept separate in the sample library.";
                    close();
                    render();
                  },
                  cancel: close,
                }),
              true,
            );
          },
          cancel: c,
        });
      },
      true,
    );
  }
  function show(x) {
    U.dialog(
      "One Piece",
      `<p>1999 · Show · 2 seasons · Sample local episodes</p>${[1, 2].map((n) => `<details ${n === 1 ? "open" : ""}><summary>Season ${n} · 3 episodes</summary>${[1, 2, 3].map((e) => `<div class="row"><div class="grow"><strong>Episode ${e}</strong><p>Available · 24 min · 1080p</p></div>${U.btn("Play", "episode", `data-n="${e}"`)}</div>`).join("")}</details>`).join("")}<div class="bar">${U.btn("Identify…", "identify")}${U.btn("Reveal in Explorer", "reveal")}</div>`,
      (el, c) =>
        U.bind(el, {
          episode: (b) => launch(x, Number(b.dataset.n)),
          identify: () => {
            c();
            identify(x);
          },
          reveal: () => {
            c();
            reveal(x);
          },
        }),
    );
  }
  render();
  if (P.detail) detail(rows.find((x) => x.id === P.detail) || rows[0]);
  if (P.dialog === "identify") identify(rows.find((x) => x.id === "unknown"));
  if (P.dialog === "ceremony") ceremony(rows[0]);
  MockUI.ready();
})();
