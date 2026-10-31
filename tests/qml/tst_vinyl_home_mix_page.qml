import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/vinyl"

TestCase {
    name: "VinylHomeMixPage"
    when: windowShown
    property var host

    Component {
        id: windowFactory
        Window {
            id: testWindow
            width: 1280
            height: 820
            color: "#101015"
            visible: true

            property alias page: mixPage
            property alias mixView: mixModel
            property alias playbackSession: fakeSession
            property int playCalls: 0
            property int lastPlayIndex: -1
            property bool lastPlayShuffled: false
            property int queueCalls: 0
            property int saveCalls: 0
            property int retryCalls: 0

            QtObject {
                id: mixModel
                property string title: "Late Night Drive"
                property string status: "ready"
                property string error: ""
                property var songs: []
            }
            QtObject {
                id: fakeSession
                property real duration: 0
                property real position: 0
                property var activeQueueRow: ({trackId: "track-b"})
                function formatTime(seconds) {
                    var value = Math.floor(Number(seconds) || 0)
                    return Math.floor(value / 60) + ":" + ("0" + (value % 60)).slice(-2)
                }
            }
            Rectangle {
                id: backdrop
                anchors.fill: parent
                color: "#101015"
            }
            VinylHomeMixPage {
                id: mixPage
                anchors.fill: parent
                mixView: mixModel
                session: fakeSession
                backdrop: backdrop
            }
            Connections {
                target: mixPage
                function onPlayRequested(index, shuffle) {
                    testWindow.playCalls++
                    testWindow.lastPlayIndex = index
                    testWindow.lastPlayShuffled = shuffle
                }
                function onQueueRequested() { testWindow.queueCalls++ }
                function onSaveRequested() { testWindow.saveCalls++ }
                function onRetryRequested() { testWindow.retryCalls++ }
            }
        }
    }

    function item(name) {
        return host ? findChild(host.contentItem, name) : null
    }
    function containsText(root, value) {
        if (!root)
            return false
        if (root.text !== undefined && String(root.text) === value)
            return true
        var children = root.children || []
        for (var i = 0; i < children.length; ++i)
            if (containsText(children[i], value))
                return true
        return false
    }
    function sampleSongs() {
        return [
            {
                track: {id: "track-a", title: "First supplied song", seconds: 65,
                    available: true, artists: ["Artist One"]},
                album: {id: "album-a", title: "First supplied album", cover: ""}
            },
            {
                track: {id: "track-b", title: "Second supplied song", seconds: 143,
                    available: true, artists: ["Artist Two"]},
                album: {id: "album-b", title: "Second supplied album", cover: ""}
            },
            {
                track: {id: "track-c", title: "Third supplied song", seconds: 204,
                    available: true, artists: ["Artist Three"]},
                album: {id: "album-c", title: "Third supplied album", cover: ""}
            }
        ]
    }
    function init() {
        host = windowFactory.createObject(null)
        verify(host !== null, "the mix test window must be created")
        host.mixView.songs = sampleSongs()
        host.show()
        tryCompare(host, "visible", true)
        waitForRendering(host.contentItem)
    }
    function cleanup() {
        if (host) {
            host.close()
            host.destroy()
            host = null
        }
    }

    function test_suppliedSongsKeepTitleCountOrdinalAndMetadata() {
        var original = JSON.stringify(host.mixView.songs)
        compare(host.page.songs.length, 3)
        verify(containsText(host.page, "Late Night Drive"), "the supplied mix title is visible")
        verify(containsText(host.page, "3 songs"), "the visible song count matches supplied rows")

        for (var i = 0; i < 3; ++i) {
            var row = item("vinylTrackRow_" + i)
            verify(row !== null, "supplied song row " + i + " is rendered")
            compare(row.accessibleName, sampleSongs()[i].track.title)
            verify(containsText(row, ("0" + (i + 1)).slice(-2)), "row ordinal is retained")
            verify(containsText(row, sampleSongs()[i].track.title), "supplied track title is retained")
        }
        compare(item("vinylTrackDuration_0").text, "1:05")
        compare(item("vinylTrackDuration_1").text, "2:23")
        compare(JSON.stringify(host.mixView.songs), original,
            "rendering the mix must not mutate source album or track records")
    }

    function test_trackClickEmitsExactPlaybackIndex() {
        var before = JSON.stringify(host.mixView.songs)
        var row = item("vinylTrackRow_2")
        verify(row !== null && row.enabled)
        mouseClick(row)
        compare(host.playCalls, 1)
        compare(host.lastPlayIndex, 2, "the clicked row index is forwarded unchanged")
        compare(host.lastPlayShuffled, false)
        compare(JSON.stringify(host.mixView.songs), before,
            "requesting playback must not rewrite supplied track or album metadata")
    }

    function test_iconActionsAreAccessibleAndInvokeTheirSignals() {
        var play = item("vinylHomeMixPlay")
        var shuffle = item("vinylHomeMixShuffle")
        var queue = item("vinylHomeMixQueue")
        var save = item("vinylHomeMixSave")
        ;[[play, "Play mix"], [shuffle, "Shuffle mix"], [queue, "Queue mix"],
            [save, "Save mix to Library"]].forEach(function (entry) {
                verify(entry[0] !== null && entry[0].enabled)
                compare(entry[0].accessibleName, entry[1])
                compare(entry[0].accessibleDescription, "",
                    "icon actions do not add hover or prototype descriptions")
                compare(entry[0].focusOnPointer, false,
                    "mouse entry must not move keyboard focus onto icon actions")
            })

        mouseClick(play)
        compare(host.playCalls, 1)
        compare(host.lastPlayIndex, 0)
        compare(host.lastPlayShuffled, false)
        mouseClick(shuffle)
        compare(host.playCalls, 2)
        compare(host.lastPlayIndex, 0)
        compare(host.lastPlayShuffled, true)
        mouseClick(queue)
        compare(host.queueCalls, 1)
        mouseClick(save)
        compare(host.saveCalls, 1)

        play.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(host.playCalls, 3, "the icon action remains usable from the keyboard")
        compare(host.lastPlayShuffled, false)
    }

    function test_loadingErrorRetryAndSavedState() {
        var retry = item("vinylHomeMixRetry")
        host.mixView.status = "loading"
        host.mixView.error = ""
        verify(containsText(host.page, "Loading music…"))
        verify(!item("vinylHomeMixPlay").enabled,"a partially loaded mix cannot start playback")
        verify(!retry.visible, "retry stays hidden while the mix is loading")

        host.mixView.status = "error"
        host.mixView.error = "Network unavailable"
        verify(containsText(host.page, "Network unavailable"))
        verify(retry.visible && retry.enabled)
        compare(retry.accessibleName, "Retry mix")
        retry.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(host.retryCalls, 1, "the retry icon remains keyboard actionable")

        host.mixView.status = "ready"
        host.mixView.error = ""
        host.page.savedPlaylistId = "saved-mix-1"
        verify(containsText(host.page, "Saved to Library"))
        verify(!item("vinylHomeMixSave").enabled,
            "a saved mix cannot be saved to the library a second time")
        verify(item("vinylHomeMixPlay").enabled, "saved state does not disable playback")
    }
}
