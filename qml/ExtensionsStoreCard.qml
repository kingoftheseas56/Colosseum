// ExtensionsStoreCard — one add-on in a Store row, to the measurements of
// agents/colosseum-extensions-world-mock.html (.addon / .art / .wash / .logo-stage):
// the add-on's own logo blown up and blurred into a colour wash, the crisp logo centred on a
// tile, then name and two lines of description. No button: the card is the button. Install
// state is a quiet corner badge that only appears once the add-on is installed or being added.
import QtQuick
import QtQuick.Effects
import "ExtensionsStoreApi.js" as StoreApi

Item {
    id: card
    objectName: card.addon && card.addon.slug ? "extensionsCard_" + card.addon.slug : ""

    property var addon: ({})
    property bool installed: false
    property bool busy: false
    property bool selected: false            // keyboard focus from the row
    property bool phoneLayout: false
    signal activated()

    readonly property bool lit: card.selected || action.hovered
    readonly property real h: StoreApi.hue(card.addon.name || "")

    width: 365
    height: phoneLayout ? mobileInstall.y + mobileInstall.height + 12 : art.height + 12 + 24 + 3 + 40

    Theme { id: theme }

    Rectangle {
        id: art
        width: parent.width
        height: card.phoneLayout ? 140 : Math.round(parent.width * 220 / 365)
        radius: 23
        clip: true
        border.width: 1
        border.color: card.lit ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42) : Qt.rgba(1, 1, 1, 0.10)
        Behavior on border.color { ColorAnimation { duration: 180 } }
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.hsla(card.h, 0.34, 0.16, 1) }
            GradientStop { position: 1; color: Qt.hsla((card.h + 30 / 360) % 1, 0.32, 0.10, 1) }
        }

        // The wash: the logo itself, huge, blurred and saturated (mock: 56% of a 170% box,
        // scale 1.35, blur 36px, saturate 1.85, brightness .72, opacity .74).
        Image {
            id: washSource
            anchors.centerIn: parent
            width: art.width * 1.7 * 0.56 * 1.35
            height: width
            source: card.addon.logo || ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: false
            sourceSize.width: 160; sourceSize.height: 160
        }
        MultiEffect {
            anchors.fill: washSource
            source: washSource
            visible: washSource.status === Image.Ready
            blurEnabled: true; blur: 1.0; blurMax: 64; blurMultiplier: 1.6
            saturation: 0.85
            brightness: -0.28
            opacity: 0.74
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.03) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.18) }
            }
        }

        Rectangle {
            id: stage
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: card.lit ? -4.5 : 0
            width: card.phoneLayout ? 82 : 112; height: width
            radius: 26
            scale: card.lit ? 1.045 : 1
            color: Qt.rgba(24 / 255, 25 / 255, 31 / 255, 0.92)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.09)
            clip: true
            Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on anchors.verticalCenterOffset { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            Image {
                id: logo
                anchors.centerIn: parent
                width: parent.width * 0.78; height: width
                source: card.addon.logo || ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize.width: 192; sourceSize.height: 192
            }
            Text {                              // no logo, or it failed: the initial
                anchors.centerIn: parent
                visible: logo.status !== Image.Ready
                text: String(card.addon.title || card.addon.name || "?").charAt(0).toUpperCase()
                color: theme.inkDim
                font.family: theme.display; font.pixelSize: 42
            }
        }

        // Quiet state badge — only when there is something to say.
        Rectangle {
            visible: !card.phoneLayout && (card.installed || card.busy)
            anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14
            width: badgeText.implicitWidth + 22; height: 26
            radius: 13
            color: Qt.rgba(10 / 255, 11 / 255, 15 / 255, 0.62)
            border.width: 1
            border.color: card.installed ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.45) : Qt.rgba(1, 1, 1, 0.16)
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: card.busy ? "Adding…" : "✓ Installed"
                color: card.installed ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
            }
        }
    }

    Text {
        id: title
        anchors.top: art.bottom; anchors.topMargin: 12
        width: parent.width
        text: card.addon.title || card.addon.name || ""
        color: theme.ink
        font.family: theme.ui; font.pixelSize: 18; font.weight: Font.DemiBold
        wrapMode: card.phoneLayout ? Text.WordWrap : Text.NoWrap
        elide: card.phoneLayout ? Text.ElideNone : Text.ElideRight
    }
    Text {
        id: description
        anchors.top: title.bottom; anchors.topMargin: 3
        width: parent.width
        height: card.phoneLayout ? implicitHeight : 40
        text: card.addon.description || ""
        color: "#aaa69e"
        font.family: theme.ui; font.pixelSize: 14
        wrapMode: Text.WordWrap
        maximumLineCount: card.phoneLayout ? 100 : 2
        elide: card.phoneLayout ? Text.ElideNone : Text.ElideRight
    }

    Rectangle {
        id: mobileInstall
        visible: card.phoneLayout
        z: 2
        anchors.top: description.bottom
        anchors.topMargin: 12
        anchors.right: parent.right
        width: 130; height: 38; radius: 8
        color: card.installed ? "#45403a" : "#e9c46a"
        Text {
            anchors.centerIn: parent
            text: card.busy ? "Adding…" : card.installed
                  ? (card.addon.configurable ? "Set up" : "Installed")
                  : (card.addon.configurable ? "Set up" : "Install")
            color: card.installed ? "#ffffff" : "#171717"
            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
        }
        KeyboardAction {
            anchors.fill: parent
            accessibleName: (card.addon.title || card.addon.name || "") + ", "
                            + (card.installed ? "installed" : "install")
            enabled: !card.busy && (!card.installed || card.addon.configurable)
            focusRadius: 8
            onTriggered: card.activated()
        }
    }

    KeyboardAction {
        id: action
        anchors.fill: parent
        z: 0
        accessibleName: (card.addon.title || card.addon.name || "")
                        + (card.installed ? ", installed" : card.addon.setupRequired ? ", set up" : ", install")
        focusEnabled: false                    // the row owns keyboard focus
        showFocusFrame: false
        onTriggered: card.activated()
    }
}
