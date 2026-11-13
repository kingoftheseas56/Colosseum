import QtQuick
import "../../qml"

// Actual Main, profile stores, importer, player and recommendation service.
// Args: tagged silent-WAV fixture folder, evidence folder, learn|restore|inspect.
// Uses semantic controls, not a physical pointer qualification.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[4] || "learn"
    property string stage: "profile"
    property int ticks: 0
    property var album: null
    property var ref: ({})
    property var queueIds: []
    property real positionBefore: 0
    property string persistedFeedback: ""
    property var feedbackRoute: null
    property bool feedbackReturnPending: false
    function plain(value) {return JSON.parse(JSON.stringify(value));}
    function check(ok,message) {if(!ok){console.error("VINYL_LEARNING_FAIL",stage,message);cycle.stop();Qt.exit(26);}return !!ok;}
    function next(value) {stage=value;ticks=0;console.log("VINYL_LEARNING_STAGE",phase,value);}
    function find(item,name) {if(!item)return null;if(item.objectName===name&&item.visible!==false)return item;for(var child of item.children||[]){var found=find(child,name);if(found)return found;}return null;}
    function candidate(r) {for(var section of plain(r.homeSections))for(var item of section.items||[])if(item.destination.provider==="local"&&item.destination.providerId===album.id)return item;return null;}
    function card(w,key) {var row=w.worldData.homeRows.find(row=>row.key===key);if(!row)return null;var at=row.items.findIndex(item=>key==="saved-artists"?item.id===album.artistIds[0]:item.destination&&item.destination.providerId===album.id);return at<0?null:find(contentItem,"vinylCard_"+key+"_"+at);}
    function reveal(w,key) {
        var item=card(w,key);if(!check(!!item,"Card missing: "+key))return false;
        var scroll=w.worldScroll,point=item.mapToItem(scroll.contentItem,0,0);
        scroll.contentY=Math.max(0,Math.min(scroll.contentHeight-scroll.height,point.y-85));
        return true;
    }
    function openActions(w,key) {
        if(key!=="saved-artists" && !reveal(w,key))return false;
        feedbackRoute={scroll:w.worldScroll.contentY,queue:w.session.queue.map(row=>row.queueId),
            playingIndex:w.session.playingIndex,generation:w.session.profileGeneration};
        if(key!=="saved-artists") {
            card(w,key).forceActiveFocus(Qt.TabFocusReason);
            return check(w.page==="home","Direct badges must stay on Home");
        }
        return check(w.worldData.requestRecommendationPreferences() && w.page==="feedback",
            "Global recommendation preferences did not open");
    }
    function findTypedAction(node,key,ref) {
        if(!node)return null;
        if(node.modelData && node.modelData.key===key && node.modelData.ref
            && ["identity","kind","providerId","itemId"].every(field=>node.modelData.ref[field]===ref[field])
            && typeof node.activate==="function")return node;
        for(var child of node.children||[]) {var found=findTypedAction(child,key,ref);if(found)return found;}
        return null;
    }
    function action(w,key,option) {
        if(option==="like" || option==="hide") {
            var target=card(w,key);
            if(!check(!!target,"Direct feedback card missing"))return false;
            target.forceActiveFocus(Qt.TabFocusReason);
            if(!check(["identity","kind","providerId","itemId"].every(field=>target.item.ref[field]===root.ref[field]),
                "Direct badge changed typed target identity"))return false;
            var badge=find(target,(option==="like" ? "vinylRecommendationLike_" : "vinylRecommendationHide_")+target.item.id);
            if(!check(badge && badge.activate(Qt.MouseFocusReason),"Direct feedback badge missing: "+option))return false;
            feedbackReturnPending=true;
            return true;
        }
        if(!w.recommendationPage)return false;
        var request=w.feedbackRequest,options=request.options
            || (request.entries||[]).reduce((all,entry)=>all.concat(entry.options||[]),[]);
        var entry=options.find(item=>item.key===option && item.ref.itemId===root.ref.itemId);
        if(!check(entry && request.generation===feedbackRoute.generation,"Missing or stale typed action"))return false;
        if(!check(entry.ref && entry.ref.identity && entry.ref.kind && entry.ref.itemId,
            "Recommendation action lost typed identity"))return false;
        if(["like","hide","unhide"].indexOf(entry.action)>=0
            && !check(JSON.stringify(plain(entry.ref))===JSON.stringify(plain(root.ref)),"Action changed target identity"))return false;
        var button=w.recommendationPage.actionFor(option);
        if(!button || !button.modelData || button.modelData.ref.itemId!==entry.ref.itemId)
            button=findTypedAction(w.recommendationPage,option,entry.ref);
        if(!check(button&&button.activate(Qt.MouseFocusReason),"Missing feedback action: "+option))return false;
        feedbackReturnPending=true;
        return true;
    }
    function capture(name,target) {next("capture");contentItem.grabToImage(result=>{result.saveToFile(Qt.application.arguments[3]+"/"+name+".png");next(target);});}
    function logSnapshot(r,label) {console.log("VINYL_LEARNING_EVIDENCE",label,JSON.stringify({home:r.homeSnapshot,history:r.historySnapshot,feedback:ProfilePreferences.vinylFeedbackSnapshot,exposure:ProfilePreferences.vinylRecommendationExposureSnapshot}));}
    Timer {
        id: cycle;interval:100;repeat:true;running:true
        onTriggered: {
            if(++root.ticks>1200){root.check(false,"Stage deadline");return;}
            if(root.stage==="profile") {
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.session,r=w.recommendationService,d=w.worldData;
            if(root.feedbackReturnPending) {
                if(!root.check(w.page==="home","Recommendation action did not return Home"))return;
                if(!root.check(Math.abs(w.worldScroll.contentY-root.feedbackRoute.scroll)<1,"Feedback changed Home scroll"))return;
                if(!root.check(s.playingIndex===root.feedbackRoute.playingIndex
                    && s.queue.map(row=>row.queueId).join("|")===root.feedbackRoute.queue.join("|"),
                    "Feedback changed playback or queue"))return;
                root.feedbackReturnPending=false;
            }
            if(root.stage==="world"&&root.ticks>5) {
                w.showHome();
                if(root.phase==="learn") {
                    if(r.onboardingState==="draft")d.skipPicker();
                    w.importLocalFiles([Qt.application.arguments[2]+"/01-Manual listening.wav",
                        Qt.application.arguments[2]+"/02-Natural ending.wav",Qt.application.arguments[2]+"/03-Album continuation.wav"]);
                    root.next("import");return;
                }
                root.album=s.library.map(entry=>entry.payload.album).find(album=>album.title==="Qualified History Fixture");
                if(!root.check(root.album&&root.album.tracks.length===3,"Persisted tagged album missing"))return;
                if(!root.check(ProfilePreferences.vinylFeedbackSnapshot.length>0,"Explicit feedback did not persist"))return;
                root.next("restored");return;
            }
            if(root.stage==="import") {
                if(w.importing)return;
                root.album=s.library.map(entry=>entry.payload.album).find(album=>album.title==="Qualified History Fixture");
                if(!root.album)return;
                if(!root.check(root.album.tracks.length===3&&root.album.artistIds.length>0,"Tagged fixture grouping/identity failed"))return;
                w.openAlbum(root.album.id);root.next("save");return;
            }
            if(root.stage==="save") {
                if(s.browsing||w.page!=="album")return;
                if(!s.saved)s.toggleSaved();
                w.showArtist({id:root.album.artistIds[0],name:root.album.artists[0]});root.next("artist");return;
            }
            if(root.stage==="artist"&&root.ticks>3) {
                if(!w.artistData.saved&&!root.check(w.artistData.toggleSaved(),"Save local artist failed"))return;
                w.showHome();root.next("home");return;
            }
            if(root.stage==="home") {
                var item=root.candidate(r);if(!item)return;
                root.ref=plain(item.ref);
                if(!root.check(item.reasons.some(reason=>reason.kind==="seed_affinity"),"Saved artist affinity absent"))return;
                if(root.ticks===3)root.reveal(w,"personalized:from-your-artists");
                if(root.ticks<8)return;
                if(!root.check(ProfilePreferences.vinylRecommendationExposureSnapshot.length>0,"Visible Home card did not record exposure"))return;
                root.openActions(w,"personalized:from-your-artists");root.capture("learning-feedback","like");return;
            }
            if(root.stage==="like") {if(!root.action(w,"personalized:from-your-artists","like"))return;root.next("liked");return;}
            if(root.stage==="liked") {
                if(!root.check(root.candidate(r).reasons.some(reason=>reason.kind==="explicit_positive"&&reason.scoreDelta===1000),"Like did not affect ranking"))return;
                root.openActions(w,"personalized:from-your-artists");root.next("hide");return;
            }
            if(root.stage==="hide") {if(!root.action(w,"personalized:from-your-artists","hide"))return;root.next("hidden");return;}
            if(root.stage==="hidden"&&root.ticks>3) {
                if(!root.check(!root.candidate(r),"Hidden item remained recommended"))return;
                root.openActions(w,"saved-artists");root.next("unhide");return;
            }
            if(root.stage==="unhide") {
                if(!w.recommendationPage)return;
                var options=w.feedbackRequest.options
                    || (w.feedbackRequest.entries||[]).reduce((all,entry)=>all.concat(entry.options||[]),[]);
                var undo=options.find(option=>option.action==="unhide"&&option.ref.itemId===root.ref.itemId);
                if(!root.check(!!undo,"Hidden item recovery unreachable"))return;
                if(!root.action(w,"saved-artists",undo.key))return;
                root.next("recovered");return;
            }
            if(root.stage==="recovered") {
                if(!root.check(!!root.candidate(r),"Unhide did not restore candidate"))return;
                root.persistedFeedback=ProfilePreferences.vinylFeedbackSnapshot;
                w.openAlbum(root.album.id);root.next("play");return;
            }
            if(root.stage==="play") {if(s.browsing||!s.tracks.length)return;s.playTrack(0);root.next("seek");return;}
            if(root.stage==="seek") {
                if(!s.loaded||s.position<1)return;
                if(!root.check(r.homeSnapshot.qualifiedManualSessions===0,"Start alone counted as qualified listening"))return;
                s.seek(45);root.next("seeked");return;
            }
            if(root.stage==="seeked") {
                if(s.seekPending||s.position<44)return;
                if(!root.check(r.homeSnapshot.qualifiedManualSessions===0,"Seek position counted as qualified listening"))return;
                s.seek(0);root.next("qualified");return;
            }
            if(root.stage==="qualified") {
                if(!s.loaded||s.position<12.5||r.homeSnapshot.qualifiedManualSessions<1)return;
                if(!root.check(r.historySnapshot.sessions.some(session=>session.qualifiedMs>=10000),"Player did not produce qualified Activity"))return;
                if(!root.check(root.candidate(r).reasons.some(reason=>reason.kind==="play_count"),"Qualified history did not affect album rank"))return;
                root.logSnapshot(r,"qualified");
                s.next(1);root.next("skip");return;
            }
            if(root.stage==="skip") {
                if(s.queueIndex!==1||!s.loaded)return;
                if(!root.check(r.historySnapshot.sessions.some(session=>session.outcome==="skip"),"Manual Next did not retain explicit skip"))return;
                if(!root.check(!root.candidate(r).reasons.some(reason=>reason.kind==="early_skip"),"One skip received negative ranking"))return;
                root.next("natural");return;
            }
            if(root.stage==="natural") {
                if(s.queueIndex!==2||!s.loaded||s.position<1)return;
                if(!root.check(r.historySnapshot.sessions.some(session=>session.outcome==="natural-eof"&&session.qualifiedMs>=10000),"Natural ending was not qualified distinctly"))return;
                if(!root.check(root.candidate(r).reasons.some(reason=>reason.kind==="recent_completion"),"Natural completion did not affect ranking"))return;
                root.logSnapshot(r,"natural-completion");
                root.next("continuation");return;
            }
            if(root.stage==="continuation") {
                if(s.position<12.5)return;
                if(!r.historySnapshot.sessions.some(session=>session.origin==="album-continuation"&&session.qualifiedMs>=10000))return;
                if(!root.check(r.homeSnapshot.qualifiedManualSessions===2,"Automatic album continuation counted as manual start"))return;
                if(!root.check(r.historySnapshot.sessions.some(session=>session.origin==="album-continuation"&&session.qualifiedMs>=10000),"Continuation origin missing"))return;
                root.queueIds=s.queue.map(row=>row.queueId);root.positionBefore=s.position;
                ProfilePreferences.keepActivityHistory=false;root.next("history-off");return;
            }
            if(root.stage==="history-off"&&root.ticks>15) {
                if(!root.check(r.homeSnapshot.qualifiedManualSessions===0&&r.homeSnapshot.seedMultiplier===1
                    &&!ProfilePreferences.vinylRecommendationExposureSnapshot,"History-off retained derived features/exposure"))return;
                if(!root.check(s.loaded&&!s.paused&&s.position>root.positionBefore&&s.queue.map(row=>row.queueId).join("|")===root.queueIds.join("|"),"Privacy toggle interrupted player/queue"))return;
                if(!root.check(ProfilePreferences.vinylFeedbackSnapshot===root.persistedFeedback,"History toggle changed explicit feedback"))return;
                root.logSnapshot(r,"history-off");
                ProfilePreferences.keepActivityHistory=true;root.next("clear");return;
            }
            if(root.stage==="clear"&&root.ticks>3) {
                if(!root.check(ProfileConsumptionHistory.clearAll(),"Existing history clear failed"))return;
                root.next("cleared");return;
            }
            if(root.stage==="cleared"&&root.ticks>2) {
                if(!root.check(r.homeSnapshot.qualifiedManualSessions===0&&r.homeSnapshot.exposureCount===0,"Clear left derived history/exposure"))return;
                if(!root.check(ProfilePreferences.vinylFeedbackSnapshot===root.persistedFeedback,"Clear erased explicit feedback"))return;
                s.paused=true;w.showHome();root.next("cleared-home");return;
            }
            if(root.stage==="cleared-home") {
                if(root.ticks===3)root.reveal(w,"personalized:from-your-artists");
                if(root.ticks<8)return;
                root.logSnapshot(r,"final");root.capture("learning-home","pass");return;
            }
            if(root.stage==="restored") {
                if(!root.candidate(r))return;
                if(root.ticks===3)root.reveal(w,"personalized:from-your-artists");
                if(root.ticks<8)return;
                if(!root.check(root.candidate(r).reasons.some(reason=>reason.kind==="explicit_positive"),"Restart lost learned explicit preference"))return;
                if(!root.check(r.homeSnapshot.qualifiedManualSessions===0,"Restart resurrected cleared behavior"))return;
                root.logSnapshot(r,"restored");root.capture("learning-restored",root.phase==="inspect"?"hold":"pass");return;
            }
            if(root.stage==="hold"){console.log("VINYL_LEARNING_PASS",root.phase);cycle.stop();return;}
            if(root.stage==="pass"){console.log("VINYL_LEARNING_PASS",root.phase);cycle.stop();Qt.quit();}
        }
    }
}
