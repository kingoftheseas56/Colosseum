/* Browser translation of current ComicReaderShell. Six HUD commands, independent
   layout/order, one temporary overlay, original local SVG narrative sample pages. */
(() => {
  "use strict";
  const M = MockUI,
    P = M.params || {},
    $ = (s) => document.querySelector(s),
    $$ = (s) => [...document.querySelectorAll(s)],
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
  let title = P.title || "One Piece",
    kind = P.kind || "chapter",
    entry = Number(P.entry) || 1,
    mode = P.mode || (P.kind === "comics" ? "comic" : "manga"),
    max = 12,
    layout = P.layout || "paired_pages",
    order = mode === "comic" ? "ltr" : "rtl",
    page =
      Number(P.page) ||
      M.get("reader.resume." + mode + "-" + kind + "-" + entry, 1),
    bookmarks = M.get("reader.bookmarks." + title, []),
    active = "",
    dispose = null,
    returnFocus = null,
    width = 78,
    gap = 0,
    zoom = 100,
    veil = "off",
    gutter = 0.22,
    coupling = "auto",
    memory = false,
    auto = false,
    speed = 1,
    raf = 0,
    last = 0,
    profile = {
      quality: "balanced",
      brightness: 0,
      contrast: 0,
      gamma: 100,
      rotation: 0,
      nightFilter: false,
      autoCrop: false,
    };
  let state = P.state || "",
    hud = true,
    loupeScale = 2;
  const prefs = M.get("reader.prefs." + title, null);
  if (prefs) {
    ({ layout, order, width, gap, zoom, veil, gutter, coupling, memory } =
      Object.assign(
        { layout, order, width, gap, zoom, veil, gutter, coupling, memory },
        prefs,
      ));
  }
  if (P.layout) layout = P.layout;
  if (mode === "strip") layout = "long_strip";
  page = Math.max(1, Math.min(max, page));
  $("#layout-popover").remove();
  $("#pages-overlay").remove();
  $(".scrub").remove();
  $(".side-scroll").style.pointerEvents = "none";
  function buttonize(el, action, label, fn) {
    let b = document.createElement("button");
    b.className = el.className;
    b.id = el.id;
    b.innerHTML = el.innerHTML;
    b.dataset.action = action;
    b.setAttribute("aria-label", label);
    el.replaceWith(b);
    b.onclick = fn;
    return b;
  }
  buttonize($(".back"), "back", "Back to series", () => leave(false));
  buttonize($(".book-label"), "navigator", "Chapter navigator", () =>
    open("navigator"),
  );
  $$(".window-verbs span").forEach((e, i) =>
    buttonize(
      e,
      ["minimize", "fullscreen", "close"][i],
      ["Minimize reader", "Toggle fullscreen", "Close reader"][i],
      () => {
        if (i === 0) {
          M.openSession({
            kind: "reader",
            id: sessionId(),
            title: title + " · " + entryLabel(),
            route: "reader.comic",
            params: { ...P, entry, page, layout },
          });
          M.back();
        } else if (i === 1)
          document.documentElement.requestFullscreen?.().catch(() => {});
        else open("close");
      },
    ),
  );
  const commands = ["bookmark", "pages", "loupe", "image", "layout", "order"];
  $$(".cmd").forEach((el, i) =>
    buttonize(
      el,
      commands[i],
      ["Toggle bookmark", "Pages", "Loupe", "Image", "Layout", "Reading order"][
        i
      ],
      () => {
        let c = commands[i];
        if (c === "bookmark") toggleBookmark();
        else if (c === "order") {
          order = order === "rtl" ? "ltr" : "rtl";
          render();
          savePrefs();
        } else open(c);
      },
    ),
  );
  buttonize($(".nav.left"), "left-page", "Turn page left", () =>
    turn(order === "rtl" ? 1 : -1),
  );
  buttonize($(".nav.right"), "right-page", "Turn page right", () =>
    turn(order === "rtl" ? -1 : 1),
  );
  buttonize($(".rail-arrow.prev"), "prev-entry", "Previous chapter", () =>
    changeEntry(-1),
  );
  buttonize($(".rail-arrow.next"), "next-entry", "Next chapter", () =>
    changeEntry(1),
  );
  let slider = document.createElement("input");
  slider.type = "range";
  slider.min = 1;
  slider.max = max;
  slider.value = page;
  slider.setAttribute("aria-label", "Reading progress");
  slider.dataset.action = "seek";
  slider.oninput = (e) => seek(Number(e.target.value));
  $(".rail").append(slider);
  $(".count.total").textContent = max;
  const sample = document.createElement("div");
  sample.className = "sample-label";
  sample.textContent = "Original sample comic · not scans from " + title;
  $("#stage").append(sample);
  let veilEl = document.createElement("div");
  veilEl.className = "reader-veil";
  $("#stage").append(veilEl);
  $("#spread").removeAttribute("aria-hidden");
  $("#strip").removeAttribute("aria-hidden");
  function entryLabel() {
    return (
      (kind === "volume"
        ? "Vol. "
        : kind === "issue"
          ? "Issue #"
          : "Chapter ") + entry
    );
  }
  function sessionId() {
    return mode + "-" + kind + "-" + entry;
  }
  function save() {
    M.set("reader.resume." + sessionId(), page);
    M.set(
      "reader.progress." + title,
      page + (layout === "paired_pages" ? 1 : 0) >= max
        ? "completed"
        : "reading",
    );
  }
  function savePrefs() {
    M.set("reader.prefs." + title, {
      layout,
      order,
      width,
      gap,
      zoom,
      veil,
      gutter,
      coupling,
      memory,
    });
  }
  function img(n) {
    return (
      '<img class="sample-page" data-page="' +
      n +
      '" src="comic-sample-' +
      n +
      '.svg" alt="Signal at Dawn, original sample page ' +
      n +
      '">'
    );
  }
  $("#stage").insertAdjacentHTML(
    "beforeend",
    '<svg width="0" height="0" aria-hidden="true"><filter id="sample-gamma"><feComponentTransfer><feFuncR type="gamma" amplitude="1" exponent="1" offset="0"/><feFuncG type="gamma" amplitude="1" exponent="1" offset="0"/><feFuncB type="gamma" amplitude="1" exponent="1" offset="0"/></feComponentTransfer></filter></svg>',
  );
  function applyImage() {
    document
      .querySelectorAll("#sample-gamma feComponentTransfer>*")
      .forEach((el) => el.setAttribute("exponent", 100 / profile.gamma));
    const filter = `brightness(${Math.max(0.05, 1 + profile.brightness / 100)}) contrast(${Math.max(0.05, 1 + profile.contrast / 100)}) ${profile.nightFilter ? "sepia(.45)" : ""} url(#sample-gamma)`;
    $$(".sample-page").forEach((el) => {
      el.style.filter = filter;
      el.style.transform =
        "scale(" +
        (layout === "long_strip" ? 1 : zoom / 100) +
        ") rotate(" +
        profile.rotation +
        "deg)";
      el.style.imageRendering =
        profile.quality === "fast" ? "pixelated" : "auto";
      el.style.objectFit = profile.autoCrop ? "cover" : "contain";
    });
    veilEl.style.opacity =
      veil === "high" ? ".38" : veil === "low" ? ".18" : "0";
  }
  function render() {
    let strip = layout === "long_strip";
    $("#spread").style.display = strip ? "none" : "flex";
    $("#strip").style.display = strip ? "block" : "none";
    $("#strip").style.opacity = "1";
    $("#spread").classList.toggle("single", layout === "single_page");
    if (strip) {
      $("#strip-stack").innerHTML = Array.from({ length: max }, (_, i) =>
        img(i + 1),
      ).join("");
      $("#strip-stack").style.width = width + "%";
      $("#strip-stack").style.gap = gap + "px";
    } else {
      let pair = layout === "paired_pages",
        nums = pair
          ? [page, Math.min(max, page + 1)].filter(
              (x, i, a) => a.indexOf(x) === i,
            )
          : [page];
      if (pair && order === "rtl") nums.reverse();
      $("#spread").innerHTML = nums.map(img).join("");
      if (pair) $("#spread").style.gap = gutter * 8 + "px";
    }
    if (state === "error" || state === "loading") {
      let target = strip ? $("#strip-stack") : $("#spread");
      target.innerHTML =
        '<div class="page-error"><h2>' +
        (state === "loading" ? "Loading page…" : "Page could not be loaded") +
        "</h2><p>Original sample page " +
        page +
        '</p><button data-action="retry-page">' +
        (state === "loading" ? "Finish fixture loading" : "Retry page") +
        '</button><button data-action="skip-page">Skip page</button></div>';
      target.querySelector("[data-action=retry-page]").onclick = () => {
        state = "";
        render();
      };
      target.querySelector("[data-action=skip-page]").onclick = () => {
        state = "";
        seek(Math.min(max, page + 1));
      };
    }
    $(".book-label").textContent = title + " · " + entryLabel();
    $(".title-toast").textContent = title;
    $("#currentCount").textContent =
      layout === "paired_pages" && page < max ? page + "–" + (page + 1) : page;
    slider.value = page;
    $("[data-action=prev-entry]").disabled = entry <= 1;
    $("[data-action=order]").innerHTML =
      '<svg class="icon"><use href="' +
      (order === "rtl" ? "#i-rtl" : "#i-next") +
      '"/></svg>' +
      (order === "rtl" ? "Manga order" : "Comic order");
    $("#layoutLabel").textContent = {
      single_page: "Single page",
      paired_pages: "Paired pages",
      long_strip: "Long strip",
    }[layout];
    $("#layoutIcon").setAttribute(
      "href",
      {
        single_page: "#i-single",
        paired_pages: "#i-paired",
        long_strip: "#i-strip",
      }[layout],
    );
    $$(".nav").forEach((b) => (b.style.display = strip ? "none" : "grid"));
    $("[data-action=bookmark]").classList.toggle(
      "active",
      bookmarks.includes(page),
    );
    $("[data-action=bookmark]").setAttribute(
      "aria-pressed",
      String(bookmarks.includes(page)),
    );
    applyImage();
    save();
    if (strip && page > 1)
      requestAnimationFrame(() => {
        let el = $('#strip-stack [data-page="' + page + '"]');
        if (el) $("#strip").scrollTop = el.offsetTop;
      });
  }
  function seek(n) {
    page = Math.max(1, Math.min(max, n));
    if (layout === "paired_pages")
      page = Math.max(1, page - (page % 2 === 0 ? 1 : 0));
    render();
  }
  function turn(delta) {
    if (active) return;
    pause();
    let step = layout === "paired_pages" ? 2 : 1;
    seek(page + delta * step);
  }
  function changeEntry(delta) {
    close();
    pause();
    entry = Math.max(1, entry + delta);
    page = 1;
    render();
  }
  function toggleBookmark() {
    bookmarks = bookmarks.includes(page)
      ? bookmarks.filter((n) => n !== page)
      : bookmarks.concat(page).sort((a, b) => a - b);
    M.set("reader.bookmarks." + title, bookmarks);
    $("[data-action=bookmark]").classList.toggle(
      "active",
      bookmarks.includes(page),
    );
    $("[data-action=bookmark]").setAttribute(
      "aria-pressed",
      String(bookmarks.includes(page)),
    );
    M.toast(bookmarks.includes(page) ? "Bookmark added" : "Bookmark removed");
  }
  function leave(closeSession) {
    save();
    if (closeSession) M.closeSession(sessionId());
    if (P.sourceReturn)
      M.go(mode === "comic" ? "comics.series" : "manga.series", {
        title,
        volume: entry,
        mode: "volumes",
      });
    else M.back();
  }
  function close() {
    document
      .querySelectorAll(
        ".reader-panel,.reader-scrim,.loupe,.loupe-info,#pages-overlay",
      )
      .forEach((el) => el.remove());
    dispose?.();
    dispose = null;
    active = "";
    returnFocus?.focus();
    $$(".cmd").forEach((b) => {
      if (b.dataset.action !== "bookmark") b.classList.remove("active");
    });
  }
  function shell(type, heading) {
    close();
    pause();
    returnFocus = document.activeElement;
    active = type;
    let scrim = document.createElement("div");
    scrim.className =
      "reader-scrim" +
      (["close", "clear", "reset", "pairing"].includes(type) ? " modal" : "");
    scrim.onclick = close;
    $("#stage").append(scrim);
    let panel = document.createElement("section");
    panel.className =
      "reader-panel" +
      (type === "settings"
        ? " settings"
        : [
              "close",
              "clear",
              "reset",
              "pairing",
              "shortcuts",
              "navigator",
              "bookmarks",
              "goto",
            ].includes(type)
          ? " center"
          : "");
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-label", heading);
    panel.innerHTML =
      '<div class="panel-head"><h2>' +
      heading +
      '</h2><button aria-label="Close ' +
      heading +
      '" data-action="close-overlay">×</button></div><div class="panel-content"></div>';
    $("#stage").append(panel);
    if (["image", "layout"].includes(type)) {
      let r = $("[data-action=" + type + "]").getBoundingClientRect();
      panel.style.left =
        Math.max(12, Math.min(innerWidth - 334, r.left + r.width / 2 - 161)) +
        "px";
      panel.style.right = "auto";
    }
    panel.querySelector("[data-action=close-overlay]").onclick = close;
    dispose = M.registerOverlay(panel, close);
    panel.querySelector("button").focus();
    panel.addEventListener("keydown", (e) => {
      if (e.key === "Tab") {
        let a = [...panel.querySelectorAll("button,input,select")].filter(
            (x) => !x.disabled && x.offsetParent !== null,
          ),
          i = a.indexOf(document.activeElement);
        if (e.shiftKey && i === 0) {
          e.preventDefault();
          a.at(-1)?.focus();
        } else if (!e.shiftKey && i === a.length - 1) {
          e.preventDefault();
          a[0]?.focus();
        }
      }
    });
    $('[data-action="' + type + '"]')?.classList.add("active");
    return panel.querySelector(".panel-content");
  }
  function choices(el, label, values, current, fn) {
    let row = document.createElement("div");
    row.className = "setting-row";
    row.innerHTML = "<span>" + label + '</span><span class="chips"></span>';
    values.forEach((v) => {
      let val = Array.isArray(v) ? v[0] : v,
        text = Array.isArray(v)
          ? v[1]
          : String(v).charAt(0).toUpperCase() + String(v).slice(1),
        b = document.createElement("button");
      b.className = "chip" + (val === current ? " on" : "");
      b.textContent = text;
      b.dataset.action = label.toLowerCase().replace(/ /g, "-") + "-" + val;
      b.setAttribute("aria-pressed", String(val === current));
      b.onclick = () => {
        fn(val);
        [...row.querySelectorAll("button")].forEach((x) => {
          x.classList.toggle("on", x === b);
          x.setAttribute("aria-pressed", String(x === b));
        });
      };
      row.querySelector(".chips").append(b);
    });
    el.append(row);
  }
  function range(el, label, value, min, max, fn) {
    let row = document.createElement("label");
    row.innerHTML =
      '<span class="setting-row"><span>' +
      label +
      "</span><output>" +
      value +
      '</output></span><input type="range" min="' +
      min +
      '" max="' +
      max +
      '" value="' +
      value +
      '" aria-label="' +
      label +
      '">';
    row.querySelector("input").oninput = (e) => {
      let v = Number(e.target.value);
      row.querySelector("output").value = v;
      fn(v);
    };
    el.append(row);
  }
  function tool(el, text, fn, action) {
    let b = document.createElement("button");
    b.className = "full";
    b.textContent = text;
    b.dataset.action = action || text.toLowerCase().replace(/ /g, "-");
    b.onclick = fn;
    el.append(b);
    return b;
  }
  function open(type) {
    if (type === active) {
      close();
      return;
    }
    if (type === "pages") {
      let c = shell(type, "Pages");
      let panel = c.closest(".reader-panel");
      panel.id = "pages-overlay";
      panel.className = "";
      panel.innerHTML =
        '<button class="pages-close" data-action="close-overlay" aria-label="Close pages">Close ×</button><div id="pages-flow"></div>';
      panel.querySelector("button").onclick = close;
      for (let n = 1; n <= max; n++) {
        let b = document.createElement("button");
        b.className = "thumb-cell" + (n === page ? " thumb-current" : "");
        b.dataset.action = "thumbnail";
        b.dataset.page = n;
        b.setAttribute("aria-label", "Go to page " + n);
        b.innerHTML =
          '<img class="thumb" src="comic-sample-' +
          n +
          '.svg" alt="Sample page ' +
          n +
          '"><span class="thumb-caption">' +
          n +
          (bookmarks.includes(n) ? " ◆" : "") +
          "</span>";
        b.onclick = () => {
          seek(n);
          close();
        };
        panel.querySelector("#pages-flow").append(b);
      }
      panel.querySelector("button").focus();
      return;
    }
    if (type === "loupe") {
      shell(type, "Loupe").closest(".reader-panel").remove();
      let l = document.createElement("div");
      l.className = "loupe";
      l.innerHTML =
        '<img src="comic-sample-' + page + '.svg" alt="Magnified sample page">';
      $("#stage").append(l);
      let info = document.createElement("div");
      info.className = "loupe-info";
      info.innerHTML =
        'Loupe · move pointer · + / − magnify <button data-action="close-loupe">Close ×</button>';
      info.querySelector("button").onclick = close;
      $("#stage").append(info);
      return;
    }
    let c = shell(
      type,
      {
        layout: "Layout",
        image: "Image",
        settings: "Reader settings",
        navigator: "Chapters",
        bookmarks: "Bookmarks",
        goto: "Go to page",
        shortcuts: "Keyboard shortcuts",
        close: "Close reader?",
        clear: "Clear resume?",
        reset: "Reset series?",
        pairing: "Page pairing",
      }[type] || type,
    );
    if (type === "layout") {
      [
        ["single_page", "Single page"],
        ["paired_pages", "Paired pages"],
        ["long_strip", "Long strip"],
      ].forEach(([v, label]) => {
        let b = tool(
          c,
          (layout === v ? "✓  " : "") + label,
          () => {
            layout = v;
            render();
            savePrefs();
            openRefresh("layout");
          },
          "layout-" + v,
        );
        b.classList.toggle("active", layout === v);
      });
      if (layout === "long_strip") {
        range(c, "Portrait width", width, 40, 100, (v) => {
          width = v;
          $("#strip-stack").style.width = v + "%";
          savePrefs();
        });
        choices(
          c,
          "Page spacing",
          [
            [0, "Seamless"],
            [8, "Breathing room"],
          ],
          gap,
          (v) => {
            gap = v;
            $("#strip-stack").style.gap = v + "px";
            savePrefs();
          },
        );
        tool(
          c,
          auto ? "Pause auto-scroll" : "Start auto-scroll",
          () => {
            if (auto) pause();
            else startAuto();
            openRefresh("layout", true);
          },
          "auto-scroll",
        );
        range(c, "Speed", speed, 1, 10, (v) => (speed = v));
      }
    } else if (type === "image") {
      choices(
        c,
        "Quality",
        ["fast", "balanced", "best"],
        profile.quality,
        (v) => {
          profile.quality = v;
          applyImage();
        },
      );
      range(c, "Brightness", profile.brightness, -100, 100, (v) => {
        profile.brightness = v;
        applyImage();
      });
      choices(
        c,
        "Night filter",
        [
          [false, "Off"],
          [true, "On"],
        ],
        profile.nightFilter,
        (v) => {
          profile.nightFilter = v;
          applyImage();
        },
      );
      let advanced = document.createElement("div");
      advanced.hidden = true;
      let t = tool(
        c,
        "Advanced image tools ›",
        () => {
          advanced.hidden = !advanced.hidden;
          t.setAttribute("aria-expanded", String(!advanced.hidden));
        },
        "advanced-image",
      );
      t.setAttribute("aria-expanded", "false");
      c.append(advanced);
      range(advanced, "Contrast", profile.contrast, -100, 100, (v) => {
        profile.contrast = v;
        applyImage();
      });
      range(advanced, "Gamma", profile.gamma, 10, 300, (v) => {
        profile.gamma = v;
        applyImage();
      });
      choices(
        advanced,
        "Rotate",
        [
          [0, "0°"],
          [90, "90°"],
          [180, "180°"],
          [270, "270°"],
        ],
        profile.rotation,
        (v) => {
          profile.rotation = v;
          applyImage();
        },
      );
      choices(
        advanced,
        "Auto-crop",
        [
          [false, "Off"],
          [true, "On"],
        ],
        profile.autoCrop,
        (v) => {
          profile.autoCrop = v;
          applyImage();
        },
      );
    } else if (type === "settings") {
      c.innerHTML = '<div class="section">Display</div>';
      choices(
        c,
        "Mode",
        ["Manga", "Comic", "Strip"],
        layout === "long_strip" ? "Strip" : order === "rtl" ? "Manga" : "Comic",
        (v) => {
          mode = v.toLowerCase();
          layout = v === "Strip" ? "long_strip" : "paired_pages";
          if (v !== "Strip") order = v === "Manga" ? "rtl" : "ltr";
          render();
          savePrefs();
        },
      );
      choices(c, "Night veil", ["off", "low", "high"], veil, (v) => {
        veil = v;
        applyImage();
        savePrefs();
      });
      c.insertAdjacentHTML(
        "beforeend",
        '<div class="section">Double page</div>',
      );
      choices(c, "Coupling", ["auto", "nudge"], coupling, (v) => {
        coupling = v;
        if (v === "nudge") page = Math.min(max, page + 1);
        render();
        savePrefs();
      });
      choices(
        c,
        "Gutter",
        [
          [0, "Off"],
          [0.22, "Subtle"],
          [0.35, "Medium"],
          [0.55, "Strong"],
        ],
        gutter,
        (v) => {
          gutter = v;
          render();
          savePrefs();
        },
      );
      range(c, "Zoom", zoom, 50, 250, (v) => {
        zoom = v;
        applyImage();
        savePrefs();
      });
      c.insertAdjacentHTML(
        "beforeend",
        '<div class="section">Long strip</div>',
      );
      choices(
        c,
        "Page width",
        [
          [62, "Narrow"],
          [78, "Comfort"],
          [90, "Wide"],
          [100, "Full"],
        ],
        width,
        (v) => {
          width = v;
          $("#strip-stack").style.width = v + "%";
          savePrefs();
        },
      );
      choices(
        c,
        "Gap",
        [
          [0, "None"],
          [8, "Thin"],
          [20, "Wide"],
        ],
        gap,
        (v) => {
          gap = v;
          $("#strip-stack").style.gap = v + "px";
          savePrefs();
        },
      );
      c.insertAdjacentHTML("beforeend", '<div class="section">Tools</div>');
      [
        ["Loupe", "loupe"],
        ["Bookmarks", "bookmarks"],
        ["Thumbnails", "pages"],
        ["Shortcuts", "shortcuts"],
        ["Go to page", "goto"],
      ].forEach(([t, a]) => tool(c, t, () => open(a)));
      choices(
        c,
        "Memory saver",
        [
          [false, "Off"],
          [true, "On"],
        ],
        memory,
        (v) => {
          memory = v;
          savePrefs();
        },
      );
      tool(c, "Clear resume", () => open("clear")).classList.add("danger");
      tool(c, "Reset series", () => open("reset")).classList.add("danger");
    } else if (type === "navigator") {
      let list = document.createElement("div");
      list.className = "navigator-list";
      for (let n = 1; n <= 24; n++) {
        let b = document.createElement("button");
        b.textContent =
          (kind === "volume"
            ? "Vol. "
            : kind === "issue"
              ? "Issue #"
              : "Chapter ") + n;
        b.classList.toggle("on", n === entry);
        b.dataset.action = "choose-entry";
        b.onclick = () => {
          entry = n;
          page = 1;
          close();
          render();
        };
        list.append(b);
      }
      c.append(list);
    } else if (type === "bookmarks") {
      if (!bookmarks.length)
        c.innerHTML =
          "<p>No bookmarks yet. Use Bookmark or press B on a page.</p>";
      bookmarks.forEach((n) =>
        tool(
          c,
          "Page " + n,
          () => {
            seek(n);
            close();
          },
          "bookmark-page",
        ),
      );
    } else if (type === "goto") {
      c.innerHTML =
        "<label>Page 1–" +
        max +
        '<input type="number" min="1" max="' +
        max +
        '" value="' +
        page +
        '" aria-label="Page number"></label><button class="primary" data-action="go-page">Go</button>';
      let go = () => {
        seek(Number(c.querySelector("input").value) || 1);
        close();
      };
      c.querySelector("button").onclick = go;
      c.querySelector("input").onkeydown = (e) => {
        if (e.key === "Enter") go();
      };
      c.querySelector("input").focus();
    } else if (type === "shortcuts") {
      c.innerHTML =
        "<p>← / → · Turn pages according to reading order<br>Home / End · First / last page<br>Page Up / Page Down · Previous / next entry<br>B · Bookmark · T · Pages · L · Loupe<br>S · Reader settings · C · Chapters<br>G · Go to page · ? · Shortcuts<br>+ / − · Zoom · H · Toggle chrome<br>Escape · Close the top overlay, then hide chrome<br>Shift + F10 · Page pairing chooser</p>";
    } else if (type === "pairing") {
      c.innerHTML = "<p>Choose the physical half to correct its pairing.</p>";
      choices(
        c,
        "Page",
        [
          [0, "Left page"],
          [1, "Right page"],
        ],
        0,
        (v) => {
          coupling = "nudge";
          page = Math.max(1, Math.min(max, page + (v === 0 ? -1 : 1)));
          close();
          render();
          savePrefs();
        },
      );
    } else if (["close", "clear", "reset"].includes(type)) {
      c.innerHTML =
        "<p>" +
        {
          close: "Your reading position is kept in this mock session.",
          clear: "Clear the saved reading position for this series?",
          reset: "Reset this series’ display settings and reading position?",
        }[type] +
        '</p><div class="toolrow"><button data-action="cancel-confirm">Cancel</button><button class="primary" data-action="confirm">' +
        (type === "close"
          ? "Close reader"
          : type === "clear"
            ? "Clear resume"
            : "Reset series") +
        "</button></div>";
      c.querySelector("[data-action=cancel-confirm]").onclick = close;
      c.querySelector("[data-action=confirm]").onclick = () => {
        if (type === "close") {
          close();
          leave(true);
        } else {
          page = 1;
          if (type === "reset") {
            layout = "paired_pages";
            order = mode === "comic" ? "ltr" : "rtl";
            width = 78;
            gap = 0;
            zoom = 100;
            veil = "off";
            gutter = 0.22;
            coupling = "auto";
            memory = false;
            savePrefs();
          }
          M.set("reader.resume." + sessionId(), 1);
          close();
          render();
        }
      };
    }
  }
  function openRefresh(t, keepAuto = false) {
    let running = auto;
    active = "";
    open(t);
    if (keepAuto && running) startAuto();
  }
  function pause() {
    auto = false;
    cancelAnimationFrame(raf);
    last = 0;
    let b = $("[data-action=auto-scroll]");
    if (b) b.textContent = "Start auto-scroll";
  }
  function startAuto() {
    if (layout !== "long_strip") return;
    auto = true;
    last = 0;
    let scroll = $("#strip").scrollTop;
    let b = $("[data-action=auto-scroll]");
    if (b) b.textContent = "Pause auto-scroll";
    function tick(now) {
      if (!auto) return;
      if (last) {
        scroll += (now - last) * 0.025 * speed;
        $("#strip").scrollTop = scroll;
      }
      last = now;
      if (
        $("#strip").scrollTop + $("#strip").clientHeight >=
        $("#strip").scrollHeight - 1
      ) {
        pause();
        return;
      }
      raf = requestAnimationFrame(tick);
    }
    raf = requestAnimationFrame(tick);
  }
  $("#strip").addEventListener("wheel", () => pause(), { passive: true });
  $("#strip").addEventListener("scroll", () => {
    if (layout !== "long_strip") return;
    let pages = $$("#strip-stack .sample-page");
    for (let el of pages) {
      if (
        el.offsetTop + el.offsetHeight >
        $("#strip").scrollTop + $("#strip").clientHeight / 2
      ) {
        page = Number(el.dataset.page);
        break;
      }
    }
    $("#currentCount").textContent = page;
    slider.value = page;
    save();
  });
  $("#stage").addEventListener("mousemove", (e) => {
    if (active === "loupe") {
      let l = $(".loupe");
      l.style.left =
        Math.min(innerWidth - 240, Math.max(0, e.clientX - 117)) + "px";
      l.style.top =
        Math.min(innerHeight - 240, Math.max(84, e.clientY - 117)) + "px";
      let im = l.querySelector("img");
      im.style.width = 235 * loupeScale + "px";
      im.style.left = -117 * (loupeScale - 1) + "px";
      im.style.top = -120 * loupeScale + "px";
    }
    if (!hud && (e.clientY < 84 || e.clientY > innerHeight - 54)) {
      hud = true;
      $("#chrome").style.opacity = "1";
      $("#chrome").inert = false;
    }
  });
  $("#stage").addEventListener("contextmenu", (e) => {
    e.preventDefault();
    open(layout === "paired_pages" ? "pairing" : "settings");
  });
  window.addEventListener(
    "keydown",
    (e) => {
      if (e.key === "Escape" && !active) {
        e.preventDefault();
        e.stopImmediatePropagation();
        hud = false;
        $("#chrome").style.opacity = "0";
        $("#chrome").inert = true;
        return;
      }
      if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
      if (active) {
        if (active === "loupe" && ["+", "=", "-"].includes(e.key)) {
          e.preventDefault();
          loupeScale = Math.max(
            1,
            Math.min(5, loupeScale + (e.key === "-" ? -0.25 : 0.25)),
          );
          let im = $(".loupe img");
          im.style.width = 235 * loupeScale + "px";
        }
        return;
      }
      let actions = {
        ArrowLeft: () => turn(order === "rtl" ? 1 : -1),
        ArrowRight: () => turn(order === "rtl" ? -1 : 1),
        Home: () => seek(1),
        End: () => seek(max),
        PageDown: () => changeEntry(1),
        PageUp: () => changeEntry(-1),
        b: toggleBookmark,
        t: () => open("pages"),
        l: () => open("loupe"),
        s: () => open("settings"),
        c: () => open("navigator"),
        g: () => open("goto"),
        "?": () => open("shortcuts"),
        h: () => {
          hud = !hud;
          $("#chrome").style.opacity = hud ? "1" : "0";
          $("#chrome").inert = !hud;
        },
        "+": () => {
          zoom = Math.min(250, zoom + 10);
          applyImage();
          savePrefs();
        },
        "-": () => {
          zoom = Math.max(50, zoom - 10);
          applyImage();
          savePrefs();
        },
      };
      if (e.shiftKey && e.key === "F10") {
        e.preventDefault();
        open("pairing");
      } else if (actions[e.key]) {
        e.preventDefault();
        actions[e.key]();
      }
    },
    true,
  );
  render();
  if (P.overlay) open(P.overlay);
  M.ready();
})();
