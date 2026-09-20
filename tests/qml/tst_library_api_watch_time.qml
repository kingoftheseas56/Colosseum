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
}
