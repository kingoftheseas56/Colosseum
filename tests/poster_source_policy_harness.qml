// Pure contract proof for PosterSourcePolicy.js + CatalogueVisualMetrics.js (Catalogue Poster
// and Shelf Polish, Task 1). No network, no QML items — just the URL mechanics and the frozen
// visual tokens. House rule: NEVER throw offscreen (hangs) — collect fails, print the unique OK
// marker POSTER_SOURCE_POLICY_OK only when clean, single Qt.exit(fails.length).
import QtQuick
import "../qml/PosterSourcePolicy.js" as Policy
import "../qml/CatalogueVisualMetrics.js" as Metrics

Item {
    id: h
    width: 10; height: 10

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    Timer {
        interval: 50; running: true; repeat: false
        onTriggered: {
            var small  = "https://images.metahub.space/poster/small/tt1375666/img";
            var medium = "https://images.metahub.space/poster/medium/tt1375666/img";
            var large  = "https://images.metahub.space/poster/large/tt1375666/img";
            var liveMed = "https://live.metahub.space/poster/medium/tt1375666/img";
            var liveSml = "https://live.metahub.space/poster/small/tt1375666/img";

            // ── small source: still tries medium first, then small (both on live host) ──
            var got = Policy.candidates(small, []);
            ok(got.length === 2, "Metahub emits medium + small (got " + got.length + ")");
            ok(got[0] === liveMed, "medium first, got " + got[0]);
            ok(got[1] === liveSml, "small fallback, got " + got[1]);

            // ── large and medium sources collapse to the same medium→small ladder ──
            ok(Policy.candidates(large, []).join("|") === (liveMed + "|" + liveSml),
               "large source -> medium then small");
            ok(Policy.candidates(medium, []).join("|") === (liveMed + "|" + liveSml),
               "medium source -> medium then small");

            // ── candidate order is stable across calls ──
            ok(Policy.candidates(small, [])[0] === liveMed && Policy.candidates(large, [])[0] === liveMed,
               "medium is invariantly the first candidate");

            // ── foreign (non-Metahub) URLs are byte-for-byte unchanged, single candidate ──
            ok(Policy.candidates("https://covers.example/a.jpg", []).join("|")
               === "https://covers.example/a.jpg", "foreign URL unchanged, single candidate");

            // ── a foreign provider's explicit candidate list is honoured (deduped, in order) ──
            var foreign = Policy.candidates("https://covers.example/a.jpg",
                                            ["https://covers.example/a.jpg", "https://covers.example/b.jpg"]);
            ok(foreign.join("|") === "https://covers.example/a.jpg|https://covers.example/b.jpg",
               "explicit foreign candidates append + dedupe, got " + foreign.join("|"));

            // ── duplicate explicit candidates collapse against the generated ladder ──
            var dup = Policy.candidates(small, [liveSml, liveSml, liveMed]);
            ok(dup.length === 2 && dup.join("|") === (liveMed + "|" + liveSml),
               "duplicate explicit candidates collapse, got " + dup.join("|"));

            // ── empty / null input returns [] (no retry loop over nothing) ──
            ok(Policy.candidates("", []).length === 0, "empty input returns []");
            ok(Policy.candidates(null, []).length === 0, "null input returns []");
            ok(Policy.candidates(undefined).length === 0, "undefined input + missing list returns []");

            // ── liveUrl normalizes only the Metahub host, leaves foreign untouched ──
            ok(Policy.liveUrl(small) === liveSml, "liveUrl normalizes metahub host");
            ok(Policy.liveUrl("https://covers.example/a.jpg") === "https://covers.example/a.jpg",
               "liveUrl leaves foreign URLs unchanged");

            // ── approved frozen visual tokens ──
            ok(Metrics.gallery.posterWidth === 148 && Metrics.gallery.posterRadius === 12,
               "approved gallery geometry (148/12)");
            ok(Metrics.gallery.cardGap === 20 && Metrics.gallery.shelfGap === 46
               && Metrics.gallery.headerGap === 18, "approved gallery spacing (20/46/18)");
            ok(Metrics.gallery.titlePixels === 13 && Metrics.gallery.titleLines === 2
               && Metrics.gallery.titleMinHeight === 35, "approved gallery title (13/2/35)");
            ok(Metrics.gallery.hoverLift === 7 && Metrics.gallery.hoverDuration === 260
               && Metrics.gallery.imageRevealDuration === 280, "approved gallery motion (7/260/280)");
            ok(Metrics.classic.posterWidth === 132 && Metrics.classic.posterRadius === 8,
               "classic geometry preserved (132/8)");

            // ── tokens are frozen (mutating must not stick) ──
            var before = Metrics.gallery.posterWidth;
            try { Metrics.gallery.posterWidth = 999; } catch (e) { /* strict throw is fine */ }
            ok(Metrics.gallery.posterWidth === before, "gallery tokens are immutable");

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("POSTER_SOURCE_POLICY_OK");
            Qt.exit(h.fails.length);
        }
    }
}
