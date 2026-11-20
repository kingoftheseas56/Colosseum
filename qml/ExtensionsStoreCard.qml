// One add-on in the Store shelves and results grid. The card opens the add-on.
import QtQuick
import QtQuick.Effects
import "ExtensionsStoreApi.js" as StoreApi

Item {
    id: card
    objectName: card.addon && card.addon.slug ? "extensionsCard_" + card.addon.slug : ""

    property var addon: ({})
    property bool installed: false
    property bool disabled: false
    property bool busy: false
    property bool selected: false            // keyboard focus from the row
    property bool phoneLayout: false
    property bool artworkActive: true
    property bool essentialsLayout: false
    property bool builtIn: false
    signal activated()
    signal configureRequested()
    signal removeRequested()
    signal browseRequested()
    signal actionFocused()

    function focusActions() {
        if (enableAction.visible && enableAction.enabled) enableAction.forceActiveFocus(Qt.TabFocusReason)
        else if (installAction.enabled) installAction.forceActiveFocus(Qt.TabFocusReason)
        else if (configuration.visible) configuration.forceActiveFocus(Qt.TabFocusReason)
    }

    readonly property bool lit: card.selected || action.hovered
                                || (enableAction.visible && (enableAction.hovered || enableAction.activeFocus))
                                || (installAction.enabled && (installAction.hovered || installAction.activeFocus)) || configuration.interactionActive
    readonly property bool browsable: !!card.addon.world
    readonly property int padding: 16
    readonly property real h: StoreApi.hue(card.addon.name || "")

    width: theme.extensionCardWidth
    height: essentialsLayout ? 174
            : phoneLayout ? mobileInstall.y + mobileInstall.height + 12 : theme.extensionCardHeight

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        visible: card.essentialsLayout
        radius: 16
        border.width: 1
        border.color: card.lit ? theme.gold : card.installed && !card.disabled
                      ? Qt.rgba(0.94, 0.77, 0.29, 0.26) : theme.edge
        gradient: Gradient {
            GradientStop { position: 0; color: card.lit ? theme.glassHi : theme.glassTint }
            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.035) }
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: card.padding
            spacing: 12

            Loader {
                width: 46
                height: 46
                active: card.essentialsLayout && card.artworkActive
                sourceComponent: AddonLogo {
                    size: 46
                    showPlate: false
                    addonId: String(card.addon.id || "")
                    addonName: String(card.addon.title || card.addon.name || "")
                    manifestLogo: String(card.addon.logo || "")
                    preferManifestLogo: true
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - 58)
                text: title.text
                textFormat: Text.PlainText
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: theme.fontSize(16)
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: card.padding
            anchors.rightMargin: card.padding + (essentialsActions.configurationAbove ? configuration.width : 0)
            anchors.topMargin: 72
            text: card.addon.description || ""
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(13)
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    Rectangle {
        id: art
        visible: !card.essentialsLayout
        width: parent.width
        height: card.essentialsLayout ? width : card.phoneLayout ? 140 : theme.extensionCardArtHeight
        radius: card.phoneLayout ? 23 : 16
        clip: true
        border.width: 1
        border.color: card.lit ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42) : Qt.rgba(1, 1, 1, 0.10)
        Behavior on border.color { ColorAnimation { duration: 180 } }
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.hsla(card.h, 0.34, 0.16, 1) }
            GradientStop { position: 1; color: Qt.hsla((card.h + 30 / 360) % 1, 0.32, 0.10, 1) }
        }

        // The blurred logo gives the card a color wash behind its smaller mark.
        Image {
            id: washSource
            anchors.centerIn: parent
            width: art.width * 1.7 * 0.56 * 1.35
            height: width
            source: !card.essentialsLayout && card.artworkActive ? (card.addon.logo || "") : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: false
            sourceSize.width: 160; sourceSize.height: 160
        }
        Loader {
            anchors.fill: washSource
            active: !card.essentialsLayout && card.artworkActive
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
            anchors.verticalCenterOffset: card.lit ? (card.phoneLayout ? -4.5 : -2) : 0
            width: card.essentialsLayout ? art.width * 0.75 : card.phoneLayout ? 82 : 52
            height: width
            radius: card.phoneLayout ? 26 : 14
            scale: card.lit ? 1.045 : 1
            color: card.essentialsLayout ? "transparent" : Qt.rgba(24 / 255, 25 / 255, 31 / 255, 0.92)
            border.width: card.essentialsLayout ? 0 : 1
            border.color: Qt.rgba(1, 1, 1, 0.09)
            clip: true
            Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on anchors.verticalCenterOffset { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            Image {
                id: logo
                anchors.centerIn: parent
                width: card.essentialsLayout ? parent.width : parent.width * 0.78
                height: width
                source: !card.essentialsLayout && card.artworkActive ? (card.addon.logo || "") : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize.width: 192; sourceSize.height: 192
            }
            Text {                              // no logo, or it failed: the initial
                anchors.centerIn: parent
                visible: logo.status !== Image.Ready
                text: String(card.addon.title || card.addon.name || "?").charAt(0).toUpperCase()
                color: theme.inkDim
                font.family: theme.display
                font.pixelSize: card.essentialsLayout ? 54 : card.phoneLayout ? 42 : 27
            }
        }

        // Quiet state badge — only when there is something to say.
        Rectangle {
            visible: !card.essentialsLayout && !card.phoneLayout && (card.installed || card.busy)
            anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 7
            width: badgeText.implicitWidth + 12; height: 18
            radius: 9
            color: Qt.rgba(10 / 255, 11 / 255, 15 / 255, 0.62)
            border.width: 1
            border.color: card.installed && !card.disabled
                          ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.45) : Qt.rgba(1, 1, 1, 0.16)
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: card.busy ? qsTr("Adding…") : card.disabled ? qsTr("Disabled") : qsTr("Installed")
                color: card.installed && !card.disabled ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold
            }
        }
    }

    Text {
        id: title
        visible: !card.essentialsLayout
        textFormat: Text.PlainText
        anchors.top: art.bottom; anchors.topMargin: card.phoneLayout ? 12 : 8
        width: parent.width
        text: card.addon.title || card.addon.name || ""
        color: theme.ink
        font.family: card.essentialsLayout ? theme.display : theme.ui
        font.pixelSize: card.essentialsLayout ? theme.fontSize(17) : card.phoneLayout ? 18 : 16
        font.weight: Font.DemiBold
        wrapMode: card.phoneLayout ? Text.WordWrap : Text.Wrap
        maximumLineCount: card.phoneLayout ? 100 : 2
        elide: card.phoneLayout ? Text.ElideNone : Text.ElideRight
    }
    Text {
        id: description
        visible: !card.essentialsLayout
        anchors.top: title.bottom; anchors.topMargin: 3
        width: parent.width
        height: card.phoneLayout ? implicitHeight : 40
        text: card.addon.description || ""
        color: card.phoneLayout ? "#aaa69e" : theme.inkDim
        font.family: theme.ui; font.pixelSize: card.phoneLayout ? 14 : 13
        wrapMode: Text.WordWrap
        maximumLineCount: card.phoneLayout ? 100 : 2
        elide: card.phoneLayout ? Text.ElideNone : Text.ElideRight
    }

    Item {
        id: essentialsActions
        visible: card.essentialsLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: card.padding
        height: 44
        readonly property bool configurationAbove: configuration.visible && enableAction.visible
            && width < enableAction.implicitWidth + installAction.implicitWidth + configuration.width + 4
        readonly property real buttonWidth: width - (configuration.visible && !configurationAbove ? configuration.width : 0)

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, (configuration.visible ? configuration.x
                       : installAction.visible ? installAction.x : parent.width) - 4)
            elide: Text.ElideRight
            visible: card.builtIn || card.busy
            text: card.builtIn ? qsTr("Built in") : qsTr("Installing…")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(12)
        }
        ExtensionsActionButton {
            id: enableAction
            objectName: "essentialsEnable_" + (card.addon.slug || "")
            visible: card.essentialsLayout && card.installed && card.disabled && !card.builtIn && !card.browsable
            enabled: visible && !card.busy
            anchors.right: configuration.visible && !essentialsActions.configurationAbove ? configuration.left : installAction.left
            anchors.rightMargin: 4
            height: parent.height
            width: Math.min(implicitWidth, Math.max(0, (essentialsActions.buttonWidth - 4) / 2,
                                                  essentialsActions.buttonWidth - installAction.implicitWidth - 4))
            padding: 4
            text: qsTr("Enable")
            readonly property string accessibleName: qsTr("Enable %1").arg(title.text)
            Accessible.name: accessibleName
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            onActiveFocusChanged: if (activeFocus) card.actionFocused()
            onClicked: {
                installAction.forceActiveFocus()
                card.activated()
            }
        }
        ExtensionsActionButton {
            id: installAction
            objectName: "essentialsAction_" + (card.addon.slug || "")
            visible: card.essentialsLayout && (!card.builtIn || card.browsable)
            enabled: visible && !card.busy
            anchors.right: parent.right
            height: parent.height
            width: enableAction.visible
                ? Math.max(0, Math.min(implicitWidth, essentialsActions.buttonWidth - enableAction.width - 4))
                : implicitWidth
            padding: enableAction.visible ? 4 : 12
            text: card.browsable ? qsTr("Browse") : card.installed ? qsTr("Uninstall") : qsTr("Install")
            readonly property string accessibleName: qsTr("%1 %2").arg(text).arg(title.text)
            Accessible.name: accessibleName
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            onActiveFocusChanged: if (activeFocus) card.actionFocused()
            onClicked: {
                if (card.browsable) card.browseRequested()
                else if (card.installed) card.removeRequested()
                else card.activated()
            }
        }
        KeyboardAction {
            id: configuration
            objectName: "essentialsConfigure_" + (card.addon.slug || "")
            visible: card.essentialsLayout && !!card.addon.configurable && !card.builtIn
            enabled: !card.busy
            width: 44
            height: parent.height
            anchors.right: essentialsActions.configurationAbove ? parent.right : installAction.left
            y: essentialsActions.configurationAbove ? card.padding + 50 - essentialsActions.y : 0
            accessibleName: qsTr("Configure %1").arg(title.text)
            onActiveFocusChanged: if (activeFocus) card.actionFocused()
            onTriggered: card.configureRequested()
            Image {
                anchors.centerIn: parent
                width: 19
                height: 19
                source: configuration.visible ? "../assets/icons/extension-configure.svg" : ""
                sourceSize.width: 38
                sourceSize.height: 38
                opacity: configuration.interactionActive ? 1 : 0.74
            }
        }
    }

    Rectangle {
        id: mobileInstall
        visible: card.phoneLayout && !card.essentialsLayout
        z: 2
        anchors.top: description.bottom
        anchors.topMargin: 12
        anchors.right: parent.right
        width: 130; height: 38; radius: 8
        color: card.installed ? "#45403a" : "#e9c46a"
        Text {
            anchors.centerIn: parent
            text: card.busy ? "Adding…" : card.installed
                  ? (card.addon.configurable ? "Set up" : card.disabled ? "Enable" : "Installed")
                  : (card.addon.configurable ? "Set up" : "Install")
            color: card.installed ? "#ffffff" : "#171717"
            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
        }
        KeyboardAction {
            anchors.fill: parent
            accessibleName: (card.addon.title || card.addon.name || "") + ", "
                            + (card.disabled ? "disabled" : card.installed ? "installed" : "install")
            enabled: !card.busy && (!card.installed || card.disabled || card.addon.configurable)
            focusRadius: 8
            onTriggered: card.activated()
        }
    }

    KeyboardAction {
        id: action
        visible: !card.essentialsLayout
        anchors.fill: parent
        z: 0
        accessibleName: (card.addon.title || card.addon.name || "")
                        + (card.disabled ? ", disabled" : card.installed ? ", installed"
                           : card.addon.setupRequired ? ", set up" : ", install")
        focusEnabled: false                    // the row owns keyboard focus
        showFocusFrame: false
        onTriggered: card.activated()
    }
}
