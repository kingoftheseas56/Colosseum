// State contract for RoundedPosterImage (Catalogue Poster & Shelf Polish, Task 2). Proves the
// bounded-decode math, the honest candidate-fallback state machine, and the single-mask contract —
// all as pure logical properties, without depending on real image loads (offscreen has no network).
// Sources are (re)assigned INSIDE the timer and every assertion runs synchronously in that one JS
// turn, so the async Image.Error auto-advance can never race the manual advanceCandidate() checks.
// House rule: NEVER throw offscreen — collect fails, print ROUNDED_POSTER_IMAGE_OK when clean.
import QtQuick
import "../qml" as UI

Item {
    id: h
    width: 400; height: 400

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    UI.RoundedPosterImage {
        id: poster
        width: 148; height: 222
        radius: 12
        revealDuration: 280
        testDevicePixelRatio: 2
        sources: []
    }

    Timer {
        interval: 80; running: true; repeat: false
        onTriggered: {
            // (re)assign synchronously → resets the state machine deterministically
            poster.sources = ["fake-candidate-a", "fake-candidate-b"];

            // ── bounded decode: 148×222 at 2× caps to 296×444 ──
            ok(poster.decodeWidth === 296 && poster.decodeHeight === 444,
               "2x decode cap, got " + poster.decodeWidth + "x" + poster.decodeHeight);
            ok(poster.effectiveScale === 2, "effective decode scale clamped to 2, got " + poster.effectiveScale);

            // ── candidate fallback state machine (honest, never wraps to 0) ──
            ok(poster.candidateIndex === 0, "first candidate active");
            ok(poster.activeSource.toString().indexOf("fake-candidate-a") !== -1,
               "activeSource is the first candidate");
            ok(poster.advanceCandidate() === true && poster.candidateIndex === 1, "advance to fallback");
            ok(poster.activeSource.toString().indexOf("fake-candidate-b") !== -1,
               "activeSource follows the index to the second candidate");
            ok(poster.advanceCandidate() === false && poster.exhausted, "exhaust honestly");
            ok(poster.candidateIndex === 1, "exhaustion does not wrap the index back to zero");

            // ── placeholder survives exhaustion (never a broken-image hole) ──
            ok(poster.placeholderVisible, "placeholder survives exhaustion");

            // ── single rounded-mask pass ──
            ok(poster.maskPassCount === 1, "one rounded mask pass, got " + poster.maskPassCount);

            // ── reassigning sources resets the machine (new title, fresh attempt) ──
            poster.sources = ["only-one"];
            ok(poster.candidateIndex === 0 && poster.exhausted === false,
               "new sources reset candidate index and exhaustion");
            ok(poster.advanceCandidate() === false && poster.exhausted,
               "a single-candidate source exhausts after one failure");

            // ── an empty source set is exhausted with the placeholder, no retry loop ──
            poster.sources = [];
            ok(poster.exhausted, "empty source set reports exhausted");
            ok(poster.placeholderVisible, "empty source set shows the placeholder");

            // ── clamp floor: dpr below 1 clamps to 1 ──
            poster.testDevicePixelRatio = 0.5;
            ok(poster.effectiveScale === 1, "decode scale clamps up to 1, got " + poster.effectiveScale);
            poster.testDevicePixelRatio = 3;
            ok(poster.effectiveScale === 2, "decode scale clamps down to 2, got " + poster.effectiveScale);

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("ROUNDED_POSTER_IMAGE_OK");
            Qt.exit(h.fails.length);
        }
    }
}
