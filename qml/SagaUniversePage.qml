// SagaUniversePage — the SAGA universe template: book-first IPs (Harry Potter, Lord of the
// Rings, A Song of Ice and Fire, Dune). Born from the 2026-07-12 correction: these pages are
// NOT the anime template re-worn — the anime reading lane does not exist here, and nothing on
// this page is a fuzzy search hit. The canon (novels / films / shows) is curated in
// Universes.js; SagaApi dresses it with real Biblio books and real Cinemeta items.
//
//   READ  = the novel sequence, reading order. The duality's Read half and every shelf
//           book route into the Biblio detail (win.openBook) — book ONE is the golden path.
//   WATCH = the adaptations: films in canon order (the duality routes to film one; for a
//           saga with no films — ASOIAF — the show is the Watch), shows beside them.
import QtQuick
import QtQuick.Controls
import "SagaApi.js" as Saga
import "ComicsApi.js" as ComicsApi

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors UniversePage; bookRequested is this template's own verb)
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)     // film/show tile → A4's TheatreSeries
    signal bookRequested(var book)      // novel tile / Read → the Biblio book detail
    signal comicsArchiveRequested(var box)   // the comics door → the GC archive index

    Theme { id: theme }

    KeyboardScrollController {
        id: pageKeyboardScroll
        flick: page
        arrowScrolling: false
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            root.backRequested()
            event.accepted = true
            return
        }
        if (!event.accepted)
            pageKeyboardScroll.handle(event)
    }
    property var uni: ({ name: "", blurb: "", banner: "", metaline: "", books: [], films: [], shows: [],
                         comics: null })

    // the pinned archive resolved live (real GC name + release count); curated pin = fallback
    property var comicsBox: null
    onUniChanged: {
        if (root.uni.comics && root.uni.comics.tagId && !root.comicsBox)
            ComicsApi.tagBox(root.uni.comics.tagId, function(b) { if (b) root.comicsBox = b })
    }
    function comicsDoor() {
        return root.comicsBox || { name: root.uni.name, tag: root.uni.comics.tag,
                                   tagId: root.uni.comics.tagId, count: 0 }
    }

    function reload() {
        if (!root.universeName.length) return         // never load a default universe (the OP-flash lesson)
        root.comicsBox = null
        Saga.loadSaga(root.universeName, function(u) { if (u) root.uni = u; })
    }
    Component.onCompleted: { reload(); root.forceActiveFocus(Qt.TabFocusReason) }
    onUniverseNameChanged: reload()

    readonly property var firstBook: uni.books.length ? uni.books[0] : null
    // Watch's golden path: film one; a saga with no films (ASOIAF) leads with show one
    readonly property var firstWatch: uni.films.length ? uni.films[0]
                                    : (uni.shows.length ? uni.shows[0] : null)

    // ---- persistent wallpaper the page floats over ----
    Item {
        id: wall
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.82) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: col
            width: page.width
            spacing: 0

            // ===== BANNER =====
            Item {
                width: parent.width; height: 360
                Image {
                    anchors.fill: parent
                    source: root.uni.banner
                    fillMode: Image.PreserveAspectCrop
                    cache: true
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.035,0.043,0.07,0.12) }
                        GradientStop { position: 0.45; color: Qt.rgba(0.035,0.043,0.07,0.04) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.035,0.043,0.07,0.92) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 28
                    spacing: 9
                    Text { text: "UNIVERSE  ·  THE SAGA"; color: theme.gold; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.uni.name; color: theme.ink
                           font.family: theme.display; font.pixelSize: 62 }
                    Text { text: root.uni.metaline; color: theme.inkDimmer
                           font.family: theme.ui; font.pixelSize: 14 }
                }
            }

            // ===== BODY =====
            Column {
                x: 54; width: parent.width - 108; spacing: 0
                topPadding: 26

                Text {
                    bottomPadding: 30
                    text: root.uni.blurb
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                    lineHeight: 1.5; wrapMode: Text.WordWrap
                    maximumLineCount: 3; elide: Text.ElideRight
                    width: Math.min(parent.width, 760)
                }

                // ===== THE CLEAVED READ / WATCH DUALITY — books vs adaptations =====
                Rectangle {
                    width: parent.width; height: 330; radius: 22; clip: true
                    color: "transparent"; border.width: 1; border.color: theme.edge
                    SagaHalf {
                        anchors.left: parent.left; width: parent.width/2; height: parent.height
                        align: Qt.AlignLeft
                        label: "Read"
                        sub: root.firstBook ? ("Begin with " + root.firstBook.title) : "The novels"
                        icon: "../assets/icons/books.svg"
                        artImage: root.firstBook ? (root.firstBook.cover || "") : ""
                        warm: true
                        enabled: !!root.firstBook
                        onActivated: if (root.firstBook) root.bookRequested(root.firstBook)
                    }
                    SagaHalf {
                        anchors.right: parent.right; width: parent.width/2; height: parent.height
                        align: Qt.AlignRight
                        label: "Watch"
                        sub: root.firstWatch ? ("Begin with " + root.firstWatch.title) : "The adaptations"
                        icon: "../assets/icons/movies.svg"
                        artImage: root.firstWatch ? (root.firstWatch.art || root.firstWatch.cover || "") : ""
                        warm: false
                        enabled: !!root.firstWatch
                        onActivated: if (root.firstWatch) root.watchRequested(root.firstWatch)
                    }
                    Rectangle {   // luminous gold seam (the house duality signature)
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 2; z: 3
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 0.18; color: Qt.rgba(0.94,0.77,0.29,0.9) }
                            GradientStop { position: 0.5; color: "#fff7df" }
                            GradientStop { position: 0.82; color: Qt.rgba(0.94,0.77,0.29,0.9) }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }
                    Rectangle {
                        anchors.centerIn: parent; width: 30; height: 30; radius: 6; z: 4
                        rotation: 45
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#fff3cf" }
                            GradientStop { position: 1.0; color: "#e0a634" }
                        }
                        border.width: 1; border.color: "#fff7df"
                    }
                }

                Item { width: 1; height: 44 }

                // ===== THE SHELF — the novels standing in reading order =====
                Column {
                    width: parent.width
                    spacing: 16
                    visible: root.uni.books.length > 0
                    Row {
                        spacing: 12
                        Text { text: "The Novels"; color: theme.ink
                               font.family: theme.display; font.pixelSize: 25 }
                        Text { text: root.uni.books.length + " books  ·  reading order"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                               anchors.baseline: parent.children[0].baseline }
                    }
                    Item {
                        width: parent.width; height: 246
                        // the ledge the books stand on
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width; height: 3; radius: 1.5
                            color: Qt.rgba(0.97, 0.97, 0.96, 0.14)
                        }
                        Row {
                            id: bookRow
                            property int currentIndex: root.uni.books.length > 0 ? 0 : -1
                            focusPolicy: root.uni.books.length > 0 ? Qt.TabFocus : Qt.NoFocus
                            Keys.onPressed: (event) => bookNav.handle(event)
                            spacing: 22
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                            Repeater {
                                id: bookRepeater
                                model: root.uni.books
                                delegate: Item {
                                    id: bookTile
                                    required property var modelData
                                    required property int index
                                    width: 150; height: 236
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8; clip: true
                                        color: "#241c14"
                                        border.width: 1
                                        border.color: (bookMa.containsMouse || (bookRow.activeFocus && bookRow.currentIndex === bookTile.index)) ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                                           : Qt.rgba(0.97,0.97,0.96,0.14)
                                        Image {
                                            anchors.fill: parent
                                            source: bookTile.modelData.cover || ""
                                            asynchronous: true; cache: true
                                            fillMode: Image.PreserveAspectCrop
                                            opacity: status === Image.Ready ? 1 : 0
                                            Behavior on opacity { NumberAnimation { duration: 220 } }
                                        }
                                        // the number plate — reading order is the shelf's whole claim
                                        Rectangle {
                                            anchors.top: parent.top; anchors.left: parent.left
                                            anchors.margins: 8
                                            width: 26; height: 26; radius: 6
                                            color: Qt.rgba(0, 0, 0, 0.62)
                                            border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.55)
                                            Text {
                                                anchors.centerIn: parent
                                                text: bookTile.index + 1
                                                color: theme.gold; font.family: theme.ui
                                                font.pixelSize: 13; font.weight: Font.Bold
                                            }
                                        }
                                        Rectangle {   // title scrim
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 56
                                            gradient: Gradient {
                                                GradientStop { position: 0; color: "transparent" }
                                                GradientStop { position: 1; color: Qt.rgba(0,0,0,0.86) }
                                            }
                                        }
                                        Text {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 9
                                            text: bookTile.modelData.title
                                            color: theme.ink; font.family: theme.ui
                                            font.pixelSize: 12; font.weight: Font.DemiBold
                                            wrapMode: Text.WordWrap; maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }
                                    }
                                    MouseArea {
                                        id: bookMa
                                        anchors.fill: parent
                                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: root.bookRequested(bookTile.modelData)
                                    }
                                }
                            }
                            KeyboardCollectionController {
                                id: bookNav
                                view: bookRow
                                count: root.uni.books.length
                                orientation: "horizontal"
                                positionIndexFn: function(index) {
                                    const item = bookRepeater.itemAt(index)
                                    if (!item) return
                                    const p = item.mapToItem(page.contentItem, 0, 0)
                                    const top = p.y
                                    const bottom = p.y + item.height
                                    const maxY = Math.max(0, page.contentHeight - page.height)
                                    if (top < page.contentY) page.contentY = Math.max(0, top)
                                    else if (bottom > page.contentY + page.height)
                                        page.contentY = Math.min(maxY, bottom - page.height)
                                }
                                onActivated: (index) => {
                                    if (index >= 0 && index < root.uni.books.length)
                                        root.bookRequested(root.uni.books[index])
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 40; visible: root.uni.books.length > 0 }

                // ===== THE ADAPTATIONS =====
                AdaptationRow { width: parent.width; title: "The Films";  items: root.uni.films;  numbered: true }
                AdaptationRow { width: parent.width; title: "TV Shows";   items: root.uni.shows;  numbered: false }

                Item { width: 1; height: 34; visible: !!root.uni.comics }

                // ===== THE COMICS DOOR — the canon in print (curated GC pin, 2026-07-13) =====
                Rectangle {
                    width: parent.width; height: 108
                    radius: 12; clip: true
                    visible: !!root.uni.comics
                    color: "#241813"
                    border.width: 1
                    border.color: (sagaComicsMa.containsMouse || sagaComicsKey.activeFocus) ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                             : Qt.rgba(0.97,0.97,0.96,0.10)
                    Image {
                        anchors.fill: parent
                        source: root.uni.banner
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? ((sagaComicsMa.containsMouse || sagaComicsKey.activeFocus) ? 0.5 : 0.28) : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.76) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.30) }
                        }
                    }
                    Column {
                        anchors.left: parent.left; anchors.leftMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7
                        Text { text: "GETCOMICS ARCHIVE"; color: theme.gold
                               font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                        Text {
                            text: (root.uni.comics && root.uni.comics.line) || "The canon continues in print."
                            color: theme.ink; font.family: theme.display; font.pixelSize: 19
                        }
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Text { text: root.comicsBox && root.comicsBox.count
                                     ? "Browse " + root.comicsBox.count + " releases"
                                     : "Browse the archive"
                               color: theme.ink; font.family: theme.ui
                               font.pixelSize: 13; font.weight: Font.DemiBold }
                        Text { text: "→"; color: theme.gold; font.pixelSize: 14 }
                    }
                    KeyboardAction {
                        id: sagaComicsKey
                        anchors.fill: parent
                        pointerEnabled: false
                        focusEnabled: parent.visible
                        accessibleName: "Browse the comics archive"
                        onTriggered: root.comicsArchiveRequested(root.comicsDoor())
                    }
                    MouseArea {
                        id: sagaComicsMa
                        anchors.fill: parent
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.comicsArchiveRequested(root.comicsDoor())
                    }
                }

                Item { width: 1; height: 60 }
            }
        }
    }

    ChromeScrim { z: 16 }
    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: root.backRequested()
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

    // ---- one duality half: art + big verb; whole-half click ----
    component SagaHalf: Item {
        id: half
        property int align: Qt.AlignLeft
        property string label
        property string sub
        property string icon
        property string artImage
        property bool warm: true
        signal activated()
        clip: true
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: half.align === Qt.AlignLeft ? 0.0 : 1.0
                               color: half.warm ? "#7a4a28" : "#28405c" }
                GradientStop { position: half.align === Qt.AlignLeft ? 1.0 : 0.0
                               color: half.warm ? "#2e1a0c" : "#0e1826" }
            }
        }
        Image {
            anchors.fill: parent
            source: half.artImage
            asynchronous: true; cache: true
            fillMode: Image.PreserveAspectCrop
            opacity: status === Image.Ready ? ((halfMa.containsMouse || halfKey.activeFocus) ? 0.65 : 0.45) : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.3; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.66) }
            }
        }
        Column {
            anchors.left: half.align === Qt.AlignLeft ? parent.left : undefined
            anchors.right: half.align === Qt.AlignRight ? parent.right : undefined
            anchors.margins: 40
            anchors.bottom: parent.bottom; anchors.bottomMargin: 34
            spacing: 8
            Text {
                text: half.label
                color: theme.ink; font.family: theme.display; font.pixelSize: 54
                horizontalAlignment: half.align === Qt.AlignLeft ? Text.AlignLeft : Text.AlignRight
                width: half.width - 80
            }
            Text {
                text: half.sub
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                horizontalAlignment: half.align === Qt.AlignLeft ? Text.AlignLeft : Text.AlignRight
                width: half.width - 80
                elide: Text.ElideRight
            }
        }
        KeyboardAction {
            id: halfKey
            anchors.fill: parent
            pointerEnabled: false
            focusEnabled: half.visible && half.enabled
            accessibleName: half.label
            onTriggered: half.activated()
        }
        MouseArea {
            id: halfMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: half.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: half.activated()
        }
    }

    // ---- one adaptations row: canon-ordered tiles routing to the Theatre ----
    component AdaptationRow: Column {
        id: arow
        property string title
        property var items: []
        property bool numbered: false
        spacing: 16
        visible: items && items.length > 0
        Row {
            spacing: 12
            Text { text: arow.title; color: theme.ink
                   font.family: theme.display; font.pixelSize: 25 }
            Text { text: (arow.items ? arow.items.length : 0) + (arow.items && arow.items.length === 1 ? " title" : " titles")
                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                   anchors.baseline: parent.children[0].baseline }
        }
        Flickable {
            id: adaptationRail
            width: parent.width; height: 238
            contentWidth: rowContent.width; contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: rowContent
                spacing: 18
                Repeater {
                    id: adaptationRepeater
                    model: arow.items
                    delegate: Item {
                        id: wTile
                        required property var modelData
                        required property int index
                        width: 150; height: 232
                        Rectangle {
                            anchors.fill: parent
                            radius: 8; clip: true
                            color: "#1a2030"
                            border.width: 1
                            border.color: (wMa.containsMouse || (adaptationRailFocus.activeFocus && adaptationRailFocus.currentIndex === wTile.index)) ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                            : Qt.rgba(0.97,0.97,0.96,0.12)
                            Image {
                                anchors.fill: parent
                                source: wTile.modelData.cover || ""
                                asynchronous: true; cache: true
                                fillMode: Image.PreserveAspectCrop
                                opacity: status === Image.Ready ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 220 } }
                            }
                            Rectangle {
                                visible: arow.numbered
                                anchors.top: parent.top; anchors.left: parent.left
                                anchors.margins: 8
                                width: 26; height: 26; radius: 6
                                color: Qt.rgba(0, 0, 0, 0.62)
                                border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.55)
                                Text {
                                    anchors.centerIn: parent
                                    text: wTile.index + 1
                                    color: theme.gold; font.family: theme.ui
                                    font.pixelSize: 13; font.weight: Font.Bold
                                }
                            }
                            Rectangle {   // UPCOMING plate — future work stays, marked (ratified 2026-07-13)
                                anchors.top: parent.top; anchors.right: parent.right
                                anchors.margins: 8
                                visible: wTile.modelData.upcoming === true
                                radius: 4
                                color: Qt.rgba(0, 0, 0, 0.72)
                                border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.5)
                                width: sagaUpTag.implicitWidth + 12; height: sagaUpTag.implicitHeight + 6
                                Text { id: sagaUpTag; anchors.centerIn: parent
                                       text: "UPCOMING"; color: theme.gold
                                       font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2 }
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 52
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 1; color: Qt.rgba(0,0,0,0.86) }
                                }
                            }
                            Text {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 9
                                text: wTile.modelData.title
                                color: theme.ink; font.family: theme.ui
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap; maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            id: wMa
                            anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.watchRequested(wTile.modelData)
                        }
                    }
                }
            }
            UniverseRailFocus {
                id: adaptationRailFocus
                flick: adaptationRail
                repeater: adaptationRepeater
                count: arow.items ? arow.items.length : 0
                itemGap: 18
                accessibleName: arow.title
                onActivated: (index) => {
                    if (index >= 0 && index < arow.items.length)
                        root.watchRequested(arow.items[index])
                }
            }
        }
    }
}
