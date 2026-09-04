pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "UniverseExtApi.js" as UniverseExtApi

Item {
    id: root
    objectName: "galaxyUniversePage"
    anchors.fill: parent
    focus: true

    property Item backdrop: null
    property string extensionId: ""
    property string universeName: "Star Wars"
    property var payload: null
    property string selectedDestinationId: ""

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)
    signal seriesRequested(var entry)
    signal bookRequested(var payload)
    signal comicsArchiveRequested(var payload)

    Theme { id: theme }

    KeyboardScrollController {
        id: destinationKeyboardScroll
        flick: destinationPage
        arrowScrolling: true
    }
    Keys.onPressed: function(event) {
        if (root.selectedDestinationId.length && !event.accepted)
            destinationKeyboardScroll.handle(event)
    }

    property bool reducedMotion: false
    readonly property var destinations: [
        { id:"high", name:"HIGH REPUBLIC", title:"High Republic", world:"VALO", env:"../assets/universes/star-wars/valo.jpg", orbit:170, angle:3.4915926536, speed:.000030, size:27, y:5, light:"#e8e4d8", mid:"#9d9a91", dark:"#242525", mark:"../assets/universes/star-wars/marks/high-republic.png", shelves:[ {ids:["high-republic-screen"],medium:"Theatre",note:"High Republic series"}, {ids:["high-republic-books","high-republic-ya"],medium:"Biblio",note:"Canon novels + young adult"}, {ids:["high-republic-comics"],medium:"Tankoban",note:"Collected comic lines"} ] },
        { id:"fall", name:"FALL OF THE JEDI", title:"Fall of the Jedi", world:"CORUSCANT", env:"../assets/universes/star-wars/coruscant.jpg", orbit:245, angle:2.36, speed:.000024, size:32, y:-4, light:"#b9a08a", mid:"#6f5747", dark:"#211918", mark:"../assets/universes/star-wars/marks/republic.svg", shelves:[ {ids:["fall-of-the-jedi-screen"],medium:"Theatre",note:"Republic era"}, {ids:["republic-books"],medium:"Biblio",note:"Republic era novels"}, {ids:["fall-empire-comics"],medium:"Tankoban",note:"Spans Fall of the Jedi + Reign of the Empire"} ] },
        { id:"empire", name:"REIGN OF THE EMPIRE", title:"Reign of the Empire", world:"MUSTAFAR", env:"../assets/universes/star-wars/mustafar.jpg", orbit:330, angle:4.56, speed:.000019, size:42, y:8, light:"#858b93", mid:"#343a40", dark:"#0f1215", mark:"../assets/universes/star-wars/marks/empire.svg", shelves:[ {ids:["reign-of-the-empire-screen"],medium:"Theatre",note:"Imperial era"}, {ids:["imperial-books"],medium:"Biblio",note:"Imperial era novels"}, {ids:["fall-empire-comics"],medium:"Tankoban",note:"Spans Fall of the Jedi + Reign of the Empire"} ] },
        { id:"rebellion", name:"AGE OF REBELLION", title:"Age of Rebellion", world:"HOTH", env:"../assets/universes/star-wars/hoth.jpg", orbit:420, angle:1.22, speed:.000015, size:37, y:-8, light:"#a38762", mid:"#534233", dark:"#191514", mark:"../assets/universes/star-wars/marks/rebel.svg", shelves:[ {ids:["age-of-rebellion-screen"],medium:"Theatre",note:"Rebellion era"}, {ids:["rebellion-new-republic-books"],medium:"Biblio",note:"Spans Age of Rebellion + New Republic"}, {ids:["rebellion-comics"],medium:"Tankoban",note:"Rebellion era comics"} ] },
        { id:"newrep", name:"NEW REPUBLIC", title:"New Republic", world:"NEVARRO", env:"../assets/universes/star-wars/nevarro.jpg", orbit:510, angle:3.42, speed:.000012, size:32, y:5, light:"#9cacab", mid:"#4c5d5e", dark:"#151d1e", mark:"../assets/universes/star-wars/marks/new-republic.png", shelves:[ {ids:["new-republic-screen"],medium:"Theatre",note:"New Republic era"}, {ids:["rebellion-new-republic-books"],medium:"Biblio",note:"Spans Age of Rebellion + New Republic"}, {ids:["new-republic-first-order-comics"],medium:"Tankoban",note:"Spans New Republic + Rise of the First Order"} ] },
        { id:"firstorder", name:"RISE OF THE FIRST ORDER", title:"Rise of the First Order", world:"JAKKU", env:"../assets/universes/star-wars/jakku.jpg", orbit:605, angle:5.48, speed:.000010, size:30, y:-4, light:"#89939f", mid:"#3b444f", dark:"#131920", mark:"../assets/universes/star-wars/marks/first-order.svg", shelves:[ {ids:["rise-first-order-screen"],medium:"Theatre",note:"First Order era"}, {ids:["first-order-books"],medium:"Biblio",note:"First Order era novels"}, {ids:["new-republic-first-order-comics"],medium:"Tankoban",note:"Spans New Republic + Rise of the First Order"} ] },
        { id:"beyond", name:"BEYOND THE SKYWALKER SAGA", title:"Beyond the Skywalker Saga", world:"AHCH-TO", env:"../assets/universes/star-wars/ahch-to.jpg", orbit:700, angle:.92, speed:.000008, size:24, y:11, light:"#b9bec8", mid:"#555966", dark:"#111217", mark:"../assets/universes/star-wars/marks/new-jedi-order.png", shelves:[ {ids:["beyond-skywalker-screen"],medium:"Theatre",note:"Beyond the Skywalker Saga"} ] },
        { id:"across", name:"ACROSS THE ERAS", title:"Across the Eras", world:"CANON · CROSS-ERA", radius:820, angle:2.72, y:30, node:true, shelves:[ {ids:["canon-anthologies-screen"],medium:"Theatre",note:"Canon anthologies"}, {ids:["young-adult-books"],medium:"Biblio",note:"Young adult stories across eras"} ] },
        { id:"outside", name:"BEYOND CANON", title:"Beyond Canon", world:"ADJACENT CONTINUITIES", radius:760, angle:-.17, y:26, node:true, shelves:[ {ids:["visions-screen","vintage-screen","lego-screen"],medium:"Theatre",note:"Visions · Vintage & Legends · LEGO"}, {ids:["visions-manga"],medium:"Tankoban",note:"Visions manga"} ] }
    ]
    readonly property var skywalkerDestination: ({
        id:"skywalker", name:"SKYWALKER SAGA", title:"Skywalker Saga",
        world:"EPISODES I–IX", env:"",
        shelves:[ {ids:["skywalker-saga-screen"],medium:"Theatre",note:"Episodes I–IX"} ]
    })

    readonly property var currentDestination: destinationById(selectedDestinationId)
    readonly property var currentShelves: currentDestination ? currentDestination.shelves : []

    function destinationById(id) {
        if (id === "skywalker") return skywalkerDestination
        for (var i = 0; i < destinations.length; ++i)
            if (destinations[i].id === id) return destinations[i]
        return null
    }
    function sectionById(id) {
        var sections = payload ? payload.sections : []
        for (var i = 0; i < sections.length; ++i)
            if (sections[i].id === id) return sections[i]
        return null
    }
    function entriesFor(ids) {
        var out = []
        for (var i = 0; i < ids.length; ++i) {
            var s = sectionById(ids[i])
            if (!s) continue
            for (var j = 0; j < s.entries.length; ++j) out.push(s.entries[j])
        }
        return out
    }
    function kindFor(ids) {
        for (var i = 0; i < ids.length; ++i) {
            var s = sectionById(ids[i])
            if (s) return s.kind
        }
        return "video"
    }
    function reload() {
        if (!extensionId.length) { payload = null; return }
        UniverseExtApi.load(extensionId, function(p) { root.payload = p })
    }
    function takeKeyboardFocus() {
        if (selectedDestinationId.length)
            root.forceActiveFocus(Qt.TabFocusReason)
        else
            galaxyView.takeKeyboardFocus()
    }
    function openDestination(id) {
        if (!destinationById(id)) return
        selectedDestinationId = id
        destinationPage.contentY = 0
        Qt.callLater(function() { root.forceActiveFocus(Qt.TabFocusReason) })
    }
    function closeDestination() {
        selectedDestinationId = ""
        Qt.callLater(root.takeKeyboardFocus)
    }
    function requestEscape() {
        if (selectedDestinationId.length)
            closeDestination()
        else
            backRequested()
    }
    function openEntry(kind, entry) {
        if (!entry) return
        if (kind === "video")
            root.watchRequested({ id: entry.id, type: entry.type, title: entry.title, cover: "" })
        else if (kind === "book")
            root.bookRequested({ id: entry.id, title: entry.title })
        else if (kind === "manga")
            root.seriesRequested(entry)
        else if (kind === "comic")
            root.comicsArchiveRequested({ title: entry.title, posts: entry.posts, year: entry.year })
    }

    Component.onCompleted: { reload(); Qt.callLater(root.takeKeyboardFocus) }
    onExtensionIdChanged: reload()

    Rectangle { anchors.fill: parent; color: "#03050a" }
    StarWarsGalaxySystem {
        id: galaxyView
        anchors.fill: parent
        visible: root.selectedDestinationId === ""
        destinations: root.destinations
        reducedMotion: root.reducedMotion
        onDestinationActivated: function(destinationId) { root.openDestination(destinationId) }
        onSkywalkerActivated: root.openDestination("skywalker")
    }

    Item {
        id: destinationView
        anchors.fill: parent
        visible: root.selectedDestinationId !== ""
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Rectangle {
            anchors.fill: parent
            color: "#05070c"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#05070c" }
                GradientStop { position: 1; color: "#090b12" }
            }
        }
        Image {
            id: environment
            anchors.fill: parent
            visible: root.currentDestination && String(root.currentDestination.env || "").length > 0
            source: visible ? Qt.resolvedUrl(String(root.currentDestination.env)) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            sourceSize.width: 1920
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(.012,.018,.03,.96) }
                GradientStop { position: 0.34; color: Qt.rgba(.012,.018,.03,.72) }
                GradientStop { position: 0.68; color: Qt.rgba(.012,.018,.03,.22) }
                GradientStop { position: 1.0; color: Qt.rgba(.012,.018,.03,.34) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(.012,.018,.03,.10) }
                GradientStop { position: 0.52; color: Qt.rgba(.012,.018,.03,.24) }
                GradientStop { position: 0.78; color: Qt.rgba(.012,.018,.03,.82) }
                GradientStop { position: 1.0; color: "#03050a" }
            }
        }

        Flickable {
            id: destinationPage
            anchors.fill: parent
            contentWidth: width
            contentHeight: destinationColumn.implicitHeight + 90
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: destinationPage }
            ScrollGlide { flick: destinationPage }
            Column {
                id: destinationColumn
                x: theme.margin
                width: destinationPage.width - theme.margin * 2
                spacing: 0

                Item {
                    width: parent.width; height: 168
                    Text {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 96
                        text: "In a galaxy far far away"
                        color: Qt.rgba(247/255,247/255,245/255,.09)
                        font.family: theme.display; font.pixelSize: 34
                    }
                }
                Text {
                    text: root.currentDestination ? root.currentDestination.world : ""
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 2.2
                }
                Text {
                    width: Math.min(parent.width * .58, 760)
                    topPadding: 10
                    text: root.currentDestination ? root.currentDestination.title : ""
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 62
                    wrapMode: Text.WordWrap; lineHeight: .98
                }
                Item { width: 1; height: 48 }
                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1,1,1,.10) }
                Item { width: 1; height: 30 }

                Repeater {
                    model: root.currentShelves
                    delegate: Column {
                        id: shelfBlock
                        required property var modelData
                        width: destinationColumn.width
                        property var shelfEntries: root.entriesFor(modelData.ids)
                        visible: shelfEntries.length > 0
                        spacing: 0

                        StarWarsMediaShelf {
                            width: parent.width
                            mediumTitle: shelfBlock.modelData.medium
                            note: shelfBlock.modelData.note
                            kind: root.kindFor(shelfBlock.modelData.ids)
                            entries: shelfBlock.shelfEntries
                            onActivated: function(entry, kind) { root.openEntry(kind, entry) }
                        }
                        Item { width: 1; height: 46 }
                    }
                }
                Item { width: 1; height: 60 }
            }
        }
    }
    ChromeScrim { z: 16 }
    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: {
            if (root.selectedDestinationId.length) root.closeDestination()
            else root.backRequested()
        }
    }
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        UniverseChromeAction {
            accessibleName: "Minimize"
            source: "../assets/icons/minimize.svg"
            onTriggered: root.minimizeRequested()
        }
        UniverseChromeAction {
            accessibleName: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                            ? "Enter fullscreen" : "Exit fullscreen"
            source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                    ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
            onTriggered: root.fullscreenRequested()
        }
        UniverseChromeAction {
            accessibleName: "Close Colosseum"
            source: "../assets/icons/power.svg"
            onTriggered: root.closeRequested()
        }
    }
}
