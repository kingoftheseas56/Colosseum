import QtQuick
import QtTest
import "../../qml"

Item {
    width: 800
    height: 450

    PlayerLoadingScreen {
        id: screen
        anchors.fill: parent
        title: "Fixture title"
        logoUrl: Qt.resolvedUrl("../../assets/addon-logos/tmdb.png")
        backdropUrl: Qt.resolvedUrl("../../assets/addon-logos/comet.png")
    }

    SignalSpy { id: manualSpy; target: screen; signalName: "manualRequested" }
    SignalSpy { id: cancelSpy; target: screen; signalName: "cancelRequested" }

    TestCase {
        name: "PlayerLoadingScreen"
        when: windowShown

        function init() {
            screen.statusText = ""
            screen.allowManual = false
            manualSpy.clear()
            cancelSpy.clear()
        }
        function cleanup() {
            screen.allowManual = false
            screen.active = false
        }
        function test_smartPickWaitUsesSameLoaderUntilConnecting() {
            screen.title = "Fixture title"
            screen.statusText = "Finding the best stream"
            screen.allowManual = true
            screen.active = true
            tryCompare(screen, "opacity", 1)
            var status = findChild(screen, "playerLoadingStatus")
            var manual = findChild(screen, "playerChooseSourceAction")
            verify(status !== null && manual !== null)
            compare(status.text, "FINDING THE BEST STREAM")
            compare(manual.visible, true)
            var position = manual.mapToItem(screen, 0, 0)
            verify(position.y >= 0 && position.y + manual.height <= screen.height)
            waitForRendering(screen)
            mouseClick(manual, 60, 18)
            compare(manualSpy.count, 1)
            compare(cancelSpy.count, 0)
            screen.allowManual = false
            screen.statusText = "Connecting to peers..."
            compare(screen.active, true)
            compare(screen.opacity, 1)
            compare(status.text, "CONNECTING TO PEERS...")
            compare(manual.visible, false)
        }
        function test_smartPickBackCancels() {
            screen.allowManual = true
            screen.active = true
            tryCompare(screen, "opacity", 1)
            mouseClick(screen, 48, 44)
            compare(cancelSpy.count, 1)
            compare(manualSpy.count, 0)
        }

        function imageForSource(item, source) {
            if (item.source !== undefined && String(item.source) === String(source))
                return item
            for (const child of item.children) {
                const match = imageForSource(child, source)
                if (match)
                    return match
            }
            return null
        }

        function test_retryKeepsArtworkThroughFade() {
            screen.active = true
            tryCompare(screen, "opacity", 1)
            const logo = imageForSource(screen, screen.logoUrl)
            const backdrop = imageForSource(screen, screen.backdropUrl)
            verify(logo && backdrop)
            tryCompare(logo, "status", Image.Ready)
            tryCompare(backdrop, "status", Image.Ready)

            screen.active = false
            verify(screen.visible)
            compare(String(logo.source), String(screen.logoUrl))
            verify(logo.visible)
            verify(backdrop.visible)

            screen.active = true
            compare(logo.status, Image.Ready)
            compare(backdrop.status, Image.Ready)

            screen.active = false
            tryCompare(screen, "visible", false)
            tryCompare(logo, "status", Image.Null)
            tryCompare(backdrop, "status", Image.Null)
        }
    }
}
