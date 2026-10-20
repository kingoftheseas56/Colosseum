/* Source: Colosseum 2916fe85812643c2e0ae45b0aec132cb6eb4e507. Every service effect is an in-memory fixture. */
const P = MockUI.params,
  M = document.getElementById("main");
let page = P.page || "connections",
  provider = P.provider || "trakt",
  section = P.section || "history",
  world = "all",
  month = "October 2026",
  updateState = P.state || "available",
  chapter = 0,
  selectedPick = 0,
  sort = "relevance";
let connected = MockUI.get("connections.linked", {
    trakt: true,
    simkl: false,
    mal: false,
    anilist: false,
  }),
  choices = {},
  bulk = [],
  stremio = MockUI.get("connections.stremio", P.state || "synced");
const providers = {
  trakt: ["Trakt", "Movies and series", "trakt"],
  simkl: ["SIMKL", "Movies, series, and anime", "simkl"],
  mal: ["MyAnimeList", "Anime and manga", "myanimelist"],
  anilist: ["AniList", "Anime and manga", "anilist"],
};
const button = U.btn,
  logo = (k) =>
    `<img class="provider-logo" src="../assets/icons/trackers/${providers[k][2]}.svg" alt="${providers[k][0]}">`;
function shell(title, copy, body, actions = "") {
  M.innerHTML = `<div class="content"><header class="head"><div><h1>${title}</h1><p>${copy}</p></div><div class="row">${actions}</div></header>${body}<div id="status" class="status" role="status"></div></div>`;
  MockUI.ready();
}
function connections() {
  shell(
    "Connections",
    "Your external library connections",
    `<section class="native panel row spread"><div class="row"><img class="provider-logo" src="../assets/icons/colosseum.svg" alt="Colosseum"><div><div class="kicker">Native library</div><h2>Colosseum</h2><p>Works without trackers.<br>Your native progress, History, and activity stay here.</p></div></div><div><span class="badge">✓ Always available</span><p style="margin-top:18px">${Object.values(connected).filter(Boolean).length} connected · ${MockUI.get("connections.imported", false) ? "0" : "2"} waiting / review</p></div></section><div class="section"><h2>Your trackers</h2><p>This profile</p><div class="grid" style="margin-top:22px">${
      Object.keys(providers)
        .filter((k) => connected[k])
        .map((k) => card(k, true))
        .join("") || "<p>No connected trackers.</p>"
    }</div></div><div class="section"><h2>External services</h2><p>Manage connections to external libraries.</p><div class="panel row spread" style="margin-top:18px"><div class="row"><img class="provider-logo" src="../assets/icons/stremio-official.svg" alt="Stremio"><div><h3>Stremio</h3><p>Library, progress, and History</p></div></div><span class="badge">${stremio === "synced" ? "Connected" : "Not connected"}</span><button data-go="connections.stremio">Open settings ›</button></div></div><div class="section"><h2>Available trackers</h2><p>Capabilities are verified per provider</p><div class="grid" style="margin-top:22px">${Object.keys(
      providers,
    )
      .filter((k) => !connected[k])
      .map((k) => card(k, false))
      .join("")}</div></div>`,
    button("Preferences", "preferences") + button("Sync all", "sync-all"),
  );
}
function card(k, on) {
  return `<button class="panel tracker-card" data-action="dossier-${k}"><div class="row">${logo(k)}<div><h3>${providers[k][0]}</h3><p>${on ? "Demo profile" : providers[k][1]}</p></div></div><div class="row spread" style="margin-top:24px"><small>${on ? "LAST SYNC · just now" : "Available to connect"}</small><span>${on ? "Open" : "Connect"} ›</span></div></button>`;
}
function dossier(k) {
  provider = k;
  U.modal(
    providers[k][0],
    `<div class="row">${logo(k)}<div><span class="badge">${connected[k] ? "THIS PROFILE" : "CAPABILITY GATE"}</span><p>${providers[k][1]}</p></div></div><section class="section"><h3>Account and profile</h3><p>${connected[k] ? "Demo profile · Only this Colosseum profile can use this connection" : "Not connected to this profile"}</p><div class="actions">${connected[k] ? button("Disconnect", "disconnect") + button("Remove imported data", "remove-imported") : button("Connect", "connect", "primary")}</div></section><section class="section"><h3>Capabilities</h3><p>Progress import · Progress export · Library matching</p><p class="status">Fixture capabilities only; no provider request is made.</p></section>${connected[k] ? `<section class="section"><h3>Import review</h3><p>Initial import · 3 entries · 1 protected · 1 unmatched</p><div class="actions">${button("Review import", "import")}</div></section><section class="section"><h3>Delivery status</h3><p>Demo update · Waiting for review</p><p>Native History is unchanged.</p></section><section class="section"><h3>Automatic sync</h3>${U.toggle("Pull changes automatically", "pull", true)}${U.toggle("Send Colosseum progress", "push", false)}${button("Review Colosseum updates before sending", "export")}${U.toggle("Live playback tracking", "live", false)}</section>` : ""}`,
    button("Close", "close"),
    true,
  );
}
const items = [
  ["Dune: Part Two", "Progress update · 42 minutes", "ordinary"],
  ["One Piece", "Native History protects this title", "protected"],
  ["Unknown tracker title", "No exact target found", "unmatched"],
];
function imports() {
  U.modal(
    "Initial import · " + providers[provider][0],
    `<p>Review each item before importing. Colosseum’s native History and witnessed activity stay protected; tracker progress never creates a watched event by itself.</p><div class="actions">${button("Review shared choices", "bulk")}${button("Clear selection", "clear-selection")}</div>${items.map((x, i) => `<section class="section"><div class="row spread"><h3>${x[0]}</h3><label><input type="checkbox" data-bulk="${i}" ${bulk.includes(i) ? "checked" : ""}> Select</label></div><p>${x[1]}</p><div class="actions">${(x[2] === "protected" ? ["Keep Colosseum progress"] : x[2] === "unmatched" ? ["Find match", "Leave unmatched"] : ["Use tracker progress", "Keep Colosseum progress", "Leave for later"]).map((c, j) => button(c, c === "Find match" ? "match" : "choice-" + i + "-" + j, choices[i] === c ? "selected" : "")).join("")}</div><p class="status">${choices[i] || "Decision needed"}</p></section>`).join("")}`,
    button("Confirm import", "confirm-import", "primary"),
    true,
    () => dossier(provider),
  );
}
function preferences() {
  U.modal(
    "All trackers · Preferences",
    U.toggle("Tracker sync", "sync", true) +
      U.toggle("Check connected trackers on launch", "launch", true) +
      U.toggle("Deliver in the background", "background", true) +
      U.toggle("Show completion messages", "messages", true),
    button("Done", "close", "primary"),
  );
}
function stremioPanel() {
  let states = {
    notConnected: [
      "Connect Stremio",
      "Bring your Stremio library, progress, History and addon collection into this profile.",
      "Connect Stremio",
    ],
    connecting: [
      "Connecting to Stremio",
      "Finish signing in through the browser window.",
      "Finish demo connection",
    ],
    syncing: [
      "Syncing with Stremio",
      "Combining your library, progress and addons safely.",
      "Finish demo sync",
    ],
    reconnectRequired: [
      "Reconnect Stremio",
      "This profile uses Stremio, but this device needs its own secure connection.",
      "Reconnect Stremio",
    ],
    synced: [
      "Stremio Sync",
      "Your newest activity wins. First merge never deletes either library.",
      "Sync now",
    ],
    syncFailed: [
      "Stremio sync needs attention",
      "Nothing was deleted. Try again when the connection is available.",
      "Sync now",
    ],
    paused: [
      "Stremio sync needs attention",
      "Nothing was deleted. Try again when the connection is available.",
      "Sync now",
    ],
    unavailable: [
      "Stremio sync needs attention",
      "Stremio Sync is unavailable while this profile is sealed.",
      "Sync now",
    ],
  };
  let s = states[stremio] || states.notConnected;
  M.innerHTML = `<div class="stremio-stage"><section class="panel stremio-panel"><div class="row"><img class="provider-logo" src="../assets/icons/stremio-official.svg" alt="Stremio"><div><h1>${s[0]}</h1><p>${s[1]}</p></div></div><section class="section"><p>Account &nbsp; ${stremio === "notConnected" ? "Not connected" : "Demo Stremio account"}</p><p>Last sync &nbsp; ${stremio === "synced" ? "Just now" : "Never"}</p></section><p id="status" class="status">Local simulation. No sign-in window or service will open.</p><div class="actions"><button data-action="stremio-primary" class="primary" ${["paused", "unavailable"].includes(stremio) ? "disabled" : ""}>${s[2]}</button>${stremio !== "notConnected" ? button("Switch account", "stremio-switch") + button("Disconnect", "stremio-disconnect") : ""}${button("Close", "back")}</div></section></div>`;
  MockUI.ready();
}
const history = [
  ["Dune: Part Two", "Theatre", "Watched · 2h 46m", "Today"],
  ["One Piece", "Tankoban", "Chapter 1112 · 18 pages", "Yesterday"],
  ["Dune", "Biblio", "Read · 45 minutes", "October 5"],
];
function activity() {
  shell(
    "Your Colosseum",
    "The moments and measures Colosseum witnessed on this profile.",
    `<div class="tabs">${["history", "highlights", "stats"].map((s) => button(s[0].toUpperCase() + s.slice(1), "activity-" + s, section === s ? "selected" : "")).join("")}</div><header class="head"><div><h2>${section[0].toUpperCase() + section.slice(1)}</h2><p>${section === "history" ? "Everything watched, read, or listened to in Colosseum appears here." : section === "highlights" ? "A portrait of the month, drawn only from activity Colosseum recorded." : "Lifetime activity recorded on this profile."}</p></div>${section === "highlights" ? `<select id="month" aria-label="Month"><option>October 2026</option><option>September 2026</option></select>` : `<select id="world-filter" aria-label="Medium">${["all", "Tankoban", "Biblio", "Theatre"].map((w) => `<option ${w === world ? "selected" : ""}>${w}</option>`).join("")}</select>`}</header>${
      P.empty ||
      MockUI.get("account.activityCleared", false) ||
      month === "September 2026"
        ? '<div class="empty"><h2>No activity yet</h2><p>There is no Colosseum activity for this selection.</p></div>'
        : section === "history"
          ? `<div class="kicker">Recent</div>${history
              .filter((x) => world === "all" || x[1] === world)
              .map(
                (x, i) =>
                  `<button class="history-row" data-action="history-${i}"><span class="badge">${x[1].toUpperCase()}</span><div><h3>${x[0]}</h3><p>${x[2]}</p></div><small>${x[3]}</small></button>`,
              )
              .join("")}`
          : section === "highlights"
            ? `<div class="portrait panel"><div class="kicker">Monthly portrait</div><h1 style="font-size:64px;margin:16px 0">October <small>2026</small></h1><div class="grid">${[
                ["3", "Titles"],
                ["4h 12m", "Time spent"],
                ["18", "Pages read"],
              ]
                .map(
                  (x) =>
                    `<div><div class="stat">${x[0]}</div><p>${x[1]}</p></div>`,
                )
                .join(
                  "",
                )}</div></div><h3 style="margin:25px 0">October 2026 highlights · 3 titles</h3>${history.map((x) => `<div class="history-row"><h3>${x[0]}</h3><p>${x[2]}</p></div>`).join("")}`
            : `<div class="grid">${[
                ["3", "Titles"],
                ["4h 12m", "Lifetime time"],
                ["18", "Pages"],
              ]
                .map(
                  (x) =>
                    `<div class="panel"><div class="stat">${x[0]}</div><p>${x[1]}</p></div>`,
                )
                .join(
                  "",
                )}</div><div class="tabs">${["Overview", "Time", "Completion"].map((s) => button(s, "stats-" + s)).join("")}</div><h3 id="stats-title">Overview · ${world.toUpperCase()} · LIFETIME</h3>${history
                .filter((x) => world === "all" || x[1] === world)
                .map(
                  (x, i) =>
                    `<div class="history-row"><small>${i + 1}</small><h3>${x[0]}</h3><p>${x[2]}</p></div>`,
                )
                .join("")}`
    }`,
  );
  let ms = document.getElementById("month");
  if (ms) ms.value = month;
}
const picks = [
  ["Cold ripple", "Colosseum Native", "wallpaper/cold-ripple.jpg"],
  ["Dune", "KDE Plasma fixture", "wallpaper/sagas/dune.jpg"],
  ["Cold ripple · animated", "Colosseum Animated", "wallpaper/cold-ripple.jpg"],
];
function wallpaper() {
  shell(
    "Wallpapers",
    "Choose a background for " + (P.world || "Home"),
    `<div class="row"><input id="wall-search" aria-label="Search wallpapers" placeholder="Search wallpapers" style="flex:1">${button("Search", "wall-search")}${["relevance", "top", "random"].map((x) => button(x[0].toUpperCase() + x.slice(1), "sort-" + x, sort === x ? "selected" : "")).join("")}</div><div class="wallpaper-grid">${["Colosseum Animated", "Colosseum Native", "KDE Plasma"].map((g, i) => `<section><h3>${g}</h3><button class="wallpaper-card" data-action="pick-${[2, 0, 1][i]}"><img src="../assets/${picks[[2, 0, 1][i]][2]}" alt="${picks[[2, 0, 1][i]][0]}"><p>${picks[[2, 0, 1][i]][0]}</p><small>${i === 0 ? "Static preview of native shader" : i === 2 ? "Bundled source artwork" : "Built in"}</small></button></section>`).join("")}</div><div class="actions">${button("Load more", "wall-more")}${button("Close", "back")}</div>`,
  );
}
function wallpaperPreview() {
  let p = picks[selectedPick];
  U.modal(
    p[0],
    `<img src="../assets/${p[2]}" alt="${p[0]} wallpaper preview" style="width:100%;height:310px;object-fit:cover;border-radius:12px"><p>${p[1]} · bundled fixture</p><p>Apply this staged wallpaper to:</p>`,
    ["Home", "Tankoban", "Biblio", "Theatre", "All worlds"]
      .map((w) => button(w, "apply-" + w))
      .join(""),
    true,
  );
}
function settings() {
  shell(
    "Preferences",
    "Personalize your Colosseum.",
    `<section class="panel"><h2>Title pages</h2>${U.toggle("Use rebuilt title pages", "rebuiltTitlePages", MockUI.get("rebuiltTitlePages", false))}<p>Changes the Manga and Theatre detail layout in this mock.</p></section><section class="section"><h2>Content preferences</h2><p>Explicit content is enabled in this source build. ContentPreferences.qml exposes a read-only value.</p><div class="actions"><button disabled>Explicit content · On</button></div></section><section class="section"><h2>World row preferences</h2><p>Theatre catalogue rows have independent order, visibility and names for Movies, Shows and Anime.</p><div class="tabs">${["Movies", "Shows", "Anime"].map((s) => button(s, "rows-" + s)).join("")}</div></section><section class="section"><h2>Appearance and services</h2><div class="actions"><button data-go="wallpaper">Wallpapers</button><button data-go="connections.center">Connections</button><button data-go="account.center">Account Centre</button><button data-go="update">Updates</button></div></section>`,
  );
}
let rowTab = "Movies",
  rows = MockUI.get("settings.rows", [
    { name: "Popular", hidden: false },
    { name: "Top rated", hidden: false },
    { name: "New releases", hidden: false },
  ]);
