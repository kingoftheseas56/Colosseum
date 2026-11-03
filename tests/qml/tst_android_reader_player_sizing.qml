import QtQuick
import QtTest
import "../../qml/reader2" as Reader
import "../../qml/comicreader" as Comic
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidReaderPlayerSizing"
    when: windowShown
    width: 1192; height: 533; visible: true
    Component { id: topFactory; Reader.TopBar { width: testCase.width; shown: true; title: "A very long book title that must stay between the back button and the reader actions"; chapterLabel: "A long chapter title to exercise the right cluster containment" } }
    Component { id: commandsFactory; Comic.ComicReaderCommandBar { width: testCase.width } }
    Component { id: iconFactory; App.PlayerIcon { width: 52; height: 52; kind: "play" } }
    Component { id: appearanceFactory; Reader.AppearancePanel { width: testCase.width; height: testCase.height } }
    function descendants(item, predicate) {
        let found = []
        for (let child of item.children) {
            if (predicate(child)) found.push(child)
            found = found.concat(descendants(child, predicate))
        }
        return found
    }
    function runtime(android,scale) {
        if(typeof FontTestFixture !== "undefined") {
            FontTestFixture.useDesktopRuntime(!android)
            FontTestFixture.setScale(scale)
        }
    }
    function init() { runtime(true,1) }
    function cleanup() { Reader.Theme.androidHost = false; Reader.Theme.fontScale = 1 }
    function test_book_targets_data() { return [{tag:"desktop", android:false, width:1192}, {tag:"android-wide", android:true, width:1192}, {tag:"android-compact", android:true, width:600}] }
    function test_book_targets(data) {
        runtime(data.android,1)
        Reader.Theme.androidHost = data.android
        const top = createTemporaryObject(topFactory, testCase, {width:data.width})
        verify(top !== null)
        wait(20)
        const buttons = descendants(top, child => typeof child.box === "number" && child.visible)
        compare(buttons.length, data.android ? 5 : 8)
        let rects = []
        for (let button of buttons) {
            if (data.android) { verify(button.width >= 48); verify(button.height >= 48); compare(button.box,28) }
            const p = button.mapToItem(top,0,0)
            verify(p.x >= 0 && p.x + button.width <= top.width)
            rects.push({x:p.x,w:button.width})
        }
        rects.sort((a,b)=>a.x-b.x)
        for(let i=1;i<rects.length;++i) verify(rects[i-1].x+rects[i-1].w<=rects[i].x)
        const title = descendants(top, child => child.text === top.title)[0]
        verify(title.width >= 0)
        if (data.android) compare(title.font.pixelSize,20)
        else compare(title.font.pixelSize,17)
    }
    function test_comic_targets() {
        runtime(true,1.3)
        const bar=createTemporaryObject(commandsFactory,testCase,{androidHost:true,width:1192})
        verify(bar!==null); setTokens(bar,1.3); wait(20)
        const buttons=descendants(bar,child=>typeof child.relayout === "function")
        compare(buttons.length,6)
        let last=0
        for(let button of buttons) {
            verify(button.width>=48); verify(button.height>=48)
            const p=button.mapToItem(bar,0,0)
            verify(p.x>=last); verify(p.x+button.width<=bar.width)
            last=p.x+button.width
            const glyph=descendants(button,child=>typeof child.kind === "string")[0]
            compare(glyph.width,28)
            const label=descendants(button,child=>typeof child.text === "string" && typeof child.font !== "undefined")[0]
            compare(label.font.pixelSize,22)
        }
    }
    function test_larger_system_font() {
        runtime(true,1.3)
        Reader.Theme.androidHost=true
        Reader.Theme.fontScale=1.3
        const top=createTemporaryObject(topFactory,testCase)
        verify(top!==null); wait(20)
        const title=descendants(top, child=>child.text===top.title)[0]
        compare(title.font.pixelSize,27)
        const actions=descendants(top,child=>typeof child.box === "number" && child.visible)
        const rightmostTitle=title.mapToItem(top,title.width,0).x
        const search=actions[1].mapToItem(top,0,0).x
        verify(rightmostTitle<=search,"Long title stays outside the action cluster")
    }
    function test_player_icon() {
        const icon=createTemporaryObject(iconFactory,testCase,{androidHost:true})
        verify(icon!==null); compare(icon.iconSize,29.12)
        icon.androidHost=false; compare(icon.iconSize,24)
    }
    Component { id: audioFactory; App.AudioMenu { overlayParent: testCase; y:460 } }
    function setTokens(item,scale,android) {
        if(typeof FontTestFixture !== "undefined") return
        for(let resource of item.resources) {
            if(typeof resource.fontSize === "function") { resource.androidHost=android===undefined ? true : android; resource.fontScale=scale }
        }
        for(let child of item.children) setTokens(child,scale,android)
    }
    function test_audio_panel_containment() {
        runtime(true,1.3)
        const menu=createTemporaryObject(audioFactory,testCase)
        verify(menu!==null)
        setTokens(menu,1.3)
        compare(menu.width,52); compare(menu.height,52)
        menu.panelOpen=true; wait(20)
        const panels=descendants(testCase,child=>typeof child.radius === "number" && child.parent===testCase && child!==menu)
        verify(panels.length>0)
        for(let panel of panels) { verify(panel.x>=0); verify(panel.y>=0); verify(panel.x+panel.width<=testCase.width); verify(panel.y+panel.height<=testCase.height) }
    }
    function blockAt(source,start) {
        verify(start>=0)
        const opening=source.indexOf("{",start)
        let depth=1
        for(let end=opening+1;end<source.length;++end) {
            if(source[end]==="{") ++depth
            else if(source[end]==="}" && --depth===0) return source.slice(start,end+1)
        }
        fail("Unterminated production component")
        return ""
    }
    function test_production_player_buttons() {
        const request=new XMLHttpRequest()
        request.open("GET",Qt.resolvedUrl("../../qml/PlayerPage.qml"),false); request.send()
        const block=blockAt(request.responseText,request.responseText.indexOf("component RoundButton:"))
        const source='import QtQuick; import "../../qml"; Item { id: root; width: 600; height: 100; Theme { id: theme; androidHost: true } function wakeChrome() {} '+block+' Row { spacing:8; RoundButton { size:38; icon:"seekBack" } RoundButton { size:46; hero:true; icon:"play" } RoundButton { size:38; icon:"seekForward" } } }'
        const fixture=Qt.createQmlObject(source,testCase,Qt.resolvedUrl("roundFixture.qml"))
        verify(fixture!==null)
        const buttons=descendants(fixture,child=>typeof child.hero === "boolean")
        compare(buttons.length,3)
        let last=0
        for(let button of buttons) {
            compare(button.width,button.hero ? 64 : 52)
            compare(button.height,button.width)
            const p=button.mapToItem(fixture,0,0)
            verify(p.x>=last); last=p.x+button.width
            const icon=descendants(button,child=>typeof child.iconSize === "number")[0]
            icon.androidHost=true; verify(icon.iconSize>=28)
        }
        fixture.destroy()
    }
    Component { id: imageFactory; Comic.ComicReaderImagePopover { width:testCase.width; height:testCase.height; open:true; advancedOpen:true } }
    Component { id: layoutFactory; Comic.ComicReaderLayoutPopover { width:testCase.width; height:testCase.height; open:true } }
    function test_comic_popover_containment_data() { return [{tag:"image-wide",factory:imageFactory,name:"imagePanel",width:1192},{tag:"layout-wide",factory:layoutFactory,name:"layoutPanel",width:1192},{tag:"image-narrow",factory:imageFactory,name:"imagePanel",width:360},{tag:"layout-narrow",factory:layoutFactory,name:"layoutPanel",width:360}] }
    function test_comic_popover_containment(data) {
        runtime(true,1.3)
        const item=createTemporaryObject(data.factory,testCase,{width:data.width})
        verify(item!==null); setTokens(item,1.3); wait(20)
        const panel=findChild(item,data.name)
        verify(panel!==null)
        verify(panel.x>=0); verify(panel.y>=0)
        verify(panel.x+panel.width<=item.width); verify(panel.y+panel.height<=item.height)
        const scrolls=descendants(panel,child=>typeof child.flickableDirection === "number")
        compare(scrolls.length,1); verify(scrolls[0].clip)
    }
    Component {
        id: comicStackFactory
        Item {
            id: stack
            width:1192; height:533
            property string activeOverlay:""
            property alias hud:stackHud
            property alias image:image
            property alias layoutMenu:layoutMenu
            property alias pages:pages
            property alias loupe:loupe
            Comic.ComicReaderHud {
                id:stackHud
                reader:({currentPage:1,max:20,mode:"long_strip",rtl:false,layout:"long_strip",order:"ltr",stripFraction:0,zoomPercent:100,hasNext:false,hasPrev:false,seriesTitle:"Long comic series title",curLabel:"A chapter with a long title",chromeVisible:true,modalOpen:false,activeOverlay:stack.activeOverlay,bookmarkPages:[]})
                onOpenImage:stack.activeOverlay="image"
                onOpenLayout:stack.activeOverlay="layout"
                onOpenPages:stack.activeOverlay="pages"
                onOpenLoupe:stack.activeOverlay="loupe"
            }
            Comic.ComicReaderImagePopover {
                id:image
                open:stack.activeOverlay==="image"
                chromeTopInset:stackHud.chromeBottom
                railHeight:stackHud.chromeRailHeight
                onDismissRequested:stack.activeOverlay=""
            }
            Comic.ComicReaderLayoutPopover {
                id:layoutMenu
                open:stack.activeOverlay==="layout"
                chromeTopInset:stackHud.chromeBottom
                railHeight:stackHud.chromeRailHeight
                onDismissRequested:stack.activeOverlay=""
            }            Comic.ComicReaderPagesOverlay {
                id:pages
                open:stack.activeOverlay==="pages"
                chromeTopInset:stackHud.chromeBottom
                railHeight:stackHud.chromeRailHeight
                onDismissRequested:stack.activeOverlay=""
            }
            Comic.ComicReaderLoupe {
                id:loupe
                open:stack.activeOverlay==="loupe"
                chromeTopInset:stackHud.chromeBottom
                railHeight:stackHud.chromeRailHeight
                onDismissRequested:stack.activeOverlay=""
            }

        }
    }
    function test_comic_hud_popover_stack_data() { return [{tag:"android",android:true,bottom:132},{tag:"desktop",android:false,bottom:84}] }
    function test_comic_hud_popover_stack(data) {
        runtime(data.android,1)
        const stack=createTemporaryObject(comicStackFactory,testCase)
        verify(stack!==null);setTokens(stack,1,data.android);wait(20)
        compare(stack.hud.chromeBottom,data.bottom)
        compare(stack.hud.chromeRailHeight,data.android?72:54)
        const commands=descendants(stack.hud,child=>typeof child.relayout === "function")
        const imageCommand=commands.filter(child=>child.modelData==="image")[0]
        const layoutCommand=commands.filter(child=>child.modelData==="layout")[0]
        verify(imageCommand!==undefined && layoutCommand!==undefined)
        const strip=findChild(stack.hud,"readerCommandStrip")
        compare(strip.y+strip.height,stack.hud.chromeBottom)
        mouseClick(imageCommand,imageCommand.width/2,imageCommand.height/2)
        compare(stack.activeOverlay,"image")
        const imagePanel=findChild(stack.image,"imagePanel")
        const imageCatcher=findChild(stack.image,"imageDismissCatcher")
        compare(imagePanel.y,data.bottom+8);compare(imageCatcher.y,data.bottom)
        compare(imageCatcher.y+imageCatcher.height,stack.height-stack.hud.chromeRailHeight)
        const commandPoint=layoutCommand.mapToItem(stack,0,0)
        verify(commandPoint.y+layoutCommand.height<=imageCatcher.y,"Image catcher leaves the complete command target exposed")
        mouseClick(layoutCommand,layoutCommand.width/2,layoutCommand.height/2)
        compare(stack.activeOverlay,"layout","Layout command stays clickable while Image is open")
        const layoutPanel=findChild(stack.layoutMenu,"layoutPanel")
        const layoutCatcher=findChild(stack.layoutMenu,"layoutDismissCatcher")
        compare(layoutPanel.y,data.bottom+8);compare(layoutCatcher.y,data.bottom)
        compare(layoutCatcher.y+layoutCatcher.height,stack.height-stack.hud.chromeRailHeight)
        verify(commandPoint.y+layoutCommand.height<=layoutCatcher.y)
        for(let entry of [{command:"pages",overlay:stack.pages,catcher:"pagesDismissCatcher"},{command:"loupe",overlay:stack.loupe,catcher:"loupeTracker"}]) {
            const command=commands.filter(child=>child.modelData===entry.command)[0]
            mouseClick(command,command.width/2,command.height/2)
            compare(stack.activeOverlay,entry.command)
            const catcher=findChild(entry.overlay,entry.catcher)
            compare(catcher.y,data.bottom)
            compare(catcher.y+catcher.height,stack.height-stack.hud.chromeRailHeight)
            verify(commandPoint.y+layoutCommand.height<=catcher.y)
        }
        const band=findChild(stack.pages,"pagesBand")
        compare(band.y+band.height,stack.height-stack.hud.chromeRailHeight)
        verify(band.y>=stack.hud.chromeBottom)
        verify(stack.loupe.lensY>=data.bottom && stack.loupe.lensY<=stack.height-stack.hud.chromeRailHeight)
        const loupeClose=findChild(stack.loupe,"loupeClose")
        compare(loupeClose.width,data.android?52:26)
        compare(loupeClose.height,data.android?52:26)
        if(data.android) {
            stack.activeOverlay="image";stack.image.advancedOpen=true;wait(20)
            verify(imagePanel.y+imagePanel.height<=stack.height-stack.hud.chromeRailHeight)
        }
        const request=new XMLHttpRequest()
        request.open("GET",Qt.resolvedUrl("../../qml/comicreader/ComicReaderShell.qml"),false);request.send()
        compare((request.responseText.match(/chromeTopInset:\s*hud.chromeBottom/g)||[]).length,4,"Production shell binds all temporary surfaces to measured HUD geometry")
        compare((request.responseText.match(/railHeight:\s*hud.chromeRailHeight/g)||[]).length,4)
    }
    function test_appearance_size_row_data() {
        return [{tag:"android-default",android:true,scale:1},{tag:"android-large-text",android:true,scale:1.3},{tag:"desktop",android:false,scale:1}]
    }
    function test_appearance_size_row(data) {
        runtime(data.android,data.scale)
        Reader.Theme.androidHost=data.android
        Reader.Theme.fontScale=data.scale
        const panel=createTemporaryObject(appearanceFactory,testCase,{open:true})
        verify(panel!==null); wait(350)
        const minus=descendants(panel,child=>child.label === "A−" && typeof child.clicked === "function")[0]
        const plus=descendants(panel,child=>child.label === "A+" && typeof child.clicked === "function")[0]
        verify(minus!==undefined && plus!==undefined)
        compare(minus.width,data.android?52:34); compare(plus.width,data.android?52:34)
        const row=minus.parent
        compare(plus.parent,row)
        const value=Array.from(row.children).filter(child=>child.text === panel.curSizePct+" %")[0]
        verify(value!==undefined)
        compare(value.width,row.width-minus.width-plus.width-2*row.spacing)
        const keyboard=descendants(minus,child=>child.keyboardLabel === "A−")[0]
        verify(keyboard!==undefined); keyboard.forceActiveFocus(Qt.TabFocusReason); wait(20)
        let last=0
        for(let item of [minus,value,plus]) {
            const p=item.mapToItem(row,0,0)
            verify(p.x>=last,"Size items stay disjoint")
            verify(p.x+item.width<=row.width,"Size items stay inside their row")
            last=p.x+item.width
            const q=item.mapToItem(panel,0,0)
            verify(q.x>=0 && q.x+item.width<=panel.width,"Size item stays within panel width")
            verify(q.y>=0 && q.y+item.height<=panel.height,"Focused size row stays within panel height")
        }
    }
    Component {
        id: audioChromeFactory
        Reader.ReaderChrome {
            width:1192;height:533
            audioAttached:true;audioTitle:"A very long audiobook and chapter title to exercise chrome containment"
            audioPlaylist:["An opening chapter with a very long title","Second chapter"];audioCurrentIndex:0
            audioPosLabel:"12:34";audioDurLabel:"1:34:56";audioDurationSec:5696
            readAlongAvailable:true;readAlongFollowDetached:true
            Component.onCompleted:wake()
        }
    }
    function test_book_audio_controls_data() {
        return [{tag:"android",android:true,scale:1,width:1192},{tag:"android-large",android:true,scale:1.3,width:1192},{tag:"android-narrow",android:true,scale:1.3,width:360},{tag:"desktop",android:false,scale:1,width:1192}]
    }
    function test_book_audio_controls(data) {
        runtime(data.android,data.scale)
        Reader.Theme.androidHost=data.android;Reader.Theme.fontScale=data.scale
        const chrome=createTemporaryObject(audioChromeFactory,testCase,{width:data.width})
        verify(chrome!==null);wait(180)
        const hud=findChild(chrome,"bookReaderAudioHud")
        verify(hud!==null && hud.visible)
        const h=hud.mapToItem(chrome,0,0)
        verify(h.x>=0 && h.x+hud.width<=chrome.width)
        verify(h.y>=0 && h.y+hud.height<=chrome.height)
        const scroll=findChild(chrome,"bookReaderAudioControlsScroll")
        verify(scroll!==null)
        const buttons=descendants(hud,child=>typeof child.kind === "string" && typeof child.box === "number")
        compare(buttons.length,6)
        const sorted=buttons.slice().sort((a,b)=>a.mapToItem(scroll.contentItem,0,0).x-b.mapToItem(scroll.contentItem,0,0).x)
        let last=0
        for(let button of sorted) {
            compare(button.width,data.android?52:button.box)
            const glyph=descendants(button,child=>typeof child.kind === "string")[0]
            compare(glyph.width,data.android?28:Math.round(button.box*0.62))
            const p=button.mapToItem(scroll.contentItem,0,0)
            verify(p.x>=last,"Audiobook button hosts stay disjoint")
            last=p.x+button.width
            verify(last<=scroll.contentWidth)
            if(data.android && scroll.interactive) {
                const key=descendants(button,child=>typeof child.keyboardLabel === "string")[0]
                key.forceActiveFocus(Qt.TabFocusReason);wait(10)
                const q=button.mapToItem(scroll,0,0)
                verify(q.x>=0 && q.x+button.width<=scroll.width,"Narrow audiobook controls remain reachable by focus/scroll")
            }
        }
        const keys=descendants(hud,child=>typeof child.keyboardLabel === "string")
        const play=keys.filter(child=>child.keyboardLabel==="Play audiobook")[0]
        verify(play!==undefined);compare(play.width,data.android?64:48);compare(play.height,data.android?64:48)
        for(let label of ["Audiobook position","Audiobook volume","Toggle elapsed and remaining time"]){
            const control=keys.filter(child=>child.keyboardLabel===label)[0]
            verify(control!==undefined)
            if(data.android) verify(control.height>=48,label+" minimum height")
        }
        const speed=keys.filter(child=>child.keyboardLabel.indexOf("Playback speed ")===0)[0]
        verify(speed!==undefined);compare(speed.height,data.android?52:34)
        const narration=descendants(chrome,child=>child.keyboardLabel==="Return to narration")[0]
        verify(narration!==undefined);compare(narration.height,data.android?52:34)
        const row=findChild(chrome,"bookReaderAudioScrubRow")
        const transport=findChild(chrome,"bookReaderAudioTransport")
        const chapter=findChild(chrome,"bookReaderAudioChapter")
        verify(chapter.y+chapter.height<=row.y && row.y+row.height<=transport.y,"The three audiobook HUD regions stay disjoint")
        if(data.android) {
            verify(transport.y+transport.height<=hud.height)
            const targets=descendants(transport,child=>typeof child.keyboardLabel === "string")
            const rects=targets.map(item=>{ const p=item.mapToItem(scroll.contentItem,0,0); return {x:p.x,y:p.y,w:item.width,h:item.height,label:item.keyboardLabel} })
            for(let i=0;i<rects.length;++i) {
                const a=rects[i]
                verify(a.w>=48 && a.h>=48,a.label+" minimum target")
                verify(a.x>=0 && a.y>=0 && a.x+a.w<=scroll.contentWidth && a.y+a.h<=scroll.height,a.label+" remains in transport canvas")
                for(let j=0;j<i;++j) {
                    const b=rects[j]
                    verify(a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y,a.label+" and "+b.label+" targets stay disjoint")
                }
            }
        }
        if(data.width===360) verify(scroll.interactive && scroll.clip && scroll.contentWidth>scroll.width)
    }
    Component { id: errorCardFactory; Comic.ComicReaderUnitError { width:360;height:533;code:"decode_failed";pageIndex:0 } }
    Component { id: settingsFactory; Comic.ComicReaderSettingsSheet {} }
    Component { id: searchFactory; Reader.SearchSheet { width:360;height:533;open:true;touchInput:true } }
    function test_remaining_reader_controls() {
        runtime(true,1.3)
        Reader.Theme.androidHost=true;Reader.Theme.fontScale=1.3
        const search=createTemporaryObject(searchFactory,testCase)
        verify(search!==null)
        const input=findChild(search,"reader2SearchInput")
        verify(input!==null);compare(input.height,48)
        const ip=input.mapToItem(input.parent,0,0)
        verify(ip.y>=0 && ip.y+input.height<=input.parent.height)
        const error=createTemporaryObject(errorCardFactory,testCase)
        verify(error!==null)
        const retry=findChild(error,"retryAction"), skip=findChild(error,"skipAction")
        verify(retry!==null && skip!==null)
        compare(retry.height,52);compare(skip.height,52)
        const a=retry.mapToItem(error,0,0),b=skip.mapToItem(error,0,0)
        verify(a.x>=0 && a.x+retry.width<=b.x && b.x+skip.width<=error.width)
        const settings=createTemporaryObject(settingsFactory,testCase,{width:360,height:533})
        verify(settings!==null);settings.open();wait(160)
        const body=findChild(settings,"settingsBody"),close=findChild(settings,"settingsCloseX")
        verify(body!==null && close!==null)
        compare(close.width,52);compare(close.height,52)
        const glyph=descendants(close,child=>child.kind==="close")[0]
        compare(glyph.width,28)
        const viewport=findChild(settings,"settingsViewport")
        verify(viewport.interactive && viewport.clip)
        const actions=descendants(settings,child=>typeof child.tapped === "function" && typeof child.label === "string")
        verify(actions.length>8)
        for(let action of actions) if(action.visible) {
            verify(action.width>=48 && action.height>=48,action.label+" minimum host")
            const key=descendants(action,child=>typeof child.keyboardLabel === "string")[0]
            verify(key!==undefined);key.forceActiveFocus(Qt.TabFocusReason);wait(10)
            const p=action.mapToItem(viewport,0,0)
            verify(p.x>=0 && p.x+action.width<=viewport.width,action.label+" width containment x="+p.x+" w="+action.width+" view="+viewport.width+" parent="+action.parent.width)
            verify(p.y>=0 && p.y+action.height<=viewport.height,action.label+" focus reachability")
        }
        const req=new XMLHttpRequest();req.open("GET",Qt.resolvedUrl("../../qml/reader2/ReaderShell.qml"),false);req.send()
        const marker=req.responseText.indexOf("id: openErrorView")
        const block=blockAt(req.responseText,req.responseText.lastIndexOf("Rectangle {",marker))
        const source='import QtQuick; import "../../qml/reader2"; Item {id:shell; width:360;height:533;property bool openErrorShown:true;property string openErrorText:"A long failed-open explanation for the current book";property int backs:0;function requestEscape(){}function goBack(){++backs} '+block+'}'
        const fixture=Qt.createQmlObject(source,testCase,Qt.resolvedUrl("bookErrorFixture.qml"))
        verify(fixture!==null)
        const text=descendants(fixture,child=>child.text==="Go back")[0]
        verify(text!==undefined);const back=text.parent
        compare(back.height,52)
        const p=back.mapToItem(fixture,0,0)
        verify(p.x>=0 && p.x+back.width<=fixture.width && p.y>=0 && p.y+back.height<=fixture.height)
        mouseClick(back,back.width/2,back.height/2);compare(fixture.backs,1)
        fixture.destroy()
    }
    function test_appearance_containment() {
        runtime(true,1.3)
        Reader.Theme.androidHost=true
        Reader.Theme.fontScale=1.3
        const panel=createTemporaryObject(appearanceFactory,testCase,{open:true})
        verify(panel!==null)
        wait(350)
        const column=descendants(panel,child=>child.width===panel.colWidth && typeof child.color !== "undefined")[0]
        verify(column!==undefined)
        const pos=column.mapToItem(panel,0,0)
        verify(pos.x>=0 && pos.x+column.width<=panel.width)
        verify(pos.y>=0 && pos.y+column.height<=panel.height)
        const scrolls=descendants(column,child=>typeof child.flickableDirection === "number")
        verify(scrolls.length>0)
        verify(scrolls[0].contentHeight>scrolls[0].height)
        const buttons=descendants(column,child=>typeof child.clicked === "function" && typeof child.label === "string")
        verify(buttons.length>0)
        for(let button of buttons) { verify(button.width>=48,button.label+" width "+button.width); verify(button.height>=48,button.label+" height "+button.height) }
    }
}
