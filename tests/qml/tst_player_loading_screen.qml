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

    TestCase {
        name: "PlayerLoadingScreen"
        when: windowShown

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
