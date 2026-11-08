import QtQuick
import QtTest
import "../../../qml" as Colosseum

TestCase {
    id: harness
    name: "TrackerHomeServerPlacement"
    width: 1024
    height: 768
    visible: true
    when: windowShown

    QtObject {
        id: fixture
        property bool available: true
        property bool busy: false
        property string notice: ""
        property bool hasError: false
        property var connections: []
        signal profileChanged()
        signal statusChanged()
        function connectServer(kind, name, address, user, password) { return false }
        function cancelConnect() { busy = false }
        function setConnectionEnabled(id, enabled) { return true }
        function removeConnection(id) { return true }
    }

    Colosseum.TrackerSyncCenterPage {
        id: page
        anchors.fill: parent
        trackerModel: null
        trackerConnector: null
        nuvioAccount: null
        stremioState: null
        homeServerController: fixture
    }

    function test_card_is_in_external_services_section() {
        const card = findChild(page, "trackerHomeServersCard")
        verify(card !== null)
        compare(card.controller, fixture)
        verify(findChild(card, "trackerHomeServerAddButton") !== null)
        verify(card.width > 100)
        verify(card.height > 50)
    }
}
