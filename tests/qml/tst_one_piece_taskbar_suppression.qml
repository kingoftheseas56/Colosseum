import QtQuick
import QtTest
import "../../qml" as App

Item {
    id: host
    width: 1280
    height: 720

    Component { id: taskbarComponent; App.Taskbar {} }

    TestCase {
        id: tests
        name: "OnePieceTaskbarSuppression"
        when: windowShown
        property var sourceFixture: null
        property var taskbar: null

        function cleanup() {
            if (sourceFixture) { sourceFixture.destroy(); sourceFixture = null }
            if (taskbar) { taskbar.destroy(); taskbar = null }
        }

        function test_onePieceTributeSuppressesDockButStandardRouteAndReturnRestoreIt() {
            const request = new XMLHttpRequest()
            request.open("GET", Qt.resolvedUrl("../../qml/Main.qml"), false)
            request.send()
            const mainSource = request.responseText.replace(/\r\n/g, "\n")
            verify(mainSource.length > 0, "Read production Main.qml")

            const tributeRoute = /function openTribute\(entry\)\s*\{([^}]*)\}/.exec(mainSource)
            verify(tributeRoute !== null, "The Universes carousel has a tribute route")
            verify(/win\.openUniverse\(entry\.extensionId, entry\.name\)/.test(tributeRoute[1]),
                   "Tributes open the bespoke page with plain=false")
            const openRoute = /function openUniverse\(extensionId, name, plain\)\s*\{([^}]*)\}/.exec(mainSource)
            verify(openRoute !== null
                   && /universeLayer\.plain\s*=\s*plain\s*===\s*true/.test(openRoute[1])
                   && /universeLayer\.templatePage\s*=\s*""/.test(openRoute[1]),
                   "The omitted tribute flag selects the bespoke route")
            const rosterRoute = /function openRosterUniverse\(entry\)\s*\{([\s\S]*?)\n    \}/.exec(mainSource)
            verify(rosterRoute !== null, "The structured roster has a separate route")
            verify(/if\s*\(entry\.page\)\s*win\.openArchivedUniverse\(entry\.name,\s*entry\.page\)/.test(rosterRoute[1]),
                   "A curated One Piece page opens through the structured template route")
            verify(/UniverseApi\.fileFor\(entry\.extensionId\)\.length > 0/.test(rosterRoute[1]),
                   "Entries without a template can select their standard extension page")
            const archivedRoute = /function openArchivedUniverse\(name, page\)\s*\{([^}]*)\}/.exec(mainSource)
            verify(archivedRoute !== null
                   && /universeLayer\.plain\s*=\s*false/.test(archivedRoute[1])
                   && /universeLayer\.templatePage\s*=\s*page/.test(archivedRoute[1])
                   && /universeLayer\.extensionId\s*=\s*"archive\."\s*\+\s*page/.test(archivedRoute[1]),
                   "The structured route stays distinct from the bespoke One Piece identity")
            const bespokeMatch = /readonly property string bespokeId:\s*([^\r\n]+)/.exec(mainSource)
            verify(bespokeMatch !== null, "Main derives the bespoke identity from plain and extensionId")

            const arcRoute = /function openOnePieceArc\(arc\)\s*\{([^}]*)\}/.exec(mainSource)
            verify(arcRoute !== null && /onePieceArcLayer\.active\s*=\s*true/.test(arcRoute[1]),
                   "Opening an arc leaves the tribute route active underneath it")
            verify(!/universeLayer\.active\s*=/.test(arcRoute[1]),
                   "Opening an arc does not leave the One Piece Tribute")
            const closeRoute = /function closeUniverse\(\)\s*\{([^}]*)\}/.exec(mainSource)
            verify(closeRoute !== null && /universeLayer\.active\s*=\s*false/.test(closeRoute[1]),
                   "Back from the Tribute closes its active route")

            const taskbarId = mainSource.indexOf("id: taskbar")
            verify(taskbarId >= 0, "The shell taskbar exists")
            const visibleStart = mainSource.indexOf("visible:", taskbarId)
            const enabledStart = mainSource.indexOf("\n        enabled:", visibleStart)
            verify(visibleStart > taskbarId && enabledStart > visibleStart,
                   "The taskbar visibility binding is bounded")
            const visibility = mainSource.slice(visibleStart + "visible:".length, enabledStart).trim()
            verify(/universeLayer\.active\s*&&\s*universeLayer\.bespokeId\s*===\s*"com\.colosseum\.universe\.onepiece"/.test(visibility),
                   "Only the active bespoke One Piece surface suppresses the dock")
            const taskbarRequest = new XMLHttpRequest()
            taskbarRequest.open("GET", Qt.resolvedUrl("../../qml/Taskbar.qml"), false)
            taskbarRequest.send()
            const taskbarSource = taskbarRequest.responseText.replace(/\r\n/g, "\n")
            verify(/onVisibleChanged:\s*\{[\s\S]*pendingAutoReveal/.test(taskbarSource),
                   "Taskbar resets when hidden and defers auto-reveal until visible")

            const fixtureSource = [
                "import QtQuick",
                "import \"../../qml\" as App",
                "Item { id: win",
                "    property bool immersiveSurfaceOpen: false",
                "    property bool vinylSongPageOpen: false",
                "    property bool vinylVideoFullscreen: false",
                "    property bool androidHost: false",
                "    property bool desktopPresentation: true",
                "    property alias routeLayer: universeLayer",
                "    property alias arcLayer: onePieceArcLayer",
                "    property alias dock: taskbar",
                "    Item { id: universeLayer",
                "        property bool active: false",
                "        property bool plain: false",
                "        property string extensionId: \"\"",
                "        property string templatePage: \"\"",
                "        readonly property string bespokeId: " + bespokeMatch[1],
                "    }",
                "    Item { id: onePieceArcLayer; property bool active: false }",
                "    Item { id: feriaLayer; property bool open: false; property var item: null }",
                "    App.Taskbar { id: taskbar; visible: " + visibility + " }",
                "}"
            ].join("\n")
            sourceFixture = Qt.createQmlObject(fixtureSource, tests,
                                               Qt.resolvedUrl("OnePieceTaskbarFixture.qml"))
            verify(sourceFixture !== null, "The production visibility expression loads in isolation")

            const route = sourceFixture.routeLayer
            route.extensionId = "archive.shelves"
            route.plain = false
            route.templatePage = "shelves"
            route.active = true
            compare(route.bespokeId, "archive.shelves")
            compare(sourceFixture.dock.visible, true, "The structured One Piece page keeps the dock")

            route.extensionId = "com.colosseum.universe.onepiece"
            route.templatePage = ""
            compare(route.bespokeId, "com.colosseum.universe.onepiece")
            sourceFixture.dock.open = true
            compare(sourceFixture.dock.visible, false, "The One Piece Tribute hides the dock")
            compare(sourceFixture.dock.open, false, "Hiding the Tribute dock collapses its open panel")
            sourceFixture.arcLayer.active = true
            compare(sourceFixture.dock.visible, false, "The dock stays hidden on the Tribute's arc page")
            sourceFixture.arcLayer.active = false
            route.active = false
            compare(sourceFixture.dock.visible, true, "Closing the Tribute restores the dock")
            compare(sourceFixture.dock.open, false, "The dock returns collapsed")

            route.extensionId = "com.colosseum.universe.starwars"
            route.active = true
            compare(sourceFixture.dock.visible, true, "Other Tributes keep normal dock visibility")
        }

        function test_hiddenTaskbarDefersAutoRevealUntilVisible() {
            taskbar = taskbarComponent.createObject(host)
            verify(taskbar !== null)
            taskbar.visible = false
            taskbar.reveal()
            taskbar.focusHistoryStatsAction()
            compare(taskbar.open, false, "Hidden-taskbar requests do not expand it")
            compare(taskbar.pendingAutoReveal, true, "The session reveal request is retained")
            taskbar.visible = true
            compare(taskbar.open, true, "A queued session reveal appears when the dock returns")
            compare(taskbar.pendingAutoReveal, false, "The reveal request is consumed once visible")
        }
    }
}
