/* The world's catalogue tabs, shown in place under the tab bar as the app does.
   Source (Colosseum master 2916fe85):
     Tankoban  qml/TankobanMangaTab.qml, TankobanComicsTab.qml, the shelf list in TankobanWorld.qml
     Theatre   qml/TheatreCatalogPage.qml, TheatreCatalogRules.js (row lists), PosterRail.qml,
               LazyPosterShelf.qml, CataloguePosterCard.qml, TheatreGenreApi.js (genre names)
     Biblio    qml/BiblioExplorePage.qml, BiblioBookRail.qml, BiblioExploreRules.js (row order)
     shared    RowIndexSidebar.qml, TrendingTop10.qml, PortraitTile.qml, GenreMosaic.qml,
               ContinueRow.qml, WidgetHeader.qml, CatalogueVisualMetrics.js
   Shelf contents, covers and counts are staged; shelf names and order are the app's. */
(function () {
  var U = window.MockUI;
  var w = (U && U.params && U.params.world) || new URLSearchParams(location.search).get("w") || "tankoban";
  var all = window.MockCatalog[w] || [];
  var page = document.getElementById("page");
  var discover = document.querySelector(".discover");
  var worldName = { tankoban: "Tankoban", biblio: "Biblio", theatre: "Theatre" }[w];

  var host = document.createElement("div");
  host.className = "cattab";
  host.hidden = true;
  discover.insertAdjacentElement("afterend", host);

  function ofKind(kind) { return all.filter(function (c) { return c.kind === kind; }); }
  // A shelf of n items from a short staged list, starting at a different title each time.
  function fill(list, n, from) {
    var out = [];
    for (var i = 0; i < n && list.length; i++) out.push(list[(i + from) % list.length]);
    return out;
  }
  function rotate(list, by) { return list.map(function (_, i) { return list[(i + by) % list.length]; }); }

  /* RowIndexSidebar.qml: a row's icon is chosen from its title, in this order. */
  function iconPaths(title) {
    var t = String(title || "").toLowerCase();
    if (/collection|library|shelf|shelves/.test(t)) return '<path d="M4 4v16h5V4zM9 6h5v14H9zM16 4l4 15-4 1-4-15z"/>';
    if (/top|rated|popular|best/.test(t)) return '<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>';
    if (/recent|new|available|release|continue/.test(t)) return '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>';
    if (/explore|discover|genre/.test(t)) return '<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5z"/>';
    if (/edition|omnibus|omnibuses|complete|run/.test(t)) return '<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M9 7h6M9 11h6M9 15h4"/>';
    return '<path d="M4 6h16M4 12h16M4 18h16"/>';
  }

  function header(title, more) {
    return '<div class="head"><h2>' + title + "</h2>" + (more ? "<span>" + more + " <b>&rsaquo;</b></span>" : "") + "</div>";
  }

  /* ContinueRow with showSeeAll false: the titles the user keeps. */
  function collection(items) {
    return '<section class="shelf" data-title="Your Collection">' + header("Your Collection", "") + '<div class="tiles">' +
      items.map(function (c) {
        return '<div class="tile" data-id="' + c.id + '"><img src="' + c.cover + '" alt=""><div class="t">' + c.title +
          '</div><div class="s">' + c.progress + '% complete</div><div class="bar"><i style="width:' + c.progress + '%"></i></div></div>';
      }).join("") + "</div></section>";
  }

  /* TrendingTop10: a 132px ghost numeral with the cover overlapping it by 32px. */
  function ranked(title, items, more) {
    return '<section class="shelf" data-title="' + title + '">' + header(title, more ? "Explore" : "") + '<div class="ranks">' +
      items.map(function (c, i) {
        return '<div class="rank" data-id="' + c.id + '"><b>' + (i + 1) + '</b><div class="ptile"><img src="' + c.cover +
          '" alt=""><span>' + c.title + "</span></div></div>";
      }).join("") + "</div></section>";
  }

  /* GenreMosaic: five columns of 112px tiles, cover dimmed to 0.46 behind the name and count. */
  function mosaic(title, genres, covers, more) {
    return '<section class="shelf" data-title="' + title + '">' + header(title, more ? "Explore" : "") + '<div class="mosaic">' +
      genres.map(function (g, i) {
        return '<div class="genre" style="background:linear-gradient(90deg,' + g[2] + "," + g[3] + ')"><img src="' +
          covers[i % covers.length].cover + '" alt="">' + (g[1] === "" ? "" : "<em>" + g[1] + "</em>") + "<strong>" + g[0] + "</strong></div>";
      }).join("") + "</div></section>";
  }

  /* PosterRail (gallery profile): 148x222 posters 20px apart, two-line title beneath.
     A ranked rail puts the same ghost numeral beside each poster. */
  function rail(title, items, ranked) {
    return '<section class="shelf" data-title="' + title + '">' + header(title, "See all") +
      '<div class="posters' + (ranked ? " ranked" : "") + '">' +
      items.map(function (c, i) {
        return '<div class="pcard" data-id="' + c.id + '">' + (ranked ? "<b>" + (i + 1) + "</b>" : "") +
          '<div class="pcov"><img src="' + c.cover + '" alt=""></div><p>' + c.title + "</p></div>";
      }).join("") + "</div></section>";
  }

  /* BiblioBookRail: 130x182 covers, title and author always shown, "See All" on the right. */
  function bookRail(title, books, ranked) {
    return '<section class="shelf" data-title="' + title + '"><div class="bhead"><h2>' + title + "</h2><span>See All</span></div>" +
      '<div class="books' + (ranked ? " ranked" : "") + '">' +
      books.map(function (b, i) {
        return '<div class="bcard">' + (ranked ? "<b>" + (i + 1) + "</b>" : "") +
          '<div class="bcov"><img src="' + b[2] + '" alt="" onerror="this.remove()"></div><p>' + b[0] + "</p><small>" + b[1] + "</small></div>";
      }).join("") + "</div></section>";
  }

  /* BiblioExplorePage's closing tiles: one wide panel of up to seven covers per facet. */
  function facet(title, books) {
    return '<section class="shelf facet" data-title="' + title + '"><div class="fcovers">' +
      books.slice(0, 7).map(function (b) {
        return '<div><img src="' + b[2] + '" alt="" onerror="this.remove()"></div>';
      }).join("") + "</div><strong>" + title + "</strong></section>";
  }

  function link(text) { return '<div class="customise"><span>' + text + "</span></div>"; }

  var PLUM = ["#532f49", "#1d121b"];
  var TONES = [["#5d4633", "#18110c"], ["#33445d", "#0c1118"], ["#5b3a64", "#170d1b"],
               ["#3f5640", "#111b12"], ["#5a3a3f", "#160d0b"], ["#3c4a63", "#0e121b"]];

  // TheatreCatalogRules.js: the house rows of each page, in order. The first is the ranked Top 10.
  var THEATRE_ROWS = {
    movies: ["Top 10 on Movies", "Recently Released", "Top Rated", "Hidden Gems", "Cult Classics", "Under Two Hours",
             "Documentary Movies", "Animated Movies", "International Cinema", "Japanese Cinema", "Korean Cinema", "French Cinema",
             "2020s Movies", "2010s Movies", "2000s Movies", "1990s Movies", "1980s Movies", "1970s Movies"],
    shows: ["Top 10 on Shows", "Currently Airing", "Recently Premiered", "Top Rated", "Hidden Gems", "Cult Classics",
            "Long-Running Series", "Limited Series", "Drama Series", "Comedy Series", "Crime and Mystery",
            "Science Fiction and Fantasy", "Documentary Series", "Animated Series", "Korean Drama"],
    anime: ["Top 10 on Anime", "Trending", "Airing Now", "Top Airing", "Upcoming Season", "Top Series", "Top Anime Movies",
            "Most Popular", "Top Rated", "Hidden Gems", "2020s Anime", "2010s Anime", "2000s Anime", "1990s and Earlier",
            "Action and Adventure", "Romance", "Slice of Life", "Mecha", "Fantasy", "Science Fiction", "Psychological",
            "Horror and Supernatural"]
  };
  // TheatreGenreApi.js: the genre box of each page.
  var THEATRE_GENRES = {
    movies: ["Movie Genres", ["Action", "Drama", "Comedy", "Sci-Fi", "Thriller", "Horror", "Romance", "Animation",
                              "Adventure", "Crime", "Mystery", "Fantasy", "Documentary"]],
    shows: ["Show Genres", ["Drama", "Comedy", "Crime", "Sci-Fi", "Thriller", "Mystery", "Action", "Animation",
                            "Adventure", "Fantasy", "Documentary", "Romance", "Horror"]],
    anime: ["Anime Genres", ["Action", "Adventure", "Comedy", "Drama", "Fantasy", "Horror", "Mystery", "Romance",
                             "Sci-Fi", "Slice of Life", "Sports", "Supernatural"]]
  };
  function theatrePage(kind) {
    return function () {
      var items = ofKind(kind), rows = THEATRE_ROWS[kind], genres = THEATRE_GENRES[kind];
      return link("Customize rows") +
        rows.map(function (title, i) { return rail(title, fill(items, i ? 8 : 10, i), !i); }).join("") +
        mosaic(genres[0], genres[1].map(function (name, i) { return [name, "", TONES[i % TONES.length][0], TONES[i % TONES.length][1]]; }), items, true);
    };
  }

  // Staged books: two covers held by the mock, the rest at the addresses the app itself uses (qml/Catalog.js biblioTop).
  var MZ = "https://is1-ssl.mzstatic.com/image/thumb/";
  function mz(path) { return MZ + path + "/400x600bb.jpg"; }
  var BOOKS = [
    ["Dune", "Frank Herbert", "../assets/feria/assets/dune-book.jpg"],
    ["Project Hail Mary", "Andy Weir", "../assets/feria/assets/phm-cover.jpg"],
    ["Regime Change", "Maggie Haberman", mz("Publication211/v4/f6/f5/a5/f6f5a534-5079-7881-13fb-6151f78a806b/9781668067260.jpg")],
    ["Aquarius", "Gemma James", mz("Publication116/v4/71/33/b9/7133b9b6-a782-fd0f-8848-302f2a5cb413/0000715555.jpg")],
    ["Whistler", "Ann Patchett", mz("Publication221/v4/e1/f4/a5/e1f4a502-9136-f220-0c92-facdfaa78e58/9780063511651.jpg")],
    ["The Calamity Club", "Kathryn Stockett", mz("Publication211/v4/39/64/45/3964459f-f5f3-d0b9-8623-d792b4ee584d/9781954118829.jpg")],
    ["Choke Point", "Brad Thor", mz("Publication211/v4/25/2d/e6/252de6b6-a031-1e18-caed-049078aab004/9781668065945.jpg")],
    ["Theo of Golden", "Allen Levi", mz("Publication211/v4/ca/94/ee/ca94ee35-57c1-9f26-2a89-7530f60797ac/9781668236536.jpg")],
    ["Weddings", "Danielle Steel", mz("Publication221/v4/1f/f9/e2/1ff9e2eb-50a2-aa1c-3816-3c6971f9e712/9780593973127.d.jpg")],
    ["The Score", "Elle Kennedy", mz("Publication221/v4/a5/e3/9d/a5e39da7-be49-daf3-2991-8c89357cf5d3/0000128008.jpg")]
  ];

  var PALETTE = [["#3f5a78", "#16222e"], ["#78503f", "#2e1c16"], ["#5a3f78", "#241630"], ["#3f785a", "#16281e"],
                 ["#78703f", "#2e2a16"], ["#783f5a", "#301624"], ["#3f6478", "#16242e"], ["#785a3f", "#2e2216"]];

  var TABS = {
    tankoban: {
      manga: function () {
        var manga = ofKind("manga");
        var genres = [["Shounen", 1240], ["Seinen", 680], ["Romance", 920], ["Action", 1510], ["Comedy", 1100], ["Drama", 870],
                      ["Shoujo", 540], ["Josei", 210], ["Isekai", 430], ["School", 360], ["Magic", 290]]
          .map(function (g) { return [g[0], g[1], PLUM[0], PLUM[1]]; });
        return collection(manga.filter(function (c) { return c.progress; })) +
          ranked("Top in Tankoban — Manga", manga, true) +
          mosaic("Explore by Genre — Manga", genres, manga, true);
      },
      comics: function () {
        var comics = ofKind("comics");
        // TankobanWorld.qml: the catalogue shelves, in this order. The last four have no Explore link.
        var shelves = [["Recently Available", 1], ["Complete Runs", 1], ["Near Complete", 1], ["Omnibuses", 1], ["Deluxe Editions", 1],
                       ["Graphic Novels", 1], ["Community Collections", 1], ["Most Stocked", 1], ["Marvel", 1], ["DC", 1], ["Image", 1],
                       ["The 2020s", 0], ["The 2010s", 0], ["Deep Shelves", 0], ["Fan-Made Shelf", 0]];
        var boxes = [["Superhero", 418], ["Science Fiction", 206], ["Fantasy", 188], ["Horror", 143],
                     ["Crime", 131], ["Action", 127], ["Comedy", 97], ["Drama", 84]]
          .map(function (g, i) { return [g[0], g[1], PALETTE[i % PALETTE.length][0], PALETTE[i % PALETTE.length][1]]; });
        return collection(comics.filter(function (c) { return c.progress; })) +
          ranked("Top in Tankoban — Comics", comics, true) +
          shelves.map(function (s, i) { return ranked(s[0], rotate(comics, i + 1), !!s[1]); }).join("") +
          mosaic("Explore Comics", boxes, comics, false);
      }
    },
    theatre: { movies: theatrePage("movies"), shows: theatrePage("shows"), anime: theatrePage("anime") },
    biblio: {
      // BiblioExploreRules.js: Top 10 first, then the six house rails in this order; then the three facet tiles.
      explore: function () {
        var rails = ["Popular", "Top Rated", "New Releases", "Trending", "Most Read", "Classics"];
        return '<div class="explorehead"><h2>Explore</h2><span>Customize shelves</span></div>' +
          bookRail("Top 10", fill(BOOKS, 10, 0), true) +
          rails.map(function (title, i) { return bookRail(title, fill(BOOKS, 8, i + 2), false); }).join("") +
          facet("Fiction", fill(BOOKS, 7, 1)) + facet("Nonfiction", fill(BOOKS, 7, 4)) + facet("Audience", fill(BOOKS, 7, 6));
      }
    }
  };

  var sections = [], rowButtons = [];

  function track() {
    if (host.hidden || !sections.length) return;
    var top = page.getBoundingClientRect().top;
    var line = 58 + 14 + 78 + 4;                 // under the pinned tab bar, then RowIndexSidebar's currentOffset + epsilon
    var current = 0;
    sections.forEach(function (s, i) { if (s.getBoundingClientRect().top - top <= line) current = i; });
    if (page.scrollTop + page.clientHeight >= page.scrollHeight - 2) current = sections.length - 1;
    rowButtons.forEach(function (b, i) { b.classList.toggle("cur", i === current); });
  }

  function show(tab) {
    var build = TABS[w] && TABS[w][tab];
    if (!build) return false;
    discover.hidden = true;
    host.hidden = false;
    var closed = !U || U.get("world." + w + ".indexClosed", true);      // the index starts retracted
    host.innerHTML =
      '<aside class="rowindex' + (closed ? " closed" : "") + '">' +
        '<button class="ri-toggle" aria-label="Expand or retract the row index"><svg viewBox="0 0 24 24"><path d="M14 6l-6 6 6 6"/></svg></button>' +
        '<div class="ri-brand"><small>WORLD</small><b>' + worldName + "</b></div>" +
        '<div class="ri-list"></div>' +
      "</aside>" +
      '<div class="catmain ' + w + '">' + build() + "</div>";
    var rail = host.querySelector(".rowindex"), list = host.querySelector(".ri-list");
    sections = Array.from(host.querySelectorAll(".shelf"));
    rowButtons = sections.map(function (s) {
      var b = document.createElement("button");
      b.className = "ri-row";
      b.title = s.dataset.title;
      b.innerHTML = '<svg viewBox="0 0 24 24">' + iconPaths(s.dataset.title) + "</svg><span>" + s.dataset.title + "</span>";
      b.onclick = function () {                  // park the row just under the pinned tab bar
        page.scrollTo({ top: page.scrollTop + s.getBoundingClientRect().top - page.getBoundingClientRect().top - 58 - 14, behavior: "smooth" });
      };
      list.appendChild(b);
      return b;
    });
    host.querySelector(".ri-toggle").onclick = function () {
      var nowClosed = rail.classList.toggle("closed");
      if (U) U.set("world." + w + ".indexClosed", nowClosed);
    };
    host.querySelectorAll("[data-id]").forEach(function (el) {
      var item = all.filter(function (c) { return String(c.id) === el.dataset.id; })[0];
      if (!item || !window.openCatalog) return;
      el.onclick = function () { window.openCatalog(item, el.classList.contains("tile")); };
    });
    track();
    return true;
  }

  function hide() {
    host.hidden = true;
    host.innerHTML = "";
    sections = []; rowButtons = [];
    discover.hidden = false;
  }

  page.addEventListener("scroll", track, { passive: true });
  window.WorldCatalogue = { has: function (tab) { return !!(TABS[w] && TABS[w][tab]); }, show: show, hide: hide };
})();
