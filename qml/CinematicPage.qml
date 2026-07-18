// CinematicPage - the CINEMATIC universe template (MCU), live. PROTOTYPE harness:
//   qml.exe qml\_cinemacheck.qml
// Approved design: obvious banner -> phases down the page, each a CHAPTER PANEL (the capstone the
// phase builds to + the road-to films + the phase's own description). ALL copy from the Fandom MCU
// Wiki (McuApi.js) - never written here. Embeddable in the shell like UniversePage.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "McuApi.js" as Mcu
import "SagaApi.js" as Saga
import "Universes.js" as UDB

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors UniversePage)
    property Item backdrop: null
    property string universeName: "Marvel Cinematic Universe"
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)       // a film tile -> A4's TheatreSeries.qml (Cinemeta id)

    // build the Cinemeta item a film tile hands to TheatreSeries (id resolved by McuApi)
    function watchItem(film) {
        return { id: film.id || "", type: film.type || "movie", title: film.title || "",
                 cover: film.poster || "", art: film.poster || "" };
    }

    Theme { id: theme }
    property var mcu: ({ phases: [] })
    property bool loading: true

    // THE TELEVISION ACT (Hemanth 2026-07-13): the Marvel Studios series + the two Special
    // Presentations, curated id-pinned in Universes.js and slot-resolved by the saga lane.
    // Each tile wears its PHASE PLATE — phase identity survives without congesting the
    // film chapter panels (his concern; layout = the surprise he commissioned).
    property var tv: ({ shows: [], films: [] })
    readonly property var showPhases: (UDB.configFor(root.universeName) || {}).mcuShowPhases || ({})

    Component.onCompleted: {
        Mcu.loadMcu(function(d) {
            if (d) { root.mcu = d; }
            root.loading = false;
        })
        Saga.loadSaga(root.universeName, function(u) { if (u) root.tv = u })
    }

    function totalFilms() {
        var n = 0; for (var i = 0; i < mcu.phases.length; i++) n += mcu.phases[i].films.length; return n;
    }

    // ---- background ----
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0608" }
            GradientStop { position: 0.6; color: "#08070b" }
            GradientStop { position: 1.0; color: "#07060a" }
        }
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

            // ===== OBVIOUS BANNER =====
            Item {
                width: parent.width; height: 250
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#2a0c10" }
                        GradientStop { position: 0.55; color: "#7a1820" }
                        GradientStop { position: 1.0; color: "#b81d24" }
                    }
                }
                // red glow + floor fade
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.45; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0.03,0.027,0.043,0.95) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.bottomMargin: 26; spacing: 9
                    Text { text: "CINEMATIC UNIVERSE"; color: theme.gold; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.universeName; color: theme.ink
                           font.family: theme.display; font.pixelSize: 52 }
                    Text {
                        text: root.mcu.phases.length
                              ? (root.mcu.phases.length + " Phases   ·   " + root.totalFilms() + " Films"
                                 + (root.tv.shows.length ? "   ·   " + root.tv.shows.length + " Series" : "")
                                 + "   ·   2 Sagas")
                              : "Loading the saga…"
                        color: Qt.rgba(1,1,1,0.72); font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // ===== PHASES (chapter panels) =====
            Column {
                x: 54; width: parent.width - 108; spacing: 22
                topPadding: 28; bottomPadding: 54

                Repeater {
                    model: root.mcu.phases
                    delegate: ChapterPanel { width: parent.width; phase: modelData }
                }
            }

            // ===== THE SERIES — the streaming age (phase plates ride the tiles) =====
            TvShelf {
                x: 54; width: parent.width - 108
                title: "The Series"
                sub: "the streaming age — every tile wears its phase"
                items: root.tv.shows
            }

            // ===== SPECIAL PRESENTATIONS — the one-night events =====
            TvShelf {
                x: 54; width: parent.width - 108
                title: "Special Presentations"
                sub: "one-night events"
                items: root.tv.films
                bottomPadding: 54
            }
        }
    }

    // one shelf = header + a horizontal walk of tiles; each tile carries its gold PHASE
    // plate (top-left, from the curated map) and the UPCOMING plate when the date is ahead
    component TvShelf: Column {
        id: shelf
        property string title
        property string sub
        property var items: []
        spacing: 16
        visible: items && items.length > 0
        Row {
            spacing: 12
            Text { text: shelf.title; color: theme.ink
                   font.family: theme.display; font.pixelSize: 25 }
            Text { text: (shelf.items ? shelf.items.length : 0) + " titles  ·  " + shelf.sub
                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                   anchors.baseline: parent.children[0].baseline }
        }
        Flickable {
            width: parent.width; height: 244
            contentWidth: shelfRow.width; contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: shelfRow
                spacing: 18
                Repeater {
                    model: shelf.items
                    delegate: Item {
                        id: tvTile
                        required property var modelData
                        width: 150; height: 232
                        Rectangle {
                            anchors.fill: parent
                            radius: 8; clip: true
                            color: "#1c0e10"
                            border.width: 1
                            border.color: tvMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                             : Qt.rgba(0.97,0.97,0.96,0.12)
                            Image {
                                anchors.fill: parent
                                source: tvTile.modelData.cover || ""
                                asynchronous: true; cache: true
                                fillMode: Image.PreserveAspectCrop
                                opacity: status === Image.Ready ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 220 } }
                            }
                            Rectangle {   // the PHASE plate — the room's soul, one numeral per tile
                                visible: !!root.showPhases[tvTile.modelData.id]
                                anchors.top: parent.top; anchors.left: parent.left
                                anchors.margins: 8
                                radius: 6
                                color: Qt.rgba(0, 0, 0, 0.66)
                                border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.55)
                                width: phaseTxt.implicitWidth + 12; height: 22
                                Text { id: phaseTxt; anchors.centerIn: parent
                                       text: root.showPhases[tvTile.modelData.id] || ""
                                       color: theme.gold; font.family: theme.display
                                       font.italic: true; font.pixelSize: 12 }
                            }
                            Rectangle {   // UPCOMING plate — future work stays, marked
                                anchors.top: parent.top; anchors.right: parent.right
                                anchors.margins: 8
                                visible: tvTile.modelData.upcoming === true
                                radius: 4
                                color: Qt.rgba(0, 0, 0, 0.72)
                                border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.5)
                                width: tvUpTag.implicitWidth + 12; height: tvUpTag.implicitHeight + 6
                                Text { id: tvUpTag; anchors.centerIn: parent
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
                                text: tvTile.modelData.title
                                color: theme.ink; font.family: theme.ui
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap; maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: tvMa
                                anchors.fill: parent
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.watchRequested(tvTile.modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- fixed back / system controls ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        BackAction {
            variant: "capsule"; tip: "Back"
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            onTriggered: root.backRequested()
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26; spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }

    // ===== one phase = a chapter panel: the capstone it builds to + the road-to films + the desc =====
    component ChapterPanel: Rectangle {
        id: cp
        property var phase
        property var roadFilms: phase && phase.films ? phase.films.slice(0, Math.max(0, phase.films.length - 1)) : []
        // grow to fit the content (+44 = the RowLayout's 22px margins top+bottom); 270 is the floor so
        // short phases still look substantial. Fixed height was clipping 2-line film captions out the bottom.
        implicitHeight: Math.max(270, body.implicitHeight + 44)
        radius: 18
        color: Qt.rgba(0.078, 0.06, 0.086, 0.55)
        border.width: 1; border.color: theme.edge

        RowLayout {
            anchors.fill: parent; anchors.margins: 22; spacing: 24

            // --- the capstone (left) — a fixed 2:3 poster so it stays clean as the panel grows ---
            Item {
                Layout.preferredWidth: 172; Layout.preferredHeight: 258
                Layout.alignment: Qt.AlignVCenter
                Rectangle {
                    anchors.fill: parent; radius: 12; clip: true
                    color: Qt.rgba(1,1,1,0.04); border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.4)
                    scale: capHov.hovered ? 1.03 : 1.0
                    Behavior on scale { NumberAnimation { duration: 130 } }
                    Image {
                        anchors.fill: parent
                        source: cp.phase && cp.phase.capstone ? cp.phase.capstone.poster : ""
                        fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true
                    }
                    HoverHandler { id: capHov }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: if (cp.phase && cp.phase.capstone && cp.phase.capstone.id)
                                       root.watchRequested(root.watchItem(cp.phase.capstone))
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 78
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.86) }
                        }
                    }
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10; spacing: 2
                        Text { text: "✦ THE PHASE CONVERGES"; color: theme.gold; font.family: theme.ui
                               font.pixelSize: 8; font.letterSpacing: 1.5; font.bold: true }
                        Text { text: cp.phase && cp.phase.capstone ? cp.phase.capstone.title : ""
                               color: "white"; font.family: theme.ui; font.pixelSize: 12; font.bold: true
                               width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                    }
                }
            }

            // --- the body (right) ---
            ColumnLayout {
                id: body
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                Text { text: cp.phase ? cp.phase.phase.toUpperCase() : ""; color: theme.gold
                       font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 3; font.bold: true }
                Text { text: cp.phase ? cp.phase.saga : ""; color: theme.ink
                       font.family: theme.display; font.pixelSize: 24 }
                Text {
                    text: cp.phase ? cp.phase.description : ""
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; lineHeight: 1.4
                    Layout.fillWidth: true; Layout.topMargin: 4
                    wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                }
                Item { Layout.preferredHeight: 12 }   // breathing room before the road row
                Text { text: "THE ROAD"; color: theme.inkDimmer; font.family: theme.ui
                       font.pixelSize: 10; font.letterSpacing: 2; Layout.bottomMargin: 2 }
                // a Flow, not a Row — Phase 3 carries ten road films and a single line ran
                // off the right edge of the screen (Hemanth 2026-07-18). Wrapping keeps every
                // poster visible; the panel already grows to fit its body.
                Flow {
                    Layout.fillWidth: true
                    spacing: 14
                    Repeater {
                        model: cp.roadFilms
                        delegate: Column {
                            width: 116
                            Rectangle {
                                width: 116; height: 172; radius: 8; clip: true
                                color: Qt.rgba(1,1,1,0.05); border.width: 1; border.color: Qt.rgba(1,1,1,0.08)
                                scale: rfHov.hovered ? 1.05 : 1.0
                                Behavior on scale { NumberAnimation { duration: 120 } }
                                Image { anchors.fill: parent; source: modelData.poster || ""
                                        fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true }
                                HoverHandler { id: rfHov }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: if (modelData.id) root.watchRequested(root.watchItem(modelData)) }
                            }
                            Text { text: modelData.title; color: theme.inkDimmer; font.family: theme.ui
                                   font.pixelSize: 11; width: 116; wrapMode: Text.WordWrap; maximumLineCount: 2
                                   elide: Text.ElideRight; topPadding: 6 }
                        }
                    }
                }
            }
        }
    }

    ScrollGlide { flick: page }
}
