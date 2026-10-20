(function () {
  var U = MockUI,
    p = U.params,
    w = p.world || "tankoban",
    rows = MockCatalog[w],
    q = p.query || "",
    genre = p.genre || "",
    status = p.status || "ready",
    expanded = false,
    audioStatus = p.audioStatus || "ready";
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
  function commit() {
    if (q.trim()) {
      var h = U.get("search." + w + ".history", []).filter(function (x) {
        return x !== q;
      });
      h.unshift(q);
      U.set("search." + w + ".history", h.slice(0, 8));
    }
  }
  function back() {
    commit();
    U.back();
  }
  $("back").onclick = back;
  $("esc").onclick = back;
  $("close").onclick = back;
  $("minimize").onclick = back;
  $("fit").onclick = function () {
    document.querySelector(".page").scrollTop = 0;
  };
  $("query").value = q;
  $("query").placeholder =
    "Search " +
    (w === "biblio"
      ? "books"
      : w === "tankoban"
        ? "manga and comics"
        : "movies, shows and anime") +
    "…";
  $("query").oninput = function () {
    q = this.value;
    genre = "";
    expanded = false;
    status = "ready";
    render();
  };
  $("queryform").onsubmit = function (e) {
    e.preventDefault();
    commit();
    render();
  };
  $("clear").onclick = function () {
    q = "";
    genre = "";
    $("query").value = "";
    status = "ready";
    render();
    $("query").focus();
  };
  $("surprise").onclick = function () {
    openCatalog(rows[Math.floor(Math.random() * rows.length)]);
  };
  $("more").onclick = function () {
    expanded = !expanded;
    render();
  };
  if (w === "biblio") {
    document.body.classList.add("biblio-search");
    document.querySelector("#genres").previousElementSibling.textContent =
      "BROWSE A GENRE";
    $("surprise").hidden = true;
    $("recent-title").textContent = "RECENT";
    var jump = document.createElement("section");
    jump.innerHTML =
      '<p class="section-label">JUMP TO</p><div class="suggestions" id="jump"><button>Home</button><button>Top 10</button><button>Genres</button></div>';
    $("recent").after(jump);
    Array.from($("jump").children).forEach(function (b, i) {
      b.onclick = function () {
        commit();
        if (i === 0) U.go("home");
        else U.back();
      };
    });
    var audio = document.createElement("section");
    audio.id = "audio-results";
    audio.innerHTML =
      '<p class="section-label" id="audio-label">AUDIOBOOKS</p><div id="audio-grid" class="grid"></div><div id="audio-state"></div>';
    $("results").appendChild(audio);
  }
  function audioResults(items) {
    if (w !== "biblio") return;
    $("audio-label").textContent = "♫ AUDIOBOOKS · " + items.length + " FOUND";
    $("audio-grid").hidden = audioStatus !== "ready";
    $("audio-state").hidden = audioStatus === "ready";
    if (audioStatus !== "ready") {
      $("audio-state").innerHTML =
        "<p>" +
        (audioStatus === "loading"
          ? "Searching audiobooks…"
          : "Audiobook search unavailable. Books remain available.") +
        '</p><button id="audio-retry" class="surprise">' +
        (audioStatus === "loading"
          ? "Show fixture results"
          : "Retry audiobooks") +
        "</button>";
      $("audio-retry").onclick = function () {
        audioStatus = "ready";
        render();
      };
      return;
    }
    $("audio-grid").innerHTML = items
      .map(function (c) {
        return (
          '<article class="card"><button class="open" aria-label="Open ' +
          esc(c.title) +
          ' audiobook"><img src="' +
          c.cover +
          '" alt="' +
          esc(c.title) +
          '"><span class="audio-mark">♫</span><h3>' +
          esc(c.title) +
          "</h3><small>" +
          (c.title === "Dune" ? "Frank Herbert" : "Andy Weir") +
          " · Audiobook</small></button></article>"
        );
      })
      .join("");
    Array.from($("audio-grid").children).forEach(function (el, i) {
      el.querySelector("button").onclick = function () {
        commit();
        var c = items[i];
        U.go("book.detail", {
          id: c.id,
          title: c.title,
          cover: c.cover,
          section: "audiobook",
          format: "audiobook",
        });
      };
    });
  }
  function render() {
    $("clear").hidden = !q;
    var result = rows.filter(function (c) {
      return (
        (!q || c.title.toLowerCase().includes(q.toLowerCase())) &&
        (!genre || c.genre === genre)
      );
    });
    if (status === "empty") result = [];
    var active = !!q || !!genre || status !== "ready";
    $("initial").hidden = active;
    $("results").hidden =
      !active || !result.length || status === "loading" || status === "error";
    $("empty").hidden = !active || !$("results").hidden;
    if (!active) {
      var h = U.get("search." + w + ".history", []);
      $("recent-title").hidden = !h.length;
      $("recent").innerHTML = h
        .map(function (t, i) {
          return (
            '<button data-query="' +
            i +
            '">' +
            esc(t) +
            '</button><button data-remove="' +
            i +
            '" aria-label="Remove ' +
            esc(t) +
            ' from history">×</button>'
          );
        })
        .join("");
      document.querySelectorAll("[data-query]").forEach(function (b) {
        b.onclick = function () {
          q = h[+b.dataset.query];
          $("query").value = q;
          render();
        };
      });
      document.querySelectorAll("[data-remove]").forEach(function (b) {
        b.onclick = function () {
          h.splice(+b.dataset.remove, 1);
          U.set("search." + w + ".history", h);
          render();
        };
      });
      $("genres").innerHTML = Array.from(
        new Set(
          rows.map(function (c) {
            return c.genre;
          }),
        ),
      )
        .map(function (t) {
          return "<button>" + esc(t) + "</button>";
        })
        .join("");
      Array.from($("genres").children).forEach(function (b) {
        b.onclick = function () {
          genre = b.textContent;
          render();
        };
      });
    }
    if (status === "loading" || status === "error") {
      $("empty").innerHTML =
        "<h2>" +
        (status === "loading" ? "Searching…" : "Search unavailable") +
        "</h2><p>" +
        (status === "loading"
          ? "Staged loading state"
          : "The staged provider returned an error. Your query is kept.") +
        '</p><button id="retry">' +
        (status === "loading" ? "Show fixture results" : "Retry") +
        "</button>";
      $("retry").onclick = function () {
        status = "ready";
        render();
      };
      return;
    }
    if (active && !result.length) {
      $("empty").innerHTML =
        "<h2>No " +
        (w === "biblio" ? "books" : "results") +
        ' found</h2><p>Try another title or browse a genre.</p><button id="retry">Clear search</button>';
      $("retry").onclick = function () {
        $("clear").click();
      };
      return;
    }
    if (active && result.length) {
      var c = result[0];
      $("match-label").textContent = genre
        ? "BROWSING · " + genre.toUpperCase()
        : "TOP MATCH";
      $("topmatch").innerHTML =
        '<img src="' +
        c.cover +
        '" alt="' +
        esc(c.title) +
        '"><div class="copy"><h2>' +
        esc(c.title) +
        "</h2><p>" +
        c.genre.toUpperCase() +
        " · " +
        c.year +
        " · " +
        c.rating +
        '</p></div><button id="open-top">Open ›</button>';
      $("open-top").onclick = function () {
        commit();
        openCatalog(c);
      };
      $("result-label").textContent =
        (w === "biblio"
          ? "BOOKS"
          : w === "tankoban"
            ? "MANGA & COMICS"
            : "MOVIES, SHOWS & ANIME") +
        " · " +
        result.length +
        " FOUND";
      var bookRows = w === "biblio" ? result.slice(1) : result;
      $("grid").innerHTML = bookRows
        .slice(0, expanded ? 100 : 6)
        .map(function (c) {
          return (
            '<article class="card"><button class="open"><img src="' +
            c.cover +
            '" alt="' +
            esc(c.title) +
            '"><h3>' +
            esc(c.title) +
            "</h3><small>" +
            c.genre +
            "</small></button></article>"
          );
        })
        .join("");
      Array.from($("grid").children).forEach(function (el, i) {
        el.querySelector("button").onclick = function () {
          commit();
          openCatalog(bookRows[i]);
        };
      });
      audioResults(result);
      $("more").hidden = bookRows.length <= 6;
      $("more").textContent = expanded ? "Show less ▴" : "See more ▾";
    }
  }
  render();
  $("query").focus();
  U.ready();
})();
