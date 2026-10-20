(function () {
  var U = MockUI;
  function action(el, fn, label) {
    if (!el) return;
    el.tabIndex = 0;
    el.setAttribute("role", "button");
    if (label) el.setAttribute("aria-label", label);
    el.onclick = fn;
    el.onkeydown = function (e) {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        fn(e);
      }
    };
  }
  action(document.querySelector(".explore"), function () {
    U.go("universe.shelves");
  });
  action(document.querySelector(".world-count"), function () {
    U.go("universes");
  });
  action(document.querySelector(".head span"), function () {
    U.go("library.continue");
  });
  var items = [
    MockCatalog.theatre[0],
    MockCatalog.tankoban[0],
    MockCatalog.biblio[0],
    MockCatalog.tankoban[7],
  ];
  document.querySelectorAll(".tile").forEach(function (t, i) {
    action(
      t,
      function () {
        openCatalog(items[i], true);
      },
      "Resume " + items[i].title,
    );
    var remove = t.querySelector(".remove");
    action(
      remove,
      function (e) {
        e.stopPropagation();
        U.set("home.hidden." + items[i].id, true);
        t.remove();
      },
      "Remove " + items[i].title + " from Continue",
    );
    if (U.get("home.hidden." + items[i].id, false)) t.remove();
  });
  var worldRoutes = [
    "world.tankoban",
    "world.theatre",
    "world.biblio",
    "vault",
  ];
  document.querySelectorAll(".widget").forEach(function (w, i) {
    action(w, function () {
      U.go(worldRoutes[i]);
    });
  });
  var hero = document.querySelector(".hero"),
    slide = 0,
    slides = [
      {
        title: "One Piece",
        cover: "../assets/universes/one-piece/east-blue/banners/romance.png",
        route: "universe.shelves",
      },
      {
        title: "Dune",
        cover: "../assets/wallpaper/sagas/dune.jpg",
        route: "universe.saga",
      },
    ];
  hero.insertAdjacentHTML(
    "beforeend",
    '<div class="home-actions"><button aria-label="Previous featured universe">‹</button><button aria-label="Next featured universe">›</button></div>',
  );
  hero.querySelectorAll("button").forEach(function (b, i) {
    b.onclick = function () {
      slide = (slide + (i ? 1 : slides.length - 1)) % slides.length;
      hero.querySelector("h1").textContent = slides[slide].title;
      hero.querySelector(".art").src = slides[slide].cover;
      action(hero.querySelector(".explore"), function () {
        U.go(slides[slide].route);
      });
    };
  });
  var page = document.querySelector(".page");
  page.scrollTop = U.get("home.scroll", 0);
  page.addEventListener("scroll", function () {
    U.set("home.scroll", page.scrollTop);
  });
  U.ready();
})();
