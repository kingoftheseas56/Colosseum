import QtQuick
import "../../qml"

// Production Main/native player/stores. Args: music folder, evidence directory,
// record|restore|inspect. Use one disposable profile across the three launches.
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property var album: null
    property string activeId: ""
    property var playerBefore: null
    property var order: []
    property real savedPosition: 0
    property string playlistId: ""
    property string expectedId: ""
    readonly property string phase: Qt.application.arguments[4] || "record"
    function item(parent,name) {
        if(!parent)return null;if(parent.objectName===name && parent.visible!==false)return parent;
        var children=parent.children || [];
        for(var i=0;i<children.length;i++){var found=item(children[i],name);if(found)return found;}
        return null;
    }
    function control(name){return item(contentItem,name);}
    function check(ok,message){if(!ok){console.error("VINYL_QUEUE_FAIL",stage,message);Qt.exit(16);}return !!ok;}
    function next(value){stage=value;ticks=0;console.log("VINYL_QUEUE_STAGE",value);}
    function activate(name){var c=control(name);if(!check(c && c.enabled,"Missing action: "+name))return false;if(typeof c.activate==="function")c.activate(Qt.MouseFocusReason);else if(typeof c.triggered==="function")c.triggered();else return check(false,"No action handler: "+name);return true;}
    function ids(s){return s.queue.map(r=>r.queueId);}
    function capture(name,target){next("capture");contentItem.grabToImage(function(result){result.saveToFile(Qt.application.arguments[3]+"/"+name+".png");root.next(target);});}
    Timer {
        interval:180;repeat:true;running:true
        onTriggered: {
            if(++root.ticks>700){root.check(false,"Stage timed out");return;}
            if(root.stage==="profile"){
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;
            var s=w.session,p=w.personalLibrary;
            if(root.stage==="world" && root.ticks>6){
                if(root.phase!=="record"){
                    if(!root.check(s.library.length===1,"Tagged library did not persist"))return;
                    root.album=s.library[0].payload.album;
                    if(root.phase==="inspect"){
                        if(!root.check(!s.queue.length && s.player===null,"Cleared queue resurrected after restart"))return;
                        w.openAlbum(root.album.id);root.next("inspect-album");return;
                    }
                    var saved=s.progressStore.get("music-session","vinyl").resume;
                    if(!root.check(s.queue.length===3 && s.paused && !s.player && s.repeatMode==="one" && s.position>=5.5 && s.queue[0].trackId===s.queue[1].trackId && new Set(root.ids(s)).size===3 && root.ids(s).join(",")===saved.rows.map(r=>r.queueId).join(","),"Queue occurrence/order/position/repeat restore failed"))return;
                    console.log("VINYL_QUEUE_RESTORE",JSON.stringify({ids:root.ids(s),position:s.position,repeat:s.repeatMode}));
                    root.activate("vinylBarPlay");root.next("restored-playing");return;
                }
                s.clearQueue();w.showLibrary();root.activate("vinylLibraryImportFolder");root.next("folder");return;
            }
            if(root.stage==="folder" && root.ticks>3){
                var dialog=null;for(var i=0;i<w.data.length;i++)if(w.data[i].objectName==="vinylOpenFolderDialog")dialog=w.data[i];
                if(!root.check(dialog && dialog.visible,"Import folder dialog unavailable"))return;
                // The OS picker cannot be piloted by this semantic driver.
                // Supply the known fixture to its existing acceptance handler.
                dialog.close();w.importLocalFiles([Qt.application.arguments[2]]);root.next("imported");return;
            }
            if(root.stage==="imported"){
                if(s.importing)return;
                if(!root.check(s.library.length===1,"Expected one tagged multidisc album: "+w.importStatus))return;
                root.album=s.library[0].payload.album;w.openAlbum(root.album.id);root.next("album");return;
            }
            if(root.stage==="album" && root.ticks>3){
                if(s.browsing)return;
                console.log("VINYL_QUEUE_ALBUM",JSON.stringify({title:root.album.title,tracks:root.album.tracks.length,editions:(root.album.editions || []).map(e=>({id:e.id,tracks:e.tracks.length,discs:e.tracks.map(t=>t.disc)})),shown:s.shownEditionId,discs:s.shownEdition.discNumbers,shownTracks:s.tracks.length}));
                if(!root.check(s.tracks.length>=3,"Tagged album missing"))return;
                root.activate("vinylAlbumNext");
                if(!root.check(s.queue.length===root.album.tracks.length && s.queueIndex===0 && s.paused && !s.player,"Queue into empty did not select a paused accessible occurrence"))return;
                root.activate("vinylBarQueue");root.next("start-queue");return;
            }
            if(root.stage==="start-queue" && root.ticks>3){root.activate("vinylQueuePlay_0");root.activate("vinylQueueClose");root.next("playing");return;}
            if(root.stage==="playing"){
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<1)return;
                if(!root.check(s.sourceKind==="local" && s.playingTrack.id===root.album.tracks[0].id && s.player.getProperty("current-ao")!=="null","Local exact recording/output failed"))return;
                root.activeId=s.queue[s.queueIndex].queueId;root.playerBefore=s.player;
                root.activate("vinylTrackNext_2");root.activate("vinylTrackQueue_1");
                if(!root.check(s.queue[1].trackId===s.tracks[2].id && s.queue[s.queue.length-1].trackId===s.tracks[1].id && new Set(root.ids(s)).size===s.queue.length && s.queue[s.queueIndex].queueId===root.activeId && s.player===root.playerBefore,"Album Next/append replaced playback or lost duplicate identity"))return;
                root.activate("vinylBarPlay");root.next("scrub");return;
            }
            if(root.stage==="scrub" && root.ticks>3){
                var bar=root.control("vinylPlayerBar"),seek=root.control("vinylBarSeek");
                if(!root.check(s.paused && seek.enabled,"Paused timeline is disabled"))return;
                bar.beginScrub(0);bar.moveScrub(seek.width*0.2);bar.commitScrub(seek.width*0.2);root.next("scrubbed");return;
            }
            if(root.stage==="scrubbed"){
                if(s.seekPending || Math.abs(s.position-s.duration*0.2)>0.7)return;
                root.savedPosition=s.position;var bar=root.control("vinylPlayerBar"),seek=root.control("vinylBarSeek");
                bar.beginScrub(seek.width*0.7);bar.cancelScrub();bar.commitScrub(seek.width*0.9);
                if(!root.check(s.paused && w.page==="album" && Math.abs(s.position-root.savedPosition)<0.7,"Cancelled scrub sought or timeline navigated"))return;
                if(s.shownEdition.discs.length>1)root.activate("vinylDisc_1");
                root.expectedId=s.tracks[0].id;root.activate("vinylTrackNext_0");
                if(!root.check(s.queue[s.queueIndex+1].trackId===root.expectedId && s.queue[s.queueIndex+1].discNumber===s.shownEdition.discNumbers[s.shownDisc],"Disc queue context changed"))return;
                root.activate("vinylBarOpenSong");root.next("song");return;
            }
            if(root.stage==="song" && root.ticks>3){
                if(!root.check(w.page==="song" && !w.browsingSong,"Cover did not open current Song"))return;
                root.activate("vinylSongNext");root.activate("vinylSongQueue");
                if(!root.check(s.queue[s.queueIndex+1].trackId===s.playingTrack.id && s.queue[s.queue.length-1].trackId===s.playingTrack.id,"Current Song Next/append failed"))return;
                w.goBack();w.showBrowseSong(root.album,1);root.next("browse-song");return;
            }
            if(root.stage==="browse-song" && root.ticks>3){
                var count=s.queue.length;root.activate("vinylSongNext");root.activate("vinylSongQueue");
                if(!root.check(w.browsingSong && s.queue.length===count+2 && s.queue[s.queueIndex+1].trackId===root.album.tracks[1].id && s.queue[s.queueIndex].queueId===root.activeId,"Browse Song queue actions replaced playback"))return;
                w.goBack();root.activate("vinylBarQueue");root.next("queue-edits");return;
            }
            if(root.stage==="queue-edits" && root.ticks>3){
                root.activate("vinylQueueMore_"+s.queueIndex);root.activate("vinylQueueMenuDown");
                if(!root.check(s.queueIndex===1 && s.queue[1].queueId===root.activeId && s.player===root.playerBefore && s.paused,"Moving current occurrence restarted playback"))return;
                root.activate("vinylQueueMore_0");root.activate("vinylQueueMenuRemove");
                root.order=root.ids(s);root.activate("vinylQueueShuffle");
                if(!root.check(s.shuffleEnabled && s.queue[s.queueIndex].queueId===root.activeId && root.ids(s).slice().sort().join(",")===root.order.slice().sort().join(","),"Shuffle lost active occurrence or rows"))return;
                root.activate("vinylQueueShuffle");
                if(!root.check(!s.shuffleEnabled && root.ids(s).join(",")===root.order.join(","),"Unshuffle lost edited order"))return;
                root.activate("vinylQueueRepeat");root.activate("vinylQueueRepeat");root.activate("vinylQueueRepeat");
                if(!root.check(s.repeatMode==="off","Repeat cycle did not return off"))return;
                root.capture("queue-edits","library-song");return;
            }
            if(root.stage==="library-song"){
                root.activate("vinylQueueClose");s.shownDisc=0;
                p.toggleSong({row:s.shownTrackRow(1)});root.playlistId=p.createPlaylist("Playback queue proof",[s.shownTrackRow(1),s.shownTrackRow(1),s.shownTrackRow(2)]);
                if(!root.check(!!root.playlistId,"Personal playlist creation failed: "+p.personalError))return;
                w.openHomeDestination("library","songs");root.next("saved-song");return;
            }
            if(root.stage==="saved-song" && root.ticks>3){
                var count=s.queue.length;root.activate("vinylLibrarySavedSongNext_0");root.activate("vinylLibrarySavedSongQueue_0");
                if(!root.check(s.queue.length===count+2 && s.queue[s.queueIndex].queueId===root.activeId,"Saved Song queue actions replaced playback"))return;
                p.setSection("playlists");p.openPlaylist(root.playlistId);root.next("playlist");return;
            }
            if(root.stage==="playlist" && root.ticks>3){
                var count=s.queue.length;root.activate("vinylPlaylistNext");root.activate("vinylPlaylistQueue");root.activate("vinylPlaylistItemNext_1");root.activate("vinylPlaylistItemQueue_1");
                if(!root.check(s.queue.length===count+8 && s.queue[s.queueIndex+1].trackId===root.album.tracks[1].id && new Set(root.ids(s)).size===s.queue.length,"Playlist Next/append lost repeated occurrences"))return;
                w.showRecordArtist("Nas",root.album);w.artistData.activeTab="singles";root.next("artist");return;
            }
            if(root.stage==="artist" && root.ticks>5){
                var count=s.queue.length;root.activate("vinylArtistSinglesNext_0");root.activate("vinylArtistSinglesQueue_0");
                if(!root.check(s.queue.length===count+2 && s.queue[s.queueIndex].queueId===root.activeId,"Artist song queue actions replaced playback"))return;
                w.goBack();root.activate("vinylPlaylistPlay");root.next("playlist-playing");return;
            }
            if(root.stage==="playlist-playing"){
                if(s.error){root.check(false,s.error);return;}if(!s.loaded || s.position<0.6)return;
                if(!root.check(s.queue.length===3 && s.queue[0].trackId===s.queue[1].trackId && s.queue[0].queueId!==s.queue[1].queueId,"Playlist Play lost duplicates"))return;
                root.activeId=s.queue[0].queueId;root.expectedId=s.queue[1].queueId;s.seek(s.duration-0.6);root.next("eof-next");return;
            }
            if(root.stage==="eof-next"){
                if(s.queue[s.queueIndex].queueId===root.activeId)return;
                if(!root.check(s.queueIndex===1 && s.queue[1].queueId===root.expectedId,"EOF skipped or retargeted duplicate occurrence"))return;
                if(!s.loaded || s.position<0.3)return;
                root.activate("vinylBarQueue");root.activate("vinylQueueRepeat");root.activate("vinylQueueRepeat");root.activate("vinylQueueClose");
                root.activeId=s.queue[s.queueIndex].queueId;s.seek(s.duration-0.6);root.next("eof-one");return;
            }
            if(root.stage==="eof-one"){
                if(s.seekPending || !s.loaded || s.position>2 || root.ticks<8)return;
                if(!root.check(s.repeatMode==="one" && s.queue[s.queueIndex].queueId===root.activeId,"Repeat one changed occurrence"))return;
                root.activate("vinylBarQueue");root.activate("vinylQueueRepeat");root.activate("vinylQueuePlay_2");root.activate("vinylQueueClose");root.next("last-playing");return;
            }
            if(root.stage==="last-playing"){
                if(!s.loaded || s.position<0.3)return;s.seek(s.duration-0.6);root.next("eof-off");return;
            }
            if(root.stage==="eof-off"){
                if(s.sourceState!=="ended")return;
                if(!root.check(s.paused && s.queueIndex===2 && s.queue.length===3,"Repeat off boundary did not stop"))return;
                root.activate("vinylBarPlay");root.next("restart-ended");return;
            }
            if(root.stage==="restart-ended"){
                if(!s.loaded || s.position<0.5)return;
                if(!root.check(s.queueIndex===2 && s.position<3,"Play after EOF did not restart current occurrence"))return;
                root.activate("vinylBarQueue");root.activate("vinylQueueRepeat");root.activate("vinylQueueClose");s.seek(s.duration-0.6);root.next("eof-all");return;
            }
            if(root.stage==="eof-all"){
                if(s.queueIndex!==0 || !s.loaded || s.position<0.3)return;
                root.activate("vinylBarQueue");root.activate("vinylQueueRepeat");root.activate("vinylQueueClose");s.seek(6);root.next("checkpoint");return;
            }
            if(root.stage==="checkpoint"){
                if(s.seekPending || Math.abs(s.position-6)>1)return;s.paused=true;s.snapshot();
                console.log("VINYL_QUEUE_RECORD",JSON.stringify({ids:root.ids(s),tracks:s.queue.map(r=>r.trackId),position:s.position,repeat:s.repeatMode}));
                root.capture("playlist-checkpoint","finish");return;
            }
            if(root.stage==="restored-playing"){
                if(!s.loaded || s.position<6.5)return;
                root.activate("vinylBarPlay");root.activate("vinylBarNext");
                if(!root.check(s.queueIndex===1 && s.paused,"Paused Next unexpectedly started playback"))return;
                root.activate("vinylBarQueue");root.activate("vinylQueueClear");root.activate("vinylQueueClose");
                if(!root.check(!s.queue.length && s.player===null && s.sourceState==="idle" && !s.shuffleEnabled,"Clear did not retire playback"))return;
                root.next("finish");return;
            }
            if(root.stage==="inspect-album" && root.ticks>4){if(s.browsing)return;root.activate("vinylAlbumQueue");s.paused=true;root.capture("album-open-for-review","finish");return;}
            if(root.stage==="finish" && root.ticks>4){console.log("VINYL_QUEUE_PASS",root.phase);running=false;if(root.phase!=="inspect")Qt.quit();}
        }
    }
}
