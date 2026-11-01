"use strict";
// Standalone, offline fitting mock. No imports, network calls, app/provider operations.
const ROOT = "../../../../reference/desktop/assets/";
const $ = (id) => document.getElementById(id);
const app = $("app"), scroll = $("page-scroll"), content = $("page-content");
const film = document.body.dataset.kind === "film";
const params = new URLSearchParams(location.search);
const capture = params.get("capture") === "1" || document.body.hasAttribute("data-capture");
if (capture) document.body.classList.add("capture");
const filmData = {
  type: "movie", title: "Harry Potter and the Deathly Hallows: Part 2", year: "2011",
  genres: "Adventure  ·  Fantasy", rating: "8.1", runtime: "2h 10m",
  art: "wallpaper/sagas/harry-potter.jpg",
  synopsis: "Harry, Ron and Hermione return to Hogwarts to find and destroy Voldemort's remaining Horcruxes. As the wizarding world gathers for its final battle, Harry must confront the truth about his past and face the Dark Lord one last time.",
  facts: [["Director","David Yates"],["Writers","Steve Kloves"],["Country","United Kingdom, United States"],["Aired","2011"]],
  cast: [["Daniel Radcliffe","Harry Potter"],["Emma Watson","Hermione Granger"],["Rupert Grint","Ron Weasley"],["Ralph Fiennes","Lord Voldemort"],["Alan Rickman","Severus Snape"],["Maggie Smith","Minerva McGonagall"],["Michael Gambon","Albus Dumbledore"],["Helena Bonham Carter","Bellatrix Lestrange"],["Tom Felton","Draco Malfoy"]],
  recommendations: [["Dune","wallpaper/sagas/dune.jpg"],["The Lord of the Rings","wallpaper/sagas/lord-of-the-rings.jpg"],["The Witcher","wallpaper/sagas/the-witcher.jpg"]]
};
const seriesData = {
  type: "series", title: "One Piece", year: "1999", genres: "Animation  ·  Action  ·  Adventure",
  rating: "9.0", runtime: "24 min", art: "universes/one-piece/new-world/banners/wano-country.png",
  synopsis: "Monkey D. Luffy sets sail to become King of the Pirates. Along the way, he assembles the Straw Hat crew and journeys across the seas in search of the legendary treasure known as the One Piece, facing powerful foes and uncovering the world's hidden history.",
  facts: [["Studio","Toei Animation"],["Network","Fuji TV"],["Country","Japan"],["Aired","1999"],["Status","Ongoing"],["Source","Manga"]],
  cast: [["Mayumi Tanaka","Monkey D. Luffy"],["Kazuya Nakai","Roronoa Zoro"],["Akemi Okamura","Nami"],["Kappei Yamaguchi","Usopp"],["Hiroaki Hirata","Sanji"],["Ikue Otani","Tony Tony Chopper"],["Yuriko Yamaguchi","Nico Robin"],["Kazuki Yao","Franky"],["Cho","Brook"]],
  recommendations: [["The Witcher","wallpaper/sagas/the-witcher.jpg"],["Dune","wallpaper/sagas/dune.jpg"],["Harry Potter","wallpaper/sagas/harry-potter.jpg"]]
};
const data = film ? filmData : seriesData;
const episodeNames = [
  "I'm Luffy! The Man Who's Gonna Be King of the Pirates!",
  "Enter the Great Swordsman! Pirate Hunter Roronoa Zoro!",
  "Morgan vs. Luffy! Who's the Mysterious Pretty Girl?",
  "Luffy's Past! The Red-haired Shanks Appears!",
  "Fear, Mysterious Power! Pirate Clown Captain Buggy!",
  "Desperate Situation! Beast Tamer Mohji vs. Luffy!",
  "Epic Showdown! Swordsman Zoro vs. Acrobat Cabaji!",
  "Who is the Victor? Devil Fruit Power Showdown!",
  "The Honorable Liar? Captain Usopp",
  "The Weirdest Guy Ever! Jango the Hypnotist!",
  "Expose the Plot! Pirate Butler, Captain Kuro!",
  "Clash with the Black Cat Pirates! The Great Battle on the Slope!",
  "The Terrifying Duo! Meowban Brothers vs. Zoro!",
  "Luffy Back in Action! Miss Kaya's Desperate Resistance!",
  "Beat Kuro! Usopp the Man's Tearful Resolve!"
];
const episodeArt = [
  "universes/one-piece/east-blue/banners/romance.png",
  "universes/one-piece/east-blue/banners/romance.png",
  "universes/one-piece/east-blue/banners/romance.png",
  "universes/one-piece/east-blue/banners/romance.png",
  "universes/one-piece/east-blue/banners/orange.png",
  "universes/one-piece/east-blue/banners/orange.png",
  "universes/one-piece/east-blue/banners/orange.png",
  "universes/one-piece/east-blue/banners/orange.png",
  "universes/one-piece/east-blue/banners/syrup.png",
  "universes/one-piece/east-blue/banners/syrup.png",
  "universes/one-piece/east-blue/banners/syrup.png",
  "universes/one-piece/east-blue/banners/syrup.png",
  "universes/one-piece/east-blue/banners/syrup.png",
  "universes/one-piece/east-blue/banners/syrup.png",
  "universes/one-piece/east-blue/banners/syrup.png"
];
const seasons = Array.from({length:22}, (_,i)=>i+1).concat([0]);
let state = {
  renderer: (params.get("renderer") || document.body.dataset.renderer) === "rebuilt" ? "rebuilt" : "legacy",
  activeSeason: 1, order: "seasons", expanded: false, saved: !film, notifications: true,
  castExpanded: false, seasonQueued: false, popup: "", contextEpisode: 1, jumpDraft: "",
  systemMenu: false, quitArmed: false
};
if ((params.get("state") || document.body.dataset.initialState) === "synopsis-open") { state.renderer = "rebuilt"; state.expanded = true; }
const escapeHtml = (value) => String(value).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;").replace(/'/g,"&#39;");
const icon = (name) => '<i aria-hidden="true" class="glyph glyph-' + name + '"></i>';
const backSvg = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 8 12l6.5 6.5"/></svg>';
const collectionSvg = '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/></svg>';
const fullArt = (p) => ROOT + p;
function status(message) { $("review-status").textContent = message; }
function factMarkup() {
  return data.facts.map((fact) => '<div class="fact"><span class="key">' + escapeHtml(fact[0]) + '</span><span class="value">' + escapeHtml(fact[1]) + '</span></div>').join("");
}
function metaMarkup() {
  const sep = state.renderer === "legacy" ? '<span class="meta-dot">-</span>' : "";
  return '<div class="meta"><strong>' + data.year + '</strong>' + sep + '<span>' +
    escapeHtml(data.genres) + '</span>' + sep + '<span class="imdb"><span class="imdb-badge">IMDb</span>' +
    data.rating + '</span>' + sep + '<span>' + data.runtime + '</span></div>';
}
function synopsisMarkup() {
  const toggle = state.renderer === "rebuilt" ? '<button type="button" class="more" data-action="synopsis" aria-expanded="' + state.expanded + '">' +
    (state.expanded ? "Less" : "More") + '</button>' : "";
  return '<div class="synopsis' + (!state.expanded && state.renderer === "rebuilt" ? " clamped" : "") +
    '"><p>' + escapeHtml(data.synopsis) + '</p>' + toggle + '</div>';
}
function actionMarkup() {
  const watch = film ? "Watch" : "Watch  S1 · E1";
  return '<div class="actions"><button type="button" class="action primary" data-action="watch">' +
    icon("play") + '<span>' + (state.order === "absolute" && !film ? "Watch  S1 · E1" : watch) + '</span></button>' +
    '<button type="button" class="action library" data-action="library"><span class="saved-mark">' +
    (state.saved ? "✓" : "+") + '</span><span>' + (state.saved ? "In Library" : "Library") + '</span></button>' +
    '<button type="button" class="action" data-action="collections">' + collectionSvg +
    '<span>Add to Collections</span></button>' +
    (!film && state.saved ? '<button type="button" class="action" data-action="notifications">Notifications ' +
    (state.notifications ? "on" : "off") + '</button>' : "") + '</div>';
}
function heroMarkup() {
  const label = (film ? "Movie" : "Series") + (state.renderer === "rebuilt" ? "  ·  Theatre" : " - Theatre");
  return '<section class="hero"><div class="hero-art" role="img" aria-label="' + escapeHtml(data.title) +
    ' local project art" style="background-image:url(' + fullArt(data.art) + ')"></div><div class="hero-wash"></div>' +
    '<div class="hero-copy"><span class="type-kicker">' + label + '</span><h1 class="hero-title">' +
    (film ? escapeHtml(data.title).replace("Part 2","Part&nbsp;2") : escapeHtml(data.title)) + '</h1>' + metaMarkup() +
    '<div class="mobile-facts">' + factMarkup() + '</div>' + synopsisMarkup() + actionMarkup() +
    '</div></section><div class="desktop-facts"><div class="standalone-synopsis synopsis"><p>' +
    escapeHtml(data.synopsis) + '</p></div><div class="facts">' + factMarkup() + '</div></div>';
}
function castMarkup() {
  const shown = state.castExpanded ? data.cast : data.cast.slice(0,8);
  const people = shown.map((person) => {
    const words = person[0].split(" ");
    const initials = words[0][0] + (words.length>1 ? words[words.length-1][0] : "");
    return '<div class="cast-person"><div class="cast-face">' + initials + '</div><div class="cast-name">' +
      escapeHtml(person[0]) + '</div><div class="cast-role">' + escapeHtml(person[1]) + '</div></div>';
  }).join("");
  const more = !state.castExpanded && data.cast.length>8 ?
    '<button type="button" data-action="cast-expand" class="cast-person cast-more"><div class="cast-face">›</div><div class="cast-name">All cast</div></button>' : "";
  return '<section class="content-section cast-section"><div class="section-label">CAST</div><div class="cast-flow">' + people + more + '</div></section>';
}
function recommendationsMarkup() {
  return '<section class="content-section recommend-section"><div class="section-label">MORE LIKE THIS</div>' +
    '<div class="rec-rail">' + data.recommendations.map((rec) =>
    '<button type="button" class="rec-card" data-action="recommend" data-title="' + escapeHtml(rec[0]) +
    '"><img src="' + fullArt(rec[1]) + '" alt="' + escapeHtml(rec[0]) + ' local artwork"><span class="rec-title">' +
    escapeHtml(rec[0]) + '</span></button>').join("") + '</div></section>';
}

