/* Local fixture controls only. No provider, torrent, party, authentication or native file IO. */
(function () {
  "use strict";
  const M = window.MockUI,
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
  const $ = (s, root = document) => root.querySelector(s),
    $$ = (s, root = document) => Array.from(root.querySelectorAll(s));
  function button(label, action, cls = "tu-btn") {
    return `<button class="${cls}" data-action="${action}">${label}</button>`;
  }
  function dialog(title, body, cls = "") {
    const ov = document.createElement("div");
    ov.className = "tu-overlay";
    ov.innerHTML = `<section class="tu-dialog ${cls}" aria-label="${esc(title)}"><header><h2>${esc(title)}</h2>${button("×", "close", "tu-close")}</header><div class="tu-dialog-body">${body}</div></section>`;
    document.body.append(ov);
    const panel = $(".tu-dialog", ov);
    let dispose = () => {};
    const close = () => {
      dispose();
      ov.remove();
    };
    dispose = M.registerOverlay(panel, close);
    $("[data-action=close]", ov).onclick = close;
    ov.addEventListener("click", (e) => {
      if (e.target === ov) close();
    });
    return { ov, panel, body: $(".tu-dialog-body", ov), close };
  }
  function choice(label, action, meta = "", selected = false) {
    return `<button class="tu-choice ${selected ? "selected" : ""}" data-action="${action}"><span>${esc(label)}${meta ? `<small>${esc(meta)}</small>` : ""}</span><span>${selected ? "✓" : ""}</span></button>`;
  }
  function activate(el, fn, label) {
    if (!el) return;
    el.tabIndex = 0;
    el.setAttribute("role", "button");
    if (label) {
      el.setAttribute("aria-label", label);
      el.title = label;
    }
    el.onclick = fn;
    el.onkeydown = (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        fn(e);
      }
    };
  }
  const sources = [
    {
      provider: "Torrentio",
      quality: "4K",
      release: "Dune.Part.Two.2024.2160p.UHD.BluRay.REMUX.HEVC",
      meta: "Seeders 142 · 64.8 GB · UHD BluRay",
      audio: "TrueHD · 7.1",
      tags: "HDR10 · HEVC · REMUX",
      kind: "Torrent",
      ready: false,
    },
    {
      provider: "Torrentio",
      quality: "1080p",
      release: "Dune.Part.Two.2024.1080p.BluRay.x264",
      meta: "Seeders 328 · 12.4 GB · BluRay",
      audio: "DTS · 5.1",
      tags: "AVC · BluRay",
      kind: "Torrent",
      ready: true,
    },
    {
      provider: "NoTorrent",
      quality: "1080p",
      release: "Dune: Part Two · Direct HTTP fixture",
      meta: "Direct · 6.2 GB · Ready",
      audio: "AAC · 5.1",
      tags: "AVC · Direct",
      kind: "Direct",
      ready: true,
    },
    {
      provider: "NoTorrent",
      quality: "720p",
      release: "Dune: Part Two · 720p HTTP fixture",
      meta: "Direct · 2.1 GB · Ready",
      audio: "AAC · Stereo",
      tags: "AVC · Direct",
      kind: "Direct",
      ready: true,
    },
    {
      provider: "Torrentio",
      quality: "SD",
      release: "Dune.Part.Two.2024.SD",
      meta: "Seeders 0 · 940 MB · Unavailable fixture",
      audio: "AAC · Stereo",
      tags: "SD",
      kind: "Torrent",
      disabled: true,
    },
  ];
  function resolveMedia(input = {}) {
    const list = window.MockCatalog?.theatre || [],
      match =
        list.find((x) => input.id && x.id === input.id) ||
        list.find((x) => x.title === input.title) ||
        {},
      media = { ...match, ...input };
    const raw =
      input.type ||
      input.mediaType ||
      input.kind ||
      match.kind ||
      (input.title === "Dune: Part Two" ? "movie" : "series");
    media.mediaType =
      raw === "movies" ? "movie" : raw === "shows" ? "series" : raw;
    media.kind =
      media.mediaType === "movie"
        ? "movies"
        : media.mediaType === "anime"
          ? "anime"
          : "shows";
    media.cover = /^\.\.\/assets\//.test(media.cover || "") ? media.cover : "";
    return media;
  }
  function sourceSheet(opts = {}) {
    opts = resolveMedia({ title: opts.title || "Dune: Part Two", ...opts });
    const root = document.createElement("section");
    root.className = "tu-sources";
    root.setAttribute("aria-label", "Sources");
    root.innerHTML = `<div class="tu-source-banner"><div>${button("‹ Back", "source-back")}</div><div><div class="kicker">${opts.mode === "season" ? "DOWNLOAD SEASON — FULL-SEASON TORRENTS" : opts.mode === "download" ? "DOWNLOAD — PICK A SOURCE" : esc(opts.episode ? "S" + (opts.season || 2) + " · E" + opts.episode : "MOVIE · THEATRE")}</div><h1>${esc(opts.title || "Dune: Part Two")}</h1></div></div><div class="tu-source-body"><button class="tu-source-provider" data-action="provider-filter">All <span style="float:right">▾</span></button><div class="tu-source-filter"></div><div class="tu-row tu-spread tu-muted"><span id="source-count"></span><span>Fixture sources · extension request order</span></div><div id="source-results"></div></div>`;
    document.body.append(root);
    $(".tu-source-banner", root).style.backgroundImage =
      "linear-gradient(#0007,#000f),url('" +
      (opts.cover || "../assets/cold-ripple.jpg") +
      "')";
    let dispose = () => {},
      provider = "All",
      quality = "all",
      state = opts.state || "ready";
    const close = () => {
      dispose();
      root.remove();
      opts.onClose?.();
    };
    dispose = M.registerOverlay(root, close);
    $("[data-action=source-back]", root).onclick = close;
    function render() {
      let base = (state === "ready" ? sources : []).filter(
        (x) => provider === "All" || x.provider === provider,
      );
      let rows = base.filter((x) => quality === "all" || x.quality === quality);
      $(".tu-source-filter", root).innerHTML = [
        "all",
        "4K",
        "1080p",
        "720p",
        "SD",
      ]
        .filter((q) => q === "all" || base.some((x) => x.quality === q))
        .map((q) =>
          button(
            `${q === "all" ? "All" : q}  ${q === "all" ? base.length : base.filter((x) => x.quality === q).length}`,
            "quality-" + q,
            "tu-chip " + (quality === q ? "selected" : ""),
          ),
        )
        .join("");
      $$('[data-action^="quality-"]', root).forEach(
        (b) =>
          (b.onclick = () => {
            quality = b.dataset.action.slice(8);
            render();
          }),
      );
      $("#source-count", root).textContent = rows.length + " sources";
      const r = $("#source-results", root);
      if (state !== "ready") {
        r.innerHTML = `<div class="tu-source-status" role="status">${state === "loading" ? "Finding sources…" : state === "empty" ? "No sources found for this title." : state === "disabled" ? "No enabled stream extensions." : "Source request timed out. Check enabled extensions and try again."}<div style="margin-top:18px">${button(state === "loading" ? "Show fixture results" : "Retry", "source-retry")} ${button("Manage extensions", "manage-extensions")}</div></div>`;
        $("[data-action=source-retry]", r).onclick = () => {
          state = "ready";
          render();
        };
        $("[data-action=manage-extensions]", r).onclick = () =>
          M.go("extensions");
        return;
      }
      r.innerHTML = rows
        .map((x) => {
          const i = sources.indexOf(x);
          return `<article class="tu-source-row" data-testid="source-row" tabindex="0"><img src="../assets/addon-logos/${x.provider === "Torrentio" ? "torrentio" : "notorrent"}.png" alt="${x.provider}" onerror="this.hidden=true"><div><h3>${x.provider}</h3><div class="quality">${x.quality}</div><div class="release">${esc(opts.title && opts.title !== "Dune: Part Two" ? opts.title + " · " + x.quality + " fixture release" : x.release)}</div><div class="tu-muted">${x.meta}</div></div><div>${x.audio}<div class="tags">ENG · SPA</div><div class="tags">${x.tags}</div><div class="tu-muted" style="margin-top:10px">${x.kind} · ${x.ready ? "Local fixture ready" : "P2P stream fixture"}</div></div><div class="tu-row"><button class="play-source" data-action="source-pick" data-i="${i}" aria-label="${opts.mode ? "Queue" : "Play"} ${x.provider} ${x.quality}" ${x.disabled ? "disabled" : ""}>${opts.mode ? "↓" : '<svg viewBox="0 0 24 24" width="19" height="19" fill="none" stroke="currentColor" stroke-width="1.8"><path d="m8 5 11 7-11 7z"/></svg>'}</button><button class="small" data-action="source-copy" data-i="${i}" aria-label="Copy source fixture reference">⧉</button><button class="small" data-action="source-download" data-i="${i}" aria-label="Queue source fixture">↓</button></div></article>`;
        })
        .join("");
      $$(".tu-source-row", r).forEach((row) => {
        row.oncontextmenu = (e) => {
          e.preventDefault();
          const menu = dialog(
            "Source options",
            choice(
              opts.mode ? "Choose source" : "Play",
              "source-context-play",
            ) +
              choice("Copy link", "source-context-copy") +
              (!opts.mode ? choice("Download", "source-context-download") : ""),
          );
          [
            ["play", "source-pick"],
            ["copy", "source-copy"],
            ["download", "source-download"],
          ].forEach(([a, b]) => {
            const x = $("[data-action=source-context-" + a + "]", menu.panel);
            if (x)
              x.onclick = () => {
                menu.close();
                $("[data-action=" + b + "]", row).click();
              };
          });
        };
        row.onkeydown = (e) => {
          if (e.target !== row) return;
          if (e.key === "Enter") {
            $("[data-action=source-pick]", row).click();
            e.preventDefault();
          }
          if (e.key === "ArrowDown" || e.key === "ArrowUp") {
            const next =
              e.key === "ArrowDown"
                ? row.nextElementSibling
                : row.previousElementSibling;
            if (next) {
              next.focus();
              e.preventDefault();
            }
          }
        };
      });
      $$("[data-action=source-pick]", r).forEach(
        (b) =>
          (b.onclick = () => {
            const x = sources[+b.dataset.i];
            if (opts.mode) {
              M.set("theatre.queued." + (opts.title || "dune"), true);
              b.textContent = "✓";
              M.toast("Queued in this fixture only");
              return;
            }
            close();
            const p = {
              ...opts,
              title: opts.title || "Dune: Part Two",
              episode: opts.mediaType === "movie" ? null : opts.episode || 1,
              season: opts.mediaType === "movie" ? null : opts.season || 2,
              source: x.provider + " " + x.quality,
              state: x.ready ? "ready" : "buffering",
            };
            delete p.onPick;
            delete p.onClose;
            opts.onPick ? opts.onPick(p) : M.go("player.video", p);
          }),
      );
      $$("[data-action=source-copy]", r).forEach(
        (b) =>
          (b.onclick = () => {
            M.set("theatre.copiedReference", "fixture-source-" + b.dataset.i);
            b.textContent = "✓";
            b.setAttribute(
              "aria-label",
              "Fixture reference staged; clipboard unchanged",
            );
          }),
      );
      $$("[data-action=source-download]", r).forEach(
        (b) =>
          (b.onclick = () => {
            M.set("theatre.queued." + (opts.title || "dune"), true);
            $$("[data-action=source-download]", r).forEach(
              (q) => (q.textContent = "✓"),
            );
            M.toast("Queued in this fixture only");
          }),
      );
    }
    $("[data-action=provider-filter]", root).onclick = () => {
      const d = dialog(
        "Extensions",
        ["All", "Torrentio", "NoTorrent"]
          .map((x, i) =>
            choice(
              x,
              "ext-" + i,
              sources.filter((s) => x === "All" || s.provider === x).length +
                " sources",
              x === provider,
            ),
          )
          .join(""),
      );
      $$("[data-action^=ext-]", d.panel).forEach(
        (b, i) =>
          (b.onclick = () => {
            provider = ["All", "Torrentio", "NoTorrent"][i];
            quality = "all";
            $("[data-action=provider-filter]", root).innerHTML =
              esc(provider) + ' <span style="float:right">▾</span>';
            d.close();
            render();
          }),
      );
    };
    render();
    return { close, root };
  }
  function party(opts = {}) {
    let state = opts.state || M.get("theatre.party.state", "idle"),
      mode =
        opts.state === "shared"
          ? "shared"
          : M.get("theatre.party.control", "host"),
      host =
        state !== "participant" &&
        state !== "mismatch" &&
        state !== "desynced" &&
        state !== "host-grace",
      room = "DEMO-4821";
    const d = dialog("Watch Party", "", "popover party");
    function render() {
      const joined = [
        "host",
        "participant",
        "shared",
        "mismatch",
        "desynced",
        "reconnecting",
        "host-grace",
      ].includes(state);
      d.body.innerHTML = `<p class="tu-muted">Local room fixture · no communication or invitations are sent.</p>${["reconnecting", "host-grace"].includes(state) ? `<p class="tu-notice">${state === "reconnecting" ? "Reconnecting to room…" : "Host is reconnecting. Playback is paused."}</p>` : ""}${joined ? `<h3>ROOM ID</h3><div class="tu-row tu-spread"><strong>${room}</strong>${button("Copy", "party-copy")}</div>${state === "mismatch" ? `<p class="tu-notice">This player is not on the room's exact source yet. Open the room source before playback can sync.</p>${button("Open room source", "party-source", "tu-btn primary")}` : ""}${host ? `<h3>PLAYBACK CONTROL</h3><div class="tu-row">${button("Host Control", "host-control")}${button("Shared Control", "shared-control")}</div>` : `<p class="tu-muted" style="margin-top:15px">${mode === "host" ? "The host controls playback." : "Shared playback control."}</p>`}${state === "desynced" ? `<p class="tu-notice">You're out of sync with the room.</p>${button("Catch Up", "party-catchup", "tu-btn primary")}` : ""}${host ? `<h3>INVITE BY EXACT USERNAME</h3><div class="tu-row"><input aria-label="Username" id="party-name" placeholder="Username" style="width:230px">${button("Invite", "party-invite")}</div><div id="party-invite-status" class="tu-muted" role="status"></div>` : ""}<h3>PARTICIPANTS  2 / 12</h3><div class="tu-member"><span>${host ? "You" : "Alex"} <small class="tu-muted">Host</small></span><span>Ready</span></div><div class="tu-member"><span>${host ? "Alex" : "You"}</span><span>${state === "mismatch" ? "Source needed" : "Ready"}</span></div><h3>ROOM CHAT</h3><div class="tu-chat" id="party-chat"><p class="tu-muted">Messages live only for this room.</p></div><div class="tu-row" style="margin-top:8px"><input aria-label="Message room" id="party-message" placeholder="Message room" style="width:250px">${button("Send", "party-send")}</div><div class="tu-row" style="margin-top:8px">${["Like", "Laugh", "Wow"].map((v, i) => button(v, "party-react-" + i)).join("")}</div><hr class="tu-divider">${button(host ? "End room" : "Leave room", "party-leave")}` : `<h3>${state === "unavailable" ? "Watch Party is unavailable in this build." : state === "signedout" ? "Sign in to start a Watch Party" : state === "source-required" ? "Choose a supported source first" : "Ready to watch together"}</h3><p class="tu-muted">${state === "source-required" ? "A supported exact source is required for synchronized playback." : state === "unavailable" ? "Room joining will appear here when the service is configured." : "Start a room from the current source, or join with a room ID."}</p><div class="tu-stack" style="margin-top:16px"><button class="tu-btn primary" data-action="party-start" ${["source-required", "unavailable", "signedout"].includes(state) ? "disabled" : ""}>Start Watch Party</button>${button("Join Watch Party", "party-join")}</div>${state === "error" ? '<p class="tu-error">Room could not be started. Try again.</p>' : ""}`}`;
      const bind = (a, fn) => {
        const b = $("[data-action=" + a + "]", d.panel);
        if (b) b.onclick = fn;
      };
      bind("party-start", () => {
        state = "host";
        host = true;
        M.set("theatre.party.state", state);
        render();
      });
      bind("party-join", () => {
        d.close();
        joinParty();
      });
      bind("party-copy", () => M.toast("Fixture room ID: " + room));
      bind("party-source", () => {
        d.close();
        sourceSheet({
          title: opts.title,
          onPick: () => party({ state: "participant" }),
        });
      });
      bind("party-catchup", () => {
        state = "participant";
        render();
      });
      bind("host-control", () => {
        mode = "host";
        M.set("theatre.party.control", mode);
        render();
      });
      bind("shared-control", () => {
        mode = "shared";
        M.set("theatre.party.control", mode);
        render();
      });
      $$('[data-action$="-control"]', d.panel).forEach((b) =>
        b.setAttribute(
          "aria-pressed",
          String(b.dataset.action === mode + "-control"),
        ),
      );
      bind("party-invite", () => {
        const val = $("#party-name", d.panel).value.trim();
        $("#party-invite-status", d.panel).textContent = val
          ? "Invitation staged for " + val + "; not sent."
          : "Enter an exact username.";
      });
      bind("party-send", () => {
        const i = $("#party-message", d.panel);
        if (i.value.trim()) {
          const p = document.createElement("p");
          p.textContent = "You: " + i.value;
          $("#party-chat", d.panel).append(p);
          i.value = "";
        }
      });
      $$("[data-action^=party-react-]", d.panel).forEach(
        (b) =>
          (b.onclick = () => {
            const n = document.createElement("p");
            n.textContent = "You reacted: " + b.textContent + " (fixture)";
            $("#party-chat", d.panel).append(n);
          }),
      );
      bind("party-leave", () => {
        const c = dialog(
          host ? "End this room?" : "Leave this room?",
          `<p class="tu-muted">This clears the local room fixture.</p><div class="tu-row" style="margin-top:20px">${button("Cancel", "cancel")}${button(host ? "End room" : "Leave room", "confirm", "tu-btn primary")}</div>`,
        );
        $("[data-action=cancel]", c.panel).onclick = c.close;
        $("[data-action=confirm]", c.panel).onclick = () => {
          M.set("theatre.party.state", "idle");
          c.close();
          d.close();
        };
      });
    }
    render();
    return d;
  }
  function joinParty(opts = {}) {
    const d = dialog(
      "Join Watch Party",
      `<p class="tu-muted">Join a private room with an exact source. Local fixture; nothing is sent.</p><div class="tu-stack" style="margin-top:24px"><label class="tu-label">ROOM ID<input id="join-id" autocomplete="off" placeholder="DEMO-4821" maxlength="80" aria-describedby="join-error"></label><label class="tu-label">DISPLAY NAME FOR THIS ROOM<input id="join-name" value="You" autocomplete="off" maxlength="80" aria-describedby="join-error"></label><div class="tu-tabs" style="margin-top:6px"><span class="tu-chip">Private</span><span class="tu-chip">2–12 people</span><span class="tu-chip">Synced playback</span><span class="tu-chip">Fetches the room's source</span></div><div id="join-error" role="status" class="tu-error"></div><div class="tu-row">${button("Cancel", "cancel")}${button("Join Room", "join", "tu-btn primary")}</div></div>`,
      "join-sheet",
    );
    $("[data-action=cancel]", d.panel).onclick = d.close;
    function joined(id) {
      $("header h2", d.panel).textContent = "Watch Party joined";
      d.body.innerHTML = `<div class="tu-notice"><h3>Room ${esc(id)}</h3><p>The room requires its exact source. Open that source in Player 1 to become playback-ready.</p></div><p class="tu-muted" style="margin:16px 0">Local fixture room only. No remote room was joined.</p><div class="tu-row">${button("Close", "join-close")}${button("Leave Room", "join-leave")}</div>`;
      $("[data-action=join-close]", d.panel).onclick = d.close;
      $("[data-action=join-leave]", d.panel).onclick = () => {
        M.set("theatre.party.state", "idle");
        d.close();
      };
    }
    function submit() {
      const id = $("#join-id", d.panel).value.trim(),
        name = $("#join-name", d.panel).value.trim();
      if (!id || !name) {
        $("#join-error", d.panel).textContent =
          "Enter a room ID and display name.";
        $("#join-id", d.panel).setAttribute("aria-invalid", String(!id));
        $("#join-name", d.panel).setAttribute("aria-invalid", String(!name));
        (!id ? $("#join-id", d.panel) : $("#join-name", d.panel)).focus();
        return;
      }
      if (id.toLowerCase() === "error") {
        $("#join-error", d.panel).textContent =
          "Room not found. Check the room ID and try again.";
        return;
      }
      M.set("theatre.party.state", "participant");
      joined(id);
    }
    $("[data-action=join]", d.panel).onclick = submit;
    $$("input", d.panel).forEach(
      (n) =>
        (n.onkeydown = (e) => {
          if (e.key === "Enter") {
            e.preventDefault();
            submit();
          }
        }),
    );
    if (opts.state === "joined") joined("DEMO-4821");
    if (opts.state === "unavailable") {
      $("[data-action=join]", d.panel).disabled = true;
      $("#join-error", d.panel).textContent =
        "Watch Party is unavailable in this build. Room joining will appear here when the service is configured.";
    }
    return d;
  }
  window.TheatreUI = {
    resolveMedia,
    M,
    $,
    $$,
    esc,
    button,
    choice,
    dialog,
    activate,
    sourceSheet,
    party,
    joinParty,
  };
})();
