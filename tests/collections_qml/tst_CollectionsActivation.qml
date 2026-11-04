import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml/collections" as CollectionsUi
Item {
    ApplicationWindow {
        id: window
        width: 900; height: 850; visible: true
        QtObject {
            id: controller
            property bool busy: false
            property bool signedIn: true
            property bool retryResetAvailable: false
            property string error: ""
            property string message: ""
            property var received: ({})
            property var publications: []
            property int refreshCount: 0
            property int openCount: 0
            property int clearCount: 0
            property string lastUrl: ""
            property string queuedUrl: ""
            property string queuedTicket: ""
            signal changed()
            signal kept(string id)
            signal copied(string id)
            function refresh() { refreshCount++ }
            function openLink(url, phrase) { lastUrl=String(url);openCount++;return true }
            function clearReceived() { received=({});clearCount++ }
        }
        CollectionsUi.CollectionsPanel {
            id: panel
            anchors.fill: parent
            nativeStore: CollectionsTest.store
            publicationController: controller
            onIncomingDismissed: function(ticket) {
                if (controller.queuedUrl) {
                    const url=controller.queuedUrl; const next=controller.queuedTicket
                    controller.queuedUrl="";controller.queuedTicket=""
                    panel.openIncoming(url,next)
                }
            }
        }
    }
    TestCase {
        name: "CollectionsActivationUi"
        when: windowShown
        SignalSpy { id: dismissed; target: panel; signalName: "incomingDismissed" }
        function link(letter) { return "https://getcolosseum.world/c/"+letter.repeat(43) }
        function init() {
            panel.profileChanged(); controller.openCount=0;controller.refreshCount=0;controller.clearCount=0;controller.lastUrl="";controller.queuedUrl="";controller.queuedTicket="";dismissed.clear()
        }
        function cleanup() { panel.profileChanged() }
        function test_incomingSkipsRefreshAndReplacesReadonly() {
            verify(panel.openIncoming(link("a"),"1"));tryCompare(controller,"openCount",1)
            compare(controller.refreshCount,0);compare(panel.incomingBlocked,false)
            const before=findChild(panel,"incomingCollectionPopup")
            // Main.openNamedCollectionsPage's already-showing branch calls this
            // exact owner method before openIncoming; it must retain the receiver.
            panel.showOverview();compare(panel.sharingActive,true);compare(panel.incomingTicket,"1");compare(before.entryTicket,"1")
            verify(panel.openIncoming(link("b"),"2"));tryCompare(controller,"openCount",2);compare(controller.lastUrl,link("b"));compare(controller.refreshCount,0)
            panel.dismissSharing(panel.profileGeneration,"1");compare(panel.sharingActive,true)
            const popup=findChild(panel,"incomingCollectionPopup");compare(popup.entryTicket,"2");compare(popup.entryGeneration,panel.profileGeneration);window.requestActivate();tryVerify(function(){return findChild(popup,"closeCollectionsSharing").activeFocus});popup.close();tryCompare(dismissed,"count",1)
            compare(dismissed.signalArguments[0][0],"2")
        }
        function test_closingAWhileBQueuedKeepsB_data() {
            return [{tag:"different URL",letter:"b"},{tag:"identical URL",letter:"a"}]
        }
        function test_closingAWhileBQueuedKeepsB(data) {
            verify(panel.openIncoming(link("a"),"1"));tryCompare(controller,"openCount",1)
            const generation=panel.profileGeneration
            const oldPopup=findChild(panel,"incomingCollectionPopup");verify(oldPopup)
            controller.queuedUrl=link(data.letter);controller.queuedTicket="2"
            oldPopup.close();tryCompare(controller,"openCount",2)
            compare(panel.incomingTicket,"2");compare(panel.incomingUrl,link(data.letter));compare(panel.sharingActive,true)
            panel.dismissSharing(generation,"1")
            compare(panel.incomingTicket,"2");compare(panel.sharingActive,true)
            tryVerify(function(){const popup=findChild(panel,"incomingCollectionPopup");return popup && popup.entryTicket === "2"})
            findChild(panel,"incomingCollectionPopup").close();tryCompare(dismissed,"count",2)
            compare(dismissed.signalArguments[1][0],"2")
        }
        function test_outgoingAndPickerBlockDelivery() {
            panel.sharingActive=true;tryCompare(panel,"incomingBlocked",true)
            compare(panel.openIncoming(link("a"),"1"),false);compare(controller.openCount,0)
            panel.profileChanged();panel.createCollection(null);tryCompare(panel,"pickerActive",true)
            compare(panel.openIncoming(link("b"),"2"),false);compare(controller.openCount,0)
            panel.profileChanged();panel.deleteActive=true;compare(panel.openIncoming(link("c"),"3"),false)
        }
        function test_editorBlocksWithoutLosingDraft() {
            const id=CollectionsTest.store.createCollection("Activation draft")
            panel.collectionId=id;panel.showing=true
            tryVerify(function(){return !!findChild(panel,"editCollectionButton")})
            mouseClick(findChild(panel,"editCollectionButton"));tryCompare(panel,"pendingChanges",true)
            compare(panel.openIncoming(link("a"),"1"),false);compare(panel.pendingChanges,true);compare(controller.openCount,0)
        }
        function test_profileForcedCloseDoesNotDismissNewLink() {
            verify(panel.openIncoming(link("a"),"1"));tryCompare(controller,"openCount",1)
            const old=panel.profileGeneration;panel.profileChanged();compare(dismissed.count,0)
            verify(panel.openIncoming(link("b"),"2"));tryCompare(controller,"openCount",2)
            panel.dismissSharing(old,"1");compare(panel.sharingActive,true);compare(dismissed.count,0)
            tryVerify(function() { const popup=findChild(panel,"incomingCollectionPopup"); return popup && popup.entryGeneration === panel.profileGeneration })
            const popup=findChild(panel,"incomingCollectionPopup");verify(popup);popup.close();tryCompare(dismissed,"count",1)
            compare(panel.sharingActive,false)
        }
    }
}
