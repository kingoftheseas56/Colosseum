(function () {
  "use strict";
  var U = MockUI,
    p = U.params;
  if (p.mode === "continue") {
    var script = document.createElement("script");
    script.src = "../shared/catalog-page.js";
    document.body.appendChild(script);
    return;
  }
  var w = p.world || "tankoban",
    all = MockCatalog[w],
    saved = U.get("library." + w + ".view", {}),
    q = p.query || saved.q || "",
    sort = p.sort || saved.sort || (w === "biblio" ? "added" : "last"),
    life = p.life || saved.life || "all",
    media = p.media || saved.media || "all",
    airing = p.airing || saved.airing || "all",
    status = p.status || "ready",
    list = false,
    menuClose = null;
  function $(id) {
    return document.getElementById(id);
  }
  function esc(s) {
    return String(s).replace(/[&<>"']/g, function (c) {
      return {
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;",
      }[c];
    });
  }
  function progress(c) {
    return U.get("progress.dismissed." + c.id, false) ? 0 : c.progress;
  }
  function savedTitle(c) {
    return U.get("collection." + c.id, c.progress > 0);
  }
  function completed(c) {
    return U.get("completed." + c.id, false);
  }
  function downloaded(c) {
    return w !== "biblio" && [0, 6].includes(+c.id.split("-").pop());
  }
  function newEpisodes(c) {
    return w === "theatre" && c.title === "Severance" ? 2 : 0;
  }
  function air(c) {
    return c.kind === "shows" || c.kind === "anime"
      ? c.title === "Stranger Things"
        ? "ended"
        : "ongoing"
      : "";
  }
  function save() {
    U.set("library." + w + ".view", {
      q: q,
      sort: sort,
      life: life,
      media: media,
      airing: airing,
      scroll: document.querySelector(".page").scrollTop,
    });
  }
  document.body.classList.add("library-mode", "source-library", w + "-library");
  $("side").hidden = true;
  $("heading").textContent = "Library";
  $("fixture").hidden = true;
  $("suggestions").hidden = true;
  $("filters").hidden = true;
  $("category").hidden = true;
  document.querySelector(".topline").hidden = true;
  var tabs = {
    tankoban: ["Discover", "Manga", "Comics", "Library"],
    biblio: ["Discover", "Explore", "Library"],
    theatre: ["Discover", "Movies", "Shows", "Anime", "Library"],
  }[w];
  $("tabs").innerHTML = tabs
    .map(function (t) {
      return (
        '<button class="' +
        (t === "Library" ? "on" : "") +
        '">' +
        t +
        "</button>"
      );
    })
    .join("");
  Array.from($("tabs").children).forEach(function (b) {
    b.onclick = function () {
      var t = b.textContent;
      if (t === "Library") return;
      if (t === "Explore") U.go("biblio.explore");
      else U.go("discover." + w, { world: w, tab: t.toLowerCase() });
    };
  });
  var header = document.createElement("div");
  header.className = "library-header";
  header.innerHTML = '<div id="ledger" class="ledger"></div>';
  document.querySelector(".main").prepend(header);
  header.appendChild($("queryform"));
  $("query").placeholder =
    w === "biblio" ? "Search by title or author" : "Search library";
  $("query").value = q;
  $("submit").hidden = true;
  $("filter-toggle").hidden = true;
  $("view-toggle").hidden = true;
  $("sort").hidden = true;
  $("queryform").onsubmit = function (e) {
    e.preventDefault();
    q = $("query").value;
    render();
  };
  $("query").oninput = function () {
    q = this.value;
    render();
  };
  $("clear").onclick = function () {
    q = "";
    $("query").value = "";
    render();
    $("query").focus();
  };
  var controls = document.createElement("div");
  controls.className = "library-controls";
  controls.id = "library-controls";
  header.after(controls);
  function pills(items, key) {
    return items
      .map(function (x) {
        return (
          '<button data-filter="' +
          key +
          '" data-value="' +
          x[0] +
          '">' +
          x[1] +
          "</button>"
        );
      })
      .join("");
  }
  var sorts = [
    ["last", w === "theatre" ? "Last watched" : "Last read"],
    ["added", "Recently added"],
    ["az", "A–Z"],
  ];
  if (w === "theatre") sorts.push(["new", "Year"]);
  controls.innerHTML =
    pills(sorts, "sort") +
    "<i></i>" +
    (w === "theatre"
      ? pills(
          [
            ["all", "All"],
            ["movies", "Movies"],
            ["series", "Series"],
          ],
          "media",
        ) +
        "<i></i>" +
        pills(
          [
            ["all", "All"],
            ["ongoing", "Ongoing"],
            ["ended", "Ended"],
          ],
          "airing",
        )
      : pills(
          w === "tankoban"
            ? [
                ["all", "All"],
                ["inProgress", "In Progress"],
                ["downloaded", "Downloaded"],
              ]
            : [
                ["all", "All"],
                ["inProgress", "In Progress"],
              ],
          "life",
        ));
  controls.querySelectorAll("button").forEach(function (b) {
    b.onclick = function () {
      var k = b.dataset.filter,
        v = b.dataset.value;
      if (k === "sort") sort = v;
      if (k === "life") life = v;
      if (k === "media") media = v;
      if (k === "airing") airing = v;
      render();
    };
  });
  function confirmRemove(c) {
    var el = document.createElement("div");
    el.className = "overlay";
    el.innerHTML =
      '<section class="dialog"><h2>Remove ' +
      esc(c.title) +
      '?</h2><p>Remove this title from your staged library? Progress is kept.</p><button data-remove>Remove from Library</button><button data-action="cancel">Cancel</button></section>';
    document.body.appendChild(el);
    var dispose = U.registerOverlay(el, close);
    function close() {
      dispose();
      el.remove();
    }
    el.querySelector("[data-action=cancel]").onclick = close;
    el.querySelector("[data-remove]").onclick = function () {
      U.set("collection." + c.id, false);
      close();
      render();
    };
    el.querySelector("[data-action=cancel]").focus();
  }
  function openMenu(c, button) {
    if (menuClose) menuClose();
    var cover = document.createElement("div");
    cover.className = "library-menu-dismiss";
    var panel = document.createElement("div");
    panel.className = "library-menu";
    panel.setAttribute("role", "menu");
    panel.setAttribute("aria-label", c.title + " actions");
    var actions = [];
    if (w !== "tankoban") {
      if (w === "theatre" || progress(c) > 0)
        actions.push({
          label: progress(c) > 0 ? "Resume" : w === "theatre" ? "Play" : "Read",
          run: function () {
            openCatalog(c, true);
          },
        });
      actions.push({
        label: "Details",
        run: function () {
          openCatalog(c);
        },
      });
    }
    if (w === "theatre") {
      if (progress(c) > 0)
        actions.push({
          label: "Dismiss progress",
          run: function () {
            U.set("progress.dismissed." + c.id, true);
            render();
          },
        });
      actions.push({
        label: completed(c) ? "Mark unwatched" : "Mark watched",
        run: function () {
          U.set("completed." + c.id, !completed(c));
          render();
        },
      });
    }
    actions.push({
      label: "Remove from Library",
      warn: true,
      run: function () {
        confirmRemove(c);
      },
    });
    actions.forEach(function (a) {
      var b = document.createElement("button");
      b.setAttribute("role", "menuitem");
      b.textContent = a.label;
      if (a.warn) b.className = "warn";
      b.onclick = function () {
        close();
        a.run();
      };
      panel.appendChild(b);
    });
    document.body.append(cover, panel);
    var rect = button.getBoundingClientRect(),
      height = actions.length * 38 + 12;
    panel.style.left = Math.max(8, Math.min(rect.right - 200, 1072)) + "px";
    panel.style.top =
      Math.max(8, Math.min(rect.bottom + 4, 720 - height - 8)) + "px";
    var dispose = U.registerOverlay(panel, close);
    panel.removeAttribute("aria-modal");
    cover.onclick = close;
    function close() {
      dispose();
      panel.remove();
      cover.remove();
      menuClose = null;
      button.focus();
    }
    menuClose = close;
    panel.addEventListener("keydown", function (e) {
      var buttons = Array.from(panel.querySelectorAll("button")),
        idx = buttons.indexOf(document.activeElement),
        n = idx;
      if (e.key === "ArrowDown") n = (idx + 1) % buttons.length;
      else if (e.key === "ArrowUp")
        n = (idx - 1 + buttons.length) % buttons.length;
      else if (e.key === "Home") n = 0;
      else if (e.key === "End") n = buttons.length - 1;
      else return;
      e.preventDefault();
      buttons[n].focus();
    });
    panel.querySelector("button").focus();
  }
  function render() {
    save();
    $("clear").hidden = !q;
    var titles = status === "empty" ? [] : all.filter(savedTitle);
    $("ledger").hidden = w !== "theatre";
    if (w === "theatre") {
      $("ledger").innerHTML = [
        ["all", "saved", titles.length],
        [
          "inProgress",
          "in progress",
          titles.filter(function (c) {
            return progress(c) > 0 && !completed(c);
          }).length,
        ],
        [
          "unread",
          "unwatched",
          titles.filter(function (c) {
            return !progress(c) && !completed(c);
          }).length,
        ],
        ["completed", "watched", titles.filter(completed).length],
        [
          "newEpisodes",
          "new episodes",
          titles.filter(function (c) {
            return newEpisodes(c) > 0;
          }).length,
        ],
        ["downloaded", "downloaded", titles.filter(downloaded).length],
      ]
        .map(function (x) {
          return (
            '<button data-life="' +
            x[0] +
            '" class="' +
            (life === x[0] ? "on" : "") +
            '"><b>' +
            x[2] +
            "</b> " +
            x[1] +
            "</button>"
          );
        })
        .join("<i>·</i>");
      $("ledger")
        .querySelectorAll("button")
        .forEach(function (b) {
          b.onclick = function () {
            life = life === b.dataset.life ? "all" : b.dataset.life;
            render();
          };
        });
    }
    controls.querySelectorAll("button").forEach(function (b) {
      b.classList.toggle(
        "on",
        b.dataset.value ===
          { sort: sort, life: life, media: media, airing: airing }[
            b.dataset.filter
          ],
      );
    });
    var rows = titles.filter(function (c) {
      return (
        (!q ||
          (
            c.title +
            " " +
            (c.kind === "book"
              ? c.title === "Dune"
                ? "Frank Herbert"
                : "Andy Weir"
              : "")
          )
            .toLowerCase()
            .includes(q.toLowerCase())) &&
        (media === "all" ||
          c.kind === media ||
          (media === "series" && ["shows", "anime"].includes(c.kind))) &&
        (airing === "all" || air(c) === airing) &&
        (life === "all" ||
          (life === "inProgress" && progress(c) > 0 && !completed(c)) ||
          (life === "unread" && !progress(c) && !completed(c)) ||
          (life === "completed" && completed(c)) ||
          (life === "newEpisodes" && newEpisodes(c) > 0) ||
          (life === "downloaded" && downloaded(c)))
      );
    });
    if (sort === "az")
      rows.sort(function (a, b) {
        return a.title.localeCompare(b.title);
      });
    if (sort === "last")
      rows.sort(function (a, b) {
        return progress(b) - progress(a);
      });
    if (sort === "added") rows.reverse();
    if (sort === "new")
      rows.sort(function (a, b) {
        return b.year - a.year;
      });
    $("count").textContent = rows.length + " saved titles · staged library";
    $("grid").hidden =
      !rows.length || status === "error" || status === "loading";
    $("empty").hidden = !$("grid").hidden;
    if ($("grid").hidden) {
      var title =
        status === "error"
          ? "Library unavailable"
          : status === "loading"
            ? "Loading library…"
            : !titles.length
              ? "Your library is empty"
              : "Nothing matches these filters";
      $("empty").innerHTML =
        "<h2>" +
        title +
        "</h2><p>" +
        (!titles.length
          ? "Save a title with + Library — it lands here."
          : "Your saved titles and progress are preserved.") +
        '</p><button id="retry">' +
        (status === "error" || status === "loading"
          ? "Retry"
          : !titles.length
            ? "Discover titles"
            : "Reset filters") +
        "</button>";
      $("retry").onclick = function () {
        if (!titles.length && status !== "error" && status !== "loading")
          U.go("discover." + w, { world: w });
        else {
          status = "ready";
          q = "";
          life = "all";
          media = "all";
          airing = "all";
          $("query").value = "";
          render();
        }
      };
    } else {
      $("grid").innerHTML = rows
        .map(function (c) {
          return (
            '<article class="card" data-id="' +
            c.id +
            '"><button class="open" aria-label="Open ' +
            esc(c.title) +
            '"><img src="' +
            c.cover +
            '" alt="' +
            esc(c.title) +
            '">' +
            (newEpisodes(c)
              ? '<span class="new-episodes">+' + newEpisodes(c) + "</span>"
              : "") +
            (downloaded(c)
              ? '<span class="downloaded-badge" aria-label="Downloaded">↓</span>'
              : "") +
            "<h3>" +
            esc(c.title) +
            "</h3><small>" +
            esc(
              w === "biblio"
                ? c.title === "Dune"
                  ? "Frank Herbert"
                  : "Andy Weir"
                : c.year + (air(c) ? " · " + air(c) : ""),
            ) +
            "</small>" +
            (progress(c)
              ? '<div class="bar"><i style="width:' +
                (completed(c) ? 100 : progress(c)) +
                '%"></i></div>'
              : "") +
            '</button><button class="more" aria-haspopup="menu" aria-label="Actions for ' +
            esc(c.title) +
            '">⋮</button></article>'
          );
        })
        .join("");
      Array.from($("grid").children).forEach(function (el, i) {
        el.querySelector(".open").onclick = function () {
          openCatalog(rows[i], w === "tankoban" && progress(rows[i]) > 0);
        };
        el.querySelector(".more").onclick = function () {
          openMenu(rows[i], this);
        };
      });
    }
  }
  render();
  document.querySelector(".page").scrollTop = saved.scroll || 0;
  document.querySelector(".page").onscroll = function () {
    if (menuClose) menuClose();
    save();
  };
  U.ready();
})();
