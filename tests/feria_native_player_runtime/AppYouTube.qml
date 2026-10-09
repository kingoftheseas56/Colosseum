import QtQuick
import "../../qml" as App

App.Main {
    id: main
    property int nativeTestStage:0
    property int nativeTestTicks:0
    property var nativeTestBrowser:null
    property var nativeTestPlayer:null
    function findNativeVisual(node,name) {
        if(!node)return null
        if(node.objectName===name)return node
        for(var child of node.children||[]){var found=findNativeVisual(child,name);if(found)return found}
        return null
    }
    Timer {
        id: nativeTestTimer
        interval:500;repeat:true;running:true
        onTriggered:{
            if(++main.nativeTestTicks>240){console.log("FERIA_APP_YOUTUBE_FAIL watchdog",main.nativeTestStage);Qt.quit();return}
            if(main.nativeTestStage===0){AccountController.continueWithoutAccount();main.openFeriaPage();main.nativeTestStage=1}
            var layer=main.findNativeVisual(main.contentItem,"feriaLayer")
            var deck=layer && layer.item ? layer.item.browserDeck : null
            if(main.nativeTestStage===1 && deck && deck.ready){
                layer.item.openHost("youtube","","home",Qt.application.arguments[3] || "https://www.youtube.com/watch?v=aqz-KE-bpKQ",0,null,true)
                main.nativeTestStage=2
            } else if(main.nativeTestStage===2 && deck && deck.activeBrowser && deck.activeBrowser.documentReady){
                main.nativeTestBrowser=deck.activeBrowser
                var entry=main.findNativeVisual(main.contentItem,"feriaPlayInFeria")
                if(!entry || !entry.enabled)return
                console.log("FERIA_APP_TOOLBAR_ENTRY",entry.visible,entry.enabled)
                entry.clicked()
                main.nativeTestStage=3
            } else if(main.nativeTestStage===3 && main.nativeTestBrowser.nativePlaybackActive){
                var player=main.findNativeVisual(main.nativeTestBrowser,"feriaNativePlayer")
                if(!player)return
                if(player.currentState.error){console.log("FERIA_APP_YOUTUBE_FAIL",player.currentState.error);Qt.quit();return}
                if(!player.currentState.ready || player.currentState.audioTracks<1)return
                main.nativeTestPlayer=player
                var state=player.currentState
                player.backend.act(state.document,state.session,"play",0)
                console.log("FERIA_APP_YOUTUBE_LOADED",JSON.stringify(state))
                main.nativeTestStage=4
            } else if(main.nativeTestStage===4 && main.nativeTestPlayer.currentState.position>5){
                nativeTestTimer.stop()
                main.nativeTestStage=5
                main.contentItem.grabToImage(function(result){
                    result.saveToFile(Qt.application.arguments[2]+"/app-youtube.png")
                    console.log("FERIA_APP_YOUTUBE_PASS",JSON.stringify(main.nativeTestPlayer.currentState))
                    main.nativeTestBrowser.prepareForClose(false)
                    Qt.callLater(Qt.quit)
                })
            }
        }
    }
}
