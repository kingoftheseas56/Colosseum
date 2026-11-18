import QtQuick
import QtTest
import "../../qml/audiobook"
import "../../qml/vinyl"

Item {
    id: root
    width: 1280
    height: 720

    Component {
        id: listenPage
        AudiobookListenPage {
            anchors.fill: parent
            book: ({ title: "The Way of Kings" })
            session: QtObject {
                property bool ready: true
                property bool completed: false
                property int chapterIndex: 0
                property real chapterDuration: 600
                property real chapterPosition: 0
                property real bookFraction: 0
                property bool canGoBack: false
                property bool paused: true
                property real speed: 1
                property real volume: 60
                property bool mute: false
                property int sleepMinutes: 0
                property int sleepMinutesLeft: 0
                property var chapterModel: []
            }
        }
    }

    Component {
        id: backdropComponent
        VinylBackdrop { anchors.fill: parent; contentStore: null }
    }

    QtObject {
        id: offlineStore
        readonly property int generation: 1
        function assetUrl(path) { return Qt.resolvedUrl("fixtures/no-store-wallpaper.jpg") }
    }

    TestCase {
        name: "AudiobookPlayerPolish"
        when: windowShown
        readonly property url missingCover: Qt.resolvedUrl("fixtures/no-audiobook-cover.png")
        readonly property url validCover: Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg")

        function wallpaper(backdrop) {
            // Get the art used by the room's standard blur pass, whose inputs are ordinary Images.
            return backdrop.children[1].children.find(function (item) { return item.blurEnabled === true }).source
        }

        function test_badCoverUsesHouseWallpaper() {
            const backdrop = createTemporaryObject(backdropComponent, root, { source: missingCover })
            verify(backdrop)
            tryVerify(function () { return wallpaper(backdrop).status === Image.Ready })
            compare(String(wallpaper(backdrop).source), String(validCover))

            const nextCover = Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg?new-cover")
            backdrop.source = nextCover
            tryVerify(function () { return wallpaper(backdrop).status === Image.Ready
                && String(wallpaper(backdrop).source) === String(nextCover) })
        }

        function test_unavailableStoreUsesBundledWallpaper() {
            const backdrop = createTemporaryObject(backdropComponent, root,
                { source: missingCover, contentStore: offlineStore })
            verify(backdrop)
            tryVerify(function () { return wallpaper(backdrop).status === Image.Ready })
            compare(String(wallpaper(backdrop).source), String(validCover))
        }

        function test_badCoverUsesHousePoster() {
            const page = createTemporaryObject(listenPage, root,
                { book: { title: "The Way of Kings", cover: String(missingCover) } })
            verify(page)
            const poster = findChild(page, "audiobookPoster")
            verify(poster)
            tryCompare(poster, "exhausted", true)
            const placeholder = findChild(poster, "biblioCoverPlaceholder")
            verify(placeholder && placeholder.visible)
            compare(placeholder.title, "The Way of Kings")
        }

        function test_chapterLabels_data() {
            return [
                { tag: "titled", label: "Chapter 1 — Stormblessed", kind: "seek", expected: "Chapter 1 — Stormblessed" },
                { tag: "prologue", label: "Prologue", kind: "seek", expected: "Prologue" },
                { tag: "interlude", label: "Interlude: Ishikk", kind: "seek", expected: "Interlude: Ishikk" },
                { tag: "slash-in-title", label: "The Hero / The Traitor", kind: "seek", expected: "The Hero / The Traitor" },
                { tag: "untitled", label: "", kind: "seek", expected: "Chapter 3" },
                { tag: "number", label: "03", kind: "seek", expected: "Chapter 3" },
                { tag: "track", label: "Track 03", kind: "seek", expected: "Chapter 3" },
                { tag: "filename", label: "stormlight_03.mp3", kind: "seek", expected: "Chapter 3" },
                { tag: "file-model", label: "stormlight_03", kind: "file", expected: "Chapter 3" }
            ]
        }

        function test_chapterLabels(data) {
            const page = createTemporaryObject(listenPage, root, { chaptersOpen: true })
            verify(page)
            page.session.chapterModel = [{ label: "Preface" }, { label: "Opening" },
                { label: data.label, kind: data.kind, time: 0 }]
            page.session.chapterIndex = 2
            page.chaptersOpen = true
            const bar = findChild(page, "abBarChapterLabel")
            verify(bar)
            compare(bar.text, data.expected)
            tryVerify(function () { return !!findChild(page, "audiobookChapter_2") })
            compare(findChild(page, "audiobookChapter_2").accessibleName, data.expected)
        }
    }
}
