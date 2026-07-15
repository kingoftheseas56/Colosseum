// DragonBallUniversePage — the bespoke SAGA page for Dragon Ball (Agent 5, 2026-07-15,
// Hemanth free-reign commission). Not the generic anime template: an authored page whose
// SIGNATURE is the seven anime rendered as the seven Dragon Balls — each an orange orb
// bearing its star-count, "collect all seven" to walk the whole saga. The numbering is
// EARNED (the balls ARE numbered by stars; the anime ARE a broadcast sequence), never decor.
//
// Data = the pinned curation in Universes.js (saga / filmEras / manga). Every anime + film
// tile dresses DIRECTLY by its verified Cinemeta id via live.metahub.space (IPv4-pinned) —
// no name-search, so Dragon Ball's same-name impostors (two "Dragon Ball Z" ids) can't slip
// in. Manga tiles carry AniList covers (s4.anilist.co, IPv4-pinned) and open the manga
// reader by title. Anime + films → A4's TheatreSeries (watchRequested); manga → the manga
// reader (seriesRequested). Chrome is house-quiet; the boldness is spent on the orbs alone.
import QtQuick
import QtQuick.Controls
import "Universes.js" as UDB

Item {
    id: root
    anchors.fill: parent

    // shell contract
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)         // anime era / film → A4's TheatreSeries.qml
    signal seriesRequested(string title)    // manga → the manga reader

    Theme { id: theme }

    // Dragon Ball palette — warm, subject-derived, layered over the house dark/gold.
    readonly property color ballHi:  "#f7b25a"   // orb highlight (lit top)
    readonly property color ballLo:  "#d0611a"   // orb shadow (Goku-gi orange)
    readonly property color starRed: "#c62828"   // the stars on the balls
    readonly property color ssjGold: "#f7c948"   // Super Saiyan glow (accent, sparing)

    property var uni: ({ name: "", blurb: "", banner: "", saga: [], filmEras: [], manga: [],
                         firstWatch: null })
    function reload() {
        if (!root.universeName.length) return
        var arr = UDB.universes
        for (var i = 0; i < arr.length; i++)
            if (arr[i].name === root.universeName) { root.uni = arr[i]; return }
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    function poster(id) { return id ? "https://live.metahub.space/poster/medium/" + id + "/img" : "" }
    function backdropFor(id) { return id ? "https://live.metahub.space/background/medium/" + id + "/img" : "" }

    // ---- the wall ----
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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.05, 0.032, 0.014, 0.9) }   // warm night
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
                width: parent.width; height: 380
                Image {
                    anchors.fill: parent
                    source: root.uni.banner
                    fillMode: Image.PreserveAspectCrop
                    cache: true
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.05,0.032,0.014,0.20) }
                        GradientStop { position: 0.5; color: Qt.rgba(0.05,0.032,0.014,0.10) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.05,0.032,0.014,0.95) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 30
                    spacing: 9
                    Text { text: "UNIVERSE  ·  THE COMPLETE SAGA"; color: root.ssjGold; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.uni.name; color: theme.ink
                           font.family: theme.display; font.pixelSize: 64 }
                    Text {
                        text: "Seven anime  ·  Twenty-five films  ·  Eight manga  —  by Akira Toriyama"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // ===== BODY =====
            Column {
                x: 54; width: parent.width - 108; spacing: 0
                topPadding: 26

                // blurb + "Begin here"
                Row {
                    width: parent.width
                    bottomPadding: 8
                    spacing: 30
                    Text {
                        width: parent.width - (beginBtn.visible ? beginBtn.width + 30 : 0)
                        text: root.uni.blurb
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                        lineHeight: 1.5; wrapMode: Text.WordWrap
                        maximumLineCount: 3; elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        id: beginBtn
                        radius: 12; height: 50; width: beginRow.implicitWidth + 46
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !!root.uni.firstWatch
                        gradient: Gradient {
                            GradientStop { position: 0; color: beginMa.containsMouse ? Qt.rgba(0.94,0.55,0.16,0.34) : Qt.rgba(1,1,1,0.12) }
                            GradientStop { position: 1; color: beginMa.containsMouse ? Qt.rgba(0.82,0.38,0.10,0.20) : Qt.rgba(1,1,1,0.05) }
                        }
                        border.width: 1
                        border.color: beginMa.containsMouse ? root.ssjGold : Qt.rgba(1,1,1,0.26)
                        Behavior on border.color { ColorAnimation { duration: 160 } }
                        Row {
                            id: beginRow; anchors.centerIn: parent; spacing: 10
                            Text { text: "Begin here"; color: theme.ink
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "→"; color: root.ssjGold; font.pixelSize: 16
                                anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea {
                            id: beginMa; anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: if (root.uni.firstWatch) root.watchRequested(root.uni.firstWatch)
                        }
                    }
                }

                Item { width: 1; height: 34 }

                // ===== SIGNATURE — THE SEVEN-STAR SAGA =====
                Row {
                    spacing: 14
                    Text { text: "The Seven-Star Saga"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28 }
                    Text { text: "every anime, in order — collect all seven"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                           anchors.baseline: parent.children[0].baseline }
                }
                Item { width: 1; height: 20 }

                Flickable {
                    width: parent.width
                    height: 288
                    contentWidth: orbRow.implicitWidth; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: orbRow
                        spacing: 8
                        Repeater {
                            model: root.uni.saga
                            delegate: Item {
                                id: orbTile
                                required property var modelData
                                width: 196; height: 280

                                Column {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    spacing: 16
                                    // ---- THE ORB ----
                                    Item {
                                        width: 150; height: 150
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        // ambient aura (gold on hover — Super Saiyan)
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: orbMa.containsMouse ? 150 : 138
                                            height: width; radius: width / 2
                                            color: "transparent"
                                            border.width: orbMa.containsMouse ? 10 : 6
                                            border.color: orbMa.containsMouse ? Qt.rgba(0.97,0.79,0.29,0.34)
                                                                              : Qt.rgba(0.85,0.42,0.12,0.16)
                                            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                            Behavior on border.color { ColorAnimation { duration: 180 } }
                                        }
                                        // the sphere
                                        Rectangle {
                                            id: sphere
                                            anchors.centerIn: parent
                                            width: 124; height: 124; radius: 62
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: root.ballHi }
                                                GradientStop { position: 1.0; color: root.ballLo }
                                            }
                                            border.width: 1
                                            border.color: orbMa.containsMouse ? root.ssjGold : Qt.rgba(1,1,1,0.18)
                                            // gloss highlight (upper-left)
                                            Rectangle {
                                                x: 26; y: 18; width: 42; height: 26; radius: 13
                                                color: Qt.rgba(1, 1, 1, 0.32); rotation: -28
                                            }
                                            // ---- the stars: N = this ball's number ----
                                            Grid {
                                                anchors.centerIn: parent
                                                columns: Math.max(1, Math.ceil(Math.sqrt(orbTile.modelData.star)))
                                                rowSpacing: -2; columnSpacing: 1
                                                Repeater {
                                                    model: orbTile.modelData.star
                                                    delegate: Text {
                                                        text: "★"
                                                        color: root.starRed
                                                        style: Text.Outline; styleColor: Qt.rgba(0.36,0.05,0.05,0.6)
                                                        font.pixelSize: orbTile.modelData.star <= 3 ? 22
                                                                      : orbTile.modelData.star <= 5 ? 18 : 15
                                                    }
                                                }
                                            }
                                        }
                                        MouseArea {
                                            id: orbMa
                                            anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.watchRequested(orbTile.modelData)
                                        }
                                    }
                                    // ---- era name / year / note ----
                                    Column {
                                        width: 190
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        spacing: 3
                                        Text {
                                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                                            text: orbTile.modelData.era
                                            color: orbMa.containsMouse ? root.ssjGold : theme.ink
                                            font.family: theme.display; font.italic: true; font.pixelSize: 17
                                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                            Behavior on color { ColorAnimation { duration: 140 } }
                                        }
                                        Text {
                                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                                            text: orbTile.modelData.year
                                            color: root.ssjGold; font.family: theme.ui
                                            font.pixelSize: 11; font.letterSpacing: 1
                                        }
                                        Text {
                                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                                            text: orbTile.modelData.note
                                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 46 }

                // ===== THE FILMS — grouped, chronological =====
                Repeater {
                    model: root.uni.filmEras
                    delegate: Column {
                        id: filmEra
                        required property var modelData
                        width: parent.width
                        spacing: 16
                        bottomPadding: 36
                        Row {
                            spacing: 12
                            Text { text: filmEra.modelData.era; color: theme.ink
                                   font.family: theme.display; font.pixelSize: 24 }
                            Text { text: filmEra.modelData.films.length + " films"
                                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                                   anchors.baseline: parent.children[0].baseline }
                        }
                        Flickable {
                            width: parent.width; height: 238
                            contentWidth: filmRow.implicitWidth; contentHeight: height
                            clip: true
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            Row {
                                id: filmRow
                                spacing: 16
                                Repeater {
                                    model: filmEra.modelData.films
                                    delegate: Item {
                                        id: fTile
                                        required property var modelData
                                        width: 150; height: 226
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8; clip: true
                                            color: "#1c1109"
                                            border.width: 1
                                            border.color: fMa.containsMouse ? root.ssjGold : Qt.rgba(1,1,1,0.12)
                                            Image {
                                                anchors.fill: parent
                                                source: root.poster(fTile.modelData.id)
                                                asynchronous: true; cache: true
                                                fillMode: Image.PreserveAspectCrop
                                                opacity: status === Image.Ready ? 1 : 0
                                                Behavior on opacity { NumberAnimation { duration: 220 } }
                                            }
                                            // title plate (always legible even if art misses)
                                            Rectangle {
                                                anchors.left: parent.left; anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                height: 58
                                                gradient: Gradient {
                                                    GradientStop { position: 0; color: "transparent" }
                                                    GradientStop { position: 1; color: Qt.rgba(0,0,0,0.9) }
                                                }
                                            }
                                            Column {
                                                anchors.left: parent.left; anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.margins: 9
                                                spacing: 1
                                                Text {
                                                    width: parent.width
                                                    text: fTile.modelData.t
                                                    color: theme.ink; font.family: theme.ui
                                                    font.pixelSize: 12; font.weight: Font.DemiBold
                                                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                                }
                                                Text {
                                                    text: fTile.modelData.year
                                                    color: root.ssjGold; font.family: theme.ui; font.pixelSize: 10
                                                }
                                            }
                                            MouseArea {
                                                id: fMa
                                                anchors.fill: parent
                                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                onClicked: root.watchRequested(fTile.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ===== THE MANGA — the source, and where it kept going =====
                Column {
                    width: parent.width
                    spacing: 16
                    visible: root.uni.manga.length > 0
                    bottomPadding: 60
                    Row {
                        spacing: 12
                        Text { text: "The Manga"; color: theme.ink
                               font.family: theme.display; font.pixelSize: 24 }
                        Text { text: "Toriyama's source, and the spin-offs it grew"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                               anchors.baseline: parent.children[0].baseline }
                    }
                    Flickable {
                        width: parent.width; height: 238
                        contentWidth: mangaRow.implicitWidth; contentHeight: height
                        clip: true
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        Row {
                            id: mangaRow
                            spacing: 18
                            Repeater {
                                model: root.uni.manga
                                delegate: Item {
                                    id: mTile
                                    required property var modelData
                                    width: 150; height: 232
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8; clip: true
                                        color: "#180f22"
                                        border.width: 1
                                        border.color: mMa.containsMouse ? root.ssjGold : Qt.rgba(1,1,1,0.12)
                                        Image {
                                            anchors.fill: parent
                                            source: mTile.modelData.cover || ""
                                            asynchronous: true; cache: true
                                            fillMode: Image.PreserveAspectCrop
                                            opacity: status === Image.Ready ? 1 : 0
                                            Behavior on opacity { NumberAnimation { duration: 220 } }
                                        }
                                        Rectangle {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 54
                                            gradient: Gradient {
                                                GradientStop { position: 0; color: "transparent" }
                                                GradientStop { position: 1; color: Qt.rgba(0,0,0,0.88) }
                                            }
                                        }
                                        Text {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 9
                                            text: mTile.modelData.t
                                            color: theme.ink; font.family: theme.ui
                                            font.pixelSize: 12; font.weight: Font.DemiBold
                                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                        }
                                        MouseArea {
                                            id: mMa
                                            anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.seriesRequested(mTile.modelData.q || mTile.modelData.t)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
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
