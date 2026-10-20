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
  let view = P.view || "editions",
    query = "",
    sort = "Newest",
    title = P.title || "The Sandman",
    dl = M.get("comics.downloads", { e1: "ready" });
  $("h1").textContent = title;
  if (P.cover && P.cover.startsWith("../assets/"))
    $(".comic-hero img").src = P.cover;
  const covers = [
      "sandman",
      "watchmen",
      "invincible",
      "hellboy",
      "saga",
      "sincity",
    ],
    titles = [
      "The Sandman",
      "Watchmen",
      "Invincible",
      "Hellboy",
      "Saga",
      "Sin City",
    ];
  $("[data-action=back]").onclick = $("[data-action=close]").onclick = () =>
    M.back();
  function read(n) {
    M.go("reader.comic", { title, entry: n, kind: "issue", mode: "comic" });
  }
  function source(n) {
    M.go("comics.sources", {
      comic: true,
      title,
      edition: n,
      editionTitle: title + " · Book " + n,
    });
  }
  $("[data-action=continue]").onclick = () => read(1);
  $("[data-action=library]").onclick = (e) => {
    let saved = !M.get("collection." + (P.id || "sandman"), false);
    M.set("collection." + (P.id || "sandman"), saved);
    e.target.textContent = saved ? "✓ In Library" : "+ Library";
  };
  document.querySelectorAll("[data-view]").forEach(
    (b) =>
      (b.onclick = () => {
        view = b.dataset.view;
        render();
      }),
  );
  function render() {
    document
      .querySelectorAll("[data-view]")
      .forEach((b) => b.classList.toggle("active", b.dataset.view === view));
    let c = $("#comics-content");
    c.innerHTML = "";
    if (["archive", "publishers", "archive-index"].includes(view)) {
      window.renderComicArchives({ view, params: P, container: c });
      return;
    }
    if (P.state === "unavailable") {
      c.innerHTML =
        '<div class="statebox"><h2>Not available from sources yet</h2><p>This series is catalogued, but no reading source carries it.</p><button class="pill" data-action="alternate">Find alternate sources</button></div>';
      c.querySelector("button").onclick = () => source(1);
      return;
    }
    if (view === "editions") {
      [
        ["Deluxe", [1, 2]],
        ["Trade Paperback", [3, 4]],
      ].forEach(([format, ns]) => {
        c.insertAdjacentHTML(
          "beforeend",
          '<div class="format-head">' +
            format +
            " <span>" +
            ns.length +
            "</span></div>",
        );
        ns.forEach((n) => {
          let ready = dl["e" + n] === "ready",
            row = document.createElement("article");
          row.className = "edition";
          row.innerHTML =
            '<div class="edition-cover">' +
            (n === 1
              ? '<img style="width:100%;height:100%;object-fit:cover" src="../assets/fixtures/captures/comic-covers/sandman.jpg" alt="Series cover fixture">'
              : "NO COVER") +
            '</div><div class="edition-info"><h2>Book ' +
            n +
            "</h2><p>Neil Gaiman · " +
            (n === 1 ? "240" : "256") +
            " Pages · 2022</p><p>Collects issues #" +
            ((n - 1) * 8 + 1) +
            "–" +
            n * 8 +
            ". " +
            format +
            '.</p><div class="source-evidence">' +
            (n === 4
              ? "No verified source available"
              : "1 verified source · GetComics · Fixture") +
            '</div></div><button class="pill" data-action="alternate-source" aria-label="Find alternate sources for Book ' +
            n +
            '">⌕</button><button class="pill ' +
            (ready ? "gold" : "") +
            '" data-action="edition-read">' +
            (ready ? "Read" : n === 4 ? "Unavailable" : "Download") +
            "</button>";
          row.querySelector("[data-action=alternate-source]").onclick = () =>
            source(n);
          row.querySelector("[data-action=edition-read]").onclick = () =>
            ready ? read(n) : source(n);
          if (n === 4)
            row.querySelector("[data-action=edition-read]").disabled = true;
          c.append(row);
        });
      });
    } else if (view === "issues") {
      c.innerHTML =
        '<div class="chapter-ledger"><div class="ledger-head"><h2>Issues</h2><small>75 issues · #1 first</small></div><div class="issues"></div></div>';
      for (let n = 1; n <= 12; n++) {
        let row = document.createElement("div");
        row.className = "issue-row";
        row.innerHTML =
          '<div class="number">' +
          n +
          '</div><div class="info">' +
          E(title) +
          " #" +
          n +
          "<small>" +
          (n === 1
            ? "Downloaded"
            : n === 7
              ? "Not available from sources yet"
              : "Available") +
          '</small></div><button class="pill" data-action="issue-read">' +
          (n === 1 ? "Read" : "Get") +
          "</button>";
        row.querySelector("button").onclick = () =>
          n === 1 ? read(n) : source(n);
        c.querySelector(".issues").append(row);
      }
    } else if (view === "releases") {
      c.innerHTML =
        '<div class="release-controls"><input aria-label="Filter releases" placeholder="Filter releases" value="' +
        E(query) +
        '"><button class="pill" data-action="clear-filter">Clear</button><select aria-label="Release order"><option>Newest</option><option>Oldest</option><option>A–Z</option></select></div><div class="releases"></div>';
      let rows = Array.from({ length: 8 }, (_, i) => ({
        n: i + 1,
        title: title + " – Book " + (i + 1),
      })).filter((x) => x.title.toLowerCase().includes(query.toLowerCase()));
      if (sort === "Newest") rows.reverse();
      c.querySelector("select").value = sort;
      c.querySelector("select").onchange = (e) => {
        sort = e.target.value;
        render();
      };
      c.querySelector("input").oninput = (e) => {
        query = e.target.value;
        let pos = e.target.selectionStart;
        render();
        c.querySelector("input").focus();
        c.querySelector("input").setSelectionRange(pos, pos);
      };
      c.querySelector("[data-action=clear-filter]").onclick = () => {
        query = "";
        render();
        c.querySelector("input").focus();
      };
      if (!rows.length)
        c.querySelector(".releases").innerHTML =
          '<div class="statebox">No releases match “' + E(query) + "”.</div>";
      rows.forEach((x) => {
        let row = document.createElement("div");
        row.className = "issue-row";
        row.innerHTML =
          '<img width="36" height="36" src="../assets/addon-logos/getcomics.png" alt="GetComics"><div class="info">' +
          E(x.title) +
          '<small>GetComics · 2026 · Fixture release</small></div><button class="pill" data-action="release-open">Read</button>';
        row.querySelector("button").onclick = () => source(x.n);
        c.querySelector(".releases").append(row);
      });
    } else if (view === "publishers") {
      c.innerHTML =
        '<div class="format-head">Publishers</div><div class="archive-grid"></div>';
      ["DC", "Marvel", "Image", "Dark Horse", "Independent"].forEach((s, i) => {
        let b = document.createElement("button");
        b.className = "archive-card pill";
        b.style.height = "130px";
        b.innerHTML =
          '<h2 style="font:26px var(--display)">' +
          s +
          "</h2><p>Browse publisher archive</p>";
        b.onclick = () => {
          view = "archive";
          render();
        };
        c.querySelector(".archive-grid").append(b);
      });
    } else {
      c.innerHTML =
        '<div class="format-head">Comic Archive</div><div class="release-controls"><label>Archive index <select aria-label="Archive index"><option>All titles</option><option>A–H</option><option>I–R</option><option>S–Z</option></select></label></div><div class="archive-grid"></div>';
      const draw = (val) => {
        let grid = c.querySelector(".archive-grid");
        grid.innerHTML = "";
        titles.forEach((t, i) => {
          let ch = t.replace(/^The /, "").charAt(0);
          if (
            (val === "A–H" && ch > "H") ||
            (val === "I–R" && (ch < "I" || ch > "R")) ||
            (val === "S–Z" && ch < "S")
          )
            return;
          let b = document.createElement("button");
          b.className = "archive-card";
          b.innerHTML =
            '<img src="../assets/fixtures/captures/comic-covers/' +
            covers[i] +
            '.jpg" alt="' +
            t +
            '"><strong>' +
            t +
            "</strong><p>Collected editions</p>";
          b.onclick = () => M.go("comics.series", { title: t });
          grid.append(b);
        });
      };
      draw("All titles");
      c.querySelector("select").onchange = (e) => draw(e.target.value);
    }
  }
  render();
  M.ready();
})();
