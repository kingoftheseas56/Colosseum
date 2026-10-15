import QtQuick

Item {
    id: root

    required property Flickable flick
    required property Repeater repeater
    property int count: 0
    property int currentIndex: count > 0 ? 0 : -1
    property string accessibleName: "Media rail"
    property int itemGap: 0
    signal activated(int index)

    x: root.flick.contentX
    y: root.flick.contentY
    width: root.flick.width
    height: root.flick.height
    focusPolicy: root.visible && root.enabled && root.count > 0 ? Qt.TabFocus : Qt.NoFocus

    function reveal(index) {
        const item = root.repeater.itemAt(index)
        if (!item)
            return
        const left = item.x
        const right = item.x + item.width
        const maxX = Math.max(0, root.flick.contentWidth - root.flick.width)
        if (left < root.flick.contentX)
            root.flick.contentX = Math.max(0, left)
        else if (right > root.flick.contentX + root.flick.width)
            root.flick.contentX = Math.min(maxX, right - root.flick.width)
    }

    Keys.onPressed: (event) => nav.handle(event)

    KeyboardCollectionController {
        id: nav
        view: root
        count: root.count
        currentIndex: root.currentIndex
        orientation: "horizontal"
        pageStep: {
            const first = root.repeater.itemAt(0)
            if (!first) return 1
            return Math.max(1, Math.floor(root.flick.width / Math.max(1, first.width + root.itemGap)))
        }
        positionIndexFn: root.reveal
        onActivated: (index) => root.activated(index)
    }

    Accessible.role: Accessible.List
    Accessible.name: root.accessibleName
    Accessible.focusable: focusPolicy !== Qt.NoFocus
}
