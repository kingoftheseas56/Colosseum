// Runtime render harness for AddonLogo.qml — proves the component actually
// constructs (imports/MultiEffect/mask resolve) and that a bundled logo file
// decodes to Image.Ready, while an unknown add-on falls back to the letter.
// Offscreen decodes images fine (only the GPU present is absent). Verdict = exit code.
import QtQuick
import "../qml" as UI

Item {
    id: root

    // a bundled match (Torrentio) — should load a real image to Ready
    UI.AddonLogo {
        id: bundled
        addonId: "com.stremio.torrentio.addon"
        addonName: "Torrentio"
        size: 44
    }
    // a true unknown — no bundled logo, no manifest logo → letter fallback
    UI.AddonLogo {
        id: fallback
        addonId: "totally.unknown.addon"
        addonName: "Zzz"
        size: 44
    }
    // a name-only match resolving through the manifest-less path
    UI.AddonLogo {
        id: byName
        addonId: "x"
        addonName: "MediaFusion"
        size: 44
    }

    Timer {
        interval: 2000; running: true; repeat: false
        onTriggered: {
            var fails = [];
            // bundled Torrentio must have resolved to a real, decoded image
            if (bundled._bundled.indexOf("torrentio.png") < 0)
                fails.push("bundled did not match torrentio.png (_bundled=" + bundled._bundled + ")");
            if (!bundled._showImage)
                fails.push("bundled image not Ready/shown (status=" + bundled.children[1].status + ")");
            // unknown must show the letter, not an image
            if (fallback._bundled !== "")
                fails.push("unknown matched a bundled logo unexpectedly: " + fallback._bundled);
            if (fallback._showImage)
                fails.push("unknown should show the letter fallback, not an image");
            // name-only match
            if (byName._bundled.indexOf("mediafusion.png") < 0)
                fails.push("name-only MediaFusion did not resolve (_bundled=" + byName._bundled + ")");

            if (fails.length === 0) {
                console.log("=== AddonLogo render: PASS (bundled decoded, fallback = letter, name-match ok) ===");
                Qt.exit(0);
            } else {
                for (var i = 0; i < fails.length; i++) console.log("FAIL: " + fails[i]);
                console.log("=== AddonLogo render: " + fails.length + " FAIL ===");
                Qt.exit(1);
            }
        }
    }
}
