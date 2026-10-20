(function () {
  "use strict";
  const U = UV,
    A = document.querySelector("#app"),
    P = MockUI.params;
  let facet = P.facet || "All",
    gallery = P.view === "gallery",
    tab = P.tab || "All";
  const marks = new Set([
    "ice-and-fire.png",
    "the-witcher.png",
    "lord-of-the-rings.png",
    "avatar.svg",
    "dc.png",
    "percy-jackson.png",
    "harry-potter.png",
    "dune.png",
    "james-bond.png",
    "sherlock-holmes.png",
    "star-trek.png",
    "studio-ghibli.svg",
    "one-piece.png",
    "naruto.svg",
    "dragon-ball.png",
    "shonen-jump.png",
    "marvel.png",
    "attack-on-titan.svg",
    "jurassic-park.png",
    "star-wars.png",
  ]);
  const tributes = [
    ["DCAU", "dcau", "DC Animated Universe"],
    ["Star Wars", "starwars", "Star Wars"],
    ["One Piece", "onepiece", "One Piece"],
    ["Cosmere", "cosmere", "Cosmere"],
  ];
  const art = (name) =>
    name.includes("DC")
      ? "universes/dcau/portals/watchtower.jpg"
      : name === "Star Wars"
        ? "universes/star-wars/hoth.jpg"
        : name === "One Piece"
          ? "universes/one-piece/east-blue/banners/romance.png"
          : name === "Dune"
            ? "wallpaper/sagas/dune.jpg"
            : "wallpaper/cold-ripple.jpg";
  function roster() {
    A.className = "home";
    let rows = UNIVERSES.filter(
      (x) =>
        facet === "All" ||
        (facet === "Books"
          ? /Book|Novel/.test(x.holds)
          : facet === "Manga"
            ? /Manga/.test(x.holds)
            : facet === "Anime"
              ? /Anime/.test(x.holds)
              : facet === "Films"
                ? /Film/.test(x.holds)
                : facet === "Series"
                  ? /Show|Series/.test(x.holds)
                  : false),
    );
    if (P.state === "empty") rows = [];
    A.innerHTML = `<div class="heading"><h1>Explore Universes</h1><div class="bar"><span>${rows.length} universes</span>${U.btn(gallery ? "List" : "Gallery", "view")}${U.btn("Tributes", "tributes")}</div></div><div class="facets glass">${["All", "Books", "Manga", "Anime", "Films", "Series"].map((x) => U.btn(x, "facet", `data-value="${x}" class="${x === facet ? "on" : ""}"`)).join("")}</div><div class="pile ${gallery ? "gallery" : ""}">${rows.map((x) => `<button class="banner" data-action="universe" data-name="${U.esc(x.name)}"><img class="art" src="${U.img(art(x.name))}" alt=""><div class="wash"></div><div class="mark">${marks.has(x.mark) ? `<img src="${U.img("universes/marks/" + x.mark)}" alt="${U.esc(x.name)}">` : ""}</div><div class="banner-copy"><h2>${x.name}</h2><p>${x.desc}</p><p class="holds">${x.holds}</p></div><span class="explore">Explore ›</span></button>`).join("") || '<div class="empty">No universes hold this yet.</div>'}</div>`;
    U.bind(A, {
      view: () => {
        gallery = !gallery;
        roster();
      },
      facet: (b) => {
        facet = b.dataset.value;
        roster();
      },
      universe: (b) =>
        MockUI.go(
          [
            "Dune",
            "Harry Potter",
            "The Lord of the Rings",
            "Sherlock Holmes",
          ].includes(b.dataset.name)
            ? "universe.saga"
            : "universe.shelves",
          { name: b.dataset.name },
        ),
      tributes: () => {
        U.dialog(
          "Tributes",
          `<p>Distinct, installed tribute entries. These are separate from the structured Universe pages.</p>${tributes.map((x) => `<div class="row"><div class="grow"><strong>${x[0]}</strong><p>Installed tribute fixture</p></div>${U.btn("Enter", "enter", `data-id="${x[1]}"`)}</div>`).join("")}`,
          (el) =>
            U.bind(el, { enter: (b) => MockUI.go("tribute." + b.dataset.id) }),
        );
      },
    });
  }
  function items(name) {
    if (name === "One Piece")
      return [
        ["One Piece", "manga", U.cover("manga-covers", "onepiece.jpg")],
        ["One Piece · Anime", "video", null],
        [
          "One Piece · Color Edition",
          "manga",
          U.cover("manga-covers", "onepiece.jpg"),
        ],
      ];
    if (name === "Dune")
      return [
        ["Dune", "book", null],
        ["Dune Messiah", "book", null],
        ["Children of Dune", "book", null],
        ["Dune: Part Two", "video", U.cover("theatre-posters", "dune.jpg")],
        ["Dune · Graphic Novel", "comic", null],
      ];
    if (name.includes("DC") || name === "Gotham")
      return [
        ["Batman: The Animated Series", "video", null],
        ["Batman: Mask of the Phantasm", "video", null],
        ["Superman: The Animated Series", "video", null],
        ["Batman Adventures", "comic", null],
      ];
    if (name === "Cosmere")
      return [
        ["Mistborn", "book", null],
        ["The Way of Kings", "book", null],
        ["Elantris", "book", null],
        ["Warbreaker", "book", null],
        ["Tress of the Emerald Sea", "book", null],
      ];
    return [
      [name, "book", null],
      [name + " · Films", "video", null],
      [name + " · Comics", "comic", null],
    ];
  }
  function standard() {
    const name = P.name || "Dune",
      kind =
        P.template ||
        (location.pathname.includes("universe-page") ? "UNIVERSE" : "SAGA"),
      media = items(name),
      visible = media.filter(
        (x) =>
          tab === "All" ||
          tab === "About" ||
          tab === "More universes" ||
          (tab === "Read" && ["book", "manga", "comic"].includes(x[1])) ||
          (tab === "Watch" && x[1] === "video"),
      );
    A.className = "screen universe-page";
    A.innerHTML = `<img class="hero-bg" src="${U.img(art(name))}" alt=""><header class="universe-hero"><div class="eyebrow">${kind === "extension" ? "UNIVERSE" : kind.toUpperCase()}</div><h1>${U.esc(name)}</h1><p class="small">${kind === "extension" ? "Installed universe extension · fixture catalogue" : "Books, stories and screen worlds, together."}</p><div class="bar" style="margin-top:24px">${U.btn("Read", "read", 'class="primary"')}${U.btn("Watch", "watch", 'class="primary"')}${tributes.find((x) => x[2] === name) ? U.btn("Open tribute", "tribute") : ""}</div></header><section class="universe-content"><div class="bar">${["All", "Read", "Watch", "About", "More universes"].map((t) => U.btn(t, "tab", `data-tab="${t}" class="${tab === t ? "on" : ""}"`)).join("")}</div>${
      P.state === "uninstalled"
        ? '<div class="empty">This universe isn’t installed yet.</div>'
        : tab === "About"
          ? `<h2 class="section-title">About ${U.esc(name)}</h2><p>${UNIVERSES.find((x) => x.name === name)?.desc || "A connected collection of stories, presented through its worlds and reading paths."}</p><div class="notice">Source-based catalogue layout. Metadata and local illustrations are a representative fixture, not a live extension response.</div>`
          : tab === "More universes"
            ? `<h2 class="section-title">More universes</h2><div class="bar">${UNIVERSES.slice(
                0,
                8,
              )
                .map((x) =>
                  U.btn(x.name, "more", `data-name="${U.esc(x.name)}"`),
                )
                .join("")}</div>`
            : `<h2 class="section-title">${tab === "All" ? "The complete shelves" : tab === "Read" ? "On the page" : "On the screen"}</h2>${
                kind === "SAGA" && tab === "All"
                  ? `<section class="panel"><div class="eyebrow">THE ORIGINAL SAGA</div><h2 class="section-title">The story, across its forms</h2><div class="split"><div><p class="small">NOVELS · READING ORDER</p><div class="media-rail">${visible
                      .filter((x) => x[1] === "book")
                      .map((x) => U.media(...x))
                      .join(
                        "",
                      )}</div></div><div><p class="small">SCREEN ADAPTATIONS</p><div class="media-rail">${visible
                      .filter((x) => x[1] === "video")
                      .map((x) => U.media(...x))
                      .join(
                        "",
                      )}</div></div></div></section><h2 class="section-title">Graphic adaptations</h2><div class="media-rail">${visible
                      .filter((x) => x[1] === "comic")
                      .map((x) => U.media(...x))
                      .join("")}</div>`
                  : `<div class="media-rail">${visible.map((x) => U.media(...x)).join("")}</div>`
              }`
    }</section>`;
    if (kind === "extension") {
      A.querySelector(".universe-hero .bar")?.remove();
      if (P.state !== "uninstalled")
        A.querySelector(".universe-content").innerHTML = [
          "manga",
          "book",
          "video",
          "comic",
        ]
          .filter((k) => media.some((x) => x[1] === k))
          .map(
            (k) =>
              `<h2 class="section-title">${{ manga: "Manga", book: "Books", video: "On screen", comic: "Comics" }[k]} <span class="small">${media.filter((x) => x[1] === k).length}</span></h2><div class="media-rail">${media
                .filter((x) => x[1] === k)
                .map((x) => U.media(...x))
                .join("")}</div>`,
          )
          .join("");
      else A.querySelector(".universe-content .bar")?.remove();
    }
    U.wireMedia(A);
    U.bind(A, {
      tab: (b) => {
        tab = b.dataset.tab;
        standard();
      },
      read: () => {
        const x = media.find((x) => x[1] !== "video");
        if (x)
          MockUI.go(
            x[1] === "manga"
              ? "manga.series"
              : x[1] === "comic"
                ? "comics.series"
                : "book.detail",
            { title: x[0] },
          );
      },
      watch: () => {
        const x = media.find((x) => x[1] === "video");
        if (x) MockUI.go("theatre.detail", { title: x[0] });
      },
      tribute: () =>
        MockUI.go("tribute." + tributes.find((x) => x[2] === name)[1]),
      more: (b) => MockUI.go("universe.shelves", { name: b.dataset.name }),
    });
    enhanceStandard(name, kind, media);
  }

  function enhanceStandard(name, kind, media) {
    if (kind === "extension" || P.state === "uninstalled") return;
    const content = A.querySelector(".universe-content"),
      stored = MockUI.get("universe.continue." + name, null),
      continuing =
        stored === null
          ? media
              .slice(0, 2)
              .map((x, i) => ({ title: x[0], kind: x[1], pct: i ? 23 : 42 }))
          : stored;
    if (tab === "All" && continuing.length) {
      const rail = document.createElement("section");
      rail.className = "continuation-section";
      rail.innerHTML = `<h2 class="section-title">Continue</h2><div class="resume-rail">${continuing.map((x, i) => `<article class="resume-card"><div class="resume-disc"><button data-u-resume="${i}" aria-label="Resume ${U.esc(x.title)}">▶</button></div><div class="grow"><strong>${U.esc(x.title)}</strong><p class="small">${x.pct}% ${x.kind === "video" ? "watched" : "read"}</p><div class="progress" style="--pct:${x.pct}%"><i></i></div><div class="bar"><button data-u-detail="${i}">Details</button><button data-u-remove="${i}" aria-label="Remove ${U.esc(x.title)} from Continue">×</button></div></div></article>`).join("")}</div>`;
      content.children[0].after(rail);
      rail.querySelectorAll("[data-u-resume]").forEach(
        (b) =>
          (b.onclick = () => {
            const x = continuing[b.dataset.uResume];
            MockUI.go(
              x.kind === "video"
                ? "player.video"
                : x.kind === "book"
                  ? "reader.book"
                  : "reader.comic",
              { title: x.title, progress: x.pct, source: "universe" },
            );
          }),
      );
      rail.querySelectorAll("[data-u-detail]").forEach(
        (b) =>
          (b.onclick = () => {
            const x = continuing[b.dataset.uDetail];
            MockUI.go(
              x.kind === "video"
                ? "theatre.detail"
                : x.kind === "book"
                  ? "book.detail"
                  : x.kind === "comic"
                    ? "comics.series"
                    : "manga.series",
              { title: x.title },
            );
          }),
      );
      rail.querySelectorAll("[data-u-remove]").forEach(
        (b) =>
          (b.onclick = () => {
            continuing.splice(Number(b.dataset.uRemove), 1);
            MockUI.set("universe.continue." + name, continuing);
            standard();
          }),
      );
    }
    if (kind === "SAGA" && tab === "All") {
      const panel = content.querySelector(".panel");
      if (panel) {
        const books = media.filter((x) => x[1] === "book"),
          film = media.find((x) => x[1] === "video");
        panel.classList.add("saga-matrix-panel");
        panel.innerHTML = `<div class="bar"><button data-u-jump="matrix">The saga</button><button data-u-jump="beyond">Beyond the saga</button><button data-u-jump="upcoming">Upcoming</button><button data-u-jump="creators">About</button></div><h2 class="section-title">The saga</h2><div class="saga-matrix-layout"><div class="matrix-scroll"><div class="saga-matrix" style="--columns:${books.length}"><div></div>${books.map((b, i) => `<div class="story-heading"><span>${i + 1}</span><strong>${U.esc(b[0])}</strong></div>`).join("")}<div class="medium-label">NOVELS</div>${books.map((b, i) => `<button class="story-cell" data-u-select="${media.indexOf(b)}"><span class="mini-book">${U.esc(b[0])}</span><small>Story ${i + 1}</small></button>`).join("")}<div class="medium-label">FILMS</div>${books.map((b, i) => (i === 0 && film ? `<button class="story-cell screen-cell" data-u-select="${media.indexOf(film)}"><img src="${film[2]}" alt=""><small>${U.esc(film[0])}</small></button>` : '<div class="matrix-gap">—</div>')).join("")}</div></div><aside class="saga-selected" id="saga-selected"></aside></div>`;
        const select = (i) => {
          const x = media[i];
          panel.querySelector("#saga-selected").innerHTML =
            `<div class="eyebrow">${x[1] === "book" ? "BOOK · ORIGINAL SAGA" : "FILM · SCREEN ADAPTATION"}</div><h2>${U.esc(x[0])}</h2><p class="small">${x[1] === "book" ? (name === "Dune" ? "Frank Herbert" : "Catalogue author fixture") : "2024 · Film"}</p><p>Choose this work to continue into its owning world. Progress and descriptions are local fixtures.</p><button data-u-open class="primary">${x[1] === "book" ? "Read" : "Watch"}</button>`;
          panel.querySelector("[data-u-open]").onclick = () =>
            MockUI.go(x[1] === "book" ? "book.detail" : "theatre.detail", {
              title: x[0],
            });
          panel
            .querySelectorAll("[data-u-select]")
            .forEach((b) =>
              b.classList.toggle("selected", Number(b.dataset.uSelect) === i),
            );
        };
        panel
          .querySelectorAll("[data-u-select]")
          .forEach(
            (b) => (b.onclick = () => select(Number(b.dataset.uSelect))),
          );
        select(0);
        const beyond = document.createElement("section");
        beyond.id = "u-beyond";
        beyond.innerHTML = `<h2 class="section-title">Beyond the saga</h2><div class="media-rail">${media
          .filter((x) => x[1] === "comic")
          .map((x) => U.media(...x))
          .join(
            "",
          )}</div><section id="u-upcoming"><h2 class="section-title">Upcoming</h2><div class="panel"><h3>${U.esc(name)} · Next screen chapter</h3><p class="small">Upcoming catalogue fixture · Details pending</p><button disabled style="margin-top:12px">Details pending</button></div></section>`;
        panel.after(beyond);
        U.wireMedia(beyond);
        panel.querySelectorAll("[data-u-jump]").forEach(
          (b) =>
            (b.onclick = () => {
              const t = b.dataset.uJump;
              const el =
                t === "matrix"
                  ? panel
                  : A.querySelector(
                      t === "creators" ? "#u-creators" : "#u-" + t,
                    );
              el?.scrollIntoView({ block: "start" });
            }),
        );
      }
    }
    if (tab === "All" || tab === "About") {
      const creators =
        name === "Dune"
          ? [
              ["Frank Herbert", "Author · 3 books"],
              ["Denis Villeneuve", "Director · 1 film"],
            ]
          : name === "One Piece"
            ? [["Eiichiro Oda", "Author · Manga"]]
            : name === "Cosmere"
              ? [["Brandon Sanderson", "Author · Books"]]
              : [];
      const lower = document.createElement("section");
      lower.className = "universe-about-sections";
      lower.innerHTML = `<section id="u-stills"><h2 class="section-title">Stills</h2><div class="still-rail"><figure><img src="${U.img(art(name))}" alt="${U.esc(name)} source artwork"><figcaption>${U.esc(name)} · bundled source artwork fixture</figcaption></figure></div></section><section id="u-creators"><h2 class="section-title">Creators</h2><div class="creator-rail">${
        creators
          .map(
            (x) =>
              `<article class="creator"><div>${x[0]
                .split(" ")
                .map((w) => w[0])
                .join(
                  "",
                )}</div><strong>${x[0]}</strong><p class="small">${x[1]}</p></article>`,
          )
          .join("") ||
        '<p class="small">Creator records have not been supplied for this sample catalogue.</p>'
      }</div></section><h2 class="section-title">Information</h2><section class="panel information-grid"><div><small>CREATOR</small><p>${creators[0]?.[0] || "Catalogue fixture"}</p></div><div><small>BOOKS / MANGA</small><p>${media.filter((x) => x[1] === "book" || x[1] === "manga").length}</p></div><div><small>FILMS / TELEVISION</small><p>${media.filter((x) => x[1] === "video").length}</p></div><div><small>CATALOGUE</small><p>Apple Books · Cinemeta · local fixture</p></div></section>`;
      content.append(lower);
    }
    if (P.section) {
      requestAnimationFrame(() =>
        A.querySelector(
          P.section === "continue"
            ? ".continuation-section"
            : P.section === "matrix"
              ? ".saga-matrix-panel"
              : "#u-" + P.section,
        )?.scrollIntoView({ block: "start" }),
      );
    }
  }

  const seaArcs = {
    "east-blue": [
      "Romance Dawn",
      "Orange Town",
      "Syrup Village",
      "Baratie",
      "Arlong Park",
      "Loguetown",
    ],
    paradise: [
      "Reverse Mountain",
      "Whisky Peak",
      "Little Garden",
      "Drum Island",
      "Arabasta",
      "Jaya",
      "Skypiea",
      "Water 7",
      "Enies Lobby",
      "Thriller Bark",
      "Sabaody",
      "Amazon Lily",
      "Impel Down",
      "Marineford",
    ],
    "new-world": [
      "Fish-Man Island",
      "Punk Hazard",
      "Dressrosa",
      "Zou",
      "Whole Cake Island",
      "Reverie",
      "Wano Country",
    ],
    "final-sea": ["Egghead", "Elbaf"],
  };
  function dcauComics(hub) {
    const posts =
      {
        Gotham: [11366, 153724, 50187, 15941, 10470, 183948, 80956],
        Metropolis: [14615],
        "Watch Tower": [48881, 10563, 8823],
        "Future Gotham": [190572, 163954, 282726],
      }[hub] || [];
    return (
      DCAU_CATALOGUE.sections.find((s) => s.id === "comics")?.entries || []
    )
      .filter((x) => (x.posts || []).some((p) => posts.includes(p)))
      .map((x) => [x.title, "comic", null, { gcdId: x.gcdId, posts: x.posts }]);
  }
  function dcauItems(hub) {
    const ids =
      {
        Gotham: [
          "tt0103359",
          "tt0106364",
          "tt0118266",
          "tt0143127",
          "tt0337763",
          "tt0346578",
          "tt6556890",
        ],
        Metropolis: ["tt0115378", "tt6075386"],
        "Watch Tower": ["tt0247729", "tt0275137", "tt6025022", "tt8752474"],
        "Future Gotham": ["tt0147746", "tt0231237", "tt0233298", "tt0260662"],
      }[hub] || [];
    return DCAU_CATALOGUE.sections
      .flatMap((s) => s.entries || [])
      .filter((x) => ids.includes(x.id))
      .map((x) => [x.title, "video", null, { id: x.id, year: x.year }]);
  }
  const systems = [
    "High Republic",
    "Fall of the Jedi",
    "Reign of the Empire",
    "Age of Rebellion",
    "New Republic",
    "Rise of the First Order",
    "Beyond the Skywalker Saga",
    "Across the Eras",
    "Beyond Canon",
    "Skywalker Saga",
  ];
  let sea = P.sea || "east-blue",
    selection = P.destination || P.hub || "";
  function tribute() {
    const type = P.tribute || "onepiece",
      title = tributes.find((x) => x[1] === type)?.[0] || "One Piece";
    A.className = "screen";
    let content = "";
    document.querySelector(".wall").style.background =
      type === "cosmere"
        ? "radial-gradient(ellipse at 40% 20%,#1a3442,#070a12 65%)"
        : type === "starwars"
          ? "radial-gradient(ellipse at 50% 45%,#202735,#050711 65%)"
          : type === "dcau"
            ? "linear-gradient(145deg,#202027,#070810)"
            : "linear-gradient(145deg,#183036,#09141a)";
    if (type === "starwars" && selection) {
      const env =
        selection === "Fall of the Jedi"
          ? "coruscant"
          : selection === "Reign of the Empire"
            ? "mustafar"
            : "hoth";
      document.querySelector(".wall").style.background =
        "url('../assets/universes/star-wars/" + env + ".jpg') center/cover";
    }
    document
      .querySelector(".wall")
      .classList.toggle("dcau-environment", type === "dcau" && !!selection);
    if (type === "dcau" && selection) {
      const env =
        {
          Gotham: "gotham",
          Metropolis: "metropolis",
          "Future Gotham": "future",
          "Watch Tower": "justice",
        }[selection] || "gotham";
      document.querySelector(".wall").style.background =
        "url('../assets/universes/dcau/environments/" +
        env +
        (env === "gotham" || env === "justice" ? "-v11-bg.png" : "-v11.svg") +
        "') center/cover";
    }
    if (type === "dcau") {
      const hubs = [
        ["Gotham", "gotham"],
        ["Metropolis", "metropolis"],
        ["Watch Tower", "watchtower"],
        ["Future Gotham", "future-gotham"],
      ];
      content = selection
        ? `<div class="dcau-interior"><h2 class="section-title">${U.esc(selection)}</h2><section><h2 class="section-title">Tankoban</h2><div class="media-rail dcau-comics">${dcauComics(
            selection,
          )
            .map((x) => U.media(...x))
            .join(
              "",
            )}</div></section><section><h2 class="section-title">Theatre</h2><div class="media-rail">${dcauItems(
            selection,
          )
            .map((x) => U.media(...x))
            .join("")}</div></section></div>`
        : `<div class="portals">${hubs.map((x) => `<button class="portal" data-action="destination" data-name="${x[0]}"><img src="${U.img("universes/dcau/portals/" + x[1] + ".jpg")}" alt=""><span>${x[0]}</span></button>`).join("")}</div>`;
    }
    if (type === "starwars") {
      content = selection
        ? `<h2 class="section-title">${U.esc(selection)}</h2><div class="split">${["Theatre", "Biblio", "Tankoban"].map((x, i) => `<section class="panel"><h2>${x}</h2><div class="media-rail">${U.media(selection + " · " + ["Screen stories", "Canon novels", "Comics"][i], ["video", "book", "comic"][i], null)}</div></section>`).join("")}</div>`
        : `<div class="atlas galaxy starwars-galaxy"><div class="orbit"></div><div class="orbit two"></div><div class="atlas-nodes">${systems.map((x) => U.btn(x, "destination", `data-name="${x}"`)).join("")}</div></div>`;
    }
    if (type === "onepiece") {
      content = `<div class="bar">${Object.keys(seaArcs)
        .map((x) =>
          U.btn(
            x
              .split("-")
              .map((s) => s[0].toUpperCase() + s.slice(1))
              .join(" "),
            "sea",
            `data-sea="${x}" class="${x === sea ? "on" : ""}"`,
          ),
        )
        .join(
          "",
        )}</div><div class="atlas" style="margin-top:22px">${sea === "east-blue" ? `<img class="map" src="${U.img("universes/one-piece/east-blue/east-blue-atlas.svg")}" alt="East Blue atlas">` : ""}<div class="atlas-nodes">${seaArcs[sea].map((x) => U.btn(x, "arc", `data-title="${x}"`)).join("")}</div></div>`;
    }
    if (type === "cosmere") {
      content = `<div class="eyebrow" style="color:#78cfe3">THE COGNITIVE ATLAS</div><h2 class="section-title">Six systems. One underlying light.</h2><div class="atlas galaxy cosmere-atlas"><div class="orbit"></div><div class="atlas-nodes">${["Scadrial", "Roshar", "Sel", "Nalthis", "Taldain", "Threnody"].map((x) => U.btn(x, "destination", `data-name="${x}"`)).join("")}</div></div>${selection ? `<h2 class="section-title">${U.esc(selection)} · World gate</h2>` : '<h2 class="section-title">Begin with the kind of journey you want.</h2>'}<div class="media-rail">${(selection ? { Scadrial: [["Mistborn", "book", null]], Roshar: [["The Way of Kings", "book", null]], Sel: [["Elantris", "book", null]], Nalthis: [["Warbreaker", "book", null]], Taldain: [["White Sand", "book", null]], Threnody: [["Shadows for Silence in the Forests of Hell", "book", null]] }[selection] || items("Cosmere") : items("Cosmere")).map((x) => U.media(...x)).join("")}</div>`;
    }
    A.innerHTML = `<div class="heading"><h1>${title}</h1><div class="bar">${selection ? U.btn("‹ Atlas / Portals", "clear") : ""}${U.btn("Structured universe", "structured")}</div></div>${type === "starwars" ? '<p class="eyebrow" style="margin-bottom:24px">IN A GALAXY FAR FAR AWAY</p>' : ""}${content}<p class="notice">Tribute fixture · ${type === "dcau" ? "Original portal and illustrated environment artwork; source-selected Tankoban and Theatre rails. Native scene motion is reduced to a static browser background." : type === "onepiece" ? "Interactive two-dimensional atlas. Original East Blue atlas is bundled; other seas use browser-safe route maps." : "Browser-safe atlas representation; native 3D scenes and live catalogue are not reproduced."}</p>`;
    U.wireMedia(A);
    U.bind(A, {
      destination: (b) => {
        selection = b.dataset.name;
        tribute();
      },
      clear: () => {
        selection = "";
        tribute();
      },
      sea: (b) => {
        sea = b.dataset.sea;
        tribute();
      },
      arc: (b) =>
        MockUI.go("universe.onepiece.arc", { title: b.dataset.title, sea }),
      structured: () =>
        MockUI.go("universe.shelves", {
          name: tributes.find((x) => x[1] === type)?.[2] || title,
        }),
    });
  }
  function arc() {
    const title = P.title || "Romance Dawn";
    document.querySelector(".wall").style.backgroundImage =
      "url('../assets/universes/one-piece/east-blue/banners/romance.png')";
    A.className = "screen";
    A.innerHTML = `<div class="eyebrow">ONE PIECE · ${U.esc((P.sea || "east-blue").replace("-", " ").toUpperCase())}</div><h1 style="font:56px var(--display);margin:15px 0">${U.esc(title)}</h1><p class="small">Arc entry · representative episode and volume fixtures</p><div class="bar" style="margin:24px 0">${["Episodes", "Volumes", "One Pace"].map((t) => U.btn(t, "tab", `data-tab="${t}" class="${tab === t || (tab === "All" && t === "Episodes") ? "on" : ""}"`)).join("")}</div>${tab === "One Pace" ? `<section class="panel"><h2>One Pace</h2><p>Alternative edit sources are selected in Theatre. No extension or torrent is started by this mock.</p><br>${U.btn("Choose source", "pace")}</section>` : tab === "Volumes" ? `<div class="media-rail">${[1, 2, 3].map((n) => U.media("One Piece · Volume " + n, "manga", n === 1 ? U.cover("manga-covers", "onepiece.jpg") : null, { volume: n })).join("")}</div>` : `<section class="panel">${[1, 2, 3].map((n) => `<div class="row"><div class="grow"><strong>Episode ${n}</strong><p>${n === 1 ? "I’m Luffy! The Man Who’s Gonna Be King of the Pirates!" : "Episode fixture · " + title}</p></div>${U.btn("Watch", "episode", `data-n="${n}"`)}</div>`).join("")}</section>`}`;
    U.wireMedia(A);
    U.bind(A, {
      tab: (b) => {
        tab = b.dataset.tab;
        arc();
      },
      pace: () =>
        MockUI.go("theatre.sources", {
          title: "One Piece · " + title,
          source: "One Pace",
        }),
      episode: (b) =>
        MockUI.go("theatre.detail", {
          title: "One Piece",
          episode: Number(b.dataset.n),
          arc: title,
        }),
    });
  }
  if (location.pathname.endsWith("universes.html")) roster();
  else if (location.pathname.endsWith("tribute-page.html")) tribute();
  else if (location.pathname.endsWith("onepiece-arc.html")) arc();
  else standard();
  MockUI.ready();
})();
