(function () {
  var U = MockUI,
    w =
      U.params.world ||
      new URLSearchParams(location.search).get("w") ||
      "tankoban",
    rows = MockCatalog[w],
    label = { tankoban: "Tankoban", biblio: "Biblio", theatre: "Theatre" }[w],
    tabs = {
      tankoban: ["Discover", "Manga", "Comics", "Library"],
      biblio: ["Discover", "Explore", "Library"],
      theatre: ["Discover", "Movies", "Shows", "Anime", "Library"],
    }[w];
  function $(id) {
    return document.getElementById(id);
  }
  function action(el, fn) {
    el.tabIndex = 0;
    el.setAttribute("role", "button");
    el.onclick = fn;
    el.onkeydown = function (e) {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        fn();
      }
    };
  }
  // The app switches tabs in place, under the tab bar (TankobanWorld.qml activeTab).
  // Tabs that have a catalogue built here do the same; the rest still open their own page.
  // ?tab=manga&y=600 opens a tab at a scroll position, for checking a page by address.
  var query = new URLSearchParams(location.search);
  var activeTab = query.get("tab") || U.get("world." + w + ".tab", "discover");
  function markTab() {
    ["flowTabs", "dockTabs"].forEach(function (id) {
      Array.from($(id).children).forEach(function (el) {
        el.classList.toggle("on", el.textContent.toLowerCase() === activeTab);
      });
    });
  }
  function routeTab(t) {
    var key = t.toLowerCase();
    if (key === "discover") {
      activeTab = key;
      WorldCatalogue.hide();
    } else if (WorldCatalogue.has(key)) {
      activeTab = key;
      WorldCatalogue.show(key);
    } else if (t === "Library") return U.go("library." + w);
    else if (t === "Explore") return U.go("biblio.explore");
    else return U.go("discover." + w, { world: w, tab: key });
    U.set("world." + w + ".tab", activeTab);
    markTab();
  }
  $("picker").textContent = label;
  $("contTitle").textContent =
    w === "theatre" ? "Continue Watching" : "Continue Reading";
  ["flowTabs", "dockTabs"].forEach(function (id) {
    $(id).innerHTML = tabs
      .map(function (t) {
        return "<span>" + t + "</span>";
      })
      .join("");
    $(id).style.width = 160 * tabs.length + "px";
    Array.from($(id).children).forEach(function (el, i) {
      action(el, function () {
        routeTab(tabs[i]);
      });
    });
  });
  $("lens").innerHTML = (
    w === "tankoban"
      ? ["Manga", "Comics"]
      : w === "theatre"
        ? ["Movies", "Shows", "Anime"]
        : ["Books"]
  )
    .map(function (t, i) {
      return '<span class="' + (!i ? "on" : "") + '">' + t + "</span>";
    })
    .join("");
  Array.from($("lens").children).forEach(function (el) {
    action(el, function () {
      routeTab(el.textContent);
    });
  });
  $("byline").textContent =
    w === "theatre" ? "Cinemeta" : label + " built-in catalogue";
  $("slides").innerHTML = rows
    .slice(0, 2)
    .map(function (c) {
      return (
        '<div class="slide poster-kind" style="background:linear-gradient(90deg,#4a3036,#111b20)"><div class="tint"></div><img class="poster" src="' +
        c.cover +
        '" alt=""><div class="film"></div><div class="copy"><div class="kick">Featured in ' +
        label +
        "</div><h1>" +
        c.title +
        "</h1><p>" +
        c.genre +
        " · " +
        c.year +
        " · " +
        c.rating +
        ' staged rating</p><div class="cta"><span class="p">' +
        (w === "theatre" ? "Watch" : "Read") +
        '</span><span class="s">Details</span></div></div></div>'
      );
    })
    .join("");
  Array.from($("slides").children).forEach(function (sl, i) {
    action(sl.querySelector(".p"), function () {
      openCatalog(rows[i], true);
    });
    action(sl.querySelector(".s"), function () {
      openCatalog(rows[i], false);
    });
  });
  $("dots").innerHTML = '<i class="on"></i><i></i>';
  var slide = 0;
  function change(i) {
    slide = (i + 2) % 2;
    $("slides").style.transform = "translateX(" + -50 * slide + "%)";
    Array.from($("dots").children).forEach(function (d, j) {
      d.classList.toggle("on", j === slide);
    });
  }
  Array.from($("dots").children).forEach(function (d, i) {
    action(d, function () {
      change(i);
    });
  });
  document
    .querySelector(".hero")
    .insertAdjacentHTML(
      "beforeend",
      '<div class="world-next"><button aria-label="Previous feature">‹</button><button aria-label="Next feature">›</button></div>',
    );
  document.querySelectorAll(".world-next button").forEach(function (b, i) {
    b.onclick = function () {
      change(slide + (i ? 1 : -1));
    };
  });
  var cont = rows.filter(function (c) {
    return c.progress;
  });
  $("tiles").innerHTML = cont
    .map(function (c) {
      return (
        '<div class="tile"><img src="' +
        c.cover +
        '" alt=""><div class="t">' +
        c.title +
        '</div><div class="s">' +
        c.progress +
        '% complete</div><div class="bar"><i style="width:' +
        c.progress +
        '%"></i></div></div>'
      );
    })
    .join("");
  Array.from($("tiles").children).forEach(function (el, i) {
    action(el, function () {
      openCatalog(cont[i], true);
    });
  });
  action(document.querySelector(".head span"), function () {
    U.go("library.continue", { world: w });
  });
  $("grid").innerHTML = rows
    .map(function (c) {
      return (
        '<div class="card"><div class="cov"><img src="' +
        c.cover +
        '" alt="' +
        c.title +
        '"></div><p>' +
        c.title +
        "</p></div>"
      );
    })
    .join("");
  Array.from($("grid").children).forEach(function (el, i) {
    action(el, function () {
      openCatalog(rows[i]);
    });
  });
  action(document.querySelector(".filter span"), function () {
    U.go("discover." + w, { world: w, filtersOpen: true });
  });
  action(document.querySelector(".now b"), function () {
    U.go("discover." + w, { world: w });
  });
  action(document.querySelector(".side"), function () {
    U.go("discover." + w, { world: w, sidebar: true });
  });
  $("grid").insertAdjacentHTML(
    "afterend",
    '<button class="mock-view-link" data-action="discover">Browse all ' +
      label +
      " titles and filters</button>",
  );
  document.querySelector("[data-action=discover]").onclick = function () {
    U.go("discover." + w, { world: w });
  };
  if (!WorldCatalogue.has(activeTab)) activeTab = "discover";
  if (activeTab !== "discover") WorldCatalogue.show(activeTab);
  markTab();
  $("page").scrollTop = query.get("y") ? +query.get("y") : U.get("world." + w + ".scroll", 0);
  $("page").onscroll = function () {
    U.set("world." + w + ".scroll", $("page").scrollTop);
  };
  U.ready();
})();
