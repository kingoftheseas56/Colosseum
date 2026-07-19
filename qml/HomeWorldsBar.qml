// HomeWorldsBar — the AF2 Home top menu. A frosted glass pill bar carrying the four
// worlds (Home · Tankoban · Theatre · Biblio) plus search + wallpaper actions. It RECEDES
// (fades + lifts) as the glass board rises on scroll, feeding on a 0..1 `recede`.
//
// Glass is the constant material: pass a `backdrop` (the living wallpaper layer) and the bar
// gets a real backdrop blur via the house Glass primitive; without one it falls to a
// translucent film. Text is the AF2 sans display face (theme.displaySans = Figtree).

import QtQuick
import QtQuick.Effects

Item {
    id: bar

    // ── public API ──
    property real recede: 0                  // 0..1 scroll recede (fade + lift)
    property string activeWorld: "Home"
    property Item backdrop: null             // wallpaper layer to frost (optional)
    signal worldPicked(string world)
    signal searchRequested()
    signal wallpaperRequested()

    Theme { id: theme }

    implicitHeight: 60
    opacity: 1 - Math.min(1, bar.recede * 1.15)
    transform: Translate { y: -bar.recede * 10 }
    // fully receded → stop eating clicks so the board scrolls under it
    enabled: opacity > 0.05

    // ── frosted glass fill: real backdrop blur when given a backdrop, else a film ──
    Loader {
        anchors.fill: parent
        active: bar.backdrop !== null
        sourceComponent: Glass { backdrop: bar.backdrop; radius: 16; tint: 0.055; scrim: 0.0; edge: theme.edgeSoft }
    }
    Rectangle {
        id: glassFilm
        anchors.fill: parent
        radius: 16
        color: bar.backdrop ? "transparent" : Qt.rgba(1, 1, 1, 0.055)
        border.width: 1
        border.color: theme.edgeSoft
        // soft drop shadow (mock: 0 12px 40px rgba(0,0,0,.35))
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.35)
            shadowVerticalOffset: 12
            shadowBlur: 1.0
            autoPaddingEnabled: true
        }
    }

    // ── content (hand-placed; no layout import needed) ──

    // brand wordmark (left)
    Text {
        id: brand
        anchors.left: parent.left; anchors.leftMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        text: "COLOSSEUM"
        color: theme.inkDim
        font.family: theme.displaySans; font.pixelSize: 12
        font.weight: Font.ExtraBold; font.letterSpacing: 1.9
    }

    // world pills (after the brand)
    Row {
        id: worldsRow
        anchors.left: brand.right; anchors.leftMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Repeater {
            model: ["Home", "Tankoban", "Theatre", "Biblio"]
            delegate: Rectangle {
                required property string modelData
                readonly property bool on: modelData === bar.activeWorld
                height: 34; radius: 17
                width: pillText.implicitWidth + 30
                color: on ? theme.ink
                      : pillMa.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                border.width: 1
                border.color: on ? "transparent" : Qt.rgba(1, 1, 1, 0.0)
                Behavior on color { ColorAnimation { duration: 140 } }
                Text {
                    id: pillText
                    anchors.centerIn: parent
                    text: modelData
                    color: parent.on ? "#0c0d12" : (pillMa.containsMouse ? theme.ink : theme.inkDim)
                    font.family: theme.displaySans; font.pixelSize: 14
                    font.weight: parent.on ? Font.Bold : Font.DemiBold
                }
                MouseArea {
                    id: pillMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.worldPicked(parent.modelData)
                }
            }
        }
    }

    // right group: search + wallpaper glass icon buttons
    Row {
        anchors.right: parent.right; anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        component IconBtn: Rectangle {
            property alias icon: img.source
            signal activated()
            width: 38; height: 38; radius: 19
            color: iconMa.containsMouse ? theme.glassHi : Qt.rgba(1, 1, 1, 0.055)
            border.width: 1; border.color: theme.edgeSoft
            Behavior on color { ColorAnimation { duration: 140 } }
            Image {
                id: img
                anchors.centerIn: parent
                width: 17; height: 17
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 34; sourceSize.height: 34
                opacity: iconMa.containsMouse ? 1.0 : 0.82
            }
            MouseArea {
                id: iconMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: parent.activated()
            }
        }

        IconBtn { icon: "../assets/icons/search.svg"; onActivated: bar.searchRequested() }
        IconBtn { icon: "../assets/icons/wallpaper.svg"; onActivated: bar.wallpaperRequested() }
    }
}
