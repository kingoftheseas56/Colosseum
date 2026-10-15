import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qml/feria" as Feria

ApplicationWindow {
    id: root
    visible: true
    width: 1100; height: 760
    title: "Feria 6–10 · component runtime exerciser"
    property string provider: "mangaplus"
    readonly property string selected: { Sessions.revision; Sessions.activeId; return Sessions.selectedBrowser(provider) }
    readonly property bool previewReady: deck.width >= 400 && deck.height >= 300
        && deck.activeBrowser !== null && deck.activeBrowser.width >= 400 && deck.activeBrowser.documentReady
        && deck.activeBrowser.title === "Fixture /hosting"
    function openApp(id) {
        provider = id
        var existing = Sessions.selectedBrowser(id)
        if (existing) Sessions.switchTo(existing)
        else Sessions.openBrowser({providerId:id, providerName:id, mode:id === "ytmusic" ? "listen" : "read",
            url:FixtureUrl + "/hosting", startUrl:FixtureUrl + "/hosting"}, true)
    }
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Button { text: "Reading app"; onClicked: root.openApp("mangaplus") }
            Button { text: "Audio app"; onClicked: root.openApp("ytmusic") }
            Label { text: "Real engine fixture. Native windows are opened by the page."; Layout.fillWidth:true }
        }
        RowLayout {
            Button { text: "Back"; onClicked: deck.back(root.selected) }
            Button { text: "Forward"; onClicked: deck.forward(root.selected) }
            Button { text: "Reload"; onClicked: deck.reload(root.selected) }
            Button { text: "Address"; checkable:true; checked:address.visible; onClicked:address.visible=checked }
            Button { text: "Max"; enabled:!!root.selected; onClicked:deck.maximizeSession(root.selected, fullSize) }
            Button { text: "Close this space"; enabled:!!root.selected; onClicked:deck.closeTab(root.selected) }
        }
        TextField {
            id: address
            visible:false
            Layout.fillWidth:true
            placeholderText:"Address or search"
            onAccepted:deck.navigateInput(root.selected,text)
        }
        RowLayout {
            Layout.fillWidth:true; Layout.fillHeight:true
            ColumnLayout {
                Layout.minimumWidth:220; Layout.preferredWidth:220; Layout.maximumWidth:220
                Layout.fillWidth:false; Layout.fillHeight:true
                Button { text:root.provider === "ytmusic" ? "New soundspace" : "New readspace"; onClicked:deck.newSpace(root.selected,false,true) }
                Button { text:"Reopen closed space"; onClicked:deck.reopenClosedSpace(root.provider) }
                ListView {
                    Layout.fillWidth:true; Layout.fillHeight:true
                    model: { Sessions.revision; Sessions.activeId; return Sessions.browserSpaces(root.provider) }
                    delegate: Button {
                        required property var modelData
                        width:ListView.view.width
                        text:modelData.title || modelData.id
                        highlighted:modelData.id === root.selected
                        onClicked:Sessions.switchTo(modelData.id)
                    }
                }
                Label { text:"Consent stays in chrome and resizes the page."; wrapMode:Text.WordWrap; Layout.fillWidth:true }
            }
            Feria.FeriaSessionDeck {
                id:deck
                Layout.minimumWidth:400; Layout.fillWidth:true; Layout.fillHeight:true
                sessions:Sessions; accountStore:FeriaAccount; policy:FeriaBrowserPolicy; displayed:true
            }
        }
    }
    Item { id:fullSize; anchors.fill:parent; visible:false }
    Component.onCompleted:openApp("mangaplus")
}
