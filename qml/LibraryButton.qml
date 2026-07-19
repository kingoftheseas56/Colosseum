import QtQuick

// + Library — the Collection arc's one button. Ghost recipe (translucent white,
// theme.edge border); flips to a gold check when saved. `entry` is the world's
// reopen snapshot: { id, type, title, cover, payload } — type ALWAYS rides
// (universe-tile law). Naming Collection.revision keeps `saved` live.
Rectangle {
    id: lib
    property string world: ""
    property var entry: null
    readonly property bool saved: (Collection.revision,
        entry && entry.id ? Collection.has(world, String(entry.id)) : false)

    width: libRow.implicitWidth + 36
    height: 42
    radius: 11
    color: libMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: theme.edge

    // Theme is not a singleton in this codebase — every reusable component instantiates
    // its own (a parent's `theme` id doesn't cross into a separately-loaded component file).
    Theme { id: theme }

    Row {
        id: libRow
        anchors.centerIn: parent
        spacing: 8
        Text {
            text: lib.saved ? "✓" : "+"
            color: lib.saved ? theme.gold : theme.ink
            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: lib.saved ? "In Library" : "Library"
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: libMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (!lib.entry || !lib.entry.id) return
            if (lib.saved) Collection.remove(lib.world, String(lib.entry.id))
            else Collection.add(lib.world, lib.entry)
        }
    }
}
