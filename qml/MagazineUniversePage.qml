// MagazineUniversePage — the MAGAZINE universe template (Weekly Shonen Jump). The magazine
// publishes MANGA — nothing else exists on this page (Hemanth 2026-07-12: no anime, no
// films). Its soul is the weekly TABLE OF CONTENTS ranked by the reader's vote, so the page
// IS the contents spread: a red masthead wearing the iconic cover, then the flagships as a
// ranked roster — giant gold numerals, cover, title, chapter count, Read →. Every entry
// routes into A1's MangaSeries by title (the manga lane's own door).
import QtQuick
import QtQuick.Controls
import "UniverseApi.js" as Api

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors UniversePage — read-only verbs)
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal seriesRequested(string title)    // a roster entry → MangaSeries

    Theme { id: theme }
    property var uni: ({ name: "", blurb: "", banner: "", manga: [] })

    function reload() {
        if (!root.universeName.length) return      // never load a default universe
        Api.loadMangaOnly(root.universeName, function(u) { if (u) root.uni = u; })
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    // ---- the newsstand wall ----
    Item {
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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.02, 0.025, 0.88) }
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

            // ===== THE MASTHEAD — Jump red, the iconic cover standing at the right =====
            Item {
                width: parent.width; height: 340
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#7e1216" }   // the Jump red, aged dark
                        GradientStop { position: 0.62; color: "#45090c" }
                        GradientStop { position: 1.0; color: "#200405" }
                    }
                }
                // the issue-one cover, presented like a magazine on the stand
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: 84
                    anchors.verticalCenter: parent.verticalCenter
                    width: 168; height: 244; radius: 6
                    rotation: 4
                    clip: true
                    color: "#2a0d0e"
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.28)
                    Image {
                        anchors.fill: parent
                        source: root.uni.banner
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 54
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 30
                    anchors.right: parent.right; anchors.rightMargin: 300
                    spacing: 10
                    Text { text: "UNIVERSE  ·  THE MAGAZINE  ·  SHUEISHA, SINCE 1968"
                           color: "#f0c44a"; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.uni.name; color: theme.ink
                           font.family: theme.display; font.pixelSize: 58 }
                    Text {
                        width: parent.width
                        text: root.uni.blurb
                        color: Qt.rgba(0.97, 0.95, 0.93, 0.8)
                        font.family: theme.ui; font.pixelSize: 15
                        lineHeight: 1.4; wrapMode: Text.WordWrap
                        maximumLineCount: 2; elide: Text.ElideRight
                    }
                }
            }

            // ===== THE TABLE OF CONTENTS — the lineup, ranked =====
            Column {
                x: 54; width: parent.width - 108
                topPadding: 34
                spacing: 18

                Row {
                    spacing: 12
                    Text { text: "This Week's Lineup"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 25 }
                    Text { text: root.uni.manga.length + " flagships  ·  the reader's vote"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                           anchors.baseline: parent.children[0].baseline }
                }

                Column {
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: root.uni.manga
                        delegate: Rectangle {
                            id: entry
                            required property var modelData
                            required property int index
                            width: parent.width; height: 96
                            radius: 12
                            color: entryMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.045)
                            border.width: 1
                            border.color: entryMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6)
                                                                : Qt.rgba(0.97, 0.97, 0.96, 0.08)
                            Behavior on color { ColorAnimation { duration: 140 } }

                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 26
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 24
                                // the rank numeral — the vote made visible
                                Text {
                                    width: 64
                                    text: (entry.index + 1 < 10 ? "0" : "") + (entry.index + 1)
                                    color: entry.index === 0 ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.45)
                                    font.family: theme.display; font.italic: true; font.pixelSize: 44
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {   // the cover thumb, magazine-listing sized
                                    width: 52; height: 74; radius: 4; clip: true
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: entry.modelData.c1 || "#2a1a14"
                                    Image {
                                        anchors.fill: parent
                                        source: entry.modelData.cover || ""
                                        asynchronous: true; cache: true
                                        fillMode: Image.PreserveAspectCrop
                                        opacity: status === Image.Ready ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 220 } }
                                    }
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5
                                    Text { text: entry.modelData.title
                                           color: theme.ink; font.family: theme.display; font.pixelSize: 21 }
                                    Text { text: entry.modelData.chapters
                                                 ? entry.modelData.chapters + " chapters"
                                                 : "Serialized in Jump"
                                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                                }
                            }
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 26
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                opacity: entryMa.containsMouse ? 1 : 0.55
                                Behavior on opacity { NumberAnimation { duration: 140 } }
                                Text { text: "Read"; color: theme.ink
                                       font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                                Text { text: "→"; color: theme.gold; font.pixelSize: 15 }
                            }
                            MouseArea {
                                id: entryMa
                                anchors.fill: parent
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.seriesRequested(entry.modelData.title)
                            }
                        }
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
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.closeRequested() }
        }
    }
}
