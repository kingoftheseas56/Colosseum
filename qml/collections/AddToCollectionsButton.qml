import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import ".."

Rectangle {
    id: root
    AdaptiveLayout {
        id: touchMetrics
        androidHost: Qt.platform.os === "android" || !!(root.Window.window && root.Window.window.androidHost === true)
    }
    objectName: "addToCollectionsButton"
    property var reference: ({})
    property string labelText: qsTr("Add to Collections")
    property bool iconOnly: false
    property bool hoverDescriptionEnabled: true
    readonly property var resolution: typeof CollectionsIdentity !== "undefined"
        ? CollectionsIdentity.collectionReference(root.reference)
        : ({ok:false, error:qsTr("Collections are not available in this window.")})
    readonly property bool available: !!resolution.ok
    readonly property int membershipCount: {
        if (typeof NamedCollections === "undefined" || !NamedCollections || !root.available) return 0
        NamedCollections.revision
        return NamedCollections.memberships(root.resolution.reference.key).length
    }
    readonly property string tooltipText: !root.available ? root.resolution.error
        : root.membershipCount > 0 ? qsTr("In %1 collections").arg(root.membershipCount) : qsTr("Keep this title in your collections")
    implicitWidth: root.iconOnly ? 34 : label.implicitWidth + 62
    implicitHeight: root.iconOnly ? 34 : 42
    radius: 11
    color: action.interactionActive ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: theme.edge
    Theme { id: theme }

    CollectionIcon {
        x: root.iconOnly ? (root.width - width) / 2 : 17
        anchors.verticalCenter: parent.verticalCenter
        width: 18; height: 18
        name: root.membershipCount > 0 ? "check" : "collections"
        strokeColor: root.membershipCount > 0 ? theme.gold : root.available ? theme.ink : theme.inkDimmer
    }
    Text {
        id: label
        objectName: root.objectName + "Caption"
        x: 43
        anchors.verticalCenter: parent.verticalCenter
        visible: !root.iconOnly
        text: root.labelText
        color: root.available ? theme.ink : theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 15
        font.weight: Font.DemiBold
        textFormat: Text.PlainText
    }
    HoverHandler { id: hintHover; cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TouchTooltip {
        id: unavailableTooltip
        owner: root
        tapOwner: root.hoverDescriptionEnabled && !root.available
    }
    ToolTip.visible: root.hoverDescriptionEnabled && (action.longPressTooltipEnabled
        ? (root.available ? action._longPressTooltip : unavailableTooltip.active)
        : hintHover.hovered)
    ToolTip.delay: action.longPressTooltipEnabled ? 0 : 350
    ToolTip.text: root.tooltipText
    KeyboardAction {
        id: action
        objectName: root.objectName + "Action"
        anchors.fill: parent
        anchors.leftMargin: -touchMetrics.targetInset(parent.width)
        anchors.rightMargin: -touchMetrics.targetInset(parent.width)
        anchors.topMargin: -touchMetrics.targetInset(parent.height)
        anchors.bottomMargin: -touchMetrics.targetInset(parent.height)
        enabled: root.available
        focusEnabled: root.visible && root.enabled && root.available
        accessibleName: root.labelText
        accessibleDescription: !root.available ? root.resolution.error
            : root.membershipCount ? qsTr("In %1 collections").arg(root.membershipCount) : ""
        tooltipText: root.hoverDescriptionEnabled && root.available ? root.tooltipText : ""
        tooltipHovered: hintHover.hovered
        ToolTip.visible: false
        focusRadius: root.radius
        onTriggered: {
            const window = root.Window.window
            if (window && typeof window.openNamedCollectionsPicker === "function")
                window.openNamedCollectionsPicker(root.resolution.reference, action)
        }
    }
}
