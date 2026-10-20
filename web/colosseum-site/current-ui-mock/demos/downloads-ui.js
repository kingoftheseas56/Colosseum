(function () {
  "use strict";
  const U = UV,
    A = document.querySelector("#app"),
    P = MockUI.params;
  let world = P.world || "all",
    ledger = P.ledger || "",
    notice = "";
  let jobs = MockUI.get("downloads.jobs", [
    {
      id: "op",
      title: "One Piece",
      world: "tankoban",
      subtitle: "Chapters 1–3",
      state: "downloading",
      pct: 42,
    },
    {
      id: "theatre",
      title: "One Piece · Season 1",
      world: "theatre",
      subtitle: "3 episodes · 1 complete",
      state: "downloading",
      pct: 64,
    },
    {
      id: "audio",
      title: "Dune · Audiobook",
      world: "biblio",
      subtitle: "Audiobook · 21 files",
      state: "failed",
      pct: 23,
    },
  ]);
  let done = MockUI.get("downloads.done", [
    {
      id: "manga",
      title: "One Piece",
      world: "tankoban",
      kind: "manga",
      subtitle: "3 chapters · 42 MB",
    },
    {
      id: "book",
      title: "Dune",
      world: "biblio",
      kind: "book",
      subtitle: "1 book · 4.8 MB",
    },
    {
      id: "film",
      title: "Dune: Part Two",
      world: "theatre",
      kind: "video",
      subtitle: "1 film · 8.4 GB",
    },
    {
      id: "show",
      title: "One Piece",
      world: "theatre",
      kind: "show",
      subtitle: "Season 1 · 3 episodes",
    },
    {
      id: "audiobook",
      title: "Dune · Audiobook",
      world: "biblio",
      kind: "audio",
      subtitle: "21 files · 624 MB",
    },
  ]);
  let remote = MockUI.get("downloads.remote", [
    {
      id: "remote",
      title: "Watchmen",
      world: "tankoban",
      kind: "comic",
      subtitle: "Available on Desktop · not downloaded here",
    },
  ]);
  if (P.state === "empty") {
    jobs = [];
    done = [];
    remote = [];
  }
  const save = () => {
    MockUI.set("downloads.jobs", jobs);
    MockUI.set("downloads.done", done);
    MockUI.set("downloads.remote", remote);
  };
  function confirmation(title, body, label, fn) {
    U.dialog(
      title,
      `<p>${body}</p><div class="bar">${U.btn("Cancel", "cancel")}${U.btn(label, "confirm", 'class="primary"')}</div>`,
      (el, c) =>
        U.bind(el, {
          cancel: c,
          confirm: () => {
            c();
            fn();
            save();
            render();
          },
        }),
      true,
    );
  }
  function open(x) {
    MockUI.openSession({
      kind: x.kind,
      id: "download-" + x.id,
      title: x.title,
    });
    MockUI.go(
      x.kind === "audio"
        ? "reader.audio"
        : x.kind === "book"
          ? "reader.book"
          : x.kind === "manga" || x.kind === "comic"
            ? "reader.comic"
            : "player.video",
      { title: x.title, source: "downloads", sample: true },
    );
  }
  function render() {
    A.className = "screen";
    const filtered = jobs.filter((x) => world === "all" || x.world === world);
    A.innerHTML = `<div class="eyebrow">COLOSSEUM · LOCAL</div><h1 style="font:56px var(--display);margin:8px 0 20px">Downloads</h1><div class="bar">${["all", "tankoban", "biblio", "theatre"].map((x) => U.btn(x === "all" ? "All worlds" : x[0].toUpperCase() + x.slice(1), "world", `data-world="${x}" class="${world === x ? "on" : ""}"`)).join("")}${U.btn("Vault ↗", "vault")}</div>${notice ? `<p class="notice" role="status">${U.esc(notice)}</p>` : ""}<h2 class="section-title">Now arriving <span class="small">${filtered.length} groups</span></h2><section class="panel">${
      filtered
        .map(
          (j) =>
            `<details ${P.group === j.id || j.canceled?.length ? "open" : ""}><summary><strong>${j.title}</strong> <span class="small">${j.subtitle} · ${j.state === "failed" ? "Needs attention" : j.state === "paused" ? "Paused" : j.pct + "%"}</span></summary><div class="progress" style="--pct:${j.pct}%"><i></i></div><div class="bar">${j.state === "failed" ? U.btn("Retry", "retry", `data-id="${j.id}"`) : U.btn(j.state === "paused" ? "Resume" : "Pause", "pause", `data-id="${j.id}"`)}${j.world === "theatre" ? U.btn("Play", "play-arriving", `data-id="${j.id}"`) : ""}${U.btn("Cancel", "cancel-job", `data-id="${j.id}"`)}</div>${[
              1, 2, 3,
            ]
              .filter((n) => !(j.canceled || []).includes(n))
              .map(
                (n) =>
                  `<div class="row"><div class="grow"><strong>${j.world === "theatre" ? "Episode" : j.id === "audio" ? "Audio part" : "Chapter"} ${n}</strong><p>${n === 1 ? "Completed" : j.state === "failed" ? "Failed · sample source interrupted" : j.state === "paused" ? "Paused" : n === 2 ? "Downloading" : "Queued"}</p></div>${n === 1 ? '<span class="status">✓</span>' : U.btn("Cancel", "cancel-item", `data-id="${j.id}" data-n="${n}"`)}</div>`,
              )
              .join("")}</details>`,
        )
        .join("") || '<p class="small">Nothing is arriving right now.</p>'
    }</section>${
      remote.filter((x) => world === "all" || x.world === world).length
        ? `<h2 class="section-title">Available on another device</h2><p class="small">The file itself is not stored on this device. Download it here when you want it.</p><section class="panel" style="margin-top:15px">${remote
            .filter((x) => world === "all" || x.world === world)
            .map(
              (x) =>
                `<div class="row"><div class="grow"><strong>${x.title}</strong><p>${x.subtitle}</p></div>${U.btn("Download here", "remote", `data-id="${x.id}"`)}</div>`,
            )
            .join("")}</section>`
        : ""
    }<div class="download-lanes">${["tankoban", "biblio", "theatre"]
      .filter((x) => world === "all" || x === world)
      .map(
        (w) =>
          `<button data-action="ledger" data-world="${w}"><strong>${w[0].toUpperCase() + w.slice(1)}</strong><span class="small">${done.filter((x) => x.world === w).length} titles on this device</span></button>`,
      )
      .join("")}</div>${
      ledger
        ? `<section class="panel"><h2>${ledger[0].toUpperCase() + ledger.slice(1)} · On this device</h2>${
            done
              .filter((x) => x.world === ledger)
              .map(
                (x) =>
                  `<div class="row"><div class="grow"><strong>${x.title}</strong><p>${x.subtitle}</p></div>${U.btn(x.kind === "show" ? "View seasons" : "Open", "open", `data-id="${x.id}"`)}${U.btn("•••", "menu", `data-id="${x.id}" aria-label="Actions for ${x.title}"`)}</div>`,
              )
              .join("") ||
            `<p>Nothing from ${ledger} lives here yet.</p><br>${U.btn("Open " + ledger, "open-world", `data-world="${ledger}"`)}`
          }</section>`
        : ""
    }<p class="notice">Staged download manager. Queue progress and local copies are fixture state. No sources are contacted, files downloaded, or real local copies deleted.</p>`;
    U.bind(A, {
      world: (b) => {
        world = b.dataset.world;
        render();
      },
      vault: () => MockUI.go("vault"),
      ledger: (b) => {
        ledger = b.dataset.world;
        render();
      },
      "open-world": (b) => MockUI.go("world." + b.dataset.world),
      pause: (b) => {
        const j = jobs.find((x) => x.id === b.dataset.id);
        j.state = j.state === "paused" ? "downloading" : "paused";
        save();
        render();
      },
      retry: (b) => {
        const j = jobs.find((x) => x.id === b.dataset.id);
        j.state = "downloading";
        notice = "Retry staged for " + j.title + ".";
        save();
        render();
      },
      "cancel-job": (b) => {
        const j = jobs.find((x) => x.id === b.dataset.id);
        confirmation(
          "Cancel download?",
          `Stop the staged download of ${U.esc(j.title)}? Completed fixture items will remain available.`,
          "Cancel download",
          () => {
            jobs = jobs.filter((x) => x.id !== j.id);
            notice = "Download canceled for " + j.title + ".";
          },
        );
      },
      "cancel-item": (b) =>
        confirmation(
          "Cancel item?",
          `Cancel item ${b.dataset.n} in this sample queue?`,
          "Cancel item",
          () => {
            notice = "Item " + b.dataset.n + " canceled in the staged queue.";
            const j = jobs.find((x) => x.id === b.dataset.id);
            j.canceled = [...(j.canceled || []), Number(b.dataset.n)];
            j.subtitle =
              j.canceled.length + " item canceled · remaining queue unchanged";
          },
        ),
      "play-arriving": (b) =>
        MockUI.go("player.video", {
          title: jobs.find((x) => x.id === b.dataset.id).title,
          source: "arriving-fixture",
        }),
      remote: (b) => {
        const x = remote.find((x) => x.id === b.dataset.id);
        jobs.push({ ...x, state: "downloading", pct: 0 });
        remote = remote.filter((y) => y.id !== x.id);
        save();
        notice = "Download staged on this device.";
        render();
      },
      open: (b) => {
        const x = done.find((x) => x.id === b.dataset.id);
        if (x.kind === "show") seasons(x);
        else open(x);
      },
      menu: (b) => menu(done.find((x) => x.id === b.dataset.id)),
    });
  }
  function seasons(x) {
    U.dialog(
      x.title + " · Downloads",
      `<details open><summary>Season 1 · 3 episodes · 1.8 GB</summary>${[1, 2, 3].map((n) => `<div class="row"><div class="grow"><strong>Episode ${n}</strong><p>Downloaded · 600 MB</p></div>${U.btn("Play", "episode", `data-n="${n}"`)}</div>`).join("")}</details>`,
      (el) =>
        U.bind(el, {
          episode: (b) =>
            MockUI.go("player.video", {
              title: x.title,
              episode: Number(b.dataset.n),
              source: "downloads",
            }),
        }),
    );
  }
  function menu(x) {
    U.dialog(
      x.title,
      `<p>${x.subtitle}</p><div class="bar" style="flex-direction:column;align-items:stretch">${U.btn("Open", "open")}${U.btn("Download again", "again")}${U.btn("Delete local copy", "delete")}${U.btn("Cancel", "cancel")}</div>`,
      (el, c) =>
        U.bind(el, {
          open: () => open(x),
          again: () => {
            c();
            jobs.push({
              ...x,
              id: x.id + "-again",
              state: "downloading",
              pct: 0,
            });
            notice = "Another download has been staged for " + x.title + ".";
            save();
            render();
          },
          delete: () => {
            c();
            confirmation(
              "Delete local copy?",
              `Remove ${U.esc(x.title)} from this device’s sample downloads? Your library entry and progress stay in place.`,
              "Delete local copy",
              () => {
                done = done.filter((y) => y.id !== x.id);
                remote.push({
                  ...x,
                  subtitle: "Not downloaded here · fixture copy removed",
                });
                notice =
                  "Local fixture copy removed. You can download it again.";
              },
            );
          },
          cancel: c,
        }),
      true,
    );
  }
  render();
  MockUI.ready();
})();
