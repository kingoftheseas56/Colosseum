(function () {
  "use strict";
  var routes = window.MOCK_ROUTES || {},
    state = {
      rebuiltTitlePages: false,
      activeWorld: "",
      "account.signed": true,
      "account.username": "demo_colosseum",
    },
    sessions = [],
    stack = [],
    current = null,
    token = "",
    vaultWindowChrome = false,
    view = document.getElementById("view"),
    fan = document.getElementById("fan"),
    sheet = document.getElementById("sheet");
  function el(id) {
    return document.getElementById(id);
  }
  function escape(s) {
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
  function fit() {
    var s = Math.min(innerWidth / 1280, innerHeight / 720);
    el("stage").style.transform = "scale(" + s + ")";
    el("stage").style.left = (innerWidth - 1280 * s) / 2 + "px";
    el("stage").style.top = (innerHeight - 720 * s) / 2 + "px";
  }
  onresize = fit;
  fit();
  function send(type, value) {
    view.contentWindow.postMessage(
      { mockHost: true, token: token, type: type, value: value },
      "*",
    );
  }
  function notice(t) {
    el("notice").textContent = t;
    el("notice").hidden = false;
    setTimeout(function () {
      el("notice").hidden = true;
    }, 3000);
  }
  function go(id, p, replace, fromHistory) {
    closeAccount(false);
    var route = routes[id];
    if (!route) {
      dialog(
        "Route unavailable",
        "This route is not registered in this reviewed build: " + id,
        [],
      );
      return;
    }
    if (current && !replace && !fromHistory) {
      stack.push(current);
    }
    current = {
      id: id,
      params: Object.assign({}, route.params || {}, p || {}),
    };
    state.activeWorld = route.world || "";
    token = Math.random().toString(36).slice(2);
    var nextUrl =
      route.file +
      "?params=" +
      encodeURIComponent(JSON.stringify(current.params)) +
      "&state=" +
      encodeURIComponent(JSON.stringify(state)) +
      "&chrome=" +
      encodeURIComponent(route.chrome || "default") +
      "&hostToken=" +
      token;
    var nextFrame = document.createElement("iframe");
    nextFrame.id = "view";
    nextFrame.title = "Colosseum content";
    nextFrame.src = nextUrl;
    view.replaceWith(nextFrame);
    view = nextFrame;
    if (!fromHistory) {
      var entry = { mockRoute: true, route: current, stack: stack.slice() };
      history[replace ? "replaceState" : "pushState"](
        entry,
        "",
        "#" +
          encodeURIComponent(id) +
          "?" +
          encodeURIComponent(JSON.stringify(current.params)),
      );
    }
    document.title = route.title + " · Colosseum mock";
    fan.hidden = true;
    sheet.hidden = true;
    el("topbar").inert = false;
    el("taskbar").inert = false;
    chrome(route);
    window.MockHost.current = current;
  }
  function back() {
    if (accountLayer && !accountLayer.hidden) {
      closeAccount();
      return;
    }
    if (!sheet.hidden) {
      closeSheet();
      return;
    }
    if (!fan.hidden) {
      fan.hidden = true;
      return;
    }
    if (stack.length) {
      history.back();
    } else if (current.id !== "home") go("home", {}, true);
  }
  window.addEventListener("popstate", function (e) {
    if (e.state && e.state.mockRoute && routes[e.state.route.id]) {
      stack.splice.apply(stack, [0, stack.length].concat(e.state.stack || []));
      go(e.state.route.id, e.state.route.params, true, true);
    }
  });
  function icon(file, label, fn) {
    var b = document.createElement("button");
    b.title = label;
    b.setAttribute("aria-label", label);
    b.innerHTML =
      file === "account"
        ? '<span class="account-glyph" aria-hidden="true"></span>'
        : '<img src="assets/icons/' + file + '.svg" alt="">';
    b.onclick = fn;
    return b;
  }
  function isVaultRoute() {
    return (
      !!current &&
      (current.id === "vault" || current.id.indexOf("vault.") === 0)
    );
  }
  function chrome(r) {
    var vault = isVaultRoute();
    vaultWindowChrome = false;
    el("topbar").classList.toggle("vault-chrome", vault);
    el("topbar").hidden = r.chrome === "none";
    el("taskbar").hidden = r.chrome === "none";
    el("worlds").hidden = vault || r.chrome === "back";
    el("system").hidden = r.chrome === "back";
    el("clock").lastElementChild.hidden = vault || r.chrome === "back";
    el("back").hidden = current.id === "home";
    el("back").querySelector("span").textContent = vault
      ? "Back"
      : r.chrome === "back"
        ? r.title
        : current.id.indexOf("world.") === 0 || current.id === "universes"
          ? "Home"
          : r.world
            ? r.world[0].toUpperCase() + r.world.slice(1)
            : "Back";
    el("worlds").innerHTML = "";
    [
      ["Tankoban", "world.tankoban"],
      ["Biblio", "world.biblio"],
      ["Theatre", "world.theatre"],
      ["Universes", "universes"],
    ].forEach(function (x) {
      var b = document.createElement("button");
      b.textContent = x[0];
      b.className = r.world === x[0].toLowerCase() ? "active" : "";
      b.onclick = function () {
        go(x[1]);
      };
      el("worlds").appendChild(b);
    });
    el("system").innerHTML = "";
    var entries = [];
    if (current.id === "home") entries.push(["update", "Updates", "update"]);
    else if (r.world && routes["search." + r.world])
      entries.push(["search", "Search", "search." + r.world]);
    if (r.world === "theatre")
      entries.push(["stremio-official", "Stremio", "connections.stremio"]);
    entries.push(
      ["trackers", "Connections", "connections.center"],
      ["account", "Account", "account.center"],
      ["settings", "Wallpaper", "wallpaper"],
    );
    if (vault) entries = [];
    entries.forEach(function (x) {
      el("system").appendChild(
        icon(x[0], x[1], function () {
          if (x[0] === "account") openAccount(this);
          else go(x[2]);
        }),
      );
    });
    updateAccountGlyph();
    el("system").appendChild(
      icon("minimize", "Minimize mock", function () {
        dialog(
          "Colosseum is minimized",
          "Native window minimization is represented in this browser mock. Your staged sessions remain open.",
          [{ label: "Restore", action: closeSheet }],
        );
      }),
    );
    el("system").appendChild(icon("fullscreen-exit", "Fit app", fit));
    el("system").appendChild(
      icon("power", "Quit", function () {
        dialog(
          "Close Colosseum?",
          "This only resets the staged browser session. No native applications are affected.",
          [
            {
              label: "Reset mock",
              action: function () {
                sessions = [];
                state = { rebuiltTitlePages: false };
                renderSessions();
                go("home", {}, true);
              },
            },
          ],
        );
      }),
    );
  }
  var restoreFocus;
  function closeSheet() {
    sheet.hidden = true;
    if (restoreFocus) restoreFocus.focus();
  }
  function dialog(title, body, actions) {
    restoreFocus = document.activeElement;
    sheet.innerHTML =
      '<div class="dialog" role="dialog" aria-modal="true" aria-labelledby="sheet-title"><h2 id="sheet-title">' +
      escape(title) +
      "</h2><p>" +
      escape(body) +
      '</p><div class="items"></div><button data-action="cancel">Close</button></div>';
    sheet.hidden = false;
    sheet.querySelector("[data-action=cancel]").onclick = closeSheet;
    (actions || []).forEach(function (a) {
      var b = document.createElement("button");
      b.textContent = a.label;
      b.onclick = a.action;
      sheet.querySelector(".items").appendChild(b);
    });
    sheet.querySelector("button").focus();
  }

  var accountLayer = null,
    accountInvoker = null;
  function accountPresent() {
    return state["account.signed"] !== false && !state["account.localOnly"];
  }
  function updateAccountGlyph() {
    var button = document.querySelector('#system button[aria-label="Account"]');
    if (!button) return;
    var signed = accountPresent();
    button.innerHTML = signed
      ? '<span class="account-glyph signed">' +
        escape(
          String(state["account.username"] || "demo_colosseum")
            .slice(0, 1)
            .toUpperCase(),
        ) +
        "</span>"
      : state["account.localOnly"]
        ? '<span class="account-device">Device</span>'
        : '<span class="account-glyph" aria-hidden="true"></span>';
    button.classList.toggle("device", !!state["account.localOnly"]);
  }
  function closeAccount(restore) {
    if (!accountLayer || accountLayer.hidden) return;
    accountLayer.hidden = true;
    view.inert = false;
    el("topbar").inert = false;
    el("taskbar").inert = false;
    if (restore !== false && accountInvoker && accountInvoker.isConnected)
      accountInvoker.focus();
  }
  function openAccount(button) {
    if (accountLayer && !accountLayer.hidden) {
      closeAccount();
      return;
    }
    accountInvoker =
      button || document.querySelector('#system button[aria-label="Account"]');
    if (!accountLayer) {
      accountLayer = document.createElement("div");
      accountLayer.id = "account-flyout";
      accountLayer.hidden = true;
      el("stage").appendChild(accountLayer);
      accountLayer.addEventListener("click", function (e) {
        if (e.target === accountLayer) closeAccount();
      });
      accountLayer.addEventListener("keydown", function (e) {
        var bs = Array.from(accountLayer.querySelectorAll("button")),
          i = bs.indexOf(document.activeElement);
        if (e.key === "Escape") {
          e.stopPropagation();
          e.preventDefault();
          closeAccount();
        } else if (
          e.key === "Tab" ||
          e.key === "ArrowUp" ||
          e.key === "ArrowDown"
        ) {
          e.preventDefault();
          var delta = e.key === "ArrowUp" || e.shiftKey ? -1 : 1;
          bs[(i + delta + bs.length) % bs.length].focus();
        }
      });
    }
    renderAccount();
    accountLayer.hidden = false;
    view.inert = true;
    el("topbar").inert = true;
    el("taskbar").inert = true;
    accountLayer.querySelector("button").focus();
  }
  function renderAccount() {
    var present = accountPresent(),
      retry = !!state["account.syncRetry"],
      pending = Number(state["account.pendingChanges"] || 0),
      sync = state["account.syncState"] || "synced",
      line =
        retry || sync === "blocked"
          ? "Sync needs attention"
          : pending
            ? "Syncing — " +
              pending +
              " change" +
              (pending === 1 ? "" : "s") +
              " pending"
            : sync === "retrying"
              ? "Waiting for the account service…"
              : sync === "inactive"
                ? "Cloud sync off"
                : "All changes synced";
    accountLayer.innerHTML =
      '<section class="account-card" role="dialog" aria-modal="true" aria-label="Colosseum account"><div class="account-identity"><span class="account-medallion">' +
      (present
        ? escape(
            String(state["account.username"] || "demo_colosseum")
              .slice(0, 1)
              .toUpperCase(),
          )
        : "?") +
      "</span><div><b>" +
      (present
        ? escape(state["account.username"] || "demo_colosseum")
        : "Not signed in") +
      "</b><small>Colosseum account</small></div></div>" +
      (present
        ? '<hr><div class="account-sync"><i class="' +
          (retry || sync === "blocked"
            ? "blocked"
            : pending || sync === "retrying"
              ? "pending"
              : "") +
          '"></i><span>' +
          escape(line) +
          '</span></div><hr><nav><button data-account="profile">Account ›</button><button data-account="devices">Devices ›</button></nav>'
        : "") +
      (present && retry
        ? '<button class="account-retry" data-account="retry">Retry sync</button>'
        : "") +
      '<button class="account-session" data-account="session">' +
      (present ? "Sign out" : "Sign in") +
      "</button></section>";
    var card = accountLayer.querySelector(".account-card"),
      scale = el("stage").getBoundingClientRect().width / 1280,
      stageRect = el("stage").getBoundingClientRect(),
      anchor = accountInvoker.getBoundingClientRect();
    card.style.left =
      Math.max(
        16,
        Math.min(
          1280 - 296 - 16,
          (anchor.right - stageRect.left) / scale - 296,
        ),
      ) + "px";
    card.style.top =
      Math.max(
        16,
        Math.min(
          720 - card.offsetHeight - 16,
          (anchor.bottom - stageRect.top) / scale + 8,
        ),
      ) + "px";
    accountLayer.querySelectorAll("[data-account]").forEach(function (b) {
      b.onclick = function () {
        var action = b.dataset.account;
        if (action === "retry") {
          state["account.syncRetry"] = false;
          state["account.syncState"] = "synced";
          state["account.pendingChanges"] = 0;
          send("state", state);
          renderAccount();
          accountLayer.querySelector("[data-account=session]").focus();
          return;
        }
        closeAccount(false);
        if (action === "profile") go("account.center", { section: "profile" });
        else if (action === "devices") go("account.devices");
        else if (present) {
          state["account.signed"] = false;
          state["account.localOnly"] = false;
          updateAccountGlyph();
          send("state", state);
          if (accountInvoker && accountInvoker.isConnected)
            accountInvoker.focus();
        } else go("account.signin");
      };
    });
  }

  function renderSessions() {
    el("sessions").innerHTML = "";
    var groups = {};
    sessions.forEach(function (s) {
      (groups[s.kind || "media"] || (groups[s.kind || "media"] = [])).push(s);
    });
    Object.keys(groups).forEach(function (k) {
      var file = /book|audio|biblio/.test(k)
        ? "book-library"
        : /video|theatre/.test(k)
          ? "projector-theatre"
          : "comic-book";
      var b = icon(file, k + " sessions", function () {
        fan.innerHTML = "";
        groups[k].forEach(function (s) {
          var row = document.createElement("div"),
            open = document.createElement("button"),
            close = document.createElement("button");
          open.textContent = s.title;
          open.onclick = function () {
            go(s.route, s.params);
          };
          close.textContent = "End session";
          close.onclick = function () {
            sessions = sessions.filter(function (x) {
              return x.id !== s.id;
            });
            renderSessions();
            fan.hidden = true;
          };
          row.append(open, close);
          fan.appendChild(row);
        });
        fan.hidden = false;
      });
      b.innerHTML += "<small>" + groups[k].length + "</small>";
      el("sessions").appendChild(b);
    });
  }
  el("back").onclick = function () {
    if (isVaultRoute()) {
      send("escape");
      return;
    }
    if (current.id.indexOf("world.") === 0 || current.id === "universes")
      go("home");
    else back();
  };
  el("start").onclick = function () {
    el("taskbar").classList.toggle("open");
    fan.hidden = true;
  };
  document.querySelector("[data-route=vault]").onclick = function () {
    go("vault");
  };
  [
    ["open-media", "Open Media", null],
    ["watch-party", "Join Watch Party", "watchparty.join"],
    ["download", "Downloads", "downloads"],
    ["extensions", "Extensions", "extensions"],
    ["your-colosseum-laurel", "Your Colosseum", "activity"],
    ["feria", "Feria", "feria"],
  ].forEach(function (x) {
    el("task-actions").appendChild(
      icon(x[0], x[1], function () {
        if (x[2]) go(x[2]);
        else
          dialog(
            "Open Media",
            "Choose a bundled sample. These local fixtures do not read files from your computer.",
            [
              {
                label: "Sample chapter · 12 pages",
                action: function () {
                  go("reader.comic", { title: "Sample chapter" });
                },
              },
              {
                label: "Sample book",
                action: function () {
                  go("reader.book");
                },
              },
              {
                label: "Sample video",
                action: function () {
                  go("player.video");
                },
              },
              {
                label: "Open Recent",
                action: function () {
                  dialog(
                    "Open Recent",
                    "Recent staged media in this session.",
                    sessions.map(function (s) {
                      return {
                        label: s.title,
                        action: function () {
                          go(s.route, s.params);
                        },
                      };
                    }),
                  );
                },
              },
              {
                label: "Next to Open",
                action: function () {
                  go("library.continue");
                },
              },
            ],
          );
      }),
    );
  });
  window.addEventListener("message", function (e) {
    var m = e.data;
    if (e.source !== view.contentWindow || !m || !m.mockUI || m.token !== token)
      return;
    if (m.type === "overlay") {
      vaultWindowChrome = false;
      el("topbar").inert = !!m.value;
      el("taskbar").inert = !!m.value;
    }
    if (m.type === "vaultWindowChrome" && isVaultRoute()) {
      vaultWindowChrome = !!m.value;
      if (vaultWindowChrome) el("topbar").inert = false;
    }
    if (m.type === "focusVaultChrome" && isVaultRoute() && vaultWindowChrome) {
      var chromeButtons = [el("back")].concat(
        Array.from(el("system").querySelectorAll("button")),
      );
      (m.value === "last"
        ? chromeButtons[chromeButtons.length - 1]
        : chromeButtons[0]
      ).focus();
    }
    if (m.type === "params") {
      Object.assign(current.params, m.value || {});
      history.replaceState(
        { mockRoute: true, route: current, stack: stack.slice() },
        "",
        "#" +
          encodeURIComponent(current.id) +
          "?" +
          encodeURIComponent(JSON.stringify(current.params)),
      );
    }
    if (m.type === "go") go(m.value.id, m.value.params);
    if (m.type === "back") back();
    if (m.type === "hello" || m.type === "ready") send("state", state);
    if (m.type === "set") {
      state[m.value.key] = m.value.value;
      updateAccountGlyph();
      send("state", state);
    }
    if (m.type === "openSession") {
      sessions = sessions.filter(function (s) {
        return s.id !== m.value.id;
      });
      sessions.push(m.value);
      renderSessions();
    }
    if (m.type === "closeSession") {
      sessions = sessions.filter(function (s) {
        return s.id !== m.value;
      });
      renderSessions();
    }
  });
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape") {
      e.preventDefault();
      if (accountLayer && !accountLayer.hidden) closeAccount();
      else if (!sheet.hidden || !fan.hidden) back();
      else send("escape");
    }
    if (
      e.key === "Tab" &&
      sheet.hidden &&
      isVaultRoute() &&
      vaultWindowChrome
    ) {
      var windowButtons = [el("back")].concat(
        Array.from(el("system").querySelectorAll("button")),
      );
      if (
        (e.shiftKey && document.activeElement === windowButtons[0]) ||
        (!e.shiftKey &&
          document.activeElement === windowButtons[windowButtons.length - 1])
      ) {
        e.preventDefault();
        send("focusVaultOverlay", e.shiftKey ? "last" : "first");
      }
    }
    if (e.key === "Tab" && !sheet.hidden) {
      var xs = Array.from(sheet.querySelectorAll("button,input"));
      if (e.shiftKey && document.activeElement === xs[0]) {
        xs[xs.length - 1].focus();
        e.preventDefault();
      } else if (!e.shiftKey && document.activeElement === xs[xs.length - 1]) {
        xs[0].focus();
        e.preventDefault();
      }
    }
  });
  function clock() {
    var d = new Date();
    el("time").textContent = d.toLocaleTimeString("en-US", {
      hour: "numeric",
      minute: "2-digit",
    });
    el("date").textContent = d.toLocaleDateString("en-US", {
      weekday: "long",
      month: "long",
      day: "numeric",
    });
  }
  clock();
  setInterval(clock, 60000);
  window.MockHost = {
    go: go,
    back: back,
    routes: routes,
    state: state,
    sessions: function () {
      return sessions.slice();
    },
    stack: stack,
    current: null,
  };
  var hash = location.hash.slice(1).split("?"),
    start = "home",
    p = {};
  try {
    start = decodeURIComponent(hash[0]) || "home";
    p = JSON.parse(decodeURIComponent(hash[1] || "%7B%7D"));
  } catch (_) {}
  go(routes[start] ? start : "home", p, true);
})();
