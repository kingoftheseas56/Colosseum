(function () {
  "use strict";
  const T = TheatreUI,
    { M, $, $$, button, dialog, esc, activate } = T,
    p = T.resolveMedia(M.params);
  let season = Number(p.season ?? 2),
    mode = p.mediaType || "series",
    rebuilt = !!p.rebuilt,
    absolute = false;
  const movie = mode === "movie",
    anime = mode === "anime";
  let title =
    p.title || (movie ? "Dune: Part Two" : anime ? "One Piece" : "The Bear");
  let library = M.get("theatre.library." + title, true),
    notifications = M.get("theatre.notifications." + title, true);
  const watched = M.get(
    "theatre.watched." + title,
    p.watched ? { "2:1": true, "2:2": true } : {},
  );
  let activeEpisode = 1;
  const style = document.createElement("style");
  style.textContent = `.stage{overflow-y:auto;overflow-x:hidden}.content{position:relative;will-change:auto;padding-bottom:40px}.chrome{position:sticky;top:0;height:160px;width:100%;background:#07080bef}.season-placeholder{height:auto}.back,.sys,.top-scrim{position:fixed}.scroll-track{display:none}.hero .art{opacity:.6}.ep-actions button{color:inherit}.tu-detail-tools{position:absolute;right:54px;bottom:28px;display:flex;gap:8px;z-index:3}.movie-meta{padding:30px 54px;color:#cac9d0;line-height:1.7}.rebuilt .hero{height:432px}.rebuilt .hero-copy{bottom:34px}.rebuilt .hero h1{font-size:60px}.rebuilt .facts{padding:6px 0 18px}.rebuilt .chrome{height:66px}.rebuilt .tabs{padding-top:0}.rebuilt .columns{display:none}.rebuilt .episode{height:96px}.rebuilt .episode.next{height:96px}.episode:hover .episode-face{background:#ffffff06}.episode:focus-within .episode-face{background:#f0c44a12}.episode .round{cursor:pointer}.ep-state{right:205px}.ep-copy{right:310px}.tabs button{padding:0;cursor:pointer;background:none;border:0}.facts{padding-top:8px}.factlist{padding-top:20px}`;
  document.head.append(style);
  document.body.classList.toggle("rebuilt", rebuilt);
  document.title = title + " · Theatre fixture";
  $(".hero h1").textContent = title;
  $(".kicker").textContent =
    (movie ? "Movie" : anime ? "Anime" : "Series") + " - Theatre";
  $(".hero .art").src = movie
    ? "../assets/fixtures/captures/theatre-posters/dune.jpg"
    : "../assets/cold-ripple.jpg";
  $(".hero .art").alt = movie
    ? "Dune poster fixture"
    : "Colosseum wallpaper; series backdrop not bundled";
  $(".meta").innerHTML =
    `<span class="year">${movie ? "2024" : anime ? "1999–" : "2022–2025"}</span><span class="dash">-</span><span>${movie ? "Adventure - Science Fiction" : anime ? "Animation - Adventure" : "Comedy - Drama"}</span><span class="dash">-</span><span class="rating"><span class="imdb">IMDb</span> ${movie ? "8.5" : anime ? "9.0" : "8.5"}</span><span class="dash">-</span><span>${movie ? "166 min" : anime ? "24 min" : "35 min"}</span>`;
  $(".synopsis").textContent = movie
    ? "Paul Atreides unites with Chani and the Fremen while war spreads across Arrakis."
    : anime
      ? "Monkey D. Luffy and his crew sail the Grand Line in search of the legendary treasure known as the One Piece."
      : "A young chef from the fine dining world returns to Chicago to run his family’s sandwich shop.";
  $(".factlist").innerHTML = (
    movie
      ? [
          ["Director", "Denis Villeneuve"],
          ["Country", "United States"],
          ["Released", "2024"],
          ["Runtime", "166 min"],
        ]
      : anime
        ? [
            ["Creator", "Eiichiro Oda"],
            ["Country", "Japan"],
            ["Aired", "1999–"],
            ["Status", "Ongoing"],
          ]
        : [
            ["Writers", "Christopher Storer"],
            ["Country", "United States"],
            ["Aired", "2022–2025"],
            ["Status", "Returning series"],
          ]
  )
    .map(
      (x) =>
        `<div class="fact"><span class="key">${x[0]}</span><span class="value">${x[1]}</span></div>`,
    )
    .join("");
  // Reconcile catalogue context rather than lending another title's fixture facts.
  const knownDefault =
    title === "The Bear" || title === "One Piece" || title === "Dune: Part Two";
  $(".hero .art").src =
    p.cover ||
    (title === "Dune: Part Two"
      ? "../assets/fixtures/captures/theatre-posters/dune.jpg"
      : "../assets/cold-ripple.jpg");
  $(".hero .art").alt = p.cover
    ? title + " artwork fixture"
    : "Colosseum wallpaper fixture";
  if (!knownDefault) {
    $(".meta").innerHTML =
      `<span class="year">${esc(p.year || "Year unavailable")}</span><span class="dash">-</span><span>${esc(p.genre || p.genres || "Genre unavailable")}</span>${p.rating ? `<span class="dash">-</span><span class="rating"><span class="imdb">IMDb</span> ${esc(p.rating)}</span>` : ""}${p.runtime ? `<span class="dash">-</span><span>${esc(p.runtime)}</span>` : ""}`;
    $(".synopsis").textContent =
      p.synopsis ||
      p.description ||
      "Local catalogue fixture for " +
        title +
        ". Choose a source to explore playback controls.";
    $(".factlist").innerHTML = [
      ["Type", movie ? "Movie" : anime ? "Anime" : "Series"],
      ["Released", p.year || "Unavailable"],
      ["Genre", p.genre || p.genres || "Unavailable"],
      ["Metadata", "Local catalogue fixture"],
    ]
      .map(
        (x) =>
          `<div class="fact"><span class="key">${esc(x[0])}</span><span class="value">${esc(x[1])}</span></div>`,
      )
      .join("");
  }
  const names = {
    1: [
      "System",
      "Hands",
      "Brigade",
      "Dogs",
      "Sheridan",
      "Ceres",
      "Review",
      "Braciole",
    ],
    2: [
      "Beef",
      "Pasta",
      "Sundae",
      "Honeydew",
      "Pop",
      "Fishes",
      "Forks",
      "Bolognese",
      "Omelette",
      "The Bear",
    ],
    3: [
      "Tomorrow",
      "Next",
      "Doors",
      "Violet",
      "Children",
      "Napkins",
      "Legacy",
      "Ice Chips",
      "Apologies",
      "Forever",
    ],
    4: [
      "Groundhogs",
      "Soubise",
      "Scallop",
      "Worms",
      "Replicants",
      "Bears",
      "Bolognese",
      "Green",
      "Tonnato",
      "Goodbye",
    ],
    0: ["Behind the scenes · Fixture special"],
  };
  const descriptions = [
    "Faced with opening a new restaurant, the crew must make a plan.",
    "The crew splits up to work towards their new vision.",
    "Sydney searches Chicago for culinary inspiration.",
    "Marcus challenges himself.",
    "The renovation gets off track.",
    "Feast of the Seven Fishes.",
    "Richie stages.",
    "The crew awaits a do-or-die fire suppression test.",
    "Final preparations are made for the first service.",
    "Friends & Family night.",
  ];
  const source = (ep = 1, download) => {
    if (p.local && !download) {
      M.go("player.video", {
        ...p,
        title,
        episode: movie ? null : ep,
        season: movie ? null : season,
        local: true,
      });
      return;
    }
    return T.sourceSheet({
      ...p,
      title,
      episode: movie ? null : ep,
      season: movie ? null : season,
      mode: download,
    });
  };
  const actions = $$(".actions .action");
  activate(actions[0], () => source(activeEpisode), "Watch");
  actions[0].dataset.action = "watch";
  activate(
    actions[1],
    () => {
      if (!library) {
        library = true;
        saveLibrary();
        return;
      }
      const d = dialog(
        "Remove from your library?",
        `<p>${esc(title)}</p><div class="tu-stack" style="margin-top:20px">${button("Remove from Colosseum", "remove-local")}${button("Remove from Colosseum & Stremio", "remove-both")}${button("Cancel", "cancel")}</div><p class="tu-muted" style="margin-top:12px">Library changes are staged locally; connected services are unchanged.</p>`,
      );
      $("[data-action=cancel]", d.panel).onclick = d.close;
      $$("[data-action^=remove-]", d.panel).forEach(
        (b) =>
          (b.onclick = () => {
            library = false;
            saveLibrary();
            d.close();
          }),
      );
    },
    "Library",
  );
  actions[1].dataset.action = "library";
  activate(
    actions[2],
    () => {
      notifications = !notifications;
      M.set("theatre.notifications." + title, notifications);
      actions[2].textContent = notifications
        ? "Notifications on"
        : "Notifications off";
    },
    "Toggle notifications",
  );
  actions[2].dataset.action = "notifications";
  function saveLibrary(persist = true) {
    if (persist) M.set("theatre.library." + title, library);
    if (persist && p.id) M.set("collection." + p.id, library);
    actions[1].innerHTML = library
      ? '<span class="check">✓</span> In Library'
      : "+ Add to Library";
  }
  saveLibrary(false);
  actions[2].textContent = notifications
    ? "Notifications on"
    : "Notifications off";
  activate($(".back"), () => M.back(), "Back");
  $(".back").dataset.action = "back";
  $(".sys").remove();
  $("#seasonPlace").append($("#chrome"));
  const extra = document.createElement("div");
  extra.className = "tu-detail-tools";
  extra.innerHTML = button("⋯", "title-more");
  $(".hero").append(extra);
  $("[data-action=title-more]").onclick = () => {
    const d = dialog(
      title,
      `${button("Join Watch Party", "join-party")}<hr class="tu-divider"><p class="tu-muted">Current title layout: ${rebuilt ? "opt-in TheatreSeriesView" : "default TheatreSeries"}</p>${button(rebuilt ? "Default title layout" : "Preview rebuilt title layout", "variant")}`,
    );
    $("[data-action=join-party]", d.panel).onclick = () => {
      d.close();
      T.joinParty();
    };
    $("[data-action=variant]", d.panel).onclick = () =>
      M.go(rebuilt ? "theatre.detail" : "theatre.rebuilt", {
        ...p,
        title,
        type: mode,
        rebuilt: !rebuilt,
      });
  };
  if (rebuilt) {
    const badge = document.createElement("div");
    badge.className = "tu-variant";
    badge.textContent = "OPT-IN · REBUILT TITLE PAGE";
    $(".hero").append(badge);
  }
  function render() {
    $$(".tu-next-ledger,.tu-ledger-jump").forEach((n) => n.remove());
    if (movie) {
      $("#seasonPlace").hidden = true;
      $("#episodes").innerHTML =
        '<div class="movie-meta"><h2 style="font:25px Fraunces">Movie</h2><p>Choose Watch to see available sources.</p><p class="tu-muted">Metadata and poster are local fixtures.</p></div>';
      $("#watchText").textContent = "Watch";
      return;
    }
    const eps = !knownDefault
      ? Array.from(
          {
            length:
              season === 0 ? 1 : absolute ? Number(p.episodeCount) || 100 : 10,
          },
          (_, i) =>
            season === 0
              ? "Special · " + title
              : title + " · Episode " + (i + 1),
        )
      : anime
        ? season === 0
          ? ["Special · East Blue"]
          : Array.from(
              { length: absolute ? Number(p.episodeCount) || 100 : 12 },
              (_, i) =>
                i === 0
                  ? "I’m Luffy! The Man Who’s Gonna Be King of the Pirates!"
                  : "Adventure on the Grand Line · Episode " + (i + 1),
            )
        : names[season] || names[2];
    $("#tabs").innerHTML =
      (anime
        ? `<button class="tab ${absolute ? "selected" : ""}" data-action="absolute">Absolute</button><button class="tab ${!absolute ? "selected" : ""}" data-action="seasons">Seasons</button>`
        : "") +
      (!absolute
        ? Array.from({ length: Number(p.seasonCount) || 4 }, (_, i) => i + 1)
            .concat(0)
            .map(
              (n) =>
                `<button class="tab ${n === season ? "selected" : ""}" data-action="season" data-season="${n}">${n ? "Season " + n : "Specials"}</button>`,
            )
            .join("")
        : "");
    if (Number(p.seasonCount) > 10 && !absolute) {
      $("#tabs").innerHTML =
        (anime
          ? `<button class="tab" data-action="absolute">Absolute</button><button class="tab selected" data-action="seasons">Seasons</button>`
          : "") +
        `<button class="tu-season-trigger" data-action="season-picker" aria-label="Choose season" aria-haspopup="listbox" aria-expanded="false">${season ? "Season " + season : "Specials"} <span>▾</span></button>`;
      const trigger = $("[data-action=season-picker]");
      trigger.onclick = () => {
        const values = Array.from(
            { length: Number(p.seasonCount) },
            (_, i) => i + 1,
          ).concat(0),
          menu = document.createElement("div"),
          cover = document.createElement("div");
        cover.className = "tu-season-cover";
        menu.className = "tu-season-menu";
        menu.setAttribute("role", "listbox");
        menu.setAttribute("aria-label", "Choose season");
        menu.innerHTML = values
          .map(
            (n) =>
              `<button role="option" aria-selected="${n === season}" data-action="choose-season" data-season="${n}">${n ? "Season " + n : "Specials"}</button>`,
          )
          .join("");
        cover.append(menu);
        document.body.append(cover);
        const rect = trigger.getBoundingClientRect();
        menu.style.left =
          Math.min(rect.left, document.documentElement.clientWidth - 246) +
          "px";
        menu.style.top =
          (rect.bottom + 8 + 304 <= window.innerHeight - 10
            ? rect.bottom + 8
            : Math.max(10, rect.top - 312)) + "px";
        trigger.setAttribute("aria-expanded", "true");
        let dispose = () => {};
        const close = () => {
          dispose();
          cover.remove();
          trigger.setAttribute("aria-expanded", "false");
          trigger.focus();
        };
        dispose = M.registerOverlay(menu, close);
        cover.onclick = (e) => {
          if (e.target === cover) close();
        };
        const items = $$("button", menu);
        let index = Math.max(0, values.indexOf(season));
        function focus() {
          items[index].focus();
          items[index].scrollIntoView({ block: "nearest" });
        }
        items.forEach(
          (b, i) =>
            (b.onclick = () => {
              season = values[i];
              activeEpisode = 1;
              close();
              render();
              $("[data-action=season-picker]")?.focus();
            }),
        );
        menu.onkeydown = (e) => {
          if (e.key === "Tab") {
            e.preventDefault();
            return;
          }
          if (e.key === "ArrowDown") index = (index + 1) % items.length;
          else if (e.key === "ArrowUp")
            index = (index - 1 + items.length) % items.length;
          else if (e.key === "Home") index = 0;
          else if (e.key === "End") index = items.length - 1;
          else return;
          e.preventDefault();
          focus();
        };
        setTimeout(focus, 1);
      };
    }
    $$("[data-action=season]").forEach(
      (b) =>
        (b.onclick = () => {
          season = +b.dataset.season;
          activeEpisode = 1;
          render();
        }),
    );
    if (anime) {
      $("[data-action=absolute]").onclick = () => {
        absolute = true;
        render();
      };
      $("[data-action=seasons]").onclick = () => {
        absolute = false;
        render();
      };
    }
    $("#seasonTitle").textContent = absolute
      ? "Episodes"
      : season
        ? "Season " + season
        : "Specials";
    $("#seasonSub").textContent =
      eps.length +
      " episodes / " +
      Object.keys(watched).filter(
        (k) => k.startsWith(season + ":") && watched[k],
      ).length +
      " watched / next E" +
      activeEpisode;
    $("#watchText").textContent = "Watch  S" + season + " · E" + activeEpisode;
    activate(
      $(".season-download"),
      () => source(1, "season"),
      "Download season",
    );
    $(".season-download").dataset.action = "download-season";
    $(".season-download").textContent = M.get("theatre.queued." + title, false)
      ? "✓ Season queued"
      : "↓ Download season";
    $("#episodes").innerHTML = eps
      .map((name, i) => {
        const n = i + 1,
          key = season + ":" + n,
          seen = watched[key],
          progress = p.progress && n === 2 ? 35 : 0;
        return `<article class="episode ${n === activeEpisode ? "next" : ""}" data-episode="${n}"><div class="episode-face"></div>${n === activeEpisode ? '<div class="rail"></div>' : ""}<div class="ep-num"><strong>${n}</strong><small>${season ? "S" + season : "SPECIAL"}</small></div><div class="still"><img src="../assets/cold-ripple.jpg" alt="Episode image fixture">${progress ? '<div style="position:absolute;bottom:0;left:0;height:3px;width:' + progress + '%;background:#f0c44a"></div>' : ""}</div><div class="ep-copy"><div class="ep-title">${esc(name)}</div><div class="ep-sub">${seen ? "Watched" : p.local ? "Downloaded" : p.queued ? "Downloading" : progress ? progress + "% watched" : n === activeEpisode ? "Next up" : "Available"} · ${season === 0 ? "Special" : "S" + season + " · E" + n}</div><div class="ep-desc">${esc(anime || !knownDefault ? "Local episode metadata fixture." : descriptions[i % descriptions.length])}</div></div><div class="ep-state" style="color:${seen ? "#76b8aa" : ""}">${seen ? "✓ WATCHED" : p.local ? "ON DISK" : p.queued ? "QUEUED" : progress ? progress + "% WATCHED" : n === activeEpisode ? "NEXT UP" : "AVAILABLE"}</div><div class="ep-actions"><button class="round play" data-action="episode-play" data-ep="${n}" aria-label="Play episode ${n}"><svg viewBox="0 0 24 24"><path d="m8 5 11 7-11 7z"/></svg></button><button class="round" data-action="episode-download" data-ep="${n}" aria-label="Download episode ${n}">↓</button></div></article>`;
      })
      .join("");
    $$("[data-action=episode-play]").forEach(
      (b) =>
        (b.onclick = () => {
          activeEpisode = +b.dataset.ep;
          source(activeEpisode);
        }),
    );
    $$("[data-action=episode-download]").forEach(
      (b) => (b.onclick = () => source(+b.dataset.ep, "download")),
    );
    $$("[data-action=watched]").forEach(
      (b) =>
        (b.onclick = () => {
          const key = season + ":" + b.dataset.ep;
          watched[key] = !watched[key];
          M.set("theatre.watched." + title, watched);
          render();
        }),
    );
    $$(".episode").forEach((row) => {
      row.tabIndex = 0;
      row.oncontextmenu = (e) => {
        e.preventDefault();
        const d = dialog(
          "Episode " + row.dataset.episode,
          button("Play", "ep-context-play") +
            " " +
            button("Download", "ep-context-download"),
        );
        $("[data-action=ep-context-play]", d.panel).onclick = () => {
          d.close();
          source(+row.dataset.episode);
        };
        $("[data-action=ep-context-download]", d.panel).onclick = () => {
          d.close();
          source(+row.dataset.episode, "download");
        };
      };
      row.onkeydown = (e) => {
        if (e.target !== row) return;
        if (e.key === "Enter") {
          source(+row.dataset.episode);
          e.preventDefault();
        }
        if (e.key === "ArrowDown" || e.key === "ArrowUp") {
          const n =
            e.key === "ArrowDown"
              ? row.nextElementSibling
              : row.previousElementSibling;
          if (n?.classList.contains("episode")) {
            n.focus();
            e.preventDefault();
          }
        }
      };
    });
    if (rebuilt) {
      const next = document.createElement("button");
      next.className = "tu-next-ledger";
      next.dataset.action = "next-up-jump";
      next.textContent = "Next · E" + activeEpisode;
      next.onclick = () => {
        $('[data-episode="' + activeEpisode + '"]').scrollIntoView({
          block: "center",
        });
      };
      $("#chrome").append(next);
      $(".season-download").hidden = absolute;
      if (eps.length >= 12) {
        const ji = document.createElement("input");
        ji.className = "tu-ledger-jump";
        ji.type = "number";
        ji.placeholder = "Go to #";
        ji.setAttribute("aria-label", "Go to episode");
        ji.onkeydown = (e) => {
          if (e.key === "Enter") {
            const n = Math.max(1, Math.min(eps.length, +ji.value || 1));
            $('[data-episode="' + n + '"]').scrollIntoView({ block: "center" });
          }
        };
        $("#chrome").append(ji);
      }
    }
    if (absolute && eps.length > 100) {
      const ranges = document.createElement("nav");
      ranges.className = "tu-tabs";
      ranges.style.cssText = "padding:10px 54px";
      ranges.setAttribute("aria-label", "Episode ranges");
      ranges.innerHTML = Array.from(
        { length: Math.ceil(eps.length / 100) },
        (_, i) =>
          button(
            i * 100 + 1 + "–" + Math.min(eps.length, (i + 1) * 100),
            "range-" + (i * 100 + 1),
            "tu-chip",
          ),
      ).join("");
      $("#episodes").prepend(ranges);
      $$("[data-action^=range-]", ranges).forEach(
        (b) =>
          (b.onclick = () => {
            $(
              '[data-episode="' + b.dataset.action.slice(6) + '"]',
            ).scrollIntoView({ block: "center" });
          }),
      );
    }
    if (absolute) {
      const jump = document.createElement("div");
      jump.className = "tu-row";
      jump.style.cssText = "padding:15px 54px";
      jump.innerHTML =
        '<label>Go to episode <input type="number" min="1" max="${eps.length}" id="jump-episode" value="1" style="width:80px"></label>' +
        button("Jump", "jump");
      $("#episodes").prepend(jump);
      $("#jump-episode").max = eps.length;
      $("[data-action=jump]").onclick = () => {
        const n = Math.max(
          1,
          Math.min(eps.length, +$("#jump-episode").value || 1),
        );
        $('[data-episode="' + n + '"]').scrollIntoView({ block: "center" });
      };
    }
  }
  if (rebuilt) {
    $(".hero-copy").insertBefore($(".synopsis"), $(".actions"));
  }
  render();
  if (p.state === "error" || p.state === "loading") {
    const d = dialog(
      title,
      `<p role="status">${p.state === "error" ? "Could not load title metadata." : "Loading…"}</p><div style="margin-top:20px">${button(p.state === "error" ? "Retry" : "Show fixture metadata", "retry")}</div>`,
    );
    $("[data-action=retry]", d.panel).onclick = d.close;
  }
  if (p.sources) source(1);
  $(".stage").addEventListener("scroll", () =>
    $(".stage").classList.toggle("scrolled", $(".stage").scrollTop > 200),
  );
  window.addEventListener("mockstate", () => {
    library = p.id
      ? M.get("collection." + p.id, M.get("theatre.library." + title, library))
      : M.get("theatre.library." + title, library);
    notifications = M.get("theatre.notifications." + title, notifications);
    saveLibrary(false);
    actions[2].textContent = notifications
      ? "Notifications on"
      : "Notifications off";
  });
  M.ready();
})();
