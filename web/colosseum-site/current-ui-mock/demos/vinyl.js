(() => {
  "use strict";
  const M = MockUI,
    F = VinylFixtures,
    L = VinylLists,
    C = VinylCatalogue,
    P = M.params,
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
  let page = P.page || "home",
    fixture = P.fixture || M.get("vinyl.fixture", "thriller"),
    profile = P.profile || M.get("vinyl.profile", "seasoned"),
    edition = Number(P.edition || 0),
    disc = 0,
    shown,
    playing = M.get("vinyl.playing", null),
    paused = true,
    track = Number(P.track ?? (fixture === "thriller" ? 5 : 0)),
    position = 72,
    lyrics = P.lyrics !== false,
    video = !!P.video,
    autoNotes = true,
    line = 1,
    source = P.source || "playing",
    offline = !!P.offline,
    lens = M.get("vinyl.lens", {
      kind: "albums",
      genre: "",
      decade: "",
      country: "",
      language: "",
    }),
    picks = M.get("vinyl.picks", []),
    answered = M.get("vinyl.answered", profile !== "new"),
    listKey = P.key || "popular",
    listHome = !!P.home,
    disposer,
    query = "",
    slide = 0;
  const art = (file, world = false) =>
    "../assets/vinyl/" + (world ? "vinyl-world/" : "vinyl-pages/") + file;
  function load() {
    shown = F.editions(fixture)[edition] || F.editions(fixture)[0];
    if (P.albumId) {
      const a = C.albums.find((x) => x.id === P.albumId);
      if (a) shown = fromWorld(a);
    }
    if (!playing) playing = shown;
    track = Math.min(track, playing.discs[0].length - 1);
  }
  function fromWorld(a) {
    return {
      id: a.id,
      name: a.title,
      artist: a.artists.join(" · "),
      year: a.year,
      cover: a.cover,
      world: true,
      discs: [
        a.tracks.map((t) => ({
          title: t.title,
          artist: t.artists.join(" · "),
          seconds: t.seconds || 240,
        })),
      ],
      about:
        "Local catalogue snapshot. Metadata and recommendation fields are fixture data.",
      type: "Album",
    };
  }
  function go(p, params = {}) {
    persist();
    M.go("vinyl." + p, Object.assign({ page: p, fixture, profile }, params));
  }
  function persist() {
    M.set("vinyl.fixture", fixture);
    M.set("vinyl.profile", profile);
    M.set("vinyl.playing", playing);
    M.set("vinyl.lens", lens);
    M.set("vinyl.picks", picks);
    M.set("vinyl.answered", answered);
  }
  function time(s) {
    return (
      Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0")
    );
  }
  function current() {
    return playing.discs[0][track] || playing.discs[0][0];
  }
  function rows(home = false) {
    let record = C.profiles[profile === "seasoned" ? "seasoned" : "new"] || {};
    return home
      ? L.homeRows(C, record, picks).concat(
          answered ? L.coldShelves(C, record, picks) : [],
        )
      : L.shelves(C, lens, record);
  }
  function card(i) {
    return `<button class="card" data-item="${esc(JSON.stringify(i))}" aria-label="${esc(i.title)}"><span class="cover ${i.round ? "round" : ""}"><img src="${art(i.cover, true)}" alt=""></span><p>${esc(i.title)}</p><small>${esc(i.subtitle)}</small>${i.reason ? `<small>${esc(i.reason)}</small>` : ""}</button>`;
  }
  function shelves(rs) {
    return (
      rs
        .map(
          (r) =>
            `<section class="shelf" id="shelf-${esc(r.key)}"><div class="head"><h2>${esc(r.title)}</h2><button data-list="${esc(r.key)}">See all ›</button></div><div class="cards">${r.items.slice(0, 8).map(card).join("")}</div></section>`,
        )
        .join("") ||
      '<p class="notice">No matching releases in this fixture.</p>'
    );
  }
  function hero() {
    const offered = rows()
        .filter((r) => r.items.length)
        .slice(0, 3),
      r = offered[slide - 1],
      item = r && r.items[0];
    return `<div class="hero"><div class="heroart"><img src="${item ? art(item.cover, true) : art(playing.cover, playing.world)}" alt=""></div><div class="herocopy"><div class="kicker">${item ? esc(r.title) : "Continue listening"}</div><h1>${esc(item ? item.title : current().title)}</h1><p>${item ? esc(item.subtitle) : esc(playing.artist) + " · " + esc(playing.name) + "<br>Up next: " + esc(playing.discs[0][(track + 1) % playing.discs[0].length].title)}</p><button class="resume" ${item ? 'data-list="' + esc(r.key) + '"' : 'data-route="song"'}>${item ? "Explore" : "Resume"}</button></div><div class="dots">${[0, ...offered.map((_, i) => i + 1)].map((i) => `<button data-slide="${i}" aria-label="Featured slide ${i + 1}" aria-pressed="${slide === i}" style="background:${slide === i ? "var(--gold)" : "#ffffff80"}"></button>`).join("")}</div></div><div class="tabslot"><div class="tabs glass"><button class="${page === "home" ? "on" : ""}" data-route="home">Home</button><button class="${page === "discover" ? "on" : ""}" data-route="discover">Discover</button><button data-action="library">Library</button></div></div>`;
  }

  function filters() {
    return `<div class="lens"><h2>${esc([lens.language, lens.decade, lens.genre, lens.country].filter(Boolean).join(" · ") || "Everything")}</h2>${["albums", "artists", "songs"].map((k) => `<button data-kind="${k}" class="${lens.kind === k ? "selected" : ""}" ${offline ? "disabled" : ""}>${k[0].toUpperCase() + k.slice(1)}</button>`).join("")}${[
      "genre",
      "decade",
      "country",
      "language",
    ]
      .map((k) => {
        const values = [
          ...new Set(
            C.albums
              .map((a) =>
                k === "decade" ? Math.floor(a.year / 10) * 10 + "s" : a[k],
              )
              .filter(Boolean),
          ),
        ].sort();
        return `<label>${k}<select data-filter="${k}" ${offline ? "disabled" : ""}><option value="">All</option>${values.map((v) => `<option ${lens[k] === v ? "selected" : ""}>${esc(v)}</option>`).join("")}</select></label>`;
      })
      .join(
        "",
      )}</div>${offline ? '<p class="notice">Catalogue unavailable · cached shelves remain available. Filters are disabled.</p>' : ""}`;
  }
  function taste() {
    return `<section class="taste"><h1>What do you listen to?</h1><div class="tools"><input aria-label="Search artists" id="artist-search" placeholder="Search artists" value="${esc(query)}"><button data-action="skip">Skip</button><button class="gold" data-action="done">Done</button></div><div id="artists" class="grid">${artistCards()}</div><p class="notice">Artist images use representative album artwork from the local fixture.</p></section>`;
  }
  function artistCards() {
    return L.artistOptions(C, query)
      .map(
        (i) =>
          `<button class="card ${picks.includes(i.id) ? "chosen" : ""}" data-artist="${esc(i.id)}" aria-pressed="${picks.includes(i.id)}"><span class="cover round" style="${picks.includes(i.id) ? "box-shadow:0 0 0 3px var(--gold)" : ""}"><img src="${art(i.cover, true)}" alt=""></span><p>${esc(i.title)}${picks.includes(i.id) ? " ✓" : ""}</p></button>`,
      )
      .join("");
  }
  function credits(record = playing) {
    return `<div class="credits-grid">${(record.credits || F.credits).map((c) => `<div><small>${esc(c.role)}</small><p>${c.names.map(esc).join("<br>")}</p></div>`).join("")}</div><p class="notice">Credits are unverified local fixture metadata.</p>`;
  }
  function album() {
    return `<div class="album-layout"><div><div class="albumcover"><img src="${art(shown.cover, shown.world)}" alt="${esc(shown.name)}"></div><h1>${esc(shown.name)}</h1><p class="artist">${esc(shown.artist)}</p><p class="albumfacts">${shown.year} · Album · ${shown.discs.flat().length} tracks · ${shown.length || ""}</p><div class="actions"><button class="gold" data-action="play-album">Play</button><button data-action="shuffle">Shuffle</button><button data-action="save">${M.get("vinyl.saved." + shown.id, false) ? "Saved" : "Save"}</button></div><div class="section-index">${["About", "Credits", "Videos", "Editions", "Information"].map((x) => `<button data-section="${x}">${x}</button>`).join("")}</div></div><div>${shown.discs.length > 1 ? shown.discs.map((d, i) => `<button data-disc="${i}" class="${i === disc ? "selected" : ""}">Disc ${i + 1}</button>`).join("") : ""}${shown.discs[disc].map((t, i) => `<button class="track ${playing.id === shown.id && track === i ? "active" : ""}" data-track="${i}"><span class="num">${String(i + 1).padStart(2, "0")}</span><span class="title">${esc(t.title)}</span><span class="duration">${time(t.seconds)}</span></button>`).join("")}</div></div><div class="album-sections"><section id="About"><h2>About</h2><p>${esc(shown.about)}</p></section><section id="Credits"><h2>Credits</h2>${credits(shown)}</section><section id="Videos"><h2>Videos</h2>${fixture === "thriller" ? ["Billie Jean", "Beat It", "Thriller"].map((x, i) => `<button data-video-track="${[5, 4, 3][i]}"><img width="170" src="${art("video-" + ["billie-jean", "beat-it", "thriller"][i] + ".jpg")}" alt=""><p>${x}</p></button>`).join("") : "<p>No additional video clips in this fixture.</p>"}</section><section id="Editions"><h2>Editions</h2>${F.editions(
      fixture,
    )
      .map(
        (e, i) =>
          `<button class="edition" data-edition="${i}"><img src="${art(e.cover)}" alt=""><span>${esc(e.subtitle || e.name)}<small>${[e.year, e.label, e.country, e.format, e.discs.flat().length + " tracks"].filter(Boolean).map(esc).join(" · ")}</small></span>${e.id === shown.id ? "✓" : ""}</button>`,
      )
      .join(
        "",
      )}</section><section id="More"><h2>More by this artist</h2><div class="cards">${fixture === "thriller" ? ["Off the Wall", "Bad", "Dangerous", "HIStory", "Invincible"].map((n, i) => `<div class="card"><span class="cover"><img src="${art(["off-the-wall.jpg", "bad.jpg", "dangerous.jpg", "history.jpg", "invincible.jpg"][i])}" alt=""></span><p>${n}</p><small>Michael Jackson</small></div>`).join("") : "<p>No additional albums in this page fixture.</p>"}</div></section><section id="Information"><h2>Information</h2><p>${[
      "date",
      "label",
      "country",
      "format",
      "catalogue",
      "barcode",
      "mbid",
    ]
      .filter((k) => shown[k])
      .map((k) => `${k.toUpperCase()} · ${esc(shown[k])}`)
      .join(
        "<br>",
      )}</p><p class="notice">Unverified fixture metadata. No external service calls.</p></section></div>`;
  }
  function song() {
    return `<button data-route="album">‹ Back to album</button><div class="song-layout ${playing.id === "mounamela" ? "tall" : ""} ${video ? "video" : ""}"><div class="lyrics" ${lyrics ? "" : "hidden"}>${F.lines.map((l, i) => `<button data-line="${i}" class="${i === line ? "current" : ""}">${esc(l.text)}</button>`).join("")}</div><div><button style="padding:0" data-route="album"><img class="song-art" src="${art(playing.cover, playing.world)}" alt="Open album"></button>${video ? '<p class="source-label">OFFICIAL VIDEO · FIXTURE STILL</p>' : ""}<h1>${esc(current().title)}</h1><p class="artist">${esc(current().artist || playing.artist)}</p></div><aside class="annotation" ${lyrics && autoNotes ? "" : "hidden"}><h3>${playing.hasNotes === false ? "" : "Notes"}</h3>${playing.hasNotes === false ? "<p>No notes for this fixture.</p>" : (F.notes[line] || []).map((n) => `<p>${esc(n.text)}</p><small>${n.verified ? "Artist note · " : ""}${n.votes} votes</small>`).join("") || "<p>Select an annotated lyric to see its notes.</p>"}<p class="notice">Original stand-in lyrics and notes, not song lyrics.</p></aside></div>`;
  }
  function render() {
    const bg =
      page === "album" || page === "credits"
        ? shown
        : page === "song"
          ? playing
          : null;
    $(".wall").style.backgroundImage =
      'url("' +
      (bg
        ? art(bg.backdrop || bg.cover, bg.world)
        : "../assets/vinyl/cold-ripple.jpg") +
      '")';
    document.body.classList.toggle("world-bg", !bg);
    const y = $("#content").scrollTop;
    $("#fixture").value = fixture;
    $("#profile").value = profile;
    $("[data-action=offline]").textContent = offline
      ? "Catalogue offline"
      : "Catalogue online";
    let html = "";
    if (page === "album" || page === "credits") html = album();
    else if (page === "song") html = song();
    else if (page === "taste") html = taste();
    else if (page === "list") {
      const row = rows(listHome).find((r) => r.key === listKey) || {
        title: "No matching shelf",
        items: [],
      };
      html = `<button data-route="${listHome ? "home" : "discover"}">‹ Back</button>${listHome ? "" : filters()}<h1>${esc(row.title)}</h1><div class="grid">${row.items.map(card).join("")}</div>`;
    } else
      html =
        hero() +
        (page === "discover"
          ? filters() + shelves(rows())
          : !answered && profile === "new"
            ? taste()
            : shelves(rows(true)));
    $("#content").innerHTML = html;
    $("#content").scrollTop = y;
    renderPlayer();
    M.ready();
  }
  function renderPlayer() {
    const t = current();
    $("#player").innerHTML =
      `<button class="thumb" data-route="song"><img src="${art(playing.cover, playing.world)}" alt="Open playing song"></button><button class="nowplaying" data-route="song"><b>${esc(t.title)}</b><small>${esc(playing.artist)} · ${esc(playing.name)}</small></button><div class="transport"><button aria-label="Previous track" data-action="previous">‹‹</button><button class="pause" aria-label="Play or pause" data-action="pause" ${source === "playing" ? "" : "disabled"}>${paused ? "▶" : "Ⅱ"}</button><button aria-label="Next track" data-action="next">››</button></div><input class="seek" aria-label="Seek playback" type="range" min="0" max="${t.seconds}" value="${position}"><div class="playerextras"><button data-action="lyrics" aria-pressed="${lyrics}">Lyrics</button><button data-action="video" ${playing.hasVideo === false ? "disabled" : ""}>Video</button><button data-action="notes" aria-pressed="${autoNotes}">Notes</button><button data-action="credits">Credits</button><button data-action="queue">Queue</button><input type="range" min="0" max="100" value="70" aria-label="Volume" style="width:65px"></div><span class="quality">${source === "finding" ? "Finding a source" : source === "retrying" ? "Trying another source" : "AAC · 256 kbps"} · ${time(position)} / ${time(t.seconds)} · simulated</span>`;
  }
  function modal(title, body) {
    const d = $("#overlay");
    d.innerHTML = `<div class="head"><h2>${title}</h2><button data-action="close">Close ×</button></div>${body}`;
    d.showModal();
    disposer = M.registerOverlay(d, close);
  }
  function close() {
    const d = $("#overlay");
    d.close();
    if (disposer) {
      const x = disposer;
      disposer = null;
      x();
    }
  }
  function play(i) {
    playing = Object.assign({}, shown, { discs: [shown.discs[disc]] });
    track = i;
    position = 0;
    paused = false;
    persist();
    M.openSession({
      kind: "music",
      id: "vinyl-fixture",
      title: current().title,
      route: "vinyl.song",
      params: { page: "song", fixture },
    });
    render();
  }
  document.addEventListener("click", (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    if (b.dataset.slide !== undefined) {
      slide = +b.dataset.slide;
      return render();
    }
    if (b.dataset.route) return go(b.dataset.route);
    if (b.dataset.item) {
      const i = JSON.parse(b.dataset.item);
      if (i.kind === "artists")
        return M.toast(
          "Artist activation is intentionally unavailable in this prototype.",
        );
      const a = C.albums.find((a) => a.id === i.albumId);
      if (i.kind === "albums") return go("album", { albumId: a.id });
      shown = fromWorld(a);
      play(i.trackIndex || 0);
      return;
    }
    if (b.dataset.list)
      return go("list", {
        key: b.dataset.list,
        home: page === "home" && !b.closest(".hero"),
      });
    if (b.dataset.artist) {
      const id = b.dataset.artist;
      picks = picks.includes(id)
        ? picks.filter((x) => x !== id)
        : picks.concat(id);
      $("#artists").innerHTML = artistCards();
      return;
    }
    if (b.dataset.kind) {
      lens.kind = b.dataset.kind;
      persist();
      return render();
    }
    if (b.dataset.track !== undefined) return play(+b.dataset.track);
    if (b.dataset.edition !== undefined) {
      edition = +b.dataset.edition;
      shown = F.editions(fixture)[edition];
      disc = 0;
      render();
      $("#content").scrollTop = 0;
      return;
    }
    if (b.dataset.disc !== undefined) {
      disc = +b.dataset.disc;
      return render();
    }
    if (b.dataset.section)
      return document
        .getElementById(b.dataset.section)
        .scrollIntoView({ block: "start" });
    if (b.dataset.videoTrack !== undefined) {
      play(+b.dataset.videoTrack);
      return go("song", { video: true });
    }
    if (b.dataset.line !== undefined) {
      line = +b.dataset.line;
      position = F.lines[line].time;
      return render();
    }
    switch (b.dataset.action) {
      case "back":
        M.back();
        break;
      case "offline":
        offline = !offline;
        render();
        break;
      case "skip":
        picks = [];
      case "done":
        answered = true;
        persist();
        go("home");
        break;
      case "library":
        M.toast("Library is an empty placeholder in the QML prototype.");
        break;
      case "play-album":
        play(0);
        break;
      case "shuffle":
        play(Math.min(3, shown.discs[disc].length - 1));
        break;
      case "save":
        M.set(
          "vinyl.saved." + shown.id,
          !M.get("vinyl.saved." + shown.id, false),
        );
        render();
        break;
      case "pause":
        paused = !paused;
        renderPlayer();
        break;
      case "next":
        track = (track + 1) % playing.discs[0].length;
        position = 0;
        render();
        break;
      case "previous":
        track = (track + playing.discs[0].length - 1) % playing.discs[0].length;
        position = 0;
        render();
        break;
      case "lyrics":
        lyrics = !lyrics;
        if (page !== "song") go("song", { lyrics });
        else render();
        break;
      case "video":
        video = !video;
        if (page !== "song") go("song", { video });
        else render();
        break;
      case "notes":
        autoNotes = !autoNotes;
        lyrics = true;
        render();
        break;
      case "credits":
        modal("Credits", credits());
        break;
      case "queue":
        modal(
          "Up next",
          playing.discs[0]
            .slice(track + 1)
            .map(
              (t) =>
                `<p class="notice">${esc(t.title)} · ${time(t.seconds)}</p>`,
            )
            .join("") || "<p>End of this fixture edition.</p>",
        );
        break;
      case "close":
        close();
        break;
    }
  });
  document.addEventListener("change", (e) => {
    if (e.target.id === "fixture") {
      fixture = e.target.value;
      edition = 0;
      track = fixture === "thriller" ? 5 : 0;
      playing = null;
      load();
      persist();
      render();
    }
    if (e.target.id === "profile") {
      profile = e.target.value;
      answered = profile !== "new";
      persist();
      render();
    }
    if (e.target.dataset.filter) {
      lens[e.target.dataset.filter] = e.target.value;
      persist();
      render();
    }
  });
  document.addEventListener("input", (e) => {
    if (e.target.id === "artist-search") {
      query = e.target.value;
      $("#artists").innerHTML = artistCards();
    }
    if (e.target.classList.contains("seek")) {
      position = +e.target.value;
      renderPlayer();
    }
  });
  load();
  render();
  if (page === "credits") modal("Credits", credits());
})();
