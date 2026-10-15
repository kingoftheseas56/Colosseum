// ExtensionsStoreRail — one Store row: header, then a horizontal rail of ExtensionsStoreCard.
// Same shape and keyboard contract as ContinueRow (KeyboardCollectionController over a Flickable).
import QtQuick

Column {
    id: rail

    property string title: ""
    property string sub: ""
    property var items: []
    property string automationId: ""
    // Install state lives with the page; `revision` re-evaluates every card when it changes.
    property var installedFn: function(addon) { return false }
    property var busy: ({})
    property int revision: 0
    property bool canSeeAll: true
    signal addonActivated(var addon)
    signal seeAllRequested()

    function _findKeyboardSectionCoordinator() {
        for (var node = rail.parent; node; node = node.parent) {
            if (node.keyboardSectionCoordinator !== undefined && node.keyboardSectionCoordinator)
                return node.keyboardSectionCoordinator
        }
        return null
    }
    property var keyboardSectionCoordinator: _findKeyboardSectionCoordinator()

    Theme { id: theme }

    width: parent ? parent.width : 800
    spacing: 14
    visible: rail.items.length > 0

    // Header as in the mock: serif title, the line under it, "See all ›" on the right.
    Item {
        width: parent.width
        height: head.height
        Column {
            id: head
            spacing: 5
            Text {
                text: rail.title
                color: theme.ink
                font.family: theme.display; font.pixelSize: 26; font.weight: Font.DemiBold
            }
            Text {
                visible: rail.sub.length > 0
                text: rail.sub
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 14
            }
        }
        Text {
            id: seeAll
            visible: rail.canSeeAll
            anchors.right: parent.right; anchors.bottom: parent.bottom
            text: "See all ›"
            color: seeAllAction.interactionActive ? theme.ink : theme.inkDim
            font.family: theme.ui; font.pixelSize: 15
            KeyboardAction {
                id: seeAllAction
                objectName: rail.automationId.length > 0 ? rail.automationId + "SeeAll" : ""
                anchors.fill: parent
                anchors.margins: -8
                accessibleName: "See all " + rail.title
                focusRadius: 8
                onTriggered: rail.seeAllRequested()
            }
        }
    }

    Flickable {
        id: flick
        objectName: rail.automationId
        property bool keyboardReturnOwner: true
        property var keyboardSectionCoordinator: rail.keyboardSectionCoordinator
        property var keyboardItemAtIndex: function(index) { return cards.itemAt(index) }
        property var keyboardItems: rail.items
        property var keyboardIdentityForIndex: function(index) {
            var a = rail.items[index]
            return a ? String(a.slug) : ""
        }
        property var keyboardIndexForIdentity: function(identity) {
            for (var i = 0; i < rail.items.length; ++i)
                if (String(rail.items[i].slug) === String(identity || "")) return i
            return -1
        }
        property var keyboardRevealIndex: function(index) {
            var card = cards.itemAt(index)
            currentIndex = index
            if (!card) return false
            if (card.x < flick.contentX)
                flick.contentX = card.x
            else if (card.x + card.width > flick.contentX + flick.width)
                flick.contentX = Math.min(Math.max(0, flick.contentWidth - flick.width), card.x + card.width - flick.width)
            return true
        }
        property int currentIndex: rail.items.length > 0 ? 0 : -1
        width: parent.width
        height: row.height
        contentWidth: row.width; contentHeight: height
        clip: true
        focusPolicy: rail.items.length > 0 ? Qt.TabFocus : Qt.NoFocus
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        Keys.onPressed: (event) => keys.handle(event)

        Row {
            id: row
            spacing: 22
            Repeater {
                id: cards
                model: rail.items
                delegate: ExtensionsStoreCard {
                    required property var modelData
                    required property int index
                    width: rail.width < 1100 ? 320 : 365
                    addon: modelData
                    installed: { rail.revision; return rail.installedFn(modelData) }
                    busy: rail.busy[modelData.slug] === true
                    selected: flick.activeFocus && flick.currentIndex === index
                    onActivated: rail.addonActivated(modelData)
                }
            }
        }

        KeyboardCollectionController {
            id: keys
            view: flick
            orientation: "horizontal"
            count: rail.items.length
            identityForIndex: flick.keyboardIdentityForIndex
            indexForIdentity: flick.keyboardIndexForIdentity
            modelRevision: rail.items.length
            keyboardSectionCoordinator: rail.keyboardSectionCoordinator
            positionIndexFn: function(index) { flick.keyboardRevealIndex(index) }
            onActivated: (index) => rail.addonActivated(rail.items[index])
        }
    }
}
