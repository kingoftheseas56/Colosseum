// ExtensionsStoreRail — one Store row: header, then a horizontal rail of ExtensionsStoreCard.
// Same shape and keyboard contract as ContinueRow (KeyboardCollectionController over a Flickable).
import QtQuick
import QtQuick.Controls.Basic

Column {
    id: rail

    property string title: ""
    property string sub: ""
    property var items: []
    property string automationId: ""
    // Install state lives with the page; `revision` re-evaluates every card when it changes.
    property var installedFn: function(addon) { return false }
    property var disabledFn: function(addon) { return false }
    property var busy: ({})
    property int revision: 0
    property bool canSeeAll: true
    readonly property int phoneVisibleCount: 4
    property bool phoneLayout: false
    property bool limitArtworkToViewport: false
    property bool artworkActive: true
    readonly property real cardWidth: Math.min(theme.extensionCardWidth, width)
    readonly property real scrollLimit: Math.max(0, shelfFlick.contentWidth - shelfFlick.width)
    readonly property bool hasOverflow: !phoneLayout && scrollLimit > 0.5
    signal addonActivated(var addon)
    signal seeAllRequested()

    function scrollByPage(direction) {
        const step = rail.cardWidth + row.spacing
        const page = Math.max(1, Math.floor((shelfFlick.width + row.spacing) / step))
        shelfFlick.cancelFlick()
        shelfFlick.contentX = Math.max(0, Math.min(rail.scrollLimit, shelfFlick.contentX + direction * page * step))
        shelfFlick.currentIndex = Math.min(rail.items.length - 1, Math.ceil(shelfFlick.contentX / step))
    }

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

    Item {
        width: parent.width
        height: Math.max(head.height, navigation.height)
        Column {
            id: head
            width: Math.max(0, rail.width - navigation.width - (navigation.width > 0 ? 12 : 0))
            spacing: 5
            Text {
                width: head.width
                wrapMode: Text.Wrap
                text: rail.title
                color: theme.ink
                font.family: theme.display; font.pixelSize: 26; font.weight: Font.DemiBold
            }
            Text {
                width: head.width
                wrapMode: Text.Wrap
                visible: rail.sub.length > 0
                text: rail.sub
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 14
            }
        }
        Row {
            id: navigation
            anchors.right: parent.right; anchors.bottom: parent.bottom
            spacing: 6
            ShelfButton {
                objectName: rail.automationId.length > 0 ? rail.automationId + "Previous" : ""
                visible: rail.hasOverflow
                enabled: shelfFlick.contentX > 0.5
                width: 36; height: 36
                padding: 0
                text: "←"
                Accessible.name: qsTr("Previous add-ons in %1").arg(rail.title)
                ToolTip.visible: Qt.platform.os !== "android" && hovered
                ToolTip.delay: 350
                ToolTip.text: qsTr("Previous add-ons in %1").arg(rail.title)
                onClicked: rail.scrollByPage(-1)
            }
            ShelfButton {
                objectName: rail.automationId.length > 0 ? rail.automationId + "Next" : ""
                visible: rail.hasOverflow
                enabled: shelfFlick.contentX < rail.scrollLimit - 0.5
                width: 36; height: 36
                padding: 0
                text: "→"
                Accessible.name: qsTr("Next add-ons in %1").arg(rail.title)
                ToolTip.visible: Qt.platform.os !== "android" && hovered
                ToolTip.delay: 350
                ToolTip.text: qsTr("Next add-ons in %1").arg(rail.title)
                onClicked: rail.scrollByPage(1)
            }
            ShelfButton {
                objectName: rail.automationId.length > 0 ? rail.automationId + "SeeAll" : ""
                visible: rail.canSeeAll
                height: rail.phoneLayout ? implicitHeight : 36
                padding: 0
                text: qsTr("See all ›")
                Accessible.name: qsTr("See all %1").arg(rail.title)
                onClicked: rail.seeAllRequested()
            }
        }
    }

    component ShelfButton: ExtensionsActionButton {
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
    }

    Column {
        id: mobileCards
        visible: rail.phoneLayout
        width: parent.width
        spacing: 18
        Repeater {
            // A phone shows the first few of a rail and leaves the rest behind See all (a lazy
            // list). Building every card of every rail at once, logos included, got the app
            // killed for memory on a 4 GB tablet (2026-10-10, Android exit record LOW_MEMORY).
            model: !rail.phoneLayout ? []
                 : (rail.canSeeAll ? rail.items.slice(0, rail.phoneVisibleCount) : rail.items)
            delegate: ExtensionsStoreCard {
                required property var modelData
                width: mobileCards.width
                phoneLayout: true
                artworkActive: rail.artworkActive
                addon: modelData
                installed: { rail.revision; return rail.installedFn(modelData) }
                disabled: { rail.revision; return rail.disabledFn(modelData) }
                busy: rail.busy[modelData.slug] === true
                onActivated: rail.addonActivated(modelData)
            }
        }
    }

    Flickable {
        id: shelfFlick
        visible: !rail.phoneLayout
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
            if (card.x < shelfFlick.contentX)
                shelfFlick.contentX = card.x
            else if (card.x + card.width > shelfFlick.contentX + shelfFlick.width)
                shelfFlick.contentX = Math.min(Math.max(0, shelfFlick.contentWidth - shelfFlick.width), card.x + card.width - shelfFlick.width)
            return true
        }
        property int currentIndex: rail.items.length > 0 ? 0 : -1
        width: parent.width
        height: rail.phoneLayout ? 0 : row.height + (rail.hasOverflow ? 16 : 0)
        contentWidth: row.width; contentHeight: row.height
        clip: true
        focusPolicy: rail.phoneLayout ? Qt.NoFocus : (rail.items.length > 0 ? Qt.TabFocus : Qt.NoFocus)
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        Keys.onPressed: (event) => keys.handle(event)
        ScrollBar.horizontal: HouseScrollBar {
            objectName: rail.automationId.length ? rail.automationId + "ScrollBar" : ""
            flick: shelfFlick
            orientation: Qt.Horizontal
            glassStyle: true
        }

        Row {
            id: row
            spacing: 14
            Repeater {
                id: cards
                // the wide row is not built at all on a phone; hidden cards still load their logos
                model: rail.phoneLayout ? [] : rail.items
                delegate: ExtensionsStoreCard {
                    required property var modelData
                    required property int index
                    width: rail.cardWidth
                    artworkActive: rail.artworkActive
                        && (!rail.limitArtworkToViewport || shelfFlick.width <= 0
                            || (x + width >= shelfFlick.contentX - shelfFlick.width * 0.5
                                && x <= shelfFlick.contentX + shelfFlick.width * 1.5))
                    addon: modelData
                    installed: { rail.revision; return rail.installedFn(modelData) }
                    disabled: { rail.revision; return rail.disabledFn(modelData) }
                    busy: rail.busy[modelData.slug] === true
                    selected: shelfFlick.activeFocus && shelfFlick.currentIndex === index
                    onActivated: rail.addonActivated(modelData)
                }
            }
        }

        KeyboardCollectionController {
            id: keys
            view: shelfFlick
            orientation: "horizontal"
            count: rail.items.length
            identityForIndex: shelfFlick.keyboardIdentityForIndex
            indexForIdentity: shelfFlick.keyboardIndexForIdentity
            modelRevision: rail.items.length
            keyboardSectionCoordinator: rail.keyboardSectionCoordinator
            positionIndexFn: function(index) { shelfFlick.keyboardRevealIndex(index) }
            onActivated: (index) => rail.addonActivated(rail.items[index])
        }
    }
}
