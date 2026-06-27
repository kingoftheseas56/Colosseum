// GenrePage — the genre BROWSE page for the Tankoban / manga lane. Recreates MyAnimeList's genre
// listing (reference: myanimelist.net/manga/genre/2/Adventure) in the house glass. Approved mock:
// mocks/genre.html. Data: GenreApi.js (Jikan / MAL, keyless). Cards route into MangaSeries.qml by title.
//
// PROTOTYPE harness:  qml.exe qml/_genrecheck.qml   (loads this page with a live genre)
//
// Signature: the genre is its OWN art — its top covers wash behind the title (GenreMosaic doctrine),
// and the rank ordinal encodes the by-readers popularity sort (real info, not decoration).
import QtQuick
import QtQuick.Layouts
import "GenreApi.js" as Api

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors UniversePage / the world-page layers)
    property Item backdrop: null
    property string genreName: "Adventure"
    property string sortMode: "readers"          // "readers" (MAL members) | "score"
    property bool compact: false                  // view toggle: false = detailed cards, true = covers
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal seriesRequested(string title)          // a card → A1's MangaSeries.qml (by title)
    signal exploreRequested()                     // the "Explore" pill → host opens the full genre index

    Theme { id: theme }

    property var genreData: ({ count: 0, desc: "", cards: [], montage: [] })
    property bool loading: true

    function reload() {
        root.loading = true
        Api.loadGenre(root.genreName, root.sortMode, function(p) {
            if (p) root.genreData = p
            root.loading = false
        })
    }
    Component.onCompleted: reload()
    onGenreNameChanged: reload()
    onSortModeChanged: reload()

    // ---- the page's own wallpaper (it's a layer floating over the shell) ----
    Item {
        id: wall
        anchors.fill: parent
        Image { anchors.fill: parent; source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.84) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 44
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: page.width
            spacing: 0

            // ===== HERO — the genre as its own art =====
            Item {
                id: hero
                width: parent.width
                height: 300

                // montage: the genre's own top covers, washed behind the title
                Row {
                    id: montage
                    anchors.fill: parent
                    Repeater {
                        model: root.genreData.montage
                        delegate: Item {
                            required property string modelData
                            width: montage.width / Math.max(1, root.genreData.montage.length)
                            height: montage.height
                            clip: true
                            Image {
                                anchors.fill: parent; source: modelData
                                fillMode: Image.PreserveAspectCrop
                                verticalAlignment: Image.AlignTop
                                cache: true; asynchronous: true
                            }
                        }
                    }
                }
                // legibility scrims — dark left (where text sits) fading right, plus a bottom wash
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0;  color: Qt.rgba(0.03,0.027,0.055,0.97) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.03,0.027,0.055,0.62) }
                        GradientStop { position: 1.0;  color: Qt.rgba(0.03,0.027,0.055,0.40) }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0;  color: "transparent" }
                        GradientStop { position: 0.62; color: "transparent" }
                        GradientStop { position: 1.0;  color: Qt.rgba(0.03,0.027,0.055,0.95) }
                    }
                }

                // hero text
                Column {
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin; anchors.bottomMargin: 30
                    width: Math.min(parent.width - theme.margin * 2, 760)
                    spacing: 0

                    Row {
                        spacing: 7
                        Text { text: "TANKOBAN · MANGA · "; color: theme.inkDimmer
                               font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.4; font.weight: Font.DemiBold }
                        Text { text: "GENRE"; color: theme.gold
                               font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.4; font.bold: true }
                    }
                    Text {
                        text: root.genreName; color: theme.ink
                        font.family: theme.display; font.pixelSize: 72; font.letterSpacing: -1
                        topPadding: 8
                    }
                    Text {
                        text: root.genreData.desc
                        visible: text.length > 0
                        color: theme.inkDim; font.family: theme.display; font.italic: true
                        font.pixelSize: 18; font.weight: Font.Light
                        lineHeight: 1.42; wrapMode: Text.WordWrap
                        width: parent.width; topPadding: 14
                        maximumLineCount: 3; elide: Text.ElideRight
                    }
                    Row {
                        spacing: 16; topPadding: 20
                        Rectangle { width: 34; height: 3; radius: 2; color: theme.gold
                                    anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.StyledText
                            font.family: theme.ui; font.pixelSize: 14
                            text: root.loading
                                  ? "<font color='#9a99a5'>Loading…</font>"
                                  : "<b><font color='#f7f7f5'>" + root.genreData.count.toLocaleString()
                                    + "</font></b> <font color='#c9c8d0'>titles · sorted by "
                                    + (root.sortMode === "score" ? "score" : "readers") + "</font>"
                        }
                    }
                }
            }

            // ===== body =====
            Column {
                x: theme.margin; width: parent.width - theme.margin * 2
                spacing: 0; topPadding: 20

                // ---- sibling-genre hop ----
                Flow {
                    width: parent.width; spacing: 9
                    Repeater {
                        model: Api.siblings()
                        delegate: Rectangle {
                            required property string modelData
                            readonly property bool selected: modelData === root.genreName
                            height: 34; radius: 17
                            width: gpl.implicitWidth + 28
                            color: selected ? Qt.rgba(0.94,0.77,0.29,0.16)
                                      : (gpMa.containsMouse ? theme.glassHi : theme.glassTint)
                            border.width: 1
                            border.color: selected ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                            Text {
                                id: gpl; anchors.centerIn: parent; text: modelData
                                color: selected ? theme.gold : (gpMa.containsMouse ? theme.ink : theme.inkDim)
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: selected ? Font.DemiBold : Font.Normal
                            }
                            MouseArea {
                                id: gpMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (!parent.selected) root.genreName = modelData
                            }
                        }
                    }
                    Rectangle {
                        height: 34; radius: 17
                        width: exploreLabel.implicitWidth + 30
                        color: exploreMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.18) : theme.glassTint
                        border.width: 1
                        border.color: exploreMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : Qt.rgba(0.94,0.77,0.29,0.32)
                        Text {
                            id: exploreLabel
                            anchors.centerIn: parent
                            text: "Explore"
                            color: theme.gold
                            font.family: theme.ui; font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: exploreMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.exploreRequested()
                        }
                    }
                }

                Item { width: 1; height: 24 }

                // ---- listing controls ----
                Item {
                    width: parent.width; height: 40; y: 0
                    Item { height: 26 }
                    Text {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        text: root.sortMode === "score" ? "TOP RATED" : "MOST READ"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold; font.letterSpacing: 1.6
                    }
                    Row {
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        // sort: readers ⇄ score (real — reloads)
                        Rectangle {
                            height: 36; radius: 10; width: srt.implicitWidth + 28
                            color: srtMa.containsMouse ? theme.glassHi : theme.glassTint
                            border.width: 1; border.color: theme.edge
                            Text {
                                id: srt; anchors.centerIn: parent
                                textFormat: Text.StyledText
                                font.family: theme.ui; font.pixelSize: 13
                                text: "<font color='#c9c8d0'>Sorted by </font><b><font color='#f7f7f5'>"
                                      + (root.sortMode === "score" ? "Score" : "Readers") + "</font></b> ⇅"
                            }
                            MouseArea {
                                id: srtMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.sortMode = (root.sortMode === "score") ? "readers" : "score"
                            }
                        }
                        // view: detailed ⇄ covers
                        Row {
                            Repeater {
                                model: [ { c: false, g: "▤" }, { c: true, g: "▦" } ]
                                delegate: Rectangle {
                                    required property var modelData
                                    width: 38; height: 36
                                    color: (root.compact === modelData.c) ? theme.glassHi : theme.glassTint
                                    border.width: 1; border.color: theme.edge
                                    Text { anchors.centerIn: parent; text: modelData.g
                                           color: (root.compact === modelData.c) ? theme.ink : theme.inkDimmer
                                           font.pixelSize: 15 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                                onClicked: root.compact = modelData.c }
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 18 }

                // ---- the card grid ----
                Grid {
                    id: grid
                    width: parent.width
                    columns: root.compact ? Math.max(2, Math.floor(width / 150))
                                          : Math.max(1, Math.floor(width / 470))
                    columnSpacing: 18; rowSpacing: 18
                    readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

                    Repeater {
                        model: root.genreData.cards
                        delegate: Loader {
                            required property var modelData
                            required property int index
                            width: grid.cellW
                            sourceComponent: root.compact ? coverTile : detailCard
                            onLoaded: { item.card = modelData; item.rank = index + 1 }
                            Connections {
                                target: item
                                ignoreUnknownSignals: true
                                function onOpen(t) { root.seriesRequested(t) }
                            }
                        }
                    }
                }

                // ---- empty / loading states ----
                Text {
                    visible: !root.loading && root.genreData.cards.length === 0
                    text: "Nothing in this genre yet — try another."
                    color: theme.inkDimmer; font.family: theme.display; font.italic: true; font.pixelSize: 16
                    topPadding: 30
                }

                Item { width: 1; height: 44 }
            }
        }
    }

    // ---- fixed back / system controls over the page (mirrors UniversePage) ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        Rectangle {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            width: 42; height: 34; radius: 17
            color: backMa.hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(0,0,0,0.40)
            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 22 }
            HoverHandler { id: backMa }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }

    // ════════════ the rich detailed card (MAL's card, in glass) ════════════
    Component {
        id: detailCard
        Rectangle {
            id: dc
            property var card: ({})
            property int rank: 0
            signal open(string title)
            height: 210; radius: 16
            color: dcMa.containsMouse ? theme.glassHi : theme.glassTint
            border.width: 1
            border.color: dcMa.containsMouse ? Qt.rgba(1,1,1,0.28) : theme.edge

            Text {
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 12
                text: dc.rank < 10 ? "0" + dc.rank : "" + dc.rank
                color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 15
                opacity: 0.7; z: 2
            }

            RowLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 16
                // cover
                Rectangle {
                    Layout.preferredWidth: 96; Layout.preferredHeight: 140
                    Layout.alignment: Qt.AlignTop
                    radius: 8; clip: true
                    gradient: Gradient {
                        GradientStop { position: 0; color: dc.card.c1 || "#33445d" }
                        GradientStop { position: 1; color: dc.card.c2 || "#0c1118" }
                    }
                    Image { anchors.fill: parent; source: dc.card.cover || ""
                            fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true }
                }
                // body
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: dc.card.title || ""; color: theme.ink
                        font.family: theme.display; font.pixelSize: 18; font.weight: Font.Medium
                        font.letterSpacing: -0.2; lineHeight: 1.08
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        rightPadding: 22
                    }
                    Text {
                        Layout.fillWidth: true; topPadding: 6
                        text: [dc.card.type, dc.card.year, dc.card.status].filter(function(s){return s}).join(" · ")
                              + "  ·  " + (dc.card.metaCounts || "")
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    // genre chips (current genre lit gold)
                    Flow {
                        Layout.fillWidth: true; topPadding: 9; spacing: 5
                        Repeater {
                            model: dc.card.genres || []
                            delegate: Rectangle {
                                required property string modelData
                                readonly property bool selected: modelData === root.genreName
                                height: 19; radius: 6; width: chl.implicitWidth + 16
                                color: selected ? Qt.rgba(0.94,0.77,0.29,0.16) : Qt.rgba(1,1,1,0.05)
                                border.width: 1
                                border.color: selected ? Qt.rgba(0.94,0.77,0.29,0.45) : Qt.rgba(1,1,1,0.10)
                                Text { id: chl; anchors.centerIn: parent; text: modelData
                                       color: selected ? theme.gold : theme.inkDim
                                       font.family: theme.ui; font.pixelSize: 11 }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true; Layout.fillHeight: true; topPadding: 10
                        text: dc.card.synopsis || ""
                        color: theme.inkDim; font.family: theme.display; font.pixelSize: 13
                        font.weight: Font.Light; lineHeight: 1.4
                        wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                        clip: true
                    }
                    // foot
                    RowLayout {
                        Layout.fillWidth: true; Layout.topMargin: 12; spacing: 13
                        Row {
                            spacing: 5
                            Text { text: "★"; color: theme.gold; font.pixelSize: 13
                                   anchors.verticalCenter: parent.verticalCenter }
                            Text { text: (dc.card.score !== null && dc.card.score !== undefined) ? dc.card.score : "—"
                                   color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                   anchors.verticalCenter: parent.verticalCenter }
                        }
                        Text { text: (dc.card.members || "0") + " readers"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                        Text { Layout.fillWidth: true; text: dc.card.authors || ""
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                               elide: Text.ElideRight }
                        // + Library (self-contained added state until the library store lands)
                        Rectangle {
                            property bool added: false
                            height: 28; radius: 9; width: addl.implicitWidth + 22
                            color: added ? Qt.rgba(0.94,0.77,0.29,0.16)
                                         : (addMa.containsMouse ? theme.glassHi : Qt.rgba(1,1,1,0.05))
                            border.width: 1
                            border.color: added ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                            Text { id: addl; anchors.centerIn: parent
                                   text: parent.added ? "✓ In Library" : "+ Library"
                                   color: parent.added ? theme.gold : theme.inkDim
                                   font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                            MouseArea { id: addMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: parent.added = !parent.added }
                        }
                    }
                }
            }
            MouseArea {
                id: dcMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // don't steal clicks from the in-card buttons
                propagateComposedEvents: true
                onClicked: dc.open(dc.card.title)
            }
            Behavior on color { ColorAnimation { duration: 130 } }
        }
    }

    // ════════════ the compact cover tile (covers view) ════════════
    Component {
        id: coverTile
        Item {
            id: ct
            property var card: ({})
            property int rank: 0
            signal open(string title)
            height: ct.width * 1.46 + 26
            Rectangle {
                id: cv
                width: parent.width; height: parent.width * 1.46
                radius: 10; clip: true
                border.width: 1; border.color: ctMa.containsMouse ? theme.gold : Qt.rgba(1,1,1,0.08)
                scale: ctMa.containsMouse ? 1.03 : 1.0
                Behavior on scale { NumberAnimation { duration: 130 } }
                gradient: Gradient {
                    GradientStop { position: 0; color: ct.card.c1 || "#33445d" }
                    GradientStop { position: 1; color: ct.card.c2 || "#0c1118" }
                }
                Image { anchors.fill: parent; source: ct.card.cover || ""
                        fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true }
                // score pill
                Rectangle {
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 7
                    height: 20; radius: 6; width: scl.implicitWidth + 14
                    color: Qt.rgba(0,0,0,0.62)
                    Text { id: scl; anchors.centerIn: parent
                           text: "★ " + ((ct.card.score !== null && ct.card.score !== undefined) ? ct.card.score : "—")
                           color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold }
                }
            }
            Text {
                anchors.top: cv.bottom; anchors.topMargin: 7
                width: parent.width; text: ct.card.title || ""
                color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                wrapMode: Text.WordWrap; maximumLineCount: 1; elide: Text.ElideRight
            }
            MouseArea { id: ctMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: ct.open(ct.card.title) }
        }
    }
}