function activeEpisodes() {
  const season = state.order === "absolute" ? 1 : state.activeSeason;
  const count = season === 1 ? episodeNames.length : 16;
  return Array.from({length: count}, (_,i) => {
    const isFirst = season === 1;
    return {
      number: i + 1, season, title: isFirst ? episodeNames[i] : "Episode " + (i + 1),
      still: isFirst ? episodeArt[i] : "universes/one-piece/new-world/banners/wano-country.png",
      status: i === 0 ? "Watched" : i === 1 ? "Next up" : i === 2 ? "43% watched" : "",
      watched: i === 0, nextUp: i === 1, progress: i === 2 ? .43 : 0,
      description: isFirst
        ? (i === 0 ? "A young pirate begins his journey across the seas in search of the One Piece."
          : i === 1 ? "Luffy crosses paths with a swordsman who could become his first crewmate."
          : i === 2 ? "Luffy confronts a powerful Marine captain in Shells Town."
          : "Luffy and his companions continue their journey toward the Grand Line.")
        : "The Straw Hat crew continues its adventure."
    };
  });
}
function episodeRowMarkup(ep) {
  const classes = ep.nextUp ? " next-up" : (ep.watched ? " watched" : "");
  const marker = ep.status || "";
  const progress = ep.progress > 0 ? '<div class="still-progress" style="width:' + (ep.progress*100) + '%"></div>' : "";
  const check = ep.watched ? '<span class="watched-mark">' + icon("check") + '</span>' : "";
  const name = escapeHtml(ep.title), num = ep.number;
  const still = '<div class="episode-still"><img alt="' + name + ' — local arc art" src="' + fullArt(ep.still) + '">' + progress + check + '</div>';
  return '<div class="episode-row' + classes + '" role="button" tabindex="0" data-episode="' + num +
    '" aria-label="Open sources for episode ' + num + '"><div class="episode-num"><span class="num">' + num +
    '</span><small>S' + ep.season + '</small></div>' + still +
    '<div class="episode-copy"><div class="episode-name">' + name + '</div>' +
    (marker ? '<div class="episode-subline">' + marker + '</div>' : "") +
    '<div class="episode-description">' + escapeHtml(ep.description) + '</div></div>' +
    '<div class="row-status">' + (ep.watched ? "WATCHED" : ep.nextUp ? "NEXT UP" : ep.progress ? "43% WATCHED" : "AVAILABLE") + '</div>' +
    '<div class="episode-buttons"><button type="button" class="circle-action" data-action="episode-play" data-episode="' + num +
    '" aria-label="Play episode ' + num + '"><span class="circle-visible">' + icon("play") + '</span></button>' +
    '<button type="button" class="circle-action" data-action="episode-download" data-episode="' + num +
    '" aria-label="Download episode ' + num + '"><span class="circle-visible">' + icon("download") + '</span></button></div></div>';
}
function orderMarkup() {
  if (state.renderer === "rebuilt") {
    return '<div class="order-segment" role="group" aria-label="Episode order">' +
      ["absolute","seasons"].map(mode => '<button type="button" class="' + (mode === state.order ? "selected" : "") +
      '" data-action="order" data-mode="' + mode + '">' + (mode === "absolute" ? "Absolute" : "Seasons") + '</button>').join("") + '</div>';
  }
  return '<div class="order-row" role="group" aria-label="Episode order">' +
    ["absolute","seasons"].map(mode => '<button type="button" class="order-btn ' + (mode === state.order ? "selected" : "") +
    '" data-action="order" data-mode="' + mode + '">' + (mode === "absolute" ? "Absolute" : "Seasons") + '</button>').join("") + '</div>';
}
function seasonMarkup() {
  if (state.order === "absolute") return "";
  return '<div class="season-pick-row"><button type="button" id="season-trigger" class="season-trigger" ' +
    'data-action="season-menu" aria-haspopup="listbox" aria-expanded="' + (state.popup === "season") + '">' +
    (state.activeSeason === 0 ? "Specials" : "Season " + state.activeSeason) +
    '<span class="down" aria-hidden="true">▾</span></button></div>';
}
function rebuiltLedgerMarkup(eps) {
  const title = state.order === "absolute" ? "All episodes" : state.activeSeason === 0 ? "Specials" : "Season " + state.activeSeason;
  return '<div class="rebuilt-ledger"><div class="rebuilt-ledger-left">' + orderMarkup() + seasonMarkup() +
    '</div><div class="rebuilt-ledger-right">' +
    (state.order !== "absolute" ? '<button type="button" class="rebuilt-pill" data-action="season-download">' +
      icon("download") + "Download season</button>" : "") +
    '<button type="button" class="rebuilt-pill next-pill" data-action="next-up">Next · E2</button>' +
    '<label class="inline-goto"><span class="visually-hidden">Go to episode number</span>' +
    '<input id="inline-goto" type="number" min="1" max="' + eps.length + '" placeholder="Go to #" aria-label="Go to episode"></label></div>' +
    '<div class="rebuilt-ledger-caption"><span class="ledger-heading">' + title + '</span>' +
    '<span class="ledger-subline">' + eps.length + ' episodes  ·  1 watched  ·  1 in progress</span></div></div>';
}
function legacyLedgerMarkup(eps) {
  const title = state.order === "absolute" ? "All episodes" : state.activeSeason === 0 ? "Specials" : "Season " + state.activeSeason;
  return orderMarkup() + seasonMarkup() + '<div class="ledger-head"><div><div class="ledger-heading">' +
    title + '</div><div class="ledger-subline">' + eps.length + ' episodes  /  1 watched  /  1 in progress  /  next E2</div></div>' +
    '<div class="ledger-actions">' +
    (state.order !== "absolute" ? '<button type="button" class="ledger-action download-season" data-action="season-download">' +
    icon("download") + 'Download season</button>' : "") +
    '<button type="button" id="jump-trigger" class="ledger-action" data-action="jump-menu" aria-haspopup="dialog" aria-expanded="' +
    (state.popup === "jump") + '">Go to episode</button></div></div>' +
    '<div class="ledger-columns"><span>EPISODE</span><span>STORY</span><span class="status-label">STATUS</span></div>';
}
function episodeSectionMarkup() {
  if (film) return "";
  const eps = activeEpisodes();
  return '<section id="episode-section" class="episode-section"><div id="season-chrome" class="season-chrome">' +
    (state.renderer === "rebuilt" ? rebuiltLedgerMarkup(eps) : legacyLedgerMarkup(eps)) + '</div>' +
    '<div class="episode-list">' + eps.map(episodeRowMarkup).join("") + '</div></section>';
}
function chromeMarkup() {
  const androidHost = app.classList.contains("android-host");
  if (androidHost) { state.systemMenu = false; state.quitArmed = false; }
  const back = '<button type="button" class="back" data-action="back">' + backSvg +
    '<span>' + (state.renderer === "rebuilt" ? "Theatre" : "Back") + '</span></button>';
  if (state.renderer === "legacy") {
    if (androidHost) return back;
    return back + '<div class="window-controls">' +
      '<button data-action="minimize" aria-label="Minimize">' + icon("minimize") + '</button>' +
      '<button data-action="fullscreen" aria-label="Toggle fullscreen">' + icon("fullscreen") + '</button>' +
      '<button data-action="quit" aria-label="Close">' + icon("power") + '</button></div>';
  }
  const menu = state.systemMenu ? '<div class="system-menu">' +
    (state.quitArmed
      ? '<strong>Quit Colosseum?</strong><button data-action="keep-watching">Keep watching</button><button class="danger" data-action="quit">Quit</button>'
      : '<button data-action="minimize">Minimize</button><button data-action="fullscreen">Exit fullscreen</button>' +
      '<hr><button class="danger" data-action="arm-quit">Quit Colosseum…</button>') + '</div>' : "";
  return '<div class="bar-chrome" id="bar-chrome">' + back +
    '<div class="bar-right"><button type="button" class="round-head" data-action="search" aria-label="Search">' +
    icon("search") + '</button>' + (androidHost ? "" : '<button type="button" class="round-head" data-action="system-menu" aria-label="Window and app" aria-expanded="' +
    state.systemMenu + '"><span class="three-dots"><i></i><i></i><i></i></span></button>') + '</div>' + menu + '</div>';
}
function setViewport() {
  const width = capture ? Math.round(window.innerWidth) : Math.max(320, +$("screenWidth").value || 412);
  const height = capture ? Math.round(window.innerHeight) : Math.max(320, +$("screenHeight").value || 900);
  if (!capture) {
    document.documentElement.style.setProperty("--screen-w", width + "px");
    document.documentElement.style.setProperty("--screen-h", height + "px");
  }
  const isPhone = Math.min(width, height) < 600;
  const tabletPortrait = !isPhone && width < 840 && height >= 1000;
  const layout = isPhone ? (width < height ? "phone-portrait" : "phone-landscape") :
    (tabletPortrait ? "phone-portrait tablet" : (width >= 1000 && height <= 750 ? "desktop" : "tablet"));
  const isAndroidHost = isPhone || layout.includes("tablet");
  app.className = state.renderer + " " + layout + (isPhone || tabletPortrait ? " is-handset" : "") + (isAndroidHost ? " android-host" : "");
  const top = capture ? +params.get("top") || 0 : +$("insetTop").value || 0;
  const bottom = capture ? +params.get("bottom") || 0 : +$("insetBottom").value || 0;
  const scale = capture ? +params.get("scale") || 1 : +$("fontScale").value || 1;
  app.style.setProperty("--safe-top", top + "px");
  app.style.setProperty("--safe-bottom", bottom + "px");
  app.style.setProperty("--font-scale", String(scale));
  if (capture) {
    document.documentElement.style.setProperty("--screen-w",width+"px");
    document.documentElement.style.setProperty("--screen-h",height+"px");
  }
}
function closePopup(returnFocus = true) {
  const old = state.popup, focus = state.popupFocus;
  state.popup = ""; state.popupFocus = null;
  $("overlay").innerHTML = ""; $("overlay").dataset.which = ""; $("overlay").classList.remove("modal-open");
  $("page-scroll").inert = false; $("chrome").inert = false;
  for (const id of ["season-trigger","jump-trigger"]) $(id)?.setAttribute("aria-expanded","false");
  if (returnFocus && old && focus?.isConnected) focus.focus({preventScroll:true});
}
function showPopup(which, episode) {
  if (!which || state.popup === which) { closePopup(); return; }
  closePopup(false); state.popup = which;
  if (episode) state.contextEpisode = episode;
  state.popupFocus = which === "season" ? $("season-trigger") : which === "jump" ? $("jump-trigger") :
    which === "removal" ? content.querySelector('[data-action="library"]') :
    content.querySelector('.episode-row[data-episode="' + state.contextEpisode + '"]');
  paintPopup();
}
function paintPopup() {
  const target = $("overlay"), which = state.popup;
  if (!which) return;
  let anchor = null, html = "", className = "";
  if (which === "season") {
    anchor = $("season-trigger"); className = "popup-season";
    html = seasons.map(season => '<button type="button" role="option" aria-selected="' + (season === state.activeSeason) +
      '" data-action="season-select" data-season="' + season + '">' + (season === 0 ? "Specials" : "Season " + season) + '</button>').join("");
  } else if (which === "jump") {
    anchor = $("jump-trigger"); className = "popup-jump";
    const max = activeEpisodes().length;
    html = '<div class="jump-input-row"><input id="jump-input" type="number" inputmode="numeric" min="1" max="' + max +
      '" placeholder="1 - ' + max + '" aria-label="Episode number"><button type="button" data-action="jump-submit" class="jump-submit">Jump</button></div>' +
      '<div class="jump-ranges">' + Array.from({length:Math.ceil(max/50)},(_,i)=>
      '<button type="button" data-action="jump-range" data-episode="' + (i*50+1) + '">' + (i*50+1) + '-' + Math.min((i+1)*50,max) + '</button>').join("") + '</div>';
  } else if (which === "context") {
    anchor = content.querySelector('.episode-row[data-episode="' + state.contextEpisode + '"]'); className = "context-menu";
    html = '<button type="button" data-action="episode-play" data-episode="' + state.contextEpisode +
      '">Play</button><button type="button" data-action="episode-download" data-episode="' + state.contextEpisode + '">Download</button>';
  } else if (which === "removal") {
    anchor = content.querySelector('[data-action="library"]'); className = "popup-removal";
    html = '<h2>Remove from your library?</h2><p>' + escapeHtml(data.title) + '</p><button data-action="remove-confirm">Remove from Colosseum</button>' +
      (params.get("linked") === "1" ? '<button data-action="remove-linked">Remove from Colosseum &amp; Stremio</button>' : '') +
      '<button class="quiet" data-action="remove-cancel">Cancel</button>';
  }
  if (!anchor) {closePopup();return;}
  let popup = target.querySelector(".popup"), fresh = !popup || target.dataset.which !== which;
  if (fresh) {
    target.innerHTML = ""; popup = document.createElement("div"); popup.className = "popup " + className;
    popup.setAttribute("role",which === "season" ? "listbox" : which === "context" ? "menu" : "dialog");
    popup.setAttribute("aria-label",which === "season" ? "Choose season" : which === "jump" ? "Go to episode" : which === "removal" ? "Remove from your library?" : "Episode actions");
    popup.innerHTML = html; target.appendChild(popup); target.dataset.which = which;
  }
  const bounds = app.getBoundingClientRect(), ref = anchor.getBoundingClientRect(), styles = getComputedStyle(app);
  const top = (parseFloat(styles.getPropertyValue("--safe-top")) || 0) + 10;
  const bottom = app.clientHeight - (parseFloat(styles.getPropertyValue("--safe-bottom")) || 0) - 10;
  const below = ref.bottom - bounds.top + 8, above = ref.top - bounds.top - 8;
  popup.style.maxWidth = Math.max(120,app.clientWidth - (which === "removal" ? 48 : 20)) + "px";
  popup.style.maxHeight = Math.max(60,Math.min(which === "season" ? 304 : bottom-top,
    which === "removal" ? bottom-top : Math.max(bottom - below, above - top))) + "px";
  const w = popup.offsetWidth, h = popup.offsetHeight;
  popup.style.left = (which === "removal" ? (app.clientWidth-w)/2 : Math.max(10,Math.min(ref.left-bounds.left,app.clientWidth-w-10))) + "px";
  popup.style.top = (which === "removal" ? top+(bottom-top-h)/2 : below+h<=bottom ? below : above-h>=top ? above-h : Math.max(top,bottom-h)) + "px";
  if (which === "season" || which === "jump") anchor.setAttribute("aria-expanded","true");
  if (which === "removal") {
    target.classList.add("modal-open"); popup.setAttribute("aria-modal","true");
    $("page-scroll").inert = true; $("chrome").inert = true;
  }
  if (fresh) {
    const focus = which === "season" ? popup.querySelector('[aria-selected="true"]') :
      which === "jump" ? popup.querySelector("input") : popup.querySelector("button");
    focus?.focus({preventScroll:true}); focus?.scrollIntoView({block:"nearest"});
  }
}
function syncSourceChrome() {
  const bar=$("bar-chrome");if(bar)bar.classList.toggle("solid",scroll.scrollTop>40);
  const safe=parseFloat(getComputedStyle(app).getPropertyValue("--safe-top"))||0;
  const narrow=app.classList.contains("android-host")&&app.clientWidth<840;
  app.style.setProperty("--phone-scrim-opacity",narrow?String(Math.min(1,Math.max(0,(scroll.scrollTop-32)/40))):"0");
  app.style.setProperty("--sticky-chrome-color",narrow?"#08090d":"rgba(4,5,9,.93)");
  const header=content.querySelector(".season-chrome");
  const pinned=!!header&&scroll.scrollTop>0&&header.getBoundingClientRect().top<=app.getBoundingClientRect().top+72+safe+1;
  app.classList.toggle("sticky-scrim",pinned);
}

