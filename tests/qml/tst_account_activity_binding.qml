// Slice D10 — bind Your Colosseum Monthly Portrait to ProfileActivity, CPP-PORT-CONTRACT.md
// arcs/02-profile-account-centre/activity-engine/reference sections 12/13/14/24.
//
// qml/account/AccountCenter.qml itself is not loaded here (it references ProfileActivity, a
// C++ context property only injected by the real app run and by native/main.cpp's
// qmlRegisterType() calls this generic QuickTest runner cannot resolve — the same rule already
// documented in tst_player1_activity.qml/tst_watchparty_source_provenance.qml for other
// context-property-dependent production files). Instead this suite:
//
//   1. tests qml/AccountActivityFormat.js's pure functions directly (the formatting rules a
//      human reviewer can read verbatim against the contract sections above), and
//   2. proves the section 14/24 "one projectMonth call per month/revision, never per metric
//      read" cache rule with a small local QML mirror of AccountCenter.qml's own
//      colosseumProjection/dependent-metric shape, driven by a recording fake ActivityStore —
//      this is possible ONLY because the null-guard/revision-read/projectMonth-call algorithm
//      lives in ONE shared function (AccountActivityFormat.projectionFor) that AccountCenter.qml
//      itself calls, so this test exercises the SAME code path, not a parallel reimplementation
//      that could drift.
import QtQuick 2.15
import QtTest 1.3
import "../../qml/AccountActivityFormat.js" as Format

