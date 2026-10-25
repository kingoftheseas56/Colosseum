import QtQuick
import "../../qml"

// Real executable/Main.qml, production native types and profile stores.
// Args: record|restore|isolated, folder, expected title, artist, tracks, discs.
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property var importedAlbum: ({})
    property var vaultIds: []
    property int importTicks: 0
    property int captureTicks: 0
    property real resumeFrom: 0
    property int windowWarmupTicks: 0
    Connections {
        target: root.vinylWorld ? root.vinylWorld.session : null
        function onActivityActiveChanged() {console.log("VINYL_TAGGED_ACTIVITY",root.vinylWorld.session.activityActive,root.vinylWorld.session.position);}
        function onActivitySinkChanged() {console.log("VINYL_TAGGED_ACTIVITY_SINK",root.vinylWorld.session.position);}
    }
    function fail(message) {console.error("VINYL_TAGGED_FAIL",stage,message);Qt.exit(12);}
    function require(value,message) {if(!value){fail(message);return false;}return true;}
    function named(item,name) {
        if(item.objectName===name)return item;
        var children=item.children || [];
        for(var i=0;i<children.length;i++){var found=named(children[i],name);if(found)return found;}
        return null;
    }
    function monthSeconds() {return Number(JSON.parse(JSON.stringify(ProfileActivity.projectMonth(Qt.formatDateTime(new Date(),"yyyy-MM")))).listenSeconds || 0);}
    function capture(name,next) {
        console.log("VINYL_TAGGED_CAPTURE_PAGE",name,vinylWorld.page);
        stage="capturing";
        if(!contentItem.grabToImage(function(result){
            if(!result.saveToFile(Qt.application.arguments[8]+"/"+name+".png")){root.fail("capture "+name);return;}
            console.log("VINYL_TAGGED_CAPTURE_OK",name);root.stage=next;
        }))fail("capture request "+name);
    }
    onStageChanged: console.log("VINYL_TAGGED_STAGE",stage)
    Timer {
        interval:100;repeat:true;running:true
        onTriggered: {
            root.ticks++;if(root.ticks>1200){root.fail("timeout");return;}
            if(root.stage==="profile"){
                if(!root.visible)return;
                if(++root.windowWarmupTicks<4){root.requestActivate();return;}
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                root.openWorld("Vinyl");root.stage="world";return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.session;
            if(s.error){root.fail(s.error);return;}
            if(root.stage==="world"){
                if(Qt.application.arguments[2]==="isolated"){
                    if(!root.require(s.library.length===0 && s.queue.length===0 && s.player===null,"music crossed a profile boundary"))return;
                    console.log("VINYL_TAGGED_ISOLATION_OK");Qt.quit();return;
                }
                if(Qt.application.arguments[2]==="restore"){
                    if(!root.require(s.library.length===1 && s.queue.length===Number(Qt.application.arguments[6]) && s.paused && s.player===null && s.position>=4,"cold restore"))return;
                    if(!root.require(root.monthSeconds()>=10,"listening history did not persist"))return;
                    console.log("VINYL_TAGGED_RESTORE_OK",JSON.stringify({position:s.position,queue:s.queue.length,listenSeconds:root.monthSeconds()}));
                    root.resumeFrom=s.position;
                    w.openAlbum(s.library[0].id);root.stage="restore-album";return;
                }
                if(!root.require(s.library.length===0,"profile must be disposable"))return;
                w.importLocalFiles([Qt.application.arguments[3]]);root.stage="import";return;
            }
            if(root.stage==="import" || root.stage==="partial"){
                root.importTicks++;if(s.importing)return;
                if(!root.require(!s.browseError && !s.pendingImportCeremony,"import failure: "+s.browseError))return;
                if(!root.require(s.library.length===1,"expected one grouped album"))return;
                var album=s.library[0].payload.album, tracks=album.tracks;
                if(!root.require(album.title===Qt.application.arguments[4] && album.artists[0]===Qt.application.arguments[5],"album tags: "+JSON.stringify({title:album.title,artists:album.artists})))return;
                if(!root.require(tracks.length===Number(Qt.application.arguments[6]) && album.discs.length===Number(Qt.application.arguments[7]),"disc/track grouping"))return;
                if(!root.require(!!album.cover && tracks.every(function(t){return t.id.indexOf("vault:")===0 && t.title && t.seconds>0;}),"artwork, tags or duration missing"))return;
                if(root.stage==="partial"){
                    if(!root.require(JSON.stringify(tracks.map(t=>t.id))===JSON.stringify(root.vaultIds),"partial reimport duplicated or retargeted tracks"))return;
                    console.log("VINYL_TAGGED_PARTIAL_REIMPORT_OK");w.openAlbum(album.id);root.stage="album";return;
                }
                root.importedAlbum=album;root.vaultIds=tracks.map(t=>t.id);
                if(!root.require(w.worldData.project([album],"songs").length===tracks.length && w.worldData.project([album],"artists").length===1,"library projections"))return;
                w.worldData.query=tracks[1].title;
                if(!root.require(w.worldData.albums.length===1,"track search does not find its album"))return;
                w.worldData.query="no-such-local-title-for-m1";
                if(!root.require(w.worldData.albums.length===0,"search empty state"))return;
                w.worldData.query="";
                console.log("VINYL_TAGGED_IMPORT_OK",JSON.stringify({title:album.title,artist:album.artists[0],tracks:tracks.length,discs:album.discs.length,declaredDiscs:album.discTotal,cover:album.cover,importTicks:root.importTicks,status:w.importStatus}));
                w.importLocalFiles([tracks[0].localPath]);root.stage="partial";return;
            }
            if(root.stage==="album" && !s.browsing && w.page==="album"){
                s.playAlbum(root.importedAlbum,0);root.stage="play";return;
            }
            if(root.stage==="play" && s.loaded && s.position>0.6){
                if(!root.require(s.queue.length===root.vaultIds.length && s.playingTrack.id===root.vaultIds[0],"album queue/order"))return;
                w.showSong();if(!root.require(w.page==="song" && w.goBack() && w.page==="album","song/back routing"))return;
                w.showRecordArtist(root.importedAlbum.artists[0],root.importedAlbum);root.stage="artist";return;
            }
            if(root.stage==="artist"){
                if(!root.require(w.page==="artist" && w.artistData.name===Qt.application.arguments[5],"artist route"))return;
                if(!root.require(w.goBack() && w.page==="album" && w.goBack() && w.page==="library","artist/album/library Back"))return;
                console.log("VINYL_TAGGED_ROUTES_OK");root.stage="listen";return;
            }
            if(root.stage==="listen" && s.loaded && s.position>11 && root.monthSeconds()>=10){
                var codec=s.player.mpvProperty("audio-codec"),output=s.player.getProperty("current-ao");
                if(!root.require(typeof codec==="string" && !!codec && typeof output==="string" && output!=="null","decoder/audio output"))return;
                s.paused=true;s.snapshot();
                console.log("VINYL_TAGGED_PLAY_OK",JSON.stringify({codec:codec,output:output,position:s.position,title:s.playingTrack.title,queue:s.queue.length,listenSeconds:root.monthSeconds()}));
                w.showLibrary();root.captureTicks=0;root.stage="capture-library";return;
            }
            if(root.stage==="capture-library"){
                if(!root.require(w.page==="library","library route did not remain active"))return;
                if(++root.captureTicks>=6)root.capture("library","capture-album-route");return;
            }
            if(root.stage==="capture-album-route"){w.openAlbum(root.importedAlbum.id);root.captureTicks=0;root.stage="capture-album";return;}
            if(root.stage==="capture-album" && !s.browsing && w.page==="album"){
                var cover=root.named(w,"vinylAlbumCover");
                if(++root.captureTicks<6)return;
                if(!cover || !cover.ready){if(root.captureTicks>30)root.fail("album artwork did not render");return;}
                console.log("VINYL_TAGGED_ARTWORK_READY",cover.activeSource);
                root.capture("album","done");return;
            }
            if(root.stage==="restore-album" && !s.browsing && w.page==="album"){
                if(!root.require(s.shownEdition.name===Qt.application.arguments[4] && s.tracks.length===Number(Qt.application.arguments[6]),"restored album presentation"))return;
                s.paused=false;root.stage="resume";return;
            }
            if(root.stage==="resume" && s.loaded && !s.seekPending && s.position>root.resumeFrom+0.5){console.log("VINYL_TAGGED_RESUME_OK",s.position);root.stage="done";return;}
            if(root.stage==="done")Qt.quit();
        }
    }
}
