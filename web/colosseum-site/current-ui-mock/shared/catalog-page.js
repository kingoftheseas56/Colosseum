(function () {
  "use strict";
  var U = MockUI,
    p = U.params,
    w = p.world || "tankoban",
    mode = p.mode || "discover",
    all =
      mode === "continue" && !p.world
        ? Object.keys(MockCatalog).flatMap(function (k) {
            return MockCatalog[k];
          })
        : MockCatalog[w],
    tabs = {
      tankoban: ["Discover", "Manga", "Comics", "Library"],
      biblio: ["Discover", "Explore", "Library"],
      theatre: ["Discover", "Movies", "Shows", "Anime", "Library"],
    }[w],
    prefix = mode + "." + w,
    saved = U.get(prefix + ".view", {}),
    q = p.query || saved.q || "",
    sort = p.sort || saved.sort || (mode === "library" ? "last" : "popular"),
    genre = p.genre || saved.genre || "All",
    tab = p.tab || saved.tab || "discover",
    list = !!saved.list,
    status = p.status || "ready",
    source = "Built-in catalogue",
    life = p.life || "all",
    media = p.media || "all";
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
  function persist() {
    U.set(prefix + ".view", {
      q: q,
      sort: sort,
      genre: genre,
      tab: tab,
      list: list,
      scroll: document.querySelector(".page").scrollTop,
    });
  }
  function getCollection(c) {
    return U.get("collection." + c.id, c.progress > 0);
  }
  function restoreStatus() {
    status = "ready";
    render();
  }
  document.body.classList.toggle("library-mode", mode === "library");
  $("heading").textContent =
    mode === "library"
      ? "Your collection"
      : mode === "continue"
        ? "Continue"
        : mode === "search"
          ? "Search " + w[0].toUpperCase() + w.slice(1)
          : p.rail ||
            (p.facet
              ? String(p.facet).replace(/[_-]/g, " ")
              : p.catalogId
                ? String(p.catalogId).replace(/[_-]/g, " ")
                : "Discover");
  $("tabs").innerHTML = tabs
    .map(function (t) {
      return (
        '<button class="' +
        ((mode === "library" && t === "Library") ||
        (mode === "discover" && t.toLowerCase() === tab)
          ? "on"
          : "") +
        '">' +
        t +
        "</button>"
      );
    })
    .join("");
  Array.from($("tabs").children).forEach(function (b) {
    b.onclick = function () {
      var t = b.textContent;
      if (t === "Library") U.go("library." + w);
      else if (t === "Explore") U.go("biblio.explore");
      else U.go("discover." + w, { world: w, tab: t.toLowerCase() });
    };
  });
  $("side").innerHTML =
    '<button id="side-toggle" aria-label="Expand source sidebar">›</button><button data-source="Built-in catalogue" title="Built-in catalogue"><img src="../assets/colosseum.svg" alt=""><span>Built-in catalogue</span></button><button data-source="' +
    (w === "theatre"
      ? "TMDB"
      : w === "tankoban"
        ? "Tankoyomi"
        : "Apple Books") +
    '" title="' +
    (w === "theatre"
      ? "TMDB"
      : w === "tankoban"
        ? "Tankoyomi"
        : "Apple Books") +
    '"><img src="../assets/addon-logos/' +
    (w === "theatre"
      ? "tmdb.png"
      : w === "tankoban"
        ? "tankoyomi.png"
        : "applebooks.ico") +
    '" alt=""><span>' +
    (w === "theatre"
      ? "TMDB"
      : w === "tankoban"
        ? "Tankoyomi"
        : "Apple Books") +
    '</span></button><button id="extensions" title="Manage extensions"><img src="../assets/icons/extensions.svg" alt=""><span>Extensions</span></button>';
  // The source sidebar starts collapsed on a fresh Discover, as DiscoverSidebar.qml specifies.
  $("side").classList.toggle("expanded", !!p.sidebar);
  $("side-toggle").onclick = function () {
    $("side").classList.toggle("expanded");
    this.textContent = $("side").classList.contains("expanded") ? "‹" : "›";
  };
  $("extensions").onclick = function () {
    U.go("extensions");
  };
  document.querySelectorAll("[data-source]").forEach(function (b) {
    b.onclick = function () {
      source = b.dataset.source;
      $("fixture").textContent = source + " · Local catalogue fixture";
      sort = "popular";
      $("sort").value = sort;
      render();
    };
  });
  if (mode === "library" || mode === "continue" || mode === "search")
    $("side").hidden = true;
  if (mode === "continue") $("tabs").hidden = true;
  if (mode === "library") {
    var ledger = document.createElement("div");
    ledger.className = "ledger";
    ledger.id = "ledger";
    document
      .querySelector(".main")
      .insertBefore(ledger, document.querySelector(".topline"));
    var tools = document.createElement("div");
    tools.className = "toolbar";
    tools.innerHTML =
      '<select id=life aria-label="Progress filter"><option value=all>All states</option><option value=inProgress>In progress</option><option value=unread>Not started</option><option value=completed>Completed</option></select><select id=media aria-label="Media type filter"><option value=all>All types</option>' +
      Array.from(
        new Set(
          all.map(function (c) {
            return c.kind;
          }),
        ),
      )
        .map(function (k) {
          return "<option value=" + k + ">" + k + "</option>";
        })
        .join("") +
      "</select>";
    tools.appendChild($("sort"));
    document.querySelector(".main").insertBefore(tools, $("count"));
    $("life").value = life;
    $("media").value = media;
    $("life").onchange = function () {
      life = this.value;
      render();
    };
    $("media").onchange = function () {
      media = this.value;
      render();
    };
    $("sort").insertAdjacentHTML(
      "beforeend",
      "<option value=last>Last opened</option><option value=added>Recently added</option>",
    );
  }
  $("query").value = q;
  $("sort").value = sort;
  $("queryform").onsubmit = function (e) {
    e.preventDefault();
    q = $("query").value.trim();
    if (mode === "search" && q) {
      var history = U.get("search." + w + ".history", []).filter(function (s) {
        return s !== q;
      });
      history.unshift(q);
      U.set("search." + w + ".history", history.slice(0, 8));
    }
    status = "ready";
    render();
  };
  $("query").addEventListener("input", function () {
    q = this.value;
    render();
  });
  $("clear").onclick = function () {
    q = "";
    $("query").value = "";
    render();
    $("query").focus();
  };
  $("sort").onchange = function () {
    sort = this.value;
    render();
  };
  $("view-toggle").onclick = function () {
    list = !list;
    render();
  };
  $("filter-toggle").onclick = function () {
    $("filters").hidden = !$("filters").hidden;
  };
  $("filters").hidden =
    !p.filtersOpen && (mode === "search" || mode === "library");
  $("filters").innerHTML =
    ["All"]
      .concat(
        Array.from(
          new Set(
            all.map(function (c) {
              return c.genre;
            }),
          ),
        ),
      )
      .map(function (g) {
        return '<button data-genre="' + esc(g) + '">' + esc(g) + "</button>";
      })
      .join("") + '<button data-action="reset">Reset filters</button>';
  document.querySelectorAll("[data-genre]").forEach(function (b) {
    b.onclick = function () {
      genre = b.dataset.genre;
      render();
    };
  });
  document.querySelector("[data-action=reset]").onclick = function () {
    genre = "All";
    q = "";
    $("query").value = "";
    sort = "popular";
    $("sort").value = sort;
    tab = "discover";
    render();
  };
  function dialog(title, text, actions) {
    var el = document.createElement("div");
    el.className = "overlay";
    el.innerHTML =
      '<section class="dialog"><h2>' +
      esc(title) +
      "</h2><p>" +
      esc(text) +
      '</p><div></div><button data-action="cancel">Cancel</button></section>';
    document.body.appendChild(el);
    var dispose = U.registerOverlay(el, close);
    function close() {
      dispose();
      el.remove();
    }
    el.querySelector("[data-action=cancel]").onclick = close;
    actions.forEach(function (a) {
      var b = document.createElement("button");
      b.textContent = a.label;
      b.onclick = function () {
        close();
        a.action();
      };
      el.querySelector(".dialog>div").appendChild(b);
    });
  }
  function menu(c) {
    dialog(c.title, "Choose an action for this staged library entry.", [
      {
        label: "Details",
        action: function () {
          openCatalog(c);
        },
      },
      {
        label: "Resume",
        action: function () {
          openCatalog(c, true);
        },
      },
      {
        label: getCollection(c)
          ? "Remove from collection"
          : "Add to collection",
        action: function () {
          if (getCollection(c))
            dialog(
              "Remove " + c.title + "?",
              "This removes the title from this mock collection. Reading and watching progress is kept.",
              [
                {
                  label: "Remove from collection",
                  action: function () {
                    U.set("collection." + c.id, false);
                    render();
                  },
                },
              ],
            );
          else {
            U.set("collection." + c.id, true);
            render();
          }
        },
      },
      {
        label: U.get("completed." + c.id, false)
          ? "Mark as unread"
          : "Mark as completed",
        action: function () {
          U.set("completed." + c.id, !U.get("completed." + c.id, false));
          render();
        },
      },
    ]);
  }
  function render() {
    persist();
    if (mode === "library") {
      var savedRows = status === "empty" ? [] : all.filter(getCollection);
      $("ledger").innerHTML = [
        ["all", "saved", savedRows.length],
        [
          "inProgress",
          "in progress",
          savedRows.filter(function (c) {
            return c.progress && !U.get("completed." + c.id, false);
          }).length,
        ],
        [
          "unread",
          w === "theatre" ? "unwatched" : "unread",
          savedRows.filter(function (c) {
            return !c.progress;
          }).length,
        ],
        [
          "completed",
          w === "theatre" ? "watched" : "completed",
          savedRows.filter(function (c) {
            return U.get("completed." + c.id, false);
          }).length,
        ],
      ]
        .map(function (x) {
          return (
            "<button data-life=" +
            x[0] +
            "><b>" +
            x[2] +
            "</b> " +
            x[1] +
            "</button>"
          );
        })
        .join("<i>·</i>");
      document.querySelectorAll("[data-life]").forEach(function (b) {
        b.classList.toggle("on", b.dataset.life === life);
        b.onclick = function () {
          life = b.dataset.life;
          $("life").value = life;
          render();
        };
      });
    }
    $("clear").hidden = !q;
    $("view-toggle").textContent = list ? "Grid view" : "List view";
    $("grid").classList.toggle("list", list);
    document.querySelectorAll("[data-genre]").forEach(function (b) {
      b.classList.toggle("on", b.dataset.genre === genre);
    });
    var rows = all.filter(function (c) {
      return (
        (!p.facet || /fiction|adult|fantasy|science|all/i.test(p.facet)) &&
        (!q || c.title.toLowerCase().includes(q.toLowerCase())) &&
        (genre === "All" || c.genre === genre) &&
        (mode !== "library" ||
          (getCollection(c) &&
            (media === "all" || c.kind === media) &&
            (life === "all" ||
              (life === "completed" && U.get("completed." + c.id, false)) ||
              (life === "inProgress" &&
                c.progress > 0 &&
                !U.get("completed." + c.id, false)) ||
              (life === "unread" &&
                !c.progress &&
                !U.get("completed." + c.id, false))))) &&
        (mode !== "continue" ||
          (c.progress > 0 && !U.get("progress.dismissed." + c.id, false))) &&
        (tab === "discover" ||
          tab === "library" ||
          tab === "all" ||
          c.kind === tab ||
          (tab === "manga" && c.kind === "manga"))
      );
    });
    if (sort === "az")
      rows.sort(function (a, b) {
        return a.title.localeCompare(b.title);
      });
    if (sort === "last")
      rows.sort(function (a, b) {
        return b.progress - a.progress;
      });
    if (sort === "added") rows.reverse();
    if (sort === "new")
      rows.sort(function (a, b) {
        return b.year - a.year;
      });
    if (sort === "rating")
      rows.sort(function (a, b) {
        return b.rating - a.rating;
      });
    $("suggestions").hidden = mode !== "search" || !!q;
    if (mode === "search" && !q) {
      var h = U.get("search." + w + ".history", []);
      $("suggestions").innerHTML =
        (h.length ? h : ["Dune", "One Piece", "Severance"])
          .map(function (t) {
            return (
              '<button data-query="' + esc(t) + '">' + esc(t) + "</button>"
            );
          })
          .join("") +
        (h.length ? '<button id="clear-history">Clear history</button>' : "");
      document.querySelectorAll("[data-query]").forEach(function (b) {
        b.onclick = function () {
          q = b.dataset.query;
          $("query").value = q;
          render();
        };
      });
      if ($("clear-history"))
        $("clear-history").onclick = function () {
          U.set("search." + w + ".history", []);
          render();
        };
    }
    if (status === "empty") rows = [];
    $("count").hidden = status === "loading" || status === "error";
    $("count").textContent =
      rows.length +
      " " +
      (mode === "library"
        ? "saved titles"
        : mode === "continue"
          ? "titles in progress"
          : "results") +
      (genre !== "All" ? " · " + genre : "");
    $("grid").hidden =
      !!(status === "loading" || status === "error") || !rows.length;
    $("empty").hidden = !$("grid").hidden;
    if (status === "loading") {
      $("empty").innerHTML =
        '<h2>Loading catalogue…</h2><p>Staged loading state</p><button id="retry">Load fixture catalogue</button>';
      $("retry").onclick = restoreStatus;
      return;
    }
    if (status === "error") {
      $("empty").innerHTML =
        '<h2>Catalogue unavailable</h2><p>The staged source could not load. Your filters are preserved.</p><button id="retry">Retry</button>';
      $("retry").onclick = restoreStatus;
      return;
    }
    if (!rows.length) {
      $("empty").innerHTML =
        "<h2>" +
        (mode === "library" && !q
          ? "Your collection is empty"
          : "No matching titles") +
        "</h2><p>" +
        (mode === "library"
          ? "Save titles from their detail page or entry menu."
          : "Try another title or reset your filters.") +
        '</p><button id="retry">' +
        (mode === "library" ? "Discover titles" : "Reset filters") +
        "</button>";
      $("retry").onclick = function () {
        if (mode === "library") U.go("discover." + w, { world: w });
        else {
          genre = "All";
          q = "";
          $("query").value = "";
          status = "ready";
          render();
        }
      };
    }
    $("grid").innerHTML = rows
      .map(function (c) {
        var completed = U.get("completed." + c.id, false);
        return (
          '<article class="card" data-id="' +
          c.id +
          '"><button class="open" aria-label="Open ' +
          esc(c.title) +
          '"><img src="' +
          c.cover +
          '" alt="' +
          esc(c.title) +
          '"><h3>' +
          esc(c.title) +
          "</h3><small>" +
          c.genre +
          " · " +
          (completed ? "Completed" : c.year) +
          "</small>" +
          (c.progress
            ? '<div class="bar"><i style="width:' +
              (completed ? 100 : c.progress) +
              '%"></i></div>'
            : "") +
          '</button><button class="more" aria-label="Actions for ' +
          esc(c.title) +
          '">⋮</button></article>'
        );
      })
      .join("");
    Array.from($("grid").children).forEach(function (el, i) {
      el.querySelector(".open").onclick = function () {
        openCatalog(rows[i], mode === "continue");
      };
      el.querySelector(".more").onclick = function () {
        menu(rows[i]);
      };
    });
  }
  render();
  document.querySelector(".page").scrollTop = saved.scroll || 0;
  document.querySelector(".page").onscroll = persist;
  U.ready();
})();
