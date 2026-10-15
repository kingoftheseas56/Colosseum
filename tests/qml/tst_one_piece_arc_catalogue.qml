import QtQuick
import QtTest
import "../../qml" as Colosseum
import "../../qml/OnePieceCatalogApi.js" as CatalogApi

TestCase {
    id: suite
    name: "OnePieceArcCatalogue"
    when: windowShown
    width: 1280
    height: 720

    property var catalogue: null
    property var owner: null
    property var replacementOwner: null
    property int episodeRequests: 0
    readonly property string localCover: String(Qt.resolvedUrl("../../assets/icons/books.svg"))

    Component {
        id: catalogueComponent
        Colosseum.OnePieceArcCatalogue { width: 1280; height: 720; reducedMotion: true }
    }
    Component {
        id: ownerComponent
        QtObject {
            property bool localReady: false
            property var rowsById: ({})
            property var requests: []
            property int reads: 0
            property int cancellations: 0
            property int activeMalId: 0
            property int revision: 0
            property string completion: "pending"
            signal readyChanged()
            signal seriesChanged(int malId)
            signal seriesFailed(int malId, string reason)
            function ready() { return localReady }
            function hasSeries(malId) { return (rowsById[malId] || []).length > 0 }
            function volumes(malId) { reads++; return rowsById[malId] || [] }
            function cancelRequest() { cancellations++ }
            function requestSeries(malId) {
                requests = requests.concat([malId])
                // The native owner has one request slot. Replacing it silently
                // cancels the previous series, without a terminal signal.
                activeMalId = malId
                if (completion === "success") {
                    activeMalId = 0
                    seriesChanged(malId)
                } else if (completion === "failure") {
                    activeMalId = 0
                    seriesFailed(malId, "OFFLINE")
                }
            }
            function publish(malId, rows) {
                if (activeMalId === malId) activeMalId = 0
                rowsById[malId] = rows
                revision++
                seriesChanged(malId)
            }
            function completeActive(rows) { publish(activeMalId, rows) }
        }
    }

    function arc(volumes) {
        return { id: "test-arc", title: "Test Arc", volumes: volumes,
                 anime: "1", liveActionSeason: 1, liveActionEpisodes: "1" }
    }
    function volumes() {
        return [{number: "1", name: "Remote One", cover: localCover},
                {number: "2", name: "Remote Two", cover: localCover},
                {number: "3", name: "Remote Three", cover: localCover}]
    }
    function create(properties) {
        var initial = {arc: arc("1-2"), backdrop: null, tankobanCatalogRef: owner}
        for (var key in (properties || {})) initial[key] = properties[key]
        catalogue = catalogueComponent.createObject(suite, initial)
        verify(catalogue !== null)
        return catalogue
    }
    function assertVolumes(numbers, title, cover) {
        compare(catalogue.volumeItems.map(function(row) { return row.number }).join(","), numbers)
        if (title !== undefined) compare(catalogue.volumeItems[0].title, title)
        if (cover !== undefined) compare(catalogue.volumeItems[0].cover, cover)
    }

    function init() {
        failOnWarning(/ReferenceError|TypeError|Binding loop detected|Cannot assign/)
        episodeRequests = 0
        CatalogApi.resetCache()
        // The real component and selection helpers run, but episode providers
        // are intercepted before XMLHttpRequest. Covers are local assets too.
        CatalogApi.setRequestAdapter(function(url, done) {
            episodeRequests++
            done(null)
        })
        owner = ownerComponent.createObject(suite)
    }
    function cleanup() {
        if (catalogue) catalogue.destroy()
        catalogue = null
        wait(0)
        owner.destroy()
        owner = null
        if (replacementOwner) replacementOwner.destroy()
        replacementOwner = null
        CatalogApi.resetRequestAdapter()
        CatalogApi.resetCache()
    }

    function test_initial_remote_request_is_once_and_keeps_fallback() {
        create()
        compare(owner.requests.length, 1)
        compare(owner.requests[0], 13)
        assertVolumes("1,2", "Volume 1", "")
        compare(episodeRequests, 2, "one initial request per intercepted episode provider")
        compare(owner.cancellations, 0)
    }
    function test_remote_success_populates_both_manga_lanes_without_local_database() {
        create()
        owner.publish(13, volumes())
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.ready(), false)
        compare(owner.requests.length, 1, "completion/revision must not request again")
        // Both live delegates use the same updated volume model and retain
        // ordinary/colored routing when activated.
        var cards = []
        function visit(item) {
            if (item.entry !== undefined && item.colorEdition !== undefined) cards.push(item)
            for (var i = 0; i < item.children.length; ++i) visit(item.children[i])
        }
        visit(catalogue)
        compare(cards.length, 4)
        var routed = []
        catalogue.mangaVolumeRequested.connect(function(colorEdition, number) {
            routed.push({colorEdition: colorEdition, number: number})
        })
        for (var i = 0; i < cards.length; ++i) {
            compare(cards[i].entry.cover, localCover)
            cards[i].activated()
        }
        compare(routed.filter(function(row) { return row.colorEdition }).length, 2)
        compare(routed.filter(function(row) { return !row.colorEdition }).length, 2)
        compare(routed.map(function(row) { return row.number }).join(","), "1,2,1,2")
    }
    function test_cached_startup_is_visible_before_refresh_finishes() {
        owner.rowsById = {13: volumes()}
        create()
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.requests.length, 1, "owner decides whether cache needs refreshing")
    }
    function test_series_completion_rebuilds_without_revision_signal() {
        create()
        owner.rowsById = {13: volumes()}
        owner.seriesChanged(13)
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.requests.length, 1)
    }
    function test_failed_refresh_keeps_cached_rows() {
        owner.rowsById = {13: volumes()}
        create()
        owner.seriesFailed(13, "OFFLINE")
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.requests.length, 1)
    }
    function test_uncached_failure_keeps_existing_fallback_without_retry_loop() {
        owner.completion = "failure"
        create()
        owner.revision++
        owner.readyChanged()
        assertVolumes("1,2", "Volume 1", "")
        compare(owner.requests.length, 1)
    }
    function test_synchronous_cached_completion_does_not_recurse() {
        owner.rowsById = {13: volumes()}
        owner.completion = "success"
        create()
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.requests.length, 1)
    }
    function test_arc_changes_reselect_pending_response_without_restarting_request() {
        create()
        catalogue.arc = arc("3")
        assertVolumes("3", "Volume 3", "")
        compare(owner.requests.length, 1)
        owner.publish(13, volumes())
        assertVolumes("3", "Remote Three", localCover)
        catalogue.arc = arc("2")
        assertVolumes("2", "Remote Two", localCover)
        catalogue.installedExtensions = [{id: "unrelated-addon", enabled: false}]
        compare(owner.requests.length, 1)
    }
    function test_unrelated_series_completion_and_failure_do_not_rebuild() {
        create()
        var reads = owner.reads
        owner.seriesChanged(99)
        owner.seriesFailed(99, "OFFLINE")
        compare(owner.reads, reads)
        assertVolumes("1,2", "Volume 1", "")
    }
    function test_revision_and_local_ready_changes_rebuild_without_requests() {
        create()
        owner.rowsById = {13: volumes()}
        owner.revision++
        assertVolumes("1,2", "Remote One", localCover)
        owner.rowsById = {13: [{number: "2", name: "Local Two", cover: localCover}]}
        owner.localReady = true
        owner.readyChanged()
        assertVolumes("2", "Local Two", localCover)
        compare(owner.requests.length, 1)
    }
    function test_local_catalogue_rows_remain_available() {
        owner.localReady = true
        owner.rowsById = {13: volumes()}
        owner.completion = "success"
        create()
        assertVolumes("1,2", "Remote One", localCover)
    }
    function test_null_arc_ignores_late_results_and_can_reopen() {
        create()
        catalogue.arc = null
        assertVolumes("")
        owner.publish(13, volumes())
        owner.seriesFailed(13, "LATE_FAILURE")
        assertVolumes("")
        catalogue.arc = arc("3")
        assertVolumes("3", "Remote Three", localCover)
        compare(owner.requests.length, 2, "re-entry revalidates the completed series through its owner")
        compare(owner.cancellations, 0)
    }
    function test_failed_request_can_retry_when_same_view_reenters_arc() {
        owner.completion = "failure"
        create()
        compare(owner.requests.length, 1)
        catalogue.arc = null
        catalogue.arc = arc("3")
        compare(owner.requests.length, 2)
        assertVolumes("3", "Volume 3", "")
        owner.publish(13, volumes())
        assertVolumes("3", "Remote Three", localCover)
        compare(owner.cancellations, 0)
    }
    function test_null_arc_reentry_reissues_once_and_ordinary_changes_do_not() {
        create()
        catalogue.arc = null
        catalogue.arc = arc("3")
        compare(owner.requests.length, 2)
        catalogue.arc = arc("3")
        catalogue.installedExtensions = []
        compare(owner.requests.length, 2)
        owner.completeActive(volumes())
        assertVolumes("3", "Remote Three", localCover)
    }
    function test_explicit_refresh_revalidates_settled_series_without_callback_loop() {
        owner.rowsById = {13: volumes()}
        owner.completion = "success"
        create()
        catalogue.refresh()
        compare(owner.requests.length, 2)
        assertVolumes("1,2", "Remote One", localCover)
        owner.revision++
        owner.seriesChanged(13)
        catalogue.arc = arc("3")
        catalogue.installedExtensions = []
        compare(owner.requests.length, 2)
    }
    function test_explicit_refresh_reissues_pending_once_without_signal_loop() {
        owner.completion = "failure"
        create()
        owner.completion = "pending"
        catalogue.refresh()
        compare(owner.requests.length, 2)
        catalogue.refresh()
        compare(owner.requests.length, 3)
        owner.completeActive(volumes())
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.requests.length, 3)
    }
    function test_explicit_refresh_recovers_request_silently_superseded_by_other_consumer() {
        create()
        owner.requestSeries(99)
        // No seriesChanged/seriesFailed for 13: its request was canceled by
        // the shared owner's other consumer, while this view remained alive.
        catalogue.refresh()
        compare(owner.requests.join(","), "13,99,13")
        owner.completeActive(volumes())
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.requests.length, 3)
        compare(owner.cancellations, 0, "the view never directly cancels shared work")
    }
    function test_reentry_recovers_request_silently_superseded_by_other_consumer() {
        create()
        catalogue.arc = null
        owner.requestSeries(99)
        catalogue.arc = arc("3")
        compare(owner.requests.join(","), "13,99,13")
        owner.completeActive(volumes())
        assertVolumes("3", "Remote Three", localCover)
        compare(owner.requests.length, 3)
    }
    function test_detached_owner_can_be_rebound_after_failed_request() {
        owner.completion = "failure"
        create()
        catalogue.tankobanCatalogRef = null
        catalogue.tankobanCatalogRef = owner
        compare(owner.requests.length, 2)
        compare(owner.cancellations, 0)
    }
    function test_initial_null_arc_waits_until_navigation() {
        create({arc: null})
        compare(owner.requests.length, 0)
        assertVolumes("")
        catalogue.arc = arc("2")
        compare(owner.requests.length, 1)
        assertVolumes("2", "Volume 2", "")
    }
    function test_changed_series_ignores_previous_series_response() {
        create()
        catalogue.mangaMalId = 99
        compare(owner.requests.join(","), "13,99")
        owner.publish(13, volumes())
        assertVolumes("1,2", "Volume 1", "")
        owner.publish(99, [{number: "2", name: "Other Series", cover: localCover}])
        assertVolumes("2", "Other Series", localCover)
        compare(owner.cancellations, 0)
    }
    function test_owner_replacement_disconnects_old_owner() {
        create()
        replacementOwner = ownerComponent.createObject(suite, {rowsById: {13: volumes()}})
        catalogue.tankobanCatalogRef = replacementOwner
        assertVolumes("1,2", "Remote One", localCover)
        compare(replacementOwner.requests.length, 1)
        var reads = replacementOwner.reads
        owner.publish(13, [])
        compare(replacementOwner.reads, reads)
        assertVolumes("1,2", "Remote One", localCover)
        compare(owner.cancellations, 0)
    }
    function test_navigation_away_does_not_cancel_shared_owner_and_reentry_reads_cache() {
        create()
        catalogue.destroy()
        catalogue = null
        wait(0)
        compare(owner.cancellations, 0)
        owner.publish(13, volumes())
        create({arc: arc("3")})
        assertVolumes("3", "Remote Three", localCover)
        compare(owner.requests.length, 2, "one request for each mounted catalogue")
    }
}
