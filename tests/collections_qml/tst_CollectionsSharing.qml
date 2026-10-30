import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtTest
import "../../qml/collections" as CollectionsUi

Item {
    ApplicationWindow {
        id: window
        visible: true
        width: 900
        height: 850
        color: "#0c0f18"
        QtObject {
            id: store
            property int revision: 1
            property bool healthy: true
            function filterOptions(items) { return [] }
            function media(key) { return {id:key,title:"An exact title",kind:"book",kindLabel:"Book",world:"Biblio",creator:"",year:"",cover:"",available:false,reference:{}} }
            property var row: ({id:"c1", name:"Across worlds", keptGift:false, presentation:{preset:"midnight",dedication:"For you",signature:"From me",cover_data:""}})
            function getCollection(id) { return id === "c1" ? row : null }
        }
        QtObject {
            id: controller
            property bool busy: false
            property bool retryResetAvailable: false
            property bool signedIn: true
            property string error: ""
            property string message: ""
            property var received: ({})
            property var publications: []
            property var called: ({})
            signal kept(string id)
            signal copied(string id)
            function refresh() {}
            function startNewPublicationRequest() { called={op:"reset"}; retryResetAvailable=false; return true }
            function savePresentation(id,p,rev) { called={op:"save",id:id,p:p,rev:rev}; store.revision++; return true }
            function publish(id,gift,phrase) { called={op:"publish",id:id,gift:gift,phrase:phrase} }
            function publishChanges(id,rev,cid,phrase) { called={op:"update",id:id,rev:rev,cid:cid,phrase:phrase} }
            function revoke(id,rev) { called={op:"revoke",id:id,rev:rev} }
            function openLink(url,phrase) { called={op:"open",url:url,phrase:phrase} }
            function keepReceivedGift() { called={op:"keep"} }
            function openEditionFile(url) { called={op:"file",url:url} }
            function loadCover(url) { return "" }
            function copyLink(id) { called={op:"copyLink",id:id} }
        }
        Component { id: detailComponent; CollectionsUi.CollectionPage { collectionId: "c1" } }
        CollectionsUi.CollectionsSharingPopup { id: popup; store: store; controller: controller; collectionId: "c1" }
    }
    TestCase {
        id: tests
        name: "CollectionsSharingUi"
        parent: window.contentItem
        when: windowShown
        function init() {
            store.row={id:"c1", name:"Across worlds", keptGift:false, presentation:{preset:"midnight",dedication:"For you",signature:"From me",cover_data:""}}
            controller.busy=false; controller.retryResetAvailable=false; controller.signedIn=true; controller.error=""; controller.message=""; controller.called={}; controller.received={}; controller.publications=[]
            popup.collectionId="c1"; popup.tab="publish"; popup.open(); tryCompare(popup,"opened",true)
        }
        function cleanup() { popup.close(); tryCompare(popup,"opened",false) }
        function control(name) { const c=findChild(popup,name); verify(c!==null,name); return c }
        function test_offline_draft() {
            controller.signedIn=false
            verify(!control("publishGiftEdition").enabled)
            verify(!control("publishOrdinaryCollection").enabled)
            const oldRevision=store.revision
            control("giftDedication").text="Saved locally"
            control("saveGiftDraft").clicked()
            compare(controller.called.op,"save"); compare(controller.called.rev,oldRevision)
            compare(controller.called.p.dedication,"Saved locally")
        }
        function test_explicit_publish() {
            control("giftDedication").text="An unsent draft"
            compare(controller.called.op,undefined)
            control("publishGiftEdition").clicked()
            compare(controller.called.op,"publish"); compare(controller.called.gift,true)
            compare(controller.called.id,"c1")
            grabImage(window.contentItem).save("collections-sharing-desktop.png")
        }
        function test_receive_passphrase_and_gift() {
            popup.tab="receive"
            control("receiveCollectionLink").text="https://example.test/collections/s/opaque"
            control("receivePassphrase").text="test phrase"
            control("openSharedCollection").clicked()
            compare(controller.called.op,"open"); compare(controller.called.phrase,"test phrase")
            controller.received={share_id:"opaque",kind:"gift",revision:1,snapshot:{name:"A gift",presentation:{preset:"paper",dedication:"Personal"},members:[{title:"One",kind:"book",note:"A note"}]}}
            wait(20)
            control("openCollectionGift").clicked()
            verify(control("keepReceivedGift").visible)
            wait(30)
            grabImage(window.contentItem).save("collections-sharing-received.png")
            control("keepReceivedGift").clicked()
            compare(controller.called.op,"keep")
        }
        function test_close_clears_passphrases() {
            control("publishPassphrase").text="private"
            control("receivePassphrase").text="private"
            popup.close(); tryCompare(popup,"opened",false)
            compare(control("publishPassphrase").text,"")
            compare(control("receivePassphrase").text,"")
        }
        function test_unknown_publish_requires_explicit_reset() {
            controller.retryResetAvailable=true
            control("requestNewPublication").clicked()
            compare(controller.called.op,undefined)
            verify(control("confirmNewPublication").visible)
            control("confirmNewPublication").clicked()
            compare(controller.called.op,"reset")
            verify(!controller.retryResetAvailable)
            compare(rootPublicationCount(),0)
        }
        function rootPublicationCount() { return controller.publications.length }
        function test_kept_edition_name_survives_local_collision() {
            popup.close(); tryCompare(popup,"opened",false)
            store.row={id:"c1",name:"Across worlds (2)",description:"Local metadata",items:["one"],notes:{},keptGift:true,
                presentation:{preset:"midnight",dedication:"Local metadata"},
                giftSnapshot:{schema_version:1,name:"Across worlds",description:"Original description",members:[],
                    presentation:{preset:"paper",dedication:"The original dedication",signature:"Sender",cover_data:""}}}
            store.revision++
            const detail=createTemporaryObject(detailComponent, window.contentItem, {width:850,height:750,store:store})
            verify(detail!==null)
            compare(detail.displayedName,"Across worlds")
            compare(detail.displayedDescription,"Original description")
            compare(findChild(detail,"collectionDisplayName").text,"Across worlds")
            const gift=findChild(detail,"keptGiftPresentation")
            verify(gift!==null)
            compare(gift.collectionName,"Across worlds")
            compare(gift.presentation.dedication,"The original dedication")
            compare(detail.visibleItems.length,0)
            findChild(gift,"openCollectionGift").clicked()
            compare(detail.giftOpened,true)
            compare(detail.visibleItems.length,1)
            compare(detail.visibleItems[0],"one")
            const revisit=createTemporaryObject(detailComponent, window.contentItem, {width:850,height:750,store:store,giftOpened:true})
            verify(revisit!==null)
            compare(revisit.visibleItems.length,1)
            verify(findChild(revisit,"keptGiftPresentation").opened)
            compare(store.row.name,"Across worlds (2)")
            verify(!findChild(detail,"editCollectionButton").visible)
        }
        function test_phone_layout() {
            window.width=390; window.height=740
            wait(50)
            verify(popup.width<=window.width)
            verify(popup.height<=window.height)
            verify(control("closeCollectionsSharing").width>30)
            grabImage(window.contentItem).save("collections-sharing-phone.png")
            window.width=900; window.height=850
        }
    }
}
