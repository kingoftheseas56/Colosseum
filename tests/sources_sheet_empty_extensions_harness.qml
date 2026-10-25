import QtQuick
import QtQuick.Window
import QtTest
import "../qml"

Item {
    id: harness
    width: 1280
    height: 900
    property var sheet: null
    property int navigationRequests: 0
    property bool sheetOpenAtNavigation: true

    Window {
        id: hostWindow
        width: 1280
        height: 900
        visible: true
        property bool androidHost: false
        Item { id: surface; anchors.fill: parent }
    }

    Component {
        id: sourcesComponent
        SourcesSheet { anchors.fill: parent }
    }

    Connections {
        target: harness.sheet
        ignoreUnknownSignals: true
        function onOpenExtensionsRequested() {
            harness.navigationRequests += 1
            harness.sheetOpenAtNavigation = harness.sheet.open
        }
    }

    TestCase {
        name: "SourcesSheetEmptyExtensions"
        when: windowShown

        function init() {
            harness.navigationRequests = 0
            harness.sheetOpenAtNavigation = true
            harness.sheet = sourcesComponent.createObject(surface)
            verify(harness.sheet !== null, "create the production SourcesSheet")
        }

        function cleanup() {
            if (harness.sheet) {
                harness.sheet.destroy()
                harness.sheet = null
            }
            hostWindow.width = 1280
            hostWindow.androidHost = false
        }

        function test_noEnabledCompatibleProviderOffersExtensionsRoute() {
            harness.sheet.show("movie", "tt0063350", "Example film", ({}))
            compare(harness.sheet.askedCount, 0)
            compare(harness.sheet.loading, false)
            verify(harness.sheet.open)
            var status = findChild(harness.sheet, "sourcesSheetEmptyText")
            compare(status.text, "No stream extension carries this title — add one on the Extensions page.")

            var action = findChild(harness.sheet, "sourcesSheetExtensionsAction")
            verify(action !== null, "empty provider state exposes its Extensions action")
            verify(action.visible, "the action is visible only for this settled empty state")
            compare(action.accessibleName, "Extensions")
            compare(findChild(harness.sheet, "sourcesSheetExtensionsLabel").text, "Extensions")
            mouseClick(action, action.width / 2, action.height / 2, Qt.LeftButton)
            tryCompare(harness.sheet, "open", false)
            compare(harness.navigationRequests, 1)
            compare(harness.sheetOpenAtNavigation, false, "close the sheet before routing away")
        }

        function test_loading_results_emptyReply_timeout_andOtherModesHideAction() {
            harness.sheet.show("movie", "tt0063350", "Example film", ({}))
            var action = findChild(harness.sheet, "sourcesSheetExtensionsAction")
            verify(action !== null, "production action exists for visibility gating")
            var status = findChild(harness.sheet, "sourcesSheetEmptyText")

            harness.sheet.askedNames = ["Example provider"]
            harness.sheet.loading = true
            verify(!action.visible, "pending providers do not show the setup CTA")
            compare(status.text, "Finding sources…")

            harness.sheet.loading = false
            harness.sheet.rows = [{ quality: "1080p", rank: 3, seeders: 4, size: "1 GB", audio: "English",
                                    release: "Example 1080p", tags: [], languages: [],
                                    streamKind: "Torrent", addonName: "Example provider",
                                    addonId: "example", infoHash: "abc", fileIdx: 0 }]
            verify(!action.visible, "loaded source rows do not show the setup CTA")

            harness.sheet.rows = []
            harness.sheet.timedOut = false
            verify(!action.visible, "an asked provider returning zero sources shows No sources found")
            compare(status.text, "No sources found.")

            harness.sheet.timedOut = true
            verify(!action.visible, "a timed-out provider uses the timeout state, not setup CTA")
            compare(status.text, "Sources timed out. Try again.")

            harness.sheet.timedOut = false
            harness.sheet.askedNames = []
            harness.sheet.mode = "download"
            verify(!action.visible, "download mode does not show the play-only setup CTA")

            harness.sheet.mode = "play"
            harness.sheet.open = false
            verify(!action.visible, "a closed sheet never exposes the setup CTA")
        }

        function test_actionRemainsInsidePhoneAndDesktopSheet() {
            harness.sheet.show("movie", "tt0063350", "Example film", ({}))
            var action = findChild(harness.sheet, "sourcesSheetExtensionsAction")
            verify(action !== null && action.visible)

            hostWindow.width = 400
            hostWindow.androidHost = true
            tryCompare(harness.sheet, "phoneLayout", true)
            var phonePoint = action.mapToItem(harness.sheet, 0, 0)
            verify(phonePoint.x >= 0 && phonePoint.x + action.width <= harness.sheet.width,
                   "phone CTA stays within the source sheet width")
            verify(phonePoint.y >= 0 && phonePoint.y + action.height <= harness.sheet.height,
                   "phone CTA stays within the source sheet height")

            hostWindow.width = 1280
            hostWindow.androidHost = false
            tryCompare(harness.sheet, "phoneLayout", false)
            var desktopPoint = action.mapToItem(harness.sheet, 0, 0)
            verify(desktopPoint.x >= 0 && desktopPoint.x + action.width <= harness.sheet.width,
                   "desktop CTA stays within the source sheet width")
            verify(desktopPoint.y >= 0 && desktopPoint.y + action.height <= harness.sheet.height,
                   "desktop CTA stays within the source sheet height")
        }
    }
}
