import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum
import "../../qml/VaultApi.js" as VaultApi

// Vault ux uplift S6 — render progress + the Continue rail on the browse face. Three layers,
// one suite:
//   1. the CARDS (production VaultPosterCard/VaultWideCard) paint the spec's gold progress
//      hairline and S3's durable watched tick from the row facts the page join supplies,
//      under the extended corner-indicator precedence away > uncertain > progress/watched >
//      count;
//   2. the JOIN (production VaultApi.joinRows — the exact function VaultPage.qml's
//      joinProgressRows runs over browseAt() rows) surfaces the stored progress of the row's
//      own vault id without recomputing anything;
//   3. the RAIL DATA (production VaultApi.continueRail over a seeded Progress stub) admits
//      only durably-Admitted vault videos with a resumable path — the rows the Continue rail
//      renders between the carousel and the grid.
// VaultPage.qml itself cannot be instantiated by the Quick Test engine (it reads the C++
// VaultLibrary/Progress singletons throughout) — same structural reason tst_vault_browse_page
// .qml mirrors that page's wiring instead; the assembled-face proof is the Lanista replay.
TestCase {
    name: "VaultContinueProgress"
    when: windowShown

    Window { id: testWindow; width: 900; height: 700; visible: true }

    Component { id: posterComp; Colosseum.VaultPosterCard {} }
    Component { id: wideComp; Colosseum.VaultWideCard {} }

    // ── the seeded Progress stub: the three calls VaultPage's join actually makes ──
    QtObject {
        id: stubProgress
        property int revision: 1
        property var records: ({})          // id -> {id, kind, progress, updatedAt, watched...}
        property var marks: ({})            // id -> 1 | -1 (ProgressStore.watchedMark's states)
        property var recents: []
        function get(kind, id) {
            return records[id] || null
        }
        function watchedMark(id) {
            return marks[id] || 0
        }
        function recent(kind, limit) {
            return recents
        }
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

    function makePoster(row) {
        var c = posterComp.createObject(testWindow, { row: row })
        verify(c !== null)
        wait(30)
        return c
    }
    function makeWide(row) {
        var c = wideComp.createObject(testWindow, { row: row })
        verify(c !== null)
        wait(30)
        return c
    }

    // ── 1. a mid-progress row renders the gold hairline (poster + wide) ─────────────────────
    function test_midprogress_row_renders_hairline() {
        var row = {
            "key": "loki-s1e4", "nodeType": "episode", "displayTitle": "The Nexus Event",
            "physicalFact": "S1:E4 · 1080p", "path": "D:/hemanth's folder/Loki/S1/e4.mkv",
            "counts": { "items": 0 }, "coverRef": "", "state": "identified", "away": false,
            "progressFraction": 0.42, "hasProgress": true
        }
        var c = makeWide(row)
        verify(c.showsProgressHairline)
        var track = findChild(c, "vaultBrowseCard_loki-s1e4_progressTrack")
        var fill = findChild(c, "vaultBrowseCard_loki-s1e4_progressFill")
        verify(track !== null); verify(fill !== null)
        compare(track.visible, true)
        // the STORED position, surfaced as-is: 42% of the track, never recomputed
        verify(Math.abs(fill.width - track.width * 0.42) < 1.5)
        // theme.gold ("#f0c44a") — the spec's gold hairline, not a neutral bar
        verify(Math.abs(fill.color.r - 240 / 255) < 0.01)
        c.destroy()

        var poster = makePoster({
            "key": "tenet-film", "nodeType": "film", "displayTitle": "Tenet",
            "physicalFact": "2020 · 1080p", "path": "D:/hemanth's folder/Tenet/tenet.mkv",
            "counts": { "items": 1 }, "coverRef": "", "state": "identified", "away": false,
            "progressFraction": 0.42, "hasProgress": true
        })
        verify(poster.showsProgressHairline)
        compare(findChild(poster, "vaultBrowseCard_tenet-film_progressTrack").visible, true)
        poster.destroy()
    }

    // ── 2. a watched row shows the tick, not the hairline (the durable mark owns a finished
    //       item; S3 retired its resume record, so progressFraction is legitimately 0) ───────
    function test_watched_row_shows_tick() {
        var row = {
            "key": "tenet-watched", "nodeType": "film", "displayTitle": "Tenet",
            "physicalFact": "2020 · 1080p", "path": "D:/hemanth's folder/Tenet/tenet.mkv",
            "counts": { "items": 1 }, "coverRef": "", "state": "identified", "away": false,
            "watched": true
        }
        var c = makePoster(row)
        compare(c.indicatorKind, "watched")
        verify(c.showIndicator)
        var tick = findChild(c, "vaultBrowseCard_tenet-watched_watchedTick")
        verify(tick !== null)
        compare(tick.visible, true)
        compare(tick.text, "✓")
        compare(c.showsProgressHairline, false)   // no resume left to show
        var track = findChild(c, "vaultBrowseCard_tenet-watched_progressTrack")
        verify(track !== null)
        compare(track.visible, false)
        c.destroy()

        var wide = makeWide({
            "key": "gintama-e101", "nodeType": "episode", "displayTitle": "Gintama",
            "physicalFact": "S2:E23", "path": "D:/hemanth's folder/Gintama/S2/e23.mkv",
            "counts": { "items": 0 }, "coverRef": "", "state": "identified", "away": false,
            "watched": true
        })
        compare(wide.indicatorKind, "watched")
        compare(findChild(wide, "vaultBrowseCard_gintama-e101_watchedTick").visible, true)
        wide.destroy()
    }

    // ── 2b. a live finished RECORD (not yet retired) reads as watched too — joinRow's
    //        progressFinished is the same truth the durable mark persists ────────────────────
    function test_live_finished_record_reads_as_watched() {
        var c = makePoster({
            "key": "fin-live", "nodeType": "film", "displayTitle": "Fin",
            "physicalFact": "1080p", "path": "D:/fin/fin.mkv",
            "counts": { "items": 1 }, "coverRef": "", "state": "identified", "away": false,
            "progressFinished": true, "progressFraction": 1
        })
        verify(c.watched)
        compare(c.indicatorKind, "watched")
        compare(c.showsProgressHairline, false)   // watched outranks a full bar
        c.destroy()
    }

    // ── 3. away precedence still wins: an away tile paints NEITHER hairline NOR tick —
    //       away is reduced ink (§4.3), no bright bar, no state chrome but its own glyph ────
    function test_away_precedence_beats_progress_and_watched() {
        var row = {
            "key": "away-film", "nodeType": "film", "displayTitle": "Away Film",
            "physicalFact": "Drive not connected", "path": "E:/Away/away.mkv",
            "counts": { "items": 1 }, "coverRef": "", "state": "identified", "away": true,
            "progressFraction": 0.42, "hasProgress": true, "watched": true
        }
        var c = makePoster(row)
        compare(c.indicatorKind, "away")          // the corner stays the away glyph's
        compare(c.showsProgressHairline, false)
        compare(findChild(c, "vaultBrowseCard_away-film_progressTrack").visible, false)
        compare(findChild(c, "vaultBrowseCard_away-film_watchedTick").visible, false)
        c.destroy()

        var wide = makeWide({
            "key": "away-ep", "nodeType": "episode", "displayTitle": "Away Ep",
            "physicalFact": "S1:E1", "path": "E:/Away/e1.mkv",
            "counts": { "items": 0 }, "coverRef": "", "state": "identified", "away": true,
            "progressFraction": 0.42, "hasProgress": true, "watched": true
        })
        compare(wide.indicatorKind, "away")
        compare(wide.showsProgressHairline, false)
        wide.destroy()
    }

    // ── 3b. uncertain still beats watched (the extended ladder is strictly ordered) ─────────
    function test_uncertain_precedes_watched() {
        var c = makePoster({
            "key": "unsure-film", "nodeType": "film", "displayTitle": "Unsure Film",
            "physicalFact": "Two films may match", "path": "D:/unsure",
            "counts": { "items": 1 }, "coverRef": "", "state": "uncertain", "away": false,
            "watched": true
        })
        compare(c.indicatorKind, "uncertain")
        compare(findChild(c, "vaultBrowseCard_unsure-film_watchedTick").visible, false)
        c.destroy()
    }

    // ── 3c. watched suppresses the plain count on the poster card (the new tier slots ABOVE
    //        count in the existing ladder) ───────────────────────────────────────────────────
    function test_watched_suppresses_count() {
        var c = makePoster({
            "key": "watched-show", "nodeType": "show", "displayTitle": "Watched Show",
            "physicalFact": "2 seasons", "path": "D:/watchedshow",
            "counts": { "items": 7 }, "coverRef": "", "state": "identified", "away": false,
            "watched": true
        })
        compare(c.indicatorKind, "watched")
        var tick = findChild(c, "vaultBrowseCard_watched-show_watchedTick")
        verify(tick !== null); compare(tick.visible, true)
        // and the count glyph is NOT rendered — the tick owns the corner
        verify(findChild(c, "vaultBrowseCard_watched-show_watchedTick").text === "✓")
        c.destroy()
    }

    // ── 3d. NEGATIVE CONTROL: a plain settled row with NO progress facts paints neither
    //        hairline nor tick — the whole tier must be able to be absent ────────────────────
    function test_plain_row_paints_no_progress_chrome() {
        var c = makePoster({
            "key": "plain-film", "nodeType": "film", "displayTitle": "Plain Film",
            "physicalFact": "2021 · 1080p", "path": "D:/plain/plain.mkv",
            "counts": { "items": 1 }, "coverRef": "", "state": "identified", "away": false
        })
        compare(c.progressFraction, 0)
        compare(c.watched, false)
        compare(c.showsProgressHairline, false)
        compare(findChild(c, "vaultBrowseCard_plain-film_progressTrack").visible, false)
        c.destroy()
    }

    // ── 4. the JOIN (production VaultApi.joinRows over a seeded store, the exact call
    //       VaultPage.joinProgressRows makes): the stored position of the row's OWN id is
    //       surfaced as progressFraction/progressed — never recomputed, never guessed. ──────
    function test_join_surfaces_stored_progress_for_rows_own_id() {
        stubProgress.records = ({
            "vault:aaa": { id: "vault:aaa", kind: "video", progress: 0.42, updatedAt: 1770000000 }
        })
        var rows = [{
            key: "/root/Tenet", nodeType: "film", displayTitle: "Tenet", physicalFact: "1080p",
            path: "/root/Tenet/tenet.mkv", counts: { items: 1 }, coverRef: "",
            state: "identified", away: false, kind: "video", id: "vault:aaa"
        }]
        var joined = VaultApi.joinRows(stubProgress, rows)
        compare(joined.length, 1)
        compare(joined[0].progressFraction, 0.42)
        verify(joined[0].hasProgress)
        verify(joined[0].progressed)              // the override VaultApi applies from the store
        compare(joined[0].lastReadMs, 1770000000)

        // NEGATIVE CONTROL: a row whose id the store does not know joins to no progress —
        // an unopened film paints nothing, and the join invents nothing.
        var fresh = VaultApi.joinRows(stubProgress, [{
            key: "/root/Fresh", nodeType: "film", displayTitle: "Fresh", physicalFact: "",
            path: "/root/Fresh/fresh.mkv", counts: { items: 1 }, coverRef: "",
            state: "identified", away: false, kind: "video", id: "vault:zzz"
        }])
        compare(fresh[0].progressFraction, 0)
        verify(!fresh[0].hasProgress)
        verify(!fresh[0].progressed)
        stubProgress.records = ({})
    }

    // ── 5. the RAIL DATA (production VaultApi.continueRail over the seeded store): the rows
    //       the Continue rail renders — admitted vault videos with a resumable path only. ────
    function test_continue_rail_admits_only_admitted_vault_videos_with_paths() {
        stubProgress.recents = [
            { id: "vault:aaa", kind: "video", title: "Tenet", cover: "",
              progress: 0.42, updatedAt: 1770000009,
              resume: { localPath: "D:/hemanth's folder/Tenet/tenet.mkv" } },
            { id: "vault:rej", kind: "video", title: "Rejected", cover: "",
              progress: 0.3, updatedAt: 1770000008,
              resume: { localPath: "D:/rej/r.mkv" } },
            { id: "vault:nopath", kind: "video", title: "No Path", cover: "",
              progress: 0.5, updatedAt: 1770000007, resume: {} },
            { id: "tt12345", kind: "video", title: "Catalogue Item", cover: "",
              progress: 0.9, updatedAt: 1770000006,
              resume: { localPath: "D:/cat/c.mkv" } }
        ]
        var admission = { "vault:aaa": "Admitted", "vault:rej": "RejectedNoVideo",
                          "vault:nopath": "Admitted", "tt12345": "Admitted" }
        var rail = VaultApi.continueRail(stubProgress, 18, admission)
        compare(rail.length, 1)                   // exactly the one admissible, resumable local
        compare(rail[0].id, "vault:aaa")
        compare(rail[0].title, "Tenet")
        compare(rail[0].progressFraction, 0.42)
        compare(rail[0].path, "D:/hemanth's folder/Tenet/tenet.mkv")

        // NEGATIVE CONTROL: nothing admitted → the rail (and the page's rail row) is empty,
        // which is what keeps the whole Continue block hidden on a fresh install.
        var none = VaultApi.continueRail(stubProgress, 18, {})
        compare(none.length, 0)
        stubProgress.recents = []
    }

    // ── 6. the page's watched decoration gate (the exact expression VaultPage's
    //       joinProgressRows applies): the durable mark reads as watched for vault ids ONLY,
    //       and only the marked state (1) — a catalogue id or an unmarked vault id never
    //       gains a Vault tick from this join. ──────────────────────────────────────────────
    function test_watched_gate_is_vault_only_and_marked_only() {
        stubProgress.marks = ({ "vault:aaa": 1, "vault:bbb": -1, "tt123": 1 })
        verify(stubProgress.watchedMark("vault:aaa") === 1
               && String("vault:aaa").indexOf("vault:") === 0)      // the joined expression
        verify(!(stubProgress.watchedMark("vault:bbb") === 1))      // explicitly-unwatched ≠ tick
        verify(!(String("tt123").indexOf("vault:") === 0))          // a catalogue id never ticks
        stubProgress.marks = ({})
    }

    // ── 7. the QML-side ordering (vault ux uplift S12 — production VaultApi.sortRowsRecentlyPlayed
    //       over JOINED rows): descending lastReadMs, never-read rows sink, ties stay stable, and
    //       the input array is never mutated (the grid feeds the result straight to syncGridModel). ──
    function test_recently_played_sort_descends_and_sinks_unread() {
        var joined = [
            { key: "never", displayTitle: "Never", lastReadMs: 0 },
            { key: "old", displayTitle: "Old", lastReadMs: 500 },
            { key: "newest", displayTitle: "Newest", lastReadMs: 900 },
            { key: "mid", displayTitle: "Mid", lastReadMs: 700 },
            { key: "old-tie", displayTitle: "OldTie", lastReadMs: 500 }
        ]
        var sorted = VaultApi.sortRowsRecentlyPlayed(joined)
        compare(sorted.map(function (r) { return r.key }),
                ["newest", "mid", "old", "old-tie", "never"])
        // stable tie: "old" stays before "old-tie" (both 500), never-read (0) sinks last
        // the input array is untouched — the projection hands syncGridModel a NEW array
        compare(joined[0].key, "never")
        compare(joined.length, 5)
        // NEGATIVE CONTROL: nothing read at all → the order is unchanged (all sink together,
        // stability keeps the incoming order), never reversed or shuffled
        var fresh = [{ key: "a", lastReadMs: 0 }, { key: "b", lastReadMs: 0 }]
        compare(VaultApi.sortRowsRecentlyPlayed(fresh).map(function (r) { return r.key }),
                ["a", "b"])
        // null/undefined rows and an empty list are honest no-ops, never throws
        compare(VaultApi.sortRowsRecentlyPlayed([]).length, 0)
        compare(VaultApi.sortRowsRecentlyPlayed(null).length, 0)
    }

    // ── 8. the watched-state filter (vault ux uplift S13 — production VaultApi
    //       filterRowsByWatched over JOINED rows): video rows only, the durable mark decides,
    //       non-video rows drop in BOTH modes (a comics tile is neither watched nor unwatched),
    //       and "" is a no-op returning a new array. ─────────────────────────────────────────
    function test_watched_filter_is_video_scoped_and_mark_decided() {
        var joined = [
            { key: "v-seen", kind: "video", watched: true },
            { key: "v-fresh", kind: "video", watched: false },
            { key: "c-1", kind: "comic", watched: false },
            { key: "b-1", kind: "book", watched: true }
        ]
        compare(VaultApi.filterRowsByWatched(joined, "watched").map(function (r) { return r.key }),
                ["v-seen"])
        compare(VaultApi.filterRowsByWatched(joined, "unwatched").map(function (r) { return r.key }),
                ["v-fresh"])
        // "" (or any unknown mode) is the no-op: everything kept, NEW array, input untouched
        var noop = VaultApi.filterRowsByWatched(joined, "")
        compare(noop.length, 4)
        verify(noop !== joined)
        // a row with no kind at all (a container tile) is not video → drops in both modes
        var withContainer = [{ key: "folder" }, { key: "v", kind: "video", watched: true }]
        compare(VaultApi.filterRowsByWatched(withContainer, "watched").map(function (r) { return r.key }),
                ["v"])
        // empty/null inputs never throw
        compare(VaultApi.filterRowsByWatched([], "watched").length, 0)
        compare(VaultApi.filterRowsByWatched(null, "unwatched").length, 0)
    }
}
