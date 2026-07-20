// Headless harness for the wallpaper-search pure logic (Axis 1+2, 2026-07-18;
// Konachan dropped 2026-07-20): the width gate, aspect labels, Wallhaven source
// mapping, and the paged single-source search-state lifecycle. Verdict rides the
// EXIT CODE — Qt.exit(0) pass — an uncaught onCompleted throw HANGS qml.exe.
import QtQuick
import "../qml/WallpaperApi.js" as W

QtObject {
    Component.onCompleted: {
        try {
            runChecks()
            Qt.exit(0)
        } catch (e) {
            console.log("HARNESS FAIL: " + e.message)
            Qt.exit(2)
        }
    }

    function runChecks() {
        // --- width gate: 1080p floor, 16:10 through ultrawide accepted, tall/small rejected ---
        if (!W.isWideEnough(1920, 1080)) throw new Error("16:9 1080p must pass")
        if (!W.isWideEnough(1920, 1200)) throw new Error("16:10 must pass (Axis 1 widened gate)")
        if (!W.isWideEnough(3440, 1440)) throw new Error("21:9 must pass")
        if (W.isWideEnough(1280, 720)) throw new Error("sub-1080p must fail")
        if (W.isWideEnough(1080, 1920)) throw new Error("portrait must fail")
        if (W.isWideEnough(3000, 1080)) throw new Error("beyond ultrawide must fail")

        if (W.aspectLabel(1920, 1080) !== "16:9") throw new Error("16:9 label")
        if (W.aspectLabel(1920, 1200) !== "16:10") throw new Error("16:10 label")
        if (W.aspectLabel(3440, 1440) !== "21:9") throw new Error("21:9 label")

        // --- Wallhaven mapping: identity + spec carry the source ---
        var wh = W.mapWallhaven({ "id": "abc123", "url": "https://wallhaven.cc/w/abc123",
                                  "path": "https://w.wallhaven.cc/full/ab/x.jpg",
                                  "dimension_x": 2560, "dimension_y": 1440,
                                  "resolution": "2560x1440",
                                  "thumbs": { "large": "https://th.wallhaven.cc/lg/ab/x.jpg" } }, "q")
        if (wh.source !== "Wallhaven" || wh.source_id !== "abc123")
            throw new Error("Wallhaven identity must carry the raw id")
        if (wh.image_url !== "https://w.wallhaven.cc/full/ab/x.jpg")
            throw new Error("Wallhaven image_url must be the full path")

        // --- state lifecycle: fresh state has more; exhausted state has none ---
        var st = W.freshState("  ", "nonsense")
        if (st.query !== "landscape" || st.sorting !== "relevance")
            throw new Error("freshState must default empty query + unknown sorting")
        if (st.koDone !== undefined || st.koPage !== undefined)
            throw new Error("freshState must not carry Konachan cursors after the 2026-07-20 removal")
        if (!W.hasMore(st))
            throw new Error("a fresh state must report more available")
        st.whLastPage = 2; st.whPage = 3
        if (W.hasMore(st))
            throw new Error("wallhaven exhausted must report no more")
        st.whPage = 2
        if (!W.hasMore(st))
            throw new Error("wallhaven pages remaining must report more")

        console.log("wallpaper_api_logic_harness: all checks passed")
    }
}
