import QtQuick
import QtQuick.Window
import "../../qml/feria"

Window {
    id: window
    width: 1440; height: 900; visible: true
    property int phase: 0
    property int rowIndex: 0
    property bool rowOpen: false
    property var rows: []
    property var expected: []
    property real homeY: 0
    property real gridY: 0
    property int selectedIndex: 0
    property bool finished: false
    property bool captured: false
    FeriaWorld { id: feria; anchors.fill: parent }
    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    function press(name) {
        var action = find(feria, name)
        return action && action.activate(Qt.OtherFocusReason)
    }
    function check(ok, detail) {
        browserReporter.report("feria-see-all", ok, detail)
        if (!ok) finish()
        return ok
    }
    function finish() {
        if (finished) return
        finished = true; step.stop()
        Qt.callLater(function() { browserReporter.finish() })
    }
    function grid() { return find(feria, "feriaSeeAllGrid") }
    function view() { return find(feria, "feriaSeeAllView") }
    Component.onCompleted: {
        feria.saved = ["phm", "stranger"]
        FeriaAccount.setRecording(true)
        FeriaAccount.clearHistory()
        FeriaAccount.beginVisit({pk:"youtube"})
        FeriaAccount.observe({href:"https://youtube.com/watch?v=seeall",title:"Video fixture",position:20,duration:100,paused:false,rate:1})
        FeriaAccount.endVisit()
        FeriaAccount.beginVisit({pk:"spotify"})
        FeriaAccount.observe({href:"https://open.spotify.com/track/fixture",title:"Audio fixture",kind:"audio",position:30,duration:120,paused:false,rate:1})
        FeriaAccount.endVisit()
        FeriaAccount.beginVisit({pk:"kindle"})
        FeriaAccount.saveReadingPlace("https://read.amazon.com/?asin=SEEALL", "Reading fixture")
        FeriaAccount.endVisit()
    }
    Timer {
        id: step
        interval: 250; repeat: true; running: true
        onTriggered: {
            if (window.phase === 0) {
                window.rows = feria.shownShelves()
                window.phase = 1
            } else if (window.phase === 1) {
                if (window.rowIndex >= window.rows.length) { window.phase = 2; return }
                var row = window.rows[window.rowIndex]
                if (!window.rowOpen) {
                    window.expected = feria.shelfItems(row)
                    if (!window.check(window.press("feriaSeeAll_" + row.id)
                        && feria.viewState === "seeAll", row.title + " exposes a working See all action")) return
                    window.rowOpen = true
                } else {
                    if (!window.check(JSON.stringify(window.view().entries) === JSON.stringify(window.expected)
                        && window.grid().count === window.expected.length,
                        row.title + " opens every title in its original order")) return
                    feria.back(); window.rowOpen = false; window.rowIndex++
                }
            } else if (window.phase === 2) {
                var home = window.find(feria, "home-focus-variant")
                var scroll = window.find(feria, "feriaHomeScroll")
                scroll.contentY = home.boundedContentY(1200)
                window.homeY = scroll.contentY
                feria.openSeeAll(window.rows[1])
                window.phase++
            } else if (window.phase === 3) {
                window.selectedIndex = window.grid().count - 1
                window.grid().currentIndex = window.selectedIndex
                window.gridY = window.grid().contentY
                var id = window.view().entries[window.selectedIndex]
                window.view().activateSelection()
                if (!window.check(feria.viewState === "title" && feria.selectedTitle === id,
                                  "Expanded grid opens the selected title")) return
                feria.back(); window.phase++
            } else if (window.phase === 4) {
                if (!window.check(feria.viewState === "seeAll" && window.grid().currentIndex === window.selectedIndex
                    && Math.abs(window.grid().contentY - window.gridY) < 1,
                    "Back from a title preserves expanded selection and scroll")) return
                feria.back(); window.phase++
            } else if (window.phase === 5) {
                if (!window.check(feria.viewState === "home"
                    && Math.abs(window.find(feria,"feriaHomeScroll").contentY - window.homeY) < 1,
                    "Back from See all restores the home scroll position")) return
                feria.selectLens("watch"); window.phase++
            } else if (window.phase === 6) {
                if (!window.check(window.press("feriaContinueRailHeader"), "Continue exposes See all")) return
                window.phase++
            } else if (window.phase === 7) {
                if (!window.check(window.view().entries.length === 1 && window.view().entries[0].kind === "video",
                                  "Watch Continue only contains video progress")) return
                var entry = window.view().entries[0]
                window.view().activateSelection()
                if (!window.check(feria.hostUrl === entry.url && feria.hostResumePosition === entry.position
                    && feria.hostReturnState === "seeAll", "Continue resumes the saved provider URL and position")) return
                feria.back(); feria.back(); feria.selectLens("read"); window.phase++
            } else if (window.phase === 8) {
                window.press("feriaContinueRailHeader"); window.phase++
            } else if (window.phase === 9) {
                if (!window.check(window.view().entries.length === 1 && window.view().entries[0].kind === "book",
                                  "Read Continue only contains saved reading places")) return
                window.find(feria, "feriaSeeAllContinue_0").removeRequested()
                window.phase++
            } else if (window.phase === 10) {
                if (!window.check(window.grid().count === 0 && feria.continueEntries().length === 0,
                                  "Removing the last reading place shows a valid empty grid")) return
                feria.back(); feria.selectLens("listen"); window.phase++
            } else if (window.phase === 11) {
                window.press("feriaContinueRailHeader"); window.phase++
            } else if (window.phase === 12) {
                if (!window.check(window.view().entries.length === 1 && window.view().entries[0].kind === "audio",
                                  "Listen Continue only contains audio progress")) return
                feria.back(); window.phase++
            } else if (window.phase === 13) {
                var albumRow = feria.shownShelves().find(function(row) { return row.id === "albums" })
                feria.openSeeAll(albumRow)
                var chips = Object.assign({}, feria.chipSelection); chips.albums = "ytmusic"; feria.chipSelection = chips
                window.phase++
            } else if (window.phase === 14) {
                if (!window.check(window.view().entries[0] === "dtmf", "Expanded music charts respect the selected provider")) return
                window.contentItem.grabToImage(function(result) {
                    result.saveToFile(smokeProfileRoot + "/see-all.png")
                    window.captured = true
                })
                window.phase++
            } else if (window.phase === 15) {
                if (!window.captured) return
                feria.back(); window.phase++
            } else {
                if (!window.check(window.press("feriaAppsSeeAll") && feria.viewState === "account"
                    && feria.accountTab === "apps", "Apps See all opens the existing app manager")) return
                window.finish()
            }
        }
    }
    Timer { interval: 25000; running: true; onTriggered: { window.check(false,"See all checks timed out"); window.finish() } }
}
