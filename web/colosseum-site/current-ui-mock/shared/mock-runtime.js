/* Colosseum fixture runtime. No provider requests or native side effects. */
(function () {
  "use strict";
  var embedded = parent !== window,
    params = {},
    state = {},
    overlays = [],
    sessions = [],
    pending = [];
  try {
    params = JSON.parse(
      new URLSearchParams(location.search).get("params") || "{}",
    );
  } catch (_) {}
  try {
    state = JSON.parse(
      new URLSearchParams(location.search).get("state") || "{}",
    );
  } catch (_) {}
  var nonce = new URLSearchParams(location.search).get("hostToken") || "";
  function send(type, value) {
    if (embedded)
      parent.postMessage(
        { mockUI: true, token: nonce, type: type, value: value },
        "*",
      );
  }
  function flush() {
    pending.forEach(function (x) {
      send(x[0], x[1]);
    });
    pending = [];
  }
  var api = (window.MockUI = {
    params: params,
    updateParams: function (values) {
      Object.assign(params, values || {});
      send("params", values || {});
    },
    go: function (id, p) {
      if (embedded) send("go", { id: id, params: p || {} });
      else
        location.href =
          "../index.html#" +
          encodeURIComponent(id) +
          "?" +
          encodeURIComponent(JSON.stringify(p || {}));
    },
    back: function () {
      if (overlays.length) {
        closeTop();
        return;
      }
      if (embedded) send("back");
      else history.back();
    },
    get: function (k, f) {
      return Object.prototype.hasOwnProperty.call(state, k) ? state[k] : f;
    },
    set: function (k, v) {
      state[k] = v;
      send("set", { key: k, value: v });
      return v;
    },
    openSession: function (s) {
      sessions = sessions.filter(function (x) {
        return x.id !== s.id;
      });
      sessions.push(s);
      send("openSession", s);
    },
    closeSession: function (id) {
      sessions = sessions.filter(function (x) {
        return x.id !== id;
      });
      send("closeSession", id);
    },
    registerOverlay: function (el, close) {
      var entry = { element: el, close: close, focus: document.activeElement };
      overlays.push(entry);
      send("overlay", overlays.length);
      el.setAttribute("role", el.getAttribute("role") || "dialog");
      el.setAttribute("aria-modal", "true");
      setTimeout(function () {
        send(
          "vaultWindowChrome",
          !!overlays.length &&
            overlays[overlays.length - 1].element.classList.contains(
              "vault-detail-overlay",
            ),
        );
        var f = el.querySelector("button,input,select,[tabindex]");
        if (f) f.focus();
      }, 0);
      return function () {
        var i = overlays.indexOf(entry);
        if (i >= 0) overlays.splice(i, 1);
        send("overlay", overlays.length);
        if (entry.focus && entry.focus.isConnected) entry.focus.focus();
      };
    },
    toast: function (t) {
      var el = document.getElementById("mock-toast");
      if (!el) {
        el = document.createElement("div");
        el.id = "mock-toast";
        el.setAttribute("role", "status");
        el.style.cssText =
          "position:fixed;bottom:80px;left:50%;transform:translateX(-50%);z-index:9999;padding:12px 22px;background:#222b30;border:1px solid #ffffff33;border-radius:12px;color:white;font:14px system-ui";
        document.body.appendChild(el);
      }
      el.textContent = t;
      el.hidden = false;
      clearTimeout(el.timer);
      el.timer = setTimeout(function () {
        el.hidden = true;
      }, 2800);
    },
    ready: function () {
      document.documentElement.dataset.mockReady = "true";
      send("ready", { title: document.title });
    },
    asset: function (p) {
      return "../assets/" + p;
    },
  });
  function closeTop() {
    var o = overlays.pop();
    if (!o) return;
    o.close();
    send("overlay", overlays.length);
    if (o.focus && o.focus.isConnected) o.focus.focus();
  }
  window.addEventListener("message", function (e) {
    if (
      e.source !== parent ||
      !e.data ||
      !e.data.mockHost ||
      e.data.token !== nonce
    )
      return;
    var m = e.data;
    if (m.type === "state") {
      state = m.value || {};
      window.dispatchEvent(new CustomEvent("mockstate", { detail: state }));
    }
    if (m.type === "focusVaultOverlay" && overlays.length) {
      var layer = overlays[overlays.length - 1].element;
      if (layer.classList.contains("vault-detail-overlay")) {
        var focusable = Array.from(
          layer.querySelectorAll("button,input,select,a[href]"),
        ).filter(function (x) {
          return !x.disabled && x.offsetParent !== null;
        });
        var target =
          m.value === "last" ? focusable[focusable.length - 1] : focusable[0];
        if (target) {
          window.focus();
          target.focus();
        }
      }
    }
    if (m.type === "escape") {
      if (overlays.length) closeTop();
      else send("back");
    }
  });
  document.addEventListener(
    "keydown",
    function (e) {
      if (e.key === "Escape") {
        e.preventDefault();
        e.stopImmediatePropagation();
        if (overlays.length) closeTop();
        else api.back();
      }
      if (e.key === "Tab" && overlays.length) {
        var root = overlays[overlays.length - 1].element,
          list = Array.from(
            root.querySelectorAll(
              'button,input,select,textarea,a[href],[tabindex="0"]',
            ),
          ).filter(function (x) {
            return !x.disabled && x.offsetParent !== null;
          });
        if (list.length) {
          var a = list[0],
            b = list[list.length - 1];
          if (
            embedded &&
            root.classList.contains("vault-detail-overlay") &&
            ((e.shiftKey && document.activeElement === a) ||
              (!e.shiftKey && document.activeElement === b))
          ) {
            e.preventDefault();
            e.stopImmediatePropagation();
            send("focusVaultChrome", e.shiftKey ? "last" : "first");
            return;
          }
          if (e.shiftKey && document.activeElement === a) {
            b.focus();
            e.preventDefault();
          } else if (!e.shiftKey && document.activeElement === b) {
            a.focus();
            e.preventDefault();
          }
        }
      }
    },
    true,
  );
  if (
    embedded &&
    new URLSearchParams(location.search).get("chrome") !== "none"
  ) {
    var style = document.createElement("style");
    style.textContent = ".topbar{display:none!important}";
    document.head.appendChild(style);
    document.documentElement.dataset.embedded = "true";
  }
  function init() {
    send("hello");
    flush();
  }
  if (document.readyState === "loading")
    document.addEventListener("DOMContentLoaded", init);
  else init();
})();
