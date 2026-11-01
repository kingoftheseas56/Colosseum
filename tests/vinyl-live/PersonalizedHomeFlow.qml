import QtQuick
import "../../qml"

// Production Main and native stores/provider/player. Args: phase, evidence dir,
// optional local WAV fixture. Semantic handler journey; no pointer-input claim.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[2]
    readonly property string evidence: Qt.application.arguments[3]
    readonly property bool qualification: phase.indexOf("qualify-")===0
    readonly property bool restoring: phase==="restore" || phase.indexOf("qualify-restore-")===0
    readonly property bool inspection: phase.indexOf("inspect")===0
    property string stage: "profile"
    property int ticks: 0
    property var wanted: phase.indexOf("john")>=0 ? ["John Williams","John Williams"]
        : phase.indexOf("cross")>=0 ? ["Nas","Ilaiyaraaja","S. P. Balasubrahmanyam"]
        : phase.indexOf("nas")>=0 ? ["Nas"] : ["Ilaiyaraaja", "S. P. Balasubrahmanyam"]
    property int pickIndex: 0
    property var queueBefore: []
    property string openedAlbum: ""
    property var listKeys: []
    property real started: 0
    property real worldReadyAt: 0
    property bool measuredRender: false
    property bool measuredRows: false
    property var firstRef: ({})
    property string feedbackBefore: ""
    property var stableHomeKeys: []
    function metric(event,elapsed,extra) {console.log("VINYL_HOME_METRIC",JSON.stringify(Object.assign({phase:phase,event:event,elapsedMs:elapsed},extra||{})));}
    function fail(message) {
        if(vinylWorld)console.error("VINYL_HOME_FAILURE_STATE",JSON.stringify({status:vinylWorld.recommendationService.homeStatus,error:vinylWorld.recommendationService.homeError,snapshot:vinylWorld.recommendationService.homeSnapshot,sections:vinylWorld.recommendationService.homeSections}));
        console.error("VINYL_PERSONALIZED_FAIL",phase,stage,message);cycle.stop();Qt.exit(23);
    }
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
            if(!root.worldReadyAt)root.worldReadyAt=Date.now();
            if(!root.measuredRender) {
                root.measuredRender=true;
                contentItem.grabToImage(function(result){root.metric("foreground-render",Date.now()-root.worldReadyAt,{width:root.width,height:root.height});});
            }
            var measuredOwn=root.section(r,"from-your-artists");
            if(!root.measuredRows&&measuredOwn&&measuredOwn.items.length) {
                root.measuredRows=true;root.metric("first-personalized-cards",Date.now()-root.worldReadyAt,{count:measuredOwn.items.length,status:r.homeStatus});
            }
            if(root.stage==="world"&&root.ticks>5) {
                w.showHome();root.started=Date.now();
                if(root.phase==="skip-restore") {root.next("skip-check");return;}
                if(root.restoring||root.inspection) {
                    if(!root.check(r.onboardingState==="answered"&&r.selectedArtists.length===root.wanted.length,"Saved tastes missing"))return;
                    if(root.qualification&&!root.check(s.queue.length>=2&&s.queueIndex===1,"Saved playback occurrence missing"))return;
                    if(root.restoring)root.metric("restored-playback",Date.now()-root.worldReadyAt,{queue:s.queue.map(row=>row.queueId),index:s.queueIndex,paused:s.paused,position:s.position});
                    root.next("rows");return;
                }
                if(!root.check(r.onboardingState==="draft","Fresh Home picker missing"))return;
                if(root.phase==="skip") {root.activate("vinylPickerSkip");root.next("skip-check");return;}
                if(root.phase.indexOf("history-off")>=0)ProfilePreferences.keepActivityHistory=false;
                var search=root.find(contentItem,"topBarSearch_Vinyl");
                if(!root.check(search&&search.visible,"Shared top search icon missing"))return;
                search.clicked();w.setQuery("not-the-final-artist-query");w.setQuery(root.wanted[0]);root.next("search");return;
            }
            if(root.stage==="search") {
                if(r.artistResults.status==="loading")return;
                if(r.artistResults.status==="error"){root.fail(r.artistResults.error);return;}
                var chosen=r.draftArtists.map(a=>a.providerId);
                var rows=r.artistResults.items||[],at=rows.findIndex(a=>a.name===root.wanted[root.pickIndex]&&chosen.indexOf(a.providerId)<0);
                if(at<0)return;
                if(!root.check(rows[at].provider==="youtube-music"&&/^UC[A-Za-z0-9_-]{22}$/.test(rows[at].providerId),"Artist is not a real provider identity"))return;
                if(root.phase.indexOf("john")>=0&&!root.check(["UCfv8TEQ44ej9uPGF-SNsDBA","UCZH8NkgXeliBVbpaYY2PoNQ"].indexOf(rows[at].providerId)>=0,"Same-name source identity changed; inspect actual result"))return;
                root.activate("vinylPickerArtist_"+at);root.pickIndex++;
                if(root.pickIndex<root.wanted.length){w.setQuery(root.wanted[root.pickIndex]);root.ticks=0;return;}
                root.started=Date.now();root.activate("vinylPickerDone");
                if(!root.check(r.onboardingState==="answered"&&r.selectedArtists.length===root.wanted.length,"Done did not commit"))return;
                root.next(root.phase.indexOf("retire")>=0?"retire":"rows");return;
            }
            if(root.stage==="retire") {
                // Retire the actual Vinyl session while real catalogue work is pending.
                r.beginTasteEdit();r.searchArtists("John Williams",{});s.clearProfile();
                root.next("retired");return;
            }
            if(root.stage==="retired"&&root.ticks>90) {
                if(!root.check(!s.profileActive&&r.homeSections.length===0&&r.selectedArtists.length===0
                    &&r.draftArtists.length===0&&s.queue.length===0&&d.homeRows.length===0,"Retired profile accepted late derived state"))return;
                root.next("pass");return;
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
                var allTastes=selected.every(id=>own.items.some(item=>item.seedArtistRef.providerId===id
                    || item.reasons.some(reason=>reason.kind==="seed_affinity"&&reason.seed&&reason.seed.itemId===id)));
                if(selected.length>1&&!allTastes&&r.homeStatus==="loading")return;
                if(selected.length>1&&!root.check(allTastes,"Mixed tastes lost one artist"))return;
                var newRow=root.section(r,"new-from-your-artists");
                if(newRow&&!root.check(newRow.items.every(item=>item.releaseDate&&/^\d{4}-\d{2}-\d{2}$/.test(item.releaseDate)),"Undated live shelf incorrectly called new"))return;
                console.log("VINYL_PERSONALIZED_ROWS",root.phase,Date.now()-root.started,JSON.stringify({snapshot:r.homeSnapshot,sections:r.homeSections,selected:r.selectedArtists}));
                if(root.inspection){root.next("inspect-ready");return;}
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
                if(root.restoring){root.capture("restored","pass");return;}
                var audio=Qt.application.arguments[4];if(!audio){root.next("pass");return;}
                var paths=root.qualification ? [audio+"/01-Manual listening.wav",audio+"/02-Natural ending.wav",audio+"/03-Album continuation.wav"] : [audio];
                var result=s.importFiles(paths);if(!root.check(result.imported===paths.length,"Fixture import failed"))return;
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
                root.metric("forced-background-complete",Date.now()-root.started,{status:r.homeStatus,error:r.homeError});
                if(!root.check(Date.now()-root.started<8800,"Background refresh exceeded eight-second budget plus polling allowance"))return;
                console.log("VINYL_PERSONALIZED_PLAYBACK_UNCHANGED",root.phase,s.position,Date.now()-root.started);
                root.capture("playing-home",root.qualification?"qualified-song":"pass");return;
            }
            if(root.stage==="qualified-song") {
                w.showSong();if(!root.check(w.page==="song","Current song route failed"))return;
                s.queueRequested();root.next("qualified-queue");return;
            }
            if(root.stage==="qualified-queue"&&root.ticks>3) {
                if(!root.check(s.queue.length>=2,"Fixture album queue too short"))return;
                if(!root.activate("vinylQueueNext"))return;root.next("qualified-next");return;
            }
            if(root.stage==="qualified-next") {
                if(!s.loaded||s.queueIndex!==1||s.position<0.3)return;
                if(!root.activate("vinylQueueClose"))return;
                if(!root.check(w.goBack()&&w.page==="home","Song/Queue Back lost Home"))return;
                s.paused=true;s.snapshot();root.queueBefore=s.queue.map(row=>row.queueId);
                root.stableHomeKeys=root.section(r,"from-your-artists").items.map(item=>item.key);
                root.next("qualified-visible");return;
            }
            if(root.stage==="qualified-visible"&&root.ticks>15) {
                if(!root.check(root.section(r,"from-your-artists").items.map(item=>item.key).join("|")===root.stableHomeKeys.join("|"),"Visible exposure moved the settled shelf"))return;
                root.feedbackBefore=ProfilePreferences.vinylFeedbackSnapshot;
                root.firstRef=JSON.parse(JSON.stringify(root.section(r,"from-your-artists").items[0].ref));
                if(!root.check(d.setRecommendationFeedback(root.firstRef,"hide",s.profileGeneration),"Hide rejected"))return;
                root.next("qualified-hidden");return;
            }
            if(root.stage==="qualified-hidden"&&root.ticks>3) {
                var hiddenKey=JSON.stringify(root.firstRef);
                if(!root.check(!root.section(r,"from-your-artists").items.some(item=>JSON.stringify(item.ref)===hiddenKey),"Hidden item remained"))return;
                if(!root.check(d.setRecommendationFeedback(root.firstRef,"unhide",s.profileGeneration),"Unhide rejected"))return;
                root.next("qualified-exclude");return;
            }
            if(root.stage==="qualified-exclude"&&root.ticks>3) {
                if(!root.check(root.section(r,"from-your-artists").items.some(item=>JSON.stringify(item.ref)===JSON.stringify(root.firstRef)),"Unhide did not restore item"))return;
                var artistRef=d.recommendationArtistRef(r.selectedArtists[0]);
                if(!root.check(d.setRecommendationFeedback(artistRef,"exclude-artist",s.profileGeneration),"Exclude artist rejected"))return;
                root.next("qualified-excluded");return;
            }
            if(root.stage==="qualified-excluded"&&root.ticks>3) {
                var excludedId=r.selectedArtists[0].providerId,remaining=root.section(r,"from-your-artists");
                if(!root.check(!remaining||!remaining.items.some(item=>(item.artistRefs||[]).some(credit=>credit.providerId===excludedId)),"Excluded implicit owner remained"))return;
                if(!root.check((r.homeSnapshot.suppressedItems||[]).some(item=>(item.excludedArtistRefs||[]).some(credit=>credit.ref.itemId===excludedId)),"Excluded own release has no recovery metadata"))return;
                if(!root.check(d.setRecommendationFeedback(d.recommendationArtistRef(r.selectedArtists[0]),"allow-artist",s.profileGeneration),"Reallow artist rejected"))return;
                root.next("qualified-edit");return;
            }
            if(root.stage==="qualified-edit"&&root.ticks>3) {
                if(!root.check(root.section(r,"from-your-artists").items.length>0,"Reallow did not restore own releases"))return;
                d.beginTasteEdit();
                if(!root.check(r.onboardingState==="editing","Edit did not open draft"))return;
                r.toggleDraftArtist(r.draftArtists[0]);d.cancelTasteEdit();
                if(!root.check(r.selectedArtists.length===root.wanted.length,"Cancel erased tastes"))return;
                d.beginTasteEdit();if(!root.activate("vinylPickerDone"))return;
                root.next("qualified-final");return;
            }
            if(root.stage==="qualified-final"&&root.ticks>5) {
                if(!root.check(s.queue.map(row=>row.queueId).join("|")===root.queueBefore.join("|")&&s.queueIndex===1,"Feedback/edit changed queue"))return;
                if(root.phase.indexOf("history-off")>=0&&!root.check(r.homeSnapshot.qualifiedManualSessions===0&&r.homeSnapshot.exposureCount===0,"History-disabled profile retained behavioral evidence"))return;
                root.metric("final-identity-state",0,{artists:r.selectedArtists.map(a=>({name:a.name,id:a.providerId,mapping:a.mappingStatus})),queue:s.queue.map(row=>row.queueId),index:s.queueIndex});
                root.next("pass");return;
            }
            if(root.stage==="skip-check") {if(!root.check(r.onboardingState==="answered"&&r.selectedArtists.length===0&&r.homeSections.length===0,"Skip fabricated recommendations"))return;root.next("pass");return;}
            if(root.stage==="inspect-ready"&&root.ticks>60) {root.showMaximized();root.next("inspect-wide");return;}
            if(root.stage==="inspect-wide"&&root.ticks>8) {
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
