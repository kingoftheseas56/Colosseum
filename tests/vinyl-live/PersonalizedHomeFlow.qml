import QtQuick
import "../../qml"

// Production Main and native stores/provider/player. Args: phase, evidence dir,
// optional local WAV fixture. Semantic handler journey; no pointer-input claim.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[2]
    readonly property string evidence: Qt.application.arguments[3]
    property string stage: "profile"
    property int ticks: 0
    property var wanted: phase === "nas" ? ["Nas"] : ["Ilaiyaraaja", "S. P. Balasubrahmanyam"]
    property int pickIndex: 0
    property var queueBefore: []
    property string openedAlbum: ""
    property var listKeys: []
    property real started: 0
    function fail(message) {console.error("VINYL_PERSONALIZED_FAIL",phase,stage,message);cycle.stop();Qt.exit(23);}
    function check(ok,message) {if(!ok)fail(message);return !!ok;}
    function next(value) {stage=value;ticks=0;console.log("VINYL_PERSONALIZED_STAGE",phase,value);}
    function find(item,name) {if(!item)return null;if(item.objectName===name&&item.visible!==false)return item;var children=item.children||[];for(var i=0;i<children.length;i++){var found=find(children[i],name);if(found)return found;}return null;}
    function activate(name) {var item=find(contentItem,name);if(!check(item&&item.enabled,"Missing action "+name))return false;if(typeof item.activate==="function")item.activate(Qt.MouseFocusReason);else item.triggered();return true;}
    function capture(name,target) {next("capture");contentItem.grabToImage(result=>{if(!result.saveToFile(evidence+"/"+phase+"-"+name+".png")){fail("capture");return;}next(target);});}
    function section(service,id) {return (service.homeSections||[]).find(row=>row.id===id);}
    Timer {
        id:cycle;interval:100;repeat:true;running:true
        onTriggered: {
            if(++root.ticks>900){root.fail("Stage deadline");return;}
            if(root.stage==="profile") {
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;var r=w.recommendationService,s=w.session,d=w.worldData;
            if(root.stage==="world"&&root.ticks>5) {
                w.showHome();root.started=Date.now();
                if(root.phase==="skip-restore") {root.next("skip-check");return;}
                if(root.phase==="restore"||root.phase==="inspect") {if(!root.check(r.onboardingState==="answered"&&r.selectedArtists.length===2,"Saved tastes missing"))return;root.next("rows");return;}
                if(!root.check(r.onboardingState==="draft","Fresh Home picker missing"))return;
                if(root.phase==="skip") {root.activate("vinylPickerSkip");root.next("skip-check");return;}
                w.enterDiscoverSearch(true,false);w.setQuery(root.wanted[0]);root.next("search");return;
            }
            if(root.stage==="search") {
                if(r.artistResults.status==="loading")return;
                if(r.artistResults.status==="error"){root.fail(r.artistResults.error);return;}
                var rows=r.artistResults.items||[],at=rows.findIndex(a=>a.name===root.wanted[root.pickIndex]);
                if(at<0)return;
                if(!root.check(rows[at].provider==="youtube-music"&&/^UC[A-Za-z0-9_-]{22}$/.test(rows[at].providerId),"Artist is not a real provider identity"))return;
                root.activate("vinylPickerArtist_"+at);root.pickIndex++;
                if(root.pickIndex<root.wanted.length){w.setQuery(root.wanted[root.pickIndex]);root.ticks=0;return;}
                root.started=Date.now();root.activate("vinylPickerDone");
                if(!root.check(r.onboardingState==="answered"&&r.selectedArtists.length===root.wanted.length,"Done did not commit"))return;
                root.next("rows");return;
            }
            if(root.stage==="rows") {
                var own=root.section(r,"from-your-artists"),related=root.section(r,"related-discoveries");
                if(!own||!own.items.length)return;
                if(!root.check(own.items.length<=12,"Unbounded Home shelf"))return;
                var selected=r.selectedArtists.map(a=>a.providerId),seen={};
                for(var i=0;i<own.items.length;i++) {
                    var item=own.items[i],seed=item.seedArtistRef||{};
                    if(!root.check(selected.indexOf(seed.providerId)>=0,"Unrelated own-artist recommendation"))return;
                    if(!root.check(item.destination.kind==="album"&&item.destination.provider==="youtube-music","Missing real album destination"))return;
                    if(!root.check(item.reasons.some(e=>e.kind==="seed_affinity"&&e.scoreDelta===300),"Seed reason absent"))return;
                    if(!root.check(!seen[item.key],"Duplicate recommendation"))return;seen[item.key]=true;
                }
                var allTastes=selected.every(id=>own.items.some(item=>item.seedArtistRef.providerId===id));
                if(selected.length===2&&!allTastes&&r.homeStatus==="loading")return;
                if(selected.length===2&&!root.check(allTastes,"Mixed tastes lost one artist"))return;
                if(!root.check(!root.section(r,"new-from-your-artists"),"Year-only live shelves incorrectly called new"))return;
                console.log("VINYL_PERSONALIZED_ROWS",root.phase,Date.now()-root.started,JSON.stringify({snapshot:r.homeSnapshot,sections:r.homeSections,selected:r.selectedArtists}));
                if(root.phase==="inspect"){root.next("inspect-ready");return;}
                root.capture("home","see-all");return;
            }
            if(root.stage==="see-all") {
                var row=d.homeRows.find(row=>row.recommendation&&row.sectionId==="from-your-artists");
                if(!root.check(!!row,"Native sections not bound into Home QML"))return;
                d.openList(row.key,true);root.listKeys=d.row(row.key,true).items.map(item=>item.key);
                if(!root.check(w.page==="home","See-all left the Home surface"))return;
                var first=d.row(row.key,true).items[0];root.openedAlbum=first.destination.id;w.activateItem(first);root.next("album");return;
            }
            if(root.stage==="album") {
                if(s.browsing)return;
                if(s.browseError){root.fail(s.browseError);return;}
                if(w.page!=="album"||!s.tracks.length)return;
                if(!root.check((s.shownEdition.sourceAlbum||{}).browseId===root.openedAlbum.slice(10),"Wrong candidate album opened"))return;
                if(!root.check(w.goBack()&&w.page==="home","Album Back lost Home"))return;
                var snap=d.homeListSnapshot;
                if(!root.check(snap&&snap.items.map(item=>item.key).join("|")===root.listKeys.join("|"),"See-all snapshot changed during detail browse"))return;
                w.goBack();w.showHome();
                var related=root.section(r,"related-discoveries");
                if(related&&related.items.length){w.activateItem(related.items[0]);root.next("artist");return;}
                root.next("audio");return;
            }
            if(root.stage==="artist"&&root.ticks>4) {if(!root.check(w.page==="artist","Related artist route failed"))return;w.goBack();if(!root.check(w.page==="home","Artist Back lost Home"))return;root.next("audio");return;}
            if(root.stage==="audio") {
                if(root.phase==="restore"){root.capture("restored","pass");return;}
                var audio=Qt.application.arguments[4];if(!audio){root.next("pass");return;}
                var result=s.importFiles([audio]);if(!root.check(result.imported===1,"Fixture import failed"))return;
                var entry=s.library.find(entry=>entry.payload&&entry.payload.album&&entry.payload.album.origin==="local");
                if(!root.check(!!entry,"Imported Collection album absent"))return;s.openAlbum(entry.payload.album.id);root.next("audio-album");return;
            }
            if(root.stage==="audio-album"&&!s.browsing&&s.tracks.length){s.playTrack(0);root.next("playing");return;}
            if(root.stage==="playing"&&s.loaded&&s.position>0.5) {
                root.queueBefore=s.queue.map(row=>row.queueId);w.showHome();root.started=Date.now();r.refreshHome(true);root.next("refresh-playing");return;
            }
            if(root.stage==="refresh-playing"&&r.homeStatus!=="loading") {
                if(!root.check(s.loaded&&!s.paused&&s.position>1,"Refresh interrupted player"))return;
                if(!root.check(s.queue.map(row=>row.queueId).join("|")===root.queueBefore.join("|"),"Refresh replaced queue"))return;
                if(!root.check(root.section(r,"from-your-artists").items.length>0,"Refresh lost recommendations"))return;
                console.log("VINYL_PERSONALIZED_PLAYBACK_UNCHANGED",root.phase,s.position,Date.now()-root.started);root.capture("playing-home","pass");return;
            }
            if(root.stage==="skip-check") {if(!root.check(r.onboardingState==="answered"&&r.selectedArtists.length===0&&r.homeSections.length===0,"Skip fabricated recommendations"))return;root.next("pass");return;}
            if(root.stage==="inspect-ready"&&root.ticks>60) {
                var scroll=root.find(contentItem,"vinylWorldScroll"),row=root.find(contentItem,"vinylHomeRow_personalized:from-your-artists");
                if(row&&scroll)scroll.contentY=Math.max(0,row.mapToItem(scroll.contentItem,0,0).y-100);
                root.capture("home-open","narrow-start");return;
            }
            if(root.stage==="narrow-start") {root.showNormal();root.width=1000;root.height=800;root.next("narrow-ready");return;}
            if(root.stage==="narrow-ready"&&root.ticks>25) {root.capture("home-narrow","wide-start");return;}
            if(root.stage==="wide-start") {root.showMaximized();root.next("hold");return;}
            if(root.stage==="hold"){console.log("VINYL_PERSONALIZED_PASS",root.phase);cycle.stop();return;}
            if(root.stage==="pass"){console.log("VINYL_PERSONALIZED_PASS",root.phase);cycle.stop();Qt.quit();}
        }
    }
}
