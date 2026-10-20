(() => {
  "use strict";
  const M = MockUI,
    P = M.params || {},
    $ = (s) => document.querySelector(s),
    E = (s) =>
      String(s).replace(
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
  let comic = !!P.comic,
    title = P.title || (comic ? "The Sandman" : "One Piece"),
    unit = Number(P.volume || P.edition) || 1,
    state = P.state || "results",
    selected = null,
    disposer = null,
    returnFocus = null;
  $(".eyebrow").textContent = comic
    ? "ALTERNATE SOURCES · COLLECTED EDITION"
    : "SOURCES · TANKOBAN VOLUME";
  $("h1").textContent = comic
    ? P.editionTitle || title + " · Book One"
    : title + " · Vol. " + unit;
  $(".identity").textContent = comic
    ? "Collected edition · Issues #1–8 · Trade Paperback"
    : "English · Volume " + unit + " · " + title;
  $(".source-query").hidden = !comic;
  if (comic)
    $(".source-hero").style.backgroundImage =
      "linear-gradient(90deg,#050608ee,#05060899),url('../assets/fixtures/captures/comic-covers/sandman.jpg')";
  $("[data-action=back]").onclick = $("[data-action=cancel]").onclick = () =>
    M.back();
  $(".source-query").onsubmit = (e) => {
    e.preventDefault();
    state = $(".source-query input").value.toLowerCase().includes("none")
      ? "empty"
      : "results";
    render();
  };
  $("[data-action=clear-query]").onclick = () => {
    $(".source-query input").value = "";
    state = "results";
    render();
    $(".source-query input").focus();
  };
  const rows = comic
    ? [
        [
          "GetComics",
          "Strong match",
          "The Sandman – Book One (Issues 1–8)",
          "strong",
          "ready",
        ],
        [
          "Pirate Bay",
          "Possible match",
          "The Sandman – Complete archive (multi-edition)",
          "possible",
          "combined",
        ],
        [
          "Torrents-CSV",
          "Possible match",
          "The Sandman – Selected issues",
          "possible",
          "incomplete",
        ],
        [
          "ExtraTorrents",
          "Possible match",
          "The Sandman – Two collected archive candidates",
          "possible",
          "ambiguous",
        ],
        [
          "Pirate Bay",
          "Weak match",
          "Sandman Mystery Theatre – Collected edition",
          "weak",
          "ready",
        ],
      ]
    : [
        [
          "Nyaa",
          "Strong match",
          title + " v" + String(unit).padStart(2, "0") + " (Digital) [English]",
          "strong",
          "ready",
        ],
        [
          "Nyaa",
          "Possible match",
          title + " Volumes 1–10 (English)",
          "possible",
          "ready",
        ],
        [
          "Nyaa",
          "Weak match",
          title + " Collected Volumes (unverified numbering)",
          "weak",
          "ready",
        ],
      ];
  function close() {
    document.querySelector(".overlay")?.remove();
    disposer?.();
    disposer = null;
    returnFocus?.focus();
  }
  function weak(row) {
    returnFocus = document.activeElement;
    let o = document.createElement("div");
    o.className = "overlay";
    o.innerHTML =
      '<div class="dialog" role="dialog" aria-modal="true" aria-label="Confirm source mismatch"><h2>Check this release</h2><p>This release does not closely match the ' +
      (comic ? "collected edition" : "volume") +
      ".</p><p>" +
      E(row[2]) +
      '</p><div class="buttons"><button class="pill" data-action="cancel-mismatch">Go back</button><button class="pill gold" data-action="confirm-mismatch">Choose anyway</button></div></div>';
    document.body.append(o);
    disposer = M.registerOverlay(o, close);
    o.querySelector("[data-action=cancel-mismatch]").onclick = close;
    o.querySelector("[data-action=confirm-mismatch]").onclick = () => {
      close();
      inspect(row);
    };
    o.querySelector("button").focus();
  }
  function inspect(row) {
    selected = row;
    state = "inspecting";
    render();
  }
  function acquired() {
    state = "downloading";
    let map = M.get(comic ? "comics.downloads" : "manga.downloads", {});
    map[(comic ? "e" : "v") + unit] = "downloading";
    M.set(comic ? "comics.downloads" : "manga.downloads", map);
    render();
  }
  function finish() {
    state = "ready";
    let map = M.get(comic ? "comics.downloads" : "manga.downloads", {});
    map[(comic ? "e" : "v") + unit] = "ready";
    M.set(comic ? "comics.downloads" : "manga.downloads", map);
    render();
  }
  function render() {
    let box = $(".source-results");
    if (state === "results") {
      box.innerHTML =
        '<div class="source-count"><span>' +
        rows.length +
        (comic ? " results" : " sources") +
        "</span><span>" +
        (comic ? "Pirate Bay · ExtraTorrents · Torrents-CSV" : "Nyaa") +
        "</span></div>";
      rows.forEach((r, i) => {
        let a = document.createElement("article");
        a.className = "source-row";
        a.innerHTML =
          '<img class="source-logo" src="../assets/addon-logos/' +
          (comic
            ? i === 0
              ? "getcomics.png"
              : "thepiratebay.png"
            : "nyaa.png") +
          '" alt="' +
          r[0] +
          '"><div class="details"><small>' +
          E(r[0]) +
          '</small><span class="confidence">' +
          r[1].toUpperCase() +
          "</span><h3>" +
          E(r[2]) +
          "</h3><small>" +
          (comic ? "FORMAT RANGE · ISSUES" : "VOLUME MATCH · ENGLISH") +
          ' · 214 MB · 24 seeds · Fixture result</small></div><button class="source-download" data-action="choose-source" data-source="' +
          i +
          '" aria-label="Choose ' +
          E(r[2]) +
          '">↓</button>';
        if (comic && (i === 2 || i === 3)) {
          let icon = a.querySelector(".source-logo"),
            fallback = document.createElement("span");
          fallback.className = "source-logo fallback";
          fallback.textContent = r[0].charAt(0);
          fallback.setAttribute("aria-label", r[0]);
          icon.replaceWith(fallback);
        }
        a.querySelector("button").onclick = () =>
          r[3] === "weak" ? weak(r) : inspect(r);
        box.append(a);
      });
      return;
    }
    let body = "",
      actions = [];
    switch (state) {
      case "inspecting":
        body = "Inspecting pack…";
        actions = [
          [
            "Finish fixture inspection",
            "inspect",
            () => {
              state = selected?.[4] || "ambiguous";
              if (state === "ready") acquired();
              else render();
            },
          ],
          [
            "Cancel",
            "cancel-inspection",
            () => {
              state = "results";
              render();
            },
          ],
        ];
        break;
      case "ambiguous":
        body =
          "Choose an archive\nMore than one archive can cover this edition.";
        actions = [
          ["Book One.cbz · #1–8", "archive-one", acquired],
          ["Book One (alternate).cbz · #1–8", "archive-two", acquired],
          [
            "Cancel",
            "cancel-archive",
            () => {
              state = "results";
              render();
            },
          ],
        ];
        break;
      case "incomplete":
        body = "This pack is missing issues this edition needs.\n#4 · #7";
        actions = [
          [
            "Try another source",
            "another-source",
            () => {
              state = "results";
              render();
            },
          ],
          [
            "Search manually",
            "manual-search",
            () => {
              state = "results";
              render();
              $(".source-query input").focus();
            },
          ],
        ];
        break;
      case "combined":
        body =
          "Only a combined archive covers this edition — it likely includes other editions too.";
        actions = [
          [
            "Go back",
            "cancel-combined",
            () => {
              state = "results";
              render();
            },
          ],
          ["Download whole archive anyway", "confirm-combined", acquired],
        ];
        break;
      case "downloading":
        body =
          "Downloading 42%\nStaged local acquisition. No file is being downloaded.";
        actions = [
          ["Finish fixture download", "finish-download", finish],
          [
            "Simulate failure",
            "fail-download",
            () => {
              state = "error";
              render();
            },
          ],
          [
            "Cancel download",
            "cancel-download",
            () => {
              let map = M.get(
                comic ? "comics.downloads" : "manga.downloads",
                {},
              );
              delete map[(comic ? "e" : "v") + unit];
              M.set(comic ? "comics.downloads" : "manga.downloads", map);
              state = "results";
              render();
            },
          ],
        ];
        break;
      case "ready":
        body =
          "Ready on this device\nSample reader pages are ready to inspect.";
        actions = [
          [
            "Read",
            "read",
            () =>
              M.go("reader.comic", {
                title,
                entry: unit,
                kind: comic ? "issue" : "volume",
                mode: comic ? "comic" : "manga",
                returnMode: "volumes",
                sourceReturn: true,
              }),
          ],
          ["Back to series", "series", () => M.back()],
        ];
        break;
      case "loading":
        body = comic ? "Searching comic sources…" : "Searching volume sources…";
        actions = [
          [
            "Finish fixture loading",
            "finish-loading",
            () => {
              state = "results";
              render();
            },
          ],
        ];
        break;
      case "unavailable":
        body = "Enable Tankorent to find volume sources.";
        actions = [["Open Extensions", "extensions", () => M.go("extensions")]];
        break;
      case "empty":
        body = "No sources found. Try a different title.";
        actions = [
          [
            "Try again",
            "retry",
            () => {
              state = "results";
              render();
            },
          ],
        ];
        break;
      default:
        body = "Source search failed. No download was started.";
        actions = [
          [
            "Retry",
            "retry",
            () => {
              state = "results";
              render();
            },
          ],
        ];
    }
    box.innerHTML =
      '<div class="statebox"><h2 style="font:26px var(--display);white-space:pre-line">' +
      E(body) +
      '</h2><div class="source-actions"></div></div>';
    actions.forEach(([label, id, fn]) => {
      let b = document.createElement("button");
      b.textContent = label;
      b.dataset.action = id;
      b.onclick = fn;
      box.querySelector(".source-actions").append(b);
    });
  }
  render();
  M.ready();
})();
