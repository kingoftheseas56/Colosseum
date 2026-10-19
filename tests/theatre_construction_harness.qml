// Actual construction/lifecycle regression for the Theatre production QML components.
// Non-network fixture: 22 shelves x 20 harmless poster descriptors, viewport 984, page width 1732.
// The synchronous _push captures the first layout boundary before the Column settles;
// no private timing constants or artificial viewport gates are used in production code.
import QtQuick
import QtCore
import "../qml" as UI

Item {
    id: h
    width: 1800; height: 984
    property var failures: []
    property var lastPush: null
    property var rows: []
    property var refs: []
    property var firstRail: null
    property int loaderCalls: 0
    property int canceled: 0
    property int phase: 0
    property real settledHeight: 0
    property int initialTransitions: -1
    property int initialRails: -1
    property int immediatePositionsValid: 0
    property var demandKeys: []
    property var factsController: ({
        setVisible: function(keys) { h.demandKeys = keys.slice() },
        cancel: function() {}
    })

    function ok(value, reason) {
        if (!value) failures.push("phase="+phase+": "+reason)
    }
    function fixtures() {
        var out = []
        for (var i=0; i<22; i++) {
            var items = []
            for (var j=0; j<20; j++)
                items.push({id:"synthetic-"+i+"-"+j, title:"Synthetic "+i+"/"+j,
                            cover:"", imdbRating:"8.0", releaseInfo:"2024"})
            out.push({key:i===0?"top-10":"fixture-"+i, title:"Shelf "+i,
                      placement:i*10, ranked:i===0, sourceKind:"house",
                      sourceLabel:"Colosseum", items:items,
                      seeAllPin:{pageKey:"movies", sourceKind:"house",rowKey:"fixture-"+i}})
        }
        return out
    }
    function fakeLoader(key, options, push) {
        loaderCalls++
        lastPush = push
        if (options.onFactsController) options.onFactsController(factsController)
        return function() { canceled++ }
    }
    function publish(r) {
        h.rows = r
        var t = Date.now()
        page._push({generation:page.generation, loading:false, error:"", rows:r})
        console.log("CONSTRUCTION_PUSH durationMs="+(Date.now()-t)+" rows="+r.length)
    }
    function shelfAt(i) { return page.mainShelfAt(i) }
    function controlsAt(i) {
        var shelf=shelfAt(i)
        if (!shelf || !shelf.parent) return null
        var row=shelf.parent
        for (var j=0;j<row.children.length;j++) {
            var child=row.children[j]
            if (child && child.rowKey === page.mainRows[i].key && child.moveUpRequested)
                return child
        }
        return null
    }
    function mountedRail(shelf) {
        if (!shelf) return null
        for (var j=0;j<shelf.children.length;j++) {
            var loader=shelf.children[j]
            if (loader && loader.item && loader.item.visibleItems !== undefined)
                return loader.item
        }
        return null
    }
    function cardsWithin(obj) {
        var count=0
        function walk(node, depth) {
            if (!node || depth>18) return
            try {
                if (node.objectName && String(node.objectName).indexOf("theatreCatalogCard_")===0) count++
                if (node.children) for (var i=0;i<node.children.length;i++) walk(node.children[i],depth+1)
            } catch(e) {}
        }
        walk(obj,0)
        return count
    }
    function positionsValid() {
        var prev=-1
        for (var i=0;i<22;i++) {
            var shelf=shelfAt(i)
            if (!shelf) return false
            var y=shelf.mapToItem(page,0,0).y
            if (y<=prev || shelf.reservedHeight<=0) return false
            prev=y
        }
        return true
    }
    function counts(label) {
        var hosts=0, rails=0, controls=0, posterNodes=0
        for (var i=0;i<22;i++) {
            var shelf=shelfAt(i)
            if (!shelf) continue
            hosts++
            if (mountedRail(shelf)) rails++
            if (controlsAt(i)) controls++
            posterNodes+=cardsWithin(shelf)
        }
        console.log("CONSTRUCTION_COUNTER "+label+" hosts="+hosts+" rails="+rails+
                    " controls="+controls+" posterTreeNodes="+posterNodes+
                    " liveTransitions="+page._liveRev+
                    " positionOrder="+h.positionsValid()+" height="+page.implicitHeight)
        return {hosts:hosts,rails:rails,controls:controls}
    }
    function actionFor(root, name) {
        function walk(obj,depth) {
            if (!obj || depth>12) return null
            if (obj.accessibleName===name && typeof obj.activate==="function") return obj
            if (obj.children)
                for(var i=0;i<obj.children.length;i++) {
                    var found=walk(obj.children[i],depth+1)
                    if (found) return found
                }
            return null
        }
        return walk(root,0)
    }
    function renameInput(root) {
        function walk(obj,depth) {
            if (!obj || depth>12) return null
            if (obj.selectByMouse!==undefined && obj.text!==undefined) return obj
            if(obj.children) for(var i=0;i<obj.children.length;i++) {
                var f=walk(obj.children[i],depth+1)
                if(f) return f
            }
            return null
        }
        return walk(root,0)
    }

    UI.TheatreRowPreferences {
        id: prefs
        settingsLocation: StandardPaths.writableLocation(StandardPaths.TempLocation) +
                          "/theatre_construction_022_fixture.ini"
    }
    UI.TheatreCatalogPage {
        id: page
        width: 1732
        pageKey: "movies"
        active: true
        visualProfile: "gallery"
        viewportTop: 0
        viewportHeight: 984
        catalogLoader: h.fakeLoader
        rowPreferences: prefs
    }
    Loader {
        id: standalone
        active: false
        sourceComponent: Component {
            UI.LazyPosterShelf {
                width: 900
                row: h.fixtures()[0]
                y: 2400
                viewportHeight: 0
            }
        }
    }
    Timer { interval: 90; repeat: true; running: true; onTriggered: h.step() }
    function step() {
        var c, ctrl, act, inp, rail, copy
        switch (phase++) {
        case 0:
            prefs.reset("movies")
            ok(lastPush!==null,"injected catalog did not initialize")
            publish(fixtures())
            c=counts("immediate")
            initialTransitions=page._liveRev
            initialRails=c.rails
            immediatePositionsValid=positionsValid()?1:0
            ok(page.mainRows.length===22 && c.hosts===22,"not 22 real shelf hosts")
            // The original was RED: all 22 rows mount a real rail at unsettled y=0.
            ok(c.rails<=9,"far shelves mounted before real position: "+c.rails)
            ok(page._liveRev<=9,"excess synchronous rail mount transitions: "+page._liveRev)
            ok(!!immediatePositionsValid,"initial Column positions were not committed")
            return
        case 1:
            c=counts("settled")
            ok(positionsValid(),"settled shelf positions not monotonically ordered")
            ok(c.rails>0 && c.rails<10,"settled live rail count out of viewport bounds")
            ok(c.controls===22,"unexpected controls object count before editing")
            for(var i=9;i<22;i++) ok(!shelfAt(i).railLoaded,"far shelf live at "+i)
            settledHeight=page.implicitHeight
            refs=Array.from({length:22},function(_,i){return shelfAt(i)})
            firstRail=mountedRail(shelfAt(0))
            ok(firstRail!==null && firstRail.visibleItems.length===10,
               "first real PosterRail and its 20 fixture cards not available")
            publish(fixtures())
            return
        case 2:
            for(var i=0;i<22;i++) ok(shelfAt(i)===refs[i],"unchanged row QObject identity lost at "+i)
            ok(mountedRail(shelfAt(0))===firstRail,"unchanged mounted PosterRail replaced")
            shelfAt(0).testSetRailContentX(64)
            page.viewportTop=shelfAt(18).mapToItem(page,0,0).y
            return
        case 3:
            c=counts("far-scroll")
            ok(shelfAt(18).railLoaded && mountedRail(shelfAt(18))!==null,
               "scroll into far shelf did not create its real PosterRail")
            ok(!shelfAt(0).railLoaded,"top rail remained live far offscreen")
            ok(c.rails>0 && c.rails<15,"scroll created unbounded rails")
            ok(Math.abs(page.implicitHeight-settledHeight)<1,"vertical reserve moved on scroll")
            page.viewportTop=0
            return
        case 4:
            rail=mountedRail(shelfAt(0))
            var listInfo="none"
            if (rail) for (var q=0;q<rail.children.length;q++) {
                var child=rail.children[q]
                if(child && child.contentWidth!==undefined)
                    listInfo="viewWidth="+child.width+" contentWidth="+child.contentWidth+" restoredFlag="+child._restored+" count="+child.count
            }
            console.log("CONSTRUCTION_HORIZONTAL saved="+shelfAt(0).savedContentX+" restored="+shelfAt(0).restoredContentX+" railX="+(rail?rail.currentContentX:"missing")+" "+listInfo)
            ok(shelfAt(0).railLoaded,"return to top did not remount first rail")
            ok(Math.abs(shelfAt(0).restoredContentX-64)<1,"horizontal offset was not preserved")
            // PosterRail restores actual ListView contentX after its Qt.callLater layout pass.
            // This non-windowed qml.exe harness checks the real rail handoff, not a render-loop claim.
            ok(rail!==null && Math.abs(rail.initialContentX-64)<1,
               "real PosterRail did not receive the stored horizontal offset")
            c=shelfAt(0).reservedHeight
            page.visualProfile="classic"
            return
        case 5:
            ok(shelfAt(0).reservedHeight!==c && shelfAt(0).reservedHeight>0,
               "classic/gallery height tokens collapsed")
            ok(positionsValid(),"classic reflow lost shelf order")
            page.visualProfile="gallery"
            page.editMode=true
            return
        case 6:
            c=counts("edit")
            ok(c.rails>0 && c.rails<15,"edit mode mounted all far shelves")
            for(var i=0;i<22;i++) {
                ctrl=controlsAt(i)
                ok(!!ctrl && ctrl.visible,"real edit controls missing for shelf "+i)
            }
            ctrl=controlsAt(2)
            act=ctrl?actionFor(ctrl,"Rename row"):null
            ok(act!==null,"existing keyboard rename action missing")
            if (act) {
                ok(act.activate(Qt.OtherFocusReason),"rename keyboard action declined activation")
                ok(ctrl.editing,"rename command did not enter input editing")
                inp=renameInput(ctrl)
                ok(inp!==null,"real rename TextInput absent")
                if (inp) inp.text="Renamed Fixture Shelf"
                ctrl.commitEdit()
            }
            return
        case 7:
            ok(page.mainRows[2].title==="Renamed Fixture Shelf","existing rename preferences not applied")
            ctrl=controlsAt(2)
            act=ctrl?actionFor(ctrl,"▼"):null
            ok(act!==null,"existing move-down keyboard action missing")
            if(act) ok(act.activate(Qt.OtherFocusReason),"keyboard move-down did not activate")
            ctrl=controlsAt(page.mainKeys.indexOf("fixture-4"))
            act=ctrl?actionFor(ctrl,"Hide"):null
            ok(act!==null,"existing hide keyboard action missing")
            if(act) ok(act.activate(Qt.OtherFocusReason),"keyboard hide action did not activate")
            return
        case 8:
            ok(page.mainKeys.indexOf("fixture-2")===3,"move-down command did not reorder")
            ok(page.mainRows.some(function(r){return r.key==="fixture-4" && r.hidden===true}),
               "hide command did not preserve hidden row in edit mode")
            prefs.reset("movies")
            page.editMode=false
            standalone.active=true
            return
        case 9:
            ok(standalone.item!==null && standalone.item.railLoaded,
               "unknown viewport standalone fallback did not mount real rail")
            ok(!controlsAt(0).visible,"controls stayed visible after leaving edit mode")
            copy=fixtures()
            copy[0].items=[]
            copy[0].pendingFacts=true
            copy[0].liveFacts=true
            publish(copy)
            return
        case 10:
            ok(shelfAt(0).row.pendingFacts===true,"pending facts source state missing")
            ok(!shelfAt(0)._hasItems,"pending facts rendered unverified items")
            rail=mountedRail(shelfAt(0))
            ok(!rail || !rail.visible || rail.visibleItems.length===0,
               "pending row exposed a poster")
            ok(shelfAt(0).parent.height>0,"pending facts reserve was lost")
            page._updateFactsDemand()
            ok(demandKeys.indexOf("top-10")!==-1,"pending nearby facts not demanded")
            copy=fixtures()
            copy[0].liveFacts=true
            publish(copy)
            return
        case 11:
            ok(mountedRail(shelfAt(0))!==null && mountedRail(shelfAt(0)).visibleItems.length===10,
               "validated facts were not restored to the real rail")
            copy=refs.slice()
            for(var i=1;i<22;i++) ok(shelfAt(i)===copy[i],"pending-facts update replaced unaffected host")
            var oldGen=page.generation
            page.active=false
            page._push({generation:oldGen,loading:false,error:"",rows:[{key:"rogue",items:[]}]})
            ok(page.mainRows.length===22,"stale callback corrupted inactive rows")
            page.remoteCatalogChanged()
            page.active=true
            return
        case 12:
            ok(loaderCalls>=2,"reentry did not request fresh catalog generation")
            publish(fixtures())
            return
        default:
            c=counts("reentered")
            ok(page.mainRows.length===22 && c.hosts===22,"reentry lost shelf descriptors")
            ok(c.rails>0 && c.rails<10,"reentry remounted far shelves")
            ok(page.mainKeys.indexOf("rogue")===-1,"stale generation was admitted")
            console.log("CONSTRUCTION_SUMMARY firstMounts="+initialRails+
                        " firstTransitions="+initialTransitions+
                        " firstPositionsValid="+immediatePositionsValid+
                        " fails="+failures.length)
            if (failures.length) console.log("CONSTRUCTION_FAILS "+failures.join(" | "))
            else console.log("THEATRE_CONSTRUCTION_OK")
            Qt.exit(failures.length?1:0)
        }
    }
}
