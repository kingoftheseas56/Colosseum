// Saga collection: reading and screen adaptations share equal prominence.
import QtQuick
import QtQuick.Controls
import "SagaApi.js" as Saga
import "ComicsApi.js" as ComicsApi
import "Universes.js" as UDB

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
    // Typography and control geometry adapted from the supplied Harbor QML prototype.
    FontLoader { id: sagaDisplay; source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { id: sagaUi; source: "../assets/fonts/Switzer-Regular.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Semibold.otf" }
    readonly property string uiFace: sagaUi.name
    property bool reducedMotion: true
    readonly property var guide: UDB.configFor(root.universeName || root.uni.name)
    readonly property color worldColor: guide.c1 || "#221c30"
    readonly property color paper: theme.ink
    readonly property real gutter: width < 700 ? 24 : 64
    readonly property bool compact: width < 800
    readonly property string displayFace: sagaDisplay.name
    readonly property var collections: Saga.sagaCollections(root.uni, root.guide)
    property int loadGeneration: 0
    function openWork(item, book) {
        if (!item || item.resolved === false) return
        if (book || item.medium === "book") root.bookRequested(item)
        else root.watchRequested(item)
    }
    function revealSection(section) {
        page.contentY = Math.min(Math.max(0, section.mapToItem(page.contentItem, 0, 0).y - 90),
                                Math.max(0, page.contentHeight - page.height))
    }

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
    Keys.onReleased: (event) => pageKeyboardScroll.handleRelease(event)
    property var uni: ({ name: "", blurb: "", banner: "", metaline: "", books: [], films: [], shows: [],
                         comics: null })

    // the pinned archive resolved live (real GC name + release count); curated pin = fallback
    property var comicsBox: null
    property int comicsGeneration: -1
    onUniChanged: {
        if (root.uni.comics && root.uni.comics.tagId && root.comicsGeneration !== root.loadGeneration) {
            const generation = root.loadGeneration
            root.comicsGeneration = generation
            ComicsApi.tagBox(root.uni.comics.tagId, function(b) {
                if (b && generation === root.loadGeneration) root.comicsBox = b
            })
        }
    }
    function comicsDoor() {
        return root.comicsBox || { name: root.uni.name, tag: root.uni.comics.tag,
                                   tagId: root.uni.comics.tagId, count: 0 }
    }

    function reload() {
        if (!root.universeName.length) return         // never load a default universe (the OP-flash lesson)
        root.comicsBox = null
        const generation = ++root.loadGeneration
        Saga.loadSaga(root.universeName, function(u) {
            if (generation === root.loadGeneration && u) root.uni = u
        })
    }
    Component.onCompleted: { reload(); root.forceActiveFocus(Qt.TabFocusReason) }
    onUniverseNameChanged: reload()

    readonly property var firstBook: uni.books.length && uni.books[0].resolved !== false ? uni.books[0] : null
    readonly property var adaptations: root.collections.adaptations
    readonly property var firstWatch: adaptations.length && adaptations[0].resolved !== false && !adaptations[0].upcoming ? adaptations[0] : null

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
        Image {
            id: worldWallpaper
            anchors.fill: parent
            source: root.guide.wallpaper || root.uni.banner
            asynchronous: true; cache: true
            fillMode: Image.PreserveAspectCrop
        }
        // Harbor's directional scrims keep the world visible behind the content.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0.067, 0.071, 0.075, 0.88) }
                GradientStop { position: 1; color: Qt.rgba(0.067, 0.071, 0.075, 0.36) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.45; color: Qt.rgba(0.067, 0.071, 0.075, 0.4) }
                GradientStop { position: 1; color: Qt.rgba(0.067, 0.071, 0.075, 0.92) }
            }
        }
    }

    Flickable {
        id: page
        objectName: "sagaScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollGlide { flick: page }

        Column {
            id: col
            width: page.width
            spacing: 0

            // The world leads; both media have the same action weight.
            Item {
                width: parent.width
                height: root.compact ? 420 : 400
                Column {
                    x: root.gutter; y: 120
                    width: parent.width - root.gutter * 2
                    spacing: 18
                    Text {
                        text: "SAGA"
                        color: theme.inkDim; font.family: root.uiFace
                        font.pixelSize: 11; font.letterSpacing: 3
                    }
                    Text {
                        width: parent.width
                        text: root.uni.name || root.universeName
                        color: root.paper; font.family: root.displayFace
                        font.pixelSize: root.compact ? 40 : 72
                        lineHeight: 1.05; wrapMode: Text.WordWrap
                    }
                    Flow {
                        width: parent.width; spacing: 12
                        SagaAction {
                            objectName: "sagaReadButton"
                            text: "Read"; primary: true; width: 128
                            enabled: !!root.firstBook
                            onTriggered: root.bookRequested(root.firstBook)
                        }
                        SagaAction {
                            objectName: "sagaAdaptationsButton"
                            text: "Watch"; primary: true; width: 128
                            enabled: !!root.firstWatch
                            onTriggered: root.watchRequested(root.firstWatch)
                        }
                        SagaAction {
                            text: "Beyond the saga"
                            enabled: root.collections.branches.length > 0
                            onTriggered: root.revealSection(branchSection)
                        }
                        SagaAction {
                            text: "Upcoming"
                            enabled: root.collections.upcoming.length > 0
                            onTriggered: root.revealSection(upcomingSection)
                        }
                    }
                }
            }

            Column {
                x: root.gutter; width: parent.width - root.gutter * 2; spacing: 0
                Row {
                    spacing: 24; height: 62
                    Text { text: "BOOKS & SCREEN"; color: theme.inkDimmer
                           font.family: root.uiFace; font.pixelSize: 10; font.letterSpacing: 2 }
                    Text { text: root.uni.metaline; color: theme.inkDim; font.family: root.uiFace
                           font.pixelSize: 12; width: Math.max(0, col.width - root.gutter * 2 - 140)
                           elide: Text.ElideRight }
                }
                Grid {
                    width: parent.width
                    columns: root.compact ? 1 : 2
                    columnSpacing: 40; rowSpacing: 28
                    Column {
                        width: root.compact ? parent.width : (parent.width - parent.columnSpacing) / 2
                        AdaptationRow {
                            id: readingSection
                            width: parent.width; title: "Books"
                            items: root.collections.books; numbered: true; books: true
                        }
                        Text {
                            visible: root.uni.books.length === 0
                            width: parent.width; height: 80
                            text: "Books unavailable"
                            color: theme.inkDim; font.family: root.uiFace; wrapMode: Text.WordWrap
                        }
                    }
                    Column {
                        width: root.compact ? parent.width : (parent.width - parent.columnSpacing) / 2
                        AdaptationRow {
                            id: screenSection
                            width: parent.width; title: "Adaptations"
                            items: root.collections.adaptations
                        }
                        Text {
                            visible: root.adaptations.length === 0
                            width: parent.width; height: 80
                            text: "Adaptations unavailable"
                            color: theme.inkDim; font.family: root.uiFace; wrapMode: Text.WordWrap
                        }
                    }
                }
                Column {
                    id: branchSection
                    objectName: "sagaBranches"
                    width: parent.width; spacing: 30
                    visible: root.collections.branches.length > 0
                    topPadding: 32
                    Rectangle { width: parent.width; height: 1; color: theme.edge }
                    Text { text: "Beyond the saga"; color: root.paper
                           font.family: root.displayFace; font.pixelSize: 34 }
                    Repeater {
                        model: root.collections.branches
                        delegate: Column {
                            id: branch
                            required property var modelData
                            width: branchSection.width; spacing: 18
                            Text { text: branch.modelData.title; color: root.paper
                                   width: parent.width; wrapMode: Text.WordWrap
                                   font.family: root.displayFace; font.pixelSize: 25 }
                            Grid {
                                width: parent.width
                                columns: root.compact || !branch.modelData.books.length || !branch.modelData.adaptations.length ? 1 : 2
                                columnSpacing: 40; rowSpacing: 20
                                AdaptationRow {
                                    width: parent.columns === 1 ? parent.width : (parent.width - parent.columnSpacing) / 2
                                    title: "Books"; headingVisible: false; books: true
                                    items: branch.modelData.books
                                }
                                AdaptationRow {
                                    width: parent.columns === 1 ? parent.width : (parent.width - parent.columnSpacing) / 2
                                    title: "Adaptations"; headingVisible: false
                                    items: branch.modelData.adaptations
                                }
                            }
                        }
                    }
                }
                AdaptationRow {
                    id: upcomingSection
                    objectName: "sagaUpcoming"
                    width: parent.width; title: "Upcoming"
                    items: root.collections.upcoming
                }
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
                        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
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
                        width: parent.width - (root.compact ? 52 : 270)
                        spacing: 7
                        Text { text: "COMICS"; color: theme.gold
                               font.family: root.uiFace; font.pixelSize: 10; font.letterSpacing: 3 }
                        Text {
                            text: "Graphic adaptations"
                            width: parent.width; elide: Text.ElideRight
                            color: theme.ink; font.family: theme.display; font.pixelSize: 19
                        }
                    }
                    Row {
                        visible: !root.compact
                        anchors.right: parent.right; anchors.rightMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Text { text: root.comicsBox && root.comicsBox.count
                                     ? "Browse " + root.comicsBox.count + " releases"
                                     : "Browse the archive"
                               color: theme.ink; font.family: root.uiFace
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

    component SagaAction: Rectangle {
        id: actionButton
        property string text
        property bool primary: false
        signal triggered()
        width: actionText.implicitWidth + 44; height: 48; radius: height / 2
        scale: actionKey.pressed ? 0.98 : 1
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 180 } }
        opacity: enabled ? 1 : 0.4
        color: actionKey.pressed ? theme.glassHi : (primary ? root.paper : (actionKey.hovered ? theme.glassTint : "transparent"))
        border.width: primary ? 0 : 1; border.color: theme.edge
        Text {
            id: actionText; anchors.centerIn: parent; text: actionButton.text
            color: actionButton.primary && !actionKey.pressed ? "#090c13" : root.paper
            font.family: root.uiFace; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        KeyboardAction {
            id: actionKey; anchors.fill: parent
            accessibleName: actionButton.text
            focusRadius: actionButton.radius
            onTriggered: actionButton.triggered()
        }
    }

    // The same soft edge scrim and chevron treatment as TrendingTop10.
    component SagaChevron: Item {
        id: chevron
        property bool atRight: false
        signal triggered()
        width: 46; z: 5
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, chevron.atRight ? 0 : 0.65) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, chevron.atRight ? 0.65 : 0) }
            }
        }
        Text {
            anchors.centerIn: parent
            text: chevron.atRight ? "\u203a" : "\u2039"
            color: edgeAction.hovered ? theme.gold : theme.ink
            font.family: root.displayFace; font.pixelSize: 42
        }
        KeyboardAction {
            id: edgeAction; anchors.fill: parent
            focusPolicy: Qt.NoFocus; focusEnabled: false
            accessibleName: chevron.atRight ? "Show later items" : "Show earlier items"
            onTriggered: chevron.triggered()
        }
    }

    // ---- one adaptations row: canon-ordered tiles routing to the Theatre ----
    component AdaptationRow: Column {
        id: arow
        property string title
        property var items: []
        property bool numbered: false
        property bool books: false
        property bool headingVisible: true
        // HarborPosterRail geometry: fit whole covers to the available width.
        readonly property int fitCount: Math.max(1, Math.floor((width + 20) / 164))
        readonly property real cardWidth: (width - (fitCount - 1) * 20) / fitCount
        readonly property real posterHeight: cardWidth * 1.5
        spacing: 16
        bottomPadding: 28
        visible: items && items.length > 0
        Column {
            visible: arow.headingVisible
            width: parent.width; spacing: 8
            Text { text: arow.title + " (" + arow.items.length + ")"; color: root.paper
                   font.family: root.uiFace; font.pixelSize: 20; font.weight: Font.Medium }

        }
        Flickable {
            id: adaptationRail
            width: parent.width; height: arow.posterHeight + 104
            contentWidth: rowContent.width; contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            // Match TrendingTop10: edge chevrons page the strip, no scrollbar.
            function pageBy(direction) {
                const next = Math.max(0, Math.min(contentWidth - width, contentX + direction * width * 0.8))
                slide.stop()
                slide.to = next
                slide.start()
            }
            NumberAnimation {
                id: slide; target: adaptationRail; property: "contentX"
                duration: root.reducedMotion ? 0 : 300; easing.type: Easing.OutCubic
            }
            SagaChevron {
                objectName: arow.books ? "sagaBooksPrevious" : "saga" + arow.title + "Previous"
                x: adaptationRail.contentX
                height: arow.posterHeight
                visible: adaptationRail.contentX > 1
                onTriggered: adaptationRail.pageBy(-1)
            }
            SagaChevron {
                objectName: arow.books ? "sagaBooksNext" : "saga" + arow.title + "Next"
                x: adaptationRail.contentX + adaptationRail.width - width
                height: arow.posterHeight; atRight: true
                visible: adaptationRail.contentX < adaptationRail.contentWidth - adaptationRail.width - 1
                onTriggered: adaptationRail.pageBy(1)
            }
            Row {
                id: rowContent
                spacing: 20
                Repeater {
                    id: adaptationRepeater
                    model: arow.items
                    delegate: Item {
                        id: wTile
                        required property var modelData
                        required property int index
                        width: arow.cardWidth; height: arow.posterHeight + 80
                        Rectangle {
                            width: parent.width; height: arow.posterHeight
                            radius: 3; clip: true
                            color: root.worldColor
                            border.width: 1
                            border.color: (wMa.containsMouse || (adaptationRailFocus.activeFocus && adaptationRailFocus.currentIndex === wTile.index)) ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                            : Qt.rgba(0.97,0.97,0.96,0.12)
                            Text {
                                anchors.centerIn: parent; width: parent.width - 24
                                text: wTile.modelData.title
                                color: theme.inkDim; font.family: root.displayFace
                                font.pixelSize: 18; wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                                visible: coverArt.status !== Image.Ready
                            }
                            Image {
                                id: coverArt
                                anchors.fill: parent
                                source: wTile.modelData.cover || ""
                                asynchronous: true; cache: true
                                fillMode: Image.PreserveAspectFit
                                opacity: status === Image.Ready ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
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
                                    color: theme.gold; font.family: root.uiFace
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
                                       font.family: root.uiFace; font.pixelSize: 9; font.letterSpacing: 2 }
                            }
                        }
                        Text {
                            x: 0; y: arow.posterHeight + 12; width: parent.width
                            text: wTile.modelData.title
                            color: root.paper; font.family: root.uiFace; font.pixelSize: 13
                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        }
                        Text {
                            visible: !arow.books || wTile.modelData.resolved === false
                            y: arow.posterHeight + 56; width: parent.width
                            text: wTile.modelData.resolved === false ? "Details pending"
                                  : (wTile.modelData.medium === "book" ? "Book"
                                     : (wTile.modelData.type === "series" ? "Series" : "Film"))
                            color: theme.inkDimmer; font.family: root.uiFace; font.pixelSize: 11
                        }
                        MouseArea {
                            id: wMa
                            anchors.fill: parent
                            enabled: wTile.modelData.resolved !== false
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.openWork(wTile.modelData, arow.books)
                        }
                    }
                }
            }
            UniverseRailFocus {
                id: adaptationRailFocus
                objectName: arow.books ? "sagaBooksFocus" : "saga" + arow.title + "Focus"
                flick: adaptationRail
                repeater: adaptationRepeater
                count: arow.items ? arow.items.length : 0
                itemGap: 20
                accessibleName: arow.title
                onActiveFocusChanged: if (activeFocus) root.revealSection(arow)
                onActivated: (index) => {
                    if (index >= 0 && index < arow.items.length)
                        root.openWork(arow.items[index], arow.books)
                }
            }
        }
    }
}
