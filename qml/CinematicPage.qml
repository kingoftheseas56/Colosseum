// CinematicPage - the CINEMATIC universe template (MCU), live. PROTOTYPE harness:
//   qml.exe qml\_cinemacheck.qml
// Approved design: obvious banner -> phases down the page, each a CHAPTER PANEL (the capstone the
// phase builds to + the road-to films + the phase's own description). ALL copy from the Fandom MCU
// Wiki (McuApi.js) - never written here. Embeddable in the shell like UniversePage.
import QtQuick
import QtQuick.Layouts
import "McuApi.js" as Mcu

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
    signal titleRequested(string title)   // a film tile -> Theatre detail (wired later)

    Theme { id: theme }
    property var mcu: ({ phases: [] })
    property bool loading: true

    Component.onCompleted: Mcu.loadMcu(function(d) {
        if (d) { root.mcu = d; }
        root.loading = false;
    })

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
                              ? (root.mcu.phases.length + " Phases   ·   " + root.totalFilms() + " Films   ·   2 Sagas")
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
        }
    }

    // ---- fixed back / system controls ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        Rectangle {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            width: 42; height: 34; radius: 17
            color: backMa.hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(0,0,0,0.4)
            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 22 }
            HoverHandler { id: backMa }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
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
        implicitHeight: 270
        radius: 18
        color: Qt.rgba(0.078, 0.06, 0.086, 0.55)
        border.width: 1; border.color: theme.edge

        RowLayout {
            anchors.fill: parent; anchors.margins: 22; spacing: 24

            // --- the capstone (left) ---
            Item {
                Layout.preferredWidth: 158; Layout.fillHeight: true
                Rectangle {
                    anchors.fill: parent; radius: 12; clip: true
                    color: Qt.rgba(1,1,1,0.04); border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.4)
                    Image {
                        anchors.fill: parent
                        source: cp.phase && cp.phase.capstone ? cp.phase.capstone.poster : ""
                        fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true
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
                Item { Layout.fillHeight: true }   // push the road row to the bottom
                Text { text: "THE ROAD"; color: theme.inkDimmer; font.family: theme.ui
                       font.pixelSize: 10; font.letterSpacing: 2; Layout.bottomMargin: 2 }
                Row {
                    spacing: 10
                    Repeater {
                        model: cp.roadFilms
                        delegate: Column {
                            width: 72
                            Rectangle {
                                width: 72; height: 106; radius: 7; clip: true
                                color: Qt.rgba(1,1,1,0.05); border.width: 1; border.color: Qt.rgba(1,1,1,0.08)
                                scale: rfHov.hovered ? 1.05 : 1.0
                                Behavior on scale { NumberAnimation { duration: 120 } }
                                Image { anchors.fill: parent; source: modelData.poster || ""
                                        fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true }
                                HoverHandler { id: rfHov }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.titleRequested(modelData.title) }
                            }
                            Text { text: modelData.title; color: theme.inkDimmer; font.family: theme.ui
                                   font.pixelSize: 10; width: 72; wrapMode: Text.WordWrap; maximumLineCount: 2
                                   elide: Text.ElideRight; topPadding: 5 }
                        }
                    }
                }
            }
        }
    }
}
