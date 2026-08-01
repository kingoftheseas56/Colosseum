// host.js — the wrapper's brain. It runs in the LOCAL qrc page (trusted) and mediates
// between the untrusted VidKing iframe and the least-privilege C++ bridge. It never
// trusts the iframe: it validates the embed URL before loading it, and validates the
// origin + source + shape of every postMessage before forwarding a tiny, allowlisted
// event through the bridge. (Theatre VidKing plan, 2026-08-02, Task 5.)
(function () {
    "use strict";

    var VIDKING_ORIGIN = "https://www.vidking.net";
    // The same event allowlist the C++ bridge enforces — belt and suspenders. The
    // bridge is the real gate; this just avoids forwarding obvious noise.
    var ALLOWED_EVENTS = ["play", "playing", "pause", "timeupdate", "seeked", "ended", "error"];

    var iframe = document.getElementById("player");
    var bridge = null;
    var sessionToken = "";

    // Read a single query parameter from the wrapper URL (CSP-safe, no eval).
    function queryParam(name) {
        var re = new RegExp("[?&]" + name + "=([^&]*)");
        var m = re.exec(window.location.search);
        return m ? decodeURIComponent(m[1].replace(/\+/g, " ")) : "";
    }

    // The embed URL is app-built (HostedPlayerApi), but we re-validate here so a
    // tampered query can never point the iframe anywhere but a VidKing embed route.
    function isValidEmbed(url) {
        try {
            var u = new URL(url);
            if (u.origin !== VIDKING_ORIGIN) return false;
            return u.pathname.indexOf("/embed/movie/") === 0
                || u.pathname.indexOf("/embed/tv/") === 0;
        } catch (e) {
            return false;
        }
    }

    function num(v) {
        var n = Number(v);
        return isFinite(n) ? n : 0;
    }

    function onMessage(ev) {
        // Only VidKing's own frame may speak, and only through OUR iframe's window.
        if (ev.origin !== VIDKING_ORIGIN) return;
        if (ev.source !== iframe.contentWindow) return;
        var data = ev.data;
        if (typeof data === "string") {
            try { data = JSON.parse(data); } catch (e) { return; }
        }
        if (!data || data.type !== "PLAYER_EVENT") return;
        var d = data.data || {};
        if (ALLOWED_EVENTS.indexOf(d.event) === -1) return;
        if (!bridge) return;

        // Forward ONLY the allowlisted fields plus OUR session token (never the
        // iframe's — the token is how the page rejects late events from a prior title).
        bridge.postPlayerEvent(JSON.stringify({
            event: d.event,
            currentTime: num(d.currentTime),
            duration: num(d.duration),
            progress: num(d.progress),
            id: String(d.id || ""),
            mediaType: String(d.mediaType || ""),
            season: num(d.season),
            episode: num(d.episode),
            session: sessionToken
        }));
    }

    function boot() {
        var url = queryParam("url");
        sessionToken = queryParam("session");
        // Assign the iframe src ONLY after validation.
        if (isValidEmbed(url)) {
            iframe.setAttribute("src", url);
        }
        window.addEventListener("message", onMessage, false);
    }

    // Bring up the WebChannel first, THEN load the iframe — so the bridge is ready
    // before any event can arrive.
    if (typeof QWebChannel !== "undefined" && window.qt && qt.webChannelTransport) {
        new QWebChannel(qt.webChannelTransport, function (channel) {
            bridge = channel.objects.hostedPlayerBridge || null;
            boot();
        });
    } else {
        // Defensive: no channel present (e.g. a bare browser preview). Still validate
        // and load the iframe so playback is visible, but no events are forwarded.
        boot();
    }
})();
