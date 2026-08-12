import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault Browse face execution plan, Slice 4 — the two card components (VaultPosterCard 2:3,
// VaultWideCard 16:9), driven with plain JS row objects (no VaultLibrary needed — the components
// are UNWIRED, no page consumes them yet). Proves the design's card-state wardrobe (design
// §6.3): resolving shows the filename, not the title; the uncertainty mark is gold and nothing
// else on the card is; away disables hover and the open signal; artwork-missing falls back to
// the real title with no Image error; a very long real title elides to one line; the
// resolving→settled crossfade fires exactly once per faceState change; and card shape (2:3 vs
// 16:9) follows nodeType.
TestCase {
    name: "VaultCards"
    when: windowShown

    Window { id: testWindow; width: 900; height: 700; visible: true }

    Component { id: posterComp; Colosseum.VaultPosterCard {} }
    Component { id: wideComp; Colosseum.VaultWideCard {} }

    SignalSpy { id: openSpy; signalName: "openRequested" }
    SignalSpy { id: crossfadeSpy; signalName: "faceCrossfaded" }

    function cleanup() {
        openSpy.clear(); openSpy.target = null
        crossfadeSpy.clear(); crossfadeSpy.target = null
    }

    function createPoster(row) {
        var c = posterComp.createObject(testWindow, { row: row })
        verify(c !== null)
        wait(30)
        return c
    }
    function createWide(row) {
        var c = wideComp.createObject(testWindow, { row: row })
        verify(c !== null)
        wait(30)
        return c
    }
    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }

    // ── real rows, real titles (Hemanth's own library, per the ground-truth pins) ───────────
    readonly property var resolvingRow: ({
        "key": "batman-s02", "nodeType": "show",
        "displayTitle": "Batman - Cruzado Encapuzado S02 2026 WEB-DL 1080p x264 DUAL 5.1",
        "physicalFact": "", "path": "D:/hemanth's folder/Batman - Cruzado Encapuzado S02",
        "counts": { "items": 0 }, "coverRef": "", "state": "resolving", "away": false
    })
    readonly property var identifiedRow: ({
        "key": "spiderman", "nodeType": "film", "displayTitle": "Spider-Man: No Way Home",
        "physicalFact": "2021 · 1080p", "path": "D:/hemanth's folder/Spider-Man No Way Home (2021)",
        "counts": { "items": 1 }, "coverRef": "", "state": "identified", "away": false
    })
    readonly property var uncertainRow: ({
        "key": "justice-league", "nodeType": "show", "displayTitle": "Justice League",
        "physicalFact": "Two shows may match", "path": "D:/hemanth's folder/Justice League",
        "counts": { "items": 2 }, "coverRef": "", "state": "uncertain", "away": false
    })
    readonly property var awayRow: ({
        "key": "loki", "nodeType": "show", "displayTitle": "Loki",
        "physicalFact": "Drive not connected", "path": "D:/hemanth's folder/Loki",
        "counts": { "items": 2 }, "coverRef": "", "state": "identified", "away": true
    })
    readonly property string longTitle: "Shubman Gill 126(177) Vs Afghanistan Only Test 2026 Ball By Ball"
    readonly property var noArtLongTitleRow: ({
        "key": "shubman-gill", "nodeType": "clip", "displayTitle": longTitle,
        "physicalFact": "Yours only", "path": "D:/hemanth's folder/Cricket/Shubman Gill.mp4",
        "counts": { "items": 0 }, "coverRef": "", "state": "localOnly", "away": false
    })

    // ── 1a. resolving shows the filename, NOT the title ─────────────────────────────────────
    function test_resolving_shows_filename_not_title() {
        var c = createPoster(resolvingRow)
        compare(c.faceState, "filename")
        var fn = findChild(c, "vaultBrowseCard_batman-s02_filename")
        var title = findChild(c, "vaultBrowseCard_batman-s02_title")
        verify(fn !== null); verify(title !== null)
        compare(fn.text, resolvingRow.displayTitle)
        compare(fn.opacity, 1)
        verify(title.text !== resolvingRow.displayTitle)
        compare(title.text, "Resolving…")
        c.destroy()
    }

    // ── 1b. uncertain shows the gold mark, and ONLY the mark is gold ────────────────────────
    function test_uncertain_shows_gold_mark() {
        var c = createPoster(uncertainRow)
        compare(c.indicatorKind, "uncertain")
        verify(c.showIndicator)
        var art = findChild(c, "vaultBrowseCard_justice-league_art")
        verify(art !== null)
        compare(art.border.width, 1)     // the gold ring the mock calls "unsure"
        var title = findChild(c, "vaultBrowseCard_justice-league_title")
        compare(title.text, "Justice League")
        c.destroy()
    }

    // ── 1c. away disables hover and the open signal ─────────────────────────────────────────
    function test_away_disables_hover_and_open_signal() {
        var c = createPoster(awayRow)
        openSpy.target = c
        compare(c.indicatorKind, "away")
        compare(c.opacity, 0.62)          // reduced ink
        var art = findChild(c, "vaultBrowseCard_loki_art")
        verify(art !== null)
        var hitArea = findChild(c, "vaultBrowseCard_loki_hitArea")
        verify(hitArea !== null)
        verify(!hitArea.enabled)          // disables hover (and click) structurally
        mouseClick(art)
        compare(openSpy.count, 0)         // no open signal while away
        c.destroy()
    }

    // ── 1d. no-art shows the real title, never a broken-image glyph ────────────────────────
    function test_no_art_shows_title_text_no_image_error() {
        var c = createPoster(noArtLongTitleRow)
        compare(c.coverRef, "")
        var title = findChild(c, "vaultBrowseCard_shubman-gill_title")
        verify(title !== null)
        compare(title.text, longTitle)
        var img = findChild(c, "vaultBrowseCard_shubman-gill_artImage")
        verify(img !== null)
        compare(img.source.toString(), "")     // empty coverRef never attempts a load
        verify(img.status !== Image.Error)
        c.destroy()
    }

    // ── 2. title elision at one line, the real long filename ───────────────────────────────
    function test_title_elides_to_one_line_for_long_title() {
        var c = createPoster(noArtLongTitleRow)
        var title = findChild(c, "vaultBrowseCard_shubman-gill_title")
        verify(title !== null)
        compare(title.maximumLineCount, 1)
        compare(title.elide, Text.ElideRight)
        verify(title.truncated)            // 150px card, ~68-char real title: must elide
        c.destroy()
    }

    // ── 3. the resolving -> settled crossfade fires EXACTLY once per faceState change ──────
    function test_crossfade_fires_exactly_once_per_face_state_change() {
        var c = createPoster(resolvingRow)
        crossfadeSpy.target = c
        compare(crossfadeSpy.count, 0)
        compare(c.faceState, "filename")

        c.row = identifiedRow               // resolving -> identified: one settle
        tryCompare(crossfadeSpy, "count", 1, 600)
        compare(c.faceState, "settled")

        c.row = uncertainRow                 // identified -> uncertain: faceState stays "settled",
        wait(300)                            // so the Behavior never runs again — spy must not grow
        compare(crossfadeSpy.count, 1)

        c.row = resolvingRow                 // settled -> resolving: one more settle (the reverse)
        tryCompare(crossfadeSpy, "count", 2, 600)
        compare(c.faceState, "filename")
        c.destroy()
    }

    // ── 4. poster vs wide aspect is selected by nodeType ────────────────────────────────────
    function test_poster_card_is_2_to_3_for_container_node_types() {
        var c = createPoster(identifiedRow)   // nodeType: film
        var art = findChild(c, "vaultBrowseCard_spiderman_art")
        verify(art !== null)
        verify(Math.abs((art.height / art.width) - 1.5) < 0.02)
        c.destroy()
    }
    function test_wide_card_is_16_to_9_for_episode_and_clip_node_types() {
        var episodeRow = {
            "key": "gintama-e3", "nodeType": "episode", "displayTitle": "Gintama",
            "physicalFact": "S1:E3 · 1080p", "path": "D:/hemanth's folder/Gintama/Season 1/e3.mkv",
            "counts": { "items": 0 }, "coverRef": "", "state": "identified", "away": false
        }
        var c = createWide(episodeRow)
        var art = findChild(c, "vaultBrowseCard_gintama-e3_art")
        verify(art !== null)
        verify(Math.abs((art.height / art.width) - (9 / 16)) < 0.02)
        // episode nodes never badge a plain item count (design §6.3) — confirms the selector's
        // choice mattered, not just the aspect ratio.
        compare(c.showIndicator, false)
        c.destroy()
    }
}