function rowPrefs() {
  U.modal(
    rowTab + " rows",
    rows
      .map(
        (r, i) =>
          `<div class="section row spread"><div><input data-row-name="${i}" aria-label="Row ${i + 1} name" value="${U.e(r.name)}"><p>${r.hidden ? "Hidden" : "Visible"}</p></div><div class="row">${button("↑", "row-up-" + i)}${button("↓", "row-down-" + i)}${button(r.hidden ? "Show" : "Hide", "row-hide-" + i)}</div></div>`,
      )
      .join(""),
    button("Reset this tab", "row-reset") +
      button("Save row names", "row-save", "primary"),
    true,
  );
}
const updateCopy = {
  idle: ["No update check yet", "Check again"],
  checking: ["Checking for updates", ""],
  current: ["Everything is up to date", "Check again"],
  available: ["Colosseum DEMO is ready", "Download update"],
  downloading: ["Updating to DEMO", "Pause download"],
  paused: ["Update paused", "Resume download"],
  verifying: ["Verifying the update", ""],
  ready: ["Ready to enter DEMO", "Restart and update"],
  installing: ["Colosseum is updating", ""],
  failed: ["The update could not finish", "Retry download"],
  unverified: ["This update could not be verified", "Check again"],
  manual: ["Manual update required", "Check again"],
};
function update() {
  let c = updateCopy[updateState] || updateCopy.available;
  const chapters = [
    [
      "RELEASE",
      "The latest chapter",
      "A staged release gallery. Version and release content are fixture data, not a release announcement.",
    ],
    [
      "FEATURE",
      "Your Colosseum, together",
      "Account, Collections and progress in one place.",
    ],
    [
      "STATISTIC",
      "Five worlds. One home.",
      "Tankoban · Biblio · Theatre · Universes · Feria",
    ],
  ];
  M.innerHTML = `<div class="update-gallery"><div class="update-chrome"><div><div class="kicker">Colosseum update</div><h1>DEMO release</h1></div>${button("‹ Back", "back", "link")}</div><div class="update-copy"><div class="kicker">${chapters[chapter][0]}</div><h1>${chapters[chapter][1]}</h1><p>${chapters[chapter][2]}</p></div><div class="update-chapters row"><span>${String(chapter + 1).padStart(2, "0")} / 03</span>${chapters.map((_, i) => button(String(i + 1).padStart(2, "0"), "chapter-" + i, chapter === i ? "selected" : "")).join("")}${button("Next", "chapter-next")}</div><div class="update-status row spread"><div><h3>${c[0]}</h3><p>Target DEMO · no update is downloaded or installed</p>${["downloading", "paused"].includes(updateState) ? '<progress value="42" max="100"></progress><small>42 of 100 MB · 42%</small>' : ""}<div id="status" class="status"></div></div>${c[1] ? button(c[1], "update-primary", "primary") : button("Advance demo state", "update-step")}</div></div>`;
  MockUI.ready();
}
window.handleAction = (a, b) => {
  if (a.startsWith("dossier-")) dossier(a.slice(8));
  if (a === "preferences") preferences();
  if (a === "sync-all") {
    U.status(
      "Fixture sync checked. 2 entries need import review; native History is unchanged.",
    );
  }
  if (a === "import") imports();
  if (a.startsWith("choice-")) {
    let [, i, j] = a.split("-");
    let options =
      items[i][2] === "protected"
        ? ["Keep Colosseum progress"]
        : items[i][2] === "unmatched"
          ? ["Find match", "Leave unmatched"]
          : [
              "Use tracker progress",
              "Keep Colosseum progress",
              "Leave for later",
            ];
    choices[i] = options[j];
    imports();
  }
  if (a === "clear-selection") {
    bulk = [];
    imports();
  }
  if (a === "bulk") {
    if (!bulk.length) {
      document.getElementById("modal-status").textContent =
        "Select one or more items first.";
      return;
    }
    U.modal(
      "Review choices for " + bulk.length,
      `<p>${bulk.map((i) => items[i][0]).join(" · ")}</p><p>Only selected items receive this decision. Protected and unmatched exceptions stay visible. No Progress or History is applied until separate import confirmation.</p>`,
      button("Keep Colosseum progress", "bulk-keep", "primary"),
    );
  }
  if (a === "bulk-keep") {
    bulk
      .filter((i) => i !== 2)
      .forEach((i) => (choices[i] = "Keep Colosseum progress"));
    imports();
  }
  if (a === "match")
    U.modal(
      "Find a Colosseum title",
      `<p>Tracker title: “Unknown tracker title”</p><p>Choose a named title already in Colosseum Progress. A match alone cannot apply tracker Progress or change History.</p><div class="actions"><input id="match-search" placeholder="Search Colosseum Progress" aria-label="Search Colosseum Progress">${button("Search", "match-search")}</div><div id="match-results" class="section">${button("Dune: Part Two · Film", "match-pick")}</div>`,
      button("Back to import review", "import"),
      true,
      imports,
    );
  if (a === "match-search") {
    let q = document.getElementById("match-search").value;
    document.getElementById("match-results").innerHTML =
      /dune/i.test(q) || !q
        ? button("Dune: Part Two · Film", "match-pick")
        : "<p>No named Colosseum titles match.</p>";
  }
  if (a === "match-pick") {
    choices[2] = "Matched Dune: Part Two · review progress separately";
    imports();
  }
  if (a === "confirm-import") {
    if (Object.keys(choices).length < 3) {
      document.getElementById("modal-status").textContent =
        "Choose a decision for each item before confirming.";
      return;
    }
    MockUI.set("connections.imported", true);
    U.close();
    connections();
    U.status(
      "Demo decisions applied. Native History and witnessed activity are unchanged.",
    );
  }
  if (a === "export")
    U.modal(
      "Send to " + providers[provider][0],
      '<p>Choose the Colosseum updates you want to send. This review is separate from importing tracker data.</p><section class="section"><label><input type="checkbox" data-export checked> Dune: Part Two · Progress 42 min</label></section><section class="section"><label><input type="checkbox" data-export> One Piece · Chapter 1112</label></section>',
      button("Send selected demo updates", "confirm-export", "primary"),
    );
  if (a === "confirm-export") {
    let n = document.querySelectorAll("[data-export]:checked").length;
    if (!n) {
      document.getElementById("modal-status").textContent =
        "Select an update first.";
      return;
    }
    U.close();
    U.status(n + " fixture updates marked sent. No external request was made.");
  }
  if (a === "disconnect" || a === "remove-imported")
    U.modal(
      a === "disconnect"
        ? "Disconnect " + providers[provider][0]
        : "Remove imported data from " + providers[provider][0],
      a === "disconnect"
        ? "<p>Disconnecting stops sync for this profile. Known-unsent updates are removed; uncertain outcomes remain paused for recovery. Native History is preserved.</p><p>The real application requests best-effort access revocation. This fixture makes no request.</p>"
        : "<p>Only this account’s tracker-imported History evidence and eligible tracker-imported Progress are removed. Native History, Activity, statistics, local Progress and other tracker data are preserved.</p><h3>Affected entries</h3><p>Dune: Part Two · tracker-imported progress</p>",
      button("Confirm", "confirm-" + a, "danger"),
    );
  if (a === "confirm-disconnect") {
    connected[provider] = false;
    MockUI.set("connections.linked", connected);
    U.close();
    connections();
    U.status("Demo tracker disconnected. Native data preserved.");
  }
  if (a === "confirm-remove-imported") {
    U.close();
    U.status("Demo imported data removed. No remote data was changed.");
  }
  if (a === "connect")
    U.modal(
      "Connect " + providers[provider][0],
      '<p>Demo approval flow. No browser login is opened.</p><div class="kicker" style="margin:20px 0">Approval code</div><div class="stat">DEMO-0000</div><p>The browser page is prefilled in the native app. The code remains here as a fallback.</p>',
      button("Move connection to this profile", "approve-connect") +
        button("Finish demo approval", "approve-connect", "primary"),
    );
  if (a === "approve-connect") {
    connected[provider] = true;
    MockUI.set("connections.linked", connected);
    U.close();
    connections();
    dossier(provider);
  }
  if (a === "stremio-primary") {
    stremio = ["notConnected", "reconnectRequired"].includes(stremio)
      ? "connecting"
      : stremio === "connecting" || stremio === "syncing"
        ? "synced"
        : "syncing";
    MockUI.set("connections.stremio", stremio);
    stremioPanel();
  }
  if (a === "stremio-switch" || a === "stremio-disconnect")
    U.modal(
      a.endsWith("switch") ? "Switch account" : "Disconnect Stremio",
      `<p>${a.endsWith("switch") ? "Switching accounts keeps everything already merged, then connects a replacement Stremio account." : "Disconnecting stops sync and removes this device’s credential. Everything already merged stays in Colosseum."}</p>`,
      button(
        a.endsWith("switch") ? "Confirm switch" : "Confirm disconnect",
        "confirm-" + a,
        "primary",
      ),
    );
  if (a.startsWith("confirm-stremio-")) {
    stremio = a.endsWith("switch") ? "connecting" : "notConnected";
    MockUI.set("connections.stremio", stremio);
    U.close();
    stremioPanel();
  }
  if (a.startsWith("activity-")) {
    section = a.slice(9);
    activity();
  }
  if (a.startsWith("stats-"))
    document.getElementById("stats-title").textContent =
      a.slice(6) + " · " + world.toUpperCase() + " · LIFETIME";
  if (a.startsWith("history-"))
    U.modal(
      history[+a.slice(8)][0],
      "<p>Recorded in this Colosseum profile. Fixture activity detail.</p><p>Native activity stays independent of tracker imports.</p>",
    );
  if (a.startsWith("sort-")) {
    sort = a.slice(5);
    wallpaper();
    U.status(
      "Bundled results sorted by " + sort + ". Network search is simulated.",
    );
  }
  if (a === "wall-search") {
    let q = document.getElementById("wall-search").value;
    document
      .querySelectorAll(".wallpaper-card")
      .forEach(
        (c) =>
          (c.hidden =
            q && !c.textContent.toLowerCase().includes(q.toLowerCase())),
      );
    U.status("Showing bundled fixture matches only.");
  }
  if (a === "wall-more")
    U.status(
      "All bundled fixture wallpapers are shown. Native catalogue pagination is simulated.",
    );
  if (a.startsWith("pick-")) {
    selectedPick = +a.slice(5);
    wallpaperPreview();
  }
  if (a.startsWith("apply-")) {
    MockUI.set("wallpaper." + a.slice(6), picks[selectedPick][2]);
    U.close();
    document.querySelector(".wall").style.backgroundImage =
      `linear-gradient(#05070bab,#05070be0),url('../assets/${picks[selectedPick][2]}')`;
    U.status("Staged wallpaper selected for " + a.slice(6) + ".");
  }
  if (a.startsWith("rows-")) {
    rowTab = a.slice(5);
    rows = MockUI.get("settings.rows." + rowTab, [
      { name: "Popular", hidden: false },
      { name: "Top rated", hidden: false },
      { name: "New releases", hidden: false },
    ]);
    rowPrefs();
  }
  if (a.startsWith("row-up-") || a.startsWith("row-down-")) {
    let i = +a.split("-").at(-1),
      j = i + (a.includes("up") ? -1 : 1);
    if (j >= 0 && j < rows.length) [rows[i], rows[j]] = [rows[j], rows[i]];
    MockUI.set("settings.rows." + rowTab, rows);
    rowPrefs();
  }
  if (a.startsWith("row-hide-")) {
    rows[+a.slice(9)].hidden = !rows[+a.slice(9)].hidden;
    MockUI.set("settings.rows." + rowTab, rows);
    rowPrefs();
  }
  if (a === "row-save") {
    document
      .querySelectorAll("[data-row-name]")
      .forEach(
        (x) =>
          (rows[+x.dataset.rowName].name =
            x.value.trim() ||
            ["Popular", "Top rated", "New releases"][+x.dataset.rowName]),
      );
    MockUI.set("settings.rows." + rowTab, rows);
    U.close();
    U.status("Row preferences saved for " + rowTab + " in this session.");
  }
  if (a === "row-reset") {
    rows = [
      { name: "Popular", hidden: false },
      { name: "Top rated", hidden: false },
      { name: "New releases", hidden: false },
    ];
    MockUI.set("settings.rows." + rowTab, rows);
    rowPrefs();
  }
  if (a.startsWith("chapter-")) {
    chapter = a === "chapter-next" ? (chapter + 1) % 3 : +a.slice(8);
    update();
  }
  if (a === "update-primary") {
    updateState =
      {
        available: "downloading",
        downloading: "paused",
        paused: "downloading",
        ready: "installing",
        failed: "downloading",
      }[updateState] || "checking";
    update();
  }
  if (a === "update-step") {
    updateState =
      { checking: "available", verifying: "ready", installing: "current" }[
        updateState
      ] || "ready";
    update();
  }
};
document.addEventListener("change", (e) => {
  let t = e.target;
  if (t.dataset.toggle) {
    MockUI.set(
      t.dataset.toggle === "rebuiltTitlePages"
        ? "rebuiltTitlePages"
        : "connections." + t.dataset.toggle,
      t.checked,
    );
    U.status("Preference updated in this staged session.");
  }
  if (t.dataset.bulk !== undefined) {
    let i = +t.dataset.bulk;
    bulk = t.checked ? [...new Set([...bulk, i])] : bulk.filter((x) => x !== i);
  }
  if (t.id === "world-filter") {
    world = t.value;
    activity();
  }
  if (t.id === "month") {
    month = t.value;
    activity();
  }
});
(
  ({
    connections,
    stremio: stremioPanel,
    activity,
    wallpaper,
    settings,
    update,
  })[page] || connections
)();
if (P.overlay) {
  setTimeout(() => {
    if (P.overlay === "dossier") dossier(provider);
    else if (P.overlay === "import") imports();
    else if (P.overlay === "preferences") preferences();
    else if (P.overlay === "preview") wallpaperPreview();
    else if (P.overlay === "rows") rowPrefs();
    else window.handleAction(P.overlay);
  }, 0);
}
