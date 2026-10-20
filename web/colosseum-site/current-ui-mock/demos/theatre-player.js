(function () {
  "use strict";
  const T = TheatreUI,
    { M, $, $$, esc, button, choice, dialog, activate } = T,
    p = T.resolveMedia({
      title: M.params.title || "Dune: Part Two",
      ...M.params,
    });
  const isMovie = p.mediaType === "movie";
  let title = p.title || "Dune: Part Two",
    episode = +p.episode || 1,
    season = +p.season || 2,
    experimental = !!p.experimental,
    live = !!p.live;
  let paused = true,
    position = Number(p.position ?? 4482),
    duration =
      Number(p.duration) ||
      parseFloat(p.runtime) * 60 ||
      (isMovie ? 9960 : 2100),
    volume = 83,
    muted = false,
    speed = 1,
    skip = 10,
    a = -1,
    b = -1,
    sleep = "Off",
    sub = "English",
    audio = "English",
    subDelay = 0,
    audioDelay = 0,
    recording = false,
    upTimer = null,
    fit = "Fit";
  if (position > duration) position = 480;
  document.body.classList.toggle("experimental", experimental);
  let spaceTimer = null,
    spaceHeld = false,
    spaceFired = false,
    spaceBaseSpeed = 1,
    skipDismissed = false;
  const skipKind = p.skipSegment,
    skipSegment = skipKind
      ? {
          kind: skipKind,
          startSec:
            skipKind === "recap"
              ? 0
              : skipKind === "credits"
                ? duration - 90
                : 30,
          endSec:
            skipKind === "recap" ? 30 : skipKind === "credits" ? duration : 90,
        }
      : null;
  if (skipSegment && p.position === undefined)
    position = skipSegment.startSec + 5;
  const sessionId = "video:" + title;
  M.openSession({
    kind: "video",
    id: sessionId,
    title,
    route: experimental ? "player.experimental" : "player.video",
    params: p,
  });
  const paths = {
    back: "M19 12H5m7-7-7 7 7 7",
    minimize: "M5 12h14",
    close: "M18 6 6 18M6 6l12 12",
    play: "m8 5 11 7-11 7z",
    pause: "M8 4v16M16 4v16",
    volume: "M11 5 6 9H3v6h3l5 4V5Z M16 8a6 6 0 0 1 0 8 M19 5a10 10 0 0 1 0 14",
    stream: "M7 7h14m-4-4 4 4-4 4M17 17H3m4-4-4 4 4 4",
    download: "M12 3v12m-4-4 4 4 4-4M4 17v3h16v-3",
    fit: "M4 7h10m4 0h2M4 17h2m4 0h10",
    "seek-back": "M3 11a9 9 0 1 1 3.7 7.2M3 4v7h7",
    "seek-forward": "M21 11a9 9 0 1 0-3.7 7.2M21 4v7h-7",
    episodes: "M4 6h9M4 11h9M4 16h7m6-2 5 3-5 3z",
    audio: "m5 8 6 6m-7 0 6-6M7 4v2m-4 0h9M13 20l5-12 5 12m-8-5h6",
    captions: "M3 5h18v14H3zM7 12h4m2 0h4M7 15h3m4 0h3",
    speed: "M5 16a9 9 0 1 1 14 0M12 13l4-4",
    fullscreen: "M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5",
    party:
      "M8 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6M2 21v-3a5 5 0 0 1 10 0v3M16 5a3 3 0 0 1 0 6m0 3a5 5 0 0 1 6 5",
  };
  function icon(n, k) {
    n.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[k] || paths.play}"/></svg>`;
  }
  $$("[data-icon]").forEach((n) => icon(n, n.dataset.icon));
  $("#hoverCard").hidden = true;
  $("#subtitleMenu").remove();
  $(".now-title").textContent = title;
  $(".now-detail").textContent = live
    ? "Live channel · fixture"
    : isMovie
      ? (p.year || "Movie") + " · 1080p"
      : "S" + season + " · E" + episode + " · 1080p";
  $("#info h2").textContent = title.toUpperCase();
  $("#info p").textContent =
    title === "Dune: Part Two"
      ? "Paul Atreides unites with Chani and the Fremen while war spreads across Arrakis."
      : p.synopsis ||
        "Local playback fixture for " + title + ". No media stream is loaded.";
  $(".clock").textContent = "1:41 PM";
  $("#picture").src =
    p.cover ||
    (title === "Dune: Part Two"
      ? "../assets/fixtures/captures/theatre-posters/dune.jpg"
      : "../assets/cold-ripple.jpg");
  $("#thumbImage").src = $("#picture").src;
  $("#picture").alt = "Artwork fixture, not a decoded video frame";
  $("#picture").removeAttribute("data-catalog-backdrop");
  $("#picture").style.objectFit = "contain";
  $("#picture").style.filter = "brightness(.55)";
  const st = document.createElement("style");
  st.textContent =
    ".chrome{pointer-events:auto}.chrome .top-scrim,.chrome .bottom-scrim{pointer-events:none}.chrome{inset:auto;left:0;right:0}#topChrome{top:0;height:90px}#bottomChrome{top:0;height:720px;pointer-events:none}#bottomChrome .transport,#bottomChrome .seek-row{pointer-events:auto}.state-row{pointer-events:none}.timeline input{width:100%;height:22px;background:transparent;border:0}.volume input{width:125px}.tu-dialog .tu-choice{font-size:13px}#picture{left:0;top:0;width:100%;height:100%;opacity:.75}.right{gap:1px}.center{left:50%}.tu-toolbar-note{position:absolute;right:54px;top:110px;color:#aaa;font-size:11px}";
  document.head.append(st);
  const fixture = document.createElement("div");
  fixture.className = "tu-fixture";
  fixture.textContent = experimental
    ? "EXPERIMENTAL PLAYER2 · LOCAL FIXTURE"
    : "LOCAL PLAYER FIXTURE · NO VIDEO STREAM";
  $("#stage").append(fixture);
  $(".timeline").innerHTML =
    '<input id="seek" data-action="seek" type="range" min="0" max="' +
    duration +
    '" step="1" aria-label="Playback position">';
  $(".vol-line").outerHTML =
    '<input type="range" id="volume" data-action="volume" min="0" max="100" value="83" aria-label="Volume">';
  $("#seek").oninput = (e) => {
    position = +e.target.value;
    update();
  };
  $("#volume").oninput = (e) => {
    volume = +e.target.value;
    muted = false;
    update();
  };
  const fmt = (s) => {
    s = Math.floor(Math.max(0, s));
    const h = Math.floor(s / 3600),
      m = Math.floor((s % 3600) / 60);
    return (
      (h ? h + ":" : "") +
      (h ? String(m).padStart(2, "0") : m) +
      ":" +
      String(s % 60).padStart(2, "0")
    );
  };
  function endTime() {
    const minutes =
        13 * 60 + 41 + Math.round((duration - position) / 60 / speed),
      h = Math.floor(minutes / 60) % 24;
    return (
      (h % 12 || 12) +
      ":" +
      String(minutes % 60).padStart(2, "0") +
      (h < 12 ? " AM" : " PM")
    );
  }
  function update() {
    renderSkip();
    position = Math.max(0, Math.min(duration, position));
    $("#seek").value = position;
    $("#elapsed").textContent = fmt(position);
    $(".duration").textContent = fmt(duration);
    $("#playerState").textContent =
      (paused ? "Paused" : "Playing fixture") +
      (speed !== 1 ? " · " + speed + "×" : "") +
      (muted ? " · Muted" : "") +
      (sleep !== "Off" ? " · Sleep: " + sleep : "");
    $("#info").style.display = paused ? "block" : "none";
    icon($("#playGlyph"), paused ? "play" : "pause");
    $("#playButton").setAttribute("aria-label", paused ? "Play" : "Pause");
    $("#infoFacts").textContent =
      "Fixture · " + Math.round(duration / 60) + " min · ends " + endTime();
    $("#ends").textContent = "ENDS  " + endTime();
    $("#volume").value = muted ? 0 : volume;
    M.set("theatre.player." + title, {
      position,
      paused,
      volume,
      muted,
      speed,
      episode,
      season,
    });
    const loop = $(".tu-ab");
    if (loop)
      loop.textContent =
        "A-B loop · " +
        (a >= 0 ? fmt(a) : "--:--") +
        " / " +
        (b >= 0 ? fmt(b) : "--:--");
  }
  $("#seek").addEventListener("pointermove", (e) => {
    const rect = e.currentTarget.getBoundingClientRect(),
      ratio = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    $("#hoverCard").hidden = false;
    $("#hoverCard").style.left =
      Math.max(20, Math.min(1044, e.clientX - 108)) + "px";
    $("#previewTime").textContent = fmt(ratio * duration);
    $("#chapterCaption").textContent =
      "Chapter " + (Math.floor(ratio * 6) + 1) + " · poster preview fixture";
  });
  $("#seek").addEventListener(
    "pointerleave",
    () => ($("#hoverCard").hidden = true),
  );
  function toggle() {
    paused = !paused;
    update();
  }
  function seek(n) {
    position += n;
    update();
  }
  function bindIcon(k, fn, label) {
    const glyph = $('[data-icon="' + k + '"]');
    if (glyph) {
      const el = glyph.parentElement;
      activate(el, fn, label);
      el.dataset.action = k;
    }
  }
  function closeSession() {
    const d = dialog(
      "End this session?",
      `<p>Your spot stays in Continue Watching.</p><div class="tu-row" style="margin-top:24px">${button("Keep watching", "keep")}${button("End session", "end", "tu-btn primary")}</div>`,
    );
    $("[data-action=keep]", d.panel).onclick = d.close;
    $("[data-action=end]", d.panel).onclick = () => {
      M.closeSession(sessionId);
      d.close();
      M.back();
    };
  }
  bindIcon("back", () => M.back(), "Back to title");
  bindIcon("close", closeSession, "End session");
  bindIcon(
    "minimize",
    () => {
      M.toast("Player minimized into staged sessions");
      M.back();
    },
    "Minimize player",
  );
  activate($("#playButton"), toggle, "Play");
  $("#playButton").dataset.action = "play-pause";
  bindIcon("seek-back", () => seek(-skip), "Seek back");
  bindIcon("seek-forward", () => seek(skip), "Seek forward");
  bindIcon(
    "volume",
    () => {
      muted = !muted;
      update();
    },
    "Mute",
  );
  bindIcon("captions", subtitles, "Subtitles");
  bindIcon("audio", audioMenu, "Audio tracks");
  bindIcon("speed", speedMenu, "Speed & sleep");
  bindIcon("fit", aspect, "Aspect ratio");
  bindIcon("episodes", episodes, "Episodes & sources");
  bindIcon("stream", () => episodes("sources"), "Pick another stream");
  bindIcon(
    "download",
    () => T.sourceSheet({ ...p, title, episode, season, mode: "download" }),
    "Download",
  );
  bindIcon("fullscreen", toggleFullscreen, "Fullscreen");
  const partyBtn = document.createElement("button");
  partyBtn.className = "action";
  partyBtn.dataset.action = "watch-party";
  partyBtn.setAttribute("aria-label", "Watch Party");
  partyBtn.innerHTML = '<span class="glyph"></span>';
  icon($(".glyph", partyBtn), "party");
  partyBtn.onclick = () => T.party({ title, state: p.partyState });
  if (!experimental) $(".cluster.right").prepend(partyBtn);
  if (experimental) {
    ["Previous episode", "Next episode"].forEach((label, i) => {
      const n = document.createElement("button");
      n.className = "action";
      n.dataset.action = i ? "next-episode" : "previous-episode";
      n.setAttribute("aria-label", label);
      n.innerHTML =
        '<span class="glyph"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="' +
        (i ? "M5 5l11 7-11 7zM19 5v14" : "M19 5 8 12l11 7zM5 5v14") +
        '"/></svg></span>';
      if (isMovie || (!i && episode === 1)) n.hidden = true;
      n.onclick = () => {
        episode = Math.max(1, episode + (i ? 1 : -1));
        position = 0;
        $(".now-detail").textContent = "S" + season + " · E" + episode;
        update();
      };
      i ? $(".cluster.center").append(n) : $(".cluster.center").prepend(n);
    });
  }
  $("#stage").addEventListener("contextmenu", (e) => {
    if (!e.target.closest(".tu-dialog")) {
      e.preventDefault();
      more();
    }
  });
  function audioMenu() {
    const d = dialog(
      "Audio · 2 tracks",
      choice(
        "English",
        "audio-en",
        "AAC · 5.1 · Default",
        audio === "English",
      ) +
        choice("Español", "audio-es", "AAC · Stereo", audio === "Español") +
        delayRow("audio"),
      "popover",
    );
    $("[data-action=audio-en]", d.panel).onclick = () => {
      audio = "English";
      M.set("theatre.audio", audio);
      d.close();
    };
    $("[data-action=audio-es]", d.panel).onclick = () => {
      audio = "Español";
      M.set("theatre.audio", audio);
      d.close();
    };
    bindDelay(d, "audio");
  }
  function delayRow(type) {
    return `<hr class="tu-divider"><div class="tu-row tu-spread"><span class="tu-muted">SYNC</span>${button("−0.1", "delay-minus")}<span id="delay-value">${(type === "audio" ? audioDelay : subDelay).toFixed(2)}s</span>${button("+0.1", "delay-plus")}${button("0", "delay-reset")}</div>`;
  }
  function bindDelay(d, type) {
    const change = (n) => {
      if (type === "audio")
        audioDelay = Math.round((audioDelay + n) * 100) / 100;
      else subDelay = Math.round((subDelay + n) * 100) / 100;
      $("#delay-value", d.panel).textContent =
        (type === "audio" ? audioDelay : subDelay).toFixed(2) + "s";
    };
    $("[data-action=delay-minus]", d.panel).onclick = () => change(-0.1);
    $("[data-action=delay-plus]", d.panel).onclick = () => change(0.1);
    $("[data-action=delay-reset]", d.panel).onclick = () => {
      if (type === "audio") audioDelay = 0;
      else subDelay = 0;
      change(0);
    };
  }
  function subtitles() {
    if (experimental) {
      const d = dialog(
        "Subtitles · 3",
        ["Off", "English", "English (SDH)", "Español"]
          .map((x, i) =>
            choice(
              x,
              "exp-sub-" + i,
              i ? "SRT · " + (i === 3 ? "SPA" : "ENG") : "",
              sub === x,
            ),
          )
          .join("") + delayRow("sub"),
        "popover track-small",
      );
      $$("[data-action^=exp-sub-]", d.panel).forEach(
        (b, i) =>
          (b.onclick = () => {
            sub = ["Off", "English", "English (SDH)", "Español"][i];
            showSubtitle();
            d.close();
          }),
      );
      bindDelay(d, "sub");
      return;
    }
    let kind = "All",
      language = "All languages",
      filter = "All",
      view = "tracks";
    const tracks = [
      ["Off", "", false],
      ["English", "ENG · SRT · Embedded · Default", false],
      ["English (SDH)", "ENG · SRT · Embedded · HI / SDH", true],
      ["Español", "SPA · SRT · External", false],
      ["English (Forced)", "ENG · SRT · External · Forced", false],
    ];
    const d = dialog("Subtitles", "", "subtitles");
    function render() {
      d.body.innerHTML = `<div class="tu-controls-grid"><section><div class="tu-tabs">${["All", "Embedded", "External"].map((v) => button(v, "sub-kind-" + v, "tu-chip " + (kind === v ? "selected" : ""))).join("")}</div><label class="tu-label" style="margin-top:15px">LANGUAGE<select id="sub-language"><option>All languages</option><option>English</option><option>Español</option></select></label><div class="tu-tabs" style="margin:12px 0">${["All", "HI / SDH", "Forced"].map((v, i) => button(v, "sub-filter-" + i, "tu-chip " + (filter === v ? "selected" : ""))).join("")}</div><div id="subtitle-tracks"></div>${delayRow("sub")}<div class="tu-row" style="margin-top:12px">${button("Find more", "find-subs")}${button("Load file", "load-subs")}</div><div id="sub-search"></div></section><section><div class="tu-row tu-spread"><h3>Appearance</h3>${button("Reset", "reset-style")}</div><p class="tu-muted">Changes apply to the local subtitle preview.</p><div class="tu-stack" style="margin-top:12px"><label class="tu-label">FONT<select id="sub-font"><option>Sans serif</option><option>Serif</option><option>Monospace</option></select></label><label class="tu-label">SIZE<input id="sub-size" type="range" min="16" max="48" value="26"></label><label class="tu-label">TEXT COLOR<input id="sub-color" type="color" value="#ffffff"></label><label class="tu-label">OUTLINE<input id="sub-outline" type="range" min="0" max="5" value="2"></label><label class="tu-label">VERTICAL POSITION<input id="sub-pos" type="range" min="0" max="100" value="90"></label><label class="tu-label">ASS OVERRIDE<select id="sub-ass"><option>Off</option><option>On</option></select></label><div class="tu-subpreview">Subtitle preview</div></div></section></div>`;
      function rows() {
        const visible = tracks.filter(
          (x) =>
            x[0] === "Off" ||
            ((kind === "All" || x[1].includes(kind)) &&
              (language === "All languages" || x[0].includes(language)) &&
              (filter === "All" || x[1].includes(filter))),
        );
        $("#subtitle-tracks", d.panel).innerHTML =
          visible
            .map((x) => choice(x[0], "pick-sub", x[1], sub === x[0]))
            .join("") ||
          '<p class="tu-muted">No tracks match these filters.</p>';
        $$("[data-action=pick-sub]", d.panel).forEach(
          (b, i) =>
            (b.onclick = () => {
              sub = visible[i][0];
              M.set("theatre.subtitle", sub);
              showSubtitle();
              rows();
            }),
        );
      }
      rows();
      bindDelay(d, "sub");
      $$("[data-action^=sub-kind-]", d.panel).forEach(
        (b) =>
          (b.onclick = () => {
            kind = b.dataset.action.slice(9);
            render();
          }),
      );
      $$("[data-action^=sub-filter-]", d.panel).forEach(
        (b, i) =>
          (b.onclick = () => {
            filter = ["All", "HI / SDH", "Forced"][i];
            render();
          }),
      );
      $("#sub-language", d.panel).value = language;
      $("#sub-language", d.panel).onchange = (e) => {
        language = e.target.value;
        rows();
      };
      $("[data-action=find-subs]", d.panel).onclick = () => {
        const r = $("#sub-search", d.panel);
        r.innerHTML =
          '<h3>Find more</h3><p class="tu-muted">IMDb matched title · online results are fixtures</p>' +
          button("Search", "search-subs");
        $("[data-action=search-subs]", r).onclick = () => {
          r.innerHTML =
            choice(
              "English · OpenSubtitles fixture",
              "add-online",
              "SRT · External",
            ) + '<p class="tu-muted">No network request is made.</p>';
          $("[data-action=add-online]", r).onclick = () => {
            tracks.push([
              "English · Online fixture",
              "ENG · SRT · External",
              false,
            ]);
            sub = "English · Online fixture";
            rows();
            showSubtitle();
            r.innerHTML = '<p class="tu-muted">Fixture track added.</p>';
          };
        };
      };
      $("[data-action=load-subs]", d.panel).onclick = () => {
        tracks.push(["Local file fixture", "SRT · External", false]);
        sub = "Local file fixture";
        rows();
        showSubtitle();
        $("#sub-search", d.panel).innerHTML =
          '<p class="tu-muted">Sample external track loaded. Native file picker is simulated.</p>';
      };
      function style() {
        const pv = $(".tu-subpreview", d.panel),
          val = {
            font: $("#sub-font", d.panel).value,
            size: $("#sub-size", d.panel).value,
            color: $("#sub-color", d.panel).value,
            outline: $("#sub-outline", d.panel).value,
            pos: $("#sub-pos", d.panel).value,
            ass: $("#sub-ass", d.panel).value,
          };
        pv.style.fontFamily = val.font;
        pv.style.fontSize = val.size + "px";
        pv.style.color = val.color;
        pv.style.textShadow = `0 1px ${val.outline}px black`;
        M.set("theatre.subtitleStyle", val);
        showSubtitle();
      }
      $$("input,select", d.panel)
        .filter((e) => e.id !== "sub-language")
        .forEach((e) => e.addEventListener("input", style));
      $("[data-action=reset-style]", d.panel).onclick = () => {
        M.set("theatre.subtitleStyle", {});
        render();
        showSubtitle();
      };
    }
    render();
  }
  function showSubtitle() {
    let n = $(".tu-sample-sub");
    if (!n) {
      n = document.createElement("div");
      n.className = "tu-sample-sub";
      $("#stage").append(n);
    }
    n.textContent = sub === "Off" ? "" : "Subtitle preview · " + sub;
    const v = M.get("theatre.subtitleStyle", {});
    n.style.fontSize = (v.size || 26) + "px";
    n.style.color = v.color || "#fff";
    n.style.bottom = 155 + (90 - (+v.pos || 90)) * 2 + "px";
    n.style.fontFamily = v.font || "sans-serif";
  }
  function speedMenu() {
    const rates = [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2],
      timers = [
        "Off",
        "15 min",
        "30 min",
        "45 min",
        "1 hour",
        "2 hours",
        "3 hours",
        "4 hours",
        "6 hours",
        "End of episode",
        "End of next episode",
      ];
    if (experimental) {
      const ex = dialog(
        "Playback speed",
        rates
          .map((n, i) =>
            choice(
              n === 1 ? "Normal" : n + "×",
              "exp-rate-" + i,
              n === 1 ? "DEFAULT" : "",
              n === speed,
            ),
          )
          .join(""),
        "popover speed-panel",
      );
      rates.forEach(
        (n, i) =>
          ($("[data-action=exp-rate-" + i + "]", ex.panel).onclick = () => {
            speed = n;
            update();
            ex.close();
          }),
      );
      return;
    }
    const d = dialog(
      "Speed & sleep",
      `<div class="tu-controls-grid"><section><h3>Playback speed</h3>${rates.map((n, i) => choice(n + "×", "rate-" + i, "", speed === n)).join("")}</section><section><h3>Sleep timer</h3>${timers.map((n, i) => choice(n, "timer-" + i, "", sleep === n)).join("")}</section></div><hr class="tu-divider"><h3>Skip step</h3><div class="tu-row">${[5, 10, 15, 30].map((n) => button(n + "s", "skip-" + n, "tu-chip " + (skip === n ? "selected" : ""))).join("")}</div>`,
      "speed-panel",
    );
    rates.forEach(
      (n, i) =>
        ($("[data-action=rate-" + i + "]", d.panel).onclick = () => {
          speed = n;
          update();
          d.close();
        }),
    );
    timers.forEach(
      (n, i) =>
        ($("[data-action=timer-" + i + "]", d.panel).onclick = () => {
          sleep = n;
          M.set("theatre.sleep", n);
          update();
          d.close();
        }),
    );
    $$("[data-action^=skip-]", d.panel).forEach(
      (e) =>
        (e.onclick = () => {
          skip = +e.dataset.action.slice(5);
          $$(".seek small").forEach((x) => (x.textContent = skip));
          d.close();
          speedMenu();
        }),
    );
  }
  function aspect() {
    const values = ["Fit", "Fill", "Zoom", "16:9", "4:3", "2.39:1"];
    const d = dialog(
      "Aspect ratio",
      values.map((x, i) => choice(x, "fit-" + i, "", fit === x)).join(""),
      "popover",
    );
    values.forEach(
      (x, i) =>
        ($("[data-action=fit-" + i + "]", d.panel).onclick = () => {
          fit = x;
          $("#picture").style.objectFit = x === "Fill" ? "cover" : "contain";
          $("#picture").style.transform = x === "Zoom" ? "scale(1.35)" : "none";
          $("#picture").style.width = x === "4:3" ? "75%" : "100%";
          $("#picture").style.left = x === "4:3" ? "12.5%" : "0";
          d.close();
        }),
    );
  }
  function episodes(initial) {
    let tab = initial === "sources" || isMovie ? "sources" : "episodes",
      drawerState = p.drawerState || "ready";
    const d = dialog(title, "", "source-drawer");
    d.ov.style.background = "transparent";
    function render() {
      d.body.innerHTML = `<div class="tu-row">${!isMovie ? button("EPISODES", "drawer-episodes", "tu-chip " + (tab === "episodes" ? "selected" : "")) : ""}${button("SOURCES", "drawer-sources", "tu-chip " + (tab === "sources" ? "selected" : ""))}</div><div id="drawer-content" style="margin-top:18px"></div>`;
      const content = $("#drawer-content", d.panel);
      const ep = $("[data-action=drawer-episodes]", d.panel);
      if (ep)
        ep.onclick = () => {
          tab = "episodes";
          render();
        };
      $("[data-action=drawer-sources]", d.panel).onclick = () => {
        tab = "sources";
        render();
      };
      if (tab === "episodes") {
        content.innerHTML =
          '<div class="tu-tabs">' +
          [0, 1, 2, 3, 4]
            .map((n) =>
              button(
                n ? "S" + n : "Specials",
                "drawer-season-" + n,
                "tu-chip " + (season === n ? "selected" : ""),
              ),
            )
            .join("") +
          '</div><div id="drawer-episodes"></div>';
        $$("[data-action^=drawer-season-]", content).forEach(
          (n) =>
            (n.onclick = () => {
              season = +n.dataset.action.slice(14);
              render();
            }),
        );
        const rows = $("#drawer-episodes", content);
        if (drawerState !== "ready") {
          rows.innerHTML =
            '<p class="tu-muted" style="margin:20px 0">' +
            (drawerState === "error"
              ? "Couldn’t load other seasons."
              : "Loading seasons…") +
            "</p>" +
            button("Retry", "drawer-retry");
          $("[data-action=drawer-retry]", rows).onclick = () => {
            drawerState = "ready";
            render();
          };
          return;
        }
        rows.innerHTML = Array.from(
          { length: season === 0 ? 1 : 10 },
          (_, i) =>
            `<button class="tu-choice drawer-episode ${episode === i + 1 ? "selected" : ""}" data-action="drawer-play-episode" data-episode="${i + 1}"><span>E${i + 1}</span><span>${season === 0 ? "Special" : title === "The Bear" ? ["Beef", "Pasta", "Sundae", "Honeydew", "Pop", "Fishes", "Forks", "Bolognese", "Omelette", "The Bear"][i] : title + " · Episode " + (i + 1)}</span><span class="tu-muted">${episode === i + 1 ? "PLAYING" : i === 0 ? "✓" : ""}</span></button>`,
        ).join("");
        $$("[data-action=drawer-play-episode]", rows).forEach(
          (n) =>
            (n.onclick = () => {
              episode = +n.dataset.episode;
              position = 0;
              $(".now-detail").textContent = "S" + season + " · E" + episode;
              d.close();
              update();
            }),
        );
      } else {
        if (p.local) {
          content.innerHTML =
            '<p class="tu-muted">This is a downloaded file — no live sources.</p>';
          return;
        }
        const values = [
          ["Torrentio", "4K", "142 seeders"],
          ["Torrentio", "1080p", "328 seeders"],
          ["NoTorrent", "1080p", "Direct HTTP fixture"],
          ["Torrentio", "SD", "didn’t start"],
        ];
        content.innerHTML = values
          .map(
            (v, i) =>
              `<div class="drawer-source"><button class="tu-choice ${i === (p.sourceIndex || 0) ? "selected" : ""}" data-action="drawer-pick-source" data-i="${i}" ${i === 3 ? "disabled" : ""}><span>${v[0]} <b>${v[1]}</b><small>${v[2]}</small></span><span class="tu-muted">${i === (p.sourceIndex || 0) ? "PLAYING" : ""}</span></button>${!experimental ? button("⧉", "drawer-copy-" + i, "tu-btn drawer-copy") : ""}</div>`,
          )
          .join("");
        $$("[data-action=drawer-pick-source]", content).forEach(
          (n) =>
            (n.onclick = () => {
              p.sourceIndex = +n.dataset.i;
              p.source = values[p.sourceIndex].slice(0, 2).join(" ");
              M.set("theatre.source", p.source);
              d.close();
              M.toast("Selected " + p.source + " · fixture");
            }),
        );
        $$("[data-action^=drawer-copy-]", content).forEach(
          (n) =>
            (n.onclick = () => {
              n.textContent = "✓";
              M.set(
                "theatre.copiedReference",
                "fixture-source-" + n.dataset.action.slice(12),
              );
            }),
        );
      }
    }
    render();
  }
  function stats() {
    dialog(
      "Playback stats",
      [
        "Resolution|1920 × 1080 (fixture)",
        "Video codec|H.264",
        "Audio codec|AAC · 5.1",
        "FPS|23.976 (fixture)",
        "Dropped frames|0 (fixture)",
        "Cache buffering|0% (fixture)",
        "Audio track|" + audio,
        "Subtitle track|" + sub,
        "Speed|" + speed + "×",
        "Volume|" + (muted ? 0 : volume) + "%",
      ]
        .map((x) => {
          const [k, v] = x.split("|");
          return `<div class="tu-choice"><span>${k}</span><span class="tu-muted">${v}</span></div>`;
        })
        .join("") +
        '<p class="tu-muted">UI sample data. No decoder or real playback measurements.</p>',
      "popover",
    );
  }
  function shortcuts() {
    dialog(
      "Keyboard shortcuts",
      [
        [
          "Space",
          experimental
            ? "Play / Pause"
            : "Play / Pause; hold 350ms for temporary ≥2×",
        ],
        [experimental ? "F / F11" : "F11", "Toggle staged fullscreen"],
        ["← / →", "Seek back / forward"],
        [", / .", "Frame step while paused; otherwise 30s"],
        ["Home / End", "Jump to start / end"],
        ["0–9", "Seek to 0–90%"],
        ["↑ / ↓", "Volume; Shift for larger step"],
        ["M", "Mute"],
        ["[ / ]", "Playback speed"],
        ["S / C", "Cycle subtitles"],
        ["Z / X", "Subtitle delay"],
        ["I / O / L", "Set A / set B / clear loop"],
        ["D", "Playback stats"],
        ["E", "Episodes & sources"],
        ["Shift+F10", "More controls"],
        ["?", "Keyboard shortcuts"],
        ["Esc", "Close menu / Back"],
      ]
        .map(
          (x) =>
            `<div class="tu-choice"><span>${x[1]}</span><strong>${x[0]}</strong></div>`,
        )
        .join(""),
      "wide",
    );
  }
  function loop(point) {
    if (point === "a") a = position;
    if (point === "b") b = position;
    if (point === "clear") {
      a = b = -1;
      $(".tu-ab")?.remove();
      return;
    }
    if (!$(".tu-ab")) {
      const n = document.createElement("div");
      n.className = "tu-ab";
      $("#stage").append(n);
    }
    update();
  }
  function capture(kind) {
    const d = dialog(
      kind === "gif" ? "Record GIF" : "Screenshot",
      kind === "gif"
        ? `<p id="capture-state">Recording GIF · fixture</p><div class="tu-row" style="margin-top:18px">${button("Stop", "capture-stop", "tu-btn primary")}${button("Discard", "capture-discard")}</div>`
        : `<p>Screenshot staged from the poster fixture.</p><p class="tu-muted">No native file was written.</p>${button("Open folder", "capture-folder")}`,
    );
    if (kind === "gif") {
      recording = true;
      $("[data-action=capture-discard]", d.panel).onclick = () => {
        recording = false;
        d.close();
      };
      $("[data-action=capture-stop]", d.panel).onclick = () => {
        recording = false;
        $("#capture-state", d.panel).textContent =
          "GIF encoding complete · fixture only; no file written.";
        $("[data-action=capture-stop]", d.panel).disabled = true;
      };
    } else
      $("[data-action=capture-folder]", d.panel).onclick = () => {
        M.toast("Native output-folder opening is simulated");
      };
  }
  function guide() {
    const d = dialog(
      "Live guide",
      `<label class="tu-label">Search channels<input id="live-query" placeholder="Search channels"></label>${button("Clear", "clear-live")}<div id="channels"></div>`,
      "popover",
    );
    function draw() {
      const q = $("#live-query", d.panel).value.toLowerCase();
      $("#channels", d.panel).innerHTML =
        [
          "News · World headlines",
          "Cinema · Evening feature",
          "Documentary · Planet Earth",
        ]
          .filter((x) => x.toLowerCase().includes(q))
          .map((x, i) => choice(x, "channel-" + i, "Live channel fixture"))
          .join("") || '<p class="tu-muted">No channels match.</p>';
      $$("[data-action^=channel-]", d.panel).forEach(
        (e) =>
          (e.onclick = () => {
            $(".now-title").textContent = e.innerText.split("\n")[0];
            position = duration;
            d.close();
            update();
          }),
      );
    }
    $("#live-query", d.panel).oninput = draw;
    $("[data-action=clear-live]", d.panel).onclick = () => {
      $("#live-query", d.panel).value = "";
      draw();
      $("#live-query", d.panel).focus();
    };
    draw();
  }
  function dvr() {
    const d = dialog(
      "DVR record",
      `<p>Live channel · local fixture</p><p class="tu-muted">Output directory and recording are native-only simulations.</p><div class="tu-row" style="margin-top:15px">${button("Start recording", "dvr-start")}${button("Close", "cancel")}</div><div id="dvr-items"></div>`,
      "popover",
    );
    $("[data-action=cancel]", d.panel).onclick = d.close;
    $("[data-action=dvr-start]", d.panel).onclick = () => {
      $("#dvr-items", d.panel).innerHTML =
        "<h3>Live channel</h3><p>recording / 0s / 0 KB · fixture</p>" +
        button("Stop", "dvr-stop");
      $("[data-action=dvr-stop]", d.panel).onclick = () => {
        $("#dvr-items", d.panel).innerHTML =
          "<p>Stopped · no recording file created.</p>";
      };
    };
  }
  function more() {
    const items = [
      ["Screenshot", () => capture("image")],
      [recording ? "Stop GIF" : "Record GIF", () => capture("gif")],
      ["Playback stats", stats],
      [
        "Loudness · " + M.get("theatre.loudness", "Off"),
        () => {
          M.set(
            "theatre.loudness",
            M.get("theatre.loudness", "Off") === "Off" ? "Normalize" : "Off",
          );
          M.toast("Loudness preference staged");
        },
      ],
      ...(live
        ? [
            ["Live guide", guide],
            ["DVR record", dvr],
            [
              "Jump to live edge",
              () => {
                position = duration;
                update();
              },
            ],
          ]
        : []),
      [
        "Picture-in-picture",
        () => {
          M.set("theatre.pip", !M.get("theatre.pip", false));
          M.toast("Picture-in-picture staged; native window unchanged");
        },
      ],
      ["Keyboard shortcuts", shortcuts],
    ];
    if (experimental) items.splice(0, 2);
    const d = dialog(
      "More controls",
      items.map((x, i) => choice(x[0], "more-" + i)).join(""),
      "popover",
    );
    items.forEach(
      (x, i) =>
        ($("[data-action=more-" + i + "]", d.panel).onclick = () => {
          d.close();
          x[1]();
        }),
    );
  }
  function upnext() {
    if ($(".tu-upnext")) return;
    let count = 20;
    const n = document.createElement("section");
    n.className = "tu-upnext";
    n.innerHTML =
      '<p id="next-count">Up next · Playing in 20s</p><h3>Episode ' +
      (episode + 1) +
      " · " +
      title +
      '</h3><div class="tu-row">' +
      button("Cancel", "next-cancel") +
      button("Play now", "next-play", "tu-btn primary") +
      "</div>";
    $("#stage").append(n);
    renderSkip();
    const finish = () => {
      clearInterval(upTimer);
      n.remove();
      episode++;
      position = 0;
      paused = false;
      $(".now-detail").textContent = "S" + season + " · E" + episode;
      update();
    };
    $("[data-action=next-cancel]", n).onclick = () => {
      clearInterval(upTimer);
      n.remove();
      renderSkip();
    };
    $("[data-action=next-play]", n).onclick = finish;
    upTimer = setInterval(() => {
      count--;
      if (count <= 0) finish();
      else
        $("#next-count", n).textContent = "Up next · Playing in " + count + "s";
    }, 1000);
  }
  function statePanel(kind) {
    const n = document.createElement("section");
    n.className = "tu-status";
    n.innerHTML =
      "<h2>" +
      ({
        loading: "Opening stream…",
        buffering: "Buffering…",
        error: "Playback failed",
        resume: "Resume from " + fmt(position),
      }[kind] || kind) +
      '</h2><p class="tu-muted">' +
      (kind === "error"
        ? "The fixture source could not be opened."
        : "Player state fixture. No media is being transferred.") +
      '</p><div class="tu-row" style="margin-top:20px">' +
      button(
        kind === "resume"
          ? "Resume"
          : kind === "error"
            ? "Retry"
            : "Continue fixture",
        "state-continue",
        "tu-btn primary",
      ) +
      button(
        kind === "resume" ? "Start over" : "Pick another stream",
        "state-other",
      ) +
      "</div>";
    $("#stage").append(n);
    let dispose = M.registerOverlay(n, () => n.remove());
    function close() {
      dispose();
      n.remove();
    }
    $("[data-action=state-continue]", n).onclick = () => {
      close();
      paused = false;
      update();
    };
    $("[data-action=state-other]", n).onclick = () => {
      close();
      if (kind === "resume") {
        position = 0;
        paused = false;
        update();
      } else
        T.sourceSheet({
          ...p,
          title,
          episode,
          season,
          onPick: () => {
            paused = false;
            update();
          },
        });
    };
  }
  function toggleFullscreen() {
    const on = !M.get("theatre.fullscreen", false);
    M.set("theatre.fullscreen", on);
    document.documentElement.dataset.mockFullscreen = String(on);
    const btn = $("[data-action=fullscreen]");
    btn.setAttribute("aria-pressed", String(on));
    btn.setAttribute("aria-label", on ? "Exit fullscreen" : "Enter fullscreen");
    M.toast(
      on
        ? "Fullscreen staged; browser window unchanged"
        : "Windowed layout staged",
    );
  }
  function renderSkip() {
    let pill = $("[data-action=skip-segment]");
    const visible =
      skipSegment &&
      !live &&
      p.showSkipButton !== false &&
      p.controlsShown !== false &&
      !skipDismissed &&
      !$(".tu-upnext") &&
      position >= skipSegment.startSec &&
      position < skipSegment.endSec;
    if (!visible) {
      if (pill) pill.hidden = true;
      return;
    }
    if (!pill) {
      pill = document.createElement("button");
      pill.className = "tu-skip-pill";
      pill.dataset.action = "skip-segment";
      pill.innerHTML =
        "<strong>" +
        ({ intro: "Skip Intro", recap: "Skip Recap", credits: "Skip Credits" }[
          skipKind
        ] || "Skip Credits") +
        "</strong><span>Skip</span>";
      pill.onclick = () => {
        position = Math.min(duration - 0.25, skipSegment.endSec + 0.75);
        skipDismissed = true;
        update();
      };
      $("#stage").append(pill);
    }
    pill.hidden = false;
  }
  function beginSpace() {
    if (spaceHeld) return;
    spaceHeld = true;
    spaceFired = false;
    spaceBaseSpeed = speed;
    spaceTimer = setTimeout(() => {
      spaceFired = true;
      speed = Math.max(2, spaceBaseSpeed);
      update();
    }, 350);
  }
  function endSpace(cancel = false) {
    if (!spaceHeld) return;
    clearTimeout(spaceTimer);
    spaceHeld = false;
    if (spaceFired) {
      speed = spaceBaseSpeed;
      update();
    } else if (!cancel) toggle();
    spaceFired = false;
  }
  document.addEventListener("keyup", (e) => {
    if (e.code === "Space" && spaceHeld) {
      e.preventDefault();
      endSpace();
    }
  });
  window.addEventListener("blur", () => endSpace(true));
  // F11 is application-scoped in Main.qml; F is local to Player2.
  document.addEventListener(
    "keydown",
    (e) => {
      if (e.key === "F11") {
        e.preventDefault();
        e.stopImmediatePropagation();
        toggleFullscreen();
      }
    },
    true,
  );
  document.addEventListener("keydown", (e) => {
    if (
      e.defaultPrevented ||
      e.target.closest("input,select,textarea,.tu-dialog,.tu-sources") ||
      $(".tu-overlay")
    )
      return;
    const key = e.key.toLowerCase();
    let handled = true;
    if (key === " ") {
      if (experimental) {
        if (!e.repeat) toggle();
      } else if (!e.repeat) beginSpace();
    } else if (key === "arrowleft") seek(-skip);
    else if (key === "arrowright") seek(skip);
    else if (key === "home") {
      position = 0;
      update();
    } else if (key === "end") {
      position = duration;
      update();
      upnext();
    } else if (/^[0-9]$/.test(key)) {
      position = (duration * Number(key)) / 10;
      update();
    } else if (key === "arrowup" || key === "arrowdown") {
      volume = Math.max(
        0,
        Math.min(
          100,
          volume + (key === "arrowup" ? 1 : -1) * (e.shiftKey ? 10 : 5),
        ),
      );
      update();
    } else if (key === "m") {
      muted = !muted;
      update();
    } else if (key === "[" || key === "]") {
      speed = Math.max(0.5, Math.min(2, speed + (key === "[" ? -0.25 : 0.25)));
      update();
    } else if (key === "," || key === ".") {
      seek((key === "," ? -1 : 1) * (paused ? 1 / 24 : 30));
    } else if (key === "s" || key === "c") {
      sub = sub === "Off" ? "English" : sub === "English" ? "Español" : "Off";
      showSubtitle();
    } else if (key === "z" || key === "x") {
      subDelay += (key === "z" ? -1 : 1) * (e.shiftKey ? 0.01 : 0.1);
      M.toast("Subtitle delay " + subDelay.toFixed(2) + "s");
    } else if (key === "i") loop("a");
    else if (key === "o") loop("b");
    else if (key === "l") loop("clear");
    else if (key === "d") stats();
    else if (key === "e") episodes();
    else if (key === "f" && experimental) toggleFullscreen();
    else if (key === "?") shortcuts();
    else if ((e.shiftKey && key === "f10") || key === "contextmenu") more();
    else handled = false;
    if (handled) e.preventDefault();
  });
  setInterval(() => {
    if (!paused && !$(".tu-status")) {
      position += speed;
      if (a >= 0 && b > a && position >= b) position = a;
      if (position >= duration) {
        paused = true;
        if (sleep === "End of episode") sleep = "Off";
        else upnext();
      }
      update();
    }
  }, 1000);
  update();
  if (["loading", "buffering", "error", "resume"].includes(p.state))
    statePanel(p.state);
  if (p.state === "upnext") upnext();
  if (p.menu === "subtitles") subtitles();
  if (p.menu === "audio") audioMenu();
  if (p.menu === "speed") speedMenu();
  if (p.menu === "more") more();
  if (p.menu === "stats") stats();
  if (p.menu === "episodes") episodes();
  if (p.menu === "sources") episodes("sources");
  if (p.menu === "gif") capture("gif");
  if (p.menu === "capture") capture("image");
  if (p.menu === "dvr") dvr();
  if (p.menu === "shortcuts") shortcuts();
  if (p.menu === "aspect") aspect();
  if (p.menu === "watchparty") T.party({ title, state: p.partyState });
  if (p.menu === "live") guide();
  M.ready();
})();
