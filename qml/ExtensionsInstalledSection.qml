pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Column {
    id: section
    required property var entries
    required property var capabilitiesFor
    required property var instanceLabelFor
    signal manageRequested(int index)
    width: parent ? parent.width : 800
    spacing: 14
    Theme { id: theme }
    Row {
        width: parent.width
        spacing: 12
        Column {
            width: parent.width - seeAll.width - 12
            spacing: 5
            Text {
                text: qsTr("Your Add-ons")
                color: theme.ink
                font.family: theme.display; font.pixelSize: 26; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: qsTr("%1 installed · Manage what powers your worlds.").arg(section.entries.length)
                wrapMode: Text.WordWrap
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 14
            }
        }
        ExtensionsActionButton {
            id: seeAll
            objectName: "extensionsInstalledSeeAll"
            text: qsTr("See all ›")
            onClicked: section.manageRequested(0)
        }
    }
    Text {
        visible: section.entries.length === 0
        width: parent.width
        text: qsTr("No add-ons installed. Explore Essentials or install from a URL.")
        wrapMode: Text.WordWrap
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 14
    }
    ListView {
        id: rail
        objectName: "extensionsInstalledRail"
        width: parent.width
        height: section.entries.length ? 138 : 0
        orientation: ListView.Horizontal
        spacing: 14
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: section.entries
        ScrollBar.horizontal: ScrollBar { }
        delegate: ExtensionsInstalledCard {
            required property var modelData
            required property int index
            objectName: "extensionsInstalledCard_" + index
            width: Math.min(320, rail.width)
            entry: modelData
            capabilities: section.capabilitiesFor(modelData.manifest)
            instanceLabel: section.instanceLabelFor(index)
            onManageRequested: section.manageRequested(index)
            onKeyboardFocusReceived: rail.positionViewAtIndex(index, ListView.Contain)
        }
    }
}
