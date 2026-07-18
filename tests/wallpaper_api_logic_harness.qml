// Headless harness for the wallpaper-search pure logic (Axis 1+2, 2026-07-18):
// the width gate, board-tag mapping, source mapping, interleave, and the paged
// search-state lifecycle. Verdict rides the EXIT CODE — Qt.exit(0) pass — an
// uncaught onCompleted throw HANGS qml.exe.
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

        // --- Konachan tag mapping: title -> ONE underscored tag, SFW pinned, sort tags ---
        var tags = W.konachanTags("One Piece", "relevance")
        if (tags.indexOf("one_piece") !== 0 || tags.indexOf("rating:s") < 0)
            throw new Error("board tags must underscore the title and pin rating:s, got '" + tags + "'")
        if (W.konachanTags("x", "top").indexOf("order:score") < 0)
            throw new Error("top sorting must ride order:score")
        if (W.konachanTags("x", "random").indexOf("order:random") < 0)
            throw new Error("random sorting must ride order:random")

        // --- Konachan mapping: protocol-relative urls normalized, identity prefixed ---
        var k = W.mapKonachan({ "id": 7, "width": 2560, "height": 1440,
                                "file_url": "//konachan.net/image/x.jpg",
                                "preview_url": "https://konachan.net/data/preview/x.jpg" }, "q")
        if (k.image_url !== "https://konachan.net/image/x.jpg")
            throw new Error("protocol-relative file_url must normalize")
        if (k.source !== "Konachan" || k.source_id !== "konachan-7")
            throw new Error("Konachan identity must be prefixed (never collides with Wallhaven ids)")

        // --- interleave mixes pools instead of stacking ---
        var mixed = W.interleave(["a1", "a2", "a3"], ["b1"])
        if (mixed.join(",") !== "a1,b1,a2,a3")
            throw new Error("interleave must alternate then drain, got " + mixed.join(","))

        // --- state lifecycle: fresh state has more; exhausted state has none ---
        var st = W.freshState("  ", "nonsense")
        if (st.query !== "landscape" || st.sorting !== "relevance")
            throw new Error("freshState must default empty query + unknown sorting")
        if (!W.hasMore(st))
            throw new Error("a fresh state must report more available")
        st.whLastPage = 2; st.whPage = 3; st.koDone = true
        if (W.hasMore(st))
            throw new Error("both lanes exhausted must report no more")
        st.whPage = 2
        if (!W.hasMore(st))
            throw new Error("wallhaven pages remaining must report more")

        console.log("wallpaper_api_logic_harness: all checks passed")
    }
}
