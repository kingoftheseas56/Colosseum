(function () {
  "use strict";
  const M = MockUI,
    $ = (s) => document.querySelector(s),
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
  const data = [
    [
      "opensubtitles",
      "OpenSubtitles v3",
      "Subtitles in every language, matched to the exact episode playing.",
      "subtitles language",
    ],
    [
      "streaming-catalogs",
      "Streaming Catalogs",
      "Streaming service catalogues for movies and shows.",
      "catalog movies",
    ],
    [
      "comet",
      "Comet",
      "Fast, filterable torrent sources with clean quality labels.",
      "stream movie debrid",
      true,
    ],
    [
      "anime-kitsu",
      "Anime Kitsu",
      "The anime shelf’s brain — proper seasons, splits and episode orders.",
      "anime metadata",
    ],
    [
      "torrentio",
      "Torrentio",
      "Play sources from twelve indexers, sorted by quality and health.",
      "stream movie anime",
      true,
    ],
    [
      "cinemeta",
      "Cinemeta",
      "The canonical catalog — every movie and show row Theatre wakes up with.",
      "catalog metadata",
    ],
    [
      "mediafusion",
      "MediaFusion",
      "A wide net — torrents and usenet under one roof.",
      "stream movie regional",
      true,
    ],
    [
      "aiostreams",
      "AIOStreams",
      "Many source addons wrapped into one, with your own ranking rules.",
      "stream movie debrid",
      true,
    ],
    [
      "peerflix",
      "Peerflix",
      "Lean torrent sources with a reputation for just working.",
      "stream movie",
    ],
    [
      "notorrent",
      "NoTorrent",
      "Direct HTTP streams — no torrents involved at all.",
      "stream movie",
    ],
    [
      "webstreamr",
      "WebStreamr",
      "Free web streams gathered from open hosts.",
      "stream movie regional",
      true,
    ],
    [
      "netflix-catalog",
      "Netflix Catalog",
      "What’s new and leaving on Netflix, as browsable rows.",
      "catalog movie",
    ],
    [
      "flixpatrol",
      "FlixPatrol Top 10",
      "The world’s top-ten charts, as browsable rows.",
      "catalog movie regional",
    ],
    [
      "aiolists",
      "AIOLists",
      "Your own watchlists from anywhere, turned into home rows.",
      "catalog",
      true,
    ],
    [
      "marvel",
      "Marvel Universe",
      "Every Marvel film and show in release or story order.",
      "catalog movie",
    ],
    [
      "dcaddon",
      "DC Universe",
      "The DC canon, ordered and browsable.",
      "catalog movie",
    ],
    [
      "morelikethis",
      "More Like This",
      "A similar-titles row on every detail page.",
      "metadata catalog",
    ],
    [
      "subsource",
      "SubSource",
      "A second deep well of subtitles when the first runs dry.",
      "subtitles language",
    ],
    [
      "subdl",
      "SubDL",
      "Community subtitles with strong regional coverage.",
      "subtitles Hindi",
    ],
    [
      "rtngz",
      "Ratings",
      "IMDb, Rotten Tomatoes and Letterboxd scores on every page.",
      "metadata",
    ],
    [
      "streailer",
      "Streailer",
      "Trailers on demand for anything in the catalog.",
      "metadata",
    ],
    [
      "meteor",
      "Meteor",
      "A second opinion for anime — alternate catalog and sources.",
      "anime metadata",
    ],
    [
      "jikan",
      "Jikan",
      "Anime catalogues and episode details.",
      "anime catalog",
    ],
    [
      "tmdb-addon",
      "The Movie Database",
      "Movies and series with detailed metadata.",
      "metadata catalog",
    ],
  ].map(([id, name, description, categories, configurable], i) => ({
    id,
    name,
    description,
    categories,
    configurable: !!configurable,
    stars: 500 - i * 11,
  }));
  const by = (id) => data.find((a) => a.id === id),
    rows = [
      [
        "Essentials",
        "The five a fresh Colosseum needs.",
        [
          "opensubtitles",
          "streaming-catalogs",
          "comet",
          "anime-kitsu",
          "torrentio",
        ],
      ],
      [
        "Most popular",
        "The most starred add-ons on stremio-addons.net.",
        [
          "cinemeta",
          "mediafusion",
          "aiostreams",
          "peerflix",
          "webstreamr",
          "netflix-catalog",
          "flixpatrol",
          "aiolists",
        ],
      ],
      [
        "Play sources",
        "Where your movies and shows actually stream from.",
        ["mediafusion", "aiostreams", "peerflix", "notorrent", "webstreamr"],
      ],
      [
        "Something to watch",
        "New rows and catalogues for Theatre.",
        [
          "cinemeta",
          "netflix-catalog",
          "flixpatrol",
          "aiolists",
          "marvel",
          "dcaddon",
        ],
      ],
      [
        "Anime",
        "Seasons, catalogues and sources for anime.",
        ["meteor", "jikan"],
      ],
      [
        "World cinema",
        "Korean, Chinese, Japanese and Indian film and drama.",
        ["flixpatrol", "webstreamr"],
      ],
      [
        "Subtitles",
        "Every language, matched to what is playing.",
        ["subsource", "subdl"],
      ],
      [
        "Better titles",
        "Posters, ratings, episode orders and cleaner metadata.",
        ["rtngz", "streailer", "morelikethis", "tmdb-addon"],
      ],
    ];
  let installed = M.get("extensions.installed", ["opensubtitles", "cinemeta"]),
    house = M.get("extensions.house", ["nyaa", "tankoyomi", "getcomics"]),
    busy = "",
    error = "";
  let view = M.params.view || "store",
    row = Number(M.params.row || 0),
    query = M.params.q || "",
    tab = M.params.tab || "configuration",
    lang = M.params.language || "en";
  const board = $("#board"),
    page = $("#page"),
    dlg = $("#setup");
  let dispose = null,
    opener = null,
    setupAddon = null;
  function logo(a) {
    const path = AddonLogos.logoFor(a.id, a.name);
    return path
      ? `<img src="${path}" alt="${esc(a.name)} logo">`
      : `<span aria-label="No bundled logo">${esc(a.name[0])}</span>`;
  }
  function card(a) {
    return `<button class="addon ${busy === a.id ? "busy" : ""}" data-action="addon" data-id="${a.id}" aria-label="${esc(a.name)}${installed.includes(a.id) ? ", installed" : ""}" style="--tone1:hsl(${[...a.name].reduce((v, c) => (v * 31 + c.charCodeAt(0)) % 360, 0)} 23% 21%);--tone2:#11151c"><div class="addon-art"><div class="logo-stage">${logo(a)}</div>${busy === a.id ? '<span class="install-state">Adding…</span>' : installed.includes(a.id) ? '<span class="install-state installed">✓ Installed</span>' : ""}</div><h3>${esc(a.name)}</h3><p>${esc(a.description)}</p></button>`;
  }
  function hero() {
    const items = [
      ["tributes", "Tributes", "icons/universes-atom.svg"],
      ["grand", "Grand Database", "addon-logos/colosseum-grand-database.png"],
      ["nyaa", "Nyaa", "addon-logos/nyaa.png"],
      ["tankoyomi", "Tankoyomi", "addon-logos/tankoyomi.png"],
      ["getcomics", "GetComics", "addon-logos/getcomics.png"],
      ["tankorent", "Tankorent", "addon-logos/tankorent.png"],
      ["libgen", "LibGen", "addon-logos/libgen.ico"],
      ["audiobookbay", "AudioBookBay", "addon-logos/audiobookbay.png"],
    ];
    return `<section class="house"><img class="art" src="../assets/extensions/house/${M.params.banner === "dcau" ? "dcau.jpg" : M.params.banner === "cosmere" ? "cosmere.jpg" : "starwars-banner.jpg"}" alt=""><button class="search" data-action="search"><img src="../assets/icons/search.svg" alt="">Search</button><div class="tray">${items.map(([id, name, src]) => `<div class="house-app"><button class="app-tile" data-action="house" data-house="${id}" aria-label="${name}" ${!["tributes", "tankoyomi"].includes(id) ? "disabled" : ""}><img src="../assets/${src}" alt="${name}"></button><div class="app-caption"><span class="name-chip">${name}</span>${!["tributes", "grand"].includes(id) ? `<button class="toggle ${house.includes(id) ? "on" : ""}" data-action="toggle-house" data-id="${id}" aria-label="${house.includes(id) ? "Disable" : "Enable"} ${name}" aria-pressed="${house.includes(id)}">${house.includes(id) ? "✓" : "+"}</button>` : ""}</div></div>`).join("")}</div></section>`;
  }
  function store() {
    return (
      hero() +
      (M.params.state === "offline"
        ? '<div class="status" role="status">The Store couldn’t reach stremio-addons.net (offline fixture). <button data-action="retry-store">Try again</button></div>'
        : M.params.state === "loading"
          ? '<div class="status">Opening the Store… <button data-action="retry-store">Load fixture catalogue</button></div>'
          : rows
              .map(
                (r, i) =>
                  `<section class="rail" data-rail="${i}"><div class="rail-head"><h2>${r[0]}</h2><p>${r[1]}</p>${i ? `<button class="seeall" data-action="seeall" data-row="${i}">See all ›</button>` : ""}</div><div class="rail-window"><div class="cards">${r[2].map((id) => card(by(id))).join("")}</div></div></section>`,
              )
              .join("")) +
      `<p class="footer">Community add-ons and stars from stremio-addons.net. Their authors run them, not Colosseum.<br>Offline catalogue fixture. Installations and source states are local to this mock.</p>`
    );
  }
  function results() {
    const items =
      view === "search"
        ? query.trim()
          ? data.filter((a) =>
              query
                .toLowerCase()
                .trim()
                .split(/\s+/)
                .every((w) =>
                  (a.name + " " + a.categories + " " + a.description)
                    .toLowerCase()
                    .includes(w),
                ),
            )
          : []
        : rows[row][2].map(by);
    return `<div class="section-head"><button data-action="store">‹ Store</button>${view === "search" ? `<div class="searchline"><input id="searchfield" class="searchbox" aria-label="Search add-ons" placeholder="Search ${data.length} add-ons" value="${esc(query)}"><button data-action="clear-search" aria-label="Clear search">×</button></div>` : `<h1>${rows[row][0]}</h1>`}<p id="resultcount">${view === "search" ? (query.trim() ? items.length + " add-ons match." : "Names, what they do, or a category: anime, subtitles, debrid, Hindi…") : rows[row][1] + " " + items.length + " add-ons."}</p></div><div class="addon-grid" id="resultgrid">${items.map(card).join("")}</div>${query && !items.length ? '<p class="empty">No add-on matches that. Try a site name, a language or a genre.</p>' : ""}`;
  }
  const languages = [
    ["en", "English"],
    ["es", "Español"],
    ["pt", "Português (Brasil)"],
    ["fr", "Français"],
  ];
  const baseProviders = {
    en: [["weebcentral", "WeebCentral", "weebcentral.com"]],
    es: [
      ["zonatmo", "ZonaTMO", "zonatmo.org"],
      ["niadd", "NiAdd", "es.niadd.com"],
    ],
    pt: [
      ["taiyo", "Taiyō", "taiyo.moe"],
      ["manga-online", "Manga Online", "mangaonline.green"],
      ["manga-night", "Manga Night", "www.manganight.com.br"],
    ],
    fr: [
      ["sushiscan-fr", "Sushiscan.fr", "sushiscan.fr"],
      ["lelscan-vf", "Lelscan-VF", "lelscanfr.com"],
      ["lelmanga", "Lelmanga", "lelmanga.com"],
      ["mangamoins", "MangaMoins", "mangamoins.com"],
    ],
  };
  let ladders = M.get(
      "extensions.ladders",
      Object.fromEntries(
        Object.entries(baseProviders).map(([k, a]) => [
          k,
          a.map(([id, name, host]) => ({ id, name, host, enabled: true })),
        ]),
      ),
    ),
    defaultLang = M.get("extensions.defaultLanguage", "en");
  function config() {
    const providers = ladders[lang] || [];
    return `<div class="config-hero"><img src="../assets/addon-logos/tankoyomi.png" alt="Tankoyomi"><div><small>COLOSSEUM · STORE · CONFIGURATION</small><h1>Tankoyomi</h1><p>Configure chapter languages and the source order Tankoyomi uses inside Chapter Mode. Auto-pick follows the provider ladder from top to bottom and never crosses into another language.</p></div><button data-action="toggle-house" data-id="tankoyomi" aria-pressed="${house.includes("tankoyomi")}">${house.includes("tankoyomi") ? "✓ Enabled" : "Off"}</button></div><div class="tabs"><button data-action="config-tab" data-tab="configuration" class="${tab === "configuration" ? "selected" : ""}">Configuration</button><button data-action="config-tab" data-tab="about" class="${tab === "about" ? "selected" : ""}">About</button><button data-action="store">‹ Store</button></div>${tab === "about" ? `<div class="panel about"><h2>About Tankoyomi</h2><p>Tankoyomi is the language-aware chapter router used by Colosseum Chapter Mode. Its provider inventory comes from the bundled manifest, while your language, enablement, and priority choices are saved locally.</p><p>Auto-pick tries the highest enabled provider first, then continues down the selected language’s ladder. Unsupported languages never fall through to another language.</p><p class="footer">Language and provider names match the bundled Tankoyomi manifest. Enablement and priority changes are local fixture state; no chapter service is contacted.</p></div>` : `<div class="config-grid"><section class="panel"><h2>Languages</h2><p>Choose which chapter languages Tankoyomi can use.</p>${languages.map(([code, name]) => `<button data-action="language" data-language="${code}" class="lang ${code === lang ? "selected" : ""}"><span>${name}<br><small>${code} · ${ladders[code].length} sources</small></span><small>${code === defaultLang ? "DEFAULT" : ladders[code].filter((a) => a.enabled).length + " active"}</small></button>`).join("")}<label class="default-lang">Default chapter language<select id="defaultlang">${languages.map(([code, name]) => `<option value="${code}" ${code === defaultLang ? "selected" : ""}>${name}</option>`).join("")}</select></label></section><section class="panel"><p style="color:var(--gold);letter-spacing:1.5px;font-size:11px">AUTO-PICK ROUTE</p><h2 style="margin-top:15px">${languages.find((a) => a[0] === lang)?.[1] || lang}</h2><p>${providers.length} configured sources · same-language fallback only</p><div class="ladder-head"><small>Highest active provider is tried first. Reorder the ladder to change priority.</small><button data-action="reset-order">Reset order</button></div>${providers.map((a, i) => `<div class="provider"><span class="rank">${i + 1}</span>${a.id === "weebcentral" ? '<img src="../assets/addon-logos/weebcentral.png" alt="">' : a.id === "mangaplus" ? '<img src="../assets/feria/brand-logos/mangaplus.png" alt="">' : ""}<div class="info"><strong>${a.name}</strong><small class="${a.enabled ? "enabled" : ""}">${a.enabled ? "ACTIVE" : "DISABLED"} · ${a.host}</small><small>Rank ${i + 1} · ${a.id}</small></div><button data-action="provider-toggle" data-index="${i}" aria-label="${a.enabled ? "Disable" : "Enable"} ${a.name}">${a.enabled ? "✓" : "+"}</button><button data-action="provider-up" data-index="${i}" ${i === 0 ? "disabled" : ""} aria-label="Move ${a.name} up">↑</button><button data-action="provider-down" data-index="${i}" ${i === providers.length - 1 ? "disabled" : ""} aria-label="Move ${a.name} down">↓</button></div>`).join("")}${!providers.some((a) => a.enabled) ? '<p class="empty">No providers are enabled for this language.</p>' : ""}</section></div><section class="panel"><h2>Behavior</h2><div class="behavior"><div><strong>Language first</strong><p>Auto-pick stays inside your selected language.</p></div><div><strong>Priority</strong><p>The highest active provider is tried first.</p></div><div><strong>Fallback</strong><p>Continue down the enabled source ladder.</p></div></div><p style="margin-top:18px">Scan quality is descriptive, not scored. The source ladder is limited to the selected language.</p></section>`}`;
  }
  function render() {
    const y = page.scrollTop;
    board.innerHTML =
      view === "config" ? config() : view === "store" ? store() : results();
    if (error)
      board.insertAdjacentHTML(
        "afterbegin",
        `<div class="status" role="alert">${esc(error)} <button data-action="dismiss-error">Dismiss</button></div>`,
      );
    page.scrollTop = y;
    M.ready();
  }
  function change(v) {
    view = v;
    page.scrollTop = 0;
    render();
    if (v === "search") $("#searchfield").focus();
  }
  function save() {
    M.set("extensions.installed", installed);
    M.set("extensions.house", house);
    M.set("extensions.ladders", ladders);
    M.set("extensions.defaultLanguage", defaultLang);
  }
  function install(a) {
    if (busy) return;
    busy = a.id;
    render();
    if (dlg.open) $("#setup-status").textContent = "Adding…";
    setTimeout(() => {
      busy = "";
      if (M.params.state === "install-error") {
        error =
          "Couldn't add it: fixture provider unavailable. Try again from the card.";
        if (dlg.open) {
          $("#setup-status").textContent = error;
        } else render();
        M.params.state = "";
        return;
      }
      error = "";
      if (!installed.includes(a.id)) installed.push(a.id);
      save();
      closeSetup();
      render();
      document
        .querySelector('[data-action=addon][data-id="' + a.id + '"]')
        ?.focus();
    }, 450);
  }
  function closeSetup() {
    if (!dlg.open) return;
    dlg.close();
    if (dispose) dispose();
    dispose = null;
    opener?.focus();
  }
  function setup(a) {
    setupAddon = a;
    opener = document.activeElement;
    dlg.innerHTML = `<div class="setup-head">${logo(a)}<h2 id="setup-title">Set up ${esc(a.name)}</h2><button data-action="close-setup" aria-label="Close setup">×</button></div><div class="setup-content"><h3>This setup page won’t open inside Colosseum.</h3><p>Try again, or set it up in your web browser, copy the add-on link it gives you, and paste it below.</p><button data-action="retry-setup">Try again</button><p class="footer">Staged setup surface. This mock never opens a provider, reads your clipboard, or transmits a link.</p></div><div class="setup-foot"><label for="addonlink">Link only?</label><input id="addonlink" placeholder="Paste the add-on link the page gave you" autocomplete="off"><button data-action="install-link">Install</button><button data-action="paste-link">Paste link</button><button data-action="install-without">Install without setup</button></div><div id="setup-status" class="setup-status" role="status"></div>`;
    dlg.showModal();
    dispose = M.registerOverlay(dlg, closeSetup);
    dlg.querySelector("[data-action=close-setup]").focus();
  }
  dlg.addEventListener("cancel", (e) => {
    e.preventDefault();
    closeSetup();
  });
  document.addEventListener("click", (e) => {
    const b = e.target.closest("[data-action]");
    if (!b) return;
    const a = b.dataset.action,
      id = b.dataset.id;
    if (a === "back") {
      if (view === "store") M.back();
      else change("store");
    }
    if (a === "search") change("search");
    if (a === "store") change("store");
    if (a === "seeall") {
      row = Number(b.dataset.row);
      change("row");
    }
    if (a === "house") {
      const h = b.dataset.house;
      if (h === "tributes") M.go("universes");
      else if (h === "tankoyomi") change("config");
      else {
        house = house.includes(h)
          ? house.filter((x) => x !== h)
          : house.concat(h);
        save();
        render();
      }
    }
    if (a === "toggle-house") {
      house = house.includes(id)
        ? house.filter((x) => x !== id)
        : house.concat(id);
      save();
      render();
    }
    if (a === "addon") {
      const item = by(id);
      if (item.configurable) setup(item);
      else if (!installed.includes(id)) install(item);
    }
    if (a === "clear-search") {
      query = "";
      render();
      $("#searchfield").focus();
    }
    if (a === "retry-store") {
      M.params.state = "";
      render();
    }
    if (a === "config-tab") {
      tab = b.dataset.tab;
      render();
    }
    if (a === "language") {
      lang = b.dataset.language;
      render();
    }
    if (a === "reset-order") {
      ladders[lang] = baseProviders[lang].map(([id, name, host]) => ({
        id,
        name,
        host,
        enabled: ladders[lang].find((p) => p.id === id)?.enabled !== false,
      }));
      save();
      render();
    }
    if (a.startsWith("provider-")) {
      const i = Number(b.dataset.index),
        arr = ladders[lang];
      if (a === "provider-toggle") arr[i].enabled = !arr[i].enabled;
      else {
        const j = i + (a === "provider-up" ? -1 : 1);
        [arr[i], arr[j]] = [arr[j], arr[i]];
      }
      save();
      render();
    }
    if (a === "close-setup") closeSetup();
    if (a === "retry-setup")
      $("#setup-status").textContent =
        "The local fixture has no embedded setup service. You can try the staged link flow below.";
    if (a === "paste-link") {
      $("#addonlink").value = "https://fixture.example/manifest.json";
      $("#setup-status").textContent =
        "Sample add-on link inserted; your clipboard was not accessed.";
    }
    if (a === "install-link") {
      const v = $("#addonlink").value.trim();
      if (!/^(https?:\/\/|stremio:\/\/).+\/manifest\.json(?:\?.*)?$/.test(v))
        $("#setup-status").textContent =
          "That isn’t an add-on link. Use a manifest.json link.";
      else install(setupAddon);
    }
    if (a === "install-without") install(setupAddon);
    if (a === "dismiss-error") {
      error = "";
      render();
    }
  });
  document.addEventListener("input", (e) => {
    if (e.target.id !== "searchfield") return;
    query = e.target.value;
    const pos = e.target.selectionStart;
    render();
    $("#searchfield").focus();
    $("#searchfield").setSelectionRange(pos, pos);
  });
  document.addEventListener("change", (e) => {
    if (e.target.id === "defaultlang") {
      defaultLang = e.target.value;
      save();
      render();
    }
  });
  window.addEventListener(
    "keydown",
    (e) => {
      if (e.key === "Escape" && !dlg.open && view !== "store") {
        e.preventDefault();
        e.stopImmediatePropagation();
        change("store");
      }
    },
    true,
  );
  if (M.params.state === "empty-providers")
    ladders[lang].forEach((p) => (p.enabled = false));
  render();
  if (M.params.setup) setup(by(M.params.setup) || by("comet"));
})();