function render(preserveScroll = true) {
  const old = preserveScroll ? scroll.scrollTop : 0;
  setViewport();
  content.innerHTML = heroMarkup() + episodeSectionMarkup() + castMarkup() + recommendationsMarkup();
  $("chrome").innerHTML = chromeMarkup();
  scroll.scrollTop = old;
  const titlebar = $("bar-chrome");
  if (titlebar) titlebar.classList.toggle("solid",scroll.scrollTop>40);
  syncSourceChrome();
  if (state.popup) paintPopup(); else $("overlay").innerHTML = "";
}
function scrollToEpisodes() {
  const section = $("episode-section");
  if (!section) {status("Film details do not have an episode ledger.");return;}
  scroll.scrollTop += section.getBoundingClientRect().top - app.getBoundingClientRect().top - (parseFloat(getComputedStyle(app).getPropertyValue("--safe-top"))||0) - 78;
  const titlebar = $("bar-chrome");
  if (titlebar) titlebar.classList.add("solid");
  syncSourceChrome();
  if (state.popup) paintPopup();
}
function jumpToEpisode(number) {
  const n = Number(number);
  const row = content.querySelector('.episode-row[data-episode="' + n + '"]');
  if (!row) {status("That episode number is outside this illustrative season.");return;}
  closePopup();
  const chrome = $("season-chrome");
  scroll.scrollTop += row.getBoundingClientRect().top - app.getBoundingClientRect().top - (parseFloat(getComputedStyle(app).getPropertyValue("--safe-top"))||0) - (chrome ? chrome.offsetHeight : 0) - 80;
  row.focus({preventScroll:true});
  status("Selected episode " + n + ". No player or source request was made.");
}
function act(action, el) {
  const episode = +(el.dataset.episode || 0);
  switch (action) {
    case "library":
      if(state.saved){showPopup("removal");break;}
      state.saved = true;
      render();content.querySelector('[data-action="library"]')?.focus({preventScroll:true});
      status(state.saved ? "Locally displayed as In Library; no collection modified." : "Locally displayed as not saved; no collection modified.");
      break;
    case "notifications":
      state.notifications = !state.notifications; render(); status("Notification label changed locally only."); break;
    case "collections":
      closePopup(false);
      BatchCollections.open({host:app,trigger:el,key:"theatre-"+data.type+":"+data.title,title:data.title,kind:film?"film":"series",kindLabel:film?"Film":"Series",year:data.year,cover:fullArt(data.art),onSaved:()=>status("Collection membership saved in this local page fixture.")});break;
    case "remove-cancel":closePopup();break;
    case "remove-confirm":case "remove-linked":
      closePopup(false);state.saved=false;render();content.querySelector('[data-action="library"]')?.focus({preventScroll:true});
      status("Removed from this local library fixture"+(action==="remove-linked"?" with the linked-account choice":"")+". No service operation.");break;
    case "watch": case "episode-play": case "episode-download": case "season-download":
      closePopup();
      status((action === "watch" ? "Watch" : action === "episode-play" ? "Episode " + episode + " Play" :
        action === "episode-download" ? "Episode " + episode + " Download" : "Download season") +
        " would open the approved SourcesSheet; no provider call or player mock.");
      break;
    case "synopsis": state.expanded = !state.expanded; render(); break;
    case "cast-expand": state.castExpanded = true; render(); break;
    case "recommend": status("More like this: " + el.dataset.title + " — navigation omitted in standalone mock."); break;
    case "order": state.order = el.dataset.mode; state.popup = ""; render(); scrollToEpisodes(); break;
    case "season-menu": showPopup("season"); break;
    case "season-select":
      closePopup(false);state.activeSeason = +el.dataset.season; render(); scrollToEpisodes();$("season-trigger")?.focus({preventScroll:true});break;
    case "jump-menu": showPopup("jump"); break;
    case "jump-submit": jumpToEpisode($("jump-input")?.value); break;
    case "jump-range": jumpToEpisode(episode); break;
    case "next-up": jumpToEpisode(2); break;
    case "back": status("Back would return to " + (state.renderer === "rebuilt" ? "Theatre" : "the previous page") + "; navigation stays local."); break;
    case "search": status("Search would open the original search route; navigation stays local."); break;
    case "system-menu": state.systemMenu = !state.systemMenu; state.quitArmed = false; render(); break;
    case "arm-quit": state.quitArmed = true; render(); break;
    case "keep-watching": state.quitArmed = false; state.systemMenu = false; render(); break;
    case "fullscreen": case "minimize": case "quit":
      status("Original " + action + " window action left inert in the local mock."); state.systemMenu = false; render(); break;
  }
}
content.addEventListener("click",(event) => {
  const control = event.target.closest("[data-action]");
  if (control) {act(control.dataset.action,control);return;}
  const row = event.target.closest(".episode-row");
  if (row) status("Episode " + row.dataset.episode + " would open SourcesSheet; no source fetch.");
});
$("chrome").addEventListener("click",(event)=>{
  const control = event.target.closest("[data-action]");
  if(control) act(control.dataset.action,control);
});
$("overlay").addEventListener("click",(event)=>{
  const control = event.target.closest("[data-action]");
  if(control) act(control.dataset.action,control);
});
content.addEventListener("contextmenu",(event)=>{
  const row = event.target.closest(".episode-row");
  if (!row) return;
  event.preventDefault(); showPopup("context",+row.dataset.episode);
});
content.addEventListener("keydown",(event)=>{
  const row = event.target.closest(".episode-row");
  if(row && event.target===row && (event.key==="Enter" || event.key===" ")) {
    event.preventDefault();status("Episode " + row.dataset.episode + " would open SourcesSheet.");
  }
});
document.addEventListener("keydown",(event)=>{
  const popup=$("overlay").querySelector(".popup");
  if(popup && ["ArrowDown","ArrowUp","Home","End","Tab"].includes(event.key) && event.target.tagName!=="INPUT"){
    const items=[...popup.querySelectorAll("button")],i=items.indexOf(document.activeElement);
    if(state.popup==="season"&&event.key==="Tab"){event.preventDefault();return;}
    let n=event.key==="Home"?0:event.key==="End"?items.length-1:event.key==="ArrowUp"||(event.key==="Tab"&&event.shiftKey)?(i+items.length-1)%items.length:(i+1)%items.length;
    event.preventDefault();items[n]?.focus();return;
  }
  if(popup && event.key==="Tab" && state.popup!=="season"){
    const items=[...popup.querySelectorAll("button,input")],i=items.indexOf(document.activeElement);
    event.preventDefault();items[(i+items.length+(event.shiftKey?-1:1))%items.length]?.focus();return;
  }
  if(event.key==="Escape"){
    if(state.popup) {closePopup();event.preventDefault();}
    else if(state.systemMenu){state.systemMenu=false;render();event.preventDefault();}
  }
  if(event.key==="Enter" && event.target.id==="jump-input"){jumpToEpisode(event.target.value);event.preventDefault();}
  if(event.key==="Enter" && event.target.id==="inline-goto"){jumpToEpisode(event.target.value);event.preventDefault();}
});
scroll.addEventListener("scroll",()=>{
  syncSourceChrome();
  if(state.popup) paintPopup();
},{passive:true});
$("preset").addEventListener("change",()=> {
  const [w,h]=$("preset").value.split("x").map(Number);
  $("screenWidth").value=w;$("screenHeight").value=h;
  state.popup="";render(false);
});
for(const id of ["screenWidth","screenHeight","insetTop","insetBottom","fontScale"]) {
  $(id).addEventListener("input",()=>{state.popup="";render(true);});
}
$("renderer").value=state.renderer;
$("renderer").addEventListener("change",()=>{state.renderer=$("renderer").value;state.popup="";render(true);});
$("topButton").addEventListener("click",()=>{scroll.scrollTop=0;closePopup();});
$("episodesButton").addEventListener("click",()=>{closePopup();scrollToEpisodes();});
$("seasonButton").addEventListener("click",()=>{
  if(film){status("Film detail has no season picker.");return;}
  closePopup();scrollToEpisodes();showPopup("season");
});
$("jumpButton").addEventListener("click",()=>{
  if(film){status("Film detail has no Go-to control.");return;}
  closePopup();scrollToEpisodes();
  if(state.renderer==="rebuilt") {$("inline-goto")?.focus();status("Rebuilt detail uses its original inline Go to # field.");}
  else showPopup("jump");
});
$("synopsisButton").addEventListener("click",()=>{
  if(state.renderer!=="rebuilt"){state.renderer="rebuilt";$("renderer").value="rebuilt";}
  state.expanded=!state.expanded;state.popup="";render(true);
});
window.addEventListener("resize",()=>{if(capture)render(true);});
render(false);
const openingState = params.get("state") || document.body.dataset.initialState;
if(openingState==="episodes" || openingState==="season-open" || openingState==="jump-open" || openingState==="context-open"){
  requestAnimationFrame(()=>{
    scrollToEpisodes();
    if(openingState==="season-open") showPopup("season");
    if(openingState==="jump-open" && state.renderer==="legacy")showPopup("jump");
    if(openingState==="context-open") showPopup("context",2);
  });
}

document.addEventListener("pointerdown",event=>{
  if(state.popup&&state.popup!=="removal"&&!event.target.closest("#overlay")&&!event.target.closest("#season-trigger,#jump-trigger"))closePopup(false);
});
if(params.get("state")==="removal-open"){state.saved=true;render();showPopup("removal");}
