// ExtensionsStoreCard keeps the wide artwork tile and uses a compact, actionable row on phones.
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
    property bool phoneDescriptionExpanded: false
    // Keep the card/navigation alive while distant artwork releases its images
    // and effect textures on memory-constrained hosts.
    property bool artworkActive: true
    signal activated()

    readonly property bool lit: card.selected || action.hovered
    readonly property real h: StoreApi.hue(card.addon.name || "")

    width: 365
    height: phoneLayout ? phoneRow.height : art.height + 12 + 24 + 3 + 40
    onAddonChanged: card.phoneDescriptionExpanded = false

    Theme { id: theme }

    Rectangle {
        id: art
        objectName: "extensionsStoreArt"
        visible: !card.phoneLayout
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
            source: card.artworkActive && !card.phoneLayout ? (card.addon.logo || "") : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: false
            sourceSize.width: 160; sourceSize.height: 160
        }
        Loader {
            anchors.fill: washSource
            active: card.artworkActive && !card.phoneLayout
            sourceComponent: Component {
                MultiEffect {
                    anchors.fill: parent
                    source: washSource
                    visible: washSource.status === Image.Ready
                    blurEnabled: true; blur: 1.0; blurMax: 64; blurMultiplier: 1.6
                    saturation: 0.85
                    brightness: -0.28
                    opacity: 0.74
                }
            }
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
                source: card.artworkActive && !card.phoneLayout ? (card.addon.logo || "") : ""
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
                font.family: theme.ui; font.pixelSize: theme.fontSize(11); font.weight: Font.DemiBold
            }
        }
    }

    Text {
        id: title
        visible: !card.phoneLayout
        anchors.top: art.bottom; anchors.topMargin: 12
        width: parent.width
        text: card.addon.title || card.addon.name || ""
        color: theme.ink
        font.family: theme.ui; font.pixelSize: theme.fontSize(18); font.weight: Font.DemiBold
        wrapMode: card.phoneLayout ? Text.WordWrap : Text.NoWrap
        elide: card.phoneLayout ? Text.ElideNone : Text.ElideRight
    }
    Text {
        id: description
        visible: !card.phoneLayout
        anchors.top: title.bottom; anchors.topMargin: 3
        width: parent.width
        height: card.phoneLayout ? implicitHeight : 40
        text: card.addon.description || ""
        color: "#aaa69e"
        font.family: theme.ui; font.pixelSize: theme.fontSize(14)
        wrapMode: Text.WordWrap
        maximumLineCount: card.phoneLayout ? 100 : 2
        elide: card.phoneLayout ? Text.ElideNone : Text.ElideRight
    }

    Item {
        id: phoneRow
        objectName: card.phoneLayout && card.addon && card.addon.slug
                    ? "extensionsPhoneRow_" + card.addon.slug : "extensionsPhoneRow"
        visible: card.phoneLayout
        width: parent.width
        height: phoneContent.implicitHeight + 20
        Rectangle {
            anchors.fill: parent
            radius: 14
            color: theme.glassTint
            border.width: card.selected ? 2 : 1
            border.color: card.selected ? theme.gold : theme.edge
        }
        Row {
            id: phoneContent
            anchors.fill: parent; anchors.margins: 10; spacing: 10
            Rectangle {
                id: phoneArtwork
                objectName: "extensionsPhoneArtwork"
                width: 56; height: 56; radius: 12; clip: true
                color: Qt.hsla(card.h, 0.30, 0.15, 1)
                Image {
                    id: phoneArtworkImage
                    anchors.fill: parent
                    anchors.margins: 5
                    source: card.artworkActive ? (card.addon.logo || "") : ""
                    sourceSize.width: 96; sourceSize.height: 96
                    fillMode: Image.PreserveAspectFit; asynchronous: true
                }
                Text {
                    objectName: "extensionsPhoneArtworkFallback"
                    anchors.centerIn: parent
                    visible: !card.artworkActive || !card.addon.logo
                             || phoneArtworkImage.status === Image.Error
                    text: String(card.addon.title || card.addon.name || "?").charAt(0).toUpperCase()
                    color: theme.inkDim; font.family: theme.display; font.pixelSize: 24
                }
            }
            Column {
                id: phoneDetails
                width: Math.max(0, parent.width - phoneArtwork.width - phoneActions.width - parent.spacing * 2)
                spacing: 4
                Text {
                    objectName: "extensionsPhoneTitle"
                    width: parent.width
                    text: card.addon.title || card.addon.name || ""
                    color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(16); font.weight: Font.DemiBold
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                }
                Row {
                    width: parent.width; spacing: 4
                    Text {
                        id: phoneDescription
                        objectName: "extensionsPhoneDescription"
                        width: Math.max(0, parent.width - 52)
                        text: card.addon.description || ""
                        color: "#aaa69e"; font.family: theme.ui; font.pixelSize: theme.fontSize(13)
                        wrapMode: Text.Wrap
                        maximumLineCount: card.phoneDescriptionExpanded ? 0 : 2
                        elide: card.phoneDescriptionExpanded ? Text.ElideNone : Text.ElideRight
                    }
                    Rectangle {
                        objectName: "extensionsPhoneMore"
                        visible: phoneDescription.truncated || card.phoneDescriptionExpanded
                        width: 48; height: 48; radius: 8
                        color: theme.glassTint; border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent
                            text: card.phoneDescriptionExpanded ? qsTr("Less") : qsTr("More")
                            color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                            font.weight: Font.DemiBold
                        }
                        KeyboardAction {
                            objectName: "extensionsPhoneMoreAction"
                            anchors.fill: parent
                            accessibleName: card.phoneDescriptionExpanded
                                            ? qsTr("Show less description") : qsTr("Show more description")
                            focusRadius: 8
                            onTriggered: card.phoneDescriptionExpanded = !card.phoneDescriptionExpanded
                        }
                    }
                }
            }
            Column {
                id: phoneActions
                width: 92; spacing: 4
                Text {
                    objectName: "extensionsPhoneState"
                    visible: card.installed && !card.busy
                    width: parent.width
                    text: card.installed
                          ? "✓ Installed" : ""
                    color: theme.gold; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                    font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight; maximumLineCount: 1
                }
                Rectangle {
                    objectName: "extensionsPhonePrimaryAction"
                    width: parent.width; height: 48; radius: 10
                    color: primaryAction.enabled
                           ? (card.installed ? "#45403a" : "#e9c46a")
                           : Qt.rgba(1, 1, 1, 0.10)
                    Text {
                        objectName: "extensionsPhoneActionLabel"
                        anchors.centerIn: parent
                        text: card.busy ? qsTr("Adding…") : card.installed
                              ? (card.addon.configurable ? qsTr("Set up") : qsTr("Installed"))
                              : (card.addon.configurable || card.addon.setupRequired
                                 ? qsTr("Set up") : qsTr("Install"))
                        color: card.installed ? "#ffffff" : "#171717"
                        font.family: theme.ui; font.pixelSize: theme.fontSize(14); font.weight: Font.DemiBold
                    }
                    KeyboardAction {
                        id: primaryAction
                        objectName: "extensionsPhonePrimaryActionKeyboard"
                        anchors.fill: parent
                        accessibleName: (card.addon.title || card.addon.name || "") + ", "
                                        + (card.busy ? qsTr("adding") : card.installed
                                           ? (card.addon.configurable ? qsTr("set up") : qsTr("installed"))
                                           : (card.addon.configurable || card.addon.setupRequired
                                              ? qsTr("set up") : qsTr("install")))
                        enabled: !card.busy && (!card.installed || !!card.addon.configurable)
                        focusRadius: 10
                        onTriggered: card.activated()
                    }
                }
            }
        }
    }

    KeyboardAction {
        id: action
        visible: !card.phoneLayout
        anchors.fill: parent
        z: 0
        accessibleName: (card.addon.title || card.addon.name || "")
                        + (card.installed ? ", installed" : card.addon.setupRequired ? ", set up" : ", install")
        focusEnabled: false                    // the row owns keyboard focus
        showFocusFrame: false
        onTriggered: card.activated()
    }
}
