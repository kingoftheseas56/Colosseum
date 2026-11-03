import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidPlayerMenuSizing"
    when: windowShown
    width: 1192; height: 533; visible: true
    property var fixture: null
    function runtime(android,scale) {
        if(typeof FontTestFixture !== "undefined") {
            FontTestFixture.useDesktopRuntime(!android)
            FontTestFixture.setScale(scale)
        }
    }
    function init() { runtime(true,1.3) }
    function descendants(item,predicate) {
        let result=[]
        for(let child of item.children) {
            if(predicate(child)) result.push(child)
            result=result.concat(descendants(child,predicate))
        }
        return result
    }
    function tokens(item) {
        if(typeof FontTestFixture !== "undefined") return
        for(let resource of item.resources) if(typeof resource.fontSize === "function") { resource.androidHost=true; resource.fontScale=1.3 }
        for(let child of item.children) tokens(child)
    }
    function blockAt(source,start) {
        verify(start>=0)
        let depth=1
        const opening=source.indexOf("{",start)
        for(let end=opening+1;end<source.length;++end) {
            if(source[end]==="{") ++depth
            else if(source[end]==="}" && --depth===0) return source.slice(start,end+1)
        }
        fail("Unterminated production component"); return ""
    }
    function makeFixture(android,kind) {
        runtime(android,1.3)
        const req=new XMLHttpRequest(); req.open("GET",Qt.resolvedUrl("../../qml/PlayerPage.qml"),false); req.send()
        const component=kind==="speed"?"SpeedMenuButton":"FillMenuButton"
        const blocks=["RoundButton",component].map(name=>blockAt(req.responseText,req.responseText.indexOf("component "+name+":"))).join("\n")
        const source='import QtQuick; import "../../qml"; import "../../qml/PlayerFocusContainment.js" as FocusContainment; Item { id: root; width:1192; height:354; property bool androidHost:'+android+'; Theme { id:theme; androidHost:root.androidHost; fontScale:1.3 } QtObject { id:mpv; property real speed:1 } QtObject { id:playerSettings; property int seekStepSeconds:10 } property var speedChoices:[0.25,0.5,0.75,1,1.25,1.5,1.75,2,2.25,2.5,2.75,3]; property var sleepPresets:[{label:"10 minutes",minutes:10},{label:"20 minutes",minutes:20},{label:"30 minutes",minutes:30}]; property bool sleepTimerActive:false; property string sleepTimerMode:""; property int fillModeIndex:0; property var fillModes:[{label:"Fit"},{label:"Fill"},{label:"Zoom"},{label:"16:9"},{label:"4:3"},{label:"2.39:1"}]; property int applied:-1; function applyFill(index){applied=index} function sleepTimerLabel(){return ""} function sleepPresetSelected(value){return false} function setSleepTimer(value){} function cancelSleepTimer(){} function closeMenus(){} function wakeChrome(){} function currentKeyboardFocusItem(){return null} function restoreKeyboardFocus(item){} function clamp(v,a,b){return Math.max(a,Math.min(v,b))} Item { id:chrome; anchors.fill:parent } '+blocks+' '+component+' { x:1100; y:290; panelOpen:true } }'
        fixture=Qt.createQmlObject(source,testCase,Qt.resolvedUrl("menuFixture.qml")); verify(fixture!==null); wait(20)
        return fixture
    }
    function cleanup() { if(fixture) fixture.destroy(); fixture=null }
    function test_rows_and_keyboard_data() { return [{tag:"speed-android",android:true,kind:"speed"},{tag:"fill-android",android:true,kind:"fill"},{tag:"speed-desktop",android:false,kind:"speed"},{tag:"fill-desktop",android:false,kind:"fill"}] }
    function test_rows_and_keyboard(data) {
        const root=makeFixture(data.android,data.kind)
        const menus=descendants(root,child=>typeof child.rowPitch === "number")
        compare(menus.length,1)
        const menu=menus[0]
        compare(menu.rowHeight,data.android?52:(data.kind==="speed"?36:32))
        compare(menu.rowPitch,menu.rowHeight+2)
        const panel=findChild(root,data.kind==="speed"?"playerSpeedPanel":"playerFillPanel")
        verify(panel!==null)
        if(data.android) { verify(panel.y>=0); verify(panel.y+panel.height<=root.height) }
        const repeaters=descendants(panel,child=>typeof child.keyboardIndex === "number" && child.count===(data.kind==="speed"?12:6))
        compare(repeaters.length,1)
        const repeater=repeaters[0]
        const rows=[]
        for(let i=0;i<repeater.count;++i) rows.push(repeater.itemAt(i))
        for(let i=0;i<rows.length;++i) { compare(rows[i].height,menu.rowHeight); if(i) verify(rows[i-1].y+rows[i-1].height<=rows[i].y) }
        repeater.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_End); wait(20)
        compare(repeater.keyboardIndex,repeater.count-1)
        if(data.android) {
            const scroll=descendants(panel,child=>typeof child.flickableDirection === "number")[0]
            verify(scroll.contentY>0)
            const last=rows[rows.length-1].mapToItem(scroll,0,0)
            verify(last.y>=0 && last.y+menu.rowHeight<=scroll.height+1)
        }
        if(data.kind==="fill") { keyClick(Qt.Key_Return); compare(root.applied,5) }
    }
    Component { id: audioFactory; App.AudioMenu { overlayParent:testCase; y:460; delay:0.1; tracks:[{id:"a",label:"English"},{id:"b",label:"A long alternate language track title"}] } }
    function test_audio_delay_targets() {
        const menu=createTemporaryObject(audioFactory,testCase); verify(menu!==null); tokens(menu); menu.panelOpen=true; wait(20)
        const panel=descendants(testCase,child=>child.parent===testCase && child!==menu && typeof child.radius === "number")[0]
        const heading=descendants(panel,child=>child.text === "Audio" && child.font !== undefined)[0]
        compare(heading.font.pixelSize,22)
        verify(panel!==undefined)
        const buttons=descendants(panel,child=>typeof child.clicked === "function" && typeof child.text === "string")
        compare(buttons.length,3)
        let last=0
        for(let button of buttons) { verify(button.width>=48); verify(button.height>=48); const p=button.mapToItem(panel,0,0); verify(p.x>=last); last=p.x+button.width; verify(last<=panel.width); verify(p.y>=0&&p.y+button.height<=panel.height) }
    }
    Component { id: styleFactory; App.SubStyleBar { width:1192; height:130; open:true } }
    function test_style_controls_scroll() {
        const bar=createTemporaryObject(styleFactory,testCase); verify(bar!==null); tokens(bar); wait(20)
        const buttons=descendants(bar,child=>typeof child.clicked === "function" && typeof child.text === "string")
        compare(buttons.length,6)
        for(let button of buttons) { verify(button.width>=48); verify(button.height>=48) }
        const scroll=descendants(bar,child=>typeof child.flickableDirection === "number")[0]
        verify(scroll!==undefined); verify(scroll.contentWidth>scroll.width); verify(scroll.clip)
        const last=buttons[buttons.length-1]
        const key=descendants(last,child=>child.activeFocusOnTab && typeof child.triggered === "function")[0]
        verify(key!==undefined); key.forceActiveFocus(Qt.TabFocusReason); wait(20)
        verify(scroll.contentX>0)
        const p=last.mapToItem(scroll,0,0)
        verify(p.x>=0 && p.x+last.width<=scroll.width)
    }    Component { id: partyFactory; App.WatchPartyPanel { overlayParent:testCase; y:460; panelOpen:true } }
    function test_watch_party_action_hosts() {
        const party=createTemporaryObject(partyFactory,testCase); verify(party!==null); tokens(party); wait(20)
        const panel=findChild(testCase,"watchPartyPanel")
        verify(panel!==null)
        verify(panel.x>=0 && panel.x+panel.width<=testCase.width)
        verify(panel.y>=0 && panel.y+panel.height<=testCase.height)
        const buttons=descendants(panel,child=>typeof child.clicked === "function" && typeof child.label === "string")
        verify(buttons.length>0)
        for(let button of buttons) if(button.visible) { verify(button.width>=48); verify(button.height>=48) }
        const close=findChild(panel,"watchPartyClose")
        verify(close.width>=48 && close.height>=48)
    }

    function productionBinding(source, name) {
        const match = source.match(new RegExp("readonly property (?:real|bool) " + name + ": ([^\\n]+)"))
        verify(match !== null, "Missing production binding: " + name)
        return match[0]
    }
    function makeControlFixture(data) {
        runtime(data.android, 1.3)
        const req = new XMLHttpRequest()
        req.open("GET", Qt.resolvedUrl("../../qml/PlayerPage.qml"), false); req.send()
        const production = req.responseText
        const stripId = production.indexOf("id: playerControlStrip")
        const strip = blockAt(production, production.lastIndexOf("Item {", stripId))
        const dockHeight = production.match(/height: theme.androidHost \? \(playerControlStrip.stacked[^\n]+/)[0]
        const seekStart = production.indexOf("id: seekRow")
        // Reuse the production timeline's positioning header without media rendering/actions.
        const seekHeader = production.slice(production.lastIndexOf("Row {", seekStart), production.indexOf("Text {", seekStart)) + "}"
        const components = ["RoundButton", "VolumeControl", "SpeedMenuButton", "FillMenuButton"].map(name => blockAt(production, production.indexOf("component " + name + ":"))).join("\n")
        const geometry = ["chromeVisibleWidth", "tight", "utilitySpace", "barSnug", "barTiny"].map(name => productionBinding(production, name)).join("\n")
        const source = `import QtQuick; import "../../qml"; import "../../qml/PlayerFocusContainment.js" as FocusContainment
Item {
    id: root; width: ${data.width}; height: 533
    property bool adjacent: ${data.adjacent}; property bool starting: ${data.retry}; property bool errored: false
    property bool browserOpen: false; property bool shellWindowed: true
    property string mediaId: "movie:fixture"; property int seekBackSeconds: 10; property int seekForwardSeconds: 10
    property var streamCandidates: [{},{}]; property var watchPartySource: ({}); property bool watchPartySourceMatchesRoom: false
    property string audioChipValue: ""; property var audioRows: []; property string subsChipValue: ""; property var subRows: []
    property var subtitleSearchMeta: ({type:"movie",imdbId:""}); property string subStreamId: ""; property bool subsLoading: false
    property var speedChoices: [0.5,1,1.5,2]; property var sleepPresets: []; property bool sleepTimerActive: false
    property string sleepTimerMode: ""; property int fillModeIndex: 0; property var fillModes: [{label:"Fit"}]
    Theme { id: theme; androidHost: ${data.android}; fontScale: 1.3 }
    QtObject { id: mpv; property bool pause: false; property bool mute: false; property real volume: 50; property real speed: 1; property var audioTracks: []; property string audioTrack: ""; property real audioDelay: 0; property string subtitleTrack: ""; property real subDelay: 0 }
    QtObject { id: playerSettings; property int seekStepSeconds: 10 }
    QtObject { id: subStyleBar; property bool open: false }
    function hasAdjacentEpisode(direction) { return adjacent }
    function volumeFraction() { return mpv.volume / 100 }
    function currentCastUrl() { return "https://fixture.invalid/media" }
    function currentStreamCandidate() { return ({}) }
    function downloadIcon() { return "download" }
    function downloadTooltip() { return "Download" }
    function subtitleAutoStatusText() { return "" }
    function currentKeyboardFocusItem() { return null }
    function restoreKeyboardFocus(item) {} function closeMenus() {} function wakeChrome() {}
    function clamp(v,a,b) { return Math.max(a,Math.min(v,b)) }
    function sleepTimerLabel() { return "" } function sleepPresetSelected(value) { return false }
    ${geometry}
    Item { id: chrome; anchors.fill: parent
        Item { id: dock; objectName: "fixtureDock"; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            ${dockHeight}
            ${seekHeader}
            ${strip}
        }
    }
    ${components}
}`
        fixture = Qt.createQmlObject(source, testCase, Qt.resolvedUrl("controlFixture.qml"))
        verify(fixture !== null); wait(30)
        return fixture
    }
    function test_control_strip_data() {
        let rows = []
        for (let android of [true, false]) {
            for (let width of (android ? [360, 400, 1192] : [1192])) {
                for (let adjacent of [false, true]) for (let retry of [false, true])
                    rows.push({tag: (android ? "android" : "desktop") + "-" + width + "-" + adjacent + "-" + retry, android:android, width:width, adjacent:adjacent, retry:retry})
            }
        }
        return rows
    }
    function test_control_strip(data) {
        const root = makeControlFixture(data)
        const dock = findChild(root, "fixtureDock")
        const strip = findChild(root, "playerControlStrip")
        const transport = findChild(root, "playerTransportRow")
        const left = findChild(root, "playerLeftUtilityRow")
        const right = findChild(root, "playerRightControlCluster")
        verify(dock && strip && transport && left && right)
        if (data.android && data.width <= 400) verify(strip.stacked)
        if (!data.android) { verify(!strip.stacked); compare(dock.height, 126) }
        if (data.android && data.width === 1192 && !data.adjacent && !data.retry) verify(!strip.stacked)
        const timelineHeader = dock.children[0]
        verify(timelineHeader.y + timelineHeader.height <= strip.y)
        let targets = []
        for (let row of [left, transport, right]) for (let child of row.children) {
            if (child.visible && child.width > 0 && child.height > 0) {
                const point = child.mapToItem(dock, 0, 0)
                verify(point.x >= 0 && point.x + child.width <= dock.width, "Horizontal containment")
                verify(point.y >= strip.y && point.y + child.height <= dock.height, "Vertical containment")
                targets.push({x: point.x, y: point.y, width: child.width, height: child.height})
            }
        }
        verify(targets.length >= 5)
        for (let i=0; i<targets.length; ++i) for (let j=i+1; j<targets.length; ++j) {
            const a=targets[i], b=targets[j]
            verify(a.x+a.width <= b.x+0.01 || b.x+b.width <= a.x+0.01 || a.y+a.height <= b.y+0.01 || b.y+b.height <= a.y+0.01, "Disjoint touch targets " + i + "/" + j)
        }
        compare(transport.width, data.android ? (data.adjacent ? 304 : 184) : (data.adjacent ? 240 : 144))
        compare(transport.x + transport.width / 2, strip.width / 2)
    }

}
