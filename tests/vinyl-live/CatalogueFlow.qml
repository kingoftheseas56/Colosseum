import QtQuick
import "../../qml"

// Actual Main.qml, native catalogue transport, profile stores and Artist page.
// Args: live|offline, evidence directory. Network results are never fixture-seeded.
Main {
    id: root
    property string stage: "profile"
    property int ticks: 0
    property int warmup: 0
    property int settle: 0
    property int tabIndex: 0
    property string selectedAlbum: ""
    property string expectedVersion: ""
    property var actor: ({})
    property string localTrack: ""
    property real localStart: 0
    readonly property bool offline: Qt.application.arguments[2]==="offline"
    function fail(message){console.error("VINYL_CATALOGUE_FAIL",stage,message);Qt.exit(18);}
    function require(value,message){if(!value){fail(message);return false;}return true;}
    function searchCatalogue(){var w=root.vinylWorld;w.worldData.onlineMode=true;w.worldData.query="Eminem";w.session.searchCatalogue("Eminem");stage="search";}
    function named(item,name){if(item.objectName===name)return item;var children=item.children || [];for(var i=0;i<children.length;++i){var match=named(children[i],name);if(match)return match;}return null;}
    function capture(name,next){
        stage="capturing";
        if(!root.contentItem.grabToImage(function(result){if(!result.saveToFile(Qt.application.arguments[3]+"/"+(root.offline?"offline-":"live-")+name+".png")){root.fail("capture");return;}root.stage=next;}))fail("capture request");
    }
    onStageChanged: console.log("VINYL_CATALOGUE_STAGE",stage)
    Timer {
        interval:150;repeat:true;running:true
        onTriggered:{
            if(++root.ticks>1800){root.fail("timeout");return;}
            if(root.stage==="profile"){
                if(!root.visible || ++root.warmup<4)return;
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                root.openWorld("Vinyl");root.stage="world";return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.session;var source=s.source;
            if(root.stage==="world"){
                if(!root.require(typeof source.openArtist==="function","production catalogue facade is missing"))return;
                if(root.offline && !root.require(s.library.length>0 && s.paused && s.player===null,"saved online album did not survive paused restart"))return;
                if(!root.offline && Qt.application.arguments[4]){w.importLocalFiles([Qt.application.arguments[4]]);root.stage="local-import";return;}
                root.searchCatalogue();return;
            }
            if(root.stage==="local-import" && !s.importing){
                if(!root.require(!s.browseError && s.library.length>0,"local import before catalogue"))return;
                s.playAlbum(s.library[0].payload.album,0);root.stage="local-play";return;
            }
            if(root.stage==="local-play" && s.loaded && s.position>0.5){
                root.localTrack=s.playingTrack.id;root.localStart=s.position;root.searchCatalogue();return;
            }
            if(root.stage==="search" && !s.browsing){
                if(!root.require(!s.browseError && s.onlineArtists.length>0 && s.onlineAlbums.length>0,"real search: "+s.browseError))return;
                root.actor=s.onlineArtists.find(function(a){return a.name==="Eminem";});
                if(!root.require(!!root.actor && root.actor.id.indexOf("ytm:artist:")===0,"provider artist identity"))return;
                console.log("VINYL_CATALOGUE_SEARCH_OK",JSON.stringify({artists:s.onlineArtists.length,albums:s.onlineAlbums.length,notice:s.catalogueNotice}));
                w.showArtist(root.actor);
                if(!root.offline && !w.artistData.saved && !root.require(w.artistData.toggleSaved(),"early artist save"))return;
                root.stage="artist";return;
            }
            if(root.stage==="artist" && !source.artistBusy){
                if(!root.require(w.page==="artist" && source.artist.name==="Eminem" && source.artistAlbums.length>0 && source.artistSingles.length>0,"artist releases: "+source.artistError))return;
                if(!root.require(w.artistData.media.videos.length>0 && w.artistData.liveCards.length>0,"video/live projections"))return;
                if(!root.require(!!source.artistIdentity.mbid,"canonical identity unavailable: "+source.artistError))return;
                if(root.offline && !root.require(source.cached && w.artistData.catalogueStatus.indexOf("cached")>=0,"offline cache is not explicit"))return;
                var bridge=source.canonicalArtistService;
                if(bridge.artist.name!=="Eminem" || bridge.discography.length===0)return;
                if(bridge.nextCursor){if(bridge.discographyState!==1)bridge.requestDiscography(bridge.nextCursor);return;}
                if(!root.require(bridge.discography.length===source.artistIdentity.releaseGroups.length,"canonical pagination is incomplete"))return;
                if(root.localTrack){
                    if(!root.require(s.loaded && !s.paused && s.playingTrack.id===root.localTrack && s.position>root.localStart+0.5,"catalogue interrupted local playback"))return;
                    console.log("VINYL_CATALOGUE_LOCAL_CONTINUITY_OK",s.position);
                }
                if(!root.require(w.artistData.saved && bridge.saved,"artist save did not survive identity enrichment/restart"))return;
                var savedArtists=s.plainData(s.collectionStore.items("Vinyl")).filter(function(e){return e.type==="artist";});
                if(!root.require(savedArtists.length===1 && savedArtists[0].id===root.actor.id && savedArtists[0].payload.externalIds.musicbrainz===source.artistIdentity.mbid,"artist save key/mapping drift"))return;
                console.log("VINYL_CATALOGUE_SAVE_STABLE_OK",savedArtists[0].id);
                console.log("VINYL_CATALOGUE_ARTIST_OK",JSON.stringify({albums:source.artistAlbums.length,singles:source.artistSingles.length,videos:w.artistData.media.videos.length,live:w.artistData.liveCards.length,mbid:source.artistIdentity.mbid,canonicalGroups:bridge.discography.length,cached:source.cached}));
                root.tabIndex=0;root.settle=0;root.stage="tabs";return;
            }
            if(root.stage==="tabs"){
                var artistPage=root.named(w,"vinylArtistPage"),tabs=["albums","singles","videos","live"];
                artistPage.switchTab(tabs[root.tabIndex],true);
                if(++root.settle<10)return;
                root.settle=0;root.capture(tabs[root.tabIndex],"next-tab");return;
            }
            if(root.stage==="next-tab"){
                if(++root.tabIndex<4){root.stage="tabs";return;}
                var release=source.artistAlbums.find(function(a){return (a.providerEditions || []).length>1;}) || source.artistAlbums[0];
                root.selectedAlbum=release.id;w.openAlbum(release.id);root.stage="album";return;
            }
            if(root.stage==="album" && !s.browsing){
                if(!root.require(!s.browseError && w.page==="album" && s.tracks.length>0 && s.shownEdition.origin==="youtube-music","album load: "+s.browseError))return;
                console.log("VINYL_CATALOGUE_ALBUM_OK",JSON.stringify({title:s.shownEdition.name,tracks:s.tracks.length,versions:s.editions.length}));
                if(!s.saved)s.toggleSaved();
                if(!root.offline && s.editions.length>1){root.expectedVersion=s.editions[1].browseId;s.selectEdition(s.editions[1].id);root.stage="version";return;}
                root.capture("album",root.offline?"done":"artist-play-open");return;
            }
            if(root.stage==="version" && !s.browsing){
                if(!root.require(!s.browseError && s.shownEdition.browseId===root.expectedVersion && s.tracks.length>0,"version did not load exact selected provider album"))return;
                console.log("VINYL_CATALOGUE_VERSION_OK",s.shownEdition.browseId,s.tracks.length);
                root.capture("album","artist-play-open");return;
            }
            if(root.stage==="artist-play-open"){w.artistData.toggleDemoPlayback();root.stage="artist-play";return;}
            if(root.stage==="artist-play"){
                if(s.browseError){root.fail("artist play album admission: "+s.browseError);return;}
                if(s.playingEdition.origin!=="youtube-music")return;
                if(!root.require(s.queue.length>0 && s.playingTrack.id!==root.localTrack,"Artist Play did not admit its real album"))return;
                console.log("VINYL_CATALOGUE_ARTIST_PLAY_REQUEST_OK",JSON.stringify({title:s.playingEdition.name,tracks:s.queue.length,state:s.sourceState}));
                s.suspendPlayback("catalogue receiving");root.stage="narrow-open";return;
            }
            if(root.stage==="narrow-open"){w.showArtist("Lifafa");root.stage="narrow";return;}
            if(root.stage==="narrow" && !source.artistBusy){
                if(!root.require(source.artist.name==="Lifafa" && source.artistAlbums.length>0,"narrow artist: "+source.artistError))return;
                console.log("VINYL_CATALOGUE_NARROW_OK",source.artist.name,source.artistAlbums.length,source.artistSingles.length);
                root.capture("narrow","unknown-open");return;
            }
            if(root.stage==="unknown-open"){
                source.openArtist(root.actor);source.openArtist({name:"NoSuchVinylArtistM2_9d7e8a1"});root.stage="unknown";return;
            }
            if(root.stage==="unknown" && !source.artistBusy){
                if(!root.require(!!source.artistError && source.artistAlbums.length===0 && source.artist.name!=="Eminem","unknown artist/late completion fence"))return;
                console.log("VINYL_CATALOGUE_UNKNOWN_OK",source.artistError);root.stage="done";return;
            }
            if(root.stage==="done"){console.log(root.offline?"VINYL_CATALOGUE_OFFLINE_OK":"VINYL_CATALOGUE_LIVE_OK");Qt.quit();}
        }
    }
}
