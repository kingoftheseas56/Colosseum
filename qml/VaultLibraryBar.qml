// Existing Vault world-bar geometry and controls, with content-aware frosting.
import QtQuick
Item {
    id: root
    property Item backdrop: null
    property Item contentBackdrop: null
    property real scrollOffset: 0
    property alias query: searchInput.text
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal accepted()
    function focusSearch() { searchInput.forceActiveFocus(Qt.ShortcutFocusReason) }
    Theme { id: theme }
    height: width < 600 ? 104 : 56
    readonly property bool stackedSearch: width < 600
    readonly property real sideWidth: Math.max(headerLeading.width, headerWindowControls.width) + 32
    Glass {
        objectName: "vaultHeaderGlass"; anchors.fill: parent; backdrop: root.backdrop
        contentBackdrop: root.scrollOffset > 1 ? root.contentBackdrop : null
        contentTrack: root.scrollOffset; radius: root.stackedSearch ? 22 : height / 2; scrim: 0.28
    }
    MouseArea { anchors.fill: parent }
    Item {
        anchors.left: parent.left; anchors.right: parent.right; height: 56
        Row {
            id: headerLeading; anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; height: 44; spacing: 12
            BackAction { objectName: "vaultHeaderBack"; anchors.verticalCenter: parent.verticalCenter; variant: "capsule"; tip: "Back"; onTriggered: root.backRequested() }
            Text { objectName: "vaultHeaderTitle"; anchors.verticalCenter: parent.verticalCenter; height: parent.height; verticalAlignment: Text.AlignVCenter; text: "Vault"; color: theme.ink; font.family: theme.display; font.pixelSize: 22; font.weight: Font.DemiBold; font.preferTypoLineMetrics: true }
        }
        Row {
            id: headerWindowControls; objectName: "vaultHeaderWindowControls"; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; height: 44
            VaultAction {
                id: minimizeButton; objectName: "vaultHeaderMinimize"; width: 44; height: 44; quiet: true; accessibleName: "Minimize"; tooltipText: accessibleName
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/minimize.svg"; opacity: minimizeButton.keyboardAction.interactionActive ? 1 : 0.8 }
                onTriggered: root.minimizeRequested()
            }
            VaultAction {
                id: fullButton; objectName: "vaultHeaderFullscreen"; width: 44; height: 44; quiet: true; accessibleName: "Toggle fullscreen"; tooltipText: accessibleName
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/fullscreen.svg"; opacity: fullButton.keyboardAction.interactionActive ? 1 : 0.8 }
                onTriggered: root.fullscreenRequested()
            }
            VaultAction {
                id: closeButton; objectName: "vaultHeaderClose"; width: 44; height: 44; quiet: true; accessibleName: "Close Colosseum"; tooltipText: accessibleName
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/power.svg"; opacity: closeButton.keyboardAction.interactionActive ? 1 : 0.8 }
                onTriggered: root.closeRequested()
            }
        }
    }
    Rectangle {
        objectName: "vaultBrowseSearchField"; anchors.horizontalCenter: parent.horizontalCenter
        y: root.stackedSearch ? 56 : (root.height - height) / 2
        width: root.stackedSearch ? parent.width - 32 : Math.min(400, parent.width - 2 * root.sideWidth)
        height: 36; radius: height / 2; color: theme.glassTint; border.width: 1; border.color: searchInput.activeFocus ? theme.gold : theme.edge
        Image { anchors.left: parent.left; anchors.leftMargin: 13; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; source: "../assets/icons/search.svg"; opacity: 0.7 }
        TextInput {
            id: searchInput; objectName: "vaultBrowseSearchInput"; anchors.fill: parent; anchors.leftMargin: 38; anchors.rightMargin: 36
            color: theme.ink; font.family: theme.ui; font.pixelSize: theme.vaultCaption; verticalAlignment: TextInput.AlignVCenter; clip: true
            selectionColor: theme.gold; selectedTextColor: theme.vaultGoldInk; Accessible.name: "Search titles, people, filenames"
            onAccepted: root.accepted()
        }
        Text { anchors.fill: parent; anchors.leftMargin: 38; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter; visible: searchInput.length === 0; text: "Search titles, people, filenames"; elide: Text.ElideRight; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption }
        VaultAction { objectName: "vaultHeaderClearSearch"; anchors.right: parent.right; width: 34; height: parent.height; text: "×"; accessibleName: "Clear search"; tooltipText: accessibleName; quiet: true; visible: searchInput.length > 0; onTriggered: { searchInput.text = ""; root.focusSearch() } }
    }
}
