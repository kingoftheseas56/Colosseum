pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: root
    property string mediumTitle: ""
    property string note: ""
    property string kind: "video"
    property var entries: []
    signal activated(var entry, string kind)

    activeFocusOnTab: entries && entries.length > 0
    width: parent ? parent.width : 0
    height: header.height + 14 + rail.height

    readonly property int cardHeight: kind === "book" ? 286 : 274
    readonly property string countText: {
        var n = entries ? entries.length : 0
        var base = n + (n === 1 ? " work" : " works")
        return note.length ? base + "  ·  " + note : base
    }

    WidgetHeader {
        id: header
        width: parent.width
        title: root.mediumTitle
        sub: root.countText
        navigable: false
    }

    ListView {
        id: rail
        anchors.top: header.bottom
        anchors.topMargin: 14
        x: 6
        width: parent.width - 6
        height: root.cardHeight
        orientation: ListView.Horizontal
        spacing: 20
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.entries || []
        currentIndex: -1
        onCountChanged: {
            if (count <= 0) currentIndex = -1
            else if (currentIndex < 0) currentIndex = 0
            else if (currentIndex >= count) currentIndex = count - 1
        }

        delegate: UniverseGalleryCard {
            required property var modelData
            required property int index
            entry: modelData
            kind: root.kind
            keyboardFocused: root.activeFocus && ListView.isCurrentItem
            onActivated: function(e) {
                rail.currentIndex = index
                root.activated(e, root.kind)
            }
        }
    }

    Keys.onLeftPressed: function(event) {
        if (rail.count > 0) rail.currentIndex = Math.max(0, rail.currentIndex - 1)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        if (rail.count > 0) rail.currentIndex = Math.min(rail.count - 1, rail.currentIndex + 1)
        event.accepted = true
    }
    Keys.onReturnPressed: function(event) {
        if (rail.currentIndex >= 0 && rail.currentIndex < rail.count)
            root.activated(root.entries[rail.currentIndex], root.kind)
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (rail.currentIndex >= 0 && rail.currentIndex < rail.count)
            root.activated(root.entries[rail.currentIndex], root.kind)
        event.accepted = true
    }
}
