import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import ".."

Rectangle {
    id: root
    objectName: "addToCollectionsButton"
    property var reference: ({})
    readonly property var resolution: typeof CollectionsIdentity !== "undefined"
        ? CollectionsIdentity.collectionReference(root.reference)
        : ({ok:false, error:qsTr("Collections are not available in this window.")})
    readonly property bool available: !!resolution.ok
    readonly property int membershipCount: {
        if (typeof NamedCollections === "undefined" || !NamedCollections || !root.available) return 0
        NamedCollections.revision
        return NamedCollections.memberships(root.resolution.reference.key).length
    }
    implicitWidth: label.implicitWidth + 62
    implicitHeight: 42
    radius: 11
    color: action.interactionActive ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: theme.edge
    Theme { id: theme }

    CollectionIcon {
        x: 17
        anchors.verticalCenter: parent.verticalCenter
        width: 18; height: 18
        name: root.membershipCount > 0 ? "check" : "collections"
        strokeColor: root.membershipCount > 0 ? theme.gold : root.available ? theme.ink : theme.inkDimmer
    }
    Text {
        id: label
        x: 43
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Add to Collections")
        color: root.available ? theme.ink : theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 15
        font.weight: Font.DemiBold
        textFormat: Text.PlainText
    }
    HoverHandler { id: hintHover; cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor }
    ToolTip.visible: hintHover.hovered
    ToolTip.delay: 450
    ToolTip.text: !root.available ? root.resolution.error
        : root.membershipCount > 0 ? qsTr("In %1 collections").arg(root.membershipCount) : qsTr("Keep this title in your collections")
    KeyboardAction {
        id: action
        objectName: root.objectName + "Action"
        anchors.fill: parent
        enabled: root.available
        focusEnabled: root.visible && root.enabled && root.available
        accessibleName: qsTr("Add to Collections")
        accessibleDescription: !root.available ? root.resolution.error
            : root.membershipCount ? qsTr("In %1 collections").arg(root.membershipCount) : ""
        focusRadius: root.radius
        onTriggered: {
            const window = root.Window.window
            if (window && typeof window.openNamedCollectionsPicker === "function")
                window.openNamedCollectionsPicker(root.resolution.reference, action)
        }
    }
}
