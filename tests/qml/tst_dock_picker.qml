import QtQuick
import QtTest
import "../../qml" as App

Item {
    width: 1280
    height: 720

    Rectangle { id: scene; anchors.fill: parent; color: "#26354e" }
    Component {
        id: taskbarComponent
        App.Taskbar {
            pickerScene: scene
            localMediaAvailable: true
            pickerPicks: ({
                tv: [
                    { title: "First", cover: Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg") },
                    { title: "Second", cover: Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg") },
                    { title: "Third", cover: Qt.resolvedUrl("../../assets/wallpaper/sagas/dune.jpg") }
                ]
            })
        }
    }

    TestCase {
        id: suite
        name: "DesktopDockPicker"
        when: windowShown
        property Item bar
        SignalSpy { id: picks; target: suite.bar; signalName: "pickerPickRequested" }
        SignalSpy { id: wallpapers; target: suite.bar; signalName: "pickerWallpaperRequested" }
        SignalSpy { id: files; target: suite.bar; signalName: "openMediaClicked" }

        function init() {
            bar = createTemporaryObject(taskbarComponent, scene.parent)
            verify(bar !== null)
            picks.clear()
            wallpapers.clear()
            files.clear()
        }

        function test_dragTucksTheSameCardAndPreservesSelection() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(picker.visible)
            compare(picker.width, 1040)
            compare(picker.height, 520)
            tryVerify(() => card.width > 40)
            const x = card.width / 2
            const y = card.height / 2
            mousePress(card, x, y)
            verify(card.pressed, "Press reaches the front cover")
            mouseMove(card, x - 11, y, 20, Qt.LeftButton)
            verify(deck.shift < 0)
            mouseRelease(card, x - 11, y)
            tryCompare(deck, "moving", -1)
            compare(deck.current, 0)
            compare(picks.count, 0)

            mousePress(card, x, y)
            mouseMove(card, x - 48, y, 20, Qt.LeftButton)
            compare(deck.moving, 0)
            mouseRelease(card, x - 48, y)
            tryVerify(() => deck.behind && deck.moving === 0 && deck.current === 1)
            tryCompare(deck, "moving", -1)
            compare(picks.count, 0)

            mouseClick(findChild(picker, "dockSlideDot_2"))
            tryCompare(picker, "slide", 2)
            mouseClick(findChild(picker, "dockSlideDot_0"))
            tryCompare(picker, "slide", 0)
            compare(deck.current, 1)
            mouseClick(card)
            compare(picks.count, 1)
            compare(picks.signalArguments[0][0], "tv")
            compare(picks.signalArguments[0][1].title, "Second")
            verify(!picker.visible)
        }

        function test_mediaAndWallpaperAvailableFromBothSlides() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            mouseClick(findChild(picker, "dockShortcut_wallpaper"))
            compare(wallpapers.count, 1)
            verify(!picker.visible)
            bar.open = true
            mouseClick(findChild(picker, "dockShortcut_media"))
            compare(files.count, 1)

            bar.open = true
            mouseClick(findChild(picker, "dockSlideDot_2"))
            const widgets = findChild(picker, "dockMediaWidgets")
            verify(widgets && widgets.visible)
            mouseClick(findChild(widgets, "dockWallpaperArtwork"))
            compare(wallpapers.count, 2)
            bar.open = true
            tryVerify(() => findChild(picker, "dockDropArtwork") !== null)
            mouseClick(findChild(picker, "dockDropArtwork"))
            compare(files.count, 2)
        }

        function test_autoRevealKeepsPickerClosed() {
            bar.reveal()
            verify(bar.open)
            verify(!bar.pickerShown)
            bar.open = false
            mouseClick(findChild(bar, "taskbarToggleAction"))
            verify(bar.pickerShown)
            bar.reveal()
            verify(bar.pickerShown)
        }
    }
}
