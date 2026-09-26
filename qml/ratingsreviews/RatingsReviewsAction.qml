import QtQuick
import ".."

// Compact house-styled entry row for title detail: star glyph + label + chevron
// in a glass pill, matching Watch/Library siblings (Arc 49 approved mock).
Item {
    id: root
    width: 176
    height: 40
    enabled: routeAvailable
    activeFocusOnTab: routeAvailable

    property var titleRegistry: null
    property string world: ""
    property string kind: ""
    property string directId: ""
    property var aliases: []
    property string titleText: ""
    property string subtitleText: ""
    property int year: 0
    property string artwork: ""
    property string origin: ""
    property var readIds: ({})
    property var returnTarget: null

    readonly property var resolved: titleRegistry
        ? titleRegistry.resolve(world, kind, directId, aliases)
        : ({ available: false, errorCode: "title_unavailable" })
    readonly property bool routeAvailable: !!resolved.available
    readonly property string hintText: routeAvailable
        ? "" : "Not available for this title."

    signal ratingsReviewsRequested(var context, var invokingItem, var fallbackItem)

    // Root-level key path: callers and tests may focus the action item itself.
    Keys.onPressed: function(event) {
        if (!routeAvailable)
            return
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                || event.key === Qt.Key_Space) {
            root.activate()
            event.accepted = true
        }
    }

    function activate() {
        if (!routeAvailable)
            return false
        var key = ({
            world: String(resolved.world || world),
            kind: String(resolved.kind || kind)
        })
        key.mediaId = String(resolved.mediaId || "")
        var ctx = ({
            title: ({
                title: root.titleText,
                subtitle: root.subtitleText,
                year: root.year,
                artwork: root.artwork
            }),
            origin: root.origin
        })
        ctx.identity = key
        ctx.readIds = root.readIds || ({})
        root.ratingsReviewsRequested(ctx, root, root.returnTarget)
        return true
    }

    Theme { id: theme }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: 11
        color: input.interactionActive
               ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
        border.width: 1
        border.color: theme.edge
        opacity: root.routeAvailable ? 1.0 : 0.45
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Row {
        anchors.centerIn: parent
        spacing: 9

        Text {
            text: root.routeAvailable ? "★" : "☆"
            color: root.routeAvailable ? theme.gold : theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 16
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            objectName: root.objectName.length > 0 ? root.objectName + "Label" : ""
            text: "Ratings & Reviews"
            color: root.routeAvailable ? theme.ink : theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 14
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.routeAvailable
            text: "›"
            color: input.interactionActive ? theme.gold : theme.inkDimmer
            font.family: theme.display
            font.pixelSize: 20
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    // Unavailable form: greyed pill, outline star, no chevron. The reason stays in
    // the accessible description; the pill never fakes an openable route.
    KeyboardAction {
        id: input
        anchors.fill: parent
        pointerEnabled: root.routeAvailable
        accessibleName: "Ratings and Reviews"
        accessibleDescription: root.routeAvailable
            ? "Open ratings and reviews" : root.hintText
        focusRadius: pill.radius
        onTriggered: root.activate()
    }
}
