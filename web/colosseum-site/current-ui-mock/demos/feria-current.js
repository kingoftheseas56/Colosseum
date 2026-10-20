(function () {
  "use strict";
  const D = FeriaData,
    M = MockUI,
    $ = (s) => document.querySelector(s),
    main = $("#main"),
    esc = (s) =>
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
      );
  const A = "../assets/feria/",
    artMap = {
      stranger: "stranger-poster.jpg",
      wednesday: "wednesday-poster.jpg",
      squid: "squid-poster.jpg",
      arcane: "arcane-poster.jpg",
      oplive: "oplive-poster.jpg",
      glassonion: "glassonion-poster.jpg",
      roma: "roma-poster.jpg",
      vinland: "vinland-poster.jpg",
      dune2: "dune2-poster.jpg",
      severance: "severance-poster.jpg",
      frieren: "frieren-poster.jpg",
      phm: "phm-cover.jpg",
      dunebook: "dune-book.jpg",
    };
  let apps = M.get("feria.apps", D.DEFAULT_APPS.slice()),
    saved = M.get("feria.saved", []),
    sessions = M.get("feria.sessions", {}),
    historyRows = M.get("feria.history", []),
    bookmarks = M.get("feria.bookmarks", []),
    closed = M.get("feria.closed", []),
    downloads = M.get("feria.downloads", []),
    prefs = M.get("feria.preferences", {
      restore: false,
      freeze: true,
      engine: "DuckDuckGo",
      layout: true,
      arrows: true,
      compatibility: false,
    });
  let appPrefs = M.get("feria.appPreferences", {});
  const appOption = (k) =>
    (appPrefs[provider] || {
      layout: true,
      arrows: true,
      compatibility: false,
    })[k];
  let view = M.params.view || "home",
    lens = M.params.lens || "all",
    titleId = M.params.id || "stranger",
    provider = M.params.app || "netflix",
    selected = M.get("feria.selectedSpaces", {}),
    stack = [],
    positions = {},
    accountTab = M.params.tab || "history",
    query = M.params.q || "",
    sheetDisposer = null,
    appOpener = null;
  let sidebar = M.params.expanded !== false,
    addressOpen = false,
    findOpen = false,
    findQuery = "",
    zoom = 100,
    full = false,
    appPage = M.params.panel || "",
    request = "",
    status = "",
    serial = Date.now(),
    restorePending = M.params.state === "restore";
  const glyph = (k, cls = "") =>
    `<img class="${cls}" src="${A}brand-logos/${k}.${D.BRAND_FORMAT[k]}" alt="${esc(D.P[k]?.n || k)}">`;
  function persist() {
    M.set("feria.selectedSpaces", selected);
    M.set("feria.apps", apps);
    M.set("feria.selectedSpaces", selected);
    M.set("feria.saved", saved);
    M.set("feria.sessions", sessions);
    M.set("feria.history", historyRows);
    M.set("feria.bookmarks", bookmarks);
    M.set("feria.closed", closed);
    M.set("feria.downloads", downloads);
    M.set("feria.preferences", prefs);
    M.set("feria.appPreferences", appPrefs);
  }
  function category() {
    return D.P[provider].v === "read"
      ? "readspaces"
      : D.P[provider].v === "listen"
        ? "soundspaces"
        : "watchspaces";
  }
  function active() {
    const a = sessions[provider] || [];
    return a.find((x) => x.id === selected[provider]) || a[0];
  }
  function newSpace(priv = false, id = "") {
    const t = D.T[id],
      s = {
        id: "feria-space-" + ++serial,
        app: provider,
        private: priv,
        title: t ? t.t : D.P[provider].n,
        titleId: id,
        url: "https://" + D.P[provider].d,
        nav: [],
        at: 0,
        playing: false,
        manualPaused: false,
        muted: false,
      };
    s.nav = [{ url: s.url, title: s.title }];
    (sessions[provider] ||= []).push(s);
    selected[provider] = s.id;
    appPage = "";
    if (!priv) addHistory(s);
    persist();
    return s;
  }
  function addHistory(s) {
    if (s.private) return;
    historyRows = historyRows.filter(
      (x) => !(x.app === provider && x.url === s.url && x.title === s.title),
    );
    historyRows.unshift({
      id: "history-" + ++serial,
      app: provider,
      url: s.url,
      title: s.title,
      titleId: s.titleId,
      time: "Today",
    });
    historyRows = historyRows.slice(0, 60);
  }
  function openApp(pk, id = "") {
    if (D.P[pk].app) {
      status = D.P[pk].n + " reads only in its own app, so it can’t open here.";
      openApps();
      return;
    }
    remember();
    stack.push({ view, lens, titleId, provider, appPage });
    provider = pk;
    view = "host";
    appPage = "";
    status = "";
    if (!sessions[pk]?.length) newSpace(false, id);
    else {
      selected[pk] = selected[pk] || sessions[pk][0].id;
      if (id) {
        let s = sessions[pk].find((x) => x.titleId === id);
        if (!s) s = newSpace(false, id);
        selected[pk] = s.id;
      }
    }
    const s = active();
    if (s.autoPaused) {
      s.playing = true;
      s.autoPaused = false;
    }
    M.openSession({
      kind: "app",
      id: "feria-app-" + pk,
      title: D.P[pk].n,
      route: "feria.host",
      params: { view: "host", app: pk },
    });
    persist();
    render();
  }
  function remember() {
    const sc = $(".fscroll");
    if (sc) positions[view + ":" + lens] = sc.scrollTop;
  }
  function navigate(v) {
    remember();
    stack.push({ view, lens, titleId, provider, appPage });
    view = v;
    status = "";
    render();
  }
  function back() {
    if ($("#apps-dialog").open) {
      closeApps();
      return;
    }
    if (view === "host" && appPage) {
      appPage = "";
      request = "";
      render();
      return;
    }
    const p = stack.pop();
    if (p) {
      assignState(p);
      render();
    } else if (view !== "home") {
      view = "home";
      render();
    } else M.back();
  }
  const assignState = (p) => {
    view = p.view;
    lens = p.lens;
    titleId = p.titleId;
    provider = p.provider;
    appPage = p.appPage || "";
  };
  function poster(id, large = false) {
    const t = D.T[id];
    if (!t) return "";
    const src = artMap[id];
    return `<div class="poster ${D.SHAPE[t.k] === "sq" ? "square" : D.SHAPE[t.k] === "ci" ? "circle" : ""} ${src ? "" : "fallback"}">${src ? `<img src="${A}assets/${src}" alt="${esc(t.t)}">` : `<span>${esc(t.t)}</span><small>Artwork not bundled</small>`}</div>`;
  }
  function card(id) {
    const t = D.T[id];
    if (!t) return "";
    return `<button class="media-card" data-action="title" data-id="${id}" aria-label="${esc(t.t)}">${poster(id)}<h3>${esc(t.t)}</h3><p>${esc(t.k === "album" || t.k === "artist" ? t.by : t.y + " · " + D.KIND[t.k])}</p></button>`;
  }
  function appTile(pk, i) {
    return `<button class="app" data-action="open-app" data-app="${pk}" aria-label="Open ${esc(D.P[pk].n)}">${i < 9 ? `<small>${i + 1}</small>` : ""}${glyph(pk)}<span>${esc(D.P[pk].n)}</span></button>`;
  }
  function shownShelves() {
    return D.SHELVES.filter(
      (s) =>
        (lens === "all" || s.v === lens) &&
        s.apps.some((a) => apps.includes(a)),
    );
  }
  function shelfItems(s) {
    return (
      s.items ||
      s.chips[apps.find((a) => s.chips[a])] ||
      s.chips[Object.keys(s.chips)[0]]
    );
  }
  function featured() {
    return lens === "read"
      ? "phm"
      : lens === "listen"
        ? "showgirl"
        : "stranger";
  }
  function home() {
    const ft = D.T[featured()],
      pk = ft.d[0][0];
    return `<div class="ambient" style="${lens === "watch" || lens === "all" ? "" : "background-image:none;background-color:#243345"}"></div><nav class="rowindex" aria-label="Feria rows"><button data-action="scroll-feature" aria-label="Featured">⌃</button><button data-action="scroll-apps" aria-label="Your apps">▦</button><button data-action="search" aria-label="Search"><img src="../assets/icons/search.svg" alt=""></button><button data-action="account" aria-label="Your Feria">◉</button></nav><div class="fscroll"><div class="fboard"><button class="feature" id="featured" data-action="title" data-id="${featured()}"><div class="featurecopy"><div class="service">${glyph(pk)}${D.P[pk].n}</div><h1>${esc(ft.t)}</h1><div class="facts"><span>${ft.y}</span>${ft.f.map((x) => `<span>${esc(x)}</span>`).join("")}</div><p>${esc(ft.s)}</p><span class="details">Details ›</span></div></button><section class="apps" id="your-apps"><div class="head"><h2>Your apps</h2><button data-action="apps">See all ›</button></div><div class="appgrid">${apps.map(appTile).join("")}<button class="app add" data-action="apps"><span style="font-size:30px">＋</span>Apps</button></div></section><nav class="lensbar" aria-label="Feria lens">${[
      ["all", "All"],
      ["watch", "Watch"],
      ["listen", "Listen"],
      ["read", "Read"],
    ]
      .map(
        ([k, n]) =>
          `<button data-action="lens" data-lens="${k}" class="${lens === k ? "on" : ""}">${n}</button>`,
      )
      .join("")}</nav>${continueShelf()}${shownShelves()
      .map(
        (s) =>
          `<section class="shelf"><div class="head"><h2>${s.title}</h2><button data-action="seeall" data-id="${s.id}">See all ›</button></div><div class="media-rail">${shelfItems(s).map(card).join("")}</div></section>`,
      )
      .join(
        "",
      )}${!apps.length ? '<div class="empty-card"><h2>Your Feria starts with your apps</h2><p>Add an app to see its shelves here.</p><button data-action="apps">Choose apps</button></div>' : ""}<p class="fixture-note">Catalogue listings and availability are illustrative, as in PorticoData. All services and browser actions here are local fixtures.</p></div></div>`;
  }
  function continueShelf() {
    const entries = Object.values(sessions)
      .flat()
      .filter(
        (s) =>
          !s.private && s.titleId && (lens === "all" || D.P[s.app].v === lens),
      );
    return entries.length
      ? `<section class="shelf"><div class="head"><h2>Continue ${lens === "read" ? "reading" : lens === "listen" ? "listening" : "watching"}</h2><button data-action="seeall" data-id="continue">See all ›</button></div><div class="media-rail">${entries.map((s) => `<button class="media-card" data-action="resume" data-app="${s.app}" data-id="${s.id}">${poster(s.titleId)}<h3>${esc(s.title)}</h3><p>${D.P[s.app].n} · Resume</p></button>`).join("")}</div></section>`
      : "";
  }
  function title() {
    const t = D.T[titleId] || D.T.stranger;
    const offers = M.params.state === "no-providers" ? [] : t.d;
    const related =
      t.k === "artist"
        ? Object.keys(D.T).filter((k) => D.T[k].a === titleId)
        : t.x || [];
    return `<div class="title-bg" style="background-image:url('${A}assets/${artMap[titleId] || "stranger-bg.jpg"}')"></div><div class="fscroll"><div class="view-pad"><button class="viewback" data-action="back">‹ Feria</button><div class="title-layout"><div class="title-cover">${poster(titleId, true)}</div><div class="title-copy"><p>${D.KIND[t.k]}</p><h1>${esc(t.t)}</h1><p>${esc(t.by)}</p><div class="facts"><span>${t.y}</span>${t.f.map((x) => `<span>${esc(x)}</span>`).join("")}</div><p>${esc(t.s)}</p><h2>Where to ${D.VERB[t.k]}</h2><p class="fixture-note">Illustrative provider choices. Availability and plan inclusion must be checked with the service.</p>${M.params.state === "offline" ? '<div class="empty-card"><p>Availability is unavailable in this offline fixture.</p><button data-action="retry-availability">Retry</button></div>' : offers.length ? `<div class="offer-grid">${offers.map(([pk, type]) => `<button class="offer" data-action="open-offer" data-app="${pk}">${glyph(pk)}<span>Open ${D.P[pk].n}<small>${type === "s" ? "Search this service" : "Provider title page"}${apps.includes(pk) ? " · In your apps" : ""}</small></span></button>`).join("")}</div>` : "<p>No provider options are listed for this title yet.</p>"}<button class="save-title ${saved.includes(titleId) ? "saved-indicator" : ""}" data-action="save-title">${saved.includes(titleId) ? "✓ Saved" : "Save"}</button>${related.length ? `<h2>${t.k === "artist" ? "Albums" : "In other media"}</h2><div class="media-rail">${related.map(card).join("")}</div>` : ""}<p class="fixture-note">Listings come from public catalogues and can lag. Whether it plays, and whether it’s included in your plan, is up to the service.</p></div></div></div></div>`;
  }
  function search() {
    const hits = query.trim()
        ? Object.keys(D.T).filter((id) =>
            (D.T[id].t + " " + D.T[id].by)
              .toLowerCase()
              .includes(query.trim().toLowerCase()),
          )
        : [],
      t = D.T[hits[0]];
    return `<div class="fscroll"><div class="view-pad"><button class="viewback" data-action="back">‹ Feria</button><div class="search-header"><h1>Search</h1><input id="feria-search" aria-label="Search Feria catalogues" placeholder="Titles, creators, authors, artists…" value="${esc(query)}"><button data-action="clear-search" aria-label="Clear search">×</button></div>${query ? (hits.length ? `<h2>${hits.length} ${hits.length === 1 ? "match" : "matches"} for “${esc(query)}”</h2><button class="feature-result" data-action="title" data-id="${hits[0]}">${poster(hits[0])}<div class="meta"><h2>${esc(t.t)}</h2><p>${esc(t.by)}</p><p>${t.y} · ${D.KIND[t.k]}</p></div><div class="summary">${esc(t.s)}<p style="margin-top:16px">Available through ${t.d.map((x) => D.P[x[0]].n).join(", ") || "no listed providers"}</p></div></button>${hits.length > 1 ? `<h2>More matches</h2><div class="poster-grid">${hits.slice(1).map(card).join("")}</div>` : ""}` : `<div class="empty-card"><h2>No catalogue matches</h2><p>Nothing in Feria’s current catalogues matches “${esc(query)}”. Try another title or search directly in an app.</p></div>`) : "<h1>Find something worth opening.</h1><p>Feria searches the catalogues and charts already powering your shelves. Start with a title, creator, author, artist, or one of these catalogue-backed shortcuts.</p>"}<h2>${query ? "Try a catalogue shortcut" : "Start somewhere"}</h2><div class="shortcut-row">${D.TRY.map((q) => `<button data-action="query" data-query="${q}">${q}</button>`).join("")}</div><h2>Search directly in an app</h2><p class="fixture-note">Provider search destination · opens a local staged provider page</p><div class="offer-grid">${apps
      .filter((k) => !D.P[k].app)
      .map(
        (k) =>
          `<button class="offer" data-action="provider-search" data-app="${k}">${glyph(k)}<span>${D.P[k].n}<small>Provider search</small></span></button>`,
      )
      .join("")}</div></div></div>`;
  }
  let seeAllId = M.params.shelf || "netflix";
  function seeAll() {
    const s = D.SHELVES.find((x) => x.id === seeAllId),
      entries =
        seeAllId === "continue"
          ? Object.values(sessions)
              .flat()
              .filter((x) => !x.private && x.titleId)
          : s
            ? shelfItems(s)
            : [];
    return `<div class="fscroll"><div class="view-pad"><button class="viewback" data-action="back">‹ Feria</button><h1>${s?.title || "Continue"}</h1><p>${s?.src || "Your Feria"} · ${entries.length} titles</p><div class="poster-grid">${entries.map((entry) => (typeof entry === "string" ? card(entry) : `<button class="media-card" data-action="resume" data-app="${entry.app}" data-id="${entry.id}">${poster(entry.titleId)}<h3>${esc(entry.title)}</h3><p>${D.P[entry.app].n} · Resume</p></button>`)).join("")}</div>${!entries.length ? '<p class="empty-card">Nothing to continue in this tab.</p>' : ""}</div></div>`;
  }
  function openApps() {
    const dlg = $("#apps-dialog");
    dlg.className = "apps-dialog";
    dlg.innerHTML = `<div class="manager-head"><h1 id="apps-heading">Your apps</h1><button data-action="close-apps">Done</button></div><p class="manager-desc">Apps you turn on appear in the row and feed their shelves. Each service keeps its own sign-in inside its page; Colosseum never signs in for you.</p>${status ? `<p class="fixture-note" role="status">${esc(status)}</p>` : ""}${Object.entries(
      D.CATALOG,
    )
      .map(
        ([v, keys]) =>
          `<section class="manager-group"><h2>${v[0].toUpperCase() + v.slice(1)}</h2>${keys.map((k) => `<div class="manager-row">${glyph(k)}<div class="info"><strong>${D.P[k].n}</strong><small>${D.P[k].app ? "Reads only in its own app, so it can’t open here" : D.P[k].d}</small></div>${apps.includes(k) ? `<div class="reorder"><button data-action="app-left" data-app="${k}" aria-label="Move ${D.P[k].n} earlier" ${apps.indexOf(k) === 0 ? "disabled" : ""}>↑</button><button data-action="app-right" data-app="${k}" aria-label="Move ${D.P[k].n} later" ${apps.indexOf(k) === apps.length - 1 ? "disabled" : ""}>↓</button></div>` : ""}<button class="switch ${apps.includes(k) ? "on" : ""}" data-action="toggle-app" data-app="${k}" aria-label="${apps.includes(k) ? "Remove" : "Add"} ${D.P[k].n}" aria-pressed="${apps.includes(k)}"></button></div>`).join("")}</section>`,
      )
      .join("")}`;
    if (!dlg.open) {
      appOpener = document.activeElement;
      dlg.showModal();
      sheetDisposer = M.registerOverlay(dlg, closeApps);
    }
  }
  function closeApps() {
    const d = $("#apps-dialog");
    if (d.open) d.close();
    if (sheetDisposer) sheetDisposer();
    sheetDisposer = null;
    status = "";
    if (view === "apps") view = "home";
    render();
    if (appOpener?.isConnected) appOpener.focus();
    else document.querySelector("[data-action=apps]")?.focus();
  }
  $("#apps-dialog").addEventListener("cancel", (e) => {
    e.preventDefault();
    closeApps();
  });
  function record(r, type) {
    return `<div class="record">${r.titleId && artMap[r.titleId] ? `<img src="${A}assets/${artMap[r.titleId]}" alt="">` : glyph(r.app || provider, "record-brand")}<div class="info"><h3>${esc(r.title)}</h3><p>${esc(r.url || D.P[r.app || provider].d)}</p></div><button data-action="${type === "history" ? "open-history" : "open-bookmark"}" data-id="${r.id}">Open</button>${type === "bookmarks" ? `<button data-action="remove-bookmark" data-id="${r.id}" aria-label="Remove ${esc(r.title)} bookmark">×</button>` : ""}</div>`;
  }
  function account() {
    return `<div class="fscroll"><div class="view-pad"><button class="viewback" data-action="back">‹ Feria</button><h1>Your Feria</h1><nav class="account-tabs">${["history", "highlights", "stats", "apps"].map((t) => `<button data-action="account-tab" data-tab="${t}" class="${accountTab === t ? "on" : ""}">${t[0].toUpperCase() + t.slice(1)}</button>`).join("")}</nav>${M.params.state === "signed-out" ? '<div class="empty-card"><h2>Your Feria is signed out</h2><p>This local fixture does not connect an account. Browse your apps without entering credentials.</p><button data-action="account-local">Use local fixture profile</button></div>' : M.params.state === "offline" ? '<div class="empty-card"><h2>Account storage is unavailable</h2><p>Offline account fixture. Your local apps can still be opened.</p><button data-action="account-local">Retry fixture</button></div>' : accountTab === "apps" ? `<p>Manage your Feria apps. Removing an app does not cancel its subscription.</p><div class="appgrid">${apps.map(appTile).join("")}</div><button data-action="apps">Manage apps</button>` : accountTab === "history" ? `<h2>History</h2>${historyRows.length ? historyRows.map((r) => record(r, "history")).join("") : '<div class="empty-card"><h2>Nothing here yet</h2><p>Open a title in a Feria app to build your local history.</p></div>'}` : accountTab === "highlights" ? `<h2>Highlights</h2><div class="poster-grid">${saved.map(card).join("")}</div>${!saved.length ? '<p class="empty-card">Save a title to keep it in your Feria highlights.</p>' : ""}` : `<h2>Your activity</h2><div class="stat-grid"><article><b>${historyRows.length}</b><p>Local browsing records</p></article><article><b>${saved.length}</b><p>Saved titles</p></article><article><b>${apps.length}</b><p>Your apps</p></article></div><p class="fixture-note">Fixture counts, not actual playback or reading statistics.</p>`}</div></div>`;
  }
  function host() {
    const s = active();
    if (!s) return "";
    return `<section class="host ${full ? "fullsize" : ""}" aria-label="${D.P[provider].n} local browser fixture"><header class="browserbar"><button data-action="minimize">‹ Feria</button><span class="identity">${glyph(provider)}${D.P[provider].n}</span><div class="nav"><button data-action="nav-back" aria-label="Back" ${s.at === 0 ? "disabled" : ""}>‹</button><button data-action="nav-forward" aria-label="Forward" ${s.at >= s.nav.length - 1 ? "disabled" : ""}>›</button><button data-action="reload" aria-label="Reload">↻</button></div><button class="url" data-action="address" aria-expanded="${addressOpen}">URL</button><div class="window"><button data-action="minimize" aria-label="Minimize app">−</button><button data-action="maximize" aria-label="${full ? "Restore" : "Maximize"} app">${full ? "❐" : "□"}</button><button data-action="close-app-request" aria-label="Close app or space">×</button></div></header>${addressOpen ? `<form class="address-row" id="addressform" novalidate><input id="address" aria-label="Address" value="${esc(s.url)}"><button type="submit">Go</button><small>Local navigation fixture</small></form>` : ""}${findOpen ? `<div class="find-row"><input id="find-input" aria-label="Find in page" placeholder="Find in page" value="${esc(findQuery)}"><span id="find-count">${findQuery ? ((s.title + " " + (D.T[s.titleId]?.s || "")).toLowerCase().includes(findQuery.toLowerCase()) ? "1 of 1" : "0 of 0") : "0 of 0"}</span><button data-action="find-next" aria-label="Next match">↓</button><button data-action="find-prev" aria-label="Previous match">↑</button><button data-action="find-close" aria-label="Close find">×</button></div>` : ""}<div class="browser"><aside class="spaces ${sidebar ? "expanded" : "collapsed"}"><div class="sideinside"><div class="sidehead"><button data-action="sidebar" aria-label="${sidebar ? "Collapse" : "Expand"} sidebar">☰</button>${sidebar ? `<strong>${D.P[provider].n}</strong>` : ""}</div><div class="slabel sidecopy">${category()}</div><div class="space-list">${sessions[provider].map((r) => `<div class="space-row"><button class="space ${r.id === s.id && !appPage ? "active" : ""}" data-action="select-space" data-id="${r.id}" title="${esc(r.title)}${r.private ? " · Private" : ""}">${glyph(provider)}<span class="space-title">${r.private ? "◈ " : ""}${esc(r.title)}</span></button><button class="close" data-action="close-space" data-id="${r.id}" aria-label="Close ${esc(r.title)} space">×</button></div>`).join("")}<button class="space" data-action="new-space" aria-label="${category()}+"><span>＋</span><span class="space-title">${category()}+</span></button></div><div class="sidebottom">${[
      ["tabs", "Spaces", "▤"],
      ["bookmarks", "Bookmarks", "☆"],
      ["history", "History", "◷"],
      ["downloads", "Downloads", "↓"],
      ["settings", "Settings", "⚙"],
      ["tools", "Page tools", "⌕"],
      ["extensions", "Extensions", "⊞"],
    ]
      .map(
        ([k, n, icon]) =>
          `<button data-action="host-page" data-page="${k}" title="${n}"><img src="../assets/icons/${{ tabs: "book-library.svg", bookmarks: "star.svg", history: "tracker-history.svg", downloads: "download.svg", settings: "settings.svg", tools: "search.svg", extensions: "extensions.svg" }[k]}" alt=""><span class="sidecopy">${n}</span></button>`,
      )
      .join(
        "",
      )}</div></div></aside><div class="site" id="site-surface">${appPage ? hostPage() : localSite(s)}</div></div></section>`;
  }
  function highlight(text) {
    text = String(text);
    if (!findOpen || !findQuery) return esc(text);
    const i = text.toLowerCase().indexOf(findQuery.toLowerCase());
    return i < 0
      ? esc(text)
      : esc(text.slice(0, i)) +
          "<mark>" +
          esc(text.slice(i, i + findQuery.length)) +
          "</mark>" +
          esc(text.slice(i + findQuery.length));
  }
  function localSite(s) {
    const t =
      D.T[s.titleId] ||
      D.T[
        provider === "spotify" ||
        provider === "ytmusic" ||
        provider === "applemusic"
          ? "showgirl"
          : D.P[provider].v === "read"
            ? "phm"
            : "stranger"
      ];
    return `<div class="local-site" style="zoom:${zoom / 100}">${D.P[provider].v === "watch" ? '<div class="siteart"></div>' : ""}<div class="local-banner"><span>LOCAL PROVIDER FIXTURE · ${s.private ? "Private space" : "No sign-in, streaming or service connection"}</span><span>${zoom}%</span></div><nav class="sitenav">${glyph(provider)}<b>${D.P[provider].n}</b>${(D.P[provider].v === "read" ? ["Home", "Library", "Browse"] : D.P[provider].v === "listen" ? ["Home", "Search", "Your Library"] : ["Home", "Series", "Films", "My List"]).map((n) => `<button data-action="site-destination" data-destination="${n}">${n}</button>`).join("")}</nav><div class="sitecopy"><div class="episode">${D.P[provider].v.toUpperCase()} · ${s.private ? "PRIVATE SPACE" : "STAGED PAGE"}</div><h2>${highlight(s.title)}</h2><p>${highlight(s.titleId ? t.s : "This local page illustrates the provider viewport. Navigation, spaces and app tools can be explored without connecting to " + D.P[provider].n + ".")}</p><div class="siteactions"><button data-action="fixture-play">${s.playing ? "Ⅱ Pause fixture" : D.P[provider].v === "read" ? "Open reading fixture" : '<img class="play-mark" src="../assets/icons/play-dark.svg" alt=""> Play fixture'}</button><button data-action="fixture-detail">More info</button></div></div><p class="sitefoot">${status ? esc(status) : s.playing ? "Fixture media state: playing. No media stream or DRM session is started." : "This is a staged HTML surface, not " + D.P[provider].n + "."}</p></div>`;
  }
  function controlsNav() {
    return `<nav class="page-nav">${["tabs", "bookmarks", "history", "settings"].map((t) => `<button data-action="host-page" data-page="${t}" class="${appPage === t ? "on" : ""}">${t === "tabs" ? "Spaces" : t[0].toUpperCase() + t.slice(1)}</button>`).join("")}<button data-action="return-site">Return to page</button></nav>`;
  }
  function hostPage() {
    const s = active(),
      title = {
        tabs: category(),
        history: "History",
        bookmarks: "Bookmarks",
        downloads: "Downloads",
        settings: "Settings",
        tools: "Page tools",
        requests: "Page request",
        extensions: "Extensions",
        close: "Close " + D.P[provider].n,
      }[appPage];
    let html = "";
    if (appPage === "tabs")
      html = `<div class="request-actions"><button data-action="new-space">New ${category().slice(0, -1)}</button><button data-action="new-private">New private space</button><button data-action="restore-closed" ${closed.some((x) => x.app === provider) ? "" : "disabled"}>Reopen closed space</button></div>${sessions[provider].map((r) => `<div class="record">${glyph(provider, "record-brand")}<div class="info"><h3>${esc(r.title)}${r.private ? " · Private" : ""}</h3><p>${esc(r.url)}</p></div><button data-action="select-space" data-id="${r.id}">Select</button><button data-action="close-space" data-id="${r.id}">Close</button></div>`).join("")}${restorePending ? '<div class="inline-request"><h2>Previous spaces are available</h2><p>Startup stays fresh. Restore this sample set explicitly, or dismiss it.</p><button data-action="restore-previous">Restore previous spaces</button> <button data-action="dismiss-previous">Dismiss</button></div>' : ""}`;
    if (appPage === "history") {
      const rows = historyRows.filter((r) => r.app === provider);
      html = `<p class="fixture-note">Normal spaces only. Private spaces never enter this history.</p><button data-action="clear-history-request" ${rows.length ? "" : "disabled"}>Clear history</button>${rows.map((r) => record(r, "history")).join("")}${!rows.length ? '<p class="empty-card">No browsing history for this app.</p>' : ""}`;
    }
    if (appPage === "bookmarks")
      html = `<button data-action="bookmark">${bookmarks.some((x) => x.app === provider && x.url === s.url) ? "Remove current bookmark" : "Bookmark current page"}</button>${bookmarks
        .filter((x) => x.app === provider)
        .map((r) => record(r, "bookmarks"))
        .join(
          "",
        )}${!bookmarks.some((x) => x.app === provider) ? '<p class="empty-card" style="margin-top:20px">No bookmarks yet.</p>' : ""}`;
    if (appPage === "downloads")
      html = `<p class="fixture-note">Download manager fixture. No file is requested or written.</p><button data-action="add-download">Add sample download</button>${downloads
        .filter((x) => x.app === provider)
        .map(
          (d) =>
            `<div class="record">${glyph(provider, "record-brand")}<div class="info"><h3>${esc(d.name)}</h3><p>${d.state} · ${d.percent}% · ${D.P[d.app].n}</p><div class="download-meter"><span style="width:${d.percent}%"></span></div></div>${d.state === "Downloading" ? `<button data-action="download-complete" data-id="${d.id}">Complete fixture</button><button data-action="download-cancel" data-id="${d.id}">Cancel</button>` : d.state === "Cancelled" || d.state === "Failed" ? `<button data-action="download-retry" data-id="${d.id}">Retry</button>` : "<span>Complete</span>"}</div>`,
        )
        .join(
          "",
        )}${!downloads.some((x) => x.app === provider) ? '<p class="empty-card" style="margin-top:20px">No downloads for this app.</p>' : ""}`;
    if (appPage === "settings")
      html = `${[
        [
          "restore",
          "Offer previous spaces",
          "Restoration is explicit; startup never opens them automatically.",
        ],
        [
          "freeze",
          "Freeze inactive pages",
          "Fixture preference; there is no native browser process here.",
        ],
        [
          "layout",
          "App layout",
          "Provider-specific fixture preference; no live service DOM is modified.",
        ],
        [
          "arrows",
          "Arrow navigation",
          "Navigate available controls with arrow keys.",
        ],
        [
          "compatibility",
          "Compatibility mode",
          "Local preference for the current provider fixture.",
        ],
      ]
        .map(
          ([k, n, d]) =>
            `<div class="setting-row"><label for="setting-${k}"><strong>${n}</strong><small>${d}</small></label><input type="checkbox" data-setting="${k}" id="setting-${k}" ${(["layout", "arrows", "compatibility"].includes(k) ? appOption(k) : prefs[k]) ? "checked" : ""}></div>`,
        )
        .join(
          "",
        )}<div class="setting-row"><label for="engine"><strong>Search engine</strong></label><select id="engine">${["DuckDuckGo", "Google", "Bing"].map((n) => `<option ${prefs.engine === n ? "selected" : ""}>${n}</option>`).join("")}</select></div><div class="setting-row"><div><strong>Site permissions</strong><small>Reset staged permissions for ${D.P[provider].d}.</small></div><button data-action="reset-permissions-request">Reset</button></div>`;
    if (appPage === "tools")
      html = `<div class="setting-row"><strong>Find in page</strong><button data-action="find">Find…</button></div><div class="setting-row"><strong>Zoom</strong><div class="request-actions"><button data-action="zoom-out" aria-label="Zoom out">−</button><button data-action="zoom-reset">${zoom}%</button><button data-action="zoom-in" aria-label="Zoom in">＋</button></div></div><div class="setting-row"><strong>Media</strong><button data-action="mute">${s.muted ? "Unmute fixture" : "Mute fixture"}</button></div><div class="setting-row"><strong>Print</strong><button data-action="print-request">Print fixture…</button></div><div class="setting-row"><strong>Site request demonstrations</strong><div class="request-actions"><button data-action="permission-request">Permission</button><button data-action="auth-request">Authentication</button></div></div>`;
    if (appPage === "extensions")
      html =
        '<div class="empty-card"><h2>Browser extensions are a future capability</h2><p>The current Feria contract reserves this sidebar location. This mock does not install or run browser extensions.</p><button data-action="extension-store">Open Colosseum add-ons</button></div>';
    if (appPage === "close")
      html = `<div class="inline-request"><h2>What would you like to close?</h2><p>The source contract leaves header X behavior open. These separate fixture actions make the target explicit.</p><div class="request-actions"><button data-action="return-site">Cancel</button><button data-action="close-space" data-id="${s.id}">Close selected space</button><button data-action="close-app">Close app and all its spaces</button></div></div>`;
    if (request) html += requestContent();
    return `<div class="host-page"><h1>${title || "Browser"}</h1><p>${D.P[provider].n} · ${s.private ? "Private space" : "Local fixture profile"}</p>${controlsNav()}${html}${status ? `<p class="fixture-note" role="status">${esc(status)}</p>` : ""}</div>`;
  }
  function requestContent() {
    const s = active();
    if (request === "clear-history")
      return '<div class="inline-request"><h2>Clear this app’s browsing history?</h2><p>Removes only staged normal-history records for this app. Open spaces and bookmarks are retained.</p><div class="request-actions"><button data-action="cancel-request">Cancel</button><button data-action="clear-history">Clear history</button></div></div>';
    if (request === "reset")
      return `<div class="inline-request"><h2>Reset permissions for ${D.P[provider].d}?</h2><p>Only staged permission choices will be reset. No browser permission or live service is affected.</p><div class="request-actions"><button data-action="cancel-request">Cancel</button><button data-action="reset-permissions">Reset permissions</button></div></div>`;
    return `<div class="inline-request"><h2>${request === "permission" ? "Allow notifications?" : request === "auth" ? "Authentication requested" : "Print preview"}</h2><p>Origin: ${D.P[provider].d}<br>Space: ${esc(s.title)} · ${s.id}<br>${request === "auth" ? "No credentials are collected. Choose the sample identity to exercise the fixture result." : request === "permission" ? "This is an illustrative notification request; no browser permission will be granted." : "Illustrative one-page preview. No system dialog, printer or file output is started."}</p><div class="request-actions"><button data-action="cancel-request">Cancel</button><button data-action="resolve-request">${request === "permission" ? "Allow fixture" : request === "auth" ? "Use sample identity" : "Complete print fixture"}</button>${request === "permission" ? '<button data-action="deny-request">Block fixture</button>' : ""}</div></div>`;
  }
  function render() {
    document.body.dataset.view = view;
    main.innerHTML =
      view === "host"
        ? host()
        : view === "title"
          ? title()
          : view === "search"
            ? search()
            : view === "seeAll"
              ? seeAll()
              : view === "account"
                ? account()
                : home();
    const sc = $(".fscroll");
    if (sc) sc.scrollTop = positions[view + ":" + lens] || 0;
    if (request && $(".inline-request"))
      $(".inline-request").scrollIntoView({ block: "nearest" });
    M.ready();
  }
  function visit(url, title) {
    const s = active();
    if (!/^https?:\/\//i.test(url)) {
      if (/^[\w.-]+\.[a-z]{2,}(\/.*)?$/i.test(url)) url = "https://" + url;
      else if (/^[\w\s-]+$/.test(url))
        url =
          "https://" +
          (prefs.engine === "Google"
            ? "www.google.com/search?q="
            : prefs.engine === "Bing"
              ? "www.bing.com/search?q="
              : "duckduckgo.com/?q=") +
          encodeURIComponent(url);
      else {
        status =
          "Use an http(s) address or search terms. Local files and script URLs are not accepted.";
        render();
        return;
      }
    }
    s.url = url;
    s.title = title || url.replace(/^https?:\/\//, "");
    s.nav = s.nav.slice(0, s.at + 1).concat({ url: s.url, title: s.title });
    s.at = s.nav.length - 1;
    addHistory(s);
    status = "Navigation staged locally; no website was requested.";
    addressOpen = false;
    appPage = "";
    persist();
    render();
  }
  function closeSpace(id) {
    const arr = sessions[provider],
      s = arr.find((x) => x.id === id);
    if (!s) return;
    if (!s.private) closed.unshift({ ...s });
    sessions[provider] = arr.filter((x) => x.id !== id);
    if (selected[provider] === id)
      selected[provider] = sessions[provider][0]?.id;
    if (!sessions[provider].length) {
      M.closeSession("feria-app-" + provider);
      view = "home";
      full = false;
    }
    persist();
    render();
  }
  function minimize() {
    const s = active();
    if (s?.playing) {
      s.playing = false;
      s.autoPaused = true;
    }
    view = "home";
    full = false;
    persist();
    render();
  }
  main.addEventListener("submit", (e) => {
    if (e.target.id === "addressform") {
      e.preventDefault();
      visit($("#address").value.trim());
    }
  });
  document.addEventListener("click", (e) => {
    const b = e.target.closest("[data-action]");
    if (!b || b.disabled) return;
    const a = b.dataset.action,
      id = b.dataset.id,
      pk = b.dataset.app;
    const s = active();
    if (a === "home" || a === "minimize") {
      if (view === "host") minimize();
      else {
        view = "home";
        render();
      }
    }
    if (a === "back") back();
    if (a === "apps") openApps();
    if (a === "close-apps") closeApps();
    if (a === "search") navigate("search");
    if (a === "account") navigate("account");
    if (a === "title") {
      remember();
      stack.push({ view, lens, titleId, provider, appPage });
      titleId = id;
      view = "title";
      render();
    }
    if (a === "open-app") openApp(pk);
    if (a === "open-offer") openApp(pk, titleId);
    if (a === "provider-search") {
      openApp(pk);
      visit(
        "https://" + D.P[pk].sp + encodeURIComponent(query),
        "Search: " + query,
      );
    }
    if (a === "resume") {
      openApp(pk);
      selected[pk] = id;
      render();
    }
    if (a === "lens") {
      remember();
      lens = b.dataset.lens;
      render();
    }
    if (a === "scroll-feature") {
      $(".fscroll").scrollTo({ top: 0 });
    }
    if (a === "scroll-apps") {
      $(".fscroll").scrollTo({ top: $("#your-apps").offsetTop - 12 });
    }
    if (a === "seeall") {
      seeAllId = id;
      navigate("seeAll");
    }
    if (a === "save-title") {
      saved = saved.includes(titleId)
        ? saved.filter((x) => x !== titleId)
        : saved.concat(titleId);
      persist();
      render();
    }
    if (a === "query") {
      query = b.dataset.query;
      render();
      $("#feria-search").focus();
    }
    if (a === "clear-search") {
      query = "";
      render();
      $("#feria-search").focus();
    }
    if (a === "toggle-app") {
      apps = apps.includes(pk) ? apps.filter((x) => x !== pk) : apps.concat(pk);
      persist();
      const y = $("#apps-dialog").scrollTop;
      openApps();
      $("#apps-dialog").scrollTop = y;
    }
    if (a === "app-left" || a === "app-right") {
      const i = apps.indexOf(pk),
        j = i + (a === "app-left" ? -1 : 1);
      [apps[i], apps[j]] = [apps[j], apps[i]];
      persist();
      const y = $("#apps-dialog").scrollTop;
      openApps();
      $("#apps-dialog").scrollTop = y;
    }
    if (a === "account-tab") {
      accountTab = b.dataset.tab;
      render();
    }
    if (a === "account-local" || a === "retry-availability") {
      M.params.state = "";
      render();
    }
    if (a === "sidebar") {
      sidebar = !sidebar;
      render();
    }
    if (a === "address") {
      addressOpen = !addressOpen;
      render();
      if (addressOpen) $("#address").focus();
    }
    if (a === "nav-back" || a === "nav-forward") {
      s.at += a === "nav-back" ? -1 : 1;
      s.url = s.nav[s.at].url;
      s.title = s.nav[s.at].title;
      appPage = "";
      persist();
      render();
    }
    if (a === "reload") {
      status =
        "Reloaded local fixture; live " +
        D.P[provider].n +
        " was not contacted.";
      render();
    }
    if (a === "maximize") {
      full = !full;
      render();
    }
    if (a === "select-space") {
      selected[provider] = id;
      appPage = "";
      request = "";
      persist();
      render();
    }
    if (a === "new-space" || a === "new-private") {
      newSpace(a === "new-private");
      render();
    }
    if (a === "close-space") closeSpace(id);
    if (a === "close-app-request") {
      appPage = "close";
      render();
    }
    if (a === "close-app") {
      (sessions[provider] || [])
        .filter((x) => !x.private)
        .forEach((x) => closed.unshift(x));
      delete sessions[provider];
      M.closeSession("feria-app-" + provider);
      view = "home";
      full = false;
      persist();
      render();
    }
    if (a === "host-page") {
      appPage = b.dataset.page;
      request = "";
      status = "";
      render();
    }
    if (a === "return-site") {
      appPage = "";
      request = "";
      render();
    }
    if (a === "site-destination")
      visit(
        "https://" +
          D.P[provider].d +
          "/" +
          b.dataset.destination.toLowerCase().replace(/ /g, "-"),
        b.dataset.destination,
      );
    if (a === "fixture-play") {
      s.playing = !s.playing;
      s.manualPaused = !s.playing;
      status = s.playing
        ? "Fixture media state: playing. No remote media is loaded."
        : "Fixture media state: paused by you.";
      persist();
      render();
    }
    if (a === "fixture-detail") {
      if (s.titleId) {
        titleId = s.titleId;
        navigate("title");
      } else {
        status =
          "Local " +
          D.P[provider].n +
          " app fixture; there is no linked catalogue title on this page.";
        render();
      }
    }
    if (a === "bookmark") {
      const found = bookmarks.find(
        (x) => x.app === provider && x.url === s.url,
      );
      bookmarks = found
        ? bookmarks.filter((x) => x.id !== found.id)
        : bookmarks.concat({
            id: "bookmark-" + ++serial,
            app: provider,
            url: s.url,
            title: s.title,
            titleId: s.titleId,
          });
      persist();
      render();
    }
    if (a === "remove-bookmark") {
      bookmarks = bookmarks.filter((x) => x.id !== id);
      persist();
      render();
    }
    if (a === "open-history" || a === "open-bookmark") {
      const r = (a === "open-history" ? historyRows : bookmarks).find(
        (x) => x.id === id,
      );
      if (r) {
        if (view !== "host" || r.app !== provider) openApp(r.app, r.titleId);
        visit(r.url, r.title);
      }
    }
    if (a === "clear-history-request") {
      request = "clear-history";
      render();
    }
    if (a === "clear-history") {
      historyRows = historyRows.filter((x) => x.app !== provider);
      request = "";
      persist();
      render();
    }
    if (a === "reset-permissions-request") {
      request = "reset";
      render();
    }
    if (a === "reset-permissions") {
      request = "";
      status = "Staged site permissions reset.";
      render();
    }
    if (a === "cancel-request") {
      request = "";
      status = "Request cancelled.";
      render();
    }
    if (a === "resolve-request" || a === "deny-request") {
      status =
        a === "deny-request"
          ? "Fixture request blocked."
          : "Fixture request completed for " +
            s.title +
            ". No system action occurred.";
      request = "";
      render();
    }
    if (["permission-request", "auth-request", "print-request"].includes(a)) {
      request = a.split("-")[0];
      render();
    }
    if (a === "find") {
      findOpen = true;
      appPage = "";
      render();
      $("#find-input").focus();
    }
    if (a === "find-close") {
      findOpen = false;
      render();
    }
    if (a === "find-next" || a === "find-prev") {
      const mark = $("#site-surface mark");
      if (mark) mark.scrollIntoView({ block: "nearest" });
    }
    if (a === "zoom-in" || a === "zoom-out" || a === "zoom-reset") {
      zoom =
        a === "zoom-reset"
          ? 100
          : Math.min(200, Math.max(50, zoom + (a === "zoom-in" ? 10 : -10)));
      render();
    }
    if (a === "mute") {
      s.muted = !s.muted;
      persist();
      render();
    }
    if (a === "extension-store") M.go("extensions");
    if (a === "add-download") {
      downloads.push({
        id: "download-" + ++serial,
        app: provider,
        name: "Feria sample page.html",
        state: M.params.state === "download-error" ? "Failed" : "Downloading",
        percent: 35,
      });
      persist();
      render();
    }
    if (a.startsWith("download-")) {
      const d = downloads.find((x) => x.id === id);
      if (d) {
        d.state =
          a === "download-complete"
            ? "Complete"
            : a === "download-cancel"
              ? "Cancelled"
              : "Downloading";
        d.percent =
          a === "download-complete"
            ? 100
            : a === "download-retry"
              ? 35
              : d.percent;
        persist();
        render();
      }
    }
    if (a === "restore-closed") {
      const r = closed.find((x) => x.app === provider);
      if (r) {
        closed = closed.filter((x) => x.id !== r.id);
        (sessions[provider] ||= []).push(r);
        selected[provider] = r.id;
        appPage = "";
        persist();
        render();
      }
    }
    if (a === "restore-previous") {
      newSpace(false, "wednesday");
      restorePending = false;
      persist();
      render();
    }
    if (a === "dismiss-previous") {
      restorePending = false;
      render();
    }
  });
  document.addEventListener("input", (e) => {
    if (e.target.id === "feria-search") {
      query = e.target.value;
      const pos = e.target.selectionStart;
      render();
      $("#feria-search").focus();
      $("#feria-search").setSelectionRange(pos, pos);
    }
    if (e.target.id === "find-input") {
      findQuery = e.target.value;
      const s = active();
      $("#find-count").textContent =
        findQuery &&
        (s.title + " " + (D.T[s.titleId]?.s || ""))
          .toLowerCase()
          .includes(findQuery.toLowerCase())
          ? "1 of 1"
          : "0 of 0";
      $("#site-surface").innerHTML = localSite(s);
    }
  });
  document.addEventListener("change", (e) => {
    if (e.target.dataset.setting) {
      const k = e.target.dataset.setting;
      if (["layout", "arrows", "compatibility"].includes(k)) {
        appPrefs[provider] ||= {
          layout: true,
          arrows: true,
          compatibility: false,
        };
        appPrefs[provider][k] = e.target.checked;
      } else prefs[k] = e.target.checked;
      persist();
    }
    if (e.target.id === "engine") {
      prefs.engine = e.target.value;
      persist();
    }
  });
  window.addEventListener(
    "keydown",
    (e) => {
      if (e.key === "Escape" && !$("#apps-dialog").open) {
        e.preventDefault();
        e.stopImmediatePropagation();
        if (full) {
          full = false;
          render();
        } else if (findOpen) {
          findOpen = false;
          render();
        } else if (addressOpen) {
          addressOpen = false;
          render();
        } else back();
      }
      if (view === "host" && e.ctrlKey) {
        if (e.key.toLowerCase() === "t") {
          e.preventDefault();
          newSpace(e.shiftKey);
          render();
        }
        if (e.key.toLowerCase() === "w") {
          e.preventDefault();
          closeSpace(active().id);
        }
        if (e.key.toLowerCase() === "f") {
          e.preventDefault();
          findOpen = true;
          appPage = "";
          render();
          $("#find-input").focus();
        }
        if (e.key.toLowerCase() === "h") {
          e.preventDefault();
          appPage = "history";
          render();
        }
        if (e.key.toLowerCase() === "j") {
          e.preventDefault();
          appPage = "downloads";
          render();
        }
      }
      if (
        ["ArrowRight", "ArrowLeft"].includes(e.key) &&
        !["INPUT", "SELECT", "TEXTAREA"].includes(
          document.activeElement.tagName,
        )
      ) {
        const parent = document.activeElement.closest(
          ".appgrid,.media-rail,.lensbar,.poster-grid",
        );
        if (parent) {
          const buttons = [...parent.querySelectorAll("button:not(:disabled)")],
            i = buttons.indexOf(document.activeElement),
            n = buttons[i + (e.key === "ArrowRight" ? 1 : -1)];
          if (n) {
            e.preventDefault();
            n.focus();
            n.scrollIntoView({ block: "nearest", inline: "nearest" });
          }
        }
      }
    },
    true,
  );
  if (view === "host") {
    if (!sessions[provider]?.length) newSpace(false, M.params.id || "");
    selected[provider] = sessions[provider].some(
      (x) => x.id === selected[provider],
    )
      ? selected[provider]
      : sessions[provider][0].id;
    appPage = M.params.panel || "";
    M.openSession({
      kind: "app",
      id: "feria-app-" + provider,
      title: D.P[provider].n,
      route: "feria.host",
      params: { view: "host", app: provider },
    });
  }
  render();
  if (view === "apps") openApps();
})();