TestCase {
    id: testCase
    name: "AccountActivityBinding"

    // ---- fixtures -----------------------------------------------------------------------

    Component {
        id: fakeStoreComponent
        QtObject {
            property int revision: 0
            property var responses: ({})
            property var callLog: []
            function projectMonth(monthKey) {
                callLog.push(monthKey)
                return responses[monthKey] !== undefined ? responses[monthKey] : {}
            }
        }
    }

    // Mirrors AccountCenter.qml's own shape: ONE cached projection property, several
    // independent dependent metric properties that read fields off it — never call
    // projectMonth() themselves.
    Component {
        id: hostComponent
        QtObject {
            property var store: null
            property string monthKey: "2026-08"
            property var projection: Format.projectionFor(store, monthKey)
            property string watchTimeText: Format.durationText(projection.watchSeconds)
            property string pagesReadText: Format.countText(projection.pagesRead)
            property string completedText: Format.countText(projection.completedCount)
            property string activeDaysText: Format.countText(projection.activeDays)
            property var highlights: Format.formatHighlights(projection.highlights)
            property var recentActivity: Format.formatRecentActivity(projection.recentActivity)
        }
    }

    // ---- month key helpers ---------------------------------------------------------------

    function test_current_month_key_matches_real_clock() {
        var now = new Date()
        var expectedMonth = now.getMonth() + 1
        var expected = now.getFullYear() + "-" + (expectedMonth < 10 ? "0" + expectedMonth : String(expectedMonth))
        compare(Format.currentMonthKey(), expected)
    }

    function test_month_name_and_year() {
        compare(Format.monthName("2026-08"), "August")
        compare(Format.monthYear("2026-08"), "2026")
        compare(Format.monthName("2026-01"), "January")
        compare(Format.monthName("2026-12"), "December")
    }

    function test_month_name_and_year_invalid_key_is_empty() {
        compare(Format.monthName(""), "")
        compare(Format.monthYear(undefined), "")
        compare(Format.monthName("2026-13"), "")
        compare(Format.monthName("2026-8"), "")
    }

    function test_shift_month_key_within_year() {
        compare(Format.shiftMonthKey("2026-08", 1), "2026-09")
        compare(Format.shiftMonthKey("2026-08", -1), "2026-07")
    }

    function test_shift_month_key_wraps_year_boundary() {
        compare(Format.shiftMonthKey("2026-12", 1), "2027-01")
        compare(Format.shiftMonthKey("2026-01", -1), "2025-12")
    }

    // ---- month navigation bounds (section 12) --------------------------------------------

    function test_next_month_enabled_only_while_selected_before_current() {
        compare(Format.nextMonthEnabled("2026-07", "2026-08"), true)
        compare(Format.nextMonthEnabled("2026-08", "2026-08"), false)
        compare(Format.nextMonthEnabled("2026-09", "2026-08"), false)
    }

    function test_next_month_enabled_false_for_invalid_keys() {
        compare(Format.nextMonthEnabled("", "2026-08"), false)
        compare(Format.nextMonthEnabled("2026-08", ""), false)
    }

    function test_previous_month_enabled_true_with_no_earliest_bound() {
        // Section 12/23: an empty/unknown earliest month must not lock previous navigation —
        // ActivityStore may simply not have reported one yet.
        compare(Format.previousMonthEnabled("2026-08", ""), true)
    }

    function test_previous_month_enabled_clamps_at_earliest() {
        compare(Format.previousMonthEnabled("2026-03", "2026-01"), true)
        compare(Format.previousMonthEnabled("2026-01", "2026-01"), false)
        compare(Format.previousMonthEnabled("2025-12", "2026-01"), false)
    }

    // ---- duration/count/percent text -------------------------------------------------------

    function test_duration_text_boundary_seconds() {
        compare(Format.durationText(0), "0m")
        compare(Format.durationText(59), "0m")
        compare(Format.durationText(60), "1m")
        compare(Format.durationText(3599), "59m")
        compare(Format.durationText(3600), "1h 0m")
        // The contract's own worked example (section 14).
        compare(Format.durationText(134640), "37h 24m")
    }

    function test_duration_text_missing_is_dash() {
        compare(Format.durationText(undefined), "—")
        compare(Format.durationText(null), "—")
    }

    function test_count_text_groups_thousands_and_handles_missing() {
        compare(Format.countText(1284), "1,284")
        compare(Format.countText(9), "9")
        compare(Format.countText(0), "0")
        compare(Format.countText(1000000), "1,000,000")
        compare(Format.countText(undefined), "—")
        compare(Format.countText(null), "—")
    }

    function test_percent_text_from_progress_micros() {
        compare(Format.percentText(0), "0%")
        compare(Format.percentText(420000), "42%")
        compare(Format.percentText(1000000), "100%")
        compare(Format.percentText(undefined), "—")
    }

    // ---- highlight role -> product copy (section 13/14) ------------------------------------

    function test_highlight_theatre_role() {
        var h = Format.formatHighlight({
            "role": "theatre", "title": "A Show", "kind": "episode", "watchSeconds": 4680
        })
        compare(h.title, "A Show")
        compare(h.label, "Most watched")
        compare(h.value, "1h 18m")
    }

    function test_highlight_tankoban_role_kind_specific_copy() {
        var manga = Format.formatHighlight({
            "role": "tankoban", "title": "A Manga", "kind": "manga_chapter", "pagesRead": 412
        })
        compare(manga.label, "Most read manga")
        compare(manga.value, "412 pages")

        var comic = Format.formatHighlight({
            "role": "tankoban", "title": "A Comic", "kind": "comic_issue", "pagesRead": 1
        })
        compare(comic.label, "Most read comics")
        compare(comic.value, "1 page")

        var volume = Format.formatHighlight({
            "role": "tankoban", "title": "A Volume Series", "kind": "tankoban_volume", "pagesRead": 30
        })
        compare(volume.label, "Most read")
        compare(volume.value, "30 pages")
    }

    // Section 13/14's explicit reflowable rule: progressMicros must never be labelled as
    // literal physical pages.
    function test_highlight_biblio_ebook_role_fixed_uses_pages() {
        var h = Format.formatHighlight({
            "role": "biblio_ebook", "title": "A Fixed Book", "kind": "book",
            "pagesRead": 268, "progressMicros": 500000
        })
        compare(h.label, "Most read book")
        compare(h.value, "268 pages")
    }

    function test_highlight_biblio_ebook_role_reflowable_uses_percent_not_pages() {
        var h = Format.formatHighlight({
            "role": "biblio_ebook", "title": "A Reflowable Book", "kind": "book",
            "pagesRead": 0, "progressMicros": 340000
        })
        compare(h.label, "Most read book")
        compare(h.value, "34%")
        verify(h.value.indexOf("page") === -1)
    }

    function test_highlight_audiobook_role() {
        var h = Format.formatHighlight({
            "role": "audiobook", "title": "An Audiobook", "kind": "audiobook", "listenSeconds": 7260
        })
        compare(h.label, "Most listened")
        compare(h.value, "2h 1m")
    }

    function test_highlight_completion_role_pluralizes_unit() {
        var one = Format.formatHighlight({
            "role": "completion", "title": "A Movie", "kind": "movie", "completedCount": 1
        })
        compare(one.label, "Completed")
        compare(one.value, "1 title")

        var many = Format.formatHighlight({
            "role": "completion", "title": "A Volume Series", "kind": "tankoban_volume", "completedCount": 3
        })
        compare(many.value, "3 volumes")
    }

    function test_highlight_recent_fallback_role_uses_best_available_metric() {
        var watched = Format.formatHighlight({
            "role": "recent", "title": "Second Watched", "kind": "movie", "watchSeconds": 600
        })
        compare(watched.label, "Recently active")
        compare(watched.value, "10m")

        var noMetric = Format.formatHighlight({
            "role": "recent", "title": "Nothing Qualifying", "kind": "movie"
        })
        compare(noMetric.value, "—")
    }

    function test_format_highlights_maps_array_and_defaults_non_array() {
        var list = Format.formatHighlights([
            { "role": "theatre", "title": "X", "kind": "movie", "watchSeconds": 60 }
        ])
        compare(list.length, 1)
        compare(list[0].title, "X")
        compare(Format.formatHighlights(undefined).length, 0)
        compare(Format.formatHighlights(null).length, 0)
    }

    // ---- recent activity row mapping -------------------------------------------------------

    function test_recent_activity_row_completed_uses_finished_copy() {
        var row = Format.formatActivityRow({
            "localDate": "2026-08-16", "title": "Blue Show", "world": "theatre",
            "itemLabel": "Episode 8", "completed": true, "verb": "watched"
        })
        compare(row.date, "Aug 16")
        compare(row.title, "Blue Show")
        compare(row.meta, "Finished Episode 8")
        compare(row.world, "Theatre")
    }

    function test_recent_activity_row_watched_uses_duration() {
        var row = Format.formatActivityRow({
            "localDate": "2026-08-01", "title": "A Movie", "world": "theatre",
            "verb": "watched", "watchSeconds": 3720, "completed": false
        })
        compare(row.meta, "Watched 1h 2m")
    }

    function test_recent_activity_row_listened_uses_duration() {
        var row = Format.formatActivityRow({
            "localDate": "2026-08-01", "title": "An Audiobook", "world": "biblio",
            "verb": "listened", "listenSeconds": 900, "completed": false
        })
        compare(row.meta, "Listened 15m")
        compare(row.world, "Biblio")
    }

    function test_recent_activity_row_read_uses_pages_then_percent() {
        var pages = Format.formatActivityRow({
            "localDate": "2026-08-01", "title": "A Manga", "world": "tankoban",
            "verb": "read", "pagesRead": 1, "progressMicros": 0, "completed": false
        })
        compare(pages.meta, "Read 1 page")

        var percent = Format.formatActivityRow({
            "localDate": "2026-08-01", "title": "A Reflowable Book", "world": "biblio",
            "verb": "read", "pagesRead": 0, "progressMicros": 120000, "completed": false
        })
        compare(percent.meta, "Read 12%")
    }

    function test_format_recent_activity_maps_array_and_defaults_non_array() {
        var rows = Format.formatRecentActivity([
            { "localDate": "2026-08-16", "title": "X", "world": "theatre", "verb": "watched", "watchSeconds": 60 }
        ])
        compare(rows.length, 1)
        compare(Format.formatRecentActivity(undefined).length, 0)
        compare(Format.formatRecentActivity(null).length, 0)
    }

    // ---- empty projection -> em-dash defaults (section 23/25) ------------------------------

    function test_empty_projection_formats_to_dashes_and_empty_arrays() {
        var empty = ({})
        compare(Format.durationText(empty.watchSeconds), "—")
        compare(Format.countText(empty.pagesRead), "—")
        compare(Format.countText(empty.completedCount), "—")
        compare(Format.countText(empty.activeDays), "—")
        compare(Format.formatHighlights(empty.highlights).length, 0)
        compare(Format.formatRecentActivity(empty.recentActivity).length, 0)
    }

    // ---- projection-cache wiring mirror (section 14/24) ------------------------------------

    function test_projection_cache_one_call_per_month_revision_never_per_metric_read() {
        var fake = fakeStoreComponent.createObject(testCase, {
            "responses": {
                "2026-08": { "watchSeconds": 120, "pagesRead": 4 },
                "2026-09": { "watchSeconds": 60 }
            }
        })
        verify(fake !== null)
        var host = hostComponent.createObject(testCase, { "store": fake, "monthKey": "2026-08" })
        verify(host !== null)
        wait(0)

        compare(fake.callLog.length, 1)
        compare(fake.callLog[0], "2026-08")

        // Reading every dependent metric property must NOT call projectMonth again — they all
        // read the cached `projection`, never the store directly.
        var touch = host.watchTimeText + host.pagesReadText + host.completedText +
            host.activeDaysText + JSON.stringify(host.highlights) +
            JSON.stringify(host.recentActivity)
        compare(touch.length > 0, true)
        compare(fake.callLog.length, 1)

        // A revision bump for the SAME month re-projects exactly once.
        fake.revision = fake.revision + 1
        wait(0)
        compare(fake.callLog.length, 2)
        compare(fake.callLog[1], "2026-08")

        // A month change re-projects exactly once, with no revision bump required.
        host.monthKey = "2026-09"
        wait(0)
        compare(fake.callLog.length, 3)
        compare(fake.callLog[2], "2026-09")
        compare(host.watchTimeText, "1m")

        host.destroy()
        fake.destroy()
    }

    function test_projection_for_missing_store_returns_empty_object_without_calling() {
        var result = Format.projectionFor(null, "2026-08")
        compare(JSON.stringify(result), JSON.stringify({}))
    }
}
