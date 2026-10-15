import QtQuick
import QtTest
import "../../qml/LibraryApi.js" as LibraryApi

TestCase {
    name: "LibraryApiWatchTime"

    function test_newerPartialProgressBeatsOlderHistoryCompletion() {
        compare(
            LibraryApi.watchState({}, {
                mark: 0,
                completed: true,
                completedAt: 3000,
                progress: 0.4,
                progressAt: 5000,
                isSeries: false
            }),
            "progress")
    }

    function test_newerCompletionBeatsOlderPartialProgress() {
        compare(
            LibraryApi.watchState({}, {
                mark: 0,
                completed: true,
                completedAt: 5000,
                progress: 0.4,
                progressAt: 3000,
                isSeries: false
            }),
            "watched")
    }

    function test_missingHistoryTimeKeepsExistingStablePrecedence() {
        compare(
            LibraryApi.watchState({}, {
                mark: 0,
                completed: true,
                progress: 0.4,
                progressAt: 5000,
                isSeries: false
            }),
            "watched")
    }

    function test_importedCurrentWatchYieldsToNewerPartialButManualDoesNot() {
        var imported = {
            mark: 1,
            markManual: false,
            markActionAt: 1000,
            completed: true,
            completedAt: 1000,
            progress: 0.4,
            progressAt: 2000,
            isSeries: false
        }
        compare(LibraryApi.watchState({}, imported), "progress")

        imported.markManual = true
        compare(LibraryApi.watchState({}, imported), "watched")
    }

    function test_nonmanualProviderTimestampOrdersCurrentState() {
        // The provider's acknowledged current flag has its own real action
        // time. It wins older History/progress but yields to newer activity.
        var providerWatchNewer = {
            mark: 1, markManual: false, markActionAt: 3000,
            completed: true, completedAt: 1000,
            progress: 0.4, progressAt: 2000, isSeries: false
        }
        compare(LibraryApi.watchState({}, providerWatchNewer), "watched")

        var providerUnwatchNewer = {
            mark: -1, markManual: false, markActionAt: 3000,
            completed: true, completedAt: 1000,
            progress: 0, progressAt: 0, isSeries: false
        }
        compare(LibraryApi.watchState({}, providerUnwatchNewer), "unwatched")

        var newerPartial = {
            mark: 1, markManual: false, markActionAt: 1000,
            completed: false, completedAt: 0,
            progress: 0.4, progressAt: 2000, isSeries: false
        }
        compare(LibraryApi.watchState({}, newerPartial), "progress")

        var newerCompletion = {
            mark: -1, markManual: false, markActionAt: 1000,
            completed: true, completedAt: 2000,
            progress: 0, progressAt: 0, isSeries: false
        }
        compare(LibraryApi.watchState({}, newerCompletion), "watched")
    }

    function test_nonmanualProviderWinsEqualAndMissingTimeTies() {
        var equalTime = {
            mark: -1, markManual: false, markActionAt: 2000,
            completed: true, completedAt: 2000,
            progress: 0, progressAt: 0, isSeries: false
        }
        compare(LibraryApi.watchState({}, equalTime), "unwatched")

        var missingProviderTime = {
            mark: -1, markManual: false,
            completed: true, completedAt: 2000,
            progress: 0, progressAt: 0, isSeries: false
        }
        compare(LibraryApi.watchState({}, missingProviderTime), "unwatched")
    }

    function test_buildRowsCarriesNonmanualProviderActionTime() {
        var rows = LibraryApi.buildRows(
            [{ id: "tt-provider-unwatch", type: "movie" }],
            [{ id: "tt-provider-unwatch", progress: 0.4, updatedAt: 4000 }],
            function(id) {
                return { mark: -1, manual: false, actionAt: 3000 }
            },
            function(entry) { return false }, [], 0)
        compare(rows.length, 1)
        compare(rows[0].state, "progress")
    }
}
