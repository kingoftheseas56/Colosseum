import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml/feria" as Feria
import "../../qml" as App
Item {
    id: stage
    width: 1280; height: 820
    // Approved HTML wallpaper, with Main.qml's adaptive vignette composition.
    // This is the component fixture backdrop, not a replacement app shell.
    Item {
        id: wallpaper
        anchors.fill: parent
        Image { anchors.fill: parent; source: "assets/approved-wallpaper.jpg"; fillMode: Image.PreserveAspectCrop }
        App.WallpaperLuma { id: luma; visible: false; source: "assets/approved-wallpaper.jpg" }
        Rectangle {
            id: vignette
            anchors.fill: parent
            readonly property real extra: 0.27 * Math.max(0, Math.min(1, (luma.luma - 0.50) / 0.35))
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0,0,0,Math.min(0.55,0.34+vignette.extra)) }
                GradientStop { position: 0.5; color: Qt.rgba(0,0,0,Math.min(0.55,0.28+vignette.extra)) }
                GradientStop { position: 1; color: Qt.rgba(0,0,0,Math.min(0.55,0.46+vignette.extra)) }
            }
        }
    }
    Feria.FeriaWorld {
        id: world
        anchors.fill: parent
        backdrop: wallpaper
        lifecycleActive: true
        sharedAccount: true
        browserDeck: deck
        // This component bridge is explicit test setup, not a substitute for
        // the full Main dispatcher or a claim about its integration.
        onBrowserOpenRequested: function(descriptor, fresh, navigate) {
            var id = Sessions.openBrowser(descriptor, fresh, navigate)
            activateBrowserRecord(Sessions.get(id))
            if (navigate) Qt.callLater(function() {
                deck.navigateSession(id, descriptor.url, descriptor.position, descriptor.locator, descriptor.visitContext)
            })
        }
    }
    Feria.FeriaSessionDeck {
        id: deck
        x: world.browserViewport.x; y: world.browserViewport.y
        width: world.browserViewport.width; height: world.browserViewport.height
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
        z: 1
    }
    SignalSpy { id: nativeFind; target: deck.activeBrowser; signalName: "findTextFinished" }
    SignalSpy { id: parkRequest; target: world; signalName: "browserParkRequested" }
    SignalSpy { id: closeRequest; target: world; signalName: "browserCloseRequested" }
    SignalSpy { id: appMinRequest; target: world; signalName: "minimizeClicked" }
    SignalSpy { id: appCloseRequest; target: world; signalName: "powerClicked" }
    TestCase {
        id: suite
        name: "FeriaPageTools"
        when: windowShown
        function resizeStage(w,h) {
            stage.Window.window.width=w; stage.Window.window.height=h
            stage.width=w; stage.height=h
            tryCompare(stage.Window.window,"width",w,5000)
            tryCompare(stage.Window.window,"height",h,5000)
            verify(waitForRendering(stage))
        }
        function init() {
            stage.Window.window.requestActivate()
            tryCompare(stage.Window.window, "active", true, 5000)
            resizeStage(1280,820)
            deck.displayed=true
            world.restoreBrowserWorld()
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck,"quiescing",false,10000)
            world.restoreBrowserWorld()
        }
        function open(provider,path) {
            world.openHost(provider,"","home",FixtureUrl+path,0,null,true)
            var id=Sessions.activeId
            verify(id.length>0)
            tryVerify(function(){return deck.entries[id] && deck.entries[id].browser},10000)
            var page=deck.entries[id].browser
            tryCompare(page,"documentReady",true,15000)
            verify(page.pageTools !== undefined,"Real browser must expose its native page-tools owner")
            return page
        }
        function select(id) { Sessions.switchTo(id); world.activateBrowserRecord(Sessions.get(id)) }
        function host() { return findChild(world,"feriaBrowserHost") }
        function panel() { return findChild(host(),"feriaFindPanel") }
        function field() { return panel().input }
        function evaluate(page,script) {
            var done=false,value
            page.evaluateJavaScript(script,function(result){value=result;done=true})
            tryVerify(function(){return done},5000)
            return value
        }
        function matches(page,count,active) {
            tryVerify(function(){return page.pageTools.hasResults && !page.pageTools.pending},5000)
            compare(page.pageTools.numberOfMatches,count)
            if (active!==undefined) compare(page.pageTools.activeMatch,active)
        }
        function test_nativeFindCountsAndClear() {
            var page=open("mangaplus","/find")
            page.pageTools.openFind(); page.pageTools.search("Comet")
            matches(page,3,1)
            page.pageTools.findNext(); matches(page,3,2)
            page.pageTools.findPrevious(); matches(page,3,1)
            page.pageTools.search("NoSuchTerm2718"); matches(page,0,0)
            compare(findChild(panel().contentItem,"feriaFindCount").text,"No matches")
            page.pageTools.search("Comet"); matches(page,3)
            nativeFind.clear()
            page.pageTools.closeFind(true)
            compare(page.pageTools.findOpen,false)
            compare(page.pageTools.numberOfMatches,0)
            verify(nativeFind.count>0,"Clear must reach the native findText API")
            compare(nativeFind.signalArguments[nativeFind.count-1][0].numberOfMatches,0)
            tryVerify(function(){return evaluate(page,"String(window.getSelection())")===""},5000)
            tryVerify(function(){return page.activeFocus},5000)
        }
        function test_repeatedNativeFindDismissal() {
            var page=open("mangaplus","/repeat-find")
            for(var i=0;i<10;++i) {
                page.pageTools.openFind(); page.pageTools.search("Comet"); matches(page,3)
                page.pageTools.closeFind(true)
                tryCompare(panel(),"visible",false,5000)
                tryVerify(function(){return page.activeFocus},5000)
                tryVerify(function(){return evaluate(page,"String(window.getSelection())")===""},5000)
            }
        }
        function test_zoomIndependentLimitsAndBackgroundShortcuts() {
            var a=open("mangaplus","/a"), aid=Sessions.activeId
            a.pageTools.zoomIn(); compare(a.zoomFactor,1.1)
            var b=open("ytmusic","/music"), bid=Sessions.activeId
            b.pageTools.zoomOut(); compare(b.zoomFactor,0.9)
            select(aid); compare(a.zoomFactor,1.1); compare(b.zoomFactor,0.9)
            a.forceActiveFocus(); keyClick(Qt.Key_Equal,Qt.ControlModifier)
            tryCompare(a,"zoomFactor",1.25,5000)
            keyClick(Qt.Key_Plus,Qt.ControlModifier); tryCompare(a,"zoomFactor",1.5,5000)
            keyClick(Qt.Key_Minus,Qt.ControlModifier); tryCompare(a,"zoomFactor",1.25,5000)
            keyClick(Qt.Key_0,Qt.ControlModifier); tryCompare(a,"zoomFactor",1,5000)
            for(var i=0;i<30;++i)a.pageTools.zoomIn()
            compare(a.zoomFactor,5); compare(a.pageTools.canZoomIn,false)
            for(var j=0;j<30;++j)a.pageTools.zoomOut()
            compare(a.zoomFactor,0.25); compare(a.pageTools.canZoomOut,false)
            a.pageTools.resetZoom(); compare(a.zoomFactor,1)
            deck.displayed=false
            stage.forceActiveFocus()
            keyClick(Qt.Key_Equal,Qt.ControlModifier); keyClick(Qt.Key_F,Qt.ControlModifier)
            compare(a.zoomFactor,1); compare(b.zoomFactor,0.9)
            compare(a.pageTools.findOpen,false); compare(b.pageTools.findOpen,false)
            deck.displayed=true; select(bid); compare(b.zoomFactor,0.9)
        }
        function test_pendingSearchSwitchNavigateAndClose() {
            var a=open("mangaplus","/a"), aid=Sessions.activeId
            var b=open("ytmusic","/b"), bid=Sessions.activeId
            select(aid); a.pageTools.openFind(); a.pageTools.search("Comet")
            verify(a.pageTools.pending)
            select(bid)
            compare(a.pageTools.findOpen,false)
            b.pageTools.openFind(); b.pageTools.search("NoSuchTerm2718"); matches(b,0,0)
            compare(a.pageTools.findOpen,false); compare(a.pageTools.numberOfMatches,0)
            b.pageTools.search("Comet"); verify(b.pageTools.pending)
            var previousNavigation=b.navigationGeneration
            b.url=FixtureUrl+"/navigated"
            tryVerify(function(){return b.navigationGeneration>previousNavigation && b.documentReady && !b.loading},15000)
            compare(b.pageTools.findOpen,false); compare(b.pageTools.query,"")
            b.pageTools.openFind(); b.pageTools.search("Comet"); b.pageTools.closeFind(false,true)
            b.pageTools.openFind(); b.pageTools.search("reading room"); matches(b,1,1)
            b.pageTools.search("Comet"); Sessions.close(bid)
            tryVerify(function(){return !deck.entries[bid]},5000)
            compare(deck.entries[aid].browser,a)
            select(aid); compare(a.pageTools.findOpen,false)
        }
        function test_escapeClosesFindBeforeImmersiveMode() {
            var page=open("ytmusic","/fullscreen")
            var entry=deck.activeEntry
            mouseClick(findChild(host(),"feriaHostFullscreen"))
            tryCompare(entry,"immersive",true,5000)
            verify(waitForRendering(stage))
            page.forceActiveFocus(); keyClick(Qt.Key_F,Qt.ControlModifier)
            tryCompare(panel(),"visible",true,5000)
            keyClick(Qt.Key_Escape)
            tryCompare(panel(),"visible",false,5000)
            compare(entry.immersive,true)
            keyClick(Qt.Key_Escape)
            tryCompare(entry,"immersive",false,5000)
        }
        function test_compactHeaderIsOptInForProviderRoute() {
            var top=findChild(world,"feriaWorldTopBar")
            resizeStage(760,700)
            compare(top.compactBrowserChrome,false)
            verify(findChild(top,"topBarClock").visible)
            open("mangaplus","/compact-header")
            compare(top.compactBrowserChrome,true)
            compare(findChild(top,"topBarClock").visible,false)
            resizeStage(1280,820)
            compare(top.compactBrowserChrome,false)
            verify(findChild(top,"topBarClock").visible)
        }
        function test_visibleSessionTileTargetsSessionNotApplication() {
            var page=open("ytmusic","/controls"), id=Sessions.activeId
            var tile=findChild(host(),"feriaSessionControlTile")
            verify(tile.visible && tile.width<=144 && tile.height<=52)
            var minimize=findChild(host(),"feriaHostMinimize")
            var maximize=findChild(host(),"feriaHostFullscreen")
            var close=findChild(host(),"feriaHostClose")
            verify(minimize.visible && maximize.visible && close.visible)
            for(var control of [minimize,maximize,close]) verify(control.width>=44 && control.height>=44)
            verify(minimize.x<maximize.x && maximize.x<close.x)
            parkRequest.clear(); closeRequest.clear(); appMinRequest.clear(); appCloseRequest.clear()
            mouseClick(minimize)
            compare(parkRequest.count,1); compare(parkRequest.signalArguments[0][0],false)
            compare(appMinRequest.count,0)
            mouseClick(close)
            compare(closeRequest.count,1); compare(closeRequest.signalArguments[0][0],id)
            compare(appCloseRequest.count,0)
        }
        function test_optionsKeyboardAndSiteDisclosure() {
            var page=open("ytmusic","/options")
            var rect=Qt.rect(page.x,page.y,page.width,page.height)
            mouseClick(findChild(host(),"feriaAppOptions"))
            var options=findChild(host(),"feriaOptionsPanel")
            tryCompare(options,"visible",true,5000)
            verify(waitForRendering(stage))
            keyClick(Qt.Key_Equal,Qt.ControlModifier); tryCompare(page,"zoomFactor",1.1,5000)
            keyClick(Qt.Key_0,Qt.ControlModifier); tryCompare(page,"zoomFactor",1,5000)
            mouseClick(findChild(host(),"feriaSiteControls"))
            tryCompare(options,"siteControlsView",true,5000)
            compare(Qt.rect(page.x,page.y,page.width,page.height),rect)
            var original=host().appOptions.layout!==false
            mouseClick(findChild(host(),"feriaAppLayout")); compare(host().appOptions.layout,!original)
            mouseClick(findChild(host(),"feriaAppLayout")); compare(host().appOptions.layout,original)
            mouseClick(findChild(host(),"feriaSiteControlsBack")); compare(options.siteControlsView,false)
            keyClick(Qt.Key_F,Qt.ControlModifier)
            tryCompare(panel(),"visible",true,5000); tryCompare(options,"visible",false,5000)
            compare(Qt.rect(page.x,page.y,page.width,page.height),rect)
        }
        function test_addressAndFindDoNotFightForFocus() {
            var page=open("ytmusic","/address")
            page.forceActiveFocus(); keyClick(Qt.Key_F,Qt.ControlModifier)
            tryCompare(panel(),"visible",true,5000)
            keyClick(Qt.Key_L,Qt.ControlModifier)
            var address=findChild(host(),"feriaBrowserAddressPanel")
            tryCompare(address,"visible",true,5000)
            compare(panel().visible,false)
            keyClick(Qt.Key_Escape)
            tryCompare(address,"visible",false,5000)
            compare(panel().visible,false)
            page.forceActiveFocus(); keyClick(Qt.Key_L,Qt.ControlModifier)
            tryCompare(address,"visible",true,5000)
            keyClick(Qt.Key_F,Qt.ControlModifier)
            tryCompare(panel(),"visible",true,5000)
            compare(address.visible,false)
            tryVerify(function(){return field().activeFocus},5000)
        }
        function test_graphicalControlsAndFloatingGeometry() {
            var page=open("mangaplus","/visual")
            var geometry=Qt.rect(page.x,page.y,page.width,page.height)
            if(Fixture.evidenceRoot.length) {
                verify(waitForRendering(stage))
                verify(Fixture.captureWindow(stage,"chrome-wide.png"))
                resizeStage(760,700)
                verify(Fixture.captureWindow(stage,"chrome-compact.png"))
                resizeStage(1280,820)
            }
            evaluate(page,"window.scrollTo(0,200); true")
            tryVerify(function(){return evaluate(page,"window.scrollY")>=190},5000)
            page.forceActiveFocus(); keyClick(Qt.Key_F,Qt.ControlModifier)
            tryVerify(function(){return panel().visible && field().activeFocus},5000)
            compare(Qt.rect(page.x,page.y,page.width,page.height),geometry)
            compare(evaluate(page,"window.scrollY"),200)
            for (var letter of "Comet") keyClick(letter); matches(page,3,1)
            keyClick(Qt.Key_Return); matches(page,3,2)
            keyClick(Qt.Key_Return,Qt.ShiftModifier); matches(page,3,1)
            keyClick(Qt.Key_F3); matches(page,3,2)
            keyClick(Qt.Key_F3,Qt.ShiftModifier); matches(page,3,1)
            keyClick(Qt.Key_Escape)
            tryCompare(panel(),"visible",false,5000)
            tryVerify(function(){return page.activeFocus},5000)
            var options=findChild(host(),"feriaAppOptions")
            var beforeOptions = Qt.rect(page.x,page.y,page.width,page.height)
            mouseClick(options)
            compare(Qt.rect(page.x,page.y,page.width,page.height),beforeOptions, "Options must overlay without moving the webpage")
            var optionsPanel=findChild(host(),"feriaOptionsPanel")
            verify(optionsPanel && optionsPanel.visible && optionsPanel.width<=320, "Options must be a compact anchored popover")
            verify(waitForRendering(stage), "Options layout must be rendered before pointer targeting")
            var find=findChild(host(),"feriaFindAction")
            verify(find.visible)
            geometry=Qt.rect(page.x,page.y,page.width,page.height)
            if(Fixture.evidenceRoot.length) Fixture.captureWindow(stage,"options-wide.png")
            mouseClick(find)
            tryCompare(panel(),"visible",true,5000)
            compare(Qt.rect(page.x,page.y,page.width,page.height),geometry)
            page.pageTools.search("Comet"); matches(page,3)
            if(Fixture.evidenceRoot.length) {
                waitForRendering(stage)
                verify(Fixture.captureWindow(stage,"find-wide.png"))
            }
            mouseClick(findChild(panel().contentItem,"feriaFindClose"))
            mouseClick(options)
            tryCompare(optionsPanel,"visible",true,5000)
            verify(waitForRendering(stage))
            mouseClick(findChild(host(),"feriaZoomIn"))
            compare(page.zoomFactor,1.1)
            mouseClick(findChild(host(),"feriaZoomReset")); compare(page.zoomFactor,1)
            resizeStage(760,700)
            verify(optionsPanel.x>=0 && optionsPanel.x+optionsPanel.width<=host().width)
            if(Fixture.evidenceRoot.length) {
                verify(waitForRendering(stage))
                verify(Fixture.captureWindow(stage,"options-compact.png"))
            }
            host().focusApp()
            if(Fixture.evidenceRoot.length) {
                verify(waitForRendering(stage))
                verify(Fixture.captureWindow(stage,"chrome-compact-after-find.png"))
            }
            page.pageTools.openFind(); page.pageTools.search("Comet"); matches(page,3)
            verify(panel().x>=0 && panel().x+panel().width<=host().width)
            if(Fixture.evidenceRoot.length) {
                waitForRendering(stage)
                verify(Fixture.captureWindow(stage,"find-compact.png"))
            }
            var top=findChild(world,"feriaWorldTopBar")
            var picker=findChild(top,"topBarWorldPicker")
            var system=findChild(top,"topBarSystemChrome")
            var left=findChild(top,"topBarLeftChrome")
            verify(top.compactBrowserChrome)
            compare(findChild(top,"topBarClock").visible,false)
            verify(left.x+left.width<=picker.x && picker.x+picker.width<=system.x,
                   "Compact shared header must not overlap")
            var next=findChild(panel().contentItem,"feriaFindNext")
            next.forceActiveFocus(); keyClick(Qt.Key_Escape)
            tryCompare(panel(),"visible",false,5000)
        }
    }
}
