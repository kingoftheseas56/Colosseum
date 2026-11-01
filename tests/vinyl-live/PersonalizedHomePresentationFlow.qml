import QtQuick
import "../../qml"

// Production Main/stores/player; args: tagged audio folder, evidence folder,
// local | restore | privacy | public | unavailable-empty | unavailable-cache | inspect.
// Unavailable runs use a missing child Python executable, never a global network toggle.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[4] || "local"
    property string stage: "profile"
    property int ticks: 0
    property var albums: []
    property var queueIds: []
    property real beforePosition: 0
    property var listKeys: []
    property string publicKey: ""
    function plain(v) {return JSON.parse(JSON.stringify(v));}
    function check(ok,message) {if(!ok){console.error("VINYL_HOME_FINISH_FAIL",phase,stage,message);cycle.stop();Qt.exit(28);}return !!ok;}
    function next(value) {stage=value;ticks=0;console.log("VINYL_HOME_FINISH_STAGE",phase,value);}
    function find(item,name) {if(!item)return null;if(item.objectName===name&&item.visible!==false)return item;for(var child of item.children||[]){var found=find(child,name);if(found)return found;}return null;}
    function activate(name) {var control=find(contentItem,name);return check(control&&control.enabled&&control.activate(Qt.MouseFocusReason),"Missing actual control "+name);}
    function section(r,id) {return plain(r.homeSections).find(row=>row.id===id);}
    function reveal(w,key) {
        var row=find(contentItem,"vinylHomeRow_"+key);if(!check(!!row,"Missing rendered row "+key))return;
        var scroll=w.worldScroll,point=row.mapToItem(scroll.contentItem,0,0);
        scroll.contentY=Math.max(0,Math.min(scroll.contentHeight-scroll.height,point.y-80));
    }
    function capture(name,target) {next("capture");contentItem.grabToImage(result=>{if(!check(result.saveToFile(Qt.application.arguments[3]+"/"+phase+"-"+name+".png"),"Capture failed"))return;next(target);});}
    function logHome(r,d) {console.log("VINYL_HOME_FINISH_EVIDENCE",phase,JSON.stringify({snapshot:r.homeSnapshot,rows:d.homeRows,status:d.homeStatus,error:d.homeError}));}
    Timer {
        id:cycle;interval:100;running:true;repeat:true
        onTriggered: {
            if(++root.ticks>1000){root.check(false,"Stage deadline");return;}
            if(root.stage==="profile") {
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.session,r=w.recommendationService,d=w.worldData;
            if(root.stage==="world"&&root.ticks>5) {
                w.showHome();
                if(r.onboardingState==="draft")d.skipPicker();
                if(root.phase==="unavailable-empty"){root.next("empty");return;}
                if(root.phase==="unavailable-cache"){r.refreshHome(true);root.next("cached");return;}
                if(root.phase==="public"){root.capture("loading","public");return;}
                if(root.phase==="local") {
                    w.importLocalFiles([Qt.application.arguments[2]+"/01.wav",Qt.application.arguments[2]+"/02.wav"]);
                    root.next("import");return;
                }
                root.next("rows");return;
            }
            if(root.stage==="import") {
                if(w.importing)return;
                root.albums=s.library.map(entry=>entry.payload.album).filter(album=>["Rediscovery Fixture","Frequent Fixture"].indexOf(album.title)>=0)
                    .sort((a,b)=>a.title==="Rediscovery Fixture"?-1:1);
                if(root.albums.length!==2)return;
                w.openAlbum(root.albums[0].id);root.next("play-a");return;
            }
            if(root.stage==="play-a") {
                if(s.browsing||w.page!=="album")return;
                if(!s.saved)s.toggleSaved();s.playTrack(0);root.next("listen-a");return;
            }
            if(root.stage==="listen-a") {
                if(r.homeSnapshot.qualifiedManualSessions<1)return;
                w.openAlbum(root.albums[1].id);root.next("play-b");return;
            }
            if(root.stage==="play-b") {
                if(s.browsing||s.shownEdition.sourceAlbum.id!==root.albums[1].id)return;
                if(!s.saved)s.toggleSaved();s.playTrack(0);root.next("listen-b");return;
            }
            if(root.stage==="listen-b") {
                if(r.homeSnapshot.qualifiedManualSessions<2)return;
                if(!root.check(w.personalLibrary.toggleSong({row:s.queue[s.queueIndex]}),"Save song failed"))return;
                s.playTrack(0);root.next("replay-b");return;
            }
            if(root.stage==="replay-b") {if(r.homeSnapshot.qualifiedManualSessions<3)return;w.showHome();root.next("rows");return;}
            if(root.stage==="rows") {
                var rediscover=root.section(r,"rediscover-library"),less=root.section(r,"less-played");
                if(!rediscover||!less)return;
                if(!root.check(less.items.length===1&&less.items[0].title==="Rediscovery Fixture","Less played used unknown/incorrect counts"))return;
                if(!root.check(rediscover.items.some(item=>item.kind==="saved-song"),"Saved song missing from rediscovery"))return;
                var keys=d.homeRows.map(row=>row.key);
                if(!root.check(keys.indexOf("saved-albums")<keys.indexOf("personalized:rediscover-library")
                    &&keys.indexOf("personalized:rediscover-library")<keys.indexOf("personalized:less-played"),"Home order incorrect"))return;
                root.logHome(r,d);root.reveal(w,"personalized:rediscover-library");root.next("wide");return;
            }
            if(root.stage==="wide"&&root.ticks>5){root.capture("history-wide","narrow-start");return;}
            if(root.stage==="narrow-start"){root.width=880;root.height=760;root.next("narrow");return;}
            if(root.stage==="narrow"&&root.ticks>5){root.reveal(w,"personalized:less-played");root.capture("history-narrow","wide-start");return;}
            if(root.stage==="wide-start"){root.width=1920;root.height=1080;root.next(root.phase==="inspect"?"hold":"see-all");return;}
            if(root.stage==="see-all"&&root.ticks>4) {
                root.queueIds=s.queue.map(row=>row.queueId);root.beforePosition=s.position;
                d.openList("personalized:less-played",true);root.listKeys=d.row("personalized:less-played",true).items.map(item=>item.key);
                w.activateItem(d.row("personalized:less-played",true).items[0]);root.next("album-back");return;
            }
            if(root.stage==="album-back") {
                if(s.browsing||w.page!=="album")return;
                if(!root.check(w.goBack()&&w.page==="home","Back lost Home"))return;
                if(!root.check(d.row("personalized:less-played",true).items.map(item=>item.key).join("|")===root.listKeys.join("|"),"History See-all snapshot changed"))return;
                if(!root.check(w.goBack(),"See-all Back failed"))return;
                var song=root.section(r,"rediscover-library").items.find(item=>item.kind==="saved-song");
                w.activateItem(song);root.next("song-back");return;
            }
            if(root.stage==="song-back") {
                if(!root.check(w.page==="song","Saved history song did not use existing Song route"))return;
                if(!root.check(w.goBack()&&w.page==="home","Song Back lost Home"))return;
                if(!root.check(s.queue.map(row=>row.queueId).join("|")===root.queueIds.join("|")&&s.position>=root.beforePosition,"Browsing changed playback/queue"))return;
                s.paused=true;root.next(root.phase==="privacy"?"privacy-off":"pass");return;
            }
            if(root.stage==="privacy-off") {
                d.openList("personalized:rediscover-library",true);ProfilePreferences.keepActivityHistory=false;
                root.next("privacy-disabled");return;
            }
            if(root.stage==="privacy-disabled"&&root.ticks>3) {
                if(!root.check(!d.homeListSnapshot&&!root.section(r,"rediscover-library"),"History-off retained private See-all"))return;
                ProfilePreferences.keepActivityHistory=true;root.next("privacy-clear");return;
            }
            if(root.stage==="privacy-clear"&&root.ticks>4) {
                if(!root.section(r,"rediscover-library"))return;
                d.openList("personalized:rediscover-library",true);
                if(!root.check(ProfileConsumptionHistory.clearAll(),"Existing history clear failed"))return;
                root.next("privacy-cleared");return;
            }
            if(root.stage==="privacy-cleared"&&root.ticks>3) {
                if(!root.check(!d.homeListSnapshot&&!root.section(r,"rediscover-library")&&!root.section(r,"less-played"),"Clear retained copied private rows"))return;
                root.next("pass");return;
            }
            if(root.stage==="public") {
                var publicRow=d.homeRows.find(row=>row.personal===false&&row.items.length&&row.kind==="albums");if(!publicRow)return;
                root.publicKey=publicRow.key;d.openList(publicRow.key,true);
                root.listKeys=d.row(publicRow.key,true).items.map(item=>item.id);
                w.activateItem(publicRow.items[0]);root.next("public-album");return;
            }
            if(root.stage==="public-album") {
                if(s.browsing)return;
                if(!root.check(w.page==="album"&&s.tracks.length>0,"Real public album route/tracklist failed: "+s.browseError))return;
                if(!root.check(w.goBack()&&w.page==="home","Public Album Back failed"))return;
                // Exercise a background library change while the public list is
                // retained. The user-facing import command deliberately navigates
                // to Library, so it is not a Back-return test for this route.
                s.importFilesAsync([Qt.application.arguments[2]+"/01.wav"]);root.next("public-import");return;
            }
            if(root.stage==="public-import") {
                if(w.importing)return;
                var local=s.library.map(entry=>entry.payload.album).find(album=>album.title==="Rediscovery Fixture");if(!local)return;
                w.openAlbum(local.id);root.next("public-play");return;
            }
            if(root.stage==="public-play") {if(s.browsing||w.page!=="album")return;s.playTrack(0);root.next("public-back");return;}
            if(root.stage==="public-back") {
                if(!s.loaded||s.position<1)return;
                if(!root.check(w.goBack()&&w.page==="home","Public playback detour lost Home"))return;
                if(!root.check(d.row(root.publicKey,true).items.map(item=>item.id).join("|")===root.listKeys.join("|"),"Public See-all emptied after playback/library changes"))return;
                s.paused=true;root.capture("public-return","pass");return;
            }
            if(root.stage==="empty") {
                if(d.homeStatus!=="error"||s.discoveryLoading)return;
                if(!root.check(!d.homeRows.some(row=>row.items.length),"No-cache failure fabricated cards"))return;
                if(!root.check(!!find(contentItem,"vinylHomeDiscover")&&!!find(contentItem,"vinylHomeLibrary"),"No-cache destinations missing"))return;
                root.logHome(r,d);root.capture("empty","empty-library");return;
            }
            if(root.stage==="empty-library") {if(!root.activate("vinylHomeLibrary"))return;root.next("empty-return");return;}
            if(root.stage==="empty-return") {
                if(!root.check(w.page==="library","Empty Library destination failed"))return;
                w.showHome();root.next("empty-discover");return;
            }
            if(root.stage==="empty-discover"&&root.ticks>4) {if(!root.activate("vinylHomeDiscover"))return;root.next("empty-check");return;}
            if(root.stage==="empty-check") {if(!root.check(w.page==="discover","Empty Discover destination failed"))return;w.showHome();root.next("pass");return;}
            if(root.stage==="cached") {
                if(r.homeStatus==="loading")return;
                var own=root.section(r,"from-your-artists");
                if(!root.check(own&&own.items.length>0&&own.status==="stale","Unavailable provider lost cached personalized cards"))return;
                root.logHome(r,d);root.reveal(w,"personalized:from-your-artists");root.next("cached-capture");return;
            }
            if(root.stage==="cached-capture") {
                if(root.ticks===10)root.reveal(w,"personalized:from-your-artists");
                if(root.ticks<18)return;
                var shelf=find(contentItem,"vinylHomeRow_personalized:from-your-artists");
                var y=shelf.mapToItem(w.worldScroll,0,0).y;
                if(!root.check(y>=50&&y<w.worldScroll.height-40,"Cached shelf outside actual viewport: "+y))return;
                root.capture("cached","pass");return;
            }
            if(root.stage==="hold"){console.log("VINYL_HOME_FINISH_PASS",root.phase);cycle.stop();return;}
            if(root.stage==="pass"){console.log("VINYL_HOME_FINISH_PASS",root.phase);cycle.stop();Qt.quit();}
        }
    }
}
