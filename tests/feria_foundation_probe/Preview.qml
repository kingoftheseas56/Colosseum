import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qml/feria" as Feria

ApplicationWindow {
    id: root
    visible: true
    width: 1100; height: 760
    title: "Feria foundation 1–5 · real engine fixture"
    readonly property bool previewReady: deck.width >= 400 && deck.height >= 300
        && deck.activeBrowser !== null && deck.activeBrowser.documentReady
    property string provider: "mangaplus"
    readonly property string selected: { Sessions.revision; Sessions.activeId; return Sessions.selectedBrowser(provider) }
    readonly property var nav: { Sessions.revision; Sessions.activeId; return deck.navigationForSession(selected) }
    function openApp(id, mode) {
        provider = id
        if (!Sessions.selectedBrowser(id))
            Sessions.openBrowser({providerId: id, providerName: id, mode: mode,
                url: FixtureUrl + "/a", startUrl: FixtureUrl + "/home"}, true)
        else if (Sessions.activeId !== Sessions.selectedBrowser(id))
            Sessions.switchTo(Sessions.selectedBrowser(id))
    }
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Button { text: "Reading app"; onClicked: root.openApp("mangaplus", "read") }
            Button { text: "Audio app"; onClicked: root.openApp("ytmusic", "listen") }
            Label { text: "Functional fixture host. Product styling and open choices are not settled." }
        }
        RowLayout {
            Button { text: "Back"; enabled: root.nav && root.nav.canGoBack; onClicked: deck.back(root.selected) }
            Button { text: "Forward"; enabled: root.nav && root.nav.canGoForward; onClicked: deck.forward(root.selected) }
            Button { text: root.nav && root.nav.loading ? "Stop" : "Reload"
                onClicked: root.nav.loading ? deck.stop(root.selected) : deck.reload(root.selected) }
            TextField { id: address; Layout.fillWidth: true; placeholderText: "Address or search"
                onAccepted: deck.navigateInput(root.selected, text) }
            Button { text: "Sidebar"; onClicked: sidebar.visible = !sidebar.visible }
        }
        Item {
            id: body
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                id: sidebar
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 240
                Button { text: root.provider === "ytmusic" ? "soundspaces+" : "readspaces+"
                    onClicked: deck.newSpace(root.selected, false, true) }
                ListView {
                    Layout.fillWidth: true; Layout.preferredHeight: 190
                    model: { Sessions.revision; Sessions.activeId; return Sessions.browserSpaces(root.provider) }
                    delegate: Button {
                        required property var modelData
                        width: ListView.view.width
                        text: modelData.title || modelData.id
                        highlighted: modelData.id === root.selected
                        onClicked: Sessions.switchTo(modelData.id)
                    }
                }
                Label { text: "This space's native history" }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    model: root.nav ? root.nav.historyItems : null
                    delegate: Button {
                        required property int offset
                        required property string title
                        width: ListView.view.width
                        text: offset + " · " + title
                        enabled: offset !== 0
                        property string originId: root.selected
                        property int token: root.nav ? root.nav.historyRevision : -1
                        onClicked: deck.historyOffset(originId, offset, token)
                    }
                }
            }
            Feria.FeriaSessionDeck {
                id: deck
                anchors.left: sidebar.visible ? sidebar.right : parent.left
                anchors.leftMargin: sidebar.visible ? 8 : 0
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
                displayed: true
            }
        }
        RowLayout {
            Repeater {
                model: { Sessions.revision; Sessions.activeId; return Sessions.browserApps() }
                Button {
                    required property var modelData
                    text: modelData.title
                    onClicked: { root.provider = modelData.providerId; Sessions.activateBrowserApp(modelData.providerId) }
                }
            }
            Label { text: "App-only projection · synthetic local pages" }
        }
    }
    Component.onCompleted: root.openApp("mangaplus", "read")
}
